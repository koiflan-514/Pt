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

/* --- W2: known-answer vectors for the pure formulas ---------------------- */

/* srgb_channel is the WCAG 2.x linearisation. Pin the exact endpoints, the
 * widely quoted midpoint and the branch threshold from both sides. */
static void test_srgb_known_answers(void) {
    pt_check_near(te_wcag_srgb_channel(0), 0.0, 0.0);
    pt_check_near(te_wcag_srgb_channel(255), 1.0, 1e-15);
    /* Canonical value for 128/255 (see WCAG relative luminance worked
     * examples): 0.2158605001... */
    pt_check_near(te_wcag_srgb_channel(128), 0.21586050011389926, 1e-12);
    /* Threshold is v <= 0.04045 on v = c/255. 10/255 = 0.0392 (linear branch),
     * 11/255 = 0.0431 (power branch). Values are exact formula results. */
    pt_check_near(te_wcag_srgb_channel(10), 10.0 / 255.0 / 12.92, 1e-15);
    pt_check_near(te_wcag_srgb_channel(11),
                  pow((11.0 / 255.0 + 0.055) / 1.055, 2.4), 1e-15);
    /* The two branches agree to within one 8-bit step at the threshold, so the
     * curve is continuous: 10 < 11 and 10/12.92 is ~0.00304, 11 power is
     * ~0.00335. */
    pt_check(te_wcag_srgb_channel(11) > te_wcag_srgb_channel(10));
    /* Every result stays in [0, 1]. */
    for (int c = 0; c <= 255; ++c) {
        double value = te_wcag_srgb_channel((uint8_t)c);
        pt_check(value >= 0.0 && value <= 1.0);
    }
}

/* Relative luminance closed forms for the primaries and their sum. */
static void test_luminance_known_answers(void) {
    pt_check_near(te_wcag_relative_luminance(0x000000FFu), 0.0, 0.0);
    pt_check_near(te_wcag_relative_luminance(0xFFFFFFFFu), 1.0, 1e-12);
    /* Pure primaries carry exactly their coefficient. */
    pt_check_near(te_wcag_relative_luminance(0xFF0000FFu), 0.2126, 1e-12);
    pt_check_near(te_wcag_relative_luminance(0x00FF00FFu), 0.7152, 1e-12);
    pt_check_near(te_wcag_relative_luminance(0x0000FFFFu), 0.0722, 1e-12);
    /* Mid grey 0x808080: 0.2158605 (all three channels equal), not 0.5. */
    pt_check_near(te_wcag_relative_luminance(0x808080FFu), 0.21586050011389926, 1e-12);
    /* Alpha is ignored: every alpha of white has luminance 1. */
    pt_check_near(te_wcag_relative_luminance(0xFFFFFF00u), 1.0, 1e-12);
    pt_check_near(te_wcag_relative_luminance(0xFFFFFF7Fu), 1.0, 1e-12);
    /* Luminance is symmetric in colour channels: R==G==B is grey. */
    pt_check_near(te_wcag_relative_luminance(0x123456FFu),
                  0.2126 * te_wcag_srgb_channel(0x12) +
                      0.7152 * te_wcag_srgb_channel(0x34) +
                      0.0722 * te_wcag_srgb_channel(0x56),
                  1e-15);
}

/* Contrast ratio closed forms: 21 for black/white, 1 for identical, symmetric,
 * and order independent. */
static void test_ratio_known_answers(void) {
    pt_check_near(te_wcag_contrast_ratio(0x000000FFu, 0xFFFFFFFFu), 21.0, 1e-12);
    pt_check_near(te_wcag_contrast_ratio(0xFFFFFFFFu, 0x000000FFu), 21.0, 1e-12);
    pt_check_near(te_wcag_contrast_ratio(0xDEADBEFFu, 0xDEADBEFFu), 1.0, 0.0);
    pt_check_near(te_wcag_contrast_ratio(0x000000FFu, 0x000000FFu), 1.0, 0.0);
    /* Symmetry for an arbitrary pair. */
    pt_check_near(te_wcag_contrast_ratio(0x123456FFu, 0xABCDEFFFu),
                  te_wcag_contrast_ratio(0xABCDEFFFu, 0x123456FFu), 0.0);
    /* Documented #777777 / #767676 on white (docs/theming.md guidance). */
    pt_check_near(te_wcag_contrast_ratio(0x777777FFu, 0xFFFFFFFFu), 4.478089453577214, 1e-9);
    pt_check_near(te_wcag_contrast_ratio(0x767676FFu, 0xFFFFFFFFu), 4.542224959605253, 1e-9);
    /* Alpha does not change the measured ratio (luminance ignores it). */
    pt_check_near(te_wcag_contrast_ratio(0xFFFFFF00u, 0x000000FFu), 21.0, 1e-12);
    /* Every ratio over a sweep stays within [1, 21]. */
    static const pt_color sweep[] = {0x000000FFu, 0x111111FFu, 0x808080FFu,
                                     0x7FFFFFFFu, 0xFFFFFFFFu, 0xFF0000FFu,
                                     0x00FF00FFu, 0x0000FFFFu};
    for (size_t i = 0; i < sizeof(sweep) / sizeof(sweep[0]); ++i) {
        for (size_t j = 0; j < sizeof(sweep) / sizeof(sweep[0]); ++j) {
            double ratio = te_wcag_contrast_ratio(sweep[i], sweep[j]);
            pt_check(ratio >= 1.0 - 1e-12 && ratio <= 21.0 + 1e-12);
        }
    }
}

/* Constructed boundary pairs where the floor flips between two adjacent 8-bit
 * greys on black. This proves the grade thresholds are applied at the exact
 * comparison (>= 4.5, >= 7.0) rather than an approximate threshold. */
static void test_grade_boundary_pairs(void) {
    /* AA / normal-text floor 4.5: grey 116 misses, 117 clears. */
    pt_check(te_wcag_contrast_ratio(0x747474FFu, 0x000000FFu) < 4.5);
    pt_check(te_wcag_contrast_ratio(0x757575FFu, 0x000000FFu) >= 4.5);
    pt_check(te_wcag_grade_for(te_wcag_contrast_ratio(0x747474FFu, 0x000000FFu)) ==
             TE_WCAG_GRADE_FAIL);
    pt_check(te_wcag_grade_for(te_wcag_contrast_ratio(0x757575FFu, 0x000000FFu)) ==
             TE_WCAG_GRADE_AA);
    /* AAA / normal-text floor 7.0: grey 148 misses, 149 clears. */
    pt_check(te_wcag_contrast_ratio(0x949494FFu, 0x000000FFu) < 7.0);
    pt_check(te_wcag_contrast_ratio(0x959595FFu, 0x000000FFu) >= 7.0);
    pt_check(te_wcag_grade_for(te_wcag_contrast_ratio(0x949494FFu, 0x000000FFu)) ==
             TE_WCAG_GRADE_AA);
    pt_check(te_wcag_grade_for(te_wcag_contrast_ratio(0x959595FFu, 0x000000FFu)) ==
             TE_WCAG_GRADE_AAA);
    /* AA-large floor 3.0: grey 89 misses, 90 clears. */
    pt_check(te_wcag_contrast_ratio(0x595959FFu, 0x000000FFu) < 3.0);
    pt_check(te_wcag_contrast_ratio(0x5A5A5AFFu, 0x000000FFu) >= 3.0);
    /* grade_for is exact at the equality points. */
    pt_check(te_wcag_grade_for(4.5) == TE_WCAG_GRADE_AA);
    pt_check(te_wcag_grade_for(7.0) == TE_WCAG_GRADE_AAA);
    pt_check(te_wcag_grade_for(3.0) == TE_WCAG_GRADE_FAIL); /* 3.0 is AA-large, below AA */
    pt_check(te_wcag_grade_for(nextafter(4.5, 0.0)) == TE_WCAG_GRADE_FAIL);
    pt_check(te_wcag_grade_for(nextafter(7.0, 0.0)) == TE_WCAG_GRADE_AA);
}

/* --- W2: straight-alpha composite vectors -------------------------------- */

static void test_composite_known_answers(void) {
    /* Opaque fg returns fg exactly, regardless of bg. */
    pt_check(te_wcag_composite_over(0xABCDEFFFu, 0x123456FFu) == 0xABCDEFFFu);
    /* Transparent fg returns bg, with alpha forced to FF. */
    pt_check(te_wcag_composite_over(0xABCDEF00u, 0x123456FFu) == 0x123456FFu);
    /* White at 50% over black is the arithmetic midpoint 128 (round half up via
     * the +127 bias). */
    pt_check(te_wcag_composite_over(0xFFFFFF80u, 0x000000FFu) == 0x808080FFu);
    /* Black at 50% over white is 127 (255*128/255 rounded = 127.5 -> 128, but
     * black*128 + 255*127 = 32385, +127 = 32512, /255 = 127). Manual: */
    pt_check(te_wcag_composite_over(0x00000080u, 0xFFFFFFFFu) == 0x7F7F7FFFu);
    /* Hand-computed straight-alpha case, differing channels:
     *   red   = (0x80*0x80 + 0x10*0x7F + 127)/255 = (16384+2032+127)/255 = 72 = 0x48
     *   green = (0x40*0x80 + 0x20*0x7F + 127)/255 = (8192+4064+127)/255  = 48 = 0x30
     *   blue  = (0x20*0x80 + 0x30*0x7F + 127)/255 = (4096+6105+127)/255  = 40 = 0x28 */
    pt_check(te_wcag_composite_over(0x80402080u, 0x102030FFu) == 0x483028FFu);
    /* Almost-transparent alpha 1: result is essentially bg (AACCEE, since
     * 0x12+1 step toward 0xAB rounds down on all three). */
    pt_check(te_wcag_composite_over(0x12345601u, 0xABCDEFFFu) == 0xAACCEEFFu);
    /* Almost-opaque alpha 254: result is essentially fg (133557). */
    pt_check(te_wcag_composite_over(0x123456FEu, 0xABCDEFFFu) == 0x133557FFu);
    /* Result alpha is always FF for any 8-bit fg alpha. */
    for (int a = 0; a <= 255; ++a) {
        pt_color out = te_wcag_composite_over(0x12345600u | (pt_color)a, 0xABCDEFFFu);
        pt_check((out & 0xFFu) == 0xFFu);
    }
    /* A composite is idempotent when the fg is already the bg (any alpha). */
    pt_check(te_wcag_composite_over(0x42A0C080u, 0x42A0C0FFu) == 0x42A0C0FFu);
}

/* Mixing linear halves: the composite is per-channel and independent. Apply the
 * same fg over two backgrounds and confirm channel ordering is respected. */
static void test_composite_channel_order(void) {
    /* A pure-red mid fg over pure-blue mid bg: red channel rises from bg's 0,
     * blue channel rises from fg's 0. */
    /* red   = (0xFF*0x80 + 0x00*0x7F + 127)/255 = (32640+127)/255 = 128 = 0x80
     * green = 0
     * blue  = (0x00*0x80 + 0xFF*0x7F + 127)/255 = (32385+127)/255 = 127 = 0x7F */
    pt_color out = te_wcag_composite_over(0xFF000080u, 0x0000FFFFu);
    pt_check(((out >> 24) & 0xFFu) == 0x80u);
    pt_check(((out >> 16) & 0xFFu) == 0x00u);
    pt_check(((out >> 8) & 0xFFu) == 0x7Fu);
    pt_check((out & 0xFFu) == 0xFFu);
}

/* --- W2: default-palette floors with documented ratios ------------------- */

/* One pair's documented ratio, keyed by the pair label. The values are the two
 * decimals quoted in docs/theming.md, widened to 0.01 tolerance as the design
 * requires. */
typedef struct documented_ratio {
    const char *label;
    double dark;
    double light;
} documented_ratio;

static const documented_ratio documented[] = {
    {"text on background", 16.59, 16.40},
    {"text on surface", 15.69, 17.74},
    {"text on surface_raised", 14.80, 17.74},
    {"text_muted on background", 7.40, 4.98},
    {"text_muted on surface", 7.00, 5.39},
    {"accent_text on accent", 4.99, 5.75},
    {"accent_text on accent_hover", 4.53, 5.91},
    {"accent_text on accent_pressed", 6.22, 6.91},
    {"danger on surface", 6.17, 6.03},
    {"success on surface", 8.89, 5.44},
    {"warning on surface", 9.56, 5.93},
    {"focus on background", 8.99, 5.32},
};

static const documented_ratio *find_documented(const char *label) {
    for (size_t i = 0; i < sizeof(documented) / sizeof(documented[0]); ++i) {
        if (strcmp(documented[i].label, label) == 0) {
            return &documented[i];
        }
    }
    return NULL;
}

/* The twelve documented floors match the guide within 0.01 for both schemes.
 * This pins the module to the same numbers the theming guide publishes. */
static void test_documented_ratios(pt_app *app) {
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
        unsigned matched = 0;
        for (unsigned i = 0; i < report.count; ++i) {
            const documented_ratio *doc = find_documented(report.rows[i].pair->label);
            if (doc == NULL) {
                continue;
            }
            ++matched;
            double expected = (s == 0) ? doc->dark : doc->light;
            pt_check_near(report.rows[i].ratio, expected, 0.01);
        }
        pt_check(matched == 12);
    }
}

/* The two selection rows are composite: the measured background must equal a
 * manually composited selection wash and differ from the raw selection token. */
static void test_selection_composites_are_composite(pt_app *app) {
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_color colors[pt_theme_token_count];
    for (int i = 0; i < pt_theme_token_count; ++i) {
        colors[i] = pt_app_get_theme_color(app, (pt_theme_token)i);
    }
    te_wcag_report report;
    memset(&report, 0, sizeof report);
    pt_check(te_wcag_check_palette(colors, &report));
    unsigned composite_rows = 0;
    for (unsigned i = 0; i < report.count; ++i) {
        const te_wcag_pair *pair = report.rows[i].pair;
        if (pair->kind != TE_WCAG_PAIR_COMPOSITE) {
            continue;
        }
        ++composite_rows;
        pt_color wash = te_wcag_composite_over(colors[pair->background],
                                                colors[pair->backdrop]);
        pt_check(report.rows[i].background == wash);
        /* The raw selection token is translucent, so compositing changes it. */
        pt_check(wash != colors[pair->background]);
        pt_check(report.rows[i].ratio >= 4.5);
    }
    pt_check(composite_rows == 2);
}

/* Break one floor on purpose: the check must return false and name the failing
 * row, proving the guard is real (not a table that always passes). */
static void test_broken_palette_guard(pt_app *app) {
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_color colors[pt_theme_token_count];
    for (int i = 0; i < pt_theme_token_count; ++i) {
        colors[i] = pt_app_get_theme_color(app, (pt_theme_token)i);
    }
    /* Start green. */
    te_wcag_report good;
    memset(&good, 0, sizeof good);
    pt_check(te_wcag_check_palette(colors, &good));

    /* Break body text: force text to the background colour so the ratio drops
     * to 1. text appears in three AAA rows. */
    colors[pt_theme_text] = colors[pt_theme_background];
    te_wcag_report bad;
    memset(&bad, 0, sizeof bad);
    pt_check(!te_wcag_check_palette(colors, &bad));
    pt_check(!te_wcag_report_passes(&bad));
    pt_check(bad.failed == 3); /* text on background, surface, surface_raised */
    unsigned text_failures = 0;
    for (unsigned i = 0; i < bad.count; ++i) {
        if (bad.rows[i].pair->foreground == pt_theme_text && !bad.rows[i].informational) {
            ++text_failures;
            pt_check(!bad.rows[i].pass);
            pt_check(bad.rows[i].ratio < 7.0);
            /* text == background is exactly 1.0 against the background row. */
            if (bad.rows[i].pair->background == pt_theme_background) {
                pt_check_near(bad.rows[i].ratio, 1.0, 0.0);
            }
        }
    }
    pt_check(text_failures == 3);
    /* The informational text_disabled row still passes even if it is broken. */
    colors[pt_theme_text] = pt_app_get_theme_color(app, pt_theme_text);
    colors[pt_theme_text_disabled] = colors[pt_theme_surface];
    te_wcag_report disabled;
    memset(&disabled, 0, sizeof disabled);
    pt_check(te_wcag_check_palette(colors, &disabled));
    for (unsigned i = 0; i < disabled.count; ++i) {
        if (disabled.rows[i].informational) {
            pt_check(disabled.rows[i].pass);
        }
    }
}

/* A single composite row can be broken on its own: an opaque selection equal to
 * the selection_text colour drops just those two rows. */
static void test_composite_row_can_fail(pt_app *app) {
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_color colors[pt_theme_token_count];
    for (int i = 0; i < pt_theme_token_count; ++i) {
        colors[i] = pt_app_get_theme_color(app, (pt_theme_token)i);
    }
    colors[pt_theme_selection] = colors[pt_theme_selection_text] | 0xFFu;
    te_wcag_report report;
    memset(&report, 0, sizeof report);
    pt_check(!te_wcag_check_palette(colors, &report));
    pt_check(report.failed == 2);
    for (unsigned i = 0; i < report.count; ++i) {
        if (report.rows[i].pair->kind == TE_WCAG_PAIR_COMPOSITE) {
            pt_check(!report.rows[i].pass);
            pt_check_near(report.rows[i].ratio, 1.0, 0.0);
        }
    }
}

/* Verify the app getters reproduce the shipped fixtures, so the floor checks
 * above measure exactly the palettes the editor imports by default. */
static void test_palette_matches_fixture(pt_app *app) {
    /* Dark fixture values (tools/theme-editor/themes/dark.json), token order. */
    static const pt_color dark[pt_theme_token_count] = {
        0x0B0E14FFu, 0x11151EFFu, 0x161B26FFu, 0x1D2431FFu, 0x232B3AFFu, 0x39445AFFu,
        0xE9EEF7FFu, 0x94A1B8FFu, 0x3568E0FFu, 0x3D6FE8FFu, 0xFFFFFFFFu, 0xF16B75FFu,
        0x4ACB8DFFu, 0xE5B454FFu, 0x00000073u, 0x0E121BFFu, 0x4A566EFFu, 0x8CB0FFFFu,
        0x080B11FFu, 0x1A2130FFu, 0x222B3CFFu, 0x3568E033u, 0x3568E059u, 0x2A3344FFu,
        0x141924FFu, 0x1A2130FFu, 0x39445AFFu, 0x232B3AFFu, 0x5B6577FFu, 0x2F5BC0FFu,
        0x2A3447FFu, 0xF16B7526u, 0x4ACB8D26u, 0xE5B45426u, 0x000000A6u, 0x1D2431FFu,
        0x2A3447FFu, 0xFFFFFFFFu};
    static const pt_color light[pt_theme_token_count] = {
        0xF4F6FAFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xF0F3F8FFu, 0xE1E6EEFFu, 0xC8D0DDFFu,
        0x111827FFu, 0x5F6B7FFFu, 0x2559E6FFu, 0x1E56E8FFu, 0xFFFFFFFFu, 0xB92C38FFu,
        0x0F7A37FFu, 0x8A5A00FFu, 0x0F172A24u, 0xFBFCFEFFu, 0xADB8C8FFu, 0x2559E6FFu,
        0xEDF0F5FFu, 0xF8FAFCFFu, 0xEFF3F9FFu, 0x2559E626u, 0x2559E640u, 0xDCE3EDFFu,
        0xEDF0F5FFu, 0xE5EAF2FFu, 0xC8D0DDFFu, 0xE1E6EEFFu, 0xA0A9B8FFu, 0x1C4ECFFFu,
        0xE4EAF3FFu, 0xB92C3820u, 0x0F7A3720u, 0x8A5A0020u, 0x0F172A66u, 0xE7EBF1FFu,
        0xF3F5F9FFu, 0x111827FFu};
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    for (int i = 0; i < pt_theme_token_count; ++i) {
        pt_check(pt_app_get_theme_color(app, (pt_theme_token)i) == dark[i]);
    }
    pt_app_set_color_scheme(app, pt_color_scheme_light);
    for (int i = 0; i < pt_theme_token_count; ++i) {
        pt_check(pt_app_get_theme_color(app, (pt_theme_token)i) == light[i]);
    }
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
    test_srgb_known_answers();
    test_luminance_known_answers();
    test_ratio_known_answers();
    test_grade_boundary_pairs();
    test_composite_known_answers();
    test_composite_channel_order();

    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.theme_wcag.more");
    pt_check(app != NULL);
    if (app != NULL) {
        test_documented_ratios(app);
        test_selection_composites_are_composite(app);
        test_broken_palette_guard(app);
        test_composite_row_can_fail(app);
        test_palette_matches_fixture(app);
        pt_app_destroy(app);
    }
    return pt_test_finish("theme_wcag");
}