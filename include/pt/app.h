/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_app_h
#define pt_app_h

#include <pt/base.h>
#include <pt/theme.h>

/* Header contract
 *
 * Threading. UI thread only, including getters. Timers and the accessibility
 * event callback run on that thread.
 * Ownership. pt_app_create returns an application owned by the caller until
 * pt_app_destroy, which destroys its windows, widget trees, overlays and
 * timers. Timer ids are values, not handles; a fired id is forfeited.
 * Errors. pt_app_create returns NULL and sets pt_get_last_error when the
 * compositor connection fails. pt_app_run and pt_app_run_for return -1 only on
 * a connection failure; timeout and a normal quit return 0.
 * pt_app_add_timeout returns 0 when the timer could not be queued.
 * pt_app_set_font_family copies the string. Theme setters clamp as documented
 * in theme.h; queries return the stored value.
 * Callbacks. A timer callback is one-shot, runs on the UI thread and may not
 * block; user_data must stay valid until it fires or the timer is cancelled.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Creates an application and connects to the compositor. Returns NULL on
 * failure; see pt_get_last_error(). All API calls are main-thread only. */
pt_api pt_app *pt_app_create(const char *app_id);

/* Destroys all windows and disconnects. */
pt_api void pt_app_destroy(pt_app *app);

/* Runs until pt_app_quit() is called or the last window closes. Returns 0 on
 * success, -1 if the connection fails. */
pt_api int pt_app_run(pt_app *app);

/* Runs for at most timeout_ms, useful for tests and embedded event pumps.
 * Returns 0 on timeout or normal shutdown, -1 on connection failure. */
pt_api int pt_app_run_for(pt_app *app, uint32_t timeout_ms);

pt_api void pt_app_quit(pt_app *app);
pt_api bool pt_app_is_running(const pt_app *app);

/* One-shot timer. The returned id can be cancelled; it is also forfeited when
 * the callback fires. */
pt_api uint64_t pt_app_add_timeout(pt_app *app, uint32_t milliseconds,
                                   pt_timer_callback callback, void *user_data);
pt_api void pt_app_cancel_timeout(pt_app *app, uint64_t timer_id);

pt_api void pt_app_set_color_scheme(pt_app *app, pt_color_scheme scheme);
pt_api pt_color_scheme pt_app_get_color_scheme(const pt_app *app);
pt_api void pt_app_set_accent_color(pt_app *app, pt_color color);
pt_api void pt_app_set_theme_color(pt_app *app, pt_theme_token token, pt_color color);
pt_api pt_color pt_app_get_theme_color(const pt_app *app, pt_theme_token token);
/* Geometric tokens shared by the built-in paints. Negative values are
 * clamped to zero; radius_pill may be large because it is clamped to the
 * shorter side of each shape. */
pt_api void pt_app_set_theme_metric(pt_app *app, pt_theme_metric metric, float value);
pt_api float pt_app_get_theme_metric(const pt_app *app, pt_theme_metric metric);
pt_api void pt_app_set_font_family(pt_app *app, const char *family);
pt_api void pt_app_set_quit_on_last_window_close(pt_app *app, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
