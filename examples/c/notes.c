/*
 * Pt Notes: a virtualized list driving a multi-line editor.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NOTE_COUNT 128u
#define TOPIC_COUNT 8u

static const char *const topics[TOPIC_COUNT] = {
    "Design tokens",
    "Responsive layout",
    "Motion and easing",
    "Virtualized lists",
    "Multiline editing",
    "Accessibility",
    "Wayland overlays",
    "Performance notes",
};

static const char *const topic_bodies[TOPIC_COUNT] = {
    "Theme tokens keep colour, spacing and typography consistent across "
    "controls. Override them per app, then let every widget follow.",
    "Wrap lays cards into rows that fit the width; grid distributes children "
    "across equal columns. Both reflow when the window is resized.",
    "Public easing curves and caller-owned transitions animate custom widgets. "
    "Reduced motion snaps them to their target instead.",
    "Rows are created, bound and recycled on demand. Scrolling 128 notes costs "
    "only the widgets that are visible plus one row of overscan.",
    "The text area shares the entry editing model: UTF-8 offsets, caret-goal "
    "navigation, undo and IME preedit, with wrapping and grow-to-content.",
    "Roles, names, states and change events feed the AT-SPI bridge so assistive "
    "technology can read and operate the interface.",
    "Menus and tooltips use native xdg_popup surfaces. Dialogs and toasts stay "
    "inside the window with focus traps and stacking.",
    "Damage tracking repaints only what changed. Text shaping is cached, so "
    "scrolling stays smooth on high-DPI outputs.",
};

typedef struct notes_state {
    pt_app *app;
    pt_widget *list;
    pt_widget *editor;
    pt_widget *title;
    pt_widget *status;
    pt_widget *words;
    pt_widget *characters;
    pt_widget *lines;
    uint32_t current;
} notes_state;

static void note_title(uint32_t index, char *out, size_t size) {
    snprintf(out, size, "%s · %02u", topics[index % TOPIC_COUNT], index + 1u);
}

static void note_body(uint32_t index, char *out, size_t size) {
    snprintf(out, size,
             "%s\n\n%s\n\nRow %u of %u is materialised only while it is "
             "visible. The list keeps a small pool of row widgets and rebinds "
             "them as you scroll.",
             topics[index % TOPIC_COUNT], topic_bodies[index % TOPIC_COUNT],
             index + 1u, NOTE_COUNT);
}

static void update_stats(notes_state *state) {
    const char *text = pt_widget_get_text(state->editor);
    if (!text) {
        text = "";
    }
    size_t characters = strlen(text);
    uint32_t words = 0;
    uint32_t lines = 1;
    bool in_word = false;
    for (size_t i = 0; i < characters; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') {
            ++lines;
        }
        if (isspace(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++words;
        }
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", words);
    pt_widget_set_text(state->words, buffer);
    snprintf(buffer, sizeof(buffer), "%zu", characters);
    pt_widget_set_text(state->characters, buffer);
    snprintf(buffer, sizeof(buffer), "%u", lines);
    pt_widget_set_text(state->lines, buffer);
}

static void select_note(notes_state *state, uint32_t index) {
    if (index >= NOTE_COUNT) {
        return;
    }
    state->current = index;
    char title[96];
    char body[512];
    note_title(index, title, sizeof(title));
    note_body(index, body, sizeof(body));
    pt_widget_set_text(state->title, title);
    pt_widget_set_text(state->editor, body);
    pt_widget_set_text(state->status, "Saved locally · edits are in memory");
    update_stats(state);
}

static void on_bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    (void)list;
    (void)data;
    char title[96];
    if (index >= NOTE_COUNT) {
        pt_widget_set_text(row, "");
        return;
    }
    note_title(index, title, sizeof(title));
    pt_widget_set_text(row, title);
}

static void on_select(pt_widget *list, uint32_t index, void *data) {
    (void)list;
    select_note(data, index);
}

static void on_activate(pt_widget *list, uint32_t index, void *data) {
    (void)list;
    notes_state *state = data;
    char message[128];
    snprintf(message, sizeof(message), "Opened note %u", index + 1u);
    pt_widget_set_text(state->status, message);
}

static void on_edit(pt_widget *editor, const char *text, void *data) {
    (void)editor;
    (void)text;
    notes_state *state = data;
    pt_widget_set_text(state->status, "Edited · not yet synced");
    update_stats(state);
}

static void on_reduced_motion(pt_widget *toggle, float value, void *data) {
    (void)toggle;
    notes_state *state = data;
    pt_app_set_reduced_motion(state->app, value > 0.5f);
    pt_widget_set_text(state->status,
                       value > 0.5f ? "Reduced motion enabled"
                                    : "Reduced motion disabled");
}

static pt_widget *chip(pt_app *app, pt_widget *parent, const char *text) {
    pt_widget *label = pt_label_create(parent, text);
    pt_widget_set_padding(label, 4.0f, 10.0f, 4.0f, 10.0f);
    pt_widget_set_background(label,
                             pt_app_get_theme_color(app, pt_theme_surface_sunken));
    pt_widget_set_radius(label, 999.0f);
    pt_widget_set_font_size(label, 12.0f);
    pt_widget_set_align_self(label, pt_align_start);
    return label;
}

static pt_widget *stat(pt_app *app, pt_widget *grid, const char *caption,
                       pt_widget **value_out) {
    pt_widget *card = pt_card_create(grid);
    pt_widget_set_padding(card, 10.0f, 12.0f, 10.0f, 12.0f);
    pt_widget_set_gap(card, 2.0f);
    pt_widget *number = pt_label_create(card, "0");
    pt_widget_set_font_size(number, 20.0f);
    pt_widget_set_font_weight(number, pt_font_weight_semibold);
    pt_widget *label = pt_label_create(card, caption);
    pt_widget_set_font_size(label, 12.0f);
    pt_widget_set_foreground(label, pt_app_get_theme_color(app, pt_theme_text_muted));
    *value_out = number;
    return card;
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.notes");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt Notes", 960, 660);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }
    pt_window_set_min_size(window, 780, 520);

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 22.0f, 22.0f, 22.0f, 22.0f);
    pt_widget_set_gap(root, 14.0f);

    notes_state state = {.app = app, .current = 0};

    pt_widget *header = pt_row_create(root);
    pt_widget_set_gap(header, 14.0f);
    pt_widget *headings = pt_column_create(header);
    pt_widget_set_grow(headings, 1.0f);
    pt_widget_set_gap(headings, 2.0f);
    pt_widget *heading = pt_label_create(headings, "Notes");
    pt_widget_set_font_size(heading, 26.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);
    pt_widget *subtitle = pt_label_create(
        headings, "A virtualized list, a multiline editor and responsive chips.");
    pt_widget_set_font_size(subtitle, 13.0f);
    pt_widget_set_foreground(subtitle, pt_app_get_theme_color(app, pt_theme_text_muted));

    pt_widget *motion_toggle = pt_toggle_create(header);
    pt_widget_set_align_self(motion_toggle, pt_align_center);
    pt_toggle_set_checked(motion_toggle, pt_app_get_reduced_motion(app));
    pt_toggle_set_on_change(motion_toggle, on_reduced_motion, &state);
    pt_widget *motion_label = pt_label_create(header, "Reduce motion");
    pt_widget_set_align_self(motion_label, pt_align_center);
    pt_widget *new_note = pt_button_create(header, "New note");
    pt_button_set_variant(new_note, pt_button_primary);
    pt_widget_set_align_self(new_note, pt_align_center);

    pt_widget *main_row = pt_row_create(root);
    pt_widget_set_grow(main_row, 1.0f);
    pt_widget_set_gap(main_row, 14.0f);

    pt_widget *sidebar = pt_card_create(main_row);
    pt_widget_set_width(sidebar, 300.0f);
    pt_widget_set_padding(sidebar, 14.0f, 10.0f, 14.0f, 10.0f);
    pt_widget_set_gap(sidebar, 10.0f);
    pt_widget *sidebar_title = pt_label_create(sidebar, "All notes");
    pt_widget_set_font_size(sidebar_title, 16.0f);
    pt_widget_set_font_weight(sidebar_title, pt_font_weight_semibold);
    state.list = pt_list_view_create(sidebar);
    pt_widget_set_grow(state.list, 1.0f);
    pt_list_view_set_row_height(state.list, 46.0f);
    pt_list_view_set_row_count(state.list, NOTE_COUNT);
    pt_list_view_set_on_bind_row(state.list, on_bind_row, &state);
    pt_list_view_set_on_select(state.list, on_select, &state);
    pt_list_view_set_on_activate(state.list, on_activate, &state);
    pt_widget_set_a11y_name(state.list, "Notes");

    pt_widget *workspace = pt_card_create(main_row);
    pt_widget_set_grow(workspace, 1.0f);
    pt_widget_set_padding(workspace, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(workspace, 12.0f);

    pt_widget *workspace_head = pt_row_create(workspace);
    pt_widget_set_gap(workspace_head, 10.0f);
    state.title = pt_label_create(workspace_head, "Design tokens · 01");
    pt_widget_set_font_size(state.title, 18.0f);
    pt_widget_set_font_weight(state.title, pt_font_weight_semibold);
    pt_widget_set_grow(state.title, 1.0f);
    state.status = pt_label_create(workspace_head, "Saved locally");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_align_self(state.status, pt_align_center);
    pt_widget_set_foreground(state.status,
                             pt_app_get_theme_color(app, pt_theme_text_muted));

    state.editor = pt_text_area_create(workspace, "Select a note to start writing…");
    pt_widget_set_grow(state.editor, 1.0f);
    pt_text_area_set_on_change(state.editor, on_edit, &state);
    pt_widget_set_a11y_name(state.editor, "Note body");
    pt_widget_set_a11y_description(state.editor, "Edits the selected note");

    pt_widget *tags = pt_wrap_create(workspace);
    pt_wrap_set_gap(tags, 6.0f, 6.0f);
    static const char *const tag_names[] = {"design", "layout", "motion",
                                            "lists", "text", "a11y"};
    for (size_t i = 0; i < sizeof(tag_names) / sizeof(tag_names[0]); ++i) {
        chip(app, tags, tag_names[i]);
    }

    pt_widget *stats = pt_grid_create(workspace, 3);
    pt_grid_set_gap(stats, 8.0f, 8.0f);
    stat(app, stats, "words", &state.words);
    stat(app, stats, "characters", &state.characters);
    stat(app, stats, "lines", &state.lines);

    select_note(&state, 0);
    pt_list_view_set_selected(state.list, 0);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
