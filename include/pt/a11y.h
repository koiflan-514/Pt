/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_a11y_h
#define pt_a11y_h

#include <pt/widget.h>

/* Header contract
 *
 * Threading. UI thread only. The event callback runs synchronously on the UI
 * thread during dispatch.
 * Ownership. Names, descriptions and text returned by getters are borrowed from
 * the widget and invalidated by the next mutation. The bridge is app-owned;
 * pt_app_enable_a11y returns no handle.
 * Errors. pt_app_enable_a11y returns false for a NULL app, a build without
 * D-Bus, or no reachable accessibility bus; the application keeps working.
 * pt_widget_get_a11y_text and pt_widget_get_a11y_value return false when the
 * widget has no such content.
 * Model. App-level role, name and description overrides are copied. Setting
 * NULL clears the override and restores the derived value. Password entries
 * expose their caret and selection but not their text.
 * Callbacks. The event callback and user_data must stay valid until the
 * callback is replaced or the app is destroyed. The callback must not block.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-neutral accessibility model. Widgets derive a role, name and state
 * from their kind and content; applications may override any of them. The
 * event callback is the bridge point for a platform adapter (AT-SPI2 on
 * Wayland) and stays dormant when no callback is installed. */

typedef enum pt_a11y_role {
    pt_a11y_role_none = 0,
    pt_a11y_role_generic,
    pt_a11y_role_panel,
    pt_a11y_role_label,
    pt_a11y_role_push_button,
    pt_a11y_role_text,
    pt_a11y_role_toggle_button,
    pt_a11y_role_switch,
    pt_a11y_role_slider,
    pt_a11y_role_scroll_pane,
    pt_a11y_role_separator,
    pt_a11y_role_check_box,
    pt_a11y_role_radio_button,
    pt_a11y_role_progress_bar,
    pt_a11y_role_spinner,
    pt_a11y_role_group,
    pt_a11y_role_menu,
    pt_a11y_role_menu_item,
    pt_a11y_role_dialog,
    pt_a11y_role_tooltip,
    pt_a11y_role_alert,
    pt_a11y_role_list,
    pt_a11y_role_list_item,
    pt_a11y_role_text_frame,
    pt_a11y_role_table,
    pt_a11y_role_table_cell,
    pt_a11y_role_table_row,
    pt_a11y_role_column_header,
    pt_a11y_role_tree,
    pt_a11y_role_tree_item,
    pt_a11y_role_tab_list,
    pt_a11y_role_tab,
    pt_a11y_role_tab_panel,
    pt_a11y_role_icon,
    pt_a11y_role_combo_box,
    /* The popup of a combo box: a collapsible set of choices, which AT-SPI
     * distinguishes from a plain list. */
    pt_a11y_role_list_box,
    pt_a11y_role_count
} pt_a11y_role;

typedef enum pt_a11y_state {
    pt_a11y_state_visible = 1u << 0,
    pt_a11y_state_enabled = 1u << 1,
    pt_a11y_state_focusable = 1u << 2,
    pt_a11y_state_focused = 1u << 3,
    pt_a11y_state_checked = 1u << 4,
    pt_a11y_state_selected = 1u << 5,
    pt_a11y_state_editable = 1u << 6,
    pt_a11y_state_expanded = 1u << 7,
    pt_a11y_state_busy = 1u << 8,
    pt_a11y_state_modal = 1u << 9,
    pt_a11y_state_expandable = 1u << 10,
    /* Set on a row that belongs to a multi-selectable view. AT-SPI exposes it
     * as STATE_SELECTABLE so a screen reader can announce that the row can be
     * added to the selection, which STATE_SELECTED alone does not convey. */
    pt_a11y_state_selectable = 1u << 11,
    /* Set on a data view whose selection can hold more than one row at a time.
     * AT-SPI exposes it as STATE_MULTISELECTABLE on the container, which is how
     * a screen reader learns to offer "add to selection" gestures. */
    pt_a11y_state_multi_selectable = 1u << 12
} pt_a11y_state;

typedef enum pt_a11y_event_type {
    pt_a11y_event_focus_changed = 0,
    pt_a11y_event_state_changed,
    pt_a11y_event_text_changed,
    pt_a11y_event_selection_changed,
    pt_a11y_event_value_changed,
    pt_a11y_event_name_changed,
    pt_a11y_event_structure_changed
} pt_a11y_event_type;

typedef enum pt_a11y_status {
    pt_a11y_status_disabled = 0,
    pt_a11y_status_unavailable,
    pt_a11y_status_active
} pt_a11y_status;

typedef void (*pt_a11y_event_callback)(pt_window *window, pt_widget *widget,
                                       pt_a11y_event_type event, void *user_data);

pt_api void pt_app_set_a11y_callback(pt_app *app, pt_a11y_event_callback callback,
                                     void *user_data);

/* Starts the platform accessibility bridge. On Wayland this connects to the
 * AT-SPI2 bus and exports the running windows through D-Bus. Returns true
 * when the bridge became active; a build without D-Bus or a session without
 * an accessibility bus reports false and keeps working normally. */
pt_api bool pt_app_enable_a11y(pt_app *app);
pt_api void pt_app_disable_a11y(pt_app *app);
pt_api pt_a11y_status pt_app_get_a11y_status(const pt_app *app);

pt_api pt_a11y_role pt_widget_get_a11y_role(const pt_widget *widget);
pt_api void pt_widget_set_a11y_role(pt_widget *widget, pt_a11y_role role);
pt_api const char *pt_widget_get_a11y_name(const pt_widget *widget);
pt_api void pt_widget_set_a11y_name(pt_widget *widget, const char *name);
pt_api const char *pt_widget_get_a11y_description(const pt_widget *widget);
pt_api void pt_widget_set_a11y_description(pt_widget *widget, const char *description);
pt_api uint32_t pt_widget_get_a11y_state(const pt_widget *widget);

/* Entry text and caret/selection offsets are UTF-8 byte positions. Password
 * entries expose their caret but not their text. Labels and buttons expose
 * their text with zeroed offsets. Returns false when the widget has no text. */
pt_api bool pt_widget_get_a11y_text(const pt_widget *widget, const char **text,
                                    uint32_t *caret, uint32_t *selection_start,
                                    uint32_t *selection_end);

/* Sliders and progress bars expose [0, 1] values; segmented controls expose
 * the selected index as a float. */
pt_api bool pt_widget_get_a11y_value(const pt_widget *widget, float *value);

#ifdef __cplusplus
}
#endif

#endif
