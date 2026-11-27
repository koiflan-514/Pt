/*
 * Pt theme editor - WCAG 2.x helpers (W0 minimal implementation).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is the W0 minimal real implementation of the frozen interface. W2 owns
 * the hardening and the full vector/default-palette suite; the formulas here
 * match tests/test_theme.c and docs/theming.md.
 */

#include "theme_wcag.h"

#include <math.h>

static_assert(pt_theme_token_count == 38, "update the WCAG pair table");

double te_wcag_srgb_channel(uint8_t component) {
    double value = (double)component / 255.0;
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return pow((value + 0.055) / 1.055, 2.4);
}

double te_wcag_relative_luminance(pt_color color) {
    double r = te_wcag_srgb_channel((uint8_t)((color >> 24) & 0xFFu));
    double g = te_wcag_srgb_channel((uint8_t)((color >> 16) & 0xFFu));
    double b = te_wcag_srgb_channel((uint8_t)((color >> 8) & 0xFFu));
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double te_wcag_contrast_ratio(pt_color a, pt_color b) {
    double first = te_wcag_relative_luminance(a);
    double second = te_wcag_relative_luminance(b);
    if (first < second) {
        double swap = first;
        first = second;
        second = swap;
    }
    return (first + 0.05) / (second + 0.05);
}

pt_color te_wcag_composite_over(pt_color fg, pt_color bg) {
    uint32_t alpha = fg & 0xFFu;
    uint32_t inverse = 255u - alpha;
    uint32_t red = (((fg >> 24) & 0xFFu) * alpha + ((bg >> 24) & 0xFFu) * inverse + 127u) / 255u;
    uint32_t green =
        (((fg >> 16) & 0xFFu) * alpha + ((bg >> 16) & 0xFFu) * inverse + 127u) / 255u;
    uint32_t blue = (((fg >> 8) & 0xFFu) * alpha + ((bg >> 8) & 0xFFu) * inverse + 127u) / 255u;
    return (red << 24) | (green << 16) | (blue << 8) | 0xFFu;
}

/* The twelve documentation floors, the two selection composites from
 * tests/test_theme.c and one informational text_disabled row. */
static const te_wcag_pair te_pairs[] = {
    {"text on background", pt_theme_text, pt_theme_background, pt_theme_background,
     TE_WCAG_PAIR_DIRECT, 7.0, false},
    {"text on surface", pt_theme_text, pt_theme_surface, pt_theme_surface,
     TE_WCAG_PAIR_DIRECT, 7.0, false},
    {"text on surface_raised", pt_theme_text, pt_theme_surface_raised, pt_theme_surface_raised,
     TE_WCAG_PAIR_DIRECT, 7.0, false},
    {"text_muted on background", pt_theme_text_muted, pt_theme_background, pt_theme_background,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"text_muted on surface", pt_theme_text_muted, pt_theme_surface, pt_theme_surface,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"accent_text on accent", pt_theme_accent_text, pt_theme_accent, pt_theme_accent,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"accent_text on accent_hover", pt_theme_accent_text, pt_theme_accent_hover,
     pt_theme_accent_hover, TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"accent_text on accent_pressed", pt_theme_accent_text, pt_theme_accent_pressed,
     pt_theme_accent_pressed, TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"danger on surface", pt_theme_danger, pt_theme_surface, pt_theme_surface,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"success on surface", pt_theme_success, pt_theme_surface, pt_theme_surface,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"warning on surface", pt_theme_warning, pt_theme_surface, pt_theme_surface,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"focus on background", pt_theme_focus, pt_theme_background, pt_theme_background,
     TE_WCAG_PAIR_DIRECT, 4.5, false},
    {"selection_text on selection over surface", pt_theme_selection_text, pt_theme_selection,
     pt_theme_surface, TE_WCAG_PAIR_COMPOSITE, 4.5, false},
    {"selection_text on selection over background", pt_theme_selection_text,
     pt_theme_selection, pt_theme_background, TE_WCAG_PAIR_COMPOSITE, 4.5, false},
    {"text_disabled on surface (informational)", pt_theme_text_disabled, pt_theme_surface,
     pt_theme_surface, TE_WCAG_PAIR_DIRECT, 0.0, true},
};

static_assert(sizeof(te_pairs) / sizeof(te_pairs[0]) == TE_WCAG_MAX_PAIRS,
              "update TE_WCAG_MAX_PAIRS with the WCAG pair table");

size_t te_wcag_pair_count(void) {
    return sizeof(te_pairs) / sizeof(te_pairs[0]);
}

const te_wcag_pair *te_wcag_pair_at(size_t index) {
    if (index >= te_wcag_pair_count()) {
        return NULL;
    }
    return &te_pairs[index];
}

const te_wcag_pair *te_wcag_pairs(void) {
    return te_pairs;
}

te_wcag_grade te_wcag_grade_for(double ratio) {
    if (ratio >= 7.0) {
        return TE_WCAG_GRADE_AAA;
    }
    if (ratio >= 4.5) {
        return TE_WCAG_GRADE_AA;
    }
    return TE_WCAG_GRADE_FAIL;
}

const char *te_wcag_grade_name(te_wcag_grade grade) {
    switch (grade) {
    case TE_WCAG_GRADE_AAA:
        return "AAA";
    case TE_WCAG_GRADE_AA:
        return "AA";
    case TE_WCAG_GRADE_FAIL:
        return "fail";
    }
    return "fail";
}

bool te_wcag_check_palette(const pt_color colors[pt_theme_token_count],
                           te_wcag_report *report) {
    if (report != NULL) {
        report->count = 0;
        report->failed = 0;
    }
    if (colors == NULL) {
        return false;
    }
    unsigned failed = 0;
    size_t count = te_wcag_pair_count();
    for (size_t i = 0; i < count; ++i) {
        const te_wcag_pair *pair = &te_pairs[i];
        pt_color foreground = colors[pair->foreground];
        pt_color background = colors[pair->background];
        if (pair->kind == TE_WCAG_PAIR_COMPOSITE) {
            background = te_wcag_composite_over(background, colors[pair->backdrop]);
        }
        double ratio = te_wcag_contrast_ratio(foreground, background);
        bool pass = pair->informational || ratio >= pair->floor;
        if (!pass) {
            ++failed;
        }
        if (report != NULL && report->count < TE_WCAG_MAX_PAIRS) {
            te_wcag_result *row = &report->rows[report->count];
            row->pair = pair;
            row->foreground = foreground;
            row->background = background;
            row->ratio = ratio;
            row->grade = te_wcag_grade_for(ratio);
            row->pass = pass;
            row->informational = pair->informational;
            ++report->count;
        }
    }
    if (report != NULL) {
        report->failed = failed;
    }
    return failed == 0;
}

bool te_wcag_report_passes(const te_wcag_report *report) {
    return report != NULL && report->failed == 0;
}