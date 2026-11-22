/*
 * Pt direction switch - Java 25 Foreign Function & Memory API.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The 2.0 direction functions take the C enum as an int. The getters return the
 * stored explicit value, so pt.ui_direction_inherit (0) and
 * pt.text_direction_auto (0) mean "inherit" rather than a resolved answer.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import pt.Pt;

public final class RtlDirection {
    private static final String MIXED =
            "שלום مرحبا Hello, 123";
    private static final String RTL_ENTRY = "עבודה work@example.com";
    private static final String RTL_AREA =
            "שלום עולם - Hello world\nHello world - שלום עולם\nمرحبا بالعالم 42";

    private RtlDirection() {}

    private static void apply(MemorySegment app, MemorySegment window, MemorySegment row,
            MemorySegment entry, MemorySegment title, MemorySegment nested,
            MemorySegment status, int[] direction, int value) {
        direction[0] = value;
        Pt.app_set_ui_direction(app, value);
        Pt.window_set_ui_direction(window, value);
        Pt.widget_set_ui_direction(row, value);
        Pt.widget_set_ui_direction(entry, value);
        Pt.widget_set_text_direction(title,
                value == Pt.ui_direction_rtl ? Pt.text_direction_rtl : Pt.text_direction_ltr);
        Pt.widget_set_ui_direction(nested, Pt.ui_direction_ltr);
        Pt.widget_set_text_direction(nested, Pt.text_direction_ltr);
        Pt.widget_set_text(status, value == Pt.ui_direction_rtl ? "RTL shell" : "LTR shell");
        System.out.printf(
                "rtl_direction: app=%d window=%d row=%d entry=%d title_text=%d nested=%d%n",
                Pt.app_get_ui_direction(app), Pt.window_get_ui_direction(window),
                Pt.widget_get_ui_direction(row), Pt.widget_get_ui_direction(entry),
                Pt.widget_get_text_direction(title), Pt.widget_get_ui_direction(nested));
        System.out.flush();
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.rtl_direction.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));

            MemorySegment window = Pt.window_create(app, "Pt Direction - Java", 720, 560);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }
            MemorySegment root = Pt.window_root(window);

            MemorySegment page = Pt.column(root);
            Pt.widget_set_padding(page, 26.0f, 26.0f, 26.0f, 26.0f);
            Pt.widget_set_gap(page, 12.0f);

            MemorySegment eyebrow = Pt.label(page, "JAVA BINDING  ·  DIRECTION");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x7AA2F7));

            final MemorySegment title = Pt.label(page, MIXED);
            Pt.widget_set_font_size(title, 30.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_bold);
            Pt.widget_set_text_direction(title, Pt.text_direction_auto);

            MemorySegment intro = Pt.label(page,
                    "pt_ui_direction mirrors rows and chrome; pt_text_direction selects the "
                            + "paragraph base. auto runs UBA P2/P3, so the heading resolves RTL "
                            + "while the ASCII suffix stays LTR.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            final MemorySegment row = Pt.row(page);
            Pt.widget_set_gap(row, 10.0f);
            Pt.widget_set_ui_direction(row, Pt.ui_direction_rtl);
            Pt.box_set_align(row, Pt.align_center);

            final MemorySegment status = Pt.label(row, "RTL shell");
            Pt.widget_set_grow(status, 1.0f);
            Pt.widget_set_font_size(status, 12.0f);
            Pt.widget_set_foreground(status, Pt.rgb(0x9ECE6A));

            final MemorySegment entry = Pt.entry(page, "Type here");
            Pt.widget_set_text(entry, RTL_ENTRY);
            Pt.widget_set_size(entry, 420.0f, 36.0f);
            Pt.widget_set_ui_direction(entry, Pt.ui_direction_rtl);
            Pt.widget_set_text_direction(entry, Pt.text_direction_auto);
            Pt.widget_set_align_self(entry, Pt.align_start);

            MemorySegment area = Pt.text_area(page, "Type a note");
            Pt.widget_set_text(area, RTL_AREA);
            Pt.widget_set_size(area, 560.0f, 130.0f);
            Pt.widget_set_ui_direction(area, Pt.ui_direction_rtl);
            Pt.widget_set_text_direction(area, Pt.text_direction_auto);

            final MemorySegment nested = Pt.card(page);
            Pt.widget_set_padding(nested, 14.0f, 14.0f, 14.0f, 14.0f);
            Pt.widget_set_gap(nested, 8.0f);
            Pt.widget_set_ui_direction(nested, Pt.ui_direction_ltr);
            Pt.widget_set_text_direction(nested, Pt.text_direction_ltr);
            MemorySegment nestedLabel =
                    Pt.label(nested, "Nested LTR island inside the RTL shell.");
            Pt.widget_set_font_size(nestedLabel, 13.0f);
            Pt.widget_set_wrap(nestedLabel, true);
            Pt.widget_set_foreground(nestedLabel, Pt.rgb(0xBB9AF7));
            MemorySegment nestedEntry = Pt.entry(nested, "code = 1;");
            Pt.widget_set_text(nestedEntry, "int main(void) { return 0; }");
            Pt.widget_set_text_direction(nestedEntry, Pt.text_direction_ltr);
            Pt.widget_set_size(nestedEntry, 340.0f, 34.0f);
            Pt.widget_set_align_self(nestedEntry, Pt.align_start);

            final int[] direction = {Pt.ui_direction_rtl};
            MemorySegment ltr = Pt.button(row, "Force LTR");
            Pt.button_set_variant(ltr, Pt.button_ghost);
            Pt.button_set_on_click(ltr, (widget, data) -> apply(app, window, row, entry, title,
                    nested, status, direction, Pt.ui_direction_ltr), arena);
            MemorySegment rtl = Pt.button(row, "Force RTL");
            Pt.button_set_variant(rtl, Pt.button_primary);
            Pt.button_set_on_click(rtl, (widget, data) -> apply(app, window, row, entry, title,
                    nested, status, direction, Pt.ui_direction_rtl), arena);

            Pt.spacer(page);
            MemorySegment quit = Pt.button(page, "Quit");
            Pt.button_set_variant(quit, Pt.button_ghost);
            Pt.widget_set_align_self(quit, Pt.align_start);
            Pt.button_set_on_click(quit, (widget, data) -> Pt.window_close(window), arena);

            apply(app, window, row, entry, title, nested, status, direction, Pt.ui_direction_rtl);
            Pt.window_show(window);

            String headless = System.getenv("PT_HEADLESS");
            int result;
            if (headless != null && !headless.isEmpty() && !headless.equals("0")) {
                apply(app, window, row, entry, title, nested, status, direction,
                        Pt.ui_direction_ltr);
                apply(app, window, row, entry, title, nested, status, direction,
                        Pt.ui_direction_rtl);
                result = Pt.app_run_for(app, 250);
            } else {
                result = Pt.app_run(app);
            }
            Pt.app_destroy(app);
            if (result != 0) {
                System.exit(result);
            }
        }
    }
}