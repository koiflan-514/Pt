/*
 * Multi-selection parity across the three row views: the set helpers, modifier
 * clicks, keyboard extension and the shared rubber band.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct sel_log {
    int selects;
    int activates;
    uint32_t last_index;
    pt_tree_node *last_node;
} sel_log;

static void note_index(pt_widget *view, uint32_t index, void *data) {
    sel_log *log = data;
    (void)view;
    ++log->selects;
    log->last_index = index;
}

static void note_node(pt_widget *tree, pt_tree_node *node, void *data) {
    sel_log *log = data;
    (void)tree;
    ++log->selects;
    log->last_node = node;
}

static void note_activate_index(pt_widget *view, uint32_t index, void *data) {
    (void)view;
    (void)index;
    ++((sel_log *)data)->activates;
}

static void note_activate_node(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    (void)node;
    ++((sel_log *)data)->activates;
}

static void bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    char label[32];
    snprintf(label, sizeof(label), "Item %u", index);
    pt_widget_set_text(row, label);
    (void)list;
    (void)data;
}

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    char label[32];
    snprintf(label, sizeof(label), "r%uc%u", row, column);
    pt_widget_set_text(cell, label);
    (void)table;
    (void)data;
}

static pt_widget *row_for(pt_widget *view, uint32_t index, pt_widget_kind kind) {
    for (pt_widget *row = view->first_child; row; row = row->next_sibling) {
        if (row->kind == kind && row->list_bound && row->list_index == index) {
            return row;
        }
    }
    return NULL;
}

static pt_rect bounds_of(const pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &width, &height);
    return (pt_rect){x, y, width, height};
}

static void paint(pt_window *window, pt_canvas **canvas) {
    if (!*canvas) {
        *canvas = pt_canvas_create(window->width, window->height, 1.0f);
    }
    pt_check(*canvas != NULL);
    if (*canvas) {
        pt_check(pt_window_paint(window, *canvas));
    }
}

static void modifiers(pt_window *window, uint32_t mods) {
    pt_window_set_input_modifiers(window, mods);
}

static void click(pt_window *window, pt_widget *row) {
    pt_rect area = bounds_of(row);
    float x = area.x + area.w * 0.5f;
    float y = area.y + area.h * 0.5f;
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 100);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 110);
}

/* Press inside the first row, release inside the last one, far enough apart that
 * the rubber band engages instead of reporting a click. */
static void drag_band(pt_window *window, pt_widget *from, pt_widget *to) {
    pt_rect a = bounds_of(from);
    pt_rect b = bounds_of(to);
    float x0 = a.x + a.w * 0.5f;
    float y0 = a.y + a.h * 0.5f;
    float x1 = b.x + b.w * 0.5f;
    float y1 = b.y + b.h * 0.5f;
    pt_window_input_pointer_enter(window, x0, y0);
    pt_window_input_pointer_button(window, 0x110u, true, x0, y0, 1, 100);
    pt_window_input_pointer_motion(window, x1, y1, 110);
    pt_window_input_pointer_button(window, 0x110u, false, x1, y1, 2, 120);
}

/* A point in the viewport below the last visible row, inside the content box. */
static void empty_point(pt_widget *view, pt_widget_kind row_kind, uint32_t last_index,
                        float *x, float *y) {
    pt_rect area = bounds_of(view);
    pt_rect last = bounds_of(row_for(view, last_index, row_kind));
    *x = area.x + 24.0f;
    *y = last.y + last.h + 10.0f;
    float bottom = area.y + area.h - view->padding_bottom - 4.0f;
    if (*y > bottom) {
        *y = bottom;
    }
    pt_check(*y > last.y + last.h);
    pt_check(*x >= area.x + view->padding_left);
    pt_check(*x <= area.x + area.w - view->padding_right);
    pt_check(*y <= area.y + area.h - view->padding_bottom);
}

static void click_empty(pt_window *window, pt_widget *view, pt_widget_kind row_kind,
                        uint32_t last_index) {
    float x = 0.0f;
    float y = 0.0f;
    empty_point(view, row_kind, last_index, &x, &y);
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 100);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 110);
}

static void drag_band_from_empty(pt_window *window, pt_widget *view,
                                 pt_widget_kind row_kind, uint32_t last_index,
                                 pt_widget *to, bool additive) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    empty_point(view, row_kind, last_index, &x0, &y0);
    pt_rect b = bounds_of(to);
    float x1 = b.x + b.w * 0.5f;
    float y1 = b.y + b.h * 0.5f;
    if (additive) {
        modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    }
    pt_window_input_pointer_enter(window, x0, y0);
    pt_window_input_pointer_button(window, 0x110u, true, x0, y0, 1, 100);
    pt_window_input_pointer_motion(window, x1, y1, 110);
    pt_window_input_pointer_button(window, 0x110u, false, x1, y1, 2, 120);
    if (additive) {
        modifiers(window, 0);
    }
}

static void key(pt_window *window, uint32_t keysym, uint32_t modifiers) {
    pt_window_input_key(window, keysym, modifiers, NULL, true);
}

/* The three views agree gesture for gesture, so one script drives the list and
 * the table and the same expectations are checked each time. The anchor is the
 * row the last click landed on, which is why the shift clicks below range from
 * row 3 rather than from the highlighted row the caret happens to name. */
static void expect_view_parity(pt_widget *view, sel_log *log,
                               uint32_t (*count)(const pt_widget *),
                               bool (*is_selected)(const pt_widget *, uint32_t),
                               void (*clear)(pt_widget *), pt_widget_kind row_kind,
                               pt_window *window, pt_canvas **canvas) {
    paint(window, canvas);
    clear(view);
    log->selects = 0;

    click(window, row_for(view, 3, row_kind));
    pt_check(count(view) == 1);
    pt_check(is_selected(view, 3));
    pt_check(log->selects == 1);

    modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    click(window, row_for(view, 5, row_kind));
    modifiers(window, 0);
    pt_check(count(view) == 2);
    pt_check(is_selected(view, 3) && is_selected(view, 5));
    pt_check(log->selects == 2);

    modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    click(window, row_for(view, 3, row_kind));
    modifiers(window, 0);
    pt_check(count(view) == 1);
    pt_check(!is_selected(view, 3));
    pt_check(is_selected(view, 5));
    /* Un-highlighting a row leaves the primary row where it was, so nothing is
     * reported: the gesture moved the set, not the caret. */
    pt_check(log->selects == 2);

    modifiers(window, (uint32_t)pt_key_modifier_shift);
    click(window, row_for(view, 0, row_kind));
    modifiers(window, 0);
    pt_check(count(view) == 4);
    pt_check(is_selected(view, 0) && is_selected(view, 1) && is_selected(view, 2) &&
             is_selected(view, 3));
    pt_check(log->selects == 3);
    pt_check(log->last_index == 0);

    modifiers(window, (uint32_t)pt_key_modifier_shift);
    click(window, row_for(view, 5, row_kind));
    modifiers(window, 0);
    pt_check(count(view) == 3);
    pt_check(is_selected(view, 3) && is_selected(view, 4) && is_selected(view, 5));
    pt_check(!is_selected(view, 0));

    clear(view);
    drag_band(window, row_for(view, 2, row_kind), row_for(view, 4, row_kind));
    pt_check(count(view) == 3);
    pt_check(is_selected(view, 2) && is_selected(view, 3) && is_selected(view, 4));
    pt_check(!is_selected(view, 5));

    modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    drag_band(window, row_for(view, 6, row_kind), row_for(view, 7, row_kind));
    modifiers(window, 0);
    pt_check(count(view) == 5);
    pt_check(is_selected(view, 2) && is_selected(view, 6) && is_selected(view, 7));

    /* Six gestures move the primary row; the two that do not stay silent. */
    pt_check(log->selects == 6);

    paint(window, canvas);
    clear(view);
    pt_check(count(view) == 0);
}

/* Shift+arrow sweeps from a fixed anchor, an unmodified arrow collapses the set
 * to the row it lands on, and Ctrl+A selects the whole model. */
static void expect_view_keyboard(pt_widget *view, sel_log *log,
                                 uint32_t (*count)(const pt_widget *),
                                 bool (*is_selected)(const pt_widget *, uint32_t),
                                 uint32_t (*get_selected)(const pt_widget *),
                                 void (*clear)(pt_widget *), void (*set_selected)(
                                     pt_widget *, uint32_t),
                                 uint32_t row_count, pt_window *window) {
    pt_widget_focus(view);
    clear(view);
    set_selected(view, 0);
    log->selects = 0;

    key(window, XKB_KEY_Down, (uint32_t)pt_key_modifier_shift);
    pt_check(count(view) == 2);
    pt_check(is_selected(view, 0) && is_selected(view, 1));
    pt_check(get_selected(view) == 1);
    key(window, XKB_KEY_Down, (uint32_t)pt_key_modifier_shift);
    pt_check(count(view) == 3);
    pt_check(is_selected(view, 2));
    key(window, XKB_KEY_Up, (uint32_t)pt_key_modifier_shift);
    pt_check(count(view) == 2);
    pt_check(is_selected(view, 0) && is_selected(view, 1));
    pt_check(!is_selected(view, 2));
    pt_check(log->selects == 3);

    key(window, XKB_KEY_Up, 0);
    pt_check(count(view) == 1);
    pt_check(is_selected(view, 0));
    key(window, XKB_KEY_a, (uint32_t)pt_key_modifier_ctrl);
    pt_check(count(view) == row_count);
    pt_check(is_selected(view, row_count - 1u));
    /* Ctrl+A keeps the primary row it already had, so it is reported through the
     * accessibility event only, not through on_select. */
    pt_check(log->selects == 4);
    modifiers(window, 0);
}

static void test_list_model(pt_window *window, pt_canvas **canvas) {
    sel_log log = {0};
    pt_widget *root = pt_window_root(window);
    pt_widget *list = pt_list_view_create(root);
    pt_widget_set_size(list, 300.0f, 240.0f);
    pt_widget_set_align_self(list, pt_align_start);
    pt_list_view_set_row_height(list, 30.0f);
    pt_list_view_set_row_count(list, 8);
    pt_list_view_set_on_bind_row(list, bind_row, NULL);
    pt_list_view_set_on_select(list, note_index, &log);
    pt_list_view_set_on_activate(list, note_activate_index, &log);
    paint(window, canvas);

    uint32_t rows[16] = {0};

    /* A single row is a selection of one, before any set exists. */
    pt_list_view_set_selected(list, 2);
    pt_check(pt_list_view_get_selection_count(list) == 1);
    pt_check(pt_list_view_is_row_selected(list, 2));
    pt_check(!pt_list_view_is_row_selected(list, 3));
    pt_check(log.selects == 0);

    pt_list_view_select_range(list, 1, 3);
    pt_check(pt_list_view_get_selection_count(list) == 3);
    pt_check(pt_list_view_get_selected(list) == 1);
    pt_check(pt_list_view_get_selection(list, rows, 16) == 3);
    pt_check(rows[0] == 1 && rows[1] == 2 && rows[2] == 3);

    pt_list_view_select_toggle(list, 5);
    pt_check(pt_list_view_get_selection_count(list) == 4);
    pt_check(pt_list_view_is_row_selected(list, 5));
    pt_check(pt_list_view_get_selected(list) == 5);

    pt_list_view_select_toggle(list, 2);
    pt_check(pt_list_view_get_selection_count(list) == 3);
    pt_check(!pt_list_view_is_row_selected(list, 2));

    pt_list_view_select_toggle(list, 5);
    pt_check(pt_list_view_get_selection_count(list) == 2);
    /* Dropping the primary row moves it to the lowest row still selected. */
    pt_check(pt_list_view_get_selected(list) == 1);

    pt_list_view_clear_selection(list);
    pt_check(pt_list_view_get_selection_count(list) == 0);
    pt_check(pt_list_view_get_selected(list) == pt_list_index_none);
    pt_check(pt_list_view_get_selection(list, rows, 16) == 0);

    /* Out-of-range ends clamp instead of running past the model. */
    pt_list_view_select_range(list, 6, 20);
    pt_check(pt_list_view_get_selection_count(list) == 2);
    pt_check(pt_list_view_is_row_selected(list, 6));
    pt_check(pt_list_view_is_row_selected(list, 7));

    /* Rows that disappear cannot stay selected. */
    pt_list_view_set_row_count(list, 5);
    pt_check(pt_list_view_get_selection_count(list) == 0);
    pt_check(pt_list_view_get_selected(list) == pt_list_index_none);

    /* Selecting a row is a choice, not an addition. */
    pt_list_view_select_range(list, 0, 3);
    pt_list_view_set_selected(list, 4);
    pt_check(pt_list_view_get_selection_count(list) == 1);
    pt_check(pt_list_view_is_row_selected(list, 4));
    pt_check(!pt_list_view_is_row_selected(list, 2));

    /* Shrinking the model past the anchor keeps the next range inside. */
    pt_list_view_select_range(list, 1, 4);
    pt_list_view_set_row_count(list, 3);
    pt_check(pt_list_view_get_selection_count(list) <= 2);
    pt_list_view_select_range(list, 1, 2);
    pt_list_view_set_row_count(list, 1);
    pt_check(pt_list_view_get_selected(list) == pt_list_index_none);
    pt_check(!pt_list_view_is_row_selected(list, 1));

    pt_list_view_set_row_count(list, 8);
    expect_view_parity(list, &log, pt_list_view_get_selection_count,
                       pt_list_view_is_row_selected, pt_list_view_clear_selection,
                       pt_widget_list_row, window, canvas);
    expect_view_keyboard(list, &log, pt_list_view_get_selection_count,
                         pt_list_view_is_row_selected, pt_list_view_get_selected,
                         pt_list_view_clear_selection, pt_list_view_set_selected, 8,
                         window);

    /* The band may begin below the last row. A plain press clears the old set,
     * and Ctrl keeps it while the band adds the rows it crosses. */
    pt_list_view_set_row_count(list, 5);
    paint(window, canvas);
    log.activates = 0;
    pt_list_view_set_selected(list, 4);
    click_empty(window, list, pt_widget_list_row, 4);
    pt_check(pt_list_view_get_selection_count(list) == 0);
    drag_band_from_empty(window, list, pt_widget_list_row, 4,
                         row_for(list, 2, pt_widget_list_row), false);
    pt_check(pt_list_view_get_selection_count(list) == 3);
    pt_check(pt_list_view_is_row_selected(list, 2));
    pt_check(pt_list_view_is_row_selected(list, 3));
    pt_check(pt_list_view_is_row_selected(list, 4));
    pt_check(!pt_list_view_is_row_selected(list, 1));

    pt_list_view_set_selected(list, 0);
    drag_band_from_empty(window, list, pt_widget_list_row, 4,
                         row_for(list, 2, pt_widget_list_row), true);
    pt_check(pt_list_view_get_selection_count(list) == 4);
    pt_check(pt_list_view_is_row_selected(list, 0));
    pt_check(pt_list_view_is_row_selected(list, 2));
    pt_check(!pt_list_view_is_row_selected(list, 1));

    modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    click_empty(window, list, pt_widget_list_row, 4);
    modifiers(window, 0);
    pt_check(pt_list_view_get_selection_count(list) == 4);
    pt_check(log.activates == 0);
    pt_list_view_set_row_count(list, 8);

    pt_widget_destroy(list);
}

static void test_table_model(pt_window *window, pt_canvas **canvas) {
    sel_log log = {0};
    pt_widget *root = pt_window_root(window);
    pt_widget *table = pt_table_create(root);
    pt_widget_set_size(table, 420.0f, 240.0f);
    pt_widget_set_align_self(table, pt_align_start);
    pt_table_set_column_count(table, 2);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Size");
    pt_table_set_column_width(table, 0, 200.0f);
    pt_table_set_column_width(table, 1, 120.0f);
    pt_table_set_row_height(table, 30.0f);
    pt_table_set_header_height(table, 30.0f);
    pt_table_set_row_count(table, 8);
    pt_table_set_on_bind_cell(table, bind_cell, NULL);
    pt_table_set_on_select(table, note_index, &log);
    pt_table_set_on_activate(table, note_activate_index, &log);
    paint(window, canvas);

    pt_table_set_selected(table, 2);
    pt_check(pt_table_get_selection_count(table) == 1);
    pt_check(pt_table_is_row_selected(table, 2));
    pt_check(!pt_table_is_row_selected(table, 3));
    pt_check(log.selects == 0);

    pt_table_select_range(table, 3, 1);
    uint32_t rows[16] = {0};
    pt_check(pt_table_get_selection_count(table) == 3);
    pt_check(pt_table_get_selected(table) == 1);
    pt_check(pt_table_get_selection(table, rows, 16) == 3);
    pt_check(rows[0] == 1 && rows[1] == 2 && rows[2] == 3);

    pt_table_select_toggle(table, 5);
    pt_check(pt_table_get_selection_count(table) == 4);
    pt_check(pt_table_get_selected(table) == 5);

    pt_table_select_toggle(table, 5);
    pt_check(pt_table_get_selection_count(table) == 3);
    pt_check(pt_table_get_selected(table) == 1);

    pt_table_clear_selection(table);
    pt_check(pt_table_get_selection_count(table) == 0);
    pt_check(pt_table_get_selected(table) == pt_list_index_none);
    pt_check(pt_table_get_selection(table, rows, 16) == 0);

    /* Out-of-range and a shrinking model behave the same as in the list. */
    pt_table_select_range(table, 6, 30);
    pt_check(pt_table_get_selection_count(table) == 2);
    pt_table_set_row_count(table, 5);
    pt_check(pt_table_get_selection_count(table) == 0);
    pt_table_set_row_count(table, 8);

    expect_view_parity(table, &log, pt_table_get_selection_count,
                       pt_table_is_row_selected, pt_table_clear_selection,
                       pt_widget_table_row, window, canvas);
    expect_view_keyboard(table, &log, pt_table_get_selection_count,
                         pt_table_is_row_selected, pt_table_get_selected,
                         pt_table_clear_selection, pt_table_set_selected, 8, window);

    pt_table_set_row_count(table, 5);
    paint(window, canvas);
    log.activates = 0;
    pt_table_set_selected(table, 4);
    click_empty(window, table, pt_widget_table_row, 4);
    pt_check(pt_table_get_selection_count(table) == 0);
    drag_band_from_empty(window, table, pt_widget_table_row, 4,
                         row_for(table, 2, pt_widget_table_row), false);
    pt_check(pt_table_get_selection_count(table) == 3);
    pt_check(pt_table_is_row_selected(table, 2));
    pt_check(pt_table_is_row_selected(table, 3));
    pt_check(pt_table_is_row_selected(table, 4));
    pt_check(!pt_table_is_row_selected(table, 1));

    pt_table_set_selected(table, 0);
    drag_band_from_empty(window, table, pt_widget_table_row, 4,
                         row_for(table, 2, pt_widget_table_row), true);
    pt_check(pt_table_get_selection_count(table) == 4);
    pt_check(pt_table_is_row_selected(table, 0));
    pt_check(pt_table_is_row_selected(table, 2));
    pt_check(!pt_table_is_row_selected(table, 1));

    modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    click_empty(window, table, pt_widget_table_row, 4);
    modifiers(window, 0);
    pt_check(pt_table_get_selection_count(table) == 4);
    pt_check(log.activates == 0);
    pt_table_set_row_count(table, 8);

    pt_widget_destroy(table);
}

static void test_tree_model(pt_window *window, pt_canvas **canvas) {
    sel_log log = {0};
    pt_widget *root = pt_window_root(window);
    pt_widget *tree = pt_tree_create(root);
    pt_widget_set_size(tree, 300.0f, 300.0f);
    pt_widget_set_align_self(tree, pt_align_start);
    pt_tree_set_row_height(tree, 30.0f);
    pt_tree_set_on_select(tree, note_node, &log);
    pt_tree_set_on_activate(tree, note_activate_node, &log);
    pt_tree_node *folder_a = pt_tree_add_node(tree, NULL, "A");
    pt_tree_node *folder_b = pt_tree_add_node(tree, NULL, "B");
    pt_tree_node *leaves[6] = {0};
    for (uint32_t i = 0; i < 4; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "A%u", i + 1);
        leaves[i] = pt_tree_add_node(tree, folder_a, label);
    }
    for (uint32_t i = 0; i < 2; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "B%u", i + 1);
        leaves[4 + i] = pt_tree_add_node(tree, folder_b, label);
    }
    pt_tree_node_set_expanded(folder_a, true);
    pt_tree_node_set_expanded(folder_b, true);
    /* A, A1..A4, B, B1, B2. */
    pt_check(pt_tree_get_visible_count(tree) == 8u);
    paint(window, canvas);

    pt_tree_select(tree, leaves[0], false);
    pt_check(pt_tree_get_selection_count(tree) == 1);
    pt_check(pt_tree_is_selected(tree, leaves[0]));
    pt_check(!pt_tree_is_selected(tree, leaves[1]));
    pt_check(log.selects == 0);

    pt_tree_node *nodes[16] = {0};
    pt_tree_select_range(tree, 1, 3);
    pt_check(pt_tree_get_selection_count(tree) == 3);
    pt_check(pt_tree_get_selection(tree, nodes, 16) == 3);
    pt_check(nodes[0] == leaves[0] && nodes[1] == leaves[1] && nodes[2] == leaves[2]);
    pt_check(pt_tree_get_selected(tree) == leaves[0]);

    pt_tree_select_toggle(tree, leaves[4]);
    pt_check(pt_tree_get_selection_count(tree) == 4);
    pt_check(pt_tree_is_selected(tree, leaves[4]));
    pt_check(pt_tree_get_selected(tree) == leaves[4]);

    pt_tree_select_toggle(tree, leaves[0]);
    pt_check(pt_tree_get_selection_count(tree) == 3);
    pt_check(!pt_tree_is_selected(tree, leaves[0]));
    /* A1 was not the primary node, so dropping it leaves the caret on A4. */
    pt_check(pt_tree_get_selected(tree) == leaves[4]);

    pt_check(pt_tree_get_selection_count(tree) == 3);
    pt_check(pt_tree_get_selection(tree, nodes, 16) == 3);
    pt_check(nodes[0] == leaves[1] && nodes[1] == leaves[2] && nodes[2] == leaves[4]);

    /* A collapsed branch keeps what it held. */
    pt_tree_node_set_expanded(folder_a, false);
    paint(window, canvas);
    pt_check(pt_tree_get_visible_count(tree) == 4u);
    pt_check(pt_tree_get_selection_count(tree) == 3);
    pt_check(pt_tree_is_selected(tree, leaves[2]));
    pt_check(pt_tree_get_selection(tree, nodes, 16) == 3);

    pt_tree_clear_selection(tree);
    pt_check(pt_tree_get_selection_count(tree) == 0);
    pt_check(pt_tree_get_selected(tree) == NULL);
    pt_check(pt_tree_get_selection(tree, nodes, 16) == 0);

    /* Pointer gestures over the visible rows. */
    pt_tree_node_set_expanded(folder_a, true);
    paint(window, canvas);
    pt_widget *row_a1 = NULL;
    pt_widget *row_a3 = NULL;
    pt_widget *row_a4 = NULL;
    pt_widget *row_b1 = NULL;
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind != pt_widget_tree_row || !row->list_bound) {
            continue;
        }
        if (row->tree_node == leaves[0]) {
            row_a1 = row;
        } else if (row->tree_node == leaves[2]) {
            row_a3 = row;
        } else if (row->tree_node == leaves[3]) {
            row_a4 = row;
        } else if (row->tree_node == leaves[4]) {
            row_b1 = row;
        }
    }
    pt_check(row_a1 && row_a3 && row_a4 && row_b1);
    if (row_a1 && row_a3 && row_a4 && row_b1) {
        log.selects = 0;
        click(window, row_a1);
        pt_check(pt_tree_get_selection_count(tree) == 1);
        pt_check(pt_tree_get_selected(tree) == leaves[0]);
        pt_check(log.selects == 1);

        modifiers(window, (uint32_t)pt_key_modifier_ctrl);
        click(window, row_a3);
        modifiers(window, 0);
        pt_check(pt_tree_get_selection_count(tree) == 2);
        pt_check(pt_tree_is_selected(tree, leaves[0]) &&
                 pt_tree_is_selected(tree, leaves[2]));

        modifiers(window, (uint32_t)pt_key_modifier_shift);
        click(window, row_a4);
        modifiers(window, 0);
        pt_check(pt_tree_get_selection_count(tree) == 2);
        pt_check(pt_tree_is_selected(tree, leaves[2]) &&
                 pt_tree_is_selected(tree, leaves[3]));
        pt_check(!pt_tree_is_selected(tree, leaves[0]));

        drag_band(window, row_a1, row_a4);
        pt_check(pt_tree_get_selection_count(tree) == 4);
        pt_check(pt_tree_get_selected(tree) == leaves[0]);

        modifiers(window, (uint32_t)pt_key_modifier_ctrl);
        drag_band(window, row_b1, row_b1);
        modifiers(window, 0);
        click(window, row_b1);
        pt_check(pt_tree_get_selection_count(tree) == 1);
        pt_check(pt_tree_get_selected(tree) == leaves[4]);
    }

    /* Keyboard, over the same eight visible rows. */
    pt_widget_focus(tree);
    click(window, row_a1);
    log.selects = 0;
    key(window, XKB_KEY_Down, (uint32_t)pt_key_modifier_shift);
    pt_check(pt_tree_get_selection_count(tree) == 2);
    pt_check(pt_tree_is_selected(tree, leaves[0]) && pt_tree_is_selected(tree, leaves[1]));
    key(window, XKB_KEY_Down, (uint32_t)pt_key_modifier_shift);
    pt_check(pt_tree_get_selection_count(tree) == 3);
    key(window, XKB_KEY_Up, (uint32_t)pt_key_modifier_shift);
    pt_check(pt_tree_get_selection_count(tree) == 2);
    key(window, XKB_KEY_Up, 0);
    pt_check(pt_tree_get_selection_count(tree) == 1);
    pt_check(pt_tree_get_selected(tree) == leaves[0]);
    /* The unmodified arrow collapses the set; Ctrl still only rides along with
     * Shift, so Ctrl+Shift+Down extends from the same anchor. */
    key(window, XKB_KEY_Down, (uint32_t)pt_key_modifier_ctrl |
                              (uint32_t)pt_key_modifier_shift);
    pt_check(pt_tree_get_selection_count(tree) == 2);
    key(window, XKB_KEY_a, (uint32_t)pt_key_modifier_ctrl);
    pt_check(pt_tree_get_selection_count(tree) == 8);
    pt_check(pt_tree_is_selected(tree, leaves[4]));
    modifiers(window, 0);

    /* A band from below the last visible row crosses rows 2..7. */
    log.activates = 0;
    pt_tree_select(tree, leaves[0], false);
    click_empty(window, tree, pt_widget_tree_row, 7);
    pt_check(pt_tree_get_selection_count(tree) == 0);
    drag_band_from_empty(window, tree, pt_widget_tree_row, 7,
                         row_for(tree, 2, pt_widget_tree_row), false);
    pt_check(pt_tree_get_selection_count(tree) == 6);
    pt_check(pt_tree_is_selected(tree, leaves[1]));
    pt_check(!pt_tree_is_selected(tree, leaves[0]));

    pt_tree_select(tree, leaves[0], false);
    drag_band_from_empty(window, tree, pt_widget_tree_row, 7,
                         row_for(tree, 2, pt_widget_tree_row), true);
    pt_check(pt_tree_get_selection_count(tree) == 7);
    pt_check(pt_tree_is_selected(tree, leaves[0]));
    pt_check(pt_tree_is_selected(tree, leaves[1]));

    modifiers(window, (uint32_t)pt_key_modifier_ctrl);
    click_empty(window, tree, pt_widget_tree_row, 7);
    modifiers(window, 0);
    pt_check(pt_tree_get_selection_count(tree) == 7);
    pt_check(log.activates == 0);

    pt_widget_destroy(tree);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.selection");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("selection");
    }

    pt_window *window = pt_window_create(app, "selection", 460, 320);
    pt_check(window != NULL);
    if (window) {
        pt_widget_set_padding(pt_window_root(window), 0.0f, 0.0f, 0.0f, 0.0f);
        pt_window_show(window);
        pt_canvas *canvas = NULL;
        test_list_model(window, &canvas);
        test_table_model(window, &canvas);
        test_tree_model(window, &canvas);
        pt_canvas_destroy(canvas);
        pt_window_free(window);
    }

    pt_app_destroy(app);
    return pt_test_finish("selection");
}
