/*
 * Foreign toplevel handles: the public xdg-foreign-unstable-v2 entry points.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

/* Argument validation lives here; the Wayland hook owns the object lifecycle
 * and the absent-global/headless fallback. W1 ships the fallback semantics and
 * W4 fills in the protocol bodies. */

bool pt_window_export_foreign_handle(pt_window *window,
                                     pt_foreign_handle_callback callback,
                                     void *user_data) {
    if (!window || !callback) {
        return false;
    }
    return pt_platform_foreign_export(window, callback, user_data);
}

void pt_window_unexport_foreign_handle(pt_window *window) {
    if (!window) {
        return;
    }
    pt_platform_foreign_unexport(window);
}

bool pt_window_set_foreign_parent(pt_window *window, const char *handle) {
    if (!window || !handle || !*handle) {
        return false;
    }
    return pt_platform_foreign_set_parent(window, handle);
}

void pt_window_clear_foreign_parent(pt_window *window) {
    if (!window) {
        return;
    }
    pt_platform_foreign_clear_parent(window);
}