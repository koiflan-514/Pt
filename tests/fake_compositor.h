/*
 * In-process Wayland wire replay facility for Pt tests (1.3 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is a header-only, statically linked fake compositor. It speaks the
 * Wayland wire protocol directly on a private unix socket: no libwayland-server
 * (which the test link line does not carry) and no changes to the library. The
 * client side is Pt itself, connecting through WAYLAND_DISPLAY exactly as it
 * would to a real compositor, so the code under test runs its native path.
 *
 * The server runs on a helper thread. Tests drive the client with
 * fake_compositor_dispatch() and request a barrier with fake_compositor_sync(),
 * which roundtrips the client display. Every received request is decoded and
 * recorded (object id, interface kind, opcode, typed arguments) so placement,
 * icon and input wire parameters can be asserted exactly. Server-to-client
 * events are injected with the fake_compositor_* functions below.
 *
 * The file must stay header-only and static: the Makefile builds each test from
 * a single translation unit plus libpt.a.
 *
 * Diagnostics: fake_compositor.verbose prints every request and event to
 * stderr. Protocol violations are collected in fake_compositor.error and
 * exposed through fake_compositor_error().
 */

#ifndef pt_tests_fake_compositor_h
#define pt_tests_fake_compositor_h

#include "../src/wayland/wayland_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ---------- limits ---------- */

#define FC_MAX_OBJECTS 1024
#define FC_MAX_REQUESTS 4096
#define FC_MAX_POPUP_RECORDS 64
#define FC_MAX_FRAMES 64
#define FC_MAX_FDS 32
#define FC_INBOX_CAPACITY 65536

/* Optional globals advertised in addition to the mandatory compositor, shm,
 * seat and xdg_wm_base. Pt treats every one of these as a runtime capability,
 * so leaving one out exercises its fallback path. */
#define FC_OPT_ICON (1u << 0)
#define FC_OPT_DECORATION (1u << 1)
#define FC_OPT_FRACTIONAL (1u << 2)
#define FC_OPT_VIEWPORTER (1u << 3)
#define FC_OPT_TEXT_INPUT (1u << 4)
#define FC_OPT_GESTURES (1u << 5)
#define FC_OPT_DATA_DEVICE (1u << 6)
#define FC_OPT_PRIMARY (1u << 7)
#define FC_OPT_TABLET (1u << 8)
/* Clamps the text-input manager global to version 1 so the v2-only requests and
 * events (action, preedit_hint, show/hide_input_panel) are unavailable. Combine
 * with FC_OPT_TEXT_INPUT; it does not advertise that global by itself. */
#define FC_OPT_TEXT_INPUT_V1 (1u << 9)
#define FC_OPT_ALL                                                                   \
    (FC_OPT_ICON | FC_OPT_DECORATION | FC_OPT_FRACTIONAL | FC_OPT_VIEWPORTER |       \
     FC_OPT_TEXT_INPUT | FC_OPT_GESTURES | FC_OPT_DATA_DEVICE | FC_OPT_PRIMARY |      \
     FC_OPT_TABLET)

/* ---------- interface kinds ---------- */

typedef enum fc_kind {
    FC_K_DISPLAY = 0,
    FC_K_REGISTRY,
    FC_K_CALLBACK,
    FC_K_COMPOSITOR,
    FC_K_SURFACE,
    FC_K_REGION,
    FC_K_SHM,
    FC_K_POOL,
    FC_K_BUFFER,
    FC_K_SEAT,
    FC_K_POINTER,
    FC_K_KEYBOARD,
    FC_K_TOUCH,
    FC_K_WM_BASE,
    FC_K_POSITIONER,
    FC_K_XDG_SURFACE,
    FC_K_TOPLEVEL,
    FC_K_POPUP,
    FC_K_ICON_MANAGER,
    FC_K_ICON,
    FC_K_DECORATION_MANAGER,
    FC_K_DECORATION,
    FC_K_FRACTIONAL_MANAGER,
    FC_K_FRACTIONAL,
    FC_K_VIEWPORTER,
    FC_K_VIEWPORT,
    FC_K_TEXT_MANAGER,
    FC_K_TEXT_INPUT,
    FC_K_GESTURES,
    FC_K_SWIPE,
    FC_K_PINCH,
    FC_K_HOLD,
    FC_K_DATA_MANAGER,
    FC_K_DATA_DEVICE,
    FC_K_DATA_SOURCE,
    FC_K_DATA_OFFER,
    FC_K_PRIMARY_MANAGER,
    FC_K_PRIMARY_DEVICE,
    FC_K_PRIMARY_SOURCE,
    FC_K_PRIMARY_OFFER,
    FC_K_TABLET_MANAGER,
    FC_K_TABLET_SEAT,
    FC_K_COUNT,
    FC_K_UNKNOWN = 0xff
} fc_kind;

/* Readable aliases for tests. */
#define fc_kind_wl_surface FC_K_SURFACE
#define fc_kind_wl_buffer FC_K_BUFFER
#define fc_kind_wl_shm FC_K_SHM
#define fc_kind_wl_shm_pool FC_K_POOL
#define fc_kind_wl_seat FC_K_SEAT
#define fc_kind_wl_callback FC_K_CALLBACK
#define fc_kind_xdg_wm_base FC_K_WM_BASE
#define fc_kind_xdg_positioner FC_K_POSITIONER
#define fc_kind_xdg_surface FC_K_XDG_SURFACE
#define fc_kind_xdg_toplevel FC_K_TOPLEVEL
#define fc_kind_xdg_popup FC_K_POPUP
#define fc_kind_xdg_toplevel_icon_manager_v1 FC_K_ICON_MANAGER
#define fc_kind_xdg_toplevel_icon_v1 FC_K_ICON
#define fc_kind_zxdg_toplevel_decoration_v1 FC_K_DECORATION

/* ---------- recorded requests ---------- */

#define FC_FLAG_TRUNCATED 1u
#define FC_FLAG_FD 2u
#define FC_FLAG_UNKNOWN_REQUEST 4u

typedef struct fc_arg {
    union {
        int32_t i;
        uint32_t u;
        double f;
    };
    bool has_text;
    char text[48];
} fc_arg;

typedef struct fc_request {
    uint32_t object;
    uint32_t new_id;
    uint16_t kind;
    uint16_t opcode;
    uint16_t nargs;
    uint16_t flags;
    fc_arg args[8];
} fc_request;

typedef struct fc_positioner {
    int32_t width;
    int32_t height;
    int32_t anchor_x;
    int32_t anchor_y;
    int32_t anchor_width;
    int32_t anchor_height;
    int32_t offset_x;
    int32_t offset_y;
    uint32_t anchor;
    uint32_t gravity;
    uint32_t constraint;
    bool size_set;
    bool anchor_rect_set;
} fc_positioner;

/* xdg-shell anchor / gravity / constraint values from protocols/xdg-shell.xml. */
#define fc_anchor_none 0u
#define fc_anchor_top 1u
#define fc_anchor_bottom 2u
#define fc_anchor_left 3u
#define fc_anchor_right 4u
#define fc_anchor_top_left 5u
#define fc_anchor_bottom_left 6u
#define fc_anchor_top_right 7u
#define fc_anchor_bottom_right 8u

#define fc_constraint_slide_x 1u
#define fc_constraint_slide_y 2u
#define fc_constraint_flip_x 4u
#define fc_constraint_flip_y 8u
#define fc_constraint_all                                                             \
    (fc_constraint_slide_x | fc_constraint_slide_y | fc_constraint_flip_x |           \
     fc_constraint_flip_y)

typedef struct fc_object {
    uint32_t id;
    uint16_t kind;
    bool live;
    uint32_t parent;
    uint32_t surface_last_buffer;
    uint32_t surface_pending_buffer;
    bool surface_pending_attach;
    bool configured;
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    fc_positioner pos;
    uint32_t icon_buffer;
    int32_t icon_scale;
} fc_object;

typedef struct fc_popup_record {
    bool used;
    bool from_reposition;
    uint32_t popup_id;
    uint32_t xdg_surface_id;
    uint32_t surface_id;
    uint32_t token;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    fc_positioner pos;
} fc_popup_record;

typedef struct fc_frame {
    bool used;
    uint32_t callback_id;
    uint32_t surface_id;
} fc_frame;

typedef struct fake_compositor {
    int listen_fd;
    int client_fd;
    char dir[256];
    char socket_path[320];
    pthread_t thread;
    bool thread_started;
    _Atomic bool stop;
    bool connected;
    unsigned int options;

    pthread_mutex_t state_lock;
    fc_object objects[FC_MAX_OBJECTS];
    fc_request requests[FC_MAX_REQUESTS];
    size_t request_count;
    bool request_overflow;
    fc_popup_record popups[FC_MAX_POPUP_RECORDS];
    size_t popup_count;
    fc_frame frames[FC_MAX_FRAMES];
    bool bound[FC_K_COUNT];
    uint32_t next_serial;
    uint32_t next_time;
    uint32_t next_server_id;
    uint32_t toplevel_surface;
    uint32_t toplevel_id;
    bool protocol_error;
    char error[256];

    int fd_queue[FC_MAX_FDS];
    int fd_count;

    uint8_t *inbox;
    size_t inbox_length;
    size_t inbox_capacity;

    char *keymap_text;
    size_t keymap_size;

    char *saved_display;
    bool had_display;
    char *saved_socket;
    bool had_socket;

    char *selection_text;

    pthread_mutex_t write_lock;
    bool verbose;
} fake_compositor;

/* ---------- protocol tables ---------- */

typedef struct fc_req_desc {
    const char *name;
    const char *sig;
    uint16_t new_kind;
    bool destructor;
} fc_req_desc;

typedef struct fc_iface_desc {
    const char *name;
    uint32_t version;
    const fc_req_desc *reqs;
    uint16_t req_count;
} fc_iface_desc;

static const fc_req_desc fc_reqs_display[] = {
    {"sync", "n", FC_K_CALLBACK, false},
    {"get_registry", "n", FC_K_REGISTRY, false},
};
static const fc_req_desc fc_reqs_registry[] = {
    {"bind", "usun", FC_K_UNKNOWN, false},
};
static const fc_req_desc fc_reqs_callback[] = {
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_compositor[] = {
    {"create_surface", "n", FC_K_SURFACE, false},
    {"create_region", "n", FC_K_REGION, false},
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_surface[] = {
    {"destroy", "", 0, true},
    {"attach", "oii", 0, false},
    {"damage", "iiii", 0, false},
    {"frame", "n", FC_K_CALLBACK, false},
    {"set_opaque_region", "o", 0, false},
    {"set_input_region", "o", 0, false},
    {"commit", "", 0, false},
    {"set_buffer_transform", "i", 0, false},
    {"set_buffer_scale", "i", 0, false},
    {"damage_buffer", "iiii", 0, false},
    {"offset", "ii", 0, false},
    {"get_release", "n", FC_K_CALLBACK, false},
};
static const fc_req_desc fc_reqs_region[] = {
    {"destroy", "", 0, true},
    {"add", "iiii", 0, false},
    {"subtract", "iiii", 0, false},
};
static const fc_req_desc fc_reqs_shm[] = {
    {"create_pool", "nhi", FC_K_POOL, false},
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_pool[] = {
    {"create_buffer", "niiiiu", FC_K_BUFFER, false},
    {"destroy", "", 0, true},
    {"resize", "i", 0, false},
};
static const fc_req_desc fc_reqs_buffer[] = {
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_seat[] = {
    {"get_pointer", "n", FC_K_POINTER, false},
    {"get_keyboard", "n", FC_K_KEYBOARD, false},
    {"get_touch", "n", FC_K_TOUCH, false},
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_pointer[] = {
    {"set_cursor", "uoii", 0, false},
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_keyboard[] = {
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_touch[] = {
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_wm_base[] = {
    {"destroy", "", 0, true},
    {"create_positioner", "n", FC_K_POSITIONER, false},
    {"get_xdg_surface", "no", FC_K_XDG_SURFACE, false},
    {"pong", "u", 0, false},
};
static const fc_req_desc fc_reqs_positioner[] = {
    {"destroy", "", 0, true},
    {"set_size", "ii", 0, false},
    {"set_anchor_rect", "iiii", 0, false},
    {"set_anchor", "u", 0, false},
    {"set_gravity", "u", 0, false},
    {"set_constraint_adjustment", "u", 0, false},
    {"set_offset", "ii", 0, false},
    {"set_reactive", "", 0, false},
    {"set_parent_size", "ii", 0, false},
    {"set_parent_configure", "u", 0, false},
};
static const fc_req_desc fc_reqs_xdg_surface[] = {
    {"destroy", "", 0, true},
    {"get_toplevel", "n", FC_K_TOPLEVEL, false},
    {"get_popup", "noo", FC_K_POPUP, false},
    {"set_window_geometry", "iiii", 0, false},
    {"ack_configure", "u", 0, false},
};
static const fc_req_desc fc_reqs_toplevel[] = {
    {"destroy", "", 0, true},
    {"set_parent", "o", 0, false},
    {"set_title", "s", 0, false},
    {"set_app_id", "s", 0, false},
    {"show_window_menu", "ouii", 0, false},
    {"move", "ou", 0, false},
    {"resize", "ouu", 0, false},
    {"set_max_size", "ii", 0, false},
    {"set_min_size", "ii", 0, false},
    {"set_maximized", "", 0, false},
    {"unset_maximized", "", 0, false},
    {"set_fullscreen", "o", 0, false},
    {"unset_fullscreen", "", 0, false},
    {"set_minimized", "", 0, false},
};
static const fc_req_desc fc_reqs_popup[] = {
    {"destroy", "", 0, true},
    {"grab", "ou", 0, false},
    {"reposition", "ou", 0, false},
};
static const fc_req_desc fc_reqs_icon_manager[] = {
    {"destroy", "", 0, true},
    {"create_icon", "n", FC_K_ICON, false},
    {"set_icon", "oo", 0, false},
};
static const fc_req_desc fc_reqs_icon[] = {
    {"destroy", "", 0, true},
    {"set_name", "s", 0, false},
    {"add_buffer", "oi", 0, false},
};
static const fc_req_desc fc_reqs_decoration_manager[] = {
    {"destroy", "", 0, true},
    {"get_toplevel_decoration", "no", FC_K_DECORATION, false},
};
static const fc_req_desc fc_reqs_decoration[] = {
    {"destroy", "", 0, true},
    {"set_mode", "u", 0, false},
    {"unset_mode", "", 0, false},
};
static const fc_req_desc fc_reqs_fractional_manager[] = {
    {"destroy", "", 0, true},
    {"get_fractional_scale", "no", FC_K_FRACTIONAL, false},
};
static const fc_req_desc fc_reqs_fractional[] = {
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_viewporter[] = {
    {"destroy", "", 0, true},
    {"get_viewport", "no", FC_K_VIEWPORT, false},
};
static const fc_req_desc fc_reqs_viewport[] = {
    {"destroy", "", 0, true},
    {"set_source", "ffff", 0, false},
    {"set_destination", "ii", 0, false},
};
static const fc_req_desc fc_reqs_text_manager[] = {
    {"destroy", "", 0, true},
    {"get_text_input", "no", FC_K_TEXT_INPUT, false},
};
static const fc_req_desc fc_reqs_text_input[] = {
    {"destroy", "", 0, true},
    {"enable", "", 0, false},
    {"disable", "", 0, false},
    {"set_surrounding_text", "sii", 0, false},
    {"set_text_change_cause", "u", 0, false},
    {"set_content_type", "uu", 0, false},
    {"set_cursor_rectangle", "iiii", 0, false},
    {"commit", "", 0, false},
    {"set_available_actions", "a", 0, false},
    {"show_input_panel", "", 0, false},
    {"hide_input_panel", "", 0, false},
};
static const fc_req_desc fc_reqs_gestures[] = {
    {"get_swipe_gesture", "no", FC_K_SWIPE, false},
    {"get_pinch_gesture", "no", FC_K_PINCH, false},
    {"release", "", 0, true},
    {"get_hold_gesture", "no", FC_K_HOLD, false},
};
static const fc_req_desc fc_reqs_gesture_device[] = {
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_data_manager[] = {
    {"create_data_source", "n", FC_K_DATA_SOURCE, false},
    {"get_data_device", "no", FC_K_DATA_DEVICE, false},
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_data_device[] = {
    {"start_drag", "ooou", 0, false},
    {"set_selection", "ou", 0, false},
    {"release", "", 0, true},
};
static const fc_req_desc fc_reqs_data_source[] = {
    {"offer", "s", 0, false},
    {"destroy", "", 0, true},
    {"set_actions", "u", 0, false},
};
static const fc_req_desc fc_reqs_data_offer[] = {
    {"accept", "us", 0, false},
    {"receive", "sh", 0, false},
    {"destroy", "", 0, true},
    {"finish", "", 0, false},
    {"set_actions", "uu", 0, false},
};
static const fc_req_desc fc_reqs_primary_manager[] = {
    {"create_source", "n", FC_K_PRIMARY_SOURCE, false},
    {"get_device", "no", FC_K_PRIMARY_DEVICE, false},
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_primary_device[] = {
    {"set_selection", "ou", 0, false},
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_primary_source[] = {
    {"offer", "s", 0, false},
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_primary_offer[] = {
    {"receive", "sh", 0, false},
    {"destroy", "", 0, true},
};
static const fc_req_desc fc_reqs_tablet_manager[] = {
    {"destroy", "", 0, true},
    {"get_tablet_seat", "no", FC_K_TABLET_SEAT, false},
};
static const fc_req_desc fc_reqs_tablet_seat[] = {
    {"destroy", "", 0, true},
};

#define FC_IFACE(k, nm, ver, tbl)                                                    \
    [(k)] = {nm, ver, tbl, (uint16_t)(sizeof(tbl) / sizeof((tbl)[0]))}

static const fc_iface_desc fc_ifaces[FC_K_COUNT] = {
    FC_IFACE(FC_K_DISPLAY, "wl_display", 1, fc_reqs_display),
    FC_IFACE(FC_K_REGISTRY, "wl_registry", 1, fc_reqs_registry),
    FC_IFACE(FC_K_CALLBACK, "wl_callback", 1, fc_reqs_callback),
    FC_IFACE(FC_K_COMPOSITOR, "wl_compositor", 6, fc_reqs_compositor),
    FC_IFACE(FC_K_SURFACE, "wl_surface", 6, fc_reqs_surface),
    FC_IFACE(FC_K_REGION, "wl_region", 1, fc_reqs_region),
    FC_IFACE(FC_K_SHM, "wl_shm", 1, fc_reqs_shm),
    FC_IFACE(FC_K_POOL, "wl_shm_pool", 1, fc_reqs_pool),
    FC_IFACE(FC_K_BUFFER, "wl_buffer", 1, fc_reqs_buffer),
    FC_IFACE(FC_K_SEAT, "wl_seat", 7, fc_reqs_seat),
    FC_IFACE(FC_K_POINTER, "wl_pointer", 7, fc_reqs_pointer),
    FC_IFACE(FC_K_KEYBOARD, "wl_keyboard", 7, fc_reqs_keyboard),
    FC_IFACE(FC_K_TOUCH, "wl_touch", 7, fc_reqs_touch),
    FC_IFACE(FC_K_WM_BASE, "xdg_wm_base", 4, fc_reqs_wm_base),
    FC_IFACE(FC_K_POSITIONER, "xdg_positioner", 4, fc_reqs_positioner),
    FC_IFACE(FC_K_XDG_SURFACE, "xdg_surface", 4, fc_reqs_xdg_surface),
    FC_IFACE(FC_K_TOPLEVEL, "xdg_toplevel", 4, fc_reqs_toplevel),
    FC_IFACE(FC_K_POPUP, "xdg_popup", 4, fc_reqs_popup),
    FC_IFACE(FC_K_ICON_MANAGER, "xdg_toplevel_icon_manager_v1", 1,
             fc_reqs_icon_manager),
    FC_IFACE(FC_K_ICON, "xdg_toplevel_icon_v1", 1, fc_reqs_icon),
    FC_IFACE(FC_K_DECORATION_MANAGER, "zxdg_decoration_manager_v1", 1,
             fc_reqs_decoration_manager),
    FC_IFACE(FC_K_DECORATION, "zxdg_toplevel_decoration_v1", 1,
             fc_reqs_decoration),
    FC_IFACE(FC_K_FRACTIONAL_MANAGER, "wp_fractional_scale_manager_v1", 1,
             fc_reqs_fractional_manager),
    FC_IFACE(FC_K_FRACTIONAL, "wp_fractional_scale_v1", 1, fc_reqs_fractional),
    FC_IFACE(FC_K_VIEWPORTER, "wp_viewporter", 1, fc_reqs_viewporter),
    FC_IFACE(FC_K_VIEWPORT, "wp_viewport", 1, fc_reqs_viewport),
    FC_IFACE(FC_K_TEXT_MANAGER, "zwp_text_input_manager_v3", 2,
             fc_reqs_text_manager),
    FC_IFACE(FC_K_TEXT_INPUT, "zwp_text_input_v3", 2, fc_reqs_text_input),
    FC_IFACE(FC_K_GESTURES, "zwp_pointer_gestures_v1", 3, fc_reqs_gestures),
    FC_IFACE(FC_K_SWIPE, "zwp_pointer_gesture_swipe_v1", 3, fc_reqs_gesture_device),
    FC_IFACE(FC_K_PINCH, "zwp_pointer_gesture_pinch_v1", 3, fc_reqs_gesture_device),
    FC_IFACE(FC_K_HOLD, "zwp_pointer_gesture_hold_v1", 3, fc_reqs_gesture_device),
    FC_IFACE(FC_K_DATA_MANAGER, "wl_data_device_manager", 3, fc_reqs_data_manager),
    FC_IFACE(FC_K_DATA_DEVICE, "wl_data_device", 3, fc_reqs_data_device),
    FC_IFACE(FC_K_DATA_SOURCE, "wl_data_source", 3, fc_reqs_data_source),
    FC_IFACE(FC_K_DATA_OFFER, "wl_data_offer", 3, fc_reqs_data_offer),
    FC_IFACE(FC_K_PRIMARY_MANAGER, "zwp_primary_selection_device_manager_v1", 1,
             fc_reqs_primary_manager),
    FC_IFACE(FC_K_PRIMARY_DEVICE, "zwp_primary_selection_device_v1", 1,
             fc_reqs_primary_device),
    FC_IFACE(FC_K_PRIMARY_SOURCE, "zwp_primary_selection_source_v1", 1,
             fc_reqs_primary_source),
    FC_IFACE(FC_K_PRIMARY_OFFER, "zwp_primary_selection_offer_v1", 1,
             fc_reqs_primary_offer),
    FC_IFACE(FC_K_TABLET_MANAGER, "zwp_tablet_manager_v2", 1, fc_reqs_tablet_manager),
    FC_IFACE(FC_K_TABLET_SEAT, "zwp_tablet_seat_v2", 1, fc_reqs_tablet_seat),
};

#undef FC_IFACE

/* ---------- errors and small helpers ---------- */

static inline void fake_compositor_set_error(fake_compositor *fc, const char *fmt, ...) {
    if (!fc) {
        return;
    }
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&fc->state_lock);
    snprintf(fc->error, sizeof(fc->error), "%s", text);
    fc->protocol_error = true;
    pthread_mutex_unlock(&fc->state_lock);
    if (fc->verbose) {
        fprintf(stderr, "fake-compositor: %s\n", fc->error);
    }
}

static inline const char *fake_compositor_error(const fake_compositor *fc) {
    return fc && fc->protocol_error ? fc->error : NULL;
}

static inline uint16_t fc_load_u16(const uint8_t *p) {
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static inline uint32_t fc_load_u32(const uint8_t *p) {
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static inline void fc_store_u16(uint8_t *p, uint16_t value) {
    memcpy(p, &value, sizeof(value));
}

static inline void fc_store_u32(uint8_t *p, uint32_t value) {
    memcpy(p, &value, sizeof(value));
}

static inline int32_t fc_load_i32(const uint8_t *p) {
    int32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static inline void fc_store_i32(uint8_t *p, int32_t value) {
    memcpy(p, &value, sizeof(value));
}

/* Caller holds state_lock. */
static inline fc_object *fc_object_for(fake_compositor *fc, uint32_t id) {
    for (size_t i = 0; i < FC_MAX_OBJECTS; ++i) {
        if (fc->objects[i].live && fc->objects[i].id == id) {
            return &fc->objects[i];
        }
    }
    return NULL;
}

/* Caller holds state_lock. */
static inline fc_object *fc_object_new_locked(fake_compositor *fc, uint32_t id,
                                              fc_kind kind, uint32_t parent) {
    for (size_t i = 0; i < FC_MAX_OBJECTS; ++i) {
        if (!fc->objects[i].live) {
            fc_object *object = &fc->objects[i];
            memset(object, 0, sizeof(*object));
            object->live = true;
            object->id = id;
            object->kind = (uint16_t)kind;
            object->parent = parent;
            return object;
        }
    }
    snprintf(fc->error, sizeof(fc->error), "object table exhausted at id %u", id);
    fc->protocol_error = true;
    return NULL;
}

/* Caller holds state_lock. */
static inline void fc_object_destroy_locked(fake_compositor *fc, uint32_t id) {
    fc_object *object = fc_object_for(fc, id);
    if (object) {
        object->live = false;
    }
    for (size_t i = 0; i < FC_MAX_FRAMES; ++i) {
        if (fc->frames[i].used &&
            (fc->frames[i].callback_id == id || fc->frames[i].surface_id == id)) {
            fc->frames[i].used = false;
        }
    }
}

/* Caller holds state_lock. */
static inline uint32_t fc_find_live_locked(fake_compositor *fc, fc_kind kind) {
    for (size_t i = FC_MAX_OBJECTS; i > 0; --i) {
        if (fc->objects[i - 1].live && fc->objects[i - 1].kind == (uint16_t)kind) {
            return fc->objects[i - 1].id;
        }
    }
    return 0;
}

static inline uint32_t fc_live_id(fake_compositor *fc, fc_kind kind) {
    pthread_mutex_lock(&fc->state_lock);
    uint32_t id = fc_find_live_locked(fc, kind);
    pthread_mutex_unlock(&fc->state_lock);
    return id;
}

/* Caller holds state_lock. Finds the newest live xdg_popup and its
 * xdg_surface so injected events do not depend on the popup-record log, which
 * tests may clear between phases. */
static inline bool fc_latest_popup_locked(fake_compositor *fc, uint32_t *popup_id,
                                          uint32_t *xdg_id) {
    for (size_t i = FC_MAX_OBJECTS; i > 0; --i) {
        fc_object *object = &fc->objects[i - 1];
        if (object->live && object->kind == (uint16_t)FC_K_POPUP) {
            if (popup_id) {
                *popup_id = object->id;
            }
            if (xdg_id) {
                *xdg_id = object->parent;
            }
            return true;
        }
    }
    return false;
}

static inline fc_kind fc_kind_from_name(const char *name) {
    if (!name) {
        return FC_K_UNKNOWN;
    }
    for (uint32_t i = 0; i < FC_K_COUNT; ++i) {
        if (fc_ifaces[i].name && strcmp(fc_ifaces[i].name, name) == 0) {
            return (fc_kind)i;
        }
    }
    return FC_K_UNKNOWN;
}

/* ---------- wire writing ---------- */

typedef struct fc_builder {
    uint8_t data[512];
    size_t length;
} fc_builder;

static inline void fc_b_reset(fc_builder *builder) {
    builder->length = 0;
}

static inline void fc_b_u32(fc_builder *builder, uint32_t value) {
    if (builder->length + 4u > sizeof(builder->data)) {
        return;
    }
    fc_store_u32(builder->data + builder->length, value);
    builder->length += 4;
}

static inline void fc_b_i32(fc_builder *builder, int32_t value) {
    if (builder->length + 4u > sizeof(builder->data)) {
        return;
    }
    fc_store_i32(builder->data + builder->length, value);
    builder->length += 4;
}

static inline void fc_b_fixed(fc_builder *builder, double value) {
    fc_b_i32(builder, (int32_t)(value * 256.0));
}

static inline void fc_b_string(fc_builder *builder, const char *text) {
    size_t length = text ? strlen(text) + 1u : 1u;
    fc_b_u32(builder, (uint32_t)length);
    size_t padded = (length + 3u) & ~(size_t)3u;
    if (builder->length + padded > sizeof(builder->data)) {
        return;
    }
    if (text) {
        memcpy(builder->data + builder->length, text, length);
    } else {
        builder->data[builder->length] = 0;
    }
    memset(builder->data + builder->length + length, 0, padded - length);
    builder->length += padded;
}

static inline void fc_b_array_empty(fc_builder *builder) {
    fc_b_u32(builder, 0);
}

static inline bool fc_write_all(fake_compositor *fc, const uint8_t *data,
                                size_t length) {
    while (length > 0) {
        ssize_t written = write(fc->client_fd, data, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timespec pause = {0, 1000000};
                nanosleep(&pause, NULL);
                continue;
            }
            return false;
        }
        data += (size_t)written;
        length -= (size_t)written;
    }
    return true;
}

static inline bool fc_send(fake_compositor *fc, uint32_t object, uint16_t opcode,
                           const fc_builder *builder) {
    uint8_t header[8];
    fc_store_u32(header, object);
    fc_store_u16(header + 4, opcode);
    fc_store_u16(header + 6, (uint16_t)(8u + builder->length));
    bool ok = false;
    pthread_mutex_lock(&fc->write_lock);
    if (fc->client_fd >= 0 && fc->connected) {
        ok = fc_write_all(fc, header, sizeof(header)) &&
             fc_write_all(fc, builder->data, builder->length);
        if (fc->verbose) {
            fprintf(stderr, "fake-compositor: event object=%u opcode=%u size=%u\n",
                    object, opcode, (unsigned)(8u + builder->length));
        }
    }
    pthread_mutex_unlock(&fc->write_lock);
    return ok;
}

static inline bool fc_send_empty(fake_compositor *fc, uint32_t object, uint16_t opcode) {
    fc_builder builder;
    fc_b_reset(&builder);
    return fc_send(fc, object, opcode, &builder);
}

static inline bool fc_send_u32(fake_compositor *fc, uint32_t object, uint16_t opcode,
                               uint32_t value) {
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, value);
    return fc_send(fc, object, opcode, &builder);
}

static inline bool fc_send_with_fd(fake_compositor *fc, uint32_t object, uint16_t opcode,
                                   const fc_builder *builder, int fd) {
    uint8_t message[8 + sizeof(fc_builder)];
    fc_store_u32(message, object);
    fc_store_u16(message + 4, opcode);
    fc_store_u16(message + 6, (uint16_t)(8u + builder->length));
    memcpy(message + 8, builder->data, builder->length);
    struct iovec iov = {.iov_base = message, .iov_len = 8u + builder->length};
    union {
        struct cmsghdr header;
        char buffer[CMSG_SPACE(sizeof(int))];
    } control;
    memset(&control, 0, sizeof(control));
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control.buffer,
        .msg_controllen = CMSG_SPACE(sizeof(int)),
    };
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    bool ok = false;
    pthread_mutex_lock(&fc->write_lock);
    if (fc->client_fd >= 0 && fc->connected) {
        ssize_t result = sendmsg(fc->client_fd, &msg, MSG_NOSIGNAL);
        ok = result == (ssize_t)iov.iov_len;
        if (fc->verbose) {
            fprintf(stderr, "fake-compositor: event object=%u opcode=%u size=%u fd\n",
                    object, opcode, (unsigned)(8u + builder->length));
        }
    }
    pthread_mutex_unlock(&fc->write_lock);
    return ok;
}

/* Sends wl_keyboard.keymap with the generated test keymap. File descriptors
 * travel in SCM_RIGHTS and occupy no bytes in the message body. */
static inline bool fc_send_keymap_to(fake_compositor *fc, uint32_t keyboard) {
    if (!keyboard || !fc->keymap_text) {
        return false;
    }
    int fd = memfd_create("pt-fake-keymap", MFD_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    size_t total = fc->keymap_size;
    const char *data = fc->keymap_text;
    while (total > 0) {
        ssize_t written = write(fd, data, total);
        if (written <= 0) {
            break;
        }
        data += (size_t)written;
        total -= (size_t)written;
    }
    lseek(fd, 0, SEEK_SET);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, 1u); /* WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 */
    fc_b_u32(&builder, (uint32_t)fc->keymap_size);
    bool ok = fc_send_with_fd(fc, keyboard, 0, &builder, fd);
    close(fd);
    return ok;
}

/* Re-sends the keymap to the current wl_keyboard (compositors do this when the
 * seat changes). */
static inline void fake_compositor_keyboard_keymap(fake_compositor *fc) {
    (void)fc_send_keymap_to(fc, fc_live_id(fc, FC_K_KEYBOARD));
}

/* ---------- positioner geometry ---------- */

/* Mirrors the unconstrained xdg_positioner rule: resolve the anchor corner of
 * the anchor rectangle, then place the popup in the gravity direction. A real
 * compositor may flip/slide; tests inject a configure for that case. */
static inline void fake_compositor_popup_geometry(const fc_positioner *pos, int32_t *x,
                                                  int32_t *y, int32_t *width,
                                                  int32_t *height) {
    int32_t w = pos->width > 0 ? pos->width : 1;
    int32_t h = pos->height > 0 ? pos->height : 1;
    int32_t aw = pos->anchor_width > 0 ? pos->anchor_width : 1;
    int32_t ah = pos->anchor_height > 0 ? pos->anchor_height : 1;
    int32_t anchor_x = pos->anchor_x;
    int32_t anchor_y = pos->anchor_y;
    switch (pos->anchor) {
    case fc_anchor_top:
        anchor_x = pos->anchor_x + aw / 2;
        break;
    case fc_anchor_bottom:
        anchor_x = pos->anchor_x + aw / 2;
        anchor_y = pos->anchor_y + ah;
        break;
    case fc_anchor_left:
        anchor_y = pos->anchor_y + ah / 2;
        break;
    case fc_anchor_right:
        anchor_x = pos->anchor_x + aw;
        anchor_y = pos->anchor_y + ah / 2;
        break;
    case fc_anchor_top_right:
        anchor_x = pos->anchor_x + aw;
        break;
    case fc_anchor_bottom_left:
        anchor_y = pos->anchor_y + ah;
        break;
    case fc_anchor_bottom_right:
        anchor_x = pos->anchor_x + aw;
        anchor_y = pos->anchor_y + ah;
        break;
    case fc_anchor_top_left:
    default:
        break;
    }
    switch (pos->gravity) {
    case fc_anchor_top:
        *x = anchor_x - w / 2;
        *y = anchor_y - h;
        break;
    case fc_anchor_bottom:
        *x = anchor_x - w / 2;
        *y = anchor_y;
        break;
    case fc_anchor_left:
        *x = anchor_x - w;
        *y = anchor_y - h / 2;
        break;
    case fc_anchor_right:
        *x = anchor_x;
        *y = anchor_y - h / 2;
        break;
    case fc_anchor_top_left:
        *x = anchor_x - w;
        *y = anchor_y - h;
        break;
    case fc_anchor_top_right:
        *x = anchor_x;
        *y = anchor_y - h;
        break;
    case fc_anchor_bottom_left:
        *x = anchor_x - w;
        *y = anchor_y;
        break;
    case fc_anchor_bottom_right:
    default:
        *x = anchor_x;
        *y = anchor_y;
        break;
    }
    *x += pos->offset_x;
    *y += pos->offset_y;
    *width = w;
    *height = h;
}

/* ---------- query API ---------- */

static inline size_t fake_compositor_request_count(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    size_t count = fc->request_count;
    pthread_mutex_unlock(&fc->state_lock);
    return count;
}

static inline size_t fake_compositor_count_requests(fake_compositor *fc, uint32_t kind,
                                                    uint16_t opcode) {
    size_t count = 0;
    pthread_mutex_lock(&fc->state_lock);
    for (size_t i = 0; i < fc->request_count; ++i) {
        if (fc->requests[i].kind == (uint16_t)kind && fc->requests[i].opcode == opcode) {
            ++count;
        }
    }
    pthread_mutex_unlock(&fc->state_lock);
    return count;
}

static inline bool fake_compositor_last_request(fake_compositor *fc, uint32_t kind,
                                                uint16_t opcode, fc_request *out) {
    bool found = false;
    pthread_mutex_lock(&fc->state_lock);
    for (size_t i = fc->request_count; i > 0; --i) {
        if (fc->requests[i - 1].kind == (uint16_t)kind &&
            fc->requests[i - 1].opcode == opcode) {
            if (out) {
                *out = fc->requests[i - 1];
            }
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&fc->state_lock);
    return found;
}

static inline bool fake_compositor_request_at(fake_compositor *fc, size_t index,
                                              fc_request *out) {
    bool found = false;
    pthread_mutex_lock(&fc->state_lock);
    if (index < fc->request_count) {
        if (out) {
            *out = fc->requests[index];
        }
        found = true;
    }
    pthread_mutex_unlock(&fc->state_lock);
    return found;
}

/* Index of the n-th (0-based) request of a kind/opcode, or -1. */
static inline ssize_t fake_compositor_nth_request(fake_compositor *fc, uint32_t kind,
                                                  uint16_t opcode, size_t nth,
                                                  fc_request *out) {
    ssize_t index = -1;
    size_t seen = 0;
    pthread_mutex_lock(&fc->state_lock);
    for (size_t i = 0; i < fc->request_count; ++i) {
        if (fc->requests[i].kind == (uint16_t)kind &&
            fc->requests[i].opcode == opcode) {
            if (seen == nth) {
                if (out) {
                    *out = fc->requests[i];
                }
                index = (ssize_t)i;
                break;
            }
            ++seen;
        }
    }
    pthread_mutex_unlock(&fc->state_lock);
    return index;
}

static inline bool fake_compositor_request_overflowed(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    bool overflow = fc->request_overflow;
    pthread_mutex_unlock(&fc->state_lock);
    return overflow;
}

/* Number of file descriptors received from the client but not yet consumed by
 * an fd-typed request. Should be zero after a sync. */
static inline int fake_compositor_pending_fd_count(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    int count = fc->fd_count;
    pthread_mutex_unlock(&fc->state_lock);
    return count;
}

static inline bool fake_compositor_global_bound(fake_compositor *fc, uint32_t kind) {
    bool bound = false;
    pthread_mutex_lock(&fc->state_lock);
    if (kind < FC_K_COUNT) {
        bound = fc->bound[kind];
    }
    pthread_mutex_unlock(&fc->state_lock);
    return bound;
}

static inline uint32_t fake_compositor_toplevel_surface(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    uint32_t surface = fc->toplevel_surface;
    pthread_mutex_unlock(&fc->state_lock);
    return surface;
}

static inline uint32_t fake_compositor_toplevel(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    uint32_t toplevel = fc->toplevel_id;
    pthread_mutex_unlock(&fc->state_lock);
    return toplevel;
}

static inline uint32_t fake_compositor_live_id(fake_compositor *fc, uint32_t kind) {
    return fc_live_id(fc, (fc_kind)kind);
}

static inline size_t fake_compositor_popup_record_count(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    size_t count = fc->popup_count;
    pthread_mutex_unlock(&fc->state_lock);
    return count;
}

static inline bool fake_compositor_popup_record_at(fake_compositor *fc, size_t index,
                                                   fc_popup_record *out) {
    bool found = false;
    pthread_mutex_lock(&fc->state_lock);
    if (index < fc->popup_count && fc->popups[index].used) {
        if (out) {
            *out = fc->popups[index];
        }
        found = true;
    }
    pthread_mutex_unlock(&fc->state_lock);
    return found;
}

/* Clears the request log and popup-record table; object state is retained. */
static inline void fake_compositor_clear(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    fc->request_count = 0;
    fc->request_overflow = false;
    fc->popup_count = 0;
    memset(fc->popups, 0, sizeof(fc->popups));
    pthread_mutex_unlock(&fc->state_lock);
}

/* ---------- injected events ---------- */

static inline void fake_compositor_pointer_enter(fake_compositor *fc, uint32_t serial,
                                                 uint32_t surface, double x, double y) {
    uint32_t pointer = fc_live_id(fc, FC_K_POINTER);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, surface);
    fc_b_fixed(&builder, x);
    fc_b_fixed(&builder, y);
    (void)fc_send(fc, pointer, 0, &builder);
}

static inline void fake_compositor_pointer_leave(fake_compositor *fc, uint32_t serial,
                                                 uint32_t surface) {
    uint32_t pointer = fc_live_id(fc, FC_K_POINTER);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, surface);
    (void)fc_send(fc, pointer, 1, &builder);
}

static inline void fake_compositor_pointer_motion(fake_compositor *fc, uint32_t time,
                                                  double x, double y) {
    uint32_t pointer = fc_live_id(fc, FC_K_POINTER);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, time);
    fc_b_fixed(&builder, x);
    fc_b_fixed(&builder, y);
    (void)fc_send(fc, pointer, 2, &builder);
}

static inline void fake_compositor_pointer_button(fake_compositor *fc, uint32_t serial,
                                                  uint32_t time, uint32_t button,
                                                  bool pressed) {
    uint32_t pointer = fc_live_id(fc, FC_K_POINTER);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, time);
    fc_b_u32(&builder, button);
    fc_b_u32(&builder, pressed ? 1u : 0u);
    (void)fc_send(fc, pointer, 3, &builder);
}

static inline void fake_compositor_pointer_axis(fake_compositor *fc, uint32_t time,
                                                uint32_t axis, double value) {
    uint32_t pointer = fc_live_id(fc, FC_K_POINTER);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, time);
    fc_b_u32(&builder, axis);
    fc_b_fixed(&builder, value);
    (void)fc_send(fc, pointer, 4, &builder);
}

static inline void fake_compositor_keyboard_enter(fake_compositor *fc, uint32_t serial,
                                                  uint32_t surface) {
    uint32_t keyboard = fc_live_id(fc, FC_K_KEYBOARD);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, surface);
    fc_b_array_empty(&builder);
    (void)fc_send(fc, keyboard, 1, &builder);
}

static inline void fake_compositor_keyboard_leave(fake_compositor *fc, uint32_t serial,
                                                  uint32_t surface) {
    uint32_t keyboard = fc_live_id(fc, FC_K_KEYBOARD);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, surface);
    (void)fc_send(fc, keyboard, 2, &builder);
}

static inline void fake_compositor_keyboard_key(fake_compositor *fc, uint32_t serial,
                                                uint32_t time, uint32_t keycode,
                                                bool pressed) {
    uint32_t keyboard = fc_live_id(fc, FC_K_KEYBOARD);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, time);
    fc_b_u32(&builder, keycode);
    fc_b_u32(&builder, pressed ? 1u : 0u);
    (void)fc_send(fc, keyboard, 3, &builder);
}

static inline void fake_compositor_keyboard_modifiers(fake_compositor *fc, uint32_t serial,
                                                      uint32_t depressed, uint32_t latched,
                                                      uint32_t locked, uint32_t group) {
    uint32_t keyboard = fc_live_id(fc, FC_K_KEYBOARD);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, serial);
    fc_b_u32(&builder, depressed);
    fc_b_u32(&builder, latched);
    fc_b_u32(&builder, locked);
    fc_b_u32(&builder, group);
    (void)fc_send(fc, keyboard, 4, &builder);
}

static inline void fake_compositor_keyboard_repeat_info(fake_compositor *fc, int32_t rate,
                                                        int32_t delay) {
    uint32_t keyboard = fc_live_id(fc, FC_K_KEYBOARD);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_i32(&builder, rate);
    fc_b_i32(&builder, delay);
    (void)fc_send(fc, keyboard, 5, &builder);
}

/* ---------- text-input v3 events ---------- */

/* Event opcodes from protocols/text-input-unstable-v3.xml. action, language and
 * preedit_hint are since version 2. `done` is the commit point: the client
 * accumulates preedit_string/commit_string/delete_surrounding_text/action/
 * preedit_hint and only applies them when done arrives. All helpers target the
 * newest live zwp_text_input_v3 object and return false when none exists. */
#define fc_text_input_enter 0u
#define fc_text_input_leave 1u
#define fc_text_input_preedit_string 2u
#define fc_text_input_commit_string 3u
#define fc_text_input_delete_surrounding_text 4u
#define fc_text_input_done 5u
#define fc_text_input_action 6u
#define fc_text_input_language 7u
#define fc_text_input_preedit_hint 8u

static inline bool fake_compositor_text_input_enter(fake_compositor *fc,
                                                     uint32_t surface) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, surface);
    return fc_send(fc, text_input, fc_text_input_enter, &builder);
}

static inline bool fake_compositor_text_input_leave(fake_compositor *fc,
                                                     uint32_t surface) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, surface);
    return fc_send(fc, text_input, fc_text_input_leave, &builder);
}

static inline bool fake_compositor_text_input_preedit_string(fake_compositor *fc,
                                                              const char *text,
                                                              int32_t cursor_begin,
                                                              int32_t cursor_end) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_string(&builder, text);
    fc_b_i32(&builder, cursor_begin);
    fc_b_i32(&builder, cursor_end);
    return fc_send(fc, text_input, fc_text_input_preedit_string, &builder);
}

static inline bool fake_compositor_text_input_commit_string(fake_compositor *fc,
                                                             const char *text) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_string(&builder, text);
    return fc_send(fc, text_input, fc_text_input_commit_string, &builder);
}

static inline bool fake_compositor_text_input_delete_surrounding_text(
    fake_compositor *fc, uint32_t before_length, uint32_t after_length) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, before_length);
    fc_b_u32(&builder, after_length);
    return fc_send(fc, text_input, fc_text_input_delete_surrounding_text, &builder);
}

static inline bool fake_compositor_text_input_done(fake_compositor *fc, uint32_t serial) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    return fc_send_u32(fc, text_input, fc_text_input_done, serial);
}

static inline bool fake_compositor_text_input_action(fake_compositor *fc, uint32_t action,
                                                      uint32_t serial) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, action);
    fc_b_u32(&builder, serial);
    return fc_send(fc, text_input, fc_text_input_action, &builder);
}

static inline bool fake_compositor_text_input_language(fake_compositor *fc,
                                                        const char *locale) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_string(&builder, locale);
    return fc_send(fc, text_input, fc_text_input_language, &builder);
}

static inline bool fake_compositor_text_input_preedit_hint(fake_compositor *fc,
                                                            uint32_t start, uint32_t end,
                                                            uint32_t hint) {
    uint32_t text_input = fc_live_id(fc, FC_K_TEXT_INPUT);
    if (!text_input) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, start);
    fc_b_u32(&builder, end);
    fc_b_u32(&builder, hint);
    return fc_send(fc, text_input, fc_text_input_preedit_hint, &builder);
}

static inline void fake_compositor_wm_ping(fake_compositor *fc, uint32_t serial) {
    (void)fc_send_u32(fc, fc_live_id(fc, FC_K_WM_BASE), 0, serial);
}

static inline void fake_compositor_toplevel_close(fake_compositor *fc) {
    (void)fc_send_empty(fc, fc_live_id(fc, FC_K_TOPLEVEL), 1);
}

static inline void fake_compositor_toplevel_configure(fake_compositor *fc, int32_t width,
                                                       int32_t height,
                                                       const uint32_t *states,
                                                       size_t state_count) {
    uint32_t toplevel = fc_live_id(fc, FC_K_TOPLEVEL);
    uint32_t xdg_surface = 0;
    pthread_mutex_lock(&fc->state_lock);
    fc_object *object = fc_object_for(fc, toplevel);
    if (object) {
        xdg_surface = object->parent;
    }
    uint32_t serial = ++fc->next_serial;
    pthread_mutex_unlock(&fc->state_lock);
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_i32(&builder, width);
    fc_b_i32(&builder, height);
    fc_b_u32(&builder, (uint32_t)(state_count * sizeof(uint32_t)));
    for (size_t i = 0; i < state_count; ++i) {
        fc_b_u32(&builder, states[i]);
    }
    (void)fc_send(fc, toplevel, 0, &builder);
    (void)fc_send_u32(fc, xdg_surface, 0, serial);
}

/* xdg-decoration-unstable-v1 mode values. configure is the server's
 * authoritative answer and may disagree with the client's set_mode request
 * (Q-DEC-001: a tiling compositor can force server-side). */
#define fc_decoration_mode_client 1u
#define fc_decoration_mode_server 2u

/* Injects zxdg_toplevel_decoration_v1.configure on the newest live decoration
 * object. The client applies it immediately (no xdg_surface configure wait). */
static inline bool fake_compositor_decoration_configure(fake_compositor *fc,
                                                         uint32_t mode) {
    uint32_t decoration = fc_live_id(fc, FC_K_DECORATION);
    if (!decoration) {
        return false;
    }
    return fc_send_u32(fc, decoration, 0, mode);
}

/* xdg_popup.configure then xdg_surface.configure for the most recent live
 * popup. Returns false when no popup exists yet. */
static inline bool fake_compositor_popup_configure(fake_compositor *fc, int32_t x,
                                                   int32_t y, int32_t width,
                                                   int32_t height) {
    uint32_t popup_id = 0;
    uint32_t xdg_id = 0;
    uint32_t serial = 0;
    pthread_mutex_lock(&fc->state_lock);
    if (fc_latest_popup_locked(fc, &popup_id, &xdg_id)) {
        fc_popup_record *record = NULL;
        for (size_t i = fc->popup_count; i > 0; --i) {
            if (fc->popups[i - 1].used && fc->popups[i - 1].popup_id == popup_id) {
                record = &fc->popups[i - 1];
                break;
            }
        }
        if (record) {
            record->x = x;
            record->y = y;
            record->width = width;
            record->height = height;
        }
        serial = ++fc->next_serial;
    }
    pthread_mutex_unlock(&fc->state_lock);
    if (!popup_id) {
        return false;
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_i32(&builder, x);
    fc_b_i32(&builder, y);
    fc_b_i32(&builder, width);
    fc_b_i32(&builder, height);
    if (!fc_send(fc, popup_id, 0, &builder)) {
        return false;
    }
    return fc_send_u32(fc, xdg_id, 0, serial);
}

static inline bool fake_compositor_popup_done(fake_compositor *fc) {
    uint32_t popup_id = 0;
    pthread_mutex_lock(&fc->state_lock);
    (void)fc_latest_popup_locked(fc, &popup_id, NULL);
    pthread_mutex_unlock(&fc->state_lock);
    if (!popup_id) {
        return false;
    }
    return fc_send_empty(fc, popup_id, 1);
}

static inline bool fake_compositor_popup_repositioned(fake_compositor *fc,
                                                       uint32_t token) {
    uint32_t popup_id = 0;
    pthread_mutex_lock(&fc->state_lock);
    (void)fc_latest_popup_locked(fc, &popup_id, NULL);
    pthread_mutex_unlock(&fc->state_lock);
    if (!popup_id) {
        return false;
    }
    return fc_send_u32(fc, popup_id, 2, token);
}

/* Injects a wl_data_device selection with an offer carrying `mimes`. The offer
 * id comes from the server side id range (0xff000000+) so it cannot collide with
 * client-created ids. The optional text is served on wl_data_offer.receive. */
static inline bool fake_compositor_selection_offer(fake_compositor *fc,
                                                   const char *const *mimes,
                                                   size_t mime_count,
                                                   const char *text) {
    uint32_t device = 0;
    uint32_t offer = 0;
    pthread_mutex_lock(&fc->state_lock);
    device = fc_find_live_locked(fc, FC_K_DATA_DEVICE);
    if (device) {
        offer = fc->next_server_id++;
        fc_object_new_locked(fc, offer, FC_K_DATA_OFFER, 0);
        free(fc->selection_text);
        fc->selection_text = text ? strdup(text) : NULL;
    }
    pthread_mutex_unlock(&fc->state_lock);
    if (!device) {
        return false;
    }
    if (!fc_send_u32(fc, device, 0, offer)) {
        return false;
    }
    for (size_t i = 0; i < mime_count; ++i) {
        fc_builder builder;
        fc_b_reset(&builder);
        fc_b_string(&builder, mimes[i]);
        if (!fc_send(fc, offer, 0, &builder)) {
            return false;
        }
    }
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, offer);
    return fc_send(fc, device, 5, &builder);
}

/* ---------- receive path ---------- */

static inline void fc_fd_push_locked(fake_compositor *fc, int fd) {
    if (fc->fd_count < FC_MAX_FDS) {
        fc->fd_queue[fc->fd_count++] = fd;
    } else {
        close(fd);
        snprintf(fc->error, sizeof(fc->error), "too many file descriptors in flight");
        fc->protocol_error = true;
    }
}

/* Caller holds state_lock. */
static inline int fc_fd_pop_locked(fake_compositor *fc) {
    if (fc->fd_count <= 0) {
        snprintf(fc->error, sizeof(fc->error), "fd argument without a queued fd");
        fc->protocol_error = true;
        return -1;
    }
    int fd = fc->fd_queue[0];
    memmove(&fc->fd_queue[0], &fc->fd_queue[1],
            sizeof(fc->fd_queue[0]) * (size_t)(fc->fd_count - 1));
    --fc->fd_count;
    return fd;
}

static inline void fc_close_queued_fds(fake_compositor *fc) {
    pthread_mutex_lock(&fc->state_lock);
    for (int i = 0; i < fc->fd_count; ++i) {
        close(fc->fd_queue[i]);
    }
    fc->fd_count = 0;
    pthread_mutex_unlock(&fc->state_lock);
}

static inline void fc_receive_into_inbox(fake_compositor *fc) {
    uint8_t chunk[8192];
    struct iovec iov = {.iov_base = chunk, .iov_len = sizeof(chunk)};
    union {
        struct cmsghdr header;
        char buffer[CMSG_SPACE(sizeof(int) * FC_MAX_FDS)];
    } control;
    memset(&control, 0, sizeof(control));
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control.buffer,
        .msg_controllen = sizeof(control.buffer),
    };
    ssize_t received = recvmsg(fc->client_fd, &msg, 0);
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        fake_compositor_set_error(fc, "recvmsg failed: %s", strerror(errno));
        fc->stop = true;
        return;
    }
    if (received == 0) {
        pthread_mutex_lock(&fc->state_lock);
        fc->connected = false;
        pthread_mutex_unlock(&fc->state_lock);
        fc->stop = true;
        return;
    }
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        size_t bytes = cmsg->cmsg_len - CMSG_LEN(0);
        size_t count = bytes / sizeof(int);
        for (size_t i = 0; i < count; ++i) {
            int fd = 0;
            memcpy(&fd, CMSG_DATA(cmsg) + i * sizeof(int), sizeof(fd));
            pthread_mutex_lock(&fc->state_lock);
            fc_fd_push_locked(fc, fd);
            pthread_mutex_unlock(&fc->state_lock);
        }
    }
    if (msg.msg_flags & MSG_CTRUNC) {
        fake_compositor_set_error(fc, "ancillary data truncated (too many fds)");
    }
    size_t needed = fc->inbox_length + (size_t)received;
    if (needed > fc->inbox_capacity) {
        size_t capacity = fc->inbox_capacity ? fc->inbox_capacity : 4096;
        while (capacity < needed && capacity < FC_INBOX_CAPACITY) {
            capacity *= 2;
        }
        if (capacity > FC_INBOX_CAPACITY) {
            capacity = FC_INBOX_CAPACITY;
        }
        if (needed > capacity) {
            fake_compositor_set_error(fc, "request larger than the inbox");
            fc->stop = true;
            return;
        }
        uint8_t *grown = realloc(fc->inbox, capacity);
        if (!grown) {
            fake_compositor_set_error(fc, "out of memory growing the inbox");
            fc->stop = true;
            return;
        }
        fc->inbox = grown;
        fc->inbox_capacity = capacity;
    }
    memcpy(fc->inbox + fc->inbox_length, chunk, (size_t)received);
    fc->inbox_length += (size_t)received;
}

/* ---------- codec ---------- */

static inline void fc_send_globals(fake_compositor *fc);

typedef struct fc_reaction {
    uint32_t callback_done;
    uint32_t bind_kind;
    uint32_t keyboard_created;
    bool send_globals;
    uint32_t releases[4];
    size_t release_count;
    uint32_t frames[8];
    size_t frame_count;
    uint32_t repositioned_token;
    uint32_t repositioned_popup;
    uint32_t configure_popup;
    uint32_t configure_toplevel;
    uint32_t configure_xdg;
    uint32_t configure_serial;
    int32_t cx;
    int32_t cy;
    int32_t cw;
    int32_t ch;
    uint32_t decoration_configure;
    uint32_t decoration_mode;
    bool send_popup_configure;
    bool send_toplevel_configure;
    bool send_xdg_configure;
    bool send_repositioned;
    bool send_decoration_configure;
} fc_reaction;

static inline void fc_reaction_reset(fc_reaction *reaction) {
    memset(reaction, 0, sizeof(*reaction));
}

/* Decodes one request under state_lock, mutates server state and fills the
 * reaction the caller sends after dropping the lock. Returns a copy of the
 * recorded request. */
static inline fc_request fc_decode_locked(fake_compositor *fc, uint32_t object_id,
                                          uint16_t opcode, const uint8_t *payload,
                                          size_t payload_length, fc_reaction *reaction) {
    fc_request request;
    memset(&request, 0, sizeof(request));
    request.object = object_id;
    request.opcode = opcode;
    fc_object *target = fc_object_for(fc, object_id);
    request.kind = target ? target->kind
                          : (object_id == 1 ? (uint16_t)FC_K_DISPLAY
                                            : (uint16_t)FC_K_UNKNOWN);
    const fc_iface_desc *iface =
        request.kind < FC_K_COUNT ? &fc_ifaces[request.kind] : NULL;
    const fc_req_desc *desc =
        iface && opcode < iface->req_count ? &iface->reqs[opcode] : NULL;
    if (!desc) {
        request.flags |= FC_FLAG_UNKNOWN_REQUEST;
        goto log;
    }
    {
        size_t offset = 0;
        for (const char *sig = desc->sig; *sig; ++sig) {
            /* Strings/arrays start with a 32-bit length; fd falls through to
             * SCM_RIGHTS and has no bytes in the message body. */
            size_t need = (*sig == 'h') ? 0u : 4u;
            if (offset + need > payload_length) {
                request.flags |= FC_FLAG_TRUNCATED;
                break;
            }
            const uint8_t *p = payload + offset;
            if (request.nargs < 8) {
                fc_arg *arg = &request.args[request.nargs];
                switch (*sig) {
                case 'i':
                    arg->i = fc_load_i32(p);
                    break;
                case 'u':
                    arg->u = fc_load_u32(p);
                    break;
                case 'f':
                    arg->f = (double)fc_load_i32(p) / 256.0;
                    break;
                case 'o':
                    arg->u = fc_load_u32(p);
                    break;
                case 'n':
                    arg->u = fc_load_u32(p);
                    request.new_id = arg->u;
                    break;
                case 's': {
                    uint32_t length = fc_load_u32(p);
                    size_t text_length = length;
                    if (text_length > sizeof(arg->text) - 1u) {
                        text_length = sizeof(arg->text) - 1u;
                        request.flags |= FC_FLAG_TRUNCATED;
                    }
                    if (offset + 4u + text_length > payload_length) {
                        request.flags |= FC_FLAG_TRUNCATED;
                        text_length = 0;
                    } else {
                        memcpy(arg->text, p + 4, text_length);
                    }
                    arg->text[text_length] = '\0';
                    arg->has_text = true;
                    need = 4u + ((length + 3u) & ~3u);
                    break;
                }
                case 'a': {
                    uint32_t length = fc_load_u32(p);
                    arg->u = length;
                    need = 4u + ((length + 3u) & ~3u);
                    break;
                }
                case 'h':
                    arg->i = fc_fd_pop_locked(fc);
                    request.flags |= FC_FLAG_FD;
                    need = 0u;
                    break;
                default:
                    break;
                }
                ++request.nargs;
            } else {
                request.flags |= FC_FLAG_TRUNCATED;
                break;
            }
            if (offset + need > payload_length) {
                request.flags |= FC_FLAG_TRUNCATED;
                break;
            }
            offset += need;
        }
    }
    /* Interface-specific state. */
    uint32_t new_kind = desc->new_kind;
    if (request.kind == (uint16_t)FC_K_REGISTRY && opcode == 0 &&
        request.nargs >= 2 && request.args[1].has_text) {
        new_kind = (uint32_t)fc_kind_from_name(request.args[1].text);
    }
    if (request.new_id && new_kind != FC_K_UNKNOWN) {
        uint32_t parent = object_id;
        if (new_kind == (uint32_t)FC_K_XDG_SURFACE && request.nargs >= 2) {
            parent = request.args[1].u;
        } else if (new_kind == (uint32_t)FC_K_VIEWPORT ||
                   new_kind == (uint32_t)FC_K_FRACTIONAL ||
                   new_kind == (uint32_t)FC_K_TEXT_INPUT ||
                   new_kind == (uint32_t)FC_K_DATA_DEVICE ||
                   new_kind == (uint32_t)FC_K_PRIMARY_DEVICE ||
                   new_kind == (uint32_t)FC_K_SWIPE ||
                   new_kind == (uint32_t)FC_K_PINCH ||
                   new_kind == (uint32_t)FC_K_HOLD) {
            if (request.nargs >= 2) {
                parent = request.args[1].u;
            }
        }
        fc_object *created =
            fc_object_new_locked(fc, request.new_id, (fc_kind)new_kind, parent);
        if (created && new_kind == (uint32_t)FC_K_BUFFER && request.nargs >= 6) {
            created->width = request.args[2].i;
            created->height = request.args[3].i;
            created->stride = request.args[4].i;
            created->format = (int32_t)request.args[5].u;
        }
        if (request.kind == (uint16_t)FC_K_REGISTRY && opcode == 0 &&
            new_kind != FC_K_UNKNOWN) {
            fc->bound[new_kind] = true;
            reaction->bind_kind = new_kind;
        }
        if (new_kind == (uint32_t)FC_K_XDG_SURFACE) {
            /* default configured = false */
        }
        if (created && new_kind == (uint32_t)FC_K_POSITIONER) {
            memset(&created->pos, 0, sizeof(created->pos));
        }
        if (new_kind == (uint32_t)FC_K_TOPLEVEL) {
            fc->toplevel_id = request.new_id;
            if (target) {
                fc->toplevel_surface = target->parent;
            }
        }
        if (new_kind == (uint32_t)FC_K_KEYBOARD) {
            reaction->keyboard_created = request.new_id;
        }
        if (new_kind == (uint32_t)FC_K_POPUP) {
            uint32_t positioner_id = request.nargs >= 3 ? request.args[2].u : 0;
            fc_popup_record *record = NULL;
            if (fc->popup_count < FC_MAX_POPUP_RECORDS) {
                record = &fc->popups[fc->popup_count++];
                memset(record, 0, sizeof(*record));
            }
            if (record) {
                fc_object *positioner = fc_object_for(fc, positioner_id);
                record->used = true;
                record->from_reposition = false;
                record->popup_id = request.new_id;
                record->xdg_surface_id = object_id;
                record->surface_id = target ? target->parent : 0;
                if (positioner) {
                    record->pos = positioner->pos;
                }
                fake_compositor_popup_geometry(&record->pos, &record->x, &record->y,
                                               &record->width, &record->height);
            }
        }
    }
    if (desc->destructor) {
        fc_object_destroy_locked(fc, object_id);
    }
    if (request.kind == (uint16_t)FC_K_DISPLAY && opcode == 0 && request.new_id) {
        reaction->callback_done = request.new_id;
    }
    if (request.kind == (uint16_t)FC_K_DISPLAY && opcode == 1) {
        reaction->send_globals = true;
    }
    if (request.kind == (uint16_t)FC_K_SURFACE) {
        if (opcode == 1) { /* attach */
            target->surface_pending_attach = true;
            target->surface_pending_buffer = request.nargs >= 1 ? request.args[0].u : 0;
        } else if (opcode == 3) { /* frame */
            for (size_t i = 0; i < FC_MAX_FRAMES; ++i) {
                if (!fc->frames[i].used) {
                    fc->frames[i].used = true;
                    fc->frames[i].callback_id = request.new_id;
                    fc->frames[i].surface_id = object_id;
                    break;
                }
            }
        } else if (opcode == 6) { /* commit */
            if (target) {
                if (target->surface_pending_attach) {
                    uint32_t previous = target->surface_last_buffer;
                    uint32_t pending = target->surface_pending_buffer;
                    target->surface_last_buffer = pending;
                    target->surface_pending_attach = false;
                    if (previous && previous != pending &&
                        reaction->release_count < 4) {
                        reaction->releases[reaction->release_count++] = previous;
                    }
                }
                for (size_t i = 0; i < FC_MAX_FRAMES; ++i) {
                    if (fc->frames[i].used && fc->frames[i].surface_id == object_id &&
                        reaction->frame_count < 8) {
                        reaction->frames[reaction->frame_count++] =
                            fc->frames[i].callback_id;
                        fc->frames[i].used = false;
                    }
                }
                fc_object *xdg = NULL;
                for (size_t i = FC_MAX_OBJECTS; i > 0; --i) {
                    fc_object *candidate = &fc->objects[i - 1];
                    if (candidate->live && candidate->kind == (uint16_t)FC_K_XDG_SURFACE &&
                        candidate->parent == object_id) {
                        xdg = candidate;
                        break;
                    }
                }
                if (xdg && !xdg->configured) {
                    uint32_t toplevel_id = 0;
                    for (size_t i = 0; i < FC_MAX_OBJECTS; ++i) {
                        fc_object *candidate = &fc->objects[i];
                        if (candidate->live &&
                            candidate->kind == (uint16_t)FC_K_TOPLEVEL &&
                            candidate->parent == xdg->id) {
                            toplevel_id = candidate->id;
                        }
                    }
                    fc_popup_record *record = NULL;
                    for (size_t i = fc->popup_count; i > 0; --i) {
                        fc_object *popup = fc_object_for(fc, fc->popups[i - 1].popup_id);
                        if (fc->popups[i - 1].used && fc->popups[i - 1].surface_id == object_id &&
                            popup && popup->live && popup->kind == (uint16_t)FC_K_POPUP &&
                            !popup->configured) {
                            record = &fc->popups[i - 1];
                            break;
                        }
                    }
                    if (toplevel_id) {
                        reaction->send_toplevel_configure = true;
                        reaction->configure_toplevel = toplevel_id;
                        reaction->send_xdg_configure = true;
                    } else if (record) {
                        fc_object *popup = fc_object_for(fc, record->popup_id);
                        fake_compositor_popup_geometry(&record->pos, &record->x,
                                                       &record->y, &record->width,
                                                       &record->height);
                        reaction->send_toplevel_configure = false;
                        reaction->send_popup_configure = true;
                        reaction->send_xdg_configure = true;
                        reaction->configure_popup = record->popup_id;
                        reaction->cx = record->x;
                        reaction->cy = record->y;
                        reaction->cw = record->width;
                        reaction->ch = record->height;
                        if (popup) {
                            popup->configured = true;
                        }
                    }
                    if (toplevel_id || record) {
                        xdg->configured = true;
                        reaction->configure_xdg = xdg->id;
                        reaction->configure_serial = ++fc->next_serial;
                    }
                }
            }
        }
    }
    if (request.kind == (uint16_t)FC_K_POSITIONER && target) {
        fc_positioner *pos = &target->pos;
        switch (opcode) {
        case 1: /* set_size */
            if (request.nargs >= 2) {
                pos->width = request.args[0].i;
                pos->height = request.args[1].i;
                pos->size_set = true;
            }
            break;
        case 2: /* set_anchor_rect */
            if (request.nargs >= 4) {
                pos->anchor_x = request.args[0].i;
                pos->anchor_y = request.args[1].i;
                pos->anchor_width = request.args[2].i;
                pos->anchor_height = request.args[3].i;
                pos->anchor_rect_set = true;
            }
            break;
        case 3:
            pos->anchor = request.nargs >= 1 ? request.args[0].u : 0;
            break;
        case 4:
            pos->gravity = request.nargs >= 1 ? request.args[0].u : 0;
            break;
        case 5:
            pos->constraint = request.nargs >= 1 ? request.args[0].u : 0;
            break;
        case 6:
            if (request.nargs >= 2) {
                pos->offset_x = request.args[0].i;
                pos->offset_y = request.args[1].i;
            }
            break;
        default:
            break;
        }
    }
    if (request.kind == (uint16_t)FC_K_POPUP && opcode == 2) {
        fc_popup_record *existing = NULL;
        for (size_t i = fc->popup_count; i > 0; --i) {
            fc_object *popup = fc_object_for(fc, fc->popups[i - 1].popup_id);
            if (fc->popups[i - 1].used && fc->popups[i - 1].popup_id == object_id &&
                popup && popup->live && popup->kind == (uint16_t)FC_K_POPUP) {
                existing = &fc->popups[i - 1];
                break;
            }
        }
        fc_popup_record snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        bool have_record = false;
        if (existing) {
            snapshot = *existing;
            have_record = true;
        } else if (target && target->live &&
                   target->kind == (uint16_t)FC_K_POPUP) {
            snapshot.used = true;
            snapshot.popup_id = object_id;
            snapshot.xdg_surface_id = target->parent;
            fc_object *xdg = fc_object_for(fc, target->parent);
            snapshot.surface_id = xdg ? xdg->parent : 0;
            have_record = true;
        }
        if (have_record) {
            snapshot.from_reposition = true;
            snapshot.token = request.nargs >= 2 ? request.args[1].u : 0;
            fc_object *positioner =
                request.nargs >= 1 ? fc_object_for(fc, request.args[0].u) : NULL;
            if (positioner) {
                snapshot.pos = positioner->pos;
            }
            fake_compositor_popup_geometry(&snapshot.pos, &snapshot.x, &snapshot.y,
                                           &snapshot.width, &snapshot.height);
            if (fc->popup_count < FC_MAX_POPUP_RECORDS) {
                fc->popups[fc->popup_count++] = snapshot;
            }
            if (target) {
                reaction->send_repositioned = true;
                reaction->repositioned_popup = snapshot.popup_id;
                reaction->repositioned_token = snapshot.token;
                reaction->send_popup_configure = true;
                reaction->configure_popup = snapshot.popup_id;
                reaction->cx = snapshot.x;
                reaction->cy = snapshot.y;
                reaction->cw = snapshot.width;
                reaction->ch = snapshot.height;
                reaction->send_xdg_configure = true;
                reaction->configure_xdg = target->parent;
                reaction->configure_serial = ++fc->next_serial;
            }
        }
    }
    if (request.kind == (uint16_t)FC_K_DECORATION && opcode == 1) {
        reaction->send_decoration_configure = true;
        reaction->decoration_configure = object_id;
        reaction->decoration_mode = request.nargs >= 1 ? request.args[0].u : 1;
    }
    if (request.kind == (uint16_t)FC_K_SHM && opcode == 0 &&
        request.nargs >= 2 && request.args[1].i >= 0) {
        /* The server does not render from client pools; close the received fd
         * so create/destroy cycles do not accumulate descriptors. */
        close(request.args[1].i);
    }
    if (request.kind == (uint16_t)FC_K_DATA_OFFER && opcode == 1) {
        int fd = request.nargs >= 2 ? request.args[1].i : -1;
        if (fd >= 0 && fc->selection_text) {
            size_t length = strlen(fc->selection_text);
            const char *data = fc->selection_text;
            while (length > 0) {
                ssize_t written = write(fd, data, length);
                if (written <= 0) {
                    break;
                }
                data += (size_t)written;
                length -= (size_t)written;
            }
        }
        if (fd >= 0) {
            close(fd);
        }
    }
log:
    if (fc->request_count < FC_MAX_REQUESTS) {
        fc->requests[fc->request_count++] = request;
    } else {
        fc->request_overflow = true;
    }
    return request;
}

static inline void fc_apply_reaction(fake_compositor *fc, const fc_reaction *reaction) {
    for (size_t i = 0; i < reaction->release_count; ++i) {
        uint32_t buffer = reaction->releases[i];
        (void)fc_send_empty(fc, buffer, 0);
    }
    for (size_t i = 0; i < reaction->frame_count; ++i) {
        uint32_t callback = reaction->frames[i];
        uint32_t time;
        pthread_mutex_lock(&fc->state_lock);
        fc->next_time += 16;
        time = fc->next_time;
        pthread_mutex_unlock(&fc->state_lock);
        (void)fc_send_u32(fc, callback, 0, time);
    }
    if (reaction->callback_done) {
        uint32_t time;
        pthread_mutex_lock(&fc->state_lock);
        fc->next_time += 16;
        time = fc->next_time;
        pthread_mutex_unlock(&fc->state_lock);
        (void)fc_send_u32(fc, reaction->callback_done, 0, time);
    }
    if (reaction->keyboard_created) {
        (void)fc_send_keymap_to(fc, reaction->keyboard_created);
    }
    if (reaction->bind_kind) {
        switch ((fc_kind)reaction->bind_kind) {
        case FC_K_SHM:
            (void)fc_send_u32(fc, fc_live_id(fc, FC_K_SHM), 0, 0u); /* ARGB8888 */
            (void)fc_send_u32(fc, fc_live_id(fc, FC_K_SHM), 0, 1u); /* XRGB8888 */
            break;
        case FC_K_SEAT: {
            uint32_t seat = fc_live_id(fc, FC_K_SEAT);
            (void)fc_send_u32(fc, seat, 0, 3u); /* keyboard | pointer */
            fc_builder builder;
            fc_b_reset(&builder);
            fc_b_string(&builder, "pt-fake-seat");
            (void)fc_send(fc, seat, 1, &builder);
            break;
        }
        case FC_K_ICON_MANAGER: {
            uint32_t manager = fc_live_id(fc, FC_K_ICON_MANAGER);
            (void)fc_send_u32(fc, manager, 0, 32u); /* icon_size */
            (void)fc_send_empty(fc, manager, 1);    /* done */
            break;
        }
        default:
            break;
        }
    }
    if (reaction->send_toplevel_configure) {
        fc_builder builder;
        fc_b_reset(&builder);
        fc_b_i32(&builder, 0);
        fc_b_i32(&builder, 0);
        fc_b_u32(&builder, 0); /* no states */
        (void)fc_send(fc, reaction->configure_toplevel, 0, &builder);
    }
    if (reaction->send_repositioned) {
        (void)fc_send_u32(fc, reaction->repositioned_popup, 2,
                          reaction->repositioned_token);
    }
    if (reaction->send_popup_configure) {
        fc_builder builder;
        fc_b_reset(&builder);
        fc_b_i32(&builder, reaction->cx);
        fc_b_i32(&builder, reaction->cy);
        fc_b_i32(&builder, reaction->cw);
        fc_b_i32(&builder, reaction->ch);
        (void)fc_send(fc, reaction->configure_popup, 0, &builder);
    }
    if (reaction->send_xdg_configure) {
        (void)fc_send_u32(fc, reaction->configure_xdg, 0, reaction->configure_serial);
    }
    if (reaction->send_decoration_configure) {
        (void)fc_send_u32(fc, reaction->decoration_configure, 0,
                          reaction->decoration_mode);
    }
    if (reaction->send_globals) {
        fc_send_globals(fc);
    }
}

static inline void fc_dispatch_message(fake_compositor *fc, uint32_t object,
                                       uint16_t opcode, const uint8_t *payload,
                                       size_t payload_length) {
    fc_reaction reaction;
    fc_reaction_reset(&reaction);
    pthread_mutex_lock(&fc->state_lock);
    fc_request request =
        fc_decode_locked(fc, object, opcode, payload, payload_length, &reaction);
    bool overflow = fc->request_overflow;
    pthread_mutex_unlock(&fc->state_lock);
    if (overflow) {
        fake_compositor_set_error(fc, "request log overflow (%d entries)", FC_MAX_REQUESTS);
    }
    if (fc->verbose) {
        fprintf(stderr, "fake-compositor: request object=%u kind=%u opcode=%u nargs=%u\n",
                request.object, request.kind, request.opcode, request.nargs);
    }
    fc_apply_reaction(fc, &reaction);
}

static inline void fc_process_inbox(fake_compositor *fc) {
    size_t consumed = 0;
    while (fc->inbox_length - consumed >= 8u) {
        const uint8_t *message = fc->inbox + consumed;
        uint32_t object = fc_load_u32(message);
        uint16_t opcode = fc_load_u16(message + 4);
        uint16_t size = fc_load_u16(message + 6);
        if (size < 8u) {
            fake_compositor_set_error(fc, "invalid message size %u", size);
            consumed = fc->inbox_length;
            break;
        }
        if (fc->inbox_length - consumed < size) {
            break;
        }
        fc_dispatch_message(fc, object, opcode, message + 8, (size_t)size - 8u);
        consumed += size;
    }
    if (consumed > 0) {
        memmove(fc->inbox, fc->inbox + consumed, fc->inbox_length - consumed);
        fc->inbox_length -= consumed;
    }
}

static inline void fc_serve(fake_compositor *fc) {
    while (!fc->stop) {
        struct pollfd pfd = {.fd = fc->client_fd, .events = POLLIN};
        int ready = poll(&pfd, 1, 20);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }
        if (pfd.revents & POLLIN) {
            fc_receive_into_inbox(fc);
            fc_process_inbox(fc);
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }
    }
    pthread_mutex_lock(&fc->state_lock);
    fc->connected = false;
    pthread_mutex_unlock(&fc->state_lock);
}

static inline void *fc_thread_main(void *data) {
    fake_compositor *fc = data;
    int client = -1;
    while (!fc->stop) {
        struct pollfd pfd = {.fd = fc->listen_fd, .events = POLLIN};
        int ready = poll(&pfd, 1, 20);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;
        }
        client = accept(fc->listen_fd, NULL, NULL);
        break;
    }
    pthread_mutex_lock(&fc->state_lock);
    fc->client_fd = client;
    fc->connected = client >= 0;
    pthread_mutex_unlock(&fc->state_lock);
    if (client >= 0) {
        fc_serve(fc);
        close(client);
    }
    pthread_mutex_lock(&fc->state_lock);
    fc->client_fd = -1;
    fc->connected = false;
    pthread_mutex_unlock(&fc->state_lock);
    return NULL;
}

/* ---------- globals ---------- */

static inline void fc_send_global(fake_compositor *fc, uint32_t name,
                                  const char *interface, uint32_t version) {
    fc_builder builder;
    fc_b_reset(&builder);
    fc_b_u32(&builder, name);
    fc_b_string(&builder, interface);
    fc_b_u32(&builder, version);
    (void)fc_send(fc, fc_live_id(fc, FC_K_REGISTRY), 0, &builder);
}

static inline void fc_send_globals(fake_compositor *fc) {
    uint32_t registry = fc_live_id(fc, FC_K_REGISTRY);
    if (!registry) {
        return;
    }
    static const struct {
        const char *name;
        uint32_t version;
    } mandatory[] = {
        {"wl_compositor", 6},
        {"wl_shm", 1},
        {"wl_seat", 7},
        {"xdg_wm_base", 4},
    };
    uint32_t name = 1;
    for (size_t i = 0; i < sizeof(mandatory) / sizeof(mandatory[0]); ++i) {
        (void)fc_send_global(fc, name++, mandatory[i].name, mandatory[i].version);
    }
    if (fc->options & FC_OPT_ICON) {
        (void)fc_send_global(fc, name++, "xdg_toplevel_icon_manager_v1", 1);
    }
    if (fc->options & FC_OPT_DECORATION) {
        (void)fc_send_global(fc, name++, "zxdg_decoration_manager_v1", 1);
    }
    if (fc->options & FC_OPT_FRACTIONAL) {
        (void)fc_send_global(fc, name++, "wp_fractional_scale_manager_v1", 1);
    }
    if (fc->options & FC_OPT_VIEWPORTER) {
        (void)fc_send_global(fc, name++, "wp_viewporter", 1);
    }
    if (fc->options & FC_OPT_TEXT_INPUT) {
        uint32_t version = (fc->options & FC_OPT_TEXT_INPUT_V1) ? 1u : 2u;
        (void)fc_send_global(fc, name++, "zwp_text_input_manager_v3", version);
    }
    if (fc->options & FC_OPT_GESTURES) {
        (void)fc_send_global(fc, name++, "zwp_pointer_gestures_v1", 3);
    }
    if (fc->options & FC_OPT_DATA_DEVICE) {
        (void)fc_send_global(fc, name++, "wl_data_device_manager", 3);
    }
    if (fc->options & FC_OPT_PRIMARY) {
        (void)fc_send_global(fc, name++, "zwp_primary_selection_device_manager_v1", 1);
    }
    if (fc->options & FC_OPT_TABLET) {
        (void)fc_send_global(fc, name++, "zwp_tablet_manager_v2", 1);
    }
}

/* ---------- lifecycle ---------- */

static inline char *fc_strdup_or_null(const char *value) {
    return value ? strdup(value) : NULL;
}

static inline bool fc_make_keymap(fake_compositor *fc) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        return false;
    }
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        xkb_context_unref(context);
        return false;
    }
    char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    if (!text) {
        return false;
    }
    fc->keymap_text = text;
    fc->keymap_size = strlen(text) + 1u; /* include the terminating NUL */
    return true;
}

static inline void fc_cleanup_start(fake_compositor *fc) {
    if (fc->listen_fd >= 0) {
        close(fc->listen_fd);
        fc->listen_fd = -1;
    }
    if (fc->socket_path[0]) {
        unlink(fc->socket_path);
    }
    if (fc->dir[0]) {
        rmdir(fc->dir);
    }
    free(fc->inbox);
    fc->inbox = NULL;
    free(fc->keymap_text);
    fc->keymap_text = NULL;
    if (fc->had_display) {
        setenv("WAYLAND_DISPLAY", fc->saved_display, 1);
    } else {
        unsetenv("WAYLAND_DISPLAY");
    }
    if (fc->had_socket) {
        setenv("WAYLAND_SOCKET", fc->saved_socket, 1);
    } else {
        unsetenv("WAYLAND_SOCKET");
    }
    free(fc->saved_display);
    free(fc->saved_socket);
    fc->saved_display = NULL;
    fc->saved_socket = NULL;
}

/* Starts the fake compositor and points WAYLAND_DISPLAY at its private socket.
 * `options` is a mask of FC_OPT_* globals to advertise in addition to the four
 * mandatory ones. The caller must have PT_HEADLESS unset. */
static inline bool fake_compositor_start(fake_compositor *fc, unsigned int options) {
    if (!fc) {
        return false;
    }
    memset(fc, 0, sizeof(*fc));
    fc->listen_fd = -1;
    fc->client_fd = -1;
    fc->options = options;
    fc->next_server_id = 0xff000000u;
    pthread_mutex_init(&fc->state_lock, NULL);
    pthread_mutex_init(&fc->write_lock, NULL);
    fc->saved_display = fc_strdup_or_null(getenv("WAYLAND_DISPLAY"));
    fc->had_display = getenv("WAYLAND_DISPLAY") != NULL;
    fc->saved_socket = fc_strdup_or_null(getenv("WAYLAND_SOCKET"));
    fc->had_socket = getenv("WAYLAND_SOCKET") != NULL;
    fc->verbose = getenv("PT_FAKE_COMPOSITOR_VERBOSE") != NULL;
    /* The fake compositor writes clipboard payloads to pipes owned by the
     * client. A client that times out and closes its end must not kill the
     * test process with SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);

    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) {
        tmp = access("/tmp/opencode", W_OK) == 0 ? "/tmp/opencode" : "/tmp";
    }
    snprintf(fc->dir, sizeof(fc->dir), "%s/pt-fake-compositor-XXXXXX", tmp);
    if (!mkdtemp(fc->dir)) {
        snprintf(fc->error, sizeof(fc->error), "mkdtemp failed: %s", strerror(errno));
        fc_cleanup_start(fc);
        return false;
    }
    snprintf(fc->socket_path, sizeof(fc->socket_path), "%s/wayland-pt-fake", fc->dir);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        snprintf(fc->error, sizeof(fc->error), "socket failed: %s", strerror(errno));
        fc_cleanup_start(fc);
        return false;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(fc->socket_path) >= sizeof(address.sun_path)) {
        snprintf(fc->error, sizeof(fc->error), "socket path too long");
        close(fd);
        fc_cleanup_start(fc);
        return false;
    }
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", fc->socket_path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, 4) < 0) {
        snprintf(fc->error, sizeof(fc->error), "bind/listen failed: %s", strerror(errno));
        close(fd);
        fc_cleanup_start(fc);
        return false;
    }
    fc->listen_fd = fd;
    if (!fc_make_keymap(fc)) {
        snprintf(fc->error, sizeof(fc->error), "failed to build the test keymap");
        fc_cleanup_start(fc);
        return false;
    }
    unsetenv("WAYLAND_SOCKET");
    setenv("WAYLAND_DISPLAY", fc->socket_path, 1);
    if (pthread_create(&fc->thread, NULL, fc_thread_main, fc) != 0) {
        snprintf(fc->error, sizeof(fc->error), "pthread_create failed");
        fc_cleanup_start(fc);
        return false;
    }
    fc->thread_started = true;
    return true;
}

static inline void fake_compositor_stop(fake_compositor *fc) {
    if (!fc) {
        return;
    }
    pthread_mutex_lock(&fc->state_lock);
    fc->stop = true;
    bool connected = fc->connected;
    int client = fc->client_fd;
    pthread_mutex_unlock(&fc->state_lock);
    if (connected && client >= 0) {
        shutdown(client, SHUT_RDWR);
    }
    if (fc->thread_started) {
        pthread_join(fc->thread, NULL);
        fc->thread_started = false;
    }
    if (fc->client_fd >= 0) {
        close(fc->client_fd);
        fc->client_fd = -1;
    }
    fc_close_queued_fds(fc);
    if (fc->listen_fd >= 0) {
        close(fc->listen_fd);
        fc->listen_fd = -1;
    }
    if (fc->socket_path[0]) {
        unlink(fc->socket_path);
        fc->socket_path[0] = '\0';
    }
    if (fc->dir[0]) {
        rmdir(fc->dir);
        fc->dir[0] = '\0';
    }
    if (fc->had_display) {
        setenv("WAYLAND_DISPLAY", fc->saved_display ? fc->saved_display : "", 1);
    } else {
        unsetenv("WAYLAND_DISPLAY");
    }
    if (fc->had_socket) {
        setenv("WAYLAND_SOCKET", fc->saved_socket ? fc->saved_socket : "", 1);
    } else {
        unsetenv("WAYLAND_SOCKET");
    }
    free(fc->saved_display);
    free(fc->saved_socket);
    free(fc->inbox);
    free(fc->keymap_text);
    free(fc->selection_text);
    fc->inbox = NULL;
    fc->keymap_text = NULL;
    fc->selection_text = NULL;
    pthread_mutex_destroy(&fc->state_lock);
    pthread_mutex_destroy(&fc->write_lock);
}

/* ---------- client driving ---------- */

static inline int fake_compositor_dispatch(fake_compositor *fc, pt_app *app,
                                           int timeout_ms) {
    (void)fc;
    if (!app || !app->platform) {
        return -1;
    }
    return pt_platform_dispatch(app, timeout_ms);
}

/* Flushes queued requests and waits for the server. After this returns all
 * requests sent before the call have been decoded and logged, and all events
 * the server wrote before the call were dispatched by the client. */
static inline bool fake_compositor_sync(fake_compositor *fc, pt_app *app) {
    (void)fc;
    if (!app || !app->platform) {
        return false;
    }
    pt_platform *platform = app->platform;
    if (!platform->display) {
        return false;
    }
    return wl_display_roundtrip(platform->display) >= 0;
}

/* Several roundtrips cover the bind -> capabilities -> device creation chain
 * (each step needs a fresh flush). */
static inline bool fake_compositor_settle(fake_compositor *fc, pt_app *app) {
    for (int i = 0; i < 6; ++i) {
        if (!fake_compositor_sync(fc, app)) {
            return false;
        }
    }
    return true;
}

/* Roundtrips until at least `min_count` requests of kind/opcode have been
 * logged, or `max_roundtrips` is exhausted. A single roundtrip only flushes
 * requests queued after its flush, so slow clients need more than one before
 * a reply to an injected event becomes visible. */
static inline bool fake_compositor_wait_request(fake_compositor *fc, pt_app *app,
                                                uint32_t kind, uint16_t opcode,
                                                size_t min_count, int max_roundtrips) {
    if (!fc || !app) {
        return false;
    }
    for (int i = 0; i < max_roundtrips; ++i) {
        if (fake_compositor_count_requests(fc, kind, opcode) >= min_count) {
            return true;
        }
        if (!fake_compositor_sync(fc, app)) {
            return false;
        }
    }
    return fake_compositor_count_requests(fc, kind, opcode) >= min_count;
}

/* Dispatches for up to timeout_ms, processing frames and configured events. */
static inline void fake_compositor_pump(fake_compositor *fc, pt_app *app, int timeout_ms) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - start.tv_sec) * 1000L +
                       (now.tv_nsec - start.tv_nsec) / 1000000L;
        if (elapsed >= timeout_ms) {
            break;
        }
        fake_compositor_dispatch(fc, app, 5);
    }
}

/* Counts open descriptors in the test process (server fds included). */
static inline int fake_compositor_fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        return -1;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            ++count;
        }
    }
    closedir(dir);
    return count - 1; /* the directory stream itself */
}

#endif