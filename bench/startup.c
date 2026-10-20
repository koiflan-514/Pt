/*
 * Pt startup and idle benchmark: process, first frame and event loop cost.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
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

static double cpu_ms(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (double)usage.ru_utime.tv_sec * 1000.0 + (double)usage.ru_utime.tv_usec / 1000.0 +
           (double)usage.ru_stime.tv_sec * 1000.0 + (double)usage.ru_stime.tv_usec / 1000.0;
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    double start = now_ms();
    pt_app *app = pt_app_create("pt.benchmark.startup");
    if (!app) {
        fprintf(stderr, "failed to create app: %s\n", pt_get_last_error());
        return 1;
    }
    double app_ms = now_ms() - start;
    long rss_app = resident_kib();

    start = now_ms();
    pt_window *window = pt_window_create(app, "Pt", 800, 600);
    pt_widget *root = pt_window_root(window);
    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 24.0f, 24.0f, 24.0f, 24.0f);
    pt_label_create(card, "Hello, Pt");
    pt_label_create(card, "Startup and idle cost");
    pt_button_create(card, "Action");
    pt_window_show(window);
    double tree_ms = now_ms() - start;
    long rss_tree = resident_kib();

    pt_canvas *canvas = pt_canvas_create(800, 600, 1.0f);
    start = now_ms();
    pt_window_paint(window, canvas);
    double frame_ms = now_ms() - start;
    long rss = resident_kib();

    double cpu_before = cpu_ms();
    start = now_ms();
    pt_app_run_for(app, 500);
    double idle_wall = now_ms() - start;
    double idle_cpu = cpu_ms() - cpu_before;

    printf("app create: %.2f ms (rss %ld KiB)\n", app_ms, rss_app);
    printf("window+tree: %.2f ms (rss %ld KiB)\n", tree_ms, rss_tree);
    printf("first frame: %.2f ms (rss %ld KiB, pss %ld KiB)\n", frame_ms, rss,
           proportional_kib());
    printf("idle 500 ms: wall %.1f ms, cpu %.3f ms\n", idle_wall, idle_cpu);

    pt_canvas_destroy(canvas);
    pt_app_destroy(app);
    return 0;
}
