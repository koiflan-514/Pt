/*
 * Drag-and-drop checks: drop target routing, hover highlighting and the
 * entry drag source.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

typedef struct dnd_capture {
    int enters;
    int motions;
    int leaves;
    int drops;
    bool accept;
    char mime[64];
    char data[64];
    float x;
    float y;
} dnd_capture;

static bool capture_enter(pt_widget *widget, const char *mime_type, void *user_data) {
    (void)widget;
    dnd_capture *capture = user_data;
    ++capture->enters;
    snprintf(capture->mime, sizeof(capture->mime), "%s", mime_type ? mime_type : "");
    return capture->accept;
}

static void capture_motion(pt_widget *widget, float x, float y, void *user_data) {
    (void)widget;
    dnd_capture *capture = user_data;
    ++capture->motions;
    capture->x = x;
    capture->y = y;
}

static void capture_leave(pt_widget *widget, void *user_data) {
    (void)widget;
    dnd_capture *capture = user_data;
    ++capture->leaves;
}

static void capture_drop(pt_widget *widget, const char *mime_type, const char *data, float x,
                         float y, void *user_data) {
    (void)widget;
    dnd_capture *capture = user_data;
    ++capture->drops;
    snprintf(capture->mime, sizeof(capture->mime), "%s", mime_type ? mime_type : "");
    snprintf(capture->data, sizeof(capture->data), "%s", data ? data : "");
    capture->x = x;
    capture->y = y;
}

typedef struct stream_capture {
    pt_drop_stream *stream;
    char text[512];
    size_t size;
    int chunks;
    int done;
    int cancelled;
    int errors;
    size_t last_size;
    bool cancel_on_first_chunk;
} stream_capture;

typedef struct structured_capture {
    int enters;
    int motions;
    int leaves;
    int drops;
    int finished;
    bool finish_success;
    bool freed;
    uint32_t mime_count;
    uint32_t actions;
    char mimes[8][64];
    char selected[64];
    char drop_mime[64];
    size_t read_limit;
    bool read_second;
    char second_mime[64];
    float x;
    float y;
    stream_capture first;
    stream_capture second;
} structured_capture;

static void stream_chunk(pt_widget *widget, pt_drop_stream *stream,
                         pt_drop_stream_state state, const char *data, size_t size,
                         void *user_data) {
    (void)widget;
    stream_capture *capture = user_data;
    if (state == pt_drop_stream_data) {
        ++capture->chunks;
        if (capture->size + size < sizeof(capture->text)) {
            memcpy(capture->text + capture->size, data, size);
            capture->size += size;
            capture->text[capture->size] = '\0';
        }
        capture->last_size = pt_drop_stream_size(stream);
        if (capture->cancel_on_first_chunk) {
            capture->cancel_on_first_chunk = false;
            pt_drop_stream_cancel(stream);
        }
        return;
    }
    if (state == pt_drop_stream_done) {
        ++capture->done;
    } else if (state == pt_drop_stream_cancelled) {
        ++capture->cancelled;
    } else {
        ++capture->errors;
    }
    capture->stream = NULL;
}

static const char *structured_enter(pt_widget *widget, pt_drop_offer *offer,
                                    void *user_data) {
    (void)widget;
    structured_capture *capture = user_data;
    ++capture->enters;
    capture->mime_count = pt_drop_offer_mime_count(offer);
    capture->actions = pt_drop_offer_actions(offer);
    uint32_t count = capture->mime_count < 8 ? capture->mime_count : 8;
    for (uint32_t i = 0; i < count; ++i) {
        snprintf(capture->mimes[i], sizeof(capture->mimes[i]), "%s",
                 pt_drop_offer_mime(offer, i));
    }
    const char *wanted =
        capture->selected[0] ? capture->selected : pt_drop_offer_mime(offer, 0);
    for (uint32_t i = 0; i < capture->mime_count; ++i) {
        const char *mime = pt_drop_offer_mime(offer, i);
        if (mime && wanted && strcmp(mime, wanted) == 0) {
            return mime;
        }
    }
    return NULL;
}

static void structured_motion(pt_widget *widget, float x, float y, void *user_data) {
    (void)widget;
    structured_capture *capture = user_data;
    ++capture->motions;
    capture->x = x;
    capture->y = y;
}

static void structured_leave(pt_widget *widget, void *user_data) {
    (void)widget;
    structured_capture *capture = user_data;
    ++capture->leaves;
}

static void structured_drop(pt_widget *widget, pt_drop_offer *offer, const char *mime_type,
                            float x, float y, void *user_data) {
    (void)widget;
    structured_capture *capture = user_data;
    ++capture->drops;
    snprintf(capture->drop_mime, sizeof(capture->drop_mime), "%s",
             mime_type ? mime_type : "");
    capture->x = x;
    capture->y = y;
    capture->first.stream = pt_drop_offer_read(offer, mime_type, capture->read_limit,
                                               stream_chunk, &capture->first);
    if (capture->read_second) {
        capture->second.stream = pt_drop_offer_read(offer, capture->second_mime, 0,
                                                    stream_chunk, &capture->second);
    }
}

static void capture_offer_finish(pt_drop_offer *offer, bool success) {
    structured_capture *capture = offer->backend;
    ++capture->finished;
    capture->finish_success = success;
}

static void capture_offer_free(pt_drop_offer *offer) {
    structured_capture *capture = offer->backend;
    capture->freed = true;
}

static pt_drop_offer *make_offer(pt_app *app, pt_window *window,
                                 structured_capture *capture) {
    static const char *mimes[] = {"text/uri-list", "text/plain;charset=utf-8",
                                  "application/x-pt"};
    pt_drop_offer *offer = pt_drop_offer_new(app, window, mimes, 3);
    if (!offer) {
        return NULL;
    }
    offer->actions = pt_drag_action_copy | pt_drag_action_move;
    offer->backend = capture;
    offer->finish = capture_offer_finish;
    offer->free_backend = capture_offer_free;
    return offer;
}

static pt_drop_target make_structured_target(structured_capture *capture) {
    pt_drop_target target = {
        .motion = structured_motion,
        .leave = structured_leave,
        .user_data = capture,
        .enter_offer = structured_enter,
        .drop_offer = structured_drop,
    };
    return target;
}

typedef struct structured_arena {
    pt_window *window;
    pt_widget *zone;
    pt_canvas *canvas;
    float x;
    float y;
} structured_arena;

static void origin_of(const pt_widget *widget, float *x, float *y);

static bool structured_arena_create(pt_app *app, structured_arena *arena) {
    arena->window = pt_window_create(app, "structured dnd", 420, 320);
    if (!arena->window) {
        return false;
    }
    pt_widget *root = pt_window_root(arena->window);
    arena->zone = pt_box_create(root, pt_direction_column);
    pt_widget_set_size(arena->zone, 280.0f, 90.0f);
    pt_widget_set_background(arena->zone, 0x223344FFu);
    pt_window_show(arena->window);
    arena->canvas = pt_canvas_create(420, 320, 1.0f);
    if (!arena->canvas) {
        return false;
    }
    if (!pt_window_paint(arena->window, arena->canvas)) {
        return false;
    }
    origin_of(arena->zone, &arena->x, &arena->y);
    arena->x += 40.0f;
    arena->y += 30.0f;
    return true;
}

static void structured_arena_destroy(structured_arena *arena) {
    pt_canvas_destroy(arena->canvas);
    pt_window_free(arena->window);
    memset(arena, 0, sizeof(*arena));
}

static const char uri_payload[] = "file:///tmp/a.txt\r\nfile:///tmp/b.txt\r\n";

static void offer_add_payloads(pt_drop_offer *offer, uint32_t uri_chunk) {
    pt_drop_offer_add_payload(offer, "text/uri-list", uri_payload, 0, uri_chunk);
    pt_drop_offer_add_payload(offer, "text/plain;charset=utf-8", "plain payload", 0, 0);
}

static void pump_streams_until_idle(pt_app *app, structured_capture *capture) {
    for (int i = 0; i < 200 && (capture->first.stream || capture->second.stream); ++i) {
        pt_dnd_pump(app);
    }
}

typedef struct drag_finished_capture {
    int count;
    bool dropped;
    pt_drag_action action;
} drag_finished_capture;

static void capture_drag_finished(pt_window *window, bool dropped, pt_drag_action action,
                                  void *user_data) {
    (void)window;
    drag_finished_capture *capture = user_data;
    ++capture->count;
    capture->dropped = dropped;
    capture->action = action;
}

static pt_drop_target make_target(dnd_capture *capture) {
    pt_drop_target target = {
        .enter = capture_enter,
        .motion = capture_motion,
        .leave = capture_leave,
        .drop = capture_drop,
        .user_data = capture,
    };
    return target;
}

static void origin_of(const pt_widget *widget, float *x, float *y) {
    *x = widget ? widget->x : 0.0f;
    *y = widget ? widget->y : 0.0f;
}

static void test_targets(pt_app *app) {
    pt_window *window = pt_window_create(app, "dnd targets", 520, 480);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget *zone = pt_box_create(root, pt_direction_column);
    pt_widget_set_size(zone, 260.0f, 80.0f);
    pt_widget_set_background(zone, 0x223344FFu);
    pt_widget *label = pt_label_create(zone, "drop here");
    pt_widget_set_size(label, 140.0f, 40.0f);
    pt_widget *zone2 = pt_box_create(root, pt_direction_column);
    pt_widget_set_size(zone2, 260.0f, 80.0f);
    pt_widget_set_background(zone2, 0x334455FFu);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 480, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    dnd_capture zone_capture = {.accept = true};
    dnd_capture zone2_capture = {.accept = true};
    dnd_capture label_capture = {.accept = false};
    pt_drop_target zone_target = make_target(&zone_capture);
    pt_drop_target zone2_target = make_target(&zone2_capture);
    pt_drop_target label_target = make_target(&label_capture);
    pt_widget_set_drop_target(zone, &zone_target);
    pt_widget_set_drop_target(zone2, &zone2_target);
    pt_widget_set_drop_target(label, &label_target);

    float lx = 0.0f;
    float ly = 0.0f;
    origin_of(label, &lx, &ly);
    float zx = 0.0f;
    float zy = 0.0f;
    origin_of(zone, &zx, &zy);
    float z2x = 0.0f;
    float z2y = 0.0f;
    origin_of(zone2, &z2x, &z2y);

    const float px = lx + label->width * 0.5f;
    const float py = ly + label->height * 0.5f;

    pt_check(!pt_window_dnd_enter(window, "text/plain;charset=utf-8", px, py));
    pt_check(label_capture.enters == 1);
    pt_check(zone_capture.enters == 0);
    pt_check(!pt_widget_get_drop_hover(label));
    pt_check(!pt_widget_get_drop_hover(zone));

    pt_widget_set_drop_target(label, NULL);
    pt_check(pt_window_dnd_enter(window, "text/plain", px, py));
    pt_check(zone_capture.enters == 1);
    pt_check(strcmp(zone_capture.mime, "text/plain") == 0);
    pt_check(pt_widget_get_drop_hover(zone));
    pt_check(!pt_widget_get_drop_hover(zone2));
    pt_check(window->drop_hover == zone);

    pt_check(pt_window_dnd_motion(window, "text/plain", px + 10.0f, py + 4.0f));
    pt_check(zone_capture.motions == 1);
    pt_check_near(zone_capture.x, px + 10.0f - zx, 0.01);
    pt_check_near(zone_capture.y, py + 4.0f - zy, 0.01);

    float xs = z2x + 40.0f;
    float ys = z2y + 30.0f;
    pt_check(pt_window_dnd_motion(window, "text/plain", xs, ys));
    pt_check(zone_capture.leaves == 1);
    pt_check(zone2_capture.enters == 1);
    pt_check(!pt_widget_get_drop_hover(zone));
    pt_check(pt_widget_get_drop_hover(zone2));
    pt_check(zone2_capture.motions == 1);
    pt_check_near(zone2_capture.x, xs - z2x, 0.01);
    pt_check_near(zone2_capture.y, ys - z2y, 0.01);

    pt_widget_set_background(zone2, 0x334455FFu);
    pt_check(pt_window_dnd_drop(window, "text/plain", "dragged text", xs, ys));
    pt_check(zone2_capture.drops == 1);
    pt_check(strcmp(zone2_capture.data, "dragged text") == 0);
    pt_check(strcmp(zone2_capture.mime, "text/plain") == 0);
    pt_check(!pt_widget_get_drop_hover(zone2));
    pt_check(window->drop_hover == NULL);
    pt_check(pt_window_paint(window, canvas));

    pt_check(!pt_window_dnd_drop(window, "text/plain", "ignored", 4.0f, 4.0f));
    pt_check(zone2_capture.drops == 1);

    zone2_capture.accept = false;
    pt_check(!pt_window_dnd_enter(window, "text/plain", xs, ys));
    pt_check(!pt_widget_get_drop_hover(zone2));
    zone2_capture.accept = true;

    const pt_drag_data text_offer = {
        .mime_type = "text/plain;charset=utf-8", .data = "cannot drag", .size = 0};
    pt_check(!pt_window_begin_drag(window, &text_offer, 1));
    pt_window_dnd_leave(window);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_structured_target(pt_app *app) {
    structured_arena arena = {0};
    pt_check(structured_arena_create(app, &arena));
    structured_capture capture = {0};
    pt_drop_target target = make_structured_target(&capture);
    pt_widget_set_drop_target(arena.zone, &target);
    const float px = arena.x;
    const float py = arena.y;

    pt_drop_offer *offer = make_offer(app, arena.window, &capture);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 7);
    pt_check(!pt_drop_offer_read(offer, "text/uri-list", 0, stream_chunk, &capture.first));
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, px, py));
    pt_check(capture.enters == 1);
    pt_check(capture.mime_count == 3);
    pt_check(capture.actions == (uint32_t)(pt_drag_action_copy | pt_drag_action_move));
    pt_check(strcmp(capture.mimes[0], "text/uri-list") == 0);
    pt_check(strcmp(capture.mimes[1], "text/plain;charset=utf-8") == 0);
    pt_check(strcmp(capture.mimes[2], "application/x-pt") == 0);
    pt_check(pt_drop_offer_has_mime(offer, "application/x-pt"));
    pt_check(pt_drop_offer_mime(offer, 3) == NULL);
    pt_check(pt_drop_offer_selected_mime(offer) != NULL);
    pt_check(strcmp(pt_drop_offer_selected_mime(offer), "text/uri-list") == 0);
    pt_check(pt_widget_get_drop_hover(arena.zone));
    pt_check(pt_window_dnd_offer_motion(arena.window, px + 5.0f, py + 6.0f));
    pt_check(capture.motions == 1);
    pt_check(capture.enters == 1);
    pt_check_near(capture.x, arena.x - arena.zone->x + 5.0f, 0.01);
    pt_check_near(capture.y, arena.y - arena.zone->y + 6.0f, 0.01);

    pt_check(pt_window_dnd_offer_drop(arena.window, px + 5.0f, py + 6.0f));
    pt_check(capture.drops == 1);
    pt_check(strcmp(capture.drop_mime, "text/uri-list") == 0);
    pt_check(!pt_widget_get_drop_hover(arena.zone));
    pt_check(capture.first.stream != NULL);
    pump_streams_until_idle(app, &capture);
    pt_check(capture.first.done == 1);
    pt_check(capture.first.cancelled == 0);
    pt_check(capture.first.errors == 0);
    pt_check(capture.first.chunks == (int)((strlen(uri_payload) + 6u) / 7u));
    pt_check(capture.first.size == strlen(uri_payload));
    pt_check(strcmp(capture.first.text, uri_payload) == 0);
    pt_check(capture.first.last_size == strlen(uri_payload));
    pt_check(pt_uri_list_count(capture.first.text) == 2);
    char *first_uri = pt_uri_list_get(capture.first.text, 0);
    pt_check(first_uri && strcmp(first_uri, "file:///tmp/a.txt") == 0);
    pt_free(first_uri);

    /* The caller's reference keeps the offer alive; a second read after the
     * drop is rejected for unknown or unreadable types. */
    pt_check(!pt_drop_offer_read(offer, "application/x-pt", 0, stream_chunk,
                                 &capture.first));
    pt_check(!pt_drop_offer_read(offer, "text/nope", 0, stream_chunk, &capture.first));
    pt_check(capture.finished == 0);
    pt_check(!capture.freed);
    pt_drop_offer_release(offer);
    pt_check(capture.finished == 1);
    pt_check(capture.finish_success);
    pt_check(capture.freed);

    dnd_capture legacy = {.accept = true};
    structured_capture legacy_offer_capture = {0};
    pt_drop_target legacy_target = make_target(&legacy);
    pt_widget_set_drop_target(arena.zone, &legacy_target);
    offer = make_offer(app, arena.window, &legacy_offer_capture);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 7);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, px, py));
    pt_check(legacy.enters == 1);
    pt_check(strcmp(legacy.mime, "text/plain;charset=utf-8") == 0);
    pt_drop_offer_release(offer);
    pt_check(pt_window_dnd_offer_drop(arena.window, px, py));
    pt_check(legacy.drops == 1);
    pt_check(strcmp(legacy.mime, "text/plain;charset=utf-8") == 0);
    pt_check(strcmp(legacy.data, "plain payload") == 0);
    pt_check(legacy_offer_capture.finished == 1);
    pt_check(legacy_offer_capture.finish_success);
    pt_check(legacy_offer_capture.freed);

    const char *binary_mimes[] = {"application/x-pt"};
    pt_drop_offer *binary = pt_drop_offer_new(app, arena.window, binary_mimes, 1);
    pt_check(binary != NULL);
    pt_check(!pt_window_dnd_offer_enter(arena.window, binary, px, py));
    pt_check(legacy.enters == 1);
    pt_check(arena.window->drop_offer == NULL);
    pt_drop_offer_release(binary);

    structured_capture reject = {0};
    snprintf(reject.selected, sizeof(reject.selected), "%s", "application/x-missing");
    pt_drop_target reject_target = make_structured_target(&reject);
    pt_widget_set_drop_target(arena.zone, &reject_target);
    pt_drop_offer *rejected = make_offer(app, arena.window, &reject);
    pt_check(rejected != NULL);
    pt_check(!pt_window_dnd_offer_enter(arena.window, rejected, px, py));
    pt_check(reject.enters == 1);
    pt_check(!pt_widget_get_drop_hover(arena.zone));
    pt_check(arena.window->drop_offer == NULL);
    pt_drop_offer_release(rejected);
    pt_check(reject.finished == 0);
    pt_check(reject.freed);

    structured_arena_destroy(&arena);
}

static void test_structured_streams(pt_app *app) {
    structured_arena arena = {0};
    pt_check(structured_arena_create(app, &arena));
    structured_capture capture = {0};
    pt_drop_target target = make_structured_target(&capture);
    pt_widget_set_drop_target(arena.zone, &target);
    capture.read_second = true;
    snprintf(capture.second_mime, sizeof(capture.second_mime), "%s",
             "text/plain;charset=utf-8");

    pt_drop_offer *offer = make_offer(app, arena.window, &capture);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 0);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, arena.x, arena.y));
    pt_drop_offer_release(offer);
    pt_check(pt_window_dnd_offer_drop(arena.window, arena.x, arena.y));
    pt_check(capture.first.stream != NULL);
    pt_check(capture.second.stream != NULL);
    pt_check(strcmp(pt_drop_stream_mime(capture.second.stream),
                    "text/plain;charset=utf-8") == 0);
    pump_streams_until_idle(app, &capture);
    pt_check(capture.first.done == 1);
    pt_check(capture.second.done == 1);
    pt_check(capture.first.size == strlen(uri_payload));
    pt_check(strcmp(capture.first.text, uri_payload) == 0);
    pt_check(strcmp(capture.second.text, "plain payload") == 0);
    pt_check(capture.finished == 1);
    pt_check(capture.finish_success);
    pt_check(capture.freed);
    pt_check(!pt_dnd_pending(app));

    /* A byte limit aborts a chunked read with an error state. */
    structured_capture limited = {0};
    limited.read_limit = 4;
    target = make_structured_target(&limited);
    pt_widget_set_drop_target(arena.zone, &target);
    offer = make_offer(app, arena.window, &limited);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 2);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, arena.x, arena.y));
    pt_drop_offer_release(offer);
    pt_check(pt_window_dnd_offer_drop(arena.window, arena.x, arena.y));
    pump_streams_until_idle(app, &limited);
    pt_check(limited.first.chunks == 2);
    pt_check(limited.first.size == 4);
    pt_check(limited.first.errors == 1);
    pt_check(limited.first.done == 0);
    pt_check(limited.first.cancelled == 0);
    pt_check(limited.finished == 1);
    pt_check(limited.freed);

    /* Cancelling from inside a data callback is deferred until delivery ends. */
    structured_capture cancelled = {0};
    cancelled.first.cancel_on_first_chunk = true;
    target = make_structured_target(&cancelled);
    pt_widget_set_drop_target(arena.zone, &target);
    offer = make_offer(app, arena.window, &cancelled);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 4);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, arena.x, arena.y));
    pt_drop_offer_release(offer);
    pt_check(pt_window_dnd_offer_drop(arena.window, arena.x, arena.y));
    pump_streams_until_idle(app, &cancelled);
    pt_check(cancelled.first.chunks == 1);
    pt_check(cancelled.first.cancelled == 1);
    pt_check(cancelled.first.done == 0);
    pt_check(cancelled.first.size == 4);
    pt_check(cancelled.finished == 1);
    pt_check(cancelled.freed);

    structured_arena_destroy(&arena);
}

static void test_structured_lifetime(pt_app *app) {
    structured_arena arena = {0};
    pt_check(structured_arena_create(app, &arena));
    structured_capture capture = {0};
    pt_drop_target target = make_structured_target(&capture);
    pt_widget_set_drop_target(arena.zone, &target);

    pt_drop_offer *offer = make_offer(app, arena.window, &capture);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 6);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, arena.x, arena.y));
    pt_drop_offer_release(offer);
    pt_check(pt_window_dnd_offer_drop(arena.window, arena.x, arena.y));
    pt_dnd_pump(app);
    pt_check(capture.first.chunks == 1);
    pt_check(capture.first.stream != NULL);
    pt_widget_destroy(arena.zone);
    pt_check(capture.first.cancelled == 1);
    pt_check(capture.first.stream == NULL);
    pt_check(capture.finished == 1);
    pt_check(capture.finish_success);
    pt_check(capture.freed);
    pt_check(!pt_dnd_pending(app));
    arena.zone = NULL;

    /* An offer that was entered but never dropped reports no finish. */
    structured_capture entered = {0};
    target = make_structured_target(&entered);
    pt_widget *second = pt_box_create(pt_window_root(arena.window), pt_direction_column);
    pt_widget_set_size(second, 120.0f, 60.0f);
    pt_widget_set_drop_target(second, &target);
    pt_check(pt_window_paint(arena.window, arena.canvas));
    float sx = 0.0f;
    float sy = 0.0f;
    origin_of(second, &sx, &sy);
    offer = make_offer(app, arena.window, &entered);
    pt_check(offer != NULL);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, sx + 10.0f, sy + 10.0f));
    pt_drop_offer_release(offer);
    pt_check(arena.window->drop_offer != NULL);
    pt_window_free(arena.window);
    pt_check(entered.finished == 0);
    pt_check(entered.freed);
    pt_canvas_destroy(arena.canvas);

    pt_check(structured_arena_create(app, &arena));
    structured_capture closing = {0};
    target = make_structured_target(&closing);
    pt_widget_set_drop_target(arena.zone, &target);
    offer = make_offer(app, arena.window, &closing);
    pt_check(offer != NULL);
    offer_add_payloads(offer, 5);
    pt_check(pt_window_dnd_offer_enter(arena.window, offer, arena.x, arena.y));
    pt_drop_offer_release(offer);
    pt_check(pt_window_dnd_offer_drop(arena.window, arena.x, arena.y));
    pt_dnd_pump(app);
    pt_check(closing.first.chunks == 1);
    pt_window_free(arena.window);
    pt_check(closing.first.cancelled == 1);
    pt_check(closing.first.stream == NULL);
    pt_check(closing.finished == 1);
    pt_check(closing.freed);
    pt_check(!pt_dnd_pending(app));
    pt_canvas_destroy(arena.canvas);
}

static void test_drag_source_api(pt_app *app) {
    pt_window *window = pt_window_create(app, "drag source", 320, 200);
    pt_check(window != NULL);
    pt_check(pt_window_get_drag_timeout(window) == 0);
    pt_check(pt_window_get_drag_result(window) == pt_drag_action_none);
    pt_check(!pt_window_get_drag_active(window));

    pt_drag_data types[2] = {
        {.mime_type = "text/plain;charset=utf-8", .data = "hello", .size = 0},
        {.mime_type = "text/uri-list", .data = "file:///tmp/a.txt", .size = 0},
    };
    pt_check(!pt_window_begin_drag(NULL, types, 2));
    pt_check(!pt_window_begin_drag(window, NULL, 2));
    pt_check(!pt_window_begin_drag(window, types, 0));
    pt_drag_data invalid[2] = {{.mime_type = "", .data = "x", .size = 0},
                               {.mime_type = "text/plain", .data = NULL, .size = 0}};
    pt_check(!pt_window_begin_drag(window, invalid, 2));
    /* Headless has no data-device global, so even valid entries fail. */
    pt_check(!pt_window_begin_drag(window, types, 2));
    pt_check(!pt_window_get_drag_active(window));
    pt_check(pt_window_get_drag_result(window) == pt_drag_action_none);

    drag_finished_capture finished = {0};
    pt_window_set_drag_finished_callback(window, capture_drag_finished, &finished);
    window->drag_active = true;
    window->drag_notified = false;
    window->drag_result = pt_drag_action_none;
    pt_window_drag_finish_notify(window, true, pt_drag_action_move);
    pt_check(finished.count == 1);
    pt_check(finished.dropped);
    pt_check(finished.action == pt_drag_action_move);
    pt_check(pt_window_get_drag_result(window) == pt_drag_action_move);
    pt_check(!pt_window_get_drag_active(window));
    pt_window_drag_finish_notify(window, false, pt_drag_action_none);
    pt_check(finished.count == 1);
    pt_check(pt_window_get_drag_result(window) == pt_drag_action_move);

    window->drag_active = true;
    window->drag_notified = false;
    window->drag_result = pt_drag_action_none;
    pt_window_cancel_drag(window);
    pt_check(finished.count == 2);
    pt_check(!finished.dropped);
    pt_check(finished.action == pt_drag_action_none);
    pt_check(pt_window_get_drag_result(window) == pt_drag_action_none);
    pt_check(!pt_window_get_drag_active(window));
    pt_window_cancel_drag(window);
    pt_check(finished.count == 2);

    pt_window_set_drag_finished_callback(window, NULL, NULL);
    window->drag_active = true;
    window->drag_notified = false;
    pt_window_cancel_drag(window);
    pt_check(finished.count == 2);
    pt_check(!pt_window_get_drag_active(window));

    pt_window_free(window);
}

static void test_entry_source(pt_app *app) {
    pt_window *window = pt_window_create(app, "dnd entry", 420, 320);
    pt_check(window != NULL);
    pt_widget *root = pt_window_root(window);
    pt_widget *entry = pt_entry_create(root, "text");
    pt_check(entry != NULL);
    pt_widget_set_size(entry, 280.0f, 40.0f);
    pt_widget_set_text(entry, "hello world");
    pt_check(pt_entry_get_drag_enabled(entry));
    pt_entry_set_drag_enabled(entry, false);
    pt_check(!pt_entry_get_drag_enabled(entry));
    pt_entry_set_drag_enabled(entry, true);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 320, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));

    float ex = 0.0f;
    float ey = 0.0f;
    origin_of(entry, &ex, &ey);
    float cx = ex + 60.0f;
    float cy = ey + entry->height * 0.5f;

    pt_entry_select_all(entry);
    uint32_t start = 0;
    uint32_t end = 0;
    pt_check(pt_entry_get_selection(entry, &start, &end));
    pt_check(start == 0 && end == 11);

    char *selected = pt_entry_selection_text_at(entry, cx, cy);
    pt_check(selected != NULL && strcmp(selected, "hello world") == 0);
    free(selected);
    selected = pt_entry_selection_text_at(entry, ex - 40.0f, cy);
    pt_check(selected == NULL);

    pt_entry_set_password(entry, true);
    selected = pt_entry_selection_text_at(entry, cx, cy);
    pt_check(selected == NULL);
    pt_entry_set_password(entry, false);
    pt_entry_select_all(entry);

    pt_window_input_pointer_enter(window, cx, cy);
    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 99, 1010);
    pt_check(window->drag_pending);
    pt_check(window->drag_widget == entry);
    pt_check(window->drag_text != NULL && strcmp(window->drag_text, "hello world") == 0);
    pt_check(entry->selection_anchor == 0 && entry->cursor == 11);

    pt_window_input_pointer_motion(window, cx + 2.0f, cy, 1020);
    pt_check(window->drag_pending);
    pt_window_input_pointer_motion(window, cx + 24.0f, cy, 1030);
    pt_check(!window->drag_pending);
    pt_check(!window->drag_started);
    pt_check(window->drag_text == NULL);
    pt_check(entry->selecting);
    pt_window_input_pointer_button(window, 0x110u, false, cx + 24.0f, cy, 100, 1040);
    pt_check(!entry->selecting);
    pt_check(window->active_widget == NULL);

    pt_entry_select_all(entry);
    pt_window_input_pointer_enter(window, cx, cy);
    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 101, 1510);
    pt_check(window->drag_pending);
    pt_window_input_pointer_button(window, 0x110u, false, cx, cy, 102, 1520);
    pt_check(!window->drag_pending);
    pt_check(entry->selection_anchor == entry->cursor);

    pt_entry_set_drag_enabled(entry, false);
    pt_entry_select_all(entry);
    pt_window_input_pointer_enter(window, cx, cy);
    pt_window_input_pointer_button(window, 0x110u, true, cx, cy, 103, 2010);
    pt_check(!window->drag_pending);
    pt_check(entry->selection_anchor == entry->cursor);
    pt_window_input_pointer_button(window, 0x110u, false, cx, cy, 104, 2020);
    pt_entry_set_drag_enabled(entry, true);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_uri_list(pt_app *app) {
    pt_check(pt_uri_list_count(NULL) == 0);
    pt_check(pt_uri_list_get(NULL, 0) == NULL);
    const char *list = "# comment\r\nfile:///tmp/a.txt\r\n\r\nfile:///tmp/b.txt\n";
    pt_check(pt_uri_list_count(list) == 2);
    char *first = pt_uri_list_get(list, 0);
    char *second = pt_uri_list_get(list, 1);
    pt_check(first && strcmp(first, "file:///tmp/a.txt") == 0);
    pt_check(second && strcmp(second, "file:///tmp/b.txt") == 0);
    pt_check(pt_uri_list_get(list, 2) == NULL);
    pt_free(first);
    pt_free(second);

    pt_window *window = pt_window_create(app, "uri", 320, 200);
    /* Composing the CRLF-separated payload is the caller's job, so the check
     * that joins it and the check that reads it back meet in one place. */
    const char *joined = "file:///tmp/a.txt\r\nfile:///tmp/b.txt\r\n";
    const pt_drag_data uri_offer = {.mime_type = "text/uri-list", .data = joined, .size = 0};
    pt_check(pt_uri_list_count(joined) == 2);
    pt_check(!pt_window_begin_drag(NULL, &uri_offer, 1));
    /* Headless has no data-device global, so even a well-formed offer fails. */
    pt_check(!pt_window_begin_drag(window, &uri_offer, 1));

    pt_color pixels[4] = {0xFF0000FFu, 0x00FF00FFu, 0x0000FFFFu, 0xFFFFFFFFu};
    pt_window_set_drag_image(window, pixels, 2, 2);
    pt_check(window->drag_image != NULL);
    pt_check(window->drag_image_width == 2);
    pt_check(window->drag_image_height == 2);
    pt_check(window->drag_image[3] == 0xFFFFFFFFu);
    pixels[3] = 0x00000000u;
    pt_check(window->drag_image[3] == 0xFFFFFFFFu);
    pt_window_clear_drag_image(window);
    pt_check(window->drag_image == NULL);
    pt_check(window->drag_image_width == 0);
    pt_window_set_drag_image(window, pixels, -1, 2);
    pt_check(window->drag_image == NULL);
    pt_window_set_drag_image(window, pixels, 2, 2);
    pt_check(window->drag_image != NULL);
    pt_window_set_drag_image(window, NULL, 0, 0);
    pt_check(window->drag_image == NULL);

    /* Cancellation is safe and clears drag state; the watchdog expires in the loop. */
    pt_check(!pt_window_get_drag_active(window));
    pt_check(pt_window_get_drag_timeout(window) == 0);
    pt_window_set_drag_timeout(window, 250);
    pt_check(pt_window_get_drag_timeout(window) == 250);
    pt_window_cancel_drag(window);
    pt_check(!pt_window_get_drag_active(window));
    window->drag_active = true;
    window->drag_deadline = pt_now_ms() + 5000;
    pt_check(pt_window_get_drag_active(window));
    pt_window_set_drag_timeout(window, 0);
    pt_check(window->drag_deadline == 0);
    pt_window_cancel_drag(window);
    pt_check(!pt_window_get_drag_active(window));
    window->drag_active = true;
    window->drag_timeout = 1;
    window->drag_deadline = pt_now_ms() - 1;
    pt_check(pt_app_run_for(app, 5) == 0);
    pt_check(!pt_window_get_drag_active(window));

    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.dnd");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("dnd");
    }
    test_targets(app);
    test_structured_target(app);
    test_structured_streams(app);
    test_structured_lifetime(app);
    test_drag_source_api(app);
    test_entry_source(app);
    test_uri_list(app);
    pt_app_destroy(app);
    return pt_test_finish("dnd");
}
