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
    paragraph = NULL;

    /* Nested isolates: both isolated runs are skipped by P2 and their control
     * bytes survive for shaping/byte maps. */
    const char *nested = "a\xE2\x81\xA7\xE2\x81\xA6" "\xD7\x90" "\xE2\x81\xA9"
                         "\xE2\x81\xA7" "\xD7\x91" "\xE2\x81\xA9\xE2\x81\xA9" "b";
    len = strlen(nested);
    pt_check(pt_bidi_text_may_reorder(nested, len));
    pt_check(pt_bidi_paragraph_analyze(nested, len, pt_bidi_base_auto, &paragraph));
    pt_check(pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_check(pt_bidi_line_create(paragraph, 0, len, &line));
    pt_check(run_bytes(&line) == len);
    pt_check(line.run_count >= 2);
    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

    /* Paragraph splitting: the adapter analyses the first paragraph only, so a
     * later Hebrew paragraph cannot flip an LTR first paragraph's base. */
    const char *paragraphs = "abc\n\xD7\x90\xD7\x91";
    len = strlen(paragraphs);
    pt_check(pt_bidi_paragraph_analyze(paragraphs, len, pt_bidi_base_auto, &paragraph));
    pt_check(pt_bidi_paragraph_base_level(paragraph) == 0);
    pt_check(pt_bidi_paragraph_length(paragraph) <= 4);
    pt_bidi_paragraph_free(paragraph);
    paragraph = NULL;

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

    /* Sub-runs of one odd-level UBA run are visual: "abc " + alef bet gimel
     * under an RTL base puts the Hebrew run, then the Common space, then the
     * even-level Latin island. The space splits from Hebrew because it starts
     * the UBA run and inherits no preceding script. */
    len = strlen(mixed_ltr);
    count = pt_font_runs_bidi(font, mixed_ltr, len, pt_bidi_base_rtl, &runs);
    pt_check(count == 3);
    pt_check(runs && runs[0].start == 4 && runs[0].end == 10 && runs[0].level == 1 &&
             runs[0].direction == HB_DIRECTION_RTL);
    pt_check(runs && runs[1].start == 3 && runs[1].end == 4 && runs[1].level == 1 &&
             runs[1].direction == HB_DIRECTION_RTL);
    pt_check(runs && runs[2].start == 0 && runs[2].end == 3 && runs[2].level == 2 &&
             runs[2].direction == HB_DIRECTION_LTR);
    pt_font_runs_free(runs);
    runs = NULL;

    /* The same bytes under an LTR base keep the RTL run single and last. */
    count = pt_font_runs_bidi(font, mixed_ltr, len, pt_bidi_base_ltr, &runs);
    pt_check(count == 2);
    pt_check(runs && runs[0].start == 0 && runs[0].end == 4 && runs[0].level == 0);
    pt_check(runs && runs[1].start == 4 && runs[1].end == 10 && runs[1].level == 1 &&
             runs[1].direction == HB_DIRECTION_RTL);
    pt_font_runs_free(runs);
    runs = NULL;

    /* Same mechanism with Arabic: the Arabic word plus the leading Common
     * space are one odd-level run and are emitted word-first. */
    const char *mixed_arabic = "abc " "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    len = strlen(mixed_arabic);
    count = pt_font_runs_bidi(font, mixed_arabic, len, pt_bidi_base_rtl, &runs);
    pt_check(count == 3);
    pt_check(runs && runs[0].start == 4 && runs[0].end == len && runs[0].level == 1);
    pt_check(runs && runs[1].start == 3 && runs[1].end == 4 && runs[1].level == 1);
    pt_check(runs && runs[2].start == 0 && runs[2].end == 3 && runs[2].level == 2);
    pt_font_runs_free(runs);
    runs = NULL;

    /* Arabic letters and Arabic-Indic digits in one RTL paragraph: the AN
     * digits are an even-level island left of the AL word, while the leading
     * Common space stays in the odd-level run and is reversed. */
    const char *arabic_digits = "abc " "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"
                                        " " "\xD9\xA1\xD9\xA2\xD9\xA3";
    len = strlen(arabic_digits);
    count = pt_font_runs_bidi(font, arabic_digits, len, pt_bidi_base_rtl, &runs);
    pt_check(count == 4);
    pt_check(runs && runs[0].start == 13 && runs[0].end == 19 && runs[0].level == 2 &&
             runs[0].direction == HB_DIRECTION_LTR);
    pt_check(runs && runs[1].start == 4 && runs[1].end == 13 && runs[1].level == 1 &&
             runs[1].direction == HB_DIRECTION_RTL);
    pt_check(runs && runs[2].start == 3 && runs[2].end == 4 && runs[2].level == 1);
    pt_check(runs && runs[3].start == 0 && runs[3].end == 3 && runs[3].level == 2);
    pt_font_runs_free(runs);
    runs = NULL;

    /* A single-line request still accepts embedded newlines: each paragraph is
     * reordered under its own base and the visual runs stay concatenated in
     * logical paragraph order (the old adapter fell back to logical order). */
    const char *multiline = "abc\n\xD7\x90\xD7\x91";
    len = strlen(multiline);
    count = pt_font_runs_bidi(font, multiline, len, pt_bidi_base_auto, &runs);
    pt_check(count == 3);
    pt_check(runs && runs[0].level == 0 && runs[0].start == 0 && runs[0].end == 3);
    pt_check(runs && runs[1].level == 0 && runs[1].start == 3 && runs[1].end == 4);
    pt_check(runs && runs[2].level == 1 && runs[2].direction == HB_DIRECTION_RTL);
    pt_check(runs && runs[2].start == 4 && runs[2].end == len);
    pt_check(pt_font_measure_bidi(font, multiline, len, pt_bidi_base_auto) > 0.0f);
    pt_font_runs_free(runs);
    runs = NULL;

    const char *rtl_first = "\xD7\x90\xD7\x91\nabc";
    len = strlen(rtl_first);
    count = pt_font_runs_bidi(font, rtl_first, len, pt_bidi_base_auto, &runs);
    pt_check(count == 3);
    /* The first paragraph is one odd-level run holding the Hebrew letters and
     * the trailing newline (Common script), so its sub-runs are reversed:
     * the newline is visually leftmost, then the Hebrew letter pair. */
    pt_check(runs && runs[0].level == 1 && runs[0].direction == HB_DIRECTION_RTL);
    pt_check(runs && runs[0].start == 4 && runs[0].end == 5);
    pt_check(runs && runs[1].level == 1 && runs[1].start == 0 && runs[1].end == 4);
    pt_check(runs && runs[2].level == 0 && runs[2].start == 5 && runs[2].end == len);
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

/* Rule L1: whitespace at a wrapped line end resets to the paragraph base even
 * when the paragraph-level analysis resolved it to an embedded level. */
static void test_line_l1_whitespace(void) {
    /* LTR base, two Hebrew words separated by spaces. The whole paragraph
     * resolves the spaces to level 1 (inside the RTL stretch); a line cut after
     * the first word resets the trailing space to level 0. */
    const char *text = "\xD7\x90\xD7\x91   \xD7\x92\xD7\x93";
    pt_bidi_paragraph *paragraph = NULL;
    pt_check(pt_bidi_paragraph_analyze(text, strlen(text), pt_bidi_base_ltr, &paragraph));
    pt_check(paragraph != NULL);
    pt_bidi_line whole = {0};
    pt_check(pt_bidi_line_create(paragraph, 0, strlen(text), &whole));
    pt_check(whole.base_level == 0);
    pt_check(pt_bidi_line_level_at(&whole, 4) == 1);
    pt_check(pt_bidi_line_level_at(&whole, 5) == 1);
    pt_bidi_line_free(&whole);

    pt_bidi_line cut = {0};
    pt_check(pt_bidi_line_create(paragraph, 0, 5, &cut));
    pt_check(cut.base_level == 0);
    pt_check(pt_bidi_line_level_at(&cut, 3) == 1);
    pt_check(pt_bidi_line_level_at(&cut, 4) == 0);
    pt_bidi_line_free(&cut);
    pt_bidi_paragraph_free(paragraph);
}

/* Logical <-> visual map: clusters, run-boundary affinity, removed controls. */
static void test_visual_map(pt_font *font) {
    const char *hebrew_text = "\xD7\x90\xD7\x91\xD7\x92";
    size_t hebrew_len = strlen(hebrew_text);
    pt_text_visual_map *map = NULL;
    pt_check(pt_text_visual_map_build_bidi(font, hebrew_text, hebrew_len, pt_bidi_base_rtl, &map));
    pt_check(map != NULL);
    if (!map) {
        return;
    }
    float width = pt_text_visual_map_width(map);
    pt_check(width > 0.0f);
    pt_check(map->count == 3);
    pt_check(map->base_level == 1);
    /* Clusters are stored in logical order but their visual x walks left for an
     * RTL run: logical start is the right edge, logical end the left edge. */
    pt_check_near(pt_text_visual_map_x_for_position(map, 0, pt_text_bias_leading), width, 0.01f);
    pt_check(pt_text_visual_map_x_for_position(map, 2, pt_text_bias_leading) < width);
    pt_check(pt_text_visual_map_x_for_position(map, 4, pt_text_bias_leading) <
             pt_text_visual_map_x_for_position(map, 2, pt_text_bias_leading));
    pt_check_near(pt_text_visual_map_x_for_position(map, hebrew_len, pt_text_bias_trailing),
                  0.0f, 0.01f);
    /* Hit testing at the visual left edge returns the logical end. */
    pt_check(pt_text_visual_map_position_for_x(map, 0.0f, pt_text_bias_leading) == hebrew_len);
    pt_check(pt_text_visual_map_position_for_x(map, width, pt_text_bias_leading) == 0);
    /* Cluster boundaries: the middle of a 2-byte Hebrew cluster is interior. */
    pt_check(pt_text_visual_map_is_boundary(map, 0));
    pt_check(!pt_text_visual_map_is_boundary(map, 1));
    pt_check(pt_text_visual_map_is_boundary(map, 2));
    pt_check(pt_text_visual_map_next_boundary(map, 0) == 2);
    pt_check(pt_text_visual_map_prev_boundary(map, hebrew_len) == 4);
    pt_text_visual_map_free(map);
    map = NULL;

    /* The map's base level is the paragraph base, not the smallest run level.
     * An all-Latin paragraph under a forced RTL base resolves to 1; Hebrew
     * under a forced LTR base resolves to 0. Auto keeps the engine result. */
    pt_check(pt_text_visual_map_build_bidi(font, "abc", 3, pt_bidi_base_rtl, &map));
    pt_check(map != NULL && map->base_level == 1);
    pt_text_visual_map_free(map);
    map = NULL;
    pt_check(pt_text_visual_map_build_bidi(font, hebrew_text, hebrew_len, pt_bidi_base_ltr,
                                           &map));
    pt_check(map != NULL && map->base_level == 0);
    pt_text_visual_map_free(map);
    map = NULL;
    pt_check(pt_text_visual_map_build_bidi(font, hebrew_text, hebrew_len, pt_bidi_base_auto,
                                           &map));
    pt_check(map != NULL && map->base_level == 1);
    pt_text_visual_map_free(map);
    map = NULL;
    pt_check(pt_text_visual_map_build_bidi(font, "abc", 3, pt_bidi_base_auto, &map));
    pt_check(map != NULL && map->base_level == 0);
    pt_text_visual_map_free(map);
    map = NULL;

    /* Mixed multi-run line: "abc " + alef bet gimel under an RTL base. Segment
     * order is visual (Hebrew, space, Latin) but the cluster array must still be
     * monotonic in logical order for the binary lookup. Concrete caret
     * positions follow the visual layout: the Latin island is rightmost, the
     * space sits between it and the Hebrew word, and logical end 10 is x=0. */
    size_t mixed_len = strlen(mixed_ltr);
    pt_check(pt_text_visual_map_build_bidi(font, mixed_ltr, mixed_len, pt_bidi_base_rtl, &map));
    pt_check(map != NULL);
    if (map) {
        float hebrew_width = pt_font_measure_direction(font, hebrew_text, hebrew_len,
                                                       HB_DIRECTION_RTL);
        float space_width = pt_font_measure(font, " ", 1);
        float abc_width = pt_font_measure(font, "abc", 3);
        float a_width = pt_font_measure(font, "a", 1);
        float ab_width = pt_font_measure(font, "ab", 2);
        float line_width = pt_text_visual_map_width(map);
        pt_check(map->count == 7);
        pt_check_near(line_width, hebrew_width + space_width + abc_width, 0.01f);
        for (size_t i = 1; i < map->count; ++i) {
            pt_check(map->clusters[i - 1].start <= map->clusters[i].start);
        }
        float latin_left = hebrew_width + space_width;
        pt_check_near(pt_text_visual_map_x_for_position(map, 0, pt_text_bias_leading),
                      latin_left, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 0, pt_text_bias_trailing),
                      latin_left, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 1, pt_text_bias_leading),
                      latin_left + a_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 1, pt_text_bias_trailing),
                      latin_left + a_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 2, pt_text_bias_leading),
                      latin_left + ab_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 2, pt_text_bias_trailing),
                      latin_left + ab_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 3, pt_text_bias_leading),
                      latin_left, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 3, pt_text_bias_trailing),
                      line_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 4, pt_text_bias_leading),
                      hebrew_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 10, pt_text_bias_leading), 0.0f,
                      0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 10, pt_text_bias_trailing), 0.0f,
                      0.01f);
        pt_text_visual_map_free(map);
        map = NULL;
    }

    /* Reverse case: the same bytes under an LTR base keep logical order. The
     * Hebrew run is an RTL island whose logical start is its right edge. */
    pt_check(pt_text_visual_map_build_bidi(font, mixed_ltr, mixed_len, pt_bidi_base_ltr, &map));
    pt_check(map != NULL);
    if (map) {
        float hebrew_width = pt_font_measure_direction(font, hebrew_text, hebrew_len,
                                                       HB_DIRECTION_RTL);
        float space_width = pt_font_measure(font, " ", 1);
        float abc_width = pt_font_measure(font, "abc", 3);
        float line_width = pt_text_visual_map_width(map);
        pt_check(map->count == 7);
        pt_check(map->base_level == 0);
        for (size_t i = 1; i < map->count; ++i) {
            pt_check(map->clusters[i - 1].start <= map->clusters[i].start);
        }
        pt_check_near(pt_text_visual_map_x_for_position(map, 0, pt_text_bias_leading), 0.0f,
                      0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 3, pt_text_bias_trailing),
                      abc_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 4, pt_text_bias_leading),
                      line_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 4, pt_text_bias_trailing),
                      abc_width + space_width, 0.01f);
        pt_check_near(pt_text_visual_map_x_for_position(map, 10, pt_text_bias_leading),
                      abc_width + space_width, 0.01f);
        pt_check_near(pt_text_visual_map_width(map),
                      abc_width + space_width + hebrew_width, 0.01f);
        pt_text_visual_map_free(map);
        map = NULL;
    }
    /* Run-boundary affinity under an LTR base: the logical position between the
     * Latin run and the Hebrew run has two visual sides. */
    const char *mixed = "abc \xD7\x90\xD7\x91";
    size_t affinity_len = strlen(mixed);
    pt_check(pt_text_visual_map_build_bidi(font, mixed, affinity_len, pt_bidi_base_auto, &map));
    pt_check(map != NULL);
    if (map) {
        float leading = pt_text_visual_map_x_for_position(map, 4, pt_text_bias_leading);
        float trailing = pt_text_visual_map_x_for_position(map, 4, pt_text_bias_trailing);
        pt_check(leading > trailing + 0.5f);
        /* The Hebrew cluster interior maps to one x side or the other, never
         * past the line edges. */
        float inside = pt_text_visual_map_x_for_position(map, 5, pt_text_bias_leading);
        pt_check(inside >= -0.01f && inside <= pt_text_visual_map_width(map) + 0.01f);
        pt_text_visual_map_free(map);
        map = NULL;
    }

    /* X9 controls keep their bytes but collapse to a zero-width cluster; they
     * are not cluster boundaries and add no width. */
    const char *controlled = "a\xE2\x80\xAA" "b";
    size_t controlled_len = strlen(controlled);
    pt_check(pt_text_visual_map_build_bidi(font, controlled, controlled_len, pt_bidi_base_ltr,
                                            &map));
    pt_check(map != NULL);
    if (map) {
        pt_check_near(pt_text_visual_map_width(map), pt_font_measure(font, "ab", 2), 0.01f);
        pt_check(pt_text_visual_map_is_boundary(map, 1));
        pt_check(!pt_text_visual_map_is_boundary(map, 2));
        pt_check(!pt_text_visual_map_is_boundary(map, 3));
        pt_check(pt_text_visual_map_is_boundary(map, 4));
        pt_check_near(pt_text_visual_map_x_for_position(map, 2, pt_text_bias_leading),
                      pt_text_visual_map_x_for_position(map, 4, pt_text_bias_leading), 0.01f);
        pt_text_visual_map_free(map);
        map = NULL;
    }

    /* The map applies rule L1 per line: a line cut after Hebrew whitespace
     * resets the trailing space to the LTR paragraph base. */
    const char *l1_text = "\xD7\x90\xD7\x91   \xD7\x92\xD7\x93";
    pt_check(pt_text_visual_map_build_bidi(font, l1_text, 5, pt_bidi_base_ltr, &map));
    pt_check(map != NULL);
    if (map && map->count > 0) {
        pt_check(map->clusters[map->count - 1].level == 0);
        pt_text_visual_map_free(map);
        map = NULL;
    }
    pt_check(pt_text_visual_map_build_bidi(font, l1_text, strlen(l1_text), pt_bidi_base_ltr,
                                            &map));
    pt_check(map != NULL);
    if (map) {
        bool saw_embedded_space = false;
        for (size_t i = 0; i < map->count; ++i) {
            if (map->clusters[i].level != 0) {
                saw_embedded_space = true;
            }
        }
        pt_check(saw_embedded_space);
        pt_text_visual_map_free(map);
        map = NULL;
    }

    /* Empty input builds an empty map rather than failing. */
    pt_check(pt_text_visual_map_build_bidi(font, "", 0, pt_bidi_base_auto, &map));
    pt_check(map != NULL && map->count == 0 && pt_text_visual_map_width(map) == 0.0f);
    pt_text_visual_map_free(map);
}

/* Shaping is idempotent: per-run map assembly and the whole-line measure must
 * agree for pathological mark/control mixes. W7 found that a combining mark
 * with its own strong script (a Hebrew point after an Arabic letter) inherited
 * the preceding run's script inside the whole line but resolved its own script
 * when re-shaped in isolation, so the map width and measure_bidi disagreed and
 * an RTL entry's right-anchored origin overhung. The regression input below is
 * the W7 fuzz reproducer: Arabic lam + Hebrew point + ZWJ + FSI + digit. */
static void test_shaping_idempotent(pt_font *font) {
    static const char *const cases[] = {
        "\xD9\x84\xD6\xB0\xE2\x80\x8D\xE2\x81\xA8" "4",          /* lam + heb point + ZWJ + FSI + 4 */
        "\xD8\xA7\xD9\x8E\xE2\x80\x8D\xE2\x81\xA8" "4",          /* alef + arab mark + ZWJ + FSI + 4 */
        "\xD7\x90\xD6\xB0\xE2\x80\x8D\xE2\x81\xA9" "7",          /* alef + heb point + ZWJ + PDI + 7 */
        "a\xD6\xB0\xE2\x81\xA8" "3",                             /* latin + heb point + FSI + 3 */
        "\xD7\x90\xD9\x8E\xE2\x80\x8D" "4",                      /* alef + arab mark + ZWJ + 4 */
        "\xD9\x84\xD6\xB0" "12" "\xD7\x90",                      /* lam + heb point + digits + alef */
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",              /* plain Arabic */
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",                      /* plain Hebrew */
    };
    static const pt_bidi_base bases[] = {pt_bidi_base_rtl, pt_bidi_base_auto,
                                         pt_bidi_base_ltr};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        size_t length = strlen(cases[i]);
        for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b) {
            pt_text_visual_map *map = NULL;
            pt_check(pt_text_visual_map_build_bidi(font, cases[i], length, bases[b], &map));
            pt_check(map != NULL);
            if (!map) {
                continue;
            }
            float map_width = pt_text_visual_map_width(map);
            pt_check(map_width > 0.0f);
            pt_check_near(map_width, pt_font_measure_bidi(font, cases[i], length, bases[b]),
                          0.01f);
            pt_text_visual_map_free(map);
        }
    }
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

    /* Sub-run order inside the mixed RTL line is pixel-visible: the Hebrew word
     * starts at the left edge, the Common space follows, then the Latin island.
     * Composing that order from separate draws must be byte-identical to the
     * whole line; the swapped order must not be. */
    float hebrew_width =
        pt_font_measure_direction(font, "\xD7\x90\xD7\x91\xD7\x92", 6, HB_DIRECTION_RTL);
    float space_width = pt_font_measure(font, " ", 1);
    float abc_width = pt_font_measure(font, "abc", 3);
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line_bidi(canvas, font, mixed_ltr, strlen(mixed_ltr), 10.0f, 50.0f,
                                  0.0f, pt_text_align_start, 0xFFFFFFFFu, false,
                                  pt_bidi_base_rtl);
    uint64_t whole = pixels_hash(canvas);
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line_direction(canvas, font, "\xD7\x90\xD7\x91\xD7\x92", 6, 10.0f,
                                       50.0f, 0.0f, pt_text_align_start, 0xFFFFFFFFu, false,
                                       HB_DIRECTION_RTL);
    pt_canvas_draw_text_line(canvas, font, "abc", 3, 10.0f + hebrew_width + space_width,
                             50.0f, 0.0f, pt_text_align_start, 0xFFFFFFFFu, false);
    pt_check(pixels_hash(canvas) == whole);
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line(canvas, font, "abc", 3, 10.0f, 50.0f, 0.0f,
                             pt_text_align_start, 0xFFFFFFFFu, false);
    pt_canvas_draw_text_line_direction(canvas, font, "\xD7\x90\xD7\x91\xD7\x92", 6,
                                       10.0f + abc_width + space_width, 50.0f, 0.0f,
                                       pt_text_align_start, 0xFFFFFFFFu, false,
                                       HB_DIRECTION_RTL);
    pt_check(pixels_hash(canvas) != whole);

    /* The same holds for Arabic in an RTL base. */
    const char *arabic_word = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    const char *mixed_arabic = "abc " "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    float arabic_width = pt_font_measure_direction(font, arabic_word, 8, HB_DIRECTION_RTL);
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line_bidi(canvas, font, mixed_arabic, strlen(mixed_arabic), 10.0f,
                                  50.0f, 0.0f, pt_text_align_start, 0xFFFFFFFFu, false,
                                  pt_bidi_base_rtl);
    uint64_t arabic_whole = pixels_hash(canvas);
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line_direction(canvas, font, arabic_word, 8, 10.0f, 50.0f, 0.0f,
                                       pt_text_align_start, 0xFFFFFFFFu, false,
                                       HB_DIRECTION_RTL);
    pt_canvas_draw_text_line(canvas, font, "abc", 3, 10.0f + arabic_width + space_width,
                             50.0f, 0.0f, pt_text_align_start, 0xFFFFFFFFu, false);
    pt_check(pixels_hash(canvas) == arabic_whole);
    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_draw_text_line(canvas, font, "abc", 3, 10.0f, 50.0f, 0.0f,
                             pt_text_align_start, 0xFFFFFFFFu, false);
    pt_canvas_draw_text_line_direction(canvas, font, arabic_word, 8,
                                       10.0f + abc_width + space_width, 50.0f, 0.0f,
                                       pt_text_align_start, 0xFFFFFFFFu, false,
                                       HB_DIRECTION_RTL);
    pt_check(pixels_hash(canvas) != arabic_whole);

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

/* --- Bidi control inventory and sanitization (2.1) ------------------------- */

/* The nine explicit controls, in kind order (kind 1..9). */
static const char *const bidi_controls[9] = {
    "\xE2\x80\xAA", "\xE2\x80\xAB", "\xE2\x80\xAC", "\xE2\x80\xAD", "\xE2\x80\xAE",
    "\xE2\x81\xA6", "\xE2\x81\xA7", "\xE2\x81\xA8", "\xE2\x81\xA9",
};
static const uint32_t bidi_control_points[9] = {0x202Au, 0x202Bu, 0x202Cu, 0x202Du, 0x202Eu,
                                                0x2066u, 0x2067u, 0x2068u, 0x2069u};

#define UFFFD "\xEF\xBF\xBD"

static void test_control_inventory_each(void) {
    /* Each control alone, and embedded in "a" ... "b", reports the exact
     * offset, length, kind and code point. */
    for (size_t i = 0; i < 9; ++i) {
        char text[8] = {'a', 0, 0, 0, 'b', 0};
        memcpy(text + 1, bidi_controls[i], 3);
        pt_bidi_control_span spans[2];
        memset(spans, 0xAB, sizeof spans);
        size_t count = pt_bidi_control_inventory(text, 5, spans, 2);
        pt_check(count == 1);
        pt_check(spans[0].offset == 1);
        pt_check(spans[0].length == 3);
        pt_check(spans[0].kind == (pt_bidi_control_kind)(i + 1));
        pt_check(spans[0].codepoint == bidi_control_points[i]);
        /* Only the first min(total, capacity) entry is written. */
        pt_check((unsigned char)spans[1].offset == 0xAB);

        count = pt_bidi_control_inventory(bidi_controls[i], 3, spans, 2);
        pt_check(count == 1 && spans[0].offset == 0 && spans[0].length == 3);
    }

    /* The enum values are the pinned ABI values. */
    pt_check(pt_bidi_control_none == 0);
    pt_check(pt_bidi_control_lre == 1 && pt_bidi_control_rle == 2);
    pt_check(pt_bidi_control_pdf == 3 && pt_bidi_control_lro == 4);
    pt_check(pt_bidi_control_rlo == 5 && pt_bidi_control_lri == 6);
    pt_check(pt_bidi_control_rli == 7 && pt_bidi_control_fsi == 8);
    pt_check(pt_bidi_control_pdi == 9);
    pt_check(pt_bidi_sanitize_strip == 0 && pt_bidi_sanitize_neutralize == 1);
}

static void test_control_inventory_mixed(void) {
    /* "abc" RLO DEF PDI "." RLI "X" LRE "Y" — five controls, exact offsets. */
    const char text[] = "abc"
                        "\xE2\x80\xAE"
                        "DEF"
                        "\xE2\x81\xA9"
                        "."
                        "\xE2\x81\xA7"
                        "X"
                        "\xE2\x80\xAA"
                        "Y";
    size_t length = sizeof text - 1;
    pt_bidi_control_span spans[8];
    size_t count = pt_bidi_control_inventory(text, length, spans, 8);
    pt_check(count == 4);
    pt_check(spans[0].offset == 3 && spans[0].kind == pt_bidi_control_rlo);
    pt_check(spans[0].codepoint == 0x202Eu);
    pt_check(spans[1].offset == 9 && spans[1].kind == pt_bidi_control_pdi);
    pt_check(spans[2].offset == 13 && spans[2].kind == pt_bidi_control_rli);
    pt_check(spans[3].offset == 17 && spans[3].kind == pt_bidi_control_lre);
    for (size_t i = 0; i < count; ++i) pt_check(spans[i].length == 3);

    /* Every control in a row keeps increasing offsets. */
    char all[9 * 3 + 2];
    all[0] = 'x';
    for (size_t i = 0; i < 9; ++i) memcpy(all + 1 + i * 3, bidi_controls[i], 3);
    all[1 + 9 * 3] = 'y';
    count = pt_bidi_control_inventory(all, 1 + 9 * 3 + 1, spans, 8);
    pt_check(count == 9);
    for (size_t i = 0; i < 8; ++i) {
        pt_check(spans[i].offset == 1 + i * 3);
        pt_check(spans[i].kind == (pt_bidi_control_kind)(i + 1));
    }

    /* Capacity truncation still returns the total; count-only needs no array. */
    pt_bidi_control_span two[2];
    count = pt_bidi_control_inventory(all, 1 + 9 * 3 + 1, two, 2);
    pt_check(count == 9);
    pt_check(two[0].offset == 1 && two[1].offset == 4);
    pt_check(pt_bidi_control_inventory(all, 1 + 9 * 3 + 1, NULL, 0) == 9);
    pt_check(pt_bidi_control_inventory(all, 1 + 9 * 3 + 1, two, 0) == 9);
    pt_check(two[0].offset == 1);

    /* NULL and empty count nothing and leave the array untouched. */
    pt_check(pt_bidi_control_inventory(NULL, 4, spans, 8) == 0);
    pt_check(pt_bidi_control_inventory(NULL, 0, spans, 8) == 0);
    pt_check(pt_bidi_control_inventory("", 0, spans, 8) == 0);
    pt_check(pt_bidi_control_inventory("abc", 0, spans, 8) == 0);

    /* A length shorter than the string stops the scan at the boundary. */
    pt_check(pt_bidi_control_inventory("abc\xE2\x80\xAE", 3, spans, 8) == 0);
    pt_check(pt_bidi_control_inventory("abc\xE2\x80\xAE", 4, spans, 8) == 0);
}

static void test_control_inventory_ignores_bn(void) {
    /* The internal paint-skip predicate covers BN and the explicit controls;
     * the public inventory reports only the nine explicit controls. ZWSP
     * U+200B, ZWNJ U+200C, ZWJ U+200D, LRM U+200E, RLM U+200F, soft hyphen
     * U+00AD, word joiner U+2060 and the byte order marks U+FEFF are not
     * reported even though several are X9-removed when painted. */
    const char bn_text[] = "a"
                           "\xE2\x80\x8B"
                           "b"
                           "\xE2\x80\x8C"
                           "c"
                           "\xE2\x80\x8D"
                           "d"
                           "\xE2\x80\x8E"
                           "e"
                           "\xE2\x80\x8F"
                           "f"
                           "\xC2\xAD"
                           "g"
                           "\xE2\x81\xA0"
                           "h"
                           "\xEF\xBB\xBF";
    pt_bidi_control_span spans[4];
    pt_check(pt_bidi_control_inventory(bn_text, sizeof bn_text - 1, spans, 4) == 0);

    pt_check(pt_bidi_is_removed(0x200Bu)); /* ZWSP: BN */
    pt_check(pt_bidi_is_removed(0x200Cu)); /* ZWNJ: BN */
    pt_check(pt_bidi_is_removed(0x200Du)); /* ZWJ: BN */
    pt_check(pt_bidi_is_removed(0x00ADu)); /* soft hyphen: BN */
    pt_check(pt_bidi_is_removed(0x2060u)); /* word joiner: BN */
    pt_check(!pt_bidi_is_removed(0x2066u)); /* LRI is not X9-removed */

    /* Invalid UTF-8 and a truncated control sequence are not reported. */
    const char invalid[] = {'a', (char)0xFF, (char)0xE2, (char)0x80, 'b', 0};
    pt_check(pt_bidi_control_inventory(invalid, 5, spans, 4) == 0);
}

static void test_control_sanitize(void) {
    const char text[] = "abc"
                        "\xE2\x80\xAE"
                        "DEF"
                        "\xE2\x81\xA9";
    size_t length = sizeof text - 1;
    size_t out_length = 123;

    char *stripped = pt_bidi_sanitize(text, length, pt_bidi_sanitize_strip, &out_length);
    pt_check(stripped != NULL);
    pt_check(out_length == 6);
    pt_check(strcmp(stripped, "abcDEF") == 0);
    pt_check(pt_bidi_control_inventory(stripped, out_length, NULL, 0) == 0);
    pt_free(stripped);

    char *neutral = pt_bidi_sanitize(text, length, pt_bidi_sanitize_neutralize, &out_length);
    pt_check(neutral != NULL);
    /* Two controls each grow to three bytes: 6 + 2 * 3. */
    pt_check(out_length == 12);
    pt_check(strcmp(neutral, "abc" UFFFD "DEF" UFFFD) == 0);
    pt_check(strstr(neutral, UFFFD) != NULL);
    pt_check(pt_bidi_control_inventory(neutral, out_length, NULL, 0) == 0);
    pt_free(neutral);

    /* The zero mode value is strip. */
    out_length = 0;
    char *zero = pt_bidi_sanitize("a" "\xE2\x80\xAA" "b", 5, (pt_bidi_sanitize_mode)0,
                                  &out_length);
    pt_check(zero != NULL && out_length == 2 && strcmp(zero, "ab") == 0);
    pt_free(zero);

    /* Every control neutralizes to exactly one U+FFFD. */
    char all[9 * 3];
    for (size_t i = 0; i < 9; ++i) memcpy(all + i * 3, bidi_controls[i], 3);
    char *all_out = pt_bidi_sanitize(all, 27, pt_bidi_sanitize_neutralize, &out_length);
    pt_check(all_out != NULL && out_length == 27);
    for (size_t i = 0; i < 9; ++i) pt_check(memcmp(all_out + i * 3, UFFFD, 3) == 0);
    pt_free(all_out);
    all_out = pt_bidi_sanitize(all, 27, pt_bidi_sanitize_strip, &out_length);
    pt_check(all_out != NULL && out_length == 0 && all_out[0] == '\0');
    pt_free(all_out);

    /* BN characters and invalid UTF-8 survive both modes unchanged. */
    const char keep[] = "a\xE2\x80\x8D" "b";
    char *kept = pt_bidi_sanitize(keep, sizeof keep - 1, pt_bidi_sanitize_neutralize, &out_length);
    pt_check(kept != NULL && out_length == sizeof keep - 1);
    pt_check(memcmp(kept, keep, sizeof keep - 1) == 0);
    pt_free(kept);
    const char bad[] = {'a', (char)0xFF, (char)0xC3, 0};
    kept = pt_bidi_sanitize(bad, 3, pt_bidi_sanitize_strip, &out_length);
    pt_check(kept != NULL && out_length == 3 && memcmp(kept, bad, 3) == 0);
    pt_free(kept);

    /* Empty input is an allocated empty string; out_length may be NULL. */
    char *empty = pt_bidi_sanitize("", 0, pt_bidi_sanitize_strip, &out_length);
    pt_check(empty != NULL && empty[0] == '\0' && out_length == 0);
    pt_free(empty);
    empty = pt_bidi_sanitize(NULL, 0, pt_bidi_sanitize_neutralize, NULL);
    pt_check(empty != NULL && empty[0] == '\0');
    pt_free(empty);

    /* Invalid arguments return NULL and do not write out_length. */
    out_length = 77;
    pt_check(pt_bidi_sanitize(NULL, 3, pt_bidi_sanitize_strip, &out_length) == NULL);
    pt_check(out_length == 77);
    pt_check(pt_bidi_sanitize("abc", 3, (pt_bidi_sanitize_mode)2, &out_length) == NULL);
    pt_check(pt_bidi_sanitize("abc", 3, (pt_bidi_sanitize_mode)-1, &out_length) == NULL);
    pt_check(out_length == 77);
    pt_check(pt_bidi_sanitize("abc", SIZE_MAX, pt_bidi_sanitize_strip, &out_length) == NULL);

    /* pt_free(NULL) is safe, and a round trip is byte-exact for plain text. */
    pt_free(NULL);
    char plain[] = "plain \xD7\x90\xD7\x91 text";
    kept = pt_bidi_sanitize(plain, sizeof plain - 1, pt_bidi_sanitize_neutralize, &out_length);
    pt_check(kept != NULL && out_length == sizeof plain - 1);
    pt_check(memcmp(kept, plain, sizeof plain - 1) == 0);
    pt_free(kept);
}

/* CVE-2021-42574 class: an RLO override that visually reverses the rest of a
 * line, wrapped in an isolate pair, hidden inside an identifier. */
static void test_control_trojan_source(void) {
    /* "if (access != 'user') {" then RLI + LRI "// Check if admin" PDI PDI so a
     * reviewer reads the comment while the bytes carry the code. */
    const char source[] = "if (isAdmin) { // admin\n"
                          "\xE2\x81\xA7"
                          "\xE2\x81\xA6"
                          " } else { // guest"
                          "\xE2\x81\xA9"
                          "\xE2\x81\xA9"
                          "\n";
    size_t length = sizeof source - 1;
    pt_bidi_control_span spans[4];
    size_t count = pt_bidi_control_inventory(source, length, spans, 4);
    pt_check(count == 4);
    pt_check(spans[0].kind == pt_bidi_control_rli);
    pt_check(spans[1].kind == pt_bidi_control_lri);
    pt_check(spans[2].kind == pt_bidi_control_pdi);
    pt_check(spans[3].kind == pt_bidi_control_pdi);
    pt_check(spans[0].offset == strlen("if (isAdmin) { // admin\n"));
    pt_check(spans[1].offset == spans[0].offset + 3);

    /* The text does reorder for the bidi-aware renderer, which is why the
     * inventory exists: the bytes look harmless without it. */
    pt_check(pt_bidi_text_may_reorder(source, length));

    size_t out_length = 0;
    char *clean = pt_bidi_sanitize(source, length, pt_bidi_sanitize_strip, &out_length);
    pt_check(clean != NULL);
    pt_check(pt_bidi_control_inventory(clean, out_length, NULL, 0) == 0);
    pt_check(strstr(clean, "if (isAdmin) { // admin\n } else { // guest\n") != NULL);
    pt_free(clean);

    /* The classic RLO filename disguise: "exe.txt" with the visible name
     * reversed. Neutralizing shows the tampering instead of hiding it. */
    const char filename[] = "report"
                            "\xE2\x80\xAE"
                            "fdp.exe";
    count = pt_bidi_control_inventory(filename, sizeof filename - 1, spans, 4);
    pt_check(count == 1 && spans[0].kind == pt_bidi_control_rlo && spans[0].offset == 6);
    char *shown = pt_bidi_sanitize(filename, sizeof filename - 1, pt_bidi_sanitize_neutralize,
                                   &out_length);
    pt_check(shown != NULL && out_length == 6 + 3 + 7);
    pt_check(memcmp(shown + 6, UFFFD, 3) == 0);
    pt_free(shown);

    /* A text setter stores the control bytes unchanged: sanitization is
     * opt-in and no existing path calls it. */
    pt_widget *label = pt_label_create(NULL, source);
    pt_check(label != NULL);
    const char *stored = pt_widget_get_text(label);
    pt_check(stored != NULL && strcmp(stored, source) == 0);
    pt_check(pt_bidi_control_inventory(stored, strlen(stored), NULL, 0) == 4);
    pt_widget_destroy(label);
}

int main(void) {
    pt_bidi_base base = pt_bidi_base_auto;

    test_paragraph_basics();
    test_byte_maps();
    test_controls_and_combining();
    test_line_l1_whitespace();
    test_control_inventory_each();
    test_control_inventory_mixed();
    test_control_inventory_ignores_bn();
    test_control_sanitize();
    test_control_trojan_source();

    pt_font *font = pt_font_acquire("DejaVu Sans", pt_font_weight_regular,
                                    pt_font_slant_upright, 24.0f);
    if (!font || !pt_font_has_glyph(font, 0x0627u) || !pt_font_has_glyph(font, 0x05D0u)) {
        skip_fonts("DejaVu Sans with Arabic and Hebrew is not installed");
    } else {
        test_font_pipeline(font);
        test_rendering(font);
        test_visual_map(font);
        test_shaping_idempotent(font);
    }

    /* Neutral fallback keeps the API surface honest even without the font. */
    pt_check(pt_font_measure_bidi(NULL, "x", 1, base) == 0.0f);

    pt_font_manager_shutdown();
    printf("bidi: %d font-dependent test(s) skipped\n", skipped);
    return pt_test_finish("bidi");
}
