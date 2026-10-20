/*
 * Live typing probe: opens a window with a focused entry and reports the text
 * that arrives through the compositor. Driven by tools/live-typing.sh.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */
#include "wayland/wayland_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *expect = argc > 1 ? argv[1] : "ptdirect";
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.live.typing");
    if (!app) {
        printf("NOAPP %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt typing probe", 360, 120);
    if (!window) {
        printf("NOWINDOW\n");
        return 1;
    }
    pt_widget *entry = pt_entry_create(pt_window_root(window), "");
    pt_widget_set_height(entry, 32.0f);
    pt_window_show(window);
    if (!pt_widget_focus(entry)) {
        printf("NOFOCUSENTRY\n");
        fflush(stdout);
        pt_app_destroy(app);
        return 2;
    }
    /* Wait for a stable compositor focus: the input method and the normal key
     * path are only meaningful while wl_keyboard focus is held. */
    int stable = 0;
    for (int i = 0; i < 200 && !stable; ++i) {
        if (window->keyboard_focused) {
            pt_app_run_for(app, 200);
            stable = window->keyboard_focused;
        } else {
            pt_app_run_for(app, 50);
        }
    }
    if (!stable) {
        printf("NOFOCUS\n");
        fflush(stdout);
        pt_app_destroy(app);
        return 2;
    }
    printf("READY\n");
    fflush(stdout);
    pt_app_run_for(app, 4000);
    const char *text = pt_widget_get_text(entry);
    printf("TEXT=%s\nEXPECT=%s\n", text ? text : "(null)", expect);
    fflush(stdout);
    int ok = text && strcmp(text, expect) == 0;
    pt_app_destroy(app);
    return ok ? 0 : 1;
}
