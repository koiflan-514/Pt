/*
 * Text-input-v3 composition, surrounding text and deletion checks.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "fake_compositor.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

/* zwp_text_input_v3 request opcodes used by the wire assertions. */
#define ime_text_input_enable 1u
#define ime_text_input_show_input_panel 9u
#define ime_text_input_hide_input_panel 10u

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

static void test_delete_codepoint_boundaries(pt_window *window) {
    pt_widget *entry = make_entry(window, "a\xF0\x9F\x98\x80" "b");

    /* The emoji is four bytes. Deleting two bytes before a caret after it would
     * start inside the sequence; the request snaps out to the codepoint edge
     * instead of splitting the UTF-8. */
    pt_entry_set_selection(entry, 5, 5);
    pt_check(pt_widget_ime_apply(entry, "X", 2, 0, true));
    pt_check(strcmp(pt_widget_get_text(entry), "a\xF0\x9F\x98\x80Xb") == 0);

    /* Likewise a partial deletion after the caret never leaves half a glyph. */
    pt_widget_set_text(entry, "a\xF0\x9F\x98\x80" "b");
    pt_entry_set_selection(entry, 1, 1);
    pt_check(pt_widget_ime_apply(entry, "Y", 0, 1, true));
    pt_check(strcmp(pt_widget_get_text(entry), "aY\xF0\x9F\x98\x80" "b") == 0);

    /* Whole-codepoint deletion still works. */
    pt_widget_set_text(entry, "a\xF0\x9F\x98\x80" "b");
    pt_entry_set_selection(entry, 5, 5);
    pt_check(pt_widget_ime_apply(entry, "", 4, 0, true));
    pt_check(strcmp(pt_widget_get_text(entry), "ab") == 0);
}

typedef struct submit_probe {
    int submits;
    int raw_keys;
    int legacy_keys;
} submit_probe;

static void count_submit(pt_widget *widget, void *data) {
    (void)widget;
    submit_probe *probe = data;
    probe->submits++;
}

static bool count_raw_key(pt_window *window, uint32_t key, uint32_t modifiers,
                          const char *utf8, void *data) {
    (void)window;
    (void)key;
    (void)modifiers;
    (void)utf8;
    submit_probe *probe = data;
    probe->raw_keys++;
    return false;
}

static bool count_legacy_key(pt_window *window, uint32_t key, uint32_t modifiers,
                             void *data) {
    (void)window;
    (void)key;
    (void)modifiers;
    submit_probe *probe = data;
    probe->legacy_keys++;
    return false;
}

static void test_v2_action_is_not_enter(pt_window *window) {
    pt_widget *entry = make_entry(window, "value");
    submit_probe probe = {0};
    pt_entry_set_on_submit(entry, count_submit, &probe);
    pt_window_set_key_event_callback(window, count_raw_key, &probe);
    pt_window_set_key_callback(window, count_legacy_key, &probe);

    /* Action 1 is submit: it must activate the entry but insert no newline and
     * produce no key callback. */
    pt_window_input_ime_action(window, 1);
    pt_check(probe.submits == 1);
    pt_check(probe.raw_keys == 0);
    pt_check(probe.legacy_keys == 0);
    pt_check(strcmp(pt_widget_get_text(entry), "value") == 0);

    /* None/unknown actions are acknowledged without side effects. */
    pt_window_input_ime_action(window, 0);
    pt_window_input_ime_action(window, 99);
    pt_check(probe.submits == 1);
    pt_window_set_key_event_callback(window, NULL, NULL);
    pt_window_set_key_callback(window, NULL, NULL);
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


typedef struct custom_ime_state {
    char commit[128];
    char preedit[128];
    int32_t cursor_begin;
    int32_t cursor_end;
    int commits;
    int preedits;
} custom_ime_state;

static void custom_commit(pt_window *window, const char *text, void *user_data) {
    (void)window;
    custom_ime_state *state = user_data;
    snprintf(state->commit, sizeof(state->commit), "%s", text ? text : "");
    state->commits++;
}

static void custom_preedit(pt_window *window, const char *text, int32_t cursor_begin,
                           int32_t cursor_end, void *user_data) {
    (void)window;
    custom_ime_state *state = user_data;
    snprintf(state->preedit, sizeof(state->preedit), "%s", text ? text : "");
    state->cursor_begin = cursor_begin;
    state->cursor_end = cursor_end;
    state->preedits++;
}

static void test_preedit_hint_ranges(pt_window *window) {
    pt_widget *entry = make_entry(window, "");

    /* Clamp, zero-length drop, overlap preservation and unknown-hint storage. */
    pt_ime_hint hints[] = {
        {0, 0, pt_ime_hint_whole},
        {2, 99, pt_ime_hint_selection},
        {1, 2, pt_ime_hint_prediction},
        {2, 3, pt_ime_hint_spelling_error},
        {0, 1, 99},
    };
    pt_check(pt_widget_preedit_hints_ex(entry, "xyz", 3, 3, hints, 5));
    pt_check(entry->preedit_hint_count == 4);
    pt_check(entry->preedit_hints[0].start == 2 && entry->preedit_hints[0].end == 3);
    pt_check(entry->preedit_hints[0].hint == pt_ime_hint_selection);
    pt_check(entry->preedit_hints[1].start == 1 && entry->preedit_hints[1].end == 2);
    pt_check(entry->preedit_hints[2].start == 2 && entry->preedit_hints[2].end == 3);
    pt_check(entry->preedit_hints[3].start == 0 && entry->preedit_hints[3].end == 1);
    pt_check(entry->preedit_hints[3].hint == 99);

    /* Offsets inside a multibyte sequence snap outward; start==end is dropped. */
    pt_ime_hint multibyte[] = {
        {2, 4, pt_ime_hint_whole},
        {1, 4, pt_ime_hint_selection},
    };
    pt_check(pt_widget_preedit_hints_ex(entry, "a\xC3\xA9\xE4\xB8\xAD", 6, 6, multibyte, 2));
    pt_check(entry->preedit_hint_count == 1);
    pt_check(entry->preedit_hints[0].start == 1 && entry->preedit_hints[0].end == 3);

    /* A control byte removed by sanitization still maps the raw byte offsets. */
    pt_ime_hint sanitized[] = {{1, 3, pt_ime_hint_whole}};
    pt_check(pt_widget_preedit_hints_ex(entry, "a\nb", 3, 3, sanitized, 1));
    pt_check(strcmp(entry->preedit, "ab") == 0);
    pt_check(entry->preedit_hint_count == 1);
    pt_check(entry->preedit_hints[0].start == 1 && entry->preedit_hints[0].end == 2);

    /* Ending the composition drops the hints with the preedit. */
    pt_check(pt_widget_preedit_hints_ex(entry, "", -1, -1, hints, 5));
    pt_check(entry->preedit == NULL);
    pt_check(entry->preedit_hint_count == 0);

    /* Hints never touch text or commit handling. */
    pt_widget_set_text(entry, "base");
    pt_ime_hint commit_hint[] = {{0, 3, pt_ime_hint_selection}};
    pt_window_input_ime_compose_ex(window, NULL, "xyz", 3, 3, 0, 0, commit_hint, 1);
    pt_check(strcmp(pt_widget_get_text(entry), "base") == 0);
    pt_check(entry->preedit_hint_count == 1);
    pt_window_input_ime_compose_ex(window, "Q", "", -1, -1, 0, 0, NULL, 0);
    pt_check(strcmp(pt_widget_get_text(entry), "baseQ") == 0);
    pt_check(entry->preedit == NULL);
    pt_check(entry->preedit_hint_count == 0);

    /* A programmatic text replacement drops the composition and its hints. */
    pt_check(pt_widget_preedit_hints_ex(entry, "xyz", 3, 3, commit_hint, 1));
    pt_check(entry->preedit_hint_count == 1);
    pt_widget_set_text(entry, "reset");
    pt_check(entry->preedit == NULL);
    pt_check(entry->preedit_hint_count == 0);
}

static bool pixel_buffers_equal(const uint32_t *a, const uint32_t *b, size_t count) {
    return memcmp(a, b, count * sizeof(*a)) == 0;
}

static void test_preedit_hint_pixels(pt_app *app) {
    pt_window *window = pt_window_create(app, "ime-hints", 260, 120);
    pt_check(window != NULL);
    pt_widget *entry = make_entry(window, "ab");
    pt_window_input_ime_compose(window, NULL, "xyz", 3, 3, 0, 0);
    pt_check(entry->preedit_hint_count == 0);

    /* Keep caret painting out of the comparison so the only delta is the hint. */
    window->caret_visible = false;
    int width = pt_window_get_width(window);
    int height = pt_window_get_height(window);
    size_t count = (size_t)width * (size_t)height;
    uint32_t *plain = pt_alloc(count * sizeof(*plain));
    uint32_t *hinted = pt_alloc(count * sizeof(*hinted));
    uint32_t *cleared = pt_alloc(count * sizeof(*cleared));
    pt_check(pt_window_paint_pixels(window, plain, width, height));

    pt_ime_hint hints[] = {
        {0, 1, pt_ime_hint_selection},
        {2, 3, pt_ime_hint_spelling_error},
    };
    pt_check(pt_widget_preedit_hints_ex(entry, "xyz", 3, 3, hints, 2));
    window->caret_visible = false;
    pt_check(pt_window_paint_pixels(window, hinted, width, height));
    pt_check(!pixel_buffers_equal(plain, hinted, count));

    /* Clearing the hints restores the exact 1.2 presentation. */
    pt_check(pt_widget_preedit_hints_ex(entry, "xyz", 3, 3, NULL, 0));
    window->caret_visible = false;
    pt_check(pt_window_paint_pixels(window, cleared, width, height));
    pt_check(pixel_buffers_equal(plain, cleared, count));

    free(plain);
    free(hinted);
    free(cleared);
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);
    pt_window_free(window);
}

static void test_text_area_hints(pt_app *app) {
    pt_window *window = pt_window_create(app, "ime-area-hints", 260, 160);
    pt_check(window != NULL);
    pt_widget *area = pt_text_area_create(pt_window_root(window), "area");
    pt_check(area != NULL);
    pt_widget_set_text(area, "line");
    pt_check(pt_widget_focus(area));
    pt_window_input_ime_compose(window, NULL, "xy", 2, 2, 0, 0);
    window->caret_visible = false;

    int width = pt_window_get_width(window);
    int height = pt_window_get_height(window);
    size_t count = (size_t)width * (size_t)height;
    uint32_t *plain = pt_alloc(count * sizeof(*plain));
    uint32_t *hinted = pt_alloc(count * sizeof(*hinted));
    pt_check(pt_window_paint_pixels(window, plain, width, height));
    pt_ime_hint hints[] = {{0, 1, pt_ime_hint_prediction}};
    pt_check(pt_widget_preedit_hints_ex(area, "xy", 2, 2, hints, 1));
    window->caret_visible = false;
    pt_check(pt_window_paint_pixels(window, hinted, width, height));
    pt_check(!pixel_buffers_equal(plain, hinted, count));

    free(plain);
    free(hinted);
    /* Free the window with the hinted composition still live: the widget tree
     * owns and releases the hint array. */
    pt_window_free(window);
}

static void test_input_panel(pt_app *app, pt_window *window) {
    pt_check(!pt_window_show_input_panel(NULL));
    pt_check(!pt_window_hide_input_panel(NULL));
    pt_check(!window->input_panel_requested);

    /* Headless has no text-input object: the desired state is recorded but no
     * protocol request can be forwarded. */
    pt_check(!pt_platform_ime_input_panel(window, true));
    pt_check(pt_window_show_input_panel(window));
    pt_check(window->input_panel_requested);
    pt_check(!pt_window_show_input_panel(window));
    pt_check(!pt_platform_ime_input_panel(window, true));

    pt_check(pt_window_hide_input_panel(window));
    pt_check(!window->input_panel_requested);
    pt_check(!pt_window_hide_input_panel(window));

    /* A window with no focused widget follows the same state machine. */
    pt_window *idle = pt_window_create(app, "ime-panel-idle", 240, 160);
    pt_check(idle != NULL);
    pt_check(!pt_window_wants_ime(idle));
    pt_check(pt_window_show_input_panel(idle));
    pt_check(idle->input_panel_requested);
    pt_check(!pt_window_show_input_panel(idle));
    pt_check(pt_window_hide_input_panel(idle));
    pt_check(!idle->input_panel_requested);
    pt_window_free(idle);
}

static void test_custom_surface(pt_app *app) {
    pt_window *window = pt_window_create(app, "custom-ime", 320, 220);
    pt_check(window != NULL);
    custom_ime_state state = {0};
    pt_window_set_ime_callbacks(window, custom_commit, custom_preedit, &state);
    pt_window_set_ime_enabled(window, true);
    pt_check(window->custom_ime_enabled);
    pt_check(pt_window_wants_ime(window));

    pt_window_set_ime_cursor_rect(window, 5, 6, 7, 8);
    pt_check(window->custom_ime_rect_valid);
    pt_check(window->custom_ime_x == 5 && window->custom_ime_y == 6);
    pt_check(window->custom_ime_width == 7 && window->custom_ime_height == 8);

    pt_window_input_ime_compose(window, NULL, "ni", 1, 1, 0, 0);
    pt_check(strcmp(state.preedit, "ni") == 0);
    pt_check(state.cursor_begin == 1 && state.cursor_end == 1);
    pt_check(state.commits == 0);
    pt_check(window->needs_draw);

    /* Hints are presentation metadata for Pt-drawn preedits; a custom surface
     * keeps the same callback payload and commit semantics. */
    pt_ime_hint custom_hints[] = {{0, 1, pt_ime_hint_selection}};
    window->needs_draw = false;
    pt_window_input_ime_compose_ex(window, NULL, "nih", 2, 2, 0, 0, custom_hints, 1);
    pt_check(strcmp(state.preedit, "nih") == 0);
    pt_check(state.cursor_begin == 2 && state.cursor_end == 2);
    pt_check(state.commits == 0);
    pt_check(window->needs_draw);
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);

    /* Empty done is only an acknowledgement; don't schedule a repaint. */
    window->needs_draw = false;
    int preedits = state.preedits;
    pt_window_input_ime_compose(window, "", "", -1, -1, 0, 0);
    pt_check(state.preedits == preedits + 1);
    pt_check(state.preedit[0] == '\0');
    pt_check(!window->needs_draw);

    pt_window_input_ime_compose(window, "\xE5\xA5\xBD", "", -1, -1, 0, 0);
    pt_check(strcmp(state.commit, "\xE5\xA5\xBD") == 0);
    pt_check(state.commits == 1);
    pt_check(state.preedit[0] == '\0');
    pt_check(window->needs_draw);

    /* A real text-entry widget keeps precedence over custom routing. */
    pt_widget *entry = pt_entry_create(pt_window_root(window), "entry");
    pt_widget_set_text(entry, "");
    pt_check(pt_widget_focus(entry));
    pt_window_input_ime_compose(window, "x", "", -1, -1, 0, 0);
    pt_check(strcmp(pt_widget_get_text(entry), "x") == 0);
    pt_check(state.commits == 1);

    /* Disabling the custom surface does not turn IME off for a focused text
     * entry, and its caret rectangle must not steal the widget's position. */
    pt_window_set_ime_cursor_rect(window, 100, 120, 2, 18);
    pt_window_set_ime_enabled(window, false);
    pt_check(!window->custom_ime_enabled);
    pt_check(pt_window_wants_ime(window));
    pt_check(window->custom_ime_x == 100 && window->custom_ime_y == 120);

    pt_window_set_ime_callbacks(window, NULL, NULL, NULL);
    pt_check(!window->custom_ime_enabled);
    pt_window_free(window);
}

/* ---------- fake-compositor wire replay ---------- */

static void ime_wire_wait_configured(fake_compositor *fc, pt_app *app,
                                     pt_window *window) {
    for (int i = 0; i < 300 && window && !window->configured; ++i) {
        fake_compositor_dispatch(fc, app, 5);
    }
}

/* Starts a non-headless Pt app against the fake compositor and returns a shown,
 * configured window with a focused entry holding "abcdef". */
static pt_window *ime_wire_open(fake_compositor *fc, pt_app **app_out) {
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.ime.wire");
    pt_check(app != NULL);
    if (!app) {
        return NULL;
    }
    pt_check(fake_compositor_settle(fc, app));
    pt_window *window = pt_window_create(app, "ime-wire", 320, 200);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        return NULL;
    }
    pt_widget *entry = pt_entry_create(pt_window_root(window), "wire");
    pt_check(entry != NULL);
    pt_widget_set_text(entry, "abcdef");
    pt_check(pt_widget_focus(entry));
    pt_window_show(window);
    ime_wire_wait_configured(fc, app, window);
    pt_check(window->configured);
    pt_check(fake_compositor_settle(fc, app));
    *app_out = app;
    return window;
}

static void test_wire_ime_composition(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    pt_app *app = NULL;
    pt_window *window = ime_wire_open(&fc, &app);
    if (!window) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_widget *entry = window->focused;
    pt_check(entry != NULL);
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);
    uint32_t text_input = fake_compositor_live_id(&fc, FC_K_TEXT_INPUT);
    pt_check(text_input != 0);

    /* text-input enter activates the focused entry and enables the object. */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_text_input_enter(&fc, surface));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TEXT_INPUT,
                                          ime_text_input_enable, 1, 32));
    pt_check(app->platform->text_input_enabled);
    pt_check(app->platform->text_input_window == window);

    /* preedit_string and preedit_hint are double-buffered: they only touch the
     * pending state until done, and then apply together. */
    pt_check(fake_compositor_text_input_preedit_string(&fc, "xyz", 1, 2));
    pt_check(fake_compositor_text_input_preedit_hint(
        &fc, 0, 1, (uint32_t)pt_ime_hint_selection));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(entry->preedit == NULL);
    pt_check(entry->preedit_hint_count == 0);
    pt_check(app->platform->pending_preedit != NULL);
    pt_check(strcmp(app->platform->pending_preedit, "xyz") == 0);
    pt_check(app->platform->pending_preedit_begin == 1);
    pt_check(app->platform->pending_preedit_end == 2);
    pt_check(app->platform->pending_hint_count == 1);

    pt_check(fake_compositor_text_input_done(&fc, 41u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->ime_serial == 41u);
    pt_check(app->platform->pending_preedit == NULL);
    pt_check(entry->preedit != NULL && strcmp(entry->preedit, "xyz") == 0);
    pt_check(entry->preedit_cursor_begin == 1);
    pt_check(entry->preedit_cursor_end == 2);
    pt_check(entry->preedit_hint_count == 1);
    pt_check(entry->preedit_hints[0].start == 0);
    pt_check(entry->preedit_hints[0].end == 1);
    pt_check(entry->preedit_hints[0].hint == pt_ime_hint_selection);

    /* A done with no text ends the visible composition: it clears the preedit
     * and hints and requests a repaint. A shown window draws synchronously when
     * scheduled and clears needs_draw, so park it unshown while observing the
     * flag itself. */
    window->shown = false;
    window->needs_draw = false;
    pt_check(fake_compositor_text_input_done(&fc, 42u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(entry->preedit == NULL);
    pt_check(entry->preedit_hint_count == 0);
    pt_check(window->needs_draw);

    /* The same empty done with no composition to end is only an acknowledgement
     * of the state we pushed, and must not trigger a repaint. */
    window->needs_draw = false;
    pt_check(fake_compositor_text_input_done(&fc, 43u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(entry->preedit == NULL);
    pt_check(!window->needs_draw);
    window->shown = true;

    /* delete_surrounding_text and commit_string apply in one done, with the
     * deletion counted from the cursor and the commit inserted at its end. */
    pt_widget_set_text(entry, "abcdef");
    pt_entry_set_selection(entry, 4, 4);
    pt_check(fake_compositor_text_input_delete_surrounding_text(&fc, 2, 1));
    pt_check(fake_compositor_text_input_commit_string(&fc, "X"));
    pt_check(fake_compositor_text_input_done(&fc, 44u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(strcmp(pt_widget_get_text(entry), "abXf") == 0);
    pt_check(entry->cursor == 3);

    /* Deletion lengths snap to UTF-8 boundaries on the wire too. The emoji is
     * four bytes, so a two-byte deletion cannot split it. */
    pt_widget_set_text(entry, "a\xF0\x9F\x98\x80" "b");
    pt_entry_set_selection(entry, 5, 5);
    pt_check(fake_compositor_text_input_delete_surrounding_text(&fc, 2, 0));
    pt_check(fake_compositor_text_input_commit_string(&fc, "X"));
    pt_check(fake_compositor_text_input_done(&fc, 45u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(strcmp(pt_widget_get_text(entry), "a\xF0\x9F\x98\x80" "Xb") == 0);

    /* The v2 action is double-buffered with done and activates the entry without
     * fabricating a key press or newline. */
    pt_widget_set_text(entry, "value");
    submit_probe probe = {0};
    pt_entry_set_on_submit(entry, count_submit, &probe);
    pt_window_set_key_event_callback(window, count_raw_key, &probe);
    pt_window_set_key_callback(window, count_legacy_key, &probe);
    pt_check(fake_compositor_text_input_action(&fc, 1u, 46u));
    pt_check(fake_compositor_text_input_done(&fc, 46u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(probe.submits == 1);
    pt_check(probe.raw_keys == 0);
    pt_check(probe.legacy_keys == 0);
    pt_check(strcmp(pt_widget_get_text(entry), "value") == 0);

    /* language is advisory and does not alter text or schedule a repaint. */
    window->needs_draw = false;
    pt_check(fake_compositor_text_input_language(&fc, "ja"));
    pt_check(fake_compositor_text_input_done(&fc, 47u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(strcmp(pt_widget_get_text(entry), "value") == 0);
    pt_check(!window->needs_draw);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_window_set_key_event_callback(window, NULL, NULL);
    pt_window_set_key_callback(window, NULL, NULL);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

static void test_wire_input_panel(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    pt_app *app = NULL;
    pt_window *window = ime_wire_open(&fc, &app);
    if (!window) {
        fake_compositor_stop(&fc);
        return;
    }
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);
    pt_check(app->platform->text_input != NULL);
    pt_check(wl_proxy_get_version((struct wl_proxy *)app->platform->text_input) == 2);

    /* Focusing the entry already enabled the text-input object, so the show
     * intent forwards the v2 request immediately. */
    fake_compositor_clear(&fc);
    pt_check(pt_window_show_input_panel(window));
    pt_check(window->input_panel_requested);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TEXT_INPUT,
                                          ime_text_input_show_input_panel, 1, 32));
    pt_check(app->platform->text_input_enabled);
    fc_request show = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_TEXT_INPUT,
                                          ime_text_input_show_input_panel, &show));
    pt_check(show.nargs == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_show_input_panel) == 1);

    /* The window-level state machine suppresses a repeated show. */
    pt_check(!pt_window_show_input_panel(window));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_show_input_panel) == 1);

    /* hide forwards the v2 opcode 10 and is likewise idempotent. */
    pt_check(pt_window_hide_input_panel(window));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TEXT_INPUT,
                                          ime_text_input_hide_input_panel, 1, 32));
    fc_request hide = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_TEXT_INPUT,
                                          ime_text_input_hide_input_panel, &hide));
    pt_check(hide.nargs == 0);
    pt_check(!pt_window_hide_input_panel(window));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_hide_input_panel) == 1);

    /* After leave there is no active text-input object again: the show intent
     * waits, then is forwarded by the next enter/enable activation. */
    pt_check(fake_compositor_text_input_leave(&fc, surface));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->text_input_window == NULL);
    size_t shows = fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                                  ime_text_input_show_input_panel);
    pt_check(pt_window_show_input_panel(window));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_show_input_panel) == shows);
    pt_check(fake_compositor_text_input_enter(&fc, surface));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TEXT_INPUT,
                                          ime_text_input_show_input_panel,
                                          shows + 1, 32));

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

static void test_wire_input_panel_v1(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_TEXT_INPUT | FC_OPT_TEXT_INPUT_V1));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    pt_app *app = NULL;
    pt_window *window = ime_wire_open(&fc, &app);
    if (!window) {
        fake_compositor_stop(&fc);
        return;
    }
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);
    pt_check(app->platform->text_input != NULL);
    pt_check(wl_proxy_get_version((struct wl_proxy *)app->platform->text_input) == 1);

    fake_compositor_clear(&fc);
    pt_check(fake_compositor_text_input_enter(&fc, surface));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TEXT_INPUT,
                                          ime_text_input_enable, 1, 32));
    pt_check(app->platform->text_input_enabled);

    /* The window still records its intent, but a v1 text-input object cannot
     * carry show/hide_input_panel: neither request may be sent. */
    pt_check(pt_window_show_input_panel(window));
    pt_check(window->input_panel_requested);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_show_input_panel) == 0);
    pt_check(pt_window_hide_input_panel(window));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_hide_input_panel) == 0);

    /* The activation replay is version-guarded as well. */
    pt_check(fake_compositor_text_input_leave(&fc, surface));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(pt_window_show_input_panel(window));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_text_input_enter(&fc, surface));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TEXT_INPUT,
                                          ime_text_input_enable, 2, 32));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_TEXT_INPUT,
                                            ime_text_input_show_input_panel) == 0);

    pt_check(fake_compositor_error(&fc) == NULL);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

#define HE_BET "\xD7\x90\xD7\x91\xD7\x92"

static float entry_visual_caret_x(pt_widget *entry) {
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    pt_bidi_base base = pt_widget_text_base(entry, text, len);
    pt_text_visual_map *map = NULL;
    float x = 0.0f;
    if (font && pt_text_visual_map_build_bidi(font, text, len, base, &map)) {
        x = pt_text_visual_map_x_for_position(map, entry->cursor,
                                              (pt_text_bias)entry->caret_affinity) /
            (ctx.scale > 0.0f ? ctx.scale : 1.0f);
    }
    pt_text_visual_map_free(map);
    return x;
}

/* IME rectangles are visual: they follow the mapped caret, not the logical
 * byte prefix, and an RTL-context preedit pushes the composed caret left. */
static void test_rtl_geometry(pt_app *app) {
    pt_window *window = pt_window_create(app, "ime-rtl", 520, 220);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 10.0f, 10.0f, 10.0f, 10.0f);
    pt_widget *entry = pt_entry_create(root, "");
    pt_widget_set_text(entry, "abc " HE_BET " 123");
    pt_widget_set_size(entry, 480.0f, 34.0f);
    pt_widget_set_align_self(entry, pt_align_start);
    pt_widget *area = pt_text_area_create(root, "");
    pt_widget_set_text(area, "abc " HE_BET " 123");
    pt_widget_set_size(area, 480.0f, 100.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 220, 1.0f);
    pt_check(canvas && pt_window_paint(window, canvas));

    pt_check(pt_widget_focus(entry));
    pt_entry_set_selection(entry, 4, 4);
    int x = 0;
    pt_widget_ime_rect(entry, &x, NULL, NULL, NULL);
    float expected = entry->x + entry->padding_left + entry_visual_caret_x(entry) -
                     entry->scroll_x;
    pt_check_near((float)x, expected, 1.0f);
    /* The visual right edge of an LTR paragraph is byte 4, not byte 14. */
    int end_x = 0;
    int start_byte_x = 0;
    pt_entry_set_selection(entry, 0, 0);
    pt_widget_ime_rect(entry, &start_byte_x, NULL, NULL, NULL);
    pt_entry_set_selection(entry, 14, 14);
    pt_widget_ime_rect(entry, &end_x, NULL, NULL, NULL);
    pt_check(x > start_byte_x + 4);
    pt_check(x > end_x + 4);

    /* RTL entry: Home (visual left) sits at the far left, End at the far
     * right, and the preedit rides to the left of the insertion point. */
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget_set_text(entry, HE_BET " abc");
    pt_entry_set_selection(entry, 6, 6);
    int rtl_home_x = 0;
    pt_widget_ime_rect(entry, &rtl_home_x, NULL, NULL, NULL);
    pt_entry_set_selection(entry, 0, 0);
    int rtl_end_x = 0;
    pt_widget_ime_rect(entry, &rtl_end_x, NULL, NULL, NULL);
    pt_check(rtl_end_x > rtl_home_x + 4);
    pt_entry_set_selection(entry, 6, 6);
    pt_check(pt_widget_preedit(entry, "xy"));
    int with_preedit_x = 0;
    pt_widget_ime_rect(entry, &with_preedit_x, NULL, NULL, NULL);
    pt_check(with_preedit_x < rtl_home_x - 4);
    pt_widget_preedit(entry, "");

    pt_check(pt_widget_focus(area));
    pt_text_area_set_selection(area, 4, 4);
    int area_x = 0;
    pt_widget_ime_rect(area, &area_x, NULL, NULL, NULL);
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, pt_text_area_content_width(area), &layout));
    int line = pt_text_layout_line_for_position(&layout, layout.caret);
    float area_expected = area->x + area->padding_left +
                          pt_text_layout_x_for_position_bias(&layout, line, layout.caret,
                                                             (pt_text_bias)area->caret_affinity);
    pt_check_near((float)area_x, area_expected, 1.0f);
    pt_text_layout_free(&layout);

    pt_text_area_set_selection(area, 0, 0);
    int area_start_x = 0;
    pt_widget_ime_rect(area, &area_start_x, NULL, NULL, NULL);
    int area_rtl_x = 0;
    pt_text_area_set_selection(area, 14, 14);
    pt_widget_ime_rect(area, &area_rtl_x, NULL, NULL, NULL);
    pt_check(area_x > area_start_x + 4);
    pt_check(area_x > area_rtl_x + 4);

    /* An RTL run preedit composes to the left of its insertion point. */
    pt_widget_set_text_direction(area, pt_text_direction_rtl);
    pt_widget_set_text(area, HE_BET);
    pt_text_area_set_selection(area, 4, 4);
    int area_home_x = 0;
    pt_widget_ime_rect(area, &area_home_x, NULL, NULL, NULL);
    pt_check(pt_widget_preedit(area, "xy"));
    int area_preedit_x = 0;
    pt_widget_ime_rect(area, &area_preedit_x, NULL, NULL, NULL);
    pt_check(area_preedit_x < area_home_x - 4);
    pt_widget_preedit(area, "");

    pt_canvas_destroy(canvas);
    pt_window_free(window);
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
    test_delete_codepoint_boundaries(window);
    test_v2_action_is_not_enter(window);
    test_scripted_sequence(window);
    test_empty_done_is_noop(window);
    test_preedit_hint_ranges(window);
    test_preedit_hint_pixels(app);
    test_text_area_hints(app);
    test_input_panel(app, window);
    test_custom_surface(app);

    test_rtl_geometry(app);

    test_wire_ime_composition();
    test_wire_input_panel();
    test_wire_input_panel_v1();

    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("ime");
}
