/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_theme_h
#define pt_theme_h

#include <pt/base.h>

/* Header contract
 *
 * The color and metric enums are append-only ABI: existing numeric values never
 * change and new roles get fresh numbers. pt_theme_token_count and
 * pt_theme_metric_count are bounds, not roles.
 * Colors are 0xRRGGBBAA; metrics are logical pixels. Negative metrics clamp to
 * zero, and a radius_pill may be large because it is clamped to the shorter side
 * of each shape. Out-of-range token or metric indices are ignored by setters;
 * the getters return 0 for them.
 * The setters and getters live in app.h and window.h and are UI-thread only. A
 * window resolves app tokens first and then its own overrides at paint time; no
 * allocation is transferred by this header.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Semantic color roles. The first block is the 0.1 baseline; the 0.9
 * additions name the container, outline, status-container and surface
 * roles that controls use for hover, pressed, drag-over, error and busy
 * states. Applications should prefer a role over a literal color. */
typedef enum pt_theme_token {
    pt_theme_background = 0,
    pt_theme_surface,
    pt_theme_surface_raised,
    pt_theme_surface_hover,
    pt_theme_border,
    pt_theme_border_strong,
    pt_theme_text,
    pt_theme_text_muted,
    pt_theme_accent,
    pt_theme_accent_hover,
    pt_theme_accent_text,
    pt_theme_danger,
    pt_theme_success,
    pt_theme_warning,
    pt_theme_shadow,
    pt_theme_titlebar,
    pt_theme_scrollbar,
    pt_theme_focus,
    pt_theme_surface_sunken,
    pt_theme_control,
    pt_theme_control_hover,
    pt_theme_accent_soft,
    pt_theme_selection,
    pt_theme_track,

    /* 0.9 semantic roles */
    pt_theme_surface_container = 24,
    pt_theme_surface_container_high,
    pt_theme_outline,
    pt_theme_outline_variant,
    pt_theme_text_disabled,
    pt_theme_accent_pressed,
    pt_theme_control_pressed,
    pt_theme_danger_soft,
    pt_theme_success_soft,
    pt_theme_warning_soft,
    pt_theme_scrim,
    pt_theme_skeleton,
    pt_theme_skeleton_highlight,
    pt_theme_selection_text,
    pt_theme_token_count
} pt_theme_token;

/* Geometric tokens. Colors stay in the token table above; metrics cover the
 * radii, focus ring, elevation and control sizing that the built-in paints
 * share, so a window can restyle its surfaces without touching widgets. */
typedef enum pt_theme_metric {
    pt_theme_metric_radius_small = 0,
    pt_theme_metric_radius_medium,
    pt_theme_metric_radius_large,
    pt_theme_metric_radius_pill,
    pt_theme_metric_focus_width,
    pt_theme_metric_focus_offset,
    pt_theme_metric_elevation_low,
    pt_theme_metric_elevation_high,
    pt_theme_metric_chip_height,
    pt_theme_metric_control_height,
    pt_theme_metric_count
} pt_theme_metric;

#ifdef __cplusplus
}
#endif

#endif
