/*
 * Pt application object and event loop.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static bool fd_watch_exists(const pt_app *app, int fd) {
    for (const pt_fd_watch *watch = app ? app->fd_watches : NULL; watch;
         watch = watch->next) {
        if (watch->fd == fd) {
            return true;
        }
    }
    for (const pt_fd_watch *watch = app ? app->fd_garbage : NULL; watch;
         watch = watch->next) {
        if (watch->fd == fd) {
            return true;
        }
    }
    return false;
}

static void fd_watch_free(pt_fd_watch *watch) {
    if (!watch) {
        return;
    }
    if (watch->owned && watch->fd >= 0) {
        close(watch->fd);
    }
    free(watch);
}

static void fd_garbage_collect(pt_app *app) {
    if (!app || app->fd_dispatch_depth > 0) {
        return;
    }
    pt_fd_watch *watch = app->fd_garbage;
    app->fd_garbage = NULL;
    while (watch) {
        pt_fd_watch *next = watch->next;
        fd_watch_free(watch);
        watch = next;
    }
}

static uint64_t fd_watch_add(pt_app *app, int fd, uint32_t events, pt_fd_callback callback,
                             void *user_data, bool owned) {
    if (!app || fd < 0 || !callback || events == 0) {
        return 0;
    }
    /* One watch per descriptor. A second watch would make poll results ambiguous
     * and an owned fd would be closed twice. */
    if (fd_watch_exists(app, fd)) {
        pt_set_error("fd is already watched");
        return 0;
    }
    pt_fd_watch *watch = pt_alloc_zeroed(1, sizeof(*watch));
    watch->id = app->next_fd_watch_id++;
    if (app->next_fd_watch_id == 0) {
        app->next_fd_watch_id = 1;
    }
    watch->fd = fd;
    watch->events = events;
    watch->callback = callback;
    watch->user_data = user_data;
    watch->owned = owned;
    watch->next = app->fd_watches;
    app->fd_watches = watch;
    return watch->id;
}

uint64_t pt_app_watch_fd(pt_app *app, int fd, uint32_t events, pt_fd_callback callback,
                         void *user_data) {
    return fd_watch_add(app, fd, events, callback, user_data, false);
}

uint64_t pt_app_watch_fd_owned(pt_app *app, int fd, uint32_t events,
                               pt_fd_callback callback, void *user_data) {
    return fd_watch_add(app, fd, events, callback, user_data, true);
}

void pt_app_unwatch_fd(pt_app *app, uint64_t watch_id) {
    if (!app || watch_id == 0) {
        return;
    }
    pt_fd_watch **link = &app->fd_watches;
    while (*link) {
        pt_fd_watch *watch = *link;
        if (watch->id == watch_id) {
            *link = watch->next;
            watch->next = NULL;
            if (app->fd_dispatch_depth > 0) {
                /* The callback that just unwatched itself is still running. Keep
                 * the record (and an owned fd) until dispatch returns. */
                watch->next = app->fd_garbage;
                app->fd_garbage = watch;
            } else {
                fd_watch_free(watch);
            }
            return;
        }
        link = &watch->next;
    }
}

size_t pt_app_fd_watch_count(const pt_app *app) {
    size_t count = 0;
    for (const pt_fd_watch *watch = app ? app->fd_watches : NULL; watch; watch = watch->next) {
        ++count;
    }
    return count;
}

static short fd_poll_events(uint32_t events) {
    short poll_events = 0;
    if (events & pt_fd_event_read) {
        poll_events |= POLLIN;
    }
    if (events & pt_fd_event_write) {
        poll_events |= POLLOUT;
    }
    return poll_events;
}

static uint32_t fd_event_mask(short revents) {
    uint32_t mask = 0;
    if (revents & (POLLIN | POLLPRI)) {
        mask |= pt_fd_event_read;
    }
    if (revents & POLLOUT) {
        mask |= pt_fd_event_write;
    }
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
        /* Error, hangup and invalid-descriptor all take the error path so a
         * callback always runs at least once for a dead watch. */
        mask |= pt_fd_event_error;
    }
    if (revents & POLLHUP) {
        mask |= pt_fd_event_hangup;
    }
    return mask;
}

void pt_app_fd_watch_prepare(const pt_app *app, struct pollfd *fds, size_t offset) {
    if (!app || !fds) {
        return;
    }
    size_t index = offset;
    for (const pt_fd_watch *watch = app->fd_watches; watch; watch = watch->next) {
        fds[index].fd = watch->fd;
        fds[index].events = fd_poll_events(watch->events);
        fds[index].revents = 0;
        ++index;
    }
}

void pt_app_fd_watch_dispatch(pt_app *app, const struct pollfd *fds, size_t offset) {
    if (!app || !fds || !app->fd_watches) {
        return;
    }
    /* Snapshot poll results by id: a callback may unwatch itself or another
     * watch, and the list is not stable across callbacks. */
    size_t count = pt_app_fd_watch_count(app);
    uint64_t *ids = pt_alloc(count * sizeof(*ids));
    size_t index = 0;
    for (const pt_fd_watch *watch = app->fd_watches; watch; watch = watch->next) {
        ids[index++] = watch->id;
    }
    app->fd_dispatch_depth++;
    for (size_t i = 0; i < count; ++i) {
        short revents = fds[offset + i].revents;
        if (revents == 0) {
            continue;
        }
        pt_fd_watch *watch = app->fd_watches;
        while (watch && watch->id != ids[i]) {
            watch = watch->next;
        }
        if (watch) {
            watch->callback(app, fd_event_mask(revents), watch->user_data);
        }
    }
    app->fd_dispatch_depth--;
    fd_garbage_collect(app);
    free(ids);
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
    app->next_fd_watch_id = 1;
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
    while (app->fd_watches) {
        pt_fd_watch *next = app->fd_watches->next;
        fd_watch_free(app->fd_watches);
        app->fd_watches = next;
    }
    app->fd_dispatch_depth = 0;
    fd_garbage_collect(app);
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
