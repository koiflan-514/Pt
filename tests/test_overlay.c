/*
 * Overlay stack checks: menus, dialogs, toasts and tooltips.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct overlay_state {
    int clicks;
    bool open_at_click;
    int closes;
    pt_menu *menu;
} overlay_state;

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
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 10);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 20);
}

static pt_widget *nth_button(pt_widget *panel, int index) {
    int seen = 0;
    for (pt_widget *child = panel ? panel->first_child : NULL; child;
         child = child->next_sibling) {
        if (child->kind == pt_widget_button) {
            if (seen == index) {
                return child;
            }
            ++seen;
        }
    }
    return NULL;
}

static void on_menu_item(pt_widget *widget, void *data) {
    (void)widget;
    overlay_state *state = data;
    ++state->clicks;
    if (state->menu) {
        state->open_at_click = pt_menu_is_open(state->menu);
    }
}

static void on_dialog_close(pt_dialog *dialog, void *data) {
    (void)dialog;
    overlay_state *state = data;
    ++state->closes;
}

static void test_menu(pt_app *app) {
    pt_window *window = pt_window_create(app, "menu", 560, 420);
    pt_widget *root = pt_window_root(window);
    pt_widget *focus_button = pt_button_create(root, "Focus me");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(560, 420, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(focus_button));

    overlay_state state = {0};
    pt_menu *menu = pt_menu_create(window);
    pt_check(menu != NULL);
    state.menu = menu;
    pt_menu_add_item(menu, "Open", "Ctrl+O", on_menu_item, &state);
    pt_menu_add_separator(menu);
    pt_menu_add_item(menu, "Quit", "Ctrl+Q", on_menu_item, &state);
    pt_menu_show_at(menu, 40.0f, 60.0f);
    pt_check(pt_menu_is_open(menu));
    pt_check(pt_window_overlays_active(window));
    pt_check(!pt_window_overlays_modal(window));
    pt_check(window->focused == nth_button(menu->overlay.panel, 0));
    pt_check(pt_window_overlays_focus_scope(window) == menu->overlay.panel);

    pt_check(pt_window_paint(window, canvas));
    pt_rect first = bounds_of(nth_button(menu->overlay.panel, 0));
    pt_rect second = bounds_of(nth_button(menu->overlay.panel, 1));
    pt_check(first.w > 0.0f && first.h > 0.0f);
    pt_check(second.y > first.y);
    pt_check(menu->overlay.rect.x >= 7.0f);
    pt_check(menu->overlay.rect.y >= window->header_height + 7.0f);

    pt_window_input_key(window, XKB_KEY_Down, 0, NULL, true);
    pt_check(window->focused == nth_button(menu->overlay.panel, 1));
    pt_window_input_key(window, XKB_KEY_Up, 0, NULL, true);
    pt_check(window->focused == nth_button(menu->overlay.panel, 0));

    pt_window_input_key(window, XKB_KEY_Escape, 0, NULL, true);
    pt_check(!pt_menu_is_open(menu));
    pt_check(!pt_window_overlays_active(window));
    pt_check(window->focused == focus_button);

    state.open_at_click = true;
    pt_menu_show_at(menu, 60.0f, 80.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_rect quit = bounds_of(nth_button(menu->overlay.panel, 1));
    click_at(window, quit.x + quit.w * 0.5f, quit.y + quit.h * 0.5f);
    pt_check(state.clicks == 1);
    pt_check(!state.open_at_click);
    pt_check(!pt_menu_is_open(menu));
    pt_check(window->focused == focus_button);

    pt_menu_destroy(menu);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_dialog(pt_app *app) {
    pt_window *window = pt_window_create(app, "dialog", 600, 460);
    pt_widget *root = pt_window_root(window);
    pt_widget *background = pt_button_create(root, "Background");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(600, 460, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_widget_focus(background));

    overlay_state state = {0};
    pt_dialog *dialog = pt_dialog_create(window, "Delete file?", "This cannot be undone.");
    pt_check(dialog != NULL);
    pt_dialog_add_action(dialog, "Cancel", false, on_menu_item, &state);
    pt_dialog_add_action(dialog, "Delete", true, on_menu_item, &state);
    pt_dialog_set_on_close(dialog, on_dialog_close, &state);
    pt_dialog_show(dialog);
    pt_check(pt_dialog_is_open(dialog));
    pt_check(pt_window_overlays_modal(window));
    pt_check(dialog->overlay.panel->a11y_role == pt_a11y_role_dialog);
    pt_check((pt_widget_get_a11y_state(dialog->overlay.panel) & pt_a11y_state_modal) != 0);
    pt_check(strcmp(pt_widget_get_a11y_name(dialog->overlay.panel), "Delete file?") == 0);
    pt_check(window->focused == nth_button(dialog->actions_row, 0));

    pt_check(pt_window_paint(window, canvas));
    pt_rect cancel = bounds_of(nth_button(dialog->actions_row, 0));
    pt_rect remove = bounds_of(nth_button(dialog->actions_row, 1));
    pt_check(cancel.w > 0.0f && remove.w > 0.0f);
    pt_check(dialog->overlay.rect.w >= 360.0f);
    pt_check(dialog->overlay.rect.x >= 0.0f);
    pt_check(dialog->overlay.rect.y >= window->header_height);

    pt_window_input_key(window, XKB_KEY_Tab, 0, NULL, true);
    pt_check(window->focused == nth_button(dialog->actions_row, 1));
    pt_window_input_key(window, XKB_KEY_Tab, (uint32_t)pt_key_modifier_shift, NULL, true);
    pt_check(window->focused == nth_button(dialog->actions_row, 0));

    click_at(window, 5.0f, 5.0f);
    pt_check(pt_dialog_is_open(dialog));
    pt_check(pt_window_overlays_modal(window));

    click_at(window, remove.x + remove.w * 0.5f, remove.y + remove.h * 0.5f);
    pt_check(state.clicks == 1);
    pt_check(state.closes == 1);
    pt_check(!pt_dialog_is_open(dialog));
    pt_check(window->focused == background);
    pt_check(!pt_window_overlays_active(window));

    pt_dialog_show(dialog);
    pt_check(pt_dialog_is_open(dialog));
    pt_window_input_key(window, XKB_KEY_Escape, 0, NULL, true);
    pt_check(!pt_dialog_is_open(dialog));
    pt_check(state.closes == 2);

    pt_dialog_destroy(dialog);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_toasts(pt_app *app) {
    pt_window *window = pt_window_create(app, "toasts", 520, 380);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 380, 1.0f);
    pt_check(canvas != NULL);

    pt_toast *first = pt_window_show_toast(window, "Project saved", pt_toast_success, 0);
    pt_toast *second = pt_window_show_toast(window, "Build failed", pt_toast_error, 0);
    pt_check(first != NULL && second != NULL);
    pt_check(pt_toast_is_visible(first) && pt_toast_is_visible(second));
    pt_check(pt_window_paint(window, canvas));
    pt_check(first->overlay.rect.w > 100.0f);
    pt_check(first->overlay.rect.y > second->overlay.rect.y);
    pt_check(first->overlay.rect.y + first->overlay.rect.h <= 380.0f);
    pt_check(second->overlay.rect.x + second->overlay.rect.w <= 520.0f);
    pt_check(first->overlay.panel->a11y_role == pt_a11y_role_alert);

    pt_toast_dismiss(second);
    pt_check(!pt_toast_is_visible(second));
    pt_check(pt_toast_is_visible(first));

    pt_toast *timed = pt_window_show_toast(window, "Autosaved", pt_toast_info, 20);
    pt_check(timed != NULL);
    pt_check(pt_toast_is_visible(timed));
    pt_app_run_for(app, 200);
    pt_check(!pt_toast_is_visible(timed));
    pt_check(pt_toast_is_visible(first));

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_tooltip(pt_app *app) {
    pt_window *window = pt_window_create(app, "tooltip", 480, 340);
    pt_widget *root = pt_window_root(window);
    pt_widget *button = pt_button_create(root, "Hover");
    pt_widget_set_tooltip(button, "Runs the build");
    pt_widget_set_tooltip_delay(button, 0);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(480, 340, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_rect bounds = bounds_of(button);

    pt_window_input_pointer_enter(window, bounds.x + bounds.w * 0.5f,
                                  bounds.y + bounds.h * 0.5f);
    pt_check(window->hovered == button);
    pt_app_run_for(app, 60);

    pt_overlay *tooltip = NULL;
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay->kind == pt_overlay_kind_tooltip) {
            tooltip = overlay;
        }
    }
    pt_check(tooltip != NULL);
    if (tooltip) {
        pt_layout_ctx ctx = pt_layout_context_for(window->root);
        pt_window_overlays_layout(window, &ctx);
        pt_check(tooltip->panel != NULL);
        pt_check(tooltip->rect.w > 0.0f && tooltip->rect.h > 0.0f);
        pt_check(tooltip->panel->a11y_role == pt_a11y_role_tooltip);
        pt_check(strcmp(pt_widget_get_a11y_name(tooltip->panel), "Runs the build") == 0);
    }

    pt_window_input_pointer_leave(window);
    tooltip = NULL;
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay->kind == pt_overlay_kind_tooltip) {
            tooltip = overlay;
        }
    }
    pt_check(tooltip == NULL);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.overlay");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("overlay");
    }
    test_menu(app);
    test_dialog(app);
    test_toasts(app);
    test_tooltip(app);
    pt_app_destroy(app);
    return pt_test_finish("overlay");
}
