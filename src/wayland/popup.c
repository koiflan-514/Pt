/*
 * Pt native popup surfaces for menus and tooltips (xdg_popup).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Popup panels are laid out in surface-local coordinates and rendered into
 * their own shm buffers. The parent window keeps the overlay in its list and
 * paints everything else; this file owns the popup role, placement, buffers
 * and input-surface mapping.
 */

#include "wayland_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define popup_buffer_count 3

typedef struct pt_wl_popup_buffer {
    struct wl_buffer *buffer;
    void *data;
    bool busy;
    pt_wl_popup *owner;
} pt_wl_popup_buffer;

struct pt_wl_popup {
    pt_platform *platform;
    pt_window *window;
    pt_overlay *overlay;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_popup *popup;
    struct wp_viewport *viewport;

    pt_wl_popup_buffer buffers[popup_buffer_count];
    void *pool_data;
    size_t pool_size;
    int buffer_width;
    int buffer_height;
    int stride;

    pt_canvas *canvas;
    int canvas_width;
    int canvas_height;
    float canvas_scale;

    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int pending_x;
    int pending_y;
    int pending_width;
    int pending_height;
    uint32_t reposition_token;
    bool configured;
    bool needs_redraw;
    bool mapped;

    pt_wl_popup *next;
};

static void popup_draw(pt_wl_popup *popup);
static void popup_free(pt_wl_popup *popup);
static void popup_done_notify(pt_overlay *overlay);

bool pt_wl_resolve_surface(pt_platform *platform, struct wl_surface *surface,
                           pt_wl_surface_ref *out) {
    if (!platform || !surface || !out) {
        return false;
    }
    if (platform->app) {
        for (pt_window *window = platform->app->first_window; window;
             window = window->next) {
            pt_wayland_window *wl = pt_wl_window_for(window);
            if (wl && wl->surface == surface) {
                *out = (pt_wl_surface_ref){window, NULL, 0.0f, 0.0f};
                return true;
            }
        }
    }
    for (pt_wl_popup *popup = platform->popups; popup; popup = popup->next) {
        if (popup->surface != surface) {
            continue;
        }
        pt_overlay *overlay = popup->overlay;
        *out = (pt_wl_surface_ref){
            popup->window,
            overlay,
            overlay ? overlay->rect.x : 0.0f,
            overlay ? overlay->rect.y : 0.0f,
        };
        return true;
    }
    return false;
}

bool pt_platform_popup_supported(pt_window *window) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform || platform->headless || !platform->wm_base || !window) {
        return false;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);
    return wl && wl->xdg_surface && window->shown && window->configured;
}

/* ---------- buffers ---------- */

static void popup_buffer_release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    pt_wl_popup_buffer *pb = data;
    pb->busy = false;
    if (pb->owner && pb->owner->needs_redraw) {
        popup_draw(pb->owner);
    }
}

static const struct wl_buffer_listener popup_buffer_listener = {
    .release = popup_buffer_release,
};

static void popup_destroy_buffers(pt_wl_popup *popup) {
    for (int i = 0; i < popup_buffer_count; ++i) {
        if (popup->buffers[i].buffer) {
            wl_buffer_destroy(popup->buffers[i].buffer);
            popup->buffers[i].buffer = NULL;
        }
        popup->buffers[i].data = NULL;
        popup->buffers[i].busy = false;
        popup->buffers[i].owner = NULL;
    }
    if (popup->pool_data) {
        munmap(popup->pool_data, popup->pool_size);
        popup->pool_data = NULL;
        popup->pool_size = 0;
    }
    popup->buffer_width = 0;
    popup->buffer_height = 0;
    popup->stride = 0;
}

static bool popup_ensure_buffers(pt_wl_popup *popup, int width, int height) {
    if (!popup->platform || !popup->platform->shm) {
        return false;
    }
    if (popup->buffer_width == width && popup->buffer_height == height &&
        popup->buffers[0].buffer) {
        return true;
    }
    for (int i = 0; i < popup_buffer_count; ++i) {
        if (popup->buffers[i].busy) {
            return false;
        }
    }
    popup_destroy_buffers(popup);
    int stride = width * 4;
    size_t plane = (size_t)stride * (size_t)height;
    size_t total = plane * popup_buffer_count;
    int fd = pt_wl_create_shm_file(total);
    if (fd < 0) {
        return false;
    }
    void *data = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(popup->platform->shm, fd, (int32_t)total);
    close(fd);
    if (!pool) {
        munmap(data, total);
        return false;
    }
    popup->pool_data = data;
    popup->pool_size = total;
    popup->buffer_width = width;
    popup->buffer_height = height;
    popup->stride = stride;
    for (int i = 0; i < popup_buffer_count; ++i) {
        pt_wl_popup_buffer *pb = &popup->buffers[i];
        pb->data = (uint8_t *)data + plane * (size_t)i;
        pb->busy = false;
        pb->owner = popup;
        pb->buffer = wl_shm_pool_create_buffer(pool, (int32_t)(plane * (size_t)i), width,
                                               height, stride, WL_SHM_FORMAT_ARGB8888);
        if (!pb->buffer) {
            wl_shm_pool_destroy(pool);
            popup_destroy_buffers(popup);
            return false;
        }
        wl_buffer_add_listener(pb->buffer, &popup_buffer_listener, pb);
    }
    wl_shm_pool_destroy(pool);
    return true;
}

static bool popup_ensure_canvas(pt_wl_popup *popup, int width, int height, float scale) {
    if (popup->canvas && popup->canvas_width == width && popup->canvas_height == height &&
        fabsf(popup->canvas_scale - scale) < 0.001f) {
        return true;
    }
    pt_canvas_destroy(popup->canvas);
    popup->canvas = pt_canvas_create(width, height, scale);
    if (!popup->canvas) {
        popup->canvas_width = 0;
        popup->canvas_height = 0;
        popup->canvas_scale = 0.0f;
        return false;
    }
    popup->canvas_width = width;
    popup->canvas_height = height;
    popup->canvas_scale = scale;
    return true;
}

/* ---------- rendering ---------- */

static void popup_draw(pt_wl_popup *popup) {
    if (!popup || !popup->configured || !popup->overlay || !popup->overlay->panel) {
        return;
    }
    pt_overlay *overlay = popup->overlay;
    pt_window *window = popup->window;
    if (!overlay->open || !window || !window->shown || window->close_requested) {
        return;
    }
    float scale = window->render_scale > 0.05f ? window->render_scale : 1.0f;
    int buffer_width = (int)lroundf((float)popup->width * scale);
    int buffer_height = (int)lroundf((float)popup->height * scale);
    if (buffer_width < 1) {
        buffer_width = 1;
    }
    if (buffer_height < 1) {
        buffer_height = 1;
    }
    if (!popup_ensure_buffers(popup, buffer_width, buffer_height) ||
        !popup_ensure_canvas(popup, buffer_width, buffer_height, scale)) {
        popup->needs_redraw = true;
        return;
    }
    pt_wl_popup_buffer *target = NULL;
    for (int i = 0; i < popup_buffer_count; ++i) {
        if (!popup->buffers[i].busy) {
            target = &popup->buffers[i];
            break;
        }
    }
    if (!target) {
        popup->needs_redraw = true;
        return;
    }

    pt_canvas_clear(popup->canvas, pt_color_transparent);
    pt_layout_ctx ctx = pt_layout_context_for(overlay->panel);
    if (!window->app) {
        ctx = (pt_layout_ctx){scale, "sans-serif"};
    }
    pt_widget_paint(overlay->panel, popup->canvas, &ctx);
    for (int row = 0; row < buffer_height; ++row) {
        const uint32_t *src = popup->canvas->pixels +
                              (size_t)row * (size_t)popup->canvas->stride;
        uint32_t *dst = (uint32_t *)((uint8_t *)target->data +
                                     (size_t)row * (size_t)popup->stride);
        memcpy(dst, src, (size_t)buffer_width * sizeof(uint32_t));
    }

    if (popup->viewport) {
        wl_surface_set_buffer_scale(popup->surface, 1);
        wp_viewport_set_destination(popup->viewport, popup->width, popup->height);
    } else {
        int integer = (int)lroundf(scale);
        if (integer < 1) {
            integer = 1;
        }
        wl_surface_set_buffer_scale(popup->surface, integer);
    }
    target->busy = true;
    wl_surface_attach(popup->surface, target->buffer, 0, 0);
    wl_surface_damage_buffer(popup->surface, 0, 0, buffer_width, buffer_height);
    wl_surface_commit(popup->surface);
    popup->needs_redraw = false;
    popup->mapped = true;
}

/* ---------- role and placement ---------- */

static struct xdg_positioner *popup_make_positioner(pt_wl_popup *popup, int width, int height,
                                                    int anchor_x, int anchor_y) {
    pt_platform *platform = popup->platform;
    struct xdg_positioner *positioner =
        xdg_wm_base_create_positioner(platform->wm_base);
    if (!positioner) {
        return NULL;
    }
    xdg_positioner_set_size(positioner, width, height);
    xdg_positioner_set_anchor_rect(positioner, anchor_x, anchor_y, 1, 1);
    xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(
        positioner, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
    xdg_positioner_set_offset(positioner, 0, 0);
    return positioner;
}

static void popup_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    pt_wl_popup *popup = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    if (popup->overlay) {
        popup->overlay->rect.x = (float)popup->pending_x;
        popup->overlay->rect.y = (float)popup->pending_y;
    }
    popup->configured = true;
    popup->needs_redraw = true;
    popup_draw(popup);
    if (popup->window) {
        popup->window->needs_draw = true;
    }
}

static const struct xdg_surface_listener popup_xdg_surface_listener = {
    .configure = popup_xdg_surface_configure,
};

static void popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x, int32_t y,
                            int32_t width, int32_t height) {
    (void)xdg_popup;
    pt_wl_popup *popup = data;
    popup->pending_x = x;
    popup->pending_y = y;
    popup->pending_width = width;
    popup->pending_height = height;
}

static void popup_repositioned(void *data, struct xdg_popup *xdg_popup, uint32_t token) {
    (void)xdg_popup;
    pt_wl_popup *popup = data;
    if (token == popup->reposition_token) {
        popup->needs_redraw = true;
    }
}

static void popup_done(void *data, struct xdg_popup *xdg_popup) {
    (void)xdg_popup;
    pt_wl_popup *popup = data;
    pt_overlay *overlay = popup->overlay;
    popup_free(popup);
    popup_done_notify(overlay);
}

static const struct xdg_popup_listener popup_listener = {
    .configure = popup_configure,
    .popup_done = popup_done,
    .repositioned = popup_repositioned,
};

static void popup_set_passive_input(pt_wl_popup *popup) {
    if (!popup->platform || !popup->platform->compositor || !popup->surface) {
        return;
    }
    struct wl_region *region = wl_compositor_create_region(popup->platform->compositor);
    if (!region) {
        return;
    }
    wl_surface_set_input_region(popup->surface, region);
    wl_region_destroy(region);
}

static bool popup_create(pt_window *window, pt_overlay *overlay) {
    pt_platform *platform = pt_platform_for(window);
    pt_wayland_window *parent = pt_wl_window_for(window);
    if (!platform || !parent || !parent->xdg_surface || !platform->wm_base) {
        return false;
    }
    int width = (int)lroundf(overlay->rect.w);
    int height = (int)lroundf(overlay->rect.h);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    int anchor_x = (int)floorf(overlay->anchor_x);
    int anchor_y = (int)floorf(overlay->anchor_y);

    pt_wl_popup *popup = pt_alloc_zeroed(1, sizeof(*popup));
    popup->platform = platform;
    popup->window = window;
    popup->overlay = overlay;
    popup->width = width;
    popup->height = height;
    popup->anchor_x = anchor_x;
    popup->anchor_y = anchor_y;
    popup->surface = wl_compositor_create_surface(platform->compositor);
    if (!popup->surface) {
        free(popup);
        return false;
    }
    popup->xdg_surface = xdg_wm_base_get_xdg_surface(platform->wm_base, popup->surface);
    if (!popup->xdg_surface) {
        wl_surface_destroy(popup->surface);
        free(popup);
        return false;
    }
    xdg_surface_add_listener(popup->xdg_surface, &popup_xdg_surface_listener, popup);

    struct xdg_positioner *positioner =
        popup_make_positioner(popup, width, height, anchor_x, anchor_y);
    if (!positioner) {
        xdg_surface_destroy(popup->xdg_surface);
        wl_surface_destroy(popup->surface);
        free(popup);
        return false;
    }
    popup->popup = xdg_surface_get_popup(popup->xdg_surface, parent->xdg_surface, positioner);
    xdg_positioner_destroy(positioner);
    if (!popup->popup) {
        xdg_surface_destroy(popup->xdg_surface);
        wl_surface_destroy(popup->surface);
        free(popup);
        return false;
    }
    xdg_popup_add_listener(popup->popup, &popup_listener, popup);
    if (platform->viewporter) {
        popup->viewport = wp_viewporter_get_viewport(platform->viewporter, popup->surface);
    }
    if (overlay->passive) {
        popup_set_passive_input(popup);
    } else {
        uint32_t serial = window->pointer_serial ? window->pointer_serial : platform->serial;
        if (platform->seat && serial) {
            xdg_popup_grab(popup->popup, platform->seat, serial);
        }
    }
    overlay->popup = popup;
    popup->next = platform->popups;
    platform->popups = popup;
    wl_surface_commit(popup->surface);
    return true;
}

static void popup_reposition(pt_wl_popup *popup, int width, int height, int anchor_x,
                             int anchor_y) {
    bool size_changed = popup->width != width || popup->height != height;
    popup->width = width;
    popup->height = height;
    popup->anchor_x = anchor_x;
    popup->anchor_y = anchor_y;
    if (xdg_popup_get_version(popup->popup) >= 3) {
        struct xdg_positioner *positioner =
            popup_make_positioner(popup, width, height, anchor_x, anchor_y);
        if (positioner) {
            uint32_t token = ++popup->reposition_token;
            xdg_popup_reposition(popup->popup, positioner, token);
            xdg_positioner_destroy(positioner);
        }
    } else if (size_changed) {
        popup->needs_redraw = true;
    }
}

void pt_platform_present_popups(pt_window *window, const pt_layout_ctx *ctx) {
    (void)ctx;
    if (!window || !window->app || !window->app->platform) {
        return;
    }
    pt_platform *platform = window->app->platform;
    if (platform->headless) {
        return;
    }
    for (pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
        if (!overlay->open || !overlay->native || !overlay->panel) {
            continue;
        }
        int width = (int)lroundf(overlay->rect.w);
        int height = (int)lroundf(overlay->rect.h);
        if (width < 1) {
            width = 1;
        }
        if (height < 1) {
            height = 1;
        }
        int anchor_x = (int)floorf(overlay->anchor_x);
        int anchor_y = (int)floorf(overlay->anchor_y);
        if (!overlay->popup) {
            if (!popup_create(window, overlay)) {
                overlay->native = false;
                window->needs_draw = true;
            }
            continue;
        }
        pt_wl_popup *popup = overlay->popup;
        if (popup->width != width || popup->height != height ||
            popup->anchor_x != anchor_x || popup->anchor_y != anchor_y) {
            popup_reposition(popup, width, height, anchor_x, anchor_y);
        }
        if (popup->configured) {
            popup_draw(popup);
        }
    }
}

static void popup_free(pt_wl_popup *popup) {
    if (!popup) {
        return;
    }
    pt_platform *platform = popup->platform;
    if (platform) {
        pt_wl_popup **link = &platform->popups;
        while (*link && *link != popup) {
            link = &(*link)->next;
        }
        if (*link) {
            *link = popup->next;
        }
        if (platform->pointer_overlay == popup->overlay) {
            platform->pointer_overlay = NULL;
        }
    }
    pt_overlay *overlay = popup->overlay;
    if (overlay && overlay->popup == popup) {
        overlay->popup = NULL;
    }
    if (popup->surface) {
        wl_surface_attach(popup->surface, NULL, 0, 0);
        wl_surface_commit(popup->surface);
    }
    popup_destroy_buffers(popup);
    pt_canvas_destroy(popup->canvas);
    if (popup->viewport) {
        wp_viewport_destroy(popup->viewport);
    }
    if (popup->popup) {
        xdg_popup_destroy(popup->popup);
    }
    if (popup->xdg_surface) {
        xdg_surface_destroy(popup->xdg_surface);
    }
    if (popup->surface) {
        wl_surface_destroy(popup->surface);
    }
    free(popup);
}

void pt_platform_popup_destroy(pt_overlay *overlay) {
    if (!overlay || !overlay->popup) {
        return;
    }
    popup_free(overlay->popup);
}

void pt_platform_popup_destroy_all(pt_window *window) {
    if (!window || !window->app || !window->app->platform) {
        return;
    }
    pt_platform *platform = window->app->platform;
    pt_wl_popup *popup = platform->popups;
    while (popup) {
        pt_wl_popup *next = popup->next;
        if (popup->window == window) {
            popup_free(popup);
        }
        popup = next;
    }
}

static void popup_done_notify(pt_overlay *overlay) {
    if (overlay && overlay->open) {
        pt_window_overlay_native_done(overlay);
    }
}
