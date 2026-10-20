/*
 * Combo box checks: model, popup interaction, filtering and accessibility.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct combo_state {
    int changes;
    uint32_t last_index;
    int text_changes;
    char last_text[64];
} combo_state;

typedef struct fixture {
    pt_window *window;
    pt_canvas *canvas;
    pt_widget *anchor;
    pt_widget *combobox;
} fixture;

static void fixture_paint(fixture *fx) {
    pt_check(pt_window_paint(fx->window, fx->canvas));
}

static void fixture_open(fixture *fx, pt_app *app, float height, float anchor_height) {
    fx->window = pt_window_create(app, "combobox", 560, (int)height);
    fx->canvas = pt_canvas_create(560, (int)height, 1.0f);
    pt_widget *root = pt_window_root(fx->window);
    fx->anchor = pt_button_create(root, "Anchor");
    if (anchor_height > 0.0f) {
        pt_widget_set_height(fx->anchor, anchor_height);
    }
    fx->combobox = pt_combobox_create(root);
    pt_widget_set_width(fx->combobox, 260.0f);
    pt_window_show(fx->window);
    fixture_paint(fx);
}

static void fixture_close(fixture *fx) {
    pt_window_free(fx->window);
    pt_canvas_destroy(fx->canvas);
    fx->window = NULL;
    fx->canvas = NULL;
}

static pt_combobox_state *state_of(pt_widget *combobox) {
    return combobox && combobox->kind == pt_widget_combobox ? combobox->extra : NULL;
}

static pt_rect bounds_of(pt_widget *widget) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(widget, &x, &y, &w, &h);
    return (pt_rect){x, y, w, h};
}

static pt_widget *popup_of(pt_widget *combobox) {
    pt_combobox_state *state = state_of(combobox);
    if (!state || !state->popup || !state->popup->open) {
        return NULL;
    }
    return state->popup->panel;
}

static pt_widget *popup_rows_of(pt_widget *combobox) {
    pt_widget *panel = popup_of(combobox);
    return panel ? panel->first_child : NULL;
}

static uint32_t option_count(pt_widget *combobox) {
    pt_widget *rows = popup_rows_of(combobox);
    uint32_t count = 0;
    for (pt_widget *row = rows ? rows->first_child : NULL; row; row = row->next_sibling) {
        if (row->kind == pt_widget_list_row && row->popup_owner == combobox) {
            ++count;
        }
    }
    return count;
}

static pt_widget *option_at(pt_widget *combobox, uint32_t position) {
    pt_widget *rows = popup_rows_of(combobox);
    uint32_t seen = 0;
    for (pt_widget *row = rows ? rows->first_child : NULL; row; row = row->next_sibling) {
        if (row->kind != pt_widget_list_row || row->popup_owner != combobox) {
            continue;
        }
        if (seen == position) {
            return row;
        }
        ++seen;
    }
    return NULL;
}

static uint32_t highlighted_of(pt_widget *combobox) {
    pt_combobox_state *state = state_of(combobox);
    return state ? state->highlighted : pt_list_index_none;
}

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 10);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 20);
}

static void type(pt_window *window, uint32_t keysym, const char *utf8) {
    pt_window_input_key(window, keysym, 0, utf8, true);
}

static void on_change(pt_widget *widget, uint32_t index, void *user_data) {
    (void)widget;
    combo_state *state = user_data;
    ++state->changes;
    state->last_index = index;
}

static void on_text_change(pt_widget *widget, const char *text, void *user_data) {
    (void)widget;
    combo_state *state = user_data;
    ++state->text_changes;
    snprintf(state->last_text, sizeof(state->last_text), "%s", text ? text : "");
}

static void test_model(void) {
    pt_widget *combobox = pt_combobox_create(NULL);
    pt_check(combobox != NULL);
    pt_check(pt_combobox_get_item_count(combobox) == 0u);
    pt_check(pt_combobox_get_selected(combobox) == pt_list_index_none);
    pt_check(strcmp(pt_combobox_get_text(combobox), "") == 0);
    pt_check(pt_combobox_get_mode(combobox) == pt_combobox_select_only);
    pt_check(!pt_combobox_is_open(combobox));
    pt_check(pt_combobox_get_popup_max_height(combobox) > 0.0f);

    pt_combobox_add_item(combobox, "Red");
    pt_combobox_add_item(combobox, "Green");
    pt_combobox_insert_item(combobox, 1u, "Amber");
    pt_check(pt_combobox_get_item_count(combobox) == 3u);
    pt_check(strcmp(pt_combobox_get_item_text(combobox, 0u), "Red") == 0);
    pt_check(strcmp(pt_combobox_get_item_text(combobox, 1u), "Amber") == 0);
    pt_check(strcmp(pt_combobox_get_item_text(combobox, 2u), "Green") == 0);
    pt_check(strcmp(pt_combobox_get_item_text(combobox, 9u), "") == 0);

    int marker = 7;
    pt_combobox_set_item_data(combobox, 1u, &marker);
    pt_check(pt_combobox_get_item_data(combobox, 1u) == &marker);
    pt_check(pt_combobox_get_item_data(combobox, 0u) == NULL);

    pt_combobox_set_selected(combobox, 2u);
    pt_check(pt_combobox_get_selected(combobox) == 2u);
    pt_check(strcmp(pt_combobox_get_text(combobox), "Green") == 0);
    /* Inserting above the selection keeps it pointing at the same item. */
    pt_combobox_insert_item(combobox, 0u, "Violet");
    pt_check(pt_combobox_get_selected(combobox) == 3u);
    pt_check(strcmp(pt_combobox_get_text(combobox), "Green") == 0);

    pt_combobox_set_item_text(combobox, 3u, "Sea Green");
    pt_check(strcmp(pt_combobox_get_text(combobox), "Sea Green") == 0);

    pt_combobox_remove_item(combobox, 0u);
    pt_check(pt_combobox_get_selected(combobox) == 2u);
    pt_combobox_remove_item(combobox, 2u);
    pt_check(pt_combobox_get_selected(combobox) == pt_list_index_none);
    pt_check(strcmp(pt_combobox_get_text(combobox), "") == 0);

    /* In select-only mode the text is matched against the items. */
    pt_combobox_set_text(combobox, "Amber");
    pt_check(pt_combobox_get_selected(combobox) == 1u);
    pt_combobox_set_text(combobox, "Nothing");
    pt_check(pt_combobox_get_selected(combobox) == pt_list_index_none);

    pt_combobox_set_item_disabled(combobox, 0u, true);
    pt_check(pt_combobox_get_item_disabled(combobox, 0u));
    pt_check(pt_combobox_get_selected(combobox) == pt_list_index_none);
    pt_combobox_set_selected(combobox, 0u);
    pt_check(pt_combobox_get_selected(combobox) == pt_list_index_none);

    pt_combobox_clear(combobox);
    pt_check(pt_combobox_get_item_count(combobox) == 0u);
    pt_check(pt_combobox_get_selected(combobox) == pt_list_index_none);
    pt_widget_destroy(combobox);
}

static void fill_basic(pt_widget *combobox, combo_state *state) {
    static const char *names[] = {"Red", "Green", "Blue", "Amber"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        pt_combobox_add_item(combobox, names[i]);
    }
    state->last_index = pt_list_index_none;
    pt_combobox_set_on_change(combobox, on_change, state);
}

static void test_keyboard_select_only(pt_app *app) {
    fixture fx;
    fixture_open(&fx, app, 420.0f, 0.0f);
    combo_state state = {0};
    fill_basic(fx.combobox, &state);

    pt_check(pt_widget_focus(fx.combobox));
    pt_check(fx.window->focused == fx.combobox);

    type(fx.window, XKB_KEY_F4, NULL);
    pt_check(pt_combobox_is_open(fx.combobox));
    pt_check(option_count(fx.combobox) == 4u);
    pt_check(highlighted_of(fx.combobox) == 0u);

    type(fx.window, XKB_KEY_Down, NULL);
    pt_check(highlighted_of(fx.combobox) == 1u);
    type(fx.window, XKB_KEY_Up, NULL);
    type(fx.window, XKB_KEY_Up, NULL);
    /* Wrapping backwards from the first option lands on the last one. */
    pt_check(highlighted_of(fx.combobox) == 3u);
    pt_check(state.changes == 0);

    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == pt_list_index_none);
    pt_check(state.changes == 0);

    pt_combobox_open(fx.combobox);
    type(fx.window, XKB_KEY_End, NULL);
    pt_check(highlighted_of(fx.combobox) == 3u);
    type(fx.window, XKB_KEY_Home, NULL);
    pt_check(highlighted_of(fx.combobox) == 0u);
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == 0u);
    pt_check(state.changes == 1);
    pt_check(state.last_index == 0u);
    pt_check(strcmp(pt_combobox_get_text(fx.combobox), "Red") == 0);

    /* Page keys step by the options that fit in the popup. */
    pt_combobox_open(fx.combobox);
    fixture_paint(&fx);
    type(fx.window, XKB_KEY_Page_Down, NULL);
    pt_check(highlighted_of(fx.combobox) == 3u);
    pt_check(pt_combobox_is_open(fx.combobox));
    type(fx.window, XKB_KEY_Page_Up, NULL);
    pt_check(highlighted_of(fx.combobox) == 0u);
    /* Page keys do nothing while it is closed. */
    pt_combobox_close(fx.combobox);
    type(fx.window, XKB_KEY_Page_Down, NULL);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == 0u);

    /* Closed Home and End move the value without opening the popup. */
    type(fx.window, XKB_KEY_End, NULL);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == 3u);
    type(fx.window, XKB_KEY_Home, NULL);
    pt_check(pt_combobox_get_selected(fx.combobox) == 0u);
    pt_check(state.changes == 3);

    /* Typeahead matches a prefix and commits while closed. */
    type(fx.window, XKB_KEY_a, "a");
    pt_check(pt_combobox_get_selected(fx.combobox) == 3u);
    pt_check(state.changes == 4);
    type(fx.window, XKB_KEY_r, "r");
    /* "ar" still matches Amber, and a repeat of the value stays silent. */
    pt_check(pt_combobox_get_selected(fx.combobox) == 3u);
    pt_check(state.changes == 4);

    /* Space toggles the popup in select-only mode. */
    type(fx.window, XKB_KEY_space, " ");
    pt_check(pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == 3u);
    type(fx.window, XKB_KEY_space, " ");
    pt_check(!pt_combobox_is_open(fx.combobox));

    /* Disabled options stay out of the highlight ring. */
    pt_combobox_set_item_disabled(fx.combobox, 0u, true);
    pt_combobox_open(fx.combobox);
    pt_check(highlighted_of(fx.combobox) == 3u);
    pt_check(option_at(fx.combobox, 0u) && !option_at(fx.combobox, 0u)->enabled);
    type(fx.window, XKB_KEY_Down, NULL);
    pt_check(highlighted_of(fx.combobox) == 1u);
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(pt_combobox_get_selected(fx.combobox) == 1u);
    pt_check(!pt_combobox_is_open(fx.combobox));

    /* Alt+Down opens, Alt+Up opens too, and Tab closes without choosing. */
    pt_widget_focus(fx.combobox);
    pt_window_input_key(fx.window, XKB_KEY_Down, (uint32_t)pt_key_modifier_alt, NULL, true);
    pt_check(pt_combobox_is_open(fx.combobox));
    pt_window_input_key(fx.window, XKB_KEY_Tab, 0, NULL, true);
    pt_check(!pt_combobox_is_open(fx.combobox));

    fixture_close(&fx);
}

static void test_pointer(pt_app *app) {
    fixture fx;
    fixture_open(&fx, app, 420.0f, 0.0f);
    combo_state state = {0};
    fill_basic(fx.combobox, &state);
    pt_combobox_set_selected(fx.combobox, 0u);
    pt_combobox_set_item_disabled(fx.combobox, 1u, true);

    pt_rect field = bounds_of(fx.combobox);
    click_at(fx.window, field.x + field.w * 0.5f, field.y + field.h * 0.5f);
    pt_check(pt_combobox_is_open(fx.combobox));
    /* The committed value carries the highlight when the popup opens. */
    pt_check(highlighted_of(fx.combobox) == 0u);
    pt_widget *second = option_at(fx.combobox, 1u);
    pt_check(second != NULL);
    pt_check(second && !second->enabled);
    fixture_paint(&fx);

    pt_widget *third = option_at(fx.combobox, 2u);
    pt_check(third != NULL);
    pt_rect third_bounds = bounds_of(third);
    pt_check(third_bounds.w > 0.0f && third_bounds.h > 0.0f);
    click_at(fx.window, third_bounds.x + third_bounds.w * 0.5f,
             third_bounds.y + third_bounds.h * 0.5f);
    pt_check(pt_combobox_get_selected(fx.combobox) == 2u);
    pt_check(state.changes == 1);
    pt_check(state.last_index == 2u);
    pt_check(!pt_combobox_is_open(fx.combobox));

    /* A press outside the field closes the popup without changing the value. */
    pt_combobox_open(fx.combobox);
    click_at(fx.window, field.x + field.w + 60.0f, field.y + field.h * 0.5f);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == 2u);

    /* A disabled option swallows the click. */
    pt_combobox_open(fx.combobox);
    fixture_paint(&fx);
    pt_rect disabled_bounds = bounds_of(option_at(fx.combobox, 1u));
    click_at(fx.window, disabled_bounds.x + disabled_bounds.w * 0.5f,
             disabled_bounds.y + disabled_bounds.h * 0.5f);
    pt_check(pt_combobox_get_selected(fx.combobox) == 2u);
    pt_check(state.changes == 1);
    pt_combobox_close(fx.combobox);

    /* Pressing the field again closes a popup that is already open. */
    pt_combobox_open(fx.combobox);
    click_at(fx.window, field.x + field.w * 0.5f, field.y + field.h * 0.5f);
    pt_check(!pt_combobox_is_open(fx.combobox));

    /* An empty combo box refuses to open. */
    pt_combobox_clear(fx.combobox);
    pt_combobox_open(fx.combobox);
    pt_check(!pt_combobox_is_open(fx.combobox));

    fixture_close(&fx);
}

static void test_popup_geometry(pt_app *app) {
    fixture fx;
    /* The anchor pushes the field down so a long list has to flip above it. */
    fixture_open(&fx, app, 300.0f, 200.0f);
    for (uint32_t i = 0; i < 40u; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Option %u", i);
        pt_combobox_add_item(fx.combobox, label);
    }
    pt_combobox_set_popup_max_height(fx.combobox, 90.0f);
    pt_check(pt_combobox_get_popup_max_height(fx.combobox) == 90.0f);

    pt_combobox_open(fx.combobox);
    fixture_paint(&fx);
    pt_rect field = bounds_of(fx.combobox);
    pt_combobox_state *internal = state_of(fx.combobox);
    pt_rect popup = internal->popup->rect;
    pt_check(popup.w >= field.w);
    pt_check(popup.h > 0.0f && popup.h <= 90.0f + internal->popup->panel->padding_top +
                                                 internal->popup->panel->padding_bottom);
    pt_check(popup.y < field.y);
    pt_check(popup.y >= fx.window->header_height);
    pt_check(popup.y + popup.h <= (float)fx.window->height);
    pt_check(popup.x >= 0.0f && popup.x + popup.w <= (float)fx.window->width);

    /* The list scrolls rather than growing past the maximum height. */
    pt_widget *rows = popup_rows_of(fx.combobox);
    pt_check(rows != NULL);
    pt_check(rows && rows->scroll_content_height > rows->height);

    /* Rebuilding for a model change keeps the highlighted item, not its slot. */
    pt_check(highlighted_of(fx.combobox) == 0u);
    pt_combobox_insert_item(fx.combobox, 0u, "Zero");
    pt_check(pt_combobox_is_open(fx.combobox));
    pt_check(option_count(fx.combobox) == 41u);
    pt_check(highlighted_of(fx.combobox) == 1u);
    pt_check(strcmp(pt_combobox_get_item_text(fx.combobox, 1u), "Option 0") == 0);
    pt_combobox_remove_item(fx.combobox, 0u);
    pt_check(option_count(fx.combobox) == 40u);
    pt_check(highlighted_of(fx.combobox) == 0u);

    fixture_paint(&fx);
    /* Moving the highlight reveals it inside the scroll viewport. The insert
     * and remove above rebuilt the popup panel, so the scroll widget is looked
     * up again instead of reused from before the rebuild. */
    pt_check(pt_widget_focus(fx.combobox));
    type(fx.window, XKB_KEY_End, NULL);
    fixture_paint(&fx);
    pt_check(highlighted_of(fx.combobox) == 39u);
    rows = popup_rows_of(fx.combobox);
    pt_check(rows && rows->scroll_y > 0.0f);

    /* A field wider than the window keeps its popup inside the edges. */
    pt_combobox_close(fx.combobox);
    pt_widget_set_width(fx.combobox, 540.0f);
    pt_widget_set_height(fx.combobox, 24.0f);
    fixture_paint(&fx);
    pt_combobox_open(fx.combobox);
    fixture_paint(&fx);
    popup = state_of(fx.combobox)->popup->rect;
    pt_check(popup.x + popup.w <= (float)fx.window->width);
    fixture_close(&fx);
}

static void test_filterable(pt_app *app) {
    fixture fx;
    fixture_open(&fx, app, 420.0f, 0.0f);
    combo_state state = {0};
    state.last_index = pt_list_index_none;
    pt_combobox_set_on_change(fx.combobox, on_change, &state);
    pt_combobox_set_on_text_change(fx.combobox, on_text_change, &state);
    pt_combobox_add_item(fx.combobox, "North");
    pt_combobox_add_item(fx.combobox, "Norfolk");
    pt_combobox_add_item(fx.combobox, "South");
    pt_combobox_set_placeholder(fx.combobox, "Region");
    pt_combobox_set_mode(fx.combobox, pt_combobox_filterable);
    pt_check(pt_combobox_get_mode(fx.combobox) == pt_combobox_filterable);

    pt_widget *field = pt_combobox_field_widget(fx.combobox);
    pt_check(field != NULL);
    if (!field) {
        fixture_close(&fx);
        return;
    }
    pt_check(strcmp(field->placeholder, "Region") == 0);
    pt_check(!fx.combobox->focusable);
    pt_check(pt_widget_focus(field));
    pt_check(fx.window->focused == field);

    type(fx.window, XKB_KEY_n, "n");
    pt_check(pt_combobox_is_open(fx.combobox));
    pt_check(option_count(fx.combobox) == 2u);
    pt_check(state.text_changes == 1);
    pt_check(strcmp(state.last_text, "n") == 0);
    pt_check(state.changes == 0);

    type(fx.window, XKB_KEY_o, "o");
    type(fx.window, XKB_KEY_r, "r");
    pt_check(option_count(fx.combobox) == 2u);
    pt_check(highlighted_of(fx.combobox) == 0u);
    fixture_paint(&fx);
    pt_rect field_bounds = bounds_of(field);
    pt_check(field_bounds.w > 0.0f && field_bounds.w < bounds_of(fx.combobox).w);

    /* A space filters like any other character and can empty the list. */
    type(fx.window, XKB_KEY_space, " ");
    pt_check(option_count(fx.combobox) == 0u);
    pt_check(strcmp(pt_combobox_get_text(fx.combobox), "nor ") == 0);

    /* Backspacing reopens the matches. */
    type(fx.window, XKB_KEY_BackSpace, NULL);
    pt_check(option_count(fx.combobox) == 2u);

    /* Enter commits the highlighted match and closes the popup. */
    pt_check(highlighted_of(fx.combobox) == 0u);
    type(fx.window, XKB_KEY_Return, NULL);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(pt_combobox_get_selected(fx.combobox) == 0u);
    pt_check(state.changes == 1);
    pt_check(strcmp(field->text, "North") == 0);

    /* A programmatic selection writes the field without reporting a filter. */
    int text_changes_before = state.text_changes;
    pt_combobox_set_selected(fx.combobox, 2u);
    pt_check(strcmp(field->text, "South") == 0);
    pt_check(strcmp(pt_combobox_get_text(fx.combobox), "South") == 0);
    pt_check(state.text_changes == text_changes_before);

    /* Escape closes and restores the committed value. */
    pt_combobox_set_text(fx.combobox, "");
    type(fx.window, XKB_KEY_s, "s");
    pt_check(pt_combobox_is_open(fx.combobox));
    pt_check(option_count(fx.combobox) == 1u);
    type(fx.window, XKB_KEY_Escape, NULL);
    pt_check(!pt_combobox_is_open(fx.combobox));
    pt_check(strcmp(field->text, "South") == 0);
    pt_check(state_of(fx.combobox)->filter == NULL);
    pt_check(pt_combobox_get_selected(fx.combobox) == 2u);

    /* A press on the field keeps the popup open so the caret can move. */
    fixture_paint(&fx);
    pt_rect field_rect = bounds_of(field);
    pt_combobox_open(fx.combobox);
    click_at(fx.window, field_rect.x + field_rect.w * 0.5f,
             field_rect.y + field_rect.h * 0.5f);
    pt_check(pt_combobox_is_open(fx.combobox));
    /* Closing it again takes the filter out of the field. */
    pt_combobox_close(fx.combobox);
    pt_check(strcmp(field->text, "South") == 0);

    /* Switching back to select-only drops the editor. */
    pt_combobox_set_mode(fx.combobox, pt_combobox_select_only);
    pt_check(pt_combobox_field_widget(fx.combobox) == NULL);
    pt_check(fx.combobox->focusable);
    pt_check(strcmp(pt_combobox_get_text(fx.combobox), "South") == 0);
    pt_check(fx.window->focused == fx.combobox);

    fixture_close(&fx);
}

static void test_a11y(pt_app *app) {
    fixture fx;
    fixture_open(&fx, app, 420.0f, 0.0f);
    pt_combobox_add_item(fx.combobox, "Red");
    pt_combobox_add_item(fx.combobox, "Green");
    pt_combobox_set_placeholder(fx.combobox, "Colour");

    pt_check(pt_widget_get_a11y_role(fx.combobox) == pt_a11y_role_combo_box);
    uint32_t closed = pt_widget_get_a11y_state(fx.combobox);
    pt_check(closed & pt_a11y_state_expandable);
    pt_check(!(closed & pt_a11y_state_expanded));

    pt_combobox_open(fx.combobox);
    fixture_paint(&fx);
    pt_check(pt_widget_get_a11y_state(fx.combobox) & pt_a11y_state_expanded);
    pt_widget *panel = popup_of(fx.combobox);
    pt_check(panel && panel->a11y_role == pt_a11y_role_list_box);
    pt_check(panel && strcmp(pt_widget_get_a11y_name(panel), "Colour") == 0);

    pt_widget *row = option_at(fx.combobox, 0u);
    pt_check(pt_widget_get_a11y_role(row) == pt_a11y_role_list_item);
    uint32_t row_state = pt_widget_get_a11y_state(row);
    pt_check(row_state & pt_a11y_state_selectable);
    pt_check(row_state & pt_a11y_state_selected);
    pt_check(!(row_state & pt_a11y_state_expanded));

    pt_combobox_set_selected(fx.combobox, 1u);
    pt_check(strcmp(pt_widget_get_a11y_name(fx.combobox), "Green") == 0);
    pt_combobox_set_item_disabled(fx.combobox, 1u, true);
    pt_widget *disabled = option_at(fx.combobox, 1u);
    pt_check(disabled == NULL ||
             !(pt_widget_get_a11y_state(disabled) & pt_a11y_state_selectable));

    pt_combobox_close(fx.combobox);
    fixture_close(&fx);
}

static void test_destroy_while_open(pt_app *app) {
    fixture fx;
    fixture_open(&fx, app, 420.0f, 0.0f);
    pt_combobox_add_item(fx.combobox, "Red");
    pt_combobox_open(fx.combobox);
    pt_check(pt_window_overlays_active(fx.window));
    pt_widget_destroy(fx.combobox);
    pt_check(!pt_window_overlays_active(fx.window));
    fixture_paint(&fx);
    fixture_close(&fx);
}

static void test_theme_sizing(pt_app *app) {
    pt_window *window = pt_window_create(app, "combobox-metrics", 560, 420);
    pt_widget *root = pt_window_root(window);
    pt_widget *combobox = pt_combobox_create(root);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(560, 420, 1.0f);
    pt_check(pt_window_paint(window, canvas));

    float control = pt_widget_theme_metric_value(combobox,
                                                 pt_theme_metric_control_height);
    pt_rect box = bounds_of(combobox);
    pt_check_near(box.h, control, 1.0f);
    pt_check(box.w >= 160.0f);

    pt_combobox_add_item(combobox, "A much longer option label");
    pt_check(pt_window_paint(window, canvas));
    pt_check(pt_combobox_field_widget(combobox) == NULL);
    pt_combobox_set_mode(combobox, pt_combobox_filterable);
    pt_widget_set_width(combobox, 300.0f);
    pt_window_invalidate_layout(window, combobox);
    pt_check(pt_window_paint(window, canvas));
    pt_widget *field = pt_combobox_field_widget(combobox);
    pt_check(field != NULL);
    if (field) {
        pt_rect field_box = bounds_of(field);
        pt_check(field_box.x >= box.x);
        pt_check(field_box.w <= bounds_of(combobox).w);
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.combobox");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("combobox");
    }
    test_model();
    test_keyboard_select_only(app);
    test_pointer(app);
    test_popup_geometry(app);
    test_filterable(app);
    test_a11y(app);
    test_destroy_while_open(app);
    test_theme_sizing(app);
    pt_app_destroy(app);
    return pt_test_finish("combobox");
}
