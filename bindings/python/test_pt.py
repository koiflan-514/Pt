"""Headless tests for the Pt Python binding.

Run with:
  PT_HEADLESS=1 PT_LIBRARY=build/libpt.so PYTHONPATH=bindings/python \
      python3 -m unittest discover -s bindings/python -p 'test_*.py'

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import ctypes
import gc
import os
import shutil
import struct
import tempfile
import time
import unittest
import zlib
from typing import Any

import pt


def _make_png(width: int, height: int, color: tuple[int, int, int, int] = (255, 0, 0, 255)) -> bytes:
    """Builds a minimal 8-bit RGBA non-interlaced PNG for the icon tests."""
    row = bytes(color) * width
    raw = b"".join(b"\x00" + row for _ in range(height))

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


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
        # Headless keeps an in-process buffer so both sides stay testable, but
        # a programmatic set has no triggering event serial and is rejected.
        self.window.set_primary("primary payload")
        self.assertEqual(self.window.primary, "")
        self.assertFalse(self.window.set_primary_serial("primary payload", 0))
        self.assertEqual(self.window.primary, "")
        self.assertTrue(self.window.set_primary_serial("primary payload", 7))
        self.assertEqual(self.window.primary, "primary payload")
        self.window.primary = "second"
        self.assertEqual(self.window.primary, "primary payload")
        self.assertTrue(self.window.set_primary_serial("second", 9))
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
            "pt_window_paint_pixels_ex": (b, [v, pu32, i, i,
                                               ("ptr", pt.PaintPixelsDesc)]),
            "pt_window_set_decoration_mode": (None, [v, i]),
            "pt_window_get_decoration_mode": (i, [v]),
            "pt_window_get_effective_decoration_mode": (i, [v]),
            "pt_window_maximize": (b, [v]),
            "pt_window_set_focus_callback": (None, [v, pt._FOCUS, v]),
            "pt_window_set_key_event_callback": (None, [v, pt._KEY_EVENT, v]),
            "pt_window_set_pointer_callback": (None, [v, pt._POINTER, v]),
            "pt_window_set_ime_callbacks": (None, [v, pt._IME_COMMIT,
                                                   pt._IME_PREEDIT, v]),
            "pt_window_set_ime_enabled": (None, [v, b]),
            "pt_window_set_ime_cursor_rect": (None, [v, i, i, i, i]),
            "pt_window_primary_set": (b, [v, ctypes.c_char_p]),
            "pt_window_primary_set_serial": (b, [v, ctypes.c_char_p, u32]),
            "pt_window_primary_get": (v, [v]),
            "pt_app_watch_fd": (u64, [v, i, u32, pt._FD, v]),
            "pt_app_watch_fd_owned": (u64, [v, i, u32, pt._FD, v]),
            "pt_app_unwatch_fd": (None, [v, u64]),
        }

        def matches(actual: Any, expected: Any) -> bool:
            if isinstance(expected, tuple):
                return getattr(actual, "_type_", None) is expected[1]
            return actual is expected

        self.assertEqual(len(signatures), 36)
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

    def test_decoration_modes_and_maximize(self) -> None:
        self.assertEqual(self.window.decoration_mode, pt.decoration_client)
        self.assertEqual(self.window.effective_decoration_mode, pt.decoration_client)
        # set_decorated only controls the header; it never requests SSD.
        self.window.set_decorated(False)
        self.assertEqual(self.window.decoration_mode, pt.decoration_client)
        self.assertEqual(self.window.effective_decoration_mode, pt.decoration_client)
        self.window.set_decorated(True)
        self.window.set_decoration_mode(pt.decoration_server)
        self.assertEqual(self.window.decoration_mode, pt.decoration_server)
        # Headless has no configure event; the effective mode follows.
        self.assertEqual(self.window.effective_decoration_mode, pt.decoration_server)
        self.window.set_decoration_mode(pt.decoration_auto)
        self.assertEqual(self.window.decoration_mode, pt.decoration_auto)
        self.assertEqual(self.window.effective_decoration_mode, pt.decoration_client)
        self.window.decoration_mode = 99  # rejected by the C entry point
        self.assertEqual(self.window.decoration_mode, pt.decoration_auto)
        self.assertEqual(self.window.effective_decoration_mode, pt.decoration_client)

        self.assertTrue(self.window.maximize())
        self.window.set_resizable(False)
        self.assertFalse(self.window.maximize())  # not resizable
        self.window.set_resizable(True)
        self.assertTrue(self.window.maximize())
        self.assertFalse(pt._pt_window_maximize(None))
        self.assertEqual(pt._pt_window_get_decoration_mode(None), pt.decoration_auto)
        self.assertEqual(pt._pt_window_get_effective_decoration_mode(None),
                         pt.decoration_auto)

    def test_paint_pixels_ex_stride_and_scale(self) -> None:
        width = self.window.width
        height = self.window.height
        tight = pt.paint_window_pixels(self.window, width, height)
        padded = pt.paint_window_pixels_ex(self.window, width, height,
                                           stride=width + 8)
        self.assertEqual(len(padded), width * height)
        self.assertEqual(padded, tight)
        self.assertEqual(self.window.paint_pixels_ex(width, height), tight)

        scaled = pt.paint_window_pixels_ex(self.window, width * 2, height * 2,
                                           scale=2.0)
        self.assertEqual(len(scaled), width * 2 * height * 2)
        self.assertEqual(pt.paint_window_pixels_ex(self.window, width, height,
                                                   scale=0.0), tight)

        desc = pt.PaintPixelsDesc(width, 1.0)
        self.assertEqual(desc.stride, width)
        self.assertAlmostEqual(desc.scale, 1.0)
        self.assertEqual(desc.format, pt.pixel_format_argb8888_premultiplied)
        self.assertEqual(list(desc.reserved), [0, 0, 0])

        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, height,
                                        pt.PaintPixelsDesc(reserved=(1, 0, 0)))
        bad_format = pt.PaintPixelsDesc()
        bad_format.format = 1
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, height, bad_format)
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, height,
                                        pt.PaintPixelsDesc(stride=width - 1))
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, height, pt.PaintPixelsDesc(scale=-1.0))
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, height,
                                        pt.PaintPixelsDesc(scale=float("nan")))
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, height,
                                        pt.PaintPixelsDesc(scale=float("inf")))
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(0, height)
        with self.assertRaises(ValueError):
            self.window.paint_pixels_ex(width, -1)

        buffer = (ctypes.c_uint32 * 4)()
        good_desc = pt.PaintPixelsDesc()
        self.assertFalse(pt._pt_window_paint_pixels_ex(
            None, buffer, 2, 2, ctypes.byref(good_desc)))
        self.assertFalse(pt._pt_window_paint_pixels_ex(
            self.window.raw, None, 2, 2, ctypes.byref(good_desc)))
        self.assertFalse(pt._pt_window_paint_pixels_ex(
            self.window.raw, buffer, 0, 2, ctypes.byref(good_desc)))

    def test_fd_watch_owned_closes_descriptor(self) -> None:
        read_fd, write_fd = os.pipe()
        events: list[int] = []
        try:
            watch = self.app.watch_fd_owned(read_fd, events.append)
            self.assertGreater(watch, 0)
            self.assertIn(watch, self.app._watch_fds)
            # The duplicate check covers owned descriptors too and must not
            # close the descriptor the first watch owns.
            with self.assertRaises(RuntimeError):
                self.app.watch_fd(read_fd, events.append)
            os.fstat(read_fd)
            os.write(write_fd, b"x")
            self.assertEqual(self.app.run_for(60), 0)
            self.assertTrue(events)
            self.assertTrue(events[0] & pt.fd_read)

            self.app.unwatch_fd(watch)
            self.assertNotIn(watch, self.app._watch_fds)
            with self.assertRaises(OSError):
                os.fstat(read_fd)  # Pt closed the owned descriptor
        finally:
            os.close(write_fd)
            try:
                os.close(read_fd)
            except OSError:
                pass

        # Destroying the app also closes every owned descriptor.
        other = pt.App("pt.tests.python.owned")
        read_fd, write_fd = os.pipe()
        try:
            self.assertGreater(other.watch_fd_owned(read_fd, lambda _events: None), 0)
            os.fstat(read_fd)
            other.destroy()
            with self.assertRaises(OSError):
                os.fstat(read_fd)
        finally:
            other.destroy()
            os.close(write_fd)
            try:
                os.close(read_fd)
            except OSError:
                pass

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
        # A programmatic publish has no trigger serial: nothing is set.
        self.assertEqual(self.window.primary, "")
        self.assertTrue(self.window.set_primary_serial("第一行 naïve ✓", 3))
        self.assertEqual(self.window.primary, "第一行 naïve ✓")
        self.assertTrue(self.window.set_primary_serial("", 4))
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
            self.assertTrue(self.window.set_primary_serial("release me", 5))
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

            # One watch per descriptor: a second watch is rejected with id 0, which
            # the wrapper surfaces as RuntimeError.
            first: list[int] = []
            first_id = self.app.watch_fd(read_fd, first.append)
            with self.assertRaises(RuntimeError):
                self.app.watch_fd(read_fd, first.append)
            os.write(write_fd, b"z")
            self.assertEqual(self.app.run_for(60), 0)
            self.assertTrue(first)
            self.app.unwatch_fd(first_id)

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


class M19BindingTests(unittest.TestCase):
    """1.2 wave: popups, font fallback, clipboard media, columns, icons, pickers."""

    def setUp(self) -> None:
        self.app = pt.App("pt.tests.python.12")
        self.window = pt.Window(self.app, "python 1.2 tests", 480, 360)
        self.root = self.window.root
        self.column = pt.column(self.root).set_gap(6)

    def tearDown(self) -> None:
        # Font fallback chains are process-wide: reset before the app goes away.
        try:
            self.app.set_font_fallbacks([])
        except Exception:
            pass
        self.app.destroy()

    @staticmethod
    def _capture(attribute: str, register: Any) -> Any:
        """Captures the trampoline passed to a private C setter."""
        captured: list[Any] = []
        original = getattr(pt, attribute)

        def spy(*args: Any) -> Any:
            captured.append(args[1])
            return original(*args)

        setattr(pt, attribute, spy)
        try:
            register()
        finally:
            setattr(pt, attribute, original)
        return captured[-1]

    # ------------------------------------------------------------------
    # Signature and layout pins for the whole 1.2 surface
    # ------------------------------------------------------------------

    def test_12_binding_signatures(self) -> None:
        """Every 1.2 entry point pins restype and argtypes."""
        v = ctypes.c_void_p
        cp = ctypes.c_char_p
        i = ctypes.c_int
        u32 = ctypes.c_uint32
        b = ctypes.c_bool
        d = ctypes.c_double
        f = ctypes.c_float
        sz = ctypes.c_size_t
        p_u32 = ("ptr", ctypes.c_uint32)
        p_i = ("ptr", ctypes.c_int)
        p_cp = ("ptr", ctypes.c_char_p)
        date = pt.Date
        anchor = pt.PopupAnchor

        signatures: dict[str, tuple[Any, list[Any]]] = {
            # popup (10)
            "pt_popup_create": (v, [v, v]),
            "pt_popup_destroy": (None, [v]),
            "pt_popup_show": (b, [v, anchor]),
            "pt_popup_reposition": (b, [v, anchor]),
            "pt_popup_dismiss": (None, [v]),
            "pt_popup_is_open": (b, [v]),
            "pt_popup_content": (v, [v]),
            "pt_popup_set_on_dismiss": (None, [v, pt._POPUP_DISMISS, v]),
            "pt_popup_set_placement": (None, [v, i]),
            "pt_popup_get_placement": (i, [v]),
            # font fallback (7)
            "pt_app_set_font_fallbacks": (None, [v, p_cp, i]),
            "pt_app_get_font_fallback_count": (i, [v]),
            "pt_app_get_font_fallback": (cp, [v, i]),
            "pt_app_set_font_fallbacks_for_family": (None, [v, cp, p_cp, i]),
            "pt_app_get_font_fallback_count_for_family": (i, [v, cp]),
            "pt_app_get_font_fallback_for_family": (cp, [v, cp, i]),
            "pt_app_font_uncovered_count": (i, [v, cp, cp, sz]),
            # clipboard media (3)
            "pt_window_clipboard_set_image_png": (b, [v, p_u32, i, i]),
            "pt_window_clipboard_get_image_png": (v, [v, p_i, p_i]),
            "pt_window_clipboard_set_uri_list": (b, [v, p_cp, u32]),
            # column persistence (4)
            "pt_table_set_column_key": (None, [v, u32, cp]),
            "pt_table_get_column_key": (cp, [v, u32]),
            "pt_table_columns_save": (v, [v]),
            "pt_table_columns_restore": (b, [v, cp]),
            # icons (12)
            "pt_icon_image_load_file": (v, [cp]),
            "pt_icon_image_load_memory": (v, [v, sz]),
            "pt_icon_image_from_name": (v, [cp, i]),
            "pt_icon_image_ref": (v, [v]),
            "pt_icon_image_destroy": (None, [v]),
            "pt_icon_image_width": (i, [v]),
            "pt_icon_image_height": (i, [v]),
            "pt_button_set_icon_image": (None, [v, v]),
            "pt_button_get_icon_image": (v, [v]),
            "pt_button_set_icon_tint": (None, [v, u32]),
            "pt_window_set_icon": (None, [v, v]),
            "pt_window_get_icon": (v, [v]),
            # searchable select (25)
            "pt_select_create": (v, [v]),
            "pt_select_set_placeholder": (None, [v, cp]),
            "pt_select_get_placeholder": (cp, [v]),
            "pt_select_add_item": (None, [v, cp]),
            "pt_select_insert_item": (None, [v, u32, cp]),
            "pt_select_add_item_with_data": (None, [v, cp, v]),
            "pt_select_remove_item": (None, [v, u32]),
            "pt_select_clear": (None, [v]),
            "pt_select_get_item_count": (u32, [v]),
            "pt_select_get_item_text": (cp, [v, u32]),
            "pt_select_get_item_data": (v, [v, u32]),
            "pt_select_set_item_data": (None, [v, u32, v]),
            "pt_select_get_selected": (u32, [v]),
            "pt_select_set_selected": (None, [v, u32]),
            "pt_select_clear_selection": (None, [v]),
            "pt_select_get_query": (cp, [v]),
            "pt_select_set_query": (None, [v, cp]),
            "pt_select_get_match_count": (u32, [v]),
            "pt_select_get_match_item": (u32, [v, u32]),
            "pt_select_set_max_popup_rows": (None, [v, u32]),
            "pt_select_get_max_popup_rows": (u32, [v]),
            "pt_select_open": (None, [v]),
            "pt_select_close": (None, [v]),
            "pt_select_is_open": (b, [v]),
            "pt_select_set_on_change": (None, [v, pt._INDEX, v]),
            # date (16)
            "pt_date_today": (date, []),
            "pt_date_is_valid": (b, [date]),
            "pt_date_picker_create": (v, [v]),
            "pt_date_picker_set_date": (b, [v, date]),
            "pt_date_picker_get_date": (date, [v]),
            "pt_date_picker_clear": (None, [v]),
            "pt_date_picker_set_format": (None, [v, i]),
            "pt_date_picker_get_format": (i, [v]),
            "pt_date_picker_set_text": (b, [v, cp]),
            "pt_date_picker_get_text": (cp, [v]),
            "pt_date_picker_set_placeholder": (None, [v, cp]),
            "pt_date_picker_get_placeholder": (cp, [v]),
            "pt_date_picker_open": (None, [v]),
            "pt_date_picker_close": (None, [v]),
            "pt_date_picker_is_open": (b, [v]),
            "pt_date_picker_set_on_change": (None, [v, pt._DATE_CALLBACK, v]),
            # number (16)
            "pt_number_picker_create": (v, [v]),
            "pt_number_picker_set_range": (None, [v, d, d]),
            "pt_number_picker_get_min": (d, [v]),
            "pt_number_picker_get_max": (d, [v]),
            "pt_number_picker_set_step": (None, [v, d]),
            "pt_number_picker_get_step": (d, [v]),
            "pt_number_picker_set_value": (None, [v, d]),
            "pt_number_picker_get_value": (d, [v]),
            "pt_number_picker_has_value": (b, [v]),
            "pt_number_picker_clear": (None, [v]),
            "pt_number_picker_step_by": (None, [v, d]),
            "pt_number_picker_set_text": (b, [v, cp]),
            "pt_number_picker_get_text": (cp, [v]),
            "pt_number_picker_set_placeholder": (None, [v, cp]),
            "pt_number_picker_get_placeholder": (cp, [v]),
            "pt_number_picker_set_on_change": (None, [v, pt._NUMBER_CALLBACK, v]),
            # colour (13)
            "pt_color_picker_create": (v, [v]),
            "pt_color_picker_set_color": (None, [v, u32]),
            "pt_color_picker_get_color": (u32, [v]),
            "pt_color_picker_set_hex": (b, [v, cp]),
            "pt_color_picker_get_hex": (cp, [v]),
            "pt_color_picker_add_preset": (None, [v, u32]),
            "pt_color_picker_clear_presets": (None, [v]),
            "pt_color_picker_get_preset_count": (u32, [v]),
            "pt_color_picker_get_preset": (u32, [v, u32]),
            "pt_color_picker_open": (None, [v]),
            "pt_color_picker_close": (None, [v]),
            "pt_color_picker_is_open": (b, [v]),
            "pt_color_picker_set_on_change": (None, [v, pt._COLOR_CALLBACK, v]),
        }

        def matches(actual: Any, expected: Any) -> bool:
            if isinstance(expected, tuple):
                return getattr(actual, "_type_", None) is expected[1]
            return actual is expected

        self.assertEqual(len(signatures), 106)
        for name, (restype, argtypes) in signatures.items():
            function = getattr(pt, f"_{name}")
            self.assertIs(function.restype, restype, name)
            self.assertEqual(len(function.argtypes), len(argtypes), name)
            for index, expected in enumerate(argtypes):
                self.assertTrue(matches(function.argtypes[index], expected),
                                f"{name} arg {index}")

        callback_signatures = {
            pt._POPUP_DISMISS: (None, (v, v)),
            pt._DATE_CALLBACK: (None, (v, date, v)),
            pt._NUMBER_CALLBACK: (None, (v, ctypes.c_double, v)),
            pt._COLOR_CALLBACK: (None, (v, u32, v)),
        }
        for callback_type, (restype, argtypes) in callback_signatures.items():
            self.assertIs(callback_type._restype_, restype)
            self.assertEqual(len(callback_type._argtypes_), len(argtypes))
            for index, expected in enumerate(argtypes):
                self.assertTrue(matches(callback_type._argtypes_[index], expected))

        self.assertEqual(pt.popup_placement_below, 0)
        self.assertEqual(pt.popup_placement_above, 1)
        self.assertEqual(pt.popup_placement_right, 2)
        self.assertEqual(pt.popup_placement_left, 3)
        self.assertEqual(pt.date_format_iso, 0)
        self.assertEqual(pt.date_format_dmy, 1)
        self.assertEqual(pt.date_format_mdy, 2)
        self.assertEqual(pt.popup_max_open, 16)
        self.assertEqual(pt.select_max_items, 1024)
        self.assertEqual(pt.picker_max_candidates, 256)
        self.assertEqual(pt.color_max_presets, 64)
        self.assertEqual(pt.clipboard_image_max_dimension, 8192)
        self.assertEqual(pt.icon_max_dimension, 4096)
        self.assertEqual(pt.font_fallback_max_families, 8)
        self.assertEqual(pt.font_fallback_max_overrides, 16)
        self.assertEqual(pt.table_max_columns, 1024)
        self.assertEqual(pt.table_column_key_max, 64)
        self.assertEqual(pt.table_column_layout_max, 262144)

    def test_12_struct_layouts(self) -> None:
        date = pt.Date
        self.assertEqual(date.year.offset, 0)
        self.assertEqual(date.month.offset, 4)
        self.assertEqual(date.day.offset, 5)
        self.assertIs(date.year.type, ctypes.c_int32)
        self.assertIs(date.month.type, ctypes.c_uint8)
        self.assertIs(date.day.type, ctypes.c_uint8)
        self.assertEqual(ctypes.sizeof(date), 8)

        anchor = pt.PopupAnchor
        self.assertEqual(anchor.x.offset, 0)
        self.assertEqual(anchor.y.offset, 4)
        self.assertEqual(anchor.width.offset, 8)
        self.assertEqual(anchor.height.offset, 12)
        self.assertIs(anchor.x.type, ctypes.c_float)
        self.assertEqual(ctypes.sizeof(anchor), 16)

        self.assertTrue(pt.Date(2024, 2, 29).is_valid())
        self.assertFalse(pt.Date(2023, 2, 29).is_valid())
        self.assertTrue(pt.date_is_valid(pt.Date(2000, 2, 29)))
        self.assertFalse(pt.date_is_valid(pt.Date(0, 0, 0)))
        today = pt.date_today()
        self.assertIsInstance(today, pt.Date)
        self.assertTrue(today.is_valid())
        self.assertEqual((pt.PopupAnchor(1, 2, 3, 4).x,
                          pt.PopupAnchor(1, 2, 3, 4).height), (1.0, 4.0))

    # ------------------------------------------------------------------
    # Popups
    # ------------------------------------------------------------------

    def test_12_popup_show_dismiss_and_lifetime(self) -> None:
        self.window.show()
        content = pt.box(None)
        popup = pt.Popup.create(self.window, content)
        self.assertFalse(popup.is_open)
        self.assertEqual(popup.placement, pt.popup_placement_below)
        self.assertIs(pt.Popup._wrap(popup.raw), popup)
        self.assertIs(popup.content, content)

        self.assertTrue(popup.show((10.0, 20.0, 30.0, 40.0)))
        self.assertTrue(popup.is_open)
        self.assertTrue(popup.show(pt.PopupAnchor(12.0, 24.0, 0.0, 0.0)))
        self.assertTrue(popup.is_open)
        self.assertTrue(popup.reposition((14.0, 28.0, 8.0, 8.0)))

        popup.placement = pt.popup_placement_above
        self.assertEqual(popup.placement, pt.popup_placement_above)
        popup.placement = 99
        self.assertEqual(popup.placement, pt.popup_placement_below)

        self.assertFalse(popup.show(pt.PopupAnchor(float("nan"), 0.0, 1.0, 1.0)))
        self.assertTrue(popup.is_open)
        self.assertFalse(popup.show(pt.PopupAnchor(0.0, 0.0, -1.0, 1.0)))

        dismissals: list[tuple[bool, bool]] = []

        def on_dismiss(target: pt.Popup) -> None:
            dismissals.append((target is popup, target.is_open))

        popup.on_dismiss(on_dismiss)
        popup.dismiss()
        self.assertFalse(popup.is_open)
        self.assertEqual(dismissals, [(True, False)])
        popup.dismiss()
        self.assertEqual(len(dismissals), 1)
        self.assertFalse(popup.reposition((0.0, 0.0, 1.0, 1.0)))

        # A direct destroy is not a dismissal and must not fire the callback.
        self.assertTrue(popup.show((4.0, 4.0, 1.0, 1.0)))
        popup.destroy()
        self.assertEqual(len(dismissals), 1)
        self.assertEqual(popup.raw, 0)
        self.assertFalse(popup.is_open)
        self.assertIsNone(popup.content)
        self.assertFalse(popup.show((0.0, 0.0, 1.0, 1.0)))
        popup.dismiss()
        popup.destroy()

        # Content wrappers are invalidated by the owning popup's destruction.
        self.assertEqual(content.raw, 0)

    def test_12_popup_open_limit_and_clear_callback(self) -> None:
        self.window.show()
        popups = []
        for index in range(pt.popup_max_open):
            content = pt.box(None).set_size(8.0, 8.0)
            popup = pt.Popup.create(self.window, content)
            popup.on_dismiss(None)
            self.assertTrue(popup.show((index, 0.0, 1.0, 1.0)), index)
            popups.append(popup)

        content = pt.box(None).set_size(8.0, 8.0)
        overflow = pt.Popup.create(self.window, content)
        self.assertFalse(overflow.show((0.0, 0.0, 1.0, 1.0)))
        self.assertFalse(overflow.is_open)
        overflow.destroy()

        for popup in popups:
            popup.destroy()

    # ------------------------------------------------------------------
    # Font fallback
    # ------------------------------------------------------------------

    def test_12_font_fallback_chains(self) -> None:
        self.assertEqual(self.app.font_fallbacks, [])
        self.assertEqual(self.app.font_fallback_count, 0)
        self.assertEqual(self.app.font_fallback(0), "")

        self.app.set_font_fallbacks(["Noto Sans CJK SC", "DejaVu Sans"])
        self.assertEqual(self.app.font_fallbacks,
                         ["Noto Sans CJK SC", "DejaVu Sans"])
        self.assertEqual(self.app.font_fallback(1), "DejaVu Sans")
        self.assertEqual(self.app.font_fallback(2), "")
        self.assertEqual(self.app.font_fallback(-1), "")

        self.app.font_fallbacks = ["", ""]
        self.assertEqual(self.app.font_fallbacks, [])
        self.app.font_fallbacks = [f"f{index}" for index in range(10)]
        self.assertEqual(self.app.font_fallback_count, pt.font_fallback_max_families)
        self.assertEqual(self.app.font_fallback(7), "f7")
        self.assertEqual(self.app.font_fallback(8), "")

        self.app.set_font_fallbacks(["DejaVu Sans"])
        family = "pt-python-fallback"
        self.assertEqual(self.app.font_fallback_count_for_family(family), 0)
        self.app.set_font_fallbacks_for_family(
            family, ["DejaVu Serif", "DejaVu Sans"]
        )
        self.assertEqual(self.app.font_fallback_count_for_family(family), 2)
        self.assertEqual(self.app.font_fallback_for_family(family, 0),
                         "DejaVu Serif")
        self.assertEqual(self.app.font_fallback_for_family(family, 2), "")
        self.app.set_font_fallbacks_for_family(family, [])
        self.assertEqual(self.app.font_fallback_count_for_family(family), 0)
        self.assertEqual(self.app.font_fallback_count, 1)

        # U+0378 is unassigned; it has no covering face in any chain.
        self.assertEqual(self.app.font_uncovered_count("DejaVu Sans", "\u0378"), 1)
        self.assertEqual(self.app.font_uncovered_count(None, "\u0378"), 1)
        self.assertEqual(self.app.font_uncovered_count(None, ""), 0)
        self.app.set_font_fallbacks([])
        self.assertEqual(self.app.font_fallbacks, [])

    # ------------------------------------------------------------------
    # Clipboard media
    # ------------------------------------------------------------------

    def test_12_clipboard_image_roundtrip(self) -> None:
        pixels = [0xFFFF0000, 0x80004000, 0x00000000, 0xFF00FF00]
        self.assertTrue(self.window.set_clipboard_image(pixels, 2, 2))
        image = self.window.clipboard_image
        self.assertIsNotNone(image)
        if image is None:
            return
        raw, width, height = image
        self.assertEqual((width, height), (2, 2))
        self.assertEqual(len(raw), 16)
        self.assertEqual(
            [int.from_bytes(raw[index:index + 4], "little")
             for index in range(0, 16, 4)],
            [0xFFFF0000, 0x80004000, 0x00000000, 0xFF00FF00],
        )
        # An image selection replaces the text one.
        self.assertEqual(self.window.clipboard, "")
        # The bytes feed straight back into the setter.
        self.assertTrue(self.window.set_clipboard_image(raw, width, height))
        self.assertIsNotNone(self.window.clipboard_image)

        # A list and a bytearray are both accepted by the buffer path.
        self.assertTrue(self.window.set_clipboard_image(bytearray(raw), 2, 2))
        with self.assertRaises(ValueError):
            self.window.set_clipboard_image(raw[:8], 2, 2)
        with self.assertRaises(ValueError):
            self.window.set_clipboard_image([], 0, 1)
        # The C-side dimension cap rejects without raising.
        self.assertFalse(
            self.window.set_clipboard_image([0] * 8193, 8193, 1)
        )

    def test_12_clipboard_uri_list(self) -> None:
        self.assertTrue(
            self.window.set_clipboard_uri_list(["/tmp/a b", "file:///tmp/x"])
        )
        self.assertEqual(self.window.clipboard, "")
        self.assertIsNone(self.window.clipboard_image)
        self.assertFalse(self.window.set_clipboard_uri_list([]))
        self.assertFalse(self.window.set_clipboard_uri_list(["relative/path"]))
        self.assertFalse(self.window.set_clipboard_uri_list(["/tmp/a\nb"]))
        self.assertTrue(self.window.clipboard == "")

    # ------------------------------------------------------------------
    # Column identity and persistence
    # ------------------------------------------------------------------

    def test_12_column_identity_and_persistence(self) -> None:
        table = pt.table(self.column)
        table.column_count = 3
        table.set_column_title(0, "Name").set_column_title(1, "Age")
        table.set_column_title(2, "City")
        table.set_column_width(0, 100.0)
        table.set_column_width(1, 80.0)
        table.set_column_width(2, 60.0)
        table.set_column_key(0, "name")
        table.set_column_key(1, "age")
        table.set_column_key(2, "city")
        table.set_column_hidden(1, True)
        table.set_sort(2, pt.sort_descending)

        self.assertEqual([table.column_key(index) for index in range(3)],
                         ["name", "age", "city"])
        self.assertEqual(table.column_key(99), "")

        blob = table.columns_save()
        self.assertTrue(blob.startswith("PTCOLS 1\nC 3\n"))
        self.assertIn("6e616d65 100000 0", blob)
        self.assertIn("616765 80000 1", blob)
        self.assertTrue(blob.endswith("S 1\n2 2\n"))

        # Reorder and mutate, then restore the saved layout.
        self.assertTrue(table.move_column(0, 2))
        self.assertEqual(table.column_key(2), "name")
        table.set_column_width(2, 42.0)
        table.set_column_hidden(0, False)
        self.assertTrue(table.columns_restore(blob))
        self.assertEqual([table.column_key(index) for index in range(3)],
                         ["name", "age", "city"])
        self.assertAlmostEqual(table.column_width(0), 100.0, places=2)
        self.assertAlmostEqual(table.column_width(1), 80.0, places=2)
        self.assertAlmostEqual(table.column_width(2), 60.0, places=2)
        self.assertTrue(table.column_hidden(1))
        self.assertFalse(table.column_hidden(0))
        self.assertEqual(table.sort, (pt.sort_descending, 2))

        # Duplicate keys are rejected; the existing key stays in place.
        table.set_column_key(1, "name")
        self.assertEqual(table.column_key(1), "age")

        # Invalid blobs are atomic: the layout is untouched.
        before = (table.column_key(0), table.column_width(0),
                  table.sort, table.column_hidden(0))
        self.assertFalse(table.columns_restore("garbage"))
        self.assertFalse(table.columns_restore("PTCOLS 9\nC 0\n"))
        self.assertEqual(
            (table.column_key(0), table.column_width(0),
             table.sort, table.column_hidden(0)),
            before,
        )

        # Empty keys serialize as "-" and restore by display position.
        table.set_column_key(2, "")
        empty_blob = table.columns_save()
        self.assertIn("- 60000 0", empty_blob)
        self.assertTrue(table.columns_restore(empty_blob))
        self.assertEqual(table.column_key(2), "")

        # A non-table widget has no persistence.
        self.assertEqual(pt.label(self.column, "x").columns_save(), "")
        self.assertFalse(pt.label(self.column, "x").columns_restore(blob))

    # ------------------------------------------------------------------
    # Raster icons
    # ------------------------------------------------------------------

    def test_12_icon_images_and_slots(self) -> None:
        png = _make_png(4, 4)
        image = pt.IconImage.load_memory(png)
        self.assertIsNotNone(image)
        if image is None:
            return
        self.assertEqual((image.width, image.height), (4, 4))
        self.assertEqual(pt.IconImage.load_memory(bytearray(png)).width, 4)
        self.assertEqual(pt.IconImage.load_memory(memoryview(png)).width, 4)
        self.assertIsNone(pt.IconImage.load_memory(b"not a png"))
        self.assertIsNone(pt.IconImage.load_memory(png[:20]))

        reference = image.ref()
        self.assertEqual(reference.raw, image.raw)
        reference.close()
        self.assertEqual(image.width, 4)
        self.assertRaises(RuntimeError, reference.ref)
        image.close()
        image.close()
        self.assertEqual(image.raw, 0)
        self.assertEqual(image.width, 0)
        self.assertRaises(RuntimeError, image.ref)

        button = pt.button(self.column, "Icon")
        icon = pt.IconImage.load_memory(png)
        self.assertIsNotNone(icon)
        if icon is None:
            return
        button.set_icon_image(icon)
        slot = button.icon_image
        self.assertIsNotNone(slot)
        if slot is None:
            return
        self.assertEqual(slot.raw, icon.raw)
        self.assertEqual(slot.width, 4)
        # The getter owns a reference: dropping it keeps the slot alive.
        slot.destroy()
        self.assertEqual(button.icon_image.width, 4)
        button.set_icon_tint(0xFF00FFFF)
        button.icon_image = None
        self.assertIsNone(button.icon_image)
        icon.destroy()
        self.assertRaises(RuntimeError, button.set_icon_image, icon)

        window_icon = pt.IconImage.load_memory(png)
        self.assertIsNotNone(window_icon)
        if window_icon is not None:
            self.window.set_icon_image(window_icon)
            self.assertIsNotNone(self.window.icon_image)
            if self.window.icon_image is not None:
                self.assertEqual(self.window.icon_image.width, 4)
            self.window.set_icon_image(None)
            self.assertIsNone(self.window.icon_image)
            window_icon.destroy()
        self.window.icon_image = None

    def test_12_icon_file_theme_lookup_and_finalizer(self) -> None:
        png = _make_png(8, 8)
        directory = tempfile.mkdtemp(prefix="pt-python-icon-")
        previous = os.environ.get("XDG_DATA_HOME")
        try:
            path = os.path.join(directory, "roundtrip.png")
            with open(path, "wb") as handle:
                handle.write(png)
            loaded = pt.IconImage.load_file(path)
            self.assertIsNotNone(loaded)
            if loaded is not None:
                self.assertEqual((loaded.width, loaded.height), (8, 8))
                loaded.close()
            self.assertIsNone(pt.IconImage.load_file(path + ".missing"))
            self.assertIsNone(pt.IconImage.load_file(directory))

            apps = os.path.join(directory, "icons", "hicolor", "32x32", "apps")
            os.makedirs(apps)
            with open(os.path.join(apps, "pt-python-icon.png"), "wb") as handle:
                handle.write(png)
            with open(os.path.join(directory, "icons", "hicolor", "index.theme"),
                      "w", encoding="utf-8") as handle:
                handle.write("[Icon Theme]\nName=Pt Test\n"
                             "Directories=32x32/apps\n\n"
                             "[32x32/apps]\nSize=32\nType=Fixed\nContext=Apps\n")
            os.environ["XDG_DATA_HOME"] = directory
            themed = pt.IconImage.from_name("pt-python-icon", 32)
            self.assertIsNotNone(themed)
            if themed is not None:
                self.assertEqual(themed.width, 8)
                themed.close()
            self.assertIsNone(pt.IconImage.from_name("pt-python-missing", 32))
            self.assertIsNone(pt.IconImage.from_name("../escape", 32))
            self.assertIsNone(pt.IconImage.from_name("", 32))
        finally:
            if previous is None:
                os.environ.pop("XDG_DATA_HOME", None)
            else:
                os.environ["XDG_DATA_HOME"] = previous
            shutil.rmtree(directory, ignore_errors=True)

        # __del__ drops the reference that was never released explicitly.
        destroyed: list[int] = []
        original = pt._pt_icon_image_destroy

        def spy(pointer: int) -> None:
            destroyed.append(int(pointer))
            original(pointer)

        pt._pt_icon_image_destroy = spy
        try:
            temporary = pt.IconImage.load_memory(png)
            self.assertIsNotNone(temporary)
            pointer = temporary.raw if temporary is not None else 0
            del temporary
            gc.collect()
        finally:
            pt._pt_icon_image_destroy = original
        self.assertIn(pointer, destroyed)



if __name__ == "__main__":
    unittest.main()
