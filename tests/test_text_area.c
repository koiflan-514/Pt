/*
 * Multi-line text editor checks: editing, navigation, selection, scrolling,
 * grow-to-content, IME and accessibility.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

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

    pt_canvas_destroy(canvas);
    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("text_area");
}
