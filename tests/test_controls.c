/*
 * Modern control and text editing checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct control_state {
    int changes;
    uint32_t index;
    float value;
} control_state;

static void on_value_change(pt_widget *widget, float value, void *data) {
    (void)widget;
    control_state *state = data;
    state->changes++;
    state->value = value;
}

static void on_index_change(pt_widget *widget, uint32_t index, void *data) {
    (void)widget;
    control_state *state = data;
    state->changes++;
    state->index = index;
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void click_at(pt_window *window, float x, float y, uint32_t time) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, time);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, time);
}

static void type_key(pt_window *window, uint32_t keysym, const char *utf8) {
    pt_window_input_key(window, keysym, 0, utf8, true);
}

static void test_new_controls(pt_app *app) {
    pt_window *window = pt_window_create(app, "controls", 560, 560);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 10.0f);

    pt_widget *checkbox = pt_checkbox_create(root, "Enable turbo");
    pt_widget_set_align_self(checkbox, pt_align_start);
    control_state checks = {0};
    pt_widget_set_on_checked(checkbox, on_value_change, &checks);

    pt_widget *radio_row = pt_row_create(root);
    pt_widget_set_height(radio_row, 24.0f);
    pt_widget_set_gap(radio_row, 16.0f);
    pt_widget *radio_a = pt_radio_create(radio_row, "Alpha");
    pt_widget *radio_b = pt_radio_create(radio_row, "Beta");
    control_state radios = {0};
    pt_widget_set_on_checked(radio_a, on_value_change, &radios);
    pt_widget_set_on_checked(radio_b, on_value_change, &radios);

    pt_widget *progress = pt_progress_create(root);
    pt_widget_set_width(progress, 240.0f);
    pt_widget_set_align_self(progress, pt_align_start);
    pt_widget *spinner = pt_spinner_create(root);
    pt_widget_set_align_self(spinner, pt_align_start);

    const char *labels[] = {"Light", "System", "Dark"};
    pt_widget *segmented = pt_segmented_create(root, labels, 3);
    control_state segments = {0};
    pt_segmented_set_on_change(segmented, on_index_change, &segments);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(560, 560, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect check_rect = bounds_of(checkbox);
    pt_rect radio_a_rect = bounds_of(radio_a);
    pt_rect radio_b_rect = bounds_of(radio_b);
    pt_rect progress_rect = bounds_of(progress);
    pt_rect spinner_rect = bounds_of(spinner);
    pt_rect segment_rect = bounds_of(segmented);
    pt_check(check_rect.w > 60.0f && check_rect.h >= 18.0f);
    pt_check(radio_a_rect.w > 40.0f);
    pt_check_near(progress_rect.w, 240.0, 0.01);
    pt_check_near(progress_rect.h, 8.0, 0.01);
    pt_check_near(spinner_rect.w, 20.0, 0.01);
    pt_check_near(spinner_rect.h, 20.0, 0.01);
    pt_check_near(segment_rect.h, 34.0, 0.01);
    pt_check(segment_rect.w > 200.0f);

    click_at(window, check_rect.x + 20.0f, check_rect.y + check_rect.h * 0.5f, 1000);
    pt_check(pt_widget_get_checked(checkbox));
    pt_check(checks.changes == 1);
    pt_check(checks.value == 1.0f);
    pt_widget_set_enabled(checkbox, false);
    click_at(window, check_rect.x + 20.0f, check_rect.y + check_rect.h * 0.5f, 2000);
    pt_check(pt_widget_get_checked(checkbox));
    pt_widget_set_enabled(checkbox, true);
    pt_widget_set_checked(checkbox, false);
    pt_check(!pt_widget_get_checked(checkbox));

    click_at(window, radio_a_rect.x + 10.0f, radio_a_rect.y + radio_a_rect.h * 0.5f, 3000);
    pt_check(pt_widget_get_checked(radio_a));
    click_at(window, radio_b_rect.x + 10.0f, radio_b_rect.y + radio_b_rect.h * 0.5f, 4000);
    pt_check(pt_widget_get_checked(radio_b));
    pt_check(!pt_widget_get_checked(radio_a));
    pt_check(radios.changes == 2);

    pt_progress_set_value(progress, 0.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_color track = pt_app_get_theme_color(app, pt_theme_track);
    pt_check(pt_canvas_get_pixel(canvas, (int)(progress_rect.x + 120.0f),
                                 (int)(progress_rect.y + 4.0f)) == track);
    pt_progress_set_value(progress, 0.7f);
    pt_check_near(pt_progress_get_value(progress), 0.7, 0.001);
    pt_progress_set_value(progress, 2.0f);
    pt_check_near(pt_progress_get_value(progress), 1.0, 0.001);
    pt_progress_set_value(progress, 0.7f);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_canvas_get_pixel(canvas, (int)(progress_rect.x + 60.0f),
                                 (int)(progress_rect.y + 4.0f)) != track);
    pt_check(pt_canvas_get_pixel(canvas, (int)(progress_rect.x + 230.0f),
                                 (int)(progress_rect.y + 4.0f)) == track);

    pt_progress_set_value(progress, 0.4f);
    pt_progress_set_indeterminate(progress, true);
    float phase = progress->phase;
    float spin_phase = spinner->phase;
    pt_check(pt_widget_update_animations(root, 0.016f));
    pt_check(progress->phase != phase);
    pt_check(spinner->phase > spin_phase);
    pt_progress_set_indeterminate(progress, false);
    pt_check(!pt_progress_get_indeterminate(progress));

    pt_check(pt_segmented_get_selected(segmented) == 0);
    click_at(window, segment_rect.x + segment_rect.w * 0.83f,
             segment_rect.y + segment_rect.h * 0.5f, 5000);
    pt_check(pt_segmented_get_selected(segmented) == 2);
    pt_check(segments.changes == 1 && segments.index == 2);
    pt_segmented_set_selected(segmented, 1);
    pt_check(pt_segmented_get_selected(segmented) == 1);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(pt_segmented_get_selected(segmented) == 2);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(pt_segmented_get_selected(segmented) == 0);
    pt_check(pt_window_paint(window, canvas));

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_entry_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "editing", 520, 320);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget *entry = pt_entry_create(root, "type");
    pt_check(entry != NULL);
    pt_widget *soft = pt_button_create(root, "Soft action");
    pt_button_set_variant(soft, pt_button_soft);
    pt_widget_set_align_self(soft, pt_align_start);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 320, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect entry_rect = bounds_of(entry);
    pt_check(entry_rect.w > 200.0f);

    pt_widget_focus(entry);
    pt_widget_set_text(entry, "hello world");
    uint32_t start = 0;
    uint32_t end = 0;
    pt_check(!pt_entry_get_selection(entry, &start, &end));

    pt_entry_set_selection(entry, 0, 5);
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 0 && end == 5);
    pt_window_input_key(window, XKB_KEY_c, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    char *clipboard = pt_window_clipboard_get(window);
    pt_check(clipboard != NULL && strcmp(clipboard, "hello") == 0);
    pt_free(clipboard);

    pt_window_input_key(window, XKB_KEY_x, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(strcmp(pt_widget_get_text(entry), " world") == 0);
    clipboard = pt_window_clipboard_get(window);
    pt_check(clipboard != NULL && strcmp(clipboard, "hello") == 0);
    pt_free(clipboard);

    pt_check(pt_entry_undo(entry));
    pt_check(strcmp(pt_widget_get_text(entry), "hello world") == 0);
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 0 && end == 5);
    pt_check(pt_entry_redo(entry));
    pt_check(strcmp(pt_widget_get_text(entry), " world") == 0);

    pt_entry_select_all(entry);
    type_key(window, XKB_KEY_X, "X");
    pt_check(strcmp(pt_widget_get_text(entry), "X") == 0);
    pt_check(pt_entry_undo(entry));
    pt_check(strcmp(pt_widget_get_text(entry), " world") == 0);
    pt_check(pt_entry_redo(entry));
    pt_check(strcmp(pt_widget_get_text(entry), "X") == 0);

    pt_widget_set_text(entry, "abcdef");
    pt_window_input_key(window, XKB_KEY_Home, (uint32_t)pt_key_modifier_shift, NULL, true);
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 0 && end == 6);
    type_key(window, XKB_KEY_Z, "Z");
    pt_check(strcmp(pt_widget_get_text(entry), "Z") == 0);
    pt_check(!pt_entry_get_selection(entry, &start, &end));

    pt_widget_set_text(entry, "alpha beta gamma");
    pt_check(pt_window_paint(window, canvas));
    entry_rect = bounds_of(entry);
    float click_x = entry_rect.x + entry->padding_left + 8.0f;
    float click_y = entry_rect.y + entry_rect.h * 0.5f;
    click_at(window, click_x, click_y, 7000);
    click_at(window, click_x, click_y, 7100);
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 0 && end == 5);
    click_at(window, click_x, click_y, 7200);
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 0 && end == 16);

    pt_entry_set_selection(entry, 11, 6);
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 6 && end == 11);
    type_key(window, XKB_KEY_Q, "Q");
    pt_check(strcmp(pt_widget_get_text(entry), "alpha Qgamma") == 0);
    pt_check(pt_window_paint(window, canvas));

    pt_check(pt_window_clipboard_set(window, "public clipboard"));
    clipboard = pt_window_clipboard_get(window);
    pt_check(clipboard != NULL && strcmp(clipboard, "public clipboard") == 0);
    pt_free(clipboard);
    pt_check(!pt_window_clipboard_set(NULL, "nope"));
    pt_check(pt_window_clipboard_get(NULL) == NULL);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_scroll_api(pt_app *app) {
    pt_window *window = pt_window_create(app, "scroll api", 380, 320);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget *scroll = pt_scroll_create(root);
    pt_widget_set_size(scroll, 260.0f, 120.0f);
    pt_widget_set_align_self(scroll, pt_align_start);
    pt_widget *column = pt_column_create(scroll);
    pt_widget_set_gap(column, 0.0f);
    for (int i = 0; i < 20; ++i) {
        pt_widget *row_label = pt_label_create(column, "row");
        pt_widget_set_height(row_label, 30.0f);
    }

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(380, 320, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    float max_offset = pt_scroll_get_max_offset(scroll);
    pt_check(max_offset > 300.0f);
    pt_scroll_set_offset(scroll, 9999.0f);
    pt_check_near(pt_scroll_get_offset(scroll), max_offset, 0.01);
    pt_scroll_set_offset(scroll, -12.0f);
    pt_check_near(pt_scroll_get_offset(scroll), 0.0, 0.01);
    pt_scroll_set_offset(scroll, 60.0f);
    pt_check_near(pt_scroll_get_offset(scroll), 60.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(pt_scroll_get_offset(scroll), 60.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.controls");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("controls");
    }
    test_new_controls(app);
    test_entry_editing(app);
    test_scroll_api(app);
    pt_app_destroy(app);
    return pt_test_finish("controls");
}
