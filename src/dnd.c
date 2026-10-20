/*
 * Pt drag-and-drop routing, drop offers and drag sources.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct pt_drop_stream {
    pt_app *app;
    pt_window *window;
    pt_drop_offer *offer;
    pt_widget *widget;
    pt_drop_stream_callback callback;
    void *user_data;
    char *mime_type;
    int fd;
    const char *memory;
    size_t memory_size;
    size_t memory_offset;
    uint32_t chunk_size;
    size_t total;
    size_t max_bytes;
    uint32_t pump_generation;
    bool finished;
    bool in_callback;
    bool cancel_requested;
    struct pt_drop_stream *next;
};

static pt_drop_stream *dnd_streams;
static uint32_t dnd_generation;

static void dnd_clear_hover(pt_window *window, bool notify);

/* ---------- offers ---------- */

static void offer_payloads_free(pt_drop_offer *offer) {
    pt_drop_payload *payload = offer->payloads;
    while (payload) {
        pt_drop_payload *next = payload->next;
        free(payload->mime_type);
        free(payload->data);
        free(payload);
        payload = next;
    }
    offer->payloads = NULL;
}

pt_drop_offer *pt_drop_offer_new(pt_app *app, pt_window *window, const char *const *mimes,
                                 uint32_t count) {
    pt_drop_offer *offer = pt_alloc_zeroed(1, sizeof(*offer));
    if (!offer) {
        return NULL;
    }
    offer->app = app;
    offer->window = window;
    offer->refs = 1;
    for (uint32_t i = 0; i < count; ++i) {
        pt_drop_offer_add_mime(offer, mimes[i]);
    }
    return offer;
}

void pt_drop_offer_retain(pt_drop_offer *offer) {
    if (offer) {
        ++offer->refs;
    }
}

void pt_drop_offer_release(pt_drop_offer *offer) {
    if (!offer || --offer->refs > 0) {
        return;
    }
    if (offer->dropped && !offer->finished) {
        offer->finished = true;
        if (offer->finish) {
            offer->finish(offer, offer->drop_accepted);
        }
    }
    if (offer->free_backend) {
        offer->free_backend(offer);
    }
    for (uint32_t i = 0; i < offer->mime_count; ++i) {
        free(offer->mimes[i]);
    }
    free(offer->mimes);
    free(offer->selected_mime);
    offer_payloads_free(offer);
    free(offer);
}

bool pt_drop_offer_has_mime(const pt_drop_offer *offer, const char *mime_type) {
    if (!offer || !mime_type) {
        return false;
    }
    for (uint32_t i = 0; i < offer->mime_count; ++i) {
        if (strcmp(offer->mimes[i], mime_type) == 0) {
            return true;
        }
    }
    return false;
}

void pt_drop_offer_add_mime(pt_drop_offer *offer, const char *mime_type) {
    if (!offer || !mime_type || !*mime_type || pt_drop_offer_has_mime(offer, mime_type)) {
        return;
    }
    char **mimes = pt_realloc_mem(offer->mimes, (offer->mime_count + 1u) * sizeof(*mimes));
    if (!mimes) {
        return;
    }
    offer->mimes = mimes;
    offer->mimes[offer->mime_count] = pt_strdup(mime_type);
    if (offer->mimes[offer->mime_count]) {
        ++offer->mime_count;
    }
}

uint32_t pt_drop_offer_mime_count(const pt_drop_offer *offer) {
    return offer ? offer->mime_count : 0;
}

const char *pt_drop_offer_mime(const pt_drop_offer *offer, uint32_t index) {
    if (!offer || index >= offer->mime_count) {
        return NULL;
    }
    return offer->mimes[index];
}

const char *pt_drop_offer_selected_mime(const pt_drop_offer *offer) {
    return offer ? offer->selected_mime : NULL;
}

uint32_t pt_drop_offer_actions(const pt_drop_offer *offer) {
    return offer ? offer->actions : 0;
}

void pt_drop_offer_add_payload(pt_drop_offer *offer, const char *mime_type, const void *data,
                               size_t size, uint32_t chunk_size) {
    if (!offer || !mime_type || !*mime_type || (!data && size > 0)) {
        return;
    }
    if (data && size == 0) {
        size = strlen((const char *)data);
    }
    pt_drop_payload *payload = offer->payloads;
    while (payload && strcmp(payload->mime_type, mime_type) != 0) {
        payload = payload->next;
    }
    if (!payload) {
        payload = pt_alloc_zeroed(1, sizeof(*payload));
        if (!payload) {
            return;
        }
        payload->mime_type = pt_strdup(mime_type);
        if (!payload->mime_type) {
            free(payload);
            return;
        }
        payload->next = offer->payloads;
        offer->payloads = payload;
    }
    char *copy = malloc(size + 1u);
    if (!copy) {
        return;
    }
    if (size > 0) {
        memcpy(copy, data, size);
    }
    copy[size] = '\0';
    free(payload->data);
    payload->data = copy;
    payload->size = size;
    payload->chunk_size = chunk_size;
}

static const pt_drop_payload *offer_find_payload(const pt_drop_offer *offer,
                                                 const char *mime_type) {
    for (const pt_drop_payload *payload = offer ? offer->payloads : NULL; payload;
         payload = payload->next) {
        if (strcmp(payload->mime_type, mime_type) == 0) {
            return payload;
        }
    }
    return NULL;
}

static char *offer_payload_copy(const pt_drop_offer *offer, const char *mime_type) {
    const pt_drop_payload *payload = offer_find_payload(offer, mime_type);
    if (!payload) {
        return NULL;
    }
    char *copy = malloc(payload->size + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, payload->data, payload->size);
    copy[payload->size] = '\0';
    return copy;
}

static char *offer_read_text_sync(pt_drop_offer *offer, const char *mime_type) {
    char *data = NULL;
    if (offer->read_sync) {
        data = offer->read_sync(offer, mime_type, pt_dnd_timeout_ms);
    }
    if (!data) {
        data = offer_payload_copy(offer, mime_type);
    }
    return data;
}

static const char *offer_preferred_text_mime(const pt_drop_offer *offer) {
    static const char *preferred[] = {
        "text/plain;charset=utf-8", "text/plain;charset=UTF-8", "text/plain;charset=utf8",
        "UTF8_STRING", "text/plain", "STRING",
    };
    if (!offer) {
        return NULL;
    }
    for (size_t p = 0; p < pt_len(preferred); ++p) {
        if (pt_drop_offer_has_mime(offer, preferred[p])) {
            return preferred[p];
        }
    }
    for (uint32_t i = 0; i < offer->mime_count; ++i) {
        if (strncmp(offer->mimes[i], "text/plain", 10) == 0) {
            return offer->mimes[i];
        }
    }
    return NULL;
}

/* ---------- streams ---------- */

static void stream_unlink(pt_drop_stream *stream) {
    pt_drop_stream **link = &dnd_streams;
    while (*link) {
        if (*link == stream) {
            *link = stream->next;
            return;
        }
        link = &(*link)->next;
    }
}

static void stream_release(pt_drop_stream *stream) {
    if (stream->fd >= 0) {
        close(stream->fd);
    }
    pt_drop_offer_release(stream->offer);
    free(stream->mime_type);
    free(stream);
}

static void stream_deliver(pt_drop_stream *stream, const char *data, size_t size) {
    stream->total += size;
    if (!stream->callback) {
        return;
    }
    stream->in_callback = true;
    stream->callback(stream->widget, stream, pt_drop_stream_data, data, size,
                     stream->user_data);
    stream->in_callback = false;
}

static void stream_finish(pt_drop_stream *stream, pt_drop_stream_state state) {
    if (stream->finished) {
        return;
    }
    stream->finished = true;
    stream_unlink(stream);
    if (stream->callback) {
        stream->in_callback = true;
        stream->callback(stream->widget, stream, state, NULL, 0, stream->user_data);
        stream->in_callback = false;
    }
    stream_release(stream);
}

pt_drop_stream *pt_drop_offer_read(pt_drop_offer *offer, const char *mime_type,
                                   size_t max_bytes, pt_drop_stream_callback callback,
                                   void *user_data) {
    if (!offer || !mime_type || !callback || !offer->dropped || offer->finished ||
        !pt_drop_offer_has_mime(offer, mime_type)) {
        return NULL;
    }
    const pt_drop_payload *payload = offer_find_payload(offer, mime_type);
    if (!payload && !offer->start_read) {
        return NULL;
    }
    pt_drop_stream *stream = pt_alloc_zeroed(1, sizeof(*stream));
    if (!stream) {
        return NULL;
    }
    stream->app = offer->app;
    stream->window = offer->window;
    stream->offer = offer;
    stream->widget = offer->target_widget;
    stream->callback = callback;
    stream->user_data = user_data;
    stream->mime_type = pt_strdup(mime_type);
    stream->fd = -1;
    stream->max_bytes = max_bytes;
    stream->pump_generation = dnd_generation;
    if (payload) {
        stream->memory = payload->data;
        stream->memory_size = payload->size;
        stream->chunk_size = payload->chunk_size ? payload->chunk_size : 4096u;
    } else {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) < 0) {
            free(stream->mime_type);
            free(stream);
            return NULL;
        }
        int flags = fcntl(fds[0], F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
        }
        bool started = offer->start_read(offer, mime_type, fds[1]);
        close(fds[1]);
        if (!started) {
            close(fds[0]);
            free(stream->mime_type);
            free(stream);
            return NULL;
        }
        stream->fd = fds[0];
    }
    pt_drop_offer_retain(offer);
    stream->next = dnd_streams;
    dnd_streams = stream;
    return stream;
}

void pt_drop_stream_cancel(pt_drop_stream *stream) {
    if (!stream || stream->finished) {
        return;
    }
    if (stream->in_callback) {
        stream->cancel_requested = true;
        return;
    }
    stream_finish(stream, pt_drop_stream_cancelled);
}

const char *pt_drop_stream_mime(const pt_drop_stream *stream) {
    return stream ? stream->mime_type : NULL;
}

size_t pt_drop_stream_size(const pt_drop_stream *stream) {
    return stream ? stream->total : 0;
}

bool pt_drop_stream_active(const pt_drop_stream *stream) {
    return stream && !stream->finished;
}

static void stream_pump(pt_drop_stream *stream) {
    if (stream->memory) {
        if (stream->memory_offset < stream->memory_size) {
            size_t remaining = stream->memory_size - stream->memory_offset;
            size_t size = stream->chunk_size < remaining ? stream->chunk_size : remaining;
            if (stream->max_bytes && stream->total + size > stream->max_bytes) {
                stream_finish(stream, pt_drop_stream_error);
                return;
            }
            const char *chunk = stream->memory + stream->memory_offset;
            stream->memory_offset += size;
            stream_deliver(stream, chunk, size);
            if (stream->cancel_requested) {
                stream_finish(stream, pt_drop_stream_cancelled);
                return;
            }
        }
        if (stream->memory_offset >= stream->memory_size) {
            stream_finish(stream, pt_drop_stream_done);
        }
        return;
    }
    char buffer[8192];
    ssize_t got = read(stream->fd, buffer, sizeof(buffer));
    if (got > 0) {
        if (stream->max_bytes && stream->total + (size_t)got > stream->max_bytes) {
            stream_finish(stream, pt_drop_stream_error);
            return;
        }
        stream_deliver(stream, buffer, (size_t)got);
        if (stream->cancel_requested) {
            stream_finish(stream, pt_drop_stream_cancelled);
        }
        return;
    }
    if (got == 0) {
        stream_finish(stream, pt_drop_stream_done);
        return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return;
    }
    stream_finish(stream, pt_drop_stream_error);
}

void pt_dnd_pump(pt_app *app) {
    if (++dnd_generation == 0) {
        ++dnd_generation;
    }
    for (;;) {
        pt_drop_stream *stream = NULL;
        for (pt_drop_stream *cursor = dnd_streams; cursor; cursor = cursor->next) {
            if (cursor->pump_generation != dnd_generation && (!app || cursor->app == app)) {
                stream = cursor;
                break;
            }
        }
        if (!stream) {
            return;
        }
        stream->pump_generation = dnd_generation;
        if (!stream->finished) {
            stream_pump(stream);
        }
    }
}

bool pt_dnd_pending(const pt_app *app) {
    for (const pt_drop_stream *stream = dnd_streams; stream; stream = stream->next) {
        if (!app || stream->app == app) {
            return true;
        }
    }
    return false;
}

static void cancel_matching_streams(pt_widget *widget, pt_window *window) {
    for (;;) {
        pt_drop_stream *stream = NULL;
        for (pt_drop_stream *cursor = dnd_streams; cursor; cursor = cursor->next) {
            bool matches = (!widget || cursor->widget == widget) &&
                           (!window || cursor->window == window);
            if (!matches) {
                continue;
            }
            if (cursor->in_callback) {
                cursor->cancel_requested = true;
                continue;
            }
            stream = cursor;
            break;
        }
        if (!stream) {
            return;
        }
        stream_finish(stream, pt_drop_stream_cancelled);
    }
}

void pt_dnd_widget_destroyed(pt_widget *widget) {
    if (!widget) {
        return;
    }
    pt_window *window = widget->window;
    if (window && window->drop_hover == widget) {
        window->drop_hover = NULL;
        widget->drop_hover = false;
    }
    cancel_matching_streams(widget, NULL);
}

void pt_dnd_window_destroyed(pt_window *window) {
    if (!window) {
        return;
    }
    if (window->drop_hover) {
        dnd_clear_hover(window, true);
    }
    if (window->drop_offer) {
        pt_drop_offer_release(window->drop_offer);
        window->drop_offer = NULL;
    }
    cancel_matching_streams(NULL, window);
}

/* ---------- routing ---------- */

static void widget_origin(const pt_window *window, const pt_widget *widget, float *x, float *y) {
    float ox = widget ? widget->x : 0.0f;
    float oy = widget ? widget->y : 0.0f;
    const pt_widget *top = widget;
    while (top && top->parent) {
        top = top->parent;
    }
    if (window && top) {
        for (const pt_overlay *overlay = window->overlays; overlay; overlay = overlay->next) {
            if (overlay->open && overlay->native && overlay->panel == top) {
                ox += overlay->rect.x;
                oy += overlay->rect.y;
                break;
            }
        }
    }
    *x = ox;
    *y = oy;
}

static pt_widget *dnd_target_at(pt_window *window, float x, float y) {
    pt_widget *hit = NULL;
    if (pt_window_overlays_active(window)) {
        hit = pt_window_overlay_hit_test(window, x, y);
    } else {
        hit = pt_widget_hit_test(window->root, x, y);
    }
    for (pt_widget *cursor = hit; cursor; cursor = cursor->parent) {
        if ((cursor->drop_target.enter || cursor->drop_target.enter_offer) &&
            cursor->enabled && cursor->visible) {
            return cursor;
        }
    }
    return NULL;
}

static void dnd_clear_hover(pt_window *window, bool notify) {
    pt_widget *target = window->drop_hover;
    if (!target) {
        return;
    }
    window->drop_hover = NULL;
    target->drop_hover = false;
    pt_widget_damage(target, 4.0f);
    if (notify && target->drop_target.leave) {
        target->drop_target.leave(target, target->drop_target.user_data);
    }
}

static bool dnd_accept_offer(pt_window *window, pt_widget *target, pt_drop_offer *offer) {
    if (!target) {
        dnd_clear_hover(window, true);
        return false;
    }
    if (window->drop_hover == target && window->drop_offer == offer) {
        return true;
    }
    dnd_clear_hover(window, true);
    const pt_drop_target *drop = &target->drop_target;
    const char *selected = NULL;
    if (drop->enter_offer) {
        const char *choice = drop->enter_offer(target, offer, drop->user_data);
        if (!choice || !pt_drop_offer_has_mime(offer, choice)) {
            return false;
        }
        selected = choice;
    } else if (drop->enter) {
        const char *mime = offer_preferred_text_mime(offer);
        if (!mime || !drop->enter(target, mime, drop->user_data)) {
            return false;
        }
        selected = mime;
    } else {
        return false;
    }
    free(offer->selected_mime);
    offer->selected_mime = pt_strdup(selected);
    if (!offer->selected_mime) {
        return false;
    }
    window->drop_hover = target;
    target->drop_hover = true;
    pt_widget_damage(target, 4.0f);
    return true;
}

static bool dnd_route_offer(pt_window *window, pt_drop_offer *offer, float x, float y,
                            bool send_motion) {
    pt_widget *target = dnd_target_at(window, x, y);
    if (!dnd_accept_offer(window, target, offer)) {
        return false;
    }
    if (send_motion && window->drop_hover->drop_target.motion) {
        float ox = 0.0f;
        float oy = 0.0f;
        widget_origin(window, window->drop_hover, &ox, &oy);
        window->drop_hover->drop_target.motion(window->drop_hover, x - ox, y - oy,
                                               window->drop_hover->drop_target.user_data);
    }
    window->needs_draw = true;
    return true;
}

bool pt_window_dnd_offer_enter(pt_window *window, pt_drop_offer *offer, float x, float y) {
    if (!window || !offer) {
        return false;
    }
    if (window->drop_offer && window->drop_offer != offer) {
        pt_drop_offer_release(window->drop_offer);
        window->drop_offer = NULL;
    }
    bool accepted = dnd_route_offer(window, offer, x, y, false);
    if (accepted) {
        if (window->drop_offer != offer) {
            pt_drop_offer_retain(offer);
            window->drop_offer = offer;
        }
    } else if (window->drop_offer == offer) {
        window->drop_offer = NULL;
        pt_drop_offer_release(offer);
    }
    return accepted;
}

bool pt_window_dnd_offer_motion(pt_window *window, float x, float y) {
    if (!window || !window->drop_offer) {
        return false;
    }
    return dnd_route_offer(window, window->drop_offer, x, y, true);
}

bool pt_window_dnd_enter(pt_window *window, const char *mime_type, float x, float y) {
    if (!window || !mime_type) {
        return false;
    }
    pt_drop_offer *offer = pt_drop_offer_new(window->app, window, &mime_type, 1);
    if (!offer) {
        return false;
    }
    bool accepted = pt_window_dnd_offer_enter(window, offer, x, y);
    pt_drop_offer_release(offer);
    return accepted;
}

bool pt_window_dnd_motion(pt_window *window, const char *mime_type, float x, float y) {
    if (!window || !mime_type) {
        return false;
    }
    if (!window->drop_offer && !pt_window_dnd_enter(window, mime_type, x, y)) {
        return false;
    }
    return pt_window_dnd_offer_motion(window, x, y);
}

void pt_window_dnd_leave(pt_window *window) {
    if (!window) {
        return;
    }
    dnd_clear_hover(window, true);
    if (window->drop_offer) {
        pt_drop_offer_release(window->drop_offer);
        window->drop_offer = NULL;
    }
    window->needs_draw = true;
}

void pt_window_dnd_cancel(pt_window *window) {
    pt_window_dnd_leave(window);
}

bool pt_window_dnd_offer_drop(pt_window *window, float x, float y) {
    if (!window || !window->drop_offer) {
        return false;
    }
    pt_drop_offer *offer = window->drop_offer;
    if (!dnd_route_offer(window, offer, x, y, false)) {
        pt_drop_offer_release(offer);
        window->drop_offer = NULL;
        window->needs_draw = true;
        return false;
    }
    pt_widget *target = window->drop_hover;
    float ox = 0.0f;
    float oy = 0.0f;
    widget_origin(window, target, &ox, &oy);
    const pt_drop_target *drop = &target->drop_target;
    void *user_data = drop->user_data;
    const char *mime = offer->selected_mime;
    dnd_clear_hover(window, false);
    window->drop_offer = NULL;
    offer->dropped = true;
    offer->drop_accepted = true;
    offer->target_widget = target;
    if (drop->drop_offer && mime) {
        drop->drop_offer(target, offer, mime, x - ox, y - oy, user_data);
    } else if (drop->drop && mime) {
        char *data = offer_read_text_sync(offer, mime);
        drop->drop(target, mime, data ? data : "", x - ox, y - oy, user_data);
        free(data);
    }
    window->needs_draw = true;
    pt_drop_offer_release(offer);
    return true;
}

bool pt_window_dnd_drop(pt_window *window, const char *mime_type, const char *data, float x,
                        float y) {
    if (!window || !mime_type) {
        return false;
    }
    if (window->drop_offer && !pt_drop_offer_has_mime(window->drop_offer, mime_type)) {
        pt_window_dnd_leave(window);
    }
    if (!window->drop_offer && !pt_window_dnd_enter(window, mime_type, x, y)) {
        return false;
    }
    pt_drop_offer_add_payload(window->drop_offer, mime_type, data ? data : "", 0, 0);
    return pt_window_dnd_offer_drop(window, x, y);
}

void pt_widget_set_drop_target(pt_widget *widget, const pt_drop_target *target) {
    if (!widget) {
        return;
    }
    if (target) {
        widget->drop_target = *target;
        return;
    }
    bool was_hover = widget->drop_hover;
    memset(&widget->drop_target, 0, sizeof(widget->drop_target));
    widget->drop_hover = false;
    if (was_hover) {
        pt_window *window = pt_widget_window(widget);
        if (window && window->drop_hover == widget) {
            window->drop_hover = NULL;
        }
        pt_widget_damage(widget, 3.0f);
    }
}

bool pt_widget_get_drop_hover(const pt_widget *widget) {
    return widget ? widget->drop_hover : false;
}

/* ---------- drag sources ---------- */

void pt_window_drag_finish_notify(pt_window *window, bool dropped, pt_drag_action action) {
    if (!window || window->drag_notified) {
        return;
    }
    window->drag_active = false;
    window->drag_deadline = 0;
    window->drag_result = dropped ? action : pt_drag_action_none;
    window->drag_notified = true;
    if (window->drag_finished) {
        window->drag_finished(window, dropped, window->drag_result,
                              window->drag_finished_data);
    }
}

void pt_window_drag_finished(pt_window *window) {
    pt_window_drag_finish_notify(window, false, pt_drag_action_none);
}

bool pt_window_drag_source_begin_types(pt_window *window, const pt_drag_data *types,
                                       uint32_t count) {
    if (!window || !types || count == 0 ||
        !pt_platform_begin_drag_types(window, types, count)) {
        return false;
    }
    window->drag_active = true;
    window->drag_notified = false;
    window->drag_result = pt_drag_action_none;
    window->drag_deadline =
        window->drag_timeout ? pt_now_ms() + (int64_t)window->drag_timeout : 0;
    return true;
}

bool pt_window_drag_source_begin(pt_window *window, const char *mime_type,
                                 const char *payload) {
    if (!window || !mime_type || !payload) {
        return false;
    }
    pt_drag_data data = {.mime_type = mime_type, .data = payload, .size = 0};
    return pt_window_drag_source_begin_types(window, &data, 1);
}

bool pt_window_begin_drag(pt_window *window, const pt_drag_data *types, uint32_t count) {
    if (!window || !types || count == 0) {
        return false;
    }
    uint32_t valid = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (types[i].mime_type && *types[i].mime_type && types[i].data) {
            ++valid;
        }
    }
    if (valid == 0) {
        return false;
    }
    return pt_window_drag_source_begin_types(window, types, count);
}

void pt_window_set_drag_finished_callback(pt_window *window,
                                          pt_drag_finished_callback callback,
                                          void *user_data) {
    if (!window) {
        return;
    }
    window->drag_finished = callback;
    window->drag_finished_data = user_data;
}

pt_drag_action pt_window_get_drag_result(const pt_window *window) {
    return window ? window->drag_result : pt_drag_action_none;
}

bool pt_window_get_drag_active(const pt_window *window) {
    return window && window->drag_active;
}

void pt_window_cancel_drag(pt_window *window) {
    if (!window) {
        return;
    }
    bool was_active = window->drag_active;
    if (was_active) {
        pt_platform_cancel_drag(window);
    }
    if (was_active && !window->drag_notified) {
        pt_window_drag_finish_notify(window, false, pt_drag_action_none);
    }
    window->drag_active = false;
    window->drag_deadline = 0;
    window->drag_pending = false;
    window->drag_started = false;
    window->drag_widget = NULL;
    pt_window_dnd_cancel(window);
    window->needs_draw = true;
    pt_platform_window_schedule_draw(window);
}

void pt_window_set_drag_timeout(pt_window *window, uint32_t milliseconds) {
    if (!window) {
        return;
    }
    window->drag_timeout = milliseconds;
    if (window->drag_active) {
        window->drag_deadline =
            milliseconds ? pt_now_ms() + (int64_t)milliseconds : 0;
    }
}

uint32_t pt_window_get_drag_timeout(const pt_window *window) {
    return window ? window->drag_timeout : 0;
}

static bool uri_list_next(const char **cursor, const char **start, size_t *length) {
    const char *position = *cursor;
    while (*position) {
        const char *line = position;
        const char *newline = strchr(position, '\n');
        size_t size = newline ? (size_t)(newline - line) : strlen(line);
        position = newline ? newline + 1 : line + size;
        if (size > 0 && line[size - 1] == '\r') {
            --size;
        }
        if (size == 0 || line[0] == '#') {
            continue;
        }
        *cursor = position;
        *start = line;
        *length = size;
        return true;
    }
    *cursor = position;
    return false;
}

uint32_t pt_uri_list_count(const char *text) {
    if (!text) {
        return 0;
    }
    uint32_t count = 0;
    const char *cursor = text;
    const char *start = NULL;
    size_t length = 0;
    while (uri_list_next(&cursor, &start, &length)) {
        ++count;
    }
    return count;
}

char *pt_uri_list_get(const char *text, uint32_t index) {
    if (!text) {
        return NULL;
    }
    const char *cursor = text;
    const char *start = NULL;
    size_t length = 0;
    for (uint32_t i = 0; uri_list_next(&cursor, &start, &length); ++i) {
        if (i != index) {
            continue;
        }
        char *uri = malloc(length + 1u);
        if (!uri) {
            return NULL;
        }
        memcpy(uri, start, length);
        uri[length] = '\0';
        return uri;
    }
    return NULL;
}

void pt_window_set_drag_image(pt_window *window, const pt_color *pixels, int width, int height) {
    if (!window) {
        return;
    }
    if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        pt_window_clear_drag_image(window);
        return;
    }
    size_t count = (size_t)width * (size_t)height;
    pt_color *copy = malloc(count * sizeof(*copy));
    if (!copy) {
        return;
    }
    memcpy(copy, pixels, count * sizeof(*copy));
    free(window->drag_image);
    window->drag_image = copy;
    window->drag_image_width = width;
    window->drag_image_height = height;
}

void pt_window_clear_drag_image(pt_window *window) {
    if (!window) {
        return;
    }
    free(window->drag_image);
    window->drag_image = NULL;
    window->drag_image_width = 0;
    window->drag_image_height = 0;
}

void pt_entry_set_drag_enabled(pt_widget *entry, bool enabled) {
    if (!entry || entry->kind != pt_widget_entry) {
        return;
    }
    entry->drag_enabled = enabled;
}

bool pt_entry_get_drag_enabled(const pt_widget *entry) {
    return entry && entry->kind == pt_widget_entry ? entry->drag_enabled : false;
}
