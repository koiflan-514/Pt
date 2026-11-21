/*
 * Pt primary selection example - C++23 bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Drag-select the left pane with the raw pointer callback, publish the range
 * from the button-release callback (where the event's serial is current), then
 * middle-click the right pane to read it back. Native strings returned by
 * pt_window_primary_get() are wrapped in std::unique_ptr with pt_free, and all
 * clip pushes are paired by an RAII guard.
 */

#include <pt/pt.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr float kPanePad = 16.0f;
constexpr float kPaneHeader = 44.0f;
constexpr float kPaneTopGap = 8.0f;
constexpr float kPaneFont = 13.0f;
constexpr float kPaneLine = 20.0f;
constexpr float kPaneGutter = 30.0f;
constexpr std::size_t kTargetLines = 9;
constexpr std::string_view kFontFamily = "monospace";

constexpr unsigned kButtonLeft = 0x110u;
constexpr unsigned kButtonMiddle = 0x112u;

constexpr std::array<std::string_view, 8> kSourceLines = {
    "Primary selection is the middle-click clipboard.",
    "Drag across these lines to select a range.",
    "On button release the range is published",
    "from inside the same pointer callback.",
    "The compositor serial therefore belongs",
    "to the release that produced the selection.",
    "",
    "Middle-click the paste pane to read it back.",
};

struct text_pos {
    int line = 0;
    int column = 0;
};

struct font_grid {
    float ascent = kPaneFont;
    float line_height = kPaneLine;
    float cell_width = kPaneFont * 0.62f;
};

struct selection_state {
    pt::window *window = nullptr;
    pt::widget source;
    pt::widget target;
    pt::widget status;
    text_pos anchor;
    text_pos head;
    bool dragging = false;
    bool has_selection = false;
    std::array<std::string, kTargetLines> target_lines;
    int target_count = 0;
    int highlight = -1;
    font_grid grid;
    bool grid_valid = false;
    unsigned publishes = 0;
    unsigned pastes = 0;
};

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

int position_compare(text_pos a, text_pos b) {
    if (a.line != b.line) {
        return a.line < b.line ? -1 : 1;
    }
    if (a.column != b.column) {
        return a.column < b.column ? -1 : 1;
    }
    return 0;
}

bool positions_equal(text_pos a, text_pos b) {
    return a.line == b.line && a.column == b.column;
}

int source_length(int line) {
    if (line < 0 || line >= static_cast<int>(kSourceLines.size())) {
        return 0;
    }
    return static_cast<int>(kSourceLines[static_cast<std::size_t>(line)].size());
}

int selection_line_count(const selection_state &state) {
    text_pos start = state.anchor;
    text_pos end = state.head;
    if (position_compare(start, end) > 0) {
        text_pos swap = start;
        start = end;
        end = swap;
    }
    return end.line - start.line + 1;
}

font_grid grid_from_context(pt::paint_context &context) {
    font_grid grid;
    if (const auto metrics = context.metrics(kFontFamily, kPaneFont, pt_font_weight_regular,
                                             pt_font_slant_upright)) {
        if (metrics->ascent > 0.0f) {
            grid.ascent = metrics->ascent;
        }
        if (metrics->line_height > 0.0f) {
            grid.line_height = std::fmax(metrics->line_height, kPaneFont + 2.0f);
        }
        if (metrics->cell_width > 0.0f) {
            grid.cell_width = metrics->cell_width;
        }
    }
    return grid;
}

text_pos position_at(selection_state &state, float window_x, float window_y) {
    const font_grid grid = state.grid_valid ? state.grid : font_grid{};
    const pt::rect bounds = state.source.bounds();
    const float top = bounds.y + kPaneHeader + kPaneTopGap;
    const float left = bounds.x + kPanePad + kPaneGutter;
    int line = static_cast<int>(std::floor((window_y - top) / grid.line_height));
    line = std::max(0, std::min(line, static_cast<int>(kSourceLines.size()) - 1));
    const int length = source_length(line);
    int column = static_cast<int>(std::lround((window_x - left) / grid.cell_width));
    column = std::max(0, std::min(column, length));
    return text_pos{.line = line, .column = column};
}

bool selection_on_line(const selection_state &state, int line, int length, int &from,
                       int &to) {
    if (!state.has_selection) {
        return false;
    }
    text_pos start = state.anchor;
    text_pos end = state.head;
    if (position_compare(start, end) > 0) {
        text_pos swap = start;
        start = end;
        end = swap;
    }
    if (line < start.line || line > end.line) {
        return false;
    }
    from = line == start.line ? start.column : 0;
    to = line == end.line ? end.column : length;
    from = std::max(0, std::min(from, length));
    to = std::max(0, std::min(to, length));
    if (to < from) {
        int swap = from;
        from = to;
        to = swap;
    }
    return to > from;
}

std::string selection_text(const selection_state &state) {
    text_pos start = state.anchor;
    text_pos end = state.head;
    if (position_compare(start, end) > 0) {
        text_pos swap = start;
        start = end;
        end = swap;
    }
    std::string text;
    for (int line = start.line; line <= end.line; ++line) {
        const std::string_view source = kSourceLines[static_cast<std::size_t>(line)];
        int from = line == start.line ? start.column : 0;
        int to = line == end.line ? end.column : static_cast<int>(source.size());
        from = std::max(0, std::min(from, static_cast<int>(source.size())));
        to = std::max(from, std::min(to, static_cast<int>(source.size())));
        if (line > start.line) {
            text.push_back('\n');
        }
        text.append(source.substr(static_cast<std::size_t>(from),
                                  static_cast<std::size_t>(to - from)));
    }
    return text;
}

std::string read_primary(const pt::window &window) {
    std::unique_ptr<char, decltype(&pt_free)> owned(window.primary(), &pt_free);
    return owned ? std::string(owned.get()) : std::string();
}

void set_status(selection_state &state, std::string_view text) {
    state.status.set_text(text);
}

void append_target_line(selection_state &state, std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (state.target_count == static_cast<int>(kTargetLines)) {
        for (std::size_t index = 1; index < kTargetLines; ++index) {
            state.target_lines[index - 1] = std::move(state.target_lines[index]);
        }
        --state.target_count;
    }
    std::string line = "> ";
    line.append(text.substr(0, 120));
    state.target_lines[static_cast<std::size_t>(state.target_count)] = std::move(line);
    ++state.target_count;
}

void paste_primary(selection_state &state) {
    const std::string primary = read_primary(*state.window);
    if (primary.empty()) {
        set_status(state, "Primary is empty. Drag over the source pane first.");
        return;
    }
    unsigned lines = 0;
    std::size_t cursor = 0;
    while (cursor < primary.size()) {
        const std::size_t newline = primary.find('\n', cursor);
        const std::size_t end = newline == std::string::npos ? primary.size() : newline;
        if (end > cursor) {
            append_target_line(state, std::string_view(primary).substr(cursor, end - cursor));
            ++lines;
        }
        if (newline == std::string::npos) {
            break;
        }
        cursor = newline + 1;
    }
    ++state.pastes;
    state.highlight = state.target_count - 1;
    char status[192];
    std::snprintf(status, sizeof(status),
                  "Read %zu bytes from primary in this pointer event; appended %u line(s), "
                  "paste #%u.",
                  primary.size(), lines, state.pastes);
    set_status(state, status);
    state.target.request_redraw();
}

bool on_pointer(pt_window *window, const pt_pointer_event *event, void *data) {
    (void)window;
    auto &state = *static_cast<selection_state *>(data);
    if (!event) {
        return false;
    }
    const pt::rect source_bounds = state.source.bounds();
    const pt::rect target_bounds = state.target.bounds();
    const bool over_source = event->x >= source_bounds.x &&
                             event->x < source_bounds.x + source_bounds.width &&
                             event->y >= source_bounds.y &&
                             event->y < source_bounds.y + source_bounds.height;
    const bool over_target = event->x >= target_bounds.x &&
                             event->x < target_bounds.x + target_bounds.width &&
                             event->y >= target_bounds.y &&
                             event->y < target_bounds.y + target_bounds.height;

    switch (event->phase) {
    case pt_pointer_phase_down:
        if (event->button == kButtonLeft && over_source) {
            state.anchor = position_at(state, event->x, event->y);
            state.head = state.anchor;
            state.dragging = true;
            state.has_selection = false;
            set_status(state, "Selecting; release inside the window to publish the range.");
            state.source.request_redraw();
            return true;
        }
        if (event->button == kButtonMiddle && over_target) {
            paste_primary(state);
            return true;
        }
        break;
    case pt_pointer_phase_motion:
        if (state.dragging) {
            const text_pos next = position_at(state, event->x, event->y);
            if (!positions_equal(next, state.head)) {
                state.head = next;
                state.has_selection = !positions_equal(state.anchor, state.head);
                state.source.request_redraw();
            }
        }
        break;
    case pt_pointer_phase_up:
        if (state.dragging && event->button == kButtonLeft) {
            state.head = position_at(state, event->x, event->y);
            state.dragging = false;
            state.has_selection = !positions_equal(state.anchor, state.head);
            if (!state.has_selection) {
                set_status(state, "Click without drag selects nothing; primary is unchanged.");
                state.source.request_redraw();
                return true;
            }
            const std::string payload = selection_text(state);
            if (payload.empty()) {
                set_status(state, "Selection contains no text; primary is unchanged.");
                state.source.request_redraw();
                return true;
            }
            /* Publish here, not from a timer: the Wayland set_selection serial
             * belongs to this button release. */
            const bool published = state.window->set_primary(payload.c_str());
            const std::string readback = published ? read_primary(*state.window) : std::string();
            if (published) {
                ++state.publishes;
                char status[192];
                std::snprintf(status, sizeof(status),
                              "Published %zu bytes from %d line(s); read back %zu bytes, "
                              "publish #%u.",
                              payload.size(), selection_line_count(state), readback.size(),
                              state.publishes);
                set_status(state, status);
            } else {
                set_status(state, "The primary selection backend rejected the set.");
            }
            state.source.request_redraw();
            return true;
        }
        break;
    case pt_pointer_phase_cancel:
        if (state.dragging) {
            state.dragging = false;
            set_status(state, "Selection cancelled; primary is unchanged.");
            state.source.request_redraw();
        }
        break;
    case pt_pointer_phase_enter:
    case pt_pointer_phase_leave:
    case pt_pointer_phase_axis:
        break;
    }
    return false;
}

void paint_source(pt_widget *widget, pt_paint_context *handle, void *data) {
    (void)widget;
    auto &state = *static_cast<selection_state *>(data);
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
    const font_grid grid = grid_from_context(context);
    state.grid = grid;
    state.grid_valid = true;

    const float top = kPaneHeader + kPaneTopGap;
    const float text_x = kPanePad + kPaneGutter;
    const float body_height = std::fmax(0.0f, height - top - kPanePad);

    context.draw_text(kFontFamily, 12.0f, pt_font_weight_semibold, pt_font_slant_upright,
                      kPanePad, 27.0f, "PRIMARY SOURCE", pt_rgb(0x7AA2F7));
    constexpr std::string_view hint = "drag to select";
    const float hint_width = context.measure_text(kFontFamily, 11.0f,
                                                   pt_font_weight_regular,
                                                   pt_font_slant_upright, hint);
    context.draw_text(kFontFamily, 11.0f, pt_font_weight_regular, pt_font_slant_upright,
                      width - kPanePad - hint_width, 26.0f, hint, pt_rgb(0x6F7C8F));
    context.draw_line(kPanePad, kPaneHeader - 1.0f, width - kPanePad, kPaneHeader - 1.0f,
                      1.0f, pt_rgb(0x263241));

    clip_guard guard(context, {kPanePad, top, width - 2.0f * kPanePad, body_height});
    for (std::size_t line = 0; line < kSourceLines.size(); ++line) {
        const float row_y = top + static_cast<float>(line) * grid.line_height;
        const int length = source_length(static_cast<int>(line));
        int from = 0;
        int to = 0;
        if (selection_on_line(state, static_cast<int>(line), length, from, to)) {
            context.fill_rect(text_x + static_cast<float>(from) * grid.cell_width, row_y,
                              static_cast<float>(to - from) * grid.cell_width,
                              grid.line_height, pt_rgb(0x2B4A78));
        }
        char number[16];
        std::snprintf(number, sizeof(number), "%02zu", line + 1);
        context.draw_text(kFontFamily, 11.0f, pt_font_weight_regular,
                          pt_font_slant_upright, kPanePad + 4.0f, row_y + grid.ascent,
                          number, pt_rgb(0x56657A));
        context.draw_text(kFontFamily, kPaneFont, pt_font_weight_regular,
                          pt_font_slant_upright, text_x, row_y + grid.ascent,
                          kSourceLines[line], pt_rgb(0xC3D0E1));
    }
    context.draw_line(kPanePad + 24.0f, top + 4.0f, kPanePad + 24.0f,
                      top + body_height - 4.0f, 1.0f, pt_rgb(0x263241));
}

void paint_target(pt_widget *widget, pt_paint_context *handle, void *data) {
    (void)widget;
    auto &state = *static_cast<selection_state *>(data);
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
    const font_grid grid = grid_from_context(context);
    const float top = kPaneHeader + kPaneTopGap;
    const float body_height = std::fmax(0.0f, height - top - kPanePad);

    context.draw_text(kFontFamily, 12.0f, pt_font_weight_semibold, pt_font_slant_upright,
                      kPanePad, 27.0f, "PASTE TARGET", pt_rgb(0x9ECE6A));
    constexpr std::string_view hint = "middle-click to paste";
    const float hint_width = context.measure_text(kFontFamily, 11.0f,
                                                   pt_font_weight_regular,
                                                   pt_font_slant_upright, hint);
    context.draw_text(kFontFamily, 11.0f, pt_font_weight_regular, pt_font_slant_upright,
                      width - kPanePad - hint_width, 26.0f, hint, pt_rgb(0x6F7C8F));
    context.draw_line(kPanePad, kPaneHeader - 1.0f, width - kPanePad, kPaneHeader - 1.0f,
                      1.0f, pt_rgb(0x263241));

    clip_guard guard(context, {kPanePad, top, width - 2.0f * kPanePad, body_height});
    if (state.target_count == 0) {
        context.draw_text(kFontFamily, kPaneFont, pt_font_weight_regular,
                          pt_font_slant_upright, kPanePad, top + grid.ascent,
                          "Middle-click here after selecting text.", pt_rgb(0x6F7C8F));
    } else {
        for (int line = 0; line < state.target_count; ++line) {
            const float row_y = top + static_cast<float>(line) * grid.line_height;
            if (line == state.highlight) {
                context.fill_rect(kPanePad, row_y, width - 2.0f * kPanePad,
                                  grid.line_height, pt_rgb(0x1D3524));
            }
            context.draw_text(kFontFamily, kPaneFont, pt_font_weight_regular,
                              pt_font_slant_upright, kPanePad, row_y + grid.ascent,
                              state.target_lines[static_cast<std::size_t>(line)],
                              line == state.highlight ? pt_rgb(0xD7F5DC)
                                                      : pt_rgb(0xC3D0E1));
        }
    }
}

void on_read_primary(pt_widget *button, void *data) {
    (void)button;
    auto &state = *static_cast<selection_state *>(data);
    const std::string text = read_primary(*state.window);
    if (text.empty()) {
        set_status(state, "Primary is empty.");
        return;
    }
    std::string preview = text.substr(0, 44);
    for (char &character : preview) {
        if (character == '\n') {
            character = ' ';
        }
    }
    char status[192];
    std::snprintf(status, sizeof(status), "Primary holds %zu bytes: \"%s%s\".", text.size(),
                  preview.c_str(), text.size() > 44 ? "…" : "");
    set_status(state, status);
}

void on_clear_target(pt_widget *button, void *data) {
    (void)button;
    auto &state = *static_cast<selection_state *>(data);
    state.target_count = 0;
    state.highlight = -1;
    set_status(state, "Paste target cleared; primary is untouched.");
    state.target.request_redraw();
}

} // namespace

int main() {
    pt::app application("dev.pt.examples.primary_selection.cpp");
    if (!application.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt::last_error().c_str());
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_dark);
    application.set_accent_color(pt_rgb(0x7AA2F7));

    pt::window window(application, "Pt Primary Selection - C++", 980, 640);
    if (!window.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt::last_error().c_str());
        return 1;
    }

    selection_state state;
    state.window = &window;
    window.on_pointer(on_pointer, &state);

    pt::widget root = window.root();
    root.set_padding(22.0f, 22.0f, 22.0f, 22.0f).set_gap(12.0f);

    pt::label(root.get(), "Primary selection")
        .set_font_size(26.0f)
        .set_font_weight(pt_font_weight_semibold);
    pt::label(root.get(),
              "Drag over the left pane, release, then middle-click the right pane. The set "
              "happens inside the button-release callback, so the serial belongs to the "
              "event that produced the selection.")
        .set_font_size(13.0f)
        .set_foreground(pt_rgb(0x9AA7B8))
        .set_wrap(true);

    pt::widget panes = pt::row(root.get());
    panes.set_gap(14.0f).set_grow(1.0f);

    state.source = pt::box(panes.get(), pt_direction_column);
    state.source.set_grow(1.0f)
        .set_min_height(300.0f)
        .set_background(pt_rgb(0x0D131C))
        .set_radius(14.0f)
        .set_border_width(1.0f)
        .set_border_color(pt_rgb(0x263241))
        .set_paint_callback(paint_source, &state)
        .set_a11y_role(pt_a11y_role_text)
        .set_a11y_name("Primary selection source")
        .set_a11y_description(
            "Eight log lines. Drag across them to select and publish a primary selection.");

    state.target = pt::box(panes.get(), pt_direction_column);
    state.target.set_grow(1.0f)
        .set_min_height(300.0f)
        .set_background(pt_rgb(0x0D131C))
        .set_radius(14.0f)
        .set_border_width(1.0f)
        .set_border_color(pt_rgb(0x263241))
        .set_paint_callback(paint_target, &state)
        .set_a11y_role(pt_a11y_role_text_frame)
        .set_a11y_name("Primary paste target")
        .set_a11y_description("Middle-click to append the current primary selection.");

    pt::widget actions = pt::row(root.get());
    actions.set_gap(10.0f).set_align_self(pt_align_center);
    pt::button(actions.get(), "Read primary")
        .set_button_variant(pt_button_secondary)
        .on_click(on_read_primary, &state)
        .set_tooltip("Calls pt_window_primary_get() and frees the result with unique_ptr");
    pt::button(actions.get(), "Clear paste target")
        .set_button_variant(pt_button_ghost)
        .on_click(on_clear_target, &state);

    state.status = pt::label(actions.get(),
                             "Drag across the source lines to make a selection.");
    state.status.set_font_size(12.0f)
        .set_foreground(pt_rgb(0x9ECE6A))
        .set_wrap(true)
        .set_grow(1.0f);

    pt::label(root.get(),
              "pt_window_primary_set() copies the string; pt_window_primary_get() returns "
              "caller-owned UTF-8 released with pt_free(). Without "
              "wp_primary_selection_unstable_v1 (or under PT_HEADLESS) both fall back to the "
              "in-process buffer, so the example still runs.")
        .set_font_size(12.0f)
        .set_foreground(pt_rgb(0x6F7C8F))
        .set_wrap(true);

    window.show();
    return application.run();
}
