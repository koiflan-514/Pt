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
    uint8_t base_level;
} rich_lines;

static void rich_lines_push(rich_lines *builder, size_t start, size_t end) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 8;
        builder->items = pt_realloc_mem(builder->items,
                                        builder->capacity * sizeof(*builder->items));
    }
    builder->items[builder->count].start = start;
    builder->items[builder->count].end = end;
    builder->items[builder->count].base_level = builder->base_level;
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

/* Resolves the logical box of one inline image. A positive, finite descriptor
 * dimension wins; zero/non-finite falls back to the icon's source size, and a
 * single positive dimension preserves the intrinsic aspect ratio. A missing
 * handle or a zero intrinsic size collapses to zero, which measures as no
 * advance and draws nothing (the painter skips non-positive boxes). */
static void rich_image_size(const pt_text_image *image, float *width, float *height) {
    float w = 0.0f;
    float h = 0.0f;
    if (image && image->image) {
        bool want_w = isfinite(image->width) && image->width > 0.0f;
        bool want_h = isfinite(image->height) && image->height > 0.0f;
        float intrinsic_w = (float)pt_icon_image_width(image->image);
        float intrinsic_h = (float)pt_icon_image_height(image->image);
        if (want_w && want_h) {
            w = image->width;
            h = image->height;
        } else if (want_w) {
            w = image->width;
            h = intrinsic_w > 0.0f && intrinsic_h > 0.0f
                    ? w * intrinsic_h / intrinsic_w
                    : intrinsic_h;
        } else if (want_h) {
            h = image->height;
            w = intrinsic_w > 0.0f && intrinsic_h > 0.0f
                    ? h * intrinsic_w / intrinsic_h
                    : intrinsic_w;
        } else {
            w = intrinsic_w;
            h = intrinsic_h;
        }
    }
    if (!isfinite(w) || w < 0.0f) {
        w = 0.0f;
    }
    if (!isfinite(h) || h < 0.0f) {
        h = 0.0f;
    }
    if (width) {
        *width = w;
    }
    if (height) {
        *height = h;
    }
}

/* True when [start, end) is exactly one valid image placeholder: the side-table
 * entry starts there and the text still carries EF BF BC. The builder splits at
 * every image start, so a fontless piece is either a whole image or a text
 * range that happens to share its start. */
static const pt_text_image *rich_image_for(const pt_widget *widget, size_t start, size_t end) {
    if (!widget || widget->image_count == 0 || end - start != 3u) {
        return NULL;
    }
    const char *text = widget->text ? widget->text : "";
    const unsigned char *bytes = (const unsigned char *)text + start;
    if (bytes[0] != 0xEFu || bytes[1] != 0xBFu || bytes[2] != 0xBCu) {
        return NULL;
    }
    for (uint32_t i = 0; i < widget->image_count; ++i) {
        if (widget->images[i].start == start) {
            return &widget->images[i];
        }
    }
    return NULL;
}

static void rich_run_reserve(pt_text_runs *runs) {
    if (runs->count < runs->capacity) {
        return;
    }
    size_t capacity = runs->capacity ? runs->capacity * 2 : 8;
    runs->items = pt_realloc_mem(runs->items, capacity * sizeof(*runs->items));
    runs->capacity = capacity;
}

static void rich_run_append(pt_text_runs *runs, size_t start, size_t end, pt_font *font,
                            pt_color color, uint32_t flags, uint8_t level,
                            hb_direction_t direction, const pt_text_image *image) {
    if (runs->count > 0) {
        pt_text_run *last = &runs->items[runs->count - 1];
        if (last->end == start && last->font == font && last->color == color &&
            last->flags == flags && last->level == level && last->direction == direction &&
            last->image == image) {
            last->end = end;
            return;
        }
    }
    rich_run_reserve(runs);
    pt_text_run *run = &runs->items[runs->count++];
    run->start = start;
    run->end = end;
    run->font = font;
    run->color = color;
    run->flags = flags;
    run->level = level;
    run->direction = direction;
    run->image = image;
}

/* Resolves the span style covering `start` and appends the [start, end) piece.
 * Used by both the logical LTR builder and the visual bidi builder so the two
 * paths agree on font, colour, flags and metrics. */
static void rich_append_piece(const pt_widget *widget, const pt_layout_ctx *ctx,
                              pt_text_runs *out, pt_font *base, pt_font_weight base_weight,
                              size_t start, size_t end, uint8_t level,
                              hb_direction_t direction) {
    const char *text = out->text;
    float scale = out->scale;
    const pt_text_image *image = rich_image_for(widget, start, end);
    if (image) {
        /* An image run is atomic, fontless and never shaped. Its logical box
         * raises the composed metrics exactly like a styled run: baseline puts
         * the whole height above the baseline, middle splits it. The line
         * height is max over every run, which the caller combines below. */
        float image_height = 0.0f;
        rich_image_size(image, NULL, &image_height);
        float above = image->align == pt_text_image_align_middle ? image_height * 0.5f
                                                                 : image_height;
        float below = image->align == pt_text_image_align_middle ? image_height * 0.5f
                                                                 : 0.0f;
        if (above > out->ascent) {
            out->ascent = above;
        }
        if (below > out->descent) {
            out->descent = below;
        }
        if (above + below > out->line_height) {
            out->line_height = above + below;
        }
        rich_run_append(out, start, end, NULL, 0, 0, level, direction, image);
        return;
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
    float run_ascent = 0.0f;
    float run_descent = 0.0f;
    pt_font_metrics_for_text(font, text + start, end - start, &run_ascent, &run_descent);
    float ascent = run_ascent / scale;
    float descent = run_descent / scale;
    if (ascent > out->ascent) {
        out->ascent = ascent;
    }
    if (descent > out->descent) {
        out->descent = descent;
    }
    if (ascent + descent > out->line_height) {
        out->line_height = ascent + descent;
    }
    rich_run_append(out, start, end, font, color, flags, level, direction, NULL);
}

static void rich_append_line_runs(const pt_widget *widget, const pt_layout_ctx *ctx,
                                  pt_text_runs *out, pt_font *base, pt_font_weight base_weight,
                                  const size_t *boundaries, size_t unique,
                                  const pt_bidi_line *line, size_t paragraph_offset) {
    for (size_t r = 0; r < line->run_count; ++r) {
        const pt_bidi_run *run = &line->runs[r];
        hb_direction_t direction = (run->level & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        size_t run_start = paragraph_offset + run->offset;
        size_t run_end = run_start + run->length;
        /* UBA runs are already in visual order. An odd-level run's styled
         * sub-runs must be emitted in reverse logical order so the master list
         * (and everything that walks it) stays visual; even runs stay logical. */
        for (size_t step = 0; step + 1 < unique; ++step) {
            size_t i = (run->level & 1u) ? unique - 2 - step : step;
            size_t start = boundaries[i];
            size_t end = boundaries[i + 1];
            size_t a = start > run_start ? start : run_start;
            size_t b = end < run_end ? end : run_end;
            if (a >= b) {
                continue;
            }
            rich_append_piece(widget, ctx, out, base, base_weight, a, b, run->level, direction);
        }
    }
}

/* Nearest explicitly selected text direction, if any. `auto` must not be
 * collapsed to one base before paragraph splitting: auto resolves P2/P3 per
 * paragraph. */
bool pt_widget_explicit_text_base(const pt_widget *widget, pt_bidi_base *out) {
    for (const pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (cursor->text_direction != pt_text_direction_auto) {
            if (out) {
                *out = (pt_text_direction)cursor->text_direction == pt_text_direction_rtl
                           ? pt_bidi_base_rtl
                           : pt_bidi_base_ltr;
            }
            return true;
        }
    }
    return false;
}

bool pt_widget_rtl_default(const pt_widget *widget) {
    return pt_widget_resolved_ui_direction(widget) == pt_ui_direction_rtl;
}

pt_bidi_base pt_widget_text_base(const pt_widget *widget, const char *text, size_t len) {
    for (const pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (cursor->text_direction != pt_text_direction_auto) {
            return (pt_text_direction)cursor->text_direction == pt_text_direction_rtl
                       ? pt_bidi_base_rtl
                       : pt_bidi_base_ltr;
        }
    }
    pt_bidi_base strong = pt_bidi_first_strong_direction(text, len);
    if (strong != pt_bidi_base_auto) {
        return strong;
    }
    return pt_widget_resolved_ui_direction(widget) == pt_ui_direction_rtl ? pt_bidi_base_rtl
                                                                          : pt_bidi_base_ltr;
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
    size_t max_runs = (size_t)(widget->span_count + widget->image_count) * 2 + 2;
    out->items = pt_alloc_zeroed(max_runs, sizeof(*out->items));
    out->capacity = max_runs;
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
    /* An image placeholder is a hard boundary: one codepoint, never part of a
     * neighbouring font run, so it cannot be shaped or split. Invalid entries
     * (the text no longer carries U+FFFC there) contribute nothing. */
    for (uint32_t i = 0; i < widget->image_count; ++i) {
        size_t start = widget->images[i].start;
        if (start > len || len - start < 3u) {
            continue;
        }
        if (rich_image_for(widget, start, start + 3u) != &widget->images[i]) {
            continue;
        }
        boundaries[boundary_count++] = start;
        boundaries[boundary_count++] = start + 3u;
    }
    qsort(boundaries, boundary_count, sizeof(*boundaries), rich_compare_positions);
    size_t unique = 0;
    for (size_t i = 0; i < boundary_count; ++i) {
        if (unique == 0 || boundaries[i] != boundaries[unique - 1]) {
            boundaries[unique++] = boundaries[i];
        }
    }

    pt_font_weight base_weight = (pt_font_weight)pt_widget_font_weight(widget);
    pt_bidi_base base_direction = pt_widget_text_base(widget, text, len);
    out->bidi = base_direction == pt_bidi_base_rtl || pt_bidi_text_may_reorder(text, len);
    out->base_level = base_direction == pt_bidi_base_rtl ? 1 : 0;

    if (!out->bidi) {
        /* LTR fast path: the pre-2.0 logical segmentation, verbatim. */
        for (size_t i = 0; i + 1 < unique; ++i) {
            size_t start = boundaries[i];
            size_t end = boundaries[i + 1];
            if (start >= end) {
                continue;
            }
            rich_append_piece(widget, ctx, out, base, base_weight, start, end, 0,
                              HB_DIRECTION_INVALID);
        }
    } else {
        /* Explicit line breaks are paragraph boundaries for the UBA. Each
         * paragraph resolves its own base: an explicit widget direction is
         * reused, while auto runs P2/P3 independently (neutral-only paragraphs
         * keep the resolved UI-direction default). Runs are concatenated in
         * per-paragraph visual order.
         *
         * Only '\n' splits paragraphs here, matching pt_text_runs_wrap (a CR
         * before LF is dropped, as there). The engine may still find another
         * Unicode B-class separator inside a line, so the inner loop advances by
         * the paragraph length it reports; that length includes the separator,
         * so the next paragraph starts immediately after it.
         *
         * Width-driven wrapping (no '\n') is not reordered in this wave; the
         * line breaker and per-line base stay W3 work. */
        pt_bidi_base explicit_base = pt_bidi_base_auto;
        bool has_explicit_base = pt_widget_explicit_text_base(widget, &explicit_base);
        bool rtl_default = pt_widget_resolved_ui_direction(widget) == pt_ui_direction_rtl;
        bool reordered = false;
        out->paragraphs = pt_alloc_zeroed(len + 1, sizeof(*out->paragraphs));
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
            size_t segment_start = paragraph_start;
            while (segment_start < content_end) {
                pt_bidi_paragraph *paragraph = NULL;
                pt_bidi_line line = {0};
                size_t segment_len = content_end - segment_start;
                bool analyzed =
                    has_explicit_base
                        ? pt_bidi_paragraph_analyze(text + segment_start, segment_len,
                                                    explicit_base, &paragraph)
                        : pt_bidi_paragraph_analyze_default(text + segment_start, segment_len,
                                                           rtl_default, &paragraph);
                bool lined = analyzed &&
                             pt_bidi_line_create(paragraph, 0, segment_len, &line);
                if (lined) {
                    if (!reordered) {
                        out->base_level = line.base_level;
                    }
                    rich_append_line_runs(widget, ctx, out, base, base_weight, boundaries,
                                          unique, &line, segment_start);
                    size_t consumed = pt_bidi_paragraph_length(paragraph);
                    if (consumed == 0 || consumed > segment_len) {
                        consumed = segment_len;
                    }
                    if (out->paragraph_count < len + 1) {
                        pt_text_line *entry = &out->paragraphs[out->paragraph_count++];
                        entry->start = segment_start;
                        entry->end = segment_start + consumed;
                        entry->base_level = line.base_level;
                    }
                    reordered = true;
                    pt_bidi_line_free(&line);
                    pt_bidi_paragraph_free(paragraph);
                    if (consumed >= segment_len) {
                        segment_start = content_end;
                        break;
                    }
                    segment_start += consumed;
                    if (segment_start >= content_end) {
                        break;
                    }
                    continue;
                }
                if (paragraph) {
                    pt_bidi_paragraph_free(paragraph);
                }
                break;
            }
            if (segment_start < content_end) {
                /* A segment the engine could not analyse keeps logical order
                 * under the resolved base instead of dropping bytes. */
                for (size_t i = 0; i + 1 < unique; ++i) {
                    size_t start = boundaries[i] > segment_start ? boundaries[i]
                                                                 : segment_start;
                    size_t end = boundaries[i + 1] < content_end ? boundaries[i + 1]
                                                                 : content_end;
                    if (start >= end) {
                        continue;
                    }
                    rich_append_piece(widget, ctx, out, base, base_weight, start, end,
                                      out->base_level,
                                      out->base_level ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                }
                if (out->paragraph_count < len + 1) {
                    pt_text_line *entry = &out->paragraphs[out->paragraph_count++];
                    entry->start = segment_start;
                    entry->end = content_end;
                    entry->base_level = out->base_level;
                }
            } else if (content_end == paragraph_start && out->paragraph_count < len + 1) {
                /* Empty paragraph: remember its resolved base for an empty
                 * wrapped line. */
                pt_text_line *entry = &out->paragraphs[out->paragraph_count++];
                entry->start = paragraph_start;
                entry->end = paragraph_start;
                entry->base_level = has_explicit_base
                                        ? (explicit_base == pt_bidi_base_rtl ? 1 : 0)
                                        : (rtl_default ? 1 : 0);
            }
            if (paragraph_end >= len) {
                break;
            }
            paragraph_start = paragraph_end + 1;
            if (paragraph_start == len) {
                break;
            }
        }
        if (out->count == 0) {
            for (size_t i = 0; i + 1 < unique; ++i) {
                size_t start = boundaries[i];
                size_t end = boundaries[i + 1];
                if (start >= end) {
                    continue;
                }
                rich_append_piece(widget, ctx, out, base, base_weight, start, end,
                                  out->base_level,
                                  out->base_level ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            }
        }
    }
    free(boundaries);
    if (widget->image_count > 0) {
        /* With an image the design's composition is explicit: ascent and
         * descent are the maxima over every piece and the composed line height
         * is their sum, so a baseline image's descent-bearing neighbours are
         * not clipped. Image-free builds keep the historical max-of-pieces
         * line height byte-identical. */
        float composed = out->ascent + out->descent;
        if (composed > out->line_height) {
            out->line_height = composed;
        }
    }
    if (out->count == 0) {
        rich_run_reserve(out);
        pt_text_run *run = &out->items[out->count++];
        run->start = 0;
        run->end = 0;
        run->font = base;
        run->level = 0;
        run->direction = HB_DIRECTION_INVALID;
        run->image = NULL;
    }
    return true;
}

void pt_text_runs_free(pt_text_runs *runs) {
    if (!runs) {
        return;
    }
    free(runs->items);
    free(runs->paragraphs);
    memset(runs, 0, sizeof(*runs));
}

uint8_t pt_text_runs_base_at(const pt_text_runs *runs, size_t position) {
    if (!runs) {
        return 0;
    }
    if (position > runs->length) {
        position = runs->length;
    }
    for (size_t i = 0; i < runs->paragraph_count; ++i) {
        const pt_text_line *paragraph = &runs->paragraphs[i];
        if (position >= paragraph->start && position <= paragraph->end) {
            return paragraph->base_level;
        }
    }
    return runs->base_level;
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
        if (run->image) {
            /* Overlap of any size counts the whole atomic box (the design's
             * partial-range rule). Device space here, divided with the rest. */
            float image_width = 0.0f;
            rich_image_size(run->image, &image_width, NULL);
            width += image_width * (runs->scale > 0.0f ? runs->scale : 1.0f);
            continue;
        }
        if (run->direction != HB_DIRECTION_INVALID) {
            width += pt_font_measure_direction(run->font, runs->text + a, b - a,
                                               run->direction);
        } else {
            width += pt_font_measure(run->font, runs->text + a, b - a);
        }
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
        builder.base_level = runs ? runs->base_level : 0;
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
        /* Every wrapped line belongs to this UBA paragraph, so it reuses the
         * paragraph base; rule L1 is applied when the line itself is drawn. */
        builder.base_level = pt_text_runs_base_at(runs, paragraph_start);
        rich_wrap_paragraph(runs, paragraph_start, content_end, max_width, &builder);
        if (paragraph_end >= runs->length) {
            break;
        }
        paragraph_start = paragraph_end + 1;
        if (paragraph_start == runs->length) {
            builder.base_level = pt_text_runs_base_at(runs, paragraph_start - 1);
            rich_lines_push(&builder, runs->length, runs->length);
            break;
        }
    }
    if (out_lines) {
        *out_lines = builder.items;
    }
    return (int)builder.count;
}

/* ---------- per-line visual segments and map (2.0 wave 3) ---------- */

typedef struct rich_segments {
    pt_text_visual_segment *items;
    size_t count;
    size_t capacity;
} rich_segments;

static void rich_segments_push(rich_segments *builder, size_t start, size_t end, pt_font *font,
                               const pt_text_image *image, hb_direction_t direction,
                               uint8_t level, float scale) {
    if (start >= end || (!font && !image)) {
        return;
    }
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 8;
        builder->items = pt_realloc_mem(builder->items,
                                        builder->capacity * sizeof(*builder->items));
    }
    float image_width = 0.0f;
    if (image) {
        float logical_width = 0.0f;
        rich_image_size(image, &logical_width, NULL);
        image_width = logical_width * (scale > 0.0f ? scale : 1.0f);
    }
    pt_text_visual_segment *segment = &builder->items[builder->count++];
    segment->start = start;
    segment->end = end;
    segment->font = font;
    segment->direction = direction;
    segment->level = level;
    segment->image = image;
    segment->image_width = image_width;
}

static const pt_text_run *rich_style_at(const pt_text_runs *runs, size_t position) {
    for (size_t i = 0; i < runs->count; ++i) {
        const pt_text_run *run = &runs->items[i];
        if (position >= run->start && position < run->end) {
            return run;
        }
    }
    return runs->count > 0 ? &runs->items[0] : NULL;
}

/* Builds the visual-order segments of one line. In the LTR fast path the
 * master runs already are logical == visual order; a bidi line runs its own
 * UBA over the visible range (so L1 applies at the line end) and splits those
 * runs at the span/font boundaries the master runs recorded. */
static void rich_line_segments(const pt_text_runs *runs, size_t start, size_t end,
                               uint8_t base_level, rich_segments *out) {
    if (!runs || start >= end || runs->count == 0) {
        return;
    }
    if (!runs->bidi) {
        for (size_t i = 0; i < runs->count; ++i) {
            const pt_text_run *run = &runs->items[i];
            size_t a = start > run->start ? start : run->start;
            size_t b = end < run->end ? end : run->end;
            rich_segments_push(out, a, b, run->font, run->image, run->direction, run->level,
                               runs->scale);
        }
        return;
    }
    pt_bidi_base base = (base_level & 1u) ? pt_bidi_base_rtl : pt_bidi_base_ltr;
    pt_bidi_paragraph *paragraph = NULL;
    pt_bidi_line line = {0};
    size_t line_len = end - start;
    bool analyzed = pt_bidi_paragraph_analyze(runs->text + start, line_len, base, &paragraph);
    if (!analyzed || !pt_bidi_line_create(paragraph, 0, line_len, &line)) {
        if (paragraph) {
            pt_bidi_paragraph_free(paragraph);
        }
        /* Unanalysable text keeps the master run order instead of dropping
         * bytes. */
        for (size_t i = 0; i < runs->count; ++i) {
            const pt_text_run *run = &runs->items[i];
            size_t a = start > run->start ? start : run->start;
            size_t b = end < run->end ? end : run->end;
            rich_segments_push(out, a, b, run->font, run->image, run->direction, run->level,
                               runs->scale);
        }
        return;
    }
    size_t boundary_capacity = runs->count * 2 + 2;
    size_t *boundaries = pt_alloc(boundary_capacity * sizeof(*boundaries));
    size_t boundary_count = 0;
    boundaries[boundary_count++] = start;
    boundaries[boundary_count++] = end;
    for (size_t i = 0; i < runs->count; ++i) {
        size_t a = runs->items[i].start;
        size_t b = runs->items[i].end;
        if (a > start && a < end && boundary_count + 1 < boundary_capacity) {
            boundaries[boundary_count++] = a;
        }
        if (b > start && b < end && boundary_count + 1 < boundary_capacity) {
            boundaries[boundary_count++] = b;
        }
    }
    qsort(boundaries, boundary_count, sizeof(*boundaries), rich_compare_positions);
    size_t unique = 0;
    for (size_t i = 0; i < boundary_count; ++i) {
        if (unique == 0 || boundaries[i] != boundaries[unique - 1]) {
            boundaries[unique++] = boundaries[i];
        }
    }
    for (size_t r = 0; r < line.run_count; ++r) {
        const pt_bidi_run *run = &line.runs[r];
        size_t run_start = start + run->offset;
        size_t run_end = run_start + run->length;
        hb_direction_t direction = (run->level & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        /* The line's UBA runs are visual already; within an odd-level run the
         * styled pieces have to be visited from the logical end so the segment
         * list (and the pen that walks it) stays visual. Even runs are logical
         * and therefore visual. */
        for (size_t step = 0; step + 1 < unique; ++step) {
            size_t i = (run->level & 1u) ? unique - 2 - step : step;
            size_t a = boundaries[i] > run_start ? boundaries[i] : run_start;
            size_t b = boundaries[i + 1] < run_end ? boundaries[i + 1] : run_end;
            if (a >= b) {
                continue;
            }
            const pt_text_run *style = rich_style_at(runs, a);
            rich_segments_push(out, a, b, style ? style->font : NULL,
                               style ? style->image : NULL, direction, run->level,
                               runs->scale);
        }
    }
    free(boundaries);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);
}

bool pt_text_visual_map_build_runs(const pt_text_runs *runs, size_t start, size_t end,
                                   pt_bidi_base base, pt_text_visual_map **out) {
    if (!out) {
        return false;
    }
    *out = NULL;
    if (!runs || start >= end || end > runs->length) {
        return false;
    }
    uint8_t base_level = pt_text_runs_base_at(runs, start);
    if (base == pt_bidi_base_rtl) {
        base_level = 1;
    } else if (base == pt_bidi_base_ltr) {
        base_level = 0;
    }
    rich_segments segments = {0};
    rich_line_segments(runs, start, end, base_level, &segments);
    bool ok = pt_text_visual_map_build(runs->text, runs->length, segments.items,
                                       segments.count, base_level, out);
    free(segments.items);
    if (!ok || !*out) {
        return false;
    }
    /* Segment offsets are already absolute; narrow the map's clamp range. */
    (*out)->start = start;
    (*out)->end = end;
    return true;
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
    if (start >= end) {
        return start;
    }
    pt_bidi_base base = (pt_text_runs_base_at(runs, start) & 1u) ? pt_bidi_base_rtl
                                                                : pt_bidi_base_ltr;
    pt_text_visual_map *map = NULL;
    if (!pt_text_visual_map_build_runs(runs, start, end, base, &map) || !map) {
        return start;
    }
    size_t position = pt_text_visual_map_position_for_x(map, local_x, pt_text_bias_leading);
    pt_text_visual_map_free(map);
    return position;
}

static const pt_text_run *rich_run_at(const pt_text_runs *runs, size_t position) {
    for (size_t i = 0; i < runs->count; ++i) {
        if (position >= runs->items[i].start && position < runs->items[i].end) {
            return &runs->items[i];
        }
    }
    return runs->count > 0 ? &runs->items[0] : NULL;
}

static const pt_font *rich_first_font(const pt_text_runs *runs) {
    for (size_t i = 0; runs && i < runs->count; ++i) {
        if (runs->items[i].font) {
            return runs->items[i].font;
        }
    }
    return NULL;
}

static void rich_draw_ellipsis(pt_canvas *canvas, const pt_text_runs *runs, float x,
                               float baseline, pt_color color) {
    static const char ellipsis[] = "\xE2\x80\xA6";
    const pt_font *font = rich_first_font(runs);
    if (font) {
        pt_canvas_draw_text_line_direction(canvas, font, ellipsis, sizeof(ellipsis) - 1, x,
                                           baseline, 0, pt_text_align_start, color, false,
                                           HB_DIRECTION_LTR);
    }
}

void pt_text_runs_draw_line_base(pt_canvas *canvas, const pt_text_runs *runs, size_t start,
                                 size_t end, uint8_t base_level, float x, float baseline,
                                 float max_width, pt_text_align align, pt_color color,
                                 bool ellipsis) {
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
    /* Visual start edge: an RTL line swaps start/end sides. Center is
     * direction-neutral. The base belongs to this line, not the first
     * paragraph of the whole run list. */
    bool rtl = (base_level & 1u) != 0;
    bool truncated = false;
    float ellipsis_width = 0.0f;
    float line_width = pt_text_runs_measure(runs, start, end);
    if (max_width > 0.0f && line_width > max_width) {
        if (ellipsis) {
            static const char ellipsis_bytes[] = "\xE2\x80\xA6";
            const pt_text_run *ellipsis_run = rich_run_at(runs, start);
            const pt_font *font =
                ellipsis_run && ellipsis_run->font ? ellipsis_run->font
                                                   : rich_first_font(runs);
            hb_direction_t ellipsis_direction =
                ellipsis_run && ellipsis_run->font ? ellipsis_run->direction
                                                   : HB_DIRECTION_LTR;
            if (font) {
                ellipsis_width =
                    pt_font_measure_direction(font, ellipsis_bytes,
                                              sizeof(ellipsis_bytes) - 1,
                                              ellipsis_direction) /
                    runs->scale;
            }
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
                } else if ((align == pt_text_align_start && rtl) ||
                           (align == pt_text_align_end && !rtl)) {
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
            /* RTL places the ellipsis left of the retained text, so the
             * composite block is ellipsis_width wider on the left. */
            if (truncated && rtl) {
                x += ellipsis_width * 0.5f;
            }
        } else if ((align == pt_text_align_start && rtl) ||
                   (align == pt_text_align_end && !rtl)) {
            x += max_width - line_width;
        } else if (truncated && rtl) {
            /* RTL align-end: the composite block starts at x, so the ellipsis
             * takes the leftmost slot and the text follows it. */
            x += ellipsis_width;
        }
    }
    /* Segments are emitted in visual order (per-line UBA for bidi lines, the
     * master logical run order otherwise). */
    rich_segments segments = {0};
    rich_line_segments(runs, start, end, base_level, &segments);
    float pen = 0.0f;
    for (size_t i = 0; i < segments.count; ++i) {
        const pt_text_visual_segment *segment = &segments.items[i];
        const pt_text_run *style = rich_style_at(runs, segment->start);
        pt_color run_color =
            style && (style->flags & pt_run_has_color) ? style->color : color;
        float segment_width;
        if (segment->image) {
            /* Atomic image box: the pen advance is logical, the drawn rect is
             * placed from the descriptor's logical height. The map already
             * carries the device width, so only this local conversion is
             * needed here and measure stays a separate logical path. */
            float image_width = 0.0f;
            float image_height = 0.0f;
            rich_image_size(segment->image, &image_width, &image_height);
            float below = segment->image->align == pt_text_image_align_middle
                              ? image_height * 0.5f
                              : 0.0f;
            pt_icon_draw_canvas(canvas, segment->image->image, x + pen,
                                baseline + below - image_height, image_width,
                                image_height, 0);
            segment_width = image_width;
        } else if (segment->direction != HB_DIRECTION_INVALID) {
            segment_width = pt_font_measure_direction(segment->font, runs->text + segment->start,
                                                      segment->end - segment->start,
                                                      segment->direction) /
                            runs->scale;
            pt_canvas_draw_text_line_direction(canvas, segment->font,
                                               runs->text + segment->start,
                                               segment->end - segment->start, x + pen,
                                               baseline, 0, pt_text_align_start, run_color,
                                               false, segment->direction);
        } else {
            segment_width = pt_font_measure(segment->font, runs->text + segment->start,
                                            segment->end - segment->start) /
                            runs->scale;
            pt_canvas_draw_text_line(canvas, segment->font, runs->text + segment->start,
                                     segment->end - segment->start, x + pen, baseline, 0,
                                     pt_text_align_start, run_color, false);
        }
        if (style && (style->flags & pt_run_underline)) {
            float thickness =
                pt_max_float(1.0f, segment->font->pixel_size / 14.0f) / runs->scale;
            float underline_y = baseline + segment->font->descent / runs->scale * 0.5f;
            pt_canvas_fill_rect(canvas, x + pen, underline_y, segment_width, thickness,
                                run_color);
        }
        pen += segment_width;
    }
    free(segments.items);
    if (truncated) {
        /* Ellipsis sits on the visual end: left for an RTL base, where the
         * retained text starts at x and the ellipsis takes the slot just
         * before it so it never overlaps the first visible glyph. */
        rich_draw_ellipsis(canvas, runs, rtl ? x - ellipsis_width : x + line_width,
                           baseline, color);
    }
}

void pt_text_runs_draw_line(pt_canvas *canvas, const pt_text_runs *runs, size_t start,
                            size_t end, float x, float baseline, float max_width,
                            pt_text_align align, pt_color color, bool ellipsis) {
    uint8_t base_level = runs ? pt_text_runs_base_at(runs, start) : 0;
    pt_text_runs_draw_line_base(canvas, runs, start, end, base_level, x, baseline, max_width,
                                align, color, ellipsis);
}
