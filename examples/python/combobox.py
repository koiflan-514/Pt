#!/usr/bin/env python3
"""Pt combobox and multi-row selection example - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import os

import pt

PLACES = (
    ("Europe/Berlin", "UTC+1", 148),
    ("Europe/Lisbon", "UTC+0", 63),
    ("Asia/Tokyo", "UTC+9", 211),
    ("Asia/Kolkata", "UTC+5:30", 174),
    ("America/Denver", "UTC-7", 92),
    ("Pacific/Auckland", "UTC+13", 41),
)

FONTS = (
    "JetBrains Mono",
    "DejaVu Sans",
    "Noto Sans CJK",
    "Inter",
    "Source Serif",
    "IBM Plex Mono",
    "Cascadia Code",
    "Roboto Flex",
    "Terminus",
)

FILES = (
    "api/pt/pt.h",
    "src/combobox.c",
    "src/tree.c",
    "src/table.c",
    "src/widget.c",
    "bindings/cpp/pt",
    "bindings/python",
    "bindings/java",
    "tests/test_a11y.c",
    "docs/bindings.md",
)


def main() -> int:
    app = pt.App("pt.example.combobox.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x4C8DFF)

    window = pt.Window(app, "Pt Combobox - Python", 880, 600)
    root = window.root
    root.set_padding(26, 26, 26, 26).set_gap(14)

    pt.label(root, "COMBOBOX  ·  SELECTION  ·  0.10").set_font_size(11).set_font_weight(
        pt.font_weight_semibold
    ).set_foreground(pt.rgb(0x4C8DFF))
    pt.label(root, "One field, a list of choices").set_font_size(30).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        root,
        "A combobox is a closed pick list built on the same overlay primitives as the rest of "
        "the toolkit. Filterable mode puts a real editor in the field and reports every "
        "keystroke. Selection in the data views is a set, so ranges and toggles behave the "
        "same in a list, a table and a tree.",
    ).set_wrap(True).set_foreground(pt.rgb(0x9AA4B2))

    status = pt.label(root, "Ready").set_font_size(12).set_foreground(pt.rgb(0x8B98AA))
    status.a11y_name = "Status"

    def report(text: str) -> None:
        status.text = text

    boxes = pt.row(root).set_gap(14)

    region_card = pt.card(boxes).set_grow(1).set_padding(16, 16, 16, 16).set_gap(10)
    pt.label(region_card, "Select only").set_font_weight(pt.font_weight_semibold)
    region_box = pt.combobox(region_card).set_min_width(220)
    region_box.combo_placeholder = "Choose a region"
    region_box.a11y_name = "Region"
    region_box.a11y_description = (
        "Closed list of regions; rows carry the model row as borrowed item data."
    )
    region_box.popup_max_height = 190
    detail = pt.label(region_card, "Pick a region to see its payload.").set_font_size(
        12
    ).set_wrap(True).set_foreground(pt.rgb(0x8B98AA))

    font_card = pt.card(boxes).set_grow(1).set_padding(16, 16, 16, 16).set_gap(10)
    pt.label(font_card, "Filterable").set_font_weight(pt.font_weight_semibold)
    font_box = pt.combobox(font_card).set_min_width(220)
    font_box.combo_mode = pt.combobox_filterable
    font_box.combo_placeholder = "Type to filter fonts"
    font_box.a11y_name = "Font family"

    list_card = pt.card(root).set_grow(1).set_padding(16, 16, 16, 16).set_gap(10)
    rows = pt.list_view(list_card).set_grow(1).set_min_height(190)
    rows.a11y_name = "Files"

    typed = {"count": 0}

    def fill_regions() -> None:
        region_box.clear_items()
        for index, place in enumerate(PLACES):
            region_box.add_item(place[0]).set_item_data(index, place)
        # Disabled rows stay visible but cannot be highlighted or chosen.
        region_box.set_item_disabled(1, True)

    def on_region(_box: pt.Widget, index: int) -> None:
        place = region_box.item_data(index)
        if place is None:
            detail.text = "That row carries no payload."
            report("Chose an item without data")
            return
        name, zone, stations = place
        detail.text = f"{name} · {zone} · {stations} stations"
        report(f"Selected {name}")
        window.show_toast(f"Selected {name}", pt.toast_info, 2000)

    def describe(prefix: str) -> None:
        held = rows.list_selection()
        indexes = ", ".join(str(row) for row in held) or "none"
        primary = "cleared" if rows.selected_row == pt.list_index_none else "set"
        report(f"{prefix} · {len(held)} of {len(FILES)} rows [{indexes}] · primary {primary}")

    def bind_row(row: pt.Widget, index: int) -> None:
        row.text = FILES[index] if index < len(FILES) else ""
        row.set_icon(pt.icon_document)

    def on_font_text(_box: pt.Widget, text: str) -> None:
        typed["count"] += 1
        report(f'Typed "{text}" · keystroke {typed["count"]}')

    def toggle_popup() -> None:
        if region_box.popup_open:
            region_box.close_popup()
            report("Popup closed")
            return
        region_box.open_popup()
        report(f"Popup open · max height {region_box.popup_max_height:.0f} px")

    def select_by_code() -> None:
        # Programmatic selection is silent, so the status line is written here.
        region_box.combo_selected = 4
        on_region(region_box, 4)
        detail.text = "Set from code: no on_change fired."

    def reload_items() -> None:
        fill_regions()
        report(f"Reloaded {region_box.item_count} regions")

    def switch_mode() -> None:
        font_box.combo_mode = (
            pt.combobox_select_only
            if font_box.combo_mode == pt.combobox_filterable
            else pt.combobox_filterable
        )
        report(
            "Font box is now select only"
            if font_box.combo_mode == pt.combobox_select_only
            else "Font box is now filterable"
        )

    def remove_first_font() -> None:
        if font_box.item_count == 0:
            report("No items left in the font list")
            return
        first = font_box.item_text(0)
        font_box.remove_item(0)
        report(f'Removed "{first}" · {font_box.item_count} left')

    def select_range() -> None:
        rows.list_select_range(2, 4)
        describe("Range 2-4")

    def toggle_row() -> None:
        row = 3 if rows.is_list_row_selected(6) else 6
        rows.list_select_toggle(row)
        describe(f"Toggled row {row}")

    def clear_rows() -> None:
        rows.clear_list_selection()
        describe("Selection cleared")

    fill_regions()
    region_box.on_combo_change(on_region)

    for name in FONTS:
        font_box.add_item(name)
    font_box.on_combo_change(lambda _box, index: report(f"Font chosen: {font_box.combo_text}"))
    font_box.on_combo_text_change(on_font_text)

    rows.set_row_height(34).set_row_count(len(FILES))
    rows.on_bind_row(bind_row).on_row_select(lambda index: describe(f"Pointer chose row {index}"))

    region_tools = pt.wrap(region_card).set_wrap_gap(8, 8)
    pt.button(region_tools, "Toggle popup").set_button_variant(pt.button_secondary).on_click(
        toggle_popup
    )
    pt.button(region_tools, "Select by code").set_button_variant(pt.button_ghost).on_click(
        select_by_code
    )
    pt.button(region_tools, "Reload items").set_button_variant(pt.button_ghost).on_click(
        reload_items
    )

    font_tools = pt.wrap(font_card).set_wrap_gap(8, 8)
    pt.button(font_tools, "Switch mode").set_button_variant(pt.button_secondary).on_click(
        switch_mode
    )
    pt.button(font_tools, "Remove first item").set_button_variant(pt.button_ghost).on_click(
        remove_first_font
    )

    row_tools = pt.wrap(list_card).set_wrap_gap(8, 8)
    pt.button(row_tools, "Range 2-4").set_button_variant(pt.button_primary).on_click(select_range)
    pt.button(row_tools, "Toggle 6").set_button_variant(pt.button_secondary).on_click(toggle_row)
    pt.button(row_tools, "Clear").set_button_variant(pt.button_ghost).on_click(clear_rows)

    pt.label(
        root,
        "The popup flips above the field near the bottom of the window. Marquee-drag or "
        "Shift-click a row to extend the selection set.",
    ).set_font_size(12).set_wrap(True).set_foreground(pt.rgb(0x6F7C8F))

    window.show()
    window.focus(region_box)

    if os.environ.get("PT_HEADLESS", "") not in ("", "0"):
        switch_mode()
        toggle_popup()
        on_region(region_box, 0)
        select_by_code()
        select_range()
        toggle_row()
        toggle_row()
        clear_rows()
        report(
            f"role {region_box.a11y_role} · expandable "
            f"{bool(region_box.a11y_state & pt.a11y_state_expandable)}"
        )
        return app.run_for(250)
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main())
