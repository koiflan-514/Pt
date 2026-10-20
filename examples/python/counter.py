#!/usr/bin/env python3
"""Pt counter example - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.
"""

from __future__ import annotations

import pt


def main() -> int:
    app = pt.App("pt.example.counter.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x9ECE6A)

    window = pt.Window(app, "Pt Counter - Python", 560, 520)
    root = window.root

    header = pt.column(root).set_gap(6).set_margin(0, 0, 4, 0)
    pt.label(header, "PYTHON BINDING").set_font_size(11).set_font_weight(
        pt.font_weight_semibold
    ).set_foreground(pt.rgb(0x9ECE6A))
    pt.label(header, "Counter").set_font_size(30).set_font_weight(pt.font_weight_bold)
    pt.label(
        header,
        "ctypes talks to the same native widgets as C. Callbacks are ordinary "
        "Python closures; Pt keeps their trampolines alive for you.",
    ).set_font_size(14).set_wrap(True).set_foreground(pt.rgb(0x9AA7B8))

    card = pt.card(root)
    card.set_padding(28, 28, 28, 28).set_radius(20)
    card.set_background(pt.rgb(0x161D28)).set_border_width(1)
    card.set_border_color(pt.rgb(0x253040)).set_gap(18)

    state = {"count": 0, "step": 1}

    controls = pt.row(card)
    controls.set_gap(22).set_justify(pt.justify_center).set_align(pt.align_center)

    pt.button(controls, "-").set_size(52, 52).set_font_size(22).set_button_variant(
        pt.button_secondary
    ).set_tooltip("Decrease by the current step").on_click(
        lambda: change(-state["step"])
    )
    value = (
        pt.label(controls, "0")
        .set_min_width(150)
        .set_font_size(56)
        .set_font_weight(pt.font_weight_bold)
        .set_text_align(pt.text_align_center)
    )
    pt.button(controls, "+").set_size(52, 52).set_font_size(22).set_button_variant(
        pt.button_primary
    ).set_tooltip("Increase by the current step").on_click(
        lambda: change(state["step"])
    )

    status = (
        pt.label(card, "Ready")
        .set_font_size(12)
        .set_foreground(pt.rgb(0x6F7C8F))
        .set_text_align(pt.text_align_center)
    )

    def render() -> None:
        value.text = str(state["count"])

    def change(delta: int) -> None:
        state["count"] += delta
        render()
        status.text = f"Count changed by {delta:+d}"

    def reset() -> None:
        state["count"] = 0
        render()
        status.text = "Reset to zero"
        window.show_toast("Counter reset", pt.toast_success, 1800)

    def update_step(text: str) -> None:
        try:
            step = int(text or "1")
        except ValueError:
            status.text = "Step must be an integer"
            return
        state["step"] = step if step != 0 else 1
        status.text = f"Step set to {state['step']}"

    step_row = pt.row(card).set_gap(10).set_align(pt.align_center)
    pt.label(step_row, "Step").set_font_size(13).set_foreground(pt.rgb(0x9AA7B8))
    step_entry = pt.entry(step_row, "1").set_size(90, 34)
    step_entry.text = "1"
    step_entry.on_change(update_step)

    actions = pt.row(card).set_gap(10).set_justify(pt.justify_center)
    pt.button(actions, "Reset").set_button_variant(pt.button_ghost).on_click(reset)
    menu = pt.Menu(window)
    menu.add_item("Reset counter", callback=reset)
    menu.add_separator()
    menu.add_item("Quit", callback=window.close)
    menu_button = (
        pt.button(actions, "Actions")
        .set_button_variant(pt.button_ghost)
        .set_tooltip("Open the actions menu")
    )
    menu_button.on_click(lambda: menu.show_below(menu_button))
    pt.button(actions, "Quit").set_button_variant(pt.button_ghost).on_click(window.close)

    pt.spacer(root)
    pt.label(root, "Edit the step field, press Tab to move focus, Esc to quit.").set_font_size(
        12
    ).set_foreground(pt.rgb(0x6F7C8F))

    def on_key(_window: pt.Window, key: int, _modifiers: int) -> bool:
        if key == 0xFF1B:
            window.close()
            return True
        return False

    window.on_key(on_key)
    window.show()
    app.run()
    menu.destroy()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
