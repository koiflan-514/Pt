/*
 * Live input-method probe: opens a window that opts into custom IME delivery
 * and records the preedit and commit text that arrives through text-input-v3.
 * Driven by tools/live-ime.sh.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * A focused text-entry widget owns the composition buffer internally, and Pt
 * exposes no preedit getter for it. A custom IME surface is the public API that
 * reports the whole composition: every preedit update and every commit arrives
 * in the callbacks below. The harness then asserts that a live input method
 * produced at least one non-empty preedit and the expected commit.
 */
#include <pt/pt.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char committed[1024];
static size_t committed_len;
static int preedit_count;
static char last_preedit[512];
static bool focused;

static void on_commit(pt_window *window, const char *text, void *data) {
    (void)window;
    (void)data;
    printf("COMMIT=%s\n", text ? text : "");
    fflush(stdout);
    if (text) {
        size_t length = strlen(text);
        if (committed_len + length < sizeof(committed)) {
            memcpy(committed + committed_len, text, length);
            committed_len += length;
            committed[committed_len] = '\0';
        }
    }
}

static void on_preedit(pt_window *window, const char *text, int32_t cursor_begin,
                       int32_t cursor_end, void *data) {
    (void)window;
    (void)data;
    printf("PREEDIT=%s (%d,%d)\n", text ? text : "", (int)cursor_begin, (int)cursor_end);
    fflush(stdout);
    if (text && *text) {
        snprintf(last_preedit, sizeof(last_preedit), "%s", text);
        preedit_count++;
    }
}

static void on_focus(pt_window *window, bool now_focused, void *data) {
    (void)window;
    (void)data;
    printf("FOCUS=%d\n", now_focused ? 1 : 0);
    fflush(stdout);
    if (now_focused) {
        focused = true;
    }
}

int main(int argc, char **argv) {
    const char *expect = argc > 1 ? argv[1] : "你好";
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.live.ime");
    if (!app) {
        printf("NOAPP %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt IME probe", 420, 160);
    if (!window) {
        printf("NOWINDOW\n");
        pt_app_destroy(app);
        return 1;
    }
    pt_widget *label = pt_label_create(pt_window_root(window), "live IME probe");
    (void)label;
    pt_window_set_focus_callback(window, on_focus, NULL);
    pt_window_set_ime_callbacks(window, on_commit, on_preedit, NULL);
    pt_window_set_ime_enabled(window, true);
    pt_window_set_ime_cursor_rect(window, 8, 8, 2, 18);
    pt_window_show(window);

    /* text-input focus is what makes the compositor forward the composition. */
    for (int i = 0; i < 200 && !focused; ++i) {
        pt_app_run_for(app, 50);
    }
    printf("READY\n");
    fflush(stdout);
    /* Long enough for a live input method to load an engine and compose. */
    pt_app_run_for(app, 6000);

    printf("TEXT=%s\nEXPECT=%s\nPREEDIT_COUNT=%d\nLAST_PREEDIT=%s\n",
           committed_len ? committed : "", expect, preedit_count, last_preedit);
    fflush(stdout);
    int ok = committed_len > 0 && strcmp(committed, expect) == 0 && preedit_count > 0;
    pt_app_destroy(app);
    return ok ? 0 : 1;
}