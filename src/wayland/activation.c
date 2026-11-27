/*
 * xdg-activation-v1 Wayland protocol objects. W1 fallback implementation.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "wayland_internal.h"

#include <stdlib.h>

/* The activation global is not bound in W1, so every request takes the
 * documented absent-global path: no object is created and the caller sees
 * false. W3 replaces these bodies with the protocol lifecycle described in
 * docs/m26-design.md. */

bool pt_platform_activation_token_request(pt_window *window, const char *app_id,
                                          pt_activation_token_callback callback,
                                          void *user_data) {
    (void)window;
    (void)app_id;
    (void)callback;
    (void)user_data;
    return false;
}

bool pt_platform_activation_activate(pt_window *window, const char *token) {
    (void)window;
    (void)token;
    return false;
}

void pt_activation_adopt_environment(pt_platform *platform) {
    (void)platform;
}

void pt_activation_window_mapped(pt_window *window) {
    (void)window;
}

void pt_activation_window_destroy(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    wl->activation_token = NULL;
    wl->activation_token_callback = NULL;
    wl->activation_token_data = NULL;
}

void pt_activation_platform_destroy(pt_platform *platform) {
    if (!platform) {
        return;
    }
    free(platform->pending_activation_token);
    platform->pending_activation_token = NULL;
}