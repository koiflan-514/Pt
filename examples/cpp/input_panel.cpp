/*
 * Pt input panel example - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * 1.3 adds the text-input-v3 v2 desired-state requests to
 * bindings/cpp/pt/pt.hpp:
 *
 *   [[nodiscard]] bool window::show_input_panel() const;
 *   [[nodiscard]] bool window::hide_input_panel() const;
 *
 * Both return true only when the recorded desired state changed, matching
 * pt_window_show_input_panel / pt_window_hide_input_panel in
 * include/pt/custom.h.
 */

#include <pt/pt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct panel_state {
    pt::window *window = nullptr;
    pt::widget panel_status;
    pt::widget entry_status;
    pt::widget preedit_status;
    bool desired_visible = false;
    int show_changes = 0;
    int hide_changes = 0;
    char preedit[128] = {};
    char committed[256] = {};
};

void show_panel(panel_state *state) {
    const bool changed = state->window->show_input_panel();
    if (changed) {
        state->desired_visible = true;
        ++state->show_changes;
    }
    char text[192];
    std::snprintf(text, sizeof(text),
                  "Panel: show %s (desired %s; the compositor still decides whether an OSK "
                  "appears)",
                  changed ? "recorded" : "already requested",
                  state->desired_visible ? "visible" : "hidden");
    state->panel_status.set_text(text);
    std::printf("input_panel_cpp: show changed=%d\n", changed ? 1 : 0);
    std::fflush(stdout);
}

void hide_panel(panel_state *state) {
    const bool changed = state->window->hide_input_panel();
    if (changed) {
        state->desired_visible = false;
        ++state->hide_changes;
    }
    char text[192];
    std::snprintf(text, sizeof(text),
                  "Panel: hide %s (desired %s; repeated calls never duplicate the request)",
                  changed ? "recorded" : "already requested",
                  state->desired_visible ? "visible" : "hidden");
    state->panel_status.set_text(text);
    std::printf("input_panel_cpp: hide changed=%d\n", changed ? 1 : 0);
    std::fflush(stdout);
}

void on_show(pt_widget *, void *data) { show_panel(static_cast<panel_state *>(data)); }
void on_hide(pt_widget *, void *data) { hide_panel(static_cast<panel_state *>(data)); }

void on_entry_change(pt_widget *, const char *text, void *data) {
    auto *state = static_cast<panel_state *>(data);
    char status[256];
    std::snprintf(status, sizeof(status), "Entry text: \"%s\"", text ? text : "");
    state->entry_status.set_text(status);
}

void on_quit(pt_widget *, void *data) {
    static_cast<pt::app *>(data)->quit();
}

/* Custom-surface callbacks: active only while no entry holds editable focus.
 * preedit_hint ranges are presentation metadata for Pt-drawn text and never
 * reach this ABI. */
void on_ime_commit(pt_window *, const char *text, void *data) {
    auto *state = static_cast<panel_state *>(data);
    const char *insert = text ? text : "";
    std::strncat(state->committed, insert,
                 sizeof(state->committed) - std::strlen(state->committed) - 1);
    state->preedit[0] = '\0';
    char status[320];
    std::snprintf(status, sizeof(status), "Committed \"%s\"; custom buffer \"%s\"", insert,
                  state->committed);
    state->preedit_status.set_text(status);
    std::printf("input_panel_cpp: commit \"%s\"\n", insert);
    std::fflush(stdout);
}

void on_ime_preedit(pt_window *, const char *text, int cursor_begin, int cursor_end, void *data) {
    auto *state = static_cast<panel_state *>(data);
    std::snprintf(state->preedit, sizeof(state->preedit), "%s", text ? text : "");
    char status[256];
    if (text && *text) {
        std::snprintf(status, sizeof(status), "Preedit \"%s\" cursor %d..%d (app-drawn)",
                      text, cursor_begin, cursor_end);
    } else {
        std::snprintf(status, sizeof(status),
                      "Preedit: (none) - an empty preedit ends the composition");
    }
    state->preedit_status.set_text(status);
    std::printf("input_panel_cpp: preedit \"%s\"\n", text ? text : "");
    std::fflush(stdout);
}

} // namespace

int main() {
    pt::app application("pt.example.input_panel.cpp");
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));

    pt::window window(application, "Pt Input Panel - C++", 680, 540);
    if (!window.valid()) {
        return 1;
    }

    panel_state state;
    state.window = &window;
    pt::widget root = window.root();
    root.set_padding(26.0f, 26.0f, 26.0f, 26.0f).set_gap(14.0f);

    pt::label(root, "CPP BINDING  -  INPUT PANEL")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x7AA2F7));
    pt::label(root, "Show and hide the on-screen input panel")
        .set_font_size(28.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(root,
              "The two calls are text-input-v3 v2 hints. Their bool result is the local desired "
              "state transition, not proof that an OSK appeared.")
        .set_font_size(13.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0x9AA7B8));

    pt::widget card = pt::card(root);
    card.set_padding(18.0f, 18.0f, 18.0f, 18.0f).set_gap(12.0f);

    pt::widget entry = pt::entry(card, "Type here - a real IME shows hinted underlines");
    entry.on_change(on_entry_change, &state);
    state.entry_status = pt::label(card, "Entry text: \"\"");
    state.entry_status.set_font_size(12.0f).set_wrap(true).set_foreground(pt_rgb(0x9AA7B8));

    pt::widget buttons = pt::row(card);
    buttons.set_gap(10.0f);
    pt::button(buttons, "Show input panel")
        .set_button_variant(pt_button_primary)
        .set_tooltip("zwp_text_input_v3.show_input_panel")
        .on_click(on_show, &state);
    pt::button(buttons, "Hide input panel")
        .set_button_variant(pt_button_secondary)
        .set_tooltip("zwp_text_input_v3.hide_input_panel")
        .on_click(on_hide, &state);

    state.panel_status = pt::label(card, "Panel: hidden (initial desired state)");
    state.panel_status.set_font_size(12.0f).set_wrap(true).set_foreground(pt_rgb(0x9ECE6A));

    state.preedit_status = pt::label(card, "Preedit: (none)");
    state.preedit_status.set_font_size(12.0f).set_wrap(true).set_foreground(pt_rgb(0xE0AF68));

    /* The custom surface stays enabled; the entry's own IME wins while it has
     * editable focus. */
    window.set_ime_callbacks(on_ime_commit, on_ime_preedit, &state)
        .set_ime_enabled(true)
        .set_ime_cursor_rect(24, 320, 2, 20);

    pt::label(root,
              "Headless runs record the desired state only. preedit_hint ranges are drawn by Pt "
              "inside entries and are absent from the custom callback ABI.")
        .set_font_size(12.0f)
        .set_wrap(true)
        .set_foreground(pt_rgb(0x6F7C8F));

    pt::widget quit = pt::button(root, "Quit");
    quit.set_button_variant(pt_button_ghost).set_align_self(pt_align_start).on_click(on_quit, &application);

    window.show();
    window.focus(entry);

    if (const char *headless = std::getenv("PT_HEADLESS");
        headless && *headless && std::strcmp(headless, "0") != 0) {
        show_panel(&state);
        show_panel(&state);
        hide_panel(&state);
        hide_panel(&state);
        std::printf("input_panel_cpp: show_changes=%d hide_changes=%d\n", state.show_changes,
                    state.hide_changes);
        return application.run_for(250);
    }
    return application.run();
}