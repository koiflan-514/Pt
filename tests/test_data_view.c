/*
 * Virtualized list checks: recycling, scrolling, selection and a11y roles.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct list_state {
    int binds;
    int selects;
    int activates;
    uint32_t last_bind;
    uint32_t last_select;
    uint32_t last_activate;
} list_state;

static void bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    list_state *state = data;
    char label[64];
    snprintf(label, sizeof(label), "Item %u", index);
    pt_widget_set_text(row, label);
    state->binds++;
    state->last_bind = index;
    (void)list;
}

static void select_row(pt_widget *list, uint32_t index, void *data) {
    list_state *state = data;
    state->selects++;
    state->last_select = index;
    (void)list;
}

static void activate_row(pt_widget *list, uint32_t index, void *data) {
    list_state *state = data;
    state->activates++;
    state->last_activate = index;
    (void)list;
}

static uint64_t row_keys[64];
static const char *row_names[] = {"Alpha", "Beta", "Charlie", "Delta", "Echo", "Foxtrot"};

static uint64_t list_row_key(pt_widget *list, uint32_t row, void *data) {
    (void)list;
    (void)data;
    return row < 64 ? row_keys[row] : pt_row_key_none;
}

static const char *list_row_text(pt_widget *list, uint32_t row, void *data) {
    (void)list;
    (void)data;
    return row_names[row % (sizeof(row_names) / sizeof(row_names[0]))];
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static pt_widget *visible_row_at(pt_widget *list, uint32_t index) {
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        if (row->visible && row->list_bound && row->list_index == index) {
            return row;
        }
    }
    return NULL;
}

static int visible_rows(pt_widget *list) {
    int count = 0;
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        if (row->visible) {
            ++count;
        }
    }
    return count;
}

static int materialized_rows(pt_widget *list) {
    int count = 0;
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        ++count;
    }
    return count;
}

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 0);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 0);
}

static void test_virtualization_and_selection(pt_app *app) {
    pt_window *window = pt_window_create(app, "list", 400, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *list = pt_list_view_create(root);
    pt_check(list != NULL);
    pt_widget_set_size(list, 320.0f, 200.0f);
    pt_widget_set_align_self(list, pt_align_start);
    pt_check_near(pt_list_view_get_row_height(list), 44.0, 0.01);
    pt_list_view_set_row_height(list, 40.0f);
    pt_check_near(pt_list_view_get_row_height(list), 40.0, 0.01);
    pt_list_view_set_row_count(list, 100);
    pt_check(pt_list_view_get_row_count(list) == 100);

    list_state state = {0};
    pt_list_view_set_on_bind_row(list, bind_row, &state);
    pt_list_view_set_on_select(list, select_row, &state);
    pt_list_view_set_on_activate(list, activate_row, &state);

    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect list_rect = bounds_of(list);
    pt_check_near(list_rect.x, 0.0, 0.01);
    pt_check_near(list_rect.y, 44.0, 0.01);
    pt_check_near(list_rect.w, 320.0, 0.01);
    pt_check_near(list_rect.h, 200.0, 0.01);

    /* 200 / 40 = 5 rows plus one overscan row are materialized. */
    pt_check(visible_rows(list) == 6);
    pt_check(materialized_rows(list) == 6);
    pt_check(state.binds == 6);
    pt_check(state.last_bind == 5);
    pt_widget *first = visible_row_at(list, 0);
    pt_widget *fifth = visible_row_at(list, 5);
    pt_check(first != NULL && fifth != NULL);
    if (first && fifth) {
        pt_check(strcmp(pt_widget_get_text(first), "Item 0") == 0);
        pt_check(strcmp(pt_widget_get_text(fifth), "Item 5") == 0);
        pt_rect first_rect = bounds_of(first);
        pt_rect fifth_rect = bounds_of(fifth);
        pt_check_near(first_rect.x, 0.0, 0.01);
        pt_check_near(first_rect.y, 44.0, 0.01);
        pt_check_near(first_rect.w, 320.0, 0.01);
        pt_check_near(first_rect.h, 40.0, 0.01);
        pt_check_near(fifth_rect.y, 244.0, 0.01);
        pt_check(pt_widget_get_kind(first) == pt_widget_list_row);
        pt_check(pt_widget_get_a11y_role(list) == pt_a11y_role_list);
        pt_check(pt_widget_get_a11y_role(first) == pt_a11y_role_list_item);
        pt_check(strcmp(pt_widget_get_a11y_name(first), "Item 0") == 0);
    }

    pt_list_view_set_selected(list, 3);
    pt_check(pt_list_view_get_selected(list) == 3);
    pt_check(state.selects == 0);
    pt_widget *third = visible_row_at(list, 3);
    pt_check(third != NULL && third->row_selected);
    pt_check((pt_widget_get_a11y_state(third) & pt_a11y_state_selected) != 0);

    /* Clicking a row focuses the list and selects the row on release. */
    click_at(window, list_rect.x + 40.0f, list_rect.y + 60.0f);
    pt_check(pt_list_view_get_selected(list) == 1);
    pt_check(state.selects == 1);
    pt_check(state.last_select == 1);
    pt_check(window->focused == list);
    pt_widget *row_one = visible_row_at(list, 1);
    pt_check(row_one != NULL && row_one->row_selected);

    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(pt_list_view_get_selected(list) == 2);
    pt_check(state.selects == 2);
    pt_window_input_key(window, XKB_KEY_Return, 0, NULL, true);
    pt_check(state.activates == 1);
    pt_check(state.last_activate == 2);

    /* End reveals the last row and recycles the visible window. */
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(pt_list_view_get_selected(list) == 99);
    pt_check_near(list->scroll_y, 3800.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_row_at(list, 99) != NULL);
    pt_check(visible_row_at(list, 0) == NULL);
    pt_check(state.binds >= 11);
    pt_check(visible_rows(list) == 5);
    pt_check(materialized_rows(list) <= 7);

    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(pt_list_view_get_selected(list) == 0);
    pt_check_near(list->scroll_y, 0.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(list) == 6);

    /* Wheel scrolling moves through rows and rebinds them. */
    pt_window_input_pointer_enter(window, list_rect.x + 40.0f, list_rect.y + 40.0f);
    pt_window_input_axis(window, list_rect.x + 40.0f, list_rect.y + 40.0f, 1.0f);
    pt_check_near(list->scroll_y, 48.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_widget *row_after_wheel = visible_row_at(list, 1);
    pt_check(row_after_wheel != NULL);
    if (row_after_wheel) {
        pt_check(strcmp(pt_widget_get_text(row_after_wheel), "Item 1") == 0);
    }

    /* Type-ahead falls back to the bound row label when no callback exists. */
    pt_list_view_set_selected(list, 0);
    pt_check(pt_window_paint(window, canvas));
    list->typeahead_length = 0;
    pt_window_input_key(window, XKB_KEY_i, 0, "i", true);
    pt_check(pt_list_view_get_selected(list) == 1);

    /* Shrinking the data keeps an in-range selection, then clears it. */
    pt_list_view_set_selected(list, 2);
    pt_check(pt_list_view_get_selected(list) == 2);
    pt_list_view_set_row_count(list, 3);
    pt_check(pt_list_view_get_row_count(list) == 3);
    pt_check(pt_list_view_get_selected(list) == 2);
    pt_check_near(list->scroll_y, 0.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(list) == 3);
    pt_check(visible_row_at(list, 2) != NULL);
    pt_check(visible_row_at(list, 3) == NULL);

    pt_list_view_set_row_count(list, 1);
    pt_check(pt_list_view_get_selected(list) == pt_list_index_none);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(list) == 1);
    pt_check(materialized_rows(list) <= 7); /* the pool is reused, not recreated */

    pt_list_view_refresh(list);
    int binds_before = state.binds;
    pt_check(pt_window_paint(window, canvas));
    pt_check(state.binds > binds_before);

    /* Rows beyond the data range clamp, and scroll_to_row reveals. */
    pt_list_view_set_row_count(list, 20);
    pt_list_view_scroll_to_row(list, 19);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_row_at(list, 19) != NULL);
    pt_check_near(list->scroll_y, 600.0, 0.01);

    /* Keyed selection survives model reordering and row-count changes. */
    for (uint32_t i = 0; i < 64; ++i) {
        row_keys[i] = 3000 + i;
    }
    pt_list_view_set_row_key_callback(list, list_row_key, NULL);
    pt_list_view_set_row_text_callback(list, list_row_text, NULL);
    pt_list_view_set_selected(list, 3);
    pt_check(pt_list_view_get_selected_key(list) == 3003);
    uint64_t reordered[20] = {row_keys[3], row_keys[0], row_keys[1], row_keys[2],
                              row_keys[4], row_keys[5], row_keys[6], row_keys[7],
                              row_keys[8], row_keys[9], row_keys[10], row_keys[11],
                              row_keys[12], row_keys[13], row_keys[14], row_keys[15],
                              row_keys[16], row_keys[17], row_keys[18], row_keys[19]};
    memcpy(row_keys, reordered, sizeof(reordered));
    pt_list_view_refresh(list);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_list_view_get_selected(list) == 0);
    pt_check(pt_list_view_get_selected_key(list) == 3003);
    pt_check(pt_list_view_set_selected_key(list, 3006));
    pt_check(pt_list_view_get_selected(list) == 6);
    pt_check(!pt_list_view_set_selected_key(list, 999999));
    pt_check(pt_list_view_get_selected(list) == 6);
    pt_list_view_set_row_count(list, 30);
    pt_check(pt_list_view_get_selected(list) == 6);
    pt_check(pt_list_view_get_selected_key(list) == 3006);

    /* Batch invalidation rebinds only rows intersecting the range. */
    pt_list_view_set_selected(list, 0);
    pt_check(pt_window_paint(window, canvas));
    state.binds = 0;
    pt_list_view_refresh_rows(list, 0, 2);
    pt_check(pt_window_paint(window, canvas));
    pt_check(state.binds == 2);
    state.binds = 0;
    pt_list_view_refresh_rows(list, 200, 2);
    pt_check(pt_window_paint(window, canvas));
    pt_check(state.binds == 0);

    /* Callback labels drive case-insensitive prefix search and cycling. */
    pt_window_focus(window, list);
    pt_list_view_set_selected(list, 0);
    list->typeahead_length = 0;
    pt_window_input_key(window, XKB_KEY_b, 0, "b", true);
    pt_check(pt_list_view_get_selected(list) == 1);
    pt_list_view_set_selected(list, 0);
    list->typeahead_length = 0;
    uint32_t expected[] = {6, 12, 18, 24, 0};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        pt_window_input_key(window, XKB_KEY_a, 0, "a", true);
        pt_check(pt_list_view_get_selected(list) == expected[i]);
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.dataview");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("data_view");
    }
    test_virtualization_and_selection(app);
    pt_app_destroy(app);
    return pt_test_finish("data_view");
}
