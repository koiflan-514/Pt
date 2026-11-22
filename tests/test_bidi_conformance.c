/*
 * Unicode BidiTest / BidiCharacterTest conformance (2.0 wave 0).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Drives the full Pt bidi adapter, not SheenBidi directly: representative
 * characters are encoded as UTF-8, analysed through pt_bidi_paragraph_analyze
 * and reordered through pt_bidi_line_create. Expected levels and reorder
 * indices come from the Unicode conformance files; removed X9 controls carry
 * an 'x' expectation and are skipped in comparisons but stay in the byte runs.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/internal.h"
#include "harness.h"

#define MAX_CPS 512

#ifndef PT_BIDI_TEST_DIR_DEFAULT
#define PT_BIDI_TEST_DIR_DEFAULT "build/bidi-test"
#endif

static unsigned char utf8_buffer[4 * MAX_CPS];
static size_t char_offset[MAX_CPS];
static int byte_index[4 * MAX_CPS];

static long detail_budget = 20;

static void fail_detail(const char *suite, long line_no, const char *format, ...) {
    if (detail_budget <= 0) return;
    --detail_budget;
    fprintf(stderr, "%s line %ld: ", suite, line_no);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static uint32_t cp_for_class(const char *name) {
    if (!strcmp(name, "L")) return 'a';
    if (!strcmp(name, "R")) return 0x05D0;
    if (!strcmp(name, "AL")) return 0x0627;
    if (!strcmp(name, "EN")) return '0';
    if (!strcmp(name, "ES")) return '+';
    if (!strcmp(name, "ET")) return '#';
    if (!strcmp(name, "AN")) return 0x0660;
    if (!strcmp(name, "CS")) return ',';
    if (!strcmp(name, "NSM")) return 0x0301;
    if (!strcmp(name, "BN")) return 0x00AD;
    if (!strcmp(name, "B")) return 0x2029;
    if (!strcmp(name, "S")) return '\t';
    if (!strcmp(name, "WS")) return ' ';
    if (!strcmp(name, "ON")) return '!';
    if (!strcmp(name, "LRE")) return 0x202A;
    if (!strcmp(name, "RLE")) return 0x202B;
    if (!strcmp(name, "PDF")) return 0x202C;
    if (!strcmp(name, "LRO")) return 0x202D;
    if (!strcmp(name, "RLO")) return 0x202E;
    if (!strcmp(name, "LRI")) return 0x2066;
    if (!strcmp(name, "RLI")) return 0x2067;
    if (!strcmp(name, "FSI")) return 0x2068;
    if (!strcmp(name, "PDI")) return 0x2069;
    return 0;
}

static size_t encode_codepoint(uint32_t cp, unsigned char *out) {
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

static size_t encode_text(const uint32_t *cps, int count) {
    size_t length = 0;
    for (int i = 0; i < count; ++i) {
        char_offset[i] = length;
        size_t n = encode_codepoint(cps[i], utf8_buffer + length);
        for (size_t k = 0; k < n; ++k) byte_index[length + k] = i;
        length += n;
    }
    return length;
}

static int parse_levels(char *text, int *levels, int max) {
    int count = 0;
    char *save = NULL;
    for (char *token = strtok_r(text, " \t\r\n", &save); token;
         token = strtok_r(NULL, " \t\r\n", &save)) {
        if (count >= max) return -1;
        levels[count++] = token[0] == 'x' ? -1 : atoi(token);
    }
    return count;
}

static int parse_order(char *text, int *order, int max) {
    int count = 0;
    char *save = NULL;
    for (char *token = strtok_r(text, " \t\r\n", &save); token;
         token = strtok_r(NULL, " \t\r\n", &save)) {
        if (count >= max) return -1;
        order[count++] = atoi(token);
    }
    return count;
}

/* Compares one analysed sequence against the expected levels and visual order.
 * `expected_para` of -1 means the suite supplies no paragraph-level
 * expectation. Removed characters are identified by the adapter helper and
 * skipped in the visual order, matching the 'x' entries in the data. */
static bool check_case(const char *suite, long line_no, const uint32_t *cps, int count,
                       size_t byte_length, pt_bidi_base base, int expected_para,
                       const int *levels, int level_count, const int *order, int order_count) {
    bool ok = true;

    pt_bidi_paragraph *paragraph = NULL;
    if (!pt_bidi_paragraph_analyze((const char *)utf8_buffer, byte_length, base, &paragraph)) {
        fail_detail(suite, line_no, "paragraph analysis failed");
        return false;
    }

    if (expected_para >= 0 && (int)pt_bidi_paragraph_base_level(paragraph) != expected_para) {
        fail_detail(suite, line_no, "paragraph level %u, want %d",
                    (unsigned)pt_bidi_paragraph_base_level(paragraph), expected_para);
        ok = false;
    }

    pt_bidi_line line;
    if (!pt_bidi_line_create(paragraph, 0, pt_bidi_paragraph_length(paragraph), &line)) {
        fail_detail(suite, line_no, "line creation failed");
        pt_bidi_paragraph_free(paragraph);
        return false;
    }

    for (int i = 0; i < count && i < level_count; ++i) {
        bool removed = pt_bidi_is_removed(cps[i]);
        if ((levels[i] < 0) != removed) {
            fail_detail(suite, line_no, "char %d removed=%d but expected level %d", i,
                        (int)removed, levels[i]);
            ok = false;
        }
        if (levels[i] < 0) continue;
        int got = pt_bidi_line_level_at(&line, char_offset[i]);
        if (got < 0) {
            fail_detail(suite, line_no, "char %d has no resolved level", i);
            ok = false;
        } else if (got != levels[i]) {
            fail_detail(suite, line_no, "level[%d] = %d, want %d", i, got, levels[i]);
            ok = false;
        }
    }

    int index = 0;
    for (size_t r = 0; r < line.run_count; ++r) {
        const pt_bidi_run *run = &line.runs[r];
        if (run->length == 0) continue;
        int first = byte_index[run->offset];
        int last = byte_index[run->offset + run->length - 1];
        if (run->level & 1) {
            for (int k = last; k >= first; --k) {
                if (pt_bidi_is_removed(cps[k])) continue;
                if (index >= order_count || order[index] != k) {
                    fail_detail(suite, line_no, "visual order[%d] = %d, want %d", index, k,
                                index < order_count ? order[index] : -1);
                    ok = false;
                }
                ++index;
            }
        } else {
            for (int k = first; k <= last; ++k) {
                if (pt_bidi_is_removed(cps[k])) continue;
                if (index >= order_count || order[index] != k) {
                    fail_detail(suite, line_no, "visual order[%d] = %d, want %d", index, k,
                                index < order_count ? order[index] : -1);
                    ok = false;
                }
                ++index;
            }
        }
    }
    if (index != order_count) {
        fail_detail(suite, line_no, "visual order length %d, want %d", index, order_count);
        ok = false;
    }

    pt_bidi_line_free(&line);
    pt_bidi_paragraph_free(paragraph);
    return ok;
}

static int parse_classes(char *text, uint32_t *cps, int max) {
    int count = 0;
    char *save = NULL;
    for (char *token = strtok_r(text, " \t\r\n", &save); token;
         token = strtok_r(NULL, " \t\r\n", &save)) {
        uint32_t cp = cp_for_class(token);
        if (cp == 0 || count >= max) return -1;
        cps[count++] = cp;
    }
    return count;
}

static bool run_bidi_test(const char *path, long *case_count, long *fail_count) {
    FILE *file = fopen(path, "r");
    if (!file) return false;

    char *line = NULL;
    size_t capacity = 0;
    long line_no = 0;
    int levels[MAX_CPS];
    int level_count = 0;
    int order[MAX_CPS];
    int order_count = 0;
    *case_count = 0;
    *fail_count = 0;

    while (getline(&line, &capacity, file) >= 0) {
        ++line_no;
        if (line[0] == '#') continue;
        if (!strncmp(line, "@Levels:", 8)) {
            level_count = parse_levels(line + 8, levels, MAX_CPS);
            continue;
        }
        if (!strncmp(line, "@Reorder:", 9)) {
            order_count = parse_order(line + 9, order, MAX_CPS);
            continue;
        }
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;

        char *semicolon = strchr(line, ';');
        if (!semicolon) continue;
        *semicolon = '\0';
        int directions = 7;
        char *rest = semicolon + 1;
        char *second = strchr(rest, ';');
        if (second) *second = '\0';
        while (*rest == ' ' || *rest == '\t') ++rest;
        if (*rest) directions = atoi(rest);

        uint32_t cps[MAX_CPS];
        int count = parse_classes(line, cps, MAX_CPS);
        if (count < 0 || level_count < 0 || order_count < 0) continue;
        size_t byte_length = encode_text(cps, count);

        /* BidiTest bitset: 1 = auto (P2-P3, default LTR), 2 = LTR, 4 = RTL. */
        static const pt_bidi_base bases[3] = {pt_bidi_base_auto, pt_bidi_base_ltr,
                                              pt_bidi_base_rtl};
        static const int bits[3] = {1, 2, 4};
        for (int i = 0; i < 3; ++i) {
            if (!(directions & bits[i])) continue;
            ++*case_count;
            if (!check_case("BidiTest", line_no, cps, count, byte_length, bases[i], -1, levels,
                            level_count, order, order_count))
                ++*fail_count;
        }
    }

    free(line);
    fclose(file);
    return true;
}

static bool run_bidi_character_test(const char *path, long *case_count, long *fail_count) {
    FILE *file = fopen(path, "r");
    if (!file) return false;

    char *line = NULL;
    size_t capacity = 0;
    long line_no = 0;
    *case_count = 0;
    *fail_count = 0;

    while (getline(&line, &capacity, file) >= 0) {
        ++line_no;
        if (line[0] == '#') continue;
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;

        char *fields[5] = {0};
        int field_count = 0;
        char *cursor = line;
        while (field_count < 5) {
            fields[field_count++] = cursor;
            char *semicolon = strchr(cursor, ';');
            if (!semicolon) break;
            *semicolon = '\0';
            cursor = semicolon + 1;
        }
        if (field_count < 5) continue;

        uint32_t cps[MAX_CPS];
        int count = 0;
        char *save = NULL;
        bool bad = false;
        for (char *token = strtok_r(fields[0], " \t\r\n", &save); token;
             token = strtok_r(NULL, " \t\r\n", &save)) {
            if (count >= MAX_CPS) {
                bad = true;
                break;
            }
            cps[count++] = (uint32_t)strtoul(token, NULL, 16);
        }
        if (bad) continue;

        int direction = atoi(fields[1]);
        int expected_para = atoi(fields[2]);
        int levels[MAX_CPS];
        int level_count = parse_levels(fields[3], levels, MAX_CPS);
        int order[MAX_CPS];
        int order_count = parse_order(fields[4], order, MAX_CPS);
        if (level_count < 0 || order_count < 0) continue;

        pt_bidi_base base;
        if (direction == 0)
            base = pt_bidi_base_ltr;
        else if (direction == 1)
            base = pt_bidi_base_rtl;
        else if (direction == 2)
            base = pt_bidi_base_auto;
        else
            continue;

        size_t byte_length = encode_text(cps, count);
        ++*case_count;
        if (!check_case("BidiCharacterTest", line_no, cps, count, byte_length, base, expected_para,
                        levels, level_count, order, order_count))
            ++*fail_count;
    }

    free(line);
    fclose(file);
    return true;
}

/* Reads the version from the first line, e.g. "# BidiTest-18.0.0.txt". */
static void read_data_version(const char *path, char *out, size_t capacity) {
    out[0] = '\0';
    FILE *file = fopen(path, "r");
    if (!file) return;
    char line[256];
    if (fgets(line, sizeof line, file)) {
        char *end = strstr(line, ".txt");
        if (end) {
            char *start = end;
            while (start > line && start[-1] != '-') --start;
            size_t length = (size_t)(end - start);
            if (length >= capacity) length = capacity - 1;
            memcpy(out, start, length);
            out[length] = '\0';
        }
    }
    fclose(file);
}

int main(void) {
    const char *directory = getenv("PT_BIDI_TEST_DIR");
    if (!directory || !*directory) directory = PT_BIDI_TEST_DIR_DEFAULT;

    char bidi_path[2048];
    char character_path[2048];
    snprintf(bidi_path, sizeof bidi_path, "%s/BidiTest.txt", directory);
    snprintf(character_path, sizeof character_path, "%s/BidiCharacterTest.txt", directory);

    char bidi_version[64];
    char character_version[64];
    read_data_version(bidi_path, bidi_version, sizeof bidi_version);
    read_data_version(character_path, character_version, sizeof character_version);

    long bidi_cases = 0;
    long bidi_failures = 0;
    long character_cases = 0;
    long character_failures = 0;
    bool have_bidi = run_bidi_test(bidi_path, &bidi_cases, &bidi_failures);
    bool have_character =
        run_bidi_character_test(character_path, &character_cases, &character_failures);

    if (!have_bidi || !have_character) {
        const char *missing = !have_bidi && !have_character
                                  ? "BidiTest.txt and BidiCharacterTest.txt"
                                  : !have_bidi ? "BidiTest.txt" : "BidiCharacterTest.txt";
        printf("skip bidi_conformance: %s missing or unreadable in %s\n", missing, directory);
        pt_check(1);
        return pt_test_finish("bidi_conformance");
    }

    printf("bidi engine %s, engine UCD %s, test data %s\n", pt_bidi_engine_version(),
           pt_bidi_unicode_version(),
           bidi_version[0] ? bidi_version : character_version[0] ? character_version : "unknown");
    printf("BidiTest %s: %ld cases, %ld failures\n", bidi_version, bidi_cases, bidi_failures);
    printf("BidiCharacterTest %s: %ld cases, %ld failures\n", character_version, character_cases,
           character_failures);

    pt_check(strcmp(pt_bidi_engine_version(), "3.0.0") == 0);
    pt_check(strcmp(pt_bidi_unicode_version(), "17.0.0") == 0);
    pt_check(strcmp(bidi_version, "18.0.0") == 0);
    pt_check(strcmp(character_version, "18.0.0") == 0);
    pt_check(bidi_cases == 770241);
    pt_check(bidi_failures == 0);
    pt_check(character_cases == 91707);
    pt_check(character_failures == 0);
    return pt_test_finish("bidi_conformance");
}