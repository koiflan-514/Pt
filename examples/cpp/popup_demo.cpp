/*
 * Pt popup demo - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * An application-owned popup surface with custom content, runtime placement
 * changes and a dismiss callback. The popup adopts the detached content card;
 * the example never touches it after the popup is created.
 */

#include <pt/pt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct popup_state {
    pt::popup *popup = nullptr;
    pt::widget trigger;
    pt::widget placement_button;
    pt::widget status;
    pt_popup_placement placement = pt_popup_placement_below;
    unsigned dismissals = 0;
    unsigned shows = 0;
};

const char *placement_name(pt_popup_placement placement) {
    switch (placement) {
    case pt_popup_placement_below:
        return "below";
    case pt_popup_placement_above:
        return "above";
    case pt_popup_placement_right:
        return "right";
    case pt_popup_placement_left:
        return "left";
    }
    return "?";
}

void refresh_status(popup_state &state, const char *what) {
    char text[224];
    std::snprintf(text, sizeof(text), "%s · placement %s · open %s · shown %u, dismissed %u",
                  what, placement_name(state.placement),
                  state.popup->is_open() ? "yes" : "no", state.shows, state.dismissals);
    state.status.set_text(text);
}

void on_dismiss(pt_popup *popup, void *data) {
    auto *ctx = static_cast<popup_state *>(data);
    ++ctx->dismissals;
    std::printf("popup-demo-cpp: dismiss #%u (is_open=%d)\n", ctx->dismissals,
                pt_popup_is_open(popup) ? 1 : 0);
    std::fflush(stdout);
    refresh_status(*ctx, "Dismiss callback fired after the popup closed");
}

pt::rect trigger_rect(const pt::widget &trigger) {
    return trigger.bounds();
}

void open_popup(popup_state &state) {
    pt::rect area = trigger_rect(state.trigger);
    if (state.popup->show(area)) {
        ++state.shows;
        refresh_status(state, "Popup opened with the current placement");
    } else {
        char text[192];
        std::snprintf(text, sizeof(text), "Could not open the popup: %s", pt_get_last_error());
        state.status.set_text(text);
    }
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.popup_demo.cpp");
    if (!application.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));

    pt::window window(application, "Pt Popup Demo - C++", 620, 460);
    if (!window.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }

    popup_state state;
    pt::widget root = window.root();
    root.set_padding(26.0f, 26.0f, 26.0f, 26.0f).set_gap(14.0f);

    pt::label(root, "POPUP  ·  APPLICATION-OWNED SURFACE  ·  CPP")
        .set_font_size(11.0f)
        .set_font_weight(pt_font_weight_semibold)
        .set_foreground(pt_rgb(0x7AA2F7));
    pt::label(root, "Custom content, real placement")
        .set_font_size(28.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(root,
              "The popup is a Pt surface built from ordinary widgets. The anchor is the trigger "
              "button rectangle; the placement flips and slides to stay inside the compositor "
              "work area. Escape, an outside click and Dismiss all run the same callback.")
        .set_font_size(13.0f)
        .set_foreground(pt_rgb(0x9AA7B8))
        .set_wrap(true);

    pt::widget card = pt::card(root);
    card.set_padding(18.0f, 18.0f, 18.0f, 18.0f).set_gap(12.0f);

    pt::widget actions = pt::row(card.get());
    actions.set_gap(10.0f).set_align_self(pt_align_center);
    state.trigger = pt::button(actions.get(), "Open popup");
    state.trigger.set_button_variant(pt_button_primary)
        .set_tooltip("Anchors the popup to this button")
        .on_click([](pt_widget *, void *data) { open_popup(*static_cast<popup_state *>(data)); },
                  &state);
    pt::button(actions.get(), "Toggle")
        .set_button_variant(pt_button_secondary)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<popup_state *>(data);
            if (ctx->popup->is_open()) {
                ctx->popup->dismiss();
                refresh_status(*ctx, "pt_popup_dismiss called");
            } else {
                open_popup(*ctx);
            }
        }, &state);
    pt::button(actions.get(), "Reposition")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<popup_state *>(data);
            pt::rect area = trigger_rect(ctx->trigger);
            area.x += 24.0f;
            area.y += 18.0f;
            if (ctx->popup->reposition(area)) {
                refresh_status(*ctx, "pt_popup_reposition moved the open popup");
            } else {
                refresh_status(*ctx, "Popup is closed; reposition is a no-op");
            }
        }, &state);
    state.placement_button = pt::button(actions.get(), placement_name(state.placement));
    state.placement_button.set_button_variant(pt_button_soft)
        .set_tooltip("Cycle below → above → right → left")
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<popup_state *>(data);
            switch (ctx->popup->placement()) {
            case pt_popup_placement_below:
                ctx->placement = pt_popup_placement_above;
                break;
            case pt_popup_placement_above:
                ctx->placement = pt_popup_placement_right;
                break;
            case pt_popup_placement_right:
                ctx->placement = pt_popup_placement_left;
                break;
            case pt_popup_placement_left:
                ctx->placement = pt_popup_placement_below;
                break;
            }
            ctx->popup->set_placement(ctx->placement);
            ctx->placement_button.set_text(placement_name(ctx->placement));
            if (ctx->popup->is_open()) {
                ctx->popup->reposition(trigger_rect(ctx->trigger));
            }
            refresh_status(*ctx, "Placement preference changed");
        }, &state);

    /* Detached content: the popup adopts it and owns it from then on. */
    pt::widget content = pt::card(nullptr);
    content.set_padding(16.0f, 16.0f, 16.0f, 16.0f).set_gap(10.0f).set_radius(12.0f)
        .set_min_width(260.0f).set_background(pt_rgb(0x161D28))
        .set_border_width(1.0f).set_border_color(pt_rgb(0x30415C))
        .set_a11y_name("Popup content");
    pt::label(content.get(), "Popup content")
        .set_font_size(16.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(content.get(),
              "This card lives in the popup tree. Escape or a click outside dismisses it and "
              "fires the callback below.")
        .set_font_size(12.0f)
        .set_foreground(pt_rgb(0x9AA7B8))
        .set_wrap(true);
    pt::widget content_actions = pt::row(content.get());
    content_actions.set_gap(8.0f);
    pt::button(content_actions.get(), "Ping")
        .set_button_variant(pt_button_secondary)
        .on_click([](pt_widget *, void *data) {
            refresh_status(*static_cast<popup_state *>(data), "Ping from the popup content");
        }, &state);
    pt::button(content_actions.get(), "Dismiss")
        .set_button_variant(pt_button_ghost)
        .on_click([](pt_widget *, void *data) {
            auto *ctx = static_cast<popup_state *>(data);
            ctx->popup->dismiss();
            refresh_status(*ctx, "Close button inside the popup");
        }, &state);

    pt::popup popup(window, content);
    if (!popup.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    state.popup = &popup;
    popup.on_dismiss(on_dismiss, &state).set_placement(state.placement);

    state.status = pt::label(root, "");
    state.status.set_font_size(12.0f).set_foreground(pt_rgb(0x9ECE6A)).set_wrap(true);
    refresh_status(state, "Ready; the popup starts closed");

    pt::label(root,
              "widget::bounds() reports window logical coordinates; the popup anchor is the same "
              "rectangle. Destroying the popup or its window does not fire the dismiss callback.")
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
    pt_widget_focus(state.trigger.get());

    const char *headless = std::getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && std::strcmp(headless, "0") != 0) {
        open_popup(state);
        std::printf("popup-demo-cpp: open=%d placement=%s content=%p\n",
                    popup.is_open() ? 1 : 0, placement_name(popup.placement()),
                    (void *)popup.content().get());
        popup.set_placement(pt_popup_placement_above);
        state.placement = pt_popup_placement_above;
        popup.reposition(trigger_rect(state.trigger));
        popup.dismiss();
        open_popup(state);
        popup.dismiss();
        std::printf("popup-demo-cpp: after dismiss open=%d dismissals=%u\n",
                    popup.is_open() ? 1 : 0, state.dismissals);
        std::fflush(stdout);
        status = application.run_for(250);
    } else {
        status = application.run();
    }
    popup.reset();
    return status;
}