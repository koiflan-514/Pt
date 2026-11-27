/*
 * M26 fake-compositor protocol facility (W2).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is the facility self-test for the xdg-activation-v1 and
 * xdg-foreign-unstable-v2 extensions added to tests/fake_compositor.h. It does
 * not exercise the public Pt API (that is W3/W4); it drives the generated
 * libwayland client protocol objects directly against the fake compositor and
 * asserts that:
 *
 *   - the two new FC_OPT_* bits are appended and stay out of FC_OPT_ALL;
 *   - the six appended interface kinds keep their numeric tail after
 *     FC_K_TABLET_TOOL, and each interface table carries the exact XML opcodes;
 *   - the optional globals are advertised only when their bit is set, and are
 *     absent (the fallback path W3/W4 test against) otherwise;
 *   - every request the XML defines is decoded with typed arguments
 *     (activate token/surface, set_serial/set_app_id/set_surface,
 *     export_toplevel(surface), import_toplevel(handle), set_parent_of(surface))
 *     and reflects the server-side object graph;
 *   - the injected events (token.done, exported.handle, imported.destroyed)
 *     reach the client listeners with the configured strings, including the
 *     empty-token and invalid-handle paths.
 *
 * The request/event opcodes match protocols/xdg-activation-v1.xml and
 * protocols/xdg-foreign-unstable-v2.xml (read directly, 2026-11-27).
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
#define token_done 0u

#define exporter_destroy 0u
#define exporter_export_toplevel 1u

#define exported_destroy 0u
#define exported_handle 0u

#define importer_destroy 0u
#define importer_import_toplevel 1u

#define imported_destroy 0u
#define imported_set_parent_of 1u
#define imported_destroyed 0u

/* ---------- listener bookkeeping ---------- */

typedef struct wire_token_log {
    int done_calls;
    char token[64];
} wire_token_log;

static void wire_token_done(void *data, struct xdg_activation_token_v1 *token,
                            const char *token_str) {
    (void)token;
    wire_token_log *log = data;
    ++log->done_calls;
    snprintf(log->token, sizeof(log->token), "%s", token_str ? token_str : "");
}

static const struct xdg_activation_token_v1_listener wire_token_listener = {
    .done = wire_token_done,
};

typedef struct wire_exported_log {
    int handle_calls;
    char handle[64];
} wire_exported_log;

static void wire_exported_handle(void *data, struct zxdg_exported_v2 *exported,
                                 const char *handle) {
    (void)exported;
    wire_exported_log *log = data;
    ++log->handle_calls;
    snprintf(log->handle, sizeof(log->handle), "%s", handle ? handle : "");
}

static const struct zxdg_exported_v2_listener wire_exported_listener = {
    .handle = wire_exported_handle,
};

typedef struct wire_imported_log {
    int destroyed_calls;
} wire_imported_log;

static void wire_imported_destroyed(void *data, struct zxdg_imported_v2 *imported) {
    (void)imported;
    wire_imported_log *log = data;
    ++log->destroyed_calls;
}

static const struct zxdg_imported_v2_listener wire_imported_listener = {
    .destroyed = wire_imported_destroyed,
};

/* ---------- raw registry bind helper ---------- */

typedef struct wire_globals {
    uint32_t activation_name;
    uint32_t activation_version;
    uint32_t exporter_name;
    uint32_t exporter_version;
    uint32_t importer_name;
    uint32_t importer_version;
    int seen;
} wire_globals;

static void wire_registry_global(void *data, struct wl_registry *registry, uint32_t name,
                                 const char *interface, uint32_t version) {
    (void)registry;
    wire_globals *globals = data;
    if (strcmp(interface, "xdg_activation_v1") == 0) {
        globals->activation_name = name;
        globals->activation_version = version;
        ++globals->seen;
    } else if (strcmp(interface, "zxdg_exporter_v2") == 0) {
        globals->exporter_name = name;
        globals->exporter_version = version;
        ++globals->seen;
    } else if (strcmp(interface, "zxdg_importer_v2") == 0) {
        globals->importer_name = name;
        globals->importer_version = version;
        ++globals->seen;
    }
}

static void wire_registry_global_remove(void *data, struct wl_registry *registry,
                                        uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener wire_registry_listener = {
    .global = wire_registry_global,
    .global_remove = wire_registry_global_remove,
};

/* Collects the new protocol globals from a fresh registry. Returns the number
 * of the three globals seen. */
static int wire_scan_globals(fake_compositor *fc, pt_app *app, wire_globals *out) {
    memset(out, 0, sizeof(*out));
    struct wl_display *display = app->platform->display;
    struct wl_registry *registry = wl_display_get_registry(display);
    pt_check(registry != NULL);
    if (!registry) {
        return 0;
    }
    pt_check(wl_registry_add_listener(registry, &wire_registry_listener, out) == 0);
    pt_check(fake_compositor_settle(fc, app));
    int seen = out->seen;
    wl_registry_destroy(registry);
    pt_check(fake_compositor_sync(fc, app));
    return seen;
}

/* ---------- static facility shape ---------- */

static void test_option_bits_and_all(void) {
    /* Appended after the touch bit (1u << 10); FC_OPT_ALL excludes them. */
    pt_check(FC_OPT_TOUCH == (1u << 10));
    pt_check(FC_OPT_ACTIVATION == (1u << 11));
    pt_check(FC_OPT_FOREIGN == (1u << 12));
    pt_check((FC_OPT_ALL & FC_OPT_ACTIVATION) == 0);
    pt_check((FC_OPT_ALL & FC_OPT_FOREIGN) == 0);
    pt_check((FC_OPT_ALL & FC_OPT_TOUCH) == 0);
    /* Every named global stays inside FC_OPT_ALL. */
    pt_check((FC_OPT_ALL & FC_OPT_ICON) != 0);
    pt_check((FC_OPT_ALL & FC_OPT_TABLET) != 0);
    /* The two extension bits do not alias each other or an old bit. */
    pt_check(FC_OPT_ACTIVATION != FC_OPT_FOREIGN);
    pt_check((FC_OPT_ALL & ~(FC_OPT_TOUCH | FC_OPT_ACTIVATION | FC_OPT_FOREIGN)) ==
             FC_OPT_ALL);

    /* Six kinds appended after FC_K_TABLET_TOOL, before FC_K_COUNT. */
    pt_check(FC_K_ACTIVATION == FC_K_TABLET_TOOL + 1);
    pt_check(FC_K_ACTIVATION_TOKEN == FC_K_ACTIVATION + 1);
    pt_check(FC_K_EXPORTER == FC_K_ACTIVATION_TOKEN + 1);
    pt_check(FC_K_EXPORTED == FC_K_EXPORTER + 1);
    pt_check(FC_K_IMPORTER == FC_K_EXPORTED + 1);
    pt_check(FC_K_IMPORTED == FC_K_IMPORTER + 1);
    pt_check(FC_K_COUNT == FC_K_IMPORTED + 1);

    /* Name -> kind mapping is bidirectional for the new interfaces. */
    pt_check(fc_kind_from_name("xdg_activation_v1") == FC_K_ACTIVATION);
    pt_check(fc_kind_from_name("xdg_activation_token_v1") == FC_K_ACTIVATION_TOKEN);
    pt_check(fc_kind_from_name("zxdg_exporter_v2") == FC_K_EXPORTER);
    pt_check(fc_kind_from_name("zxdg_exported_v2") == FC_K_EXPORTED);
    pt_check(fc_kind_from_name("zxdg_importer_v2") == FC_K_IMPORTER);
    pt_check(fc_kind_from_name("zxdg_imported_v2") == FC_K_IMPORTED);

    /* Interface table names and versions match the XML (all version 1). */
    pt_check(strcmp(fc_ifaces[FC_K_ACTIVATION].name, "xdg_activation_v1") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_ACTIVATION_TOKEN].name,
                    "xdg_activation_token_v1") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_EXPORTER].name, "zxdg_exporter_v2") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_EXPORTED].name, "zxdg_exported_v2") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_IMPORTER].name, "zxdg_importer_v2") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_IMPORTED].name, "zxdg_imported_v2") == 0);
    pt_check(fc_ifaces[FC_K_ACTIVATION].version == 1);
    pt_check(fc_ifaces[FC_K_ACTIVATION_TOKEN].version == 1);
    pt_check(fc_ifaces[FC_K_EXPORTER].version == 1);
    pt_check(fc_ifaces[FC_K_EXPORTED].version == 1);
    pt_check(fc_ifaces[FC_K_IMPORTER].version == 1);
    pt_check(fc_ifaces[FC_K_IMPORTED].version == 1);

    /* Request counts and signatures are the XML request lists verbatim. */
    pt_check(fc_ifaces[FC_K_ACTIVATION].req_count == 3);
    pt_check(fc_ifaces[FC_K_ACTIVATION_TOKEN].req_count == 5);
    pt_check(fc_ifaces[FC_K_EXPORTER].req_count == 2);
    pt_check(fc_ifaces[FC_K_EXPORTED].req_count == 1);
    pt_check(fc_ifaces[FC_K_IMPORTER].req_count == 2);
    pt_check(fc_ifaces[FC_K_IMPORTED].req_count == 2);

    const fc_req_desc *activation = fc_ifaces[FC_K_ACTIVATION].reqs;
    pt_check(strcmp(activation[activation_destroy].name, "destroy") == 0);
    pt_check(activation[activation_destroy].destructor);
    pt_check(strcmp(activation[activation_get_token].name, "get_activation_token") == 0);
    pt_check(activation[activation_get_token].new_kind == FC_K_ACTIVATION_TOKEN);
    pt_check(strcmp(activation[activation_get_token].sig, "n") == 0);
    pt_check(strcmp(activation[activation_activate].name, "activate") == 0);
    pt_check(strcmp(activation[activation_activate].sig, "so") == 0);

    const fc_req_desc *token = fc_ifaces[FC_K_ACTIVATION_TOKEN].reqs;
    pt_check(strcmp(token[token_set_serial].name, "set_serial") == 0);
    pt_check(strcmp(token[token_set_serial].sig, "uo") == 0);
    pt_check(strcmp(token[token_set_app_id].name, "set_app_id") == 0);
    pt_check(strcmp(token[token_set_app_id].sig, "s") == 0);
    pt_check(strcmp(token[token_set_surface].name, "set_surface") == 0);
    pt_check(strcmp(token[token_set_surface].sig, "o") == 0);
    pt_check(strcmp(token[token_commit].name, "commit") == 0);
    pt_check(strcmp(token[token_commit].sig, "") == 0);
    pt_check(strcmp(token[token_destroy].name, "destroy") == 0);
    pt_check(token[token_destroy].destructor);

    const fc_req_desc *exporter = fc_ifaces[FC_K_EXPORTER].reqs;
    pt_check(strcmp(exporter[exporter_export_toplevel].name, "export_toplevel") == 0);
    pt_check(exporter[exporter_export_toplevel].new_kind == FC_K_EXPORTED);
    pt_check(strcmp(exporter[exporter_export_toplevel].sig, "no") == 0);

    const fc_req_desc *importer = fc_ifaces[FC_K_IMPORTER].reqs;
    pt_check(strcmp(importer[importer_import_toplevel].name, "import_toplevel") == 0);
    pt_check(importer[importer_import_toplevel].new_kind == FC_K_IMPORTED);
    pt_check(strcmp(importer[importer_import_toplevel].sig, "ns") == 0);

    const fc_req_desc *imported = fc_ifaces[FC_K_IMPORTED].reqs;
    pt_check(strcmp(imported[imported_set_parent_of].name, "set_parent_of") == 0);
    pt_check(strcmp(imported[imported_set_parent_of].sig, "o") == 0);
}

/* ---------- optional-global fallback (unsupported path) ---------- */

static void test_no_bit_no_global(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL)); /* no activation/foreign */
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.none");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));

    wire_globals globals;
    pt_check(wire_scan_globals(&fc, app, &globals) == 0);
    pt_check(globals.activation_name == 0);
    pt_check(globals.exporter_name == 0);
    pt_check(globals.importer_name == 0);
    /* None of the extension kinds were ever bound or created. */
    pt_check(!fake_compositor_global_bound(&fc, FC_K_ACTIVATION));
    pt_check(!fake_compositor_global_bound(&fc, FC_K_EXPORTER));
    pt_check(!fake_compositor_global_bound(&fc, FC_K_IMPORTER));
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION) == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_EXPORTER) == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTER) == 0);
    /* Zero requests on any extension interface. */
    for (uint32_t kind = FC_K_ACTIVATION; kind <= FC_K_IMPORTED; ++kind) {
        pt_check(fake_compositor_count_requests(&fc, kind, 0) == 0);
        pt_check(fake_compositor_count_requests(&fc, kind, 1) == 0);
    }
    /* Injecting into a nonexistent object is a no-op, not a crash. */
    pt_check(!fake_compositor_activation_token_done(&fc, "abc"));
    pt_check(!fake_compositor_exported_handle(&fc, "abc"));
    pt_check(!fake_compositor_imported_destroyed(&fc));

    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* Each bit advertises only its own global. */
static void test_activation_bit_only(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ACTIVATION));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.activation-only");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    wire_globals globals;
    pt_check(wire_scan_globals(&fc, app, &globals) == 1);
    pt_check(globals.activation_name != 0);
    pt_check(globals.activation_version == 1);
    pt_check(globals.exporter_name == 0);
    pt_check(globals.importer_name == 0);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

static void test_foreign_bit_only(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.foreign-only");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    wire_globals globals;
    pt_check(wire_scan_globals(&fc, app, &globals) == 2);
    pt_check(globals.activation_name == 0);
    pt_check(globals.exporter_name != 0);
    pt_check(globals.exporter_version == 1);
    pt_check(globals.importer_name != 0);
    pt_check(globals.importer_version == 1);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- activation wire (supported path) ---------- */

static pt_window *wire_open_window(fake_compositor *fc, pt_app *app) {
    pt_window *window = pt_window_create(app, "wire-proto", 320, 240);
    pt_check(window != NULL);
    if (!window) {
        return NULL;
    }
    pt_window_show(window);
    for (int i = 0; i < 400 && !window->configured; ++i) {
        fake_compositor_dispatch(fc, app, 5);
    }
    pt_check(window->configured);
    pt_check(window->platform_window != NULL);
    pt_check(window->platform_window->surface != NULL);
    return window;
}

static struct xdg_activation_v1 *wire_bind_activation(fake_compositor *fc, pt_app *app,
                                                     uint32_t *out_name) {
    wire_globals globals;
    pt_check(wire_scan_globals(fc, app, &globals) >= 1);
    if (!globals.activation_name) {
        return NULL;
    }
    struct wl_registry *registry = wl_display_get_registry(app->platform->display);
    if (!registry) {
        return NULL;
    }
    struct xdg_activation_v1 *activation = wl_registry_bind(
        registry, globals.activation_name, &xdg_activation_v1_interface, 1);
    pt_check(activation != NULL);
    wl_registry_destroy(registry);
    pt_check(fake_compositor_sync(fc, app));
    if (out_name) {
        *out_name = globals.activation_name;
    }
    return activation;
}

static void test_activation_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ACTIVATION));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.activation");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_window *window = wire_open_window(&fc, app);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    uint32_t surface = window->platform_window->surface
                           ? wl_proxy_get_id((struct wl_proxy *)window->platform_window->surface)
                           : 0;
    pt_check(surface != 0);

    struct xdg_activation_v1 *activation = wire_bind_activation(&fc, app, NULL);
    pt_check(activation != NULL);
    if (!activation) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_global_bound(&fc, FC_K_ACTIVATION));
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION) != 0);

    /* get_activation_token creates a token object of the right kind. */
    fake_compositor_clear(&fc);
    struct xdg_activation_token_v1 *token =
        xdg_activation_v1_get_activation_token(activation);
    pt_check(token != NULL);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_get_token) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN) != 0);
    uint32_t token_id = fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN);
    if (!token_id) {
        xdg_activation_v1_destroy(activation);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }

    wire_token_log log;
    memset(&log, 0, sizeof(log));
    pt_check(xdg_activation_token_v1_add_listener(token, &wire_token_listener, &log) == 0);

    /* set_serial(serial, seat) + set_app_id(string) + set_surface(surface) +
     * commit: the exact pre-commit sequence, decoded with typed arguments. */
    struct wl_seat *seat = app->platform->seat;
    pt_check(seat != NULL);
    uint32_t seat_id = seat ? wl_proxy_get_id((struct wl_proxy *)seat) : 0;
    fake_compositor_clear(&fc);
    if (seat) {
        xdg_activation_token_v1_set_serial(token, 4242u, seat);
    }
    xdg_activation_token_v1_set_app_id(token, "pt.tests.wire");
    xdg_activation_token_v1_set_surface(token, window->platform_window->surface);
    xdg_activation_token_v1_commit(token);
    pt_check(fake_compositor_sync(&fc, app));

    fc_request request;
    pt_check(fake_compositor_last_request(&fc, FC_K_ACTIVATION_TOKEN,
                                          token_set_serial, &request));
    pt_check(request.nargs == 2);
    pt_check(request.args[0].u == 4242u);
    pt_check(request.args[1].u == seat_id);
    pt_check(fake_compositor_last_request(&fc, FC_K_ACTIVATION_TOKEN, token_set_app_id,
                                          &request));
    pt_check(request.nargs == 1 && request.args[0].has_text);
    pt_check(strcmp(request.args[0].text, "pt.tests.wire") == 0);
    pt_check(fake_compositor_last_request(&fc, FC_K_ACTIVATION_TOKEN, token_set_surface,
                                          &request));
    pt_check(request.nargs == 1 && request.args[0].u == surface);
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION_TOKEN, token_commit) == 1);
    /* set_serial precedes set_app_id precedes set_surface precedes commit. */
    ssize_t serial_at = fake_compositor_nth_request(&fc, FC_K_ACTIVATION_TOKEN,
                                                    token_set_serial, 0, NULL);
    ssize_t app_at = fake_compositor_nth_request(&fc, FC_K_ACTIVATION_TOKEN,
                                                 token_set_app_id, 0, NULL);
    ssize_t surf_at = fake_compositor_nth_request(&fc, FC_K_ACTIVATION_TOKEN,
                                                  token_set_surface, 0, NULL);
    ssize_t commit_at = fake_compositor_nth_request(&fc, FC_K_ACTIVATION_TOKEN,
                                                    token_commit, 0, NULL);
    pt_check(serial_at >= 0 && app_at > serial_at && surf_at > app_at &&
             commit_at > surf_at);

    /* done(token) delivers the configured 32-character token exactly once. */
    static const char wire_token[] = "0123456789abcdef0123456789abcdef"; /* 32 */
    pt_check(strlen(wire_token) == 32);
    pt_check(fake_compositor_activation_token_done(&fc, wire_token));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(log.done_calls == 1);
    pt_check(strcmp(log.token, wire_token) == 0);

    /* activate(token, surface) records the token string and surface object. */
    fake_compositor_clear(&fc);
    xdg_activation_v1_activate(activation, wire_token, window->platform_window->surface);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_activate) == 1);
    pt_check(fake_compositor_last_request(&fc, FC_K_ACTIVATION, activation_activate,
                                          &request));
    pt_check(request.nargs == 2);
    pt_check(request.args[0].has_text && strcmp(request.args[0].text, wire_token) == 0);
    pt_check(request.args[1].u == surface);

    /* The token object destroys cleanly. */
    xdg_activation_token_v1_destroy(token);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION_TOKEN, token_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION_TOKEN) == 0);

    /* Empty token path: compositor reports "" and the client sees it. */
    struct xdg_activation_token_v1 *empty = xdg_activation_v1_get_activation_token(activation);
    pt_check(empty != NULL);
    wire_token_log empty_log;
    memset(&empty_log, 0, sizeof(empty_log));
    pt_check(xdg_activation_token_v1_add_listener(empty, &wire_token_listener, &empty_log) == 0);
    xdg_activation_token_v1_commit(empty);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_activation_token_done(&fc, ""));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(empty_log.done_calls == 1);
    pt_check(empty_log.token[0] == '\0');
    xdg_activation_token_v1_destroy(empty);
    pt_check(fake_compositor_sync(&fc, app));

    /* No protocol violation was recorded across the whole flow. */
    pt_check(fake_compositor_error(&fc) == NULL);

    xdg_activation_v1_destroy(activation);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_ACTIVATION,
                                            activation_destroy) >= 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_ACTIVATION) == 0);

    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- foreign wire (supported path) ---------- */

typedef struct wire_foreign {
    struct zxdg_exporter_v2 *exporter;
    struct zxdg_importer_v2 *importer;
} wire_foreign;

static bool wire_bind_foreign(fake_compositor *fc, pt_app *app, wire_foreign *out) {
    memset(out, 0, sizeof(*out));
    wire_globals globals;
    int seen = wire_scan_globals(fc, app, &globals);
    if (seen < 2 || !globals.exporter_name || !globals.importer_name) {
        return false;
    }
    struct wl_registry *registry = wl_display_get_registry(app->platform->display);
    if (!registry) {
        return false;
    }
    out->exporter = wl_registry_bind(registry, globals.exporter_name,
                                     &zxdg_exporter_v2_interface, 1);
    out->importer = wl_registry_bind(registry, globals.importer_name,
                                     &zxdg_importer_v2_interface, 1);
    wl_registry_destroy(registry);
    pt_check(fake_compositor_sync(fc, app));
    return out->exporter && out->importer;
}

static void test_foreign_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.foreign");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));

    pt_window *window = wire_open_window(&fc, app);
    pt_window *child_window = wire_open_window(&fc, app);
    if (!window || !child_window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    uint32_t export_surface =
        wl_proxy_get_id((struct wl_proxy *)window->platform_window->surface);
    uint32_t child_surface =
        wl_proxy_get_id((struct wl_proxy *)child_window->platform_window->surface);
    pt_check(export_surface != 0 && child_surface != 0);

    wire_foreign foreign;
    pt_check(wire_bind_foreign(&fc, app, &foreign));
    if (!foreign.exporter || !foreign.importer) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_global_bound(&fc, FC_K_EXPORTER));
    pt_check(fake_compositor_global_bound(&fc, FC_K_IMPORTER));

    /* export_toplevel(surface) creates an exported object. */
    fake_compositor_clear(&fc);
    struct zxdg_exported_v2 *exported =
        zxdg_exporter_v2_export_toplevel(foreign.exporter, window->platform_window->surface);
    pt_check(exported != NULL);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTER,
                                            exporter_export_toplevel) == 1);
    fc_request request;
    pt_check(fake_compositor_last_request(&fc, FC_K_EXPORTER, exporter_export_toplevel,
                                          &request));
    pt_check(request.nargs == 2);
    pt_check(request.args[1].u == export_surface);
    uint32_t exported_id = fake_compositor_live_id(&fc, FC_K_EXPORTED);
    pt_check(exported_id != 0 && exported_id == request.new_id);

    wire_exported_log exported_log;
    memset(&exported_log, 0, sizeof(exported_log));
    pt_check(zxdg_exported_v2_add_listener(exported, &wire_exported_listener,
                                           &exported_log) == 0);

    /* handle(string) delivers the configured 32-character opaque handle. */
    static const char wire_handle[] = "fedcba9876543210fedcba9876543210"; /* 32 */
    pt_check(strlen(wire_handle) == 32);
    pt_check(fake_compositor_exported_handle(&fc, wire_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(exported_log.handle_calls == 1);
    pt_check(strcmp(exported_log.handle, wire_handle) == 0);

    /* import_toplevel(handle) creates an imported object; decode the string. */
    fake_compositor_clear(&fc);
    struct zxdg_imported_v2 *imported =
        zxdg_importer_v2_import_toplevel(foreign.importer, wire_handle);
    pt_check(imported != NULL);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER,
                                            importer_import_toplevel) == 1);
    pt_check(fake_compositor_last_request(&fc, FC_K_IMPORTER, importer_import_toplevel,
                                          &request));
    pt_check(request.nargs == 2 && request.args[1].has_text);
    pt_check(strcmp(request.args[1].text, wire_handle) == 0);
    uint32_t imported_id = fake_compositor_live_id(&fc, FC_K_IMPORTED);
    pt_check(imported_id != 0 && imported_id == request.new_id);

    wire_imported_log imported_log;
    memset(&imported_log, 0, sizeof(imported_log));
    pt_check(zxdg_imported_v2_add_listener(imported, &wire_imported_listener,
                                           &imported_log) == 0);

    /* set_parent_of(surface) records the exact child surface object. */
    fake_compositor_clear(&fc);
    zxdg_imported_v2_set_parent_of(imported, child_window->platform_window->surface);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED,
                                            imported_set_parent_of) == 1);
    pt_check(fake_compositor_last_request(&fc, FC_K_IMPORTED, imported_set_parent_of,
                                          &request));
    pt_check(request.nargs == 1 && request.args[0].u == child_surface);

    /* Invalid-handle path: the compositor sends destroyed; the client must
     * still destroy the object afterwards and the server then forgets it. */
    pt_check(fake_compositor_imported_destroyed(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(imported_log.destroyed_calls == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) != 0); /* client still owns it */
    zxdg_imported_v2_destroy(imported);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) == 0);

    /* Unexport destroys the exported object. */
    zxdg_exported_v2_destroy(exported);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_EXPORTED) == 0);

    pt_check(fake_compositor_error(&fc) == NULL);

    zxdg_exporter_v2_destroy(foreign.exporter);
    zxdg_importer_v2_destroy(foreign.importer);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTER, exporter_destroy) == 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_destroy) == 1);

    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* The event opcode constants must match the XML event lists. They are part of
 * the facility contract W3/W4 inject through. */
static void test_event_opcodes(void) {
    pt_check(token_done == 0);
    pt_check(exported_handle == 0);
    pt_check(imported_destroyed == 0);
    /* Request opcode constants are the XML request order. */
    pt_check(activation_destroy == 0 && activation_get_token == 1 &&
             activation_activate == 2);
    pt_check(token_set_serial == 0 && token_set_app_id == 1 && token_set_surface == 2 &&
             token_commit == 3 && token_destroy == 4);
    pt_check(exporter_destroy == 0 && exporter_export_toplevel == 1);
    pt_check(exported_destroy == 0);
    pt_check(importer_destroy == 0 && importer_import_toplevel == 1);
    pt_check(imported_destroy == 0 && imported_set_parent_of == 1);
}

int main(void) {
    unsetenv("PT_HEADLESS");
    test_option_bits_and_all();
    test_event_opcodes();
    test_no_bit_no_global();
    test_activation_bit_only();
    test_foreign_bit_only();
    test_activation_wire();
    test_foreign_wire();
    return pt_test_finish("protocol_wire");
}
