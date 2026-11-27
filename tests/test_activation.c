/*
 * xdg-activation-v1 core: token request, explicit activation, startup-token
 * adoption and object teardown.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Registry binding belongs to W5 (src/wayland/platform.c). Until that wave
 * lands, each wire test binds xdg_activation_v1 itself on a private registry
 * and installs the proxy in pt_platform, which is exactly the state W5 will
 * produce. Everything below then runs the native Pt path against the
 * header-only fake compositor: the public API, src/activation.c, the
 * src/wayland/activation.c hooks and the generated libwayland client code.
 */

#include "../src/internal.h"
#include "fake_compositor.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

/* ---------- opcode constants (from the vendored XML) ---------- */

#define activation_destroy 0u
#define activation_get_token 1u
#define activation_activate 2u

#define token_set_serial 0u
#define token_set_app_id 1u
#define token_set_surface 2u
#define token_commit 3u
#define token_destroy 4u

/* ---------- callback logs ---------- */

typedef struct token_log {
    int calls;
    pt_window *window;
    char token[64];
} token_log;

static void log_token(pt_window *window, const char *token, void *user_data) {
    token_log *log = user_data;
    ++log->calls;
    log->window = window;
    snprintf(log->token, sizeof(log->token), "%s", token ? token : "(null)");
}

typedef struct reentrant_ctx {
    pt_window *window;
    int outer_calls;
    int inner_calls;
    bool inner_request_ok;
    char outer[64];
    char inner[64];
} reentrant_ctx;

static void log_inner_token(pt_window *window, const char *token, void *user_data) {
    (void)window;
    reentrant_ctx *ctx = user_data;
    ++ctx->inner_calls;
    snprintf(ctx->inner, sizeof(ctx->inner), "%s", token ? token : "(null)");
}

static void log_outer_token(pt_window *window, const char *token, void *user_data) {
    reentrant_ctx *ctx = user_data;
    ++ctx->outer_calls;
    snprintf(ctx->outer, sizeof(ctx->outer), "%s", token ? token : "(null)");
    /* The done path has already cleared and destroyed the old object, so this
     * installs an independent token for the next event. */
    ctx->inner_request_ok =
        pt_window_request_activation_token(window, "pt.tests.inner", log_inner_token, ctx);
}

/* ---------- private registry bind (W5 placeholder) ---------- */

typedef struct activation_globals {
    uint32_t name;
    uint32_t version;
} activation_globals;

static void bind_global(void *data, struct wl_registry *registry, uint32_t name,
                        const char *interface, uint32_t version) {
    (void)registry;
    activation_globals *globals = data;
    if (strcmp(interface, "xdg_activation_v1") == 0) {
        globals->name = name;
        globals->version = version;
    }
}

static void bind_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener bind_listener = {
    .global = bind_global,
    .global_remove = bind_global_remove,
};

/* Creates a second registry on the live display, discovers the activation
 * global and binds version 1 into the platform record. W5 will do the same in
 * registry_global; this keeps W3 independent of platform.c until then. */
static bool bind_activation(pt_platform *platform) {
    if (!platform || !platform->display) {
        return false;
    }
    /* W5 wires the global into registry_global, so pt_app_create has already
     * bound it on a compositor that advertises it. The manual bind remains for
     * the fallback fixtures and must not create a second proxy. */
    if (platform->activation) {
        return true;
    }
    activation_globals globals = {0};
    struct wl_registry *registry = wl_display_get_registry(platform->display);
    if (!registry) {
        return false;
    }
    wl_registry_add_listener(registry, &bind_listener, &globals);
    if (wl_display_roundtrip(platform->display) < 0 || globals.name == 0) {
        wl_registry_destroy(registry);
        return false;
    }
    platform->activation =
        wl_registry_bind(registry, globals.name, &xdg_activation_v1_interface, 1);
    bool ok = platform->activation != NULL;
    if (wl_display_roundtrip(platform->display) < 0) {
        ok = false;
    }
    wl_registry_destroy(registry);
    return ok;
}

/* ---------- fixtures ---------- */

static bool wait_configured(fake_compositor *fc, pt_app *app, pt_window *window) {
    for (int i = 0; i < 400 && window && !window->configured; ++i) {
        fake_compositor_dispatch(fc, app, 5);
    }
    return window && window->configured;
}

static pt_window *open_window(fake_compositor *fc, pt_app *app, const char *title) {
    pt_window *window = pt_window_create(app, title, 320, 200);
    pt_check(window != NULL);
    if (!window) {
        return NULL;
    }
    pt_window_show(window);
    pt_check(wait_configured(fc, app, window));
    return window;
}

static uint32_t proxy_id(void *proxy) {
    return proxy ? wl_proxy_get_id((struct wl_proxy *)proxy) : 0;
}

/* Asserts the exact request sequence for one token: get_activation_token,
 * optional set_serial, optional set_app_id, set_surface, commit. Serial
 * 0 means "no recent input serial"; NULL/empty app_id means "no set_app_id". */
static void check_token_sequence(fake_compositor *fc, pt_platform *platform,
                                 uint32_t surface, uint32_t serial, const char *app_id) {
    fc_request get = {0};
    fc_request set_surface = {0};
    fc_request commit = {0};
    ssize_t i_get =
        fake_compositor_nth_request(fc, FC_K_ACTIVATION, activation_get_token, 0, &get);
    ssize_t i_surface =
        fake_compositor_nth_request(fc, FC_K_ACTIVATION_TOKEN, token_set_surface, 0,
                                    &set_surface);
    ssize_t i_commit =
        fake_compositor_nth_request(fc, FC_K_ACTIVATION_TOKEN, token_commit, 0, &commit);
    pt_check(i_get >= 0);
    pt_check(i_surface >= 0);
    pt_check(i_commit >= 0);
    pt_check(get.new_id != 0);
    pt_check(i_get < i_surface && i_surface < i_commit);
    if (i_surface >= 0 && set_surface.nargs >= 1) {
        pt_check(set_surface.args[0].u == surface);
    }

    if (serial) {
        fc_request set_serial = {0};
        ssize_t i_serial =
            fake_compositor_nth_request(fc, FC_K_ACTIVATION_TOKEN, token_set_serial, 0,
                                        &set_serial);
        pt_check(i_serial >= 0);
        pt_check(i_serial > i_get && i_serial < i_surface);
        pt_check(set_serial.nargs >= 2);
        pt_check(set_serial.args[0].u == serial);
        pt_check(set_serial.args[1].u == proxy_id(platform->seat));
    } else {
        pt_check(fake_compositor_count_requests(fc, FC_K_ACTIVATION_TOKEN,
                                                token_set_serial) == 0);
    }

    if (app_id && *app_id) {
        fc_request set_app = {0};
        ssize_t i_app =
            fake_compositor_nth_request(fc, FC_K_ACTIVATION_TOKEN, token_set_app_id, 0,
                                        &set_app);
        pt_check(i_app >= 0);
        pt_check(i_app > i_get && i_app < i_surface);
        pt_check(set_app.nargs >= 1);
        pt_check(set_app.args[0].has_text);
        pt_check(strcmp(set_app.args[0].text, app_id) == 0);
    } else {
        pt_check(fake_compositor_count_requests(fc, FC_K_ACTIVATION_TOKEN,
                                                token_set_app_id) == 0);
    }
}

/* ---------- headless ---------- */

static void test_headless_fallback(void) {
    setenv("PT_HEADLESS", "1", 1);
    setenv("XDG_ACTIVATION_TOKEN", "headless-token", 1);
    token_log log = {0};

    pt_check(!pt_window_request_activation_token(NULL, "pt.tests", log_token, &log));
    pt_check(!pt_window_request_activation_token(NULL, "pt.tests", NULL, NULL));

    pt_app *app = pt_app_create("pt.tests.activation.headless");
    pt_check(app != NULL);
    pt_platform *platform = app ? app->platform : NULL;
    pt_check(platform != NULL);
    if (platform) {
        pt_check(platform->headless);
        pt_check(platform->activation == NULL);
    }
    pt_window *window = app ? pt_window_create(app, "activation headless", 320, 200) : NULL;
    pt_check(window != NULL);
    if (window) {
        pt_check(!pt_window_request_activation_token(window, "pt.tests", log_token, &log));
        pt_check(!pt_window_request_activation_token(window, "pt.tests", NULL, NULL));
        pt_check(!pt_window_activate(window, "opaque-token"));
        pt_check(!pt_window_activate(window, ""));
        pt_check(!pt_window_activate(window, NULL));
    }
    pt_check(!pt_window_activate(NULL, "opaque-token"));
    pt_check(log.calls == 0);

    /* No global means the environment is neither read nor unset, even when the
     * hook is called directly (W5 calls it after the first roundtrip). */
    pt_activation_adopt_environment(platform);
    const char *kept = getenv("XDG_ACTIVATION_TOKEN");
    pt_check(kept != NULL && strcmp(kept, "headless-token") == 0);
    if (platform) {
        pt_check(platform->pending_activation_token == NULL);
    }
    pt_activation_window_destroy(NULL);
    pt_activation_platform_destroy(NULL);

    if (window) {
        pt_window_free(window);
    }
    if (app) {
        pt_app_destroy(app);
    }
    unsetenv("XDG_ACTIVATION_TOKEN");
    unsetenv("PT_HEADLESS");
}

/* ---------- absent global ---------- */

static void test_no_global_fallback(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL)); /* activation stays absent */
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    setenv("XDG_ACTIVATION_TOKEN", "unsupported-token", 1);
    token_log log = {0};

    pt_app *app = pt_app_create("pt.tests.activation.noglobal");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    pt_check(platform != NULL);
    if (!platform) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(!platform->headless);
    pt_check(platform->activation == NULL);
    pt_check(!fake_compositor_global_bound(&fc, FC_K_ACTIVATION));

    pt_window *window = open_window(&fc, app, "activation no global");
    if (window) {
        fake_compositor_clear(&fc);
        pt_check(!pt_window_request_activation_token(window, "pt.tests", log_token, &log));
        pt_check(!pt_window_request_activation_token(window, NULL, log_token, &log));
        pt_check(!pt_window_activate(window, "opaque-token"));
        pt_activation_adopt_environment(platform);
        pt_check(fake_compositor_sync(&fc, app));

        const char *kept = getenv("XDG_ACTIVATION_TOKEN");
        pt_check(kept != NULL && strcmp(kept, "unsupported-token") == 0);
        pt_check(platform->pending_activation_token == NULL);
        pt_check(log.calls == 0);
        pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                                activation_get_token) == 0);
        pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                                activation_activate) == 0);
        pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION) == 0);
        pt_activation_window_destroy(window);
        pt_window_free(window);
    }

    pt_activation_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
    unsetenv("XDG_ACTIVATION_TOKEN");
}

/* ---------- supported path ---------- */

static void test_supported_flow(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL | FC_OPT_ACTIVATION));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    unsetenv("XDG_ACTIVATION_TOKEN");

    pt_app *app = pt_app_create("pt.tests.activation.supported");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    pt_check(platform != NULL);
    if (!platform) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(bind_activation(platform));
    pt_check(platform->activation != NULL);
    pt_check(fake_compositor_global_bound(&fc, FC_K_ACTIVATION));

    pt_window *window = open_window(&fc, app, "activation supported");
    if (!window) {
        pt_activation_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_check(wl != NULL && wl->surface != NULL);
    uint32_t surface = proxy_id(wl->surface);
    pt_check(surface != 0);

    /* A pointer press and a later key press: the activation rule prefers the
     * pointer button serial and falls back to the key serial. */
    fake_compositor_pointer_enter(&fc, 7000u, surface, 20.0, 20.0);
    fake_compositor_pointer_button(&fc, 7001u, 10u, 0x110u, true);
    fake_compositor_keyboard_enter(&fc, 7002u, surface);
    fake_compositor_keyboard_key(&fc, 7003u, 11u, 30u, true);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(platform->button_serial == 7001u);
    pt_check(platform->key_serial == 7003u);
    pt_check(proxy_id(platform->seat) != 0);

    fake_compositor_clear(&fc);
    token_log first = {0};
    pt_check(pt_window_request_activation_token(window, "pt.tests.target", log_token, &first));
    pt_check(fake_compositor_sync(&fc, app));
    check_token_sequence(&fc, platform, surface, 7001u, "pt.tests.target");
    pt_check(wl->activation_token != NULL);
    pt_check(first.calls == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN) != 0);

    /* A second request destroys the old token object and drops its callback;
     * only the new token can deliver. */
    fake_compositor_clear(&fc);
    token_log second = {0};
    pt_check(pt_window_request_activation_token(window, "pt.tests.target2", log_token,
                                                 &second));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION_TOKEN, token_destroy) == 1);
    check_token_sequence(&fc, platform, surface, 7001u, "pt.tests.target2");
    pt_check(first.calls == 0);
    pt_check(second.calls == 0);

    pt_check(fake_compositor_activation_token_done(&fc, "token-alpha"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(second.calls == 1);
    pt_check(first.calls == 0);
    pt_check(second.window == window);
    pt_check(strcmp(second.token, "token-alpha") == 0);
    pt_check(wl->activation_token == NULL);
    pt_check(wl->activation_token_callback == NULL);
    pt_check(wl->activation_token_data == NULL);
    /* The destroy is marshalled from inside the done callback, after the
     * roundtrip flush; wait for it to reach the server log. */
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_ACTIVATION_TOKEN, token_destroy, 2,
                                          8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION_TOKEN, token_destroy) == 2);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN) == 0);

    /* Public validation and the explicit activate request. */
    fake_compositor_clear(&fc);
    pt_check(!pt_window_request_activation_token(window, "pt.tests", NULL, NULL));
    pt_check(!pt_window_activate(window, NULL));
    pt_check(!pt_window_activate(window, ""));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_get_token) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 0);
    pt_check(pt_window_activate(window, "token-alpha"));
    pt_check(fake_compositor_sync(&fc, app));
    fc_request activate = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_ACTIVATION, activation_activate,
                                          &activate));
    pt_check(activate.nargs >= 2);
    pt_check(activate.args[0].has_text);
    pt_check(strcmp(activate.args[0].text, "token-alpha") == 0);
    pt_check(activate.args[1].u == surface);
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 1);

    /* An unknown token is sent and ignored by the compositor; the connection
     * stays usable and a later real token still completes. */
    fake_compositor_clear(&fc);
    pt_check(pt_window_activate(window, "stale-or-invalid-token"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 1);
    token_log recovered = {0};
    pt_check(pt_window_request_activation_token(window, "pt.tests.recovered", log_token,
                                                &recovered));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_activation_token_done(&fc, "token-recovered"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(recovered.calls == 1);
    pt_check(strcmp(recovered.token, "token-recovered") == 0);
    pt_check(wl->activation_token == NULL);

    /* Window teardown destroys a still-pending token and never calls back. */
    fake_compositor_clear(&fc);
    token_log pending = {0};
    pt_check(pt_window_request_activation_token(window, "pt.tests.pending", log_token,
                                                &pending));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN) != 0);
    pt_activation_window_destroy(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(pending.calls == 0);
    pt_check(wl->activation_token == NULL);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION_TOKEN, token_destroy) >= 1);

    /* Platform teardown frees a pending startup string and destroys the
     * global object. */
    setenv("XDG_ACTIVATION_TOKEN", "pending-startup", 1);
    pt_activation_adopt_environment(platform);
    pt_check(getenv("XDG_ACTIVATION_TOKEN") == NULL);
    pt_check(platform->pending_activation_token != NULL);
    pt_check(strcmp(platform->pending_activation_token, "pending-startup") == 0);
    fake_compositor_clear(&fc);
    pt_activation_platform_destroy(platform);
    pt_check(platform->pending_activation_token == NULL);
    pt_check(platform->pending_activation_consumed == false);
    pt_check(platform->activation == NULL);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION, activation_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION) == 0);
    pt_check(fake_compositor_error(&fc) == NULL);

    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- startup environment adoption ---------- */

static void test_environment_adoption(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL | FC_OPT_ACTIVATION));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    unsetenv("XDG_ACTIVATION_TOKEN");

    pt_app *app = pt_app_create("pt.tests.activation.env");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    pt_check(platform != NULL);
    if (!platform) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(bind_activation(platform));
    pt_window *first = open_window(&fc, app, "activation env first");
    pt_window *second = open_window(&fc, app, "activation env second");
    if (!first || !second) {
        if (first) {
            pt_window_free(first);
        }
        if (second) {
            pt_window_free(second);
        }
        pt_activation_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    uint32_t first_surface = proxy_id(pt_wl_window_for(first)->surface);

    /* Flush the configure-driven frame for the two fresh windows before a token
     * exists. With W5 live wiring the real first mapped commit consumes the
     * token; the manual hook below must therefore be the first one to see it. */
    (void)fake_compositor_sync(&fc, app);
    (void)fake_compositor_sync(&fc, app);

    setenv("XDG_ACTIVATION_TOKEN", "startup-token-42", 1);
    fake_compositor_clear(&fc);
    pt_activation_adopt_environment(platform);
    pt_check(getenv("XDG_ACTIVATION_TOKEN") == NULL);
    pt_check(platform->pending_activation_token != NULL);
    pt_check(strcmp(platform->pending_activation_token, "startup-token-42") == 0);
    pt_check(platform->pending_activation_consumed == false);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 0);

    /* The first mapped window consumes the startup token exactly once. */
    pt_activation_window_mapped(first);
    pt_check(fake_compositor_sync(&fc, app));
    fc_request activate = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_ACTIVATION, activation_activate,
                                          &activate));
    pt_check(activate.nargs >= 2);
    pt_check(activate.args[0].has_text);
    pt_check(strcmp(activate.args[0].text, "startup-token-42") == 0);
    pt_check(activate.args[1].u == first_surface);
    pt_check(platform->pending_activation_token == NULL);
    pt_check(platform->pending_activation_consumed == true);
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 1);

    /* A second window, and a repeated map of the first, consume nothing. */
    pt_activation_window_mapped(second);
    pt_activation_window_mapped(first);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 1);

    /* An adopted empty value is unset and never becomes a request. */
    setenv("XDG_ACTIVATION_TOKEN", "", 1);
    pt_activation_adopt_environment(platform);
    pt_check(getenv("XDG_ACTIVATION_TOKEN") == NULL);
    pt_check(platform->pending_activation_token == NULL);
    pt_activation_window_mapped(second);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 1);

    pt_activation_window_destroy(first);
    pt_activation_window_destroy(second);
    pt_activation_platform_destroy(platform);
    pt_window_free(first);
    pt_window_free(second);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- serial omission, app_id omission, key fallback ---------- */

static void test_optional_fields(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL | FC_OPT_ACTIVATION));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    unsetenv("XDG_ACTIVATION_TOKEN");

    pt_app *app = pt_app_create("pt.tests.activation.optional");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    pt_check(platform != NULL);
    if (!platform) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(bind_activation(platform));
    pt_window *window = open_window(&fc, app, "activation optional");
    if (!window) {
        pt_activation_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    uint32_t surface = proxy_id(pt_wl_window_for(window)->surface);
    pt_check(platform->button_serial == 0);
    pt_check(platform->key_serial == 0);

    /* No serial, NULL app_id: only get_surface and commit are sent. */
    fake_compositor_clear(&fc);
    token_log null_app = {0};
    pt_check(pt_window_request_activation_token(window, NULL, log_token, &null_app));
    pt_check(fake_compositor_sync(&fc, app));
    check_token_sequence(&fc, platform, surface, 0, NULL);

    /* Empty app_id behaves like NULL. Re-request replaces the pending token. */
    fake_compositor_clear(&fc);
    pt_check(pt_window_request_activation_token(window, "", log_token, &null_app));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION_TOKEN, token_destroy) == 1);
    check_token_sequence(&fc, platform, surface, 0, "");
    /* An invalid token is delivered as an empty borrowed string, never NULL. */
    pt_check(fake_compositor_activation_token_done(&fc, ""));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(null_app.calls == 1);
    pt_check(strcmp(null_app.token, "") == 0);
    pt_check(pt_wl_window_for(window)->activation_token == NULL);

    /* Key-only input falls back to key_serial. */
    fake_compositor_clear(&fc);
    fake_compositor_keyboard_enter(&fc, 8100u, surface);
    fake_compositor_keyboard_key(&fc, 8101u, 20u, 30u, true);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(platform->button_serial == 0);
    pt_check(platform->key_serial == 8101u);
    token_log key_log = {0};
    pt_check(pt_window_request_activation_token(window, "pt.tests.key", log_token, &key_log));
    pt_check(fake_compositor_sync(&fc, app));
    check_token_sequence(&fc, platform, surface, 8101u, "pt.tests.key");
    pt_check(fake_compositor_activation_token_done(&fc, "key-token"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(key_log.calls == 1);

    pt_activation_window_destroy(window);
    pt_activation_platform_destroy(platform);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- callback re-entrancy ---------- */

static void test_reentrant_request(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL | FC_OPT_ACTIVATION));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    unsetenv("XDG_ACTIVATION_TOKEN");

    pt_app *app = pt_app_create("pt.tests.activation.reentrant");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    pt_check(platform != NULL);
    if (!platform) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(bind_activation(platform));
    pt_window *window = open_window(&fc, app, "activation reentrant");
    if (!window) {
        pt_activation_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);

    reentrant_ctx ctx = {.window = window};
    fake_compositor_clear(&fc);
    pt_check(pt_window_request_activation_token(window, "pt.tests.outer", log_outer_token,
                                                &ctx));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_activation_token_done(&fc, "outer-token"));
    pt_check(fake_compositor_sync(&fc, app));

    pt_check(ctx.outer_calls == 1);
    pt_check(strcmp(ctx.outer, "outer-token") == 0);
    pt_check(ctx.inner_request_ok);
    pt_check(ctx.inner_calls == 0);
    pt_check(wl->activation_token != NULL);
    /* The inner request is marshalled inside the outer callback, after the
     * roundtrip flush; wait for it to be decoded before counting. */
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_ACTIVATION, activation_get_token,
                                          2, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_get_token) == 2);

    pt_check(fake_compositor_activation_token_done(&fc, "inner-token"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(ctx.inner_calls == 1);
    pt_check(strcmp(ctx.inner, "inner-token") == 0);
    pt_check(wl->activation_token == NULL);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_ACTIVATION_TOKEN, token_destroy, 2,
                                          8));

    pt_activation_window_destroy(window);
    pt_activation_platform_destroy(platform);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

int main(void) {
    test_headless_fallback();
    test_no_global_fallback();
    test_supported_flow();
    test_environment_adoption();
    test_optional_fields();
    test_reentrant_request();
    return pt_test_finish("activation");
}