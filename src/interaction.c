/*
 * Pt gesture, long-press and kinetic scrolling handling.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <math.h>
#include <stdint.h>

#define pt_long_press_slop 8.0f
#define pt_momentum_friction 4.2f
#define pt_momentum_min_velocity 40.0f
#define pt_momentum_max_dt 0.05f

void pt_window_set_gesture_callback(pt_window *window, pt_gesture_callback callback,
                                    void *user_data) {
    if (!window) {
        return;
    }
    window->gesture_callback = callback;
    window->gesture_data = user_data;
}

void pt_window_set_long_press_delay(pt_window *window, uint32_t milliseconds) {
    if (!window) {
        return;
    }
    window->long_press_delay = milliseconds;
    if (milliseconds == 0) {
        pt_window_press_cancel(window);
    }
}

void pt_window_set_stylus_callback(pt_window *window, pt_stylus_callback callback,
                                   void *user_data) {
    if (!window) {
        return;
    }
    window->stylus_callback = callback;
    window->stylus_data = user_data;
}

void pt_window_set_momentum_scrolling(pt_window *window, bool enabled) {    if (!window) {
        return;
    }
    window->momentum_enabled = enabled;
    if (!enabled) {
        pt_window_momentum_stop(window);
    }
}

bool pt_window_get_momentum_scrolling(const pt_window *window) {
    return window ? window->momentum_enabled : false;
}

static pt_widget *interaction_target_at(pt_window *window, float x, float y) {
    if (!window || !window->root) {
        return NULL;
    }
    pt_widget *hit = NULL;
    if (pt_window_overlays_active(window)) {
        hit = pt_window_overlay_hit_test(window, x, y);
    } else {
        hit = pt_widget_hit_test(window->root, x, y);
    }
    return hit;
}

static void emit_gesture(pt_window *window, const pt_gesture_event *event) {
    if (window->gesture_callback) {
        window->gesture_callback(window, event, window->gesture_data);
    }
}

void pt_window_gesture_input(pt_window *window, pt_gesture_kind kind, pt_gesture_phase phase,
                             uint32_t fingers, float dx, float dy, float scale,
                             float rotation) {
    if (!window) {
        return;
    }
    if (phase == pt_gesture_phase_begin) {
        window->gesture_active = true;
        window->gesture_kind = kind;
        window->gesture_fingers = fingers;
        window->gesture_dx = 0.0f;
        window->gesture_dy = 0.0f;
        window->gesture_scale = 1.0f;
        window->gesture_rotation = 0.0f;
        window->gesture_x = window->pointer_x;
        window->gesture_y = window->pointer_y;
        window->gesture_target =
            interaction_target_at(window, window->pointer_x, window->pointer_y);
    } else if (!window->gesture_active || window->gesture_kind != kind) {
        return;
    }
    if (phase == pt_gesture_phase_update) {
        window->gesture_dx += dx;
        window->gesture_dy += dy;
        window->gesture_x += dx;
        window->gesture_y += dy;
        if (fingers > 0) {
            window->gesture_fingers = fingers;
        }
        if (kind == pt_gesture_kind_pinch) {
            if (scale > 0.0f) {
                window->gesture_scale = scale;
            }
            window->gesture_rotation += rotation;
        }
    }
    pt_gesture_event event = {
        .kind = window->gesture_kind,
        .phase = phase,
        .fingers = window->gesture_fingers,
        .x = window->gesture_x,
        .y = window->gesture_y,
        .dx = window->gesture_dx,
        .dy = window->gesture_dy,
        .scale = window->gesture_scale,
        .rotation = window->gesture_rotation,
        .target = window->gesture_target,
    };
    emit_gesture(window, &event);
    if (phase == pt_gesture_phase_end || phase == pt_gesture_phase_cancel) {
        window->gesture_active = false;
        window->gesture_target = NULL;
    }
}

void pt_window_press_begin(pt_window *window, pt_widget *target, float x, float y,
                           uint32_t button) {
    if (!window) {
        return;
    }
    pt_window_momentum_stop(window);
    window->long_press_timer = 0;
    window->long_press_fired = false;
    window->press_target = NULL;
    if (button != 0x110u || window->long_press_delay == 0 || !target || !target->enabled ||
        !target->visible) {
        return;
    }
    window->press_target = target;
    window->press_x = x;
    window->press_y = y;
    window->long_press_timer =
        (uint64_t)(pt_now_ms() + (int64_t)window->long_press_delay);
}

void pt_window_press_motion(pt_window *window, float x, float y) {
    if (!window || !window->long_press_timer) {
        return;
    }
    float dx = x - window->press_x;
    float dy = y - window->press_y;
    if (dx * dx + dy * dy > pt_long_press_slop * pt_long_press_slop) {
        window->long_press_timer = 0;
        window->press_target = NULL;
    }
}

void pt_window_press_end(pt_window *window, float x, float y) {
    if (!window) {
        return;
    }
    if (window->long_press_fired) {
        pt_gesture_event event = {
            .kind = pt_gesture_kind_hold,
            .phase = pt_gesture_phase_end,
            .fingers = 1,
            .x = x,
            .y = y,
            .scale = 1.0f,
            .target = window->press_target,
        };
        emit_gesture(window, &event);
    }
    window->long_press_timer = 0;
    window->long_press_fired = false;
    window->press_target = NULL;
}

void pt_window_press_cancel(pt_window *window) {
    if (!window) {
        return;
    }
    if (window->long_press_fired) {
        pt_gesture_event event = {
            .kind = pt_gesture_kind_hold,
            .phase = pt_gesture_phase_cancel,
            .fingers = 1,
            .x = window->pointer_x,
            .y = window->pointer_y,
            .scale = 1.0f,
            .target = window->press_target,
        };
        emit_gesture(window, &event);
    }
    window->long_press_timer = 0;
    window->long_press_fired = false;
    window->press_target = NULL;
}

bool pt_window_interaction_tick(pt_window *window, float dt) {
    if (!window) {
        return false;
    }
    if (window->long_press_timer && !window->long_press_fired &&
        pt_now_ms() >= (int64_t)window->long_press_timer) {
        window->long_press_timer = 0;
        window->long_press_fired = true;
        pt_gesture_event event = {
            .kind = pt_gesture_kind_hold,
            .phase = pt_gesture_phase_begin,
            .fingers = 1,
            .x = window->press_x,
            .y = window->press_y,
            .scale = 1.0f,
            .target = window->press_target,
        };
        emit_gesture(window, &event);
    }
    if (window->momentum_active) {
        pt_window_momentum_advance(window, dt);
    }
    return window->momentum_active;
}

int64_t pt_window_interaction_deadline(const pt_window *window) {
    if (!window || window->long_press_fired || !window->long_press_timer) {
        return -1;
    }
    return (int64_t)window->long_press_timer;
}

void pt_window_momentum_start(pt_window *window, float velocity, float x, float y) {
    if (!window || !window->momentum_enabled || !isfinite(velocity) ||
        fabsf(velocity) < pt_momentum_min_velocity) {
        return;
    }
    window->momentum_active = true;
    window->momentum_velocity = velocity;
    window->momentum_x = x;
    window->momentum_y = y;
    window->momentum_last_time = pt_now_seconds();
    window->animating = true;
}

void pt_window_momentum_stop(pt_window *window) {
    if (!window) {
        return;
    }
    window->momentum_active = false;
    window->momentum_velocity = 0.0f;
}

void pt_window_momentum_advance(pt_window *window, float dt) {
    if (!window || !window->momentum_active) {
        return;
    }
    dt = pt_clamp_float(dt, 0.0f, pt_momentum_max_dt);
    if (dt <= 0.0f) {
        return;
    }
    pt_widget *target = interaction_target_at(window, window->momentum_x, window->momentum_y);
    bool moved = pt_widget_scroll_pixels(target, window->momentum_velocity * dt);
    window->momentum_velocity *= expf(-pt_momentum_friction * dt);
    if (!moved || fabsf(window->momentum_velocity) < pt_momentum_min_velocity) {
        window->momentum_active = false;
        window->momentum_velocity = 0.0f;
    }
}
