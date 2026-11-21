#!/usr/bin/env python3
"""Pt primary selection example - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

Drag-select the left pane with Window.on_pointer(), publish from the
button-release callback where the event serial is current, then middle-click the
right pane to read the primary selection back. Paint callbacks follow the same
purity and clip-balance rules as the C example.
"""

from __future__ import annotations

import math
from contextlib import contextmanager
from typing import Iterator

import pt

PANE_PAD = 16.0
PANE_HEADER = 44.0
PANE_TOP_GAP = 8.0
PANE_FONT = 13.0
PANE_LINE = 20.0
PANE_GUTTER = 30.0
FONT_FAMILY = "monospace"

BUTTON_LEFT = 0x110
BUTTON_MIDDLE = 0x112

SOURCE_LINES = [
    "Primary selection is the middle-click clipboard.",
    "Drag across these lines to select a range.",
    "On button release the range is published",
    "from inside the same pointer callback.",
    "The compositor serial therefore belongs",
    "to the release that produced the selection.",
    "",
    "Middle-click the paste pane to read it back.",
]
TARGET_LINES = 9


@contextmanager
def clipped(ctx: pt.PaintContext, x: float, y: float, width: float,
            height: float) -> Iterator[None]:
    ctx.push_clip(x, y, width, height)
    try:
        yield
    finally:
        ctx.pop_clip()


def position_compare(a: tuple[int, int], b: tuple[int, int]) -> int:
    if a[0] != b[0]:
        return -1 if a[0] < b[0] else 1
    if a[1] != b[1]:
        return -1 if a[1] < b[1] else 1
    return 0


class Selection:
    """Mutable selection and paste state; paint reads it only."""

    def __init__(self, window: pt.Window) -> None:
        self.window = window
        self.source: pt.Widget | None = None
        self.target: pt.Widget | None = None
        self.status: pt.Widget | None = None
        self.anchor = (0, 0)
        self.head = (0, 0)
        self.dragging = False
        self.has_selection = False
        self.target_lines: list[str] = []
        self.highlight = -1
        self.grid: tuple[float, float, float] | None = None
        self.publishes = 0
        self.pastes = 0

    def set_status(self, text: str) -> None:
        if self.status is not None:
            self.status.text = text

    def position_at(self, x: float, y: float) -> tuple[int, int]:
        assert self.source is not None
        bx, by, _bw, _bh = self.source.bounds
        _ascent, line_height, cell_width = self.grid or (
            PANE_FONT, PANE_LINE, PANE_FONT * 0.62
        )
        top = by + PANE_HEADER + PANE_TOP_GAP
        left = bx + PANE_PAD + PANE_GUTTER
        line = math.floor((y - top) / line_height)
        line = min(max(line, 0), len(SOURCE_LINES) - 1)
        length = len(SOURCE_LINES[line])
        column = round((x - left) / cell_width)
        column = min(max(column, 0), length)
        return (line, column)

    def selection_on_line(self, line: int, length: int) -> tuple[int, int] | None:
        if not self.has_selection:
            return None
        start, end = self.anchor, self.head
        if position_compare(start, end) > 0:
            start, end = end, start
        if line < start[0] or line > end[0]:
            return None
        first = start[1] if line == start[0] else 0
        last = end[1] if line == end[0] else length
        first = min(max(first, 0), length)
        last = min(max(last, 0), length)
        if last < first:
            first, last = last, first
        return (first, last) if last > first else None

    def selection_text(self) -> str:
        start, end = self.anchor, self.head
        if position_compare(start, end) > 0:
            start, end = end, start
        # The sample text is ASCII, so columns and UTF-8 byte offsets agree.
        pieces: list[str] = []
        for line in range(start[0], end[0] + 1):
            text = SOURCE_LINES[line]
            first = start[1] if line == start[0] else 0
            last = end[1] if line == end[0] else len(text)
            pieces.append(text[first:last])
        return "\n".join(pieces)

    def paste_primary(self) -> None:
        primary = self.window.primary
        if not primary:
            self.set_status("Primary is empty. Drag over the source pane first.")
            return
        added = 0
        for line in primary.split("\n"):
            if not line:
                continue
            if len(self.target_lines) == TARGET_LINES:
                self.target_lines.pop(0)
            self.target_lines.append("> " + line[:120])
            added += 1
        self.pastes += 1
        self.highlight = len(self.target_lines) - 1
        byte_length = len(primary.encode("utf-8"))
        self.set_status(
            f"Read {byte_length} bytes from primary in this pointer event; "
            f"appended {added} line(s), paste #{self.pastes}."
        )
        assert self.target is not None
        self.target.request_redraw()

    def on_pointer(self, _window: pt.Window, event: pt.PointerEvent) -> bool:
        assert self.source is not None and self.target is not None
        sx, sy, sw, sh = self.source.bounds
        tx, ty, tw, th = self.target.bounds
        over_source = sx <= event.x < sx + sw and sy <= event.y < sy + sh
        over_target = tx <= event.x < tx + tw and ty <= event.y < ty + th

        if event.phase == pt.pointer_phase_down:
            if event.button == BUTTON_LEFT and over_source:
                self.anchor = self.position_at(event.x, event.y)
                self.head = self.anchor
                self.dragging = True
                self.has_selection = False
                self.set_status(
                    "Selecting; release inside the window to publish the range."
                )
                self.source.request_redraw()
                return True
            if event.button == BUTTON_MIDDLE and over_target:
                self.paste_primary()
                return True
        elif event.phase == pt.pointer_phase_motion and self.dragging:
            nxt = self.position_at(event.x, event.y)
            if nxt != self.head:
                self.head = nxt
                self.has_selection = self.head != self.anchor
                self.source.request_redraw()
        elif event.phase == pt.pointer_phase_up and self.dragging and (
            event.button == BUTTON_LEFT
        ):
            self.head = self.position_at(event.x, event.y)
            self.dragging = False
            self.has_selection = self.head != self.anchor
            if not self.has_selection:
                self.set_status(
                    "Click without drag selects nothing; primary is unchanged."
                )
                self.source.request_redraw()
                return True
            payload = self.selection_text()
            if not payload:
                self.set_status("Selection contains no text; primary is unchanged.")
                self.source.request_redraw()
                return True
            # Publish inside the release callback: the Wayland set_selection
            # serial belongs to this event.
            self.window.primary = payload
            readback = self.window.primary
            self.publishes += 1
            start, end = self.anchor, self.head
            if position_compare(start, end) > 0:
                start, end = end, start
            self.set_status(
                f"Published {len(payload.encode('utf-8'))} bytes from "
                f"{end[0] - start[0] + 1} line(s); read back "
                f"{len(readback.encode('utf-8'))} bytes, publish #{self.publishes}."
            )
            self.source.request_redraw()
            return True
        elif event.phase == pt.pointer_phase_cancel and self.dragging:
            self.dragging = False
            self.set_status("Selection cancelled; primary is unchanged.")
            self.source.request_redraw()
        return False

    def paint_source(self, _widget: pt.Widget, ctx: pt.PaintContext) -> None:
        width = ctx.width
        height = ctx.height
        if width <= 0 or height <= 0 or ctx.clip_rect() is None:
            return
        ascent, line_height, cell_width = self._metrics(ctx)
        self.grid = (ascent, line_height, cell_width)
        top = PANE_HEADER + PANE_TOP_GAP
        text_x = PANE_PAD + PANE_GUTTER
        body_height = max(0.0, height - top - PANE_PAD)

        ctx.draw_text(
            FONT_FAMILY, 12.0, pt.font_weight_semibold, pt.font_slant_upright,
            PANE_PAD, 27.0, "PRIMARY SOURCE", pt.rgb(0x7AA2F7),
        )
        hint = "drag to select"
        hint_width = ctx.measure_text(
            FONT_FAMILY, 11.0, pt.font_weight_regular, pt.font_slant_upright, hint
        )
        ctx.draw_text(
            FONT_FAMILY, 11.0, pt.font_weight_regular, pt.font_slant_upright,
            width - PANE_PAD - hint_width, 26.0, hint, pt.rgb(0x6F7C8F),
        )
        ctx.draw_line(
            PANE_PAD, PANE_HEADER - 1.0, width - PANE_PAD, PANE_HEADER - 1.0, 1.0,
            pt.rgb(0x263241),
        )
        with clipped(ctx, PANE_PAD, top, width - 2 * PANE_PAD, body_height):
            for line, text in enumerate(SOURCE_LINES):
                row_y = top + line * line_height
                span = self.selection_on_line(line, len(text))
                if span is not None:
                    first, last = span
                    ctx.fill_rect(
                        text_x + first * cell_width, row_y,
                        (last - first) * cell_width, line_height, pt.rgb(0x2B4A78),
                    )
                ctx.draw_text(
                    FONT_FAMILY, 11.0, pt.font_weight_regular, pt.font_slant_upright,
                    PANE_PAD + 4.0, row_y + ascent, f"{line + 1:02d}", pt.rgb(0x56657A),
                )
                ctx.draw_text(
                    FONT_FAMILY, PANE_FONT, pt.font_weight_regular, pt.font_slant_upright,
                    text_x, row_y + ascent, text, pt.rgb(0xC3D0E1),
                )
            ctx.draw_line(
                PANE_PAD + 24.0, top + 4.0, PANE_PAD + 24.0, top + body_height - 4.0,
                1.0, pt.rgb(0x263241),
            )

    def paint_target(self, _widget: pt.Widget, ctx: pt.PaintContext) -> None:
        width = ctx.width
        height = ctx.height
        if width <= 0 or height <= 0 or ctx.clip_rect() is None:
            return
        ascent, line_height, _cell_width = self._metrics(ctx)
        top = PANE_HEADER + PANE_TOP_GAP
        body_height = max(0.0, height - top - PANE_PAD)

        ctx.draw_text(
            FONT_FAMILY, 12.0, pt.font_weight_semibold, pt.font_slant_upright,
            PANE_PAD, 27.0, "PASTE TARGET", pt.rgb(0x9ECE6A),
        )
        hint = "middle-click to paste"
        hint_width = ctx.measure_text(
            FONT_FAMILY, 11.0, pt.font_weight_regular, pt.font_slant_upright, hint
        )
        ctx.draw_text(
            FONT_FAMILY, 11.0, pt.font_weight_regular, pt.font_slant_upright,
            width - PANE_PAD - hint_width, 26.0, hint, pt.rgb(0x6F7C8F),
        )
        ctx.draw_line(
            PANE_PAD, PANE_HEADER - 1.0, width - PANE_PAD, PANE_HEADER - 1.0, 1.0,
            pt.rgb(0x263241),
        )
        with clipped(ctx, PANE_PAD, top, width - 2 * PANE_PAD, body_height):
            if not self.target_lines:
                ctx.draw_text(
                    FONT_FAMILY, PANE_FONT, pt.font_weight_regular,
                    pt.font_slant_upright, PANE_PAD, top + ascent,
                    "Middle-click here after selecting text.", pt.rgb(0x6F7C8F),
                )
            for line, text in enumerate(self.target_lines):
                row_y = top + line * line_height
                if line == self.highlight:
                    ctx.fill_rect(
                        PANE_PAD, row_y, width - 2 * PANE_PAD, line_height,
                        pt.rgb(0x1D3524),
                    )
                ctx.draw_text(
                    FONT_FAMILY, PANE_FONT, pt.font_weight_regular, pt.font_slant_upright,
                    PANE_PAD, row_y + ascent, text,
                    pt.rgb(0xD7F5DC) if line == self.highlight else pt.rgb(0xC3D0E1),
                )

    @staticmethod
    def _metrics(ctx: pt.PaintContext) -> tuple[float, float, float]:
        ascent = PANE_FONT
        line_height = PANE_LINE
        cell_width = PANE_FONT * 0.62
        metrics = ctx.font_metrics(
            FONT_FAMILY, PANE_FONT, pt.font_weight_regular, pt.font_slant_upright
        )
        if metrics is not None:
            ascent, _descent, line_height, cell_width = metrics
            line_height = max(line_height, PANE_FONT + 2.0)
            if cell_width <= 0:
                cell_width = PANE_FONT * 0.62
        return ascent, line_height, cell_width


def main() -> int:
    app = pt.App("pt.example.primary_selection.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x7AA2F7)

    window = pt.Window(app, "Pt Primary Selection - Python", 980, 640)
    state = Selection(window)
    window.on_pointer(state.on_pointer)

    root = window.root
    root.set_padding(22, 22, 22, 22).set_gap(12)
    pt.label(root, "Primary selection").set_font_size(26).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        root,
        "Drag over the left pane, release, then middle-click the right pane. The "
        "set happens inside the button-release callback, so the serial belongs to "
        "the event that produced the selection.",
    ).set_font_size(13).set_foreground(pt.rgb(0x9AA7B8)).set_wrap(True)

    panes = pt.row(root).set_gap(14).set_grow(1)

    state.source = pt.box(panes, pt.direction_column).set_grow(1).set_min_height(
        300
    ).set_background(pt.rgb(0x0D131C)).set_radius(14).set_border_width(
        1
    ).set_border_color(
        pt.rgb(0x263241)
    )
    state.source.on_paint(state.paint_source)
    state.source.a11y_role = pt.a11y_role_text
    state.source.a11y_name = "Primary selection source"
    state.source.a11y_description = (
        "Eight log lines. Drag across them to select and publish a primary selection."
    )

    state.target = pt.box(panes, pt.direction_column).set_grow(1).set_min_height(
        300
    ).set_background(pt.rgb(0x0D131C)).set_radius(14).set_border_width(
        1
    ).set_border_color(
        pt.rgb(0x263241)
    )
    state.target.on_paint(state.paint_target)
    state.target.a11y_role = pt.a11y_role_text_frame
    state.target.a11y_name = "Primary paste target"
    state.target.a11y_description = (
        "Middle-click to append the current primary selection."
    )

    actions = pt.row(root).set_gap(10).set_align_self(pt.align_center)

    def read_primary() -> None:
        text = window.primary
        if not text:
            state.set_status("Primary is empty.")
            return
        preview = text[:44].replace("\n", " ")
        state.set_status(
            f"Primary holds {len(text.encode('utf-8'))} bytes: "
            f"\"{preview}{'…' if len(text) > 44 else ''}\"."
        )

    def clear_target() -> None:
        state.target_lines.clear()
        state.highlight = -1
        state.set_status("Paste target cleared; primary is untouched.")
        assert state.target is not None
        state.target.request_redraw()

    pt.button(actions, "Read primary").set_button_variant(
        pt.button_secondary
    ).set_tooltip(
        "Reads Window.primary, which frees the native buffer for you"
    ).on_click(
        read_primary
    )
    pt.button(actions, "Clear paste target").set_button_variant(
        pt.button_ghost
    ).on_click(
        clear_target
    )

    state.status = pt.label(
        actions, "Drag across the source lines to make a selection."
    ).set_font_size(12).set_foreground(pt.rgb(0x9ECE6A)).set_wrap(True).set_grow(1)

    pt.label(
        root,
        "Window.primary copies on set and returns caller-owned UTF-8 that the "
        "wrapper frees on get. Without wp_primary_selection_unstable_v1 (or under "
        "PT_HEADLESS) both sides fall back to the in-process buffer, so the "
        "example still runs.",
    ).set_font_size(12).set_foreground(pt.rgb(0x6F7C8F)).set_wrap(True)

    window.show()
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
