/*
 * Bidi adapter over the vendored SheenBidi engine (2.0).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is the only file that knows SheenBidi's types. Everything above it
 * speaks logical UTF-8 byte offsets and plain levels; the engine's SB objects
 * never escape. All analysis is bounded: invalid ranges and allocation
 * failures return false without leaving a partially owned object behind.
 *
 * The engine is fed UTF-32 code points rather than UTF-8 directly. Its UTF-8
 * code-unit model marks continuation bytes as BN, and SBLine's L1 reset then
 * mis-resets isolate formatting characters that follow a segment separator
 * (e.g. "S RLE PDI R": the PDI loses its resolved level). Decoding with the
 * engine's own SBCodepointDecodeNextFromUTF8 and keeping a code-point-to-byte
 * table gives spec-correct lines while the public adapter API still exposes
 * logical UTF-8 byte offsets. The 770,241 BidiTest cases pass on this path;
 * the direct UTF-8 path fails 1,514 of them.
 */

#include <stdlib.h>
#include <string.h>

#include <SheenBidi/SheenBidi.h>

#include "internal.h"

/* Unicode Character Database release the vendored engine tables were built
 * from. Keep in step with third_party/sheenbidi/PROVENANCE.md. */
#define PT_BIDI_UNICODE_VERSION "17.0.0"

struct pt_bidi_paragraph {
    SBAlgorithmRef algorithm;
    SBParagraphRef paragraph;
    /* Decoded UTF-32 copy handed to the engine, and the byte offset of every
     * code point plus one past-the-end entry. */
    uint32_t *codepoints;
    size_t *byte_offsets;
    size_t codepoint_count;
    size_t codepoint_length;
    size_t length;
    uint8_t base_level;
};

static void pt_bidi_paragraph_dispose(pt_bidi_paragraph *paragraph) {
    if (paragraph->paragraph) SBParagraphRelease(paragraph->paragraph);
    if (paragraph->algorithm) SBAlgorithmRelease(paragraph->algorithm);
    free(paragraph->codepoints);
    free(paragraph->byte_offsets);
    free(paragraph);
}

static SBLevel pt_bidi_sb_base_level(pt_bidi_base base) {
    switch (base) {
    case pt_bidi_base_ltr:
        return 0;
    case pt_bidi_base_rtl:
        return 1;
    case pt_bidi_base_auto:
    default:
        /* Rules P2-P3, defaulting to 0 when there is no strong character. */
        return SBLevelDefaultLTR;
    }
}

/* Finds the code point index whose byte offset is exactly `byte`. */
static bool pt_bidi_byte_to_codepoint(const size_t *offsets, size_t codepoint_count,
                                      size_t byte, size_t *out) {
    size_t low = 0;
    size_t high = codepoint_count;
    while (low <= high) {
        size_t middle = low + (high - low) / 2;
        if (offsets[middle] == byte) {
            *out = middle;
            return true;
        }
        if (offsets[middle] < byte) {
            low = middle + 1;
        } else {
            if (middle == 0) break;
            high = middle - 1;
        }
    }
    return false;
}

static bool pt_bidi_paragraph_create(const char *text, size_t len, SBLevel sb_base,
                                     uint8_t empty_base, pt_bidi_paragraph **out) {
    if (!out) return false;
    *out = NULL;
    if (!text && len != 0) return false;
    if (len > SIZE_MAX / sizeof(uint32_t) - 1 || len > SIZE_MAX / sizeof(size_t) - 1)
        return false;

    pt_bidi_paragraph *paragraph = malloc(sizeof *paragraph);
    if (!paragraph) return false;
    paragraph->algorithm = NULL;
    paragraph->paragraph = NULL;
    paragraph->codepoints = NULL;
    paragraph->byte_offsets = NULL;
    paragraph->codepoint_count = 0;
    paragraph->codepoint_length = 0;
    paragraph->length = 0;
    paragraph->base_level = empty_base;

    if (len == 0) {
        *out = paragraph;
        return true;
    }

    paragraph->codepoints = malloc(len * sizeof *paragraph->codepoints);
    paragraph->byte_offsets = malloc((len + 1) * sizeof *paragraph->byte_offsets);
    if (!paragraph->codepoints || !paragraph->byte_offsets) {
        pt_bidi_paragraph_dispose(paragraph);
        return false;
    }

    size_t count = 0;
    SBUInteger at = 0;
    while (at < (SBUInteger)len) {
        SBUInteger next = at;
        SBCodepoint codepoint = SBCodepointDecodeNextFromUTF8((const SBUInt8 *)text,
                                                              (SBUInteger)len, &next);
        if (codepoint == SBCodepointInvalid || next <= at || next > (SBUInteger)len) {
            codepoint = SBCodepointFaulty;
            next = at + 1;
        }
        paragraph->byte_offsets[count] = (size_t)at;
        paragraph->codepoints[count] = (uint32_t)codepoint;
        ++count;
        at = next;
    }
    paragraph->byte_offsets[count] = len;
    paragraph->codepoint_count = count;

    SBCodepointSequence sequence;
    sequence.stringEncoding = SBStringEncodingUTF32;
    sequence.stringBuffer = paragraph->codepoints;
    sequence.stringLength = (SBUInteger)count;

    paragraph->algorithm = SBAlgorithmCreate(&sequence);
    if (!paragraph->algorithm) {
        pt_bidi_paragraph_dispose(paragraph);
        return false;
    }

    paragraph->paragraph = SBAlgorithmCreateParagraph(paragraph->algorithm, 0, (SBUInteger)count,
                                                     sb_base);
    if (!paragraph->paragraph) {
        pt_bidi_paragraph_dispose(paragraph);
        return false;
    }

    size_t paragraph_length = (size_t)SBParagraphGetLength(paragraph->paragraph);
    if (paragraph_length > count) {
        pt_bidi_paragraph_dispose(paragraph);
        return false;
    }
    paragraph->codepoint_length = paragraph_length;
    paragraph->length = paragraph->byte_offsets[paragraph_length];
    paragraph->base_level = (uint8_t)SBParagraphGetBaseLevel(paragraph->paragraph);
    *out = paragraph;
    return true;
}

void pt_bidi_paragraph_free(pt_bidi_paragraph *paragraph) {
    if (paragraph) pt_bidi_paragraph_dispose(paragraph);
}

bool pt_bidi_paragraph_analyze(const char *text, size_t len, pt_bidi_base base,
                               pt_bidi_paragraph **out) {
    if (base != pt_bidi_base_auto && base != pt_bidi_base_ltr && base != pt_bidi_base_rtl)
        return false;
    return pt_bidi_paragraph_create(text, len, pt_bidi_sb_base_level(base),
                                    base == pt_bidi_base_rtl ? 1 : 0, out);
}

bool pt_bidi_paragraph_analyze_default(const char *text, size_t len, bool rtl_default,
                                       pt_bidi_paragraph **out) {
    return pt_bidi_paragraph_create(text, len,
                                    rtl_default ? SBLevelDefaultRTL : SBLevelDefaultLTR,
                                    rtl_default ? 1 : 0, out);
}

uint8_t pt_bidi_paragraph_base_level(const pt_bidi_paragraph *paragraph) {
    return paragraph ? paragraph->base_level : 0;
}

size_t pt_bidi_paragraph_length(const pt_bidi_paragraph *paragraph) {
    return paragraph ? paragraph->length : 0;
}

bool pt_bidi_line_create(const pt_bidi_paragraph *paragraph, size_t offset, size_t length,
                         pt_bidi_line *out) {
    if (!paragraph || !out) return false;

    out->offset = 0;
    out->length = 0;
    out->base_level = paragraph->base_level;
    out->runs = NULL;
    out->run_count = 0;

    if (offset > paragraph->length || length > paragraph->length - offset) return false;
    out->offset = offset;
    out->length = length;
    if (length == 0) return true;

    size_t first;
    size_t last;
    if (!pt_bidi_byte_to_codepoint(paragraph->byte_offsets, paragraph->codepoint_length, offset,
                                   &first) ||
        !pt_bidi_byte_to_codepoint(paragraph->byte_offsets, paragraph->codepoint_length,
                                   offset + length, &last))
        return false;

    SBLineRef line = SBParagraphCreateLine(paragraph->paragraph, (SBUInteger)first,
                                           (SBUInteger)(last - first));
    if (!line) return false;

    SBUInteger count = SBLineGetRunCount(line);
    const SBRun *source = SBLineGetRunsPtr(line);
    if (count > 0 && !source) {
        SBLineRelease(line);
        return false;
    }
    if ((size_t)count > SIZE_MAX / sizeof(pt_bidi_run)) {
        SBLineRelease(line);
        return false;
    }

    pt_bidi_run *runs = count > 0 ? malloc((size_t)count * sizeof *runs) : NULL;
    if (count > 0 && !runs) {
        SBLineRelease(line);
        return false;
    }

    for (size_t i = 0; i < (size_t)count; ++i) {
        size_t run_start = (size_t)source[i].offset;
        size_t run_length = (size_t)source[i].length;
        if (run_start > paragraph->codepoint_count ||
            run_length > paragraph->codepoint_count - run_start) {
            free(runs);
            SBLineRelease(line);
            return false;
        }
        runs[i].offset = paragraph->byte_offsets[run_start];
        runs[i].length =
            paragraph->byte_offsets[run_start + run_length] - paragraph->byte_offsets[run_start];
        runs[i].level = (uint8_t)source[i].level;
        runs[i].visual_index = i;
    }
    SBLineRelease(line);

    out->runs = runs;
    out->run_count = (size_t)count;
    return true;
}

void pt_bidi_line_free(pt_bidi_line *line) {
    if (!line) return;
    free(line->runs);
    line->runs = NULL;
    line->run_count = 0;
    line->offset = 0;
    line->length = 0;
    line->base_level = 0;
}

int pt_bidi_line_level_at(const pt_bidi_line *line, size_t byte_offset) {
    if (!line) return -1;
    for (size_t i = 0; i < line->run_count; ++i) {
        const pt_bidi_run *run = &line->runs[i];
        if (byte_offset >= run->offset && byte_offset - run->offset < run->length)
            return (int)run->level;
    }
    return -1;
}

bool pt_bidi_is_removed(uint32_t codepoint) {
    if (!SBCodepointIsValid(codepoint)) return false;
    switch (SBCodepointGetBidiType(codepoint)) {
    case SBBidiTypeBN:
    case SBBidiTypeLRE:
    case SBBidiTypeRLE:
    case SBBidiTypeLRO:
    case SBBidiTypeRLO:
    case SBBidiTypePDF:
        return true;
    default:
        return false;
    }
}

bool pt_bidi_text_may_reorder(const char *text, size_t len) {
    if (!text || len == 0) return false;
    SBUInteger at = 0;
    while (at < (SBUInteger)len) {
        SBUInteger next = at;
        SBCodepoint codepoint =
            SBCodepointDecodeNextFromUTF8((const SBUInt8 *)text, (SBUInteger)len, &next);
        if (codepoint == SBCodepointInvalid || next <= at) {
            ++at;
            continue;
        }
        SBBidiType type = SBCodepointGetBidiType(codepoint);
        if (type == SBBidiTypeR || type == SBBidiTypeAL || SBBidiTypeIsFormat(type)) return true;
        at = next;
    }
    return false;
}

pt_bidi_base pt_bidi_first_strong_direction(const char *text, size_t len) {
    if (!text || len == 0) return pt_bidi_base_auto;
    SBUInteger at = 0;
    unsigned int isolate_depth = 0;
    while (at < (SBUInteger)len) {
        SBUInteger next = at;
        SBCodepoint codepoint =
            SBCodepointDecodeNextFromUTF8((const SBUInt8 *)text, (SBUInteger)len, &next);
        if (codepoint == SBCodepointInvalid || next <= at) {
            ++at;
            continue;
        }
        SBBidiType type = SBCodepointGetBidiType(codepoint);
        /* Rule P2: only characters outside isolating runs count, and the first
         * paragraph alone decides the base for a single-line presentation. */
        if (type == SBBidiTypeLRI || type == SBBidiTypeRLI || type == SBBidiTypeFSI) {
            ++isolate_depth;
        } else if (type == SBBidiTypePDI) {
            if (isolate_depth > 0) --isolate_depth;
        } else if (isolate_depth == 0) {
            if (type == SBBidiTypeL) return pt_bidi_base_ltr;
            if (type == SBBidiTypeR || type == SBBidiTypeAL) return pt_bidi_base_rtl;
            if (type == SBBidiTypeB) break;
        }
        at = next;
    }
    return pt_bidi_base_auto;
}

const char *pt_bidi_engine_version(void) {
    return SBVersionGetString();
}

const char *pt_bidi_unicode_version(void) {
    return PT_BIDI_UNICODE_VERSION;
}

/* Bidi control inventory and sanitization (2.1).
 *
 * Pure functions over caller-owned memory. They do not use the engine, do not
 * allocate for the inventory and are never called by a text, shape, layout or
 * paint path. The scan decodes with the shared bounded UTF-8 decoder so a
 * control split by an invalid sequence is not reported, and then classifies
 * the code point exactly. Only the nine explicit controls are reported; BN
 * characters stay ordinary text here even though pt_bidi_is_removed skips them
 * when painting. */

static pt_bidi_control_kind pt_bidi_control_classify(uint32_t codepoint) {
    switch (codepoint) {
    case 0x202Au:
        return pt_bidi_control_lre;
    case 0x202Bu:
        return pt_bidi_control_rle;
    case 0x202Cu:
        return pt_bidi_control_pdf;
    case 0x202Du:
        return pt_bidi_control_lro;
    case 0x202Eu:
        return pt_bidi_control_rlo;
    case 0x2066u:
        return pt_bidi_control_lri;
    case 0x2067u:
        return pt_bidi_control_rli;
    case 0x2068u:
        return pt_bidi_control_fsi;
    case 0x2069u:
        return pt_bidi_control_pdi;
    default:
        return pt_bidi_control_none;
    }
}

size_t pt_bidi_control_inventory(const char *text, size_t length, pt_bidi_control_span *spans,
                                 size_t capacity) {
    if (!text || length == 0) return 0;
    size_t total = 0;
    size_t at = 0;
    while (at < length) {
        size_t next = at;
        uint32_t codepoint = pt_utf8_decode(text, length, at, &next);
        if (next <= at) {
            ++at;
            continue;
        }
        pt_bidi_control_kind kind = pt_bidi_control_classify(codepoint);
        if (kind != pt_bidi_control_none) {
            if (spans && total < capacity) {
                spans[total].offset = at;
                spans[total].length = next - at;
                spans[total].kind = kind;
                spans[total].codepoint = codepoint;
            }
            ++total;
        }
        at = next;
    }
    return total;
}

char *pt_bidi_sanitize(const char *text, size_t length, pt_bidi_sanitize_mode mode,
                       size_t *out_length) {
    if (mode != pt_bidi_sanitize_strip && mode != pt_bidi_sanitize_neutralize) return NULL;
    if (!text && length != 0) return NULL;
    /* Worst case: every control is one byte of input that grows to three. */
    if (length > (SIZE_MAX - 1) / 3) return NULL;

    /* pt_alloc is fatal on failure, matching the rest of Pt. */
    char *out = pt_alloc(length * 3 + 1);

    size_t written = 0;
    size_t at = 0;
    while (at < length) {
        size_t next = at;
        uint32_t codepoint = pt_utf8_decode(text, length, at, &next);
        if (next <= at) next = at + 1;
        if (pt_bidi_control_classify(codepoint) != pt_bidi_control_none) {
            if (mode == pt_bidi_sanitize_neutralize) {
                out[written++] = (char)0xEF;
                out[written++] = (char)0xBF;
                out[written++] = (char)0xBD;
            }
        } else {
            memcpy(out + written, text + at, next - at);
            written += next - at;
        }
        at = next;
    }
    out[written] = '\0';
    if (out_length) *out_length = written;
    return out;
}