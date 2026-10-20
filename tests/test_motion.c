/*
 * Easing, transitions and the reduced-motion preference.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

static void test_easing(void) {
    pt_easing curves[] = {pt_easing_linear,      pt_easing_in_quad,
                          pt_easing_out_quad,    pt_easing_in_out_quad,
                          pt_easing_out_cubic,   pt_easing_in_out_cubic,
                          pt_easing_out_back};
    for (size_t i = 0; i < sizeof(curves) / sizeof(curves[0]); ++i) {
        pt_check_near(pt_ease(curves[i], 0.0f), 0.0, 0.0001);
        pt_check_near(pt_ease(curves[i], 1.0f), 1.0, 0.0001);
        pt_check_near(pt_ease(curves[i], -3.0f), 0.0, 0.0001);
        pt_check_near(pt_ease(curves[i], 4.0f), 1.0, 0.0001);
    }
    pt_check_near(pt_ease(pt_easing_linear, 0.5f), 0.5, 0.0001);
    pt_check_near(pt_ease(pt_easing_in_quad, 0.5f), 0.25, 0.0001);
    pt_check_near(pt_ease(pt_easing_out_quad, 0.5f), 0.75, 0.0001);
    pt_check_near(pt_ease(pt_easing_in_out_quad, 0.5f), 0.5, 0.0001);
    pt_check(pt_ease(pt_easing_out_back, 0.7f) > 1.0f);
    pt_check_near(pt_ease(pt_easing_in_out_cubic, 0.5f), 0.5, 0.0001);
    pt_check_near(pt_ease_value(pt_easing_linear, 10.0f, 30.0f, 0.25f), 15.0, 0.0001);
    pt_check_near(pt_ease_value(pt_easing_out_quad, 0.0f, 100.0f, 0.5f), 75.0, 0.0001);
}

static void test_transition(void) {
    pt_transition transition;
    pt_transition_init(&transition, 0.0f);
    pt_check(!pt_transition_active(&transition));
    pt_check_near(transition.value, 0.0, 0.0001);

    pt_transition_to(&transition, 10.0f, 100, pt_easing_linear);
    pt_check(pt_transition_active(&transition));
    pt_check(pt_transition_advance(&transition, 0.05f));
    pt_check_near(transition.value, 5.0, 0.01);
    pt_check(pt_transition_advance(&transition, 0.1f) == false);
    pt_check(!pt_transition_active(&transition));
    pt_check_near(transition.value, 10.0, 0.0001);

    /* Retargeting starts from the current value instead of jumping. */
    pt_transition_to(&transition, -10.0f, 100, pt_easing_linear);
    pt_check_near(transition.start, 10.0, 0.0001);
    pt_transition_advance(&transition, 0.05f);
    float midway = transition.value;
    pt_check_near(midway, 0.0, 0.01);
    pt_transition_to(&transition, 20.0f, 100, pt_easing_linear);
    pt_check(pt_transition_active(&transition));
    pt_check_near(transition.start, midway, 0.0001);
    pt_transition_advance(&transition, 0.05f);
    pt_check(transition.value > midway);
    pt_transition_advance(&transition, 1.0f);
    pt_check_near(transition.value, 20.0, 0.0001);

    /* Zero duration and no-op retargets settle immediately. */
    pt_transition_to(&transition, 30.0f, 0, pt_easing_out_cubic);
    pt_check(!pt_transition_active(&transition));
    pt_check_near(transition.value, 30.0, 0.0001);
    pt_transition_to(&transition, 30.0f, 100, pt_easing_out_cubic);
    pt_check(!pt_transition_active(&transition));

    pt_transition_set(&transition, 42.0f);
    pt_check_near(transition.value, 42.0, 0.0001);
    pt_check_near(transition.target, 42.0, 0.0001);
    pt_check(!pt_transition_active(&transition));
}

static void test_reduced_motion(pt_app *app) {
    pt_check(!pt_app_get_reduced_motion(app));

    pt_window *window = pt_window_create(app, "motion", 240, 160);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 0.0f, 0.0f, 0.0f, 0.0f);
    pt_widget *button = pt_button_create(root, "Animate");
    pt_widget_set_size(button, 160.0f, 40.0f);
    pt_widget_set_align_self(button, pt_align_start);

    pt_canvas *canvas = pt_canvas_create(240, 160, 1.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_window_input_pointer_enter(window, 20.0f, 60.0f);
    pt_check(pt_widget_update_animations(root, 0.016f));
    pt_check(button->hover_mix > 0.0f && button->hover_mix < 1.0f);

    pt_app_set_reduced_motion(app, true);
    pt_check(pt_app_get_reduced_motion(app));
    button->hover_mix = 0.0f;
    pt_widget_update_animations(root, 0.016f);
    pt_check_near(button->hover_mix, 1.0, 0.0001);
    pt_app_set_reduced_motion(app, false);

    /* The preference defaults from the environment at app creation. */
    setenv("PT_REDUCED_MOTION", "1", 1);
    pt_app *second = pt_app_create("pt.tests.motion.env");
    pt_check(second != NULL);
    if (second) {
        pt_check(pt_app_get_reduced_motion(second));
        pt_app_destroy(second);
    }
    unsetenv("PT_REDUCED_MOTION");

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    unsetenv("PT_REDUCED_MOTION");
    pt_app *app = pt_app_create("pt.tests.motion");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("motion");
    }
    test_easing();
    test_transition();
    test_reduced_motion(app);
    pt_app_destroy(app);
    return pt_test_finish("motion");
}
