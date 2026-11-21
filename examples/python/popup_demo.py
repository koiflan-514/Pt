#!/usr/bin/env python3
"""Pt popup demo - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.


An application-owned popup surface with custom content, runtime placement
changes and a dismiss callback. The popup adopts the detached content card;
the example never touches it after the popup is created.
"""

from __future__ import annotations

import os
import sys
from typing import Any

import pt


PLACEMENT_NAMES = {
    pt.popup_placement_below: "below",
    pt.popup_placement_above: "above",
    pt.popup_placement_right: "right",
    pt.popup_placement_left: "left",
}


def main() -> int:
    app = pt.App("pt.example.popup_demo.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)

    window = pt.Window(app, "Pt Popup Demo - Python", 620, 460)
    root = window.root
    root.set_padding(26, 26, 26, 26).set_gap(14)

    pt.label(root, "POPUP  ·  APPLICATION-OWNED SURFACE  ·  PYTHON").set_font_size(
        11
    ).set_font_weight(pt.font_weight_semibold).set_foreground(pt.rgb(0x7AA2F7))
    pt.label(root, "Custom content, real placement").set_font_size(28).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        root,
        "The popup is a Pt surface built from ordinary widgets. The anchor is the "
        "trigger button rectangle; the placement flips and slides to stay inside the "
        "compositor work area. Escape, an outside click and Dismiss all run the same "
        "callback.",
    ).set_font_size(13).set_foreground(pt.rgb(0x9AA7B8)).set_wrap(True)

    card = pt.card(root)
    card.set_padding(18, 18, 18, 18).set_gap(12)

    actions = pt.row(card)
    actions.set_gap(10).set_align_self(pt.align_center)

    state: dict[str, Any] = {
        "popup": None,
        "placement": pt.popup_placement_below,
        "shows": 0,
        "dismissals": 0,
        "status": None,
        "placement_button": None,
    }

    def trigger_anchor() -> tuple[float, float, float, float]:
        return trigger.bounds

    def refresh_status(what: str) -> None:
        popup: pt.Popup = state["popup"]
        state["status"].text = (
            f"{what} · placement {PLACEMENT_NAMES[state['placement']]} · "
            f"open {'yes' if popup.is_open else 'no'} · "
            f"shown {state['shows']}, dismissed {state['dismissals']}"
        )

    def open_popup() -> None:
        popup: pt.Popup = state["popup"]
        if popup.show(trigger_anchor()):
            state["shows"] += 1
            refresh_status("Popup opened with the current placement")

    def on_dismiss(popup: pt.Popup) -> None:
        state["dismissals"] += 1
        print(
            f"popup-demo-python: dismiss #{state['dismissals']} "
            f"(is_open={popup.is_open})",
            flush=True,
        )
        refresh_status("Dismiss callback fired after the popup closed")

    trigger = pt.button(actions, "Open popup")
    trigger.set_button_variant(pt.button_primary).set_tooltip(
        "Anchors the popup to this button"
    ).on_click(open_popup)

    def toggle() -> None:
        popup: pt.Popup = state["popup"]
        if popup.is_open:
            popup.dismiss()
            refresh_status("pt_popup_dismiss called")
        else:
            open_popup()

    pt.button(actions, "Toggle").set_button_variant(pt.button_secondary).on_click(toggle)

    def reposition() -> None:
        popup: pt.Popup = state["popup"]
        x, y, width, height = trigger_anchor()
        if popup.reposition((x + 24, y + 18, width, height)):
            refresh_status("pt_popup_reposition moved the open popup")
        else:
            refresh_status("Popup is closed; reposition is a no-op")

    pt.button(actions, "Reposition").set_button_variant(pt.button_ghost).on_click(
        reposition
    )

    def cycle_placement() -> None:
        popup: pt.Popup = state["popup"]
        order = [
            pt.popup_placement_below,
            pt.popup_placement_above,
            pt.popup_placement_right,
            pt.popup_placement_left,
        ]
        state["placement"] = order[(order.index(popup.placement) + 1) % len(order)]
        popup.placement = state["placement"]
        state["placement_button"].text = PLACEMENT_NAMES[state["placement"]]
        if popup.is_open:
            popup.reposition(trigger_anchor())
        refresh_status("Placement preference changed")

    state["placement_button"] = (
        pt.button(actions, PLACEMENT_NAMES[state["placement"]])
        .set_button_variant(pt.button_soft)
        .set_tooltip("Cycle below → above → right → left")
        .on_click(cycle_placement)
    )

    # Detached content: the popup adopts it and owns it from then on.
    content = pt.card(None)
    content.set_padding(16, 16, 16, 16).set_gap(10).set_radius(12).set_min_width(
        260
    ).set_background(pt.rgb(0x161D28)).set_border_width(1).set_border_color(
        pt.rgb(0x30415C)
    )
    content.a11y_name = "Popup content"
    pt.label(content, "Popup content").set_font_size(16).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        content,
        "This card lives in the popup tree. Escape or a click outside dismisses it and "
        "fires the callback below.",
    ).set_font_size(12).set_foreground(pt.rgb(0x9AA7B8)).set_wrap(True)
    content_actions = pt.row(content)
    content_actions.set_gap(8)
    pt.button(content_actions, "Ping").set_button_variant(pt.button_secondary).on_click(
        lambda: refresh_status("Ping from the popup content")
    )

    def close_from_content() -> None:
        state["popup"].dismiss()
        refresh_status("Close button inside the popup")

    pt.button(content_actions, "Dismiss").set_button_variant(pt.button_ghost).on_click(
        close_from_content
    )

    popup = pt.Popup.create(window, content)
    state["popup"] = popup
    popup.on_dismiss(on_dismiss)
    popup.placement = state["placement"]

    state["status"] = pt.label(root, "").set_font_size(12).set_foreground(
        pt.rgb(0x9ECE6A)
    ).set_wrap(True)
    refresh_status("Ready; the popup starts closed")

    pt.label(
        root,
        "Widget.bounds reports window logical coordinates; the popup anchor is the same "
        "rectangle. Destroying the popup or its window does not fire the dismiss "
        "callback.",
    ).set_font_size(12).set_foreground(pt.rgb(0x6F7C8F)).set_wrap(True)

    pt.button(root, "Quit").set_button_variant(pt.button_ghost).set_align_self(
        pt.align_start
    ).on_click(window.close)

    window.show()
    trigger.focus()

    if os.environ.get("PT_HEADLESS") not in (None, "", "0"):
        open_popup()
        print(
            f"popup-demo-python: open={popup.is_open} "
            f"placement={PLACEMENT_NAMES[popup.placement]} "
            f"content={popup.content!r}"
        )
        popup.placement = pt.popup_placement_above
        state["placement"] = pt.popup_placement_above
        popup.reposition(trigger_anchor())
        popup.dismiss()
        open_popup()
        popup.dismiss()
        print(
            f"popup-demo-python: after dismiss open={popup.is_open} "
            f"dismissals={state['dismissals']}"
        )
        status = app.run_for(250)
    else:
        status = app.run()
    popup.destroy()
    return status


if __name__ == "__main__":
    sys.exit(main())