/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_interaction_h
#define pt_interaction_h

#include <pt/window.h>

/* Header contract
 *
 * Threading. UI thread only, including getters.
 * Ownership. The event pointer passed to a gesture or stylus callback is
 * borrowed and valid only for that callback. target is borrowed from the
 * window and may already have been destroyed if the callback destroys it.
 * Errors. Setters are no-ops for NULL windows. Getters return the stored value
 * and false for NULL.
 * Lifetimes. user_data must stay valid until the callback is replaced or the
 * window is destroyed. Coordinates are logical window pixels; pressure and
 * distance are normalized to [0, 1], tilt is in degrees per axis and rotation is
 * in radians. The callback must not block.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Gestures normalize compositor multi-finger input and Pt-synthesized
 * long presses into one stream. Coordinates are logical window pixels and
 * target is the widget under the gesture centroid at gesture begin, when one
 * exists. Swipe reports accumulated translation in dx/dy; pinch reports the
 * scale relative to gesture begin and rotation in radians. A hold gesture
 * has one finger when Pt detects a long press and three fingers when the
 * compositor reports it. */
typedef enum pt_gesture_kind {
    pt_gesture_kind_swipe = 0,
    pt_gesture_kind_pinch,
    pt_gesture_kind_hold
} pt_gesture_kind;

typedef enum pt_gesture_phase {
    pt_gesture_phase_begin = 0,
    pt_gesture_phase_update,
    pt_gesture_phase_end,
    pt_gesture_phase_cancel
} pt_gesture_phase;

typedef struct pt_gesture_event {
    pt_gesture_kind kind;
    pt_gesture_phase phase;
    uint32_t fingers;
    float x;
    float y;
    float dx;
    float dy;
    float scale;
    float rotation;
    pt_widget *target;
} pt_gesture_event;

typedef void (*pt_gesture_callback)(pt_window *window, const pt_gesture_event *event,
                                    void *user_data);

pt_api void pt_window_set_gesture_callback(pt_window *window, pt_gesture_callback callback,
                                           void *user_data);

/* Pt synthesizes a single-finger hold after the delay while the pointer stays
 * down. The default is 500 ms; 0 disables it. Compositor three-finger holds
 * are delivered regardless of this setting. */
pt_api void pt_window_set_long_press_delay(pt_window *window, uint32_t milliseconds);

/* Kinetic scrolling continues a finger-driven wheel or touchpad fling after
 * the last axis event. It is enabled by default and only applies to pointer
 * axis sources that report continuous motion. */
pt_api void pt_window_set_momentum_scrolling(pt_window *window, bool enabled);
pt_api bool pt_window_get_momentum_scrolling(const pt_window *window);

/* Stylus input from tablet-v2 tools. Events are delivered in logical window
 * pixels alongside the normal pointer stream, so existing widgets keep
 * working. pressure and distance are normalized to [0, 1]; tilt is reported
 * in degrees on each axis and rotation in radians. A pen in proximity without
 * contact reports enter and motion; contact adds down/up. */
typedef enum pt_stylus_tool {
    pt_stylus_tool_pen = 0,
    pt_stylus_tool_eraser,
    pt_stylus_tool_brush,
    pt_stylus_tool_pencil,
    pt_stylus_tool_airbrush,
    pt_stylus_tool_unknown
} pt_stylus_tool;

typedef enum pt_stylus_phase {
    pt_stylus_phase_enter = 0,
    pt_stylus_phase_motion,
    pt_stylus_phase_down,
    pt_stylus_phase_up,
    pt_stylus_phase_leave
} pt_stylus_phase;

/* Barrel and auxiliary buttons held on the stylus, reported as a bitmask.
 * The tip is represented by the down/up phases instead. */
#define PT_STYLUS_BUTTON_BARREL 0x1u
#define PT_STYLUS_BUTTON_SECONDARY 0x2u
#define PT_STYLUS_BUTTON_TERTIARY 0x4u

typedef struct pt_stylus_event {
    pt_stylus_phase phase;
    pt_stylus_tool tool;
    float x;
    float y;
    float pressure;
    float distance;
    float tilt_x;
    float tilt_y;
    float rotation;
    uint32_t buttons; /* PT_STYLUS_BUTTON_* bitmask */
} pt_stylus_event;

typedef void (*pt_stylus_callback)(pt_window *window, const pt_stylus_event *event,
                                   void *user_data);

pt_api void pt_window_set_stylus_callback(pt_window *window, pt_stylus_callback callback,
                                          void *user_data);

#ifdef __cplusplus
}
#endif

#endif
