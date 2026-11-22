/*
 * Direction properties, inheritance and auto resolution (2.0 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Covers the explicit storage contract, the widget -> ancestor -> window ->
 * application resolution chain, P2/P3 first-strong detection (including
 * isolates), the neutral-only UI fallback, invalid-value rejection and the
 * layout/repaint invalidation marks. No visual reordering is involved.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#define CHECK_FIRST_STRONG(literal, expected)                                        \
    pt_check(pt_text_first_strong_direction((literal), sizeof(literal) - 1) ==       \
             (expected))

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

static void check_first_strong(void) {
    /* Empty and neutral-only text has no strong character. */
    pt_check(pt_text_first_strong_direction(NULL, 0) == pt_text_direction_auto);
    CHECK_FIRST_STRONG("", pt_text_direction_auto);
    CHECK_FIRST_STRONG(" \t1234!?-.,", pt_text_direction_auto);
    CHECK_FIRST_STRONG("\xE2\x80\x8B", pt_text_direction_auto); /* ZWSP is BN */

    /* Latin first strong, including weak digits before it. */
    CHECK_FIRST_STRONG("Hello", pt_text_direction_ltr);
    CHECK_FIRST_STRONG("123 abc", pt_text_direction_ltr);
    CHECK_FIRST_STRONG("_#! abc", pt_text_direction_ltr);
    CHECK_FIRST_STRONG("\xC3\xA9t\xC3\xA9", pt_text_direction_ltr); /* été */

    /* Hebrew and Arabic first strong. */
    CHECK_FIRST_STRONG("\xD7\x90\xD7\x91\xD7\x92", pt_text_direction_rtl); /* אבג */
    CHECK_FIRST_STRONG("\xD7\x90\xD7\x91 abc", pt_text_direction_rtl);
    CHECK_FIRST_STRONG("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",
                       pt_text_direction_rtl); /* مرحبا */
    CHECK_FIRST_STRONG("\xEF\xAD\x90", pt_text_direction_rtl); /* U+FB50 */

    /* Weak character classes never win: ASCII digits, Arabic-Indic digits,
       separators and punctuation all stay auto on their own. */
    CHECK_FIRST_STRONG("\xD9\xA1\xD9\xA2\xD9\xA3",
                       pt_text_direction_auto); /* ١٢٣ Arabic-Indic digits */
    CHECK_FIRST_STRONG("\xD9\xA1\xD9\xA2\xD9\xA3 \xD7\x90", pt_text_direction_rtl);

    /* Isolates: their contents are skipped, and unbalanced initiators hide the
       rest of the paragraph. */
    CHECK_FIRST_STRONG("\xE2\x81\xA7\xD7\x90\xD7\x91\xD7\x92\xE2\x81\xA9 abc",
                       pt_text_direction_ltr); /* RLI Hebrew PDI " abc" */
    CHECK_FIRST_STRONG("\xE2\x81\xA7\xD7\x90\xD7\x91\xD7\x92\xE2\x81\xA9",
                       pt_text_direction_auto); /* RLI Hebrew PDI only */
    CHECK_FIRST_STRONG("\xE2\x81\xA8\xD7\x90\xE2\x81\xA6\xD7\x91\xE2\x81\xA9"
                       "\xE2\x81\xA9 abc",
                       pt_text_direction_ltr); /* nested FSI/LRI/PDI/PDI */
    CHECK_FIRST_STRONG("\xE2\x81\xA6\xD7\x90",
                       pt_text_direction_auto); /* unclosed LRI hides Hebrew */

    /* Invalid UTF-8 is skipped (decoded as U+FFFD) rather than crashing. */
    CHECK_FIRST_STRONG("\xFF\xD7\x90", pt_text_direction_rtl);

    /* Combining marks alone are not strong; the Latin base before them is. */
    CHECK_FIRST_STRONG("a\xCC\x81", pt_text_direction_ltr);
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
        check_first_strong();
        check_text_resolution(app, window, box, label);
        check_invalidation(app, window, box, label);
        check_text_unchanged(label);
    }

    pt_app_destroy(app);
    return pt_test_finish("direction");
}