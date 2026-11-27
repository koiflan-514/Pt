/*
 * Pt protocol extensions: xdg-activation-v1 and xdg-foreign-unstable-v2.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/window.h):
 *
 *  - pt_window_request_activation_token creates an asynchronous token object
 *    and delivers one borrowed token string to the callback. A launcher copies
 *    the string and passes it to the child out of band.
 *  - pt_window_activate sends a token once and retains nothing. A true return
 *    means the request was sent, never that the compositor moved focus.
 *  - pt_window_export_foreign_handle creates an export object and delivers one
 *    opaque borrowed handle. The export stays alive until unexport or window
 *    destruction, and re-exporting invalidates the old handle.
 *  - pt_window_set_foreign_parent imports the handle and gives the window the
 *    same stacking semantics as xdg_toplevel.set_parent. It replaces a previous
 *    parent; an invalid handle is cleared silently on the compositor's
 *    destroyed event.
 *  - unexport and clear are idempotent no-ops when nothing is exported or
 *    imported. Window destruction releases both sides.
 *
 * Without the protocol globals, or on a headless platform, every call returns
 * false (or no-ops) before creating an object. The application needs no branch:
 * PT_HEADLESS=1 asserts that fallback, the headless-safe teardown and exit 0.
 * The live path below is exercised only when a compositor offers the globals.
 */

#include <pt/pt.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct demo_state {
    pt_window *dialog;
    bool token_seen;
    bool handle_seen;
    char token[256];
} demo_state;

static void on_token(pt_window *window, const char *token, void *user_data) {
    demo_state *state = user_data;
    state->token_seen = true;
    snprintf(state->token, sizeof state->token, "%s", token ? token : "");
    /* The token is borrowed for this call. A real launcher copies it and gives
     * the copy to the child process; this loopback activates in place. */
    if (state->token[0] != '\0') {
        pt_window_activate(window, state->token);
    }
}

static void on_handle(pt_window *exporter, const char *handle, void *user_data) {
    demo_state *state = user_data;
    (void)exporter;
    state->handle_seen = handle && handle[0] != '\0';
    /* The handle is opaque and borrowed. Import it before returning; another
     * process would receive the same string out of band. */
    if (state->handle_seen) {
        pt_window_set_foreign_parent(state->dialog, handle);
    }
}

static void add_text(pt_widget *parent, const char *text, float size, pt_color color,
                     bool wrap) {
    pt_widget *label = pt_label_create(parent, text);
    pt_widget_set_font_size(label, size);
    pt_widget_set_foreground(label, color);
    pt_widget_set_label_wrap(label, wrap);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.protocol_extensions");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt Protocol Extensions", 720, 420);
    pt_window *dialog = pt_window_create(app, "Pt Foreign Dialog", 420, 240);
    if (!window || !dialog) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
    pt_widget_set_gap(root, 10.0f);

    pt_widget *eyebrow =
        pt_label_create(root, "ACTIVATION  ·  XDG-FOREIGN-V2  ·  BEST EFFORT");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    add_text(root,
             "The window requests a one-shot activation token, activates with the "
             "borrowed string, exports a foreign toplevel handle and imports it on "
             "the second window.",
             13.0f, pt_rgb(0x9AA7B8), true);
    add_text(root,
             "A token or handle callback is valid only during the call. True from "
             "activate means sent, not focused, and the handle is never parsed.",
             12.0f, pt_rgb(0x6F7C8F), true);

    demo_state state = {0};
    state.dialog = dialog;

    pt_window_show(window);
    pt_window_show(dialog);

    const char *headless = getenv("PT_HEADLESS");
    bool headless_mode = headless && *headless && strcmp(headless, "0") != 0;

    int failures = 0;
    int run_status;
    if (headless_mode) {
        /* No compositor: no global is bound, so every call is the documented
         * false/no-op fallback and no callback may run. */
        bool token_request = pt_window_request_activation_token(
            window, "dev.pt.examples.protocol_extensions", on_token, &state);
        bool activated = pt_window_activate(window, "headless-token");
        bool exported = pt_window_export_foreign_handle(window, on_handle, &state);
        pt_window_unexport_foreign_handle(window);
        pt_window_unexport_foreign_handle(window);
        bool parented = pt_window_set_foreign_parent(dialog, "headless-handle");
        pt_window_clear_foreign_parent(dialog);
        pt_window_clear_foreign_parent(dialog);

        if (token_request || activated || exported || parented) {
            failures++;
        }
        if (state.token_seen || state.handle_seen) {
            failures++;
        }
        printf("protocol_extensions: headless request=%d activate=%d export=%d "
               "parent=%d callbacks=%d/%d failures=%d\n",
               (int)token_request, (int)activated, (int)exported, (int)parented,
               (int)state.token_seen, (int)state.handle_seen, failures);
        fflush(stdout);
        run_status = pt_app_run_for(app, 250);
    } else {
        /* Live path: the callbacks copy the borrowed strings and use them. The
         * requests stay pending if the compositor has no such global. */
        bool token_request = pt_window_request_activation_token(
            window, "dev.pt.examples.protocol_extensions", on_token, &state);
        bool exported = pt_window_export_foreign_handle(window, on_handle, &state);
        printf("protocol_extensions: live request=%d export=%d\n",
               (int)token_request, (int)exported);
        fflush(stdout);
        run_status = pt_app_run(app);
    }

    pt_app_destroy(app);
    return failures ? 1 : run_status;
}