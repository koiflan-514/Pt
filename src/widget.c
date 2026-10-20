/*
 * Pt widget tree, interaction and state.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct pt_edit_state {
    char *text;
    size_t cursor;
    size_t anchor;
    pt_text_span *spans;
    uint32_t span_count;
} pt_edit_state;

typedef struct pt_edit_history {
    pt_edit_state undo[32];
    size_t undo_count;
    pt_edit_state redo[32];
    size_t redo_count;
} pt_edit_history;

#define pt_edit_history_cap 32

static void entry_after_edit(pt_widget *entry);
static bool kind_is_checked_control(pt_widget_kind kind);
static void checked_control_set(pt_widget *widget, bool checked, bool notify);
static void segmented_select(pt_widget *segmented, uint32_t index, bool notify);

static void edit_history_free(pt_edit_history *history) {
    if (!history) {
        return;
    }
    for (size_t i = 0; i < history->undo_count; ++i) {
        free(history->undo[i].text);
        free(history->undo[i].spans);
    }
    for (size_t i = 0; i < history->redo_count; ++i) {
        free(history->redo[i].text);
        free(history->redo[i].spans);
    }
    free(history);
}

static bool kind_is_container(pt_widget_kind kind) {
    return kind == pt_widget_box || kind == pt_widget_card || kind == pt_widget_scroll ||
           kind == pt_widget_wrap || kind == pt_widget_grid || kind == pt_widget_list ||
           kind == pt_widget_list_row || kind == pt_widget_table ||
           kind == pt_widget_table_header || kind == pt_widget_table_row ||
           kind == pt_widget_tree || kind == pt_widget_tabs || kind == pt_widget_tab_page ||
           kind == pt_widget_sidebar || kind == pt_widget_combobox;
}

static bool kind_is_focusable(pt_widget_kind kind) {
    return kind == pt_widget_button || kind == pt_widget_entry || kind == pt_widget_toggle ||
           kind == pt_widget_slider || kind == pt_widget_checkbox ||
           kind == pt_widget_radio || kind == pt_widget_segmented ||
           kind == pt_widget_text_area || kind == pt_widget_list ||
           kind == pt_widget_table || kind == pt_widget_tree || kind == pt_widget_tabs ||
           kind == pt_widget_sidebar || kind == pt_widget_chip ||
           kind == pt_widget_combobox;
}

bool pt_widget_is_descendant(const pt_widget *widget, const pt_widget *root) {
    for (const pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (cursor == root) {
            return true;
        }
    }
    return false;
}

static bool widget_is_descendant(const pt_widget *widget, const pt_widget *root) {
    return pt_widget_is_descendant(widget, root);
}

static void window_forget_subtree(pt_window *window, pt_widget *root) {
    if (!window) {
        return;
    }
    if (window->hovered && widget_is_descendant(window->hovered, root)) {
        window->hovered = NULL;
    }
    if (window->active_widget && widget_is_descendant(window->active_widget, root)) {
        window->active_widget = NULL;
    }
    if (window->focused && widget_is_descendant(window->focused, root)) {
        window->focused = NULL;
    }
    if (window->drop_hover && widget_is_descendant(window->drop_hover, root)) {
        window->drop_hover = NULL;
    }
    if (window->drag_widget && widget_is_descendant(window->drag_widget, root)) {
        window->drag_widget = NULL;
        window->drag_pending = false;
        window->drag_started = false;
        free(window->drag_text);
        window->drag_text = NULL;
    }
    if (window->press_target && widget_is_descendant(window->press_target, root)) {
        window->press_target = NULL;
        window->long_press_timer = 0;
        window->long_press_fired = false;
    }
    if (window->gesture_target && widget_is_descendant(window->gesture_target, root)) {
        window->gesture_target = NULL;
    }
    if (window->layout_root && widget_is_descendant(window->layout_root, root)) {
        window->layout_root = NULL;
    }
}

void pt_window_invalidate_layout(pt_window *window, pt_widget *subtree) {
    if (!window) {
        return;
    }
    if (!window->needs_layout) {
        window->needs_layout = true;
        window->layout_root = subtree;
        return;
    }
    if (!window->layout_root || !subtree) {
        window->layout_root = NULL;
        return;
    }
    if (window->layout_root == subtree ||
        pt_widget_is_descendant(subtree, window->layout_root)) {
        return;
    }
    if (pt_widget_is_descendant(window->layout_root, subtree)) {
        window->layout_root = subtree;
        return;
    }
    window->layout_root = NULL;
}

/* Relayout may start at the highest ancestor whose slot is pinned by explicit
 * requested dimensions and was not shrunk by its parent. Everything outside
 * that box keeps its geometry, and a shrunk slot could be redistributed by a
 * content change, so it falls back to a full layout. */
static pt_widget *layout_boundary_for(pt_widget *widget) {
    pt_window *window = pt_widget_window(widget);
    if (!window || !widget->parent) {
        return NULL;
    }
    pt_widget *boundary = NULL;
    for (pt_widget *cursor = widget->parent; cursor && cursor != window->root;
         cursor = cursor->parent) {
        if (!cursor->visible || cursor->requested_width < 0.0f ||
            cursor->requested_height < 0.0f) {
            continue;
        }
        if (cursor->width < cursor->requested_width - 0.5f ||
            cursor->height < cursor->requested_height - 0.5f) {
            continue;
        }
        boundary = cursor;
    }
    return boundary;
}

static void entry_notify_text(pt_widget *entry) {
    if (entry->on_text) {
        entry->on_text(entry, entry->text ? entry->text : "", entry->on_text_data);
    }
    pt_widget_a11y_emit(entry, pt_a11y_event_text_changed);
}

static void entry_reset_caret(pt_widget *entry) {
    pt_window *window = pt_widget_window(entry);
    if (!window) {
        return;
    }
    window->caret_visible = true;
    window->caret_last_toggle = pt_now_seconds();
    window->needs_draw = true;
    pt_widget_a11y_emit(entry, pt_a11y_event_selection_changed);
}

static size_t entry_cursor_clamped(const pt_widget *entry, size_t cursor) {
    size_t len = entry->text ? strlen(entry->text) : 0;
    return cursor > len ? len : cursor;
}

static size_t entry_length(const pt_widget *entry) {
    return entry->text ? strlen(entry->text) : 0;
}

static bool entry_has_selection(const pt_widget *entry) {
    return entry->selection_anchor != entry->cursor;
}

static void entry_selection_range(const pt_widget *entry, size_t *start, size_t *end) {
    size_t len = entry_length(entry);
    size_t a = entry_cursor_clamped(entry, entry->selection_anchor);
    size_t b = entry_cursor_clamped(entry, entry->cursor);
    if (a <= b) {
        *start = a;
        *end = b;
    } else {
        *start = b;
        *end = a;
    }
    if (*end > len) {
        *end = len;
    }
}

static void entry_set_cursor(pt_widget *entry, size_t cursor, bool extend) {
    cursor = entry_cursor_clamped(entry, cursor);
    entry->cursor = cursor;
    entry->caret_goal_x = -1.0f;
    if (!extend) {
        entry->selection_anchor = cursor;
    }
}

static void edit_state_push(pt_edit_state *stack, size_t *count, const pt_widget *entry) {
    if (*count == pt_edit_history_cap) {
        free(stack[0].text);
        free(stack[0].spans);
        memmove(&stack[0], &stack[1], (*count - 1) * sizeof(*stack));
        --*count;
    }
    stack[*count].text = pt_strdup(entry->text ? entry->text : "");
    stack[*count].cursor = entry_cursor_clamped(entry, entry->cursor);
    stack[*count].anchor = entry_cursor_clamped(entry, entry->selection_anchor);
    stack[*count].span_count = entry->span_count;
    stack[*count].spans = NULL;
    if (entry->span_count > 0) {
        stack[*count].spans = pt_alloc(entry->span_count * sizeof(*entry->spans));
        memcpy(stack[*count].spans, entry->spans,
               entry->span_count * sizeof(*entry->spans));
    }
    ++*count;
}

static void edit_state_clear(pt_edit_state *stack, size_t *count) {
    for (size_t i = 0; i < *count; ++i) {
        free(stack[i].text);
        free(stack[i].spans);
    }
    *count = 0;
}

static void entry_history_push_raw(pt_widget *entry) {
    if (!entry->edit_history) {
        entry->edit_history = pt_alloc_zeroed(1, sizeof(*entry->edit_history));
    }
    pt_edit_history *history = entry->edit_history;
    edit_state_clear(history->redo, &history->redo_count);
    edit_state_push(history->undo, &history->undo_count, entry);
}

static void entry_history_push(pt_widget *entry) {
    entry->ime_composing = false;
    entry->ime_history_pushed = false;
    entry_history_push_raw(entry);
}

/* One composition session is a single undo step even when the input method
 * deletes a selection and then commits text in separate done events. */
static void entry_ime_history_push(pt_widget *entry) {
    entry->ime_composing = true;
    if (entry->ime_history_pushed) {
        return;
    }
    entry_history_push_raw(entry);
    entry->ime_history_pushed = true;
}

static void entry_notify_restored(pt_widget *entry) {
    entry_notify_text(entry);
    entry_reset_caret(entry);
    pt_widget_mark_dirty(entry, true);
}

static bool entry_history_step(pt_widget *entry, bool redo) {
    pt_edit_history *history = entry->edit_history;
    if (!history) {
        return false;
    }
    pt_edit_state *from = redo ? history->redo : history->undo;
    size_t *from_count = redo ? &history->redo_count : &history->undo_count;
    pt_edit_state *to = redo ? history->undo : history->redo;
    size_t *to_count = redo ? &history->undo_count : &history->redo_count;
    if (*from_count == 0) {
        return false;
    }
    edit_state_push(to, to_count, entry);
    --*from_count;
    pt_edit_state state = from[*from_count];
    free(entry->text);
    entry->text = state.text;
    free(entry->spans);
    entry->spans = state.spans;
    entry->span_count = state.span_count;
    entry->cursor = entry_cursor_clamped(entry, state.cursor);
    entry->selection_anchor = entry_cursor_clamped(entry, state.anchor);
    free(entry->preedit);
    entry->preedit = NULL;
    entry->preedit_cursor_begin = -1;
    entry->preedit_cursor_end = -1;
    entry->ime_composing = false;
    entry->ime_history_pushed = false;
    entry->selecting = false;
    entry_notify_restored(entry);
    return true;
}

static bool entry_position_is_word(uint32_t codepoint) {
    if (codepoint == '_') {
        return true;
    }
    if (codepoint < 0x80u) {
        return (codepoint >= '0' && codepoint <= '9') ||
               (codepoint >= 'A' && codepoint <= 'Z') ||
               (codepoint >= 'a' && codepoint <= 'z');
    }
    return codepoint != 0x3000u;
}

static void entry_select_word(pt_widget *entry, size_t position) {
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    if (position > len) {
        position = len;
    }
    size_t start = position;
    while (start > 0) {
        size_t previous = pt_utf8_prev_boundary(text, len, start);
        size_t next = start;
        uint32_t codepoint = pt_utf8_decode(text, len, previous, &next);
        if (!entry_position_is_word(codepoint)) {
            break;
        }
        start = previous;
    }
    size_t end = position;
    while (end < len) {
        size_t next = end;
        uint32_t codepoint = pt_utf8_decode(text, len, end, &next);
        if (!entry_position_is_word(codepoint)) {
            break;
        }
        end = next;
    }
    if (start == end) {
        entry_set_cursor(entry, position, false);
    } else {
        entry->selection_anchor = start;
        entry->cursor = end;
    }
    entry_reset_caret(entry);
}

static void entry_select_all(pt_widget *entry) {
    entry->selection_anchor = 0;
    entry->cursor = entry_length(entry);
    entry_reset_caret(entry);
    pt_widget_mark_dirty(entry, false);
}

static char *entry_selected_text(const pt_widget *entry) {
    if (!entry_has_selection(entry)) {
        return NULL;
    }
    size_t start = 0;
    size_t end = 0;
    entry_selection_range(entry, &start, &end);
    if (start >= end) {
        return NULL;
    }
    return pt_strdup_len((entry->text ? entry->text : "") + start, end - start);
}

static bool entry_copy_selection(pt_widget *entry, bool cut) {
    char *selected = entry_selected_text(entry);
    if (!selected) {
        return false;
    }
    pt_window *window = pt_widget_window(entry);
    if (window) {
        pt_platform_clipboard_set(window, selected);
    }
    free(selected);
    if (cut) {
        size_t start = 0;
        size_t end = 0;
        entry_selection_range(entry, &start, &end);
        entry_history_push(entry);
        size_t len = entry_length(entry);
        memmove(entry->text + start, entry->text + end, len - end + 1);
        entry->cursor = start;
        entry->selection_anchor = start;
        entry_after_edit(entry);
    }
    return true;
}

static char *entry_display_text(const pt_widget *entry, size_t *out_len) {
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    if (!entry->password) {
        char *copy = pt_alloc(len + 1);
        memcpy(copy, text, len + 1);
        if (out_len) {
            *out_len = len;
        }
        return copy;
    }
    size_t count = pt_utf8_count(text, len);
    size_t size = count * 3 + 1;
    char *display = pt_alloc(size);
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        display[offset++] = (char)0xE2;
        display[offset++] = (char)0x80;
        display[offset++] = (char)0xA2;
    }
    display[offset] = '\0';
    if (out_len) {
        *out_len = offset;
    }
    return display;
}

static size_t entry_display_position(const pt_widget *entry, size_t text_pos) {
    if (!entry->password) {
        return text_pos;
    }
    const char *text = entry->text ? entry->text : "";
    return pt_utf8_count(text, text_pos) * 3;
}

static size_t entry_text_position_for_display(const pt_widget *entry, size_t display_pos) {
    if (!entry->password) {
        return display_pos;
    }
    const char *text = entry->text ? entry->text : "";
    size_t len = strlen(text);
    size_t count = display_pos / 3;
    size_t pos = 0;
    for (size_t i = 0; i < count && pos < len; ++i) {
        pos = pt_utf8_next_boundary(text, len, pos);
    }
    return pos;
}

static float measure_prefix(const pt_font *font, const char *text, size_t len, size_t pos,
                            float scale) {
    if (pos > len) {
        pos = len;
    }
    return pt_font_measure(font, text, pos) / scale;
}

static char *entry_display_with_preedit(const pt_widget *entry, size_t *out_len,
                                        size_t *out_caret) {
    size_t len = 0;
    char *display = entry_display_text(entry, &len);
    size_t display_pos = entry_display_position(entry, entry_cursor_clamped(entry, entry->cursor));
    if (display_pos > len) {
        display_pos = len;
    }
    size_t preedit_len = entry->preedit ? strlen(entry->preedit) : 0;
    if (preedit_len == 0) {
        if (out_len) {
            *out_len = len;
        }
        if (out_caret) {
            *out_caret = display_pos;
        }
        return display;
    }
    char *combined = pt_alloc(len + preedit_len + 1);
    memcpy(combined, display, display_pos);
    memcpy(combined + display_pos, entry->preedit, preedit_len);
    memcpy(combined + display_pos + preedit_len, display + display_pos, len - display_pos);
    combined[len + preedit_len] = '\0';
    free(display);
    if (out_len) {
        *out_len = len + preedit_len;
    }
    if (out_caret) {
        *out_caret = display_pos + pt_widget_preedit_caret(entry);
    }
    return combined;
}

static float entry_caret_offset(const pt_widget *entry, const pt_font *font, float scale) {
    size_t len = 0;
    size_t caret = 0;
    char *display = entry_display_with_preedit(entry, &len, &caret);
    float width = measure_prefix(font, display, len, caret, scale);
    free(display);
    return width;
}

static size_t entry_position_from_x(const pt_widget *entry, const pt_font *font, float scale,
                                    float local_x) {
    float target = local_x + entry->scroll_x;
    if (target < 0.0f) {
        target = 0.0f;
    }
    size_t len = 0;
    char *display = entry_display_text(entry, &len);
    float best_distance = FLT_MAX;
    size_t best_display = 0;
    size_t scan = 0;
    for (;;) {
        float width = measure_prefix(font, display, len, scan, scale);
        float distance = fabsf(width - target);
        if (distance < best_distance) {
            best_distance = distance;
            best_display = scan;
        }
        if (scan >= len) {
            break;
        }
        scan = pt_utf8_next_boundary(display, len, scan);
    }
    size_t position = entry_text_position_for_display(entry, best_display);
    free(display);
    return position;
}

static void entry_after_edit(pt_widget *entry) {
    entry->cursor = entry_cursor_clamped(entry, entry->cursor);
    entry->selection_anchor = entry->cursor;
    entry->caret_goal_x = -1.0f;
    entry_notify_text(entry);
    entry_reset_caret(entry);
    pt_widget_mark_dirty(entry, true);
}

static void entry_delete_range(pt_widget *entry, size_t start, size_t end) {
    size_t len = entry->text ? strlen(entry->text) : 0;
    if (start > len) {
        start = len;
    }
    if (end > len) {
        end = len;
    }
    if (start >= end) {
        return;
    }
    entry_history_push(entry);
    pt_widget_spans_on_delete(entry, start, end);
    memmove(entry->text + start, entry->text + end, len - end + 1);
    entry->cursor = start;
    entry->selection_anchor = start;
    entry_after_edit(entry);
}

static char *entry_sanitize_insert(const pt_widget *entry, const char *text, size_t *out_len) {
    bool multiline = entry->kind == pt_widget_text_area;
    size_t capacity = strlen(text) * 4 + 1;
    char *sanitized = pt_alloc(capacity);
    size_t out = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (*cursor == '\t') {
            for (int i = 0; i < 4 && out + 1 < capacity; ++i) {
                sanitized[out++] = ' ';
            }
        } else if (*cursor == '\n' && multiline) {
            sanitized[out++] = '\n';
        } else if (*cursor >= 0x20u && *cursor != 0x7Fu) {
            sanitized[out++] = (char)*cursor;
        }
    }
    sanitized[out] = '\0';
    if (out_len) {
        *out_len = out;
    }
    return sanitized;
}

static char *entry_sanitize_preedit(const pt_widget *entry, const char *text, size_t *out_len) {
    bool multiline = entry->kind == pt_widget_text_area;
    size_t len = text ? strlen(text) : 0;
    char *clean = pt_alloc(len + 1);
    size_t out = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '\n' && multiline) {
            clean[out++] = '\n';
        } else if (byte >= 0x20u && byte != 0x7Fu) {
            clean[out++] = (char)byte;
        }
    }
    clean[out] = '\0';
    if (out_len) {
        *out_len = out;
    }
    return clean;
}

static size_t entry_snap_forward(const char *text, size_t len, size_t position) {
    if (position > len) {
        position = len;
    }
    while (position < len && ((unsigned char)text[position] & 0xC0u) == 0x80u) {
        ++position;
    }
    return position;
}

static size_t entry_snap_backward(const char *text, size_t position) {
    while (position > 0 && ((unsigned char)text[position] & 0xC0u) == 0x80u) {
        --position;
    }
    return position;
}

static void entry_insert_text(pt_widget *entry, const char *text) {
    if (!text || !*text) {
        return;
    }
    size_t out = 0;
    char *sanitized = entry_sanitize_insert(entry, text, &out);
    if (out == 0) {
        free(sanitized);
        return;
    }
    size_t len = entry->text ? strlen(entry->text) : 0;
    size_t start = entry_cursor_clamped(entry, entry->cursor);
    size_t end = start;
    if (entry_has_selection(entry)) {
        entry_selection_range(entry, &start, &end);
    }
    size_t removed = end - start;
    if (len - removed + out >= pt_entry_max_bytes) {
        free(sanitized);
        return;
    }
    entry_history_push(entry);
    char *next = pt_alloc(len - removed + out + 1);
    memcpy(next, entry->text ? entry->text : "", start);
    memcpy(next + start, sanitized, out);
    memcpy(next + start + out, (entry->text ? entry->text : "") + end, len - end + 1);
    if (removed > 0) {
        pt_widget_spans_on_delete(entry, start, end);
    }
    pt_widget_spans_on_insert(entry, start, out);
    free(entry->text);
    entry->text = next;
    entry->cursor = start + out;
    entry->selection_anchor = entry->cursor;
    free(sanitized);
    entry_after_edit(entry);
}

static bool widget_is_text_input(const pt_widget *widget) {
    return widget && pt_widget_kind_is_text_input(widget->kind);
}

size_t pt_widget_preedit_caret(const pt_widget *widget) {
    if (!widget || !widget->preedit) {
        return 0;
    }
    size_t len = strlen(widget->preedit);
    if (widget->preedit_cursor_begin < 0) {
        return len;
    }
    size_t position = (size_t)widget->preedit_cursor_begin;
    if (position > len) {
        position = len;
    }
    return entry_snap_backward(widget->preedit, position);
}

static void entry_ime_end_composition(pt_widget *widget) {
    widget->ime_composing = false;
    widget->ime_history_pushed = false;
}

static void entry_ime_clear_preedit(pt_widget *widget) {
    free(widget->preedit);
    widget->preedit = NULL;
    widget->preedit_cursor_begin = -1;
    widget->preedit_cursor_end = -1;
}

static bool entry_ime_delete_selection(pt_widget *widget) {
    if (!entry_has_selection(widget)) {
        return false;
    }
    size_t start = 0;
    size_t end = 0;
    entry_selection_range(widget, &start, &end);
    if (start >= end) {
        return false;
    }
    entry_ime_history_push(widget);
    size_t len = entry_length(widget);
    pt_widget_spans_on_delete(widget, start, end);
    memmove(widget->text + start, widget->text + end, len - end + 1);
    widget->cursor = start;
    widget->selection_anchor = start;
    entry_after_edit(widget);
    return true;
}

bool pt_widget_preedit_ex(pt_widget *widget, const char *text, int32_t cursor_begin,
                          int32_t cursor_end) {
    if (!widget_is_text_input(widget) || !widget->enabled) {
        return false;
    }
    size_t length = 0;
    char *clean = entry_sanitize_preedit(widget, text, &length);
    if (length > 0) {
        widget->ime_composing = true;
        entry_ime_delete_selection(widget);
    } else {
        entry_ime_end_composition(widget);
    }
    free(widget->preedit);
    widget->preedit = length > 0 ? clean : NULL;
    if (length == 0) {
        free(clean);
    }
    if (length > 0 && cursor_begin >= 0 && cursor_end >= 0) {
        size_t begin = (size_t)cursor_begin;
        size_t end = (size_t)cursor_end;
        if (begin > length) {
            begin = length;
        }
        if (end > length) {
            end = length;
        }
        begin = entry_snap_backward(widget->preedit, begin);
        end = entry_snap_backward(widget->preedit, end);
        if (begin > end) {
            size_t swap = begin;
            begin = end;
            end = swap;
        }
        widget->preedit_cursor_begin = (int32_t)begin;
        widget->preedit_cursor_end = (int32_t)end;
    } else {
        widget->preedit_cursor_begin = -1;
        widget->preedit_cursor_end = -1;
    }
    entry_reset_caret(widget);
    pt_widget_mark_dirty(widget, false);
    return true;
}

bool pt_widget_ime_has_state(const pt_widget *widget) {
    return widget && (widget->ime_composing || widget->preedit != NULL);
}

typedef struct entry_deletion {
    size_t start;
    size_t end;
} entry_deletion;

static void entry_delete_ranges_add(entry_deletion *ranges, size_t *count, size_t start,
                                    size_t end) {
    if (start >= end) {
        return;
    }
    ranges[*count].start = start;
    ranges[*count].end = end;
    ++*count;
}

static size_t entry_delete_ranges_merge(entry_deletion *ranges, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        entry_deletion item = ranges[i];
        size_t j = i;
        while (j > 0 && ranges[j - 1].start > item.start) {
            ranges[j] = ranges[j - 1];
            --j;
        }
        ranges[j] = item;
    }
    size_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        if (out > 0 && ranges[i].start <= ranges[out - 1].end) {
            if (ranges[i].end > ranges[out - 1].end) {
                ranges[out - 1].end = ranges[i].end;
            }
        } else {
            ranges[out++] = ranges[i];
        }
    }
    return out;
}

bool pt_widget_ime_apply(pt_widget *widget, const char *commit, uint32_t delete_before,
                         uint32_t delete_after, bool finalize) {
    if (!widget_is_text_input(widget) || !widget->enabled) {
        return false;
    }
    bool submit = widget->kind == pt_widget_entry && commit && strchr(commit, '\n');
    size_t insert_len = 0;
    char *insert = entry_sanitize_insert(widget, commit ? commit : "", &insert_len);

    const char *text = widget->text ? widget->text : "";
    size_t len = strlen(text);
    size_t cursor = entry_cursor_clamped(widget, widget->cursor);
    size_t anchor = entry_cursor_clamped(widget, widget->selection_anchor);
    size_t selection_start = cursor < anchor ? cursor : anchor;
    size_t selection_end = cursor < anchor ? anchor : cursor;

    size_t before_start = cursor;
    size_t after_end = cursor;
    if (selection_start != selection_end) {
        before_start = selection_start;
        after_end = selection_end;
    }
    before_start = before_start > delete_before ? before_start - delete_before : 0;
    before_start = entry_snap_forward(text, len, before_start);
    after_end = len - after_end > delete_after ? after_end + delete_after : len;
    after_end = entry_snap_backward(text, after_end);

    entry_deletion ranges[3];
    size_t range_count = 0;
    entry_delete_ranges_add(ranges, &range_count, selection_start, selection_end);
    entry_delete_ranges_add(ranges, &range_count, before_start, selection_start);
    entry_delete_ranges_add(ranges, &range_count, selection_end, after_end);
    range_count = entry_delete_ranges_merge(ranges, range_count);

    size_t removed = 0;
    for (size_t i = 0; i < range_count; ++i) {
        removed += ranges[i].end - ranges[i].start;
    }
    if (range_count == 0 && insert_len == 0) {
        if (finalize) {
            entry_ime_clear_preedit(widget);
            entry_ime_end_composition(widget);
            entry_reset_caret(widget);
            pt_widget_mark_dirty(widget, false);
        }
        free(insert);
        return true;
    }
    if (len - removed + insert_len >= pt_entry_max_bytes) {
        free(insert);
        return false;
    }

    entry_ime_history_push(widget);
    char *next = pt_alloc(len - removed + insert_len + 1);
    size_t insert_at = selection_start != selection_end ? selection_start : before_start;
    size_t source = 0;
    size_t out = 0;
    bool inserted = false;
    for (size_t i = 0; i < range_count; ++i) {
        if (!inserted && insert_at <= ranges[i].start) {
            size_t prefix = insert_at - source;
            memcpy(next + out, text + source, prefix);
            out += prefix;
            memcpy(next + out, insert, insert_len);
            out += insert_len;
            source = insert_at;
            inserted = true;
        }
        size_t keep = ranges[i].start - source;
        memcpy(next + out, text + source, keep);
        out += keep;
        source = ranges[i].end;
        if (!inserted && insert_at <= source) {
            memcpy(next + out, insert, insert_len);
            out += insert_len;
            inserted = true;
        }
    }
    if (!inserted) {
        size_t prefix = insert_at - source;
        memcpy(next + out, text + source, prefix);
        out += prefix;
        memcpy(next + out, insert, insert_len);
        out += insert_len;
        source = insert_at;
    }
    memcpy(next + out, text + source, len - source + 1);
    free(insert);
    for (size_t i = range_count; i > 0; --i) {
        pt_widget_spans_on_delete(widget, ranges[i - 1].start, ranges[i - 1].end);
    }
    if (insert_len > 0) {
        pt_widget_spans_on_insert(widget, insert_at, insert_len);
    }
    free(widget->text);
    widget->text = next;
    widget->cursor = out;
    widget->selection_anchor = widget->cursor;
    if (finalize) {
        entry_ime_clear_preedit(widget);
        entry_ime_end_composition(widget);
    }
    entry_after_edit(widget);
    if (submit && widget->on_submit) {
        widget->on_submit(widget, widget->on_submit_data);
    }
    return true;
}

bool pt_widget_ime_surrounding_text(const pt_widget *widget, char **text, uint32_t *cursor,
                                    uint32_t *anchor) {
    if (!widget_is_text_input(widget) || !widget->enabled) {
        return false;
    }
    bool sensitive = widget->kind == pt_widget_entry &&
                     (widget->password || widget->input_purpose == pt_input_purpose_password);
    const char *source = sensitive ? "" : (widget->text ? widget->text : "");
    if (text) {
        *text = pt_strdup(source);
    }
    size_t len = strlen(source);
    size_t caret = sensitive ? 0 : entry_cursor_clamped(widget, widget->cursor);
    size_t selection = sensitive ? 0 : entry_cursor_clamped(widget, widget->selection_anchor);
    if (caret > len) {
        caret = len;
    }
    if (selection > len) {
        selection = len;
    }
    if (cursor) {
        *cursor = (uint32_t)caret;
    }
    if (anchor) {
        *anchor = (uint32_t)selection;
    }
    return true;
}

bool pt_widget_text_selection_get(const pt_widget *widget, uint32_t *start, uint32_t *end) {
    if (!widget_is_text_input(widget) || !entry_has_selection(widget)) {
        return false;
    }
    size_t a = 0;
    size_t b = 0;
    entry_selection_range(widget, &a, &b);
    if (a == b) {
        return false;
    }
    if (start) {
        *start = (uint32_t)a;
    }
    if (end) {
        *end = (uint32_t)b;
    }
    return true;
}

bool pt_widget_text_selection_set(pt_widget *widget, uint32_t start, uint32_t end) {
    if (!widget_is_text_input(widget)) {
        return false;
    }
    size_t a = start;
    size_t b = end;
    size_t len = entry_length(widget);
    if (a > len) {
        a = len;
    }
    if (b > len) {
        b = len;
    }
    if (a > b) {
        size_t swap = a;
        a = b;
        b = swap;
    }
    widget->selection_anchor = a;
    widget->cursor = b;
    widget->selecting = false;
    widget->caret_goal_x = -1.0f;
    entry_reset_caret(widget);
    pt_widget_mark_dirty(widget, false);
    return true;
}

void pt_widget_text_select_all(pt_widget *widget) {
    if (widget_is_text_input(widget)) {
        entry_select_all(widget);
    }
}

bool pt_widget_text_undo(pt_widget *widget) {
    return widget_is_text_input(widget) && entry_history_step(widget, false);
}

bool pt_widget_text_redo(pt_widget *widget) {
    return widget_is_text_input(widget) && entry_history_step(widget, true);
}

void pt_widget_apply_defaults(pt_widget *widget) {
    widget->visible = true;
    widget->enabled = true;
    widget->focusable = kind_is_focusable(widget->kind);
    widget->requested_width = pt_auto;
    widget->requested_height = pt_auto;
    widget->min_width = 0.0f;
    widget->min_height = 0.0f;
    widget->max_width = FLT_MAX;
    widget->max_height = FLT_MAX;
    widget->font_size = pt_default_font_size;
    widget->font_weight = pt_font_weight_regular;
    widget->radius = 10.0f;
    widget->border_width = 0.0f;
    widget->text_align = pt_text_align_start;
    widget->align_self = pt_align_auto;
    widget->direction = pt_direction_column;
    widget->align_items = pt_align_stretch;
    widget->justify = pt_justify_start;
    widget->value = 0.0f;
    widget->wrap = false;
    widget->button_variant = pt_button_primary;
    widget->selection_anchor = 0;
    widget->preedit_cursor_begin = -1;
    widget->preedit_cursor_end = -1;
    widget->input_purpose = pt_input_purpose_normal;
    widget->segment_count = 1;
    widget->segment_labels = NULL;
    widget->tooltip_delay = 600u;
    widget->grid_columns = 2;
    widget->grid_span = 1;
    widget->row_gap = 12.0f;
    widget->row_height = 44.0f;
    widget->list_selected = pt_list_index_none;
    widget->list_index = pt_list_index_none;

    switch (widget->kind) {
    case pt_widget_button:
        widget->padding_top = 10.0f;
        widget->padding_bottom = 10.0f;
        widget->padding_left = 18.0f;
        widget->padding_right = 18.0f;
        widget->radius = 10.0f;
        widget->font_weight = pt_font_weight_medium;
        break;
    case pt_widget_entry:
        widget->padding_top = 11.0f;
        widget->padding_bottom = 11.0f;
        widget->padding_left = 14.0f;
        widget->padding_right = 14.0f;
        widget->radius = 10.0f;
        widget->drag_enabled = true;
        break;
    case pt_widget_card:
        widget->padding_top = 22.0f;
        widget->padding_bottom = 22.0f;
        widget->padding_left = 22.0f;
        widget->padding_right = 22.0f;
        widget->gap = 14.0f;
        widget->radius = 16.0f;
        break;
    case pt_widget_toggle:
        widget->requested_width = 44.0f;
        widget->requested_height = 24.0f;
        widget->radius = 12.0f;
        break;
    case pt_widget_slider:
        widget->requested_width = 180.0f;
        widget->requested_height = 22.0f;
        break;
    case pt_widget_scroll:
        widget->requested_width = 240.0f;
        widget->requested_height = 200.0f;
        break;
    case pt_widget_progress:
        widget->requested_width = 200.0f;
        widget->requested_height = 8.0f;
        break;
    case pt_widget_spinner:
        widget->requested_width = 20.0f;
        widget->requested_height = 20.0f;
        break;
    case pt_widget_segmented:
        widget->requested_height = 34.0f;
        widget->radius = 10.0f;
        break;
    case pt_widget_checkbox:
    case pt_widget_radio:
        widget->font_size = pt_default_font_size;
        break;
    case pt_widget_spacer:
        widget->grow = 1.0f;
        break;
    case pt_widget_text_area:
        widget->padding_top = 12.0f;
        widget->padding_bottom = 12.0f;
        widget->padding_left = 14.0f;
        widget->padding_right = 14.0f;
        widget->radius = 10.0f;
        widget->requested_height = 160.0f;
        widget->wrap = true;
        break;
    case pt_widget_wrap:
        widget->gap = 12.0f;
        widget->row_gap = 12.0f;
        break;
    case pt_widget_grid:
        widget->requested_height = pt_auto;
        widget->gap = 12.0f;
        widget->row_gap = 12.0f;
        break;
    case pt_widget_list:
        widget->requested_width = 320.0f;
        widget->requested_height = 260.0f;
        widget->radius = 12.0f;
        break;
    case pt_widget_list_row:
        widget->direction = pt_direction_row;
        widget->align_items = pt_align_center;
        widget->padding_left = 16.0f;
        widget->padding_right = 16.0f;
        widget->gap = 12.0f;
        widget->radius = 0.0f;
        break;
    case pt_widget_icon:
        widget->icon_size = 20.0f;
        widget->radius = 0.0f;
        break;
    case pt_widget_table:
        widget->radius = 12.0f;
        widget->row_height = 40.0f;
        break;
    case pt_widget_table_header:
        widget->direction = pt_direction_row;
        widget->align_items = pt_align_stretch;
        widget->radius = 0.0f;
        break;
    case pt_widget_table_header_cell:
        widget->font_size = 13.0f;
        widget->font_weight = pt_font_weight_semibold;
        widget->padding_left = 12.0f;
        widget->padding_right = 12.0f;
        widget->radius = 0.0f;
        break;
    case pt_widget_table_row:
        widget->direction = pt_direction_row;
        widget->align_items = pt_align_stretch;
        widget->radius = 0.0f;
        break;
    case pt_widget_table_cell:
        widget->padding_left = 12.0f;
        widget->padding_right = 12.0f;
        widget->radius = 0.0f;
        break;
    case pt_widget_tree:
        widget->radius = 12.0f;
        widget->row_height = 32.0f;
        break;
    case pt_widget_tree_row:
        widget->radius = 0.0f;
        break;
    case pt_widget_tabs:
        widget->radius = 0.0f;
        break;
    case pt_widget_tab_page:
        widget->padding_top = 16.0f;
        widget->padding_bottom = 16.0f;
        widget->padding_left = 16.0f;
        widget->padding_right = 16.0f;
        widget->gap = 12.0f;
        widget->radius = 0.0f;
        break;
    case pt_widget_sidebar:
        widget->radius = 12.0f;
        widget->row_height = 44.0f;
        break;
    case pt_widget_sidebar_item:
        widget->direction = pt_direction_row;
        widget->align_items = pt_align_center;
        widget->padding_left = 12.0f;
        widget->padding_right = 12.0f;
        widget->gap = 12.0f;
        widget->radius = 10.0f;
        break;
    case pt_widget_chip:
        widget->direction = pt_direction_row;
        widget->align_items = pt_align_center;
        widget->padding_left = 12.0f;
        widget->padding_right = 12.0f;
        widget->gap = 6.0f;
        widget->radius = 999.0f;
        widget->font_size = 13.0f;
        widget->font_weight = pt_font_weight_medium;
        break;
    case pt_widget_skeleton:
        widget->radius = 6.0f;
        widget->requested_width = 120.0f;
        widget->requested_height = 12.0f;
        break;
    case pt_widget_combobox:
        widget->padding_top = 0.0f;
        widget->padding_bottom = 0.0f;
        widget->padding_left = 12.0f;
        widget->padding_right = 12.0f;
        widget->radius = 10.0f;
        widget->font_size = 13.0f;
        widget->text_align = pt_text_align_start;
        break;
    default:
        break;
    }
}

pt_widget *pt_widget_new(pt_widget *parent, pt_widget_kind kind) {
    pt_widget *widget = pt_alloc_zeroed(1, sizeof(*widget));
    widget->kind = kind;
    widget->a11y_id = pt_a11y_alloc_id();
    pt_widget_apply_defaults(widget);
    if (parent) {
        pt_widget_append(parent, widget);
    }
    return widget;
}

void pt_widget_append(pt_widget *parent, pt_widget *child) {
    if (!parent || !child || child->parent) {
        return;
    }
    child->parent = parent;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static pt_widget *widget_root(pt_widget *widget) {
    while (widget && widget->parent) {
        widget = widget->parent;
    }
    return widget;
}

pt_window *pt_widget_window(const pt_widget *widget) {
    pt_widget *root = widget_root((pt_widget *)widget);
    return root ? root->window : NULL;
}

pt_app *pt_widget_app(const pt_widget *widget) {
    pt_window *window = pt_widget_window(widget);
    return window ? window->app : NULL;
}

static void widget_detach(pt_widget *widget) {
    pt_widget *parent = widget->parent;
    if (!parent) {
        return;
    }
    pt_widget *previous = NULL;
    for (pt_widget *child = parent->first_child; child; child = child->next_sibling) {
        if (child == widget) {
            if (previous) {
                previous->next_sibling = child->next_sibling;
            } else {
                parent->first_child = child->next_sibling;
            }
            if (parent->last_child == child) {
                parent->last_child = previous;
            }
            break;
        }
        previous = child;
    }
    widget->parent = NULL;
    widget->next_sibling = NULL;
}

void pt_widget_extra_free(pt_widget *widget) {
    if (!widget) {
        return;
    }
    switch (widget->kind) {
    case pt_widget_list:
        pt_list_free_extra(widget);
        break;
    case pt_widget_table:
        pt_table_free_extra(widget);
        break;
    case pt_widget_tree:
        pt_tree_free_extra(widget);
        break;
    case pt_widget_tabs:
        pt_tabs_free_extra(widget);
        break;
    case pt_widget_sidebar:
        pt_sidebar_free_extra(widget);
        break;
    case pt_widget_combobox:
        pt_combobox_free_extra(widget);
        break;
    default:
        break;
    }
}

void pt_widget_free_tree(pt_widget *widget) {
    if (!widget) {
        return;
    }
    pt_dnd_widget_destroyed(widget);
    pt_widget *child = widget->first_child;
    while (child) {
        pt_widget *next = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        pt_widget_free_tree(child);
        child = next;
    }
    pt_widget_extra_free(widget);
    free(widget->text);
    free(widget->spans);
    free(widget->placeholder);
    free(widget->preedit);
    free(widget->shortcut);
    free(widget->tooltip);
    free(widget->a11y_name);
    free(widget->a11y_description);
    if (widget->segment_labels) {
        for (uint32_t i = 0; i < widget->segment_count; ++i) {
            free(widget->segment_labels[i]);
        }
        free(widget->segment_labels);
    }
    edit_history_free(widget->edit_history);
    free(widget);
}

static void widget_forget_owner_state(pt_widget *widget) {
    if (!widget || !widget->parent) {
        return;
    }
    if (widget->kind == pt_widget_tab_page && widget->parent->kind == pt_widget_tabs) {
        pt_tabs_forget_page(widget->parent, widget);
    }
}

void pt_widget_destroy(pt_widget *widget) {
    if (!widget) {
        return;
    }
    pt_window *window = pt_widget_window(widget);
    if (window && window->root == widget) {
        pt_log_warn("cannot destroy a window root widget");
        return;
    }
    window_forget_subtree(window, widget);
    pt_window_overlays_forget_widget(window, widget);
    widget_forget_owner_state(widget);
    widget_detach(widget);
    pt_widget_free_tree(widget);
    if (window) {
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

void pt_widget_clear_children(pt_widget *widget) {
    if (!widget) {
        return;
    }
    pt_window *window = pt_widget_window(widget);
    for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        window_forget_subtree(window, child);
    }
    while (widget->first_child) {
        pt_widget *child = widget->first_child;
        widget_forget_owner_state(child);
        widget->first_child = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        pt_widget_free_tree(child);
    }
    widget->last_child = NULL;
    pt_widget_mark_dirty(widget, true);
}

pt_widget *pt_widget_parent(const pt_widget *widget) {
    return widget ? widget->parent : NULL;
}

pt_widget_kind pt_widget_get_kind(const pt_widget *widget) {
    return widget ? widget->kind : pt_widget_box;
}

void pt_widget_mark_dirty(pt_widget *widget, bool layout_changed) {
    pt_window *window = pt_widget_window(widget);
    if (!window) {
        return;
    }
    if (layout_changed) {
        pt_widget *boundary = layout_boundary_for(widget);
        pt_window_invalidate_layout(window, boundary);
        pt_widget *root = window->layout_root;
        if (root && root->width > 0.0f && root->height > 0.0f) {
            pt_window_damage_rect(window, (pt_rect){root->x - 24.0f, root->y - 24.0f,
                                                    root->width + 48.0f,
                                                    root->height + 48.0f});
        } else {
            pt_window_damage_all(window);
        }
    } else {
        pt_widget_damage(widget, 2.0f);
    }
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

/* ---------- constructors ---------- */

pt_widget *pt_box_create(pt_widget *parent, pt_direction direction) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_box);
    widget->direction = direction;
    return widget;
}

pt_widget *pt_row_create(pt_widget *parent) {
    return pt_box_create(parent, pt_direction_row);
}

pt_widget *pt_column_create(pt_widget *parent) {
    return pt_box_create(parent, pt_direction_column);
}

pt_widget *pt_label_create(pt_widget *parent, const char *text) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_label);
    widget->text = pt_strdup(text ? text : "");
    return widget;
}

pt_widget *pt_button_create(pt_widget *parent, const char *text) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_button);
    widget->text = pt_strdup(text ? text : "");
    return widget;
}

pt_widget *pt_entry_create(pt_widget *parent, const char *placeholder) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_entry);
    widget->placeholder = pt_strdup(placeholder ? placeholder : "");
    widget->text = pt_strdup("");
    return widget;
}

pt_widget *pt_toggle_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_toggle);
}

pt_widget *pt_slider_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_slider);
}

pt_widget *pt_scroll_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_scroll);
}

pt_widget *pt_divider_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_divider);
}

pt_widget *pt_card_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_card);
}

pt_widget *pt_spacer_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_spacer);
}

pt_widget *pt_checkbox_create(pt_widget *parent, const char *text) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_checkbox);
    widget->text = pt_strdup(text ? text : "");
    return widget;
}

pt_widget *pt_radio_create(pt_widget *parent, const char *text) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_radio);
    widget->text = pt_strdup(text ? text : "");
    return widget;
}

pt_widget *pt_progress_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_progress);
}

pt_widget *pt_spinner_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_spinner);
}

pt_widget *pt_chip_create(pt_widget *parent, const char *text) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_chip);
    widget->text = pt_strdup(text ? text : "");
    return widget;
}

pt_widget *pt_skeleton_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_skeleton);
}

pt_widget *pt_segmented_create(pt_widget *parent, const char *const *labels, uint32_t count) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_segmented);
    if (count == 0) {
        count = 1;
    }
    widget->segment_labels = pt_alloc_zeroed(count, sizeof(*widget->segment_labels));
    widget->segment_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        const char *label = labels && labels[i] ? labels[i] : "";
        widget->segment_labels[i] = pt_strdup(label);
    }
    return widget;
}

pt_widget *pt_text_area_create(pt_widget *parent, const char *placeholder) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_text_area);
    widget->placeholder = pt_strdup(placeholder ? placeholder : "");
    return widget;
}

pt_widget *pt_wrap_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_wrap);
}

pt_widget *pt_grid_create(pt_widget *parent, uint32_t columns) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_grid);
    widget->grid_columns = columns > 0 ? columns : 1;
    return widget;
}

pt_widget *pt_list_view_create(pt_widget *parent) {
    return pt_widget_new(parent, pt_widget_list);
}

/* ---------- layout setters ---------- */

static void set_dimension(pt_widget *widget, float *field, float value) {
    if (!widget) {
        return;
    }
    *field = value < 0.0f ? pt_auto : value;
    pt_widget_mark_dirty(widget, true);
}

void pt_widget_set_width(pt_widget *widget, float width) {
    if (widget) {
        set_dimension(widget, &widget->requested_width, width);
    }
}

void pt_widget_set_height(pt_widget *widget, float height) {
    if (widget) {
        set_dimension(widget, &widget->requested_height, height);
    }
}

void pt_widget_set_size(pt_widget *widget, float width, float height) {
    if (widget) {
        set_dimension(widget, &widget->requested_width, width);
        set_dimension(widget, &widget->requested_height, height);
    }
}

void pt_widget_set_min_width(pt_widget *widget, float width) {
    if (widget) {
        widget->min_width = pt_max_float(0.0f, width);
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_min_height(pt_widget *widget, float height) {
    if (widget) {
        widget->min_height = pt_max_float(0.0f, height);
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_max_width(pt_widget *widget, float width) {
    if (widget) {
        widget->max_width = width < 0.0f ? FLT_MAX : width;
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_max_height(pt_widget *widget, float height) {
    if (widget) {
        widget->max_height = height < 0.0f ? FLT_MAX : height;
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_margin(pt_widget *widget, float top, float right, float bottom, float left) {
    if (!widget) {
        return;
    }
    widget->margin_top = top;
    widget->margin_right = right;
    widget->margin_bottom = bottom;
    widget->margin_left = left;
    pt_widget_mark_dirty(widget, true);
}

void pt_widget_set_padding(pt_widget *widget, float top, float right, float bottom, float left) {
    if (!widget) {
        return;
    }
    widget->padding_top = top;
    widget->padding_right = right;
    widget->padding_bottom = bottom;
    widget->padding_left = left;
    pt_widget_mark_dirty(widget, true);
}

void pt_widget_set_gap(pt_widget *widget, float gap) {
    if (widget) {
        widget->gap = pt_max_float(0.0f, gap);
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_grow(pt_widget *widget, float grow) {
    if (widget) {
        widget->grow = pt_max_float(0.0f, grow);
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_align_self(pt_widget *widget, pt_align align) {
    if (widget) {
        widget->align_self = align;
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_box_set_direction(pt_widget *box, pt_direction direction) {
    if (box && kind_is_container(box->kind)) {
        box->direction = direction;
        pt_widget_mark_dirty(box, true);
    }
}

void pt_box_set_gap(pt_widget *box, float gap) {
    pt_widget_set_gap(box, gap);
}

void pt_box_set_align(pt_widget *box, pt_align align) {
    if (box && kind_is_container(box->kind)) {
        box->align_items = align == pt_align_auto ? pt_align_stretch : align;
        pt_widget_mark_dirty(box, true);
    }
}

void pt_box_set_justify(pt_widget *box, pt_justify justify) {
    if (box && kind_is_container(box->kind)) {
        box->justify = justify;
        pt_widget_mark_dirty(box, true);
    }
}

void pt_wrap_set_gap(pt_widget *wrap, float row_gap, float column_gap) {
    if (!wrap || wrap->kind != pt_widget_wrap) {
        return;
    }
    wrap->row_gap = pt_max_float(0.0f, row_gap);
    wrap->gap = pt_max_float(0.0f, column_gap);
    pt_widget_mark_dirty(wrap, true);
}

void pt_grid_set_columns(pt_widget *grid, uint32_t columns) {
    if (!grid || grid->kind != pt_widget_grid) {
        return;
    }
    grid->grid_columns = columns > 0 ? columns : 1;
    pt_widget_mark_dirty(grid, true);
}

uint32_t pt_grid_get_columns(const pt_widget *grid) {
    return grid && grid->kind == pt_widget_grid ? grid->grid_columns : 0;
}

void pt_grid_set_gap(pt_widget *grid, float row_gap, float column_gap) {
    if (!grid || grid->kind != pt_widget_grid) {
        return;
    }
    grid->row_gap = pt_max_float(0.0f, row_gap);
    grid->gap = pt_max_float(0.0f, column_gap);
    pt_widget_mark_dirty(grid, true);
}

void pt_widget_set_grid_span(pt_widget *widget, uint32_t span) {
    if (!widget) {
        return;
    }
    widget->grid_span = span > 0 ? span : 1;
    pt_widget_mark_dirty(widget, true);
}

uint32_t pt_widget_get_grid_span(const pt_widget *widget) {
    return widget ? widget->grid_span : 0;
}

/* ---------- appearance ---------- */

void pt_widget_set_background(pt_widget *widget, pt_color color) {
    if (!widget) {
        return;
    }
    widget->background = color;
    widget->has_background = true;
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_set_foreground(pt_widget *widget, pt_color color) {
    if (!widget) {
        return;
    }
    widget->foreground = color;
    widget->has_foreground = true;
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_set_border_color(pt_widget *widget, pt_color color) {
    if (!widget) {
        return;
    }
    widget->border_color = color;
    widget->has_border_color = true;
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_set_border_width(pt_widget *widget, float width) {
    if (!widget) {
        return;
    }
    widget->border_width = pt_max_float(0.0f, width);
    widget->has_border_width = true;
    pt_widget_mark_dirty(widget, true);
}

void pt_widget_set_radius(pt_widget *widget, float radius) {
    if (!widget) {
        return;
    }
    widget->radius = pt_max_float(0.0f, radius);
    widget->has_radius = true;
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_set_font_size(pt_widget *widget, float size) {
    if (!widget) {
        return;
    }
    widget->font_size = size > 0.0f ? size : pt_default_font_size;
    widget->has_font_size = true;
    pt_widget_mark_dirty(widget, true);
}

void pt_widget_set_font_weight(pt_widget *widget, pt_font_weight weight) {
    if (!widget) {
        return;
    }
    widget->font_weight = (int)weight;
    widget->has_font_weight = true;
    pt_widget_mark_dirty(widget, true);
}

void pt_widget_set_text_align(pt_widget *widget, pt_text_align align) {
    if (widget) {
        widget->text_align = align;
        pt_widget_mark_dirty(widget, false);
    }
}

void pt_widget_set_visible(pt_widget *widget, bool visible) {
    if (widget) {
        widget->visible = visible;
        pt_widget_mark_dirty(widget, true);
        pt_widget_a11y_emit(widget, pt_a11y_event_state_changed);
    }
}

void pt_widget_set_enabled(pt_widget *widget, bool enabled) {
    if (widget) {
        widget->enabled = enabled;
        if (!enabled) {
            widget->pressed = false;
        }
        pt_widget_mark_dirty(widget, true);
        pt_widget_a11y_emit(widget, pt_a11y_event_state_changed);
    }
}

void pt_widget_set_label_wrap(pt_widget *widget, bool wrap) {
    if (widget) {
        widget->wrap = wrap;
        pt_widget_mark_dirty(widget, true);
    }
}

void pt_widget_set_text(pt_widget *widget, const char *text) {
    if (!widget) {
        return;
    }
    char *copy = pt_strdup(text ? text : "");
    free(widget->text);
    widget->text = copy;
    free(widget->spans);
    widget->spans = NULL;
    widget->span_count = 0;
    bool editable = widget_is_text_input(widget);
    if (!editable) {
        pt_widget_a11y_emit(widget, pt_a11y_event_name_changed);
    }
    if (editable) {
        widget->cursor = strlen(copy);
        widget->selection_anchor = widget->cursor;
        widget->caret_goal_x = -1.0f;
        widget->selecting = false;
        widget->scroll_y = 0.0f;
        free(widget->preedit);
        widget->preedit = NULL;
        widget->preedit_cursor_begin = -1;
        widget->preedit_cursor_end = -1;
        widget->ime_composing = false;
        widget->ime_history_pushed = false;
        edit_history_free(widget->edit_history);
        widget->edit_history = NULL;
    }
    pt_widget_mark_dirty(widget, true);
    if (editable) {
        entry_notify_text(widget);
    }
}

const char *pt_widget_get_text(const pt_widget *widget) {
    return widget && widget->text ? widget->text : "";
}

void pt_widget_set_tooltip(pt_widget *widget, const char *text) {
    if (!widget) {
        return;
    }
    char *copy = text && *text ? pt_strdup(text) : NULL;
    free(widget->tooltip);
    widget->tooltip = copy;
    if (pt_widget_window(widget)) {
        pt_window_overlays_dismiss_tooltip(pt_widget_window(widget));
    }
}

void pt_widget_set_tooltip_delay(pt_widget *widget, uint32_t milliseconds) {
    if (widget) {
        widget->tooltip_delay = milliseconds;
    }
}

void pt_button_set_variant(pt_widget *button, pt_button_variant variant) {
    if (button && button->kind == pt_widget_button) {
        button->button_variant = variant;
        pt_widget_mark_dirty(button, false);
    }
}

void pt_button_set_on_click(pt_widget *button, pt_click_callback callback, void *user_data) {
    if (button && button->kind == pt_widget_button) {
        button->on_click = callback;
        button->on_click_data = user_data;
    }
}

void pt_chip_set_icon(pt_widget *chip, pt_icon icon) {
    if (chip && chip->kind == pt_widget_chip) {
        chip->icon = icon;
        pt_widget_mark_dirty(chip, true);
    }
}

void pt_chip_set_removable(pt_widget *chip, bool removable) {
    if (chip && chip->kind == pt_widget_chip) {
        chip->chip_removable = removable;
        pt_widget_mark_dirty(chip, true);
    }
}

bool pt_chip_get_removable(const pt_widget *chip) {
    return chip && chip->kind == pt_widget_chip && chip->chip_removable;
}

void pt_chip_set_selected(pt_widget *chip, bool selected) {
    if (!chip || chip->kind != pt_widget_chip || chip->chip_selected == selected) {
        return;
    }
    chip->chip_selected = selected;
    pt_widget_mark_dirty(chip, false);
    pt_widget_a11y_emit(chip, pt_a11y_event_state_changed);
}

bool pt_chip_get_selected(const pt_widget *chip) {
    return chip && chip->kind == pt_widget_chip && chip->chip_selected;
}

void pt_chip_set_on_click(pt_widget *chip, pt_click_callback callback, void *user_data) {
    if (chip && chip->kind == pt_widget_chip) {
        chip->on_click = callback;
        chip->on_click_data = user_data;
    }
}

void pt_chip_set_on_remove(pt_widget *chip, pt_click_callback callback, void *user_data) {
    if (chip && chip->kind == pt_widget_chip) {
        chip->on_remove = callback;
        chip->on_remove_data = user_data;
    }
}

void pt_widget_set_busy(pt_widget *widget, bool busy) {
    if (!widget || widget->busy == busy) {
        return;
    }
    widget->busy = busy;
    pt_widget_mark_dirty(widget, false);
    pt_widget_a11y_emit(widget, pt_a11y_event_state_changed);
    if (busy && widget->window) {
        widget->window->animating = true;
        pt_platform_window_schedule_draw(widget->window);
    }
}

bool pt_widget_get_busy(const pt_widget *widget) {
    return widget && widget->busy;
}

void pt_widget_set_invalid(pt_widget *widget, bool invalid) {
    if (!widget || widget->invalid == invalid) {
        return;
    }
    widget->invalid = invalid;
    pt_widget_mark_dirty(widget, false);
    pt_widget_a11y_emit(widget, pt_a11y_event_state_changed);
}

bool pt_widget_get_invalid(const pt_widget *widget) {
    return widget && widget->invalid;
}

void pt_entry_set_placeholder(pt_widget *entry, const char *placeholder) {
    if (entry && entry->kind == pt_widget_entry) {
        char *copy = pt_strdup(placeholder ? placeholder : "");
        free(entry->placeholder);
        entry->placeholder = copy;
        pt_widget_mark_dirty(entry, false);
    }
}

void pt_widget_set_input_purpose(pt_widget *widget, pt_input_purpose purpose) {
    if (!widget_is_text_input(widget)) {
        return;
    }
    if (purpose < pt_input_purpose_normal || purpose > pt_input_purpose_terminal) {
        purpose = pt_input_purpose_normal;
    }
    widget->input_purpose = purpose;
    if (widget->window && widget->window->focused == widget) {
        pt_platform_ime_set_enabled(widget->window, true);
    }
}

pt_input_purpose pt_widget_get_input_purpose(const pt_widget *widget) {
    return widget ? widget->input_purpose : pt_input_purpose_normal;
}

void pt_entry_set_password(pt_widget *entry, bool password) {
    if (entry && entry->kind == pt_widget_entry) {
        entry->password = password;
        pt_widget_mark_dirty(entry, true);
        if (entry->focused && entry->window && entry->window->focused == entry) {
            pt_platform_ime_set_enabled(entry->window, true);
        }
    }
}

void pt_entry_set_on_change(pt_widget *entry, pt_text_callback callback, void *user_data) {
    if (entry && entry->kind == pt_widget_entry) {
        entry->on_text = callback;
        entry->on_text_data = user_data;
    }
}

void pt_entry_set_on_submit(pt_widget *entry, pt_click_callback callback, void *user_data) {
    if (entry && entry->kind == pt_widget_entry) {
        entry->on_submit = callback;
        entry->on_submit_data = user_data;
    }
}

void pt_entry_select_all(pt_widget *entry) {
    if (entry && entry->kind == pt_widget_entry) {
        entry_select_all(entry);
    }
}

void pt_entry_set_selection(pt_widget *entry, uint32_t start, uint32_t end) {
    if (entry && entry->kind == pt_widget_entry) {
        pt_widget_text_selection_set(entry, start, end);
    }
}

bool pt_entry_get_selection(const pt_widget *entry, uint32_t *start, uint32_t *end) {
    if (!entry || entry->kind != pt_widget_entry) {
        return false;
    }
    return pt_widget_text_selection_get(entry, start, end);
}

bool pt_entry_undo(pt_widget *entry) {
    return entry && entry->kind == pt_widget_entry && entry_history_step(entry, false);
}

bool pt_entry_redo(pt_widget *entry) {
    return entry && entry->kind == pt_widget_entry && entry_history_step(entry, true);
}

void pt_text_area_set_placeholder(pt_widget *area, const char *placeholder) {
    if (area && area->kind == pt_widget_text_area) {
        char *copy = pt_strdup(placeholder ? placeholder : "");
        free(area->placeholder);
        area->placeholder = copy;
        pt_widget_mark_dirty(area, false);
    }
}

void pt_text_area_set_on_change(pt_widget *area, pt_text_callback callback, void *user_data) {
    if (area && area->kind == pt_widget_text_area) {
        area->on_text = callback;
        area->on_text_data = user_data;
    }
}

void pt_text_area_set_grow_to_content(pt_widget *area, bool grow) {
    if (area && area->kind == pt_widget_text_area) {
        area->grow_to_content = grow;
        pt_widget_mark_dirty(area, true);
    }
}

bool pt_text_area_get_grow_to_content(const pt_widget *area) {
    return area && area->kind == pt_widget_text_area && area->grow_to_content;
}

void pt_text_area_scroll_to_caret(pt_widget *area) {
    if (!area || area->kind != pt_widget_text_area) {
        return;
    }
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_area_ensure_caret_visible(area, &ctx);
    pt_widget_mark_dirty(area, false);
}

void pt_text_area_select_all(pt_widget *area) {
    if (area && area->kind == pt_widget_text_area) {
        entry_select_all(area);
    }
}

void pt_text_area_set_selection(pt_widget *area, uint32_t start, uint32_t end) {
    if (area && area->kind == pt_widget_text_area) {
        pt_widget_text_selection_set(area, start, end);
    }
}

bool pt_text_area_get_selection(const pt_widget *area, uint32_t *start, uint32_t *end) {
    if (!area || area->kind != pt_widget_text_area) {
        return false;
    }
    return pt_widget_text_selection_get(area, start, end);
}

bool pt_text_area_undo(pt_widget *area) {
    return area && area->kind == pt_widget_text_area && entry_history_step(area, false);
}

bool pt_text_area_redo(pt_widget *area) {
    return area && area->kind == pt_widget_text_area && entry_history_step(area, true);
}

bool pt_toggle_get_checked(const pt_widget *toggle) {
    return toggle && toggle->kind == pt_widget_toggle ? toggle->checked : false;
}

void pt_toggle_set_checked(pt_widget *toggle, bool checked) {
    if (toggle && toggle->kind == pt_widget_toggle) {
        checked_control_set(toggle, checked, false);
    }
}

void pt_toggle_set_on_change(pt_widget *toggle, pt_value_callback callback, void *user_data) {
    if (toggle && toggle->kind == pt_widget_toggle) {
        toggle->on_value = callback;
        toggle->on_value_data = user_data;
    }
}

float pt_slider_get_value(const pt_widget *slider) {
    return slider && slider->kind == pt_widget_slider ? slider->value : 0.0f;
}

void pt_slider_set_value(pt_widget *slider, float value) {
    if (slider && slider->kind == pt_widget_slider) {
        float next = pt_clamp_float(value, 0.0f, 1.0f);
        if (next != slider->value) {
            slider->value = next;
            pt_widget_mark_dirty(slider, false);
            pt_widget_a11y_emit(slider, pt_a11y_event_value_changed);
        }
    }
}

void pt_slider_set_on_change(pt_widget *slider, pt_value_callback callback, void *user_data) {
    if (slider && slider->kind == pt_widget_slider) {
        slider->on_value = callback;
        slider->on_value_data = user_data;
    }
}

bool pt_widget_get_checked(const pt_widget *widget) {
    return widget && kind_is_checked_control(widget->kind) ? widget->checked : false;
}

void pt_widget_set_checked(pt_widget *widget, bool checked) {
    checked_control_set(widget, checked, false);
}

void pt_widget_set_on_checked(pt_widget *widget, pt_value_callback callback, void *user_data) {
    if (widget && kind_is_checked_control(widget->kind)) {
        widget->on_value = callback;
        widget->on_value_data = user_data;
    }
}

static bool kind_is_progress(pt_widget_kind kind) {
    return kind == pt_widget_progress;
}

float pt_progress_get_value(const pt_widget *progress) {
    return progress && kind_is_progress(progress->kind) ? progress->value : 0.0f;
}

void pt_progress_set_value(pt_widget *progress, float value) {
    if (progress && kind_is_progress(progress->kind)) {
        float next = pt_clamp_float(value, 0.0f, 1.0f);
        if (next != progress->value) {
            progress->value = next;
            pt_widget_mark_dirty(progress, false);
            pt_widget_a11y_emit(progress, pt_a11y_event_value_changed);
        }
    }
}

bool pt_progress_get_indeterminate(const pt_widget *progress) {
    return progress && kind_is_progress(progress->kind) ? progress->indeterminate : false;
}

void pt_progress_set_indeterminate(pt_widget *progress, bool indeterminate) {
    if (progress && kind_is_progress(progress->kind)) {
        progress->indeterminate = indeterminate;
        pt_widget_mark_dirty(progress, false);
        pt_widget_a11y_emit(progress, pt_a11y_event_state_changed);
    }
}

void pt_segmented_set_on_change(pt_widget *segmented, pt_index_callback callback,
                                void *user_data) {
    if (segmented && segmented->kind == pt_widget_segmented) {
        segmented->on_index = callback;
        segmented->on_index_data = user_data;
    }
}

uint32_t pt_segmented_get_selected(const pt_widget *segmented) {
    if (!segmented || segmented->kind != pt_widget_segmented || segmented->segment_count == 0) {
        return 0;
    }
    return segmented->segment_selected < segmented->segment_count ? segmented->segment_selected
                                                                  : 0;
}

void pt_segmented_set_selected(pt_widget *segmented, uint32_t index) {
    segmented_select(segmented, index, false);
}

void pt_scroll_set_offset(pt_widget *scroll, float offset) {
    if (!scroll || scroll->kind != pt_widget_scroll) {
        return;
    }
    float max_offset = pt_max_float(0.0f, scroll->scroll_content_height - scroll->height);
    float next = pt_clamp_float(offset, 0.0f, max_offset);
    if (next != scroll->scroll_y) {
        scroll->scroll_y = next;
        pt_widget_mark_dirty(scroll, true);
    }
}

float pt_scroll_get_offset(const pt_widget *scroll) {
    return scroll && scroll->kind == pt_widget_scroll ? scroll->scroll_y : 0.0f;
}

float pt_scroll_get_max_offset(const pt_widget *scroll) {
    if (!scroll || scroll->kind != pt_widget_scroll) {
        return 0.0f;
    }
    return pt_max_float(0.0f, scroll->scroll_content_height - scroll->height);
}

/* ---------- virtualized list ---------- */

uint64_t pt_row_key_at(const pt_widget *view, uint32_t row) {
    if (!view || !view->row_key_callback || row >= view->row_count) {
        return pt_row_key_none;
    }
    return view->row_key_callback((pt_widget *)view, row, view->row_key_data);
}

uint32_t pt_row_find_key(const pt_widget *view, uint64_t key) {
    if (!view || key == pt_row_key_none) {
        return pt_list_index_none;
    }
    for (uint32_t row = 0; row < view->row_count; ++row) {
        if (pt_row_key_at(view, row) == key) {
            return row;
        }
    }
    return pt_list_index_none;
}

void pt_data_view_sync_selected_key(pt_widget *view) {
    if (!view) {
        return;
    }
    view->selected_key_valid = false;
    view->selected_key = 0;
    if (!view->row_key_callback || view->list_selected == pt_list_index_none) {
        return;
    }
    uint64_t key = pt_row_key_at(view, view->list_selected);
    if (key != pt_row_key_none) {
        view->selected_key = key;
        view->selected_key_valid = true;
    }
}

static void list_selection_ensure(pt_widget *list, pt_list_state *state) {
    if (list->row_count <= state->selection_capacity) {
        return;
    }
    uint32_t capacity = list->row_count;
    uint32_t doubled = state->selection_capacity * 2u + 16u;
    if (doubled > capacity) {
        capacity = doubled;
    }
    uint8_t *rows = pt_realloc_mem(state->selection, capacity * sizeof(*rows));
    memset(rows + state->selection_capacity, 0,
           (capacity - state->selection_capacity) * sizeof(*rows));
    state->selection = rows;
    state->selection_capacity = capacity;
}

static void list_selection_reset(pt_list_state *state) {
    if (state->selection) {
        memset(state->selection, 0, state->selection_capacity * sizeof(*state->selection));
    }
    state->selection_count = 0;
}

static void list_selection_add(pt_list_state *state, uint32_t row) {
    if (row < state->selection_capacity && !state->selection[row]) {
        state->selection[row] = 1;
        ++state->selection_count;
    }
}

static void list_selection_drop(pt_list_state *state, uint32_t row) {
    if (row < state->selection_capacity && state->selection[row]) {
        state->selection[row] = 0;
        --state->selection_count;
    }
}

static pt_list_state *list_state(pt_widget *list) {
    if (!list || list->kind != pt_widget_list) {
        return NULL;
    }
    pt_list_state *state = list->extra;
    if (state) {
        return state;
    }
    state = pt_alloc_zeroed(1, sizeof(*state));
    list->extra = state;
    /* The primary row is a selection on its own, so the set starts out holding
     * it. Without this the count would drop as soon as a second row joins. */
    if (list->list_selected != pt_list_index_none) {
        list_selection_ensure(list, state);
        list_selection_add(state, list->list_selected);
        state->selection_anchor = list->list_selected;
    }
    return state;
}

static const pt_list_state *list_state_const(const pt_widget *list) {
    return list && list->kind == pt_widget_list ? list->extra : NULL;
}

static bool list_row_is_selected(const pt_widget *list, const pt_list_state *state,
                                 uint32_t row) {
    if (state && row < state->selection_capacity && state->selection[row]) {
        return true;
    }
    return row == list->list_selected;
}

static void list_mark_rebind(pt_widget *list) {
    if (!list) {
        return;
    }
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        row->list_bound = false;
    }
}

static void list_reveal_index(pt_widget *list, uint32_t index) {
    float row_height = pt_max_float(1.0f, list->row_height);
    float viewport = pt_max_float(0.0f, list->height - list->padding_top -
                                            list->padding_bottom);
    float top = (float)index * row_height;
    float bottom = top + row_height;
    if (top < list->scroll_y) {
        list->scroll_y = top;
    } else if (viewport > 0.0f && bottom > list->scroll_y + viewport) {
        list->scroll_y = bottom - viewport;
    }
    float content = (float)list->row_count * row_height;
    float max_scroll = pt_max_float(0.0f, content - viewport);
    list->scroll_y = pt_clamp_float(list->scroll_y, 0.0f, max_scroll);
    pt_widget_mark_dirty(list, true);
}

static void list_apply_selection(pt_widget *list) {
    const pt_list_state *state = list_state_const(list);
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        bool selected = row->list_bound && list_row_is_selected(list, state, row->list_index);
        if (row->row_selected != selected) {
            row->row_selected = selected;
            pt_widget_mark_dirty(row, false);
        }
    }
}

static void list_select(pt_widget *list, uint32_t index, bool notify) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    if (index >= list->row_count) {
        index = pt_list_index_none;
    }
    bool changed = list->list_selected != index;
    list->list_selected = index;
    pt_list_state *state = list->extra;
    if (state) {
        /* Picking a row is a choice, not an addition: the extra rows a marquee or
         * Ctrl-click collected go away with it. */
        list_selection_reset(state);
        if (index != pt_list_index_none) {
            list_selection_ensure(list, state);
            list_selection_add(state, index);
            state->selection_anchor = index;
        }
    }
    pt_data_view_sync_selected_key(list);
    list_apply_selection(list);
    if (index != pt_list_index_none) {
        list_reveal_index(list, index);
    }
    if (!changed) {
        return;
    }
    pt_widget_a11y_emit(list, pt_a11y_event_selection_changed);
    pt_widget_mark_dirty(list, false);
    if (notify && list->on_select) {
        list->on_select(list, index, list->on_select_data);
    }
}

void pt_list_view_set_row_count(pt_widget *list, uint32_t count) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    uint32_t old_selected = list->list_selected;
    bool keyed = list->row_key_callback && old_selected != pt_list_index_none;
    uint64_t key = keyed ? pt_row_key_at(list, old_selected) : pt_row_key_none;
    keyed = keyed && key != pt_row_key_none;
    list->row_count = count;
    pt_list_state *state = list->extra;
    if (state) {
        /* Rows that no longer exist cannot stay selected, and an anchor past the
         * end would stretch the next range selection off the list. */
        for (uint32_t row = count; row < state->selection_capacity; ++row) {
            if (state->selection[row]) {
                state->selection[row] = 0;
                --state->selection_count;
            }
        }
        if (state->selection_anchor >= count) {
            state->selection_anchor = count > 0 ? 0u : pt_list_index_none;
        }
    }
    if (keyed) {
        list->list_selected = pt_row_find_key(list, key);
        list->selected_key = key;
        list->selected_key_valid = list->list_selected != pt_list_index_none;
    } else {
        if (list->list_selected >= count) {
            list->list_selected = pt_list_index_none;
        }
        pt_data_view_sync_selected_key(list);
    }
    list->scroll_y = 0.0f;
    list_mark_rebind(list);
    list_apply_selection(list);
    pt_widget_mark_dirty(list, true);
}

uint32_t pt_list_view_get_row_count(const pt_widget *list) {
    return list && list->kind == pt_widget_list ? list->row_count : 0;
}

void pt_list_view_set_row_height(pt_widget *list, float height) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    list->row_height = pt_max_float(1.0f, height);
    pt_widget_mark_dirty(list, true);
}

float pt_list_view_get_row_height(const pt_widget *list) {
    return list && list->kind == pt_widget_list ? list->row_height : 0.0f;
}

void pt_list_view_set_on_bind_row(pt_widget *list, pt_list_bind_callback callback,
                                  void *user_data) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    list->on_bind_row = callback;
    list->on_bind_row_data = user_data;
    list_mark_rebind(list);
    pt_widget_mark_dirty(list, true);
}

void pt_list_view_set_on_select(pt_widget *list, pt_index_callback callback, void *user_data) {
    if (list && list->kind == pt_widget_list) {
        list->on_select = callback;
        list->on_select_data = user_data;
    }
}

void pt_list_view_set_on_activate(pt_widget *list, pt_index_callback callback,
                                  void *user_data) {
    if (list && list->kind == pt_widget_list) {
        list->on_activate = callback;
        list->on_activate_data = user_data;
    }
}

uint32_t pt_list_view_get_selected(const pt_widget *list) {
    return list && list->kind == pt_widget_list ? list->list_selected
                                                 : pt_list_index_none;
}

void pt_list_view_set_selected(pt_widget *list, uint32_t index) {
    list_select(list, index, false);
}

void pt_list_view_refresh(pt_widget *list) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    if (list->row_key_callback && list->list_selected != pt_list_index_none) {
        uint64_t key = list->selected_key_valid
                           ? list->selected_key
                           : pt_row_key_at(list, list->list_selected);
        if (key != pt_row_key_none) {
            list->list_selected = pt_row_find_key(list, key);
            list->selected_key = key;
            list->selected_key_valid = list->list_selected != pt_list_index_none;
        } else {
            list->selected_key_valid = false;
        }
    }
    list_mark_rebind(list);
    pt_widget_mark_dirty(list, true);
}

void pt_list_view_refresh_rows(pt_widget *list, uint32_t first, uint32_t count) {
    if (!list || list->kind != pt_widget_list || count == 0 || first >= list->row_count) {
        return;
    }
    uint64_t end = (uint64_t)first + count;
    if (end > list->row_count) {
        end = list->row_count;
    }
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        if (row->list_bound && row->list_index >= first && row->list_index < end) {
            row->list_bound = false;
        }
    }
    pt_widget_mark_dirty(list, true);
}

void pt_list_view_set_row_key_callback(pt_widget *list, pt_row_key_callback callback,
                                       void *user_data) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    list->row_key_callback = callback;
    list->row_key_data = user_data;
    pt_data_view_sync_selected_key(list);
}

uint64_t pt_list_view_get_selected_key(const pt_widget *list) {
    if (!list || list->kind != pt_widget_list || !list->row_key_callback ||
        list->list_selected == pt_list_index_none) {
        return pt_row_key_none;
    }
    if (list->selected_key_valid) {
        return list->selected_key;
    }
    return pt_row_key_at(list, list->list_selected);
}

bool pt_list_view_set_selected_key(pt_widget *list, uint64_t key) {
    if (!list || list->kind != pt_widget_list) {
        return false;
    }
    if (key == pt_row_key_none) {
        list_select(list, pt_list_index_none, false);
        return true;
    }
    uint32_t row = pt_row_find_key(list, key);
    if (row == pt_list_index_none) {
        return false;
    }
    list_select(list, row, false);
    return true;
}

void pt_list_view_set_row_text_callback(pt_widget *list, pt_row_text_callback callback,
                                        void *user_data) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    list->row_text_callback = callback;
    list->row_text_data = user_data;
}

void pt_list_view_scroll_to_row(pt_widget *list, uint32_t index) {
    if (!list || list->kind != pt_widget_list || index >= list->row_count) {
        return;
    }
    list_reveal_index(list, index);
}

uint32_t pt_list_selection_count(const pt_widget *list) {
    if (!list || list->kind != pt_widget_list) {
        return 0;
    }
    const pt_list_state *state = list_state_const(list);
    if (!state) {
        return list->list_selected == pt_list_index_none ? 0u : 1u;
    }
    return state->selection_count;
}

bool pt_list_row_is_selected(const pt_widget *list, uint32_t row) {
    if (!list || list->kind != pt_widget_list || row >= list->row_count) {
        return false;
    }
    return list_row_is_selected(list, list_state_const(list), row);
}

void pt_list_select_index(pt_widget *list, uint32_t index, bool notify) {
    list_select(list, index, notify);
}

uint32_t pt_list_view_get_selection_count(const pt_widget *list) {
    return pt_list_selection_count(list);
}

bool pt_list_view_is_row_selected(const pt_widget *list, uint32_t row) {
    return pt_list_row_is_selected(list, row);
}

uint32_t pt_list_view_get_selection(const pt_widget *list, uint32_t *rows,
                                    uint32_t capacity) {
    const pt_list_state *state = list_state_const(list);
    if (!list || list->kind != pt_widget_list || !rows || capacity == 0) {
        return 0;
    }
    if (!state) {
        if (list->list_selected == pt_list_index_none) {
            return 0;
        }
        rows[0] = list->list_selected;
        return 1;
    }
    uint32_t written = 0;
    uint32_t limit = state->selection_capacity < list->row_count ? state->selection_capacity
                                                                  : list->row_count;
    for (uint32_t i = 0; i < limit && written < capacity; ++i) {
        if (state->selection[i]) {
            rows[written++] = i;
        }
    }
    return written;
}

void pt_list_view_select_range(pt_widget *list, uint32_t first, uint32_t last) {
    if (!list || list->kind != pt_widget_list || list->row_count == 0) {
        return;
    }
    pt_list_state *state = list_state(list);
    if (first > last) {
        uint32_t swap = first;
        first = last;
        last = swap;
    }
    if (first >= list->row_count) {
        first = list->row_count - 1;
    }
    if (last >= list->row_count) {
        last = list->row_count - 1;
    }
    list_selection_ensure(list, state);
    list_selection_reset(state);
    for (uint32_t i = first; i <= last; ++i) {
        list_selection_add(state, i);
    }
    state->selection_anchor = first;
    list->list_selected = first;
    pt_data_view_sync_selected_key(list);
    list_apply_selection(list);
    list_reveal_index(list, first);
    pt_widget_mark_dirty(list, false);
    pt_widget_a11y_emit(list, pt_a11y_event_selection_changed);
}

void pt_list_view_select_toggle(pt_widget *list, uint32_t row) {
    if (!list || list->kind != pt_widget_list || row >= list->row_count) {
        return;
    }
    pt_list_state *state = list_state(list);
    list_selection_ensure(list, state);
    bool was_selected = list_row_is_selected(list, state, row);
    if (was_selected) {
        list_selection_drop(state, row);
        if (list->list_selected == row) {
            /* The primary row has to name a row that is still selected, because
             * a row counts as selected while it is primary even if its bit is
             * clear. */
            list->list_selected = pt_list_index_none;
            uint32_t limit = state->selection_capacity < list->row_count
                               ? state->selection_capacity
                               : list->row_count;
            for (uint32_t i = 0; i < limit; ++i) {
                if (state->selection[i]) {
                    list->list_selected = i;
                    break;
                }
            }
        }
    } else {
        list_selection_add(state, row);
        list->list_selected = row;
        list_reveal_index(list, row);
    }
    state->selection_anchor = row;
    pt_data_view_sync_selected_key(list);
    list_apply_selection(list);
    pt_widget_mark_dirty(list, false);
    pt_widget_a11y_emit(list, pt_a11y_event_selection_changed);
}

void pt_list_view_clear_selection(pt_widget *list) {
    if (!list || list->kind != pt_widget_list) {
        return;
    }
    pt_list_state *state = list->extra;
    if (state) {
        list_selection_reset(state);
        /* With nothing selected there is no anchor left to range from, so the next
         * Shift-click starts where it lands instead of reaching back to a row the
         * application just let go of. */
        state->selection_anchor = pt_list_index_none;
    }
    list->list_selected = pt_list_index_none;
    pt_data_view_sync_selected_key(list);
    list_apply_selection(list);
    pt_widget_mark_dirty(list, false);
    pt_widget_a11y_emit(list, pt_a11y_event_selection_changed);
}

/* The public set helpers stay silent so an application can rebuild a selection
 * without echoing, but every gesture that moves the primary row reports it, the
 * way a plain click already does. */
static void list_notify_primary(pt_widget *list, uint32_t previous) {
    if (list->on_select && list->list_selected != previous &&
        list->list_selected != pt_list_index_none) {
        list->on_select(list, list->list_selected, list->on_select_data);
    }
}

static void list_apply_marquee(pt_widget *list, pt_list_state *state) {
    uint32_t previous = list->list_selected;
    pt_window *window = pt_widget_window(list);
    bool additive = window && (window->modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    list_selection_ensure(list, state);
    if (!additive) {
        list_selection_reset(state);
    }
    uint32_t before = state->selection_count;
    uint32_t first_hit = pt_list_index_none;
    for (pt_widget *row = list->first_child; row; row = row->next_sibling) {
        if (row->kind != pt_widget_list_row || !row->list_bound ||
            row->list_index >= list->row_count) {
            continue;
        }
        if (!pt_marquee_hit(&state->marquee,
                           (pt_rect){row->x, row->y, row->width, row->height})) {
            continue;
        }
        list_selection_add(state, row->list_index);
        if (first_hit == pt_list_index_none || row->list_index < first_hit) {
            first_hit = row->list_index;
        }
    }
    if (first_hit == pt_list_index_none) {
        list_apply_selection(list);
        pt_widget_mark_dirty(list, false);
        return;
    }
    list->list_selected = first_hit;
    pt_data_view_sync_selected_key(list);
    list_apply_selection(list);
    pt_widget_mark_dirty(list, false);
    if (state->selection_count != before) {
        pt_widget_a11y_emit(list, pt_a11y_event_selection_changed);
    }
    list_notify_primary(list, previous);
}

void pt_list_row_down(pt_widget *list, pt_widget *row, float x, float y) {
    pt_list_state *state = list_state(list);
    if (!state || !row || row->list_index >= list->row_count) {
        return;
    }
    uint32_t index = row->list_index;
    uint32_t previous = list->list_selected;
    pt_window *window = pt_widget_window(list);
    uint32_t modifiers = window ? window->modifiers : 0;
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool ctrl = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    state->shift_select = shift;
    state->toggle_select = ctrl;
    if (shift) {
        uint32_t anchor = state->selection_anchor < list->row_count ? state->selection_anchor
                                                                     : index;
        pt_list_view_select_range(list, anchor, index);
        list->list_selected = index;
        pt_data_view_sync_selected_key(list);
        state->selection_anchor = anchor;
        list_apply_selection(list);
    } else if (ctrl) {
        pt_list_view_select_toggle(list, index);
    } else {
        state->selection_anchor = index;
    }
    pt_marquee_begin(&state->marquee, x, y);
    pt_widget_mark_dirty(list, false);
    list_notify_primary(list, previous);
}

void pt_list_row_drag(pt_widget *list, float x, float y) {
    pt_list_state *state = list->extra;
    if (!state) {
        return;
    }
    if (pt_marquee_move(&state->marquee, x, y)) {
        list_apply_marquee(list, state);
    }
}

bool pt_list_row_up(pt_widget *list) {
    pt_list_state *state = list->extra;
    if (!state) {
        return false;
    }
    bool dragged = pt_marquee_end(&state->marquee);
    bool consumed = dragged || state->shift_select || state->toggle_select;
    state->shift_select = false;
    state->toggle_select = false;
    pt_widget_mark_dirty(list, false);
    return consumed;
}

/* The viewport background is the part of the view not covered by a row. A
 * press there is not a row click, but it still starts the shared band; this is
 * what lets a drag that begins below the last row sweep upward over the model. */
static bool list_view_background(const pt_widget *list, float x, float y) {
    if (!list || x < list->x + list->padding_left ||
        x > list->x + list->width - list->padding_right ||
        y < list->y + list->padding_top ||
        y > list->y + list->height - list->padding_bottom) {
        return false;
    }
    for (pt_widget *child = list->first_child; child; child = child->next_sibling) {
        if (!child->visible) {
            continue;
        }
        if (x >= child->x && x <= child->x + child->width && y >= child->y &&
            y <= child->y + child->height) {
            return false;
        }
    }
    return true;
}

void pt_list_view_down(pt_widget *list, float x, float y) {
    pt_list_state *state = list->extra;
    if (!state || !list->enabled || !list->visible || !list_view_background(list, x, y)) {
        return;
    }
    pt_window *window = pt_widget_window(list);
    uint32_t modifiers = window ? window->modifiers : 0;
    bool additive = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    if (!additive) {
        pt_list_view_clear_selection(list);
    }
    state->shift_select = false;
    state->toggle_select = false;
    pt_marquee_begin(&state->marquee, x, y);
    pt_widget_mark_dirty(list, false);
}

void pt_list_view_drag(pt_widget *list, float x, float y) {
    pt_list_state *state = list->extra;
    if (!state) {
        return;
    }
    if (pt_marquee_move(&state->marquee, x, y)) {
        list_apply_marquee(list, state);
    }
    pt_widget_mark_dirty(list, false);
}

bool pt_list_view_up(pt_widget *list) {
    pt_list_state *state = list->extra;
    if (!state) {
        return false;
    }
    bool dragged = pt_marquee_end(&state->marquee);
    state->shift_select = false;
    state->toggle_select = false;
    pt_widget_mark_dirty(list, false);
    return dragged;
}

void pt_list_free_extra(pt_widget *list) {
    pt_list_state *state = list && list->kind == pt_widget_list ? list->extra : NULL;
    if (!state) {
        return;
    }
    free(state->selection);
    free(state);
    list->extra = NULL;
}

void pt_widget_get_bounds(const pt_widget *widget, float *x, float *y, float *width,
                          float *height) {
    if (x) {
        *x = widget ? widget->x : 0.0f;
    }
    if (y) {
        *y = widget ? widget->y : 0.0f;
    }
    if (width) {
        *width = widget ? widget->width : 0.0f;
    }
    if (height) {
        *height = widget ? widget->height : 0.0f;
    }
}

/* ---------- fonts ---------- */

float pt_widget_font_size(const pt_widget *widget) {
    if (!widget) {
        return pt_default_font_size;
    }
    return widget->font_size > 0.0f ? widget->font_size : pt_default_font_size;
}

int pt_widget_font_weight(const pt_widget *widget) {
    return widget ? widget->font_weight : pt_font_weight_regular;
}

pt_font *pt_widget_font(const pt_widget *widget, float logical_size, pt_font_weight weight,
                        const pt_layout_ctx *ctx) {
    return pt_widget_font_styled(widget, logical_size, weight, pt_font_slant_upright, ctx);
}

pt_font *pt_widget_font_styled(const pt_widget *widget, float logical_size, pt_font_weight weight,
                               pt_font_slant slant, const pt_layout_ctx *ctx) {
    pt_unused(widget);
    const char *family = ctx && ctx->font_family ? ctx->font_family : "sans-serif";
    float scale = ctx && ctx->scale > 0.0f ? ctx->scale : 1.0f;
    return pt_font_acquire(family, (int)weight, slant, logical_size * scale);
}

pt_font *pt_widget_font_for(const pt_widget *widget, const pt_layout_ctx *ctx) {
    return pt_widget_font(widget, pt_widget_font_size(widget),
                          (pt_font_weight)pt_widget_font_weight(widget), ctx);
}

/* ---------- focus ---------- */

pt_widget *pt_widget_focusable_ancestor(pt_widget *widget) {
    for (pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (cursor->focusable) {
            return cursor;
        }
    }
    return NULL;
}

static bool widget_can_focus(const pt_widget *widget) {
    if (!widget || !widget->focusable || !widget->visible || !widget->enabled) {
        return false;
    }
    for (const pt_widget *cursor = widget->parent; cursor; cursor = cursor->parent) {
        if (!cursor->visible || !cursor->enabled) {
            return false;
        }
    }
    return true;
}

bool pt_widget_focus(pt_widget *widget) {
    if (!widget_can_focus(widget)) {
        return false;
    }
    pt_window *window = pt_widget_window(widget);
    if (!window) {
        widget->focused = true;
        return true;
    }
    if (window->focused == widget) {
        widget->focus_visible = true;
        entry_reset_caret(widget);
        return true;
    }
    if (window->focused) {
        pt_widget *previous = window->focused;
        previous->focused = false;
        pt_a11y_bridge_widget_state(previous, "focused", false);
        if (previous->editor_owner) {
            window->focused = NULL;
            pt_inline_editor_finish(previous, false);
        }
    }
    window->focused = widget;
    widget->focused = true;
    widget->focus_visible = true;
    pt_widget_a11y_emit(widget, pt_a11y_event_focus_changed);
    widget->pressed = false;
    entry_reset_caret(widget);
    window->caret_visible = true;
    window->needs_draw = true;
    pt_window_update_caret_timer(window);

    bool editable = pt_window_has_editable_focus(window);
    pt_platform_ime_set_enabled(window, editable);
    if (editable) {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        pt_widget_ime_rect(widget, &x, &y, &width, &height);
        pt_platform_ime_set_cursor_rect(window, x, y, width, height);
    }
    pt_platform_window_schedule_draw(window);
    return true;
}

bool pt_widget_has_focus(const pt_widget *widget) {
    return widget && widget->focused;
}

static void collect_focusables(pt_widget *widget, pt_widget ***items, size_t *count,
                               size_t *capacity) {
    if (!widget || !widget->visible || !widget->enabled) {
        return;
    }
    if (widget->focusable) {
        if (*count == *capacity) {
            *capacity = *capacity ? *capacity * 2 : 16;
            *items = pt_realloc_mem(*items, *capacity * sizeof(**items));
        }
        (*items)[(*count)++] = widget;
    }
    for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        collect_focusables(child, items, count, capacity);
    }
}

void pt_widget_focus_first(pt_window *window) {
    if (!window || !window->root) {
        return;
    }
    pt_widget **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    collect_focusables(window->root, &items, &count, &capacity);
    if (count > 0) {
        pt_widget_focus(items[0]);
    }
    free(items);
}

bool pt_widget_focus_next(pt_window *window, bool backwards) {
    if (!window || !window->root) {
        return false;
    }
    pt_widget **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    collect_focusables(window->root, &items, &count, &capacity);
    if (count == 0) {
        free(items);
        return false;
    }
    size_t index = count;
    for (size_t i = 0; i < count; ++i) {
        if (items[i] == window->focused) {
            index = i;
            break;
        }
    }
    size_t next = 0;
    if (index == count) {
        next = backwards ? count - 1 : 0;
    } else if (backwards) {
        next = (index + count - 1) % count;
    } else {
        next = (index + 1) % count;
    }
    bool result = pt_widget_focus(items[next]);
    free(items);
    return result;
}

void pt_widget_set_focus_visual(pt_widget *widget, bool focused) {
    if (!widget) {
        return;
    }
    widget->focused = focused;
    if (focused) {
        entry_reset_caret(widget);
    }
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_clear_focus_visual(pt_widget *widget) {
    pt_widget_set_focus_visual(widget, false);
}

/* ---------- activation and keyboard ---------- */

static bool kind_is_checked_control(pt_widget_kind kind) {
    return kind == pt_widget_toggle || kind == pt_widget_checkbox || kind == pt_widget_radio;
}

static void radio_uncheck_siblings(pt_widget *radio) {
    if (!radio->parent) {
        return;
    }
    for (pt_widget *sibling = radio->parent->first_child; sibling;
         sibling = sibling->next_sibling) {
        if (sibling != radio && sibling->kind == pt_widget_radio && sibling->checked) {
            sibling->checked = false;
            pt_widget_mark_dirty(sibling, false);
        }
    }
}

static void checked_control_set(pt_widget *widget, bool checked, bool notify) {
    if (!widget || !kind_is_checked_control(widget->kind)) {
        return;
    }
    if (widget->kind == pt_widget_radio && checked) {
        radio_uncheck_siblings(widget);
    }
    if (widget->checked == checked) {
        return;
    }
    widget->checked = checked;
    if (!notify) {
        widget->checked_mix = checked ? 1.0f : 0.0f;
    }
    pt_widget_mark_dirty(widget, false);
    pt_widget_a11y_emit(widget, pt_a11y_event_state_changed);
    pt_widget_a11y_emit(widget, pt_a11y_event_value_changed);
    if (notify && widget->on_value) {
        widget->on_value(widget, checked ? 1.0f : 0.0f, widget->on_value_data);
    }
}

static void segmented_select(pt_widget *segmented, uint32_t index, bool notify) {
    if (!segmented || segmented->kind != pt_widget_segmented || segmented->segment_count == 0) {
        return;
    }
    if (index >= segmented->segment_count) {
        index = segmented->segment_count - 1;
    }
    if (segmented->segment_selected == index) {
        return;
    }
    segmented->segment_selected = index;
    pt_widget_mark_dirty(segmented, false);
    pt_widget_a11y_emit(segmented, pt_a11y_event_selection_changed);
    if (notify && segmented->on_index) {
        segmented->on_index(segmented, index, segmented->on_index_data);
    }
}

bool pt_widget_activate(pt_widget *widget) {
    if (!widget || !widget->enabled || !widget->visible || widget->busy) {
        return false;
    }
    switch (widget->kind) {
    case pt_widget_button:
        if (widget->on_click) {
            widget->on_click(widget, widget->on_click_data);
        }
        return true;
    case pt_widget_chip:
        if (widget->on_click) {
            widget->on_click(widget, widget->on_click_data);
        }
        return true;
    case pt_widget_toggle:
    case pt_widget_checkbox:
        checked_control_set(widget, !widget->checked, true);
        return true;
    case pt_widget_radio:
        checked_control_set(widget, true, true);
        return true;
    case pt_widget_entry:
        if (widget->on_submit) {
            widget->on_submit(widget, widget->on_submit_data);
        }
        return true;
    case pt_widget_list_row:
        if (widget->popup_owner && widget->popup_owner->kind == pt_widget_combobox) {
            pt_combobox_choose_option(widget->popup_owner, widget->list_index);
            return true;
        }
        if (widget->parent && widget->parent->kind == pt_widget_list &&
            widget->list_index < widget->parent->row_count) {
            list_select(widget->parent, widget->list_index, true);
        }
        return true;
    case pt_widget_combobox:
        if (pt_combobox_is_open(widget)) {
            pt_combobox_close(widget);
        } else {
            pt_combobox_open(widget);
        }
        return true;
    case pt_widget_list:
        if (widget->list_selected < widget->row_count && widget->on_activate) {
            widget->on_activate(widget, widget->list_selected, widget->on_activate_data);
            return true;
        }
        return false;
    case pt_widget_table_row:
        if (widget->parent && widget->parent->kind == pt_widget_table &&
            widget->list_index < widget->parent->row_count) {
            pt_table_select(widget->parent, widget->list_index, true);
            return true;
        }
        return false;
    case pt_widget_table:
        return pt_table_activate_selected(widget);
    case pt_widget_tree_row:
        if (widget->parent && widget->parent->kind == pt_widget_tree && widget->tree_node) {
            pt_tree_select(widget->parent, widget->tree_node, true);
            return true;
        }
        return false;
    case pt_widget_tree:
        return pt_tree_activate_selected(widget);
    case pt_widget_sidebar_item:
        if (widget->parent && widget->parent->kind == pt_widget_sidebar) {
            uint32_t count = pt_sidebar_get_item_count(widget->parent);
            for (uint32_t i = 0; i < count; ++i) {
                if (pt_sidebar_get_item(widget->parent, i) == widget) {
                    pt_sidebar_select(widget->parent, i, true);
                    return true;
                }
            }
        }
        return false;
    default:
        return false;
    }
}

void pt_inline_editor_finish(pt_widget *editor, bool cancel) {
    if (!editor) {
        return;
    }
    pt_widget *owner = editor->editor_owner;
    if (!owner) {
        return;
    }
    if (owner->kind == pt_widget_table) {
        if (cancel) {
            pt_table_cancel_edit(owner);
        } else {
            pt_table_commit_edit(owner);
        }
    } else if (owner->kind == pt_widget_tree) {
        if (cancel) {
            pt_tree_cancel_rename(owner);
        } else {
            pt_tree_commit_rename(owner);
        }
    }
}

static bool list_key(pt_widget *list, uint32_t keysym, uint32_t modifiers) {
    if (list->row_count == 0) {
        return false;
    }
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    if ((modifiers & (uint32_t)pt_key_modifier_ctrl) != 0 &&
        (keysym == XKB_KEY_a || keysym == XKB_KEY_A)) {
        uint32_t previous = list->list_selected;
        pt_list_view_select_range(list, 0, list->row_count - 1);
        list_notify_primary(list, previous);
        return true;
    }
    uint32_t current = list->list_selected;
    uint32_t next = current;
    float row_height = pt_max_float(1.0f, list->row_height);
    float viewport = pt_max_float(1.0f, list->height - list->padding_top -
                                           list->padding_bottom);
    uint32_t page = (uint32_t)pt_max_float(1.0f, viewport / row_height);
    if (current == pt_list_index_none) {
        if (keysym == XKB_KEY_Down || keysym == XKB_KEY_Page_Down ||
            keysym == XKB_KEY_Home) {
            next = 0;
        } else if (keysym == XKB_KEY_Up || keysym == XKB_KEY_Page_Up ||
                   keysym == XKB_KEY_End) {
            next = list->row_count - 1;
        } else if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter ||
                   keysym == XKB_KEY_space) {
            return false;
        } else {
            return false;
        }
    } else {
        switch (keysym) {
        case XKB_KEY_Down:
            next = current + 1 < list->row_count ? current + 1 : current;
            break;
        case XKB_KEY_Up:
            next = current > 0 ? current - 1 : 0;
            break;
        case XKB_KEY_Page_Down:
            next = current + page < list->row_count ? current + page : list->row_count - 1;
            break;
        case XKB_KEY_Page_Up:
            next = current > page ? current - page : 0;
            break;
        case XKB_KEY_Home:
            next = 0;
            break;
        case XKB_KEY_End:
            next = list->row_count - 1;
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            return pt_widget_activate(list);
        default:
            return false;
        }
    }
    if (next == current) {
        return true;
    }
    if (shift && current != pt_list_index_none) {
        /* The anchor stays put so repeated Shift+ arrows extend and shrink the
         * same range instead of chasing the caret. */
        pt_list_state *state = list_state(list);
        uint32_t anchor =
            state->selection_anchor < list->row_count ? state->selection_anchor : current;
        pt_list_view_select_range(list, anchor, next);
        list->list_selected = next;
        pt_data_view_sync_selected_key(list);
        state->selection_anchor = anchor;
        list_apply_selection(list);
        list_reveal_index(list, next);
        pt_widget_mark_dirty(list, false);
        list_notify_primary(list, current);
    } else {
        list_select(list, next, true);
        pt_list_state *state = list->extra;
        if (state) {
            state->selection_anchor = next;
        }
    }
    return true;
}

static bool edit_shortcut(pt_widget *widget, uint32_t keysym, uint32_t modifiers) {
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool ctrl = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    bool alt = (modifiers & ((uint32_t)pt_key_modifier_alt |
                             (uint32_t)pt_key_modifier_logo)) != 0;
    if (!ctrl || alt) {
        return false;
    }
    switch (keysym) {
    case XKB_KEY_a:
    case XKB_KEY_A:
        entry_select_all(widget);
        widget->caret_goal_x = -1.0f;
        return true;
    case XKB_KEY_c:
    case XKB_KEY_C:
        entry_copy_selection(widget, false);
        return true;
    case XKB_KEY_x:
    case XKB_KEY_X:
        entry_copy_selection(widget, true);
        return true;
    case XKB_KEY_v:
    case XKB_KEY_V: {
        pt_window *window = pt_widget_window(widget);
        if (window && window->app && window->app->platform) {
            char *clipboard = pt_platform_clipboard_get(window);
            if (clipboard) {
                entry_insert_text(widget, clipboard);
                free(clipboard);
            }
        }
        return true;
    }
    case XKB_KEY_z:
    case XKB_KEY_Z:
        return shift ? pt_widget_text_redo(widget) : pt_widget_text_undo(widget);
    case XKB_KEY_y:
    case XKB_KEY_Y:
        return pt_widget_text_redo(widget);
    default:
        return false;
    }
}

static bool entry_key(pt_widget *entry, uint32_t keysym, uint32_t modifiers) {
    size_t len = entry_length(entry);
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool alt = (modifiers & ((uint32_t)pt_key_modifier_alt |
                             (uint32_t)pt_key_modifier_logo)) != 0;
    bool handled = false;
    if (edit_shortcut(entry, keysym, modifiers)) {
        return true;
    }
    if (alt) {
        return false;
    }
    switch (keysym) {
    case XKB_KEY_Left: {
        if (!shift && entry_has_selection(entry)) {
            size_t start = 0;
            size_t end = 0;
            entry_selection_range(entry, &start, &end);
            entry_set_cursor(entry, start, false);
        } else {
            size_t cursor = entry->cursor;
            if (cursor > 0) {
                cursor = pt_utf8_prev_boundary(entry->text, len, cursor);
            }
            if (cursor != entry->cursor || shift) {
                entry_set_cursor(entry, cursor, shift);
            }
        }
        entry_reset_caret(entry);
        handled = true;
        break;
    }
    case XKB_KEY_Right: {
        if (!shift && entry_has_selection(entry)) {
            size_t start = 0;
            size_t end = 0;
            entry_selection_range(entry, &start, &end);
            entry_set_cursor(entry, end, false);
        } else {
            size_t cursor = entry->cursor;
            if (cursor < len) {
                cursor = pt_utf8_next_boundary(entry->text, len, cursor);
            }
            if (cursor != entry->cursor || shift) {
                entry_set_cursor(entry, cursor, shift);
            }
        }
        entry_reset_caret(entry);
        handled = true;
        break;
    }
    case XKB_KEY_Home:
        entry_set_cursor(entry, 0, shift);
        entry_reset_caret(entry);
        handled = true;
        break;
    case XKB_KEY_End:
        entry_set_cursor(entry, len, shift);
        entry_reset_caret(entry);
        handled = true;
        break;
    case XKB_KEY_BackSpace:
        if (entry_has_selection(entry)) {
            size_t start = 0;
            size_t end = 0;
            entry_selection_range(entry, &start, &end);
            entry_delete_range(entry, start, end);
        } else if (entry->cursor > 0) {
            size_t start = pt_utf8_prev_boundary(entry->text, len, entry->cursor);
            entry_delete_range(entry, start, entry->cursor);
        }
        handled = true;
        break;
    case XKB_KEY_Delete:
        if (entry_has_selection(entry)) {
            size_t start = 0;
            size_t end = 0;
            entry_selection_range(entry, &start, &end);
            entry_delete_range(entry, start, end);
        } else if (entry->cursor < len) {
            size_t end = pt_utf8_next_boundary(entry->text, len, entry->cursor);
            entry_delete_range(entry, entry->cursor, end);
        }
        handled = true;
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (entry->on_submit) {
            entry->on_submit(entry, entry->on_submit_data);
        }
        entry_reset_caret(entry);
        handled = true;
        break;
    default:
        break;
    }
    if (handled) {
        pt_widget_mark_dirty(entry, false);
    }
    return handled;
}

static bool text_area_build_layout(pt_widget *area, pt_layout_ctx *ctx, pt_text_layout *layout) {
    *ctx = pt_layout_context_for(area);
    return pt_text_layout_build(area, ctx, pt_text_area_content_width(area), layout);
}

static void text_area_after_caret_move(pt_widget *area) {
    pt_layout_ctx ctx = pt_layout_context_for(area);
    pt_text_area_ensure_caret_visible(area, &ctx);
    entry_reset_caret(area);
    pt_widget_mark_dirty(area, false);
}

static bool text_area_vertical(pt_widget *area, uint32_t keysym, bool shift) {
    pt_layout_ctx ctx = {0};
    pt_text_layout layout = {0};
    if (!text_area_build_layout(area, &ctx, &layout)) {
        return false;
    }
    int line = pt_text_layout_line_for_position(&layout, layout.caret);
    float goal = area->caret_goal_x >= 0.0f
                     ? area->caret_goal_x
                     : pt_text_layout_x_for_position(&layout, line, layout.caret);
    int delta = keysym == XKB_KEY_Up || keysym == XKB_KEY_Page_Up ? -1 : 1;
    if (keysym == XKB_KEY_Page_Up || keysym == XKB_KEY_Page_Down) {
        float viewport = pt_max_float(0.0f, area->height - area->padding_top -
                                             area->padding_bottom);
        int page = layout.line_height > 0.0f
                       ? (int)(viewport / layout.line_height) - 1
                       : 1;
        if (page < 1) {
            page = 1;
        }
        delta *= page;
    }
    int target = line + delta;
    size_t position = 0;
    if (target < 0) {
        position = 0;
    } else if (target >= layout.line_count) {
        position = layout.display_len;
    } else {
        position = pt_text_layout_position_for_x(&layout, target, goal);
    }
    pt_text_layout_free(&layout);
    entry_set_cursor(area, position, shift);
    area->caret_goal_x = goal;
    text_area_after_caret_move(area);
    return true;
}

static bool text_area_line_edge(pt_widget *area, bool end, bool shift, bool document) {
    pt_layout_ctx ctx = {0};
    pt_text_layout layout = {0};
    if (!text_area_build_layout(area, &ctx, &layout)) {
        return false;
    }
    size_t position = 0;
    if (document) {
        position = end ? layout.display_len : 0;
    } else {
        int line = pt_text_layout_line_for_position(&layout, layout.caret);
        position = end ? layout.lines[line].end : layout.lines[line].start;
    }
    pt_text_layout_free(&layout);
    entry_set_cursor(area, position, shift);
    text_area_after_caret_move(area);
    return true;
}

static bool text_area_horizontal(pt_widget *area, bool right, bool shift) {
    size_t len = entry_length(area);
    size_t position = area->cursor;
    if (!shift && entry_has_selection(area)) {
        size_t start = 0;
        size_t end = 0;
        entry_selection_range(area, &start, &end);
        position = right ? end : start;
    } else if (right) {
        if (position < len) {
            position = pt_utf8_next_boundary(area->text, len, position);
        }
    } else if (position > 0) {
        position = pt_utf8_prev_boundary(area->text, len, position);
    }
    entry_set_cursor(area, position, shift);
    text_area_after_caret_move(area);
    return true;
}

static bool text_area_key(pt_widget *area, uint32_t keysym, uint32_t modifiers) {
    bool shift = (modifiers & (uint32_t)pt_key_modifier_shift) != 0;
    bool ctrl = (modifiers & (uint32_t)pt_key_modifier_ctrl) != 0;
    bool alt = (modifiers & ((uint32_t)pt_key_modifier_alt |
                             (uint32_t)pt_key_modifier_logo)) != 0;
    if (edit_shortcut(area, keysym, modifiers)) {
        return true;
    }
    if (alt) {
        return false;
    }
    size_t len = entry_length(area);
    switch (keysym) {
    case XKB_KEY_Left:
        return text_area_horizontal(area, false, shift);
    case XKB_KEY_Right:
        return text_area_horizontal(area, true, shift);
    case XKB_KEY_Up:
    case XKB_KEY_Down:
    case XKB_KEY_Page_Up:
    case XKB_KEY_Page_Down:
        return text_area_vertical(area, keysym, shift);
    case XKB_KEY_Home:
        return text_area_line_edge(area, false, shift, ctrl);
    case XKB_KEY_End:
        return text_area_line_edge(area, true, shift, ctrl);
    case XKB_KEY_BackSpace:
        if (entry_has_selection(area)) {
            size_t start = 0;
            size_t end = 0;
            entry_selection_range(area, &start, &end);
            entry_delete_range(area, start, end);
        } else if (area->cursor > 0) {
            size_t start = pt_utf8_prev_boundary(area->text, len, area->cursor);
            entry_delete_range(area, start, area->cursor);
        }
        return true;
    case XKB_KEY_Delete:
        if (entry_has_selection(area)) {
            size_t start = 0;
            size_t end = 0;
            entry_selection_range(area, &start, &end);
            entry_delete_range(area, start, end);
        } else if (area->cursor < len) {
            size_t end = pt_utf8_next_boundary(area->text, len, area->cursor);
            entry_delete_range(area, area->cursor, end);
        }
        return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        entry_insert_text(area, "\n");
        return true;
    default:
        return false;
    }
}

static int typeahead_lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool typeahead_matches(const char *text, const char *prefix) {
    if (!text || !prefix) {
        return false;
    }
    while (*prefix) {
        if (!*text || typeahead_lower((unsigned char)*text) !=
                          typeahead_lower((unsigned char)*prefix)) {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

static bool typeahead_same_char(const char *text, size_t length) {
    if (length == 0) {
        return false;
    }
    for (size_t i = 1; i < length; ++i) {
        if (text[i] != text[0]) {
            return false;
        }
    }
    return true;
}

static const char *typeahead_row_text(pt_widget *view, uint32_t row) {
    if (view->row_text_callback) {
        return view->row_text_callback(view, row, view->row_text_data);
    }
    if (view->kind == pt_widget_tree) {
        pt_tree_node *node = pt_tree_get_visible_node(view, row);
        return node ? pt_tree_node_get_text(node) : NULL;
    }
    for (pt_widget *child = view->first_child; child; child = child->next_sibling) {
        if (child->kind != pt_widget_list_row && child->kind != pt_widget_table_row) {
            continue;
        }
        if (!child->list_bound || !child->visible || child->list_index != row) {
            continue;
        }
        if (view->kind == pt_widget_table) {
            for (pt_widget *cell = child->first_child; cell; cell = cell->next_sibling) {
                if (cell->kind == pt_widget_table_cell && cell->visible && cell->text) {
                    return cell->text;
                }
            }
            return NULL;
        }
        return child->text;
    }
    return NULL;
}

static uint32_t typeahead_selected(pt_widget *view) {
    if (view->kind == pt_widget_tree) {
        pt_tree_rebuild(view);
        return pt_tree_index_of(view, pt_tree_get_selected(view));
    }
    return view->list_selected;
}

static void typeahead_select(pt_widget *view, uint32_t row) {
    if (view->kind == pt_widget_tree) {
        pt_tree_select_index(view, row, true);
    } else if (view->kind == pt_widget_table) {
        pt_table_select(view, row, true);
    } else {
        list_select(view, row, true);
    }
}

bool pt_data_view_typeahead(pt_widget *view, const char *utf8) {
    if (!view || !utf8 || !utf8[0]) {
        return false;
    }
    if (view->kind == pt_widget_tree) {
        pt_tree_rebuild(view);
    }
    uint32_t count = view->kind == pt_widget_tree ? pt_tree_get_visible_count(view)
                                                  : view->row_count;
    if (count == 0) {
        return false;
    }
    int64_t now = pt_now_ms();
    if (now - view->typeahead_at > 700) {
        view->typeahead_length = 0;
    }
    view->typeahead_at = now;

    size_t length = strlen(utf8);
    if (length >= sizeof(view->typeahead)) {
        length = sizeof(view->typeahead) - 1;
    }
    bool repeat = view->typeahead_length > 0 && length == 1 &&
                  typeahead_same_char(view->typeahead, view->typeahead_length) &&
                  utf8[0] == view->typeahead[0];
    if (repeat) {
        view->typeahead_length = 1;
        view->typeahead[0] = utf8[0];
        view->typeahead[1] = '\0';
    } else {
        if (view->typeahead_length + length >= sizeof(view->typeahead)) {
            view->typeahead_length = 0;
        }
        memcpy(view->typeahead + view->typeahead_length, utf8, length);
        view->typeahead_length += length;
        view->typeahead[view->typeahead_length] = '\0';
    }

    uint32_t current = typeahead_selected(view);
    uint32_t start = current == pt_list_index_none ? 0 : (current + 1) % count;
    for (uint32_t step = 0; step < count; ++step) {
        uint32_t row = (start + step) % count;
        if (typeahead_matches(typeahead_row_text(view, row), view->typeahead)) {
            typeahead_select(view, row);
            return true;
        }
    }
    return false;
}

/* The combo box that owns an embedded editor, or NULL. A filterable box builds
 * its entry as the only child of the combobox widget, so the parent link is
 * enough to route keys and presses back to the control. */
static pt_widget *combobox_of_field(pt_widget *widget) {
    if (!widget || !widget->parent || widget->parent->kind != pt_widget_combobox) {
        return NULL;
    }
    return pt_combobox_field_widget(widget->parent) == widget ? widget->parent : NULL;
}

/* A filterable box reads its navigation keys through the editor it embeds, so
 * the control gets first refusal on anything it binds. */
static bool combobox_field_key(pt_widget *widget, uint32_t keysym, uint32_t modifiers,
                               const char *utf8) {
    pt_widget *combo = combobox_of_field(widget);
    return combo && pt_combobox_key(combo, keysym, modifiers, utf8);
}

bool pt_widget_key(pt_widget *widget, uint32_t keysym, uint32_t modifiers, const char *utf8) {
    if (!widget || !widget->enabled || !widget->visible) {
        return false;
    }
    if ((widget->kind == pt_widget_list || widget->kind == pt_widget_table ||
         widget->kind == pt_widget_tree) &&
        utf8 && utf8[0] >= 0x20 && utf8[0] != 0x7f &&
        !(modifiers & ((uint32_t)pt_key_modifier_ctrl | (uint32_t)pt_key_modifier_alt |
                       (uint32_t)pt_key_modifier_logo)) &&
        pt_data_view_typeahead(widget, utf8)) {
        return true;
    }
    switch (widget->kind) {
    case pt_widget_entry:
        if (widget->editor_owner) {
            if (keysym == XKB_KEY_Escape) {
                pt_inline_editor_finish(widget, true);
                return true;
            }
            if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
                pt_inline_editor_finish(widget, false);
                return true;
            }
        }
        if (combobox_field_key(widget, keysym, modifiers, utf8)) {
            return true;
        }
        if (entry_key(widget, keysym, modifiers)) {
            return true;
        }
        if (utf8 && utf8[0] &&
            !(modifiers & ((uint32_t)pt_key_modifier_ctrl | (uint32_t)pt_key_modifier_alt |
                           (uint32_t)pt_key_modifier_logo))) {
            return pt_widget_text_input(widget, utf8);
        }
        return false;
    case pt_widget_text_area:
        if (text_area_key(widget, keysym, modifiers)) {
            return true;
        }
        if (utf8 && utf8[0] &&
            !(modifiers & ((uint32_t)pt_key_modifier_ctrl | (uint32_t)pt_key_modifier_alt |
                           (uint32_t)pt_key_modifier_logo))) {
            return pt_widget_text_input(widget, utf8);
        }
        return false;
    case pt_widget_button:
    case pt_widget_toggle:
    case pt_widget_checkbox:
    case pt_widget_radio:
        if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter || keysym == XKB_KEY_space) {
            return pt_widget_activate(widget);
        }
        return false;
    case pt_widget_segmented: {
        uint32_t selected = widget->segment_selected;
        if (keysym == XKB_KEY_Left || keysym == XKB_KEY_Up) {
            selected = selected > 0 ? selected - 1 : 0;
        } else if (keysym == XKB_KEY_Right || keysym == XKB_KEY_Down) {
            selected = selected + 1 < widget->segment_count ? selected + 1
                                                            : widget->segment_count - 1;
        } else if (keysym == XKB_KEY_Home) {
            selected = 0;
        } else if (keysym == XKB_KEY_End) {
            selected = widget->segment_count - 1;
        } else {
            return false;
        }
        segmented_select(widget, selected, true);
        return true;
    }
    case pt_widget_list:
        return list_key(widget, keysym, modifiers);
    case pt_widget_table:
        return pt_table_key(widget, keysym, modifiers);
    case pt_widget_tree:
        return pt_tree_key(widget, keysym, modifiers);
    case pt_widget_tabs:
        return pt_tabs_key(widget, keysym);
    case pt_widget_sidebar:
        return pt_sidebar_key(widget, keysym);
    case pt_widget_combobox:
        return pt_combobox_key(widget, keysym, modifiers, utf8);
    case pt_widget_slider: {
        float step = (modifiers & (uint32_t)pt_key_modifier_shift) ? 0.1f : 0.02f;
        float value = widget->value;
        if (keysym == XKB_KEY_Left || keysym == XKB_KEY_Down) {
            value -= step;
        } else if (keysym == XKB_KEY_Right || keysym == XKB_KEY_Up) {
            value += step;
        } else if (keysym == XKB_KEY_Home) {
            value = 0.0f;
        } else if (keysym == XKB_KEY_End) {
            value = 1.0f;
        } else {
            return false;
        }
        widget->value = pt_clamp_float(value, 0.0f, 1.0f);
        if (widget->on_value) {
            widget->on_value(widget, widget->value, widget->on_value_data);
        }
        pt_widget_mark_dirty(widget, false);
        return true;
    }
    default:
        return false;
    }
}

bool pt_widget_text_input(pt_widget *widget, const char *text) {
    if (!widget_is_text_input(widget) || !widget->enabled || !text) {
        return false;
    }
    free(widget->preedit);
    widget->preedit = NULL;
    widget->preedit_cursor_begin = -1;
    widget->preedit_cursor_end = -1;
    bool single_line = widget->kind == pt_widget_entry;
    bool submit = single_line && strchr(text, '\n') != NULL;
    entry_insert_text(widget, text);
    if (submit && widget->on_submit) {
        widget->on_submit(widget, widget->on_submit_data);
    }
    return true;
}

bool pt_widget_preedit(pt_widget *widget, const char *text) {
    size_t length = text ? strlen(text) : 0;
    return pt_widget_preedit_ex(widget, text, (int32_t)length, (int32_t)length);
}

void pt_widget_ime_rect(const pt_widget *widget, int *x, int *y, int *w, int *h) {
    int out_x = 0;
    int out_y = 0;
    int out_w = 2;
    int out_h = 18;
    if (widget && widget->kind == pt_widget_entry) {
        pt_layout_ctx ctx = pt_layout_context_for(widget);
        pt_font *font = pt_widget_font_for(widget, &ctx);
        if (font) {
            float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
            float line_height = font->line_height / scale;
            float caret = widget->padding_left + entry_caret_offset(widget, font, scale) -
                          widget->scroll_x;
            out_x = (int)lroundf(widget->x + caret);
            out_y = (int)lroundf(widget->y +
                                 (widget->height - line_height) * 0.5f);
            out_w = 2;
            out_h = (int)lroundf(line_height);
        }
    } else if (widget && widget->kind == pt_widget_text_area) {
        pt_layout_ctx ctx = pt_layout_context_for(widget);
        pt_text_area_ensure_caret_visible((pt_widget *)widget, &ctx);
        pt_text_area_caret_box(widget, &ctx, &out_x, &out_y, &out_w, &out_h);
    }
    if (x) {
        *x = out_x;
    }
    if (y) {
        *y = out_y;
    }
    if (w) {
        *w = out_w;
    }
    if (h) {
        *h = out_h;
    }
}

void pt_widget_entry_ensure_caret_visible(pt_widget *entry, float available_width,
                                          pt_font *font, float scale) {
    if (!entry || entry->kind != pt_widget_entry || !font) {
        return;
    }
    float caret = entry_caret_offset(entry, font, scale);
    float left = entry->scroll_x;
    float right = left + pt_max_float(1.0f, available_width);
    if (caret < left) {
        entry->scroll_x = caret;
    } else if (caret > right - 4.0f && available_width > 0.0f) {
        entry->scroll_x = caret - available_width + 12.0f;
    }
    if (entry->scroll_x < 0.0f) {
        entry->scroll_x = 0.0f;
    }
}

bool pt_widget_entry_changed(pt_widget *entry) {
    if (!entry || entry->kind != pt_widget_entry) {
        return false;
    }
    entry_notify_text(entry);
    return true;
}

void pt_widget_invalidate_caret_rect(pt_window *window) {
    if (!window || !window->focused ||
        !pt_widget_kind_is_text_input(window->focused->kind)) {
        return;
    }
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    pt_widget_ime_rect(window->focused, &x, &y, &w, &h);
    pt_platform_ime_set_cursor_rect(window, x, y, w, h);
}

/* ---------- pointer input ---------- */

void pt_widget_pointer_enter(pt_widget *widget, float x, float y) {
    pt_unused(x);
    pt_unused(y);
    if (!widget) {
        return;
    }
    widget->hovered = true;
    pt_widget_mark_dirty(widget, false);
}

void pt_widget_pointer_leave(pt_widget *widget) {
    if (!widget) {
        return;
    }
    widget->hovered = false;
    if (widget->kind != pt_widget_slider || !widget->sliding) {
        widget->pressed = false;
        widget->pressed_inside = false;
    }
    pt_widget_mark_dirty(widget, false);
}

static void slider_set_from_x(pt_widget *slider, float x, bool notify) {
    float track_left = slider->x + 9.0f;
    float track_width = slider->width - 18.0f;
    if (track_width <= 1.0f) {
        return;
    }
    float value = pt_clamp_float((x - track_left) / track_width, 0.0f, 1.0f);
    if (value != slider->value) {
        slider->value = value;
        if (notify && slider->on_value) {
            slider->on_value(slider, value, slider->on_value_data);
        }
        pt_widget_mark_dirty(slider, false);
        pt_widget_a11y_emit(slider, pt_a11y_event_value_changed);
    }
}

static bool chip_remove_hit(const pt_widget *chip, float x, float y);

void pt_widget_pointer_down(pt_widget *widget, float x, float y) {
    pt_unused(y);
    if (!widget || !widget->enabled || !widget->visible) {
        return;
    }
    widget->pressed = true;
    widget->pressed_inside = true;
    if (pt_widget_kind_is_text_input(widget->kind)) {
        pt_layout_ctx ctx = pt_layout_context_for(widget);
        size_t position = 0;
        if (widget->kind == pt_widget_entry) {
            pt_font *font = pt_widget_font_for(widget, &ctx);
            if (font) {
                position = entry_position_from_x(widget, font, ctx.scale,
                                                 x - widget->x - widget->padding_left);
            }
        } else {
            position = pt_text_area_position_at(widget, &ctx, x, y);
        }
        entry_set_cursor(widget, position, false);
        widget->selecting = true;
        entry_reset_caret(widget);
        if (widget->kind == pt_widget_entry) {
            pt_widget *combo = combobox_of_field(widget);
            if (combo) {
                pt_combobox_pointer_down(combo, x, y);
            }
        }
    } else if (widget->kind == pt_widget_segmented) {
        float inner_x = widget->x + widget->padding_left + 3.0f;
        float inner_w = widget->width - widget->padding_left - widget->padding_right - 6.0f;
        if (inner_w > 0.0f && widget->segment_count > 0) {
            float index = (x - inner_x) / (inner_w / (float)widget->segment_count);
            if (index < 0.0f) {
                index = 0.0f;
            }
            segmented_select(widget, (uint32_t)index, true);
        }
    } else if (widget->kind == pt_widget_chip) {
        widget->chip_remove_pressed = chip_remove_hit(widget, x, y);
    } else if (widget->kind == pt_widget_slider) {
        widget->sliding = true;
        slider_set_from_x(widget, x, true);
    } else if (widget->kind == pt_widget_table_header_cell && widget->parent &&
               widget->parent->parent &&
               widget->parent->parent->kind == pt_widget_table) {
        pt_table_header_down(widget->parent->parent, widget, x);
    } else if (widget->kind == pt_widget_list_row && widget->parent &&
               widget->parent->kind == pt_widget_list) {
        pt_list_row_down(widget->parent, widget, x, y);
    } else if (widget->kind == pt_widget_table_row && widget->parent &&
               widget->parent->kind == pt_widget_table) {
        pt_table_row_down(widget->parent, widget, x, y);
    } else if (widget->kind == pt_widget_tree_row && widget->parent &&
               widget->parent->kind == pt_widget_tree) {
        pt_tree_row_down(widget->parent, widget, x, y);
    } else if (widget->kind == pt_widget_list) {
        pt_list_view_down(widget, x, y);
    } else if (widget->kind == pt_widget_table) {
        pt_table_view_down(widget, x, y);
    } else if (widget->kind == pt_widget_tree) {
        pt_tree_view_down(widget, x, y);
    } else if (widget->kind == pt_widget_tabs) {
        pt_tabs_pointer_down(widget, x, y);
    } else if (widget->kind == pt_widget_combobox) {
        pt_combobox_pointer_down(widget, x, y);
    }
    pt_widget_mark_dirty(widget, false);
}

static bool chip_remove_hit(const pt_widget *chip, float x, float y) {
    if (!chip->chip_removable || y < chip->y || y > chip->y + chip->height) {
        return false;
    }
    return x >= chip->x + chip->width - 26.0f && x <= chip->x + chip->width;
}

void pt_widget_pointer_up(pt_widget *widget, bool inside) {
    if (!widget) {
        return;
    }
    bool chip_remove = widget->kind == pt_widget_chip && inside &&
                       widget->chip_remove_pressed;
    bool activate = widget->pressed && inside &&
                    !pt_widget_kind_is_text_input(widget->kind) &&
                    widget->kind != pt_widget_slider;
    widget->pressed = false;
    widget->pressed_inside = false;
    widget->chip_remove_pressed = false;
    if (widget->kind == pt_widget_slider) {
        widget->sliding = false;
    }
    if (widget->kind == pt_widget_table_header_cell && widget->parent &&
        widget->parent->parent && widget->parent->parent->kind == pt_widget_table) {
        pt_table_pointer_up(widget->parent->parent);
    }
    bool row_dragged = false;
    if (widget->kind == pt_widget_table_row && widget->parent &&
        widget->parent->kind == pt_widget_table) {
        row_dragged = pt_table_row_up(widget->parent);
    } else if (widget->kind == pt_widget_list_row && widget->parent &&
               widget->parent->kind == pt_widget_list) {
        row_dragged = pt_list_row_up(widget->parent);
    } else if (widget->kind == pt_widget_tree_row && widget->parent &&
               widget->parent->kind == pt_widget_tree) {
        row_dragged = pt_tree_row_up(widget->parent);
    } else if (widget->kind == pt_widget_list) {
        pt_list_view_up(widget);
        /* An empty-viewport click is a selection gesture, never an activation
         * of the row that happened to be primary before it. */
        row_dragged = true;
    } else if (widget->kind == pt_widget_table) {
        pt_table_view_up(widget);
        row_dragged = true;
    } else if (widget->kind == pt_widget_tree) {
        pt_tree_view_up(widget);
        row_dragged = true;
    }
    if (pt_widget_kind_is_text_input(widget->kind)) {
        widget->selecting = false;
    }
    pt_widget_mark_dirty(widget, false);
    if (activate && chip_remove && widget->on_remove) {
        widget->on_remove(widget, widget->on_remove_data);
        return;
    }
    if (activate && !row_dragged) {
        pt_widget_activate(widget);
    }
}

void pt_widget_pointer_drag(pt_widget *widget, float x, float y) {
    if (!widget) {
        return;
    }
    if (widget->kind == pt_widget_slider && widget->sliding) {
        slider_set_from_x(widget, x, true);
    } else if (widget->kind == pt_widget_table_header_cell && widget->parent &&
               widget->parent->parent &&
               widget->parent->parent->kind == pt_widget_table) {
        pt_table_pointer_drag(widget->parent->parent, x);
    } else if (widget->kind == pt_widget_table_row && widget->parent &&
               widget->parent->kind == pt_widget_table) {
        pt_table_row_drag(widget->parent, x, y);
    } else if (widget->kind == pt_widget_list_row && widget->parent &&
               widget->parent->kind == pt_widget_list) {
        pt_list_row_drag(widget->parent, x, y);
    } else if (widget->kind == pt_widget_tree_row && widget->parent &&
               widget->parent->kind == pt_widget_tree) {
        pt_tree_row_drag(widget->parent, x, y);
    } else if (widget->kind == pt_widget_list) {
        pt_list_view_drag(widget, x, y);
    } else if (widget->kind == pt_widget_table) {
        pt_table_view_drag(widget, x, y);
    } else if (widget->kind == pt_widget_tree) {
        pt_tree_view_drag(widget, x, y);
    } else if (pt_widget_kind_is_text_input(widget->kind) && widget->selecting) {
        pt_layout_ctx ctx = pt_layout_context_for(widget);
        size_t position = 0;
        if (widget->kind == pt_widget_entry) {
            pt_font *font = pt_widget_font_for(widget, &ctx);
            if (font) {
                position = entry_position_from_x(widget, font, ctx.scale,
                                                 x - widget->x - widget->padding_left);
            }
        } else {
            position = pt_text_area_position_at(widget, &ctx, x, y);
        }
        entry_set_cursor(widget, position, true);
        if (widget->kind == pt_widget_text_area) {
            pt_text_area_ensure_caret_visible(widget, &ctx);
        }
        entry_reset_caret(widget);
        pt_widget_mark_dirty(widget, false);
    }
    if (widget->pressed) {
        widget->pressed_inside = x >= widget->x && x <= widget->x + widget->width &&
                                 y >= widget->y && y <= widget->y + widget->height;
    }
}

void pt_widget_pointer_multi_click(pt_widget *widget, float x, float y, uint32_t click_count) {
    if (!widget || click_count < 2) {
        return;
    }
    if (widget->kind == pt_widget_table_row && widget->parent &&
        widget->parent->kind == pt_widget_table) {
        uint32_t column = pt_table_column_none;
        for (pt_widget *cell = widget->first_child; cell; cell = cell->next_sibling) {
            if (cell->kind != pt_widget_table_cell || !cell->visible) {
                continue;
            }
            if (x >= cell->x && x <= cell->x + cell->width) {
                column = cell->column_index;
                break;
            }
        }
        if (column == pt_table_column_none) {
            for (pt_widget *cell = widget->first_child; cell; cell = cell->next_sibling) {
                if (cell->kind == pt_widget_table_cell && cell->visible) {
                    column = cell->column_index;
                    break;
                }
            }
        }
        if (column != pt_table_column_none) {
            pt_table_begin_edit(widget->parent, widget->list_index, column);
        }
        return;
    }
    if (widget->kind == pt_widget_table_cell && widget->parent &&
        widget->parent->kind == pt_widget_table_row && widget->parent->parent &&
        widget->parent->parent->kind == pt_widget_table) {
        pt_table_begin_edit(widget->parent->parent, widget->parent->list_index,
                            widget->column_index);
        return;
    }
    if (widget->kind == pt_widget_tree_row && widget->parent &&
        widget->parent->kind == pt_widget_tree) {
        pt_tree_begin_rename(widget->parent, widget->tree_node);
        return;
    }
    if (!pt_widget_kind_is_text_input(widget->kind)) {
        return;
    }
    if (click_count == 2) {
        pt_layout_ctx ctx = pt_layout_context_for(widget);
        size_t position = 0;
        bool have_position = false;
        if (widget->kind == pt_widget_entry) {
            pt_font *font = pt_widget_font_for(widget, &ctx);
            if (font) {
                position = entry_position_from_x(widget, font, ctx.scale,
                                                 x - widget->x - widget->padding_left);
                have_position = true;
            }
        } else {
            position = pt_text_area_position_at(widget, &ctx, x, y);
            have_position = true;
        }
        if (have_position) {
            entry_select_word(widget, position);
        }
    } else {
        entry_select_all(widget);
    }
    pt_widget_mark_dirty(widget, false);
}

char *pt_entry_selection_text_at(pt_widget *entry, float x, float y) {
    if (!entry || entry->kind != pt_widget_entry || !entry->drag_enabled || entry->password ||
        !entry->enabled || !entry->visible || !entry_has_selection(entry)) {
        return NULL;
    }
    if (!pt_rect_contains((pt_rect){entry->x, entry->y, entry->width, entry->height}, x, y)) {
        return NULL;
    }
    size_t start = 0;
    size_t end = 0;
    entry_selection_range(entry, &start, &end);
    if (start >= end) {
        return NULL;
    }
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    if (!font) {
        return NULL;
    }
    size_t position =
        entry_position_from_x(entry, font, ctx.scale, x - entry->x - entry->padding_left);
    if (position < start || position > end) {
        return NULL;
    }
    return entry_selected_text(entry);
}

bool pt_widget_scroll_pixels(pt_widget *widget, float pixels) {
    for (pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (cursor->kind == pt_widget_text_area) {
            float viewport = pt_max_float(0.0f, cursor->height - cursor->padding_top -
                                                 cursor->padding_bottom);
            float max_scroll = pt_max_float(0.0f, cursor->scroll_content_height - viewport);
            float next = pt_clamp_float(cursor->scroll_y + pixels, 0.0f, max_scroll);
            if (next == cursor->scroll_y) {
                return false;
            }
            cursor->scroll_y = next;
            pt_widget_mark_dirty(cursor, false);
            return true;
        }
        if (cursor->kind == pt_widget_list) {
            float viewport = pt_max_float(0.0f, cursor->height - cursor->padding_top -
                                                 cursor->padding_bottom);
            float content = (float)cursor->row_count * pt_max_float(1.0f, cursor->row_height);
            float max_scroll = pt_max_float(0.0f, content - viewport);
            float next = pt_clamp_float(cursor->scroll_y + pixels, 0.0f, max_scroll);
            if (next == cursor->scroll_y) {
                return false;
            }
            cursor->scroll_y = next;
            pt_widget_mark_dirty(cursor, true);
            return true;
        }
        if (cursor->kind == pt_widget_table) {
            if (pt_table_scroll(cursor, pixels)) {
                return true;
            }
            continue;
        }
        if (cursor->kind == pt_widget_tree || cursor->kind == pt_widget_sidebar) {
            float viewport = pt_max_float(0.0f, cursor->height - cursor->padding_top -
                                                 cursor->padding_bottom);
            float max_scroll = pt_max_float(0.0f, cursor->scroll_content_height - viewport);
            float next = pt_clamp_float(cursor->scroll_y + pixels, 0.0f, max_scroll);
            if (next == cursor->scroll_y) {
                continue;
            }
            cursor->scroll_y = next;
            pt_widget_mark_dirty(cursor, true);
            return true;
        }
        if (cursor->kind == pt_widget_scroll) {
            float viewport = cursor->height;
            float max_scroll = pt_max_float(0.0f, cursor->scroll_content_height - viewport);
            float next = pt_clamp_float(cursor->scroll_y + pixels, 0.0f, max_scroll);
            if (next == cursor->scroll_y) {
                return false;
            }
            cursor->scroll_y = next;
            pt_widget_mark_dirty(cursor, true);
            return true;
        }
    }
    return false;
}

void pt_widget_wheel(pt_widget *widget, float delta) {
    pt_widget_scroll_pixels(widget, delta * 48.0f);
}

pt_cursor pt_widget_cursor(const pt_widget *widget) {
    if (!widget || !widget->enabled) {
        return pt_cursor_default;
    }
    switch (widget->kind) {
    case pt_widget_button:
    case pt_widget_toggle:
    case pt_widget_slider:
    case pt_widget_checkbox:
    case pt_widget_radio:
    case pt_widget_segmented:
    case pt_widget_chip:
    case pt_widget_combobox:
        return pt_cursor_pointer;
    case pt_widget_entry:
    case pt_widget_text_area:
        return pt_cursor_text;
    case pt_widget_list:
    case pt_widget_list_row:
    case pt_widget_table:
    case pt_widget_table_row:
    case pt_widget_table_cell:
    case pt_widget_table_header_cell:
    case pt_widget_tree_row:
    case pt_widget_tabs:
    case pt_widget_sidebar_item:
        return pt_cursor_pointer;
    default:
        return pt_cursor_default;
    }
}

pt_cursor pt_widget_cursor_at(const pt_widget *widget, float x, float y) {
    if (!widget || !widget->enabled) {
        return pt_cursor_default;
    }
    if (widget->kind == pt_widget_table) {
        return pt_table_cursor(widget, x, y);
    }
    if (widget->kind == pt_widget_table_header_cell && widget->parent &&
        widget->parent->parent && widget->parent->parent->kind == pt_widget_table) {
        return pt_table_cursor(widget->parent->parent, x, y);
    }
    return pt_widget_cursor(widget);
}

void pt_widget_pointer_hover(pt_widget *widget, float x, float y) {
    if (widget && !widget->hovered) {
        pt_widget_pointer_enter(widget, x, y);
    }
}

/* ---------- animation and hit testing ---------- */

static void animate_value(pt_widget *widget, float *value, float target, float dt,
                          bool *changed) {
    pt_app *app = pt_widget_app(widget);
    float next = app && app->reduced_motion ? target
                                            : pt_ease_towards(*value, target, 24.0f, dt);
    if (next != *value) {
        *value = next;
        *changed = true;
        pt_widget_damage(widget, 3.0f);
    }
}

bool pt_widget_update_animations(pt_widget *widget, float dt) {
    if (!widget || !widget->visible) {
        return false;
    }
    bool changed = false;
    animate_value(widget, &widget->hover_mix, widget->hovered && widget->enabled ? 1.0f : 0.0f,
                  dt, &changed);
    animate_value(widget, &widget->press_mix, widget->pressed && widget->enabled ? 1.0f : 0.0f,
                  dt, &changed);
    animate_value(widget, &widget->checked_mix, widget->checked ? 1.0f : 0.0f, dt, &changed);
    if (widget->kind == pt_widget_spinner) {
        widget->phase += dt * 0.85f;
        if (widget->phase >= 1.0f) {
            widget->phase -= 1.0f;
        }
        changed = true;
        pt_widget_damage(widget, 3.0f);
    } else if (widget->kind == pt_widget_progress && widget->indeterminate) {
        widget->phase += dt * 0.5f;
        if (widget->phase >= 1.0f) {
            widget->phase -= 1.0f;
        }
        changed = true;
        pt_widget_damage(widget, 3.0f);
    } else if (widget->kind == pt_widget_tabs &&
               pt_tabs_update_animation(widget, dt)) {
        changed = true;
    }
    if (widget->kind == pt_widget_skeleton || widget->busy) {
        widget->phase += dt * (widget->busy ? 0.9f : 0.4f);
        if (widget->phase >= 1.0f) {
            widget->phase -= 1.0f;
        }
        changed = true;
        pt_widget_damage(widget, 3.0f);
    }
    if (widget->kind == pt_widget_segmented && widget->segment_indicator_ready) {
        animate_value(widget, &widget->segment_x, widget->segment_target_x, dt, &changed);
        animate_value(widget, &widget->segment_width, widget->segment_target_width, dt,
                      &changed);
    }
    for (pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        if (pt_widget_update_animations(child, dt)) {
            changed = true;
        }
    }
    return changed;
}

static bool widget_owns_pointer(const pt_widget *widget) {
    switch (widget->kind) {
    case pt_widget_button:
    case pt_widget_entry:
    case pt_widget_text_area:
    case pt_widget_toggle:
    case pt_widget_slider:
    case pt_widget_checkbox:
    case pt_widget_radio:
    case pt_widget_segmented:
    case pt_widget_scroll:
    case pt_widget_chip:
        return true;
    default:
        return false;
    }
}

pt_widget *pt_widget_interaction_target(pt_widget *widget) {
    if (!widget) {
        return NULL;
    }
    for (pt_widget *cursor = widget; cursor; cursor = cursor->parent) {
        if (widget_owns_pointer(cursor)) {
            return widget;
        }
        if (cursor->kind == pt_widget_list_row) {
            return cursor;
        }
        if (cursor->kind == pt_widget_table_cell) {
            return cursor->parent;
        }
    }
    return widget;
}

pt_widget *pt_widget_hit_test(pt_widget *root, float x, float y) {
    if (!root || !root->visible) {
        return NULL;
    }
    if (root->clip.w <= 0.0f || root->clip.h <= 0.0f) {
        return NULL;
    }
    if (!pt_rect_contains(root->clip, x, y)) {
        return NULL;
    }
    for (pt_widget *child = root->first_child; child; child = child->next_sibling) {
        pt_widget *hit = pt_widget_hit_test(child, x, y);
        if (hit) {
            return hit;
        }
    }
    if (pt_rect_contains((pt_rect){root->x, root->y, root->width, root->height}, x, y)) {
        return root;
    }
    return NULL;
}
