/*
 * Picker widget checks (1.2 wave 2): searchable select, date, number and
 * colour pickers. The first section covers the shared plumbing and the
 * searchable select; later sections belong to the control whose commit landed
 * them.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct picker_fixture {
    pt_window *window;
    pt_canvas *canvas;
    pt_widget *select;
    pt_widget *next;
} picker_fixture;

typedef struct select_log {
    int changes;
    uint32_t last_index;
    bool destroy;
} select_log;

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 10);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 20);
}

static void type(pt_window *window, uint32_t keysym, const char *utf8) {
    pt_window_input_key(window, keysym, 0, utf8, true);
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static pt_picker *picker_of(pt_widget *widget) {
    pt_combobox_state *state =
        widget && widget->kind == pt_widget_combobox ? widget->extra : NULL;
    return state ? state->picker : NULL;
}

static pt_widget *field_of(pt_widget *widget) {
    pt_picker *picker = picker_of(widget);
    return picker ? picker->field : NULL;
}

static uint32_t row_count(pt_widget *widget) {
    pt_picker *picker = picker_of(widget);
    if (!picker || !picker->content) {
        return 0u;
    }
    uint32_t count = 0;
    for (pt_widget *row = picker->content->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_list_row) {
            ++count;
        }
    }
    return count;
}

static pt_widget *row_at(pt_widget *widget, uint32_t position) {
    pt_picker *picker = picker_of(widget);
    if (!picker || !picker->content) {
        return NULL;
    }
    uint32_t seen = 0;
    for (pt_widget *row = picker->content->first_child; row; row = row->next_sibling) {
        if (row->kind != pt_widget_list_row) {
            continue;
        }
        if (seen == position) {
            return row;
        }
        ++seen;
    }
    return NULL;
}

static void on_select_change(pt_widget *widget, uint32_t index, void *user_data) {
    (void)widget;
    select_log *log = user_data;
    ++log->changes;
    log->last_index = index;
    if (log->destroy) {
        pt_widget_destroy(widget);
    }
}

static void fixture_open(picker_fixture *fx, pt_app *app) {
    fx->window = pt_window_create(app, "picker", 640, 520);
    fx->canvas = pt_canvas_create(640, 520, 1.0f);
    pt_widget *root = pt_window_root(fx->window);
    fx->select = pt_select_create(root);
    pt_widget_set_width(fx->select, 260.0f);
    fx->next = pt_button_create(root, "Next");
    pt_window_show(fx->window);
    pt_check(pt_window_paint(fx->window, fx->canvas));
}

static void fixture_close(picker_fixture *fx) {
    pt_window_free(fx->window);
    pt_canvas_destroy(fx->canvas);
    fx->window = NULL;
    fx->canvas = NULL;
}

static void fill_regions(pt_widget *select) {
    pt_select_add_item(select, "North");
    pt_select_add_item(select, "Norfolk");
    pt_select_add_item(select, "South");
}

/* ---------- searchable select: model ---------- */

static void test_select_model(void) {
    pt_widget *select = pt_select_create(NULL);
    pt_check(select != NULL);
    pt_check(picker_of(select) != NULL);
    pt_check(picker_of(select)->kind == pt_picker_kind_select);
    pt_check(pt_widget_get_kind(select) == pt_widget_combobox);
    pt_check(field_of(select) != NULL);
    pt_check(pt_select_get_item_count(select) == 0u);
    pt_check(pt_select_get_selected(select) == pt_list_index_none);
    pt_check(strcmp(pt_select_get_query(select), "") == 0);
    pt_check(pt_select_get_max_popup_rows(select) == 8u);
    pt_check(strcmp(pt_select_get_placeholder(select), "") == 0);

    pt_select_set_placeholder(select, "Region");
    pt_check(strcmp(pt_select_get_placeholder(select), "Region") == 0);
    pt_check(strcmp(pt_widget_get_text(field_of(select)), "") == 0);

    pt_select_add_item(select, "Red");
    pt_select_add_item(select, "Green");
    pt_select_insert_item(select, 1u, "Amber");
    pt_check(pt_select_get_item_count(select) == 3u);
    pt_check(strcmp(pt_select_get_item_text(select, 0u), "Red") == 0);
    pt_check(strcmp(pt_select_get_item_text(select, 1u), "Amber") == 0);
    pt_check(strcmp(pt_select_get_item_text(select, 2u), "Green") == 0);
    pt_check(strcmp(pt_select_get_item_text(select, 9u), "") == 0);

    int marker = 5;
    pt_select_add_item_with_data(select, "Violet", &marker);
    pt_check(pt_select_get_item_data(select, 3u) == &marker);
    pt_select_set_item_data(select, 0u, &marker);
    pt_check(pt_select_get_item_data(select, 0u) == &marker);
    pt_check(pt_select_get_item_data(select, 9u) == NULL);

    pt_select_set_selected(select, 2u);
    pt_check(pt_select_get_selected(select) == 2u);
    pt_check(strcmp(pt_widget_get_text(field_of(select)), "Green") == 0);
    pt_check(strcmp(pt_combobox_get_text(select), "Green") == 0);

    /* Inserting above the selection keeps it on the same item. */
    pt_select_insert_item(select, 0u, "Zero");
    pt_check(pt_select_get_selected(select) == 3u);
    pt_check(strcmp(pt_widget_get_text(field_of(select)), "Green") == 0);

    pt_select_remove_item(select, 0u);
    pt_check(pt_select_get_selected(select) == 2u);
    pt_select_remove_item(select, 2u);
    pt_check(pt_select_get_selected(select) == pt_list_index_none);
    pt_check(strcmp(pt_widget_get_text(field_of(select)), "") == 0);

    /* An out-of-range index clears, mirroring the combo box. */
    pt_select_set_selected(select, 1u);
    pt_select_set_selected(select, 99u);
    pt_check(pt_select_get_selected(select) == pt_list_index_none);
    pt_check(strcmp(pt_widget_get_text(field_of(select)), "") == 0);

    pt_select_set_selected(select, 0u);
    pt_select_clear_selection(select);
    pt_check(pt_select_get_selected(select) == pt_list_index_none);
    pt_check(strcmp(pt_widget_get_text(field_of(select)), "") == 0);

    pt_select_clear(select);
    pt_check(pt_select_get_item_count(select) == 0u);
    pt_check(pt_select_get_match_count(select) == 0u);

    /* The combo-box model API is a no-op on a picker host. */
    pt_combobox_add_item(select, "Ignored");
    pt_combobox_set_selected(select, 0u);
    pt_combobox_set_mode(select, pt_combobox_select_only);
    pt_check(pt_select_get_item_count(select) == 0u);
    pt_check(pt_select_get_selected(select) == pt_list_index_none);

    /* Limits: the count stops at PT_SELECT_MAX_ITEMS and reports the error. */
    for (uint32_t i = 0; i < PT_SELECT_MAX_ITEMS + 3u; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Item %u", i);
        pt_select_add_item(select, label);
    }
    pt_check(pt_select_get_item_count(select) == PT_SELECT_MAX_ITEMS);
    pt_check(pt_get_last_error()[0] != '\0');

    pt_widget_destroy(select);
}

/* ---------- searchable select: keyboard and filtering ---------- */

static void test_select_keyboard(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    select_log log = {0};
    log.last_index = pt_list_index_none;
    fill_regions(fx.select);
    pt_select_set_on_change(fx.select, on_select_change, &log);

    pt_widget *field = field_of(fx.select);
    pt_check(pt_widget_focus(field));
    pt_check(fx.window->focused == field);
    pt_check(!pt_select_is_open(fx.select));

    /* Typing filters and opens the popup at the same time. */
    type(fx.window, XKB_KEY_n, "n");
    pt_check(pt_select_is_open(fx.select));
    pt_check(pt_select_get_match_count(fx.select) == 2u);
    pt_check(row_count(fx.select) == 2u);
    pt_check(strcmp(pt_select_get_query(fx.select), "n") == 0);
    pt_check(pt_widget_get_a11y_state(fx.select) & pt_a11y_state_expanded);

    type(fx.window, XKB_KEY_o, "o");
    type(fx.window, XKB_KEY_r, "r");
    pt_check(pt_select_get_match_count(fx.select) == 2u);
    pt_check(log.changes == 0);

    /* A space filters like any character and can empty the candidate set. */
    type(fx.window, XKB_KEY_space, " ");
    pt_check(pt_select_get_match_count(fx.select) == 0u);
    pt_check(row_count(fx.select) == 1u);
    pt_widget *empty = row_at(fx.select, 0u);
    pt_check(empty && !empty->enabled);
    pt_check(empty && strcmp(pt_widget_get_a11y_name(empty), "No matches") == 0);

    type(fx.window, XKB_KEY_BackSpace, NULL);
    pt_check(pt_select_get_match_count(fx.select) == 2u);
    pt_check(picker_of(fx.select)->highlighted == 0u);

    type(fx.window, XKB_KEY_Down, NULL);
    pt_check(picker_of(fx.select)->highlighted == 1u);
    type(fx.window, XKB_KEY_Up, NULL);
    type(fx.window, XKB_KEY_Up, NULL);
    pt_check(picker_of(fx.select)->highlighted == 1u);

    /* Enter commits the highlight, closes the popup and restores the label. */
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(!pt_select_is_open(fx.select));
    pt_check(pt_select_get_selected(fx.select) == 1u);
    pt_check(log.changes == 1);
    pt_check(log.last_index == 1u);
    pt_check(strcmp(pt_widget_get_text(field), "Norfolk") == 0);
    pt_check(strcmp(pt_select_get_query(fx.select), "") == 0);
    pt_check(fx.window->focused == field);

    /* Escape cancels: the committed label returns and the filter is dropped. */
    type(fx.window, XKB_KEY_s, "s");
    pt_check(pt_select_is_open(fx.select));
    pt_check(pt_select_get_match_count(fx.select) == 1u);
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_select_is_open(fx.select));
    pt_check(strcmp(pt_widget_get_text(field), "Norfolk") == 0);
    pt_check(strcmp(pt_select_get_query(fx.select), "") == 0);
    pt_check(pt_select_get_selected(fx.select) == 1u);
    pt_check(log.changes == 1);

    /* F4 opens on the whole model and Home/End/Page move the highlight. */
    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(pt_select_is_open(fx.select));
    pt_check(picker_of(fx.select)->highlighted == 1u);
    type(fx.window, XKB_KEY_End, NULL);
    pt_check(picker_of(fx.select)->highlighted == 2u);
    type(fx.window, XKB_KEY_Home, NULL);
    pt_check(picker_of(fx.select)->highlighted == 0u);
    type(fx.window, XKB_KEY_Page_Down, NULL);
    pt_check(picker_of(fx.select)->highlighted == 2u);
    type(fx.window, XKB_KEY_Page_Up, NULL);
    pt_check(picker_of(fx.select)->highlighted == 0u);
    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(!pt_select_is_open(fx.select));

    /* Alt+Down opens; Down on a closed select opens it; Delete clears. */
    pt_window_input_key(fx.window, XKB_KEY_Down, (uint32_t)pt_key_modifier_alt, NULL, true);
    pt_check(pt_select_is_open(fx.select));
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_select_is_open(fx.select));
    type(fx.window, XKB_KEY_Down, NULL);
    pt_check(pt_select_is_open(fx.select));
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_select_is_open(fx.select));

    pt_check(pt_widget_key(field, XKB_KEY_Delete, 0, NULL));
    pt_check(pt_select_get_selected(fx.select) == pt_list_index_none);
    pt_check(strcmp(pt_widget_get_text(field), "") == 0);

    /* Enter with the popup closed opens it instead of committing. */
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(pt_select_is_open(fx.select));
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_select_is_open(fx.select));

    /* Tab closes the popup and continues in the normal focus order. */
    pt_widget_focus(field);
    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(pt_select_is_open(fx.select));
    type(fx.window, XKB_KEY_Tab, NULL);
    pt_check(!pt_select_is_open(fx.select));
    pt_check(fx.window->focused != field);
    pt_check(fx.window->focused == fx.next);

    /* Programmatic query filtering works while closed and does not open. */
    pt_select_set_query(fx.select, "sou");
    pt_check(!pt_select_is_open(fx.select));
    pt_check(pt_select_get_match_count(fx.select) == 1u);
    pt_check(pt_select_get_match_item(fx.select, 0u) == 2u);

    fixture_close(&fx);
}

/* ---------- searchable select: pointer and popup ---------- */

static void test_select_pointer(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    select_log log = {0};
    log.last_index = pt_list_index_none;
    fill_regions(fx.select);
    pt_select_set_on_change(fx.select, on_select_change, &log);
    pt_check(pt_window_paint(fx.window, fx.canvas));

    pt_rect host = bounds_of(fx.select);
    click_at(fx.window, host.x + host.w - 8.0f, host.y + host.h * 0.5f);
    pt_check(pt_select_is_open(fx.select));
    /* The list is a real application popup, below the field. */
    pt_picker *picker = picker_of(fx.select);
    pt_check(picker && picker->popup != NULL);
    pt_check(picker && picker->popup->overlay.kind == pt_overlay_kind_popup);
    pt_check(picker && picker->popup->overlay.owner == fx.select);
    pt_check(pt_window_overlays_active(fx.window));
    pt_rect popup = picker->popup->overlay.rect;
    pt_check(popup.y >= host.y + host.h);
    pt_check(popup.x >= 0.0f && popup.x + popup.w <= (float)fx.window->width);
    pt_check(popup.y + popup.h <= (float)fx.window->height);
    pt_check(row_count(fx.select) == 3u);

    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *third = row_at(fx.select, 2u);
    pt_check(third != NULL);
    pt_rect third_bounds = bounds_of(third);
    click_at(fx.window, third_bounds.x + third_bounds.w * 0.5f,
             third_bounds.y + third_bounds.h * 0.5f);
    pt_check(pt_select_get_selected(fx.select) == 2u);
    pt_check(log.changes == 1);
    pt_check(log.last_index == 2u);
    pt_check(!pt_select_is_open(fx.select));
    pt_check(strcmp(pt_widget_get_text(field_of(fx.select)), "South") == 0);

    /* A press outside closes and restores focus to the editor. */
    pt_check(pt_window_paint(fx.window, fx.canvas));
    click_at(fx.window, host.x + host.w - 8.0f, host.y + host.h * 0.5f);
    pt_check(pt_select_is_open(fx.select));
    click_at(fx.window, 5.0f, 5.0f);
    pt_check(!pt_select_is_open(fx.select));
    pt_check(pt_select_get_selected(fx.select) == 2u);
    pt_check(fx.window->focused == field_of(fx.select));

    /* A press on the field itself is outside the popup and closes it too. */
    click_at(fx.window, host.x + 10.0f, host.y + host.h * 0.5f);
    pt_check(pt_select_is_open(fx.select));
    click_at(fx.window, host.x + 10.0f, host.y + host.h * 0.5f);
    pt_check(!pt_select_is_open(fx.select));

    /* Choosing a row through the widget activation path (Enter on a row) goes
     * through the same commit hook as the keyboard and the pointer. */
    pt_select_open(fx.select);
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_check(pt_widget_activate(row_at(fx.select, 0u)));
    pt_check(pt_select_get_selected(fx.select) == 0u);
    pt_check(log.changes == 2);
    pt_check(pt_select_is_open(fx.select) == false);

    /* An empty select refuses to open. */
    pt_select_clear(fx.select);
    pt_select_open(fx.select);
    pt_check(!pt_select_is_open(fx.select));

    fixture_close(&fx);
}

static void test_select_limits_and_a11y(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    for (uint32_t i = 0; i < 10u; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Option %u", i);
        pt_select_add_item(fx.select, label);
    }
    pt_check(pt_widget_get_a11y_role(fx.select) == pt_a11y_role_combo_box);
    pt_check(strcmp(pt_widget_get_a11y_name(fx.select), "Select") == 0);
    pt_widget_set_a11y_name(fx.select, "Region");
    pt_check(strcmp(pt_widget_get_a11y_name(fx.select), "Region") == 0);
    uint32_t closed = pt_widget_get_a11y_state(fx.select);
    pt_check(closed & pt_a11y_state_expandable);
    pt_check(!(closed & pt_a11y_state_expanded));

    float value = 0.0f;
    pt_check(pt_widget_get_a11y_value(fx.select, &value));
    pt_check(value < 0.0f);
    pt_select_set_selected(fx.select, 3u);
    pt_check(pt_widget_get_a11y_value(fx.select, &value));
    pt_check_near(value, 3.0, 0.001);

    /* Row limit clamps and truncates the exposed candidates. */
    pt_select_set_max_popup_rows(fx.select, 0u);
    pt_check(pt_select_get_max_popup_rows(fx.select) == 1u);
    pt_select_set_max_popup_rows(fx.select, 5000u);
    pt_check(pt_select_get_max_popup_rows(fx.select) == PT_PICKER_MAX_CANDIDATES);
    pt_select_set_max_popup_rows(fx.select, 2u);
    pt_select_set_selected(fx.select, 1u);
    pt_select_open(fx.select);
    pt_check(pt_select_get_match_count(fx.select) == 2u);
    pt_check(row_count(fx.select) == 2u);
    pt_check(pt_widget_get_a11y_state(fx.select) & pt_a11y_state_expanded);

    pt_widget *first = row_at(fx.select, 0u);
    pt_widget *second = row_at(fx.select, 1u);
    pt_check(first && second);
    pt_check(first && pt_widget_get_a11y_role(first) == pt_a11y_role_list_item);
    pt_check(first && strcmp(pt_widget_get_a11y_name(first), "Option 0") == 0);
    uint32_t first_state = first ? pt_widget_get_a11y_state(first) : 0u;
    pt_check(first_state & pt_a11y_state_selectable);
    pt_check(!(first_state & pt_a11y_state_selected));
    uint32_t second_state = second ? pt_widget_get_a11y_state(second) : 0u;
    pt_check(second_state & pt_a11y_state_selected);
    pt_check(second && second->row_current);

    /* A refresh keeps the committed item current after the query changes. */
    pt_select_set_query(fx.select, "option 5");
    pt_check(pt_select_get_match_count(fx.select) == 1u);
    pt_widget *only = row_at(fx.select, 0u);
    pt_check(only && strcmp(pt_widget_get_a11y_name(only), "Option 5") == 0);
    pt_check(only && !only->row_current);

    pt_select_close(fx.select);
    pt_check(!(pt_widget_get_a11y_state(fx.select) & pt_a11y_state_expanded));
    fixture_close(&fx);
}

static void test_select_teardown(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    fill_regions(fx.select);
    pt_select_open(fx.select);
    pt_check(pt_window_overlays_active(fx.window));
    pt_widget_destroy(fx.select);
    pt_check(!pt_window_overlays_active(fx.window));
    pt_check(pt_window_paint(fx.window, fx.canvas));
    fixture_close(&fx);

    /* A callback that destroys the picker must not leave the popup touching
     * freed state. */
    select_log log = {0};
    log.last_index = pt_list_index_none;
    log.destroy = true;
    fixture_open(&fx, app);
    fill_regions(fx.select);
    pt_select_set_on_change(fx.select, on_select_change, &log);
    pt_select_open(fx.select);
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *row = row_at(fx.select, 0u);
    pt_check(row != NULL);
    if (row) {
        pt_check(pt_widget_activate(row));
    }
    pt_check(log.changes == 1);
    pt_check(!pt_widget_attached(fx.window, fx.select));
    pt_check(!pt_window_overlays_active(fx.window));
    pt_check(pt_window_paint(fx.window, fx.canvas));
    fixture_close(&fx);
}

static void test_select_null(void) {
    pt_check(pt_select_create(NULL) != NULL); /* detached is legal */
    pt_widget *detached = pt_select_create(NULL);
    pt_select_add_item(detached, "A");
    pt_select_set_selected(detached, 0u);
    pt_check(pt_select_get_selected(detached) == 0u);
    pt_select_open(detached); /* no window: refuses, no crash */
    pt_check(!pt_select_is_open(detached));
    pt_widget_destroy(detached);

    pt_select_add_item(NULL, "x");
    pt_select_insert_item(NULL, 0u, "x");
    pt_select_remove_item(NULL, 0u);
    pt_select_clear(NULL);
    pt_select_set_placeholder(NULL, "x");
    pt_select_set_query(NULL, "x");
    pt_select_set_selected(NULL, 0u);
    pt_select_clear_selection(NULL);
    pt_select_open(NULL);
    pt_select_close(NULL);
    pt_select_set_max_popup_rows(NULL, 4u);
    pt_select_set_on_change(NULL, NULL, NULL);
    pt_select_set_item_data(NULL, 0u, NULL);
    pt_check(pt_select_get_placeholder(NULL)[0] == '\0');
    pt_check(pt_select_get_item_count(NULL) == 0u);
    pt_check(pt_select_get_item_text(NULL, 0u)[0] == '\0');
    pt_check(pt_select_get_item_data(NULL, 0u) == NULL);
    pt_check(pt_select_get_selected(NULL) == pt_list_index_none);
    pt_check(pt_select_get_query(NULL)[0] == '\0');
    pt_check(pt_select_get_match_count(NULL) == 0u);
    pt_check(pt_select_get_match_item(NULL, 0u) == pt_list_index_none);
    pt_check(pt_select_get_max_popup_rows(NULL) == 0u);
    pt_check(!pt_select_is_open(NULL));
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.picker");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("picker");
    }
    test_select_model();
    test_select_keyboard(app);
    test_select_pointer(app);
    test_select_limits_and_a11y(app);
    test_select_teardown(app);
    test_select_null();
    pt_app_destroy(app);
    return pt_test_finish("picker");
}