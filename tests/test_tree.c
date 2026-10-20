/*
 * Tree view checks: hierarchy, expansion, selection, keys and virtualization.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct tree_state_log {
    int selects;
    int activates;
    pt_tree_node *last_select;
    pt_tree_node *last_activate;
} tree_state_log;

static void select_node(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    tree_state_log *log = data;
    ++log->selects;
    log->last_select = node;
}

static void activate_node(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    tree_state_log *log = data;
    ++log->activates;
    log->last_activate = node;
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

static pt_widget *visible_row(pt_widget *tree, uint32_t index) {
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_tree_row && row->visible && row->list_bound &&
            row->list_index == index) {
            return row;
        }
    }
    return NULL;
}

static int visible_rows(pt_widget *tree) {
    int count = 0;
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_tree_row && row->visible) {
            ++count;
        }
    }
    return count;
}

static int materialized_rows(pt_widget *tree) {
    int count = 0;
    for (pt_widget *row = tree->first_child; row; row = row->next_sibling) {
        if (row->kind == pt_widget_tree_row) {
            ++count;
        }
    }
    return count;
}

static void test_hierarchy(pt_app *app) {
    pt_window *window = pt_window_create(app, "tree", 480, 400);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *tree = pt_tree_create(root);
    pt_check(tree != NULL);
    pt_widget_set_align_self(tree, pt_align_start);
    pt_widget_set_width(tree, 320.0f);
    pt_widget_set_height(tree, 240.0f);
    pt_tree_set_row_height(tree, 32.0f);
    pt_tree_set_indent(tree, 22.0f);

    tree_state_log log = {0};
    pt_tree_set_on_select(tree, select_node, &log);
    pt_tree_set_on_activate(tree, activate_node, &log);

    pt_tree_node *alpha = pt_tree_add_node(tree, NULL, "alpha");
    pt_tree_node *alpha_one = pt_tree_add_node(tree, alpha, "alpha/one");
    pt_tree_node *alpha_deep = pt_tree_add_node(tree, alpha_one, "alpha/one/deep");
    pt_tree_node *alpha_two = pt_tree_add_node(tree, alpha, "alpha/two");
    pt_tree_node *beta = pt_tree_add_node(tree, NULL, "beta");
    pt_check(alpha && alpha_one && alpha_deep && alpha_two && beta);
    pt_check(pt_tree_add_node(tree, alpha_deep, "child") != NULL);
    pt_tree_node *fresh = pt_tree_node_get_child(alpha_deep, 0);

    pt_check(pt_tree_get_root_count(tree) == 2);
    pt_check(pt_tree_get_root_node(tree, 0) == alpha);
    pt_check(pt_tree_get_root_node(tree, 1) == beta);
    pt_check(pt_tree_get_root_node(tree, 2) == NULL);
    pt_check(strcmp(pt_tree_node_get_text(alpha), "alpha") == 0);
    pt_check(strcmp(pt_tree_node_get_text(alpha_one), "alpha/one") == 0);
    pt_check(pt_tree_node_has_children(alpha));
    pt_check(pt_tree_node_has_children(alpha_one));
    pt_check(!pt_tree_node_has_children(beta));
    pt_check(pt_tree_node_get_depth(alpha) == 0);
    pt_check(pt_tree_node_get_depth(alpha_one) == 1);
    pt_check(pt_tree_node_get_depth(alpha_deep) == 2);
    pt_check(pt_tree_node_parent(alpha_one) == alpha);
    pt_check(pt_tree_node_parent(alpha) == NULL);
    pt_check(pt_tree_node_get_child_count(alpha) == 2);
    pt_check(pt_tree_node_get_child(alpha, 0) == alpha_one);
    pt_check(pt_tree_node_get_child(alpha, 1) == alpha_two);
    pt_check(pt_tree_node_get_child(alpha, 2) == NULL);
    pt_check(fresh && pt_tree_node_get_child(alpha_deep, 0) == fresh);

    int payload = 42;
    pt_tree_node_set_data(beta, &payload);
    pt_check(pt_tree_node_get_data(beta) == &payload);
    pt_tree_node_set_text(beta, "beta renamed");
    pt_check(strcmp(pt_tree_node_get_text(beta), "beta renamed") == 0);

    /* Collapsed roots only; expansion adds descendants in order. */
    pt_check(pt_tree_get_visible_count(tree) == 2);
    pt_check(pt_tree_get_visible_node(tree, 0) == alpha);
    pt_check(pt_tree_get_visible_node(tree, 1) == beta);
    pt_check(pt_tree_get_visible_node(tree, 2) == NULL);
    pt_tree_node_set_expanded(alpha, true);
    pt_check(pt_tree_get_visible_count(tree) == 4);
    pt_check(pt_tree_get_visible_node(tree, 1) == alpha_one);
    pt_check(pt_tree_get_visible_node(tree, 2) == alpha_two);
    pt_tree_node_set_expanded(alpha_one, true);
    pt_check(pt_tree_get_visible_count(tree) == 5);
    pt_check(pt_tree_get_visible_node(tree, 2) == alpha_deep);
    pt_check(pt_tree_get_visible_node(tree, 3) == alpha_two);
    pt_check(pt_tree_get_visible_node(tree, 4) == beta);
    pt_check(pt_tree_node_get_child(alpha_deep, 0) == fresh);
    pt_check(!pt_tree_node_get_expanded(alpha_deep));

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(480, 400, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect tree_rect = bounds_of(tree);
    pt_check_near(tree_rect.x, 0.0, 0.01);
    pt_check_near(tree_rect.y, 44.0, 0.01);
    pt_check_near(tree_rect.w, 320.0, 0.01);
    pt_check_near(tree_rect.h, 240.0, 0.01);
    pt_check(visible_rows(tree) == 5);
    pt_check(materialized_rows(tree) == 5);

    pt_widget *row_alpha = visible_row(tree, 0);
    pt_check(row_alpha != NULL);
    if (row_alpha) {
        pt_check(row_alpha->tree_node == alpha);
        pt_check_near(bounds_of(row_alpha).h, 32.0, 0.01);
        pt_check(pt_widget_get_a11y_role(tree) == pt_a11y_role_tree);
        pt_check(pt_widget_get_a11y_role(row_alpha) == pt_a11y_role_tree_item);
        pt_check(strcmp(pt_widget_get_a11y_name(row_alpha), "alpha") == 0);
        uint32_t state = pt_widget_get_a11y_state(row_alpha);
        pt_check((state & pt_a11y_state_expandable) != 0);
        pt_check((state & pt_a11y_state_expanded) != 0);
    }

    /* Clicking the disclosure chevron collapses and selects the node. */
    pt_rect alpha_rect = bounds_of(row_alpha);
    click_at(window, alpha_rect.x + 10.0f, alpha_rect.y + alpha_rect.h * 0.5f);
    pt_check(!pt_tree_node_get_expanded(alpha));
    pt_check(pt_tree_get_visible_count(tree) == 2);
    pt_check(pt_tree_get_selected(tree) == alpha);
    pt_check(log.selects == 1);
    pt_check(log.last_select == alpha);
    pt_check(window->focused == tree);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(tree) == 2);
    row_alpha = visible_row(tree, 0);
    pt_check(row_alpha != NULL);
    if (row_alpha) {
        uint32_t state = pt_widget_get_a11y_state(row_alpha);
        pt_check((state & pt_a11y_state_expandable) != 0);
        pt_check((state & pt_a11y_state_expanded) == 0);
        pt_check((state & pt_a11y_state_selected) != 0);
    }

    /* Right expands or descends; Left collapses or returns to the parent. */
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(pt_tree_node_get_expanded(alpha));
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == alpha_one);
    pt_check(log.selects == 2);
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(!pt_tree_node_get_expanded(alpha_one));
    pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == alpha);
    pt_check(log.selects == 3);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == beta);
    pt_window_input_key(window, XKB_KEY_Return, 0, NULL, true);
    pt_check(log.activates == 1);
    pt_check(log.last_activate == beta);
    pt_check(pt_window_paint(window, canvas));
    pt_widget *row_beta = visible_row(tree, pt_tree_get_visible_count(tree) - 1);
    pt_check(row_beta != NULL && row_beta->tree_node == beta);

    /* A long flat list virtualizes and reveals through selection. */
    pt_tree_clear(tree);
    pt_check(pt_tree_get_root_count(tree) == 0);
    pt_check(pt_tree_get_visible_count(tree) == 0);
    pt_check(pt_tree_get_selected(tree) == NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(tree) == 0);
    pt_check(visible_row(tree, 0) == NULL);
    pt_check(pt_tree_add_node(tree, NULL, "fresh") != NULL);

    pt_tree_clear(tree);
    pt_tree_node *last = NULL;
    for (uint32_t i = 0; i < 60; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Node %u", i);
        pt_tree_node *node = pt_tree_add_node(tree, NULL, label);
        pt_check(node != NULL);
        if (i == 59) {
            last = node;
        }
    }
    pt_check(pt_tree_get_visible_count(tree) == 60);
    pt_widget_set_height(tree, 160.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_rows(tree) == 6);
    pt_check(materialized_rows(tree) == 6);
    pt_check(visible_row(tree, 0) != NULL && visible_row(tree, 0)->tree_node != NULL);
    pt_check(strcmp(pt_tree_node_get_text(visible_row(tree, 0)->tree_node), "Node 0") == 0);

    pt_tree_set_selected(tree, last);
    pt_check(pt_tree_get_selected(tree) == last);
    pt_check_near(tree->scroll_y, 1760.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_row(tree, 59) != NULL);
    pt_check(visible_row(tree, 0) == NULL);
    pt_check(materialized_rows(tree) <= 7);

    pt_tree_scroll_to_node(tree, last);
    pt_check_near(tree->scroll_y, 1760.0, 0.01);
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_root_node(tree, 0));
    pt_check_near(tree->scroll_y, 0.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_check(visible_row(tree, 0) != NULL);
    pt_check(visible_row(tree, 59) == NULL);

    /* Page keys move by a viewport and reveal the new selection. */
    pt_window_input_key(window, XKB_KEY_Page_Down, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_visible_node(tree, 5));
    pt_check(strcmp(pt_tree_node_get_text(pt_tree_get_selected(tree)), "Node 5") == 0);
    pt_window_input_key(window, XKB_KEY_Page_Down, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_visible_node(tree, 10));
    pt_window_input_key(window, XKB_KEY_Page_Up, 0, NULL, true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_visible_node(tree, 5));

    /* Type-ahead matches node labels and cycles repeated characters. */
    pt_tree_set_selected(tree, pt_tree_get_root_node(tree, 0));
    tree->typeahead_length = 0;
    pt_window_input_key(window, XKB_KEY_5, 0, "node 5", true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_visible_node(tree, 5));
    pt_tree_set_selected(tree, pt_tree_get_root_node(tree, 0));
    tree->typeahead_length = 0;
    pt_window_input_key(window, XKB_KEY_n, 0, "n", true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_visible_node(tree, 1));
    pt_window_input_key(window, XKB_KEY_n, 0, "n", true);
    pt_check(pt_tree_get_selected(tree) == pt_tree_get_visible_node(tree, 2));
    pt_check(window->focused == tree);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.tree");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("tree");
    }
    test_hierarchy(app);
    pt_app_destroy(app);
    return pt_test_finish("tree");
}
