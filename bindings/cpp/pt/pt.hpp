/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Header-only C++23 conveniences over the flat C API. Widgets and windows are
 * non-owning handles: pt::app owns the native object graph. Do not destroy a
 * widget through the C API while a pt::widget refers to it.
 */

#ifndef pt_cpp_hpp
#define pt_cpp_hpp

#include <pt/pt.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pt {

inline std::string copy_string(std::string_view text) {
    return std::string(text);
}

class app;
class window;
class widget;
class menu;
class dialog;
class toast;

using tree_node = pt_tree_node;
using stylus_event = pt_stylus_event;
using drag_data = pt_drag_data;
using drop_stream_state = pt_drop_stream_state;
using combo_mode = pt_combobox_mode;

struct rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct a11y_text_info {
    std::string text;
    uint32_t caret = 0;
    uint32_t selection_start = 0;
    uint32_t selection_end = 0;
};

struct sort_key {
    uint32_t column = 0;
    pt_sort_direction direction = pt_sort_none;
};

class app {
  public:
    explicit app(std::string_view app_id) {
        std::string id = copy_string(app_id);
        handle_ = pt_app_create(id.c_str());
    }
    ~app() {
        if (handle_) {
            pt_app_destroy(handle_);
        }
    }
    app(const app &) = delete;
    app &operator=(const app &) = delete;
    app(app &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    app &operator=(app &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                pt_app_destroy(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] pt_app *get() const { return handle_; }
    operator pt_app *() const { return handle_; }

    int run() const { return pt_app_run(handle_); }
    int run_for(unsigned timeout_ms) const { return pt_app_run_for(handle_, timeout_ms); }
    void quit() const { pt_app_quit(handle_); }
    [[nodiscard]] bool running() const { return pt_app_is_running(handle_); }
    [[nodiscard]] uint64_t add_timeout(uint32_t milliseconds, pt_timer_callback callback,
                                       void *data = nullptr) const {
        return pt_app_add_timeout(handle_, milliseconds, callback, data);
    }
    void cancel_timeout(uint64_t timer_id) const {
        pt_app_cancel_timeout(handle_, timer_id);
    }
    void set_color_scheme(pt_color_scheme scheme) const {
        pt_app_set_color_scheme(handle_, scheme);
    }
    [[nodiscard]] pt_color_scheme color_scheme() const {
        return pt_app_get_color_scheme(handle_);
    }
    void set_quit_on_last_window_close(bool enabled) const {
        pt_app_set_quit_on_last_window_close(handle_, enabled);
    }
    void set_accent_color(pt_color color) const { pt_app_set_accent_color(handle_, color); }
    [[nodiscard]] pt_color theme_color(pt_theme_token token) const {
        return pt_app_get_theme_color(handle_, token);
    }
    void set_theme_color(pt_theme_token token, pt_color color) const {
        pt_app_set_theme_color(handle_, token, color);
    }
    [[nodiscard]] float theme_metric(pt_theme_metric metric) const {
        return pt_app_get_theme_metric(handle_, metric);
    }
    void set_theme_metric(pt_theme_metric metric, float value) const {
        pt_app_set_theme_metric(handle_, metric, value);
    }
    void set_font_family(std::string_view family) const {
        std::string value = copy_string(family);
        pt_app_set_font_family(handle_, value.c_str());
    }
    void set_reduced_motion(bool reduced) const {
        pt_app_set_reduced_motion(handle_, reduced);
    }
    [[nodiscard]] bool reduced_motion() const { return pt_app_get_reduced_motion(handle_); }
    void on_a11y_event(pt_a11y_event_callback callback, void *data = nullptr) const {
        pt_app_set_a11y_callback(handle_, callback, data);
    }
    bool enable_a11y() const { return pt_app_enable_a11y(handle_); }
    void disable_a11y() const { pt_app_disable_a11y(handle_); }
    [[nodiscard]] pt_a11y_status a11y_status() const {
        return pt_app_get_a11y_status(handle_);
    }

  private:
    pt_app *handle_ = nullptr;
};

inline std::string last_error() {
    const char *message = pt_get_last_error();
    return message ? message : "";
}

/* Non-owning handle over an asynchronous drop read. Streams are opened by
 * drop_offer::read and released by Pt once their final state callback returns.
 * Every accessor tolerates a null handle, so a failed read behaves like an
 * inactive empty stream. */
class drop_stream {
  public:
    drop_stream() = default;
    explicit drop_stream(pt_drop_stream *handle) : handle_(handle) {}

    [[nodiscard]] pt_drop_stream *get() const { return handle_; }
    operator pt_drop_stream *() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }

    void cancel() const { pt_drop_stream_cancel(handle_); }
    [[nodiscard]] std::string_view mime() const {
        const char *value = pt_drop_stream_mime(handle_);
        return value ? std::string_view(value) : std::string_view();
    }
    [[nodiscard]] size_t size() const { return pt_drop_stream_size(handle_); }
    [[nodiscard]] bool active() const { return pt_drop_stream_active(handle_); }

  private:
    pt_drop_stream *handle_ = nullptr;
};

/* Non-owning handle over an incoming drop offer. An offer is only valid while
 * its enter_offer or drop_offer callback runs; streams read from it outlive the
 * callback. Accessors are null-safe and the MIME views point into storage owned
 * by the offer. */
class drop_offer {
  public:
    drop_offer() = default;
    explicit drop_offer(pt_drop_offer *handle) : handle_(handle) {}

    [[nodiscard]] pt_drop_offer *get() const { return handle_; }
    operator pt_drop_offer *() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }

    [[nodiscard]] uint32_t mime_count() const { return pt_drop_offer_mime_count(handle_); }
    [[nodiscard]] std::string_view mime(uint32_t index) const {
        const char *value = pt_drop_offer_mime(handle_, index);
        return value ? std::string_view(value) : std::string_view();
    }
    [[nodiscard]] bool has_mime(std::string_view mime_type) const {
        std::string value = copy_string(mime_type);
        return pt_drop_offer_has_mime(handle_, value.c_str());
    }
    [[nodiscard]] std::string_view selected_mime() const {
        const char *value = pt_drop_offer_selected_mime(handle_);
        return value ? std::string_view(value) : std::string_view();
    }
    /* Bit mask of pt_drag_action values advertised by the source. */
    [[nodiscard]] uint32_t actions() const { return pt_drop_offer_actions(handle_); }
    /* max_bytes of zero means unlimited. Returns an invalid stream when the
     * offer is finished, the MIME type is unknown or no read backend exists. */
    [[nodiscard]] drop_stream read(std::string_view mime_type, size_t max_bytes = 0,
                                   pt_drop_stream_callback callback = nullptr,
                                   void *data = nullptr) const {
        std::string value = copy_string(mime_type);
        return drop_stream(
            pt_drop_offer_read(handle_, value.c_str(), max_bytes, callback, data));
    }

  private:
    pt_drop_offer *handle_ = nullptr;
};

class widget {
  public:
    widget() = default;
    explicit widget(pt_widget *handle) : handle_(handle) {}

    [[nodiscard]] pt_widget *get() const { return handle_; }
    operator pt_widget *() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] pt_widget_kind kind() const { return pt_widget_get_kind(handle_); }
    [[nodiscard]] widget parent() const { return widget(pt_widget_parent(handle_)); }
    [[nodiscard]] rect bounds() const {
        rect value;
        pt_widget_get_bounds(handle_, &value.x, &value.y, &value.width, &value.height);
        return value;
    }
    void destroy() {
        if (handle_) {
            pt_widget_destroy(handle_);
            handle_ = nullptr;
        }
    }
    widget &clear_children() {
        pt_widget_clear_children(handle_);
        return *this;
    }

    widget &set_size(float width, float height) {
        pt_widget_set_size(handle_, width, height);
        return *this;
    }
    widget &set_min_width(float width) {
        pt_widget_set_min_width(handle_, width);
        return *this;
    }
    widget &set_min_height(float height) {
        pt_widget_set_min_height(handle_, height);
        return *this;
    }
    widget &set_max_width(float width) {
        pt_widget_set_max_width(handle_, width);
        return *this;
    }
    widget &set_max_height(float height) {
        pt_widget_set_max_height(handle_, height);
        return *this;
    }
    widget &set_width(float width) {
        pt_widget_set_width(handle_, width);
        return *this;
    }
    widget &set_height(float height) {
        pt_widget_set_height(handle_, height);
        return *this;
    }
    widget &set_margin(float top, float right, float bottom, float left) {
        pt_widget_set_margin(handle_, top, right, bottom, left);
        return *this;
    }
    widget &set_padding(float top, float right, float bottom, float left) {
        pt_widget_set_padding(handle_, top, right, bottom, left);
        return *this;
    }
    widget &set_gap(float gap) {
        pt_widget_set_gap(handle_, gap);
        return *this;
    }
    widget &set_grow(float grow) {
        pt_widget_set_grow(handle_, grow);
        return *this;
    }
    widget &set_align_self(pt_align align) {
        pt_widget_set_align_self(handle_, align);
        return *this;
    }
    widget &set_background(pt_color color) {
        pt_widget_set_background(handle_, color);
        return *this;
    }
    widget &set_foreground(pt_color color) {
        pt_widget_set_foreground(handle_, color);
        return *this;
    }
    widget &set_border_color(pt_color color) {
        pt_widget_set_border_color(handle_, color);
        return *this;
    }
    widget &set_border_width(float width) {
        pt_widget_set_border_width(handle_, width);
        return *this;
    }
    widget &set_radius(float radius) {
        pt_widget_set_radius(handle_, radius);
        return *this;
    }
    widget &set_font_size(float size) {
        pt_widget_set_font_size(handle_, size);
        return *this;
    }
    widget &set_font_weight(pt_font_weight weight) {
        pt_widget_set_font_weight(handle_, weight);
        return *this;
    }
    widget &set_text_align(pt_text_align align) {
        pt_widget_set_text_align(handle_, align);
        return *this;
    }
    widget &set_visible(bool visible) {
        pt_widget_set_visible(handle_, visible);
        return *this;
    }
    widget &set_enabled(bool enabled) {
        pt_widget_set_enabled(handle_, enabled);
        return *this;
    }
    widget &set_busy(bool busy) {
        pt_widget_set_busy(handle_, busy);
        return *this;
    }
    [[nodiscard]] bool busy() const { return pt_widget_get_busy(handle_); }
    widget &set_invalid(bool invalid) {
        pt_widget_set_invalid(handle_, invalid);
        return *this;
    }
    [[nodiscard]] bool invalid() const { return pt_widget_get_invalid(handle_); }
    widget &set_chip_icon(pt_icon glyph) {
        pt_chip_set_icon(handle_, glyph);
        return *this;
    }
    widget &set_chip_removable(bool removable) {
        pt_chip_set_removable(handle_, removable);
        return *this;
    }
    [[nodiscard]] bool chip_removable() const { return pt_chip_get_removable(handle_); }
    widget &set_chip_selected(bool selected) {
        pt_chip_set_selected(handle_, selected);
        return *this;
    }
    [[nodiscard]] bool chip_selected() const { return pt_chip_get_selected(handle_); }
    widget &on_chip_click(pt_click_callback callback, void *data = nullptr) {
        pt_chip_set_on_click(handle_, callback, data);
        return *this;
    }
    widget &on_chip_remove(pt_click_callback callback, void *data = nullptr) {
        pt_chip_set_on_remove(handle_, callback, data);
        return *this;
    }
    widget &set_wrap(bool wrap) {
        pt_widget_set_label_wrap(handle_, wrap);
        return *this;
    }
    widget &set_text(std::string_view text) {
        std::string value = copy_string(text);
        pt_widget_set_text(handle_, value.c_str());
        return *this;
    }
    [[nodiscard]] std::string text() const {
        const char *value = pt_widget_get_text(handle_);
        return value ? value : "";
    }
    widget &set_text_spans(const std::vector<pt_text_span> &spans) {
        pt_widget_set_text_spans(handle_, spans.data(), (uint32_t)spans.size());
        return *this;
    }
    widget &set_text_spans(std::initializer_list<pt_text_span> spans) {
        std::vector<pt_text_span> values(spans);
        return set_text_spans(values);
    }
    widget &clear_text_spans() {
        pt_widget_clear_text_spans(handle_);
        return *this;
    }
    [[nodiscard]] uint32_t text_span_count() const {
        return pt_widget_get_text_span_count(handle_);
    }
    [[nodiscard]] std::optional<pt_text_span> text_span(uint32_t index) const {
        pt_text_span span{};
        if (!pt_widget_get_text_span(handle_, index, &span)) {
            return std::nullopt;
        }
        return span;
    }
    widget &set_input_purpose(pt_input_purpose purpose) {
        pt_widget_set_input_purpose(handle_, purpose);
        return *this;
    }
    [[nodiscard]] pt_input_purpose input_purpose() const {
        return pt_widget_get_input_purpose(handle_);
    }
    [[nodiscard]] pt_widget *raw() const { return handle_; }

    widget &set_button_variant(pt_button_variant variant) {
        pt_button_set_variant(handle_, variant);
        return *this;
    }
    widget &on_click(pt_click_callback callback, void *data = nullptr) {
        pt_button_set_on_click(handle_, callback, data);
        return *this;
    }
    widget &set_password(bool password) {
        pt_entry_set_password(handle_, password);
        return *this;
    }
    widget &set_placeholder(std::string_view placeholder) {
        std::string value = copy_string(placeholder);
        if (is_text_area()) {
            pt_text_area_set_placeholder(handle_, value.c_str());
        } else {
            pt_entry_set_placeholder(handle_, value.c_str());
        }
        return *this;
    }
    widget &on_change(pt_text_callback callback, void *data = nullptr) {
        if (is_text_area()) {
            pt_text_area_set_on_change(handle_, callback, data);
        } else {
            pt_entry_set_on_change(handle_, callback, data);
        }
        return *this;
    }
    widget &on_submit(pt_click_callback callback, void *data = nullptr) {
        pt_entry_set_on_submit(handle_, callback, data);
        return *this;
    }
    widget &select_all() {
        if (is_text_area()) {
            pt_text_area_select_all(handle_);
        } else {
            pt_entry_select_all(handle_);
        }
        return *this;
    }
    widget &set_selection(uint32_t start, uint32_t end) {
        if (is_text_area()) {
            pt_text_area_set_selection(handle_, start, end);
        } else {
            pt_entry_set_selection(handle_, start, end);
        }
        return *this;
    }
    [[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> selection() const {
        uint32_t start = 0;
        uint32_t end = 0;
        bool found = is_text_area() ? pt_text_area_get_selection(handle_, &start, &end)
                                    : pt_entry_get_selection(handle_, &start, &end);
        if (!found) {
            return std::nullopt;
        }
        return std::pair{start, end};
    }
    bool undo() const {
        return is_text_area() ? pt_text_area_undo(handle_) : pt_entry_undo(handle_);
    }
    bool redo() const {
        return is_text_area() ? pt_text_area_redo(handle_) : pt_entry_redo(handle_);
    }
    widget &set_grow_to_content(bool enabled) {
        pt_text_area_set_grow_to_content(handle_, enabled);
        return *this;
    }
    [[nodiscard]] bool grow_to_content() const {
        return pt_text_area_get_grow_to_content(handle_);
    }
    widget &scroll_to_caret() {
        pt_text_area_scroll_to_caret(handle_);
        return *this;
    }
    bool focus() const { return pt_widget_focus(handle_); }
    [[nodiscard]] bool focused() const { return pt_widget_has_focus(handle_); }

    widget &set_drop_target(const pt_drop_target &target) {
        pt_widget_set_drop_target(handle_, &target);
        return *this;
    }
    /* Structured target: enter_offer picks an advertised MIME type to accept,
     * drop_offer receives the accepted offer and streams the payload. */
    widget &set_drop_target(pt_drop_offer_enter_callback enter,
                            pt_drop_offer_drop_callback drop,
                            pt_drop_motion_callback motion = nullptr,
                            pt_drop_leave_callback leave = nullptr,
                            void *data = nullptr) {
        pt_drop_target target{};
        target.motion = motion;
        target.leave = leave;
        target.user_data = data;
        target.enter_offer = enter;
        target.drop_offer = drop;
        return set_drop_target(target);
    }
    widget &clear_drop_target() {
        pt_widget_set_drop_target(handle_, nullptr);
        return *this;
    }
    [[nodiscard]] bool drop_hover() const { return pt_widget_get_drop_hover(handle_); }
    widget &set_drag_enabled(bool enabled) {
        pt_entry_set_drag_enabled(handle_, enabled);
        return *this;
    }
    [[nodiscard]] bool drag_enabled() const { return pt_entry_get_drag_enabled(handle_); }

    widget &set_checked(bool checked) {
        pt_widget_set_checked(handle_, checked);
        return *this;
    }
    [[nodiscard]] bool checked() const { return pt_widget_get_checked(handle_); }
    widget &on_checked(pt_value_callback callback, void *data = nullptr) {
        pt_widget_set_on_checked(handle_, callback, data);
        return *this;
    }
    widget &on_toggle(pt_value_callback callback, void *data = nullptr) {
        return on_checked(callback, data);
    }
    widget &set_toggle_checked(bool checked) {
        pt_toggle_set_checked(handle_, checked);
        return *this;
    }
    [[nodiscard]] bool toggle_checked() const { return pt_toggle_get_checked(handle_); }
    widget &on_toggle_change(pt_value_callback callback, void *data = nullptr) {
        pt_toggle_set_on_change(handle_, callback, data);
        return *this;
    }
    widget &set_value(float value) {
        pt_slider_set_value(handle_, value);
        return *this;
    }
    [[nodiscard]] float value() const { return pt_slider_get_value(handle_); }
    widget &on_value_change(pt_value_callback callback, void *data = nullptr) {
        pt_slider_set_on_change(handle_, callback, data);
        return *this;
    }
    widget &set_progress(float value) {
        pt_progress_set_value(handle_, value);
        return *this;
    }
    [[nodiscard]] float progress() const { return pt_progress_get_value(handle_); }
    widget &set_indeterminate(bool indeterminate) {
        pt_progress_set_indeterminate(handle_, indeterminate);
        return *this;
    }
    [[nodiscard]] bool indeterminate() const {
        return pt_progress_get_indeterminate(handle_);
    }
    widget &select_segment(uint32_t index) {
        pt_segmented_set_selected(handle_, index);
        return *this;
    }
    [[nodiscard]] uint32_t segment() const { return pt_segmented_get_selected(handle_); }
    widget &on_segment_change(pt_index_callback callback, void *data = nullptr) {
        pt_segmented_set_on_change(handle_, callback, data);
        return *this;
    }
    widget &set_scroll_offset(float offset) {
        pt_scroll_set_offset(handle_, offset);
        return *this;
    }
    [[nodiscard]] float scroll_offset() const { return pt_scroll_get_offset(handle_); }
    [[nodiscard]] float max_scroll_offset() const {
        return pt_scroll_get_max_offset(handle_);
    }
    widget &set_wrap_gap(float row_gap, float column_gap) {
        pt_wrap_set_gap(handle_, row_gap, column_gap);
        return *this;
    }
    widget &set_grid_columns(uint32_t columns) {
        pt_grid_set_columns(handle_, columns);
        return *this;
    }
    [[nodiscard]] uint32_t grid_columns() const { return pt_grid_get_columns(handle_); }
    widget &set_grid_gap(float row_gap, float column_gap) {
        pt_grid_set_gap(handle_, row_gap, column_gap);
        return *this;
    }
    widget &set_grid_span(uint32_t span) {
        pt_widget_set_grid_span(handle_, span);
        return *this;
    }
    [[nodiscard]] uint32_t grid_span() const { return pt_widget_get_grid_span(handle_); }
    widget &set_row_count(uint32_t count) {
        pt_list_view_set_row_count(handle_, count);
        return *this;
    }
    [[nodiscard]] uint32_t row_count() const { return pt_list_view_get_row_count(handle_); }
    widget &set_row_height(float height) {
        pt_list_view_set_row_height(handle_, height);
        return *this;
    }
    [[nodiscard]] float row_height() const { return pt_list_view_get_row_height(handle_); }
    widget &on_bind_row(pt_list_bind_callback callback, void *data = nullptr) {
        pt_list_view_set_on_bind_row(handle_, callback, data);
        return *this;
    }
    widget &on_row_select(pt_index_callback callback, void *data = nullptr) {
        pt_list_view_set_on_select(handle_, callback, data);
        return *this;
    }
    widget &on_row_activate(pt_index_callback callback, void *data = nullptr) {
        pt_list_view_set_on_activate(handle_, callback, data);
        return *this;
    }
    [[nodiscard]] uint32_t selected_row() const {
        return pt_list_view_get_selected(handle_);
    }
    widget &set_selected_row(uint32_t index) {
        pt_list_view_set_selected(handle_, index);
        return *this;
    }
    widget &refresh() {
        pt_list_view_refresh(handle_);
        return *this;
    }
    widget &refresh_rows(uint32_t first, uint32_t count) {
        pt_list_view_refresh_rows(handle_, first, count);
        return *this;
    }
    widget &scroll_to_row(uint32_t index) {
        pt_list_view_scroll_to_row(handle_, index);
        return *this;
    }
    widget &set_row_key_callback(pt_row_key_callback callback, void *data = nullptr) {
        pt_list_view_set_row_key_callback(handle_, callback, data);
        return *this;
    }
    [[nodiscard]] uint64_t selected_key() const {
        return pt_list_view_get_selected_key(handle_);
    }
    bool set_selected_key(uint64_t key) { return pt_list_view_set_selected_key(handle_, key); }
    widget &set_row_text_callback(pt_row_text_callback callback, void *data = nullptr) {
        pt_list_view_set_row_text_callback(handle_, callback, data);
        return *this;
    }
    [[nodiscard]] uint32_t list_selection_count() const {
        return pt_list_view_get_selection_count(handle_);
    }
    [[nodiscard]] bool is_list_row_selected(uint32_t row) const {
        return pt_list_view_is_row_selected(handle_, row);
    }
    [[nodiscard]] std::vector<uint32_t> list_selection() const {
        std::vector<uint32_t> rows(list_selection_count());
        if (rows.empty()) {
            return rows;
        }
        rows.resize(pt_list_view_get_selection(handle_, rows.data(),
                                               static_cast<uint32_t>(rows.size())));
        return rows;
    }
    widget &list_select_range(uint32_t first, uint32_t last) {
        pt_list_view_select_range(handle_, first, last);
        return *this;
    }
    widget &list_select_toggle(uint32_t row) {
        pt_list_view_select_toggle(handle_, row);
        return *this;
    }
    widget &clear_list_selection() {
        pt_list_view_clear_selection(handle_);
        return *this;
    }
    widget &set_icon(pt_icon icon) {
        pt_widget_set_icon(handle_, icon);
        return *this;
    }
    [[nodiscard]] pt_icon icon() const { return pt_widget_get_icon(handle_); }
    widget &set_icon_size(float size) {
        pt_icon_set_size(handle_, size);
        return *this;
    }
    [[nodiscard]] float icon_size() const { return pt_icon_get_size(handle_); }

    widget &set_column_count(uint32_t count) {
        pt_table_set_column_count(handle_, count);
        return *this;
    }
    [[nodiscard]] uint32_t column_count() const {
        return pt_table_get_column_count(handle_);
    }
    widget &set_column_title(uint32_t column, std::string_view title) {
        std::string value = copy_string(title);
        pt_table_set_column_title(handle_, column, value.c_str());
        return *this;
    }
    widget &set_column_width(uint32_t column, float width) {
        pt_table_set_column_width(handle_, column, width);
        return *this;
    }
    [[nodiscard]] float column_width(uint32_t column) const {
        return pt_table_get_column_width(handle_, column);
    }
    widget &set_column_align(uint32_t column, pt_text_align align) {
        pt_table_set_column_align(handle_, column, align);
        return *this;
    }
    widget &set_column_resizable(uint32_t column, bool resizable) {
        pt_table_set_column_resizable(handle_, column, resizable);
        return *this;
    }
    widget &set_column_hidden(uint32_t column, bool hidden) {
        pt_table_set_column_hidden(handle_, column, hidden);
        return *this;
    }
    [[nodiscard]] bool column_hidden(uint32_t column) const {
        return pt_table_get_column_hidden(handle_, column);
    }
    widget &set_table_row_count(uint32_t count) {
        pt_table_set_row_count(handle_, count);
        return *this;
    }
    [[nodiscard]] uint32_t table_row_count() const {
        return pt_table_get_row_count(handle_);
    }
    widget &set_table_row_height(float height) {
        pt_table_set_row_height(handle_, height);
        return *this;
    }
    [[nodiscard]] float table_row_height() const {
        return pt_table_get_row_height(handle_);
    }
    widget &set_header_height(float height) {
        pt_table_set_header_height(handle_, height);
        return *this;
    }
    widget &on_bind_cell(pt_table_bind_cell_callback callback, void *data = nullptr) {
        pt_table_set_on_bind_cell(handle_, callback, data);
        return *this;
    }
    widget &on_table_select(pt_index_callback callback, void *data = nullptr) {
        pt_table_set_on_select(handle_, callback, data);
        return *this;
    }
    widget &on_table_activate(pt_index_callback callback, void *data = nullptr) {
        pt_table_set_on_activate(handle_, callback, data);
        return *this;
    }
    widget &on_table_sort(pt_table_sort_callback callback, void *data = nullptr) {
        pt_table_set_on_sort(handle_, callback, data);
        return *this;
    }
    widget &on_sort_columns(pt_table_sort_columns_callback callback,
                            void *data = nullptr) {
        pt_table_set_on_sort_columns(handle_, callback, data);
        return *this;
    }
    widget &set_sort(uint32_t column, pt_sort_direction direction) {
        pt_table_set_sort(handle_, column, direction);
        return *this;
    }
    [[nodiscard]] pt_sort_direction sort(uint32_t *column = nullptr) const {
        return pt_table_get_sort(handle_, column);
    }
    widget &set_sort_columns(std::initializer_list<sort_key> keys) {
        std::vector<uint32_t> columns;
        std::vector<pt_sort_direction> directions;
        columns.reserve(keys.size());
        directions.reserve(keys.size());
        for (const sort_key &key : keys) {
            columns.push_back(key.column);
            directions.push_back(key.direction);
        }
        pt_table_set_sort_columns(handle_, static_cast<uint32_t>(columns.size()),
                                  columns.data(), directions.data());
        return *this;
    }
    [[nodiscard]] std::vector<sort_key> sort_columns() const {
        uint32_t columns[PT_TABLE_MAX_SORT_COLUMNS] = {};
        pt_sort_direction directions[PT_TABLE_MAX_SORT_COLUMNS] = {};
        uint32_t count = pt_table_get_sort_columns(handle_, columns, directions);
        std::vector<sort_key> keys;
        keys.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            keys.push_back(sort_key{columns[i], directions[i]});
        }
        return keys;
    }
    [[nodiscard]] uint32_t table_selected() const {
        return pt_table_get_selected(handle_);
    }
    widget &set_table_selected(uint32_t row) {
        pt_table_set_selected(handle_, row);
        return *this;
    }
    widget &set_table_row_key_callback(pt_row_key_callback callback,
                                       void *data = nullptr) {
        pt_table_set_row_key_callback(handle_, callback, data);
        return *this;
    }
    [[nodiscard]] uint64_t table_selected_key() const {
        return pt_table_get_selected_key(handle_);
    }
    bool set_table_selected_key(uint64_t key) {
        return pt_table_set_selected_key(handle_, key);
    }
    widget &set_table_row_text_callback(pt_row_text_callback callback,
                                        void *data = nullptr) {
        pt_table_set_row_text_callback(handle_, callback, data);
        return *this;
    }
    widget &table_refresh() {
        pt_table_refresh(handle_);
        return *this;
    }
    widget &table_refresh_rows(uint32_t first, uint32_t count) {
        pt_table_refresh_rows(handle_, first, count);
        return *this;
    }
    widget &scroll_to_column(uint32_t column) {
        pt_table_scroll_to_column(handle_, column);
        return *this;
    }
    widget &table_scroll_to_row(uint32_t row) {
        pt_table_scroll_to_row(handle_, row);
        return *this;
    }
    widget &set_table_editable(bool editable) {
        pt_table_set_editable(handle_, editable);
        return *this;
    }
    [[nodiscard]] bool table_editable() const { return pt_table_get_editable(handle_); }
    bool begin_cell_edit(uint32_t row, uint32_t column) {
        return pt_table_begin_edit(handle_, row, column);
    }
    bool commit_cell_edit() { return pt_table_commit_edit(handle_); }
    widget &cancel_cell_edit() {
        pt_table_cancel_edit(handle_);
        return *this;
    }
    [[nodiscard]] bool table_editing() const { return pt_table_is_editing(handle_); }
    widget &on_cell_edited(pt_table_cell_edited_callback callback, void *data = nullptr) {
        pt_table_set_on_cell_edited(handle_, callback, data);
        return *this;
    }
    [[nodiscard]] uint32_t table_selection_count() const {
        return pt_table_get_selection_count(handle_);
    }
    [[nodiscard]] bool is_row_selected(uint32_t row) const {
        return pt_table_is_row_selected(handle_, row);
    }
    [[nodiscard]] std::vector<uint32_t> table_selection() const {
        std::vector<uint32_t> rows(table_selection_count());
        if (rows.empty()) {
            return rows;
        }
        rows.resize(pt_table_get_selection(handle_, rows.data(),
                                           static_cast<uint32_t>(rows.size())));
        return rows;
    }
    widget &select_range(uint32_t first, uint32_t last) {
        pt_table_select_range(handle_, first, last);
        return *this;
    }
    widget &select_toggle(uint32_t row) {
        pt_table_select_toggle(handle_, row);
        return *this;
    }
    widget &clear_table_selection() {
        pt_table_clear_selection(handle_);
        return *this;
    }
    widget &set_columns_reorderable(bool reorderable) {
        pt_table_set_columns_reorderable(handle_, reorderable);
        return *this;
    }
    [[nodiscard]] bool columns_reorderable() const {
        return pt_table_get_columns_reorderable(handle_);
    }
    bool move_column(uint32_t from, uint32_t to) {
        return pt_table_move_column(handle_, from, to);
    }
    widget &on_columns_reordered(pt_table_columns_reordered_callback callback,
                                 void *data = nullptr) {
        pt_table_set_on_columns_reordered(handle_, callback, data);
        return *this;
    }
    widget &on_row_drag(pt_table_row_drag_callback callback, void *data = nullptr) {
        pt_table_set_on_row_drag(handle_, callback, data);
        return *this;
    }

    [[nodiscard]] uint32_t tree_root_count() const {
        return pt_tree_get_root_count(handle_);
    }
    [[nodiscard]] tree_node *tree_root(uint32_t index) const {
        return pt_tree_get_root_node(handle_, index);
    }
    tree_node *add_tree_node(tree_node *parent, std::string_view text) {
        std::string value = copy_string(text);
        return pt_tree_add_node(handle_, parent, value.c_str());
    }
    widget &clear_tree() {
        pt_tree_clear(handle_);
        return *this;
    }
    [[nodiscard]] uint32_t tree_visible_count() const {
        return pt_tree_get_visible_count(handle_);
    }
    [[nodiscard]] tree_node *tree_visible_node(uint32_t row) const {
        return pt_tree_get_visible_node(handle_, row);
    }
    [[nodiscard]] tree_node *tree_selected() const {
        return pt_tree_get_selected(handle_);
    }
    widget &set_tree_selected(tree_node *node) {
        pt_tree_set_selected(handle_, node);
        return *this;
    }
    [[nodiscard]] uint32_t tree_selection_count() const {
        return pt_tree_get_selection_count(handle_);
    }
    [[nodiscard]] bool is_tree_node_selected(const tree_node *node) const {
        return pt_tree_is_selected(handle_, node);
    }
    [[nodiscard]] std::vector<tree_node *> tree_selection() const {
        std::vector<tree_node *> nodes(tree_selection_count());
        if (nodes.empty()) {
            return nodes;
        }
        nodes.resize(pt_tree_get_selection(handle_, nodes.data(),
                                           static_cast<uint32_t>(nodes.size())));
        return nodes;
    }
    widget &tree_select_range(uint32_t first_row, uint32_t last_row) {
        pt_tree_select_range(handle_, first_row, last_row);
        return *this;
    }
    widget &tree_select_toggle(tree_node *node) {
        pt_tree_select_toggle(handle_, node);
        return *this;
    }
    widget &clear_tree_selection() {
        pt_tree_clear_selection(handle_);
        return *this;
    }
    widget &set_tree_row_height(float height) {
        pt_tree_set_row_height(handle_, height);
        return *this;
    }
    widget &set_tree_indent(float indent) {
        pt_tree_set_indent(handle_, indent);
        return *this;
    }
    widget &on_tree_select(pt_tree_node_callback callback, void *data = nullptr) {
        pt_tree_set_on_select(handle_, callback, data);
        return *this;
    }
    widget &on_tree_activate(pt_tree_node_callback callback, void *data = nullptr) {
        pt_tree_set_on_activate(handle_, callback, data);
        return *this;
    }
    widget &tree_refresh() {
        pt_tree_refresh(handle_);
        return *this;
    }
    widget &scroll_to_tree_node(tree_node *node) {
        pt_tree_scroll_to_node(handle_, node);
        return *this;
    }
    widget &set_tree_renamable(bool renamable) {
        pt_tree_set_renamable(handle_, renamable);
        return *this;
    }
    [[nodiscard]] bool tree_renamable() const { return pt_tree_get_renamable(handle_); }
    bool begin_tree_rename(tree_node *node) { return pt_tree_begin_rename(handle_, node); }
    bool commit_tree_rename() { return pt_tree_commit_rename(handle_); }
    widget &cancel_tree_rename() {
        pt_tree_cancel_rename(handle_);
        return *this;
    }
    [[nodiscard]] bool tree_renaming() const { return pt_tree_is_renaming(handle_); }
    widget &on_tree_rename(pt_tree_rename_callback callback, void *data = nullptr) {
        pt_tree_set_on_rename(handle_, callback, data);
        return *this;
    }

    widget &add_item(std::string_view text) {
        std::string value = copy_string(text);
        pt_combobox_add_item(handle_, value.c_str());
        return *this;
    }
    widget &insert_item(uint32_t index, std::string_view text) {
        std::string value = copy_string(text);
        pt_combobox_insert_item(handle_, index, value.c_str());
        return *this;
    }
    widget &remove_item(uint32_t index) {
        pt_combobox_remove_item(handle_, index);
        return *this;
    }
    widget &clear_items() {
        pt_combobox_clear(handle_);
        return *this;
    }
    [[nodiscard]] uint32_t item_count() const { return pt_combobox_get_item_count(handle_); }
    [[nodiscard]] std::string item_text(uint32_t index) const {
        const char *value = pt_combobox_get_item_text(handle_, index);
        return value ? value : "";
    }
    widget &set_item_text(uint32_t index, std::string_view text) {
        std::string value = copy_string(text);
        pt_combobox_set_item_text(handle_, index, value.c_str());
        return *this;
    }
    widget &set_item_data(uint32_t index, void *data) {
        pt_combobox_set_item_data(handle_, index, data);
        return *this;
    }
    [[nodiscard]] void *item_data(uint32_t index) const {
        return pt_combobox_get_item_data(handle_, index);
    }
    widget &set_item_disabled(uint32_t index, bool disabled) {
        pt_combobox_set_item_disabled(handle_, index, disabled);
        return *this;
    }
    [[nodiscard]] bool is_item_disabled(uint32_t index) const {
        return pt_combobox_get_item_disabled(handle_, index);
    }
    widget &set_combo_mode(pt::combo_mode mode) {
        pt_combobox_set_mode(handle_, mode);
        return *this;
    }
    [[nodiscard]] pt::combo_mode combo_mode() const { return pt_combobox_get_mode(handle_); }
    [[nodiscard]] uint32_t combo_selected() const { return pt_combobox_get_selected(handle_); }
    widget &set_combo_selected(uint32_t index) {
        pt_combobox_set_selected(handle_, index);
        return *this;
    }
    [[nodiscard]] std::string combo_text() const {
        const char *value = pt_combobox_get_text(handle_);
        return value ? value : "";
    }
    widget &set_combo_text(std::string_view text) {
        std::string value = copy_string(text);
        pt_combobox_set_text(handle_, value.c_str());
        return *this;
    }
    [[nodiscard]] std::string combo_placeholder() const {
        const char *value = pt_combobox_get_placeholder(handle_);
        return value ? value : "";
    }
    widget &set_combo_placeholder(std::string_view text) {
        std::string value = copy_string(text);
        pt_combobox_set_placeholder(handle_, value.c_str());
        return *this;
    }
    widget &open_popup() {
        pt_combobox_open(handle_);
        return *this;
    }
    widget &close_popup() {
        pt_combobox_close(handle_);
        return *this;
    }
    [[nodiscard]] bool is_popup_open() const { return pt_combobox_is_open(handle_); }
    widget &set_popup_max_height(float height) {
        pt_combobox_set_popup_max_height(handle_, height);
        return *this;
    }
    [[nodiscard]] float popup_max_height() const {
        return pt_combobox_get_popup_max_height(handle_);
    }
    widget &on_combo_change(pt_index_callback callback, void *data = nullptr) {
        pt_combobox_set_on_change(handle_, callback, data);
        return *this;
    }
    widget &on_combo_text_change(pt_combobox_text_callback callback, void *data = nullptr) {
        pt_combobox_set_on_text_change(handle_, callback, data);
        return *this;
    }

    widget add_tab(std::string_view title) const {
        std::string value = copy_string(title);
        return widget(pt_tabs_add_page(handle_, value.c_str()));
    }
    void remove_tab(uint32_t index) const { pt_tabs_remove_page(handle_, index); }
    [[nodiscard]] uint32_t tab_count() const {
        return pt_tabs_get_page_count(handle_);
    }
    [[nodiscard]] widget tab_page(uint32_t index) const {
        return widget(pt_tabs_get_page(handle_, index));
    }
    widget &set_tab_title(uint32_t index, std::string_view title) {
        std::string value = copy_string(title);
        pt_tabs_set_page_title(handle_, index, value.c_str());
        return *this;
    }
    [[nodiscard]] std::string tab_title(uint32_t index) const {
        const char *value = pt_tabs_get_page_title(handle_, index);
        return value ? value : "";
    }
    [[nodiscard]] uint32_t active_tab() const { return pt_tabs_get_active(handle_); }
    widget &set_active_tab(uint32_t index) {
        pt_tabs_set_active(handle_, index);
        return *this;
    }
    widget &on_tab_change(pt_index_callback callback, void *data = nullptr) {
        pt_tabs_set_on_change(handle_, callback, data);
        return *this;
    }
    widget &set_tab_strip_height(float height) {
        pt_tabs_set_strip_height(handle_, height);
        return *this;
    }

    widget add_sidebar_item(pt_icon icon, std::string_view label) const {
        std::string value = copy_string(label);
        return widget(pt_sidebar_add_item(handle_, icon, value.c_str()));
    }
    [[nodiscard]] uint32_t sidebar_item_count() const {
        return pt_sidebar_get_item_count(handle_);
    }
    [[nodiscard]] widget sidebar_item(uint32_t index) const {
        return widget(pt_sidebar_get_item(handle_, index));
    }
    [[nodiscard]] uint32_t active_sidebar_item() const {
        return pt_sidebar_get_active(handle_);
    }
    widget &set_active_sidebar_item(uint32_t index) {
        pt_sidebar_set_active(handle_, index);
        return *this;
    }
    [[nodiscard]] bool sidebar_compact() const {
        return pt_sidebar_get_compact(handle_);
    }
    widget &set_sidebar_compact(bool compact) {
        pt_sidebar_set_compact(handle_, compact);
        return *this;
    }
    widget &set_sidebar_header(std::string_view text) {
        std::string value = copy_string(text);
        pt_sidebar_set_header(handle_, value.c_str());
        return *this;
    }
    [[nodiscard]] std::string sidebar_header() const {
        const char *value = pt_sidebar_get_header(handle_);
        return value ? value : "";
    }
    widget &on_sidebar_select(pt_index_callback callback, void *data = nullptr) {
        pt_sidebar_set_on_select(handle_, callback, data);
        return *this;
    }
    widget &set_box_direction(pt_direction direction) {
        pt_box_set_direction(handle_, direction);
        return *this;
    }
    widget &set_box_gap(float gap) {
        pt_box_set_gap(handle_, gap);
        return *this;
    }
    widget &box_align(pt_align align) {
        pt_box_set_align(handle_, align);
        return *this;
    }
    widget &box_justify(pt_justify justify) {
        pt_box_set_justify(handle_, justify);
        return *this;
    }
    widget &set_tooltip(std::string_view text) {
        std::string value = copy_string(text);
        pt_widget_set_tooltip(handle_, value.c_str());
        return *this;
    }
    widget &set_tooltip_delay(uint32_t milliseconds) {
        pt_widget_set_tooltip_delay(handle_, milliseconds);
        return *this;
    }
    widget &set_a11y_role(pt_a11y_role role) {
        pt_widget_set_a11y_role(handle_, role);
        return *this;
    }
    [[nodiscard]] pt_a11y_role a11y_role() const {
        return pt_widget_get_a11y_role(handle_);
    }
    widget &set_a11y_name(std::string_view name) {
        std::string value = copy_string(name);
        pt_widget_set_a11y_name(handle_, value.c_str());
        return *this;
    }
    [[nodiscard]] std::string a11y_name() const {
        const char *value = pt_widget_get_a11y_name(handle_);
        return value ? value : "";
    }
    widget &set_a11y_description(std::string_view description) {
        std::string value = copy_string(description);
        pt_widget_set_a11y_description(handle_, value.c_str());
        return *this;
    }
    [[nodiscard]] std::string a11y_description() const {
        const char *value = pt_widget_get_a11y_description(handle_);
        return value ? value : "";
    }
    [[nodiscard]] uint32_t a11y_state() const { return pt_widget_get_a11y_state(handle_); }
    [[nodiscard]] std::optional<a11y_text_info> a11y_text() const {
        const char *text = nullptr;
        a11y_text_info info;
        if (!pt_widget_get_a11y_text(handle_, &text, &info.caret, &info.selection_start,
                                     &info.selection_end)) {
            return std::nullopt;
        }
        info.text = text ? text : "";
        return info;
    }
    [[nodiscard]] std::optional<float> a11y_value() const {
        float value = 0.0f;
        if (!pt_widget_get_a11y_value(handle_, &value)) {
            return std::nullopt;
        }
        return value;
    }

  protected:
    [[nodiscard]] bool is_text_area() const {
        return handle_ != nullptr && pt_widget_get_kind(handle_) == pt_widget_text_area;
    }

    pt_widget *handle_ = nullptr;
};

inline widget box(pt_widget *parent, pt_direction direction) {
    return widget(pt_box_create(parent, direction));
}
inline widget row(pt_widget *parent) { return widget(pt_row_create(parent)); }
inline widget column(pt_widget *parent) { return widget(pt_column_create(parent)); }
inline widget label(pt_widget *parent, std::string_view text) {
    std::string value = copy_string(text);
    return widget(pt_label_create(parent, value.c_str()));
}
inline widget button(pt_widget *parent, std::string_view text) {
    std::string value = copy_string(text);
    return widget(pt_button_create(parent, value.c_str()));
}
inline widget entry(pt_widget *parent, std::string_view placeholder) {
    std::string value = copy_string(placeholder);
    return widget(pt_entry_create(parent, value.c_str()));
}
inline widget toggle(pt_widget *parent) { return widget(pt_toggle_create(parent)); }
inline widget checkbox(pt_widget *parent, std::string_view text) {
    std::string value = copy_string(text);
    return widget(pt_checkbox_create(parent, value.c_str()));
}
inline widget radio(pt_widget *parent, std::string_view text) {
    std::string value = copy_string(text);
    return widget(pt_radio_create(parent, value.c_str()));
}
inline widget slider(pt_widget *parent) { return widget(pt_slider_create(parent)); }
inline widget progress(pt_widget *parent) { return widget(pt_progress_create(parent)); }
inline widget spinner(pt_widget *parent) { return widget(pt_spinner_create(parent)); }
inline widget segmented(pt_widget *parent, std::initializer_list<std::string_view> labels) {
    std::vector<std::string> owned;
    owned.reserve(labels.size());
    for (std::string_view label : labels) {
        owned.emplace_back(label);
    }
    std::vector<const char *> pointers;
    pointers.reserve(owned.size());
    for (const std::string &label : owned) {
        pointers.push_back(label.c_str());
    }
    return widget(pt_segmented_create(parent, pointers.data(),
                                      static_cast<uint32_t>(pointers.size())));
}
inline widget text_area(pt_widget *parent, std::string_view placeholder = {}) {
    std::string value = copy_string(placeholder);
    return widget(pt_text_area_create(parent, value.c_str()));
}
inline widget scroll(pt_widget *parent) { return widget(pt_scroll_create(parent)); }
inline widget wrap(pt_widget *parent) { return widget(pt_wrap_create(parent)); }
inline widget grid(pt_widget *parent, uint32_t columns) {
    return widget(pt_grid_create(parent, columns));
}
inline widget list_view(pt_widget *parent) { return widget(pt_list_view_create(parent)); }
inline widget table(pt_widget *parent) { return widget(pt_table_create(parent)); }
inline widget tree(pt_widget *parent) { return widget(pt_tree_create(parent)); }
inline widget combobox(pt_widget *parent) { return widget(pt_combobox_create(parent)); }
inline widget tabs(pt_widget *parent) { return widget(pt_tabs_create(parent)); }
inline widget sidebar(pt_widget *parent) { return widget(pt_sidebar_create(parent)); }
inline widget icon(pt_widget *parent, pt_icon glyph) {
    return widget(pt_icon_create(parent, glyph));
}
inline widget icon_button(pt_widget *parent, pt_icon glyph, std::string_view accessible_name) {
    std::string value = copy_string(accessible_name);
    return widget(pt_icon_button_create(parent, glyph, value.c_str()));
}
inline widget divider(pt_widget *parent) { return widget(pt_divider_create(parent)); }
inline widget card(pt_widget *parent) { return widget(pt_card_create(parent)); }
inline widget spacer(pt_widget *parent) { return widget(pt_spacer_create(parent)); }
inline widget chip(pt_widget *parent, std::string_view text) {
    std::string value = copy_string(text);
    return widget(pt_chip_create(parent, value.c_str()));
}
inline widget skeleton(pt_widget *parent) { return widget(pt_skeleton_create(parent)); }

class window {
  public:
    window() = default;
    explicit window(pt_window *handle) : handle_(handle) {}
    window(app &application, std::string_view title, int width, int height) {
        std::string value = copy_string(title);
        handle_ = pt_window_create(application.get(), value.c_str(), width, height);
    }

    [[nodiscard]] pt_window *get() const { return handle_; }
    operator pt_window *() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }

    void show() const { pt_window_show(handle_); }
    void hide() const { pt_window_hide(handle_); }
    void close() const { pt_window_close(handle_); }
    [[nodiscard]] bool is_open() const { return pt_window_is_open(handle_); }
    [[nodiscard]] int width() const { return pt_window_get_width(handle_); }
    [[nodiscard]] int height() const { return pt_window_get_height(handle_); }
    [[nodiscard]] float scale() const { return pt_window_get_scale(handle_); }
    [[nodiscard]] widget root() const { return widget(pt_window_root(handle_)); }
    [[nodiscard]] pt_app *application() const { return pt_window_app(handle_); }

    window &set_title(std::string_view title) {
        std::string value = copy_string(title);
        pt_window_set_title(handle_, value.c_str());
        return *this;
    }
    window &set_size(int width, int height) {
        pt_window_set_size(handle_, width, height);
        return *this;
    }
    window &set_min_size(int width, int height) {
        pt_window_set_min_size(handle_, width, height);
        return *this;
    }
    window &set_resizable(bool resizable) {
        pt_window_set_resizable(handle_, resizable);
        return *this;
    }
    window &set_decorated(bool decorated) {
        pt_window_set_decorated(handle_, decorated);
        return *this;
    }
    window &set_popup_policy(pt_popup_policy policy) {
        pt_window_set_popup_policy(handle_, policy);
        return *this;
    }
    [[nodiscard]] pt_popup_policy popup_policy() const {
        return pt_window_get_popup_policy(handle_);
    }
    void request_redraw() const { pt_window_request_redraw(handle_); }
    void focus(const widget &target) const { pt_window_focus(handle_, target.get()); }
    window &set_theme_color(pt_theme_token token, pt_color color) {
        pt_window_set_theme_color(handle_, token, color);
        return *this;
    }
    [[nodiscard]] pt_color theme_color(pt_theme_token token) const {
        return pt_window_get_theme_color(handle_, token);
    }
    window &set_theme_metric(pt_theme_metric metric, float value) {
        pt_window_set_theme_metric(handle_, metric, value);
        return *this;
    }
    [[nodiscard]] float theme_metric(pt_theme_metric metric) const {
        return pt_window_get_theme_metric(handle_, metric);
    }
    window &clear_theme_overrides() {
        pt_window_clear_theme_overrides(handle_);
        return *this;
    }
    window &on_close(pt_close_callback callback, void *data) {
        pt_window_set_close_callback(handle_, callback, data);
        return *this;
    }
    window &on_resize(pt_resize_callback callback, void *data) {
        pt_window_set_resize_callback(handle_, callback, data);
        return *this;
    }
    window &on_key(pt_key_callback callback, void *data) {
        pt_window_set_key_callback(handle_, callback, data);
        return *this;
    }
    window &on_gesture(pt_gesture_callback callback, void *data = nullptr) {
        pt_window_set_gesture_callback(handle_, callback, data);
        return *this;
    }
    window &set_long_press_delay(uint32_t milliseconds) {
        pt_window_set_long_press_delay(handle_, milliseconds);
        return *this;
    }
    window &set_momentum_scrolling(bool enabled) {
        pt_window_set_momentum_scrolling(handle_, enabled);
        return *this;
    }
    [[nodiscard]] bool momentum_scrolling() const {
        return pt_window_get_momentum_scrolling(handle_);
    }
    /* Structured drag offering several MIME types. Entries with a zero size
     * carry NUL-terminated payloads. */
    bool begin_drag(std::span<const pt_drag_data> types) const {
        return pt_window_begin_drag(handle_, types.data(),
                                    static_cast<uint32_t>(types.size()));
    }
    bool begin_drag(std::initializer_list<pt_drag_data> types) const {
        std::vector<pt_drag_data> values(types);
        return begin_drag(std::span<const pt_drag_data>(values));
    }
    window &set_drag_finished_callback(pt_drag_finished_callback callback,
                                       void *data = nullptr) {
        pt_window_set_drag_finished_callback(handle_, callback, data);
        return *this;
    }
    /* Action performed by the last finished drag, or pt_drag_action_none. */
    [[nodiscard]] pt_drag_action drag_result() const {
        return pt_window_get_drag_result(handle_);
    }
    void set_drag_image(const pt_color *pixels, int width, int height) const {
        pt_window_set_drag_image(handle_, pixels, width, height);
    }
    void clear_drag_image() const { pt_window_clear_drag_image(handle_); }
    [[nodiscard]] bool drag_active() const { return pt_window_get_drag_active(handle_); }
    void cancel_drag() const { pt_window_cancel_drag(handle_); }
    window &set_drag_timeout(uint32_t milliseconds) {
        pt_window_set_drag_timeout(handle_, milliseconds);
        return *this;
    }
    [[nodiscard]] uint32_t drag_timeout() const {
        return pt_window_get_drag_timeout(handle_);
    }
    window &on_stylus(pt_stylus_callback callback, void *data = nullptr) {
        pt_window_set_stylus_callback(handle_, callback, data);
        return *this;
    }
    bool set_clipboard(std::string_view text) const {
        std::string value = copy_string(text);
        return pt_window_clipboard_set(handle_, value.c_str());
    }
    [[nodiscard]] std::string clipboard() const {
        char *value = pt_window_clipboard_get(handle_);
        if (!value) {
            return {};
        }
        std::string copy(value);
        pt_free(value);
        return copy;
    }
    [[nodiscard]] toast show_toast(std::string_view text,
                                   pt_toast_kind kind = pt_toast_info,
                                   uint32_t duration_ms = 3000) const;

  private:
    pt_window *handle_ = nullptr;
};

class menu {
  public:
    explicit menu(pt_window *owner) : handle_(pt_menu_create(owner)) {}
    explicit menu(const window &owner) : menu(owner.get()) {}
    ~menu() {
        if (handle_) {
            pt_menu_destroy(handle_);
        }
    }
    menu(const menu &) = delete;
    menu &operator=(const menu &) = delete;
    menu(menu &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    menu &operator=(menu &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                pt_menu_destroy(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] pt_menu *get() const { return handle_; }

    menu &add_item(std::string_view label, std::string_view shortcut,
                   pt_click_callback callback = nullptr, void *data = nullptr) {
        std::string label_copy = copy_string(label);
        std::string shortcut_copy = copy_string(shortcut);
        pt_menu_add_item(handle_, label_copy.c_str(), shortcut_copy.c_str(), callback, data);
        return *this;
    }
    menu &add_separator() {
        pt_menu_add_separator(handle_);
        return *this;
    }
    void show_at(float x, float y) const { pt_menu_show_at(handle_, x, y); }
    void show_below(const widget &anchor) const {
        pt_menu_show_below(handle_, anchor.get());
    }
    void dismiss() const { pt_menu_dismiss(handle_); }
    [[nodiscard]] bool is_open() const { return pt_menu_is_open(handle_); }

  private:
    pt_menu *handle_ = nullptr;
};

class dialog {
  public:
    dialog(pt_window *owner, std::string_view title, std::string_view message)
        : handle_(nullptr) {
        std::string title_copy = copy_string(title);
        std::string message_copy = copy_string(message);
        handle_ = pt_dialog_create(owner, title_copy.c_str(), message_copy.c_str());
    }
    dialog(const window &owner, std::string_view title, std::string_view message)
        : dialog(owner.get(), title, message) {}
    ~dialog() {
        if (handle_) {
            pt_dialog_destroy(handle_);
        }
    }
    dialog(const dialog &) = delete;
    dialog &operator=(const dialog &) = delete;
    dialog(dialog &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    dialog &operator=(dialog &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                pt_dialog_destroy(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] pt_dialog *get() const { return handle_; }

    dialog &set_title(std::string_view title) {
        std::string value = copy_string(title);
        pt_dialog_set_title(handle_, value.c_str());
        return *this;
    }
    dialog &set_message(std::string_view message) {
        std::string value = copy_string(message);
        pt_dialog_set_message(handle_, value.c_str());
        return *this;
    }
    dialog &add_action(std::string_view label, bool primary,
                       pt_click_callback callback = nullptr, void *data = nullptr) {
        std::string value = copy_string(label);
        pt_dialog_add_action(handle_, value.c_str(), primary, callback, data);
        return *this;
    }
    void show() const { pt_dialog_show(handle_); }
    void close() const { pt_dialog_close(handle_); }
    [[nodiscard]] bool is_open() const { return pt_dialog_is_open(handle_); }
    dialog &on_close(pt_dialog_callback callback, void *data = nullptr) {
        pt_dialog_set_on_close(handle_, callback, data);
        return *this;
    }

  private:
    pt_dialog *handle_ = nullptr;
};

class toast {
  public:
    toast() = default;
    explicit toast(pt_toast *handle) : handle_(handle) {}

    [[nodiscard]] pt_toast *get() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != nullptr; }
    [[nodiscard]] bool visible() const { return pt_toast_is_visible(handle_); }
    void dismiss() const { pt_toast_dismiss(handle_); }

  private:
    pt_toast *handle_ = nullptr;
};

inline toast window::show_toast(std::string_view text, pt_toast_kind kind,
                                uint32_t duration_ms) const {
    std::string value = copy_string(text);
    return toast(pt_window_show_toast(handle_, value.c_str(), kind, duration_ms));
}

inline float ease(pt_easing easing, float t) { return pt_ease(easing, t); }

inline uint32_t uri_list_count(std::string_view text) {
    std::string value = copy_string(text);
    return pt_uri_list_count(value.c_str());
}

inline std::string uri_list_get(std::string_view text, uint32_t index) {
    std::string value = copy_string(text);
    char *uri = pt_uri_list_get(value.c_str(), index);
    if (!uri) {
        return {};
    }
    std::string copy(uri);
    pt_free(uri);
    return copy;
}

inline std::string tree_text(const tree_node *node) {
    const char *value = pt_tree_node_get_text(node);
    return value ? value : "";
}

inline void set_tree_text(tree_node *node, std::string_view text) {
    std::string value = copy_string(text);
    pt_tree_node_set_text(node, value.c_str());
}

inline bool tree_expanded(const tree_node *node) {
    return pt_tree_node_get_expanded(node);
}

inline void set_tree_expanded(tree_node *node, bool expanded) {
    pt_tree_node_set_expanded(node, expanded);
}

inline bool tree_has_children(const tree_node *node) {
    return pt_tree_node_has_children(node);
}

inline uint32_t tree_depth(const tree_node *node) { return pt_tree_node_get_depth(node); }

inline tree_node *tree_parent(const tree_node *node) { return pt_tree_node_parent(node); }

inline uint32_t tree_child_count(const tree_node *node) {
    return pt_tree_node_get_child_count(node);
}

inline tree_node *tree_child(const tree_node *node, uint32_t index) {
    return pt_tree_node_get_child(node, index);
}

inline void *tree_data(const tree_node *node) { return pt_tree_node_get_data(node); }

inline void set_tree_data(tree_node *node, void *data) { pt_tree_node_set_data(node, data); }

inline float ease_value(pt_easing easing, float from, float to, float t) {
    return pt_ease_value(easing, from, to, t);
}

class transition {
  public:
    explicit transition(float value = 0.0f) { pt_transition_init(&value_, value); }

    transition &to(float target, uint32_t duration_ms,
                   pt_easing easing = pt_easing_out_cubic) {
        pt_transition_to(&value_, target, duration_ms, easing);
        return *this;
    }
    bool advance(float dt) { return pt_transition_advance(&value_, dt); }
    transition &set(float value) {
        pt_transition_set(&value_, value);
        return *this;
    }
    [[nodiscard]] float value() const { return value_.value; }
    [[nodiscard]] bool active() const { return pt_transition_active(&value_); }
    [[nodiscard]] pt_transition *raw() { return &value_; }
    [[nodiscard]] const pt_transition *raw() const { return &value_; }

  private:
    pt_transition value_{};
};

} // namespace pt

#endif
