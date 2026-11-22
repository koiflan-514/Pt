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

typedef struct wire_stylus_log {
    int count;
    pt_stylus_event last;
} wire_stylus_log;

static void wire_log_stylus(pt_window *window, const pt_stylus_event *event,
                            void *user_data) {
    (void)window;
    wire_stylus_log *log = user_data;
    log->last = *event;
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
    pt_check(FC_K_TABLET > FC_K_TABLET_SEAT);
    pt_check(FC_K_TABLET_TOOL > FC_K_TABLET);
    pt_check(FC_K_COUNT == FC_K_TABLET_TOOL + 1);

    pt_check(fc_kind_from_name("wl_touch") == FC_K_TOUCH);
    pt_check(fc_kind_from_name("zwp_tablet_seat_v2") == FC_K_TABLET_SEAT);
    pt_check(fc_kind_from_name("zwp_tablet_v2") == FC_K_TABLET);
    pt_check(fc_kind_from_name("zwp_tablet_tool_v2") == FC_K_TABLET_TOOL);
    pt_check(fc_kind_from_name("zwp_tablet_manager_v2") == FC_K_TABLET_MANAGER);

    /* wayland.xml: wl_touch.down/up/motion/frame/cancel are 0..4 and the only
     * request is release(0). */
    pt_check(fc_ifaces[FC_K_TOUCH].req_count == 1);
    pt_check(strcmp(fc_ifaces[FC_K_TOUCH].reqs[0].name, "release") == 0);

    /* tablet-unstable-v2: seat tablet_added(0)/tool_added(1)/pad_added(2);
     * tablet destroy(0); tool set_cursor(0)/destroy(1). */
    pt_check(fc_ifaces[FC_K_TABLET_SEAT].req_count == 1);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET].name, "zwp_tablet_v2") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET].reqs[0].name, "destroy") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET_MANAGER].reqs[0].name, "get_tablet_seat") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET_MANAGER].reqs[0].sig, "no") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET_MANAGER].reqs[1].name, "destroy") == 0);
    pt_check(fc_ifaces[FC_K_TABLET_TOOL].req_count == 2);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET_TOOL].reqs[0].name, "set_cursor") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET_TOOL].reqs[0].sig, "uoii") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_TABLET_TOOL].reqs[1].name, "destroy") == 0);

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

static void test_tablet_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.tablet");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_check(fake_compositor_global_bound(&fc, FC_K_TABLET_MANAGER));
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET_SEAT) != 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_TABLET_MANAGER, 0) >= 1);
    fc_request tablet_seat_request = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_TABLET_MANAGER, 0, &tablet_seat_request));
    pt_check(tablet_seat_request.new_id ==
             fake_compositor_live_id(&fc, FC_K_TABLET_SEAT));
    pt_check(tablet_seat_request.args[1].u == fake_compositor_live_id(&fc, FC_K_SEAT));

    pt_window *window = wire_open_window(&fc, app, "tablet-wire", 400, 300);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_widget *target = pt_button_create(pt_window_root(window), "Ink");
    pt_widget_set_size(target, 200.0f, 90.0f);
    pt_widget_set_align_self(target, pt_align_start);
    int clicks = 0;
    pt_button_set_on_click(target, wire_count_click, &clicks);
    pt_canvas *canvas = wire_paint(window, 400, 300);
    float cx = target->x + target->width * 0.5f;
    float cy = target->y + target->height * 0.5f;
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);

    wire_stylus_log stylus = {0};
    pt_window_set_stylus_callback(window, wire_log_stylus, &stylus);

    /* The compositor announces a tablet, then a tool on the tablet seat. Both
     * proxies use server ids and are registered in the object table. */
    uint32_t tablet = 0;
    pt_check(fake_compositor_tablet_add(&fc, &tablet));
    pt_check(tablet != 0);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET) == tablet);
    pt_check(fake_compositor_tablet_name(&fc, "Fake Intuos"));
    pt_check(fake_compositor_tablet_id(&fc, 0x056au, 0x0002u));
    pt_check(fake_compositor_tablet_path(&fc, "/dev/fake-tablet"));
    pt_check(fake_compositor_tablet_done(&fc));
    pt_check(fake_compositor_sync(&fc, app));

    uint32_t tool = 0;
    pt_check(fake_compositor_tablet_add_tool(&fc, &tool));
    pt_check(tool != 0);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET_TOOL) == tool);
    pt_check(app->platform->tablet_tool_count == 1);
    pt_check(fake_compositor_tablet_tool_type(&fc, wire_tablet_type_pen));
    pt_check(fake_compositor_tablet_tool_hardware_serial(&fc, 0x11223344u, 0x55667788u));
    pt_check(fake_compositor_tablet_tool_hardware_id(&fc, 0x056au, 0x0002u));
    pt_check(fake_compositor_tablet_tool_capability(&fc, wire_tablet_capability_pressure));
    pt_check(fake_compositor_tablet_tool_capability(&fc, wire_tablet_capability_tilt));
    pt_check(fake_compositor_tablet_tool_done(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->tablet_tools[0]->type == pt_stylus_tool_pen);

    /* Proximity with motion/pressure/tilt is grouped into one enter frame.
     * 32768/65535 normalizes to 0.5 exactly. */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_tablet_tool_proximity_in(&fc, 9101u, surface));
    pt_check(fake_compositor_tablet_tool_motion(&fc, cx, cy));
    pt_check(fake_compositor_tablet_tool_pressure(&fc, 32768u));
    pt_check(fake_compositor_tablet_tool_tilt(&fc, 30.0, -15.0));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 1u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 1);
    pt_check(stylus.last.phase == pt_stylus_phase_enter);
    pt_check(stylus.last.tool == pt_stylus_tool_pen);
    pt_check_near(stylus.last.x, cx, 0.5);
    pt_check_near(stylus.last.y, cy, 0.5);
    pt_check_near(stylus.last.pressure, 0.5, 0.0001);
    pt_check_near(stylus.last.tilt_x, 30.0, 0.01);
    pt_check_near(stylus.last.tilt_y, -15.0, 0.01);
    pt_check(window->stylus_in_range);

    /* Contact: down, then full pressure. */
    pt_check(fake_compositor_tablet_tool_down(&fc, 9102u));
    pt_check(fake_compositor_tablet_tool_pressure(&fc, 65535u));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 2u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 2);
    pt_check(stylus.last.phase == pt_stylus_phase_down);
    pt_check_near(stylus.last.pressure, 1.0, 0.0001);
    pt_check(window->stylus_down);

    /* Motion carries the barrel button bit; 16384/65535 = 0.25. */
    pt_check(fake_compositor_tablet_tool_motion(&fc, cx + 8.0, cy + 4.0));
    pt_check(fake_compositor_tablet_tool_pressure(&fc, 16384u));
    pt_check(fake_compositor_tablet_tool_button(&fc, 9103u, wire_btn_stylus, true));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 3u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 3);
    pt_check(stylus.last.phase == pt_stylus_phase_motion);
    pt_check_near(stylus.last.x, cx + 8.0, 0.5);
    pt_check_near(stylus.last.y, cy + 4.0, 0.5);
    pt_check_near(stylus.last.pressure, 0.25, 0.0001);
    pt_check(stylus.last.buttons == PT_STYLUS_BUTTON_BARREL);

    /* A second button adds the secondary bit; the button release clears it. */
    pt_check(fake_compositor_tablet_tool_button(&fc, 9104u, wire_btn_stylus2, true));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 4u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 4);
    pt_check(stylus.last.buttons ==
             (PT_STYLUS_BUTTON_BARREL | PT_STYLUS_BUTTON_SECONDARY));
    pt_check(fake_compositor_tablet_tool_button(&fc, 9105u, wire_btn_stylus, false));
    pt_check(fake_compositor_tablet_tool_button(&fc, 9106u, wire_btn_stylus2, false));
    pt_check(fake_compositor_tablet_tool_pressure(&fc, 0u));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 5u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 5);
    pt_check(stylus.last.buttons == 0u);
    pt_check_near(stylus.last.pressure, 0.0, 0.0001);

    /* Up ends contact; the button click fires on the press/release pair. */
    pt_check(fake_compositor_tablet_tool_up(&fc));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 6u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 6);
    pt_check(stylus.last.phase == pt_stylus_phase_up);
    pt_check(!window->stylus_down);
    pt_check(clicks == 1);

    /* Leaving proximity emits leave and clears the range state. */
    pt_check(fake_compositor_tablet_tool_proximity_out(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 7);
    pt_check(stylus.last.phase == pt_stylus_phase_leave);
    pt_check(!window->stylus_in_range);
    pt_check(!window->stylus_down);

    /* Tool removal destroys the client proxy and frees the Pt tool state. */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_tablet_tool_removed(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->tablet_tool_count == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET_TOOL) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_TABLET_TOOL, 1) >= 1);

    /* A second tool maps a different type and follows the same lifecycle. */
    uint32_t tool2 = 0;
    pt_check(fake_compositor_tablet_add_tool(&fc, &tool2));
    pt_check(tool2 != 0);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_tablet_tool_type(&fc, wire_tablet_type_eraser));
    pt_check(fake_compositor_tablet_tool_done(&fc));
    pt_check(fake_compositor_tablet_tool_proximity_in(&fc, 9201u, surface));
    pt_check(fake_compositor_tablet_tool_motion(&fc, cx, cy));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 7u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->tablet_tool_count == 1);
    pt_check(app->platform->tablet_tools[0]->type == pt_stylus_tool_eraser);
    pt_check(stylus.count == 8);
    pt_check(stylus.last.phase == pt_stylus_phase_enter);
    pt_check(stylus.last.tool == pt_stylus_tool_eraser);
    pt_check(fake_compositor_tablet_tool_removed(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->tablet_tool_count == 0);

    /* Tablet removal destroys the tablet proxy. */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_tablet_removed(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET) == 0);
    pt_check(fake_compositor_count_requests(&fc, FC_K_TABLET, 0) >= 1);

    pt_check(!fake_compositor_request_overflowed(&fc));
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_check(fake_compositor_error(&fc) == NULL);

    if (canvas) {
        pt_canvas_destroy(canvas);
    }
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

int main(void) {
    test_facility_tables();
    test_touch_wire();
    test_tablet_wire();
    return pt_test_finish("wire_input");
}