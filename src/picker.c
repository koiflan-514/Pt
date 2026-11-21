/*
 * 1.2 pickers: searchable select, date picker, number picker and colour picker.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * A picker lives on the shipped combo-box widget kind: that kind is the one the
 * framework dispatches keyboard, pointer, measurement, layout and teardown
 * hooks to, and its extra already carries an entry field and a popup slot. The
 * item model of the combo box stays unused; the picker state below owns the
 * model and the real pt_popup surface. src/combobox.c forwards to this file
 * whenever extra->picker is set.
 *
 * The popup is an application-owned pt_popup, so user popups, pickers and the
 * rest of the overlay family share placement, dismiss and headless fallback.
 * Its content tree is built from ordinary widgets and kept across dismiss; the
 * rows are rebuilt in place so a filter keystroke never replaces the popup.
 */

#include "internal.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define picker_popup_gap 4.0f
#define picker_popup_max_height 260.0f
#define picker_popup_padding 6.0f
#define picker_popup_radius 14.0f
#define picker_row_height 30.0f
#define picker_row_gap 1.0f
#define picker_row_radius 8.0f
#define picker_scroll_gutter 12.0f
#define picker_min_width 160.0f
#define picker_default_max_rows 8u
#define picker_query_max 256u

static void picker_field_changed(pt_widget *field, const char *text, void *user_data);
static void picker_on_dismiss(pt_popup *popup, void *user_data);

pt_picker *pt_picker_of(const pt_widget *widget) {
    if (!widget || widget->kind != pt_widget_combobox) {
        return NULL;
    }
    pt_combobox_state *state = widget->extra;
    return state ? state->picker : NULL;
}

pt_picker *pt_picker_of_kind(const pt_widget *widget, pt_picker_kind kind) {
    pt_picker *picker = pt_picker_of(widget);
    return picker && picker->kind == kind ? picker : NULL;
}

/* ---------- shared text helpers ---------- */

static char picker_fold(char value) {
    return value >= 'A' && value <= 'Z' ? (char)(value + 32) : value;
}

static bool picker_contains(const char *text, const char *needle) {
    if (!needle || needle[0] == '\0') {
        return true;
    }
    if (!text) {
        return false;
    }
    size_t length = strlen(needle);
    for (const char *cursor = text; *cursor; ++cursor) {
        size_t step = 0;
        while (step < length && picker_fold(cursor[step]) == picker_fold(needle[step])) {
            ++step;
        }
        if (step == length) {
            return true;
        }
    }
    return false;
}

/* ---------- shared widget plumbing ---------- */

static pt_widget *picker_field_create(pt_widget *host, pt_picker *picker,
                                      const char *placeholder) {
    pt_widget *field = pt_widget_new(host, pt_widget_entry);
    field->has_background = true;
    field->background = pt_color_transparent;
    field->has_border_width = true;
    field->border_width = 0.0f;
    field->padding_top = 0.0f;
    field->padding_bottom = 0.0f;
    field->padding_left = 0.0f;
    field->padding_right = 0.0f;
    field->radius = 0.0f;
    field->placeholder = pt_strdup(placeholder ? placeholder : "");
    pt_entry_set_on_change(field, picker_field_changed, host);
    picker->field = field;
    return field;
}

static void picker_set_field_text(pt_widget *host, pt_picker *picker, const char *text) {
    const char *value = text ? text : "";
    picker->applying = true;
    if (picker->field) {
        pt_widget_set_text(picker->field, value);
        /* Selecting the committed label means the next typed character starts a
         * fresh query instead of appending to the label it just committed. */
        pt_entry_select_all(picker->field);
    }
    pt_widget_set_text(host, value);
    picker->applying = false;
}

static pt_rect picker_anchor(const pt_widget *host) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(host, &x, &y, &w, &h);
    return (pt_rect){x, y, pt_max_float(1.0f, w), pt_max_float(1.0f, h)};
}

/* ---------- searchable select model ---------- */

static void select_option_clear(pt_picker_option *option) {
    free(option->text);
    memset(option, 0, sizeof(*option));
}

static void select_options_reserve(pt_picker *picker, uint32_t count) {
    if (count <= picker->option_capacity) {
        return;
    }
    uint32_t capacity = picker->option_capacity ? picker->option_capacity * 2u : 8u;
    while (capacity < count) {
        capacity *= 2u;
    }
    picker->options = pt_realloc_mem(picker->options,
                                     (size_t)capacity * sizeof(*picker->options));
    memset(picker->options + picker->option_capacity, 0,
           (size_t)(capacity - picker->option_capacity) * sizeof(*picker->options));
    picker->option_capacity = capacity;
}

static void select_visible_reserve(pt_picker *picker, uint32_t count) {
    if (count <= picker->visible_capacity) {
        return;
    }
    uint32_t capacity = picker->visible_capacity ? picker->visible_capacity * 2u : 16u;
    while (capacity < count) {
        capacity *= 2u;
    }
    picker->visible = pt_realloc_mem(picker->visible,
                                     (size_t)capacity * sizeof(*picker->visible));
    picker->visible_capacity = capacity;
}

static const char *select_item_text(const pt_picker *picker, uint32_t index) {
    if (!picker || index >= picker->option_count || !picker->options[index].text) {
        return "";
    }
    return picker->options[index].text;
}

static uint32_t select_visible_item(const pt_picker *picker, uint32_t position) {
    if (!picker || position >= picker->visible_count) {
        return pt_list_index_none;
    }
    return picker->visible[position];
}

static int select_position_of(const pt_picker *picker, uint32_t item) {
    if (!picker) {
        return -1;
    }
    for (uint32_t i = 0; i < picker->visible_count; ++i) {
        if (picker->visible[i] == item) {
            return (int)i;
        }
    }
    return -1;
}

/* The candidate set is truncated at the configured row limit, so the keyboard
 * highlight can never point at a row that was not built. */
static void select_rebuild_visible(pt_picker *picker) {
    if (!picker) {
        return;
    }
    select_visible_reserve(picker, picker->option_count);
    picker->visible_count = 0;
    const char *query = picker->query ? picker->query : "";
    for (uint32_t i = 0; i < picker->option_count; ++i) {
        if (!picker_contains(select_item_text(picker, i), query)) {
            continue;
        }
        picker->visible[picker->visible_count++] = i;
        if (picker->visible_count >= picker->max_rows) {
            break;
        }
    }
}

/* ---------- popup surface ---------- */

static pt_widget *picker_content_create(pt_picker *picker, pt_widget *host) {
    if (!picker || !host) {
        return NULL;
    }
    pt_widget *content = NULL;
    if (picker->kind == pt_picker_kind_select) {
        content = pt_scroll_create(NULL);
        content->direction = pt_direction_column;
        content->gap = picker_row_gap;
        content->align_items = pt_align_stretch;
        content->has_background = true;
        content->background = pt_color_transparent;
        content->has_border_width = true;
        content->border_width = 0.0f;
        content->radius = picker_row_radius;
        content->a11y_role = pt_a11y_role_list_box;
        pt_widget_set_a11y_name(content, "Options");
        picker->content = content;
    }
    return content;
}

static bool picker_ensure_popup(pt_widget *host) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker) {
        return false;
    }
    if (picker->popup) {
        return true;
    }
    pt_window *window = pt_widget_window(host);
    if (!window) {
        pt_set_error("picker: the control has no window to anchor a popup");
        return false;
    }
    pt_widget *content = picker_content_create(picker, host);
    if (!content) {
        return false;
    }
    pt_popup *popup = pt_popup_create(window, content);
    if (!popup) {
        return false;
    }
    picker->popup = popup;
    picker->window = window;
    picker->app = window->app;
    pt_popup_set_placement(popup, pt_popup_placement_below);
    pt_popup_set_on_dismiss(popup, picker_on_dismiss, picker);
    /* The owner routes Tab through the picker (see overlay.c) and is otherwise
     * unused by the application-popup paths. */
    popup->overlay.owner = host;
    return true;
}

static void picker_mark_rows(pt_widget *host, pt_picker *picker) {
    if (!picker || !picker->content) {
        return;
    }
    for (pt_widget *row = picker->content->first_child; row; row = row->next_sibling) {
        if (row->kind != pt_widget_list_row || row->popup_owner != host) {
            continue;
        }
        uint32_t item = select_visible_item(picker, row->list_index);
        row->row_selected = item != pt_list_index_none && item == picker->highlighted;
    }
    pt_widget_mark_dirty(picker->content, false);
    pt_widget_mark_dirty(host, false);
}

static void select_reveal_highlight(pt_widget *host, pt_picker *picker) {
    if (!picker || !picker->content || picker->highlighted == pt_list_index_none) {
        return;
    }
    int position = select_position_of(picker, picker->highlighted);
    if (position < 0) {
        return;
    }
    float step = picker_row_height + picker_row_gap;
    float top = (float)position * step;
    float viewport = pt_max_float(0.0f, picker->content->height);
    float offset = picker->content->scroll_y;
    if (top < offset) {
        offset = top;
    } else if (top + picker_row_height > offset + viewport) {
        offset = top + picker_row_height - viewport;
    }
    offset = pt_max_float(0.0f, offset);
    if (offset != picker->content->scroll_y) {
        picker->content->scroll_y = offset;
        pt_widget_mark_dirty(picker->content, false);
    }
    pt_widget_mark_dirty(host, false);
}

static pt_widget *select_row_create(pt_widget *host, pt_picker *picker, uint32_t position) {
    uint32_t item = select_visible_item(picker, position);
    if (item == pt_list_index_none) {
        return NULL;
    }
    const char *text = select_item_text(picker, item);
    pt_widget *row = pt_widget_new(NULL, pt_widget_list_row);
    row->text = pt_strdup(text);
    row->list_index = position;
    row->popup_owner = host;
    row->enabled = true;
    row->row_selected = item == picker->highlighted;
    row->row_current = item == picker->selected;
    row->radius = picker_row_radius;
    row->a11y_role = pt_a11y_role_list_item;
    row->a11y_name = pt_strdup(text);
    pt_widget_set_height(row, picker_row_height);
    pt_widget_set_padding(row, 4.0f, 12.0f, 4.0f, 12.0f);
    pt_widget_set_font_size(row, 13.0f);
    return row;
}

static pt_widget *select_empty_row_create(void) {
    pt_widget *row = pt_widget_new(NULL, pt_widget_list_row);
    row->text = pt_strdup("No matches");
    row->list_index = pt_list_index_none;
    row->enabled = false;
    row->radius = picker_row_radius;
    row->a11y_role = pt_a11y_role_list_item;
    row->a11y_name = pt_strdup("No matches");
    pt_widget_set_height(row, picker_row_height);
    pt_widget_set_padding(row, 4.0f, 12.0f, 4.0f, 12.0f);
    pt_widget_set_font_size(row, 13.0f);
    return row;
}

static void select_rebuild_popup(pt_widget *host, pt_picker *picker) {
    if (!picker || !picker->content) {
        return;
    }
    pt_widget_clear_children(picker->content);
    uint32_t rows = picker->visible_count;
    for (uint32_t i = 0; i < rows; ++i) {
        pt_widget *row = select_row_create(host, picker, i);
        if (row) {
            pt_widget_append(picker->content, row);
        }
    }
    if (rows == 0) {
        pt_widget_append(picker->content, select_empty_row_create());
        rows = 1;
    }
    float width = pt_max_float(host->width, picker_min_width);
    float content_height = (float)rows * (picker_row_height + picker_row_gap) - picker_row_gap;
    picker->content->requested_width =
        pt_max_float(80.0f, width - picker_popup_padding * 2.0f - picker_scroll_gutter);
    picker->content->requested_height = pt_min_float(content_height, picker_popup_max_height);
    select_reveal_highlight(host, picker);
}

/* ---------- popup lifecycle ---------- */

static void picker_restore_display(pt_widget *host, pt_picker *picker) {
    if (!picker) {
        return;
    }
    free(picker->query);
    picker->query = NULL;
    picker->highlighted = pt_list_index_none;
    if (picker->kind == pt_picker_kind_select) {
        picker_set_field_text(host, picker, select_item_text(picker, picker->selected));
    }
}

static void picker_on_dismiss(pt_popup *popup, void *user_data) {
    pt_unused(popup);
    pt_picker *picker = user_data;
    if (!picker) {
        return;
    }
    pt_widget *host = picker->host;
    picker_restore_display(host, picker);
    pt_widget_a11y_emit(host, pt_a11y_event_state_changed);
    pt_widget_mark_dirty(host, false);
    pt_window *window = pt_widget_window(host);
    if (window && !window->focused && picker->field) {
        pt_widget_focus(picker->field);
    }
}

static void picker_rebuild_popup(pt_widget *host, pt_picker *picker) {
    if (!picker) {
        return;
    }
    if (picker->kind == pt_picker_kind_select) {
        select_rebuild_visible(picker);
        select_rebuild_popup(host, picker);
    }
    pt_window *window = pt_widget_window(host);
    if (window) {
        pt_window_invalidate_layout(window, NULL);
    }
    if (picker->popup && pt_popup_is_open(picker->popup)) {
        pt_rect anchor = picker_anchor(host);
        pt_popup_reposition(picker->popup,
                            (pt_popup_anchor){anchor.x, anchor.y, anchor.w, anchor.h});
    }
    pt_widget_mark_dirty(host, false);
}

static void picker_popup_show(pt_widget *host, pt_picker *picker) {
    if (!picker || picker->kind == pt_picker_kind_number) {
        return;
    }
    if (picker->kind == pt_picker_kind_select && picker->option_count == 0) {
        return;
    }
    if (!picker_ensure_popup(host)) {
        return;
    }
    picker_rebuild_popup(host, picker);
    pt_rect anchor = picker_anchor(host);
    if (!pt_popup_show(picker->popup,
                       (pt_popup_anchor){anchor.x, anchor.y + anchor.h + picker_popup_gap,
                                         anchor.w, anchor.h})) {
        return;
    }
    /* pt_window_popup_show installs a NULL owner; the picker needs its own so
     * overlay.c can route Tab back to the control. */
    picker->popup->overlay.owner = host;
    pt_widget_a11y_emit(host, pt_a11y_event_state_changed);
    pt_widget_mark_dirty(host, false);
}

void pt_picker_open(pt_widget *host) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker || !host->enabled) {
        return;
    }
    if (picker->popup && pt_popup_is_open(picker->popup)) {
        return;
    }
    if (picker->kind == pt_picker_kind_select) {
        /* Opening without typing starts from the whole model, committed item
         * highlighted, exactly like the combo-box list. */
        picker->highlighted = picker->selected;
    }
    picker_popup_show(host, picker);
}

void pt_picker_close(pt_widget *host) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker || !picker->popup || !pt_popup_is_open(picker->popup)) {
        return;
    }
    pt_popup_dismiss(picker->popup);
}

bool pt_picker_is_open(const pt_widget *host) {
    pt_picker *picker = pt_picker_of(host);
    return picker && picker->popup && pt_popup_is_open(picker->popup);
}

bool pt_picker_popup_tab(pt_widget *host, bool shift) {
    pt_unused(shift);
    if (!pt_picker_is_open(host)) {
        return false;
    }
    pt_picker_close(host);
    return true;
}

/* ---------- selection and filtering ---------- */

static void select_set_query(pt_widget *host, pt_picker *picker, const char *text) {
    free(picker->query);
    picker->query = NULL;
    if (text && text[0]) {
        size_t length = strlen(text);
        if (length > picker_query_max) {
            length = picker_query_max;
        }
        picker->query = pt_strdup_len(text, length);
    }
    select_rebuild_visible(picker);
    if (picker->highlighted == pt_list_index_none ||
        select_position_of(picker, picker->highlighted) < 0) {
        picker->highlighted = picker->visible_count > 0 ? picker->visible[0]
                                                        : pt_list_index_none;
    }
    pt_unused(host);
}

static void picker_field_changed(pt_widget *field, const char *text, void *user_data) {
    pt_widget *host = user_data;
    pt_picker *picker = pt_picker_of(host);
    if (!picker || picker->applying) {
        return;
    }
    const char *value = text ? text : (field->text ? field->text : "");
    if (picker->kind != pt_picker_kind_select) {
        return;
    }
    select_set_query(host, picker, value);
    pt_window *window = pt_widget_window(host);
    if (window && !pt_picker_is_open(host)) {
        if (picker->option_count > 0) {
            picker_popup_show(host, picker);
            /* picker_popup_show rebuilt the model again; the query survives. */
        }
    } else if (pt_picker_is_open(host)) {
        picker_rebuild_popup(host, picker);
    }
    pt_widget_mark_dirty(host, false);
    pt_widget_a11y_emit(host, pt_a11y_event_text_changed);
}

static void select_highlight_at(pt_widget *host, pt_picker *picker, uint32_t item) {
    if (picker->highlighted == item) {
        return;
    }
    picker->highlighted = item;
    picker_mark_rows(host, picker);
    select_reveal_highlight(host, picker);
    pt_widget_a11y_emit(host, pt_a11y_event_state_changed);
}

static void select_move_highlight(pt_widget *host, pt_picker *picker, int direction) {
    if (picker->visible_count == 0) {
        return;
    }
    uint32_t count = picker->visible_count;
    int current = select_position_of(picker, picker->highlighted);
    uint32_t start;
    if (current < 0) {
        start = direction > 0 ? 0u : count - 1u;
    } else {
        start = (uint32_t)((current + direction + (int)count * 2) % (int)count);
    }
    select_highlight_at(host, picker, picker->visible[start]);
}

static void select_highlight_edge(pt_widget *host, pt_picker *picker, bool first) {
    if (picker->visible_count == 0) {
        return;
    }
    select_highlight_at(host, picker,
                        picker->visible[first ? 0u : picker->visible_count - 1u]);
}

/* Commits the visible position, then reports it. The callback runs last and may
 * destroy the picker, so the window is captured first and attachment checked
 * before the popup is closed. */
void pt_picker_choose(pt_widget *host, uint32_t position) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker) {
        return;
    }
    if (picker->kind != pt_picker_kind_select) {
        return;
    }
    uint32_t item = select_visible_item(picker, position);
    if (item == pt_list_index_none) {
        return;
    }
    pt_window *window = pt_widget_window(host);
    bool changed = picker->selected != item;
    picker->selected = item;
    picker->highlighted = item;
    free(picker->query);
    picker->query = NULL;
    picker_set_field_text(host, picker, select_item_text(picker, item));
    pt_widget_a11y_emit(host, pt_a11y_event_selection_changed);
    if (changed) {
        pt_widget_a11y_emit(host, pt_a11y_event_value_changed);
    }
    pt_widget_mark_dirty(host, false);
    if (changed && picker->on_select) {
        picker->on_select(host, item, picker->select_data);
    }
    if (pt_widget_attached(window, host)) {
        pt_picker_close(host);
    }
}

static void select_clear_selection(pt_widget *host, pt_picker *picker) {
    bool had = picker->selected != pt_list_index_none;
    picker->selected = pt_list_index_none;
    picker->highlighted = pt_list_index_none;
    free(picker->query);
    picker->query = NULL;
    picker_set_field_text(host, picker, "");
    if (had) {
        pt_widget_a11y_emit(host, pt_a11y_event_selection_changed);
        pt_widget_a11y_emit(host, pt_a11y_event_value_changed);
    }
    pt_widget_mark_dirty(host, false);
}

static void select_set_selected(pt_widget *host, pt_picker *picker, uint32_t index) {
    if (index >= picker->option_count) {
        picker->selected = pt_list_index_none;
        free(picker->query);
        picker->query = NULL;
        picker_set_field_text(host, picker, "");
        pt_widget_a11y_emit(host, pt_a11y_event_selection_changed);
        pt_widget_mark_dirty(host, false);
        return;
    }
    picker->selected = index;
    picker->highlighted = index;
    free(picker->query);
    picker->query = NULL;
    picker_set_field_text(host, picker, select_item_text(picker, index));
    pt_widget_a11y_emit(host, pt_a11y_event_selection_changed);
    pt_widget_mark_dirty(host, false);
}

/* ---------- keyboard ---------- */

static bool select_key(pt_widget *host, pt_picker *picker, uint32_t keysym,
                       uint32_t modifiers, const char *utf8) {
    pt_unused(utf8);
    bool open = picker->popup && pt_popup_is_open(picker->popup);
    bool alt = (modifiers & (uint32_t)pt_key_modifier_alt) != 0;

    if (keysym == XKB_KEY_Escape) {
        if (open) {
            pt_picker_close(host);
            return true;
        }
        if (picker->query && picker->query[0]) {
            picker_restore_display(host, picker);
            return true;
        }
        return false;
    }
    if (keysym == XKB_KEY_Tab || keysym == XKB_KEY_ISO_Left_Tab) {
        if (open) {
            pt_picker_close(host);
        }
        return false;
    }
    if (keysym == XKB_KEY_F4 || (alt && (keysym == XKB_KEY_Down || keysym == XKB_KEY_Up))) {
        if (open) {
            pt_picker_close(host);
        } else {
            pt_picker_open(host);
        }
        return true;
    }
    if (keysym == XKB_KEY_Down || keysym == XKB_KEY_Up) {
        int direction = keysym == XKB_KEY_Down ? 1 : -1;
        if (!open) {
            pt_picker_open(host);
            return pt_picker_is_open(host);
        }
        select_move_highlight(host, picker, direction);
        return true;
    }
    if (keysym == XKB_KEY_Page_Down || keysym == XKB_KEY_Page_Up) {
        if (!open) {
            return false;
        }
        int direction = keysym == XKB_KEY_Page_Down ? 1 : -1;
        for (int i = 0; i < 5; ++i) {
            select_move_highlight(host, picker, direction);
        }
        return true;
    }
    if (keysym == XKB_KEY_Home || keysym == XKB_KEY_End) {
        if (!open) {
            return false;
        }
        select_highlight_edge(host, picker, keysym == XKB_KEY_Home);
        return true;
    }
    if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
        if (!open) {
            pt_picker_open(host);
            return pt_picker_is_open(host);
        }
        uint32_t item = picker->highlighted;
        if (item == pt_list_index_none) {
            return true;
        }
        int position = select_position_of(picker, item);
        if (position < 0) {
            return true;
        }
        pt_picker_choose(host, (uint32_t)position);
        return true;
    }
    if (keysym == XKB_KEY_Delete && !open && (!picker->query || !picker->query[0]) &&
        picker->selected != pt_list_index_none) {
        select_clear_selection(host, picker);
        return true;
    }
    return false;
}

bool pt_picker_key(pt_widget *host, uint32_t keysym, uint32_t modifiers, const char *utf8) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker) {
        return false;
    }
    if (picker->kind == pt_picker_kind_select) {
        return select_key(host, picker, keysym, modifiers, utf8);
    }
    return false;
}

/* ---------- pointer, layout and measurement ---------- */

void pt_picker_pointer_down(pt_widget *host, float x, float y) {
    pt_unused(x);
    pt_unused(y);
    pt_picker *picker = pt_picker_of(host);
    if (!picker || !host->enabled) {
        return;
    }
    if (picker->field) {
        pt_widget_focus(picker->field);
    }
    if (picker->kind == pt_picker_kind_number) {
        return;
    }
    if (pt_picker_is_open(host)) {
        pt_picker_close(host);
    } else {
        pt_picker_open(host);
    }
}

void pt_picker_layout(pt_widget *host, const pt_layout_ctx *ctx, pt_rect inner) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker || !picker->field) {
        return;
    }
    float width = pt_max_float(0.0f, inner.w - pt_combobox_chevron_reserve);
    pt_widget_layout_child(picker->field, inner.x, inner.y, width, inner.h, ctx);
}

void pt_picker_preferred(pt_widget *host, const pt_layout_ctx *ctx, float *width,
                         float *height) {
    pt_picker *picker = pt_picker_of(host);
    float scale = ctx->scale > 0.0f ? ctx->scale : 1.0f;
    pt_font *font = pt_widget_font_for(host, ctx);
    float natural = 0.0f;
    const char *text = "";
    if (picker && picker->field) {
        text = pt_widget_get_text(picker->field);
    }
    if (font && text && text[0]) {
        natural = pt_font_measure(font, text, strlen(text)) / scale;
    } else if (font && host->placeholder && host->placeholder[0]) {
        natural = pt_font_measure(font, host->placeholder, strlen(host->placeholder)) / scale;
    }
    float padding_w = host->padding_left + host->padding_right;
    float padding_h = host->padding_top + host->padding_bottom;
    *width = pt_max_float(picker_min_width,
                          natural + padding_w + pt_combobox_chevron_reserve);
    *height = pt_widget_theme_metric_value(host, pt_theme_metric_control_height) + padding_h;
}

/* ---------- teardown ---------- */

void pt_picker_free(pt_widget *host) {
    pt_picker *picker = pt_picker_of(host);
    if (!picker) {
        return;
    }
    pt_combobox_state *state = host ? host->extra : NULL;
    if (picker->popup) {
        /* A window destroys its popups before its widget tree, so the handle is
         * only dereferenced while the owning window is still linked on the app.
         * unlink_window runs first in pt_window_free, which makes the scan a
         * safe liveness check without touching the freed window. */
        bool window_alive = false;
        if (picker->app) {
            for (pt_window *window = picker->app->first_window; window; window = window->next) {
                if (window == picker->window) {
                    window_alive = true;
                    break;
                }
            }
        }
        if (window_alive) {
            pt_popup_destroy(picker->popup);
        }
        picker->popup = NULL;
    }
    picker->content = NULL;
    if (state) {
        state->picker = NULL;
        state->field = NULL;
    }
    for (uint32_t i = 0; i < picker->option_count; ++i) {
        select_option_clear(&picker->options[i]);
    }
    free(picker->options);
    free(picker->visible);
    free(picker->query);
    free(picker);
}

/* ---------- public API: searchable select ---------- */

pt_widget *pt_select_create(pt_widget *parent) {
    pt_widget *host = pt_combobox_create(parent);
    if (!host) {
        return NULL;
    }
    pt_combobox_state *state = host->extra;
    pt_picker *picker = pt_alloc_zeroed(1, sizeof(*picker));
    picker->kind = pt_picker_kind_select;
    picker->host = host;
    picker->selected = pt_list_index_none;
    picker->highlighted = pt_list_index_none;
    picker->max_rows = picker_default_max_rows;
    state->picker = picker;
    state->mode = pt_combobox_filterable;
    host->focusable = false;
    picker_field_create(host, picker, "");
    state->field = picker->field;
    picker_set_field_text(host, picker, "");
    pt_widget_set_a11y_role(host, pt_a11y_role_combo_box);
    pt_widget_set_a11y_name(host, "Select");
    return host;
}

void pt_select_set_placeholder(pt_widget *select, const char *text) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    char *copy = pt_strdup(text ? text : "");
    free(select->placeholder);
    select->placeholder = copy;
    if (picker->field) {
        pt_entry_set_placeholder(picker->field, text);
    }
    pt_widget_mark_dirty(select, false);
}

const char *pt_select_get_placeholder(const pt_widget *select) {
    return select ? (select->placeholder ? select->placeholder : "") : "";
}

void pt_select_add_item(pt_widget *select, const char *text) {
    pt_select_insert_item(select, pt_list_index_none, text);
}

void pt_select_insert_item(pt_widget *select, uint32_t index, const char *text) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    if (picker->option_count >= PT_SELECT_MAX_ITEMS) {
        pt_set_error("pt_select: the item limit (%u) is reached", PT_SELECT_MAX_ITEMS);
        return;
    }
    if (index == pt_list_index_none || index > picker->option_count) {
        index = picker->option_count;
    }
    select_options_reserve(picker, picker->option_count + 1u);
    for (uint32_t i = picker->option_count; i > index; --i) {
        picker->options[i] = picker->options[i - 1u];
    }
    memset(&picker->options[index], 0, sizeof(picker->options[index]));
    picker->options[index].text = pt_strdup(text ? text : "");
    ++picker->option_count;
    if (picker->selected != pt_list_index_none && index <= picker->selected) {
        ++picker->selected;
    }
    if (picker->highlighted != pt_list_index_none && index <= picker->highlighted) {
        ++picker->highlighted;
    }
    if (pt_picker_is_open(select)) {
        picker_rebuild_popup(select, picker);
    }
    pt_widget_mark_dirty(select, false);
    pt_widget_a11y_emit(select, pt_a11y_event_structure_changed);
}

void pt_select_add_item_with_data(pt_widget *select, const char *text, void *data) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    pt_select_add_item(select, text);
    if (picker->option_count > 0 && picker->options[picker->option_count - 1u].text) {
        picker->options[picker->option_count - 1u].data = data;
    }
}

void pt_select_remove_item(pt_widget *select, uint32_t index) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker || index >= picker->option_count) {
        return;
    }
    select_option_clear(&picker->options[index]);
    for (uint32_t i = index + 1u; i < picker->option_count; ++i) {
        picker->options[i - 1u] = picker->options[i];
    }
    --picker->option_count;
    if (picker->selected == index) {
        picker->selected = pt_list_index_none;
        picker_set_field_text(select, picker, "");
    } else if (picker->selected != pt_list_index_none && picker->selected > index) {
        --picker->selected;
    }
    if (picker->highlighted != pt_list_index_none) {
        if (picker->highlighted == index) {
            picker->highlighted = pt_list_index_none;
        } else if (picker->highlighted > index) {
            --picker->highlighted;
        }
    }
    if (pt_picker_is_open(select)) {
        picker_rebuild_popup(select, picker);
    }
    pt_widget_mark_dirty(select, false);
    pt_widget_a11y_emit(select, pt_a11y_event_structure_changed);
}

void pt_select_clear(pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    for (uint32_t i = 0; i < picker->option_count; ++i) {
        select_option_clear(&picker->options[i]);
    }
    picker->option_count = 0;
    picker->visible_count = 0;
    picker->selected = pt_list_index_none;
    picker->highlighted = pt_list_index_none;
    picker_set_field_text(select, picker, "");
    pt_picker_close(select);
    pt_widget_mark_dirty(select, false);
    pt_widget_a11y_emit(select, pt_a11y_event_structure_changed);
}

uint32_t pt_select_get_item_count(const pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    return picker ? picker->option_count : 0u;
}

const char *pt_select_get_item_text(const pt_widget *select, uint32_t index) {
    return select_item_text(pt_picker_of_kind(select, pt_picker_kind_select), index);
}

void *pt_select_get_item_data(const pt_widget *select, uint32_t index) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    return picker && index < picker->option_count ? picker->options[index].data : NULL;
}

void pt_select_set_item_data(pt_widget *select, uint32_t index, void *data) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (picker && index < picker->option_count) {
        picker->options[index].data = data;
    }
}

uint32_t pt_select_get_selected(const pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    return picker ? picker->selected : pt_list_index_none;
}

void pt_select_set_selected(pt_widget *select, uint32_t index) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    select_set_selected(select, picker, index);
}

void pt_select_clear_selection(pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    select_clear_selection(select, picker);
}

const char *pt_select_get_query(const pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    return picker && picker->query ? picker->query : "";
}

void pt_select_set_query(pt_widget *select, const char *text) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    picker_set_field_text(select, picker, text);
    select_set_query(select, picker, text);
    picker_rebuild_popup(select, picker);
    pt_widget_a11y_emit(select, pt_a11y_event_text_changed);
}

uint32_t pt_select_get_match_count(const pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    return picker ? picker->visible_count : 0u;
}

uint32_t pt_select_get_match_item(const pt_widget *select, uint32_t position) {
    return select_visible_item(pt_picker_of_kind(select, pt_picker_kind_select), position);
}

void pt_select_set_max_popup_rows(pt_widget *select, uint32_t rows) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    if (rows < 1u) {
        rows = 1u;
    }
    if (rows > PT_PICKER_MAX_CANDIDATES) {
        rows = PT_PICKER_MAX_CANDIDATES;
    }
    picker->max_rows = rows;
    if (picker->selected != pt_list_index_none &&
        !(picker->query && picker->query[0])) {
        picker->highlighted = picker->selected;
    }
    if (pt_picker_is_open(select)) {
        picker_rebuild_popup(select, picker);
    }
    pt_widget_mark_dirty(select, false);
}

uint32_t pt_select_get_max_popup_rows(const pt_widget *select) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    return picker ? picker->max_rows : 0u;
}

void pt_select_open(pt_widget *select) {
    pt_picker_open(select);
}

void pt_select_close(pt_widget *select) {
    pt_picker_close(select);
}

bool pt_select_is_open(const pt_widget *select) {
    return pt_picker_is_open(select);
}

void pt_select_set_on_change(pt_widget *select, pt_index_callback callback, void *user_data) {
    pt_picker *picker = pt_picker_of_kind(select, pt_picker_kind_select);
    if (!picker) {
        return;
    }
    picker->on_select = callback;
    picker->select_data = user_data;
}

/* ---------- accessibility hooks ---------- */

bool pt_picker_a11y_value(const pt_widget *widget, float *value) {
    pt_picker *picker = pt_picker_of(widget);
    if (!picker || !value || picker->kind != pt_picker_kind_select) {
        return false;
    }
    *value = picker->selected == pt_list_index_none ? -1.0f : (float)picker->selected;
    return true;
}

bool pt_picker_a11y_has_popup(const pt_widget *widget) {
    pt_picker *picker = pt_picker_of(widget);
    return !picker || picker->kind != pt_picker_kind_number;
}