/*
 * Pt primary selection example: drag-select text with the raw pointer callback,
 * publish it to the middle-click selection from the button-release callback,
 * and paste it back into a second pane.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Serial contract (docs/standards.md section 9): a raw pointer callback runs
 * while the compositor's serial for that event is still current. Pt's primary
 * selection uses the seat serial of the event being dispatched, so a publish
 * made from the button-release callback belongs to the press/release that
 * produced the selection. Publishing later, from an unrelated timer or button,
 * would reuse whatever serial happened to be current instead.
 *
 * The panes are custom-painted so the example also keeps the paint contract:
 * the callback is a pure function of the selection state, every clip push has a
 * matching pop, and input handlers mutate state then call
 * pt_widget_request_redraw().
 */

#include <pt/pt.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANE_PAD 16.0f
#define PANE_HEADER 44.0f
#define PANE_TOP_GAP 8.0f
#define PANE_FONT 13.0f
#define PANE_LINE 20.0f
#define PANE_GUTTER 30.0f
#define FONT_FAMILY "monospace"
#define TARGET_LINE_COUNT 9
#define TARGET_LINE_MAX 160

/* Linux input-event button codes. The raw pointer callback reports these. */
#define EXAMPLE_BTN_LEFT 0x110u
#define EXAMPLE_BTN_MIDDLE 0x112u

#define SOURCE_LINE_COUNT 8

typedef struct text_pos {
    int line;
    int column;
} text_pos;

typedef struct font_grid {
    float ascent;
    float line_height;
    float cell_width;
} font_grid;

typedef struct primary_state {
    pt_window *window;
    pt_widget *source;
    pt_widget *target;
    pt_widget *status;
    text_pos anchor;
    text_pos head;
    bool dragging;
    bool has_selection;
    char target_lines[TARGET_LINE_COUNT][TARGET_LINE_MAX];
    int target_count;
    int highlight;
    font_grid grid;
    bool grid_valid;
    unsigned publishes;
    unsigned pastes;
} primary_state;

static const char *const source_lines[SOURCE_LINE_COUNT] = {
    "Primary selection is the middle-click clipboard.",
    "Drag across these lines to select a range.",
    "On button release the range is published",
    "from inside the same pointer callback.",
    "The compositor serial therefore belongs",
    "to the release that produced the selection.",
    "",
    "Middle-click the paste pane to read it back.",
};

static bool point_inside(const pt_widget *widget, float x, float y) {
    float bx = 0.0f;
    float by = 0.0f;
    float bw = 0.0f;
    float bh = 0.0f;
    pt_widget_get_bounds(widget, &bx, &by, &bw, &bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

static int position_compare(text_pos a, text_pos b) {
    if (a.line != b.line) {
        return a.line < b.line ? -1 : 1;
    }
    if (a.column != b.column) {
        return a.column < b.column ? -1 : 1;
    }
    return 0;
}

static bool positions_equal(text_pos a, text_pos b) {
    return a.line == b.line && a.column == b.column;
}

static int selection_line_count(const primary_state *state) {
    text_pos start = state->anchor;
    text_pos end = state->head;
    if (position_compare(start, end) > 0) {
        text_pos swap = start;
        start = end;
        end = swap;
    }
    return end.line - start.line + 1;
}

static int source_length(int line) {
    if (line < 0 || line >= SOURCE_LINE_COUNT) {
        return 0;
    }
    return (int)strlen(source_lines[line]);
}

/* Font metrics are only available from a paint context. The source paint
 * callback caches the monospace grid so pointer hit-testing can map window
 * coordinates back to a line and column. This cache is not part of paint
 * output: every tile still draws the same pixels. */
static font_grid grid_from_context(pt_paint_context *context) {
    font_grid grid = {.ascent = PANE_FONT, .line_height = PANE_LINE,
                      .cell_width = PANE_FONT * 0.62f};
    float ascent = 0.0f;
    float descent = 0.0f;
    float line_height = 0.0f;
    float cell_width = 0.0f;
    if (pt_paint_font_metrics(context, FONT_FAMILY, PANE_FONT, pt_font_weight_regular,
                              pt_font_slant_upright, &ascent, &descent, &line_height,
                              &cell_width)) {
        if (ascent > 0.0f) {
            grid.ascent = ascent;
        }
        if (line_height > 0.0f) {
            grid.line_height = fmaxf(line_height, PANE_FONT + 2.0f);
        }
        if (cell_width > 0.0f) {
            grid.cell_width = cell_width;
        }
    }
    return grid;
}

static text_pos position_at(primary_state *state, float window_x, float window_y) {
    font_grid grid = state->grid_valid
                         ? state->grid
                         : (font_grid){.ascent = PANE_FONT,
                                       .line_height = PANE_LINE,
                                       .cell_width = PANE_FONT * 0.62f};
    float bx = 0.0f;
    float by = 0.0f;
    float bw = 0.0f;
    float bh = 0.0f;
    pt_widget_get_bounds(state->source, &bx, &by, &bw, &bh);
    float top = by + PANE_HEADER + PANE_TOP_GAP;
    float left = bx + PANE_PAD + PANE_GUTTER;
    int line = (int)floorf((window_y - top) / grid.line_height);
    if (line < 0) {
        line = 0;
    }
    if (line >= SOURCE_LINE_COUNT) {
        line = SOURCE_LINE_COUNT - 1;
    }
    int length = source_length(line);
    int column = (int)lroundf((window_x - left) / grid.cell_width);
    if (column < 0) {
        column = 0;
    }
    if (column > length) {
        column = length;
    }
    return (text_pos){.line = line, .column = column};
}

/* Returns the highlighted byte range for one source line, if any. */
static bool selection_on_line(const primary_state *state, int line, int length, int *from_out,
                              int *to_out) {
    if (!state->has_selection) {
        return false;
    }
    text_pos start = state->anchor;
    text_pos end = state->head;
    if (position_compare(start, end) > 0) {
        text_pos swap = start;
        start = end;
        end = swap;
    }
    if (line < start.line || line > end.line) {
        return false;
    }
    int from = line == start.line ? start.column : 0;
    int to = line == end.line ? end.column : length;
    if (from < 0) {
        from = 0;
    }
    if (from > length) {
        from = length;
    }
    if (to < 0) {
        to = 0;
    }
    if (to > length) {
        to = length;
    }
    if (to < from) {
        int swap = from;
        from = to;
        to = swap;
    }
    if (to == from) {
        return false;
    }
    *from_out = from;
    *to_out = to;
    return true;
}

/* Builds a malloc'd UTF-8 copy of the selected rows. The caller frees it. */
static char *selection_text(const primary_state *state, size_t *length_out) {
    text_pos start = state->anchor;
    text_pos end = state->head;
    if (position_compare(start, end) > 0) {
        text_pos swap = start;
        start = end;
        end = swap;
    }
    size_t capacity = 1;
    for (int line = start.line; line <= end.line; ++line) {
        int length = source_length(line);
        int from = line == start.line ? start.column : 0;
        int to = line == end.line ? end.column : length;
        if (from < 0) {
            from = 0;
        }
        if (from > length) {
            from = length;
        }
        if (to < from) {
            to = from;
        }
        if (to > length) {
            to = length;
        }
        capacity += (size_t)(to - from) + 1u;
    }
    char *text = malloc(capacity);
    if (!text) {
        return NULL;
    }
    size_t used = 0;
    for (int line = start.line; line <= end.line; ++line) {
        int length = source_length(line);
        int from = line == start.line ? start.column : 0;
        int to = line == end.line ? end.column : length;
        if (from < 0) {
            from = 0;
        }
        if (from > length) {
            from = length;
        }
        if (to < from) {
            to = from;
        }
        if (to > length) {
            to = length;
        }
        if (line > start.line) {
            text[used++] = '\n';
        }
        memcpy(text + used, source_lines[line] + from, (size_t)(to - from));
        used += (size_t)(to - from);
    }
    text[used] = '\0';
    if (length_out) {
        *length_out = used;
    }
    return text;
}

static void set_status(primary_state *state, const char *text) {
    pt_widget_set_text(state->status, text);
}

static void append_target_line(primary_state *state, const char *text, size_t length) {
    if (length == 0) {
        return;
    }
    if (state->target_count == TARGET_LINE_COUNT) {
        for (int index = 1; index < TARGET_LINE_COUNT; ++index) {
            memmove(state->target_lines[index - 1], state->target_lines[index],
                    TARGET_LINE_MAX);
        }
        --state->target_count;
    }
    size_t take = length < 120u ? length : 120u;
    snprintf(state->target_lines[state->target_count], TARGET_LINE_MAX, "> %.*s",
             (int)take, text);
    ++state->target_count;
}

static void paste_primary(primary_state *state) {
    char *primary = pt_window_primary_get(state->window);
    if (!primary || !*primary) {
        pt_free(primary);
        set_status(state, "Primary is empty. Drag over the source pane first.");
        return;
    }
    size_t length = strlen(primary);
    unsigned lines = 0;
    const char *cursor = primary;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        size_t line_length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        if (line_length > 0) {
            append_target_line(state, cursor, line_length);
            ++lines;
        }
        if (!newline) {
            break;
        }
        cursor = newline + 1;
    }
    pt_free(primary);
    ++state->pastes;
    state->highlight = state->target_count - 1;
    char status[192];
    snprintf(status, sizeof(status),
             "Read %zu bytes from primary in this pointer event; appended %u line(s), paste #%u.",
             length, lines, state->pastes);
    set_status(state, status);
    pt_widget_request_redraw(state->target);
}

static bool on_pointer(pt_window *window, const pt_pointer_event *event, void *user_data) {
    primary_state *state = user_data;
    if (!event) {
        return false;
    }
    switch (event->phase) {
    case pt_pointer_phase_down:
        if (event->button == EXAMPLE_BTN_LEFT &&
            point_inside(state->source, event->x, event->y)) {
            state->anchor = position_at(state, event->x, event->y);
            state->head = state->anchor;
            state->dragging = true;
            state->has_selection = false;
            set_status(state, "Selecting; release inside the window to publish the range.");
            pt_widget_request_redraw(state->source);
            return true;
        }
        if (event->button == EXAMPLE_BTN_MIDDLE &&
            point_inside(state->target, event->x, event->y)) {
            paste_primary(state);
            return true;
        }
        break;
    case pt_pointer_phase_motion:
        if (state->dragging) {
            text_pos next = position_at(state, event->x, event->y);
            if (!positions_equal(next, state->head)) {
                state->head = next;
                state->has_selection = !positions_equal(state->anchor, state->head);
                pt_widget_request_redraw(state->source);
            }
        }
        break;
    case pt_pointer_phase_up:
        if (state->dragging && event->button == EXAMPLE_BTN_LEFT) {
            state->head = position_at(state, event->x, event->y);
            state->dragging = false;
            state->has_selection = !positions_equal(state->anchor, state->head);
            if (!state->has_selection) {
                set_status(state, "Click without drag selects nothing; primary is unchanged.");
                pt_widget_request_redraw(state->source);
                return true;
            }
            size_t length = 0;
            char *payload = selection_text(state, &length);
            if (!payload || length == 0) {
                free(payload);
                set_status(state, "Selection contains no text; primary is unchanged.");
                pt_widget_request_redraw(state->source);
                return true;
            }
            /* Publish here, not from a timer: the serial used by Wayland's
             * set_selection belongs to this button release. */
            bool published = pt_window_primary_set(state->window, payload);
            char *readback = published ? pt_window_primary_get(state->window) : NULL;
            size_t readback_length = readback ? strlen(readback) : 0;
            pt_free(readback);
            free(payload);
            if (published) {
                ++state->publishes;
                char status[192];
                snprintf(status, sizeof(status),
                         "Published %zu bytes from %d line(s); read back %zu bytes, publish #%u.",
                         length, selection_line_count(state), readback_length,
                         state->publishes);
                set_status(state, status);
            } else {
                set_status(state, "The primary selection backend rejected the set.");
            }
            pt_widget_request_redraw(state->source);
            return true;
        }
        break;
    case pt_pointer_phase_cancel:
        if (state->dragging) {
            state->dragging = false;
            set_status(state, "Selection cancelled; primary is unchanged.");
            pt_widget_request_redraw(state->source);
        }
        break;
    case pt_pointer_phase_enter:
    case pt_pointer_phase_leave:
    case pt_pointer_phase_axis:
        break;
    }
    (void)window;
    return false;
}

static void paint_source(pt_widget *widget, pt_paint_context *context, void *user_data) {
    (void)widget;
    primary_state *state = user_data;
    float width = pt_paint_width(context);
    float height = pt_paint_height(context);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    float clip_x = 0.0f;
    float clip_y = 0.0f;
    float clip_w = 0.0f;
    float clip_h = 0.0f;
    if (!pt_paint_clip_rect(context, &clip_x, &clip_y, &clip_w, &clip_h)) {
        return;
    }
    font_grid grid = grid_from_context(context);
    state->grid = grid;
    state->grid_valid = true;

    float top = PANE_HEADER + PANE_TOP_GAP;
    float text_x = PANE_PAD + PANE_GUTTER;
    float body_height = fmaxf(0.0f, height - top - PANE_PAD);

    pt_paint_draw_text(context, FONT_FAMILY, 12.0f, pt_font_weight_semibold,
                       pt_font_slant_upright, PANE_PAD, 27.0f, "PRIMARY SOURCE",
                       strlen("PRIMARY SOURCE"), pt_rgb(0x7AA2F7));
    const char *hint = "drag to select";
    float hint_width = pt_paint_measure_text(context, FONT_FAMILY, 11.0f,
                                             pt_font_weight_regular, pt_font_slant_upright,
                                             hint, strlen(hint));
    pt_paint_draw_text(context, FONT_FAMILY, 11.0f, pt_font_weight_regular,
                       pt_font_slant_upright, width - PANE_PAD - hint_width, 26.0f, hint,
                       strlen(hint), pt_rgb(0x6F7C8F));
    pt_paint_draw_line(context, PANE_PAD, PANE_HEADER - 1.0f, width - PANE_PAD,
                       PANE_HEADER - 1.0f, 1.0f, pt_rgb(0x263241));

    pt_paint_push_clip(context, PANE_PAD, top, width - 2.0f * PANE_PAD, body_height);
    for (int line = 0; line < SOURCE_LINE_COUNT; ++line) {
        float row_y = top + (float)line * grid.line_height;
        int length = source_length(line);
        int from = 0;
        int to = 0;
        if (selection_on_line(state, line, length, &from, &to)) {
            pt_paint_fill_rect(context, text_x + (float)from * grid.cell_width, row_y,
                               (float)(to - from) * grid.cell_width, grid.line_height,
                               pt_rgb(0x2B4A78));
        }
        char number[16];
        snprintf(number, sizeof(number), "%02u", (unsigned)(line + 1));
        pt_paint_draw_text(context, FONT_FAMILY, 11.0f, pt_font_weight_regular,
                           pt_font_slant_upright, PANE_PAD + 4.0f, row_y + grid.ascent,
                           number, strlen(number), pt_rgb(0x56657A));
        pt_paint_draw_text(context, FONT_FAMILY, PANE_FONT, pt_font_weight_regular,
                           pt_font_slant_upright, text_x, row_y + grid.ascent,
                           source_lines[line], strlen(source_lines[line]),
                           pt_rgb(0xC3D0E1));
    }
    pt_paint_draw_line(context, PANE_PAD + 24.0f, top + 4.0f, PANE_PAD + 24.0f,
                       top + body_height - 4.0f, 1.0f, pt_rgb(0x263241));
    pt_paint_pop_clip(context);
}

static void paint_target(pt_widget *widget, pt_paint_context *context, void *user_data) {
    (void)widget;
    primary_state *state = user_data;
    float width = pt_paint_width(context);
    float height = pt_paint_height(context);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    float clip_x = 0.0f;
    float clip_y = 0.0f;
    float clip_w = 0.0f;
    float clip_h = 0.0f;
    if (!pt_paint_clip_rect(context, &clip_x, &clip_y, &clip_w, &clip_h)) {
        return;
    }
    font_grid grid = grid_from_context(context);
    float top = PANE_HEADER + PANE_TOP_GAP;
    float body_height = fmaxf(0.0f, height - top - PANE_PAD);

    pt_paint_draw_text(context, FONT_FAMILY, 12.0f, pt_font_weight_semibold,
                       pt_font_slant_upright, PANE_PAD, 27.0f, "PASTE TARGET",
                       strlen("PASTE TARGET"), pt_rgb(0x9ECE6A));
    const char *hint = "middle-click to paste";
    float hint_width = pt_paint_measure_text(context, FONT_FAMILY, 11.0f,
                                             pt_font_weight_regular, pt_font_slant_upright,
                                             hint, strlen(hint));
    pt_paint_draw_text(context, FONT_FAMILY, 11.0f, pt_font_weight_regular,
                       pt_font_slant_upright, width - PANE_PAD - hint_width, 26.0f, hint,
                       strlen(hint), pt_rgb(0x6F7C8F));
    pt_paint_draw_line(context, PANE_PAD, PANE_HEADER - 1.0f, width - PANE_PAD,
                       PANE_HEADER - 1.0f, 1.0f, pt_rgb(0x263241));

    pt_paint_push_clip(context, PANE_PAD, top, width - 2.0f * PANE_PAD, body_height);
    if (state->target_count == 0) {
        const char *empty = "Middle-click here after selecting text.";
        pt_paint_draw_text(context, FONT_FAMILY, PANE_FONT, pt_font_weight_regular,
                           pt_font_slant_upright, PANE_PAD, top + grid.ascent, empty,
                           strlen(empty), pt_rgb(0x6F7C8F));
    } else {
        for (int line = 0; line < state->target_count; ++line) {
            float row_y = top + (float)line * grid.line_height;
            if (line == state->highlight) {
                pt_paint_fill_rect(context, PANE_PAD, row_y, width - 2.0f * PANE_PAD,
                                   grid.line_height, pt_rgb(0x1D3524));
            }
            pt_paint_draw_text(context, FONT_FAMILY, PANE_FONT, pt_font_weight_regular,
                               pt_font_slant_upright, PANE_PAD, row_y + grid.ascent,
                               state->target_lines[line], strlen(state->target_lines[line]),
                               line == state->highlight ? pt_rgb(0xD7F5DC)
                                                        : pt_rgb(0xC3D0E1));
        }
    }
    pt_paint_pop_clip(context);
}

static void on_read_primary(pt_widget *button, void *user_data) {
    (void)button;
    primary_state *state = user_data;
    char *text = pt_window_primary_get(state->window);
    if (!text || !*text) {
        pt_free(text);
        set_status(state, "Primary is empty.");
        return;
    }
    size_t length = strlen(text);
    char preview[56];
    size_t used = 0;
    for (size_t index = 0; text[index] && used + 1 < sizeof(preview) && index < 44; ++index) {
        preview[used++] = text[index] == '\n' ? ' ' : text[index];
    }
    preview[used] = '\0';
    char status[192];
    snprintf(status, sizeof(status), "Primary holds %zu bytes: \"%s%s\".", length, preview,
             length > 44 ? "…" : "");
    pt_free(text);
    set_status(state, status);
}

static void on_clear_target(pt_widget *button, void *user_data) {
    (void)button;
    primary_state *state = user_data;
    state->target_count = 0;
    state->highlight = -1;
    set_status(state, "Paste target cleared; primary is untouched.");
    pt_widget_request_redraw(state->target);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.primary_selection");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt Primary Selection", 980, 640);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    primary_state state = {.window = window, .highlight = -1};
    pt_window_set_pointer_callback(window, on_pointer, &state);

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 22.0f, 22.0f, 22.0f, 22.0f);
    pt_widget_set_gap(root, 12.0f);

    pt_widget *title = pt_label_create(root, "Primary selection");
    pt_widget_set_font_size(title, 26.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);
    pt_widget *subtitle = pt_label_create(
        root, "Drag over the left pane, release, then middle-click the right pane. The set "
              "happens inside the button-release callback, so the serial belongs to the event "
              "that produced the selection.");
    pt_widget_set_font_size(subtitle, 13.0f);
    pt_widget_set_foreground(subtitle, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(subtitle, true);

    pt_widget *panes = pt_row_create(root);
    pt_widget_set_gap(panes, 14.0f);
    pt_widget_set_grow(panes, 1.0f);

    state.source = pt_box_create(panes, pt_direction_column);
    pt_widget_set_grow(state.source, 1.0f);
    pt_widget_set_min_height(state.source, 300.0f);
    pt_widget_set_background(state.source, pt_rgb(0x0D131C));
    pt_widget_set_radius(state.source, 14.0f);
    pt_widget_set_border_width(state.source, 1.0f);
    pt_widget_set_border_color(state.source, pt_rgb(0x263241));
    pt_widget_set_paint_callback(state.source, paint_source, &state);
    pt_widget_set_a11y_role(state.source, pt_a11y_role_text);
    pt_widget_set_a11y_name(state.source, "Primary selection source");
    pt_widget_set_a11y_description(
        state.source,
        "Eight log lines. Drag across them to select and publish a primary selection.");

    state.target = pt_box_create(panes, pt_direction_column);
    pt_widget_set_grow(state.target, 1.0f);
    pt_widget_set_min_height(state.target, 300.0f);
    pt_widget_set_background(state.target, pt_rgb(0x0D131C));
    pt_widget_set_radius(state.target, 14.0f);
    pt_widget_set_border_width(state.target, 1.0f);
    pt_widget_set_border_color(state.target, pt_rgb(0x263241));
    pt_widget_set_paint_callback(state.target, paint_target, &state);
    pt_widget_set_a11y_role(state.target, pt_a11y_role_text_frame);
    pt_widget_set_a11y_name(state.target, "Primary paste target");
    pt_widget_set_a11y_description(
        state.target, "Middle-click to append the current primary selection.");

    pt_widget *actions = pt_row_create(root);
    pt_widget_set_gap(actions, 10.0f);
    pt_widget_set_align_self(actions, pt_align_center);
    pt_widget *read = pt_button_create(actions, "Read primary");
    pt_button_set_variant(read, pt_button_secondary);
    pt_button_set_on_click(read, on_read_primary, &state);
    pt_widget_set_tooltip(read, "Calls pt_window_primary_get() and frees the result");
    pt_widget *clear = pt_button_create(actions, "Clear paste target");
    pt_button_set_variant(clear, pt_button_ghost);
    pt_button_set_on_click(clear, on_clear_target, &state);

    state.status = pt_label_create(
        actions, "Drag across the source lines to make a selection.");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);
    pt_widget_set_grow(state.status, 1.0f);

    pt_widget *footer = pt_label_create(
        root, "pt_window_primary_set() copies the string; pt_window_primary_get() returns "
              "caller-owned UTF-8 that is released with pt_free(). Without "
              "wp_primary_selection_unstable_v1 (or under PT_HEADLESS) both fall back to the "
              "in-process buffer, so the example still runs.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
