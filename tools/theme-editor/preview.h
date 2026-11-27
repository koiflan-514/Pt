/*
 * Pt theme editor - preview window interface.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_theme_editor_preview_h
#define pt_theme_editor_preview_h

#include <stdbool.h>

#include <pt/pt.h>

#include "theme_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Frozen interface for the preview module (M27 W0).
 *
 * A preview is one second top-level Pt window created by the module and owned
 * by the application. It shows a representative sample of real controls and is
 * restyled only through the public per-window override API: pt_window_set_theme_color() and
 * pt_window_set_theme_metric(). The editor's own window is never restyled by
 * this module. Only public headers are used; there is no private Pt state.
 *
 * Ownership: te_preview_create() returns an opaque handle owned by the caller.
 * The handle borrows app (which must outlive it) and retains no document
 * pointer; te_preview_apply() copies the values into the window overrides. Call
 * te_preview_destroy() before pt_app_destroy(); destroying NULL is a no-op.
 */

typedef struct te_preview te_preview;

/* Creates the preview window and, when initial is non-NULL, applies its
 * colours and metrics. Returns NULL on allocation or window-creation failure.
 * Must run on the Pt UI thread while app is live. */
te_preview *te_preview_create(pt_app *app, const te_theme_doc *initial);

/* Applies all 38 colours and 10 metrics of doc to the preview window only.
 * A NULL preview or doc is a no-op. The values are copied; doc may be freed
 * after the call. */
void te_preview_apply(te_preview *preview, const te_theme_doc *doc);

/* Clears every override, returning the preview to the application palette.
 * NULL is a no-op. */
void te_preview_reset(te_preview *preview);

/* Borrowed preview window, or NULL for a NULL handle or a failed create. The
 * pointer is valid until te_preview_destroy(). */
pt_window *te_preview_window(const te_preview *preview);

/* Destroys the preview handle and requests the window to close. The application
 * owns the window and releases it at pt_app_destroy(); NULL is a no-op. */
void te_preview_destroy(te_preview *preview);

#ifdef __cplusplus
}
#endif

#endif