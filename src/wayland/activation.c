/*
 * xdg-activation-v1: activation tokens and surface activation.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The protocol is best effort: a token request produces an asynchronous token
 * string, and activate() only asks the compositor to raise a surface. Neither
 * call promises that focus moves. Everything is gated on the optional global
 * and on a headless platform, so an unsupported compositor sees no behavior
 * change and no protocol object.
 */

#include "wayland_internal.h"

#include <stdlib.h>
#include <string.h>

/* One token object per window. The listener data is the window record, whose
 * fields hold the callback; that keeps the done path independent of the proxy
 * user data and lets the teardown hook clear everything in one place. */
static void activation_token_done(void *data, struct xdg_activation_token_v1 *token,
                                  const char *value);

static const struct xdg_activation_token_v1_listener activation_token_listener = {
    .done = activation_token_done,
};

/* Destroys a pending token object and drops its callback without invoking it.
 * Safe to call with a half-cleared record; a NULL object is ignored. */
static void activation_token_release(pt_wayland_window *wl) {
    if (!wl) {
        return;
    }
    if (wl->activation_token) {
        xdg_activation_token_v1_destroy(wl->activation_token);
        wl->activation_token = NULL;
    }
    wl->activation_token_callback = NULL;
    wl->activation_token_data = NULL;
}

static void activation_token_done(void *data, struct xdg_activation_token_v1 *token,
                                  const char *value) {
    pt_wayland_window *wl = data;
    if (!wl || wl->activation_token != token) {
        return;
    }
    /* Copy the borrowed string, clear the window state and destroy the object
     * before calling out. A re-entrant request from the callback therefore
     * installs a fresh, independent token instead of clobbering this call. */
    pt_activation_token_callback callback = wl->activation_token_callback;
    void *user_data = wl->activation_token_data;
    char *copy = strdup(value ? value : "");
    activation_token_release(wl);
    if (callback) {
        callback(wl->window, copy ? copy : "", user_data);
    }
    free(copy);
}

bool pt_platform_activation_token_request(pt_window *window, const char *app_id,
                                          pt_activation_token_callback callback,
                                          void *user_data) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || platform->headless || !platform->activation || !wl->surface) {
        return false;
    }
    /* A second request invalidates the previous token object and drops its
     * pending callback; the two requests never share state. */
    activation_token_release(wl);
    struct xdg_activation_token_v1 *token =
        xdg_activation_v1_get_activation_token(platform->activation);
    if (!token) {
        return false;
    }
    wl->activation_token = token;
    wl->activation_token_callback = callback;
    wl->activation_token_data = user_data;
    xdg_activation_token_v1_add_listener(token, &activation_token_listener, wl);

    /* Reuse the clipboard rule: the latest pointer button serial wins, with
     * the last key press as the fallback. A compositor may refuse a token
     * without a recent serial, but the request itself is still valid. */
    uint32_t serial = platform->button_serial ? platform->button_serial : platform->key_serial;
    if (serial && platform->seat) {
        xdg_activation_token_v1_set_serial(token, serial, platform->seat);
    }
    if (app_id && *app_id) {
        xdg_activation_token_v1_set_app_id(token, app_id);
    }
    xdg_activation_token_v1_set_surface(token, wl->surface);
    xdg_activation_token_v1_commit(token);
    return true;
}

bool pt_platform_activation_activate(pt_window *window, const char *token) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || platform->headless || !platform->activation || !wl->surface ||
        !token || !*token) {
        return false;
    }
    /* A true return means the request was sent. An unknown or stale token is
     * ignored by the compositor, which is the protocol's documented recovery
     * path; no local object is retained. */
    xdg_activation_v1_activate(platform->activation, token, wl->surface);
    return true;
}

void pt_activation_adopt_environment(pt_platform *platform) {
    /* Unsupported compositors must see the environment exactly as they did
     * before M26, so the variable is neither read nor unset without a global. */
    if (!platform || platform->headless || !platform->activation) {
        return;
    }
    const char *value = getenv("XDG_ACTIVATION_TOKEN");
    if (!value) {
        return;
    }
    char *copy = value[0] ? strdup(value) : NULL;
    unsetenv("XDG_ACTIVATION_TOKEN");
    free(platform->pending_activation_token);
    platform->pending_activation_token = copy;
    platform->pending_activation_consumed = false;
}

void pt_activation_window_mapped(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || platform->headless || !platform->activation || !wl->surface) {
        return;
    }
    if (platform->pending_activation_consumed || !platform->pending_activation_token) {
        return;
    }
    /* Consume exactly once: the first mapped window takes the token, and every
     * later window finds the flag set. Free the string after the request has
     * marshalled it; activate() does not retain it. */
    char *token = platform->pending_activation_token;
    platform->pending_activation_token = NULL;
    platform->pending_activation_consumed = true;
    xdg_activation_v1_activate(platform->activation, token, wl->surface);
    free(token);
}

void pt_activation_window_destroy(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    activation_token_release(wl);
}

void pt_activation_platform_destroy(pt_platform *platform) {
    if (!platform) {
        return;
    }
    free(platform->pending_activation_token);
    platform->pending_activation_token = NULL;
    platform->pending_activation_consumed = false;
    if (platform->activation) {
        xdg_activation_v1_destroy(platform->activation);
        platform->activation = NULL;
    }
}