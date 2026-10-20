/*
 * Pt hello world.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <stdio.h>

typedef struct hello_state {
    pt_app *app;
    pt_widget *title;
    int welcomes;
} hello_state;

static void welcome(hello_state *state) {
    char text[64];
    ++state->welcomes;
    snprintf(text, sizeof(text), "Hello from Pt, %d time%s!", state->welcomes,
             state->welcomes == 1 ? "" : "s");
    pt_widget_set_text(state->title, text);
}

static void on_welcome(pt_widget *button, void *data) {
    (void)button;
    welcome((hello_state *)data);
}

static void on_quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.hello");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Hello, Pt", 640, 440);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 32.0f, 32.0f, 32.0f, 32.0f);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_gap(card, 18.0f);

    pt_widget *eyebrow = pt_label_create(card, "WAYLAND NATIVE  ·  C23");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, 0x4C8DFFFF);

    pt_widget *heading = pt_label_create(card, "Hello, Pt");
    pt_widget_set_font_size(heading, 40.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *body = pt_label_create(
        card, "A small, dense and unreactive toolkit for Wayland desktops. "
              "This window is drawn entirely in software at fractional scale.");
    pt_widget_set_foreground(body, 0x9AA4B2FF);
    pt_widget_set_label_wrap(body, true);

    hello_state state = {.app = app, .title = body, .welcomes = 0};

    pt_widget *actions = pt_row_create(card);
    pt_widget_set_gap(actions, 10.0f);
    pt_widget *greet = pt_button_create(actions, "Greet");
    pt_button_set_on_click(greet, on_welcome, &state);
    pt_widget *quit = pt_button_create(actions, "Quit");
    pt_button_set_variant(quit, pt_button_secondary);
    pt_button_set_on_click(quit, on_quit, app);

    pt_widget *hint = pt_label_create(card, "Press Tab to move focus, Space to activate.");
    pt_widget_set_font_size(hint, 12.0f);
    pt_widget_set_foreground(hint, 0x6B7688FF);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
