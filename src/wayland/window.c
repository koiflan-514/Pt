/*
 * Pt Wayland window surfaces, buffers and decorations.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "wayland_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

pt_wayland_window *pt_wl_window_for(pt_window *window) {
    return window ? (pt_wayland_window *)window->platform_window : NULL;
}

pt_platform *pt_platform_for(pt_window *window) {
    return window && window->app ? (pt_platform *)window->app->platform : NULL;
}

int pt_wl_create_shm_file(size_t size) {
    int fd = memfd_create("pt-shm", MFD_CLOEXEC);
    if (fd < 0) {
        char name[64];
        for (int attempt = 0; attempt < 16; ++attempt) {
            snprintf(name, sizeof(name), "/pt-shm-%d-%d", (int)getpid(), attempt);
            fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
            if (fd >= 0) {
                shm_unlink(name);
                break;
            }
            if (errno != EEXIST) {
                break;
            }
        }
    }
    if (fd < 0) {
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *buffer) {
    (void)buffer;
    pt_wl_buffer *pb = data;
    pb->busy = false;
    pt_wayland_window *wl = (pt_wayland_window *)pb->owner;
    if (wl && wl->window && !wl->window->close_requested && wl->window->shown &&
        wl->needs_redraw) {
        pt_wl_window_schedule_frame(wl->window);
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void destroy_buffers(pt_wayland_window *wl) {
    for (int i = 0; i < pt_wl_buffer_count; ++i) {
        if (wl->buffers[i].buffer) {
            wl_buffer_destroy(wl->buffers[i].buffer);
            wl->buffers[i].buffer = NULL;
        }
        wl->buffers[i].owner = NULL;
        wl->buffers[i].data = NULL;
        wl->buffers[i].busy = false;
        wl->buffers[i].pending_damage = (pt_rect){0.0f, 0.0f, 0.0f, 0.0f};
        wl->buffers[i].pending_valid = false;
        wl->buffers[i].pending_full = false;
    }
    if (wl->pool_data) {
        munmap(wl->pool_data, wl->pool_size);
        wl->pool_data = NULL;
        wl->pool_size = 0;
    }
    wl->buffer_width = 0;
    wl->buffer_height = 0;
    wl->stride = 0;
}

static pt_rect rect_union(pt_rect a, pt_rect b) {
    if (a.w <= 0.0f || a.h <= 0.0f) {
        return b;
    }
    if (b.w <= 0.0f || b.h <= 0.0f) {
        return a;
    }
    float x0 = a.x < b.x ? a.x : b.x;
    float y0 = a.y < b.y ? a.y : b.y;
    float x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return (pt_rect){x0, y0, x1 - x0, y1 - y0};
}

static void buffer_add_damage(pt_wl_buffer *pb, pt_rect damage, bool full) {
    if (full) {
        pb->pending_full = true;
        pb->pending_valid = false;
        return;
    }
    if (pb->pending_full) {
        return;
    }
    if (damage.w <= 0.0f || damage.h <= 0.0f) {
        return;
    }
    pb->pending_damage = pb->pending_valid ? rect_union(pb->pending_damage, damage) : damage;
    pb->pending_valid = true;
}

static void buffer_clear_damage(pt_wl_buffer *pb) {
    pb->pending_damage = (pt_rect){0.0f, 0.0f, 0.0f, 0.0f};
    pb->pending_valid = false;
    pb->pending_full = false;
}

static bool ensure_backing(pt_window *window, pt_wayland_window *wl, int width, int height,
                           float scale) {
    if (window->backing && window->backing_width == width &&
        window->backing_height == height &&
        fabsf(window->backing_scale - scale) < 0.001f) {
        return true;
    }
    pt_canvas_destroy(window->backing);
    window->backing = pt_canvas_create(width, height, scale);
    if (!window->backing) {
        window->backing_width = 0;
        window->backing_height = 0;
        window->backing_scale = 0.0f;
        return false;
    }
    window->backing_width = width;
    window->backing_height = height;
    window->backing_scale = scale;
    pt_window_damage_all(window);
    for (int i = 0; i < pt_wl_buffer_count; ++i) {
        buffer_add_damage(&wl->buffers[i], (pt_rect){0, 0, 0, 0}, true);
    }
    return true;
}

static bool damage_to_pixels(const pt_canvas *canvas, pt_rect damage, int *out_x, int *out_y,
                             int *out_w, int *out_h) {
    float scale = canvas->scale > 0.0f ? canvas->scale : 1.0f;
    int x0 = (int)floorf(damage.x * scale);
    int y0 = (int)floorf(damage.y * scale);
    int x1 = (int)ceilf((damage.x + damage.w) * scale);
    int y1 = (int)ceilf((damage.y + damage.h) * scale);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > canvas->width) {
        x1 = canvas->width;
    }
    if (y1 > canvas->height) {
        y1 = canvas->height;
    }
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    *out_x = x0;
    *out_y = y0;
    *out_w = x1 - x0;
    *out_h = y1 - y0;
    return true;
}

static void copy_backing_region(const pt_canvas *backing, pt_wl_buffer *target,
                                const pt_wayland_window *wl, int x, int y, int width,
                                int height) {
    for (int row = 0; row < height; ++row) {
        const uint32_t *src = backing->pixels + (size_t)(y + row) * (size_t)backing->stride +
                              (size_t)x;
        uint32_t *dst = (uint32_t *)((uint8_t *)target->data +
                                     (size_t)(y + row) * (size_t)wl->stride) +
                        (size_t)x;
        memcpy(dst, src, (size_t)width * sizeof(uint32_t));
    }
}

static bool ensure_buffers(pt_window *window, int width, int height) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || !platform->shm) {
        return false;
    }
    if (wl->buffer_width == width && wl->buffer_height == height &&
        wl->buffers[0].buffer) {
        return true;
    }
    for (int i = 0; i < pt_wl_buffer_count; ++i) {
        if (wl->buffers[i].busy) {
            return false;
        }
    }
    destroy_buffers(wl);
    int stride = width * 4;
    size_t plane = (size_t)stride * (size_t)height;
    size_t total = plane * pt_wl_buffer_count;
    int fd = pt_wl_create_shm_file(total);
    if (fd < 0) {
        pt_set_error("failed to create shared memory buffer");
        return false;
    }
    void *data = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        pt_set_error("failed to map shared memory buffer");
        return false;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(platform->shm, fd, (int32_t)total);
    close(fd);
    if (!pool) {
        munmap(data, total);
        pt_set_error("failed to create wl_shm pool");
        return false;
    }
    wl->pool_data = data;
    wl->pool_size = total;
    wl->buffer_width = width;
    wl->buffer_height = height;
    wl->stride = stride;
    for (int i = 0; i < pt_wl_buffer_count; ++i) {
        pt_wl_buffer *pb = &wl->buffers[i];
        pb->data = (uint8_t *)data + plane * (size_t)i;
        pb->busy = false;
        pb->owner = wl;
        pb->pending_damage = (pt_rect){0.0f, 0.0f, 0.0f, 0.0f};
        pb->pending_valid = false;
        pb->pending_full = true;
        pb->buffer = wl_shm_pool_create_buffer(pool, (int32_t)(plane * (size_t)i), width,
                                               height, stride, WL_SHM_FORMAT_ARGB8888);
        if (!pb->buffer) {
            wl_shm_pool_destroy(pool);
            munmap(data, total);
            wl->pool_data = NULL;
            wl->pool_size = 0;
            wl->buffer_width = 0;
            wl->buffer_height = 0;
            pt_set_error("failed to create wl_shm buffer");
            return false;
        }
        wl_buffer_add_listener(pb->buffer, &buffer_listener, pb);
    }
    wl_shm_pool_destroy(pool);
    return true;
}

void pt_wl_window_update_viewport(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform) {
        return;
    }
    if (wl->viewport) {
        wp_viewport_set_destination(wl->viewport, window->width, window->height);
    }
}

void pt_wl_window_apply_scale(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl) {
        return;
    }
    float desired = window->scale > 0.05f ? window->scale : 1.0f;
    if (platform && platform->viewporter && wl->viewport) {
        window->render_scale = desired;
        wl_surface_set_buffer_scale(wl->surface, 1);
        pt_wl_window_update_viewport(window);
    } else {
        int integer = (int)lroundf(desired);
        if (integer < 1) {
            integer = 1;
        }
        if (integer > 8) {
            integer = 8;
        }
        window->scale = (float)integer;
        window->render_scale = (float)integer;
        wl_surface_set_buffer_scale(wl->surface, integer);
    }
    pt_window_invalidate_layout(window, NULL);
    window->needs_draw = true;
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    (void)time;
    pt_wayland_window *wl = data;
    wl_callback_destroy(callback);
    wl->frame_callback = NULL;
    if (wl->window && wl->window->shown && !wl->window->close_requested &&
        wl->needs_redraw) {
        pt_wl_window_draw(wl->window);
    }
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

void pt_wl_window_schedule_frame(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    wl->needs_redraw = true;
    if (wl->drawing) {
        wl->redraw_pending = true;
        return;
    }
    if (!wl->configured || !window->shown || window->close_requested || wl->frame_callback) {
        return;
    }
    pt_wl_window_draw(window);
}

bool pt_wl_window_draw(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || !wl->configured || !window->shown || window->width <= 0 ||
        window->height <= 0) {
        return false;
    }
    float scale = window->render_scale > 0.05f ? window->render_scale : 1.0f;
    int buffer_width = (int)lroundf((float)window->width * scale);
    int buffer_height = (int)lroundf((float)window->height * scale);
    if (buffer_width < 1) {
        buffer_width = 1;
    }
    if (buffer_height < 1) {
        buffer_height = 1;
    }
    if (!ensure_buffers(window, buffer_width, buffer_height)) {
        wl->needs_redraw = true;
        return false;
    }
    if (!ensure_backing(window, wl, buffer_width, buffer_height, scale)) {
        wl->needs_redraw = true;
        return false;
    }
    pt_wl_buffer *target = NULL;
    for (int i = 0; i < pt_wl_buffer_count; ++i) {
        if (!wl->buffers[i].busy) {
            target = &wl->buffers[i];
            break;
        }
    }
    if (!target) {
        wl->needs_redraw = true;
        return false;
    }

    wl->drawing = true;
    bool painted = pt_window_paint_damage(window, window->backing);
    wl->drawing = false;
    if (!painted) {
        return false;
    }
    pt_rect frame_damage = window->last_damage;
    bool frame_full = window->last_damage_full;
    if (frame_damage.w <= 0.0f || frame_damage.h <= 0.0f) {
        wl->needs_redraw = false;
        return true;
    }

    pt_rect copy = frame_full ? (pt_rect){0.0f, 0.0f, (float)window->width,
                                          (float)window->height}
                              : frame_damage;
    if (target->pending_full) {
        copy = (pt_rect){0.0f, 0.0f, (float)window->width, (float)window->height};
    } else if (target->pending_valid) {
        copy = rect_union(copy, target->pending_damage);
    }

    int pixel_x = 0;
    int pixel_y = 0;
    int pixel_w = 0;
    int pixel_h = 0;
    if (!damage_to_pixels(window->backing, copy, &pixel_x, &pixel_y, &pixel_w, &pixel_h)) {
        buffer_clear_damage(target);
        wl->needs_redraw = false;
        return true;
    }
    copy_backing_region(window->backing, target, wl, pixel_x, pixel_y, pixel_w, pixel_h);
    buffer_clear_damage(target);
    for (int i = 0; i < pt_wl_buffer_count; ++i) {
        if (&wl->buffers[i] != target) {
            buffer_add_damage(&wl->buffers[i], frame_damage, frame_full);
        }
    }

    pt_wl_window_update_viewport(window);
    if (platform->viewporter && wl->viewport) {
        wl_surface_set_buffer_scale(wl->surface, 1);
    } else if (window->scale > 0.05f) {
        wl_surface_set_buffer_scale(wl->surface, (int32_t)lroundf(window->scale));
    }
    target->busy = true;
    if (!wl->frame_callback) {
        wl->frame_callback = wl_surface_frame(wl->surface);
        wl_callback_add_listener(wl->frame_callback, &frame_listener, wl);
    }
    wl_surface_attach(wl->surface, target->buffer, 0, 0);
    if (wl_surface_get_version(wl->surface) >= 4) {
        wl_surface_damage_buffer(wl->surface, pixel_x, pixel_y, pixel_w, pixel_h);
    } else {
        wl_surface_damage(wl->surface, 0, 0, INT32_MAX, INT32_MAX);
    }
    wl_surface_commit(wl->surface);
    wl->needs_redraw = wl->redraw_pending;
    wl->redraw_pending = false;
    wl->mapped = true;
    return true;
}

/* ---------- xdg-shell ---------- */

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states) {
    (void)toplevel;
    pt_wayland_window *wl = data;
    wl->pending_width = width;
    wl->pending_height = height;
    bool maximized = false;
    bool fullscreen = false;
    uint32_t *state = states->data;
    for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i) {
        if (state[i] == XDG_TOPLEVEL_STATE_MAXIMIZED) {
            maximized = true;
        } else if (state[i] == XDG_TOPLEVEL_STATE_FULLSCREEN) {
            fullscreen = true;
        }
    }
    if (wl->window) {
        wl->window->maximized = maximized;
        wl->window->fullscreen = fullscreen;
        wl->window->needs_draw = true;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    pt_wayland_window *wl = data;
    if (wl->window) {
        pt_window_request_close(wl->window, false);
    }
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width,
                                      int32_t height) {
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
                                     struct wl_array *capabilities) {
    (void)data;
    (void)toplevel;
    (void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = toplevel_configure_bounds,
    .wm_capabilities = toplevel_wm_capabilities,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
    pt_wayland_window *wl = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    wl->configured = true;
    if (!wl->window) {
        return;
    }
    int width = wl->pending_width > 0 ? wl->pending_width : wl->window->width;
    int height = wl->pending_height > 0 ? wl->pending_height : wl->window->height;
    pt_window_apply_configure(wl->window, width, height);
    pt_wl_window_apply_scale(wl->window);
    pt_wl_window_schedule_frame(wl->window);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void decoration_configure(void *data,
                                 struct zxdg_toplevel_decoration_v1 *decoration,
                                 uint32_t mode) {
    (void)decoration;
    pt_wayland_window *wl = data;
    if (!wl->window) {
        return;
    }
    bool client_side = mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    pt_window_set_csd_enabled(wl->window, client_side);
    pt_wl_window_schedule_frame(wl->window);
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = decoration_configure,
};

static void fractional_scale_preferred(void *data, struct wp_fractional_scale_v1 *scale,
                                       uint32_t numerator) {
    (void)scale;
    pt_wayland_window *wl = data;
    if (!wl->window) {
        return;
    }
    float value = (float)numerator / 120.0f;
    if (value < 0.5f) {
        value = 0.5f;
    }
    if (value > 4.0f) {
        value = 4.0f;
    }
    wl->window->scale = value;
    pt_wl_window_apply_scale(wl->window);
    pt_wl_window_schedule_frame(wl->window);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_preferred,
};

static void surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
    (void)data;
    (void)surface;
    (void)output;
}

static void surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
    (void)data;
    (void)surface;
    (void)output;
}

static void surface_preferred_buffer_scale(void *data, struct wl_surface *surface,
                                           int32_t factor) {
    (void)surface;
    pt_wayland_window *wl = data;
    pt_platform *platform = pt_platform_for(wl->window);
    if (!wl->window) {
        return;
    }
    wl->window->scale = factor > 0 ? (float)factor : 1.0f;
    if (!platform || !platform->fractional_scale_manager) {
        pt_wl_window_apply_scale(wl->window);
    }
}

static void surface_preferred_buffer_transform(void *data, struct wl_surface *surface,
                                               uint32_t transform) {
    (void)data;
    (void)surface;
    (void)transform;
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
    .preferred_buffer_scale = surface_preferred_buffer_scale,
    .preferred_buffer_transform = surface_preferred_buffer_transform,
};

/* ---------- platform hooks ---------- */

bool pt_platform_window_init(pt_window *window) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform) {
        return false;
    }
    if (platform->headless) {
        window->configured = true;
        return true;
    }
    if (!platform->compositor || !platform->wm_base) {
        pt_set_error("compositor is missing required Wayland globals");
        return false;
    }
    pt_wayland_window *wl = pt_alloc_zeroed(1, sizeof(*wl));
    wl->window = window;
    wl->surface = wl_compositor_create_surface(platform->compositor);
    if (!wl->surface) {
        free(wl);
        pt_set_error("failed to create Wayland surface");
        return false;
    }
    wl_surface_add_listener(wl->surface, &surface_listener, wl);
    wl->xdg_surface = xdg_wm_base_get_xdg_surface(platform->wm_base, wl->surface);
    xdg_surface_add_listener(wl->xdg_surface, &xdg_surface_listener, wl);
    wl->toplevel = xdg_surface_get_toplevel(wl->xdg_surface);
    xdg_toplevel_add_listener(wl->toplevel, &toplevel_listener, wl);
    xdg_toplevel_set_app_id(wl->toplevel, window->app->app_id);
    xdg_toplevel_set_title(wl->toplevel, window->title ? window->title : "Pt");
    if (platform->decoration_manager) {
        wl->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            platform->decoration_manager, wl->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(wl->decoration, &decoration_listener, wl);
        zxdg_toplevel_decoration_v1_set_mode(
            wl->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
    if (platform->fractional_scale_manager) {
        wl->fractional_scale =
            wp_fractional_scale_manager_v1_get_fractional_scale(
                platform->fractional_scale_manager, wl->surface);
        wp_fractional_scale_v1_add_listener(wl->fractional_scale,
                                            &fractional_scale_listener, wl);
    }
    if (platform->viewporter) {
        wl->viewport = wp_viewporter_get_viewport(platform->viewporter, wl->surface);
    }
    wl->pending_width = window->width;
    wl->pending_height = window->height;
    window->platform_window = wl;
    pt_platform_window_set_size_hint(window);
    wl_surface_commit(wl->surface);
    return true;
}

void pt_platform_window_destroy(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        window->platform_window = NULL;
        return;
    }
    pt_platform *platform = pt_platform_for(window);
    if (platform && platform->text_input_window == window) {
        pt_platform_ime_set_enabled(window, false);
    }
    if (platform && platform->touch_focus == window) {
        platform->touch_focus = NULL;
    }
    pt_platform_popup_destroy_all(window);
    if (wl->frame_callback) {
        wl_callback_destroy(wl->frame_callback);
        wl->frame_callback = NULL;
    }
    if (wl->surface) {
        wl_surface_attach(wl->surface, NULL, 0, 0);
        wl_surface_commit(wl->surface);
    }
    destroy_buffers(wl);
    if (wl->viewport) {
        wp_viewport_destroy(wl->viewport);
    }
    if (wl->fractional_scale) {
        wp_fractional_scale_v1_destroy(wl->fractional_scale);
    }
    if (wl->decoration) {
        zxdg_toplevel_decoration_v1_destroy(wl->decoration);
    }
    if (wl->toplevel) {
        xdg_toplevel_destroy(wl->toplevel);
    }
    if (wl->xdg_surface) {
        xdg_surface_destroy(wl->xdg_surface);
    }
    if (wl->surface) {
        wl_surface_destroy(wl->surface);
    }
    free(wl);
    window->platform_window = NULL;
}

void pt_platform_window_show(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!platform) {
        return;
    }
    if (platform->headless) {
        window->shown = true;
        window->configured = true;
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
        return;
    }
    if (!wl) {
        return;
    }
    wl->needs_redraw = true;
    if (wl->configured) {
        pt_wl_window_schedule_frame(window);
    }
}

void pt_platform_window_hide(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    pt_platform_popup_destroy_all(window);
    if (wl->frame_callback) {
        wl_callback_destroy(wl->frame_callback);
        wl->frame_callback = NULL;
    }
    wl->needs_redraw = false;
    wl_surface_attach(wl->surface, NULL, 0, 0);
    wl_surface_commit(wl->surface);
    wl->mapped = false;
}

void pt_platform_window_set_title(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (wl && wl->toplevel) {
        xdg_toplevel_set_title(wl->toplevel, window->title ? window->title : "");
    }
}

void pt_platform_window_set_min_size(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (wl && wl->toplevel) {
        xdg_toplevel_set_min_size(wl->toplevel, window->min_width, window->min_height);
    }
}

void pt_platform_window_set_resizable(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl || !wl->toplevel) {
        return;
    }
    if (window->resizable) {
        xdg_toplevel_set_max_size(wl->toplevel, 0, 0);
        pt_platform_window_set_min_size(window);
    } else {
        xdg_toplevel_set_min_size(wl->toplevel, window->width, window->height);
        xdg_toplevel_set_max_size(wl->toplevel, window->width, window->height);
    }
}

void pt_platform_window_set_size_hint(pt_window *window) {
    pt_platform_window_set_resizable(window);
}

void pt_platform_window_set_decorated(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (wl && wl->decoration) {
        zxdg_toplevel_decoration_v1_set_mode(
            wl->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
    if (!wl && window->app && window->app->platform) {
        pt_platform *platform = (pt_platform *)window->app->platform;
        if (platform->headless) {
            window->csd = window->decorated;
            window->header_height = window->decorated ? pt_header_height : 0.0f;
            pt_window_invalidate_layout(window, NULL);
            window->needs_draw = true;
        }
    }
}

void pt_platform_window_schedule_draw(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    pt_wl_window_schedule_frame(window);
}

void pt_platform_window_begin_move(pt_window *window, uint32_t serial) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (wl && wl->toplevel && platform && platform->seat) {
        xdg_toplevel_move(wl->toplevel, platform->seat, serial);
    }
}

void pt_platform_window_begin_resize(pt_window *window, uint32_t serial, uint32_t edges) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (wl && wl->toplevel && platform && platform->seat && edges != 0) {
        xdg_toplevel_resize(wl->toplevel, platform->seat, serial, edges);
    }
}

void pt_platform_window_minimize(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (wl && wl->toplevel) {
        xdg_toplevel_set_minimized(wl->toplevel);
    }
}

void pt_platform_window_toggle_maximize(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl || !wl->toplevel) {
        return;
    }
    if (window->maximized) {
        xdg_toplevel_set_maximized(wl->toplevel);
    } else {
        xdg_toplevel_unset_maximized(wl->toplevel);
    }
}

void pt_platform_window_set_fullscreen(pt_window *window, bool fullscreen) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl || !wl->toplevel) {
        return;
    }
    if (fullscreen) {
        xdg_toplevel_set_fullscreen(wl->toplevel, NULL);
    } else {
        xdg_toplevel_unset_fullscreen(wl->toplevel);
    }
}
