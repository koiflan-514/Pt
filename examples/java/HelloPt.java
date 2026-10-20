/*
 * Pt counter example - Java 25 Foreign Function & Memory API.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import pt.Pt;

public final class HelloPt {
    private HelloPt() {}

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.counter.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }

            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0xBB9AF7));

            MemorySegment window = Pt.window_create(app, "Pt Counter - Java", 560, 460);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            MemorySegment root = Pt.window_root(window);

            MemorySegment header = Pt.column(root);
            Pt.widget_set_gap(header, 6.0f);
            Pt.widget_set_margin(header, 0.0f, 0.0f, 4.0f, 0.0f);

            MemorySegment eyebrow = Pt.label(header, "JAVA BINDING");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0xBB9AF7));

            MemorySegment title = Pt.label(header, "Counter");
            Pt.widget_set_font_size(title, 30.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_bold);

            MemorySegment subtitle = Pt.label(header,
                    "JDK 25 FFM downcalls and upcalls drive the same native widgets "
                            + "as the C API, with no JNI glue.");
            Pt.widget_set_font_size(subtitle, 14.0f);
            Pt.widget_set_wrap(subtitle, true);
            Pt.widget_set_foreground(subtitle, Pt.rgb(0x9AA7B8));

            MemorySegment card = Pt.card(root);
            Pt.widget_set_padding(card, 28.0f, 28.0f, 28.0f, 28.0f);
            Pt.widget_set_radius(card, 20.0f);
            Pt.widget_set_background(card, Pt.rgb(0x161D28));
            Pt.widget_set_border_width(card, 1.0f);
            Pt.widget_set_border_color(card, Pt.rgb(0x253040));
            Pt.widget_set_gap(card, 18.0f);

            MemorySegment controls = Pt.row(card);
            Pt.widget_set_gap(controls, 22.0f);
            Pt.box_set_justify(controls, Pt.justify_center);
            Pt.box_set_align(controls, Pt.align_center);

            final int[] count = {0};

            MemorySegment minus = Pt.button(controls, "-");
            Pt.widget_set_size(minus, 52.0f, 52.0f);
            Pt.widget_set_font_size(minus, 22.0f);
            Pt.button_set_variant(minus, Pt.button_secondary);
            Pt.widget_set_tooltip(minus, "Decrease the counter");

            MemorySegment value = Pt.label(controls, "0");
            Pt.widget_set_min_width(value, 150.0f);
            Pt.widget_set_font_size(value, 56.0f);
            Pt.widget_set_font_weight(value, Pt.font_weight_bold);
            Pt.widget_set_text_align(value, Pt.text_align_center);
            Pt.widget_set_a11y_name(value, "Counter value");

            MemorySegment plus = Pt.button(controls, "+");
            Pt.widget_set_size(plus, 52.0f, 52.0f);
            Pt.widget_set_font_size(plus, 22.0f);
            Pt.button_set_variant(plus, Pt.button_primary);
            Pt.widget_set_tooltip(plus, "Increase the counter");

            Pt.button_set_on_click(minus, (widget, data) -> {
                count[0] -= 1;
                Pt.widget_set_text(value, Integer.toString(count[0]));
            }, arena);
            Pt.button_set_on_click(plus, (widget, data) -> {
                count[0] += 1;
                Pt.widget_set_text(value, Integer.toString(count[0]));
            }, arena);

            MemorySegment actions = Pt.row(card);
            Pt.widget_set_gap(actions, 10.0f);
            Pt.box_set_justify(actions, Pt.justify_center);

            MemorySegment reset = Pt.button(actions, "Reset");
            Pt.button_set_variant(reset, Pt.button_ghost);
            Pt.button_set_on_click(reset, (widget, data) -> {
                count[0] = 0;
                Pt.widget_set_text(value, "0");
                Pt.window_show_toast(window, "Counter reset", Pt.toast_success, 1800);
            }, arena);

            MemorySegment menu = Pt.menu_create(window);
            Pt.menu_add_item(menu, "Reset counter", null, (widget, data) -> {
                count[0] = 0;
                Pt.widget_set_text(value, "0");
            }, arena);
            Pt.menu_add_separator(menu);
            Pt.menu_add_item(menu, "Quit", null, (widget, data) -> Pt.window_close(window),
                    arena);
            MemorySegment menu_button = Pt.button(actions, "Actions");
            Pt.button_set_variant(menu_button, Pt.button_ghost);
            Pt.widget_set_tooltip(menu_button, "Open the actions menu");
            Pt.button_set_on_click(menu_button,
                    (widget, data) -> Pt.menu_show_below(menu, widget), arena);

            MemorySegment quit = Pt.button(actions, "Quit");
            Pt.button_set_variant(quit, Pt.button_ghost);
            Pt.button_set_on_click(quit, (widget, data) -> Pt.window_close(window), arena);

            Pt.spacer(root);

            MemorySegment footer = Pt.label(root,
                    "JVM object lifetimes stay outside the ABI; only NUL-terminated "
                            + "UTF-8 strings cross the boundary.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            Pt.window_set_key_handler(window, (target, key, modifiers, data) -> {
                if (key == 0xFF1B) {
                    Pt.window_close(target);
                    return true;
                }
                return false;
            }, arena);

            Pt.window_show(window);
            int result = Pt.app_run(app);
            Pt.menu_destroy(menu);
            Pt.app_destroy(app);
            if (result != 0) {
                System.exit(result);
            }
        }
    }
}
