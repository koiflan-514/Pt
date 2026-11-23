/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_bidi_h
#define pt_bidi_h

#include <pt/base.h>

/* Bidi control inventory and sanitization (since 2.1)
 *
 * Trojan Source. The nine explicit directional formatting controls can
 * reorder what a reader sees without changing the stored bytes. An untrusted
 * string can therefore display one thing while a compiler, a log viewer or
 * `git diff` sees another. These two helpers let an application audit and
 * mitigate such input at its trust boundary.
 *
 * Opt-in. Neither function is called by any Pt text path. pt_widget_set_text,
 * the entry, the text area, the clipboard, drag and accessibility paths store
 * and report the bytes the application supplied, controls included. The
 * controls stay in the stored string for shaping and are skipped when painted,
 * exactly as in 2.0. An application that wants sanitized text calls
 * pt_bidi_sanitize itself before setting it.
 *
 * Scope. The inventory reports only the nine explicit controls. It is not the
 * internal paint-skip predicate, which also removes BN characters (ZWSP, soft
 * hyphen, ZWJ and ZWNJ, BOM) under UAX #9 rule X9. ZWJ and ZWNJ are legitimate
 * text content and are never reported or removed here.
 *
 * Threading. Both functions read caller-owned data only, allocate no shared
 * state and are safe to call off the UI thread, like the value helpers in
 * base.h. They do not require an application.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The nine explicit directional formatting controls of the Unicode bidi
 * algorithm. none is the zero value and is never reported; it is the kind of
 * an uninitialized pt_bidi_control_span. Legacy embeddings and overrides are
 * lre/rle/pdf/lro/rlo (U+202A-U+202E); isolates are lri/rli/fsi/pdi
 * (U+2066-U+2069). Values are the stable ABI values recorded in api/pt.api. */
typedef enum pt_bidi_control_kind {
    pt_bidi_control_none = 0,
    pt_bidi_control_lre, /* U+202A LEFT-TO-RIGHT EMBEDDING */
    pt_bidi_control_rle, /* U+202B RIGHT-TO-LEFT EMBEDDING */
    pt_bidi_control_pdf, /* U+202C POP DIRECTIONAL FORMATTING */
    pt_bidi_control_lro, /* U+202D LEFT-TO-RIGHT OVERRIDE */
    pt_bidi_control_rlo, /* U+202E RIGHT-TO-LEFT OVERRIDE */
    pt_bidi_control_lri, /* U+2066 LEFT-TO-RIGHT ISOLATE */
    pt_bidi_control_rli, /* U+2067 RIGHT-TO-LEFT ISOLATE */
    pt_bidi_control_fsi, /* U+2068 FIRST STRONG ISOLATE */
    pt_bidi_control_pdi  /* U+2069 POP DIRECTIONAL ISOLATE */
} pt_bidi_control_kind;

/* One explicit control found in a string. offset is the UTF-8 byte offset in
 * the input, length the bytes occupied (three for every reported control) and
 * codepoint the decoded control. A zero-initialized span has offset 0,
 * length 0, kind none and codepoint 0. */
typedef struct pt_bidi_control_span {
    size_t offset;
    size_t length;
    pt_bidi_control_kind kind;
    uint32_t codepoint;
} pt_bidi_control_span;

/* Reports every explicit control in text in logical (byte) order. The return
 * value is the total count, even when spans is NULL or capacity is smaller;
 * up to min(capacity, total) entries are written. A NULL text or a zero length
 * counts nothing and returns 0, and spans is left untouched. capacity is
 * ignored when spans is NULL, so a count-only pass needs neither an array nor
 * a guess at the size. The scan is byte-exact over the three-byte UTF-8
 * encodings E2 80 AA-E2 80 AE and E2 81 A6-E2 81 A9: it is O(n), allocates
 * nothing and cannot be confused by invalid UTF-8. Since 2.1. */
pt_api size_t pt_bidi_control_inventory(const char *text, size_t length,
                                        pt_bidi_control_span *spans, size_t capacity);

/* What pt_bidi_sanitize does with a control. strip, the zero value, removes
 * it; neutralize replaces each control with U+FFFD REPLACEMENT CHARACTER so a
 * reader can see that something was removed. */
typedef enum pt_bidi_sanitize_mode {
    pt_bidi_sanitize_strip = 0,
    pt_bidi_sanitize_neutralize
} pt_bidi_sanitize_mode;

/* Returns a newly allocated NUL-terminated copy with the nine explicit
 * controls removed (strip) or each replaced by one U+FFFD (neutralize, three
 * bytes per control). out_length, when non-NULL, receives the byte length
 * excluding the NUL. The caller owns the buffer and releases it with pt_free.
 * The allocation is at most 3 * length + 1 bytes.
 *
 * An empty input returns an allocated empty string with out_length 0. A NULL
 * text with a non-zero length, a length past SIZE_MAX / 3, or a mode other
 * than strip and neutralize returns NULL without allocating. Every other
 * byte, including invalid UTF-8 and the BN characters above, is copied
 * unchanged. This function is never called automatically by a text setter
 * and does not validate the rest of the UTF-8. Since 2.1. */
pt_api char *pt_bidi_sanitize(const char *text, size_t length, pt_bidi_sanitize_mode mode,
                              size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
