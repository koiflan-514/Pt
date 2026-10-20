/*
 * Pt Wayland platform: registry, input, clipboard, IME and event loop.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "wayland_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#define pt_clipboard_max_bytes (16u * 1024u * 1024u)
#define pt_clipboard_timeout_ms 1500

static pt_window *window_for_surface(pt_platform *platform, struct wl_surface *surface) {
    if (!platform || !platform->app || !surface) {
        return NULL;
    }
    for (pt_window *window = platform->app->first_window; window; window = window->next) {
        pt_wayland_window *wl = pt_wl_window_for(window);
        if (wl && wl->surface == surface) {
            return window;
        }
    }
    return NULL;
}

static void cancel_repeat(pt_platform *platform) {
    if (platform->repeat_active && platform->app && platform->repeat_timer) {
        pt_app_cancel_timeout(platform->app, platform->repeat_timer);
    }
    platform->repeat_active = false;
    platform->repeat_key = 0;
    platform->repeat_timer = 0;
}

static void repeat_fire(pt_app *app, void *data) {
    (void)app;
    pt_platform *platform = data;
    if (!platform->repeat_active || !platform->keyboard_focus) {
        return;
    }
    pt_window *window = platform->keyboard_focus;
    const char *text = platform->repeat_utf8[0] ? platform->repeat_utf8 : NULL;
    pt_window_input_key(window, platform->repeat_keysym, platform->repeat_modifiers, text, true);
    uint32_t rate = platform->repeat_rate > 0 ? (uint32_t)platform->repeat_rate : 25u;
    platform->repeat_timer =
        pt_app_add_timeout_internal(platform->app, 1000u / rate, repeat_fire, platform);
}

static void begin_repeat(pt_platform *platform, pt_window *window, uint32_t key,
                         uint32_t keysym, uint32_t modifiers, const char *text) {
    cancel_repeat(platform);
    if (!window || !pt_window_has_editable_focus(window) || !platform->app) {
        return;
    }
    platform->repeat_active = true;
    platform->repeat_key = key;
    platform->repeat_keysym = keysym;
    platform->repeat_modifiers = modifiers;
    if (text) {
        snprintf(platform->repeat_utf8, sizeof(platform->repeat_utf8), "%s", text);
    } else {
        platform->repeat_utf8[0] = '\0';
    }
    int32_t delay = platform->repeat_delay > 0 ? platform->repeat_delay : 500;
    platform->repeat_timer =
        pt_app_add_timeout_internal(platform->app, (uint32_t)delay, repeat_fire, platform);
}

/* ---------- keyboard ---------- */

static uint32_t keyboard_modifiers(pt_platform *platform) {
    uint32_t modifiers = 0;
    if (!platform->xkb_state) {
        return 0;
    }
    if (xkb_state_mod_name_is_active(platform->xkb_state, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= (uint32_t)pt_key_modifier_shift;
    }
    if (xkb_state_mod_name_is_active(platform->xkb_state, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= (uint32_t)pt_key_modifier_ctrl;
    }
    if (xkb_state_mod_name_is_active(platform->xkb_state, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= (uint32_t)pt_key_modifier_alt;
    }
    if (xkb_state_mod_name_is_active(platform->xkb_state, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) {
        modifiers |= (uint32_t)pt_key_modifier_logo;
    }
    return modifiers;
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
                            int32_t fd, uint32_t size) {
    (void)keyboard;
    pt_platform *platform = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0 || fd < 0 ||
        !platform->xkb_context) {
        close(fd);
        return;
    }
    char *mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return;
    }
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_string(platform->xkb_context, mapped,
                                   XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(mapped, size);
    close(fd);
    if (!keymap) {
        pt_log_warn("failed to compile keymap");
        return;
    }
    struct xkb_state *state =
        xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        pt_log_warn("failed to create xkb state");
        return;
    }
    if (platform->xkb_state) {
        xkb_state_unref(platform->xkb_state);
    }
    if (platform->xkb_keymap) {
        xkb_keymap_unref(platform->xkb_keymap);
    }
    platform->xkb_keymap = keymap;
    platform->xkb_state = state;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
    (void)keyboard;
    (void)keys;
    pt_platform *platform = data;
    platform->serial = serial;
    pt_wl_surface_ref ref = {0};
    pt_window *window =
        pt_wl_resolve_surface(platform, surface, &ref) ? ref.window : NULL;
    platform->keyboard_focus = window;
    if (window) {
        pt_window_input_keyboard_focus(window, true, serial);
    }
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface) {
    (void)keyboard;
    (void)serial;
    pt_platform *platform = data;
    pt_window *window = platform->keyboard_focus;
    pt_wl_surface_ref ref = {0};
    if (pt_wl_resolve_surface(platform, surface, &ref)) {
        window = ref.window;
    }
    cancel_repeat(platform);
    if (window) {
        pt_window_input_keyboard_focus(window, false, serial);
    }
    if (platform->keyboard_focus == window) {
        platform->keyboard_focus = NULL;
    }
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    (void)keyboard;
    (void)time;
    pt_platform *platform = data;
    pt_window *window = platform->keyboard_focus;
    if (!window) {
        return;
    }
    platform->serial = serial;
    uint32_t keycode = key + 8u;
    bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (!pressed) {
        if (platform->repeat_active && platform->repeat_key == keycode) {
            cancel_repeat(platform);
        }
        return;
    }
    if (!platform->xkb_state) {
        return;
    }
    uint32_t keysym = (uint32_t)xkb_state_key_get_one_sym(platform->xkb_state, keycode);
    uint32_t modifiers = keyboard_modifiers(platform);
    char utf8[32] = {0};
    bool accelerators = (modifiers & ((uint32_t)pt_key_modifier_ctrl |
                                      (uint32_t)pt_key_modifier_alt |
                                      (uint32_t)pt_key_modifier_logo)) != 0;
    /* Text input being enabled does not mean the compositor forwards every
     * key to the input method. Unconsumed keys still arrive here and carry the
     * text the user typed; only IME composition arrives as commit_string. */
    if (!accelerators) {
        xkb_state_key_get_utf8(platform->xkb_state, keycode, utf8, sizeof(utf8));
    }
    pt_window_input_key(window, keysym, modifiers, utf8[0] ? utf8 : NULL, true);
    begin_repeat(platform, window, keycode, keysym, modifiers, utf8[0] ? utf8 : NULL);
}

static void keyboard_modifiers_changed(void *data, struct wl_keyboard *keyboard,
                                       uint32_t serial, uint32_t depressed,
                                       uint32_t latched, uint32_t locked, uint32_t group) {
    (void)keyboard;
    (void)serial;
    pt_platform *platform = data;
    if (platform->xkb_state) {
        xkb_state_update_mask(platform->xkb_state, depressed, latched, locked, 0, 0, group);
    }
    if (platform->keyboard_focus) {
        pt_window_set_input_modifiers(platform->keyboard_focus,
                                      keyboard_modifiers(platform));
    }
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
                                 int32_t delay) {
    (void)keyboard;
    pt_platform *platform = data;
    platform->repeat_rate = rate;
    platform->repeat_delay = delay > 0 ? delay : 500;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers_changed,
    .repeat_info = keyboard_repeat_info,
};

/* ---------- pointer ---------- */

static const char *cursor_name_for(pt_cursor cursor) {
    switch (cursor) {
    case pt_cursor_text:
        return "text";
    case pt_cursor_pointer:
        return "pointer";
    case pt_cursor_resize_h:
        return "ew-resize";
    case pt_cursor_resize_v:
        return "ns-resize";
    case pt_cursor_resize_nwse:
        return "nwse-resize";
    case pt_cursor_resize_nesw:
        return "nesw-resize";
    case pt_cursor_not_allowed:
        return "not-allowed";
    case pt_cursor_default:
    default:
        return "default";
    }
}

void pt_platform_window_set_cursor(pt_window *window, pt_cursor cursor) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform || platform->headless || !platform->pointer || !platform->shm) {
        return;
    }
    if (!platform->pointer_focus) {
        return;
    }
    float scale = window && window->scale > 0.0f ? window->scale : 1.0f;
    int size = (int)(24.0f * scale + 0.5f);
    if (size < 16) {
        size = 16;
    }
    if (size > 96) {
        size = 96;
    }
    if (!platform->cursor_theme || platform->cursor_theme_size != size) {
        if (platform->cursor_theme) {
            wl_cursor_theme_destroy(platform->cursor_theme);
            platform->cursor_theme = NULL;
            platform->cursor_image = NULL;
        }
        platform->cursor_theme = wl_cursor_theme_load(NULL, size, platform->shm);
        platform->cursor_theme_size = size;
    }
    if (!platform->cursor_theme) {
        return;
    }
    struct wl_cursor *wl_cursor =
        wl_cursor_theme_get_cursor(platform->cursor_theme, cursor_name_for(cursor));
    if (!wl_cursor || wl_cursor->image_count == 0) {
        wl_cursor = wl_cursor_theme_get_cursor(platform->cursor_theme, "default");
    }
    if (!wl_cursor || wl_cursor->image_count == 0) {
        return;
    }
    struct wl_cursor_image *image = wl_cursor->images[0];
    if (!platform->cursor_surface) {
        platform->cursor_surface = wl_compositor_create_surface(platform->compositor);
        if (!platform->cursor_surface) {
            return;
        }
    }
    struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) {
        return;
    }
    wl_surface_attach(platform->cursor_surface, buffer, 0, 0);
    wl_surface_damage(platform->cursor_surface, 0, 0, (int32_t)image->width,
                      (int32_t)image->height);
    wl_surface_commit(platform->cursor_surface);
    platform->cursor_image = image;
    platform->cursor_kind = cursor;
    wl_pointer_set_cursor(platform->pointer, platform->pointer_serial, platform->cursor_surface,
                          (int32_t)image->hotspot_x, (int32_t)image->hotspot_y);
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    pt_platform *platform = data;
    platform->pointer_serial = serial;
    platform->serial = serial;
    pt_wl_surface_ref ref = {0};
    bool found = pt_wl_resolve_surface(platform, surface, &ref);
    pt_window *window = found ? ref.window : NULL;
    platform->pointer_focus = window;
    platform->pointer_overlay = found ? ref.overlay : NULL;
    platform->pointer_offset_x = found ? ref.offset_x : 0.0f;
    platform->pointer_offset_y = found ? ref.offset_y : 0.0f;
    if (!window) {
        return;
    }
    pt_window_input_pointer_enter(window, (float)wl_fixed_to_double(sx) + ref.offset_x,
                                  (float)wl_fixed_to_double(sy) + ref.offset_y);
    pt_platform_window_set_cursor(window, window->current_cursor);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    pt_platform *platform = data;
    pt_window *window = platform->pointer_focus;
    pt_wl_surface_ref ref = {0};
    if (pt_wl_resolve_surface(platform, surface, &ref)) {
        window = ref.window;
    }
    if (window) {
        pt_window_input_pointer_leave(window);
    }
    platform->pointer_focus = NULL;
    platform->pointer_overlay = NULL;
    platform->pointer_offset_x = 0.0f;
    platform->pointer_offset_y = 0.0f;
    platform->axis_momentum_armed = false;
    platform->axis_velocity = 0.0f;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer;
    pt_platform *platform = data;
    if (platform->pointer_focus) {
        pt_window_input_pointer_motion(
            platform->pointer_focus,
            (float)wl_fixed_to_double(sx) + platform->pointer_offset_x,
            (float)wl_fixed_to_double(sy) + platform->pointer_offset_y, time);
    }
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer;
    pt_platform *platform = data;
    platform->serial = serial;
    if (!platform->pointer_focus) {
        return;
    }
    bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
    pt_window *window = platform->pointer_focus;
    pt_window_input_pointer_button(window, button, pressed, window->pointer_x,
                                   window->pointer_y, serial, time);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                         wl_fixed_t value) {
    (void)pointer;
    pt_platform *platform = data;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !platform->pointer_focus) {
        return;
    }
    pt_window *window = platform->pointer_focus;
    float delta = (float)wl_fixed_to_double(value) / 10.0f;
    if (platform->axis_momentum_armed && time > platform->axis_last_time) {
        uint32_t elapsed = time - platform->axis_last_time;
        if (elapsed >= 1 && elapsed <= 120) {
            float instant = delta * 48.0f * 1000.0f / (float)elapsed;
            platform->axis_velocity =
                platform->axis_velocity * 0.35f + instant * 0.65f;
            platform->axis_velocity = pt_clamp_float(platform->axis_velocity, -6000.0f, 6000.0f);
        }
    } else {
        platform->axis_velocity = 0.0f;
    }
    platform->axis_last_time = time;
    platform->axis_momentum_armed = true;
    pt_window_input_axis(window, window->pointer_x, window->pointer_y, delta);
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {
    (void)pointer;
    pt_platform *platform = data;
    if (platform->axis_source != source) {
        platform->axis_velocity = 0.0f;
        platform->axis_momentum_armed = false;
    }
    platform->axis_source = source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                              uint32_t axis) {
    (void)pointer;
    (void)time;
    pt_platform *platform = data;
    bool continuous = platform->axis_source == WL_POINTER_AXIS_SOURCE_FINGER ||
                      platform->axis_source == WL_POINTER_AXIS_SOURCE_CONTINUOUS;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL && continuous && platform->pointer_focus &&
        platform->axis_momentum_armed) {
        pt_window *window = platform->pointer_focus;
        pt_window_momentum_start(window, platform->axis_velocity, window->pointer_x,
                                 window->pointer_y);
    }
    platform->axis_momentum_armed = false;
    platform->axis_velocity = 0.0f;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

/* ---------- pointer gestures ---------- */

static pt_window *gesture_window_for(pt_platform *platform, struct wl_surface *surface) {
    pt_wl_surface_ref ref = {0};
    if (surface && pt_wl_resolve_surface(platform, surface, &ref)) {
        return ref.window;
    }
    return platform->pointer_focus;
}

static void gesture_swipe_begin(void *data, struct zwp_pointer_gesture_swipe_v1 *gesture,
                                uint32_t serial, uint32_t time, struct wl_surface *surface,
                                uint32_t fingers) {
    (void)gesture;
    (void)serial;
    (void)time;
    pt_platform *platform = data;
    platform->gesture_window = gesture_window_for(platform, surface);
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_swipe,
                                pt_gesture_phase_begin, fingers, 0.0f, 0.0f, 1.0f, 0.0f);
    }
}

static void gesture_swipe_update(void *data, struct zwp_pointer_gesture_swipe_v1 *gesture,
                                 uint32_t time, wl_fixed_t dx, wl_fixed_t dy) {
    (void)gesture;
    (void)time;
    pt_platform *platform = data;
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_swipe,
                                pt_gesture_phase_update, 0,
                                (float)wl_fixed_to_double(dx),
                                (float)wl_fixed_to_double(dy), 1.0f, 0.0f);
    }
}

static void gesture_swipe_end(void *data, struct zwp_pointer_gesture_swipe_v1 *gesture,
                              uint32_t serial, uint32_t time, int32_t cancelled) {
    (void)gesture;
    (void)serial;
    (void)time;
    pt_platform *platform = data;
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_swipe,
                                cancelled ? pt_gesture_phase_cancel : pt_gesture_phase_end, 0,
                                0.0f, 0.0f, 1.0f, 0.0f);
    }
    platform->gesture_window = NULL;
}

static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = {
    .begin = gesture_swipe_begin,
    .update = gesture_swipe_update,
    .end = gesture_swipe_end,
};

static void gesture_pinch_begin(void *data, struct zwp_pointer_gesture_pinch_v1 *gesture,
                                uint32_t serial, uint32_t time, struct wl_surface *surface,
                                uint32_t fingers) {
    (void)gesture;
    (void)serial;
    (void)time;
    pt_platform *platform = data;
    platform->gesture_window = gesture_window_for(platform, surface);
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_pinch,
                                pt_gesture_phase_begin, fingers, 0.0f, 0.0f, 1.0f, 0.0f);
    }
}

static void gesture_pinch_update(void *data, struct zwp_pointer_gesture_pinch_v1 *gesture,
                                 uint32_t time, wl_fixed_t dx, wl_fixed_t dy,
                                 wl_fixed_t scale, wl_fixed_t rotation) {
    (void)gesture;
    (void)time;
    pt_platform *platform = data;
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_pinch,
                                pt_gesture_phase_update, 0,
                                (float)wl_fixed_to_double(dx),
                                (float)wl_fixed_to_double(dy),
                                (float)wl_fixed_to_double(scale),
                                (float)wl_fixed_to_double(rotation) * 0.017453292519943295f);
    }
}

static void gesture_pinch_end(void *data, struct zwp_pointer_gesture_pinch_v1 *gesture,
                              uint32_t serial, uint32_t time, int32_t cancelled) {
    (void)gesture;
    (void)serial;
    (void)time;
    pt_platform *platform = data;
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_pinch,
                                cancelled ? pt_gesture_phase_cancel : pt_gesture_phase_end, 0,
                                0.0f, 0.0f, 1.0f, 0.0f);
    }
    platform->gesture_window = NULL;
}

static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
    .begin = gesture_pinch_begin,
    .update = gesture_pinch_update,
    .end = gesture_pinch_end,
};

static void gesture_hold_begin(void *data, struct zwp_pointer_gesture_hold_v1 *gesture,
                               uint32_t serial, uint32_t time, struct wl_surface *surface,
                               uint32_t fingers) {
    (void)gesture;
    (void)serial;
    (void)time;
    pt_platform *platform = data;
    platform->gesture_window = gesture_window_for(platform, surface);
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_hold,
                                pt_gesture_phase_begin, fingers, 0.0f, 0.0f, 1.0f, 0.0f);
    }
}

static void gesture_hold_end(void *data, struct zwp_pointer_gesture_hold_v1 *gesture,
                             uint32_t serial, uint32_t time, int32_t cancelled) {
    (void)gesture;
    (void)serial;
    (void)time;
    pt_platform *platform = data;
    if (platform->gesture_window) {
        pt_window_gesture_input(platform->gesture_window, pt_gesture_kind_hold,
                                cancelled ? pt_gesture_phase_cancel : pt_gesture_phase_end, 0,
                                0.0f, 0.0f, 1.0f, 0.0f);
    }
    platform->gesture_window = NULL;
}

static const struct zwp_pointer_gesture_hold_v1_listener hold_listener = {
    .begin = gesture_hold_begin,
    .end = gesture_hold_end,
};

static void ensure_gesture_devices(pt_platform *platform) {
    if (!platform->gestures || !platform->pointer) {
        return;
    }
    if (!platform->gesture_swipe) {
        platform->gesture_swipe =
            zwp_pointer_gestures_v1_get_swipe_gesture(platform->gestures, platform->pointer);
        if (platform->gesture_swipe) {
            zwp_pointer_gesture_swipe_v1_add_listener(platform->gesture_swipe,
                                                      &swipe_listener, platform);
        }
    }
    if (!platform->gesture_pinch) {
        platform->gesture_pinch =
            zwp_pointer_gestures_v1_get_pinch_gesture(platform->gestures, platform->pointer);
        if (platform->gesture_pinch) {
            zwp_pointer_gesture_pinch_v1_add_listener(platform->gesture_pinch,
                                                      &pinch_listener, platform);
        }
    }
    if (zwp_pointer_gestures_v1_get_version(platform->gestures) >= 3 &&
        !platform->gesture_hold) {
        platform->gesture_hold =
            zwp_pointer_gestures_v1_get_hold_gesture(platform->gestures, platform->pointer);
        if (platform->gesture_hold) {
            zwp_pointer_gesture_hold_v1_add_listener(platform->gesture_hold, &hold_listener,
                                                     platform);
        }
    }
}

static void destroy_gesture_devices(pt_platform *platform) {
    if (platform->gesture_hold) {
        zwp_pointer_gesture_hold_v1_destroy(platform->gesture_hold);
        platform->gesture_hold = NULL;
    }
    if (platform->gesture_pinch) {
        zwp_pointer_gesture_pinch_v1_destroy(platform->gesture_pinch);
        platform->gesture_pinch = NULL;
    }
    if (platform->gesture_swipe) {
        zwp_pointer_gesture_swipe_v1_destroy(platform->gesture_swipe);
        platform->gesture_swipe = NULL;
    }
    if (platform->gestures) {
        zwp_pointer_gestures_v1_destroy(platform->gestures);
        platform->gestures = NULL;
    }
    platform->gesture_window = NULL;
}

/* ---------- stylus (tablet-v2) ---------- */

static pt_stylus_tool stylus_tool_from_wayland(uint32_t type) {
    switch (type) {
    case ZWP_TABLET_TOOL_V2_TYPE_PEN:
        return pt_stylus_tool_pen;
    case ZWP_TABLET_TOOL_V2_TYPE_ERASER:
        return pt_stylus_tool_eraser;
    case ZWP_TABLET_TOOL_V2_TYPE_BRUSH:
        return pt_stylus_tool_brush;
    case ZWP_TABLET_TOOL_V2_TYPE_PENCIL:
        return pt_stylus_tool_pencil;
    case ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH:
        return pt_stylus_tool_airbrush;
    default:
        return pt_stylus_tool_unknown;
    }
}

static void stylus_emit(pt_wl_tablet_tool *state, pt_stylus_phase phase) {
    if (!state->window) {
        return;
    }
    pt_window_stylus_input(state->window, phase, state->type, state->x, state->y,
                           state->pressure, state->distance, state->tilt_x, state->tilt_y,
                           state->rotation, state->buttons);
}

static void tablet_tool_type(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t tool_type) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    state->type = stylus_tool_from_wayland(tool_type);
}

static void tablet_tool_hardware_serial(void *data, struct zwp_tablet_tool_v2 *tool,
                                        uint32_t serial_hi, uint32_t serial_lo) {
    (void)data;
    (void)tool;
    (void)serial_hi;
    (void)serial_lo;
}

static void tablet_tool_hardware_id_wacom(void *data, struct zwp_tablet_tool_v2 *tool,
                                          uint32_t id_hi, uint32_t id_lo) {
    (void)data;
    (void)tool;
    (void)id_hi;
    (void)id_lo;
}

static void tablet_tool_capability(void *data, struct zwp_tablet_tool_v2 *tool,
                                   uint32_t capability) {
    (void)data;
    (void)tool;
    (void)capability;
}

static void tablet_tool_done(void *data, struct zwp_tablet_tool_v2 *tool) {
    (void)data;
    (void)tool;
}

static void tablet_tool_proximity_in(void *data, struct zwp_tablet_tool_v2 *tool,
                                     uint32_t serial, struct zwp_tablet_v2 *tablet,
                                     struct wl_surface *surface) {
    (void)tool;
    (void)serial;
    pt_wl_tablet_tool *state = data;
    pt_wl_surface_ref ref = {0};
    state->tablet = tablet;
    state->window = NULL;
    state->in_range = false;
    if (!pt_wl_resolve_surface(state->platform, surface, &ref) || !ref.window) {
        return;
    }
    state->window = ref.window;
    state->offset_x = ref.offset_x;
    state->offset_y = ref.offset_y;
    state->in_range = true;
    state->pending_enter = true;
    state->window->stylus_in_range = true;
}

static void tablet_tool_flush_up(pt_wl_tablet_tool *state) {
    if (!state->down && !state->pending_down && !state->pending_up) {
        return;
    }
    state->down = false;
    state->pending_down = false;
    state->pending_up = false;
    if (state->window) {
        state->window->stylus_down = false;
    }
    stylus_emit(state, pt_stylus_phase_up);
}

static void tablet_tool_proximity_out(void *data, struct zwp_tablet_tool_v2 *tool) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    if (!state->in_range) {
        return;
    }
    tablet_tool_flush_up(state);
    if (state->window) {
        stylus_emit(state, pt_stylus_phase_leave);
        state->window->stylus_in_range = false;
        state->window->stylus_down = false;
    }
    state->buttons = 0;
    state->window = NULL;
    state->in_range = false;
    state->pending_enter = false;
    state->pending_motion = false;
}

static void tablet_tool_down(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial) {
    (void)tool;
    (void)serial;
    pt_wl_tablet_tool *state = data;
    if (!state->in_range) {
        return;
    }
    state->down = true;
    state->pending_down = true;
    if (state->window) {
        state->window->stylus_down = true;
    }
}

static void tablet_tool_up(void *data, struct zwp_tablet_tool_v2 *tool) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    if (!state->down && !state->pending_down) {
        return;
    }
    state->down = false;
    state->pending_up = true;
    if (state->window) {
        state->window->stylus_down = false;
    }
}

static void tablet_tool_motion(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x,
                               wl_fixed_t y) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    if (!state->in_range) {
        return;
    }
    state->x = (float)wl_fixed_to_double(x) + state->offset_x;
    state->y = (float)wl_fixed_to_double(y) + state->offset_y;
    state->pending_motion = true;
}

static void tablet_tool_pressure(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t pressure) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    state->pressure = (float)pressure / 65535.0f;
    state->pending_motion = true;
}

static void tablet_tool_distance(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t distance) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    state->distance = (float)distance / 65535.0f;
    state->pending_motion = true;
}

static void tablet_tool_tilt(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t tilt_x,
                             wl_fixed_t tilt_y) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    state->tilt_x = (float)wl_fixed_to_double(tilt_x);
    state->tilt_y = (float)wl_fixed_to_double(tilt_y);
    state->pending_motion = true;
}

static void tablet_tool_rotation(void *data, struct zwp_tablet_tool_v2 *tool,
                                 wl_fixed_t degrees) {
    (void)tool;
    pt_wl_tablet_tool *state = data;
    state->rotation = (float)wl_fixed_to_double(degrees) * 0.017453292519943295f;
    state->pending_motion = true;
}

static void tablet_tool_slider(void *data, struct zwp_tablet_tool_v2 *tool, int32_t position) {
    (void)data;
    (void)tool;
    (void)position;
}

static void tablet_tool_wheel(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees,
                              int32_t clicks) {
    (void)data;
    (void)tool;
    (void)degrees;
    (void)clicks;
}

static void tablet_tool_button(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial,
                               uint32_t button, uint32_t state) {
    (void)tool;
    (void)serial;
    pt_wl_tablet_tool *tool_state = data;
    uint32_t bit = 0;
    switch (button) {
    case BTN_STYLUS:
        bit = PT_STYLUS_BUTTON_BARREL;
        break;
    case BTN_STYLUS2:
        bit = PT_STYLUS_BUTTON_SECONDARY;
        break;
    case BTN_STYLUS3:
        bit = PT_STYLUS_BUTTON_TERTIARY;
        break;
    default:
        return;
    }
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        tool_state->buttons |= bit;
    } else {
        tool_state->buttons &= ~bit;
    }
    tool_state->pending_motion = true;
}

static void tablet_tool_frame(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t time) {
    (void)tool;
    (void)time;
    pt_wl_tablet_tool *state = data;
    if (!state->window || !state->in_range) {
        state->pending_enter = false;
        state->pending_motion = false;
        return;
    }
    if (state->pending_enter) {
        stylus_emit(state, pt_stylus_phase_enter);
        state->pending_enter = false;
        state->pending_motion = false;
    }
    if (state->pending_down) {
        stylus_emit(state, pt_stylus_phase_down);
        state->pending_down = false;
        state->pending_motion = false;
    } else if (state->pending_motion) {
        stylus_emit(state, pt_stylus_phase_motion);
        state->pending_motion = false;
    }
    if (state->pending_up) {
        stylus_emit(state, pt_stylus_phase_up);
        state->pending_up = false;
    }
}

static void tablet_tool_removed(void *data, struct zwp_tablet_tool_v2 *tool) {
    pt_wl_tablet_tool *state = data;
    tablet_tool_flush_up(state);
    if (state->window) {
        stylus_emit(state, pt_stylus_phase_leave);
        state->window->stylus_in_range = false;
        state->window->stylus_down = false;
        state->window = NULL;
    }
    state->buttons = 0;
    zwp_tablet_tool_v2_destroy(tool);
    state->tool = NULL;
    pt_platform *platform = state->platform;
    for (uint32_t i = 0; i < platform->tablet_tool_count; ++i) {
        if (platform->tablet_tools[i] == state) {
            for (uint32_t j = i + 1; j < platform->tablet_tool_count; ++j) {
                platform->tablet_tools[j - 1] = platform->tablet_tools[j];
            }
            --platform->tablet_tool_count;
            break;
        }
    }
    free(state);
}

static const struct zwp_tablet_tool_v2_listener tablet_tool_listener = {
    .type = tablet_tool_type,
    .hardware_serial = tablet_tool_hardware_serial,
    .hardware_id_wacom = tablet_tool_hardware_id_wacom,
    .capability = tablet_tool_capability,
    .done = tablet_tool_done,
    .removed = tablet_tool_removed,
    .proximity_in = tablet_tool_proximity_in,
    .proximity_out = tablet_tool_proximity_out,
    .down = tablet_tool_down,
    .up = tablet_tool_up,
    .motion = tablet_tool_motion,
    .pressure = tablet_tool_pressure,
    .distance = tablet_tool_distance,
    .tilt = tablet_tool_tilt,
    .rotation = tablet_tool_rotation,
    .slider = tablet_tool_slider,
    .wheel = tablet_tool_wheel,
    .button = tablet_tool_button,
    .frame = tablet_tool_frame,
};

static void tablet_name(void *data, struct zwp_tablet_v2 *tablet, const char *name) {
    (void)data;
    (void)tablet;
    (void)name;
}

static void tablet_id(void *data, struct zwp_tablet_v2 *tablet, uint32_t vid, uint32_t pid) {
    (void)data;
    (void)tablet;
    (void)vid;
    (void)pid;
}

static void tablet_path(void *data, struct zwp_tablet_v2 *tablet, const char *path) {
    (void)data;
    (void)tablet;
    (void)path;
}

static void tablet_done(void *data, struct zwp_tablet_v2 *tablet) {
    (void)data;
    (void)tablet;
}

static void tablet_removed(void *data, struct zwp_tablet_v2 *tablet) {
    (void)data;
    zwp_tablet_v2_destroy(tablet);
}

static const struct zwp_tablet_v2_listener tablet_listener = {
    .name = tablet_name,
    .id = tablet_id,
    .path = tablet_path,
    .done = tablet_done,
    .removed = tablet_removed,
};

static void tablet_added(void *data, struct zwp_tablet_seat_v2 *seat,
                         struct zwp_tablet_v2 *tablet) {
    (void)data;
    (void)seat;
    zwp_tablet_v2_add_listener(tablet, &tablet_listener, NULL);
}

static void tablet_pad_added(void *data, struct zwp_tablet_seat_v2 *seat,
                             struct zwp_tablet_pad_v2 *pad) {
    (void)data;
    (void)seat;
    zwp_tablet_pad_v2_destroy(pad);
}

static void tablet_tool_added(void *data, struct zwp_tablet_seat_v2 *seat,
                              struct zwp_tablet_tool_v2 *tool) {
    (void)seat;
    pt_platform *platform = data;
    if (platform->tablet_tool_count >= 8) {
        zwp_tablet_tool_v2_destroy(tool);
        return;
    }
    pt_wl_tablet_tool *state = calloc(1, sizeof(*state));
    if (!state) {
        zwp_tablet_tool_v2_destroy(tool);
        return;
    }
    state->tool = tool;
    state->platform = platform;
    state->type = pt_stylus_tool_unknown;
    platform->tablet_tools[platform->tablet_tool_count++] = state;
    zwp_tablet_tool_v2_add_listener(tool, &tablet_tool_listener, state);
}

static const struct zwp_tablet_seat_v2_listener tablet_seat_listener = {
    .tablet_added = tablet_added,
    .tool_added = tablet_tool_added,
    .pad_added = tablet_pad_added,
};

static void ensure_tablet_devices(pt_platform *platform) {
    if (platform->tablet_seat || !platform->tablet_manager || !platform->seat) {
        return;
    }
    platform->tablet_seat =
        zwp_tablet_manager_v2_get_tablet_seat(platform->tablet_manager, platform->seat);
    if (platform->tablet_seat) {
        zwp_tablet_seat_v2_add_listener(platform->tablet_seat, &tablet_seat_listener, platform);
    }
}

static void destroy_tablet_devices(pt_platform *platform) {
    for (uint32_t i = 0; i < platform->tablet_tool_count; ++i) {
        pt_wl_tablet_tool *state = platform->tablet_tools[i];
        if (!state) {
            continue;
        }
        if (state->tool) {
            zwp_tablet_tool_v2_destroy(state->tool);
        }
        free(state);
        platform->tablet_tools[i] = NULL;
    }
    platform->tablet_tool_count = 0;
    if (platform->tablet_seat) {
        zwp_tablet_seat_v2_destroy(platform->tablet_seat);
        platform->tablet_seat = NULL;
    }
    if (platform->tablet_manager) {
        zwp_tablet_manager_v2_destroy(platform->tablet_manager);
        platform->tablet_manager = NULL;
    }
}

/* ---------- touch ---------- */

static void touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                       struct wl_surface *surface, int32_t id, wl_fixed_t sx, wl_fixed_t sy) {
    (void)touch;
    pt_platform *platform = data;
    pt_wl_surface_ref ref = {0};
    bool found = pt_wl_resolve_surface(platform, surface, &ref);
    pt_window *window = found ? ref.window : NULL;
    platform->serial = serial;
    platform->touch_focus = window;
    platform->touch_offset_x = found ? ref.offset_x : 0.0f;
    platform->touch_offset_y = found ? ref.offset_y : 0.0f;
    if (!window) {
        return;
    }
    pt_window_input_touch_down(window, id, (float)wl_fixed_to_double(sx) + ref.offset_x,
                               (float)wl_fixed_to_double(sy) + ref.offset_y, time, serial);
}

static void touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                     int32_t id) {
    (void)touch;
    (void)serial;
    pt_platform *platform = data;
    pt_window *window = platform->touch_focus;
    if (window) {
        pt_window_input_touch_up(window, id, time);
    }
    platform->touch_focus = NULL;
    platform->touch_offset_x = 0.0f;
    platform->touch_offset_y = 0.0f;
}

static void touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id,
                         wl_fixed_t sx, wl_fixed_t sy) {
    (void)touch;
    pt_platform *platform = data;
    if (platform->touch_focus) {
        pt_window_input_touch_motion(platform->touch_focus, id,
                                     (float)wl_fixed_to_double(sx) +
                                         platform->touch_offset_x,
                                     (float)wl_fixed_to_double(sy) +
                                         platform->touch_offset_y,
                                     time);
    }
}

static void touch_frame(void *data, struct wl_touch *touch) {
    (void)data;
    (void)touch;
}

static void touch_cancel(void *data, struct wl_touch *touch) {
    (void)touch;
    pt_platform *platform = data;
    if (platform->touch_focus) {
        pt_window_input_touch_cancel(platform->touch_focus);
    }
    platform->touch_focus = NULL;
    platform->touch_offset_x = 0.0f;
    platform->touch_offset_y = 0.0f;
}

static const struct wl_touch_listener touch_listener = {
    .down = touch_down,
    .up = touch_up,
    .motion = touch_motion,
    .frame = touch_frame,
    .cancel = touch_cancel,
};

/* ---------- text input ---------- */

static void text_input_enter(void *data, struct zwp_text_input_v3 *text_input,
                             struct wl_surface *surface) {
    (void)text_input;
    pt_platform *platform = data;
    pt_window *window = window_for_surface(platform, surface);
    platform->text_input_window = window;
    platform->text_input_enabled = false;
    if (window && pt_window_has_editable_focus(window)) {
        pt_platform_ime_set_enabled(window, true);
    }
}

static void text_input_leave(void *data, struct zwp_text_input_v3 *text_input,
                             struct wl_surface *surface) {
    (void)text_input;
    (void)surface;
    pt_platform *platform = data;
    pt_window *window = platform->text_input_window;
    platform->text_input_window = NULL;
    platform->text_input_enabled = false;
    free(platform->pending_preedit);
    platform->pending_preedit = NULL;
    free(platform->pending_commit);
    platform->pending_commit = NULL;
    platform->pending_preedit_begin = -1;
    platform->pending_preedit_end = -1;
    platform->pending_delete_before = 0;
    platform->pending_delete_after = 0;
    if (window) {
        pt_window_input_ime_preedit(window, "");
    }
}

static void text_input_preedit_string(void *data, struct zwp_text_input_v3 *text_input,
                                      const char *text, int32_t cursor_begin,
                                      int32_t cursor_end) {
    (void)text_input;
    pt_platform *platform = data;
    free(platform->pending_preedit);
    platform->pending_preedit = pt_strdup(text ? text : "");
    platform->pending_preedit_begin = text && *text ? cursor_begin : -1;
    platform->pending_preedit_end = text && *text ? cursor_end : -1;
}

static void text_input_commit_string(void *data, struct zwp_text_input_v3 *text_input,
                                     const char *text) {
    (void)text_input;
    pt_platform *platform = data;
    free(platform->pending_commit);
    platform->pending_commit = pt_strdup(text ? text : "");
}

static void text_input_delete_surrounding_text(void *data,
                                               struct zwp_text_input_v3 *text_input,
                                               uint32_t before_length,
                                               uint32_t after_length) {
    (void)text_input;
    pt_platform *platform = data;
    platform->pending_delete_before = before_length;
    platform->pending_delete_after = after_length;
}

static void text_input_done(void *data, struct zwp_text_input_v3 *text_input,
                            uint32_t serial) {
    (void)text_input;
    pt_platform *platform = data;
    platform->serial = serial;
    pt_window *window = platform->text_input_window;
    char *preedit = platform->pending_preedit;
    char *commit = platform->pending_commit;
    int32_t preedit_begin = platform->pending_preedit_begin;
    int32_t preedit_end = platform->pending_preedit_end;
    uint32_t delete_before = platform->pending_delete_before;
    uint32_t delete_after = platform->pending_delete_after;
    platform->pending_preedit = NULL;
    platform->pending_commit = NULL;
    platform->pending_preedit_begin = -1;
    platform->pending_preedit_end = -1;
    platform->pending_delete_before = 0;
    platform->pending_delete_after = 0;
    if (window) {
        pt_window_input_ime_compose(window, commit, preedit ? preedit : "", preedit_begin,
                                    preedit_end, delete_before, delete_after);
    }
    free(preedit);
    free(commit);
}

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = text_input_enter,
    .leave = text_input_leave,
    .preedit_string = text_input_preedit_string,
    .commit_string = text_input_commit_string,
    .delete_surrounding_text = text_input_delete_surrounding_text,
    .done = text_input_done,
};

#define ime_surrounding_max 4000u

static uint32_t ime_content_hint(const pt_widget *widget) {
    uint32_t hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE;
    if (!widget) {
        return hint;
    }
    if (widget->kind == pt_widget_text_area) {
        hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE;
    }
    if (widget->kind == pt_widget_entry &&
        (widget->password || widget->input_purpose == pt_input_purpose_password)) {
        hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |
                ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA |
                ZWP_TEXT_INPUT_V3_CONTENT_HINT_NO_EMOJI;
    }
    return hint;
}

static uint32_t ime_content_purpose(const pt_widget *widget) {
    if (!widget) {
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    }
    if (widget->kind == pt_widget_entry && widget->password) {
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
    }
    switch (widget->input_purpose) {
    case pt_input_purpose_email:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
    case pt_input_purpose_url:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL;
    case pt_input_purpose_phone:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE;
    case pt_input_purpose_number:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER;
    case pt_input_purpose_password:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
    case pt_input_purpose_terminal:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL;
    case pt_input_purpose_normal:
    default:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
    }
}

static void ime_request_surrounding_text(pt_platform *platform, pt_window *window) {
    if (!platform || !platform->text_input || !window) {
        return;
    }
    char *text = NULL;
    uint32_t cursor = 0;
    uint32_t anchor = 0;
    if (!pt_widget_ime_surrounding_text(window->focused, &text, &cursor, &anchor)) {
        free(text);
        return;
    }
    size_t len = strlen(text);
    if (len <= ime_surrounding_max) {
        zwp_text_input_v3_set_surrounding_text(platform->text_input, text, (int32_t)cursor,
                                               (int32_t)anchor);
        free(text);
        return;
    }
    size_t window_size = ime_surrounding_max - 8u;
    size_t start = cursor > window_size / 2 ? cursor - window_size / 2 : 0;
    if (start + window_size > len) {
        start = len > window_size ? len - window_size : 0;
    }
    while (start < len && ((unsigned char)text[start] & 0xC0u) == 0x80u) {
        ++start;
    }
    size_t end = start + window_size;
    if (end > len) {
        end = len;
    }
    while (end > start && ((unsigned char)text[end] & 0xC0u) == 0x80u) {
        --end;
    }
    uint32_t local_cursor = cursor < start ? 0 : (uint32_t)(cursor > end ? end - start : cursor - start);
    uint32_t local_anchor = anchor < start ? 0 : (uint32_t)(anchor > end ? end - start : anchor - start);
    char *slice = pt_strdup_len(text + start, end - start);
    zwp_text_input_v3_set_surrounding_text(platform->text_input, slice, (int32_t)local_cursor,
                                           (int32_t)local_anchor);
    free(slice);
    free(text);
}

static void ime_request_state(pt_platform *platform, pt_window *window) {
    if (!platform || !platform->text_input || !window) {
        return;
    }
    zwp_text_input_v3_set_content_type(platform->text_input, ime_content_hint(window->focused),
                                       ime_content_purpose(window->focused));
    ime_request_surrounding_text(platform, window);
}

void pt_platform_ime_set_enabled(pt_window *window, bool enabled) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform || platform->headless || !platform->text_input) {
        return;
    }
    if (enabled) {
        if (platform->text_input_enabled && platform->text_input_window == window) {
            ime_request_state(platform, window);
            zwp_text_input_v3_commit(platform->text_input);
            return;
        }
        if (platform->text_input_enabled) {
            zwp_text_input_v3_disable(platform->text_input);
            zwp_text_input_v3_commit(platform->text_input);
            platform->text_input_enabled = false;
        }
        platform->text_input_window = window;
        platform->text_input_enabled = true;
        zwp_text_input_v3_enable(platform->text_input);
        ime_request_state(platform, window);
        zwp_text_input_v3_commit(platform->text_input);
    } else if (platform->text_input_enabled) {
        if (window && platform->text_input_window != window) {
            return;
        }
        zwp_text_input_v3_disable(platform->text_input);
        zwp_text_input_v3_commit(platform->text_input);
        platform->text_input_enabled = false;
    }
}

void pt_platform_ime_set_cursor_rect(pt_window *window, int x, int y, int width, int height) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform || platform->headless || !platform->text_input ||
        !platform->text_input_enabled || platform->text_input_window != window) {
        return;
    }
    ime_request_surrounding_text(platform, window);
    zwp_text_input_v3_set_cursor_rectangle(platform->text_input, x, y, width, height);
    zwp_text_input_v3_commit(platform->text_input);
}

/* ---------- clipboard ---------- */

static void offer_free(pt_platform *platform, struct pt_wl_offer *offer) {
    if (!offer) {
        return;
    }
    if (platform->selection == offer) {
        platform->selection = NULL;
    }
    if (platform->drag_offer == offer) {
        platform->drag_offer = NULL;
    }
    struct pt_wl_offer **link = &platform->offers;
    while (*link) {
        if (*link == offer) {
            *link = offer->next;
            break;
        }
        link = &(*link)->next;
    }
    for (int i = 0; i < offer->mime_count; ++i) {
        free(offer->mimes[i]);
    }
    if (offer->offer) {
        wl_data_offer_destroy(offer->offer);
    }
    free(offer);
}

static const char *offer_text_mime(const struct pt_wl_offer *offer) {
    static const char *preferred[] = {
        "text/plain;charset=utf-8", "text/plain;charset=UTF-8", "text/plain;charset=utf8",
        "UTF8_STRING", "text/plain", "STRING",
    };
    if (!offer) {
        return NULL;
    }
    for (size_t p = 0; p < pt_len(preferred); ++p) {
        for (int i = 0; i < offer->mime_count; ++i) {
            if (strcmp(offer->mimes[i], preferred[p]) == 0) {
                return preferred[p];
            }
        }
    }
    for (int i = 0; i < offer->mime_count; ++i) {
        if (strncmp(offer->mimes[i], "text/plain", 10) == 0) {
            return offer->mimes[i];
        }
    }
    return NULL;
}

static void data_offer_offer(void *data, struct wl_data_offer *wl_offer,
                             const char *mime_type) {
    (void)wl_offer;
    struct pt_wl_offer *offer = data;
    if (!offer || !mime_type || offer->mime_count >= (int)pt_len(offer->mimes)) {
        return;
    }
    for (int i = 0; i < offer->mime_count; ++i) {
        if (strcmp(offer->mimes[i], mime_type) == 0) {
            return;
        }
    }
    offer->mimes[offer->mime_count++] = pt_strdup(mime_type);
}

static void data_offer_source_actions(void *data, struct wl_data_offer *wl_offer,
                                      uint32_t source_actions) {
    (void)wl_offer;
    struct pt_wl_offer *offer = data;
    if (offer) {
        offer->source_actions = source_actions;
    }
}

static void data_offer_action(void *data, struct wl_data_offer *wl_offer, uint32_t action) {
    (void)wl_offer;
    struct pt_wl_offer *offer = data;
    if (offer) {
        offer->action = action;
    }
}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_offer,
    .source_actions = data_offer_source_actions,
    .action = data_offer_action,
};

static void data_device_data_offer(void *data, struct wl_data_device *device,
                                   struct wl_data_offer *wl_offer) {
    (void)device;
    pt_platform *platform = data;
    struct pt_wl_offer *offer = pt_alloc_zeroed(1, sizeof(*offer));
    offer->offer = wl_offer;
    offer->platform = platform;
    offer->next = platform->offers;
    platform->offers = offer;
    wl_data_offer_add_listener(wl_offer, &data_offer_listener, offer);
}

static char *offer_read_text(pt_platform *platform, struct pt_wl_offer *offer, const char *mime,
                             uint32_t timeout_ms);
static pt_drop_offer *platform_drop_offer_create(pt_window *window, struct pt_wl_offer *wl);

static struct pt_wl_offer *offer_find(pt_platform *platform, struct wl_data_offer *wl_offer) {
    for (struct pt_wl_offer *offer = platform->offers; offer; offer = offer->next) {
        if (offer->offer == wl_offer) {
            return offer;
        }
    }
    return NULL;
}

static void drag_offer_accept(pt_platform *platform, uint32_t serial, bool accepted) {
    struct pt_wl_offer *offer = platform->drag_offer;
    if (!offer || !offer->offer) {
        return;
    }
    if (wl_data_device_manager_get_version(platform->data_device_manager) >= 3 &&
        (offer->source_actions & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) != 0) {
        wl_data_offer_set_actions(offer->offer, offer->source_actions,
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }
    wl_data_offer_accept(offer->offer, serial,
                         accepted && platform->drag_mime ? platform->drag_mime : NULL);
}

static void data_device_enter(void *data, struct wl_data_device *device, uint32_t serial,
                              struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
                              struct wl_data_offer *wl_offer) {
    (void)device;
    pt_platform *platform = data;
    pt_wl_surface_ref ref = {0};
    if (!pt_wl_resolve_surface(platform, surface, &ref)) {
        return;
    }
    struct pt_wl_offer *offer = offer_find(platform, wl_offer);
    if (!offer) {
        return;
    }
    free(platform->drag_mime);
    platform->drag_mime = NULL;
    platform->drag_offer = offer;
    platform->drag_window = ref.window;
    platform->drag_offset_x = ref.offset_x;
    platform->drag_offset_y = ref.offset_y;
    platform->drag_x = (float)wl_fixed_to_double(x) + ref.offset_x;
    platform->drag_y = (float)wl_fixed_to_double(y) + ref.offset_y;
    platform->drag_serial = serial;
    bool accepted = false;
    if (ref.window) {
        pt_drop_offer *drop = platform_drop_offer_create(ref.window, offer);
        if (drop) {
            accepted = pt_window_dnd_offer_enter(ref.window, drop, platform->drag_x,
                                                 platform->drag_y);
            if (accepted && drop->selected_mime) {
                platform->drag_mime = pt_strdup(drop->selected_mime);
            }
            drag_offer_accept(platform, serial, accepted);
            pt_drop_offer_release(drop);
            if (!accepted) {
                platform->drag_offer = NULL;
            }
            return;
        }
    }
    drag_offer_accept(platform, serial, false);
    platform->drag_offer = NULL;
}

static void data_device_leave(void *data, struct wl_data_device *device) {
    (void)device;
    pt_platform *platform = data;
    if (platform->drag_offer && !platform->drag_offer->in_use) {
        offer_free(platform, platform->drag_offer);
    }
    if (platform->drag_window) {
        pt_window_dnd_leave(platform->drag_window);
    }
    platform->drag_offer = NULL;
    platform->drag_window = NULL;
    free(platform->drag_mime);
    platform->drag_mime = NULL;
}

static void data_device_motion(void *data, struct wl_data_device *device, uint32_t time,
                               wl_fixed_t x, wl_fixed_t y) {
    (void)device;
    (void)time;
    pt_platform *platform = data;
    if (!platform->drag_window || !platform->drag_offer) {
        return;
    }
    platform->drag_x = (float)wl_fixed_to_double(x) + platform->drag_offset_x;
    platform->drag_y = (float)wl_fixed_to_double(y) + platform->drag_offset_y;
    bool accepted = pt_window_dnd_offer_motion(platform->drag_window, platform->drag_x,
                                               platform->drag_y);
    if (accepted && platform->drag_window->drop_offer) {
        const char *mime = platform->drag_window->drop_offer->selected_mime;
        if (mime && (!platform->drag_mime || strcmp(platform->drag_mime, mime) != 0)) {
            free(platform->drag_mime);
            platform->drag_mime = pt_strdup(mime);
        }
    }
    drag_offer_accept(platform, platform->drag_serial, accepted);
}

static void data_device_drop(void *data, struct wl_data_device *device) {
    (void)device;
    pt_platform *platform = data;
    pt_window *window = platform->drag_window;
    bool delivered = false;
    if (window) {
        delivered = pt_window_dnd_offer_drop(window, platform->drag_x, platform->drag_y);
        if (!delivered) {
            pt_window_dnd_cancel(window);
        }
        window->needs_draw = true;
    }
    platform->drag_offer = NULL;
    platform->drag_window = NULL;
    free(platform->drag_mime);
    platform->drag_mime = NULL;
}

static void data_device_selection(void *data, struct wl_data_device *device,
                                  struct wl_data_offer *wl_offer) {
    (void)device;
    pt_platform *platform = data;
    struct pt_wl_offer *selected = NULL;
    for (struct pt_wl_offer *offer = platform->offers; offer; offer = offer->next) {
        if (offer->offer == wl_offer) {
            selected = offer;
            break;
        }
    }
    struct pt_wl_offer *offer = platform->offers;
    while (offer) {
        struct pt_wl_offer *next = offer->next;
        if (offer != selected && offer != platform->drag_offer && !offer->in_use) {
            offer_free(platform, offer);
        }
        offer = next;
    }
    platform->selection = selected;
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_data_offer,
    .enter = data_device_enter,
    .leave = data_device_leave,
    .motion = data_device_motion,
    .drop = data_device_drop,
    .selection = data_device_selection,
};

static bool mime_is_text(const char *mime_type) {
    return mime_type && (strcmp(mime_type, "UTF8_STRING") == 0 ||
                         strcmp(mime_type, "STRING") == 0 ||
                         strncmp(mime_type, "text/plain", 10) == 0);
}

static const pt_wl_drag_type *drag_type_find(const pt_platform *platform,
                                             const char *mime_type) {
    for (uint32_t i = 0; i < platform->drag_type_count; ++i) {
        if (strcmp(platform->drag_types[i].mime_type, mime_type) == 0) {
            return &platform->drag_types[i];
        }
    }
    return NULL;
}

static const pt_wl_drag_type *drag_type_text(const pt_platform *platform) {
    for (uint32_t i = 0; i < platform->drag_type_count; ++i) {
        if (strncmp(platform->drag_types[i].mime_type, "text/plain", 10) == 0) {
            return &platform->drag_types[i];
        }
    }
    return NULL;
}

static bool source_write_pump_one(pt_wl_source_write *pending_write) {
    while (pending_write->offset < pending_write->size) {
        ssize_t written = write(pending_write->fd, pending_write->data + pending_write->offset,
                                pending_write->size - pending_write->offset);
        if (written > 0) {
            pending_write->offset += (size_t)written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;
        }
        pending_write->offset = pending_write->size;
        break;
    }
    return true;
}

static bool source_writes_pump(pt_platform *platform) {
    bool pending = false;
    pt_wl_source_write **link = &platform->source_writes;
    while (*link) {
        pt_wl_source_write *pending_write = *link;
        if (source_write_pump_one(pending_write)) {
            *link = pending_write->next;
            close(pending_write->fd);
            free(pending_write->data);
            free(pending_write);
        } else {
            pending = true;
            link = &pending_write->next;
        }
    }
    return pending;
}

static void data_source_send(void *data, struct wl_data_source *source, const char *mime_type,
                             int32_t fd) {
    pt_platform *platform = data;
    const char *payload = NULL;
    size_t size = 0;
    if (platform->drag_source == source) {
        const pt_wl_drag_type *type = drag_type_find(platform, mime_type);
        if (!type && mime_is_text(mime_type)) {
            type = drag_type_text(platform);
        }
        if (type) {
            payload = type->data;
            size = type->size;
        }
    } else if (mime_is_text(mime_type) && platform->clipboard_text) {
        payload = platform->clipboard_text;
        size = strlen(platform->clipboard_text);
    }
    if (!payload) {
        close(fd);
        return;
    }
    pt_wl_source_write *write = pt_alloc_zeroed(1, sizeof(*write));
    char *copy = malloc(size ? size : 1u);
    if (!write || !copy) {
        free(write);
        free(copy);
        close(fd);
        return;
    }
    if (size > 0) {
        memcpy(copy, payload, size);
    }
    write->fd = fd;
    write->data = copy;
    write->size = size;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    if (!source_write_pump_one(write)) {
        write->next = platform->source_writes;
        platform->source_writes = write;
        return;
    }
    close(fd);
    free(copy);
    free(write);
}

static void platform_offers_text(struct wl_data_source *source) {
    wl_data_source_offer(source, "text/plain;charset=utf-8");
    wl_data_source_offer(source, "text/plain;charset=UTF-8");
    wl_data_source_offer(source, "text/plain");
    wl_data_source_offer(source, "UTF8_STRING");
    wl_data_source_offer(source, "STRING");
}

#define pt_drag_default_width 112
#define pt_drag_default_height 30
#define pt_drag_max_dimension 4096

static void drag_icon_buffer_release(void *data, struct wl_buffer *buffer) {
    (void)data;
    (void)buffer;
}

static const struct wl_buffer_listener drag_icon_buffer_listener = {
    .release = drag_icon_buffer_release,
};

static void drag_icon_hide(pt_platform *platform) {
    if (platform->drag_icon_surface) {
        wl_surface_attach(platform->drag_icon_surface, NULL, 0, 0);
        wl_surface_commit(platform->drag_icon_surface);
    }
}

static void drag_icon_destroy(pt_platform *platform) {
    if (platform->drag_icon_surface) {
        wl_surface_destroy(platform->drag_icon_surface);
        platform->drag_icon_surface = NULL;
    }
    if (platform->drag_icon_buffer) {
        wl_buffer_destroy(platform->drag_icon_buffer);
        platform->drag_icon_buffer = NULL;
    }
    if (platform->drag_icon_data) {
        munmap(platform->drag_icon_data, platform->drag_icon_size);
        platform->drag_icon_data = NULL;
    }
    platform->drag_icon_size = 0;
    platform->drag_icon_width = 0;
    platform->drag_icon_height = 0;
}

static uint32_t drag_icon_pixel(pt_color color) {
    uint32_t a = color & 0xFFu;
    if (a == 0) {
        return 0;
    }
    uint32_t r = (color >> 24) & 0xFFu;
    uint32_t g = (color >> 16) & 0xFFu;
    uint32_t b = (color >> 8) & 0xFFu;
    r = (r * a + 127u) / 255u;
    g = (g * a + 127u) / 255u;
    b = (b * a + 127u) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static bool drag_icon_ensure(pt_platform *platform, pt_window *window) {
    if (platform->drag_icon_surface) {
        return true;
    }
    if (!platform->shm || !platform->compositor) {
        return false;
    }
    const pt_color *custom = NULL;
    int width = pt_drag_default_width;
    int height = pt_drag_default_height;
    if (window && window->drag_image && window->drag_image_width > 0 &&
        window->drag_image_height > 0) {
        custom = window->drag_image;
        width = window->drag_image_width;
        height = window->drag_image_height;
    }
    if (width <= 0 || height <= 0 || width > pt_drag_max_dimension ||
        height > pt_drag_max_dimension) {
        return false;
    }
    pt_canvas *canvas = NULL;
    if (!custom) {
        canvas = pt_canvas_create(width, height, 1.0f);
        if (!canvas) {
            return false;
        }
        pt_canvas_clear(canvas, 0x00000000u);
        pt_color accent =
            platform->app ? platform->app->theme.colors[pt_theme_accent] : 0xFF4F6BEDu;
        pt_color ink =
            platform->app ? platform->app->theme.colors[pt_theme_accent_text] : 0xFFFFFFFFu;
        pt_canvas_fill_rounded_rect(canvas, 1.0f, 1.0f, (float)width - 2.0f,
                                    (float)height - 2.0f, 9.0f, accent);
        pt_canvas_fill_rounded_rect(canvas, 14.0f, 9.0f, 42.0f, 3.0f, 1.5f, ink);
        pt_canvas_fill_rounded_rect(canvas, 14.0f, 15.0f, 56.0f, 3.0f, 1.5f,
                                    pt_color_alpha(ink, 200));
        pt_canvas_fill_rounded_rect(canvas, 14.0f, 21.0f, 34.0f, 3.0f, 1.5f,
                                    pt_color_alpha(ink, 150));
    }
    size_t stride = (size_t)width * 4u;
    if ((size_t)height > SIZE_MAX / stride) {
        pt_canvas_destroy(canvas);
        return false;
    }
    size_t size = stride * (size_t)height;
    int fd = pt_wl_create_shm_file(size);
    if (fd < 0) {
        pt_canvas_destroy(canvas);
        return false;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        pt_canvas_destroy(canvas);
        return false;
    }
    if (custom) {
        uint32_t *dst = data;
        for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
            dst[i] = drag_icon_pixel(custom[i]);
        }
    } else {
        memcpy(data, canvas->pixels, size);
        pt_canvas_destroy(canvas);
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(platform->shm, fd, (int32_t)size);
    close(fd);
    if (!pool) {
        munmap(data, size);
        return false;
    }
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                                         (int32_t)stride,
                                                         WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!buffer) {
        munmap(data, size);
        return false;
    }
    wl_buffer_add_listener(buffer, &drag_icon_buffer_listener, NULL);
    struct wl_surface *surface = wl_compositor_create_surface(platform->compositor);
    if (!surface) {
        wl_buffer_destroy(buffer);
        munmap(data, size);
        return false;
    }
    platform->drag_icon_surface = surface;
    platform->drag_icon_buffer = buffer;
    platform->drag_icon_data = data;
    platform->drag_icon_size = size;
    platform->drag_icon_width = width;
    platform->drag_icon_height = height;
    return true;
}

static void drag_icon_show(pt_platform *platform, pt_window *window) {
    drag_icon_destroy(platform);
    if (!drag_icon_ensure(platform, window)) {
        return;
    }
    wl_surface_attach(platform->drag_icon_surface, platform->drag_icon_buffer, 0, 0);
    wl_surface_damage(platform->drag_icon_surface, 0, 0, platform->drag_icon_width,
                      platform->drag_icon_height);
    wl_surface_commit(platform->drag_icon_surface);
}

static pt_drag_action drag_action_from_wl(uint32_t action) {
    switch (action) {
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY:
        return pt_drag_action_copy;
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE:
        return pt_drag_action_move;
    case WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK:
        return pt_drag_action_ask;
    default:
        return pt_drag_action_none;
    }
}

static void drag_source_finish(pt_platform *platform, struct wl_data_source *source) {
    platform->drag_source = NULL;
    for (uint32_t i = 0; i < platform->drag_type_count; ++i) {
        free(platform->drag_types[i].mime_type);
        free(platform->drag_types[i].data);
    }
    free(platform->drag_types);
    platform->drag_types = NULL;
    platform->drag_type_count = 0;
    pt_window *window = platform->drag_source_window;
    platform->drag_source_window = NULL;
    bool dropped = platform->drag_drop_performed;
    pt_drag_action action =
        dropped ? drag_action_from_wl(platform->drag_finished_action) : pt_drag_action_none;
    platform->drag_drop_performed = false;
    platform->drag_finished_action = 0;
    platform->drag_action = 0;
    wl_data_source_destroy(source);
    drag_icon_hide(platform);
    if (window) {
        pt_window_drag_finish_notify(window, dropped, action);
    }
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
    pt_platform *platform = data;
    if (platform->data_source == source) {
        platform->data_source = NULL;
        wl_data_source_destroy(source);
    } else if (platform->drag_source == source) {
        drag_source_finish(platform, source);
    }
}

static void data_source_target(void *data, struct wl_data_source *source,
                               const char *mime_type) {
    (void)data;
    (void)source;
    (void)mime_type;
}

static void data_source_action(void *data, struct wl_data_source *source, uint32_t action) {
    pt_platform *platform = data;
    if (platform->drag_source == source) {
        platform->drag_action = action;
    }
}

static void data_source_drop_performed(void *data, struct wl_data_source *source) {
    pt_platform *platform = data;
    if (platform->drag_source == source) {
        platform->drag_drop_performed = true;
    }
}

static void data_source_finished(void *data, struct wl_data_source *source) {
    pt_platform *platform = data;
    if (platform->drag_source == source) {
        platform->drag_drop_performed = true;
        platform->drag_finished_action = platform->drag_action;
        drag_source_finish(platform, source);
    }
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_drop_performed,
    .dnd_finished = data_source_finished,
    .action = data_source_action,
};

void pt_platform_clipboard_set(pt_window *window, const char *text) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform) {
        return;
    }
    free(platform->clipboard_text);
    platform->clipboard_text = text ? pt_strdup(text) : NULL;
    if (platform->headless) {
        return;
    }
    if (!platform->data_device || !platform->data_device_manager) {
        return;
    }
    struct wl_data_source *old_source = platform->data_source;
    platform->data_source = NULL;
    if (!platform->clipboard_text) {
        wl_data_device_set_selection(platform->data_device, NULL, platform->serial);
    } else {
        struct wl_data_source *source =
            wl_data_device_manager_create_data_source(platform->data_device_manager);
        if (source) {
            platform->data_source = source;
            wl_data_source_add_listener(source, &data_source_listener, platform);
            platform_offers_text(source);
            wl_data_device_set_selection(platform->data_device, source, platform->serial);
        }
    }
    if (old_source) {
        wl_data_source_destroy(old_source);
    }
}

static char *offer_read_text(pt_platform *platform, struct pt_wl_offer *offer, const char *mime,
                             uint32_t timeout_ms) {
    if (!platform || !offer || !offer->offer || !mime) {
        return NULL;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) {
        return NULL;
    }
    wl_data_offer_receive(offer->offer, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(platform->display);
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = pt_alloc(capacity);
    int64_t deadline = pt_now_ms() + timeout_ms;
    for (;;) {
        int64_t now = pt_now_ms();
        int wait = (int)(deadline - now);
        if (wait < 0) {
            break;
        }
        struct pollfd pfd = {.fd = fds[0], .events = POLLIN};
        int ready = poll(&pfd, 1, wait);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            break;
        }
        if (length + 1 >= capacity) {
            if (capacity >= pt_clipboard_max_bytes) {
                break;
            }
            capacity *= 2;
            buffer = pt_realloc_mem(buffer, capacity);
        }
        ssize_t got = read(fds[0], buffer + length, capacity - length - 1);
        if (got > 0) {
            length += (size_t)got;
            if (length >= pt_clipboard_max_bytes) {
                break;
            }
            continue;
        }
        if (got == 0) {
            break;
        }
        if (errno == EINTR || errno == EAGAIN) {
            continue;
        }
        break;
    }
    close(fds[0]);
    if (length == 0) {
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}

char *pt_platform_clipboard_get(pt_window *window) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform) {
        return NULL;
    }
    if (platform->headless) {
        return platform->clipboard_text ? pt_strdup(platform->clipboard_text) : NULL;
    }
    if (platform->data_source && platform->clipboard_text) {
        return pt_strdup(platform->clipboard_text);
    }
    const char *mime = offer_text_mime(platform->selection);
    if (!mime) {
        return NULL;
    }
    return offer_read_text(platform, platform->selection, mime, pt_clipboard_timeout_ms);
}

static void ensure_seat_devices(pt_platform *platform);

static bool platform_offer_start_read(pt_drop_offer *drop, const char *mime_type, int fd) {
    struct pt_wl_offer *offer = drop->backend;
    if (!offer || !offer->offer) {
        return false;
    }
    wl_data_offer_receive(offer->offer, mime_type, fd);
    if (offer->platform && offer->platform->display) {
        wl_display_flush(offer->platform->display);
    }
    return true;
}

static char *platform_offer_read_sync(pt_drop_offer *drop, const char *mime_type,
                                      uint32_t timeout_ms) {
    struct pt_wl_offer *offer = drop->backend;
    if (!offer || !offer->platform) {
        return NULL;
    }
    return offer_read_text(offer->platform, offer, mime_type, timeout_ms);
}

static void platform_offer_finish_wl(pt_drop_offer *drop, bool success) {
    struct pt_wl_offer *offer = drop->backend;
    if (!offer || !offer->offer || !offer->platform) {
        return;
    }
    if (success &&
        wl_data_device_manager_get_version(offer->platform->data_device_manager) >= 3) {
        wl_data_offer_finish(offer->offer);
    }
}

static void platform_offer_free_wl(pt_drop_offer *drop) {
    struct pt_wl_offer *offer = drop->backend;
    if (!offer) {
        return;
    }
    drop->backend = NULL;
    offer->in_use = false;
    offer_free(offer->platform, offer);
}

static pt_drop_offer *platform_drop_offer_create(pt_window *window, struct pt_wl_offer *wl) {
    if (!window || !wl) {
        return NULL;
    }
    pt_drop_offer *drop =
        pt_drop_offer_new(window->app, window, (const char *const *)wl->mimes,
                          (uint32_t)wl->mime_count);
    if (!drop) {
        return NULL;
    }
    wl->in_use = true;
    drop->backend = wl;
    drop->actions = wl->source_actions;
    drop->start_read = platform_offer_start_read;
    drop->read_sync = platform_offer_read_sync;
    drop->finish = platform_offer_finish_wl;
    drop->free_backend = platform_offer_free_wl;
    return drop;
}

static void drag_types_free(pt_wl_drag_type *types, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        free(types[i].mime_type);
        free(types[i].data);
    }
    free(types);
}

bool pt_platform_begin_drag_types(pt_window *window, const pt_drag_data *types,
                                  uint32_t count) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform || platform->headless || !types || count == 0) {
        return false;
    }
    if (!platform->data_device_manager || !platform->compositor) {
        return false;
    }
    if (platform->drag_source) {
        return false;
    }
    ensure_seat_devices(platform);
    pt_wayland_window *wl = pt_wl_window_for(window);
    if (!platform->data_device || !wl || !wl->surface) {
        return false;
    }
    uint32_t serial = window->pointer_serial ? window->pointer_serial : platform->pointer_serial;
    if (!serial) {
        return false;
    }
    uint32_t valid = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (types[i].mime_type && *types[i].mime_type && types[i].data) {
            ++valid;
        }
    }
    if (valid == 0) {
        return false;
    }
    pt_wl_drag_type *copy = pt_alloc_zeroed(valid, sizeof(*copy));
    if (!copy) {
        return false;
    }
    uint32_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const pt_drag_data *type = &types[i];
        if (!type->mime_type || !*type->mime_type || !type->data) {
            continue;
        }
        size_t size = type->size ? type->size : strlen(type->data);
        copy[at].mime_type = pt_strdup(type->mime_type);
        copy[at].data = malloc(size ? size : 1u);
        if (!copy[at].mime_type || !copy[at].data) {
            drag_types_free(copy, valid);
            return false;
        }
        if (size > 0) {
            memcpy(copy[at].data, type->data, size);
        }
        copy[at].size = size;
        ++at;
    }
    struct wl_data_source *source =
        wl_data_device_manager_create_data_source(platform->data_device_manager);
    if (!source) {
        drag_types_free(copy, valid);
        return false;
    }
    platform->drag_source = source;
    platform->drag_source_window = window;
    platform->drag_types = copy;
    platform->drag_type_count = valid;
    platform->drag_action = 0;
    platform->drag_finished_action = 0;
    platform->drag_drop_performed = false;
    wl_data_source_add_listener(source, &data_source_listener, platform);
    bool text = false;
    for (uint32_t i = 0; i < valid; ++i) {
        wl_data_source_offer(source, copy[i].mime_type);
        if (strncmp(copy[i].mime_type, "text/plain", 10) == 0) {
            text = true;
        }
    }
    if (text) {
        static const char *aliases[] = {"text/plain;charset=utf-8", "text/plain;charset=UTF-8",
                                        "text/plain", "UTF8_STRING", "STRING"};
        for (size_t i = 0; i < pt_len(aliases); ++i) {
            if (!drag_type_find(platform, aliases[i])) {
                wl_data_source_offer(source, aliases[i]);
            }
        }
    }
    if (wl_data_device_manager_get_version(platform->data_device_manager) >= 3) {
        wl_data_source_set_actions(source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }
    drag_icon_show(platform, window);
    wl_data_device_start_drag(platform->data_device, source, wl->surface,
                              platform->drag_icon_surface, serial);
    wl_display_flush(platform->display);
    return true;
}

void pt_platform_cancel_drag(pt_window *window) {
    pt_platform *platform = pt_platform_for(window);
    if (!platform || !platform->drag_source) {
        return;
    }
    drag_source_finish(platform, platform->drag_source);
    wl_display_flush(platform->display);
}

/* ---------- seat ---------- */

static void ensure_seat_devices(pt_platform *platform) {
    if (!platform->seat) {
        return;
    }
    if (platform->data_device_manager && !platform->data_device) {
        platform->data_device =
            wl_data_device_manager_get_data_device(platform->data_device_manager,
                                                   platform->seat);
        if (platform->data_device) {
            wl_data_device_add_listener(platform->data_device, &data_device_listener, platform);
        }
    }
    if (platform->text_input_manager && !platform->text_input) {
        platform->text_input =
            zwp_text_input_manager_v3_get_text_input(platform->text_input_manager,
                                                     platform->seat);
        if (platform->text_input) {
            zwp_text_input_v3_add_listener(platform->text_input, &text_input_listener,
                                           platform);
        }
    }
    ensure_gesture_devices(platform);
    ensure_tablet_devices(platform);
}

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    pt_platform *platform = data;
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !platform->keyboard) {
        platform->keyboard = wl_seat_get_keyboard(seat);
        if (platform->keyboard) {
            wl_keyboard_add_listener(platform->keyboard, &keyboard_listener, platform);
        }
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && platform->keyboard) {
        cancel_repeat(platform);
        if (platform->keyboard_focus) {
            pt_window_input_keyboard_focus(platform->keyboard_focus, false, 0);
            platform->keyboard_focus = NULL;
        }
        wl_keyboard_release(platform->keyboard);
        platform->keyboard = NULL;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !platform->pointer) {
        platform->pointer = wl_seat_get_pointer(seat);
        if (platform->pointer) {
            wl_pointer_add_listener(platform->pointer, &pointer_listener, platform);
        }
        ensure_gesture_devices(platform);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && platform->pointer) {
        if (platform->pointer_focus) {
            pt_window_input_pointer_leave(platform->pointer_focus);
            platform->pointer_focus = NULL;
        }
        destroy_gesture_devices(platform);
        wl_pointer_release(platform->pointer);
        platform->pointer = NULL;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !platform->touch) {
        platform->touch = wl_seat_get_touch(seat);
        if (platform->touch) {
            wl_touch_add_listener(platform->touch, &touch_listener, platform);
        }
    } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && platform->touch) {
        if (platform->touch_focus) {
            pt_window_input_touch_cancel(platform->touch_focus);
            platform->touch_focus = NULL;
        }
        wl_touch_release(platform->touch);
        platform->touch = NULL;
    }
    ensure_seat_devices(platform);
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* ---------- registry ---------- */

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    pt_platform *platform = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t bound = version < 6 ? version : 6;
        platform->compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface, bound);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        platform->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t bound = version < 7 ? version : 7;
        platform->seat = wl_registry_bind(registry, name, &wl_seat_interface, bound);
        if (platform->seat) {
            wl_seat_add_listener(platform->seat, &seat_listener, platform);
        }
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        uint32_t bound = version < 4 ? version : 4;
        platform->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, bound);
        if (platform->wm_base) {
            xdg_wm_base_add_listener(platform->wm_base, &wm_base_listener, platform);
        }
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        platform->decoration_manager = wl_registry_bind(
            registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        platform->fractional_scale_manager = wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        platform->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        platform->text_input_manager = wl_registry_bind(
            registry, name, &zwp_text_input_manager_v3_interface, 1);
        ensure_seat_devices(platform);
    } else if (strcmp(interface, zwp_pointer_gestures_v1_interface.name) == 0) {
        uint32_t bound = version < 3 ? version : 3;
        platform->gestures =
            wl_registry_bind(registry, name, &zwp_pointer_gestures_v1_interface, bound);
        ensure_gesture_devices(platform);
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        uint32_t bound = version < 3 ? version : 3;
        platform->data_device_manager =
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, bound);
        ensure_seat_devices(platform);
    } else if (strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0) {
        platform->tablet_manager =
            wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1);
        ensure_tablet_devices(platform);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ---------- lifecycle ---------- */

static bool env_flag(const char *name) {
    const char *value = getenv(name);
    if (!value || !*value) {
        return false;
    }
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 &&
           strcmp(value, "no") != 0;
}

bool pt_platform_start(pt_app *app) {
    pt_platform *platform = pt_alloc_zeroed(1, sizeof(*platform));
    platform->app = app;
    platform->headless = env_flag("PT_HEADLESS");
    platform->started = true;
    platform->repeat_rate = 25;
    platform->repeat_delay = 500;
    platform->pending_preedit_begin = -1;
    platform->pending_preedit_end = -1;
    platform->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!platform->headless) {
        platform->display = wl_display_connect(NULL);
        if (!platform->display) {
            pt_set_error("failed to connect to a Wayland compositor (use PT_HEADLESS=1 to run "
                         "without a display)");
            xkb_context_unref(platform->xkb_context);
            free(platform);
            return false;
        }
        platform->registry = wl_display_get_registry(platform->display);
        if (!platform->registry) {
            pt_set_error("failed to obtain the Wayland registry");
            wl_display_disconnect(platform->display);
            xkb_context_unref(platform->xkb_context);
            free(platform);
            return false;
        }
        wl_registry_add_listener(platform->registry, &registry_listener, platform);
        if (wl_display_roundtrip(platform->display) < 0) {
            pt_set_error("failed while querying Wayland globals");
            wl_display_disconnect(platform->display);
            xkb_context_unref(platform->xkb_context);
            free(platform);
            return false;
        }
        ensure_seat_devices(platform);
    }
    app->platform = platform;
    return true;
}

void pt_platform_stop(pt_app *app) {
    if (!app || !app->platform) {
        return;
    }
    pt_platform *platform = (pt_platform *)app->platform;
    app->platform = NULL;
    if (platform->repeat_active && app) {
        pt_app_cancel_timeout(app, platform->repeat_timer);
    }
    free(platform->clipboard_text);
    free(platform->pending_preedit);
    free(platform->pending_commit);
    if (platform->drag_types) {
        drag_types_free(platform->drag_types, platform->drag_type_count);
        platform->drag_types = NULL;
        platform->drag_type_count = 0;
    }
    while (platform->source_writes) {
        pt_wl_source_write *write = platform->source_writes;
        platform->source_writes = write->next;
        close(write->fd);
        free(write->data);
        free(write);
    }
    free(platform->drag_mime);
    if (platform->headless) {
        if (platform->xkb_context) {
            xkb_context_unref(platform->xkb_context);
        }
        free(platform);
        return;
    }
    while (platform->offers) {
        offer_free(platform, platform->offers);
    }
    if (platform->data_source) {
        wl_data_source_destroy(platform->data_source);
    }
    if (platform->drag_source) {
        wl_data_source_destroy(platform->drag_source);
    }
    if (platform->data_device) {
        wl_data_device_destroy(platform->data_device);
    }
    drag_icon_destroy(platform);
    if (platform->text_input) {
        zwp_text_input_v3_destroy(platform->text_input);
    }
    if (platform->keyboard) {
        wl_keyboard_release(platform->keyboard);
    }
    if (platform->touch) {
        wl_touch_release(platform->touch);
    }
    destroy_tablet_devices(platform);
    destroy_gesture_devices(platform);
    if (platform->pointer) {
        wl_pointer_release(platform->pointer);
    }
    if (platform->cursor_theme) {
        wl_cursor_theme_destroy(platform->cursor_theme);
    }
    if (platform->cursor_surface) {
        wl_surface_destroy(platform->cursor_surface);
    }
    if (platform->text_input_manager) {
        zwp_text_input_manager_v3_destroy(platform->text_input_manager);
    }
    if (platform->data_device_manager) {
        wl_data_device_manager_destroy(platform->data_device_manager);
    }
    if (platform->fractional_scale_manager) {
        wp_fractional_scale_manager_v1_destroy(platform->fractional_scale_manager);
    }
    if (platform->viewporter) {
        wp_viewporter_destroy(platform->viewporter);
    }
    if (platform->decoration_manager) {
        zxdg_decoration_manager_v1_destroy(platform->decoration_manager);
    }
    if (platform->wm_base) {
        xdg_wm_base_destroy(platform->wm_base);
    }
    if (platform->seat) {
        wl_seat_release(platform->seat);
    }
    if (platform->shm) {
        wl_shm_destroy(platform->shm);
    }
    if (platform->compositor) {
        wl_compositor_destroy(platform->compositor);
    }
    if (platform->registry) {
        wl_registry_destroy(platform->registry);
    }
    if (platform->xkb_state) {
        xkb_state_unref(platform->xkb_state);
    }
    if (platform->xkb_keymap) {
        xkb_keymap_unref(platform->xkb_keymap);
    }
    if (platform->xkb_context) {
        xkb_context_unref(platform->xkb_context);
    }
    wl_display_flush(platform->display);
    wl_display_disconnect(platform->display);
    free(platform);
}

int pt_platform_dispatch(pt_app *app, int timeout_ms) {
    if (!app || !app->platform) {
        return -1;
    }
    pt_platform *platform = (pt_platform *)app->platform;
    int a11y_fd = pt_a11y_bridge_fd(app);
    if (platform->headless) {
        struct pollfd pfd = {.fd = a11y_fd, .events = POLLIN};
        int result = poll(&pfd, a11y_fd >= 0 ? 1 : 0, timeout_ms);
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        if (a11y_fd >= 0) {
            pt_a11y_bridge_dispatch(app);
        }
        return 0;
    }
    struct wl_display *display = platform->display;
    if (wl_display_dispatch_pending(display) < 0) {
        return -1;
    }
    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
        return -1;
    }
    if (a11y_fd >= 0) {
        pt_a11y_bridge_dispatch(app);
    }
    if (timeout_ms == 0) {
        return 0;
    }
    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
            return -1;
        }
    }
    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display);
        return -1;
    }
    struct pollfd fds[2] = {
        {.fd = wl_display_get_fd(display), .events = POLLIN},
        {.fd = a11y_fd, .events = POLLIN},
    };
    nfds_t count = a11y_fd >= 0 ? 2 : 1;
    int ready = poll(fds, count, timeout_ms);
    if (ready < 0) {
        wl_display_cancel_read(display);
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (ready == 0) {
        wl_display_cancel_read(display);
        return 0;
    }
    if (fds[0].revents & POLLIN) {
        if (wl_display_read_events(display) < 0) {
            return -1;
        }
    } else {
        wl_display_cancel_read(display);
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return -1;
        }
    }
    if (count > 1 && (fds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
        pt_a11y_bridge_dispatch(app);
    }
    if (wl_display_dispatch_pending(display) < 0) {
        return -1;
    }
    return 0;
}

int pt_platform_dispatch_once(pt_app *app) {
    return pt_platform_dispatch(app, 0);
}

void pt_platform_flush(pt_app *app) {
    if (app && app->platform) {
        pt_platform *platform = (pt_platform *)app->platform;
        if (!platform->headless && platform->display) {
            wl_display_flush(platform->display);
        }
    }
}

bool pt_platform_pump_io(pt_app *app) {
    if (!app || !app->platform) {
        return false;
    }
    pt_platform *platform = (pt_platform *)app->platform;
    if (platform->headless) {
        return false;
    }
    return source_writes_pump(platform);
}
