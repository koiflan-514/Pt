/*
 * Pt in-window overlays: menus, dialogs, toasts and tooltips.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Overlay panels are detached widget trees with window coordinates. They are
 * laid out after the window content and composited in z-order so the rest of
 * the framework keeps a single window surface.
 */

#include "internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define overlay_edge 8.0f
#define overlay_menu_padding 6.0f
#define overlay_menu_min_width 184.0f
#define overlay_menu_max_width 420.0f
#define overlay_dialog_width 440.0f
#define overlay_dialog_max_width 540.0f
#define overlay_toast_max_width 360.0f
#define overlay_toast_margin 20.0f
#define overlay_toast_gap 10.0f
#define overlay_tooltip_max_width 340.0f
#define overlay_tooltip_delay 600u

void pt_window_set_popup_policy(pt_window *window, pt_popup_policy policy) {
    if (!window) {
        return;
    }
    if (policy != pt_popup_policy_auto && policy != pt_popup_policy_native &&
        policy != pt_popup_policy_window) {
        policy = pt_popup_policy_auto;
    }
    window->popup_policy = policy;
}

pt_popup_policy pt_window_get_popup_policy(const pt_window *window) {
    return window ? window->popup_policy : pt_popup_policy_auto;
}

static void overlay_push_garbage(pt_window *window, pt_widget *panel,
                                 pt_overlay_action *actions) {
    if (!window || (!panel && !actions)) {
        return;
    }
    pt_overlay_garbage *item = pt_alloc_zeroed(1, sizeof(*item));
    item->panel = panel;
    item->actions = actions;
    item->next = window->overlay_garbage;
    window->overlay_garbage = item;
}

void pt_window_overlays_collect(pt_window *window) {
    if (!window) {
        return;
    }
    pt_overlay_garbage *item = window->overlay_garbage;
    window->overlay_garbage = NULL;
    while (item) {
        pt_overlay_garbage *next = item->next;
        if (item->panel) {
            pt_widget_free_tree(item->panel);
        }
        pt_overlay_action *action = item->actions;
        while (action) {
            pt_overlay_action *following = action->next;
            free(action);
            action = following;
        }
        free(item);
        item = next;
    }
}

static bool overlay_contains(const pt_overlay *overlay, const pt_widget *widget) {
    return overlay && overlay->panel && widget &&
           pt_widget_is_descendant(widget, overlay->panel);
}

static void overlay_unlink(pt_window *window, pt_overlay *overlay) {
    if (!window) {
        return;
    }
    pt_overlay **link = &window->overlays;
    while (*link) {
        if (*link == overlay) {
            *link = overlay->next;
            overlay->next = NULL;
            return;
        }
        link = &(*link)->next;
    }
}

static pt_overlay *overlay_latest(const pt_window *window, pt_overlay_kind kind) {
    if (!window) {
        return NULL;
    }
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay->kind == kind) {
            return overlay;
        }
    }
    return NULL;
}

static pt_overlay *overlay_top_interactive(const pt_window *window) {
    pt_overlay *dialog = overlay_latest(window, pt_overlay_kind_dialog);
    if (dialog) {
        return dialog;
    }
    pt_overlay *menu = overlay_latest(window, pt_overlay_kind_menu);
    if (menu) {
        return menu;
    }
    return overlay_latest(window, pt_overlay_kind_combobox);
}

/* Native xdg_popup surfaces are only worth their cost for a popup that stays
 * stable while it is open. A filterable combo box rebuilds its option list on
 * every keystroke, so it keeps the cheaper in-window panel. */
static bool overlay_use_native(const pt_window *window, const pt_overlay *overlay) {
    if (!window || !overlay) {
        return false;
    }
    switch (overlay->kind) {
    case pt_overlay_kind_menu:
    case pt_overlay_kind_tooltip:
        break;
    case pt_overlay_kind_combobox: {
        pt_widget *owner = overlay->owner;
        if (!owner || pt_combobox_get_mode(owner) != pt_combobox_select_only) {
            return false;
        }
        break;
    }
    default:
        return false;
    }
    if (window->popup_policy == pt_popup_policy_window) {
        return false;
    }
    return pt_platform_popup_supported((pt_window *)window);
}

static void overlay_local_point(const pt_overlay *overlay, float *x, float *y) {
    if (overlay && overlay->native) {
        *x -= overlay->rect.x;
        *y -= overlay->rect.y;
    }
}

static pt_widget *overlay_hit_test_at(const pt_overlay *overlay, float x, float y) {
    if (!overlay || !overlay->panel) {
        return NULL;
    }
    overlay_local_point(overlay, &x, &y);
    return pt_widget_hit_test(overlay->panel, x, y);
}

static void cancel_overlay_timer(pt_overlay *overlay) {
    if (overlay && overlay->timer_id && overlay->window && overlay->window->app) {
        pt_app_cancel_timeout(overlay->window->app, overlay->timer_id);
        overlay->timer_id = 0;
    }
}

static void overlay_restore_focus(pt_overlay *overlay) {
    pt_window *window = overlay->window;
    if (!window || overlay->passive) {
        return;
    }
    pt_widget *previous = overlay->previous_focus;
    overlay->previous_focus = NULL;
    if (window->focused && overlay_contains(overlay, window->focused)) {
        window->focused->focused = false;
        pt_a11y_bridge_widget_state(window->focused, "focused", false);
        window->focused = NULL;
        pt_platform_ime_set_enabled(window, false);
    }
    if (window->hovered && overlay_contains(overlay, window->hovered)) {
        window->hovered = NULL;
    }
    if (window->active_widget && overlay_contains(overlay, window->active_widget)) {
        window->active_widget = NULL;
    }
    if (previous && pt_widget_window(previous) == window) {
        pt_widget_focus(previous);
    }
}

static void overlay_detach_panel(pt_overlay *overlay) {
    if (!overlay || !overlay->panel) {
        return;
    }
    pt_window *window = overlay->window;
    pt_widget *panel = overlay->panel;
    panel->window = NULL;
    overlay->panel = NULL;
    overlay_push_garbage(window, panel, overlay->actions);
    overlay->actions = NULL;
}

static void overlay_remove(pt_overlay *overlay) {
    if (!overlay || !overlay->open) {
        return;
    }
    pt_window *window = overlay->window;
    cancel_overlay_timer(overlay);
    if (overlay->popup) {
        pt_platform_popup_destroy(overlay);
    }
    overlay->native = false;
    if (overlay->panel) {
        pt_a11y_bridge_widget_state(overlay->panel, "showing", false);
    }
    overlay_unlink(window, overlay);
    overlay_restore_focus(overlay);
    if (overlay->passive && window) {
        /* A passive popup never took focus, so restore_focus left the pointer
         * state alone. The panel is about to become garbage either way. */
        if (window->active_widget && overlay_contains(overlay, window->active_widget)) {
            window->active_widget = NULL;
        }
        if (window->hovered && overlay_contains(overlay, window->hovered)) {
            window->hovered = NULL;
        }
    }
    overlay->open = false;
    overlay_detach_panel(overlay);
    if (window) {
        pt_window_damage_all(window);
        window->needs_draw = true;
        pt_platform_window_schedule_draw(window);
    }
}

void pt_window_overlay_native_done(pt_overlay *overlay) {
    if (!overlay || !overlay->open) {
        return;
    }
    switch (overlay->kind) {
    case pt_overlay_kind_menu:
        pt_menu_dismiss((pt_menu *)overlay);
        break;
    case pt_overlay_kind_tooltip:
        pt_window_overlays_dismiss_tooltip(overlay->window);
        break;
    case pt_overlay_kind_combobox:
        if (overlay->owner) {
            pt_combobox_close_popup(overlay->owner);
        }
        break;
    default:
        break;
    }
}

static void overlay_link(pt_overlay *overlay) {
    pt_window *window = overlay->window;
    overlay->native = overlay_use_native(window, overlay);
    overlay->next = window->overlays;
    window->overlays = overlay;
    overlay->open = true;
    overlay->previous_focus = window->focused;
    if (overlay->panel) {
        pt_a11y_bridge_widget_state(overlay->panel, "showing", true);
        if (overlay->kind == pt_overlay_kind_toast) {
            pt_toast *toast = (pt_toast *)overlay;
            int politeness = toast->kind == pt_toast_warning ||
                                     toast->kind == pt_toast_error
                                 ? 2
                                 : 1;
            pt_a11y_bridge_announce(overlay->panel, politeness, toast->text);
        } else if (overlay->kind == pt_overlay_kind_dialog) {
            pt_dialog *dialog = (pt_dialog *)overlay;
            pt_a11y_bridge_announce(overlay->panel, 2,
                                    dialog->message ? dialog->message : dialog->title);
        }
    }
    pt_window_damage_all(window);
    window->needs_draw = true;
}

/* ---------- owner-built popups ---------- */

/* A combo box allocates its overlay inside the widget state and hands the panel
 * to the overlay layer here, so dismiss-on-outside-click, native surfaces,
 * damage and panel garbage collection stay shared with menus and dialogs. */
void pt_window_popup_show(pt_overlay *overlay, pt_window *window, pt_widget *owner,
                          pt_widget *panel) {
    if (!overlay || !window || !panel) {
        return;
    }
    if (overlay->open) {
        pt_window_popup_replace_panel(overlay, panel);
        return;
    }
    overlay->window = window;
    overlay->owner = owner;
    overlay->panel = panel;
    panel->window = window;
    overlay_link(overlay);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

void pt_window_popup_hide(pt_overlay *overlay) {
    overlay_remove(overlay);
}

void pt_window_popup_replace_panel(pt_overlay *overlay, pt_widget *panel) {
    if (!overlay || !panel || !overlay->open || !overlay->window ||
        overlay->panel == panel) {
        return;
    }
    pt_window *window = overlay->window;
    pt_widget *old = overlay->panel;
    if (old) {
        if (window->hovered && overlay_contains(overlay, window->hovered)) {
            window->hovered = NULL;
        }
        if (window->active_widget && overlay_contains(overlay, window->active_widget)) {
            window->active_widget = NULL;
        }
        pt_a11y_bridge_widget_state(old, "showing", false);
        old->window = NULL;
        overlay_push_garbage(window, old, NULL);
    }
    overlay->panel = panel;
    panel->window = window;
    pt_a11y_bridge_widget_state(panel, "showing", true);
    pt_window_damage_all(window);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

static void panel_collect_focusables(pt_widget *widget, pt_widget ***items, size_t *count,
                                     size_t *capacity) {    if (!widget || !widget->visible || !widget->enabled) {
        return;
    }
    if (widget->focusable) {
        if (*count == *capacity) {
            *capacity = *capacity ? *capacity * 2 : 8;
            *items = pt_realloc_mem(*items, *capacity * sizeof(**items));
        }
        (*items)[(*count)++] = widget;
        return;
    }
    for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        panel_collect_focusables(child, items, count, capacity);
    }
}

static pt_widget *panel_first_focusable(pt_widget *panel) {
    pt_widget **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    panel_collect_focusables(panel, &items, &count, &capacity);
    pt_widget *first = count > 0 ? items[0] : NULL;
    free(items);
    return first;
}

static void overlay_focus_first(pt_overlay *overlay) {
    if (!overlay || !overlay->panel || overlay->passive) {
        return;
    }
    pt_widget *first = panel_first_focusable(overlay->panel);
    if (first) {
        pt_widget_focus(first);
    }
}

pt_widget *pt_window_overlays_focus_scope(const pt_window *window) {
    pt_overlay *overlay = overlay_top_interactive(window);
    return overlay ? overlay->panel : NULL;
}

bool pt_window_overlays_focus_next(pt_window *window, bool backwards) {
    pt_widget *scope = pt_window_overlays_focus_scope(window);
    if (!scope) {
        return false;
    }
    pt_widget **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    panel_collect_focusables(scope, &items, &count, &capacity);
    if (count == 0) {
        free(items);
        return false;
    }
    size_t index = count;
    for (size_t i = 0; i < count; ++i) {
        if (items[i] == window->focused) {
            index = i;
            break;
        }
    }
    size_t next = 0;
    if (index == count) {
        next = backwards ? count - 1 : 0;
    } else if (backwards) {
        next = (index + count - 1) % count;
    } else {
        next = (index + 1) % count;
    }
    bool result = pt_widget_focus(items[next]);
    free(items);
    return result;
}

bool pt_window_overlays_active(const pt_window *window) {
    return overlay_top_interactive(window) != NULL;
}

bool pt_window_overlays_modal(const pt_window *window) {
    return overlay_latest(window, pt_overlay_kind_dialog) != NULL;
}

/* ---------- menu ---------- */

static void overlay_action_click(pt_widget *widget, void *data) {
    pt_overlay_action *action = data;
    if (!action) {
        return;
    }
    pt_click_callback callback = action->callback;
    void *user_data = action->user_data;
    pt_overlay *overlay = action->overlay;
    if (overlay && overlay->open) {
        if (overlay->kind == pt_overlay_kind_dialog) {
            pt_dialog *dialog = (pt_dialog *)overlay;
            pt_dialog_close(dialog);
        } else {
            overlay_remove(overlay);
        }
    }
    if (callback) {
        callback(widget, user_data);
    }
}

static pt_overlay_action *overlay_add_action(pt_overlay *overlay, pt_click_callback callback,
                                             void *user_data) {
    pt_overlay_action *action = pt_alloc_zeroed(1, sizeof(*action));
    action->overlay = overlay;
    action->callback = callback;
    action->user_data = user_data;
    action->next = overlay->actions;
    overlay->actions = action;
    return action;
}

static void menu_item_clear(pt_menu_item *item) {
    free(item->label);
    free(item->shortcut);
    memset(item, 0, sizeof(*item));
}

static void menu_build_panel(pt_menu *menu, bool focus_first) {
    pt_window *window = menu->overlay.window;
    if (!window) {
        return;
    }
    if (menu->overlay.panel) {
        pt_widget *old = menu->overlay.panel;
        if (window->focused && pt_widget_is_descendant(window->focused, old)) {
            window->focused->focused = false;
            pt_a11y_bridge_widget_state(window->focused, "focused", false);
            window->focused = NULL;
        }
        if (window->hovered && pt_widget_is_descendant(window->hovered, old)) {
            window->hovered = NULL;
        }
        if (window->active_widget && pt_widget_is_descendant(window->active_widget, old)) {
            window->active_widget = NULL;
        }
        old->window = NULL;
        overlay_push_garbage(window, old, menu->overlay.actions);
        menu->overlay.actions = NULL;
        menu->overlay.panel = NULL;
    }

    pt_widget *panel = pt_card_create(NULL);
    panel->window = window;
    panel->direction = pt_direction_column;
    panel->gap = 1.0f;
    panel->padding_top = overlay_menu_padding;
    panel->padding_bottom = overlay_menu_padding;
    panel->padding_left = overlay_menu_padding;
    panel->padding_right = overlay_menu_padding;
    panel->radius = 12.0f;
    panel->a11y_role = pt_a11y_role_menu;
    panel->a11y_name = pt_strdup("Menu");

    pt_layout_ctx ctx = pt_layout_context_for(panel);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    pt_font *font = pt_font_acquire(ctx.font_family, pt_font_weight_regular, pt_font_slant_upright, 13.0f * scale);
    float width = overlay_menu_min_width;
    for (size_t i = 0; i < menu->item_count; ++i) {
        pt_menu_item *item = &menu->items[i];
        if (item->separator) {
            continue;
        }
        float label_width = font ? pt_font_measure(font, item->label ? item->label : "",
                                                   strlen(item->label ? item->label : "")) /
                                       scale
                                 : 0.0f;
        float shortcut_width =
            font && item->shortcut
                ? pt_font_measure(font, item->shortcut, strlen(item->shortcut)) / scale
                : 0.0f;
        float needed = label_width + shortcut_width + (item->shortcut ? 64.0f : 36.0f);
        if (needed > width) {
            width = needed;
        }
    }
    if (width > overlay_menu_max_width) {
        width = overlay_menu_max_width;
    }
    panel->min_width = width;
    panel->max_width = overlay_menu_max_width;

    for (size_t i = 0; i < menu->item_count; ++i) {
        pt_menu_item *item = &menu->items[i];
        if (item->separator) {
            pt_widget *divider = pt_divider_create(panel);
            pt_widget_set_height(divider, 1.0f);
            pt_widget_set_margin(divider, 4.0f, 6.0f, 4.0f, 6.0f);
            continue;
        }
        pt_widget *button = pt_button_create(panel, item->label);
        pt_button_set_variant(button, pt_button_ghost);
        pt_widget_set_font_size(button, 13.0f);
        pt_widget_set_font_weight(button, pt_font_weight_regular);
        pt_widget_set_padding(button, 6.0f, 12.0f, 6.0f, 12.0f);
        pt_widget_set_height(button, 30.0f);
        pt_widget_set_radius(button, 7.0f);
        button->menu_item = true;
        button->shortcut = item->shortcut ? pt_strdup(item->shortcut) : NULL;
        button->a11y_role = pt_a11y_role_menu_item;
        button->a11y_name = pt_strdup(item->label ? item->label : "");
        pt_overlay_action *action =
            overlay_add_action(&menu->overlay, item->callback, item->user_data);
        button->on_click = overlay_action_click;
        button->on_click_data = action;
    }

    menu->overlay.panel = panel;
    menu->overlay.rect.w = width + panel->padding_left + panel->padding_right;
    if (focus_first && menu->overlay.open) {
        overlay_focus_first(&menu->overlay);
    }
    pt_window_damage_all(window);
}

pt_menu *pt_menu_create(pt_window *window) {
    if (!window) {
        pt_set_error("pt_menu_create requires a window");
        return NULL;
    }
    pt_menu *menu = pt_alloc_zeroed(1, sizeof(*menu));
    menu->overlay.kind = pt_overlay_kind_menu;
    menu->overlay.window = window;
    return menu;
}

void pt_menu_destroy(pt_menu *menu) {
    if (!menu) {
        return;
    }
    overlay_remove(&menu->overlay);
    for (size_t i = 0; i < menu->item_count; ++i) {
        menu_item_clear(&menu->items[i]);
    }
    free(menu->items);
    free(menu);
}

void pt_menu_add_item(pt_menu *menu, const char *label, const char *shortcut,
                      pt_click_callback callback, void *user_data) {
    if (!menu) {
        return;
    }
    if (menu->item_count == menu->item_capacity) {
        size_t capacity = menu->item_capacity ? menu->item_capacity * 2 : 8;
        menu->items = pt_realloc_mem(menu->items, capacity * sizeof(*menu->items));
        memset(menu->items + menu->item_capacity, 0,
               (capacity - menu->item_capacity) * sizeof(*menu->items));
        menu->item_capacity = capacity;
    }
    pt_menu_item *item = &menu->items[menu->item_count++];
    item->label = pt_strdup(label ? label : "");
    item->shortcut = shortcut && *shortcut ? pt_strdup(shortcut) : NULL;
    item->callback = callback;
    item->user_data = user_data;
    item->separator = false;
    if (menu->overlay.open) {
        menu_build_panel(menu, false);
    }
}

void pt_menu_add_separator(pt_menu *menu) {
    if (!menu) {
        return;
    }
    if (menu->item_count == menu->item_capacity) {
        size_t capacity = menu->item_capacity ? menu->item_capacity * 2 : 8;
        menu->items = pt_realloc_mem(menu->items, capacity * sizeof(*menu->items));
        memset(menu->items + menu->item_capacity, 0,
               (capacity - menu->item_capacity) * sizeof(*menu->items));
        menu->item_capacity = capacity;
    }
    pt_menu_item *item = &menu->items[menu->item_count++];
    item->separator = true;
    if (menu->overlay.open) {
        menu_build_panel(menu, false);
    }
}

void pt_menu_show_at(pt_menu *menu, float x, float y) {
    if (!menu || !menu->overlay.window) {
        return;
    }
    pt_window *window = menu->overlay.window;
    for (pt_overlay *overlay = window->overlays; overlay;) {
        pt_overlay *next = overlay->next;
        if (overlay != &menu->overlay && overlay->open &&
            overlay->kind == pt_overlay_kind_menu) {
            pt_menu_dismiss((pt_menu *)overlay);
        }
        overlay = next;
    }
    pt_window_overlays_dismiss_tooltip(window);
    bool was_open = menu->overlay.open;
    menu_build_panel(menu, false);
    menu->overlay.rect.x = x;
    menu->overlay.rect.y = y;
    menu->overlay.anchor_x = x;
    menu->overlay.anchor_y = y;
    if (!was_open) {
        overlay_link(&menu->overlay);
    }
    overlay_focus_first(&menu->overlay);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

void pt_menu_show_below(pt_menu *menu, pt_widget *anchor) {
    if (!menu || !anchor) {
        return;
    }
    pt_menu_show_at(menu, anchor->x, anchor->y + anchor->height + 6.0f);
}

void pt_menu_dismiss(pt_menu *menu) {
    if (!menu) {
        return;
    }
    overlay_remove(&menu->overlay);
}

bool pt_menu_is_open(const pt_menu *menu) {
    return menu && menu->overlay.open;
}

/* ---------- dialog ---------- */

static void dialog_apply_texts(pt_dialog *dialog) {
    if (dialog->title_label) {
        pt_widget_set_text(dialog->title_label, dialog->title ? dialog->title : "");
        pt_widget_set_visible(dialog->title_label, dialog->title && dialog->title[0] != '\0');
    }
    if (dialog->message_label) {
        pt_widget_set_text(dialog->message_label, dialog->message ? dialog->message : "");
        pt_widget_set_visible(dialog->message_label,
                              dialog->message && dialog->message[0] != '\0');
    }
    if (dialog->overlay.panel) {
        free(dialog->overlay.panel->a11y_name);
        dialog->overlay.panel->a11y_name =
            pt_strdup(dialog->title ? dialog->title : "Dialog");
    }
}

static void dialog_build_panel(pt_dialog *dialog) {
    pt_window *window = dialog->overlay.window;
    if (!window) {
        return;
    }
    pt_widget *panel = pt_card_create(NULL);
    panel->window = window;
    panel->direction = pt_direction_column;
    panel->gap = 14.0f;
    panel->padding_top = 22.0f;
    panel->padding_bottom = 20.0f;
    panel->padding_left = 24.0f;
    panel->padding_right = 24.0f;
    panel->radius = 16.0f;
    panel->min_width = 360.0f;
    panel->max_width = overlay_dialog_max_width;
    panel->a11y_role = pt_a11y_role_dialog;
    panel->a11y_name = pt_strdup(dialog->title ? dialog->title : "Dialog");

    dialog->title_label = pt_label_create(panel, dialog->title ? dialog->title : "");
    pt_widget_set_font_size(dialog->title_label, 19.0f);
    pt_widget_set_font_weight(dialog->title_label, pt_font_weight_semibold);
    dialog->title_label->a11y_role = pt_a11y_role_label;

    dialog->message_label = pt_label_create(panel, dialog->message ? dialog->message : "");
    pt_widget_set_font_size(dialog->message_label, 13.0f);
    pt_widget_set_label_wrap(dialog->message_label, true);
    if (window->app) {
        pt_widget_set_foreground(
            dialog->message_label,
            pt_app_get_theme_color(window->app, pt_theme_text_muted));
    }
    dialog->message_label->a11y_role = pt_a11y_role_label;

    dialog->actions_row = pt_row_create(panel);
    dialog->actions_row->justify = pt_justify_end;
    dialog->actions_row->align_items = pt_align_center;
    dialog->actions_row->gap = 8.0f;
    dialog->overlay.panel = panel;
}

pt_dialog *pt_dialog_create(pt_window *window, const char *title, const char *message) {
    if (!window) {
        pt_set_error("pt_dialog_create requires a window");
        return NULL;
    }
    pt_dialog *dialog = pt_alloc_zeroed(1, sizeof(*dialog));
    dialog->overlay.kind = pt_overlay_kind_dialog;
    dialog->overlay.window = window;
    dialog->overlay.modal = true;
    dialog->title = pt_strdup(title ? title : "");
    dialog->message = pt_strdup(message ? message : "");
    dialog_build_panel(dialog);
    return dialog;
}

void pt_dialog_destroy(pt_dialog *dialog) {
    if (!dialog) {
        return;
    }
    overlay_remove(&dialog->overlay);
    free(dialog->title);
    free(dialog->message);
    free(dialog);
}

void pt_dialog_set_title(pt_dialog *dialog, const char *title) {
    if (!dialog) {
        return;
    }
    char *copy = pt_strdup(title ? title : "");
    free(dialog->title);
    dialog->title = copy;
    dialog_apply_texts(dialog);
    pt_window_invalidate_layout(dialog->overlay.window, NULL);
    dialog->overlay.window->needs_draw = true;
    pt_window_damage_all(dialog->overlay.window);
    pt_platform_window_schedule_draw(dialog->overlay.window);
}

void pt_dialog_set_message(pt_dialog *dialog, const char *message) {
    if (!dialog) {
        return;
    }
    char *copy = pt_strdup(message ? message : "");
    free(dialog->message);
    dialog->message = copy;
    dialog_apply_texts(dialog);
    pt_window_invalidate_layout(dialog->overlay.window, NULL);
    dialog->overlay.window->needs_draw = true;
    pt_window_damage_all(dialog->overlay.window);
    pt_platform_window_schedule_draw(dialog->overlay.window);
}

void pt_dialog_add_action(pt_dialog *dialog, const char *label, bool primary,
                          pt_click_callback callback, void *user_data) {
    if (!dialog || !dialog->actions_row) {
        return;
    }
    pt_widget *button = pt_button_create(dialog->actions_row, label);
    pt_button_set_variant(button, primary ? pt_button_primary : pt_button_secondary);
    pt_widget_set_font_size(button, 13.0f);
    pt_widget_set_padding(button, 8.0f, 16.0f, 8.0f, 16.0f);
    button->a11y_role = pt_a11y_role_push_button;
    if (primary) {
        for (pt_widget *child = dialog->actions_row->first_child; child;
             child = child->next_sibling) {
            if (child != button && child->button_variant == pt_button_primary) {
                pt_button_set_variant(child, pt_button_secondary);
            }
        }
    }
    pt_overlay_action *action =
        overlay_add_action(&dialog->overlay, callback, user_data);
    button->on_click = overlay_action_click;
    button->on_click_data = action;
    pt_window_invalidate_layout(dialog->overlay.window, NULL);
    dialog->overlay.window->needs_draw = true;
    pt_window_damage_all(dialog->overlay.window);
}

void pt_dialog_show(pt_dialog *dialog) {
    if (!dialog || dialog->overlay.open || !dialog->overlay.window) {
        return;
    }
    if (!dialog->overlay.panel) {
        dialog_build_panel(dialog);
    }
    pt_window_overlays_dismiss_tooltip(dialog->overlay.window);
    overlay_link(&dialog->overlay);
    overlay_focus_first(&dialog->overlay);
    pt_window_invalidate_layout(dialog->overlay.window, NULL);
    dialog->overlay.window->needs_draw = true;
    pt_platform_window_schedule_draw(dialog->overlay.window);
}

void pt_dialog_close(pt_dialog *dialog) {
    if (!dialog || !dialog->overlay.open || dialog->closing) {
        return;
    }
    dialog->closing = true;
    pt_dialog_callback callback = dialog->on_close;
    void *user_data = dialog->on_close_data;
    overlay_remove(&dialog->overlay);
    dialog->closing = false;
    if (callback) {
        callback(dialog, user_data);
    }
}

bool pt_dialog_is_open(const pt_dialog *dialog) {
    return dialog && dialog->overlay.open;
}

void pt_dialog_set_on_close(pt_dialog *dialog, pt_dialog_callback callback, void *user_data) {
    if (dialog) {
        dialog->on_close = callback;
        dialog->on_close_data = user_data;
    }
}

/* ---------- toast ---------- */

static pt_color toast_kind_color(const pt_window *window, pt_toast_kind kind) {
    pt_theme_token token = pt_theme_accent;
    switch (kind) {
    case pt_toast_success:
        token = pt_theme_success;
        break;
    case pt_toast_warning:
        token = pt_theme_warning;
        break;
    case pt_toast_error:
        token = pt_theme_danger;
        break;
    case pt_toast_info:
    default:
        token = pt_theme_accent;
        break;
    }
    return window && window->app ? window->app->theme.colors[token] : 0x5B8CFFFFu;
}

static void toast_build_panel(pt_toast *toast) {
    pt_window *window = toast->overlay.window;
    if (!window) {
        return;
    }
    pt_widget *panel = pt_card_create(NULL);
    panel->window = window;
    panel->direction = pt_direction_row;
    panel->gap = 10.0f;
    panel->padding_top = 12.0f;
    panel->padding_bottom = 12.0f;
    panel->padding_left = 14.0f;
    panel->padding_right = 16.0f;
    panel->radius = 12.0f;
    panel->min_width = 240.0f;
    panel->max_width = overlay_toast_max_width;
    panel->a11y_role = pt_a11y_role_alert;
    panel->a11y_name = pt_strdup(toast->text ? toast->text : "");

    pt_color accent = toast_kind_color(window, toast->kind);
    pt_widget *dot = pt_box_create(panel, pt_direction_column);
    dot->has_background = true;
    dot->background = accent;
    dot->requested_width = 8.0f;
    dot->requested_height = 8.0f;
    dot->radius = 4.0f;
    dot->align_self = pt_align_center;
    dot->a11y_role = pt_a11y_role_none;

    pt_widget *label = pt_label_create(panel, toast->text ? toast->text : "");
    pt_widget_set_font_size(label, 13.0f);
    pt_widget_set_label_wrap(label, true);
    pt_widget_set_grow(label, 1.0f);
    label->a11y_role = pt_a11y_role_label;

    toast->overlay.panel = panel;
}

static void toast_timeout(pt_app *app, void *user_data) {
    (void)app;
    pt_toast *toast = user_data;
    if (!toast) {
        return;
    }
    toast->overlay.timer_id = 0;
    pt_toast_dismiss(toast);
}

static void toast_append(pt_window *window, pt_toast *toast) {
    if (!window->first_toast) {
        window->first_toast = toast;
        return;
    }
    pt_toast *cursor = window->first_toast;
    while (cursor->next) {
        cursor = cursor->next;
    }
    cursor->next = toast;
}

pt_toast *pt_window_show_toast(pt_window *window, const char *text, pt_toast_kind kind,
                               uint32_t duration_ms) {
    if (!window) {
        pt_set_error("pt_window_show_toast requires a window");
        return NULL;
    }
    pt_toast *toast = pt_alloc_zeroed(1, sizeof(*toast));
    toast->overlay.kind = pt_overlay_kind_toast;
    toast->overlay.window = window;
    toast->overlay.passive = true;
    toast->kind = kind;
    toast->text = pt_strdup(text ? text : "");
    toast->duration_ms = duration_ms;
    toast_build_panel(toast);
    overlay_link(&toast->overlay);
    toast_append(window, toast);
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    if (duration_ms > 0 && window->app) {
        toast->overlay.timer_id =
            pt_app_add_timeout_internal(window->app, duration_ms, toast_timeout, toast);
    }
    pt_platform_window_schedule_draw(window);
    return toast;
}

void pt_toast_dismiss(pt_toast *toast) {
    if (!toast) {
        return;
    }
    overlay_remove(&toast->overlay);
}

bool pt_toast_is_visible(const pt_toast *toast) {
    return toast && toast->overlay.open;
}

/* ---------- tooltip ---------- */

static void tooltip_timeout(pt_app *app, void *user_data) {
    (void)app;
    pt_window *window = user_data;
    if (!window || window->close_requested || !window->tooltip_target) {
        return;
    }
    window->tooltip_timer = 0;
    pt_widget *target = window->tooltip_target;
    if (!target->tooltip) {
        return;
    }
    bool hovered = false;
    for (pt_widget *cursor = window->hovered; cursor; cursor = cursor->parent) {
        if (cursor == target) {
            hovered = true;
            break;
        }
    }
    if (!hovered) {
        return;
    }
    pt_tooltip *tooltip = pt_alloc_zeroed(1, sizeof(*tooltip));
    tooltip->overlay.kind = pt_overlay_kind_tooltip;
    tooltip->overlay.window = window;
    tooltip->overlay.passive = true;
    tooltip->target = target;

    pt_widget *panel = pt_box_create(NULL, pt_direction_column);
    panel->window = window;
    panel->has_background = true;
    panel->background = window->app ? window->app->theme.colors[pt_theme_surface_raised]
                                    : 0x161B26FFu;
    panel->has_border_color = true;
    panel->border_color = window->app ? window->app->theme.colors[pt_theme_border_strong]
                                      : 0x39445AFFu;
    panel->has_border_width = true;
    panel->border_width = 1.0f;
    panel->radius = 8.0f;
    panel->padding_top = 7.0f;
    panel->padding_bottom = 7.0f;
    panel->padding_left = 10.0f;
    panel->padding_right = 10.0f;
    panel->a11y_role = pt_a11y_role_tooltip;
    panel->a11y_name = pt_strdup(target->tooltip);
    pt_widget *label = pt_label_create(panel, target->tooltip);
    pt_widget_set_font_size(label, 12.0f);
    label->a11y_role = pt_a11y_role_label;
    tooltip->overlay.panel = panel;

    pt_layout_ctx ctx = pt_layout_context_for(panel);
    float width = 0.0f;
    float height = 0.0f;
    pt_widget_measure(panel, &ctx, overlay_tooltip_max_width, 600.0f, &width, &height);
    if (width > overlay_tooltip_max_width) {
        width = overlay_tooltip_max_width;
    }
    bool native = overlay_use_native(window, &tooltip->overlay);
    float x = window->pointer_x + 14.0f;
    float y = window->pointer_y + 20.0f;
    if (!native) {
        if (x + width > (float)window->width - overlay_edge) {
            x = (float)window->width - width - overlay_edge;
        }
        if (y + height > (float)window->height - overlay_edge) {
            y = window->pointer_y - height - 10.0f;
        }
        if (x < overlay_edge) {
            x = overlay_edge;
        }
        if (y < window->header_height + overlay_edge) {
            y = window->header_height + overlay_edge;
        }
    }
    tooltip->overlay.anchor_x = x;
    tooltip->overlay.anchor_y = y;
    tooltip->overlay.rect = (pt_rect){x, y, width, height};
    overlay_link(&tooltip->overlay);
    /* Tooltips never steal focus. */
    tooltip->overlay.previous_focus = NULL;
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

void pt_window_overlays_dismiss_tooltip(pt_window *window) {
    if (!window) {
        return;
    }
    if (window->tooltip_timer && window->app) {
        pt_app_cancel_timeout(window->app, window->tooltip_timer);
        window->tooltip_timer = 0;
    }
    window->tooltip_target = NULL;
    pt_overlay *tooltip = overlay_latest(window, pt_overlay_kind_tooltip);
    if (tooltip) {
        pt_tooltip *owner = (pt_tooltip *)tooltip;
        overlay_remove(tooltip);
        free(owner);
    }
}

void pt_window_overlays_update_tooltip(pt_window *window) {
    if (!window) {
        return;
    }
    pt_widget *target = window->hovered;
    while (target && !(target->tooltip && target->tooltip[0])) {
        target = target->parent;
    }
    pt_overlay *open = overlay_latest(window, pt_overlay_kind_tooltip);
    if (open) {
        pt_tooltip *tooltip = (pt_tooltip *)open;
        if (tooltip->target == target) {
            return;
        }
        pt_window_overlays_dismiss_tooltip(window);
    }
    if (window->tooltip_target == target && window->tooltip_timer) {
        return;
    }
    if (window->tooltip_timer && window->app) {
        pt_app_cancel_timeout(window->app, window->tooltip_timer);
        window->tooltip_timer = 0;
    }
    window->tooltip_target = target;
    if (!target || !target->tooltip || !target->tooltip[0] || !window->app) {
        return;
    }
    window->tooltip_timer = pt_app_add_timeout_internal(
        window->app, target->tooltip_delay, tooltip_timeout, window);
}

/* ---------- layout and paint ---------- */

static void overlay_clamp_rect(pt_window *window, pt_rect *rect, bool allow_flip) {
    (void)allow_flip;
    float min_x = overlay_edge;
    float max_x = (float)window->width - rect->w - overlay_edge;
    float min_y = window->header_height + overlay_edge;
    float max_y = (float)window->height - rect->h - overlay_edge;
    if (max_x < min_x) {
        rect->x = min_x;
        rect->w = pt_max_float(1.0f, (float)window->width - min_x * 2.0f);
    } else if (rect->x > max_x) {
        rect->x = max_x;
    }
    if (rect->x < min_x) {
        rect->x = min_x;
    }
    if (max_y < min_y) {
        rect->y = min_y;
        rect->h = pt_max_float(1.0f, (float)window->height - min_y - overlay_edge);
    } else if (rect->y > max_y) {
        rect->y = max_y;
    }
    if (rect->y < min_y) {
        rect->y = min_y;
    }
}

void pt_window_overlays_layout(pt_window *window, const pt_layout_ctx *ctx) {
    if (!window) {
        return;
    }
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (!overlay->open || !overlay->panel) {
            continue;
        }
        float width = 0.0f;
        float height = 0.0f;
        switch (overlay->kind) {
        case pt_overlay_kind_menu: {
            pt_widget_measure(overlay->panel, ctx, overlay_menu_max_width, 900.0f, &width,
                              &height);
            overlay->rect.w = width;
            overlay->rect.h = height;
            if (!overlay->native) {
                overlay_clamp_rect(window, &overlay->rect, true);
            }
            break;
        }
        case pt_overlay_kind_dialog: {
            pt_widget_measure(overlay->panel, ctx, overlay_dialog_max_width, 900.0f, &width,
                              &height);
            width = pt_clamp_float(width, 360.0f, (float)overlay_dialog_max_width);
            if (width > (float)window->width - overlay_edge * 2.0f) {
                width = pt_max_float(1.0f, (float)window->width - overlay_edge * 2.0f);
            }
            height = pt_min_float(height, (float)window->height - overlay_edge * 2.0f);
            overlay->rect.w = width;
            overlay->rect.h = height;
            overlay->rect.x = ((float)window->width - width) * 0.5f;
            overlay->rect.y = window->header_height +
                              ((float)window->height - window->header_height - height) * 0.5f;
            overlay_clamp_rect(window, &overlay->rect, false);
            break;
        }
        case pt_overlay_kind_tooltip:
            /* Position was fixed when the tooltip appeared. */
            break;
        case pt_overlay_kind_toast:
            break;
        case pt_overlay_kind_combobox:
            if (overlay->owner) {
                pt_combobox_popup_layout(overlay->owner, overlay, ctx);
            }
            break;
        }
        float panel_x = overlay->native ? 0.0f : overlay->rect.x;
        float panel_y = overlay->native ? 0.0f : overlay->rect.y;
        pt_widget_layout(overlay->panel, panel_x, panel_y, overlay->rect.w, overlay->rect.h,
                         ctx);
    }

    float cursor = (float)window->height - overlay_toast_margin;
    float max_width = pt_min_float(overlay_toast_max_width,
                                   (float)window->width - overlay_toast_margin * 2.0f);
    if (max_width < 120.0f) {
        max_width = pt_max_float(80.0f, (float)window->width - overlay_toast_margin);
    }
    for (pt_toast *toast = window->first_toast; toast; toast = toast->next) {
        if (!toast->overlay.open || !toast->overlay.panel) {
            continue;
        }
        float width = 0.0f;
        float height = 0.0f;
        pt_widget_measure(toast->overlay.panel, ctx, max_width, 600.0f, &width, &height);
        width = pt_min_float(width, max_width);
        toast->overlay.rect.w = width;
        toast->overlay.rect.h = height;
        toast->overlay.rect.x = (float)window->width - overlay_toast_margin - width;
        if (toast->overlay.rect.x < overlay_toast_margin) {
            toast->overlay.rect.x = overlay_toast_margin;
        }
        cursor -= height;
        toast->overlay.rect.y = cursor;
        cursor -= overlay_toast_gap;
        pt_widget_layout(toast->overlay.panel, toast->overlay.rect.x,
                         toast->overlay.rect.y, toast->overlay.rect.w,
                         toast->overlay.rect.h, ctx);
    }
}

static void overlay_paint_group(pt_window *window, pt_canvas *canvas,
                                const pt_layout_ctx *ctx, pt_overlay_kind kind) {
    /* Oldest first so newer surfaces stay on top. */
    pt_overlay **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay->kind == kind && overlay->panel && !overlay->native) {
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 8;
                items = pt_realloc_mem(items, capacity * sizeof(*items));
            }
            items[count++] = overlay;
        }
    }
    for (size_t i = count; i > 0; --i) {
        pt_widget_paint(items[i - 1]->panel, canvas, ctx);
    }
    free(items);
}

void pt_window_overlays_paint(pt_window *window, pt_canvas *canvas) {
    if (!window) {
        return;
    }
    bool any = false;
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    if (!window->app) {
        ctx = (pt_layout_ctx){canvas->scale > 0.0f ? canvas->scale : 1.0f, "sans-serif"};
    }
    pt_window_overlays_layout(window, &ctx);

    if (pt_window_overlays_modal(window)) {
        bool dark = !window->app || window->app->theme.scheme == pt_color_scheme_dark;
        pt_color scrim = dark ? 0x05070BB0u : 0x0F172A40u;
        pt_canvas_fill_rect(canvas, 0.0f, 0.0f, (float)window->width,
                            (float)window->height, scrim);
    }
    overlay_paint_group(window, canvas, &ctx, pt_overlay_kind_toast);
    overlay_paint_group(window, canvas, &ctx, pt_overlay_kind_menu);
    overlay_paint_group(window, canvas, &ctx, pt_overlay_kind_combobox);
    overlay_paint_group(window, canvas, &ctx, pt_overlay_kind_dialog);
    overlay_paint_group(window, canvas, &ctx, pt_overlay_kind_tooltip);
    pt_platform_present_popups(window, &ctx);
}

/* ---------- interaction ---------- */

pt_widget *pt_window_overlay_hit_test(pt_window *window, float x, float y) {
    if (!window) {
        return NULL;
    }
    for (int priority = 3; priority >= 1; --priority) {
        pt_overlay_kind kind = priority == 3
                                   ? pt_overlay_kind_dialog
                                   : (priority == 2 ? pt_overlay_kind_menu
                                                    : pt_overlay_kind_combobox);
        for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
            if (!overlay->open || overlay->kind != kind || !overlay->panel) {
                continue;
            }
            if (!pt_rect_contains(overlay->rect, x, y)) {
                continue;
            }
            return overlay_hit_test_at(overlay, x, y);
        }
    }
    return NULL;
}

static bool overlay_widget_belongs_to_window(const pt_window *window, const pt_widget *widget) {
    for (const pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay_contains(overlay, widget)) {
            return true;
        }
    }
    return false;
}

static bool overlay_release_active(pt_window *window, pt_overlay *overlay, float x, float y) {
    pt_widget *active = window->active_widget;
    if (!active || !overlay || !overlay->panel ||
        !pt_widget_is_descendant(active, overlay->panel)) {
        return false;
    }
    overlay_local_point(overlay, &x, &y);
    window->active_widget = NULL;
    bool inside = pt_rect_contains(
        (pt_rect){active->x, active->y, active->width, active->height}, x, y);
    pt_widget_pointer_up(active, inside);
    window->needs_draw = true;
    return true;
}

bool pt_window_overlays_pointer(pt_window *window, uint32_t button, bool pressed, float x,
                                float y, uint32_t serial, uint32_t time) {
    if (!window || button != 0x110u) {
        return false;
    }
    pt_overlay *dialog = overlay_latest(window, pt_overlay_kind_dialog);
    pt_overlay *menu = overlay_latest(window, pt_overlay_kind_menu);
    if (pressed) {
        pt_window_overlays_dismiss_tooltip(window);
    }
    if (dialog) {
        if (pressed) {
            pt_widget *hit = overlay_hit_test_at(dialog, x, y);
            if (hit) {
                pt_widget *focusable = pt_widget_focusable_ancestor(hit);
                if (focusable) {
                    pt_widget_focus(focusable);
                }
                float local_x = x;
                float local_y = y;
                overlay_local_point(dialog, &local_x, &local_y);
                window->active_widget = hit;
                pt_widget_pointer_down(hit, local_x, local_y);
            }
            window->needs_draw = true;
        } else {
            overlay_release_active(window, dialog, x, y);
            window->active_widget = NULL;
        }
        pt_window_overlays_collect(window);
        return true;
    }
    if (menu) {
        if (pressed) {
            pt_widget *hit = overlay_hit_test_at(menu, x, y);
            if (hit) {
                pt_widget *focusable = pt_widget_focusable_ancestor(hit);
                if (focusable) {
                    pt_widget_focus(focusable);
                }
                float local_x = x;
                float local_y = y;
                overlay_local_point(menu, &local_x, &local_y);
                window->active_widget = hit;
                pt_widget_pointer_down(hit, local_x, local_y);
            } else if (!menu->native) {
                pt_menu_dismiss((pt_menu *)menu);
            }
            window->needs_draw = true;
        } else {
            overlay_release_active(window, menu, x, y);
            window->active_widget = NULL;
        }
        pt_window_overlays_collect(window);
        return true;
    }
    pt_overlay *combo = overlay_latest(window, pt_overlay_kind_combobox);
    if (combo) {
        if (pressed) {
            pt_widget *hit = overlay_hit_test_at(combo, x, y);
            if (hit) {
                float local_x = x;
                float local_y = y;
                overlay_local_point(combo, &local_x, &local_y);
                window->active_widget = hit;
                pt_widget_pointer_down(hit, local_x, local_y);
            } else if (combo->owner && pt_combobox_field_hit(combo->owner, x, y)) {
                /* The press belongs to the editor itself: the popup stays open
                 * and the window routes the click so the caret moves. */
                return false;
            } else if (combo->owner) {
                pt_combobox_close_popup(combo->owner);
            }
            window->needs_draw = true;
        } else if (!overlay_release_active(window, combo, x, y)) {
            window->active_widget = NULL;
            return false;
        }
        pt_window_overlays_collect(window);
        return true;
    }
    if (!pressed && window->active_widget &&
        overlay_widget_belongs_to_window(window, window->active_widget)) {
        for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
            if (overlay->open && overlay_contains(overlay, window->active_widget)) {
                overlay_release_active(window, overlay, x, y);
                break;
            }
        }
        pt_window_overlays_collect(window);
        return true;
    }
    (void)serial;
    (void)time;
    return false;
}

static bool menu_focus_step(pt_window *window, int direction) {
    pt_widget *scope = pt_window_overlays_focus_scope(window);
    if (!scope) {
        return false;
    }
    pt_widget **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    panel_collect_focusables(scope, &items, &count, &capacity);
    if (count == 0) {
        free(items);
        return false;
    }
    size_t index = count;
    for (size_t i = 0; i < count; ++i) {
        if (items[i] == window->focused) {
            index = i;
            break;
        }
    }
    size_t next = 0;
    if (index == count) {
        next = direction < 0 ? count - 1 : 0;
    } else if (direction < 0) {
        next = (index + count - 1) % count;
    } else {
        next = (index + 1) % count;
    }
    bool result = pt_widget_focus(items[next]);
    free(items);
    return result;
}

bool pt_window_overlays_key(pt_window *window, uint32_t keysym, uint32_t modifiers,
                            const char *utf8) {
    (void)utf8;
    if (!window) {
        return false;
    }
    bool tooltip_open = overlay_latest(window, pt_overlay_kind_tooltip) != NULL;
    if (tooltip_open) {
        pt_window_overlays_dismiss_tooltip(window);
    }
    pt_overlay *dialog = overlay_latest(window, pt_overlay_kind_dialog);
    pt_overlay *menu = overlay_latest(window, pt_overlay_kind_menu);
    if (!dialog && !menu) {
        pt_overlay *combo = overlay_latest(window, pt_overlay_kind_combobox);
        if (!combo || !combo->owner) {
            return false;
        }
        /* The combo box keeps keyboard focus in its own field, so a key the
         * popup does not consume has to reach the normal route. */
        bool consumed = pt_combobox_key(combo->owner, keysym, modifiers, utf8);
        if (!consumed) {
            return false;
        }
        window->needs_draw = true;
        pt_platform_window_schedule_draw(window);
        pt_window_overlays_collect(window);
        return true;
    }
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool consumed = false;
    if (keysym == XKB_KEY_Escape) {
        if (dialog) {
            pt_dialog_close((pt_dialog *)dialog);
        } else if (menu) {
            pt_menu_dismiss((pt_menu *)menu);
        }
        consumed = true;
    } else if (keysym == XKB_KEY_Tab) {
        pt_window_overlays_focus_next(window, shift);
        consumed = true;
    } else if (menu) {
        if (keysym == XKB_KEY_Up || keysym == XKB_KEY_Left) {
            menu_focus_step(window, -1);
        } else if (keysym == XKB_KEY_Down || keysym == XKB_KEY_Right) {
            menu_focus_step(window, 1);
        } else if (keysym == XKB_KEY_Home) {
            pt_widget *scope = pt_window_overlays_focus_scope(window);
            pt_widget *first = scope ? panel_first_focusable(scope) : NULL;
            if (first) {
                pt_widget_focus(first);
            }
        } else if (keysym == XKB_KEY_End) {
            pt_widget *scope = pt_window_overlays_focus_scope(window);
            if (scope) {
                pt_widget **items = NULL;
                size_t count = 0;
                size_t capacity = 0;
                panel_collect_focusables(scope, &items, &count, &capacity);
                if (count > 0) {
                    pt_widget_focus(items[count - 1]);
                }
                free(items);
            }
        } else if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter ||
                   keysym == XKB_KEY_space) {
            if (window->focused) {
                pt_widget_activate(window->focused);
            }
        }
        consumed = true;
    } else if (dialog) {
        if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter ||
            keysym == XKB_KEY_space) {
            if (window->focused) {
                pt_widget_activate(window->focused);
            }
        }
        consumed = true;
    }
    (void)consumed;
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
    pt_window_overlays_collect(window);
    return true;
}

bool pt_window_overlays_axis(pt_window *window, float x, float y, float delta) {
    if (!pt_window_overlays_active(window)) {
        return false;
    }
    pt_widget *hit = window->hovered;
    if (hit && overlay_widget_belongs_to_window(window, hit)) {
        pt_widget_wheel(hit, delta);
    } else {
        pt_overlay *top = overlay_top_interactive(window);
        hit = top ? overlay_hit_test_at(top, x, y) : NULL;
        if (hit) {
            pt_widget_wheel(hit, delta);
        }
    }
    return true;
}

void pt_window_overlays_tick(pt_window *window, float dt) {
    if (!window) {
        return;
    }
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay->panel) {
            if (pt_widget_update_animations(overlay->panel, dt)) {
                window->needs_draw = true;
                window->animating = true;
            }
        }
    }
}

void pt_window_overlays_forget_widget(pt_window *window, pt_widget *root) {
    if (!window || !root) {
        return;
    }
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->previous_focus && pt_widget_is_descendant(overlay->previous_focus, root)) {
            overlay->previous_focus = NULL;
        }
    }
    if (window->tooltip_target && pt_widget_is_descendant(window->tooltip_target, root)) {
        pt_window_overlays_dismiss_tooltip(window);
    }
}

void pt_window_overlays_free(pt_window *window) {
    if (!window) {
        return;
    }
    if (window->tooltip_timer && window->app) {
        pt_app_cancel_timeout(window->app, window->tooltip_timer);
        window->tooltip_timer = 0;
    }
    window->tooltip_target = NULL;

    pt_overlay *overlay = window->overlays;
    while (overlay) {
        pt_overlay *next = overlay->next;
        cancel_overlay_timer(overlay);
        overlay->open = false;
        overlay->previous_focus = NULL;
        if (overlay->kind == pt_overlay_kind_tooltip || overlay->kind == pt_overlay_kind_toast) {
            if (overlay->panel) {
                overlay->panel->window = NULL;
                overlay_push_garbage(window, overlay->panel, overlay->actions);
            }
            overlay->panel = NULL;
            overlay->actions = NULL;
            if (overlay->kind == pt_overlay_kind_tooltip) {
                free(overlay);
            }
        } else {
            /* Menus and dialogs are caller-owned. Invalidate them so they can
             * still be destroyed safely after the window is gone. */
            if (overlay->panel) {
                overlay->panel->window = NULL;
                overlay_push_garbage(window, overlay->panel, overlay->actions);
            }
            overlay->panel = NULL;
            overlay->actions = NULL;
            overlay->window = NULL;
        }
        overlay = next;
    }
    window->overlays = NULL;
    pt_window_overlays_collect(window);

    pt_toast *toast = window->first_toast;
    window->first_toast = NULL;
    while (toast) {
        pt_toast *next = toast->next;
        free(toast->text);
        free(toast);
        toast = next;
    }
}
