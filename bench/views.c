/*
 * Pt data-view micro-benchmark: a 10,000-row table and a 1,000-node tree.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define view_width 1280
#define view_height 800
#define table_rows 10000u
#define tree_roots 100u
#define tree_children 9u
#define view_warmup 20
#define view_frames 200

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

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    (void)table;
    (void)data;
    char text[64];
    if (column == 0) {
        snprintf(text, sizeof(text), "package-%05u.tar.zst", row);
    } else if (column == 1) {
        snprintf(text, sizeof(text), "%u cells", (row * 37u) % 900u + 10u);
    } else if (column == 2) {
        snprintf(text, sizeof(text), "%u.%u MiB", row % 48u, row % 10u);
    } else {
        snprintf(text, sizeof(text), "2026-%02u-%02u", row % 12u + 1u, row % 28u + 1u);
    }
    pt_widget_set_text(cell, text);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    double build_start = now_ms();
    pt_app *app = pt_app_create("pt.benchmark.views");
    if (!app) {
        fprintf(stderr, "failed to create app: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt data views", view_width, view_height);
    if (!window) {
        fprintf(stderr, "failed to create window: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }
    pt_widget *root = pt_window_root(window);
    long rss_start = resident_kib();

    pt_widget *heading = pt_label_create(root, "Package index");
    pt_widget_set_font_size(heading, 24.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_bold);

    pt_widget *table = pt_table_create(root);
    pt_widget_set_height(table, 470.0f);
    pt_table_set_column_count(table, 4);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Contents");
    pt_table_set_column_title(table, 2, "Size");
    pt_table_set_column_title(table, 3, "Modified");
    pt_table_set_column_width(table, 0, 360.0f);
    pt_table_set_column_width(table, 1, 180.0f);
    pt_table_set_column_width(table, 2, 140.0f);
    pt_table_set_column_width(table, 3, 180.0f);
    pt_table_set_row_height(table, 32.0f);
    pt_table_set_header_height(table, 38.0f);
    pt_table_set_row_count(table, table_rows);
    pt_table_set_on_bind_cell(table, bind_cell, NULL);
    pt_table_set_sort(table, 0, pt_sort_ascending);

    pt_widget *tree = pt_tree_create(root);
    pt_widget_set_height(tree, 220.0f);
    pt_tree_set_row_height(tree, 28.0f);
    pt_tree_set_indent(tree, 20.0f);
    for (uint32_t i = 0; i < tree_roots; ++i) {
        char label[64];
        snprintf(label, sizeof(label), "module-%03u", i);
        pt_tree_node *node = pt_tree_add_node(tree, NULL, label);
        for (uint32_t j = 0; j < tree_children; ++j) {
            snprintf(label, sizeof(label), "unit-%u", j);
            pt_tree_add_node(tree, node, label);
        }
        pt_tree_node_set_expanded(node, true);
    }

    double build_ms = now_ms() - build_start;
    long rss_tree = resident_kib();

    uint32_t *pixels = calloc((size_t)view_width * (size_t)view_height, sizeof(uint32_t));
    if (!pixels) {
        pt_app_destroy(app);
        return 1;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, view_width, view_height, view_width, 1.0f);

    for (int i = 0; i < view_warmup; ++i) {
        pt_window_paint(window, &canvas);
    }
    size_t nodes = count_nodes(root);
    long rss_warm = resident_kib();

    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    double layout_total = 0.0;
    double paint_total = 0.0;
    for (int i = 0; i < view_frames; ++i) {
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

    pt_widget *header = table->first_child;
    pt_widget *first_row = header ? header->next_sibling : NULL;
    pt_widget *first_cell = first_row ? first_row->first_child : NULL;
    double damage_total = 0.0;
    window->needs_layout = false;
    window->needs_draw = false;
    pt_window_paint(window, &canvas);
    if (first_cell) {
        for (int i = 0; i < view_frames; ++i) {
            pt_widget_damage(first_cell, 3.0f);
            double start = now_ms();
            pt_window_paint_damage(window, &canvas);
            damage_total += now_ms() - start;
        }
    }

    double layout_ms = layout_total / view_frames;
    double paint_ms = paint_total / view_frames;
    double damage_ms = damage_total / view_frames;
    long rss = resident_kib();
    printf("table rows: %u, tree nodes: %u, visible tree rows: %u, widgets: %zu, window: %dx%d\n",
           table_rows, tree_roots * (tree_children + 1u), pt_tree_get_visible_count(tree), nodes,
           view_width, view_height);
    printf("build: %.2f ms\n", build_ms);
    printf("layout: %.3f ms/frame (%.0f frames/s from layout)\n", layout_ms,
           1000.0 / (layout_ms > 0.0001 ? layout_ms : 0.0001));
    printf("paint:  %.3f ms/frame (%.0f frames/s)\n", paint_ms,
           1000.0 / (paint_ms > 0.0001 ? paint_ms : 0.0001));
    printf("damage: %.3f ms/frame (%.1f%% of full paint)\n", damage_ms,
           paint_ms > 0.0001 ? damage_ms * 100.0 / paint_ms : 0.0);
    printf("memory: %ld KiB RSS / %ld KiB PSS after %d frames (start: %ld, tree: %ld, warm: %ld)\n",
           rss, proportional_kib(), view_frames + view_warmup, rss_start, rss_tree, rss_warm);

    free(pixels);
    pt_app_destroy(app);
    return 0;
}
