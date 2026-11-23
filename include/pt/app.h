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
 * in theme.h; queries return the stored value. pt_app_set_font_fallbacks and
 * its per-family variant copy the families and are described beside the
 * declarations.
 * Callbacks. A timer callback is one-shot, runs on the UI thread and may not
 * block; user_data must stay valid until it fires or the timer is cancelled.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Creates an application and connects to the compositor. Returns NULL on
 * failure; see pt_get_last_error(). All API calls are main-thread only. Since 1.0. */
pt_api pt_app *pt_app_create(const char *app_id);

/* Destroys all windows and disconnects. Since 1.0. */
pt_api void pt_app_destroy(pt_app *app);

/* Runs until pt_app_quit() is called or the last window closes. Returns 0 on
 * success, -1 if the connection fails. Since 1.0. */
pt_api int pt_app_run(pt_app *app);

/* Runs for at most timeout_ms, useful for tests and embedded event pumps.
 * Returns 0 on timeout or normal shutdown, -1 on connection failure. Since 1.0. */
pt_api int pt_app_run_for(pt_app *app, uint32_t timeout_ms);

/* Since 1.0. */
pt_api void pt_app_quit(pt_app *app);
/* Since 1.0. */
pt_api bool pt_app_is_running(const pt_app *app);

/* One-shot timer. The returned id can be cancelled; it is also forfeited when
 * the callback fires. Since 1.0. */
pt_api uint64_t pt_app_add_timeout(pt_app *app, uint32_t milliseconds,
                                   pt_timer_callback callback, void *user_data);
/* Since 1.0. */
pt_api void pt_app_cancel_timeout(pt_app *app, uint64_t timer_id);

/* File-descriptor watches integrate application descriptors into Pt's poll
 * loop (standards section 11). The callback runs on the UI thread when the
 * descriptor is ready and must not block. `events` is a mask of pt_fd_event
 * values; the callback receives the readiness actually observed.
 *
 * Ownership: pt_app_watch_fd borrows the descriptor and never closes it; the
 * caller closes it after pt_app_unwatch_fd. pt_app_watch_fd_owned transfers it,
 * and Pt closes it on unwatch and when the app is destroyed (including a
 * self-unwatch from inside the callback, whose free is deferred until dispatch
 * returns). Watching the same fd twice is rejected with id 0; callbacks may
 * unwatch themselves. POLLERR, POLLHUP and POLLNVAL all report
 * pt_fd_event_error, so the callback runs at least once for a dead descriptor
 * (POLLHUP additionally sets pt_fd_event_hangup). Watches participate in the
 * poll timeout: pt_app_run_for wakes to dispatch a ready descriptor but still
 * returns at its requested deadline. */
typedef enum pt_fd_event {
    pt_fd_event_read = 1u << 0,
    pt_fd_event_write = 1u << 1,
    pt_fd_event_error = 1u << 2,
    pt_fd_event_hangup = 1u << 3
} pt_fd_event;

typedef void (*pt_fd_callback)(pt_app *app, uint32_t events, void *user_data);

/* Since 1.1. */
pt_api uint64_t pt_app_watch_fd(pt_app *app, int fd, uint32_t events,
                                pt_fd_callback callback, void *user_data);
/* Takes ownership of `fd`; Pt closes it on unwatch or app destroy. Same
 * duplicate-fd and error rules as pt_app_watch_fd. Since 1.1. */
pt_api uint64_t pt_app_watch_fd_owned(pt_app *app, int fd, uint32_t events,
                                      pt_fd_callback callback, void *user_data);
/* Since 1.1. */
pt_api void pt_app_unwatch_fd(pt_app *app, uint64_t watch_id);

/* Since 1.0. */
pt_api void pt_app_set_color_scheme(pt_app *app, pt_color_scheme scheme);
/* Since 1.0. */
pt_api pt_color_scheme pt_app_get_color_scheme(const pt_app *app);
/* Since 1.0. */
pt_api void pt_app_set_accent_color(pt_app *app, pt_color color);
/* Since 1.0. */
pt_api void pt_app_set_theme_color(pt_app *app, pt_theme_token token, pt_color color);
/* Since 1.0. */
pt_api pt_color pt_app_get_theme_color(const pt_app *app, pt_theme_token token);
/* Geometric tokens shared by the built-in paints. Negative values are
 * clamped to zero; radius_pill may be large because it is clamped to the
 * shorter side of each shape. Since 1.0. */
pt_api void pt_app_set_theme_metric(pt_app *app, pt_theme_metric metric, float value);
/* Since 1.0. */
pt_api float pt_app_get_theme_metric(const pt_app *app, pt_theme_metric metric);
/* Since 1.0. */
pt_api void pt_app_set_font_family(pt_app *app, const char *family);

/* Per-run font fallback (since 1.2)
 *
 * Pt splits a UTF-8 run into HarfBuzz-script segments. A segment is shaped with
 * the primary family when that face has a glyph for every character, otherwise
 * with the first family in the fallback chain that covers it, and finally with
 * Fontconfig's own sort order when no explicit chain is installed. Codepoints
 * that no installed face covers keep the existing tofu (.notdef) behaviour and
 * can be counted with pt_app_font_uncovered_count().
 *
 * The font cache is shared by every application in the process, so the chain is
 * process-wide: installing one repaints the calling app's windows and applies to
 * text laid out afterwards. UI thread only. Families are copied; an empty entry is
 * skipped, count is clamped to PT_FONT_FALLBACK_MAX_FAMILIES, and a NULL or
 * empty list restores the built-in default (Fontconfig's sort order). An
 * explicit chain is authoritative: a character that none of its families covers
 * renders as tofu even when another installed font would have it.
 *
 * A per-family override replaces the app default for that exact primary family
 * string ("sans-serif", "monospace", or the app's configured font family). An
 * empty or NULL list removes the override and falls back to the app default. At
 * most PT_FONT_FALLBACK_MAX_OVERRIDES families may carry an override; setting a
 * new one evicts the least recently set.
 *
 * Metrics. A text block's ascent is the maximum ascent and its descent the
 * maximum descent over the faces used by its runs; line height is
 * max(ascent + descent, primary line height). pt_font/measure and the widget
 * layout use that composition, so a mixed line reserves room for the tallest
 * fallback face. Caret, selection and hit-test offsets stay in logical UTF-8
 * byte order and the accessibility text is the unchanged widget string.
 *
 * Errors. Setters are no-ops for a NULL app. Getters return 0/NULL for a NULL
 * app or out-of-range index. The per-family getters report the override only;
 * zero means no override is installed and the app default applies. Returned
 * family pointers borrow process-wide chain storage rather than memory owned by
 * `app`: pt_app_destroy does not invalidate them, but any fallback installation
 * (including one made through another application in the process), any
 * per-family override eviction, and pt_font_manager_shutdown() do. Copy a
 * returned family immediately if it must outlive the call.
 * pt_app_font_uncovered_count returns 0 on a NULL app or empty text and counts
 * codepoints, not bytes. A NULL or empty family selects the app's configured
 * font family at the default text style (14 px, regular, upright), so NULL means
 * the application default rather than a fixed family.
 *
 * Every function in this block is since 1.2.
 */
#define PT_FONT_FALLBACK_MAX_FAMILIES 8
#define PT_FONT_FALLBACK_MAX_OVERRIDES 16

/* Since 1.2. */
pt_api void pt_app_set_font_fallbacks(pt_app *app, const char *const *families,
                                      int count);
/* Since 1.2. */
pt_api int pt_app_get_font_fallback_count(const pt_app *app);
/* Since 1.2. */
pt_api const char *pt_app_get_font_fallback(const pt_app *app, int index);
/* Since 1.2. */
pt_api void pt_app_set_font_fallbacks_for_family(pt_app *app, const char *family,
                                                 const char *const *families,
                                                 int count);
/* Since 1.2. */
pt_api int pt_app_get_font_fallback_count_for_family(const pt_app *app,
                                                      const char *family);
/* Since 1.2. */
pt_api const char *pt_app_get_font_fallback_for_family(const pt_app *app,
                                                        const char *family,
                                                        int index);
/* Since 1.2. */
pt_api int pt_app_font_uncovered_count(const pt_app *app, const char *family,
                                       const char *text, size_t length);

/* Application-wide interface direction (since 2.0). The value is the last
 * fallback in the resolution chain widget -> nearest explicit ancestor ->
 * window -> application -> LTR. The setter stores the explicit value, ignores a
 * NULL app and rejects values outside pt_ui_direction; a change invalidates the
 * layout and requests a repaint of every window. pt_ui_direction_inherit is the
 * zero value and is equivalent to LTR for an application.
 *
 * The getter reports the stored explicit value, not the resolved one, so a
 * fresh application returns pt_ui_direction_inherit. Direction is presentation
 * only: it never changes stored UTF-8 bytes, selections or accessibility
 * character offsets. Auto text direction uses the resolved value only as the
 * last-strong fallback for a neutral-only paragraph. Since 2.0. */
pt_api void pt_app_set_ui_direction(pt_app *app, pt_ui_direction direction);
/* Since 2.0. */
pt_api pt_ui_direction pt_app_get_ui_direction(const pt_app *app);

/* Application-wide caret movement policy (since 2.1). It is the last fallback
 * in the resolution chain widget -> nearest explicit ancestor -> window ->
 * application -> visual. The setter stores the explicit value, ignores a NULL
 * app and rejects values outside pt_caret_movement. pt_caret_movement_inherit
 * is the zero value, so a fresh application resolves to visual. A change does
 * not invalidate layout or repaint: it affects the next key event only. The
 * getter reports the stored explicit value, not the resolved one. Since 2.1. */
pt_api void pt_app_set_caret_movement(pt_app *app, pt_caret_movement movement);
/* Since 2.1. */
pt_api pt_caret_movement pt_app_get_caret_movement(const pt_app *app);

/* Stores a locale hint (since 2.1). The string is copied; NULL or an empty
 * string clears the hint. When the application's explicit UI direction is
 * inherit, the hint supplies the application fallback in the resolution chain;
 * an explicit pt_app_set_ui_direction always wins, regardless of call order. A
 * change invalidates layout and repaints only when it changes the resolved
 * application fallback. Pt never reads the process environment: the caller
 * passes the locale it already resolved. Since 2.1. */
pt_api void pt_app_set_locale(pt_app *app, const char *locale);
/* Since 2.1. Returns the stored locale hint, or NULL when none is set. The
 * pointer is owned by the app and stays valid until the next set_locale or
 * pt_app_destroy. */
pt_api const char *pt_app_get_locale(const pt_app *app);

/* Since 1.0. */
pt_api void pt_app_set_quit_on_last_window_close(pt_app *app, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
