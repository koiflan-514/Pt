/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_dnd_h
#define pt_dnd_h

#include <pt/widget.h>

/* Header contract
 *
 * Threading. UI thread only. Drop, offer and stream callbacks run on the UI
 * thread from the event loop.
 * Ownership. pt_drop_offer_mime, pt_drop_offer_selected_mime and
 * pt_drop_stream_mime return borrowed strings that stay valid for the callback
 * that inspected them, or until the stream ends. pt_drop_offer_read returns a
 * Pt-owned stream that must not be freed; it stays valid until its terminal
 * callback returns. pt_uri_list_get returns caller-owned memory released with
 * pt_free. Drag data, images and drop-target descriptions are copied; the
 * caller keeps ownership of the input.
 * Errors. pt_window_begin_drag returns false for NULL, count 0, a payload set
 * with no usable entry, no implicit grab, or a compositor rejection.
 * pt_drop_offer_read returns NULL for a finished offer, an unknown MIME type or
 * no read backend. A max_bytes overrun ends the stream with
 * pt_drop_stream_error. Setters are no-ops for NULL.
 * Lifetimes. An offer is valid during enter_offer and drop_offer only; streams
 * opened from it outlive the callback. Stream chunks are borrowed for the
 * duration of the callback and are not NUL-terminated, so concatenate them.
 * Multiple reads may run concurrently. pt_drop_stream_cancel is safe from the
 * stream's own callback and delivers pt_drop_stream_cancelled before returning.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* External drag and drop. Positions are logical pixels relative to the target
 * widget's top-left corner.
 *
 * The legacy callbacks below receive one text-like MIME type selected by Pt and
 * read it synchronously on drop. Structured targets use enter_offer/drop_offer
 * instead: they can inspect every advertised MIME type and stream dropped data
 * asynchronously, so a slow source never blocks the event loop. */
typedef bool (*pt_drop_enter_callback)(pt_widget *widget, const char *mime_type,
                                       void *user_data);
typedef void (*pt_drop_motion_callback)(pt_widget *widget, float x, float y, void *user_data);
typedef void (*pt_drop_leave_callback)(pt_widget *widget, void *user_data);
typedef void (*pt_drop_callback)(pt_widget *widget, const char *mime_type, const char *data,
                                 float x, float y, void *user_data);

/* Actions a drag source advertises. Values are bit flags so they can be
 * combined in an actions mask. */
typedef enum pt_drag_action {
    pt_drag_action_none = 0,
    pt_drag_action_copy = 1,
    pt_drag_action_move = 2,
    pt_drag_action_ask = 4,
} pt_drag_action;

typedef struct pt_drop_offer pt_drop_offer;
typedef struct pt_drop_stream pt_drop_stream;

typedef enum pt_drop_stream_state {
    pt_drop_stream_data = 0,
    pt_drop_stream_done,
    pt_drop_stream_cancelled,
    pt_drop_stream_error,
} pt_drop_stream_state;

/* Returns an offered MIME type to accept, or NULL to decline the drag. The
 * returned string must be one of pt_drop_offer_mime()'s values. */
typedef const char *(*pt_drop_offer_enter_callback)(pt_widget *widget,
                                                    pt_drop_offer *offer,
                                                    void *user_data);
/* Receives an accepted drop. Start one or more reads with pt_drop_offer_read;
 * an offer without active streams is released when this callback returns. */
typedef void (*pt_drop_offer_drop_callback)(pt_widget *widget, pt_drop_offer *offer,
                                            const char *mime_type, float x, float y,
                                            void *user_data);
/* Chunks arrive on the UI thread from the event loop. data is not
 * NUL-terminated; concatenate chunks for text formats. A stream is released
 * after its final done/cancelled/error callback returns. */
typedef void (*pt_drop_stream_callback)(pt_widget *widget, pt_drop_stream *stream,
                                        pt_drop_stream_state state, const char *data,
                                        size_t size, void *user_data);

typedef struct pt_drop_target {
    pt_drop_enter_callback enter;
    pt_drop_motion_callback motion;
    pt_drop_leave_callback leave;
    pt_drop_callback drop;
    void *user_data;
    /* Structured surface. When enter_offer is set it takes precedence over
     * enter; drop_offer takes precedence over drop. */
    pt_drop_offer_enter_callback enter_offer;
    pt_drop_offer_drop_callback drop_offer;
} pt_drop_target;

/* Copies the target description. Passing NULL removes it. Drop targets are
 * inherited by descendants: the deepest registered ancestor under the
 * pointer receives the callbacks. */
pt_api void pt_widget_set_drop_target(pt_widget *widget, const pt_drop_target *target);
pt_api bool pt_widget_get_drop_hover(const pt_widget *widget);

/* Incoming offer introspection. An offer is valid during enter_offer and
 * drop_offer; streams opened from it outlive the callback. */
pt_api uint32_t pt_drop_offer_mime_count(const pt_drop_offer *offer);
pt_api const char *pt_drop_offer_mime(const pt_drop_offer *offer, uint32_t index);
pt_api bool pt_drop_offer_has_mime(const pt_drop_offer *offer, const char *mime_type);
/* MIME type returned by enter_offer and used for the drop, or NULL. */
pt_api const char *pt_drop_offer_selected_mime(const pt_drop_offer *offer);
/* Bit mask of pt_drag_action values advertised by the source. */
pt_api uint32_t pt_drop_offer_actions(const pt_drop_offer *offer);

/* Starts an asynchronous read of one offered MIME type. Returns NULL when the
 * offer is finished, the MIME type is unknown, or no read backend exists.
 * max_bytes of zero means unlimited; exceeding it ends the stream with
 * pt_drop_stream_error. Multiple reads may run concurrently. */
pt_api pt_drop_stream *pt_drop_offer_read(pt_drop_offer *offer, const char *mime_type,
                                          size_t max_bytes,
                                          pt_drop_stream_callback callback,
                                          void *user_data);
/* Cancels an active stream, delivering pt_drop_stream_cancelled before the
 * callback returns. Safe to call from the stream's own callback. */
pt_api void pt_drop_stream_cancel(pt_drop_stream *stream);
pt_api const char *pt_drop_stream_mime(const pt_drop_stream *stream);
pt_api size_t pt_drop_stream_size(const pt_drop_stream *stream);
pt_api bool pt_drop_stream_active(const pt_drop_stream *stream);

/* Starts a structured drag offering several MIME types. Each entry carries a
 * payload; size zero means NUL-terminated data. Wayland requires a drag to
 * start inside an implicit grab, so this only succeeds while a pointer or touch
 * press is active. */
typedef struct pt_drag_data {
    const char *mime_type;
    const char *data;
    size_t size;
} pt_drag_data;

pt_api bool pt_window_begin_drag(pt_window *window, const pt_drag_data *types,
                                 uint32_t count);

/* Completion of a source drag. dropped is false when the drag was cancelled;
 * otherwise action reports what the destination performed. Called on the UI
 * thread exactly once per successful begin. */
typedef void (*pt_drag_finished_callback)(pt_window *window, bool dropped,
                                          pt_drag_action action, void *user_data);

pt_api void pt_window_set_drag_finished_callback(pt_window *window,
                                                 pt_drag_finished_callback callback,
                                                 void *user_data);
/* Action performed by the last finished drag, or pt_drag_action_none. */
pt_api pt_drag_action pt_window_get_drag_result(const pt_window *window);

/* Replaces the built-in drag thumbnail for subsequent drags with RGBA pixels
 * (0xRRGGBBAA, top row first). Pt copies the pixels. Passing a zero size or
 * clearing restores the built-in thumbnail. */
pt_api void pt_window_set_drag_image(pt_window *window, const pt_color *pixels, int width,
                                     int height);
pt_api void pt_window_clear_drag_image(pt_window *window);

/* True while a drag started by this window is awaiting a drop. */
pt_api bool pt_window_get_drag_active(const pt_window *window);

/* Cancels the active drag source, hiding its thumbnail and clearing any
 * pending entry drag. Safe to call when no drag is active. */
pt_api void pt_window_cancel_drag(pt_window *window);

/* Optional watchdog for source drags. A non-zero timeout cancels a drag that
 * has not finished within that many milliseconds; zero (the default) leaves
 * cancellation to the compositor. */
pt_api void pt_window_set_drag_timeout(pt_window *window, uint32_t milliseconds);
pt_api uint32_t pt_window_get_drag_timeout(const pt_window *window);

/* text/uri-list helpers for drop callbacks. Comments (#...) and blank lines
 * are ignored and CRLF is accepted. pt_uri_list_get returns a newly allocated
 * copy of the URI at index, freed with pt_free; out-of-range indices return
 * NULL. */
pt_api uint32_t pt_uri_list_count(const char *text);
pt_api char *pt_uri_list_get(const char *text, uint32_t index);

/* Entries support dragging the selected text once the pointer moves a few
 * pixels. Enabled by default. */
pt_api void pt_entry_set_drag_enabled(pt_widget *entry, bool enabled);
pt_api bool pt_entry_get_drag_enabled(const pt_widget *entry);

#ifdef __cplusplus
}
#endif

#endif
