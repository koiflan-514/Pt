/*
 * Pt protocol extensions - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The wrapper exposes the six 2.4.0 calls on pt::window. Token and handle
 * inputs are std::string_view and are copied before the C call; callbacks stay
 * raw pt_*_callback pointers with the usual user_data convention, and the
 * borrowed token or handle is never retained.
 *
 * Without the xdg-activation or xdg-foreign globals, or on a headless platform,
 * every call returns false (or no-ops). The example asserts that fallback under
 * PT_HEADLESS=1 and exits 0; the live path runs only with a compositor.
 */

#include <pt/pt.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct demo_state {
    pt_window *dialog = nullptr;
    bool token_seen = false;
    bool handle_seen = false;
    char token[256] = {};
};

void on_token(pt_window *window, const char *token, void *user_data) {
    auto *state = static_cast<demo_state *>(user_data);
    state->token_seen = true;
    std::snprintf(state->token, sizeof state->token, "%s", token ? token : "");
    /* The string is borrowed for this call; state->token is our copy. */
    if (state->token[0] != '\0') {
        pt_window_activate(window, state->token);
    }
}

void on_handle(pt_window *exporter, const char *handle, void *user_data) {
    auto *state = static_cast<demo_state *>(user_data);
    (void)exporter;
    state->handle_seen = handle && handle[0] != '\0';
    if (state->handle_seen) {
        pt_window_set_foreign_parent(state->dialog, handle);
    }
}

void add_text(pt::widget root, const char *text, float size, pt_color color) {
    pt::label(root.get(), text)
        .set_font_size(size)
        .set_foreground(color)
        .set_wrap(true);
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.protocol_extensions.cpp");
    if (!application.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt::last_error().c_str());
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));

    pt::window window(application, "Pt Protocol Extensions - C++", 720, 420);
    pt::window dialog(application, "Pt Foreign Dialog - C++", 420, 240);
    if (!window.valid() || !dialog.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt::last_error().c_str());
        return 1;
    }

    pt::widget root = window.root();
    root.set_padding(24.0f, 24.0f, 24.0f, 24.0f).set_gap(10.0f);

    pt::label(root.get(), "CPP BINDING  ·  ACTIVATION  ·  XDG-FOREIGN-V2")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x7AA2F7));

    add_text(root,
             "The window requests a one-shot activation token, activates with the "
             "borrowed string, exports a foreign toplevel handle and imports it on "
             "the second window.",
             13.0f, pt_rgb(0x9AA7B8));
    add_text(root,
             "A token or handle callback is valid only during the call. True from "
             "activate means sent, not focused, and the handle is never parsed.",
             12.0f, pt_rgb(0x6F7C8F));

    demo_state state;
    state.dialog = dialog.get();

    window.show();
    dialog.show();

    const char *headless = std::getenv("PT_HEADLESS");
    const bool headless_mode =
        headless && *headless && std::strcmp(headless, "0") != 0;

    int failures = 0;
    int run_status;
    if (headless_mode) {
        const bool token_request = window.request_activation_token(
            "dev.pt.examples.protocol_extensions.cpp", on_token, &state);
        const bool activated = window.activate("headless-token");
        const bool exported = window.export_foreign_handle(on_handle, &state);
        window.unexport_foreign_handle();
        window.unexport_foreign_handle();
        const bool parented = dialog.set_foreign_parent("headless-handle");
        dialog.clear_foreign_parent();
        dialog.clear_foreign_parent();

        if (token_request || activated || exported || parented) {
            ++failures;
        }
        if (state.token_seen || state.handle_seen) {
            ++failures;
        }
        std::printf("protocol_extensions: headless request=%d activate=%d export=%d "
                    "parent=%d callbacks=%d/%d failures=%d\n",
                    static_cast<int>(token_request), static_cast<int>(activated),
                    static_cast<int>(exported), static_cast<int>(parented),
                    static_cast<int>(state.token_seen),
                    static_cast<int>(state.handle_seen), failures);
        std::fflush(stdout);
        run_status = application.run_for(250);
    } else {
        const bool token_request = window.request_activation_token(
            "dev.pt.examples.protocol_extensions.cpp", on_token, &state);
        const bool exported = window.export_foreign_handle(on_handle, &state);
        std::printf("protocol_extensions: live request=%d export=%d\n",
                    static_cast<int>(token_request), static_cast<int>(exported));
        std::fflush(stdout);
        run_status = application.run();
    }

    return failures ? 1 : run_status;
}