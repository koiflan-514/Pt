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
 * Decoration. set_mode is a preference; the compositor's configure is
 * authoritative. Requested and effective modes are tracked separately.
 * pt_window_set_decorated controls only whether Pt paints its header.
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

/* Decoration negotiation. set_mode is a preference and the compositor's
 * configure is authoritative: a client request may be answered with the other
 * mode (Sway tiles force server-side, Q-DEC-001). The default is an explicit
 * client-side request; Pt draws the header itself when it is also decorated.
 * pt_decoration_auto means "prefer client-side, accept a forced server-side",
 * and pt_decoration_server asks the compositor to decorate. Since 1.1. */
typedef enum pt_decoration_mode {
    pt_decoration_auto = 0,
    pt_decoration_client = 1,
    pt_decoration_server = 2
} pt_decoration_mode;

/* A decorated window lets Pt draw its own header (default true). This never
 * requests server-side decoration: false means Pt draws nothing and no
 * server-side mode is asked for. Requesting compositor decoration is a
 * separate pt_window_set_decoration_mode(pt_decoration_server) choice. */
pt_api void pt_window_set_decorated(pt_window *window, bool decorated);

/* Requests a decoration preference. Sends a preference only when the request
 * changes; the effective mode always follows the compositor's configure. The
 * getters return the requested and effective modes. Since 1.1. */
pt_api void pt_window_set_decoration_mode(pt_window *window, pt_decoration_mode mode);
pt_api pt_decoration_mode pt_window_get_decoration_mode(const pt_window *window);
pt_api pt_decoration_mode pt_window_get_effective_decoration_mode(const pt_window *window);

/* Requests maximize. Returns false when the window is not resizable, the
 * compositor did not advertise the maximize capability, or the window is NULL.
 * The header's maximize button is disabled in the same case. Since 1.1. */
pt_api bool pt_window_maximize(pt_window *window);
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

/* Called when the compositor gives or takes keyboard focus. The callback runs
 * on the UI thread and may request a redraw. */
typedef void (*pt_focus_callback)(pt_window *window, bool focused, void *user_data);
pt_api void pt_window_set_focus_callback(pt_window *window, pt_focus_callback callback,
                                         void *user_data);

/* Extended ("raw") key callback. Dispatch order is overlay key handling, this
 * callback, the legacy key callback, then the focused widget. Returning true
 * consumes the key for the remaining widget dispatch only: the window's
 * modifier and latest-key-serial accounting still completes, so popup grabs and
 * primary-selection serials cannot go stale. A consumed key never reaches
 * text-input as an inserted commit. `utf8` is the xkb UTF-8 text for the key
 * (may be NULL or empty for non-text keys) and is valid only during the call. */
typedef bool (*pt_key_event_callback)(pt_window *window, uint32_t key, uint32_t modifiers,
                                      const char *utf8, void *user_data);
pt_api void pt_window_set_key_event_callback(pt_window *window,
                                             pt_key_event_callback callback,
                                             void *user_data);

/* Pointer phases for the window-level pointer callback. */
typedef enum pt_pointer_phase {
    pt_pointer_phase_enter = 0,
    pt_pointer_phase_leave,
    pt_pointer_phase_motion,
    pt_pointer_phase_down,
    pt_pointer_phase_up,
    pt_pointer_phase_axis,
    pt_pointer_phase_cancel
} pt_pointer_phase;

/* A normalized pointer event in logical window coordinates. For axis events,
 * delta_y is positive downward and is expressed in the compositor's axis units
 * (typically 15 per wheel notch). `button` is a Linux BTN_* code for down/up and
 * 0 otherwise. For down and up, `pressed` reports the state of that button and
 * `click_count` its own multi-click count; for the other phases `pressed`
 * reports whether any pointer button is held. Non-left buttons never alter the
 * left button's press state or click count. */
typedef struct pt_pointer_event {
    pt_pointer_phase phase;
    float x;
    float y;
    float delta_x;
    float delta_y;
    uint32_t button;
    uint32_t modifiers;
    uint32_t click_count;
    bool pressed;
} pt_pointer_event;

/* Runs before overlay/widget pointer dispatch. Returning true consumes down,
 * up and axis events for the remaining widget dispatch of that event only;
 * button state, click counts, focus and serials are updated before the callback
 * and are never rolled back, and the next event is dispatched normally. Enter,
 * leave and motion callbacks are advisory: the return value is ignored for them
 * so hover tracking keeps working. The pointer event is borrowed for the call
 * and becomes invalid when the callback returns. */
typedef bool (*pt_pointer_callback)(pt_window *window, const pt_pointer_event *event,
                                    void *user_data);
pt_api void pt_window_set_pointer_callback(pt_window *window, pt_pointer_callback callback,
                                           void *user_data);

/* Text clipboard backed by the seat's wl_data_device. The getter returns
 * malloc'd UTF-8 memory owned by the caller; release it with pt_free().
 * Returns NULL when the clipboard is empty or unavailable. */
pt_api bool pt_window_clipboard_set(pt_window *window, const char *text);
pt_api char *pt_window_clipboard_get(pt_window *window);

/* Primary selection (middle-click paste), backed by
 * wp_primary_selection_unstable_v1 when the compositor offers it. Without the
 * protocol, or headless, both sides fall back to an in-process buffer so the
 * API stays usable everywhere. Ownership and error contract match the
 * clipboard pair: the setter copies, the getter returns malloc'd UTF-8 owned
 * by the caller (release with pt_free()), NULL when empty or unavailable.
 *
 * Serial sourcing (standards section 9, Q-PRI-001). pt_window_primary_set
 * publishes only when a pointer release or key press has supplied a legal
 * trigger serial; after any run of input events it uses the most recent one.
 * A programmatic selection with no triggering event returns false and sends
 * nothing. Inside a callback that knows the trigger serial, call
 * pt_window_primary_set_serial instead. Call the setter once when a drag
 * selection completes, not on every motion event. Since 1.1. */
pt_api bool pt_window_primary_set(pt_window *window, const char *text);
pt_api bool pt_window_primary_set_serial(pt_window *window, const char *text,
                                         uint32_t serial);
pt_api char *pt_window_primary_get(pt_window *window);

#ifdef __cplusplus
}
#endif

#endif
