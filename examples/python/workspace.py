"""A tabbed file workspace with a sortable table and a tree view.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import pt

FILES = [
    ["main.c", "C source", "18 KB", "Today 09:42"],
    ["table.c", "C source", "42 KB", "Today 09:12"],
    ["tree.c", "C source", "31 KB", "Yesterday"],
    ["navigation.c", "C source", "27 KB", "Yesterday"],
    ["README.md", "Markdown", "11 KB", "Today 08:30"],
    ["bindings.md", "Markdown", "32 KB", "Oct 13"],
    ["icons.svg", "Vector", "9 KB", "Oct 11"],
    ["logo.svg", "Vector", "6 KB", "Oct 10"],
    ["pt.h", "Header", "8 KB", "Today 09:40"],
    ["interaction.h", "Header", "12 KB", "Oct 14"],
    ["theme.json", "Data", "3 KB", "Oct 09"],
    ["release.tar.gz", "Archive", "1.0 MB", "Oct 08"],
    ["session.log", "Log", "128 KB", "Today 10:02"],
    ["build.log", "Log", "96 KB", "Yesterday"],
]


def main() -> int:
    app = pt.App("dev.pt.examples.workspace.python")
    app.color_scheme = pt.color_scheme_dark
    window = pt.Window(app, "Pt Workspace", 920, 640)
    # Window-scoped token override; the app-level theme stays untouched.
    window.set_theme_metric(pt.theme_metric_radius_medium, 12)
    root = window.root.set_padding(24, 24, 24, 24).set_gap(14)

    pt.label(root, "Workspace").set_font_size(26).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(root, "A table and a tree inside a tabbed shell.").set_font_size(13)

    tabs = pt.tabs(root).set_grow(1.0)
    table_page = tabs.add_tab("Files").set_padding(12, 0, 0, 0).set_gap(8)
    tree_page = tabs.add_tab("Tree").set_padding(12, 0, 0, 0).set_gap(8)

    chips = pt.row(table_page).set_gap(8)
    pt.chip(chips, "Source").set_chip_icon(pt.icon_document).set_chip_removable(True)
    active_chip = pt.chip(chips, "Edited this week").set_chip_selected(True)
    active_chip.on_chip_click(
        lambda: active_chip.set_chip_selected(not active_chip.chip_selected)
    )
    pt.skeleton(chips).set_height(12)

    status = pt.label(table_page, "14 files · sorted by Name (ascending)")
    status.set_font_size(13)

    order = list(range(len(FILES)))
    sort_column = 0
    sort_direction = pt.sort_ascending

    def update_status(text: str) -> None:
        status.text = text

    table = pt.table(table_page).set_grow(1.0)
    table.set_column_count(4)
    for column, title, width in (
        (0, "Name", 240),
        (1, "Kind", 140),
        (2, "Size", 100),
        (3, "Modified", 160),
    ):
        table.set_column_title(column, title).set_column_width(column, width)
        table.set_column_resizable(column, True)
    table.set_column_align(2, pt.text_align_end)
    table.set_table_row_height(34)
    table.set_table_row_count(len(FILES))

    def bind_cell(cell: pt.Widget, row: int, column: int) -> None:
        file = FILES[order[row]]
        cell.text = file[column]
        if column == 0:
            cell.icon = pt.icon_document

    def on_select(row: int) -> None:
        name, kind, size, _modified = FILES[order[row]]
        update_status(f"Selected {name} · {kind} · {size}")

    def on_activate(row: int) -> None:
        name = FILES[order[row]][0]
        update_status(f"Opened {name}")
        window.show_toast(f"Opened {name}", pt.toast_info, 2000)

    def on_sort(column: int, direction: int) -> None:
        nonlocal sort_column, sort_direction, order
        sort_column = column
        sort_direction = direction
        order = list(range(len(FILES)))
        if direction != pt.sort_none:
            order.sort(
                key=lambda index: FILES[index][column],
                reverse=direction == pt.sort_descending,
            )
        table.table_refresh()
        labels = ("Name", "Kind", "Size", "Modified")
        mode = {
            pt.sort_ascending: "ascending",
            pt.sort_descending: "descending",
            pt.sort_none: "unsorted",
        }[direction]
        update_status(f"{len(FILES)} files · sorted by {labels[column]} ({mode})")

    def on_cell_edited(_table: pt.Widget, row: int, column: int, text: str) -> None:
        FILES[order[row]][column] = text
        _table.table_refresh()
        update_status(f"Edited {FILES[order[row]][0]} · column {column}")

    def on_columns_reordered(_table: pt.Widget, source: int, target: int) -> None:
        update_status(f"Column moved from {source} to {target}")

    def on_row_drag(_table: pt.Widget, row: int) -> None:
        update_status(f"Dragging row {row}")

    table.on_bind_cell(bind_cell)
    table.on_table_select(on_select)
    table.on_table_activate(on_activate)
    table.on_table_sort(on_sort)
    table.set_table_editable(True)
    table.on_cell_edited(on_cell_edited)
    table.set_columns_reorderable(True)
    table.on_columns_reordered(on_columns_reordered)
    table.on_row_drag(on_row_drag)
    table.table_selected = 0

    tree = pt.tree(tree_page).set_grow(1.0)
    tree.set_tree_row_height(32).set_tree_indent(18)
    workspace = tree.add_tree_node(None, "workspace")
    src = tree.add_tree_node(workspace, "src")
    tree.add_tree_node(src, "main.c")
    table_node = tree.add_tree_node(src, "table.c")
    tree.add_tree_node(src, "tree.c")
    tree.add_tree_node(src, "navigation.c")
    docs = tree.add_tree_node(workspace, "docs")
    tree.add_tree_node(docs, "quickstart.md")
    tree.add_tree_node(docs, "bindings.md")
    assets = tree.add_tree_node(workspace, "assets")
    tree.add_tree_node(assets, "icons.svg")
    tree.add_tree_node(assets, "logo.svg")
    workspace.expanded = True
    src.expanded = True
    docs.expanded = True

    def on_tree_select(node: pt.TreeNode) -> None:
        update_status(f"Tree: {node.text} (depth {node.depth})")

    tree.on_tree_select(on_tree_select)
    tree.set_tree_renamable(True)
    tree.on_tree_rename(
        lambda _tree, _node, text: update_status(f"Renamed node to {text}")
    )
    tree.set_tree_selected(table_node)

    window.show()
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main())
