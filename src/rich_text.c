/*
 * Pt styled rich-text spans, runs, measurement and painting helpers.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define pt_run_underline pt_text_span_underline
#define pt_run_has_color pt_text_span_has_color

typedef struct rich_lines {
    pt_text_line *items;
    size_t count;
    size_t capacity;
} rich_lines;

static void rich_lines_push(rich_lines *builder, size_t start, size_t end) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 8;
        builder->items = pt_realloc_mem(builder->items,
                                        builder->capacity * sizeof(*builder->items));
    }
    builder->items[builder->count].start = start;
    builder->items[builder->count].end = end;
    ++builder->count;
}

bool pt_widget_supports_spans(const pt_widget *widget) {
    return widget && (widget->kind == pt_widget_label || widget->kind == pt_widget_text_area);
}

static size_t rich_snap_start(const char *text, size_t len, size_t position) {
    if (position > len) {
        position = len;
    }
    while (position > 0 && position < len &&
           ((unsigned char)text[position] & 0xC0u) == 0x80u) {
        --position;
    }
    return position;
}

static size_t rich_snap_end(const char *text, size_t len, size_t position) {
    if (position > len) {
        position = len;
    }
    while (position < len && ((unsigned char)text[position] & 0xC0u) == 0x80u) {
        ++position;
    }
    return position;
}

bool pt_widget_set_text_spans(pt_widget *widget, const pt_text_span *spans, uint32_t count) {
    if (!pt_widget_supports_spans(widget)) {
        return false;
    }
    if (!spans || count == 0) {
        pt_widget_clear_text_spans(widget);
        return true;
    }
    const char *text = widget->text ? widget->text : "";
    size_t len = strlen(text);
    pt_text_span *copy = pt_alloc_zeroed(count, sizeof(*copy));
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        size_t start = rich_snap_start(text, len, spans[i].start);
        size_t end = start + spans[i].length;
        if (end < start) {
            end = len;
        }
        if (end > len) {
            end = len;
        }
        end = rich_snap_end(text, len, end);
        if (end <= start) {
            continue;
        }
        copy[kept] = spans[i];
        copy[kept].start = (uint32_t)start;
        copy[kept].length = (uint32_t)(end - start);
        if (copy[kept].slant < pt_font_slant_upright ||
            copy[kept].slant > pt_font_slant_oblique) {
            copy[kept].slant = pt_font_slant_upright;
        }
        ++kept;
    }
    if (kept == 0) {
        free(copy);
        copy = NULL;
    }
    free(widget->spans);
    widget->spans = copy;
    widget->span_count = kept;
    pt_widget_mark_dirty(widget, true);
    return true;
}

void pt_widget_clear_text_spans(pt_widget *widget) {
    if (!widget || (!widget->spans && widget->span_count == 0)) {
        return;
    }
    free(widget->spans);
    widget->spans = NULL;
    widget->span_count = 0;
    pt_widget_mark_dirty(widget, true);
}

uint32_t pt_widget_get_text_span_count(const pt_widget *widget) {
    return widget ? widget->span_count : 0;
}

bool pt_widget_get_text_span(const pt_widget *widget, uint32_t index, pt_text_span *span) {
    if (!widget || !span || index >= widget->span_count) {
        return false;
    }
    *span = widget->spans[index];
    return true;
}

static void rich_spans_clamp_to(pt_widget *widget, size_t len) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < widget->span_count; ++i) {
        pt_text_span span = widget->spans[i];
        size_t start = span.start > len ? len : span.start;
        size_t end = start + span.length;
        if (end < start || end > len) {
            end = len;
        }
        if (end <= start) {
            continue;
        }
        span.start = (uint32_t)start;
        span.length = (uint32_t)(end - start);
        widget->spans[kept++] = span;
    }
    widget->span_count = kept;
}

void pt_widget_spans_clamp(pt_widget *widget) {
    if (!widget || widget->span_count == 0) {
        return;
    }
    size_t len = widget->text ? strlen(widget->text) : 0;
    rich_spans_clamp_to(widget, len);
}

void pt_widget_spans_on_insert(pt_widget *widget, size_t position, size_t length) {
    if (!widget || widget->span_count == 0 || length == 0) {
        return;
    }
    size_t text_len = widget->text ? strlen(widget->text) : 0;
    if (position > text_len) {
        position = text_len;
    }
    for (uint32_t i = 0; i < widget->span_count; ++i) {
        pt_text_span *span = &widget->spans[i];
        size_t start = span->start;
        size_t end = start + span->length;
        if (position <= start) {
            size_t next = start + length;
            span->start = (uint32_t)next;
        } else if (position <= end) {
            size_t next = span->length + length;
            span->length = (uint32_t)next;
        }
    }
}

void pt_widget_spans_on_delete(pt_widget *widget, size_t start, size_t end) {
    if (!widget || widget->span_count == 0 || start >= end) {
        return;
    }
    size_t removed = end - start;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < widget->span_count; ++i) {
        pt_text_span span = widget->spans[i];
        size_t span_start = span.start;
        size_t span_end = span_start + span.length;
        if (span_end <= start) {
            widget->spans[kept++] = span;
        } else if (span_start >= end) {
            span.start = (uint32_t)(span_start - removed);
            widget->spans[kept++] = span;
        } else if (span_start < start && span_end > end) {
            span.length = (uint32_t)(span.length - removed);
            widget->spans[kept++] = span;
        } else if (span_start < start) {
            span.length = (uint32_t)(start - span_start);
            if (span.length > 0) {
                widget->spans[kept++] = span;
            }
        } else if (span_end > end) {
            size_t tail = span_end - end;
            span.start = (uint32_t)start;
            span.length = (uint32_t)tail;
            widget->spans[kept++] = span;
        }
    }
    widget->span_count = kept;
}

static int rich_compare_positions(const void *left, const void *right) {
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    return (a > b) - (a < b);
}

static const pt_text_span *rich_span_for(const pt_widget *widget, size_t position) {
    const pt_text_span *winner = NULL;
    for (uint32_t i = 0; i < widget->span_count; ++i) {
        const pt_text_span *span = &widget->spans[i];
        size_t start = span->start;
        size_t end = start + span->length;
        if (position >= start && position < end) {
            winner = span;
        }
    }
    return winner;
}

static void rich_run_append(pt_text_runs *runs, size_t start, size_t end, pt_font *font,
                            pt_color color, uint32_t flags) {
    if (runs->count > 0) {
        pt_text_run *last = &runs->items[runs->count - 1];
        if (last->end == start && last->font == font && last->color == color &&
            last->flags == flags) {
            last->end = end;
            return;
        }
    }
    pt_text_run *run = &runs->items[runs->count++];
    run->start = start;
    run->end = end;
    run->font = font;
    run->color = color;
    run->flags = flags;
}

bool pt_text_runs_build(const pt_widget *widget, const pt_layout_ctx *ctx, pt_text_runs *out) {
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!widget) {
        return false;
    }
    pt_font *base = pt_widget_font_for(widget, ctx);
    if (!base) {
        return false;
    }
    const char *text = widget->text ? widget->text : "";
    size_t len = strlen(text);
    float scale = ctx && ctx->scale > 0.0f ? ctx->scale : 1.0f;
    size_t max_runs = (size_t)widget->span_count * 2 + 2;
    out->items = pt_alloc_zeroed(max_runs, sizeof(*out->items));
    out->text = text;
    out->length = len;
    out->scale = scale;
    out->ascent = base->ascent / scale;
    out->descent = base->descent / scale;
    out->line_height = base->line_height / scale;

    size_t *boundaries = pt_alloc(max_runs * sizeof(*boundaries));
    size_t boundary_count = 0;
    boundaries[boundary_count++] = 0;
    boundaries[boundary_count++] = len;
    for (uint32_t i = 0; i < widget->span_count; ++i) {
        const pt_text_span *span = &widget->spans[i];
        size_t start = span->start < len ? span->start : len;
        size_t end = start + span->length;
        if (end < start) {
            end = len;
        }
        if (end > len) {
            end = len;
        }
        boundaries[boundary_count++] = start;
        boundaries[boundary_count++] = end;
    }
    qsort(boundaries, boundary_count, sizeof(*boundaries), rich_compare_positions);
    size_t unique = 0;
    for (size_t i = 0; i < boundary_count; ++i) {
        if (unique == 0 || boundaries[i] != boundaries[unique - 1]) {
            boundaries[unique++] = boundaries[i];
        }
    }

    pt_font_weight base_weight = (pt_font_weight)pt_widget_font_weight(widget);
    for (size_t i = 0; i + 1 < unique; ++i) {
        size_t start = boundaries[i];
        size_t end = boundaries[i + 1];
        if (start >= end) {
            continue;
        }
        const pt_text_span *span = rich_span_for(widget, start);
        pt_font_weight weight = base_weight;
        pt_font_slant slant = pt_font_slant_upright;
        pt_color color = 0;
        uint32_t flags = 0;
        float logical_size = pt_widget_font_size(widget);
        const char *family = NULL;
        if (span) {
            flags = span->flags & (pt_run_underline | pt_run_has_color);
            if (span->flags & pt_text_span_has_weight) {
                weight = span->weight;
            }
            if (span->flags & pt_text_span_has_slant) {
                slant = span->slant;
            }
            if (span->flags & pt_run_has_color) {
                color = span->color;
            }
            if ((span->flags & pt_text_span_has_family) && span->family[0]) {
                family = span->family;
            }
            if ((span->flags & pt_text_span_has_size) && span->size > 0.0f) {
                logical_size = span->size;
            }
        }
        pt_font *font = NULL;
        if (family) {
            font = pt_font_acquire(family, (int)weight, slant, logical_size * scale);
        }
        if (!font) {
            font = pt_widget_font_styled(widget, logical_size, weight, slant, ctx);
        }
        if (!font) {
            font = base;
        }
        float ascent = font->ascent / scale;
        float descent = font->descent / scale;
        if (ascent > out->ascent) {
            out->ascent = ascent;
        }
        if (descent > out->descent) {
            out->descent = descent;
        }
        if (ascent + descent > out->line_height) {
            out->line_height = ascent + descent;
        }
        rich_run_append(out, start, end, font, color, flags);
    }
    free(boundaries);
    if (out->count == 0) {
        pt_text_run *run = &out->items[out->count++];
        run->start = 0;
        run->end = 0;
        run->font = base;
    }
    return true;
}

void pt_text_runs_free(pt_text_runs *runs) {
    if (!runs) {
        return;
    }
    free(runs->items);
    memset(runs, 0, sizeof(*runs));
}

float pt_text_runs_measure(const pt_text_runs *runs, size_t start, size_t end) {
    if (!runs) {
        return 0.0f;
    }
    if (start > runs->length) {
        start = runs->length;
    }
    if (end > runs->length) {
        end = runs->length;
    }
    if (start >= end) {
        return 0.0f;
    }
    float width = 0.0f;
    for (size_t i = 0; i < runs->count; ++i) {
        const pt_text_run *run = &runs->items[i];
        size_t a = start > run->start ? start : run->start;
        size_t b = end < run->end ? end : run->end;
        if (a >= b) {
            continue;
        }
        width += pt_font_measure(run->font, runs->text + a, b - a);
    }
    float scale = runs->scale > 0.0f ? runs->scale : 1.0f;
    return width / scale;
}

float pt_text_runs_natural_width(const pt_text_runs *runs) {
    return runs ? pt_text_runs_measure(runs, 0, runs->length) : 0.0f;
}

static void rich_wrap_paragraph(const pt_text_runs *runs, size_t begin, size_t end,
                                float max_width, rich_lines *builder) {
    if (begin >= end) {
        rich_lines_push(builder, begin, begin);
        return;
    }
    max_width = pt_max_float(1.0f, max_width);
    size_t position = begin;
    while (position < end) {
        if (pt_text_runs_measure(runs, position, end) <= max_width) {
            rich_lines_push(builder, position, end);
            return;
        }
        size_t fit = position;
        size_t space_after = 0;
        float width = 0.0f;
        size_t scan = position;
        while (scan < end) {
            size_t next = pt_utf8_next_boundary(runs->text, end, scan);
            if (next <= scan) {
                next = scan + 1;
            }
            float codepoint_width = pt_text_runs_measure(runs, scan, next);
            if (width + codepoint_width > max_width && fit > position) {
                break;
            }
            width += codepoint_width;
            fit = next;
            if (runs->text[next - 1] == ' ' || runs->text[next - 1] == '\t') {
                space_after = next;
            }
            scan = next;
        }
        if (fit <= position) {
            fit = pt_utf8_next_boundary(runs->text, end, position);
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
        rich_lines_push(builder, position, line_end);
        position = line_end;
    }
}

int pt_text_runs_wrap(const pt_text_runs *runs, float max_width, pt_text_line **out_lines) {
    rich_lines builder = {0};
    if (!runs || runs->length == 0) {
        rich_lines_push(&builder, 0, 0);
        if (out_lines) {
            *out_lines = builder.items;
        }
        return (int)builder.count;
    }
    size_t paragraph_start = 0;
    for (;;) {
        size_t paragraph_end = paragraph_start;
        while (paragraph_end < runs->length && runs->text[paragraph_end] != '\n') {
            ++paragraph_end;
        }
        size_t content_end = paragraph_end;
        if (content_end > paragraph_start && runs->text[content_end - 1] == '\r') {
            --content_end;
        }
        rich_wrap_paragraph(runs, paragraph_start, content_end, max_width, &builder);
        if (paragraph_end >= runs->length) {
            break;
        }
        paragraph_start = paragraph_end + 1;
        if (paragraph_start == runs->length) {
            rich_lines_push(&builder, runs->length, runs->length);
            break;
        }
    }
    if (out_lines) {
        *out_lines = builder.items;
    }
    return (int)builder.count;
}

size_t pt_text_runs_position_for_x(const pt_text_runs *runs, size_t start, size_t end,
                                   float local_x) {
    if (!runs) {
        return start;
    }
    if (start > runs->length) {
        start = runs->length;
    }
    if (end > runs->length) {
        end = runs->length;
    }
    if (start >= end || local_x <= 0.0f) {
        return start;
    }
    float best_distance = 0.0f;
    size_t best = start;
    bool have_best = false;
    float width = 0.0f;
    size_t scan = start;
    for (;;) {
        float distance = fabsf(width - local_x);
        if (!have_best || distance < best_distance) {
            best_distance = distance;
            best = scan;
            have_best = true;
        }
        if (scan >= end) {
            break;
        }
        size_t next = pt_utf8_next_boundary(runs->text, end, scan);
        if (next <= scan) {
            next = scan + 1;
        }
        width += pt_text_runs_measure(runs, scan, next);
        scan = next;
    }
    return best;
}

static void rich_draw_ellipsis(pt_canvas *canvas, const pt_text_runs *runs, float x,
                               float baseline, pt_color color) {
    static const char ellipsis[] = "\xE2\x80\xA6";
    const pt_font *font = runs->count > 0 ? runs->items[0].font : NULL;
    if (font) {
        pt_canvas_draw_text_line(canvas, font, ellipsis, sizeof(ellipsis) - 1, x, baseline, 0,
                                 pt_text_align_start, color, false);
    }
}

void pt_text_runs_draw_line(pt_canvas *canvas, const pt_text_runs *runs, size_t start,
                            size_t end, float x, float baseline, float max_width,
                            pt_text_align align, pt_color color, bool ellipsis) {
    if (!canvas || !runs || runs->count == 0 || runs->length == 0) {
        return;
    }
    if (start > runs->length) {
        start = runs->length;
    }
    if (end > runs->length) {
        end = runs->length;
    }
    if (start >= end) {
        return;
    }
    bool truncated = false;
    float line_width = pt_text_runs_measure(runs, start, end);
    if (max_width > 0.0f && line_width > max_width) {
        if (ellipsis) {
            static const char ellipsis_bytes[] = "\xE2\x80\xA6";
            const pt_font *font = runs->items[0].font;
            float ellipsis_width =
                pt_font_measure(font, ellipsis_bytes, sizeof(ellipsis_bytes) - 1) /
                runs->scale;
            size_t fit = start;
            size_t scan = start;
            while (scan < end) {
                size_t next = pt_utf8_next_boundary(runs->text, end, scan);
                if (next <= scan) {
                    next = scan + 1;
                }
                if (pt_text_runs_measure(runs, start, next) + ellipsis_width > max_width) {
                    break;
                }
                fit = next;
                scan = next;
            }
            end = fit;
            line_width = pt_text_runs_measure(runs, start, end);
            truncated = true;
            if (start >= end) {
                float origin = x;
                if (align == pt_text_align_center) {
                    origin += (max_width - ellipsis_width) * 0.5f;
                } else if (align == pt_text_align_end) {
                    origin += max_width - ellipsis_width;
                }
                rich_draw_ellipsis(canvas, runs, origin, baseline, color);
                return;
            }
        } else {
            while (end > start && pt_text_runs_measure(runs, start, end) > max_width) {
                end = pt_utf8_prev_boundary(runs->text, end, end);
                if (end < start) {
                    end = start;
                }
            }
            line_width = pt_text_runs_measure(runs, start, end);
        }
    }
    if (max_width > 0.0f) {
        if (align == pt_text_align_center) {
            x += (max_width - line_width) * 0.5f;
        } else if (align == pt_text_align_end) {
            x += max_width - line_width;
        }
    }
    float pen = 0.0f;
    for (size_t i = 0; i < runs->count; ++i) {
        const pt_text_run *run = &runs->items[i];
        size_t a = start > run->start ? start : run->start;
        size_t b = end < run->end ? end : run->end;
        if (a >= b) {
            continue;
        }
        float segment_width = pt_font_measure(run->font, runs->text + a, b - a) / runs->scale;
        pt_color run_color = (run->flags & pt_run_has_color) ? run->color : color;
        pt_canvas_draw_text_line(canvas, run->font, runs->text + a, b - a, x + pen, baseline, 0,
                                 pt_text_align_start, run_color, false);
        if (run->flags & pt_run_underline) {
            float thickness = pt_max_float(1.0f, run->font->pixel_size / 14.0f) / runs->scale;
            float underline_y = baseline + run->font->descent / runs->scale * 0.5f;
            pt_canvas_fill_rect(canvas, x + pen, underline_y, segment_width, thickness,
                                run_color);
        }
        pen += segment_width;
    }
    if (truncated) {
        rich_draw_ellipsis(canvas, runs, x + line_width, baseline, color);
    }
}
