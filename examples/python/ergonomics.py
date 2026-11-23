#!/usr/bin/env python3
"""Pt ergonomics notebook - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

The 2.1 surface in the binding:

- ``caret_movement`` is a property plus ``set_caret_movement`` on ``App``,
  ``Window`` and ``Widget``; ``caret_movement_inherit`` (0) resolves to visual,
  so nothing changes until a policy is set.
- ``locale`` is a property on ``App`` and ``locale_ui_direction`` is a free
  helper; the hint only supplies the application fallback and an explicit
  ``ui_direction`` still wins.
- ``set_padding_logical(top, start, bottom, end)`` resolves start/end from the
  widget's UI direction on every layout; ``set_padding`` stays physical.
- ``bidi_control_inventory`` returns a list of spans and ``bidi_sanitize``
  returns a new ``str``.
"""

from __future__ import annotations

import os

import pt

UNTRUSTED = "report\u202efdp.exe\u2066https://example.com\u2069"
HEBREW_MIXED = "שלום مرحبا Hello, 123"


def report_safety() -> None:
    spans = pt.bidi_control_inventory(UNTRUSTED)
    first = spans[0] if spans else None
    print(
        f"ergonomics: inventory={len(spans)} "
        f"first=({first.offset},{first.length},kind={first.kind})"
        if first
        else "ergonomics: inventory=0",
        flush=True,
    )
    stripped = pt.bidi_sanitize(UNTRUSTED)
    neutral = pt.bidi_sanitize(UNTRUSTED, pt.bidi_sanitize_neutralize)
    print(
        f"ergonomics: strip={len(stripped.encode())} "
        f"neutralize={len(neutral.encode())} "
        f"replacement={neutral.count(chr(0xFFFD))}",
        flush=True,
    )
    print(
        "ergonomics: locale "
        f"ar={pt.locale_ui_direction('ar')} "
        f"he={pt.locale_ui_direction('he')} "
        f"az-Arab={pt.locale_ui_direction('az-Arab')} "
        f"az-Latn={pt.locale_ui_direction('az-Latn')} "
        f"C={pt.locale_ui_direction('C')}",
        flush=True,
    )


def main() -> int:
    app = pt.App("pt.example.ergonomics.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)
    app.locale = "he_IL.UTF-8"

    window = pt.Window(app, "Pt Ergonomics - Python", 760, 600)
    root = window.root

    page = pt.column(root).set_padding(24, 24, 24, 24).set_gap(12)

    pt.label(page, "PYTHON BINDING  ·  2.1 ERGONOMICS").set_font_size(11).set_font_weight(
        pt.font_weight_semibold
    ).set_foreground(pt.rgb(0x7AA2F7))

    pt.label(page, HEBREW_MIXED).set_font_size(28).set_font_weight(
        pt.font_weight_bold
    ).set_text_direction(pt.text_direction_auto)

    pt.label(
        page,
        "The locale hint resolves the shell, the caret policy switches the editor and "
        "the card's logical padding follows whichever direction wins.",
    ).set_font_size(13).set_wrap(True).set_foreground(pt.rgb(0x9AA7B8))

    row = pt.row(page).set_gap(10)
    row.set_align(pt.align_center)
    status = pt.label(row, "").set_grow(1).set_font_size(12).set_foreground(pt.rgb(0x9ECE6A))

    card = pt.card(page).set_gap(8).set_ui_direction(pt.ui_direction_rtl)
    pt.label(card, "Logical padding: start 36, end 8").set_font_size(13).set_font_weight(
        pt.font_weight_semibold
    ).set_wrap(True)

    editor = (
        pt.entry(card, "identifier")
        .set_size(420, 36)
        .set_ui_direction(pt.ui_direction_ltr)
        .set_text_direction(pt.text_direction_ltr)
        .set_caret_movement(pt.caret_movement_logical)
        .set_align_self(pt.align_start)
    )
    editor.text = "shalom_el(marhaba, 123);"

    def apply(movement: int, shell: int) -> None:
        app.caret_movement = movement
        window.caret_movement = movement
        editor.caret_movement = pt.caret_movement_logical
        window.ui_direction = shell
        # Re-resolved on the next layout; no second call is needed.
        card.set_padding_logical(12, 36, 12, 8)
        card.ui_direction = shell
        status.text = "logical arrows" if movement == pt.caret_movement_logical else "visual arrows"
        print(
            f"ergonomics: movement={app.caret_movement} "
            f"window_movement={window.caret_movement} "
            f"editor={editor.caret_movement} shell={window.ui_direction} "
            f"locale={app.locale}",
            flush=True,
        )

    pt.button(row, "Visual").set_button_variant(pt.button_ghost).on_click(
        lambda: apply(pt.caret_movement_visual, pt.ui_direction_rtl)
    )
    pt.button(row, "Logical").set_button_variant(pt.button_primary).on_click(
        lambda: apply(pt.caret_movement_logical, pt.ui_direction_rtl)
    )
    pt.button(row, "LTR shell").set_button_variant(pt.button_ghost).on_click(
        lambda: apply(pt.caret_movement_logical, pt.ui_direction_ltr)
    )

    pt.spacer(page)
    pt.button(page, "Quit").set_button_variant(pt.button_ghost).set_align_self(
        pt.align_start
    ).on_click(window.close)

    apply(pt.caret_movement_visual, pt.ui_direction_rtl)
    report_safety()
    window.show()

    if os.environ.get("PT_HEADLESS", "") not in ("", "0"):
        apply(pt.caret_movement_logical, pt.ui_direction_ltr)
        apply(pt.caret_movement_visual, pt.ui_direction_rtl)
        report_safety()
        return app.run_for(250)
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
