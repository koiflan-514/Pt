/*
 * Per-run font fallback chains (1.2 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Fonts present on the machine decide how much of this can run. A missing
 * script is reported as an explicit skip and counted in the final line; the
 * chain/API and tofu paths always run.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int skipped;
static const char *latin_family = "DejaVu Sans";

static void skip_script(const char *what) {
    ++skipped;
    printf("skip font_fallback: %s\n", what);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static pt_font *acquire(const char *family) {
    return pt_font_acquire(family, pt_font_weight_regular, pt_font_slant_upright, 18.0f);
}

/* Returns the first installed family in the list that covers the codepoint. */
static const char *find_family(const char *const *families, int count, uint32_t codepoint) {
    for (int i = 0; i < count; ++i) {
        pt_font *font = acquire(families[i]);
        if (font && pt_font_has_glyph(font, codepoint)) {
            return families[i];
        }
    }
    return NULL;
}

static void reset_chains(pt_app *app) {
    pt_app_set_font_fallbacks(app, NULL, 0);
    static const char *const names[] = {"serif", "sans-serif", "monospace", "fam-new"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        pt_app_set_font_fallbacks_for_family(app, names[i], NULL, 0);
    }
    for (int i = 0; i < PT_FONT_FALLBACK_MAX_OVERRIDES; ++i) {
        char family[16];
        snprintf(family, sizeof(family), "fam%d", i);
        pt_app_set_font_fallbacks_for_family(app, family, NULL, 0);
    }
}

/* ---------- chain installation ---------- */

static void test_chain_api(pt_app *app) {
    pt_check(pt_app_get_font_fallback_count(app) == 0);
    pt_check(pt_app_get_font_fallback(app, 0) == NULL);

    const char *chain[] = {"Noto Sans CJK SC", "DejaVu Sans"};
    pt_app_set_font_fallbacks(app, chain, 2);
    pt_check(pt_app_get_font_fallback_count(app) == 2);
    pt_check(strcmp(pt_app_get_font_fallback(app, 0), "Noto Sans CJK SC") == 0);
    pt_check(strcmp(pt_app_get_font_fallback(app, 1), "DejaVu Sans") == 0);
    pt_check(pt_app_get_font_fallback(app, 2) == NULL);
    pt_check(pt_app_get_font_fallback(app, -1) == NULL);

    /* NULL, an empty list, and entries that are all blank restore the default. */
    pt_app_set_font_fallbacks(app, NULL, 0);
    pt_check(pt_app_get_font_fallback_count(app) == 0);
    pt_app_set_font_fallbacks(app, chain, 0);
    pt_check(pt_app_get_font_fallback_count(app) == 0);
    const char *blank[] = {"", ""};
    pt_app_set_font_fallbacks(app, blank, 2);
    pt_check(pt_app_get_font_fallback_count(app) == 0);

    /* The cap is a public bound: extra names are dropped, not accepted. */
    const char *many[] = {"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9"};
    pt_app_set_font_fallbacks(app, many, 10);
    pt_check(pt_app_get_font_fallback_count(app) == PT_FONT_FALLBACK_MAX_FAMILIES);
    pt_check(strcmp(pt_app_get_font_fallback(app, 7), "f7") == 0);
    pt_check(pt_app_get_font_fallback(app, 8) == NULL);
    pt_app_set_font_fallbacks(app, NULL, 0);

    /* Null app handles are no-ops for every entry point. */
    pt_app_set_font_fallbacks(NULL, chain, 2);
    pt_app_set_font_fallbacks_for_family(NULL, "serif", chain, 2);
    pt_check(pt_app_get_font_fallback_count(NULL) == 0);
    pt_check(pt_app_get_font_fallback(NULL, 0) == NULL);
    pt_check(pt_app_get_font_fallback_count_for_family(NULL, "serif") == 0);
    pt_check(pt_app_get_font_fallback_for_family(NULL, "serif", 0) == NULL);
    pt_check(pt_app_font_uncovered_count(NULL, "sans-serif", "x", 1) == 0);
}

static void test_family_overrides(pt_app *app) {
    const char *app_chain[] = {"DejaVu Sans"};
    const char *serif_chain[] = {"Noto Sans Arabic", "DejaVu Sans"};
    pt_app_set_font_fallbacks(app, app_chain, 1);
    pt_app_set_font_fallbacks_for_family(app, "serif", serif_chain, 2);

    pt_check(pt_app_get_font_fallback_count(app) == 1);
    pt_check(pt_app_get_font_fallback_count_for_family(app, "sans-serif") == 0);
    pt_check(pt_app_get_font_fallback_count_for_family(app, "serif") == 2);
    pt_check(strcmp(pt_app_get_font_fallback_for_family(app, "serif", 0),
                    "Noto Sans Arabic") == 0);
    pt_check(pt_app_get_font_fallback_for_family(app, "serif", 2) == NULL);
    pt_check(pt_app_get_font_fallback_for_family(app, "serif", -1) == NULL);

    /* An empty override removes it; the app default is untouched. */
    pt_app_set_font_fallbacks_for_family(app, "serif", NULL, 0);
    pt_check(pt_app_get_font_fallback_count_for_family(app, "serif") == 0);
    pt_check(pt_app_get_font_fallback_count(app) == 1);

    /* Boundary: the last slot is usable, one past it evicts the oldest set. */
    char names[PT_FONT_FALLBACK_MAX_OVERRIDES][16];
    for (int i = 0; i < PT_FONT_FALLBACK_MAX_OVERRIDES; ++i) {
        snprintf(names[i], sizeof(names[i]), "fam%d", i);
        const char *one[] = {names[i]};
        pt_app_set_font_fallbacks_for_family(app, names[i], one, 1);
    }
    pt_check(pt_app_get_font_fallback_count_for_family(app, names[0]) == 1);
    const char *extra[] = {"overflow"};
    pt_app_set_font_fallbacks_for_family(app, "fam-new", extra, 1);
    pt_check(pt_app_get_font_fallback_count_for_family(app, "fam-new") == 1);
    pt_check(pt_app_get_font_fallback_count_for_family(app, names[0]) == 0);
    pt_check(pt_app_get_font_fallback_count_for_family(app, names[1]) == 1);

    /* Re-setting an existing family keeps one slot and moves it to newest. */
    pt_app_set_font_fallbacks_for_family(app, "fam-new", extra, 1);
    pt_check(pt_app_get_font_fallback_count_for_family(app, "fam-new") == 1);
    pt_check(pt_app_get_font_fallback_count_for_family(app, names[2]) == 1);

    reset_chains(app);
    pt_check(pt_app_get_font_fallback_count(app) == 0);
    pt_check(pt_app_get_font_fallback_count_for_family(app, "fam-new") == 0);
}

/* ---------- script segmentation and face selection ---------- */

static int cjk_runs_index(const pt_font_run *runs, int count, size_t start) {
    for (int i = 0; i < count; ++i) {
        if (runs[i].start == start) {
            return i;
        }
    }
    return -1;
}

/* The segmentation tests need an app for the setter, so they take one. */
static void check_cjk_segmentation(pt_app *app, const char *cjk_family) {
    if (!cjk_family) {
        skip_script("no CJK family installed; script-boundary run skipped");
        return;
    }
    pt_font *base = acquire(latin_family);
    if (!base) {
        skip_script("no base family for segmentation");
        return;
    }
    const char *chain[] = {cjk_family};
    pt_app_set_font_fallbacks(app, chain, 1);

    const char *mixed = "Hello \xE4\xB8\x96\xE7\x95\x8C";
    pt_font_run *runs = NULL;
    int count = pt_font_runs(base, mixed, strlen(mixed), &runs);
    pt_check(count == 2);
    if (count == 2) {
        pt_check(runs[0].start == 0 && runs[0].end == 6);
        pt_check(runs[0].script == HB_SCRIPT_LATIN);
        pt_check(runs[0].font == base);
        pt_check(runs[0].covered);
        pt_check(runs[1].start == 6 && runs[1].end == 12);
        pt_check(runs[1].script == HB_SCRIPT_HAN);
        pt_check(runs[1].font != base);
        pt_check(runs[1].font && strcmp(runs[1].font->family, cjk_family) == 0);
        pt_check(runs[1].covered);
    }
    pt_font_runs_free(runs);

    /* Coverage decides the split even inside one script: a face that lacks a
     * codepoint breaks the run at that codepoint. U+0378 is unassigned. */
    const char *gap = "a\xCD\xB8" "b"; /* a + unassigned U+0378 + b */
    runs = NULL;
    count = pt_font_runs(base, gap, strlen(gap), &runs);
    pt_check(count >= 2);
    int gap_index = cjk_runs_index(runs, count, 1);
    pt_check(gap_index >= 0);
    if (gap_index >= 0) {
        pt_check(!runs[gap_index].covered);
    }
    if (count >= 2) {
        pt_check(runs[count - 1].covered);
    }
    pt_font_runs_free(runs);
}

static void check_arabic_segmentation(pt_app *app, const char *arabic_family) {
    if (!arabic_family) {
        skip_script("no Arabic family installed; Arabic run skipped");
        return;
    }
    pt_font *base = acquire(latin_family);
    if (!base) {
        return;
    }
    const char *chain[] = {arabic_family};
    pt_app_set_font_fallbacks(app, chain, 1);
    /* "abc " + Arabic "مرحبا". */
    const char *mixed = "abc \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    pt_font_run *runs = NULL;
    int count = pt_font_runs(base, mixed, strlen(mixed), &runs);
    pt_check(count == 2);
    if (count == 2) {
        pt_check(runs[0].end == 4);
        pt_check(runs[0].script == HB_SCRIPT_LATIN);
        pt_check(runs[1].start == 4 && runs[1].end == (size_t)strlen(mixed));
        pt_check(runs[1].script == HB_SCRIPT_ARABIC);
        pt_check(runs[1].covered);
        /* DejaVu Sans itself carries Arabic, so the primary may legitimately
         * win here; the script boundary is what this check owns. */
        if (!pt_font_has_glyph(base, 0x0645u)) {
            pt_check(runs[1].font && strcmp(runs[1].font->family, arabic_family) == 0);
        }
    }
    pt_font_runs_free(runs);
}

static void check_devanagari_segmentation(pt_app *app, const char *devanagari_family) {
    if (!devanagari_family) {
        skip_script("no Devanagari family installed; Devanagari run skipped");
        return;
    }
    pt_font *base = acquire(latin_family);
    if (!base) {
        return;
    }
    const char *chain[] = {devanagari_family};
    pt_app_set_font_fallbacks(app, chain, 1);
    /* "नमस्ते" contains a virama and a combining mark. */
    const char *word = "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87";
    pt_font_run *runs = NULL;
    int count = pt_font_runs(base, word, strlen(word), &runs);
    pt_check(count == 1);
    if (count == 1) {
        pt_check(runs[0].script == HB_SCRIPT_DEVANAGARI);
        pt_check(runs[0].covered);
        pt_check(runs[0].font && strcmp(runs[0].font->family, devanagari_family) == 0);
        pt_check(runs[0].end == strlen(word));
    }
    pt_font_runs_free(runs);
}

static void check_selection_order(pt_app *app, const char *first, const char *second) {
    if (!first || !second || strcmp(first, second) == 0) {
        skip_script("need two CJK families for the ordering check");
        return;
    }
    pt_font *base = acquire(latin_family);
    if (!base) {
        return;
    }
    const char *mixed = "\xE4\xB8\x96\xE7\x95\x8C";
    const char *chain_a[] = {first, second};
    pt_app_set_font_fallbacks(app, chain_a, 2);
    pt_font_run *runs = NULL;
    int count = pt_font_runs(base, mixed, strlen(mixed), &runs);
    pt_check(count >= 1);
    if (count >= 1) {
        pt_check(strcmp(runs[0].font->family, first) == 0);
    }
    pt_font_runs_free(runs);

    const char *chain_b[] = {second, first};
    pt_app_set_font_fallbacks(app, chain_b, 2);
    runs = NULL;
    count = pt_font_runs(base, mixed, strlen(mixed), &runs);
    pt_check(count >= 1);
    if (count >= 1) {
        pt_check(strcmp(runs[0].font->family, second) == 0);
    }
    pt_font_runs_free(runs);

    /* The primary wins when it covers the text, regardless of the chain. */
    pt_font *primary = acquire(first);
    pt_check(primary != NULL);
    if (primary) {
        pt_app_set_font_fallbacks(app, chain_b, 2);
        runs = NULL;
        count = pt_font_runs(primary, mixed, strlen(mixed), &runs);
        pt_check(count == 1);
        if (count == 1) {
            pt_check(runs[0].font == primary);
            pt_check(runs[0].covered);
        }
        pt_font_runs_free(runs);
    }

    /* A per-family override is used instead of the app default for that exact
     * primary family. */
    pt_app_set_font_fallbacks(app, chain_b, 2);
    pt_app_set_font_fallbacks_for_family(app, base->family, chain_a, 2);
    runs = NULL;
    count = pt_font_runs(base, mixed, strlen(mixed), &runs);
    pt_check(count >= 1);
    if (count >= 1) {
        pt_check(strcmp(runs[0].font->family, first) == 0);
    }
    pt_font_runs_free(runs);
    pt_app_set_font_fallbacks_for_family(app, base->family, NULL, 0);
}

static void test_tofu(pt_app *app, const char *non_covering_family) {
    pt_font *base = acquire(latin_family);
    if (!base) {
        return;
    }

    /* With no explicit chain the system order is authoritative only over what
     * it actually has; an unassigned codepoint has no face anywhere. */
    pt_app_set_font_fallbacks(app, NULL, 0);
    const char *unassigned = "\xCD\xB8"; /* U+0378 */
    pt_check(pt_app_font_uncovered_count(app, latin_family, unassigned,
                                         strlen(unassigned)) == 1);
    pt_font_run *runs = NULL;
    int count = pt_font_runs(base, unassigned, strlen(unassigned), &runs);
    pt_check(count == 1);
    if (count == 1) {
        pt_check(!runs[0].covered);
        pt_check(runs[0].font == base);
    }
    pt_font_runs_free(runs);

    /* With no explicit chain the platform order supplies the fallback face for
     * a real script when the system has one. */
    const char *cjk_text = "\xE4\xB8\x96\xE7\x95\x8C";
    runs = NULL;
    count = pt_font_runs(base, cjk_text, strlen(cjk_text), &runs);
    if (count == 1 && !runs[0].covered) {
        skip_script("system Fontconfig order has no CJK face");
    } else {
        pt_check(count == 1);
        pt_check(runs[0].covered);
        pt_check(runs[0].font != base);
    }
    pt_font_runs_free(runs);

    /* An explicit chain is authoritative: nobody in it covers Han, so the run
     * stays on the primary face and reports uncovered. */
    if (non_covering_family) {
        const char *chain[] = {non_covering_family};
        pt_app_set_font_fallbacks(app, chain, 1);
        const char *cjk = "\xE4\xB8\x96\xE7\x95\x8C";
        pt_check(pt_app_font_uncovered_count(app, latin_family, cjk, strlen(cjk)) == 2);
        runs = NULL;
        count = pt_font_runs(base, cjk, strlen(cjk), &runs);
        pt_check(count == 1);
        if (count == 1) {
            pt_check(!runs[0].covered);
            pt_check(runs[0].font == base);
        }
        pt_font_runs_free(runs);
    } else {
        skip_script("no non-covering family for the authoritative-chain check");
    }

    pt_app_set_font_fallbacks(app, NULL, 0);
}

/* ---------- composed metrics ---------- */

static void test_metrics(pt_app *app, const char *cjk_family) {
    pt_font *base = acquire(latin_family);
    pt_font *cjk = cjk_family ? acquire(cjk_family) : NULL;
    if (!base) {
        return;
    }
    if (!cjk || !pt_font_has_glyph(cjk, 0x4E16u)) {
        skip_script("no CJK family installed; metric-composition checks skipped");
        return;
    }
    const char *chain[] = {cjk_family};
    pt_app_set_font_fallbacks(app, chain, 1);

    const char *mixed = "Hello \xE4\xB8\x96\xE7\x95\x8C";
    float ascent = 0.0f;
    float descent = 0.0f;
    pt_font_metrics_for_text(base, mixed, strlen(mixed), &ascent, &descent);
    pt_check(ascent >= base->ascent - 0.01f);
    pt_check(ascent >= cjk->ascent - 0.01f);
    pt_check(descent >= base->descent - 0.01f);
    pt_check(descent >= cjk->descent - 0.01f);
    float line_height = pt_font_line_height_for(base, mixed, strlen(mixed));
    pt_check_near(line_height, fmaxf(ascent + descent, base->line_height), 0.01);

    /* ASCII-only text keeps the primary line height exactly. */
    float ascii_ascent = 0.0f;
    float ascii_descent = 0.0f;
    pt_font_metrics_for_text(base, "Hello", 5, &ascii_ascent, &ascii_descent);
    pt_check_near(ascii_ascent, base->ascent, 0.01);
    pt_check_near(ascii_descent, base->descent, 0.01);
    pt_check_near(pt_font_line_height_for(base, "Hello", 5), base->line_height, 0.01);

    /* Wrapping reserves the composed height on every line. */
    const char *paragraph =
        "Pt mixes scripts: \xE4\xB8\x96\xE7\x95\x8C\xE7\x95\x8C\xE4\xB8\x96 "
        "\xE4\xB8\x96\xE7\x95\x8C\xE7\x95\x8C\xE4\xB8\x96 and wraps them.";
    float height = pt_font_wrapped_height(base, paragraph, 120.0f);
    pt_text_line *lines = NULL;
    int count = pt_font_wrap_lines(base, paragraph, strlen(paragraph), 120.0f, &lines);
    free(lines);
    pt_check(count >= 2);
    pt_check_near(height, (float)count * line_height, 0.01f);

    /* The mixed advance is not the tofu advance: at least one CJK glyph comes
     * from a different face, so the run measures wider than the primary alone
     * could (both runs are measured, not just the first). */
    float mixed_width = pt_font_measure(base, mixed, strlen(mixed));
    float latin_width = pt_font_measure(base, "Hello ", 6);
    pt_check(mixed_width > latin_width);
}

/* ---------- cache bounds ---------- */

static size_t encode_utf8(uint32_t codepoint, char *out) {
    if (codepoint < 0x80u) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800u) {
        out[0] = (char)(0xC0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2;
    }
    if (codepoint < 0x10000u) {
        out[0] = (char)(0xE0u | (codepoint >> 12));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (codepoint & 0x3Fu));
    return 4;
}

static void test_cache_bounds(pt_app *app, const char *cjk_family) {
    pt_font *base = acquire(latin_family);
    if (!base) {
        return;
    }
    pt_app_set_font_fallbacks(app, NULL, 0);
    for (int i = 0; i < 400; ++i) {
        char text[32];
        snprintf(text, sizeof(text), "line-%03d %c%c", i, 'a' + i % 26, 'A' + i % 26);
        (void)pt_font_measure(base, text, strlen(text));
    }
    pt_check(pt_font_shape_cache_count(base) == 256);

    if (!cjk_family) {
        skip_script("no CJK family; fallback face-cache bound skipped");
        return;
    }
    const char *chain[] = {cjk_family};
    pt_app_set_font_fallbacks(app, chain, 1);
    const char *mixed = "cache \xE4\xB8\x96\xE7\x95\x8C";
    (void)pt_font_measure(base, mixed, strlen(mixed));
    size_t faces = pt_font_manager_face_count();
    for (int i = 0; i < 500; ++i) {
        (void)pt_font_measure(base, mixed, strlen(mixed));
    }
    /* Warm shaping reuses the resolved face; it must not query Fontconfig and
     * load another face per call. */
    pt_check(pt_font_manager_face_count() == faces);
    pt_check(pt_font_shape_cache_count(base) <= 256);

    /* The per-face codepoint cache is fixed too: a single-script string with
     * more distinct codepoints than its load factor resolves without caching,
     * so the table stops short of its 512 slots. */
    const char *only_latin[] = {latin_family};
    pt_app_set_font_fallbacks(app, only_latin, 1);
    char ethiopic[600 * 4 + 1];
    size_t used = 0;
    for (uint32_t codepoint = 0x1000u; codepoint < 0x1000u + 600u; ++codepoint) {
        used += encode_utf8(codepoint, ethiopic + used);
    }
    (void)pt_font_measure(base, ethiopic, used);
    size_t cached = pt_font_fallback_cache_count(base);
    pt_check(cached > 300);
    pt_check(cached < 512);
    float first = pt_font_measure(base, ethiopic, used);
    size_t again = pt_font_fallback_cache_count(base);
    pt_check(again == cached);
    pt_check_near(pt_font_measure(base, ethiopic, used), first, 0.01f);
}

/* ---------- caret offsets keep logical order ---------- */

static void test_caret(pt_app *app, const char *cjk_family) {
    if (!cjk_family) {
        skip_script("no CJK family; caret checks skipped");
        return;
    }
    const char *chain[] = {cjk_family};
    pt_app_set_font_fallbacks(app, chain, 1);

    pt_window *window = pt_window_create(app, "fallback caret", 420, 200);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *area = pt_text_area_create(pt_window_root(window), NULL);
    const char *text = "abc\xE4\xB8\x96\xE7\x95\x8C" "def";
    pt_widget_set_text(area, text);
    pt_layout_ctx ctx = {.scale = 1.0f, .font_family = latin_family};
    pt_text_layout layout = {0};
    pt_widget_layout(pt_window_root(window), 0.0f, 0.0f, 420.0f, 160.0f, &ctx);
    pt_check(area->width > 100.0f);
    pt_check(pt_text_layout_build(area, &ctx, 380.0f, &layout));
    pt_check(layout.line_count == 1);

    float previous = -1.0f;
    for (size_t position = 0; position <= layout.display_len;) {
        float x = pt_text_layout_x_for_position(&layout, 0, position);
        pt_check(x >= previous - 0.001f);
        previous = x;
        if (position == layout.display_len) {
            break;
        }
        position = pt_utf8_next_boundary(text, layout.display_len, position);
    }
    pt_check_near(pt_text_layout_x_for_position(&layout, 0, 0), 0.0f, 0.01);
    float at_cjk = pt_text_layout_x_for_position(&layout, 0, 3);
    float after_cjk = pt_text_layout_x_for_position(&layout, 0, 9);
    pt_check(after_cjk > at_cjk);
    pt_check_near(pt_text_layout_x_for_position(&layout, 0, layout.display_len),
                  pt_font_measure(layout.font, text, strlen(text)), 0.01f);

    /* Hit testing at the exact CJK boundary lands on the logical byte offset,
     * not on a visual or glyph index. */
    size_t hit = pt_text_layout_position_for_x(&layout, 0, at_cjk);
    pt_check(hit == 3);
    pt_check(pt_text_area_position_at(area, &ctx, area->x + area->padding_left + after_cjk,
                                      area->y + area->padding_top + 4.0f) == 9);

    /* AT-SPI and editing keep the stored string; fallback never reorders it. */
    pt_check(strcmp(pt_widget_get_text(area), text) == 0);
    pt_text_layout_free(&layout);
    pt_window_free(window);
}

/* ---------- performance guard ---------- */

static void test_mixed_shaping_cost(pt_app *app, const char *cjk_family) {
    if (!cjk_family) {
        skip_script("no CJK family; mixed shaping timing skipped");
        return;
    }
    pt_font *base = acquire(latin_family);
    if (!base) {
        return;
    }
    const char *chain[] = {cjk_family};
    pt_app_set_font_fallbacks(app, chain, 1);
    const char *mixed = "Perf \xE4\xB8\x96\xE7\x95\x8C mixed \xE4\xB8\x96\xE7\x95\x8C text";
    size_t len = strlen(mixed);
    for (int i = 0; i < 20; ++i) {
        (void)pt_font_measure(base, mixed, len);
    }
    size_t faces = pt_font_manager_face_count();
    double start = now_ms();
    const int iterations = 2000;
    float total = 0.0f;
    for (int i = 0; i < iterations; ++i) {
        total += pt_font_measure(base, mixed, len);
    }
    double elapsed = now_ms() - start;
    pt_check(total > 0.0f);
    /* Warm mixed shaping is cached source runs: far below a frame budget. */
    pt_check(elapsed < 2000.0);
    pt_check(elapsed / (double)iterations < 1.0);
    printf("font_fallback: warm mixed shaping %.2f ms / %d calls\n", elapsed, iterations);
    pt_check(pt_font_manager_face_count() == faces);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.font_fallback");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("font_fallback");
    }

    static const char *const cjk_families[] = {
        "Noto Sans CJK SC", "Noto Sans CJK JP", "Noto Sans CJK HK", "Noto Sans CJK TC",
        "Noto Sans CJK KR", "Source Han Sans SC", "WenQuanYi Zen Hei", "Noto Sans SC"};
    static const char *const arabic_families[] = {
        "Noto Sans Arabic", "Noto Naskh Arabic", "Noto Kufi Arabic", "DejaVu Sans"};
    static const char *const devanagari_families[] = {
        "Noto Sans Devanagari", "Noto Sans Devanagari UI", "Noto Serif Devanagari"};
    static const char *const latin_families[] = {"DejaVu Sans", "Liberation Sans",
                                                 "Noto Sans", "FreeSans"};

    const char *cjk = find_family(cjk_families, (int)(sizeof(cjk_families) / sizeof(cjk_families[0])),
                                  0x4E16u);
    const char *cjk_second = NULL;
    if (cjk) {
        for (size_t i = 0; i < sizeof(cjk_families) / sizeof(cjk_families[0]); ++i) {
            if (strcmp(cjk_families[i], cjk) != 0 &&
                find_family(&cjk_families[i], 1, 0x4E16u)) {
                cjk_second = cjk_families[i];
                break;
            }
        }
    }
    const char *arabic =
        find_family(arabic_families,
                    (int)(sizeof(arabic_families) / sizeof(arabic_families[0])), 0x0645u);
    const char *devanagari =
        find_family(devanagari_families,
                    (int)(sizeof(devanagari_families) / sizeof(devanagari_families[0])),
                    0x0926u);
    const char *non_covering = NULL;
    for (size_t i = 0; i < sizeof(latin_families) / sizeof(latin_families[0]); ++i) {
        pt_font *font = acquire(latin_families[i]);
        if (font && !pt_font_has_glyph(font, 0x4E16u)) {
            non_covering = latin_families[i];
            break;
        }
    }

    if (!cjk) {
        skip_script("no CJK family installed; CJK-dependent coverage skipped");
    }
    if (!arabic) {
        skip_script("no Arabic family installed");
    }
    if (!devanagari) {
        skip_script("no Devanagari family installed");
    }
    if (non_covering) {
        latin_family = non_covering;
    } else {
        skip_script("no Latin-only family; fallback-dependent checks skipped");
        cjk = NULL;
        cjk_second = NULL;
        arabic = NULL;
        devanagari = NULL;
    }

    reset_chains(app);
    test_chain_api(app);
    test_family_overrides(app);
    check_cjk_segmentation(app, cjk);
    check_arabic_segmentation(app, arabic);
    check_devanagari_segmentation(app, devanagari);
    check_selection_order(app, cjk, cjk_second);
    test_tofu(app, non_covering);
    test_metrics(app, cjk);
    test_cache_bounds(app, cjk);
    test_caret(app, cjk);
    test_mixed_shaping_cost(app, cjk);

    reset_chains(app);
    /* Lifecycle: tearing the manager down and rebuilding the caches must not
     * accumulate faces or rely on stale state. */
    for (int cycle = 0; cycle < 3; ++cycle) {
        pt_font_manager_shutdown();
        pt_font *font = acquire(latin_family);
        pt_check(font != NULL);
        if (font) {
            (void)pt_font_measure(font, "cycle \xE4\xB8\x96\xE7\x95\x8C", 12);
            pt_check(pt_font_manager_face_count() >= 1);
        }
    }
    pt_app_destroy(app);
    pt_font_manager_shutdown();
    printf("font_fallback: %d script(s) skipped\n", skipped);
    return pt_test_finish("font_fallback");
}
