/*
 * Bounded SVG icon support: the vendored nanosvg parser and rasterizer behind
 * an internal API that enforces Pt's icon limits.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * nanosvg and nanosvgrast are third-party zlib-licensed headers; their
 * provenance banners and original notices live in src/nanosvg.h and
 * src/nanosvgrast.h. This file owns everything Pt-specific: the 16 MiB input
 * cap, the PT_ICON_MAX_DIMENSION intrinsic/raster cap, the empty-or-malformed
 * rejection rule, the straight-alpha to premultiplied conversion and the
 * aspect-preserving fit into the requested device pixel box.
 */

#include "internal.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION

/* Third-party code is compiled with Pt's full warning set; keep its warnings
 * local to the vendor include rather than silencing them for this file. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "nanosvg.h"
#include "nanosvgrast.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* The icon loader applies the same 16 MiB transfer cap it uses for PNG. An SVG
 * document is source text, so the cap is generous; the raster cap below is what
 * bounds decoded pixels. */
#define pt_svg_input_max_bytes (16u * 1024u * 1024u)

/* Parses a private, NUL-terminated copy of the document (nanosvg modifies its
 * input) and applies Pt's validity rules. Always sets the error on failure.
 * The caller owns the returned image and releases it with nsvgDelete. */
static NSVGimage *svg_parse(const void *data, size_t size) {
    if (!data || size == 0) {
        pt_set_error("empty SVG icon data");
        return NULL;
    }
    if (size > pt_svg_input_max_bytes) {
        pt_set_error("oversized SVG icon data");
        return NULL;
    }
    if (size == SIZE_MAX) {
        pt_set_error("oversized SVG icon data");
        return NULL;
    }
    char *text = pt_alloc(size + 1);
    memcpy(text, data, size);
    text[size] = '\0';
    NSVGimage *image = nsvgParse(text, "px", 96.0f);
    free(text);
    if (!image) {
        pt_set_error("cannot parse SVG icon data");
        return NULL;
    }
    if (!isfinite(image->width) || !isfinite(image->height) || image->width <= 0.0f ||
        image->height <= 0.0f) {
        nsvgDelete(image);
        pt_set_error("SVG icon has no usable intrinsic size");
        return NULL;
    }
    if (image->width > (float)PT_ICON_MAX_DIMENSION ||
        image->height > (float)PT_ICON_MAX_DIMENSION) {
        nsvgDelete(image);
        pt_set_error("SVG icon dimensions exceed %d", PT_ICON_MAX_DIMENSION);
        return NULL;
    }
    if (!image->shapes) {
        nsvgDelete(image);
        pt_set_error("SVG icon has no drawable shapes");
        return NULL;
    }
    return image;
}

/* Parses the document only to report its intrinsic size, rounded to whole
 * pixels. Zero is written on failure. */
bool pt_svg_measure(const void *data, size_t size, int *out_width, int *out_height) {
    if (out_width) {
        *out_width = 0;
    }
    if (out_height) {
        *out_height = 0;
    }
    if (!out_width || !out_height) {
        pt_set_error("missing SVG size output");
        return false;
    }
    NSVGimage *image = svg_parse(data, size);
    if (!image) {
        return false;
    }
    long width = lroundf(image->width);
    long height = lroundf(image->height);
    nsvgDelete(image);
    if (width < 1 || height < 1 || width > PT_ICON_MAX_DIMENSION ||
        height > PT_ICON_MAX_DIMENSION) {
        pt_set_error("SVG icon dimensions exceed %d", PT_ICON_MAX_DIMENSION);
        return false;
    }
    *out_width = (int)width;
    *out_height = (int)height;
    return true;
}

/* Converts one straight-alpha RGBA pixel from nanosvg into Pt's premultiplied
 * 0xAARRGGBB layout. Nanosvg returns straight RGBA (nsvg__unpremultiplyAlpha
 * runs after rasterisation), so a naive repack would produce colour components
 * above the alpha and break Pt's premultiplied blend path. */
static uint32_t svg_premultiply(const unsigned char *rgba) {
    uint32_t alpha = rgba[3];
    if (alpha == 0) {
        return 0;
    }
    uint32_t r = ((uint32_t)rgba[0] * alpha + 127u) / 255u;
    uint32_t g = ((uint32_t)rgba[1] * alpha + 127u) / 255u;
    uint32_t b = ((uint32_t)rgba[2] * alpha + 127u) / 255u;
    return (alpha << 24) | (r << 16) | (g << 8) | b;
}

/* Rasterises an SVG document into exactly width x height device pixels. The
 * intrinsic aspect ratio is preserved and the result is centred; pixels outside
 * the artwork stay fully transparent. Returns caller-owned premultiplied
 * 0xAARRGGBB pixels (release with pt_free) or NULL with the error set. */
pt_color *pt_svg_rasterize(const void *data, size_t size, int width, int height) {
    if (width <= 0 || height <= 0 || width > PT_ICON_MAX_DIMENSION ||
        height > PT_ICON_MAX_DIMENSION) {
        pt_set_error("SVG raster size %dx%d is out of range", width, height);
        return NULL;
    }
    NSVGimage *image = svg_parse(data, size);
    if (!image) {
        return NULL;
    }
    NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
    if (!rasterizer) {
        nsvgDelete(image);
        pt_set_error("cannot create the SVG rasterizer");
        return NULL;
    }
    size_t count = (size_t)width * (size_t)height;
    unsigned char *rgba = pt_alloc_zeroed(count, 4u);
    float scale_x = (float)width / image->width;
    float scale_y = (float)height / image->height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    float tx = ((float)width - image->width * scale) * 0.5f;
    float ty = ((float)height - image->height * scale) * 0.5f;
    nsvgRasterize(rasterizer, image, tx, ty, scale, rgba, width, height, width * 4);
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);

    uint32_t *pixels = pt_alloc(count * sizeof(uint32_t));
    for (size_t i = 0; i < count; ++i) {
        pixels[i] = svg_premultiply(rgba + i * 4u);
    }
    free(rgba);
    return (pt_color *)pixels;
}