/*
 * Pt multi-line text layout for the text area widget.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct text_line_builder {
    pt_text_line *lines;
    size_t count;
    size_t capacity;
} text_line_builder;

static void line_push(text_line_builder *builder, size_t start, size_t end) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 8;
        builder->lines = pt_realloc_mem(builder->lines, builder->capacity * sizeof(*builder->lines));
    }
    builder->lines[builder->count].start = start;
    builder->lines[builder->count].end = end;
    ++builder->count;
}

static float measure_prefix(const pt_font *font, const char *text, size_t len, size_t position,
                            float scale) {
    if (position > len) {
        position = len;
    }
    return pt_font_measure(font, text, position) / scale;
}

static void layout_paragraph(const pt_font *font, const char *text, float scale, float max_width,
                             size_t start, size_t end, text_line_builder *builder) {
    if (start >= end) {
        line_push(builder, start, start);
        return;
    }
    max_width = pt_max_float(1.0f, max_width);
    size_t position = start;
    while (position < end) {
        if (pt_font_measure(font, text + position, end - position) / scale <= max_width) {
            line_push(builder, position, end);
            return;
        }
        size_t fit = position;
        size_t space_after = 0;
        size_t scan = position;
        while (scan < end) {
            size_t next = pt_utf8_next_boundary(text, end, scan);
            if (next <= scan) {
                next = scan + 1;
            }
            float width = pt_font_measure(font, text + position, next - position) / scale;
            if (width > max_width && fit > position) {
                break;
            }
            fit = next;
            if (text[next - 1] == ' ' || text[next - 1] == '\t') {
                space_after = next;
            }
            scan = next;
        }
        if (fit <= position) {
            fit = pt_utf8_next_boundary(text, end, position);
            if (fit <= position) {
                fit = position + 1;
            }
        }
        size_t line_end = fit;
        if (space_after > position && space_after < fit) {
            line_end = space_after;
        }
        if (line_end <= position) {
            line_end = fit;
        }
        line_push(builder, position, line_end);
        position = line_end;
    }
}

bool pt_text_layout_build(const pt_widget *area, const pt_layout_ctx *ctx, float width,
                          pt_text_layout *out) {
    if (!area || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    pt_font *font = pt_widget_font_for(area, ctx);
    if (!font) {
        return false;
    }
    float scale = ctx && ctx->scale > 0.0f ? ctx->scale : 1.0f;
    const char *text = area->text ? area->text : "";
    size_t len = strlen(text);
    out->font = font;
    out->scale = scale;
    out->line_height = font->line_height / scale;
    out->display = pt_alloc(len + 1);
    memcpy(out->display, text, len + 1);
    out->display_len = len;
    out->caret = area->cursor <= len ? area->cursor : len;
    if (area->span_count > 0 && pt_widget_supports_spans(area)) {
        if (pt_text_runs_build(area, ctx, &out->runs)) {
            out->runs.text = out->display;
            out->runs.length = len;
            out->has_runs = true;
            if (out->runs.line_height > out->line_height) {
                out->line_height = out->runs.line_height;
            }
        }
    }

    text_line_builder builder = {0};
    size_t paragraph_start = 0;
    for (;;) {
        size_t paragraph_end = paragraph_start;
        while (paragraph_end < len && text[paragraph_end] != '\n') {
            ++paragraph_end;
        }
        size_t content_end = paragraph_end;
        if (content_end > paragraph_start && text[content_end - 1] == '\r') {
            --content_end;
        }
        layout_paragraph(font, text, scale, width, paragraph_start, content_end, &builder);
        if (paragraph_end >= len) {
            break;
        }
        paragraph_start = paragraph_end + 1;
        if (paragraph_start == len) {
            line_push(&builder, len, len);
            break;
        }
    }
    if (builder.count == 0) {
        line_push(&builder, 0, 0);
    }
    out->lines = builder.lines;
    out->line_count = (int)builder.count;
    return true;
}

void pt_text_layout_free(pt_text_layout *layout) {
    if (!layout) {
        return;
    }
    if (layout->has_runs) {
        pt_text_runs_free(&layout->runs);
    }
    free(layout->display);
    free(layout->lines);
    memset(layout, 0, sizeof(*layout));
}

int pt_text_layout_line_for_position(const pt_text_layout *layout, size_t position) {
    if (!layout || layout->line_count <= 0) {
        return 0;
    }
    if (position > layout->display_len) {
        position = layout->display_len;
    }
    for (int i = 0; i < layout->line_count; ++i) {
        if (position >= layout->lines[i].start && position <= layout->lines[i].end) {
            return i;
        }
        if (position < layout->lines[i].start) {
            return i > 0 ? i - 1 : 0;
        }
    }
    return layout->line_count - 1;
}

float pt_text_layout_x_for_position(const pt_text_layout *layout, int line, size_t position) {
    if (!layout || layout->line_count <= 0) {
        return 0.0f;
    }
    if (line < 0) {
        line = 0;
    }
    if (line >= layout->line_count) {
        line = layout->line_count - 1;
    }
    pt_text_line span = layout->lines[line];
    if (position < span.start) {
        position = span.start;
    }
    if (position > span.end) {
        position = span.end;
    }
    if (layout->has_runs) {
        return pt_text_runs_measure(&layout->runs, span.start, position);
    }
    return measure_prefix(layout->font, layout->display + span.start, span.end - span.start,
                          position - span.start, layout->scale);
}

size_t pt_text_layout_position_for_x(const pt_text_layout *layout, int line, float local_x) {
    if (!layout || layout->line_count <= 0) {
        return 0;
    }
    if (line < 0) {
        line = 0;
    }
    if (line >= layout->line_count) {
        line = layout->line_count - 1;
    }
    pt_text_line span = layout->lines[line];
    if (local_x <= 0.0f) {
        return span.start;
    }
    if (layout->has_runs) {
        return pt_text_runs_position_for_x(&layout->runs, span.start, span.end, local_x);
    }
    const char *text = layout->display + span.start;
    size_t len = span.end - span.start;
    float best_distance = FLT_MAX;
    size_t best = span.start;
    size_t scan = 0;
    for (;;) {
        float width = measure_prefix(layout->font, text, len, scan, layout->scale);
        float distance = fabsf(width - local_x);
        if (distance < best_distance) {
            best_distance = distance;
            best = span.start + scan;
        }
        if (scan >= len) {
            break;
        }
        size_t next = pt_utf8_next_boundary(text, len, scan);
        if (next <= scan) {
            next = scan + 1;
        }
        scan = next;
    }
    return best;
}

float pt_text_area_measure_height(const pt_widget *area, const pt_layout_ctx *ctx, float width) {
    pt_text_layout layout = {0};
    if (!pt_text_layout_build(area, ctx, width, &layout)) {
        return 0.0f;
    }
    float height = (float)layout.line_count * layout.line_height;
    pt_text_layout_free(&layout);
    return height;
}

float pt_text_area_content_width(const pt_widget *area) {
    return pt_max_float(1.0f, area->width - area->padding_left - area->padding_right);
}

void pt_text_area_ensure_caret_visible(pt_widget *area, const pt_layout_ctx *ctx) {
    if (!area) {
        return;
    }
    pt_text_layout layout = {0};
    if (!pt_text_layout_build(area, ctx, pt_text_area_content_width(area), &layout)) {
        return;
    }
    float viewport = pt_max_float(1.0f, area->height - area->padding_top -
                                            area->padding_bottom);
    int line = pt_text_layout_line_for_position(&layout, layout.caret);
    float top = (float)line * layout.line_height;
    if (top < area->scroll_y) {
        area->scroll_y = top;
    } else if (top + layout.line_height > area->scroll_y + viewport) {
        area->scroll_y = top + layout.line_height - viewport;
    }
    float max_scroll = pt_max_float(0.0f, (float)layout.line_count * layout.line_height -
                                              viewport);
    area->scroll_y = pt_clamp_float(area->scroll_y, 0.0f, max_scroll);
    pt_text_layout_free(&layout);
}

void pt_text_area_caret_box(const pt_widget *area, const pt_layout_ctx *ctx, int *x, int *y,
                            int *w, int *h) {
    int out_x = 0;
    int out_y = 0;
    int out_w = 2;
    int out_h = 18;
    pt_text_layout layout = {0};
    if (area && pt_text_layout_build(area, ctx, pt_text_area_content_width(area), &layout)) {
        int line = pt_text_layout_line_for_position(&layout, layout.caret);
        float caret_x = pt_text_layout_x_for_position(&layout, line, layout.caret);
        if (area->preedit && *area->preedit) {
            size_t preedit_caret = pt_widget_preedit_caret(area);
            caret_x += pt_font_measure(layout.font, area->preedit, preedit_caret) /
                       layout.scale;
        }
        out_x = (int)lroundf(area->x + area->padding_left + caret_x);
        out_y = (int)lroundf(area->y + area->padding_top +
                             (float)line * layout.line_height - area->scroll_y);
        out_w = 2;
        out_h = (int)lroundf(layout.line_height);
        pt_text_layout_free(&layout);
    }
    if (x) {
        *x = out_x;
    }
    if (y) {
        *y = out_y;
    }
    if (w) {
        *w = out_w;
    }
    if (h) {
        *h = out_h;
    }
}

size_t pt_text_area_position_at(const pt_widget *area, const pt_layout_ctx *ctx, float x,
                                float y) {
    if (!area) {
        return 0;
    }
    pt_text_layout layout = {0};
    if (!pt_text_layout_build(area, ctx, pt_text_area_content_width(area), &layout)) {
        return 0;
    }
    float local_x = x - area->x - area->padding_left;
    float local_y = y - area->y - area->padding_top + area->scroll_y;
    int line = 0;
    if (layout.line_height > 0.0f) {
        line = (int)floorf(local_y / layout.line_height);
    }
    if (line < 0) {
        line = 0;
    }
    if (line >= layout.line_count) {
        line = layout.line_count - 1;
    }
    size_t position = pt_text_layout_position_for_x(&layout, line, local_x);
    pt_text_layout_free(&layout);
    return position;
}
