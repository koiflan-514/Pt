/*
 * Pt micro-benchmark: builds a dense widget tree and measures software
 * layout and paint throughput in headless mode.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define bench_width 1280
#define bench_height 800
#define bench_rows 300
#define bench_warmup 20
#define bench_frames 200

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static size_t count_nodes(const pt_widget *widget) {
    size_t total = 1;
    for (const pt_widget *child = widget->first_child; child; child = child->next_sibling) {
        total += count_nodes(child);
    }
    return total;
}

static long resident_kib(void) {
    FILE *file = fopen("/proc/self/status", "r");
    if (!file) {
        return -1;
    }
    char line[256];
    long value = -1;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            value = strtol(line + 6, NULL, 10);
            break;
        }
    }
    fclose(file);
    return value;
}

static long proportional_kib(void) {
    FILE *file = fopen("/proc/self/smaps_rollup", "r");
    if (!file) {
        return -1;
    }
    char line[256];
    long value = -1;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Pss:", 4) == 0) {
            value = strtol(line + 4, NULL, 10);
            break;
        }
    }
    fclose(file);
    return value;
}

static pt_widget *build_row(pt_widget *parent, int index) {
    pt_widget *row = pt_row_create(parent);
    pt_widget_set_padding(row, 8.0f, 10.0f, 8.0f, 10.0f);
    pt_widget_set_gap(row, 12.0f);
    pt_widget_set_border_width(row, 1.0f);
    pt_widget_set_radius(row, 8.0f);

    char name[64];
    snprintf(name, sizeof(name), "Service node %03d", index);
    pt_widget *title = pt_label_create(row, name);
    pt_widget_set_font_size(title, 14.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);

    pt_spacer_create(row);

    pt_widget *status = pt_label_create(row, index % 3 == 0 ? "degraded" : "healthy");
    pt_widget_set_font_size(status, 12.0f);
    pt_widget_set_foreground(status, index % 3 == 0 ? 0xE5A54BFFu : 0x69C779FFu);

    pt_widget *action = pt_button_create(row, "Inspect");
    pt_button_set_variant(action, pt_button_ghost);
    pt_widget_set_font_size(action, 12.0f);
    return row;
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    double build_start = now_ms();
    pt_app *app = pt_app_create("pt.benchmark");
    if (!app) {
        fprintf(stderr, "failed to create app: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt benchmark", bench_width, bench_height);
    if (!window) {
        fprintf(stderr, "failed to create window: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }
    pt_widget *root = pt_window_root(window);
    long rss_start = resident_kib();

    pt_widget *heading = pt_label_create(root, "Fleet overview");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_bold);
    pt_widget *subtitle =
        pt_label_create(root, "A dense list of rows, labels and buttons used to exercise layout "
                              "and software rasterization.");
    pt_widget_set_font_size(subtitle, 13.0f);
    pt_widget_set_label_wrap(subtitle, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_gap(card, 6.0f);
    pt_widget *scroll = pt_scroll_create(card);
    pt_widget_set_height(scroll, 560.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_gap(list, 6.0f);
    pt_widget *first_row = NULL;
    for (int i = 0; i < bench_rows; ++i) {
        pt_widget *row = build_row(list, i);
        if (i == 0) {
            first_row = row;
        }
    }

    size_t nodes = count_nodes(root);
    double build_ms = now_ms() - build_start;
    long rss_tree = resident_kib();

    int scaled_width = bench_width;
    int scaled_height = bench_height;
    uint32_t *pixels = calloc((size_t)scaled_width * (size_t)scaled_height, sizeof(uint32_t));
    if (!pixels) {
        pt_app_destroy(app);
        return 1;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, scaled_width, scaled_height, scaled_width, 1.0f);

    for (int i = 0; i < bench_warmup; ++i) {
        pt_window_paint(window, &canvas);
    }
    long rss_warm = resident_kib();

    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    double layout_total = 0.0;
    double paint_total = 0.0;
    for (int i = 0; i < bench_frames; ++i) {
        window->needs_layout = true;
        window->needs_draw = true;
        double start = now_ms();
        pt_widget_layout(window->root, 0.0f, window->header_height, (float)window->width,
                         (float)window->height - window->header_height, &ctx);
        layout_total += now_ms() - start;
        start = now_ms();
        pt_window_paint(window, &canvas);
        paint_total += now_ms() - start;
    }

    double damage_total = 0.0;
    window->needs_layout = false;
    window->needs_draw = false;
    pt_window_paint(window, &canvas);
    for (int i = 0; i < bench_frames; ++i) {
        pt_widget_damage(first_row, 3.0f);
        double start = now_ms();
        pt_window_paint_damage(window, &canvas);
        damage_total += now_ms() - start;
    }

    double layout_ms = layout_total / bench_frames;
    double paint_ms = paint_total / bench_frames;
    double damage_ms = damage_total / bench_frames;
    long rss = resident_kib();
    printf("nodes: %zu, rows: %d, window: %dx%d\n", nodes, bench_rows, bench_width,
           bench_height);
    printf("build: %.2f ms\n", build_ms);
    printf("layout: %.3f ms/frame (%.0f frames/s from layout)\n", layout_ms,
           1000.0 / (layout_ms > 0.0001 ? layout_ms : 0.0001));
    printf("paint:  %.3f ms/frame (%.0f frames/s)\n", paint_ms,
           1000.0 / (paint_ms > 0.0001 ? paint_ms : 0.0001));
    printf("damage: %.3f ms/frame (%.1f%% of full paint)\n", damage_ms,
           paint_ms > 0.0001 ? damage_ms * 100.0 / paint_ms : 0.0);
    printf("memory: %ld KiB RSS / %ld KiB PSS after %d frames (start: %ld, tree: %ld, warm: %ld)\n",
           rss, proportional_kib(), bench_frames + bench_warmup, rss_start, rss_tree, rss_warm);

    free(pixels);
    pt_app_destroy(app);
    return 0;
}
