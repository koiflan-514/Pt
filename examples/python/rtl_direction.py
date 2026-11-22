#!/usr/bin/env python3
"""Pt direction switch - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

The eight 2.0 direction functions are exposed as properties plus fluent
``set_*`` methods. ``pt_ui_direction_inherit`` and ``pt_text_direction_auto``
are the zero values, so an unset widget inherits and existing code keeps its
1.x behavior.
"""

from __future__ import annotations

import os

import pt

HEBREW_MIXED = "שלום مرحبا Hello, 123"
RTL_ENTRY = "עבודה work@example.com"


def main() -> int:
    app = pt.App("pt.example.rtl_direction.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)

    window = pt.Window(app, "Pt Direction - Python", 720, 560)
    root = window.root

    page = pt.column(root).set_padding(26, 26, 26, 26).set_gap(12)

    pt.label(page, "PYTHON BINDING  ·  DIRECTION").set_font_size(11).set_font_weight(
        pt.font_weight_semibold
    ).set_foreground(pt.rgb(0x7AA2F7))

    title = (
        pt.label(page, HEBREW_MIXED)
        .set_font_size(30)
        .set_font_weight(pt.font_weight_bold)
        .set_text_direction(pt.text_direction_auto)
    )
    pt.label(
        page,
        "pt.ui_direction mirrors rows and chrome; pt.text_direction selects the paragraph "
        "base. auto runs UBA P2/P3, so the heading resolves RTL while the ASCII suffix stays "
        "LTR.",
    ).set_font_size(13).set_wrap(True).set_foreground(pt.rgb(0x9AA7B8))

    row = pt.row(page).set_gap(10).set_ui_direction(pt.ui_direction_rtl)
    row.set_align(pt.align_center)
    status = (
        pt.label(row, "RTL shell")
        .set_grow(1)
        .set_font_size(12)
        .set_foreground(pt.rgb(0x9ECE6A))
    )

    entry = (
        pt.entry(page, "Type here")
        .set_size(420, 36)
        .set_ui_direction(pt.ui_direction_rtl)
        .set_text_direction(pt.text_direction_auto)
        .set_align_self(pt.align_start)
    )
    entry.text = RTL_ENTRY

    area = (
        pt.text_area(page, "Type a note")
        .set_size(560, 130)
        .set_ui_direction(pt.ui_direction_rtl)
        .set_text_direction(pt.text_direction_auto)
    )
    area.text = "שלום עולם - Hello world\nHello world - שלום עולם\nمرحبا بالعالم 42"

    nested = pt.card(page).set_padding(14, 14, 14, 14).set_gap(8)
    nested.set_ui_direction(pt.ui_direction_ltr)
    nested.set_text_direction(pt.text_direction_ltr)
    pt.label(nested, "Nested LTR island inside the RTL shell.").set_font_size(
        13
    ).set_wrap(True).set_foreground(pt.rgb(0xBB9AF7))
    nested_entry = pt.entry(nested, "code = 1;").set_size(340, 34).set_align_self(
        pt.align_start
    )
    nested_entry.text = "int main(void) { return 0; }"
    nested_entry.set_text_direction(pt.text_direction_ltr)

    def apply(direction: int) -> None:
        app.ui_direction = direction
        window.ui_direction = direction
        row.ui_direction = direction
        entry.ui_direction = direction
        title.text_direction = (
            pt.text_direction_rtl if direction == pt.ui_direction_rtl else pt.text_direction_ltr
        )
        nested.ui_direction = pt.ui_direction_ltr
        nested.text_direction = pt.text_direction_ltr
        status.text = "RTL shell" if direction == pt.ui_direction_rtl else "LTR shell"
        print(
            "rtl_direction: "
            f"app={app.ui_direction} window={window.ui_direction} row={row.ui_direction} "
            f"entry={entry.ui_direction} title_text={title.text_direction} "
            f"nested={nested.ui_direction}",
            flush=True,
        )

    pt.button(row, "Force LTR").set_button_variant(pt.button_ghost).on_click(
        lambda: apply(pt.ui_direction_ltr)
    )
    pt.button(row, "Force RTL").set_button_variant(pt.button_primary).on_click(
        lambda: apply(pt.ui_direction_rtl)
    )

    pt.spacer(page)
    pt.button(page, "Quit").set_button_variant(pt.button_ghost).set_align_self(
        pt.align_start
    ).on_click(window.close)

    apply(pt.ui_direction_rtl)
    window.show()

    if os.environ.get("PT_HEADLESS", "") not in ("", "0"):
        apply(pt.ui_direction_ltr)
        apply(pt.ui_direction_rtl)
        return app.run_for(250)
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())