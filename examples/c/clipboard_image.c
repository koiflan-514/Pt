/*
 * Pt clipboard images: snapshot the window offscreen, publish it as image/png
 * on the clipboard, read it back and paint the decoded pixels, then write a
 * text/uri-list offer.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/window.h):
 *
 *  - pt_window_paint_pixels() produces premultiplied 0xAARRGGBB pixels with a
 *    stride of width, the same layout the image clipboard setter accepts.
 *  - pt_window_clipboard_set_image_png() copies the pixels, converts to
 *    straight-alpha RGBA and writes a PNG. The getter returns caller-owned
 *    premultiplied pixels and sets the width/height out parameters.
 *  - A successful set of any clipboard type replaces the previous one, so the
 *    uri-list write below makes the image and text getters return NULL.
 *  - Headless and self-owned selections use the in-process fallback, so this
 *    example round-trips without a compositor.
 *  - Decoding rejects corrupt data, oversized dimensions and decompression
 *    bombs without crashing; the preview repaints only from stored state.
 */

#include <pt/pt.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct clipboard_state {
    pt_app *app;
    pt_window *window;
    pt_widget *preview;
    pt_widget *status;
    pt_widget *info;
    pt_color *snapshot;
    int snapshot_width;
    int snapshot_height;
    pt_color *readback;
    int readback_width;
    int readback_height;
    unsigned sets;
    unsigned reads;
} clipboard_state;

static void free_pixels(pt_color **pixels) {
    if (*pixels) {
        pt_free(*pixels);
        *pixels = NULL;
    }
}

static uint32_t pixel_hash(const pt_color *pixels, size_t count) {
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < count; ++index) {
        hash ^= pixels[index];
        hash *= 16777619u;
    }
    return hash;
}

/* Pure paint callback: reads the stored readback buffer and the widget size,
 * never mutates state. The image is sampled in small blocks so a 700x500
 * snapshot does not turn into 350000 paint calls per frame. */
static void paint_preview(pt_widget *widget, pt_paint_context *context, void *user_data) {
    (void)widget;
    clipboard_state *state = user_data;
    if (!context) {
        return;
    }
    float width = pt_paint_width(context);
    float height = pt_paint_height(context);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float cell = 12.0f;
    for (float y = 0.0f; y < height; y += cell) {
        for (float x = 0.0f; x < width; x += cell) {
            bool dark = ((int)(x / cell) + (int)(y / cell)) % 2 == 0;
            pt_paint_fill_rect(context, x, y, fminf(cell, width - x), fminf(cell, height - y),
                               dark ? pt_rgb(0x161D28) : pt_rgb(0x1E2836));
        }
    }

    const pt_color *pixels = state->readback ? state->readback : state->snapshot;
    int image_width = state->readback ? state->readback_width : state->snapshot_width;
    int image_height = state->readback ? state->readback_height : state->snapshot_height;
    if (!pixels || image_width <= 0 || image_height <= 0) {
        pt_paint_draw_text(context, "sans-serif", 13.0f, pt_font_weight_regular,
                           pt_font_slant_upright, 14.0f, 26.0f,
                           "No image yet: press Snapshot", strlen("No image yet: press Snapshot"),
                           pt_rgb(0x6F7C8F));
        return;
    }

    float scale = fminf(width / (float)image_width, height / (float)image_height);
    float draw_width = (float)image_width * scale;
    float draw_height = (float)image_height * scale;
    float left = (width - draw_width) * 0.5f;
    float top = (height - draw_height) * 0.5f;

    int step = (int)((float)image_width / 90.0f);
    if (step < 1) {
        step = 1;
    }
    for (int sy = 0; sy < image_height; sy += step) {
        for (int sx = 0; sx < image_width; sx += step) {
            pt_color pixel = pixels[(size_t)sy * (size_t)image_width + (size_t)sx];
            float block_width = (float)step * scale + 0.5f;
            float block_height = (float)step * scale + 0.5f;
            pt_paint_fill_rect(context, left + (float)sx * scale, top + (float)sy * scale,
                               block_width, block_height, pixel);
        }
    }
    pt_paint_stroke_rect(context, left - 0.5f, top - 0.5f, draw_width + 1.0f,
                         draw_height + 1.0f, 1.0f, pt_rgb(0x30415C));
}

static void update_info(clipboard_state *state, const char *what) {
    char text[320];
    snprintf(text, sizeof(text), "%s · snapshot %dx%d · readback %dx%d · sets %u, reads %u", what,
             state->snapshot_width, state->snapshot_height, state->readback_width,
             state->readback_height, state->sets, state->reads);
    pt_widget_set_text(state->info, text);
}

static bool take_snapshot(clipboard_state *state) {
    int width = pt_window_get_width(state->window);
    int height = pt_window_get_height(state->window);
    if (width <= 0 || height <= 0) {
        pt_widget_set_text(state->status, "Window has no size to snapshot yet");
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    pt_color *pixels = malloc(count * sizeof(*pixels));
    if (!pixels) {
        pt_widget_set_text(state->status, "Out of memory for the snapshot");
        return false;
    }
    if (!pt_window_paint_pixels(state->window, pixels, width, height)) {
        free(pixels);
        pt_widget_set_text(state->status, "Offscreen snapshot unavailable");
        return false;
    }
    free(state->snapshot);
    state->snapshot = pixels;
    state->snapshot_width = width;
    state->snapshot_height = height;
    return true;
}

static bool read_image(clipboard_state *state) {
    int width = 0;
    int height = 0;
    pt_color *pixels = pt_window_clipboard_get_image_png(state->window, &width, &height);
    ++state->reads;
    if (!pixels) {
        char text[256];
        snprintf(text, sizeof(text), "No image on the clipboard: %s", pt_get_last_error());
        pt_widget_set_text(state->status, text);
        update_info(state, "Image read failed");
        return false;
    }
    free_pixels(&state->readback);
    state->readback = pixels;
    state->readback_width = width;
    state->readback_height = height;
    uint32_t hash = pixel_hash(pixels, (size_t)width * (size_t)height);
    char text[256];
    size_t differing = 0;
    if (state->snapshot && state->snapshot_width == width && state->snapshot_height == height) {
        size_t count = (size_t)width * (size_t)height;
        for (size_t index = 0; index < count; ++index) {
            if (state->snapshot[index] != pixels[index]) {
                ++differing;
            }
        }
    }
    snprintf(text, sizeof(text),
             "Read back %dx%d premultiplied pixels, hash 0x%08X, %zu differ from the snapshot",
             width, height, hash, differing);
    pt_widget_set_text(state->status, text);
    update_info(state, "Image round trip complete");
    pt_widget_request_redraw(state->preview);
    return true;
}

static void on_snapshot(pt_widget *button, void *user_data) {
    (void)button;
    clipboard_state *state = user_data;
    if (!take_snapshot(state)) {
        return;
    }
    if (!pt_window_clipboard_set_image_png(state->window, state->snapshot,
                                           state->snapshot_width, state->snapshot_height)) {
        char text[256];
        snprintf(text, sizeof(text), "Image clipboard set failed: %s", pt_get_last_error());
        pt_widget_set_text(state->status, text);
        return;
    }
    ++state->sets;
    read_image(state);
    char *text_copy = pt_window_clipboard_get(state->window);
    char text[256];
    snprintf(text, sizeof(text),
             "Published the snapshot as image/png; the text getter now returns %s",
             text_copy ? "text" : "NULL (as documented)");
    pt_free(text_copy);
    pt_widget_set_text(state->status, text);
    update_info(state, "Snapshot published");
}

static void on_read(pt_widget *button, void *user_data) {
    (void)button;
    read_image((clipboard_state *)user_data);
}

static void on_write_text(pt_widget *button, void *user_data) {
    (void)button;
    clipboard_state *state = user_data;
    if (pt_window_clipboard_set(state->window, "plain text from clipboard_image")) {
        ++state->sets;
        pt_widget_set_text(state->status,
                          "Text published; the image getter now returns NULL");
        update_info(state, "Text replaces the image");
    } else {
        pt_widget_set_text(state->status, "Text clipboard set failed");
    }
}

static bool write_sample_file(const char *path, const char *body) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    size_t length = strlen(body);
    bool ok = fwrite(body, 1, length, file) == length;
    fclose(file);
    return ok;
}

static void on_write_uri_list(pt_widget *button, void *user_data) {
    (void)button;
    clipboard_state *state = user_data;
    char directory[] = "/tmp/pt-example-uris-XXXXXX";
    if (!mkdtemp(directory)) {
        pt_widget_set_text(state->status, "Could not create a temporary directory");
        return;
    }
    char readme[512];
    char notes[512];
    char listing[512];
    snprintf(readme, sizeof(readme), "%s/readme.txt", directory);
    snprintf(notes, sizeof(notes), "%s/notes.txt", directory);
    snprintf(listing, sizeof(listing), "%s/list.txt", directory);
    static const char *const bodies[] = {
        "Pt clipboard image example\n",
        "Two ordinary files plus a listing, offered as text/uri-list.\n",
        "file one\nfile two\n",
    };
    const char *paths[] = {readme, notes, listing};
    bool wrote = true;
    for (size_t index = 0; index < 3; ++index) {
        wrote = wrote && write_sample_file(paths[index], bodies[index]);
    }
    if (!wrote) {
        pt_widget_set_text(state->status, "Could not write the sample URI files");
        return;
    }
    if (!pt_window_clipboard_set_uri_list(state->window, paths, 3)) {
        char text[256];
        snprintf(text, sizeof(text), "uri-list set failed: %s", pt_get_last_error());
        pt_widget_set_text(state->status, text);
        return;
    }
    ++state->sets;
    int width = 0;
    int height = 0;
    pt_color *image = pt_window_clipboard_get_image_png(state->window, &width, &height);
    bool had_image = image != NULL;
    pt_free(image);
    char text[320];
    snprintf(text, sizeof(text),
             "Wrote 3 absolute paths as text/uri-list under %s; image getter now %s", directory,
             had_image ? "has data" : "returns NULL");
    pt_widget_set_text(state->status, text);
    update_info(state, "URI list published");
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.clipboard_image");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x9ECE6A));

    pt_window *window = pt_window_create(app, "Pt Clipboard Image", 760, 640);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    clipboard_state state = {.app = app, .window = window};
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
    pt_widget_set_gap(root, 12.0f);

    pt_widget *eyebrow = pt_label_create(root, "CLIPBOARD  ·  IMAGE/PNG  ·  TEXT/URI-LIST");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x9ECE6A));

    pt_widget *heading = pt_label_create(root, "Snapshot, publish, read back");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "The button snapshots this window with pt_window_paint_pixels(), publishes the "
              "premultiplied pixels as image/png and immediately decodes them again. The preview "
              "paints only the stored readback buffer.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    state.preview = pt_box_create(root, pt_direction_column);
    pt_widget_set_grow(state.preview, 1.0f);
    pt_widget_set_min_height(state.preview, 240.0f);
    pt_widget_set_radius(state.preview, 12.0f);
    pt_widget_set_border_width(state.preview, 1.0f);
    pt_widget_set_border_color(state.preview, pt_rgb(0x30415C));
    pt_widget_set_paint_callback(state.preview, paint_preview, &state);
    pt_widget_set_a11y_role(state.preview, pt_a11y_role_icon);
    pt_widget_set_a11y_name(state.preview, "Clipboard image preview");
    pt_widget_set_a11y_description(
        state.preview, "Paints the decoded clipboard image over a checkerboard.");

    pt_widget *tools = pt_wrap_create(root);
    pt_wrap_set_gap(tools, 8.0f, 8.0f);
    pt_widget *snapshot = pt_button_create(tools, "Snapshot → clipboard image");
    pt_button_set_variant(snapshot, pt_button_primary);
    pt_button_set_on_click(snapshot, on_snapshot, &state);
    pt_widget_set_tooltip(snapshot, "Paint offscreen, encode PNG, then decode it again");
    pt_widget *read = pt_button_create(tools, "Read image");
    pt_button_set_variant(read, pt_button_secondary);
    pt_button_set_on_click(read, on_read, &state);
    pt_widget *text = pt_button_create(tools, "Write text");
    pt_button_set_variant(text, pt_button_ghost);
    pt_button_set_on_click(text, on_write_text, &state);
    pt_widget *uri = pt_button_create(tools, "Write uri-list");
    pt_button_set_variant(uri, pt_button_soft);
    pt_button_set_on_click(uri, on_write_uri_list, &state);
    pt_widget_set_tooltip(uri, "Absolute paths are emitted as file:// URIs with CRLF");

    state.status = pt_label_create(root, "Ready · press Snapshot to publish an image");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);
    pt_widget_set_a11y_name(state.status, "Clipboard status");

    state.info = pt_label_create(root, "");
    pt_widget_set_font_size(state.info, 12.0f);
    pt_widget_set_foreground(state.info, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(state.info, true);
    update_info(&state, "No snapshot yet");

    pt_widget *footer = pt_label_create(
        root, "The getter returns caller-owned pixels (pt_free). Decoding reuses the 1.5 s / "
              "16 MiB transfer cap, rejects dimensions above pt_clipboard_image_max_dimension "
              "and never crashes on corrupt data.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(snapshot);

    printf("clipboard_image: window %dx%d, max dimension %d\n", pt_window_get_width(window),
           pt_window_get_height(window), pt_clipboard_image_max_dimension);
    fflush(stdout);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        on_snapshot(NULL, &state);
        printf("clipboard_image: snapshot=%dx%d readback=%dx%d sets=%u reads=%u\n",
               state.snapshot_width, state.snapshot_height, state.readback_width,
               state.readback_height, state.sets, state.reads);
        on_write_uri_list(NULL, &state);
        printf("clipboard_image: after uri-list sets=%u readback retained=%dx%d\n", state.sets,
               state.readback_width, state.readback_height);
        fflush(stdout);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    free(state.snapshot);
    free_pixels(&state.readback);
    pt_app_destroy(app);
    return status;
}
