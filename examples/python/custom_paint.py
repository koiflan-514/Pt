#!/usr/bin/env python3
"""Pt custom paint dashboard - Python bindings.

Copyright 2026 The Pt Authors.
Licensed under the Apache License, Version 2.0.

The ctypes wrapper exposes the same borrowed PaintContext as the C API. The
callback is a pure function of Dashboard state, clip pushes are paired with a
context manager, and input callbacks request damage instead of changing the
tree from inside paint.
"""

from __future__ import annotations

import math
from contextlib import contextmanager
from typing import Iterator

import pt

PANEL_PAD = 18.0
PANEL_HEADER = 48.0
PANEL_FOOTER = 34.0
TERMINAL_FONT = 13.0
GAUGE_WIDTH = 132.0
FONT_FAMILY = "monospace"

ROWS = 8

INITIAL_LINES = [
    "pt_paint_* draws into a borrowed paint context.",
    "Coordinates are widget-local logical pixels.",
    "Every push_clip has exactly one pop_clip.",
    "Paint is pure: state in, pixels out, no counters.",
    "One damage tile may be painted 0..N times.",
    "Caret and gauge state live outside the callback.",
    "Callbacks mutate state, then request_redraw().",
    "request_redraw_rect() damages only the gauge.",
]


@contextmanager
def clipped(ctx: pt.PaintContext, x: float, y: float, width: float,
            height: float) -> Iterator[None]:
    """Balances push_clip/pop_clip even if drawing raises."""
    ctx.push_clip(x, y, width, height)
    try:
        yield
    finally:
        ctx.pop_clip()


def intersects(ax: float, ay: float, aw: float, ah: float,
               bx: float, by: float, bw: float, bh: float) -> bool:
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by


class Dashboard:
    """All mutable state lives here; paint only reads it."""

    def __init__(self) -> None:
        self.window: pt.Window | None = None
        self.panel: pt.Widget | None = None
        self.status: pt.Widget | None = None
        self.load = 0.42
        self.frame = 0
        self.appended = 0
        self.caret_on = True
        self.lines = list(INITIAL_LINES)

    def reset(self) -> None:
        self.frame = 0
        self.appended = 0
        self.lines = list(INITIAL_LINES)
        assert self.status is not None
        self.status.text = "Log reset; the paint callback re-reads state."
        assert self.panel is not None
        self.panel.request_redraw()

    def append(self) -> None:
        self.appended += 1
        self.frame += 1
        self.lines = self.lines[1:] + [
            f"[{self.appended:03d}] appended in on_click; redraw is deferred"
        ]
        assert self.status is not None
        self.status.text = (
            f"Line {self.appended} appended; requested a widget repaint."
        )
        assert self.panel is not None
        self.panel.request_redraw()

    def set_load(self, value: float) -> None:
        self.load = min(max(float(value), 0.0), 1.0)
        assert self.panel is not None
        _x, _y, width, height = self.panel.bounds
        gauge_width = min(GAUGE_WIDTH + 24.0, width)
        # Damage only the gauge column; the terminal text stays clean.
        self.panel.request_redraw_rect(
            width - gauge_width - PANEL_PAD,
            PANEL_HEADER,
            gauge_width,
            max(0.0, height - PANEL_HEADER - PANEL_FOOTER),
        )
        if self.status is not None:
            self.status.text = (
                f"Load {self.load * 100:.0f}%; damaged only the gauge column."
            )

    def paint(self, _widget: pt.Widget, ctx: pt.PaintContext) -> None:
        width = ctx.width
        height = ctx.height
        if width <= 0 or height <= 0:
            return
        clip = ctx.clip_rect()
        if clip is None:
            return

        ascent = TERMINAL_FONT
        line_height = TERMINAL_FONT * 1.45
        cell_width = TERMINAL_FONT * 0.62
        metrics = ctx.font_metrics(
            FONT_FAMILY, TERMINAL_FONT, pt.font_weight_regular, pt.font_slant_upright
        )
        if metrics is not None:
            ascent, _descent, line_height, cell_width = metrics
            line_height = max(line_height, TERMINAL_FONT + 7.0)
            if cell_width <= 0:
                cell_width = TERMINAL_FONT * 0.62

        gauge_x = width - PANEL_PAD - GAUGE_WIDTH
        terminal_width = max(0.0, gauge_x - 16.0 - PANEL_PAD)
        text_x = PANEL_PAD + 30.0
        rows_top = PANEL_HEADER
        rows_height = ROWS * line_height

        if intersects(*clip, 0.0, 0.0, width, PANEL_HEADER):
            ctx.fill_rounded_rect(PANEL_PAD, 15.0, 10.0, 10.0, 5.0, pt.rgb(0x9ECE6A))
            ctx.draw_text(
                FONT_FAMILY, 18.0, pt.font_weight_semibold, pt.font_slant_upright,
                PANEL_PAD + 20.0, 32.0, "pt_paint_* dashboard", pt.rgb(0xE6EDF7),
            )
            frame = f"frame {self.frame:03d}"
            frame_width = ctx.measure_text(
                FONT_FAMILY, 12.0, pt.font_weight_regular, pt.font_slant_upright, frame
            )
            ctx.draw_text(
                FONT_FAMILY, 12.0, pt.font_weight_regular, pt.font_slant_upright,
                width - PANEL_PAD - frame_width, 30.0, frame, pt.rgb(0x6F7C8F),
            )
            ctx.draw_line(
                PANEL_PAD, PANEL_HEADER - 1.0, width - PANEL_PAD, PANEL_HEADER - 1.0,
                1.0, pt.rgb(0x24303F),
            )

        if intersects(*clip, PANEL_PAD, rows_top, terminal_width, rows_height):
            with clipped(ctx, PANEL_PAD, rows_top, terminal_width, rows_height):
                for row, text in enumerate(self.lines):
                    row_y = rows_top + row * line_height
                    if row == ROWS - 1:
                        ctx.fill_rect(
                            PANEL_PAD, row_y, terminal_width, line_height, pt.rgb(0x172231)
                        )
                    elif row % 2 == 0:
                        ctx.fill_rect(
                            PANEL_PAD, row_y, terminal_width, line_height, pt.rgb(0x0E141D)
                        )
                    number = f"{row + 1:02d}"
                    ctx.draw_text(
                        FONT_FAMILY, 11.0, pt.font_weight_regular,
                        pt.font_slant_upright, PANEL_PAD + 6.0, row_y + ascent,
                        number, pt.rgb(0x56657A),
                    )
                    ctx.draw_text(
                        FONT_FAMILY, TERMINAL_FONT, pt.font_weight_regular,
                        pt.font_slant_upright, text_x, row_y + ascent, text,
                        pt.rgb(0xD7E2F2) if row == ROWS - 1 else pt.rgb(0xA9B7C9),
                    )
                ctx.draw_line(
                    PANEL_PAD + 26.0, rows_top + 4.0, PANEL_PAD + 26.0,
                    rows_top + rows_height - 4.0, 1.0, pt.rgb(0x263241),
                )
                if self.caret_on:
                    last = self.lines[-1]
                    caret_x = text_x + len(last) * cell_width + 1.0
                    ctx.fill_rect(
                        caret_x,
                        rows_top + (ROWS - 1) * line_height + 3.0,
                        max(2.0, cell_width * 0.5),
                        max(6.0, line_height - 6.0),
                        pt.rgb(0x9ECE6A),
                    )

        if intersects(*clip, gauge_x, rows_top, GAUGE_WIDTH, rows_height):
            with clipped(ctx, gauge_x, rows_top, GAUGE_WIDTH, rows_height):
                ctx.fill_rounded_rect(
                    gauge_x, rows_top, GAUGE_WIDTH, rows_height, 12.0, pt.rgb(0x111823)
                )
                ctx.stroke_rect(
                    gauge_x + 0.5, rows_top + 0.5, GAUGE_WIDTH - 1.0,
                    rows_height - 1.0, 1.0, pt.rgb(0x263241),
                )
                ctx.draw_text(
                    FONT_FAMILY, 11.0, pt.font_weight_semibold,
                    pt.font_slant_upright, gauge_x + 14.0, rows_top + 24.0, "LOAD",
                    pt.rgb(0x7A8798),
                )
                bar_x = gauge_x + 14.0
                bar_y = rows_top + 34.0
                bar_width = GAUGE_WIDTH - 28.0
                bar_height = max(24.0, rows_height - 86.0)
                ctx.fill_rounded_rect(
                    bar_x, bar_y, bar_width, bar_height, 6.0, pt.rgb(0x1B2432)
                )
                fill = bar_height * min(max(self.load, 0.0), 1.0)
                bar_color = (
                    pt.rgb(0xF7768E)
                    if self.load > 0.8
                    else pt.rgb(0xE0AF68)
                    if self.load > 0.55
                    else pt.rgb(0x9ECE6A)
                )
                if fill > 0:
                    ctx.fill_rounded_rect(
                        bar_x, bar_y + bar_height - fill, bar_width, fill, 6.0, bar_color
                    )
                for step in range(1, 4):
                    grid_y = bar_y + bar_height * step / 4.0
                    ctx.draw_line(
                        bar_x + 4.0, grid_y, bar_x + bar_width - 4.0, grid_y, 1.0,
                        0xFFFFFF1C,
                    )
                percent = f"{self.load * 100:.0f}%"
                percent_width = ctx.measure_text(
                    FONT_FAMILY, 13.0, pt.font_weight_medium,
                    pt.font_slant_upright, percent,
                )
                ctx.draw_text(
                    FONT_FAMILY, 13.0, pt.font_weight_medium, pt.font_slant_upright,
                    gauge_x + (GAUGE_WIDTH - percent_width) * 0.5,
                    rows_top + rows_height - 14.0, percent, pt.rgb(0xD7E2F2),
                )

        footer_top = height - PANEL_FOOTER
        if footer_top > rows_top + rows_height + 4.0 and intersects(
            *clip, 0.0, footer_top, width, PANEL_FOOTER
        ):
            ctx.draw_line(
                PANEL_PAD, footer_top, width - PANEL_PAD, footer_top, 1.0,
                pt.rgb(0x24303F),
            )
            with clipped(
                ctx, PANEL_PAD, footer_top + 1.0, width - 2 * PANEL_PAD, PANEL_FOOTER - 2.0
            ):
                marquee = "pure · idempotent · clip-balanced · damage-aware · "
                marquee_width = ctx.measure_text(
                    FONT_FAMILY, 12.0, pt.font_weight_regular,
                    pt.font_slant_upright, marquee,
                )
                if marquee_width > 0:
                    offset = math.fmod(self.frame * 22.0, marquee_width)
                    ctx.draw_text(
                        FONT_FAMILY, 12.0, pt.font_weight_regular,
                        pt.font_slant_upright, PANEL_PAD - offset, footer_top + 21.0,
                        marquee, pt.rgb(0x6F7C8F),
                    )
                    ctx.draw_text(
                        FONT_FAMILY, 12.0, pt.font_weight_regular,
                        pt.font_slant_upright,
                        PANEL_PAD - offset + marquee_width, footer_top + 21.0,
                        marquee, pt.rgb(0x6F7C8F),
                    )


def snapshot_hash(window: pt.Window, width: int, height: int) -> int:
    pixels = pt.paint_window_pixels(window, width, height)
    value = 2166136261
    for pixel in pixels:
        value = ((value ^ pixel) * 16777619) & 0xFFFFFFFF
    return value


def main() -> int:
    app = pt.App("pt.example.custom_paint.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x9ECE6A)

    window = pt.Window(app, "Pt Custom Paint - Python", 900, 580)
    state = Dashboard()
    state.window = window
    root = window.root

    root.set_padding(22, 22, 22, 22).set_gap(12)
    pt.label(root, "Custom paint").set_font_size(26).set_font_weight(
        pt.font_weight_semibold
    )
    pt.label(
        root,
        "A terminal-like panel painted with pt_paint_* through ctypes. Tiles and "
        "repeats cannot change the result.",
    ).set_font_size(13).set_foreground(pt.rgb(0x9AA7B8)).set_wrap(True)

    state.panel = pt.box(root, pt.direction_column)
    state.panel.set_height(300).set_min_height(260).set_background(
        pt.rgb(0x0B1017)
    ).set_radius(14).set_border_width(1).set_border_color(pt.rgb(0x24303F))
    state.panel.on_paint(state.paint)
    state.panel.a11y_role = pt.a11y_role_panel
    state.panel.a11y_name = "Custom paint dashboard"
    state.panel.a11y_description = (
        "A terminal log, a load gauge and a marquee drawn by a pure paint callback."
    )

    controls = pt.row(root).set_gap(10).set_align_self(pt.align_center)
    pt.label(controls, "Load").set_font_size(13).set_foreground(pt.rgb(0x9AA7B8))
    slider = pt.slider(controls).set_grow(1).set_value(state.load)
    slider.on_value_change(state.set_load)
    slider.set_tooltip("Damages only the gauge column through request_redraw_rect")
    pt.button(controls, "Append line").set_button_variant(
        pt.button_primary
    ).set_tooltip(
        "Shift the log, mutate state and request a redraw"
    ).on_click(
        state.append
    )
    pt.button(controls, "Reset log").set_button_variant(pt.button_secondary).on_click(
        state.reset
    )

    state.status = pt.label(
        root, "Paint callback active; append a line or move the load."
    ).set_font_size(12).set_foreground(pt.rgb(0x9ECE6A)).set_wrap(True)

    pt.label(
        root,
        "The callback never mutates the tree. Clicks and the caret timer own all "
        "state changes, then ask for damage; a context manager keeps every clip "
        "push paired.",
    ).set_font_size(12).set_foreground(pt.rgb(0x6F7C8F)).set_wrap(True)

    first = snapshot_hash(window, window.width, window.height)
    second = snapshot_hash(window, window.width, window.height)
    print(
        f"custom_paint (Python): offscreen {window.width}x{window.height} "
        f"hash 0x{first:08X}, repeat "
        f"{'identical' if first == second else 'CHANGED'}",
        flush=True,
    )

    def caret_tick() -> None:
        state.caret_on = not state.caret_on
        assert state.panel is not None
        state.panel.request_redraw()
        app.add_timeout(530, caret_tick)

    window.show()
    app.add_timeout(530, caret_tick)
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
