/*
 * Pt primary selection example - Java bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Drag-select the left pane with Pt.window_set_pointer_handler, publish from
 * the button-release upcall where the event serial is current, then
 * middle-click the right pane to read the primary selection back. Paint
 * callbacks follow the purity and clip-balance rules of the C example.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;

import pt.Pt;

public final class PrimarySelection {
    private PrimarySelection() {}

    private static final float PANE_PAD = 16.0f;
    private static final float PANE_HEADER = 44.0f;
    private static final float PANE_TOP_GAP = 8.0f;
    private static final float PANE_FONT = 13.0f;
    private static final float PANE_LINE = 20.0f;
    private static final float PANE_GUTTER = 30.0f;
    private static final String FONT_FAMILY = "monospace";
    private static final int TARGET_LINE_COUNT = 9;

    private static final int BUTTON_LEFT = 0x110;
    private static final int BUTTON_MIDDLE = 0x112;

    private static final String[] SOURCE_LINES = {
        "Primary selection is the middle-click clipboard.",
        "Drag across these lines to select a range.",
        "On button release the range is published",
        "from inside the same pointer callback.",
        "The compositor serial therefore belongs",
        "to the release that produced the selection.",
        "",
        "Middle-click the paste pane to read it back.",
    };

    private static boolean inside(MemorySegment widget, float x, float y) {
        float[] bounds = Pt.widget_get_bounds(widget);
        return x >= bounds[0] && x < bounds[0] + bounds[2]
                && y >= bounds[1] && y < bounds[1] + bounds[3];
    }

    private static int positionCompare(int lineA, int columnA, int lineB, int columnB) {
        if (lineA != lineB) {
            return lineA < lineB ? -1 : 1;
        }
        if (columnA != columnB) {
            return columnA < columnB ? -1 : 1;
        }
        return 0;
    }

    private static boolean positionsEqual(int lineA, int columnA, int lineB, int columnB) {
        return lineA == lineB && columnA == columnB;
    }

    private static final class Selection {
        MemorySegment window;
        MemorySegment source;
        MemorySegment target;
        MemorySegment status;
        int anchorLine;
        int anchorColumn;
        int headLine;
        int headColumn;
        boolean dragging;
        boolean hasSelection;
        final ArrayList<String> targetLines = new ArrayList<>();
        int highlight = -1;
        float ascent = PANE_FONT;
        float lineHeight = PANE_LINE;
        float cellWidth = PANE_FONT * 0.62f;
        boolean gridValid;
        int publishes;
        int pastes;

        void setStatus(String text) {
            Pt.widget_set_text(status, text);
        }

        void positionAt(float windowX, float windowY, int[] out) {
            float lineHeightNow = gridValid ? lineHeight : PANE_LINE;
            float cellWidthNow = gridValid ? cellWidth : PANE_FONT * 0.62f;
            float[] bounds = Pt.widget_get_bounds(source);
            float top = bounds[1] + PANE_HEADER + PANE_TOP_GAP;
            float left = bounds[0] + PANE_PAD + PANE_GUTTER;
            int line = (int) Math.floor((windowY - top) / lineHeightNow);
            line = Math.min(Math.max(line, 0), SOURCE_LINES.length - 1);
            int length = SOURCE_LINES[line].length();
            int column = Math.round((windowX - left) / cellWidthNow);
            column = Math.min(Math.max(column, 0), length);
            out[0] = line;
            out[1] = column;
        }

        /** Returns {from, to} for a highlighted line, or null. */
        int[] selectionOnLine(int line, int length) {
            if (!hasSelection) {
                return null;
            }
            int startLine = anchorLine;
            int startColumn = anchorColumn;
            int endLine = headLine;
            int endColumn = headColumn;
            if (positionCompare(startLine, startColumn, endLine, endColumn) > 0) {
                int swapLine = startLine;
                int swapColumn = startColumn;
                startLine = endLine;
                startColumn = endColumn;
                endLine = swapLine;
                endColumn = swapColumn;
            }
            if (line < startLine || line > endLine) {
                return null;
            }
            int from = line == startLine ? startColumn : 0;
            int to = line == endLine ? endColumn : length;
            from = Math.min(Math.max(from, 0), length);
            to = Math.min(Math.max(to, 0), length);
            if (to < from) {
                int swap = from;
                from = to;
                to = swap;
            }
            return to == from ? null : new int[] {from, to};
        }

        int selectionLineCount() {
            int startLine = anchorLine;
            int startColumn = anchorColumn;
            int endLine = headLine;
            int endColumn = headColumn;
            if (positionCompare(startLine, startColumn, endLine, endColumn) > 0) {
                int swapLine = startLine;
                int swapColumn = startColumn;
                startLine = endLine;
                startColumn = endColumn;
                endLine = swapLine;
                endColumn = swapColumn;
            }
            return endLine - startLine + 1;
        }

        String selectionText() {
            int startLine = anchorLine;
            int startColumn = anchorColumn;
            int endLine = headLine;
            int endColumn = headColumn;
            if (positionCompare(startLine, startColumn, endLine, endColumn) > 0) {
                int swapLine = startLine;
                int swapColumn = startColumn;
                startLine = endLine;
                startColumn = endColumn;
                endLine = swapLine;
                endColumn = swapColumn;
            }
            StringBuilder text = new StringBuilder();
            for (int line = startLine; line <= endLine; line++) {
                int length = SOURCE_LINES[line].length();
                int from = line == startLine ? startColumn : 0;
                int to = line == endLine ? endColumn : length;
                from = Math.min(Math.max(from, 0), length);
                to = Math.min(Math.max(to, 0), length);
                if (to < from) {
                    int swap = from;
                    from = to;
                    to = swap;
                }
                if (line > startLine) {
                    text.append('\n');
                }
                text.append(SOURCE_LINES[line], from, to);
            }
            return text.toString();
        }

        void readGrid(MemorySegment context) {
            Pt.FontMetrics metrics = Pt.paint_font_metrics(context, FONT_FAMILY, PANE_FONT,
                    Pt.font_weight_regular, Pt.font_slant_upright);
            if (metrics != null) {
                if (metrics.ascent() > 0.0f) {
                    ascent = metrics.ascent();
                }
                if (metrics.line_height() > 0.0f) {
                    lineHeight = Math.max(metrics.line_height(), PANE_FONT + 2.0f);
                }
                if (metrics.cell_width() > 0.0f) {
                    cellWidth = metrics.cell_width();
                }
            }
            gridValid = true;
        }

        void pastePrimary() {
            String primary = Pt.window_primary_get(window);
            if (primary.isEmpty()) {
                setStatus("Primary is empty. Drag over the source pane first.");
                return;
            }
            int lines = 0;
            for (String line : primary.split("\n", -1)) {
                if (line.isEmpty()) {
                    continue;
                }
                appendTargetLine("> " + line);
                lines += 1;
            }
            pastes += 1;
            highlight = targetLines.size() - 1;
            setStatus("Read " + primary.getBytes(java.nio.charset.StandardCharsets.UTF_8).length
                    + " bytes from primary in this pointer event; appended " + lines
                    + " line(s), paste #" + pastes + ".");
            Pt.widget_request_redraw(target);
        }

        void appendTargetLine(String text) {
            if (targetLines.size() == TARGET_LINE_COUNT) {
                targetLines.remove(0);
            }
            targetLines.add(text.length() > 124 ? text.substring(0, 124) : text);
        }
    }

    private static void paintSource(MemorySegment widget, MemorySegment context,
            MemorySegment data, Selection state) {
        float width = Pt.paint_width(context);
        float height = Pt.paint_height(context);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }
        float[] clip = Pt.paint_clip_rect(context);
        if (clip == null) {
            return;
        }
        state.readGrid(context);

        float top = PANE_HEADER + PANE_TOP_GAP;
        float textX = PANE_PAD + PANE_GUTTER;
        float bodyHeight = Math.max(0.0f, height - top - PANE_PAD);
        Pt.paint_draw_text(context, FONT_FAMILY, 12.0f, Pt.font_weight_semibold,
                Pt.font_slant_upright, PANE_PAD, 27.0f, "PRIMARY SOURCE",
                Pt.rgb(0x7AA2F7));
        String hint = "drag to select";
        float hintWidth = Pt.paint_measure_text(context, FONT_FAMILY, 11.0f,
                Pt.font_weight_regular, Pt.font_slant_upright, hint);
        Pt.paint_draw_text(context, FONT_FAMILY, 11.0f, Pt.font_weight_regular,
                Pt.font_slant_upright, width - PANE_PAD - hintWidth, 26.0f, hint,
                Pt.rgb(0x6F7C8F));
        Pt.paint_draw_line(context, PANE_PAD, PANE_HEADER - 1.0f, width - PANE_PAD,
                PANE_HEADER - 1.0f, 1.0f, Pt.rgb(0x263241));

        Pt.paint_push_clip(context, PANE_PAD, top, width - 2.0f * PANE_PAD, bodyHeight);
        for (int line = 0; line < SOURCE_LINES.length; line++) {
            float rowY = top + line * state.lineHeight;
            int length = SOURCE_LINES[line].length();
            int[] span = state.selectionOnLine(line, length);
            if (span != null) {
                Pt.paint_fill_rect(context, textX + span[0] * state.cellWidth, rowY,
                        (span[1] - span[0]) * state.cellWidth, state.lineHeight,
                        Pt.rgb(0x2B4A78));
            }
            String number = String.format("%02d", line + 1);
            Pt.paint_draw_text(context, FONT_FAMILY, 11.0f, Pt.font_weight_regular,
                    Pt.font_slant_upright, PANE_PAD + 4.0f, rowY + state.ascent, number,
                    Pt.rgb(0x56657A));
            Pt.paint_draw_text(context, FONT_FAMILY, PANE_FONT, Pt.font_weight_regular,
                    Pt.font_slant_upright, textX, rowY + state.ascent, SOURCE_LINES[line],
                    Pt.rgb(0xC3D0E1));
        }
        Pt.paint_draw_line(context, PANE_PAD + 24.0f, top + 4.0f, PANE_PAD + 24.0f,
                top + bodyHeight - 4.0f, 1.0f, Pt.rgb(0x263241));
        Pt.paint_pop_clip(context);
    }

    private static void paintTarget(MemorySegment widget, MemorySegment context,
            MemorySegment data, Selection state) {
        float width = Pt.paint_width(context);
        float height = Pt.paint_height(context);
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }
        float[] clip = Pt.paint_clip_rect(context);
        if (clip == null) {
            return;
        }
        state.readGrid(context);

        float top = PANE_HEADER + PANE_TOP_GAP;
        float bodyHeight = Math.max(0.0f, height - top - PANE_PAD);
        Pt.paint_draw_text(context, FONT_FAMILY, 12.0f, Pt.font_weight_semibold,
                Pt.font_slant_upright, PANE_PAD, 27.0f, "PASTE TARGET",
                Pt.rgb(0x9ECE6A));
        String hint = "middle-click to paste";
        float hintWidth = Pt.paint_measure_text(context, FONT_FAMILY, 11.0f,
                Pt.font_weight_regular, Pt.font_slant_upright, hint);
        Pt.paint_draw_text(context, FONT_FAMILY, 11.0f, Pt.font_weight_regular,
                Pt.font_slant_upright, width - PANE_PAD - hintWidth, 26.0f, hint,
                Pt.rgb(0x6F7C8F));
        Pt.paint_draw_line(context, PANE_PAD, PANE_HEADER - 1.0f, width - PANE_PAD,
                PANE_HEADER - 1.0f, 1.0f, Pt.rgb(0x263241));

        Pt.paint_push_clip(context, PANE_PAD, top, width - 2.0f * PANE_PAD, bodyHeight);
        if (state.targetLines.isEmpty()) {
            String empty = "Middle-click here after selecting text.";
            Pt.paint_draw_text(context, FONT_FAMILY, PANE_FONT, Pt.font_weight_regular,
                    Pt.font_slant_upright, PANE_PAD, top + state.ascent, empty,
                    Pt.rgb(0x6F7C8F));
        } else {
            for (int line = 0; line < state.targetLines.size(); line++) {
                float rowY = top + line * state.lineHeight;
                if (line == state.highlight) {
                    Pt.paint_fill_rect(context, PANE_PAD, rowY,
                            width - 2.0f * PANE_PAD, state.lineHeight, Pt.rgb(0x1D3524));
                }
                Pt.paint_draw_text(context, FONT_FAMILY, PANE_FONT,
                        Pt.font_weight_regular, Pt.font_slant_upright, PANE_PAD,
                        rowY + state.ascent, state.targetLines.get(line),
                        line == state.highlight ? Pt.rgb(0xD7F5DC) : Pt.rgb(0xC3D0E1));
            }
        }
        Pt.paint_pop_clip(context);
    }

    private static boolean onPointer(MemorySegment window, Pt.PointerEvent event,
            MemorySegment data, Selection state) {
        switch (event.phase()) {
            case Pt.pointer_phase_down:
                if (event.button() == BUTTON_LEFT && inside(state.source, event.x(), event.y())) {
                    int[] position = new int[2];
                    state.positionAt(event.x(), event.y(), position);
                    state.anchorLine = position[0];
                    state.anchorColumn = position[1];
                    state.headLine = position[0];
                    state.headColumn = position[1];
                    state.dragging = true;
                    state.hasSelection = false;
                    state.setStatus("Selecting; release inside the window to publish the range.");
                    Pt.widget_request_redraw(state.source);
                    return true;
                }
                if (event.button() == BUTTON_MIDDLE
                        && inside(state.target, event.x(), event.y())) {
                    state.pastePrimary();
                    return true;
                }
                break;
            case Pt.pointer_phase_motion:
                if (state.dragging) {
                    int[] next = new int[2];
                    state.positionAt(event.x(), event.y(), next);
                    if (!positionsEqual(next[0], next[1], state.headLine, state.headColumn)) {
                        state.headLine = next[0];
                        state.headColumn = next[1];
                        state.hasSelection = !positionsEqual(state.anchorLine,
                                state.anchorColumn, state.headLine, state.headColumn);
                        Pt.widget_request_redraw(state.source);
                    }
                }
                break;
            case Pt.pointer_phase_up:
                if (state.dragging && event.button() == BUTTON_LEFT) {
                    int[] next = new int[2];
                    state.positionAt(event.x(), event.y(), next);
                    state.headLine = next[0];
                    state.headColumn = next[1];
                    state.dragging = false;
                    state.hasSelection = !positionsEqual(state.anchorLine, state.anchorColumn,
                            state.headLine, state.headColumn);
                    if (!state.hasSelection) {
                        state.setStatus(
                                "Click without drag selects nothing; primary is unchanged.");
                        Pt.widget_request_redraw(state.source);
                        return true;
                    }
                    String payload = state.selectionText();
                    if (payload.isEmpty()) {
                        state.setStatus("Selection contains no text; primary is unchanged.");
                        Pt.widget_request_redraw(state.source);
                        return true;
                    }
                    /* Publish here, not from a timer: the Wayland set_selection
                     * serial belongs to this button release. */
                    boolean published = Pt.window_primary_set(window, payload);
                    String readback = published ? Pt.window_primary_get(window) : "";
                    if (published) {
                        state.publishes += 1;
                        state.setStatus("Published " + payload.length() + " bytes from "
                                + state.selectionLineCount() + " line(s); read back "
                                + readback.length() + " bytes, publish #" + state.publishes
                                + ".");
                    } else {
                        state.setStatus("The primary selection backend rejected the set.");
                    }
                    Pt.widget_request_redraw(state.source);
                    return true;
                }
                break;
            case Pt.pointer_phase_cancel:
                if (state.dragging) {
                    state.dragging = false;
                    state.setStatus("Selection cancelled; primary is unchanged.");
                    Pt.widget_request_redraw(state.source);
                }
                break;
            default:
                break;
        }
        return false;
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.primary_selection.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));

            MemorySegment window =
                    Pt.window_create(app, "Pt Primary Selection - Java", 980, 640);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            Selection state = new Selection();
            state.window = window;
            Pt.window_set_pointer_handler(window,
                    (target, event, data) -> onPointer(target, event, data, state), arena);

            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 22.0f, 22.0f, 22.0f, 22.0f);
            Pt.widget_set_gap(root, 12.0f);

            MemorySegment title = Pt.label(root, "Primary selection");
            Pt.widget_set_font_size(title, 26.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_semibold);
            MemorySegment subtitle = Pt.label(root,
                    "Drag over the left pane, release, then middle-click the right pane. "
                            + "The set happens inside the button-release callback, so the "
                            + "serial belongs to the event that produced the selection.");
            Pt.widget_set_font_size(subtitle, 13.0f);
            Pt.widget_set_foreground(subtitle, Pt.rgb(0x9AA7B8));
            Pt.widget_set_wrap(subtitle, true);

            MemorySegment panes = Pt.row(root);
            Pt.widget_set_gap(panes, 14.0f);
            Pt.widget_set_grow(panes, 1.0f);

            state.source = Pt.box(panes, Pt.direction_column);
            Pt.widget_set_grow(state.source, 1.0f);
            Pt.widget_set_min_height(state.source, 300.0f);
            Pt.widget_set_background(state.source, Pt.rgb(0x0D131C));
            Pt.widget_set_radius(state.source, 14.0f);
            Pt.widget_set_border_width(state.source, 1.0f);
            Pt.widget_set_border_color(state.source, Pt.rgb(0x263241));
            Pt.widget_set_paint_callback(state.source,
                    (widget, context, data) -> paintSource(widget, context, data, state),
                    arena);
            Pt.widget_set_a11y_role(state.source, Pt.a11y_role_text);
            Pt.widget_set_a11y_name(state.source, "Primary selection source");
            Pt.widget_set_a11y_description(state.source,
                    "Eight log lines. Drag across them to select and publish a primary selection.");

            state.target = Pt.box(panes, Pt.direction_column);
            Pt.widget_set_grow(state.target, 1.0f);
            Pt.widget_set_min_height(state.target, 300.0f);
            Pt.widget_set_background(state.target, Pt.rgb(0x0D131C));
            Pt.widget_set_radius(state.target, 14.0f);
            Pt.widget_set_border_width(state.target, 1.0f);
            Pt.widget_set_border_color(state.target, Pt.rgb(0x263241));
            Pt.widget_set_paint_callback(state.target,
                    (widget, context, data) -> paintTarget(widget, context, data, state),
                    arena);
            Pt.widget_set_a11y_role(state.target, Pt.a11y_role_text_frame);
            Pt.widget_set_a11y_name(state.target, "Primary paste target");
            Pt.widget_set_a11y_description(state.target,
                    "Middle-click to append the current primary selection.");

            MemorySegment actions = Pt.row(root);
            Pt.widget_set_gap(actions, 10.0f);
            Pt.widget_set_align_self(actions, Pt.align_center);
            MemorySegment read = Pt.button(actions, "Read primary");
            Pt.button_set_variant(read, Pt.button_secondary);
            Pt.button_set_on_click(read, (widget, data) -> {
                String primary = Pt.window_primary_get(state.window);
                state.setStatus(primary.isEmpty() ? "Primary is empty."
                        : "Primary holds " + primary.length() + " characters.");
            }, arena);
            Pt.widget_set_tooltip(read,
                    "Calls window_primary_get(), which copies and frees the native buffer");
            MemorySegment clear = Pt.button(actions, "Clear paste target");
            Pt.button_set_variant(clear, Pt.button_ghost);
            Pt.button_set_on_click(clear, (widget, data) -> {
                state.targetLines.clear();
                state.highlight = -1;
                state.setStatus("Paste target cleared; primary is untouched.");
                Pt.widget_request_redraw(state.target);
            }, arena);

            state.status = Pt.label(actions,
                    "Drag across the source lines to make a selection.");
            Pt.widget_set_font_size(state.status, 12.0f);
            Pt.widget_set_foreground(state.status, Pt.rgb(0x9ECE6A));
            Pt.widget_set_wrap(state.status, true);
            Pt.widget_set_grow(state.status, 1.0f);

            MemorySegment footer = Pt.label(root,
                    "window_primary_set() copies the string; window_primary_get() returns "
                            + "caller-owned UTF-8 that the binding copies and frees. Without "
                            + "wp_primary_selection_unstable_v1 (or under PT_HEADLESS) both "
                            + "sides fall back to the in-process buffer, so the example still "
                            + "runs.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));
            Pt.widget_set_wrap(footer, true);

            Pt.window_show(window);
            Pt.app_run(app);
            Pt.app_destroy(app);
        }
    }
}