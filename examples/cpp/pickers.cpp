/*
 * Pt pickers - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * A searchable select, a date picker, a number picker and a colour picker in
 * one window. Programmatic setters are silent; the on_change callbacks write a
 * log line into the status label.
 */

#include <pt/pt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct pickers_state {
    pt::widget select;
    pt::widget date;
    pt::widget number;
    pt::widget color;
    pt::widget status;
    unsigned changes = 0;
};

void log_line(pickers_state &state, const std::string &line) {
    ++state.changes;
    char text[320];
    std::snprintf(text, sizeof(text), "#%u · %s", state.changes, line.c_str());
    state.status.set_text(text);
    std::printf("pickers-cpp: %s\n", text);
    std::fflush(stdout);
}

void on_select(pt_widget *select, uint32_t index, void *data) {
    auto *ctx = static_cast<pickers_state *>(data);
    if (index == pt_list_index_none) {
        log_line(*ctx, "select cleared");
        return;
    }
    const char *text = pt_select_get_item_text(select, index);
    log_line(*ctx, "select committed item " + std::to_string(index) + " \"" +
                          (text ? text : "") + "\"");
}

void on_date(pt_widget *, pt_date date, void *data) {
    auto *ctx = static_cast<pickers_state *>(data);
    char line[96];
    if (date.year == 0) {
        log_line(*ctx, "date cleared");
        return;
    }
    std::snprintf(line, sizeof(line), "date committed %04d-%02u-%02u", (int)date.year,
                  (unsigned)date.month, (unsigned)date.day);
    log_line(*ctx, line);
}

void on_number(pt_widget *, double value, void *data) {
    auto *ctx = static_cast<pickers_state *>(data);
    char line[96];
    std::snprintf(line, sizeof(line), "number committed %.3f", value);
    log_line(*ctx, line);
}

void on_color(pt_widget *picker, pt_color color, void *data) {
    auto *ctx = static_cast<pickers_state *>(data);
    const char *hex = pt_color_picker_get_hex(picker);
    char line[128];
    std::snprintf(line, sizeof(line), "colour committed %s (0x%08X)", hex ? hex : "",
                  (unsigned)color);
    log_line(*ctx, line);
}

pt::widget field_block(pt_widget *parent, const char *label) {
    pt::widget column = pt::column(parent);
    column.set_gap(4.0f);
    pt::label(column.get(), label)
        .set_font_size(12.0f)
        .set_font_weight(pt_font_weight_medium)
        .set_foreground(pt_rgb(0xAAB6C6));
    return column;
}

void field_note(pt_widget *column, const char *hint) {
    pt::label(column, hint).set_font_size(11.0f).set_foreground(pt_rgb(0x6F7C8F)).set_wrap(true);
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.pickers.cpp");
    if (!application.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x4C8DFF));

    pt::window window(application, "Pt Pickers - C++", 760, 760);
    if (!window.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }

    pickers_state state;
    pt::widget root = window.root();
    root.set_padding(26.0f, 26.0f, 26.0f, 26.0f).set_gap(14.0f);

    pt::label(root, "PICKERS  ·  1.2  ·  CPP")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x4C8DFF));
    pt::label(root, "Four fields, one focus chain")
        .set_font_size(28.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(root,
              "Every control is reachable with Tab. F4 or Alt+Down opens a popup; Down/Up move "
              "the highlight, Enter commits, Escape restores the committed value.")
        .set_font_size(13.0f)
        .set_foreground(pt_rgb(0x9AA7B8))
        .set_wrap(true);

    pt::widget card = pt::card(root);
    card.set_grow(1.0f).set_padding(18.0f, 18.0f, 18.0f, 18.0f).set_gap(14.0f);

    pt::widget select_block = field_block(card.get(), "Searchable select");
    state.select = pt::select(select_block.get());
    state.select.set_grow(1.0f)
        .set_min_width(260.0f)
        .set_a11y_name("City")
        .set_select_placeholder("Type to search a city")
        .set_select_max_popup_rows(6);
    static const char *const cities[] = {
        "Amsterdam", "Barcelona", "Berlin",   "Bordeaux", "Bristol",   "Brussels",
        "Copenhagen", "Dublin",   "Edinburgh", "Lisbon",  "London",    "Madrid",
        "Milan",      "Munich",   "Oslo",      "Paris",    "Porto",     "Prague",
        "Rome",       "Stockholm", "Vienna",   "Warsaw",   "Zurich",
    };
    for (const char *city : cities) {
        state.select.add_select_item(city);
    }
    state.select.on_select_change(on_select, &state);
    field_note(select_block.get(), "Substring filter is ASCII-case-insensitive. Delete with an "
                                   "empty query clears the value.");

    pt::widget date_block = field_block(card.get(), "Date picker");
    state.date = pt::date_picker(date_block.get());
    state.date.set_grow(1.0f)
        .set_a11y_name("Start date")
        .set_date_placeholder("Pick a day")
        .set_date_format(pt_date_format_dmy);
    state.date.set_date(pt::date_today());
    state.date.on_date_change(on_date, &state);
    field_note(date_block.get(), "Six fixed calendar weeks. Left/Right move a day, Up/Down a "
                                 "week, Page Up/Down a month, T selects today.");

    pt::widget number_block = field_block(card.get(), "Number picker");
    state.number = pt::number_picker(number_block.get());
    state.number.set_grow(1.0f)
        .set_a11y_name("Duration in minutes")
        .set_number_placeholder("Minutes")
        .set_number_range(0.0, 240.0)
        .set_number_step(5.0)
        .set_number_value(45.0);
    state.number.on_number_change(on_number, &state);
    field_note(number_block.get(), "No popup. Up/Down step by 5, Page Up/Down by 50, Home/End "
                                   "jump to the range edges; typed text clamps.");

    pt::widget color_block = field_block(card.get(), "Colour picker");
    state.color = pt::color_picker(color_block.get());
    state.color.set_grow(1.0f)
        .set_a11y_name("Accent colour")
        .set_color(pt_rgb(0x4C8DFF))
        .add_color_preset(pt_rgb(0x9ECE6A))
        .add_color_preset(pt_rgb(0xF7768E));
    state.color.on_color_change(on_color, &state);
    field_note(color_block.get(), "Accepts #RRGGBB and #RRGGBBAA. Tab moves through the "
                                  "palette; Enter commits the highlighted preset.");

    pt::widget tools = pt::row(card.get());
    tools.set_gap(8.0f).set_align_self(pt_align_center);
    pt::button(tools.get(), "Open select")
        .set_button_variant(pt_button_secondary)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<pickers_state *>(data);
            ctx->select.open_select();
            log_line(*ctx, std::string("select popup open=") +
                                 (ctx->select.is_select_open() ? "yes" : "no") + " matches=" +
                                 std::to_string(ctx->select.select_match_count()));
        }, &state);
    pt::button(tools.get(), "Open calendar")
        .set_button_variant(pt_button_secondary)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<pickers_state *>(data);
            ctx->date.open_date_picker();
            log_line(*ctx, std::string("calendar open=") +
                                 (ctx->date.is_date_picker_open() ? "yes" : "no"));
        }, &state);
    pt::button(tools.get(), "Open palette")
        .set_button_variant(pt_button_secondary)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<pickers_state *>(data);
            ctx->color.open_color_picker();
            log_line(*ctx, std::string("palette open=") +
                                 (ctx->color.is_color_picker_open() ? "yes" : "no") + " presets=" +
                                 std::to_string(ctx->color.color_preset_count()));
        }, &state);
    pt::button(tools.get(), "Close popups")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<pickers_state *>(data);
            ctx->select.close_select();
            ctx->date.close_date_picker();
            ctx->color.close_color_picker();
            log_line(*ctx, "popups closed; focus returns to the entry");
            pt_widget_focus(ctx->select.get());
        }, &state);
    pt::button(tools.get(), "Clear values")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<pickers_state *>(data);
            ctx->select.clear_select_selection();
            ctx->date.clear_date();
            ctx->number.clear_number();
            ctx->color.set_color(pt_rgb(0x000000));
            log_line(*ctx, "values cleared programmatically (silent)");
        }, &state);

    state.status = pt::label(root, "Ready · commit a value to see the change log");
    state.status.set_font_size(12.0f).set_foreground(pt_rgb(0x9ECE6A)).set_wrap(true)
        .set_a11y_name("Change log");

    pt::label(root,
              "Callbacks run after the control state has updated. Programmatic setters are "
              "silent, so a callback may destroy its picker; this example only reads it.")
        .set_font_size(12.0f)
        .set_foreground(pt_rgb(0x6F7C8F))
        .set_wrap(true);

    pt::button(root, "Quit")
        .set_button_variant(pt_button_ghost)
        .set_align_self(pt_align_start)
        .on_click([](pt_widget *, void *data) {
            static_cast<pt::app *>(data)->quit();
        }, &application);

    window.show();
    pt_widget_focus(state.select.get());

    const char *headless = std::getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && std::strcmp(headless, "0") != 0) {
        std::printf("pickers-cpp: select items=%u max_rows=%u placeholder=\"%s\"\n",
                    state.select.select_item_count(), state.select.select_max_popup_rows(),
                    state.select.select_placeholder().c_str());
        state.select.set_select_query("ber");
        std::printf("pickers-cpp: query \"ber\" matches=%u\n",
                    state.select.select_match_count());
        state.select.clear_select_selection();
        state.select.set_select_query("");
        state.select.open_select();
        state.select.set_select_selected(2);
        on_select(state.select.get(), state.select.select_selected(), &state);
        state.select.close_select();

        pt_date date = state.date.date_value();
        std::printf("pickers-cpp: date %04d-%02u-%02u valid=%d format=%d\n", (int)date.year,
                    (unsigned)date.month, (unsigned)date.day, pt::date_valid(date) ? 1 : 0,
                    (int)state.date.date_format());
        state.date.set_date(pt::date{2027, 1, 15});
        on_date(state.date.get(), state.date.date_value(), &state);

        state.number.step_number_by(2.0);
        std::printf("pickers-cpp: number %.1f has_value=%d text=\"%s\"\n",
                    state.number.number_value(), state.number.number_has_value() ? 1 : 0,
                    state.number.number_text().c_str());
        on_number(state.number.get(), state.number.number_value(), &state);

        state.color.set_color_hex("#ff8800");
        on_color(state.color.get(), state.color.color(), &state);
        state.color.open_color_picker();
        state.color.close_color_picker();
        status = application.run_for(250);
    } else {
        status = application.run();
    }
    return status;
}