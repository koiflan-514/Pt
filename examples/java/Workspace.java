/*
 * A tabbed file workspace with a sortable table and a tree view.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import pt.Pt;

public final class Workspace {
    private Workspace() {}

    private static final String[][] FILES = {
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

    private static final String[] COLUMN_LABELS = {"Name", "Kind", "Size", "Modified"};

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.examples.workspace.java");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);

            MemorySegment window = Pt.window_create(app, "Pt Workspace", 920, 640);
            // Window-scoped token override; the app-level theme stays untouched.
            Pt.window_set_theme_metric(window, Pt.theme_metric_radius_medium, 12.0f);
            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment title = Pt.label(root, "Workspace");
            Pt.widget_set_font_size(title, 26.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_semibold);
            MemorySegment subtitle =
                    Pt.label(root, "A table and a tree inside a tabbed shell.");
            Pt.widget_set_font_size(subtitle, 13.0f);

            MemorySegment tabs = Pt.tabs(root);
            Pt.widget_set_grow(tabs, 1.0f);
            Pt.tabs_set_strip_height(tabs, 46.0f);
            MemorySegment tablePage = Pt.tabs_add_page(tabs, "Files");
            Pt.widget_set_padding(tablePage, 12.0f, 0.0f, 0.0f, 0.0f);
            Pt.widget_set_gap(tablePage, 8.0f);
            MemorySegment treePage = Pt.tabs_add_page(tabs, "Tree");
            Pt.widget_set_padding(treePage, 12.0f, 0.0f, 0.0f, 0.0f);
            Pt.widget_set_gap(treePage, 8.0f);

            MemorySegment table = Pt.table(tablePage);
            Pt.widget_set_grow(table, 1.0f);
            Pt.table_set_column_count(table, 4);
            float[] widths = {240.0f, 140.0f, 100.0f, 160.0f};
            for (int column = 0; column < 4; column++) {
                Pt.table_set_column_title(table, column, COLUMN_LABELS[column]);
                Pt.table_set_column_width(table, column, widths[column]);
                Pt.table_set_column_resizable(table, column, true);
            }
            Pt.table_set_column_align(table, 2, Pt.text_align_end);
            Pt.table_set_row_height(table, 34.0f);
            Pt.table_set_row_count(table, FILES.length);

            MemorySegment chips = Pt.row(tablePage);
            Pt.widget_set_gap(chips, 8.0f);
            MemorySegment sourceChip = Pt.chip(chips, "Source");
            Pt.chip_set_icon(sourceChip, Pt.icon_document);
            Pt.chip_set_removable(sourceChip, true);
            MemorySegment activeChip = Pt.chip(chips, "Edited this week");
            Pt.chip_set_selected(activeChip, true);
            Pt.chip_set_on_click(activeChip, (chip, data) -> Pt.chip_set_selected(chip,
                    !Pt.chip_get_selected(chip)), arena);
            MemorySegment skeleton = Pt.skeleton(chips);
            Pt.widget_set_height(skeleton, 12.0f);

            MemorySegment status =
                    Pt.label(tablePage, FILES.length + " files · sorted by Name (ascending)");
            Pt.widget_set_font_size(status, 13.0f);

            int[] order = new int[FILES.length];
            for (int index = 0; index < order.length; index++) {
                order[index] = index;
            }
            Pt.table_set_on_bind_cell(table, (handle, cell, row, column, data) -> {
                String[] file = FILES[order[row]];
                Pt.widget_set_text(cell, file[column]);
                if (column == 0) {
                    Pt.widget_set_icon(cell, Pt.icon_document);
                }
            }, arena);

            Pt.table_set_on_select(table, (handle, row, data) -> {
                String[] file = FILES[order[row]];
                Pt.widget_set_text(status,
                        "Selected " + file[0] + " · " + file[1] + " · " + file[2]);
            }, arena);

            Pt.table_set_on_activate(table, (handle, row, data) -> {
                String name = FILES[order[row]][0];
                Pt.widget_set_text(status, "Opened " + name);
                MemorySegment toast =
                        Pt.window_show_toast(window, "Opened " + name, Pt.toast_info, 2000);
                if (toast.address() == 0) {
                    System.err.println(Pt.last_error());
                }
            }, arena);

            Pt.table_set_on_sort(table, (handle, column, direction, data) -> {
                List<Integer> sorted = new ArrayList<>();
                for (int index = 0; index < FILES.length; index++) {
                    sorted.add(index);
                }
                Comparator<Integer> byColumn =
                        Comparator.comparing(index -> FILES[index][column]);
                if (direction == Pt.sort_descending) {
                    byColumn = byColumn.reversed();
                }
                if (direction == Pt.sort_none) {
                    sorted.sort(Integer::compare);
                } else {
                    sorted.sort(byColumn);
                }
                for (int index = 0; index < order.length; index++) {
                    order[index] = sorted.get(index);
                }
                Pt.table_refresh(handle);
                String mode = direction == Pt.sort_ascending ? "ascending"
                        : direction == Pt.sort_descending ? "descending" : "unsorted";
                Pt.widget_set_text(status, FILES.length + " files · sorted by "
                        + COLUMN_LABELS[column] + " (" + mode + ")");
            }, arena);

            Pt.table_set_editable(table, true);
            Pt.table_set_on_cell_edited(table, (handle, row, column, text, data) -> {
                FILES[order[row]][column] = text;
                Pt.table_refresh(handle);
                Pt.widget_set_text(status,
                        "Edited " + FILES[order[row]][0] + " · column " + column);
            }, arena);
            Pt.table_set_columns_reorderable(table, true);
            Pt.table_set_on_columns_reordered(table, (handle, from, to, data) ->
                    Pt.widget_set_text(status, "Column moved from " + from + " to " + to),
                    arena);
            Pt.table_set_on_row_drag(table, (handle, row, data) ->
                    Pt.widget_set_text(status, "Dragging row " + row), arena);
            Pt.table_set_selected(table, 0);

            MemorySegment tree = Pt.tree(treePage);
            Pt.widget_set_grow(tree, 1.0f);
            Pt.tree_set_row_height(tree, 32.0f);
            Pt.tree_set_indent(tree, 18.0f);

            MemorySegment workspace = Pt.tree_add_node(tree, null, "workspace");
            MemorySegment src = Pt.tree_add_node(tree, workspace, "src");
            Pt.tree_add_node(tree, src, "main.c");
            MemorySegment tableNode = Pt.tree_add_node(tree, src, "table.c");
            Pt.tree_add_node(tree, src, "tree.c");
            Pt.tree_add_node(tree, src, "navigation.c");
            MemorySegment docs = Pt.tree_add_node(tree, workspace, "docs");
            Pt.tree_add_node(tree, docs, "quickstart.md");
            Pt.tree_add_node(tree, docs, "bindings.md");
            MemorySegment assets = Pt.tree_add_node(tree, workspace, "assets");
            Pt.tree_add_node(tree, assets, "icons.svg");
            Pt.tree_add_node(tree, assets, "logo.svg");
            Pt.tree_node_set_expanded(workspace, true);
            Pt.tree_node_set_expanded(src, true);
            Pt.tree_node_set_expanded(docs, true);

            Pt.tree_set_on_select(tree, (handle, node, data) -> Pt.widget_set_text(status,
                    "Tree: " + Pt.tree_node_get_text(node) + " (depth "
                            + Pt.tree_node_get_depth(node) + ")"), arena);
            Pt.tree_set_renamable(tree, true);
            Pt.tree_set_on_rename(tree, (handle, node, text, data) ->
                    Pt.widget_set_text(status, "Renamed node to " + text), arena);
            Pt.tree_set_selected(tree, tableNode);

            Pt.window_show(window);
            int result = Pt.app_run(app);
            Pt.app_destroy(app);
            if (result != 0) {
                System.exit(result);
            }
        }
    }
}
