/*
 * Accessibility model checks: roles, names, states, text, values and events.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct a11y_log {
    int focus_changed;
    int state_changed;
    int text_changed;
    int selection_changed;
    int value_changed;
    int name_changed;
    int structure_changed;
    pt_widget *last_widget;
} a11y_log;

static void on_a11y_event(pt_window *window, pt_widget *widget, pt_a11y_event_type event,
                          void *data) {
    (void)window;
    a11y_log *log = data;
    log->last_widget = widget;
    switch (event) {
    case pt_a11y_event_focus_changed:
        ++log->focus_changed;
        break;
    case pt_a11y_event_state_changed:
        ++log->state_changed;
        break;
    case pt_a11y_event_text_changed:
        ++log->text_changed;
        break;
    case pt_a11y_event_selection_changed:
        ++log->selection_changed;
        break;
    case pt_a11y_event_value_changed:
        ++log->value_changed;
        break;
    case pt_a11y_event_name_changed:
        ++log->name_changed;
        break;
    case pt_a11y_event_structure_changed:
        ++log->structure_changed;
        break;
    }
}

static void test_roles_and_names(pt_app *app) {
    pt_window *window = pt_window_create(app, "a11y", 520, 640);
    pt_widget *root = pt_window_root(window);
    pt_widget *label = pt_label_create(root, "Name");
    pt_widget *button = pt_button_create(root, "Save");
    pt_widget *entry = pt_entry_create(root, "Search");
    pt_widget *toggle = pt_toggle_create(root);
    pt_widget *slider = pt_slider_create(root);
    pt_widget *scroll = pt_scroll_create(root);
    pt_widget *divider = pt_divider_create(root);
    pt_widget *card = pt_card_create(root);
    pt_widget *spacer = pt_spacer_create(root);
    pt_widget *checkbox = pt_checkbox_create(root, "Agree");
    pt_widget *radio = pt_radio_create(root, "One");
    pt_widget *progress = pt_progress_create(root);
    pt_widget *spinner = pt_spinner_create(root);
    const char *labels[] = {"A", "B", "C"};
    pt_widget *segmented = pt_segmented_create(root, labels, 3);

    pt_check(pt_widget_get_a11y_role(NULL) == pt_a11y_role_none);
    pt_check(pt_widget_get_a11y_role(label) == pt_a11y_role_label);
    pt_check(pt_widget_get_a11y_role(button) == pt_a11y_role_push_button);
    pt_check(pt_widget_get_a11y_role(entry) == pt_a11y_role_text_frame);
    pt_check(pt_widget_get_a11y_role(toggle) == pt_a11y_role_switch);
    pt_check(pt_widget_get_a11y_role(slider) == pt_a11y_role_slider);
    pt_check(pt_widget_get_a11y_role(scroll) == pt_a11y_role_scroll_pane);
    pt_check(pt_widget_get_a11y_role(divider) == pt_a11y_role_separator);
    pt_check(pt_widget_get_a11y_role(card) == pt_a11y_role_panel);
    pt_check(pt_widget_get_a11y_role(spacer) == pt_a11y_role_none);
    pt_check(pt_widget_get_a11y_role(checkbox) == pt_a11y_role_check_box);
    pt_check(pt_widget_get_a11y_role(radio) == pt_a11y_role_radio_button);
    pt_check(pt_widget_get_a11y_role(progress) == pt_a11y_role_progress_bar);
    pt_check(pt_widget_get_a11y_role(spinner) == pt_a11y_role_spinner);
    pt_check(pt_widget_get_a11y_role(segmented) == pt_a11y_role_group);
    pt_check(strcmp(pt_widget_get_a11y_name(label), "Name") == 0);
    pt_check(strcmp(pt_widget_get_a11y_name(button), "Save") == 0);
    pt_check(strcmp(pt_widget_get_a11y_name(entry), "Search") == 0);
    pt_check(strcmp(pt_widget_get_a11y_name(NULL), "") == 0);
    pt_check(strcmp(pt_widget_get_a11y_description(button), "") == 0);

    pt_widget_set_a11y_name(button, "Save document");
    pt_widget_set_a11y_description(button, "Writes changes to disk");
    pt_check(strcmp(pt_widget_get_a11y_name(button), "Save document") == 0);
    pt_check(strcmp(pt_widget_get_a11y_description(button),
                    "Writes changes to disk") == 0);
    pt_widget_set_a11y_role(button, pt_a11y_role_toggle_button);
    pt_check(pt_widget_get_a11y_role(button) == pt_a11y_role_toggle_button);

    uint32_t state = pt_widget_get_a11y_state(label);
    pt_check((state & pt_a11y_state_visible) != 0);
    pt_check((state & pt_a11y_state_enabled) != 0);
    pt_check((state & pt_a11y_state_focusable) == 0);
    state = pt_widget_get_a11y_state(button);
    pt_check((state & pt_a11y_state_focusable) != 0);
    pt_widget_set_visible(button, false);
    state = pt_widget_get_a11y_state(button);
    pt_check((state & pt_a11y_state_visible) == 0);
    pt_widget_set_visible(button, true);
    pt_widget_set_enabled(button, false);
    state = pt_widget_get_a11y_state(button);
    pt_check((state & pt_a11y_state_enabled) == 0);

    pt_widget_set_checked(toggle, true);
    pt_widget_set_checked(checkbox, true);
    pt_widget_set_checked(radio, true);
    pt_check((pt_widget_get_a11y_state(toggle) & pt_a11y_state_checked) != 0);
    pt_check((pt_widget_get_a11y_state(checkbox) & pt_a11y_state_checked) != 0);
    pt_check((pt_widget_get_a11y_state(radio) & pt_a11y_state_checked) != 0);
    pt_check((pt_widget_get_a11y_state(segmented) & pt_a11y_state_selected) != 0);
    pt_progress_set_indeterminate(progress, true);
    pt_check((pt_widget_get_a11y_state(progress) & pt_a11y_state_busy) != 0);
    pt_check((pt_widget_get_a11y_state(spinner) & pt_a11y_state_busy) != 0);

    pt_window_free(window);
}

static void test_text_and_values(pt_app *app) {
    pt_window *window = pt_window_create(app, "a11y.text", 480, 480);
    pt_widget *root = pt_window_root(window);
    pt_widget *label = pt_label_create(root, "Hello");
    pt_widget *entry = pt_entry_create(root, "Type");
    pt_widget *area = pt_text_area_create(root, "Notes");
    pt_widget *slider = pt_slider_create(root);
    pt_widget *progress = pt_progress_create(root);
    pt_widget *checkbox = pt_checkbox_create(root, "Check");
    const char *labels[] = {"A", "B", "C"};
    pt_widget *segmented = pt_segmented_create(root, labels, 3);

    const char *text = NULL;
    uint32_t caret = 99;
    uint32_t start = 99;
    uint32_t end = 99;
    pt_check(pt_widget_get_a11y_text(label, &text, &caret, &start, &end));
    pt_check(strcmp(text, "Hello") == 0);
    pt_check(caret == 0 && start == 0 && end == 0);
    pt_check(!pt_widget_get_a11y_text(root, &text, &caret, &start, &end));

    pt_widget_set_text(entry, "selection");
    pt_entry_set_selection(entry, 2, 6);
    pt_check(pt_widget_get_a11y_text(entry, &text, &caret, &start, &end));
    pt_check(strcmp(text, "selection") == 0);
    pt_check(start == 2 && end == 6);
    pt_check(caret <= 9);

    pt_widget_set_text(area, "caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c");
    pt_text_area_set_selection(area, 1, 6);
    pt_check(pt_widget_get_a11y_text(area, &text, &caret, &start, &end));
    pt_check(strcmp(text, "caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c") == 0);
    pt_check(start == 1 && end == 6);
    pt_check(caret == 6);
    uint32_t area_state = pt_widget_get_a11y_state(area);
    pt_check((area_state & pt_a11y_state_enabled) != 0);
    pt_check((area_state & pt_a11y_state_editable) != 0);
    pt_widget_set_enabled(area, false);
    area_state = pt_widget_get_a11y_state(area);
    pt_check((area_state & pt_a11y_state_editable) == 0);
    pt_widget_set_enabled(area, true);

    pt_entry_set_password(entry, true);
    pt_check(!pt_widget_get_a11y_text(entry, &text, &caret, &start, &end));

    float value = -1.0f;
    pt_slider_set_value(slider, 0.25f);
    pt_check(pt_widget_get_a11y_value(slider, &value));
    pt_check_near(value, 0.25, 0.0001);
    pt_progress_set_value(progress, 0.75f);
    pt_check(pt_widget_get_a11y_value(progress, &value));
    pt_check_near(value, 0.75, 0.0001);
    pt_check(pt_widget_get_a11y_value(checkbox, &value));
    pt_check_near(value, 0.0, 0.0001);
    pt_widget_set_checked(checkbox, true);
    pt_check(pt_widget_get_a11y_value(checkbox, &value));
    pt_check_near(value, 1.0, 0.0001);
    pt_check(pt_widget_get_a11y_value(segmented, &value));
    pt_check_near(value, 0.0, 0.0001);
    pt_check(!pt_widget_get_a11y_value(label, &value));

    a11y_log log = {0};
    pt_app_set_a11y_callback(app, on_a11y_event, &log);
    pt_widget_set_text(entry, "selection changed");
    pt_check(log.text_changed == 1);
    pt_check(log.last_widget == entry);
    pt_entry_set_selection(entry, 0, 4);
    pt_check(log.selection_changed == 1);
    pt_slider_set_value(slider, 0.5f);
    pt_check(log.value_changed == 1);
    pt_widget_set_checked(checkbox, false);
    pt_check(log.state_changed == 1);
    pt_widget_set_a11y_name(label, "Greeting");
    pt_check(log.name_changed == 1);
    pt_widget_set_a11y_role(label, pt_a11y_role_generic);
    pt_check(log.structure_changed == 1);
    pt_widget_focus(checkbox);
    pt_check(log.focus_changed == 1);
    pt_check((pt_widget_get_a11y_state(checkbox) & pt_a11y_state_focused) != 0);
    pt_app_set_a11y_callback(app, NULL, NULL);

    pt_window_free(window);
}

static void bind_list_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    char label[32];
    snprintf(label, sizeof(label), "Item %u", index);
    pt_widget_set_text(row, label);
    (void)list;
    (void)data;
}

static void bind_table_cell(pt_widget *table, pt_widget *cell, uint32_t row,
                            uint32_t column, void *data) {
    char label[32];
    snprintf(label, sizeof(label), "r%uc%u", row, column);
    pt_widget_set_text(cell, label);
    (void)table;
    (void)data;
}

/* Views only materialize the rows that fit, so the checks below look the row up
 * by the model index it was last bound to rather than by counting children. */
static pt_widget *bound_row(pt_widget *view, uint32_t index, pt_widget_kind kind) {
    for (pt_widget *row = view->first_child; row; row = row->next_sibling) {
        if (row->kind == kind && row->list_bound && row->list_index == index) {
            return row;
        }
    }
    return NULL;
}

static pt_widget *tree_row_for(pt_widget *tree, pt_tree_node *node) {
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_tree_row && row->list_bound &&
            row->tree_node == node) {
            return row;
        }
    }
    return NULL;
}

static void test_data_view_roles_and_states(pt_app *app) {
    pt_window *window = pt_window_create(app, "a11y.views", 520, 1100);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *list = pt_list_view_create(root);
    pt_widget_set_size(list, 320.0f, 200.0f);
    pt_widget_set_align_self(list, pt_align_start);
    pt_list_view_set_row_height(list, 40.0f);
    pt_list_view_set_row_count(list, 8);
    pt_list_view_set_on_bind_row(list, bind_list_row, NULL);

    pt_widget *table = pt_table_create(root);
    pt_widget_set_size(table, 420.0f, 200.0f);
    pt_widget_set_align_self(table, pt_align_start);
    pt_table_set_column_count(table, 2);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Size");
    pt_table_set_column_width(table, 0, 200.0f);
    pt_table_set_column_width(table, 1, 120.0f);
    pt_table_set_row_height(table, 30.0f);
    pt_table_set_header_height(table, 30.0f);
    pt_table_set_row_count(table, 8);
    pt_table_set_on_bind_cell(table, bind_table_cell, NULL);

    pt_widget *tree = pt_tree_create(root);
    pt_widget_set_size(tree, 300.0f, 300.0f);
    pt_widget_set_align_self(tree, pt_align_start);
    pt_tree_set_row_height(tree, 30.0f);
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

    pt_widget *sidebar = pt_sidebar_create(root);
    pt_widget_set_size(sidebar, 200.0f, 180.0f);
    pt_widget_set_align_self(sidebar, pt_align_start);
    pt_widget *side_files = pt_sidebar_add_item(sidebar, pt_icon_home, "Files");
    pt_widget *side_search = pt_sidebar_add_item(sidebar, pt_icon_document, "Search");

    pt_canvas *canvas = pt_canvas_create(520, 1100, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));

    pt_widget *list_row0 = bound_row(list, 0, pt_widget_list_row);
    pt_widget *table_row0 = bound_row(table, 0, pt_widget_table_row);
    pt_check(list_row0 != NULL && table_row0 != NULL);

    pt_check(pt_widget_get_a11y_role(list) == pt_a11y_role_list);
    pt_check(pt_widget_get_a11y_role(table) == pt_a11y_role_table);
    pt_check(pt_widget_get_a11y_role(tree) == pt_a11y_role_tree);
    pt_check(pt_widget_get_a11y_role(sidebar) == pt_a11y_role_list);
    if (list_row0) {
        pt_check(pt_widget_get_a11y_role(list_row0) == pt_a11y_role_list_item);
        pt_check(strcmp(pt_widget_get_a11y_name(list_row0), "Item 0") == 0);
    }
    if (table_row0) {
        pt_check(pt_widget_get_a11y_role(table_row0) == pt_a11y_role_table_row);
        pt_widget *cell = table_row0->first_child;
        pt_check(cell && pt_widget_get_a11y_role(cell) == pt_a11y_role_table_cell);
        pt_check(cell && strcmp(pt_widget_get_a11y_name(cell), "r0c0") == 0);
    }
    pt_widget *header = NULL;
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_table_header) {
            header = child;
            break;
        }
    }
    pt_check(header != NULL && header->first_child != NULL);
    if (header && header->first_child) {
        pt_check(pt_widget_get_a11y_role(header->first_child) ==
                 pt_a11y_role_column_header);
    }
    pt_widget *tree_leaf = tree_row_for(tree, leaves[0]);
    if (tree_leaf) {
        pt_check(pt_widget_get_a11y_role(tree_leaf) == pt_a11y_role_tree_item);
        pt_check(strcmp(pt_widget_get_a11y_name(tree_leaf), "A1") == 0);
    }
    pt_check(side_files && pt_widget_get_a11y_role(side_files) == pt_a11y_role_list_item);
    pt_check(side_files &&
             strcmp(pt_widget_get_a11y_name(side_files), "Files") == 0);

    /* The three data views hold a set of rows, so each one says more than one can
     * be selected. A sidebar or tab strip offers a single choice instead. */
    pt_check((pt_widget_get_a11y_state(list) & pt_a11y_state_multi_selectable) != 0);
    pt_check((pt_widget_get_a11y_state(table) & pt_a11y_state_multi_selectable) != 0);
    pt_check((pt_widget_get_a11y_state(tree) & pt_a11y_state_multi_selectable) != 0);
    pt_check((pt_widget_get_a11y_state(sidebar) & pt_a11y_state_multi_selectable) == 0);

    pt_list_view_select_range(list, 1, 3);
    pt_table_select_range(table, 1, 3);
    pt_tree_select_range(tree, 1, 3);
    pt_check(pt_list_view_get_selection_count(list) == 3);
    pt_check(pt_table_get_selection_count(table) == 3);
    pt_check(pt_tree_get_selection_count(tree) == 3);

    for (uint32_t index = 0; index < 4; ++index) {
        pt_widget *list_row = bound_row(list, index, pt_widget_list_row);
        pt_widget *table_row = bound_row(table, index, pt_widget_table_row);
        pt_widget *tree_row = bound_row(tree, index, pt_widget_tree_row);
        pt_check(list_row && table_row && tree_row);
        if (!list_row || !table_row || !tree_row) {
            continue;
        }
        /* Every row reads as one of the choices on offer; SELECTED then names the
         * ones the user has made. */
        uint32_t flags[] = {pt_widget_get_a11y_state(list_row),
                            pt_widget_get_a11y_state(table_row),
                            pt_widget_get_a11y_state(tree_row)};
        for (size_t i = 0; i < 3; ++i) {
            pt_check((flags[i] & pt_a11y_state_selectable) != 0);
            pt_check((flags[i] & pt_a11y_state_selected) ==
                     (index >= 1 && index <= 3 ? pt_a11y_state_selected : 0u));
        }
    }

    /* A branch reports both halves of the expand pair, a leaf reports neither. */
    pt_widget *branch = tree_row_for(tree, folder_a);
    pt_check(branch != NULL);
    if (branch) {
        uint32_t open = pt_widget_get_a11y_state(branch);
        pt_check((open & pt_a11y_state_expandable) != 0);
        pt_check((open & pt_a11y_state_expanded) != 0);
        pt_tree_node_set_expanded(folder_a, false);
        pt_check(pt_tree_get_visible_count(tree) == 4u);
        pt_check(pt_window_paint(window, canvas));
        pt_widget *closed = bound_row(tree, 0, pt_widget_tree_row);
        pt_check(closed && closed->tree_node == folder_a);
        if (closed) {
            uint32_t shut = pt_widget_get_a11y_state(closed);
            pt_check((shut & pt_a11y_state_expandable) != 0);
            pt_check((shut & pt_a11y_state_expanded) == 0);
        }
        pt_tree_node_set_expanded(folder_a, true);
        pt_check(pt_window_paint(window, canvas));
    }
    if (tree_leaf) {
        uint32_t leaf = pt_widget_get_a11y_state(tree_leaf);
        pt_check((leaf & pt_a11y_state_expandable) == 0);
        pt_check((leaf & pt_a11y_state_expanded) == 0);
    }

    pt_sidebar_set_active(sidebar, 1);
    if (side_files && side_search) {
        pt_check((pt_widget_get_a11y_state(side_search) & pt_a11y_state_selectable) != 0);
        pt_check((pt_widget_get_a11y_state(side_search) & pt_a11y_state_selected) != 0);
        pt_check((pt_widget_get_a11y_state(side_files) & pt_a11y_state_selected) == 0);
    }

    /* Disabling takes the choice away, not the selection: the set is kept and the
     * row still reads SELECTED, while ENABLED and MULTISELECTABLE say what the
     * view is willing to accept right now. */
    pt_widget_set_enabled(list, false);
    pt_widget *disabled_row = bound_row(list, 1, pt_widget_list_row);
    pt_check(disabled_row != NULL);
    if (disabled_row) {
        uint32_t state = pt_widget_get_a11y_state(disabled_row);
        pt_check((state & pt_a11y_state_enabled) == 0);
        pt_check((state & pt_a11y_state_selectable) == 0);
        pt_check((state & pt_a11y_state_selected) != 0);
    }
    pt_check((pt_widget_get_a11y_state(list) & pt_a11y_state_multi_selectable) != 0);
    pt_widget_set_enabled(list, true);
    if (disabled_row) {
        pt_check((pt_widget_get_a11y_state(disabled_row) & pt_a11y_state_selectable) != 0);
    }

    /* One row can be greyed out on its own, which is how a disabled item in a
     * menu-like list is meant to read. */
    pt_widget *single = bound_row(list, 2, pt_widget_list_row);
    if (single) {
        pt_widget_set_enabled(single, false);
        pt_check((pt_widget_get_a11y_state(single) & pt_a11y_state_selectable) == 0);
        pt_widget_set_enabled(single, true);
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.a11y");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("a11y");
    }
    test_roles_and_names(app);
    test_text_and_values(app);
    test_data_view_roles_and_states(app);
    pt_app_destroy(app);
    return pt_test_finish("a11y");
}
