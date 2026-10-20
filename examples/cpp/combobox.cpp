/*
 * Pt combobox and multi-row selection - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.hpp>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct place {
    const char *name;
    const char *zone;
    unsigned stations;
};

constexpr place places[] = {
    {"Europe/Berlin", "UTC+1", 148},
    {"Europe/Lisbon", "UTC+0", 63},
    {"Asia/Tokyo", "UTC+9", 211},
    {"Asia/Kolkata", "UTC+5:30", 174},
    {"America/Denver", "UTC-7", 92},
    {"Pacific/Auckland", "UTC+13", 41},
};

constexpr std::string_view files[] = {
    "api/pt/pt.h",       "src/combobox.c",  "src/tree.c",      "src/table.c",
    "src/widget.c",      "bindings/cpp/pt", "bindings/python", "bindings/java",
    "tests/test_a11y.c", "docs/bindings.md",
};

struct combo_state {
    pt_window *window = nullptr;
    pt::widget region_box;
    pt::widget font_box;
    pt::widget list;
    pt::widget detail;
    pt::widget status;
    unsigned typed = 0;
};

std::string join(const std::vector<uint32_t> &rows) {
    std::string text;
    for (size_t i = 0; i < rows.size(); ++i) {
        text += i == 0 ? "" : ", ";
        text += std::to_string(rows[i]);
    }
    return text;
}

void report(combo_state *state, std::string_view text) {
    state->status.set_text(text);
}

void describe(combo_state *state, std::string prefix) {
    std::vector<uint32_t> rows = state->list.list_selection();
    prefix += " · " + std::to_string(rows.size()) + " of " + std::to_string(std::size(files)) +
              " rows [" + join(rows) + "] · primary " +
              (state->list.selected_row() == pt_list_index_none ? "cleared" : "set");
    report(state, prefix);
}

void on_region(pt_widget *box, uint32_t index, void *data) {
    auto *state = static_cast<combo_state *>(data);
    const auto *chosen = static_cast<const place *>(pt_combobox_get_item_data(box, index));
    if (!chosen) {
        state->detail.set_text("That row carries no payload.");
        report(state, "Chose an item without data");
        return;
    }
    state->detail.set_text(std::string(chosen->name) + " · " + chosen->zone + " · " +
                           std::to_string(chosen->stations) + " stations");
    std::string text = std::string("Selected ") + chosen->name;
    report(state, text);
    pt_window_show_toast(state->window, text.c_str(), pt_toast_info, 2000);
}

void on_font(pt_widget *, uint32_t, void *data) {
    auto *state = static_cast<combo_state *>(data);
    report(state, "Font chosen: " + state->font_box.combo_text());
}

void on_font_text(pt_widget *, const char *text, void *data) {
    auto *state = static_cast<combo_state *>(data);
    ++state->typed;
    report(state, "Typed \"" + std::string(text ? text : "") + "\" · keystroke " +
                      std::to_string(state->typed));
}

void on_bind(pt_widget *, pt_widget *row, uint32_t index, void *) {
    if (index >= std::size(files)) {
        pt_widget_set_text(row, "");
        return;
    }
    pt_widget_set_text(row, std::string(files[index]).c_str());
    pt_widget_set_icon(row, pt_icon_document);
}

void on_rows_selected(pt_widget *, uint32_t index, void *data) {
    describe(static_cast<combo_state *>(data), "Pointer chose row " + std::to_string(index));
}

void on_range(pt_widget *, void *data) {
    auto *state = static_cast<combo_state *>(data);
    state->list.list_select_range(2, 4);
    describe(state, "Range 2-4");
}

void on_toggle(pt_widget *, void *data) {
    auto *state = static_cast<combo_state *>(data);
    uint32_t row = state->list.is_list_row_selected(6) ? 3u : 6u;
    state->list.list_select_toggle(row);
    describe(state, "Toggled row " + std::to_string(row));
}

void on_clear(pt_widget *, void *data) {
    auto *state = static_cast<combo_state *>(data);
    state->list.clear_list_selection();
    describe(state, "Selection cleared");
}

void on_popup(pt_widget *, void *data) {
    auto *state = static_cast<combo_state *>(data);
    if (state->region_box.is_popup_open()) {
        state->region_box.close_popup();
        report(state, "Popup closed");
        return;
    }
    state->region_box.open_popup();
    report(state, "Popup open · max height " +
                      std::to_string(static_cast<int>(state->region_box.popup_max_height())) +
                      " px");
}

void on_preset(pt_widget *, void *data) {
    auto *state = static_cast<combo_state *>(data);
    /* Programmatic selection is silent, so the status line is written here. */
    state->region_box.set_combo_selected(4);
    on_region(state->region_box.get(), 4, state);
    state->detail.set_text("Set from code: no on_change fired.");
}

void on_mode(pt_widget *, void *data) {
    auto *state = static_cast<combo_state *>(data);
    pt::combo_mode mode = state->font_box.combo_mode() == pt_combobox_select_only
                              ? pt_combobox_filterable
                              : pt_combobox_select_only;
    state->font_box.set_combo_mode(mode);
    report(state, mode == pt_combobox_filterable ? "Font box is now filterable"
                                                 : "Font box is now select only");
}

void fill_regions(combo_state *state) {
    state->region_box.clear_items();
    for (const place &entry : places) {
        state->region_box
            .add_item(entry.name)
            .set_item_data(state->region_box.item_count() - 1u, const_cast<place *>(&entry));
    }
    /* Disabled rows stay visible but cannot be highlighted or chosen. */
    state->region_box.set_item_disabled(1, true);
}

bool headless() {
    const char *value = std::getenv("PT_HEADLESS");
    return value && *value && std::string_view(value) != "0";
}

} // namespace

int main() {
    pt::app application("pt.example.combobox.cpp");
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x4C8DFF));

    pt::window window(application, "Pt Combobox - C++", 880, 600);
    if (!window.valid()) {
        return 1;
    }

    combo_state state;
    state.window = window.get();
    pt::widget root = window.root();
    root.set_gap(14.0f).set_padding(26.0f, 26.0f, 26.0f, 26.0f);

    pt::label(root.get(), "COMBOBOX  ·  SELECTION  ·  0.10")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x4C8DFF));
    pt::label(root.get(), "One field, a list of choices")
        .set_font_size(30.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(root.get(),
              "A combobox is a closed pick list built on the same overlay primitives as the "
              "rest of the toolkit. Filterable mode puts a real editor in the field and reports "
              "every keystroke. Selection in the data views is a set, so ranges and toggles "
              "behave the same in a list, a table and a tree.")
        .set_wrap(true)
        .set_foreground(pt_rgb(0x9AA4B2));

    pt::widget boxes = pt::row(root.get());
    boxes.set_gap(14.0f);

    pt::widget region_card = pt::card(boxes.get());
    region_card.set_grow(1.0f).set_padding(16.0f, 16.0f, 16.0f, 16.0f).set_gap(10.0f);
    pt::label(region_card.get(), "Select only").set_font_weight(pt_font_weight_semibold);
    state.region_box = pt::combobox(region_card.get());
    state.region_box.set_min_width(220.0f)
        .set_combo_placeholder("Choose a region")
        .set_a11y_name("Region")
        .set_a11y_description("Closed list of regions; rows carry the model pointer.")
        .set_popup_max_height(190.0f)
        .on_combo_change(on_region, &state);
    fill_regions(&state);
    state.detail = pt::label(region_card.get(), "Pick a region to see its payload.")
                       .set_font_size(12.0f)
                       .set_wrap(true)
                       .set_foreground(pt_rgb(0x8B98AA));

    pt::widget region_tools = pt::wrap(region_card.get());
    region_tools.set_wrap_gap(8.0f, 8.0f);
    pt::button(region_tools.get(), "Toggle popup")
        .set_button_variant(pt_button_secondary)
        .set_tooltip("Open or close the panel")
        .on_click(on_popup, &state);
    pt::button(region_tools.get(), "Select by code")
        .set_button_variant(pt_button_ghost)
        .on_click(on_preset, &state);
    pt::button(region_tools.get(), "Reload items")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            auto *view = static_cast<combo_state *>(data);
            fill_regions(view);
            report(view, "Reloaded items with fresh payloads");
        }, &state);

    pt::widget font_card = pt::card(boxes.get());
    font_card.set_grow(1.0f).set_padding(16.0f, 16.0f, 16.0f, 16.0f).set_gap(10.0f);
    pt::label(font_card.get(), "Filterable").set_font_weight(pt_font_weight_semibold);
    state.font_box = pt::combobox(font_card.get());
    state.font_box.set_min_width(220.0f)
        .set_combo_mode(pt_combobox_filterable)
        .set_combo_placeholder("Type to filter fonts")
        .set_a11y_name("Font family")
        .on_combo_change(on_font, &state)
        .on_combo_text_change(on_font_text, &state);
    for (std::string_view font : {"JetBrains Mono", "DejaVu Sans", "Noto Sans CJK", "Inter",
                                  "Source Serif", "IBM Plex Mono", "Cascadia Code",
                                  "Roboto Flex", "Terminus"}) {
        state.font_box.add_item(font);
    }

    pt::widget font_tools = pt::wrap(font_card.get());
    font_tools.set_wrap_gap(8.0f, 8.0f);
    pt::button(font_tools.get(), "Switch mode")
        .set_button_variant(pt_button_secondary)
        .on_click(on_mode, &state);
    pt::button(font_tools.get(), "Remove first item")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            auto *view = static_cast<combo_state *>(data);
            if (view->font_box.item_count() == 0) {
                report(view, "No items left in the font list");
                return;
            }
            std::string first = view->font_box.item_text(0);
            view->font_box.remove_item(0);
            report(view, "Removed \"" + first + "\" · " +
                             std::to_string(view->font_box.item_count()) + " left");
        }, &state);

    pt::widget list_card = pt::card(root.get());
    list_card.set_grow(1.0f).set_padding(16.0f, 16.0f, 16.0f, 16.0f).set_gap(10.0f);
    state.list = pt::list_view(list_card.get());
    state.list.set_grow(1.0f)
        .set_min_height(190.0f)
        .set_a11y_name("Files")
        .set_row_height(34.0f)
        .set_row_count(static_cast<uint32_t>(std::size(files)))
        .on_bind_row(on_bind, &state)
        .on_row_select(on_rows_selected, &state);

    pt::widget row_tools = pt::wrap(list_card.get());
    row_tools.set_wrap_gap(8.0f, 8.0f);
    pt::button(row_tools.get(), "Range 2-4")
        .set_button_variant(pt_button_primary)
        .on_click(on_range, &state);
    pt::button(row_tools.get(), "Toggle 6")
        .set_button_variant(pt_button_secondary)
        .on_click(on_toggle, &state);
    pt::button(row_tools.get(), "Clear")
        .set_button_variant(pt_button_ghost)
        .on_click(on_clear, &state);

    state.status =
        pt::label(root.get(), "Ready").set_font_size(12.0f).set_a11y_name("Status").set_foreground(
            pt_rgb(0x8B98AA));
    pt::label(root.get(),
              "The popup flips above the field near the bottom of the window. Marquee-drag or "
              "Shift-click a row to extend the selection set.")
        .set_font_size(12.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0x6F7C8F));

    window.show();
    window.focus(state.region_box);

    if (!headless()) {
        return application.run();
    }
    on_mode(nullptr, &state);
    on_popup(nullptr, &state);
    on_region(state.region_box.get(), 0, &state);
    on_preset(nullptr, &state);
    on_range(nullptr, &state);
    on_toggle(nullptr, &state);
    on_toggle(nullptr, &state);
    on_clear(nullptr, &state);
    return application.run_for(250);
}
