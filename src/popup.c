/*
 * Application-owned popups built on the overlay and xdg_popup layers.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * A popup owns a panel card and the content widget handed to pt_popup_create.
 * The generic overlay layer supplies focus, dismiss-on-outside-click, native
 * surface placement and painting; this file owns the public contract, the
 * anchor geometry, the in-window flip/slide fallback and the dismiss callback.
 */

#include "internal.h"

#include <math.h>
#include <stdlib.h>

#define popup_edge 8.0f
#define popup_max_measure 900.0f

_Static_assert(offsetof(pt_popup, overlay) == 0,
               "the popup overlay must be the first member for overlay casts");

static float popup_anchor_extent(float value) {
    return value > 0.0f ? value : 1.0f;
}

static bool popup_anchor_valid(const pt_popup_anchor *anchor) {
    if (!anchor) {
        return false;
    }
    if (!isfinite((double)anchor->x) || !isfinite((double)anchor->y) ||
        !isfinite((double)anchor->width) || !isfinite((double)anchor->height)) {
        return false;
    }
    return anchor->width >= 0.0f && anchor->height >= 0.0f;
}

static size_t popup_open_count(const pt_window *window) {
    size_t count = 0;
    for (const pt_overlay *overlay = window ? window->overlays : NULL; overlay;
         overlay = overlay->next) {
        if (overlay->open && overlay->kind == pt_overlay_kind_popup) {
            ++count;
        }
    }
    return count;
}

/* Detaches a widget from its current parent without freeing it, so a widget
 * already in the window tree can move into the popup. pt_widget_append refuses
 * an attached child, and widget.c is outside this workstream's file set. */
static void popup_detach_widget(pt_widget *widget) {
    pt_widget *parent = widget ? widget->parent : NULL;
    if (!parent) {
        return;
    }
    pt_widget *previous = NULL;
    for (pt_widget *child = parent->first_child; child; child = child->next_sibling) {
        if (child == widget) {
            if (previous) {
                previous->next_sibling = child->next_sibling;
            } else {
                parent->first_child = child->next_sibling;
            }
            if (parent->last_child == child) {
                parent->last_child = previous;
            }
            break;
        }
        previous = child;
    }
    widget->parent = NULL;
    widget->next_sibling = NULL;
}

static void popup_place(pt_popup *popup, const pt_layout_ctx *ctx, float *width,
                        float *height) {
    pt_overlay *overlay = &popup->overlay;
    pt_window *window = overlay->window;
    float max_width = popup_max_measure;
    float max_height = popup_max_measure;
    if (window && !overlay->native) {
        max_width = pt_max_float(1.0f, (float)window->width - popup_edge * 2.0f);
        max_height = pt_max_float(1.0f, (float)window->height - window->header_height -
                                            popup_edge * 2.0f);
    }
    pt_widget_measure(overlay->panel, ctx, max_width, max_height, width, height);
    *width = pt_max_float(1.0f, *width);
    *height = pt_max_float(1.0f, *height);

    float anchor_x = popup->anchor.x;
    float anchor_y = popup->anchor.y;
    float anchor_w = popup_anchor_extent(popup->anchor.width);
    float anchor_h = popup_anchor_extent(popup->anchor.height);
    float x = anchor_x;
    float y = anchor_y;
    switch (popup->placement) {
    case pt_popup_placement_above:
        y = anchor_y - *height;
        break;
    case pt_popup_placement_right:
        x = anchor_x + anchor_w;
        break;
    case pt_popup_placement_left:
        x = anchor_x - *width;
        break;
    case pt_popup_placement_below:
    default:
        y = anchor_y + anchor_h;
        break;
    }

    if (window && !overlay->native) {
        float left = popup_edge;
        float right = (float)window->width - popup_edge;
        float top = window->header_height + popup_edge;
        float bottom = (float)window->height - popup_edge;
        if (right < left) {
            left = 0.0f;
            right = (float)window->width;
        }
        if (bottom < top) {
            top = 0.0f;
            bottom = (float)window->height;
        }
        /* Flip on the placement axis when the preferred side does not fit and
         * the opposite side has room; then slide on the other axis. This mirrors
         * xdg_positioner's flip_x/flip_y and slide_x/slide_y adjustments. */
        switch (popup->placement) {
        case pt_popup_placement_above:
            if (y < top && anchor_y + anchor_h + *height <= bottom) {
                y = anchor_y + anchor_h;
            }
            break;
        case pt_popup_placement_right:
            if (x + *width > right && anchor_x - *width >= left) {
                x = anchor_x - *width;
            }
            break;
        case pt_popup_placement_left:
            if (x < left && anchor_x + anchor_w + *width <= right) {
                x = anchor_x + anchor_w;
            }
            break;
        case pt_popup_placement_below:
        default:
            if (y + *height > bottom && anchor_y - *height >= top) {
                y = anchor_y - *height;
            }
            break;
        }
        if (*width > right - left) {
            *width = pt_max_float(1.0f, right - left);
            x = left;
        } else {
            x = pt_clamp_float(x, left, right - *width);
        }
        if (*height > bottom - top) {
            *height = pt_max_float(1.0f, bottom - top);
            y = top;
        } else {
            y = pt_clamp_float(y, top, bottom - *height);
        }
    }

    overlay->rect = (pt_rect){x, y, *width, *height};
    /* Only the fallback painter uses these; the native path positions from the
     * anchor rectangle through xdg_positioner. */
    overlay->anchor_x = x;
    overlay->anchor_y = y;
}

void pt_popup_overlay_layout(pt_popup *popup, pt_overlay *overlay,
                             const pt_layout_ctx *ctx) {
    if (!popup || !overlay || !overlay->panel || overlay != &popup->overlay) {
        return;
    }
    float width = 0.0f;
    float height = 0.0f;
    float placed_x = overlay->rect.x;
    float placed_y = overlay->rect.y;
    popup_place(popup, ctx, &width, &height);
    if (overlay->native && popup->native_placed) {
        /* The compositor already flipped/slid the surface and reported the
         * final position. Recomputing the requested anchor here would desync
         * hit-testing from the real surface until the next configure. */
        overlay->rect.x = placed_x;
        overlay->rect.y = placed_y;
        overlay->anchor_x = placed_x;
        overlay->anchor_y = placed_y;
    }
}

static void popup_damage(pt_popup *popup) {
    pt_window *window = popup ? popup->overlay.window : NULL;
    if (!window) {
        return;
    }
    pt_window_invalidate_layout(window, NULL);
    pt_window_damage_all(window);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

static bool popup_update_geometry(pt_popup *popup, pt_popup_anchor anchor) {
    pt_window *window = popup->overlay.window;
    if (!window) {
        pt_set_error("pt_popup: the parent window is gone");
        return false;
    }
    if (!popup_anchor_valid(&anchor)) {
        pt_set_error("pt_popup requires a finite, non-negative anchor");
        return false;
    }
    popup->anchor = anchor;
    popup->native_placed = false;
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    float width = 0.0f;
    float height = 0.0f;
    popup_place(popup, &ctx, &width, &height);
    return true;
}

pt_popup *pt_popup_create(pt_window *window, pt_widget *content) {
    if (!window) {
        pt_set_error("pt_popup_create requires a window");
        return NULL;
    }
    if (!content) {
        pt_set_error("pt_popup_create requires a content widget");
        return NULL;
    }
    if (content == window->root) {
        pt_set_error("pt_popup_create cannot adopt the window root");
        return NULL;
    }
    pt_window *owner = pt_widget_window(content);
    if (owner && owner != window) {
        pt_set_error("pt_popup content belongs to another window");
        return NULL;
    }
    if (content->parent == NULL && content->window != NULL) {
        pt_set_error("pt_popup content is attached outside a window tree");
        return NULL;
    }

    pt_popup *popup = pt_alloc_zeroed(1, sizeof(*popup));
    popup->overlay.kind = pt_overlay_kind_popup;
    popup->overlay.window = window;
    popup->placement = pt_popup_placement_below;
    popup->content = content;

    pt_widget *panel = pt_card_create(NULL);
    panel->window = window;
    panel->direction = pt_direction_column;
    panel->align_items = pt_align_stretch;
    panel->gap = 0.0f;
    panel->padding_top = 8.0f;
    panel->padding_bottom = 8.0f;
    panel->padding_left = 8.0f;
    panel->padding_right = 8.0f;
    panel->radius = 12.0f;
    panel->a11y_role = pt_a11y_role_panel;
    const char *name = pt_widget_get_a11y_name(content);
    pt_widget_set_a11y_name(panel, name[0] != '\0' ? name : "Popup");

    popup_detach_widget(content);
    pt_widget_append(panel, content);
    /* The content leaves the visible window tree until the popup opens. Drop
     * pointer/focus state that pointed into it so an invisible widget does not
     * keep receiving input. */
    if (window->focused && pt_widget_is_descendant(window->focused, content)) {
        window->focused->focused = false;
        pt_a11y_bridge_widget_state(window->focused, "focused", false);
        window->focused = NULL;
        pt_platform_ime_set_enabled(window, false);
    }
    if (window->hovered && pt_widget_is_descendant(window->hovered, content)) {
        window->hovered = NULL;
    }
    if (window->active_widget && pt_widget_is_descendant(window->active_widget, content)) {
        window->active_widget = NULL;
    }
    popup->overlay.panel = panel;
    return popup;
}

void pt_popup_destroy(pt_popup *popup) {
    if (!popup) {
        return;
    }
    pt_window *window = popup->overlay.window;
    if (popup->overlay.open) {
        pt_window_popup_hide(&popup->overlay);
    }
    if (popup->overlay.panel) {
        pt_window_overlay_defer_panel_free(window, popup->overlay.panel);
        popup->overlay.panel = NULL;
    }
    popup->content = NULL;
    free(popup);
}

bool pt_popup_show(pt_popup *popup, pt_popup_anchor anchor) {
    if (!popup) {
        pt_set_error("pt_popup_show requires a popup");
        return false;
    }
    pt_window *window = popup->overlay.window;
    if (!window || !popup->overlay.panel) {
        pt_set_error("pt_popup_show: the parent window is gone");
        return false;
    }
    if (popup->overlay.open) {
        return pt_popup_reposition(popup, anchor);
    }
    if (!popup_anchor_valid(&anchor)) {
        pt_set_error("pt_popup_show requires a finite, non-negative anchor");
        return false;
    }
    if (popup_open_count(window) >= (size_t)PT_POPUP_MAX_OPEN) {
        pt_set_error("pt_popup_show: too many open popups on this window");
        return false;
    }
    popup->anchor = anchor;
    popup->native_placed = false;
    popup->overlay.panel->window = window;
    pt_window_popup_show(&popup->overlay, window, NULL, popup->overlay.panel);
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    float width = 0.0f;
    float height = 0.0f;
    popup_place(popup, &ctx, &width, &height);
    if (window->overlays != &popup->overlay) {
        /* pt_window_popup_show links the overlay; a rejected link would mean the
         * window is already tearing down. */
        pt_window_popup_hide(&popup->overlay);
        pt_set_error("pt_popup_show: the parent window is not available");
        return false;
    }
    pt_window_overlay_focus_first(&popup->overlay);
    popup_damage(popup);
    return true;
}

bool pt_popup_reposition(pt_popup *popup, pt_popup_anchor anchor) {
    if (!popup) {
        pt_set_error("pt_popup_reposition requires a popup");
        return false;
    }
    if (!popup->overlay.open) {
        pt_set_error("pt_popup_reposition requires an open popup");
        return false;
    }
    if (!popup_update_geometry(popup, anchor)) {
        return false;
    }
    popup_damage(popup);
    return true;
}

void pt_popup_dismiss(pt_popup *popup) {
    if (!popup || !popup->overlay.open || popup->dismissing) {
        return;
    }
    popup->dismissing = true;
    pt_popup_dismiss_callback callback = popup->on_dismiss;
    void *user_data = popup->on_dismiss_data;
    pt_window_popup_hide(&popup->overlay);
    popup->dismissing = false;
    if (callback) {
        /* The state is already closed and the content is retained. The callback
         * may destroy the popup; nothing below touches it afterwards. */
        callback(popup, user_data);
    }
}

bool pt_popup_is_open(const pt_popup *popup) {
    return popup && popup->overlay.open;
}

pt_widget *pt_popup_content(const pt_popup *popup) {
    return popup ? popup->content : NULL;
}

void pt_popup_set_on_dismiss(pt_popup *popup, pt_popup_dismiss_callback callback,
                             void *user_data) {
    if (!popup) {
        return;
    }
    popup->on_dismiss = callback;
    popup->on_dismiss_data = user_data;
}

void pt_popup_set_placement(pt_popup *popup, pt_popup_placement placement) {
    if (!popup) {
        return;
    }
    if (placement != pt_popup_placement_below && placement != pt_popup_placement_above &&
        placement != pt_popup_placement_right && placement != pt_popup_placement_left) {
        placement = pt_popup_placement_below;
    }
    if (popup->placement == placement) {
        return;
    }
    popup->placement = placement;
    popup->native_placed = false;
    if (popup->overlay.open) {
        pt_layout_ctx ctx = pt_layout_context_for(popup->overlay.window->root);
        float width = 0.0f;
        float height = 0.0f;
        popup_place(popup, &ctx, &width, &height);
        popup_damage(popup);
    }
}

pt_popup_placement pt_popup_get_placement(const pt_popup *popup) {
    return popup ? popup->placement : pt_popup_placement_below;
}

void pt_popup_window_free(pt_popup *popup) {
    if (!popup) {
        return;
    }
    pt_window *window = popup->overlay.window;
    popup->overlay.open = false;
    popup->overlay.window = NULL;
    popup->overlay.previous_focus = NULL;
    if (popup->overlay.panel) {
        pt_a11y_bridge_widget_state(popup->overlay.panel, "showing", false);
        pt_window_overlay_defer_panel_free(window, popup->overlay.panel);
        popup->overlay.panel = NULL;
    }
    popup->content = NULL;
    free(popup);
}