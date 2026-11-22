/*
 * Multi-line text editor checks: editing, navigation, selection, scrolling,
 * grow-to-content, IME and accessibility.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

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

    pt_canvas_destroy(canvas);
    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("text_area");
}
