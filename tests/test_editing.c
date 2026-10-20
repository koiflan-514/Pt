/*
 * Table cell editing, selection, column reorder and row drag, plus tree rename.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct edit_log {
    int binds;
    int edits;
    int reorders;
    int drags;
    uint32_t edit_row;
    uint32_t edit_column;
    char edit_text[64];
    uint32_t reorder_from;
    uint32_t reorder_to;
    uint32_t drag_row;
} edit_log;

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    (void)table;
    edit_log *log = data;
    char label[32];
    snprintf(label, sizeof(label), "r%uc%u", row, column);
    pt_widget_set_text(cell, label);
    ++log->binds;
}

static void cell_edited(pt_widget *table, uint32_t row, uint32_t column, const char *text,
                        void *data) {
    (void)table;
    edit_log *log = data;
    ++log->edits;
    log->edit_row = row;
    log->edit_column = column;
    snprintf(log->edit_text, sizeof(log->edit_text), "%s", text ? text : "");
}

static void columns_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    (void)table;
    edit_log *log = data;
    ++log->reorders;
    log->reorder_from = from;
    log->reorder_to = to;
}

static void row_dragged(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    edit_log *log = data;
    ++log->drags;
    log->drag_row = row;
}

static void click_at(pt_window *window, float x, float y, uint32_t time) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, time);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, time);
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

static pt_widget *visible_row(pt_widget *table, uint32_t index) {
    for (pt_widget *row = table->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_table_row && row->visible && row->list_bound &&
            row->list_index == index) {
            return row;
        }
    }
    return NULL;
}

static pt_widget *row_cell(pt_widget *row, uint32_t column) {
    pt_widget *cell = row ? row->first_child : NULL;
    while (cell && column > 0) {
        cell = cell->next_sibling;
        --column;
    }
    return cell && cell->kind == pt_widget_table_cell ? cell : NULL;
}

static void test_table_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "editing", 640, 400);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget *table = pt_table_create(root);
    pt_check(table != NULL);
    pt_widget_set_size(table, 640.0f, 400.0f);
    pt_widget_set_align_self(table, pt_align_start);
    pt_table_set_column_count(table, 3);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Size");
    pt_table_set_column_title(table, 2, "Kind");
    pt_table_set_column_width(table, 0, 220.0f);
    pt_table_set_column_width(table, 1, 120.0f);
    pt_table_set_column_width(table, 2, 140.0f);
    pt_table_set_row_height(table, 30.0f);
    pt_table_set_header_height(table, 30.0f);
    pt_table_set_row_count(table, 6);

    edit_log log = {0};
    pt_table_set_on_bind_cell(table, bind_cell, &log);
    pt_table_set_on_cell_edited(table, cell_edited, &log);
    pt_table_set_on_columns_reordered(table, columns_reordered, &log);
    pt_table_set_on_row_drag(table, row_dragged, &log);
    pt_table_set_editable(table, true);
    pt_check(pt_table_get_editable(table));

    pt_window_show(window);
    pt_canvas *canvas = NULL;
    paint(window, &canvas);

    pt_widget *row = visible_row(table, 1);
    pt_widget *cell = row_cell(row, 0);
    pt_check(row != NULL && cell != NULL);
    if (!row || !cell) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    float cx = cell->x + 12.0f;
    float cy = cell->y + cell->height * 0.5f;

    /* Double-click starts in-place editing over the cell. */
    click_at(window, cx, cy, 100);
    click_at(window, cx, cy, 180);
    pt_check(pt_table_is_editing(table));
    pt_widget *editor = window->focused;
    pt_check(editor != NULL && editor->kind == pt_widget_entry);
    pt_check(editor && editor->editor_owner == table);
    if (editor) {
        pt_window_input_key(window, 0, 0, "Zed", true);
        pt_check(strcmp(pt_widget_get_text(editor), "Zed") == 0);
        pt_window_input_key(window, XKB_KEY_Return, 0, NULL, true);
    }
    pt_check(!pt_table_is_editing(table));
    pt_check(log.edits == 1);
    pt_check(log.edit_row == 1 && log.edit_column == 0);
    pt_check(strcmp(log.edit_text, "Zed") == 0);
    pt_check(strcmp(pt_widget_get_text(cell), "Zed") == 0);

    /* Escape discards the draft and keeps the committed text. */
    pt_check(pt_table_begin_edit(table, 2, 0));
    pt_check(pt_table_is_editing(table));
    pt_window_input_key(window, 0, 0, "discard", true);
    pt_window_input_key(window, XKB_KEY_Escape, 0, NULL, true);
    pt_check(!pt_table_is_editing(table));
    pt_check(log.edits == 1);
    pt_widget *second = row_cell(visible_row(table, 2), 0);
    pt_check(second && strcmp(pt_widget_get_text(second), "r2c0") == 0);

    /* Programmatic range selection. */
    pt_table_select_range(table, 1, 3);
    pt_check(pt_table_get_selection_count(table) == 3);
    pt_check(!pt_table_is_row_selected(table, 0));
    pt_check(pt_table_is_row_selected(table, 1));
    pt_check(pt_table_is_row_selected(table, 3));
    uint32_t rows[8] = {0};
    uint32_t selected = pt_table_get_selection(table, rows, 8);
    pt_check(selected == 3);
    pt_check(rows[0] == 1 && rows[1] == 2 && rows[2] == 3);
    pt_table_clear_selection(table);
    pt_check(pt_table_get_selection_count(table) == 0);

    /* Column reorder keeps widths and titles in sync and remaps sorting. */
    pt_table_set_sort(table, 0, pt_sort_ascending);
    pt_check(pt_table_move_column(table, 0, 2));
    pt_check(log.reorders == 1 && log.reorder_from == 0 && log.reorder_to == 2);
    pt_check(strcmp(pt_widget_get_text(table->extra ? ((pt_table_state *)table->extra)->header
                                                              ->first_child
                                                    : NULL),
                    "Size") == 0);
    pt_check_near(pt_table_get_column_width(table, 0), 120.0, 0.01);
    pt_check_near(pt_table_get_column_width(table, 2), 220.0, 0.01);
    uint32_t sort_column = 99;
    pt_check(pt_table_get_sort(table, &sort_column) == pt_sort_ascending);
    pt_check(sort_column == 2);
    pt_check(!pt_table_move_column(table, 0, 0));

    /* Dragging a row selects the swept range and reports the final row. */
    paint(window, &canvas);
    pt_widget *drag_row = visible_row(table, 0);
    pt_widget *target_row = visible_row(table, 2);
    pt_check(drag_row != NULL && target_row != NULL);
    if (drag_row && target_row) {
        float start_x = drag_row->x + 40.0f;
        float start_y = drag_row->y + drag_row->height * 0.5f;
        float end_y = target_row->y + target_row->height * 0.5f;
        pt_window_input_pointer_enter(window, start_x, start_y);
        pt_window_input_pointer_button(window, 0x110u, true, start_x, start_y, 3, 300);
        pt_window_input_pointer_motion(window, start_x, end_y, 310);
        pt_window_input_pointer_button(window, 0x110u, false, start_x, end_y, 4, 320);
        pt_check(log.drags == 1);
        pt_check(log.drag_row == 2);
        pt_check(pt_table_get_selection_count(table) == 3);
        pt_check(pt_table_is_row_selected(table, 0));
        pt_check(pt_table_is_row_selected(table, 2));
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

typedef struct rename_log {
    int renames;
    char text[64];
} rename_log;

static void tree_renamed(pt_widget *tree, pt_tree_node *node, const char *text, void *data) {
    (void)tree;
    (void)node;
    rename_log *log = data;
    ++log->renames;
    snprintf(log->text, sizeof(log->text), "%s", text ? text : "");
}

static void test_tree_rename(pt_app *app) {
    pt_window *window = pt_window_create(app, "rename", 360, 260);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget *tree = pt_tree_create(root);
    pt_check(tree != NULL);
    pt_widget_set_size(tree, 360.0f, 260.0f);
    pt_widget_set_align_self(tree, pt_align_start);
    pt_tree_node *alpha = pt_tree_add_node(tree, NULL, "alpha");
    pt_tree_add_node(tree, NULL, "beta");
    pt_tree_node *child = pt_tree_add_node(tree, alpha, "child");
    pt_check(alpha && child);

    rename_log log = {0};
    pt_tree_set_on_rename(tree, tree_renamed, &log);
    pt_tree_set_renamable(tree, true);
    pt_check(pt_tree_get_renamable(tree));

    pt_window_show(window);
    pt_canvas *canvas = NULL;
    paint(window, &canvas);

    pt_tree_set_selected(tree, alpha);
    pt_check(pt_widget_focus(tree));
    pt_window_input_key(window, XKB_KEY_F2, 0, NULL, true);
    pt_check(pt_tree_is_renaming(tree));
    pt_widget *editor = window->focused;
    pt_check(editor != NULL && editor->kind == pt_widget_entry);
    pt_check(editor && editor->editor_owner == tree);
    paint(window, &canvas);
    if (editor) {
        pt_check(editor->visible);
        pt_check(editor->width > 40.0f);
        pt_check(editor->x > tree->x);
        pt_window_input_key(window, 0, 0, "renamed", true);
        pt_window_input_key(window, XKB_KEY_Return, 0, NULL, true);
    }
    pt_check(!pt_tree_is_renaming(tree));
    pt_check(log.renames == 1);
    pt_check(strcmp(log.text, "renamed") == 0);
    pt_check(strcmp(pt_tree_node_get_text(alpha), "renamed") == 0);

    /* Escape restores the previous name. */
    pt_check(pt_tree_begin_rename(tree, alpha));
    pt_window_input_key(window, 0, 0, "draft", true);
    pt_window_input_key(window, XKB_KEY_Escape, 0, NULL, true);
    pt_check(!pt_tree_is_renaming(tree));
    pt_check(log.renames == 1);
    pt_check(strcmp(pt_tree_node_get_text(alpha), "renamed") == 0);

    /* Double-clicking a row begins rename; a non-renamable tree refuses. */
    pt_window_input_pointer_leave(window);
    pt_tree_set_renamable(tree, false);
    pt_check(!pt_tree_begin_rename(tree, alpha));
    pt_tree_set_renamable(tree, true);
    pt_tree_reveal(tree, alpha);
    paint(window, &canvas);
    pt_tree_node *visible = pt_tree_get_visible_node(tree, 0);
    pt_check(visible != NULL);
    pt_widget *tree_row = NULL;
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_tree_row && row->visible && row->tree_node == visible) {
            tree_row = row;
            break;
        }
    }
    pt_check(tree_row != NULL);
    if (tree_row) {
        float tx = tree_row->x + 60.0f;
        float ty = tree_row->y + tree_row->height * 0.5f;
        pt_window_input_pointer_leave(window);
        click_at(window, tx, ty, 400);
        click_at(window, tx, ty, 460);
        pt_check(pt_tree_is_renaming(tree));
        pt_tree_cancel_rename(tree);
        pt_check(!pt_tree_is_renaming(tree));
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.editing");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("editing");
    }
    test_table_editing(app);
    test_tree_rename(app);
    pt_app_destroy(app);
    return pt_test_finish("editing");
}
