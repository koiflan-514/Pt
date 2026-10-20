/*
 * Pt application object and event loop.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t pt_timer_add(pt_app *app, uint32_t milliseconds, pt_timer_callback callback,
                             void *user_data) {
    if (!app || !callback) {
        return 0;
    }
    pt_timer *timer = pt_alloc_zeroed(1, sizeof(*timer));
    timer->id = app->next_timer_id++;
    if (app->next_timer_id == 0) {
        app->next_timer_id = 1;
    }
    timer->deadline_ms = pt_now_ms() + (int64_t)milliseconds;
    timer->callback = callback;
    timer->user_data = user_data;
    timer->next = app->timers;
    app->timers = timer;
    return timer->id;
}

uint64_t pt_app_add_timeout_internal(pt_app *app, uint32_t ms, pt_timer_callback callback,
                                     void *data) {
    return pt_timer_add(app, ms, callback, data);
}

uint64_t pt_app_add_timeout(pt_app *app, uint32_t milliseconds, pt_timer_callback callback,
                            void *user_data) {
    return pt_timer_add(app, milliseconds, callback, user_data);
}

void pt_app_cancel_timeout(pt_app *app, uint64_t timer_id) {
    if (!app || timer_id == 0) {
        return;
    }
    pt_timer **link = &app->timers;
    while (*link) {
        pt_timer *timer = *link;
        if (timer->id == timer_id) {
            *link = timer->next;
            free(timer);
            return;
        }
        link = &timer->next;
    }
}

static bool run_due_timers(pt_app *app, int64_t now) {
    bool any = false;
    pt_timer **link = &app->timers;
    while (*link) {
        pt_timer *timer = *link;
        if (timer->deadline_ms > now) {
            link = &timer->next;
            continue;
        }
        *link = timer->next;
        pt_timer_callback callback = timer->callback;
        void *data = timer->user_data;
        free(timer);
        any = true;
        callback(app, data);
        if (app->quit_requested) {
            break;
        }
        link = &app->timers;
    }
    return any;
}

static int64_t next_timer_deadline(const pt_app *app) {
    int64_t next = -1;
    for (const pt_timer *timer = app->timers; timer; timer = timer->next) {
        if (next < 0 || timer->deadline_ms < next) {
            next = timer->deadline_ms;
        }
    }
    return next;
}

static int64_t next_interaction_deadline(const pt_app *app) {
    int64_t next = -1;
    for (const pt_window *window = app->first_window; window; window = window->next) {
        if (window->close_requested) {
            continue;
        }
        int64_t deadline = pt_window_interaction_deadline(window);
        if (deadline >= 0 && (next < 0 || deadline < next)) {
            next = deadline;
        }
        if (window->drag_active && window->drag_timeout && window->drag_deadline > 0 &&
            (next < 0 || window->drag_deadline < next)) {
            next = window->drag_deadline;
        }
    }
    return next;
}

static bool any_window_wants_frames(const pt_app *app) {
    for (const pt_window *window = app->first_window; window; window = window->next) {
        if (window->close_requested) {
            continue;
        }
        if (window->animating || window->needs_draw) {
            return true;
        }
        if (window->focused && window->keyboard_focused &&
            pt_widget_kind_is_text_input(window->focused->kind)) {
            return true;
        }
    }
    return false;
}

static void tick_windows(pt_app *app) {
    int64_t now = pt_now_ms();
    for (pt_window *window = app->first_window; window; window = window->next) {
        if (window->close_requested) {
            continue;
        }
        if (window->drag_active && window->drag_timeout && window->drag_deadline > 0 &&
            now >= window->drag_deadline) {
            pt_window_cancel_drag(window);
        }
        pt_window_tick(window);
    }
}

static bool app_has_open_window(const pt_app *app) {
    for (const pt_window *window = app->first_window; window; window = window->next) {
        if (!window->close_requested) {
            return true;
        }
    }
    return false;
}

static int app_event_loop(pt_app *app, bool timed, int64_t stop_at) {
    app->running = true;
    app->quit_requested = false;
    for (pt_window *window = app->first_window; window; window = window->next) {
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
        pt_window_damage_all(window);
        if (window->shown) {
            pt_platform_window_schedule_draw(window);
        }
    }
    while (!app->quit_requested) {
        int64_t now = pt_now_ms();
        if (timed) {
            if (now >= stop_at) {
                break;
            }
        } else if (!timed && app->quit_on_last_window_close && !app_has_open_window(app)) {
            break;
        }
        run_due_timers(app, now);
        pt_app_cleanup_windows(app);
        if (app->quit_requested) {
            break;
        }
        if (timed) {
            if (pt_now_ms() >= stop_at) {
                break;
            }
        } else if (!timed && app->quit_on_last_window_close && !app_has_open_window(app)) {
            break;
        }
        tick_windows(app);
        pt_app_cleanup_windows(app);
        if (app->quit_requested) {
            break;
        }

        pt_dnd_pump(app);
        bool platform_io = pt_platform_pump_io(app);

        int timeout = 250;
        if (any_window_wants_frames(app)) {
            timeout = 16;
        }
        if ((platform_io || pt_dnd_pending(app)) && timeout > 4) {
            timeout = 4;
        }
        int64_t next = next_timer_deadline(app);
        int64_t interaction = next_interaction_deadline(app);
        if (interaction >= 0 && (next < 0 || interaction < next)) {
            next = interaction;
        }
        now = pt_now_ms();
        if (next >= 0) {
            int64_t wait = next - now;
            if (wait < 0) {
                wait = 0;
            }
            if (wait < timeout) {
                timeout = (int)wait;
            }
        }
        if (timed) {
            int64_t wait = stop_at - now;
            if (wait < 0) {
                wait = 0;
            }
            if (wait < timeout) {
                timeout = (int)wait;
            }
        }
        int result = pt_platform_dispatch(app, timeout);
        if (result < 0) {
            app->running = false;
            app->quit_requested = true;
            return -1;
        }
        pt_app_cleanup_windows(app);
    }
    app->running = false;
    return 0;
}

pt_app *pt_app_create(const char *app_id) {
    pt_app *app = pt_alloc_zeroed(1, sizeof(*app));
    app->app_id = pt_strdup(app_id && *app_id ? app_id : "pt.application");
    app->next_timer_id = 1;
    app->quit_on_last_window_close = true;
    const char *reduced = getenv("PT_REDUCED_MOTION");
    app->reduced_motion = reduced &&
                          (strcmp(reduced, "1") == 0 || strcmp(reduced, "true") == 0 ||
                           strcmp(reduced, "yes") == 0);
    pt_theme_init(&app->theme, pt_color_scheme_dark);
    if (!pt_platform_start(app)) {
        free(app->app_id);
        free(app);
        return NULL;
    }
    app->initialized = true;
    pt_log_debug("app %s ready", app->app_id);
    return app;
}

void pt_app_cleanup_windows(pt_app *app) {
    if (!app) {
        return;
    }
    bool closed_any = false;
    pt_window *window = app->first_window;
    while (window) {
        pt_window *next = window->next;
        if (window->close_requested) {
            pt_window_free(window);
            closed_any = true;
        }
        window = next;
    }
    if (closed_any && app->running && app->quit_on_last_window_close &&
        !app_has_open_window(app)) {
        app->quit_requested = true;
    }
}

void pt_app_destroy(pt_app *app) {
    if (!app) {
        return;
    }
    while (app->timers) {
        pt_timer *next = app->timers->next;
        free(app->timers);
        app->timers = next;
    }
    pt_a11y_bridge_stop(app);
    while (app->first_window) {
        pt_window_free(app->first_window);
    }
    pt_platform_stop(app);
    free(app->app_id);
    free(app);
}

int pt_app_run(pt_app *app) {
    if (!app) {
        return -1;
    }
    return app_event_loop(app, false, 0);
}

int pt_app_run_for(pt_app *app, uint32_t timeout_ms) {
    if (!app) {
        return -1;
    }
    int64_t stop_at = pt_now_ms() + (int64_t)timeout_ms;
    return app_event_loop(app, true, stop_at);
}

void pt_app_quit(pt_app *app) {
    if (app) {
        app->quit_requested = true;
    }
}

bool pt_app_is_running(const pt_app *app) {
    return app && app->running;
}

void pt_app_set_color_scheme(pt_app *app, pt_color_scheme scheme) {
    if (!app || app->theme.scheme == scheme) {
        return;
    }
    pt_theme_set_defaults(&app->theme, scheme);
    for (pt_window *window = app->first_window; window; window = window->next) {
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

pt_color_scheme pt_app_get_color_scheme(const pt_app *app) {
    return app ? app->theme.scheme : pt_color_scheme_dark;
}

void pt_app_set_accent_color(pt_app *app, pt_color color) {
    if (!app) {
        return;
    }
    bool dark = app->theme.scheme == pt_color_scheme_dark;
    app->theme.colors[pt_theme_accent] = color;
    app->theme.colors[pt_theme_accent_hover] =
        dark ? pt_color_lighten(color, 0.14f) : pt_color_blend(color, 0x000000FFu, 0.14f);
    app->theme.colors[pt_theme_focus] =
        dark ? pt_color_lighten(color, 0.22f) : pt_color_blend(color, 0x000000FFu, 0.06f);
    app->theme.colors[pt_theme_accent_text] = pt_theme_contrast_text(color);
    app->theme.colors[pt_theme_accent_pressed] =
        dark ? pt_color_blend(color, 0x000000FFu, 0.18f)
             : pt_color_blend(color, 0x000000FFu, 0.22f);
    app->theme.colors[pt_theme_accent_soft] =
        pt_color_alpha(color, dark ? 0x33u : 0x26u);
    app->theme.colors[pt_theme_selection] =
        pt_color_alpha(color, dark ? 0x59u : 0x40u);
    for (pt_window *window = app->first_window; window; window = window->next) {
        window->needs_draw = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

void pt_app_set_theme_color(pt_app *app, pt_theme_token token, pt_color color) {
    if (!app || token < 0 || token >= pt_theme_token_count) {
        return;
    }
    app->theme.colors[token] = color;
    for (pt_window *window = app->first_window; window; window = window->next) {
        window->needs_draw = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

pt_color pt_app_get_theme_color(const pt_app *app, pt_theme_token token) {
    if (!app || token < 0 || token >= pt_theme_token_count) {
        return 0;
    }
    return app->theme.colors[token];
}

void pt_app_set_theme_metric(pt_app *app, pt_theme_metric metric, float value) {
    if (!app || metric < 0 || metric >= pt_theme_metric_count) {
        return;
    }
    app->theme.metrics[metric] = pt_max_float(0.0f, value);
    for (pt_window *window = app->first_window; window; window = window->next) {
        window->needs_draw = true;
        window->needs_layout = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

float pt_app_get_theme_metric(const pt_app *app, pt_theme_metric metric) {
    if (!app || metric < 0 || metric >= pt_theme_metric_count) {
        return 0.0f;
    }
    return app->theme.metrics[metric];
}

void pt_app_set_font_family(pt_app *app, const char *family) {
    if (!app) {
        return;
    }
    const char *value = family && *family ? family : "sans-serif";
    snprintf(app->theme.font_family, sizeof(app->theme.font_family), "%s", value);
    for (pt_window *window = app->first_window; window; window = window->next) {
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

void pt_app_set_quit_on_last_window_close(pt_app *app, bool enabled) {
    if (app) {
        app->quit_on_last_window_close = enabled;
    }
}
