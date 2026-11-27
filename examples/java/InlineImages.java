/*
 * Pt inline images - Java 25 Foreign Function & Memory API.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Pt.TextImage mirrors the C descriptor: a UTF-8 byte offset, the native
 * pt_icon_image pointer, logical width/height, alignment and bounded alt text.
 * The setter copies the array and takes its own image reference; the getter
 * returns a descriptor whose pointer is borrowed; text_area_insert_image
 * returns the new byte offset, or -1 when the call is rejected.
 *
 * The example embeds a 32x32 PNG, decorates two U+FFFC placeholders with
 * different alignment and alt text, inserts a third at the caret and checks the
 * model under PT_HEADLESS. It never asserts rendered pixels.
 */

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.Base64;

import pt.Pt;

public final class InlineImages {
    private static final String ICON_PNG =
            "iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAADuklEQVR42u3XW2vbMBjG8X2d"
            + "ntdt3aHr2nXb1woYDAKDwRAwAYNBIBAIDIZgCBgMBoHBkO+RpmnXraftTvvfNbC0TVv3Zkzw"
            + "3iWvfyR6H8kvXvxfD1ynvV33vffBnfXeU+/cj95basf97L1x573X7tkefOLtuVPvIwXAA+AB"
            + "8AB4ADwAHgDvNfXKXXjb3UFm/r6b+Z/ciQ/AB+AD8AH4AHwAPgAfgA/AB+AD8Lfdpf+S2noa"
            + "5FgcuJkAIAAIAAKAACAACAACgAAgAAgAAoAAIAAIAGLLXYnNxyGmwWd3HAAIAAQAAgABgGD3"
            + "1obnAYAAQAAgABAACDbddbDxMMQ0PHTTEEAIIAQQAgj3lm5yEQIIAYQAQgDhBrW+3PePoi9u"
            + "GgGIAEQAov1H/4+XEYAIQAQgWne/orX7ex31AfQB9AH0D568m6/6APoA+gD69wAm8Vd3FAOI"
            + "AcQA4g4AMYAYQAwgXnO/49Xbe04GAAYABgAGnzub5esBgAGAAYDBLYBJ8s1NEgAJgARA0iEg"
            + "AZAASAAkq9TK370nKYAUQAogPbz34WcpOZCSAyk5kO7c+/nrFEAKIAWQLgJIABKABCBvADPJ"
            + "GErGUJIDkhyQBJEkiCQACUACkASRJIgkOSDJAckYSsZQ3iThtQQgAUgAchFAAVAAFAA1B1AA"
            + "FAAFQAFQABQABUABUAAUAAVAAVAAFAAFQM0BFAAFQAFQiwAagAagAeg5gAagAWgAGoAGoAFo"
            + "ABqABqABaAAagAagAWgAeg6gAWgAGoBeBDAADAADwMwBDAADwAAwAAwAA8AAMAAMAAPAADAA"
            + "DAADwAAwcwADwAAwAMwiQAYgA5AByJbYhBmADEAGIFtiE2YAMgAZgGwRIAeQA8gB5Ixh3uEY"
            + "5oxhDiAHkAPIV27JgiGAIYAhgGGHgCGAIYAhgOFdSVgAKAAUAAqiuOggiguiuABQACgAFKt3"
            + "9zwaARgBGAEYdQAYARgBGAEYLXMalgBKACWAkuO4fMJxXHIclwBKACWAcm25XtMKQAWgAlAx"
            + "hhVjWD3gQlIxhhVjWAGoAFQAqvUH3opqADWAGkANoCYHanKgvuNKVpMDNTlQA6gB1ABqAPXG"
            + "437FYwvAArAALAALwBJEliCy5IAlByw5YAkiSxBZABaABWABWAB282n7aNYAaAA0ABoADYAG"
            + "QAOgAdAAaAA0ABoADYAGQAOg2eru/eCkBdACaAG0AFoALYAWQAugBdACaAG028/3hnQ6BjAG"
            + "MAYwBjAGMAYwBjB+xlezf3b9AeJbDQsb/1nuAAAAAElFTkSuQmCC";

    private static final String INITIAL_TEXT =
            "Build: \uFFFC passed \uFFFC pending";
    private static final int FIRST_START = 7;
    private static final int SECOND_START = 18;

    private InlineImages() {}

    private static Pt.TextImage image(int start, MemorySegment icon, float width,
            float height, int align, String alt) {
        return new Pt.TextImage(start, icon.address(), width, height, align, alt);
    }

    private static boolean headless() {
        String value = System.getenv("PT_HEADLESS");
        return value != null && !value.isEmpty() && !value.equals("0");
    }

    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.examples.inline_images.java");
            if (app.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                System.exit(1);
            }
            Pt.app_set_color_scheme(app, Pt.color_scheme_dark);
            Pt.app_set_accent_color(app, Pt.rgb(0x7AA2F7));

            MemorySegment window = Pt.window_create(app, "Pt Inline Images - Java", 720, 480);
            if (window.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            MemorySegment root = Pt.window_root(window);
            Pt.widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
            Pt.widget_set_gap(root, 12.0f);

            MemorySegment eyebrow = Pt.label(root, "JAVA BINDING  ·  U+FFFC  ·  SIDE TABLE");
            Pt.widget_set_font_size(eyebrow, 11.0f);
            Pt.widget_set_font_weight(eyebrow, Pt.font_weight_semibold);
            Pt.widget_set_foreground(eyebrow, Pt.rgb(0x7AA2F7));

            MemorySegment intro = Pt.label(root,
                    "The text holds two object replacement characters. One image sits on "
                            + "the baseline, the other is centred; a third is inserted at "
                            + "the caret.");
            Pt.widget_set_font_size(intro, 13.0f);
            Pt.widget_set_wrap(intro, true);
            Pt.widget_set_foreground(intro, Pt.rgb(0x9AA7B8));

            MemorySegment area = Pt.text_area(root, "Build log");
            Pt.widget_set_text(area, INITIAL_TEXT);
            Pt.widget_set_size(area, 600.0f, 140.0f);

            MemorySegment icon = Pt.icon_image_load_memory(Base64.getDecoder().decode(ICON_PNG));
            if (icon.address() == 0) {
                System.err.println("Pt error: " + Pt.last_error());
                Pt.app_destroy(app);
                System.exit(1);
            }

            // start 0 points at 'B', not a placeholder, so the setter drops it.
            Pt.widget_set_text_images(area, new Pt.TextImage[] {
                    image(FIRST_START, icon, 20.0f, 20.0f,
                            Pt.text_image_align_baseline, "Passed"),
                    image(SECOND_START, icon, 24.0f, 24.0f,
                            Pt.text_image_align_middle, "Pending"),
                    image(0, icon, 16.0f, 16.0f,
                            Pt.text_image_align_baseline, "Invalid"),
            });

            int length = INITIAL_TEXT.getBytes(StandardCharsets.UTF_8).length;
            Pt.text_area_set_selection(area, length, length);
            int insertedStart = Pt.text_area_insert_image(area,
                    image(0, icon, 18.0f, 18.0f, Pt.text_image_align_baseline, "Working"));
            Pt.icon_image_destroy(icon);

            int count = Pt.widget_get_text_image_count(area);
            Pt.TextImage first = Pt.widget_get_text_image(area, 0);
            Pt.TextImage second = Pt.widget_get_text_image(area, 1);

            int failures = 0;
            if (insertedStart != 29) {
                failures++;
            }
            if (count != 3 || first == null || second == null) {
                failures++;
            }
            if (first == null || first.align() != Pt.text_image_align_baseline
                    || !"Passed".equals(first.alt())) {
                failures++;
            }
            if (second == null || second.align() != Pt.text_image_align_middle
                    || !"Pending".equals(second.alt())) {
                failures++;
            }

            int textBytes = Pt.widget_get_text(area).getBytes(StandardCharsets.UTF_8).length;
            String summary = String.format(
                    "entries=%d  inserted_at=%d  first=%d/%s  second=%d/%s  text_bytes=%d",
                    count, insertedStart, first == null ? -1 : first.align(),
                    first == null ? "" : first.alt(), second == null ? -1 : second.align(),
                    second == null ? "" : second.alt(), textBytes);
            MemorySegment status = Pt.label(root, summary);
            Pt.widget_set_font_size(status, 12.0f);
            Pt.widget_set_wrap(status, true);
            Pt.widget_set_foreground(status, Pt.rgb(0x9ECE6A));

            MemorySegment footer = Pt.label(root,
                    "The text still contains EF BF BC; the side table is an index over it. "
                            + "Selection and bidi offsets are unchanged.");
            Pt.widget_set_font_size(footer, 12.0f);
            Pt.widget_set_wrap(footer, true);
            Pt.widget_set_foreground(footer, Pt.rgb(0x6F7C8F));

            MemorySegment quit = Pt.button(root, "Quit");
            Pt.button_set_variant(quit, Pt.button_ghost);
            Pt.button_set_on_click(quit, (widget, data) -> Pt.window_close(window), arena);

            Pt.window_show(window);

            int statusCode;
            if (headless()) {
                System.out.printf(
                        "inline_images: count=%d inserted_start=%d first_align=%d "
                                + "second_align=%d second_alt=%s text_bytes=%d%n",
                        count, insertedStart, first == null ? -1 : first.align(),
                        second == null ? -1 : second.align(),
                        second == null ? "" : second.alt(), textBytes);
                System.out.flush();
                statusCode = Pt.app_run_for(app, 250);
            } else {
                statusCode = Pt.app_run(app);
            }

            Pt.app_destroy(app);
            if (failures > 0) {
                System.exit(1);
            }
            if (statusCode != 0) {
                System.exit(statusCode);
            }
        }
    }
}
