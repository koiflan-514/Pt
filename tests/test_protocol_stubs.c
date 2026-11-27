/*
 * W1 protocol-extension stubs: the public activation and foreign-surface
 * entry points must be NULL-safe, headless-safe and take the documented
 * false/no-op fallback before the protocol waves fill them in.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include "harness.h"

#include <stdlib.h>
#include <string.h>

static int token_calls;
static int handle_calls;

static void count_token(pt_window *window, const char *token, void *user_data) {
    (void)window;
    (void)token;
    (void)user_data;
    ++token_calls;
}

static void count_handle(pt_window *window, const char *handle, void *user_data) {
    (void)window;
    (void)handle;
    (void)user_data;
    ++handle_calls;
}

/* Compile-time signature check: both callback typedefs must have the
 * (window, borrowed string, user_data) shape used by the other window
 * callbacks. A mismatch is a -Werror build failure here. */
static void check_callback_signatures(void) {
    pt_activation_token_callback token = count_token;
    pt_foreign_handle_callback handle = count_handle;
    void (*token_shape)(pt_window *, const char *, void *) = token;
    void (*handle_shape)(pt_window *, const char *, void *) = handle;

    pt_check(token != NULL);
    pt_check(handle != NULL);
    pt_check(token_shape != NULL);
    pt_check(handle_shape != NULL);
}

static void check_null_and_empty(const pt_window *window) {
    pt_check(!pt_window_request_activation_token(NULL, "pt.tests.stubs", count_token, NULL));
    pt_check(!pt_window_request_activation_token((pt_window *)window, NULL, NULL, NULL));
    pt_check(!pt_window_activate(NULL, "token"));
    pt_check(!pt_window_activate((pt_window *)window, NULL));
    pt_check(!pt_window_activate((pt_window *)window, ""));

    pt_check(!pt_window_export_foreign_handle(NULL, count_handle, NULL));
    pt_check(!pt_window_export_foreign_handle((pt_window *)window, NULL, NULL));
    pt_check(!pt_window_set_foreign_parent(NULL, "handle"));
    pt_check(!pt_window_set_foreign_parent((pt_window *)window, NULL));
    pt_check(!pt_window_set_foreign_parent((pt_window *)window, ""));

    /* The no-op revokers accept a NULL window and never crash. */
    pt_window_unexport_foreign_handle(NULL);
    pt_window_clear_foreign_parent(NULL);
}

static void check_headless_fallback(pt_window *window) {
    /* W1 binds no activation or foreign global. Every well-formed request must
     * report unavailable without invoking a callback or creating an object. */
    pt_check(!pt_window_request_activation_token(window, "pt.tests.stubs", count_token, NULL));
    pt_check(!pt_window_request_activation_token(window, NULL, count_token, NULL));
    pt_check(!pt_window_activate(window, "opaque-token"));

    pt_check(!pt_window_export_foreign_handle(window, count_handle, NULL));
    pt_check(!pt_window_set_foreign_parent(window, "opaque-handle"));

    pt_window_unexport_foreign_handle(window);
    pt_window_clear_foreign_parent(window);

    pt_check(token_calls == 0);
    pt_check(handle_calls == 0);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    /* With no activation global, the startup token must stay untouched. */
    setenv("XDG_ACTIVATION_TOKEN", "pt-w1-stub-token", 1);

    check_callback_signatures();
    check_null_and_empty(NULL);

    pt_app *app = pt_app_create("pt.tests.protocol-stubs");
    pt_check(app != NULL);
    if (!app) {
        unsetenv("XDG_ACTIVATION_TOKEN");
        return pt_test_finish("protocol_stubs");
    }

    const char *token = getenv("XDG_ACTIVATION_TOKEN");
    pt_check(token != NULL && strcmp(token, "pt-w1-stub-token") == 0);

    pt_window *window = pt_window_create(app, "protocol stubs", 320, 200);
    pt_check(window != NULL);
    if (window) {
        check_null_and_empty(window);
        check_headless_fallback(window);
    }

    pt_window_unexport_foreign_handle(window);
    pt_window_clear_foreign_parent(window);
    pt_app_destroy(app);
    unsetenv("XDG_ACTIVATION_TOKEN");

    return pt_test_finish("protocol_stubs");
}