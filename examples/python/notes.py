#!/usr/bin/env python3
"""Pt notes example - virtualized list and multiline editor in Python.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import pt

TOPICS = [
    ("Design tokens", "Theme tokens keep colour, spacing and typography consistent."),
    ("Responsive layout", "Wrap and grid reflow children as the window resizes."),
    ("Motion", "Easing curves and transitions animate custom widgets."),
    ("Virtualized lists", "Only visible rows are materialised and rebound."),
    ("Multiline editing", "UTF-8 offsets, wrapping, undo and IME preedit."),
    ("Accessibility", "Roles, names, states and events feed the AT-SPI bridge."),
    ("Overlays", "Native popup menus, modal dialogs and stacking toasts."),
    ("Performance", "Damage tracking repaints only the changed regions."),
]

NOTE_COUNT = 128


def note_title(index: int) -> str:
    return f"{TOPICS[index % len(TOPICS)][0]} · {index + 1:02d}"


def note_body(index: int) -> str:
    topic, summary = TOPICS[index % len(TOPICS)]
    return (
        f"{topic}\n\n{summary}\n\nRow {index + 1} of {NOTE_COUNT} is materialised "
        "only while it is visible. The list keeps a small row pool and rebinds it "
        "as you scroll."
    )


def main() -> int:
    app = pt.App("pt.example.notes.python")
    app.color_scheme = pt.color_scheme_dark

    window = pt.Window(app, "Pt Notes - Python", 960, 660)
    root = window.root.set_gap(14).set_padding(22, 22, 22, 22)

    header = pt.row(root).set_gap(14)
    headings = pt.column(header).set_gap(2).set_grow(1)
    pt.label(headings, "Notes").set_font_size(26).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        headings, "A virtualized list, a multiline editor and responsive chips."
    ).set_font_size(13).set_foreground(pt.rgb(0x9AA7B8))

    motion = pt.toggle(header).set_align_self(pt.align_center)
    motion.checked = app.reduced_motion
    pt.label(header, "Reduce motion").set_align_self(pt.align_center)

    main_row = pt.row(root).set_gap(14).set_grow(1)

    sidebar = pt.card(main_row).set_width(300).set_padding(14, 10, 14, 10).set_gap(10)
    pt.label(sidebar, "All notes").set_font_size(16).set_font_weight(
        pt.font_weight_semibold
    )
    notes = (
        pt.list_view(sidebar)
        .set_grow(1)
        .set_row_height(46)
        .set_align_self(pt.align_stretch)
    )
    notes.row_count = NOTE_COUNT
    notes.a11y_name = "Notes"

    workspace = pt.card(main_row).set_grow(1).set_padding(18, 18, 18, 18).set_gap(12)
    workspace_head = pt.row(workspace).set_gap(10)
    title = pt.label(workspace_head, note_title(0)).set_font_size(18).set_font_weight(
        pt.font_weight_semibold
    ).set_grow(1)
    status = pt.label(workspace_head, "Saved locally").set_font_size(12).set_align_self(
        pt.align_center
    ).set_foreground(pt.rgb(0x9AA7B8))

    editor = (
        pt.text_area(workspace, "Select a note to start writing…")
        .set_grow(1)
        .set_align_self(pt.align_stretch)
    )
    editor.a11y_name = "Note body"

    tags = pt.wrap(workspace).set_wrap_gap(6, 6)
    for tag in ("design", "layout", "motion", "lists", "text", "a11y"):
        pt.label(tags, tag).set_padding(4, 10, 4, 10).set_font_size(12).set_radius(
            999
        ).set_align_self(pt.align_start).set_background(
            app.get_theme_color(pt.theme_surface_sunken)
        )

    stats = pt.grid(workspace, 3).set_grid_gap(8, 8)
    counters: dict[str, pt.Widget] = {}
    for caption in ("words", "characters", "lines"):
        card = pt.card(stats).set_padding(10, 12, 10, 12).set_gap(2)
        counters[caption] = pt.label(card, "0").set_font_size(20).set_font_weight(
            pt.font_weight_semibold
        )
        pt.label(card, caption).set_font_size(12).set_foreground(pt.rgb(0x9AA7B8))

    def render_stats() -> None:
        text = editor.text
        counters["words"].text = str(len(text.split()))
        counters["characters"].text = str(len(text))
        counters["lines"].text = str(text.count("\n") + 1)

    def select_note(index: int) -> None:
        title.text = note_title(index)
        editor.text = note_body(index)
        status.text = "Saved locally · edits are in memory"
        render_stats()

    def bind_row(row: pt.Widget, index: int) -> None:
        row.text = note_title(index)

    def on_edit(_text: str) -> None:
        status.text = "Edited · not yet synced"
        render_stats()

    def on_select(index: int) -> None:
        select_note(index)

    def on_motion(checked: bool) -> None:
        app.reduced_motion = checked
        status.text = (
            "Reduced motion enabled" if checked else "Reduced motion disabled"
        )

    notes.on_bind_row(bind_row)
    notes.on_row_select(on_select)
    notes.on_row_activate(
        lambda index: setattr(status, "text", f"Opened note {index + 1}")
    )
    editor.on_change(on_edit)
    motion.on_checked(on_motion)

    select_note(0)
    notes.selected_row = 0

    window.show()
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
