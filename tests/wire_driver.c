/*
 * Minimal C wire driver for the binding tests (1.3 wave 2).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * tests/fake_compositor.h is header-only C: it uses `_Atomic`, C-only goto
 * rules and implicit void* conversions, so it cannot be included from C++ or
 * from a ctypes/FFM test directly. This translation unit is the smallest
 * adapter that keeps the facility compiled as C and exposes a flat C ABI to
 * the C++/Python/Java binding tests.
 *
 * The driver deliberately never calls Pt internals: it drives the client
 * through wl_display_roundtrip() and reads only the platform display pointer
 * from the app it is handed. That keeps libwire_driver.so free of hidden Pt
 * symbols, so it can be loaded next to libpt.so (Python) or next to
 * libpt.a-linked tests (C++, Java) without a second copy of the library.
 */

#include "fake_compositor.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static fake_compositor g_fc;
static bool g_started;

/* ---------- lifecycle ---------- */

int wd_start(unsigned int options) {
    if (g_started) {
        return -1;
    }
    if (!fake_compositor_start(&g_fc, options)) {
        return -1;
    }
    g_started = true;
    return 0;
}

void wd_stop(void) {
    if (!g_started) {
        return;
    }
    fake_compositor_stop(&g_fc);
    g_started = false;
}

int wd_started(void) {
    return g_started ? 1 : 0;
}

const char *wd_error(void) {
    return fake_compositor_error(&g_fc);
}

/* ---------- client driving ---------- */

/* One wl_display roundtrip: flushes requests and dispatches every event that
 * was already on the wire, including events injected just before this call. */
int wd_sync(void *app) {
    if (!app) {
        return -1;
    }
    return fake_compositor_sync(&g_fc, (pt_app *)app) ? 0 : -1;
}

int wd_settle(void *app) {
    if (!app) {
        return -1;
    }
    return fake_compositor_settle(&g_fc, (pt_app *)app) ? 0 : -1;
}

/* Roundtrips until the toplevel reports configured, up to `attempts` times.
 * Returns 1 when configured, 0 when still pending, -1 on a wire failure. */
int wd_wait_configured(void *app, void *window, int attempts) {
    if (!app || !window) {
        return -1;
    }
    pt_window *client_window = (pt_window *)window;
    for (int i = 0; i < attempts; ++i) {
        if (client_window->configured) {
            return 1;
        }
        if (!fake_compositor_sync(&g_fc, (pt_app *)app)) {
            return -1;
        }
    }
    return client_window->configured ? 1 : 0;
}

/* ---------- query ---------- */

uint32_t wd_toplevel_surface(void) {
    return fake_compositor_toplevel_surface(&g_fc);
}

uint32_t wd_toplevel(void) {
    return fake_compositor_toplevel(&g_fc);
}

uint32_t wd_live_id(uint32_t kind) {
    return fake_compositor_live_id(&g_fc, kind);
}

int wd_global_bound(uint32_t kind) {
    return fake_compositor_global_bound(&g_fc, kind) ? 1 : 0;
}

int wd_request_count(uint32_t kind, uint32_t opcode) {
    return (int)fake_compositor_count_requests(&g_fc, kind, (uint16_t)opcode);
}

int wd_request_overflowed(void) {
    return fake_compositor_request_overflowed(&g_fc) ? 1 : 0;
}

int wd_pending_fd_count(void) {
    return fake_compositor_pending_fd_count(&g_fc);
}

/* ---------- injected 2.4 protocol events ---------- */

/* xdg_activation_token_v1.done: delivers the opaque token the compositor
 * generated. Returns 0 when the client has no live token object. */
int wd_activation_token_done(const char *token) {
    return fake_compositor_activation_token_done(&g_fc, token) ? 1 : 0;
}

/* zxdg_exported_v2.handle: delivers the opaque handle for the export.
 * Returns 0 when the client has no live exported object. */
int wd_exported_handle(const char *handle) {
    return fake_compositor_exported_handle(&g_fc, handle) ? 1 : 0;
}

/* ---------- injected keyboard events ---------- */

void wd_keyboard_enter(uint32_t serial, uint32_t surface) {
    fake_compositor_keyboard_enter(&g_fc, serial, surface);
}

void wd_keyboard_leave(uint32_t serial, uint32_t surface) {
    fake_compositor_keyboard_leave(&g_fc, serial, surface);
}

/* Physical evdev keycode, without the Wayland +8 offset. */
void wd_keyboard_key(uint32_t serial, uint32_t time, uint32_t keycode, int pressed) {
    fake_compositor_keyboard_key(&g_fc, serial, time, keycode, pressed != 0);
}

void wd_keyboard_modifiers(uint32_t serial, uint32_t depressed, uint32_t latched,
                           uint32_t locked, uint32_t group) {
    fake_compositor_keyboard_modifiers(&g_fc, serial, depressed, latched, locked, group);
}

/* ---------- injected pointer events ---------- */

void wd_pointer_enter(uint32_t serial, uint32_t surface, double x, double y) {
    fake_compositor_pointer_enter(&g_fc, serial, surface, x, y);
}

void wd_pointer_leave(uint32_t serial, uint32_t surface) {
    fake_compositor_pointer_leave(&g_fc, serial, surface);
}

void wd_pointer_motion(uint32_t time, double x, double y) {
    fake_compositor_pointer_motion(&g_fc, time, x, y);
}

void wd_pointer_button(uint32_t serial, uint32_t time, uint32_t button, int pressed) {
    fake_compositor_pointer_button(&g_fc, serial, time, button, pressed != 0);
}

void wd_pointer_axis(uint32_t time, uint32_t axis, double value) {
    fake_compositor_pointer_axis(&g_fc, time, axis, value);
}