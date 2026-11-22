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
    uint8_t base_level;
} text_line_builder;

static void line_push(text_line_builder *builder, size_t start, size_t end) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 8;
        builder->lines = pt_realloc_mem(builder->lines, builder->capacity * sizeof(*builder->lines));
    }
    builder->lines[builder->count].start = start;
    builder->lines[builder->count].end = end;
    builder->lines[builder->count].base_level = builder->base_level;
    ++builder->count;
}

static float measure_prefix(const pt_font *font, const char *text, size_t len, size_t position,
                            float scale) {
    if (position > len) {
        position = len;
    }
    return pt_font_measure(font, text, position) / scale;
}

/* Width of a logical range for the breaker: styled runs measure their visual
 * runs, a plain bidi paragraph sums the UBA runs that overlap, and the LTR
 * fast path keeps the pre-2.0 prefix measurement. */
typedef struct layout_measure {
    const pt_text_runs *runs;
    const pt_font *font;
    const char *text;
    const pt_bidi_line *line;
    size_t line_offset;
    float scale;
    bool bidi;
} layout_measure;

static float layout_measure_range(const layout_measure *measure, size_t from, size_t to) {
    if (to <= from) {
        return 0.0f;
    }
    if (measure->runs) {
        return pt_text_runs_measure(measure->runs, from, to);
    }
    if (!measure->bidi || !measure->line) {
        return pt_font_measure(measure->font, measure->text + from, to - from) / measure->scale;
    }
    float total = 0.0f;
    for (size_t i = 0; i < measure->line->run_count; ++i) {
        const pt_bidi_run *run = &measure->line->runs[i];
        size_t run_start = measure->line_offset + run->offset;
        size_t run_end = run_start + run->length;
        size_t a = from > run_start ? from : run_start;
        size_t b = to < run_end ? to : run_end;
        if (a >= b) {
            continue;
        }
        hb_direction_t direction = (run->level & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        total += pt_font_measure_direction(measure->font, measure->text + a, b - a, direction);
    }
    return total / measure->scale;
}

static void layout_paragraph(const char *text, float max_width, size_t start, size_t end,
                             const layout_measure *measure, text_line_builder *builder) {
    if (start >= end) {
        line_push(builder, start, start);
        return;
    }
    max_width = pt_max_float(1.0f, max_width);
    size_t position = start;
    while (position < end) {
        if (layout_measure_range(measure, position, end) <= max_width) {
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
            float width = layout_measure_range(measure, position, next);
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
    float text_ascent = 0.0f;
    float text_descent = 0.0f;
    pt_font_metrics_for_text(font, text, len, &text_ascent, &text_descent);
    out->ascent = text_ascent / scale;
    out->line_height = pt_max_float(text_ascent + text_descent, font->line_height) / scale;
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
        size_t content_len = content_end - paragraph_start;
        pt_bidi_base base = pt_widget_text_base(area, text + paragraph_start, content_len);
        uint8_t base_level = base == pt_bidi_base_rtl ? 1 : 0;
        bool bidi = out->has_runs ? out->runs.bidi
                                  : (base == pt_bidi_base_rtl ||
                                     (content_len > 0 &&
                                      pt_bidi_text_may_reorder(text + paragraph_start,
                                                               content_len)));
        if (out->has_runs) {
            base_level = pt_text_runs_base_at(&out->runs, paragraph_start);
        }
        layout_measure measure = {.runs = out->has_runs ? &out->runs : NULL,
                                  .font = font,
                                  .text = text,
                                  .scale = scale,
                                  .bidi = false};
        pt_bidi_paragraph *paragraph = NULL;
        pt_bidi_line line = {0};
        if (bidi && content_len > 0) {
            pt_bidi_base forced = (base_level & 1u) ? pt_bidi_base_rtl : pt_bidi_base_ltr;
            if (pt_bidi_paragraph_analyze(text + paragraph_start, content_len, forced,
                                          &paragraph) &&
                pt_bidi_line_create(paragraph, 0, content_len, &line)) {
                measure.line = &line;
                measure.line_offset = paragraph_start;
                measure.bidi = true;
                if (!out->has_runs) {
                    base_level = line.base_level;
                }
            }
        }
        builder.base_level = base_level;
        layout_paragraph(text, width, paragraph_start, content_end, &measure, &builder);
        if (line.runs) {
            pt_bidi_line_free(&line);
        }
        if (paragraph) {
            pt_bidi_paragraph_free(paragraph);
        }
        if (paragraph_end >= len) {
            break;
        }
        paragraph_start = paragraph_end + 1;
        if (paragraph_start == len) {
            builder.base_level = base_level;
            line_push(&builder, len, len);
            break;
        }
    }
    if (builder.count == 0) {
        line_push(&builder, 0, 0);
    }
    out->lines = builder.lines;
    out->line_count = (int)builder.count;
    /* Build one logical<->visual map per rendered line. The plain path
     * re-analyses the line under its paragraph base so rule L1 applies at the
     * wrapped line end; styled runs combine their span boundaries with a
     * per-line UBA. */
    if (out->line_count > 0 && out->display_len > 0) {
        out->maps = pt_alloc_zeroed((size_t)out->line_count, sizeof(*out->maps));
        for (int i = 0; i < out->line_count; ++i) {
            size_t start = out->lines[i].start;
            size_t end = out->lines[i].end;
            if (start >= end) {
                continue;
            }
            pt_bidi_base line_base = (out->lines[i].base_level & 1u) ? pt_bidi_base_rtl
                                                                     : pt_bidi_base_ltr;
            pt_text_visual_map *map = NULL;
            if (out->has_runs) {
                if (!pt_text_visual_map_build_runs(&out->runs, start, end, line_base, &map)) {
                    map = NULL;
                }
            } else if (pt_text_visual_map_build_bidi(font, out->display + start, end - start,
                                                      line_base, &map)) {
                /* Shift the slice-relative offsets into the full display. */
                map->start = start;
                map->end = end;
                for (size_t c = 0; c < map->count; ++c) {
                    map->clusters[c].start += start;
                    map->clusters[c].end += start;
                }
            } else {
                map = NULL;
            }
            out->maps[i] = map;
        }
    }
    return true;
}

void pt_text_layout_free(pt_text_layout *layout) {
    if (!layout) {
        return;
    }
    if (layout->has_runs) {
        pt_text_runs_free(&layout->runs);
    }
    if (layout->maps) {
        for (int i = 0; i < layout->line_count; ++i) {
            pt_text_visual_map_free(layout->maps[i]);
        }
        free(layout->maps);
    }
    free(layout->display);
    free(layout->lines);
    memset(layout, 0, sizeof(*layout));
}

int pt_text_layout_line_for_position_bias(const pt_text_layout *layout, size_t position,
                                          pt_text_bias bias) {
    if (!layout || layout->line_count <= 0) {
        return 0;
    }
    if (position > layout->display_len) {
        position = layout->display_len;
    }
    for (int i = 0; i < layout->line_count; ++i) {
        if (position < layout->lines[i].start) {
            return i > 0 ? i - 1 : 0;
        }
        if (position > layout->lines[i].end) {
            continue;
        }
        /* A soft-wrap boundary belongs to both lines: the previous line ends
         * there and the next one starts there. The caret's affinity decides
         * which side of the break it is drawn on; a hard newline has its own
         * byte between the two lines and stays on the upper one. */
        if (bias == pt_text_bias_leading && i + 1 < layout->line_count &&
            position == layout->lines[i + 1].start) {
            return i + 1;
        }
        return i;
    }
    return layout->line_count - 1;
}

int pt_text_layout_line_for_position(const pt_text_layout *layout, size_t position) {
    /* The historical (pre-affinity) assignment is the trailing one: the byte
     * at a shared soft-wrap boundary stays on the line above. */
    return pt_text_layout_line_for_position_bias(layout, position, pt_text_bias_trailing);
}

pt_text_bias pt_text_layout_bias_for_line(const pt_text_layout *layout, int line,
                                          size_t position, pt_text_bias preferred) {
    pt_text_bias other = preferred == pt_text_bias_trailing ? pt_text_bias_leading
                                                            : pt_text_bias_trailing;
    if (pt_text_layout_line_for_position_bias(layout, position, preferred) == line) {
        return preferred;
    }
    if (pt_text_layout_line_for_position_bias(layout, position, other) == line) {
        return other;
    }
    return preferred;
}

float pt_text_layout_x_for_position(const pt_text_layout *layout, int line, size_t position) {
    return pt_text_layout_x_for_position_bias(layout, line, position, pt_text_bias_leading);
}

float pt_text_layout_x_for_position_bias(const pt_text_layout *layout, int line,
                                         size_t position, pt_text_bias bias) {
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
    if (layout->maps && layout->maps[line]) {
        return pt_text_visual_map_x_for_position(layout->maps[line], position, bias);
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
    if (layout->maps && layout->maps[line]) {
        return pt_text_visual_map_position_for_x(layout->maps[line], local_x,
                                                 pt_text_bias_leading);
    }
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
    int line = pt_text_layout_line_for_position_bias(&layout, layout.caret,
                                                     (pt_text_bias)area->caret_affinity);
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

/* Embedding level of the cluster that a caret at `position` attaches to
 * (leading affinity). Used for preedit shaping and caret composition. */
uint8_t pt_text_layout_level_for_position(const pt_text_layout *layout, int line,
                                          size_t position) {
    if (!layout || layout->line_count <= 0) {
        return 0;
    }
    if (line < 0) {
        line = 0;
    }
    if (line >= layout->line_count) {
        line = layout->line_count - 1;
    }
    const pt_text_visual_map *map = layout->maps ? layout->maps[line] : NULL;
    if (map && map->count > 0) {
        if (position <= map->start) {
            return map->clusters[0].level;
        }
        for (size_t i = map->count; i > 0; --i) {
            if (map->clusters[i - 1].start <= position) {
                return map->clusters[i - 1].level;
            }
        }
        return map->clusters[0].level;
    }
    return layout->lines[line].base_level;
}

void pt_text_area_caret_box(const pt_widget *area, const pt_layout_ctx *ctx, int *x, int *y,
                            int *w, int *h) {
    int out_x = 0;
    int out_y = 0;
    int out_w = 2;
    int out_h = 18;
    pt_text_layout layout = {0};
    if (area && pt_text_layout_build(area, ctx, pt_text_area_content_width(area), &layout)) {
        int line = pt_text_layout_line_for_position_bias(&layout, layout.caret,
                                                         (pt_text_bias)area->caret_affinity);
        pt_text_bias bias = (pt_text_bias)area->caret_affinity;
        float caret_x = pt_text_layout_x_for_position_bias(&layout, line, layout.caret, bias);
        if (area->preedit && *area->preedit) {
            size_t preedit_caret = pt_widget_preedit_caret(area);
            float preedit_width =
                pt_font_measure(layout.font, area->preedit, preedit_caret) / layout.scale;
            /* A preedit inserted inside an RTL run extends visually left from
             * the insertion point, so the composed caret sits before it. */
            if (pt_text_layout_level_for_position(&layout, line, layout.caret) & 1u) {
                caret_x -= preedit_width;
            } else {
                caret_x += preedit_width;
            }
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

size_t pt_text_area_position_at_bias(const pt_widget *area, const pt_layout_ctx *ctx, float x,
                                    float y, pt_text_bias *bias) {
    if (!area) {
        if (bias) {
            *bias = pt_text_bias_leading;
        }
        return 0;
    }
    pt_text_layout layout = {0};
    if (!pt_text_layout_build(area, ctx, pt_text_area_content_width(area), &layout)) {
        if (bias) {
            *bias = pt_text_bias_leading;
        }
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
    /* A click on a shared soft-wrap boundary keeps the caret on the line that
     * was clicked, so the affinity that renders there is the one we store. */
    pt_text_bias chosen =
        pt_text_layout_bias_for_line(&layout, line, position, pt_text_bias_leading);
    pt_text_layout_free(&layout);
    if (bias) {
        *bias = chosen;
    }
    return position;
}

size_t pt_text_area_position_at(const pt_widget *area, const pt_layout_ctx *ctx, float x,
                                float y) {
    return pt_text_area_position_at_bias(area, ctx, x, y, NULL);
}
