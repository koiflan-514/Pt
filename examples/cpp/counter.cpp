/*
 * Pt counter example - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.hpp>

#include <string>

namespace {

struct counter_state {
    pt_widget *value = nullptr;
    pt_window *window = nullptr;
    int count = 0;
};

void render(counter_state *state) {
    std::string text = std::to_string(state->count);
    pt_widget_set_text(state->value, text.c_str());
}

void on_step(pt_widget *widget, void *data) {
    (void)widget;
    auto *state = static_cast<counter_state *>(data);
    state->count += 1;
    render(state);
}

void on_decrement(pt_widget *widget, void *data) {
    (void)widget;
    auto *state = static_cast<counter_state *>(data);
    state->count -= 1;
    render(state);
}

void on_reset(pt_widget *widget, void *data) {
    (void)widget;
    auto *state = static_cast<counter_state *>(data);
    state->count = 0;
    render(state);
    pt_window_show_toast(state->window, "Counter reset", pt_toast_success, 1800);
}

void on_quit(pt_widget *widget, void *data) {
    (void)widget;
    auto *window = static_cast<pt_window *>(data);
    pt_window_close(window);
}

} // namespace

int main() {
    pt::app application("pt.example.counter.cpp");
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));

    pt::window window(application, "Pt Counter - C++", 560, 460);
    if (!window.valid()) {
        return 1;
    }

    counter_state state;
    state.window = window.get();
    pt::menu menu(window);
    menu.add_item("Reset counter", "", on_reset, &state)
        .add_separator()
        .add_item("Quit", "", on_quit, &window);
    pt_widget *root = window.root().get();

    pt::widget header = pt::column(root);
    header.set_gap(6.0f).set_margin(0.0f, 0.0f, 4.0f, 0.0f);
    pt::label(header.get(), "CPP BINDING")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x7AA2F7));
    pt::label(header.get(), "Counter")
        .set_font_size(30.0f)
        .set_font_weight(pt_font_weight_bold);
    pt::label(header.get(),
              "A minimal C++23 front end over the Pt C ABI. Widgets stay in "
              "native code; the wrapper only adds type-safe handles.")
        .set_font_size(14.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0x9AA7B8));

    pt::widget card = pt::card(root);
    card.set_padding(28.0f, 28.0f, 28.0f, 28.0f)
        .set_radius(20.0f)
        .set_background(pt_rgb(0x161D28))
        .set_border_width(1.0f)
        .set_border_color(pt_rgb(0x253040))
        .set_gap(18.0f);

    pt::widget controls = pt::row(card.get());
    controls.set_gap(22.0f).box_justify(pt_justify_center).box_align(pt_align_center);
    pt::button(controls.get(), "-")
        .set_size(52.0f, 52.0f)
        .set_font_size(22.0f)
        .set_button_variant(pt_button_secondary)
        .set_tooltip("Decrease by the current step")
        .on_click(on_decrement, &state);
    state.value = pt::label(controls.get(), "0")
                      .set_min_width(150.0f)
                      .set_font_size(56.0f)
                      .set_font_weight(pt_font_weight_bold)
                      .set_text_align(pt_text_align_center)
                      .set_a11y_name("Counter value")
                      .get();
    pt::button(controls.get(), "+")
        .set_size(52.0f, 52.0f)
        .set_font_size(22.0f)
        .set_button_variant(pt_button_primary)
        .set_tooltip("Increase by the current step")
        .on_click(on_step, &state);

    pt::widget actions = pt::row(card.get());
    actions.set_gap(10.0f).box_justify(pt_justify_center);
    pt::button(actions.get(), "Reset")
        .set_button_variant(pt_button_ghost)
        .on_click(on_reset, &state);
    pt::button(actions.get(), "Actions")
        .set_button_variant(pt_button_ghost)
        .set_tooltip("Open the actions menu")
        .on_click([](pt_widget *widget, void *data) {
            static_cast<pt::menu *>(data)->show_below(pt::widget(widget));
        }, &menu);
    pt::button(actions.get(), "Quit")
        .set_button_variant(pt_button_ghost)
        .on_click(on_quit, &window);

    pt::spacer(root);
    pt::label(root, "Tab moves focus; Enter activates buttons or menu items.")
        .set_font_size(12.0f)
        .set_foreground(pt_rgb(0x6F7C8F));

    window.show();
    return application.run();
}
