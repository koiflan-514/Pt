/*
 * Layout engine checks: flex distribution, alignment, margins, scrolling.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void test_flex_row(pt_app *app) {
    pt_window *window = pt_window_create(app, "flex", 600, 400);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *row = pt_row_create(root);
    pt_widget_set_height(row, 100.0f);
    pt_widget_set_padding(row, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(row, 10.0f);
    pt_widget *fixed = pt_box_create(row, pt_direction_row);
    pt_widget_set_width(fixed, 50.0f);
    pt_widget_set_height(fixed, 20.0f);
    pt_widget *flex_a = pt_box_create(row, pt_direction_row);
    pt_widget_set_height(flex_a, 20.0f);
    pt_widget_set_grow(flex_a, 1.0f);
    pt_widget *flex_b = pt_box_create(row, pt_direction_row);
    pt_widget_set_height(flex_b, 20.0f);
    pt_widget_set_grow(flex_b, 2.0f);

    pt_canvas *canvas = pt_canvas_create(600, 400, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect row_rect = bounds_of(row);
    pt_check_near(row_rect.x, 0.0, 0.01);
    pt_check_near(row_rect.y, 44.0, 0.01);
    pt_check_near(row_rect.w, 600.0, 0.01);
    pt_check_near(row_rect.h, 100.0, 0.01);

    pt_rect a = bounds_of(fixed);
    pt_rect b = bounds_of(flex_a);
    pt_rect c = bounds_of(flex_b);
    pt_check_near(a.x, 10.0, 0.01);
    pt_check_near(a.y, 54.0, 0.01);
    pt_check_near(a.w, 50.0, 0.01);
    pt_check_near(a.h, 80.0, 0.01);
    pt_check_near(b.x, 70.0, 0.01);
    pt_check_near(c.x, 250.0, 0.01);
    pt_check_near(b.w, 170.0, 0.01);
    pt_check_near(c.w, 340.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_alignment_and_margins(pt_app *app) {
    pt_window *window = pt_window_create(app, "align", 300, 200);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    root->align_items = pt_align_center;

    pt_widget *child = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(child, 100.0f, 40.0f);
    pt_widget_set_margin(child, 10.0f, 0.0f, 0.0f, 0.0f);

    pt_canvas *canvas = pt_canvas_create(300, 200, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect rect = bounds_of(child);
    pt_check_near(rect.x, 100.0, 0.01);
    pt_check_near(rect.y, 54.0, 0.01);
    pt_check_near(rect.w, 100.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_scroll_clamp(pt_app *app) {
    pt_window *window = pt_window_create(app, "scroll", 300, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *scroll = pt_scroll_create(root);
    pt_widget_set_height(scroll, 200.0f);
    pt_widget_set_width(scroll, 300.0f);
    pt_widget *content = pt_column_create(scroll);
    pt_widget_set_gap(content, 0.0f);
    for (int i = 0; i < 10; ++i) {
        pt_widget *label = pt_label_create(content, "row");
        pt_widget_set_height(label, 30.0f);
    }

    pt_canvas *canvas = pt_canvas_create(300, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_check_near(scroll->scroll_content_height, 300.0, 1.0);
    pt_check_near(scroll->scroll_y, 0.0, 0.01);

    pt_widget_wheel(scroll, 1.0f);
    pt_check_near(scroll->scroll_y, 48.0, 0.01);
    pt_widget_wheel(scroll, 10.0f);
    pt_check_near(scroll->scroll_y, 100.0, 1.0);
    pt_widget_wheel(scroll, -1.0f);
    pt_check_near(scroll->scroll_y, 52.0, 1.0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_hidden_and_wrap(pt_app *app) {
    pt_window *window = pt_window_create(app, "wrap", 240, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *first = pt_label_create(root, "first");
    pt_widget_set_height(first, 20.0f);
    pt_widget *hidden = pt_label_create(root, "hidden");
    pt_widget_set_height(hidden, 50.0f);
    pt_widget *second = pt_label_create(root, "second");
    pt_widget_set_height(second, 20.0f);
    pt_widget_set_visible(hidden, false);

    pt_widget *wrapped = pt_label_create(root, "a long paragraph that needs several lines");
    pt_widget_set_label_wrap(wrapped, true);
    pt_widget_set_width(wrapped, 100.0f);

    pt_canvas *canvas = pt_canvas_create(240, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    pt_rect first_rect = bounds_of(first);
    pt_rect second_rect = bounds_of(second);
    pt_rect wrapped_rect = bounds_of(wrapped);
    pt_check_near(first_rect.y, 54.0, 0.01);
    pt_check_near(second_rect.y, 74.0, 0.01);
    pt_check(wrapped_rect.h > 20.0f);
    pt_check_near(wrapped_rect.x, 10.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_min_max(pt_app *app) {
    pt_window *window = pt_window_create(app, "bounds", 400, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *box = pt_box_create(root, pt_direction_row);
    pt_widget_set_align_self(box, pt_align_start);
    pt_widget_set_width(box, 10.0f);
    pt_widget_set_min_width(box, 120.0f);
    pt_widget_set_max_width(box, 200.0f);
    pt_widget_set_height(box, 40.0f);

    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect rect = bounds_of(box);
    pt_check_near(rect.w, 120.0, 0.01);

    pt_widget_set_width(box, 300.0f);
    pt_check(pt_window_paint(window, canvas));
    rect = bounds_of(box);
    pt_check_near(rect.w, 200.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Wrapped RTL text keeps its logical line order but reorders per line; the
 * label grows to the wrapped line count and a text area measures the same. */
static void test_wrapped_rtl(pt_app *app) {
    pt_font *probe = pt_font_acquire("DejaVu Sans", pt_font_weight_regular,
                                     pt_font_slant_upright, 14.0f);
    if (!probe || !pt_font_has_glyph(probe, 0x05D0u)) {
        printf("skip layout: no Hebrew-capable frame font; wrapped RTL checks skipped\n");
        return;
    }
    const char *text =
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B \xD7\x9C\xD7\x9D\xD7\x9E";
    pt_text_line *lines = NULL;
    int count = pt_font_wrap_lines_bidi(probe, text, strlen(text), 60.0f, pt_bidi_base_auto,
                                        false, &lines);
    pt_check(count >= 2);
    for (int i = 0; i < count; ++i) {
        pt_check(lines[i].base_level == 1);
        pt_check(pt_font_measure_bidi(probe, text + lines[i].start,
                                      lines[i].end - lines[i].start,
                                      pt_bidi_base_rtl) <= 60.5f);
    }
    free(lines);

    pt_window *window = pt_window_create(app, "rtl wrap", 260, 240);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    pt_widget *label = pt_label_create(root, text);
    pt_widget_set_label_wrap(label, true);
    pt_widget_set_width(label, 60.0f);
    pt_widget_set_align_self(label, pt_align_start);
    pt_canvas *canvas = pt_canvas_create(260, 240, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    pt_rect bounds = bounds_of(label);
    pt_check(bounds.h > probe->line_height);
    uint32_t background = pt_canvas_get_pixel(canvas, 0, 0);
    int painted = 0;
    for (int y = 0; y < canvas->height; ++y) {
        for (int x = 0; x < canvas->width; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != background) {
                ++painted;
            }
        }
    }
    pt_check(painted > 50);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* ---- 2.1 logical start/end padding ------------------------------------- */

/* The resolved physical sides are layout state, so the checks read the widget
 * fields through internal.h after a layout pass. */
static void check_padding(const pt_widget *widget, double top, double right, double bottom,
                          double left) {
    pt_check_near(widget->padding_top, top, 0.01);
    pt_check_near(widget->padding_right, right, 0.01);
    pt_check_near(widget->padding_bottom, bottom, 0.01);
    pt_check_near(widget->padding_left, left, 0.01);
}

/* LTR: start is left and end is right. The child sits after the start padding
 * and the content width is the box minus both logical edges. */
static void test_logical_padding_ltr(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-ltr", 400, 240);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *card = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(card, 300.0f, 60.0f);
    pt_widget_set_align_self(card, pt_align_start);
    pt_widget_set_padding_logical(card, 5.0f, 16.0f, 7.0f, 8.0f);
    pt_check(card->padding_logical);
    pt_widget *child = pt_box_create(card, pt_direction_row);
    pt_widget_set_size(child, 40.0f, 20.0f);
    pt_widget_set_align_self(child, pt_align_start);

    pt_canvas *canvas = pt_canvas_create(400, 240, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 5.0, 8.0, 7.0, 16.0);
    pt_rect card_rect = bounds_of(card);
    pt_rect child_rect = bounds_of(child);
    pt_check_near(child_rect.x, card_rect.x + 16.0, 0.01);
    pt_check_near(child_rect.y, card_rect.y + 5.0, 0.01);
    pt_check(card_rect.w == 300.0f);

    /* The measured width agrees with the laid-out width. */
    pt_layout_ctx ctx = pt_layout_context_for(card);
    float measured_w = 0.0f;
    float measured_h = 0.0f;
    pt_widget_measure(card, &ctx, 300.0f, 60.0f, &measured_w, &measured_h);
    pt_check(measured_w == 300.0f);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* RTL: the same call mirrors to start=right and end=left. */
static void test_logical_padding_rtl(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-rtl", 400, 240);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);

    pt_widget *card = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(card, 300.0f, 60.0f);
    pt_widget_set_align_self(card, pt_align_start);
    pt_widget_set_padding_logical(card, 5.0f, 16.0f, 7.0f, 8.0f);
    pt_widget *child = pt_box_create(card, pt_direction_row);
    pt_widget_set_size(child, 40.0f, 20.0f);
    pt_widget_set_align_self(child, pt_align_start);

    pt_canvas *canvas = pt_canvas_create(400, 240, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 5.0, 16.0, 7.0, 8.0);
    pt_rect card_rect = bounds_of(card);
    pt_rect child_rect = bounds_of(child);
    /* The row mirrors, so the first child packs against the right start edge
     * and clears the 16 px start padding there. */
    pt_check_near(child_rect.x, card_rect.x + card_rect.w - 16.0 - 40.0, 0.01);
    pt_check_near(child_rect.y, card_rect.y + 5.0, 0.01);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* A direction change after the setter re-resolves on the next layout, both for
 * an explicit value on the widget and for one inherited from an ancestor. */
static void test_logical_padding_direction_change(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-dir", 400, 240);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);

    pt_widget *card = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(card, 300.0f, 60.0f);
    pt_widget_set_align_self(card, pt_align_start);
    pt_widget_set_padding_logical(card, 0.0f, 16.0f, 0.0f, 8.0f);
    pt_canvas *canvas = pt_canvas_create(400, 240, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 0.0, 8.0, 0.0, 16.0);

    /* A painted window does not lay out again unless something is dirty, so
     * change the direction through the public setter and repaint. */
    pt_widget_set_ui_direction(card, pt_ui_direction_rtl);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 0.0, 16.0, 0.0, 8.0);
    pt_widget_set_ui_direction(card, pt_ui_direction_inherit);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 0.0, 8.0, 0.0, 16.0);

    /* A standalone layout entry resolves the subtree it is given, so the flip
     * does not depend on the window paint path. The card itself is measured
     * along with the root. */
    pt_window *standalone = pt_window_create(app, "logical-dir-2", 400, 240);
    pt_check(standalone != NULL);
    pt_widget *standalone_root = pt_window_root(standalone);
    pt_widget_set_padding(standalone_root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(standalone_root, 0.0f);
    pt_widget_set_ui_direction(standalone_root, pt_ui_direction_rtl);
    pt_widget *inner_card = pt_box_create(standalone_root, pt_direction_row);
    pt_check(inner_card != NULL);
    pt_widget_set_size(inner_card, 60.0f, 20.0f);
    pt_widget_set_padding_logical(inner_card, 0.0f, 2.0f, 0.0f, 6.0f);
    pt_widget_layout(standalone_root, 0.0f, 0.0f, 400.0f, 240.0f, NULL);
    check_padding(inner_card, 0.0, 2.0, 0.0, 6.0);
    pt_widget_set_ui_direction(standalone_root, pt_ui_direction_ltr);
    pt_widget_layout(standalone_root, 0.0f, 0.0f, 400.0f, 240.0f, NULL);
    check_padding(inner_card, 0.0, 6.0, 0.0, 2.0);
    pt_window_free(standalone);

    /* A nested widget with its own LTR direction inside an RTL parent keeps
     * LTR mapping while measuring on its own. */
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    pt_widget *inner = pt_box_create(card, pt_direction_row);
    pt_widget_set_size(inner, 100.0f, 20.0f);
    pt_widget_set_ui_direction(inner, pt_ui_direction_ltr);
    pt_widget_set_padding_logical(inner, 0.0f, 3.0f, 0.0f, 4.0f);
    pt_layout_ctx ctx = pt_layout_context_for(inner);
    float measured_w = 0.0f;
    float measured_h = 0.0f;
    pt_widget_measure(inner, &ctx, 100.0f, 20.0f, &measured_w, &measured_h);
    check_padding(inner, 0.0, 4.0, 0.0, 3.0);
    pt_check_near(measured_w, 100.0, 0.01);
    pt_widget_set_ui_direction(inner, pt_ui_direction_inherit);
    pt_widget_measure(inner, &ctx, 100.0f, 20.0f, &measured_w, &measured_h);
    check_padding(inner, 0.0, 3.0, 0.0, 4.0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* The physical and logical setters share the four edges: the last call wins. */
static void test_logical_padding_last_call_wins(pt_app *app) {
    pt_window *window = pt_window_create(app, "logical-wins", 400, 240);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(root, 0.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);

    pt_widget *card = pt_box_create(root, pt_direction_row);
    pt_widget_set_size(card, 300.0f, 60.0f);
    pt_widget_set_align_self(card, pt_align_start);
    pt_canvas *canvas = pt_canvas_create(400, 240, 1.0f);
    pt_check(canvas != NULL);

    /* Logical set, then physical: the physical values survive a layout even
     * though the widget sits in RTL. */
    pt_widget_set_padding_logical(card, 1.0f, 10.0f, 2.0f, 20.0f);
    pt_widget_set_padding(card, 3.0f, 30.0f, 4.0f, 40.0f);
    pt_check(!card->padding_logical);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 3.0, 30.0, 4.0, 40.0);

    /* Physical set, then logical: the logical mapping is active. */
    pt_widget_set_padding(card, 3.0f, 30.0f, 4.0f, 40.0f);
    pt_widget_set_padding_logical(card, 1.0f, 10.0f, 2.0f, 20.0f);
    pt_check(card->padding_logical);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 1.0, 10.0, 2.0, 20.0);

    /* A physical call also drops the stored logical pair from the next pass. */
    pt_widget_set_padding(card, 0.0f, 5.0f, 0.0f, 6.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_ltr);
    pt_check(pt_window_paint(window, canvas));
    check_padding(card, 0.0, 5.0, 0.0, 6.0);

    /* NULL is a no-op. */
    pt_widget_set_padding_logical(NULL, 1.0f, 2.0f, 3.0f, 4.0f);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.layout");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("layout");
    }
    test_flex_row(app);
    test_alignment_and_margins(app);
    test_scroll_clamp(app);
    test_hidden_and_wrap(app);
    test_min_max(app);
    test_wrapped_rtl(app);
    test_logical_padding_ltr(app);
    test_logical_padding_rtl(app);
    test_logical_padding_direction_change(app);
    test_logical_padding_last_call_wins(app);
    pt_app_destroy(app);
    return pt_test_finish("layout");
}
