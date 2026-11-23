/*
 * Pt ergonomics notebook - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The 2.1 surface in the wrapper:
 *
 *  - set_caret_movement / caret_movement on app, window and widget; the zero
 *    value pt_caret_movement_inherit resolves to visual, so nothing changes
 *    until a policy is set.
 *  - set_locale / locale on the app plus the free locale_ui_direction helper;
 *    the hint only supplies the application fallback and an explicit
 *    ui_direction still wins.
 *  - set_padding_logical on a widget; the physical set_padding stays and the
 *    last call wins.
 *  - bidi_control_inventory returns std::vector<pt_bidi_control_span> and
 *    bidi_sanitize returns std::string.
 */

#include <pt/pt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char *untrusted =
    "report"
    "\xE2\x80\xAE"   /* U+202E RIGHT-TO-LEFT OVERRIDE */
    "fdp.exe"
    "\xE2\x81\xA6"   /* U+2066 LEFT-TO-RIGHT ISOLATE */
    "https://example.com"
    "\xE2\x81\xA9";  /* U+2069 POP DIRECTIONAL ISOLATE */

constexpr const char *hebrew_mixed =
    "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "          /* Shalom */
    "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 "  /* Marhaba */
    "Hello, 123";

struct notebook_state {
    pt::app *application = nullptr;
    pt::window *window = nullptr;
    pt_widget *editor = nullptr;
    pt_widget *card = nullptr;
    pt_widget *status = nullptr;
};

void apply(notebook_state *state, pt_caret_movement movement, pt_ui_direction shell) {
    state->application->set_caret_movement(movement);
    state->window->set_caret_movement(movement);
    pt::widget(state->editor).set_caret_movement(pt_caret_movement_logical);
    state->window->set_ui_direction(shell);
    pt::widget(state->card)
        .set_padding_logical(12.0f, 36.0f, 12.0f, 8.0f)
        .set_ui_direction(shell);
    pt::widget(state->status)
        .set_text(movement == pt_caret_movement_logical ? "logical arrows, RTL card"
                                                        : "visual arrows, RTL card");
    std::printf("ergonomics: movement=%d window_movement=%d editor=%d shell=%d locale=%s\n",
                static_cast<int>(state->application->caret_movement()),
                static_cast<int>(state->window->caret_movement()),
                static_cast<int>(pt::widget(state->editor).caret_movement()),
                static_cast<int>(state->window->ui_direction()),
                state->application->locale() ? state->application->locale()->c_str() : "(none)");
    std::fflush(stdout);
}

void report_safety() {
    const std::vector<pt_bidi_control_span> spans = pt::bidi_control_inventory(untrusted);
    std::printf("ergonomics: inventory=%zu first=(%zu,%zu,kind=%d)\n", spans.size(),
                spans.empty() ? 0 : spans.front().offset,
                spans.empty() ? 0 : spans.front().length,
                spans.empty() ? 0 : static_cast<int>(spans.front().kind));
    const std::string stripped = pt::bidi_sanitize(untrusted);
    const std::string neutral = pt::bidi_sanitize(untrusted, true);
    std::printf("ergonomics: strip=%zu neutralize=%zu\n", stripped.size(), neutral.size());
    std::printf("ergonomics: locale ar=%d az-Arab=%d az-Latn=%d C=%d\n",
                static_cast<int>(pt::locale_ui_direction("ar")),
                static_cast<int>(pt::locale_ui_direction("az-Arab")),
                static_cast<int>(pt::locale_ui_direction("az-Latn")),
                static_cast<int>(pt::locale_ui_direction("C")));
    std::fflush(stdout);
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.ergonomics.cpp");
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));
    application.set_locale("he_IL.UTF-8");

    pt::window window(application, "Pt Ergonomics - C++", 760, 600);
    if (!window.valid()) {
        return 1;
    }

    notebook_state state;
    state.application = &application;
    state.window = &window;

    pt::widget page = pt::column(window.root().get());
    page.set_padding(24.0f, 24.0f, 24.0f, 24.0f).set_gap(12.0f);

    pt::label(page.get(), "CPP BINDING  ·  2.1 ERGONOMICS")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x7AA2F7));

    pt::label(page.get(), hebrew_mixed)
        .set_font_size(28.0f)
        .set_font_weight(pt_font_weight_bold)
        .set_text_direction(pt_text_direction_auto);

    pt::label(page.get(),
              "The locale hint resolves the shell, the caret policy switches the entry, "
              "and the card's logical padding follows whichever direction wins.")
        .set_font_size(13.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0x9AA7B8));

    pt::widget row = pt::row(page.get());
    row.set_gap(10.0f).box_align(pt_align_center);
    state.status = pt::label(row.get(), "")
                       .set_grow(1.0f)
                       .set_font_size(12.0f)
                       .set_foreground(pt_rgb(0x9ECE6A))
                       .get();

    pt::widget card = pt::card(page.get());
    card.set_gap(8.0f).set_ui_direction(pt_ui_direction_rtl);
    state.card = card.get();
    pt::label(card.get(), "Logical padding: start 36, end 8")
        .set_font_size(13.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_wrap(true);

    state.editor = pt::entry(card.get(), "identifier")
                       .set_text("shalom_el(marhaba, 123);")
                       .set_size(420.0f, 36.0f)
                       .set_ui_direction(pt_ui_direction_ltr)
                       .set_text_direction(pt_text_direction_ltr)
                       .set_caret_movement(pt_caret_movement_logical)
                       .set_align_self(pt_align_start)
                       .get();

    pt::button(row.get(), "Visual")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            apply(static_cast<notebook_state *>(data), pt_caret_movement_visual,
                  pt_ui_direction_rtl);
        }, &state);
    pt::button(row.get(), "Logical")
        .set_button_variant(pt_button_primary)
        .on_click([](pt_widget *, void *data) {
            apply(static_cast<notebook_state *>(data), pt_caret_movement_logical,
                  pt_ui_direction_rtl);
        }, &state);
    pt::button(row.get(), "LTR shell")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            apply(static_cast<notebook_state *>(data), pt_caret_movement_logical,
                  pt_ui_direction_ltr);
        }, &state);

    pt::spacer(page.get());
    pt::button(page.get(), "Quit")
        .set_button_variant(pt_button_ghost)
        .set_align_self(pt_align_start)
        .on_click([](pt_widget *, void *data) {
            static_cast<pt::window *>(data)->close();
        }, &window);

    apply(&state, pt_caret_movement_visual, pt_ui_direction_rtl);
    report_safety();
    window.show();

    const char *headless = std::getenv("PT_HEADLESS");
    if (headless && *headless && std::strcmp(headless, "0") != 0) {
        apply(&state, pt_caret_movement_logical, pt_ui_direction_ltr);
        apply(&state, pt_caret_movement_visual, pt_ui_direction_rtl);
        report_safety();
        return application.run_for(250);
    }
    return application.run();
}
