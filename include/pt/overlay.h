/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_overlay_h
#define pt_overlay_h

#include <pt/widget.h>

/* Header contract
 *
 * Threading. UI thread only, including getters and show/dismiss.
 * Ownership. pt_menu_create and pt_dialog_create return objects owned by the
 * caller and released with the matching destroy function, independently of the
 * window that hosts them. pt_window_show_toast returns a window-owned handle
 * that becomes invalid when the toast is dismissed or the window is destroyed.
 * Menu labels, shortcuts, dialog text and toast text are copied.
 * Errors. Creators return NULL and set pt_get_last_error. Setters are no-ops for
 * NULL. Showing an already open overlay is a no-op; dismiss and close are safe
 * in any state, and is_open/is_visible report false for NULL.
 * Callbacks. Menu item, dialog action and on-close callbacks run on the UI
 * thread after the overlay has closed, so they may open another overlay, mutate
 * the tree or destroy the handle. user_data must stay valid until the menu or
 * dialog is destroyed or the callback is replaced.
 * Surfaces. Dialogs and toasts stay in the parent surface. Menus and tooltips
 * may use a native xdg_popup, whose grab follows the contract in window.h:
 * popup_done dismisses the overlay and never falls back to the window path.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Overlays are normally composited inside the parent window with a z-ordered
 * layer, and keep focus inside their own widget tree while open. Menus and
 * tooltips may instead use native xdg_popup surfaces so they can extend past
 * the window and follow compositor placement rules. */
typedef enum pt_popup_policy {
    pt_popup_policy_auto = 0,
    pt_popup_policy_native,
    pt_popup_policy_window
} pt_popup_policy;

/* Selects native popups when the backend supports them (auto), always uses
 * them (native) or never uses them (window). Dialogs and toasts always stay
 * in the parent surface. Defaults to auto. */
pt_api void pt_window_set_popup_policy(pt_window *window, pt_popup_policy policy);
pt_api pt_popup_policy pt_window_get_popup_policy(const pt_window *window);

typedef struct pt_menu pt_menu;
typedef struct pt_dialog pt_dialog;
typedef struct pt_toast pt_toast;

typedef enum pt_toast_kind {
    pt_toast_info = 0,
    pt_toast_success,
    pt_toast_warning,
    pt_toast_error
} pt_toast_kind;

typedef void (*pt_dialog_callback)(pt_dialog *dialog, void *user_data);

/* Menus copy item labels and shortcuts. The callback runs after the menu has
 * closed, so it may open another overlay. */
pt_api pt_menu *pt_menu_create(pt_window *window);
pt_api void pt_menu_destroy(pt_menu *menu);
pt_api void pt_menu_add_item(pt_menu *menu, const char *label, const char *shortcut,
                             pt_click_callback callback, void *user_data);
pt_api void pt_menu_add_separator(pt_menu *menu);
pt_api void pt_menu_show_at(pt_menu *menu, float x, float y);
pt_api void pt_menu_show_below(pt_menu *menu, pt_widget *anchor);
pt_api void pt_menu_dismiss(pt_menu *menu);
pt_api bool pt_menu_is_open(const pt_menu *menu);

/* Modal dialogs dim the window behind them, trap Tab focus and close on
 * Escape. Actions close the dialog before their callback runs. */
pt_api pt_dialog *pt_dialog_create(pt_window *window, const char *title,
                                   const char *message);
pt_api void pt_dialog_destroy(pt_dialog *dialog);
pt_api void pt_dialog_set_title(pt_dialog *dialog, const char *title);
pt_api void pt_dialog_set_message(pt_dialog *dialog, const char *message);
pt_api void pt_dialog_add_action(pt_dialog *dialog, const char *label, bool primary,
                                 pt_click_callback callback, void *user_data);
pt_api void pt_dialog_show(pt_dialog *dialog);
pt_api void pt_dialog_close(pt_dialog *dialog);
pt_api bool pt_dialog_is_open(const pt_dialog *dialog);
pt_api void pt_dialog_set_on_close(pt_dialog *dialog, pt_dialog_callback callback,
                                   void *user_data);

/* Toasts stack at the bottom-right corner and expire automatically. A duration
 * of 0 keeps the toast until dismissed. The handle is owned by the window. */
pt_api pt_toast *pt_window_show_toast(pt_window *window, const char *text,
                                      pt_toast_kind kind, uint32_t duration_ms);
pt_api void pt_toast_dismiss(pt_toast *toast);
pt_api bool pt_toast_is_visible(const pt_toast *toast);

#ifdef __cplusplus
}
#endif

#endif
