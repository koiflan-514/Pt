/*
 * Pt Wayland backend internals.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_wayland_internal_h
#define pt_wayland_internal_h

#include "internal.h"

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "fractional-scale-v1-client-protocol.h"
#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "tablet-unstable-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define pt_wl_buffer_count 3

struct pt_wayland_window;
typedef struct pt_wayland_window pt_wayland_window;

struct pt_wl_popup;
typedef struct pt_wl_popup pt_wl_popup;

typedef struct pt_wl_surface_ref {
    pt_window *window;
    pt_overlay *overlay;
    float offset_x;
    float offset_y;
} pt_wl_surface_ref;

typedef struct pt_wl_buffer {
    struct wl_buffer *buffer;
    void *data;
    bool busy;
    struct pt_wayland_window *owner;
    pt_rect pending_damage;
    bool pending_valid;
    bool pending_full;
} pt_wl_buffer;

struct pt_wayland_window {
    pt_window *window;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    struct wp_fractional_scale_v1 *fractional_scale;
    struct wp_viewport *viewport;
    struct wl_callback *frame_callback;

    pt_wl_buffer buffers[pt_wl_buffer_count];
    void *pool_data;
    size_t pool_size;
    int buffer_width;
    int buffer_height;
    int stride;

    int pending_width;
    int pending_height;
    bool configured;
    bool needs_redraw;
    bool mapped;
    bool buffer_scale_applied;
    bool drawing;
    bool redraw_pending;
};

struct pt_wl_offer {
    struct wl_data_offer *offer;
    struct pt_platform *platform;
    char *mimes[32];
    int mime_count;
    uint32_t source_actions;
    uint32_t action;
    bool in_use;
    struct pt_wl_offer *next;
};

typedef struct pt_wl_drag_type {
    char *mime_type;
    char *data;
    size_t size;
} pt_wl_drag_type;

typedef struct pt_wl_source_write {
    int fd;
    char *data;
    size_t size;
    size_t offset;
    struct pt_wl_source_write *next;
} pt_wl_source_write;

struct pt_wl_tablet_tool {
    struct zwp_tablet_tool_v2 *tool;
    struct zwp_tablet_v2 *tablet;
    struct pt_platform *platform;
    pt_window *window;
    pt_stylus_tool type;
    bool in_range;
    bool down;
    bool pending_enter;
    bool pending_down;
    bool pending_up;
    bool pending_motion;
    float x;
    float y;
    float offset_x;
    float offset_y;
    float pressure;
    float distance;
    float tilt_x;
    float tilt_y;
    float rotation;
    uint32_t buttons;
};

typedef struct pt_wl_tablet_tool pt_wl_tablet_tool;

struct pt_platform {
    pt_app *app;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct zwp_pointer_gestures_v1 *gestures;
    struct zwp_pointer_gesture_swipe_v1 *gesture_swipe;
    struct zwp_pointer_gesture_pinch_v1 *gesture_pinch;
    struct zwp_pointer_gesture_hold_v1 *gesture_hold;
    pt_window *gesture_window;
    struct wl_keyboard *keyboard;
    struct wl_touch *touch;
    struct zwp_tablet_manager_v2 *tablet_manager;
    struct zwp_tablet_seat_v2 *tablet_seat;
    struct pt_wl_tablet_tool *tablet_tools[8];
    uint32_t tablet_tool_count;
    struct wl_data_device_manager *data_device_manager;
    struct wl_data_device *data_device;
    struct wl_data_source *data_source;
    struct wl_data_source *drag_source;
    pt_window *drag_source_window;
    pt_wl_drag_type *drag_types;
    uint32_t drag_type_count;
    uint32_t drag_action;
    uint32_t drag_finished_action;
    bool drag_drop_performed;
    pt_wl_source_write *source_writes;
    struct wl_surface *drag_icon_surface;
    struct wl_buffer *drag_icon_buffer;
    void *drag_icon_data;
    size_t drag_icon_size;
    int drag_icon_width;
    int drag_icon_height;
    struct pt_wl_offer *offers;
    struct pt_wl_offer *selection;

    struct pt_wl_offer *drag_offer;
    pt_window *drag_window;
    char *drag_mime;
    float drag_x;
    float drag_y;
    float drag_offset_x;
    float drag_offset_y;
    uint32_t drag_serial;

    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct wp_viewporter *viewporter;
    struct zwp_text_input_manager_v3 *text_input_manager;
    struct zwp_text_input_v3 *text_input;
    pt_window *text_input_window;
    bool text_input_enabled;

    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    struct wl_cursor_image *cursor_image;
    int cursor_theme_size;
    pt_cursor cursor_kind;
    uint32_t pointer_serial;

    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    pt_window *pointer_focus;
    pt_window *keyboard_focus;
    pt_window *touch_focus;
    uint32_t serial;

    pt_wl_popup *popups;

    float pointer_offset_x;
    float pointer_offset_y;
    pt_overlay *pointer_overlay;
    float touch_offset_x;
    float touch_offset_y;

    uint32_t axis_source;
    uint32_t axis_last_time;
    float axis_velocity;
    bool axis_momentum_armed;

    char *clipboard_text;
    char *pending_preedit;
    char *pending_commit;
    int32_t pending_preedit_begin;
    int32_t pending_preedit_end;
    uint32_t pending_delete_before;
    uint32_t pending_delete_after;

    uint32_t repeat_key;
    uint32_t repeat_keysym;
    uint32_t repeat_modifiers;
    char repeat_utf8[32];
    uint64_t repeat_timer;
    int32_t repeat_rate;
    int32_t repeat_delay;
    bool repeat_active;

    bool headless;
    bool started;
};

pt_wayland_window *pt_wl_window_for(pt_window *window);
pt_platform *pt_platform_for(pt_window *window);
bool pt_wl_resolve_surface(pt_platform *platform, struct wl_surface *surface,
                           pt_wl_surface_ref *out);
int pt_wl_create_shm_file(size_t size);
void pt_wl_window_schedule_frame(pt_window *window);
bool pt_wl_window_draw(pt_window *window);
void pt_wl_window_apply_scale(pt_window *window);
void pt_wl_window_update_viewport(pt_window *window);

#endif
