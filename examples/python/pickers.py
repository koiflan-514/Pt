#!/usr/bin/env python3
"""Pt pickers - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.


A searchable select, a date picker, a number picker and a colour picker in one
window. Programmatic setters are silent; the on_change callbacks write a log
line into the status label.
"""

from __future__ import annotations

import os
import sys

import pt


def main() -> int:
    app = pt.App("pt.example.pickers.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x4C8DFF)

    window = pt.Window(app, "Pt Pickers - Python", 760, 760)
    root = window.root
    root.set_padding(26, 26, 26, 26).set_gap(14)

    pt.label(root, "PICKERS  ·  1.2  ·  PYTHON").set_font_size(11).set_font_weight(
        pt.font_weight_semibold
    ).set_foreground(pt.rgb(0x4C8DFF))
    pt.label(root, "Four fields, one focus chain").set_font_size(28).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        root,
        "Every control is reachable with Tab. F4 or Alt+Down opens a popup; Down/Up "
        "move the highlight, Enter commits, Escape restores the committed value.",
    ).set_font_size(13).set_foreground(pt.rgb(0x9AA7B8)).set_wrap(True)

    card = pt.card(root)
    card.set_grow(1).set_padding(18, 18, 18, 18).set_gap(14)

    state = {"changes": 0, "status": None}

    def log_line(line: str) -> None:
        state["changes"] += 1
        text = f"#{state['changes']} · {line}"
        state["status"].text = text
        print(f"pickers-python: {text}", flush=True)

    def field_block(label: str) -> pt.Widget:
        column = pt.column(card)
        column.set_gap(4)
        pt.label(column, label).set_font_size(12).set_font_weight(
            pt.font_weight_medium
        ).set_foreground(pt.rgb(0xAAB6C6))
        return column

    def field_note(column: pt.Widget, hint: str) -> None:
        pt.label(column, hint).set_font_size(11).set_foreground(pt.rgb(0x6F7C8F)).set_wrap(
            True
        )

    select_block = field_block("Searchable select")
    select = pt.select(select_block)
    select.set_grow(1).set_min_width(260)
    select.a11y_name = "City"
    select.select_placeholder = "Type to search a city"
    select.select_max_popup_rows = 6
    for city in (
        "Amsterdam", "Barcelona", "Berlin", "Bordeaux", "Bristol", "Brussels",
        "Copenhagen", "Dublin", "Edinburgh", "Lisbon", "London", "Madrid",
        "Milan", "Munich", "Oslo", "Paris", "Porto", "Prague",
        "Rome", "Stockholm", "Vienna", "Warsaw", "Zurich",
    ):
        select.select_add_item(city)

    def on_select(_widget: pt.Widget, index: int) -> None:
        if index == pt.list_index_none:
            log_line("select cleared")
        else:
            log_line(f'select committed item {index} "{select.select_item_text(index)}"')

    select.on_select_change(on_select)
    field_note(
        select_block,
        "Substring filter is ASCII-case-insensitive. Delete with an empty query clears "
        "the value.",
    )

    date_block = field_block("Date picker")
    date = pt.date_picker(date_block)
    date.set_grow(1)
    date.a11y_name = "Start date"
    date.date_picker_placeholder = "Pick a day"
    date.date_picker_format = pt.date_format_dmy
    date.set_date_picker_date(pt.date_today())

    def on_date(_widget: pt.Widget, value: pt.Date) -> None:
        if value.year == 0:
            log_line("date cleared")
        else:
            log_line(f"date committed {value.year:04d}-{value.month:02d}-{value.day:02d}")

    date.on_date_change(on_date)
    field_note(
        date_block,
        "Six fixed calendar weeks. Left/Right move a day, Up/Down a week, Page Up/Down "
        "a month, T selects today.",
    )

    number_block = field_block("Number picker")
    number = pt.number_picker(number_block)
    number.set_grow(1)
    number.a11y_name = "Duration in minutes"
    number.number_picker_placeholder = "Minutes"
    number.set_number_picker_range(0, 240)
    number.set_number_picker_step(5)
    number.set_number_picker_value(45)

    def on_number(_widget: pt.Widget, value: float) -> None:
        log_line(f"number committed {value:.3f}")

    number.on_number_change(on_number)
    field_note(
        number_block,
        "No popup. Up/Down step by 5, Page Up/Down by 50, Home/End jump to the range "
        "edges; typed text clamps.",
    )

    color_block = field_block("Colour picker")
    color = pt.color_picker(color_block)
    color.set_grow(1)
    color.a11y_name = "Accent colour"
    color.color_picker_color = pt.rgb(0x4C8DFF)
    color.color_picker_add_preset(pt.rgb(0x9ECE6A))
    color.color_picker_add_preset(pt.rgb(0xF7768E))

    def on_color(_widget: pt.Widget, value: int) -> None:
        log_line(f"colour committed {color.color_picker_hex} (0x{value:08X})")

    color.on_color_change(on_color)
    field_note(
        color_block,
        "Accepts #RRGGBB and #RRGGBBAA. Tab moves through the palette; Enter commits "
        "the highlighted preset.",
    )

    tools = pt.row(card)
    tools.set_gap(8).set_align_self(pt.align_center)

    def open_select() -> None:
        select.select_open()
        log_line(
            f"select popup open={'yes' if select.select_popup_open else 'no'} "
            f"matches={select.select_match_count}"
        )

    def open_calendar() -> None:
        date.date_picker_open()
        log_line(
            f"calendar open={'yes' if date.date_picker_popup_open else 'no'} "
            f'text="{date.date_picker_text}"'
        )

    def open_palette() -> None:
        color.color_picker_open()
        log_line(
            f"palette open={'yes' if color.color_picker_popup_open else 'no'} "
            f"presets={color.color_picker_preset_count}"
        )

    def close_all() -> None:
        select.select_close()
        date.date_picker_close()
        color.color_picker_close()
        log_line("popups closed; focus returns to the entry")
        select.focus()

    def clear_values() -> None:
        select.select_clear_selection()
        date.date_picker_clear()
        number.number_picker_clear()
        color.color_picker_color = pt.rgb(0x000000)
        log_line("values cleared programmatically (silent)")

    pt.button(tools, "Open select").set_button_variant(pt.button_secondary).on_click(
        open_select
    )
    pt.button(tools, "Open calendar").set_button_variant(pt.button_secondary).on_click(
        open_calendar
    )
    pt.button(tools, "Open palette").set_button_variant(pt.button_secondary).on_click(
        open_palette
    )
    pt.button(tools, "Close popups").set_button_variant(pt.button_ghost).on_click(
        close_all
    )
    pt.button(tools, "Clear values").set_button_variant(pt.button_ghost).on_click(
        clear_values
    )

    state["status"] = (
        pt.label(root, "Ready · commit a value to see the change log")
        .set_font_size(12)
        .set_foreground(pt.rgb(0x9ECE6A))
        .set_wrap(True)
    )
    state["status"].a11y_name = "Change log"

    pt.label(
        root,
        "Callbacks run after the control state has updated. Programmatic setters are "
        "silent, so a callback may destroy its picker; this example only reads it.",
    ).set_font_size(12).set_foreground(pt.rgb(0x6F7C8F)).set_wrap(True)

    pt.button(root, "Quit").set_button_variant(pt.button_ghost).set_align_self(
        pt.align_start
    ).on_click(window.close)

    window.show()
    select.focus()

    if os.environ.get("PT_HEADLESS") not in (None, "", "0"):
        print(
            f"pickers-python: select items={select.select_item_count} "
            f"max_rows={select.select_max_popup_rows} "
            f'placeholder="{select.select_placeholder}"'
        )
        select.select_query = "ber"
        print(f'pickers-python: query "ber" matches={select.select_match_count}')
        select.select_clear_selection()
        select.select_query = ""
        select.select_open()
        select.select_selected = 2
        on_select(select, select.select_selected)
        select.select_close()

        value = date.date_picker_date
        print(
            f"pickers-python: date {value.year:04d}-{value.month:02d}-{value.day:02d} "
            f"valid={pt.date_is_valid(value)} format={date.date_picker_format}"
        )
        date.set_date_picker_date(pt.Date(2027, 1, 15))
        on_date(date, date.date_picker_date)

        number.number_picker_step_by(2)
        print(
            f"pickers-python: number {number.number_picker_value:.1f} "
            f"has_value={number.number_picker_has_value} "
            f'text="{number.number_picker_text}"'
        )
        on_number(number, number.number_picker_value)

        color.set_color_picker_hex("#ff8800")
        print(
            f"pickers-python: colour hex={color.color_picker_hex} "
            f"presets={color.color_picker_preset_count}"
        )
        on_color(color, color.color_picker_color)
        open_palette()
        close_all()
        return app.run_for(250)
    return app.run()


if __name__ == "__main__":
    sys.exit(main())