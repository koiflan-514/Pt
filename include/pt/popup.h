/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_popup_h
#define pt_popup_h

#include <pt/widget.h>

/* Header contract
 *
 * Threading. UI thread only, including getters, show, reposition and dismiss.
 * Ownership. pt_popup_create takes a parent window and a content widget; the
 * popup adopts the content widget and its subtree. On pt_popup_destroy the
 * popup destroys the content tree, so the application must not use or free the
 * content afterwards. pt_popup_content returns a borrowed pointer that stays
 * valid until the popup is destroyed. A popup is destroyed automatically when
 * its parent window is destroyed; the handle is invalid from that moment and
 * must not be used or destroyed again. Destroying a popup does not invoke the
 * dismiss callback. The dismiss callback never owns the popup: it may destroy
 * or re-show it.
 * Errors. pt_popup_create returns NULL and sets pt_get_last_error for a NULL
 * window, a NULL/window-root content widget, content that belongs to another
 * window, or invalid parent state. pt_popup_show and pt_popup_reposition
 * return false and set the error for a NULL popup, an invalid (non-finite or
 * negative-size) anchor, a destroyed parent window, or the per-window open
 * limit. pt_popup_dismiss, pt_popup_destroy and setters are no-ops for NULL.
 * Callbacks. pt_popup_dismiss_callback runs on the UI thread after the popup
 * has closed: pt_popup_is_open is already false when it runs. It may destroy
 * the popup, re-show it with a new anchor, open another popup, or mutate the
 * widget tree. The callback runs exactly once for each show that ends through
 * dismissal (outside click, Escape, grab break or pt_popup_dismiss); it does
 * not run on pt_popup_destroy or parent-window destruction. user_data must
 * stay valid until the callback is replaced or the popup is destroyed.
 * Placement. The anchor is a rectangle in parent-window logical pixels.
 * Native popups use xdg_positioner flip/slide constraints and the compositor's
 * work area. When the window is not shown/configured or the platform has no
 * xdg-shell popup support, the popup follows the in-window overlay fallback and
 * is flipped and slid inside the window content area instead. Menu, tooltip and
 * combobox native-popup policy (pt_window_set_popup_policy) also applies here.
 * Accessibility. The popup exposes a panel that contains the content widget,
 * carries the content's accessible name (or "Popup"), and announces the
 * showing state while open.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pt_popup pt_popup;

/* An anchor rectangle in parent-window logical pixels. Since 1.2. */
typedef struct pt_popup_anchor {
    float x;
    float y;
    float width;
    float height;
} pt_popup_anchor;

/* Where the popup is placed relative to its anchor rectangle. The default is
 * below. Each placement flips to the opposite side when the preferred side
 * does not fit, and slides along the other axis to stay visible. Since 1.2. */
typedef enum pt_popup_placement {
    pt_popup_placement_below = 0,
    pt_popup_placement_above,
    pt_popup_placement_right,
    pt_popup_placement_left
} pt_popup_placement;

/* Maximum number of popups that may be open on one window at the same time.
 * Opening beyond the limit fails without changing any open popup. Since 1.2. */
#define PT_POPUP_MAX_OPEN 16

/* Dismiss callback. It runs after the popup has closed; the payload is the
 * popup handle and user_data. The handle is still valid for pt_popup_is_open,
 * pt_popup_show and pt_popup_destroy, but not after the callback destroys it. */
typedef void (*pt_popup_dismiss_callback)(pt_popup *popup, void *user_data);

/* Creates a popup for window and adopts content. content must be a widget of
 * window or a detached widget without a window, and must not be the window
 * root. content is reparented into the popup's own panel tree. The popup starts
 * closed. Since 1.2. */
pt_api pt_popup *pt_popup_create(pt_window *window, pt_widget *content);

/* Destroys the popup and its content tree. Safe while open; does not invoke the
 * dismiss callback. A popup already destroyed with its parent window must not
 * be passed here. Since 1.2. */
pt_api void pt_popup_destroy(pt_popup *popup);

/* Opens the popup with anchor in parent-window logical coordinates. A zero
 * width or height is allowed and treated as a one-pixel anchor. Showing an open
 * popup repositions it and keeps the dismiss-callback registration. Returns
 * false on a failed anchor or when PT_POPUP_MAX_OPEN popups are already open.
 * Since 1.2. */
pt_api bool pt_popup_show(pt_popup *popup, pt_popup_anchor anchor);

/* Updates the anchor of an open popup. Returns false when the popup is closed
 * or the anchor is invalid; a closed popup keeps its placement for the next
 * pt_popup_show. Since 1.2. */
pt_api bool pt_popup_reposition(pt_popup *popup, pt_popup_anchor anchor);

/* Closes the popup and invokes the dismiss callback after the open state has
 * been cleared. Safe in any state and from the popup's own dismiss callback.
 * Since 1.2. */
pt_api void pt_popup_dismiss(pt_popup *popup);

/* True while the popup is open. False for NULL and after dismissal. Since 1.2. */
pt_api bool pt_popup_is_open(const pt_popup *popup);

/* Borrowed content widget; NULL for NULL. The popup owns the tree, so the
 * pointer is invalid once the popup is destroyed. Since 1.2. */
pt_api pt_widget *pt_popup_content(const pt_popup *popup);

/* Sets the dismiss callback and its user_data. Replacing it does not affect an
 * already-open popup. Since 1.2. */
pt_api void pt_popup_set_on_dismiss(pt_popup *popup, pt_popup_dismiss_callback callback,
                                    void *user_data);

/* Placement for the next show/reposition. Invalid values select below. A change
 * while open takes effect at the next layout or reposition. Since 1.2. */
pt_api void pt_popup_set_placement(pt_popup *popup, pt_popup_placement placement);
pt_api pt_popup_placement pt_popup_get_placement(const pt_popup *popup);

#ifdef __cplusplus
}
#endif

#endif