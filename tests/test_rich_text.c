/*
 * Styled rich-text span, run, layout and editing checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

static pt_widget *make_area(pt_window *window, const char *text) {
    pt_widget *area = pt_text_area_create(pt_window_root(window), NULL);
    pt_widget_set_text(area, text);
    return area;
}

static pt_text_span span_of(uint32_t start, uint32_t length, uint32_t flags) {
    pt_text_span span = {0};
    span.start = start;
    span.length = length;
    span.flags = flags;
    return span;
}

static void test_span_api(pt_window *window) {
    pt_widget *label = pt_label_create(pt_window_root(window), "Hello");
    pt_check(pt_widget_set_text_spans(label, NULL, 0));

    pt_text_span span = span_of(1, 3, pt_text_span_has_color);
    span.color = 0xFF0000FFu;
    pt_check(pt_widget_set_text_spans(label, &span, 1));
    pt_check(pt_widget_get_text_span_count(label) == 1);
    pt_text_span got = {0};
    pt_check(pt_widget_get_text_span(label, 0, &got));
    pt_check(got.start == 1 && got.length == 3);
    pt_check(got.color == 0xFF0000FFu);
    pt_check(!pt_widget_get_text_span(label, 1, &got));

    span = span_of(3, 100, 0);
    pt_check(pt_widget_set_text_spans(label, &span, 1));
    pt_check(pt_widget_get_text_span(label, 0, &got));
    pt_check(got.start == 3 && got.length == 2);

    span = span_of(2, 0, pt_text_span_underline);
    pt_check(pt_widget_set_text_spans(label, &span, 1));
    pt_check(pt_widget_get_text_span_count(label) == 0);

    /* A set that drops every range has to behave like a clear: nothing is kept
     * and the next set still installs. */
    pt_widget_clear_text_spans(label);
    span = span_of(2, 3, pt_text_span_underline);
    pt_check(pt_widget_set_text_spans(label, &span, 1));
    pt_check(pt_widget_get_text_span(label, 0, &got));
    pt_check(got.start == 2 && got.length == 3);

    /* Destroying a spanned widget releases the buffer with the rest of its data. */
    pt_widget *temp = pt_label_create(pt_window_root(window), "abcdef");
    pt_check(pt_widget_set_text_spans(temp, &span, 1));
    pt_widget_destroy(temp);

    pt_widget *cjk = pt_label_create(pt_window_root(window), "ab\xE4\xB8\x96\xE7\x95\x8C");
    span = span_of(3, 1, pt_text_span_has_weight);
    span.weight = pt_font_weight_bold;
    pt_check(pt_widget_set_text_spans(cjk, &span, 1));
    pt_check(pt_widget_get_text_span(cjk, 0, &got));
    pt_check(got.start == 2 && got.length == 3);

    pt_widget *button = pt_button_create(pt_window_root(window), "button");
    pt_check(!pt_widget_set_text_spans(button, &span, 1));
    pt_check(pt_widget_get_text_span_count(button) == 0);

    pt_widget_set_text(label, "reset");
    pt_check(pt_widget_get_text_span_count(label) == 0);
    pt_widget_clear_text_spans(label);
}

static void test_runs(pt_window *window) {
    pt_widget *label = pt_label_create(pt_window_root(window), "abcdef");
    pt_text_span spans[2];
    spans[0] = span_of(0, 3, pt_text_span_has_weight | pt_text_span_has_color);
    spans[0].weight = pt_font_weight_bold;
    spans[0].color = 0xFF0000FFu;
    spans[1] = span_of(2, 3, pt_text_span_underline);
    pt_check(pt_widget_set_text_spans(label, spans, 2));

    pt_layout_ctx ctx = {.scale = 1.0f, .font_family = "sans-serif"};
    pt_text_runs runs = {0};
    pt_check(pt_text_runs_build(label, &ctx, &runs));
    pt_check(runs.length == 6);
    pt_check(runs.count == 3);
    pt_check(runs.items[0].start == 0 && runs.items[0].end == 2);
    pt_check(runs.items[0].font->weight == (int)pt_font_weight_bold);
    pt_check((runs.items[0].flags & pt_text_span_has_color) != 0);
    pt_check(runs.items[0].color == 0xFF0000FFu);
    pt_check(runs.items[1].start == 2 && runs.items[1].end == 5);
    pt_check(runs.items[1].font->weight == (int)pt_font_weight_regular);
    pt_check((runs.items[1].flags & pt_text_span_underline) != 0);
    pt_check((runs.items[1].flags & pt_text_span_has_color) == 0);
    pt_check(runs.items[2].start == 5 && runs.items[2].end == 6);
    pt_check(runs.items[2].flags == 0);

    float first = pt_text_runs_measure(&runs, 0, 2);
    float line = pt_text_runs_measure(&runs, 0, 6);
    pt_check(first > 0.0f);
    pt_check(line > first);
    pt_check_near(pt_text_runs_natural_width(&runs), line, 0.001);
    pt_check(pt_text_runs_position_for_x(&runs, 0, 6, 0.0f) == 0);
    pt_check(pt_text_runs_position_for_x(&runs, 0, 6, line * 2.0f) == 6);
    size_t middle = pt_text_runs_position_for_x(&runs, 0, 6, line * 0.5f);
    pt_check(middle > 0 && middle < 6);
    pt_text_runs_free(&runs);

    pt_widget_set_text(label,
                       "rich text wraps across several lines when the width is small");
    pt_check(pt_widget_set_text_spans(label, spans, 2));
    ctx = (pt_layout_ctx){.scale = 2.0f, .font_family = "sans-serif"};
    pt_check(pt_text_runs_build(label, &ctx, &runs));
    pt_text_line *lines = NULL;
    int line_count = pt_text_runs_wrap(&runs, 70.0f, &lines);
    pt_check(line_count >= 2);
    for (int i = 0; i < line_count; ++i) {
        pt_check(pt_text_runs_measure(&runs, lines[i].start, lines[i].end) <= 71.0f);
    }
    pt_check(runs.line_height > 0.0f);
    free(lines);
    pt_text_runs_free(&runs);
}

static void test_layout(pt_window *window) {
    pt_widget *area = make_area(window, "abcdef");
    pt_text_span span = span_of(0, 3, pt_text_span_has_weight | pt_text_span_has_slant);
    span.weight = pt_font_weight_bold;
    span.slant = pt_font_slant_italic;
    pt_check(pt_widget_set_text_spans(area, &span, 1));

    pt_layout_ctx ctx = {.scale = 1.0f, .font_family = "sans-serif"};
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, 200.0f, &layout));
    pt_check(layout.has_runs);
    pt_check(layout.runs.count >= 2);
    pt_check(layout.runs.items[0].font->slant == (int)pt_font_slant_italic);
    pt_check(layout.line_height > 0.0f);
    float x0 = pt_text_layout_x_for_position(&layout, 0, 0);
    float x3 = pt_text_layout_x_for_position(&layout, 0, 3);
    float x6 = pt_text_layout_x_for_position(&layout, 0, 6);
    pt_check(x0 <= 0.01f);
    pt_check(x3 > x0);
    pt_check(x6 > x3);
    size_t pos = pt_text_layout_position_for_x(&layout, 0, (x3 + x6) * 0.5f);
    pt_check(pos >= 3 && pos <= 6);
    pt_text_layout_free(&layout);

    pt_check(pt_text_layout_build(area, &ctx, 26.0f, &layout));
    pt_check(layout.line_count >= 2);
    for (int i = 0; i < layout.line_count; ++i) {
        float width = pt_text_layout_x_for_position(&layout, i, layout.lines[i].end);
        pt_check(width <= 27.0f);
    }
    pt_text_layout_free(&layout);

    float height = pt_text_area_measure_height(area, &ctx, 26.0f);
    pt_check(height > 0.0f);
}

static void test_edit_maintenance(pt_window *window) {
    pt_widget *area = make_area(window, "hello");
    pt_text_span span = span_of(0, 5, pt_text_span_has_weight);
    span.weight = pt_font_weight_bold;
    pt_check(pt_widget_set_text_spans(area, &span, 1));

    pt_text_area_set_selection(area, 5, 5);
    pt_check(pt_widget_text_input(area, "X"));
    pt_check(strcmp(pt_widget_get_text(area), "helloX") == 0);
    pt_text_span got = {0};
    pt_check(pt_widget_get_text_span_count(area) == 1);
    pt_check(pt_widget_get_text_span(area, 0, &got));
    pt_check(got.start == 0 && got.length == 6);

    pt_check(pt_text_area_undo(area));
    pt_check(strcmp(pt_widget_get_text(area), "hello") == 0);
    pt_check(pt_widget_get_text_span_count(area) == 1);
    pt_check(pt_widget_get_text_span(area, 0, &got));
    pt_check(got.length == 5);
    pt_check(pt_text_area_redo(area));
    pt_check(strcmp(pt_widget_get_text(area), "helloX") == 0);
    pt_check(pt_widget_get_text_span(area, 0, &got));
    pt_check(got.length == 6);

    pt_text_area_set_selection(area, 0, 0);
    pt_check(pt_widget_text_input(area, "Y"));
    pt_check(strcmp(pt_widget_get_text(area), "YhelloX") == 0);
    pt_check(pt_widget_get_text_span(area, 0, &got));
    pt_check(got.start == 1 && got.length == 6);

    pt_text_area_set_selection(area, 2, 4);
    pt_check(pt_widget_text_input(area, "ZZ"));
    pt_check(strcmp(pt_widget_get_text(area), "YhZZloX") == 0);
    pt_check(pt_widget_get_text_span(area, 0, &got));
    pt_check(got.start == 1 && got.length == 6);

    pt_text_area_set_selection(area, 0, 7);
    pt_check(pt_widget_text_input(area, "z"));
    pt_check(strcmp(pt_widget_get_text(area), "z") == 0);
    pt_check(pt_widget_get_text_span_count(area) == 0);
    pt_check(pt_text_area_undo(area));
    pt_check(pt_widget_get_text_span_count(area) == 1);

    pt_check(pt_widget_set_text_spans(area, &span, 1));
    pt_text_area_set_selection(area, 2, 2);
    pt_check(pt_widget_ime_apply(area, "!", 1, 0, true));
    pt_check(strcmp(pt_widget_get_text(area), "Y!ZZloX") == 0);
    pt_check(pt_widget_get_text_span(area, 0, &got));
    pt_check(got.start == 0 && got.length == 5);
}

static void test_label_paint(pt_app *app) {
    pt_window *window = pt_window_create(app, "rich label", 420, 260);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget *label = pt_label_create(
        root, "Styled label text with weight, slant, underline and color that wraps");
    pt_widget_set_width(label, 160.0f);
    pt_widget_set_align_self(label, pt_align_start);
    pt_widget_set_label_wrap(label, true);
    pt_text_span spans[2];
    spans[0] = span_of(0, 6, pt_text_span_has_weight | pt_text_span_underline |
                                pt_text_span_has_color);
    spans[0].weight = pt_font_weight_bold;
    spans[0].color = 0xFF2288FFu;
    spans[1] = span_of(7, 5, pt_text_span_has_slant | pt_text_span_has_color);
    spans[1].slant = pt_font_slant_italic;
    spans[1].color = 0x33CC66FFu;
    pt_check(pt_widget_set_text_spans(label, spans, 2));

    pt_canvas *canvas = pt_canvas_create(420, 260, 2.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(label, &x, &y, &w, &h);
    pt_check(w >= 150.0f && w <= 170.0f);
    pt_font *font = pt_widget_font(label, pt_widget_font_size(label),
                                   pt_font_weight_regular,
                                   &(pt_layout_ctx){.scale = 1.0f,
                                                    .font_family = "sans-serif"});
    pt_check(font != NULL);
    pt_check(h > font->line_height + 1.0f);

    bool colored = false;
    for (int py = 0; py < canvas->height && !colored; ++py) {
        for (int px = 0; px < canvas->width; ++px) {
            uint32_t pixel = pt_canvas_get_pixel(canvas, px, py);
            if (pixel == 0xFF2288FFu || pixel == 0x33CC66FFu) {
                colored = true;
                break;
            }
        }
    }
    pt_check(colored);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_font_cache(void) {
    pt_font *regular = pt_font_acquire("sans-serif", pt_font_weight_regular,
                                       pt_font_slant_upright, 14.0f);
    pt_font *again = pt_font_acquire("sans-serif", pt_font_weight_regular,
                                     pt_font_slant_upright, 14.0f);
    pt_check(regular != NULL && regular == again);
    pt_font *bold = pt_font_acquire("sans-serif", pt_font_weight_bold,
                                    pt_font_slant_upright, 14.0f);
    pt_check(bold != NULL && bold->weight == (int)pt_font_weight_bold);
    pt_font *italic = pt_font_acquire("sans-serif", pt_font_weight_regular,
                                      pt_font_slant_italic, 14.0f);
    pt_check(italic != NULL && italic->slant == (int)pt_font_slant_italic);
    for (int size = 5; size <= 90; ++size) {
        pt_check(pt_font_acquire("monospace", (size % 2) ? pt_font_weight_bold
                                                          : pt_font_weight_regular,
                                 (size % 3) ? pt_font_slant_upright : pt_font_slant_italic,
                                 (float)size) != NULL);
    }
    pt_check(pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright,
                             14.0f) == regular);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.rich_text");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("rich_text");
    }
    pt_window *window = pt_window_create(app, "rich", 480, 320);
    pt_check(window != NULL);

    test_span_api(window);
    test_runs(window);
    test_layout(window);
    test_edit_maintenance(window);
    test_font_cache();

    pt_window_free(window);
    test_label_paint(app);
    pt_app_destroy(app);
    return pt_test_finish("rich_text");
}
