/*
 * Activation tokens: the public xdg-activation-v1 entry points.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

/* Argument validation lives here so every platform backs the same contract.
 * The absent-global and headless false path belongs to the Wayland hook, which
 * returns false until W3 installs the protocol implementation. */

bool pt_window_request_activation_token(pt_window *window, const char *app_id,
                                         pt_activation_token_callback callback,
                                         void *user_data) {
    if (!window || !callback) {
        return false;
    }
    return pt_platform_activation_token_request(window, app_id, callback, user_data);
}

bool pt_window_activate(pt_window *window, const char *token) {
    if (!window || !token || !*token) {
        return false;
    }
    return pt_platform_activation_activate(window, token);
}