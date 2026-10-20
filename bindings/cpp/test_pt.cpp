/*
 * Headless tests for the header-only C++ binding.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int checks = 0;
int failures = 0;

#define check(expression)                                                                        \
    do {                                                                                         \
        ++checks;                                                                                \
        if (!(expression)) {                                                                     \
            ++failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);           \
        }                                                                                        \
    } while (0)

void on_timer(pt_app *app, void *data) {
    (void)app;
    *static_cast<bool *>(data) = true;
}

bool drop_enter(pt_widget *widget, const char *mime_type, void *data) {
    (void)widget;
    (void)mime_type;
    (void)data;
    return false;
}

void drop_motion(pt_widget *widget, float x, float y, void *data) {
    (void)widget;
    (void)x;
    (void)y;
    (void)data;
}

void drop_leave(pt_widget *widget, void *data) {
    (void)widget;
    (void)data;
}

void drop_receive(pt_widget *widget, const char *mime_type, const char *text, float x, float y,
                  void *data) {
    (void)widget;
    (void)mime_type;
    (void)text;
    (void)x;
    (void)y;
    (void)data;
}

struct offer_capture {
    uint32_t mime_count = 0;
    uint32_t actions = 0;
    size_t finished_size = 0;
    bool still_active = true;
    bool dropped = false;
    std::string selected;
    std::string payload;
};

void offer_stream(pt_widget *widget, pt_drop_stream *raw_stream, pt::drop_stream_state state,
                  const char *chunk, size_t length, void *data) {
    (void)widget;
    auto *capture = static_cast<offer_capture *>(data);
    const pt::drop_stream stream(raw_stream);
    if (state == pt_drop_stream_data) {
        capture->payload.append(chunk, length);
        return;
    }
    capture->finished_size = stream.size();
    capture->still_active = stream.active();
}

const char *offer_enter(pt_widget *widget, pt_drop_offer *raw_offer, void *data) {
    (void)widget;
    auto *capture = static_cast<offer_capture *>(data);
    const pt::drop_offer offer(raw_offer);
    capture->mime_count = offer.mime_count();
    capture->actions = offer.actions();
    capture->selected.clear();
    if (offer.has_mime("text/plain;charset=utf-8")) {
        capture->selected = "text/plain;charset=utf-8";
    } else if (offer.mime_count() > 0) {
        capture->selected = std::string(offer.mime(0));
    }
    return capture->selected.empty() ? nullptr : capture->selected.c_str();
}

void offer_drop(pt_widget *widget, pt_drop_offer *raw_offer, const char *mime_type, float x,
                float y, void *data) {
    (void)widget;
    (void)x;
    (void)y;
    auto *capture = static_cast<offer_capture *>(data);
    const pt::drop_offer offer(raw_offer);
    capture->dropped = true;
    const std::string_view wanted =
        mime_type ? std::string_view(mime_type) : offer.selected_mime();
    const pt::drop_stream stream = offer.read(wanted, 4096, offer_stream, capture);
    if (stream.active()) {
        stream.cancel();
    }
}

void drag_finished(pt_window *window, bool dropped, pt_drag_action action, void *data) {
    (void)window;
    (void)action;
    *static_cast<bool *>(data) = dropped;
}

void on_gesture(pt_window *window, const pt_gesture_event *event, void *data) {
    (void)window;
    (void)event;
    (void)data;
}

void bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    (void)list;
    (void)data;
    char text[32];
    std::snprintf(text, sizeof(text), "row %u", index);
    pt_widget_set_text(row, text);
}

void on_row(pt_widget *list, uint32_t index, void *data) {
    (void)list;
    *static_cast<uint32_t *>(data) = index;
}

void on_combo_text(pt_widget *combobox, const char *text, void *data) {
    (void)combobox;
    *static_cast<std::string *>(data) = text ? text : "";
}

void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column, void *data) {
    (void)table;
    (void)data;
    char text[48];
    std::snprintf(text, sizeof(text), "%u:%u", row, column);
    pt_widget_set_text(cell, text);
}

void on_sort(pt_widget *table, uint32_t column, pt_sort_direction direction, void *data) {
    (void)table;
    (void)direction;
    *static_cast<uint32_t *>(data) = column;
}

void on_tree(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    *static_cast<pt_tree_node **>(data) = node;
}

void on_stylus(pt_window *window, const pt_stylus_event *event, void *data) {
    (void)window;
    (void)event;
    (void)data;
}

uint64_t row_key(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    (void)data;
    return 1000u + row * 10u;
}

const char *row_text(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    (void)data;
    static char text[32];
    std::snprintf(text, sizeof(text), "item %u", row);
    return text;
}

struct sort_capture {
    uint32_t count = 0;
    uint32_t columns[PT_TABLE_MAX_SORT_COLUMNS] = {};
    pt_sort_direction directions[PT_TABLE_MAX_SORT_COLUMNS] = {};
};

void on_sort_columns(pt_widget *table, uint32_t count, const uint32_t *columns,
                     const pt_sort_direction *directions, void *data) {
    (void)table;
    auto *capture = static_cast<sort_capture *>(data);
    capture->count = count;
    for (uint32_t i = 0; i < count && i < PT_TABLE_MAX_SORT_COLUMNS; ++i) {
        capture->columns[i] = columns[i];
        capture->directions[i] = directions[i];
    }
}

struct edit_capture {
    uint32_t row = pt_list_index_none;
    uint32_t column = pt_table_column_none;
    std::string text;
};

struct reorder_capture {
    uint32_t from = pt_table_column_none;
    uint32_t to = pt_table_column_none;
};

struct rename_capture {
    pt_tree_node *node = nullptr;
    std::string text;
};

void on_cell_edited(pt_widget *table, uint32_t row, uint32_t column, const char *text,
                    void *data) {
    (void)table;
    auto *capture = static_cast<edit_capture *>(data);
    capture->row = row;
    capture->column = column;
    capture->text = text ? text : "";
}

void on_columns_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    (void)table;
    auto *capture = static_cast<reorder_capture *>(data);
    capture->from = from;
    capture->to = to;
}

void on_row_drag(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    *static_cast<uint32_t *>(data) = row;
}

void on_tree_rename(pt_widget *tree, pt_tree_node *node, const char *text, void *data) {
    (void)tree;
    auto *capture = static_cast<rename_capture *>(data);
    capture->node = node;
    capture->text = text ? text : "";
}

} // namespace

int main() {
    setenv("PT_HEADLESS", "1", 1);
    unsetenv("PT_REDUCED_MOTION");

    pt::app application("pt.tests.cpp");
    check(application.valid());
    if (!application.valid()) {
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_light);
    application.set_accent_color(pt_rgb(0x4C9AFF));
    application.set_font_family("monospace");
    check(application.theme_color(pt_theme_accent) == pt_rgb(0x4C9AFF));
    check(application.theme_color(pt_theme_control) !=
          application.theme_color(pt_theme_track));
    check(std::fabs(application.theme_metric(pt_theme_metric_radius_small) - 6.0f) <
          0.0001f);
    application.set_theme_metric(pt_theme_metric_chip_height, 30.0f);
    check(std::fabs(application.theme_metric(pt_theme_metric_chip_height) - 30.0f) <
          0.0001f);
    check(!application.reduced_motion());
    application.set_reduced_motion(true);
    check(application.reduced_motion());
    application.set_reduced_motion(false);
    check(application.a11y_status() == pt_a11y_status_disabled);
    const bool a11y_enabled = application.enable_a11y();
    check(a11y_enabled == (application.a11y_status() == pt_a11y_status_active));
    check(application.a11y_status() != pt_a11y_status_disabled);
    application.disable_a11y();
    check(application.a11y_status() == pt_a11y_status_disabled);
    check(std::fabs(pt::ease(pt_easing_linear, 0.5f) - 0.5f) < 0.0001f);
    check(std::fabs(pt::ease_value(pt_easing_linear, 10.0f, 30.0f, 0.25f) - 15.0f) <
          0.0001f);

    pt::window window(application, "cpp binding", 420, 320);
    check(window.valid());
    check(window.width() == 420);
    check(window.height() == 320);
    check(window.scale() > 0.0f);
    check(window.theme_color(pt_theme_background) ==
          application.theme_color(pt_theme_background));
    window.set_theme_color(pt_theme_background, pt_rgb(0x102030));
    window.set_theme_metric(pt_theme_metric_focus_width, 3.0f);
    check(window.theme_color(pt_theme_background) == pt_rgb(0x102030));
    check(std::fabs(window.theme_metric(pt_theme_metric_focus_width) - 3.0f) < 0.0001f);
    check(application.theme_color(pt_theme_background) != pt_rgb(0x102030));
    window.clear_theme_overrides();
    check(window.theme_color(pt_theme_background) ==
          application.theme_color(pt_theme_background));

    pt::widget root = window.root();
    check(root.valid());
    pt::widget column = pt::column(root.get());
    column.set_gap(8.0f).set_padding(12.0f, 12.0f, 12.0f, 12.0f);

    pt::widget title = pt::label(column.get(), "hello");
    title.set_font_size(20.0f).set_font_weight(pt_font_weight_bold).set_wrap(true);
    check(title.text() == "hello");
    title.set_text("changed");
    check(title.text() == "changed");
    title.set_text("styled title");
    pt_text_span title_span{};
    title_span.start = 0;
    title_span.length = 6;
    title_span.weight = pt_font_weight_bold;
    title_span.flags = pt_text_span_underline | pt_text_span_has_weight;
    title.set_text_spans({title_span});
    check(title.text_span_count() == 1);
    const std::optional<pt_text_span> found = title.text_span(0);
    check(found.has_value());
    check(found && found->length == 6 && found->weight == pt_font_weight_bold);
    check(found && (found->flags & pt_text_span_underline) != 0);
    check(!title.text_span(1).has_value());
    title.clear_text_spans();
    check(title.text_span_count() == 0);

    pt::widget button = pt::button(column.get(), "Press");
    button.set_button_variant(pt_button_primary).on_click(nullptr);
    pt::widget entry = pt::entry(column.get(), "placeholder");
    entry.set_password(true).on_change(nullptr).on_submit(nullptr);
    entry.set_input_purpose(pt_input_purpose_email);
    check(entry.input_purpose() == pt_input_purpose_email);

    pt::widget toggle = pt::toggle(column.get());
    toggle.set_checked(true);
    check(toggle.checked());
    toggle.set_checked(false);
    check(!toggle.checked());

    pt::widget checkbox = pt::checkbox(column.get(), "Subscribe");
    checkbox.set_checked(true);
    check(checkbox.checked());
    checkbox.on_checked(nullptr);

    pt::widget radio_a = pt::radio(column.get(), "Comfort");
    pt::widget radio_b = pt::radio(column.get(), "Compact");
    radio_a.set_checked(true);
    check(radio_a.checked());
    radio_b.set_checked(true);
    check(radio_b.checked());
    check(!radio_a.checked());

    pt::widget slider = pt::slider(column.get());
    slider.set_value(0.5f);
    check(std::fabs(slider.value() - 0.5f) < 0.0001f);
    slider.on_value_change(nullptr);

    pt::widget progress = pt::progress(column.get());
    progress.set_progress(0.625f);
    check(std::fabs(progress.progress() - 0.625f) < 0.0001f);
    progress.set_indeterminate(true);
    check(progress.indeterminate());
    check(pt::spinner(column.get()).valid());

    pt::widget segmented = pt::segmented(column.get(), {"Dense", "Cozy", "Compact"});
    segmented.select_segment(1).on_segment_change(nullptr);
    check(segmented.segment() == 1);

    entry.set_text("alpha beta gamma").select_all();
    check((entry.selection() == std::pair<uint32_t, uint32_t>{0, 16}));
    entry.set_selection(10, 6);
    check((entry.selection() == std::pair<uint32_t, uint32_t>{6, 10}));
    check(!entry.undo());
    check(!entry.redo());
    check(entry.drag_enabled());
    entry.set_drag_enabled(false);
    check(!entry.drag_enabled());
    entry.set_drag_enabled(true);

    const pt_drop_target target = {drop_enter, drop_motion, drop_leave, drop_receive,
                                   nullptr, nullptr, nullptr};
    entry.set_drop_target(target);
    check(!entry.drop_hover());
    entry.clear_drop_target();

    /* Structured targets use the offer callbacks; headless routing never runs
     * them, so registering and clearing must both be side-effect free. */
    offer_capture incoming;
    entry.set_drop_target(offer_enter, offer_drop, drop_motion, drop_leave, &incoming);
    check(!entry.drop_hover());
    check(!incoming.dropped);
    entry.clear_drop_target();
    check(!entry.drop_hover());
    check(incoming.mime_count == 0);

    window.on_gesture(on_gesture).set_long_press_delay(320);
    check(window.momentum_scrolling());
    window.set_momentum_scrolling(false);
    check(!window.momentum_scrolling());
    window.set_momentum_scrolling(true);
    check(window.popup_policy() == pt_popup_policy_auto);
    window.set_popup_policy(pt_popup_policy_window);
    check(window.popup_policy() == pt_popup_policy_window);
    window.set_popup_policy(pt_popup_policy_auto);
    check(!window.drag_active());
    check(window.drag_timeout() == 0);
    window.set_drag_timeout(1500);
    check(window.drag_timeout() == 1500);
    window.cancel_drag();
    check(!window.drag_active());
    window.set_drag_timeout(0);

    pt::drag_data plain_type{};
    plain_type.mime_type = "text/plain;charset=utf-8";
    plain_type.data = "structured drag";
    plain_type.size = 0;
    const pt::drag_data uri_type{"text/uri-list", "file:///tmp/a.txt\r\n", 0};
    const pt::drag_data types[] = {plain_type, uri_type};
    const std::string error_before_drag = pt::last_error();
    check(!window.begin_drag(types)); // headless has no drag source
    check(!window.begin_drag({plain_type, uri_type}));
    check(!window.begin_drag(std::span<const pt::drag_data>()));
    check(!window.drag_active());
    check(window.drag_result() == pt_drag_action_none);
    check(pt::last_error() == error_before_drag);
    bool drag_reported = false;
    window.set_drag_finished_callback(drag_finished, &drag_reported);
    window.cancel_drag(); // no source drag was started, so nothing notifies
    check(!drag_reported);
    window.set_drag_finished_callback(nullptr);
    check(window.drag_result() == pt_drag_action_none);

    const pt::drop_offer empty_offer;
    check(!empty_offer.valid());
    check(empty_offer.get() == nullptr);
    check(empty_offer.mime_count() == 0);
    check(empty_offer.mime(0).empty());
    check(empty_offer.mime(9).empty());
    check(!empty_offer.has_mime("text/plain"));
    check(empty_offer.selected_mime().empty());
    check(empty_offer.actions() == 0u);
    const pt::drop_stream empty_stream =
        empty_offer.read("text/plain", 64, offer_stream, &incoming);
    check(!empty_stream.valid());
    check(!empty_stream.active());
    check(empty_stream.size() == 0);
    check(empty_stream.mime().empty());
    empty_stream.cancel();

    const pt::drop_offer null_offer(nullptr);
    check(!null_offer.read("").valid());
    check(null_offer.read("text/plain").get() == nullptr);
    check(!null_offer.read("text/uri-list", 0, offer_stream, &incoming).active());
    const pt::drop_stream null_stream(nullptr);
    null_stream.cancel();
    check(!null_stream.valid());
    /* No offer, drop or stream callback runs without a compositor. */
    check(incoming.payload.empty());
    check(incoming.finished_size == 0);

    pt::widget scroll = pt::scroll(column.get());
    scroll.set_scroll_offset(24.0f);
    check(scroll.scroll_offset() >= 0.0f);
    check(scroll.max_scroll_offset() >= 0.0f);

    pt::widget notes = pt::text_area(column.get(), "Notes");
    check(notes.valid());
    check(notes.kind() == pt_widget_text_area);
    notes.set_text("alpha\nbeta").set_grow_to_content(true);
    check(notes.grow_to_content());
    notes.set_grow_to_content(false);
    notes.on_change(nullptr);
    notes.set_placeholder("Write");
    notes.select_all();
    check((notes.selection() == std::pair<uint32_t, uint32_t>{0, 10}));
    notes.set_selection(6, 3);
    check((notes.selection() == std::pair<uint32_t, uint32_t>{3, 6}));
    check(!notes.undo());
    check(!notes.redo());
    notes.scroll_to_caret();
    check(notes.a11y_role() == pt_a11y_role_text_frame);

    pt::widget wrapping = pt::wrap(column.get());
    wrapping.set_wrap_gap(8.0f, 6.0f);
    check(wrapping.kind() == pt_widget_wrap);
    pt::widget tile = pt::card(wrapping.get());
    tile.set_width(80.0f).set_height(40.0f).set_align_self(pt_align_start);

    pt::widget gridded = pt::grid(column.get(), 3);
    gridded.set_grid_gap(10.0f, 12.0f);
    check(gridded.grid_columns() == 3);
    gridded.set_grid_columns(2);
    check(gridded.grid_columns() == 2);
    pt::widget wide = pt::card(gridded.get());
    wide.set_grid_span(2);
    check(wide.grid_span() == 2);
    wide.set_grid_span(1);
    check(wide.grid_span() == 1);

    pt::widget rows = pt::list_view(column.get());
    check(rows.kind() == pt_widget_list);
    rows.set_row_height(36.0f);
    check(std::fabs(rows.row_height() - 36.0f) < 0.0001f);
    rows.set_row_count(50).refresh();
    check(rows.row_count() == 50);
    check(rows.selected_row() == pt_list_index_none);
    uint32_t active = pt_list_index_none;
    rows.on_bind_row(bind_row).on_row_select(on_row, &active).on_row_activate(on_row, &active);
    rows.set_selected_row(7);
    check(rows.selected_row() == 7);
    rows.scroll_to_row(49).refresh();
    check(active == pt_list_index_none);
    rows.set_row_key_callback(row_key);
    check(rows.selected_key() == 1070);
    rows.set_row_count(50);
    check(rows.selected_row() == 7);
    rows.set_selected_row(3);
    check(rows.selected_key() == 1030);
    check(!rows.set_selected_key(999999));
    check(rows.selected_row() == 3);
    check(rows.set_selected_key(1040));
    check(rows.selected_row() == 4);
    check(rows.selected_key() == 1040);
    rows.refresh_rows(0, 4);
    check(rows.row_count() == 50);
    check(rows.set_selected_key(pt_row_key_none));
    check(rows.selected_row() == pt_list_index_none);
    check(rows.selected_key() == pt_row_key_none);
    check(rows.set_selected_key(1030));
    rows.set_row_count(2);
    check(rows.selected_row() == pt_list_index_none);
    rows.set_selected_row(1);
    check(rows.selected_key() == 1010);
    rows.set_row_count(2);
    check(rows.selected_row() == 1);
    rows.set_row_text_callback(row_text);
    rows.set_selected_row(pt_list_index_none);
    rows.set_row_count(2);
    check(rows.selected_row() == pt_list_index_none);
    check(rows.a11y_role() == pt_a11y_role_list);
    rows.set_row_count(8);
    check(rows.list_selection_count() == 0);
    check(rows.list_selection().empty());
    rows.list_select_range(2, 4);
    check(rows.list_selection_count() == 3);
    check(rows.list_selection() == std::vector<uint32_t>({2, 3, 4}));
    check(rows.is_list_row_selected(3));
    check(!rows.is_list_row_selected(5));
    check(rows.selected_row() == 2);
    rows.list_select_toggle(6);
    check(rows.list_selection() == std::vector<uint32_t>({2, 3, 4, 6}));
    rows.list_select_toggle(3);
    check(rows.list_selection() == std::vector<uint32_t>({2, 4, 6}));
    rows.clear_list_selection();
    check(rows.list_selection_count() == 0);
    check(rows.selected_row() == pt_list_index_none);

    pt::widget icon = pt::icon(column.get(), pt_icon_search);
    check(icon.kind() == pt_widget_icon);
    check(icon.icon() == pt_icon_search);
    check(std::fabs(icon.icon_size() - 20.0f) < 0.0001f);
    icon.set_icon_size(28.0f).set_icon(pt_icon_bell);
    check(icon.icon() == pt_icon_bell);
    check(std::fabs(icon.icon_size() - 28.0f) < 0.0001f);
    check(icon.a11y_role() == pt_a11y_role_icon);
    pt::widget icon_button = pt::icon_button(column.get(), pt_icon_plus, "Add item");
    check(icon_button.a11y_name() == "Add item");
    check(icon_button.a11y_role() == pt_a11y_role_push_button);
    button.set_icon(pt_icon_trash);
    check(button.icon() == pt_icon_trash);
    button.set_busy(true);
    check(button.busy());
    button.set_busy(false);
    check(!button.busy());
    button.set_invalid(true);
    check(button.invalid());
    button.set_invalid(false);
    check(!button.invalid());

    pt::widget chip = pt::chip(column.get(), "filter");
    check(chip.kind() == pt_widget_chip);
    chip.set_chip_icon(pt_icon_search).set_chip_removable(true);
    check(chip.chip_removable());
    check(!chip.chip_selected());
    chip.set_chip_selected(true);
    check(chip.chip_selected());
    chip.on_chip_click(nullptr).on_chip_remove(nullptr);
    pt::widget skeleton = pt::skeleton(column.get());
    check(skeleton.kind() == pt_widget_skeleton);

    pt::widget table = pt::table(column.get());
    check(table.kind() == pt_widget_table);
    check(table.a11y_role() == pt_a11y_role_table);
    table.set_column_count(3).set_header_height(34.0f);
    check(table.column_count() == 3);
    table.set_column_title(0, "Name").set_column_title(1, "Kind");
    table.set_column_width(0, 160.0f).set_column_align(1, pt_text_align_center);
    table.set_column_resizable(2, false);
    check(std::fabs(table.column_width(0) - 160.0f) < 0.0001f);
    table.set_table_row_count(40).set_table_row_height(32.0f);
    check(table.table_row_count() == 40);
    check(std::fabs(table.table_row_height() - 32.0f) < 0.0001f);
    uint32_t sorted_column = pt_table_column_none;
    uint32_t table_active = pt_list_index_none;
    table.on_bind_cell(bind_cell)
        .on_table_select(on_row, &table_active)
        .on_table_activate(on_row, &table_active)
        .on_table_sort(on_sort, &sorted_column);
    table.set_table_selected(3);
    check(table.table_selected() == 3);
    table.set_sort(1, pt_sort_ascending);
    uint32_t sort_column = pt_table_column_none;
    check(table.sort(&sort_column) == pt_sort_ascending);
    check(sort_column == 1);
    check(!table.column_hidden(1));
    table.set_column_hidden(1, true);
    check(table.column_hidden(1));
    table.set_column_hidden(1, false);
    check(!table.column_hidden(1));
    check(!table.column_hidden(7));
    sort_capture sort_state;
    table.on_sort_columns(on_sort_columns, &sort_state);
    table.set_sort_columns({{2, pt_sort_descending}, {0, pt_sort_ascending}});
    std::vector<pt::sort_key> sort_keys = table.sort_columns();
    check(sort_keys.size() == 2);
    if (sort_keys.size() == 2) {
        check(sort_keys[0].column == 2);
        check(sort_keys[0].direction == pt_sort_descending);
        check(sort_keys[1].column == 0);
        check(sort_keys[1].direction == pt_sort_ascending);
    }
    uint32_t primary_column = pt_table_column_none;
    check(table.sort(&primary_column) == pt_sort_descending);
    check(primary_column == 2);
    table.set_sort_columns({{1, pt_sort_ascending}, {1, pt_sort_descending},
                            {7, pt_sort_ascending}});
    sort_keys = table.sort_columns();
    check(sort_keys.size() == 1);
    if (!sort_keys.empty()) {
        check(sort_keys[0].column == 1);
    }
    table.set_table_row_key_callback(row_key);
    check(table.table_selected() == 3);
    check(table.table_selected_key() == 1030);
    table.set_table_row_count(40);
    check(table.table_selected() == 3);
    table.set_table_selected(0);
    check(table.table_selected_key() == 1000);
    check(!table.set_table_selected_key(999999));
    check(table.table_selected() == 0);
    check(table.set_table_selected_key(1050));
    check(table.table_selected() == 5);
    check(table.table_selected_key() == 1050);
    check(table.set_table_selected_key(pt_row_key_none));
    check(table.table_selected() == pt_list_index_none);
    check(table.set_table_selected_key(1010));
    check(table.table_selected() == 1);
    table.table_refresh_rows(0, 3);
    check(table.table_row_count() == 40);
    table.set_table_row_text_callback(row_text);
    table.set_table_row_key_callback(nullptr);
    table.set_table_selected(3);
    check(!table.table_editable());
    table.set_table_editable(true);
    check(table.table_editable());
    edit_capture edit_state;
    table.on_cell_edited(on_cell_edited, &edit_state);
    check(!table.table_editing());
    check(table.begin_cell_edit(1, 0));
    check(table.table_editing());
    check(table.commit_cell_edit());
    check(!table.table_editing());
    check(edit_state.row == 1);
    check(edit_state.column == 0);
    table.select_range(1, 3);
    check(table.table_selection_count() == 3);
    check(table.is_row_selected(2));
    check(table.table_selection() == std::vector<uint32_t>({1, 2, 3}));
    table.select_toggle(5);
    check(table.table_selection() == std::vector<uint32_t>({1, 2, 3, 5}));
    check(table.table_selected() == 5);
    table.select_toggle(2);
    check(table.table_selection() == std::vector<uint32_t>({1, 3, 5}));
    table.clear_table_selection();
    check(table.table_selection_count() == 0);
    check(table.table_selected() == pt_list_index_none);
    reorder_capture reorder_state;
    uint32_t dragged_row = pt_list_index_none;
    table.on_columns_reordered(on_columns_reordered, &reorder_state)
        .on_row_drag(on_row_drag, &dragged_row);
    check(!table.columns_reorderable());
    table.set_columns_reorderable(true);
    check(table.columns_reorderable());
    check(table.move_column(0, 2));
    check(reorder_state.from == 0);
    check(reorder_state.to == 2);
    table.scroll_to_column(2).table_scroll_to_row(39).table_refresh();
    table.set_table_row_count(2);
    check(table.table_selected() == pt_list_index_none);

    pt::widget tree = pt::tree(column.get());
    check(tree.kind() == pt_widget_tree);
    check(tree.a11y_role() == pt_a11y_role_tree);
    pt::tree_node *alpha = tree.add_tree_node(nullptr, "alpha");
    pt::tree_node *beta = tree.add_tree_node(nullptr, "beta");
    pt::tree_node *child = tree.add_tree_node(alpha, "child");
    check(tree.tree_root_count() == 2);
    check(pt::tree_text(alpha) == "alpha");
    check(pt::tree_has_children(alpha));
    check(pt::tree_child_count(alpha) == 1);
    check(pt::tree_child(alpha, 0) == child);
    check(pt::tree_parent(child) == alpha);
    check(pt::tree_depth(child) == 1);
    int payload = 7;
    pt::set_tree_data(child, &payload);
    check(pt::tree_data(child) == &payload);
    check(tree.tree_visible_count() == 2);
    pt::set_tree_expanded(alpha, true);
    check(tree.tree_visible_count() == 3);
    check(tree.tree_visible_node(1) == child);
    pt::tree_node *selected_node = nullptr;
    tree.on_tree_select(on_tree, &selected_node).on_tree_activate(on_tree, &selected_node);
    tree.set_tree_selected(child);
    check(tree.tree_selected() == child);
    tree.scroll_to_tree_node(beta).tree_refresh();
    pt::set_tree_text(beta, "beta renamed");
    check(pt::tree_text(beta) == "beta renamed");
    check(!tree.tree_renamable());
    check(!tree.tree_renaming());
    tree.set_tree_renamable(true);
    check(tree.tree_renamable());
    rename_capture rename_state;
    tree.on_tree_rename(on_tree_rename, &rename_state);
    check(tree.begin_tree_rename(beta));
    check(tree.tree_renaming());
    check(tree.commit_tree_rename());
    check(!tree.tree_renaming());
    check(rename_state.node == beta);
    check(rename_state.text == "beta renamed");
    check(!tree.commit_tree_rename());
    check(tree.tree_selection_count() == 1);
    check(tree.tree_selection() == std::vector<pt::tree_node *>({beta}));
    tree.tree_select_range(0, 1);
    check(tree.tree_selection_count() == 2);
    check(tree.tree_selection() == std::vector<pt::tree_node *>({alpha, child}));
    check(tree.is_tree_node_selected(child));
    check(!tree.is_tree_node_selected(beta));
    check(tree.tree_selected() == alpha);
    tree.tree_select_toggle(beta);
    check(tree.tree_selection_count() == 3);
    check(tree.tree_selected() == beta);
    tree.tree_select_toggle(child);
    check(tree.tree_selection_count() == 2);
    check(tree.tree_selection() == std::vector<pt::tree_node *>({alpha, beta}));
    tree.clear_tree_selection();
    check(tree.tree_selection_count() == 0);
    check(tree.tree_selected() == nullptr);
    tree.clear_tree();
    check(tree.tree_root_count() == 0);

    pt::widget tabs = pt::tabs(column.get());
    check(tabs.kind() == pt_widget_tabs);
    pt::widget page_a = tabs.add_tab("General");
    pt::widget page_b = tabs.add_tab("Advanced");
    check(page_a.kind() == pt_widget_tab_page);
    check(page_b.kind() == pt_widget_tab_page);
    check(tabs.tab_count() == 2);
    check(tabs.tab_title(0) == "General");
    tabs.set_tab_title(1, "Options");
    check(tabs.tab_title(1) == "Options");
    check(tabs.active_tab() == 0);
    tabs.set_active_tab(1);
    check(tabs.active_tab() == 1);
    uint32_t changed_tab = pt_list_index_none;
    tabs.on_tab_change(on_row, &changed_tab).set_tab_strip_height(40.0f);
    tabs.remove_tab(0);
    check(tabs.tab_count() == 1);
    check(tabs.active_tab() == 0);
    check(tabs.tab_page(1).get() == nullptr);

    pt::widget sidebar = pt::sidebar(column.get());
    check(sidebar.kind() == pt_widget_sidebar);
    pt::widget nav_home = sidebar.add_sidebar_item(pt_icon_home, "Home");
    check(nav_home.kind() == pt_widget_sidebar_item);
    sidebar.add_sidebar_item(pt_icon_folder, "Files");
    check(sidebar.sidebar_item_count() == 2);
    check(sidebar.active_sidebar_item() == pt_list_index_none);
    sidebar.set_active_sidebar_item(1);
    check(sidebar.active_sidebar_item() == 1);
    sidebar.set_sidebar_header("Workspace");
    check(sidebar.sidebar_header() == "Workspace");
    check(!sidebar.sidebar_compact());
    sidebar.set_sidebar_compact(true);
    check(sidebar.sidebar_compact());
    uint32_t picked_item = pt_list_index_none;
    sidebar.on_sidebar_select(on_row, &picked_item);

    pt::widget colour = pt::combobox(column.get());
    check(colour.kind() == pt_widget_combobox);
    check(colour.a11y_role() == pt_a11y_role_combo_box);
    check(colour.item_count() == 0);
    check(colour.combo_selected() == pt_list_index_none);
    check(colour.combo_text().empty());
    check(colour.combo_placeholder().empty());
    check(colour.combo_mode() == pt_combobox_select_only);
    check(!colour.is_popup_open());
    check(std::fabs(colour.popup_max_height() - 260.0f) < 0.0001f);
    colour.set_combo_placeholder("Colour").add_item("Red").add_item("Green").insert_item(
        1, "Yellow");
    check(colour.item_count() == 3);
    check(colour.item_text(0) == "Red");
    check(colour.item_text(1) == "Yellow");
    check(colour.combo_placeholder() == "Colour");
    colour.set_item_text(2, "Blue");
    check(colour.item_text(2) == "Blue");
    int colour_payload = 0x1234;
    colour.set_item_data(2, &colour_payload);
    check(colour.item_data(2) == &colour_payload);
    check(colour.item_data(0) == nullptr);
    check(!colour.is_item_disabled(1));
    colour.set_item_disabled(1, true);
    check(colour.is_item_disabled(1));
    colour.set_item_disabled(1, false);
    uint32_t chosen = pt_list_index_none;
    std::string typed;
    colour.on_combo_change(on_row, &chosen).on_combo_text_change(on_combo_text, &typed);
    colour.set_combo_selected(2);
    check(colour.combo_selected() == 2);
    check(colour.combo_text() == "Blue");
    check(chosen == pt_list_index_none);
    colour.set_combo_mode(pt_combobox_filterable);
    check(colour.combo_mode() == pt_combobox_filterable);
    colour.set_combo_text("gr");
    check(colour.combo_text() == "gr");
    check(typed.empty());
    colour.open_popup();
    check(colour.is_popup_open());
    colour.set_popup_max_height(120.0f);
    check(std::fabs(colour.popup_max_height() - 120.0f) < 0.0001f);
    colour.close_popup();
    check(!colour.is_popup_open());
    colour.remove_item(0);
    check(colour.item_count() == 2);
    check(colour.item_text(0) == "Yellow");
    colour.clear_items();
    check(colour.item_count() == 0);
    check(colour.combo_selected() == pt_list_index_none);

    const std::string uri_list = "file:///tmp/a.txt\r\nfile:///tmp/b%20c.txt\r\n";
    check(pt::uri_list_count(uri_list) == 2);
    check(pt::uri_list_get(uri_list, 0) == "file:///tmp/a.txt");
    check(pt::uri_list_get(uri_list, 1) == "file:///tmp/b%20c.txt");
    check(pt::uri_list_count("") == 0);
    check(!window.begin_drag({pt::drag_data{"text/uri-list", uri_list.c_str(), 0}}));
    pt_color drag_pixels[4] = {pt_rgb(0xFF0000), pt_rgb(0x00FF00), pt_rgb(0x0000FF),
                               pt_rgb(0xFFFFFF)};
    window.set_drag_image(drag_pixels, 2, 2);
    window.clear_drag_image();
    window.on_stylus(on_stylus);
    check((pt_a11y_state_expandable & pt_a11y_state_expandable) != 0);
    check(pt_icon_warning > pt_icon_none);

    pt::transition fade(0.0f);
    fade.to(10.0f, 100, pt_easing_linear);
    check(fade.active());
    fade.advance(0.05f);
    check(std::fabs(fade.value() - 5.0f) < 0.01f);
    fade.to(20.0f, 100, pt_easing_linear);
    fade.advance(0.05f);
    check(fade.value() > 5.0f);
    fade.set(30.0f);
    check(!fade.active());
    check(std::fabs(fade.value() - 30.0f) < 0.0001f);
    check(fade.raw() != nullptr);

    pt::widget tip_target = pt::button(column.get(), "Tip");
    tip_target.set_tooltip("Helpful hint").set_tooltip_delay(0);
    check(tip_target.a11y_role() == pt_a11y_role_push_button);
    check(tip_target.a11y_name() == "Tip");
    tip_target.set_a11y_name("Tip button").set_a11y_description("Shows a tooltip");
    check(tip_target.a11y_name() == "Tip button");
    check(tip_target.a11y_description() == "Shows a tooltip");
    check((tip_target.a11y_state() & pt_a11y_state_focusable) != 0);
    slider.set_a11y_name("Amount");
    check(slider.a11y_value().has_value());
    const std::optional<pt::a11y_text_info> notes_text = notes.a11y_text();
    check(notes_text.has_value());
    if (notes_text) {
        check(notes_text->text == "alpha\nbeta");
        check(notes_text->selection_start == 3);
        check(notes_text->selection_end == 6);
    }
    check(!entry.a11y_text().has_value()); // password text is not exposed

    pt::menu file_menu(window);
    check(file_menu.valid());
    file_menu.add_item("Open", "Ctrl+O").add_separator().add_item("Quit", "Ctrl+Q");
    file_menu.show_at(20.0f, 80.0f);
    check(file_menu.is_open());
    file_menu.dismiss();
    check(!file_menu.is_open());

    pt::dialog dialog(window, "Delete", "This cannot be undone.");
    check(dialog.valid());
    dialog.add_action("Cancel", false).add_action("Delete", true).on_close(nullptr);
    dialog.show();
    check(dialog.is_open());
    dialog.set_title("Delete file").set_message("Updated message");
    dialog.close();
    check(!dialog.is_open());

    pt::toast notice = window.show_toast("Saved", pt_toast_success, 0);
    check(notice.valid());
    check(notice.visible());
    notice.dismiss();
    check(!notice.visible());

    check(window.set_clipboard("copied text"));
    check(window.clipboard() == "copied text");

    pt::widget card = pt::card(column.get());
    card.set_radius(12.0f).set_background(pt_rgb(0x161D28)).set_gap(4.0f);
    pt::widget row = pt::row(card.get());
    row.box_align(pt_align_center).box_justify(pt_justify_center);
    pt::spacer(row.get());
    pt::divider(row.get());

    bool fired = false;
    const uint64_t timer = pt_app_add_timeout(application.get(), 5, on_timer, &fired);
    check(timer != 0);
    window.show();
    check(application.run_for(80) == 0);
    check(fired);
    check(window.is_open());
    check(application.get() != nullptr);

    const int result = failures == 0 ? 0 : 1;
    std::printf("cpp binding: %d checks, %d failure(s)\n", checks, failures);
    return result;
}
