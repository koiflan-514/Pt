/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Cross-version LTR byte-compatibility corpus.
 *
 * Renders a fixed set of pure-LTR scenes through the public API and writes one
 * PPM P6 image per scene. The 2.0 bidi work may only change RTL and mixed
 * text; running this program against 1.4.0 and 2.0 on the same machine must
 * produce byte-identical images. The comparison lives in tools/ltr-compat.sh.
 *
 * Determinism rules for this file: fixed window sizes, an explicit font
 * family and fallback chain, an explicit color scheme, no focus (no blinking
 * caret), no hover, no animation, no randomness, no time or locale input.
 *
 * Usage: ltr_corpus <output-directory>
 * The directory must already exist. PT_HEADLESS=1 is set by the harness.
 */

#include <pt/pt.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct scene {
    const char *file;
    const char *title;
    int width;
    int height;
    void (*build)(pt_widget *root);
    /* Decorated scenes paint the CSD title bar; the rest are content-only. */
    bool decorated;
} scene;

static void scene_ascii_label(pt_widget *root) {
    pt_widget *label = pt_label_create(
        root, "The quick brown fox jumps over the lazy dog, twice.");
    pt_widget_set_font_size(label, 20.0f);
}

/* Client-side decoration: the title bar with an ASCII title. The 1.4.0 title
 * origin is the clip's leading edge; anything else changes these bytes. */
static void scene_csd_title(pt_widget *root) {
    pt_label_create(root, "Decorations");
}

static void scene_numeric_label(pt_widget *root) {
    pt_widget *label = pt_label_create(
        root, "Order #A-1042: 3.14159 + 2,718.28 = 42% [OK] (v1.4.0)");
    pt_widget_set_font_size(label, 18.0f);
}

static void scene_entry(pt_widget *root) {
    pt_widget *entry = pt_entry_create(root, "name@example.com");
    pt_widget_set_text(entry, "hello.world@example.com");
    pt_widget_set_size(entry, 360.0f, 36.0f);
    pt_widget_set_align_self(entry, pt_align_start);
}

static void scene_text_area(pt_widget *root) {
    pt_widget *area = pt_text_area_create(root, "Notes");
    pt_widget_set_text(area,
                       "Line one of the note.\n"
                       "Line two has words, commas, and dots.\n"
                       "Third line: punctuation!  \n"
                       "Last line with a trailing period.");
    pt_widget_set_size(area, 440.0f, 150.0f);
    pt_widget_set_font_size(area, 15.0f);
}

static void scene_wrapped_label(pt_widget *root) {
    pt_widget *label = pt_label_create(
        root,
        "A paragraph that wraps at the fixed width so line breaking, "
        "word spacing and the last line all take part in the snapshot.");
    pt_widget_set_width(label, 280.0f);
    pt_widget_set_label_wrap(label, true);
    pt_widget_set_font_size(label, 16.0f);
}

static void scene_ellipsis_label(pt_widget *root) {
    pt_widget *label = pt_label_create(
        root, "A very long single line that must be truncated with an ellipsis.");
    pt_widget_set_width(label, 220.0f);
    pt_widget_set_font_size(label, 17.0f);
}

static void span_range(pt_text_span *span, const char *text, const char *needle) {
    const char *found = strstr(text, needle);
    span->start = found ? (uint32_t)(found - text) : 0u;
    span->length = (uint32_t)strlen(needle);
}

static void scene_rich_spans(pt_widget *root) {
    static const char text[] =
        "Plain bold red italic underlined tail";
    pt_widget *label = pt_label_create(root, text);
    pt_widget_set_font_size(label, 18.0f);
    pt_widget_set_width(label, 480.0f);

    pt_text_span spans[5];
    memset(spans, 0, sizeof(spans));

    span_range(&spans[0], text, "bold");
    spans[0].flags = pt_text_span_has_weight;
    spans[0].weight = pt_font_weight_semibold;

    span_range(&spans[1], text, "red");
    spans[1].flags = pt_text_span_has_color;
    spans[1].color = 0xE5484DFFu;

    span_range(&spans[2], text, "italic");
    spans[2].flags = pt_text_span_has_slant;
    spans[2].slant = pt_font_slant_italic;

    span_range(&spans[3], text, "underlined");
    spans[3].flags = pt_text_span_underline;

    span_range(&spans[4], text, "tail");
    spans[4].flags = pt_text_span_has_size;
    spans[4].size = 22.0f;

    pt_widget_set_text_spans(label, spans, 5);
}

static void scene_cjk_label(pt_widget *root) {
    pt_widget *label = pt_label_create(
        root, "中文渲染测试：字体回退、字距与标点。");
    pt_widget_set_font_size(label, 22.0f);
    pt_widget_set_width(label, 460.0f);
}

static const char *const table_cells[4][3] = {
    {"Ada Lovelace", "Analytical Engine", "1843"},
    {"Grace Hopper", "COBOL compiler", "1952"},
    {"Margaret Hamilton", "Apollo guidance", "1969"},
    {"Barbara Liskov", "CLU language", "1974"},
};

static void bind_table_cell(pt_widget *table, pt_widget *cell, uint32_t row,
                            uint32_t column, void *user_data) {
    (void)table;
    (void)user_data;
    if (row < 4u && column < 3u) {
        pt_widget_set_text(cell, table_cells[row][column]);
    }
}

static void scene_table(pt_widget *root) {
    pt_widget *table = pt_table_create(root);
    pt_table_set_column_count(table, 3);
    pt_table_set_column_title(table, 0, "Person");
    pt_table_set_column_title(table, 1, "Work");
    pt_table_set_column_title(table, 2, "Year");
    pt_table_set_column_width(table, 0, 160.0f);
    pt_table_set_column_width(table, 1, 190.0f);
    pt_table_set_column_width(table, 2, 80.0f);
    pt_table_set_column_align(table, 2, pt_text_align_end);
    pt_table_set_on_bind_cell(table, bind_table_cell, NULL);
    pt_table_set_row_count(table, 4);
    pt_table_set_row_height(table, 30.0f);
    pt_widget_set_size(table, 440.0f, 190.0f);
    pt_widget_set_align_self(table, pt_align_start);
}

static void scene_controls(pt_widget *root) {
    pt_widget *row = pt_row_create(root);
    pt_widget_set_gap(row, 10.0f);
    pt_widget *save = pt_button_create(row, "Save changes");
    (void)save;
    pt_widget *cancel = pt_button_create(row, "Cancel");
    pt_button_set_variant(cancel, pt_button_secondary);

    pt_widget *checks = pt_column_create(root);
    pt_widget_set_gap(checks, 8.0f);
    pt_widget *auto_update = pt_checkbox_create(checks, "Enable automatic updates");
    pt_widget_set_checked(auto_update, true);
    pt_widget *diagnostics = pt_checkbox_create(checks, "Send diagnostics");
    pt_widget_set_checked(diagnostics, false);
}

static const scene scenes[] = {
    {"01-label-ascii.ppm", "LTR corpus: ASCII label", 660, 100, scene_ascii_label, false},
    {"02-label-numbers.ppm", "LTR corpus: numeric label", 560, 100,
     scene_numeric_label, false},
    {"03-entry.ppm", "LTR corpus: entry", 480, 120, scene_entry, false},
    {"04-text-area.ppm", "LTR corpus: text area", 520, 220, scene_text_area, false},
    {"05-label-wrap.ppm", "LTR corpus: wrapped label", 360, 210,
     scene_wrapped_label, false},
    {"06-label-ellipsis.ppm", "LTR corpus: ellipsis", 300, 100,
     scene_ellipsis_label, false},
    {"07-rich-spans.ppm", "LTR corpus: rich spans", 560, 110, scene_rich_spans, false},
    {"08-cjk-fallback.ppm", "LTR corpus: CJK fallback", 520, 120,
     scene_cjk_label, false},
    {"09-table.ppm", "LTR corpus: table", 520, 280, scene_table, false},
    {"10-controls.ppm", "LTR corpus: controls", 520, 180, scene_controls, false},
    {"11-csd-title.ppm", "LTR corpus: CSD title", 520, 260, scene_csd_title, true},
};

#define SCENE_COUNT (sizeof(scenes) / sizeof(scenes[0]))

static bool write_ppm(const char *directory, const char *name,
                      const uint32_t *pixels, int width, int height) {
    char path[1024];
    int written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fprintf(stderr, "ltr_corpus: output path too long for %s\n", name);
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "ltr_corpus: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }

    bool ok = fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
    unsigned char *row = malloc((size_t)width * 3u);
    if (!row) {
        ok = false;
    }
    for (int y = 0; ok && y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
            row[(size_t)x * 3u + 0u] = (unsigned char)((pixel >> 16) & 0xFFu);
            row[(size_t)x * 3u + 1u] = (unsigned char)((pixel >> 8) & 0xFFu);
            row[(size_t)x * 3u + 2u] = (unsigned char)(pixel & 0xFFu);
        }
        if (ok && fwrite(row, 3u, (size_t)width, file) != (size_t)width) {
            ok = false;
        }
    }
    free(row);
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        fprintf(stderr, "ltr_corpus: failed writing %s\n", path);
    }
    return ok;
}

static bool render_scene(pt_app *app, const scene *item, const char *directory) {
    pt_window *window =
        pt_window_create(app, item->title, item->width, item->height);
    if (!window) {
        fprintf(stderr, "ltr_corpus: window for %s: %s\n", item->file,
                pt_get_last_error());
        return false;
    }
    pt_window_set_decorated(window, item->decorated);
    pt_window_set_resizable(window, false);

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 10.0f);
    item->build(root);

    pt_window_show(window);

    size_t count = (size_t)item->width * (size_t)item->height;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    if (!pixels) {
        fprintf(stderr, "ltr_corpus: out of memory for %s\n", item->file);
        return false;
    }
    bool ok = pt_window_paint_pixels(window, pixels, item->width, item->height);
    if (!ok) {
        fprintf(stderr, "ltr_corpus: paint failed for %s: %s\n", item->file,
                pt_get_last_error());
    } else {
        ok = write_ppm(directory, item->file, pixels, item->width, item->height);
    }
    free(pixels);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <output-directory>\n", argv[0]);
        return 2;
    }

    pt_app *app = pt_app_create("dev.pt.tools.ltr-corpus");
    if (!app) {
        fprintf(stderr, "ltr_corpus: %s\n", pt_get_last_error());
        return 1;
    }

    /* Pin every input that could drift between runs or versions. */
    pt_app_set_font_family(app, "DejaVu Sans");
    static const char *const fallbacks[] = {"Noto Sans CJK SC", "DejaVu Sans"};
    pt_app_set_font_fallbacks(app, fallbacks, 2);
    pt_app_set_color_scheme(app, pt_color_scheme_dark);

    bool ok = true;
    for (size_t i = 0; i < SCENE_COUNT && ok; ++i) {
        ok = render_scene(app, &scenes[i], argv[1]);
    }

    pt_app_destroy(app);
    return ok ? 0 : 1;
}
