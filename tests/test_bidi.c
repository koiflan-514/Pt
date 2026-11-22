/*
 * UBA levels, reordering and single-line shaping (2.0 wave 2).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The engine conformance suites stay in test_bidi_conformance; this file is the
 * Pt-layer contract: byte maps through the paragraph/line adapter, X9 controls
 * kept for shaping but skipped when painted, explicit per-run directions,
 * visual run order, HarfBuzz bracket mirroring and Arabic joining.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int skipped;

static void skip_fonts(const char *what) {
    ++skipped;
    printf("skip bidi: %s\n", what);
}

static size_t run_bytes(const pt_bidi_line *line) {
    size_t total = 0;
    for (size_t i = 0; i < line->run_count; ++i) {
        total += line->runs[i].length;
    }
    return total;
}

static uint64_t pixels_hash(const pt_canvas *canvas) {
    uint64_t hash = 1469598103934665603ull;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) {
        hash ^= canvas->pixels[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/* "abc " + alef bet gimel */
static const char mixed_ltr[] = "abc \xD7\x90\xD7\x91\xD7\x92";
/* alef bet gimel + " abc" */
static const char mixed_rtl[] = "\xD7\x90\xD7\x91\xD7\x92 abc";
static const char hebrew[] = "\xD7\x90\xD7\x91\xD7\x92";
static const char arabic[] = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";

static void test_paragraph_basics(void) {
    pt_bidi_paragraph *paragraph = NULL;
    pt_check(pt_bidi_paragraph_analyze("abc", 3, pt_bidi_base_auto, &paragraph));
    pt_check(paragraph != NULL && pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_check(pt_bidi_paragraph_length(paragraph) == 3);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    pt_check(pt_bidi_paragraph_analyze(hebrew, strlen(hebrew), pt_bidi_base_auto, &paragraph));
    pt_check(paragraph != NULL && pt_bidi_paragraph_base_level(paragraph) == 1);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    /* Explicit bases bypass P2/P3. */
    pt_check(pt_bidi_paragraph_analyze(hebrew, strlen(hebrew), pt_bidi_base_ltr, &paragraph));
    pt_check(paragraph != NULL && pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    /* Neutral-only text and empty input stay safe. */
    pt_check(pt_bidi_paragraph_analyze("123 :", 5, pt_bidi_base_auto, &paragraph));
    pt_check(paragraph != NULL && pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    pt_check(pt_bidi_paragraph_analyze("", 0, pt_bidi_base_auto, &paragraph));
    pt_check(paragraph != NULL && pt_bidi_paragraph_length(paragraph) == 0);
    pt_check(pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_bidi_line empty = {0};
    pt_check(pt_bidi_line_create(paragraph, 0, 0, &empty));
    pt_check(empty.run_count == 0);
    pt_bidi_line_free(&empty);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    /* NULL and invalid direction requests fail without owning anything. */
    pt_check(!pt_bidi_paragraph_analyze(NULL, 1, pt_bidi_base_auto, &paragraph));
    pt_check(paragraph == NULL);
    pt_check(!pt_bidi_paragraph_analyze("a", 1, (pt_bidi_base)99, &paragraph));
    pt_check(paragraph == NULL);

    /* Fast-path pre-scan. */
    pt_check(!pt_bidi_text_may_reorder("Hello, Pt", 9));
    pt_check(pt_bidi_text_may_reorder(hebrew, strlen(hebrew)));
    pt_check(pt_bidi_text_may_reorder(arabic, strlen(arabic)));
    pt_check(pt_bidi_text_may_reorder("a\xE2\x80\xAA" "b", 5)); /* LRE */
    pt_check(!pt_bidi_text_may_reorder("a\xE2\x80\x8D" "b", 5)); /* ZWJ is BN */
    pt_check(pt_bidi_first_strong_direction("abc", 3) == pt_bidi_base_ltr);
    pt_check(pt_bidi_first_strong_direction(hebrew, strlen(hebrew)) == pt_bidi_base_rtl);
    pt_check(pt_bidi_first_strong_direction("123 :", 5) == pt_bidi_base_auto);
}

static void test_byte_maps(void) {
    size_t len = strlen(mixed_ltr);
    pt_bidi_paragraph *paragraph = NULL;
    pt_check(pt_bidi_paragraph_analyze(mixed_ltr, len, pt_bidi_base_ltr, &paragraph));
    pt_bidi_line line = {0};
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(line.base_level == 0);
    pt_check(line.run_count == 2);
    pt_check(run_bytes(&line) == len);
    /* UTF-8 offsets: Latin "abc " is four bytes, each Hebrew letter two. */
    pt_check(line.runs[0].offset == 0 && line.runs[0].length == 4);
    pt_check(line.runs[0].level == 0);
    pt_check(line.runs[1].offset == 4 && line.runs[1].length == 6);
    pt_check(line.runs[1].level == 1);
    pt_check(line.runs[0].visual_index == 0 && line.runs[1].visual_index == 1);
    pt_check(pt_bidi_line_level_at(&line, 0) == 0);
    pt_check(pt_bidi_line_level_at(&line, 4) == 1);
    pt_check(pt_bidi_line_level_at(&line, 9) == 1);
    pt_check(pt_bidi_line_level_at(&line, len) == -1);
    pt_bidi_line_free(&line);

    /* A range that starts inside a code point is rejected, not rounded. */
    pt_bidi_line bogus = {0};
    pt_bidi_paragraph *hebrew_paragraph = NULL;
    pt_check(pt_bidi_paragraph_analyze(hebrew, strlen(hebrew), pt_bidi_base_rtl,
                                       &hebrew_paragraph));
    pt_check(!pt_bidi_line_create(hebrew_paragraph, 1, 2, &bogus));
    pt_bidi_paragraph_free(hebrew_paragraph);
    pt_check(!pt_bidi_line_create(paragraph, 0, len + 1, &bogus));
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    /* RTL base reorders the two runs: the Latin island is visually left. */
    len = strlen(mixed_rtl);
    pt_check(pt_bidi_paragraph_analyze(mixed_rtl, len, pt_bidi_base_rtl, &paragraph));
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(line.base_level == 1);
    pt_check(line.run_count == 2);
    pt_check(run_bytes(&line) == len);
    pt_check(line.runs[0].offset == 7 && line.runs[0].level == 2);
    pt_check(line.runs[1].offset == 0 && line.runs[1].level == 1);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    /* Pure right-to-left text is one logical run at level 1. */
    len = strlen(hebrew);
    pt_check(pt_bidi_paragraph_analyze(hebrew, len, pt_bidi_base_auto, &paragraph));
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(line.base_level == 1 && line.run_count == 1);
    pt_check(line.runs[0].offset == 0 && line.runs[0].length == len);
    pt_check(line.runs[0].level == 1);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);
}

static void test_controls_and_combining(void) {
    /* X9: LRE/PDF are removed from the visual order but keep their bytes for
     * shaping and for the logical/byte map. */
    const char *text = "a\xE2\x80\xAA" "b"; /* a LRE b */
    size_t len = strlen(text);
    pt_check(len == 5);
    pt_check(pt_bidi_is_removed(0x202Au)); /* LRE */
    pt_check(pt_bidi_is_removed(0x202Bu)); /* RLE */
    pt_check(pt_bidi_is_removed(0x202Cu)); /* PDF */
    pt_check(pt_bidi_is_removed(0x202Du)); /* LRO */
    pt_check(pt_bidi_is_removed(0x202Eu)); /* RLO */
    pt_check(pt_bidi_is_removed(0x200Bu)); /* ZWSP, bidi class BN */
    pt_check(pt_bidi_is_removed(0x00ADu)); /* soft hyphen, BN */
    pt_check(!pt_bidi_is_removed('a'));
    /* ZWJ is bidi class BN, so it is X9-removed for painting, but shaping keeps
     * its bytes so Arabic joining still sees it. */
    pt_check(pt_bidi_is_removed(0x200Du));
    pt_check(!pt_bidi_is_removed(0xFFFFFFFFu)); /* invalid safe */

    pt_bidi_paragraph *paragraph = NULL;
    pt_check(pt_bidi_paragraph_analyze(text, len, pt_bidi_base_ltr, &paragraph));
    pt_bidi_line line = {0};
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(run_bytes(&line) == len);
    pt_check(pt_bidi_line_level_at(&line, 1) >= 0);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);

    /* Combining marks stay inside the base letter's run and keep their bytes. */
    const char *composed = "e\xCC\x81"; /* e + combining acute */
    len = strlen(composed);
    pt_check(len == 3);
    pt_check(pt_bidi_paragraph_analyze(composed, len, pt_bidi_base_auto, &paragraph));
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(line.run_count == 1);
    pt_check(line.runs[0].offset == 0 && line.runs[0].length == 3);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);

    /* Isolate nesting: P2 skips the isolated Hebrew, so the trailing Latin
     * decides the base direction. The isolates remain present as controls. */
    const char *isolated = "a\xE2\x81\xA7" "\xD7\x90\xD7\x91\xD7\x92"
                           "\xE2\x81\xA9" "b"; /* a RLI hebrew PDI b */
    len = strlen(isolated);
    pt_check(pt_bidi_paragraph_analyze(isolated, len, pt_bidi_base_auto, &paragraph));
    pt_check(pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(run_bytes(&line) == len);
    pt_check(line.run_count >= 2);
    bool saw_odd = false;
    for (size_t i = 0; i < line.run_count; ++i) {
        if ((line.runs[i].level & 1u) != 0) saw_odd = true;
    }
    pt_check(saw_odd);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);

    /* Malformed UTF-8 is decoded as faulty code points, never out of bounds. */
    const char bad[] = {'a', (char)0xFF, 'b', (char)0xC3};
    pt_check(pt_bidi_paragraph_analyze(bad, sizeof(bad), pt_bidi_base_auto, &paragraph));
    pt_check(pt_bidi_line_create(paragraph, 0, sizeof(bad), &line));
    pt_check(run_bytes(&line) == sizeof(bad));
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);
}

static void test_font_pipeline(pt_font *font) {
    size_t len = strlen("Hello, Pt");
    pt_font_run *runs = NULL;
    /* LTR fast path: one level-0 run and the legacy direction sentinel. */
    int count = pt_font_runs_bidi(font, "Hello, Pt", len, pt_bidi_base_ltr, &runs);
    pt_check(count == 1);
    pt_check(runs && runs[0].level == 0 && runs[0].direction == HB_DIRECTION_INVALID);
    pt_check_near(pt_font_measure_bidi(font, "Hello, Pt", len, pt_bidi_base_ltr),
                  pt_font_measure(font, "Hello, Pt", len), 0.0f);
    pt_font_runs_free(runs);
    runs = NULL;

    count = pt_font_runs_bidi(font, "123 :", 5, pt_bidi_base_auto, &runs);
    pt_check(count == 1 && runs[0].direction == HB_DIRECTION_INVALID);
    pt_font_runs_free(runs);
    runs = NULL;

    /* Mixed LTR base: Latin then Hebrew, both explicit. */
    len = strlen(mixed_ltr);
    float mixed_width = pt_font_measure_bidi(font, mixed_ltr, len, pt_bidi_base_ltr);
    pt_check(mixed_width > 0.0f);
    count = pt_font_runs_bidi(font, mixed_ltr, len, pt_bidi_base_ltr, &runs);
    pt_check(count == 2);
    pt_check(runs[0].level == 0 && runs[0].direction == HB_DIRECTION_LTR);
    pt_check(runs[1].level == 1 && runs[1].direction == HB_DIRECTION_RTL);
    pt_check(runs[0].start == 0 && runs[0].end == 4);
    pt_check(runs[1].start == 4 && runs[1].end == len);
    pt_check(runs[1].font != NULL);
    pt_font_runs_free(runs);
    runs = NULL;

    /* RTL base: visual order starts with the even-level Latin island. */
    len = strlen(mixed_rtl);
    count = pt_font_runs_bidi(font, mixed_rtl, len, pt_bidi_base_rtl, &runs);
    pt_check(count == 2);
    pt_check(runs[0].level == 2 && runs[0].direction == HB_DIRECTION_LTR);
    pt_check(runs[1].level == 1 && runs[1].direction == HB_DIRECTION_RTL);
    pt_check(runs[0].start == 7 && runs[1].start == 0);
    pt_check(runs[0].visual_index == 0 && runs[1].visual_index == 1);
    pt_font_runs_free(runs);
    runs = NULL;

    /* Pure RTL and pure LTR. */
    len = strlen(hebrew);
    count = pt_font_runs_bidi(font, hebrew, len, pt_bidi_base_auto, &runs);
    pt_check(count == 1);
    pt_check(runs[0].level == 1 && runs[0].direction == HB_DIRECTION_RTL);
    pt_check(pt_font_measure_bidi(font, hebrew, len, pt_bidi_base_auto) > 0.0f);
    pt_font_runs_free(runs);
    runs = NULL;

    /* Empty and malformed input. */
    pt_check(pt_font_measure_bidi(font, "", 0, pt_bidi_base_auto) == 0.0f);
    pt_check(pt_font_runs_bidi(font, "", 0, pt_bidi_base_auto, &runs) == 0);
    pt_check(runs == NULL);
    const char bad[] = {'a', (char)0xFF, 'b'};
    pt_check(pt_font_measure_bidi(font, bad, sizeof(bad), pt_bidi_base_auto) >= 0.0f);
    float ascent = 0.0f;
    float descent = 0.0f;
    pt_font_metrics_for_text_bidi(font, mixed_ltr, strlen(mixed_ltr), pt_bidi_base_ltr,
                                  &ascent, &descent);
    pt_check(ascent > 0.0f);

    /* Arabic joining: the whole word shapes narrower than isolated letters. */
    len = strlen(arabic);
    float joined = pt_font_measure_bidi(font, arabic, len, pt_bidi_base_rtl);
    float separate = pt_font_measure_bidi(font, "\xD8\xB3", 2, pt_bidi_base_rtl) +
                     pt_font_measure_bidi(font, "\xD9\x84", 2, pt_bidi_base_rtl) +
                     pt_font_measure_bidi(font, "\xD8\xA7", 2, pt_bidi_base_rtl) +
                     pt_font_measure_bidi(font, "\xD9\x85", 2, pt_bidi_base_rtl);
    pt_check(joined > 0.0f);
    pt_check(joined + 1.0f < separate);
    count = pt_font_runs_bidi(font, arabic, len, pt_bidi_base_rtl, &runs);
    pt_check(count == 1);
    pt_check(runs[0].level == 1 && runs[0].direction == HB_DIRECTION_RTL);
    pt_font_runs_free(runs);
    runs = NULL;

    /* The cache keys the direction, so LTR and RTL shaping cannot alias. */
    const char *cache_text = "Cache \xD7\x90\xD7\x91\xD7\x92";
    len = strlen(cache_text);
    size_t before = pt_font_shape_cache_count(font);
    (void)pt_font_measure_bidi(font, cache_text, len, pt_bidi_base_ltr);
    size_t after_ltr = pt_font_shape_cache_count(font);
    (void)pt_font_measure_bidi(font, cache_text, len, pt_bidi_base_rtl);
    size_t after_rtl = pt_font_shape_cache_count(font);
    pt_check(after_ltr > before);
    pt_check(after_rtl > after_ltr);
    pt_check_near(pt_font_measure_bidi(font, cache_text, len, pt_bidi_base_ltr),
                  pt_font_measure_bidi(font, cache_text, len, pt_bidi_base_ltr), 0.0f);
}

static uint64_t draw_one(pt_canvas *canvas, pt_font *font, const char *text, pt_bidi_base base,
                         float x) {
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line_bidi(canvas, font, text, strlen(text), x, 50.0f, 0.0f,
                                  pt_text_align_start, 0xFFFFFFFFu, false, base);
    return pixels_hash(canvas);
}

static void test_rendering(pt_font *font) {
    pt_canvas *canvas = pt_canvas_create(320, 80, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) return;

    /* HB mirrors paired brackets from the explicit RTL direction: drawing "("
     * as RTL equals drawing ")" as LTR. Pt never pre-mirrors. */
    uint64_t open_ltr = draw_one(canvas, font, "(", pt_bidi_base_ltr, 20.0f);
    uint64_t open_rtl = draw_one(canvas, font, "(", pt_bidi_base_rtl, 20.0f);
    uint64_t close_ltr = draw_one(canvas, font, ")", pt_bidi_base_ltr, 20.0f);
    pt_check(open_ltr != open_rtl);
    pt_check(open_rtl == close_ltr);

    uint64_t bracket_ltr = draw_one(canvas, font, "[", pt_bidi_base_ltr, 20.0f);
    uint64_t bracket_rtl = draw_one(canvas, font, "[", pt_bidi_base_rtl, 20.0f);
    uint64_t bracket_close = draw_one(canvas, font, "]", pt_bidi_base_ltr, 20.0f);
    pt_check(bracket_ltr != bracket_rtl);
    pt_check(bracket_rtl == bracket_close);

    /* Visual order is real: the same bytes under an LTR and an RTL base paint
     * different frames because the Latin and Hebrew runs swap sides. */
    uint64_t mixed_ltr_hash = draw_one(canvas, font, mixed_ltr, pt_bidi_base_ltr, 10.0f);
    uint64_t mixed_rtl_hash = draw_one(canvas, font, mixed_ltr, pt_bidi_base_rtl, 10.0f);
    pt_check(mixed_ltr_hash != mixed_rtl_hash);

    /* A mixed line paints a meaningful number of pixels. */
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line_bidi(canvas, font, mixed_ltr, strlen(mixed_ltr), 10.0f, 50.0f,
                                  0.0f, pt_text_align_start, 0xFFFFFFFFu, false,
                                  pt_bidi_base_ltr);
    int painted = 0;
    for (int y = 0; y < canvas->height; ++y) {
        for (int x = 0; x < canvas->width; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != 0x000000FFu) ++painted;
        }
    }
    pt_check(painted > 100);

    /* Removed controls keep their bytes but add no paint width. */
    const char *control_text = "a\xE2\x80\xAA" "b";
    float with_control =
        pt_font_measure_bidi(font, control_text, strlen(control_text), pt_bidi_base_ltr);
    pt_check_near(with_control, pt_font_measure(font, "ab", 2), 0.001f);

    pt_canvas_destroy(canvas);
}

int main(void) {
    pt_bidi_base base = pt_bidi_base_auto;

    test_paragraph_basics();
    test_byte_maps();
    test_controls_and_combining();

    pt_font *font = pt_font_acquire("DejaVu Sans", pt_font_weight_regular,
                                    pt_font_slant_upright, 24.0f);
    if (!font || !pt_font_has_glyph(font, 0x0627u) || !pt_font_has_glyph(font, 0x05D0u)) {
        skip_fonts("DejaVu Sans with Arabic and Hebrew is not installed");
    } else {
        test_font_pipeline(font);
        test_rendering(font);
    }

    /* Neutral fallback keeps the API surface honest even without the font. */
    pt_check(pt_font_measure_bidi(NULL, "x", 1, base) == 0.0f);

    pt_font_manager_shutdown();
    printf("bidi: %d font-dependent test(s) skipped\n", skipped);
    return pt_test_finish("bidi");
}