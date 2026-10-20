/*
 * Text-input-v3 composition, surrounding text and deletion checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

static pt_widget *make_entry(pt_window *window, const char *initial) {
    pt_widget *entry = pt_entry_create(pt_window_root(window), "type");
    pt_widget_set_text(entry, initial);
    pt_check(pt_widget_focus(entry));
    return entry;
}

static void test_surrounding(pt_window *window) {
    pt_widget *entry = make_entry(window, "hello");
    pt_entry_set_selection(entry, 1, 4);

    char *text = NULL;
    uint32_t cursor = 0;
    uint32_t anchor = 0;
    pt_check(pt_widget_ime_surrounding_text(entry, &text, &cursor, &anchor));
    pt_check(text != NULL && strcmp(text, "hello") == 0);
    pt_check(cursor == 4 && anchor == 1);
    free(text);

    pt_entry_set_password(entry, true);
    pt_check(pt_widget_ime_surrounding_text(entry, &text, &cursor, &anchor));
    pt_check(text != NULL && text[0] == '\0');
    pt_check(cursor == 0 && anchor == 0);
    free(text);
    pt_entry_set_password(entry, false);

    pt_check(pt_widget_get_input_purpose(entry) == pt_input_purpose_normal);
    pt_widget_set_input_purpose(entry, pt_input_purpose_email);
    pt_check(pt_widget_get_input_purpose(entry) == pt_input_purpose_email);
    pt_widget_set_input_purpose(entry, (pt_input_purpose)99);
    pt_check(pt_widget_get_input_purpose(entry) == pt_input_purpose_normal);
    pt_widget_set_input_purpose(entry, pt_input_purpose_password);
    pt_check(pt_widget_ime_surrounding_text(entry, &text, &cursor, &anchor));
    pt_check(text != NULL && text[0] == '\0');
    free(text);

    pt_widget_set_text(entry, "reset");
    pt_widget_set_input_purpose(entry, pt_input_purpose_normal);
}

static void test_preedit_cursor(pt_window *window) {
    pt_widget *entry = make_entry(window, "abcd");
    pt_window_input_ime_preedit(window, "xyz");
    pt_check(entry->preedit != NULL && strcmp(entry->preedit, "xyz") == 0);
    pt_check(pt_widget_preedit_caret(entry) == 3);

    pt_check(pt_widget_preedit_ex(entry, "xyz", 1, 2));
    pt_check(entry->preedit_cursor_begin == 1);
    pt_check(entry->preedit_cursor_end == 2);
    pt_check(pt_widget_preedit_caret(entry) == 1);

    int begin_x = 0;
    int begin_y = 0;
    int begin_w = 0;
    int begin_h = 0;
    pt_widget_ime_rect(entry, &begin_x, &begin_y, &begin_w, &begin_h);
    pt_check(pt_widget_preedit_ex(entry, "xyz", 3, 3));
    int end_x = 0;
    pt_widget_ime_rect(entry, &end_x, NULL, NULL, NULL);
    pt_check(end_x > begin_x);

    pt_window_input_ime_preedit(window, "");
    pt_check(entry->preedit == NULL);
    pt_check(entry->preedit_cursor_begin == -1);
    pt_check(entry->preedit_cursor_end == -1);

    /* A composition that replaces a selection is one undo step. */
    pt_widget_set_text(entry, "abcd");
    pt_entry_set_selection(entry, 1, 3);
    pt_check(pt_widget_preedit_ex(entry, "xy", 2, 2));
    pt_check(strcmp(pt_widget_get_text(entry), "ad") == 0);
    pt_window_input_ime_compose(window, "X", "", -1, -1, 0, 0);
    pt_check(strcmp(pt_widget_get_text(entry), "aXd") == 0);
    pt_check(entry->preedit == NULL);
    pt_check(pt_widget_text_undo(entry));
    pt_check(strcmp(pt_widget_get_text(entry), "abcd") == 0);
    pt_check(pt_widget_text_redo(entry));
    pt_check(strcmp(pt_widget_get_text(entry), "aXd") == 0);

    /* Hidden preedit cursors suppress the local caret box. */
    pt_check(pt_widget_preedit_ex(entry, "zz", -1, -1));
    pt_check(entry->preedit_cursor_begin == -1);
    pt_check(pt_widget_preedit_caret(entry) == 2);
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);
    pt_check(entry->preedit == NULL);
}

static void test_delete_surrounding(pt_window *window) {
    pt_widget *entry = make_entry(window, "abcdef");
    pt_entry_set_selection(entry, 4, 4);
    pt_check(pt_widget_ime_apply(entry, "X", 2, 1, true));
    pt_check(strcmp(pt_widget_get_text(entry), "abXf") == 0);
    pt_check(entry->cursor == 3);

    pt_widget_set_text(entry, "abcdef");
    pt_entry_set_selection(entry, 1, 4);
    pt_check(pt_widget_ime_apply(entry, "Z", 1, 1, true));
    pt_check(strcmp(pt_widget_get_text(entry), "Zf") == 0);
    pt_check(entry->cursor == 1);

    /* Deletion lengths snap to UTF-8 boundaries. */
    pt_widget_set_text(entry, "a\xC3\xA9" "bc");
    pt_entry_set_selection(entry, 3, 3);
    pt_check(pt_widget_ime_apply(entry, "X", 1, 0, true));
    pt_check(strcmp(pt_widget_get_text(entry), "a\xC3\xA9" "Xbc") == 0);

    /* Pure deletion keeps the selection-less cursor at the cut point. */
    pt_widget_set_text(entry, "one two");
    pt_entry_set_selection(entry, 7, 7);
    pt_check(pt_widget_ime_apply(entry, "", 3, 0, true));
    pt_check(strcmp(pt_widget_get_text(entry), "one ") == 0);
    pt_check(entry->cursor == 4);

    int submits = 0;
    (void)submits;
}

static void test_scripted_sequence(pt_window *window) {
    pt_widget *entry = make_entry(window, "");
    pt_widget *area = pt_text_area_create(pt_window_root(window), "note");
    pt_check(area != NULL);

    /* enter -> preedit -> commit -> leave, as a compositor would deliver it. */
    pt_window_input_ime_compose(window, NULL, "\xE3\x81\xAB", 0, 0, 0, 0);
    pt_check(entry->preedit != NULL);
    pt_check(strcmp(entry->preedit, "\xE3\x81\xAB") == 0);
    pt_window_input_ime_compose(window, "\xE6\x97\xA5\xE6\x9C\xAC", "", -1, -1, 0, 0);
    pt_check(strcmp(pt_widget_get_text(entry), "\xE6\x97\xA5\xE6\x9C\xAC") == 0);
    pt_check(entry->preedit == NULL);
    pt_check(entry->ime_composing == false);

    /* Replacement over surrounding text in one done event. */
    pt_window_input_ime_compose(window, "\xE4\xB8\x96", "", -1, -1, 3, 0);
    pt_check(strcmp(pt_widget_get_text(entry), "\xE6\x97\xA5\xE4\xB8\x96") == 0);

    /* Text-area caret box follows the preedit cursor offset. */
    pt_check(pt_widget_focus(area));
    pt_window_input_ime_compose(window, NULL, "pre", 0, 0, 0, 0);
    int start_x = 0;
    int start_y = 0;
    int start_w = 0;
    int start_h = 0;
    pt_text_area_caret_box(area, &(pt_layout_ctx){.scale = 1.0f, .font_family = "sans-serif"},
                           &start_x, &start_y, &start_w, &start_h);
    pt_check(pt_widget_preedit_ex(area, "pre", 3, 3));
    int end_x = 0;
    pt_text_area_caret_box(area, &(pt_layout_ctx){.scale = 1.0f, .font_family = "sans-serif"},
                           &end_x, NULL, NULL, NULL);
    pt_check(end_x > start_x);
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);
    pt_check(area->preedit == NULL);
}

static void count_selection_event(pt_window *window, pt_widget *widget,
                                  pt_a11y_event_type event, void *user_data) {
    (void)window;
    (void)widget;
    int *count = user_data;
    if (event == pt_a11y_event_selection_changed) {
        *count += 1;
    }
}

static void test_empty_done_is_noop(pt_window *window) {
    pt_widget *entry = make_entry(window, "hello");
    int selection_events = 0;
    pt_app_set_a11y_callback(window->app, count_selection_event, &selection_events);

    /* A done carrying no commit, no preedit and no deletion is the input method
     * acknowledging the surrounding text and caret rectangle we pushed for it.
     * Answering that acknowledgement pushes another update, which asks for another
     * acknowledgement, so an idle focused entry would redraw and re-announce its
     * selection thousands of times a second. */
    window->needs_draw = false;
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);
    pt_check(!window->needs_draw);
    pt_check(selection_events == 0);
    pt_check(entry->preedit == NULL);

    /* Mid-composition the same event is the only end-of-composition signal, so it
     * still clears the preedit, repaints and reports the caret move. */
    pt_window_input_ime_compose(window, NULL, "ni", 2, 2, 0, 0);
    pt_check(entry->preedit != NULL);
    int after_preedit = selection_events;
    pt_check(after_preedit > 0);
    window->needs_draw = false;
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);
    pt_check(entry->preedit == NULL);
    pt_check(window->needs_draw);
    pt_check(selection_events > after_preedit);

    /* A commit is never dropped by the guard, even with nothing composed. */
    window->needs_draw = false;
    pt_window_input_ime_compose(window, "k", "", -1, -1, 0, 0);
    pt_check(window->needs_draw);
    pt_check(strcmp(pt_widget_get_text(entry), "hellok") == 0);

    pt_app_set_a11y_callback(window->app, NULL, NULL);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.ime");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("ime");
    }
    pt_window *window = pt_window_create(app, "ime", 420, 300);
    pt_check(window != NULL);

    test_surrounding(window);
    test_preedit_cursor(window);
    test_delete_surrounding(window);
    test_scripted_sequence(window);
    test_empty_done_is_noop(window);

    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("ime");
}
