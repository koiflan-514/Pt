/*
 * Pt interaction lab: external drag and drop, compositor gestures, long press
 * and kinetic scrolling.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <string.h>

typedef struct lab_state {
    pt_window *window;
    pt_widget *drop_zone;
    pt_widget *drop_label;
    pt_widget *offer_zone;
    pt_widget *offer_label;
    pt_widget *drag_status;
    pt_widget *gesture_status;
    pt_widget *momentum_status;
    char streamed[512];
    size_t streamed_length;
    int drops;
} lab_state;

static const char *phase_name(pt_gesture_phase phase) {
    switch (phase) {
    case pt_gesture_phase_begin:
        return "begin";
    case pt_gesture_phase_update:
        return "update";
    case pt_gesture_phase_end:
        return "end";
    case pt_gesture_phase_cancel:
        return "cancel";
    }
    return "?";
}

static bool on_drop_enter(pt_widget *widget, const char *mime_type, void *data) {
    (void)widget;
    lab_state *state = data;
    char text[160];
    snprintf(text, sizeof(text), "Release to drop %s",
             mime_type && *mime_type ? mime_type : "text");
    pt_widget_set_text(state->drop_label, text);
    pt_widget_set_border_color(state->drop_zone, pt_rgb(0x7AA2F7));
    return true;
}

static void on_drop_motion(pt_widget *widget, float x, float y, void *data) {
    (void)widget;
    (void)x;
    (void)y;
    lab_state *state = data;
    pt_widget_set_border_color(state->drop_zone, pt_rgb(0x9ECE6A));
}

static void on_drop_leave(pt_widget *widget, void *data) {
    (void)widget;
    lab_state *state = data;
    pt_widget_set_text(state->drop_label, "Drop text here");
    pt_widget_set_border_color(state->drop_zone, pt_rgb(0x2A3648));
}

static void on_drop(pt_widget *widget, const char *mime_type, const char *data, float x,
                    float y, void *user_data) {
    (void)widget;
    (void)mime_type;
    (void)x;
    (void)y;
    lab_state *state = user_data;
    char text[160];
    ++state->drops;
    snprintf(text, sizeof(text), "Received: %.96s", data ? data : "");
    pt_widget_set_text(state->drop_label, text);
    pt_widget_set_border_color(state->drop_zone, pt_rgb(0x2A3648));
    char notice[96];
    snprintf(notice, sizeof(notice), "Text dropped (%d)", state->drops);
    pt_window_show_toast(state->window, notice, pt_toast_success, 2000);
}

/* Structured drop target: inspect every advertised type, accept one, then read
 * it in chunks on the event loop instead of blocking the drop callback. */
static const char *on_offer_enter(pt_widget *widget, pt_drop_offer *offer, void *user_data) {
    (void)widget;
    lab_state *state = user_data;
    char text[192];
    snprintf(text, sizeof(text), "%u MIME types offered; release to stream them",
             pt_drop_offer_mime_count(offer));
    pt_widget_set_text(state->offer_label, text);
    pt_widget_set_border_color(state->offer_zone, pt_rgb(0x7AA2F7));
    if (pt_drop_offer_has_mime(offer, "application/json")) {
        return "application/json";
    }
    return pt_drop_offer_mime(offer, 0);
}

static void on_stream(pt_widget *widget, pt_drop_stream *stream, pt_drop_stream_state status,
                      const char *data, size_t size, void *user_data) {
    (void)widget;
    lab_state *state = user_data;
    switch (status) {
    case pt_drop_stream_data:
        if (data && size > 0) {
            size_t room = sizeof(state->streamed) - state->streamed_length - 1;
            size_t take = size < room ? size : room;
            memcpy(state->streamed + state->streamed_length, data, take);
            state->streamed_length += take;
            state->streamed[state->streamed_length] = '\0';
        }
        if (!pt_drop_stream_active(stream)) {
            char notice[128];
            snprintf(notice, sizeof(notice), "Read %zu bytes of %s", state->streamed_length,
                     pt_drop_stream_mime(stream) ? pt_drop_stream_mime(stream) : "data");
            pt_window_show_toast(state->window, notice, pt_toast_success, 2200);
        }
        break;
    case pt_drop_stream_done:
        pt_widget_set_text(state->offer_label, state->streamed[0] ? state->streamed
                                                                  : "Drop finished with no body");
        break;
    case pt_drop_stream_cancelled:
        pt_widget_set_text(state->offer_label, "Read cancelled by the source");
        break;
    case pt_drop_stream_error:
        pt_widget_set_text(state->offer_label, "Read failed; asked for a smaller payload");
        break;
    }
    if (status != pt_drop_stream_data) {
        pt_widget_set_border_color(state->offer_zone, pt_rgb(0x2A3648));
        state->streamed_length = 0;
        state->streamed[0] = '\0';
    }
}

static void on_offer_drop(pt_widget *widget, pt_drop_offer *offer, const char *mime_type, float x,
                          float y, void *user_data) {
    (void)widget;
    (void)x;
    (void)y;
    lab_state *state = user_data;
    state->streamed_length = 0;
    state->streamed[0] = '\0';
    /* max_bytes caps the read so a huge source cannot exhaust memory; the
     * stream then ends with pt_drop_stream_error. */
    if (pt_drop_offer_read(offer, mime_type, 4096, on_stream, state)) {
        pt_widget_set_text(state->offer_label, "Streaming…");
    } else {
        pt_widget_set_text(state->offer_label, "Could not open a reader for that type");
        pt_widget_set_border_color(state->offer_zone, pt_rgb(0x2A3648));
    }
}

static void on_drag_finished(pt_window *window, bool dropped, pt_drag_action action,
                             void *user_data) {
    (void)window;
    lab_state *state = user_data;
    const char *name = action == pt_drag_action_copy ? "copy"
                     : action == pt_drag_action_move ? "move"
                     : action == pt_drag_action_ask  ? "ask"
                                                     : "none";
    char text[128];
    snprintf(text, sizeof(text), dropped ? "Drop accepted with action %s"
                                         : "Drag cancelled (last action %s)",
             name);
    pt_widget_set_text(state->drag_status, text);
}

static void on_offer_source_click(pt_widget *button, void *user_data) {
    (void)button;
    lab_state *state = user_data;
    static const char *json = "{\"rows\":3,\"label\":\"interaction lab\"}";
    pt_drag_data types[] = {
        {.mime_type = "application/json", .data = json, .size = 0},
        {.mime_type = "text/plain", .data = "interaction lab", .size = 0},
        {.mime_type = "text/uri-list", .data = "file:///tmp/pt-example.json", .size = 0},
    };
    if (pt_window_begin_drag(state->window, types, 3)) {
        pt_widget_set_text(state->drag_status, "Dragging three MIME types out…");
    } else {
        pt_widget_set_text(state->drag_status,
                           "Drag needs an active pointer press; press the button again");
    }
}

static void on_gesture(pt_window *window, const pt_gesture_event *event, void *data) {
    (void)window;
    lab_state *state = data;
    char text[160];
    switch (event->kind) {
    case pt_gesture_kind_hold:
        snprintf(text, sizeof(text), "Hold %s - %u finger%s at %.0f, %.0f",
                 phase_name(event->phase), event->fingers,
                 event->fingers == 1 ? "" : "s", event->x, event->y);
        break;
    case pt_gesture_kind_swipe:
        snprintf(text, sizeof(text), "Swipe %s - dx %.0f, dy %.0f",
                 phase_name(event->phase), event->dx, event->dy);
        break;
    case pt_gesture_kind_pinch:
        snprintf(text, sizeof(text), "Pinch %s - scale %.2f, rotation %.2f rad",
                 phase_name(event->phase), event->scale, event->rotation);
        break;
    }
    pt_widget_set_text(state->gesture_status, text);
}

static void on_momentum(pt_widget *toggle, float value, void *data) {
    (void)toggle;
    lab_state *state = data;
    bool enabled = value > 0.5f;
    pt_window_set_momentum_scrolling(state->window, enabled);
    pt_widget_set_text(state->momentum_status,
                       enabled ? "Kinetic scrolling on" : "Kinetic scrolling off");
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.interaction");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt Interaction Lab", 720, 800);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    lab_state state = {.window = window};
    pt_window_set_gesture_callback(window, on_gesture, &state);
    pt_window_set_long_press_delay(window, 450);

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *title = pt_label_create(root, "Interaction lab");
    pt_widget_set_font_size(title, 28.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);
    pt_widget *subtitle = pt_label_create(
        root, "Drag text between applications, hold to open a context, or fling the list.");
    pt_widget_set_font_size(subtitle, 13.0f);
    pt_widget_set_foreground(subtitle, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(subtitle, true);

    pt_widget *cards = pt_row_create(root);
    pt_widget_set_gap(cards, 14.0f);

    pt_widget *dnd_card = pt_card_create(cards);
    pt_widget_set_grow(dnd_card, 1.0f);
    pt_widget_set_padding(dnd_card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(dnd_card, 10.0f);
    pt_widget *dnd_title = pt_label_create(dnd_card, "Drag and drop");
    pt_widget_set_font_size(dnd_title, 17.0f);
    pt_widget_set_font_weight(dnd_title, pt_font_weight_semibold);
    pt_widget *dnd_hint =
        pt_label_create(dnd_card, "Select the text below and drag it onto the drop zone.");
    pt_widget_set_font_size(dnd_hint, 12.0f);
    pt_widget_set_foreground(dnd_hint, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(dnd_hint, true);
    pt_widget *draggable =
        pt_entry_create(dnd_card, "Type text, select it, then drag");
    pt_widget_set_text(draggable, "Drag this sentence into another app");
    pt_entry_set_drag_enabled(draggable, true);
    pt_widget_set_tooltip(draggable, "Entries become drag sources once text is selected");

    state.drop_zone = pt_card_create(dnd_card);
    pt_widget_set_height(state.drop_zone, 132.0f);
    pt_widget_set_radius(state.drop_zone, 14.0f);
    pt_widget_set_background(state.drop_zone, pt_rgb(0x111823));
    pt_widget_set_border_width(state.drop_zone, 2.0f);
    pt_widget_set_border_color(state.drop_zone, pt_rgb(0x2A3648));
    pt_widget_set_padding(state.drop_zone, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(state.drop_zone, 6.0f);
    state.drop_label = pt_label_create(state.drop_zone, "Drop text here");
    pt_widget_set_font_size(state.drop_label, 16.0f);
    pt_widget_set_font_weight(state.drop_label, pt_font_weight_medium);
    pt_widget_set_label_wrap(state.drop_label, true);
    pt_widget *mime_hint = pt_label_create(state.drop_zone,
                                           "Accepts UTF-8 text from other Wayland clients.");
    pt_widget_set_font_size(mime_hint, 12.0f);
    pt_widget_set_foreground(mime_hint, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(mime_hint, true);
    pt_drop_target drop_target = {
        .enter = on_drop_enter,
        .motion = on_drop_motion,
        .leave = on_drop_leave,
        .drop = on_drop,
        .user_data = &state,
    };
    pt_widget_set_drop_target(state.drop_zone, &drop_target);
    pt_widget_set_a11y_name(state.drop_zone, "Text drop zone");
    pt_widget_set_a11y_description(state.drop_zone, "Accepts text dragged from any window");

    pt_widget *gesture_card = pt_card_create(cards);
    pt_widget_set_grow(gesture_card, 1.0f);
    pt_widget_set_padding(gesture_card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(gesture_card, 10.0f);
    pt_widget *gesture_title = pt_label_create(gesture_card, "Gestures");
    pt_widget_set_font_size(gesture_title, 17.0f);
    pt_widget_set_font_weight(gesture_title, pt_font_weight_semibold);
    pt_widget *gesture_hint = pt_label_create(
        gesture_card,
        "Swipe or pinch on a touchpad. Press and hold the button for a long press.");
    pt_widget_set_font_size(gesture_hint, 12.0f);
    pt_widget_set_foreground(gesture_hint, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(gesture_hint, true);
    pt_widget *hold = pt_button_create(gesture_card, "Press and hold");
    pt_button_set_variant(hold, pt_button_secondary);
    pt_widget_set_tooltip(hold, "Fires a hold gesture after 450 ms");
    state.gesture_status = pt_label_create(gesture_card, "No gesture yet");
    pt_widget_set_font_size(state.gesture_status, 13.0f);
    pt_widget_set_foreground(state.gesture_status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.gesture_status, true);

    pt_widget *momentum_row = pt_row_create(gesture_card);
    pt_widget_set_gap(momentum_row, 10.0f);
    pt_widget *momentum = pt_toggle_create(momentum_row);
    pt_widget_set_align_self(momentum, pt_align_center);
    pt_widget_set_checked(momentum, pt_window_get_momentum_scrolling(window));
    pt_toggle_set_on_change(momentum, on_momentum, &state);
    state.momentum_status = pt_label_create(momentum_row, "Kinetic scrolling on");
    pt_widget_set_align_self(state.momentum_status, pt_align_center);
    pt_widget_set_font_size(state.momentum_status, 13.0f);

    pt_widget *list_title = pt_label_create(gesture_card, "Fling this list");
    pt_widget_set_font_size(list_title, 13.0f);
    pt_widget_set_font_weight(list_title, pt_font_weight_medium);
    pt_widget *scroll = pt_scroll_create(gesture_card);
    pt_widget_set_height(scroll, 118.0f);
    pt_widget_set_background(scroll, pt_rgb(0x111823));
    pt_widget_set_radius(scroll, 10.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_gap(list, 0.0f);
    for (int index = 0; index < 24; ++index) {
        char row_text[64];
        snprintf(row_text, sizeof(row_text), "Momentum row %d", index + 1);
        pt_widget *row = pt_label_create(list, row_text);
        pt_widget_set_height(row, 26.0f);
        pt_widget_set_padding(row, 0.0f, 10.0f, 0.0f, 10.0f);
        pt_widget_set_align_self(row, pt_align_center);
        pt_divider_create(list);
    }

    pt_widget *structured_card = pt_card_create(root);
    pt_widget_set_padding(structured_card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(structured_card, 10.0f);
    pt_widget *structured_title = pt_label_create(structured_card, "Structured drag data");
    pt_widget_set_font_size(structured_title, 17.0f);
    pt_widget_set_font_weight(structured_title, pt_font_weight_semibold);
    pt_widget *structured_hint = pt_label_create(
        structured_card,
        "The button offers JSON, plain text and a URI in one drag. The zone on the right "
        "inspects every advertised type and streams the accepted one in chunks.");
    pt_widget_set_font_size(structured_hint, 12.0f);
    pt_widget_set_foreground(structured_hint, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(structured_hint, true);
    pt_widget *structured_row = pt_row_create(structured_card);
    pt_widget_set_gap(structured_row, 14.0f);
    pt_widget *offer_source = pt_button_create(structured_row, "Offer three types");
    pt_button_set_on_click(offer_source, on_offer_source_click, &state);
    pt_widget_set_tooltip(offer_source, "Start a multi-MIME drag from the current pointer press");
    state.drag_status = pt_label_create(structured_row, "No source drag yet");
    pt_widget_set_font_size(state.drag_status, 13.0f);
    pt_widget_set_foreground(state.drag_status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.drag_status, true);

    state.offer_zone = pt_card_create(structured_row);
    pt_widget_set_grow(state.offer_zone, 1.0f);
    pt_widget_set_height(state.offer_zone, 96.0f);
    pt_widget_set_radius(state.offer_zone, 14.0f);
    pt_widget_set_background(state.offer_zone, pt_rgb(0x111823));
    pt_widget_set_border_width(state.offer_zone, 2.0f);
    pt_widget_set_border_color(state.offer_zone, pt_rgb(0x2A3648));
    pt_widget_set_padding(state.offer_zone, 14.0f, 14.0f, 14.0f, 14.0f);
    pt_widget_set_gap(state.offer_zone, 6.0f);
    state.offer_label = pt_label_create(state.offer_zone, "Drop structured data here");
    pt_widget_set_font_size(state.offer_label, 14.0f);
    pt_widget_set_label_wrap(state.offer_label, true);
    pt_drop_target offer_target = {
        .motion = on_drop_motion,
        .leave = on_drop_leave,
        .user_data = &state,
        .enter_offer = on_offer_enter,
        .drop_offer = on_offer_drop,
    };
    pt_widget_set_drop_target(state.offer_zone, &offer_target);
    pt_widget_set_a11y_name(state.offer_zone, "Structured drop zone");
    pt_window_set_drag_finished_callback(window, on_drag_finished, &state);

    pt_widget *footer = pt_label_create(
        root,
        "Drop targets accept either a single MIME text payload or an offer they introspect and "
        "stream asynchronously; gestures normalize touch and touchpad input; long press is "
        "synthesized from pointer holds.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
