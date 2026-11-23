/*
 * Pt ergonomics gallery - C, the 2.1.0 RTL ergonomics and safety surface.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/base.h, app.h, window.h, widget.h,
 * bidi.h):
 *
 *  - pt_caret_movement (inherit/visual/logical) is a policy, not a new editing
 *    model. Resolution is widget -> nearest explicit ancestor -> window -> app
 *    -> visual, and inherit is the zero value, so an application that never
 *    sets it keeps the 2.0 visual arrows.
 *  - pt_locale_ui_direction is a bounded, ICU-free BCP-47/POSIX lookup. The
 *    script subtag wins over the language, so az-Arab is RTL while az-Latn is
 *    LTR. pt_app_set_locale stores the result as the application fallback; an
 *    explicit pt_app_set_ui_direction still wins in either call order.
 *  - pt_widget_set_padding_logical takes top/start/bottom/end and resolves
 *    start/end from the resolved UI direction on every layout. The physical
 *    setter stays and the last call wins; margin stays physical.
 *  - Untrusted text is scanned with pt_bidi_control_inventory and mitigated
 *    with pt_bidi_sanitize. Neither runs automatically: the stored bytes of
 *    any other text path are unchanged.
 *
 * Under PT_HEADLESS the example runs every setter, prints the getter values
 * and the sanitized byte length, then exits through pt_app_run_for, so it is a
 * smoke test as well as a visual demo.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A hostile string: an RLO override (U+202E) smuggles a reversed suffix and an
 * FSI/PDI pair wraps it, the Trojan-Source trick. The trailing part is the
 * remote the reader believes they are looking at. */
static const char untrusted_text[] =
    "invoice"
    "\xE2\x80\xAE"          /* U+202E RIGHT-TO-LEFT OVERRIDE */
    "txt.exe"
    "\xE2\x81\xA8"          /* U+2068 FIRST STRONG ISOLATE */
    "user@example.com"
    "\xE2\x81\xA9";         /* U+2069 POP DIRECTIONAL ISOLATE */

static const char hebrew_label[] =
    "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D"; /* Shalom olam */

typedef struct ergonomics_state {
    pt_app *app;
    pt_window *window;
    pt_widget *source_editor;
    pt_widget *shell_entry;
    pt_widget *logical_card;
    pt_widget *status;
    pt_caret_movement movement;
    pt_ui_direction shell;
} ergonomics_state;

static void report(const char *message) {
    printf("ergonomics_gallery: %s\n", message);
    fflush(stdout);
}

static void set_caret(ergonomics_state *state, pt_caret_movement movement) {
    state->movement = movement;
    /* The app value is the last fallback; the window repeats it to exercise
     * the per-window level, and the source editor states its own policy. */
    pt_app_set_caret_movement(state->app, movement);
    pt_window_set_caret_movement(state->window, movement);
    pt_widget_set_caret_movement(state->source_editor, pt_caret_movement_logical);
    pt_widget_set_caret_movement(state->shell_entry, pt_caret_movement_inherit);

    char line[200];
    snprintf(line, sizeof(line),
             "caret=%s: app=%d window=%d editor=%d entry=%d",
             movement == pt_caret_movement_logical ? "logical" : "visual",
             (int)pt_app_get_caret_movement(state->app),
             (int)pt_window_get_caret_movement(state->window),
             (int)pt_widget_get_caret_movement(state->source_editor),
             (int)pt_widget_get_caret_movement(state->shell_entry));
    report(line);
    pt_widget_set_text(state->status, line);
}

static void set_shell(ergonomics_state *state, pt_ui_direction direction) {
    state->shell = direction;
    pt_app_set_ui_direction(state->app, direction);
    pt_window_set_ui_direction(state->window, direction);
    /* No padding call follows: the logical padding below re-resolves from the
     * new direction on the next layout. */
    char line[160];
    snprintf(line, sizeof(line), "shell=%s: window=%d",
             direction == pt_ui_direction_rtl ? "rtl" : "ltr",
             (int)pt_window_get_ui_direction(state->window));
    report(line);
}

static void on_visual(pt_widget *button, void *data) {
    (void)button;
    set_caret((ergonomics_state *)data, pt_caret_movement_visual);
}

static void on_logical(pt_widget *button, void *data) {
    (void)button;
    set_caret((ergonomics_state *)data, pt_caret_movement_logical);
}

static void on_ltr(pt_widget *button, void *data) {
    (void)button;
    set_shell((ergonomics_state *)data, pt_ui_direction_ltr);
}

static void on_rtl(pt_widget *button, void *data) {
    (void)button;
    set_shell((ergonomics_state *)data, pt_ui_direction_rtl);
}

static void on_quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

/* Prints the locale lookup table and the inventory/sanitize result. Runs once
 * at startup and again in headless mode, so the demo is also its own oracle. */
static void report_safety(const char *untrusted) {
    size_t length = strlen(untrusted);
    size_t count = pt_bidi_control_inventory(untrusted, length, NULL, 0);
    pt_bidi_control_span spans[8];
    size_t written = pt_bidi_control_inventory(untrusted, length, spans,
                                               count > 8 ? 8 : count);
    printf("ergonomics_gallery: locale ar=%d he=%d fa=%d az-Arab=%d az-Latn=%d C=%d\n",
           (int)pt_locale_ui_direction("ar_EG.UTF-8"),
           (int)pt_locale_ui_direction("he"),
           (int)pt_locale_ui_direction("fa_IR"),
           (int)pt_locale_ui_direction("az-Arab"),
           (int)pt_locale_ui_direction("az-Latn"),
           (int)pt_locale_ui_direction("C"));
    printf("ergonomics_gallery: inventory controls=%zu first=(%zu,%zu,kind=%d) "
           "capacity_truncated_at=%zu\n",
           count, written ? spans[0].offset : 0, written ? spans[0].length : 0,
           written ? (int)spans[0].kind : 0, written);

    size_t stripped_length = 0;
    char *stripped = pt_bidi_sanitize(untrusted, length, pt_bidi_sanitize_strip,
                                      &stripped_length);
    size_t neutral_length = 0;
    char *neutral = pt_bidi_sanitize(untrusted, length,
                                     pt_bidi_sanitize_neutralize, &neutral_length);
    printf("ergonomics_gallery: sanitize input=%zu strip=%zu neutralize=%zu\n",
           length, stripped_length, neutral_length);
    pt_free(stripped);
    pt_free(neutral);
    fflush(stdout);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.ergonomics_gallery");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    /* A locale hint, not a guess: the caller resolved the locale already and
     * the helper maps it. Pt never reads the environment. */
    pt_app_set_locale(app, "he_IL.UTF-8");
    const char *hint = pt_app_get_locale(app);
    printf("ergonomics_gallery: locale hint=%s->%d\n", hint ? hint : "(none)",
           (int)pt_locale_ui_direction(hint));
    fflush(stdout);

    pt_window *window = pt_window_create(app, "Pt Ergonomics Gallery", 860, 700);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    ergonomics_state state = {
        .app = app,
        .window = window,
        .movement = pt_caret_movement_visual,
        .shell = pt_ui_direction_rtl,
    };

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "2.1  ·  CARET POLICY  ·  LOCALE  ·  LOGICAL PADDING  ·  BIDI SAFETY");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    pt_widget *title = pt_label_create(root, hebrew_label);
    pt_widget_set_font_size(title, 30.0f);
    pt_widget_set_font_weight(title, pt_font_weight_bold);
    pt_widget_set_text_direction(title, pt_text_direction_auto);

    pt_widget *intro = pt_label_create(
        root, "The editor below moves logically, the entry beside it keeps the visual 2.0 "
              "default, and neither needs a key callback. The card pads start/end, so the "
              "gap follows the shell direction.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    /* Caret policy: a row of switches plus the two editors. */
    pt_widget *caret_card = pt_card_create(root);
    pt_widget_set_padding(caret_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(caret_card, 10.0f);

    pt_widget *caret_row = pt_row_create(caret_card);
    pt_widget_set_gap(caret_row, 10.0f);
    pt_widget_set_align_self(caret_row, pt_align_center);

    pt_widget *caret_label = pt_label_create(caret_row, "Caret movement");
    pt_widget_set_font_size(caret_label, 14.0f);
    pt_widget_set_font_weight(caret_label, pt_font_weight_semibold);
    pt_widget_set_grow(caret_label, 1.0f);

    pt_widget *visual_button = pt_button_create(caret_row, "Visual (2.0)");
    pt_button_set_variant(visual_button, pt_button_ghost);
    pt_button_set_on_click(visual_button, on_visual, &state);

    pt_widget *logical_button = pt_button_create(caret_row, "Logical");
    pt_button_set_variant(logical_button, pt_button_primary);
    pt_button_set_on_click(logical_button, on_logical, &state);

    pt_widget *editor_label = pt_label_create(caret_card, "Source editor: explicit logical policy");
    pt_widget_set_font_size(editor_label, 12.0f);
    pt_widget_set_foreground(editor_label, pt_rgb(0x9AA7B8));

    state.source_editor = pt_entry_create(caret_card, "identifier");
    pt_widget_set_text(state.source_editor, "shalom_el(marhaba, 123);");
    pt_widget_set_size(state.source_editor, 460.0f, 36.0f);
    pt_widget_set_ui_direction(state.source_editor, pt_ui_direction_ltr);
    pt_widget_set_text_direction(state.source_editor, pt_text_direction_ltr);
    pt_widget_set_caret_movement(state.source_editor, pt_caret_movement_logical);
    pt_widget_set_align_self(state.source_editor, pt_align_start);

    pt_widget *entry_label = pt_label_create(caret_card, "RTL entry: inherited policy");
    pt_widget_set_font_size(entry_label, 12.0f);
    pt_widget_set_foreground(entry_label, pt_rgb(0x9AA7B8));

    state.shell_entry = pt_entry_create(caret_card, "Type here");
    pt_widget_set_text(state.shell_entry, hebrew_label);
    pt_widget_set_size(state.shell_entry, 460.0f, 36.0f);
    pt_widget_set_ui_direction(state.shell_entry, pt_ui_direction_rtl);
    pt_widget_set_text_direction(state.shell_entry, pt_text_direction_rtl);
    pt_widget_set_align_self(state.shell_entry, pt_align_start);

    /* Logical padding: top/start/bottom/end. In RTL the content begins at the
     * right, so start is the wide edge there and the narrow one in LTR. */
    state.logical_card = pt_card_create(root);
    pt_widget_set_padding_logical(state.logical_card, 14.0f, 40.0f, 14.0f, 8.0f);
    pt_widget_set_gap(state.logical_card, 8.0f);

    pt_widget *pad_title = pt_label_create(
        state.logical_card, "Logical padding: start 40, end 8 (marginless)");
    pt_widget_set_font_size(pad_title, 13.0f);
    pt_widget_set_font_weight(pad_title, pt_font_weight_semibold);
    pt_widget_set_label_wrap(pad_title, true);

    pt_widget *pad_row = pt_row_create(state.logical_card);
    pt_widget_set_gap(pad_row, 10.0f);
    pt_widget_set_align_self(pad_row, pt_align_center);

    pt_widget *ltr_button = pt_button_create(pad_row, "LTR shell");
    pt_button_set_variant(ltr_button, pt_button_ghost);
    pt_button_set_on_click(ltr_button, on_ltr, &state);

    pt_widget *rtl_button = pt_button_create(pad_row, "RTL shell");
    pt_button_set_variant(rtl_button, pt_button_primary);
    pt_button_set_on_click(rtl_button, on_rtl, &state);

    pt_widget *pad_note = pt_label_create(
        state.logical_card,
        "The setter is never called again: the layout prepass re-resolves start/end "
        "from the resolved UI direction on every layout.");
    pt_widget_set_font_size(pad_note, 12.0f);
    pt_widget_set_foreground(pad_note, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(pad_note, true);

    /* Bidi safety: an untrusted string, inventoried first and previewed only
     * in its sanitized form. The stored text never contains the controls. */
    pt_widget *safety_card = pt_card_create(root);
    pt_widget_set_padding(safety_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(safety_card, 8.0f);

    pt_widget *safety_title = pt_label_create(
        safety_card, "Untrusted text: pt_bidi_control_inventory + pt_bidi_sanitize");
    pt_widget_set_font_size(safety_title, 13.0f);
    pt_widget_set_font_weight(safety_title, pt_font_weight_semibold);
    pt_widget_set_label_wrap(safety_title, true);

    size_t untrusted_length = strlen(untrusted_text);
    size_t neutral_length = 0;
    char *preview = pt_bidi_sanitize(untrusted_text, untrusted_length,
                                     pt_bidi_sanitize_neutralize, &neutral_length);
    if (preview) {
        char line[192];
        snprintf(line, sizeof(line),
                 "Neutralized preview (%zu controls replaced by U+FFFD): %s",
                 pt_bidi_control_inventory(untrusted_text, untrusted_length, NULL, 0),
                 preview);
        pt_widget *preview_label = pt_label_create(safety_card, line);
        pt_widget_set_font_size(preview_label, 13.0f);
        pt_widget_set_foreground(preview_label, pt_rgb(0xF7768E));
        pt_widget_set_label_wrap(preview_label, true);
        pt_free(preview);
    }

    pt_widget *safety_note = pt_label_create(
        safety_card,
        "Nothing is sanitized automatically: the security decision belongs to the "
        "application that accepts the text.");
    pt_widget_set_font_size(safety_note, 12.0f);
    pt_widget_set_foreground(safety_note, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(safety_note, true);

    state.status = pt_label_create(root, "");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    set_shell(&state, pt_ui_direction_rtl);
    set_caret(&state, pt_caret_movement_visual);
    report_safety(untrusted_text);

    pt_window_show(window);
    pt_widget_focus(state.shell_entry);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        set_caret(&state, pt_caret_movement_logical);
        set_shell(&state, pt_ui_direction_ltr);
        set_shell(&state, pt_ui_direction_rtl);
        report_safety(untrusted_text);
        printf("ergonomics_gallery: headless locale=%s movement=%d shell=%d\n",
               pt_app_get_locale(app) ? pt_app_get_locale(app) : "(none)",
               (int)pt_app_get_caret_movement(app),
               (int)pt_window_get_ui_direction(window));
        fflush(stdout);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}
