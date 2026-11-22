/*
 * Pt input panel example - Java 25 FFM bindings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * 1.3 adds the text-input-v3 v2 desired-state requests to
 * bindings/java/src/pt/Pt.java:
 *
 *   public static boolean window_show_input_panel(MemorySegment window);
 *   public static boolean window_hide_input_panel(MemorySegment window);
 *
 * Both mirror pt_window_show_input_panel / pt_window_hide_input_panel and
 * return true only when the recorded desired state changed.
 *
 * The request is a text-input-v3 v2 hint. The compositor decides whether an
 * on-screen keyboard appears, and Pt never reports whether it did. Headless
 * windows still record the desired state. preedit_hint ranges are presentation
 * metadata rendered by Pt inside its own entries; the custom IME handlers below
 * receive text and cursor offsets only.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

import pt.Pt;

public final class InputPanel {
    private InputPanel() {}

    private static MemorySegment window;
    private static MemorySegment panelStatus;
    private static MemorySegment entryStatus;
    private static MemorySegment preeditStatus;
    private static boolean desiredVisible;
    private static int showChanges;
    private static int hideChanges;
    private static final StringBuilder committed = new StringBuilder();

    private static void showPanel() {
        boolean changed = Pt.window_show_input_panel(window);
        if (changed) {
            desiredVisible = true;
            showChanges += 1;
        }
        Pt.widget_set_text(panelStatus,
                "Panel: show " + (changed ? "recorded" : "already requested")
                        + " (desired " + (desiredVisible ? "visible" : "hidden")
                        + "; the compositor decides whether an OSK appears)");
        System.out.printf("input_panel_java: show changed=%d%n", changed ? 1 : 0);
    }

    private static void hidePanel() {
        boolean changed = Pt.window_hide_input_panel(window);
        if (changed) {
            desiredVisible = false;
            hideChanges += 1;
        }
        Pt.widget_set_text(panelStatus,
                "Panel: hide " + (changed ? "recorded" : "already requested")
                        + " (desired " + (desiredVisible ? "visible" : "hidden")
                        + "; repeated calls never duplicate the request)");
        System.out.printf("input_panel_java: hide changed=%d%n", changed ? 1 : 0);
    }

    private static void onCommit(MemorySegment ignored, String text, MemorySegment data) {
        committed.append(text == null ? "" : text);
        Pt.widget_set_text(preeditStatus,
                "Committed \"" + text + "\"; custom buffer \"" + committed + "\"");
        System.out.printf("input_panel_java: commit \"%s\"%n", text);
    }

    private static void onPreedit(MemorySegment ignored, String text, int cursorBegin,
            int cursorEnd, MemorySegment data) {
        if (text != null && !text.isEmpty()) {
            Pt.widget_set_text(preeditStatus,
                    "Preedit \"" + text + "\" cursor " + cursorBegin + ".." + cursorEnd
                            + " (app-drawn)");
        } else {
            Pt.widget_set_text(preeditStatus,
                    "Preedit: (none) - an empty preedit ends the composition");
        }
        System.out.printf("input_panel_java: preedit \"%s\"%n", text == null ? "" : text);
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("pt.example.input_panel.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));

            window = Pt.window_create(app, "Pt Input Panel - Java", 680, 540);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
            Pt.widget_set_gap(root, 14.0f);

            MemorySegment eyebrow = Pt.label(root, "JAVA BINDING  -  INPUT PANEL");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x7AA2F7));

            MemorySegment heading = Pt.label(root, "Show and hide the on-screen input panel");
            Pt.widget_set_font_size(heading, 28.0f);
            Pt.widget_set_font_weight(heading, Pt.font_weight_bold);

            MemorySegment intro = Pt.label(root,
                    "The two calls are text-input-v3 v2 hints. Their boolean result is the "
                            + "local desired state transition, not proof that an OSK appeared.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            MemorySegment card = Pt.card(root);
            Pt.widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
            Pt.widget_set_gap(card, 12.0f);

            MemorySegment entry =
                    Pt.entry(card, "Type here - a real IME shows hinted underlines");
            entryStatus = Pt.label(card, "Entry text: \"\"");
            Pt.widget_set_font_size(entryStatus, 12.0f);
            Pt.widget_set_wrap(entryStatus, true);
            Pt.widget_set_foreground(entryStatus, Pt.rgb(0x9AA7B8));
            Pt.entry_set_on_change(entry, (widget, text, data) -> Pt.widget_set_text(entryStatus,
                    "Entry text: \"" + (text == null ? "" : text.getString(0)) + "\""), arena);

            MemorySegment buttons = Pt.row(card);
            Pt.widget_set_gap(buttons, 10.0f);
            MemorySegment show = Pt.button(buttons, "Show input panel");
            Pt.button_set_variant(show, Pt.button_primary);
            Pt.widget_set_tooltip(show, "zwp_text_input_v3.show_input_panel");
            Pt.button_set_on_click(show, (widget, data) -> showPanel(), arena);
            MemorySegment hide = Pt.button(buttons, "Hide input panel");
            Pt.button_set_variant(hide, Pt.button_secondary);
            Pt.widget_set_tooltip(hide, "zwp_text_input_v3.hide_input_panel");
            Pt.button_set_on_click(hide, (widget, data) -> hidePanel(), arena);

            panelStatus = Pt.label(card, "Panel: hidden (initial desired state)");
            Pt.widget_set_font_size(panelStatus, 12.0f);
            Pt.widget_set_wrap(panelStatus, true);
            Pt.widget_set_foreground(panelStatus, Pt.rgb(0x9ECE6A));

            preeditStatus = Pt.label(card, "Preedit: (none)");
            Pt.widget_set_font_size(preeditStatus, 12.0f);
            Pt.widget_set_wrap(preeditStatus, true);
            Pt.widget_set_foreground(preeditStatus, Pt.rgb(0xE0AF68));

            /* Active whenever no entry holds editable focus; the entry's own IME
             * wins while it is focused. */
            Pt.window_set_ime_callbacks(window, InputPanel::onCommit, InputPanel::onPreedit,
                    arena);
            Pt.window_set_ime_enabled(window, true);
            Pt.window_set_ime_cursor_rect(window, 24, 320, 2, 20);

            MemorySegment footer = Pt.label(root,
                    "Headless runs record the desired state only. preedit_hint ranges are "
                            + "drawn by Pt inside entries and are absent from the custom "
                            + "callback ABI.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_wrap(footer, true);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            MemorySegment quit = Pt.button(root, "Quit");
            Pt.button_set_variant(quit, Pt.button_ghost);
            Pt.widget_set_align_self(quit, Pt.align_start);
            Pt.button_set_on_click(quit, (widget, data) -> Pt.app_quit(app), arena);

            Pt.window_show(window);
            Pt.widget_focus(entry);

            String headless = System.getenv("PT_HEADLESS");
            int status;
            if (headless != null && !headless.isEmpty() && !headless.equals("0")) {
                showPanel();
                showPanel();
                hidePanel();
                hidePanel();
                System.out.printf("input_panel_java: show_changes=%d hide_changes=%d%n",
                        showChanges, hideChanges);
                status = Pt.app_run_for(app, 250);
            } else {
                status = Pt.app_run(app);
            }
            Pt.app_destroy(app);
            System.exit(status);
        }
    }
}