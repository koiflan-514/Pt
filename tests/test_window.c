/*
 * Window, client-side decoration and scaling checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct resize_state {
    int calls;
    int width;
    int height;
} resize_state;

static void on_resize(pt_window *window, int width, int height, void *data) {
    (void)window;
    resize_state *state = data;
    state->calls++;
    state->width = width;
    state->height = height;
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void test_csd_hover_and_hit(pt_app *app) {
    pt_window *window = pt_window_create(app, "csd", 400, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget *button = pt_button_create(root, "Content");
    pt_window_show(window);

    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_color titlebar = pt_app_get_theme_color(app, pt_theme_titlebar);
    pt_check(pt_canvas_get_pixel(canvas, 4, 4) == titlebar);
    pt_check_near(window->header_height, 44.0, 0.01);

    pt_window_input_pointer_enter(window, 370.0f, 20.0f);
    pt_check(window->hovered_csd == pt_csd_part_close);
    pt_check(window->current_cursor == pt_cursor_pointer);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_canvas_get_pixel(canvas, 372, 20) != titlebar);

    pt_window_input_pointer_motion(window, 2.0f, 2.0f, 10);
    pt_check(window->hovered_csd == pt_csd_part_edge_top_left);
    pt_check(window->current_cursor == pt_cursor_resize_nwse);

    pt_window_input_pointer_motion(window, 180.0f, 20.0f, 20);
    pt_check(window->hovered_csd == pt_csd_part_titlebar);
    pt_check(window->current_cursor == pt_cursor_default);

    pt_rect button_rect = bounds_of(button);
    pt_window_input_pointer_motion(window, button_rect.x + 5.0f,
                                   button_rect.y + button_rect.h * 0.5f, 30);
    pt_check(window->hovered_csd == pt_csd_part_none);
    pt_check(window->hovered == button);

    pt_window_input_pointer_leave(window);
    pt_check(window->hovered == NULL);
    pt_check(window->hovered_csd == pt_csd_part_none);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_scaled_painting(pt_app *app) {
    pt_window *window = pt_window_create(app, "scale", 320, 240);
    pt_widget *label = pt_label_create(pt_window_root(window), "Scaled");
    pt_window_show(window);

    pt_canvas *canvas = pt_canvas_create(640, 480, 2.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_color titlebar = pt_app_get_theme_color(app, pt_theme_titlebar);
    pt_color background = pt_app_get_theme_color(app, pt_theme_background);
    pt_check(pt_canvas_get_pixel(canvas, 8, 8) == titlebar);
    pt_check(pt_canvas_get_pixel(canvas, 8, 200) == background);
    pt_rect label_rect = bounds_of(label);
    pt_check(label_rect.x < 320.0f && label_rect.y < 240.0f);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_window_properties(pt_app *app) {
    pt_window *window = pt_window_create(app, "props", 360, 260);
    pt_check(pt_window_get_width(window) == 360);
    pt_check(pt_window_get_height(window) == 260);

    resize_state state = {0};
    pt_window_set_resize_callback(window, on_resize, &state);
    pt_window_apply_configure(window, 480, 320);
    pt_check(state.calls == 1);
    pt_check(state.width == 480 && state.height == 320);
    pt_check(pt_window_get_width(window) == 480);
    pt_check(pt_window_get_height(window) == 320);

    pt_window_set_min_size(window, 200, 150);
    pt_window_apply_configure(window, 100, 100);
    pt_check(pt_window_get_width(window) == 200);
    pt_check(pt_window_get_height(window) == 150);

    pt_window_set_size(window, 420, 300);
    pt_check(pt_window_get_width(window) == 420);
    pt_check(pt_window_get_height(window) == 300);

    pt_window_set_decorated(window, false);
    pt_check_near(window->header_height, 0.0, 0.01);
    pt_window_set_decorated(window, true);
    pt_check_near(window->header_height, 44.0, 0.01);

    pt_window_set_title(window, "new title");
    pt_check(strcmp(window->title, "new title") == 0);

    pt_widget_set_visible(pt_window_root(window), false);
    pt_check(pt_window_paint(window, NULL) == false);
    pt_check(pt_window_is_open(window));
    pt_window_free(window);
}

static void test_scroll_painting(pt_app *app) {
    pt_window *window = pt_window_create(app, "scroll paint", 420, 300);
    pt_widget *scroll = pt_scroll_create(pt_window_root(window));
    pt_widget_set_height(scroll, 160.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_gap(list, 4.0f);
    for (int i = 0; i < 80; ++i) {
        char text[32];
        snprintf(text, sizeof(text), "Row %d", i);
        pt_label_create(list, text);
    }
    pt_window_show(window);

    pt_canvas *canvas = pt_canvas_create(420, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_check(scroll->scroll_content_height > scroll->height);
    pt_color background = pt_app_get_theme_color(app, pt_theme_background);
    pt_check(pt_canvas_get_pixel(canvas, 10, 290) == background);

    pt_widget_wheel(scroll, 4.0f);
    pt_check(scroll->scroll_y > 0.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_canvas_get_pixel(canvas, 10, 290) == background);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_animation(pt_app *app) {
    pt_window *window = pt_window_create(app, "anim", 320, 240);
    pt_widget *button = pt_button_create(pt_window_root(window), "Hover");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(320, 240, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    pt_widget_pointer_enter(button, 0.0f, 0.0f);
    float before = button->hover_mix;
    for (int i = 0; i < 20; ++i) {
        pt_widget_update_animations(pt_window_root(window), 0.016f);
    }
    pt_check(button->hover_mix > before);
    pt_check(button->hover_mix > 0.9f);
    pt_widget_pointer_leave(button);
    for (int i = 0; i < 30; ++i) {
        pt_widget_update_animations(pt_window_root(window), 0.016f);
    }
    pt_check(button->hover_mix < 0.1f);

    pt_window_tick(window);
    pt_check(pt_window_paint(window, canvas));
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.window");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("window");
    }
    test_csd_hover_and_hit(app);
    test_scaled_painting(app);
    test_window_properties(app);
    test_scroll_painting(app);
    test_animation(app);
    pt_app_destroy(app);
    return pt_test_finish("window");
}
