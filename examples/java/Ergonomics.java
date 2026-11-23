/*
 * Pt ergonomics notebook - Java 25 Foreign Function & Memory API.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The 2.1 surface takes the C enums as ints: caret_movement_inherit (0)
 * resolves to visual, ui_direction_inherit (0) resolves from the locale hint,
 * and bidi_sanitize_neutralize (1) keeps a visible U+FFFD per control.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import pt.Pt;

public final class Ergonomics {
    private static final String MIXED =
            "שלום مرحبا Hello, 123";
    private static final String UNTRUSTED =
            "report\u202efdp.exe\u2066https://example.com\u2069";

    private Ergonomics() {}

    private static void reportSafety() {
        Pt.BidiControlSpan[] spans = Pt.bidi_control_inventory(UNTRUSTED);
        String first = spans.length == 0 ? "none"
                : "(" + spans[0].offset() + "," + spans[0].length() + ",kind="
                        + spans[0].kind() + ")";
        System.out.printf("ergonomics: inventory=%d first=%s%n", spans.length, first);
        String stripped = Pt.bidi_sanitize(UNTRUSTED, Pt.BIDI_SANITIZE_STRIP);
        String neutral = Pt.bidi_sanitize(UNTRUSTED, Pt.BIDI_SANITIZE_NEUTRALIZE);
        System.out.printf("ergonomics: strip=%d neutralize=%d replacement=%d%n",
                stripped.getBytes(java.nio.charset.StandardCharsets.UTF_8).length,
                neutral.getBytes(java.nio.charset.StandardCharsets.UTF_8).length,
                neutral.chars().filter(c -> c == 0xFFFD).count());
        System.out.printf("ergonomics: locale ar=%d he=%d az-Arab=%d az-Latn=%d C=%d%n",
                Pt.locale_ui_direction("ar"), Pt.locale_ui_direction("he"),
                Pt.locale_ui_direction("az-Arab"), Pt.locale_ui_direction("az-Latn"),
                Pt.locale_ui_direction("C"));
        System.out.flush();
    }

    private static void apply(MemorySegment app, MemorySegment window, MemorySegment editor,
            MemorySegment card, MemorySegment status, int movement, int shell) {
        Pt.app_set_caret_movement(app, movement);
        Pt.window_set_caret_movement(window, movement);
        Pt.widget_set_caret_movement(editor, Pt.caret_movement_logical);
        Pt.window_set_ui_direction(window, shell);
        Pt.widget_set_padding_logical(card, 12.0f, 36.0f, 12.0f, 8.0f);
        Pt.widget_set_ui_direction(card, shell);
        Pt.widget_set_text(status,
                movement == Pt.caret_movement_logical ? "logical arrows" : "visual arrows");
        System.out.printf(
                "ergonomics: movement=%d window_movement=%d editor=%d shell=%d locale=%s%n",
                Pt.app_get_caret_movement(app), Pt.window_get_caret_movement(window),
                Pt.widget_get_caret_movement(editor), Pt.window_get_ui_direction(window),
                Pt.app_get_locale(app));
        System.out.flush();
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.ergonomics.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));
            Pt.app_set_locale(app, "he_IL.UTF-8");

            MemorySegment window = Pt.window_create(app, "Pt Ergonomics - Java", 760, 600);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }
            MemorySegment root = Pt.window_root(window);

            MemorySegment page = Pt.column(root);
            Pt.widget_set_padding(page, 24.0f, 24.0f, 24.0f, 24.0f);
            Pt.widget_set_gap(page, 12.0f);

            MemorySegment eyebrow = Pt.label(page, "JAVA BINDING  ·  2.1 ERGONOMICS");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x7AA2F7));

            MemorySegment title = Pt.label(page, MIXED);
            Pt.widget_set_font_size(title, 28.0f);
            Pt.widget_set_font_weight(title, Pt.font_weight_bold);
            Pt.widget_set_text_direction(title, Pt.text_direction_auto);

            MemorySegment intro = Pt.label(page,
                    "The locale hint resolves the shell, the caret policy switches the editor "
                            + "and the card's logical padding follows whichever direction wins.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            MemorySegment row = Pt.row(page);
            Pt.widget_set_gap(row, 10.0f);
            Pt.box_set_align(row, Pt.align_center);

            final MemorySegment card = Pt.card(page);
            Pt.widget_set_gap(card, 8.0f);
            Pt.widget_set_ui_direction(card, Pt.ui_direction_rtl);
            MemorySegment cardLabel = Pt.label(card, "Logical padding: start 36, end 8");
            Pt.widget_set_font_size(cardLabel, 13.0f);
            Pt.widget_set_font_weight(cardLabel, Pt.font_weight_semibold);
            Pt.widget_set_wrap(cardLabel, true);

            final MemorySegment editor = Pt.entry(card, "identifier");
            Pt.widget_set_text(editor, "shalom_el(marhaba, 123);");
            Pt.widget_set_size(editor, 420.0f, 36.0f);
            Pt.widget_set_ui_direction(editor, Pt.ui_direction_ltr);
            Pt.widget_set_text_direction(editor, Pt.text_direction_ltr);
            Pt.widget_set_caret_movement(editor, Pt.caret_movement_logical);
            Pt.widget_set_align_self(editor, Pt.align_start);

            final MemorySegment status = Pt.label(row, "");
            Pt.widget_set_grow(status, 1.0f);
            Pt.widget_set_font_size(status, 12.0f);
            Pt.widget_set_foreground(status, Pt.rgb(0x9ECE6A));

            MemorySegment visual = Pt.button(row, "Visual");
            Pt.button_set_variant(visual, Pt.button_ghost);
            Pt.button_set_on_click(visual, (widget, data) -> apply(app, window, editor, card,
                    status, Pt.caret_movement_visual, Pt.ui_direction_rtl), arena);
            MemorySegment logical = Pt.button(row, "Logical");
            Pt.button_set_variant(logical, Pt.button_primary);
            Pt.button_set_on_click(logical, (widget, data) -> apply(app, window, editor, card,
                    status, Pt.caret_movement_logical, Pt.ui_direction_rtl), arena);
            MemorySegment ltr = Pt.button(row, "LTR shell");
            Pt.button_set_variant(ltr, Pt.button_ghost);
            Pt.button_set_on_click(ltr, (widget, data) -> apply(app, window, editor, card,
                    status, Pt.caret_movement_logical, Pt.ui_direction_ltr), arena);

            Pt.spacer(page);
            MemorySegment quit = Pt.button(page, "Quit");
            Pt.button_set_variant(quit, Pt.button_ghost);
            Pt.widget_set_align_self(quit, Pt.align_start);
            Pt.button_set_on_click(quit, (widget, data) -> Pt.window_close(window), arena);

            apply(app, window, editor, card, status, Pt.caret_movement_visual,
                    Pt.ui_direction_rtl);
            reportSafety();
            Pt.window_show(window);

            String headless = System.getenv("PT_HEADLESS");
            int result;
            if (headless != null && !headless.isEmpty() && !headless.equals("0")) {
                apply(app, window, editor, card, status, Pt.caret_movement_logical,
                        Pt.ui_direction_ltr);
                apply(app, window, editor, card, status, Pt.caret_movement_visual,
                        Pt.ui_direction_rtl);
                reportSafety();
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
