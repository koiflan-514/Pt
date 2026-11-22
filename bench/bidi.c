/*
 * Pt bidi/RTL performance guards (2.0 wave 7).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Three dedicated ceilings for the paths the 2.0 bidi work added, kept apart
 * from the LTR guards so the existing LTR ceilings stay untouched:
 *
 *   1. a dense mixed-script paragraph (~2000 characters) laid out and painted
 *      with wrapping,
 *   2. a 100-row RTL table with mixed cell text painted,
 *   3. an RTL entry caret moved 1000 times (geometry plus invalidation).
 *
 * Ceilings are generous (~2x the measured median on the reference machine)
 * and scale with PT_PERF_GUARD_SCALE, like bench/guards.c.
 */

#include "../src/internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define bidi_batches 5
#define bidi_frames 20
#define bidi_width 900
#define bidi_height 700
#define bidi_caret_moves 1000
#define bidi_table_rows 100

static int failures;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double median(double *values, int count) {
    qsort(values, (size_t)count, sizeof(*values), compare_double);
    return values[count / 2];
}

static double batch_median(const double *totals, int batches, int frames) {
    double means[bidi_batches];
    for (int i = 0; i < batches; ++i) {
        means[i] = totals[i] / (double)frames;
    }
    return median(means, batches);
}

static void report_time(const char *name, double value, double limit_ms) {
    bool ok = value <= limit_ms;
    printf("%-34s %9.3f ms   limit %8.1f ms   %s\n", name, value, limit_ms,
           ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

/* Fills `buffer` with repeated mixed Hebrew/Arabic/Latin/digit segments until
 * at least `target` bytes are present. Returns the byte length. */
static size_t mixed_text(char *buffer, size_t capacity, size_t target) {
    static const char segment[] =
        "Hello \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8"
        "\xD8\xA7 12345 \xE4\xB8\x96\xE7\x95\x8C ";
    size_t length = 0;
    while (length + sizeof(segment) - 1u < capacity && length < target) {
        memcpy(buffer + length, segment, sizeof(segment) - 1u);
        length += sizeof(segment) - 1u;
    }
    buffer[length] = '\0';
    return length;
}

static double measure_mixed_paragraph(void) {
    pt_app *app = pt_app_create("pt.guards.bidi.paragraph");
    if (!app) {
        ++failures;
        return 1e9;
    }
    pt_app_set_font_family(app, "DejaVu Sans");
    pt_window *window = pt_window_create(app, "Bidi paragraph", bidi_width, bidi_height);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return 1e9;
    }
    char text[4096];
    size_t length = mixed_text(text, sizeof(text), 2000u);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget *label = pt_label_create(root, text);
    pt_widget_set_font_size(label, 16.0f);
    pt_widget_set_width(label, 560.0f);
    pt_widget_set_label_wrap(label, true);

    uint32_t *pixels = calloc((size_t)bidi_width * (size_t)bidi_height, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return 1e9;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, bidi_width, bidi_height, bidi_width, 1.0f);
    pt_window_show(window);
    for (int i = 0; i < 8; ++i) {
        pt_window_paint(window, &canvas);
    }

    double totals[bidi_batches] = {0};
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    for (int batch = 0; batch < bidi_batches; ++batch) {
        for (int frame = 0; frame < bidi_frames; ++frame) {
            window->needs_layout = true;
            window->needs_draw = true;
            double start = now_ms();
            pt_widget_layout(window->root, 0.0f, window->header_height,
                             (float)window->width,
                             (float)window->height - window->header_height, &ctx);
            pt_window_paint(window, &canvas);
            totals[batch] += now_ms() - start;
        }
    }
    double result = batch_median(totals, bidi_batches, bidi_frames);
    printf("mixed paragraph: %zu bytes wrapped at 560 px\n", length);
    free(pixels);
    pt_app_destroy(app);
    return result;
}

typedef struct bidi_table_state {
    const char *texts[4];
} bidi_table_state;

static void bind_bidi_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                           void *data) {
    (void)table;
    bidi_table_state *state = data;
    if (row < bidi_table_rows && column < 4u) {
        pt_widget_set_text(cell, state->texts[column]);
    }
}

static double measure_rtl_table(void) {
    pt_app *app = pt_app_create("pt.guards.bidi.table");
    if (!app) {
        ++failures;
        return 1e9;
    }
    pt_app_set_font_family(app, "DejaVu Sans");
    pt_window *window = pt_window_create(app, "Bidi table", bidi_width, bidi_height);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return 1e9;
    }
    pt_window_set_ui_direction(window, pt_ui_direction_rtl);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget *table = pt_table_create(root);
    pt_table_set_column_count(table, 4);
    for (uint32_t column = 0; column < 4u; ++column) {
        pt_table_set_column_width(table, column, 180.0f);
    }
    pt_table_set_row_height(table, 28.0f);
    pt_table_set_row_count(table, bidi_table_rows);
    bidi_table_state state = {
        .texts = {"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D 2026",
                  "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 42%",
                  "Mixed \xD7\x90\xD7\x91\xD7\x92 value", "1234 \xE4\xB8\x96\xE7\x95\x8C"},
    };
    pt_table_set_on_bind_cell(table, bind_bidi_cell, &state);
    pt_widget_set_height(table, 600.0f);

    uint32_t *pixels = calloc((size_t)bidi_width * (size_t)bidi_height, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return 1e9;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, bidi_width, bidi_height, bidi_width, 1.0f);
    pt_window_show(window);
    uint32_t *dump = malloc((size_t)bidi_width * (size_t)bidi_height * sizeof(*dump));
    if (dump) {
        if (!pt_window_paint_pixels(window, dump, bidi_width, bidi_height)) {
            ++failures;
        }
        free(dump);
    }
    for (int i = 0; i < 10; ++i) {
        window->needs_draw = true;
        pt_window_paint(window, &canvas);
    }

    double totals[bidi_batches] = {0};
    for (int batch = 0; batch < bidi_batches; ++batch) {
        for (int frame = 0; frame < bidi_frames; ++frame) {
            window->needs_draw = true;
            double start = now_ms();
            pt_window_paint(window, &canvas);
            totals[batch] += now_ms() - start;
        }
    }
    double result = batch_median(totals, bidi_batches, bidi_frames);
    free(pixels);
    pt_app_destroy(app);
    return result;
}

static double measure_rtl_entry_caret(void) {
    pt_app *app = pt_app_create("pt.guards.bidi.entry");
    if (!app) {
        ++failures;
        return 1e9;
    }
    pt_app_set_font_family(app, "DejaVu Sans");
    pt_window *window = pt_window_create(app, "Bidi entry", 640, 200);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return 1e9;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget *entry = pt_entry_create(root, "");
    pt_widget_set_size(entry, 600.0f, 34.0f);
    pt_widget_set_font_size(entry, 16.0f);
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    char text[512];
    size_t length = mixed_text(text, sizeof(text), 320u);
    text[length] = '\0';
    pt_widget_set_text(entry, text);
    pt_widget_set_align_self(entry, pt_align_start);

    uint32_t *pixels = calloc(640u * 200u, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return 1e9;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, 640, 200, 640, 1.0f);
    pt_window_show(window);
    pt_window_paint(window, &canvas);
    if (!pt_widget_focus(entry)) {
        ++failures;
    }
    window->caret_visible = true;

    double start = now_ms();
    for (int move = 0; move < bidi_caret_moves; ++move) {
        uint32_t key = (move & 1) ? XKB_KEY_Left : XKB_KEY_Right;
        pt_window_input_key(window, key, 0, NULL, true);
    }
    double result = now_ms() - start;
    printf("rtl entry: %zu bytes, %d caret moves, final cursor %zu\n", length,
           bidi_caret_moves, entry->cursor);
    free(pixels);
    pt_app_destroy(app);
    return result;
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    double scale = 1.0;
    const char *scale_env = getenv("PT_PERF_GUARD_SCALE");
    if (scale_env && scale_env[0]) {
        scale = atof(scale_env);
        if (scale <= 0.0) {
            scale = 1.0;
        }
    }
    printf("bidi perf guards; thresholds scaled by %.2f\n", scale);

    report_time("mixed paragraph wrap+paint", measure_mixed_paragraph(), 2.0 * scale);
    report_time("rtl table 100 rows paint", measure_rtl_table(), 7.0 * scale);
    report_time("rtl entry 1000 caret moves", measure_rtl_entry_caret(), 100.0 * scale);

    if (failures) {
        printf("bidi perf guards: %d failure(s)\n", failures);
        return 1;
    }
    printf("bidi perf guards: all ceilings hold\n");
    return 0;
}