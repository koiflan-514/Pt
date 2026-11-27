/*
 * xdg-foreign-unstable-v2 core: exported handles, imported parents, the
 * absent-global/headless fallback and object teardown.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Registry binding belongs to W5 (src/wayland/platform.c). Until that wave
 * lands, each wire test binds zxdg_exporter_v2 and zxdg_importer_v2 itself on a
 * private registry and installs the proxies in pt_platform, which is exactly
 * the state W5 will produce. Everything below then runs the native Pt path
 * against the header-only fake compositor: the public API, src/foreign.c, the
 * src/wayland/foreign.c hooks and the generated libwayland client code.
 */

#include "../src/internal.h"
#include "fake_compositor.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

/* ---------- opcode constants (from the vendored XML) ---------- */

#define exporter_destroy 0u
#define exporter_export_toplevel 1u

#define exported_destroy 0u
#define exported_handle 0u

#define importer_destroy 0u
#define importer_import_toplevel 1u

#define imported_destroy 0u
#define imported_set_parent_of 1u
#define imported_destroyed 0u

/* A representative opaque handle: 32 characters, never parsed or logged. */
#define wire_handle "fedcba9876543210fedcba9876543210"

/* ---------- callback logs ---------- */

typedef struct handle_log {
    int calls;
    pt_window *window;
    char handle[64];
} handle_log;

static void log_handle(pt_window *window, const char *handle, void *user_data) {
    handle_log *log = user_data;
    ++log->calls;
    log->window = window;
    snprintf(log->handle, sizeof(log->handle), "%s", handle ? handle : "(null)");
}

/* ---------- private registry bind (W5 placeholder) ---------- */

typedef struct foreign_globals {
    uint32_t exporter_name;
    uint32_t exporter_version;
    uint32_t importer_name;
    uint32_t importer_version;
} foreign_globals;

static void bind_global(void *data, struct wl_registry *registry, uint32_t name,
                        const char *interface, uint32_t version) {
    (void)registry;
    foreign_globals *globals = data;
    if (strcmp(interface, "zxdg_exporter_v2") == 0) {
        globals->exporter_name = name;
        globals->exporter_version = version;
    } else if (strcmp(interface, "zxdg_importer_v2") == 0) {
        globals->importer_name = name;
        globals->importer_version = version;
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

/* Creates a second registry on the live display, discovers the two foreign
 * globals and binds version 1 into the platform record. W5 will do the same in
 * registry_global; this keeps W4 independent of platform.c until then. */
static bool bind_foreign(pt_platform *platform) {
    if (!platform || !platform->display) {
        return false;
    }
    foreign_globals globals = {0};
    struct wl_registry *registry = wl_display_get_registry(platform->display);
    if (!registry) {
        return false;
    }
    wl_registry_add_listener(registry, &bind_listener, &globals);
    if (wl_display_roundtrip(platform->display) < 0) {
        wl_registry_destroy(registry);
        return false;
    }
    bool ok = true;
    if (globals.exporter_name) {
        platform->foreign_exporter =
            wl_registry_bind(registry, globals.exporter_name, &zxdg_exporter_v2_interface, 1);
        ok = platform->foreign_exporter != NULL;
    }
    if (globals.importer_name) {
        platform->foreign_importer =
            wl_registry_bind(registry, globals.importer_name, &zxdg_importer_v2_interface, 1);
        ok = ok && platform->foreign_importer != NULL;
    }
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

/* ---------- headless ---------- */

static void test_headless_fallback(void) {
    setenv("PT_HEADLESS", "1", 1);
    handle_log log = {0};

    pt_check(!pt_window_export_foreign_handle(NULL, log_handle, &log));
    pt_check(!pt_window_set_foreign_parent(NULL, "opaque-handle"));
    pt_check(!pt_window_set_foreign_parent(NULL, ""));
    pt_check(!pt_window_set_foreign_parent(NULL, NULL));

    pt_app *app = pt_app_create("pt.tests.foreign.headless");
    pt_check(app != NULL);
    pt_platform *platform = app ? app->platform : NULL;
    pt_check(platform != NULL);
    if (platform) {
        pt_check(platform->headless);
        pt_check(platform->foreign_exporter == NULL);
        pt_check(platform->foreign_importer == NULL);
    }
    pt_window *window = app ? pt_window_create(app, "foreign headless", 320, 200) : NULL;
    pt_check(window != NULL);
    if (window) {
        pt_check(!pt_window_export_foreign_handle(window, log_handle, &log));
        pt_check(!pt_window_export_foreign_handle(window, NULL, NULL));
        pt_check(!pt_window_set_foreign_parent(window, "opaque-handle"));
        pt_check(!pt_window_set_foreign_parent(window, ""));
        pt_check(!pt_window_set_foreign_parent(window, NULL));
    }
    pt_check(log.calls == 0);

    /* The teardown hooks tolerate a NULL record and an absent global. */
    pt_foreign_window_destroy(NULL);
    pt_foreign_platform_destroy(NULL);
    if (platform) {
        pt_foreign_platform_destroy(platform);
        pt_check(platform->foreign_exporter == NULL);
        pt_check(platform->foreign_importer == NULL);
    }
    if (window) {
        pt_foreign_window_destroy(window);
        pt_window_free(window);
    }
    if (app) {
        pt_app_destroy(app);
    }
    unsetenv("PT_HEADLESS");
}

/* ---------- absent globals ---------- */

static void test_no_global_fallback(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL)); /* foreign stays absent */
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    handle_log log = {0};

    pt_app *app = pt_app_create("pt.tests.foreign.noglobal");
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
    pt_check(platform->foreign_exporter == NULL);
    pt_check(platform->foreign_importer == NULL);
    pt_check(!fake_compositor_global_bound(&fc, FC_K_EXPORTER));
    pt_check(!fake_compositor_global_bound(&fc, FC_K_IMPORTER));

    pt_window *window = open_window(&fc, app, "foreign no global");
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    fake_compositor_clear(&fc);
    pt_check(!pt_window_export_foreign_handle(window, log_handle, &log));
    pt_check(!pt_window_export_foreign_handle(window, NULL, NULL));
    pt_check(!pt_window_set_foreign_parent(window, wire_handle));
    pt_check(!pt_window_set_foreign_parent(window, ""));
    pt_check(!pt_window_set_foreign_parent(window, NULL));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(log.calls == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTER, exporter_export_toplevel) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_import_toplevel) == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_EXPORTER) == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTER) == 0);

    /* Injecting into a nonexistent object is a no-op, not a crash. */
    pt_check(!fake_compositor_exported_handle(&fc, wire_handle));
    pt_check(!fake_compositor_imported_destroyed(&fc));

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_foreign_window_destroy(window);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- export / unexport ---------- */

static void test_export_unexport(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.export");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_global_bound(&fc, FC_K_EXPORTER));
    pt_check(fake_compositor_global_bound(&fc, FC_K_IMPORTER));

    pt_window *window = open_window(&fc, app, "foreign export");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_check(wl != NULL && wl->surface != NULL);
    uint32_t surface = proxy_id(wl->surface);
    pt_check(surface != 0);

    handle_log log = {0};
    fake_compositor_clear(&fc);
    pt_check(pt_window_export_foreign_handle(window, log_handle, &log));
    pt_check(fake_compositor_sync(&fc, app));

    /* export_toplevel(surface) creates the exported object. */
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTER, exporter_export_toplevel) == 1);
    fc_request request = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_EXPORTER, exporter_export_toplevel,
                                          &request));
    pt_check(request.nargs == 2);
    pt_check(request.args[1].u == surface);
    uint32_t exported_id = fake_compositor_live_id(&fc, FC_K_EXPORTED);
    pt_check(exported_id != 0 && exported_id == request.new_id);
    pt_check(wl->foreign_exported != NULL);
    pt_check(log.calls == 0);

    /* The injected handle delivers the exact 32-character string once and the
     * export object stays alive (the handle stays valid). */
    pt_check(strlen(wire_handle) == 32);
    pt_check(fake_compositor_exported_handle(&fc, wire_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(log.calls == 1);
    pt_check(log.window == window);
    pt_check(strcmp(log.handle, wire_handle) == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_EXPORTED) != 0);

    /* Unexport destroys the exported object; a second unexport is a no-op and
     * no handle can be delivered afterwards. */
    pt_window_unexport_foreign_handle(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_EXPORTED) == 0);
    pt_check(wl->foreign_exported == NULL);
    pt_check(wl->foreign_export_callback == NULL);
    pt_check(wl->foreign_export_data == NULL);
    pt_window_unexport_foreign_handle(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 1);
    pt_check(!fake_compositor_exported_handle(&fc, wire_handle));

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_foreign_window_destroy(window);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- re-export invalidates the old object ---------- */

static void test_reexport(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.reexport");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window *window = open_window(&fc, app, "foreign reexport");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }

    static const char handle_a[] = "11111111111111111111111111111111";
    static const char handle_b[] = "22222222222222222222222222222222";
    pt_check(strlen(handle_a) == 32 && strlen(handle_b) == 32);

    handle_log first = {0};
    handle_log second = {0};
    fake_compositor_clear(&fc);
    pt_check(pt_window_export_foreign_handle(window, log_handle, &first));
    pt_check(fake_compositor_sync(&fc, app));
    ssize_t i_first_export =
        fake_compositor_nth_request(&fc, FC_K_EXPORTER, exporter_export_toplevel, 0, NULL);
    pt_check(i_first_export >= 0);

    /* The first handle reaches the first callback. */
    pt_check(fake_compositor_exported_handle(&fc, handle_a));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(first.calls == 1);
    pt_check(strcmp(first.handle, handle_a) == 0);
    pt_check(second.calls == 0);

    /* A second export destroys the old object first, then creates the new one. */
    fake_compositor_clear(&fc);
    pt_check(pt_window_export_foreign_handle(window, log_handle, &second));
    pt_check(fake_compositor_sync(&fc, app));
    ssize_t i_destroy = fake_compositor_nth_request(&fc, FC_K_EXPORTED, exported_destroy, 0, NULL);
    ssize_t i_second_export =
        fake_compositor_nth_request(&fc, FC_K_EXPORTER, exporter_export_toplevel, 0, NULL);
    pt_check(i_destroy >= 0);
    pt_check(i_second_export >= 0);
    pt_check(i_destroy < i_second_export);
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 1);

    /* The old callback is dropped: only the new object delivers, and it uses
     * the new callback. */
    pt_check(fake_compositor_exported_handle(&fc, handle_b));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(first.calls == 1);
    pt_check(second.calls == 1);
    pt_check(strcmp(second.handle, handle_b) == 0);

    pt_window_unexport_foreign_handle(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 2);
    pt_check(fake_compositor_error(&fc) == NULL);

    pt_foreign_window_destroy(window);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- import / clear ---------- */

static void test_import_clear(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.import");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window *window = open_window(&fc, app, "foreign import");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_check(wl != NULL && wl->surface != NULL);
    uint32_t surface = proxy_id(wl->surface);
    pt_check(surface != 0);

    fake_compositor_clear(&fc);
    pt_check(pt_window_set_foreign_parent(window, wire_handle));
    pt_check(fake_compositor_sync(&fc, app));

    /* import_toplevel(handle) creates the imported object. */
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_import_toplevel) == 1);
    fc_request request = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_IMPORTER, importer_import_toplevel,
                                          &request));
    pt_check(request.nargs == 2 && request.args[1].has_text);
    pt_check(strcmp(request.args[1].text, wire_handle) == 0);
    uint32_t imported_id = fake_compositor_live_id(&fc, FC_K_IMPORTED);
    pt_check(imported_id != 0 && imported_id == request.new_id);
    pt_check(wl->foreign_parent != NULL);

    /* set_parent_of(this window surface) applies the relationship. */
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_set_parent_of) == 1);
    pt_check(fake_compositor_last_request(&fc, FC_K_IMPORTED, imported_set_parent_of, &request));
    pt_check(request.nargs == 1 && request.args[0].u == surface);

    /* Clear destroys the imported object; a second clear is a no-op. */
    pt_window_clear_foreign_parent(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) == 0);
    pt_check(wl->foreign_parent == NULL);
    pt_window_clear_foreign_parent(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);

    /* A cleared window can import again. */
    pt_check(pt_window_set_foreign_parent(window, wire_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_import_toplevel) == 2);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) != 0);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_foreign_window_destroy(window);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- replace an existing imported parent ---------- */

static void test_replace_import(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.replace");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window *window = open_window(&fc, app, "foreign replace");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }

    static const char handle_a[] = "33333333333333333333333333333333";
    static const char handle_b[] = "44444444444444444444444444444444";
    pt_check(strlen(handle_a) == 32 && strlen(handle_b) == 32);

    fake_compositor_clear(&fc);
    pt_check(pt_window_set_foreign_parent(window, handle_a));
    pt_check(fake_compositor_sync(&fc, app));
    uint32_t first_imported = fake_compositor_live_id(&fc, FC_K_IMPORTED);
    pt_check(first_imported != 0);

    /* A second set_parent destroys the old imported object before the new
     * import; only the newest object is live. */
    pt_check(pt_window_set_foreign_parent(window, handle_b));
    pt_check(fake_compositor_sync(&fc, app));
    ssize_t i_destroy = fake_compositor_nth_request(&fc, FC_K_IMPORTED, imported_destroy, 0, NULL);
    ssize_t i_second_import =
        fake_compositor_nth_request(&fc, FC_K_IMPORTER, importer_import_toplevel, 1, NULL);
    pt_check(i_destroy >= 0);
    pt_check(i_second_import >= 0);
    pt_check(i_destroy < i_second_import);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) != 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) != first_imported);

    /* The new import carried the second handle and applied set_parent_of again. */
    fc_request request = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_IMPORTER, importer_import_toplevel,
                                          &request));
    pt_check(request.nargs == 2 && request.args[1].has_text);
    pt_check(strcmp(request.args[1].text, handle_b) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_set_parent_of) == 2);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_foreign_window_destroy(window);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- invalid handle: destroyed invalidation and recovery ---------- */

static void test_invalid_handle_recovery(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.invalid");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window *window = open_window(&fc, app, "foreign invalid");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_check(wl != NULL);

    static const char bad_handle[] = "55555555555555555555555555555555";
    static const char good_handle[] = "66666666666666666666666666666666";
    pt_check(strlen(bad_handle) == 32 && strlen(good_handle) == 32);

    fake_compositor_clear(&fc);
    pt_check(pt_window_set_foreign_parent(window, bad_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(wl->foreign_parent != NULL);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) != 0);

    /* The compositor revokes the import; the client must clear its state,
     * destroy the object and not crash. */
    pt_check(fake_compositor_imported_destroyed(&fc));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_IMPORTED, imported_destroy, 1, 8));
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_IMPORTED, false, 8));
    pt_check(wl->foreign_parent == NULL);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);

    /* A later import with a fresh handle recovers cleanly. */
    pt_check(pt_window_set_foreign_parent(window, good_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_import_toplevel) == 2);
    pt_check(wl->foreign_parent != NULL);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTED) != 0);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_foreign_window_destroy(window);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- window and platform teardown ---------- */

static void test_window_destroy_lifecycle(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.destroy");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window *window = open_window(&fc, app, "foreign destroy");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_check(wl != NULL);

    handle_log log = {0};
    pt_check(pt_window_export_foreign_handle(window, log_handle, &log));
    pt_check(pt_window_set_foreign_parent(window, wire_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(wl->foreign_exported != NULL);
    pt_check(wl->foreign_parent != NULL);

    /* Window destruction destroys both objects and drops the pending handle
     * callback without calling it. */
    fake_compositor_clear(&fc);
    pt_foreign_window_destroy(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(log.calls == 0);
    pt_check(wl->foreign_exported == NULL);
    pt_check(wl->foreign_parent == NULL);
    pt_check(wl->foreign_export_callback == NULL);
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_EXPORTED, false, 8));
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_IMPORTED, false, 8));

    /* Unexport and clear after destruction are no-ops. */
    pt_window_unexport_foreign_handle(window);
    pt_window_clear_foreign_parent(window);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTED, exported_destroy) == 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTED, imported_destroy) == 1);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_window_free(window);
    pt_foreign_platform_destroy(platform);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

static void test_platform_destroy_lifecycle(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_FOREIGN));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n", fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");

    pt_app *app = pt_app_create("pt.tests.foreign.platform");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_platform *platform = app->platform;
    if (!platform || !bind_foreign(platform)) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window *window = open_window(&fc, app, "foreign platform");
    if (!window) {
        pt_foreign_platform_destroy(platform);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }

    /* Platform destruction destroys both globals. */
    fake_compositor_clear(&fc);
    pt_foreign_platform_destroy(platform);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(platform->foreign_exporter == NULL);
    pt_check(platform->foreign_importer == NULL);
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTER, exporter_destroy) == 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_destroy) == 1);
    pt_check(fake_compositor_live_id(&fc, FC_K_EXPORTER) == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_IMPORTER) == 0);

    /* With the globals gone the API falls back to false and sends nothing. */
    handle_log log = {0};
    pt_check(!pt_window_export_foreign_handle(window, log_handle, &log));
    pt_check(!pt_window_set_foreign_parent(window, wire_handle));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(log.calls == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_EXPORTER, exporter_export_toplevel) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_IMPORTER, importer_import_toplevel) == 0);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

int main(void) {
    unsetenv("PT_HEADLESS");
    test_headless_fallback();
    test_no_global_fallback();
    test_export_unexport();
    test_reexport();
    test_import_clear();
    test_replace_import();
    test_invalid_handle_recovery();
    test_window_destroy_lifecycle();
    test_platform_destroy_lifecycle();
    return pt_test_finish("foreign");
}
