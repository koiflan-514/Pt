"""Headless tests for the Pt Python binding.

Run with:
  PT_HEADLESS=1 PT_LIBRARY=build/libpt.so PYTHONPATH=bindings/python \
      python3 -m unittest discover -s bindings/python -p 'test_*.py'

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import ctypes
import time
import unittest

import pt


class BindingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.app = pt.App("pt.tests.python")
        self.window = pt.Window(self.app, "python tests", 400, 300)
        self.root = self.window.root
        self.column = pt.column(self.root).set_gap(6)

    def tearDown(self) -> None:
        self.app.destroy()

    def test_window_properties(self) -> None:
        self.assertEqual(self.window.width, 400)
        self.assertEqual(self.window.height, 300)
        self.assertTrue(self.window.is_open)
        self.assertGreater(self.window.scale, 0.0)
        self.window.set_title("renamed").set_min_size(240, 180).set_resizable(False)
        self.assertTrue(self.window.is_open)
        self.assertEqual(self.window.popup_policy, pt.popup_policy_auto)
        self.window.popup_policy = pt.popup_policy_window
        self.assertEqual(self.window.popup_policy, pt.popup_policy_window)
        self.window.popup_policy = pt.popup_policy_auto
        self.assertFalse(self.window.drag_active)
        self.assertEqual(self.window.drag_timeout, 0)
        self.window.drag_timeout = 1500
        self.assertEqual(self.window.drag_timeout, 1500)
        self.window.cancel_drag()
        self.assertFalse(self.window.drag_active)
        self.window.drag_timeout = 0

    def test_a11y_status(self) -> None:
        self.assertEqual(self.app.a11y_status, pt.a11y_status_disabled)
        enabled = self.app.enable_a11y()
        self.assertEqual(enabled, self.app.a11y_status == pt.a11y_status_active)
        self.assertNotEqual(self.app.a11y_status, pt.a11y_status_disabled)
        self.app.disable_a11y()
        self.assertEqual(self.app.a11y_status, pt.a11y_status_disabled)

    def test_widgets_and_values(self) -> None:
        label = pt.label(self.column, "hello")
        self.assertEqual(label.text, "hello")
        label.text = "changed"
        self.assertEqual(label.text, "changed")
        self.assertGreater(label.bounds[0], -1.0)

        label.text = "styled label"
        span = pt.TextSpan()
        span.start = 0
        span.length = 6
        span.weight = pt.font_weight_bold
        span.color = 0xFF2288FF
        span.family = b"DejaVu Serif"
        span.size = 24.0
        span.flags = (
            pt.text_span_underline
            | pt.text_span_has_weight
            | pt.text_span_has_color
            | pt.text_span_has_family
            | pt.text_span_has_size
        )
        label.set_text_spans([span])
        self.assertEqual(label.text_span_count(), 1)
        stored = label.text_span(0)
        self.assertIsNotNone(stored)
        if stored is not None:
            self.assertEqual(stored.start, 0)
            self.assertEqual(stored.length, 6)
            self.assertEqual(stored.weight, pt.font_weight_bold)
            self.assertEqual(stored.color, 0xFF2288FF)
            self.assertEqual(bytes(stored.family).split(b"\0")[0], b"DejaVu Serif")
            self.assertAlmostEqual(stored.size, 24.0, places=4)
            self.assertTrue(stored.flags & pt.text_span_underline)
            self.assertTrue(stored.flags & pt.text_span_has_family)
            self.assertTrue(stored.flags & pt.text_span_has_size)
        self.assertIsNone(label.text_span(1))
        label.clear_text_spans()
        self.assertEqual(label.text_span_count(), 0)

        button = pt.button(self.column, "Press")
        button.set_button_variant(pt.button_primary).on_click(lambda: None)

        entry = pt.entry(self.column, "placeholder").set_password(True)
        entry.on_change(lambda text: None)
        entry.on_submit(lambda: None)
        entry.set_input_purpose(pt.input_purpose_email)
        self.assertEqual(entry.input_purpose(), pt.input_purpose_email)

        toggle = pt.toggle(self.column).set_checked(True)
        self.assertTrue(toggle.checked)
        toggle.checked = False
        self.assertFalse(toggle.checked)

        slider = pt.slider(self.column).set_value(0.375)
        self.assertAlmostEqual(slider.value, 0.375, places=4)
        slider.on_value_change(lambda value: None)

        self.assertFalse(pt.label(self.column, "").focus())

    def test_modern_controls(self) -> None:
        checkbox = pt.checkbox(self.column, "Subscribe")
        self.assertFalse(checkbox.checked)
        checkbox.checked = True
        self.assertTrue(checkbox.checked)
        checkbox.on_checked(lambda value: None)

        radio = pt.radio(self.column, "Comfort")
        radio.checked = True
        self.assertTrue(radio.checked)

        other = pt.radio(self.column, "Compact")
        other.checked = True
        self.assertFalse(radio.checked)

        progress = pt.progress(self.column).set_progress(0.625)
        self.assertAlmostEqual(progress.progress, 0.625, places=4)
        progress.indeterminate = True
        self.assertTrue(progress.indeterminate)

        pt.spinner(self.column)

        selected: list[int] = []
        segmented = pt.segmented(self.column, ["Dense", "Cozy", "Compact"])
        segmented.select(1).on_select(selected.append)
        self.assertEqual(segmented.selected, 1)

        self.assertEqual(self.app.get_theme_color(pt.theme_accent),
                         self.app.accent_color)
        self.assertNotEqual(self.app.get_theme_color(pt.theme_control),
                            self.app.get_theme_color(pt.theme_track))

    def test_theme_and_modern_apis(self) -> None:
        self.assertAlmostEqual(
            self.app.get_theme_metric(pt.theme_metric_radius_small), 6.0, places=3
        )
        self.app.set_theme_metric(pt.theme_metric_chip_height, 30.0)
        self.assertAlmostEqual(
            self.app.get_theme_metric(pt.theme_metric_chip_height), 30.0, places=3
        )
        self.assertEqual(
            self.app.get_theme_color(pt.theme_skeleton),
            self.window.get_theme_color(pt.theme_skeleton),
        )
        self.window.set_theme_color(pt.theme_background, 0x102030FF)
        self.window.set_theme_metric(pt.theme_metric_focus_width, 3.0)
        self.assertEqual(self.window.get_theme_color(pt.theme_background), 0x102030FF)
        self.assertAlmostEqual(
            self.window.get_theme_metric(pt.theme_metric_focus_width), 3.0, places=3
        )
        self.assertNotEqual(
            self.app.get_theme_color(pt.theme_background), 0x102030FF
        )
        self.window.clear_theme_overrides()
        self.assertEqual(
            self.window.get_theme_color(pt.theme_background),
            self.app.get_theme_color(pt.theme_background),
        )

        chip = pt.chip(self.column, "filter")
        self.assertFalse(chip.chip_removable)
        self.assertFalse(chip.chip_selected)
        chip.set_chip_removable(True).set_chip_selected(True)
        self.assertTrue(chip.chip_removable)
        self.assertTrue(chip.chip_selected)
        chip.set_chip_icon(pt.icon_check)
        clicks: list[str] = []
        chip.on_chip_click(lambda: clicks.append("click"))
        chip.on_chip_remove(lambda: clicks.append("remove"))

        skeleton = pt.skeleton(self.column)
        self.assertGreaterEqual(skeleton.bounds[2], 0.0)

        busy = pt.button(self.column, "Save").set_busy(True)
        self.assertTrue(busy.busy)
        busy.busy = False
        self.assertFalse(busy.busy)
        field = pt.entry(self.column, "email").set_invalid(True)
        self.assertTrue(field.invalid)
        field.invalid = False
        self.assertFalse(field.invalid)

        table = pt.table(self.column).set_height(160)
        table.column_count = 3
        table.set_column_title(0, "Name").set_column_title(1, "Size")
        table.table_row_count = 5
        edits: list[tuple[int, int, str]] = []
        table.set_table_editable(True)
        self.assertTrue(table.table_editable)
        table.on_cell_edited(
            lambda _table, row, column, text: edits.append((row, column, text))
        )
        self.assertFalse(table.table_editing)
        self.assertTrue(table.begin_cell_edit(1, 0))
        self.assertTrue(table.table_editing)
        self.assertTrue(table.commit_cell_edit())
        self.assertFalse(table.table_editing)
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0][:2], (1, 0))
        table.select_range(1, 3)
        self.assertEqual(table.table_selection_count, 3)
        self.assertTrue(table.is_row_selected(2))
        self.assertEqual(table.table_selection(), [1, 2, 3])
        table.select_toggle(4)
        self.assertEqual(table.table_selection(), [1, 2, 3, 4])
        self.assertEqual(table.table_selected, 4)
        table.select_toggle(2)
        self.assertEqual(table.table_selection(), [1, 3, 4])
        table.clear_table_selection()
        self.assertEqual(table.table_selection_count, 0)
        self.assertEqual(table.table_selected, pt.list_index_none)
        reorders: list[tuple[int, int]] = []
        table.on_columns_reordered(
            lambda _table, source, target: reorders.append((source, target))
        )
        table.on_row_drag(lambda _table, _row: None)
        table.set_columns_reorderable(True)
        self.assertTrue(table.columns_reorderable)
        self.assertTrue(table.move_column(0, 2))
        self.assertEqual(reorders, [(0, 2)])

        tree = pt.tree(self.column)
        node = tree.add_tree_node(None, "before")
        self.assertFalse(tree.tree_renamable)
        self.assertFalse(tree.tree_renaming)
        tree.set_tree_renamable(True)
        renames: list[tuple[str, str]] = []
        tree.on_tree_rename(
            lambda _tree, target, text: renames.append((target.text, text))
        )
        self.assertTrue(tree.begin_tree_rename(node))
        self.assertTrue(tree.tree_renaming)
        self.assertTrue(tree.commit_tree_rename())
        self.assertFalse(tree.tree_renaming)
        self.assertEqual(renames, [("before", "before")])
        self.assertFalse(tree.commit_tree_rename())

    def test_menu_and_dialog(self) -> None:
        clicked: list[str] = []
        menu = pt.Menu(self.window)
        menu.add_item("Open", "Ctrl+O", lambda: clicked.append("open"))
        menu.add_separator()
        menu.add_item("Quit")
        menu.show_at(24, 32)
        self.assertTrue(menu.is_open)
        menu.dismiss()
        self.assertFalse(menu.is_open)
        menu.show_below(pt.button(self.column, "Menu"))
        self.assertTrue(menu.is_open)
        menu.destroy()
        self.assertFalse(menu.is_open)
        self.assertEqual(clicked, [])

        closed: list[bool] = []
        dialog = pt.Dialog(self.window, "Rename", "Choose a new name")
        dialog.add_action("Cancel")
        dialog.add_action("Save", primary=True)
        dialog.on_close(lambda _dialog: closed.append(True))
        dialog.show()
        self.assertTrue(dialog.is_open)
        dialog.set_title("Rename file").set_message("Pick a label")
        dialog.close()
        self.assertFalse(dialog.is_open)
        self.assertEqual(closed, [True])
        dialog.destroy()

    def test_toasts(self) -> None:
        toast = self.window.show_toast("Saved", pt.toast_success, 0)
        self.assertTrue(toast.visible)
        toast.dismiss()
        self.assertFalse(toast.visible)
        self.window.show_toast("Copied", pt.toast_info, 10)

    def test_tooltip_and_a11y(self) -> None:
        label = pt.label(self.column, "hello")
        label.set_tooltip("Greeting").set_tooltip_delay(0)
        self.assertEqual(label.a11y_role, pt.a11y_role_label)
        self.assertEqual(label.a11y_name, "hello")
        self.assertEqual(label.a11y_text, ("hello", 0, 0, 0))
        self.assertIsNone(label.a11y_value)
        self.assertTrue(label.a11y_state & pt.a11y_state_visible)
        self.assertTrue(label.a11y_state & pt.a11y_state_enabled)
        label.a11y_name = "Greeting"
        label.a11y_description = "Title text"
        self.assertEqual(label.a11y_name, "Greeting")
        self.assertEqual(label.a11y_description, "Title text")
        label.a11y_role = pt.a11y_role_generic
        self.assertEqual(label.a11y_role, pt.a11y_role_generic)

        placeholder = pt.entry(self.column, "Search")
        self.assertEqual(placeholder.a11y_name, "Search")
        placeholder.text = "secret"
        self.assertEqual(placeholder.a11y_text[0], "secret")
        placeholder.set_password(True)
        self.assertIsNone(placeholder.a11y_text)

        slider = pt.slider(self.column).set_value(0.25)
        self.assertAlmostEqual(slider.a11y_value, 0.25, places=4)
        self.assertTrue(slider.a11y_state & pt.a11y_state_focusable)

        events: list[tuple[object, object, int]] = []
        self.app.on_a11y_event(
            lambda window, widget, event: events.append((window, widget, event))
        )
        entry = pt.entry(self.column, "Log")
        entry.text = "hello"
        self.assertEqual(events[-1][2], pt.a11y_event_text_changed)
        self.assertIs(events[-1][0], self.window)
        self.assertIs(events[-1][1], entry)
        slider.value = 0.5
        self.assertEqual(events[-1][2], pt.a11y_event_value_changed)

    def test_text_editing_and_clipboard(self) -> None:
        entry = pt.entry(self.column, "editable")
        entry.text = "alpha beta gamma"
        self.assertEqual(entry.selection, None)
        entry.select_all()
        self.assertEqual(entry.selection, (0, 16))
        entry.set_selection(10, 6)
        self.assertEqual(entry.selection, (6, 10))
        self.assertFalse(entry.undo())
        self.assertFalse(entry.redo())

        self.window.clipboard = "copied text"
        self.assertEqual(self.window.clipboard, "copied text")
        self.assertTrue(self.window.set_clipboard("second").clipboard == "second")

    def test_scroll_offsets(self) -> None:
        scroll = pt.scroll(self.column).set_height(80)
        content = pt.column(scroll).set_gap(0)
        for index in range(12):
            pt.label(content, f"row {index}").set_height(24)
        self.assertEqual(scroll.scroll_offset, 0)
        scroll.scroll_offset = 50
        self.assertGreaterEqual(scroll.scroll_offset, 0)
        self.assertGreaterEqual(scroll.max_scroll_offset, 0)
        scroll.set_scroll_offset(0)
        self.assertEqual(scroll.scroll_offset, 0)

    def test_text_area(self) -> None:
        notes = pt.text_area(self.column, "Notes")
        self.assertTrue(notes.grow_to_content is False)
        notes.text = "alpha\nbeta"
        notes.on_change(lambda _text: None)
        notes.set_placeholder("Write")
        notes.select_all()
        self.assertEqual(notes.selection, (0, 10))
        notes.set_selection(6, 3)
        self.assertEqual(notes.selection, (3, 6))
        self.assertFalse(notes.undo())
        self.assertFalse(notes.redo())
        notes.scroll_to_caret()
        notes.grow_to_content = True
        self.assertTrue(notes.grow_to_content)
        notes.set_grow_to_content(False)
        self.assertFalse(notes.grow_to_content)
        self.assertEqual(notes.a11y_role, pt.a11y_role_text_frame)
        self.assertEqual(notes.a11y_text[0], "alpha\nbeta")
        self.assertEqual(notes.a11y_text[2:], (3, 6))

    def test_responsive_containers(self) -> None:
        wrapping = pt.wrap(self.column)
        wrapping.set_wrap_gap(8, 6)
        pt.card(wrapping).set_size(80, 40).set_align_self(pt.align_start)

        gridded = pt.grid(self.column, 3)
        gridded.set_grid_gap(10, 12)
        self.assertEqual(gridded.grid_columns, 3)
        gridded.grid_columns = 2
        self.assertEqual(gridded.grid_columns, 2)
        wide = pt.card(gridded)
        wide.set_grid_span(2)
        self.assertEqual(wide.grid_span, 2)
        wide.grid_span = 1
        self.assertEqual(wide.grid_span, 1)

    def test_list_view(self) -> None:
        rows = pt.list_view(self.column).set_height(160)
        rows.row_height = 36
        self.assertAlmostEqual(rows.row_height, 36, places=4)
        rows.row_count = 50
        self.assertEqual(rows.row_count, 50)
        self.assertEqual(rows.selected_row, pt.list_index_none)
        bindings: list[tuple[object, int]] = []
        rows.on_bind_row(lambda row, index: bindings.append((row, index)))
        rows.on_row_select(lambda _index: None)
        rows.on_row_activate(lambda _index: None)
        rows.selected_row = 7
        self.assertEqual(rows.selected_row, 7)
        rows.scroll_to_row(49)
        rows.refresh()
        rows.set_row_key_callback(lambda _view, row: 1000 + row * 10)
        self.assertEqual(rows.selected_key, 1070)
        rows.row_count = 50
        self.assertEqual(rows.selected_row, 7)
        rows.selected_row = 3
        self.assertEqual(rows.selected_key, 1030)
        self.assertFalse(rows.set_selected_key(999999))
        self.assertEqual(rows.selected_row, 3)
        self.assertTrue(rows.set_selected_key(1040))
        self.assertEqual(rows.selected_row, 4)
        self.assertEqual(rows.selected_key, 1040)
        rows.refresh_rows(0, 4)
        self.assertEqual(rows.row_count, 50)
        self.assertTrue(rows.set_selected_key(pt.row_key_none))
        self.assertEqual(rows.selected_row, pt.list_index_none)
        self.assertEqual(rows.selected_key, pt.row_key_none)
        self.assertTrue(rows.set_selected_key(1030))
        rows.row_count = 2
        self.assertEqual(rows.selected_row, pt.list_index_none)
        rows.selected_row = 1
        self.assertEqual(rows.selected_key, 1010)
        rows.row_count = 2
        self.assertEqual(rows.selected_row, 1)
        rows.set_row_text_callback(lambda _view, row: f"item {row}")
        rows.selected_row = pt.list_index_none
        rows.row_count = 2
        self.assertEqual(rows.selected_row, pt.list_index_none)
        self.assertEqual(rows.a11y_role, pt.a11y_role_list)
        rows.row_count = 8
        self.assertEqual(rows.list_selection_count, 0)
        self.assertEqual(rows.list_selection(), [])
        rows.list_select_range(2, 4)
        self.assertEqual(rows.list_selection_count, 3)
        self.assertEqual(rows.list_selection(), [2, 3, 4])
        self.assertTrue(rows.is_list_row_selected(3))
        self.assertFalse(rows.is_list_row_selected(5))
        self.assertEqual(rows.selected_row, 2)
        rows.list_select_toggle(6)
        self.assertEqual(rows.list_selection(), [2, 3, 4, 6])
        rows.list_select_toggle(3)
        self.assertEqual(rows.list_selection(), [2, 4, 6])
        rows.clear_list_selection()
        self.assertEqual(rows.list_selection_count, 0)
        self.assertEqual(rows.selected_row, pt.list_index_none)

    def test_icons_and_icon_button(self) -> None:
        icon = pt.icon(self.column, pt.icon_search)
        self.assertEqual(icon.icon, pt.icon_search)
        self.assertAlmostEqual(icon.icon_size, 20.0, places=4)
        icon.set_icon_size(28).set_icon(pt.icon_bell)
        self.assertEqual(icon.icon, pt.icon_bell)
        self.assertAlmostEqual(icon.icon_size, 28.0, places=4)
        self.assertEqual(icon.a11y_role, pt.a11y_role_icon)
        icon.icon = pt.icon_home
        self.assertEqual(icon.icon, pt.icon_home)

        action = pt.icon_button(self.column, pt.icon_plus, "Add item")
        self.assertEqual(action.a11y_name, "Add item")
        self.assertEqual(action.a11y_role, pt.a11y_role_push_button)
        self.assertGreater(pt.icon_warning, pt.icon_none)
        self.assertEqual(pt.icon_warning, 21)

    def test_table(self) -> None:
        table = pt.table(self.column).set_height(180)
        self.assertEqual(table.a11y_role, pt.a11y_role_table)
        table.column_count = 3
        self.assertEqual(table.column_count, 3)
        table.set_column_title(0, "Name").set_column_title(1, "Kind")
        table.set_column_width(0, 140).set_column_align(1, pt.text_align_center)
        table.set_column_resizable(2, False)
        self.assertAlmostEqual(table.column_width(0), 140, places=4)
        table.table_row_count = 40
        table.table_row_height = 32
        self.assertEqual(table.table_row_count, 40)
        self.assertAlmostEqual(table.table_row_height, 32, places=4)
        table.set_header_height(34)
        table.on_bind_cell(lambda cell, row, column: None)
        table.on_table_select(lambda _index: None)
        table.on_table_activate(lambda _index: None)
        table.on_table_sort(lambda _column, _direction: None)
        self.assertEqual(table.table_selected, pt.list_index_none)
        table.table_selected = 3
        self.assertEqual(table.table_selected, 3)
        table.set_sort(1, pt.sort_ascending)
        self.assertEqual(table.sort, (pt.sort_ascending, 1))
        self.assertFalse(table.column_hidden(1))
        table.set_column_hidden(1, True)
        self.assertTrue(table.column_hidden(1))
        table.set_column_hidden(1, False)
        self.assertFalse(table.column_hidden(1))
        self.assertFalse(table.column_hidden(7))
        reported: list[tuple[int, list[int], list[int]]] = []
        table.on_sort_columns(
            lambda count, columns, directions: reported.append(
                (count, columns, directions)
            )
        )
        table.set_sort_columns([(2, pt.sort_descending), (0, pt.sort_ascending)])
        self.assertEqual(
            table.sort_columns, [(2, pt.sort_descending), (0, pt.sort_ascending)]
        )
        self.assertEqual(table.sort, (pt.sort_descending, 2))
        table.set_sort_columns(
            [(1, pt.sort_ascending), (1, pt.sort_descending), (7, pt.sort_ascending)]
        )
        self.assertEqual(table.sort_columns, [(1, pt.sort_ascending)])
        table.set_table_row_key_callback(lambda _table, row: 1000 + row * 10)
        self.assertEqual(table.table_selected, 3)
        self.assertEqual(table.table_selected_key, 1030)
        table.table_row_count = 40
        self.assertEqual(table.table_selected, 3)
        table.table_selected = 0
        self.assertEqual(table.table_selected_key, 1000)
        self.assertFalse(table.set_table_selected_key(999999))
        self.assertEqual(table.table_selected, 0)
        self.assertTrue(table.set_table_selected_key(1050))
        self.assertEqual(table.table_selected, 5)
        self.assertEqual(table.table_selected_key, 1050)
        self.assertTrue(table.set_table_selected_key(pt.row_key_none))
        self.assertEqual(table.table_selected, pt.list_index_none)
        self.assertTrue(table.set_table_selected_key(1010))
        self.assertEqual(table.table_selected, 1)
        table.table_refresh_rows(0, 3)
        self.assertEqual(table.table_row_count, 40)
        table.set_table_row_text_callback(lambda _table, row: f"row {row}")
        table.table_selected = 3
        table.table_scroll_to_row(39).scroll_to_column(2).table_refresh()
        table.table_row_count = 2
        self.assertEqual(table.table_selected, pt.list_index_none)
        self.assertEqual(pt.table_column_none, pt.list_index_none)
        self.assertEqual(pt.sort_descending, 2)

    def test_tree(self) -> None:
        tree = pt.tree(self.column)
        self.assertEqual(tree.a11y_role, pt.a11y_role_tree)
        alpha = tree.add_tree_node(None, "alpha")
        beta = tree.add_tree_node(None, "beta")
        leaf = tree.add_tree_node(alpha, "leaf")
        self.assertEqual(tree.tree_root_count, 2)
        self.assertEqual(alpha.text, "alpha")
        self.assertTrue(alpha.has_children)
        self.assertEqual(alpha.child_count, 1)
        self.assertEqual(alpha.child(0), leaf)
        self.assertEqual(alpha.children, [leaf])
        self.assertEqual(leaf.parent, alpha)
        self.assertEqual(leaf.depth, 1)
        self.assertEqual(beta.parent, None)
        leaf.data = 7
        self.assertEqual(leaf.data, 7)
        self.assertEqual(tree.tree_visible_count, 2)
        alpha.expanded = True
        self.assertTrue(alpha.expanded)
        self.assertEqual(tree.tree_visible_count, 3)
        self.assertEqual(tree.tree_visible_node(1), leaf)
        self.assertEqual(tree.tree_selected, None)
        tree.on_tree_select(lambda _node: None)
        tree.on_tree_activate(lambda _node: None)
        tree.tree_selected = leaf
        self.assertEqual(tree.tree_selected, leaf)
        tree.scroll_to_tree_node(beta).tree_refresh()
        beta.text = "beta renamed"
        self.assertEqual(beta.text, "beta renamed")
        self.assertEqual(str(leaf).startswith("<pt.TreeNode 'leaf'"), True)
        self.assertEqual(tree.tree_selection_count, 1)
        self.assertEqual(tree.tree_selection(), [leaf])
        tree.tree_select_range(0, 1)
        self.assertEqual(tree.tree_selection_count, 2)
        self.assertEqual(tree.tree_selection(), [alpha, leaf])
        self.assertTrue(tree.is_tree_node_selected(leaf))
        self.assertFalse(tree.is_tree_node_selected(beta))
        self.assertEqual(tree.tree_selected, alpha)
        tree.tree_select_toggle(beta)
        self.assertEqual(tree.tree_selection_count, 3)
        self.assertEqual(tree.tree_selected, beta)
        tree.tree_select_toggle(leaf)
        self.assertEqual(tree.tree_selection(), [alpha, beta])
        tree.clear_tree_selection()
        self.assertEqual(tree.tree_selection_count, 0)
        self.assertEqual(tree.tree_selected, None)
        tree.clear_tree()
        self.assertEqual(tree.tree_root_count, 0)
        self.assertEqual(tree.tree_visible_count, 0)

    def test_tabs(self) -> None:
        tabs = pt.tabs(self.column)
        first = tabs.add_tab("General")
        second = tabs.add_tab("Advanced")
        self.assertEqual(tabs.tab_count, 2)
        self.assertEqual(tabs.tab_title(0), "General")
        tabs.set_tab_title(1, "Options")
        self.assertEqual(tabs.tab_title(1), "Options")
        self.assertEqual(tabs.active_tab, 0)
        tabs.active_tab = 1
        self.assertEqual(tabs.active_tab, 1)
        tabs.set_active_tab(0)
        self.assertEqual(tabs.active_tab, 0)
        changed: list[int] = []
        tabs.on_tab_change(changed.append)
        tabs.set_tab_strip_height(40)
        self.assertIsNotNone(tabs.tab_page(0))
        self.assertIsNotNone(tabs.tab_page(1))
        self.assertIsNone(tabs.tab_page(2))
        tabs.remove_tab(0)
        self.assertEqual(tabs.tab_count, 1)
        self.assertEqual(tabs.tab_title(0), "Options")
        self.assertEqual(tabs.active_tab, 0)
        self.assertIsNone(tabs.tab_page(1))
        self.assertNotEqual(first.raw, second.raw)

    def test_sidebar(self) -> None:
        sidebar = pt.sidebar(self.column)
        home = sidebar.add_sidebar_item(pt.icon_home, "Home")
        sidebar.add_sidebar_item(pt.icon_folder, "Files")
        self.assertEqual(sidebar.sidebar_item_count, 2)
        self.assertEqual(sidebar.active_sidebar_item, pt.list_index_none)
        sidebar.active_sidebar_item = 1
        self.assertEqual(sidebar.active_sidebar_item, 1)
        sidebar.set_active_sidebar_item(0)
        self.assertEqual(sidebar.active_sidebar_item, 0)
        sidebar.sidebar_header = "Workspace"
        self.assertEqual(sidebar.sidebar_header, "Workspace")
        self.assertFalse(sidebar.sidebar_compact)
        sidebar.sidebar_compact = True
        self.assertTrue(sidebar.sidebar_compact)
        sidebar.set_sidebar_compact(False)
        self.assertFalse(sidebar.sidebar_compact)
        sidebar.on_sidebar_select(lambda _index: None)
        self.assertEqual(home.a11y_name, "Home")
        self.assertIsNotNone(sidebar.sidebar_item(1))
        self.assertIsNone(sidebar.sidebar_item(2))

    def test_combobox(self) -> None:
        combo = pt.combobox(self.column).set_width(200)
        self.assertEqual(combo.a11y_role, pt.a11y_role_combo_box)
        self.assertEqual(combo.item_count, 0)
        self.assertEqual(combo.combo_selected, pt.list_index_none)
        self.assertEqual(combo.combo_text, "")
        self.assertEqual(combo.combo_mode, pt.combobox_select_only)
        self.assertFalse(combo.popup_open)
        self.assertAlmostEqual(combo.popup_max_height, 260.0, places=4)
        combo.set_combo_placeholder("Colour")
        combo.add_item("Red").add_item("Green").insert_item(1, "Yellow")
        self.assertEqual(combo.item_count, 3)
        self.assertEqual(combo.item_text(1), "Yellow")
        self.assertEqual(combo.combo_placeholder, "Colour")
        combo.set_item_text(2, "Blue")
        self.assertEqual(combo.item_text(2), "Blue")
        payload = {"rgb": 0x1234}
        combo.set_item_data(2, payload)
        self.assertIs(combo.item_data(2), payload)
        self.assertIsNone(combo.item_data(0))
        self.assertFalse(combo.is_item_disabled(1))
        combo.set_item_disabled(1, True)
        self.assertTrue(combo.is_item_disabled(1))
        combo.set_item_disabled(1, False)
        chosen: list[int] = []
        typed: list[str] = []
        combo.on_combo_change(lambda _combo, index: chosen.append(index))
        combo.on_combo_text_change(lambda _combo, text: typed.append(text))
        combo.combo_selected = 2
        self.assertEqual(combo.combo_selected, 2)
        self.assertEqual(combo.combo_text, "Blue")
        self.assertEqual(chosen, [])
        combo.set_combo_mode(pt.combobox_filterable)
        self.assertEqual(combo.combo_mode, pt.combobox_filterable)
        combo.combo_text = "gr"
        self.assertEqual(combo.combo_text, "gr")
        self.assertEqual(typed, [])
        combo.open_popup()
        self.assertTrue(combo.popup_open)
        combo.popup_max_height = 120
        self.assertAlmostEqual(combo.popup_max_height, 120.0, places=4)
        combo.close_popup()
        self.assertFalse(combo.popup_open)
        combo.remove_item(0)
        self.assertEqual(combo.item_count, 2)
        self.assertEqual(combo.item_text(0), "Yellow")
        combo.clear_items()
        self.assertEqual(combo.item_count, 0)
        self.assertEqual(combo.combo_selected, pt.list_index_none)

    def test_uri_list_stylus_and_drag_image(self) -> None:
        payload = "file:///tmp/a.txt\r\n# comment\r\nfile:///tmp/b%20c.txt\r\n"
        self.assertEqual(pt.uri_list_count(payload), 2)
        self.assertEqual(pt.uri_list_get(payload, 0), "file:///tmp/a.txt")
        self.assertEqual(pt.uri_list_get(payload, 1), "file:///tmp/b%20c.txt")
        self.assertIsNone(pt.uri_list_get(payload, 2))
        self.assertEqual(pt.uri_list(payload),
                         ["file:///tmp/a.txt", "file:///tmp/b%20c.txt"])
        self.assertEqual(pt.uri_list_count(""), 0)

        self.assertFalse(
            self.window.begin_drag([("text/uri-list", "file:///tmp/a.txt\r\n")])
        )
        self.window.set_drag_image([0xFF0000FF, 0x00FF00FF], 1, 2)
        self.window.clear_drag_image()
        with self.assertRaises(ValueError):
            self.window.set_drag_image([0xFF0000FF], 2, 2)

        stylus: list[pt.StylusEvent] = []
        self.window.on_stylus(lambda _window, event: stylus.append(event))
        self.window.show()
        self.assertEqual(self.app.run_for(20), 0)
        self.assertEqual(stylus, [])
        self.assertEqual(pt.stylus_phase_down, 2)
        self.assertEqual(pt.stylus_tool_eraser, 1)
        self.assertEqual(pt.a11y_state_expandable, 1 << 10)
        self.assertGreater(pt.a11y_role_icon, pt.a11y_role_tab_panel)

    def test_motion(self) -> None:
        self.assertAlmostEqual(pt.ease(pt.easing_linear, 0.5), 0.5, places=5)
        self.assertAlmostEqual(
            pt.ease_value(pt.easing_linear, 10, 30, 0.25), 15, places=5
        )
        transition = pt.Transition(0).to(10, 100, pt.easing_linear)
        self.assertTrue(transition.active)
        transition.advance(0.05)
        self.assertAlmostEqual(transition.value, 5, delta=0.01)
        transition.to(20, 100, pt.easing_linear).advance(0.05)
        self.assertGreater(transition.value, 5)
        transition.set(30)
        self.assertFalse(transition.active)
        self.assertAlmostEqual(transition.value, 30, places=5)
        self.app.reduced_motion = False
        self.assertFalse(self.app.reduced_motion)
        self.app.reduced_motion = True
        self.assertTrue(self.app.reduced_motion)
        self.app.reduced_motion = False

    def test_interaction_bindings(self) -> None:
        entry = pt.entry(self.column, "draggable")
        self.assertTrue(entry.drag_enabled)
        entry.set_drag_enabled(False)
        self.assertFalse(entry.drag_enabled)
        entry.drag_enabled = True
        self.assertTrue(entry.drag_enabled)

        received: list[str] = []
        entry.set_drop_target(
            enter=lambda _widget, mime: received.append(f"enter:{mime}") or True,
            motion=lambda _widget, _x, _y: received.append("motion"),
            leave=lambda _widget: received.append("leave"),
            drop=lambda _widget, _mime, _data, _x, _y: received.append("drop"),
        )
        self.assertIsNotNone(entry._drop_target)
        entry.set_drop_target()
        self.assertIsNone(entry._drop_target)

        gestures: list[pt.GestureEvent] = []
        self.window.on_gesture(lambda _window, event: gestures.append(event))
        self.window.set_long_press_delay(320)
        self.assertTrue(self.window.momentum_scrolling)
        self.window.set_momentum_scrolling(False)
        self.assertFalse(self.window.momentum_scrolling)
        self.window.momentum_scrolling = True
        self.assertTrue(self.window.momentum_scrolling)
        self.assertFalse(
            self.window.begin_drag([("text/plain;charset=utf-8", "dragged")])
        )

        self.window.show()
        self.assertEqual(self.app.run_for(20), 0)
        self.assertEqual(received, [])
        self.assertEqual(gestures, [])

    def test_timer_and_quit(self) -> None:
        fired: list[bool] = []
        timeout = self.app.add_timeout(5, lambda: fired.append(True))
        cancelled: list[bool] = []
        cancel_id = self.app.add_timeout(30, lambda: cancelled.append(True))
        self.app.cancel_timeout(cancel_id)
        self.window.show()
        self.assertEqual(self.app.run_for(80), 0)
        self.assertEqual(fired, [True])
        self.assertEqual(cancelled, [])
        self.assertNotIn(timeout, self.app._timers)
        self.assertTrue(self.app.running is False)

    def test_close_veto(self) -> None:
        allowed = [False]
        self.window.on_close(lambda _window: allowed[0])
        self.window.close()
        self.assertTrue(self.window.is_open)
        allowed[0] = True
        self.window.close()
        self.assertFalse(self.window.is_open)
        with self.assertRaises(RuntimeError):
            _ = self.window.root

    def test_close_quits_loop(self) -> None:
        def close_later() -> None:
            self.window.close()

        self.app.add_timeout(5, close_later)
        self.window.show()
        started = time.monotonic()
        self.assertEqual(self.app.run_for(2000), 0)
        self.assertLess(time.monotonic() - started, 1.0)

    def test_key_and_resize_handlers(self) -> None:
        self.window.on_key(lambda _window, _key, _modifiers: False)
        self.window.on_resize(lambda _window, _width, _height: None)
        self.window.show()
        self.assertEqual(self.app.run_for(20), 0)

    def test_theme_helpers(self) -> None:
        self.app.color_scheme = pt.color_scheme_light
        self.assertEqual(self.app.color_scheme, pt.color_scheme_light)
        self.app.accent_color = pt.rgb(0x9ECE6A)
        self.assertEqual(self.app.accent_color, pt.rgb(0x9ECE6A))
        self.app.set_font_family("monospace")
        self.assertEqual(pt.rgba(10, 20, 30), 0x0A141EFF)
        self.assertEqual(pt.color_alpha(pt.rgb(0x112233), 128), 0x11223380)
        self.assertEqual(pt.auto, -1.0)


class StructuredDragDropTests(unittest.TestCase):
    """The 0.8 asynchronous drag and drop surface, without a compositor.

    No offer can reach a headless window, so these tests cover the source side
    returning False, the structured callbacks wiring and the NULL-safe handle
    accessors. Callback trampolines are invoked directly to check how the
    binding marshals MIME types and raw chunks.
    """

    def setUp(self) -> None:
        self.app = pt.App("pt.tests.python.dnd")
        self.window = pt.Window(self.app, "python dnd", 400, 300)
        self.root = self.window.root

    def tearDown(self) -> None:
        self.window.set_drag_finished_callback(None)
        self.root.set_drop_target()
        self.app.destroy()

    def test_begin_drag_and_result(self) -> None:
        self.assertEqual(self.window.drag_result, pt.drag_action_none)
        self.assertFalse(self.window.drag_active)
        self.assertFalse(self.window.begin_drag([("text/plain", "drag me")]))
        self.assertFalse(
            self.window.begin_drag(
                [
                    ("text/plain", "drag me"),
                    ("application/json", b'{"a":1}', 7),
                    {"mime_type": "text/uri-list", "data": "file:///tmp/a.txt"},
                    {
                        "mime_type": "application/octet-stream",
                        "data": b"\x00\x01",
                        "size": 2,
                    },
                    pt.DragData(b"text/plain", b"raw payload", 0),
                ]
            )
        )
        self.assertFalse(self.window.begin_drag([]))
        self.assertEqual(self.window.drag_result, pt.drag_action_none)
        with self.assertRaises(TypeError):
            self.window.begin_drag([("text/plain", "text", 1, 2)])
        with self.assertRaises(TypeError):
            self.window.begin_drag([(7, "text/plain")])

    def test_drag_finished_callback_round_trip(self) -> None:
        finished: list[tuple[bool, int]] = []
        registered = self.window.set_drag_finished_callback(
            lambda _window, dropped, action: finished.append((dropped, action))
        )
        self.assertIs(registered, self.window)
        self.window.show()
        self.assertEqual(self.app.run_for(20), 0)
        self.assertEqual(finished, [])
        self.window.set_drag_finished_callback(None)
        self.assertFalse(
            self.window.begin_drag([("text/plain;charset=utf-8", "dragged")])
        )
        self.assertEqual(self.app.run_for(20), 0)
        self.assertEqual(finished, [])
        self.assertFalse(self.window.drag_active)

    def test_null_handles_are_safe(self) -> None:
        offer = pt.DropOffer(None)
        self.assertEqual(offer.raw, 0)
        self.assertEqual(offer.mime_count, 0)
        self.assertIsNone(offer.mime(0))
        self.assertIsNone(offer.mime(9))
        self.assertFalse(offer.has_mime("text/plain"))
        self.assertIsNone(offer.selected_mime)
        self.assertEqual(offer.actions, 0)
        self.assertIsNone(offer.read("text/plain"))
        self.assertIsNone(offer.read("text/plain", 32, lambda *_args: None))
        self.assertEqual(pt._drop_streams, {})
        self.assertIn("DropOffer", repr(offer))

        stream = pt.DropStream(None)
        self.assertEqual(stream.raw, 0)
        self.assertIsNone(stream.mime)
        self.assertEqual(stream.size, 0)
        self.assertFalse(stream.active)
        self.assertIs(stream.cancel(), stream)
        self.assertEqual(pt._drop_streams, {})

        self.assertEqual(pt.drag_action_none, 0)
        self.assertEqual(pt.drag_action_ask, 4)
        self.assertEqual(pt.drop_stream_data, 0)
        self.assertEqual(pt.drop_stream_error, 3)

    def test_drop_target_offer_callbacks(self) -> None:
        entered: list[tuple[int, bool, object, int]] = []
        dropped: list[tuple[str, float, float]] = []
        target = pt.label(self.root, "drop target")
        target.set_drop_target(
            enter_offer=lambda _widget, offer: entered.append(
                (
                    offer.mime_count,
                    offer.has_mime("text/plain"),
                    offer.selected_mime,
                    offer.actions,
                )
            )
            or "text/plain",
            drop_offer=lambda _widget, offer, mime, x, y: dropped.append((mime, x, y))
            or offer.read("text/plain", 32, lambda *_args: None),
        )
        self.assertIsNotNone(target._drop_target)
        assert target._drop_target is not None
        self.assertTrue(bool(target._drop_target.enter))
        self.assertTrue(bool(target._drop_target.enter_offer))
        self.assertTrue(bool(target._drop_target.drop_offer))
        self.assertFalse(bool(target._drop_target.motion))
        self.assertFalse(bool(target._drop_target.leave))
        self.assertFalse(bool(target._drop_target.drop))

        self.assertEqual(target._drop_target.enter_offer(0, 0, 0), b"text/plain")
        self.assertEqual(entered, [(0, False, None, 0)])
        self.assertEqual(bytes(target._drop_mime.value), b"text/plain")

        self.assertIsNone(
            target._drop_target.drop_offer(0, 0, b"text/plain", 4.0, 6.0, 0)
        )
        self.assertEqual(dropped, [("text/plain", 4.0, 6.0)])
        self.assertEqual(pt._drop_streams, {})

        declined: list[str] = []
        target.set_drop_target(
            enter_offer=lambda _widget, _offer: declined.append("offer") or None
        )
        assert target._drop_target is not None
        self.assertIsNone(target._drop_target.enter_offer(0, 0, 0))
        self.assertEqual(declined, ["offer"])
        self.assertIsNone(target._drop_mime)
        self.assertFalse(bool(target._drop_target.drop_offer))

        target.set_drop_target()
        self.assertIsNone(target._drop_target)

    def test_legacy_drop_target_still_registers(self) -> None:
        received: list[str] = []
        target = pt.entry(self.root, "legacy")
        target.set_drop_target(
            enter=lambda _widget, mime: received.append(f"enter:{mime}") or True,
            leave=lambda _widget: received.append("leave"),
        )
        assert target._drop_target is not None
        self.assertTrue(bool(target._drop_target.enter))
        self.assertFalse(bool(target._drop_target.enter_offer))
        self.assertTrue(target._drop_target.enter(0, b"text/plain", 0))
        target._drop_target.leave(0, 0)
        self.assertEqual(received, ["enter:text/plain", "leave"])
        target.set_drop_target()
        self.assertIsNone(target._drop_target)

    def test_stream_chunks_keep_exact_length(self) -> None:
        payload = ctypes.create_string_buffer(b"a\x00b", 3)
        address = ctypes.addressof(payload)
        self.assertEqual(pt._bytes_at(address, 3), b"a\x00b")
        self.assertEqual(pt._bytes_at(address, 2), b"a\x00")
        self.assertEqual(pt._bytes_at(address, 1), b"a")
        self.assertEqual(pt._bytes_at(None, 3), b"")
        self.assertEqual(pt._bytes_at(address, 0), b"")


if __name__ == "__main__":
    unittest.main()
