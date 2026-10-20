/*
 * Pt accessibility model and AT-SPI bridge example.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct a11y_state {
    pt_app *app;
    pt_widget *progress;
    pt_widget *notify;
    pt_widget *announcement;
    int uploads;
} a11y_state;

static const char *event_name(pt_a11y_event_type event) {
    switch (event) {
    case pt_a11y_event_focus_changed:
        return "focus_changed";
    case pt_a11y_event_state_changed:
        return "state_changed";
    case pt_a11y_event_text_changed:
        return "text_changed";
    case pt_a11y_event_selection_changed:
        return "selection_changed";
    case pt_a11y_event_value_changed:
        return "value_changed";
    case pt_a11y_event_name_changed:
        return "name_changed";
    case pt_a11y_event_structure_changed:
        return "structure_changed";
    }
    return "unknown";
}

static const char *role_name(pt_a11y_role role) {
    switch (role) {
    case pt_a11y_role_push_button:
        return "push_button";
    case pt_a11y_role_label:
        return "label";
    case pt_a11y_role_check_box:
        return "check_box";
    case pt_a11y_role_progress_bar:
        return "progress_bar";
    case pt_a11y_role_slider:
        return "slider";
    case pt_a11y_role_text:
        return "text";
    case pt_a11y_role_text_frame:
        return "text_frame";
    case pt_a11y_role_panel:
        return "panel";
    default:
        return "generic";
    }
}

static const char *status_name(pt_a11y_status status) {
    switch (status) {
    case pt_a11y_status_active:
        return "active (AT-SPI bridge connected)";
    case pt_a11y_status_unavailable:
        return "unavailable (no accessibility bus)";
    case pt_a11y_status_disabled:
        return "disabled";
    }
    return "unknown";
}

static void on_a11y_event(pt_window *window, pt_widget *widget, pt_a11y_event_type event,
                          void *data) {
    (void)window;
    (void)data;
    if (!widget) {
        printf("a11y event: %s (window)\n", event_name(event));
        return;
    }
    const char *name = pt_widget_get_a11y_name(widget);
    printf("a11y event: %s on \"%s\" (%s)\n", event_name(event),
           name && *name ? name : "unnamed", role_name(pt_widget_get_a11y_role(widget)));
    fflush(stdout);
}

static void announce(a11y_state *state, const char *text) {
    pt_widget_set_text(state->announcement, text);
    pt_widget_set_a11y_description(state->announcement, text);
}

static void on_upload(pt_widget *button, void *data) {
    (void)button;
    a11y_state *state = data;
    ++state->uploads;
    float progress = state->uploads >= 3 ? 1.0f : 0.25f + 0.25f * (float)state->uploads;
    pt_progress_set_value(state->progress, progress);
    char message[96];
    snprintf(message, sizeof(message), "Upload %d of 3 complete", state->uploads);
    announce(state, message);
}

static void on_notify(pt_widget *toggle, float checked, void *data) {
    (void)toggle;
    a11y_state *state = data;
    announce(state, checked != 0.0f ? "Notifications enabled" : "Notifications muted");
}

static bool on_key(pt_window *window, uint32_t key, uint32_t modifiers, void *data) {
    (void)window;
    (void)modifiers;
    if (key == 0xFF1Bu) {
        pt_app_quit((pt_app *)data);
        return true;
    }
    return false;
}

static void on_quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

static void simulate_headless(pt_app *app, void *data) {
    (void)app;
    on_upload(NULL, data);
}

static void quit_timer(pt_app *app, void *data) {
    (void)data;
    pt_app_quit(app);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.accessibility");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt accessibility", 720, 560);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 28.0f, 28.0f, 28.0f, 28.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *heading = pt_label_create(root, "Accessible by construction");
    pt_widget_set_font_size(heading, 30.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);
    pt_widget_set_a11y_role(heading, pt_a11y_role_label);

    pt_widget *intro = pt_label_create(
        root, "Widgets derive roles, names and states from their kind and content. "
              "Override them when the visible label is not enough, and enable the "
              "AT-SPI bridge to expose the live tree to screen readers.");
    pt_widget_set_label_wrap(intro, true);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA4B2));

    pt_widget *card = pt_card_create(root);
    pt_widget_set_gap(card, 12.0f);
    pt_widget_set_padding(card, 20.0f, 20.0f, 20.0f, 20.0f);

    pt_widget *entry = pt_entry_create(card, "Display name");
    pt_widget_set_text(entry, "Ada Lovelace");
    pt_widget_set_a11y_name(entry, "Display name");
    pt_widget_set_a11y_description(
        entry, "Shown next to your uploads; exposed with caret and selection offsets.");

    pt_widget *notify = pt_checkbox_create(card, "Notify me when an upload finishes");
    pt_widget_set_checked(notify, true);
    pt_widget_set_a11y_description(notify, "Sends a toast when the upload queue drains.");

    pt_widget *progress = pt_progress_create(card);
    pt_progress_set_value(progress, 0.25f);
    pt_widget_set_a11y_name(progress, "Upload progress");
    pt_widget_set_a11y_description(progress, "Fraction of the current upload that is complete.");
    pt_widget_set_a11y_role(progress, pt_a11y_role_progress_bar);

    pt_widget *actions = pt_row_create(card);
    pt_widget_set_gap(actions, 10.0f);
    pt_widget *upload = pt_button_create(actions, "Upload next item");
    pt_widget_set_a11y_description(upload, "Advances the simulated upload by one quarter.");
    pt_widget *quit = pt_button_create(actions, "Quit");
    pt_button_set_variant(quit, pt_button_secondary);
    pt_button_set_on_click(quit, on_quit, app);

    pt_widget *announcement = pt_label_create(root, "Ready.");
    pt_widget_set_font_size(announcement, 13.0f);
    pt_widget_set_foreground(announcement, pt_rgb(0x6F7C8F));
    pt_widget_set_a11y_name(announcement, "Status announcement");
    pt_widget_set_a11y_role(announcement, pt_a11y_role_label);

    pt_widget *footer = pt_label_create(
        root, "Tab moves focus between controls. The console prints the same "
              "accessibility events the AT-SPI bridge publishes.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    a11y_state state = {
        .app = app,
        .progress = progress,
        .notify = notify,
        .announcement = announcement,
        .uploads = 0,
    };
    pt_button_set_on_click(upload, on_upload, &state);
    pt_widget_set_on_checked(notify, on_notify, &state);
    pt_app_set_a11y_callback(app, on_a11y_event, &state);
    pt_window_set_key_callback(window, on_key, app);

    pt_window_show(window);
    bool enabled = pt_app_enable_a11y(app);
    printf("a11y status: %s (enable returned %s)\n", status_name(pt_app_get_a11y_status(app)),
           enabled ? "true" : "false");

    const char *text = NULL;
    uint32_t caret = 0;
    uint32_t start = 0;
    uint32_t end = 0;
    if (pt_widget_get_a11y_text(entry, &text, &caret, &start, &end)) {
        printf("entry a11y text: \"%s\" caret=%u selection=%u..%u\n", text, caret, start,
               end);
    }
    float value = 0.0f;
    if (pt_widget_get_a11y_value(progress, &value)) {
        printf("progress a11y value: %.2f\n", value);
    }
    fflush(stdout);

    const char *headless = getenv("PT_HEADLESS");
    if (headless && *headless && strcmp(headless, "0") != 0) {
        pt_app_add_timeout(app, 400, simulate_headless, &state);
        pt_app_add_timeout(app, 1200, simulate_headless, &state);
        pt_app_add_timeout(app, 2000, quit_timer, NULL);
    }

    int status = pt_app_run(app);
    printf("a11y events delivered; final status: %s\n",
           status_name(pt_app_get_a11y_status(app)));
    pt_app_destroy(app);
    return status;
}
