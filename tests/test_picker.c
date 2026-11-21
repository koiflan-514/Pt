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

/* ---------- date picker ---------- */

typedef struct date_log {
    int changes;
    pt_date last;
    bool destroy;
} date_log;

static void on_date_change(pt_widget *picker, pt_date date, void *user_data) {
    (void)picker;
    date_log *log = user_data;
    ++log->changes;
    log->last = date;
    if (log->destroy) {
        pt_widget_destroy(picker);
    }
}

static pt_widget *find_named(pt_widget *root, const char *name) {
    if (!root) {
        return NULL;
    }
    if (strcmp(pt_widget_get_a11y_name(root), name) == 0) {
        return root;
    }
    for (pt_widget *child = root->first_child; child; child = child->next_sibling) {
        pt_widget *found = find_named(child, name);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static uint32_t count_kind(pt_widget *root, pt_widget_kind kind) {
    if (!root) {
        return 0u;
    }
    uint32_t count = root->kind == kind ? 1u : 0u;
    for (pt_widget *child = root->first_child; child; child = child->next_sibling) {
        count += count_kind(child, kind);
    }
    return count;
}

static void test_date_values(void) {
    pt_check(!pt_date_is_valid((pt_date){2026, 2, 29}));
    pt_check(pt_date_is_valid((pt_date){2024, 2, 29}));
    pt_check(pt_date_is_valid((pt_date){2000, 2, 29}));
    pt_check(!pt_date_is_valid((pt_date){1900, 2, 29}));
    pt_check(!pt_date_is_valid((pt_date){2026, 13, 1}));
    pt_check(!pt_date_is_valid((pt_date){2026, 0, 1}));
    pt_check(!pt_date_is_valid((pt_date){2026, 4, 31}));
    pt_check(!pt_date_is_valid((pt_date){0, 1, 1}));
    pt_date today = pt_date_today();
    pt_check(today.year == 0 || pt_date_is_valid(today));

    pt_widget *picker = pt_date_picker_create(NULL);
    pt_check(picker != NULL);
    pt_check(picker_of(picker) && picker_of(picker)->kind == pt_picker_kind_date);
    pt_check(pt_date_picker_get_date(picker).year == 0);
    pt_check(strcmp(pt_date_picker_get_text(picker), "") == 0);
    pt_check(pt_date_picker_get_format(picker) == pt_date_format_iso);
    pt_check(strcmp(pt_date_picker_get_placeholder(picker), "Date") == 0);
    pt_check(pt_widget_get_a11y_role(picker) == pt_a11y_role_combo_box);
    pt_check(strcmp(pt_widget_get_a11y_name(picker), "Date") == 0);

    pt_check(pt_date_picker_set_date(picker, (pt_date){2026, 11, 22}));
    pt_check(pt_date_picker_get_date(picker).year == 2026);
    pt_check(pt_date_picker_get_date(picker).month == 11u);
    pt_check(pt_date_picker_get_date(picker).day == 22u);
    pt_check(strcmp(pt_date_picker_get_text(picker), "2026-11-22") == 0);
    pt_check(strcmp(pt_widget_get_text(field_of(picker)), "2026-11-22") == 0);

    /* An invalid set leaves both value and text untouched. */
    pt_check(!pt_date_picker_set_date(picker, (pt_date){2026, 2, 30}));
    pt_check(pt_date_picker_get_date(picker).day == 22u);
    pt_check(strcmp(pt_date_picker_get_text(picker), "2026-11-22") == 0);

    /* Text commit and the ISO fallback. */
    pt_check(pt_date_picker_set_text(picker, "2027-01-05"));
    pt_check(pt_date_picker_get_date(picker).year == 2027);
    pt_check(pt_date_picker_get_date(picker).month == 1u);
    pt_check(!pt_date_picker_set_text(picker, "2027-02-29"));
    pt_check(pt_date_picker_get_date(picker).day == 5u);
    pt_check(strcmp(pt_date_picker_get_text(picker), "2027-01-05") == 0);

    /* Pictures follow the selected format; parsing accepts the format and
     * always falls back to ISO. */
    pt_date_picker_set_format(picker, pt_date_format_dmy);
    pt_check(pt_date_picker_get_format(picker) == pt_date_format_dmy);
    pt_check(strcmp(pt_date_picker_get_text(picker), "05/01/2027") == 0);
    pt_check(pt_date_picker_set_text(picker, "22/11/2026"));
    pt_check(pt_date_picker_get_date(picker).month == 11u);
    pt_check(pt_date_picker_get_date(picker).day == 22u);
    pt_check(pt_date_picker_set_text(picker, "2026-12-31"));
    pt_check(pt_date_picker_get_date(picker).month == 12u);
    pt_check(strcmp(pt_date_picker_get_text(picker), "31/12/2026") == 0);
    pt_date_picker_set_format(picker, pt_date_format_mdy);
    pt_check(strcmp(pt_date_picker_get_text(picker), "12/31/2026") == 0);
    pt_check(pt_date_picker_set_text(picker, "11/22/2026"));
    pt_check(pt_date_picker_get_date(picker).month == 11u);

    /* A11y value is YYYYMMDD. */
    float value = 0.0f;
    pt_check(pt_widget_get_a11y_value(picker, &value));
    pt_check_near(value, 20261122.0, 0.5);

    pt_check(pt_date_picker_set_date(picker, (pt_date){1970, 1, 1}));
    pt_check(pt_date_picker_get_text(picker)[0] != '\0');
    pt_check(pt_date_picker_set_date(picker, (pt_date){9999, 12, 31}));
    pt_check(pt_date_picker_get_date(picker).year == 9999);
    pt_check(!pt_date_picker_set_date(picker, (pt_date){10000, 1, 1}));

    /* Clear through the explicit path and through an empty text commit. */
    pt_date_picker_clear(picker);
    pt_check(pt_date_picker_get_date(picker).year == 0);
    pt_check(strcmp(pt_date_picker_get_text(picker), "") == 0);
    pt_check(pt_widget_get_a11y_value(picker, &value));
    pt_check_near(value, 0.0, 0.001);
    pt_check(pt_date_picker_set_date(picker, (pt_date){2026, 11, 22}));
    pt_check(pt_date_picker_set_text(picker, ""));
    pt_check(pt_date_picker_get_date(picker).year == 0);

    /* Combo-box API stays inert on a picker host. */
    pt_combobox_add_item(picker, "Ignored");
    pt_check(pt_select_get_item_count(picker) == 0u);

    pt_widget_destroy(picker);
}

static void test_date_keyboard(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    pt_widget *picker = pt_date_picker_create(pt_window_root(fx.window));
    pt_widget_set_width(picker, 260.0f);
    pt_widget_destroy(fx.select); /* only one picker is needed here */
    fx.select = picker;
    date_log log = {0};
    pt_date_picker_set_on_change(picker, on_date_change, &log);
    pt_check(pt_date_picker_set_date(picker, (pt_date){2026, 11, 22}));
    pt_check(pt_window_paint(fx.window, fx.canvas));

    pt_widget *field = field_of(picker);
    pt_check(pt_widget_focus(field));
    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(pt_date_picker_is_open(picker));
    pt_picker *state = picker_of(picker);
    pt_check(state && state->popup->overlay.kind == pt_overlay_kind_popup);
    pt_check(state && state->popup->overlay.owner == picker);
    pt_check(state && state->focus_date.year == 2026 && state->focus_date.month == 11u);
    pt_check(state && state->focus_date.day == 22u);
    pt_check(state && count_kind(state->content, pt_widget_list_row) == 42u);
    pt_check(state && find_named(state->content, "2026-11-22") != NULL);
    pt_check(state && find_named(state->content, "November 2026") != NULL);

    /* Arrows move the focused day; crossing a month boundary changes the view. */
    type(fx.window, XKB_KEY_Right, NULL);
    pt_check(state->focus_date.day == 23u);
    type(fx.window, XKB_KEY_Up, NULL);
    pt_check(state->focus_date.day == 16u);
    type(fx.window, XKB_KEY_Left, NULL);
    pt_check(state->focus_date.day == 15u);
    type(fx.window, XKB_KEY_Down, NULL);
    pt_check(state->focus_date.day == 22u);
    type(fx.window, XKB_KEY_End, NULL);
    pt_check(state->focus_date.day == 30u);
    type(fx.window, XKB_KEY_Home, NULL);
    pt_check(state->focus_date.day == 1u);
    type(fx.window, XKB_KEY_Page_Up, NULL);
    pt_check(state->focus_date.month == 10u);
    pt_check(state->view_month == 10u);
    pt_check(find_named(state->content, "October 2026") != NULL);
    type(fx.window, XKB_KEY_Page_Down, NULL);
    pt_check(state->focus_date.month == 11u);
    type(fx.window, XKB_KEY_Right, NULL);
    pt_check(state->focus_date.day == 2u);

    /* The focused cell carries the selected state and its ISO name. */
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *focused_cell = find_named(state->content, "2026-11-02");
    pt_check(focused_cell != NULL);
    if (focused_cell) {
        uint32_t cell_state = pt_widget_get_a11y_state(focused_cell);
        pt_check(cell_state & pt_a11y_state_selectable);
        pt_check(cell_state & pt_a11y_state_selected);
        pt_check(focused_cell->row_selected);
        pt_check(pt_widget_get_a11y_role(focused_cell) == pt_a11y_role_table_cell);
    }

    /* Enter commits the focused day and closes the popup. */
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(!pt_date_picker_is_open(picker));
    pt_check(pt_date_picker_get_date(picker).day == 2u);
    pt_check(log.changes == 1);
    pt_check(log.last.day == 2u);
    pt_check(strcmp(pt_date_picker_get_text(picker), "2026-11-02") == 0);
    pt_check(fx.window->focused == field);

    /* Escape cancels: the committed text and date are unchanged. */
    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(pt_date_picker_is_open(picker));
    type(fx.window, XKB_KEY_Right, NULL);
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_date_picker_is_open(picker));
    pt_check(pt_date_picker_get_date(picker).day == 2u);
    pt_check(strcmp(pt_date_picker_get_text(picker), "2026-11-02") == 0);
    pt_check(fx.window->focused == field);

    /* T is the today shortcut and commits immediately. */
    pt_date today = pt_date_today();
    if (today.year != 0) {
        type(fx.window, XKB_KEY_F4, NULL);
        pt_check(pt_date_picker_is_open(picker));
        type(fx.window, XKB_KEY_t, "t");
        pt_check(!pt_date_picker_is_open(picker));
        pt_check(pt_date_picker_get_date(picker).year == today.year);
        pt_check(log.changes == 2);
        pt_check(log.last.month == today.month);
    }

    /* Typed text commits on Enter; invalid text is rejected and restored. */
    pt_check(pt_date_picker_set_date(picker, (pt_date){2026, 11, 22}));
    pt_widget_set_text(field, "2027-03-04");
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(pt_date_picker_get_date(picker).year == 2027);
    pt_check(log.changes >= 3);
    pt_widget_set_text(field, "2030-02-30");
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(pt_date_picker_get_date(picker).year == 2027);
    pt_check(strcmp(pt_widget_get_text(field), "2027-03-04") == 0);
    pt_check(pt_get_last_error()[0] != '\0');
    /* Escape on edited text restores the committed picture. */
    pt_widget_set_text(field, "nonsense");
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(strcmp(pt_widget_get_text(field), "2027-03-04") == 0);

    /* Tab closes a calendar popup and continues focus order. */
    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(pt_date_picker_is_open(picker));
    type(fx.window, XKB_KEY_Tab, NULL);
    pt_check(!pt_date_picker_is_open(picker));

    fixture_close(&fx);
}

static void test_date_pointer_and_limits(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    pt_widget *picker = pt_date_picker_create(pt_window_root(fx.window));
    pt_widget_set_width(picker, 260.0f);
    pt_widget_destroy(fx.select);
    fx.select = picker;
    date_log log = {0};
    pt_date_picker_set_on_change(picker, on_date_change, &log);
    pt_check(pt_date_picker_set_date(picker, (pt_date){2026, 11, 22}));
    pt_check(pt_window_paint(fx.window, fx.canvas));

    pt_rect host = bounds_of(picker);
    click_at(fx.window, host.x + host.w - 8.0f, host.y + host.h * 0.5f);
    pt_check(pt_date_picker_is_open(picker));
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_picker *state = picker_of(picker);
    pt_rect popup = state->popup->overlay.rect;
    pt_check(popup.x >= 0.0f && popup.x + popup.w <= (float)fx.window->width);
    pt_check(popup.y >= host.y + host.h);
    pt_check(popup.y + popup.h <= (float)fx.window->height);

    /* Month navigation buttons rebuild the 42-cell grid without growth. */
    pt_widget *next = find_named(state->content, "Next month");
    pt_check(next != NULL);
    if (next) {
        pt_rect next_bounds = bounds_of(next);
        click_at(fx.window, next_bounds.x + next_bounds.w * 0.5f,
                 next_bounds.y + next_bounds.h * 0.5f);
        pt_check(state->view_month == 12u);
        pt_check(count_kind(state->content, pt_widget_list_row) == 42u);
        pt_check(find_named(state->content, "December 2026") != NULL);
    }
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *previous = find_named(state->content, "Previous month");
    pt_check(previous != NULL);
    if (previous) {
        pt_rect previous_bounds = bounds_of(previous);
        click_at(fx.window, previous_bounds.x + previous_bounds.w * 0.5f,
                 previous_bounds.y + previous_bounds.h * 0.5f);
        pt_check(state->view_month == 11u);
    }

    /* A press on a day commits it and closes. */
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *day = find_named(state->content, "2026-11-10");
    pt_check(day != NULL);
    if (day) {
        pt_rect day_bounds = bounds_of(day);
        click_at(fx.window, day_bounds.x + day_bounds.w * 0.5f,
                 day_bounds.y + day_bounds.h * 0.5f);
        pt_check(pt_date_picker_get_date(picker).day == 10u);
        pt_check(log.changes == 1);
        pt_check(!pt_date_picker_is_open(picker));
    }

    /* Adjacent-month cells are disabled and swallow the press. */
    pt_date_picker_open(picker);
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *outside = find_named(state->content, "2026-10-30");
    if (outside) {
        pt_check(!outside->enabled);
    }
    /* Today button commits the local date. */
    pt_widget *today_button = find_named(state->content, "Today");
    pt_check(today_button != NULL);
    if (today_button) {
        pt_rect today_bounds = bounds_of(today_button);
        click_at(fx.window, today_bounds.x + today_bounds.w * 0.5f,
                 today_bounds.y + today_bounds.h * 0.5f);
        pt_date today = pt_date_today();
        if (today.year != 0) {
            pt_check(pt_date_picker_get_date(picker).year == today.year);
            pt_check(pt_date_picker_get_date(picker).month == today.month);
            pt_check(!pt_date_picker_is_open(picker));
        }
    }

    /* Destroy while open. */
    pt_date_picker_open(picker);
    pt_check(pt_window_overlays_active(fx.window));
    pt_widget_destroy(picker);
    pt_check(!pt_window_overlays_active(fx.window));
    pt_check(pt_window_paint(fx.window, fx.canvas));
    fixture_close(&fx);
}

static void test_date_callbacks_and_null(pt_app *app) {
    picker_fixture fx;
    fixture_open(&fx, app);
    pt_widget *picker = pt_date_picker_create(pt_window_root(fx.window));
    pt_widget_destroy(fx.select);
    date_log log = {0};
    log.destroy = true;
    pt_date_picker_set_on_change(picker, on_date_change, &log);
    pt_check(pt_date_picker_set_date(picker, (pt_date){2026, 11, 22}));
    pt_date_picker_open(picker);
    pt_check(pt_window_paint(fx.window, fx.canvas));
    pt_widget *day = find_named(picker_of(picker)->content, "2026-11-23");
    pt_check(day != NULL);
    if (day) {
        pt_check(pt_widget_activate(day));
    }
    pt_check(log.changes == 1);
    pt_check(!pt_widget_attached(fx.window, picker));
    pt_check(!pt_window_overlays_active(fx.window));
    fixture_close(&fx);

    pt_check(pt_date_is_valid((pt_date){2026, 11, 22}));
    pt_date_picker_set_date(NULL, (pt_date){2026, 1, 1});
    pt_date_picker_clear(NULL);
    pt_date_picker_set_format(NULL, pt_date_format_dmy);
    pt_date_picker_set_text(NULL, "2026-01-01");
    pt_date_picker_set_placeholder(NULL, "x");
    pt_date_picker_open(NULL);
    pt_date_picker_close(NULL);
    pt_date_picker_set_on_change(NULL, NULL, NULL);
    pt_check(pt_date_picker_get_date(NULL).year == 0);
    pt_check(pt_date_picker_get_text(NULL)[0] == '\0');
    pt_check(pt_date_picker_get_format(NULL) == pt_date_format_iso);
    pt_check(pt_date_picker_get_placeholder(NULL)[0] == '\0');
    pt_check(!pt_date_picker_is_open(NULL));
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
    test_date_values();
    test_date_keyboard(app);
    test_date_pointer_and_limits(app);
    test_date_callbacks_and_null(app);
    pt_app_destroy(app);
    return pt_test_finish("picker");
}