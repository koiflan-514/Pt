/*
 * Pt theme editor - preview window (W0 scaffold).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is the W0 stub: one real Pt window with a small sample. W3b expands the
 * representative control set; the per-window override path is already the only
 * way this module restyles anything.
 */

#include "preview.h"

#include <stdlib.h>

struct te_preview {
    pt_window *window;
};

te_preview *te_preview_create(pt_app *app, const te_theme_doc *initial) {
    if (app == NULL) {
        return NULL;
    }
    te_preview *preview = calloc(1, sizeof(*preview));
    if (preview == NULL) {
        return NULL;
    }
    preview->window = pt_window_create(app, "Theme preview", 520, 400);
    if (preview->window == NULL) {
        free(preview);
        return NULL;
    }
    pt_widget *root = pt_window_root(preview->window);
    if (root != NULL) {
        pt_label_create(root, "Theme preview");
        pt_label_create(root, "Muted sample text");
        pt_button_create(root, "Primary");
        pt_button_create(root, "Secondary");
    }
    if (initial != NULL) {
        te_preview_apply(preview, initial);
    }
    return preview;
}

void te_preview_apply(te_preview *preview, const te_theme_doc *doc) {
    if (preview == NULL || preview->window == NULL || doc == NULL) {
        return;
    }
    for (int i = 0; i < pt_theme_token_count; ++i) {
        pt_window_set_theme_color(preview->window, (pt_theme_token)i, doc->colors[i]);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        pt_window_set_theme_metric(preview->window, (pt_theme_metric)i, doc->metrics[i]);
    }
}

void te_preview_reset(te_preview *preview) {
    if (preview == NULL || preview->window == NULL) {
        return;
    }
    pt_window_clear_theme_overrides(preview->window);
}

pt_window *te_preview_window(const te_preview *preview) {
    return preview != NULL ? preview->window : NULL;
}

void te_preview_destroy(te_preview *preview) {
    if (preview == NULL) {
        return;
    }
    if (preview->window != NULL) {
        /* The application owns every window; pt_window_close is the public
         * request and pt_app_destroy releases the surface. */
        pt_window_close(preview->window);
    }
    free(preview);
}