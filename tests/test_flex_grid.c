/*
 * Responsive layout checks: flex wrap, grid columns and spans.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <string.h>

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static pt_widget *fixed_box(pt_widget *parent, float width, float height) {
    pt_widget *box = pt_box_create(parent, pt_direction_row);
    pt_widget_set_size(box, width, height);
    return box;
}

static void test_wrap(pt_app *app) {
    pt_window *window = pt_window_create(app, "wrap", 500, 520);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *wrap = pt_wrap_create(root);
    pt_widget_set_size(wrap, 300.0f, 200.0f);
    pt_widget_set_align_self(wrap, pt_align_start);
    pt_wrap_set_gap(wrap, 10.0f, 8.0f);
    pt_widget *a = fixed_box(wrap, 100.0f, 40.0f);
    pt_widget *b = fixed_box(wrap, 100.0f, 40.0f);
    pt_widget *c = fixed_box(wrap, 100.0f, 40.0f);

    pt_widget *auto_wrap = pt_wrap_create(root);
    pt_widget_set_width(auto_wrap, 200.0f);
    pt_widget_set_align_self(auto_wrap, pt_align_start);
    pt_wrap_set_gap(auto_wrap, 6.0f, 4.0f);
    fixed_box(auto_wrap, 90.0f, 30.0f);
    pt_widget *e = fixed_box(auto_wrap, 90.0f, 30.0f);
    pt_widget *f = fixed_box(auto_wrap, 90.0f, 30.0f);

    pt_canvas *canvas = pt_canvas_create(500, 520, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect wrap_rect = bounds_of(wrap);
    pt_check_near(wrap_rect.x, 0.0, 0.01);
    pt_check_near(wrap_rect.y, 44.0, 0.01);
    pt_check_near(wrap_rect.w, 300.0, 0.01);
    pt_check_near(wrap_rect.h, 200.0, 0.01);

    pt_rect a_rect = bounds_of(a);
    pt_rect b_rect = bounds_of(b);
    pt_rect c_rect = bounds_of(c);
    pt_check_near(a_rect.x, 0.0, 0.01);
    pt_check_near(a_rect.y, 44.0, 0.01);
    pt_check_near(a_rect.w, 100.0, 0.01);
    pt_check_near(a_rect.h, 40.0, 0.01);
    pt_check_near(b_rect.x, 108.0, 0.01);
    pt_check_near(b_rect.y, 44.0, 0.01);
    pt_check_near(c_rect.x, 0.0, 0.01);
    pt_check_near(c_rect.y, 94.0, 0.01);
    pt_check_near(c_rect.w, 100.0, 0.01);

    /* Auto-height wraps measure their lines; 90 + 4 + 90 exceeds 200. */
    pt_rect auto_rect = bounds_of(auto_wrap);
    pt_check_near(auto_rect.y, 244.0, 0.01);
    pt_check_near(auto_rect.w, 200.0, 0.01);
    pt_check_near(auto_rect.h, 66.0, 0.01);
    pt_rect e_rect = bounds_of(e);
    pt_rect f_rect = bounds_of(f);
    pt_check_near(e_rect.y, 244.0, 0.01);
    pt_check_near(e_rect.x, 94.0, 0.01);
    pt_check_near(f_rect.y, 280.0, 0.01);
    pt_check_near(f_rect.x, 0.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_grid(pt_app *app) {
    pt_window *window = pt_window_create(app, "grid", 500, 320);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *grid = pt_grid_create(root, 3);
    pt_check(grid != NULL);
    pt_check(pt_grid_get_columns(grid) == 3);
    pt_widget_set_width(grid, 310.0f);
    pt_widget_set_align_self(grid, pt_align_start);
    pt_grid_set_gap(grid, 12.0f, 10.0f);

    pt_widget *a = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(a, 30.0f);
    pt_widget *b = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(b, 30.0f);
    pt_widget *c = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(c, 30.0f);
    pt_widget *wide = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(wide, 50.0f);
    pt_widget_set_grid_span(wide, 2);
    pt_check(pt_widget_get_grid_span(wide) == 2);

    pt_canvas *canvas = pt_canvas_create(500, 320, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    pt_rect grid_rect = bounds_of(grid);
    pt_check_near(grid_rect.x, 0.0, 0.01);
    pt_check_near(grid_rect.y, 44.0, 0.01);
    pt_check_near(grid_rect.w, 310.0, 0.01);
    pt_check_near(grid_rect.h, 92.0, 0.01);

    float column = (310.0f - 20.0f) / 3.0f;
    pt_rect a_rect = bounds_of(a);
    pt_rect b_rect = bounds_of(b);
    pt_rect c_rect = bounds_of(c);
    pt_rect w_rect = bounds_of(wide);
    pt_check_near(a_rect.x, 0.0, 0.01);
    pt_check_near(a_rect.y, 44.0, 0.01);
    pt_check_near(a_rect.w, column, 0.01);
    pt_check_near(a_rect.h, 30.0, 0.01);
    pt_check_near(b_rect.x, column + 10.0, 0.01);
    pt_check_near(c_rect.x, (column + 10.0) * 2.0, 0.01);
    pt_check_near(w_rect.x, 0.0, 0.01);
    pt_check_near(w_rect.y, 86.0, 0.01);
    pt_check_near(w_rect.w, column * 2.0 + 10.0, 0.01);
    pt_check_near(w_rect.h, 50.0, 0.01);

    pt_grid_set_columns(grid, 2);
    pt_check(pt_grid_get_columns(grid) == 2);
    pt_grid_set_columns(grid, 0);
    pt_check(pt_grid_get_columns(grid) == 1);
    pt_widget_set_grid_span(wide, 0);
    pt_check(pt_widget_get_grid_span(wide) == 1);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_rtl_mirror(pt_app *app) {
    pt_window *window = pt_window_create(app, "rtl", 500, 400);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    /* A row reverses: the first child takes the right edge. justify_end packs
     * the run against the left edge instead. */
    pt_widget *row = pt_box_create(root, pt_direction_row);
    pt_widget_set_ui_direction(row, pt_ui_direction_rtl);
    pt_widget_set_size(row, 300.0f, 40.0f);
    pt_widget_set_align_self(row, pt_align_start);
    pt_widget_set_gap(row, 10.0f);
    pt_widget *first = fixed_box(row, 60.0f, 20.0f);
    pt_widget *second = fixed_box(row, 60.0f, 20.0f);

    pt_widget *row_end = pt_box_create(root, pt_direction_row);
    pt_widget_set_ui_direction(row_end, pt_ui_direction_rtl);
    pt_widget_set_size(row_end, 300.0f, 40.0f);
    pt_widget_set_align_self(row_end, pt_align_start);
    pt_box_set_justify(row_end, pt_justify_end);
    pt_widget *end_a = fixed_box(row_end, 60.0f, 20.0f);
    pt_widget *end_b = fixed_box(row_end, 60.0f, 20.0f);

    /* A column mirrors only horizontally: start packs right, end packs left,
     * and the vertical start alignment of the children is unchanged. */
    pt_widget *column = pt_box_create(root, pt_direction_column);
    pt_widget_set_ui_direction(column, pt_ui_direction_rtl);
    pt_widget_set_size(column, 200.0f, 100.0f);
    pt_widget_set_align_self(column, pt_align_end);
    column->align_items = pt_align_start;
    pt_widget *col_start = fixed_box(column, 40.0f, 30.0f);
    pt_widget *col_end = fixed_box(column, 40.0f, 30.0f);
    pt_widget_set_align_self(col_end, pt_align_end);

    /* Grid column 0 starts at the right edge; a span grows leftwards. */
    pt_widget *grid = pt_grid_create(root, 3);
    pt_widget_set_ui_direction(grid, pt_ui_direction_rtl);
    pt_widget_set_width(grid, 310.0f);
    pt_widget_set_align_self(grid, pt_align_start);
    pt_grid_set_gap(grid, 12.0f, 10.0f);
    pt_widget *grid_a = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(grid_a, 30.0f);
    pt_widget *grid_b = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(grid_b, 30.0f);
    pt_widget *grid_c = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(grid_c, 30.0f);
    pt_widget *grid_wide = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(grid_wide, 30.0f);
    pt_widget_set_grid_span(grid_wide, 2);

    /* A scroll view keeps its gutter on the left in RTL. */
    pt_widget *scroll = pt_scroll_create(root);
    pt_widget_set_ui_direction(scroll, pt_ui_direction_rtl);
    pt_widget_set_size(scroll, 200.0f, 80.0f);
    pt_widget_set_align_self(scroll, pt_align_start);
    pt_widget *scrolled = fixed_box(scroll, 50.0f, 30.0f);

    pt_canvas *canvas = pt_canvas_create(500, 400, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect row_rect = bounds_of(row);
    pt_check_near(row_rect.x, 0.0, 0.01);
    pt_check_near(row_rect.w, 300.0, 0.01);
    /* Child 0 packed against the right edge first, then child 1; within a slot
     * the child still fills from the slot's left, so the pair reads right to
     * left by slot while each box keeps its width. */
    pt_check_near(bounds_of(first).x, 240.0, 0.01);
    pt_check_near(bounds_of(second).x, 170.0, 0.01);
    pt_check_near(bounds_of(first).w, 60.0, 0.01);

    pt_rect end_a_rect = bounds_of(end_a);
    pt_rect end_b_rect = bounds_of(end_b);
    /* justify_end packs the pair against the trailing edge, child 0 nearest
     * the middle. */
    pt_check_near(end_a_rect.x, 60.0, 0.01);
    pt_check_near(end_b_rect.x, 0.0, 0.01);

    pt_rect column_rect = bounds_of(column);
    pt_check_near(column_rect.x, 300.0, 0.01);
    pt_check_near(bounds_of(col_start).x, 460.0, 0.01);
    pt_check_near(bounds_of(col_start).y, column_rect.y, 0.01);
    pt_check_near(bounds_of(col_end).x, 300.0, 0.01);

    /* Column 0 hugs the right edge and each later column steps left by one
     * column plus a gap; the spanning cell starts one column further in. */
    float grid_column = (310.0f - 20.0f) / 3.0f;
    pt_check_near(bounds_of(grid_a).x, 310.0 - grid_column, 0.01);
    pt_check_near(bounds_of(grid_a).w, grid_column, 0.01);
    /* One column plus a gap is 106.67; the second and third columns step in by
     * that much, and the spanning cell covers columns 1 and 2. */
    pt_check_near(bounds_of(grid_b).x, 106.666664, 0.01);
    pt_check_near(bounds_of(grid_c).x, 0.0, 0.01);
    pt_check_near(bounds_of(grid_wide).x, 106.666664, 0.01);
    pt_check_near(bounds_of(grid_wide).w, grid_column * 2.0 + 10.0, 0.01);
    pt_check_near(bounds_of(grid_wide).y, bounds_of(grid_a).y + 30.0 + 12.0, 0.01);
    pt_check_near(bounds_of(grid_wide).h, 30.0, 0.01);

    pt_rect scroll_rect = bounds_of(scroll);
    pt_check_near(bounds_of(scrolled).x, scroll_rect.x + 10.0, 0.01);

    /* The mirror is per widget: a nested LTR box keeps LTR geometry. */
    pt_widget *inner_ltr = pt_box_create(row_end, pt_direction_row);
    pt_widget_set_ui_direction(inner_ltr, pt_ui_direction_ltr);
    pt_widget_set_size(inner_ltr, 100.0f, 20.0f);
    pt_widget *ltr_a = fixed_box(inner_ltr, 30.0f, 10.0f);
    pt_widget *ltr_b = fixed_box(inner_ltr, 30.0f, 10.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect ltr_rect = bounds_of(inner_ltr);
    pt_check_near(bounds_of(ltr_a).x, ltr_rect.x, 0.01);
    pt_check_near(bounds_of(ltr_b).x, ltr_rect.x + 30.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.flexgrid");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("flex_grid");
    }
    test_wrap(app);
    test_grid(app);
    test_rtl_mirror(app);
    pt_app_destroy(app);
    return pt_test_finish("flex_grid");
}
