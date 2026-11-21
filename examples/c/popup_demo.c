/*
 * Pt popup demo: an application-owned popup surface with custom content,
 * runtime placement changes and a dismiss callback.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/popup.h):
 *
 *  - pt_popup_create adopts the content widget and its subtree. The content is
 *    built detached (pt_card_create(NULL)) and handed over exactly once; the
 *    application never uses or frees it afterwards.
 *  - The anchor is a rectangle in parent-window logical pixels. The placement
 *    is a preference: below/above/right/left flip to the opposite side and
 *    slide along the other axis when the preferred side does not fit.
 *  - The dismiss callback runs after the popup has closed, with
 *    pt_popup_is_open already false. It may re-show the popup, but it must not
 *    assume the popup is still open.
 *  - Escape, an outside click and pt_popup_dismiss all end through the same
 *    dismissal path and fire the callback exactly once.
 *  - Destroying the popup (or its parent window) does not fire the callback.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct popup_demo_state {
    pt_app *app;
    pt_window *window;
    pt_popup *popup;
    pt_widget *trigger;
    pt_widget *placement_button;
    pt_widget *status;
    pt_popup_anchor anchor;
    pt_popup_placement placement;
    unsigned dismissals;
    unsigned shows;
} popup_demo_state;

static const char *placement_name(pt_popup_placement placement) {
    switch (placement) {
    case pt_popup_placement_below:
        return "below";
    case pt_popup_placement_above:
        return "above";
    case pt_popup_placement_right:
        return "right";
    case pt_popup_placement_left:
        return "left";
    }
    return "?";
}

static void refresh_status(popup_demo_state *state, const char *what) {
    char text[224];
    snprintf(text, sizeof(text),
             "%s · placement %s · open %s · shown %u, dismissed %u", what,
             placement_name(state->placement),
             pt_popup_is_open(state->popup) ? "yes" : "no", state->shows,
             state->dismissals);
    pt_widget_set_text(state->status, text);
}

/* Runs after pt_popup_is_open() has already become false. The handle stays
 * valid, so we may query it or re-show it; this demo only logs. */
static void on_dismiss(pt_popup *popup, void *user_data) {
    popup_demo_state *state = user_data;
    ++state->dismissals;
    printf("popup_demo: dismiss #%u (is_open=%d)\n", state->dismissals,
           pt_popup_is_open(popup) ? 1 : 0);
    fflush(stdout);
    refresh_status(state, "Dismiss callback fired after the popup closed");
}

static void update_anchor(popup_demo_state *state) {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    pt_widget_get_bounds(state->trigger, &x, &y, &width, &height);
    state->anchor = (pt_popup_anchor){.x = x, .y = y, .width = width, .height = height};
}

static void open_popup(popup_demo_state *state) {
    update_anchor(state);
    if (pt_popup_show(state->popup, state->anchor)) {
        ++state->shows;
        refresh_status(state, "Popup opened with the current placement");
    } else {
        char text[192];
        snprintf(text, sizeof(text), "Could not open the popup: %s", pt_get_last_error());
        pt_widget_set_text(state->status, text);
    }
}

static void on_open(pt_widget *button, void *user_data) {
    (void)button;
    open_popup((popup_demo_state *)user_data);
}

static void on_toggle(pt_widget *button, void *user_data) {
    (void)button;
    popup_demo_state *state = user_data;
    if (pt_popup_is_open(state->popup)) {
        /* Dismissal is asynchronous through the framework, but the callback
         * still runs before this handler returns under the in-window fallback. */
        pt_popup_dismiss(state->popup);
        refresh_status(state, "pt_popup_dismiss called");
        return;
    }
    open_popup(state);
}

static void on_reposition(pt_widget *button, void *user_data) {
    (void)button;
    popup_demo_state *state = user_data;
    update_anchor(state);
    state->anchor.x += 24.0f;
    state->anchor.y += 18.0f;
    if (pt_popup_reposition(state->popup, state->anchor)) {
        refresh_status(state, "pt_popup_reposition moved the open popup");
    } else {
        refresh_status(state, "Popup is closed; reposition is a no-op");
    }
}

static void on_cycle_placement(pt_widget *button, void *user_data) {
    (void)button;
    popup_demo_state *state = user_data;
    switch (pt_popup_get_placement(state->popup)) {
    case pt_popup_placement_below:
        state->placement = pt_popup_placement_above;
        break;
    case pt_popup_placement_above:
        state->placement = pt_popup_placement_right;
        break;
    case pt_popup_placement_right:
        state->placement = pt_popup_placement_left;
        break;
    case pt_popup_placement_left:
        state->placement = pt_popup_placement_below;
        break;
    }
    pt_popup_set_placement(state->popup, state->placement);
    pt_widget_set_text(state->placement_button,
                       placement_name(state->placement));
    if (pt_popup_is_open(state->popup)) {
        update_anchor(state);
        pt_popup_reposition(state->popup, state->anchor);
    }
    refresh_status(state, "Placement preference changed");
}

static void on_popup_close(pt_widget *button, void *user_data) {
    (void)button;
    popup_demo_state *state = user_data;
    pt_popup_dismiss(state->popup);
    refresh_status(state, "Close button inside the popup");
}

static void on_popup_ping(pt_widget *button, void *user_data) {
    (void)button;
    popup_demo_state *state = user_data;
    refresh_status(state, "Ping from the popup content");
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.popup_demo");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt Popup Demo", 620, 460);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    popup_demo_state state = {
        .app = app,
        .window = window,
        .placement = pt_popup_placement_below,
        .anchor = {.width = 1.0f, .height = 1.0f},
    };

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "POPUP  ·  APPLICATION-OWNED SURFACE");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    pt_widget *heading = pt_label_create(root, "Custom content, real placement");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "The popup is a Pt surface built from ordinary widgets. The anchor is the trigger "
              "button rectangle; the placement flips and slides to stay inside the compositor "
              "work area. Escape, an outside click and Dismiss all run the same callback.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(card, 12.0f);

    pt_widget *actions = pt_row_create(card);
    pt_widget_set_gap(actions, 10.0f);
    pt_widget_set_align_self(actions, pt_align_center);
    state.trigger = pt_button_create(actions, "Open popup");
    pt_button_set_variant(state.trigger, pt_button_primary);
    pt_button_set_on_click(state.trigger, on_open, &state);
    pt_widget_set_tooltip(state.trigger, "Anchors the popup to this button");

    pt_widget *toggle = pt_button_create(actions, "Toggle");
    pt_button_set_variant(toggle, pt_button_secondary);
    pt_button_set_on_click(toggle, on_toggle, &state);

    pt_widget *reposition = pt_button_create(actions, "Reposition");
    pt_button_set_variant(reposition, pt_button_ghost);
    pt_button_set_on_click(reposition, on_reposition, &state);

    state.placement_button = pt_button_create(actions, placement_name(state.placement));
    pt_button_set_variant(state.placement_button, pt_button_soft);
    pt_button_set_on_click(state.placement_button, on_cycle_placement, &state);
    pt_widget_set_tooltip(state.placement_button, "Cycle below → above → right → left");

    /* Detached content: the popup adopts it and owns it from then on. */
    pt_widget *content = pt_card_create(NULL);
    pt_widget_set_padding(content, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(content, 10.0f);
    pt_widget_set_background(content, pt_rgb(0x161D28));
    pt_widget_set_border_width(content, 1.0f);
    pt_widget_set_border_color(content, pt_rgb(0x30415C));
    pt_widget_set_radius(content, 12.0f);
    pt_widget_set_min_width(content, 260.0f);
    pt_widget_set_a11y_name(content, "Popup content");

    pt_widget *content_title = pt_label_create(content, "Popup content");
    pt_widget_set_font_size(content_title, 16.0f);
    pt_widget_set_font_weight(content_title, pt_font_weight_semibold);
    pt_widget *content_body = pt_label_create(
        content, "This card lives in the popup tree. Escape or a click outside dismisses it and "
                 "fires the callback below.");
    pt_widget_set_font_size(content_body, 12.0f);
    pt_widget_set_foreground(content_body, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(content_body, true);

    pt_widget *content_actions = pt_row_create(content);
    pt_widget_set_gap(content_actions, 8.0f);
    pt_widget *ping = pt_button_create(content_actions, "Ping");
    pt_button_set_variant(ping, pt_button_secondary);
    pt_button_set_on_click(ping, on_popup_ping, &state);
    pt_widget *close = pt_button_create(content_actions, "Dismiss");
    pt_button_set_variant(close, pt_button_ghost);
    pt_button_set_on_click(close, on_popup_close, &state);

    state.popup = pt_popup_create(window, content);
    if (!state.popup) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }
    pt_popup_set_on_dismiss(state.popup, on_dismiss, &state);
    pt_popup_set_placement(state.popup, state.placement);

    state.status = pt_label_create(root, "");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);
    refresh_status(&state, "Ready; the popup starts closed");

    pt_widget *footer = pt_label_create(
        root, "pt_popup_content() returns a borrowed pointer valid until the popup is destroyed. "
              "Destroying the popup or its window does not fire the dismiss callback.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(state.trigger);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        on_open(NULL, &state);
        printf("popup_demo: open=%d placement=%s content=%p\n",
               pt_popup_is_open(state.popup) ? 1 : 0,
               placement_name(pt_popup_get_placement(state.popup)),
               (void *)pt_popup_content(state.popup));
        on_cycle_placement(NULL, &state);
        on_reposition(NULL, &state);
        on_popup_ping(NULL, &state);
        on_toggle(NULL, &state);
        on_open(NULL, &state);
        pt_popup_dismiss(state.popup);
        printf("popup_demo: after dismiss open=%d dismissals=%u\n",
               pt_popup_is_open(state.popup) ? 1 : 0, state.dismissals);
        fflush(stdout);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    /* Still safe while the window is alive; this does not fire the callback. */
    pt_popup_destroy(state.popup);
    pt_app_destroy(app);
    return status;
}
