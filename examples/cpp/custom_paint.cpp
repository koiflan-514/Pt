/*
 * Pt custom paint dashboard - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The C++ version keeps the same paint contract as the C example: the callback
 * is a pure function of dashboard_state, every clip push is paired by an RAII
 * guard, and input callbacks own state changes and request damage afterwards.
 * pt::paint_context is a borrowed handle; it is never stored past the call.
 */

#include <pt/pt.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr float kPanelPad = 18.0f;
constexpr float kPanelHeader = 48.0f;
constexpr float kPanelFooter = 34.0f;
constexpr float kTerminalFont = 13.0f;
constexpr float kGaugeWidth = 132.0f;
constexpr std::size_t kTerminalRows = 8;
constexpr std::string_view kFontFamily = "monospace";

constexpr std::array<std::string_view, kTerminalRows> kInitialLines = {
    "pt_paint_* draws into a borrowed paint context.",
    "Coordinates are widget-local logical pixels.",
    "Every push_clip has exactly one pop_clip.",
    "Paint is pure: state in, pixels out, no counters.",
    "One damage tile may be painted 0..N times.",
    "Caret and gauge state live outside the callback.",
    "Callbacks mutate state, then request_redraw().",
    "request_redraw_rect() damages only the gauge.",
};

struct dashboard_state {
    pt::window *window = nullptr;
    pt::widget panel;
    pt::widget status;
    float load = 0.42f;
    unsigned frame = 0;
    unsigned appended = 0;
    bool caret_on = true;
    std::array<std::string, kTerminalRows> lines;
};

/* Pops the clip in the destructor, so no early return can unbalance the stack.
 * The native paint context is borrowed only for the callback; this guard never
 * outlives it. */
class clip_guard {
  public:
    clip_guard(pt::paint_context &context, pt::rect area) : context_(context) {
        context_.push_clip(area);
    }
    ~clip_guard() { context_.pop_clip(); }
    clip_guard(const clip_guard &) = delete;
    clip_guard &operator=(const clip_guard &) = delete;

  private:
    pt::paint_context &context_;
};

bool intersects(pt::rect a, pt::rect b) {
    return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height &&
           a.y + a.height > b.y;
}

void reset_lines(dashboard_state &state, unsigned frame) {
    state.frame = frame;
    for (std::size_t row = 0; row < kTerminalRows; ++row) {
        state.lines[row] = std::string(kInitialLines[row]);
    }
}

void paint_dashboard(pt_widget *widget, pt_paint_context *handle, void *data) {
    (void)widget;
    auto &state = *static_cast<dashboard_state *>(data);
    pt::paint_context context(handle);
    const float width = context.width();
    const float height = context.height();
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    const std::optional<pt::rect> clip = context.clip();
    if (!clip) {
        return;
    }

    float ascent = kTerminalFont;
    float line_height = kTerminalFont * 1.45f;
    float cell_width = kTerminalFont * 0.62f;
    if (const auto metrics =
            context.metrics(kFontFamily, kTerminalFont, pt_font_weight_regular,
                            pt_font_slant_upright)) {
        ascent = metrics->ascent;
        line_height = std::fmax(metrics->line_height, kTerminalFont + 7.0f);
        if (metrics->cell_width > 0.0f) {
            cell_width = metrics->cell_width;
        }
    }

    const float gauge_x = width - kPanelPad - kGaugeWidth;
    const float terminal_right = gauge_x - 16.0f;
    const float terminal_width = std::fmax(0.0f, terminal_right - kPanelPad);
    const float text_x = kPanelPad + 30.0f;
    const float rows_top = kPanelHeader;
    const float rows_height = static_cast<float>(kTerminalRows) * line_height;

    if (intersects(*clip, {0.0f, 0.0f, width, kPanelHeader})) {
        context.fill_rounded_rect(kPanelPad, 15.0f, 10.0f, 10.0f, 5.0f, pt_rgb(0x9ECE6A));
        context.draw_text(kFontFamily, 18.0f, pt_font_weight_semibold,
                          pt_font_slant_upright, kPanelPad + 20.0f, 32.0f,
                          "pt_paint_* dashboard", pt_rgb(0xE6EDF7));
        const std::string frame = "frame " + std::to_string(state.frame);
        const float frame_width =
            context.measure_text(kFontFamily, 12.0f, pt_font_weight_regular,
                                 pt_font_slant_upright, frame);
        context.draw_text(kFontFamily, 12.0f, pt_font_weight_regular, pt_font_slant_upright,
                          width - kPanelPad - frame_width, 30.0f, frame, pt_rgb(0x6F7C8F));
        context.draw_line(kPanelPad, kPanelHeader - 1.0f, width - kPanelPad,
                          kPanelHeader - 1.0f, 1.0f, pt_rgb(0x24303F));
    }

    if (intersects(*clip, {kPanelPad, rows_top, terminal_width, rows_height})) {
        clip_guard guard(context, {kPanelPad, rows_top, terminal_width, rows_height});
        for (std::size_t row = 0; row < kTerminalRows; ++row) {
            const float row_y = rows_top + static_cast<float>(row) * line_height;
            if (row == kTerminalRows - 1) {
                context.fill_rect(kPanelPad, row_y, terminal_width, line_height,
                                  pt_rgb(0x172231));
            } else if ((row & 1u) == 0u) {
                context.fill_rect(kPanelPad, row_y, terminal_width, line_height,
                                  pt_rgb(0x0E141D));
            }
            const std::string number = std::to_string(row + 1);
            const std::string padded = number.size() < 2 ? "0" + number : number;
            context.draw_text(kFontFamily, 11.0f, pt_font_weight_regular,
                              pt_font_slant_upright, kPanelPad + 6.0f, row_y + ascent,
                              padded, pt_rgb(0x56657A));
            context.draw_text(kFontFamily, kTerminalFont, pt_font_weight_regular,
                              pt_font_slant_upright, text_x, row_y + ascent,
                              state.lines[row],
                              row == kTerminalRows - 1 ? pt_rgb(0xD7E2F2)
                                                       : pt_rgb(0xA9B7C9));
        }
        context.draw_line(kPanelPad + 26.0f, rows_top + 4.0f, kPanelPad + 26.0f,
                          rows_top + rows_height - 4.0f, 1.0f, pt_rgb(0x263241));
        if (state.caret_on) {
            const std::string &last = state.lines[kTerminalRows - 1];
            const float caret_x =
                text_x + static_cast<float>(last.size()) * cell_width + 1.0f;
            context.fill_rect(caret_x,
                              rows_top + static_cast<float>(kTerminalRows - 1) * line_height +
                                  3.0f,
                              std::fmax(2.0f, cell_width * 0.5f),
                              std::fmax(6.0f, line_height - 6.0f), pt_rgb(0x9ECE6A));
        }
    }

    if (intersects(*clip, {gauge_x, rows_top, kGaugeWidth, rows_height})) {
        clip_guard guard(context, {gauge_x, rows_top, kGaugeWidth, rows_height});
        context.fill_rounded_rect(gauge_x, rows_top, kGaugeWidth, rows_height, 12.0f,
                                  pt_rgb(0x111823));
        context.stroke_rect(gauge_x + 0.5f, rows_top + 0.5f, kGaugeWidth - 1.0f,
                            rows_height - 1.0f, 1.0f, pt_rgb(0x263241));
        context.draw_text(kFontFamily, 11.0f, pt_font_weight_semibold,
                          pt_font_slant_upright, gauge_x + 14.0f, rows_top + 24.0f, "LOAD",
                          pt_rgb(0x7A8798));

        const float bar_x = gauge_x + 14.0f;
        const float bar_y = rows_top + 34.0f;
        const float bar_width = kGaugeWidth - 28.0f;
        const float bar_height = std::fmax(24.0f, rows_height - 86.0f);
        context.fill_rounded_rect(bar_x, bar_y, bar_width, bar_height, 6.0f,
                                  pt_rgb(0x1B2432));
        const float fill = bar_height * std::fmin(std::fmax(state.load, 0.0f), 1.0f);
        const pt_color bar_color = state.load > 0.8f   ? pt_rgb(0xF7768E)
                                   : state.load > 0.55f ? pt_rgb(0xE0AF68)
                                                        : pt_rgb(0x9ECE6A);
        if (fill > 0.0f) {
            context.fill_rounded_rect(bar_x, bar_y + bar_height - fill, bar_width, fill,
                                      6.0f, bar_color);
        }
        for (int step = 1; step < 4; ++step) {
            const float grid_y = bar_y + bar_height * static_cast<float>(step) / 4.0f;
            context.draw_line(bar_x + 4.0f, grid_y, bar_x + bar_width - 4.0f, grid_y, 1.0f,
                              0xFFFFFF1Cu);
        }
        char percent[24];
        std::snprintf(percent, sizeof(percent), "%.0f%%",
                      static_cast<double>(state.load * 100.0f));
        const float percent_width =
            context.measure_text(kFontFamily, 13.0f, pt_font_weight_medium,
                                 pt_font_slant_upright, percent);
        context.draw_text(kFontFamily, 13.0f, pt_font_weight_medium,
                          pt_font_slant_upright, gauge_x + (kGaugeWidth - percent_width) * 0.5f,
                          rows_top + rows_height - 14.0f, percent, pt_rgb(0xD7E2F2));
    }

    const float footer_top = height - kPanelFooter;
    if (footer_top > rows_top + rows_height + 4.0f &&
        intersects(*clip, {0.0f, footer_top, width, kPanelFooter})) {
        context.draw_line(kPanelPad, footer_top, width - kPanelPad, footer_top, 1.0f,
                          pt_rgb(0x24303F));
        clip_guard guard(context, {kPanelPad, footer_top + 1.0f,
                                   width - 2.0f * kPanelPad, kPanelFooter - 2.0f});
        constexpr std::string_view marquee =
            "pure · idempotent · clip-balanced · damage-aware · ";
        const float marquee_width =
            context.measure_text(kFontFamily, 12.0f, pt_font_weight_regular,
                                 pt_font_slant_upright, marquee);
        if (marquee_width > 0.0f) {
            const float offset =
                std::fmod(static_cast<float>(state.frame) * 22.0f, marquee_width);
            context.draw_text(kFontFamily, 12.0f, pt_font_weight_regular,
                              pt_font_slant_upright, kPanelPad - offset, footer_top + 21.0f,
                              marquee, pt_rgb(0x6F7C8F));
            context.draw_text(kFontFamily, 12.0f, pt_font_weight_regular,
                              pt_font_slant_upright, kPanelPad - offset + marquee_width,
                              footer_top + 21.0f, marquee, pt_rgb(0x6F7C8F));
        }
    }
}

std::optional<uint32_t> snapshot_hash(const pt::window &window) {
    const int width = window.width();
    const int height = window.height();
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    std::vector<uint32_t> pixels(static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height));
    if (!window.paint_pixels(pixels, width, height)) {
        return std::nullopt;
    }
    uint32_t hash = 2166136261u;
    for (uint32_t pixel : pixels) {
        hash ^= pixel;
        hash *= 16777619u;
    }
    return hash;
}

void on_append(pt_widget *button, void *data) {
    (void)button;
    auto &state = *static_cast<dashboard_state *>(data);
    for (std::size_t row = 1; row < kTerminalRows; ++row) {
        state.lines[row - 1] = state.lines[row];
    }
    ++state.appended;
    ++state.frame;
    state.lines[kTerminalRows - 1] = "[" + std::to_string(state.appended) +
                                     "] appended in on_click; redraw is deferred";
    state.status.set_text("Line " + std::to_string(state.appended) +
                          " appended; requested a widget repaint.");
    state.panel.request_redraw();
}

void on_reset(pt_widget *button, void *data) {
    (void)button;
    auto &state = *static_cast<dashboard_state *>(data);
    state.appended = 0;
    reset_lines(state, 0);
    state.status.set_text("Log reset; the paint callback re-reads state.");
    state.panel.request_redraw();
}

void on_load(pt_widget *slider, float value, void *data) {
    (void)slider;
    auto &state = *static_cast<dashboard_state *>(data);
    state.load = std::fmin(std::fmax(value, 0.0f), 1.0f);
    const pt::rect bounds = state.panel.bounds();
    const float gauge_width = std::fmin(kGaugeWidth + 24.0f, bounds.width);
    state.panel.request_redraw_rect(bounds.width - gauge_width - kPanelPad, kPanelHeader,
                                    gauge_width,
                                    std::fmax(0.0f, bounds.height - kPanelHeader -
                                                        kPanelFooter));
    char status[96];
    std::snprintf(status, sizeof(status), "Load %.0f%%; damaged only the gauge column.",
                  static_cast<double>(state.load * 100.0f));
    state.status.set_text(status);
}

void on_caret_tick(pt_app *app, void *data) {
    auto &state = *static_cast<dashboard_state *>(data);
    state.caret_on = !state.caret_on;
    state.panel.request_redraw();
    pt_app_add_timeout(app, 530, on_caret_tick, &state);
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.custom_paint.cpp");
    if (!application.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt::last_error().c_str());
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x9ECE6A));

    pt::window window(application, "Pt Custom Paint - C++", 900, 580);
    if (!window.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt::last_error().c_str());
        return 1;
    }

    dashboard_state state;
    state.window = &window;
    reset_lines(state, 0);

    pt::widget root = window.root();
    root.set_padding(22.0f, 22.0f, 22.0f, 22.0f).set_gap(12.0f);

    pt::label(root.get(), "Custom paint")
        .set_font_size(26.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(root.get(),
              "A terminal-like panel painted with pt_paint_fill_*, drawLine and "
              "drawText. Tiles and repeats cannot change the result.")
        .set_font_size(13.0f)
        .set_foreground(pt_rgb(0x9AA7B8))
        .set_wrap(true);

    state.panel = pt::box(root.get(), pt_direction_column);
    state.panel.set_height(300.0f)
        .set_min_height(260.0f)
        .set_background(pt_rgb(0x0B1017))
        .set_radius(14.0f)
        .set_border_width(1.0f)
        .set_border_color(pt_rgb(0x24303F))
        .set_paint_callback(paint_dashboard, &state)
        .set_a11y_role(pt_a11y_role_panel)
        .set_a11y_name("Custom paint dashboard")
        .set_a11y_description(
            "A terminal log, a load gauge and a marquee drawn by a pure paint callback.");

    pt::widget controls = pt::row(root.get());
    controls.set_gap(10.0f).set_align_self(pt_align_center);
    pt::label(controls.get(), "Load")
        .set_font_size(13.0f)
        .set_foreground(pt_rgb(0x9AA7B8));
    pt::slider(controls.get())
        .set_grow(1.0f)
        .set_value(state.load)
        .on_value_change(on_load, &state)
        .set_tooltip("Damages only the gauge column through request_redraw_rect");
    pt::button(controls.get(), "Append line")
        .set_button_variant(pt_button_primary)
        .on_click(on_append, &state)
        .set_tooltip("Shift the log, mutate state and request a redraw");
    pt::button(controls.get(), "Reset log")
        .set_button_variant(pt_button_secondary)
        .on_click(on_reset, &state);

    state.status = pt::label(root.get(),
                             "Paint callback active; append a line or move the load.");
    state.status.set_font_size(12.0f)
        .set_foreground(pt_rgb(0x9ECE6A))
        .set_wrap(true);

    pt::label(root.get(),
              "The callback never mutates the tree. Clicks and the caret timer own all "
              "state changes, then ask for damage; RAII clip guards keep every push paired.")
        .set_font_size(12.0f)
        .set_foreground(pt_rgb(0x6F7C8F))
        .set_wrap(true);

    const std::optional<uint32_t> first = snapshot_hash(window);
    const std::optional<uint32_t> second = snapshot_hash(window);
    if (first && second) {
        std::printf("custom_paint (C++): offscreen %dx%d hash 0x%08X, repeat %s\n",
                    window.width(), window.height(), *first,
                    *first == *second ? "identical" : "CHANGED");
        std::fflush(stdout);
    } else {
        std::fprintf(stderr, "custom_paint (C++): offscreen snapshot unavailable\n");
    }

    window.show();
    const uint64_t caret_timer = application.add_timeout(530, on_caret_tick, &state);
    (void)caret_timer;
    return application.run();
}
