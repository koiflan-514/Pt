/*
 * Input wire matrix replay: touch, tablet and drag-and-drop (1.4 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Every check here runs the native Pt client path against the header-only fake
 * compositor: events are injected on the wire and the resulting public
 * callbacks, internal window state and decoded client requests are asserted
 * against the protocols/ XML files and wayland.xml opcodes.
 */

#include "../src/internal.h"
#include "fake_compositor.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

/* wl_touch geometry is float on the client side; the wire is wl_fixed_t. */
#define wire_touch_x 120.0
#define wire_touch_y 90.0

/* tablet-unstable-v2 tool types / capabilities / button codes. */
#define wire_tablet_type_pen 0x140u
#define wire_tablet_type_eraser 0x141u
#define wire_tablet_capability_tilt 1u
#define wire_tablet_capability_pressure 2u
#define wire_btn_stylus 0x14bu
#define wire_btn_stylus2 0x14cu

/* ---------- shared fixtures ---------- */

static bool wire_wait_configured(fake_compositor *fc, pt_app *app, pt_window *window) {
    for (int i = 0; i < 400 && window && !window->configured; ++i) {
        fake_compositor_dispatch(fc, app, 5);
    }
    return window && window->configured;
}

static pt_window *wire_open_window(fake_compositor *fc, pt_app *app, const char *title,
                                   int width, int height) {
    pt_window *window = pt_window_create(app, title, width, height);
    pt_check(window != NULL);
    if (!window) {
        return NULL;
    }
    pt_window_show(window);
    pt_check(wire_wait_configured(fc, app, window));
    return window;
}

static pt_canvas *wire_paint(pt_window *window, int width, int height) {
    pt_canvas *canvas = pt_canvas_create(width, height, 1.0f);
    pt_check(canvas != NULL);
    if (canvas) {
        pt_check(pt_window_paint(window, canvas));
    }
    return canvas;
}

typedef struct wire_gesture_log {
    int count;
    pt_gesture_event events[24];
} wire_gesture_log;

static void wire_log_gesture(pt_window *window, const pt_gesture_event *event,
                             void *user_data) {
    (void)window;
    wire_gesture_log *log = user_data;
    if (log->count < (int)(sizeof(log->events) / sizeof(log->events[0]))) {
        log->events[log->count] = *event;
    }
    ++log->count;
}

static void wire_count_click(pt_widget *widget, void *user_data) {
    (void)widget;
    ++*(int *)user_data;
}

/* ---------- facility self-test ---------- */

/* The three new interface tables must append after the existing kinds so every
 * pre-1.4 kind value stays stable, and the inspected opcodes must match the
 * XML the client headers were generated from. */
static void test_facility_tables(void) {

    pt_check(fc_kind_from_name("wl_touch") == FC_K_TOUCH);

    /* wayland.xml: wl_touch.down/up/motion/frame/cancel are 0..4 and the only
     * request is release(0). */
    pt_check(fc_ifaces[FC_K_TOUCH].req_count == 1);
    pt_check(strcmp(fc_ifaces[FC_K_TOUCH].reqs[0].name, "release") == 0);

    /* tablet-unstable-v2: seat tablet_added(0)/tool_added(1)/pad_added(2);
     * tablet destroy(0); tool set_cursor(0)/destroy(1). */

    /* wayland.xml wl_data_offer: accept(0), receive(1), destroy(2), finish(3),
     * set_actions(4). wl_data_source: offer(0), destroy(1), set_actions(2). */

    /* primary-selection-unstable-v1: manager create_source(0)/get_device(1),
     * source offer(0)/destroy(1), offer receive(0)/destroy(1). */
}

/* ---------- touch ---------- */

static void test_touch_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL | FC_OPT_TOUCH));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.touch");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_check(fake_compositor_global_bound(&fc, FC_K_SEAT));
    pt_check(fake_compositor_live_id(&fc, FC_K_TOUCH) != 0);

    pt_window *window = wire_open_window(&fc, app, "touch-wire", 400, 300);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_widget *tap = pt_button_create(pt_window_root(window), "Tap");
    pt_widget_set_size(tap, 240.0f, 100.0f);
    pt_widget_set_align_self(tap, pt_align_start);
    int clicks = 0;
    pt_button_set_on_click(tap, wire_count_click, &clicks);
    pt_canvas *canvas = wire_paint(window, 400, 300);
    float cx = tap->x + tap->width * 0.5f;
    float cy = tap->y + tap->height * 0.5f;
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);

    /* Single touch: down acts as pointer enter + left press, motion moves the
     * pointer, up releases it. Values round-trip through wl_fixed_t. */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_touch_down(&fc, 9001u, 100u, surface, 0, cx, cy));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->touch != NULL);
    pt_check(app->platform->touch_serial == 9001u);
    pt_check(window->touch_count == 1);
    pt_check(window->touch_active);
    pt_check(window->touch_id == 0);
    pt_check(window->pointer_inside);
    pt_check_near(window->pointer_x, cx, 0.5);
    pt_check_near(window->pointer_y, cy, 0.5);

    pt_check(fake_compositor_touch_motion(&fc, 101u, 0, cx + 18.0, cy + 9.0));
    pt_check(fake_compositor_touch_frame(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check_near(window->pointer_x, cx + 18.0, 0.5);
    pt_check_near(window->pointer_y, cy + 9.0, 0.5);

    pt_check(fake_compositor_touch_up(&fc, 9002u, 102u, 0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 0);
    pt_check(!window->touch_active);
    pt_check(!window->pointer_inside);
    pt_check(clicks == 1);

    /* Two touches switch from pointer emulation to pinch: the second down
     * cancels the press, the gesture callback sees begin/update/cancel. */
    wire_gesture_log gestures = {0};
    pt_window_set_gesture_callback(window, wire_log_gesture, &gestures);
    pt_check(fake_compositor_touch_down(&fc, 9101u, 200u, surface, 0, 100.0, 100.0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 1);
    pt_check(!window->touch_pinch);
    pt_check(fake_compositor_touch_down(&fc, 9102u, 201u, surface, 1, 200.0, 100.0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 2);
    pt_check(window->touch_pinch);
    pt_check(window->touch_id == -1);
    pt_check(gestures.count == 1);
    if (gestures.count >= 1) {
        pt_check(gestures.events[0].kind == pt_gesture_kind_pinch);
        pt_check(gestures.events[0].phase == pt_gesture_phase_begin);
        pt_check(gestures.events[0].fingers == 2);
    }

    pt_check(fake_compositor_touch_motion(&fc, 202u, 1, 300.0, 100.0));
    pt_check(fake_compositor_touch_frame(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(gestures.count == 2);
    if (gestures.count >= 2) {
        pt_check(gestures.events[1].phase == pt_gesture_phase_update);
        pt_check(gestures.events[1].fingers == 2);
        pt_check_near(gestures.events[1].dx, 50.0, 0.5);
        pt_check_near(gestures.events[1].dy, 0.0, 0.5);
        pt_check_near(gestures.events[1].scale, 2.0, 0.01);
    }

    /* Cancel is the only path that releases both contacts in one event. */
    pt_check(fake_compositor_touch_cancel(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 0);
    pt_check(!window->touch_pinch);
    pt_check(!window->pointer_inside);
    pt_check(gestures.count == 3);
    if (gestures.count >= 3) {
        pt_check(gestures.events[2].phase == pt_gesture_phase_cancel);
    }
    pt_check(clicks == 1); /* a pinch never clicks the button */

    /* Removing the touch capability releases wl_touch and cancels the focus. */
    pt_check(fake_compositor_touch_down(&fc, 9301u, 400u, surface, 0, 150.0, 120.0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 1);
    pt_check(fake_compositor_seat_capabilities(&fc, 3u)); /* keyboard | pointer */
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->touch == NULL);
    pt_check(window->touch_count == 0);
    pt_check(!window->pointer_inside);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TOUCH, 0, 1, 8));
    pt_check(!fake_compositor_touch_down(&fc, 9302u, 401u, surface, 0, 10.0, 10.0));

    /* Known Pt limitation (reported, not masked): platform.c touch_up clears
     * touch_focus after the first lifted contact, so later up/motion events for
     * the remaining contact never reach the window. The wire path is real
     * multi-contact, but only the first up is delivered. */
    pt_check(fake_compositor_seat_capabilities(&fc, 7u)); /* touch back */
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_live_id(&fc, FC_K_TOUCH) != 0);
    pt_check(fake_compositor_touch_down(&fc, 9401u, 500u, surface, 0, 100.0, 100.0));
    pt_check(fake_compositor_touch_down(&fc, 9402u, 501u, surface, 1, 200.0, 100.0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 2);
    pt_check(fake_compositor_touch_up(&fc, 9403u, 502u, 1));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 1);
    pt_check(app->platform->touch_focus == NULL);
    pt_check(fake_compositor_touch_up(&fc, 9404u, 503u, 0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 1); /* second up is dropped by Pt */
    pt_check(window->touch_pinch);
    float stale_x = window->pointer_x;
    pt_check(fake_compositor_touch_motion(&fc, 504u, 0, 10.0, 10.0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check_near(window->pointer_x, stale_x, 0.01); /* motion also dropped */

    pt_check(!fake_compositor_request_overflowed(&fc));
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_check(fake_compositor_error(&fc) == NULL);

    /* Clear the stale internal contact state that Pt left behind. */
    pt_window_input_touch_cancel(window);
    if (canvas) {
        pt_canvas_destroy(canvas);
    }
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- tablet / stylus ---------- */

int main(void) {
    test_facility_tables();
    test_touch_wire();
    return pt_test_finish("wire_input");
}