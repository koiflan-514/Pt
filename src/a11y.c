/*
 * Pt accessibility model: roles, names, states and event notifications.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is the semantic layer consumed by an assistive-technology bridge. The
 * framework keeps the model authoritative and emits change events; binding it
 * to AT-SPI over D-Bus is a separate backend concern.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

static uint32_t a11y_next_id = 1;

uint32_t pt_a11y_alloc_id(void) {
    uint32_t id = a11y_next_id++;
    if (a11y_next_id == 0) {
        a11y_next_id = 1;
    }
    return id ? id : a11y_next_id++;
}

void pt_widget_a11y_emit(pt_widget *widget, pt_a11y_event_type type) {
    if (!widget) {
        return;
    }
    pt_window *window = pt_widget_window(widget);
    if (!window || !window->app) {
        return;
    }
    if (window->app->a11y_callback) {
        window->app->a11y_callback(window, widget, type, window->app->a11y_data);
    }
    pt_a11y_bridge_emit(widget, type);
}

pt_a11y_role pt_widget_get_a11y_role(const pt_widget *widget) {
    if (!widget) {
        return pt_a11y_role_none;
    }
    if (widget->a11y_role != pt_a11y_role_none) {
        return widget->a11y_role;
    }
    switch (widget->kind) {
    case pt_widget_label:
        return pt_a11y_role_label;
    case pt_widget_button:
        return widget->menu_item ? pt_a11y_role_menu_item : pt_a11y_role_push_button;
    case pt_widget_entry:
    case pt_widget_text_area:
        return pt_a11y_role_text_frame;
    case pt_widget_list:
        return pt_a11y_role_list;
    case pt_widget_list_row:
        return pt_a11y_role_list_item;
    case pt_widget_toggle:
        return pt_a11y_role_switch;
    case pt_widget_slider:
        return pt_a11y_role_slider;
    case pt_widget_scroll:
        return pt_a11y_role_scroll_pane;
    case pt_widget_divider:
        return pt_a11y_role_separator;
    case pt_widget_card:
        return pt_a11y_role_panel;
    case pt_widget_spacer:
        return pt_a11y_role_none;
    case pt_widget_checkbox:
        return pt_a11y_role_check_box;
    case pt_widget_radio:
        return pt_a11y_role_radio_button;
    case pt_widget_progress:
        return pt_a11y_role_progress_bar;
    case pt_widget_spinner:
        return pt_a11y_role_spinner;
    case pt_widget_segmented:
        return pt_a11y_role_group;
    case pt_widget_icon:
        return pt_a11y_role_icon;
    case pt_widget_table:
        return pt_a11y_role_table;
    case pt_widget_table_header:
        return pt_a11y_role_generic;
    case pt_widget_table_header_cell:
        return pt_a11y_role_column_header;
    case pt_widget_table_row:
        return pt_a11y_role_table_row;
    case pt_widget_table_cell:
        return pt_a11y_role_table_cell;
    case pt_widget_tree:
        return pt_a11y_role_tree;
    case pt_widget_tree_row:
        return pt_a11y_role_tree_item;
    case pt_widget_tabs:
        return pt_a11y_role_tab_list;
    case pt_widget_tab_page:
        return pt_a11y_role_tab_panel;
    case pt_widget_sidebar:
        return pt_a11y_role_list;
    case pt_widget_sidebar_item:
        return pt_a11y_role_list_item;
    case pt_widget_combobox:
        return pt_a11y_role_combo_box;
    case pt_widget_box:
    default:
        return pt_a11y_role_generic;
    }
}

void pt_widget_set_a11y_role(pt_widget *widget, pt_a11y_role role) {
    if (!widget) {
        return;
    }
    widget->a11y_role = role;
    pt_widget_a11y_emit(widget, pt_a11y_event_structure_changed);
}

const char *pt_widget_get_a11y_name(const pt_widget *widget) {
    if (!widget) {
        return "";
    }
    if (widget->a11y_name) {
        return widget->a11y_name;
    }
    if (widget->kind == pt_widget_tree_row && widget->tree_node) {
        return pt_tree_node_get_text(widget->tree_node);
    }
    if (widget->text && widget->text[0] != '\0') {
        return widget->text;
    }
    if (widget->kind == pt_widget_sidebar) {
        const char *header = pt_sidebar_get_header(widget);
        if (header[0] != '\0') {
            return header;
        }
    }
    if (pt_widget_kind_is_text_input(widget->kind) && widget->placeholder) {
        return widget->placeholder;
    }
    return "";
}

void pt_widget_set_a11y_name(pt_widget *widget, const char *name) {
    if (!widget) {
        return;
    }
    free(widget->a11y_name);
    widget->a11y_name = name && *name ? pt_strdup(name) : NULL;
    pt_widget_a11y_emit(widget, pt_a11y_event_name_changed);
}

const char *pt_widget_get_a11y_description(const pt_widget *widget) {
    return widget && widget->a11y_description ? widget->a11y_description : "";
}

void pt_widget_set_a11y_description(pt_widget *widget, const char *description) {
    if (!widget) {
        return;
    }
    free(widget->a11y_description);
    widget->a11y_description = description && *description ? pt_strdup(description) : NULL;
    pt_widget_a11y_emit(widget, pt_a11y_event_name_changed);
}

static bool widget_ancestors_visible(const pt_widget *widget) {
    for (const pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (!cursor->visible) {
            return false;
        }
    }
    return true;
}

static bool widget_ancestors_enabled(const pt_widget *widget) {
    for (const pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (!cursor->enabled) {
            return false;
        }
    }
    return true;
}

static int widget_tab_page_index(const pt_widget *page) {
    if (page->kind != pt_widget_tab_page || !page->parent ||
        page->parent->kind != pt_widget_tabs) {
        return -1;
    }
    int index = 0;
    for (const pt_widget *child = page->parent->first_child; child;
         child = child->next_sibling) {
        if (child->kind != pt_widget_tab_page) {
            continue;
        }
        if (child == page) {
            return index;
        }
        ++index;
    }
    return -1;
}

uint32_t pt_widget_get_a11y_state(const pt_widget *widget) {
    if (!widget) {
        return 0;
    }
    uint32_t state = 0;
    if (widget_ancestors_visible(widget)) {
        state |= pt_a11y_state_visible;
    }
    if (widget_ancestors_enabled(widget)) {
        state |= pt_a11y_state_enabled;
    }
    if (widget->focusable) {
        state |= pt_a11y_state_focusable;
    }
    if (widget->focused) {
        state |= pt_a11y_state_focused;
    }
    switch (widget->kind) {
    case pt_widget_toggle:
    case pt_widget_checkbox:
    case pt_widget_radio:
        if (widget->checked) {
            state |= pt_a11y_state_checked;
        }
        break;
    case pt_widget_segmented:
        state |= pt_a11y_state_selected;
        break;
    case pt_widget_list_row:
    case pt_widget_table_row:
    case pt_widget_tree_row:
    case pt_widget_sidebar_item:
        /* A row of a data view is one of the choices a user can make, which AT-SPI
         * calls SELECTABLE; SELECTED then says whether this one has been made.
         * Reporting only the selected rows would leave a screen reader unable to
         * announce that an unselected row can be added to the selection. */
        if ((state & pt_a11y_state_enabled) != 0) {
            state |= pt_a11y_state_selectable;
        }
        if (widget->row_selected) {
            state |= pt_a11y_state_selected;
        }
        if (widget->kind == pt_widget_tree_row && widget->tree_node &&
            pt_tree_node_get_child_count(widget->tree_node) > 0) {
            state |= pt_a11y_state_expandable;
            if (pt_tree_node_get_expanded(widget->tree_node)) {
                state |= pt_a11y_state_expanded;
            }
        }
        break;
    case pt_widget_list:
    case pt_widget_table:
    case pt_widget_tree:
        /* All three views keep a set of rows, and two or more of them can be
         * selected at a time. The combo box popup is not one of these: its rows
         * live in a scroll, and picking one closes the popup. */
        state |= pt_a11y_state_multi_selectable;
        break;
    case pt_widget_tab_page: {
        int index = widget_tab_page_index(widget);
        if (index >= 0 && (uint32_t)index == pt_tabs_get_active(widget->parent)) {
            state |= pt_a11y_state_selected;
        }
        break;
    }
    case pt_widget_entry:
    case pt_widget_text_area:
        if (widget->enabled) {
            state |= pt_a11y_state_editable;
        }
        break;
    case pt_widget_progress:
        if (widget->indeterminate) {
            state |= pt_a11y_state_busy;
        }
        break;
    case pt_widget_spinner:
        state |= pt_a11y_state_busy;
        break;
    case pt_widget_combobox:
        state |= pt_a11y_state_expandable;
        if (pt_combobox_is_open(widget)) {
            state |= pt_a11y_state_expanded;
        }
        break;
    default:
        break;
    }
    if (widget->a11y_role == pt_a11y_role_dialog) {
        state |= pt_a11y_state_modal;
    }
    return state;
}

bool pt_widget_get_a11y_text(const pt_widget *widget, const char **text, uint32_t *caret,
                             uint32_t *selection_start, uint32_t *selection_end) {
    if (!widget) {
        return false;
    }
    if (widget->kind == pt_widget_label || widget->kind == pt_widget_button) {
        if (text) {
            *text = widget->text ? widget->text : "";
        }
        if (caret) {
            *caret = 0;
        }
        if (selection_start) {
            *selection_start = 0;
        }
        if (selection_end) {
            *selection_end = 0;
        }
        return true;
    }
    if (!pt_widget_kind_is_text_input(widget->kind) || widget->password) {
        return false;
    }
    size_t length = strlen(widget->text ? widget->text : "");
    if (text) {
        *text = widget->text ? widget->text : "";
    }
    if (caret) {
        *caret = (uint32_t)(widget->cursor <= length ? widget->cursor : length);
    }
    uint32_t start = 0;
    uint32_t end = 0;
    if (!pt_widget_text_selection_get(widget, &start, &end)) {
        start = caret ? *caret : 0;
        end = start;
    }
    if (selection_start) {
        *selection_start = start;
    }
    if (selection_end) {
        *selection_end = end;
    }
    return true;
}

bool pt_widget_get_a11y_value(const pt_widget *widget, float *value) {
    if (!widget || !value) {
        return false;
    }
    switch (widget->kind) {
    case pt_widget_slider:
    case pt_widget_progress:
        *value = widget->value;
        return true;
    case pt_widget_toggle:
    case pt_widget_checkbox:
    case pt_widget_radio:
        *value = widget->checked ? 1.0f : 0.0f;
        return true;
    case pt_widget_segmented:
        *value = (float)widget->segment_selected;
        return true;
    default:
        return false;
    }
}

void pt_app_set_a11y_callback(pt_app *app, pt_a11y_event_callback callback, void *user_data) {
    if (!app) {
        return;
    }
    app->a11y_callback = callback;
    app->a11y_data = user_data;
}

bool pt_app_enable_a11y(pt_app *app) {
    if (!app) {
        pt_set_error("pt_app_enable_a11y requires a valid app");
        return false;
    }
    if (app->a11y_status == pt_a11y_status_active) {
        return true;
    }
    if (pt_a11y_bridge_start(app)) {
        app->a11y_status = pt_a11y_status_active;
        return true;
    }
    app->a11y_status = pt_a11y_status_unavailable;
    return false;
}

void pt_app_disable_a11y(pt_app *app) {
    if (!app) {
        return;
    }
    pt_a11y_bridge_stop(app);
    app->a11y_status = pt_a11y_status_disabled;
}

pt_a11y_status pt_app_get_a11y_status(const pt_app *app) {
    return app ? app->a11y_status : pt_a11y_status_disabled;
}
