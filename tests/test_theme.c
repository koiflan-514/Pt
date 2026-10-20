/*
 * Semantic theme tokens, metrics and window-level overrides.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>

static double srgb_channel(uint32_t component) {
    double value = (double)component / 255.0;
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

static double relative_luminance(pt_color color) {
    double r = srgb_channel((color >> 24) & 0xFFu);
    double g = srgb_channel((color >> 16) & 0xFFu);
    double b = srgb_channel((color >> 8) & 0xFFu);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static double contrast_ratio(pt_color first, pt_color second) {
    double a = relative_luminance(first);
    double b = relative_luminance(second);
    if (a < b) {
        double swap = a;
        a = b;
        b = swap;
    }
    return (a + 0.05) / (b + 0.05);
}

/* Straight-alpha composite of fg over an opaque bg, matching the renderer's
 * 0xRRGGBBAA convention. */
static pt_color composite_over(pt_color fg, pt_color bg) {
    uint32_t alpha = fg & 0xFFu;
    uint32_t inverse = 255u - alpha;
    uint32_t red = (((fg >> 24) & 0xFFu) * alpha + ((bg >> 24) & 0xFFu) * inverse + 127u) /
                   255u;
    uint32_t green = (((fg >> 16) & 0xFFu) * alpha + ((bg >> 16) & 0xFFu) * inverse + 127u) /
                     255u;
    uint32_t blue = (((fg >> 8) & 0xFFu) * alpha + ((bg >> 8) & 0xFFu) * inverse + 127u) /
                    255u;
    return (red << 24) | (green << 16) | (blue << 8) | 0xFFu;
}

/* The theming guide quotes these floors, so they are asserted rather than
 * described: body text clears WCAG AAA, muted text and the status roles clear
 * AA for normal text, and the accent pair clears AA. */
static void test_contrast(pt_app *app, pt_color_scheme scheme) {
    pt_app_set_color_scheme(app, scheme);
    pt_color background = pt_app_get_theme_color(app, pt_theme_background);
    pt_color surface = pt_app_get_theme_color(app, pt_theme_surface);
    pt_color surface_raised = pt_app_get_theme_color(app, pt_theme_surface_raised);
    pt_color text = pt_app_get_theme_color(app, pt_theme_text);
    pt_color muted = pt_app_get_theme_color(app, pt_theme_text_muted);
    pt_color accent = pt_app_get_theme_color(app, pt_theme_accent);
    pt_color accent_hover = pt_app_get_theme_color(app, pt_theme_accent_hover);
    pt_color accent_pressed = pt_app_get_theme_color(app, pt_theme_accent_pressed);
    pt_color accent_text = pt_app_get_theme_color(app, pt_theme_accent_text);
    pt_color danger = pt_app_get_theme_color(app, pt_theme_danger);
    pt_color success = pt_app_get_theme_color(app, pt_theme_success);
    pt_color warning = pt_app_get_theme_color(app, pt_theme_warning);
    pt_color focus = pt_app_get_theme_color(app, pt_theme_focus);
    pt_color selection = pt_app_get_theme_color(app, pt_theme_selection);
    pt_color selection_text = pt_app_get_theme_color(app, pt_theme_selection_text);

    pt_check(contrast_ratio(text, background) >= 7.0);
    pt_check(contrast_ratio(text, surface) >= 7.0);
    pt_check(contrast_ratio(text, surface_raised) >= 7.0);
    pt_check(contrast_ratio(muted, background) >= 4.5);
    pt_check(contrast_ratio(muted, surface) >= 4.5);
    pt_check(contrast_ratio(accent_text, accent) >= 4.5);
    pt_check(contrast_ratio(accent_text, accent_hover) >= 4.5);
    pt_check(contrast_ratio(accent_text, accent_pressed) >= 4.5);
    pt_check(contrast_ratio(danger, surface) >= 4.5);
    pt_check(contrast_ratio(success, surface) >= 4.5);
    pt_check(contrast_ratio(warning, surface) >= 4.5);
    pt_check(contrast_ratio(focus, background) >= 4.5);
    /* Custom widgets may fill a selection themselves and use the matching
     * foreground token; the built-in editors keep the normal text color. */
    pt_check(contrast_ratio(selection_text, composite_over(selection, surface)) >= 4.5);
    pt_check(contrast_ratio(selection_text, composite_over(selection, background)) >= 4.5);
}

static void test_tokens_and_metrics(pt_app *app) {
    pt_color dark_background = pt_app_get_theme_color(app, pt_theme_background);
    pt_color dark_surface = pt_app_get_theme_color(app, pt_theme_surface_container);
    pt_check(dark_background != 0);
    pt_check(dark_surface != 0);
    pt_check(dark_background != dark_surface);
    pt_check(pt_app_get_theme_color(app, pt_theme_skeleton) != 0);
    pt_check(pt_app_get_theme_color(app, pt_theme_outline_variant) != 0);

    pt_check_near(pt_app_get_theme_metric(app, pt_theme_metric_radius_small), 6.0, 0.01);
    pt_check_near(pt_app_get_theme_metric(app, pt_theme_metric_chip_height), 28.0, 0.01);
    pt_check_near(pt_app_get_theme_metric(app, pt_theme_metric_control_height), 34.0, 0.01);
    pt_check(pt_app_get_theme_metric(app, pt_theme_metric_elevation_low) > 0.0f);

    pt_app_set_color_scheme(app, pt_color_scheme_light);
    pt_color light_background = pt_app_get_theme_color(app, pt_theme_background);
    pt_check(light_background != dark_background);
    pt_check(light_background != 0);
    pt_app_set_color_scheme(app, pt_color_scheme_dark);

    pt_app_set_accent_color(app, 0x3366FFFFu);
    pt_check(pt_app_get_theme_color(app, pt_theme_accent) == 0x3366FFFFu);
    pt_check(pt_app_get_theme_color(app, pt_theme_accent_pressed) !=
             pt_app_get_theme_color(app, pt_theme_accent));
    pt_check(pt_app_get_theme_color(app, pt_theme_accent_text) == 0xFFFFFFFFu);

    pt_app_set_theme_metric(app, pt_theme_metric_radius_small, 9.0f);
    pt_check_near(pt_app_get_theme_metric(app, pt_theme_metric_radius_small), 9.0, 0.01);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_small, -4.0f);
    pt_check_near(pt_app_get_theme_metric(app, pt_theme_metric_radius_small), 0.0, 0.01);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_small, 6.0f);
}

static void test_window_overrides(pt_app *app) {
    pt_window *first = pt_window_create(app, "theme one", 240, 120);
    pt_window *second = pt_window_create(app, "theme two", 240, 120);
    pt_check(first != NULL && second != NULL);
    if (!first || !second) {
        return;
    }
    pt_widget *label = pt_label_create(pt_window_root(first), "token probe");

    pt_color app_background = pt_app_get_theme_color(app, pt_theme_background);
    pt_color custom_background = 0x112233FFu;
    pt_color app_accent = pt_app_get_theme_color(app, pt_theme_accent);
    pt_check(pt_window_get_theme_color(first, pt_theme_background) == app_background);

    pt_window_set_theme_color(first, pt_theme_background, custom_background);
    pt_check(pt_window_get_theme_color(first, pt_theme_background) == custom_background);
    pt_check(pt_theme_color(first, pt_theme_background) == custom_background);
    pt_check(pt_widget_theme_color(label, pt_theme_background) == custom_background);
    pt_check(pt_window_get_theme_color(second, pt_theme_background) == app_background);
    pt_check(pt_window_get_theme_color(first, pt_theme_accent) == app_accent);

    float app_radius = pt_app_get_theme_metric(app, pt_theme_metric_radius_large);
    pt_window_set_theme_metric(first, pt_theme_metric_radius_large, 20.0f);
    pt_check_near(pt_window_get_theme_metric(first, pt_theme_metric_radius_large), 20.0, 0.01);
    pt_check_near(pt_theme_metric_value(first, pt_theme_metric_radius_large), 20.0, 0.01);
    pt_check_near(pt_widget_theme_metric_value(label, pt_theme_metric_radius_large), 20.0,
                  0.01);
    pt_check_near(pt_window_get_theme_metric(second, pt_theme_metric_radius_large),
                  app_radius, 0.01);
    pt_window_set_theme_metric(first, pt_theme_metric_radius_large, -3.0f);
    pt_check_near(pt_window_get_theme_metric(first, pt_theme_metric_radius_large), 0.0, 0.01);

    pt_window_set_theme_color(first, (pt_theme_token)pt_theme_token_count, custom_background);
    pt_window_set_theme_metric(first, (pt_theme_metric)pt_theme_metric_count, 3.0f);
    pt_check(pt_window_get_theme_color(first, (pt_theme_token)pt_theme_token_count) == 0);
    pt_check_near(pt_window_get_theme_metric(first, (pt_theme_metric)pt_theme_metric_count), 0.0,
                  0.01);

    pt_window_clear_theme_overrides(first);
    pt_check(pt_window_get_theme_color(first, pt_theme_background) == app_background);
    pt_check_near(pt_window_get_theme_metric(first, pt_theme_metric_radius_large), app_radius,
                  0.01);

    pt_window_free(first);
    pt_window_free(second);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.theme");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("theme");
    }
    test_contrast(app, pt_color_scheme_dark);
    test_contrast(app, pt_color_scheme_light);
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    test_tokens_and_metrics(app);
    test_window_overrides(app);
    pt_app_destroy(app);
    return pt_test_finish("theme");
}
