/*
 * WCAG pure helpers, the floor table and the default-palette verdict.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "harness.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <pt/pt.h>

#include "../tools/theme-editor/theme_wcag.h"

static_assert(pt_theme_token_count == 38, "update the WCAG pair table");
static_assert(TE_WCAG_MAX_PAIRS == 15u, "the table has 14 floors plus one informational row");

/* Compile-time signature check for the frozen interface. */
static void test_signatures(void) {
    double (*channel)(uint8_t) = te_wcag_srgb_channel;
    double (*luminance)(pt_color) = te_wcag_relative_luminance;
    double (*ratio)(pt_color, pt_color) = te_wcag_contrast_ratio;
    pt_color (*composite)(pt_color, pt_color) = te_wcag_composite_over;
    size_t (*count)(void) = te_wcag_pair_count;
    const te_wcag_pair *(*at)(size_t) = te_wcag_pair_at;
    const te_wcag_pair *(*all)(void) = te_wcag_pairs;
    te_wcag_grade (*grade)(double) = te_wcag_grade_for;
    const char *(*grade_name)(te_wcag_grade) = te_wcag_grade_name;
    bool (*check)(const pt_color *, te_wcag_report *) = te_wcag_check_palette;
    bool (*passes)(const te_wcag_report *) = te_wcag_report_passes;

    pt_check(channel != NULL);
    pt_check(luminance != NULL);
    pt_check(ratio != NULL);
    pt_check(composite != NULL);
    pt_check(count != NULL);
    pt_check(at != NULL);
    pt_check(all != NULL);
    pt_check(grade != NULL);
    pt_check(grade_name != NULL);
    pt_check(check != NULL);
    pt_check(passes != NULL);
}

static void test_vectors(void) {
    pt_check_near(te_wcag_contrast_ratio(0x000000FFu, 0xFFFFFFFFu), 21.0, 1e-9);
    pt_check_near(te_wcag_contrast_ratio(0xFFFFFFFFu, 0x000000FFu), 21.0, 1e-9);
    pt_check_near(te_wcag_contrast_ratio(0x777777FFu, 0xFFFFFFFFu), 4.4781, 1e-4);
    pt_check_near(te_wcag_contrast_ratio(0x767676FFu, 0xFFFFFFFFu), 4.5422, 1e-4);
    pt_check_near(te_wcag_contrast_ratio(0x123456FFu, 0x123456FFu), 1.0, 1e-12);

    pt_check_near(te_wcag_relative_luminance(0xFFFFFFFFu), 1.0, 1e-9);
    pt_check_near(te_wcag_relative_luminance(0x000000FFu), 0.0, 1e-9);
    /* Luminance ignores alpha. */
    pt_check_near(te_wcag_relative_luminance(0xFFFFFF00u),
                  te_wcag_relative_luminance(0xFFFFFFFFu), 1e-12);
}

static void test_srgb_threshold(void) {
    double linear = 10.0 / 255.0 / 12.92;
    double value = 11.0 / 255.0;
    double power = pow((value + 0.055) / 1.055, 2.4);
    pt_check_near(te_wcag_srgb_channel(10), linear, 1e-12);
    pt_check_near(te_wcag_srgb_channel(11), power, 1e-12);
    for (int c = 1; c <= 255; ++c) {
        pt_check(te_wcag_srgb_channel((uint8_t)(c - 1)) <
                 te_wcag_srgb_channel((uint8_t)c));
    }
}

static void test_composite(void) {
    pt_check(te_wcag_composite_over(0x112233FFu, 0x445566FFu) == 0x112233FFu);
    pt_check(te_wcag_composite_over(0x11223300u, 0x445566FFu) == 0x445566FFu);
    pt_check(te_wcag_composite_over(0xFFFFFF80u, 0x000000FFu) == 0x808080FFu);
    pt_check(te_wcag_composite_over(0x00000080u, 0xFFFFFFFFu) == 0x7F7F7FFFu);
    /* Source-over an opaque background always yields an opaque result. */
    pt_check((te_wcag_composite_over(0x12345612u, 0xABCDEF34u) & 0xFFu) == 0xFFu);
}

static void test_pair_table(void) {
    size_t count = te_wcag_pair_count();
    const te_wcag_pair *pairs = te_wcag_pairs();
    pt_check(count == TE_WCAG_MAX_PAIRS);
    pt_check(count == 15);
    pt_check(pairs != NULL);
    pt_check(te_wcag_pair_at(count) == NULL);

    unsigned floored = 0;
    unsigned informational = 0;
    unsigned composite = 0;
    for (size_t i = 0; i < count; ++i) {
        const te_wcag_pair *pair = te_wcag_pair_at(i);
        pt_check(pair != NULL);
        if (pair == NULL) {
            continue;
        }
        pt_check(pair->label != NULL && pair->label[0] != 0);
        pt_check((int)pair->foreground >= 0 && (int)pair->foreground < pt_theme_token_count);
        pt_check((int)pair->background >= 0 && (int)pair->background < pt_theme_token_count);
        pt_check((int)pair->backdrop >= 0 && (int)pair->backdrop < pt_theme_token_count);
        if (pair->informational) {
            ++informational;
            pt_check(pair->floor == 0.0);
        } else {
            ++floored;
            pt_check(pair->floor == 7.0 || pair->floor == 4.5);
        }
        if (pair->kind == TE_WCAG_PAIR_COMPOSITE) {
            ++composite;
            /* Both selection rows composite the translucent wash before
             * measuring. */
            pt_check(pair->foreground == pt_theme_selection_text);
            pt_check(pair->background == pt_theme_selection);
            pt_check(pair->backdrop == pt_theme_surface ||
                     pair->backdrop == pt_theme_background);
        }
    }
    pt_check(floored == 14);
    pt_check(informational == 1);
    pt_check(composite == 2);
}

static void test_grades(void) {
    pt_check(te_wcag_grade_for(21.0) == TE_WCAG_GRADE_AAA);
    pt_check(te_wcag_grade_for(7.0) == TE_WCAG_GRADE_AAA);
    pt_check(te_wcag_grade_for(6.999) == TE_WCAG_GRADE_AA);
    pt_check(te_wcag_grade_for(4.5) == TE_WCAG_GRADE_AA);
    pt_check(te_wcag_grade_for(4.49) == TE_WCAG_GRADE_FAIL);
    pt_check(te_wcag_grade_for(-1.0) == TE_WCAG_GRADE_FAIL);
    pt_check(strcmp(te_wcag_grade_name(TE_WCAG_GRADE_AAA), "AAA") == 0);
    pt_check(strcmp(te_wcag_grade_name(TE_WCAG_GRADE_AA), "AA") == 0);
    pt_check(strcmp(te_wcag_grade_name(TE_WCAG_GRADE_FAIL), "fail") == 0);
    pt_check(te_wcag_grade_name((te_wcag_grade)99) != NULL);
}

static void test_flat_palette(void) {
    pt_color colors[pt_theme_token_count];
    for (int i = 0; i < pt_theme_token_count; ++i) {
        colors[i] = 0x808080FFu;
    }
    te_wcag_report report;
    memset(&report, 0, sizeof report);
    pt_check(!te_wcag_check_palette(colors, &report));
    pt_check(report.count == 15);
    pt_check(report.failed == 14);
    pt_check(!te_wcag_report_passes(&report));
    for (unsigned i = 0; i < report.count; ++i) {
        pt_check_near(report.rows[i].ratio, 1.0, 1e-12);
        if (report.rows[i].informational) {
            pt_check(report.rows[i].pass);
        } else {
            pt_check(!report.rows[i].pass);
        }
    }
    pt_check(!te_wcag_check_palette(NULL, &report));
    pt_check(report.count == 0);
    pt_check(report.failed == 0);
    pt_check(!te_wcag_report_passes(NULL));
}

/* The two built-in palettes must clear every documented floor. This also proves
 * the table indices resolve against the real public getters. W2 can extend the
 * per-pair documentation ratios. */
static void test_default_palettes(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.theme_wcag");
    pt_check(app != NULL);
    if (app == NULL) {
        return;
    }
    pt_color_scheme schemes[] = {pt_color_scheme_dark, pt_color_scheme_light};
    for (size_t s = 0; s < sizeof(schemes) / sizeof(schemes[0]); ++s) {
        pt_app_set_color_scheme(app, schemes[s]);
        pt_color colors[pt_theme_token_count];
        for (int i = 0; i < pt_theme_token_count; ++i) {
            colors[i] = pt_app_get_theme_color(app, (pt_theme_token)i);
        }
        te_wcag_report report;
        memset(&report, 0, sizeof report);
        pt_check(te_wcag_check_palette(colors, &report));
        pt_check(report.failed == 0);
        pt_check(report.count == 15);
        bool informational_below_aa = false;
        for (unsigned i = 0; i < report.count; ++i) {
            if (report.rows[i].informational) {
                informational_below_aa = report.rows[i].ratio < 4.5;
                pt_check(report.rows[i].pass);
            }
        }
        pt_check(informational_below_aa);
    }
    pt_app_destroy(app);
}

int main(void) {
    test_signatures();
    test_vectors();
    test_srgb_threshold();
    test_composite();
    test_pair_table();
    test_grades();
    test_flat_palette();
    test_default_palettes();
    return pt_test_finish("theme_wcag");
}