/*
 * Pt SVG icons: a vector icon decoded from memory and from a file, a PNG
 * payload that takes the raster path, hicolor scalable lookup, and the
 * best-effort window icon.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/icon.h):
 *
 *  - pt_icon_image_load_memory and pt_icon_image_load_file sniff the payload:
 *    after optional UTF-8 BOM/ASCII whitespace, '<' followed by `<svg` or an
 *    `<?xml` declaration selects the SVG path. Every other byte stream stays on
 *    the PNG path. The example loads one SVG buffer and one PNG buffer through
 *    the same call to show that the bytes decide the decoder, not the API.
 *  - SVG supports rect/circle/ellipse/line/polyline/polygon/path, groups,
 *    gradients and opacity. It is rasterised per requested size/scale into the
 *    same cache as PNG, preserving the intrinsic aspect ratio (PNG is resized
 *    to the exact box). Unsupported elements are ignored, never fatal.
 *  - pt_button_set_icon_image shares one reference-counted image across an
 *    icon+text button, an icon-only button and a tinted button. Setters take
 *    their own reference; the example drops the loader reference at the end.
 *  - pt_icon_image_from_name follows $XDG_DATA_HOME/icons and
 *    $XDG_DATA_DIRS/icons. Without a usable index.theme it scans NxN and
 *    scalable directories, preferring .svg over .png in scalable/. The example
 *    builds a private hicolor tree, points XDG_DATA_HOME at it and looks up the
 *    name again; a missing name is a NULL return plus pt_get_last_error, never
 *    a crash.
 *  - pt_window_set_icon is best effort. Without xdg_toplevel_icon_v1 (niri,
 *    Sway, Weston) and under PT_HEADLESS it is a silent no-op, while the
 *    getter still reports the retained image.
 */

#include <pt/pt.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A 64x64 vector icon: a half-transparent gradient plate, an opaque-ish
 * circle and a translucent triangle. fill-opacity/opacity are carried by
 * nanosvg's paints, so the alpha channel survives rasterisation. */
static const char svg_source[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" "
    "viewBox=\"0 0 64 64\">\n"
    "  <defs>\n"
    "    <linearGradient id=\"warm\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">\n"
    "      <stop offset=\"0\" stop-color=\"#F7768E\"/>\n"
    "      <stop offset=\"1\" stop-color=\"#E0AF68\"/>\n"
    "    </linearGradient>\n"
    "  </defs>\n"
    "  <rect x=\"4\" y=\"4\" width=\"56\" height=\"56\" fill=\"url(#warm)\" "
    "fill-opacity=\"0.45\"/>\n"
    "  <circle cx=\"32\" cy=\"32\" r=\"17\" fill=\"#7AA2F7\" opacity=\"0.9\"/>\n"
    "  <path d=\"M32 20 L42 39 L22 39 Z\" fill=\"#FFFFFF\" fill-opacity=\"0.75\"/>\n"
    "</svg>\n";

/* A 16x16 RGBA checker with two alpha levels. The loader sees no '<' and
 * decodes it as PNG, so the example can compare both paths side by side. */
static const unsigned char icon_png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0xF3, 0xFF, 0x61, 0x00, 0x00, 0x00,
    0x26, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x98, 0x77, 0x2E, 0xEB,
    0xFF, 0xBC, 0x73, 0x59, 0x0B, 0xC8, 0xA5, 0x19, 0x28, 0xD1, 0x0C, 0xA2,
    0x41, 0x06, 0x8C, 0xBA, 0x60, 0x34, 0x0C, 0x46, 0xD3, 0x41, 0xD6, 0x20,
    0x08, 0x03, 0x00, 0x68, 0x70, 0xA5, 0x9F, 0x30, 0x75, 0xC0, 0x6C, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

#define THEME_ICON_NAME "pt-example-svg"

typedef struct svg_state {
    pt_app *app;
    pt_window *window;
    pt_widget *vg_button;
    pt_widget *png_button;
    pt_widget *lookup_button;
    pt_widget *status;
    bool tinted;
} svg_state;

static void report(svg_state *state, const char *text) {
    pt_widget_set_text(state->status, text);
    printf("svg_icon: %s\n", text);
    fflush(stdout);
}

static bool make_dir(const char *path) {
    if (mkdir(path, 0700) == 0) {
        return true;
    }
    return errno == EEXIST;
}

/* Bounds-checked path concatenation: unlike snprintf("%s/suffix") it never
 * provokes -Wformat-truncation in a fixed PT_ICON_PATH_MAX buffer. */
static bool join_path(char *out, size_t out_size, const char *base, const char *suffix) {
    size_t base_length = strlen(base);
    size_t suffix_length = strlen(suffix);
    if (base_length + suffix_length >= out_size) {
        return false;
    }
    memcpy(out, base, base_length);
    memcpy(out + base_length, suffix, suffix_length + 1);
    return true;
}

static bool write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    size_t length = strlen(text);
    bool ok = fwrite(text, 1, length, file) == length;
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

/* Builds $TMPDIR/pt-svg-icon-XXXXXX; returns false when no temp directory
 * could be created, in which case the example still runs the memory paths. */
static bool make_temp_tree(char *root, size_t root_size, char *svg_path,
                           size_t svg_path_size, char *theme_icon_path,
                           size_t theme_icon_path_size) {
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) {
        tmp = "/tmp";
    }
    int written = snprintf(root, root_size, "%s/pt-svg-icon-XXXXXX", tmp);
    if (written <= 0 || (size_t)written >= root_size || !mkdtemp(root)) {
        return false;
    }
    if (!make_dir(root)) {
        return false;
    }
    char icons[PT_ICON_PATH_MAX];
    char hicolor[PT_ICON_PATH_MAX];
    char scalable[PT_ICON_PATH_MAX];
    char apps[PT_ICON_PATH_MAX];
    if (!join_path(icons, sizeof(icons), root, "/icons") ||
        !join_path(hicolor, sizeof(hicolor), icons, "/hicolor") ||
        !join_path(scalable, sizeof(scalable), hicolor, "/scalable") ||
        !join_path(apps, sizeof(apps), scalable, "/apps")) {
        return false;
    }
    if (!make_dir(icons) || !make_dir(hicolor) || !make_dir(scalable) || !make_dir(apps)) {
        return false;
    }
    if (!join_path(svg_path, svg_path_size, root, "/icon.svg")) {
        return false;
    }
    const char suffix[] = "/" THEME_ICON_NAME ".svg";
    if (!join_path(theme_icon_path, theme_icon_path_size, apps, suffix)) {
        return false;
    }
    return write_file(svg_path, svg_source) && write_file(theme_icon_path, svg_source);
}

static void remove_tree(const char *root) {
    char path[PT_ICON_PATH_MAX];
    if (!join_path(path, sizeof(path), root, "/icons/hicolor/scalable/apps/"
                                           THEME_ICON_NAME ".svg")) {
        return;
    }
    unlink(path);
    char apps[PT_ICON_PATH_MAX];
    char scalable[PT_ICON_PATH_MAX];
    char hicolor[PT_ICON_PATH_MAX];
    char icons[PT_ICON_PATH_MAX];
    if (!join_path(apps, sizeof(apps), root, "/icons/hicolor/scalable/apps") ||
        !join_path(scalable, sizeof(scalable), root, "/icons/hicolor/scalable") ||
        !join_path(hicolor, sizeof(hicolor), root, "/icons/hicolor") ||
        !join_path(icons, sizeof(icons), root, "/icons")) {
        return;
    }
    rmdir(apps);
    rmdir(scalable);
    rmdir(hicolor);
    rmdir(icons);
    if (join_path(path, sizeof(path), root, "/icon.svg")) {
        unlink(path);
    }
    rmdir(root);
}

static void on_tint_toggle(pt_widget *button, void *user_data) {
    (void)button;
    svg_state *state = user_data;
    /* Toggling between "own colours" and a theme tint on the same shared image
     * shows that the tint is a slot property, not a pixel edit. */
    state->tinted = !state->tinted;
    pt_button_set_icon_tint(state->vg_button, state->tinted ? pt_rgb(0x9ECE6A) : 0u);
    char text[128];
    snprintf(text, sizeof(text), "SVG button tint %s",
             state->tinted ? "0x9ECE6A (alpha mask follows the theme)"
                           : "cleared (gradient colours)");
    report(state, text);
}

static void on_lookup(pt_widget *button, void *user_data) {
    (void)button;
    svg_state *state = user_data;
    pt_icon_image *found = pt_icon_image_from_name(THEME_ICON_NAME, 24);
    if (found) {
        pt_button_set_icon_image(state->lookup_button, found);
        char text[192];
        snprintf(text, sizeof(text), "hicolor scalable hit: %dx%d from the private theme tree",
                 pt_icon_image_width(found), pt_icon_image_height(found));
        report(state, text);
        pt_icon_image_destroy(found);
        return;
    }
    char text[256];
    snprintf(text, sizeof(text), "hicolor miss (safe): %s", pt_get_last_error());
    report(state, text);
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.svg_icon");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt SVG Icons", 720, 560);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    svg_state state = {.app = app, .window = window};
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "ICONS  ·  SVG  ·  SNIFF  ·  HICOLOR");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    pt_widget *heading = pt_label_create(root, "Vector icons from one loader");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "The same pt_icon_image_load_memory call decodes both buffers: the '<svg' "
              "prefix selects nanosvg, anything else stays on the PNG decoder. Both buffers "
              "drive reference-counted button slots, and the gradient's fill-opacity is "
              "rasterised at each requested size.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    /* Memory path: the SVG bytes are copied during the call, so the static
     * buffer may stay read-only for the whole run. */
    pt_icon_image *vector = pt_icon_image_load_memory(svg_source, sizeof(svg_source) - 1);
    if (!vector) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pt_icon_image *raster = pt_icon_image_load_memory(icon_png, sizeof(icon_png));
    if (!raster) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_icon_image_destroy(vector);
        pt_app_destroy(app);
        return 1;
    }

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(card, 14.0f);

    pt_widget *row = pt_row_create(card);
    pt_widget_set_gap(row, 12.0f);
    pt_widget_set_align_self(row, pt_align_center);

    pt_widget *icon_only = pt_button_create(row, "");
    pt_widget_set_size(icon_only, 52.0f, 46.0f);
    pt_button_set_variant(icon_only, pt_button_secondary);
    pt_button_set_icon_image(icon_only, vector);
    pt_widget_set_a11y_name(icon_only, "SVG icon-only action");
    pt_widget_set_tooltip(icon_only, "SVG: centred alone, 18 logical px");

    state.vg_button = pt_button_create(row, "SVG icon and label");
    pt_button_set_variant(state.vg_button, pt_button_secondary);
    pt_button_set_icon_image(state.vg_button, vector);
    pt_widget_set_tooltip(state.vg_button, "SVG: gradient plus translucent shapes");

    state.png_button = pt_button_create(row, "PNG (sniffed)");
    pt_button_set_variant(state.png_button, pt_button_secondary);
    pt_button_set_icon_image(state.png_button, raster);
    pt_widget_set_tooltip(state.png_button, "Same load call, but the bytes start with PNG magic");

    pt_widget *tools = pt_row_create(card);
    pt_widget_set_gap(tools, 10.0f);

    pt_widget *tint_button = pt_button_create(tools, "Toggle SVG tint");
    pt_button_set_variant(tint_button, pt_button_secondary);
    pt_button_set_on_click(tint_button, on_tint_toggle, &state);

    state.lookup_button = pt_button_create(tools, "Look up " THEME_ICON_NAME);
    pt_button_set_variant(state.lookup_button, pt_button_ghost);
    pt_button_set_on_click(state.lookup_button, on_lookup, &state);
    pt_widget_set_tooltip(state.lookup_button, "Private hicolor tree: scalable/*.svg");

    state.status = pt_label_create(root, "");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);

    /* File path: exercise load_file on the same bytes, then use the result as
     * the window icon. The temp tree is removed after the event loop, so the
     * decoded handles stay valid for the whole run. */
    char tree[PT_ICON_PATH_MAX] = "";
    char svg_path[PT_ICON_PATH_MAX] = "";
    char theme_icon_path[PT_ICON_PATH_MAX] = "";
    bool tree_ok = make_temp_tree(tree, sizeof(tree), svg_path, sizeof(svg_path), theme_icon_path,
                                  sizeof(theme_icon_path));
    char summary[320];
    if (tree_ok) {
        pt_icon_image *from_file = pt_icon_image_load_file(svg_path);
        if (from_file) {
            pt_window_set_icon(window, from_file);
            snprintf(summary, sizeof(summary),
                     "SVG memory %dx%d, SVG file %dx%d, PNG %dx%d; window icon %s",
                     pt_icon_image_width(vector), pt_icon_image_height(vector),
                     pt_icon_image_width(from_file), pt_icon_image_height(from_file),
                     pt_icon_image_width(raster), pt_icon_image_height(raster),
                     pt_window_get_icon(window) ? "retained (best effort)" : "rejected");
            pt_icon_image_destroy(from_file);
        } else {
            snprintf(summary, sizeof(summary), "SVG file load failed: %s", pt_get_last_error());
        }
    } else {
        pt_window_set_icon(window, vector);
        snprintf(summary, sizeof(summary),
                 "SVG memory %dx%d, PNG %dx%d; temp tree unavailable, window icon %s",
                 pt_icon_image_width(vector), pt_icon_image_height(vector),
                 pt_icon_image_width(raster), pt_icon_image_height(raster),
                 pt_window_get_icon(window) ? "retained (best effort)" : "rejected");
    }
    report(&state, summary);

    /* hicolor lookup with the private tree: the name resolves to scalable SVG
     * without any index.theme, then a miss still only records an error. The
     * tree stays alive and on $XDG_DATA_HOME for the whole run so the lookup
     * button works interactively; cleanup happens after the event loop. */
    char *saved_data_home = NULL;
    if (tree_ok) {
        const char *old_home = getenv("XDG_DATA_HOME");
        saved_data_home = old_home ? strdup(old_home) : NULL;
        setenv("XDG_DATA_HOME", tree, 1);
        pt_icon_image *found = pt_icon_image_from_name(THEME_ICON_NAME, 24);
        if (found) {
            pt_button_set_icon_image(state.lookup_button, found);
            char text[224];
            snprintf(text, sizeof(text),
                     "hicolor lookup: %s at %dx%d through $XDG_DATA_HOME=%s",
                     THEME_ICON_NAME, pt_icon_image_width(found), pt_icon_image_height(found),
                     tree);
            report(&state, text);
            pt_icon_image_destroy(found);
        } else {
            char text[288];
            snprintf(text, sizeof(text), "hicolor lookup missed (no crash): %s",
                     pt_get_last_error());
            report(&state, text);
        }
        pt_icon_image *missing = pt_icon_image_from_name("pt-example-missing", 32);
        if (!missing) {
            char text[288];
            snprintf(text, sizeof(text), "missing-name lookup returned NULL: %s",
                     pt_get_last_error());
            report(&state, text);
        } else {
            pt_icon_image_destroy(missing);
        }
    }

    pt_widget *footer = pt_label_create(
        root, "SVG rasterises per requested size/scale and keeps its aspect ratio; the PNG path "
              "resizes to the exact box. Both share the bounded LRU cache. Setters hold their own "
              "reference, so the loader references are dropped below.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(icon_only);

    /* The slots took their own references above. */
    pt_icon_image_destroy(vector);
    pt_icon_image_destroy(raster);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        on_lookup(NULL, &state);
        on_tint_toggle(NULL, &state);
        printf("svg_icon: headless buttons=%d/%d/%d lookup=%d window-icon=%d\n",
               pt_button_get_icon_image(icon_only) ? 1 : 0,
               pt_button_get_icon_image(state.vg_button) ? 1 : 0,
               pt_button_get_icon_image(state.png_button) ? 1 : 0,
               pt_button_get_icon_image(state.lookup_button) ? 1 : 0,
               pt_window_get_icon(window) ? 1 : 0);
        fflush(stdout);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    if (tree_ok) {
        if (saved_data_home) {
            setenv("XDG_DATA_HOME", saved_data_home, 1);
            free(saved_data_home);
        } else {
            unsetenv("XDG_DATA_HOME");
        }
    }
    remove_tree(tree);
    pt_app_destroy(app);
    return status;
}