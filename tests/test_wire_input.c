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
    /* Touch is opt-in: FC_OPT_ALL keeps the keyboard|pointer seat that every
     * pre-1.4 consumer was written against. */
    pt_check((FC_OPT_ALL & FC_OPT_TOUCH) == 0);
    pt_check(FC_OPT_TOUCH == (1u << 10));

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
    pt_check(strcmp(fc_ifaces[FC_K_DATA_OFFER].reqs[0].name, "accept") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_DATA_OFFER].reqs[1].name, "receive") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_DATA_OFFER].reqs[3].name, "finish") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_DATA_OFFER].reqs[4].name, "set_actions") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_DATA_SOURCE].reqs[0].name, "offer") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_DATA_SOURCE].reqs[1].name, "destroy") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_DATA_SOURCE].reqs[2].name, "set_actions") == 0);

    /* primary-selection-unstable-v1: manager create_source(0)/get_device(1),
     * source offer(0)/destroy(1), offer receive(0)/destroy(1). */
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_MANAGER].reqs[0].name, "create_source") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_MANAGER].reqs[1].name, "get_device") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_SOURCE].reqs[0].name, "offer") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_OFFER].reqs[0].name, "receive") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_OFFER].reqs[0].sig, "sh") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_DEVICE].reqs[0].name, "set_selection") == 0);
    pt_check(strcmp(fc_ifaces[FC_K_PRIMARY_DEVICE].reqs[0].sig, "ou") == 0);
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TOUCH, true, 8));
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

    /* wl_touch.frame is a no-op in platform.c: touch_frame forwards nothing and
     * down/motion apply immediately, so a lone frame must not change state. */
    pt_check(fake_compositor_touch_frame(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->touch_count == 1);
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TOUCH, true, 8));
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TABLET_SEAT, true, 8));
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET_SEAT) != 0);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TABLET_MANAGER, 0, 1, 8));
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TABLET, true, 8));
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TABLET_TOOL, true, 8));
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
    pt_check(fake_compositor_tablet_tool_distance(&fc, 32768u));
    pt_check(fake_compositor_tablet_tool_rotation(&fc, 90.0));
    /* slider/wheel are decoded but ignored by Pt's listener; sending them here
     * still proves their wire signatures against tablet-unstable-v2.xml. */
    pt_check(fake_compositor_tablet_tool_slider(&fc, -2048));
    pt_check(fake_compositor_tablet_tool_wheel(&fc, 15.0, 1));
    pt_check(fake_compositor_tablet_tool_button(&fc, 9103u, wire_btn_stylus, true));
    pt_check(fake_compositor_tablet_tool_frame(&fc, 3u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(stylus.count == 3);
    pt_check(stylus.last.phase == pt_stylus_phase_motion);
    pt_check_near(stylus.last.x, cx + 8.0, 0.5);
    pt_check_near(stylus.last.y, cy + 4.0, 0.5);
    pt_check_near(stylus.last.pressure, 0.25, 0.0001);
    pt_check_near(stylus.last.distance, 0.5, 0.0001);
    pt_check_near(stylus.last.rotation, 1.5707963, 0.01);
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TABLET_TOOL, false, 8));
    pt_check(app->platform->tablet_tool_count == 0);
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET_TOOL) == 0);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TABLET_TOOL, 1, 1, 8));
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
    pt_check(fake_compositor_wait_object(&fc, app, FC_K_TABLET, false, 8));
    pt_check(fake_compositor_live_id(&fc, FC_K_TABLET) == 0);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_TABLET, 0, 1, 8));
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

/* ---------- drag and drop ---------- */

typedef struct wire_drop {
    int enters;
    int motions;
    int leaves;
    int drops;
    uint32_t mime_count;
    uint32_t actions;
    bool has_plain;
    bool has_html;
    char selected[64];
    char mime[64];
    char text[128];
    size_t size;
    int chunks;
    int done;
    int cancelled;
    float x;
    float y;
} wire_drop;

static const char *wire_drop_enter(pt_widget *widget, pt_drop_offer *offer, void *user_data) {
    (void)widget;
    wire_drop *drop = user_data;
    ++drop->enters;
    drop->mime_count = pt_drop_offer_mime_count(offer);
    drop->actions = pt_drop_offer_actions(offer);
    drop->has_plain = pt_drop_offer_has_mime(offer, "text/plain;charset=utf-8");
    drop->has_html = pt_drop_offer_has_mime(offer, "text/html");

    if (drop->has_plain) {
        return "text/plain;charset=utf-8";
    }
    return pt_drop_offer_mime(offer, 0);
}

static void wire_drop_motion(pt_widget *widget, float x, float y, void *user_data) {
    (void)widget;
    wire_drop *drop = user_data;
    ++drop->motions;
    drop->x = x;
    drop->y = y;
}

static void wire_drop_leave(pt_widget *widget, void *user_data) {
    (void)widget;
    wire_drop *drop = user_data;
    ++drop->leaves;
}

static void wire_drop_stream(pt_widget *widget, pt_drop_stream *stream,
                             pt_drop_stream_state state, const char *data, size_t size,
                             void *user_data) {
    (void)widget;
    (void)stream;
    wire_drop *drop = user_data;
    if (state == pt_drop_stream_data) {
        ++drop->chunks;
        if (drop->size + size < sizeof(drop->text)) {
            memcpy(drop->text + drop->size, data, size);
            drop->size += size;
            drop->text[drop->size] = '\0';
        }
    } else if (state == pt_drop_stream_done) {
        ++drop->done;
    } else if (state == pt_drop_stream_cancelled) {
        ++drop->cancelled;
    }
}

static void wire_drop_offer_drop(pt_widget *widget, pt_drop_offer *offer,
                                 const char *mime_type, float x, float y,
                                 void *user_data) {
    (void)widget;
    wire_drop *drop = user_data;
    ++drop->drops;
    snprintf(drop->mime, sizeof(drop->mime), "%s", mime_type ? mime_type : "");
    snprintf(drop->selected, sizeof(drop->selected), "%s",
             pt_drop_offer_selected_mime(offer) ? pt_drop_offer_selected_mime(offer) : "");
    drop->x = x;
    drop->y = y;
    (void)pt_drop_offer_read(offer, mime_type, 0, wire_drop_stream, drop);
}

typedef struct wire_drag_result {
    int count;
    bool dropped;
    pt_drag_action action;
} wire_drag_result;

static void wire_drag_finished(pt_window *window, bool dropped, pt_drag_action action,
                               void *user_data) {
    (void)window;
    wire_drag_result *result = user_data;
    ++result->count;
    result->dropped = dropped;
    result->action = action;
}

static void wire_pump_dnd(fake_compositor *fc, pt_app *app, wire_drop *drop) {
    for (int i = 0; i < 200 && drop->done == 0 && drop->cancelled == 0; ++i) {
        fake_compositor_dispatch(fc, app, 5);
        pt_dnd_pump(app);
    }
}

static void test_dnd_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.dnd");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));

    pt_window *window = wire_open_window(&fc, app, "dnd-wire", 400, 300);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_widget *zone = pt_box_create(pt_window_root(window), pt_direction_row);
    pt_widget_set_size(zone, 240.0f, 140.0f);
    pt_widget_set_align_self(zone, pt_align_start);
    wire_drop drop = {0};
    pt_drop_target target = {
        .motion = wire_drop_motion,
        .leave = wire_drop_leave,
        .enter_offer = wire_drop_enter,
        .drop_offer = wire_drop_offer_drop,
        .user_data = &drop,
    };
    pt_widget_set_drop_target(zone, &target);
    wire_drag_result result = {0};
    pt_window_set_drag_finished_callback(window, wire_drag_finished, &result);
    pt_canvas *canvas = wire_paint(window, 400, 300);
    float cx = zone->x + 50.0f;
    float cy = zone->y + 40.0f;
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    pt_check(surface != 0);

    /* Implicit grab: a pointer press supplies the serial start_drag needs. */
    fake_compositor_pointer_enter(&fc, 8000u, surface, cx, cy);
    fake_compositor_pointer_button(&fc, 8001u, 10u, 0x110u, true);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->pointer_serial == 8001u);
    pt_check(window->pointer_inside);

    const pt_drag_data drag_data = {
        .mime_type = "text/plain;charset=utf-8",
        .data = "wire drag payload",
        .size = 0,
    };
    fake_compositor_clear(&fc);
    pt_check(pt_window_begin_drag(window, &drag_data, 1));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(pt_window_get_drag_active(window));

    /* Source setup requests: one data source, the offer list including every
     * text alias, a copy-only action mask and start_drag with the grab serial. */
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_MANAGER, 0, 1, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_DATA_MANAGER, 0) >= 1);
    fc_request create_source = {0};
    pt_check(fake_compositor_last_request(&fc, FC_K_DATA_MANAGER, 0, &create_source));
    pt_check(create_source.new_id != 0);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_SOURCE, 0, 5, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_DATA_SOURCE, 0) >= 5);
    bool offered_plain = false;
    size_t offer_count = fake_compositor_count_requests(&fc, FC_K_DATA_SOURCE, 0);
    for (size_t i = 0; i < offer_count; ++i) {
        fc_request offer_request;
        if (fake_compositor_nth_request(&fc, FC_K_DATA_SOURCE, 0, i, &offer_request) >= 0 &&
            offer_request.nargs >= 1 && offer_request.args[0].has_text &&
            strcmp(offer_request.args[0].text, "text/plain;charset=utf-8") == 0) {
            offered_plain = true;
        }
    }
    pt_check(offered_plain);
    fc_request set_actions = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_SOURCE, 2, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_DATA_SOURCE, 2, &set_actions));
    pt_check(set_actions.args[0].u == 1u); /* COPY */
    fc_request start_drag = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_DEVICE, 0, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_DATA_DEVICE, 0, &start_drag));
    pt_check(start_drag.nargs == 4);
    pt_check(start_drag.args[0].u == create_source.new_id);
    pt_check(start_drag.args[1].u == surface);
    pt_check(start_drag.args[3].u == 8001u);

    /* Source send: the compositor asks for a MIME type with an fd; the client
     * writes its payload through SCM_RIGHTS and we read it back. */
    pt_check(fake_compositor_drag_source_send(&fc, "text/plain;charset=utf-8"));
    pt_check(fake_compositor_sync(&fc, app));
    char source_payload[128] = {0};
    ssize_t source_size =
        fake_compositor_drag_source_payload(&fc, source_payload, sizeof(source_payload));
    pt_check(source_size == (ssize_t)strlen("wire drag payload"));
    pt_check(strcmp(source_payload, "wire drag payload") == 0);

    /* Repeating the SCM_RIGHTS send/payload cycle keeps the descriptor count
     * stable; each pipe is closed by the facility after the read. */
    int source_fds_before = fake_compositor_fd_count();
    for (int i = 0; i < 8; ++i) {
        pt_check(fake_compositor_drag_source_send(&fc, "text/plain;charset=utf-8"));
        pt_check(fake_compositor_sync(&fc, app));
        char loop_payload[64] = {0};
        ssize_t loop_size = fake_compositor_drag_source_payload(&fc, loop_payload,
                                                                sizeof(loop_payload));
        pt_check(loop_size == (ssize_t)strlen("wire drag payload"));
        pt_check(strcmp(loop_payload, "wire drag payload") == 0);
        pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    }
    int source_fds_after = fake_compositor_fd_count();
    pt_check(source_fds_before > 0 && source_fds_after > 0);
    pt_check(source_fds_after <= source_fds_before);

    /* target/action arrive before the drop. */
    pt_check(fake_compositor_drag_source_target(&fc, "text/plain;charset=utf-8"));
    pt_check(fake_compositor_drag_source_action(&fc, 1u)); /* COPY */
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->drag_action == 1u);

    /* Destination side: a compositor offer with two MIME types. */
    static const char *const drag_mimes[] = {"text/plain;charset=utf-8", "text/html"};
    pt_check(fake_compositor_drag_offer(&fc, drag_mimes, 2, "wire drop text", 3u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_drag_offer_id(&fc) != 0);
    pt_check(app->platform->offers != NULL);
    pt_check(app->platform->drag_offer == NULL);

    fake_compositor_clear(&fc);
    pt_check(fake_compositor_drag_enter(&fc, 8101u, surface, cx, cy));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(drop.enters == 1);
    pt_check(drop.mime_count == 2);
    pt_check(drop.actions == 3u); /* COPY | MOVE advertised by the source */
    pt_check(drop.has_plain);
    pt_check(drop.has_html);
    pt_check(app->platform->drag_mime != NULL);
    if (app->platform->drag_mime) {
        pt_check(strcmp(app->platform->drag_mime, "text/plain;charset=utf-8") == 0);
    }
    fc_request accept = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_OFFER, 0, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_DATA_OFFER, 0, &accept));
    pt_check(accept.nargs == 2);
    pt_check(accept.args[0].u == 8101u);
    pt_check(accept.args[1].has_text);
    pt_check(app->platform->drag_mime != NULL);
    if (app->platform->drag_mime) {
        pt_check(strcmp(accept.args[1].text, app->platform->drag_mime) == 0);
    }
    fc_request negotiated = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_OFFER, 4, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_DATA_OFFER, 4, &negotiated));
    pt_check(negotiated.args[0].u == 3u);
    pt_check(negotiated.args[1].u == 1u);

    /* Motion routes to the target widget in widget-local coordinates. */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_drag_motion(&fc, 8102u, cx + 12.0, cy + 7.0));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(drop.motions == 1);
    pt_check_near(drop.x, 62.0, 0.5);
    pt_check_near(drop.y, 47.0, 0.5);

    /* Drop: the client accepts the selected MIME, reads the payload over the
     * SCM_RIGHTS fd and finishes the transfer (v3). */
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_drag_drop(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    wire_pump_dnd(&fc, app, &drop);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(drop.drops == 1);
    pt_check(drop.done == 1);
    pt_check(drop.cancelled == 0);
    pt_check(strcmp(drop.text, "wire drop text") == 0);
    pt_check(strcmp(drop.mime, "text/plain;charset=utf-8") == 0);
    pt_check_near(drop.x, 62.0, 0.5);
    pt_check_near(drop.y, 47.0, 0.5);
    fc_request receive = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_OFFER, 1, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_DATA_OFFER, 1, &receive));
    pt_check(receive.nargs == 2);
    pt_check(receive.args[0].has_text);
    pt_check(strcmp(receive.args[0].text, "text/plain;charset=utf-8") == 0);
    pt_check((receive.flags & FC_FLAG_FD) != 0u);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_OFFER, 3, 1, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_DATA_OFFER, 3) >= 1);

    /* Source completion: dnd_drop_performed then dnd_finished with the
     * negotiated action. The client destroys its source. */
    pt_check(fake_compositor_drag_source_drop_performed(&fc));
    pt_check(fake_compositor_drag_source_finished(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(result.count == 1);
    pt_check(result.dropped);
    pt_check(result.action == pt_drag_action_copy);
    pt_check(!pt_window_get_drag_active(window));
    pt_check(pt_window_get_drag_result(window) == pt_drag_action_copy);
    pt_check(app->platform->drag_source == NULL);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_SOURCE, 1, 1, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_DATA_SOURCE, 1) >= 1);
    pt_check(app->platform->drag_icon_surface != NULL);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);

    /* Leave without a drop: the target's leave callback fires, the client
     * destroys the offer (wl_data_offer.destroy, opcode 2) and no drop is
     * delivered. */
    fake_compositor_clear(&fc);
    static const char *const leave_mimes[] = {"text/plain;charset=utf-8"};
    pt_check(fake_compositor_drag_offer(&fc, leave_mimes, 1, "leave payload", 1u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_drag_enter(&fc, 8301u, surface, cx, cy));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(drop.enters == 2);
    pt_check(drop.has_plain);
    pt_check(!drop.has_html);
    pt_check(fake_compositor_drag_leave(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(drop.leaves == 1);
    pt_check(drop.drops == 1);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_OFFER, 2, 1, 8));
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);

    /* Cancellation path: a new grab, a new source, then wl_data_source.cancelled
     * reports dropped=false and leaves the destination untouched. */
    fake_compositor_pointer_button(&fc, 8201u, 11u, 0x110u, false);
    fake_compositor_pointer_button(&fc, 8202u, 12u, 0x110u, true);
    pt_check(fake_compositor_sync(&fc, app));
    const pt_drag_data cancel_data = {
        .mime_type = "text/plain;charset=utf-8",
        .data = "cancel payload",
        .size = 0,
    };
    pt_check(pt_window_begin_drag(window, &cancel_data, 1));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(pt_window_get_drag_active(window));
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_drag_source_cancelled(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(result.count == 2);
    pt_check(!result.dropped);
    pt_check(result.action == pt_drag_action_none);
    pt_check(!pt_window_get_drag_active(window));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_DATA_SOURCE, 1, 1, 8));

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

/* ---------- primary selection ---------- */

static void test_primary_wire(void) {
    static fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.wire.primary");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_check(fake_compositor_global_bound(&fc, FC_K_PRIMARY_MANAGER));

    pt_window *window = wire_open_window(&fc, app, "primary-wire", 360, 260);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_canvas *canvas = wire_paint(window, 360, 260);

    /* Publishing sends create_source, the four text aliases and set_selection
     * with the caller-supplied trigger serial. */
    fake_compositor_clear(&fc);
    pt_check(pt_window_primary_set_serial(window, "wire primary text", 9001u));
    pt_check(fake_compositor_sync(&fc, app));
    fc_request create_source = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_MANAGER, 0, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_PRIMARY_MANAGER, 0, &create_source));
    pt_check(create_source.new_id != 0);
    pt_check(app->platform->primary_source != NULL);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_SOURCE, 0, 4, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_PRIMARY_SOURCE, 0) >= 4);
    fc_request selection = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_DEVICE, 0, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_PRIMARY_DEVICE, 0, &selection));
    pt_check(selection.nargs == 2);
    pt_check(selection.args[0].u == create_source.new_id);
    pt_check(selection.args[1].u == 9001u);

    /* Clearing the owner sends set_selection(NULL) and destroys the source. */
    pt_check(pt_window_primary_set_serial(window, NULL, 9002u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_DEVICE, 0, 2, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_PRIMARY_DEVICE, 0, &selection));
    pt_check(selection.args[0].u == 0u);
    pt_check(selection.args[1].u == 9002u);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_SOURCE, 1, 1, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_PRIMARY_SOURCE, 1) >= 1);
    pt_check(app->platform->primary_source == NULL);

    /* Incoming offer: data_offer + mime list + selection, then the getter
     * pulls the text through zwp_primary_selection_offer_v1.receive. */
    static const char *const primary_mimes[] = {"text/plain", "text/plain;charset=utf-8"};
    fake_compositor_clear(&fc);
    pt_check(fake_compositor_primary_offer(&fc, primary_mimes, 2, "wire primary payload"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_primary_offer_id(&fc) != 0);
    pt_check(app->platform->primary_offer != NULL);
    pt_check(app->platform->primary_mime != NULL);
    if (app->platform->primary_mime) {
        pt_check(strcmp(app->platform->primary_mime, "text/plain;charset=utf-8") == 0);
    }
    char *text = pt_window_primary_get(window);
    pt_check(text != NULL);
    if (text) {
        pt_check(strcmp(text, "wire primary payload") == 0);
        free(text);
    }
    fc_request receive = {0};
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_OFFER, 0, 1, 8));
    pt_check(fake_compositor_last_request(&fc, FC_K_PRIMARY_OFFER, 0, &receive));
    pt_check(receive.nargs == 2);
    pt_check(receive.args[0].has_text);
    pt_check(strcmp(receive.args[0].text, "text/plain;charset=utf-8") == 0);
    pt_check((receive.flags & FC_FLAG_FD) != 0u);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);

    /* Ownership cancelled: the client destroys the source and drops its text. */
    fake_compositor_clear(&fc);
    pt_check(pt_window_primary_set_serial(window, "owned again", 9003u));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->primary_source != NULL);
    pt_check(fake_compositor_primary_source_cancelled(&fc));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->primary_source == NULL);
    pt_check(fake_compositor_wait_request(&fc, app, FC_K_PRIMARY_SOURCE, 1, 1, 8));
    pt_check(fake_compositor_count_requests(&fc, FC_K_PRIMARY_SOURCE, 1) >= 1);

    /* Repeated offer/read cycles keep the descriptor count stable. */
    int fds_before = fake_compositor_fd_count();
    for (int i = 0; i < 12; ++i) {
        pt_check(fake_compositor_primary_offer(&fc, primary_mimes, 2, "loop payload"));
        pt_check(fake_compositor_sync(&fc, app));
        char *loop_text = pt_window_primary_get(window);
        pt_check(loop_text != NULL && strcmp(loop_text, "loop payload") == 0);
        free(loop_text);
    }
    int fds_after = fake_compositor_fd_count();
    pt_check(fds_before > 0 && fds_after > 0);
    pt_check(fds_after <= fds_before);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);

    pt_check(!fake_compositor_request_overflowed(&fc));
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
    test_dnd_wire();
    test_primary_wire();
    return pt_test_finish("wire_input");
}