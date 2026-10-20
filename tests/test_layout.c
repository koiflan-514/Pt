/*
 * Layout engine checks: flex distribution, alignment, margins, scrolling.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void test_flex_row(pt_app *app) {
    pt_window *window = pt_window_create(app, "flex", 600, 400);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *row = pt_row_create(root);
    pt_widget_set_height(row, 100.0f);
    pt_widget_set_padding(row, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(row, 10.0f);
    pt_widget *fixed = pt_box_create(row, pt_direction_row);
    pt_widget_set_width(fixed, 50.0f);
    pt_widget_set_height(fixed, 20.0f);
    pt_widget *flex_a = pt_box_create(row, pt_direction_row);
    pt_widget_set_height(flex_a, 20.0f);
    pt_widget_set_grow(flex_a, 1.0f);
    pt_widget *flex_b = pt_box_create(row, pt_direction_row);
    pt_widget_set_height(flex_b, 20.0f);
    pt_widget_set_grow(flex_b, 2.0f);

    pt_canvas *canvas = pt_canvas_create(600, 400, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect row_rect = bounds_of(row);
    pt_check_near(row_rect.x, 0.0, 0.01);
    pt_check_near(row_rect.y, 44.0, 0.01);
    pt_check_near(row_rect.w, 600.0, 0.01);
    pt_check_near(row_rect.h, 100.0, 0.01);

    pt_rect a = bounds_of(fixed);
    pt_rect b = bounds_of(flex_a);
    pt_rect c = bounds_of(flex_b);
    pt_check_near(a.x, 10.0, 0.01);
    pt_check_near(a.y, 54.0, 0.01);
    pt_check_near(a.w, 50.0, 0.01);
    pt_check_near(a.h, 80.0, 0.01);
    pt_check_near(b.x, 70.0, 0.01);
    pt_check_near(c.x, 250.0, 0.01);
    pt_check_near(b.w, 170.0, 0.01);
    pt_check_near(c.w, 340.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_alignment_and_margins(pt_app *app) {
    pt_window *window = pt_window_create(app, "align", 300, 200);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    root->align_items = pt_align_center;

    pt_widget *child = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(child, 100.0f, 40.0f);
    pt_widget_set_margin(child, 10.0f, 0.0f, 0.0f, 0.0f);

    pt_canvas *canvas = pt_canvas_create(300, 200, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect rect = bounds_of(child);
    pt_check_near(rect.x, 100.0, 0.01);
    pt_check_near(rect.y, 54.0, 0.01);
    pt_check_near(rect.w, 100.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_scroll_clamp(pt_app *app) {
    pt_window *window = pt_window_create(app, "scroll", 300, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *scroll = pt_scroll_create(root);
    pt_widget_set_height(scroll, 200.0f);
    pt_widget_set_width(scroll, 300.0f);
    pt_widget *content = pt_column_create(scroll);
    pt_widget_set_gap(content, 0.0f);
    for (int i = 0; i < 10; ++i) {
        pt_widget *label = pt_label_create(content, "row");
        pt_widget_set_height(label, 30.0f);
    }

    pt_canvas *canvas = pt_canvas_create(300, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(scroll->scroll_content_height, 300.0, 1.0);
    pt_check_near(scroll->scroll_y, 0.0, 0.01);

    pt_widget_wheel(scroll, 1.0f);
    pt_check_near(scroll->scroll_y, 48.0, 0.01);
    pt_widget_wheel(scroll, 10.0f);
    pt_check_near(scroll->scroll_y, 100.0, 1.0);
    pt_widget_wheel(scroll, -1.0f);
    pt_check_near(scroll->scroll_y, 52.0, 1.0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_hidden_and_wrap(pt_app *app) {
    pt_window *window = pt_window_create(app, "wrap", 240, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *first = pt_label_create(root, "first");
    pt_widget_set_height(first, 20.0f);
    pt_widget *hidden = pt_label_create(root, "hidden");
    pt_widget_set_height(hidden, 50.0f);
    pt_widget *second = pt_label_create(root, "second");
    pt_widget_set_height(second, 20.0f);
    pt_widget_set_visible(hidden, false);

    pt_widget *wrapped = pt_label_create(root, "a long paragraph that needs several lines");
    pt_widget_set_label_wrap(wrapped, true);
    pt_widget_set_width(wrapped, 100.0f);

    pt_canvas *canvas = pt_canvas_create(240, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    pt_rect first_rect = bounds_of(first);
    pt_rect second_rect = bounds_of(second);
    pt_rect wrapped_rect = bounds_of(wrapped);
    pt_check_near(first_rect.y, 54.0, 0.01);
    pt_check_near(second_rect.y, 74.0, 0.01);
    pt_check(wrapped_rect.h > 20.0f);
    pt_check_near(wrapped_rect.x, 10.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_min_max(pt_app *app) {
    pt_window *window = pt_window_create(app, "bounds", 400, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *box = pt_box_create(root, pt_direction_row);
    pt_widget_set_align_self(box, pt_align_start);
    pt_widget_set_width(box, 10.0f);
    pt_widget_set_min_width(box, 120.0f);
    pt_widget_set_max_width(box, 200.0f);
    pt_widget_set_height(box, 40.0f);

    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect rect = bounds_of(box);
    pt_check_near(rect.w, 120.0, 0.01);

    pt_widget_set_width(box, 300.0f);
    pt_check(pt_window_paint(window, canvas));
    rect = bounds_of(box);
    pt_check_near(rect.w, 200.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.layout");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("layout");
    }
    test_flex_row(app);
    test_alignment_and_margins(app);
    test_scroll_clamp(app);
    test_hidden_and_wrap(app);
    test_min_max(app);
    pt_app_destroy(app);
    return pt_test_finish("layout");
}
