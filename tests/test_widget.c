/*
 * Widget, input, theme and window checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct click_state {
    int clicks;
    int changes;
    char last_text[128];
} click_state;

static void on_click(pt_widget *widget, void *data) {
    (void)widget;
    ((click_state *)data)->clicks++;
}

static void on_change(pt_widget *widget, const char *text, void *data) {
    (void)widget;
    click_state *state = data;
    state->changes++;
    snprintf(state->last_text, sizeof(state->last_text), "%s", text ? text : "");
}

static void on_submit(pt_widget *widget, void *data) {
    (void)widget;
    ((click_state *)data)->clicks++;
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 0);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 0);
}

static void key_text(pt_window *window, uint32_t keysym, const char *utf8) {
    pt_window_input_key(window, keysym, 0, utf8, true);
}

static bool close_callback(pt_window *window, void *data) {
    (void)window;
    return *(bool *)data;
}

static void quit_callback(pt_app *app, void *data) {
    *(bool *)data = true;
    pt_app_quit(app);
}

static void cancelled_callback(pt_app *app, void *data) {
    (void)app;
    *(bool *)data = true;
}

static void test_controls(pt_app *app) {
    pt_window *window = pt_window_create(app, "controls", 520, 420);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);

    pt_widget *label = pt_label_create(root, "Hello, Pt");
    pt_widget *entry = pt_entry_create(root, "type here");
    pt_widget *button = pt_button_create(root, "Click");
    pt_widget *toggle = pt_toggle_create(root);
    pt_widget *slider = pt_slider_create(root);
    pt_widget_set_align_self(toggle, pt_align_start);
    pt_widget_set_align_self(slider, pt_align_start);

    click_state state = {0};
    pt_button_set_on_click(button, on_click, &state);
    pt_button_set_variant(button, pt_button_secondary);
    pt_entry_set_on_change(entry, on_change, &state);
    pt_entry_set_on_submit(entry, on_submit, &state);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 420, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_rect root_rect = bounds_of(root);
    pt_check_near(root_rect.y, 44.0, 0.01);
    pt_check_near(root_rect.h, 376.0, 0.01);
    pt_rect label_rect = bounds_of(label);
    pt_rect entry_rect = bounds_of(entry);
    pt_rect button_rect = bounds_of(button);
    pt_rect toggle_rect = bounds_of(toggle);
    pt_rect slider_rect = bounds_of(slider);
    pt_check(label_rect.h > 0.0f);
    pt_check(entry_rect.w > 0.0f);
    pt_check(button_rect.w > 0.0f);
    pt_check(toggle_rect.w == 44.0f);
    pt_check(slider_rect.w == 180.0f);

    click_at(window, button_rect.x + button_rect.w * 0.5f,
             button_rect.y + button_rect.h * 0.5f);
    pt_check(state.clicks == 1);

    pt_widget_set_enabled(button, false);
    click_at(window, button_rect.x + button_rect.w * 0.5f,
             button_rect.y + button_rect.h * 0.5f);
    pt_check(state.clicks == 1);
    pt_widget_set_enabled(button, true);

    click_at(window, entry_rect.x + 20.0f, entry_rect.y + entry_rect.h * 0.5f);
    pt_check(pt_widget_has_focus(entry));
    key_text(window, XKB_KEY_h, "h");
    key_text(window, XKB_KEY_i, "i");
    pt_check(strcmp(pt_widget_get_text(entry), "hi") == 0);
    pt_check(state.changes >= 2);
    key_text(window, XKB_KEY_BackSpace, NULL);
    pt_check(strcmp(pt_widget_get_text(entry), "h") == 0);

    pt_platform_clipboard_set(window, "XYZ");
    char *clipboard = pt_platform_clipboard_get(window);
    pt_check(clipboard != NULL && strcmp(clipboard, "XYZ") == 0);
    free(clipboard);
    pt_window_input_key(window, XKB_KEY_v, (uint32_t)pt_key_modifier_ctrl, NULL, true);
    pt_check(strcmp(pt_widget_get_text(entry), "hXYZ") == 0);

    pt_window_input_ime_commit(window, "\xE4\xB8\x96\xE7\x95\x8C");
    pt_check(strcmp(pt_widget_get_text(entry), "hXYZ\xE4\xB8\x96\xE7\x95\x8C") == 0);
    pt_window_input_ime_preedit(window, "pre");
    pt_check(entry->preedit != NULL && strcmp(entry->preedit, "pre") == 0);
    pt_check(pt_window_paint(window, canvas));
    {
        pt_layout_ctx ctx = pt_layout_context_for(entry);
        pt_font *font = pt_widget_font_for(entry, &ctx);
        pt_check(font != NULL);
        float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
        float area_x = entry->x + entry->padding_left;
        float area_y = entry->y + entry->padding_top;
        float area_h = entry->height - entry->padding_top - entry->padding_bottom;
        float baseline = area_y + (area_h - font->line_height / scale) * 0.5f +
                         font->ascent / scale;
        float prefix = pt_font_measure(font, entry->text, entry->cursor) / scale;
        float preedit_w =
            pt_font_measure(font, entry->preedit, strlen(entry->preedit)) / scale;
        pt_color background = pt_app_get_theme_color(app, pt_theme_control);
        int x = (int)(area_x + prefix + preedit_w * 0.5f);
        int y = (int)(baseline + 2.75f);
        pt_check(pt_canvas_get_pixel(canvas, x, y) != background);
    }
    pt_window_input_ime_preedit(window, "");
    pt_check(entry->preedit == NULL);

    key_text(window, XKB_KEY_Return, NULL);
    pt_check(state.clicks == 2);

    click_at(window, toggle_rect.x + toggle_rect.w * 0.5f,
             toggle_rect.y + toggle_rect.h * 0.5f);
    pt_check(pt_toggle_get_checked(toggle));
    click_at(window, toggle_rect.x + toggle_rect.w * 0.5f,
             toggle_rect.y + toggle_rect.h * 0.5f);
    pt_check(!pt_toggle_get_checked(toggle));

    float slider_cx = slider_rect.x + slider_rect.w * 0.75f;
    float slider_cy = slider_rect.y + slider_rect.h * 0.5f;
    pt_window_input_pointer_enter(window, slider_cx, slider_cy);
    pt_window_input_pointer_button(window, 0x110u, true, slider_cx, slider_cy, 3, 0);
    pt_window_input_pointer_motion(window, slider_rect.x + slider_rect.w * 0.25f, slider_cy, 1);
    pt_window_input_pointer_button(window, 0x110u, false,
                                   slider_rect.x + slider_rect.w * 0.25f, slider_cy, 4, 0);
    pt_check(pt_slider_get_value(slider) > 0.15f && pt_slider_get_value(slider) < 0.35f);
    pt_check(!slider->sliding);

    pt_widget *before = window->focused;
    pt_window_input_key(window, XKB_KEY_Tab, 0, NULL, true);
    pt_check(window->focused != NULL && window->focused != before);
    pt_window_input_key(window, XKB_KEY_Tab, (uint32_t)pt_key_modifier_shift, NULL, true);
    pt_check(window->focused == before);

    pt_check(pt_window_paint(window, canvas));
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_theme_and_decorations(pt_app *app) {
    pt_window *window = pt_window_create(app, "theme", 320, 220);
    pt_widget *root = pt_window_root(window);
    pt_widget *label = pt_label_create(root, "Theme");
    pt_widget_set_height(label, 24.0f);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(320, 220, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    pt_color titlebar = pt_app_get_theme_color(app, pt_theme_titlebar);
    pt_check(pt_canvas_get_pixel(canvas, 4, 4) == titlebar);
    pt_color background = pt_app_get_theme_color(app, pt_theme_background);
    pt_check(pt_canvas_get_pixel(canvas, 4, 100) == background);

    pt_app_set_color_scheme(app, pt_color_scheme_light);
    pt_check(pt_app_get_color_scheme(app) == pt_color_scheme_light);
    pt_color light_background = pt_app_get_theme_color(app, pt_theme_background);
    pt_check(light_background != background);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_canvas_get_pixel(canvas, 4, 100) == light_background);

    pt_color accent = pt_rgba(0x12, 0x88, 0x44, 0xFF);
    pt_app_set_accent_color(app, accent);
    pt_check(pt_app_get_theme_color(app, pt_theme_accent) == accent);
    pt_check(pt_app_get_theme_color(app, pt_theme_accent_text) != accent);

    pt_window_set_decorated(window, false);
    pt_check_near(window->header_height, 0.0, 0.01);
    pt_check(pt_window_paint(window, canvas));
    pt_rect label_rect = bounds_of(label);
    pt_check_near(label_rect.y, 24.0, 1.0);

    pt_window_set_title(window, "renamed");
    pt_check(strcmp(window->title, "renamed") == 0);
    pt_app_set_font_family(app, "monospace");
    pt_check(strcmp(app->theme.font_family, "monospace") == 0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_timers_and_close(pt_app *app) {
    bool fired = false;
    pt_app_add_timeout(app, 5, quit_callback, &fired);
    pt_check(pt_app_run_for(app, 400) == 0);
    pt_check(fired);
    pt_check(!pt_app_is_running(app));

    bool cancelled = false;
    uint64_t id = pt_app_add_timeout(app, 5, cancelled_callback, &cancelled);
    pt_app_cancel_timeout(app, id);
    pt_check(pt_app_run_for(app, 40) == 0);
    pt_check(!cancelled);

    pt_window *veto_window = pt_window_create(app, "veto", 200, 160);
    bool allow_close = false;
    pt_window_set_close_callback(veto_window, close_callback, &allow_close);
    pt_window_close(veto_window);
    pt_check(pt_window_is_open(veto_window));
    allow_close = true;
    pt_window_close(veto_window);
    pt_check(app->first_window == NULL);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.widget");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("widget");
    }
    test_controls(app);
    test_theme_and_decorations(app);
    test_timers_and_close(app);
    pt_app_destroy(app);
    return pt_test_finish("widget");
}
