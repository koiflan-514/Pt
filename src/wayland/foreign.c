/*
 * xdg-foreign-unstable-v2: exported toplevel handles and imported parents.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The protocol is best effort: export delivers an opaque handle asynchronously
 * and import sets up a parent/child relationship with the same stacking and
 * positioning semantics as xdg_toplevel.set_parent. Every failure is a false
 * return or a silent invalidation, never a crash or a protocol error. All
 * objects are gated on the optional exporter/importer globals and on a headless
 * platform, so an unsupported compositor sees no behavior change.
 */

#include "wayland_internal.h"

/* One exported object per window. The listener data is the window record, whose
 * fields hold the callback, so the handle path and the teardown hook can clear
 * everything in one place. */
static void exported_handle(void *data, struct zxdg_exported_v2 *exported,
                            const char *handle);

static const struct zxdg_exported_v2_listener exported_listener = {
    .handle = exported_handle,
};

/* One imported object per window. The destroyed event is a silent
 * invalidation: the object is destroyed and the field cleared; no callback is
 * exposed for it. */
static void imported_destroyed(void *data, struct zxdg_imported_v2 *imported);

static const struct zxdg_imported_v2_listener imported_listener = {
    .destroyed = imported_destroyed,
};

/* Destroys a pending exported object and drops its callback. Idempotent and
 * safe on a half-cleared record; a NULL object is ignored. */
static void export_release(pt_wayland_window *wl) {
    if (!wl) {
        return;
    }
    if (wl->foreign_exported) {
        zxdg_exported_v2_destroy(wl->foreign_exported);
        wl->foreign_exported = NULL;
    }
    wl->foreign_export_callback = NULL;
    wl->foreign_export_data = NULL;
}

/* Destroys a pending imported object and clears the field. Idempotent. */
static void import_release(pt_wayland_window *wl) {
    if (!wl) {
        return;
    }
    if (wl->foreign_parent) {
        zxdg_imported_v2_destroy(wl->foreign_parent);
        wl->foreign_parent = NULL;
    }
}

static void exported_handle(void *data, struct zxdg_exported_v2 *exported,
                            const char *handle) {
    pt_wayland_window *wl = data;
    if (!wl || wl->foreign_exported != exported) {
        return;
    }
    /* The export stays alive so the handle keeps its validity; the callback
     * string is opaque and borrowed only for the call, never parsed or kept. */
    if (wl->foreign_export_callback) {
        wl->foreign_export_callback(wl->window, handle ? handle : "",
                                    wl->foreign_export_data);
    }
}

static void imported_destroyed(void *data, struct zxdg_imported_v2 *imported) {
    pt_wayland_window *wl = data;
    if (!wl || wl->foreign_parent != imported) {
        return;
    }
    /* The compositor revoked the import (invalid handle or a destroyed export).
     * Drop the object and clear the field; a later set_parent creates a fresh
     * import. */
    import_release(wl);
}

bool pt_platform_foreign_export(pt_window *window, pt_foreign_handle_callback callback,
                                void *user_data) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || platform->headless || !platform->foreign_exporter ||
        !wl->surface) {
        return false;
    }
    /* Re-exporting destroys the previous object first, which invalidates its
     * handle and drops the pending callback before the new object exists. */
    export_release(wl);
    struct zxdg_exported_v2 *exported =
        zxdg_exporter_v2_export_toplevel(platform->foreign_exporter, wl->surface);
    if (!exported) {
        return false;
    }
    wl->foreign_exported = exported;
    wl->foreign_export_callback = callback;
    wl->foreign_export_data = user_data;
    zxdg_exported_v2_add_listener(exported, &exported_listener, wl);
    return true;
}

void pt_platform_foreign_unexport(pt_window *window) {
    export_release(pt_wl_window_for(window));
}

bool pt_platform_foreign_set_parent(pt_window *window, const char *handle) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    pt_platform *platform = pt_platform_for(window);
    if (!wl || !platform || platform->headless || !platform->foreign_importer ||
        !wl->surface || !handle || !*handle) {
        return false;
    }
    /* A window keeps at most one imported parent; a new set_parent replaces the
     * previous imported object. */
    import_release(wl);
    struct zxdg_imported_v2 *imported =
        zxdg_importer_v2_import_toplevel(platform->foreign_importer, handle);
    if (!imported) {
        return false;
    }
    wl->foreign_parent = imported;
    zxdg_imported_v2_add_listener(imported, &imported_listener, wl);
    /* Make this window's surface the child of the imported surface immediately. */
    zxdg_imported_v2_set_parent_of(imported, wl->surface);
    return true;
}

void pt_platform_foreign_clear_parent(pt_window *window) {
    import_release(pt_wl_window_for(window));
}

void pt_foreign_window_destroy(pt_window *window) {
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!wl) {
        return;
    }
    export_release(wl);
    import_release(wl);
}

void pt_foreign_platform_destroy(pt_platform *platform) {
    if (!platform) {
        return;
    }
    if (platform->foreign_exporter) {
        zxdg_exporter_v2_destroy(platform->foreign_exporter);
        platform->foreign_exporter = NULL;
    }
    if (platform->foreign_importer) {
        zxdg_importer_v2_destroy(platform->foreign_importer);
        platform->foreign_importer = NULL;
    }
}
