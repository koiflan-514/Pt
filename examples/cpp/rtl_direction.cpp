/*
 * Pt direction switch - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The eight 2.0 direction functions are ordinary value setters/getters, so the
 * C++ wrapper returns the builder from each setter and a [[nodiscard]] enum
 * from each getter. Both zero values (pt_ui_direction_inherit and
 * pt_text_direction_auto) inherit, so this file leaves the nested LTR island
 * explicit while the shell toggles.
 */

#include <pt/pt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char *hebrew_mixed =
    "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "          /* Shalom */
    "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "  /* Marhaba */
    "Hello, 123";

struct direction_state {
    pt::app *application = nullptr;
    pt::window *window = nullptr;
    pt_widget *row = nullptr;
    pt_widget *entry = nullptr;
    pt_widget *title = nullptr;
    pt_widget *nested = nullptr;
    pt_widget *status = nullptr;
    pt_ui_direction direction = pt_ui_direction_rtl;
};

void apply(direction_state *state, pt_ui_direction direction) {
    state->direction = direction;
    state->application->set_ui_direction(direction);
    state->window->set_ui_direction(direction);
    pt::widget(state->row).set_ui_direction(direction);
    pt::widget(state->entry).set_ui_direction(direction);
    pt::widget(state->title).set_text_direction(direction == pt_ui_direction_rtl
                                                    ? pt_text_direction_rtl
                                                    : pt_text_direction_ltr);
    pt::widget(state->nested).set_ui_direction(pt_ui_direction_ltr);
    pt::widget(state->nested).set_text_direction(pt_text_direction_ltr);
    pt::widget(state->status)
        .set_text(direction == pt_ui_direction_rtl ? "RTL shell" : "LTR shell");
    std::printf("rtl_direction: app=%d window=%d row=%d entry=%d nested=%d\n",
                static_cast<int>(state->application->ui_direction()),
                static_cast<int>(state->window->ui_direction()),
                static_cast<int>(pt::widget(state->row).ui_direction()),
                static_cast<int>(pt::widget(state->entry).ui_direction()),
                static_cast<int>(pt::widget(state->nested).ui_direction()));
    std::fflush(stdout);
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.rtl_direction.cpp");
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));

    pt::window window(application, "Pt Direction - C++", 720, 560);
    if (!window.valid()) {
        return 1;
    }

    direction_state state;
    state.application = &application;
    state.window = &window;
    pt_widget *root = window.root().get();

    pt::widget page = pt::column(root);
    page.set_padding(26.0f, 26.0f, 26.0f, 26.0f).set_gap(12.0f);

    pt::label(page.get(), "CPP BINDING  ·  DIRECTION")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x7AA2F7));

    state.title = pt::label(page.get(), hebrew_mixed)
                      .set_font_size(30.0f)
                      .set_font_weight(pt_font_weight_bold)
                      .set_text_direction(pt_text_direction_auto)
                      .get();
    pt::label(page.get(),
              "pt_ui_direction mirrors rows and chrome; pt_text_direction selects the "
              "paragraph base. auto runs UBA P2/P3, so the heading resolves RTL while the "
              "ASCII suffix stays LTR.")
        .set_font_size(13.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0x9AA7B8));

    pt::widget row = pt::row(page.get());
    row.set_gap(10.0f).set_ui_direction(pt_ui_direction_rtl).box_align(pt_align_center);
    state.row = row.get();
    pt::button(row.get(), "Force LTR")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            apply(static_cast<direction_state *>(data), pt_ui_direction_ltr);
        }, &state);
    pt::button(row.get(), "Force RTL")
        .set_button_variant(pt_button_primary)
        .on_click([](pt_widget *, void *data) {
            apply(static_cast<direction_state *>(data), pt_ui_direction_rtl);
        }, &state);
    state.status = pt::label(row.get(), "RTL shell")
                       .set_grow(1.0f)
                       .set_font_size(12.0f)
                       .set_foreground(pt_rgb(0x9ECE6A))
                       .get();

    state.entry = pt::entry(page.get(), "Type here")
                      .set_text("\xD7\xA2\xD7\x91\xD7\x95\xD7\x93\xD7\x94 work@example.com")
                      .set_size(420.0f, 36.0f)
                      .set_ui_direction(pt_ui_direction_rtl)
                      .set_text_direction(pt_text_direction_auto)
                      .set_align_self(pt_align_start)
                      .get();

    pt::widget area = pt::text_area(page.get(), "Type a note");
    area.set_text("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D"
                  " - Hello world\nHello - \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D")
        .set_size(560.0f, 130.0f)
        .set_ui_direction(pt_ui_direction_rtl)
        .set_text_direction(pt_text_direction_auto);

    pt::widget nested = pt::card(page.get());
    nested.set_padding(14.0f, 14.0f, 14.0f, 14.0f).set_gap(8.0f);
    nested.set_ui_direction(pt_ui_direction_ltr).set_text_direction(pt_text_direction_ltr);
    state.nested = nested.get();
    pt::label(nested.get(), "Nested LTR island inside the RTL shell.")
        .set_font_size(13.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0xBB9AF7));
    pt::entry(nested.get(), "code = 1;")
        .set_text("int main(void) { return 0; }")
        .set_text_direction(pt_text_direction_ltr)
        .set_size(340.0f, 34.0f)
        .set_align_self(pt_align_start);

    pt::spacer(page.get());
    pt::button(page.get(), "Quit")
        .set_button_variant(pt_button_ghost)
        .set_align_self(pt_align_start)
        .on_click([](pt_widget *, void *data) {
            static_cast<pt::window *>(data)->close();
        }, &window);

    apply(&state, pt_ui_direction_rtl);
    window.show();

    const char *headless = std::getenv("PT_HEADLESS");
    if (headless && *headless && std::strcmp(headless, "0") != 0) {
        apply(&state, pt_ui_direction_ltr);
        apply(&state, pt_ui_direction_rtl);
        int status = application.run_for(250);
        return status;
    }
    return application.run();
}