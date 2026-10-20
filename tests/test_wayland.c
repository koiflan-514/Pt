/*
 * Live Wayland smoke test. Skips cleanly when no compositor is available.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/wayland/wayland_internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    (void)table;
    (void)data;
    char text[32];
    snprintf(text, sizeof(text), "%u:%u", row, column);
    pt_widget_set_text(cell, text);
}

static void quit_callback(pt_app *app, void *data) {
    (void)data;
    pt_app_quit(app);
}

static double process_cpu_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        return -1.0;
    }
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

/* A real desktop can deliver keys or pointer motion to the focused entry while
 * the idle slice runs. Mark the slice instead of counting the user's work as
 * Pt's, and let the guard retry. Returning false keeps normal input routing. */
static bool external_key_seen;

static bool note_external_key(pt_window *window, uint32_t key, uint32_t modifiers, void *data) {
    (void)window;
    (void)key;
    (void)modifiers;
    (void)data;
    external_key_seen = true;
    return false;
}

int main(void) {
    unsetenv("PT_HEADLESS");
    if (!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET")) {
        printf("skip wayland (no WAYLAND_DISPLAY)\n");
        return 0;
    }
    pt_app *app = pt_app_create("pt.tests.wayland");
    if (!app) {
        printf("skip wayland (%s)\n", pt_get_last_error());
        return 0;
    }
    pt_window *window = pt_window_create(app, "Pt Wayland smoke test", 400, 300);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        return pt_test_finish("wayland");
    }
    pt_widget *label = pt_label_create(pt_window_root(window), "Wayland");
    pt_widget *entry = pt_entry_create(pt_window_root(window), "input");
    pt_widget *button = pt_button_create(pt_window_root(window), "OK");
    pt_widget_set_height(label, 24.0f);
    pt_widget *table = pt_table_create(pt_window_root(window));
    pt_table_set_column_count(table, 2);
    pt_table_set_row_count(table, 20);
    pt_table_set_on_bind_cell(table, bind_cell, NULL);
    pt_widget_set_height(table, 120.0f);
    pt_window_show(window);

    pt_platform_clipboard_set(window, "pt wayland clipboard");
    char *clipboard = pt_platform_clipboard_get(window);
    pt_check(clipboard != NULL && strcmp(clipboard, "pt wayland clipboard") == 0);
    free(clipboard);

    pt_app_add_timeout(app, 220, quit_callback, NULL);
    pt_check(pt_app_run_for(app, 2000) == 0);
    pt_check(!pt_app_is_running(app));
    pt_check(window->configured);
    pt_check(window->render_scale > 0.5f);
    pt_check(window->width > 0 && window->height > 0);
    pt_check(window->platform_window != NULL);
    pt_check(pt_window_is_open(window));
    pt_check(entry->visible && button->visible);
    pt_widget *first_cell = pt_table_row_cell(table, 0, 0);
    pt_check(first_cell != NULL && strcmp(first_cell->text, "0:0") == 0);

    /* The empty text-input acknowledgement is invisible to a headless test
     * once it reaches the protocol. Focus an entry on a real compositor and
     * measure the process's own CPU over a fixed slice: a state-push loop turns
     * this into tens of percent of a core, while an idle entry is a small
     * fraction even with caret blink.
     *
     * A live desktop can still type into the focused entry or move the pointer
     * across it. That work belongs to the user, not to Pt, so a slice with a
     * key, text change, focus change or pointer movement is discarded. The
     * official live target runs in a nested compositor; there is no input at
     * all, and one clean slice is available. On a busy desktop the guard
     * reports a skip rather than a false regression after four attempts. */
    pt_widget_set_text(entry, "idle guard");
    pt_check(pt_widget_focus(entry));
    pt_window_input_keyboard_focus(window, true, 0);
    pt_app_add_timeout(app, 100, quit_callback, NULL);
    pt_check(pt_app_run_for(app, 1000) == 0);
    pt_check(window->keyboard_focused);
    pt_check(window->focused == entry);
    pt_window_set_key_callback(window, note_external_key, NULL);
    bool idle_clean = false;
    bool idle_clock_ok = true;
    double idle_cpu = 0.0;
    int idle_run = 0;
    for (int attempt = 0; attempt < 4 && !idle_clean; ++attempt) {
        if (attempt > 0) {
            pt_window_input_keyboard_focus(window, false, 0);
            pt_widget_set_text(entry, "idle guard");
            (void)pt_widget_focus(entry);
            pt_window_input_keyboard_focus(window, true, 0);
            pt_app_add_timeout(app, 100, quit_callback, NULL);
            if (pt_app_run_for(app, 1000) != 0) {
                break;
            }
        }
        float pointer_x = window->pointer_x;
        float pointer_y = window->pointer_y;
        pt_widget *hovered = window->hovered;
        external_key_seen = false;
        double idle_cpu_start = process_cpu_ms();
        pt_app_add_timeout(app, 5000, quit_callback, NULL);
        idle_run = pt_app_run_for(app, 7000);
        double idle_cpu_end = process_cpu_ms();
        if (idle_cpu_start < 0.0 || idle_cpu_end < 0.0) {
            idle_clock_ok = false;
            break;
        }
        const char *text = pt_widget_get_text(entry);
        bool clean = !external_key_seen && text && strcmp(text, "idle guard") == 0 &&
                     window->keyboard_focused && window->focused == entry &&
                     window->pointer_x == pointer_x && window->pointer_y == pointer_y &&
                     window->hovered == hovered;
        if (clean) {
            idle_cpu = idle_cpu_end - idle_cpu_start;
            idle_clean = true;
        } else {
            printf("focused-entry idle guard retry: external input reached the window\n");
        }
    }
    if (!idle_clean) {
        printf("focused-entry idle guard skipped: desktop input was continuous\n");
    }
    printf("focused-entry idle CPU: %.1f ms over 5000 ms%s\n", idle_cpu,
           idle_clean ? "" : " (skipped)");
    pt_check(idle_run == 0);
    pt_check(idle_clock_ok);
    pt_check(idle_cpu >= 0.0);
    pt_check(!idle_clean || idle_cpu < 1500.0);

    pt_window_set_popup_policy(window, pt_popup_policy_window);
    pt_check(pt_window_get_popup_policy(window) == pt_popup_policy_window);
    pt_menu *window_menu = pt_menu_create(window);
    pt_menu_add_item(window_menu, "Window", NULL, NULL, NULL);
    pt_menu_show_at(window_menu, 20.0f, 20.0f);
    pt_check(pt_menu_is_open(window_menu));
    pt_check(!window_menu->overlay.native);
    pt_check(window_menu->overlay.popup == NULL);
    pt_menu_dismiss(window_menu);
    pt_check(!pt_menu_is_open(window_menu));
    pt_menu_destroy(window_menu);

    pt_check(pt_platform_popup_supported(window));
    pt_window_set_popup_policy(window, pt_popup_policy_auto);
    pt_check(pt_window_get_popup_policy(window) == pt_popup_policy_auto);
    /* A grabbed xdg_popup is the compositor's to end: a focus change or a
     * competing grab makes it send popup_done, and Pt then closes the overlay
     * rather than keep a surface no compositor will map. On a live desktop that
     * can land inside the wait below, so retry a dismissed attempt and assert on
     * the one that survived. */
    pt_menu *menu = pt_menu_create(window);
    pt_menu_add_item(menu, "Alpha", "Ctrl+A", NULL, NULL);
    pt_menu_add_separator(menu);
    pt_menu_add_item(menu, "Beta", NULL, NULL, NULL);
    bool menu_held = false;
    int menu_slices = 0;
    bool external_pointer_seen = false;
    external_key_seen = false;
    for (int step = 0; step < 20 && !menu_held; ++step) {
        float pointer_x = window->pointer_x;
        float pointer_y = window->pointer_y;
        pt_menu_show_at(menu, 24.0f, 24.0f);
        pt_app_add_timeout(app, 260, quit_callback, NULL);
        menu_slices = pt_app_run_for(app, 2000);
        if (window->pointer_x != pointer_x || window->pointer_y != pointer_y) {
            external_pointer_seen = true;
        }
        menu_held = menu->overlay.open && menu->overlay.popup != NULL;
        if (!menu_held) {
            pt_menu_dismiss(menu);
        }
    }
    pt_check(menu_slices == 0);
    if (!menu_held && (external_key_seen || external_pointer_seen)) {
        printf("# menu popup check skipped: external input kept dismissing the grab\n");
    } else {
        pt_check(menu_held);
        pt_check(pt_menu_is_open(menu));
        pt_check(menu->overlay.native);
        pt_check(menu->overlay.popup != NULL);
        pt_check(menu->overlay.open);
        pt_check(menu->overlay.rect.w > 0.0f && menu->overlay.rect.h > 0.0f);
        pt_widget *first_item = menu->overlay.panel ? menu->overlay.panel->first_child : NULL;
        pt_check(first_item != NULL);
        if (first_item) {
            pt_widget *hit = pt_window_overlay_hit_test(
                window, menu->overlay.rect.x + first_item->x + 2.0f,
                menu->overlay.rect.y + first_item->y + 2.0f);
            pt_check(hit == first_item || (hit && pt_widget_is_descendant(hit, first_item)));
            pt_check(pt_window_overlay_hit_test(window, -50.0f, -50.0f) == NULL);
        }
    }
    pt_menu_dismiss(menu);
    pt_check(menu->overlay.popup == NULL);
    pt_check(!pt_menu_is_open(menu));
    pt_menu_destroy(menu);

    /* A compositor pointer sends live wl_pointer enters while the loop runs, and
     * a live enter replaces any synthetic hover and cancels the pending tip, so
     * a fixed delay can be restarted forever. Arm with no delay, follow the live
     * hover when it moves, and retry until a slice completes with a tip whose
     * popup the compositor actually kept. */
    pt_widget *tip_target = window->hovered ? window->hovered : label;
    pt_overlay *tooltip = NULL;
    external_key_seen = false;
    external_pointer_seen = false;
    for (int step = 0; step < 40 && !tooltip; ++step) {
        float tip_x =
            tip_target == label ? tip_target->x + tip_target->width * 0.5f : window->pointer_x;
        float tip_y =
            tip_target == label ? tip_target->y + tip_target->height * 0.5f : window->pointer_y;
        pt_widget_set_tooltip(tip_target, "Wayland tooltip");
        pt_widget_set_tooltip_delay(tip_target, 0);
        pt_window_input_pointer_motion(window, tip_x, tip_y, 0);
        pt_app_add_timeout(app, 100, quit_callback, NULL);
        pt_app_run_for(app, 2000);
        if (window->pointer_x != tip_x || window->pointer_y != tip_y) {
            external_pointer_seen = true;
        }
        for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
            if (overlay->open && overlay->kind == pt_overlay_kind_tooltip &&
                overlay->popup != NULL) {
                tooltip = overlay;
                break;
            }
        }
        if (!tooltip && window->hovered && window->hovered != tip_target) {
            tip_target = window->hovered;
        }
    }
    if (!tooltip && (external_key_seen || external_pointer_seen)) {
        printf("# tooltip popup check skipped: external input kept dismissing the passive tip\n");
    } else {
        pt_check(tooltip != NULL);
    }
    if (tooltip) {
        pt_check(tooltip->native);
        pt_check(tooltip->popup != NULL);
    }
    pt_window_overlays_dismiss_tooltip(window);
    tooltip = NULL;
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (overlay->open && overlay->kind == pt_overlay_kind_tooltip) {
            tooltip = overlay;
            break;
        }
    }
    pt_check(tooltip == NULL);
    pt_platform *platform = app->platform;
    pt_check(platform != NULL);
    if (platform) {
        pt_check(platform->popups == NULL);
    }
    pt_window_set_key_callback(window, NULL, NULL);

    int pixel_width = (int)((float)window->width * window->render_scale);
    int pixel_height = (int)((float)window->height * window->render_scale);
    pt_canvas *canvas = pt_canvas_create(pixel_width, pixel_height, window->render_scale);
    pt_check(canvas != NULL);
    if (canvas) {
        pt_check(pt_window_paint(window, canvas));
        pt_check(pt_canvas_get_pixel(canvas, 2, 2) !=
                 pt_app_get_theme_color(app, pt_theme_background));
        pt_canvas_destroy(canvas);
    }

    pt_window_close(window);
    pt_check(app->first_window == NULL);
    pt_app_destroy(app);
    return pt_test_finish("wayland");
}
