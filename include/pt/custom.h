/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_custom_h
#define pt_custom_h

#include <pt/widget.h>

/* Header contract
 *
 * Threading. UI thread only, including paint callbacks and getters.
 * Ownership. The paint context is borrowed for the duration of the callback and
 * becomes invalid when it returns. Strings passed to drawing helpers are read
 * only for the duration of the call; they are not retained.
 * Errors. Drawing helpers are no-ops for NULL contexts, empty rectangles and
 * transparent colors. Font lookup failure is silent and draws nothing.
 * Callbacks. A paint callback runs during the window paint pass. It must not
 * call setters that change layout or destroy widgets, because the paint pass is
 * traversing the tree; use pt_app_add_timeout(..., 0, ...) to defer such work.
 * All coordinates are logical pixels relative to the widget's top-left corner,
 * and text sizes are logical pixels. Pt applies the window scale internally.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque paint context passed to a paint callback. */
typedef struct pt_paint_context pt_paint_context;

/* Called while a widget is painted, after its normal background/content pass.
 * The context is clipped to the widget rectangle. Drawing is additive: the
 * callback can cover the default appearance.
 *
 * Contract (standards section 7):
 * - Pure and idempotent. The same widget is painted 0..N times per frame (once
 *   per damage tile) and must produce identical pixels regardless of tile order
 *   or count. Draw from widget state only; do not advance counters or mutate
 *   data that changes the output across calls in one frame.
 * - Do not change the widget tree and do not trigger layout. Use
 *   pt_widget_request_redraw/pt_widget_request_redraw_rect, or change another
 *   widget and let the frame finish; the change is queued for the next frame.
 * - `context` and its canvas are valid only during the call. Local coordinates
 *   are logical widget pixels; pt_paint_scale() is the device-pixel ratio.
 * - Every pt_paint_push_clip must have a matching pt_paint_pop_clip. A debug
 *   build reports an unbalanced stack and the frame restores its own clip.
 * - Do not call pt_window_paint_pixels/pt_window_paint_pixels_ex or
 *   pt_window_paint from inside the callback; the re-entrant call returns false
 *   and records an error.
 * - The callback may destroy its widget (or another widget). The free is
 *   deferred until the paint pass returns; pointers passed to the callback are
 *   invalid after it returns.
 *
 * A custom-painted control still needs its accessibility role, name and state
 * declared explicitly; paint does not replace semantics. */
typedef void (*pt_paint_callback)(pt_widget *widget, pt_paint_context *context,
                                  void *user_data);

/* Installs or removes a paint callback on any widget. Passing NULL clears it.
 * The callback data must stay valid until it is replaced or the widget is
 * destroyed. Calling this requests a redraw. The callback rules above apply. */
pt_api void pt_widget_set_paint_callback(pt_widget *widget, pt_paint_callback callback,
                                         void *user_data);
pt_api void pt_widget_clear_paint_callback(pt_widget *widget);

/* Paints one full frame of `window` into `pixels` at a scale of 1.0 with a
 * stride of `width` pixels. Pixels are premultiplied 0xAARRGGBB, row-major.
 * This is a pure snapshot: it does not damage the window, clear
 * needs_draw/redraw_scheduled, touch last_paint_time or collect deferred
 * overlays, so calling it on a mapped window never swallows the next live
 * frame. It must not be called from a paint callback (the call is rejected and
 * records an error). Returns false for a NULL window, NULL buffer or
 * non-positive size. */
pt_api bool pt_window_paint_pixels(pt_window *window, uint32_t *pixels, int width,
                                   int height);

/* Pixel layout of an offscreen snapshot. `stride` is in pixels per row and 0
 * means `width`; `scale` is the device-pixel ratio and 0 means 1.0. Only
 * premultiplied 0xAARRGGBB is supported. Unused fields are reserved for a later
 * minor and must be zero-initialized. Since 1.1. */
typedef enum pt_pixel_format {
    pt_pixel_format_argb8888_premultiplied = 0
} pt_pixel_format;

typedef struct pt_paint_pixels_desc {
    int stride;
    float scale;
    pt_pixel_format format;
    uint32_t reserved[3];
} pt_paint_pixels_desc;

/* Extended offscreen snapshot. `width` and `height` are device pixels (a scale
 * of 2.0 needs width = 2 * logical_width). The stride may exceed width, in
 * which case the padding pixels are left untouched. Returns false for an
 * invalid stride, scale, format or buffer. Since 1.1. */
pt_api bool pt_window_paint_pixels_ex(pt_window *window, uint32_t *pixels, int width,
                                      int height, const pt_paint_pixels_desc *desc);

/* Requests a repaint of this widget and its subtree. Unlike a window-level
 * redraw this damages the widget rectangle only. */
pt_api void pt_widget_request_redraw(pt_widget *widget);

/* Requests a repaint of a sub-rectangle. Coordinates are widget-local logical
 * pixels and are clamped to the widget. Use this for small updates such as a
 * caret or cursor so the paint callback only needs to touch the visible
 * damage. */
pt_api void pt_widget_request_redraw_rect(pt_widget *widget, float x, float y,
                                          float width, float height);

/* Geometry of the paint target, in logical pixels. */
pt_api float pt_paint_width(const pt_paint_context *context);
pt_api float pt_paint_height(const pt_paint_context *context);
pt_api float pt_paint_scale(const pt_paint_context *context);

/* Returns the current damage clip in widget-local logical pixels. The paint
 * callback may be invoked once per damaged tile; callers can use this to skip
 * work outside the clip. Returns false when the clip is empty. */
pt_api bool pt_paint_clip_rect(const pt_paint_context *context, float *x, float *y,
                               float *width, float *height);

/* Clip helpers: coordinates are widget-local and intersect with the widget
 * rectangle and every enclosing clip. Each push must have a matching pop. */
pt_api void pt_paint_push_clip(pt_paint_context *context, float x, float y, float width,
                               float height);
pt_api void pt_paint_pop_clip(pt_paint_context *context);

/* Primitive drawing. */
pt_api void pt_paint_fill_rect(pt_paint_context *context, float x, float y, float width,
                               float height, pt_color color);
pt_api void pt_paint_fill_rounded_rect(pt_paint_context *context, float x, float y,
                                       float width, float height, float radius,
                                       pt_color color);
pt_api void pt_paint_stroke_rect(pt_paint_context *context, float x, float y, float width,
                                 float height, float line_width, pt_color color);
pt_api void pt_paint_draw_line(pt_paint_context *context, float x0, float y0, float x1,
                               float y1, float line_width, pt_color color);

/* Draw a single line of UTF-8 text. `baseline` is the text baseline. The run is
 * not truncated; clip it with pt_paint_push_clip when needed. `size` is logical
 * pixels. Weight is a pt_font_weight value and slant a pt_font_slant value. */
pt_api void pt_paint_draw_text(pt_paint_context *context, const char *family, float size,
                               int weight, int slant, float x, float baseline,
                               const char *text, size_t length, pt_color color);

/* Returns true when the resolved font has a glyph for a Unicode code point.
 * Useful for selecting a fallback family per character. */
pt_api bool pt_paint_font_has_glyph(pt_paint_context *context, const char *family,
                                   float size, uint32_t codepoint);

/* Measures a UTF-8 run in logical pixels. Returns 0 for empty text. */
pt_api float pt_paint_measure_text(pt_paint_context *context, const char *family,
                                   float size, int weight, int slant, const char *text,
                                   size_t length);

/* Returns logical font metrics. cell_width is the advance of "M", useful for
 * monospace grids. Any out pointer may be NULL. Returns false when the font
 * cannot be resolved. */
pt_api bool pt_paint_font_metrics(pt_paint_context *context, const char *family,
                                  float size, int weight, int slant, float *ascent,
                                  float *descent, float *line_height, float *cell_width);

/* Text input for custom widgets and terminal views.
 *
 * Pt normally enables zwp_text_input_v3 when a text-entry widget has focus. A
 * custom surface can opt in instead by installing callbacks and enabling IME.
 * This lets a terminal receive composed text without pretending to be a text
 * entry. Callbacks run on the UI thread; text is borrowed for the call and is
 * invalid after it returns. A callback may request a redraw or change its own
 * composition buffer, and may disable IME; it must not call
 * pt_app_run_for or destroy the window synchronously.
 *
 * Event surface (standards section 7, Q-IME-001/002):
 * - `commit` delivers text to insert. `preedit` replaces the current
 *   composition; an empty string ends it. Cursor offsets are UTF-8 byte
 *   offsets into `preedit`, or -1 when unspecified. Pt does not apply either
 *   itself: the callback owns insertion and redraw.
 * - Every done event resends the complete state (content type, surrounding
 *   text, available actions and caret rectangle); never rely on an IME having
 *   seen an earlier delta.
 * - A done with no commit, no preedit and no deletion is an acknowledgement of
 *   the state Pt pushed. It does not insert text and does not require another
 *   state push; an idle custom surface must not answer it with a redraw loop.
 * - The local composition buffer belongs to the callback and is cleared only
 *   on a commit (or an explicit empty preedit), never on each preedit update.
 * - text-input-v3 v2 `action` (submit) activates the focused entry through its
 *   submit callback. It is not turned into a synthetic Enter key and inserts
 *   no newline. `preedit_hint` ranges are accepted but not rendered; they never
 *   change the composition buffer or synthesize input.
 * - `delete_surrounding_text` lengths are UTF-8 byte counts measured from the
 *   caret in each direction. Pt snaps them to codepoint boundaries so a
 *   multibyte sequence is never split.
 *
 * `pt_window_set_ime_cursor_rect` sets the logical-pixel caret rectangle used
 * by candidate windows. It is remembered while IME is disabled and pushed when
 * IME becomes active. Coordinates are relative to the window content area.
 * Calling any of these for a headless window is harmless. */
typedef void (*pt_ime_commit_callback)(pt_window *window, const char *text, void *user_data);
typedef void (*pt_ime_preedit_callback)(pt_window *window, const char *text,
                                        int32_t cursor_begin, int32_t cursor_end,
                                        void *user_data);
pt_api void pt_window_set_ime_callbacks(pt_window *window,
                                        pt_ime_commit_callback commit_callback,
                                        pt_ime_preedit_callback preedit_callback,
                                        void *user_data);
pt_api void pt_window_set_ime_enabled(pt_window *window, bool enabled);
pt_api void pt_window_set_ime_cursor_rect(pt_window *window, int x, int y, int width,
                                          int height);

#ifdef __cplusplus
}
#endif

#endif
