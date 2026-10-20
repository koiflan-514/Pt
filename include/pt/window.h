/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_window_h
#define pt_window_h

#include <pt/app.h>
#include <pt/base.h>

/* Header contract
 *
 * Threading. UI thread only, including getters.
 * Ownership. The application owns every window; pt_window_create returns a
 * borrowed handle that pt_app_destroy releases. A window owns its root widget
 * tree and its toast handles. pt_window_clipboard_get returns memory owned by
 * the caller and released with pt_free; every other returned pointer or string
 * is borrowed.
 * Errors. pt_window_create returns NULL and sets pt_get_last_error. Setters are
 * no-ops for NULL windows or invalid theme tokens. pt_window_close is a request,
 * not a destruction: the close callback may veto, and destruction is deferred
 * until the current dispatch finishes. Theme getters return the app value when
 * the window has no override.
 * Callbacks. Close, resize, key and drag-finished callbacks run on the UI
 * thread; user_data must stay valid until the callback is replaced or the
 * window is destroyed. Pointer and text arguments passed to a callback are
 * valid only during that call.
 * Popups. A grabbed xdg_popup belongs to the compositor. popup_done dismisses
 * the overlay instead of falling back to the in-window path. The grab serial is
 * the window's last pointer serial, or the seat's most recent serial when no
 * pointer event has landed on the window. Menus, tooltips and select-only
 * combobox popups inherit this contract (see overlay.h).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a window and its root widget. The window is hidden until
 * pt_window_show(). Width and height are logical pixels. */
pt_api pt_window *pt_window_create(pt_app *app, const char *title, int width, int height);
pt_api void pt_window_show(pt_window *window);
pt_api void pt_window_hide(pt_window *window);

/* Requests close. The close callback may veto. Actual destruction is deferred
 * until the current event dispatch finishes. */
pt_api void pt_window_close(pt_window *window);
pt_api bool pt_window_is_open(const pt_window *window);

pt_api pt_widget *pt_window_root(pt_window *window);
pt_api pt_app *pt_window_app(pt_window *window);
pt_api int pt_window_get_width(const pt_window *window);
pt_api int pt_window_get_height(const pt_window *window);
pt_api float pt_window_get_scale(const pt_window *window);

pt_api void pt_window_set_title(pt_window *window, const char *title);
pt_api void pt_window_set_size(pt_window *window, int width, int height);
pt_api void pt_window_set_min_size(pt_window *window, int width, int height);
pt_api void pt_window_set_resizable(pt_window *window, bool resizable);

/* A decorated window gets a Pt-drawn title bar when the compositor permits
 * client-side decorations. Defaults to true. */
pt_api void pt_window_set_decorated(pt_window *window, bool decorated);
pt_api void pt_window_request_redraw(pt_window *window);
pt_api void pt_window_focus(pt_window *window, pt_widget *widget);

/* Per-window theme overrides. A window inherits the application theme until a
 * role or metric is overridden here, so a settings preview or a second
 * surface can restyle itself without copying the whole palette. Passing an
 * invalid token or metric is ignored. pt_window_clear_theme_overrides()
 * returns every role and metric to the application value. */
pt_api void pt_window_set_theme_color(pt_window *window, pt_theme_token token,
                                      pt_color color);
pt_api pt_color pt_window_get_theme_color(const pt_window *window,
                                          pt_theme_token token);
pt_api void pt_window_set_theme_metric(pt_window *window, pt_theme_metric metric,
                                       float value);
pt_api float pt_window_get_theme_metric(const pt_window *window,
                                        pt_theme_metric metric);
pt_api void pt_window_clear_theme_overrides(pt_window *window);

pt_api void pt_window_set_close_callback(pt_window *window, pt_close_callback callback,
                                         void *user_data);
pt_api void pt_window_set_resize_callback(pt_window *window, pt_resize_callback callback,
                                          void *user_data);
pt_api void pt_window_set_key_callback(pt_window *window, pt_key_callback callback,
                                       void *user_data);

/* Text clipboard backed by the seat's wl_data_device. The getter returns
 * malloc'd UTF-8 memory owned by the caller; release it with pt_free().
 * Returns NULL when the clipboard is empty or unavailable. */
pt_api bool pt_window_clipboard_set(pt_window *window, const char *text);
pt_api char *pt_window_clipboard_get(pt_window *window);

#ifdef __cplusplus
}
#endif

#endif
