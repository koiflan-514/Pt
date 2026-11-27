/*
 * xdg-foreign-unstable-v2 Wayland protocol objects. W1 fallback implementation.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "wayland_internal.h"

/* The exporter and importer globals are not bound in W1, so every request
 * takes the documented no-op/false fallback. W4 replaces these bodies with the
 * export/import lifecycle described in docs/m26-design.md. */

bool pt_platform_foreign_export(pt_window *window, pt_foreign_handle_callback callback,
                                void *user_data) {
    (void)window;
    (void)callback;
    (void)user_data;
    return false;
}

void pt_platform_foreign_unexport(pt_window *window) {
    (void)window;
}

bool pt_platform_foreign_set_parent(pt_window *window, const char *handle) {
    (void)window;
    (void)handle;
    return false;
}

void pt_platform_foreign_clear_parent(pt_window *window) {
    (void)window;
}

void pt_foreign_window_destroy(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    wl->foreign_exported = NULL;
    wl->foreign_export_callback = NULL;
    wl->foreign_export_data = NULL;
    wl->foreign_parent = NULL;
}

void pt_foreign_platform_destroy(pt_platform *platform) {
    (void)platform;
}