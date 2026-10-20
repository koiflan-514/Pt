/*
 * Pt windows, client-side decorations and input routing.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define csd_button_width 36.0f
#define csd_button_height 30.0f

static pt_rect damage_union(pt_rect a, pt_rect b) {
    float x0 = pt_min_float(a.x, b.x);
    float y0 = pt_min_float(a.y, b.y);
    float x1 = pt_max_float(a.x + a.w, b.x + b.w);
    float y1 = pt_max_float(a.y + a.h, b.y + b.h);
    return (pt_rect){x0, y0, x1 - x0, y1 - y0};
}

static bool damage_tiles_touch(pt_rect a, pt_rect b) {
    return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;
}

static float damage_area(pt_rect rect) {
    return rect.w * rect.h;
}

static void damage_tiles_compact(pt_window *window) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (uint8_t i = 0; i < window->damage_tile_count && !merged; ++i) {
            for (uint8_t j = (uint8_t)(i + 1); j < window->damage_tile_count; ++j) {
                if (damage_tiles_touch(window->damage_tiles[i], window->damage_tiles[j])) {
                    window->damage_tiles[i] =
                        damage_union(window->damage_tiles[i], window->damage_tiles[j]);
                    window->damage_tiles[j] =
                        window->damage_tiles[--window->damage_tile_count];
                    merged = true;
                    break;
                }
            }
        }
    }
}

void pt_window_damage_all(pt_window *window) {
    if (!window) {
        return;
    }
    window->damage_full = true;
    window->damage_valid = false;
    window->damage_tile_count = 0;
}

void pt_window_damage_rect(pt_window *window, pt_rect rect) {
    if (!window || window->damage_full) {
        return;
    }
    float width = (float)window->width;
    float height = (float)window->height;
    if (rect.x < 0.0f) {
        rect.w += rect.x;
        rect.x = 0.0f;
    }
    if (rect.y < 0.0f) {
        rect.h += rect.y;
        rect.y = 0.0f;
    }
    if (rect.x + rect.w > width) {
        rect.w = width - rect.x;
    }
    if (rect.y + rect.h > height) {
        rect.h = height - rect.y;
    }
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    if (!window->damage_valid) {
        window->damage = rect;
        window->damage_valid = true;
        window->damage_tiles[0] = rect;
        window->damage_tile_count = 1;
        return;
    }
    window->damage = damage_union(window->damage, rect);
    if (window->damage_tile_count < pt_damage_tile_capacity) {
        window->damage_tiles[window->damage_tile_count++] = rect;
    } else {
        /* Saturated index: fold the new rect into the tile with the least
         * growth so small updates do not degrade into one window-sized tile. */
        uint8_t best = 0;
        float best_growth = FLT_MAX;
        for (uint8_t i = 0; i < window->damage_tile_count; ++i) {
            pt_rect merged = damage_union(window->damage_tiles[i], rect);
            float growth = damage_area(merged) - damage_area(window->damage_tiles[i]);
            if (growth < best_growth) {
                best_growth = growth;
                best = i;
            }
        }
        window->damage_tiles[best] = damage_union(window->damage_tiles[best], rect);
    }
    damage_tiles_compact(window);
}

void pt_widget_damage(pt_widget *widget, float margin) {
    if (!widget) {
        return;
    }
    pt_window *window = pt_widget_window(widget);
    if (!window) {
        return;
    }
    if (window->needs_layout) {
        pt_window_damage_all(window);
        return;
    }
    pt_window_damage_rect(window, (pt_rect){widget->x - margin, widget->y - margin,
                                            widget->width + margin * 2.0f,
                                            widget->height + margin * 2.0f});
}

static void unlink_window(pt_window *window) {
    if (!window || !window->app) {
        return;
    }
    pt_window **link = &window->app->first_window;
    while (*link) {
        if (*link == window) {
            *link = window->next;
            break;
        }
        link = &(*link)->next;
    }
    window->next = NULL;
}

static void update_window_header(pt_window *window) {
    window->header_height = (window->csd && window->decorated) ? pt_header_height : 0.0f;
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    pt_window_damage_all(window);
}

pt_window *pt_window_alloc(pt_app *app, const char *title, int width, int height) {
    pt_window *window = pt_alloc_zeroed(1, sizeof(*window));
    window->app = app;
    window->a11y_id = pt_a11y_alloc_id();
    window->title = pt_strdup(title ? title : "Pt");
    window->width = width > 0 ? width : 640;
    window->height = height > 0 ? height : 420;
    window->requested_width = width > 0 ? width : 640;
    window->requested_height = height > 0 ? height : 420;
    window->scale = 1.0f;
    window->render_scale = 1.0f;
    window->open = true;
    window->decorated = true;
    window->csd = true;
    window->resizable = true;
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    window->damage_full = true;
    window->popup_policy = pt_popup_policy_auto;
    window->long_press_delay = 500u;
    window->momentum_enabled = true;
    window->root = pt_widget_new(NULL, pt_widget_box);
    window->root->window = window;
    window->root->direction = pt_direction_column;
    window->root->gap = 16.0f;
    window->root->padding_top = 24.0f;
    window->root->padding_bottom = 24.0f;
    window->root->padding_left = 24.0f;
    window->root->padding_right = 24.0f;
    update_window_header(window);
    return window;
}

pt_window *pt_window_create(pt_app *app, const char *title, int width, int height) {
    if (!app) {
        pt_set_error("pt_window_create requires a valid app");
        return NULL;
    }
    pt_window *window = pt_window_alloc(app, title, width, height);
    if (!pt_platform_window_init(window)) {
        pt_widget_free_tree(window->root);
        free(window->title);
        free(window);
        return NULL;
    }
    window->next = app->first_window;
    app->first_window = window;
    return window;
}

void pt_window_free(pt_window *window) {
    if (!window) {
        return;
    }
    pt_app *app = window->app;
    unlink_window(window);
    if (window->drag_active) {
        pt_window_cancel_drag(window);
    }
    pt_dnd_window_destroyed(window);
    if (window->platform_window) {
        pt_platform_window_destroy(window);
    }
    pt_window_overlays_free(window);
    if (window->root) {
        window->root->window = NULL;
        pt_widget_free_tree(window->root);
    }
    pt_canvas_destroy(window->backing);
    free(window->drag_text);
    free(window->drag_image);
    free(window->title);
    free(window);
    (void)app;
}

void pt_window_show(pt_window *window) {
    if (!window || window->close_requested) {
        return;
    }
    window->shown = true;
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    pt_platform_window_show(window);
    if (!window->focused) {
        pt_widget_focus_first(window);
    }
    pt_platform_window_schedule_draw(window);
}

void pt_window_hide(pt_window *window) {
    if (!window) {
        return;
    }
    window->shown = false;
    pt_platform_window_hide(window);
}

void pt_window_close(pt_window *window) {
    pt_window_request_close(window, false);
}

bool pt_window_is_open(const pt_window *window) {
    return window && window->open && !window->close_requested;
}

pt_widget *pt_window_root(pt_window *window) {
    return window ? window->root : NULL;
}

pt_app *pt_window_app(pt_window *window) {
    return window ? window->app : NULL;
}

int pt_window_get_width(const pt_window *window) {
    return window ? window->width : 0;
}

int pt_window_get_height(const pt_window *window) {
    return window ? window->height : 0;
}

float pt_window_get_scale(const pt_window *window) {
    return window ? window->scale : 1.0f;
}

void pt_window_set_title(pt_window *window, const char *title) {
    if (!window) {
        return;
    }
    char *copy = pt_strdup(title ? title : "");
    free(window->title);
    window->title = copy;
    pt_platform_window_set_title(window);
    window->needs_draw = true;
    pt_window_damage_rect(window, (pt_rect){0.0f, 0.0f, (float)window->width,
                                            window->header_height + 1.0f});
    pt_platform_window_schedule_draw(window);
}

bool pt_window_clipboard_set(pt_window *window, const char *text) {
    if (!window) {
        return false;
    }
    pt_platform_clipboard_set(window, text);
    return true;
}

char *pt_window_clipboard_get(pt_window *window) {
    if (!window) {
        return NULL;
    }
    return pt_platform_clipboard_get(window);
}

void pt_window_set_size(pt_window *window, int width, int height) {
    if (!window) {
        return;
    }
    if (width > 0) {
        window->width = width;
        window->requested_width = width;
    }
    if (height > 0) {
        window->height = height;
        window->requested_height = height;
    }
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    pt_window_damage_all(window);
    pt_platform_window_set_size_hint(window);
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_min_size(pt_window *window, int width, int height) {
    if (!window) {
        return;
    }
    window->min_width = width > 0 ? width : 0;
    window->min_height = height > 0 ? height : 0;
    pt_platform_window_set_min_size(window);
    if (window->width < window->min_width) {
        window->width = window->min_width;
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
    }
    if (window->height < window->min_height) {
        window->height = window->min_height;
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
    }
    pt_window_damage_all(window);
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_resizable(pt_window *window, bool resizable) {
    if (!window) {
        return;
    }
    window->resizable = resizable;
    pt_platform_window_set_resizable(window);
    window->needs_draw = true;
    pt_window_damage_all(window);
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_decorated(pt_window *window, bool decorated) {
    if (!window) {
        return;
    }
    window->decorated = decorated;
    pt_platform_window_set_decorated(window);
    update_window_header(window);
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_csd_enabled(pt_window *window, bool enabled) {
    if (!window) {
        return;
    }
    window->csd = enabled;
    update_window_header(window);
    pt_platform_window_schedule_draw(window);
}

void pt_window_request_redraw(pt_window *window) {
    if (!window) {
        return;
    }
    window->needs_draw = true;
    pt_window_damage_all(window);
    pt_platform_window_schedule_draw(window);
}

void pt_window_focus(pt_window *window, pt_widget *widget) {
    if (window && widget) {
        pt_widget_focus(widget);
    }
}

void pt_window_set_theme_color(pt_window *window, pt_theme_token token, pt_color color) {
    if (!window || token < 0 || token >= pt_theme_token_count) {
        return;
    }
    window->theme_colors[token] = color;
    window->theme_color_set[token] = true;
    pt_window_request_redraw(window);
}

pt_color pt_window_get_theme_color(const pt_window *window, pt_theme_token token) {
    if (!window || token < 0 || token >= pt_theme_token_count) {
        return 0;
    }
    return pt_theme_color(window, token);
}

void pt_window_set_theme_metric(pt_window *window, pt_theme_metric metric, float value) {
    if (!window || metric < 0 || metric >= pt_theme_metric_count) {
        return;
    }
    window->theme_metrics[metric] = pt_max_float(0.0f, value);
    window->theme_metric_set[metric] = true;
    pt_window_invalidate_layout(window, NULL);
    pt_window_request_redraw(window);
}

float pt_window_get_theme_metric(const pt_window *window, pt_theme_metric metric) {
    if (!window || metric < 0 || metric >= pt_theme_metric_count) {
        return 0.0f;
    }
    return pt_theme_metric_value(window, metric);
}

void pt_window_clear_theme_overrides(pt_window *window) {
    if (!window) {
        return;
    }
    memset(window->theme_color_set, 0, sizeof(window->theme_color_set));
    memset(window->theme_metric_set, 0, sizeof(window->theme_metric_set));
    pt_window_invalidate_layout(window, NULL);
    pt_window_request_redraw(window);
}

void pt_window_set_close_callback(pt_window *window, pt_close_callback callback,
                                  void *user_data) {
    if (window) {
        window->close_callback = callback;
        window->close_data = user_data;
    }
}

void pt_window_set_resize_callback(pt_window *window, pt_resize_callback callback,
                                   void *user_data) {
    if (window) {
        window->resize_callback = callback;
        window->resize_data = user_data;
    }
}

void pt_window_set_key_callback(pt_window *window, pt_key_callback callback, void *user_data) {
    if (window) {
        window->key_callback = callback;
        window->key_data = user_data;
    }
}

void pt_window_apply_configure(pt_window *window, int width, int height) {
    if (!window) {
        return;
    }
    int new_width = width > 0 ? width : window->requested_width;
    int new_height = height > 0 ? height : window->requested_height;
    if (new_width < window->min_width) {
        new_width = window->min_width;
    }
    if (new_height < window->min_height) {
        new_height = window->min_height;
    }
    if (new_width < 1) {
        new_width = 1;
    }
    if (new_height < 1) {
        new_height = 1;
    }
    bool changed = new_width != window->width || new_height != window->height;
    window->width = new_width;
    window->height = new_height;
    window->configured = true;
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
    pt_window_damage_all(window);
    if (changed && window->resize_callback) {
        window->resize_callback(window, new_width, new_height, window->resize_data);
    }
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_fullscreen(pt_window *window, bool fullscreen) {
    if (!window) {
        return;
    }
    window->fullscreen = fullscreen;
    pt_platform_window_set_fullscreen(window, fullscreen);
}

void pt_window_set_maximized(pt_window *window, bool maximized) {
    if (!window || window->maximized == maximized) {
        return;
    }
    window->maximized = maximized;
    pt_platform_window_toggle_maximize(window);
}

bool pt_window_has_editable_focus(const pt_window *window) {
    return window && window->focused && window->focused->enabled &&
           pt_widget_kind_is_text_input(window->focused->kind);
}

void pt_window_update_caret_timer(pt_window *window) {
    if (!window) {
        return;
    }
    window->caret_visible = true;
    window->caret_last_toggle = pt_now_seconds();
}

void pt_window_tick(pt_window *window) {
    if (!window || window->close_requested) {
        return;
    }
    double now = pt_now_seconds();
    float dt = window->last_paint_time > 0.0 ? (float)(now - window->last_paint_time) : 0.016f;
    dt = pt_clamp_float(dt, 0.0f, 0.05f);
    window->animating = false;
    if (window->root && pt_widget_update_animations(window->root, dt)) {
        window->animating = true;
        window->needs_draw = true;
    }
    if (pt_window_interaction_tick(window, dt)) {
        window->animating = true;
        window->needs_draw = true;
    }
    pt_window_overlays_tick(window, dt);
    pt_window_overlays_collect(window);
    if (pt_window_has_editable_focus(window) && window->keyboard_focused) {
        if (now - window->caret_last_toggle >= 0.53) {
            window->caret_visible = !window->caret_visible;
            window->caret_last_toggle = now;
            window->needs_draw = true;
            pt_widget_damage(window->focused, 4.0f);
        }
    } else {
        window->caret_visible = true;
    }
    if (window->needs_draw && window->shown && window->configured) {
        pt_platform_window_schedule_draw(window);
    }
}

void pt_window_request_close(pt_window *window, bool force) {
    if (!window || window->close_requested) {
        return;
    }
    if (!force && window->close_callback &&
        !window->close_callback(window, window->close_data)) {
        return;
    }
    window->close_requested = true;
    window->open = false;
    pt_platform_window_hide(window);
    if (window->app && !window->app->running) {
        pt_app_cleanup_windows(window->app);
    }
}

/* ---------- client-side decorations ---------- */

static pt_color window_theme_color(const pt_window *window, pt_theme_token token,
                                   pt_color fallback) {
    if (!window || !window->app) {
        return fallback;
    }
    return pt_theme_color(window, token);
}

static const char *window_font_family(const pt_window *window) {
    return window->app && window->app->theme.font_family[0]
               ? window->app->theme.font_family
               : "sans-serif";
}

static pt_rect csd_close_rect(const pt_window *window) {
    float right = (float)window->width - 8.0f;
    float y = (window->header_height - csd_button_height) * 0.5f;
    if (y < 0.0f) {
        y = 0.0f;
    }
    return (pt_rect){right - csd_button_width, y, csd_button_width, csd_button_height};
}

static pt_rect csd_maximize_rect(const pt_window *window) {
    pt_rect close = csd_close_rect(window);
    return (pt_rect){close.x - csd_button_width, close.y, csd_button_width,
                     csd_button_height};
}

static pt_rect csd_minimize_rect(const pt_window *window) {
    pt_rect maximize = csd_maximize_rect(window);
    return (pt_rect){maximize.x - csd_button_width, maximize.y, csd_button_width,
                     csd_button_height};
}

static pt_csd_part csd_button_at(const pt_window *window, float x, float y) {
    pt_rect close = csd_close_rect(window);
    if (pt_rect_contains(close, x, y)) {
        return pt_csd_part_close;
    }
    pt_rect maximize = csd_maximize_rect(window);
    if (pt_rect_contains(maximize, x, y)) {
        return pt_csd_part_maximize;
    }
    pt_rect minimize = csd_minimize_rect(window);
    if (pt_rect_contains(minimize, x, y)) {
        return pt_csd_part_minimize;
    }
    return pt_csd_part_none;
}

static pt_csd_part csd_part_at(const pt_window *window, float x, float y) {
    if (!window->csd || window->header_height <= 0.0f) {
        return pt_csd_part_none;
    }
    float margin = pt_resize_margin;
    if (window->resizable && !window->maximized && !window->fullscreen) {
        bool left = x < margin;
        bool right = x >= (float)window->width - margin;
        bool top = y < margin;
        bool bottom = y >= (float)window->height - margin;
        if (left && top) {
            return pt_csd_part_edge_top_left;
        }
        if (right && top) {
            return pt_csd_part_edge_top_right;
        }
        if (left && bottom) {
            return pt_csd_part_edge_bottom_left;
        }
        if (right && bottom) {
            return pt_csd_part_edge_bottom_right;
        }
        if (top) {
            return pt_csd_part_edge_top;
        }
        if (bottom) {
            return pt_csd_part_edge_bottom;
        }
        if (left) {
            return pt_csd_part_edge_left;
        }
        if (right) {
            return pt_csd_part_edge_right;
        }
    }
    if (y < window->header_height) {
        pt_csd_part button = csd_button_at(window, x, y);
        if (button != pt_csd_part_none) {
            return button;
        }
        return pt_csd_part_titlebar;
    }
    return pt_csd_part_none;
}

static uint32_t csd_resize_edges(pt_csd_part part) {
    switch (part) {
    case pt_csd_part_edge_top:
        return 1;
    case pt_csd_part_edge_bottom:
        return 2;
    case pt_csd_part_edge_left:
        return 4;
    case pt_csd_part_edge_right:
        return 8;
    case pt_csd_part_edge_top_left:
        return 1 | 4;
    case pt_csd_part_edge_top_right:
        return 1 | 8;
    case pt_csd_part_edge_bottom_left:
        return 2 | 4;
    case pt_csd_part_edge_bottom_right:
        return 2 | 8;
    default:
        return 0;
    }
}

static pt_cursor csd_cursor(pt_csd_part part) {
    switch (part) {
    case pt_csd_part_edge_top:
    case pt_csd_part_edge_bottom:
        return pt_cursor_resize_v;
    case pt_csd_part_edge_left:
    case pt_csd_part_edge_right:
        return pt_cursor_resize_h;
    case pt_csd_part_edge_top_left:
    case pt_csd_part_edge_bottom_right:
        return pt_cursor_resize_nwse;
    case pt_csd_part_edge_top_right:
    case pt_csd_part_edge_bottom_left:
        return pt_cursor_resize_nesw;
    default:
        return pt_cursor_default;
    }
}

static void paint_csd_button(pt_canvas *canvas, pt_window *window, pt_csd_part part,
                             pt_rect rect, bool hovered, bool pressed) {
    pt_color background = pt_color_transparent;
    bool close = part == pt_csd_part_close;
    if (hovered || pressed) {
        background = close ? window_theme_color(window, pt_theme_danger, 0xE5484DFFu)
                           : window_theme_color(window, pt_theme_surface_hover,
                                                0x232C3AFFu);
        if (pressed) {
            background = pt_color_lighten(background, 0.06f);
        }
    }
    if ((background & 0xFFu) != 0) {
        pt_canvas_fill_rounded_rect(canvas, rect.x + 2.0f, rect.y + 3.0f, rect.w - 4.0f,
                                    rect.h - 6.0f, 7.0f, background);
    }
    pt_color color = close && (hovered || pressed)
                         ? window_theme_color(window, pt_theme_accent_text, 0xFFFFFFFFu)
                         : window_theme_color(window, pt_theme_text_muted, 0x8B98A9FFu);
    float cx = rect.x + rect.w * 0.5f;
    float cy = rect.y + rect.h * 0.5f;
    switch (part) {
    case pt_csd_part_minimize:
        pt_canvas_draw_line(canvas, cx - 5.0f, cy + 4.0f, cx + 5.0f, cy + 4.0f, 1.5f, color);
        break;
    case pt_csd_part_maximize:
        if (window->maximized) {
            pt_canvas_stroke_rounded_rect(canvas, cx - 5.0f, cy - 3.0f, 8.0f, 8.0f, 1.5f,
                                          1.3f, color);
            pt_canvas_stroke_rounded_rect(canvas, cx - 3.0f, cy - 5.0f, 8.0f, 8.0f, 1.5f,
                                          1.3f, color);
        } else {
            pt_canvas_stroke_rounded_rect(canvas, cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.5f,
                                          1.3f, color);
        }
        break;
    case pt_csd_part_close:
        pt_canvas_draw_line(canvas, cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f, 1.5f, color);
        pt_canvas_draw_line(canvas, cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f, 1.5f, color);
        break;
    default:
        break;
    }
}

static void paint_csd(pt_canvas *canvas, pt_window *window) {
    if (window->header_height <= 0.0f) {
        return;
    }
    pt_color titlebar = window_theme_color(window, pt_theme_titlebar, 0x121821FFu);
    pt_canvas_fill_rect(canvas, 0.0f, 0.0f, (float)window->width, window->header_height,
                        titlebar);
    pt_color border = window_theme_color(window, pt_theme_border, 0x2A3342FFu);
    pt_canvas_fill_rect(canvas, 0.0f, window->header_height - 1.0f, (float)window->width, 1.0f,
                        border);

    pt_rect minimize = csd_minimize_rect(window);
    pt_rect maximize = csd_maximize_rect(window);
    pt_rect close = csd_close_rect(window);
    paint_csd_button(canvas, window, pt_csd_part_minimize, minimize,
                     window->hovered_csd == pt_csd_part_minimize,
                     window->active_csd == pt_csd_part_minimize);
    paint_csd_button(canvas, window, pt_csd_part_maximize, maximize,
                     window->hovered_csd == pt_csd_part_maximize,
                     window->active_csd == pt_csd_part_maximize);
    paint_csd_button(canvas, window, pt_csd_part_close, close,
                     window->hovered_csd == pt_csd_part_close,
                     window->active_csd == pt_csd_part_close);

    const char *title = window->title ? window->title : "";
    if (*title) {
        float scale = window->render_scale > 0.0f ? window->render_scale : 1.0f;
        pt_font *font = pt_font_acquire(window_font_family(window), pt_font_weight_semibold, pt_font_slant_upright, 13.0f * scale);
        if (font) {
            float line_height = font->line_height / scale;
            float baseline = (window->header_height - line_height) * 0.5f +
                             font->ascent / scale;
            float left = minimize.x - 16.0f;
            float title_width = pt_max_float(0.0f, left - 8.0f);
            if (title_width < 20.0f) {
                title_width = (float)window->width - 16.0f;
                left = 8.0f;
            }
            pt_color color = window_theme_color(window, pt_theme_text, 0xE6EDF3FFu);
            if (!window->keyboard_focused) {
                color = pt_color_alpha(color, 190);
            }
            pt_canvas_push_clip(canvas, 8.0f, 0.0f, title_width, window->header_height - 1.0f);
            pt_canvas_draw_text_line(canvas, font, title, strlen(title), 8.0f, baseline,
                                     title_width, pt_text_align_center, color, true);
            pt_canvas_pop_clip(canvas);
        }
    }
}

static bool window_paint_common(pt_window *window, pt_canvas *canvas, bool force_full) {
    if (!window || !canvas || !window->root) {
        return false;
    }
    if (!force_full && !window->needs_draw && !window->needs_layout &&
        !window->damage_valid && !window->damage_full) {
        window->last_damage = (pt_rect){0.0f, 0.0f, 0.0f, 0.0f};
        window->last_damage_full = false;
        return true;
    }
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    if (!window->app) {
        ctx = (pt_layout_ctx){canvas->scale > 0.0f ? canvas->scale : 1.0f, "sans-serif"};
    }
    if (window->needs_layout) {
        pt_widget *layout_root = window->layout_root;
        bool partial = layout_root && layout_root != window->root && layout_root->width > 0.0f &&
                       layout_root->height > 0.0f;
        if (partial) {
            pt_widget_layout(layout_root, layout_root->x, layout_root->y, layout_root->width,
                             layout_root->height, &ctx);
        } else {
            float content_height = (float)window->height - window->header_height;
            if (content_height < 0.0f) {
                content_height = 0.0f;
            }
            pt_widget_layout(window->root, 0.0f, window->header_height, (float)window->width,
                             content_height, &ctx);
            if (!force_full) {
                pt_window_damage_all(window);
            }
        }
        window->last_layout_partial = partial;
        window->layout_root = NULL;
        window->needs_layout = false;
    }

    bool full = force_full || window->damage_full || !window->damage_valid;
    pt_rect damage = full ? (pt_rect){0.0f, 0.0f, (float)window->width,
                                      (float)window->height}
                          : window->damage;
    window->last_damage = damage;
    window->last_damage_full = full;
    window->last_damage_tile_count = full ? 0 : window->damage_tile_count;
    if (!full) {
        memcpy(window->last_damage_tiles, window->damage_tiles,
               sizeof(pt_rect) * (size_t)window->damage_tile_count);
    }
    window->paint_visits = 0;
    canvas->visit_count = 0;

    pt_color background = window_theme_color(window, pt_theme_background, 0x0E1116FFu);
    if (full) {
        pt_canvas_clear(canvas, background);
        paint_csd(canvas, window);
        pt_widget_paint(window->root, canvas, &ctx);
        pt_window_overlays_paint(window, canvas);
    } else {
        for (uint8_t tile = 0; tile < window->damage_tile_count; ++tile) {
            pt_rect part = window->damage_tiles[tile];
            pt_canvas_push_clip(canvas, part.x, part.y, part.w, part.h);
            pt_canvas_clear_rect(canvas, part.x, part.y, part.w, part.h, background);
            paint_csd(canvas, window);
            pt_widget_paint(window->root, canvas, &ctx);
            pt_window_overlays_paint(window, canvas);
            pt_canvas_pop_clip(canvas);
        }
    }

    window->damage_valid = false;
    window->damage_full = false;
    window->damage_tile_count = 0;
    window->paint_visits = canvas->visit_count;
    window->needs_draw = false;
    window->redraw_scheduled = false;
    window->last_paint_time = pt_now_seconds();
    pt_window_overlays_collect(window);
    return true;
}

bool pt_window_paint(pt_window *window, pt_canvas *canvas) {
    if (!window) {
        return false;
    }
    pt_window_damage_all(window);
    return window_paint_common(window, canvas, true);
}

bool pt_window_paint_damage(pt_window *window, pt_canvas *canvas) {
    return window_paint_common(window, canvas, false);
}

/* ---------- input ---------- */

static void update_cursor(pt_window *window, pt_cursor cursor) {
    if (window->current_cursor != cursor) {
        window->current_cursor = cursor;
        pt_platform_window_set_cursor(window, cursor);
    }
}

static void set_hovered_widget(pt_window *window, pt_widget *widget) {
    if (window->hovered == widget) {
        return;
    }
    if (window->hovered) {
        pt_widget_damage(window->hovered, 3.0f);
        pt_widget_pointer_leave(window->hovered);
    }
    window->hovered = widget;
    if (widget) {
        pt_widget_damage(widget, 3.0f);
        pt_widget_pointer_enter(widget, window->pointer_x, window->pointer_y);
    }
    window->needs_draw = true;
}

static void set_hovered_csd(pt_window *window, pt_csd_part part) {
    if (window->hovered_csd != part) {
        window->hovered_csd = part;
        window->needs_draw = true;
        pt_window_damage_rect(window, (pt_rect){0.0f, 0.0f, (float)window->width,
                                                window->header_height + 1.0f});
    }
}

static void update_ime_rect(pt_window *window) {
    if (!window || !window->focused ||
        !pt_widget_kind_is_text_input(window->focused->kind)) {
        return;
    }
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    pt_widget_ime_rect(window->focused, &x, &y, &w, &h);
    pt_platform_ime_set_cursor_rect(window, x, y, w, h);
}

static void update_hover(pt_window *window, float x, float y) {
    if (pt_window_overlays_active(window)) {
        set_hovered_csd(window, pt_csd_part_none);
        pt_widget *hit = pt_window_overlay_hit_test(window, x, y);
        pt_widget *cursor_widget = hit ? pt_widget_focusable_ancestor(hit) : NULL;
        set_hovered_widget(window, hit);
        update_cursor(window, pt_widget_cursor_at(cursor_widget ? cursor_widget : hit, x, y));
        pt_window_overlays_update_tooltip(window);
        return;
    }
    pt_csd_part part = csd_part_at(window, x, y);
    set_hovered_csd(window, part);
    if (part == pt_csd_part_none) {
        if (window->header_height > 0.0f && y < window->header_height) {
            set_hovered_widget(window, NULL);
            update_cursor(window, pt_cursor_default);
            pt_window_overlays_update_tooltip(window);
            return;
        }
        pt_widget *hit = pt_widget_interaction_target(pt_widget_hit_test(window->root, x, y));
        pt_widget *cursor_widget = hit ? pt_widget_focusable_ancestor(hit) : NULL;
        set_hovered_widget(window, hit);
        update_cursor(window, pt_widget_cursor_at(cursor_widget ? cursor_widget : hit, x, y));
        pt_window_overlays_update_tooltip(window);
        return;
    }
    set_hovered_widget(window, NULL);
    if (part == pt_csd_part_minimize || part == pt_csd_part_maximize ||
        part == pt_csd_part_close) {
        update_cursor(window, pt_cursor_pointer);
    } else if (part == pt_csd_part_titlebar) {
        update_cursor(window, pt_cursor_default);
    } else {
        update_cursor(window, csd_cursor(part));
    }
    pt_window_overlays_update_tooltip(window);
}

static void clear_entry_drag(pt_window *window) {
    window->drag_pending = false;
    window->drag_started = false;
    window->drag_widget = NULL;
    free(window->drag_text);
    window->drag_text = NULL;
}

void pt_window_input_pointer_enter(pt_window *window, float x, float y) {
    if (!window) {
        return;
    }
    window->pointer_inside = true;
    window->pointer_x = x;
    window->pointer_y = y;
    update_hover(window, x, y);
}

void pt_window_input_pointer_motion(pt_window *window, float x, float y, uint32_t time) {
    (void)time;
    if (!window) {
        return;
    }
    window->pointer_inside = true;
    window->pointer_x = x;
    window->pointer_y = y;
    pt_window_press_motion(window, x, y);
    if (window->drag_pending && window->drag_widget) {
        float dx = x - window->drag_start_x;
        float dy = y - window->drag_start_y;
        if (!window->drag_started && dx * dx + dy * dy >= 36.0f) {
            window->drag_started = true;
            window->drag_pending = false;
            pt_widget *drag_widget = window->drag_widget;
            char *drag_text = window->drag_text;
            window->drag_text = NULL;
            window->drag_widget = NULL;
            bool started = pt_window_drag_source_begin(
                window, "text/plain;charset=utf-8", drag_text);
            free(drag_text);
            if (!started) {
                window->drag_started = false;
                pt_widget_pointer_down(drag_widget, window->drag_start_x, window->drag_start_y);
                pt_widget_pointer_drag(drag_widget, x, y);
            }
        }
    }
    if (window->active_widget) {
        pt_widget *active = window->active_widget;
        if ((active->kind == pt_widget_slider && active->sliding) ||
            (pt_widget_kind_is_text_input(active->kind) && active->selecting) ||
            active->kind == pt_widget_table_header_cell ||
            ((active->kind == pt_widget_table_row || active->kind == pt_widget_list_row ||
              active->kind == pt_widget_tree_row || active->kind == pt_widget_list ||
              active->kind == pt_widget_table || active->kind == pt_widget_tree) &&
             active->pressed)) {
            pt_widget_pointer_drag(active, x, y);
            window->needs_draw = true;
        } else if (active->pressed) {
            active->pressed_inside = pt_rect_contains(
                (pt_rect){active->x, active->y, active->width, active->height}, x, y);
            window->needs_draw = true;
        }
    }
    update_hover(window, x, y);
}

void pt_window_input_pointer_leave(pt_window *window) {
    if (!window) {
        return;
    }
    window->pointer_inside = false;
    clear_entry_drag(window);
    pt_window_press_cancel(window);
    pt_window_momentum_stop(window);
    set_hovered_widget(window, NULL);
    set_hovered_csd(window, pt_csd_part_none);
    window->active_csd = pt_csd_part_none;
    pt_window_overlays_dismiss_tooltip(window);
    window->needs_draw = true;
}

void pt_window_stylus_input(pt_window *window, pt_stylus_phase phase, pt_stylus_tool tool,
                            float x, float y, float pressure, float distance, float tilt_x,
                            float tilt_y, float rotation, uint32_t buttons) {
    if (!window) {
        return;
    }
    if (window->stylus_callback) {
        pt_stylus_event event = {
            .phase = phase,
            .tool = tool,
            .x = x,
            .y = y,
            .pressure = pressure,
            .distance = distance,
            .tilt_x = tilt_x,
            .tilt_y = tilt_y,
            .rotation = rotation,
            .buttons = buttons,
        };
        window->stylus_callback(window, &event, window->stylus_data);
    }
    switch (phase) {
    case pt_stylus_phase_enter:
        pt_window_input_pointer_enter(window, x, y);
        break;
    case pt_stylus_phase_motion:
        pt_window_input_pointer_motion(window, x, y, 0);
        break;
    case pt_stylus_phase_down:
        window->stylus_down = true;
        pt_window_input_pointer_button(window, 0x110u, true, x, y, 0, 0);
        break;
    case pt_stylus_phase_up:
        window->stylus_down = false;
        pt_window_input_pointer_button(window, 0x110u, false, x, y, 0, 0);
        break;
    case pt_stylus_phase_leave:
        window->stylus_down = false;
        pt_window_input_pointer_leave(window);
        break;
    }
}

void pt_window_input_pointer_button(pt_window *window, uint32_t button, bool pressed, float x,
                                    float y, uint32_t serial, uint32_t time) {
    if (!window || button != 0x110u) {
        return;
    }
    window->pointer_x = x;
    window->pointer_y = y;
    if (serial != 0) {
        window->pointer_serial = serial;
    }
    if (!pressed) {
        pt_window_press_end(window, x, y);
    } else {
        pt_window_momentum_stop(window);
    }
    if (pt_window_overlays_pointer(window, button, pressed, x, y, serial, time)) {
        update_hover(window, x, y);
        window->needs_draw = true;
        pt_platform_window_schedule_draw(window);
        return;
    }
    if (pressed) {
        uint32_t event_time = time ? time : (uint32_t)pt_now_ms();
        bool same_spot = fabsf(x - window->last_click_x) <= 6.0f &&
                         fabsf(y - window->last_click_y) <= 6.0f;
        bool repeat = window->click_count > 0 && same_spot &&
                      event_time >= window->last_click_time &&
                      event_time - window->last_click_time <= 400u;
        window->click_count = repeat ? window->click_count + 1u : 1u;
        if (window->click_count > 3u) {
            window->click_count = 1u;
        }
        window->last_click_time = event_time;
        window->last_click_x = x;
        window->last_click_y = y;

        pt_csd_part part = csd_part_at(window, x, y);
        if (part == pt_csd_part_titlebar) {
            pt_platform_window_begin_move(window, serial);
            return;
        }
        if (part >= pt_csd_part_edge_top && part <= pt_csd_part_edge_bottom_right) {
            pt_platform_window_begin_resize(window, serial, csd_resize_edges(part));
            return;
        }
        if (part == pt_csd_part_minimize || part == pt_csd_part_maximize ||
            part == pt_csd_part_close) {
            window->active_csd = part;
            window->needs_draw = true;
            pt_window_damage_rect(window, (pt_rect){0.0f, 0.0f, (float)window->width,
                                                    window->header_height + 1.0f});
            return;
        }
        pt_widget *hit = pt_widget_interaction_target(pt_widget_hit_test(window->root, x, y));
        if (hit) {
            pt_widget *focusable = pt_widget_focusable_ancestor(hit);
            if (focusable) {
                pt_widget_focus(focusable);
                if (!pt_widget_kind_is_text_input(focusable->kind)) {
                    focusable->focus_visible = false;
                }
            }
            window->active_widget = hit;
            pt_window_momentum_stop(window);
            pt_window_press_begin(window, hit, x, y, button);
            clear_entry_drag(window);
            char *selected = pt_entry_selection_text_at(hit, x, y);
            if (selected) {
                window->drag_pending = true;
                window->drag_widget = hit;
                window->drag_start_x = x;
                window->drag_start_y = y;
                window->drag_text = selected;
            } else {
                pt_widget_pointer_down(hit, x, y);
                bool multi_clickable =
                    pt_widget_kind_is_text_input(hit->kind) ||
                    hit->kind == pt_widget_table_cell || hit->kind == pt_widget_table_row ||
                    hit->kind == pt_widget_tree_row;
                if (multi_clickable && window->click_count > 1) {
                    pt_widget_pointer_multi_click(hit, x, y, window->click_count);
                }
            }
            window->needs_draw = true;
            update_ime_rect(window);
        }
        return;
    }

    if (window->active_csd != pt_csd_part_none) {
        pt_csd_part target = csd_part_at(window, x, y);        pt_csd_part part = window->active_csd;
        window->active_csd = pt_csd_part_none;
        window->needs_draw = true;
        pt_window_damage_rect(window, (pt_rect){0.0f, 0.0f, (float)window->width,
                                                window->header_height + 1.0f});
        if (target == part) {
            if (part == pt_csd_part_minimize) {
                pt_platform_window_minimize(window);
            } else if (part == pt_csd_part_maximize) {
                window->maximized = !window->maximized;
                pt_platform_window_toggle_maximize(window);
            } else if (part == pt_csd_part_close) {
                pt_window_request_close(window, false);
            }
        }
        return;
    }

    if (window->active_widget) {
        pt_widget *active = window->active_widget;
        bool click_selection = window->drag_pending && window->drag_widget == active;
        window->active_widget = NULL;
        clear_entry_drag(window);
        bool inside = pt_rect_contains((pt_rect){active->x, active->y, active->width,
                                                 active->height}, x, y);
        if (click_selection) {
            pt_widget_pointer_down(active, x, y);
            if (pt_widget_kind_is_text_input(active->kind) && window->click_count > 1) {
                pt_widget_pointer_multi_click(active, x, y, window->click_count);
            }
            pt_widget_pointer_up(active, inside);
        } else {
            pt_widget_pointer_up(active, inside);
        }
        window->needs_draw = true;
        if (pt_widget_kind_is_text_input(active->kind)) {
            update_ime_rect(window);
        }
    }
}

void pt_window_input_axis(pt_window *window, float x, float y, float delta) {
    if (!window) {
        return;
    }
    pt_window_momentum_stop(window);
    if (pt_window_overlays_axis(window, x, y, delta)) {
        return;
    }
    pt_widget *hit = window->hovered;
    if (!hit) {
        hit = pt_widget_hit_test(window->root, x, y);
    }
    if (hit) {
        pt_widget_wheel(hit, delta);
    }
}

static void pt_window_touch_begin_pinch(pt_window *window);

void pt_window_input_touch_down(pt_window *window, int32_t id, float x, float y, uint32_t time,
                                uint32_t serial) {
    if (!window || window->touch_count >= pt_touch_max) {
        return;
    }
    for (uint32_t i = 0; i < window->touch_count; ++i) {
        if (window->touches[i].id == id) {
            return;
        }
    }
    window->touches[window->touch_count].id = id;
    window->touches[window->touch_count].x = x;
    window->touches[window->touch_count].y = y;
    ++window->touch_count;
    if (window->touch_count == 1) {
        window->touch_active = true;
        window->touch_pinch = false;
        window->touch_gesture = false;
        window->touch_id = id;
        window->pointer_inside = true;
        window->pointer_x = x;
        window->pointer_y = y;
        update_hover(window, x, y);
        pt_window_input_pointer_button(window, 0x110u, true, x, y, serial, time);
    } else if (!window->touch_pinch) {
        pt_window_touch_begin_pinch(window);
    }
}

static int touch_index_of(const pt_window *window, int32_t id) {
    for (uint32_t i = 0; i < window->touch_count; ++i) {
        if (window->touches[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static void touch_remove_at(pt_window *window, uint32_t index) {
    if (index >= window->touch_count) {
        return;
    }
    for (uint32_t i = index + 1; i < window->touch_count; ++i) {
        window->touches[i - 1] = window->touches[i];
    }
    --window->touch_count;
}

static void touch_pinch_metrics(const pt_window *window, float *centroid_x, float *centroid_y,
                                float *distance, float *angle) {
    float cx = 0.0f;
    float cy = 0.0f;
    for (uint32_t i = 0; i < window->touch_count; ++i) {
        cx += window->touches[i].x;
        cy += window->touches[i].y;
    }
    if (window->touch_count > 0) {
        cx /= (float)window->touch_count;
        cy /= (float)window->touch_count;
    }
    *centroid_x = cx;
    *centroid_y = cy;
    float dx = window->touches[1].x - window->touches[0].x;
    float dy = window->touches[1].y - window->touches[0].y;
    *distance = sqrtf(dx * dx + dy * dy);
    *angle = atan2f(dy, dx);
}

static void pt_window_touch_begin_pinch(pt_window *window) {
    pt_window_press_cancel(window);
    if (window->active_widget) {
        pt_widget *active = window->active_widget;
        window->active_widget = NULL;
        clear_entry_drag(window);
        pt_widget_pointer_up(active, false);
    }
    window->active_csd = pt_csd_part_none;
    window->touch_id = -1;
    window->touch_pinch = true;
    window->touch_gesture = window->gesture_callback != NULL &&
                            !(window->gesture_active &&
                              window->gesture_kind == pt_gesture_kind_pinch);
    float cx = 0.0f;
    float cy = 0.0f;
    float distance = 0.0f;
    float angle = 0.0f;
    touch_pinch_metrics(window, &cx, &cy, &distance, &angle);
    window->pointer_inside = true;
    window->pointer_x = cx;
    window->pointer_y = cy;
    window->touch_pinch_distance = distance > 0.5f ? distance : 1.0f;
    window->touch_pinch_angle = angle;
    if (window->touch_gesture) {
        pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_begin,
                                window->touch_count, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    window->needs_draw = true;
}

static void touch_update_pinch(pt_window *window) {
    if (!window->touch_pinch || window->touch_count < 2) {
        return;
    }
    float cx = 0.0f;
    float cy = 0.0f;
    float distance = 0.0f;
    float angle = 0.0f;
    touch_pinch_metrics(window, &cx, &cy, &distance, &angle);
    float dx = cx - window->pointer_x;
    float dy = cy - window->pointer_y;
    float scale = distance / window->touch_pinch_distance;
    float rotation = angle - window->touch_pinch_angle;
    while (rotation > 3.14159265f) {
        rotation -= 6.28318531f;
    }
    while (rotation < -3.14159265f) {
        rotation += 6.28318531f;
    }
    window->pointer_x = cx;
    window->pointer_y = cy;
    window->touch_pinch_angle = angle;
    if (window->touch_gesture) {
        pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_update,
                                window->touch_count, dx, dy, scale, rotation);
    }
}

void pt_window_input_touch_motion(pt_window *window, int32_t id, float x, float y,
                                  uint32_t time) {
    if (!window) {
        return;
    }
    int index = touch_index_of(window, id);
    if (index < 0) {
        return;
    }
    window->touches[index].x = x;
    window->touches[index].y = y;
    if (window->touch_pinch) {
        touch_update_pinch(window);
        return;
    }
    if (window->touch_active && window->touch_id == id) {
        pt_window_input_pointer_motion(window, x, y, time);
    }
}

void pt_window_input_touch_up(pt_window *window, int32_t id, uint32_t time) {
    if (!window) {
        return;
    }
    int index = touch_index_of(window, id);
    if (index < 0) {
        return;
    }
    bool primary = window->touch_id == id;
    touch_remove_at(window, (uint32_t)index);
    if (window->touch_pinch) {
        if (window->touch_gesture && window->touch_count < 2) {
            pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_end,
                                    window->gesture_fingers, 0.0f, 0.0f, 1.0f, 0.0f);
            window->touch_gesture = false;
        }
        if (window->touch_count == 0) {
            window->touch_pinch = false;
            window->touch_active = false;
            window->touch_id = -1;
            pt_window_input_pointer_leave(window);
        }
        window->needs_draw = true;
        return;
    }
    if (primary) {
        float x = window->pointer_x;
        float y = window->pointer_y;
        pt_window_input_pointer_button(window, 0x110u, false, x, y, 0, time);
        window->touch_active = false;
        window->touch_id = -1;
        pt_window_input_pointer_leave(window);
    }
}

void pt_window_input_touch_cancel(pt_window *window) {
    if (!window || window->touch_count == 0) {
        return;
    }
    if (window->touch_gesture) {
        pt_window_gesture_input(window, pt_gesture_kind_pinch, pt_gesture_phase_cancel,
                                window->gesture_fingers, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    if (!window->touch_pinch && window->touch_active) {
        pt_window_press_cancel(window);
        pt_window_input_pointer_button(window, 0x110u, false, -100000.0f, -100000.0f, 0, 0);
    }
    window->touch_count = 0;
    window->touch_pinch = false;
    window->touch_gesture = false;
    window->touch_active = false;
    window->touch_id = -1;
    pt_window_input_pointer_leave(window);
}

void pt_window_input_keyboard_focus(pt_window *window, bool focused, uint32_t serial) {
    (void)serial;
    if (!window) {
        return;
    }
    window->keyboard_focused = focused;
    if (focused) {
        bool editable = pt_window_has_editable_focus(window);
        pt_platform_ime_set_enabled(window, editable);
        if (editable) {
            update_ime_rect(window);
        }
    } else {
        pt_platform_ime_set_enabled(window, false);
    }
    window->needs_draw = true;
    if (window->header_height > 0.0f) {
        pt_window_damage_rect(window, (pt_rect){0.0f, 0.0f, (float)window->width,
                                                window->header_height});
    }
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_input_modifiers(pt_window *window, uint32_t modifiers) {
    if (window) {
        window->modifiers = modifiers;
    }
}

void pt_window_input_key(pt_window *window, uint32_t keysym, uint32_t modifiers,
                         const char *utf8, bool pressed) {
    if (!window) {
        return;
    }
    window->modifiers = modifiers;
    if (!pressed) {
        return;
    }
    if (pt_window_overlays_key(window, keysym, modifiers, utf8)) {
        if (window->focused && pt_widget_kind_is_text_input(window->focused->kind)) {
            window->caret_visible = true;
            window->caret_last_toggle = pt_now_seconds();
            update_ime_rect(window);
        }
        window->needs_draw = true;
        pt_platform_window_schedule_draw(window);
        return;
    }
    bool consumed = false;
    if (window->key_callback) {
        consumed = window->key_callback(window, keysym, modifiers, window->key_data);
    }
    if (!consumed && keysym == 0xFF09u) {
        pt_widget_focus_next(window, (modifiers & (uint32_t)pt_key_modifier_shift) != 0);
        consumed = true;
    }
    if (!consumed && window->focused) {
        consumed = pt_widget_key(window->focused, keysym, modifiers, utf8);
    }
    if (consumed) {
        if (window->focused && pt_widget_kind_is_text_input(window->focused->kind)) {
            window->caret_visible = true;
            window->caret_last_toggle = pt_now_seconds();
            update_ime_rect(window);
        }
        window->needs_draw = true;
        pt_platform_window_schedule_draw(window);
    }
}

void pt_window_input_ime_commit(pt_window *window, const char *text) {
    pt_window_input_ime_compose(window, text, "", -1, -1, 0, 0);
}

void pt_window_input_ime_preedit(pt_window *window, const char *text) {
    size_t length = text ? strlen(text) : 0;
    pt_window_input_ime_compose(window, NULL, text ? text : "", (int32_t)length,
                                (int32_t)length, 0, 0);
}

void pt_window_input_ime_compose(pt_window *window, const char *commit, const char *preedit,
                                 int32_t cursor_begin, int32_t cursor_end,
                                 uint32_t delete_before, uint32_t delete_after) {
    if (!window || !window->focused ||
        !pt_widget_kind_is_text_input(window->focused->kind)) {
        return;
    }
    pt_widget *focused = window->focused;
    bool preedit_present = preedit && *preedit;
    bool has_edit = (commit && *commit) || delete_before > 0 || delete_after > 0;
    if (has_edit) {
        if (!pt_widget_ime_apply(focused, commit, delete_before, delete_after,
                                 !preedit_present)) {
            return;
        }
    }
    if (preedit_present) {
        pt_widget_preedit_ex(focused, preedit, cursor_begin, cursor_end);
    } else if (!has_edit) {
        /* A done carrying no text is the input method acknowledging the state we just
         * pushed. Replying to it pushes another state update and another done, so an
         * entry with no composition to end ignores it. */
        if (!pt_widget_ime_has_state(focused)) {
            return;
        }
        pt_widget_preedit_ex(focused, "", -1, -1);
    }
    window->caret_visible = true;
    window->caret_last_toggle = pt_now_seconds();
    update_ime_rect(window);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}
