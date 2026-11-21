/*
 * Pt custom paint dashboard - Java bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The Java binding exposes the same borrowed paint context as the C API. The
 * callback is a pure function of Dashboard state, clip pushes are paired with
 * pops, and input callbacks request damage instead of changing the tree from
 * inside paint. An offscreen snapshot hashes the same state twice to show the
 * pass is idempotent.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import pt.Pt;

public final class CustomPaint {
    private CustomPaint() {}

    private static final float PANEL_PAD = 18.0f;
    private static final float PANEL_HEADER = 48.0f;
    private static final float PANEL_FOOTER = 34.0f;
    private static final float TERMINAL_FONT = 13.0f;
    private static final float GAUGE_WIDTH = 132.0f;
    private static final String FONT_FAMILY = "monospace";
    private static final int ROWS = 8;

    private static final String[] INITIAL_LINES = {
        "pt_paint_* draws into a borrowed paint context.",
        "Coordinates are widget-local logical pixels.",
        "Every push_clip has exactly one pop_clip.",
        "Paint is pure: state in, pixels out, no counters.",
        "One damage tile may be painted 0..N times.",
        "Caret and gauge state live outside the callback.",
        "Callbacks mutate state, then request_redraw().",
        "request_redraw_rect() damages only the gauge.",
    };

    private static boolean intersects(float[] clip, float x, float y, float width,
            float height) {
        return clip[0] < x + width && clip[0] + clip[2] > x
                && clip[1] < y + height && clip[1] + clip[3] > y;
    }

    /** All mutable state lives here; paint only reads it. */
    private static final class Dashboard {
        MemorySegment window;
        MemorySegment panel;
        MemorySegment status;
        float load = 0.42f;
        int frame = 0;
        int appended = 0;
        boolean caretOn = true;
        final String[] lines = INITIAL_LINES.clone();

        void reset() {
            frame = 0;
            appended = 0;
            System.arraycopy(INITIAL_LINES, 0, lines, 0, INITIAL_LINES.length);
            Pt.widget_set_text(status, "Log reset; the paint callback re-reads state.");
            Pt.widget_request_redraw(panel);
        }

        void append() {
            appended += 1;
            frame += 1;
            for (int row = 1; row < ROWS; row++) {
                lines[row - 1] = lines[row];
            }
            lines[ROWS - 1] = "[" + String.format("%03d", appended)
                    + "] appended in on_click; redraw is deferred";
            Pt.widget_set_text(status,
                    "Line " + appended + " appended; requested a widget repaint.");
            Pt.widget_request_redraw(panel);
        }

        void setLoad(float value) {
            load = Math.min(Math.max(value, 0.0f), 1.0f);
            float[] bounds = Pt.widget_get_bounds(panel);
            float gaugeWidth = Math.min(GAUGE_WIDTH + 24.0f, bounds[2]);
            Pt.widget_request_redraw_rect(panel, bounds[2] - gaugeWidth - PANEL_PAD,
                    PANEL_HEADER, gaugeWidth,
                    Math.max(0.0f, bounds[3] - PANEL_HEADER - PANEL_FOOTER));
            if (status.address() != 0) {
                Pt.widget_set_text(status,
                        "Load " + Math.round(load * 100.0f)
                                + "%; damaged only the gauge column.");
            }
        }

        void paint(MemorySegment widget, MemorySegment context, MemorySegment data) {
            float width = Pt.paint_width(context);
            float height = Pt.paint_height(context);
            if (width <= 0.0f || height <= 0.0f) {
                return;
            }
            float[] clip = Pt.paint_clip_rect(context);
            if (clip == null) {
                return;
            }

            float ascent = TERMINAL_FONT;
            float lineHeight = TERMINAL_FONT * 1.45f;
            float cellWidth = TERMINAL_FONT * 0.62f;
            Pt.FontMetrics metrics = Pt.paint_font_metrics(context, FONT_FAMILY,
                    TERMINAL_FONT, Pt.font_weight_regular, Pt.font_slant_upright);
            if (metrics != null) {
                ascent = metrics.ascent();
                lineHeight = Math.max(metrics.line_height(), TERMINAL_FONT + 7.0f);
                if (metrics.cell_width() > 0.0f) {
                    cellWidth = metrics.cell_width();
                }
            }

            float gaugeX = width - PANEL_PAD - GAUGE_WIDTH;
            float terminalWidth = Math.max(0.0f, gaugeX - 16.0f - PANEL_PAD);
            float textX = PANEL_PAD + 30.0f;
            float rowsTop = PANEL_HEADER;
            float rowsHeight = ROWS * lineHeight;

            if (intersects(clip, 0.0f, 0.0f, width, PANEL_HEADER)) {
                Pt.paint_fill_rounded_rect(context, PANEL_PAD, 15.0f, 10.0f, 10.0f,
                        5.0f, Pt.rgb(0x9ECE6A));
                Pt.paint_draw_text(context, FONT_FAMILY, 18.0f,
                        Pt.font_weight_semibold, Pt.font_slant_upright, PANEL_PAD + 20.0f,
                        32.0f, "pt_paint_* dashboard", Pt.rgb(0xE6EDF7));
                String frameText = "frame " + String.format("%03d", frame);
                float frameWidth = Pt.paint_measure_text(context, FONT_FAMILY, 12.0f,
                        Pt.font_weight_regular, Pt.font_slant_upright, frameText);
                Pt.paint_draw_text(context, FONT_FAMILY, 12.0f,
                        Pt.font_weight_regular, Pt.font_slant_upright,
                        width - PANEL_PAD - frameWidth, 30.0f, frameText, Pt.rgb(0x6F7C8F));
                Pt.paint_draw_line(context, PANEL_PAD, PANEL_HEADER - 1.0f,
                        width - PANEL_PAD, PANEL_HEADER - 1.0f, 1.0f, Pt.rgb(0x24303F));
            }

            if (intersects(clip, PANEL_PAD, rowsTop, terminalWidth, rowsHeight)) {
                Pt.paint_push_clip(context, PANEL_PAD, rowsTop, terminalWidth, rowsHeight);
                for (int row = 0; row < ROWS; row++) {
                    float rowY = rowsTop + row * lineHeight;
                    if (row == ROWS - 1) {
                        Pt.paint_fill_rect(context, PANEL_PAD, rowY, terminalWidth,
                                lineHeight, Pt.rgb(0x172231));
                    } else if (row % 2 == 0) {
                        Pt.paint_fill_rect(context, PANEL_PAD, rowY, terminalWidth,
                                lineHeight, Pt.rgb(0x0E141D));
                    }
                    String number = String.format("%02d", row + 1);
                    Pt.paint_draw_text(context, FONT_FAMILY, 11.0f,
                            Pt.font_weight_regular, Pt.font_slant_upright, PANEL_PAD + 6.0f,
                            rowY + ascent, number, Pt.rgb(0x56657A));
                    Pt.paint_draw_text(context, FONT_FAMILY, TERMINAL_FONT,
                            Pt.font_weight_regular, Pt.font_slant_upright, textX,
                            rowY + ascent, lines[row],
                            row == ROWS - 1 ? Pt.rgb(0xD7E2F2) : Pt.rgb(0xA9B7C9));
                }
                Pt.paint_draw_line(context, PANEL_PAD + 26.0f, rowsTop + 4.0f,
                        PANEL_PAD + 26.0f, rowsTop + rowsHeight - 4.0f, 1.0f,
                        Pt.rgb(0x263241));
                if (caretOn) {
                    String last = lines[ROWS - 1];
                    float caretX = textX + last.length() * cellWidth + 1.0f;
                    Pt.paint_fill_rect(context, caretX,
                            rowsTop + (ROWS - 1) * lineHeight + 3.0f,
                            Math.max(2.0f, cellWidth * 0.5f),
                            Math.max(6.0f, lineHeight - 6.0f), Pt.rgb(0x9ECE6A));
                }
                Pt.paint_pop_clip(context);
            }

            if (intersects(clip, gaugeX, rowsTop, GAUGE_WIDTH, rowsHeight)) {
                Pt.paint_push_clip(context, gaugeX, rowsTop, GAUGE_WIDTH, rowsHeight);
                Pt.paint_fill_rounded_rect(context, gaugeX, rowsTop, GAUGE_WIDTH,
                        rowsHeight, 12.0f, Pt.rgb(0x111823));
                Pt.paint_stroke_rect(context, gaugeX + 0.5f, rowsTop + 0.5f,
                        GAUGE_WIDTH - 1.0f, rowsHeight - 1.0f, 1.0f, Pt.rgb(0x263241));
                Pt.paint_draw_text(context, FONT_FAMILY, 11.0f,
                        Pt.font_weight_semibold, Pt.font_slant_upright, gaugeX + 14.0f,
                        rowsTop + 24.0f, "LOAD", Pt.rgb(0x7A8798));
                float barX = gaugeX + 14.0f;
                float barY = rowsTop + 34.0f;
                float barWidth = GAUGE_WIDTH - 28.0f;
                float barHeight = Math.max(24.0f, rowsHeight - 86.0f);
                Pt.paint_fill_rounded_rect(context, barX, barY, barWidth, barHeight,
                        6.0f, Pt.rgb(0x1B2432));
                float fill = barHeight * Math.min(Math.max(load, 0.0f), 1.0f);
                int barColor = load > 0.8f ? Pt.rgb(0xF7768E)
                        : load > 0.55f ? Pt.rgb(0xE0AF68) : Pt.rgb(0x9ECE6A);
                if (fill > 0.0f) {
                    Pt.paint_fill_rounded_rect(context, barX, barY + barHeight - fill,
                            barWidth, fill, 6.0f, barColor);
                }
                for (int step = 1; step < 4; step++) {
                    float gridY = barY + barHeight * step / 4.0f;
                    Pt.paint_draw_line(context, barX + 4.0f, gridY,
                            barX + barWidth - 4.0f, gridY, 1.0f, 0xFFFFFF1C);
                }
                String percent = Math.round(load * 100.0f) + "%";
                float percentWidth = Pt.paint_measure_text(context, FONT_FAMILY, 13.0f,
                        Pt.font_weight_medium, Pt.font_slant_upright, percent);
                Pt.paint_draw_text(context, FONT_FAMILY, 13.0f,
                        Pt.font_weight_medium, Pt.font_slant_upright,
                        gaugeX + (GAUGE_WIDTH - percentWidth) * 0.5f,
                        rowsTop + rowsHeight - 14.0f, percent, Pt.rgb(0xD7E2F2));
                Pt.paint_pop_clip(context);
            }

            float footerTop = height - PANEL_FOOTER;
            if (footerTop > rowsTop + rowsHeight + 4.0f
                    && intersects(clip, 0.0f, footerTop, width, PANEL_FOOTER)) {
                Pt.paint_draw_line(context, PANEL_PAD, footerTop, width - PANEL_PAD,
                        footerTop, 1.0f, Pt.rgb(0x24303F));
                Pt.paint_push_clip(context, PANEL_PAD, footerTop + 1.0f,
                        width - 2.0f * PANEL_PAD, PANEL_FOOTER - 2.0f);
                String marquee = "pure · idempotent · clip-balanced · damage-aware · ";
                float marqueeWidth = Pt.paint_measure_text(context, FONT_FAMILY, 12.0f,
                        Pt.font_weight_regular, Pt.font_slant_upright, marquee);
                if (marqueeWidth > 0.0f) {
                    float offset = (frame * 22.0f) % marqueeWidth;
                    Pt.paint_draw_text(context, FONT_FAMILY, 12.0f,
                            Pt.font_weight_regular, Pt.font_slant_upright,
                            PANEL_PAD - offset, footerTop + 21.0f, marquee,
                            Pt.rgb(0x6F7C8F));
                    Pt.paint_draw_text(context, FONT_FAMILY, 12.0f,
                            Pt.font_weight_regular, Pt.font_slant_upright,
                            PANEL_PAD - offset + marqueeWidth, footerTop + 21.0f, marquee,
                            Pt.rgb(0x6F7C8F));
                }
                Pt.paint_pop_clip(context);
            }
        }
    }

    private static int snapshotHash(MemorySegment window) {
        int width = Pt.window_get_width(window);
        int height = Pt.window_get_height(window);
        if (width <= 0 || height <= 0) {
            return 0;
        }
        int[] pixels = new int[width * height];
        if (!Pt.window_paint_pixels(window, pixels, width, height)) {
            return 0;
        }
        int hash = 0x811C9DC5;
        for (int pixel : pixels) {
            hash ^= pixel;
            hash *= 0x01000193;
        }
        return hash;
    }

    private static void scheduleCaret(MemorySegment app, Dashboard state, Arena arena) {
        Pt.app_add_timeout(app, 530, (target, data) -> {
            state.caretOn = !state.caretOn;
            Pt.widget_request_redraw(state.panel);
            scheduleCaret(target, state, arena);
        }, arena);
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.custom_paint.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x9ECE6A));

            MemorySegment window =
                    Pt.window_create(app, "Pt Custom Paint - Java", 900, 580);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            Dashboard state = new Dashboard();
            state.window = window;
            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 22.0f, 22.0f, 22.0f, 22.0f);
            Pt.widget_set_gap(root, 12.0f);

            MemorySegment title = Pt.label(root, "Custom paint");
            Pt.widget_set_font_size(title, 26.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_semibold);
            MemorySegment subtitle = Pt.label(root,
                    "A terminal-like panel painted with pt_paint_* through FFM. "
                            + "Tiles and repeats cannot change the result.");
            Pt.widget_set_font_size(subtitle, 13.0f);
            Pt.widget_set_foreground(subtitle, Pt.rgb(0x9AA7B8));
            Pt.widget_set_wrap(subtitle, true);

            state.panel = Pt.box(root, Pt.direction_column);
            Pt.widget_set_height(state.panel, 300.0f);
            Pt.widget_set_min_height(state.panel, 260.0f);
            Pt.widget_set_background(state.panel, Pt.rgb(0x0B1017));
            Pt.widget_set_radius(state.panel, 14.0f);
            Pt.widget_set_border_width(state.panel, 1.0f);
            Pt.widget_set_border_color(state.panel, Pt.rgb(0x24303F));
            Pt.widget_set_paint_callback(state.panel, state::paint, arena);
            Pt.widget_set_a11y_role(state.panel, Pt.a11y_role_panel);
            Pt.widget_set_a11y_name(state.panel, "Custom paint dashboard");
            Pt.widget_set_a11y_description(state.panel,
                    "A terminal log, a load gauge and a marquee drawn by a pure paint callback.");

            MemorySegment controls = Pt.row(root);
            Pt.widget_set_gap(controls, 10.0f);
            Pt.widget_set_align_self(controls, Pt.align_center);
            MemorySegment loadLabel = Pt.label(controls, "Load");
            Pt.widget_set_font_size(loadLabel, 13.0f);
            Pt.widget_set_foreground(loadLabel, Pt.rgb(0x9AA7B8));
            MemorySegment slider = Pt.slider(controls);
            Pt.widget_set_grow(slider, 1.0f);
            Pt.slider_set_value(slider, state.load);
            Pt.slider_set_on_change(slider,
                    (widget, value, data) -> state.setLoad(value), arena);
            Pt.widget_set_tooltip(slider,
                    "Damages only the gauge column through request_redraw_rect");
            MemorySegment append = Pt.button(controls, "Append line");
            Pt.button_set_variant(append, Pt.button_primary);
            Pt.button_set_on_click(append, (widget, data) -> state.append(), arena);
            Pt.widget_set_tooltip(append,
                    "Shift the log, mutate state and request a redraw");
            MemorySegment reset = Pt.button(controls, "Reset log");
            Pt.button_set_variant(reset, Pt.button_secondary);
            Pt.button_set_on_click(reset, (widget, data) -> state.reset(), arena);

            state.status = Pt.label(root,
                    "Paint callback active; append a line or move the load.");
            Pt.widget_set_font_size(state.status, 12.0f);
            Pt.widget_set_foreground(state.status, Pt.rgb(0x9ECE6A));
            Pt.widget_set_wrap(state.status, true);

            MemorySegment footer = Pt.label(root,
                    "The callback never mutates the tree. Clicks and the caret timer own "
                            + "all state changes, then ask for damage; every clip push is "
                            + "paired with a pop.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));
            Pt.widget_set_wrap(footer, true);

            int first = snapshotHash(window);
            int second = snapshotHash(window);
            System.out.printf("custom_paint (Java): offscreen %dx%d hash 0x%08X, repeat %s%n",
                    Pt.window_get_width(window), Pt.window_get_height(window), first,
                    first == second ? "identical" : "CHANGED");

            Pt.window_show(window);
            scheduleCaret(app, state, arena);
            Pt.app_run(app);
            Pt.app_destroy(app);
        }
    }
}