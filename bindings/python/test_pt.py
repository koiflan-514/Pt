"""Headless tests for the Pt Python binding.

Run with:
  PT_HEADLESS=1 PT_LIBRARY=build/libpt.so PYTHONPATH=bindings/python \
      python3 -m unittest discover -s bindings/python -p 'test_*.py'

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import ctypes
import os
import time
import unittest
from typing import Any

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


class ExtensionApiTests(unittest.TestCase):
    """Headless coverage for the custom paint, IME, fd-watch and
    primary-selection extension APIs (pt/custom.h and friends)."""

    def setUp(self) -> None:
        self.app = pt.App("pt.tests.python.extension")
        self.window = pt.Window(self.app, "extension tests", 320, 240)
        self.root = self.window.root
        self.column = pt.column(self.root).set_gap(6)

    def tearDown(self) -> None:
        self.app.destroy()

    def test_paint_callback_and_context(self) -> None:
        fill_color = pt.rgb(0xAA3300)
        checked: list[bool] = []

        def paint(widget: pt.Widget, context: pt.PaintContext) -> None:
            # The context is borrowed: every read must happen inside the
            # callback, before the paint pass returns.
            self.assertGreaterEqual(context.width, 40.0)
            self.assertGreaterEqual(context.height, 20.0)
            self.assertGreater(context.scale, 0.0)
            clip = context.clip_rect()
            self.assertIsNotNone(clip)
            context.fill_rect(0.0, 0.0, 12.0, 12.0, fill_color)
            context.fill_rounded_rect(0.0, 0.0, 8.0, 8.0, 2.0, pt.rgb(0x00AA33))
            context.stroke_rect(1.0, 1.0, 6.0, 6.0, 1.0, pt.rgb(0x0033AA))
            context.draw_line(0.0, 0.0, 4.0, 4.0, 1.0, pt.rgb(0x333333))
            context.draw_text("sans", 12.0, pt.font_weight_regular,
                              pt.font_slant_upright, 2.0, 10.0, "Pt", pt.rgb(0x111111))
            self.assertGreater(context.measure_text(
                "sans", 12.0, pt.font_weight_regular, pt.font_slant_upright, "Pt"), 0.0)
            self.assertIsNotNone(context.font_metrics(
                "sans", 12.0, pt.font_weight_regular, pt.font_slant_upright))
            self.assertTrue(context.font_has_glyph("sans", 12.0, ord("P")))
            checked.append(True)

        box = pt.box(self.column)
        box.set_size(40, 20)
        box.on_paint(paint)
        pixels = pt.paint_window_pixels(self.window, 320, 240)
        self.assertEqual(checked, [True])
        # The fill landed in the offscreen buffer, so the callback really ran
        # through the paint pass instead of being a registration no-op.
        # Colors arrive as 0xAARRGGBB; pt.rgb packs 0xRRGGBBAA.
        buffer_value = ((fill_color & 0xFF) << 24) | (fill_color >> 8)
        self.assertIn(buffer_value, pixels)

        box.clear_paint_callback()
        checked.clear()
        pt.paint_window_pixels(self.window, 320, 240)
        self.assertEqual(checked, [])

    def test_watch_fd_read_events(self) -> None:
        import os

        read_fd, write_fd = os.pipe()
        try:
            events: list[int] = []
            watch = self.app.watch_fd(read_fd, events.append)
            os.write(write_fd, b"x")
            self.assertEqual(self.app.run_for(50), 0)
            self.assertTrue(events)
            self.assertTrue(events[0] & pt.fd_read)
            self.app.unwatch_fd(watch)
            events.clear()
            os.write(write_fd, b"y")
            self.assertEqual(self.app.run_for(50), 0)
            self.assertEqual(events, [])
        finally:
            os.close(read_fd)
            os.close(write_fd)

    def test_primary_selection_roundtrip(self) -> None:
        # Headless keeps an in-process buffer so both sides stay testable.
        self.assertTrue(self.window.set_primary("primary payload"))
        self.assertEqual(self.window.primary, "primary payload")
        self.window.primary = "second"
        self.assertEqual(self.window.primary, "second")

    def test_extension_binding_signatures(self) -> None:
        """Every 1.1 extension entry point pins restype and argtypes."""
        v = ctypes.c_void_p
        f = ctypes.c_float
        i = ctypes.c_int
        u32 = ctypes.c_uint32
        u64 = ctypes.c_uint64
        b = ctypes.c_bool
        sz = ctypes.c_size_t
        pf = ("ptr", ctypes.c_float)
        pu32 = ("ptr", ctypes.c_uint32)
        signatures = {
            "pt_widget_set_paint_callback": (None, [v, pt._PAINT, v]),
            "pt_widget_clear_paint_callback": (None, [v]),
            "pt_widget_request_redraw": (None, [v]),
            "pt_widget_request_redraw_rect": (None, [v, f, f, f, f]),
            "pt_paint_width": (f, [v]),
            "pt_paint_height": (f, [v]),
            "pt_paint_scale": (f, [v]),
            "pt_paint_clip_rect": (b, [v, pf, pf, pf, pf]),
            "pt_paint_push_clip": (None, [v, f, f, f, f]),
            "pt_paint_pop_clip": (None, [v]),
            "pt_paint_fill_rect": (None, [v, f, f, f, f, u32]),
            "pt_paint_fill_rounded_rect": (None, [v, f, f, f, f, f, u32]),
            "pt_paint_stroke_rect": (None, [v, f, f, f, f, f, u32]),
            "pt_paint_draw_line": (None, [v, f, f, f, f, f, u32]),
            "pt_paint_draw_text": (None, [v, ctypes.c_char_p, f, i, i, f, f,
                                            ctypes.c_char_p, sz, u32]),
            "pt_paint_font_has_glyph": (b, [v, ctypes.c_char_p, f, u32]),
            "pt_paint_measure_text": (f, [v, ctypes.c_char_p, f, i, i,
                                           ctypes.c_char_p, sz]),
            "pt_paint_font_metrics": (b, [v, ctypes.c_char_p, f, i, i,
                                           pf, pf, pf, pf]),
            "pt_window_paint_pixels": (b, [v, pu32, i, i]),
            "pt_window_set_focus_callback": (None, [v, pt._FOCUS, v]),
            "pt_window_set_key_event_callback": (None, [v, pt._KEY_EVENT, v]),
            "pt_window_set_pointer_callback": (None, [v, pt._POINTER, v]),
            "pt_window_set_ime_callbacks": (None, [v, pt._IME_COMMIT,
                                                   pt._IME_PREEDIT, v]),
            "pt_window_set_ime_enabled": (None, [v, b]),
            "pt_window_set_ime_cursor_rect": (None, [v, i, i, i, i]),
            "pt_window_primary_set": (b, [v, ctypes.c_char_p]),
            "pt_window_primary_get": (v, [v]),
            "pt_app_watch_fd": (u64, [v, i, u32, pt._FD, v]),
            "pt_app_unwatch_fd": (None, [v, u64]),
        }

        def matches(actual: Any, expected: Any) -> bool:
            if isinstance(expected, tuple):
                return getattr(actual, "_type_", None) is expected[1]
            return actual is expected

        self.assertEqual(len(signatures), 29)
        for name, (restype, argtypes) in signatures.items():
            function = getattr(pt, f"_{name}")
            self.assertIs(function.restype, restype, name)
            self.assertEqual(len(function.argtypes), len(argtypes), name)
            for index, expected in enumerate(argtypes):
                self.assertTrue(matches(function.argtypes[index], expected),
                                f"{name} arg {index}")

        callback_signatures = {
            pt._PAINT: (None, (v, v, v)),
            pt._FD: (None, (v, u32, v)),
            pt._IME_COMMIT: (None, (v, ctypes.c_char_p, v)),
            pt._IME_PREEDIT: (None, (v, ctypes.c_char_p, ctypes.c_int32,
                                     ctypes.c_int32, v)),
            pt._FOCUS: (None, (v, b, v)),
            pt._KEY_EVENT: (b, (v, u32, u32, ctypes.c_char_p, v)),
            pt._POINTER: (b, (v, ("ptr", pt.PointerEvent), v)),
        }
        for callback_type, (restype, argtypes) in callback_signatures.items():
            self.assertIs(callback_type._restype_, restype)
            self.assertEqual(len(callback_type._argtypes_), len(argtypes))
            for index, expected in enumerate(argtypes):
                self.assertTrue(matches(callback_type._argtypes_[index], expected))

    def test_paint_context_null_paths(self) -> None:
        context = pt.PaintContext(0)
        self.assertEqual(context.width, 0.0)
        self.assertEqual(context.height, 0.0)
        self.assertEqual(context.scale, 1.0)  # the C default for no canvas
        self.assertIsNone(context.clip_rect())
        self.assertIs(context.push_clip(0, 0, 1, 1), context)
        self.assertIs(context.pop_clip(), context)
        self.assertIs(context.fill_rect(0, 0, 1, 1, 0xFFFFFFFF), context)
        self.assertIs(context.fill_rounded_rect(0, 0, 1, 1, 2, 0xFFFFFFFF), context)
        self.assertIs(context.stroke_rect(0, 0, 1, 1, 1, 0xFFFFFFFF), context)
        self.assertIs(context.draw_line(0, 0, 1, 1, 1, 0xFFFFFFFF), context)
        self.assertIs(context.draw_text("sans", 10, pt.font_weight_regular,
                                        pt.font_slant_upright, 0, 0, "x",
                                        0xFFFFFFFF), context)
        self.assertEqual(context.measure_text("sans", 10, pt.font_weight_regular,
                                              pt.font_slant_upright, "x"), 0.0)
        self.assertIsNone(context.font_metrics("sans", 10, pt.font_weight_regular,
                                               pt.font_slant_upright))
        self.assertFalse(context.font_has_glyph("sans", 10, ord("P")))

    def test_paint_callback_dispatch_and_payload_copy(self) -> None:
        fill_color = pt.rgb(0xAA3300)
        widgets: list[pt.Widget] = []
        payloads: list[dict[str, Any]] = []
        calls: list[int] = []

        def paint(widget: pt.Widget, context: pt.PaintContext) -> None:
            # Copy every value that is needed later while the context is
            # borrowed. Nothing below retains the native pointer.
            clip = context.clip_rect()
            widgets.append(widget)
            payloads.append({
                "width": context.width,
                "height": context.height,
                "scale": context.scale,
                "clip": clip,
                "measured": context.measure_text(
                    "sans", 12.0, pt.font_weight_regular,
                    pt.font_slant_upright, "Pt"),
                "metrics": context.font_metrics(
                    "sans", 12.0, pt.font_weight_regular,
                    pt.font_slant_upright),
                "glyph": context.font_has_glyph("sans", 12.0, ord("P")),
            })
            context.push_clip(0.0, 0.0, 40.0, 20.0).fill_rect(
                0.0, 0.0, 40.0, 20.0, fill_color).pop_clip()
            context.fill_rounded_rect(0.0, 0.0, 8.0, 8.0, 2.0, pt.rgb(0x00AA33))
            context.stroke_rect(1.0, 1.0, 6.0, 6.0, 1.0, pt.rgb(0x0033AA))
            context.draw_line(0.0, 0.0, 4.0, 4.0, 1.0, pt.rgb(0x333333))
            context.draw_text("sans", 12.0, pt.font_weight_regular,
                              pt.font_slant_upright, 2.0, 10.0, "Pt",
                              pt.rgb(0x111111))
            calls.append(1)

        box = pt.box(self.column)
        box.set_size(40, 20)
        box.on_paint(paint)
        first_pixels = pt.paint_window_pixels(self.window, 320, 240)
        self.assertEqual(len(first_pixels), 320 * 240)
        self.assertTrue(calls)
        self.assertIs(widgets[0], box)
        payload = payloads[0]
        self.assertGreaterEqual(payload["width"], 40.0)
        self.assertGreaterEqual(payload["height"], 20.0)
        self.assertGreater(payload["scale"], 0.0)
        self.assertIsNotNone(payload["clip"])
        if payload["clip"] is not None:
            self.assertGreaterEqual(payload["clip"][2], 40.0)
            self.assertGreaterEqual(payload["clip"][3], 20.0)
        self.assertGreater(payload["measured"], 0.0)
        self.assertIsNotNone(payload["metrics"])
        if payload["metrics"] is not None:
            self.assertEqual(len(payload["metrics"]), 4)
            self.assertGreater(payload["metrics"][2], 0.0)
        self.assertTrue(payload["glyph"])

        # A second snapshot must run the callback again and report the same
        # geometry: the paint pass is idempotent for this widget.
        first_count = len(calls)
        second_pixels = pt.paint_window_pixels(self.window, 320, 240)
        self.assertEqual(len(second_pixels), 320 * 240)
        self.assertGreater(len(calls), first_count)
        self.assertEqual(payloads[first_count], payloads[-1])

        # 0xRRGGBBAA in, 0xAARRGGBB in the buffer.
        buffer_value = ((fill_color & 0xFF) << 24) | (fill_color >> 8)
        self.assertIn(buffer_value, first_pixels)
        self.assertIn(buffer_value, second_pixels)

        box.clear_paint_callback()
        count = len(calls)
        pt.paint_window_pixels(self.window, 320, 240)
        self.assertEqual(len(calls), count)

    def test_paint_redraw_requests_and_offscreen_errors(self) -> None:
        ran: list[bool] = []
        box = pt.box(self.column)
        box.set_size(24, 16)
        box.on_paint(lambda _widget, _context: ran.append(True))
        self.assertIs(box.request_redraw(), box)
        self.assertIs(box.request_redraw_rect(-5.0, -5.0, 4.0, 4.0), box)
        self.assertIs(box.request_redraw_rect(0.0, 0.0, 0.0, 0.0), box)
        pt.paint_window_pixels(self.window, 320, 240)
        self.assertTrue(ran)
        ran.clear()
        box.clear_paint_callback()

        with self.assertRaises(ValueError):
            pt.paint_window_pixels(self.window, 0, 10)
        with self.assertRaises(ValueError):
            pt.paint_window_pixels(self.window, 10, -1)
        buffer = (ctypes.c_uint32 * 4)()
        self.assertFalse(pt._pt_window_paint_pixels(None, buffer, 2, 2))
        self.assertFalse(pt._pt_window_paint_pixels(self.window.raw, None, 2, 2))
        self.assertFalse(pt._pt_window_paint_pixels(self.window.raw, buffer, 0, 2))

    def test_primary_selection_ownership_and_utf8(self) -> None:
        self.assertIs(self.window.set_primary("primary payload"), self.window)
        self.assertEqual(self.window.primary, "primary payload")
        self.window.primary = "第一行 naïve ✓"
        self.assertEqual(self.window.primary, "第一行 naïve ✓")
        self.assertEqual(self.window.set_primary("").primary, "")
        self.assertEqual(self.window.primary, "")

        # The getter hands malloc'd memory to the wrapper; the wrapper must
        # copy it and release it exactly once.
        freed: list[int] = []
        original_free = pt._pt_free

        def tracked_free(pointer: Any) -> None:
            freed.append(int(pointer))
            original_free(pointer)

        pt._pt_free = tracked_free
        try:
            self.assertEqual(self.window.primary, "")
            self.window.primary = "release me"
            self.assertEqual(self.window.primary, "release me")
        finally:
            pt._pt_free = original_free
        self.assertEqual(len(freed), 2)
        self.assertTrue(all(freed))

    def test_fd_watch_dispatch_events_and_validation(self) -> None:
        read_fd, write_fd = os.pipe()
        write_open = True
        try:
            read_events: list[int] = []
            watch = self.app.watch_fd(read_fd, read_events.append)
            self.assertGreater(watch, 0)
            self.assertIn(watch, self.app._watch_fds)
            os.write(write_fd, b"x")
            self.assertEqual(self.app.run_for(60), 0)
            self.assertTrue(read_events)
            self.assertTrue(read_events[0] & pt.fd_read)

            self.app.unwatch_fd(watch)
            self.assertNotIn(watch, self.app._watch_fds)
            self.app.unwatch_fd(watch)  # idempotent
            read_events.clear()
            os.write(write_fd, b"y")
            self.assertEqual(self.app.run_for(40), 0)
            self.assertEqual(read_events, [])

            # The header allows two independent watches on the same fd.
            first: list[int] = []
            second: list[int] = []
            first_id = self.app.watch_fd(read_fd, first.append)
            second_id = self.app.watch_fd(read_fd, second.append)
            self.assertNotEqual(first_id, second_id)
            os.write(write_fd, b"z")
            self.assertEqual(self.app.run_for(60), 0)
            self.assertTrue(first and second)
            self.app.unwatch_fd(first_id)
            self.app.unwatch_fd(second_id)

            # A writable descriptor is reported immediately.
            writable: list[int] = []
            write_watch = self.app.watch_fd(write_fd, writable.append,
                                            events=pt.fd_write)
            self.assertEqual(self.app.run_for(60), 0)
            self.assertTrue(writable)
            self.assertTrue(writable[0] & pt.fd_write)
            self.app.unwatch_fd(write_watch)

            # A callback may unwatch itself; it must fire at least once and
            # then stop, without a use-after-free in the C dispatcher.
            self_unwatch = {"id": 0, "events": []}

            def unwatch_self(mask: int) -> None:
                self_unwatch["events"].append(mask)
                self.app.unwatch_fd(self_unwatch["id"])

            self_unwatch["id"] = self.app.watch_fd(read_fd, unwatch_self)
            self.assertEqual(self.app.run_for(60), 0)
            self.assertEqual(len(self_unwatch["events"]), 1)
            os.write(write_fd, b"w")
            self.assertEqual(self.app.run_for(40), 0)
            self.assertEqual(len(self_unwatch["events"]), 1)

            # POLLHUP is surfaced once the write end disappears.
            hung_up: list[int] = []
            hup_watch = self.app.watch_fd(read_fd, hung_up.append)
            os.close(write_fd)
            write_open = False
            self.assertEqual(self.app.run_for(60), 0)
            self.assertTrue(hung_up)
            self.assertTrue(hung_up[0] & (pt.fd_hangup | pt.fd_error))
            self.app.unwatch_fd(hup_watch)

            with self.assertRaises(ValueError):
                self.app.watch_fd(-1, read_events.append)
            with self.assertRaises(ValueError):
                self.app.watch_fd(read_fd, read_events.append, events=0)
            self.app.unwatch_fd(0)
        finally:
            if write_open:
                os.close(write_fd)
            os.close(read_fd)

    def test_ime_callback_dispatch_and_clear(self) -> None:
        commits: list[tuple[pt.Window, str]] = []
        preedits: list[tuple[pt.Window, str, int, int]] = []
        registered: list[tuple[Any, Any]] = []
        original = pt._pt_window_set_ime_callbacks

        def spy(window: int, commit: Any, preedit: Any, data: int) -> None:
            registered.append((commit, preedit))
            original(window, commit, preedit, data)

        pt._pt_window_set_ime_callbacks = spy
        try:
            self.assertIs(self.window.set_ime_callbacks(
                commit=lambda window, text: commits.append((window, text)),
                preedit=lambda window, text, begin, end: preedits.append(
                    (window, text, begin, end)),
            ), self.window)
            self.assertIs(self.window.set_ime_enabled(True), self.window)
            self.assertIs(self.window.set_ime_cursor_rect(4, 8, 12, 16),
                          self.window)
        finally:
            pt._pt_window_set_ime_callbacks = original

        self.assertEqual(len(registered), 1)
        commit_ptr, preedit_ptr = registered[0]
        self.assertIsNot(commit_ptr, pt._NULL_IME_COMMIT)
        self.assertIsNot(preedit_ptr, pt._NULL_IME_PREEDIT)

        # Execute the exact trampoline registered with C with a synthetic
        # payload; this checks the upcall decoding and copy semantics.
        commit_ptr(self.window.raw, b"hello \xe4\xb8\x96", None)
        preedit_ptr(self.window.raw, b"ni", 0, 2, None)
        self.assertEqual(commits, [(self.window, "hello 世")])
        self.assertEqual(preedits, [(self.window, "ni", 0, 2)])
        commit_ptr(self.window.raw, None, None)
        preedit_ptr(self.window.raw, b"", -1, -1, None)
        self.assertEqual(commits[-1], (self.window, ""))
        self.assertEqual(preedits[-1], (self.window, "", -1, -1))

        registered.clear()
        pt._pt_window_set_ime_callbacks = spy
        try:
            # Clearing both removes custom handling; omitting preedit leaves
            # that slot NULL independently.
            self.window.set_ime_callbacks()
            self.window.set_ime_callbacks(
                commit=lambda _window, _text: None)
            self.assertIs(self.window.set_ime_enabled(False), self.window)
        finally:
            pt._pt_window_set_ime_callbacks = original
        self.assertEqual(len(registered), 2)
        self.assertIs(registered[0][0], pt._NULL_IME_COMMIT)
        self.assertIs(registered[0][1], pt._NULL_IME_PREEDIT)
        self.assertIsNot(registered[1][0], pt._NULL_IME_COMMIT)
        self.assertIs(registered[1][1], pt._NULL_IME_PREEDIT)

    def test_raw_input_callbacks_dispatch_and_payloads(self) -> None:
        focus_events: list[tuple[pt.Window, bool]] = []
        key_events: list[tuple[pt.Window, int, int, str]] = []
        pointer_events: list[pt.PointerEvent] = []
        captured: dict[str, Any] = {}

        def install(name: str, original: Any, register: Any) -> None:
            def spy(window: int, callback: Any, data: Any) -> None:
                captured[name] = callback
                original(window, callback, data)

            setattr(pt, name, spy)
            try:
                register()
            finally:
                setattr(pt, name, original)

        install("_pt_window_set_focus_callback",
                pt._pt_window_set_focus_callback,
                lambda: self.window.on_focus(
                    lambda window, focused: focus_events.append(
                        (window, focused))))
        install("_pt_window_set_key_event_callback",
                pt._pt_window_set_key_event_callback,
                lambda: self.window.on_key_event(
                    lambda window, key, modifiers, text: key_events.append(
                        (window, key, modifiers, text)) or True))
        install("_pt_window_set_pointer_callback",
                pt._pt_window_set_pointer_callback,
                lambda: self.window.on_pointer(
                    lambda window, event: pointer_events.append(event) or True))

        self.assertTrue(captured)

        captured["_pt_window_set_focus_callback"](self.window.raw, True, None)
        captured["_pt_window_set_focus_callback"](self.window.raw, False, None)
        self.assertEqual(focus_events, [(self.window, True), (self.window, False)])

        consumed = captured["_pt_window_set_key_event_callback"](
            self.window.raw, 0x61, pt.key_modifier_ctrl, b"a", None)
        self.assertTrue(consumed)
        captured["_pt_window_set_key_event_callback"](
            self.window.raw, 0xFF09, 0, None, None)
        self.assertEqual(key_events[0],
                         (self.window, 0x61, pt.key_modifier_ctrl, "a"))
        self.assertEqual(key_events[1], (self.window, 0xFF09, 0, ""))

        event = pt.PointerEvent()
        event.phase = pt.pointer_phase_down
        event.x = 12.0
        event.y = 34.0
        event.delta_x = 1.5
        event.delta_y = -2.5
        event.button = 0x110
        event.modifiers = pt.key_modifier_shift
        event.click_count = 2
        event.pressed = True
        consumed_pointer = captured["_pt_window_set_pointer_callback"](
            self.window.raw, ctypes.pointer(event), None)
        self.assertTrue(consumed_pointer)
        self.assertEqual(len(pointer_events), 1)
        received = pointer_events[0]
        self.assertEqual(received.phase, pt.pointer_phase_down)
        self.assertEqual(received.position, (12.0, 34.0))
        self.assertEqual(received.delta_x, 1.5)
        self.assertEqual(received.delta_y, -2.5)
        self.assertEqual(received.button, 0x110)
        self.assertEqual(received.modifiers, pt.key_modifier_shift)
        self.assertEqual(received.click_count, 2)
        self.assertTrue(received.pressed)
        # The wrapper copies the C struct, so mutating the caller's copy after
        # the upcall must not affect what the callback retained.
        event.x = 999.0
        event.pressed = False
        self.assertEqual(received.x, 12.0)
        self.assertTrue(received.pressed)

        # A declining pointer callback propagates False.
        declined: list[bool] = []
        install("_pt_window_set_pointer_callback",
                pt._pt_window_set_pointer_callback,
                lambda: self.window.on_pointer(
                    lambda _window, _event: declined.append(True) or False))
        self.assertFalse(captured["_pt_window_set_pointer_callback"](
            self.window.raw, ctypes.pointer(event), None))
        self.assertEqual(declined, [True])

    def test_pointer_event_layout(self) -> None:
        event = pt.PointerEvent
        self.assertEqual(event.phase.offset, 0)
        self.assertEqual(event.x.offset, 4)
        self.assertEqual(event.y.offset, 8)
        self.assertEqual(event.delta_x.offset, 12)
        self.assertEqual(event.delta_y.offset, 16)
        self.assertEqual(event.button.offset, 20)
        self.assertEqual(event.modifiers.offset, 24)
        self.assertEqual(event.click_count.offset, 28)
        self.assertEqual(event.pressed.offset, 32)
        self.assertEqual(ctypes.sizeof(event), 36)
        self.assertIs(event.phase.type, ctypes.c_uint32)
        self.assertIs(event.pressed.type, ctypes.c_bool)


if __name__ == "__main__":
    unittest.main()
