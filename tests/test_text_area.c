/*
 * Multi-line text editor checks: editing, navigation, selection, scrolling,
 * grow-to-content, IME and accessibility.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 0);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 0);
}

static void type_text(pt_window *window, const char *text) {
    for (const char *cursor = text; *cursor; ++cursor) {
        char utf8[2] = {*cursor, '\0'};
        uint32_t keysym = *cursor == '\n' ? XKB_KEY_Return : (uint32_t)(unsigned char)*cursor;
        pt_window_input_key(window, keysym, 0, *cursor == '\n' ? NULL : utf8, true);
    }
}

static void test_editing(pt_window *window, pt_widget *area, pt_canvas *canvas) {
    pt_check(pt_widget_get_a11y_role(area) == pt_a11y_role_text_frame);
    pt_check((pt_widget_get_a11y_state(area) & pt_a11y_state_editable) != 0);
    pt_check(strcmp(pt_widget_get_a11y_name(area), "Write a note") == 0);

    pt_rect rect = bounds_of(area);
    pt_check_near(rect.x, 0.0, 0.01);
    pt_check_near(rect.y, 44.0, 0.01);
    pt_check_near(rect.w, 320.0, 0.01);
    pt_check_near(rect.h, 160.0, 0.01);

    click_at(window, rect.x + 16.0f, rect.y + 16.0f);
    pt_check(pt_widget_has_focus(area));

    type_text(window, "hello");
    type_text(window, "\n");
    type_text(window, "world");
    pt_check(strcmp(pt_widget_get_text(area), "hello\nworld") == 0);
    pt_check(area->cursor == 11);

    pt_window_input_key(window, XKB_KEY_BackSpace, 0, NULL, true);
    pt_check(strcmp(pt_widget_get_text(area), "hello\nworl") == 0);
    type_text(window, "d");
    pt_check(strcmp(pt_widget_get_text(area), "hello\nworld") == 0);

    pt_check(pt_window_paint(window, canvas));

    /* Hard-wrapped lines with equal content exercise vertical navigation. */
    pt_widget_set_text(area, "aaaaa\naaaaa");
    pt_check(area->cursor == 11);
    pt_window_input_key(window, XKB_KEY_Up, 0, NULL, true);
    pt_check(area->cursor == 5);
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(area->cursor == 11);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == 6);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == 11);
    pt_window_input_key(window, XKB_KEY_Home, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(area->cursor == 0);
    pt_window_input_key(window, XKB_KEY_End, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(area->cursor == 11);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(area->cursor == 10);

    /* Shift extends; Backspace deletes the selection; undo/redo restore it. */
    pt_text_area_set_selection(area, 10, 10);
    pt_window_input_key(window, XKB_KEY_Left, (uint32_t)pt_key_modifier_shift, NULL, true);
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 9 && sel_end == 10);
    pt_window_input_key(window, XKB_KEY_BackSpace, 0, NULL, true);
    pt_check(strcmp(pt_widget_get_text(area), "aaaaa\naaaa") == 0);
    pt_check(pt_text_area_undo(area));
    pt_check(strcmp(pt_widget_get_text(area), "aaaaa\naaaaa") == 0);
    pt_check(pt_text_area_redo(area));
    pt_check(strcmp(pt_widget_get_text(area), "aaaaa\naaaa") == 0);
    pt_check(!pt_text_area_redo(area));

    /* Selection endpoints clamp and accept either order; select-all spans it. */
    pt_widget_set_text(area, "alpha\nbeta\ngamma");
    pt_text_area_set_selection(area, 7, 2);
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 2 && sel_end == 7);
    pt_text_area_set_selection(area, 10, 1000);
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 10 && sel_end == 16);
    pt_text_area_select_all(area);
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 0 && sel_end == 16);

    const char *a11y_text = NULL;
    uint32_t caret = 0;
    uint32_t a_start = 0;
    uint32_t a_end = 0;
    pt_check(pt_widget_get_a11y_text(area, &a11y_text, &caret, &a_start, &a_end));
    pt_check(a11y_text != NULL && strcmp(a11y_text, "alpha\nbeta\ngamma") == 0);
    pt_check(a_start == 0 && a_end == 16);

    /* Double-click selects a word using layout coordinates. */
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(pt_window_paint(window, canvas));
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    float word_x = area->x + area->padding_left +
                   pt_text_layout_x_for_position(&layout, 1, 6) + 2.0f;
    float word_y = area->y + area->padding_top + layout.line_height * 1.5f;
    pt_text_layout_free(&layout);
    pt_widget_pointer_multi_click(area, word_x, word_y, 2);
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 6 && sel_end == 10);

    /* Drag selection follows the pointer across lines. */
    pt_widget_pointer_down(area, area->x + area->padding_left + 1.0f,
                           area->y + area->padding_top + 2.0f);
    pt_widget_pointer_drag(area, area->x + area->padding_left + 1.0f,
                           area->y + area->padding_top + 2.0f + 60.0f);
    pt_widget_pointer_up(area, true);
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start < sel_end);

    /* IME text commits into the selection and preedit is exposed/cleared. */
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_ime_preedit(window, "pre");
    pt_check(area->preedit != NULL && strcmp(area->preedit, "pre") == 0);
    pt_window_input_ime_preedit(window, "");
    pt_check(area->preedit == NULL);
    size_t before = strlen(pt_widget_get_text(area));
    pt_window_input_ime_commit(window, "\xE4\xB8\x96\xE7\x95\x8C");
    pt_check(strncmp(pt_widget_get_text(area), "\xE4\xB8\x96\xE7\x95\x8C", 6) == 0);
    pt_check(strlen(pt_widget_get_text(area)) == before + 6);
    pt_check(area->cursor == 6);
    pt_check(area->selection_anchor == area->cursor);
}

static void test_scrolling(pt_window *window, pt_widget *area, pt_canvas *canvas) {
    char text[1024];
    size_t offset = 0;
    for (int i = 0; i < 40; ++i) {
        int written = snprintf(text + offset, sizeof(text) - offset, "line %d\n", i);
        if (written <= 0) {
            break;
        }
        offset += (size_t)written;
    }
    text[offset] = '\0';
    pt_widget_set_text(area, text);
    pt_window_input_key(window, XKB_KEY_Home, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(pt_window_paint(window, canvas));

    pt_layout_ctx ctx = pt_layout_context_for(area);
    float content = pt_text_area_measure_height(area, &ctx, pt_text_area_content_width(area));
    float viewport = area->height - area->padding_top - area->padding_bottom;
    pt_check(content > viewport);
    pt_check_near(area->scroll_content_height, content, 0.01);
    pt_check_near(area->scroll_y, 0.0, 0.01);

    pt_window_input_key(window, XKB_KEY_End, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(area->cursor == strlen(text));
    pt_check(area->scroll_y > 0.0f);
    pt_check_near(area->scroll_y, content - viewport, 0.01);

    pt_window_input_key(window, XKB_KEY_Home, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check_near(area->scroll_y, 0.0, 0.01);

    pt_check(pt_widget_scroll_pixels(area, 50.0f));
    pt_check_near(area->scroll_y, 50.0, 0.01);
    pt_check_near(pt_text_area_content_width(area), 292.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check(area->scroll_y > 0.0f);
    pt_check(pt_widget_kind_is_text_input(pt_widget_get_kind(area)));
    pt_check(pt_widget_cursor(area) == pt_cursor_text);
}

static void test_grow_to_content(pt_window *window, pt_canvas *canvas) {
    pt_widget *root = pt_window_root(window);
    pt_widget *area = pt_text_area_create(root, "grow");
    pt_check(!pt_text_area_get_grow_to_content(area));
    pt_text_area_set_grow_to_content(area, true);
    pt_check(pt_text_area_get_grow_to_content(area));
    pt_widget_set_width(area, 220.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_widget_set_text(area, "one\ntwo\nthree");
    pt_check(pt_window_paint(window, canvas));

    pt_layout_ctx ctx = pt_layout_context_for(area);
    float measured = pt_text_area_measure_height(area, &ctx, 220.0f);
    pt_check(measured > 0.0f);
    pt_check_near(area->height, measured + area->padding_top + area->padding_bottom, 0.01);
    pt_check(area->height < 160.0f);
    float first_height = area->height;

    pt_widget_set_text(area, "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight");
    pt_check(pt_window_paint(window, canvas));
    measured = pt_text_area_measure_height(area, &ctx, 220.0f);
    pt_check(area->height > first_height);
    pt_check_near(area->height, measured + area->padding_top + area->padding_bottom, 0.01);

    pt_text_area_set_grow_to_content(area, false);
    pt_check(!pt_text_area_get_grow_to_content(area));
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(area->height, 160.0, 0.01);
}

/* Finds the horizontal ink extent inside one line band. The area background is
 * the dominant color in the band; antialiased text differs from it. */
static void band_ink(pt_canvas *canvas, int x0, int x1, int y0, int y1, int *min_x,
                     int *max_x) {
    *min_x = -1;
    *max_x = -1;
    uint32_t best_color = 0;
    int best_count = 0;
    for (int x = x0; x < x1; ++x) {
        for (int y = y0; y < y1; ++y) {
            uint32_t color = pt_canvas_get_pixel(canvas, x, y);
            int count = 0;
            for (int sx = x0; sx < x1; ++sx) {
                for (int sy = y0; sy < y1; ++sy) {
                    if (pt_canvas_get_pixel(canvas, sx, sy) == color) {
                        ++count;
                    }
                }
            }
            if (count > best_count) {
                best_count = count;
                best_color = color;
            }
        }
    }
    for (int x = x0; x < x1; ++x) {
        for (int y = y0; y < y1; ++y) {
            if (pt_canvas_get_pixel(canvas, x, y) != best_color) {
                if (*min_x < 0) {
                    *min_x = x;
                }
                *max_x = x;
                break;
            }
        }
    }
}

/* RTL multi-line display, per-paragraph alignment, width wrapping and the
 * logical/visual map consumed by the layout accessors (2.0 wave 3). */
static void test_rtl_layout(pt_app *app) {
    pt_font *probe = pt_font_acquire("DejaVu Sans", pt_font_weight_regular,
                                     pt_font_slant_upright, 14.0f);
    if (!probe || !pt_font_has_glyph(probe, 0x05D0u)) {
        printf("skip text_area: no Hebrew-capable frame font; RTL layout checks skipped\n");
        return;
    }
    pt_window *window = pt_window_create(app, "rtl textarea", 360, 260);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    pt_widget *area = pt_text_area_create(root, NULL);
    pt_widget_set_size(area, 300.0f, 200.0f);
    pt_widget_set_align_self(area, pt_align_start);
    /* "abc" then alef bet gimel: auto resolves each paragraph separately. */
    pt_widget_set_text(area, "abc\n\xD7\x90\xD7\x91\xD7\x92");
    pt_canvas *canvas = pt_canvas_create(360, 260, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));

    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.line_count == 2);
    if (layout.line_count == 2) {
        size_t ltr_start = layout.lines[0].start;
        size_t ltr_end = layout.lines[0].end;
        size_t rtl_start = layout.lines[1].start;
        size_t rtl_end = layout.lines[1].end;
        pt_check(layout.lines[0].base_level == 0);
        pt_check(layout.lines[1].base_level == 1);
        /* LTR line grows right; the RTL line's logical start is its visual
         * right edge and its logical end is the left edge. */
        pt_check_near(pt_text_layout_x_for_position(&layout, 0, ltr_start), 0.0f, 0.01f);
        pt_check(pt_text_layout_x_for_position(&layout, 0, ltr_end) > 0.0f);
        float rtl_width = pt_text_layout_x_for_position(&layout, 1, rtl_start);
        pt_check(rtl_width > 0.0f);
        pt_check_near(pt_text_layout_x_for_position(&layout, 1, rtl_end), 0.0f, 0.01f);
        pt_check(pt_text_layout_x_for_position(&layout, 1, rtl_start + 2) < rtl_width);
        /* Cluster boundaries and visual hit testing through the line map. */
        pt_check(pt_text_layout_position_for_x(&layout, 1, 0.0f) == rtl_end);
        pt_check(pt_text_layout_position_for_x(&layout, 1, rtl_width) == rtl_start);
        size_t boundary = pt_text_layout_position_for_x(&layout, 1, rtl_width);
        pt_check(boundary == rtl_start);
        float mid = (pt_text_layout_x_for_position(&layout, 1, rtl_start) +
                     pt_text_layout_x_for_position(&layout, 1, rtl_start + 2)) *
                    0.5f;
        size_t hit = pt_text_layout_position_for_x(&layout, 1, mid);
        pt_check(hit == rtl_start || hit == rtl_start + 2);
    }

    /* Wrapping is width-driven in logical order; each visible line carries the
     * RTL paragraph base and stays inside the width. */
    pt_widget_set_text(area,
                       "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
                       "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E");
    pt_text_layout_free(&layout);
    memset(&layout, 0, sizeof(layout));
    pt_check(pt_text_layout_build(area, &ctx, 70.0f, &layout));
    pt_check(layout.line_count >= 2);
    for (int i = 0; i < layout.line_count; ++i) {
        pt_check(layout.lines[i].base_level == 1);
        float width = pt_text_layout_x_for_position(&layout, i, layout.lines[i].start);
        pt_check(width <= 71.0f);
        pt_check(pt_text_layout_x_for_position(&layout, i, layout.lines[i].end) <= width + 0.01f);
    }
    pt_text_layout_free(&layout);

    /* A wrapped mixed-script paragraph under a forced RTL base: every line's
     * map is logical-ordered (binary lookup valid) and its logical endpoints
     * sit at the visual edges (start right, end left) once the line contains
     * odd-level text. */
    pt_widget_set_text_direction(area, pt_text_direction_rtl);
    pt_widget_set_text(area,
                       "abc \xD7\x90\xD7\x91\xD7\x92 abc \xD7\x90\xD7\x91\xD7\x92 "
                       "abc \xD7\x90\xD7\x91\xD7\x92");
    memset(&layout, 0, sizeof(layout));
    pt_check(pt_text_layout_build(area, &ctx, 90.0f, &layout));
    pt_check(layout.line_count >= 2);
    int mixed_lines = 0;
    for (int i = 0; i < layout.line_count; ++i) {
        pt_check(layout.lines[i].base_level == 1);
        if (!layout.maps || !layout.maps[i]) {
            pt_check(layout.lines[i].start >= layout.lines[i].end);
            continue;
        }
        const pt_text_visual_map *map = layout.maps[i];
        bool has_odd = false;
        for (size_t c = 0; c < map->count; ++c) {
            if (map->clusters[c].level & 1u) {
                has_odd = true;
            }
            if (c > 0) {
                pt_check(map->clusters[c - 1].start <= map->clusters[c].start);
            }
        }
        float start_x = pt_text_layout_x_for_position(&layout, i, layout.lines[i].start);
        float end_x = pt_text_layout_x_for_position(&layout, i, layout.lines[i].end);
        pt_check(start_x >= -0.01f && start_x <= 91.0f);
        pt_check(end_x >= -0.01f && end_x <= 91.0f);
        if (has_odd) {
            pt_check(start_x >= end_x - 0.01f);
            ++mixed_lines;
        }
    }
    pt_check(mixed_lines >= 1);
    pt_text_layout_free(&layout);
    pt_widget_set_text_direction(area, pt_text_direction_auto);

    /* Per-paragraph alignment: align_start puts LTR left and RTL right;
     * align_end swaps both. pt_widget_set_text_align now reaches the text
     * area instead of the hard-coded start edge. */
    pt_widget_set_text(area, "abc\n\xD7\x90\xD7\x91\xD7\x92");
    pt_widget_set_text_align(area, pt_text_align_start);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    int inner_x = (int)lroundf(area->x + area->padding_left);
    int inner_y = (int)lroundf(area->y + area->padding_top);
    int inner_w = (int)lroundf(area->width - area->padding_left - area->padding_right);
    int line_h = (int)lroundf(layout.line_height);
    int ltr_min_start = -1;
    int ltr_max_start = -1;
    int rtl_min_start = -1;
    int rtl_max_start = -1;
    if (layout.line_count == 2 && line_h > 8) {
        band_ink(canvas, inner_x, inner_x + inner_w, inner_y + 3, inner_y + line_h - 3,
                 &ltr_min_start, &ltr_max_start);
        band_ink(canvas, inner_x, inner_x + inner_w, inner_y + line_h + 3,
                 inner_y + 2 * line_h - 3, &rtl_min_start, &rtl_max_start);
        pt_check(ltr_min_start >= inner_x && ltr_min_start < inner_x + 20);
        pt_check(rtl_min_start > inner_x + 40);
    }
    pt_text_layout_free(&layout);

    pt_widget_set_text_align(area, pt_text_align_end);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    if (layout.line_count == 2 && line_h > 8) {
        int ltr_min_end = -1;
        int ltr_max_end = -1;
        int rtl_min_end = -1;
        int rtl_max_end = -1;
        band_ink(canvas, inner_x, inner_x + inner_w, inner_y + 3, inner_y + line_h - 3,
                 &ltr_min_end, &ltr_max_end);
        band_ink(canvas, inner_x, inner_x + inner_w, inner_y + line_h + 3,
                 inner_y + 2 * line_h - 3, &rtl_min_end, &rtl_max_end);
        pt_check(ltr_min_end > inner_x + 40);
        pt_check(rtl_min_end >= inner_x && rtl_min_end < inner_x + 20);
        pt_check(ltr_min_end != ltr_min_start);
        pt_check(rtl_min_end != rtl_min_start);
    }
    pt_text_layout_free(&layout);

    /* Removed controls keep their logical bytes but collapse in the map. */
    pt_widget_set_text(area, "a\xE2\x80\xAA" "b");
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    float before_control = pt_text_layout_x_for_position(&layout, 0, 1);
    float inside_control = pt_text_layout_x_for_position(&layout, 0, 2);
    float after_control = pt_text_layout_x_for_position(&layout, 0, 4);
    pt_check_near(before_control, inside_control, 0.01f);
    pt_check_near(inside_control, after_control, 0.01f);
    pt_check(pt_text_layout_x_for_position(&layout, 0, 5) > after_control);
    pt_text_layout_free(&layout);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* W3 x W5a interaction: an RTL shell (inherited UI direction) with a wrapped
 * RTL paragraph edits through the visual map, and the caret box follows the
 * line and the visual edge on the wrapped lines. */
static void test_rtl_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "rtl-editing", 420, 300);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    pt_widget *area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 220.0f, 180.0f);
    pt_widget_set_align_self(area, pt_align_start);
    const char *wrapped =
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E \xD7\xA0\xD7\xA1\xD7\xA2 "
        "\xD7\xA3\xD7\xA4\xD7\xA5 \xD7\xA6\xD7\xA7\xD7\xA8 "
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E";
    pt_widget_set_text(area, wrapped);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 300, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.line_count >= 2);
    pt_text_layout_free(&layout);

    size_t line0_end = 0;
    int line_h = 0;
    pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout);
    if (layout.line_count > 0) {
        line0_end = layout.lines[0].end;
        line_h = (int)lroundf(layout.line_height);
    }
    pt_text_layout_free(&layout);
    pt_check(line0_end > 0 && line_h > 6);

    /* Within the first line, End is the visual right (logical byte 0) and Home
     * is the visual left (that line's logical end). */
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == 0);
    int end_x = 0;
    int end_y = 0;
    pt_widget_ime_rect(area, &end_x, &end_y, NULL, NULL);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == line0_end);
    int home_x = 0;
    int home_y = 0;
    pt_widget_ime_rect(area, &home_x, &home_y, NULL, NULL);
    pt_check(home_x < end_x);
    pt_check(home_y == end_y);

    /* Right from the visual left of the RTL line walks the run while x
     * grows and the logical offset falls by one cluster. */
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor < line0_end && area->cursor + 4 >= line0_end);
    int next_x = 0;
    pt_widget_ime_rect(area, &next_x, NULL, NULL, NULL);
    pt_check(next_x > home_x);

    /* Vertical motion keeps a visual goal x and reports a caret on the
     * adjacent wrapped line. */
    pt_text_area_set_selection(area, 4, 4);
    int top_y = 0;
    int top_x = 0;
    pt_widget_ime_rect(area, &top_x, &top_y, NULL, NULL);
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    int down_x = 0;
    int down_y = 0;
    pt_widget_ime_rect(area, &down_x, &down_y, NULL, NULL);
    pt_check(down_x <= top_x);
    pt_check(down_y >= top_y);
    pt_window_input_key(window, XKB_KEY_Up, 0, NULL, true);
    int up_x = 0;
    int up_y = 0;
    pt_widget_ime_rect(area, &up_x, &up_y, NULL, NULL);
    pt_check(up_y <= down_y);
    pt_check(up_x >= down_x);
    pt_check(area->caret_goal_x >= 0.0f);

    /* Shift+Left extends a logical range from the fixed visual-right anchor
     * even though the caret moves to the visual left (the logical successor). */
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_window_input_key(window, XKB_KEY_Left, 1u, NULL, true);
    pt_check(area->selection_anchor == 0);
    size_t caret_after_shift = area->cursor;
    pt_check(caret_after_shift > 0);
    uint32_t sel_start = 99;
    uint32_t sel_end = 0;
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 0 && sel_end == caret_after_shift);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Logical caret movement in a text area (2.1): Left/Right move by UTF-8
 * boundary across soft wraps and Home/End stop at the logical line ends, while
 * vertical motion and Ctrl+Home/End keep their 2.0 semantics. */
static void test_logical_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-textarea", 460, 320);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget *area = pt_text_area_create(root, "note");
    pt_check(area != NULL);
    const char *mixed = "abc \xD7\x90\xD7\x91\xD7\x92 123";
    pt_widget_set_text(area, mixed);
    pt_widget_set_size(area, 400.0f, 120.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(460, 320, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));

    /* Default visual: End is the visual right edge (byte 4). */
    pt_check(pt_widget_resolved_caret_movement(area) == pt_caret_movement_visual);
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == 4);

    pt_widget_set_caret_movement(area, pt_caret_movement_logical);
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == 14);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == 0);
    const size_t logical_right[] = {1, 2, 3, 4, 6, 8, 10, 11, 12, 13, 14};
    for (size_t i = 0; i < sizeof(logical_right) / sizeof(logical_right[0]); ++i) {
        pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
        pt_check(area->cursor == logical_right[i]);
    }
    const size_t logical_left[] = {13, 12, 11, 10, 8, 6, 4, 3, 2, 1, 0};
    for (size_t i = 0; i < sizeof(logical_left) / sizeof(logical_left[0]); ++i) {
        pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
        pt_check(area->cursor == logical_left[i]);
    }

    /* Shift still extends a logical range from the fixed anchor. */
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 1u, NULL, true);
    pt_check(area->selection_anchor == 0 && area->cursor == 14);

    pt_canvas_destroy(canvas);
    pt_window_free(window);

    /* Wrapped RTL line: logical Home/End are the line's byte start/end, and a
     * right step crosses the soft wrap into the next UTF-8 boundary. */
    window = pt_window_create(app, "logical-textarea-rtl", 420, 300);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 220.0f, 180.0f);
    pt_widget_set_align_self(area, pt_align_start);
    const char *wrapped =
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E \xD7\xA0\xD7\xA1\xD7\xA2 "
        "\xD7\xA3\xD7\xA4\xD7\xA5 \xD7\xA6\xD7\xA7\xD7\xA8 "
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E";
    pt_widget_set_text(area, wrapped);
    pt_widget_set_caret_movement(area, pt_caret_movement_logical);
    pt_window_show(window);
    canvas = pt_canvas_create(420, 300, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.line_count >= 2);
    size_t line0_end = layout.line_count > 0 ? layout.lines[0].end : 0;
    size_t total = layout.display_len;
    pt_text_layout_free(&layout);
    pt_check(line0_end > 0 && total > line0_end);

    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == line0_end);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == 0);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == 2);

    pt_text_area_set_selection(area, line0_end, line0_end);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == pt_utf8_next_boundary(area->text, total, line0_end));

    pt_window_input_key(window, XKB_KEY_End, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(area->cursor == total);
    pt_window_input_key(window, XKB_KEY_Home, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(area->cursor == 0);

    /* Vertical motion keeps the visual goal-x rule. */
    pt_text_area_set_selection(area, 4, 4);
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(area->cursor >= line0_end);
    pt_check(area->caret_goal_x >= 0.0f);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Soft-wrap boundaries are shared by both adjacent lines (the previous line's
 * end and the next line's start are the same byte). The caret's affinity picks
 * the line the caret box, Home/End, the next visual step and a click act on:
 * trailing stays on the upper line, leading belongs to the lower one. A hard
 * newline keeps its own byte and never moves. The un-biased layout query keeps
 * the pre-affinity (trailing) answer, so plain LTR behaviour is unchanged. */
static void test_soft_wrap_affinity(pt_app *app) {
    pt_window *window = pt_window_create(app, "soft-wrap-affinity", 420, 380);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    pt_widget *area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 220.0f, 140.0f);
    pt_widget_set_align_self(area, pt_align_start);
    const char *wrapped =
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E \xD7\xA0\xD7\xA1\xD7\xA2 "
        "\xD7\xA3\xD7\xA4\xD7\xA5 \xD7\xA6\xD7\xA7\xD7\xA8 "
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E";
    pt_widget_set_text(area, wrapped);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 380, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));
    window->caret_visible = true;

    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.line_count >= 2);
    size_t boundary = layout.lines[0].end;
    size_t line1_end = layout.lines[1].end;
    float line_h = layout.line_height;
    /* The same byte has two visual stops: line 0's visual left and line 1's
     * visual right (Hebrew lines are visual right-to-left). Home/End targets
     * are the opposite edges of the line they act on. */
    float line0_right = pt_text_layout_x_for_position_bias(&layout, 0, 0,
                                                           pt_text_bias_leading);
    float line0_left = pt_text_layout_x_for_position_bias(&layout, 0, boundary,
                                                          pt_text_bias_trailing);
    float line1_right = pt_text_layout_x_for_position_bias(&layout, 1, boundary,
                                                           pt_text_bias_leading);
    float line1_left = pt_text_layout_x_for_position_bias(&layout, 1, line1_end,
                                                          pt_text_bias_leading);
    pt_check(boundary == layout.lines[1].start && boundary > 0);
    pt_check(line1_end > boundary);
    pt_text_layout_free(&layout);

    int line0_y = (int)lroundf(area->y + area->padding_top);
    int line1_y = (int)lroundf(area->y + area->padding_top + line_h);
    pt_check(line1_y > line0_y);
    float inner_x = area->x + area->padding_left;
    int x = 0;
    int y = 0;

    /* Leading affinity at the boundary is the first character of line 1: the
     * IME rectangle sits on line 1 at that line's visual right edge. */
    pt_text_area_set_selection(area, boundary, boundary);
    pt_check(area->caret_affinity == pt_text_bias_leading);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line1_y);
    pt_check_near((float)x, inner_x + line1_right, 1.0f);

    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == boundary);
    pt_check(area->caret_affinity == pt_text_bias_leading);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line1_y);
    pt_check_near((float)x, inner_x + line1_right, 1.0f);

    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == line1_end);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line1_y);
    pt_check_near((float)x, inner_x + line1_left, 1.0f);

    /* Trailing affinity belongs to line 0: the control case for the same byte. */
    pt_text_area_set_selection(area, boundary, boundary);
    area->caret_affinity = pt_text_bias_trailing;
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line0_y);
    pt_check_near((float)x, inner_x + line0_left, 1.0f);

    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == 0);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line0_y);
    pt_check_near((float)x, inner_x + line0_right, 1.0f);

    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == boundary);
    pt_check(area->caret_affinity == pt_text_bias_trailing);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line0_y);
    pt_check_near((float)x, inner_x + line0_left, 1.0f);

    /* Clicking line 1's visual start keeps the caret on line 1, and clicking
     * line 0's visual end keeps it on line 0. */
    float click_x = inner_x + line1_right + 1.0f;
    float click_y = area->y + area->padding_top + line_h * 1.5f;
    pt_text_bias click_bias = pt_text_bias_trailing;
    pt_check(pt_text_area_position_at_bias(area, &ctx, click_x, click_y, &click_bias) ==
              boundary);
    pt_check(click_bias == pt_text_bias_leading);
    pt_widget_pointer_down(area, click_x, click_y);
    pt_check(area->cursor == boundary && area->caret_affinity == pt_text_bias_leading);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line1_y);
    pt_widget_pointer_up(area, true);

    click_x = inner_x + 0.5f;
    click_y = area->y + area->padding_top + line_h * 0.5f;
    click_bias = pt_text_bias_leading;
    pt_check(pt_text_area_position_at_bias(area, &ctx, click_x, click_y, &click_bias) ==
              boundary);
    pt_check(click_bias == pt_text_bias_trailing);
    pt_widget_pointer_down(area, click_x, click_y);
    pt_check(area->cursor == boundary && area->caret_affinity == pt_text_bias_trailing);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line0_y);
    pt_widget_pointer_up(area, true);

    /* A selection spanning wrapped RTL lines collapses to its visual edge:
     * Right to the end line's visual right byte (the line's logical start), Left
     * to the start line's visual left byte (the line's logical end). */
    pt_text_area_set_selection(area, 10, 60);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == 49);
    pt_check(area->selection_anchor == area->cursor);
    pt_text_area_set_selection(area, 10, 60);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(area->cursor == 49);
    pt_check(area->selection_anchor == area->cursor);

    /* Pure LTR wrap: the un-biased query keeps the old upper-line answer for
     * existing callers, while a trailing caret still renders and navigates on
     * line 0 and a leading caret on line 1. */
    pt_widget *plain = pt_text_area_create(root, "ltr");
    pt_check(plain != NULL);
    pt_widget_set_text(plain,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    pt_widget_set_size(plain, 220.0f, 100.0f);
    pt_widget_set_align_self(plain, pt_align_start);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(plain));

    pt_layout_ctx lctx = pt_layout_context_for(plain);
    pt_text_layout latin = {0};
    pt_check(pt_text_layout_build(plain, &lctx, pt_text_area_content_width(plain), &latin));
    pt_check(latin.line_count >= 2);
    size_t latin_boundary = latin.lines[0].end;
    size_t latin_line1_end = latin.lines[1].end;
    float latin_h = latin.line_height;
    pt_check(latin_boundary == latin.lines[1].start &&
             latin_boundary < latin.display_len);
    pt_check(pt_text_layout_line_for_position(&latin, latin_boundary) == 0);
    pt_check(pt_text_layout_line_for_position_bias(&latin, latin_boundary,
                                                   pt_text_bias_trailing) == 0);
    pt_check(pt_text_layout_line_for_position_bias(&latin, latin_boundary,
                                                   pt_text_bias_leading) == 1);
    float latin_line0_end = pt_text_layout_x_for_position_bias(&latin, 0, latin_boundary,
                                                               pt_text_bias_trailing);
    float latin_line1_start = pt_text_layout_x_for_position_bias(&latin, 1, latin_boundary,
                                                                 pt_text_bias_leading);
    pt_text_layout_free(&latin);
    int latin_line0_y = (int)lroundf(plain->y + plain->padding_top);
    int latin_line1_y = (int)lroundf(plain->y + plain->padding_top + latin_h);
    pt_check(latin_line1_y > latin_line0_y);
    float latin_x = plain->x + plain->padding_left;

    pt_text_area_set_selection(plain, latin_boundary, latin_boundary);
    pt_check(plain->caret_affinity == pt_text_bias_leading);
    pt_widget_ime_rect(plain, &x, &y, NULL, NULL);
    pt_check(y == latin_line1_y);
    pt_check_near((float)x, latin_x + latin_line1_start, 1.0f);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(plain->cursor == latin_line1_end);
    pt_widget_ime_rect(plain, &x, &y, NULL, NULL);
    pt_check(y == latin_line1_y);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(plain->cursor == latin_boundary);
    pt_widget_ime_rect(plain, &x, &y, NULL, NULL);
    pt_check(y == latin_line1_y);

    pt_text_area_set_selection(plain, latin_boundary, latin_boundary);
    plain->caret_affinity = pt_text_bias_trailing;
    pt_widget_ime_rect(plain, &x, &y, NULL, NULL);
    pt_check(y == latin_line0_y);
    pt_check_near((float)x, latin_x + latin_line0_end, 1.0f);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(plain->cursor == latin_boundary);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(plain->cursor == 0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Visual edges of one line's map as the editor sees them (the same arithmetic
 * as widget.c's static visual_line_edge): the cluster with the smallest x0
 * starts the visual left edge, the cluster with the largest x1 ends the visual
 * right edge, and an RTL cluster's logical start sits at its visual right. The
 * right byte is the visual edge's own start, exactly what the collapse lands
 * on. Returns the byte and the local x of each edge. */
static bool visual_map_edges(const pt_text_visual_map *map, size_t *left, float *left_x,
                             size_t *right, float *right_x) {
    if (!map || map->count == 0) {
        return false;
    }
    const pt_text_visual_cluster *best_left = &map->clusters[0];
    const pt_text_visual_cluster *best_right = &map->clusters[0];
    for (size_t i = 1; i < map->count; ++i) {
        if (map->clusters[i].x0 < best_left->x0) {
            best_left = &map->clusters[i];
        }
        if (map->clusters[i].x1 > best_right->x1) {
            best_right = &map->clusters[i];
        }
    }
    *left = (best_left->level & 1u) ? best_left->end : best_left->start;
    *left_x = best_left->x0;
    *right = (best_right->level & 1u) ? best_right->start : best_right->end;
    *right_x = best_right->x1;
    return true;
}

/* M24 W1: a plain arrow over a multi-line mapped selection collapses to the
 * selection's visual edge - the start line's visual edge on the way out of the
 * range and the end line's on the way in - and the logical range, its painting
 * and the anchor stay the documented logical min/max until the collapse clears
 * them. The bidi paragraph wraps inside a pure LTR area, so every endpoint row
 * carries a visual map while the paragraph base stays LTR. */
static void test_visual_collapse(pt_app *app) {
    pt_window *window = pt_window_create(app, "visual-collapse", 420, 320);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 14.0f, 12.0f, 14.0f, 12.0f);
    pt_widget *area = pt_text_area_create(root, "note");
    pt_check(area != NULL);
    pt_widget_set_size(area, 140.0f, 200.0f);
    pt_widget_set_align_self(area, pt_align_start);
    /* abc <alef-bet-gimel> def <dalet-he-vav> ghi <zayin-het-tet> klmn */
    const char *mixed =
        "abc \xD7\x90\xD7\x91\xD7\x92 def \xD7\x93\xD7\x94\xD7\x95 ghi "
        "\xD7\x96\xD7\x97\xD7\x98 klmn";
    pt_widget_set_text(area, mixed);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 320, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));

    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.line_count == 2);
    if (layout.line_count != 2) {
        pt_text_layout_free(&layout);
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    size_t boundary = layout.lines[0].end;
    pt_check(boundary == layout.lines[1].start);
    /* The visual edges of both endpoint lines, read from the maps the painter
     * uses: LTR-base rows still reorder their Hebrew runs (the cluster with the
     * smallest x0 starts the visual left edge, the smallest start_x the right
     * one). */
    const pt_text_visual_map *map0 = layout.maps[0];
    const pt_text_visual_map *map1 = layout.maps[1];
    pt_check(map0 != NULL && map0->count > 0);
    pt_check(map1 != NULL && map1->count > 0);
    if (!map0 || !map1 || map0->count == 0 || map1->count == 0) {
        pt_text_layout_free(&layout);
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    size_t line0_left = 0;
    size_t line0_right = 0;
    size_t line1_left = 0;
    size_t line1_right = 0;
    float left0_x = 0.0f;
    float right0_x = 0.0f;
    float left1_x = 0.0f;
    float right1_x = 0.0f;
    pt_check(visual_map_edges(map0, &line0_left, &left0_x, &line0_right, &right0_x));
    pt_check(visual_map_edges(map1, &line1_left, &left1_x, &line1_right, &right1_x));
    pt_check(line0_left == 0 && line0_right == boundary);
    pt_check(line1_left == boundary && line1_right == layout.display_len);
    pt_check(left0_x < right0_x && left1_x < right1_x);
    float line_h = layout.line_height;
    float line0_left_x = pt_text_layout_x_for_position_bias(&layout, 0, line0_left,
                                                           pt_text_bias_leading);
    float line0_right_x = pt_text_layout_x_for_position_bias(&layout, 0, line0_right,
                                                            pt_text_bias_leading);
    pt_text_layout_free(&layout);

    float inner_x = area->x + area->padding_left;
    int line0_y = (int)lroundf(area->y + area->padding_top);
    int line1_y = (int)lroundf(area->y + area->padding_top + line_h);
    pt_check(line1_y > line0_y);

    /* A selection covering the whole first row still paints that logical range
     * (its highlight is present before any key), so a collapse that fired too
     * early would be caught as a missing highlight. */
    pt_text_area_set_selection(area, 0, boundary);
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == 0 && sel_end == boundary);
    pt_check(pt_window_paint(window, canvas));
    uint32_t unselected = pt_canvas_get_pixel(canvas, (int)lroundf(inner_x + 1.0f),
                                              (int)lroundf(area->y + area->padding_top + 1.0f));
    int row0_x = (int)lroundf(inner_x + line0_left_x + 4.0f);
    int row0_y = line0_y + (int)(line_h * 0.5f);
    uint32_t painted = pt_canvas_get_pixel(canvas, row0_x, row0_y);
    pt_check(painted != unselected);
    /* Right collapses to the range's visual right: the range ends exactly on the
     * shared soft-wrap boundary, so its end row is the upper line and the
     * landing is that line's visual right byte. */
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == boundary);
    pt_check(area->selection_anchor == area->cursor);
    pt_check(!pt_text_area_get_selection(area, &sel_start, &sel_end));
    int x = 0;
    int y = 0;
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line0_y);
    pt_check_near((float)x, inner_x + line0_right_x, 1.0f);

    /* The same range on Left collapses to the visual left of its first row. */
    pt_text_area_set_selection(area, 0, boundary);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(area->cursor == line0_left);
    pt_check(area->selection_anchor == area->cursor);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line0_y);
    pt_check_near((float)x, inner_x + line0_left_x, 1.0f);

    /* A three-stop range spanning the wrap (start on row 0, end inside row 1)
     * collapses at the endpoint rows, not at the row the caret happens to sit
     * on: Right lands on row 1's visual right, Left on row 0's visual left. */
    pt_text_area_set_selection(area, 10, 30);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == line1_right);
    pt_check(area->selection_anchor == area->cursor);
    pt_text_area_set_selection(area, 10, 30);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(area->cursor == line0_left);
    pt_check(area->selection_anchor == area->cursor);

    /* A single-line mapped selection keeps its in-line visual edge rule. */
    pt_text_area_set_selection(area, 0, 22);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == 22);
    pt_text_area_set_selection(area, 0, 22);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(area->cursor == 0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);

    /* An LTR area without bidi content keeps the logical collapse. */
    window = pt_window_create(app, "ltr-collapse", 420, 320);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    root = pt_window_root(window);
    pt_widget_set_padding(root, 14.0f, 12.0f, 14.0f, 12.0f);
    area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 140.0f, 200.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_widget_set_text(area, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    pt_window_show(window);
    canvas = pt_canvas_create(420, 320, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));
    pt_text_area_set_selection(area, 5, 25);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == 25);
    pt_check(area->selection_anchor == area->cursor);
    pt_text_area_set_selection(area, 5, 25);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(area->cursor == 5);
    pt_check(area->selection_anchor == area->cursor);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}


/* M24 W1: Shift+arrow crosses a soft-wrap boundary one visual row per press.
 * At a mapped line's visual edge - where the in-line visual step has nowhere
 * to go - the caret enters the next physical row (top-to-bottom) at the column
 * nearest its current x. The logical anchor does not move, the range stays
 * logical min/max, and the caret's affinity is re-pinned after
 * entry_set_cursor so the box renders and navigates on the row it entered (the
 * IME y is that row). A hard newline has its own byte and never crosses; an
 * LTR line without bidi content keeps the plain code-unit step. */
static void test_shift_cross_wrap(pt_app *app) {
    pt_window *window = pt_window_create(app, "shift-cross-wrap", 420, 380);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    pt_widget *area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 140.0f, 260.0f);
    pt_widget_set_align_self(area, pt_align_start);
    /* Ten three-letter Hebrew words: several wrapped rows at this width. */
    const char *wrapped =
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E \xD7\xA0\xD7\xA1\xD7\xA2 "
        "\xD7\xA3\xD7\xA4\xD7\xA5 \xD7\xA6\xD7\xA7\xD7\xA8 \xD7\xA9\xD7\xAA "
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E \xD7\xA0\xD7\xA1\xD7\xA2 "
        "\xD7\xA3\xD7\xA4\xD7\xA5 \xD7\xA6\xD7\xA7\xD7\xA8";
    pt_widget_set_text(area, wrapped);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 380, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));

    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.line_count >= 3);
    if (layout.line_count < 3) {
        pt_text_layout_free(&layout);
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    /* The visual edges of the first three rows, from the same x0/x1 rule the
     * widget's visual_line_edge uses. Every row here is RTL, so a row's
     * logical start sits at its visual right and its logical end at its
     * visual left. */
    size_t vleft[3] = {0};
    size_t vright[3] = {0};
    float vleft_x[3] = {0.0f};
    float vright_x[3] = {0.0f};
    bool edges_ok = true;
    for (int l = 0; l < 3; ++l) {
        if (!visual_map_edges(layout.maps[l], &vleft[l], &vleft_x[l], &vright[l],
                              &vright_x[l])) {
            edges_ok = false;
            break;
        }
    }
    size_t line2_start = layout.lines[2].start;
    size_t line2_end = layout.lines[2].end;
    size_t line1_start = layout.lines[1].start;
    pt_check(layout.lines[0].end == line1_start);
    pt_check(layout.lines[1].end == line2_start);
    float line_h = layout.line_height;
    pt_text_layout_free(&layout);
    if (!edges_ok) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    /* Sanity: the wrap boundaries hold and each row's visual edges straddle
     * its byte range (RTL: visual left is the logical end). */
    pt_check(line1_start > 0 && line2_start > line1_start);
    pt_check(vright[1] == line1_start && vleft[1] == line2_start);
    pt_check(vleft[1] > vright[1]);

    int area_x = (int)lroundf(area->x);
    int area_y = (int)lroundf(area->y);
    int line_y[3] = {
        area_y + (int)lroundf(area->padding_top),
        area_y + (int)lroundf(area->padding_top + line_h),
        area_y + (int)lroundf(area->padding_top + 2.0f * line_h),
    };
    pt_check(line_y[1] > line_y[0] && line_y[2] > line_y[1]);
    int x = 0;
    int y = 0;

    /* Forward across the wrap: from the start of row 2 (its logical start ==
     * visual right) Shift+Right has no in-line visual stop and crosses to row 3
     * at the column nearest its x. The anchor is the original byte and the
     * caret renders on row 3 (one row down). */
    pt_text_area_set_selection(area, line2_start, line2_start);
    pt_check(area->caret_affinity == pt_text_bias_leading);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line_y[2]);
    pt_check_near((float)x, (float)area_x + area->padding_left + vright_x[2], 1.0f);
    pt_window_input_key(window, XKB_KEY_Right, (uint32_t)pt_key_modifier_shift, NULL, true);
    pt_check(area->cursor == line2_end); /* row 3's visual right == its logical start */
    pt_check(area->selection_anchor == line2_start);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line_y[2] + (int)lroundf(line_h)); /* row 3, one row below */
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    pt_check(pt_text_area_get_selection(area, &sel_start, &sel_end));
    pt_check(sel_start == (uint32_t)line2_start && sel_end == (uint32_t)line2_end);

    /* One row per press: a second Shift+Right crosses exactly one more row, and
     * the anchor never moves. */
    size_t after_one = area->cursor;
    pt_window_input_key(window, XKB_KEY_Right, (uint32_t)pt_key_modifier_shift, NULL, true);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(area->cursor != after_one);
    pt_check(y == line_y[2] + (int)lroundf(2.0f * line_h)); /* row 4 */
    pt_check(area->selection_anchor == line2_start);

    /* Backward across the wrap: from row 2's logical end (its visual left, byte
     * line2_end with trailing affinity) Shift+Left crosses up to row 1. The
     * anchor stays and the caret renders one row above. */
    pt_text_area_set_selection(area, line2_end, line2_end);
    area->caret_affinity = pt_text_bias_trailing;
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line_y[2]);
    pt_window_input_key(window, XKB_KEY_Left, (uint32_t)pt_key_modifier_shift, NULL, true);
    pt_check(area->cursor == line2_start); /* row 1's visual left byte */
    pt_check(area->selection_anchor == line2_end);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line_y[2] - (int)lroundf(line_h)); /* row 1, one row above */

    /* Logical caret movement keeps the plain logical step: Shift+Right at the
     * end of a row stays a code-point step on the same row, crossing nothing
     * special (here it lands on the next byte of the same run). */
    pt_widget_set_caret_movement(area, pt_caret_movement_logical);
    pt_text_area_set_selection(area, line2_start, line2_start);
    pt_window_input_key(window, XKB_KEY_Right, (uint32_t)pt_key_modifier_shift, NULL, true);
    /* One logical code point (the Hebrew letters are two bytes), on the same
     * row: no visual re-derivation, no wrap crossing. */
    pt_check(area->cursor > line2_start);
    pt_check(area->cursor == line2_start + 2);
    pt_check(area->selection_anchor == line2_start);
    pt_widget_ime_rect(area, &x, &y, NULL, NULL);
    pt_check(y == line_y[2]);

    pt_canvas_destroy(canvas);
    pt_window_free(window);

    /* A plain LTR area without bidi content keeps the code-unit step across a
     * wrap: Shift+Right from a row's end reaches the next byte, not a
     * re-derived column on the following row. */
    window = pt_window_create(app, "ltr-cross", 420, 320);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    root = pt_window_root(window);
    pt_widget_set_padding(root, 14.0f, 12.0f, 14.0f, 12.0f);
    area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 120.0f, 200.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_widget_set_text(area, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    pt_window_show(window);
    canvas = pt_canvas_create(420, 320, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(area));
    pt_layout_ctx lctx = pt_layout_context_for(area);
    pt_text_layout ltr = {0};
    pt_check(pt_text_layout_build(area, &lctx, pt_text_area_content_width(area), &ltr));
    pt_check(ltr.line_count >= 2);
    if (ltr.line_count >= 2) {
        size_t l0_end = ltr.lines[0].end;
        /* Place on the trailing edge of row 0, then Shift+Right: plain step to
         * the next byte, still a code unit (no visual re-derivation). */
        pt_text_area_set_selection(area, l0_end - 1, l0_end - 1);
        area->caret_affinity = pt_text_bias_trailing;
        pt_window_input_key(window, XKB_KEY_Right, (uint32_t)pt_key_modifier_shift, NULL, true);
        pt_check(area->cursor == l0_end);
        pt_check(area->selection_anchor == l0_end - 1);
    }
    pt_text_layout_free(&ltr);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}


#define INLINE_PLACEHOLDER "\xEF\xBF\xBC"

static pt_icon_image *make_inline_icon(void) {
    uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) {
        pixels[i] = 0x80FF0000u | (uint32_t)i;
    }
    size_t size = 0;
    uint8_t *png = pt_png_encode_argb_premultiplied(pixels, 4, 4, &size);
    pt_check(png != NULL && size > 0);
    pt_icon_image *image = png ? pt_icon_image_load_memory(png, size) : NULL;
    free(png);
    return image;
}

/* W3c inline images in the text area: selection endpoints snap to the object
 * edges (one atomic rectangle), the caret box follows the raised line height,
 * typing shifts the descriptor, IME deletion never splits U+FFFC and undo/redo
 * restore text and image together. */
static void test_inline_image_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "inline-image-area", 420, 240);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *area = pt_text_area_create(pt_window_root(window), "");
    pt_widget_set_size(area, 360.0f, 180.0f);
    pt_check(area != NULL);
    pt_icon_image *icon = make_inline_icon();
    pt_check(icon != NULL);
    if (!area || !icon) {
        pt_icon_image_destroy(icon);
        pt_window_free(window);
        return;
    }
    pt_widget_set_text(area, "ab" INLINE_PLACEHOLDER "cd");
    pt_text_image entry = {0};
    entry.start = 2;
    entry.image = icon;
    entry.width = 16.0f;
    entry.height = 24.0f;
    entry.align = pt_text_image_align_middle;
    memcpy(entry.alt, "chart", 6);
    pt_check(pt_widget_set_text_images(area, &entry, 1));
    pt_window_show(window);
    pt_check(pt_widget_focus(area));

    /* Both endpoints inside the placeholder snap outward to the whole object. */
    pt_text_area_set_selection(area, 3, 4);
    uint32_t start = 0;
    uint32_t end = 0;
    pt_check(pt_text_area_get_selection(area, &start, &end));
    pt_check(start == 2 && end == 5);
    pt_text_area_set_selection(area, 4, 4);
    pt_text_area_set_selection(area, 4, 2);
    pt_check(pt_text_area_get_selection(area, &start, &end));
    pt_check(start == 2 && end == 5);

    /* The caret is as tall as the composed line, which the 24px image raises. */
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    pt_check(layout.has_runs);
    pt_check(layout.line_height >= 24.0f);
    int caret_x = 0;
    int caret_y = 0;
    int caret_w = 0;
    int caret_h = 0;
    pt_text_area_caret_box(area, &ctx, &caret_x, &caret_y, &caret_w, &caret_h);
    pt_check(caret_w > 0);
    pt_check(caret_h >= 24);
    pt_check_near(caret_h, layout.line_height, 1.0);
    pt_text_layout_free(&layout);

    /* Editing before the image shifts the descriptor; undo restores it. */
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_z, 0, "z", true);
    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(area, 0, &got));
    pt_check(got.start == 3);
    pt_check(pt_text_area_undo(area));
    pt_check(pt_widget_get_text_image(area, 0, &got));
    pt_check(got.start == 2);
    pt_check(strcmp(pt_widget_get_text(area), "ab" INLINE_PLACEHOLDER "cd") == 0);

    /* Backspace just after the image deletes the whole object atomically. */
    pt_text_area_set_selection(area, 5, 5);
    pt_check(pt_widget_ime_apply(area, "Q", 1, 0, true));
    pt_check(strcmp(pt_widget_get_text(area), "abQcd") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 0);
    pt_check(pt_text_area_undo(area));
    pt_check(strcmp(pt_widget_get_text(area), "ab" INLINE_PLACEHOLDER "cd") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 1);
    pt_check(pt_widget_get_text_image(area, 0, &got));
    pt_check(got.start == 2 && got.image == icon);
    pt_check(strcmp(got.alt, "chart") == 0);
    pt_check(pt_text_area_redo(area));
    pt_check(strcmp(pt_widget_get_text(area), "abQcd") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 0);
    pt_check(pt_text_area_undo(area));
    pt_check(strcmp(pt_widget_get_text(area), "ab" INLINE_PLACEHOLDER "cd") == 0);

    pt_icon_image_destroy(icon);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.textarea");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("text_area");
    }

    pt_window *window = pt_window_create(app, "textarea", 460, 560);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    pt_widget *area = pt_text_area_create(root, "Write a note");
    pt_widget_set_size(area, 320.0f, 160.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_canvas *canvas = pt_canvas_create(460, 560, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    test_editing(window, area, canvas);
    test_scrolling(window, area, canvas);
    test_grow_to_content(window, canvas);
    test_rtl_layout(app);
    test_rtl_editing(app);
    test_logical_editing(app);
    test_soft_wrap_affinity(app);
    test_visual_collapse(app);
    test_shift_cross_wrap(app);
    test_inline_image_editing(app);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("text_area");
}
