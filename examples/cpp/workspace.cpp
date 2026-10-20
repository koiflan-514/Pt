/*
 * A tabbed file workspace with a sortable table and a tree view.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace {

struct file_row {
    std::string name;
    std::string kind;
    std::string size;
    std::string modified;
};

struct workspace_state {
    std::vector<file_row> files;
    std::vector<uint32_t> order;
    pt_widget *status = nullptr;
    pt_widget *table = nullptr;
    uint32_t sort_column = 0;
    pt_sort_direction sort_direction = pt_sort_ascending;
};

void update_status(workspace_state *state, const char *prefix, const file_row &file) {
    char text[192];
    std::snprintf(text, sizeof(text), "%s %s · %s · %s", prefix, file.name.c_str(),
                  file.kind.c_str(), file.size.c_str());
    pt_widget_set_text(state->status, text);
}

void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column, void *data) {
    (void)table;
    auto *state = static_cast<workspace_state *>(data);
    if (row >= state->order.size()) {
        return;
    }
    const file_row &file = state->files[state->order[row]];
    const char *text = column == 0   ? file.name.c_str()
                       : column == 1 ? file.kind.c_str()
                       : column == 2 ? file.size.c_str()
                                     : file.modified.c_str();
    pt_widget_set_text(cell, text);
    if (column == 0) {
        pt_widget_set_icon(cell, pt_icon_document);
    }
}

void sort_rows(workspace_state *state) {
    std::iota(state->order.begin(), state->order.end(), 0u);
    if (state->sort_direction == pt_sort_none) {
        return;
    }
    const bool descending = state->sort_direction == pt_sort_descending;
    const uint32_t column = state->sort_column;
    std::stable_sort(state->order.begin(), state->order.end(),
                     [state, column, descending](uint32_t left, uint32_t right) {
                         const file_row &a = state->files[left];
                         const file_row &b = state->files[right];
                         const std::string &first = column == 0   ? a.name
                                                    : column == 1 ? a.kind
                                                    : column == 2 ? a.size
                                                                  : a.modified;
                         const std::string &second = column == 0   ? b.name
                                                     : column == 1 ? b.kind
                                                     : column == 2 ? b.size
                                                                   : b.modified;
                         return descending ? first > second : first < second;
                     });
}

void on_sort(pt_widget *table, uint32_t column, pt_sort_direction direction, void *data) {
    auto *state = static_cast<workspace_state *>(data);
    state->sort_column = column;
    state->sort_direction = direction;
    sort_rows(state);
    pt_table_refresh(table);
    static const char *const labels[] = {"Name", "Kind", "Size", "Modified"};
    char text[128];
    std::snprintf(text, sizeof(text), "%zu files · sorted by %s (%s)", state->files.size(),
                  column < 4 ? labels[column] : "?",
                  direction == pt_sort_ascending    ? "ascending"
                  : direction == pt_sort_descending ? "descending"
                                                     : "unsorted");
    pt_widget_set_text(state->status, text);
}

void on_select(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    auto *state = static_cast<workspace_state *>(data);
    if (row < state->order.size()) {
        update_status(state, "Selected", state->files[state->order[row]]);
    }
}

void on_activate(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    auto *state = static_cast<workspace_state *>(data);
    if (row < state->order.size()) {
        update_status(state, "Opened", state->files[state->order[row]]);
    }
}

void on_tree_select(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    auto *state = static_cast<workspace_state *>(data);
    char text[160];
    std::snprintf(text, sizeof(text), "Tree: %s (depth %u)", pt::tree_text(node).c_str(),
                  pt::tree_depth(node));
    pt_widget_set_text(state->status, text);
}

void on_cell_edited(pt_widget *table, uint32_t row, uint32_t column, const char *text,
                    void *data) {
    auto *state = static_cast<workspace_state *>(data);
    if (row >= state->order.size()) {
        return;
    }
    file_row &file = state->files[state->order[row]];
    std::string &target = column == 0   ? file.name
                          : column == 1 ? file.kind
                          : column == 2 ? file.size
                                        : file.modified;
    target = text ? text : "";
    pt_table_refresh(table);
    char message[192];
    std::snprintf(message, sizeof(message), "Edited %s · column %u", file.name.c_str(), column);
    pt_widget_set_text(state->status, message);
}

void on_columns_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    (void)table;
    auto *state = static_cast<workspace_state *>(data);
    char message[96];
    std::snprintf(message, sizeof(message), "Column moved from %u to %u", from, to);
    pt_widget_set_text(state->status, message);
}

void on_row_drag(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    auto *state = static_cast<workspace_state *>(data);
    char message[96];
    std::snprintf(message, sizeof(message), "Dragging row %u", row);
    pt_widget_set_text(state->status, message);
}

void on_tree_rename(pt_widget *tree, pt_tree_node *node, const char *text, void *data) {
    (void)tree;
    (void)node;
    auto *state = static_cast<workspace_state *>(data);
    char message[160];
    std::snprintf(message, sizeof(message), "Renamed node to %s", text);
    pt_widget_set_text(state->status, message);
}

void toggle_chip(pt_widget *chip, void *data) {
    (void)data;
    pt_chip_set_selected(chip, !pt_chip_get_selected(chip));
}

} // namespace

int main() {
    pt::app app("dev.pt.examples.workspace.cpp");
    if (!app.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    app.set_color_scheme(pt_color_scheme_dark);

    pt::window window(app, "Pt Workspace", 920, 640);
    if (!window.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    // Window-scoped token override; the app-level theme stays untouched.
    window.set_theme_metric(pt_theme_metric_radius_medium, 12.0f);
    pt::widget root = window.root();
    root.set_padding(24.0f, 24.0f, 24.0f, 24.0f).set_gap(14.0f);

    pt::widget title = pt::label(root.get(), "Workspace");
    title.set_font_size(26.0f).set_font_weight(pt_font_weight_semibold);
    pt::widget subtitle = pt::label(root.get(), "A table and a tree inside a tabbed shell.");
    subtitle.set_font_size(13.0f);

    pt::widget tabs = pt::tabs(root.get());
    tabs.set_grow(1.0f);
    pt::widget table_page = tabs.add_tab("Files");
    table_page.set_padding(12.0f, 0.0f, 0.0f, 0.0f).set_gap(8.0f);
    pt::widget tree_page = tabs.add_tab("Tree");
    tree_page.set_padding(12.0f, 0.0f, 0.0f, 0.0f).set_gap(8.0f);

    workspace_state state;
    state.files = {
        {"main.c", "C source", "18 KB", "Today 09:42"},
        {"table.c", "C source", "42 KB", "Today 09:12"},
        {"tree.c", "C source", "31 KB", "Yesterday"},
        {"navigation.c", "C source", "27 KB", "Yesterday"},
        {"README.md", "Markdown", "11 KB", "Today 08:30"},
        {"bindings.md", "Markdown", "32 KB", "Oct 13"},
        {"icons.svg", "Vector", "9 KB", "Oct 11"},
        {"logo.svg", "Vector", "6 KB", "Oct 10"},
        {"pt.h", "Header", "8 KB", "Today 09:40"},
        {"interaction.h", "Header", "12 KB", "Oct 14"},
        {"theme.json", "Data", "3 KB", "Oct 09"},
        {"release.tar.gz", "Archive", "1.0 MB", "Oct 08"},
        {"session.log", "Log", "128 KB", "Today 10:02"},
        {"build.log", "Log", "96 KB", "Yesterday"},
    };
    state.order.resize(state.files.size());

    pt::widget chips = pt::row(table_page.get());
    chips.set_gap(8.0f);
    pt::chip(chips.get(), "Source")
        .set_chip_icon(pt_icon_document)
        .set_chip_removable(true);
    pt::widget active_chip = pt::chip(chips.get(), "Edited this week");
    active_chip.set_chip_selected(true).on_chip_click(toggle_chip);
    pt::skeleton(chips.get()).set_height(12.0f);

    pt::widget table = pt::table(table_page.get());
    table.set_grow(1.0f);
    table.set_column_count(4);
    table.set_column_title(0, "Name").set_column_width(0, 240.0f);
    table.set_column_title(1, "Kind").set_column_width(1, 140.0f);
    table.set_column_title(2, "Size").set_column_width(2, 100.0f);
    table.set_column_title(3, "Modified").set_column_width(3, 160.0f);
    table.set_column_align(2, pt_text_align_end);
    table.set_column_resizable(0, true).set_column_resizable(1, true);
    table.set_column_resizable(2, true).set_column_resizable(3, true);
    table.set_table_row_height(34.0f);
    table.set_table_row_count(static_cast<uint32_t>(state.files.size()));
    table.on_bind_cell(bind_cell, &state);
    table.on_table_select(on_select, &state);
    table.on_table_activate(on_activate, &state);
    table.on_table_sort(on_sort, &state);
    table.set_table_editable(true).on_cell_edited(on_cell_edited, &state);
    table.set_columns_reorderable(true)
        .on_columns_reordered(on_columns_reordered, &state)
        .on_row_drag(on_row_drag, &state);
    state.table = table.get();
    sort_rows(&state);
    table.set_sort(0, pt_sort_ascending);

    pt::widget status =
        pt::label(table_page.get(), "14 files · sorted by Name (ascending)");
    status.set_font_size(13.0f);
    state.status = status.get();
    table.set_table_selected(0);

    pt::widget tree = pt::tree(tree_page.get());
    tree.set_grow(1.0f);
    tree.set_tree_row_height(32.0f).set_tree_indent(18.0f);
    tree.on_tree_select(on_tree_select, &state);
    tree.set_tree_renamable(true).on_tree_rename(on_tree_rename, &state);
    pt_tree_node *workspace = tree.add_tree_node(nullptr, "workspace");
    pt_tree_node *src = tree.add_tree_node(workspace, "src");
    tree.add_tree_node(src, "main.c");
    pt_tree_node *table_node = tree.add_tree_node(src, "table.c");
    tree.add_tree_node(src, "tree.c");
    tree.add_tree_node(src, "navigation.c");
    pt_tree_node *docs = tree.add_tree_node(workspace, "docs");
    tree.add_tree_node(docs, "quickstart.md");
    tree.add_tree_node(docs, "bindings.md");
    pt_tree_node *assets = tree.add_tree_node(workspace, "assets");
    tree.add_tree_node(assets, "icons.svg");
    tree.add_tree_node(assets, "logo.svg");
    pt::set_tree_expanded(workspace, true);
    pt::set_tree_expanded(src, true);
    pt::set_tree_expanded(docs, true);
    tree.set_tree_selected(table_node);

    window.show();
    return app.run();
}
