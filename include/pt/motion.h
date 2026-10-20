/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_motion_h
#define pt_motion_h

#include <pt/base.h>

/* Header contract
 *
 * Threading. pt_ease, pt_ease_value and the pt_transition_* helpers operate
 * only on caller-owned values and may be used from any thread that owns the
 * transition. The reduced-motion setter and getter read application state and
 * are UI-thread only.
 * Ownership. pt_transition is caller-owned storage with no allocation; it may
 * live inside a custom widget and needs no destroy call.
 * Errors. Easing and advance clamp out-of-range time and duration instead of
 * failing. A NULL transition is a no-op for setters and yields false or zero
 * from queries. Reduced motion defaults from the PT_REDUCED_MOTION environment
 * variable; an explicit setter overrides it for framework controls.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Easing functions map a normalized time in [0, 1] to a progress value.
 * out_back overshoots slightly before settling, which suits small control
 * state changes but not layout. */
typedef enum pt_easing {
    pt_easing_linear = 0,
    pt_easing_in_quad,
    pt_easing_out_quad,
    pt_easing_in_out_quad,
    pt_easing_out_cubic,
    pt_easing_in_out_cubic,
    pt_easing_out_back
} pt_easing;

pt_api float pt_ease(pt_easing easing, float t);
pt_api float pt_ease_value(pt_easing easing, float from, float to, float t);

/* A transition animates a value over a fixed duration. Calling
 * pt_transition_to while one is running retargets from the current value, so
 * interrupted animations never jump. The struct is caller-owned and holds no
 * allocations; it may live inside a custom widget. */
typedef struct pt_transition {
    float value;
    float start;
    float target;
    float elapsed;
    float duration;
    pt_easing easing;
    bool active;
} pt_transition;

pt_api void pt_transition_init(pt_transition *transition, float value);
pt_api void pt_transition_to(pt_transition *transition, float target, uint32_t duration_ms,
                             pt_easing easing);
pt_api bool pt_transition_advance(pt_transition *transition, float dt);
pt_api void pt_transition_set(pt_transition *transition, float value);
pt_api bool pt_transition_active(const pt_transition *transition);

/* Reduced motion is an app-level preference. When enabled, framework control
 * animations snap to their target state and custom widgets should finish
 * transitions instead of interpolating them. The default reads the
 * PT_REDUCED_MOTION environment variable ("1", "true" or "yes"). */
pt_api void pt_app_set_reduced_motion(pt_app *app, bool reduced);
pt_api bool pt_app_get_reduced_motion(const pt_app *app);

#ifdef __cplusplus
}
#endif

#endif
