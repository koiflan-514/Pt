/*
 * Pt custom paint widget support.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This file implements the small public surface in <pt/custom.h>. It is a thin
 * adapter over the internal canvas and font cache: coordinates stay logical,
 * fonts are acquired at device size, and every operation is clipped to the
 * widget rectangle by the paint context.
 */

#include "internal.h"

#include <math.h>
#include <string.h>

void pt_widget_set_paint_callback(pt_widget *widget, pt_paint_callback callback,
                                  void *user_data) {
    if (!widget) {
        return;
    }
    widget->paint_callback = callback;
    widget->paint_callback_data = user_data;
    pt_widget_request_redraw(widget);
}

void pt_widget_clear_paint_callback(pt_widget *widget) {
    pt_widget_set_paint_callback(widget, NULL, NULL);
}

void pt_widget_request_redraw(pt_widget *widget) {
    if (!widget) {
        return;
    }
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_request_redraw_rect(pt_widget *widget, float x, float y, float width,
                                   float height) {
    if (!widget) {
        return;
    }
    pt_window *window = pt_widget_window(widget);
    if (!window) {
        return;
    }
    if (window->needs_layout) {
        pt_widget_mark_dirty(widget, false);
        return;
    }
    float x0 = x < 0.0f ? 0.0f : x;
    float y0 = y < 0.0f ? 0.0f : y;
    float x1 = x + width;
    float y1 = y + height;
    if (x1 > widget->width) {
        x1 = widget->width;
    }
    if (y1 > widget->height) {
        y1 = widget->height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    pt_window_damage_rect(window, (pt_rect){widget->x + x0, widget->y + y0, x1 - x0,
                                            y1 - y0});
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

float pt_paint_width(const pt_paint_context *context) {
    return context && context->widget ? context->widget->width : 0.0f;
}

float pt_paint_height(const pt_paint_context *context) {
    return context && context->widget ? context->widget->height : 0.0f;
}

float pt_paint_scale(const pt_paint_context *context) {
    return context && context->canvas ? context->canvas->scale : 1.0f;
}

bool pt_paint_clip_rect(const pt_paint_context *context, float *x, float *y, float *width,
                        float *height) {
    if (!context || !context->canvas || !context->widget) {
        return false;
    }
    float scale = context->canvas->scale > 0.0f ? context->canvas->scale : 1.0f;
    pt_rect clip = context->canvas->clip;
    float x0 = clip.x / scale - context->widget->x;
    float y0 = clip.y / scale - context->widget->y;
    float x1 = (clip.x + clip.w) / scale - context->widget->x;
    float y1 = (clip.y + clip.h) / scale - context->widget->y;
    if (x0 < 0.0f) {
        x0 = 0.0f;
    }
    if (y0 < 0.0f) {
        y0 = 0.0f;
    }
    if (x1 > context->widget->width) {
        x1 = context->widget->width;
    }
    if (y1 > context->widget->height) {
        y1 = context->widget->height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    if (x) {
        *x = x0;
    }
    if (y) {
        *y = y0;
    }
    if (width) {
        *width = x1 - x0;
    }
    if (height) {
        *height = y1 - y0;
    }
    return true;
}

static float clamp_local_x(const pt_paint_context *context, float x, float width) {
    if (x < 0.0f) {
        width += x;
        x = 0.0f;
    }
    if (width < 0.0f) {
        width = 0.0f;
    }
    float max_width = context->widget->width - x;
    if (width > max_width) {
        width = max_width;
    }
    return width > 0.0f ? width : 0.0f;
}

static float clamp_local_y(const pt_paint_context *context, float y, float height) {
    if (y < 0.0f) {
        height += y;
        y = 0.0f;
    }
    if (height < 0.0f) {
        height = 0.0f;
    }
    float max_height = context->widget->height - y;
    if (height > max_height) {
        height = max_height;
    }
    return height > 0.0f ? height : 0.0f;
}

void pt_paint_push_clip(pt_paint_context *context, float x, float y, float width,
                        float height) {
    if (!context || !context->canvas || !context->widget) {
        return;
    }
    width = clamp_local_x(context, x, width);
    height = clamp_local_y(context, y, height);
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (y < 0.0f) {
        y = 0.0f;
    }
    if (width <= 0.0f || height <= 0.0f) {
        /* Push an empty clip instead of unbalancing the stack. */
        pt_canvas_push_clip(context->canvas, context->widget->x, context->widget->y, 0.0f,
                            0.0f);
        return;
    }
    pt_canvas_push_clip(context->canvas, context->widget->x + x, context->widget->y + y,
                        width, height);
}

void pt_paint_pop_clip(pt_paint_context *context) {
    if (context && context->canvas) {
        pt_canvas_pop_clip(context->canvas);
    }
}

void pt_paint_fill_rect(pt_paint_context *context, float x, float y, float width,
                        float height, pt_color color) {
    if (!context || !context->canvas || !context->widget || (color & 0xFFu) == 0) {
        return;
    }
    width = clamp_local_x(context, x, width);
    height = clamp_local_y(context, y, height);
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (y < 0.0f) {
        y = 0.0f;
    }
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    pt_canvas_fill_rect(context->canvas, context->widget->x + x, context->widget->y + y,
                        width, height, color);
}

void pt_paint_fill_rounded_rect(pt_paint_context *context, float x, float y, float width,
                                float height, float radius, pt_color color) {
    if (!context || !context->canvas || !context->widget || (color & 0xFFu) == 0) {
        return;
    }
    width = clamp_local_x(context, x, width);
    height = clamp_local_y(context, y, height);
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (y < 0.0f) {
        y = 0.0f;
    }
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    pt_canvas_fill_rounded_rect(context->canvas, context->widget->x + x,
                                context->widget->y + y, width, height, radius, color);
}

void pt_paint_stroke_rect(pt_paint_context *context, float x, float y, float width,
                          float height, float line_width, pt_color color) {
    if (!context || !context->canvas || !context->widget || (color & 0xFFu) == 0 ||
        line_width <= 0.0f) {
        return;
    }
    width = clamp_local_x(context, x, width);
    height = clamp_local_y(context, y, height);
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (y < 0.0f) {
        y = 0.0f;
    }
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    pt_canvas_stroke_rounded_rect(context->canvas, context->widget->x + x,
                                  context->widget->y + y, width, height, 0.0f, line_width,
                                  color);
}

void pt_paint_draw_line(pt_paint_context *context, float x0, float y0, float x1, float y1,
                        float line_width, pt_color color) {
    if (!context || !context->canvas || !context->widget || (color & 0xFFu) == 0 ||
        line_width <= 0.0f) {
        return;
    }
    pt_canvas_draw_line(context->canvas, context->widget->x + x0, context->widget->y + y0,
                        context->widget->x + x1, context->widget->y + y1, line_width,
                        color);
}

static pt_font *custom_font(const pt_paint_context *context, const char *family, float size,
                            int weight, int slant) {
    if (!context || !context->canvas) {
        return NULL;
    }
    if (!isfinite(size) || size <= 0.0f) {
        return NULL;
    }
    if (weight < 100 || weight > 900) {
        weight = pt_font_weight_regular;
    }
    if (slant < pt_font_slant_upright || slant > pt_font_slant_oblique) {
        slant = pt_font_slant_upright;
    }
    const char *name = family && *family ? family : "monospace";
    return pt_font_acquire(name, weight, (pt_font_slant)slant,
                           size * context->canvas->scale);
}

void pt_paint_draw_text(pt_paint_context *context, const char *family, float size,
                        int weight, int slant, float x, float baseline, const char *text,
                        size_t length, pt_color color) {
    if (!context || !context->canvas || !context->widget || !text || length == 0 ||
        (color & 0xFFu) == 0) {
        return;
    }
    pt_font *font = custom_font(context, family, size, weight, slant);
    if (!font) {
        return;
    }
    pt_canvas_draw_text_line(context->canvas, font, text, length,
                             context->widget->x + x, context->widget->y + baseline, 0.0f,
                             pt_text_align_start, color, false);
}

bool pt_paint_font_has_glyph(pt_paint_context *context, const char *family, float size,
                             uint32_t codepoint) {
    if (!context || !context->canvas) {
        return false;
    }
    pt_font *font = custom_font(context, family, size, pt_font_weight_regular,
                                pt_font_slant_upright);
    if (!font || !font->face) {
        return false;
    }
    return FT_Get_Char_Index(font->face, codepoint) != 0;
}

float pt_paint_measure_text(pt_paint_context *context, const char *family, float size,
                            int weight, int slant, const char *text, size_t length) {
    if (!context || !context->canvas || !text || length == 0) {
        return 0.0f;
    }
    pt_font *font = custom_font(context, family, size, weight, slant);
    if (!font) {
        return 0.0f;
    }
    return pt_font_measure(font, text, length) / context->canvas->scale;
}

bool pt_paint_font_metrics(pt_paint_context *context, const char *family, float size,
                           int weight, int slant, float *ascent, float *descent,
                           float *line_height, float *cell_width) {
    if (!context || !context->canvas) {
        return false;
    }
    pt_font *font = custom_font(context, family, size, weight, slant);
    if (!font) {
        return false;
    }
    float scale = context->canvas->scale > 0.0f ? context->canvas->scale : 1.0f;
    if (ascent) {
        *ascent = font->ascent / scale;
    }
    if (descent) {
        *descent = font->descent / scale;
    }
    if (line_height) {
        *line_height = font->line_height / scale;
    }
    if (cell_width) {
        *cell_width = pt_font_measure(font, "M", 1) / scale;
    }
    return true;
}

bool pt_window_paint_pixels(pt_window *window, uint32_t *pixels, int width,
                            int height) {
    if (!window || !pixels || width <= 0 || height <= 0) {
        return false;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, width, height, width, 1.0f);
    return pt_window_paint(window, &canvas);
}
