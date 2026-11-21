/*
 * Pt custom paint dashboard: a terminal-like panel drawn entirely with the
 * pt_paint_* helpers from <pt/custom.h>.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (see docs/standards.md section 7):
 *
 *  - Paint is pure and idempotent. paint_dashboard() only reads dashboard_state
 *    and never appends lines, advances a counter or changes layout. The widget
 *    can be painted 0..N times per frame (once per damage tile) in any order;
 *    every call for the same state produces the same pixels.
 *  - Every pt_paint_push_clip() has exactly one pt_paint_pop_clip() on the same
 *    path. Nothing returns between a push and its pop.
 *  - pt_paint_clip_rect() lets a callback skip regions outside the current
 *    damage tile; clipping itself happens in the toolkit.
 *  - State changes happen in button, slider and timer callbacks. They request a
 *    repaint with pt_widget_request_redraw()/pt_widget_request_redraw_rect()
 *    instead of touching the tree from inside the paint callback.
 *  - pt_window_paint_pixels() snapshots the window offscreen, which is how the
 *    example checks idempotence without a compositor.
 */

#include <pt/pt.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_PAD 18.0f
#define PANEL_HEADER 48.0f
#define PANEL_FOOTER 34.0f
#define TERMINAL_ROWS 8
#define TERMINAL_FONT 13.0f
#define GAUGE_WIDTH 132.0f
#define FONT_FAMILY "monospace"

typedef struct dashboard_state {
    pt_window *window;
    pt_widget *panel;
    pt_widget *status;
    float load;
    unsigned frame;
    unsigned appended;
    bool caret_on;
    char lines[TERMINAL_ROWS][96];
} dashboard_state;

static const char *const initial_lines[TERMINAL_ROWS] = {
    "pt_paint_* draws into a borrowed paint context.",
    "Coordinates are widget-local logical pixels.",
    "Every push_clip has exactly one pop_clip.",
    "Paint is pure: state in, pixels out, no counters.",
    "One damage tile may be painted 0..N times.",
    "Caret and gauge state live outside the callback.",
    "Callbacks mutate state, then request_redraw().",
    "request_redraw_rect() damages only the gauge.",
};

static bool intersects(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                       float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void reset_lines(dashboard_state *state, unsigned frame) {
    state->frame = frame;
    for (int row = 0; row < TERMINAL_ROWS; ++row) {
        snprintf(state->lines[row], sizeof(state->lines[row]), "%s", initial_lines[row]);
    }
}

/* The whole custom widget. No API setter, tree mutation or counter lives here:
 * this is a function of dashboard_state and the paint context only. */
static void paint_dashboard(pt_widget *widget, pt_paint_context *context, void *user_data) {
    (void)widget;
    dashboard_state *state = user_data;
    if (!context) {
        return;
    }
    float width = pt_paint_width(context);
    float height = pt_paint_height(context);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    /* The callback may be invoked per damage tile. Returning early when the
     * tile is empty is the documented way to skip the whole routine. */
    float clip_x = 0.0f;
    float clip_y = 0.0f;
    float clip_w = 0.0f;
    float clip_h = 0.0f;
    if (!pt_paint_clip_rect(context, &clip_x, &clip_y, &clip_w, &clip_h)) {
        return;
    }

    /* Monospace grid. The values are recomputed from the context on every call,
     * so tile order and repeat count cannot change the result. */
    float ascent = TERMINAL_FONT;
    float line_height = TERMINAL_FONT * 1.45f;
    float cell_width = TERMINAL_FONT * 0.62f;
    float metrics_ascent = 0.0f;
    float metrics_descent = 0.0f;
    float metrics_line = 0.0f;
    float metrics_cell = 0.0f;
    if (pt_paint_font_metrics(context, FONT_FAMILY, TERMINAL_FONT, pt_font_weight_regular,
                              pt_font_slant_upright, &metrics_ascent, &metrics_descent,
                              &metrics_line, &metrics_cell)) {
        ascent = metrics_ascent;
        line_height = fmaxf(metrics_line, TERMINAL_FONT + 7.0f);
        if (metrics_cell > 0.0f) {
            cell_width = metrics_cell;
        }
    }

    float gauge_x = width - PANEL_PAD - GAUGE_WIDTH;
    float terminal_right = gauge_x - 16.0f;
    float terminal_width = fmaxf(0.0f, terminal_right - PANEL_PAD);
    float text_x = PANEL_PAD + 30.0f;
    float rows_top = PANEL_HEADER;
    float rows_height = (float)TERMINAL_ROWS * line_height;

    if (intersects(clip_x, clip_y, clip_w, clip_h, 0.0f, 0.0f, width, PANEL_HEADER)) {
        pt_paint_fill_rounded_rect(context, PANEL_PAD, 15.0f, 10.0f, 10.0f, 5.0f,
                                   pt_rgb(0x9ECE6A));
        pt_paint_draw_text(context, FONT_FAMILY, 18.0f, pt_font_weight_semibold,
                           pt_font_slant_upright, PANEL_PAD + 20.0f, 32.0f,
                           "pt_paint_* dashboard", strlen("pt_paint_* dashboard"),
                           pt_rgb(0xE6EDF7));
        char frame[48];
        snprintf(frame, sizeof(frame), "frame %03u", state->frame);
        float frame_width = pt_paint_measure_text(context, FONT_FAMILY, 12.0f,
                                                  pt_font_weight_regular,
                                                  pt_font_slant_upright, frame, strlen(frame));
        pt_paint_draw_text(context, FONT_FAMILY, 12.0f, pt_font_weight_regular,
                           pt_font_slant_upright, width - PANEL_PAD - frame_width, 30.0f,
                           frame, strlen(frame), pt_rgb(0x6F7C8F));
        pt_paint_draw_line(context, PANEL_PAD, PANEL_HEADER - 1.0f, width - PANEL_PAD,
                           PANEL_HEADER - 1.0f, 1.0f, pt_rgb(0x24303F));
    }

    if (intersects(clip_x, clip_y, clip_w, clip_h, PANEL_PAD, rows_top, terminal_width,
                   rows_height)) {
        /* One push ... */
        pt_paint_push_clip(context, PANEL_PAD, rows_top, terminal_width, rows_height);
        for (int row = 0; row < TERMINAL_ROWS; ++row) {
            float row_y = rows_top + (float)row * line_height;
            if (row == TERMINAL_ROWS - 1) {
                pt_paint_fill_rect(context, PANEL_PAD, row_y, terminal_width, line_height,
                                   pt_rgb(0x172231));
            } else if ((row & 1) == 0) {
                pt_paint_fill_rect(context, PANEL_PAD, row_y, terminal_width, line_height,
                                   pt_rgb(0x0E141D));
            }
            char number[16];
            snprintf(number, sizeof(number), "%02u", (unsigned)(row + 1));
            pt_paint_draw_text(context, FONT_FAMILY, 11.0f, pt_font_weight_regular,
                               pt_font_slant_upright, PANEL_PAD + 6.0f, row_y + ascent,
                               number, strlen(number), pt_rgb(0x56657A));
            pt_paint_draw_text(context, FONT_FAMILY, TERMINAL_FONT, pt_font_weight_regular,
                               pt_font_slant_upright, text_x, row_y + ascent,
                               state->lines[row], strlen(state->lines[row]),
                               row == TERMINAL_ROWS - 1 ? pt_rgb(0xD7E2F2)
                                                        : pt_rgb(0xA9B7C9));
        }
        pt_paint_draw_line(context, PANEL_PAD + 26.0f, rows_top + 4.0f, PANEL_PAD + 26.0f,
                           rows_top + rows_height - 4.0f, 1.0f, pt_rgb(0x263241));
        if (state->caret_on) {
            const char *last = state->lines[TERMINAL_ROWS - 1];
            float caret_x = text_x + (float)strlen(last) * cell_width + 1.0f;
            pt_paint_fill_rect(context, caret_x,
                               rows_top + (float)(TERMINAL_ROWS - 1) * line_height + 3.0f,
                               fmaxf(2.0f, cell_width * 0.5f),
                               fmaxf(6.0f, line_height - 6.0f), pt_rgb(0x9ECE6A));
        }
        /* ... and exactly one pop, on the same path. */
        pt_paint_pop_clip(context);
    }

    if (intersects(clip_x, clip_y, clip_w, clip_h, gauge_x, rows_top, GAUGE_WIDTH,
                   rows_height)) {
        pt_paint_push_clip(context, gauge_x, rows_top, GAUGE_WIDTH, rows_height);
        pt_paint_fill_rounded_rect(context, gauge_x, rows_top, GAUGE_WIDTH, rows_height, 12.0f,
                                   pt_rgb(0x111823));
        pt_paint_stroke_rect(context, gauge_x + 0.5f, rows_top + 0.5f, GAUGE_WIDTH - 1.0f,
                             rows_height - 1.0f, 1.0f, pt_rgb(0x263241));
        pt_paint_draw_text(context, FONT_FAMILY, 11.0f, pt_font_weight_semibold,
                           pt_font_slant_upright, gauge_x + 14.0f, rows_top + 24.0f, "LOAD",
                           strlen("LOAD"), pt_rgb(0x7A8798));

        float bar_x = gauge_x + 14.0f;
        float bar_y = rows_top + 34.0f;
        float bar_width = GAUGE_WIDTH - 28.0f;
        float bar_height = fmaxf(24.0f, rows_height - 86.0f);
        pt_paint_fill_rounded_rect(context, bar_x, bar_y, bar_width, bar_height, 6.0f,
                                   pt_rgb(0x1B2432));
        float fill = bar_height * fminf(fmaxf(state->load, 0.0f), 1.0f);
        pt_color bar_color = state->load > 0.8f   ? pt_rgb(0xF7768E)
                             : state->load > 0.55f ? pt_rgb(0xE0AF68)
                                                   : pt_rgb(0x9ECE6A);
        if (fill > 0.0f) {
            pt_paint_fill_rounded_rect(context, bar_x, bar_y + bar_height - fill, bar_width,
                                       fill, 6.0f, bar_color);
        }
        for (int step = 1; step < 4; ++step) {
            float grid_y = bar_y + bar_height * (float)step / 4.0f;
            pt_paint_draw_line(context, bar_x + 4.0f, grid_y, bar_x + bar_width - 4.0f,
                               grid_y, 1.0f, 0xFFFFFF1Cu);
        }
        char percent[24];
        snprintf(percent, sizeof(percent), "%.0f%%", (double)(state->load * 100.0f));
        float percent_width =
            pt_paint_measure_text(context, FONT_FAMILY, 13.0f, pt_font_weight_medium,
                                  pt_font_slant_upright, percent, strlen(percent));
        pt_paint_draw_text(context, FONT_FAMILY, 13.0f, pt_font_weight_medium,
                           pt_font_slant_upright,
                           gauge_x + (GAUGE_WIDTH - percent_width) * 0.5f,
                           rows_top + rows_height - 14.0f, percent, strlen(percent),
                           pt_rgb(0xD7E2F2));
        pt_paint_pop_clip(context);
    }

    float footer_top = height - PANEL_FOOTER;
    if (footer_top > rows_top + rows_height + 4.0f &&
        intersects(clip_x, clip_y, clip_w, clip_h, 0.0f, footer_top, width, PANEL_FOOTER)) {
        pt_paint_draw_line(context, PANEL_PAD, footer_top, width - PANEL_PAD, footer_top, 1.0f,
                           pt_rgb(0x24303F));
        pt_paint_push_clip(context, PANEL_PAD, footer_top + 1.0f, width - 2.0f * PANEL_PAD,
                           PANEL_FOOTER - 2.0f);
        const char *marquee = "pure · idempotent · clip-balanced · damage-aware · ";
        float marquee_width =
            pt_paint_measure_text(context, FONT_FAMILY, 12.0f, pt_font_weight_regular,
                                  pt_font_slant_upright, marquee, strlen(marquee));
        if (marquee_width > 0.0f) {
            float offset = fmodf((float)state->frame * 22.0f, marquee_width);
            pt_paint_draw_text(context, FONT_FAMILY, 12.0f, pt_font_weight_regular,
                               pt_font_slant_upright, PANEL_PAD - offset,
                               footer_top + 21.0f, marquee, strlen(marquee),
                               pt_rgb(0x6F7C8F));
            pt_paint_draw_text(context, FONT_FAMILY, 12.0f, pt_font_weight_regular,
                               pt_font_slant_upright, PANEL_PAD - offset + marquee_width,
                               footer_top + 21.0f, marquee, strlen(marquee),
                               pt_rgb(0x6F7C8F));
        }
        pt_paint_pop_clip(context);
    }
}

/* Offscreen snapshots go through pt_window_paint_pixels(), which needs no
 * compositor. Two snapshots of the same state must hash identically. */
static bool snapshot_hash(dashboard_state *state, uint32_t *hash_out) {
    int width = pt_window_get_width(state->window);
    int height = pt_window_get_height(state->window);
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    if (!pixels) {
        return false;
    }
    bool painted = pt_window_paint_pixels(state->window, pixels, width, height);
    uint32_t hash = 2166136261u;
    if (painted) {
        for (size_t index = 0; index < count; ++index) {
            hash ^= pixels[index];
            hash *= 16777619u;
        }
    }
    free(pixels);
    if (painted) {
        *hash_out = hash;
    }
    return painted;
}

static void on_append(pt_widget *button, void *user_data) {
    (void)button;
    dashboard_state *state = user_data;
    for (int row = 1; row < TERMINAL_ROWS; ++row) {
        memmove(state->lines[row - 1], state->lines[row], sizeof(state->lines[row]));
    }
    ++state->appended;
    ++state->frame;
    snprintf(state->lines[TERMINAL_ROWS - 1], sizeof(state->lines[TERMINAL_ROWS - 1]),
             "[%03u] appended in on_click; redraw is deferred", state->appended);
    char status[96];
    snprintf(status, sizeof(status), "Line %u appended; requested a widget repaint.",
             state->appended);
    pt_widget_set_text(state->status, status);
    pt_widget_request_redraw(state->panel);
}

static void on_reset(pt_widget *button, void *user_data) {
    (void)button;
    dashboard_state *state = user_data;
    state->appended = 0;
    reset_lines(state, 0);
    pt_widget_set_text(state->status, "Log reset; the paint callback re-reads state.");
    pt_widget_request_redraw(state->panel);
}

static void on_load(pt_widget *slider, float value, void *user_data) {
    (void)slider;
    dashboard_state *state = user_data;
    state->load = fminf(fmaxf(value, 0.0f), 1.0f);
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    pt_widget_get_bounds(state->panel, &x, &y, &width, &height);
    (void)x;
    (void)y;

    /* Only the gauge column is dirty; the terminal text stays untouched. */
    float gauge_width = fminf(GAUGE_WIDTH + 24.0f, width);
    pt_widget_request_redraw_rect(state->panel, width - gauge_width - PANEL_PAD,
                                  PANEL_HEADER, gauge_width,
                                  fmaxf(0.0f, height - PANEL_HEADER - PANEL_FOOTER));
    char status[96];
    snprintf(status, sizeof(status), "Load %.0f%%; damaged only the gauge column.",
             (double)(state->load * 100.0f));
    pt_widget_set_text(state->status, status);
}

static void on_caret_tick(pt_app *app, void *user_data) {
    dashboard_state *state = user_data;
    state->caret_on = !state->caret_on;
    /* A terminal with cached metrics would damage just the caret cell with
     * pt_widget_request_redraw_rect(); this example repaints the panel. */
    pt_widget_request_redraw(state->panel);
    pt_app_add_timeout(app, 530, on_caret_tick, state);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.custom_paint");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x9ECE6A));

    pt_window *window = pt_window_create(app, "Pt Custom Paint", 900, 580);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    dashboard_state state = {.window = window, .load = 0.42f, .caret_on = true};
    reset_lines(&state, 0);

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 22.0f, 22.0f, 22.0f, 22.0f);
    pt_widget_set_gap(root, 12.0f);

    pt_widget *title = pt_label_create(root, "Custom paint");
    pt_widget_set_font_size(title, 26.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);
    pt_widget *subtitle = pt_label_create(
        root, "A terminal-like panel painted with pt_paint_fill_*, draw_line and draw_text. "
              "Tiles and repeats cannot change the result.");
    pt_widget_set_font_size(subtitle, 13.0f);
    pt_widget_set_foreground(subtitle, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(subtitle, true);

    state.panel = pt_box_create(root, pt_direction_column);
    pt_widget_set_height(state.panel, 300.0f);
    pt_widget_set_min_height(state.panel, 260.0f);
    pt_widget_set_background(state.panel, pt_rgb(0x0B1017));
    pt_widget_set_radius(state.panel, 14.0f);
    pt_widget_set_border_width(state.panel, 1.0f);
    pt_widget_set_border_color(state.panel, pt_rgb(0x24303F));
    pt_widget_set_paint_callback(state.panel, paint_dashboard, &state);
    pt_widget_set_a11y_role(state.panel, pt_a11y_role_panel);
    pt_widget_set_a11y_name(state.panel, "Custom paint dashboard");
    pt_widget_set_a11y_description(
        state.panel,
        "A terminal log, a load gauge and a marquee drawn by a pure paint callback.");

    pt_widget *controls = pt_row_create(root);
    pt_widget_set_gap(controls, 10.0f);
    pt_widget_set_align_self(controls, pt_align_center);
    pt_widget *load_label = pt_label_create(controls, "Load");
    pt_widget_set_font_size(load_label, 13.0f);
    pt_widget_set_foreground(load_label, pt_rgb(0x9AA7B8));
    pt_widget *slider = pt_slider_create(controls);
    pt_widget_set_grow(slider, 1.0f);
    pt_slider_set_value(slider, state.load);
    pt_slider_set_on_change(slider, on_load, &state);
    pt_widget_set_tooltip(slider, "Damages only the gauge column through request_redraw_rect");
    pt_widget *append = pt_button_create(controls, "Append line");
    pt_button_set_variant(append, pt_button_primary);
    pt_button_set_on_click(append, on_append, &state);
    pt_widget_set_tooltip(append, "Shift the log, mutate state and request a redraw");
    pt_widget *reset = pt_button_create(controls, "Reset log");
    pt_button_set_variant(reset, pt_button_secondary);
    pt_button_set_on_click(reset, on_reset, &state);

    state.status = pt_label_create(root, "Paint callback active; append a line or move the load.");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);

    pt_widget *footer = pt_label_create(
        root, "The callback never mutates the tree. Clicks and the caret timer own all state "
              "changes, then ask for damage; clips are balanced on every path.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    /* Headless check of paint purity: two identical offscreen frames. */
    uint32_t first_hash = 0;
    uint32_t second_hash = 0;
    if (snapshot_hash(&state, &first_hash) && snapshot_hash(&state, &second_hash)) {
        printf("custom_paint: offscreen %dx%d hash 0x%08X, repeat %s\n",
               pt_window_get_width(window), pt_window_get_height(window), first_hash,
               first_hash == second_hash ? "identical" : "CHANGED");
        fflush(stdout);
    } else {
        fprintf(stderr, "custom_paint: offscreen snapshot unavailable\n");
    }

    pt_window_show(window);
    pt_app_add_timeout(app, 530, on_caret_tick, &state);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
