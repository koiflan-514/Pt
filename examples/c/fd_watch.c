/*
 * Pt fd watch example: an eventfd owned by the application drives the UI.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here:
 *
 *  - Pt borrows the descriptor. It stores the value and polls it; it never
 *    dup()s or closes it. The application owns the fd and must keep it valid
 *    for the whole watch.
 *  - The callback runs on the UI thread and must not block. This example only
 *    performs a non-blocking read() and updates widget state.
 *  - A watch can be removed from inside its own callback. Pt snapshots watch
 *    ids before dispatch, so self-unwatch is safe. After unwatching, the owner
 *    closes the fd here to show where ownership sits.
 *
 * A repeating Pt timer writes to the eventfd, and the fd watch turns those
 * writes into UI updates. The timer is only a convenient event source; the
 * interesting path is fd -> poll -> callback. "Send one event" writes to the
 * same eventfd from a button callback, so the descriptor can also be driven
 * manually.
 */

#include <pt/pt.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define TICK_TARGET 8u
#define TICK_INTERVAL_MS 250

typedef struct watch_state {
    pt_app *app;
    pt_window *window;
    pt_widget *progress;
    pt_widget *counter;
    pt_widget *status;
    pt_widget *arm;
    int fd;
    uint64_t watch_id;
    uint64_t ticks;
    bool feeder_pending;
} watch_state;

static void set_status(watch_state *state, const char *text) {
    pt_widget_set_text(state->status, text);
}

static void render_ticks(watch_state *state) {
    float value = (float)state->ticks / (float)TICK_TARGET;
    if (value > 1.0f) {
        value = 1.0f;
    }
    pt_progress_set_value(state->progress, value);
    char counter[48];
    snprintf(counter, sizeof(counter), "%llu / %u events",
             (unsigned long long)state->ticks, TICK_TARGET);
    pt_widget_set_text(state->counter, counter);
}

static bool write_event(watch_state *state) {
    if (state->fd < 0) {
        return false;
    }
    uint64_t one = 1;
    ssize_t written = 0;
    do {
        written = write(state->fd, &one, sizeof(one));
    } while (written < 0 && errno == EINTR);
    return written == (ssize_t)sizeof(one);
}

/* One feeder is always enough. It stops rescheduling once the watch has been
 * released, and a pending feeder simply serves a newly armed watch. */
static void on_feeder(pt_app *app, void *user_data) {
    watch_state *state = user_data;
    state->feeder_pending = false;
    if (state->watch_id == 0 || state->fd < 0) {
        return;
    }
    (void)write_event(state);
    uint64_t next = pt_app_add_timeout(app, TICK_INTERVAL_MS, on_feeder, state);
    if (next != 0) {
        state->feeder_pending = true;
    }
}

static void schedule_feeder(pt_app *app, watch_state *state) {
    if (state->feeder_pending) {
        return;
    }
    uint64_t next = pt_app_add_timeout(app, TICK_INTERVAL_MS, on_feeder, state);
    if (next != 0) {
        state->feeder_pending = true;
    }
}

static void release_watch(pt_app *app, watch_state *state, const char *message) {
    uint64_t old_id = state->watch_id;
    state->watch_id = 0;
    /* Self-unwatch: Pt removes the watch after this dispatch round. */
    if (old_id != 0) {
        pt_app_unwatch_fd(app, old_id);
    }
    if (state->fd >= 0) {
        /* Pt only borrowed the descriptor; the application closes it. */
        close(state->fd);
        state->fd = -1;
    }
    pt_widget_set_text(state->arm, "Arm another watch");
    set_status(state, message);
}

static void on_fd(pt_app *app, uint32_t events, void *user_data) {
    watch_state *state = user_data;
    if (events & (pt_fd_event_error | pt_fd_event_hangup)) {
        release_watch(app, state, "The descriptor reported an error; the watch was released.");
        return;
    }
    if ((events & pt_fd_event_read) == 0 || state->fd < 0) {
        return;
    }

    uint64_t counter = 0;
    ssize_t got = 0;
    do {
        got = read(state->fd, &counter, sizeof(counter));
    } while (got < 0 && errno == EINTR);
    if (got != (ssize_t)sizeof(counter)) {
        if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            set_status(state, "read() on the watched eventfd failed.");
        }
        return;
    }

    state->ticks += counter;
    render_ticks(state);
    char status[128];
    snprintf(status, sizeof(status), "Read %llu event(s) from borrowed fd %d.",
             (unsigned long long)counter, state->fd);
    set_status(state, status);
    printf("fd_watch: %llu/%u events from fd %d\n", (unsigned long long)state->ticks,
           TICK_TARGET, state->fd);
    fflush(stdout);

    if (state->ticks >= TICK_TARGET) {
        release_watch(app, state,
                      "Reached 8 events. The watch released itself and the fd was closed.");
        pt_window_show_toast(state->window, "fd watch released itself", pt_toast_success,
                             1800);
        printf("fd_watch: watch released after %llu events; fd closed by its owner\n",
               (unsigned long long)state->ticks);
        fflush(stdout);
    }
}

static void arm_watch(watch_state *state) {
    if (state->watch_id != 0) {
        set_status(state, "A watch is already armed; wait for it to release itself.");
        return;
    }
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        set_status(state, "eventfd() failed.");
        return;
    }
    state->fd = fd;
    state->ticks = 0;
    render_ticks(state);
    uint64_t watch_id = pt_app_watch_fd(state->app, fd, pt_fd_event_read, on_fd, state);
    if (watch_id == 0) {
        close(fd);
        state->fd = -1;
        set_status(state, "pt_app_watch_fd() rejected the descriptor.");
        return;
    }
    state->watch_id = watch_id;
    pt_widget_set_text(state->arm, "Watch armed");
    char status[128];
    snprintf(status, sizeof(status),
             "Pt borrows eventfd %d; the feeder writes an event every %d ms.", state->fd,
             TICK_INTERVAL_MS);
    set_status(state, status);
    schedule_feeder(state->app, state);
    printf("fd_watch: armed borrowed fd %d for %u events\n", state->fd, TICK_TARGET);
    fflush(stdout);
}

static void on_arm(pt_widget *button, void *user_data) {
    (void)button;
    arm_watch((watch_state *)user_data);
}

static void on_send(pt_widget *button, void *user_data) {
    (void)button;
    watch_state *state = user_data;
    if (state->watch_id == 0 || state->fd < 0) {
        set_status(state, "Arm a watch before sending an event.");
        return;
    }
    if (write_event(state)) {
        set_status(state, "Wrote one event to the borrowed fd from a button callback.");
    } else {
        set_status(state, "Writing to the borrowed fd failed.");
    }
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.fd_watch");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt fd Watch", 640, 460);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    watch_state state = {.app = app, .window = window, .fd = -1};

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *title = pt_label_create(root, "File descriptor watch");
    pt_widget_set_font_size(title, 26.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);
    pt_widget *subtitle = pt_label_create(
        root, "An eventfd owned by this example wakes Pt's poll loop. Pt only borrows the "
              "descriptor; the callback self-unwatches, then the example closes its own fd.");
    pt_widget_set_font_size(subtitle, 13.0f);
    pt_widget_set_foreground(subtitle, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(subtitle, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 22.0f, 22.0f, 22.0f, 22.0f);
    pt_widget_set_gap(card, 14.0f);
    pt_widget_set_background(card, pt_rgb(0x141B26));
    pt_widget_set_border_width(card, 1.0f);
    pt_widget_set_border_color(card, pt_rgb(0x253040));

    state.counter = pt_label_create(card, "0 / 8 events");
    pt_widget_set_font_size(state.counter, 28.0f);
    pt_widget_set_font_weight(state.counter, pt_font_weight_semibold);
    state.progress = pt_progress_create(card);
    pt_widget_set_height(state.progress, 10.0f);
    pt_progress_set_value(state.progress, 0.0f);

    state.status = pt_label_create(card, "Arming the first watch…");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);

    pt_widget *actions = pt_row_create(card);
    pt_widget_set_gap(actions, 10.0f);
    state.arm = pt_button_create(actions, "Watch armed");
    pt_button_set_variant(state.arm, pt_button_primary);
    pt_button_set_on_click(state.arm, on_arm, &state);
    pt_widget_set_tooltip(state.arm,
                          "Creates a fresh eventfd and asks Pt to watch it for read events");
    pt_widget *send = pt_button_create(actions, "Send one event");
    pt_button_set_variant(send, pt_button_secondary);
    pt_button_set_on_click(send, on_send, &state);
    pt_widget_set_tooltip(send, "Writes to the same borrowed eventfd from a click callback");

    pt_widget *footer = pt_label_create(
        root, "The callback never blocks: it drains the eventfd with a non-blocking read, "
              "updates widgets and returns. Removing a watch from inside its own callback is "
              "supported; the fd stays owned by the application.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_window_show(window);
    arm_watch(&state);
    int status = pt_app_run(app);
    if (state.watch_id != 0) {
        pt_app_unwatch_fd(app, state.watch_id);
    }
    if (state.fd >= 0) {
        close(state.fd);
    }
    pt_app_destroy(app);
    return status;
}
