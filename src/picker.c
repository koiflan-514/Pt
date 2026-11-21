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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
static void pt_date_prev_click(pt_widget *button, void *user_data);
static void pt_date_next_click(pt_widget *button, void *user_data);
static void pt_date_today_click(pt_widget *button, void *user_data);

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

/* ---------- civil date arithmetic ---------- */

#define picker_date_max_year 9999

static const char *const picker_month_names[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December",
};

static const char *const picker_weekday_names[7] = {"Mo", "Tu", "We", "Th",
                                                   "Fr", "Sa", "Su"};

static bool date_is_leap(int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint32_t date_month_length(int32_t year, uint32_t month) {
    static const uint8_t lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1u || month > 12u) {
        return 0u;
    }
    return month == 2u && date_is_leap(year) ? 29u : lengths[month - 1u];
}

static bool date_equal(pt_date a, pt_date b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

pt_date pt_date_today(void) {
    time_t now = time(NULL);
    struct tm local;
    if (now == (time_t)-1 || !localtime_r(&now, &local)) {
        return (pt_date){0, 0, 0};
    }
    int32_t year = local.tm_year + 1900;
    uint32_t month = (uint32_t)local.tm_mon + 1u;
    uint32_t day = (uint32_t)local.tm_mday;
    if (year < 1 || year > picker_date_max_year) {
        return (pt_date){0, 0, 0};
    }
    return (pt_date){year, (uint8_t)month, (uint8_t)day};
}

bool pt_date_is_valid(pt_date date) {
    if (date.year < 1 || date.year > picker_date_max_year || date.month < 1u ||
        date.month > 12u || date.day < 1u) {
        return false;
    }
    return (uint32_t)date.day <= date_month_length(date.year, date.month);
}

/* Howard Hinnant's civil calendar algorithms; the epoch itself is irrelevant,
 * only the difference in days matters and no time zone is involved. */
static int64_t date_days_from_civil(int32_t year, uint32_t month, uint32_t day) {
    int32_t y = year - (month <= 2u);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - (int32_t)era * 400);
    uint32_t doy = (153u * (month + (month > 2u ? (uint32_t)-3 : 9u)) + 2u) / 5u + day - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static pt_date date_from_days(int64_t days) {
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int64_t year = (int64_t)yoe + era * 400;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t day = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t month = mp + (mp < 10u ? 3u : (uint32_t)-9);
    year += month <= 2u;
    if (year < 1) {
        year = 1;
    }
    if (year > picker_date_max_year) {
        year = picker_date_max_year;
    }
    return (pt_date){(int32_t)year, (uint8_t)month, (uint8_t)day};
}

static pt_date date_clamp(pt_date date) {
    if (date.year < 1) {
        return (pt_date){1, 1, 1};
    }
    if (date.year > picker_date_max_year) {
        return (pt_date){picker_date_max_year, 12, 31};
    }
    if (date.month < 1u) {
        date.month = 1;
    }
    if (date.month > 12u) {
        date.month = 12;
    }
    uint32_t length = date_month_length(date.year, date.month);
    if (date.day < 1u) {
        date.day = 1;
    }
    if ((uint32_t)date.day > length) {
        date.day = (uint8_t)length;
    }
    return date;
}

/* 0 = Monday, matching ISO 8601 and the calendar header. */
static int date_weekday(pt_date date) {
    int64_t days = date_days_from_civil(date.year, date.month, date.day);
    int sunday = (int)((days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6));
    return (sunday + 6) % 7;
}

static pt_date date_add_days(pt_date date, int days) {
    int64_t value = date_days_from_civil(date.year, date.month, date.day) + days;
    return date_clamp(date_from_days(value));
}

static pt_date date_add_months(pt_date date, int months) {
    int32_t total = date.year * 12 + (int32_t)date.month - 1 + months;
    int32_t year = total / 12;
    int32_t month = total % 12;
    if (month < 0) {
        month += 12;
        --year;
    }
    if (year < 1) {
        year = 1;
        month = 0;
    }
    if (year > picker_date_max_year) {
        year = picker_date_max_year;
        month = 11;
    }
    pt_date shifted = {(int32_t)year, (uint8_t)(month + 1), date.day};
    return date_clamp(shifted);
}

static pt_date date_first_of_month(int32_t year, uint32_t month) {
    return (pt_date){year, (uint8_t)month, 1};
}

/* The Monday on or before the first of the viewed month. */
static pt_date date_grid_start(int32_t year, uint32_t month) {
    pt_date first = date_first_of_month(year, month);
    return date_add_days(first, -date_weekday(first));
}

/* ---------- date text ---------- */

static void date_format_into(char *buffer, size_t size, pt_date date,
                             pt_date_format format) {
    switch (format) {
    case pt_date_format_dmy:
        snprintf(buffer, size, "%02u/%02u/%04d", (unsigned)date.day,
                 (unsigned)date.month, date.year);
        break;
    case pt_date_format_mdy:
        snprintf(buffer, size, "%02u/%02u/%04d", (unsigned)date.month,
                 (unsigned)date.day, date.year);
        break;
    case pt_date_format_iso:
    default:
        snprintf(buffer, size, "%04d-%02u-%02u", date.year, (unsigned)date.month,
                 (unsigned)date.day);
        break;
    }
}

static bool date_tail_ok(const char *text, int consumed) {
    if (consumed <= 0) {
        return false;
    }
    for (const char *cursor = text + consumed; *cursor; ++cursor) {
        if (!isspace((unsigned char)*cursor)) {
            return false;
        }
    }
    return true;
}

static bool date_parse_with(const char *text, pt_date_format format, pt_date *out) {
    if (!text || !text[0] || !out) {
        return false;
    }
    int a = 0;
    int b = 0;
    int c = 0;
    int consumed = 0;
    char extra = '\0';
    int fields = 0;
    if (format == pt_date_format_dmy) {
        fields = sscanf(text, "%d/%d/%d%n%c", &a, &b, &c, &consumed, &extra);
        if (fields < 3) {
            fields = sscanf(text, "%d-%d-%d%n%c", &a, &b, &c, &consumed, &extra);
        }
        pt_date date = {(int32_t)c, (uint8_t)b, (uint8_t)a};
        if (fields >= 3 && date_tail_ok(text, consumed) && pt_date_is_valid(date)) {
            *out = date;
            return true;
        }
        return false;
    }
    if (format == pt_date_format_mdy) {
        fields = sscanf(text, "%d/%d/%d%n%c", &a, &b, &c, &consumed, &extra);
        if (fields < 3) {
            fields = sscanf(text, "%d-%d-%d%n%c", &a, &b, &c, &consumed, &extra);
        }
        pt_date date = {(int32_t)c, (uint8_t)a, (uint8_t)b};
        if (fields >= 3 && date_tail_ok(text, consumed) && pt_date_is_valid(date)) {
            *out = date;
            return true;
        }
        return false;
    }
    fields = sscanf(text, "%d-%d-%d%n%c", &a, &b, &c, &consumed, &extra);
    if (fields < 3) {
        fields = sscanf(text, "%d/%d/%d%n%c", &a, &b, &c, &consumed, &extra);
    }
    pt_date date = {(int32_t)a, (uint8_t)b, (uint8_t)c};
    if (fields >= 3 && date_tail_ok(text, consumed) && pt_date_is_valid(date)) {
        *out = date;
        return true;
    }
    return false;
}

static bool date_parse(const char *text, pt_date_format format, pt_date *out) {
    if (date_parse_with(text, format, out)) {
        return true;
    }
    /* ISO is always accepted, so a pasted value works in every picture. */
    if (format != pt_date_format_iso) {
        return date_parse_with(text, pt_date_format_iso, out);
    }
    return false;
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
    } else if (picker->kind == pt_picker_kind_date) {
        content = pt_column_create(NULL);
        content->gap = 4.0f;
        content->align_items = pt_align_stretch;
        content->a11y_role = pt_a11y_role_group;
        pt_widget_set_a11y_name(content, "Calendar");
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

/* ---------- date picker internals ---------- */

static void date_refresh_text(pt_widget *host, pt_picker *picker) {
    if (picker->date.year == 0) {
        picker->display[0] = '\0';
    } else {
        date_format_into(picker->display, sizeof(picker->display), picker->date,
                         picker->format);
    }
    picker_set_field_text(host, picker, picker->display);
}

static void date_sync_view(pt_picker *picker) {
    picker->view_year = picker->focus_date.year;
    picker->view_month = picker->focus_date.month;
}

static void date_commit(pt_widget *host, pt_picker *picker, pt_date date, bool notify) {
    if (!pt_date_is_valid(date)) {
        return;
    }
    bool changed = !date_equal(picker->date, date);
    picker->date = date;
    picker->focus_date = date;
    date_sync_view(picker);
    date_refresh_text(host, picker);
    pt_widget_a11y_emit(host, pt_a11y_event_selection_changed);
    if (changed) {
        pt_widget_a11y_emit(host, pt_a11y_event_value_changed);
    }
    pt_widget_mark_dirty(host, false);
    if (changed && notify && picker->on_date) {
        picker->on_date(host, date, picker->date_data);
    }
}

static void date_clear_value(pt_widget *host, pt_picker *picker) {
    bool had = picker->date.year != 0;
    picker->date = (pt_date){0, 0, 0};
    picker->display[0] = '\0';
    picker_set_field_text(host, picker, "");
    if (had) {
        pt_widget_a11y_emit(host, pt_a11y_event_selection_changed);
        pt_widget_a11y_emit(host, pt_a11y_event_value_changed);
    }
    pt_widget_mark_dirty(host, false);
}

/* Rebuilds the whole calendar for the viewed month. Six weeks are always
 * materialized so a month change cannot grow the tree without bound. */
static void date_rebuild_popup(pt_widget *host, pt_picker *picker) {
    if (!picker || !picker->content) {
        return;
    }
    pt_widget_clear_children(picker->content);
    pt_date first = date_first_of_month(picker->view_year, picker->view_month);
    pt_date grid_start = date_grid_start(picker->view_year, picker->view_month);

    pt_widget *header = pt_row_create(picker->content);
    header->align_items = pt_align_center;
    header->gap = 4.0f;
    pt_widget_set_height(header, 30.0f);

    pt_widget *previous = pt_button_create(header, "");
    previous->button_variant = pt_button_ghost;
    previous->focusable = false;
    pt_widget_set_icon(previous, pt_icon_chevron_left);
    pt_widget_set_size(previous, 26.0f, 26.0f);
    pt_widget_set_padding(previous, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_a11y_name(previous, "Previous month");
    pt_button_set_on_click(previous, pt_date_prev_click, host);

    char title[64];
    snprintf(title, sizeof(title), "%s %d", picker_month_names[picker->view_month - 1u],
             (int)picker->view_year);
    pt_widget *label = pt_label_create(header, title);
    label->grow = 1.0f;
    pt_widget_set_text_align(label, pt_text_align_center);
    pt_widget_set_font_size(label, 13.0f);
    pt_widget_set_font_weight(label, pt_font_weight_medium);

    pt_widget *next = pt_button_create(header, "");
    next->button_variant = pt_button_ghost;
    next->focusable = false;
    pt_widget_set_icon(next, pt_icon_chevron_right);
    pt_widget_set_size(next, 26.0f, 26.0f);
    pt_widget_set_padding(next, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_a11y_name(next, "Next month");
    pt_button_set_on_click(next, pt_date_next_click, host);

    pt_widget *today = pt_button_create(header, "Today");
    today->button_variant = pt_button_ghost;
    today->focusable = false;
    pt_widget_set_height(today, 26.0f);
    pt_widget_set_padding(today, 4.0f, 8.0f, 4.0f, 8.0f);
    pt_widget_set_font_size(today, 12.0f);
    pt_button_set_on_click(today, pt_date_today_click, host);

    pt_widget *weekday = pt_row_create(picker->content);
    weekday->align_items = pt_align_center;
    weekday->gap = 2.0f;
    pt_widget_set_height(weekday, 18.0f);
    for (uint32_t i = 0; i < 7u; ++i) {
        pt_widget *day = pt_label_create(weekday, picker_weekday_names[i]);
        day->grow = 1.0f;
        pt_widget_set_text_align(day, pt_text_align_center);
        pt_widget_set_font_size(day, 11.0f);
        day->a11y_role = pt_a11y_role_label;
    }

    pt_widget *grid = pt_grid_create(picker->content, 7u);
    grid->gap = 2.0f;
    grid->row_gap = 2.0f;
    pt_grid_set_gap(grid, 2.0f, 2.0f);
    grid->a11y_role = pt_a11y_role_table;
    pt_widget_set_a11y_name(grid, "Calendar days");
    for (uint32_t i = 0; i < 42u; ++i) {
        pt_date day = date_add_days(grid_start, (int)i);
        bool in_month = day.year == picker->view_year && day.month == picker->view_month;
        pt_widget *cell = pt_widget_new(NULL, pt_widget_list_row);
        char number[8];
        snprintf(number, sizeof(number), "%u", (unsigned)day.day);
        cell->text = pt_strdup(in_month ? number : "");
        cell->list_index = i;
        cell->popup_owner = host;
        cell->enabled = in_month;
        cell->row_selected = in_month && date_equal(day, picker->focus_date);
        cell->row_current = in_month && date_equal(day, picker->date);
        cell->radius = 6.0f;
        cell->a11y_role = pt_a11y_role_table_cell;
        char name[24];
        snprintf(name, sizeof(name), "%04d-%02u-%02u", day.year, (unsigned)day.month,
                 (unsigned)day.day);
        cell->a11y_name = pt_strdup(name);
        pt_widget_set_height(cell, 28.0f);
        pt_widget_set_padding(cell, 0.0f, 0.0f, 0.0f, 0.0f);
        pt_widget_set_text_align(cell, pt_text_align_center);
        pt_widget_set_font_size(cell, 12.0f);
        pt_widget_append(grid, cell);
    }
    (void)first;

    picker->content->requested_width = 7.0f * 32.0f + 6.0f * 2.0f;
    picker->content->requested_height = 234.0f;
}

static void date_focus_set(pt_widget *host, pt_picker *picker, pt_date date) {
    if (!pt_date_is_valid(date)) {
        return;
    }
    picker->focus_date = date;
    date_sync_view(picker);
    date_rebuild_popup(host, picker);
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
    pt_widget_a11y_emit(host, pt_a11y_event_state_changed);
}

static void date_view_shift(pt_widget *host, pt_picker *picker, int months) {
    pt_date focus = picker->focus_date;
    if (focus.year == 0) {
        focus = pt_date_today();
        if (!pt_date_is_valid(focus)) {
            focus = (pt_date){1970, 1, 1};
        }
    }
    date_focus_set(host, picker, date_add_months(focus, months));
}

static void pt_date_prev_click(pt_widget *button, void *user_data) {
    pt_unused(button);
    pt_widget *host = user_data;
    pt_picker *picker = pt_picker_of_kind(host, pt_picker_kind_date);
    if (picker) {
        date_view_shift(host, picker, -1);
    }
}

static void pt_date_next_click(pt_widget *button, void *user_data) {
    pt_unused(button);
    pt_widget *host = user_data;
    pt_picker *picker = pt_picker_of_kind(host, pt_picker_kind_date);
    if (picker) {
        date_view_shift(host, picker, 1);
    }
}

static void pt_date_today_click(pt_widget *button, void *user_data) {
    pt_unused(button);
    pt_widget *host = user_data;
    pt_picker *picker = pt_picker_of_kind(host, pt_picker_kind_date);
    if (!picker) {
        return;
    }
    pt_date today = pt_date_today();
    if (!pt_date_is_valid(today)) {
        return;
    }
    pt_window *window = pt_widget_window(host);
    date_commit(host, picker, today, true);
    if (pt_widget_attached(window, host)) {
        pt_picker_close(host);
    }
}

static bool date_commit_text(pt_widget *host, pt_picker *picker, const char *text,
                             bool notify) {
    const char *value = text ? text : "";
    if (value[0] == '\0') {
        bool had = picker->date.year != 0;
        date_clear_value(host, picker);
        if (had && notify && picker->on_date) {
            picker->on_date(host, (pt_date){0, 0, 0}, picker->date_data);
        }
        return true;
    }
    pt_date parsed = {0, 0, 0};
    if (!date_parse(value, picker->format, &parsed)) {
        pt_set_error("pt_date_picker: '%s' is not a valid date", value);
        picker_set_field_text(host, picker, picker->display);
        return false;
    }
    date_commit(host, picker, parsed, notify);
    return true;
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
    } else if (picker->kind == pt_picker_kind_date) {
        picker_set_field_text(host, picker, picker->display);
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
    } else if (picker->kind == pt_picker_kind_date) {
        date_rebuild_popup(host, picker);
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
    } else if (picker->kind == pt_picker_kind_date) {
        pt_date focus = pt_date_is_valid(picker->date) ? picker->date : pt_date_today();
        if (!pt_date_is_valid(focus)) {
            focus = (pt_date){1970, 1, 1};
        }
        picker->focus_date = focus;
        date_sync_view(picker);
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
    if (picker->kind == pt_picker_kind_date) {
        if (position >= 42u || picker->view_month < 1u || picker->view_month > 12u) {
            return;
        }
        pt_date start = date_grid_start(picker->view_year, picker->view_month);
        pt_date day = date_add_days(start, (int)position);
        if (day.year != picker->view_year || day.month != picker->view_month ||
            !pt_date_is_valid(day)) {
            return;
        }
        pt_window *window = pt_widget_window(host);
        date_commit(host, picker, day, true);
        if (pt_widget_attached(window, host)) {
            pt_picker_close(host);
        }
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

static bool date_key(pt_widget *host, pt_picker *picker, uint32_t keysym,
                     uint32_t modifiers, const char *utf8) {
    bool open = picker->popup && pt_popup_is_open(picker->popup);
    bool alt = (modifiers & (uint32_t)pt_key_modifier_alt) != 0;
    const char *field_text = picker->field ? pt_widget_get_text(picker->field) : "";
    bool dirty = strcmp(field_text, picker->display) != 0;

    if (keysym == XKB_KEY_Escape) {
        if (open) {
            pt_picker_close(host);
            return true;
        }
        if (dirty) {
            picker_set_field_text(host, picker, picker->display);
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
    if (!open) {
        if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
            if (dirty) {
                date_commit_text(host, picker, field_text, true);
                return true;
            }
            pt_picker_open(host);
            return pt_picker_is_open(host);
        }
        return false;
    }

    pt_date focus = picker->focus_date;
    if (focus.year == 0) {
        focus = pt_date_today();
    }
    if (keysym == XKB_KEY_Left) {
        date_focus_set(host, picker, date_add_days(focus, -1));
        return true;
    }
    if (keysym == XKB_KEY_Right) {
        date_focus_set(host, picker, date_add_days(focus, 1));
        return true;
    }
    if (keysym == XKB_KEY_Up) {
        date_focus_set(host, picker, date_add_days(focus, -7));
        return true;
    }
    if (keysym == XKB_KEY_Down) {
        date_focus_set(host, picker, date_add_days(focus, 7));
        return true;
    }
    if (keysym == XKB_KEY_Page_Up || keysym == XKB_KEY_Page_Down) {
        date_focus_set(host, picker,
                       date_add_months(focus, keysym == XKB_KEY_Page_Up ? -1 : 1));
        return true;
    }
    if (keysym == XKB_KEY_Home || keysym == XKB_KEY_End) {
        pt_date edge = focus;
        uint32_t length = date_month_length(edge.year, edge.month);
        edge.day = keysym == XKB_KEY_Home ? 1 : (uint8_t)length;
        date_focus_set(host, picker, edge);
        return true;
    }
    if (utf8 && (utf8[0] == 't' || utf8[0] == 'T') && !alt &&
        !(modifiers & ((uint32_t)pt_key_modifier_ctrl | (uint32_t)pt_key_modifier_logo))) {
        pt_date today = pt_date_today();
        if (!pt_date_is_valid(today)) {
            return false;
        }
        pt_window *window = pt_widget_window(host);
        date_commit(host, picker, today, true);
        if (pt_widget_attached(window, host)) {
            pt_picker_close(host);
        }
        return true;
    }
    if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
        pt_window *window = pt_widget_window(host);
        if (dirty) {
            date_commit_text(host, picker, field_text, true);
        } else {
            date_commit(host, picker, focus, true);
        }
        if (pt_widget_attached(window, host)) {
            pt_picker_close(host);
        }
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
    if (picker->kind == pt_picker_kind_date) {
        return date_key(host, picker, keysym, modifiers, utf8);
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

/* ---------- public API: date picker ---------- */

pt_widget *pt_date_picker_create(pt_widget *parent) {
    pt_widget *host = pt_combobox_create(parent);
    if (!host) {
        return NULL;
    }
    pt_combobox_state *state = host->extra;
    pt_picker *picker = pt_alloc_zeroed(1, sizeof(*picker));
    picker->kind = pt_picker_kind_date;
    picker->host = host;
    picker->format = pt_date_format_iso;
    state->picker = picker;
    state->mode = pt_combobox_filterable;
    host->focusable = false;
    free(host->placeholder);
    host->placeholder = pt_strdup("Date");
    picker_field_create(host, picker, "Date");
    state->field = picker->field;
    picker_set_field_text(host, picker, "");
    pt_widget_set_a11y_role(host, pt_a11y_role_combo_box);
    pt_widget_set_a11y_name(host, "Date");
    return host;
}

bool pt_date_picker_set_date(pt_widget *picker_widget, pt_date date) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    if (!picker) {
        return false;
    }
    if (date.year == 0) {
        date_clear_value(picker_widget, picker);
        return true;
    }
    if (!pt_date_is_valid(date)) {
        pt_set_error("pt_date_picker: invalid date %d-%u-%u", (int)date.year,
                     (unsigned)date.month, (unsigned)date.day);
        return false;
    }
    date_commit(picker_widget, picker, date, false);
    return true;
}

pt_date pt_date_picker_get_date(const pt_widget *picker_widget) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    return picker ? picker->date : (pt_date){0, 0, 0};
}

void pt_date_picker_clear(pt_widget *picker_widget) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    if (picker) {
        date_clear_value(picker_widget, picker);
    }
}

void pt_date_picker_set_format(pt_widget *picker_widget, pt_date_format format) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    if (!picker) {
        return;
    }
    if (format != pt_date_format_iso && format != pt_date_format_dmy &&
        format != pt_date_format_mdy) {
        format = pt_date_format_iso;
    }
    if (picker->format == format) {
        return;
    }
    picker->format = format;
    date_refresh_text(picker_widget, picker);
    if (pt_picker_is_open(picker_widget)) {
        pt_widget_mark_dirty(picker_widget, false);
    }
    pt_widget_a11y_emit(picker_widget, pt_a11y_event_text_changed);
}

pt_date_format pt_date_picker_get_format(const pt_widget *picker_widget) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    return picker ? picker->format : pt_date_format_iso;
}

bool pt_date_picker_set_text(pt_widget *picker_widget, const char *text) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    if (!picker) {
        return false;
    }
    if (!text || !text[0]) {
        date_clear_value(picker_widget, picker);
        return true;
    }
    pt_date parsed = {0, 0, 0};
    if (!date_parse(text, picker->format, &parsed)) {
        pt_set_error("pt_date_picker: '%s' is not a valid date", text);
        picker_set_field_text(picker_widget, picker, picker->display);
        return false;
    }
    date_commit(picker_widget, picker, parsed, false);
    return true;
}

const char *pt_date_picker_get_text(const pt_widget *picker_widget) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    return picker ? picker->display : "";
}

void pt_date_picker_set_placeholder(pt_widget *picker_widget, const char *text) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    if (!picker) {
        return;
    }
    char *copy = pt_strdup(text ? text : "");
    free(picker_widget->placeholder);
    picker_widget->placeholder = copy;
    if (picker->field) {
        pt_entry_set_placeholder(picker->field, text);
    }
    pt_widget_mark_dirty(picker_widget, false);
}

const char *pt_date_picker_get_placeholder(const pt_widget *picker_widget) {
    return picker_widget
               ? (picker_widget->placeholder ? picker_widget->placeholder : "")
               : "";
}

void pt_date_picker_open(pt_widget *picker_widget) {
    pt_picker_open(picker_widget);
}

void pt_date_picker_close(pt_widget *picker_widget) {
    pt_picker_close(picker_widget);
}

bool pt_date_picker_is_open(const pt_widget *picker_widget) {
    return pt_picker_is_open(picker_widget);
}

void pt_date_picker_set_on_change(pt_widget *picker_widget, pt_date_callback callback,
                                  void *user_data) {
    pt_picker *picker = pt_picker_of_kind(picker_widget, pt_picker_kind_date);
    if (!picker) {
        return;
    }
    picker->on_date = callback;
    picker->date_data = user_data;
}

/* ---------- accessibility hooks ---------- */

bool pt_picker_a11y_value(const pt_widget *widget, float *value) {
    pt_picker *picker = pt_picker_of(widget);
    if (!picker || !value) {
        return false;
    }
    if (picker->kind == pt_picker_kind_select) {
        *value = picker->selected == pt_list_index_none ? -1.0f : (float)picker->selected;
        return true;
    }
    if (picker->kind == pt_picker_kind_date) {
        if (picker->date.year == 0) {
            *value = 0.0f;
        } else {
            *value = (float)(picker->date.year * 10000 + (int32_t)picker->date.month * 100 +
                             (int32_t)picker->date.day);
        }
        return true;
    }
    return false;
}

bool pt_picker_a11y_has_popup(const pt_widget *widget) {
    pt_picker *picker = pt_picker_of(widget);
    return !picker || picker->kind != pt_picker_kind_number;
}