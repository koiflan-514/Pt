/*
 * Pt built-in color themes.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <string.h>

typedef struct pt_palette {
    pt_color colors[pt_theme_token_count];
} pt_palette;

static const pt_palette pt_dark_palette = {
    .colors = {
        [pt_theme_background] = 0x0B0E14FFu,
        [pt_theme_surface] = 0x11151EFFu,
        [pt_theme_surface_raised] = 0x161B26FFu,
        [pt_theme_surface_hover] = 0x1D2431FFu,
        [pt_theme_border] = 0x232B3AFFu,
        [pt_theme_border_strong] = 0x39445AFFu,
        [pt_theme_text] = 0xE9EEF7FFu,
        [pt_theme_text_muted] = 0x94A1B8FFu,
        [pt_theme_accent] = 0x3568E0FFu,
        [pt_theme_accent_hover] = 0x3D6FE8FFu,
        [pt_theme_accent_text] = 0xFFFFFFFFu,
        [pt_theme_danger] = 0xF16B75FFu,
        [pt_theme_success] = 0x4ACB8DFFu,
        [pt_theme_warning] = 0xE5B454FFu,
        [pt_theme_shadow] = 0x00000073u,
        [pt_theme_titlebar] = 0x0E121BFFu,
        [pt_theme_scrollbar] = 0x4A566EFFu,
        [pt_theme_focus] = 0x8CB0FFFFu,
        [pt_theme_surface_sunken] = 0x080B11FFu,
        [pt_theme_control] = 0x1A2130FFu,
        [pt_theme_control_hover] = 0x222B3CFFu,
        [pt_theme_accent_soft] = 0x3568E033u,
        [pt_theme_selection] = 0x3568E059u,
        [pt_theme_track] = 0x2A3344FFu,
        [pt_theme_surface_container] = 0x141924FFu,
        [pt_theme_surface_container_high] = 0x1A2130FFu,
        [pt_theme_outline] = 0x39445AFFu,
        [pt_theme_outline_variant] = 0x232B3AFFu,
        [pt_theme_text_disabled] = 0x5B6577FFu,
        [pt_theme_accent_pressed] = 0x2F5BC0FFu,
        [pt_theme_control_pressed] = 0x2A3447FFu,
        [pt_theme_danger_soft] = 0xF16B7526u,
        [pt_theme_success_soft] = 0x4ACB8D26u,
        [pt_theme_warning_soft] = 0xE5B45426u,
        [pt_theme_scrim] = 0x000000A6u,
        [pt_theme_skeleton] = 0x1D2431FFu,
        [pt_theme_skeleton_highlight] = 0x2A3447FFu,
        [pt_theme_selection_text] = 0xFFFFFFFFu,
    },
};

static const pt_palette pt_light_palette = {
    .colors = {
        [pt_theme_background] = 0xF4F6FAFFu,
        [pt_theme_surface] = 0xFFFFFFFFu,
        [pt_theme_surface_raised] = 0xFFFFFFFFu,
        [pt_theme_surface_hover] = 0xF0F3F8FFu,
        [pt_theme_border] = 0xE1E6EEFFu,
        [pt_theme_border_strong] = 0xC8D0DDFF,
        [pt_theme_text] = 0x111827FFu,
        [pt_theme_text_muted] = 0x5F6B7FFFu,
        [pt_theme_accent] = 0x2559E6FFu,
        [pt_theme_accent_hover] = 0x1E56E8FFu,
        [pt_theme_accent_text] = 0xFFFFFFFFu,
        [pt_theme_danger] = 0xB92C38FFu,
        [pt_theme_success] = 0x0F7A37FFu,
        [pt_theme_warning] = 0x8A5A00FFu,
        [pt_theme_shadow] = 0x0F172A24u,
        [pt_theme_titlebar] = 0xFBFCFEFFu,
        [pt_theme_scrollbar] = 0xADB8C8FFu,
        [pt_theme_focus] = 0x2559E6FFu,
        [pt_theme_surface_sunken] = 0xEDF0F5FFu,
        [pt_theme_control] = 0xF8FAFCFFu,
        [pt_theme_control_hover] = 0xEFF3F9FFu,
        [pt_theme_accent_soft] = 0x2559E626u,
        [pt_theme_selection] = 0x2559E640u,
        [pt_theme_track] = 0xDCE3EDFFu,
        [pt_theme_surface_container] = 0xEDF0F5FFu,
        [pt_theme_surface_container_high] = 0xE5EAF2FFu,
        [pt_theme_outline] = 0xC8D0DDFFu,
        [pt_theme_outline_variant] = 0xE1E6EEFFu,
        [pt_theme_text_disabled] = 0xA0A9B8FFu,
        [pt_theme_accent_pressed] = 0x1C4ECFFFu,
        [pt_theme_control_pressed] = 0xE4EAF3FFu,
        [pt_theme_danger_soft] = 0xB92C3820u,
        [pt_theme_success_soft] = 0x0F7A3720u,
        [pt_theme_warning_soft] = 0x8A5A0020u,
        [pt_theme_scrim] = 0x0F172A66u,
        [pt_theme_skeleton] = 0xE7EBF1FFu,
        [pt_theme_skeleton_highlight] = 0xF3F5F9FFu,
        [pt_theme_selection_text] = 0x111827FFu,
    },
};

static const float pt_default_metrics[pt_theme_metric_count] = {
    [pt_theme_metric_radius_small] = 6.0f,
    [pt_theme_metric_radius_medium] = 10.0f,
    [pt_theme_metric_radius_large] = 16.0f,
    [pt_theme_metric_radius_pill] = 999.0f,
    [pt_theme_metric_focus_width] = 2.0f,
    [pt_theme_metric_focus_offset] = 2.0f,
    [pt_theme_metric_elevation_low] = 8.0f,
    [pt_theme_metric_elevation_high] = 20.0f,
    [pt_theme_metric_chip_height] = 28.0f,
    [pt_theme_metric_control_height] = 34.0f,
};

void pt_theme_set_defaults(pt_theme_data *theme, pt_color_scheme scheme) {
    const pt_palette *palette =
        scheme == pt_color_scheme_light ? &pt_light_palette : &pt_dark_palette;
    memcpy(theme->colors, palette->colors, sizeof(theme->colors));
    memcpy(theme->metrics, pt_default_metrics, sizeof(theme->metrics));
    theme->scheme = scheme;
}

void pt_theme_init(pt_theme_data *theme, pt_color_scheme scheme) {
    memset(theme, 0, sizeof(*theme));
    pt_theme_set_defaults(theme, scheme);
    memcpy(theme->font_family, "sans-serif", sizeof("sans-serif"));
}

pt_color pt_theme_contrast_text(pt_color background) {
    float r = (float)((background >> 24) & 0xFFu) / 255.0f;
    float g = (float)((background >> 16) & 0xFFu) / 255.0f;
    float b = (float)((background >> 8) & 0xFFu) / 255.0f;
    float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return luminance > 0.62f ? 0x101418FFu : 0xFFFFFFFFu;
}

pt_color pt_theme_color(const pt_window *window, pt_theme_token token) {
    if (!window || token < 0 || token >= pt_theme_token_count) {
        return 0xFFFFFFFFu;
    }
    if (window->theme_color_set[token]) {
        return window->theme_colors[token];
    }
    return window->app ? window->app->theme.colors[token] : 0xFFFFFFFFu;
}

float pt_theme_metric_value(const pt_window *window, pt_theme_metric metric) {
    if (!window || metric < 0 || metric >= pt_theme_metric_count) {
        return 0.0f;
    }
    float value = window->theme_metric_set[metric]
                      ? window->theme_metrics[metric]
                      : (window->app ? window->app->theme.metrics[metric]
                                     : pt_default_metrics[metric]);
    return pt_max_float(0.0f, value);
}

pt_color pt_widget_theme_color(const pt_widget *widget, pt_theme_token token) {
    return pt_theme_color(pt_widget_window(widget), token);
}

float pt_widget_theme_metric_value(const pt_widget *widget, pt_theme_metric metric) {
    pt_window *window = pt_widget_window(widget);
    if (window) {
        return pt_theme_metric_value(window, metric);
    }
    if (metric < 0 || metric >= pt_theme_metric_count) {
        return 0.0f;
    }
    return pt_max_float(0.0f, pt_default_metrics[metric]);
}
