/*
 * Damage tracking, shape cache and touch input checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct touch_state {
    int clicks;
    int pinch_begins;
    int pinch_updates;
    int pinch_ends;
    float pinch_scale;
    float pinch_rotation;
} touch_state;

static void on_click(pt_widget *widget, void *data) {
    (void)widget;
    touch_state *state = data;
    ++state->clicks;
}

static void on_gesture(pt_window *window, const pt_gesture_event *event, void *data) {
    (void)window;
    touch_state *state = data;
    if (event->kind != pt_gesture_kind_pinch) {
        return;
    }
    if (event->phase == pt_gesture_phase_begin) {
        ++state->pinch_begins;
    } else if (event->phase == pt_gesture_phase_update) {
        ++state->pinch_updates;
        state->pinch_scale = event->scale;
        state->pinch_rotation = event->rotation;
    } else if (event->phase == pt_gesture_phase_end) {
        ++state->pinch_ends;
    }
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void test_partial_damage(pt_app *app) {
    pt_window *window = pt_window_create(app, "damage", 480, 360);
    pt_widget *root = pt_window_root(window);
    pt_widget *first = pt_button_create(root, "First");
    pt_widget *second = pt_button_create(root, "Second");
    pt_window_show(window);

    pt_canvas *canvas = pt_canvas_create(480, 360, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(window->last_damage_full);
    pt_check(!window->damage_valid);
    pt_check(!window->damage_full);

    pt_widget_damage(first, 0.0f);
    pt_check(window->damage_valid);
    pt_check(!window->damage_full);
    pt_rect first_rect = bounds_of(first);
    pt_check(window->damage.x <= first_rect.x + 0.01f);
    pt_check(window->damage.y <= first_rect.y + 0.01f);
    pt_check(window->damage.x + window->damage.w >= first_rect.x + first_rect.w - 0.01f);

    pt_widget_damage(second, 0.0f);
    pt_check(window->damage.x <= first_rect.x + 0.01f);
    pt_check(window->damage.x + window->damage.w >= 480.0f - 24.0f);

    pt_canvas_clear(canvas, 0xFF00FFFFu);
    pt_check(pt_window_paint_damage(window, canvas));
    pt_check(!window->last_damage_full);
    pt_check(window->last_damage.w < 480.0f);
    pt_check(pt_canvas_get_pixel(canvas, 2, 2) == 0xFF00FFFFu);
    pt_check(pt_canvas_get_pixel(canvas, 470, 350) == 0xFF00FFFFu);

    pt_window_damage_all(window);
    pt_check(window->damage_full);
    pt_widget_damage(second, 0.0f);
    pt_check(window->damage_full);
    pt_check(pt_window_paint_damage(window, canvas));
    pt_check(window->last_damage_full);
    pt_check(pt_canvas_get_pixel(canvas, 2, 2) != 0xFF00FFFFu);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_shape_cache(void) {
    pt_font *font = pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright, 13.0f);
    pt_check(font != NULL);
    if (!font) {
        return;
    }
    size_t baseline = font->shapes.count;
    float a = pt_font_measure(font, "Cached label", 12);
    float b = pt_font_measure(font, "Cached label", 12);
    pt_check_near(a, b, 0.001);
    pt_check(font->shapes.count == baseline + 1);
    (void)pt_font_measure(font, "Cached label", 12);
    pt_check(font->shapes.count == baseline + 1);
    (void)pt_font_measure(font, "Another label", 13);
    pt_check(font->shapes.count == baseline + 2);

    for (int i = 0; i < 300; ++i) {
        char text[32];
        int length = snprintf(text, sizeof(text), "Entry %d", i);
        (void)pt_font_measure(font, text, (size_t)length);
    }
    pt_check(font->shapes.count <= 256);
    pt_check(font->shapes.bytes <= 256u * 1024u);
}

static void test_touch(pt_app *app) {
    pt_window *window = pt_window_create(app, "touch", 420, 320);
    pt_widget *root = pt_window_root(window);
    pt_widget *button = pt_button_create(root, "Tap me");
    touch_state state = {0};
    pt_button_set_on_click(button, on_click, &state);
    pt_window_set_gesture_callback(window, on_gesture, &state);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 320, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect rect = bounds_of(button);
    float x = rect.x + rect.w * 0.5f;
    float y = rect.y + rect.h * 0.5f;

    pt_window_input_touch_down(window, 7, x, y, 10, 3);
    pt_check(window->touch_active);
    pt_check(window->touch_id == 7);
    pt_window_input_touch_down(window, 8, x + 100.0f, y, 11, 4);
    pt_check(window->touch_id == -1);
    pt_check(window->touch_active);
    pt_check(window->touch_pinch);
    pt_check(state.clicks == 0);
    pt_window_input_touch_motion(window, 8, x + 200.0f, y, 12);
    pt_check(state.pinch_begins == 1);
    pt_check(state.pinch_updates == 1);
    pt_check(state.pinch_scale > 1.9f && state.pinch_scale < 2.1f);
    pt_window_input_touch_motion(window, 8, x, y + 100.0f, 13);
    pt_check(state.pinch_updates == 2);
    pt_check(state.pinch_rotation > 1.5f && state.pinch_rotation < 1.65f);
    pt_window_input_touch_up(window, 7, 20);
    pt_check(state.pinch_ends == 1);
    pt_check(window->touch_active);
    pt_check(state.clicks == 0);
    pt_window_input_touch_up(window, 8, 21);
    pt_check(!window->touch_active);
    pt_check(window->touch_id == -1);
    pt_check(!window->touch_pinch);

    pt_window_input_touch_down(window, 1, x, y, 30, 5);
    pt_check(window->touch_active);
    pt_window_input_touch_cancel(window);
    pt_check(state.clicks == 0);
    pt_check(!window->touch_active);

    pt_window_input_touch_down(window, 2, x, y, 40, 6);
    pt_window_input_touch_motion(window, 3, 1.0f, 1.0f, 41);
    pt_window_input_touch_up(window, 2, 50);
    pt_check(state.clicks == 1);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_damage_index(pt_app *app) {
    pt_window *window = pt_window_create(app, "damage index", 640, 900);
    pt_widget *root = pt_window_root(window);
    pt_widget *rows[20];
    for (int i = 0; i < 20; ++i) {
        pt_widget *row = pt_row_create(root);
        char label[24];
        snprintf(label, sizeof(label), "Row %02d", i);
        pt_label_create(row, label);
        rows[i] = row;
    }
    pt_window_show(window);

    pt_canvas *canvas = pt_canvas_create(640, 900, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    uint32_t full_visits = window->paint_visits;
    pt_check(full_visits >= 30);

    pt_widget_damage(rows[0], 0.0f);
    pt_check(window->damage_tile_count == 1);
    pt_widget_damage(rows[19], 0.0f);
    pt_check(window->damage_tile_count == 2);
    pt_widget_damage(rows[19], 0.0f);
    pt_check(window->damage_tile_count == 2);
    pt_check(!window->damage_full);

    pt_canvas_clear(canvas, 0xFFFF00FFu);
    pt_check(pt_window_paint_damage(window, canvas));
    pt_check(window->last_damage_tile_count == 2);
    pt_check(!window->last_damage_full);
    pt_check(window->paint_visits <= full_visits / 2);

    pt_rect first = bounds_of(rows[0]);
    pt_rect last = bounds_of(rows[19]);
    pt_rect middle = bounds_of(rows[9]);
    int first_x = (int)(first.x + first.w * 0.5f);
    int first_y = (int)(first.y + first.h * 0.5f);
    int last_x = (int)(last.x + last.w * 0.5f);
    int last_y = (int)(last.y + last.h * 0.5f);
    int middle_x = (int)(middle.x + middle.w * 0.5f);
    int middle_y = (int)(middle.y + middle.h * 0.5f);
    pt_check(pt_canvas_get_pixel(canvas, first_x, first_y) != 0xFFFF00FFu);
    pt_check(pt_canvas_get_pixel(canvas, last_x, last_y) != 0xFFFF00FFu);
    pt_check(pt_canvas_get_pixel(canvas, middle_x, middle_y) == 0xFFFF00FFu);

    /* Adjacent updates coalesce into a single tile; a scattered burst stays
     * bounded by the index capacity instead of one window-sized rectangle. */
    pt_widget_damage(rows[10], 10.0f);
    pt_widget_damage(rows[11], 10.0f);
    pt_check(window->damage_tile_count == 1);

    for (int i = 0; i < 24; ++i) {
        pt_window_damage_rect(window,
                              (pt_rect){(float)(i % 2) * 320.0f, (float)(i * 18), 12.0f, 12.0f});
    }
    pt_check(window->damage_tile_count <= pt_damage_tile_capacity);
    pt_check(window->damage_tile_count >= 2);
    pt_check(window->damage.x <= 1.0f && window->damage.y <= 1.0f);
    pt_check(window->damage.x + window->damage.w >= 330.0f);
    pt_check(window->damage.y + window->damage.h >= 420.0f);

    pt_window_damage_rect(window, (pt_rect){0.0f, 0.0f, 640.0f, 900.0f});
    pt_check(window->damage_tile_count == 1);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_partial_layout(pt_app *app) {
    pt_window *window = pt_window_create(app, "partial layout", 480, 320);
    pt_widget *root = pt_window_root(window);
    pt_widget *outside = pt_label_create(root, "Window title");
    pt_widget *card = pt_card_create(root);
    pt_widget_set_size(card, 300.0f, 180.0f);
    pt_widget *inner = pt_label_create(card, "Short");
    pt_window_show(window);

    pt_canvas *canvas = pt_canvas_create(480, 320, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect card_before = bounds_of(card);

    pt_widget_set_text(inner, "Text that only reflows inside the pinned card");
    pt_check(window->needs_layout);
    pt_check(window->layout_root == card);
    pt_check(!window->damage_full);
    pt_check(window->damage_tile_count >= 1);
    pt_check(window->damage.w <= card_before.w + 97.0f);
    pt_check(window->damage.h <= card_before.h + 97.0f);

    pt_check(pt_window_paint_damage(window, canvas));
    pt_check(window->last_layout_partial);
    pt_check(!window->needs_layout);
    pt_check(window->layout_root == NULL);
    pt_rect card_after = bounds_of(card);
    pt_check_near(card_after.x, card_before.x, 0.01f);
    pt_check_near(card_after.w, card_before.w, 0.01f);
    pt_check_near(card_after.h, card_before.h, 0.01f);

    pt_widget_set_text(outside, "Renamed outside the pinned subtree");
    pt_check(window->needs_layout);
    pt_check(window->layout_root == NULL);
    pt_check(window->damage_full);
    pt_check(pt_window_paint_damage(window, canvas));
    pt_check(!window->last_layout_partial);

    pt_widget_set_height(card, 120.0f);
    pt_check(window->layout_root == NULL);
    pt_check(window->damage_full);
    pt_check(pt_window_paint(window, canvas));

    pt_widget_set_text(inner, "Pinned again");
    pt_check(window->layout_root == card);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.damage");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("damage");
    }
    test_partial_damage(app);
    test_damage_index(app);
    test_partial_layout(app);
    test_shape_cache();
    test_touch(app);
    pt_app_destroy(app);
    return pt_test_finish("damage");
}
