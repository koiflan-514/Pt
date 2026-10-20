/*
 * Pt virtualized table data view.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define pt_table_default_width 160.0f
#define pt_table_min_width 48.0f

static pt_table_state *table_state(pt_widget *table) {
    if (!table || table->kind != pt_widget_table) {
        return NULL;
    }
    return table->extra;
}

static const pt_table_state *table_state_const(const pt_widget *table) {
    if (!table || table->kind != pt_widget_table) {
        return NULL;
    }
    return table->extra;
}

static uint32_t table_column_count(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    return state ? state->column_count : 0;
}

static pt_widget *table_row_at(pt_widget *table, uint32_t index) {
    pt_table_state *state = table_state(table);
    if (!state || !state->header) {
        return NULL;
    }
    pt_widget *row = state->header->next_sibling;
    while (row && index > 0) {
        row = row->next_sibling;
        --index;
    }
    return row && row->kind == pt_widget_table_row ? row : NULL;
}

static pt_widget *table_cell_at(pt_widget *row, uint32_t column) {
    pt_widget *cell = row ? row->first_child : NULL;
    while (cell && column > 0) {
        cell = cell->next_sibling;
        --column;
    }
    return cell && cell->kind == pt_widget_table_cell ? cell : NULL;
}

static pt_widget *table_row_widget(pt_widget *table, uint32_t index) {
    pt_table_state *state = table_state(table);
    if (!state || !state->header) {
        return NULL;
    }
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        if (row->kind == pt_widget_table_row && row->list_index == index) {
            return row;
        }
    }
    return NULL;
}

static void table_selection_ensure(pt_widget *table, pt_table_state *state) {
    if (table->row_count <= state->selection_capacity) {
        return;
    }
    uint32_t capacity = table->row_count;
    uint32_t doubled = state->selection_capacity * 2u + 16u;
    if (doubled > capacity) {
        capacity = doubled;
    }
    uint8_t *rows = pt_realloc_mem(state->selection, capacity * sizeof(*rows));
    memset(rows + state->selection_capacity, 0,
           (capacity - state->selection_capacity) * sizeof(*rows));
    state->selection = rows;
    state->selection_capacity = capacity;
}

static bool table_row_is_selected(const pt_widget *table, const pt_table_state *state,
                                  uint32_t row) {
    if (row < state->selection_capacity && state->selection[row]) {
        return true;
    }
    return row == table->list_selected;
}

static void table_selection_reset(pt_table_state *state) {
    if (state->selection) {
        memset(state->selection, 0, state->selection_capacity * sizeof(*state->selection));
    }
    state->selection_count = 0;
}

static void table_selection_add(pt_table_state *state, uint32_t row) {
    if (row < state->selection_capacity && !state->selection[row]) {
        state->selection[row] = 1;
        ++state->selection_count;
    }
}

static void table_selection_drop(pt_table_state *state, uint32_t row) {
    if (row < state->selection_capacity && state->selection[row]) {
        state->selection[row] = 0;
        --state->selection_count;
    }
}

static void table_selection_apply(pt_widget *table, pt_table_state *state);

/* The public set helpers stay silent so an application can rebuild a selection
 * without echoing, but every gesture that moves the primary row reports it, the
 * way a plain click already does. */
static void table_notify_primary(pt_widget *table, uint32_t previous) {
    if (table->on_select && table->list_selected != previous &&
        table->list_selected != pt_list_index_none) {
        table->on_select(table, table->list_selected, table->on_select_data);
    }
}

/* Rows report their bounds in window space, the same frame the rubber band is
 * dragged in, so a rectangle test needs nothing but the visible pool. Ctrl keeps
 * what the view already had and adds the band on top; without it the band
 * replaces the selection. */
static void table_apply_marquee(pt_widget *table, pt_table_state *state) {
    uint32_t previous = table->list_selected;
    pt_window *window = pt_widget_window(table);
    bool additive = window && (window->modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    table_selection_ensure(table, state);
    if (!additive) {
        table_selection_reset(state);
    }
    uint32_t before = state->selection_count;
    uint32_t first_hit = pt_list_index_none;
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind != pt_widget_table_row || !child->list_bound ||
            child->list_index >= table->row_count) {
            continue;
        }
        if (!pt_marquee_hit(&state->marquee,
                           (pt_rect){child->x, child->y, child->width, child->height})) {
            continue;
        }
        table_selection_add(state, child->list_index);
        if (first_hit == pt_list_index_none || child->list_index < first_hit) {
            first_hit = child->list_index;
        }
    }
    if (first_hit != pt_list_index_none) {
        table->list_selected = first_hit;
        pt_data_view_sync_selected_key(table);
        table_selection_apply(table, state);
        pt_widget_mark_dirty(table, false);
        if (state->selection_count != before) {
            pt_widget_a11y_emit(table, pt_a11y_event_selection_changed);
        }
        table_notify_primary(table, previous);
    }
}

static void table_selection_apply(pt_widget *table, pt_table_state *state) {
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_table_row) {
            child->row_selected =
                child->list_bound &&
                table_row_is_selected(table, state, child->list_index);
        }
    }
}

static bool table_column_hidden(const pt_table_state *state, uint32_t column) {
    return state && state->column_hidden && column < state->column_count &&
           state->column_hidden[column];
}

static uint32_t table_visible_column_count(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    if (!state) {
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < state->column_count; ++i) {
        if (!table_column_hidden(state, i)) {
            ++count;
        }
    }
    return count;
}

static float table_column_left(const pt_widget *table, uint32_t column) {
    const pt_table_state *state = table_state_const(table);
    if (!state) {
        return 0.0f;
    }
    if (column > state->column_count) {
        column = state->column_count;
    }
    float left = 0.0f;
    for (uint32_t i = 0; i < column; ++i) {
        if (!table_column_hidden(state, i)) {
            left += state->column_widths[i];
        }
    }
    return left;
}

static int32_t table_sort_index(const pt_table_state *state, uint32_t column) {
    if (!state) {
        return -1;
    }
    for (uint32_t i = 0; i < state->sort_count; ++i) {
        if (state->sort_columns[i] == column) {
            return (int32_t)i;
        }
    }
    return -1;
}

static pt_sort_direction table_sort_direction_for(const pt_table_state *state,
                                                  uint32_t column) {
    int32_t index = table_sort_index(state, column);
    return index >= 0 ? state->sort_directions[index] : pt_sort_none;
}

static float table_content_width(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    if (!state) {
        return 0.0f;
    }
    float width = 0.0f;
    for (uint32_t i = 0; i < state->column_count; ++i) {
        if (!table_column_hidden(state, i)) {
            width += state->column_widths[i];
        }
    }
    return width;
}

static void table_update_header_icons(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state || !state->header) {
        return;
    }
    uint32_t column = 0;
    for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
        if (cell->kind != pt_widget_table_header_cell) {
            continue;
        }
        pt_icon icon = pt_icon_none;
        pt_sort_direction direction = table_sort_direction_for(state, column);
        if (direction == pt_sort_ascending) {
            icon = pt_icon_chevron_up;
        } else if (direction == pt_sort_descending) {
            icon = pt_icon_chevron_down;
        }
        cell->icon = icon;
        ++column;
    }
}

static void table_clear_rows(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state || !state->header) {
        return;
    }
    pt_widget *row = state->header->next_sibling;
    while (row) {
        pt_widget *next = row->next_sibling;
        pt_widget_destroy(row);
        row = next;
    }
}

static void table_sync_row_cells(pt_widget *table, pt_widget *row) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    while (row->first_child) {
        pt_widget_destroy(row->first_child);
    }
    for (uint32_t column = 0; column < state->column_count; ++column) {
        pt_widget *cell = pt_widget_new(row, pt_widget_table_cell);
        cell->column_index = column;
        cell->requested_width = state->column_widths[column];
        cell->text_align = state->column_aligns[column];
        cell->visible = !table_column_hidden(state, column);
    }
}

pt_widget *pt_table_create(pt_widget *parent) {
    pt_widget *table = pt_widget_new(parent, pt_widget_table);
    pt_table_state *state = pt_alloc_zeroed(1, sizeof(*state));
    table->extra = state;
    state->header_height = 38.0f;
    state->sort_count = 0;
    state->resizing_column = -1;
    state->editing_row = -1;
    state->editing_column = -1;
    state->reorder_column = -1;
    state->reorder_target = -1;
    state->dragged_row = -1;
    table->row_height = 40.0f;
    state->header = pt_widget_new(table, pt_widget_table_header);
    state->header->direction = pt_direction_row;
    state->header->gap = 0.0f;
    state->header->padding_top = 0.0f;
    state->header->padding_right = 0.0f;
    state->header->padding_bottom = 0.0f;
    state->header->padding_left = 0.0f;
    state->header->radius = 0.0f;
    pt_table_set_column_count(table, 3);
    return table;
}

void pt_table_set_column_count(pt_widget *table, uint32_t count) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    uint32_t old_count = state->column_count;
    float *widths = pt_alloc_zeroed(count ? count : 1, sizeof(*widths));
    char **titles = pt_alloc_zeroed(count ? count : 1, sizeof(*titles));
    pt_text_align *aligns = pt_alloc_zeroed(count ? count : 1, sizeof(*aligns));
    bool *resizable = pt_alloc_zeroed(count ? count : 1, sizeof(*resizable));
    bool *hidden = pt_alloc_zeroed(count ? count : 1, sizeof(*hidden));
    for (uint32_t i = 0; i < count; ++i) {
        widths[i] = i < old_count ? state->column_widths[i] : pt_table_default_width;
        titles[i] = pt_strdup(i < old_count && state->column_titles[i]
                                  ? state->column_titles[i]
                                  : "");
        aligns[i] = i < old_count ? state->column_aligns[i] : pt_text_align_start;
        resizable[i] = i < old_count ? state->column_resizable[i] : true;
        hidden[i] = i < old_count ? state->column_hidden[i] : false;
    }
    for (uint32_t i = 0; i < old_count; ++i) {
        free(state->column_titles[i]);
    }
    free(state->column_widths);
    free(state->column_titles);
    free(state->column_aligns);
    free(state->column_resizable);
    free(state->column_hidden);
    state->column_widths = widths;
    state->column_titles = titles;
    state->column_aligns = aligns;
    state->column_resizable = resizable;
    state->column_hidden = hidden;
    state->column_count = count;

    uint32_t sort_count = 0;
    for (uint32_t i = 0; i < state->sort_count; ++i) {
        uint32_t column = state->sort_columns[i];
        if (column < count) {
            state->sort_columns[sort_count] = column;
            state->sort_directions[sort_count] = state->sort_directions[i];
            ++sort_count;
        }
    }
    state->sort_count = sort_count;

    pt_widget_clear_children(state->header);
    for (uint32_t i = 0; i < count; ++i) {
        pt_widget *cell = pt_widget_new(state->header, pt_widget_table_header_cell);
        cell->column_index = i;
        cell->text = pt_strdup(titles[i]);
        cell->requested_width = widths[i];
        cell->text_align = aligns[i];
        cell->font_weight = pt_font_weight_semibold;
        cell->font_size = 13.0f;
        cell->radius = 0.0f;
        cell->padding_left = 12.0f;
        cell->padding_right = 12.0f;
        cell->visible = !hidden[i];
    }
    table_clear_rows(table);
    state->rebind = true;
    table_update_header_icons(table);
    pt_widget_mark_dirty(table, true);
    pt_widget_a11y_emit(table, pt_a11y_event_structure_changed);
}

uint32_t pt_table_get_column_count(const pt_widget *table) {
    return table_column_count(table);
}

void pt_table_set_column_title(pt_widget *table, uint32_t column, const char *title) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count) {
        return;
    }
    free(state->column_titles[column]);
    state->column_titles[column] = pt_strdup(title ? title : "");
    uint32_t index = 0;
    for (pt_widget *header_cell = state->header->first_child; header_cell;
         header_cell = header_cell->next_sibling) {
        if (header_cell->kind != pt_widget_table_header_cell) {
            continue;
        }
        if (index == column) {
            free(header_cell->text);
            header_cell->text = pt_strdup(state->column_titles[column]);
            pt_widget_mark_dirty(table, false);
            pt_widget_a11y_emit(header_cell, pt_a11y_event_name_changed);
            break;
        }
        ++index;
    }
}

void pt_table_set_column_width(pt_widget *table, uint32_t column, float width) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count) {
        return;
    }
    state->column_widths[column] = pt_max_float(pt_table_min_width, width);
    uint32_t index = 0;
    for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
        if (cell->kind != pt_widget_table_header_cell) {
            continue;
        }
        if (index == column) {
            cell->requested_width = state->column_widths[column];
        }
        ++index;
    }
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        pt_widget *cell = table_cell_at(row, column);
        if (cell) {
            cell->requested_width = state->column_widths[column];
        }
    }
    pt_widget_mark_dirty(table, true);
}

float pt_table_get_column_width(const pt_widget *table, uint32_t column) {
    const pt_table_state *state = table_state_const(table);
    if (!state || column >= state->column_count) {
        return 0.0f;
    }
    return state->column_widths[column];
}

void pt_table_set_column_align(pt_widget *table, uint32_t column, pt_text_align align) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count) {
        return;
    }
    state->column_aligns[column] = align;
    uint32_t index = 0;
    for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
        if (cell->kind != pt_widget_table_header_cell) {
            continue;
        }
        if (index == column) {
            cell->text_align = align;
        }
        ++index;
    }
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        pt_widget *cell = table_cell_at(row, column);
        if (cell) {
            cell->text_align = align;
        }
    }
    pt_widget_mark_dirty(table, false);
}

void pt_table_set_column_resizable(pt_widget *table, uint32_t column, bool resizable) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count) {
        return;
    }
    state->column_resizable[column] = resizable;
}

void pt_table_set_column_hidden(pt_widget *table, uint32_t column, bool hidden) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count || state->column_hidden[column] == hidden) {
        return;
    }
    state->column_hidden[column] = hidden;
    uint32_t index = 0;
    for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
        if (cell->kind != pt_widget_table_header_cell) {
            continue;
        }
        if (index == column) {
            cell->visible = !hidden;
        }
        ++index;
    }
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        pt_widget *cell = table_cell_at(row, column);
        if (cell) {
            cell->visible = !hidden;
        }
    }
    float content = table_content_width(table);
    float viewport = pt_max_float(1.0f, table->width - table->padding_left -
                                           table->padding_right);
    if (table->scroll_x > pt_max_float(0.0f, content - viewport)) {
        table->scroll_x = pt_max_float(0.0f, content - viewport);
    }
    pt_widget_mark_dirty(table, true);
    pt_widget_a11y_emit(table, pt_a11y_event_structure_changed);
}

bool pt_table_get_column_hidden(const pt_widget *table, uint32_t column) {
    const pt_table_state *state = table_state_const(table);
    if (!state || column >= state->column_count) {
        return false;
    }
    return state->column_hidden[column];
}

void pt_table_set_row_count(pt_widget *table, uint32_t count) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    uint32_t old_selected = table->list_selected;
    bool keyed = table->row_key_callback && old_selected != pt_list_index_none;
    uint64_t key = keyed ? pt_row_key_at(table, old_selected) : pt_row_key_none;
    keyed = keyed && key != pt_row_key_none;
    table->row_count = count;
    if (keyed) {
        table->list_selected = pt_row_find_key(table, key);
        table->selected_key = key;
        table->selected_key_valid = table->list_selected != pt_list_index_none;
    } else {
        if (table->list_selected >= count) {
            table->list_selected = pt_list_index_none;
        }
        pt_data_view_sync_selected_key(table);
    }
    state->rebind = true;
    if (state->selection) {
        uint32_t selected = 0;
        uint32_t limit = state->selection_capacity < count ? state->selection_capacity : count;
        for (uint32_t i = 0; i < limit; ++i) {
            if (state->selection[i]) {
                ++selected;
            }
        }
        state->selection_count = selected;
        /* An anchor past the end would stretch the next range selection off the
         * table, exactly as in the list. */
        if (state->selection_anchor >= count) {
            state->selection_anchor = count > 0 ? 0u : pt_list_index_none;
        }
    }
    pt_widget_mark_dirty(table, true);
    pt_widget_a11y_emit(table, pt_a11y_event_structure_changed);
}

uint32_t pt_table_get_row_count(const pt_widget *table) {
    return table && table->kind == pt_widget_table ? table->row_count : 0;
}

void pt_table_set_row_height(pt_widget *table, float height) {
    if (!table || table->kind != pt_widget_table) {
        return;
    }
    table->row_height = pt_max_float(20.0f, height);
    pt_widget_mark_dirty(table, true);
}

float pt_table_get_row_height(const pt_widget *table) {
    return table && table->kind == pt_widget_table ? table->row_height : 0.0f;
}

void pt_table_set_header_height(pt_widget *table, float height) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->header_height = pt_max_float(0.0f, height);
    pt_widget_mark_dirty(table, true);
}

void pt_table_set_on_bind_cell(pt_widget *table, pt_table_bind_cell_callback callback,
                               void *user_data) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->on_bind_cell = callback;
    state->bind_data = user_data;
    state->rebind = true;
    pt_widget_mark_dirty(table, true);
}

void pt_table_set_on_select(pt_widget *table, pt_index_callback callback, void *user_data) {
    if (!table || table->kind != pt_widget_table) {
        return;
    }
    table->on_select = callback;
    table->on_select_data = user_data;
}

void pt_table_set_on_activate(pt_widget *table, pt_index_callback callback,
                              void *user_data) {
    if (!table || table->kind != pt_widget_table) {
        return;
    }
    table->on_activate = callback;
    table->on_activate_data = user_data;
}

void pt_table_set_on_sort(pt_widget *table, pt_table_sort_callback callback,
                          void *user_data) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->on_sort = callback;
    state->sort_data = user_data;
}

void pt_table_set_on_sort_columns(pt_widget *table,
                                  pt_table_sort_columns_callback callback,
                                  void *user_data) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->on_sort_columns = callback;
    state->sort_columns_data = user_data;
}

pt_sort_direction pt_table_get_sort(const pt_widget *table, uint32_t *column) {
    const pt_table_state *state = table_state_const(table);
    if (!state || state->sort_count == 0) {
        if (column) {
            *column = pt_table_column_none;
        }
        return pt_sort_none;
    }
    if (column) {
        *column = state->sort_columns[0];
    }
    return state->sort_directions[0];
}

void pt_table_set_sort(pt_widget *table, uint32_t column, pt_sort_direction direction) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    if (column >= state->column_count || direction == pt_sort_none) {
        state->sort_count = 0;
    } else {
        state->sort_columns[0] = column;
        state->sort_directions[0] = direction;
        state->sort_count = 1;
    }
    table_update_header_icons(table);
    pt_widget_mark_dirty(table, false);
}

void pt_table_set_sort_columns(pt_widget *table, uint32_t count, const uint32_t *columns,
                               const pt_sort_direction *directions) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    uint32_t next_columns[PT_TABLE_MAX_SORT_COLUMNS];
    pt_sort_direction next_directions[PT_TABLE_MAX_SORT_COLUMNS];
    uint32_t stored = 0;
    if (columns && directions) {
        for (uint32_t i = 0; i < count && stored < PT_TABLE_MAX_SORT_COLUMNS; ++i) {
            uint32_t column = columns[i];
            if (column >= state->column_count || directions[i] == pt_sort_none) {
                continue;
            }
            bool duplicate = false;
            for (uint32_t j = 0; j < stored; ++j) {
                if (next_columns[j] == column) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            next_columns[stored] = column;
            next_directions[stored] = directions[i];
            ++stored;
        }
    }
    for (uint32_t i = 0; i < stored; ++i) {
        state->sort_columns[i] = next_columns[i];
        state->sort_directions[i] = next_directions[i];
    }
    state->sort_count = stored;
    table_update_header_icons(table);
    pt_widget_mark_dirty(table, false);
    pt_widget_a11y_emit(table, pt_a11y_event_state_changed);
}

uint32_t pt_table_get_sort_columns(const pt_widget *table, uint32_t *columns,
                                   pt_sort_direction *directions) {
    const pt_table_state *state = table_state_const(table);
    if (!state) {
        return 0;
    }
    if (columns && directions) {
        for (uint32_t i = 0; i < state->sort_count; ++i) {
            columns[i] = state->sort_columns[i];
            directions[i] = state->sort_directions[i];
        }
    }
    return state->sort_count;
}

void pt_table_sort_changed(pt_widget *table, bool additive, uint32_t column) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count) {
        return;
    }
    if (additive) {
        int32_t index = table_sort_index(state, column);
        if (index < 0) {
            if (state->sort_count < PT_TABLE_MAX_SORT_COLUMNS) {
                state->sort_columns[state->sort_count] = column;
                state->sort_directions[state->sort_count] = pt_sort_ascending;
                ++state->sort_count;
            }
        } else if (state->sort_directions[index] == pt_sort_ascending) {
            state->sort_directions[index] = pt_sort_descending;
        } else {
            for (uint32_t i = (uint32_t)index + 1; i < state->sort_count; ++i) {
                state->sort_columns[i - 1] = state->sort_columns[i];
                state->sort_directions[i - 1] = state->sort_directions[i];
            }
            --state->sort_count;
        }
    } else if (state->sort_count == 1 && state->sort_columns[0] == column) {
        if (state->sort_directions[0] == pt_sort_ascending) {
            state->sort_directions[0] = pt_sort_descending;
        } else {
            state->sort_count = 0;
        }
    } else {
        state->sort_columns[0] = column;
        state->sort_directions[0] = pt_sort_ascending;
        state->sort_count = 1;
    }
    table_update_header_icons(table);
    pt_widget_mark_dirty(table, false);
    pt_widget_a11y_emit(table, pt_a11y_event_state_changed);
    if (state->on_sort_columns) {
        state->on_sort_columns(table, state->sort_count, state->sort_columns,
                               state->sort_directions, state->sort_columns_data);
    }
    if (state->on_sort) {
        state->on_sort(table, column, table_sort_direction_for(state, column),
                       state->sort_data);
    }
}

uint32_t pt_table_get_selected(const pt_widget *table) {
    return table && table->kind == pt_widget_table ? table->list_selected
                                                   : pt_list_index_none;
}

void pt_table_select(pt_widget *table, uint32_t row, bool notify) {
    if (!table || table->kind != pt_widget_table) {
        return;
    }
    pt_table_state *state = table_state(table);
    if (row >= table->row_count) {
        row = pt_list_index_none;
    }
    bool changed = table->list_selected != row;
    table->list_selected = row;
    if (state) {
        table_selection_ensure(table, state);
        table_selection_reset(state);
        if (row != pt_list_index_none) {
            table_selection_add(state, row);
            state->selection_anchor = row;
        }
    }
    pt_data_view_sync_selected_key(table);
    if (state) {
        table_selection_apply(table, state);
    }
    if (row != pt_list_index_none) {
        pt_table_reveal_row(table, row);
    }
    pt_widget_mark_dirty(table, false);
    if (changed) {
        pt_widget_a11y_emit(table, pt_a11y_event_selection_changed);
        if (notify && row != pt_list_index_none && table->on_select) {
            table->on_select(table, row, table->on_select_data);
        }
    }
}

void pt_table_set_selected(pt_widget *table, uint32_t row) {
    pt_table_select(table, row, false);
}

uint32_t pt_table_get_selection_count(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    return state ? state->selection_count : 0;
}

bool pt_table_is_row_selected(const pt_widget *table, uint32_t row) {
    const pt_table_state *state = table_state_const(table);
    if (!table || table->kind != pt_widget_table || row >= table->row_count) {
        return false;
    }
    if (!state) {
        return row == table->list_selected;
    }
    return table_row_is_selected(table, state, row);
}

uint32_t pt_table_get_selection(const pt_widget *table, uint32_t *rows, uint32_t capacity) {
    const pt_table_state *state = table_state_const(table);
    if (!state || !rows || capacity == 0) {
        return 0;
    }
    uint32_t written = 0;
    uint32_t limit = state->selection_capacity < table->row_count ? state->selection_capacity
                                                                  : table->row_count;
    for (uint32_t i = 0; i < limit && written < capacity; ++i) {
        if (state->selection[i]) {
            rows[written++] = i;
        }
    }
    return written;
}

void pt_table_select_range(pt_widget *table, uint32_t first, uint32_t last) {
    pt_table_state *state = table_state(table);
    if (!state || table->row_count == 0) {
        return;
    }
    if (first > last) {
        uint32_t swap = first;
        first = last;
        last = swap;
    }
    if (first >= table->row_count) {
        first = table->row_count - 1;
    }
    if (last >= table->row_count) {
        last = table->row_count - 1;
    }
    table_selection_ensure(table, state);
    table_selection_reset(state);
    for (uint32_t i = first; i <= last; ++i) {
        table_selection_add(state, i);
    }
    state->selection_anchor = first;
    table->list_selected = first;
    pt_data_view_sync_selected_key(table);
    table_selection_apply(table, state);
    pt_table_reveal_row(table, first);
    pt_widget_mark_dirty(table, false);
    pt_widget_a11y_emit(table, pt_a11y_event_selection_changed);
}

void pt_table_select_toggle(pt_widget *table, uint32_t row) {
    pt_table_state *state = table_state(table);
    if (!state || row >= table->row_count) {
        return;
    }
    table_selection_ensure(table, state);
    bool was_selected = table_row_is_selected(table, state, row);
    if (was_selected) {
        table_selection_drop(state, row);
        if (table->list_selected == row) {
            /* The primary row has to name a row that is still selected, because
             * pt_table_is_row_selected treats it as selected even when the bit
             * is clear. */
            table->list_selected = pt_list_index_none;
            uint32_t limit = state->selection_capacity < table->row_count
                               ? state->selection_capacity
                               : table->row_count;
            for (uint32_t i = 0; i < limit; ++i) {
                if (state->selection[i]) {
                    table->list_selected = i;
                    break;
                }
            }
        }
    } else {
        table_selection_add(state, row);
        table->list_selected = row;
        pt_table_reveal_row(table, row);
    }
    state->selection_anchor = row;
    pt_data_view_sync_selected_key(table);
    table_selection_apply(table, state);
    pt_widget_mark_dirty(table, false);
    pt_widget_a11y_emit(table, pt_a11y_event_selection_changed);
}

void pt_table_clear_selection(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    table_selection_reset(state);
    state->selection_anchor = pt_list_index_none;
    table->list_selected = pt_list_index_none;
    pt_data_view_sync_selected_key(table);
    table_selection_apply(table, state);
    pt_widget_mark_dirty(table, false);
    pt_widget_a11y_emit(table, pt_a11y_event_selection_changed);
}

/* ---------- cell editing ---------- */

void pt_table_set_editable(pt_widget *table, bool editable) {
    pt_table_state *state = table_state(table);
    if (!state || state->editable == editable) {
        return;
    }
    state->editable = editable;
    if (!editable) {
        pt_table_cancel_edit(table);
    }
}

bool pt_table_get_editable(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    return state && state->editable;
}

bool pt_table_begin_edit(pt_widget *table, uint32_t row, uint32_t column) {
    pt_table_state *state = table_state(table);
    if (!state || !state->editable || row >= table->row_count ||
        column >= state->column_count || table_column_hidden(state, column)) {
        return false;
    }
    pt_table_cancel_edit(table);
    pt_table_reveal_row(table, row);
    pt_widget *cell = pt_table_row_cell(table, row, column);
    char *initial = pt_strdup(cell && cell->text ? cell->text : "");
    pt_widget *editor = pt_widget_new(table, pt_widget_entry);
    editor->editor_owner = table;
    editor->text = initial;
    editor->requested_width = pt_auto;
    editor->requested_height = pt_auto;
    if (state->column_titles && state->column_titles[column] &&
        state->column_titles[column][0]) {
        editor->a11y_name = pt_strdup(state->column_titles[column]);
    }
    state->editor = editor;
    state->editing_row = (int32_t)row;
    state->editing_column = (int32_t)column;
    pt_widget_focus(editor);
    pt_widget_text_select_all(editor);
    pt_widget_mark_dirty(table, true);
    return true;
}

static void table_editor_detach(pt_widget *table, pt_table_state *state) {
    pt_widget *editor = state->editor;
    if (!editor) {
        return;
    }
    state->editor = NULL;
    state->editing_row = -1;
    state->editing_column = -1;
    editor->editor_owner = NULL;
    pt_window *window = pt_widget_window(table);
    if (window && window->focused == editor) {
        editor->focused = false;
        window->focused = NULL;
        pt_platform_ime_set_enabled(window, false);
    }
    pt_widget_destroy(editor);
}

bool pt_table_commit_edit(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state || !state->editor) {
        return false;
    }
    pt_widget *editor = state->editor;
    uint32_t row = (uint32_t)state->editing_row;
    uint32_t column = (uint32_t)state->editing_column;
    char *text = pt_strdup(editor->text ? editor->text : "");
    table_editor_detach(table, state);
    pt_widget *cell = pt_table_row_cell(table, row, column);
    if (cell) {
        free(cell->text);
        cell->text = pt_strdup(text);
    }
    pt_widget_mark_dirty(table, true);
    if (state->on_cell_edited) {
        state->on_cell_edited(table, row, column, text, state->cell_edited_data);
    }
    pt_widget_a11y_emit(table, pt_a11y_event_value_changed);
    free(text);
    return true;
}

void pt_table_cancel_edit(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state || !state->editor) {
        return;
    }
    table_editor_detach(table, state);
    pt_widget_mark_dirty(table, true);
}

bool pt_table_is_editing(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    return state && state->editor != NULL;
}

void pt_table_set_on_cell_edited(pt_widget *table, pt_table_cell_edited_callback callback,
                                 void *user_data) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->on_cell_edited = callback;
    state->cell_edited_data = user_data;
}

/* ---------- column reordering and row dragging ---------- */

void pt_table_set_columns_reorderable(pt_widget *table, bool reorderable) {
    pt_table_state *state = table_state(table);
    if (state) {
        state->columns_reorderable = reorderable;
    }
}

bool pt_table_get_columns_reorderable(const pt_widget *table) {
    const pt_table_state *state = table_state_const(table);
    return state && state->columns_reorderable;
}

static void swap_columns(pt_table_state *state, uint32_t a, uint32_t b) {
    if (a == b) {
        return;
    }
    float width = state->column_widths[a];
    state->column_widths[a] = state->column_widths[b];
    state->column_widths[b] = width;
    char *title = state->column_titles[a];
    state->column_titles[a] = state->column_titles[b];
    state->column_titles[b] = title;
    pt_text_align align = state->column_aligns[a];
    state->column_aligns[a] = state->column_aligns[b];
    state->column_aligns[b] = align;
    bool resizable = state->column_resizable[a];
    state->column_resizable[a] = state->column_resizable[b];
    state->column_resizable[b] = resizable;
    bool hidden = state->column_hidden[a];
    state->column_hidden[a] = state->column_hidden[b];
    state->column_hidden[b] = hidden;
}

bool pt_table_move_column(pt_widget *table, uint32_t from, uint32_t to) {
    pt_table_state *state = table_state(table);
    if (!state || from >= state->column_count) {
        return false;
    }
    uint32_t last = state->column_count - 1;
    if (to > last) {
        to = last;
    }
    if (from == to) {
        return false;
    }
    if (from < to) {
        for (uint32_t i = from; i < to; ++i) {
            swap_columns(state, i, i + 1);
        }
    } else {
        for (uint32_t i = from; i > to; --i) {
            swap_columns(state, i, i - 1);
        }
    }
    for (uint32_t i = 0; i < state->sort_count; ++i) {
        uint32_t column = state->sort_columns[i];
        if (column == from) {
            state->sort_columns[i] = to;
        } else if (from < to && column > from && column <= to) {
            state->sort_columns[i] = column - 1;
        } else if (to < from && column >= to && column < from) {
            state->sort_columns[i] = column + 1;
        }
    }
    uint32_t count = state->column_count;
    pt_table_set_column_count(table, count);
    if (state->on_columns_reordered) {
        state->on_columns_reordered(table, from, to, state->columns_reordered_data);
    }
    return true;
}

void pt_table_set_on_columns_reordered(pt_widget *table,
                                       pt_table_columns_reordered_callback callback,
                                       void *user_data) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->on_columns_reordered = callback;
    state->columns_reordered_data = user_data;
}

void pt_table_set_on_row_drag(pt_widget *table, pt_table_row_drag_callback callback,
                              void *user_data) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->on_row_drag = callback;
    state->row_drag_data = user_data;
}

static pt_widget *table_header_cell_for(const pt_table_state *state, uint32_t column) {
    if (!state || !state->header) {
        return NULL;
    }
    for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
        if (cell->kind == pt_widget_table_header_cell && cell->column_index == column) {
            return cell;
        }
    }
    return NULL;
}

void pt_table_row_down(pt_widget *table, pt_widget *row, float x, float y) {
    pt_table_state *state = table_state(table);
    if (!state || !row || row->list_index >= table->row_count) {
        return;
    }
    uint32_t index = row->list_index;
    uint32_t previous = table->list_selected;
    pt_window *window = pt_widget_window(table);
    uint32_t modifiers = window ? window->modifiers : 0;
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool ctrl = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    uint32_t previous_anchor = state->selection_anchor;
    state->shift_select = shift;
    state->toggle_select = ctrl;
    if (shift) {
        uint32_t anchor = previous_anchor < table->row_count ? previous_anchor : index;
        pt_table_select_range(table, anchor, index);
        table->list_selected = index;
        /* The range helper anchors on the row it started from; the gesture is the
         * one that decides, so the anchor goes back where the sweep began. */
        state->selection_anchor = anchor;
        pt_data_view_sync_selected_key(table);
        table_selection_apply(table, state);
    } else if (ctrl) {
        pt_table_select_toggle(table, index);
    } else {
        state->selection_anchor = index;
    }
    pt_marquee_begin(&state->marquee, x, y);
    state->row_drag_active = false;
    state->row_drag_start_y = y;
    state->dragged_row = (int32_t)index;
    pt_widget_mark_dirty(table, false);
    table_notify_primary(table, previous);
}

/* The row a pointer y falls inside, counted from the first model row rather than
 * the visible pool, so a reordering drag keeps reporting a stable destination even
 * while rows recycle during the drag. */
static uint32_t table_row_at_y(const pt_widget *table, const pt_table_state *state, float y) {
    if (table->row_count == 0) {
        return pt_list_index_none;
    }
    float row_height = pt_max_float(20.0f, table->row_height);
    float top = table->y + table->padding_top + state->header_height;
    int32_t index = (int32_t)((y - top + table->scroll_y) / row_height);
    if (index < 0) {
        index = 0;
    }
    if (index >= (int32_t)table->row_count) {
        index = (int32_t)table->row_count - 1;
    }
    return (uint32_t)index;
}

void pt_table_row_drag(pt_widget *table, float x, float y) {
    pt_table_state *state = table_state(table);
    if (!state || !state->marquee.pressed) {
        return;
    }
    if (pt_marquee_move(&state->marquee, x, y)) {
        table_apply_marquee(table, state);
    }
    if (fabsf(y - state->row_drag_start_y) > 4.0f) {
        state->row_drag_active = true;
        uint32_t index = table_row_at_y(table, state, y);
        state->dragged_row = (int32_t)index;
    }
    pt_widget_mark_dirty(table, false);
}

bool pt_table_row_up(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return false;
    }
    bool dragged = pt_marquee_end(&state->marquee);
    bool consumed = dragged || state->row_drag_active || state->shift_select ||
                    state->toggle_select;
    if (state->row_drag_active && state->on_row_drag && state->dragged_row >= 0) {
        state->on_row_drag(table, (uint32_t)state->dragged_row, state->row_drag_data);
    }
    state->row_drag_active = false;
    state->dragged_row = -1;
    state->shift_select = false;
    state->toggle_select = false;
    pt_widget_mark_dirty(table, false);
    return consumed;
}

/* A press on the viewport background starts a band instead of a row click. The
 * header is excluded even where no header cell covers it, so a drag there never
 * turns into a selection. */
static bool table_view_background(const pt_widget *table, const pt_table_state *state,
                                  float x, float y) {
    if (!table || !state || x < table->x + table->padding_left ||
        x > table->x + table->width - table->padding_right ||
        y < table->y + table->padding_top + state->header_height ||
        y > table->y + table->height - table->padding_bottom) {
        return false;
    }
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        if (x >= child->x && x <= child->x + child->width && y >= child->y &&
            y <= child->y + child->height) {
            return false;
        }
    }
    return true;
}

void pt_table_view_down(pt_widget *table, float x, float y) {
    pt_table_state *state = table_state(table);
    if (!state || !table->enabled || !table->visible ||
        !table_view_background(table, state, x, y)) {
        return;
    }
    pt_window *window = pt_widget_window(table);
    uint32_t modifiers = window ? window->modifiers : 0;
    bool additive = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    if (!additive) {
        pt_table_clear_selection(table);
    }
    state->shift_select = false;
    state->toggle_select = false;
    state->row_drag_active = false;
    state->dragged_row = -1;
    pt_marquee_begin(&state->marquee, x, y);
    pt_widget_mark_dirty(table, false);
}

void pt_table_view_drag(pt_widget *table, float x, float y) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    if (pt_marquee_move(&state->marquee, x, y)) {
        table_apply_marquee(table, state);
    }
    pt_widget_mark_dirty(table, false);
}

bool pt_table_view_up(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return false;
    }
    bool dragged = pt_marquee_end(&state->marquee);
    state->row_drag_active = false;
    state->dragged_row = -1;
    state->shift_select = false;
    state->toggle_select = false;
    pt_widget_mark_dirty(table, false);
    return dragged;
}

void pt_table_set_row_key_callback(pt_widget *table, pt_row_key_callback callback,
                                   void *user_data) {
    if (!table || table->kind != pt_widget_table) {
        return;
    }
    table->row_key_callback = callback;
    table->row_key_data = user_data;
    pt_data_view_sync_selected_key(table);
}

uint64_t pt_table_get_selected_key(const pt_widget *table) {
    if (!table || table->kind != pt_widget_table || !table->row_key_callback ||
        table->list_selected == pt_list_index_none) {
        return pt_row_key_none;
    }
    if (table->selected_key_valid) {
        return table->selected_key;
    }
    return pt_row_key_at(table, table->list_selected);
}

bool pt_table_set_selected_key(pt_widget *table, uint64_t key) {
    if (!table || table->kind != pt_widget_table) {
        return false;
    }
    if (key == pt_row_key_none) {
        pt_table_select(table, pt_list_index_none, false);
        return true;
    }
    uint32_t row = pt_row_find_key(table, key);
    if (row == pt_list_index_none) {
        return false;
    }
    pt_table_select(table, row, false);
    return true;
}

void pt_table_set_row_text_callback(pt_widget *table, pt_row_text_callback callback,
                                    void *user_data) {
    if (!table || table->kind != pt_widget_table) {
        return;
    }
    table->row_text_callback = callback;
    table->row_text_data = user_data;
}

void pt_table_reveal_row(pt_widget *table, uint32_t row) {
    pt_table_state *state = table_state(table);
    if (!state || row >= table->row_count) {
        return;
    }
    float row_height = pt_max_float(1.0f, table->row_height);
    float viewport = pt_max_float(1.0f, table->height - table->padding_top -
                                           table->padding_bottom - state->header_height);
    float top = (float)row * row_height;
    float bottom = top + row_height;
    if (top < table->scroll_y) {
        table->scroll_y = top;
    } else if (bottom > table->scroll_y + viewport) {
        table->scroll_y = bottom - viewport;
    }
    float max_scroll = pt_max_float(0.0f, (float)table->row_count * row_height - viewport);
    table->scroll_y = pt_clamp_float(table->scroll_y, 0.0f, max_scroll);
    pt_widget_mark_dirty(table, true);
}

void pt_table_reveal_column(pt_widget *table, uint32_t column) {
    pt_table_state *state = table_state(table);
    if (!state || column >= state->column_count || table_column_hidden(state, column)) {
        return;
    }
    float left = table_column_left(table, column);
    float right = left + state->column_widths[column];
    float viewport = pt_max_float(1.0f, table->width - table->padding_left -
                                           table->padding_right);
    if (left < table->scroll_x) {
        table->scroll_x = left;
    } else if (right > table->scroll_x + viewport) {
        table->scroll_x = right - viewport;
    }
    float max_scroll = pt_max_float(0.0f, table_content_width(table) - viewport);
    table->scroll_x = pt_clamp_float(table->scroll_x, 0.0f, max_scroll);
    pt_widget_mark_dirty(table, true);
}

void pt_table_scroll_to_row(pt_widget *table, uint32_t row) {
    pt_table_reveal_row(table, row);
}

void pt_table_scroll_to_column(pt_widget *table, uint32_t column) {
    pt_table_reveal_column(table, column);
}

bool pt_table_scroll(pt_widget *table, float pixels) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return false;
    }
    float viewport = pt_max_float(1.0f, table->height - table->padding_top -
                                           table->padding_bottom - state->header_height);
    float content = (float)table->row_count * pt_max_float(20.0f, table->row_height);
    float max_scroll = pt_max_float(0.0f, content - viewport);
    float next = pt_clamp_float(table->scroll_y + pixels, 0.0f, max_scroll);
    if (next == table->scroll_y) {
        return false;
    }
    table->scroll_y = next;
    pt_widget_mark_dirty(table, true);
    return true;
}

bool pt_table_scroll_horizontal(pt_widget *table, float pixels) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return false;
    }
    float viewport = pt_max_float(1.0f, table->width - table->padding_left -
                                           table->padding_right);
    float max_scroll = pt_max_float(0.0f, table_content_width(table) - viewport);
    float next = pt_clamp_float(table->scroll_x + pixels, 0.0f, max_scroll);
    if (next == table->scroll_x) {
        return false;
    }
    table->scroll_x = next;
    pt_widget_mark_dirty(table, true);
    return true;
}

void pt_table_refresh(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    if (table->row_key_callback && table->list_selected != pt_list_index_none) {
        uint64_t key = table->selected_key_valid
                           ? table->selected_key
                           : pt_row_key_at(table, table->list_selected);
        if (key != pt_row_key_none) {
            table->list_selected = pt_row_find_key(table, key);
            table->selected_key = key;
            table->selected_key_valid = table->list_selected != pt_list_index_none;
        } else {
            table->selected_key_valid = false;
        }
    }
    state->rebind = true;
    pt_widget_mark_dirty(table, true);
}

void pt_table_refresh_rows(pt_widget *table, uint32_t first, uint32_t count) {
    pt_table_state *state = table_state(table);
    if (!state || count == 0 || first >= table->row_count) {
        return;
    }
    uint64_t end = (uint64_t)first + count;
    if (end > table->row_count) {
        end = table->row_count;
    }
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        if (row->kind == pt_widget_table_row && row->list_bound &&
            row->list_index >= first && row->list_index < end) {
            row->list_bound = false;
        }
    }
    pt_widget_mark_dirty(table, true);
}

bool pt_table_activate_selected(pt_widget *table) {
    if (!table || table->kind != pt_widget_table ||
        table->list_selected >= table->row_count) {
        return false;
    }
    if (table->on_activate) {
        table->on_activate(table, table->list_selected, table->on_activate_data);
        return true;
    }
    return false;
}

bool pt_table_key(pt_widget *table, uint32_t keysym, uint32_t modifiers) {
    pt_table_state *state = table_state(table);
    if (!state || table->row_count == 0) {
        return false;
    }
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    if ((modifiers & (uint32_t)pt_key_modifier_ctrl) != 0 &&
        (keysym == XKB_KEY_a || keysym == XKB_KEY_A)) {
        uint32_t previous = table->list_selected;
        pt_table_select_range(table, 0, table->row_count - 1);
        table_notify_primary(table, previous);
        return true;
    }
    uint32_t current = table->list_selected;
    uint32_t next = current;
    float row_height = pt_max_float(1.0f, table->row_height);
    float viewport = pt_max_float(1.0f, table->height - table->padding_top -
                                           table->padding_bottom - state->header_height);
    uint32_t page = (uint32_t)pt_max_float(1.0f, viewport / row_height);
    if (current == pt_list_index_none) {
        if (keysym == XKB_KEY_Down || keysym == XKB_KEY_Page_Down ||
            keysym == XKB_KEY_Home) {
            next = 0;
        } else if (keysym == XKB_KEY_Up || keysym == XKB_KEY_Page_Up ||
                   keysym == XKB_KEY_End) {
            next = table->row_count - 1;
        } else {
            return false;
        }
    } else {
        switch (keysym) {
        case XKB_KEY_F2: {
            if (!state->editable || current >= table->row_count) {
                return false;
            }
            uint32_t column = 0;
            while (column < state->column_count && table_column_hidden(state, column)) {
                ++column;
            }
            if (column >= state->column_count) {
                return false;
            }
            return pt_table_begin_edit(table, current, column);
        }
        case XKB_KEY_Down:
            next = current + 1 < table->row_count ? current + 1 : current;
            break;
        case XKB_KEY_Up:
            next = current > 0 ? current - 1 : 0;
            break;
        case XKB_KEY_Page_Down:
            next = current + page < table->row_count ? current + page
                                                     : table->row_count - 1;
            break;
        case XKB_KEY_Page_Up:
            next = current > page ? current - page : 0;
            break;
        case XKB_KEY_Home:
            next = 0;
            break;
        case XKB_KEY_End:
            next = table->row_count - 1;
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            return pt_table_activate_selected(table);
        default:
            return false;
        }
    }
    if (next == current) {
        return true;
    }
    uint32_t anchor = state->selection_anchor < table->row_count &&
                              current != pt_list_index_none
                          ? state->selection_anchor
                          : next;
    if (shift && current != pt_list_index_none) {
        /* The anchor stays put so repeated Shift+ arrows extend and shrink the
         * same range instead of chasing the caret. */
        pt_table_select_range(table, anchor, next);
        table->list_selected = next;
        pt_data_view_sync_selected_key(table);
        table_selection_apply(table, state);
        pt_table_reveal_row(table, next);
        state->selection_anchor = anchor;
        pt_widget_mark_dirty(table, false);
        table_notify_primary(table, current);
    } else {
        pt_table_select(table, next, true);
        state->selection_anchor = next;
    }
    return true;
}

void pt_table_header_down(pt_widget *table, pt_widget *cell, float x) {
    pt_table_state *state = table_state(table);
    if (!state || !cell || cell->kind != pt_widget_table_header_cell ||
        cell->column_index >= state->column_count) {
        return;
    }
    uint32_t column = cell->column_index;
    float edge = cell->x + cell->width;
    if (state->column_resizable[column] && x >= edge - 6.0f && x <= edge + 2.0f) {
        state->resizing_column = (int32_t)column;
        state->resize_start_x = x;
        state->resize_start_width = state->column_widths[column];
        cell->pressed = true;
        return;
    }
    pt_window *window = pt_widget_window(table);
    state->sort_pending = true;
    state->sort_pending_column = column;
    state->sort_pending_additive =
        window && (window->modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    if (state->columns_reorderable) {
        state->reorder_column = (int32_t)column;
        state->reorder_target = (int32_t)column;
        state->reorder_active = false;
    }
    cell->pressed = true;
}

void pt_table_pointer_drag(pt_widget *table, float x) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    if (state->resizing_column >= 0 &&
        (uint32_t)state->resizing_column < state->column_count) {
        float width = state->resize_start_width + (x - state->resize_start_x);
        pt_table_set_column_width(table, (uint32_t)state->resizing_column, width);
        return;
    }
    if (state->reorder_column < 0) {
        return;
    }
    pt_widget *cell = table_header_cell_for(state, (uint32_t)state->reorder_column);
    if (!cell) {
        return;
    }
    if (!state->reorder_active &&
        (x < cell->x + 4.0f || x > cell->x + cell->width - 4.0f)) {
        state->reorder_active = true;
    }
    if (!state->reorder_active) {
        return;
    }
    int32_t target = 0;
    for (pt_widget *header_cell = state->header->first_child; header_cell;
         header_cell = header_cell->next_sibling) {
        if (header_cell->kind != pt_widget_table_header_cell || !header_cell->visible) {
            continue;
        }
        if (x > header_cell->x + header_cell->width * 0.5f) {
            ++target;
        }
    }
    if (target != state->reorder_target) {
        state->reorder_target = target;
        pt_widget_mark_dirty(table, false);
    }
}

void pt_table_pointer_up(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    state->resizing_column = -1;
    if (state->reorder_active && state->reorder_column >= 0) {
        uint32_t from = (uint32_t)state->reorder_column;
        uint32_t to = state->reorder_target < 0 ? from : (uint32_t)state->reorder_target;
        state->reorder_column = -1;
        state->reorder_target = -1;
        state->reorder_active = false;
        state->sort_pending = false;
        pt_table_move_column(table, from, to);
    } else if (state->sort_pending) {
        uint32_t column = state->sort_pending_column;
        bool additive = state->sort_pending_additive;
        state->sort_pending = false;
        state->reorder_column = -1;
        state->reorder_target = -1;
        pt_table_sort_changed(table, additive, column);
    }
    if (state->header) {
        for (pt_widget *cell = state->header->first_child; cell;
             cell = cell->next_sibling) {
            cell->pressed = false;
        }
    }
    pt_widget_mark_dirty(table, false);
}

pt_cursor pt_table_cursor(const pt_widget *table, float x, float y) {
    const pt_table_state *state = table_state_const(table);
    if (!state) {
        return pt_cursor_default;
    }
    if (state->resizing_column >= 0) {
        return pt_cursor_resize_h;
    }
    float header_top = table->y + table->padding_top;
    float header_bottom = header_top + state->header_height;
    if (y < header_top || y > header_bottom) {
        return pt_cursor_default;
    }
    float left = table->x + table->padding_left - table->scroll_x;
    for (uint32_t i = 0; i < state->column_count; ++i) {
        if (table_column_hidden(state, i)) {
            continue;
        }
        left += state->column_widths[i];
        if (state->column_resizable[i] && fabsf(x - left) <= 6.0f) {
            return pt_cursor_resize_h;
        }
    }
    return pt_cursor_default;
}

void pt_table_free_extra(pt_widget *table) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    for (uint32_t i = 0; i < state->column_count; ++i) {
        free(state->column_titles[i]);
    }
    free(state->column_widths);
    free(state->column_titles);
    free(state->column_aligns);
    free(state->column_resizable);
    free(state->column_hidden);
    free(state->selection);
    free(state);
    table->extra = NULL;
}

pt_widget *pt_table_row_cell(pt_widget *table, uint32_t row_index, uint32_t column) {
    pt_widget *row = table_row_at(table, row_index);
    return row ? table_cell_at(row, column) : NULL;
}

/* ---------- layout ---------- */

void pt_table_preferred(pt_widget *table, const pt_layout_ctx *ctx, float *width,
                        float *height) {
    pt_unused(ctx);
    pt_table_state *state = table_state(table);
    float content_w = table_content_width(table);
    if (content_w <= 0.0f) {
        content_w = 360.0f;
    }
    float rows = (float)(table->row_count > 8 ? 8 : table->row_count);
    float preferred_h = (state ? state->header_height : 38.0f) +
                        pt_max_float(3.0f, rows) * pt_max_float(20.0f, table->row_height);
    *width = table->requested_width >= 0.0f ? table->requested_width : content_w;
    *height = table->requested_height >= 0.0f ? table->requested_height : preferred_h;
}

static void table_bind_row(pt_widget *table, pt_widget *row, uint32_t index) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    row->list_index = index;
    row->list_bound = true;
    row->row_selected = table_row_is_selected(table, state, index);
    for (uint32_t column = 0; column < state->column_count; ++column) {
        pt_widget *cell = table_cell_at(row, column);
        if (!cell) {
            continue;
        }
        cell->column_index = column;
        cell->requested_width = state->column_widths[column];
        cell->text_align = state->column_aligns[column];
        cell->visible = !table_column_hidden(state, column);
        free(cell->text);
        cell->text = NULL;
        cell->icon = pt_icon_none;
        cell->has_background = false;
        cell->has_foreground = false;
        cell->has_font_weight = false;
        cell->has_font_size = false;
        if (state->on_bind_cell) {
            state->on_bind_cell(table, cell, index, column, state->bind_data);
        }
    }
}

void pt_table_layout(pt_widget *table, const pt_layout_ctx *ctx, pt_rect inner) {
    pt_table_state *state = table_state(table);
    if (!state) {
        return;
    }
    pt_rect inherited = table->clip;
    float row_height = pt_max_float(20.0f, table->row_height);
    float content_w = pt_max_float(inner.w, table_content_width(table));
    float content_h = (float)table->row_count * row_height;
    table->scroll_content_height = content_h;
    bool has_columns = table_visible_column_count(table) > 0;
    float header_h = has_columns ? state->header_height : 0.0f;
    float rows_h = pt_max_float(0.0f, inner.h - header_h);
    float max_scroll_y = pt_max_float(0.0f, content_h - rows_h);
    float max_scroll_x = pt_max_float(0.0f, content_w - inner.w);
    table->scroll_y = pt_clamp_float(table->scroll_y, 0.0f, max_scroll_y);
    table->scroll_x = pt_clamp_float(table->scroll_x, 0.0f, max_scroll_x);

    pt_rect header_area = {inner.x, inner.y, inner.w, header_h};
    pt_rect rows_area = {inner.x, inner.y + header_h, inner.w, rows_h};
    table->clip = pt_rect_intersect(inherited, header_area);
    state->header->visible = has_columns;
    if (state->header->visible) {
        pt_widget_layout_child(state->header, inner.x - table->scroll_x, inner.y,
                               content_w, header_h, ctx);
        state->header->clip = table->clip;
        uint32_t column = 0;
        for (pt_widget *cell = state->header->first_child; cell;
             cell = cell->next_sibling) {
            if (cell->kind == pt_widget_table_header_cell) {
                cell->column_index = column++;
            }
        }
    }

    uint32_t first = row_height > 0.0f ? (uint32_t)(table->scroll_y / row_height) : 0;
    if (first > table->row_count) {
        first = table->row_count;
    }
    uint32_t needed = 0;
    if (first < table->row_count && rows_h > 0.0f) {
        float visible = (table->scroll_y + rows_h) / row_height - (float)first;
        needed = (uint32_t)ceilf(pt_max_float(0.0f, visible)) + 1u;
    }
    if (needed > table->row_count - first) {
        needed = table->row_count - first;
    }
    uint32_t existing = 0;
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        if (row->kind == pt_widget_table_row) {
            ++existing;
        }
    }
    while (existing < needed) {
        pt_widget *row = pt_widget_new(table, pt_widget_table_row);
        table_sync_row_cells(table, row);
        ++existing;
    }
    table->clip = pt_rect_intersect(inherited, rows_area);
    uint32_t index = 0;
    for (pt_widget *row = state->header->next_sibling; row; row = row->next_sibling) {
        if (row->kind != pt_widget_table_row) {
            continue;
        }
        if (index >= needed) {
            row->visible = false;
            row->list_bound = false;
            row->list_index = pt_list_index_none;
            ++index;
            continue;
        }
        uint32_t row_index = first + (uint32_t)index;
        row->visible = true;
        if (state->rebind || !row->list_bound || row->list_index != row_index) {
            table_bind_row(table, row, row_index);
        }
        row->row_selected = table_row_is_selected(table, state, row_index);
        float row_y = rows_area.y - table->scroll_y + (float)row_index * row_height;
        pt_widget_layout_child(row, inner.x - table->scroll_x, row_y, content_w,
                               row_height, ctx);
        row->clip = table->clip;
        ++index;
    }
    if (state->editor) {
        if (state->editing_row < 0 || state->editing_column < 0) {
            state->editor->visible = false;
        } else {
            pt_widget *row = table_row_widget(table, (uint32_t)state->editing_row);
            pt_widget *cell = row && row->visible
                                  ? table_cell_at(row, (uint32_t)state->editing_column)
                                  : NULL;
            if (!cell || !cell->visible) {
                state->editor->visible = false;
            } else {
                state->editor->visible = true;
                pt_widget_layout_child(state->editor, cell->x, cell->y, cell->width,
                                       cell->height, ctx);
                state->editor->clip = table->clip;
            }
        }
    }
    state->rebind = false;
    table->clip = pt_rect_intersect(inherited, inner);
}
