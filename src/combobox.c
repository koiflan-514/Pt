/*
 * Pt combo box: a field with a popup list of choices, select-only or filterable.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

static const float combobox_popup_gap = 4.0f;
static const float combobox_popup_padding = 6.0f;
static const float combobox_popup_radius = 14.0f;
static const float combobox_option_height = 30.0f;
static const float combobox_option_gap = 1.0f;
static const float combobox_option_radius = 8.0f;
static const float combobox_scroll_gutter = 12.0f;
static const float combobox_min_width = 160.0f;
static const float combobox_popup_edge = 8.0f;
static const float combobox_popup_default_max_height = 260.0f;
static const uint32_t combobox_typeahead_window_ms = 700u;

static pt_combobox_state *combobox_state(pt_widget *combobox) {
    if (!combobox || combobox->kind != pt_widget_combobox) {
        return NULL;
    }
    return combobox->extra;
}

static const pt_combobox_state *combobox_state_const(const pt_widget *combobox) {
    if (!combobox || combobox->kind != pt_widget_combobox) {
        return NULL;
    }
    return combobox->extra;
}

static bool popup_open(const pt_combobox_state *state) {
    return state && state->popup && state->popup->open;
}

/* Rebuilds an open popup after a model change, keeping the highlight when the
 * option is still there. Defined with the popup contents. */
static void refresh_popup(pt_widget *combobox);
static void sync_highlight(pt_widget *combobox);
static void set_display(pt_widget *combobox, const char *text);

static const char *item_text(const pt_combobox_state *state, uint32_t index) {
    if (!state || index >= state->item_count || !state->items[index].text) {
        return "";
    }
    return state->items[index].text;
}

static bool item_disabled(const pt_combobox_state *state, uint32_t index) {
    return state && index < state->item_count && state->items[index].disabled;
}

/* Matching is ASCII case-insensitive: enough for a Latin label list without
 * pulling a locale-aware fold into the widget layer. */
static char fold_ascii(char value) {
    return value >= 'A' && value <= 'Z' ? (char)(value + 32) : value;
}

static bool text_contains(const char *text, const char *needle) {
    if (!needle || needle[0] == '\0') {
        return true;
    }
    if (!text) {
        return false;
    }
    size_t needle_length = strlen(needle);
    for (const char *cursor = text; *cursor; ++cursor) {
        size_t step = 0;
        while (step < needle_length &&
               fold_ascii(cursor[step]) == fold_ascii(needle[step])) {
            ++step;
        }
        if (step == needle_length) {
            return true;
        }
    }
    return false;
}

static bool text_starts_with(const char *text, const char *prefix) {
    if (!text || !prefix || prefix[0] == '\0') {
        return false;
    }
    for (size_t i = 0;; ++i) {
        char right = fold_ascii(prefix[i]);
        if (right == '\0') {
            return true;
        }
        if (fold_ascii(text[i]) != right) {
            return false;
        }
    }
}

static void items_reserve(pt_combobox_state *state, uint32_t count) {
    if (count <= state->item_capacity) {
        return;
    }
    uint32_t capacity = state->item_capacity ? state->item_capacity * 2u : 8u;
    while (capacity < count) {
        capacity *= 2u;
    }
    state->items = pt_realloc_mem(state->items, (size_t)capacity * sizeof(*state->items));
    memset(state->items + state->item_capacity, 0,
           (size_t)(capacity - state->item_capacity) * sizeof(*state->items));
    state->item_capacity = capacity;
}

static void item_clear(pt_combobox_item *item) {
    free(item->text);
    memset(item, 0, sizeof(*item));
}

static void visible_reserve(pt_combobox_state *state, uint32_t count) {
    if (count <= state->visible_capacity) {
        return;
    }
    uint32_t capacity = state->visible_capacity ? state->visible_capacity * 2u : 16u;
    while (capacity < count) {
        capacity *= 2u;
    }
    state->visible = pt_realloc_mem(state->visible, (size_t)capacity * sizeof(*state->visible));
    state->visible_capacity = capacity;
}

/* The item index at a visible position, or pt_list_index_none. */
static uint32_t visible_item(const pt_combobox_state *state, uint32_t position) {
    if (!state || position >= state->visible_count) {
        return pt_list_index_none;
    }
    return state->visible[position];
}

static int position_of_item(const pt_combobox_state *state, uint32_t item) {
    if (!state) {
        return -1;
    }
    for (uint32_t i = 0; i < state->visible_count; ++i) {
        if (state->visible[i] == item) {
            return (int)i;
        }
    }
    return -1;
}

static void rebuild_visible(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    visible_reserve(state, state->item_count);
    state->visible_count = 0;
    const char *query = state->filter ? state->filter : "";
    for (uint32_t i = 0; i < state->item_count; ++i) {
        if (!text_contains(item_text(state, i), query)) {
            continue;
        }
        state->visible[state->visible_count++] = i;
    }
}

/* The nearest selectable item scanning from a visible position in `direction`,
 * wrapping once. Returns an item index, or pt_list_index_none when every
 * visible option is disabled. */
static uint32_t pick_option(const pt_combobox_state *state, uint32_t position,
                            int direction) {
    if (!state || state->visible_count == 0) {
        return pt_list_index_none;
    }
    uint32_t count = state->visible_count;
    if (position >= count) {
        position = count - 1u;
    }
    for (uint32_t step = 0; step < count; ++step) {
        long index = ((long)position + (long)direction * (long)step + (long)count * 2) %
                     (long)count;
        uint32_t item = state->visible[(uint32_t)index];
        if (!item_disabled(state, item)) {
            return item;
        }
    }
    return pt_list_index_none;
}

/* First or last selectable item of the full model, used by Home and End while
 * the popup is closed. */
static uint32_t item_edge(const pt_combobox_state *state, bool first) {
    if (!state || state->item_count == 0) {
        return pt_list_index_none;
    }
    if (first) {
        for (uint32_t i = 0; i < state->item_count; ++i) {
            if (!item_disabled(state, i)) {
                return i;
            }
        }
        return pt_list_index_none;
    }
    for (uint32_t i = state->item_count; i > 0; --i) {
        if (!item_disabled(state, i - 1u)) {
            return i - 1u;
        }
    }
    return pt_list_index_none;
}

/* The row scroll lives inside the panel built by build_panel, so it is read
 * back from the overlay instead of cached in the state. */
static pt_widget *popup_rows(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !state->popup || !state->popup->panel) {
        return NULL;
    }
    pt_widget *scroll = state->popup->panel->first_child;
    return scroll && scroll->kind == pt_widget_scroll ? scroll : NULL;
}

static void mark_options(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    pt_widget *scroll = popup_rows(combobox);
    if (!state || !scroll) {
        return;
    }
    for (pt_widget *row = scroll->first_child; row; row = row->next_sibling) {
        if (row->kind != pt_widget_list_row || !row->popup_owner) {
            continue;
        }
        uint32_t item = visible_item(state, row->list_index);
        row->row_selected = item != pt_list_index_none && item == state->highlighted;
    }
    pt_widget_mark_dirty(scroll, false);
    pt_widget_mark_dirty(combobox, false);
}

static void reveal_highlight(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    pt_widget *scroll = popup_rows(combobox);
    if (!state || !scroll || state->highlighted == pt_list_index_none) {
        return;
    }
    int position = position_of_item(state, state->highlighted);
    if (position < 0) {
        return;
    }
    float step = combobox_option_height + combobox_option_gap;
    float top = (float)position * step;
    float viewport = pt_max_float(0.0f, scroll->height);
    float offset = scroll->scroll_y;
    if (top < offset) {
        offset = top;
    } else if (top + combobox_option_height > offset + viewport) {
        offset = top + combobox_option_height - viewport;
    }
    offset = pt_max_float(0.0f, offset);
    if (offset != scroll->scroll_y) {
        scroll->scroll_y = offset;
        pt_widget_mark_dirty(scroll, false);
    }
}

static void set_highlight(pt_widget *combobox, uint32_t item) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || state->highlighted == item) {
        return;
    }
    state->highlighted = item;
    mark_options(combobox);
    reveal_highlight(combobox);
}

static void move_highlight(pt_widget *combobox, int direction) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || state->visible_count == 0) {
        return;
    }
    uint32_t count = state->visible_count;
    int current = position_of_item(state, state->highlighted);
    uint32_t start;
    if (current < 0) {
        start = direction > 0 ? 0u : count - 1u;
    } else {
        start = (uint32_t)((current + direction + (int)count * 2) % (int)count);
    }
    uint32_t item = pick_option(state, start, direction);
    if (item != pt_list_index_none) {
        set_highlight(combobox, item);
    }
}

static void highlight_edge(pt_widget *combobox, bool first) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || state->visible_count == 0) {
        return;
    }
    uint32_t item = pick_option(state, first ? 0u : state->visible_count - 1u,
                                first ? 1 : -1);
    if (item == pt_list_index_none) {
        item = first ? state->visible[0] : state->visible[state->visible_count - 1u];
    }
    set_highlight(combobox, item);
}

/* Page keys move by the number of options that fit in the popup, the same rule
 * the data views use. A popup that has not been laid out yet falls back to a
 * fixed step so the keys stay usable. */
static int page_step(pt_widget *combobox) {
    pt_widget *scroll = popup_rows(combobox);
    float step = combobox_option_height + combobox_option_gap;
    if (!scroll || scroll->height <= step) {
        return 5;
    }
    int rows = (int)(scroll->height / step);
    return rows > 1 ? rows : 1;
}

static pt_widget *option_create(pt_widget *combobox, uint32_t position) {
    pt_combobox_state *state = combobox_state(combobox);
    uint32_t item = visible_item(state, position);
    if (item == pt_list_index_none) {
        return NULL;
    }
    pt_widget *row = pt_widget_new(NULL, pt_widget_list_row);
    const char *text = item_text(state, item);
    row->text = pt_strdup(text);
    row->list_index = position;
    row->popup_owner = combobox;
    row->enabled = !item_disabled(state, item);
    row->row_selected = item == state->highlighted;
    row->row_current = item == state->selected;
    row->radius = combobox_option_radius;
    row->a11y_role = pt_a11y_role_list_item;
    row->a11y_name = pt_strdup(text);
    pt_widget_set_height(row, combobox_option_height);
    pt_widget_set_padding(row, 4.0f, 12.0f, 4.0f, 12.0f);
    pt_widget_set_font_size(row, 13.0f);
    return row;
}

static pt_widget *build_panel(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return NULL;
    }
    pt_widget *panel = pt_card_create(NULL);
    panel->window = pt_widget_window(combobox);
    panel->direction = pt_direction_column;
    panel->align_items = pt_align_stretch;
    panel->gap = 0.0f;
    panel->padding_top = combobox_popup_padding;
    panel->padding_bottom = combobox_popup_padding;
    panel->padding_left = combobox_popup_padding;
    panel->padding_right = combobox_popup_padding;
    panel->radius = combobox_popup_radius;
    panel->a11y_role = pt_a11y_role_list_box;
    panel->a11y_name = pt_strdup(state->field && state->field->placeholder
                                     ? state->field->placeholder
                                     : combobox->placeholder);

    float width = pt_max_float(combobox->width, combobox_min_width);
    float rows_width = pt_max_float(60.0f, width - combobox_popup_padding * 2.0f -
                                                combobox_scroll_gutter);
    float content = (float)state->visible_count * (combobox_option_height +
                                                   combobox_option_gap) -
                    combobox_option_gap;
    if (content < 0.0f) {
        content = 0.0f;
    }
    pt_widget *scroll = pt_scroll_create(panel);
    scroll->direction = pt_direction_column;
    scroll->gap = combobox_option_gap;
    scroll->align_items = pt_align_stretch;
    scroll->has_background = true;
    scroll->background = pt_color_transparent;
    scroll->has_border_width = true;
    scroll->border_width = 0.0f;
    scroll->requested_width = rows_width;
    scroll->requested_height = pt_min_float(content, state->popup_max_height);
    scroll->a11y_role = pt_a11y_role_list_box;

    for (uint32_t i = 0; i < state->visible_count; ++i) {
        pt_widget *row = option_create(combobox, i);
        if (row) {
            pt_widget_append(scroll, row);
        }
    }
    return panel;
}

void pt_combobox_popup_layout(pt_widget *combobox, pt_overlay *overlay,
                              const pt_layout_ctx *ctx) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !overlay->panel) {
        return;
    }
    float field_width = pt_max_float(combobox->width, combobox_min_width);
    float width = 0.0f;
    float height = 0.0f;
    pt_widget_measure(overlay->panel, ctx, field_width, 900.0f, &width, &height);
    width = pt_max_float(field_width, width);
    height = pt_max_float(1.0f, height);

    pt_window *window = overlay->window;
    float x = combobox->x;
    float y = combobox->y + combobox->height + combobox_popup_gap;
    if (!overlay->native && window) {
        float limit_x = (float)window->width - combobox_popup_edge;
        float bottom = (float)window->height - combobox_popup_edge;
        float top_limit = window->header_height + combobox_popup_edge;
        if (y + height > bottom) {
            float above = combobox->y - combobox_popup_gap - height;
            y = above >= top_limit ? above
                                   : pt_max_float(top_limit, bottom - height);
        }
        if (x + width > limit_x) {
            x = pt_max_float(combobox_popup_edge, limit_x - width);
        }
        if (x < combobox_popup_edge) {
            x = combobox_popup_edge;
        }
    }
    overlay->rect = (pt_rect){x, y, width, height};
    overlay->anchor_x = x;
    overlay->anchor_y = y;
}

/* Opening keeps the current filter when asked, so a keystroke that reveals the
 * popup does not erase what the user typed. */
static void show_popup(pt_widget *combobox, bool reset_filter) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !combobox->enabled || state->item_count == 0) {
        return;
    }
    pt_window *window = pt_widget_window(combobox);
    if (!window) {
        return;
    }
    if (reset_filter) {
        free(state->filter);
        state->filter = NULL;
    }
    rebuild_visible(combobox);
    if (!popup_open(state)) {
        if (position_of_item(state, state->selected) >= 0 &&
            !item_disabled(state, state->selected)) {
            state->highlighted = state->selected;
        } else {
            state->highlighted = pick_option(state, 0u, 1);
        }
        state->popup->kind = pt_overlay_kind_combobox;
        state->popup->owner = combobox;
        state->popup->passive = true;
        pt_window_popup_show(state->popup, window, combobox, build_panel(combobox));
    } else {
        sync_highlight(combobox);
        if (state->popup->panel) {
            mark_options(combobox);
        }
        reveal_highlight(combobox);
        pt_window_invalidate_layout(window, NULL);
    }
    reveal_highlight(combobox);
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_state_changed);
}

void pt_combobox_open_popup(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (popup_open(state)) {
        return;
    }
    show_popup(combobox, true);
}

void pt_combobox_close_popup(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!popup_open(state)) {
        return;
    }
    pt_window_popup_hide(state->popup);
    state->highlighted = pt_list_index_none;
    /* Closing without committing is a cancel: the field goes back to the
     * committed value and the filter is dropped, the way Escape behaves in a
     * QComboBox edit. */
    set_display(combobox, item_text(state, state->selected));
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_state_changed);
}

/* The popup stays open when the press lands on the field itself, which is how
 * the user re-positions the caret while filtering. */
bool pt_combobox_field_hit(pt_widget *combobox, float x, float y) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || state->mode != pt_combobox_filterable || !state->field) {
        return false;
    }
    return pt_rect_contains((pt_rect){state->field->x, state->field->y,
                                      state->field->width, state->field->height},
                            x, y);
}

pt_widget *pt_combobox_field_widget(const pt_widget *combobox) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    return state ? state->field : NULL;
}

static void set_display(pt_widget *combobox, const char *text) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    free(state->filter);
    state->filter = NULL;
    if (state->mode == pt_combobox_filterable && state->field) {
        state->applying = true;
        pt_widget_set_text(state->field, text ? text : "");
        state->applying = false;
    }
    pt_widget_set_text(combobox, text ? text : "");
}

static void commit_highlight(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (state && state->highlighted != pt_list_index_none) {
        pt_combobox_select(combobox, state->highlighted, true);
    }
}

void pt_combobox_choose_option(pt_widget *combobox, uint32_t position) {
    pt_combobox_state *state = combobox_state(combobox);
    uint32_t item = visible_item(state, position);
    if (!state || item == pt_list_index_none || item_disabled(state, item)) {
        return;
    }
    pt_combobox_select(combobox, item, true);
    pt_combobox_close_popup(combobox);
}

/* ---------- public API ---------- */

pt_widget *pt_combobox_create(pt_widget *parent) {
    pt_widget *combobox = pt_widget_new(parent, pt_widget_combobox);
    pt_combobox_state *state = pt_alloc_zeroed(1, sizeof(*state));
    state->selected = pt_list_index_none;
    state->highlighted = pt_list_index_none;
    state->popup_max_height = combobox_popup_default_max_height;
    state->popup = pt_alloc_zeroed(1, sizeof(*state->popup));
    state->popup->kind = pt_overlay_kind_combobox;
    state->popup->owner = combobox;
    state->popup->passive = true;
    combobox->extra = state;
    combobox->text = pt_strdup("");
    combobox->placeholder = pt_strdup("");
    return combobox;
}

void pt_combobox_add_item(pt_widget *combobox, const char *text) {
    pt_combobox_insert_item(combobox, pt_list_index_none, text);
}

void pt_combobox_insert_item(pt_widget *combobox, uint32_t index, const char *text) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    if (index == pt_list_index_none || index > state->item_count) {
        index = state->item_count;
    }
    items_reserve(state, state->item_count + 1u);
    for (uint32_t i = state->item_count; i > index; --i) {
        state->items[i] = state->items[i - 1u];
    }
    memset(&state->items[index], 0, sizeof(state->items[index]));
    state->items[index].text = pt_strdup(text ? text : "");
    ++state->item_count;
    if (state->selected != pt_list_index_none && index <= state->selected) {
        ++state->selected;
    }
    /* The highlight is an item index too, so it shifts with the rest of the
     * model instead of sliding onto the inserted row. */
    if (state->highlighted != pt_list_index_none && index <= state->highlighted) {
        ++state->highlighted;
    }
    refresh_popup(combobox);
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_structure_changed);
}

void pt_combobox_remove_item(pt_widget *combobox, uint32_t index) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || index >= state->item_count) {
        return;
    }
    item_clear(&state->items[index]);
    for (uint32_t i = index + 1u; i < state->item_count; ++i) {
        state->items[i - 1u] = state->items[i];
    }
    --state->item_count;
    if (state->selected == index) {
        state->selected = pt_list_index_none;
        set_display(combobox, "");
    } else if (state->selected != pt_list_index_none && state->selected > index) {
        --state->selected;
    }
    if (state->highlighted == index) {
        state->highlighted = pt_list_index_none;
    } else if (state->highlighted != pt_list_index_none && state->highlighted > index) {
        --state->highlighted;
    }
    refresh_popup(combobox);
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_structure_changed);
}

void pt_combobox_clear(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    for (uint32_t i = 0; i < state->item_count; ++i) {
        item_clear(&state->items[i]);
    }
    state->item_count = 0;
    state->visible_count = 0;
    state->selected = pt_list_index_none;
    state->highlighted = pt_list_index_none;
    set_display(combobox, "");
    pt_combobox_close_popup(combobox);
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_structure_changed);
}

uint32_t pt_combobox_get_item_count(const pt_widget *combobox) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    return state ? state->item_count : 0;
}

const char *pt_combobox_get_item_text(const pt_widget *combobox, uint32_t index) {
    return item_text(combobox_state_const(combobox), index);
}

void pt_combobox_set_item_text(pt_widget *combobox, uint32_t index, const char *text) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || index >= state->item_count) {
        return;
    }
    free(state->items[index].text);
    state->items[index].text = pt_strdup(text ? text : "");
    if (index == state->selected && !popup_open(state)) {
        set_display(combobox, state->items[index].text);
    }
    refresh_popup(combobox);
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_text_changed);
}

void pt_combobox_set_item_data(pt_widget *combobox, uint32_t index, void *data) {
    pt_combobox_state *state = combobox_state(combobox);
    if (state && index < state->item_count) {
        state->items[index].data = data;
    }
}

void *pt_combobox_get_item_data(const pt_widget *combobox, uint32_t index) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    return state && index < state->item_count ? state->items[index].data : NULL;
}

void pt_combobox_set_item_disabled(pt_widget *combobox, uint32_t index, bool disabled) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || index >= state->item_count || state->items[index].disabled == disabled) {
        return;
    }
    state->items[index].disabled = disabled;
    if (disabled && state->selected == index) {
        state->selected = pt_list_index_none;
        set_display(combobox, "");
    }
    if (disabled && state->highlighted == index) {
        state->highlighted = pt_list_index_none;
    }
    refresh_popup(combobox);
    pt_widget_mark_dirty(combobox, false);
    pt_widget_a11y_emit(combobox, pt_a11y_event_state_changed);
}

bool pt_combobox_get_item_disabled(const pt_widget *combobox, uint32_t index) {
    return item_disabled(combobox_state_const(combobox), index);
}

static void field_changed(pt_widget *field, const char *text, void *user_data);

void pt_combobox_set_mode(pt_widget *combobox, pt_combobox_mode mode) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || state->mode == mode) {
        return;
    }
    pt_combobox_close_popup(combobox);
    state->mode = mode;
    if (mode == pt_combobox_filterable) {
        if (!state->field) {
            pt_widget *field = pt_widget_new(combobox, pt_widget_entry);
            field->has_background = true;
            field->background = pt_color_transparent;
            field->has_border_width = true;
            field->border_width = 0.0f;
            field->padding_top = 0.0f;
            field->padding_bottom = 0.0f;
            field->padding_left = 0.0f;
            field->padding_right = 0.0f;
            field->radius = 0.0f;
            field->placeholder = pt_strdup(combobox->placeholder
                                                ? combobox->placeholder
                                                : "");
            pt_widget_set_text(field, item_text(state, state->selected));
            pt_entry_set_on_change(field, field_changed, combobox);
            state->field = field;
        }
        /* The editor takes over the keyboard, so the box stops being a focus
         * target. Focus moves with it instead of landing on the next widget. */
        pt_window *window = pt_widget_window(combobox);
        combobox->focusable = false;
        if (window && window->focused == combobox) {
            pt_widget_focus(state->field);
        }
    } else {
        combobox->focusable = true;
        if (state->field) {
            pt_widget *field = state->field;
            state->field = NULL;
            pt_window *window = pt_widget_window(combobox);
            if (window && window->focused == field) {
                pt_widget_focus(combobox);
            }
            pt_widget_destroy(field);
        }
    }
    set_display(combobox, state->selected != pt_list_index_none
                              ? item_text(state, state->selected)
                              : "");
    pt_widget_mark_dirty(combobox, true);
}

pt_combobox_mode pt_combobox_get_mode(const pt_widget *combobox) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    return state ? state->mode : pt_combobox_select_only;
}

uint32_t pt_combobox_get_selected(const pt_widget *combobox) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    return state ? state->selected : pt_list_index_none;
}

void pt_combobox_select(pt_widget *combobox, uint32_t index, bool notify) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || index >= state->item_count || state->items[index].disabled) {
        return;
    }
    bool changed = state->selected != index;
    state->selected = index;
    state->highlighted = index;
    set_display(combobox, item_text(state, index));
    pt_widget_a11y_emit(combobox, pt_a11y_event_selection_changed);
    /* The callback reports a choice, not an attempt: re-selecting the value
     * that is already in the field stays silent, as it does in GtkComboBox and
     * QComboBox. */
    if (notify && changed && state->on_change) {
        state->on_change(combobox, index, state->change_data);
    }
    if (changed) {
        pt_widget_a11y_emit(combobox, pt_a11y_event_value_changed);
    }
    if (popup_open(state)) {
        mark_options(combobox);
    }
}

void pt_combobox_set_selected(pt_widget *combobox, uint32_t index) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    if (index >= state->item_count) {
        state->selected = pt_list_index_none;
        state->highlighted = pt_list_index_none;
        set_display(combobox, "");
        pt_widget_mark_dirty(combobox, false);
        pt_widget_a11y_emit(combobox, pt_a11y_event_selection_changed);
        return;
    }
    pt_combobox_select(combobox, index, false);
}

const char *pt_combobox_get_text(const pt_widget *combobox) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    if (!state) {
        return "";
    }
    if (state->mode == pt_combobox_filterable && state->field) {
        return state->field->text ? state->field->text : "";
    }
    return combobox->text ? combobox->text : "";
}

void pt_combobox_set_text(pt_widget *combobox, const char *text) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    const char *wanted = text ? text : "";
    if (state->mode == pt_combobox_filterable) {
        state->applying = true;
        if (state->field) {
            pt_widget_set_text(state->field, wanted);
        }
        state->applying = false;
        free(state->filter);
        state->filter = pt_strdup(wanted);
        pt_widget_set_text(combobox, wanted);
        pt_widget_mark_dirty(combobox, false);
        return;
    }
    uint32_t match = pt_list_index_none;
    for (uint32_t i = 0; i < state->item_count; ++i) {
        if (strcmp(item_text(state, i), wanted) == 0) {
            match = i;
            break;
        }
    }
    if (match == pt_list_index_none) {
        state->selected = pt_list_index_none;
        set_display(combobox, "");
        pt_widget_mark_dirty(combobox, false);
        pt_widget_a11y_emit(combobox, pt_a11y_event_selection_changed);
        return;
    }
    pt_combobox_select(combobox, match, false);
}

const char *pt_combobox_get_placeholder(const pt_widget *combobox) {
    return combobox && combobox->placeholder ? combobox->placeholder : "";
}

void pt_combobox_set_placeholder(pt_widget *combobox, const char *text) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!combobox) {
        return;
    }
    char *copy = pt_strdup(text ? text : "");
    free(combobox->placeholder);
    combobox->placeholder = copy;
    if (state && state->field) {
        pt_entry_set_placeholder(state->field, text);
    }
    pt_widget_mark_dirty(combobox, false);
}

void pt_combobox_open(pt_widget *combobox) {
    pt_combobox_open_popup(combobox);
}

void pt_combobox_close(pt_widget *combobox) {
    pt_combobox_close_popup(combobox);
}

bool pt_combobox_is_open(const pt_widget *combobox) {
    return popup_open(combobox_state_const(combobox));
}

void pt_combobox_set_popup_max_height(pt_widget *combobox, float height) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    state->popup_max_height = pt_max_float(combobox_option_height, height);
    if (popup_open(state)) {
        pt_window_popup_replace_panel(state->popup, build_panel(combobox));
        pt_window_invalidate_layout(pt_widget_window(combobox), NULL);
    }
    pt_widget_mark_dirty(combobox, false);
}

float pt_combobox_get_popup_max_height(const pt_widget *combobox) {
    const pt_combobox_state *state = combobox_state_const(combobox);
    return state ? state->popup_max_height : 0.0f;
}

void pt_combobox_set_on_change(pt_widget *combobox, pt_index_callback callback,
                               void *user_data) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    state->on_change = callback;
    state->change_data = user_data;
}

void pt_combobox_set_on_text_change(pt_widget *combobox,
                                    pt_combobox_text_callback callback, void *user_data) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    state->on_text_change = callback;
    state->text_change_data = user_data;
}

/* ---------- popup contents ---------- */

/* Rebuilding an open popup keeps the highlight when the option is still there
 * and falls back to the committed value, then to the first selectable option. */
static void sync_highlight(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || (state->highlighted != pt_list_index_none &&
                   position_of_item(state, state->highlighted) >= 0)) {
        return;
    }
    if (state->selected != pt_list_index_none && !item_disabled(state, state->selected) &&
        position_of_item(state, state->selected) >= 0) {
        state->highlighted = state->selected;
        return;
    }
    state->highlighted = pick_option(state, 0u, 1);
}

/* Rebuilds an open popup after a model change, keeping the highlight when the
 * option is still there. */
static void refresh_popup(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!popup_open(state)) {
        return;
    }
    rebuild_visible(combobox);
    sync_highlight(combobox);
    pt_window_popup_replace_panel(state->popup, build_panel(combobox));
    reveal_highlight(combobox);
    pt_window_invalidate_layout(pt_widget_window(combobox), NULL);
}

/* ---------- interaction ---------- */

static void typeahead_reset(pt_widget *combobox) {
    combobox->typeahead_length = 0;
    combobox->typeahead[0] = '\0';
    combobox->typeahead_at = 0;
}

/* Prefix matching over the option labels, restarting the buffer after a pause
 * so "no", "nor", "north" arrive as one word and a second "n" starts fresh.
 * With the popup closed this commits the match; with it open it only moves the
 * highlight, which is how the ARIA select-only pattern behaves. */
static bool combobox_typeahead(pt_widget *combobox, const char *utf8) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !utf8 || !utf8[0] || state->item_count == 0) {
        return false;
    }
    int64_t now = pt_now_ms();
    if (combobox->typeahead_at == 0 ||
        (uint64_t)(now - combobox->typeahead_at) > combobox_typeahead_window_ms) {
        typeahead_reset(combobox);
    }
    size_t length = strlen(utf8);
    if (combobox->typeahead_length + length >= sizeof(combobox->typeahead)) {
        typeahead_reset(combobox);
    }
    memcpy(combobox->typeahead + combobox->typeahead_length, utf8, length);
    combobox->typeahead_length += length;
    combobox->typeahead[combobox->typeahead_length] = '\0';
    combobox->typeahead_at = now;

    bool open = popup_open(state);
    uint32_t count = open ? state->visible_count : state->item_count;
    if (count == 0) {
        return false;
    }
    uint32_t current = open ? state->highlighted : state->selected;
    uint32_t start = 0;
    if (current != pt_list_index_none) {
        int position = open ? position_of_item(state, current) : (int)current;
        if (position >= 0) {
            start = (uint32_t)((position + 1) % (int)count);
        }
    }
    for (uint32_t step = 0; step < count; ++step) {
        uint32_t position = (start + step) % count;
        uint32_t item = open ? visible_item(state, position) : position;
        if (item == pt_list_index_none || item_disabled(state, item)) {
            continue;
        }
        if (text_starts_with(item_text(state, item), combobox->typeahead)) {
            if (open) {
                set_highlight(combobox, item);
            } else {
                pt_combobox_select(combobox, item, true);
            }
            return true;
        }
    }
    return false;
}

static void field_changed(pt_widget *field, const char *text, void *user_data) {
    pt_widget *combobox = user_data;
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || state->applying) {
        return;
    }
    const char *value = text ? text : (field->text ? field->text : "");
    free(state->filter);
    state->filter = pt_strdup(value);
    free(combobox->text);
    combobox->text = pt_strdup(value);
    combobox->typeahead_at = 0;
    if (state->on_text_change) {
        state->on_text_change(combobox, value, state->text_change_data);
    }
    if (!popup_open(state)) {
        show_popup(combobox, false);
    } else {
        rebuild_visible(combobox);
        sync_highlight(combobox);
        pt_window_popup_replace_panel(state->popup, build_panel(combobox));
        reveal_highlight(combobox);
        pt_window_invalidate_layout(pt_widget_window(combobox), NULL);
    }
    pt_widget_mark_dirty(combobox, false);
}

bool pt_combobox_key(pt_widget *combobox, uint32_t keysym, uint32_t modifiers,
                     const char *utf8) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !combobox->enabled) {
        return false;
    }
    bool open = popup_open(state);
    bool filterable = state->mode == pt_combobox_filterable;
    bool alt = (modifiers & (uint32_t)pt_key_modifier_alt) != 0;

    if (keysym == XKB_KEY_Escape) {
        if (!open) {
            return false;
        }
        pt_combobox_close_popup(combobox);
        return true;
    }
    if (keysym == XKB_KEY_Tab || keysym == XKB_KEY_ISO_Left_Tab) {
        if (open) {
            pt_combobox_close_popup(combobox);
        }
        return false;
    }
    if (!open && (keysym == XKB_KEY_F4 ||
                  (alt && (keysym == XKB_KEY_Down || keysym == XKB_KEY_Up)))) {
        show_popup(combobox, true);
        return popup_open(state);
    }
    if (keysym == XKB_KEY_Down || keysym == XKB_KEY_Up) {
        int direction = keysym == XKB_KEY_Down ? 1 : -1;
        if (!open) {
            show_popup(combobox, filterable);
            return popup_open(state);
        }
        move_highlight(combobox, direction);
        return true;
    }
    if (keysym == XKB_KEY_Page_Down || keysym == XKB_KEY_Page_Up) {
        if (!open) {
            return false;
        }
        int direction = keysym == XKB_KEY_Page_Down ? 1 : -1;
        int step = page_step(combobox);
        for (int i = 0; i < step; ++i) {
            move_highlight(combobox, direction);
        }
        return true;
    }
    if (keysym == XKB_KEY_Home || keysym == XKB_KEY_End) {
        bool first = keysym == XKB_KEY_Home;
        if (open) {
            highlight_edge(combobox, first);
            return true;
        }
        /* A filterable box leaves Home and End to the editor. */
        if (filterable) {
            return false;
        }
        uint32_t item = item_edge(state, first);
        if (item == pt_list_index_none) {
            return false;
        }
        pt_combobox_select(combobox, item, true);
        return true;
    }
    if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
        if (open) {
            commit_highlight(combobox);
            pt_combobox_close_popup(combobox);
            return true;
        }
        if (filterable) {
            return false;
        }
        show_popup(combobox, true);
        return popup_open(state);
    }
    if (keysym == XKB_KEY_space) {
        /* A space belongs to the editor in a filterable box. */
        if (filterable) {
            return false;
        }
        if (open) {
            commit_highlight(combobox);
            pt_combobox_close_popup(combobox);
        } else {
            show_popup(combobox, true);
        }
        return true;
    }
    if (!filterable && utf8 && utf8[0] && utf8[0] != ' ' &&
        !(modifiers & ((uint32_t)pt_key_modifier_ctrl | (uint32_t)pt_key_modifier_alt |
                       (uint32_t)pt_key_modifier_logo))) {
        return combobox_typeahead(combobox, utf8);
    }
    return false;
}

void pt_combobox_pointer_down(pt_widget *combobox, float x, float y) {
    pt_unused(x);
    pt_unused(y);
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !combobox->enabled) {
        return;
    }
    if (popup_open(state)) {
        /* Reaching the field itself keeps the popup open; that click only
         * moves the caret, which the entry handles. */
        if (!state->field || !pt_rect_contains((pt_rect){state->field->x, state->field->y,
                                                         state->field->width,
                                                         state->field->height},
                                               x, y)) {
            pt_combobox_close_popup(combobox);
        }
        return;
    }
    if (state->mode == pt_combobox_filterable && state->field) {
        pt_widget_focus(state->field);
    }
    show_popup(combobox, true);
}

/* ---------- layout, measurement and teardown ---------- */

void pt_combobox_preferred(pt_widget *combobox, const pt_layout_ctx *ctx, float *width,
                           float *height) {
    float scale = ctx->scale > 0.0f ? ctx->scale : 1.0f;
    pt_font *font = pt_widget_font_for(combobox, ctx);
    float natural = 0.0f;
    const char *text = pt_combobox_get_text(combobox);
    if (font && text && text[0]) {
        natural = pt_font_measure(font, text, strlen(text)) / scale;
    } else if (font && combobox->placeholder && combobox->placeholder[0]) {
        natural = pt_font_measure(font, combobox->placeholder,
                                 strlen(combobox->placeholder)) / scale;
    }
    float padding_w = combobox->padding_left + combobox->padding_right;
    float padding_h = combobox->padding_top + combobox->padding_bottom;
    *width = pt_max_float(combobox_min_width,
                         natural + padding_w + pt_combobox_chevron_reserve);
    *height = pt_widget_theme_metric_value(combobox, pt_theme_metric_control_height) +
              padding_h;
}

void pt_combobox_layout(pt_widget *combobox, const pt_layout_ctx *ctx, pt_rect inner) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state || !state->field) {
        return;
    }
    float text_width = pt_max_float(0.0f, inner.w - pt_combobox_chevron_reserve);
    pt_widget_layout_child(state->field, inner.x, inner.y, text_width, inner.h, ctx);
}

void pt_combobox_free_extra(pt_widget *combobox) {
    pt_combobox_state *state = combobox_state(combobox);
    if (!state) {
        return;
    }
    if (popup_open(state)) {
        state->popup->owner = NULL;
        pt_combobox_close_popup(combobox);
    }
    if (state->popup) {
        state->popup->owner = NULL;
        free(state->popup);
        state->popup = NULL;
    }
    for (uint32_t i = 0; i < state->item_count; ++i) {
        item_clear(&state->items[i]);
    }
    free(state->items);
    free(state->visible);
    free(state->filter);
    combobox->extra = NULL;
    free(state);
}
