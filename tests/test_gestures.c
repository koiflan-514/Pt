/*
 * Gesture, long-press and kinetic scrolling checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

typedef struct gesture_log {
    int count;
    pt_gesture_event events[16];
} gesture_log;

typedef struct stylus_log {
    int count;
    pt_stylus_event last;
} stylus_log;

static void log_gesture(pt_window *window, const pt_gesture_event *event, void *user_data) {
    (void)window;
    gesture_log *log = user_data;
    if (log->count < (int)(sizeof(log->events) / sizeof(log->events[0]))) {
        log->events[log->count] = *event;
    }
    ++log->count;
}

static void log_stylus(pt_window *window, const pt_stylus_event *event, void *user_data) {
    (void)window;
    stylus_log *log = user_data;
    log->last = *event;
    ++log->count;
}

static void count_click(pt_widget *widget, void *user_data) {
    (void)widget;
    ++*(int *)user_data;
}

static pt_window *make_window(pt_app *app, const char *title, int width, int height) {
    pt_window *window = pt_window_create(app, title, width, height);
    pt_check(window != NULL);
    return window;
}

static pt_canvas *paint_window(pt_window *window) {
    pt_canvas *canvas = pt_canvas_create(window->width, window->height, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    return canvas;
}

static void test_swipe_and_pinch(pt_app *app) {
    pt_window *window = make_window(app, "gestures", 420, 360);
    if (!window) {
        return;
    }
    pt_widget *zone = pt_box_create(pt_window_root(window), pt_direction_row);
    pt_widget_set_size(zone, 240.0f, 120.0f);
    pt_widget_set_align_self(zone, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = paint_window(window);
    float cx = zone->x + 60.0f;
    float cy = zone->y + 40.0f;
    pt_window_input_pointer_enter(window, cx, cy);

    gesture_log log = {0};
    pt_window_set_gesture_callback(window, log_gesture, &log);

    pt_window_gesture_input(window, pt_gesture_kind_swipe, pt_gesture_phase_begin, 3,
                            0.0f, 0.0f, 1.0f, 0.0f);
    pt_check(log.count == 1);
    pt_check(log.events[0].kind == pt_gesture_kind_swipe);
    pt_check(log.events[0].phase == pt_gesture_phase_begin);
    pt_check(log.events[0].fingers == 3);
    pt_check(log.events[0].target == zone);
    pt_check_near(log.events[0].x, cx, 0.01);
    pt_check_near(log.events[0].y, cy, 0.01);
    pt_check_near(log.events[0].scale, 1.0, 0.0001);

    pt_window_gesture_input(window, pt_gesture_kind_swipe, pt_gesture_phase_update, 0,
                            10.0f, -4.0f, 1.0f, 0.0f);
    pt_window_gesture_input(window, pt_gesture_kind_swipe, pt_gesture_phase_update, 0,
                            5.0f, 2.0f, 1.0f, 0.0f);
    pt_check(log.count == 3);
    pt_check(log.events[2].phase == pt_gesture_phase_update);
    pt_check_near(log.events[2].dx, 15.0, 0.001);
    pt_check_near(log.events[2].dy, -2.0, 0.001);
    pt_check_near(log.events[2].x, cx + 15.0, 0.01);
    pt_check_near(log.events[2].y, cy - 2.0, 0.01);
    pt_check(log.events[2].fingers == 3);

    pt_window_gesture_input(window, pt_gesture_kind_swipe, pt_gesture_phase_end, 0,
                            0.0f, 0.0f, 1.0f, 0.0f);
    pt_check(log.count == 4);
    pt_check(log.events[3].phase == pt_gesture_phase_end);
    pt_check(!window->gesture_active);

    int before = log.count;
    pt_window_gesture_input(window, pt_gesture_kind_swipe, pt_gesture_phase_update, 0,
                            1.0f, 1.0f, 1.0f, 0.0f);
    pt_check(log.count == before);

    pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_begin, 2,
                            0.0f, 0.0f, 1.0f, 0.0f);
    pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_update, 2,
                            3.0f, 4.0f, 1.5f, 0.5f);
    pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_update, 2,
                            3.0f, 4.0f, 2.0f, 0.25f);
    pt_check(log.count == 7);
    pt_check(log.events[6].kind == pt_gesture_kind_pinch);
    pt_check_near(log.events[6].dx, 6.0, 0.001);
    pt_check_near(log.events[6].dy, 8.0, 0.001);
    pt_check_near(log.events[6].scale, 2.0, 0.001);
    pt_check_near(log.events[6].rotation, 0.75, 0.001);
    pt_check_near(log.events[6].x, cx + 6.0, 0.01);
    pt_check_near(log.events[6].y, cy + 8.0, 0.01);
    pt_check(log.events[6].target == zone);
    pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_end, 0,
                            0.0f, 0.0f, 1.0f, 0.0f);

    pt_window_gesture_input(window, pt_gesture_kind_hold, pt_gesture_phase_begin, 3,
                            0.0f, 0.0f, 1.0f, 0.0f);
    pt_window_gesture_input(window, pt_gesture_kind_hold, pt_gesture_phase_cancel, 0,
                            0.0f, 0.0f, 1.0f, 0.0f);
    pt_check(log.events[log.count - 2].kind == pt_gesture_kind_hold);
    pt_check(log.events[log.count - 1].phase == pt_gesture_phase_cancel);
    pt_check(log.events[log.count - 1].fingers == 3);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static pt_window *make_long_press_window(pt_app *app, gesture_log *log,
                                         pt_widget **target_out) {
    pt_window *window = make_window(app, "long press", 360, 300);
    if (!window) {
        return NULL;
    }
    pt_widget *zone = pt_box_create(pt_window_root(window), pt_direction_row);
    pt_widget_set_size(zone, 200.0f, 100.0f);
    pt_widget_set_align_self(zone, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(window->width, window->height, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_canvas_destroy(canvas);
    pt_window_set_gesture_callback(window, log_gesture, log);
    *target_out = zone;
    return window;
}

static void test_long_press(pt_app *app) {
    gesture_log log = {0};
    pt_widget *zone = NULL;
    pt_window *window = make_long_press_window(app, &log, &zone);
    if (!window) {
        return;
    }
    pt_window_set_long_press_delay(window, 40);
    float cx = zone->x + 40.0f;
    float cy = zone->y + 30.0f;
    pt_window_input_pointer_enter(window, cx, cy);
    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 11, 5000);
    pt_check(window->long_press_timer != 0);
    pt_check(window->press_target == zone);
    pt_check(pt_window_interaction_deadline(window) == (int64_t)window->long_press_timer);

    pt_window_tick(window);
    pt_check(log.count == 0);

    window->long_press_timer = (uint64_t)pt_now_ms() - 1;
    pt_window_tick(window);
    pt_check(log.count == 1);
    pt_check(log.events[0].kind == pt_gesture_kind_hold);
    pt_check(log.events[0].phase == pt_gesture_phase_begin);
    pt_check(log.events[0].fingers == 1);
    pt_check(log.events[0].target == zone);
    pt_check_near(log.events[0].x, cx, 0.01);
    pt_check_near(log.events[0].y, cy, 0.01);
    pt_check(pt_window_interaction_deadline(window) == -1);

    pt_window_input_pointer_button(window, 0x110u, false, cx, cy, 0, 5000);
    pt_check(log.count == 2);
    pt_check(log.events[1].kind == pt_gesture_kind_hold);
    pt_check(log.events[1].phase == pt_gesture_phase_end);
    pt_check(window->long_press_timer == 0);
    pt_check(!window->long_press_fired);

    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 12, 6000);
    pt_window_input_pointer_button(window, 0x110u, false, cx, cy, 0, 6000);
    pt_check(log.count == 2);

    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 13, 7000);
    pt_window_input_pointer_motion(window, cx + 30.0f, cy, 7001);
    pt_check(window->long_press_timer == 0);
    pt_check(pt_window_interaction_deadline(window) == -1);
    pt_window_tick(window);
    pt_check(log.count == 2);
    pt_window_input_pointer_button(window, 0x110u, false, cx + 30.0f, cy, 0, 7002);

    pt_window_set_long_press_delay(window, 0);
    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 14, 8000);
    pt_check(window->long_press_timer == 0);
    pt_window_input_pointer_button(window, 0x110u, false, cx, cy, 0, 8000);

    pt_window_set_long_press_delay(window, 40);
    pt_window_input_touch_down(window, 3, cx, cy, 9000, 15);
    window->long_press_timer = (uint64_t)pt_now_ms() - 1;
    pt_window_tick(window);
    pt_check(log.count == 3);
    pt_check(log.events[2].kind == pt_gesture_kind_hold);
    pt_window_input_touch_cancel(window);
    pt_check(log.count == 4);
    pt_check(log.events[3].phase == pt_gesture_phase_cancel);

    pt_window_free(window);
}

static void test_momentum(pt_app *app) {
    pt_window *window = make_window(app, "momentum", 380, 320);
    if (!window) {
        return;
    }
    pt_widget *scroll = pt_scroll_create(pt_window_root(window));
    pt_widget_set_size(scroll, 260.0f, 120.0f);
    pt_widget_set_align_self(scroll, pt_align_start);
    pt_widget *column = pt_column_create(scroll);
    pt_widget_set_gap(column, 0.0f);
    for (int i = 0; i < 20; ++i) {
        pt_widget *label = pt_label_create(column, "row");
        pt_widget_set_height(label, 30.0f);
    }
    pt_window_show(window);
    pt_canvas *canvas = paint_window(window);
    float max_offset = pt_scroll_get_max_offset(scroll);
    pt_check(max_offset > 300.0f);
    pt_check(pt_window_get_momentum_scrolling(window));
    float cx = scroll->x + 40.0f;
    float cy = scroll->y + 40.0f;
    pt_window_input_pointer_enter(window, cx, cy);

    pt_window_momentum_start(window, 20.0f, cx, cy);
    pt_check(!window->momentum_active);
    pt_window_momentum_start(window, 900.0f, cx, cy);
    pt_check(window->momentum_active);
    pt_check(window->animating);

    float previous = pt_scroll_get_offset(scroll);
    bool advanced = false;
    for (int i = 0; i < 120 && window->momentum_active; ++i) {
        pt_window_momentum_advance(window, 0.016f);
        float current = pt_scroll_get_offset(scroll);
        pt_check(current >= previous);
        if (current > previous) {
            advanced = true;
        }
        previous = current;
    }
    pt_check(advanced);
    pt_check(!window->momentum_active);
    pt_check(pt_scroll_get_offset(scroll) > 0.0f);

    pt_scroll_set_offset(scroll, 0.0f);
    pt_window_momentum_start(window, 1200.0f, cx, cy);
    pt_check(window->momentum_active);
    pt_window_input_axis(window, cx, cy, 1.0f);
    pt_check(!window->momentum_active);

    pt_scroll_set_offset(scroll, 0.0f);
    pt_window_momentum_start(window, 1200.0f, cx, cy);
    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 21, 10000);
    pt_check(!window->momentum_active);
    pt_window_input_pointer_button(window, 0x110u, false, cx, cy, 0, 10000);

    pt_window_set_momentum_scrolling(window, false);
    pt_check(!pt_window_get_momentum_scrolling(window));
    pt_window_momentum_start(window, 1200.0f, cx, cy);
    pt_check(!window->momentum_active);
    pt_window_set_momentum_scrolling(window, true);

    pt_scroll_set_offset(scroll, max_offset - 0.5f);
    pt_window_momentum_start(window, 1500.0f, cx, cy);
    for (int i = 0; i < 20 && window->momentum_active; ++i) {
        pt_window_momentum_advance(window, 0.016f);
    }
    pt_check_near(pt_scroll_get_offset(scroll), max_offset, 0.01);
    pt_check(!window->momentum_active);

    pt_window_input_pointer_leave(window);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_stylus(pt_app *app) {
    pt_window *window = make_window(app, "stylus", 320, 240);
    if (!window) {
        return;
    }
    pt_widget *button = pt_button_create(pt_window_root(window), "Stylus");
    pt_widget_set_size(button, 120.0f, 40.0f);
    pt_widget_set_align_self(button, pt_align_start);
    int clicked = 0;
    pt_button_set_on_click(button, count_click, &clicked);
    pt_window_show(window);
    pt_canvas *canvas = paint_window(window);

    stylus_log log = {0};
    pt_window_set_stylus_callback(window, log_stylus, &log);
    float cx = button->x + button->width * 0.5f;
    float cy = button->y + button->height * 0.5f;

    pt_window_stylus_input(window, pt_stylus_phase_enter, pt_stylus_tool_pen, cx, cy, 0.0f,
                           0.2f, 4.0f, -3.0f, 0.5f, 0);
    pt_check(log.count == 1);
    pt_check(log.last.phase == pt_stylus_phase_enter);
    pt_check(log.last.tool == pt_stylus_tool_pen);
    pt_check_near(log.last.x, cx, 0.01);
    pt_check_near(log.last.y, cy, 0.01);
    pt_check(window->pointer_inside);

    pt_window_stylus_input(window, pt_stylus_phase_down, pt_stylus_tool_pen, cx, cy, 0.8f,
                           0.1f, 4.0f, -3.0f, 0.5f, PT_STYLUS_BUTTON_BARREL);
    pt_check(log.count == 2);
    pt_check(log.last.phase == pt_stylus_phase_down);
    pt_check_near(log.last.pressure, 0.8, 0.0001);
    pt_check(log.last.buttons == PT_STYLUS_BUTTON_BARREL);
    pt_check(window->stylus_down);

    pt_window_stylus_input(window, pt_stylus_phase_motion, pt_stylus_tool_pen, cx + 4.0f,
                           cy + 2.0f, 0.6f, 0.1f, 4.0f, -3.0f, 0.5f,
                           PT_STYLUS_BUTTON_BARREL | PT_STYLUS_BUTTON_SECONDARY);
    pt_check(log.count == 3);
    pt_check(log.last.phase == pt_stylus_phase_motion);
    pt_check(log.last.buttons ==
             (PT_STYLUS_BUTTON_BARREL | PT_STYLUS_BUTTON_SECONDARY));

    pt_window_stylus_input(window, pt_stylus_phase_up, pt_stylus_tool_pen, cx + 4.0f,
                           cy + 2.0f, 0.0f, 0.3f, 4.0f, -3.0f, 0.5f, 0);
    pt_check(log.count == 4);
    pt_check(log.last.phase == pt_stylus_phase_up);
    pt_check(!window->stylus_down);
    pt_check(clicked == 1);

    pt_window_stylus_input(window, pt_stylus_phase_leave, pt_stylus_tool_eraser, cx, cy,
                           0.0f, 0.5f, 4.0f, -3.0f, 0.5f, 0);
    pt_check(log.last.tool == pt_stylus_tool_eraser);
    pt_check(log.count == 5);
    pt_check(log.last.phase == pt_stylus_phase_leave);
    pt_check(!window->pointer_inside);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_guards(void) {
    pt_window_set_gesture_callback(NULL, NULL, NULL);
    pt_window_set_long_press_delay(NULL, 10);
    pt_window_set_momentum_scrolling(NULL, false);
    pt_check(!pt_window_get_momentum_scrolling(NULL));
    pt_window_momentum_start(NULL, 100.0f, 0.0f, 0.0f);
    pt_window_momentum_stop(NULL);
    pt_window_momentum_advance(NULL, 0.016f);
    pt_check(!pt_window_interaction_tick(NULL, 0.016f));
    pt_check(pt_window_interaction_deadline(NULL) == -1);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.gestures");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("gestures");
    }
    test_swipe_and_pinch(app);
    test_long_press(app);
    test_momentum(app);
    test_stylus(app);
    test_guards();
    pt_app_destroy(app);
    return pt_test_finish("gestures");
}
