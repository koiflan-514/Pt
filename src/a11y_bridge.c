/*
 * Pt accessibility bridge to AT-SPI2 over D-Bus.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The bridge exports the framework's semantic accessibility model as an
 * org.a11y.atspi object tree. Accessible objects are not registered one by
 * one: a fallback object below /org/pt/a11y resolves stable ids lazily, so
 * widgets and windows can come and go freely. The application root lives at
 * the standard path /org/a11y/atspi/accessible/root and registers itself with
 * the accessibility registry through org.a11y.atspi.Socket.Embed.
 */

#include "internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef PT_HAVE_DBUS

#include <dbus/dbus.h>

#define a11y_root_path "/org/a11y/atspi/accessible/root"
#define a11y_null_path "/org/a11y/atspi/null"
#define a11y_fallback_path "/org/pt/a11y"

enum {
    atspi_role_alert = 2,
    atspi_role_check_box = 7,
    atspi_role_combo_box = 11,
    atspi_role_dialog = 16,
    atspi_role_frame = 23,
    atspi_role_icon = 26,
    atspi_role_label = 29,
    atspi_role_list = 31,
    atspi_role_list_item = 32,
    atspi_role_menu = 33,
    atspi_role_menu_item = 35,
    atspi_role_page_tab = 37,
    atspi_role_page_tab_list = 38,
    atspi_role_panel = 39,
    atspi_role_password_text = 40,
    atspi_role_popup_menu = 41,
    atspi_role_progress_bar = 42,
    atspi_role_push_button = 43,
    atspi_role_radio_button = 44,
    atspi_role_scroll_pane = 49,
    atspi_role_separator = 50,
    atspi_role_filler = 20,
    atspi_role_slider = 51,
    atspi_role_table = 55,
    atspi_role_table_cell = 56,
    atspi_role_table_column_header = 57,
    atspi_role_text = 61,
    atspi_role_toggle_button = 62,
    atspi_role_tool_tip = 64,
    atspi_role_tree = 65,
    atspi_role_unknown = 67,
    atspi_role_application = 75,
    atspi_role_entry = 79,
    atspi_role_table_row = 90,
    atspi_role_tree_item = 91,
    atspi_role_list_box = 98,
    atspi_role_grouping = 99
};

enum {
    atspi_state_active = 1,    atspi_state_busy = 3,
    atspi_state_checked = 4,
    atspi_state_collapsed = 5,
    atspi_state_editable = 7,
    atspi_state_enabled = 8,
    atspi_state_expandable = 9,
    atspi_state_expanded = 10,
    atspi_state_focusable = 11,
    atspi_state_focused = 12,
    atspi_state_has_tooltip = 13,
    atspi_state_modal = 16,
    atspi_state_multiselectable = 18,
    atspi_state_pressed = 20,
    atspi_state_selectable = 22,
    atspi_state_selected = 23,
    atspi_state_sensitive = 24,
    atspi_state_showing = 25,
    atspi_state_visible = 30,
    atspi_state_indeterminate = 32,
    atspi_state_selectable_text = 38,
    atspi_state_checkable = 41,
    atspi_state_read_only = 43
};

enum {
    atspi_relation_label_for = 1,
    atspi_relation_labelled_by = 2,
    atspi_relation_member_of = 5,
    atspi_relation_node_child_of = 7,
    atspi_relation_node_parent_of = 8,
    atspi_relation_subwindow_of = 12,
    atspi_relation_popup_for = 15
};

typedef enum pt_a11y_obj_type {
    pt_a11y_obj_none = 0,
    pt_a11y_obj_app,
    pt_a11y_obj_window,
    pt_a11y_obj_widget
} pt_a11y_obj_type;

typedef struct pt_a11y_obj {
    pt_a11y_obj_type type;
    pt_window *window;
    pt_widget *widget;
} pt_a11y_obj;

typedef struct pt_a11y_bridge {
    pt_app *app;
    DBusConnection *bus;
    const char *bus_name;
    int32_t application_id;
    DBusObjectPathVTable vtable;
} pt_a11y_bridge;

/* ---------- paths ---------- */

static void a11y_window_path(const pt_window *window, char *buffer, size_t size) {
    snprintf(buffer, size, a11y_fallback_path "/w%u", window ? window->a11y_id : 0u);
}

static void a11y_widget_path(const pt_widget *widget, char *buffer, size_t size) {
    pt_window *window = pt_widget_window(widget);
    snprintf(buffer, size, a11y_fallback_path "/w%u/n%u", window ? window->a11y_id : 0u,
             widget ? widget->a11y_id : 0u);
}

static bool parse_uint32(const char **cursor, uint32_t *out) {
    const char *p = *cursor;
    if (!p || !isdigit((unsigned char)*p)) {
        return false;
    }
    unsigned long value = 0;
    while (isdigit((unsigned char)*p)) {
        value = value * 10u + (unsigned long)(*p - '0');
        if (value > UINT32_MAX) {
            return false;
        }
        ++p;
    }
    *cursor = p;
    *out = (uint32_t)value;
    return true;
}

static pt_window *a11y_find_window(pt_app *app, uint32_t id) {
    for (pt_window *window = app ? app->first_window : NULL; window; window = window->next) {
        if (window->a11y_id == id) {
            return window;
        }
    }
    return NULL;
}

static pt_widget *a11y_find_widget_in_tree(pt_widget *root, uint32_t id) {
    if (!root) {
        return NULL;
    }
    if (root->a11y_id == id) {
        return root;
    }
    for (pt_widget *child = root->first_child; child; child = child->next_sibling) {
        pt_widget *found = a11y_find_widget_in_tree(child, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static pt_widget *a11y_find_widget(pt_window *window, uint32_t id) {
    if (!window) {
        return NULL;
    }
    pt_widget *found = a11y_find_widget_in_tree(window->root, id);
    if (found) {
        return found;
    }
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (!overlay->open || !overlay->panel) {
            continue;
        }
        found = a11y_find_widget_in_tree(overlay->panel, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static pt_a11y_obj a11y_resolve(pt_app *app, const char *path) {
    pt_a11y_obj obj = {0};
    if (!app || !path) {
        return obj;
    }
    if (strcmp(path, a11y_root_path) == 0) {
        obj.type = pt_a11y_obj_app;
        return obj;
    }
    const char *prefix = a11y_fallback_path "/w";
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0) {
        return obj;
    }
    const char *cursor = path + prefix_len;
    uint32_t window_id = 0;
    if (!parse_uint32(&cursor, &window_id)) {
        return obj;
    }
    pt_window *window = a11y_find_window(app, window_id);
    if (!window) {
        return obj;
    }
    if (*cursor == '\0') {
        obj.type = pt_a11y_obj_window;
        obj.window = window;
        return obj;
    }
    if (strncmp(cursor, "/n", 2) != 0) {
        return obj;
    }
    cursor += 2;
    uint32_t widget_id = 0;
    if (!parse_uint32(&cursor, &widget_id) || *cursor != '\0') {
        return obj;
    }
    pt_widget *widget = a11y_find_widget(window, widget_id);
    if (!widget) {
        return obj;
    }
    obj.type = pt_a11y_obj_widget;
    obj.window = window;
    obj.widget = widget;
    return obj;
}

static bool a11y_obj_path(const pt_a11y_obj *obj, char *buffer, size_t size) {
    switch (obj->type) {
    case pt_a11y_obj_app:
        snprintf(buffer, size, "%s", a11y_root_path);
        return true;
    case pt_a11y_obj_window:
        a11y_window_path(obj->window, buffer, size);
        return true;
    case pt_a11y_obj_widget:
        a11y_widget_path(obj->widget, buffer, size);
        return true;
    default:
        snprintf(buffer, size, "%s", a11y_null_path);
        return false;
    }
}

/* ---------- tree walking ---------- */

static int a11y_overlay_count(const pt_window *window) {
    int count = 0;
    for (pt_overlay *overlay = window ? window->overlays : NULL; overlay;
         overlay = overlay->next) {
        if (overlay->open && overlay->panel) {
            ++count;
        }
    }
    return count;
}

static pt_overlay *a11y_overlay_at(pt_window *window, int index, int *index_in_parent) {
    int cursor = window->root ? 1 : 0;
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (!overlay->open || !overlay->panel) {
            continue;
        }
        if (cursor == index) {
            if (index_in_parent) {
                *index_in_parent = cursor;
            }
            return overlay;
        }
        ++cursor;
    }
    return NULL;
}

/* Data views keep a pool of row widgets; unbound rows are layout
 * placeholders, not accessible children. */
static bool a11y_child_is_real(const pt_widget *child) {
    if (!child) {
        return false;
    }
    switch (child->kind) {
    case pt_widget_table_row:
    case pt_widget_list_row:
        return child->list_bound && child->list_index != pt_list_index_none;
    case pt_widget_tree_row:
        return child->tree_node != NULL;
    default:
        return true;
    }
}

static int a11y_child_count(const pt_a11y_obj *obj) {
    switch (obj->type) {
    case pt_a11y_obj_window:
        return (obj->window->root ? 1 : 0) + a11y_overlay_count(obj->window);
    case pt_a11y_obj_widget: {
        int count = 0;
        for (pt_widget *child = obj->widget->first_child; child; child = child->next_sibling) {
            if (a11y_child_is_real(child)) {
                ++count;
            }
        }
        return count;
    }
    default:
        return 0;
    }
}

static int a11y_app_child_count(const pt_app *app) {
    int count = 0;
    for (pt_window *window = app ? app->first_window : NULL; window; window = window->next) {
        ++count;
    }
    return count;
}

static pt_window *a11y_app_child_at(pt_app *app, int index) {
    int cursor = 0;
    for (pt_window *window = app ? app->first_window : NULL; window; window = window->next) {
        if (cursor == index) {
            return window;
        }
        ++cursor;
    }
    return NULL;
}

static pt_widget *a11y_widget_child_at(pt_widget *widget, int index) {
    int cursor = 0;
    for (pt_widget *child = widget ? widget->first_child : NULL; child;
         child = child->next_sibling) {
        if (!a11y_child_is_real(child)) {
            continue;
        }
        if (cursor == index) {
            return child;
        }
        ++cursor;
    }
    return NULL;
}

static pt_a11y_obj a11y_child_at(pt_app *app, const pt_a11y_obj *parent, int index) {
    pt_a11y_obj child = {0};
    if (!parent || index < 0) {
        return child;
    }
    switch (parent->type) {
    case pt_a11y_obj_app: {
        pt_window *window = a11y_app_child_at(app, index);
        if (window) {
            child.type = pt_a11y_obj_window;
            child.window = window;
        }
        break;
    }
    case pt_a11y_obj_window:
        if (index == 0 && parent->window->root) {
            child.type = pt_a11y_obj_widget;
            child.window = parent->window;
            child.widget = parent->window->root;
        } else {
            pt_overlay *overlay = a11y_overlay_at(parent->window, index, NULL);
            if (overlay) {
                child.type = pt_a11y_obj_widget;
                child.window = parent->window;
                child.widget = overlay->panel;
            }
        }
        break;
    case pt_a11y_obj_widget: {
        pt_widget *widget = a11y_widget_child_at(parent->widget, index);
        if (widget) {
            child.type = pt_a11y_obj_widget;
            child.window = parent->window;
            child.widget = widget;
        }
        break;
    }
    default:
        break;
    }
    return child;
}

static int a11y_child_index(const pt_a11y_obj *obj) {
    if (!obj) {
        return -1;
    }
    switch (obj->type) {
    case pt_a11y_obj_window: {
        pt_app *app = obj->window->app;
        int index = 0;
        for (pt_window *window = app ? app->first_window : NULL; window;
             window = window->next) {
            if (window == obj->window) {
                return index;
            }
            ++index;
        }
        return -1;
    }
    case pt_a11y_obj_widget: {
        pt_widget *widget = obj->widget;
        if (widget->parent) {
            int index = 0;
            for (pt_widget *child = widget->parent->first_child; child;
                 child = child->next_sibling) {
                if (!a11y_child_is_real(child)) {
                    continue;
                }
                if (child == widget) {
                    return index;
                }
                ++index;
            }
            return -1;
        }
        pt_window *window = obj->window;
        if (window->root == widget) {
            return 0;
        }
        int index = window->root ? 1 : 0;
        for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
            if (!overlay->open || !overlay->panel) {
                continue;
            }
            if (overlay->panel == widget) {
                return index;
            }
            ++index;
        }
        return -1;
    }
    default:
        return -1;
    }
}

static void a11y_parent_ref(const pt_a11y_bridge *bridge, const pt_a11y_obj *obj,
                            char *bus_buffer, size_t bus_size, char *path_buffer,
                            size_t path_size) {
    snprintf(bus_buffer, bus_size, "%s", "");
    snprintf(path_buffer, path_size, "%s", a11y_null_path);
    if (!obj || !bridge) {
        return;
    }
    if (obj->type == pt_a11y_obj_window) {
        snprintf(bus_buffer, bus_size, "%s", bridge->bus_name ? bridge->bus_name : "");
        snprintf(path_buffer, path_size, "%s", a11y_root_path);
        return;
    }
    if (obj->type != pt_a11y_obj_widget) {
        return;
    }
    pt_widget *widget = obj->widget;
    pt_a11y_obj parent = {0};
    if (widget->parent) {
        parent.type = pt_a11y_obj_widget;
        parent.window = obj->window;
        parent.widget = widget->parent;
    } else {
        parent.type = pt_a11y_obj_window;
        parent.window = obj->window;
    }
    a11y_obj_path(&parent, path_buffer, path_size);
    snprintf(bus_buffer, bus_size, "%s", bridge->bus_name ? bridge->bus_name : "");
}

/* ---------- semantics ---------- */

static bool a11y_obj_has_text(const pt_a11y_obj *obj) {
    if (!obj || obj->type != pt_a11y_obj_widget) {
        return false;
    }
    pt_widget *widget = obj->widget;
    return pt_widget_kind_is_text_input(widget->kind) || widget->kind == pt_widget_label ||
           widget->kind == pt_widget_button;
}

static bool a11y_obj_has_value(const pt_a11y_obj *obj) {
    if (!obj || obj->type != pt_a11y_obj_widget) {
        return false;
    }
    switch (obj->widget->kind) {
    case pt_widget_slider:
    case pt_widget_progress:
    case pt_widget_toggle:
    case pt_widget_checkbox:
    case pt_widget_radio:
    case pt_widget_segmented:
        return true;
    default:
        return false;
    }
}

static bool a11y_obj_actionable(const pt_a11y_obj *obj) {
    if (!obj || obj->type != pt_a11y_obj_widget) {
        return false;
    }
    switch (obj->widget->kind) {
    case pt_widget_button:
    case pt_widget_toggle:
    case pt_widget_checkbox:
    case pt_widget_radio:
    case pt_widget_list:
    case pt_widget_list_row:
    case pt_widget_table:
    case pt_widget_table_row:
    case pt_widget_tree:
    case pt_widget_tree_row:
    case pt_widget_tabs:
    case pt_widget_sidebar:
    case pt_widget_sidebar_item:
    case pt_widget_combobox:
        return obj->widget->enabled;
    default:
        return false;
    }
}

static bool a11y_obj_focusable(const pt_a11y_obj *obj) {
    return obj && obj->type == pt_a11y_obj_widget && obj->widget->focusable &&
           obj->widget->enabled && obj->widget->visible;
}

/* ---------- data view helpers ---------- */

static pt_widget *a11y_obj_table_widget(const pt_a11y_obj *obj) {
    if (!obj || obj->type != pt_a11y_obj_widget || obj->widget->kind != pt_widget_table) {
        return NULL;
    }
    return obj->widget;
}

static pt_table_state *a11y_table_state(pt_widget *table) {
    return table && table->kind == pt_widget_table ? table->extra : NULL;
}

static pt_widget *a11y_table_row_widget(pt_widget *table, int32_t row) {
    if (!table || row < 0 || (uint64_t)row >= table->row_count) {
        return NULL;
    }
    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_table_row && child->list_bound &&
            child->list_index == (uint32_t)row) {
            return child;
        }
    }
    return NULL;
}

static pt_widget *a11y_table_cell_widget(pt_widget *table, int32_t row, int32_t column) {
    pt_table_state *state = a11y_table_state(table);
    if (!state || column < 0 || (uint32_t)column >= state->column_count) {
        return NULL;
    }
    pt_widget *row_widget = a11y_table_row_widget(table, row);
    for (pt_widget *cell = row_widget ? row_widget->first_child : NULL; cell;
         cell = cell->next_sibling) {
        if (cell->kind == pt_widget_table_cell && cell->column_index == (uint32_t)column) {
            return cell;
        }
    }
    return NULL;
}

static pt_widget *a11y_table_header_cell_widget(pt_widget *table, int32_t column) {
    pt_table_state *state = a11y_table_state(table);
    if (!state || !state->header || column < 0 || (uint32_t)column >= state->column_count) {
        return NULL;
    }
    for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
        if (cell->kind == pt_widget_table_header_cell &&
            cell->column_index == (uint32_t)column) {
            return cell;
        }
    }
    return NULL;
}

static pt_widget *a11y_table_of_cell(const pt_widget *cell) {
    if (!cell || !cell->parent) {
        return NULL;
    }
    if (cell->kind == pt_widget_table_cell && cell->parent->kind == pt_widget_table_row &&
        cell->parent->parent && cell->parent->parent->kind == pt_widget_table) {
        return cell->parent->parent;
    }
    if (cell->kind == pt_widget_table_header_cell &&
        cell->parent->kind == pt_widget_table_header && cell->parent->parent &&
        cell->parent->parent->kind == pt_widget_table) {
        return cell->parent->parent;
    }
    return NULL;
}

static int32_t a11y_table_cell_row(const pt_widget *cell) {
    if (!cell || cell->kind != pt_widget_table_cell || !cell->parent ||
        cell->parent->kind != pt_widget_table_row ||
        cell->parent->list_index == pt_list_index_none) {
        return -1;
    }
    return (int32_t)cell->parent->list_index;
}

static int32_t a11y_table_cell_column(const pt_widget *cell) {
    if (!cell) {
        return -1;
    }
    if (cell->kind != pt_widget_table_cell && cell->kind != pt_widget_table_header_cell) {
        return -1;
    }
    return (int32_t)cell->column_index;
}

static pt_widget *a11y_tree_row_for_node(pt_widget *tree, pt_tree_node *node) {
    if (!tree || !node) {
        return NULL;
    }
    for (pt_widget *child = tree->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_tree_row && child->tree_node == node) {
            return child;
        }
    }
    return NULL;
}

static bool a11y_obj_is_selection(const pt_a11y_obj *obj) {
    if (!obj || obj->type != pt_a11y_obj_widget) {
        return false;
    }
    switch (obj->widget->kind) {
    case pt_widget_table:
    case pt_widget_list:
    case pt_widget_tree:
    case pt_widget_sidebar:
    case pt_widget_tabs:
        return true;
    default:
        return false;
    }
}

/* Whether a child of a selection container answers as selected right now. Rows
 * carry the flag themselves, tabs and sidebar items are compared against the
 * view's single active index. */
static bool a11y_child_selected(const pt_widget *container, const pt_widget *child) {
    switch (child->kind) {
    case pt_widget_table_row:
    case pt_widget_list_row:
    case pt_widget_tree_row:
        return child->row_selected;
    case pt_widget_sidebar_item:
        return container->kind == pt_widget_sidebar && child->row_selected;
    case pt_widget_tab_page: {
        if (container->kind != pt_widget_tabs) {
            return false;
        }
        uint32_t index = 0;
        for (const pt_widget *page = container->first_child; page && page != child;
             page = page->next_sibling) {
            if (page->kind == pt_widget_tab_page) {
                ++index;
            }
        }
        return index == pt_tabs_get_active(container);
    }
    default:
        return false;
    }
}

static pt_widget *a11y_selection_nth(const pt_a11y_obj *obj, int index) {
    if (!a11y_obj_is_selection(obj) || index < 0) {
        return NULL;
    }
    int seen = 0;
    for (pt_widget *child = obj->widget->first_child; child;
         child = child->next_sibling) {
        if (!a11y_child_is_real(child) || !a11y_child_selected(obj->widget, child)) {
            continue;
        }
        if (seen++ == index) {
            return child;
        }
    }
    return NULL;
}

/* How many children of a selection container answer as selected. A data view
 * keeps a set, so more than one can be selected at a time. */
static int a11y_selection_count(const pt_a11y_obj *obj) {
    if (!a11y_obj_is_selection(obj)) {
        return 0;
    }
    int count = 0;
    for (pt_widget *child = obj->widget->first_child; child;
         child = child->next_sibling) {
        if (a11y_child_is_real(child) && a11y_child_selected(obj->widget, child)) {
            ++count;
        }
    }
    return count;
}

/* The child the view names as its caret row, falling back to the first selected
 * one. A SelectionChanged listener wants the row a gesture just moved to, which
 * is not necessarily the top of a multi-row set. */
static pt_widget *a11y_selection_primary(const pt_a11y_obj *obj) {
    if (!a11y_obj_is_selection(obj)) {
        return NULL;
    }
    pt_widget *container = obj->widget;
    switch (container->kind) {
    case pt_widget_table:
    case pt_widget_list: {
        pt_widget_kind row_kind = container->kind == pt_widget_table ? pt_widget_table_row
                                                                      : pt_widget_list_row;
        for (pt_widget *child = container->first_child; child;
             child = child->next_sibling) {
            if (child->kind == row_kind && child->list_bound &&
                child->list_index == container->list_selected) {
                return child;
            }
        }
        break;
    }
    case pt_widget_tree: {
        pt_tree_state *state = container->extra;
        pt_widget *row = state ? a11y_tree_row_for_node(container, state->selected) : NULL;
        if (row) {
            return row;
        }
        break;
    }
    default:
        break;
    }
    return a11y_selection_nth(obj, 0);
}

static bool a11y_selection_select(const pt_a11y_obj *obj, int child_index) {
    if (!a11y_obj_is_selection(obj) || child_index < 0) {
        return false;
    }
    pt_widget *container = obj->widget;
    pt_widget *child = a11y_widget_child_at(container, child_index);
    if (!child) {
        return false;
    }
    switch (container->kind) {
    case pt_widget_table:
        if (child->kind != pt_widget_table_row || !child->list_bound ||
            child->list_index == pt_list_index_none) {
            return false;
        }
        pt_table_select(container, child->list_index, false);
        return true;
    case pt_widget_list:
        if (child->kind != pt_widget_list_row || !child->list_bound ||
            child->list_index == pt_list_index_none) {
            return false;
        }
        pt_list_view_set_selected(container, child->list_index);
        return true;
    case pt_widget_tree:
        if (child->kind != pt_widget_tree_row || !child->tree_node) {
            return false;
        }
        pt_tree_select(container, child->tree_node, false);
        return true;
    case pt_widget_sidebar:
        if (child->kind != pt_widget_sidebar_item) {
            return false;
        }
        pt_sidebar_select(container, (uint32_t)child_index, false);
        return true;
    case pt_widget_tabs: {
        if (child->kind != pt_widget_tab_page) {
            return false;
        }
        uint32_t page_index = 0;
        for (pt_widget *page = container->first_child; page && page != child;
             page = page->next_sibling) {
            if (page->kind == pt_widget_tab_page) {
                ++page_index;
            }
        }
        pt_tabs_select(container, page_index, false);
        return true;
    }
    default:
        return false;
    }
}

static bool a11y_selection_clear(const pt_a11y_obj *obj) {
    if (!a11y_obj_is_selection(obj)) {
        return false;
    }
    pt_widget *container = obj->widget;
    switch (container->kind) {
    case pt_widget_table:
        if (pt_table_get_selection_count(container) == 0) {
            return false;
        }
        pt_table_clear_selection(container);
        return true;
    case pt_widget_list:
        if (pt_list_view_get_selection_count(container) == 0) {
            return false;
        }
        pt_list_view_clear_selection(container);
        return true;
    case pt_widget_tree:
        if (pt_tree_get_selection_count(container) == 0) {
            return false;
        }
        pt_tree_clear_selection(container);
        return true;
    case pt_widget_sidebar:
        if (pt_sidebar_get_active(container) == pt_list_index_none) {
            return false;
        }
        pt_sidebar_select(container, pt_list_index_none, false);
        return true;
    default:
        return false;
    }
}

/* Take one child out of the set without disturbing the rest, which is what
 * org.a11y.atspi.Selection expects from DeselectChild and
 * DeselectSelectedChild. A tab panel always keeps an active page, so there is
 * nothing to take back there. */
static bool a11y_selection_remove(const pt_a11y_obj *obj, pt_widget *child) {
    if (!a11y_obj_is_selection(obj) || !child || child->parent != obj->widget) {
        return false;
    }
    pt_widget *container = obj->widget;
    if (!a11y_child_selected(container, child)) {
        return false;
    }
    switch (child->kind) {
    case pt_widget_table_row:
        if (child->list_index == pt_list_index_none) {
            return false;
        }
        pt_table_select_toggle(container, child->list_index);
        return true;
    case pt_widget_list_row:
        if (child->list_index == pt_list_index_none) {
            return false;
        }
        pt_list_view_select_toggle(container, child->list_index);
        return true;
    case pt_widget_tree_row:
        pt_tree_select_toggle(container, child->tree_node);
        return true;
    case pt_widget_sidebar_item:
        return a11y_selection_clear(obj);
    default:
        return false;
    }
}

/* SelectAll covers the row views; the sidebar and the tab strip choose one entry
 * at a time, so the call has nothing to do there. */
static bool a11y_selection_select_all(const pt_a11y_obj *obj) {
    if (!a11y_obj_is_selection(obj)) {
        return false;
    }
    pt_widget *container = obj->widget;
    switch (container->kind) {
    case pt_widget_table:
        if (pt_table_get_row_count(container) == 0) {
            return false;
        }
        pt_table_select_range(container, 0, pt_table_get_row_count(container) - 1u);
        return true;
    case pt_widget_list:
        if (pt_list_view_get_row_count(container) == 0) {
            return false;
        }
        pt_list_view_select_range(container, 0,
                                  pt_list_view_get_row_count(container) - 1u);
        return true;
    case pt_widget_tree:
        if (pt_tree_get_visible_count(container) == 0) {
            return false;
        }
        pt_tree_select_range(container, 0, pt_tree_get_visible_count(container) - 1u);
        return true;
    default:
        return false;
    }
}

static bool a11y_selection_deselect_child(const pt_a11y_obj *obj, int child_index) {
    if (!a11y_obj_is_selection(obj) || obj->widget->kind == pt_widget_tabs) {
        return false;
    }
    return a11y_selection_remove(obj, a11y_widget_child_at(obj->widget, child_index));
}

static int a11y_action_count(const pt_a11y_obj *obj) {
    int count = 0;
    if (a11y_obj_actionable(obj)) {
        ++count;
    }
    if (a11y_obj_focusable(obj) && !a11y_obj_actionable(obj)) {
        ++count;
    }
    return count;
}

static const char *a11y_action_name(const pt_a11y_obj *obj, int index) {
    if (index != 0) {
        return "";
    }
    return a11y_obj_actionable(obj) ? "click" : "focus";
}

static uint32_t a11y_role_number(const pt_a11y_obj *obj) {
    if (!obj || obj->type == pt_a11y_obj_none) {
        return atspi_role_unknown;
    }
    if (obj->type == pt_a11y_obj_app) {
        return atspi_role_application;
    }
    if (obj->type == pt_a11y_obj_window) {
        return atspi_role_frame;
    }
    pt_widget *widget = obj->widget;
    if (widget->kind == pt_widget_entry && widget->password) {
        return atspi_role_password_text;
    }
    switch (pt_widget_get_a11y_role(widget)) {
    case pt_a11y_role_label:
        return atspi_role_label;
    case pt_a11y_role_push_button:
        return atspi_role_push_button;
    case pt_a11y_role_text:
    case pt_a11y_role_text_frame:
        return atspi_role_entry;
    case pt_a11y_role_list:
        return atspi_role_list;
    case pt_a11y_role_list_box:
        return atspi_role_list_box;
    case pt_a11y_role_list_item:
        return atspi_role_list_item;
    case pt_a11y_role_toggle_button:
    case pt_a11y_role_switch:
        return atspi_role_toggle_button;
    case pt_a11y_role_slider:
        return atspi_role_slider;
    case pt_a11y_role_scroll_pane:
        return atspi_role_scroll_pane;
    case pt_a11y_role_separator:
        return atspi_role_separator;
    case pt_a11y_role_check_box:
        return atspi_role_check_box;
    case pt_a11y_role_radio_button:
        return atspi_role_radio_button;
    case pt_a11y_role_progress_bar:
        return atspi_role_progress_bar;
    case pt_a11y_role_spinner:
        return atspi_role_progress_bar;
    case pt_a11y_role_group:
        return atspi_role_grouping;
    case pt_a11y_role_menu:
        return atspi_role_popup_menu;
    case pt_a11y_role_menu_item:
        return atspi_role_menu_item;
    case pt_a11y_role_dialog:
        return atspi_role_dialog;
    case pt_a11y_role_tooltip:
        return atspi_role_tool_tip;
    case pt_a11y_role_alert:
        return atspi_role_alert;
    case pt_a11y_role_panel:
        return atspi_role_panel;
    case pt_a11y_role_icon:
        return atspi_role_icon;
    case pt_a11y_role_table:
        return atspi_role_table;
    case pt_a11y_role_table_cell:
        return atspi_role_table_cell;
    case pt_a11y_role_table_row:
        return atspi_role_table_row;
    case pt_a11y_role_column_header:
        return atspi_role_table_column_header;
    case pt_a11y_role_tree:
        return atspi_role_tree;
    case pt_a11y_role_tree_item:
        return atspi_role_tree_item;
    case pt_a11y_role_tab_list:
        return atspi_role_page_tab_list;
    case pt_a11y_role_tab:
        return atspi_role_page_tab;
    case pt_a11y_role_tab_panel:
        return atspi_role_panel;
    case pt_a11y_role_combo_box:
        return atspi_role_combo_box;
    case pt_a11y_role_generic:
        return atspi_role_filler;
    case pt_a11y_role_none:
    default:
        return atspi_role_unknown;
    }
}

static const char *a11y_role_name(uint32_t role) {
    switch (role) {
    case atspi_role_alert:
        return "alert";
    case atspi_role_check_box:
        return "check box";
    case atspi_role_combo_box:
        return "combo box";
    case atspi_role_dialog:
        return "dialog";
    case atspi_role_frame:
        return "frame";
    case atspi_role_label:
        return "label";
    case atspi_role_list:
        return "list";
    case atspi_role_list_box:
        return "list box";
    case atspi_role_list_item:
        return "list item";
    case atspi_role_filler:
        return "filler";
    case atspi_role_menu:
        return "menu";
    case atspi_role_menu_item:
        return "menu item";
    case atspi_role_panel:
        return "panel";
    case atspi_role_password_text:
        return "password text";
    case atspi_role_popup_menu:
        return "popup menu";
    case atspi_role_progress_bar:
        return "progress bar";
    case atspi_role_push_button:
        return "button";
    case atspi_role_radio_button:
        return "radio button";
    case atspi_role_scroll_pane:
        return "scroll pane";
    case atspi_role_separator:
        return "separator";
    case atspi_role_slider:
        return "slider";
    case atspi_role_text:
        return "text";
    case atspi_role_toggle_button:
        return "toggle button";
    case atspi_role_tool_tip:
        return "tool tip";
    case atspi_role_application:
        return "application";
    case atspi_role_entry:
        return "entry";
    case atspi_role_grouping:
        return "grouping";
    case atspi_role_icon:
        return "icon";
    case atspi_role_page_tab:
        return "page tab";
    case atspi_role_page_tab_list:
        return "page tab list";
    case atspi_role_table:
        return "table";
    case atspi_role_table_cell:
        return "table cell";
    case atspi_role_table_column_header:
        return "table column header";
    case atspi_role_table_row:
        return "table row";
    case atspi_role_tree:
        return "tree";
    case atspi_role_tree_item:
        return "tree item";
    default:
        return "unknown";
    }
}

static const char *a11y_obj_name(const pt_a11y_obj *obj) {
    if (!obj) {
        return "";
    }
    switch (obj->type) {
    case pt_a11y_obj_app:
        return "";
    case pt_a11y_obj_window:
        return obj->window && obj->window->title ? obj->window->title : "";
    case pt_a11y_obj_widget:
        return pt_widget_get_a11y_name(obj->widget);
    default:
        return "";
    }
}

static const char *a11y_obj_description(const pt_a11y_obj *obj) {
    if (!obj) {
        return "";
    }
    switch (obj->type) {
    case pt_a11y_obj_app:
        return "Pt application";
    case pt_a11y_obj_window:
        return "Pt window";
    case pt_a11y_obj_widget:
        return pt_widget_get_a11y_description(obj->widget);
    default:
        return "";
    }
}

static void a11y_obj_id_string(const pt_a11y_obj *obj, char *buffer, size_t size) {
    if (!obj) {
        snprintf(buffer, size, "%s", "");
        return;
    }
    switch (obj->type) {
    case pt_a11y_obj_app:
        snprintf(buffer, size, "%s", "root");
        break;
    case pt_a11y_obj_window:
        snprintf(buffer, size, "w%u", obj->window->a11y_id);
        break;
    case pt_a11y_obj_widget:
        snprintf(buffer, size, "n%u", obj->widget->a11y_id);
        break;
    default:
        snprintf(buffer, size, "%s", "");
        break;
    }
}

static void a11y_app_name(const pt_app *app, char *buffer, size_t size) {
    snprintf(buffer, size, "%s", app && app->app_id ? app->app_id : "pt.application");
}

static void a11y_obj_name_buffer(const pt_app *app, const pt_a11y_obj *obj, char *buffer,
                                 size_t size) {
    if (obj && obj->type == pt_a11y_obj_app) {
        a11y_app_name(app, buffer, size);
        return;
    }
    snprintf(buffer, size, "%s", a11y_obj_name(obj));
}

static void a11y_state_words(const pt_a11y_obj *obj, uint32_t words[2]) {
    words[0] = 0;
    words[1] = 0;
    if (!obj) {
        return;
    }
#define a11y_set_state(bit) ((words)[(bit) / 32u] |= 1u << ((bit) % 32u))
    if (obj->type == pt_a11y_obj_app) {
        a11y_set_state(atspi_state_enabled);
        a11y_set_state(atspi_state_sensitive);
        a11y_set_state(atspi_state_showing);
        a11y_set_state(atspi_state_visible);
    } else if (obj->type == pt_a11y_obj_window) {
        bool shown = obj->window->shown && obj->window->open;
        a11y_set_state(atspi_state_enabled);
        a11y_set_state(atspi_state_sensitive);
        a11y_set_state(atspi_state_focusable);
        if (shown) {
            a11y_set_state(atspi_state_showing);
            a11y_set_state(atspi_state_visible);
        }
        if (obj->window->keyboard_focused) {
            a11y_set_state(atspi_state_active);
            a11y_set_state(atspi_state_focused);
        }
    } else if (obj->type == pt_a11y_obj_widget) {
        uint32_t state = pt_widget_get_a11y_state(obj->widget);
        pt_widget *widget = obj->widget;
        if (state & pt_a11y_state_visible) {
            a11y_set_state(atspi_state_visible);
            a11y_set_state(atspi_state_showing);
        }
        if (state & pt_a11y_state_enabled) {
            a11y_set_state(atspi_state_enabled);
            a11y_set_state(atspi_state_sensitive);
        }
        if (state & pt_a11y_state_focusable) {
            a11y_set_state(atspi_state_focusable);
        }
        if (state & pt_a11y_state_focused) {
            a11y_set_state(atspi_state_focused);
            a11y_set_state(atspi_state_active);
        }
        if (state & pt_a11y_state_checked) {
            a11y_set_state(atspi_state_checked);
        }
        if (state & pt_a11y_state_selected) {
            a11y_set_state(atspi_state_selected);
        }
        if (state & pt_a11y_state_selectable) {
            a11y_set_state(atspi_state_selectable);
        }
        if (state & pt_a11y_state_multi_selectable) {
            a11y_set_state(atspi_state_multiselectable);
        }
        if (state & pt_a11y_state_editable) {
            a11y_set_state(atspi_state_editable);
        }
        if (state & pt_a11y_state_expanded) {
            a11y_set_state(atspi_state_expanded);
        }
        if (state & pt_a11y_state_expandable) {
            a11y_set_state(atspi_state_expandable);
            /* AT-SPI spells the folded half of the pair: a branch that can open
             * reports COLLAPSED while it is shut, so a reader says "collapsed"
             * instead of staying silent. */
            if (!(state & pt_a11y_state_expanded)) {
                a11y_set_state(atspi_state_collapsed);
            }
        }
        if (state & pt_a11y_state_busy) {
            a11y_set_state(atspi_state_busy);
        }
        if (state & pt_a11y_state_modal) {
            a11y_set_state(atspi_state_modal);
        }
        if (widget->kind == pt_widget_toggle || widget->kind == pt_widget_checkbox ||
            widget->kind == pt_widget_radio) {
            a11y_set_state(atspi_state_checkable);
            a11y_set_state(atspi_state_selectable);
        }
        if (widget->kind == pt_widget_segmented) {
            a11y_set_state(atspi_state_selectable);
        }
        if (widget->kind == pt_widget_progress && widget->indeterminate) {
            a11y_set_state(atspi_state_indeterminate);
        }
        if (widget->tooltip && widget->tooltip[0] != '\0') {
            a11y_set_state(atspi_state_has_tooltip);
        }
        if (widget->pressed) {
            a11y_set_state(atspi_state_pressed);
        }
        if (pt_widget_kind_is_text_input(widget->kind)) {
            /* Both editable text kinds support selection. READ_ONLY belongs to
             * text that a reader can inspect but not change: labels and
             * buttons, never an entry or a text area. */
            a11y_set_state(atspi_state_selectable_text);
        } else if (a11y_obj_has_text(obj)) {
            a11y_set_state(atspi_state_read_only);
        }
    }
#undef a11y_set_state
}

static void a11y_obj_rect(const pt_a11y_obj *obj, int *x, int *y, int *width, int *height) {
    float fx = 0.0f;
    float fy = 0.0f;
    float fw = 0.0f;
    float fh = 0.0f;
    if (obj) {
        if (obj->type == pt_a11y_obj_window) {
            fw = (float)obj->window->width;
            fh = (float)obj->window->height;
        } else if (obj->type == pt_a11y_obj_widget) {
            pt_widget *widget = obj->widget;
            fx = widget->x;
            fy = widget->y;
            pt_widget *top = widget;
            while (top->parent) {
                top = top->parent;
            }
            for (pt_overlay *overlay = obj->window->overlays; overlay; overlay = overlay->next) {
                if (overlay->open && overlay->native && overlay->panel == top) {
                    fx += overlay->rect.x;
                    fy += overlay->rect.y;
                    break;
                }
            }
            fw = widget->width;
            fh = widget->height;
        }
    }
    if (x) {
        *x = (int)fx;
    }
    if (y) {
        *y = (int)fy;
    }
    if (width) {
        *width = (int)fw;
    }
    if (height) {
        *height = (int)fh;
    }
}

static void a11y_parent_origin(const pt_a11y_obj *obj, float *x, float *y) {
    *x = 0.0f;
    *y = 0.0f;
    if (!obj || obj->type != pt_a11y_obj_widget) {
        return;
    }
    pt_widget *widget = obj->widget;
    if (!widget->parent) {
        return;
    }
    pt_a11y_obj parent = {.type = pt_a11y_obj_widget, .window = obj->window,
                          .widget = widget->parent};
    int px = 0;
    int py = 0;
    a11y_obj_rect(&parent, &px, &py, NULL, NULL);
    *x = (float)px;
    *y = (float)py;
}

static float a11y_value_min(const pt_a11y_obj *obj) {
    (void)obj;
    return 0.0f;
}

static float a11y_value_max(const pt_a11y_obj *obj) {
    if (obj && obj->type == pt_a11y_obj_widget &&
        obj->widget->kind == pt_widget_segmented) {
        return obj->widget->segment_count > 1 ? (float)(obj->widget->segment_count - 1) : 1.0f;
    }
    return 1.0f;
}

static float a11y_value_increment(const pt_a11y_obj *obj) {
    if (obj && obj->type == pt_a11y_obj_widget) {
        switch (obj->widget->kind) {
        case pt_widget_toggle:
        case pt_widget_checkbox:
        case pt_widget_radio:
        case pt_widget_segmented:
            return 1.0f;
        default:
            break;
        }
    }
    return 0.01f;
}

static bool a11y_value_current(const pt_a11y_obj *obj, float *value) {
    if (!a11y_obj_has_value(obj)) {
        return false;
    }
    return pt_widget_get_a11y_value(obj->widget, value);
}

static bool a11y_value_apply(const pt_a11y_obj *obj, float value) {
    if (!a11y_obj_has_value(obj)) {
        return false;
    }
    pt_widget *widget = obj->widget;
    switch (widget->kind) {
    case pt_widget_slider:
        pt_slider_set_value(widget, value);
        return true;
    case pt_widget_progress:
        pt_progress_set_value(widget, value);
        return true;
    case pt_widget_toggle:
    case pt_widget_checkbox:
    case pt_widget_radio:
        pt_widget_set_checked(widget, value >= 0.5f);
        return true;
    case pt_widget_segmented:
        pt_segmented_set_selected(widget, (uint32_t)(value + 0.5f));
        return true;
    default:
        return false;
    }
}

/* ---------- UTF-8 helpers ---------- */

static size_t a11y_utf8_count(const char *text) {
    size_t count = 0;
    for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor;) {
        size_t step = 1;
        if ((*cursor & 0xE0u) == 0xC0u) {
            step = 2;
        } else if ((*cursor & 0xF0u) == 0xE0u) {
            step = 3;
        } else if ((*cursor & 0xF8u) == 0xF0u) {
            step = 4;
        }
        for (size_t i = 1; i < step; ++i) {
            if ((cursor[i] & 0xC0u) != 0x80u) {
                step = 1;
                break;
            }
        }
        cursor += step;
        ++count;
    }
    return count;
}

static size_t a11y_utf8_byte_offset(const char *text, size_t char_index) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    size_t index = 0;
    while (*cursor && index < char_index) {
        size_t step = 1;
        if ((*cursor & 0xE0u) == 0xC0u) {
            step = 2;
        } else if ((*cursor & 0xF0u) == 0xE0u) {
            step = 3;
        } else if ((*cursor & 0xF8u) == 0xF0u) {
            step = 4;
        }
        for (size_t i = 1; i < step; ++i) {
            if ((cursor[i] & 0xC0u) != 0x80u) {
                step = 1;
                break;
            }
        }
        cursor += step;
        ++index;
    }
    return (size_t)(cursor - (const unsigned char *)(text ? text : ""));
}

static size_t a11y_utf8_char_offset(const char *text, size_t byte_offset) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    const unsigned char *end = cursor + byte_offset;
    size_t index = 0;
    while (*cursor && cursor < end) {
        size_t step = 1;
        if ((*cursor & 0xE0u) == 0xC0u) {
            step = 2;
        } else if ((*cursor & 0xF0u) == 0xE0u) {
            step = 3;
        } else if ((*cursor & 0xF8u) == 0xF0u) {
            step = 4;
        }
        for (size_t i = 1; i < step; ++i) {
            if ((cursor[i] & 0xC0u) != 0x80u) {
                step = 1;
                break;
            }
        }
        cursor += step;
        ++index;
    }
    return index;
}

static uint32_t a11y_utf8_decode(const char *text, size_t byte_offset) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "") + byte_offset;
    unsigned char lead = cursor[0];
    if (lead < 0x80u) {
        return lead;
    }
    size_t length = 0;
    uint32_t value = 0;
    if ((lead & 0xE0u) == 0xC0u) {
        length = 2;
        value = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3;
        value = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4;
        value = lead & 0x07u;
    } else {
        return 0xFFFDu;
    }
    for (size_t i = 1; i < length; ++i) {
        unsigned char byte = cursor[i];
        if ((byte & 0xC0u) != 0x80u) {
            return 0xFFFDu;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }
    return value;
}

typedef struct pt_a11y_text_view {
    const char *text;
    size_t chars;
    size_t caret;
    size_t sel_start;
    size_t sel_end;
    bool password;
    bool editable;
} pt_a11y_text_view;

static bool a11y_text_view_for(const pt_a11y_obj *obj, pt_a11y_text_view *view) {
    if (!obj || obj->type != pt_a11y_obj_widget || !a11y_obj_has_text(obj)) {
        return false;
    }
    pt_widget *widget = obj->widget;
    const char *text = NULL;
    uint32_t caret = 0;
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    if (!pt_widget_get_a11y_text(widget, &text, &caret, &sel_start, &sel_end)) {
        if (!pt_widget_kind_is_text_input(widget->kind)) {
            return false;
        }
        text = widget->text ? widget->text : "";
        caret = (uint32_t)(widget->cursor <= strlen(text) ? widget->cursor : strlen(text));
        uint32_t start = 0;
        uint32_t end = 0;
        if (!pt_widget_text_selection_get(widget, &start, &end)) {
            start = caret;
            end = caret;
        }
        sel_start = start;
        sel_end = end;
    }
    view->text = text ? text : "";
    view->chars = a11y_utf8_count(view->text);
    view->caret = a11y_utf8_char_offset(view->text, caret);
    view->sel_start = a11y_utf8_char_offset(view->text, sel_start);
    view->sel_end = a11y_utf8_char_offset(view->text, sel_end);
    view->password = widget->kind == pt_widget_entry && widget->password;
    view->editable = pt_widget_kind_is_text_input(widget->kind) && widget->enabled;
    return true;
}

static void a11y_substring(const char *text, size_t start_char, size_t end_char,
                           char **out) {
    size_t start = a11y_utf8_byte_offset(text, start_char);
    size_t end = a11y_utf8_byte_offset(text, end_char);
    if (end < start) {
        end = start;
    }
    size_t length = end - start;
    char *copy = pt_alloc(length + 1);
    memcpy(copy, (text ? text : "") + start, length);
    copy[length] = '\0';
    *out = copy;
}

static void a11y_word_range(const char *text, size_t chars, size_t offset, size_t *start,
                            size_t *end) {
    *start = offset;
    *end = offset;
    if (!text || chars == 0) {
        return;
    }
    size_t cursor = offset <= chars ? offset : chars;
    while (cursor > 0) {
        size_t byte = a11y_utf8_byte_offset(text, cursor - 1);
        uint32_t code = a11y_utf8_decode(text, byte);
        if (isspace((int)code)) {
            break;
        }
        --cursor;
    }
    *start = cursor;
    cursor = offset <= chars ? offset : chars;
    while (cursor < chars) {
        size_t byte = a11y_utf8_byte_offset(text, cursor);
        uint32_t code = a11y_utf8_decode(text, byte);
        if (isspace((int)code)) {
            break;
        }
        ++cursor;
    }
    *end = cursor;
}

/* ---------- D-Bus append helpers ---------- */

static void a11y_append_string(DBusMessageIter *iter, const char *value) {
    const char *text = value ? value : "";
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &text);
}

static void a11y_append_path(DBusMessageIter *iter, const char *path) {
    const char *value = path && *path ? path : a11y_null_path;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &value);
}

static void a11y_append_uint32(DBusMessageIter *iter, uint32_t value) {
    dbus_uint32_t wire = (dbus_uint32_t)value;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &wire);
}

static void a11y_append_int32(DBusMessageIter *iter, int32_t value) {
    dbus_int32_t wire = (dbus_int32_t)value;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &wire);
}

static void a11y_append_int16(DBusMessageIter *iter, int16_t value) {
    dbus_int16_t wire = (dbus_int16_t)value;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_INT16, &wire);
}

static void a11y_append_bool(DBusMessageIter *iter, bool value) {
    dbus_bool_t wire = value ? TRUE : FALSE;
    dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &wire);
}

static void a11y_append_double(DBusMessageIter *iter, double value) {
    dbus_message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &value);
}

static void a11y_append_ref(DBusMessageIter *iter, const pt_a11y_bridge *bridge,
                            const char *path) {
    DBusMessageIter structure;
    dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &structure);
    a11y_append_string(&structure, bridge && bridge->bus_name ? bridge->bus_name : "");
    a11y_append_path(&structure, path);
    dbus_message_iter_close_container(iter, &structure);
}

static void a11y_append_null_ref(DBusMessageIter *iter) {
    DBusMessageIter structure;
    dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &structure);
    a11y_append_string(&structure, "");
    a11y_append_path(&structure, a11y_null_path);
    dbus_message_iter_close_container(iter, &structure);
}

static void a11y_append_empty_dict(DBusMessageIter *iter) {
    DBusMessageIter dict;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_close_container(iter, &dict);
}

static void a11y_append_empty_attribute_dict(DBusMessageIter *iter) {
    DBusMessageIter dict;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{ss}", &dict);
    dbus_message_iter_close_container(iter, &dict);
}

static void a11y_dict_open(DBusMessageIter *iter, DBusMessageIter *dict) {
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{ss}", dict);
}

static void a11y_dict_add_string(DBusMessageIter *dict, const char *key, const char *value) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    a11y_append_string(&entry, key);
    a11y_append_string(&entry, value);
    dbus_message_iter_close_container(dict, &entry);
}

static const char *a11y_sort_direction_text(pt_sort_direction direction) {
    switch (direction) {
    case pt_sort_ascending:
        return "ascending";
    case pt_sort_descending:
        return "descending";
    default:
        return "none";
    }
}

static void a11y_dict_add_row_span(DBusMessageIter *dict, const pt_widget *row) {
    if (!row || row->list_index == pt_list_index_none) {
        return;
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", row->list_index);
    a11y_dict_add_string(dict, "row-index", buffer);
    a11y_dict_add_string(dict, "selected", row->row_selected ? "true" : "false");
}

static void a11y_append_attributes(DBusMessageIter *iter, const pt_a11y_obj *obj) {
    DBusMessageIter dict;
    a11y_dict_open(iter, &dict);
    if (!obj || obj->type != pt_a11y_obj_widget) {
        dbus_message_iter_close_container(iter, &dict);
        return;
    }
    pt_widget *widget = obj->widget;
    char buffer[96];
    if (widget->a11y_role == pt_a11y_role_alert) {
        a11y_dict_add_string(&dict, "live", "assertive");
        a11y_dict_add_string(&dict, "atomic", "true");
    }
    switch (widget->kind) {
    case pt_widget_table: {
        const pt_table_state *state = widget->extra;
        if (state && state->sort_count > 0) {
            size_t used = 0;
            for (uint32_t i = 0; i < state->sort_count && used < sizeof(buffer); ++i) {
                int written = snprintf(buffer + used, sizeof(buffer) - used, "%s%u %s",
                                       i ? ", " : "", state->sort_columns[i],
                                       a11y_sort_direction_text(state->sort_directions[i]));
                if (written < 0) {
                    break;
                }
                used += (size_t)written;
            }
            a11y_dict_add_string(&dict, "sort-columns", buffer);
        }
        break;
    }
    case pt_widget_table_header_cell: {
        snprintf(buffer, sizeof(buffer), "%u", widget->column_index);
        a11y_dict_add_string(&dict, "column-index", buffer);
        pt_widget *table = widget->parent ? widget->parent->parent : NULL;
        const pt_table_state *state =
            table && table->kind == pt_widget_table ? table->extra : NULL;
        pt_sort_direction direction = pt_sort_none;
        uint32_t priority = 0;
        if (state) {
            for (uint32_t i = 0; i < state->sort_count; ++i) {
                if (state->sort_columns[i] == widget->column_index) {
                    direction = state->sort_directions[i];
                    priority = i + 1;
                    break;
                }
            }
        }
        a11y_dict_add_string(&dict, "sort", a11y_sort_direction_text(direction));
        if (priority > 0) {
            snprintf(buffer, sizeof(buffer), "%u", priority);
            a11y_dict_add_string(&dict, "sort-priority", buffer);
        }
        break;
    }
    case pt_widget_table_cell:
        if (widget->parent) {
            char row_buffer[32];
            if (widget->parent->list_index != pt_list_index_none) {
                snprintf(row_buffer, sizeof(row_buffer), "%u", widget->parent->list_index);
                a11y_dict_add_string(&dict, "row-index", row_buffer);
            }
        }
        snprintf(buffer, sizeof(buffer), "%u", widget->column_index);
        a11y_dict_add_string(&dict, "column-index", buffer);
        break;
    case pt_widget_table_row:
        a11y_dict_add_row_span(&dict, widget);
        break;
    case pt_widget_tree_row: {
        pt_tree_node *node = widget->tree_node;
        if (node) {
            snprintf(buffer, sizeof(buffer), "%u", pt_tree_node_get_depth(node));
            a11y_dict_add_string(&dict, "level", buffer);
            if (pt_tree_node_has_children(node)) {
                a11y_dict_add_string(&dict, "expanded",
                                     pt_tree_node_get_expanded(node) ? "true" : "false");
            }
            a11y_dict_add_string(&dict, "selected",
                                 widget->row_selected ? "true" : "false");
        }
        break;
    }
    default:
        break;
    }
    dbus_message_iter_close_container(iter, &dict);
}

static void a11y_append_string_variant(DBusMessageIter *iter, const char *value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant);
    a11y_append_string(&variant, value);
    dbus_message_iter_close_container(iter, &variant);
}

static void a11y_append_double_variant(DBusMessageIter *iter, double value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "d", &variant);
    a11y_append_double(&variant, value);
    dbus_message_iter_close_container(iter, &variant);
}

static void a11y_append_uint32_variant(DBusMessageIter *iter, uint32_t value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "u", &variant);
    a11y_append_uint32(&variant, value);
    dbus_message_iter_close_container(iter, &variant);
}

static void a11y_append_int32_variant(DBusMessageIter *iter, int32_t value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &variant);
    a11y_append_int32(&variant, value);
    dbus_message_iter_close_container(iter, &variant);
}

static void a11y_append_position_variant(DBusMessageIter *iter, int32_t row,
                                         int32_t column) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(ii)", &variant);
    DBusMessageIter structure;
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &structure);
    a11y_append_int32(&structure, row);
    a11y_append_int32(&structure, column);
    dbus_message_iter_close_container(&variant, &structure);
    dbus_message_iter_close_container(iter, &variant);
}

static void a11y_append_ref_variant(DBusMessageIter *iter, const pt_a11y_bridge *bridge,
                                    const pt_a11y_obj *target) {
    char path[256];
    bool valid = a11y_obj_path(target, path, sizeof(path));
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(so)", &variant);
    DBusMessageIter structure;
    dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &structure);
    a11y_append_string(&structure, valid && bridge->bus_name ? bridge->bus_name : "");
    a11y_append_path(&structure, valid ? path : a11y_null_path);
    dbus_message_iter_close_container(&variant, &structure);
    dbus_message_iter_close_container(iter, &variant);
}

/* ---------- replies ---------- */

static DBusHandlerResult a11y_send(DBusConnection *connection, DBusMessage *message,
                                   DBusMessage *reply) {
    if (reply) {
        if (!dbus_message_get_no_reply(message)) {
            dbus_connection_send(connection, reply, NULL);
            dbus_connection_flush(connection);
        }
        dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult a11y_error(DBusConnection *connection, DBusMessage *message,
                                    const char *name, const char *text) {
    DBusMessage *reply = dbus_message_new_error(message, name, text);
    return a11y_send(connection, message, reply);
}

static DBusMessage *a11y_reply(DBusMessage *message) {
    return dbus_message_new_method_return(message);
}

/* ---------- property values ---------- */

static bool a11y_obj_is_table_cell(const pt_a11y_obj *obj);

static bool a11y_property_value(DBusMessageIter *iter, const pt_a11y_bridge *bridge,
                                const pt_a11y_obj *obj, const char *iface,
                                const char *property) {
    if (strcmp(iface, "org.a11y.atspi.Accessible") == 0) {
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "Name") == 0) {
            char name[512];
            a11y_obj_name_buffer(bridge->app, obj, name, sizeof(name));
            a11y_append_string_variant(iter, name);
            return true;
        }
        if (strcmp(property, "Description") == 0) {
            a11y_append_string_variant(iter, a11y_obj_description(obj));
            return true;
        }
        if (strcmp(property, "Parent") == 0) {
            char bus[256];
            char path[256];
            a11y_parent_ref(bridge, obj, bus, sizeof(bus), path, sizeof(path));
            DBusMessageIter variant;
            dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(so)", &variant);
            DBusMessageIter structure;
            dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &structure);
            a11y_append_string(&structure, bus);
            a11y_append_path(&structure, path);
            dbus_message_iter_close_container(&variant, &structure);
            dbus_message_iter_close_container(iter, &variant);
            return true;
        }
        if (strcmp(property, "ChildCount") == 0) {
            int count = 0;
            if (obj && obj->type == pt_a11y_obj_app) {
                count = a11y_app_child_count(bridge->app);
            } else if (obj) {
                count = a11y_child_count(obj);
            }
            a11y_append_int32_variant(iter, count);
            return true;
        }
        if (strcmp(property, "Locale") == 0 || strcmp(property, "HelpText") == 0) {
            a11y_append_string_variant(iter, "");
            return true;
        }
        if (strcmp(property, "AccessibleId") == 0) {
            char id[32];
            a11y_obj_id_string(obj, id, sizeof(id));
            a11y_append_string_variant(iter, id);
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Application") == 0) {
        if (obj->type != pt_a11y_obj_app) {
            return false;
        }
        if (strcmp(property, "ToolkitName") == 0) {
            a11y_append_string_variant(iter, "Pt");
            return true;
        }
        if (strcmp(property, "Version") == 0 || strcmp(property, "ToolkitVersion") == 0) {
            a11y_append_string_variant(iter, pt_version_string);
            return true;
        }
        if (strcmp(property, "AtspiVersion") == 0) {
            a11y_append_string_variant(iter, "2.1");
            return true;
        }
        if (strcmp(property, "InterfaceVersion") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "Id") == 0) {
            a11y_append_int32_variant(iter, bridge->application_id);
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Action") == 0) {
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "NActions") == 0) {
            a11y_append_int32_variant(iter, a11y_action_count(obj));
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Component") == 0) {
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Text") == 0) {
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "CharacterCount") == 0) {
            pt_a11y_text_view view = {0};
            if (!a11y_text_view_for(obj, &view)) {
                return false;
            }
            a11y_append_int32_variant(iter, (int32_t)view.chars);
            return true;
        }
        if (strcmp(property, "CaretOffset") == 0) {
            pt_a11y_text_view view = {0};
            if (!a11y_text_view_for(obj, &view)) {
                return false;
            }
            a11y_append_int32_variant(iter, (int32_t)view.caret);
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Value") == 0) {
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        float value = 0.0f;
        if (!a11y_value_current(obj, &value)) {
            return false;
        }
        if (strcmp(property, "MinimumValue") == 0) {
            a11y_append_double_variant(iter, a11y_value_min(obj));
            return true;
        }
        if (strcmp(property, "MaximumValue") == 0) {
            a11y_append_double_variant(iter, a11y_value_max(obj));
            return true;
        }
        if (strcmp(property, "MinimumIncrement") == 0) {
            a11y_append_double_variant(iter, a11y_value_increment(obj));
            return true;
        }
        if (strcmp(property, "CurrentValue") == 0) {
            a11y_append_double_variant(iter, value);
            return true;
        }
        if (strcmp(property, "Text") == 0) {
            a11y_append_string_variant(iter, "");
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Table") == 0) {
        pt_widget *table = a11y_obj_table_widget(obj);
        pt_table_state *state = a11y_table_state(table);
        if (!table || !state) {
            return false;
        }
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "NRows") == 0) {
            a11y_append_int32_variant(iter, (int32_t)table->row_count);
            return true;
        }
        if (strcmp(property, "NColumns") == 0) {
            a11y_append_int32_variant(iter, (int32_t)state->column_count);
            return true;
        }
        if (strcmp(property, "NSelectedRows") == 0) {
            a11y_append_int32_variant(iter,
                                      (int32_t)pt_table_get_selection_count(table));
            return true;
        }
        if (strcmp(property, "NSelectedColumns") == 0) {
            a11y_append_int32_variant(iter, 0);
            return true;
        }
        if (strcmp(property, "Caption") == 0 || strcmp(property, "Summary") == 0) {
            a11y_append_ref_variant(iter, bridge, NULL);
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.TableCell") == 0) {
        if (!a11y_obj_is_table_cell(obj)) {
            return false;
        }
        pt_widget *cell = obj->widget;
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "RowSpan") == 0 || strcmp(property, "ColumnSpan") == 0) {
            a11y_append_int32_variant(iter, 1);
            return true;
        }
        if (strcmp(property, "Position") == 0) {
            a11y_append_position_variant(iter, a11y_table_cell_row(cell),
                                         a11y_table_cell_column(cell));
            return true;
        }
        if (strcmp(property, "Table") == 0) {
            pt_widget *table = a11y_table_of_cell(cell);
            pt_a11y_obj target = {0};
            if (table) {
                target.type = pt_a11y_obj_widget;
                target.window = obj->window;
                target.widget = table;
            }
            a11y_append_ref_variant(iter, bridge, table ? &target : NULL);
            return true;
        }
        return false;
    }
    if (strcmp(iface, "org.a11y.atspi.Selection") == 0) {
        if (!a11y_obj_is_selection(obj)) {
            return false;
        }
        if (strcmp(property, "version") == 0) {
            a11y_append_uint32_variant(iter, 1u);
            return true;
        }
        if (strcmp(property, "NSelectedChildren") == 0) {
            a11y_append_int32_variant(iter, a11y_selection_count(obj));
            return true;
        }
        return false;
    }
    return false;
}

static bool a11y_interface_has_properties(const pt_a11y_obj *obj, const char *iface) {
    if (strcmp(iface, "org.a11y.atspi.Accessible") == 0) {
        return true;
    }
    if (strcmp(iface, "org.a11y.atspi.Application") == 0) {
        return obj && obj->type == pt_a11y_obj_app;
    }
    if (strcmp(iface, "org.a11y.atspi.Action") == 0) {
        return a11y_action_count(obj) > 0;
    }
    if (strcmp(iface, "org.a11y.atspi.Component") == 0) {
        return obj && obj->type != pt_a11y_obj_app;
    }
    if (strcmp(iface, "org.a11y.atspi.Text") == 0) {
        return a11y_obj_has_text(obj);
    }
    if (strcmp(iface, "org.a11y.atspi.Value") == 0) {
        return a11y_obj_has_value(obj);
    }
    if (strcmp(iface, "org.a11y.atspi.Table") == 0) {
        return a11y_obj_table_widget(obj) != NULL;
    }
    if (strcmp(iface, "org.a11y.atspi.TableCell") == 0) {
        return a11y_obj_is_table_cell(obj);
    }
    if (strcmp(iface, "org.a11y.atspi.Selection") == 0) {
        return a11y_obj_is_selection(obj);
    }
    return false;
}

static const char *const a11y_accessible_props[] = {
    "version", "Name", "Description", "Parent", "ChildCount",
    "Locale", "AccessibleId", "HelpText",
};
static const char *const a11y_application_props[] = {
    "ToolkitName", "Version", "ToolkitVersion", "AtspiVersion", "InterfaceVersion", "Id",
};
static const char *const a11y_action_props[] = {"version", "NActions"};
static const char *const a11y_component_props[] = {"version"};
static const char *const a11y_text_props[] = {"version", "CharacterCount", "CaretOffset"};
static const char *const a11y_value_props[] = {
    "version", "MinimumValue", "MaximumValue", "MinimumIncrement", "CurrentValue", "Text",
};
static const char *const a11y_table_props[] = {
    "version", "NRows", "NColumns", "Caption", "Summary", "NSelectedRows",
    "NSelectedColumns",
};
static const char *const a11y_table_cell_props[] = {
    "version", "RowSpan", "ColumnSpan", "Position", "Table",
};
static const char *const a11y_selection_props[] = {"version", "NSelectedChildren"};

static void a11y_properties_get_all(DBusMessageIter *iter, const pt_a11y_bridge *bridge,
                                    const pt_a11y_obj *obj, const char *iface) {
    const char *const *properties = NULL;
    size_t count = 0;
    if (strcmp(iface, "org.a11y.atspi.Accessible") == 0) {
        properties = a11y_accessible_props;
        count = sizeof(a11y_accessible_props) / sizeof(a11y_accessible_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Application") == 0) {
        properties = a11y_application_props;
        count = sizeof(a11y_application_props) / sizeof(a11y_application_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Action") == 0) {
        properties = a11y_action_props;
        count = sizeof(a11y_action_props) / sizeof(a11y_action_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Component") == 0) {
        properties = a11y_component_props;
        count = sizeof(a11y_component_props) / sizeof(a11y_component_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Text") == 0) {
        properties = a11y_text_props;
        count = sizeof(a11y_text_props) / sizeof(a11y_text_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Value") == 0) {
        properties = a11y_value_props;
        count = sizeof(a11y_value_props) / sizeof(a11y_value_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Table") == 0) {
        properties = a11y_table_props;
        count = sizeof(a11y_table_props) / sizeof(a11y_table_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.TableCell") == 0) {
        properties = a11y_table_cell_props;
        count = sizeof(a11y_table_cell_props) / sizeof(a11y_table_cell_props[0]);
    } else if (strcmp(iface, "org.a11y.atspi.Selection") == 0) {
        properties = a11y_selection_props;
        count = sizeof(a11y_selection_props) / sizeof(a11y_selection_props[0]);
    }
    DBusMessageIter dict;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
    for (size_t i = 0; i < count; ++i) {
        DBusMessageIter entry;
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        a11y_append_string(&entry, properties[i]);
        if (!a11y_property_value(&entry, bridge, obj, iface, properties[i])) {
            a11y_append_string_variant(&entry, "");
        }
        dbus_message_iter_close_container(&dict, &entry);
    }
    dbus_message_iter_close_container(iter, &dict);
}

/* ---------- interface lists ---------- */

enum {
    a11y_iface_accessible = 0,
    a11y_iface_application,
    a11y_iface_component,
    a11y_iface_action,
    a11y_iface_text,
    a11y_iface_editable_text,
    a11y_iface_value,
    a11y_iface_table,
    a11y_iface_table_cell,
    a11y_iface_selection,
    a11y_iface_count
};

static const char *const a11y_interface_names[a11y_iface_count] = {
    "org.a11y.atspi.Accessible", "org.a11y.atspi.Application",
    "org.a11y.atspi.Component",  "org.a11y.atspi.Action",
    "org.a11y.atspi.Text",       "org.a11y.atspi.EditableText",
    "org.a11y.atspi.Value",      "org.a11y.atspi.Table",
    "org.a11y.atspi.TableCell",  "org.a11y.atspi.Selection",
};

static bool a11y_obj_is_table_cell(const pt_a11y_obj *obj) {
    return obj && obj->type == pt_a11y_obj_widget &&
           obj->widget->kind == pt_widget_table_cell;
}

static void a11y_interfaces(const pt_a11y_obj *obj, bool flags[a11y_iface_count]) {
    memset(flags, 0, sizeof(bool) * a11y_iface_count);
    if (!obj) {
        return;
    }
    flags[a11y_iface_accessible] = true;
    flags[a11y_iface_application] = obj->type == pt_a11y_obj_app;
    flags[a11y_iface_component] = obj->type != pt_a11y_obj_app;
    flags[a11y_iface_action] = a11y_action_count(obj) > 0;
    flags[a11y_iface_text] = a11y_obj_has_text(obj);
    flags[a11y_iface_editable_text] =
        obj->type == pt_a11y_obj_widget &&
        pt_widget_kind_is_text_input(obj->widget->kind);
    flags[a11y_iface_value] = a11y_obj_has_value(obj);
    flags[a11y_iface_table] = a11y_obj_table_widget(obj) != NULL;
    flags[a11y_iface_table_cell] = a11y_obj_is_table_cell(obj);
    flags[a11y_iface_selection] = a11y_obj_is_selection(obj);
}

static void a11y_append_interfaces(DBusMessageIter *iter, const pt_a11y_obj *obj) {
    bool flags[a11y_iface_count];
    a11y_interfaces(obj, flags);
    DBusMessageIter array;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &array);
    for (size_t i = 0; i < a11y_iface_count; ++i) {
        if (flags[i]) {
            a11y_append_string(&array, a11y_interface_names[i]);
        }
    }
    dbus_message_iter_close_container(iter, &array);
}

/* ---------- introspection ---------- */

typedef struct a11y_buffer {
    char data[4096];
    size_t length;
} a11y_buffer;

static void a11y_buffer_add(a11y_buffer *buffer, const char *text) {
    size_t length = strlen(text);
    if (buffer->length + length >= sizeof(buffer->data)) {
        return;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

static DBusHandlerResult a11y_introspect(DBusConnection *connection, DBusMessage *message,
                                         const pt_a11y_obj *obj) {
    static const char prefix[] =
        "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
        "<node>\n"
        "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
        "    <method name=\"Introspect\">\n"
        "      <arg direction=\"out\" type=\"s\"/>\n"
        "    </method>\n"
        "  </interface>\n"
        "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
        "    <method name=\"Get\">\n"
        "      <arg direction=\"in\" type=\"s\"/>\n"
        "      <arg direction=\"in\" type=\"s\"/>\n"
        "      <arg direction=\"out\" type=\"v\"/>\n"
        "    </method>\n"
        "    <method name=\"GetAll\">\n"
        "      <arg direction=\"in\" type=\"s\"/>\n"
        "      <arg direction=\"out\" type=\"a{sv}\"/>\n"
        "    </method>\n"
        "    <method name=\"Set\">\n"
        "      <arg direction=\"in\" type=\"s\"/>\n"
        "      <arg direction=\"in\" type=\"s\"/>\n"
        "      <arg direction=\"in\" type=\"v\"/>\n"
        "    </method>\n"
        "  </interface>\n";
    a11y_buffer buffer = {0};
    a11y_buffer_add(&buffer, prefix);
    bool flags[a11y_iface_count];
    a11y_interfaces(obj, flags);
    for (size_t i = 0; i < a11y_iface_count; ++i) {
        if (!flags[i]) {
            continue;
        }
        a11y_buffer_add(&buffer, "  <interface name=\"");
        a11y_buffer_add(&buffer, a11y_interface_names[i]);
        a11y_buffer_add(&buffer, "\"/>\n");
    }
    a11y_buffer_add(&buffer, "</node>\n");
    DBusMessage *reply = a11y_reply(message);
    if (!reply) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    a11y_append_string(&iter, buffer.data);
    return a11y_send(connection, message, reply);
}

/* ---------- properties methods ---------- */

static DBusHandlerResult a11y_properties(DBusConnection *connection, DBusMessage *message,
                                         const pt_a11y_bridge *bridge,
                                         const pt_a11y_obj *obj) {
    const char *member = dbus_message_get_member(message);
    if (strcmp(member, "Get") == 0) {
        const char *iface = NULL;
        const char *property = NULL;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &iface,
                                   DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "Get expects interface and property names");
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        if (!a11y_property_value(&iter, bridge, obj, iface, property)) {
            dbus_message_unref(reply);
            return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_PROPERTY,
                              "unknown accessibility property");
        }
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetAll") == 0) {
        const char *iface = NULL;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &iface,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetAll expects an interface name");
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        if (a11y_interface_has_properties(obj, iface)) {
            a11y_properties_get_all(&iter, bridge, obj, iface);
        } else {
            a11y_append_empty_dict(&iter);
        }
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "Set") == 0) {
        const char *iface = NULL;
        const char *property = NULL;
        DBusMessageIter iter;
        if (!dbus_message_iter_init(message, &iter)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "Set expects interface, property and value");
        }
        dbus_message_iter_get_basic(&iter, &iface);
        dbus_message_iter_next(&iter);
        dbus_message_iter_get_basic(&iter, &property);
        dbus_message_iter_next(&iter);
        int type = dbus_message_iter_get_arg_type(&iter);
        if (type != DBUS_TYPE_VARIANT) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "property value must be a variant");
        }
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        if (strcmp(iface, "org.a11y.atspi.Application") == 0 &&
            strcmp(property, "Id") == 0 && obj->type == pt_a11y_obj_app) {
            dbus_int32_t id = 0;
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_INT32) {
                return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                                  "Application.Id expects an integer");
            }
            dbus_message_iter_get_basic(&variant, &id);
            ((pt_a11y_bridge *)bridge)->application_id = (int32_t)id;
            return a11y_send(connection, message, a11y_reply(message));
        }
        if (strcmp(iface, "org.a11y.atspi.Value") == 0 &&
            strcmp(property, "CurrentValue") == 0) {
            double value = 0.0;
            if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_DOUBLE) {
                return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                                  "CurrentValue expects a double");
            }
            dbus_message_iter_get_basic(&variant, &value);
            if (!a11y_value_apply(obj, (float)value)) {
                return a11y_error(connection, message, DBUS_ERROR_FAILED,
                                  "value is not writable");
            }
            return a11y_send(connection, message, a11y_reply(message));
        }
        return a11y_error(connection, message, DBUS_ERROR_PROPERTY_READ_ONLY,
                          "property is read-only");
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- relations ---------- */

typedef struct a11y_relation_writer {
    DBusMessageIter *array;
    DBusMessageIter entry;
    DBusMessageIter refs;
    int count;
} a11y_relation_writer;

static void a11y_relation_begin(DBusMessageIter *array, uint32_t type,
                                a11y_relation_writer *writer) {
    writer->array = array;
    writer->count = 0;
    dbus_message_iter_open_container(array, DBUS_TYPE_STRUCT, NULL, &writer->entry);
    a11y_append_uint32(&writer->entry, type);
    dbus_message_iter_open_container(&writer->entry, DBUS_TYPE_ARRAY, "(so)",
                                     &writer->refs);
}

static void a11y_relation_add(a11y_relation_writer *writer,
                              const pt_a11y_bridge *bridge, const pt_a11y_obj *target) {
    char path[256];
    if (!target || !a11y_obj_path(target, path, sizeof(path))) {
        return;
    }
    a11y_append_ref(&writer->refs, bridge, path);
    ++writer->count;
}

static void a11y_relation_end(a11y_relation_writer *writer) {
    dbus_message_iter_close_container(&writer->entry, &writer->refs);
    dbus_message_iter_close_container(writer->array, &writer->entry);
}

static bool a11y_obj_is_real_target(const pt_a11y_obj *target) {
    char path[256];
    return target && a11y_obj_path(target, path, sizeof(path));
}

static void a11y_relation_single(DBusMessageIter *array, const pt_a11y_bridge *bridge,
                                 uint32_t type, const pt_a11y_obj *target) {
    if (!a11y_obj_is_real_target(target)) {
        return;
    }
    a11y_relation_writer writer;
    a11y_relation_begin(array, type, &writer);
    a11y_relation_add(&writer, bridge, target);
    a11y_relation_end(&writer);
}

static pt_a11y_obj a11y_widget_obj(const pt_a11y_obj *base, pt_widget *widget) {
    pt_a11y_obj obj = {0};
    if (widget) {
        obj.type = pt_a11y_obj_widget;
        obj.window = base ? base->window : NULL;
        obj.widget = widget;
    }
    return obj;
}

static void a11y_append_relations(DBusMessageIter *iter, const pt_a11y_bridge *bridge,
                                  const pt_a11y_obj *obj) {
    DBusMessageIter array;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(ua(so))", &array);
    if (obj && obj->type == pt_a11y_obj_widget) {
        pt_widget *widget = obj->widget;
        switch (widget->kind) {
        case pt_widget_table_cell: {
            pt_widget *table = a11y_table_of_cell(widget);
            pt_a11y_obj target = a11y_widget_obj(obj, table);
            a11y_relation_single(&array, bridge, atspi_relation_member_of, &target);
            pt_widget *header =
                a11y_table_header_cell_widget(table, a11y_table_cell_column(widget));
            pt_a11y_obj header_obj = a11y_widget_obj(obj, header);
            a11y_relation_single(&array, bridge, atspi_relation_labelled_by, &header_obj);
            break;
        }
        case pt_widget_table_header_cell: {
            pt_widget *table = a11y_table_of_cell(widget);
            pt_a11y_obj target = a11y_widget_obj(obj, table);
            a11y_relation_single(&array, bridge, atspi_relation_label_for, &target);
            break;
        }
        case pt_widget_table_row: {
            pt_widget *table =
                widget->parent && widget->parent->kind == pt_widget_table ? widget->parent
                                                                          : NULL;
            pt_a11y_obj target = a11y_widget_obj(obj, table);
            a11y_relation_single(&array, bridge, atspi_relation_member_of, &target);
            break;
        }
        case pt_widget_tree_row: {
            pt_widget *tree = widget->parent && widget->parent->kind == pt_widget_tree
                                  ? widget->parent
                                  : NULL;
            pt_a11y_obj tree_obj = a11y_widget_obj(obj, tree);
            a11y_relation_single(&array, bridge, atspi_relation_member_of, &tree_obj);
            pt_tree_node *node = widget->tree_node;
            if (node && node->parent) {
                pt_widget *parent_row = a11y_tree_row_for_node(tree, node->parent);
                pt_a11y_obj parent_obj = a11y_widget_obj(obj, parent_row);
                a11y_relation_single(&array, bridge, atspi_relation_node_child_of,
                                     &parent_obj);
            }
            if (node && node->first_child) {
                int visible_children = 0;
                for (pt_tree_node *child = node->first_child; child;
                     child = child->next_sibling) {
                    if (a11y_tree_row_for_node(tree, child)) {
                        ++visible_children;
                    }
                }
                if (visible_children > 0) {
                    a11y_relation_writer writer;
                    a11y_relation_begin(&array, atspi_relation_node_parent_of, &writer);
                    for (pt_tree_node *child = node->first_child; child;
                         child = child->next_sibling) {
                        pt_widget *child_row = a11y_tree_row_for_node(tree, child);
                        pt_a11y_obj child_obj = a11y_widget_obj(obj, child_row);
                        a11y_relation_add(&writer, bridge, &child_obj);
                    }
                    a11y_relation_end(&writer);
                }
            }
            break;
        }
        case pt_widget_list_row: {
            pt_widget *list = widget->parent && widget->parent->kind == pt_widget_list
                                  ? widget->parent
                                  : NULL;
            pt_a11y_obj target = a11y_widget_obj(obj, list);
            a11y_relation_single(&array, bridge, atspi_relation_member_of, &target);
            break;
        }
        case pt_widget_sidebar_item: {
            pt_widget *sidebar =
                widget->parent && widget->parent->kind == pt_widget_sidebar ? widget->parent
                                                                           : NULL;
            pt_a11y_obj target = a11y_widget_obj(obj, sidebar);
            a11y_relation_single(&array, bridge, atspi_relation_member_of, &target);
            break;
        }
        case pt_widget_tab_page: {
            pt_widget *tabs = widget->parent && widget->parent->kind == pt_widget_tabs
                                  ? widget->parent
                                  : NULL;
            pt_a11y_obj target = a11y_widget_obj(obj, tabs);
            a11y_relation_single(&array, bridge, atspi_relation_member_of, &target);
            break;
        }
        default:
            break;
        }
        for (pt_overlay *overlay = obj->window ? obj->window->overlays : NULL; overlay;
             overlay = overlay->next) {
            if (overlay->panel != widget) {
                continue;
            }
            pt_a11y_obj window_obj = {0};
            window_obj.type = pt_a11y_obj_window;
            window_obj.window = obj->window;
            a11y_relation_single(&array, bridge, atspi_relation_subwindow_of,
                                 &window_obj);
            if (overlay->kind == pt_overlay_kind_tooltip) {
                pt_tooltip *tooltip = (pt_tooltip *)overlay;
                pt_a11y_obj target = a11y_widget_obj(obj, tooltip->target);
                a11y_relation_single(&array, bridge, atspi_relation_popup_for, &target);
            }
            break;
        }
    }
    dbus_message_iter_close_container(iter, &array);
}

/* ---------- accessible methods ---------- */

static DBusHandlerResult a11y_accessible(DBusConnection *connection, DBusMessage *message,
                                         const pt_a11y_bridge *bridge,
                                         const pt_a11y_obj *obj) {
    const char *member = dbus_message_get_member(message);
    if (strcmp(member, "GetChildAtIndex") == 0) {
        dbus_int32_t index = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &index,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetChildAtIndex expects an index");
        }
        pt_a11y_obj child = a11y_child_at(bridge->app, obj, index);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        char path[256];
        if (a11y_obj_path(&child, path, sizeof(path))) {
            a11y_append_ref(&iter, bridge, path);
        } else {
            a11y_append_null_ref(&iter);
        }
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetChildren") == 0) {
        int count = obj->type == pt_a11y_obj_app ? a11y_app_child_count(bridge->app)
                                                 : a11y_child_count(obj);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        DBusMessageIter array;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(so)", &array);
        for (int i = 0; i < count; ++i) {
            pt_a11y_obj child = a11y_child_at(bridge->app, obj, i);
            char path[256];
            if (!a11y_obj_path(&child, path, sizeof(path))) {
                continue;
            }
            a11y_append_ref(&array, bridge, path);
        }
        dbus_message_iter_close_container(&iter, &array);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetIndexInParent") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, a11y_child_index(obj));
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRole") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_uint32(&iter, a11y_role_number(obj));
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRoleName") == 0 || strcmp(member, "GetLocalizedRoleName") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, a11y_role_name(a11y_role_number(obj)));
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetState") == 0) {
        uint32_t words[2];
        a11y_state_words(obj, words);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        DBusMessageIter array;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "u", &array);
        for (size_t i = 0; i < 2; ++i) {
            a11y_append_uint32(&array, words[i]);
        }
        dbus_message_iter_close_container(&iter, &array);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetAttributes") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_attributes(&iter, obj);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRelationSet") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_relations(&iter, bridge, obj);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetApplication") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_ref(&iter, bridge, a11y_root_path);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetInterfaces") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_interfaces(&iter, obj);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- component methods ---------- */

static void a11y_point_to_window(const pt_a11y_obj *obj, int x, int y, uint32_t coord_type,
                                 float *out_x, float *out_y) {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    if (coord_type == 2) {
        a11y_parent_origin(obj, &origin_x, &origin_y);
    }
    *out_x = (float)x + origin_x;
    *out_y = (float)y + origin_y;
}

static pt_widget *a11y_hit_widget_tree(pt_widget *widget, const pt_window *window, float x,
                                       float y) {
    if (!widget || !widget->visible) {
        return NULL;
    }
    for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        pt_widget *hit = a11y_hit_widget_tree(child, window, x, y);
        if (hit) {
            return hit;
        }
    }
    pt_a11y_obj obj = {.type = pt_a11y_obj_widget, .window = (pt_window *)window,
                       .widget = widget};
    int wx = 0;
    int wy = 0;
    int ww = 0;
    int wh = 0;
    a11y_obj_rect(&obj, &wx, &wy, &ww, &wh);
    if (x >= (float)wx && y >= (float)wy && x < (float)(wx + ww) &&
        y < (float)(wy + wh)) {
        return widget;
    }
    return NULL;
}

static DBusHandlerResult a11y_component(DBusConnection *connection, DBusMessage *message,
                                        const pt_a11y_bridge *bridge,
                                        const pt_a11y_obj *obj) {
    (void)bridge;
    const char *member = dbus_message_get_member(message);
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    a11y_obj_rect(obj, &x, &y, &width, &height);
    if (strcmp(member, "Contains") == 0) {
        dbus_int32_t px = 0;
        dbus_int32_t py = 0;
        dbus_uint32_t coord_type = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &px, DBUS_TYPE_INT32,
                                   &py, DBUS_TYPE_UINT32, &coord_type,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "Contains expects x, y and coordinate type");
        }
        float wx = 0.0f;
        float wy = 0.0f;
        a11y_point_to_window(obj, px, py, coord_type, &wx, &wy);
        bool contains = wx >= (float)x && wy >= (float)y &&
                        wx < (float)(x + width) && wy < (float)(y + height);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, contains);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetAccessibleAtPoint") == 0) {
        dbus_int32_t px = 0;
        dbus_int32_t py = 0;
        dbus_uint32_t coord_type = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &px, DBUS_TYPE_INT32,
                                   &py, DBUS_TYPE_UINT32, &coord_type,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetAccessibleAtPoint expects x, y and coordinate type");
        }
        float wx = 0.0f;
        float wy = 0.0f;
        a11y_point_to_window(obj, px, py, coord_type, &wx, &wy);
        pt_a11y_obj hit = {0};
        if (obj->type == pt_a11y_obj_window) {
            pt_widget *widget = pt_window_overlay_hit_test(obj->window, wx, wy);
            if (!widget) {
                widget = pt_widget_hit_test(obj->window->root, wx, wy);
            }
            if (widget) {
                hit.type = pt_a11y_obj_widget;
                hit.window = obj->window;
                hit.widget = widget;
            }
        } else if (obj->type == pt_a11y_obj_widget) {
            pt_widget *widget = a11y_hit_widget_tree(obj->widget, obj->window, wx, wy);
            if (widget) {
                hit.type = pt_a11y_obj_widget;
                hit.window = obj->window;
                hit.widget = widget;
            }
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        char path[256];
        if (a11y_obj_path(&hit, path, sizeof(path))) {
            a11y_append_ref(&iter, bridge, path);
        } else {
            a11y_append_null_ref(&iter);
        }
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetExtents") == 0) {
        dbus_uint32_t coord_type = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_UINT32, &coord_type,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetExtents expects a coordinate type");
        }
        if (coord_type == 2) {
            float origin_x = 0.0f;
            float origin_y = 0.0f;
            a11y_parent_origin(obj, &origin_x, &origin_y);
            x -= (int)origin_x;
            y -= (int)origin_y;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        DBusMessageIter structure;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL, &structure);
        a11y_append_int32(&structure, x);
        a11y_append_int32(&structure, y);
        a11y_append_int32(&structure, width);
        a11y_append_int32(&structure, height);
        dbus_message_iter_close_container(&iter, &structure);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetPosition") == 0) {
        dbus_uint32_t coord_type = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_UINT32, &coord_type,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetPosition expects a coordinate type");
        }
        if (coord_type == 2) {
            float origin_x = 0.0f;
            float origin_y = 0.0f;
            a11y_parent_origin(obj, &origin_x, &origin_y);
            x -= (int)origin_x;
            y -= (int)origin_y;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, x);
        a11y_append_int32(&iter, y);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetSize") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, width);
        a11y_append_int32(&iter, height);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetLayer") == 0) {
        uint32_t layer = 3;
        if (obj->type == pt_a11y_obj_window) {
            layer = 8;
        } else if (obj->type == pt_a11y_obj_widget) {
            pt_widget *widget = obj->widget;
            for (pt_overlay *overlay = obj->window->overlays; overlay;
                 overlay = overlay->next) {
                if (overlay->open && overlay->panel == widget) {
                    layer = overlay->kind == pt_overlay_kind_dialog ? 6 : 5;
                    break;
                }
            }
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_uint32(&iter, layer);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetMDIZOrder") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int16(&iter, -1);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetAlpha") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_double(&iter, 1.0);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GrabFocus") == 0) {
        bool focused = false;
        if (obj->type == pt_a11y_obj_widget) {
            focused = pt_widget_focus(obj->widget);
        } else if (obj->type == pt_a11y_obj_window) {
            pt_widget_focus_first(obj->window);
            focused = obj->window->focused != NULL;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, focused);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "ScrollTo") == 0) {
        dbus_uint32_t scroll_type = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_UINT32, &scroll_type,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "ScrollTo expects a scroll type");
        }
        bool handled = false;
        if (obj->type == pt_a11y_obj_widget && obj->widget->kind == pt_widget_scroll) {
            float max_offset = pt_scroll_get_max_offset(obj->widget);
            switch (scroll_type) {
            case 0:
            case 2:
                pt_scroll_set_offset(obj->widget, 0.0f);
                handled = true;
                break;
            case 1:
            case 3:
                pt_scroll_set_offset(obj->widget, max_offset);
                handled = true;
                break;
            default:
                break;
            }
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, handled);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "SetExtents") == 0 || strcmp(member, "SetPosition") == 0 ||
        strcmp(member, "SetSize") == 0 || strcmp(member, "ScrollToPoint") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, false);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- action methods ---------- */

static DBusHandlerResult a11y_action(DBusConnection *connection, DBusMessage *message,
                                     const pt_a11y_bridge *bridge, const pt_a11y_obj *obj) {
    (void)bridge;
    const char *member = dbus_message_get_member(message);
    if (strcmp(member, "GetActions") == 0) {
        int count = a11y_action_count(obj);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        DBusMessageIter array;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(sss)", &array);
        for (int i = 0; i < count; ++i) {
            DBusMessageIter structure;
            dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, NULL, &structure);
            a11y_append_string(&structure, a11y_action_name(obj, i));
            a11y_append_string(&structure, i == 0 ? "Activates the control" : "");
            a11y_append_string(&structure, "");
            dbus_message_iter_close_container(&array, &structure);
        }
        dbus_message_iter_close_container(&iter, &array);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "DoAction") == 0) {
        dbus_int32_t index = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &index,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "DoAction expects an index");
        }
        bool done = false;
        if (index == 0) {
            if (a11y_obj_actionable(obj)) {
                done = pt_widget_activate(obj->widget);
            } else if (a11y_obj_focusable(obj)) {
                done = pt_widget_focus(obj->widget);
            }
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetDescription") == 0 || strcmp(member, "GetName") == 0 ||
        strcmp(member, "GetLocalizedName") == 0 || strcmp(member, "GetKeyBinding") == 0) {
        dbus_int32_t index = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &index,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "action query expects an index");
        }
        const char *text = "";
        if (index >= 0 && index < a11y_action_count(obj)) {
            if (strcmp(member, "GetName") == 0 || strcmp(member, "GetLocalizedName") == 0) {
                text = a11y_action_name(obj, index);
            } else if (strcmp(member, "GetDescription") == 0 && index == 0) {
                text = "Activates the control";
            }
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, text);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- text methods ---------- */

static DBusHandlerResult a11y_text(DBusConnection *connection, DBusMessage *message,
                                   const pt_a11y_obj *obj) {
    const char *member = dbus_message_get_member(message);
    pt_a11y_text_view view = {0};
    if (!a11y_text_view_for(obj, &view)) {
        if (strcmp(member, "GetTextBeforeOffset") == 0 ||
            strcmp(member, "GetTextAtOffset") == 0 ||
            strcmp(member, "GetTextAfterOffset") == 0) {
            return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_METHOD,
                              "object does not implement Text");
        }
        return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_METHOD,
                          "object has no text");
    }
    const char *visible_text = view.password ? "" : view.text;
    size_t visible_chars = view.password ? 0 : view.chars;
    if (strcmp(member, "GetText") == 0) {
        dbus_int32_t start = 0;
        dbus_int32_t end = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &start,
                                   DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetText expects start and end offsets");
        }
        if (start < 0) {
            start = 0;
        }
        if (end < 0) {
            end = (dbus_int32_t)visible_chars;
        } else if (end < start) {
            end = start;
        }
        if ((size_t)start > visible_chars) {
            start = (dbus_int32_t)visible_chars;
        }
        if ((size_t)end > visible_chars) {
            end = (dbus_int32_t)visible_chars;
        }
        char *text = NULL;
        a11y_substring(visible_text, (size_t)start, (size_t)end, &text);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            free(text);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, text);
        free(text);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "SetCaretOffset") == 0) {
        dbus_int32_t offset = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &offset,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "SetCaretOffset expects an offset");
        }
        if (offset < 0) {
            offset = 0;
        }
        if ((size_t)offset > view.chars) {
            offset = (dbus_int32_t)view.chars;
        }
        size_t byte = a11y_utf8_byte_offset(view.text, (size_t)offset);
        bool done = false;
        if (view.editable) {
            pt_widget_text_selection_set(obj->widget, (uint32_t)byte, (uint32_t)byte);
            done = true;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetCharacterCount") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, (int32_t)view.chars);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetStringAtOffset") == 0) {
        dbus_int32_t offset = 0;
        dbus_uint32_t granularity = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &offset,
                                   DBUS_TYPE_UINT32, &granularity,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetStringAtOffset expects offset and granularity");
        }
        size_t start_char = 0;
        size_t end_char = 0;
        if (granularity == 1) {
            a11y_word_range(view.text, view.chars, (size_t)offset, &start_char, &end_char);
        } else if (granularity == 0) {
            start_char = offset > 0 ? (size_t)offset : 0;
            end_char = start_char + 1;
            if (start_char > view.chars) {
                start_char = view.chars;
            }
            if (end_char > view.chars) {
                end_char = view.chars;
            }
        } else {
            start_char = 0;
            end_char = view.chars;
        }
        char *text = NULL;
        a11y_substring(visible_text, start_char, end_char, &text);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            free(text);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, text);
        a11y_append_int32(&iter, (int32_t)start_char);
        a11y_append_int32(&iter, (int32_t)end_char);
        free(text);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetTextBeforeOffset") == 0 ||
        strcmp(member, "GetTextAtOffset") == 0 ||
        strcmp(member, "GetTextAfterOffset") == 0) {
        dbus_int32_t offset = 0;
        dbus_uint32_t granularity = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &offset,
                                   DBUS_TYPE_UINT32, &granularity,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "text range query expects offset and granularity");
        }
        size_t start_char = 0;
        size_t end_char = 0;
        if (strcmp(member, "GetTextBeforeOffset") == 0) {
            start_char = 0;
            end_char = offset > 0 ? (size_t)offset : 0;
        } else if (strcmp(member, "GetTextAfterOffset") == 0) {
            start_char = offset > 0 ? (size_t)offset : 0;
            end_char = view.chars;
        } else if (granularity == 1) {
            a11y_word_range(view.text, view.chars, (size_t)offset, &start_char, &end_char);
        } else if (granularity == 0) {
            start_char = offset > 0 ? (size_t)offset : 0;
            end_char = start_char + 1;
            if (start_char > view.chars) {
                start_char = view.chars;
            }
            if (end_char > view.chars) {
                end_char = view.chars;
            }
        } else {
            start_char = 0;
            end_char = view.chars;
        }
        if (start_char > view.chars) {
            start_char = view.chars;
        }
        if (end_char > view.chars) {
            end_char = view.chars;
        }
        char *text = NULL;
        a11y_substring(visible_text, start_char, end_char, &text);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            free(text);
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, text);
        a11y_append_int32(&iter, (int32_t)start_char);
        a11y_append_int32(&iter, (int32_t)end_char);
        free(text);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetCharacterAtOffset") == 0) {
        dbus_int32_t offset = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &offset,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetCharacterAtOffset expects an offset");
        }
        uint32_t code = 0;
        if (offset >= 0 && (size_t)offset < visible_chars) {
            code = a11y_utf8_decode(visible_text,
                                    a11y_utf8_byte_offset(visible_text, (size_t)offset));
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, (int32_t)code);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetAttributeValue") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, "");
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetAttributes") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_empty_attribute_dict(&iter);
        a11y_append_int32(&iter, 0);
        a11y_append_int32(&iter, 0);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetDefaultAttributes") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_empty_attribute_dict(&iter);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetCharacterExtents") == 0 ||
        strcmp(member, "GetRangeExtents") == 0) {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        a11y_obj_rect(obj, &x, &y, &width, &height);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, x);
        a11y_append_int32(&iter, y);
        a11y_append_int32(&iter, width);
        a11y_append_int32(&iter, height);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetOffsetAtPoint") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, (int32_t)view.caret);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetNSelections") == 0) {
        int count = view.sel_end > view.sel_start ? 1 : 0;
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, count);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetSelection") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, (int32_t)view.sel_start);
        a11y_append_int32(&iter, (int32_t)view.sel_end);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "AddSelection") == 0 || strcmp(member, "SetSelection") == 0) {
        dbus_int32_t start = 0;
        dbus_int32_t end = 0;
        dbus_int32_t selection = 0;
        bool ok = false;
        if (strcmp(member, "SetSelection") == 0) {
            ok = dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &selection,
                                       DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end,
                                       DBUS_TYPE_INVALID) &&
                 selection == 0;
        } else {
            ok = dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &start,
                                       DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID);
        }
        bool done = false;
        if (ok && view.editable) {
            if (start < 0) {
                start = 0;
            }
            if (end < start) {
                end = start;
            }
            if ((size_t)start > view.chars) {
                start = (int32_t)view.chars;
            }
            if ((size_t)end > view.chars) {
                end = (int32_t)view.chars;
            }
            size_t byte_start = a11y_utf8_byte_offset(view.text, (size_t)start);
            size_t byte_end = a11y_utf8_byte_offset(view.text, (size_t)end);
            pt_widget_text_selection_set(obj->widget, (uint32_t)byte_start,
                                   (uint32_t)byte_end);
            done = true;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "RemoveSelection") == 0) {
        dbus_int32_t selection = 0;
        bool done = false;
        if (dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &selection,
                                  DBUS_TYPE_INVALID) &&
            selection == 0 && view.editable) {
            size_t byte = a11y_utf8_byte_offset(view.text, view.caret);
            pt_widget_text_selection_set(obj->widget, (uint32_t)byte, (uint32_t)byte);
            done = true;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "ScrollSubstringTo") == 0 ||
        strcmp(member, "ScrollSubstringToPoint") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, false);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- editable text methods ---------- */

static DBusHandlerResult a11y_editable_text(DBusConnection *connection,
                                            DBusMessage *message,
                                            const pt_a11y_obj *obj) {
    const char *member = dbus_message_get_member(message);
    pt_a11y_text_view view = {0};
    if (!a11y_text_view_for(obj, &view)) {
        return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_METHOD,
                          "object has no editable text");
    }
    pt_widget *entry = obj->widget;
    if (strcmp(member, "SetTextContents") == 0) {
        const char *text = NULL;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &text,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "SetTextContents expects a string");
        }
        bool done = false;
        if (view.editable) {
            pt_widget_set_text(entry, text);
            done = true;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "InsertText") == 0) {
        dbus_int32_t offset = 0;
        const char *text = NULL;
        dbus_int32_t length = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &offset,
                                   DBUS_TYPE_STRING, &text, DBUS_TYPE_INT32, &length,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "InsertText expects offset, text and length");
        }
        (void)length;
        bool done = false;
        if (view.editable) {
            if (offset < 0) {
                offset = 0;
            }
            if ((size_t)offset > view.chars) {
                offset = (int32_t)view.chars;
            }
            size_t byte = a11y_utf8_byte_offset(view.text, (size_t)offset);
            pt_widget_text_selection_set(entry, (uint32_t)byte, (uint32_t)byte);
            pt_widget_text_input(entry, text);
            done = true;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "DeleteText") == 0 || strcmp(member, "CopyText") == 0 ||
        strcmp(member, "CutText") == 0) {
        dbus_int32_t start = 0;
        dbus_int32_t end = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &start,
                                   DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "text range expects start and end offsets");
        }
        if (start < 0) {
            start = 0;
        }
        if (end < start) {
            end = start;
        }
        if ((size_t)start > view.chars) {
            start = (int32_t)view.chars;
        }
        if ((size_t)end > view.chars) {
            end = (int32_t)view.chars;
        }
        bool copy = strcmp(member, "CopyText") == 0 || strcmp(member, "CutText") == 0;
        bool remove = strcmp(member, "DeleteText") == 0 || strcmp(member, "CutText") == 0;
        bool done = false;
        const char *text = view.password ? "" : view.text;
        if (view.editable) {
            if (copy) {
                char *selected = NULL;
                a11y_substring(text, (size_t)start, (size_t)end, &selected);
                pt_platform_clipboard_set(obj->window, selected);
                free(selected);
            }
            if (remove) {
                char *before = NULL;
                char *after = NULL;
                a11y_substring(text, 0, (size_t)start, &before);
                a11y_substring(text, (size_t)end, view.chars, &after);
                size_t length = strlen(before) + strlen(after) + 1;
                char *next = pt_alloc(length);
                snprintf(next, length, "%s%s", before, after);
                free(before);
                free(after);
                pt_widget_set_text(entry, next);
                free(next);
                size_t byte = a11y_utf8_byte_offset(pt_widget_get_text(entry),
                                                    (size_t)start);
                pt_widget_text_selection_set(entry, (uint32_t)byte, (uint32_t)byte);
            }
            done = true;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "PasteText") == 0) {
        dbus_int32_t offset = 0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &offset,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "PasteText expects an offset");
        }
        bool done = false;
        char *clipboard = pt_platform_clipboard_get(obj->window);
        if (view.editable && clipboard) {
            if (offset < 0) {
                offset = 0;
            }
            if ((size_t)offset > view.chars) {
                offset = (int32_t)view.chars;
            }
            size_t byte = a11y_utf8_byte_offset(view.text, (size_t)offset);
            pt_widget_text_selection_set(entry, (uint32_t)byte, (uint32_t)byte);
            pt_widget_text_input(entry, clipboard);
            done = true;
        }
        free(clipboard);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- table methods ---------- */

static bool a11y_arg_int32(DBusMessage *message, int32_t *value) {
    dbus_int32_t wire = 0;
    if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &wire, DBUS_TYPE_INVALID)) {
        return false;
    }
    *value = (int32_t)wire;
    return true;
}

static bool a11y_args_row_column(DBusMessage *message, int32_t *row, int32_t *column) {
    dbus_int32_t first = 0;
    dbus_int32_t second = 0;
    if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &first, DBUS_TYPE_INT32,
                               &second, DBUS_TYPE_INVALID)) {
        return false;
    }
    *row = (int32_t)first;
    *column = (int32_t)second;
    return true;
}

static bool a11y_table_cell_valid(const pt_widget *table, const pt_table_state *state,
                                  int32_t row, int32_t column) {
    return state && table && row >= 0 && (uint64_t)row < table->row_count &&
           column >= 0 && (uint32_t)column < state->column_count;
}

static int32_t a11y_table_index(const pt_widget *table, const pt_table_state *state,
                                int32_t row, int32_t column) {
    if (!a11y_table_cell_valid(table, state, row, column)) {
        return -1;
    }
    return row * (int32_t)state->column_count + column;
}

static void a11y_append_optional_ref(DBusMessageIter *iter, const pt_a11y_bridge *bridge,
                                     pt_widget *widget) {
    if (!widget) {
        a11y_append_null_ref(iter);
        return;
    }
    char path[256];
    a11y_widget_path(widget, path, sizeof(path));
    a11y_append_ref(iter, bridge, path);
}

static DBusHandlerResult a11y_table(DBusConnection *connection, DBusMessage *message,
                                    const pt_a11y_bridge *bridge,
                                    const pt_a11y_obj *obj) {
    pt_widget *table = a11y_obj_table_widget(obj);
    pt_table_state *state = a11y_table_state(table);
    if (!table || !state) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char *member = dbus_message_get_member(message);
    if (strcmp(member, "GetAccessibleAt") == 0) {
        int32_t row = 0;
        int32_t column = 0;
        if (!a11y_args_row_column(message, &row, &column)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetAccessibleAt expects row and column");
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_optional_ref(&iter, bridge, a11y_table_cell_widget(table, row, column));
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetIndexAt") == 0) {
        int32_t row = 0;
        int32_t column = 0;
        if (!a11y_args_row_column(message, &row, &column)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetIndexAt expects row and column");
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, a11y_table_index(table, state, row, column));
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRowAtIndex") == 0 ||
        strcmp(member, "GetColumnAtIndex") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "table index expects an int32");
        }
        int64_t cells = (int64_t)table->row_count * (int64_t)state->column_count;
        int32_t result = -1;
        if (index >= 0 && (int64_t)index < cells && state->column_count > 0) {
            result = strcmp(member, "GetRowAtIndex") == 0
                         ? index / (int32_t)state->column_count
                         : index % (int32_t)state->column_count;
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, result);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRowDescription") == 0 ||
        strcmp(member, "GetColumnDescription") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "description expects an int32");
        }
        const char *description = "";
        if (strcmp(member, "GetColumnDescription") == 0 && index >= 0 &&
            (uint32_t)index < state->column_count && state->column_titles &&
            state->column_titles[index]) {
            description = state->column_titles[index];
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, description);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRowExtentAt") == 0 ||
        strcmp(member, "GetColumnExtentAt") == 0) {
        int32_t row = 0;
        int32_t column = 0;
        if (!a11y_args_row_column(message, &row, &column)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "extent expects row and column");
        }
        bool valid = a11y_table_cell_valid(table, state, row, column);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_int32(&iter, valid ? 1 : 0);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRowHeader") == 0) {
        int32_t row = 0;
        if (!a11y_arg_int32(message, &row)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetRowHeader expects a row");
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_null_ref(&iter);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetColumnHeader") == 0) {
        int32_t column = 0;
        if (!a11y_arg_int32(message, &column)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetColumnHeader expects a column");
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_optional_ref(&iter, bridge,
                                 a11y_table_header_cell_widget(table, column));
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetSelectedRows") == 0 ||
        strcmp(member, "GetSelectedColumns") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        DBusMessageIter array;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "i", &array);
        if (strcmp(member, "GetSelectedRows") == 0) {
            for (uint32_t row = 0; row < table->row_count; ++row) {
                if (pt_table_is_row_selected(table, row)) {
                    a11y_append_int32(&array, (int32_t)row);
                }
            }
        }
        dbus_message_iter_close_container(&iter, &array);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "IsRowSelected") == 0 || strcmp(member, "IsColumnSelected") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "selection query expects an int32");
        }
        bool selected = strcmp(member, "IsRowSelected") == 0 && index >= 0 &&
                        (uint64_t)index < table->row_count &&
                        pt_table_is_row_selected(table, (uint32_t)index);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, selected);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "IsSelected") == 0) {
        int32_t row = 0;
        int32_t column = 0;
        if (!a11y_args_row_column(message, &row, &column)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "IsSelected expects row and column");
        }
        bool selected = a11y_table_cell_valid(table, state, row, column) &&
                        a11y_table_cell_widget(table, row, column) &&
                        row >= 0 &&
                        pt_table_is_row_selected(table, (uint32_t)row);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, selected);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "AddRowSelection") == 0 ||
        strcmp(member, "RemoveRowSelection") == 0 ||
        strcmp(member, "AddColumnSelection") == 0 ||
        strcmp(member, "RemoveColumnSelection") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "selection change expects an int32");
        }
        bool done = false;
        if (strcmp(member, "AddRowSelection") == 0) {
            done = a11y_table_cell_valid(table, state, index, 0);
            if (done && !pt_table_is_row_selected(table, (uint32_t)index)) {
                /* A toggle adds the row and keeps the rest of the set, which is
                 * what AtkTable's add-row-selection asks for. */
                pt_table_select_toggle(table, (uint32_t)index);
            }
        } else if (strcmp(member, "RemoveRowSelection") == 0) {
            done = index >= 0 && (uint64_t)index < table->row_count &&
                   pt_table_is_row_selected(table, (uint32_t)index);
            if (done) {
                pt_table_select_toggle(table, (uint32_t)index);
            }
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetRowColumnExtentsAtIndex") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetRowColumnExtentsAtIndex expects an index");
        }
        int64_t cells = (int64_t)table->row_count * (int64_t)state->column_count;
        bool valid = index >= 0 && (int64_t)index < cells && state->column_count > 0;
        int32_t row = valid ? index / (int32_t)state->column_count : 0;
        int32_t column = valid ? index % (int32_t)state->column_count : 0;
        bool bound = valid && a11y_table_cell_widget(table, row, column) != NULL;
        bool selected = bound && table->list_selected == (uint32_t)row;
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, valid);
        a11y_append_int32(&iter, row);
        a11y_append_int32(&iter, column);
        a11y_append_int32(&iter, valid ? 1 : 0);
        a11y_append_int32(&iter, valid ? 1 : 0);
        a11y_append_bool(&iter, selected);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- table cell methods ---------- */

static DBusHandlerResult a11y_table_cell(DBusConnection *connection, DBusMessage *message,
                                         const pt_a11y_bridge *bridge,
                                         const pt_a11y_obj *obj) {
    if (!a11y_obj_is_table_cell(obj)) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    pt_widget *cell = obj->widget;
    const char *member = dbus_message_get_member(message);
    if (strcmp(member, "GetRowColumnSpan") == 0) {
        int32_t row = a11y_table_cell_row(cell);
        int32_t column = a11y_table_cell_column(cell);
        bool valid = row >= 0 && column >= 0;
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, valid);
        a11y_append_int32(&iter, valid ? row : 0);
        a11y_append_int32(&iter, valid ? column : 0);
        a11y_append_int32(&iter, valid ? 1 : 0);
        a11y_append_int32(&iter, valid ? 1 : 0);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetColumnHeaderCells") == 0 ||
        strcmp(member, "GetRowHeaderCells") == 0) {
        bool column_headers = strcmp(member, "GetColumnHeaderCells") == 0;
        pt_widget *table = a11y_table_of_cell(cell);
        pt_widget *header =
            column_headers ? a11y_table_header_cell_widget(table,
                                                           a11y_table_cell_column(cell))
                           : NULL;
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        DBusMessageIter array;
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(so)", &array);
        if (header) {
            char path[256];
            a11y_widget_path(header, path, sizeof(path));
            a11y_append_ref(&array, bridge, path);
        }
        dbus_message_iter_close_container(&iter, &array);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- selection methods ---------- */

static DBusHandlerResult a11y_selection(DBusConnection *connection, DBusMessage *message,
                                        const pt_a11y_bridge *bridge,
                                        const pt_a11y_obj *obj) {
    if (!a11y_obj_is_selection(obj)) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char *member = dbus_message_get_member(message);
    pt_widget *container = obj->widget;
    if (strcmp(member, "GetSelectedChild") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "GetSelectedChild expects an index");
        }
        pt_widget *selected = a11y_selection_nth(obj, index);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        if (selected) {
            char path[256];
            a11y_widget_path(selected, path, sizeof(path));
            a11y_append_ref(&iter, bridge, path);
        } else {
            a11y_append_null_ref(&iter);
        }
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "SelectChild") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "SelectChild expects an index");
        }
        bool done = a11y_selection_select(obj, index);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "DeselectSelectedChild") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "DeselectSelectedChild expects an index");
        }
        bool done = container->kind != pt_widget_tabs &&
                    a11y_selection_remove(obj, a11y_selection_nth(obj, index));
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "IsChildSelected") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "IsChildSelected expects an index");
        }
        pt_widget *child = a11y_widget_child_at(container, index);
        bool selected = child != NULL && a11y_child_selected(container, child);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, selected);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "SelectAll") == 0) {
        bool done = a11y_selection_select_all(obj);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "ClearSelection") == 0) {
        bool done = a11y_selection_clear(obj);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "DeselectChild") == 0) {
        int32_t index = 0;
        if (!a11y_arg_int32(message, &index)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "DeselectChild expects an index");
        }
        bool done = a11y_selection_deselect_child(obj, index);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- value methods ---------- */

static DBusHandlerResult a11y_value(DBusConnection *connection, DBusMessage *message,
                                    const pt_a11y_obj *obj) {
    const char *member = dbus_message_get_member(message);
    float value = 0.0f;
    if (!a11y_value_current(obj, &value)) {
        return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_METHOD,
                          "object has no value");
    }
    if (strcmp(member, "GetMinimumValue") == 0 ||
        strcmp(member, "GetMaximumValue") == 0 ||
        strcmp(member, "GetMinimumIncrement") == 0 ||
        strcmp(member, "GetCurrentValue") == 0) {
        double result = value;
        if (strcmp(member, "GetMinimumValue") == 0) {
            result = a11y_value_min(obj);
        } else if (strcmp(member, "GetMaximumValue") == 0) {
            result = a11y_value_max(obj);
        } else if (strcmp(member, "GetMinimumIncrement") == 0) {
            result = a11y_value_increment(obj);
        }
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_double(&iter, result);
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "SetCurrentValue") == 0) {
        double requested = 0.0;
        if (!dbus_message_get_args(message, NULL, DBUS_TYPE_DOUBLE, &requested,
                                   DBUS_TYPE_INVALID)) {
            return a11y_error(connection, message, DBUS_ERROR_INVALID_ARGS,
                              "SetCurrentValue expects a double");
        }
        bool done = a11y_value_apply(obj, (float)requested);
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_bool(&iter, done);
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- application and socket methods ---------- */

static DBusHandlerResult a11y_application(DBusConnection *connection, DBusMessage *message,
                                          const pt_a11y_bridge *bridge,
                                          const pt_a11y_obj *obj) {
    (void)bridge;
    const char *member = dbus_message_get_member(message);
    if (obj->type != pt_a11y_obj_app) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (strcmp(member, "GetLocale") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        a11y_append_string(&iter, "");
        return a11y_send(connection, message, reply);
    }
    if (strcmp(member, "GetApplicationBusAddress") == 0) {
        DBusMessage *reply = a11y_reply(message);
        if (!reply) {
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        const char *address = getenv("AT_SPI_BUS_ADDRESS");
        a11y_append_string(&iter, address ? address : "");
        return a11y_send(connection, message, reply);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult a11y_socket(DBusConnection *connection, DBusMessage *message) {
    const char *member = dbus_message_get_member(message);
    if (strcmp(member, "Embedded") == 0) {
        return a11y_send(connection, message, a11y_reply(message));
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------- message dispatch ---------- */

static DBusHandlerResult a11y_message(DBusConnection *connection, DBusMessage *message,
                                      void *data) {
    pt_a11y_bridge *bridge = data;
    if (!bridge || !bridge->app || !message) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char *path = dbus_message_get_path(message);
    const char *iface = dbus_message_get_interface(message);
    if (!path || !iface) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    pt_a11y_obj obj = a11y_resolve(bridge->app, path);
    if (strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0 &&
        strcmp(dbus_message_get_member(message), "Introspect") == 0) {
        return a11y_introspect(connection, message, &obj);
    }
    if (obj.type == pt_a11y_obj_none) {
        return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_OBJECT,
                          "unknown accessibility object");
    }
    if (strcmp(iface, "org.freedesktop.DBus.Properties") == 0) {
        DBusHandlerResult result = a11y_properties(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Accessible") == 0) {
        DBusHandlerResult result = a11y_accessible(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Component") == 0 &&
               obj.type != pt_a11y_obj_app) {
        DBusHandlerResult result = a11y_component(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Action") == 0) {
        DBusHandlerResult result = a11y_action(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Text") == 0) {
        DBusHandlerResult result = a11y_text(connection, message, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.EditableText") == 0) {
        DBusHandlerResult result = a11y_editable_text(connection, message, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Value") == 0) {
        DBusHandlerResult result = a11y_value(connection, message, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Table") == 0) {
        DBusHandlerResult result = a11y_table(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.TableCell") == 0) {
        DBusHandlerResult result = a11y_table_cell(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Selection") == 0) {
        DBusHandlerResult result = a11y_selection(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Application") == 0) {
        DBusHandlerResult result = a11y_application(connection, message, bridge, &obj);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    } else if (strcmp(iface, "org.a11y.atspi.Socket") == 0) {
        DBusHandlerResult result = a11y_socket(connection, message);
        if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED) {
            return result;
        }
    }
    return a11y_error(connection, message, DBUS_ERROR_UNKNOWN_METHOD,
                      "unknown accessibility method");
}

/* ---------- events ---------- */

static void a11y_emit_event(const pt_widget *widget, const char *iface, const char *signal,
                            const char *detail, int32_t first, int32_t second,
                            int variant_type, const char *text_value, double double_value) {
    pt_window *window = pt_widget_window(widget);
    if (!window || !window->app) {
        return;
    }
    pt_a11y_bridge *bridge = window->app->a11y_bridge;
    if (!bridge || !bridge->bus) {
        return;
    }
    char path[256];
    a11y_widget_path(widget, path, sizeof(path));
    DBusMessage *message = dbus_message_new_signal(path, iface, signal);
    if (!message) {
        return;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(message, &iter);
    a11y_append_string(&iter, detail ? detail : "");
    a11y_append_int32(&iter, first);
    a11y_append_int32(&iter, second);
    if (variant_type == 'd') {
        a11y_append_double_variant(&iter, double_value);
    } else {
        a11y_append_string_variant(&iter, text_value ? text_value : "");
    }
    a11y_append_empty_dict(&iter);
    dbus_connection_send(bridge->bus, message, NULL);
    dbus_connection_flush(bridge->bus);
    dbus_message_unref(message);
}

static void a11y_emit_state(pt_widget *widget, const char *state, bool enabled) {
    a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "StateChanged", state,
                    enabled ? 1 : 0, 0, 's', "", 0.0);
}

void pt_a11y_bridge_emit(pt_widget *widget, pt_a11y_event_type type) {
    if (!widget) {
        return;
    }
    pt_window *window = pt_widget_window(widget);
    if (!window || !window->app || !window->app->a11y_bridge) {
        return;
    }
    switch (type) {
    case pt_a11y_event_focus_changed:
        a11y_emit_state(widget, "focused", widget->focused);
        a11y_emit_event(widget, "org.a11y.atspi.Event.Focus", "Focus", "", 0, 0, 's', "",
                        0.0);
        break;
    case pt_a11y_event_state_changed: {
        const char *state = "enabled";
        bool value = widget->enabled;
        if (widget->kind == pt_widget_toggle || widget->kind == pt_widget_checkbox ||
            widget->kind == pt_widget_radio) {
            state = "checked";
            value = widget->checked;
        } else if (widget->kind == pt_widget_progress && widget->indeterminate) {
            state = "indeterminate";
            value = true;
        } else if (widget->kind == pt_widget_spinner) {
            state = "busy";
            value = true;
        }
        a11y_emit_state(widget, state, value);
        break;
    }
    case pt_a11y_event_text_changed: {
        const char *text = NULL;
        uint32_t caret = 0;
        uint32_t start = 0;
        uint32_t end = 0;
        if (pt_widget_get_a11y_text(widget, &text, &caret, &start, &end)) {
            a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "TextChanged", "", 0, 0,
                            's', text, 0.0);
        }
        break;
    }
    case pt_a11y_event_selection_changed: {
        a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "SelectionChanged", "", 0, 0,
                        's', "", 0.0);
        pt_a11y_obj obj = {0};
        obj.type = pt_a11y_obj_widget;
        obj.window = window;
        obj.widget = widget;
        pt_widget *selected = a11y_selection_primary(&obj);
        if (selected) {
            a11y_emit_state(selected, "selected", true);
        }
        break;
    }
    case pt_a11y_event_value_changed: {
        float value = 0.0f;
        if (pt_widget_get_a11y_value(widget, &value)) {
            a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "PropertyChange",
                            "accessible-value", 0, 0, 'd', NULL, (double)value);
        }
        break;
    }
    case pt_a11y_event_name_changed:
        a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "PropertyChange",
                        "accessible-name", 0, 0, 's', pt_widget_get_a11y_name(widget), 0.0);
        break;
    case pt_a11y_event_structure_changed:
        a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "ChildrenChanged", "reorder",
                        -1, 0, 's', "", 0.0);
        break;
    }
}

void pt_a11y_bridge_widget_state(pt_widget *widget, const char *state, bool value) {
    if (!widget || !state || !*state) {
        return;
    }
    a11y_emit_state(widget, state, value);
}

void pt_a11y_bridge_announce(pt_widget *widget, int politeness, const char *text) {
    if (!widget || !text || !*text) {
        return;
    }
    a11y_emit_event(widget, "org.a11y.atspi.Event.Object", "Announcement", "",
                    politeness, 0, 's', text, 0.0);
}

/* ---------- lifecycle ---------- */

static void a11y_bridge_free(pt_a11y_bridge *bridge) {
    if (!bridge) {
        return;
    }
    if (bridge->bus) {
        if (dbus_connection_get_is_connected(bridge->bus)) {
            dbus_connection_unregister_object_path(bridge->bus, a11y_root_path);
            dbus_connection_unregister_object_path(bridge->bus, a11y_fallback_path);
            dbus_connection_flush(bridge->bus);
        }
        dbus_connection_close(bridge->bus);
        dbus_connection_unref(bridge->bus);
    }
    free(bridge);
}

static DBusConnection *a11y_open_bus(DBusError *error) {
    const char *address = getenv("AT_SPI_BUS_ADDRESS");
    DBusConnection *bus = NULL;
    if (address && *address) {
        bus = dbus_connection_open_private(address, error);
        if (!bus) {
            return NULL;
        }
        if (!dbus_bus_register(bus, error)) {
            dbus_connection_close(bus);
            dbus_connection_unref(bus);
            return NULL;
        }
        return bus;
    }
    DBusConnection *session = dbus_bus_get_private(DBUS_BUS_SESSION, error);
    if (!session) {
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect(session, FALSE);
    DBusMessage *request = dbus_message_new_method_call(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    if (!request) {
        dbus_connection_close(session);
        dbus_connection_unref(session);
        return NULL;
    }
    DBusMessage *reply =
        dbus_connection_send_with_reply_and_block(session, request, 3000, error);
    dbus_message_unref(request);
    if (!reply) {
        dbus_connection_close(session);
        dbus_connection_unref(session);
        return NULL;
    }
    const char *bus_address = NULL;
    if (!dbus_message_get_args(reply, error, DBUS_TYPE_STRING, &bus_address,
                               DBUS_TYPE_INVALID)) {
        dbus_message_unref(reply);
        dbus_connection_close(session);
        dbus_connection_unref(session);
        return NULL;
    }
    char *copy = bus_address ? strdup(bus_address) : NULL;
    dbus_message_unref(reply);
    dbus_connection_close(session);
    dbus_connection_unref(session);
    if (!copy) {
        dbus_set_error(error, DBUS_ERROR_NO_MEMORY, "cannot copy accessibility bus address");
        return NULL;
    }
    bus = dbus_connection_open_private(copy, error);
    free(copy);
    if (!bus) {
        return NULL;
    }
    if (!dbus_bus_register(bus, error)) {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
        return NULL;
    }
    return bus;
}

static bool a11y_embed_root(pt_a11y_bridge *bridge) {
    DBusMessage *message = dbus_message_new_method_call(
        "org.a11y.atspi.Registry", a11y_root_path, "org.a11y.atspi.Socket", "Embed");
    if (!message) {
        return false;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(message, &iter);
    a11y_append_ref(&iter, bridge, a11y_root_path);
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bridge->bus, message,
                                                                   3000, &error);
    dbus_message_unref(message);
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        if (reply) {
            dbus_message_unref(reply);
        }
        return false;
    }
    if (!reply) {
        return false;
    }
    dbus_message_unref(reply);
    return true;
}

bool pt_a11y_bridge_start(pt_app *app) {
    if (!app) {
        return false;
    }
    if (app->a11y_bridge) {
        return true;
    }
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *bus = a11y_open_bus(&error);
    if (!bus) {
        dbus_error_free(&error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(bus, FALSE);
    pt_a11y_bridge *bridge = pt_alloc_zeroed(1, sizeof(*bridge));
    bridge->app = app;
    bridge->bus = bus;
    bridge->bus_name = dbus_bus_get_unique_name(bus);
    bridge->vtable.message_function = a11y_message;
    if (!dbus_connection_register_object_path(bus, a11y_root_path, &bridge->vtable,
                                              bridge)) {
        a11y_bridge_free(bridge);
        return false;
    }
    if (!dbus_connection_register_fallback(bus, a11y_fallback_path, &bridge->vtable,
                                           bridge)) {
        dbus_connection_unregister_object_path(bus, a11y_root_path);
        a11y_bridge_free(bridge);
        return false;
    }
    if (!a11y_embed_root(bridge)) {
        dbus_connection_unregister_object_path(bus, a11y_root_path);
        dbus_connection_unregister_object_path(bus, a11y_fallback_path);
        a11y_bridge_free(bridge);
        return false;
    }
    app->a11y_bridge = bridge;
    return true;
}

void pt_a11y_bridge_stop(pt_app *app) {
    if (!app || !app->a11y_bridge) {
        return;
    }
    pt_a11y_bridge *bridge = app->a11y_bridge;
    if (bridge->bus && dbus_connection_get_is_connected(bridge->bus)) {
        DBusMessage *message = dbus_message_new_method_call(
            "org.a11y.atspi.Registry", a11y_root_path, "org.a11y.atspi.Socket",
            "Unembed");
        if (message) {
            DBusMessageIter iter;
            dbus_message_iter_init_append(message, &iter);
            a11y_append_ref(&iter, bridge, a11y_root_path);
            dbus_connection_send(bridge->bus, message, NULL);
            dbus_message_unref(message);
        }
    }
    app->a11y_bridge = NULL;
    a11y_bridge_free(bridge);
}

int pt_a11y_bridge_fd(const pt_app *app) {
    if (!app || !app->a11y_bridge || !app->a11y_bridge->bus) {
        return -1;
    }
    int fd = -1;
    if (!dbus_connection_get_unix_fd(app->a11y_bridge->bus, &fd)) {
        return -1;
    }
    return fd;
}

void pt_a11y_bridge_dispatch(pt_app *app) {
    if (!app || !app->a11y_bridge) {
        return;
    }
    pt_a11y_bridge *bridge = app->a11y_bridge;
    if (!bridge->bus || !dbus_connection_get_is_connected(bridge->bus)) {
        pt_a11y_bridge_stop(app);
        if (app->a11y_status == pt_a11y_status_active) {
            app->a11y_status = pt_a11y_status_unavailable;
        }
        return;
    }
    if (!dbus_connection_read_write(bridge->bus, 0)) {
        pt_a11y_bridge_stop(app);
        if (app->a11y_status == pt_a11y_status_active) {
            app->a11y_status = pt_a11y_status_unavailable;
        }
        return;
    }
    while (dbus_connection_get_dispatch_status(bridge->bus) == DBUS_DISPATCH_DATA_REMAINS) {
        if (dbus_connection_dispatch(bridge->bus) == DBUS_DISPATCH_COMPLETE) {
            break;
        }
    }
}

#else /* !PT_HAVE_DBUS */

bool pt_a11y_bridge_start(pt_app *app) {
    (void)app;
    return false;
}

void pt_a11y_bridge_stop(pt_app *app) {
    (void)app;
}

int pt_a11y_bridge_fd(const pt_app *app) {
    (void)app;
    return -1;
}

void pt_a11y_bridge_dispatch(pt_app *app) {
    (void)app;
}

void pt_a11y_bridge_emit(pt_widget *widget, pt_a11y_event_type type) {
    (void)widget;
    (void)type;
}

void pt_a11y_bridge_widget_state(pt_widget *widget, const char *state, bool value) {
    (void)widget;
    (void)state;
    (void)value;
}

void pt_a11y_bridge_announce(pt_widget *widget, int politeness, const char *text) {
    (void)widget;
    (void)politeness;
    (void)text;
}

#endif /* PT_HAVE_DBUS */
