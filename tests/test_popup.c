/*
 * Application-owned popup surfaces (1.2 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "fake_compositor.h"
#include "harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xdg-shell-client-protocol.h>

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

/* Resolves the corner of the anchor rectangle named by the positioner anchor,
 * then applies the gravity direction. xdg_positioner anchor/gravity values are
 * plain enums, not bit flags, so both steps use switches. Before the compositor
 * flips or slides the surface, this is the popup origin the request asks for. */
static float positioner_anchor_x(const pt_popup_positioner_spec *spec) {
    switch (spec->anchor) {
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
        return (float)(spec->anchor_x + spec->anchor_width);
    default:
        return (float)spec->anchor_x;
    }
}

static float positioner_anchor_y(const pt_popup_positioner_spec *spec) {
    switch (spec->anchor) {
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
        return (float)(spec->anchor_y + spec->anchor_height);
    default:
        return (float)spec->anchor_y;
    }
}

static void positioner_origin(const pt_popup_positioner_spec *spec, float width, float height,
                              float *x, float *y) {
    float anchor_x = positioner_anchor_x(spec);
    float anchor_y = positioner_anchor_y(spec);
    switch (spec->gravity) {
    case XDG_POSITIONER_GRAVITY_TOP_LEFT:
    case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
        *y = anchor_y - height;
        break;
    default:
        *y = anchor_y;
        break;
    }
    switch (spec->gravity) {
    case XDG_POSITIONER_GRAVITY_TOP_LEFT:
    case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
        *x = anchor_x - width;
        break;
    default:
        *x = anchor_x;
        break;
    }
}

static void test_positioner_spec(void) {
    static const struct {
        pt_popup_placement placement;
        uint32_t anchor;
        uint32_t gravity;
    } cases[] = {
        {pt_popup_placement_below, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
         XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT},
        {pt_popup_placement_above, XDG_POSITIONER_ANCHOR_TOP_LEFT,
         XDG_POSITIONER_GRAVITY_TOP_RIGHT},
        {pt_popup_placement_right, XDG_POSITIONER_ANCHOR_TOP_RIGHT,
         XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT},
        {pt_popup_placement_left, XDG_POSITIONER_ANCHOR_TOP_LEFT,
         XDG_POSITIONER_GRAVITY_BOTTOM_LEFT},
    };
    const uint32_t constraint = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y;
    for (size_t i = 0; i < pt_len(cases); ++i) {
        pt_popup_positioner_spec spec;
        pt_popup_positioner_spec_for(pt_overlay_kind_popup, cases[i].placement, 30, 120, 80,
                                     24, &spec);
        pt_check(spec.anchor_x == 30);
        pt_check(spec.anchor_y == 120);
        pt_check(spec.anchor_width == 80);
        pt_check(spec.anchor_height == 24);
        pt_check(spec.anchor == cases[i].anchor);
        pt_check(spec.gravity == cases[i].gravity);
        pt_check(spec.constraint == constraint);
    }

    /* Zero or negative extents become the one-pixel anchor xdg-shell requires. */
    pt_popup_positioner_spec point;
    pt_popup_positioner_spec_for(pt_overlay_kind_popup, pt_popup_placement_above, -4, 7, 0,
                                 -2, &point);
    pt_check(point.anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT);
    pt_check(point.gravity == XDG_POSITIONER_GRAVITY_TOP_RIGHT);
    pt_check(point.anchor_width == 1);
    pt_check(point.anchor_height == 1);

    /* Menus, tooltips and combo boxes keep their point-anchored down-right
     * placement regardless of the legacy placement value. */
    pt_popup_positioner_spec menu;
    pt_popup_positioner_spec_for(pt_overlay_kind_menu, pt_popup_placement_above, 10, 20, 1,
                                 1, &menu);
    pt_check(menu.anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    pt_check(menu.gravity == XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    pt_check(menu.constraint == constraint);
    /* A missing result sink is ignored rather than dereferenced. */
    pt_popup_positioner_spec_for(pt_overlay_kind_menu, pt_popup_placement_left, 10, 20, 1, 1,
                                 NULL);
}

/* The native decision and the in-window fallback must describe the same
 * geometry: same edge of the anchor rectangle, same growth direction. */
static void test_positioner_matches_fallback(pt_app *app) {
    pt_window *window = pt_window_create(app, "popup-positioner", 400, 300);
    pt_window_show(window);
    pt_popup *popup = pt_popup_create(window, fixed_box(100.0f, 60.0f));
    pt_check(popup != NULL);
    if (!popup) {
        pt_window_free(window);
        return;
    }
    const pt_popup_anchor anchor = {150.0f, 140.0f, 80.0f, 24.0f};
    static const pt_popup_placement placements[] = {
        pt_popup_placement_below,
        pt_popup_placement_above,
        pt_popup_placement_right,
        pt_popup_placement_left,
    };
    for (size_t i = 0; i < pt_len(placements); ++i) {
        pt_popup_set_placement(popup, placements[i]);
        pt_check(pt_popup_show(popup, anchor));
        /* Headless: no compositor placement, so the panel rectangle comes from
         * the in-window fallback. It must ask for the same origin the native
         * positioner requests, or live and headless placements drift apart. */
        pt_check(!popup->overlay.native);
        pt_popup_positioner_spec spec;
        pt_popup_positioner_spec_for(pt_overlay_kind_popup, popup->placement, 150, 140, 80, 24,
                                     &spec);
        float x = 0.0f;
        float y = 0.0f;
        positioner_origin(&spec, popup->overlay.rect.w, popup->overlay.rect.h, &x, &y);
        pt_check_near(x, popup->overlay.rect.x, 0.01);
        pt_check_near(y, popup->overlay.rect.y, 0.01);
    }

    /* Above: the popup's bottom edge sits on the anchor's top edge and its left
     * edge on the anchor's left edge. The old TOP_RIGHT/BOTTOM_LEFT pair
     * reported a downward, overlapping origin instead. */
    pt_popup_set_placement(popup, pt_popup_placement_above);
    pt_check(pt_popup_show(popup, anchor));
    pt_check_near(popup->overlay.rect.x, 150.0f, 0.01);
    pt_check_near(popup->overlay.rect.y + popup->overlay.rect.h, 140.0f, 0.01);
    pt_check(popup->overlay.rect.y < 140.0f);

    pt_popup_destroy(popup);
    pt_window_free(window);
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

/* Native popup placement over the fake compositor wire: the positioner
 * setters, xdg_popup.grab, compositor configure and repositioned/popup_done
 * events all travel through the real protocol path. */
static void wait_popup_native(fake_compositor *fc, pt_app *app, pt_popup *popup) {
    for (int i = 0; i < 300; ++i) {
        if (popup->overlay.popup && popup->native_placed) {
            return;
        }
        fake_compositor_dispatch(fc, app, 5);
    }
}

static void test_native_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "%s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.popup.wire");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_window *window = pt_window_create(app, "popup-wire", 400, 300);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window_show(window);
    for (int i = 0; i < 300 && !window->configured; ++i) {
        fake_compositor_dispatch(&fc, app, 5);
    }
    pt_check(window->configured);
    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    fake_compositor_pump(&fc, app, 80);
    pt_check(fake_compositor_sync(&fc, app));
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);

    /* A pointer button serial is what xdg_popup.grab carries. */
    fake_compositor_pointer_enter(&fc, 1001u, surface, 200.0, 150.0);
    /* A press (release would overwrite the cached serial) is the event a real
     * client answers with xdg_popup.grab. */
    fake_compositor_pointer_button(&fc, 1002u, 10u, 0x110u, true);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->pointer_serial == 1002u);

    popup_state state = {0};
    pt_popup *popup = pt_popup_create(window, fixed_box(100.0f, 60.0f));
    pt_check(popup != NULL);
    if (!popup) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    state.popup = popup;
    pt_popup_set_on_dismiss(popup, on_dismiss, &state);
    const pt_popup_anchor anchor = {30.0f, 120.0f, 80.0f, 24.0f};
    static const struct {
        pt_popup_placement placement;
        uint32_t anchor;
        uint32_t gravity;
        bool reposition;
        int32_t expected_x;
        int32_t expected_y;
    } cases[] = {
        {pt_popup_placement_below, 6u, 8u, false, 30, 144},  /* bottom_left / bottom_right */
        {pt_popup_placement_above, 5u, 7u, true, 30, 44},    /* top_left / top_right */
        {pt_popup_placement_right, 7u, 8u, true, 110, 120},  /* top_right / bottom_right */
        {pt_popup_placement_left, 5u, 6u, true, -86, 120},   /* top_left / bottom_left */
    };
    for (size_t i = 0; i < pt_len(cases); ++i) {
        fake_compositor_clear(&fc);
        pt_popup_set_placement(popup, cases[i].placement);
        pt_check(pt_popup_show(popup, anchor));
        pt_check(pt_window_paint(window, canvas));
        wait_popup_native(&fc, app, popup);
        pt_check(fake_compositor_sync(&fc, app));
        pt_check(popup->overlay.popup != NULL);
        pt_check(popup->overlay.native);
        pt_check(popup->native_placed);

        fc_popup_record record;
        memset(&record, 0, sizeof(record));
        pt_check(fake_compositor_popup_record_count(&fc) == 1);
        pt_check(fake_compositor_popup_record_at(&fc, 0, &record));
        pt_check(record.from_reposition == cases[i].reposition);
        pt_check(record.popup_id != 0 && record.surface_id != 0);
        pt_check(record.pos.size_set);
        pt_check(record.pos.anchor_rect_set);
        pt_check(record.pos.width >= 100 && record.pos.height >= 60);
        pt_check(record.pos.anchor_x == 30);
        pt_check(record.pos.anchor_y == 120);
        pt_check(record.pos.anchor_width == 80);
        pt_check(record.pos.anchor_height == 24);
        pt_check(record.pos.anchor == cases[i].anchor);
        pt_check(record.pos.gravity == cases[i].gravity);
        pt_check(record.pos.constraint == fc_constraint_all);
        pt_check(record.pos.offset_x == 0 && record.pos.offset_y == 0);

        /* Literal expected configure geometry, independent of the facility's
         * own positioner solver: anchor (30,120,80,24), panel 116x76. */
        pt_check(record.width == 116 && record.height == 76);
        pt_check(record.x == cases[i].expected_x && record.y == cases[i].expected_y);
        pt_check_near(popup->overlay.rect.x, (float)cases[i].expected_x, 0.01);
        pt_check_near(popup->overlay.rect.y, (float)cases[i].expected_y, 0.01);
        pt_check_near(popup->overlay.rect.w, 116.0f, 0.01);
        pt_check_near(popup->overlay.rect.h, 76.0f, 0.01);

        /* Hit-testing uses the compositor-placed rectangle. */
        pt_widget *hit = pt_window_overlay_hit_test(
            window, (float)cases[i].expected_x + 2.0f, (float)cases[i].expected_y + 2.0f);
        pt_check(hit != NULL);
        if (hit) {
            pt_check(hit == popup->overlay.panel ||
                     pt_widget_is_descendant(hit, popup->overlay.panel));
        }
        if (i == 0) {
            /* The first creation carried an explicit grab with the freshly
             * recorded pointer serial. */
            fc_request grab = {0};
            pt_check(fake_compositor_last_request(&fc, FC_K_POPUP, 1, &grab));
            pt_check(grab.nargs == 2);
            pt_check(grab.args[0].u == fake_compositor_live_id(&fc, FC_K_SEAT));
            pt_check(grab.args[1].u == 1002u);
        }
    }

    /* Reposition requests toggled by placement carry the positioner and a
     * token the server answers with repositioned + configure. */
    size_t reposition_count = fake_compositor_count_requests(&fc, FC_K_POPUP, 2);
    pt_check(reposition_count >= 1);
    fc_request reposition = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_POPUP, 2, &reposition));
    pt_check(reposition.nargs == 2);
    pt_check(reposition.args[1].u == 3u);

    /* A compositor flip wins over the requested placement: the injected
     * configure moves the surface and hit-testing follows it. */
    fc_popup_record last;
    memset(&last, 0, sizeof(last));
    pt_check(fake_compositor_popup_record_at(&fc, 0, &last));
    int32_t flip_x = 12;
    int32_t flip_y = 34;
    pt_check(popup->overlay.popup != NULL);
    pt_check(fake_compositor_popup_configure(&fc, flip_x, flip_y, last.width, last.height));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(popup->native_placed);
    pt_check_near(popup->overlay.rect.x, (float)flip_x, 0.01);
    pt_check_near(popup->overlay.rect.y, (float)flip_y, 0.01);
    pt_widget *flipped = pt_window_overlay_hit_test(window, (float)flip_x + 2.0f,
                                                    (float)flip_y + 2.0f);
    pt_check(flipped != NULL);

    /* popup_done from the compositor follows the same dismissal path. */
    pt_check(fake_compositor_popup_done(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(state.dismissals == 1);
    pt_check(!state.open_at_dismiss);
    pt_check(!pt_popup_is_open(popup));
    pt_check(popup->overlay.popup == NULL);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_check(fake_compositor_error(&fc) == NULL);

    pt_popup_destroy(popup);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

int main(void) {
    test_native_wire();
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.popup");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("popup");
    }
    test_create_and_show(app);
    test_headless_before_show(app);
    test_flip_and_slide(app);
    test_positioner_spec();
    test_positioner_matches_fallback(app);
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