/*
 * Live RTL caret/selection probe (2.0 wave 7).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Driven by tools/check-rtl-live.sh inside a nested niri compositor. The entry
 * starts with mixed RTL text; the harness types one ASCII byte through
 * zwp_virtual_keyboard_v1 so the wire path is exercised, then this probe walks
 * the visual caret with Right/Left/Home/End and Shift+Right and compares the
 * IME rectangle against the visual map. The live surface is also painted into
 * an offscreen canvas to confirm the entry renders ink.
 *
 * Pixel-level screenshot readback is not available through the public protocol
 * surface, so the check compares the IME rectangle (the compositor-facing caret
 * position) with the visual map, and the nested compositor run proves the
 * window survived on the wire. That is the documented fallback for P3.
 */
#include "../src/internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define live_hebrew "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"
#define live_mixed "abc " live_hebrew " 123"

static int failures;

static void check(bool condition, const char *what) {
    if (!condition) {
        ++failures;
        printf("FAIL %s\n", what);
    } else {
        printf("PASS %s\n", what);
    }
}

static float caret_visual_x(pt_widget *entry) {
    int x = 0;
    pt_widget_ime_rect(entry, &x, NULL, NULL, NULL);
    const char *text = entry->text ? entry->text : "";
    size_t length = strlen(text);
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    float origin = -entry->scroll_x;
    pt_bidi_base base = pt_widget_text_base(entry, text, length);
    if (font && base == pt_bidi_base_rtl) {
        float area_w = pt_max_float(0.0f, entry->width - entry->padding_left -
                                              entry->padding_right);
        origin = area_w - pt_font_measure_bidi(font, text, length, base) / scale +
                 entry->scroll_x;
    }
    return (float)x - entry->x - entry->padding_left - origin;
}

static float expected_caret_x(pt_widget *entry, const pt_text_visual_map *map) {
    if (!map) {
        return 0.0f;
    }
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    float scale = ctx.scale > 0.0f ? ctx.scale : 1.0f;
    return pt_text_visual_map_x_for_position(map, entry->cursor,
                                             (pt_text_bias)entry->caret_affinity) /
           scale;
}

static void check_caret(pt_widget *entry, const pt_text_visual_map *map, const char *label) {
    check(entry->cursor <= strlen(entry->text ? entry->text : ""), "cursor in range");
    float actual = caret_visual_x(entry);
    check(isfinite(actual), "ime caret x finite");
    if (map && pt_text_visual_map_is_boundary(map, entry->cursor)) {
        char message[96];
        snprintf(message, sizeof(message), "%s: ime rect matches visual map", label);
        check(fabsf(actual - expected_caret_x(entry, map)) <= 1.0f, message);
    }
}

static int selection_spans_ok(pt_widget *entry, const pt_text_visual_map *map) {
    if (!map) {
        return 1;
    }
    uint32_t from = 0;
    uint32_t to = 0;
    if (!pt_entry_get_selection(entry, &from, &to)) {
        return 1;
    }
    uint32_t start = from < to ? from : to;
    uint32_t end = from < to ? from : to;
    if (from < to) {
        start = from;
        end = to;
    } else {
        start = to;
        end = from;
    }
    float *spans = calloc(map->count * 2u + 1u, sizeof(*spans));
    if (!spans) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < map->count; ++i) {
        const pt_text_visual_cluster *cluster = &map->clusters[i];
        if (cluster->end <= start || cluster->start >= end || cluster->x1 <= cluster->x0) {
            continue;
        }
        size_t j = count;
        while (j > 0 && spans[(j - 1u) * 2u] > cluster->x0) {
            spans[j * 2u] = spans[(j - 1u) * 2u];
            spans[j * 2u + 1u] = spans[(j - 1u) * 2u + 1u];
            --j;
        }
        spans[j * 2u] = cluster->x0;
        spans[j * 2u + 1u] = cluster->x1;
        ++count;
    }
    float previous = -1.0f;
    int ok = 1;
    for (size_t i = 0; i < count; ++i) {
        float x0 = spans[i * 2u];
        float x1 = spans[i * 2u + 1u];
        if (x0 < -0.01f || x1 > map->width + 0.05f || x0 < previous - 0.01f) {
            ok = 0;
        }
        previous = x1;
    }
    free(spans);
    return ok;
}

static int wait_for_typed_text(pt_app *app, pt_widget *entry, size_t baseline) {
    for (int i = 0; i < 300; ++i) {
        pt_app_run_for(app, 20);
        const char *text = pt_widget_get_text(entry);
        if (text && strlen(text) > baseline) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.live.rtl");
    if (!app) {
        printf("NOAPP %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_font_family(app, "DejaVu Sans");
    pt_window *window = pt_window_create(app, "Pt RTL live probe", 560, 200);
    if (!window) {
        printf("NOWINDOW\n");
        pt_app_destroy(app);
        return 1;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget *entry = pt_entry_create(root, "");
    pt_widget_set_size(entry, 520.0f, 34.0f);
    pt_widget_set_font_size(entry, 16.0f);
    pt_widget_set_align_self(entry, pt_align_start);
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget_set_text(entry, live_mixed);
    pt_window_show(window);
    if (!pt_widget_focus(entry)) {
        printf("NOFOCUSENTRY\n");
        pt_app_destroy(app);
        return 2;
    }
    int focused = 0;
    for (int i = 0; i < 200 && !focused; ++i) {
        if (window->keyboard_focused) {
            pt_app_run_for(app, 200);
            focused = window->keyboard_focused;
        } else {
            pt_app_run_for(app, 50);
        }
    }
    if (!focused) {
        printf("NOFOCUS\n");
        pt_app_destroy(app);
        return 2;
    }
    printf("READY\n");
    fflush(stdout);
    size_t baseline = strlen(pt_widget_get_text(entry));
    if (!wait_for_typed_text(app, entry, baseline)) {
        printf("FAIL typed byte did not arrive through the wire\n");
        pt_app_destroy(app);
        return 1;
    }
    printf("TYPED=%s\n", pt_widget_get_text(entry));

    const char *text = pt_widget_get_text(entry);
    size_t length = strlen(text);
    pt_layout_ctx ctx = pt_layout_context_for(entry);
    pt_font *font = pt_widget_font_for(entry, &ctx);
    pt_bidi_base base = pt_widget_text_base(entry, text, length);
    pt_text_visual_map *map = NULL;
    if (!font || !pt_text_visual_map_build_bidi(font, text, length, base, &map)) {
        printf("FAIL visual map\n");
        pt_app_destroy(app);
        return 1;
    }
    check(map->width > 0.0f, "visual map width positive");

    pt_entry_set_selection(entry, 0, 0);
    pt_window_input_key(window, XKB_KEY_End, 0, NULL, true);
    check_caret(entry, map, "End");
    pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
    check_caret(entry, map, "Right at visual edge");
    pt_window_input_key(window, XKB_KEY_Home, 0, NULL, true);
    check_caret(entry, map, "Home");
    for (int i = 0; i < 5; ++i) {
        pt_window_input_key(window, XKB_KEY_Right, 0, NULL, true);
        check_caret(entry, map, "Right");
    }
    for (int i = 0; i < 5; ++i) {
        pt_window_input_key(window, XKB_KEY_Left, 0, NULL, true);
        check_caret(entry, map, "Left");
    }
    pt_entry_set_selection(entry, 0, 0);
    pt_window_input_key(window, XKB_KEY_Right, 1u, NULL, true);
    pt_window_input_key(window, XKB_KEY_Right, 1u, NULL, true);
    check(entry->selection_anchor == 0u, "Shift+Right keeps anchor");
    check(selection_spans_ok(entry, map), "selection spans ordered and in bounds");
    uint32_t from = 0;
    uint32_t to = 0;
    check(pt_entry_get_selection(entry, &from, &to) && from == 0u, "selection readable");

    pt_canvas *canvas = pt_canvas_create(560, 200, 1.0f);
    if (!canvas || !pt_window_paint(window, canvas)) {
        check(false, "live window paint");
    } else {
        uint32_t ink = 0;
        uint32_t background = pt_canvas_get_pixel(canvas, 2, 2);
        for (int y = (int)entry->y; y < (int)(entry->y + entry->height); ++y) {
            for (int x = (int)entry->x; x < (int)(entry->x + entry->width); ++x) {
                if (pt_canvas_get_pixel(canvas, x, y) != background) {
                    ++ink;
                }
            }
        }
        check(ink > 100u, "entry rendered ink");
    }
    pt_canvas_destroy(canvas);
    pt_text_visual_map_free(map);
    printf("RESULT %s\n", failures ? "FAIL" : "PASS");
    fflush(stdout);
    pt_app_destroy(app);
    return failures ? 1 : 0;
}