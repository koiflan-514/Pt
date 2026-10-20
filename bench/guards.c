/*
 * Pt performance guards: median layout, paint, damage, 4K and large-tree
 * scroll measurements with generous regression ceilings.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define guard_batches 5
#define guard_frames 24
#define guard_4k_batches 3
#define guard_4k_frames 5
#define guard_dense_rows 300
#define guard_huge_rows 100000
#define guard_width 1280
#define guard_height 800
#define guard_4k_width 3840
#define guard_4k_height 2160

static int failures;

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

static double cpu_ms(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (double)usage.ru_utime.tv_sec * 1000.0 + (double)usage.ru_utime.tv_usec / 1000.0 +
           (double)usage.ru_stime.tv_sec * 1000.0 + (double)usage.ru_stime.tv_usec / 1000.0;
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
    double means[guard_batches];
    for (int i = 0; i < batches; ++i) {
        means[i] = totals[i] / (double)frames;
    }
    return median(means, batches);
}

static void report_time(const char *name, double value, double limit_ms) {
    bool ok = value <= limit_ms;
    printf("%-30s %9.3f ms   limit %8.1f ms   %s\n", name, value, limit_ms, ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

static void report_ratio(const char *name, double value, double limit) {
    bool ok = value <= limit;
    printf("%-30s %9.3f%%     limit %8.1f%%    %s\n", name, value * 100.0, limit * 100.0,
           ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

static void report_count(const char *name, size_t value, size_t limit, bool at_least) {
    bool ok = at_least ? value >= limit : value <= limit;
    printf("%-30s %9zu       limit %8zu      %s\n", name, value, limit, ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

typedef struct dense_metrics {
    double build_ms;
    double layout_ms;
    double paint_ms;
    double damage_ms;
    uint32_t visits_full;
    uint32_t visits_damage;
    size_t nodes;
} dense_metrics;

static pt_widget *build_dense_row(pt_widget *parent, int index) {
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

static bool dense_scene(int width, int height, pt_app **app_out, pt_window **window_out,
                        pt_widget **first_row_out, uint32_t **pixels_out, pt_canvas *canvas,
                        double *build_ms) {
    double start = now_ms();
    pt_app *app = pt_app_create("pt.guards.dense");
    if (!app) {
        fprintf(stderr, "app create failed: %s\n", pt_get_last_error());
        return false;
    }
    pt_window *window = pt_window_create(app, "Pt guards", width, height);
    if (!window) {
        fprintf(stderr, "window create failed: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return false;
    }
    pt_widget *root = pt_window_root(window);
    pt_label_create(root, "Fleet overview");
    pt_widget *subtitle =
        pt_label_create(root, "Dense rows used to exercise layout and rasterization.");
    pt_widget_set_label_wrap(subtitle, true);
    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_gap(card, 6.0f);
    pt_widget *scroll = pt_scroll_create(card);
    pt_widget_set_height(scroll, 560.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_gap(list, 6.0f);
    pt_widget *first_row = NULL;
    for (int i = 0; i < guard_dense_rows; ++i) {
        pt_widget *row = build_dense_row(list, i);
        if (i == 0) {
            first_row = row;
        }
    }

    uint32_t *pixels = calloc((size_t)width * (size_t)height, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        return false;
    }
    pt_canvas_init(canvas, pixels, width, height, width, 1.0f);
    *build_ms = now_ms() - start;
    *app_out = app;
    *window_out = window;
    *first_row_out = first_row;
    *pixels_out = pixels;
    return true;
}

static dense_metrics measure_dense(int width, int height, bool detail) {
    dense_metrics metrics = {0};
    pt_app *app = NULL;
    pt_window *window = NULL;
    pt_widget *first_row = NULL;
    uint32_t *pixels = NULL;
    pt_canvas canvas;
    double build = 0.0;
    if (!dense_scene(width, height, &app, &window, &first_row, &pixels, &canvas, &build)) {
        ++failures;
        return metrics;
    }
    metrics.build_ms = build;
    metrics.nodes = count_nodes(window->root);

    int warmup = detail ? 20 : 3;
    for (int i = 0; i < warmup; ++i) {
        pt_window_paint(window, &canvas);
    }

    int batches = detail ? guard_batches : guard_4k_batches;
    int frames = detail ? guard_frames : guard_4k_frames;
    double layout_totals[guard_batches] = {0};
    double paint_totals[guard_batches] = {0};
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    for (int batch = 0; batch < batches; ++batch) {
        for (int frame = 0; frame < frames; ++frame) {
            if (detail) {
                window->needs_layout = true;
                window->needs_draw = true;
                double start = now_ms();
                pt_widget_layout(window->root, 0.0f, window->header_height,
                                 (float)window->width,
                                 (float)window->height - window->header_height, &ctx);
                layout_totals[batch] += now_ms() - start;
                start = now_ms();
                pt_window_paint(window, &canvas);
                paint_totals[batch] += now_ms() - start;
            } else {
                double start = now_ms();
                pt_window_paint(window, &canvas);
                paint_totals[batch] += now_ms() - start;
            }
        }
    }
    metrics.layout_ms = detail ? batch_median(layout_totals, batches, frames) : 0.0;
    metrics.paint_ms = batch_median(paint_totals, batches, frames);

    /* One-row damage runs for both scenes: the 4K pass is the guard that a
     * damaged row repaints one tile and walks a slice of the tree instead of
     * the whole visible widget list. */
    double damage_totals[guard_batches] = {0};
    window->needs_layout = false;
    window->needs_draw = false;
    pt_window_paint(window, &canvas);
    metrics.visits_full = window->paint_visits;
    for (int batch = 0; batch < batches; ++batch) {
        for (int frame = 0; frame < frames; ++frame) {
            pt_widget_damage(first_row, 3.0f);
            double start = now_ms();
            pt_window_paint_damage(window, &canvas);
            damage_totals[batch] += now_ms() - start;
        }
    }
    metrics.damage_ms = batch_median(damage_totals, batches, frames);
    metrics.visits_damage = window->paint_visits;

    free(pixels);
    pt_app_destroy(app);
    return metrics;
}

typedef struct view_metrics {
    double build_ms;
    double layout_ms;
    double paint_ms;
    double damage_ms;
    size_t nodes;
    uint32_t visible_tree;
} view_metrics;

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

static view_metrics measure_views(void) {
    view_metrics metrics = {0};
    double start = now_ms();
    pt_app *app = pt_app_create("pt.guards.views");
    if (!app) {
        ++failures;
        return metrics;
    }
    pt_window *window = pt_window_create(app, "Pt guards views", guard_width, guard_height);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_widget *root = pt_window_root(window);
    pt_label_create(root, "Package index");
    pt_widget *table = pt_table_create(root);
    pt_widget_set_height(table, 470.0f);
    pt_table_set_column_count(table, 4);
    for (uint32_t column = 0; column < 4; ++column) {
        char title[16];
        snprintf(title, sizeof(title), "Column %u", column);
        pt_table_set_column_title(table, column, title);
        pt_table_set_column_width(table, column, 180.0f);
    }
    pt_table_set_row_height(table, 32.0f);
    pt_table_set_header_height(table, 38.0f);
    pt_table_set_row_count(table, 10000);
    pt_table_set_on_bind_cell(table, bind_cell, NULL);

    pt_widget *tree = pt_tree_create(root);
    pt_widget_set_height(tree, 220.0f);
    pt_tree_set_row_height(tree, 28.0f);
    for (uint32_t i = 0; i < 100; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "module-%03u", i);
        pt_tree_node *node = pt_tree_add_node(tree, NULL, label);
        for (uint32_t j = 0; j < 9; ++j) {
            snprintf(label, sizeof(label), "unit-%u", j);
            pt_tree_add_node(tree, node, label);
        }
        pt_tree_node_set_expanded(node, true);
    }
    metrics.build_ms = now_ms() - start;

    uint32_t *pixels = calloc((size_t)guard_width * (size_t)guard_height, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, guard_width, guard_height, guard_width, 1.0f);
    for (int i = 0; i < 20; ++i) {
        pt_window_paint(window, &canvas);
    }
    metrics.nodes = count_nodes(root);
    metrics.visible_tree = pt_tree_get_visible_count(tree);

    double layout_totals[guard_batches] = {0};
    double paint_totals[guard_batches] = {0};
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    for (int batch = 0; batch < guard_batches; ++batch) {
        for (int frame = 0; frame < guard_frames; ++frame) {
            window->needs_layout = true;
            window->needs_draw = true;
            double frame_start = now_ms();
            pt_widget_layout(window->root, 0.0f, window->header_height,
                             (float)window->width,
                             (float)window->height - window->header_height, &ctx);
            layout_totals[batch] += now_ms() - frame_start;
            frame_start = now_ms();
            pt_window_paint(window, &canvas);
            paint_totals[batch] += now_ms() - frame_start;
        }
    }
    metrics.layout_ms = batch_median(layout_totals, guard_batches, guard_frames);
    metrics.paint_ms = batch_median(paint_totals, guard_batches, guard_frames);

    pt_widget *header = table->first_child;
    pt_widget *first_row = header ? header->next_sibling : NULL;
    pt_widget *first_cell = first_row ? first_row->first_child : NULL;
    if (first_cell) {
        double damage_totals[guard_batches] = {0};
        window->needs_layout = false;
        window->needs_draw = false;
        pt_window_paint(window, &canvas);
        for (int batch = 0; batch < guard_batches; ++batch) {
            for (int frame = 0; frame < guard_frames; ++frame) {
                pt_widget_damage(first_cell, 3.0f);
                double frame_start = now_ms();
                pt_window_paint_damage(window, &canvas);
                damage_totals[batch] += now_ms() - frame_start;
            }
        }
        metrics.damage_ms = batch_median(damage_totals, guard_batches, guard_frames);
    }

    free(pixels);
    pt_app_destroy(app);
    return metrics;
}

typedef struct scroll_metrics {
    double paint_ms;
    uint32_t visible_nodes;
    size_t row_widgets;
} scroll_metrics;

static scroll_metrics measure_large_tree_scroll(void) {
    scroll_metrics metrics = {0};
    pt_app *app = pt_app_create("pt.guards.scroll");
    if (!app) {
        ++failures;
        return metrics;
    }
    pt_window *window = pt_window_create(app, "Pt guards scroll", 900, 620);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget *tree = pt_tree_create(root);
    pt_widget_set_height(tree, 320.0f);
    pt_tree_set_row_height(tree, 28.0f);
    pt_tree_node *nodes[10200];
    uint32_t count = 0;
    for (uint32_t i = 0; i < 200; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "group-%03u", i);
        pt_tree_node *group = pt_tree_add_node(tree, NULL, label);
        nodes[count++] = group;
        for (uint32_t j = 0; j < 50; ++j) {
            snprintf(label, sizeof(label), "leaf-%u", j);
            nodes[count++] = pt_tree_add_node(tree, group, label);
        }
        pt_tree_node_set_expanded(group, true);
    }

    uint32_t *pixels = calloc(900u * 620u, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, 900, 620, 900, 1.0f);
    for (int i = 0; i < 10; ++i) {
        pt_window_paint(window, &canvas);
    }
    metrics.visible_nodes = pt_tree_get_visible_count(tree);

    double totals[guard_batches] = {0};
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    for (int batch = 0; batch < guard_batches; ++batch) {
        for (int frame = 0; frame < guard_frames; ++frame) {
            uint32_t target = (uint32_t)(batch * guard_frames + frame) * 79u % count;
            window->needs_layout = true;
            window->needs_draw = true;
            double start = now_ms();
            pt_tree_scroll_to_node(tree, nodes[target]);
            pt_widget_layout(window->root, 0.0f, window->header_height,
                             (float)window->width,
                             (float)window->height - window->header_height, &ctx);
            pt_window_paint(window, &canvas);
            totals[batch] += now_ms() - start;
        }
    }
    metrics.paint_ms = batch_median(totals, guard_batches, guard_frames);
    for (pt_widget *child = tree->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_tree_row) {
            ++metrics.row_widgets;
        }
    }

    free(pixels);
    pt_app_destroy(app);
    return metrics;
}

typedef struct huge_metrics {
    double build_ms;
    double scroll_ms;
    double damage_ms;
    size_t row_widgets;
    size_t nodes;
} huge_metrics;

static void bind_huge_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                           void *data) {
    (void)table;
    (void)data;
    char text[48];
    snprintf(text, sizeof(text), "col%u/%05u", column, row);
    pt_widget_set_text(cell, text);
}

/* A 100,000-row table stays virtualized: a scroll frame must bind a viewport of
 * rows, not touch the row count, so the ceiling scales with the viewport. */
static huge_metrics measure_huge_table(void) {
    huge_metrics metrics = {0};
    pt_app *app = pt_app_create("pt.guards.huge");
    if (!app) {
        ++failures;
        return metrics;
    }
    pt_window *window = pt_window_create(app, "Pt guards huge table", guard_width, guard_height);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_widget *root = pt_window_root(window);
    double start = now_ms();
    pt_widget *table = pt_table_create(root);
    pt_widget_set_height(table, 600.0f);
    pt_table_set_column_count(table, 5);
    for (uint32_t column = 0; column < 5; ++column) {
        pt_table_set_column_width(table, column, 200.0f);
    }
    pt_table_set_row_height(table, 28.0f);
    pt_table_set_row_count(table, guard_huge_rows);
    pt_table_set_on_bind_cell(table, bind_huge_cell, NULL);
    metrics.build_ms = now_ms() - start;

    uint32_t *pixels = calloc((size_t)guard_width * (size_t)guard_height, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, guard_width, guard_height, guard_width, 1.0f);
    for (int i = 0; i < 10; ++i) {
        pt_window_paint(window, &canvas);
    }
    metrics.nodes = count_nodes(root);

    double totals[guard_batches] = {0};
    pt_layout_ctx ctx = pt_layout_context_for(window->root);
    for (int batch = 0; batch < guard_batches; ++batch) {
        for (int frame = 0; frame < guard_frames; ++frame) {
            uint32_t row = (uint32_t)(((batch * guard_frames + frame) * 7919u) % guard_huge_rows);
            double frame_start = now_ms();
            pt_table_scroll_to_row(table, row);
            pt_widget_layout(window->root, 0.0f, window->header_height, (float)window->width,
                             (float)window->height - window->header_height, &ctx);
            pt_window_paint(window, &canvas);
            totals[batch] += now_ms() - frame_start;
        }
    }
    metrics.scroll_ms = batch_median(totals, guard_batches, guard_frames);

    pt_widget *header = table->first_child;
    pt_widget *first_row = header ? header->next_sibling : NULL;
    pt_widget *first_cell = first_row ? first_row->first_child : NULL;
    if (first_cell) {
        double damage_totals[guard_batches] = {0};
        window->needs_layout = false;
        window->needs_draw = false;
        pt_window_paint(window, &canvas);
        for (int batch = 0; batch < guard_batches; ++batch) {
            for (int frame = 0; frame < guard_frames; ++frame) {
                pt_widget_damage(first_cell, 3.0f);
                double frame_start = now_ms();
                pt_window_paint_damage(window, &canvas);
                damage_totals[batch] += now_ms() - frame_start;
            }
        }
        metrics.damage_ms = batch_median(damage_totals, guard_batches, guard_frames);
    }

    for (pt_widget *child = table->first_child; child; child = child->next_sibling) {
        if (child->kind == pt_widget_table_row) {
            ++metrics.row_widgets;
        }
    }

    free(pixels);
    pt_app_destroy(app);
    return metrics;
}

typedef struct marquee_metrics {
    double drag_ms;
    uint32_t selected;
    size_t widgets;
} marquee_metrics;

static void bind_marquee_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    (void)list;
    (void)data;
    char text[48];
    snprintf(text, sizeof(text), "Selection record %03u", index);
    pt_widget_set_text(row, text);
}

/* A held drag across a virtualized list: every frame moves the band, applies
 * the new selection to the visible pool and repaints the damaged region. */
static marquee_metrics measure_marquee(void) {
    marquee_metrics metrics = {0};
    pt_app *app = pt_app_create("pt.guards.marquee");
    if (!app) {
        ++failures;
        return metrics;
    }
    pt_window *window = pt_window_create(app, "Pt guards marquee", 960, 640);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_label_create(root, "Selection");
    pt_widget *list = pt_list_view_create(root);
    pt_widget_set_size(list, 900.0f, 540.0f);
    pt_list_view_set_row_height(list, 24.0f);
    pt_list_view_set_row_count(list, 400);
    pt_list_view_set_on_bind_row(list, bind_marquee_row, NULL);

    uint32_t *pixels = calloc(960u * 640u, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, 960, 640, 960, 1.0f);
    pt_window_show(window);
    for (int i = 0; i < 10; ++i) {
        pt_window_paint(window, &canvas);
    }

    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(list, &x, &y, &w, &h);
    float anchor_x = x + 40.0f;
    float anchor_y = y + 24.0f * 1.5f;
    float drag_x = x + w - 40.0f;
    float bottom_y = y + h - 24.0f;
    pt_window_input_pointer_enter(window, anchor_x, anchor_y);
    pt_window_input_pointer_button(window, 0x110u, true, anchor_x, anchor_y, 1, 100);
    pt_window_input_pointer_motion(window, drag_x, bottom_y, 110);
    pt_window_paint(window, &canvas);
    metrics.selected = pt_list_view_get_selection_count(list);
    pt_window_input_pointer_button(window, 0x110u, false, drag_x, bottom_y, 2, 120);
    metrics.widgets = count_nodes(root);

    pt_window_input_pointer_button(window, 0x110u, true, anchor_x, anchor_y, 3, 200);
    double totals[guard_batches] = {0};
    for (int batch = 0; batch < guard_batches; ++batch) {
        for (int frame = 0; frame < guard_frames; ++frame) {
            float target = anchor_y + (float)(frame % 16 + 2) * 24.0f;
            double start = now_ms();
            pt_window_input_pointer_motion(window, drag_x, target, 300u + (uint32_t)frame);
            pt_window_paint_damage(window, &canvas);
            totals[batch] += now_ms() - start;
        }
    }
    pt_window_input_pointer_button(window, 0x110u, false, drag_x, anchor_y, 4, 900);
    metrics.drag_ms = batch_median(totals, guard_batches, guard_frames);
    metrics.selected = pt_list_view_get_selection_count(list);

    free(pixels);
    pt_app_destroy(app);
    return metrics;
}

typedef struct popup_metrics {
    double frame_ms;
    size_t widgets;
    int width;
    int height;
} popup_metrics;

/* Native popup surfaces are not available without a compositor, but their
 * expensive half is platform-independent: lay out the overlay panel in
 * surface-local coordinates and repaint only the panel into its own canvas,
 * exactly as popup_draw does before the shm copy. */
static popup_metrics measure_popup_surface(void) {
    popup_metrics metrics = {0};
    pt_app *app = pt_app_create("pt.guards.popup");
    if (!app) {
        ++failures;
        return metrics;
    }
    pt_window *window = pt_window_create(app, "Pt guards popup", 900, 700);
    if (!window) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget *combobox = pt_combobox_create(root);
    pt_widget_set_size(combobox, 240.0f, 34.0f);
    pt_widget_set_align_self(combobox, pt_align_start);
    pt_combobox_set_mode(combobox, pt_combobox_select_only);
    for (uint32_t i = 0; i < 80; ++i) {
        char label[48];
        snprintf(label, sizeof(label), "Option %03u", i);
        pt_combobox_add_item(combobox, label);
    }
    pt_combobox_set_popup_max_height(combobox, 320.0f);

    uint32_t *pixels = calloc(900u * 700u, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        ++failures;
        return metrics;
    }
    pt_canvas window_canvas;
    pt_canvas_init(&window_canvas, pixels, 900, 700, 900, 1.0f);
    pt_window_show(window);
    for (int i = 0; i < 8; ++i) {
        pt_window_paint(window, &window_canvas);
    }

    pt_combobox_open(combobox);
    pt_combobox_state *state = combobox->extra;
    pt_overlay *overlay = state ? state->popup : NULL;
    pt_canvas *panel_canvas = NULL;
    if (overlay && overlay->panel) {
        /* Surface-local layout is what the native path uses. */
        overlay->native = true;
        pt_layout_ctx ctx = pt_layout_context_for(root);
        pt_window_overlays_layout(window, &ctx);
        metrics.width = (int)lroundf(overlay->rect.w);
        metrics.height = (int)lroundf(overlay->rect.h);
        if (metrics.width < 1) {
            metrics.width = 1;
        }
        if (metrics.height < 1) {
            metrics.height = 1;
        }
        panel_canvas = pt_canvas_create(metrics.width, metrics.height, 1.0f);
        if (panel_canvas) {
            for (int i = 0; i < 10; ++i) {
                pt_canvas_clear(panel_canvas, pt_color_transparent);
                pt_window_overlays_layout(window, &ctx);
                pt_widget_paint(overlay->panel, panel_canvas, &ctx);
            }
            metrics.widgets = count_nodes(overlay->panel);
            double totals[guard_batches] = {0};
            for (int batch = 0; batch < guard_batches; ++batch) {
                for (int frame = 0; frame < guard_frames; ++frame) {
                    double start = now_ms();
                    pt_canvas_clear(panel_canvas, pt_color_transparent);
                    pt_window_overlays_layout(window, &ctx);
                    pt_widget_paint(overlay->panel, panel_canvas, &ctx);
                    totals[batch] += now_ms() - start;
                }
            }
            metrics.frame_ms = batch_median(totals, guard_batches, guard_frames);
        }
    } else {
        ++failures;
    }

    pt_canvas_destroy(panel_canvas);
    pt_combobox_close(combobox);
    free(pixels);
    pt_app_destroy(app);
    return metrics;
}

static void measure_startup(double *first_frame_ms, double *idle_cpu_ms) {
    pt_app *app = pt_app_create("pt.guards.startup");
    if (!app) {
        *first_frame_ms = 1e9;
        *idle_cpu_ms = 1e9;
        ++failures;
        return;
    }
    pt_window *window = pt_window_create(app, "Pt guards startup", 800, 600);
    if (!window) {
        pt_app_destroy(app);
        *first_frame_ms = 1e9;
        *idle_cpu_ms = 1e9;
        ++failures;
        return;
    }
    pt_widget *card = pt_card_create(pt_window_root(window));
    pt_label_create(card, "startup");
    uint32_t *pixels = calloc(800u * 600u, sizeof(*pixels));
    if (!pixels) {
        pt_app_destroy(app);
        *first_frame_ms = 1e9;
        *idle_cpu_ms = 1e9;
        ++failures;
        return;
    }
    pt_canvas canvas;
    pt_canvas_init(&canvas, pixels, 800, 600, 800, 1.0f);
    double start = now_ms();
    pt_window_paint(window, &canvas);
    *first_frame_ms = now_ms() - start;

    double cpu_start = cpu_ms();
    pt_app_run_for(app, 300);
    *idle_cpu_ms = cpu_ms() - cpu_start;
    free(pixels);
    pt_app_destroy(app);
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
    printf("perf guards; thresholds scaled by %.2f\n", scale);

    double first_frame = 0.0;
    double idle_cpu = 0.0;
    measure_startup(&first_frame, &idle_cpu);
    report_time("startup first 800x600 frame", first_frame, 250.0 * scale);
    report_time("idle 300 ms CPU", idle_cpu, 50.0 * scale);

    marquee_metrics marquee = measure_marquee();
    printf("marquee scene: %zu widgets, %u selected rows\n", marquee.widgets,
           marquee.selected);
    report_count("marquee scene widgets", marquee.widgets, 20, true);
    report_count("marquee selected rows", marquee.selected, 8, true);
    report_time("marquee drag frame", marquee.drag_ms, 20.0 * scale);

    popup_metrics popup = measure_popup_surface();
    printf("popup scene: %dx%d surface, %zu widgets\n", popup.width, popup.height,
           popup.widgets);
    report_count("popup panel widgets", popup.widgets, 20, true);
    report_time("popup surface frame", popup.frame_ms, 20.0 * scale);

    dense_metrics dense = measure_dense(guard_width, guard_height, true);
    printf("dense scene: %zu nodes, build %.2f ms\n", dense.nodes, dense.build_ms);
    report_count("dense nodes", dense.nodes, 1400, true);
    report_time("dense layout frame", dense.layout_ms, 25.0 * scale);
    report_time("dense full paint frame", dense.paint_ms, 100.0 * scale);
    report_time("dense one-row damage", dense.damage_ms, 10.0 * scale);
    if (dense.paint_ms > 0.0) {
        report_ratio("dense damage share", dense.damage_ms / dense.paint_ms, 0.30);
    }
    if (dense.visits_full > 0) {
        printf("dense tree traversal: %u nodes painted per full frame, %u per damaged frame\n",
               dense.visits_full, dense.visits_damage);
        report_ratio("dense damage traversal share",
                     (double)dense.visits_damage / (double)dense.visits_full, 0.35);
    }

    dense_metrics uhd = measure_dense(guard_4k_width, guard_4k_height, false);
    report_time("4K full paint frame", uhd.paint_ms, (dense.paint_ms * 14.0 + 30.0) * scale);
    if (dense.paint_ms > 0.0) {
        printf("%-30s %9.2fx\n", "4K paint vs 1280x800", uhd.paint_ms / dense.paint_ms);
    }
    report_time("4K one-row damage", uhd.damage_ms, 12.0 * scale);
    if (uhd.paint_ms > 0.0) {
        report_ratio("4K damage share", uhd.damage_ms / uhd.paint_ms, 0.12);
    }
    if (uhd.visits_full > 0) {
        printf("4K tree traversal: %u nodes painted per full frame, %u per damaged frame\n",
               uhd.visits_full, uhd.visits_damage);
        report_ratio("4K damage traversal share",
                     (double)uhd.visits_damage / (double)uhd.visits_full, 0.35);
    }

    view_metrics views = measure_views();
    printf("views scene: %zu widgets, %u visible tree rows\n", views.nodes, views.visible_tree);
    report_count("views materialized widgets", views.nodes, 130, false);
    report_count("views visible tree nodes", views.visible_tree, 1000, true);
    report_time("views layout frame", views.layout_ms, 2.0 * scale);
    report_time("views full paint frame", views.paint_ms, 80.0 * scale);
    report_time("views one-cell damage", views.damage_ms, 5.0 * scale);
    if (views.paint_ms > 0.0) {
        report_ratio("views damage share", views.damage_ms / views.paint_ms, 0.25);
    }

    scroll_metrics scroll = measure_large_tree_scroll();
    printf("scroll scene: %u visible nodes, %zu row widgets\n", scroll.visible_nodes,
           scroll.row_widgets);
    report_count("scroll visible nodes", scroll.visible_nodes, 10200, true);
    report_count("scroll row widgets", scroll.row_widgets, 24, false);
    report_time("large-tree scroll frame", scroll.paint_ms, 100.0 * scale);

    huge_metrics huge = measure_huge_table();
    printf("100k table scene: %zu widgets, %zu materialized rows, build %.2f ms\n", huge.nodes,
           huge.row_widgets, huge.build_ms);
    report_count("100k table row widgets", huge.row_widgets, 40, false);
    report_time("100k table scroll frame", huge.scroll_ms, 20.0 * scale);
    report_time("100k table one-cell damage", huge.damage_ms, 6.0 * scale);
    if (huge.scroll_ms > 0.0) {
        report_ratio("100k damage share", huge.damage_ms / huge.scroll_ms, 0.60);
    }

    if (failures) {
        printf("perf guards: %d failure(s)\n", failures);
        return 1;
    }
    printf("perf guards: all ceilings hold\n");
    return 0;
}
