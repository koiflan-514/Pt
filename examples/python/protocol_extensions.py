#!/usr/bin/env python3
"""Pt protocol extensions - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

``request_activation_token`` requests an asynchronous one-shot token and calls
back once with a Python ``str`` borrowed for the call. ``activate`` sends a
token and retains nothing. ``export_foreign_handle`` delivers one opaque
borrowed handle; ``set_foreign_parent`` imports it on another window.
``unexport_foreign_handle`` and ``clear_foreign_parent`` are idempotent.

The wrapper appends its CFUNCTYPE trampolines to the callback roots, so a
token or handle delivered after this function returns still finds a live
callback. Without the protocol globals, or on a headless platform, every call
returns False and no callback runs. PT_HEADLESS=1 asserts that fallback.
"""

from __future__ import annotations

import os

import pt


def add_text(root: pt.Widget, text: str, size: float, color: int) -> None:
    pt.label(root, text).set_font_size(size).set_foreground(color).set_wrap(True)


def main() -> int:
    app = pt.App("dev.pt.examples.protocol_extensions.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)

    window = pt.Window(app, "Pt Protocol Extensions - Python", 720, 420)
    dialog = pt.Window(app, "Pt Foreign Dialog - Python", 420, 240)
    root = window.root
    root.set_padding(24, 24, 24, 24).set_gap(10)

    pt.label(root, "PYTHON BINDING  ·  ACTIVATION  ·  XDG-FOREIGN-V2").set_font_size(
        11
    ).set_font_weight(pt.font_weight_semibold).set_foreground(pt.rgb(0x7AA2F7))
    add_text(
        root,
        "The window requests a one-shot activation token, activates with the "
        "borrowed string, exports a foreign toplevel handle and imports it on "
        "the second window.",
        13,
        pt.rgb(0x9AA7B8),
    )
    add_text(
        root,
        "A token or handle callback is valid only during the call. True from "
        "activate means sent, not focused, and the handle is never parsed.",
        12,
        pt.rgb(0x6F7C8F),
    )

    state = {"token_seen": False, "handle_seen": False, "token": ""}

    def on_token(source: pt.Window, token: str) -> None:
        state["token_seen"] = True
        state["token"] = token
        # The string is borrowed for this call; state["token"] is our copy.
        if token:
            source.activate(token)

    def on_handle(exporter: pt.Window, handle: str) -> None:
        del exporter
        state["handle_seen"] = bool(handle)
        if handle:
            dialog.set_foreign_parent(handle)

    window.show()
    dialog.show()

    headless = os.environ.get("PT_HEADLESS", "") not in ("", "0")
    failures = 0
    if headless:
        token_request = window.request_activation_token(
            "dev.pt.examples.protocol_extensions.python", on_token
        )
        activated = window.activate("headless-token")
        exported = window.export_foreign_handle(on_handle)
        window.unexport_foreign_handle()
        window.unexport_foreign_handle()
        parented = dialog.set_foreign_parent("headless-handle")
        dialog.clear_foreign_parent()
        dialog.clear_foreign_parent()

        if token_request or activated or exported or parented:
            failures += 1
        if state["token_seen"] or state["handle_seen"]:
            failures += 1
        print(
            "protocol_extensions: headless "
            f"request={int(token_request)} activate={int(activated)} "
            f"export={int(exported)} parent={int(parented)} "
            f"callbacks={int(state['token_seen'])}/{int(state['handle_seen'])} "
            f"failures={failures}",
            flush=True,
        )
        run_status = app.run_for(250)
    else:
        token_request = window.request_activation_token(
            "dev.pt.examples.protocol_extensions.python", on_token
        )
        exported = window.export_foreign_handle(on_handle)
        print(
            "protocol_extensions: live "
            f"request={int(token_request)} export={int(exported)}",
            flush=True,
        )
        app.run()
        run_status = 0

    return 1 if failures else run_status


if __name__ == "__main__":
    raise SystemExit(main())