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

#include <xkbcommon/xkbcommon-keysyms.h>

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

typedef struct paint_probe {
    int calls;
    float width;
} paint_probe;

typedef struct pointer_probe {
    int count;
    pt_pointer_phase phase;
    uint32_t button;
    uint32_t click_count;
    bool pressed;
    bool consume_motion;
} pointer_probe;

static bool record_pointer(pt_window *window, const pt_pointer_event *event, void *data) {
    (void)window;
    pointer_probe *probe = data;
    probe->count++;
    probe->phase = event->phase;
    probe->button = event->button;
    probe->click_count = event->click_count;
    probe->pressed = event->pressed;
    return event->phase == pt_pointer_phase_motion && probe->consume_motion;
}

typedef struct key_probe {
    int raw_calls;
    int legacy_calls;
    bool consume_raw;
    bool consume_legacy;
    uint32_t modifiers_seen;
    int order[8];
    int order_count;
} key_probe;

static bool record_raw_key(pt_window *window, uint32_t key, uint32_t modifiers,
                           const char *utf8, void *data) {
    (void)window;
    (void)key;
    (void)utf8;
    key_probe *probe = data;
    probe->raw_calls++;
    probe->modifiers_seen = modifiers;
    if (probe->order_count < 8) {
        probe->order[probe->order_count++] = 1;
    }
    return probe->consume_raw;
}

static bool record_legacy_key(pt_window *window, uint32_t key, uint32_t modifiers,
                              void *data) {
    (void)window;
    (void)key;
    (void)modifiers;
    key_probe *probe = data;
    probe->legacy_calls++;
    if (probe->order_count < 8) {
        probe->order[probe->order_count++] = 2;
    }
    return probe->consume_legacy;
}

static void record_paint(pt_widget *widget, pt_paint_context *context, void *data) {
    (void)widget;
    paint_probe *probe = data;
    probe->calls++;
    probe->width = pt_paint_width(context);
    pt_paint_fill_rect(context, 0.0f, 0.0f, 12.0f, 12.0f, pt_rgb(0xAA3300));
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

/* Button specificity (standards section 9): right and middle must not touch
 * the left button's press state, click count or long-press timer. */
static void test_button_specificity(pt_app *app) {
    pt_window *window = pt_window_create(app, "buttons", 320, 240);
    pt_widget *button = pt_button_create(pt_window_root(window), "Target");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(320, 240, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect bounds = bounds_of(button);
    float cx = bounds.x + bounds.w * 0.5f;
    float cy = bounds.y + bounds.h * 0.5f;

    pointer_probe probe = {0};
    pt_window_set_pointer_callback(window, record_pointer, &probe);
    pt_window_input_pointer_enter(window, cx, cy);
    pt_window_input_pointer_motion(window, cx, cy, 900);

    const uint32_t left = 0x110u;
    const uint32_t right = 0x111u;
    uint32_t left_slot = left & 31u;

    pt_window_input_pointer_button(window, left, true, cx, cy, 1, 1000);
    pt_window_input_pointer_button(window, left, false, cx, cy, 2, 1010);
    pt_check(window->click_count[left_slot] == 1);
    pt_check((window->pointer_buttons & (1u << left_slot)) == 0);
    pt_check(probe.phase == pt_pointer_phase_up && probe.button == left);
    pt_check(!probe.pressed);

    /* A right click in the same spot is its own gesture. */
    pt_window_input_pointer_button(window, right, true, cx, cy, 3, 1020);
    pt_check(window->click_count[left_slot] == 1);
    pt_check(probe.phase == pt_pointer_phase_down && probe.button == right);
    pt_check(probe.pressed);
    pt_check(probe.click_count == 1);
    pt_window_input_pointer_button(window, right, false, cx, cy, 4, 1030);

    /* The next left click is therefore a double click. */
    pt_window_input_pointer_button(window, left, true, cx, cy, 5, 1040);
    pt_check(window->click_count[left_slot] == 2);
    pt_check(probe.click_count == 2);
    pt_window_input_pointer_button(window, left, false, cx, cy, 6, 1050);

    /* A right press/release inside a held left press keeps both the left bit
     * and the long-press target alive; only the left release ends it. */
    pt_window_input_pointer_button(window, left, true, cx, cy, 7, 2000);
    pt_check(window->press_target == button);
    pt_window_input_pointer_button(window, right, true, cx, cy, 8, 2010);
    pt_window_input_pointer_button(window, right, false, cx, cy, 9, 2020);
    pt_check(window->press_target == button);
    pt_check((window->pointer_buttons & (1u << left_slot)) != 0);
    pt_window_input_pointer_button(window, left, false, cx, cy, 10, 2030);
    pt_check(window->press_target == NULL);
    pt_check(window->pointer_buttons == 0);

    pt_window_input_pointer_leave(window);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Motion return values are advisory: a consuming motion callback must not
 * stop hover, drag tracking or slider value updates. */
static void test_motion_advisory(pt_app *app) {
    pt_window *window = pt_window_create(app, "motion", 360, 240);
    pt_widget *slider = pt_slider_create(pt_window_root(window));
    pt_widget_set_height(slider, 32.0f);
    pt_slider_set_value(slider, 0.2f);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 240, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect bounds = bounds_of(slider);
    float cy = bounds.y + bounds.h * 0.5f;
    float start_x = bounds.x + bounds.w * 0.35f;
    float end_x = bounds.x + bounds.w * 0.9f;

    pointer_probe probe = {.consume_motion = true};
    pt_window_set_pointer_callback(window, record_pointer, &probe);
    pt_window_input_pointer_enter(window, start_x, cy);
    pt_check(window->hovered == slider);
    pt_window_input_pointer_button(window, 0x110u, true, start_x, cy, 20, 3000);
    pt_window_input_pointer_motion(window, end_x, cy, 3010);
    pt_check(probe.phase == pt_pointer_phase_motion);
    pt_check(probe.count > 0);
    pt_check(pt_slider_get_value(slider) > 0.7f);
    pt_window_input_pointer_button(window, 0x110u, false, end_x, cy, 21, 3020);

    /* Leaving still clears hover even though the callback returns true. */
    pointer_probe leave_probe = {.consume_motion = true};
    pt_window_set_pointer_callback(window, record_pointer, &leave_probe);
    pt_window_input_pointer_enter(window, start_x, cy);
    pt_check(window->hovered == slider);
    pt_window_input_pointer_leave(window);
    pt_check(window->hovered == NULL);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Raw input dispatch order and consume scope (standards section 9). */
static void test_raw_key_dispatch(pt_app *app) {
    pt_window *window = pt_window_create(app, "raw", 360, 260);
    pt_widget *entry = pt_entry_create(pt_window_root(window), "type");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 260, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_window_focus(window, entry);

    key_probe probe = {0};
    pt_window_set_key_event_callback(window, record_raw_key, &probe);
    pt_window_set_key_callback(window, record_legacy_key, &probe);

    /* Raw consumes: no legacy call, no widget insert, modifiers still land. */
    probe.consume_raw = true;
    pt_window_input_key(window, XKB_KEY_a, (uint32_t)pt_key_modifier_shift, "a", true);
    pt_check(probe.raw_calls == 1);
    pt_check(probe.legacy_calls == 0);
    pt_check(probe.modifiers_seen == (uint32_t)pt_key_modifier_shift);
    pt_check(window->modifiers == (uint32_t)pt_key_modifier_shift);
    pt_check(strcmp(pt_widget_get_text(entry), "") == 0);

    /* Raw passes, legacy consumes: still no widget insert. */
    probe.consume_raw = false;
    probe.consume_legacy = true;
    pt_window_input_key(window, XKB_KEY_b, 0, "b", true);
    pt_check(probe.raw_calls == 2);
    pt_check(probe.legacy_calls == 1);
    pt_check(strcmp(pt_widget_get_text(entry), "") == 0);

    /* Neither consumes: raw runs before legacy and the text arrives. */
    probe.consume_legacy = false;
    probe.order_count = 0;
    pt_window_input_key(window, XKB_KEY_c, 0, "c", true);
    pt_check(probe.order_count == 2);
    pt_check(probe.order[0] == 1 && probe.order[1] == 2);
    pt_check(strcmp(pt_widget_get_text(entry), "c") == 0);

    /* Overlay key handling runs before raw input: while the menu is open it
     * owns the keyboard, so the callback never sees a key. */
    pt_menu *menu = pt_menu_create(window);
    pt_menu_add_item(menu, "One", NULL, NULL, NULL);
    pt_menu_add_item(menu, "Two", NULL, NULL, NULL);
    pt_menu_show_at(menu, 24.0f, window->header_height + 24.0f);
    pt_check(pt_menu_is_open(menu));
    int before = probe.raw_calls;
    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(probe.raw_calls == before);
    pt_window_input_key(window, XKB_KEY_Escape, 0, NULL, true);
    pt_check(!pt_menu_is_open(menu));
    pt_check(probe.raw_calls == before);

    /* With no overlay the callback is reached again. */
    probe.consume_raw = true;
    pt_window_input_key(window, XKB_KEY_F5, 0, NULL, true);
    pt_check(probe.raw_calls == before + 1);
    pt_menu_destroy(menu);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Decoration negotiation and window-state consumption (standards section 8). */
static void test_decoration(pt_app *app) {
    pt_window *window = pt_window_create(app, "decor", 400, 300);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    /* Explicit client-side by default; Pt draws the header. */
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_client);
    pt_check(pt_window_get_effective_decoration_mode(window) == pt_decoration_client);
    pt_check_near(window->header_height, pt_header_height, 0.01);

    /* set_decorated only governs the Pt-drawn header and never requests SSD. */
    pt_window_set_decorated(window, false);
    pt_check_near(window->header_height, 0.0, 0.01);
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_client);
    pt_window_set_decorated(window, true);
    pt_check_near(window->header_height, pt_header_height, 0.01);

    /* A server request takes effect immediately headlessly. */
    pt_window_set_decoration_mode(window, pt_decoration_server);
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_server);
    pt_check(pt_window_get_effective_decoration_mode(window) == pt_decoration_server);
    pt_check(!window->csd);
    pt_check_near(window->header_height, 0.0, 0.01);
    pt_window_set_decorated(window, false);
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_server);
    pt_window_set_decorated(window, true);
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_server);
    pt_check_near(window->header_height, 0.0, 0.01);

    /* Back to client-side, then Q-DEC-001: requested CSD, configure says SSD. */
    pt_window_set_decoration_mode(window, pt_decoration_client);
    pt_check_near(window->header_height, pt_header_height, 0.01);
    pt_window_apply_decoration_mode(window, pt_decoration_server);
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_client);
    pt_check(pt_window_get_effective_decoration_mode(window) == pt_decoration_server);
    pt_check(!window->csd);
    pt_check_near(window->header_height, 0.0, 0.01);
    pt_window_apply_decoration_mode(window, pt_decoration_client);
    pt_check_near(window->header_height, pt_header_height, 0.01);

    /* Fullscreen hides the header independent of the decoration mode. */
    pt_window_apply_toplevel_state(window, false, true, false, false, false, false);
    pt_check_near(window->header_height, 0.0, 0.01);
    pt_window_apply_toplevel_state(window, false, false, false, false, false, false);
    pt_check_near(window->header_height, pt_header_height, 0.01);

    /* Tiling disables edge-resize affordances (the compositor owns geometry). */
    pt_window_apply_toplevel_state(window, false, false, true, false, false, false);
    pt_window_input_pointer_enter(window, 1.0f, 120.0f);
    pt_check(window->hovered_csd == pt_csd_part_none);
    pt_window_apply_toplevel_state(window, false, false, false, false, false, false);
    pt_window_input_pointer_enter(window, 1.0f, 120.0f);
    pt_check(window->hovered_csd == pt_csd_part_edge_left);
    pt_window_input_pointer_leave(window);

    /* configure_bounds clamps size requests; zero clears the bound. */
    pt_window_apply_configure_bounds(window, 300, 200);
    pt_window_set_size(window, 500, 400);
    pt_check(pt_window_get_width(window) == 300);
    pt_check(pt_window_get_height(window) == 200);
    pt_window_apply_configure_bounds(window, 0, 0);
    pt_window_set_size(window, 500, 400);
    pt_check(pt_window_get_width(window) == 500);
    pt_check(pt_window_get_height(window) == 400);

    /* Missing maximize capability disables the request and the button. */
    pt_window_apply_wm_capabilities(window, false, false, false);
    pt_check(!window->maximize_enabled);
    pt_check(!pt_window_maximize(window));
    float max_x = (float)pt_window_get_width(window) - 8.0f - 36.0f - 18.0f;
    pt_window_input_pointer_enter(window, max_x, window->header_height * 0.5f);
    pt_check(window->hovered_csd != pt_csd_part_maximize);
    pt_window_input_pointer_leave(window);
    pt_window_apply_wm_capabilities(window, true, true, true);
    pt_check(pt_window_maximize(window));
    pt_check(window->maximized);
    pt_window_apply_toplevel_state(window, false, false, false, false, false, false);

    /* Invalid enum values are ignored. */
    pt_window_set_decoration_mode(window, (pt_decoration_mode)99);
    pt_check(pt_window_get_decoration_mode(window) == pt_decoration_client);

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
    test_button_specificity(app);
    test_motion_advisory(app);
    test_raw_key_dispatch(app);
    test_decoration(app);

    /* Primary selection (middle-click paste) API. Headless runs keep an
     * in-process buffer, but publication still requires a triggering event
     * serial (Q-PRI-001): a programmatic set with no trigger returns false. */
    pt_window *pwin = pt_window_create(app, "primary", 320, 240);
    pt_check(pwin != NULL);
    pt_check(pt_window_primary_get(pwin) == NULL);
    pt_check(!pt_window_primary_set(pwin, "programmatic"));
    pt_check(pt_window_primary_get(pwin) == NULL);
    pt_check(!pt_window_primary_set_serial(pwin, "no serial", 0));

    /* Keyboard selection: the last key press supplies the serial. */
    pt_window_input_key_serial(pwin, 41);
    pt_check(pwin->key_serial == 41);
    pt_check(pwin->primary_serial == 41);
    pt_check(pt_window_primary_set(pwin, "keyboard text"));
    char *primary = pt_window_primary_get(pwin);
    pt_check(primary != NULL && strcmp(primary, "keyboard text") == 0);
    pt_free(primary);

    /* Drag selection: press alone is not a trigger; the release is. */
    float click_x = 20.0f;
    float click_y = pwin->header_height + 20.0f;
    pt_window_input_pointer_button(pwin, 0x110u, true, click_x, click_y, 50, 1000);
    pt_check(pwin->primary_serial == 41);
    pt_window_input_pointer_button(pwin, 0x110u, false, click_x, click_y, 51, 1010);
    pt_check(pwin->primary_serial == 51);
    pt_check(pt_window_primary_set(pwin, "drag text"));
    /* A right release must not replace the trigger. */
    pt_window_input_pointer_button(pwin, 0x111u, true, click_x, click_y, 60, 1020);
    pt_window_input_pointer_button(pwin, 0x111u, false, click_x, click_y, 61, 1030);
    pt_check(pwin->primary_serial == 51);
    primary = pt_window_primary_get(pwin);
    pt_check(primary != NULL && strcmp(primary, "drag text") == 0);
    pt_free(primary);

    /* The explicit entry point accepts a serial from inside an event callback. */
    pt_check(pt_window_primary_set_serial(pwin, "explicit", 77));
    primary = pt_window_primary_get(pwin);
    pt_check(primary != NULL && strcmp(primary, "explicit") == 0);
    pt_free(primary);
    pt_check(pt_window_primary_set(pwin, NULL));
    pt_check(pt_window_primary_get(pwin) == NULL);
    /* NULL-window error contract matches the clipboard pair. */
    pt_check(!pt_window_primary_set(NULL, "nope"));
    pt_check(!pt_window_primary_set_serial(NULL, "nope", 5));
    pt_check(pt_window_primary_get(NULL) == NULL);
    pt_window_free(pwin);

    /* Offscreen painting: drives a custom paint callback without a
     * compositor and proves the drawn pixels land in the caller buffer.
     * The buffer holds 0xAARRGGBB words. */
    pt_window *dwin = pt_window_create(app, "offscreen", 64, 48);
    pt_check(dwin != NULL);
    paint_probe probe = {0, 0};
    pt_widget_set_paint_callback(pt_window_root(dwin), record_paint, &probe);
    uint32_t *pixels = calloc(64 * 48, sizeof(uint32_t));
    pt_check(pixels != NULL);
    pt_check(pt_window_paint_pixels(dwin, pixels, 64, 48));
    pt_check(probe.calls == 1);
    pt_check(probe.width >= 1.0f);
    pt_color mark = pt_rgb(0xAA3300);
    uint32_t expected = ((mark & 0xFFu) << 24) | (mark >> 8);
    bool found = false;
    for (int i = 0; i < 64 * 48; ++i) {
        if (pixels[i] == expected) {
            found = true;
            break;
        }
    }
    pt_check(found);
    pt_check(!pt_window_paint_pixels(dwin, NULL, 64, 48));
    pt_check(!pt_window_paint_pixels(NULL, pixels, 64, 48));
    pt_check(!pt_window_paint_pixels(dwin, pixels, 0, 48));
    free(pixels);
    pt_window_free(dwin);

    pt_app_destroy(app);
    return pt_test_finish("window");
}
