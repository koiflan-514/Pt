/*
 * Pt combobox and multi-row selection: choose from a list, filter it as you
 * type, and drive range/toggle selection on a virtualized list.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGION_COUNT 6
#define FONT_COUNT 9
#define ROW_COUNT 10

typedef struct region {
    const char *name;
    const char *zone;
    uint32_t stations;
} region;

static region regions[REGION_COUNT] = {
    {"Europe/Berlin", "UTC+1", 148},
    {"Europe/Lisbon", "UTC+0", 63},
    {"Asia/Tokyo", "UTC+9", 211},
    {"Asia/Kolkata", "UTC+5:30", 174},
    {"America/Denver", "UTC-7", 92},
    {"Pacific/Auckland", "UTC+13", 41},
};

static const char *const fonts[FONT_COUNT] = {
    "JetBrains Mono", "DejaVu Sans", "Noto Sans CJK", "Inter", "Source Serif",
    "IBM Plex Mono", "Cascadia Code", "Roboto Flex", "Terminus",
};

static const char *const rows[ROW_COUNT] = {
    "api/pt/pt.h",       "src/combobox.c",  "src/tree.c",       "src/table.c",
    "src/widget.c",      "bindings/cpp/pt", "bindings/python",  "bindings/java",
    "tests/test_a11y.c", "docs/bindings.md",
};

typedef struct combo_state {
    pt_app *app;
    pt_window *window;
    pt_widget *region_box;
    pt_widget *font_box;
    pt_widget *list;
    pt_widget *detail;
    pt_widget *status;
    uint32_t typed;
} combo_state;

static void set_status(combo_state *state, const char *text) {
    pt_widget_set_text(state->status, text);
}

static void fill_regions(combo_state *state) {
    pt_combobox_clear(state->region_box);
    for (uint32_t i = 0; i < REGION_COUNT; ++i) {
        pt_combobox_add_item(state->region_box, regions[i].name);
        /* Item data is borrowed: the static table outlives the widget. */
        pt_combobox_set_item_data(state->region_box, i, &regions[i]);
    }
    /* Disabled rows stay visible but cannot be highlighted or chosen. */
    pt_combobox_set_item_disabled(state->region_box, 1, true);
}

static void on_region_chosen(pt_widget *box, uint32_t index, void *data) {
    combo_state *state = data;
    const region *chosen = pt_combobox_get_item_data(box, index);
    char text[192];
    if (!chosen) {
        pt_widget_set_text(state->detail, "No payload attached to that row.");
        snprintf(text, sizeof(text), "Chose item %u without data", (unsigned)index);
        set_status(state, text);
        return;
    }
    snprintf(text, sizeof(text), "%s · %s · %u stations", chosen->name, chosen->zone,
             (unsigned)chosen->stations);
    pt_widget_set_text(state->detail, text);
    snprintf(text, sizeof(text), "Selected %s", chosen->name);
    set_status(state, text);
    pt_window_show_toast(state->window, text, pt_toast_info, 2000);
}

static void on_font_typed(pt_widget *box, const char *text, void *data) {
    combo_state *state = data;
    ++state->typed;
    char status[128];
    snprintf(status, sizeof(status), "Filter %s · \"%s\" (%u keystroke%s)",
             pt_combobox_get_text(box), text ? text : "", (unsigned)state->typed,
             state->typed == 1 ? "" : "s");
    set_status(state, status);
}

static void on_font_chosen(pt_widget *box, uint32_t index, void *data) {
    combo_state *state = data;
    char text[128];
    snprintf(text, sizeof(text), "Font set to %s", pt_combobox_get_item_text(box, index));
    set_status(state, text);
}

static void on_reload_items(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    fill_regions(state);
    char text[96];
    snprintf(text, sizeof(text), "Reloaded %u regions", (unsigned)pt_combobox_get_item_count(
                                                             state->region_box));
    set_status(state, text);
}

static void on_remove_font_item(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    if (pt_combobox_get_item_count(state->font_box) == 0) {
        set_status(state, "No items left in the font list");
        return;
    }
    char text[128];
    snprintf(text, sizeof(text), "Removed \"%s\" · %u left",
             pt_combobox_get_item_text(state->font_box, 0),
             (unsigned)pt_combobox_get_item_count(state->font_box) - 1u);
    pt_combobox_remove_item(state->font_box, 0);
    set_status(state, text);
}

static void bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    (void)list;
    (void)data;
    if (index >= ROW_COUNT) {
        pt_widget_set_text(row, "");
        return;
    }
    pt_widget_set_text(row, rows[index]);
    pt_widget_set_icon(row, pt_icon_document);
}

static void describe_selection(combo_state *state, const char *prefix) {
    uint32_t held[ROW_COUNT] = {0};
    uint32_t count = pt_list_view_get_selection(state->list, held, ROW_COUNT);
    char indexes[96] = "";
    size_t used = 0;
    for (uint32_t i = 0; i < count && used + 8 < sizeof(indexes); ++i) {
        used += (size_t)snprintf(indexes + used, sizeof(indexes) - used, "%s%u", i ? ", " : "",
                                 (unsigned)held[i]);
    }
    char text[256];
    snprintf(text, sizeof(text), "%s · %u of %u rows [%s] · primary %s", prefix,
             (unsigned)count, (unsigned)ROW_COUNT, count ? indexes : "none",
             pt_list_view_get_selected(state->list) == pt_list_index_none ? "cleared" : "set");
    set_status(state, text);
}

static void on_rows_selected(pt_widget *list, uint32_t index, void *data) {
    (void)list;
    combo_state *state = data;
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "Pointer chose row %u", (unsigned)index);
    describe_selection(state, prefix);
}

static void on_select_range(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    pt_list_view_select_range(state->list, 2, 4);
    describe_selection(state, "Range 2-4");
}

static void on_toggle_row(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    uint32_t row = pt_list_view_is_row_selected(state->list, 6) ? 3 : 6;
    pt_list_view_select_toggle(state->list, row);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "Toggled row %u", (unsigned)row);
    describe_selection(state, prefix);
}

static void on_clear_rows(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    pt_list_view_clear_selection(state->list);
    describe_selection(state, "Selection cleared");
}

static void on_popup_toggle(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    if (pt_combobox_is_open(state->region_box)) {
        pt_combobox_close(state->region_box);
        set_status(state, "Popup closed");
        return;
    }
    pt_combobox_open(state->region_box);
    char text[128];
    snprintf(text, sizeof(text), "Popup open · max height %.0f px",
             pt_combobox_get_popup_max_height(state->region_box));
    set_status(state, text);
}

static void on_preset(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    /* Programmatic selection is silent, so the callback is not what updates the
     * status line here. */
    pt_combobox_set_selected(state->region_box, 4);
    on_region_chosen(state->region_box, 4, state);
    pt_widget_set_text(state->detail, "Set from code: no on_change fired.");
}

static void on_mode_switch(pt_widget *button, void *data) {
    (void)button;
    combo_state *state = data;
    pt_combobox_mode mode = pt_combobox_get_mode(state->font_box) == pt_combobox_select_only
                                ? pt_combobox_filterable
                                : pt_combobox_select_only;
    pt_combobox_set_mode(state->font_box, mode);
    char text[128];
    snprintf(text, sizeof(text), "Font box is now %s",
             mode == pt_combobox_filterable ? "filterable" : "select only");
    set_status(state, text);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.combobox");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt combobox and selection", 880, 620);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "COMBOBOX  ·  SELECTION  ·  0.10");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x4C8DFF));

    pt_widget *heading = pt_label_create(root, "One field, a list of choices");
    pt_widget_set_font_size(heading, 30.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "A combobox is a closed pick list with the same overlay and popup primitives as "
              "the rest of the toolkit. Filterable mode puts a real editor in the field and "
              "reports every keystroke. Selection in the data views is now a set, so ranges, "
              "toggles and marquee drags behave the same in a list, a table and a tree.");
    pt_widget_set_label_wrap(intro, true);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA4B2));

    combo_state state = {
        .app = app,
        .window = window,
        .typed = 0,
    };

    pt_widget *boxes = pt_row_create(root);
    pt_widget_set_gap(boxes, 14.0f);

    pt_widget *region_card = pt_card_create(boxes);
    pt_widget_set_grow(region_card, 1.0f);
    pt_widget_set_padding(region_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(region_card, 10.0f);
    pt_widget_set_a11y_name(region_card, "Region");

    pt_widget *region_title = pt_label_create(region_card, "Select only");
    pt_widget_set_font_weight(region_title, pt_font_weight_semibold);

    state.region_box = pt_combobox_create(region_card);
    pt_widget_set_min_width(state.region_box, 220.0f);
    pt_combobox_set_placeholder(state.region_box, "Choose a region");
    pt_widget_set_a11y_name(state.region_box, "Region");
    pt_widget_set_a11y_description(state.region_box,
                                   "Closed list of regions; rows carry the model pointer.");
    fill_regions(&state);
    pt_combobox_set_popup_max_height(state.region_box, 190.0f);
    pt_combobox_set_on_change(state.region_box, on_region_chosen, &state);
    state.detail = pt_label_create(region_card, "Pick a region to see its payload.");
    pt_widget_set_font_size(state.detail, 12.0f);
    pt_widget_set_foreground(state.detail, pt_rgb(0x8B98AA));
    pt_widget_set_label_wrap(state.detail, true);

    pt_widget *region_tools = pt_wrap_create(region_card);
    pt_wrap_set_gap(region_tools, 8.0f, 8.0f);
    pt_widget *popup_button = pt_button_create(region_tools, "Toggle popup");
    pt_button_set_variant(popup_button, pt_button_secondary);
    pt_button_set_on_click(popup_button, on_popup_toggle, &state);
    pt_widget *preset_button = pt_button_create(region_tools, "Select by code");
    pt_button_set_variant(preset_button, pt_button_ghost);
    pt_button_set_on_click(preset_button, on_preset, &state);
    pt_widget *reload_button = pt_button_create(region_tools, "Reload items");
    pt_button_set_variant(reload_button, pt_button_ghost);
    pt_button_set_on_click(reload_button, on_reload_items, &state);

    pt_widget *font_card = pt_card_create(boxes);
    pt_widget_set_grow(font_card, 1.0f);
    pt_widget_set_padding(font_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(font_card, 10.0f);
    pt_widget_set_a11y_name(font_card, "Font");

    pt_widget *font_title = pt_label_create(font_card, "Filterable");
    pt_widget_set_font_weight(font_title, pt_font_weight_semibold);

    state.font_box = pt_combobox_create(font_card);
    pt_widget_set_min_width(state.font_box, 220.0f);
    pt_combobox_set_mode(state.font_box, pt_combobox_filterable);
    pt_combobox_set_placeholder(state.font_box, "Type to filter fonts");
    pt_widget_set_a11y_name(state.font_box, "Font family");
    for (uint32_t i = 0; i < FONT_COUNT; ++i) {
        pt_combobox_add_item(state.font_box, fonts[i]);
    }
    pt_combobox_set_on_change(state.font_box, on_font_chosen, &state);
    pt_combobox_set_on_text_change(state.font_box, on_font_typed, &state);

    pt_widget *font_tools = pt_wrap_create(font_card);
    pt_wrap_set_gap(font_tools, 8.0f, 8.0f);
    pt_widget *mode_button = pt_button_create(font_tools, "Switch mode");
    pt_button_set_variant(mode_button, pt_button_secondary);
    pt_button_set_on_click(mode_button, on_mode_switch, &state);
    pt_widget *trim_button = pt_button_create(font_tools, "Remove first item");
    pt_button_set_variant(trim_button, pt_button_ghost);
    pt_button_set_on_click(trim_button, on_remove_font_item, &state);

    pt_widget *list_card = pt_card_create(root);
    pt_widget_set_grow(list_card, 1.0f);
    pt_widget_set_padding(list_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(list_card, 10.0f);

    state.list = pt_list_view_create(list_card);
    pt_widget_set_grow(state.list, 1.0f);
    pt_widget_set_min_height(state.list, 190.0f);
    pt_widget_set_a11y_name(state.list, "Files");
    pt_list_view_set_row_height(state.list, 34.0f);
    pt_list_view_set_row_count(state.list, ROW_COUNT);
    pt_list_view_set_on_bind_row(state.list, bind_row, &state);
    pt_list_view_set_on_select(state.list, on_rows_selected, &state);

    pt_widget *row_tools = pt_wrap_create(list_card);
    pt_wrap_set_gap(row_tools, 8.0f, 8.0f);
    pt_widget *range_button = pt_button_create(row_tools, "Range 2-4");
    pt_button_set_variant(range_button, pt_button_primary);
    pt_button_set_on_click(range_button, on_select_range, &state);
    pt_widget *toggle_button = pt_button_create(row_tools, "Toggle 6");
    pt_button_set_variant(toggle_button, pt_button_secondary);
    pt_button_set_on_click(toggle_button, on_toggle_row, &state);
    pt_widget *clear_button = pt_button_create(row_tools, "Clear");
    pt_button_set_variant(clear_button, pt_button_ghost);
    pt_button_set_on_click(clear_button, on_clear_rows, &state);

    state.status = pt_label_create(root, "");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x8B98AA));
    pt_widget_set_a11y_name(state.status, "Status");
    set_status(&state, "Ready");

    pt_widget *footer = pt_label_create(
        root, "The popup flips above the field near the bottom of the window and opens as a "
              "native surface when the compositor allows it. Marquee-drag or Shift-click a "
              "list row to extend the selection set.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_window_show(window);
    pt_widget_focus(state.region_box);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        char probe[96];
        snprintf(probe, sizeof(probe), "role %d · expandable %d",
                 (int)pt_widget_get_a11y_role(state.region_box),
                 (pt_widget_get_a11y_state(state.region_box) & pt_a11y_state_expandable) != 0);
        set_status(&state, probe);
        pt_combobox_set_text(state.font_box, "jet");
        on_font_typed(state.font_box, "jet", &state);
        on_mode_switch(NULL, &state);
        on_popup_toggle(NULL, &state);
        on_region_chosen(state.region_box, 0, &state);
        on_preset(NULL, &state);
        on_select_range(NULL, &state);
        on_toggle_row(NULL, &state);
        on_toggle_row(NULL, &state);
        on_clear_rows(NULL, &state);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}
