/*
 * Pt pickers: a searchable select, a date picker, a number picker and a colour
 * picker in one window, with a change log.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/picker.h):
 *
 *  - Pickers are editor fields with a popup surface. The committed value is
 *    what the on_change callback reports; programmatic setters are silent.
 *  - The entry keeps focus while the popup is open, so typing keeps filtering
 *    or editing. Escape closes and restores the committed text; Tab closes the
 *    popup and continues in the focus order.
 *  - Select accessors return the committed item index (pt_list_index_none for
 *    none), the live filter query and the matching-row window.
 *  - Date, number and colour values are plain structs/doubles; the number
 *    picker has no popup, the date picker renders six fixed weeks and the
 *    colour palette is bounded by PT_COLOR_MAX_PRESETS.
 */

#include <pt/pt.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct pickers_state {
    pt_app *app;
    pt_window *window;
    pt_widget *select;
    pt_widget *date;
    pt_widget *number;
    pt_widget *color;
    pt_widget *status;
    unsigned changes;
} pickers_state;

static void log_line(pickers_state *state, const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ++state->changes;
    char text[320];
    snprintf(text, sizeof(text), "#%u · %s", state->changes, line);
    pt_widget_set_text(state->status, text);
    printf("pickers: %s\n", text);
    fflush(stdout);
}

static void on_select(pt_widget *select, uint32_t index, void *user_data) {
    pickers_state *state = user_data;
    if (index == pt_list_index_none) {
        log_line(state, "select cleared");
        return;
    }
    const char *text = pt_select_get_item_text(select, index);
    log_line(state, "select committed item %u \"%s\"", index, text ? text : "");
}

static void on_date(pt_widget *picker, pt_date date, void *user_data) {
    (void)picker;
    pickers_state *state = user_data;
    if (date.year == 0) {
        log_line(state, "date cleared");
        return;
    }
    log_line(state, "date committed %04d-%02u-%02u", (int)date.year, (unsigned)date.month,
             (unsigned)date.day);
}

static void on_number(pt_widget *picker, double value, void *user_data) {
    (void)picker;
    pickers_state *state = user_data;
    log_line(state, "number committed %.3f", value);
}

static void on_color(pt_widget *picker, pt_color color, void *user_data) {
    pickers_state *state = user_data;
    log_line(state, "colour committed %s (0x%08X)", pt_color_picker_get_hex(picker),
             (unsigned)color);
}

static void on_open_select(pt_widget *button, void *user_data) {
    (void)button;
    pickers_state *state = user_data;
    pt_select_open(state->select);
    log_line(state, "select popup open=%s query=\"%s\" matches=%u",
             pt_select_is_open(state->select) ? "yes" : "no",
             pt_select_get_query(state->select),
             pt_select_get_match_count(state->select));
}

static void on_open_date(pt_widget *button, void *user_data) {
    (void)button;
    pickers_state *state = user_data;
    pt_date_picker_open(state->date);
    log_line(state, "calendar open=%s text=\"%s\"",
             pt_date_picker_is_open(state->date) ? "yes" : "no",
             pt_date_picker_get_text(state->date));
}

static void on_open_color(pt_widget *button, void *user_data) {
    (void)button;
    pickers_state *state = user_data;
    pt_color_picker_open(state->color);
    log_line(state, "palette open=%s presets=%u",
             pt_color_picker_is_open(state->color) ? "yes" : "no",
             pt_color_picker_get_preset_count(state->color));
}

static void on_clear_values(pt_widget *button, void *user_data) {
    (void)button;
    pickers_state *state = user_data;
    pt_select_clear_selection(state->select);
    pt_date_picker_clear(state->date);
    pt_number_picker_clear(state->number);
    pt_color_picker_set_color(state->color, pt_rgb(0x000000));
    log_line(state, "values cleared programmatically (silent)");
}

static void on_close_all(pt_widget *button, void *user_data) {
    (void)button;
    pickers_state *state = user_data;
    pt_select_close(state->select);
    pt_date_picker_close(state->date);
    pt_color_picker_close(state->color);
    log_line(state, "popups closed; focus returns to the entry");
    pt_widget_focus(state->select);
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

/* One labelled field block: the control is created with the returned column as
 * its parent, then field_note() appends the hint below it. */
static pt_widget *field_block(pt_widget *parent, const char *label) {
    pt_widget *column = pt_column_create(parent);
    pt_widget_set_gap(column, 4.0f);
    pt_widget *caption = pt_label_create(column, label);
    pt_widget_set_font_size(caption, 12.0f);
    pt_widget_set_font_weight(caption, pt_font_weight_medium);
    pt_widget_set_foreground(caption, pt_rgb(0xAAB6C6));
    return column;
}

static void field_note(pt_widget *column, const char *hint) {
    pt_widget *note = pt_label_create(column, hint);
    pt_widget_set_font_size(note, 11.0f);
    pt_widget_set_foreground(note, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(note, true);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.pickers");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x4C8DFF));

    pt_window *window = pt_window_create(app, "Pt Pickers", 760, 760);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pickers_state state = {.app = app, .window = window};
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "PICKERS  ·  1.2");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x4C8DFF));

    pt_widget *heading = pt_label_create(root, "Four fields, one focus chain");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "Every control is reachable with Tab. F4 or Alt+Down opens a popup; Down/Up move "
              "the highlight, Enter commits, Escape restores the committed value. Typing in the "
              "select filters, typing in the date/number/colour fields edits the text.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_grow(card, 1.0f);
    pt_widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(card, 14.0f);

    pt_widget *select_block = field_block(card, "Searchable select");
    state.select = pt_select_create(select_block);
    pt_widget_set_grow(state.select, 1.0f);
    pt_select_set_placeholder(state.select, "Type to search a city");
    pt_widget_set_min_width(state.select, 260.0f);
    pt_widget_set_a11y_name(state.select, "City");
    pt_select_set_max_popup_rows(state.select, 6);
    static const char *const cities[] = {
        "Amsterdam", "Barcelona", "Berlin",      "Bordeaux", "Bristol",  "Brussels",
        "Copenhagen", "Dublin",   "Edinburgh",   "Lisbon",   "London",   "Madrid",
        "Milan",      "Munich",   "Oslo",        "Paris",    "Porto",    "Prague",
        "Rome",       "Stockholm", "Vienna",     "Warsaw",   "Zurich",
    };
    for (size_t index = 0; index < sizeof(cities) / sizeof(cities[0]); ++index) {
        pt_select_add_item(state.select, cities[index]);
    }
    pt_select_set_on_change(state.select, on_select, &state);
    field_note(select_block, "Substring filter is ASCII-case-insensitive. "
                             "Delete with an empty query clears the value.");

    pt_widget *date_block = field_block(card, "Date picker");
    state.date = pt_date_picker_create(date_block);
    pt_widget_set_grow(state.date, 1.0f);
    pt_date_picker_set_placeholder(state.date, "Pick a day");
    pt_date_picker_set_format(state.date, pt_date_format_dmy);
    pt_date_picker_set_date(state.date, pt_date_today());
    pt_widget_set_a11y_name(state.date, "Start date");
    pt_date_picker_set_on_change(state.date, on_date, &state);
    field_note(date_block, "Six fixed calendar weeks. Left/Right move a day, Up/Down a "
                           "week, Page Up/Down a month, T selects today.");

    pt_widget *number_block = field_block(card, "Number picker");
    state.number = pt_number_picker_create(number_block);
    pt_widget_set_grow(state.number, 1.0f);
    pt_number_picker_set_placeholder(state.number, "Minutes");
    pt_number_picker_set_range(state.number, 0.0, 240.0);
    pt_number_picker_set_step(state.number, 5.0);
    pt_number_picker_set_value(state.number, 45.0);
    pt_widget_set_a11y_name(state.number, "Duration in minutes");
    pt_number_picker_set_on_change(state.number, on_number, &state);
    field_note(number_block, "No popup. Up/Down step by 5, Page Up/Down by 50, Home/End "
                             "jump to the range edges; typed text clamps.");

    pt_widget *color_block = field_block(card, "Colour picker");
    state.color = pt_color_picker_create(color_block);
    pt_widget_set_grow(state.color, 1.0f);
    pt_color_picker_set_color(state.color, pt_rgb(0x4C8DFF));
    pt_color_picker_add_preset(state.color, pt_rgb(0x9ECE6A));
    pt_color_picker_add_preset(state.color, pt_rgb(0xF7768E));
    pt_widget_set_a11y_name(state.color, "Accent colour");
    pt_color_picker_set_on_change(state.color, on_color, &state);
    field_note(color_block, "Accepts #RRGGBB and #RRGGBBAA. Tab moves through the "
                            "palette; Enter commits the highlighted preset.");

    pt_widget *tools = pt_row_create(card);
    pt_widget_set_gap(tools, 8.0f);
    pt_widget_set_align_self(tools, pt_align_center);
    pt_widget *open_select = pt_button_create(tools, "Open select");
    pt_button_set_variant(open_select, pt_button_secondary);
    pt_button_set_on_click(open_select, on_open_select, &state);
    pt_widget *open_date = pt_button_create(tools, "Open calendar");
    pt_button_set_variant(open_date, pt_button_secondary);
    pt_button_set_on_click(open_date, on_open_date, &state);
    pt_widget *open_color = pt_button_create(tools, "Open palette");
    pt_button_set_variant(open_color, pt_button_secondary);
    pt_button_set_on_click(open_color, on_open_color, &state);
    pt_widget *close_all = pt_button_create(tools, "Close popups");
    pt_button_set_variant(close_all, pt_button_ghost);
    pt_button_set_on_click(close_all, on_close_all, &state);
    pt_widget *clear = pt_button_create(tools, "Clear values");
    pt_button_set_variant(clear, pt_button_ghost);
    pt_button_set_on_click(clear, on_clear_values, &state);

    state.status = pt_label_create(root, "Ready · commit a value to see the change log");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);
    pt_widget_set_a11y_name(state.status, "Change log");

    pt_widget *footer = pt_label_create(
        root, "Callbacks run after the control state has updated. Programmatic setters are "
              "silent, so a callback may destroy its picker; this example only reads it.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(state.select);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        printf("pickers: select items=%u max_rows=%u placeholder=\"%s\"\n",
               pt_select_get_item_count(state.select),
               pt_select_get_max_popup_rows(state.select),
               pt_select_get_placeholder(state.select));
        pt_select_set_query(state.select, "ber");
        printf("pickers: query \"ber\" matches=%u\n", pt_select_get_match_count(state.select));
        pt_select_clear_selection(state.select);
        pt_select_set_query(state.select, "");
        pt_select_open(state.select);
        on_open_select(NULL, &state);
        pt_select_set_selected(state.select, 2);
        on_select(state.select, pt_select_get_selected(state.select), &state);
        pt_select_close(state.select);

        pt_date date = pt_date_picker_get_date(state.date);
        printf("pickers: date %04d-%02u-%02u valid=%d format=%d\n", (int)date.year,
               (unsigned)date.month, (unsigned)date.day, pt_date_is_valid(date) ? 1 : 0,
               (int)pt_date_picker_get_format(state.date));
        on_date(state.date, date, &state);
        pt_date_picker_set_date(state.date, (pt_date){.year = 2027, .month = 1, .day = 15});
        on_date(state.date, pt_date_picker_get_date(state.date), &state);

        pt_number_picker_step_by(state.number, 2.0);
        printf("pickers: number %.1f has_value=%d text=\"%s\"\n",
               pt_number_picker_get_value(state.number),
               pt_number_picker_has_value(state.number) ? 1 : 0,
               pt_number_picker_get_text(state.number));
        on_number(state.number, pt_number_picker_get_value(state.number), &state);

        pt_color_picker_set_hex(state.color, "#ff8800");
        printf("pickers: colour hex=%s presets=%u\n", pt_color_picker_get_hex(state.color),
               pt_color_picker_get_preset_count(state.color));
        on_color(state.color, pt_color_picker_get_color(state.color), &state);
        on_open_color(NULL, &state);
        on_close_all(NULL, &state);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}
