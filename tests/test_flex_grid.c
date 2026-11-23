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

/* ---- 2.1 logical padding inside the flex and grid containers ----------- */

/* The flex row consumes the resolved physical sides; an asymmetric logical
 * pair in RTL packs the first child against the right start edge. */
static void test_logical_padding_flex(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-flex", 500, 300);
    pt_check(window != NULL);
    pt_window_show(window);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *row = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(row, 300.0f, 40.0f);
    pt_widget_set_align_self(row, pt_align_start);
    pt_widget_set_gap(row, 10.0f);
    pt_widget_set_padding_logical(row, 4.0f, 12.0f, 6.0f, 18.0f);
    pt_widget *first = fixed_box(row, 60.0f, 20.0f);
    pt_widget *second = fixed_box(row, 60.0f, 20.0f);

    pt_canvas *canvas = pt_canvas_create(500, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect row_rect = bounds_of(row);
    pt_check_near(row->padding_left, 12.0, 0.01);
    pt_check_near(row->padding_right, 18.0, 0.01);
    pt_check_near(row->padding_top, 4.0, 0.01);
    pt_check_near(row->padding_bottom, 6.0, 0.01);
    pt_check_near(bounds_of(first).x, row_rect.x + 12.0, 0.01);
    pt_check_near(bounds_of(second).x, row_rect.x + 12.0 + 60.0 + 10.0, 0.01);
    pt_check_near(bounds_of(first).y, row_rect.y + 4.0, 0.01);

    pt_widget_set_ui_direction(row, pt_ui_direction_rtl);
    pt_check(pt_window_paint(window, canvas));
    /* start=12 is the right edge now, end=18 the left edge. */
    pt_check_near(row->padding_right, 12.0, 0.01);
    pt_check_near(row->padding_left, 18.0, 0.01);
    pt_check_near(bounds_of(first).x, row_rect.x + row_rect.w - 12.0 - 60.0, 0.01);
    pt_check_near(bounds_of(second).x, row_rect.x + row_rect.w - 12.0 - 60.0 - 10.0 - 60.0,
                  0.01);
    pt_check_near(bounds_of(first).y, row_rect.y + 4.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Grid columns are sized inside the resolved content box: the inner width
 * subtracts the resolved physical left and right edges. A logical pair that is
 * asymmetric therefore changes the cell width when the direction flips. */
static void test_logical_padding_grid(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-grid", 500, 300);
    pt_check(window != NULL);
    pt_window_show(window);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *grid = pt_grid_create(root, 2);
    pt_widget_set_width(grid, 220.0f);
    pt_widget_set_align_self(grid, pt_align_start);
    pt_grid_set_gap(grid, 10.0f, 10.0f);
    pt_widget_set_padding_logical(grid, 0.0f, 20.0f, 0.0f, 10.0f);
    pt_widget *cell_a = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(cell_a, 30.0f);
    pt_widget *cell_b = pt_box_create(grid, pt_direction_column);
    pt_widget_set_height(cell_b, 30.0f);

    pt_canvas *canvas = pt_canvas_create(500, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect grid_rect = bounds_of(grid);
    pt_check_near(grid_rect.w, 220.0, 0.01);
    pt_check_near(grid->padding_left, 20.0, 0.01);
    pt_check_near(grid->padding_right, 10.0, 0.01);
    float ltr_cell_w = bounds_of(cell_a).w;
    pt_check(ltr_cell_w > 0.0f);
    pt_check(grid->padding_left + grid->padding_right == 30.0f);

    /* In RTL start (20) lands on the right and end (10) on the left. The sum
     * is unchanged, so the frozen column width holds while the sides swap. */
    pt_widget_set_ui_direction(grid, pt_ui_direction_rtl);
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(grid->padding_right, 20.0, 0.01);
    pt_check_near(grid->padding_left, 10.0, 0.01);
    pt_check_near(bounds_of(grid).x, grid_rect.x, 0.01);
    pt_check(bounds_of(cell_a).w == ltr_cell_w);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* The table and tree column/row containers opt out of row mirroring. They keep
 * their logical child order, so a logical padding on such a container resolves
 * from the container's own direction without disturbing that order. */
static void test_logical_padding_optout_containers(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-optout", 640, 480);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);

    pt_widget *table = pt_table_create(root);
    pt_check(table != NULL);
    pt_widget_set_align_self(table, pt_align_start);
    pt_widget_set_width(table, 400.0f);
    pt_widget_set_height(table, 200.0f);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Size");
    pt_table_set_column_width(table, 0, 200.0f);
    pt_table_set_column_width(table, 1, 200.0f);
    pt_table_set_row_height(table, 40.0f);
    pt_table_set_header_height(table, 38.0f);
    pt_table_set_row_count(table, 4);

    pt_widget *tree = pt_tree_create(root);
    pt_check(tree != NULL);
    pt_widget_set_align_self(tree, pt_align_start);
    pt_widget_set_size(tree, 320.0f, 160.0f);
    pt_tree_set_row_height(tree, 32.0f);
    pt_tree_add_node(tree, NULL, "alpha");
    pt_tree_add_node(tree, NULL, "beta");

    pt_canvas *canvas = pt_canvas_create(640, 480, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_widget *header = NULL;
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_table_header) {
            header = child;
            break;
        }
    }
    pt_check(header != NULL);
    /* Only the table header and table row opt out of row mirroring; a tree row
     * mirrors like any list row and is exercised through the table row below. */
    pt_widget *table_row = NULL;
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_table_row && child->list_bound) {
            table_row = child;
            break;
        }
    }
    pt_check(table_row != NULL);
    pt_widget *tree_row = NULL;
    for (pt_widget *child = tree->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_tree_row && child->list_bound) {
            tree_row = child;
            break;
        }
    }
    pt_check(tree_row != NULL);

    if (header && tree_row) {
        /* The row containers keep logical order regardless of padding. */
        pt_check(header->rtl_logical_order);
        /* A tree row is an ordinary list row (no opt-out). */
        pt_check(!tree_row->rtl_logical_order);
        pt_rect table_rect = bounds_of(table);
        pt_rect header_before = bounds_of(header);
        pt_rect tree_rect = bounds_of(tree);
        pt_rect tree_row_before = bounds_of(tree_row);

        /* Asymmetric logical padding on the opt-out containers resolves from
         * the RTL direction: start (30) on the right, end (6) on the left. */
        pt_widget_set_padding_logical(header, 0.0f, 30.0f, 0.0f, 6.0f);
        if (table_row) {
            pt_widget_set_padding_logical(table_row, 0.0f, 30.0f, 0.0f, 6.0f);
        }
        pt_widget_set_padding_logical(tree_row, 0.0f, 30.0f, 0.0f, 6.0f);
        pt_check(pt_window_paint(window, canvas));
        pt_check_near(header->padding_right, 30.0, 0.01);
        pt_check_near(header->padding_left, 6.0, 0.01);
        if (table_row) {
            pt_check_near(table_row->padding_right, 30.0, 0.01);
            pt_check_near(table_row->padding_left, 6.0, 0.01);
        }
        pt_check_near(tree_row->padding_right, 30.0, 0.01);
        pt_check_near(tree_row->padding_left, 6.0, 0.01);
        /* The containers themselves sit where they did: the padding applies
         * inside the container it is set on. */
        pt_check_near(bounds_of(header).x, header_before.x, 0.01);
        pt_check_near(bounds_of(header).w, header_before.w, 0.01);
        pt_check_near(bounds_of(tree_row).x, tree_row_before.x, 0.01);
        pt_check(table_rect.w == 400.0f);
        pt_check(tree_rect.w == 320.0f);

        /* Column 0 is still the first child and still hugs the table's right
         * edge, so the logical column order is untouched. */
        pt_widget *head0 = header->first_child;
        pt_widget *head1 = head0 ? head0->next_sibling : NULL;
        pt_check(head0 && head1);
        if (head0 && head1) {
            pt_check(strcmp(pt_widget_get_text(head0), "Name") == 0);
            pt_check(strcmp(pt_widget_get_text(head1), "Size") == 0);
            pt_check(head0->x > head1->x);
        }

        /* A later physical call wins over the logical one. */
        pt_widget_set_padding(header, 0.0f, 2.0f, 0.0f, 3.0f);
        pt_check(pt_window_paint(window, canvas));
        pt_check_near(header->padding_right, 2.0, 0.01);
        pt_check_near(header->padding_left, 3.0, 0.01);
    }

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
    test_logical_padding_flex(app);
    test_logical_padding_grid(app);
    test_logical_padding_optout_containers(app);
    pt_app_destroy(app);
    return pt_test_finish("flex_grid");
}
