/*
 * Pt theme editor - pure WCAG 2.x contrast helpers and the floor table.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_theme_editor_theme_wcag_h
#define pt_theme_editor_theme_wcag_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pt/theme.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frozen interface for the WCAG module (M27 W0).
 *
 * Everything here is a pure function or static data: no Pt state, no window, no
 * allocation, no locale. pt_color is the public 0xRRGGBBAA value; the helpers
 * match the formulas used by tests/test_theme.c and docs/theming.md, not the
 * core's approximate pt_theme_contrast_text().
 */

/* WCAG 2.x sRGB linearisation for one 8-bit component:
 *   v = c / 255
 *   v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ^ 2.4
 * The result is in [0, 1] and is monotonic in component. */
double te_wcag_srgb_channel(uint8_t component);

/* Relative luminance on linear channels, ignoring alpha:
 *   0.2126 R + 0.7152 G + 0.0722 B
 * The result is in [0, 1]. */
double te_wcag_relative_luminance(pt_color color);

/* WCAG contrast ratio (Lmax + 0.05) / (Lmin + 0.05), ignoring alpha. The
 * result is in [1, 21]; identical colours give 1.0 and black/white give 21.0. */
double te_wcag_contrast_ratio(pt_color a, pt_color b);

/* Straight-alpha source-over composite of fg on an opaque bg, using the
 * 0xRRGGBBAA byte order and integer rounding like tests/test_theme.c:
 * result = round((fg * a + bg * (255 - a)) / 255) per channel, alpha 0xFF.
 * A fully transparent fg returns bg (opaque) and an opaque fg returns fg. */
pt_color te_wcag_composite_over(pt_color fg, pt_color bg);

/* How a pair is measured. DIRECT compares foreground against background.
 * COMPOSITE first composites background (the translucent wash) over backdrop,
 * then compares foreground against that resolved colour. */
typedef enum te_wcag_pair_kind {
    TE_WCAG_PAIR_DIRECT = 0,
    TE_WCAG_PAIR_COMPOSITE,
} te_wcag_pair_kind;

/* One table row. label is a static borrowed English description. foreground,
 * background and backdrop are pt_theme_token values; backdrop is used only for
 * TE_WCAG_PAIR_COMPOSITE. floor is the documented minimum ratio (7.0 AAA,
 * 4.5 AA). informational rows are reported but never fail: floor is 0.0 and
 * informational is true (text_disabled is intentionally below AA). */
typedef struct te_wcag_pair {
    const char *label;
    pt_theme_token foreground;
    pt_theme_token background;
    pt_theme_token backdrop;
    te_wcag_pair_kind kind;
    double floor;
    bool informational;
} te_wcag_pair;

/* The table has 12 floored documentation pairs, the two selection_text
 * composites and one informational text_disabled row; TE_WCAG_MAX_PAIRS is the
 * upper bound used by te_wcag_report. te_wcag_pairs() returns a static borrowed
 * array of te_wcag_pair_count() entries; te_wcag_pair_at() returns NULL for an
 * out-of-range index. Rows are in a stable order: it is the order --check
 * prints. */
#define TE_WCAG_MAX_PAIRS 15u
size_t te_wcag_pair_count(void);
const te_wcag_pair *te_wcag_pair_at(size_t index);
const te_wcag_pair *te_wcag_pairs(void);

/* Grade of a raw ratio: >= 7.0 AAA, >= 4.5 AA, else fail. This is a level, not
 * a pass/fail verdict for a specific pair; te_wcag_result.pass applies the
 * pair's floor. */
typedef enum te_wcag_grade {
    TE_WCAG_GRADE_FAIL = 0,
    TE_WCAG_GRADE_AA,
    TE_WCAG_GRADE_AAA,
} te_wcag_grade;

te_wcag_grade te_wcag_grade_for(double ratio);
/* "fail", "AA" or "AAA"; never NULL. */
const char *te_wcag_grade_name(te_wcag_grade grade);

/* One measured row. pair is a borrowed pointer into the static table.
 * foreground and background are the resolved colours actually measured
 * (background is the composite result for a COMPOSITE row). ratio is in
 * [1, 21]. grade is the raw level and pass is ratio >= pair->floor (always true
 * for informational rows). */
typedef struct te_wcag_result {
    const te_wcag_pair *pair;
    pt_color foreground;
    pt_color background;
    double ratio;
    te_wcag_grade grade;
    bool pass;
    bool informational;
} te_wcag_result;

/* Measured table. count is the number of rows written (at most
 * TE_WCAG_MAX_PAIRS) and failed counts only non-informational rows that miss
 * their floor. */
typedef struct te_wcag_report {
    unsigned count;
    unsigned failed;
    te_wcag_result rows[TE_WCAG_MAX_PAIRS];
} te_wcag_report;

/* Fills report from a 38-entry palette in pt_theme_token order. Returns true
 * when every non-informational row passes its floor, false otherwise (also
 * false for NULL colours). report may be NULL, in which case only the verdict
 * is returned. The input array is borrowed for the duration of the call. */
bool te_wcag_check_palette(const pt_color colors[pt_theme_token_count],
                           te_wcag_report *report);

/* Convenience verdict for a report; NULL is false. */
bool te_wcag_report_passes(const te_wcag_report *report);

#ifdef __cplusplus
}
#endif

#endif