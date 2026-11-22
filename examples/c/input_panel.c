/*
 * Pt input panel and IME surface: text-input-v3 v2 show/hide requests from an
 * entry, plus the custom-surface IME callbacks and the preedit contract.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/custom.h):
 *
 *  - pt_window_show_input_panel / pt_window_hide_input_panel forward the
 *    text-input-v3 v2 requests for the window's active text-input surface.
 *    They are hints only: the compositor decides whether an on-screen keyboard
 *    appears, and Pt never reports whether it did. In headless mode the desired
 *    state is still recorded.
 *  - Both calls return true only when the recorded desired state changed, so
 *    repeated calls are idempotent and never duplicate a request. A `show`
 *    requested while no text-input object is active is forwarded when IME next
 *    activates, and the remembered state is replayed once on refocus.
 *  - preedit_hint ranges (text-input-v3 v2, since 1.3) are presentation-only
 *    metadata for Pt-drawn entries and text areas: `whole` keeps the plain
 *    1.5 px accent underline, `selection` thickens it to 2.5 px, prefix/suffix/
 *    prediction add a 1.0 px muted line and spelling/compose errors a 2.0 px
 *    danger line. They never change commit text or the composition buffer.
 *    The custom-callback ABI carries preedit text and cursor offsets only, so a
 *    custom surface receives no hint ranges. Type into the entry with a real
 *    text-input-v3 v2 IME to see the hinted underlines; this example cannot
 *    synthesize hints, because that path is compositor-driven and private to
 *    Pt's own preedit renderer.
 *  - pt_window_set_ime_callbacks + pt_window_set_ime_enabled let a custom
 *    surface (a terminal, a canvas) own insertion. The callback owns the
 *    composition buffer, clears it on commit/empty preedit, and never relies on
 *    hints. A done with no commit and no preedit is only an acknowledgement.
 *  - The "Simulate composition" button drives the same local functions the
 *    callbacks use, so the composition preview works in headless CI and on a
 *    compositor without an IME. It is labelled as a local simulation: real
 *    preedit arrives from the compositor.
 */

#include <pt/pt.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct panel_state {
    pt_app *app;
    pt_window *window;
    pt_widget *entry;
    pt_widget *panel_status;
    pt_widget *entry_status;
    pt_widget *preedit_status;
    pt_widget *commit_status;
    pt_widget *custom_button;
    bool panel_shown;
    int show_changes;
    int hide_changes;
    bool simulated_preedit;
    char preedit[128];
    char committed[256];
} panel_state;

static void set_status(pt_widget *label, const char *text) {
    pt_widget_set_text(label, text);
}

static void log_panel(panel_state *state, const char *action, bool changed) {
    state->panel_shown = strcmp(action, "show") == 0 ? true : state->panel_shown;
    if (strcmp(action, "hide") == 0) {
        state->panel_shown = false;
    }
    char text[192];
    if (changed) {
        snprintf(text, sizeof(text), "Panel: %s requested (desired state changed; the "
                                     "compositor decides whether an OSK appears)",
                 action);
    } else {
        snprintf(text, sizeof(text),
                 "Panel: %s ignored (already in that desired state; no duplicate request)",
                 action);
    }
    set_status(state->panel_status, text);
    printf("input_panel: %s changed=%d desired=%s\n", action, changed ? 1 : 0,
           state->panel_shown ? "shown" : "hidden");
    fflush(stdout);
}

static void on_show_panel(pt_widget *button, void *user_data) {
    (void)button;
    panel_state *state = user_data;
    bool changed = pt_window_show_input_panel(state->window);
    if (changed) {
        ++state->show_changes;
    }
    log_panel(state, "show", changed);
}

static void on_hide_panel(pt_widget *button, void *user_data) {
    (void)button;
    panel_state *state = user_data;
    bool changed = pt_window_hide_input_panel(state->window);
    if (changed) {
        ++state->hide_changes;
    }
    log_panel(state, "hide", changed);
}

static void on_entry_change(pt_widget *entry, const char *text, void *user_data) {
    (void)entry;
    panel_state *state = user_data;
    char status[256];
    snprintf(status, sizeof(status), "Entry text: \"%.160s\" (IME-driven; Pt owns insertion)",
             text ? text : "");
    set_status(state->entry_status, status);
}

/* Custom-surface callbacks: Pt calls these only while the custom IME surface is
 * active, i.e. while no text-entry widget holds editable focus. Text is
 * borrowed for the call; copy what you need. */
static void on_ime_commit(pt_window *window, const char *text, void *user_data) {
    (void)window;
    panel_state *state = user_data;
    if (!text) {
        text = "";
    }
    size_t used = strlen(state->committed);
    size_t room = sizeof(state->committed) - used - 1;
    size_t take = strlen(text) < room ? strlen(text) : room;
    if (take > 0) {
        memcpy(state->committed + used, text, take);
        state->committed[used + take] = '\0';
    }
    state->preedit[0] = '\0';
    state->simulated_preedit = false;
    char status[320];
    snprintf(status, sizeof(status), "Custom surface committed \"%s\"; buffer: \"%s\"", text,
             state->committed);
    set_status(state->commit_status, status);
    set_status(state->preedit_status, "Preedit: (none) - a commit clears the local buffer");
    printf("input_panel: custom commit \"%s\" buffer=\"%s\"\n", text, state->committed);
    fflush(stdout);
}

static void on_ime_preedit(pt_window *window, const char *text, int32_t cursor_begin,
                           int32_t cursor_end, void *user_data) {
    (void)window;
    panel_state *state = user_data;
    if (!text) {
        text = "";
    }
    snprintf(state->preedit, sizeof(state->preedit), "%s", text);
    state->simulated_preedit = text[0] != '\0';
    char status[256];
    if (text[0] == '\0') {
        snprintf(status, sizeof(status), "Preedit: (none) - empty preedit ends the composition");
    } else {
        snprintf(status, sizeof(status),
                 "Preedit: \"%.64s\" cursor %d..%d (app-drawn; hints stay compositor-side)",
                 text, (int)cursor_begin, (int)cursor_end);
    }
    set_status(state->preedit_status, status);
    printf("input_panel: custom preedit \"%s\" cursor=%d..%d\n", text, (int)cursor_begin,
           (int)cursor_end);
    fflush(stdout);
}

/* Local simulation drives the same functions as the compositor would, without
 * pretending to be an IME: the second click commits the composed text. */
static void on_simulate(pt_widget *button, void *user_data) {
    (void)button;
    panel_state *state = user_data;
    if (!state->simulated_preedit) {
        on_ime_preedit(state->window, "\xE3\x81\xAB\xE3\x81\xBB\xE3\x82\x93" /* にほん */,
                       9, 9, state);
        state->simulated_preedit = true;
        char text[192];
        snprintf(text, sizeof(text),
                 "Local simulation: preedit injected without an IME; click again to commit");
        set_status(state->preedit_status, text);
    } else {
        on_ime_commit(state->window, "\xE6\x97\xA5\xE6\x9C\xAC" /* 日本 */, state);
    }
}

static void on_focus_entry(pt_widget *button, void *user_data) {
    (void)button;
    panel_state *state = user_data;
    pt_widget_focus(state->entry);
    set_status(state->preedit_status,
               "Entry holds editable focus: the entry's IME owns composition and hint rendering");
}

static void on_focus_custom(pt_widget *button, void *user_data) {
    (void)button;
    panel_state *state = user_data;
    pt_widget_focus(state->custom_button);
    set_status(state->preedit_status,
               "Custom surface holds focus: commit/preedit go to the callbacks below");
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.input_panel");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt Input Panel and IME", 720, 620);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    panel_state state = {.app = app, .window = window};
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "IME  ·  INPUT PANEL  ·  TEXT-INPUT v3 v2");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    pt_widget *heading = pt_label_create(root, "Show or hide the on-screen keyboard");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "Both buttons forward text-input-v3 v2 requests for this window. The return value "
              "reports only whether the recorded desired state changed, never whether the "
              "compositor showed a panel. Click twice to see the idempotent second call.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(card, 12.0f);

    state.entry = pt_entry_create(card, "Type here - a real IME shows hinted underlines");
    pt_entry_set_on_change(state.entry, on_entry_change, &state);
    pt_widget_set_a11y_name(state.entry, "Input panel text entry");

    pt_widget *buttons = pt_row_create(card);
    pt_widget_set_gap(buttons, 10.0f);
    pt_widget *show = pt_button_create(buttons, "Show input panel");
    pt_button_set_variant(show, pt_button_primary);
    pt_button_set_on_click(show, on_show_panel, &state);
    pt_widget_set_tooltip(show, "zwp_text_input_v3.show_input_panel, a hint only");
    pt_widget *hide = pt_button_create(buttons, "Hide input panel");
    pt_button_set_variant(hide, pt_button_secondary);
    pt_button_set_on_click(hide, on_hide_panel, &state);
    pt_widget_set_tooltip(hide, "zwp_text_input_v3.hide_input_panel, a hint only");

    state.panel_status = pt_label_create(
        card, "Panel: hidden (initial desired state). Headless runs only record this state.");
    pt_widget_set_font_size(state.panel_status, 12.0f);
    pt_widget_set_foreground(state.panel_status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.panel_status, true);

    state.entry_status = pt_label_create(card, "Entry text: \"\"");
    pt_widget_set_font_size(state.entry_status, 12.0f);
    pt_widget_set_foreground(state.entry_status, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(state.entry_status, true);

    pt_widget *custom_card = pt_card_create(root);
    pt_widget_set_padding(custom_card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(custom_card, 10.0f);

    pt_widget *custom_heading = pt_label_create(custom_card, "Custom IME surface");
    pt_widget_set_font_size(custom_heading, 16.0f);
    pt_widget_set_font_weight(custom_heading, pt_font_weight_semibold);

    pt_widget *custom_note = pt_label_create(
        custom_card, "When no entry holds editable focus, Pt sends commit/preedit to these "
                     "callbacks and the surface owns insertion. The ABI carries text and cursor "
                     "offsets only; preedit_hint ranges never reach a custom surface.");
    pt_widget_set_font_size(custom_note, 12.0f);
    pt_widget_set_foreground(custom_note, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(custom_note, true);

    pt_widget *focus_row = pt_row_create(custom_card);
    pt_widget_set_gap(focus_row, 10.0f);
    pt_widget *focus_entry = pt_button_create(focus_row, "Focus entry");
    pt_button_set_variant(focus_entry, pt_button_ghost);
    pt_button_set_on_click(focus_entry, on_focus_entry, &state);
    state.custom_button = pt_button_create(focus_row, "Focus custom surface");
    pt_button_set_variant(state.custom_button, pt_button_secondary);
    pt_button_set_on_click(state.custom_button, on_focus_custom, &state);
    pt_widget *simulate = pt_button_create(focus_row, "Simulate composition");
    pt_button_set_variant(simulate, pt_button_secondary);
    pt_button_set_on_click(simulate, on_simulate, &state);
    pt_widget_set_tooltip(simulate,
                          "Local preedit/commit injection, not a synthetic key or IME");

    state.preedit_status = pt_label_create(
        custom_card, "Preedit: (none) - a real composition is compositor-driven");
    pt_widget_set_font_size(state.preedit_status, 12.0f);
    pt_widget_set_foreground(state.preedit_status, pt_rgb(0xE0AF68));
    pt_widget_set_label_wrap(state.preedit_status, true);

    state.commit_status = pt_label_create(custom_card, "Custom surface buffer: \"\"");
    pt_widget_set_font_size(state.commit_status, 12.0f);
    pt_widget_set_foreground(state.commit_status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.commit_status, true);

    /* Install the custom surface and keep it enabled. While the entry has
     * editable focus Pt restores the entry's IME; the custom path activates as
     * soon as focus leaves every text input. */
    pt_window_set_ime_callbacks(window, on_ime_commit, on_ime_preedit, &state);
    pt_window_set_ime_enabled(window, true);
    pt_window_set_ime_cursor_rect(window, 24, 360, 2, 20);

    pt_widget *footer = pt_label_create(
        root, "Under text-input v3 v1 the requests are dropped. Under v3 v2 a compositor may "
              "still ignore them: Pt reports the intent, not the OSK. The remembered show/hide "
              "state is replayed once after a focus round-trip.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(state.entry);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        on_show_panel(NULL, &state);
        on_show_panel(NULL, &state);
        on_hide_panel(NULL, &state);
        on_hide_panel(NULL, &state);
        on_focus_custom(NULL, &state);
        on_simulate(NULL, &state);
        on_simulate(NULL, &state);
        printf("input_panel: headless show_changes=%d hide_changes=%d committed=\"%s\"\n",
               state.show_changes, state.hide_changes, state.committed);
        fflush(stdout);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}