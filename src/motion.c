/*
 * Pt easing curves and value transitions.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

static float ease_out_back(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

float pt_ease(pt_easing easing, float t) {
    t = pt_clamp_float(t, 0.0f, 1.0f);
    switch (easing) {
    case pt_easing_in_quad:
        return t * t;
    case pt_easing_out_quad:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case pt_easing_in_out_quad:
        return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * 0.5f;
    case pt_easing_out_cubic: {
        float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case pt_easing_in_out_cubic:
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) *
                                     (-2.0f * t + 2.0f) * 0.5f;
    case pt_easing_out_back:
        return ease_out_back(t);
    case pt_easing_linear:
    default:
        return t;
    }
}

float pt_ease_value(pt_easing easing, float from, float to, float t) {
    return from + (to - from) * pt_ease(easing, t);
}

void pt_transition_init(pt_transition *transition, float value) {
    if (!transition) {
        return;
    }
    memset(transition, 0, sizeof(*transition));
    transition->value = value;
    transition->start = value;
    transition->target = value;
    transition->easing = pt_easing_out_cubic;
}

void pt_transition_to(pt_transition *transition, float target, uint32_t duration_ms,
                       pt_easing easing) {
    if (!transition) {
        return;
    }
    transition->start = transition->value;
    transition->elapsed = 0.0f;
    transition->target = target;
    transition->duration = (float)duration_ms / 1000.0f;
    transition->easing = easing;
    if (transition->duration <= 0.0f || target == transition->value) {
        transition->value = target;
        transition->start = target;
        transition->elapsed = transition->duration;
        transition->active = false;
        return;
    }
    transition->active = true;
}

bool pt_transition_advance(pt_transition *transition, float dt) {
    if (!transition || !transition->active) {
        return false;
    }
    transition->elapsed += pt_max_float(0.0f, dt);
    float t = transition->duration > 0.0f ? transition->elapsed / transition->duration : 1.0f;
    if (t >= 1.0f) {
        transition->value = transition->target;
        transition->elapsed = transition->duration;
        transition->active = false;
        return false;
    }
    transition->value =
        pt_ease_value(transition->easing, transition->start, transition->target,
                      t);
    return true;
}

void pt_transition_set(pt_transition *transition, float value) {
    if (!transition) {
        return;
    }
    transition->value = value;
    transition->start = value;
    transition->target = value;
    transition->elapsed = 0.0f;
    transition->active = false;
}

bool pt_transition_active(const pt_transition *transition) {
    return transition && transition->active;
}

void pt_app_set_reduced_motion(pt_app *app, bool reduced) {
    if (app) {
        app->reduced_motion = reduced;
    }
}

bool pt_app_get_reduced_motion(const pt_app *app) {
    return app && app->reduced_motion;
}
