/*
 * Icon images: bounded PNG loading, hicolor lookup, the raster cache, button
 * slots, and the best-effort window icon.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int skipped_checks;

static bool make_directory(const char *path) {
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool make_tree(char *root, size_t capacity) {
    const char *base = getenv("TMPDIR");
    snprintf(root, capacity, "%s/pt-icon-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(root)) {
        return false;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/icons", root);
    if (!make_directory(path)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/icons/hicolor", root);
    if (!make_directory(path)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/icons/hicolor/32x32", root);
    if (!make_directory(path)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/icons/hicolor/32x32/apps", root);
    if (!make_directory(path)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/icons/hicolor/scalable", root);
    if (!make_directory(path)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/icons/hicolor/scalable/apps", root);
    return make_directory(path);
}

static void remove_tree(const char *root) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/roundtrip.png", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/truncated.png", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/loop.svg", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/32x32/apps/pt-m19-icon.png", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/scalable/apps/pt-svg-icon.svg", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/index.theme", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/scalable/apps", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/scalable", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/32x32/apps", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor/32x32", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/icons/hicolor", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/icons", root);
    rmdir(path);
    rmdir(root);
}

static void write_test_png(const char *path, int width, int height) {
    size_t count = (size_t)width * (size_t)height;
    uint8_t *rgba = calloc(count, 4);
    pt_check(rgba != NULL);
    if (!rgba) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t *pixel = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            if (x == 0 && y == 0) {
                pixel[0] = 255;
                pixel[1] = 0;
                pixel[2] = 0;
                pixel[3] = 255;
            } else if (x == 1) {
                pixel[0] = 0;
                pixel[1] = 255;
                pixel[2] = 0;
                pixel[3] = 128;
            } else if (y == 1) {
                pixel[0] = 0;
                pixel[1] = 0;
                pixel[2] = 255;
                pixel[3] = 255;
            } else {
                pixel[0] = (uint8_t)(x * 16);
                pixel[1] = (uint8_t)(y * 16);
                pixel[2] = 64;
                pixel[3] = 255;
            }
        }
    }
    pt_check(pt_write_png_rgba(path, width, height, rgba) == 0);
    free(rgba);
}

static pt_icon_image *make_icon(void) {
    size_t size = 0;
    uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) {
        pixels[i] = 0x80FF0000u | (uint32_t)i;
    }
    uint8_t *png = pt_png_encode_argb_premultiplied(pixels, 4, 4, &size);
    pt_check(png != NULL && size > 0);
    pt_icon_image *image = pt_icon_image_load_memory(png, size);
    free(png);
    return image;
}

static void test_png_roundtrip(const char *root) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/roundtrip.png", root);
    write_test_png(path, 4, 4);
    pt_icon_image *image = pt_icon_image_load_file(path);
    pt_check(image != NULL);
    if (!image) {
        ++skipped_checks;
        return;
    }
    pt_check(pt_icon_image_width(image) == 4);
    pt_check(pt_icon_image_height(image) == 4);
    int stride = 0;
    const uint32_t *raster = pt_icon_image_raster(image, 4, 4, &stride);
    pt_check(raster != NULL && stride == 4);
    if (raster) {
        pt_check(raster[0] == 0xFFFF0000u);
        pt_check((raster[1] >> 24) == 128u);
        pt_check(((raster[1] >> 8) & 0xFFu) == 128u);
        pt_check(raster[4] == 0xFF0000FFu);
    }
    pt_icon_image *again = pt_icon_image_ref(image);
    pt_check(again == image);
    pt_icon_image_destroy(image);
    pt_check(pt_icon_image_width(again) == 4);
    pt_icon_image_destroy(again);
    pt_check(pt_icon_cache_entry_count() == 0);
}

static void test_png_failures(const char *root) {
    static const uint8_t garbage[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    pt_check(pt_icon_image_load_memory(garbage, sizeof(garbage)) == NULL);
    pt_check(pt_get_last_error() != NULL);

    char path[1024];
    snprintf(path, sizeof(path), "%s/truncated.png", root);
    write_test_png(path, 8, 8);
    FILE *file = fopen(path, "rb");
    pt_check(file != NULL);
    if (file) {
        uint8_t prefix[30];
        size_t read = fread(prefix, 1, sizeof(prefix), file);
        fclose(file);
        pt_check(read == sizeof(prefix));
        pt_check(pt_icon_image_load_memory(prefix, read) == NULL);
    }

    /* A valid signature and IHDR with a 5000x10 claim must be rejected from the
     * header before the decoder allocates or inflates anything. */
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t header[24] = {0};
    memcpy(header, signature, sizeof(signature));
    header[11] = 13;
    memcpy(header + 12, "IHDR", 4);
    header[16] = 0;
    header[17] = 0;
    header[18] = 0x13;
    header[19] = 0x88; /* 5000 */
    header[20] = 0;
    header[21] = 0;
    header[22] = 0;
    header[23] = 10;
    pt_check(pt_icon_image_load_memory(header, sizeof(header)) == NULL);
    pt_check(strstr(pt_get_last_error() ? pt_get_last_error() : "", "exceed") != NULL);

    pt_check(pt_icon_image_load_file(NULL) == NULL);
    pt_check(pt_icon_image_load_file("/definitely/not/here.png") == NULL);
    pt_check(pt_icon_image_load_file("") == NULL);
}

static void test_cache_bounds(void) {
    pt_icon_image *image = make_icon();
    pt_check(image != NULL);
    if (!image) {
        ++skipped_checks;
        return;
    }
    pt_check(pt_icon_cache_entry_count() == 0);
    for (int size = 4; size < 40; ++size) {
        int stride = 0;
        const uint32_t *raster = pt_icon_image_raster(image, size, size, &stride);
        pt_check(raster != NULL && stride == size);
    }
    pt_check(pt_icon_cache_entry_count() <= PT_ICON_IMAGE_CACHE_MAX_ENTRIES);
    pt_check(pt_icon_cache_pixel_count() <= PT_ICON_CACHE_MAX_PIXELS);
    pt_check(pt_icon_image_raster(image, 0, 4, NULL) == NULL);
    pt_check(pt_icon_image_raster(image, 4, -1, NULL) == NULL);
    pt_check(pt_icon_image_raster(image, PT_ICON_MAX_DIMENSION + 1, 4, NULL) == NULL);

    /* A second and third handle share the global budget. */
    pt_icon_image *second = make_icon();
    pt_icon_image *third = make_icon();
    pt_check(second && third);
    if (second && third) {
        for (int size = 4; size < 30; ++size) {
            pt_check(pt_icon_image_raster(second, size, size, NULL) != NULL);
            pt_check(pt_icon_image_raster(third, size, size, NULL) != NULL);
        }
    }
    pt_check(pt_icon_cache_entry_count() <= PT_ICON_CACHE_MAX_ENTRIES);
    pt_check(pt_icon_cache_pixel_count() <= PT_ICON_CACHE_MAX_PIXELS);
    pt_icon_image_destroy(image);
    pt_icon_image_destroy(second);
    pt_icon_image_destroy(third);
    pt_check(pt_icon_cache_entry_count() == 0);
    pt_check(pt_icon_cache_pixel_count() == 0);
}

static void test_hicolor(const char *root) {
    char icon_path[1024];
    snprintf(icon_path, sizeof(icon_path), "%s/icons/hicolor/32x32/apps/pt-m19-icon.png",
             root);
    write_test_png(icon_path, 8, 8);
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/icons/hicolor/index.theme", root);
    FILE *index = fopen(index_path, "w");
    pt_check(index != NULL);
    if (index) {
        fputs("[Icon Theme]\nName=Pt Test\nDirectories=32x32/apps\n\n", index);
        fputs("[32x32/apps]\nSize=32\nType=Fixed\nContext=Apps\n", index);
        fclose(index);
    }
    char previous[1024] = {0};
    const char *old = getenv("XDG_DATA_HOME");
    if (old) {
        snprintf(previous, sizeof(previous), "%s", old);
    }
    setenv("XDG_DATA_HOME", root, 1);
    pt_icon_image *image = pt_icon_image_from_name("pt-m19-icon", 32);
    pt_check(image != NULL);
    if (image) {
        pt_check(pt_icon_image_width(image) == 8);
        pt_icon_image_destroy(image);
    } else {
        ++skipped_checks;
    }

    /* Without index.theme the NxN directory scan is the fallback. */
    pt_check(unlink(index_path) == 0);
    image = pt_icon_image_from_name("pt-m19-icon", 24);
    pt_check(image != NULL);
    if (image) {
        pt_icon_image_destroy(image);
    } else {
        ++skipped_checks;
    }

    /* Missing and invalid names fail without crashing. */
    pt_check(pt_icon_image_from_name("pt-m19-definitely-missing", 32) == NULL);
    pt_check(pt_icon_image_from_name("../escape", 32) == NULL);
    pt_check(pt_icon_image_from_name("", 32) == NULL);
    pt_check(pt_icon_image_from_name(NULL, 32) == NULL);
    ++skipped_checks; /* the deliberate missing-name lookup */

    if (old) {
        setenv("XDG_DATA_HOME", previous, 1);
    } else {
        unsetenv("XDG_DATA_HOME");
    }
    printf("# icon: hicolor lookups skipped %d (missing index fallback or environment)\n",
           skipped_checks);
}

static const char svg_rect[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
    "<rect x=\"2\" y=\"2\" width=\"4\" height=\"4\" fill=\"#ff0000\"/></svg>";
static const char svg_semi[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
    "<rect width=\"8\" height=\"8\" fill=\"#ff0000\" fill-opacity=\"0.5\"/></svg>";
static const char svg_viewbox[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 12\">"
    "<rect width=\"24\" height=\"12\" fill=\"#0000ff\"/></svg>";

static int open_fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        return -1;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

static void test_svg_shapes(void) {
    pt_icon_image *image = pt_icon_image_load_memory(svg_rect, sizeof(svg_rect) - 1);
    pt_check(image != NULL);
    if (!image) {
        ++skipped_checks;
        return;
    }
    pt_check(pt_icon_image_width(image) == 8);
    pt_check(pt_icon_image_height(image) == 8);
    int stride = 0;
    const uint32_t *raster = pt_icon_image_raster(image, 8, 8, &stride);
    pt_check(raster != NULL && stride == 8);
    if (raster) {
        /* Transparent background, opaque red artwork, exact premultiplied
         * ARGB at full coverage. */
        pt_check(raster[0] == 0);
        pt_check(raster[1 * 8 + 1] == 0);
        pt_check(raster[2 * 8 + 2] == 0xFFFF0000u);
        pt_check(raster[4 * 8 + 4] == 0xFFFF0000u);
        pt_check(raster[5 * 8 + 5] == 0xFFFF0000u);
    }

    /* The same handle rasterises through pt_icon_draw_canvas at 1x and 2x; the
     * cache keys are the device sizes (8x8 and 16x16). */
    pt_canvas *canvas = pt_canvas_create(16, 16, 1.0f);
    pt_check(canvas != NULL);
    if (canvas) {
        pt_icon_draw_canvas(canvas, image, 0.0f, 0.0f, 8.0f, 8.0f, 0);
        pt_check(canvas->pixels[4 * (size_t)canvas->stride + 4] == 0xFFFF0000u);
        pt_canvas *hidpi = pt_canvas_create(32, 32, 2.0f);
        pt_check(hidpi != NULL);
        if (hidpi) {
            pt_icon_draw_canvas(hidpi, image, 0.0f, 0.0f, 8.0f, 8.0f, 0);
            pt_check(hidpi->pixels[8 * (size_t)hidpi->stride + 8] == 0xFFFF0000u);
            pt_check(pt_icon_cache_entry_count() == 2);
            pt_canvas_destroy(hidpi);
        }
        pt_canvas_destroy(canvas);
    }

    /* Reference counting and cache ownership are unchanged for SVG handles. */
    pt_icon_image *again = pt_icon_image_ref(image);
    pt_check(again == image);
    pt_icon_image_destroy(image);
    pt_check(pt_icon_image_width(again) == 8);
    pt_icon_image_destroy(again);
    pt_check(pt_icon_cache_entry_count() == 0);

    /* Nanosvg returns straight RGBA. Pt stores premultiplied 0xAARRGGBB, so a
     * 50% red fill must come out as alpha 127 with the red component equal to
     * that alpha, never as 0xAARRGGBB with an un-premultiplied 255 red. */
    image = pt_icon_image_load_memory(svg_semi, sizeof(svg_semi) - 1);
    pt_check(image != NULL);
    if (image) {
        raster = pt_icon_image_raster(image, 8, 8, &stride);
        pt_check(raster != NULL && stride == 8);
        if (raster) {
            uint32_t pixel = raster[4 * 8 + 4];
            uint32_t alpha = pixel >> 24;
            pt_check(alpha == 127u);
            pt_check(((pixel >> 16) & 0xFFu) == alpha);
            pt_check(((pixel >> 8) & 0xFFu) == 0u);
            pt_check((pixel & 0xFFu) == 0u);
            pt_check(pixel == 0x7F7F0000u);
        }
        pt_icon_image_destroy(image);
    }

    /* A viewBox-only document gets an intrinsic size from its content and
     * keeps its 2:1 aspect ratio inside a square request (letterboxed). */
    image = pt_icon_image_load_memory(svg_viewbox, sizeof(svg_viewbox) - 1);
    pt_check(image != NULL);
    if (image) {
        pt_check(pt_icon_image_width(image) == 24);
        pt_check(pt_icon_image_height(image) == 12);
        raster = pt_icon_image_raster(image, 16, 16, &stride);
        pt_check(raster != NULL && stride == 16);
        if (raster) {
            pt_check(raster[1 * 16 + 8] == 0);
            pt_check(raster[8 * 16 + 8] == 0xFF0000FFu);
            pt_check(raster[14 * 16 + 8] == 0);
        }
        pt_icon_image_destroy(image);
    }
    pt_check(pt_icon_cache_entry_count() == 0);
}

static void test_svg_cache(void) {
    pt_icon_image *image = pt_icon_image_load_memory(svg_rect, sizeof(svg_rect) - 1);
    pt_check(image != NULL);
    if (!image) {
        ++skipped_checks;
        return;
    }
    int stride = 0;
    const uint32_t *first = pt_icon_image_raster(image, 8, 8, &stride);
    pt_check(first != NULL);
    pt_check(pt_icon_cache_entry_count() == 1);
    pt_check(pt_icon_image_raster(image, 8, 8, &stride) == first);
    pt_check(pt_icon_cache_entry_count() == 1);
    pt_check(pt_icon_image_raster(image, 16, 16, &stride) != NULL);
    pt_check(pt_icon_cache_entry_count() == 2);
    pt_check(pt_icon_cache_pixel_count() == 8u * 8u + 16u * 16u);

    /* The per-handle and global caps apply to SVG rasters as they do to PNG. */
    for (int size = 4; size < 40; ++size) {
        pt_check(pt_icon_image_raster(image, size, size, &stride) != NULL);
    }
    pt_check(pt_icon_cache_entry_count() <= PT_ICON_IMAGE_CACHE_MAX_ENTRIES);
    pt_check(pt_icon_cache_pixel_count() <= PT_ICON_CACHE_MAX_PIXELS);

    pt_icon_image *second = pt_icon_image_load_memory(svg_rect, sizeof(svg_rect) - 1);
    pt_check(second != NULL);
    if (second) {
        for (int size = 4; size < 30; ++size) {
            pt_check(pt_icon_image_raster(second, size, size, NULL) != NULL);
        }
        pt_icon_image_destroy(second);
        pt_check(pt_icon_cache_entry_count() <= PT_ICON_CACHE_MAX_ENTRIES);
        pt_check(pt_icon_cache_pixel_count() <= PT_ICON_CACHE_MAX_PIXELS);
    }

    pt_check(pt_icon_image_raster(image, 0, 8, NULL) == NULL);
    pt_check(pt_icon_image_raster(image, 8, -1, NULL) == NULL);
    pt_check(pt_icon_image_raster(image, PT_ICON_MAX_DIMENSION + 1, 8, NULL) == NULL);
    pt_icon_image_destroy(image);
    pt_check(pt_icon_cache_entry_count() == 0);
    pt_check(pt_icon_cache_pixel_count() == 0);
}

static uint32_t svg_rng_state = 0x9E3779B9u;

static uint32_t svg_rng_next(void) {
    svg_rng_state ^= svg_rng_state << 13;
    svg_rng_state ^= svg_rng_state >> 17;
    svg_rng_state ^= svg_rng_state << 5;
    return svg_rng_state;
}

static void test_svg_failures(void) {
    size_t svg_length = sizeof(svg_rect) - 1;
    pt_check(pt_icon_image_load_memory(NULL, 16) == NULL);
    pt_check(pt_icon_image_load_memory(svg_rect, 0) == NULL);
    pt_check(pt_icon_image_load_memory("<svg", 4) == NULL);
    pt_check(pt_icon_image_load_memory("<svg width=\"8\" height=\"8\"><rect width=\"8\"",
                                       sizeof("<svg width=\"8\" height=\"8\"><rect width=\"8\"") -
                                           1) == NULL);
    pt_check(pt_icon_image_load_memory("<svg width=\"8\" height=\"8\"></svg>",
                                       sizeof("<svg width=\"8\" height=\"8\"></svg>") - 1) ==
             NULL);
    pt_check(pt_icon_image_load_memory("<?xml version=\"1.0\"?><not-svg/>",
                                       sizeof("<?xml version=\"1.0\"?><not-svg/>") - 1) == NULL);
    pt_check(pt_icon_image_load_memory("<html></html>", sizeof("<html></html>") - 1) == NULL);

    /* Intrinsic dimensions above PT_ICON_MAX_DIMENSION fail at load. */
    pt_check(pt_icon_image_load_memory(
                 "<svg width=\"5000\" height=\"10\"><rect width=\"5\" height=\"5\"/></svg>",
                 sizeof("<svg width=\"5000\" height=\"10\"><rect width=\"5\" height=\"5\"/></svg>") -
                     1) == NULL);
    pt_check(strstr(pt_get_last_error() ? pt_get_last_error() : "", "exceed") != NULL);

    int width = 0;
    int height = 0;
    pt_check(!pt_svg_measure(NULL, 16, &width, &height));
    pt_check(!pt_svg_measure(svg_rect, sizeof(svg_rect) - 1, NULL, &height));
    pt_check(pt_svg_rasterize(svg_rect, sizeof(svg_rect) - 1, 0, 8) == NULL);
    pt_check(pt_svg_rasterize(svg_rect, sizeof(svg_rect) - 1, 8, PT_ICON_MAX_DIMENSION + 1) ==
             NULL);

    /* The 16 MiB input cap rejects the source before parsing. */
    size_t oversized = 16u * 1024u * 1024u + 1u;
    uint8_t *blob = malloc(oversized);
    pt_check(blob != NULL);
    if (blob) {
        memset(blob, ' ', oversized);
        blob[0] = '<';
        memcpy(blob + 1, "svg width=\"8\" height=\"8\"", 24);
        pt_check(pt_icon_image_load_memory(blob, oversized) == NULL);
        pt_check(!pt_svg_measure(blob, oversized, &width, &height));
        free(blob);
    }

    /* Truncation at every byte of a valid document must never crash. Prefixes
     * that still contain a complete shape legitimately parse; the rest fail
     * with an error. */
    bool errors_ok = true;
    size_t full = sizeof(svg_rect) - 1;
    for (size_t cut = 0; cut <= full; ++cut) {
        pt_icon_image *image = pt_icon_image_load_memory(svg_rect, cut);
        if (image) {
            int stride = 0;
            if (!pt_icon_image_raster(image, 8, 8, &stride)) {
                errors_ok = false;
            }
            pt_icon_image_destroy(image);
        } else if (!pt_get_last_error() || !*pt_get_last_error()) {
            errors_ok = false;
        }
    }
    pt_check(errors_ok);

    /* Deterministic tag soup after an SVG prefix. This exercises nanosvg's XML
     * and attribute scanners; every failure must set an error and nothing may
     * crash, including under ASan/UBSan. */
    static const char alphabet[] = "<>/=\"' abcdefghijklmnopqrstuvwxyz0123456789:;.,-#()%";
    static const char prefix[] = "<svg width=\"8\" height=\"8\">";
    int parsed = 0;
    bool fuzz_ok = true;
    for (int iteration = 0; iteration < 300; ++iteration) {
        char buffer[512];
        size_t prefix_length = sizeof(prefix) - 1;
        memcpy(buffer, prefix, prefix_length);
        size_t length = prefix_length + 1u + svg_rng_next() % (sizeof(buffer) - prefix_length - 1u);
        for (size_t i = prefix_length; i < length; ++i) {
            buffer[i] = alphabet[svg_rng_next() % (sizeof(alphabet) - 1u)];
        }
        pt_icon_image *image = pt_icon_image_load_memory(buffer, length);
        if (image) {
            ++parsed;
            if (!pt_icon_image_raster(image, 8, 8, NULL)) {
                fuzz_ok = false;
            }
            pt_icon_image_destroy(image);
        } else if (!pt_get_last_error() || !*pt_get_last_error()) {
            fuzz_ok = false;
        }
    }
    pt_check(fuzz_ok);
    pt_check(parsed <= 300);
    printf("# icon: svg fuzz parsed %d of 300 generated documents\n", parsed);

    /* Byte mutations of a valid document reach attribute and path parsers that
     * tag soup rarely does; the whole valid shape remains for many edits. */
    int mutated = 0;
    bool mutation_ok = true;
    for (int iteration = 0; iteration < 300; ++iteration) {
        char buffer[256];
        memcpy(buffer, svg_rect, svg_length);
        int edits = 1 + (int)(svg_rng_next() % 8u);
        for (int edit = 0; edit < edits; ++edit) {
            size_t position = svg_rng_next() % svg_length;
            buffer[position] = alphabet[svg_rng_next() % (sizeof(alphabet) - 1u)];
        }
        pt_icon_image *image = pt_icon_image_load_memory(buffer, svg_length);
        if (image) {
            ++mutated;
            if (!pt_icon_image_raster(image, 8, 8, NULL)) {
                mutation_ok = false;
            }
            pt_icon_image_destroy(image);
        } else if (!pt_get_last_error() || !*pt_get_last_error()) {
            mutation_ok = false;
        }
    }
    pt_check(mutation_ok);
    pt_check(mutated <= 300);
    printf("# icon: svg mutations parsed %d of 300 edited documents\n", mutated);
    pt_check(pt_icon_cache_entry_count() == 0);

    /* The sniff accepts a UTF-8 BOM and leading whitespace. */
    uint8_t bom[512];
    bom[0] = 0xEFu;
    bom[1] = 0xBBu;
    bom[2] = 0xBFu;
    bom[3] = ' ';
    bom[4] = '\n';
    memcpy(bom + 5, svg_rect, svg_length);
    pt_icon_image *image = pt_icon_image_load_memory(bom, svg_length + 5u);
    pt_check(image != NULL);
    if (image) {
        pt_check(pt_icon_image_width(image) == 8);
        pt_icon_image_destroy(image);
    }
    pt_check(pt_icon_cache_entry_count() == 0);
}

static void test_svg_hicolor(const char *root) {
    char icon_path[1024];
    snprintf(icon_path, sizeof(icon_path), "%s/icons/hicolor/scalable/apps/pt-svg-icon.svg",
             root);
    FILE *icon = fopen(icon_path, "w");
    pt_check(icon != NULL);
    if (icon) {
        fputs(svg_rect, icon);
        fclose(icon);
    }
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/icons/hicolor/index.theme", root);
    FILE *index = fopen(index_path, "w");
    pt_check(index != NULL);
    if (index) {
        fputs("[Icon Theme]\nName=Pt SVG Test\n"
              "Directories=scalable/apps,32x32/apps\n\n",
              index);
        fputs("[scalable/apps]\nSize=64\nMinSize=16\nMaxSize=256\nType=Scalable\nContext=Apps\n\n",
              index);
        fputs("[32x32/apps]\nSize=32\nType=Fixed\nContext=Apps\n", index);
        fclose(index);
    }
    char previous[1024] = {0};
    const char *old = getenv("XDG_DATA_HOME");
    if (old) {
        snprintf(previous, sizeof(previous), "%s", old);
    }
    setenv("XDG_DATA_HOME", root, 1);
    pt_icon_image *image = pt_icon_image_from_name("pt-svg-icon", 48);
    pt_check(image != NULL);
    if (image) {
        pt_check(pt_icon_image_width(image) == 8);
        pt_check(pt_icon_image_height(image) == 8);
        int stride = 0;
        const uint32_t *raster = pt_icon_image_raster(image, 48, 48, &stride);
        pt_check(raster != NULL && stride == 48);
        if (raster) {
            pt_check(raster[24 * 48 + 24] == 0xFFFF0000u);
        }
        pt_icon_image_destroy(image);
    } else {
        ++skipped_checks;
    }

    /* Without index.theme the scan must still discover scalable/. */
    pt_check(unlink(index_path) == 0);
    image = pt_icon_image_from_name("pt-svg-icon", 24);
    pt_check(image != NULL);
    if (image) {
        pt_check(pt_icon_image_width(image) == 8);
        pt_icon_image_destroy(image);
    } else {
        ++skipped_checks;
    }
    if (old) {
        setenv("XDG_DATA_HOME", previous, 1);
    } else {
        unsetenv("XDG_DATA_HOME");
    }
}

static void test_svg_load_loop(const char *root) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/loop.svg", root);
    FILE *file = fopen(path, "w");
    pt_check(file != NULL);
    if (!file) {
        return;
    }
    fputs(svg_rect, file);
    fclose(file);
    int before = open_fd_count();
    bool stable = true;
    for (int i = 0; i < 100; ++i) {
        pt_icon_image *image = pt_icon_image_load_file(path);
        if (!image) {
            stable = false;
            break;
        }
        int stride = 0;
        if (!pt_icon_image_raster(image, 32, 32, &stride) || stride != 32) {
            stable = false;
        }
        pt_icon_image_destroy(image);
    }
    pt_check(stable);
    int after = open_fd_count();
    pt_check(before >= 0);
    pt_check(after == before);
    pt_check(pt_icon_cache_entry_count() == 0);
}

static void test_button_layout(pt_app *app) {
    pt_window *window = pt_window_create(app, "icon-buttons", 360, 220);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 12.0f, 12.0f, 12.0f, 12.0f);
    pt_widget_set_gap(root, 10.0f);
    pt_icon_image *image = make_icon();
    pt_check(image != NULL);
    if (!image) {
        pt_window_free(window);
        return;
    }

    pt_widget *plain = pt_button_create(root, "Save file");
    pt_widget_set_align_self(plain, pt_align_start);
    pt_widget *with_icon = pt_button_create(root, "Save file");
    pt_widget_set_align_self(with_icon, pt_align_start);
    pt_button_set_icon_image(with_icon, image);
    pt_check(pt_button_get_icon_image(with_icon) == image);
    pt_widget *icon_only = pt_button_create(root, "");
    pt_widget_set_align_self(icon_only, pt_align_start);
    pt_button_set_icon_image(icon_only, image);
    pt_button_set_icon_tint(icon_only, 0xFF3366FFu);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 220, 1.0f);
    pt_check(canvas != NULL && pt_window_paint(window, canvas));
    pt_check(plain->width > 0.0f && with_icon->width > plain->width + 12.0f);
    pt_check(with_icon->height >= 18.0f);
    pt_check(icon_only->width < 64.0f);
    pt_check(icon_only->width >= 18.0f && icon_only->height >= 18.0f);

    uint64_t tinted_hash = 0;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) {
        tinted_hash = (tinted_hash ^ canvas->pixels[i]) * 1099511628211ull;
    }
    pt_button_set_icon_tint(icon_only, 0);
    pt_check(pt_window_paint(window, canvas));
    uint64_t plain_hash = 0;
    for (size_t i = 0; i < count; ++i) {
        plain_hash = (plain_hash ^ canvas->pixels[i]) * 1099511628211ull;
    }
    pt_check(tinted_hash != plain_hash);

    /* 2x rendering is deterministic and moves pixels. */
    pt_canvas *hidpi = pt_canvas_create(720, 440, 2.0f);
    pt_check(hidpi != NULL && pt_window_paint(window, hidpi));
    uint64_t hidpi_hash = 0;
    size_t hidpi_count = (size_t)hidpi->width * (size_t)hidpi->height;
    for (size_t i = 0; i < hidpi_count; ++i) {
        hidpi_hash = (hidpi_hash ^ hidpi->pixels[i]) * 1099511628211ull;
    }
    pt_check(pt_window_paint(window, hidpi));
    uint64_t hidpi_repeat = 0;
    for (size_t i = 0; i < hidpi_count; ++i) {
        hidpi_repeat = (hidpi_repeat ^ hidpi->pixels[i]) * 1099511628211ull;
    }
    pt_check(hidpi_hash == hidpi_repeat);
    pt_check(hidpi_hash != plain_hash);
    pt_check(pt_icon_cache_entry_count() > 0);

    /* NULL clears the slot and returns the layout to the text-only width. */
    pt_button_set_icon_image(with_icon, NULL);
    pt_check(pt_button_get_icon_image(with_icon) == NULL);
    pt_check(pt_window_paint(window, canvas));
    pt_check(with_icon->width == plain->width);
    pt_button_set_icon_image(icon_only, NULL);
    pt_icon_image_destroy(image);
    pt_check(pt_icon_cache_entry_count() == 0);

    pt_canvas_destroy(canvas);
    pt_canvas_destroy(hidpi);
    pt_window_free(window);
}

static void test_window_icon(pt_app *app) {
    pt_window *window = pt_window_create(app, "icon-window", 200, 120);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_icon_image *image = make_icon();
    pt_check(image != NULL);
    if (!image) {
        pt_window_free(window);
        return;
    }
    /* Headless has no xdg_toplevel_icon_v1 global: the setter must be a silent
     * no-op that still retains the image, and the getter must stay valid after
     * the caller drops its reference. */
    pt_window_set_icon(window, image);
    pt_check(pt_window_get_icon(window) == image);
    pt_icon_image_destroy(image);
    pt_check(pt_icon_image_width(pt_window_get_icon(window)) == 4);
    pt_window_set_icon(window, NULL);
    pt_check(pt_window_get_icon(window) == NULL);
    pt_check(pt_icon_cache_entry_count() == 0);

    int size = 0;
    int scale = 0;
    pt_check(pt_icon_window_buffer_geometry(48, 48, 2.0f, &size, &scale));
    pt_check(size == 64 && scale == 2);
    pt_check(pt_icon_window_buffer_geometry(100, 50, 1.0f, &size, &scale));
    pt_check(size == 32 && scale == 1);
    pt_check(pt_icon_window_buffer_geometry(100, 50, 3.0f, &size, &scale));
    pt_check(size == 96 && scale == 3);
    pt_check(pt_icon_window_buffer_geometry(100, 50, 9.0f, &size, &scale));
    pt_check(size == PT_ICON_WINDOW_SIZE * PT_ICON_WINDOW_MAX_SCALE);
    pt_check(scale == PT_ICON_WINDOW_MAX_SCALE);
    pt_check(!pt_icon_window_buffer_geometry(0, 10, 1.0f, &size, &scale));
    pt_check(!pt_icon_window_buffer_geometry(10, 10, 1.0f, NULL, &scale));
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    char root[512];
    if (!make_tree(root, sizeof(root))) {
        fprintf(stderr, "cannot create the icon test tree\n");
        return 1;
    }
    test_png_roundtrip(root);
    test_png_failures(root);
    test_cache_bounds();
    test_hicolor(root);
    test_svg_shapes();
    test_svg_cache();
    test_svg_failures();
    test_svg_hicolor(root);
    test_svg_load_loop(root);

    pt_app *app = pt_app_create("pt.tests.icon");
    pt_check(app != NULL);
    if (app) {
        test_button_layout(app);
        test_window_icon(app);
        pt_app_destroy(app);
    }
    remove_tree(root);
    return pt_test_finish("icon");
}
