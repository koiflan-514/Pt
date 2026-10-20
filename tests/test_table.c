/*
 * Virtualized table checks: columns, sorting, selection, resizing and scrolling.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct table_state_log {
    int binds;
    int selects;
    int activates;
    int sorts;
    int multi_sorts;
    uint32_t last_row;
    uint32_t last_column;
    uint32_t last_select;
    uint32_t last_activate;
    uint32_t sort_column;
    pt_sort_direction sort_direction;
    uint32_t multi_count;
    uint32_t multi_columns[PT_TABLE_MAX_SORT_COLUMNS];
    pt_sort_direction multi_directions[PT_TABLE_MAX_SORT_COLUMNS];
} table_state_log;

static uint64_t row_keys[64];
static const char *row_names[] = {"Alpha", "Beta", "Charlie", "Delta", "Echo", "Foxtrot"};

static uint64_t table_row_key(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    (void)data;
    return row < 64 ? row_keys[row] : pt_row_key_none;
}

static const char *table_row_text(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    (void)data;
    return row_names[row % (sizeof(row_names) / sizeof(row_names[0]))];
}

static void sort_columns(pt_widget *table, uint32_t count, const uint32_t *columns,
                         const pt_sort_direction *directions, void *data) {
    (void)table;
    table_state_log *log = data;
    ++log->multi_sorts;
    log->multi_count = count;
    for (uint32_t i = 0; i < count && i < PT_TABLE_MAX_SORT_COLUMNS; ++i) {
        log->multi_columns[i] = columns[i];
        log->multi_directions[i] = directions[i];
    }
}

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    (void)table;
    table_state_log *log = data;
    char label[32];
    snprintf(label, sizeof(label), "r%uc%u", row, column);
    pt_widget_set_text(cell, label);
    ++log->binds;
    log->last_row = row;
    log->last_column = column;
}

static void select_row(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    table_state_log *log = data;
    ++log->selects;
    log->last_select = row;
}

static void activate_row(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    table_state_log *log = data;
    ++log->activates;
    log->last_activate = row;
}

static void sort_column(pt_widget *table, uint32_t column, pt_sort_direction direction,
                        void *data) {
    (void)table;
    table_state_log *log = data;
    ++log->sorts;
    log->sort_column = column;
    log->sort_direction = direction;
}

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

static pt_widget *header_cell(pt_widget *table, uint32_t column) {
    pt_table_state *state = table ? table->extra : NULL;
    if (!state || !state->header) {
        return NULL;
    }
    pt_widget *cell = state->header->first_child;
    while (cell && column > 0) {
        cell = cell->next_sibling;
        --column;
    }
    return cell && cell->kind == pt_widget_table_header_cell ? cell : NULL;
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

static int visible_rows(pt_widget *table) {
    int count = 0;
    for (pt_widget *row = table->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_table_row && row->visible) {
            ++count;
        }
    }
    return count;
}

static int materialized_rows(pt_widget *table) {
    int count = 0;
    for (pt_widget *row = table->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_table_row) {
            ++count;
        }
    }
    return count;
}

static void test_table(pt_app *app) {
    pt_window *window = pt_window_create(app, "table", 640, 480);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *table = pt_table_create(root);
    pt_check(table != NULL);
    pt_widget_set_align_self(table, pt_align_start);
    pt_widget_set_width(table, 400.0f);
    pt_widget_set_height(table, 300.0f);
    pt_check(pt_table_get_column_count(table) == 3);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Modified");
    pt_table_set_column_title(table, 2, "Size");
    pt_table_set_column_width(table, 0, 120.0f);
    pt_table_set_column_width(table, 1, 140.0f);
    pt_table_set_column_width(table, 2, 100.0f);
    pt_table_set_column_align(table, 2, pt_text_align_end);
    pt_table_set_column_resizable(table, 2, false);
    pt_check_near(pt_table_get_column_width(table, 0), 120.0, 0.01);
    pt_check_near(pt_table_get_column_width(table, 1), 140.0, 0.01);
    pt_check_near(pt_table_get_column_width(table, 2), 100.0, 0.01);
    pt_table_set_row_height(table, 40.0f);
    pt_table_set_header_height(table, 38.0f);
    pt_check_near(pt_table_get_row_height(table), 40.0, 0.01);

    table_state_log log = {0};
    pt_table_set_on_bind_cell(table, bind_cell, &log);
    pt_table_set_on_select(table, select_row, &log);
    pt_table_set_on_activate(table, activate_row, &log);
    pt_table_set_on_sort(table, sort_column, &log);
    pt_table_set_row_count(table, 50);
    pt_check(pt_table_get_row_count(table) == 50);
    pt_check(pt_table_get_selected(table) == pt_list_index_none);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(640, 480, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect table_rect = bounds_of(table);
    pt_check_near(table_rect.x, 0.0, 0.01);
    pt_check_near(table_rect.y, 44.0, 0.01);
    pt_check_near(table_rect.w, 400.0, 0.01);
    pt_check_near(table_rect.h, 300.0, 0.01);

    /* 262 px of rows / 40 px = 6.55, so 7 rows plus one overscan row. */
    pt_check(visible_rows(table) == 8);
    pt_check(materialized_rows(table) == 8);
    pt_check(log.binds == 24);
    pt_check(log.last_row == 7);
    pt_check(log.last_column == 2);

    pt_widget *first = visible_row(table, 0);
    pt_widget *last = visible_row(table, 7);
    pt_check(first != NULL && last != NULL);
    if (first && last) {
        pt_rect first_rect = bounds_of(first);
        pt_rect last_rect = bounds_of(last);
        pt_check_near(first_rect.x, 0.0, 0.01);
        pt_check_near(first_rect.y, 82.0, 0.01);
        pt_check_near(first_rect.h, 40.0, 0.01);
        pt_check_near(last_rect.y, 362.0, 0.01);
        pt_check(pt_widget_get_a11y_role(table) == pt_a11y_role_table);
        pt_check(pt_widget_get_a11y_role(first) == pt_a11y_role_table_row);
        pt_widget *cell = row_cell(first, 0);
        pt_check(cell != NULL);
        if (cell) {
            pt_check(pt_widget_get_kind(cell) == pt_widget_table_cell);
            pt_check(strcmp(pt_widget_get_text(cell), "r0c0") == 0);
            pt_check(pt_widget_get_a11y_role(cell) == pt_a11y_role_table_cell);
            pt_check_near(bounds_of(cell).w, 120.0, 0.01);
        }
        cell = row_cell(first, 2);
        pt_check(cell != NULL);
        if (cell) {
            pt_check(strcmp(pt_widget_get_text(cell), "r0c2") == 0);
            pt_check_near(bounds_of(cell).w, 100.0, 0.01);
            pt_check(cell->text_align == pt_text_align_end);
        }
    }

    pt_widget *head = header_cell(table, 0);
    pt_check(head != NULL);
    if (head) {
        pt_check(strcmp(pt_widget_get_text(head), "Name") == 0);
        pt_check(pt_widget_get_a11y_role(head) == pt_a11y_role_column_header);
    }

    /* Row click selects, arrow keys move and Return activates. */
    pt_rect first_rect = bounds_of(first);
    click_at(window, first_rect.x + 30.0f, first_rect.y + first_rect.h * 0.5f);
    pt_check(pt_table_get_selected(table) == 0);
    pt_check(log.selects == 1);
    pt_check(log.last_select == 0);
    pt_check(window->focused == table);
    pt_widget *selected_row = visible_row(table, 0);
    pt_check(selected_row != NULL && selected_row->row_selected);
    if (selected_row) {
        pt_check((pt_widget_get_a11y_state(selected_row) & pt_a11y_state_selected) != 0);
    }
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(pt_table_get_selected(table) == 1);
    pt_check(log.selects == 2);
    pt_window_input_key(window, XKB_KEY_Return, 0, NULL, true);
    pt_check(log.activates == 1);
    pt_check(log.last_activate == 1);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(pt_table_get_selected(table) == 49);

    /* Header clicks cycle ascending, descending and none. */
    pt_check(pt_window_paint(window, canvas));
    head = header_cell(table, 0);
    pt_rect head_rect = bounds_of(head);
    click_at(window, head_rect.x + 24.0f, head_rect.y + head_rect.h * 0.5f);
    uint32_t sort_column_index = 99;
    pt_check(pt_table_get_sort(table, &sort_column_index) == pt_sort_ascending);
    pt_check(sort_column_index == 0);
    pt_check(log.sorts == 1);
    pt_check(log.sort_direction == pt_sort_ascending);
    pt_check(head->icon == pt_icon_chevron_up);
    click_at(window, head_rect.x + 24.0f, head_rect.y + head_rect.h * 0.5f);
    pt_check(pt_table_get_sort(table, &sort_column_index) == pt_sort_descending);
    pt_check(log.sorts == 2);
    pt_check(log.sort_direction == pt_sort_descending);
    pt_check(head->icon == pt_icon_chevron_down);
    click_at(window, head_rect.x + 24.0f, head_rect.y + head_rect.h * 0.5f);
    pt_check(pt_table_get_sort(table, &sort_column_index) == pt_sort_none);
    pt_check(sort_column_index == pt_table_column_none);
    pt_check(log.sorts == 3);
    pt_check(head->icon == pt_icon_none);

    /* Dragging the resize edge widens the column. */
    pt_widget *second_head = header_cell(table, 1);
    pt_rect second_rect = bounds_of(second_head);
    float edge = second_rect.x + second_rect.w;
    float mid_y = second_rect.y + second_rect.h * 0.5f;
    pt_window_input_pointer_enter(window, edge - 2.0f, mid_y);
    pt_check(pt_widget_cursor_at(table, edge - 2.0f, mid_y) == pt_cursor_resize_h);
    pt_window_input_pointer_button(window, 0x110u, true, edge - 2.0f, mid_y, 3, 100);
    pt_check(second_head->pressed);
    pt_window_input_pointer_motion(window, edge + 38.0f, mid_y, 110);
    pt_check_near(pt_table_get_column_width(table, 1), 180.0, 0.6);
    pt_window_input_pointer_button(window, 0x110u, false, edge + 38.0f, mid_y, 4, 120);
    pt_table_state *state = table->extra;
    pt_check(state->resizing_column == -1);

    /* The non-resizable third column keeps its width when its edge is dragged. */
    pt_check(pt_window_paint(window, canvas));
    pt_widget *third_head = header_cell(table, 2);
    pt_rect third_rect = bounds_of(third_head);
    float third_edge = third_rect.x + third_rect.w;
    float third_width = pt_table_get_column_width(table, 2);
    pt_window_input_pointer_enter(window, third_edge - 2.0f, mid_y);
    pt_window_input_pointer_button(window, 0x110u, true, third_edge - 2.0f, mid_y, 5, 200);
    pt_window_input_pointer_motion(window, third_edge + 20.0f, mid_y, 210);
    pt_window_input_pointer_button(window, 0x110u, false, third_edge + 20.0f, mid_y, 6, 220);
    pt_check_near(pt_table_get_column_width(table, 2), third_width, 0.01);

    /* Horizontal reveal brings the last column into view. */
    pt_table_set_column_width(table, 0, 200.0f);
    pt_table_set_column_width(table, 1, 200.0f);
    pt_table_set_column_width(table, 2, 200.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_table_scroll_to_column(table, 2);
    pt_check_near(table->scroll_x, 200.0, 0.01);
    pt_table_scroll_to_row(table, 0);
    pt_check(pt_window_paint(window, canvas));
    pt_widget *third_visible = row_cell(visible_row(table, 0), 2);
    pt_check(third_visible != NULL);
    if (third_visible) {
        pt_check_near(bounds_of(third_visible).x, 200.0, 0.01);
    }
    pt_table_scroll_to_column(table, 0);
    pt_check_near(table->scroll_x, 0.0, 0.01);
    pt_table_scroll_to_row(table, 49);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_row(table, 49) != NULL);
    pt_check(visible_row(table, 0) == NULL);
    pt_check(materialized_rows(table) <= 9);

    /* Shrinking the model clamps selection and rebinds without stale cells. */
    pt_table_set_selected(table, 10);
    pt_check(pt_table_get_selected(table) == 10);
    pt_table_set_row_count(table, 5);
    pt_check(pt_table_get_selected(table) == pt_list_index_none);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(table) == 5);
    pt_check(materialized_rows(table) <= 9);
    pt_widget *row_zero = visible_row(table, 0);
    pt_check(row_zero != NULL);
    if (row_zero) {
        pt_widget *cell = row_cell(row_zero, 0);
        pt_check(cell != NULL);
        if (cell) {
            pt_check(strcmp(pt_widget_get_text(cell), "r0c0") == 0);
        }
    }

    /* Keyed selection survives model reordering and row-count changes. */
    for (uint32_t i = 0; i < 64; ++i) {
        row_keys[i] = 1000 + i;
    }
    pt_table_set_row_key_callback(table, table_row_key, NULL);
    pt_table_set_row_text_callback(table, table_row_text, NULL);
    pt_table_set_row_count(table, 8);
    pt_table_set_selected(table, 3);
    pt_check(pt_table_get_selected_key(table) == 1003);
    uint64_t reordered[8] = {row_keys[3], row_keys[0], row_keys[1], row_keys[2],
                             row_keys[4], row_keys[5], row_keys[6], row_keys[7]};
    memcpy(row_keys, reordered, sizeof(reordered));
    pt_table_refresh(table);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_table_get_selected(table) == 0);
    pt_check(pt_table_get_selected_key(table) == 1003);
    pt_check(pt_table_set_selected_key(table, 1006));
    pt_check(pt_table_get_selected(table) == 6);
    pt_check(!pt_table_set_selected_key(table, 424242));
    pt_check(pt_table_get_selected(table) == 6);
    pt_table_set_row_count(table, 20);
    pt_check(pt_table_get_selected(table) == 6);
    pt_check(pt_table_get_selected_key(table) == 1006);

    /* Batch invalidation rebinds only rows intersecting the range. */
    pt_table_set_selected(table, 0);
    pt_check(pt_window_paint(window, canvas));
    log.binds = 0;
    pt_table_refresh_rows(table, 0, 2);
    pt_check(pt_window_paint(window, canvas));
    pt_check(log.binds == 6);
    log.binds = 0;
    pt_table_refresh_rows(table, 100, 5);
    pt_check(pt_window_paint(window, canvas));
    pt_check(log.binds == 0);

    /* Type-ahead matches row text case-insensitively and cycles repeats. */
    pt_window_focus(window, table);
    pt_table_set_selected(table, 0);
    table->typeahead_length = 0;
    pt_window_input_key(window, XKB_KEY_b, 0, "b", true);
    pt_check(pt_table_get_selected(table) == 1);
    pt_table_set_selected(table, 0);
    table->typeahead_length = 0;
    pt_window_input_key(window, XKB_KEY_a, 0, "a", true);
    pt_check(pt_table_get_selected(table) == 6);
    pt_window_input_key(window, XKB_KEY_a, 0, "a", true);
    pt_check(pt_table_get_selected(table) == 12);
    pt_window_input_key(window, XKB_KEY_a, 0, "a", true);
    pt_check(pt_table_get_selected(table) == 18);
    pt_window_input_key(window, XKB_KEY_a, 0, "a", true);
    pt_check(pt_table_get_selected(table) == 0);

    /* Multi-column sorting composes with Shift-click and reports priority. */
    pt_table_set_on_sort_columns(table, sort_columns, &log);
    uint32_t sort_columns_in[2] = {2, 1};
    pt_sort_direction sort_dirs_in[2] = {pt_sort_ascending, pt_sort_descending};
    pt_table_set_sort_columns(table, 2, sort_columns_in, sort_dirs_in);
    uint32_t sort_columns_out[PT_TABLE_MAX_SORT_COLUMNS] = {0};
    pt_sort_direction sort_dirs_out[PT_TABLE_MAX_SORT_COLUMNS] = {0};
    pt_check(pt_table_get_sort_columns(table, sort_columns_out, sort_dirs_out) == 2);
    pt_check(sort_columns_out[0] == 2 && sort_dirs_out[0] == pt_sort_ascending);
    pt_check(sort_columns_out[1] == 1 && sort_dirs_out[1] == pt_sort_descending);
    pt_check(pt_window_paint(window, canvas));
    pt_widget *multi_head = header_cell(table, 0);
    pt_rect multi_rect = bounds_of(multi_head);
    pt_window_set_input_modifiers(window, pt_key_modifier_shift);
    click_at(window, multi_rect.x + 20.0f, multi_rect.y + multi_rect.h * 0.5f);
    pt_check(pt_table_get_sort_columns(table, sort_columns_out, sort_dirs_out) == 3);
    pt_check(sort_columns_out[2] == 0 && sort_dirs_out[2] == pt_sort_ascending);
    pt_check(log.multi_sorts == 1 && log.multi_count == 3);
    pt_window_set_input_modifiers(window, 0);
    click_at(window, multi_rect.x + 20.0f, multi_rect.y + multi_rect.h * 0.5f);
    pt_check(pt_table_get_sort_columns(table, sort_columns_out, sort_dirs_out) == 1);
    pt_check(sort_columns_out[0] == 0 && sort_dirs_out[0] == pt_sort_ascending);
    pt_check(log.multi_sorts == 2 && log.multi_count == 1);

    /* Hidden columns keep data and width but leave layout and painting. */
    float width_before_hide = pt_table_get_column_width(table, 1);
    pt_widget *hidden_head = header_cell(table, 1);
    pt_widget *visible_head = header_cell(table, 2);
    pt_table_set_column_hidden(table, 1, true);
    pt_check(pt_table_get_column_hidden(table, 1));
    pt_check(pt_window_paint(window, canvas));
    pt_check(!hidden_head->visible);
    pt_check(visible_head->visible);
    pt_check_near(bounds_of(visible_head).x, bounds_of(header_cell(table, 0)).w, 0.01);
    pt_widget *row_after_hide = visible_row(table, 0);
    pt_check(row_after_hide != NULL);
    if (row_after_hide) {
        pt_widget *hidden_cell = row_cell(row_after_hide, 1);
        pt_check(hidden_cell && !hidden_cell->visible);
    }
    pt_check_near(pt_table_get_column_width(table, 1), width_before_hide, 0.01);
    pt_table_set_column_hidden(table, 1, false);
    pt_check(pt_window_paint(window, canvas));
    pt_check(hidden_head->visible);
    pt_check_near(bounds_of(visible_head).x,
                  bounds_of(header_cell(table, 0)).w + width_before_hide, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.table");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("table");
    }
    test_table(app);
    pt_app_destroy(app);
    return pt_test_finish("table");
}
