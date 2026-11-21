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
 * callback can cover the default appearance. */
typedef void (*pt_paint_callback)(pt_widget *widget, pt_paint_context *context,
                                  void *user_data);

/* Installs or removes a paint callback on any widget. Passing NULL clears it.
 * The callback data must stay valid until it is replaced or the widget is
 * destroyed. Calling this requests a redraw. */
pt_api void pt_widget_set_paint_callback(pt_widget *widget, pt_paint_callback callback,
                                         void *user_data);
pt_api void pt_widget_clear_paint_callback(pt_widget *widget);

/* Paints one full frame of `window` into `pixels` (one uint32_t per pixel in
 * 0xAARRGGBB byte order, `width` pixels per row) at a scale of 1.0. This is
 * the offscreen entry point for headless and unit testing of custom paint
 * callbacks: no compositor or window buffer is involved. Returns false when
 * the window is NULL or the size is invalid. */
pt_api bool pt_window_paint_pixels(pt_window *window, uint32_t *pixels, int width,
                                   int height);

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
 * entry. Callbacks run on the UI thread; text is borrowed for the call.
 *
 * `commit` delivers text that should be inserted. `preedit` replaces the
 * current composition; an empty string ends it. Cursor offsets are UTF-8 byte
 * offsets into `preedit`, or -1 when unspecified. Pt does not apply a commit or
 * preedit itself, so the callback owns insertion and redraw.
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
