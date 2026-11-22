/*
 * Direction properties, inheritance and auto resolution (2.0 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Covers the explicit storage contract, the widget -> ancestor -> window ->
 * application resolution chain, auto P2/P3 resolution through the vendored
 * engine (including isolates and neutral/empty UI fallback), invalid-value
 * rejection and the layout/repaint invalidation marks. No visual reordering is
 * involved.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#define CHECK_RESOLVED(widget, literal, expected)                                       \
    pt_check(pt_widget_resolved_text_direction((widget), (literal),                    \
                                               sizeof(literal) - 1) == (expected))

static void reset_marks(pt_window *window) {
    window->needs_layout = false;
    window->needs_draw = false;
}

static void check_default_getters(pt_app *app, pt_window *window, pt_widget *label) {
    pt_check(pt_app_get_ui_direction(NULL) == pt_ui_direction_inherit);
    pt_check(pt_window_get_ui_direction(NULL) == pt_ui_direction_inherit);
    pt_check(pt_widget_get_ui_direction(NULL) == pt_ui_direction_inherit);
    pt_check(pt_widget_get_text_direction(NULL) == pt_text_direction_auto);

    pt_check(pt_app_get_ui_direction(app) == pt_ui_direction_inherit);
    pt_check(pt_window_get_ui_direction(window) == pt_ui_direction_inherit);
    pt_check(pt_widget_get_ui_direction(label) == pt_ui_direction_inherit);
    pt_check(pt_widget_get_text_direction(label) == pt_text_direction_auto);
}

static void check_explicit_storage(pt_app *app, pt_window *window, pt_widget *label) {
    /* Invalid values are rejected and the previous value stays. */
    pt_app_set_ui_direction(app, (pt_ui_direction)99);
    pt_check(pt_app_get_ui_direction(app) == pt_ui_direction_inherit);
    pt_window_set_ui_direction(window, (pt_ui_direction)-1);
    pt_check(pt_window_get_ui_direction(window) == pt_ui_direction_inherit);
    pt_widget_set_ui_direction(label, (pt_ui_direction)7);
    pt_check(pt_widget_get_ui_direction(label) == pt_ui_direction_inherit);
    pt_widget_set_text_direction(label, (pt_text_direction)7);
    pt_check(pt_widget_get_text_direction(label) == pt_text_direction_auto);

    /* NULL setters are no-ops. */
    pt_app_set_ui_direction(NULL, pt_ui_direction_rtl);
    pt_window_set_ui_direction(NULL, pt_ui_direction_rtl);
    pt_widget_set_ui_direction(NULL, pt_ui_direction_rtl);
    pt_widget_set_text_direction(NULL, pt_text_direction_rtl);

    pt_app_set_ui_direction(app, pt_ui_direction_rtl);
    pt_check(pt_app_get_ui_direction(app) == pt_ui_direction_rtl);
    pt_app_set_ui_direction(app, pt_ui_direction_inherit);
    pt_check(pt_app_get_ui_direction(app) == pt_ui_direction_inherit);

    pt_window_set_ui_direction(window, pt_ui_direction_rtl);
    pt_check(pt_window_get_ui_direction(window) == pt_ui_direction_rtl);
    pt_window_set_ui_direction(window, pt_ui_direction_inherit);
    pt_check(pt_window_get_ui_direction(window) == pt_ui_direction_inherit);

    pt_widget_set_ui_direction(label, pt_ui_direction_rtl);
    pt_check(pt_widget_get_ui_direction(label) == pt_ui_direction_rtl);
    pt_widget_set_ui_direction(label, pt_ui_direction_ltr);
    pt_check(pt_widget_get_ui_direction(label) == pt_ui_direction_ltr);
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);
    pt_check(pt_widget_get_ui_direction(label) == pt_ui_direction_inherit);

    pt_widget_set_text_direction(label, pt_text_direction_rtl);
    pt_check(pt_widget_get_text_direction(label) == pt_text_direction_rtl);
    pt_widget_set_text_direction(label, pt_text_direction_ltr);
    pt_check(pt_widget_get_text_direction(label) == pt_text_direction_ltr);
    pt_widget_set_text_direction(label, pt_text_direction_auto);
    pt_check(pt_widget_get_text_direction(label) == pt_text_direction_auto);
}

static void check_ui_resolution(pt_app *app, pt_window *window, pt_widget *box,
                                pt_widget *label) {
    /* Zero values resolve to LTR. */
    pt_app_set_ui_direction(app, pt_ui_direction_inherit);
    pt_window_set_ui_direction(window, pt_ui_direction_inherit);
    pt_widget_set_ui_direction(box, pt_ui_direction_inherit);
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_ltr);

    /* Application value reaches the widget through the window and ancestors. */
    pt_app_set_ui_direction(app, pt_ui_direction_rtl);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_rtl);

    /* Window overrides the application. */
    pt_window_set_ui_direction(window, pt_ui_direction_ltr);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_ltr);

    /* Nearest explicit ancestor overrides the window. */
    pt_widget_set_ui_direction(box, pt_ui_direction_rtl);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_rtl);
    pt_check(pt_widget_resolved_ui_direction(box) == pt_ui_direction_rtl);

    /* The widget's own value is nearest. */
    pt_widget_set_ui_direction(label, pt_ui_direction_ltr);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_ltr);
    pt_check(pt_widget_resolved_ui_direction(box) == pt_ui_direction_rtl);

    /* Clearing the widget falls back to the ancestor again. */
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_rtl);

    /* A detached widget has no window or application fallback but keeps its
       own explicit value. */
    pt_widget *detached = pt_label_create(NULL, "detached");
    pt_check(detached != NULL);
    pt_check(pt_widget_resolved_ui_direction(detached) == pt_ui_direction_ltr);
    pt_widget_set_ui_direction(detached, pt_ui_direction_rtl);
    pt_check(pt_widget_resolved_ui_direction(detached) == pt_ui_direction_rtl);
    pt_widget_destroy(detached);
}

static void check_auto_direction(pt_widget *box, pt_widget *label) {
    /* Reset to auto text with an LTR UI so neutral paragraphs exercise the
       documented UI fallback. */
    pt_widget_set_text_direction(label, pt_text_direction_auto);
    pt_widget_set_ui_direction(box, pt_ui_direction_inherit);
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);
    pt_check(pt_widget_resolved_ui_direction(label) == pt_ui_direction_ltr);

    /* Empty and neutral-only text has no strong character and falls back to
       the resolved UI direction. */
    CHECK_RESOLVED(label, "", pt_text_direction_ltr);
    CHECK_RESOLVED(label, " \t1234!?-.,", pt_text_direction_ltr);
    CHECK_RESOLVED(label, "\xE2\x80\x8B", pt_text_direction_ltr); /* ZWSP is BN */

    /* The fallback follows an RTL UI direction, while a strong character still
       wins over it. */
    pt_widget_set_ui_direction(label, pt_ui_direction_rtl);
    CHECK_RESOLVED(label, "1234", pt_text_direction_rtl);
    CHECK_RESOLVED(label, "", pt_text_direction_rtl);
    CHECK_RESOLVED(label, "\xE2\x80\x8B", pt_text_direction_rtl);
    CHECK_RESOLVED(label, "Hello", pt_text_direction_ltr);
    CHECK_RESOLVED(label, "\xD7\x90\xD7\x91", pt_text_direction_rtl);
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);

    /* Latin first strong, including weak digits before it. */
    CHECK_RESOLVED(label, "Hello", pt_text_direction_ltr);
    CHECK_RESOLVED(label, "123 abc", pt_text_direction_ltr);
    CHECK_RESOLVED(label, "_#! abc", pt_text_direction_ltr);
    CHECK_RESOLVED(label, "\xC3\xA9t\xC3\xA9", pt_text_direction_ltr); /* été */

    /* Hebrew and Arabic first strong. */
    CHECK_RESOLVED(label, "\xD7\x90\xD7\x91\xD7\x92", pt_text_direction_rtl); /* אבג */
    CHECK_RESOLVED(label, "\xD7\x90\xD7\x91 abc", pt_text_direction_rtl);
    CHECK_RESOLVED(label, "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",
                   pt_text_direction_rtl); /* مرحبا */
    CHECK_RESOLVED(label, "\xEF\xAD\x90", pt_text_direction_rtl); /* U+FB50 */

    /* Weak character classes never win: ASCII digits, Arabic-Indic digits,
       separators and punctuation all fall back on their own. */
    CHECK_RESOLVED(label, "\xD9\xA1\xD9\xA2\xD9\xA3",
                   pt_text_direction_ltr); /* ١٢٣ Arabic-Indic digits */
    CHECK_RESOLVED(label, "\xD9\xA1\xD9\xA2\xD9\xA3 \xD7\x90", pt_text_direction_rtl);

    /* Isolates: their contents are skipped, and unbalanced initiators hide the
       rest of the paragraph. */
    CHECK_RESOLVED(label, "\xE2\x81\xA7\xD7\x90\xD7\x91\xD7\x92\xE2\x81\xA9 abc",
                   pt_text_direction_ltr); /* RLI Hebrew PDI " abc" */
    CHECK_RESOLVED(label, "\xE2\x81\xA8\xD7\x90\xE2\x81\xA6\xD7\x91\xE2\x81\xA9"
                           "\xE2\x81\xA9 abc",
                   pt_text_direction_ltr); /* nested FSI/LRI/PDI/PDI */

    /* Isolate-only text has no strong character outside the isolate, so it
       falls back to the resolved UI direction. */
    pt_widget_set_ui_direction(label, pt_ui_direction_rtl);
    CHECK_RESOLVED(label, "\xE2\x81\xA7\xD7\x90\xD7\x91\xD7\x92\xE2\x81\xA9",
                   pt_text_direction_rtl); /* RLI Hebrew PDI only */
    CHECK_RESOLVED(label, "\xE2\x81\xA6\xD7\x90",
                   pt_text_direction_rtl); /* unclosed LRI hides Hebrew */
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);

    /* Invalid UTF-8 is skipped (decoded as U+FFFD) rather than crashing. */
    CHECK_RESOLVED(label, "\xFF\xD7\x90", pt_text_direction_rtl);

    /* Combining marks alone are not strong; the Latin base before them is. */
    CHECK_RESOLVED(label, "a\xCC\x81", pt_text_direction_ltr);

    /* Review counterexamples against the engine's Unicode data. These are the
       cases a hand-written strong-range table got wrong; the engine is now the
       single source of truth for P2/P3. */
    CHECK_RESOLVED(label, "\xE4\xBD\xA0\xE5\xA5\xBD", pt_text_direction_ltr); /* 你好 L */
    CHECK_RESOLVED(label, "\xD4\xB1\xD7\x90",
                   pt_text_direction_ltr); /* U+0531 Armenian L, then Hebrew R */
    CHECK_RESOLVED(label, "\xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA",
                   pt_text_direction_ltr); /* สวัส Thai L */
    CHECK_RESOLVED(label, "\xE0\xA4\xB9\xE0\xA4\xBF",
                   pt_text_direction_ltr); /* हि Devanagari L */
    CHECK_RESOLVED(label, "\xD8\x9F" "abc", pt_text_direction_rtl); /* U+061F AL */
    CHECK_RESOLVED(label, "\xD8\x9C" "abc", pt_text_direction_rtl); /* U+061C ALM AL */
    CHECK_RESOLVED(label, "\xD6\xB0" "abc", pt_text_direction_ltr); /* U+05B0 NSM */
    CHECK_RESOLVED(label, "\xEF\xB4\xBE" "abc", pt_text_direction_ltr); /* U+FD3E ON */
    CHECK_RESOLVED(label, "\xE0\xA3\xA2" "abc", pt_text_direction_ltr); /* U+08E2 AN */

    /* Paragraph semantics: only the first paragraph selects the base, both for
       a following newline and a following paragraph separator. */
    CHECK_RESOLVED(label, "\xD7\x90\nabc", pt_text_direction_rtl);
    CHECK_RESOLVED(label, "abc\n\xD7\x90", pt_text_direction_ltr);
}

static void check_text_resolution(pt_app *app, pt_window *window, pt_widget *box,
                                  pt_widget *label) {
    /* Clear all explicit values first. */
    pt_app_set_ui_direction(app, pt_ui_direction_inherit);
    pt_window_set_ui_direction(window, pt_ui_direction_inherit);
    pt_widget_set_ui_direction(box, pt_ui_direction_inherit);
    pt_widget_set_ui_direction(label, pt_ui_direction_inherit);
    pt_widget_set_text_direction(box, pt_text_direction_auto);
    pt_widget_set_text_direction(label, pt_text_direction_auto);

    /* Auto follows the text's first strong character. */
    pt_check(pt_widget_resolved_text_direction(label, "Hello", 5) ==
             pt_text_direction_ltr);
    pt_check(pt_widget_resolved_text_direction(label, "\xD7\x90\xD7\x91", 4) ==
             pt_text_direction_rtl);
    pt_check(pt_widget_resolved_text_direction(label,
                                               "\xD9\x85\xD8\xB1\xD8\xAD", 6) ==
             pt_text_direction_rtl);

    /* Neutral-only text falls back to the resolved UI direction. */
    pt_check(pt_widget_resolved_text_direction(label, "1234", 4) ==
             pt_text_direction_ltr);
    pt_check(pt_widget_resolved_text_direction(label, "", 0) ==
             pt_text_direction_ltr);
    pt_window_set_ui_direction(window, pt_ui_direction_rtl);
    pt_check(pt_widget_resolved_text_direction(label, "1234", 4) ==
             pt_text_direction_rtl);
    pt_check(pt_widget_resolved_text_direction(label, "", 0) ==
             pt_text_direction_rtl);

    /* Explicit text direction wins over detection and UI fallback. */
    pt_widget_set_text_direction(label, pt_text_direction_ltr);
    pt_check(pt_widget_resolved_text_direction(label, "\xD7\x90\xD7\x91", 4) ==
             pt_text_direction_ltr);
    pt_widget_set_text_direction(label, pt_text_direction_rtl);
    pt_check(pt_widget_resolved_text_direction(label, "Hello", 5) ==
             pt_text_direction_rtl);

    /* Text inheritance walks ancestors; the getter still reports auto. */
    pt_widget_set_text_direction(label, pt_text_direction_auto);
    pt_widget_set_text_direction(box, pt_text_direction_rtl);
    pt_check(pt_widget_get_text_direction(label) == pt_text_direction_auto);
    pt_check(pt_widget_resolved_text_direction(label, "Hello", 5) ==
             pt_text_direction_rtl);
    pt_widget_set_text_direction(box, pt_text_direction_auto);
}

static void check_invalidation(pt_app *app, pt_window *window, pt_widget *box,
                               pt_widget *label) {
    /* Widget setters mark layout and repaint dirty through the window. */
    reset_marks(window);
    pt_widget_set_ui_direction(box, pt_ui_direction_ltr);
    pt_check(window->needs_layout);
    pt_check(window->needs_draw);

    reset_marks(window);
    pt_widget_set_text_direction(label, pt_text_direction_ltr);
    pt_check(window->needs_layout);
    pt_check(window->needs_draw);

    /* Re-setting the same value is a no-op. */
    reset_marks(window);
    pt_widget_set_ui_direction(box, pt_ui_direction_ltr);
    pt_check(!window->needs_layout);
    pt_check(!window->needs_draw);
    reset_marks(window);
    pt_widget_set_text_direction(label, pt_text_direction_ltr);
    pt_check(!window->needs_layout);
    pt_check(!window->needs_draw);

    /* Invalid values do not dirty anything. */
    reset_marks(window);
    pt_widget_set_ui_direction(box, (pt_ui_direction)42);
    pt_widget_set_text_direction(label, (pt_text_direction)42);
    pt_check(!window->needs_layout);
    pt_check(!window->needs_draw);

    /* Window and application setters invalidate their windows. */
    reset_marks(window);
    pt_window_set_ui_direction(window, pt_ui_direction_ltr);
    pt_check(window->needs_layout);
    pt_check(window->needs_draw);

    reset_marks(window);
    pt_app_set_ui_direction(app, pt_ui_direction_rtl);
    pt_check(window->needs_layout);
    pt_check(window->needs_draw);

    reset_marks(window);
    pt_app_set_ui_direction(app, pt_ui_direction_rtl);
    pt_window_set_ui_direction(window, pt_ui_direction_ltr);
    pt_check(!window->needs_layout);
    pt_check(!window->needs_draw);
}

static void check_text_unchanged(pt_widget *label) {
    /* Direction is presentation state; the stored UTF-8 string, and therefore
       every byte offset, is untouched by the direction setters. */
    pt_widget_set_text(label, "abc \xD7\x90\xD7\x91");
    const char *before = pt_widget_get_text(label);
    pt_check(before != NULL);
    size_t length = strlen(before);
    pt_widget_set_ui_direction(label, pt_ui_direction_rtl);
    pt_widget_set_text_direction(label, pt_text_direction_rtl);
    const char *after = pt_widget_get_text(label);
    pt_check(after != NULL);
    pt_check(strcmp(before, after) == 0);
    pt_check(strlen(after) == length);
    pt_check(strcmp(after, "abc \xD7\x90\xD7\x91") == 0);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.direction");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("direction");
    }
    pt_window *window = pt_window_create(app, "direction", 240, 120);
    pt_check(window != NULL);
    pt_widget *box = window ? pt_box_create(pt_window_root(window), pt_direction_row) : NULL;
    pt_widget *label = box ? pt_label_create(box, "base") : NULL;
    pt_check(box != NULL);
    pt_check(label != NULL);

    if (window && box && label) {
        check_default_getters(app, window, label);
        check_explicit_storage(app, window, label);
        check_ui_resolution(app, window, box, label);
        check_auto_direction(box, label);
        check_text_resolution(app, window, box, label);
        check_invalidation(app, window, box, label);
        check_text_unchanged(label);
    }

    pt_app_destroy(app);
    return pt_test_finish("direction");
}