/*
 * Pt navigation shell: icons, tabs and sidebar.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define pt_tabs_gap 6.0f
#define pt_tabs_margin 4.0f
#define pt_tabs_arrow_width 30.0f

static pt_tabs_state *tabs_state(pt_widget *tabs) {
    if (!tabs || tabs->kind != pt_widget_tabs) {
        return NULL;
    }
    return tabs->extra;
}

static const pt_tabs_state *tabs_state_const(const pt_widget *tabs) {
    if (!tabs || tabs->kind != pt_widget_tabs) {
        return NULL;
    }
    return tabs->extra;
}

static pt_sidebar_state *sidebar_state(pt_widget *sidebar) {
    if (!sidebar || sidebar->kind != pt_widget_sidebar) {
        return NULL;
    }
    return sidebar->extra;
}

static const pt_sidebar_state *sidebar_state_const(const pt_widget *sidebar) {
    if (!sidebar || sidebar->kind != pt_widget_sidebar) {
        return NULL;
    }
    return sidebar->extra;
}

/* ---------- icons ---------- */

pt_widget *pt_icon_create(pt_widget *parent, pt_icon icon) {
    pt_widget *widget = pt_widget_new(parent, pt_widget_icon);
    widget->icon = icon;
    widget->icon_size = 20.0f;
    return widget;
}

void pt_widget_set_icon(pt_widget *widget, pt_icon icon) {
    if (!widget) {
        return;
    }
    widget->icon = icon;
    pt_widget_mark_dirty(widget, widget->kind == pt_widget_icon);
}

pt_icon pt_widget_get_icon(const pt_widget *widget) {
    return widget ? widget->icon : pt_icon_none;
}

void pt_icon_set_size(pt_widget *icon, float size) {
    if (!icon || icon->kind != pt_widget_icon) {
        return;
    }
    icon->icon_size = pt_max_float(8.0f, size);
    pt_widget_mark_dirty(icon, true);
}

float pt_icon_get_size(const pt_widget *icon) {
    if (!icon || icon->kind != pt_widget_icon) {
        return 0.0f;
    }
    return icon->icon_size;
}

pt_widget *pt_icon_button_create(pt_widget *parent, pt_icon icon,
                                 const char *accessible_name) {
    pt_widget *button = pt_button_create(parent, NULL);
    button->icon = icon;
    button->button_variant = pt_button_secondary;
    button->requested_width = 36.0f;
    button->requested_height = 36.0f;
    button->padding_left = 8.0f;
    button->padding_right = 8.0f;
    const char *name = accessible_name ? accessible_name : "";
    pt_widget_set_a11y_name(button, name);
    if (name[0] != '\0') {
        pt_widget_set_tooltip(button, name);
    }
    return button;
}

/* ---------- tabs ---------- */

static pt_widget *tabs_page_at_raw(const pt_widget *tabs, uint32_t index) {
    pt_widget *page = tabs ? tabs->first_child : NULL;
    while (page) {
        if (page->kind == pt_widget_tab_page) {
            if (index == 0) {
                return page;
            }
            --index;
        }
        page = page->next_sibling;
    }
    return NULL;
}

static uint32_t tabs_actual_pages(const pt_widget *tabs) {
    uint32_t count = 0;
    for (pt_widget *page = tabs ? tabs->first_child : NULL; page;
         page = page->next_sibling) {
        if (page->kind == pt_widget_tab_page) {
            ++count;
        }
    }
    return count;
}

pt_widget *pt_tabs_page_at(const pt_widget *tabs, uint32_t index) {
    return tabs_page_at_raw(tabs, index);
}

uint32_t pt_tabs_page_count(const pt_widget *tabs) {
    const pt_tabs_state *state = tabs_state_const(tabs);
    return state ? state->count : 0;
}

static void tabs_apply_visibility(pt_widget *tabs) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    uint32_t index = 0;
    for (pt_widget *page = tabs->first_child; page; page = page->next_sibling) {
        if (page->kind != pt_widget_tab_page) {
            continue;
        }
        page->visible = state->count > 0 && index == state->active;
        ++index;
    }
}

static void tabs_ensure_capacity(pt_tabs_state *state, uint32_t count) {
    if (count <= state->capacity) {
        return;
    }
    uint32_t capacity = state->capacity ? state->capacity : 4;
    while (capacity < count) {
        capacity *= 2u;
    }
    state->titles = pt_realloc_mem(state->titles, (size_t)capacity * sizeof(*state->titles));
    for (uint32_t i = state->capacity; i < capacity; ++i) {
        state->titles[i] = NULL;
    }
    state->capacity = capacity;
}

static void tabs_ensure_tab_capacity(pt_tabs_state *state, uint32_t count) {
    if (count <= state->tab_capacity) {
        return;
    }
    uint32_t capacity = state->tab_capacity ? state->tab_capacity : 8;
    while (capacity < count) {
        capacity *= 2u;
    }
    state->tab_x = pt_realloc_mem(state->tab_x, (size_t)capacity * sizeof(*state->tab_x));
    state->tab_width =
        pt_realloc_mem(state->tab_width, (size_t)capacity * sizeof(*state->tab_width));
    state->tab_capacity = capacity;
}

pt_widget *pt_tabs_create(pt_widget *parent) {
    pt_widget *tabs = pt_widget_new(parent, pt_widget_tabs);
    pt_tabs_state *state = pt_alloc_zeroed(1, sizeof(*state));
    tabs->extra = state;
    state->strip_height = 44.0f;
    return tabs;
}

pt_widget *pt_tabs_add_page(pt_widget *tabs, const char *title) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return NULL;
    }
    tabs_ensure_capacity(state, state->count + 1);
    pt_widget *page = pt_widget_new(tabs, pt_widget_tab_page);
    state->titles[state->count] = pt_strdup(title ? title : "");
    ++state->count;
    if (state->count == 1) {
        state->active = 0;
        page->visible = true;
    } else {
        page->visible = false;
    }
    state->indicator_ready = false;
    pt_tabs_sync_pages(tabs);
    pt_widget_mark_dirty(tabs, true);
    pt_widget_a11y_emit(tabs, pt_a11y_event_structure_changed);
    return page;
}

void pt_tabs_forget_page(pt_widget *tabs, pt_widget *page) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state || !page) {
        return;
    }
    uint32_t index = 0;
    bool found = false;
    for (pt_widget *child = tabs->first_child; child; child = child->next_sibling) {
        if (child->kind != pt_widget_tab_page) {
            continue;
        }
        if (child == page) {
            found = true;
            break;
        }
        ++index;
    }
    if (!found) {
        return;
    }
    free(state->titles[index]);
    for (uint32_t i = index + 1; i < state->count; ++i) {
        state->titles[i - 1] = state->titles[i];
    }
    if (state->count > 0) {
        --state->count;
        state->titles[state->count] = NULL;
    }
    if (state->count == 0) {
        state->active = 0;
    } else if (state->active > index || state->active >= state->count) {
        state->active = state->active > 0 ? state->active - 1u : 0u;
        if (state->active >= state->count) {
            state->active = state->count - 1;
        }
    }
    state->indicator_ready = false;
    tabs_apply_visibility(tabs);
    pt_widget_mark_dirty(tabs, true);
    pt_widget_a11y_emit(tabs, pt_a11y_event_structure_changed);
}

void pt_tabs_remove_page(pt_widget *tabs, uint32_t index) {
    pt_widget *page = tabs_page_at_raw(tabs, index);
    if (!page) {
        return;
    }
    pt_widget_destroy(page);
}

uint32_t pt_tabs_get_page_count(const pt_widget *tabs) {
    return pt_tabs_page_count(tabs);
}

pt_widget *pt_tabs_get_page(const pt_widget *tabs, uint32_t index) {
    return tabs_page_at_raw(tabs, index);
}

void pt_tabs_set_page_title(pt_widget *tabs, uint32_t index, const char *title) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state || index >= state->count) {
        return;
    }
    free(state->titles[index]);
    state->titles[index] = pt_strdup(title ? title : "");
    pt_widget_mark_dirty(tabs, true);
    pt_widget_a11y_emit(tabs, pt_a11y_event_name_changed);
}

const char *pt_tabs_get_page_title(const pt_widget *tabs, uint32_t index) {
    const pt_tabs_state *state = tabs_state_const(tabs);
    if (!state || index >= state->count) {
        return "";
    }
    return state->titles[index] ? state->titles[index] : "";
}

uint32_t pt_tabs_get_active(const pt_widget *tabs) {
    const pt_tabs_state *state = tabs_state_const(tabs);
    return state ? state->active : 0;
}

void pt_tabs_select(pt_widget *tabs, uint32_t index, bool notify) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    if (state->count == 0) {
        return;
    }
    if (index >= state->count) {
        index = state->count - 1;
    }
    bool changed = state->active != index;
    state->active = index;
    tabs_apply_visibility(tabs);
    pt_widget_mark_dirty(tabs, true);
    if (changed) {
        pt_widget_a11y_emit(tabs, pt_a11y_event_selection_changed);
        if (notify && state->on_change) {
            state->on_change(tabs, index, state->change_data);
        }
    }
}

void pt_tabs_set_active(pt_widget *tabs, uint32_t index) {
    pt_tabs_select(tabs, index, false);
}

void pt_tabs_set_on_change(pt_widget *tabs, pt_index_callback callback, void *user_data) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    state->on_change = callback;
    state->change_data = user_data;
}

void pt_tabs_set_strip_height(pt_widget *tabs, float height) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    state->strip_height = pt_max_float(28.0f, height);
    pt_widget_mark_dirty(tabs, true);
}

void pt_tabs_sync_pages(pt_widget *tabs) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    if (state->count == 0) {
        state->active = 0;
    } else if (state->active >= state->count) {
        state->active = state->count - 1;
    }
    tabs_apply_visibility(tabs);
}

bool pt_tabs_key(pt_widget *tabs, uint32_t keysym) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state || state->count == 0) {
        return false;
    }
    uint32_t next = state->active;
    switch (keysym) {
    case XKB_KEY_Left:
        next = state->active > 0 ? state->active - 1 : 0;
        break;
    case XKB_KEY_Right:
        next = state->active + 1 < state->count ? state->active + 1 : state->active;
        break;
    case XKB_KEY_Home:
        next = 0;
        break;
    case XKB_KEY_End:
        next = state->count - 1;
        break;
    default:
        return false;
    }
    if (next != state->active) {
        pt_tabs_select(tabs, next, true);
    }
    return true;
}

static bool tabs_arrow_zone(const pt_tabs_state *state, float x, float inner_x,
                            float inner_w, bool *left) {
    if (!state->overflow) {
        return false;
    }
    if (x >= inner_x && x <= inner_x + pt_tabs_arrow_width) {
        *left = true;
        return true;
    }
    if (x >= inner_x + inner_w - pt_tabs_arrow_width && x <= inner_x + inner_w) {
        *left = false;
        return true;
    }
    return false;
}

void pt_tabs_pointer_down(pt_widget *tabs, float x, float y) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    float inner_x = tabs->x + tabs->padding_left;
    float inner_w = pt_max_float(0.0f, tabs->width - tabs->padding_left -
                                           tabs->padding_right);
    float strip_top = tabs->y + tabs->padding_top;
    if (y < strip_top || y > strip_top + state->strip_height) {
        return;
    }
    bool left = false;
    if (tabs_arrow_zone(state, x, inner_x, inner_w, &left)) {
        float available = pt_max_float(1.0f, inner_w - pt_tabs_arrow_width * 2.0f -
                                               pt_tabs_margin * 2.0f);
        float max_scroll = pt_max_float(0.0f, state->content_width - available);
        state->scroll_x = pt_clamp_float(state->scroll_x + (left ? -120.0f : 120.0f),
                                         0.0f, max_scroll);
        pt_widget_mark_dirty(tabs, true);
        return;
    }
    for (uint32_t i = 0; i < state->count; ++i) {
        if (x >= state->tab_x[i] && x <= state->tab_x[i] + state->tab_width[i]) {
            pt_tabs_select(tabs, i, true);
            return;
        }
    }
}

void pt_tabs_preferred(pt_widget *tabs, const pt_layout_ctx *ctx, float *width,
                       float *height) {
    pt_tabs_state *state = tabs_state(tabs);
    float preferred_w = 360.0f;
    float preferred_h = 260.0f;
    if (state && state->count > 0) {
        pt_widget *page = tabs_page_at_raw(tabs, state->active);
        if (page) {
            float page_w = 0.0f;
            float page_h = 0.0f;
            pt_widget_measure_child(page, ctx, 640.0f, 480.0f, &page_w, &page_h);
            preferred_w = pt_max_float(280.0f, page_w);
            preferred_h = state->strip_height + pt_max_float(160.0f, page_h);
        }
    }
    *width = tabs->requested_width >= 0.0f ? tabs->requested_width : preferred_w;
    *height = tabs->requested_height >= 0.0f ? tabs->requested_height : preferred_h;
}

static float tabs_tab_width(const pt_layout_ctx *ctx, const char *title) {
    float scale = ctx->scale > 0.0f ? ctx->scale : 1.0f;
    pt_font *font = pt_font_acquire(ctx->font_family, pt_font_weight_medium, pt_font_slant_upright, 14.0f * scale);
    float text_width = 0.0f;
    if (font && title && *title) {
        text_width = pt_font_measure(font, title, strlen(title)) / scale;
    }
    return pt_clamp_float(text_width + 32.0f, 88.0f, 220.0f);
}

void pt_tabs_layout(pt_widget *tabs, const pt_layout_ctx *ctx, pt_rect inner) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    if (state->count > tabs_actual_pages(tabs)) {
        state->count = tabs_actual_pages(tabs);
    }
    if (state->count == 0) {
        state->active = 0;
    } else if (state->active >= state->count) {
        state->active = state->count - 1;
    }
    state->strip_height = pt_max_float(28.0f, state->strip_height);
    tabs_ensure_tab_capacity(state, state->count);
    tabs_apply_visibility(tabs);

    float content_width = pt_tabs_margin * 2.0f;
    for (uint32_t i = 0; i < state->count; ++i) {
        state->tab_width[i] = tabs_tab_width(ctx, state->titles[i]);
        content_width += state->tab_width[i];
        if (i + 1 < state->count) {
            content_width += pt_tabs_gap;
        }
    }
    state->content_width = content_width;
    state->overflow = content_width > inner.w;
    float arrow_space = state->overflow ? pt_tabs_arrow_width * 2.0f : 0.0f;
    float available = pt_max_float(1.0f, inner.w - arrow_space - pt_tabs_margin * 2.0f);
    state->viewport_width = available;
    float max_scroll = pt_max_float(0.0f, content_width - pt_tabs_margin * 2.0f - available);
    if (state->count > 0) {
        float start = 0.0f;
        for (uint32_t i = 0; i < state->active; ++i) {
            start += state->tab_width[i] + pt_tabs_gap;
        }
        if (start < state->scroll_x) {
            state->scroll_x = start;
        } else if (start + state->tab_width[state->active] > state->scroll_x + available) {
            state->scroll_x = start + state->tab_width[state->active] - available;
        }
    }
    state->scroll_x = pt_clamp_float(state->scroll_x, 0.0f, max_scroll);

    float cursor = inner.x + pt_tabs_margin + (state->overflow ? pt_tabs_arrow_width : 0.0f) -
                   state->scroll_x;
    for (uint32_t i = 0; i < state->count; ++i) {
        state->tab_x[i] = cursor;
        cursor += state->tab_width[i] + pt_tabs_gap;
    }
    if (state->count > 0) {
        state->target_x = state->tab_x[state->active];
        state->target_width = state->tab_width[state->active];
    } else {
        state->target_x = inner.x;
        state->target_width = 0.0f;
    }
    if (!state->indicator_ready) {
        state->indicator_x = state->target_x;
        state->indicator_width = state->target_width;
        state->indicator_ready = true;
    }

    tabs->clip = pt_rect_intersect(tabs->clip, inner);
    float page_y = inner.y + state->strip_height;
    float page_h = pt_max_float(0.0f, inner.h - state->strip_height);
    uint32_t index = 0;
    for (pt_widget *page = tabs->first_child; page; page = page->next_sibling) {
        if (page->kind != pt_widget_tab_page) {
            continue;
        }
        if (index == state->active && state->count > 0) {
            page->visible = true;
            pt_widget_layout_child(page, inner.x, page_y, inner.w, page_h, ctx);
        }
        ++index;
    }
}

bool pt_tabs_update_animation(pt_widget *tabs, float dt) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state || !state->indicator_ready) {
        return false;
    }
    pt_app *app = pt_widget_app(tabs);
    bool reduced = app && app->reduced_motion;
    float x = reduced ? state->target_x
                      : pt_ease_towards(state->indicator_x, state->target_x, 20.0f, dt);
    float width = reduced ? state->target_width
                          : pt_ease_towards(state->indicator_width, state->target_width,
                                            20.0f, dt);
    if (fabsf(x - state->indicator_x) < pt_animation_epsilon) {
        x = state->target_x;
    }
    if (fabsf(width - state->indicator_width) < pt_animation_epsilon) {
        width = state->target_width;
    }
    if (x == state->indicator_x && width == state->indicator_width) {
        return false;
    }
    state->indicator_x = x;
    state->indicator_width = width;
    pt_widget_damage(tabs, 3.0f);
    return true;
}

void pt_tabs_refresh(pt_widget *tabs) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    state->count = tabs_actual_pages(tabs);
    state->indicator_ready = false;
    pt_tabs_sync_pages(tabs);
    pt_widget_mark_dirty(tabs, true);
}

void pt_tabs_free_extra(pt_widget *tabs) {
    pt_tabs_state *state = tabs_state(tabs);
    if (!state) {
        return;
    }
    for (uint32_t i = 0; i < state->count; ++i) {
        free(state->titles[i]);
    }
    free(state->titles);
    free(state->tab_x);
    free(state->tab_width);
    free(state);
    tabs->extra = NULL;
}

/* ---------- sidebar ---------- */

static uint32_t sidebar_count_raw(const pt_widget *sidebar) {
    uint32_t count = 0;
    for (pt_widget *item = sidebar ? sidebar->first_child : NULL; item;
         item = item->next_sibling) {
        if (item->kind == pt_widget_sidebar_item) {
            ++count;
        }
    }
    return count;
}

static pt_widget *sidebar_item_raw(const pt_widget *sidebar, uint32_t index) {
    pt_widget *item = sidebar ? sidebar->first_child : NULL;
    while (item) {
        if (item->kind == pt_widget_sidebar_item) {
            if (index == 0) {
                return item;
            }
            --index;
        }
        item = item->next_sibling;
    }
    return NULL;
}

pt_widget *pt_sidebar_create(pt_widget *parent) {
    pt_widget *sidebar = pt_widget_new(parent, pt_widget_sidebar);
    pt_sidebar_state *state = pt_alloc_zeroed(1, sizeof(*state));
    sidebar->extra = state;
    state->active = pt_list_index_none;
    state->item_height = 44.0f;
    return sidebar;
}

pt_widget *pt_sidebar_add_item(pt_widget *sidebar, pt_icon icon, const char *label) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return NULL;
    }
    pt_widget *item = pt_widget_new(sidebar, pt_widget_sidebar_item);
    item->icon = icon;
    item->text = pt_strdup(label ? label : "");
    item->row_selected = sidebar_count_raw(sidebar) - 1u == state->active;
    pt_widget_mark_dirty(sidebar, true);
    pt_widget_a11y_emit(sidebar, pt_a11y_event_structure_changed);
    return item;
}

uint32_t pt_sidebar_get_item_count(const pt_widget *sidebar) {
    return sidebar_count_raw(sidebar);
}

pt_widget *pt_sidebar_get_item(const pt_widget *sidebar, uint32_t index) {
    return sidebar_item_raw(sidebar, index);
}

uint32_t pt_sidebar_get_active(const pt_widget *sidebar) {
    const pt_sidebar_state *state = sidebar_state_const(sidebar);
    return state ? state->active : pt_list_index_none;
}

void pt_sidebar_select(pt_widget *sidebar, uint32_t index, bool notify) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return;
    }
    uint32_t count = sidebar_count_raw(sidebar);
    if (index >= count) {
        index = pt_list_index_none;
    }
    bool changed = state->active != index;
    state->active = index;
    uint32_t i = 0;
    for (pt_widget *item = sidebar->first_child; item; item = item->next_sibling) {
        if (item->kind != pt_widget_sidebar_item) {
            continue;
        }
        item->row_selected = i == index;
        ++i;
    }
    pt_widget_mark_dirty(sidebar, false);
    if (changed) {
        pt_widget_a11y_emit(sidebar, pt_a11y_event_selection_changed);
        if (notify && index != pt_list_index_none && state->on_select) {
            state->on_select(sidebar, index, state->select_data);
        }
    }
}

void pt_sidebar_set_active(pt_widget *sidebar, uint32_t index) {
    pt_sidebar_select(sidebar, index, false);
}

bool pt_sidebar_get_compact(const pt_widget *sidebar) {
    const pt_sidebar_state *state = sidebar_state_const(sidebar);
    return state ? state->compact : false;
}

void pt_sidebar_set_compact(pt_widget *sidebar, bool compact) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state || state->compact == compact) {
        return;
    }
    state->compact = compact;
    pt_widget_mark_dirty(sidebar, true);
}

void pt_sidebar_set_header(pt_widget *sidebar, const char *text) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return;
    }
    free(state->header);
    state->header = text && *text ? pt_strdup(text) : NULL;
    pt_widget_mark_dirty(sidebar, true);
}

const char *pt_sidebar_get_header(const pt_widget *sidebar) {
    const pt_sidebar_state *state = sidebar_state_const(sidebar);
    return state && state->header ? state->header : "";
}

void pt_sidebar_set_on_select(pt_widget *sidebar, pt_index_callback callback,
                              void *user_data) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return;
    }
    state->on_select = callback;
    state->select_data = user_data;
}

bool pt_sidebar_key(pt_widget *sidebar, uint32_t keysym) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return false;
    }
    uint32_t count = sidebar_count_raw(sidebar);
    if (count == 0) {
        return false;
    }
    uint32_t current = state->active;
    uint32_t next = current;
    switch (keysym) {
    case XKB_KEY_Down:
        next = current == pt_list_index_none ? 0
                                             : (current + 1 < count ? current + 1 : current);
        break;
    case XKB_KEY_Up:
        next = current == pt_list_index_none ? count - 1 : (current > 0 ? current - 1 : 0);
        break;
    case XKB_KEY_Home:
        next = 0;
        break;
    case XKB_KEY_End:
        next = count - 1;
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        if (current != pt_list_index_none) {
            if (state->on_select) {
                state->on_select(sidebar, current, state->select_data);
            }
            return true;
        }
        return false;
    default:
        return false;
    }
    if (next != current) {
        pt_sidebar_select(sidebar, next, true);
    }
    return true;
}

void pt_sidebar_preferred(pt_widget *sidebar, const pt_layout_ctx *ctx, float *width,
                          float *height) {
    pt_unused(ctx);
    const pt_sidebar_state *state = sidebar_state_const(sidebar);
    bool compact = state && state->compact;
    float preferred_w = compact ? 72.0f : 228.0f;
    float preferred_h = 24.0f + (float)sidebar_count_raw(sidebar) *
                                     (state ? state->item_height : 44.0f) +
                         (state && state->header ? 40.0f : 0.0f);
    *width = sidebar->requested_width >= 0.0f ? sidebar->requested_width : preferred_w;
    *height = sidebar->requested_height >= 0.0f ? sidebar->requested_height : preferred_h;
}

void pt_sidebar_layout(pt_widget *sidebar, const pt_layout_ctx *ctx, pt_rect inner) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return;
    }
    pt_rect inherited = sidebar->clip;
    float top = inner.y + (state->header && state->header[0] ? 56.0f : 12.0f);
    float item_height = pt_max_float(28.0f, state->item_height);
    float content_h = top - inner.y + (float)sidebar_count_raw(sidebar) * item_height +
                      8.0f;
    sidebar->scroll_content_height = content_h;
    float max_scroll = pt_max_float(0.0f, content_h - inner.h);
    sidebar->scroll_y = pt_clamp_float(sidebar->scroll_y, 0.0f, max_scroll);
    sidebar->clip = pt_rect_intersect(inherited, inner);
    float x = inner.x + 10.0f;
    float width = pt_max_float(0.0f, inner.w - 20.0f);
    float y = top - sidebar->scroll_y;
    uint32_t index = 0;
    for (pt_widget *item = sidebar->first_child; item; item = item->next_sibling) {
        if (item->kind != pt_widget_sidebar_item) {
            continue;
        }
        item->row_selected = index == state->active;
        pt_widget_layout_child(item, x, y, width, item_height, ctx);
        y += item_height + 4.0f;
        ++index;
    }
}

void pt_sidebar_free_extra(pt_widget *sidebar) {
    pt_sidebar_state *state = sidebar_state(sidebar);
    if (!state) {
        return;
    }
    free(state->header);
    free(state);
    sidebar->extra = NULL;
}
