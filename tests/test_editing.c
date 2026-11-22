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

#define HE_BET "\xD7\x90\xD7\x91\xD7\x92"
#define HE_GIMEL_DALET_HE "\xD7\x93\xD7\x94\xD7\x95"

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

static float entry_caret_visual_x(pt_widget *entry) {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    pt_widget_ime_rect(entry, &x, &y, &w, &h);
    pt_unused(y);
    pt_unused(w);
    pt_unused(h);
    /* The IME rectangle is window-relative; subtract the paragraph anchor so
     * the result stays the caret's visual offset from the line's left edge.
     * RTL lines are right-anchored (W5b), so their line origin is mirrored. */
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    float origin = -entry->scroll_x;
    pt_bidi_base base = pt_widget_text_base(entry, text, len);
    if (font && base == pt_bidi_base_rtl) {
        float area_w = pt_max_float(0.0f, entry->width - entry->padding_left -
                                              entry->padding_right);
        origin = area_w - pt_font_measure_bidi(font, text, len, base) / scale +
                 entry->scroll_x;
    }
    return (float)x - entry->x - entry->padding_left - origin;
}

static float entry_expected_caret_x(pt_widget *entry) {
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    pt_bidi_base base = pt_widget_text_base(entry, text, len);
    pt_text_visual_map *map = NULL;
    float x = 0.0f;
    if (font && pt_text_visual_map_build_bidi(font, text, len, base, &map)) {
        x = pt_text_visual_map_x_for_position(map, entry->cursor,
                                              (pt_text_bias)entry->caret_affinity) /
            (ctx.scale > 0.0f ? ctx.scale : 1.0f);
    }
    pt_text_visual_map_free(map);
    return x;
}

static void expect_entry_caret(pt_window *window, pt_widget *entry, size_t cursor,
                               const char *label) {
    pt_unused(window);
    if (entry->cursor != cursor) {
        fprintf(stderr, "caret %s: cursor %zu expected %zu\n", label, entry->cursor, cursor);
    }
    pt_check(entry->cursor == cursor);
    pt_check_near(entry_caret_visual_x(entry), entry_expected_caret_x(entry), 1.0f);
}

static float text_area_expected_caret_x(pt_widget *area) {
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    if (!pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout)) {
        return 0.0f;
    }
    int line = pt_text_layout_line_for_position_bias(&layout, layout.caret,
                                                     (pt_text_bias)area->caret_affinity);
    float x = pt_text_layout_x_for_position_bias(&layout, line, layout.caret,
                                                 (pt_text_bias)area->caret_affinity);
    pt_text_layout_free(&layout);
    return x;
}

static void test_bidi_entry_editing(pt_app *app) {
    pt_window *window = pt_window_create(app, "bidi-editing", 640, 240);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(root, 8.0f);
    pt_widget *entry = pt_entry_create(root, "");
    pt_check(entry != NULL);
    const char *mixed = "abc " HE_BET " 123";
    pt_widget_set_text(entry, mixed);
    pt_widget_set_size(entry, 600.0f, 32.0f);
    pt_widget_set_align_self(entry, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(640, 240, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(entry));
    window->caret_visible = true;

    /* Visual Right: Latin prefix first, then the level-2 digits in the middle,
     * then the RTL run backwards (logical offsets fall while x grows), and the
     * caret stops at both affinities of the shared byte-4 boundary. */
    pt_entry_set_selection(entry, 0, 0);
    expect_entry_caret(window, entry, 0, "home");
    const size_t right_path[] = {1, 2, 3, 4, 12, 13, 14, 10, 8, 6, 4};
    for (size_t i = 0; i < sizeof(right_path) / sizeof(right_path[0]); ++i) {
        pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
        expect_entry_caret(window, entry, right_path[i], "right");
    }
    /* End: visual right edge for an LTR paragraph is logical byte 4 here. */
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    expect_entry_caret(window, entry, 4, "end");
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    expect_entry_caret(window, entry, 0, "home2");

    /* Visual Left walks the same line in reverse, including the trailing
     * affinity of the run boundary. */
    const size_t left_path[] = {6, 8, 10, 11, 13, 12, 11, 3, 2, 1, 0};
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    for (size_t i = 0; i < sizeof(left_path) / sizeof(left_path[0]); ++i) {
        pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
        expect_entry_caret(window, entry, left_path[i], "left");
    }

    /* Shift keeps a logical anchor while the caret moves visually. */
    pt_entry_set_selection(entry, 0, 0);
    pt_window_input_key(window, XKB_KEY_Right, 1u, NULL, true);
    pt_window_input_key(window, XKB_KEY_Right, 1u, NULL, true);
    pt_check(entry->selection_anchor == 0 && entry->cursor == 2);
    pt_window_input_key(window, XKB_KEY_Left, 1u, NULL, true);
    pt_check(entry->selection_anchor == 0 && entry->cursor == 1);
    uint32_t sel_start = 99;
    uint32_t sel_end = 99;
    pt_check(pt_entry_get_selection(entry, &sel_start, &sel_end));
    pt_check(sel_start == 0 && sel_end == 1);

    /* Backspace and Delete remove the logical predecessor/successor. */
    pt_widget_set_text(entry, mixed);
    pt_entry_set_selection(entry, 4, 4);
    pt_window_input_key(window, XKB_KEY_BackSpace, 0, NULL, true);
    pt_check(strcmp(pt_widget_get_text(entry), "abc" HE_BET " 123") == 0);
    pt_widget_set_text(entry, mixed);
    pt_entry_set_selection(entry, 4, 4);
    pt_window_input_key(window, XKB_KEY_Delete, 0, NULL, true);
    pt_check(strcmp(pt_widget_get_text(entry), "abc \xD7\x91\xD7\x92 123") == 0);

    /* A combining cluster never splits: e + U+0301 moves as one stop. */
    pt_widget_set_text(entry, "abc e\xCC\x81 " HE_BET);
    pt_entry_set_selection(entry, 4, 4);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(entry->cursor == 7);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(entry->cursor == 4);

    /* X9 removed controls keep their bytes but are not a caret stop. A forced
     * RTL base keeps the map path so the zero-width cluster is exercised. */
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget_set_text(entry, "\xD7\x90\xE2\x80\x8B\xD7\x91");
    pt_entry_set_selection(entry, 0, 0);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(entry->cursor == 2);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(entry->cursor == 7);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(entry->cursor == 7);

    /* RTL paragraph: Home is the visual left (logical end), End the visual
     * right (logical start), and Right raises x by lowering the offset. */
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget_set_text(entry, HE_BET " abc " HE_GIMEL_DALET_HE);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    expect_entry_caret(window, entry, 0, "rtl end");
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    expect_entry_caret(window, entry, 0, "rtl right edge");
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    expect_entry_caret(window, entry, strlen(pt_widget_get_text(entry)), "rtl home");
    const size_t rtl_right[] = {15, 13, 11, 10, 8, 9, 10, 6, 4, 2, 0};
    for (size_t i = 0; i < sizeof(rtl_right) / sizeof(rtl_right[0]); ++i) {
        pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
        expect_entry_caret(window, entry, rtl_right[i], "rtl right");
    }
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    expect_entry_caret(window, entry, 2, "rtl left");

    /* Password bullets follow the stored text's direction but expose nothing. */
    pt_widget_set_text(entry, HE_BET " abc");
    pt_entry_set_password(entry, true);
    pt_entry_set_selection(entry, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(entry->cursor == 0);
    pt_check(pt_entry_get_selection(entry, &sel_start, &sel_end) == false);
    pt_check(strcmp(pt_widget_get_text(entry), HE_BET " abc") == 0);

    /* With no explicit direction, the bullets' order still comes from the
     * stored Hebrew text: End stays the visual right (byte 0). */
    pt_widget_set_text_direction(entry, pt_text_direction_auto);
    pt_widget_set_text(entry, HE_BET);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(entry->cursor == 0);
    int password_right_x = 0;
    pt_widget_ime_rect(entry, &password_right_x, NULL, NULL, NULL);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(entry->cursor == 6);
    int password_left_x = 0;
    pt_widget_ime_rect(entry, &password_left_x, NULL, NULL, NULL);
    pt_check(password_right_x > password_left_x + 4);
    pt_entry_set_password(entry, false);

    /* A double-click word selection writes its own anchor and caret, so it
     * must clear a stale trailing affinity; otherwise the next visual caret
     * step starts from the wrong side of a run boundary. */
    pt_widget_set_text_direction(entry, pt_text_direction_auto);
    pt_widget_set_text(entry, mixed);
    pt_entry_set_selection(entry, 4, 4);
    entry->caret_affinity = pt_text_bias_trailing;
    pt_widget_pointer_multi_click(entry,
                                  entry->x + entry->padding_left - entry->scroll_x + 4.0f,
                                  entry->y + entry->height * 0.5f, 2);
    pt_check(entry->selection_anchor == 0 && entry->cursor == 3);
    pt_check(entry->caret_affinity == pt_text_bias_leading);

    /* The text area shares the visual map: one line of the same mixed text
     * moves identically, and Home/End are visual edges. */
    pt_widget *area = pt_text_area_create(root, "");
    pt_check(area != NULL);
    pt_widget_set_text(area, mixed);
    pt_widget_set_size(area, 600.0f, 120.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_window_paint(window, canvas);
    pt_check(pt_widget_focus(area));
    pt_text_area_set_selection(area, 0, 0);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == 1);
    int caret_x = 0;
    pt_widget_ime_rect(area, &caret_x, NULL, NULL, NULL);
    pt_check_near((float)caret_x,
                  area->x + area->padding_left + text_area_expected_caret_x(area), 1.0f);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(area->cursor == 4);
    pt_text_area_set_selection(area, 4, 4);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    for (int i = 0; i < 4; ++i) {
        pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    }
    pt_check(area->cursor == 4);
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(area->cursor == 12);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(area->cursor == 0);
    pt_widget_ime_rect(area, &caret_x, NULL, NULL, NULL);
    pt_check_near((float)caret_x,
                  area->x + area->padding_left + text_area_expected_caret_x(area), 1.0f);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* RTL entries anchor their text to the right edge (2.0 wave 5b): scroll_x = 0
 * shows the logical start at the visual right, and overflowing backwards moves
 * the line right. IME rectangles and click mapping use the same origin. */
static void test_rtl_entry_anchor(pt_app *app) {
    pt_window *window = pt_window_create(app, "rtl-entry-anchor", 640, 240);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(root, 8.0f);
    pt_widget *entry = pt_entry_create(root, "");
    pt_check(entry != NULL);
    const char *long_rtl = HE_BET HE_GIMEL_DALET_HE HE_BET HE_GIMEL_DALET_HE
                           HE_BET HE_GIMEL_DALET_HE " abc " HE_BET;
    pt_widget_set_text(entry, long_rtl);
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget_set_size(entry, 200.0f, 32.0f);
    pt_widget_set_align_self(entry, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = NULL;
    paint(window, &canvas);
    pt_check(canvas != NULL);
    pt_check(pt_widget_focus(entry));
    window->caret_visible = true;

    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    pt_check(font != NULL);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    float area_w = entry->width - entry->padding_left - entry->padding_right;
    float total = font ? pt_font_measure_bidi(font, long_rtl, strlen(long_rtl),
                                              pt_bidi_base_rtl) / scale
                       : 0.0f;
    pt_check(total > area_w);

    /* Logical start: the caret sits on the entry's right edge, no scroll. */
    pt_entry_set_selection(entry, 0, 0);
    paint(window, &canvas);
    pt_check_near(entry->scroll_x, 0.0, 0.01);
    int x0 = 0;
    pt_widget_ime_rect(entry, &x0, NULL, NULL, NULL);
    pt_check_near((float)x0,
                  entry->x + entry->width - entry->padding_right, 1.0f);

    /* Logical end: the caret is pulled into view from the left, which scrolls
     * the right-anchored line right by total - width + 12. */
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(entry->cursor == strlen(long_rtl));
    paint(window, &canvas);
    float expected_scroll = total - area_w + 12.0f;
    pt_check_near(entry->scroll_x, expected_scroll, 0.6);
    int x_end = 0;
    pt_widget_ime_rect(entry, &x_end, NULL, NULL, NULL);
    pt_check_near((float)x_end, entry->x + entry->padding_left + 12.0f, 1.0f);
    pt_check(x_end >= (int)entry->x && x_end <= (int)(entry->x + entry->width));

    /* Going back to the logical start returns to the right anchor. */
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(entry->cursor == 0);
    paint(window, &canvas);
    pt_check_near(entry->scroll_x, 0.0, 0.01);

    /* Click mapping follows the same origin: the right edge is the logical
     * start, the left edge the logical end. */
    float mid_y = entry->y + entry->height * 0.5f;
    click_at(window, entry->x + entry->width - 6.0f, mid_y, 100);
    pt_check(entry->cursor <= 2);
    click_at(window, entry->x + 4.0f, mid_y, 140);
    pt_check(entry->cursor + 2 >= strlen(long_rtl));
    paint(window, &canvas);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Count exact-colored pixels along one scanline. Chrome colours are opaque, so
 * a fully covered fill yields the token value byte for byte. */
static int scan_color_count(const pt_canvas *canvas, int y, int x0, int x1,
                            pt_color want) {
    int count = 0;
    for (int x = x0; x <= x1; ++x) {
        if (x >= 0 && x < canvas->width && pt_canvas_get_pixel(canvas, x, y) == want) {
            ++count;
        }
    }
    return count;
}

/* RTL entries anchor their line to the content box's right edge, which is the
 * exclusive clip boundary: the caret at the logical start must be painted just
 * inside it. Regression for the wave 5b right-anchoring change, which left the
 * caret invisible for empty and start-of-line RTL entries. */
static void test_rtl_entry_caret_pixels(pt_app *app) {
    pt_window *window = pt_window_create(app, "rtl-entry-caret", 640, 260);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(root, 8.0f);
    pt_widget *entry = pt_entry_create(root, "");
    pt_check(entry != NULL);
    pt_widget_set_size(entry, 200.0f, 32.0f);
    pt_widget_set_align_self(entry, pt_align_start);
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget *ltr = pt_entry_create(root, "");
    pt_check(ltr != NULL);
    pt_widget_set_size(ltr, 200.0f, 32.0f);
    pt_widget_set_align_self(ltr, pt_align_start);
    pt_widget_set_text_direction(ltr, pt_text_direction_ltr);
    pt_window_show(window);
    pt_canvas *canvas = NULL;
    paint(window, &canvas);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_color accent = pt_app_get_theme_color(app, pt_theme_accent);
    const char *long_rtl = HE_BET HE_GIMEL_DALET_HE HE_BET HE_GIMEL_DALET_HE
                           HE_BET HE_GIMEL_DALET_HE HE_BET HE_GIMEL_DALET_HE
                           HE_BET HE_GIMEL_DALET_HE HE_BET HE_GIMEL_DALET_HE;
    float area_left = entry->x + entry->padding_left;
    float area_right = entry->x + entry->width - entry->padding_right;
    int mid = (int)(entry->y + entry->height * 0.5f);

    pt_check(pt_widget_focus(entry));
    window->caret_visible = true;
    paint(window, &canvas);
    /* Empty RTL entry: the caret sits one pixel inside the right edge ... */
    pt_check(pt_canvas_get_pixel(canvas, (int)area_right - 1, mid) == accent);
    /* ... and not on the visual left. */
    pt_check(scan_color_count(canvas, mid, (int)area_left, (int)area_left + 10,
                              accent) == 0);

    /* Short text, cursor 0: the logical start is still the right edge. */
    pt_widget_set_text(entry, HE_BET);
    pt_entry_set_selection(entry, 0, 0);
    window->caret_visible = true;
    paint(window, &canvas);
    pt_check(pt_canvas_get_pixel(canvas, (int)area_right - 1, mid) == accent);

    /* Overlong text, cursor 0: scroll stays zero at the anchor, so the caret
     * stays on the right edge. */
    pt_widget_set_text(entry, long_rtl);
    pt_entry_set_selection(entry, 0, 0);
    window->caret_visible = true;
    paint(window, &canvas);
    pt_check(entry->scroll_x == 0.0f);
    pt_check(pt_canvas_get_pixel(canvas, (int)area_right - 1, mid) == accent);

    /* Home runs to the logical end (visual left), End returns to the start:
     * the caret must be visible in both places. */
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    window->caret_visible = true;
    paint(window, &canvas);
    pt_check(entry->cursor == strlen(long_rtl));
    pt_check(scan_color_count(canvas, mid, (int)area_left, (int)area_right,
                              accent) > 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    window->caret_visible = true;
    paint(window, &canvas);
    pt_check(entry->cursor == 0);
    pt_check(pt_canvas_get_pixel(canvas, (int)area_right - 1, mid) == accent);

    /* LTR control: unchanged, the caret stays on the left edge. */
    pt_check(pt_widget_focus(ltr));
    window->caret_visible = true;
    paint(window, &canvas);
    int ltr_left = (int)(ltr->x + ltr->padding_left);
    int ltr_right = (int)(ltr->x + ltr->width - ltr->padding_right);
    int ltr_mid = (int)(ltr->y + ltr->height * 0.5f);
    pt_check(pt_canvas_get_pixel(canvas, ltr_left, ltr_mid) == accent);
    pt_check(pt_canvas_get_pixel(canvas, ltr_right - 1, ltr_mid) != accent);

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
    test_bidi_entry_editing(app);
    test_rtl_entry_anchor(app);
    test_rtl_entry_caret_pixels(app);
    pt_app_destroy(app);
    return pt_test_finish("editing");
}
