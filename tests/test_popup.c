/*
 * Application-owned popup surfaces (1.2 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct popup_state {
    int dismissals;
    bool open_at_dismiss;
    pt_popup *popup;
    bool destroy_in_callback;
} popup_state;

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 10);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 20);
}

static void on_dismiss(pt_popup *popup, void *data) {
    popup_state *state = data;
    ++state->dismissals;
    state->open_at_dismiss = pt_popup_is_open(popup);
    if (state->destroy_in_callback) {
        pt_popup_destroy(popup);
        state->popup = NULL;
    }
}

static pt_widget *fixed_box(float width, float height) {
    pt_widget *box = pt_box_create(NULL, pt_direction_column);
    box->has_background = true;
    box->background = 0x223344FFu;
    pt_widget_set_width(box, width);
    pt_widget_set_height(box, height);
    return box;
}

static void paint(pt_window *window, pt_canvas *canvas) {
    pt_check(pt_window_paint(window, canvas));
}

static void test_create_and_show(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-show", 400, 300);
    pt_widget *root = pt_window_root(window);
    pt_widget *background = pt_button_create(root, "Background");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_widget_focus(background));

    pt_widget *entry = pt_entry_create(NULL, "Search");
    pt_widget *content = pt_box_create(NULL, pt_direction_column);
    pt_widget_set_width(content, 100.0f);
    pt_widget_set_height(content, 60.0f);
    pt_widget_append(content, entry);
    pt_widget_set_a11y_name(content, "Filters");

    pt_popup *popup = pt_popup_create(window, content);
    pt_check(popup != NULL);
    if (!popup) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    pt_check(!pt_popup_is_open(popup));
    pt_check(pt_popup_content(popup) == content);
    pt_check(pt_widget_parent(content) == popup->overlay.panel);
    pt_check(pt_popup_get_placement(popup) == pt_popup_placement_below);
    pt_check(popup->overlay.panel != NULL);
    pt_check(pt_widget_get_a11y_role(popup->overlay.panel) == pt_a11y_role_panel);
    pt_check(strcmp(pt_widget_get_a11y_name(popup->overlay.panel), "Filters") == 0);
    pt_check(!popup->overlay.native);

    pt_popup_anchor anchor = {20.0f, 60.0f, 80.0f, 24.0f};
    pt_check(pt_popup_show(popup, anchor));
    pt_check(pt_popup_is_open(popup));
    pt_check(pt_window_overlays_active(window));
    pt_check(pt_window_overlays_focus_scope(window) == popup->overlay.panel);
    pt_check(window->focused == entry);

    /* Below-left of the anchor, content plus the panel padding. */
    pt_check(popup->overlay.rect.x >= 8.0f);
    pt_check_near(popup->overlay.rect.x, 20.0f, 0.01);
    pt_check_near(popup->overlay.rect.y, 84.0f, 0.01);
    pt_check(popup->overlay.rect.w >= 100.0f);
    pt_check(popup->overlay.rect.h >= 60.0f);
    paint(window, canvas);
    pt_check_near(popup->overlay.rect.y, 84.0f, 0.01);

    /* The content widget receives ordinary keys while the popup is open. */
    pt_window_input_key(window, XKB_KEY_a, 0, "a", true);
    pt_check(strcmp(pt_widget_get_text(entry), "a") == 0);

    pt_popup_dismiss(popup);
    pt_check(!pt_popup_is_open(popup));
    pt_check(!pt_window_overlays_active(window));
    pt_check(window->focused == background);

    /* Dismissal keeps the content tree so the popup can be shown again. */
    pt_check(pt_popup_content(popup) == content);
    pt_check(pt_widget_parent(content) == popup->overlay.panel);
    pt_check(pt_popup_show(popup, (pt_popup_anchor){40.0f, 60.0f, 1.0f, 1.0f}));
    pt_check(pt_popup_is_open(popup));
    pt_check_near(popup->overlay.rect.x, 40.0f, 0.01);
    pt_check_near(popup->overlay.rect.y, 61.0f, 0.01);

    pt_popup_destroy(popup);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_headless_before_show(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-headless", 300, 220);
    pt_widget *root = pt_window_root(window);
    pt_widget *content = pt_box_create(root, pt_direction_column);
    pt_widget *field = pt_entry_create(content, "Type");
    pt_widget_set_width(content, 90.0f);
    pt_widget_set_height(content, 40.0f);
    pt_canvas *canvas = pt_canvas_create(300, 220, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_widget_focus(field));

    /* Creating from the live tree detaches the content and clears focus that
     * pointed into it; the closed popup owns the tree from now on. */
    pt_popup *popup = pt_popup_create(window, content);
    pt_check(popup != NULL);
    if (!popup) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    pt_check(window->focused == NULL);
    pt_check(pt_widget_parent(content) == popup->overlay.panel);
    pt_check(!popup->overlay.native);

    /* No compositor and no shown window yet: the in-window fallback still
     * provides a usable, clamped geometry. */
    pt_check(pt_popup_show(popup, (pt_popup_anchor){20.0f, 100.0f, 20.0f, 20.0f}));
    pt_check(pt_popup_is_open(popup));
    pt_check(window->focused == field);
    pt_check(!popup->overlay.native);
    pt_check(popup->overlay.rect.x >= 8.0f);
    pt_check(popup->overlay.rect.y >= window->header_height + 8.0f);
    pt_check(popup->overlay.rect.x + popup->overlay.rect.w <= 292.0f);
    pt_check(popup->overlay.rect.y + popup->overlay.rect.h <= 212.0f);

    pt_window_show(window);
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_popup_is_open(popup));

    pt_popup_destroy(popup);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_flip_and_slide(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-place", 400, 300);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(canvas != NULL);

    pt_popup *popup = pt_popup_create(window, fixed_box(100.0f, 60.0f));
    pt_check(popup != NULL);
    if (!popup) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    float popup_width = 116.0f;
    float popup_height = 76.0f;

    /* Not enough room below: flip above the anchor. */
    pt_check(pt_popup_show(popup, (pt_popup_anchor){20.0f, 270.0f, 80.0f, 20.0f}));
    pt_check(popup->overlay.rect.y < 270.0f);
    pt_check_near(popup->overlay.rect.y, 270.0f - popup_height, 0.01);
    pt_check(popup->overlay.rect.y + popup->overlay.rect.h <= 292.0f);

    /* Right edge: slide back inside while staying below. */
    pt_check(pt_popup_reposition(popup, (pt_popup_anchor){380.0f, 20.0f, 10.0f, 10.0f}));
    pt_check_near(popup->overlay.rect.x, 400.0f - 8.0f - popup_width, 0.01);
    pt_check(popup->overlay.rect.x + popup->overlay.rect.w <= 392.0f);
    pt_check_near(popup->overlay.rect.y, window->header_height + 8.0f, 0.01);

    /* Explicit above/right/left placements. */
    pt_popup_set_placement(popup, pt_popup_placement_above);
    pt_check(pt_popup_get_placement(popup) == pt_popup_placement_above);
    pt_check(pt_popup_reposition(popup, (pt_popup_anchor){200.0f, 200.0f, 20.0f, 20.0f}));
    pt_check_near(popup->overlay.rect.y, 200.0f - popup_height, 0.01);

    pt_popup_set_placement(popup, pt_popup_placement_right);
    pt_check(pt_popup_reposition(popup, (pt_popup_anchor){200.0f, 100.0f, 20.0f, 20.0f}));
    pt_check_near(popup->overlay.rect.x, 220.0f, 0.01);

    pt_popup_set_placement(popup, pt_popup_placement_left);
    pt_check(pt_popup_reposition(popup, (pt_popup_anchor){200.0f, 100.0f, 20.0f, 20.0f}));
    pt_check_near(popup->overlay.rect.x, 200.0f - popup_width, 0.01);

    /* A left placement with no room flips to the right of the anchor. */
    pt_check(pt_popup_reposition(popup, (pt_popup_anchor){40.0f, 100.0f, 20.0f, 20.0f}));
    pt_check_near(popup->overlay.rect.x, 60.0f, 0.01);

    paint(window, canvas);

    /* A native configure reports the compositor's final flip/slide position;
     * later layout passes keep it until a new anchor or placement request. */
    pt_popup_set_placement(popup, pt_popup_placement_below);
    popup->overlay.native = true;
    popup->native_placed = true;
    popup->overlay.rect.x = 123.0f;
    popup->overlay.rect.y = 77.0f;
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    pt_popup_overlay_layout(popup, &popup->overlay, &ctx);
    pt_check_near(popup->overlay.rect.x, 123.0f, 0.01);
    pt_check_near(popup->overlay.rect.y, 77.0f, 0.01);
    pt_check(pt_popup_reposition(popup, (pt_popup_anchor){60.0f, 120.0f, 20.0f, 20.0f}));
    pt_check(!popup->native_placed);
    pt_check_near(popup->overlay.rect.x, 60.0f, 0.01);
    pt_check_near(popup->overlay.rect.y, 140.0f, 0.01);
    popup->overlay.native = false;

    pt_popup_destroy(popup);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_dismiss_paths(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-dismiss", 480, 340);
    pt_widget *root = pt_window_root(window);
    pt_widget *background = pt_button_create(root, "Background");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(480, 340, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_widget_focus(background));

    popup_state state = {0};
    pt_popup *popup = pt_popup_create(window, fixed_box(80.0f, 40.0f));
    pt_check(popup != NULL);
    if (!popup) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    state.popup = popup;
    pt_popup_set_on_dismiss(popup, on_dismiss, &state);
    pt_check(pt_popup_show(popup, (pt_popup_anchor){100.0f, 80.0f, 40.0f, 20.0f}));
    paint(window, canvas);

    /* Outside click. */
    click_at(window, 5.0f, 5.0f);
    pt_check(state.dismissals == 1);
    pt_check(!state.open_at_dismiss);
    pt_check(!pt_popup_is_open(popup));
    pt_check(window->focused == background);

    /* Escape closes an open popup. */
    pt_check(pt_popup_show(popup, (pt_popup_anchor){100.0f, 80.0f, 40.0f, 20.0f}));
    pt_window_input_key(window, XKB_KEY_Escape, 0, NULL, true);
    pt_check(state.dismissals == 2);
    pt_check(!state.open_at_dismiss);
    pt_check(!pt_popup_is_open(popup));

    /* A compositor grab break follows the same path as popup_done. */
    pt_check(pt_popup_show(popup, (pt_popup_anchor){100.0f, 80.0f, 40.0f, 20.0f}));
    pt_window_overlay_native_done(&popup->overlay);
    pt_check(state.dismissals == 3);
    pt_check(!pt_popup_is_open(popup));

    /* pt_popup_destroy is not a dismissal and must not fire the callback. */
    pt_check(pt_popup_show(popup, (pt_popup_anchor){100.0f, 80.0f, 40.0f, 20.0f}));
    pt_popup_destroy(popup);
    pt_check(state.dismissals == 3);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_callback_self_destroy(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-self", 360, 260);
    pt_window_show(window);
    popup_state state = {0};
    state.destroy_in_callback = true;
    pt_popup *popup = pt_popup_create(window, fixed_box(60.0f, 40.0f));
    pt_check(popup != NULL);
    if (!popup) {
        pt_window_free(window);
        return;
    }
    state.popup = popup;
    pt_popup_set_on_dismiss(popup, on_dismiss, &state);
    pt_check(pt_popup_show(popup, (pt_popup_anchor){20.0f, 20.0f, 10.0f, 10.0f}));
    pt_popup_dismiss(popup);
    pt_check(state.dismissals == 1);
    pt_check(state.popup == NULL);
    pt_window_free(window);
}

static void destroy_popup_on_click(pt_widget *widget, void *data) {
    (void)widget;
    popup_state *state = data;
    ++state->dismissals;
    pt_popup_destroy(state->popup);
    state->popup = NULL;
}

static void test_destroy_from_content_callback(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-inner-destroy", 360, 260);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 260, 1.0f);
    pt_check(canvas != NULL);
    popup_state state = {0};
    pt_widget *button = pt_button_create(NULL, "Act");
    pt_button_set_on_click(button, destroy_popup_on_click, &state);
    pt_popup *popup = pt_popup_create(window, button);
    pt_check(popup != NULL);
    if (!popup) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    state.popup = popup;
    pt_check(pt_popup_show(popup, (pt_popup_anchor){40.0f, 80.0f, 60.0f, 24.0f}));
    paint(window, canvas);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(button, &x, &y, &w, &h);
    pt_check(w > 0.0f && h > 0.0f);
    click_at(window, x + w * 0.5f, y + h * 0.5f);
    pt_check(state.dismissals == 1);
    pt_check(state.popup == NULL);
    pt_check(!pt_window_overlays_active(window));
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_window_destroy_cascade(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-cascade", 320, 240);
    pt_window_show(window);
    pt_popup *popup = pt_popup_create(window, fixed_box(70.0f, 50.0f));
    pt_check(popup != NULL);
    if (popup) {
        pt_check(pt_popup_show(popup, (pt_popup_anchor){10.0f, 10.0f, 5.0f, 5.0f}));
        /* The window owns the popup: this releases both the popup and the
         * content tree. The handle must not be used afterwards. */
    }
    pt_window_free(window);
}

static void test_create_destroy_cycles(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-cycles", 300, 220);
    pt_window_show(window);
    for (int i = 0; i < 64; ++i) {
        pt_popup *popup = pt_popup_create(window, fixed_box(40.0f, 20.0f));
        pt_check(popup != NULL);
        if (!popup) {
            break;
        }
        if ((i & 1) == 0) {
            pt_check(pt_popup_show(popup,
                                   (pt_popup_anchor){10.0f + (float)i, 60.0f, 20.0f, 10.0f}));
        }
        pt_popup_destroy(popup);
    }
    pt_check(!pt_window_overlays_active(window));
    pt_window_free(window);
}

static void test_limit(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-limit", 320, 240);
    pt_window_show(window);
    pt_popup *popups[PT_POPUP_MAX_OPEN + 1];
    for (int i = 0; i <= PT_POPUP_MAX_OPEN; ++i) {
        popups[i] = pt_popup_create(window, fixed_box(20.0f, 20.0f));
        pt_check(popups[i] != NULL);
    }
    for (int i = 0; i < PT_POPUP_MAX_OPEN; ++i) {
        pt_check(pt_popup_show(popups[i],
                               (pt_popup_anchor){20.0f + (float)i, 20.0f, 8.0f, 8.0f}));
    }
    pt_check(!pt_popup_show(popups[PT_POPUP_MAX_OPEN],
                            (pt_popup_anchor){20.0f, 20.0f, 8.0f, 8.0f}));
    pt_check(pt_get_last_error() != NULL && pt_get_last_error()[0] != '\0');
    pt_check(!pt_popup_is_open(popups[PT_POPUP_MAX_OPEN]));

    /* Closing one slot admits the next popup. */
    pt_popup_dismiss(popups[0]);
    pt_check(pt_popup_show(popups[PT_POPUP_MAX_OPEN],
                           (pt_popup_anchor){20.0f, 20.0f, 8.0f, 8.0f}));
    pt_check(pt_popup_is_open(popups[PT_POPUP_MAX_OPEN]));

    for (int i = 0; i <= PT_POPUP_MAX_OPEN; ++i) {
        pt_popup_destroy(popups[i]);
    }
    pt_window_free(window);
}

static void test_invalid(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-invalid", 320, 240);
    pt_window *other = pt_window_create(app, "popup-other", 320, 240);
    pt_window_show(window);
    pt_window_show(other);

    pt_widget *orphan = fixed_box(10.0f, 10.0f);
    pt_check(pt_popup_create(NULL, orphan) == NULL);
    pt_widget_free_tree(orphan);
    pt_check(pt_popup_create(window, NULL) == NULL);
    pt_check(pt_popup_create(window, pt_window_root(window)) == NULL);
    pt_widget *foreign = pt_button_create(pt_window_root(other), "Other");
    pt_check(pt_popup_create(window, foreign) == NULL);

    pt_popup *popup = pt_popup_create(window, fixed_box(10.0f, 10.0f));
    pt_check(popup != NULL);
    if (popup) {
        pt_check(pt_popup_show(NULL, (pt_popup_anchor){0.0f, 0.0f, 1.0f, 1.0f}) == false);
        pt_check(pt_popup_show(popup, (pt_popup_anchor){NAN, 0.0f, 1.0f, 1.0f}) == false);
        pt_check(pt_popup_show(popup, (pt_popup_anchor){0.0f, 0.0f, -1.0f, 1.0f}) == false);
        pt_check(!pt_popup_is_open(popup));
        pt_check(pt_popup_reposition(popup, (pt_popup_anchor){0.0f, 0.0f, 1.0f, 1.0f}) ==
                 false);
        pt_popup_dismiss(NULL);
        pt_popup_destroy(NULL);
        pt_check(pt_popup_is_open(NULL) == false);
        pt_check(pt_popup_content(NULL) == NULL);
        pt_check(pt_popup_get_placement(NULL) == pt_popup_placement_below);
        pt_popup_set_placement(NULL, pt_popup_placement_above);
        pt_popup_set_on_dismiss(NULL, NULL, NULL);
        /* Zero width/height anchors are legal and treated as one pixel. */
        pt_check(pt_popup_show(popup, (pt_popup_anchor){30.0f, 40.0f, 0.0f, 0.0f}));
        pt_check_near(popup->overlay.rect.y, window->header_height + 8.0f, 0.01);
        pt_popup_destroy(popup);
    }
    pt_window_free(other);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.popup");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("popup");
    }
    test_create_and_show(app);
    test_headless_before_show(app);
    test_flip_and_slide(app);
    test_dismiss_paths(app);
    test_callback_self_destroy(app);
    test_destroy_from_content_callback(app);
    test_window_destroy_cascade(app);
    test_create_destroy_cycles(app);
    test_limit(app);
    test_invalid(app);
    pt_app_destroy(app);
    return pt_test_finish("popup");
}