#!/usr/bin/env python3
"""Pt input panel example - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

1.3 adds the text-input-v3 v2 desired-state requests to bindings/python/pt.py:

    Window.show_input_panel() -> bool
    Window.hide_input_panel() -> bool

Both mirror pt_window_show_input_panel / pt_window_hide_input_panel and return
True only when the recorded desired state changed.

The state is a hint: the compositor decides whether an on-screen keyboard
appears, and Pt never reports whether it did. Headless windows still record the
desired state. preedit_hint ranges are drawn by Pt inside entries and text
areas only; the custom IME callbacks below receive text and cursor offsets but
no hint metadata.
"""

from __future__ import annotations

import os

import pt


def main() -> int:
    app = pt.App("pt.example.input_panel.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)

    window = pt.Window(app, "Pt Input Panel - Python", 680, 540)
    state = {
        "desired": False,
        "show_changes": 0,
        "hide_changes": 0,
        "preedit": "",
        "committed": "",
    }

    root = window.root
    root.set_padding(26, 26, 26, 26).set_gap(14)

    pt.label(root, "PYTHON BINDING  -  INPUT PANEL").set_font_size(11).set_font_weight(
        pt.font_weight_semibold
    ).set_foreground(pt.rgb(0x7AA2F7))
    pt.label(root, "Show and hide the on-screen input panel").set_font_size(28).set_font_weight(
        pt.font_weight_bold
    )
    pt.label(
        root,
        "The two calls are text-input-v3 v2 hints. Their bool result is the local desired "
        "state transition, not proof that an OSK appeared.",
    ).set_font_size(13).set_wrap(True).set_foreground(pt.rgb(0x9AA7B8))

    card = pt.card(root)
    card.set_padding(18, 18, 18, 18).set_gap(12)

    entry = pt.entry(card, "Type here - a real IME shows hinted underlines")

    entry_status = pt.label(card, 'Entry text: ""')
    entry_status.set_font_size(12).set_wrap(True).set_foreground(pt.rgb(0x9AA7B8))
    entry.on_change(lambda text: setattr(entry_status, "text", f'Entry text: "{text}"'))

    buttons = pt.row(card)
    buttons.set_gap(10)

    panel_status = pt.label(card, "Panel: hidden (initial desired state)")
    panel_status.set_font_size(12).set_wrap(True).set_foreground(pt.rgb(0x9ECE6A))

    def show_panel() -> None:
        changed = window.show_input_panel()
        if changed:
            state["desired"] = True
            state["show_changes"] += 1
        panel_status.text = (
            "Panel: show "
            + ("recorded" if changed else "already requested")
            + f" (desired {'visible' if state['desired'] else 'hidden'})"
        )
        print(f"input_panel_py: show changed={int(changed)}", flush=True)

    def hide_panel() -> None:
        changed = window.hide_input_panel()
        if changed:
            state["desired"] = False
            state["hide_changes"] += 1
        panel_status.text = (
            "Panel: hide "
            + ("recorded" if changed else "already requested")
            + f" (desired {'visible' if state['desired'] else 'hidden'})"
        )
        print(f"input_panel_py: hide changed={int(changed)}", flush=True)

    pt.button(buttons, "Show input panel").set_button_variant(pt.button_primary).set_tooltip(
        "zwp_text_input_v3.show_input_panel"
    ).on_click(show_panel)
    pt.button(buttons, "Hide input panel").set_button_variant(pt.button_secondary).set_tooltip(
        "zwp_text_input_v3.hide_input_panel"
    ).on_click(hide_panel)

    preedit_status = pt.label(card, "Preedit: (none)")
    preedit_status.set_font_size(12).set_wrap(True).set_foreground(pt.rgb(0xE0AF68))

    def on_commit(_window: pt.Window, text: str) -> None:
        state["committed"] += text
        state["preedit"] = ""
        preedit_status.text = f'Committed "{text}"; custom buffer "{state["committed"]}"'
        print(f'input_panel_py: commit "{text}"', flush=True)

    def on_preedit(
        _window: pt.Window, text: str, cursor_begin: int, cursor_end: int
    ) -> None:
        state["preedit"] = text
        if text:
            preedit_status.text = (
                f'Preedit "{text}" cursor {cursor_begin}..{cursor_end} (app-drawn)'
            )
        else:
            preedit_status.text = "Preedit: (none) - an empty preedit ends the composition"
        print(f'input_panel_py: preedit "{text}"', flush=True)

    # Active whenever no entry holds editable focus; the entry's own IME wins
    # while it is focused.
    window.set_ime_callbacks(commit=on_commit, preedit=on_preedit)
    window.set_ime_enabled(True)
    window.set_ime_cursor_rect(24, 320, 2, 20)

    pt.label(
        root,
        "Headless runs record the desired state only. preedit_hint ranges are drawn by Pt "
        "inside entries and are absent from the custom callback ABI.",
    ).set_font_size(12).set_wrap(True).set_foreground(pt.rgb(0x6F7C8F))

    pt.button(root, "Quit").set_button_variant(pt.button_ghost).set_align(pt.align_start).on_click(
        app.quit
    )

    window.show()
    window.focus(entry)

    if os.environ.get("PT_HEADLESS", "") not in ("", "0"):
        show_panel()
        show_panel()
        hide_panel()
        hide_panel()
        print(
            "input_panel_py: show_changes="
            f"{state['show_changes']} hide_changes={state['hide_changes']}",
            flush=True,
        )
        return app.run_for(250)
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main())