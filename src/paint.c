/*
 * Pt widget painting.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

static pt_color color_from_theme(const pt_widget *widget, pt_theme_token token) {
    pt_app *app = pt_widget_app(widget);
    if (!app) {
        return 0xFFFFFFFFu;
    }
    return pt_widget_theme_color(widget, token);
}

static float metric_from_theme(const pt_widget *widget, pt_theme_metric metric) {
    return pt_widget_theme_metric_value(widget, metric);
}

static pt_color text_color(const pt_widget *widget) {
    pt_color color = widget->has_foreground ? widget->foreground : color_from_theme(widget,
                                                                                   pt_theme_text);
    if (!widget->enabled) {
        color = pt_color_blend(color, color_from_theme(widget, pt_theme_background), 0.55f);
    }
    return color;
}

static pt_color shape_background(const pt_widget *widget, pt_theme_token token) {
    return widget->has_background ? widget->background : color_from_theme(widget, token);
}

static float shape_radius(const pt_widget *widget) {
    float radius = widget->radius;
    float limit = pt_min_float(widget->width, widget->height) * 0.5f;
    return pt_min_float(pt_max_float(0.0f, radius), limit);
}

static void paint_focus_ring(pt_canvas *canvas, const pt_widget *widget) {
    if (!widget->focused || !widget->enabled) {
        return;
    }
    bool always_visible = pt_widget_kind_is_text_input(widget->kind);
    if (!widget->focus_visible && !always_visible) {
        return;
    }
    pt_color focus = color_from_theme(widget, pt_theme_focus);
    focus = pt_color_alpha(focus, 200);
    float width = metric_from_theme(widget, pt_theme_metric_focus_width);
    float offset = metric_from_theme(widget, pt_theme_metric_focus_offset);
    if (width <= 0.0f) {
        return;
    }
    float radius = shape_radius(widget);
    pt_canvas_stroke_rounded_rect(canvas, widget->x - offset, widget->y - offset,
                                  widget->width + offset * 2.0f,
                                  widget->height + offset * 2.0f,
                                  pt_max_float(0.0f, radius + offset), width, focus);
}

static void paint_outline(pt_canvas *canvas, const pt_widget *widget, pt_color color,
                          float width) {
    if (width <= 0.0f) {
        return;
    }
    pt_canvas_stroke_rounded_rect(canvas, widget->x, widget->y, widget->width, widget->height,
                                  shape_radius(widget), width, color);
}

static void paint_drop_highlight(pt_canvas *canvas, pt_widget *widget) {
    if (!widget->drop_hover) {
        return;
    }
    pt_color accent = color_from_theme(widget, pt_theme_accent);
    pt_canvas_fill_rounded_rect(canvas, widget->x, widget->y, widget->width, widget->height,
                                shape_radius(widget), pt_color_alpha(accent, 22));
    pt_canvas_stroke_rounded_rect(canvas, widget->x + 1.0f, widget->y + 1.0f,
                                  pt_max_float(0.0f, widget->width - 2.0f),
                                  pt_max_float(0.0f, widget->height - 2.0f),
                                  pt_max_float(0.0f, shape_radius(widget) - 1.0f), 2.0f,
                                  accent);
}

static pt_rect text_area(const pt_widget *widget) {
    return (pt_rect){widget->x + widget->padding_left, widget->y + widget->padding_top,
                     pt_max_float(0.0f, widget->width - widget->padding_left -
                                            widget->padding_right),
                     pt_max_float(0.0f, widget->height - widget->padding_top -
                                            widget->padding_bottom)};
}

/* The rubber band every row view draws while a drag is in progress. Clipped to
 * the content box so it never spills over the chrome the view paints around it. */
static void paint_marquee(pt_canvas *canvas, const pt_widget *view,
                          const pt_marquee *marquee) {
    if (!marquee->dragging) {
        return;
    }
    pt_rect area = pt_rect_intersect(pt_marquee_rect(marquee), text_area(view));
    if (area.w <= 0.0f || area.h <= 0.0f) {
        return;
    }
    pt_color accent = color_from_theme(view, pt_theme_accent);
    pt_canvas_fill_rect(canvas, area.x, area.y, area.w, area.h, pt_color_alpha(accent, 0x24u));
    pt_canvas_stroke_rounded_rect(canvas, area.x + 0.5f, area.y + 0.5f,
                                  pt_max_float(0.0f, area.w - 1.0f),
                                  pt_max_float(0.0f, area.h - 1.0f), 3.0f, 1.0f,
                                  pt_color_alpha(accent, 0xC0u));
}

static void paint_rich_single_line(pt_canvas *canvas, const pt_widget *widget, pt_color color,
                                   pt_text_align align, bool ellipsis, float offset_x,
                                   pt_rect area) {
    pt_layout_ctx ctx = pt_layout_context_for(widget);
    pt_font *base = pt_widget_font_for(widget, &ctx);
    pt_text_runs runs = {0};
    if (!base || !pt_text_runs_build(widget, &ctx, &runs)) {
        return;
    }
    float line_height = runs.line_height;
    float baseline = area.y + (area.h - line_height) * 0.5f + runs.ascent;
    pt_canvas_push_clip(canvas, area.x, area.y, area.w, area.h);
    pt_text_runs_draw_line(canvas, &runs, 0, runs.length, area.x + offset_x, baseline,
                           area.w - offset_x, align, color, ellipsis);
    pt_canvas_pop_clip(canvas);
    pt_text_runs_free(&runs);
}

static bool paint_rich_wrapped(pt_canvas *canvas, const pt_widget *widget, pt_color color,
                               pt_rect area) {
    pt_layout_ctx ctx = pt_layout_context_for(widget);
    pt_font *base = pt_widget_font_for(widget, &ctx);
    pt_text_runs runs = {0};
    if (!base || !pt_text_runs_build(widget, &ctx, &runs)) {
        return false;
    }
    pt_text_line *lines = NULL;
    int count = pt_text_runs_wrap(&runs, area.w, &lines);
    float ascent = runs.ascent;
    float y = area.y;
    for (int i = 0; i < count; ++i) {
        if (y + runs.line_height > area.y + area.h) {
            break;
        }
        pt_text_runs_draw_line(canvas, &runs, lines[i].start, lines[i].end, area.x,
                               y + ascent, area.w, widget->text_align, color, false);
        y += runs.line_height;
    }
    free(lines);
    pt_text_runs_free(&runs);
    return true;
}

static void paint_single_line(pt_canvas *canvas, const pt_widget *widget, const char *text,
                              pt_color color, pt_text_align align, bool ellipsis,
                              float offset_x, pt_rect area) {
    if (widget->kind == pt_widget_label && widget->span_count > 0 && text == widget->text) {
        paint_rich_single_line(canvas, widget, color, align, ellipsis, offset_x, area);
        return;
    }
    pt_layout_ctx ctx = pt_layout_context_for(widget);
    pt_font *font = pt_widget_font_for(widget, &ctx);
    if (!font || !text || !*text || area.w <= 0.0f) {
        return;
    }
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    float line_height = font->line_height / scale;
    float baseline = area.y + (area.h - line_height) * 0.5f + font->ascent / scale;
    pt_canvas_push_clip(canvas, area.x, area.y, area.w, area.h);
    pt_canvas_draw_text_line(canvas, font, text, strlen(text),
                             area.x + offset_x, baseline, area.w - offset_x, align, color,
                             ellipsis);
    pt_canvas_pop_clip(canvas);
}

static void icon_polyline(pt_canvas *canvas, float cx, float cy, float scale, pt_color color,
                          float line_width, const float *points, size_t count) {
    for (size_t i = 0; i + 1 < count; ++i) {
        float x0 = cx + (points[i * 2] - 12.0f) * scale;
        float y0 = cy + (points[i * 2 + 1] - 12.0f) * scale;
        float x1 = cx + (points[i * 2 + 2] - 12.0f) * scale;
        float y1 = cy + (points[i * 2 + 3] - 12.0f) * scale;
        pt_canvas_draw_line(canvas, x0, y0, x1, y1, line_width, color);
    }
}

static void icon_ring(pt_canvas *canvas, float cx, float cy, float scale, float gx, float gy,
                      float radius, pt_color color, float line_width) {
    const int segments = 20;
    float rx = cx + (gx - 12.0f) * scale;
    float ry = cy + (gy - 12.0f) * scale;
    float r = radius * scale;
    float previous_x = rx + r;
    float previous_y = ry;
    for (int i = 1; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 6.2831853f;
        float x = rx + cosf(angle) * r;
        float y = ry + sinf(angle) * r;
        pt_canvas_draw_line(canvas, previous_x, previous_y, x, y, line_width, color);
        previous_x = x;
        previous_y = y;
    }
}

void pt_paint_icon_glyph(pt_canvas *canvas, pt_icon icon, float cx, float cy, float size,
                         pt_color color) {
    if (!canvas || icon == pt_icon_none || size <= 0.0f) {
        return;
    }
    float scale = size / 24.0f;
    float width = pt_max_float(1.2f, size * 0.09f);
    switch (icon) {
    case pt_icon_chevron_right: {
        const float points[] = {9.0f, 6.0f, 15.0f, 12.0f, 9.0f, 18.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, points, 3);
        break;
    }
    case pt_icon_chevron_down: {
        const float points[] = {6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 9.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, points, 3);
        break;
    }
    case pt_icon_chevron_left: {
        const float points[] = {15.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, points, 3);
        break;
    }
    case pt_icon_chevron_up: {
        const float points[] = {6.0f, 15.0f, 12.0f, 9.0f, 18.0f, 15.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, points, 3);
        break;
    }
    case pt_icon_search: {
        icon_ring(canvas, cx, cy, scale, 10.5f, 10.5f, 6.0f, color, width);
        const float handle[] = {15.0f, 15.0f, 20.0f, 20.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, handle, 2);
        break;
    }
    case pt_icon_menu:
        for (int i = 0; i < 3; ++i) {
            float y = 7.0f + (float)i * 5.0f;
            pt_canvas_draw_line(canvas, cx + (4.0f - 12.0f) * scale,
                                cy + (y - 12.0f) * scale, cx + (20.0f - 12.0f) * scale,
                                cy + (y - 12.0f) * scale, width, color);
        }
        break;
    case pt_icon_close: {
        pt_canvas_draw_line(canvas, cx - 6.0f * scale, cy - 6.0f * scale,
                            cx + 6.0f * scale, cy + 6.0f * scale, width, color);
        pt_canvas_draw_line(canvas, cx + 6.0f * scale, cy - 6.0f * scale,
                            cx - 6.0f * scale, cy + 6.0f * scale, width, color);
        break;
    }
    case pt_icon_check: {
        const float points[] = {5.0f, 12.5f, 10.0f, 17.5f, 19.0f, 7.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, points, 3);
        break;
    }
    case pt_icon_folder: {
        const float points[] = {3.0f, 7.0f, 3.0f, 19.0f, 21.0f, 19.0f, 21.0f, 9.0f,
                                12.0f, 9.0f, 10.0f, 6.0f, 3.0f, 6.0f, 3.0f, 7.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, points, 8);
        break;
    }
    case pt_icon_document: {
        const float outline[] = {6.0f, 3.0f, 14.0f, 3.0f, 19.0f, 8.0f, 19.0f, 21.0f,
                                 6.0f, 21.0f, 6.0f, 3.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, outline, 6);
        const float fold[] = {14.0f, 3.0f, 14.0f, 8.0f, 19.0f, 8.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, fold, 3);
        break;
    }
    case pt_icon_plus: {
        pt_canvas_draw_line(canvas, cx, cy - 7.0f * scale, cx, cy + 7.0f * scale, width,
                            color);
        pt_canvas_draw_line(canvas, cx - 7.0f * scale, cy, cx + 7.0f * scale, cy, width,
                            color);
        break;
    }
    case pt_icon_settings: {
        icon_ring(canvas, cx, cy, scale, 12.0f, 12.0f, 4.0f, color, width);
        for (int i = 0; i < 8; ++i) {
            float angle = (float)i / 8.0f * 6.2831853f;
            float inner = 6.4f;
            float outer = 9.2f;
            pt_canvas_draw_line(canvas, cx + cosf(angle) * inner * scale,
                                cy + sinf(angle) * inner * scale,
                                cx + cosf(angle) * outer * scale,
                                cy + sinf(angle) * outer * scale, width, color);
        }
        break;
    }
    case pt_icon_home: {
        const float roof[] = {3.0f, 11.0f, 12.0f, 3.0f, 21.0f, 11.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, roof, 3);
        const float walls[] = {6.0f, 9.5f, 6.0f, 20.0f, 18.0f, 20.0f, 18.0f, 9.5f};
        icon_polyline(canvas, cx, cy, scale, color, width, walls, 4);
        const float door[] = {10.0f, 20.0f, 10.0f, 14.0f, 14.0f, 14.0f, 14.0f, 20.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, door, 4);
        break;
    }
    case pt_icon_bell: {
        const float body[] = {6.0f, 16.0f, 7.0f, 10.0f, 9.0f, 6.5f, 12.0f, 5.5f,
                              15.0f, 6.5f, 17.0f, 10.0f, 18.0f, 16.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, body, 7);
        pt_canvas_draw_line(canvas, cx - 7.0f * scale, cy + 5.0f * scale,
                            cx + 7.0f * scale, cy + 5.0f * scale, width, color);
        pt_canvas_draw_line(canvas, cx - 1.5f * scale, cy + 8.0f * scale,
                            cx + 1.5f * scale, cy + 8.0f * scale, width, color);
        break;
    }
    case pt_icon_user: {
        icon_ring(canvas, cx, cy, scale, 12.0f, 8.0f, 4.2f, color, width);
        const float shoulders[] = {4.5f, 20.0f, 4.5f, 17.0f, 6.5f, 14.2f, 9.0f, 12.8f,
                                   15.0f, 12.8f, 17.5f, 14.2f, 19.5f, 17.0f, 19.5f, 20.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, shoulders, 8);
        break;
    }
    case pt_icon_refresh: {
        icon_ring(canvas, cx, cy, scale, 12.0f, 12.0f, 7.0f, color, width);
        pt_canvas_fill_circle(canvas, cx + 8.2f * scale, cy - 3.4f * scale, width * 0.9f,
                              color);
        const float arrow[] = {15.0f, 3.5f, 19.0f, 4.2f, 18.4f, 8.3f};
        icon_polyline(canvas, cx, cy, scale, color, width, arrow, 3);
        break;
    }
    case pt_icon_trash: {
        pt_canvas_draw_line(canvas, cx - 8.0f * scale, cy - 5.0f * scale,
                            cx + 8.0f * scale, cy - 5.0f * scale, width, color);
        const float handle[] = {9.0f, 7.0f, 9.0f, 4.0f, 15.0f, 4.0f, 15.0f, 7.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, handle, 4);
        const float body[] = {6.0f, 7.0f, 7.0f, 21.0f, 17.0f, 21.0f, 18.0f, 7.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, body, 4);
        pt_canvas_draw_line(canvas, cx - 2.0f * scale, cy - 1.0f * scale,
                            cx - 2.0f * scale, cy + 5.0f * scale, width, color);
        pt_canvas_draw_line(canvas, cx + 2.0f * scale, cy - 1.0f * scale,
                            cx + 2.0f * scale, cy + 5.0f * scale, width, color);
        break;
    }
    case pt_icon_arrow_right: {
        pt_canvas_draw_line(canvas, cx - 8.0f * scale, cy, cx + 8.0f * scale, cy, width,
                            color);
        const float head[] = {14.0f, 6.0f, 20.0f, 12.0f, 14.0f, 18.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, head, 3);
        break;
    }
    case pt_icon_arrow_left: {
        pt_canvas_draw_line(canvas, cx - 8.0f * scale, cy, cx + 8.0f * scale, cy, width,
                            color);
        const float head[] = {10.0f, 6.0f, 4.0f, 12.0f, 10.0f, 18.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, head, 3);
        break;
    }
    case pt_icon_info: {
        icon_ring(canvas, cx, cy, scale, 12.0f, 12.0f, 8.5f, color, width);
        pt_canvas_draw_line(canvas, cx, cy - 1.0f * scale, cx, cy + 5.5f * scale, width,
                            color);
        pt_canvas_fill_circle(canvas, cx, cy - 4.5f * scale, width * 0.85f, color);
        break;
    }
    case pt_icon_warning: {
        const float triangle[] = {12.0f, 3.0f, 22.0f, 20.0f, 2.0f, 20.0f, 12.0f, 3.0f};
        icon_polyline(canvas, cx, cy, scale, color, width, triangle, 4);
        pt_canvas_draw_line(canvas, cx, cy - 3.0f * scale, cx, cy + 2.5f * scale, width,
                            color);
        pt_canvas_fill_circle(canvas, cx, cy + 5.2f * scale, width * 0.85f, color);
        break;
    }
    case pt_icon_none:
    default:
        break;
    }
}

static void paint_wrapped_text(pt_canvas *canvas, pt_widget *widget, const char *text,
                               pt_color color) {
    pt_rect area = text_area(widget);
    pt_canvas_push_clip(canvas, area.x, area.y, area.w, area.h);
    if (widget->span_count > 0 && pt_widget_supports_spans(widget)) {
        if (paint_rich_wrapped(canvas, widget, color, area)) {
            pt_canvas_pop_clip(canvas);
            return;
        }
    }
    pt_layout_ctx ctx = pt_layout_context_for(widget);
    pt_font *font = pt_widget_font_for(widget, &ctx);
    if (!font || !text || !*text) {
        pt_canvas_pop_clip(canvas);
        return;
    }
    pt_canvas_draw_text_wrapped(canvas, font, text, area.x, area.y, area.w, area.h,
                                widget->text_align, color);
    pt_canvas_pop_clip(canvas);
}

static char *entry_display_bullets(const pt_widget *entry) {
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    if (!entry->password) {
        char *copy = pt_alloc(len + 1);
        memcpy(copy, text, len + 1);
        return copy;
    }
    size_t count = pt_utf8_count(text, len);
    char *display = pt_alloc(count * 3 + 1);
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        display[offset++] = (char)0xE2;
        display[offset++] = (char)0x80;
        display[offset++] = (char)0xA2;
    }
    display[offset] = '\0';
    return display;
}

static size_t display_position_for(const pt_widget *entry, size_t text_pos) {
    if (!entry->password) {
        return text_pos;
    }
    const char *text = entry->text ? entry->text : "";
    return pt_utf8_count(text, text_pos) * 3;
}

static float measure_prefix_width(const pt_font *font, const char *text, size_t pos,
                                  float scale) {
    return pt_font_measure(font, text, pos) / scale;
}

static void paint_entry(pt_canvas *canvas, pt_widget *entry) {
    pt_color background = shape_background(entry, pt_theme_control);
    if (!entry->has_background && entry->hovered && !entry->focused) {
        background = color_from_theme(entry, pt_theme_control_hover);
    }
    if (!entry->enabled) {
        background = pt_color_alpha(background, 140);
    }
    pt_canvas_fill_rounded_rect(canvas, entry->x, entry->y, entry->width, entry->height,
                                shape_radius(entry), background);
    pt_color border;
    if (entry->has_border_color) {
        border = entry->border_color;
    } else if (entry->focused) {
        border = color_from_theme(entry, pt_theme_accent);
    } else if (entry->hovered) {
        border = pt_color_lighten(color_from_theme(entry, pt_theme_border_strong), 0.12f);
    } else {
        border = color_from_theme(entry, pt_theme_border_strong);
    }
    float border_width = entry->has_border_width ? entry->border_width
                                                 : (entry->focused ? 1.5f : 1.0f);
    paint_outline(canvas, entry, border, border_width);

    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    if (!font) {
        return;
    }
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    float line_height = font->line_height / scale;
    pt_rect area = text_area(entry);
    float baseline = area.y + (area.h - line_height) * 0.5f + font->ascent / scale;
    pt_widget_entry_ensure_caret_visible(entry, area.w, font, scale);
    pt_color color = text_color(entry);

    size_t display_pos = display_position_for(entry, entry->cursor);
    size_t preedit_len = entry->preedit ? strlen(entry->preedit) : 0;
    char *display = entry_display_bullets(entry);
    size_t display_len = strlen(display);
    size_t selection_start = 0;
    size_t selection_end = 0;
    bool has_selection = entry->selection_anchor != entry->cursor;
    if (has_selection) {
        size_t text_len = entry->text ? strlen(entry->text) : 0;
        size_t a = entry->selection_anchor < entry->cursor ? entry->selection_anchor
                                                           : entry->cursor;
        size_t b = entry->selection_anchor < entry->cursor ? entry->cursor
                                                           : entry->selection_anchor;
        if (a > text_len) {
            a = text_len;
        }
        if (b > text_len) {
            b = text_len;
        }
        a = display_position_for(entry, a);
        b = display_position_for(entry, b);
        selection_start = a < display_len ? a : display_len;
        selection_end = b < display_len ? b : display_len;
    }
    char *combined = display;
    size_t combined_len = display_len;
    size_t preedit_start = 0;
    if (preedit_len > 0) {
        if (display_pos > display_len) {
            display_pos = display_len;
        }
        preedit_start = display_pos;
        combined = pt_alloc(display_len + preedit_len + 1);
        memcpy(combined, display, display_pos);
        memcpy(combined + display_pos, entry->preedit, preedit_len);
        memcpy(combined + display_pos + preedit_len, display + display_pos,
               display_len - display_pos);
        combined_len = display_len + preedit_len;
        combined[combined_len] = '\0';
        free(display);
        if (has_selection) {
            if (selection_start >= display_pos) {
                selection_start += preedit_len;
            }
            if (selection_end >= display_pos) {
                selection_end += preedit_len;
            }
        }
        display_pos += preedit_len;
    }
    pt_canvas_push_clip(canvas, area.x, area.y, area.w, area.h);
    if (has_selection && selection_end > selection_start) {
        float start_x = measure_prefix_width(font, combined, selection_start, scale);
        float end_x = measure_prefix_width(font, combined, selection_end, scale);
        pt_canvas_fill_rect(canvas, area.x - entry->scroll_x + start_x, area.y + 2.0f,
                            end_x - start_x, pt_max_float(0.0f, area.h - 4.0f),
                            color_from_theme(entry, pt_theme_selection));
    }
    if (combined_len == 0) {
        const char *placeholder = entry->placeholder ? entry->placeholder : "";
        if (*placeholder) {
            pt_color muted = color_from_theme(entry, pt_theme_text_muted);
            pt_canvas_draw_text_line(canvas, font, placeholder, strlen(placeholder), area.x,
                                     baseline, area.w, pt_text_align_start, muted, true);
        }
    } else {
        pt_canvas_draw_text_line(canvas, font, combined, combined_len,
                                 area.x - entry->scroll_x, baseline, 0.0f,
                                 pt_text_align_start, color, false);
    }
    if (preedit_len > 0) {
        float preedit_x = area.x - entry->scroll_x +
                          measure_prefix_width(font, combined, preedit_start, scale);
        float preedit_w = measure_prefix_width(font, entry->preedit, preedit_len, scale);
        pt_color underline = pt_color_alpha(color_from_theme(entry, pt_theme_accent), 0xB0u);
        pt_canvas_fill_rect(canvas, preedit_x, baseline + 2.0f, preedit_w, 1.5f, underline);
    }
    bool preedit_cursor_visible =
        preedit_len == 0 || (entry->preedit_cursor_begin >= 0 && entry->preedit_cursor_end >= 0);
    if (entry->focused && pt_widget_window(entry) && pt_widget_window(entry)->caret_visible &&
        !has_selection && preedit_cursor_visible) {
        int caret_x = 0;
        int caret_y = 0;
        int caret_w = 0;
        int caret_h = 0;
        pt_widget_ime_rect(entry, &caret_x, &caret_y, &caret_w, &caret_h);
        pt_color accent = color_from_theme(entry, pt_theme_accent);
        pt_canvas_fill_rect(canvas, (float)caret_x, (float)caret_y, 1.6f,
                            pt_max_float(6.0f, (float)caret_h - 2.0f), accent);
    }
    pt_canvas_pop_clip(canvas);
    free(combined);
    paint_focus_ring(canvas, entry);
}

static void paint_label(pt_canvas *canvas, pt_widget *label) {
    if (label->has_background) {
        pt_canvas_fill_rounded_rect(canvas, label->x, label->y, label->width, label->height,
                                    shape_radius(label), label->background);
    }
    const char *text = label->text ? label->text : "";
    if (!*text) {
        return;
    }
    pt_color color = text_color(label);
    if (label->wrap) {
        paint_wrapped_text(canvas, label, text, color);
    } else {
        paint_single_line(canvas, label, text, color, label->text_align, true, 0.0f,
                          text_area(label));
    }
}

static void paint_input_chrome(pt_canvas *canvas, pt_widget *input, pt_color background) {
    if (!input->has_background && input->hovered && !input->focused) {
        background = color_from_theme(input, pt_theme_control_hover);
    }
    if (!input->enabled) {
        background = pt_color_alpha(background, 140);
    }
    pt_canvas_fill_rounded_rect(canvas, input->x, input->y, input->width, input->height,
                                shape_radius(input), background);
    pt_color border;
    if (input->has_border_color) {
        border = input->border_color;
    } else if (input->focused) {
        border = color_from_theme(input, pt_theme_accent);
    } else if (input->hovered) {
        border = pt_color_lighten(color_from_theme(input, pt_theme_border_strong), 0.12f);
    } else {
        border = color_from_theme(input, pt_theme_border_strong);
    }
    float border_width = input->has_border_width ? input->border_width
                                                 : (input->focused ? 1.5f : 1.0f);
    paint_outline(canvas, input, border, border_width);
}

/* Combo box field. A filterable box owns an embedded entry that draws its own
 * text, caret and selection, so only the select-only mode paints a value here.
 * Focus is carried by the thick accent outline rather than a separate ring:
 * the ring would have to be drawn around the outer box while the focus flag
 * lives on the inset field, and modern toolkits resolve the same case with a
 * 2px outline. */
static void paint_combobox(pt_canvas *canvas, pt_widget *combobox) {
    bool open = pt_combobox_is_open(combobox);
    pt_widget *field = pt_combobox_field_widget(combobox);
    bool focused = combobox->focused || (field && field->focused);

    pt_color background = shape_background(combobox, pt_theme_control);
    if (!combobox->has_background) {
        if (combobox->press_mix > 0.01f) {
            background = pt_color_blend(background,
                                        color_from_theme(combobox, pt_theme_control_pressed),
                                        combobox->press_mix);
        } else if (combobox->hovered && !focused) {
            background = color_from_theme(combobox, pt_theme_control_hover);
        }
    }
    if (!combobox->enabled) {
        background = pt_color_alpha(background, 140);
    }
    pt_canvas_fill_rounded_rect(canvas, combobox->x, combobox->y, combobox->width,
                                combobox->height, shape_radius(combobox), background);

    pt_color border;
    if (combobox->has_border_color) {
        border = combobox->border_color;
    } else if (open || focused) {
        border = color_from_theme(combobox, pt_theme_accent);
    } else if (combobox->hovered) {
        border = pt_color_lighten(color_from_theme(combobox, pt_theme_border_strong), 0.12f);
    } else {
        border = color_from_theme(combobox, pt_theme_border_strong);
    }
    float border_width = combobox->has_border_width
                             ? combobox->border_width
                             : (open || focused ? 1.5f : 1.0f);
    paint_outline(canvas, combobox, border, border_width);

    pt_rect area = text_area(combobox);
    area.w = pt_max_float(0.0f, area.w - pt_combobox_chevron_reserve);

    pt_color muted = color_from_theme(combobox, pt_theme_text_muted);
    if (!field) {
        const char *value = combobox->text ? combobox->text : "";
        if (value[0] != '\0') {
            paint_single_line(canvas, combobox, value, text_color(combobox),
                              combobox->text_align, true, 0.0f, area);
        } else {
            const char *placeholder = combobox->placeholder ? combobox->placeholder : "";
            if (placeholder[0] != '\0') {
                paint_single_line(canvas, combobox, placeholder, pt_color_alpha(muted, 0xB0u),
                                  pt_text_align_start, true, 0.0f, area);
            }
        }
    }

    pt_color chevron = combobox->enabled
                           ? (open ? color_from_theme(combobox, pt_theme_accent) : muted)
                           : pt_color_alpha(muted, 0x80u);
    float center_x = combobox->x + combobox->width - combobox->padding_right -
                     pt_combobox_chevron_reserve * 0.5f;
    float center_y = combobox->y + combobox->height * 0.5f;
    pt_paint_icon_glyph(canvas, open ? pt_icon_chevron_up : pt_icon_chevron_down, center_x,
                        center_y, 11.0f, chevron);
}

static void paint_vertical_scrollbar(pt_canvas *canvas, const pt_widget *widget, float x,
                                     float y, float height, float viewport, float content,
                                     float offset) {
    if (content <= viewport + 0.5f || height <= 4.0f) {
        return;
    }
    float track_h = pt_max_float(0.0f, height - 8.0f);
    float thumb_h = pt_max_float(28.0f, track_h * (viewport / content));
    if (thumb_h > track_h) {
        thumb_h = track_h;
    }
    float max_scroll = pt_max_float(0.0f, content - viewport);
    float position = max_scroll > 0.0f ? offset / max_scroll : 0.0f;
    float thumb_y = y + 4.0f + (track_h - thumb_h) * pt_clamp_float(position, 0.0f, 1.0f);
    pt_canvas_fill_rounded_rect(canvas, x, thumb_y, 4.0f, thumb_h, 2.0f,
                                color_from_theme(widget, pt_theme_scrollbar));
}

static void paint_text_area(pt_canvas *canvas, pt_widget *area) {
    paint_input_chrome(canvas, area, shape_background(area, pt_theme_control));
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_font *font = pt_widget_font_for(area, &ctx);
    if (!font) {
        return;
    }
    pt_text_layout layout = {0};
    if (!pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout)) {
        return;
    }
    float scale = layout.scale;
    float ascent = layout.has_runs ? layout.runs.ascent : font->ascent / scale;
    pt_rect inner = text_area(area);
    float top = inner.y - area->scroll_y;
    pt_color color = text_color(area);
    size_t sel_start = 0;
    size_t sel_end = 0;
    bool has_selection = false;
    if (area->selection_anchor != area->cursor) {
        size_t a = area->selection_anchor;
        size_t b = area->cursor;
        if (a > b) {
            size_t swap = a;
            a = b;
            b = swap;
        }
        if (b > layout.display_len) {
            b = layout.display_len;
        }
        if (a > layout.display_len) {
            a = layout.display_len;
        }
        sel_start = a;
        sel_end = b;
        has_selection = sel_end > sel_start;
    }
    pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, inner.h);
    for (int i = 0; i < layout.line_count; ++i) {
        size_t line_start = layout.lines[i].start;
        size_t line_end = layout.lines[i].end;
        float line_top = top + (float)i * layout.line_height;
        if (line_top + layout.line_height < inner.y) {
            continue;
        }
        if (line_top > inner.y + inner.h) {
            break;
        }
        if (has_selection) {
            size_t a = sel_start > line_start ? sel_start : line_start;
            size_t b = sel_end < line_end ? sel_end : line_end;
            bool full = sel_start <= line_start && sel_end > line_end;
            if (full) {
                a = line_start;
                b = line_end;
            }
            if (b > a || full) {
                float x0 = inner.x + pt_text_layout_x_for_position(&layout, i, a);
                float x1 = inner.x + pt_text_layout_x_for_position(&layout, i, b);
                if (full) {
                    x0 = inner.x;
                    x1 = inner.x + inner.w;
                }
                if (x1 > x0) {
                    pt_canvas_fill_rect(canvas, x0, line_top + 2.0f, x1 - x0,
                                        pt_max_float(0.0f, layout.line_height - 4.0f),
                                        color_from_theme(area, pt_theme_selection));
                }
            }
        }
        if (line_end > line_start) {
            if (layout.has_runs) {
                pt_text_runs_draw_line(canvas, &layout.runs, line_start, line_end, inner.x,
                                       line_top + ascent, inner.w, pt_text_align_start, color,
                                       false);
            } else {
                pt_canvas_draw_text_line(canvas, font, layout.display + line_start,
                                         line_end - line_start, inner.x, line_top + ascent,
                                         inner.w, pt_text_align_start, color, false);
            }
        }
    }
    if (layout.display_len == 0 && area->placeholder && area->placeholder[0]) {
        pt_color muted = color_from_theme(area, pt_theme_text_muted);
        pt_canvas_draw_text_line(canvas, font, area->placeholder, strlen(area->placeholder),
                                 inner.x, inner.y + ascent, inner.w, pt_text_align_start,
                                 muted, true);
    }
    int caret_line = pt_text_layout_line_for_position(&layout, layout.caret);
    float caret_x = inner.x + pt_text_layout_x_for_position(&layout, caret_line, layout.caret);
    float caret_top = top + (float)caret_line * layout.line_height;
    size_t preedit_len = area->preedit ? strlen(area->preedit) : 0;
    bool preedit_caret_visible = true;
    if (preedit_len > 0) {
        pt_canvas_draw_text_line(canvas, font, area->preedit, preedit_len, caret_x,
                                 caret_top + ascent, inner.w, pt_text_align_start, color,
                                 false);
        pt_color underline = pt_color_alpha(color_from_theme(area, pt_theme_accent), 0xB0u);
        float preedit_w = pt_font_measure(font, area->preedit, preedit_len) / scale;
        pt_canvas_fill_rect(canvas, caret_x, caret_top + ascent + 2.0f, preedit_w, 1.5f,
                            underline);
        preedit_caret_visible =
            area->preedit_cursor_begin >= 0 && area->preedit_cursor_end >= 0;
        size_t preedit_caret = pt_widget_preedit_caret(area);
        caret_x += pt_font_measure(font, area->preedit, preedit_caret) / scale;
    }
    if (area->focused && pt_widget_window(area) && pt_widget_window(area)->caret_visible &&
        !has_selection && preedit_caret_visible) {
        pt_color accent = color_from_theme(area, pt_theme_accent);
        pt_canvas_fill_rect(canvas, caret_x, caret_top + 1.0f, 1.6f,
                            pt_max_float(6.0f, layout.line_height - 2.0f), accent);
    }
    pt_canvas_pop_clip(canvas);
    paint_vertical_scrollbar(canvas, area, area->x + area->width - 8.0f, inner.y,
                             inner.h, inner.h, area->scroll_content_height, area->scroll_y);
    paint_focus_ring(canvas, area);
    pt_text_layout_free(&layout);
}

static void paint_list(pt_canvas *canvas, pt_widget *list) {
    paint_input_chrome(canvas, list, shape_background(list, pt_theme_control));
    pt_list_state *state = list->extra;
    if (state) {
        paint_marquee(canvas, list, &state->marquee);
    }
    paint_focus_ring(canvas, list);
}

static void paint_list_row(pt_canvas *canvas, pt_widget *row) {
    /* A row that belongs to a popup floats as a rounded pill: the panel behind
     * it already carries the card surface, so the selection band follows the
     * menu language instead of the edge-to-edge list language. */
    bool in_popup = row->popup_owner != NULL;
    float band_inset = in_popup ? 2.0f : 0.0f;
    float band_x = row->x + band_inset;
    float band_w = pt_max_float(0.0f, row->width - 2.0f * band_inset);
    float band_radius = in_popup ? pt_min_float(row->radius, row->height * 0.5f) : 0.0f;
    if (row->row_selected) {
        if (in_popup) {
            pt_canvas_fill_rounded_rect(canvas, band_x, row->y, band_w, row->height,
                                        band_radius,
                                        color_from_theme(row, pt_theme_accent_soft));
        } else {
            pt_canvas_fill_rect(canvas, row->x, row->y, row->width, row->height,
                                color_from_theme(row, pt_theme_accent_soft));
            pt_canvas_fill_rect(canvas, row->x, row->y, 2.5f, row->height,
                                color_from_theme(row, pt_theme_accent));
        }
    } else if (row->hovered || row->press_mix > 0.01f) {
        float strength = pt_max_float(row->hover_mix, row->press_mix);
        pt_color hover = color_from_theme(row, pt_theme_surface_hover);
        pt_color tint = pt_color_alpha(hover, (uint8_t)(strength * 200.0f));
        if (in_popup) {
            pt_canvas_fill_rounded_rect(canvas, band_x, row->y, band_w, row->height,
                                        band_radius, tint);
        } else {
            pt_canvas_fill_rect(canvas, row->x, row->y, row->width, row->height, tint);
        }
    }
    if (row->text && row->text[0] != '\0') {
        pt_layout_ctx ctx = pt_layout_context_for(row);
        pt_font *font = pt_widget_font_for(row, &ctx);
        if (font) {
            float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
            float line_height = font->line_height / scale;
            float reserve = in_popup ? 24.0f : 0.0f;
            float x = row->x + row->padding_left;
            float width = pt_max_float(0.0f, row->width - row->padding_left -
                                              row->padding_right - reserve);
            float baseline = row->y + (row->height - line_height) * 0.5f +
                             font->ascent / scale;
            pt_canvas_push_clip(canvas, row->x, row->y, row->width, row->height);
            pt_color color = text_color(row);
            if (in_popup && row->row_current && row->enabled) {
                color = color_from_theme(row, pt_theme_accent);
            }
            pt_canvas_draw_text_line(canvas, font, row->text, strlen(row->text), x, baseline,
                                     width, pt_text_align_start, color, true);
            if (in_popup && row->row_current) {
                pt_color check = row->enabled
                                     ? color_from_theme(row, pt_theme_accent)
                                     : pt_color_alpha(color_from_theme(row,
                                                                       pt_theme_text_muted),
                                                      0x80u);
                pt_paint_icon_glyph(canvas, pt_icon_check,
                                    row->x + row->width - row->padding_right - 8.0f,
                                    row->y + row->height * 0.5f, 12.0f, check);
            }
            pt_canvas_pop_clip(canvas);
        }
    }
    if (!in_popup && row->parent && row->parent->kind == pt_widget_list &&
        row->list_index + 1 < row->parent->row_count) {
        pt_color divider = color_from_theme(row, pt_theme_border);
        pt_canvas_fill_rect(canvas, row->x + row->padding_left,
                            row->y + row->height - 1.0f,
                            pt_max_float(0.0f, row->width - row->padding_left -
                                                row->padding_right),
                            1.0f, pt_color_alpha(divider, 0x70u));
    }
}

static pt_color variant_base(const pt_widget *button) {
    if (button->has_background) {
        return button->background;
    }
    switch (button->button_variant) {
    case pt_button_secondary:
        return color_from_theme(button, pt_theme_surface);
    case pt_button_ghost:
        return pt_color_transparent;
    case pt_button_soft:
        return color_from_theme(button, pt_theme_accent_soft);
    case pt_button_danger:
        return color_from_theme(button, pt_theme_danger);
    case pt_button_primary:
    default:
        return color_from_theme(button, pt_theme_accent);
    }
}

static pt_color variant_hover(const pt_widget *button) {
    switch (button->button_variant) {
    case pt_button_secondary:
    case pt_button_ghost:
        return color_from_theme(button, pt_theme_surface_hover);
    case pt_button_soft:
        return pt_color_alpha(color_from_theme(button, pt_theme_accent), 0x4Du);
    case pt_button_danger:
        return pt_color_lighten(color_from_theme(button, pt_theme_danger), 0.12f);
    case pt_button_primary:
    default:
        return color_from_theme(button, pt_theme_accent_hover);
    }
}

static void paint_menu_item(pt_canvas *canvas, pt_widget *item) {
    float mix = pt_clamp_float(item->hover_mix + item->press_mix * 0.35f, 0.0f, 1.0f);
    pt_color background = 0;
    if (item->focused) {
        background = color_from_theme(item, pt_theme_accent_soft);
    } else if (mix > 0.0f) {
        background = pt_color_alpha(color_from_theme(item, pt_theme_surface_hover),
                                    (uint8_t)(mix * 190.0f));
    }
    if ((background & 0xFFu) != 0) {
        pt_canvas_fill_rounded_rect(canvas, item->x, item->y, item->width, item->height,
                                    shape_radius(item), background);
    }
    const char *label = item->text ? item->text : "";
    pt_rect area = text_area(item);
    if (area.w <= 0.0f || !*label) {
        return;
    }
    float shortcut_width = 0.0f;
    pt_layout_ctx ctx = pt_layout_context_for(item);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    pt_font *shortcut_font = NULL;
    if (item->shortcut && *item->shortcut) {
        shortcut_font = pt_font_acquire(ctx.font_family, pt_font_weight_regular, pt_font_slant_upright, 12.0f * scale);
        if (shortcut_font) {
            shortcut_width = pt_font_measure(shortcut_font, item->shortcut,
                                             strlen(item->shortcut)) /
                             scale;
        }
    }
    float gap = shortcut_width > 0.0f ? 14.0f : 0.0f;
    float label_width = pt_max_float(0.0f, area.w - shortcut_width - gap);
    paint_single_line(canvas, item, label, text_color(item), pt_text_align_start, true, 0.0f,
                      (pt_rect){area.x, area.y, label_width, area.h});
    if (shortcut_width > 0.0f && shortcut_font) {
        pt_color muted = color_from_theme(item, pt_theme_text_muted);
        float line_height = shortcut_font->line_height / scale;
        float baseline = area.y + (area.h - line_height) * 0.5f + shortcut_font->ascent / scale;
        pt_canvas_push_clip(canvas, area.x + area.w - shortcut_width, area.y, shortcut_width,
                            area.h);
        pt_canvas_draw_text_line(canvas, shortcut_font, item->shortcut,
                                 strlen(item->shortcut), area.x + area.w - shortcut_width,
                                 baseline, shortcut_width, pt_text_align_end, muted, false);
        pt_canvas_pop_clip(canvas);
    }
}

static void paint_button(pt_canvas *canvas, pt_widget *button) {
    if (button->menu_item) {
        paint_menu_item(canvas, button);
        return;
    }
    pt_color base = variant_base(button);
    pt_color hover = variant_hover(button);
    pt_color color = pt_color_blend(base, hover, button->hover_mix);
    if (button->press_mix > 0.0f) {
        color = pt_color_blend(color, 0x000000FFu, button->press_mix * 0.18f);
    }
    if (!button->enabled) {
        color = pt_color_alpha(color, (uint8_t)((color & 0xFFu) / 2u));
    }
    if ((color & 0xFFu) != 0) {
        if (button->button_variant == pt_button_primary ||
            button->button_variant == pt_button_danger ||
            button->button_variant == pt_button_soft) {
            pt_canvas_fill_rounded_rect_gradient(canvas, button->x, button->y, button->width,
                                                 button->height, shape_radius(button),
                                                 pt_color_lighten(color, 0.05f), color);
        } else {
            pt_canvas_fill_rounded_rect(canvas, button->x, button->y, button->width,
                                        button->height, shape_radius(button), color);
        }
    }
    pt_color border = 0;
    float border_width = 0.0f;
    if (button->has_border_color) {
        border = button->border_color;
        border_width = button->has_border_width ? button->border_width : 1.0f;
    } else if (button->button_variant == pt_button_secondary) {
        border = color_from_theme(button, pt_theme_border_strong);
        border_width = 1.0f;
    } else if (button->button_variant == pt_button_soft) {
        border = pt_color_alpha(color_from_theme(button, pt_theme_accent), 0x40u);
        border_width = 1.0f;
    } else if (button->has_border_width) {
        border = color_from_theme(button, pt_theme_border);
        border_width = button->border_width;
    }
    paint_outline(canvas, button, border, border_width);

    pt_color foreground;
    if (button->has_foreground) {
        foreground = button->foreground;
    } else if (button->button_variant == pt_button_primary ||
               button->button_variant == pt_button_danger) {
        foreground = color_from_theme(button, pt_theme_accent_text);
    } else if (button->button_variant == pt_button_soft) {
        foreground = color_from_theme(button, pt_theme_accent);
    } else {
        foreground = color_from_theme(button, pt_theme_text);
    }
    if (!button->enabled) {
        foreground = pt_color_alpha(foreground, 140);
    }
    if (button->icon != pt_icon_none) {
        pt_layout_ctx icon_ctx = pt_layout_context_for(button);
        pt_font *font = pt_widget_font_for(button, &icon_ctx);
        float scale = icon_ctx.scale > 0.0f ? icon_ctx.scale : 1.0f;
        const char *label = button->text ? button->text : "";
        float text_width = font && *label ? pt_font_measure(font, label, strlen(label)) / scale
                                          : 0.0f;
        float icon_size = button->icon_size > 0.0f ? button->icon_size : 18.0f;
        icon_size = pt_min_float(icon_size, pt_max_float(8.0f, button->height - 10.0f));
        float gap = text_width > 0.0f ? 8.0f : 0.0f;
        float total = icon_size + gap + text_width;
        float start = button->x + (button->width - total) * 0.5f;
        pt_paint_icon_glyph(canvas, button->icon, start + icon_size * 0.5f,
                            button->y + button->height * 0.5f, icon_size, foreground);
        if (text_width > 0.0f) {
            paint_single_line(canvas, button, label, foreground, pt_text_align_start, false,
                              0.0f,
                              (pt_rect){start + icon_size + gap, button->y,
                                        text_width + 1.0f, button->height});
        }
    } else {
        paint_single_line(canvas, button, button->text ? button->text : "", foreground,
                          pt_text_align_center, false, 0.0f, text_area(button));
    }
    paint_focus_ring(canvas, button);
}

static void paint_toggle(pt_canvas *canvas, pt_widget *toggle) {
    float radius = pt_min_float(toggle->height * 0.5f, 14.0f);
    float knob_radius = pt_max_float(3.0f, radius - 3.5f);
    pt_color off = color_from_theme(toggle, pt_theme_track);
    pt_color on = color_from_theme(toggle, pt_theme_accent);
    pt_color track = pt_color_blend(off, on, toggle->checked_mix);
    if (toggle->hover_mix > 0.0f) {
        track = pt_color_lighten(track, toggle->hover_mix * 0.08f);
    }
    if (!toggle->enabled) {
        track = pt_color_alpha(track, 120);
    }
    pt_canvas_fill_rounded_rect(canvas, toggle->x, toggle->y, toggle->width, toggle->height,
                                radius, track);
    float travel = pt_max_float(0.0f, toggle->width - radius * 2.0f);
    float knob_x = toggle->x + radius + travel * toggle->checked_mix;
    float knob_y = toggle->y + toggle->height * 0.5f;
    pt_color knob = toggle->enabled ? 0xFFFFFFFFu : 0xE8ECF2B0u;
    pt_canvas_fill_circle(canvas, knob_x, knob_y, knob_radius + 0.75f,
                          pt_color_alpha(0xFF000000u, 40));
    pt_canvas_fill_circle(canvas, knob_x, knob_y, knob_radius, knob);
    paint_focus_ring(canvas, toggle);
}

static void paint_slider(pt_canvas *canvas, pt_widget *slider) {
    float track_left = slider->x + 9.0f;
    float track_width = pt_max_float(0.0f, slider->width - 18.0f);
    float center_y = slider->y + slider->height * 0.5f;
    float track_height = 5.0f;
    pt_color track = color_from_theme(slider, pt_theme_track);
    pt_color accent = color_from_theme(slider, pt_theme_accent);
    if (slider->hover_mix > 0.0f) {
        track = pt_color_lighten(track, slider->hover_mix * 0.06f);
    }
    pt_canvas_fill_rounded_rect(canvas, track_left, center_y - track_height * 0.5f,
                                track_width, track_height, track_height * 0.5f, track);
    float filled = track_width * pt_clamp_float(slider->value, 0.0f, 1.0f);
    if (filled > 0.0f) {
        pt_canvas_fill_rounded_rect_gradient(canvas, track_left,
                                             center_y - track_height * 0.5f, filled,
                                             track_height, track_height * 0.5f,
                                             pt_color_lighten(accent, 0.08f), accent);
    }
    float knob_x = track_left + filled;
    float knob_radius = 7.0f + slider->hover_mix * 0.6f + slider->press_mix * 0.9f;
    pt_canvas_fill_circle(canvas, knob_x, center_y, knob_radius + 3.0f,
                          pt_color_alpha(accent, 46));
    pt_canvas_fill_circle(canvas, knob_x, center_y, knob_radius + 1.0f,
                          color_from_theme(slider, pt_theme_surface));
    pt_canvas_fill_circle(canvas, knob_x, center_y, knob_radius, 0xFFFFFFFFu);
    pt_canvas_stroke_rounded_rect(canvas, knob_x - knob_radius, center_y - knob_radius,
                                  knob_radius * 2.0f, knob_radius * 2.0f, knob_radius, 1.0f,
                                  pt_color_alpha(0xFF000000u, 40));
    paint_focus_ring(canvas, slider);
}

static void paint_choice_row_background(pt_canvas *canvas, pt_widget *widget) {
    if (widget->hover_mix <= 0.01f || !widget->enabled) {
        return;
    }
    pt_color hover = color_from_theme(widget, pt_theme_surface_hover);
    hover = pt_color_alpha(hover, (uint8_t)(widget->hover_mix * 110.0f));
    pt_canvas_fill_rounded_rect(canvas, widget->x, widget->y, widget->width, widget->height,
                                pt_min_float(8.0f, widget->height * 0.5f), hover);
}

static void paint_checkbox(pt_canvas *canvas, pt_widget *widget) {
    paint_choice_row_background(canvas, widget);
    float inner_h = pt_max_float(0.0f, widget->height - widget->padding_top -
                                          widget->padding_bottom);
    float box = pt_min_float(18.0f, pt_max_float(8.0f, inner_h));
    float box_x = widget->x + widget->padding_left;
    float box_y = widget->y + widget->padding_top + (inner_h - box) * 0.5f;
    float mix = widget->checked_mix;
    pt_color background = pt_color_blend(color_from_theme(widget, pt_theme_control),
                                         color_from_theme(widget, pt_theme_accent), mix);
    if (widget->hover_mix > 0.0f) {
        background = pt_color_lighten(background, widget->hover_mix * 0.04f);
    }
    if (!widget->enabled) {
        background = pt_color_alpha(background, 120);
    }
    pt_canvas_fill_rounded_rect(canvas, box_x, box_y, box, box, box * 0.28f, background);
    pt_color border = pt_color_blend(color_from_theme(widget, pt_theme_border_strong),
                                     color_from_theme(widget, pt_theme_accent), mix);
    pt_canvas_stroke_rounded_rect(canvas, box_x, box_y, box, box, box * 0.28f, 1.5f, border);
    float check = pt_clamp_float((mix - 0.15f) / 0.85f, 0.0f, 1.0f);
    if (check > 0.01f) {
        pt_color mark = color_from_theme(widget, pt_theme_accent_text);
        mark = pt_color_alpha(mark, (uint8_t)(check * 255.0f));
        pt_canvas_draw_line(canvas, box_x + box * 0.24f, box_y + box * 0.52f,
                            box_x + box * 0.43f, box_y + box * 0.72f, 2.0f, mark);
        pt_canvas_draw_line(canvas, box_x + box * 0.43f, box_y + box * 0.72f,
                            box_x + box * 0.77f, box_y + box * 0.30f, 2.0f, mark);
    }
    pt_rect area = {box_x + box + 10.0f, widget->y + widget->padding_top,
                    pt_max_float(0.0f, widget->width - widget->padding_left -
                                           widget->padding_right - box - 10.0f),
                    inner_h};
    paint_single_line(canvas, widget, widget->text ? widget->text : "", text_color(widget),
                      pt_text_align_start, false, 0.0f, area);
    paint_focus_ring(canvas, widget);
}

static void paint_radio(pt_canvas *canvas, pt_widget *widget) {
    paint_choice_row_background(canvas, widget);
    float inner_h = pt_max_float(0.0f, widget->height - widget->padding_top -
                                          widget->padding_bottom);
    float box = pt_min_float(18.0f, pt_max_float(8.0f, inner_h));
    float box_x = widget->x + widget->padding_left;
    float box_y = widget->y + widget->padding_top + (inner_h - box) * 0.5f;
    float mix = widget->checked_mix;
    float center_x = box_x + box * 0.5f;
    float center_y = box_y + box * 0.5f;
    pt_color background = pt_color_blend(color_from_theme(widget, pt_theme_control),
                                         color_from_theme(widget, pt_theme_accent), mix * 0.14f);
    if (!widget->enabled) {
        background = pt_color_alpha(background, 120);
    }
    pt_canvas_fill_circle(canvas, center_x, center_y, box * 0.5f, background);
    pt_color border = pt_color_blend(color_from_theme(widget, pt_theme_border_strong),
                                     color_from_theme(widget, pt_theme_accent), mix);
    pt_canvas_stroke_rounded_rect(canvas, box_x, box_y, box, box, box * 0.5f, 1.5f, border);
    if (mix > 0.01f) {
        pt_color dot = color_from_theme(widget, pt_theme_accent);
        dot = pt_color_alpha(dot, (uint8_t)(mix * 255.0f));
        pt_canvas_fill_circle(canvas, center_x, center_y, box * 0.26f * mix, dot);
    }
    pt_rect area = {box_x + box + 10.0f, widget->y + widget->padding_top,
                    pt_max_float(0.0f, widget->width - widget->padding_left -
                                           widget->padding_right - box - 10.0f),
                    inner_h};
    paint_single_line(canvas, widget, widget->text ? widget->text : "", text_color(widget),
                      pt_text_align_start, false, 0.0f, area);
    paint_focus_ring(canvas, widget);
}

static void paint_progress(pt_canvas *canvas, pt_widget *progress) {
    float height = progress->height;
    float width = progress->width;
    if (height <= 0.0f || width <= 0.0f) {
        return;
    }
    float radius = height * 0.5f;
    pt_color track = color_from_theme(progress, pt_theme_track);
    if (!progress->enabled) {
        track = pt_color_alpha(track, 120);
    }
    pt_canvas_fill_rounded_rect(canvas, progress->x, progress->y, width, height, radius, track);
    pt_color accent = color_from_theme(progress, pt_theme_accent);
    if (!progress->enabled) {
        accent = pt_color_alpha(accent, 140);
    }
    if (progress->indeterminate) {
        float segment = pt_max_float(height * 2.0f, width * 0.32f);
        float start = progress->phase * (width + segment) - segment;
        pt_canvas_push_clip(canvas, progress->x, progress->y, width, height);
        pt_canvas_fill_rounded_rect_gradient(canvas, progress->x + start, progress->y,
                                             segment, height, radius,
                                             pt_color_lighten(accent, 0.1f), accent);
        pt_canvas_pop_clip(canvas);
    } else {
        float filled = width * pt_clamp_float(progress->value, 0.0f, 1.0f);
        if (filled > 0.0f) {
            float filled_radius = pt_min_float(radius, filled * 0.5f);
            pt_canvas_fill_rounded_rect_gradient(canvas, progress->x, progress->y, filled,
                                                 height, filled_radius,
                                                 pt_color_lighten(accent, 0.1f), accent);
        }
    }
}

static void paint_spinner(pt_canvas *canvas, pt_widget *spinner) {
    float radius = pt_min_float(spinner->width, spinner->height) * 0.5f - 3.0f;
    if (radius < 2.0f) {
        return;
    }
    const float two_pi = 6.2831853f;
    float center_x = spinner->x + spinner->width * 0.5f;
    float center_y = spinner->y + spinner->height * 0.5f;
    pt_color accent = color_from_theme(spinner, pt_theme_accent);
    if (!spinner->enabled) {
        accent = pt_color_alpha(accent, 130);
    }
    pt_color faint = pt_color_alpha(color_from_theme(spinner, pt_theme_border_strong), 90);
    int track_steps = 32;
    for (int i = 0; i < track_steps; ++i) {
        float angle = (float)i / (float)track_steps * two_pi;
        pt_canvas_fill_circle(canvas, center_x + cosf(angle) * radius,
                              center_y + sinf(angle) * radius, 0.9f, faint);
    }
    int active_steps = 20;
    float base = spinner->phase * two_pi;
    for (int i = 0; i < active_steps; ++i) {
        float t = (float)i / (float)(active_steps - 1);
        float angle = base + t * 1.75f * 3.14159265f;
        pt_color dot = pt_color_alpha(accent, (uint8_t)(30.0f + 225.0f * t * t));
        pt_canvas_fill_circle(canvas, center_x + cosf(angle) * radius,
                              center_y + sinf(angle) * radius, 1.0f + 0.9f * t, dot);
    }
}

static void paint_segmented(pt_canvas *canvas, pt_widget *segmented) {
    if (segmented->segment_count == 0) {
        return;
    }
    pt_color background = shape_background(segmented, pt_theme_control);
    if (segmented->hover_mix > 0.0f && !segmented->has_background) {
        background = pt_color_blend(background,
                                    color_from_theme(segmented, pt_theme_control_hover),
                                    segmented->hover_mix * 0.6f);
    }
    if (!segmented->enabled) {
        background = pt_color_alpha(background, 140);
    }
    pt_canvas_fill_rounded_rect(canvas, segmented->x, segmented->y, segmented->width,
                                segmented->height, shape_radius(segmented), background);
    pt_color border = segmented->has_border_color
                          ? segmented->border_color
                          : color_from_theme(segmented, pt_theme_border_strong);
    paint_outline(canvas, segmented, border, 1.0f);
    float inset = 3.0f;
    pt_rect inner = {segmented->x + inset, segmented->y + inset,
                     pt_max_float(0.0f, segmented->width - inset * 2.0f),
                     pt_max_float(0.0f, segmented->height - inset * 2.0f)};
    float segment_width = inner.w / (float)segmented->segment_count;
    if (segment_width <= 0.0f) {
        return;
    }
    float target_x = inner.x + (float)segmented->segment_selected * segment_width + 1.0f;
    float target_w = pt_max_float(0.0f, segment_width - 2.0f);
    segmented->segment_target_x = target_x;
    segmented->segment_target_width = target_w;
    if (!segmented->segment_indicator_ready) {
        segmented->segment_x = target_x;
        segmented->segment_width = target_w;
        segmented->segment_indicator_ready = true;
    }
    if (segmented->segment_width > 0.5f) {
        pt_color accent = color_from_theme(segmented, pt_theme_accent);
        if (!segmented->enabled) {
            accent = pt_color_alpha(accent, 150);
        }
        pt_canvas_fill_rounded_rect_gradient(
            canvas, segmented->segment_x, inner.y, segmented->segment_width, inner.h,
            pt_min_float(7.0f, inner.h * 0.5f), pt_color_lighten(accent, 0.05f), accent);
    }
    for (uint32_t i = 0; i < segmented->segment_count; ++i) {
        float x = inner.x + (float)i * segment_width;
        bool selected = i == segmented->segment_selected;
        pt_color text = selected ? color_from_theme(segmented, pt_theme_accent_text)
                                 : color_from_theme(segmented, pt_theme_text_muted);
        if (!segmented->enabled) {
            text = pt_color_alpha(text, 130);
        }
        pt_rect area = {x, inner.y, segment_width, inner.h};
        paint_single_line(canvas, segmented, segmented->segment_labels[i], text,
                          pt_text_align_center, false, 0.0f, area);
    }
    paint_focus_ring(canvas, segmented);
}

static void paint_divider(pt_canvas *canvas, pt_widget *divider) {
    pt_color color = divider->has_border_color ? divider->border_color
                                               : color_from_theme(divider, pt_theme_border);
    if (divider->width >= divider->height) {
        pt_canvas_fill_rect(canvas, divider->x, divider->y + divider->height * 0.5f,
                            divider->width, 1.0f, color);
    } else {
        pt_canvas_fill_rect(canvas, divider->x + divider->width * 0.5f, divider->y, 1.0f,
                            divider->height, color);
    }
}

static void paint_card(pt_canvas *canvas, pt_widget *card) {
    float radius = shape_radius(card);
    pt_color shadow = color_from_theme(card, pt_theme_shadow);
    if ((shadow & 0xFFu) != 0) {
        pt_canvas_draw_shadow(canvas, card->x, card->y + 2.0f, card->width, card->height,
                              radius, metric_from_theme(card, pt_theme_metric_elevation_low),
                              shadow);
    }
    pt_color background = shape_background(card, pt_theme_surface_raised);
    if (card->has_background || !card->enabled) {
        pt_canvas_fill_rounded_rect(canvas, card->x, card->y, card->width, card->height, radius,
                                    background);
    } else {
        pt_canvas_fill_rounded_rect_gradient(canvas, card->x, card->y, card->width,
                                             card->height, radius,
                                             pt_color_lighten(background, 0.02f), background);
    }
    pt_color border = card->has_border_color ? card->border_color
                                              : color_from_theme(card, pt_theme_border);
    float border_width = card->has_border_width ? card->border_width : 1.0f;
    paint_outline(canvas, card, border, border_width);
}

static void paint_scroll_bar(pt_canvas *canvas, pt_widget *scroll) {
    if (scroll->scroll_content_height <= scroll->height + 0.5f) {
        return;
    }
    float viewport = scroll->height;
    float content = scroll->scroll_content_height;
    float track_x = scroll->x + scroll->width - 8.0f;
    float track_y = scroll->y + 4.0f;
    float track_h = pt_max_float(0.0f, scroll->height - 8.0f);
    float thumb_h = pt_max_float(28.0f, track_h * (viewport / content));
    if (thumb_h > track_h) {
        thumb_h = track_h;
    }
    float max_scroll = pt_max_float(0.0f, content - viewport);
    float position = max_scroll > 0.0f ? scroll->scroll_y / max_scroll : 0.0f;
    float thumb_y = track_y + (track_h - thumb_h) * pt_clamp_float(position, 0.0f, 1.0f);
    pt_canvas_fill_rounded_rect(canvas, track_x, thumb_y, 4.0f, thumb_h, 2.0f,
                                color_from_theme(scroll, pt_theme_scrollbar));
}

static void paint_horizontal_scrollbar(pt_canvas *canvas, const pt_widget *widget, float x,
                                       float y, float width, float viewport, float content,
                                       float offset) {
    if (content <= viewport + 0.5f || width <= 4.0f) {
        return;
    }
    float track_w = pt_max_float(0.0f, width - 8.0f);
    float thumb_w = pt_max_float(28.0f, track_w * (viewport / content));
    if (thumb_w > track_w) {
        thumb_w = track_w;
    }
    float max_scroll = pt_max_float(0.0f, content - viewport);
    float position = max_scroll > 0.0f ? offset / max_scroll : 0.0f;
    float thumb_x = x + 4.0f + (track_w - thumb_w) * pt_clamp_float(position, 0.0f, 1.0f);
    pt_canvas_fill_rounded_rect(canvas, thumb_x, y, thumb_w, 4.0f, 2.0f,
                                color_from_theme(widget, pt_theme_scrollbar));
}

static float table_columns_width(const pt_widget *table) {
    const pt_table_state *state = table ? table->extra : NULL;
    float width = 0.0f;
    if (state) {
        for (uint32_t i = 0; i < state->column_count; ++i) {
            width += state->column_widths[i];
        }
    }
    return width;
}

static void paint_icon_widget(pt_canvas *canvas, pt_widget *widget) {
    float size = widget->icon_size > 0.0f ? widget->icon_size : 20.0f;
    size = pt_min_float(size, pt_min_float(widget->width, widget->height));
    pt_color color = widget->has_foreground ? widget->foreground : text_color(widget);
    if (!widget->enabled) {
        color = pt_color_alpha(color, 140);
    }
    pt_paint_icon_glyph(canvas, widget->icon, widget->x + widget->width * 0.5f,
                        widget->y + widget->height * 0.5f, size, color);
}

static void paint_table(pt_canvas *canvas, pt_widget *table) {
    pt_table_state *state = table->extra;
    paint_input_chrome(canvas, table, shape_background(table, pt_theme_surface));
    if (!state) {
        return;
    }
    pt_rect inner = text_area(table);
    float header_h = state->column_count > 0 ? state->header_height : 0.0f;
    if (header_h > 0.0f) {
        pt_color raised = color_from_theme(table, pt_theme_surface_raised);
        pt_canvas_fill_rect(canvas, table->x + table->padding_left, inner.y, inner.w,
                            header_h, raised);
        pt_canvas_fill_rect(canvas, table->x + table->padding_left, inner.y + header_h - 1.0f,
                            inner.w, 1.0f,
                            pt_color_alpha(color_from_theme(table, pt_theme_border), 0xB0u));
    }
    pt_color divider = color_from_theme(table, pt_theme_border);
    pt_canvas_fill_rect(canvas, inner.x, inner.y + header_h,
                        pt_max_float(0.0f, inner.w), 1.0f,
                        pt_color_alpha(divider, 0x60u));
    paint_focus_ring(canvas, table);
}

static uint32_t table_header_sort_priority(const pt_widget *cell) {
    if (!cell || cell->kind != pt_widget_table_header_cell || !cell->parent ||
        !cell->parent->parent || cell->parent->parent->kind != pt_widget_table) {
        return 0;
    }
    const pt_widget *table = cell->parent->parent;
    const pt_table_state *state = table->extra;
    if (!state) {
        return 0;
    }
    for (uint32_t i = 0; i < state->sort_count; ++i) {
        if (state->sort_columns[i] == cell->column_index) {
            return i + 1;
        }
    }
    return 0;
}

static void paint_sort_priority_badge(pt_canvas *canvas, pt_widget *cell,
                                      uint32_t priority) {
    float cx = cell->x + cell->width - cell->padding_right - 24.0f;
    float cy = cell->y + cell->height * 0.5f;
    pt_canvas_fill_circle(canvas, cx, cy, 7.0f,
                          color_from_theme(cell, pt_theme_accent_soft));
    pt_layout_ctx ctx = pt_layout_context_for(cell);
    pt_font *font = pt_widget_font(cell, 9.0f, pt_font_weight_semibold, &ctx);
    if (!font) {
        return;
    }
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    char text[2] = {(char)('0' + (priority % 10)), '\0'};
    float width = pt_font_measure(font, text, 1) / scale;
    float baseline = cy - font->line_height / (2.0f * scale) + font->ascent / scale;
    pt_canvas_draw_text_line(canvas, font, text, 1, cx - width * 0.5f, baseline, width,
                             pt_text_align_start, color_from_theme(cell, pt_theme_accent),
                             false);
}

static void paint_table_header_cell(pt_canvas *canvas, pt_widget *cell) {
    float mix = pt_clamp_float(cell->hover_mix + cell->press_mix * 0.35f, 0.0f, 1.0f);
    if (mix > 0.01f) {
        pt_canvas_fill_rect(canvas, cell->x, cell->y, cell->width, cell->height,
                            pt_color_alpha(color_from_theme(cell, pt_theme_surface_hover),
                                           (uint8_t)(mix * 170.0f)));
    }
    pt_color border = color_from_theme(cell, pt_theme_border);
    pt_canvas_fill_rect(canvas, cell->x + cell->width - 1.0f, cell->y + 10.0f, 1.0f,
                        pt_max_float(0.0f, cell->height - 20.0f),
                        pt_color_alpha(border, 0x90u));
    uint32_t priority = table_header_sort_priority(cell);
    bool multi = priority > 0 && cell->parent && cell->parent->parent &&
                 cell->parent->parent->extra &&
                 ((const pt_table_state *)cell->parent->parent->extra)->sort_count > 1;
    float icon_space = cell->icon != pt_icon_none ? (multi ? 40.0f : 20.0f) : 0.0f;
    pt_rect area = {cell->x + cell->padding_left, cell->y,
                    pt_max_float(0.0f, cell->width - cell->padding_left -
                                           cell->padding_right - icon_space),
                    cell->height};
    paint_single_line(canvas, cell, cell->text ? cell->text : "", text_color(cell),
                      cell->text_align, true, 0.0f, area);
    if (cell->icon != pt_icon_none) {
        pt_color icon_color = color_from_theme(cell, pt_theme_accent);
        pt_paint_icon_glyph(canvas, cell->icon,
                            cell->x + cell->width - cell->padding_right - 7.0f,
                            cell->y + cell->height * 0.5f, 14.0f, icon_color);
    }
    if (multi) {
        paint_sort_priority_badge(canvas, cell, priority);
    }
}

static void paint_table_row(pt_canvas *canvas, pt_widget *row) {
    if (row->row_selected) {
        pt_canvas_fill_rect(canvas, row->x, row->y, row->width, row->height,
                            color_from_theme(row, pt_theme_accent_soft));
        pt_canvas_fill_rect(canvas, row->x, row->y, 2.5f, row->height,
                            color_from_theme(row, pt_theme_accent));
    } else if (row->hovered || row->press_mix > 0.01f) {
        float strength = pt_max_float(row->hover_mix, row->press_mix);
        pt_canvas_fill_rect(canvas, row->x, row->y, row->width, row->height,
                            pt_color_alpha(color_from_theme(row, pt_theme_surface_hover),
                                           (uint8_t)(strength * 180.0f)));
    } else if ((row->list_index & 1u) != 0u) {
        pt_canvas_fill_rect(canvas, row->x, row->y, row->width, row->height,
                            pt_color_alpha(color_from_theme(row, pt_theme_surface), 0x50u));
    }
    const pt_table_state *state = row->parent ? row->parent->extra : NULL;
    if (state && state->row_drag_active && state->dragged_row == (int32_t)row->list_index) {
        pt_canvas_stroke_rounded_rect(canvas, row->x + 1.0f, row->y + 1.0f,
                                      pt_max_float(0.0f, row->width - 2.0f),
                                      pt_max_float(0.0f, row->height - 2.0f), 6.0f, 2.0f,
                                      color_from_theme(row, pt_theme_accent));
    }
    pt_canvas_fill_rect(canvas, row->x, row->y + row->height - 1.0f, row->width, 1.0f,
                        pt_color_alpha(color_from_theme(row, pt_theme_border), 0x70u));
}

static void paint_table_cell(pt_canvas *canvas, pt_widget *cell) {
    if (cell->has_background) {
        pt_canvas_fill_rect(canvas, cell->x, cell->y, cell->width, cell->height,
                            cell->background);
    }
    bool has_icon = cell->icon != pt_icon_none;
    float icon_space = has_icon ? 22.0f : 0.0f;
    if (has_icon) {
        pt_color icon_color = cell->has_foreground ? cell->foreground : text_color(cell);
        pt_paint_icon_glyph(canvas, cell->icon, cell->x + cell->padding_left + 8.0f,
                            cell->y + cell->height * 0.5f, 16.0f, icon_color);
    }
    pt_rect area = {cell->x + cell->padding_left + icon_space, cell->y,
                    pt_max_float(0.0f, cell->width - cell->padding_left -
                                           cell->padding_right - icon_space),
                    cell->height};
    paint_single_line(canvas, cell, cell->text ? cell->text : "", text_color(cell),
                      cell->text_align, true, 0.0f, area);
}

static void paint_tree(pt_canvas *canvas, pt_widget *tree) {
    paint_input_chrome(canvas, tree, shape_background(tree, pt_theme_control));
    pt_tree_state *state = tree->extra;
    if (state) {
        paint_marquee(canvas, tree, &state->marquee);
    }
    paint_focus_ring(canvas, tree);
}

static void paint_tree_row(pt_canvas *canvas, pt_widget *row) {
    if (row->row_selected) {
        pt_canvas_fill_rounded_rect(canvas, row->x + 4.0f, row->y + 1.0f,
                                    pt_max_float(0.0f, row->width - 8.0f),
                                    pt_max_float(0.0f, row->height - 2.0f), 7.0f,
                                    color_from_theme(row, pt_theme_accent_soft));
        pt_canvas_fill_rect(canvas, row->x + 4.0f, row->y + 8.0f, 2.5f,
                            pt_max_float(0.0f, row->height - 16.0f),
                            color_from_theme(row, pt_theme_accent));
    } else if (row->hovered || row->press_mix > 0.01f) {
        float strength = pt_max_float(row->hover_mix, row->press_mix);
        pt_canvas_fill_rounded_rect(canvas, row->x + 4.0f, row->y + 1.0f,
                                    pt_max_float(0.0f, row->width - 8.0f),
                                    pt_max_float(0.0f, row->height - 2.0f), 7.0f,
                                    pt_color_alpha(color_from_theme(row,
                                                                    pt_theme_surface_hover),
                                                   (uint8_t)(strength * 180.0f)));
    }
    pt_tree_node *node = row->tree_node;
    if (!node) {
        return;
    }
    pt_tree_state *state = row->parent ? row->parent->extra : NULL;
    float indent = state && state->indent > 0.0f ? state->indent : 22.0f;
    float left = row->x + (float)node->depth * indent + 8.0f;
    if (node->first_child) {
        pt_icon chevron = node->expanded ? pt_icon_chevron_down : pt_icon_chevron_right;
        pt_paint_icon_glyph(canvas, chevron, left + 7.0f, row->y + row->height * 0.5f, 14.0f,
                            color_from_theme(row, pt_theme_text_muted));
    } else if (node->depth > 0) {
        pt_canvas_fill_circle(canvas, left + 6.0f, row->y + row->height * 0.5f, 1.8f,
                              color_from_theme(row, pt_theme_border_strong));
    }
    const char *text = node->text ? node->text : "";
    pt_rect area = {row->x + (float)node->depth * indent + 28.0f, row->y,
                    pt_max_float(0.0f, row->width - (float)node->depth * indent - 36.0f),
                    row->height};
    paint_single_line(canvas, row, text, text_color(row), pt_text_align_start, true, 0.0f,
                      area);
}

static void paint_tabs_strip(pt_canvas *canvas, pt_widget *tabs) {
    pt_tabs_state *state = tabs->extra;
    if (!state) {
        return;
    }
    pt_rect inner = text_area(tabs);
    float strip_h = state->strip_height;
    pt_color border = color_from_theme(tabs, pt_theme_border);
    pt_canvas_fill_rect(canvas, inner.x, inner.y + strip_h - 1.0f, inner.w, 1.0f,
                        pt_color_alpha(border, 0xA0u));
    pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, strip_h);
    for (uint32_t i = 0; i < state->count; ++i) {
        float tab_x = state->tab_x[i];
        float tab_w = state->tab_width[i];
        bool active = i == state->active;
        if (active) {
            pt_color pill = color_from_theme(tabs, pt_theme_surface_raised);
            pt_canvas_fill_rounded_rect(canvas, tab_x, inner.y + 6.0f, tab_w,
                                        pt_max_float(0.0f, strip_h - 12.0f), 9.0f, pill);
            pt_canvas_fill_rect(canvas, tab_x + 12.0f, inner.y + strip_h - 1.0f,
                                pt_max_float(0.0f, tab_w - 24.0f), 1.0f,
                                pt_color_alpha(color_from_theme(tabs, pt_theme_accent), 0x70u));
        }
        pt_layout_ctx ctx = pt_layout_context_for(tabs);
        float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
        pt_font *font = pt_font_acquire(ctx.font_family, pt_font_weight_medium, pt_font_slant_upright, 14.0f * scale);
        const char *title = state->titles[i] ? state->titles[i] : "";
        pt_color color = active ? color_from_theme(tabs, pt_theme_text)
                                : color_from_theme(tabs, pt_theme_text_muted);
        if (font && *title) {
            float line_height = font->line_height / scale;
            float baseline = inner.y + (strip_h - line_height) * 0.5f + font->ascent / scale;
            pt_canvas_draw_text_line(canvas, font, title, strlen(title), tab_x + 14.0f,
                                     baseline, pt_max_float(0.0f, tab_w - 28.0f),
                                     pt_text_align_center, color, true);
        }
    }
    if (state->count > 0 && state->indicator_width > 0.5f) {
        pt_canvas_fill_rounded_rect(canvas, state->indicator_x,
                                    inner.y + strip_h - 3.0f, state->indicator_width, 2.5f,
                                    1.25f, color_from_theme(tabs, pt_theme_accent));
    }
    pt_canvas_pop_clip(canvas);
    if (state->overflow) {
        float center_y = inner.y + strip_h * 0.5f;
        pt_color arrow = color_from_theme(tabs, pt_theme_text_muted);
        pt_paint_icon_glyph(canvas, pt_icon_chevron_left, inner.x + 15.0f, center_y, 16.0f,
                            arrow);
        pt_paint_icon_glyph(canvas, pt_icon_chevron_right, inner.x + inner.w - 15.0f,
                            center_y, 16.0f, arrow);
    }
}

static void paint_sidebar(pt_canvas *canvas, pt_widget *sidebar) {
    paint_input_chrome(canvas, sidebar, shape_background(sidebar, pt_theme_surface));
    pt_sidebar_state *state = sidebar->extra;
    if (!state || !state->header || state->header[0] == '\0') {
        return;
    }
    pt_rect inner = text_area(sidebar);
    pt_layout_ctx ctx = pt_layout_context_for(sidebar);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    pt_font *font = pt_font_acquire(ctx.font_family, pt_font_weight_semibold, pt_font_slant_upright, 12.0f * scale);
    if (!font) {
        return;
    }
    float line_height = font->line_height / scale;
    float baseline = inner.y + 24.0f + line_height * 0.5f + font->ascent / scale;
    pt_canvas_draw_text_line(canvas, font, state->header, strlen(state->header),
                             inner.x + 16.0f, baseline,
                             pt_max_float(0.0f, inner.w - 32.0f), pt_text_align_start,
                             color_from_theme(sidebar, pt_theme_text_muted), true);
}

static void paint_sidebar_item(pt_canvas *canvas, pt_widget *item) {
    pt_sidebar_state *state = item->parent ? item->parent->extra : NULL;
    bool compact = state && state->compact;
    if (item->row_selected) {
        pt_canvas_fill_rounded_rect(canvas, item->x, item->y, item->width, item->height,
                                    shape_radius(item),
                                    color_from_theme(item, pt_theme_accent_soft));
        pt_canvas_fill_rounded_rect(canvas, item->x + 1.0f, item->y + 9.0f, 2.5f,
                                    pt_max_float(0.0f, item->height - 18.0f), 1.25f,
                                    color_from_theme(item, pt_theme_accent));
    } else if (item->hovered || item->press_mix > 0.01f) {
        float strength = pt_max_float(item->hover_mix, item->press_mix);
        pt_canvas_fill_rounded_rect(canvas, item->x, item->y, item->width, item->height,
                                    shape_radius(item),
                                    pt_color_alpha(color_from_theme(item,
                                                                    pt_theme_surface_hover),
                                                   (uint8_t)(strength * 180.0f)));
    }
    float icon_size = 20.0f;
    float icon_x = compact ? item->x + item->width * 0.5f
                           : item->x + item->padding_left + 10.0f;
    pt_color icon_color = item->row_selected ? color_from_theme(item, pt_theme_accent)
                                             : color_from_theme(item, pt_theme_text_muted);
    if (!item->enabled) {
        icon_color = pt_color_alpha(icon_color, 140);
    }
    pt_paint_icon_glyph(canvas, item->icon, icon_x, item->y + item->height * 0.5f,
                        icon_size, icon_color);
    if (!compact) {
        pt_rect area = {item->x + item->padding_left + 32.0f, item->y,
                        pt_max_float(0.0f, item->width - item->padding_left -
                                               item->padding_right - 32.0f),
                        item->height};
        pt_color color = item->row_selected ? color_from_theme(item, pt_theme_text)
                                            : text_color(item);
        paint_single_line(canvas, item, item->text ? item->text : "", color,
                          pt_text_align_start, true, 0.0f, area);
    }
}

static float chip_remove_region(const pt_widget *chip) {
    return chip->chip_removable ? 26.0f : 0.0f;
}

static void paint_chip(pt_canvas *canvas, pt_widget *chip) {
    float radius = pt_min_float(metric_from_theme(chip, pt_theme_metric_radius_pill),
                                pt_min_float(chip->width, chip->height) * 0.5f);
    pt_color background = chip->has_background
                              ? chip->background
                              : color_from_theme(chip, pt_theme_control);
    if (!chip->has_background) {
        if (chip->chip_selected) {
            background = color_from_theme(chip, pt_theme_accent_soft);
        }
        if (chip->press_mix > 0.01f) {
            background = pt_color_blend(
                background, color_from_theme(chip, pt_theme_control_pressed),
                chip->press_mix);
        } else if (chip->hover_mix > 0.01f) {
            background = pt_color_blend(
                background, color_from_theme(chip, pt_theme_control_hover),
                chip->hover_mix * 0.8f);
        }
    }
    pt_canvas_fill_rounded_rect(canvas, chip->x, chip->y, chip->width, chip->height, radius,
                                background);
    pt_color outline = chip->has_border_color
                           ? chip->border_color
                           : chip->chip_selected
                                 ? pt_color_alpha(color_from_theme(chip, pt_theme_accent), 0xB0u)
                                 : color_from_theme(chip, pt_theme_outline_variant);
    pt_canvas_stroke_rounded_rect(canvas, chip->x + 0.5f, chip->y + 0.5f,
                                  pt_max_float(0.0f, chip->width - 1.0f),
                                  pt_max_float(0.0f, chip->height - 1.0f),
                                  pt_max_float(0.0f, radius - 0.5f),
                                  chip->has_border_width ? chip->border_width : 1.0f,
                                  outline);
    pt_color foreground = chip->has_foreground ? chip->foreground : text_color(chip);
    float left = chip->x + chip->padding_left;
    if (chip->icon != pt_icon_none) {
        pt_color icon_color = chip->chip_selected
                                  ? color_from_theme(chip, pt_theme_accent)
                                  : color_from_theme(chip, pt_theme_text_muted);
        if (!chip->enabled) {
            icon_color = color_from_theme(chip, pt_theme_text_disabled);
        }
        pt_paint_icon_glyph(canvas, chip->icon, left + 7.0f,
                            chip->y + chip->height * 0.5f, 14.0f, icon_color);
        left += 20.0f;
    }
    float right = chip_remove_region(chip) + chip->padding_right;
    pt_rect area = {left, chip->y,
                    pt_max_float(0.0f, chip->x + chip->width - right - left), chip->height};
    paint_single_line(canvas, chip, chip->text ? chip->text : "", foreground,
                      pt_text_align_start, true, 0.0f, area);
    if (chip->chip_removable) {
        pt_color close = color_from_theme(chip, pt_theme_text_muted);
        if (!chip->enabled) {
            close = color_from_theme(chip, pt_theme_text_disabled);
        }
        pt_paint_icon_glyph(canvas, pt_icon_close,
                            chip->x + chip->width - 13.0f,
                            chip->y + chip->height * 0.5f, 12.0f, close);
    }
    paint_focus_ring(canvas, chip);
}

static void paint_skeleton(pt_canvas *canvas, pt_widget *skeleton) {
    float radius = pt_min_float(metric_from_theme(skeleton, pt_theme_metric_radius_medium),
                                pt_min_float(skeleton->width, skeleton->height) * 0.5f);
    pt_canvas_fill_rounded_rect(canvas, skeleton->x, skeleton->y, skeleton->width,
                                skeleton->height, radius,
                                color_from_theme(skeleton, pt_theme_skeleton));
    if (skeleton->width < 8.0f || skeleton->height < 4.0f) {
        return;
    }
    float band = pt_max_float(24.0f, skeleton->width * 0.38f);
    float travel = skeleton->width + band * 2.0f;
    float phase = skeleton->phase - floorf(skeleton->phase);
    float x = skeleton->x - band + phase * travel;
    pt_canvas_push_clip(canvas, skeleton->x, skeleton->y, skeleton->width,
                        skeleton->height);
    pt_canvas_fill_rounded_rect(canvas, x, skeleton->y, band, skeleton->height, radius,
                                pt_color_alpha(color_from_theme(skeleton,
                                                                pt_theme_skeleton_highlight),
                                               0xB4u));
    pt_canvas_pop_clip(canvas);
}

static void paint_busy_dots(pt_canvas *canvas, pt_widget *widget) {
    pt_color accent = color_from_theme(widget, pt_theme_accent);
    float radius = pt_min_float(2.2f, pt_min_float(widget->width, widget->height) * 0.12f);
    float cx = widget->x + widget->width - 12.0f;
    float cy = widget->y + widget->height * 0.5f;
    for (int i = 0; i < 3; ++i) {
        float wave = sinf((widget->phase + (float)i * 0.22f) * 6.2831853f);
        float alpha = 0.35f + 0.65f * pt_clamp_float(wave * 0.5f + 0.5f, 0.0f, 1.0f);
        pt_canvas_fill_circle(canvas, cx + (float)(i - 1) * (radius * 2.6f), cy, radius,
                              pt_color_alpha(accent, (uint8_t)(alpha * 255.0f)));
    }
}

static void paint_state_decorations(pt_canvas *canvas, pt_widget *widget) {
    if (!widget->visible) {
        return;
    }
    if (widget->invalid) {
        float width = pt_max_float(1.5f,
                                   metric_from_theme(widget, pt_theme_metric_focus_width) *
                                       0.75f);
        pt_canvas_stroke_rounded_rect(canvas, widget->x + 0.5f, widget->y + 0.5f,
                                      pt_max_float(0.0f, widget->width - 1.0f),
                                      pt_max_float(0.0f, widget->height - 1.0f),
                                      shape_radius(widget), width,
                                      color_from_theme(widget, pt_theme_danger));
    }
    if (widget->busy && widget->width >= 28.0f) {
        paint_busy_dots(canvas, widget);
    }
}

static void paint_table_overlays(pt_canvas *canvas, pt_widget *table) {
    pt_table_state *state = table->extra;
    if (!state) {
        return;
    }
    paint_marquee(canvas, table, &state->marquee);
    if (state->reorder_active && state->reorder_column >= 0 && state->header) {
        float header_top = table->y + table->padding_top;
        float x = table->x + table->padding_left - table->scroll_x;
        for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
            if (cell->kind != pt_widget_table_header_cell || !cell->visible) {
                continue;
            }
            if (cell->column_index == (uint32_t)state->reorder_target) {
                x = cell->x;
                break;
            }
            x = cell->x + cell->width;
        }
        pt_canvas_fill_rect(canvas, x - 1.0f, header_top, 2.0f, state->header_height,
                            color_from_theme(table, pt_theme_accent));
        for (pt_widget *cell = state->header->first_child; cell; cell = cell->next_sibling) {
            if (cell->kind == pt_widget_table_header_cell &&
                cell->column_index == (uint32_t)state->reorder_column) {
                pt_canvas_fill_rect(canvas, cell->x, cell->y, cell->width, cell->height,
                                    pt_color_alpha(color_from_theme(table, pt_theme_accent),
                                                   0x1Cu));
                break;
            }
        }
    }
}

static bool intersects_clip(const pt_widget *widget, const pt_canvas *canvas) {
    float scale = canvas->scale > 0.0f ? canvas->scale : 1.0f;
    float margin = 48.0f * scale;
    float x = widget->x * scale;
    float y = widget->y * scale;
    float w = widget->width * scale;
    float h = widget->height * scale;
    return x + w + margin >= canvas->clip.x && y + h + margin >= canvas->clip.y &&
           x - margin <= canvas->clip.x + canvas->clip.w &&
           y - margin <= canvas->clip.y + canvas->clip.h;
}

static void paint_widget_internal(pt_widget *widget, pt_canvas *canvas,
                                  const pt_layout_ctx *ctx);

static void paint_widget_contents(pt_widget *widget, pt_canvas *canvas,
                                  const pt_layout_ctx *ctx) {
    (void)ctx;
    if (!widget->visible || !intersects_clip(widget, canvas)) {
        return;
    }
    ++canvas->visit_count;
    switch (widget->kind) {
    case pt_widget_box:
    case pt_widget_wrap:
    case pt_widget_grid:
        if (widget->has_background) {
            pt_canvas_fill_rounded_rect(canvas, widget->x, widget->y, widget->width,
                                        widget->height, shape_radius(widget),
                                        widget->background);
            if (widget->has_border_width || widget->has_border_color) {
                pt_color border = widget->has_border_color
                                      ? widget->border_color
                                      : color_from_theme(widget, pt_theme_border);
                paint_outline(canvas, widget, border,
                              widget->has_border_width ? widget->border_width : 1.0f);
            }
        }
        break;
    case pt_widget_card:
        paint_card(canvas, widget);
        break;
    case pt_widget_label:
        paint_label(canvas, widget);
        break;
    case pt_widget_button:
        paint_button(canvas, widget);
        break;
    case pt_widget_entry:
        paint_entry(canvas, widget);
        break;
    case pt_widget_text_area:
        paint_text_area(canvas, widget);
        break;
    case pt_widget_list:
        paint_list(canvas, widget);
        break;
    case pt_widget_list_row:
        paint_list_row(canvas, widget);
        break;
    case pt_widget_combobox:
        paint_combobox(canvas, widget);
        break;
    case pt_widget_toggle:
        paint_toggle(canvas, widget);
        break;
    case pt_widget_slider:
        paint_slider(canvas, widget);
        break;
    case pt_widget_checkbox:
        paint_checkbox(canvas, widget);
        break;
    case pt_widget_radio:
        paint_radio(canvas, widget);
        break;
    case pt_widget_progress:
        paint_progress(canvas, widget);
        break;
    case pt_widget_spinner:
        paint_spinner(canvas, widget);
        break;
    case pt_widget_segmented:
        paint_segmented(canvas, widget);
        break;
    case pt_widget_divider:
        paint_divider(canvas, widget);
        break;
    case pt_widget_icon:
        paint_icon_widget(canvas, widget);
        break;
    case pt_widget_table:
        paint_table(canvas, widget);
        break;
    case pt_widget_table_header_cell:
        paint_table_header_cell(canvas, widget);
        break;
    case pt_widget_table_row:
        paint_table_row(canvas, widget);
        break;
    case pt_widget_table_cell:
        paint_table_cell(canvas, widget);
        break;
    case pt_widget_tree:
        paint_tree(canvas, widget);
        break;
    case pt_widget_tree_row:
        paint_tree_row(canvas, widget);
        break;
    case pt_widget_tabs:
        paint_tabs_strip(canvas, widget);
        break;
    case pt_widget_sidebar:
        paint_sidebar(canvas, widget);
        break;
    case pt_widget_sidebar_item:
        paint_sidebar_item(canvas, widget);
        break;
    case pt_widget_chip:
        paint_chip(canvas, widget);
        break;
    case pt_widget_skeleton:
        paint_skeleton(canvas, widget);
        break;
    case pt_widget_scroll:
    case pt_widget_spacer:
    case pt_widget_table_header:
    case pt_widget_tab_page:
    default:
        break;
    }

    if (widget->kind == pt_widget_scroll) {
        pt_canvas_push_clip(canvas, widget->x + widget->padding_left,
                            widget->y + widget->padding_top,
                            pt_max_float(0.0f, widget->width - widget->padding_left -
                                                   widget->padding_right - 10.0f),
                            pt_max_float(0.0f, widget->height - widget->padding_top -
                                                   widget->padding_bottom));
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_scroll_bar(canvas, widget);
        paint_drop_highlight(canvas, widget);
        return;
    }
    if (widget->kind == pt_widget_list) {
        pt_rect inner = text_area(widget);
        pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, inner.h);
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_vertical_scrollbar(canvas, widget, widget->x + widget->width - 8.0f, inner.y,
                                 inner.h, inner.h, widget->scroll_content_height,
                                 widget->scroll_y);
        paint_drop_highlight(canvas, widget);
        return;
    }
    if (widget->kind == pt_widget_table) {
        pt_table_state *state = widget->extra;
        pt_rect inner = text_area(widget);
        pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, inner.h);
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_table_overlays(canvas, widget);
        if (state) {
            float header_h = state->column_count > 0 ? state->header_height : 0.0f;
            float rows_h = pt_max_float(0.0f, inner.h - header_h);
            paint_vertical_scrollbar(canvas, widget, widget->x + widget->width - 8.0f,
                                     inner.y + header_h, rows_h, rows_h,
                                     (float)widget->row_count *
                                         pt_max_float(20.0f, widget->row_height),
                                     widget->scroll_y);
            float content_w = table_columns_width(widget);
            paint_horizontal_scrollbar(canvas, widget, widget->x + widget->padding_left,
                                       widget->y + widget->height - widget->padding_bottom -
                                           6.0f,
                                       pt_max_float(0.0f, inner.w - 6.0f), inner.w,
                                       content_w, widget->scroll_x);
        }
        paint_drop_highlight(canvas, widget);
        return;
    }
    if (widget->kind == pt_widget_tree) {
        pt_rect inner = text_area(widget);
        pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, inner.h);
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_vertical_scrollbar(canvas, widget, widget->x + widget->width - 8.0f, inner.y,
                                 inner.h, inner.h, widget->scroll_content_height,
                                 widget->scroll_y);
        paint_drop_highlight(canvas, widget);
        return;
    }
    if (widget->kind == pt_widget_tabs) {
        pt_rect inner = text_area(widget);
        pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, inner.h);
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_drop_highlight(canvas, widget);
        return;
    }
    if (widget->kind == pt_widget_sidebar) {
        pt_rect inner = text_area(widget);
        pt_canvas_push_clip(canvas, inner.x, inner.y, inner.w, inner.h);
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_vertical_scrollbar(canvas, widget, widget->x + widget->width - 8.0f, inner.y,
                                 inner.h, inner.h, widget->scroll_content_height,
                                 widget->scroll_y);
        paint_drop_highlight(canvas, widget);
        return;
    }
    if (widget->kind == pt_widget_tab_page) {
        pt_canvas_push_clip(canvas, widget->x, widget->y, widget->width, widget->height);
        for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
            paint_widget_internal(child, canvas, ctx);
        }
        pt_canvas_pop_clip(canvas);
        paint_drop_highlight(canvas, widget);
        return;
    }
    for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        paint_widget_internal(child, canvas, ctx);
    }
    paint_drop_highlight(canvas, widget);
}

static void paint_widget_internal(pt_widget *widget, pt_canvas *canvas,
                                  const pt_layout_ctx *ctx) {
    paint_widget_contents(widget, canvas, ctx);
    paint_state_decorations(canvas, widget);
}

void pt_widget_paint(pt_widget *widget, pt_canvas *canvas, const pt_layout_ctx *ctx) {
    if (!widget || !canvas) {
        return;
    }
    pt_layout_ctx local = ctx ? *ctx : pt_layout_context_for(widget);
    paint_widget_internal(widget, canvas, &local);
}
