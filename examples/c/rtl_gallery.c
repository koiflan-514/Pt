/*
 * Pt RTL gallery: mixed Arabic/Hebrew/Latin text, a mirrored RTL layout, an
 * RTL entry and text area, a nested LTR island and a live direction switch.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/base.h, widget.h, window.h, app.h):
 *
 *  - pt_ui_direction (inherit/ltr/rtl) orders rows, resolves start/end
 *    alignment, moves scrollbars and selects mirrored chrome. pt_text_direction
 *    (auto/ltr/rtl) selects each paragraph's base level. They are independent,
 *    so an LTR code editor can live inside an RTL shell.
 *  - Both zero values inherit: an unset widget reports
 *    pt_ui_direction_inherit / pt_text_direction_auto. The resolution chain is
 *    widget -> nearest explicit ancestor -> window -> app -> LTR.
 *  - auto runs UBA P2/P3 per paragraph, so Hebrew, Arabic and Latin can share
 *    one label or text area. A neutral-only paragraph falls back to the
 *    resolved UI direction.
 *  - Entries and text areas edit visually: Left/Right move to the visually
 *    adjacent cluster boundary, Home/End go to the visual line ends, Shift
 *    keeps the logical anchor, and Backspace/Delete stay logical. The caret
 *    rectangle sent to the IME is the visual rectangle.
 *  - Table and tree columns keep logical order; padding and margin stay
 *    physical. This example mirrors rows and text only.
 *
 * Under PT_HEADLESS the example toggles the direction twice, prints the getter
 * values and exits through pt_app_run_for, so it is a smoke test as well as a
 * visual demo.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char mixed_label[] =
    "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "          /* Shalom */
    "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "  /* Marhaba */
    "Hello, "                                     /* Latin */
    "123";                                        /* weak/neutral */

static const char rtl_entry_text[] =
    "\xD7\xA2\xD7\x91\xD7\x95\xD7\x93\xD7\x94 "  /* Avoda */
    "work@example.com";

static const char rtl_area_text[] =
    "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D "
    "- Hello world\n"                             /* Hebrew - Latin */
    "Hello world - \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C"
    "\xD7\x9D\n"                                  /* Latin - Hebrew */
    "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "
    "\xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85 42"; /*
                                                                     Arabic 42 */

typedef struct rtl_state {
    pt_app *app;
    pt_window *window;
    pt_widget *title;
    pt_widget *row;
    pt_widget *entry;
    pt_widget *area;
    pt_widget *nested;
    pt_widget *status;
    pt_ui_direction direction;
} rtl_state;

static void report(rtl_state *state, const char *message) {
    (void)state;
    printf("rtl_gallery: %s\n", message);
    fflush(stdout);
}

static void set_direction(rtl_state *state, pt_ui_direction direction) {
    state->direction = direction;
    /* The app value is the last fallback; the window repeats it so the
     * example also exercises the per-window override and its getter. */
    pt_app_set_ui_direction(state->app, direction);
    pt_window_set_ui_direction(state->window, direction);
    pt_widget_set_ui_direction(state->row, direction);
    pt_widget_set_ui_direction(state->entry, direction);
    pt_widget_set_ui_direction(state->area, direction);
    pt_widget_set_text_direction(state->title, direction == pt_ui_direction_rtl
                                               ? pt_text_direction_rtl
                                               : pt_text_direction_ltr);
    /* The nested editor is an explicit LTR island and never follows the shell. */
    pt_widget_set_ui_direction(state->nested, pt_ui_direction_ltr);
    pt_widget_set_text_direction(state->nested, pt_text_direction_ltr);

    const char *label = direction == pt_ui_direction_rtl ? "RTL shell" : "LTR shell";
    pt_widget_set_text(state->status, label);
    char line[160];
    snprintf(line, sizeof(line),
             "%s: app=%d window=%d row=%d entry=%d title_text=%d nested_ui=%d",
             label, (int)pt_app_get_ui_direction(state->app),
             (int)pt_window_get_ui_direction(state->window),
             (int)pt_widget_get_ui_direction(state->row),
             (int)pt_widget_get_ui_direction(state->entry),
             (int)pt_widget_get_text_direction(state->title),
             (int)pt_widget_get_ui_direction(state->nested));
    report(state, line);
}

static void on_ltr(pt_widget *button, void *data) {
    (void)button;
    set_direction((rtl_state *)data, pt_ui_direction_ltr);
}

static void on_rtl(pt_widget *button, void *data) {
    (void)button;
    set_direction((rtl_state *)data, pt_ui_direction_rtl);
}

static void on_quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.rtl_gallery");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt RTL Gallery", 820, 660);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    rtl_state state = {
        .app = app,
        .window = window,
        .direction = pt_ui_direction_rtl,
    };

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "BIDI  ·  UBA  ·  VISUAL CARET  ·  MIRROR");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    state.title = pt_label_create(root, mixed_label);
    pt_widget_set_font_size(state.title, 30.0f);
    pt_widget_set_font_weight(state.title, pt_font_weight_bold);
    pt_widget_set_text_direction(state.title, pt_text_direction_auto);

    pt_widget *intro = pt_label_create(
        root, "One label, three scripts and one text area show the UBA base per paragraph. "
              "The row below mirrors with RTL UI direction; the nested card keeps LTR layout. "
              "Entries and text areas move the caret visually by default.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(card, 12.0f);

    /* A row that mirrors: first child on the right, start/end alignment and
     * the two buttons swap sides when the UI direction changes. */
    state.row = pt_row_create(card);
    pt_widget_set_gap(state.row, 12.0f);
    pt_widget_set_align_self(state.row, pt_align_stretch);
    pt_widget_set_ui_direction(state.row, pt_ui_direction_rtl);

    pt_widget *badge = pt_label_create(state.row, "RTL row");
    pt_widget_set_font_size(badge, 13.0f);
    pt_widget_set_foreground(badge, pt_rgb(0x9ECE6A));
    pt_widget_set_align_self(badge, pt_align_center);

    pt_widget *row_text = pt_label_create(state.row, mixed_label);
    pt_widget_set_font_size(row_text, 15.0f);
    pt_widget_set_label_wrap(row_text, true);
    pt_widget_set_grow(row_text, 1.0f);

    pt_widget *ltr_button = pt_button_create(state.row, "Force LTR");
    pt_button_set_variant(ltr_button, pt_button_ghost);
    pt_button_set_on_click(ltr_button, on_ltr, &state);

    pt_widget *rtl_button = pt_button_create(state.row, "Force RTL");
    pt_button_set_variant(rtl_button, pt_button_primary);
    pt_button_set_on_click(rtl_button, on_rtl, &state);

    pt_widget *field_label = pt_label_create(card, "RTL entry (right-anchored at scroll 0)");
    pt_widget_set_font_size(field_label, 12.0f);
    pt_widget_set_foreground(field_label, pt_rgb(0x9AA7B8));

    state.entry = pt_entry_create(card, "Type here");
    pt_widget_set_text(state.entry, rtl_entry_text);
    pt_widget_set_size(state.entry, 420.0f, 36.0f);
    pt_widget_set_ui_direction(state.entry, pt_ui_direction_rtl);
    pt_widget_set_text_direction(state.entry, pt_text_direction_auto);
    pt_widget_set_align_self(state.entry, pt_align_start);

    pt_widget *area_label = pt_label_create(card, "RTL text area: one logical selection, per-line base");
    pt_widget_set_font_size(area_label, 12.0f);
    pt_widget_set_foreground(area_label, pt_rgb(0x9AA7B8));

    state.area = pt_text_area_create(card, "Type a note");
    pt_widget_set_text(state.area, rtl_area_text);
    pt_widget_set_size(state.area, 560.0f, 150.0f);
    pt_widget_set_ui_direction(state.area, pt_ui_direction_rtl);
    pt_widget_set_text_direction(state.area, pt_text_direction_auto);

    /* The nested LTR island: its own layout and text base, untouched by the
     * shell direction. This is the reason text and UI direction are separate. */
    state.nested = pt_card_create(root);
    pt_widget_set_padding(state.nested, 14.0f, 14.0f, 14.0f, 14.0f);
    pt_widget_set_gap(state.nested, 8.0f);
    pt_widget_set_ui_direction(state.nested, pt_ui_direction_ltr);

    pt_widget *nested_label = pt_label_create(
        state.nested, "Nested LTR island: left-to-right layout inside the RTL shell.");
    pt_widget_set_font_size(nested_label, 13.0f);
    pt_widget_set_foreground(nested_label, pt_rgb(0xBB9AF7));
    pt_widget_set_label_wrap(nested_label, true);

    pt_widget *nested_entry = pt_entry_create(state.nested, "code = = 1;");
    pt_widget_set_text(nested_entry, "int main(void) { return 0; }");
    pt_widget_set_text_direction(nested_entry, pt_text_direction_ltr);
    pt_widget_set_size(nested_entry, 360.0f, 34.0f);
    pt_widget_set_align_self(nested_entry, pt_align_start);

    state.status = pt_label_create(root, "");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);

    pt_widget *footer = pt_label_create(
        root, "Left/Right move to the visually adjacent cluster edge, Home/End go to the visual "
              "line ends, Shift keeps a logical anchor, and Backspace/Delete stay logical. "
              "The table and tree keep logical column order; padding stays physical.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    set_direction(&state, pt_ui_direction_rtl);

    pt_window_show(window);
    pt_widget_focus(state.entry);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        set_direction(&state, pt_ui_direction_ltr);
        set_direction(&state, pt_ui_direction_rtl);
        printf("rtl_gallery: headless text=%zu entry_ui=%d area_ui=%d nested_ui=%d "
               "title_text=%d\n",
               strlen(pt_widget_get_text(state.area)),
               (int)pt_widget_get_ui_direction(state.area),
               (int)pt_widget_get_ui_direction(state.entry),
               (int)pt_widget_get_ui_direction(state.nested),
               (int)pt_widget_get_text_direction(state.title));
        fflush(stdout);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}