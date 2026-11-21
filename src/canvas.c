/*
 * Pt software canvas. Pixels are premultiplied ARGB8888 (0xAARRGGBB in memory
 * on little-endian machines), matching WL_SHM_FORMAT_ARGB8888.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t pack_premultiplied(pt_color color) {
    uint32_t a = color & 0xFFu;
    if (a == 0) {
        return 0;
    }
    uint32_t r = (color >> 24) & 0xFFu;
    uint32_t g = (color >> 16) & 0xFFu;
    uint32_t b = (color >> 8) & 0xFFu;
    r = (r * a + 127u) / 255u;
    g = (g * a + 127u) / 255u;
    b = (b * a + 127u) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Coverage and the source alpha are combined here; src is premultiplied. */
static inline uint32_t blend_pixel_scaled(uint32_t dst, uint32_t src, uint32_t coverage) {
    if (coverage == 0) {
        return dst;
    }
    if (coverage > 255u) {
        coverage = 255u;
    }
    uint32_t src_alpha = (src >> 24) & 0xFFu;
    uint32_t alpha = (src_alpha * coverage + 127u) / 255u;
    if (alpha == 0) {
        return dst;
    }
    uint32_t inv = 255u - alpha;
    uint32_t dst_alpha = (dst >> 24) & 0xFFu;
    uint32_t out_alpha = alpha + (dst_alpha * inv + 127u) / 255u;
    if (out_alpha > 255u) {
        out_alpha = 255u;
    }
    uint32_t sr = (((src >> 16) & 0xFFu) * coverage + 127u) / 255u;
    uint32_t sg = (((src >> 8) & 0xFFu) * coverage + 127u) / 255u;
    uint32_t sb = ((src & 0xFFu) * coverage + 127u) / 255u;
    uint32_t out_r = sr + (((dst >> 16) & 0xFFu) * inv + 127u) / 255u;
    uint32_t out_g = sg + (((dst >> 8) & 0xFFu) * inv + 127u) / 255u;
    uint32_t out_b = sb + ((dst & 0xFFu) * inv + 127u) / 255u;
    out_r = out_r > 255u ? 255u : out_r;
    out_g = out_g > 255u ? 255u : out_g;
    out_b = out_b > 255u ? 255u : out_b;
    return (out_alpha << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

/* Source-over with full coverage: the source channels are already premultiplied,
 * so no source scaling is needed. */
static inline uint32_t blend_pixel_opaque(uint32_t dst, uint32_t src) {
    uint32_t alpha = src >> 24;
    if (alpha == 255u) {
        return src;
    }
    if (alpha == 0u) {
        return dst;
    }
    uint32_t inv = 255u - alpha;
    uint32_t out_alpha = alpha + (((dst >> 24) & 0xFFu) * inv + 127u) / 255u;
    uint32_t out_r = ((src >> 16) & 0xFFu) + ((((dst >> 16) & 0xFFu) * inv + 127u) / 255u);
    uint32_t out_g = ((src >> 8) & 0xFFu) + ((((dst >> 8) & 0xFFu) * inv + 127u) / 255u);
    uint32_t out_b = (src & 0xFFu) + (((dst & 0xFFu) * inv + 127u) / 255u);
    out_alpha = out_alpha > 255u ? 255u : out_alpha;
    out_r = out_r > 255u ? 255u : out_r;
    out_g = out_g > 255u ? 255u : out_g;
    out_b = out_b > 255u ? 255u : out_b;
    return (out_alpha << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

/* Fills [from, to) with one premultiplied color and one coverage value. Span
 * fills let the rasterizers skip per-pixel coverage math on pixels whose
 * coverage is provably 1, which is most of a large rounded surface. */
static void blend_span(uint32_t *dst, int from, int to, uint32_t src, uint32_t coverage) {
    if (from >= to || coverage == 0) {
        return;
    }
    if (coverage >= 255u) {
        uint32_t alpha = src >> 24;
        if (alpha == 255u) {
            for (int i = from; i < to; ++i) {
                dst[i] = src;
            }
            return;
        }
        if (alpha == 0u) {
            return;
        }
        for (int i = from; i < to; ++i) {
            dst[i] = blend_pixel_opaque(dst[i], src);
        }
        return;
    }
    for (int i = from; i < to; ++i) {
        dst[i] = blend_pixel_scaled(dst[i], src, coverage);
    }
}

static inline uint32_t coverage_u8(float coverage) {
    return (uint32_t)(coverage * 255.0f + 0.5f);
}

static inline float edge_coverage(float pixel_start, float pixel_end, float min_value,
                                  float max_value) {
    float lo = pixel_start > min_value ? pixel_start : min_value;
    float hi = pixel_end < max_value ? pixel_end : max_value;
    return hi > lo ? hi - lo : 0.0f;
}

void pt_canvas_init(pt_canvas *canvas, uint32_t *pixels, int width, int height, int stride,
                    float scale) {
    memset(canvas, 0, sizeof(*canvas));
    canvas->pixels = pixels;
    canvas->width = width;
    canvas->height = height;
    canvas->stride = stride;
    canvas->scale = scale > 0.0f ? scale : 1.0f;
    canvas->clip = (pt_rect){0.0f, 0.0f, (float)width, (float)height};
    canvas->clip_depth = 0;
    canvas->owns_pixels = false;
}

pt_canvas *pt_canvas_create(int width, int height, float scale) {
    if (width <= 0 || height <= 0) {
        pt_set_error("invalid canvas size %dx%d", width, height);
        return NULL;
    }
    pt_canvas *canvas = pt_alloc_zeroed(1, sizeof(*canvas));
    uint32_t *pixels = pt_alloc_zeroed((size_t)width * (size_t)height, sizeof(uint32_t));
    pt_canvas_init(canvas, pixels, width, height, width, scale);
    canvas->owns_pixels = true;
    return canvas;
}

void pt_canvas_destroy(pt_canvas *canvas) {
    if (!canvas) {
        return;
    }
    if (canvas->owns_pixels) {
        free(canvas->pixels);
    }
    free(canvas);
}

void pt_canvas_clear(pt_canvas *canvas, pt_color color) {
    uint32_t src = pack_premultiplied(color);
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    if (canvas->stride == canvas->width) {
        if (src == 0) {
            memset(canvas->pixels, 0, count * sizeof(uint32_t));
        } else {
            for (size_t i = 0; i < count; ++i) {
                canvas->pixels[i] = src;
            }
        }
        return;
    }
    for (int row = 0; row < canvas->height; ++row) {
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride;
        for (int column = 0; column < canvas->width; ++column) {
            dst[column] = src;
        }
    }
}

void pt_canvas_clear_rect(pt_canvas *canvas, float x, float y, float w, float h,
                          pt_color color) {
    if (!canvas || w <= 0.0f || h <= 0.0f) {
        return;
    }
    int clip_x0 = (int)canvas->clip.x;
    int clip_y0 = (int)canvas->clip.y;
    int clip_x1 = (int)ceilf(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)ceilf(canvas->clip.y + canvas->clip.h);
    int x0 = (int)floorf(x * canvas->scale);
    int y0 = (int)floorf(y * canvas->scale);
    int x1 = (int)ceilf((x + w) * canvas->scale);
    int y1 = (int)ceilf((y + h) * canvas->scale);
    if (x0 < clip_x0) {
        x0 = clip_x0;
    }
    if (y0 < clip_y0) {
        y0 = clip_y0;
    }
    if (x1 > clip_x1) {
        x1 = clip_x1;
    }
    if (y1 > clip_y1) {
        y1 = clip_y1;
    }
    if (x1 > canvas->width) {
        x1 = canvas->width;
    }
    if (y1 > canvas->height) {
        y1 = canvas->height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    uint32_t src = pack_premultiplied(color);
    for (int row = y0; row < y1; ++row) {
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride + x0;
        int count = x1 - x0;
        for (int i = 0; i < count; ++i) {
            dst[i] = src;
        }
    }
}

void pt_canvas_push_clip(pt_canvas *canvas, float x, float y, float w, float h) {
    if (canvas->clip_depth >= pt_clip_stack_max) {
        pt_log_warn("clip stack overflow");
        return;
    }
    canvas->clip_stack[canvas->clip_depth++] = canvas->clip;
    pt_rect logical = {x * canvas->scale, y * canvas->scale, w * canvas->scale,
                       h * canvas->scale};
    pt_rect inter = pt_rect_intersect(canvas->clip, logical);
    float ix0 = ceilf(inter.x);
    float iy0 = ceilf(inter.y);
    float ix1 = floorf(inter.x + inter.w);
    float iy1 = floorf(inter.y + inter.h);
    if (ix1 < ix0) {
        ix1 = ix0;
    }
    if (iy1 < iy0) {
        iy1 = iy0;
    }
    canvas->clip = (pt_rect){ix0, iy0, ix1 - ix0, iy1 - iy0};
}

void pt_canvas_pop_clip(pt_canvas *canvas) {
    if (canvas->clip_depth <= 0) {
        pt_log_warn("clip stack underflow");
        return;
    }
    canvas->clip = canvas->clip_stack[--canvas->clip_depth];
}

void pt_canvas_fill_rect(pt_canvas *canvas, float x, float y, float w, float h, pt_color color) {
    if (w <= 0.0f || h <= 0.0f || (color & 0xFFu) == 0) {
        return;
    }
    float dx0 = x * canvas->scale;
    float dy0 = y * canvas->scale;
    float dx1 = (x + w) * canvas->scale;
    float dy1 = (y + h) * canvas->scale;

    int clip_x0 = (int)canvas->clip.x;
    int clip_y0 = (int)canvas->clip.y;
    int clip_x1 = (int)ceilf(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)ceilf(canvas->clip.y + canvas->clip.h);

    int x0 = (int)floorf(dx0);
    int y0 = (int)floorf(dy0);
    int x1 = (int)ceilf(dx1);
    int y1 = (int)ceilf(dy1);
    if (x0 < clip_x0) {
        x0 = clip_x0;
    }
    if (y0 < clip_y0) {
        y0 = clip_y0;
    }
    if (x1 > clip_x1) {
        x1 = clip_x1;
    }
    if (y1 > clip_y1) {
        y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    uint32_t src = pack_premultiplied(color);
    uint32_t src_alpha = color & 0xFFu;

    bool fully_inside = dx0 <= (float)x0 && dx1 >= (float)x1 && dy0 <= (float)y0 &&
                        dy1 >= (float)y1;
    if (fully_inside && src_alpha == 255) {
        if (x0 == 0 && x1 == canvas->width) {
            size_t offset = (size_t)y0 * (size_t)canvas->stride;
            size_t count = (size_t)(y1 - y0) * (size_t)canvas->stride;
            for (size_t i = 0; i < count; ++i) {
                canvas->pixels[offset + i] = src;
            }
        } else {
            for (int row = y0; row < y1; ++row) {
                uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride + x0;
                int count = x1 - x0;
                for (int i = 0; i < count; ++i) {
                    dst[i] = src;
                }
            }
        }
        return;
    }

    for (int row = y0; row < y1; ++row) {
        float cov_y = edge_coverage((float)row, (float)row + 1.0f, dy0, dy1);
        if (cov_y <= 0.0f) {
            continue;
        }
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride;
        for (int column = x0; column < x1; ++column) {
            float cov_x = edge_coverage((float)column, (float)column + 1.0f, dx0, dx1);
            if (cov_x <= 0.0f) {
                continue;
            }
            uint32_t coverage = (uint32_t)lroundf(cov_x * cov_y * 255.0f);
            dst[column] = blend_pixel_scaled(dst[column], src, coverage);
        }
    }
}

/* Squared-distance fast path: the sqrtf is only evaluated for pixels inside the
 * one-pixel anti-aliasing band. Interior and distant exterior pixels exit on
 * squared comparisons, which keeps rounded surfaces cheap at 4K. */
static inline float rounded_box_coverage(float px, float py, float cx, float cy,
                                         float half_w, float half_h, float radius) {
    float qx = fabsf(px - cx) - half_w + radius;
    float qy = fabsf(py - cy) - half_h + radius;
    float mx = qx > qy ? qx : qy;
    if (mx <= 0.0f) {
        float cov = 0.5f - (mx - radius);
        return cov >= 1.0f ? 1.0f : (cov <= 0.0f ? 0.0f : cov);
    }
    float ax = qx > 0.0f ? qx : 0.0f;
    float ay = qy > 0.0f ? qy : 0.0f;
    if (ax > 0.0f && ay > 0.0f) {
        float outside_sq = ax * ax + ay * ay;
        float inner = radius - 0.5f;
        if (inner > 0.0f && outside_sq <= inner * inner) {
            return 1.0f;
        }
        float outer = radius + 0.5f;
        if (outside_sq >= outer * outer) {
            return 0.0f;
        }
        float cov = 0.5f - (sqrtf(outside_sq) - radius);
        return cov >= 1.0f ? 1.0f : (cov <= 0.0f ? 0.0f : cov);
    }
    float cov = 0.5f - (mx - radius);
    return cov >= 1.0f ? 1.0f : (cov <= 0.0f ? 0.0f : cov);
}

/* Shadows need the signed distance over the whole blur falloff, not only the
 * one-pixel coverage band, so they keep the exact Euclidean form. */
static inline float rounded_box_sdf(float px, float py, float cx, float cy, float half_w,
                                    float half_h, float radius) {
    float qx = fabsf(px - cx) - half_w + radius;
    float qy = fabsf(py - cy) - half_h + radius;
    float ax = qx > 0.0f ? qx : 0.0f;
    float ay = qy > 0.0f ? qy : 0.0f;
    float outside = sqrtf(ax * ax + ay * ay);
    float mx = qx > qy ? qx : qy;
    float inside = mx < 0.0f ? mx : 0.0f;
    return outside + inside - radius;
}

static void fill_rounded_rect_impl(pt_canvas *canvas, float x, float y, float w, float h,
                                   float radius, pt_color top, pt_color bottom) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    if (radius <= 0.25f) {
        pt_canvas_fill_rect(canvas, x, y, w, h, top);
        return;
    }
    float dx = x * canvas->scale;
    float dy = y * canvas->scale;
    float dw = w * canvas->scale;
    float dh = h * canvas->scale;
    float dr = radius * canvas->scale;
    if (dr > dw * 0.5f) {
        dr = dw * 0.5f;
    }
    if (dr > dh * 0.5f) {
        dr = dh * 0.5f;
    }
    pt_color base_top = top;
    pt_color base_bottom = bottom;
    bool gradient = base_top != base_bottom;

    int clip_x0 = (int)canvas->clip.x;
    int clip_y0 = (int)canvas->clip.y;
    int clip_x1 = (int)ceilf(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)ceilf(canvas->clip.y + canvas->clip.h);
    int x0 = (int)floorf(dx) - 1;
    int y0 = (int)floorf(dy) - 1;
    int x1 = (int)ceilf(dx + dw) + 1;
    int y1 = (int)ceilf(dy + dh) + 1;
    if (x0 < clip_x0) {
        x0 = clip_x0;
    }
    if (y0 < clip_y0) {
        y0 = clip_y0;
    }
    if (x1 > clip_x1) {
        x1 = clip_x1;
    }
    if (y1 > clip_y1) {
        y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    float cx = dx + dw * 0.5f;
    float cy = dy + dh * 0.5f;
    float half_w = dw * 0.5f;
    float half_h = dh * 0.5f;
    /* Interior coverage is exactly 1 only once the radius is at least half a
     * pixel, so tiny radii keep the per-pixel path. */
    bool span_ok = dr >= 1.0f;
    int corner_band = (int)ceilf(dr) + 2;

    for (int row = y0; row < y1; ++row) {
        float py = (float)row + 0.5f;
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride;
        pt_color base = base_top;
        if (gradient) {
            float t = pt_clamp_float((py - dy) / dh, 0.0f, 1.0f);
            base = pt_color_blend(base_top, base_bottom, t);
        }
        uint32_t base_alpha = base & 0xFFu;
        if (base_alpha == 0) {
            continue;
        }
        uint32_t premul = pack_premultiplied(base);

        int left_end = x0;
        int right_start = x0;
        if (span_ok) {
            bool corner_row = py < dy + dr || py > dy + dh - dr;
            int band = corner_row ? corner_band : 2;
            left_end = (int)floorf(dx) + band;
            right_start = (int)ceilf(dx + dw) - band;
            if (left_end < x0) {
                left_end = x0;
            } else if (left_end > x1) {
                left_end = x1;
            }
            if (right_start > x1) {
                right_start = x1;
            } else if (right_start < x0) {
                right_start = x0;
            }
            if (left_end >= right_start) {
                left_end = x0;
                right_start = x0;
            }
        }
        for (int column = x0; column < left_end; ++column) {
            float px = (float)column + 0.5f;
            float cov = rounded_box_coverage(px, py, cx, cy, half_w, half_h, dr);
            if (cov <= 0.0f) {
                continue;
            }
            dst[column] = blend_pixel_scaled(dst[column], premul, coverage_u8(cov));
        }
        for (int column = right_start; column < x1; ++column) {
            float px = (float)column + 0.5f;
            float cov = rounded_box_coverage(px, py, cx, cy, half_w, half_h, dr);
            if (cov <= 0.0f) {
                continue;
            }
            dst[column] = blend_pixel_scaled(dst[column], premul, coverage_u8(cov));
        }
        /* Middle columns share one coverage value; it only depends on py and
         * equals 1 except on the single AA row at the top and bottom edges. */
        float qy = fabsf(py - cy) - half_h + dr;
        float mid = pt_clamp_float(0.5f + dr - (qy > 0.0f ? qy : 0.0f), 0.0f, 1.0f);
        blend_span(dst, left_end, right_start, premul, coverage_u8(mid));
    }
}

void pt_canvas_fill_rounded_rect(pt_canvas *canvas, float x, float y, float w, float h,
                                 float radius, pt_color color) {
    fill_rounded_rect_impl(canvas, x, y, w, h, radius, color, color);
}

void pt_canvas_fill_rounded_rect_gradient(pt_canvas *canvas, float x, float y, float w, float h,
                                          float radius, pt_color top, pt_color bottom) {
    fill_rounded_rect_impl(canvas, x, y, w, h, radius, top, bottom);
}

void pt_canvas_stroke_rounded_rect(pt_canvas *canvas, float x, float y, float w, float h,
                                   float radius, float border_width, pt_color color) {
    if (border_width <= 0.0f || w <= 0.0f || h <= 0.0f || (color & 0xFFu) == 0) {
        return;
    }
    float dx = x * canvas->scale;
    float dy = y * canvas->scale;
    float dw = w * canvas->scale;
    float dh = h * canvas->scale;
    float dr = radius * canvas->scale;
    if (dr > dw * 0.5f) {
        dr = dw * 0.5f;
    }
    if (dr > dh * 0.5f) {
        dr = dh * 0.5f;
    }
    float bw = border_width * canvas->scale;

    int clip_x0 = (int)canvas->clip.x;
    int clip_y0 = (int)canvas->clip.y;
    int clip_x1 = (int)ceilf(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)ceilf(canvas->clip.y + canvas->clip.h);
    int x0 = (int)floorf(dx) - 1;
    int y0 = (int)floorf(dy) - 1;
    int x1 = (int)ceilf(dx + dw) + 1;
    int y1 = (int)ceilf(dy + dh) + 1;
    if (x0 < clip_x0) {
        x0 = clip_x0;
    }
    if (y0 < clip_y0) {
        y0 = clip_y0;
    }
    if (x1 > clip_x1) {
        x1 = clip_x1;
    }
    if (y1 > clip_y1) {
        y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    float cx = dx + dw * 0.5f;
    float cy = dy + dh * 0.5f;
    float half_w = dw * 0.5f;
    float half_h = dh * 0.5f;
    uint32_t premul = pack_premultiplied(color);

    float ix = dx + bw;
    float iy = dy + bw;
    float iw = dw - 2.0f * bw;
    float ih = dh - 2.0f * bw;
    float ir = pt_max_float(0.0f, dr - bw);
    float icx = ix + iw * 0.5f;
    float icy = iy + ih * 0.5f;
    bool has_inner = iw > 0.0f && ih > 0.0f;
    /* Only the ring carries coverage: the horizontal edge bands span every
     * column, and everywhere else the two curved sides form a band no wider
     * than max(radius, border width). Iterating those bands instead of the
     * whole rectangle keeps a one-pixel border proportional to its perimeter. */
    int edge_band = (int)ceilf(bw) + 3;
    int corner_band = (int)ceilf(pt_max_float(bw, dr)) + 3;

    for (int row = y0; row < y1; ++row) {
        float py = (float)row + 0.5f;
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride;
        /* Rows whose horizontal border edge is within half a pixel can still
         * bleed into the middle columns, so they stay on the full-row path. */
        bool horizontal_edge = py < dy + bw + 0.5f || py > dy + dh - bw - 0.5f;
        int left_end = x0;
        int right_start = x0;
        if (!horizontal_edge) {
            bool corner_row = py < dy + dr || py > dy + dh - dr;
            int band = corner_row ? corner_band : edge_band;
            left_end = (int)floorf(dx) + band;
            right_start = (int)ceilf(dx + dw) - band;
            if (left_end < x0) {
                left_end = x0;
            } else if (left_end > x1) {
                left_end = x1;
            }
            if (right_start > x1) {
                right_start = x1;
            } else if (right_start < x0) {
                right_start = x0;
            }
            if (left_end >= right_start) {
                left_end = x0;
                right_start = x0;
            }
        }
        for (int side = 0; side < 2; ++side) {
            int from = side == 0 ? x0 : right_start;
            int to = side == 0 ? left_end : x1;
            for (int column = from; column < to; ++column) {
                float px = (float)column + 0.5f;
                float out_cov = rounded_box_coverage(px, py, cx, cy, half_w, half_h, dr);
                if (out_cov <= 0.0f) {
                    continue;
                }
                float in_cov = 0.0f;
                if (has_inner) {
                    in_cov = rounded_box_coverage(px, py, icx, icy, iw * 0.5f, ih * 0.5f, ir);
                }
                float band = out_cov - in_cov;
                if (band <= 0.0f) {
                    continue;
                }
                dst[column] = blend_pixel_scaled(dst[column], premul,
                                                 coverage_u8(pt_clamp_float(band, 0.0f,
                                                                            1.0f)));
            }
        }
    }
}

void pt_canvas_fill_circle(pt_canvas *canvas, float cx, float cy, float radius, pt_color color) {
    if (radius <= 0.0f) {
        return;
    }
    pt_canvas_fill_rounded_rect(canvas, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f,
                                radius, color);
}

void pt_canvas_draw_line(pt_canvas *canvas, float x0, float y0, float x1, float y1,
                         float width, pt_color color) {
    if (width <= 0.0f || (color & 0xFFu) == 0) {
        return;
    }
    float ax = x0 * canvas->scale;
    float ay = y0 * canvas->scale;
    float bx = x1 * canvas->scale;
    float by = y1 * canvas->scale;
    float half = width * canvas->scale * 0.5f;
    float min_x = pt_min_float(ax, bx) - half - 1.0f;
    float min_y = pt_min_float(ay, by) - half - 1.0f;
    float max_x = pt_max_float(ax, bx) + half + 1.0f;
    float max_y = pt_max_float(ay, by) + half + 1.0f;

    int ix0 = (int)floorf(min_x);
    int iy0 = (int)floorf(min_y);
    int ix1 = (int)ceilf(max_x);
    int iy1 = (int)ceilf(max_y);
    ix0 = (int)pt_max_float((float)ix0, canvas->clip.x);
    iy0 = (int)pt_max_float((float)iy0, canvas->clip.y);
    ix1 = (int)pt_min_float((float)ix1, canvas->clip.x + canvas->clip.w);
    iy1 = (int)pt_min_float((float)iy1, canvas->clip.y + canvas->clip.h);
    if (ix0 >= ix1 || iy0 >= iy1) {
        return;
    }
    float dx = bx - ax;
    float dy = by - ay;
    float length_sq = dx * dx + dy * dy;
    uint32_t premul = pack_premultiplied(color);

    for (int row = iy0; row < iy1; ++row) {
        float py = (float)row + 0.5f;
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride;
        for (int column = ix0; column < ix1; ++column) {
            float px = (float)column + 0.5f;
            float t = 0.0f;
            if (length_sq > 0.0001f) {
                t = ((px - ax) * dx + (py - ay) * dy) / length_sq;
                t = pt_clamp_float(t, 0.0f, 1.0f);
            }
            float nearest_x = ax + dx * t;
            float nearest_y = ay + dy * t;
            float ddx = px - nearest_x;
            float ddy = py - nearest_y;
            float distance = sqrtf(ddx * ddx + ddy * ddy);
            float cov = pt_clamp_float(half + 0.5f - distance, 0.0f, 1.0f);
            if (cov <= 0.0f) {
                continue;
            }
            uint32_t coverage = (uint32_t)lroundf(cov * 255.0f);
            dst[column] = blend_pixel_scaled(dst[column], premul, coverage);
        }
    }
}

static inline uint32_t shadow_coverage(float d, float reach) {
    if (d <= 0.0f) {
        return 255u;
    }
    float t = pt_clamp_float(1.0f - d / reach, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    return (uint32_t)lroundf(t * 255.0f);
}

void pt_canvas_draw_shadow(pt_canvas *canvas, float x, float y, float w, float h, float radius,
                           float blur, pt_color color) {
    if (blur <= 0.0f || (color & 0xFFu) == 0 || w <= 0.0f || h <= 0.0f) {
        return;
    }
    float dx = x * canvas->scale;
    float dy = y * canvas->scale;
    float dw = w * canvas->scale;
    float dh = h * canvas->scale;
    float dr = radius * canvas->scale;
    float dblur = pt_max_float(1.0f, blur * canvas->scale);
    float cx = dx + dw * 0.5f;
    float cy = dy + dh * 0.5f;
    float half_w = dw * 0.5f;
    float half_h = dh * 0.5f;

    int clip_x0 = (int)canvas->clip.x;
    int clip_y0 = (int)canvas->clip.y;
    int clip_x1 = (int)ceilf(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)ceilf(canvas->clip.y + canvas->clip.h);
    int x0 = (int)floorf(dx - dblur * 2.0f);
    int y0 = (int)floorf(dy - dblur * 2.0f);
    int x1 = (int)ceilf(dx + dw + dblur * 2.0f);
    int y1 = (int)ceilf(dy + dh + dblur * 2.0f);
    if (x0 < clip_x0) {
        x0 = clip_x0;
    }
    if (y0 < clip_y0) {
        y0 = clip_y0;
    }
    if (x1 > clip_x1) {
        x1 = clip_x1;
    }
    if (y1 > clip_y1) {
        y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    uint32_t premul = pack_premultiplied(color);
    /* In the flat horizontal stretch of a row the SDF only depends on py, so
       the middle span shares one coverage value and only the rounded side
       bands need the per-pixel distance field. */
    bool span_ok = half_w > dr;
    int left_end = x0;
    int right_start = x0;
    if (span_ok) {
        left_end = (int)ceilf(cx - half_w + dr);
        right_start = (int)floorf(cx + half_w - dr);
        if (left_end < x0) {
            left_end = x0;
        }
        if (right_start > x1) {
            right_start = x1;
        }
        if (left_end > right_start) {
            left_end = x0;
            right_start = x0;
        }
    }
    float reach = dblur * 2.0f;
    for (int row = y0; row < y1; ++row) {
        float py = (float)row + 0.5f;
        float qy = fabsf(py - cy) - half_h + dr;
        float dmid = qy > 0.0f ? qy - dr : -dr;
        if (dmid > reach) {
            continue;
        }
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride;
        if (!span_ok) {
            left_end = x0;
            right_start = x0;
        }
        uint32_t mid_coverage = shadow_coverage(dmid, reach);
        if (left_end > x0) {
            for (int column = x0; column < left_end; ++column) {
                float px = (float)column + 0.5f;
                float d = rounded_box_sdf(px, py, cx, cy, half_w, half_h, dr);
                uint32_t coverage = shadow_coverage(d, reach);
                if (coverage == 0) {
                    continue;
                }
                dst[column] = blend_pixel_scaled(dst[column], premul, coverage);
            }
        }
        blend_span(dst, left_end, right_start, premul, mid_coverage);
        if (right_start < x1) {
            for (int column = right_start; column < x1; ++column) {
                float px = (float)column + 0.5f;
                float d = rounded_box_sdf(px, py, cx, cy, half_w, half_h, dr);
                uint32_t coverage = shadow_coverage(d, reach);
                if (coverage == 0) {
                    continue;
                }
                dst[column] = blend_pixel_scaled(dst[column], premul, coverage);
            }
        }
    }
}

void pt_canvas_blend_mask(pt_canvas *canvas, int x, int y, const uint8_t *coverage, int width,
                          int height, int stride, pt_color color, float opacity) {
    if (!coverage || width <= 0 || height <= 0 || (color & 0xFFu) == 0) {
        return;
    }
    int clip_x0 = (int)canvas->clip.x;
    int clip_y0 = (int)canvas->clip.y;
    int clip_x1 = (int)ceilf(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)ceilf(canvas->clip.y + canvas->clip.h);
    int x0 = 0;
    int x1 = width;
    if (x < clip_x0) {
        x0 = clip_x0 - x;
    }
    if (x + width > clip_x1) {
        x1 = clip_x1 - x;
    }
    int y0 = 0;
    int y1 = height;
    if (y < clip_y0) {
        y0 = clip_y0 - y;
    }
    if (y + height > clip_y1) {
        y1 = clip_y1 - y;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    uint32_t premul = pack_premultiplied(color);
    if (premul == 0) {
        return;
    }
    opacity = pt_clamp_float(opacity, 0.0f, 1.0f);
    uint8_t table[256];
    bool scaled = opacity < 1.0f;
    if (scaled) {
        for (int i = 0; i < 256; ++i) {
            table[i] = (uint8_t)lroundf((float)i * opacity);
        }
    }
    for (int row = y0; row < y1; ++row) {
        uint32_t *dst =
            canvas->pixels + (size_t)(y + row) * (size_t)canvas->stride + (size_t)x;
        const uint8_t *src = coverage + (size_t)row * (size_t)stride;
        for (int column = x0; column < x1; ++column) {
            uint32_t mask = scaled ? (uint32_t)table[src[column]] : (uint32_t)src[column];
            if (mask == 0) {
                continue;
            }
            dst[column] = mask == 255u ? blend_pixel_opaque(dst[column], premul)
                                       : blend_pixel_scaled(dst[column], premul, mask);
        }
    }
}

uint32_t pt_canvas_get_pixel(const pt_canvas *canvas, int x, int y) {
    if (!canvas || x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) {
        return 0;
    }
    uint32_t pixel = canvas->pixels[(size_t)y * (size_t)canvas->stride + (size_t)x];
    uint32_t a = pixel >> 24;
    uint32_t r = (pixel >> 16) & 0xFFu;
    uint32_t g = (pixel >> 8) & 0xFFu;
    uint32_t b = pixel & 0xFFu;
    if (a != 0 && a != 255) {
        r = (r * 255u + a / 2u) / a;
        g = (g * 255u + a / 2u) / a;
        b = (b * 255u + a / 2u) / a;
        r = r > 255u ? 255u : r;
        g = g > 255u ? 255u : g;
        b = b > 255u ? 255u : b;
    }
    return (r << 24) | (g << 16) | (b << 8) | a;
}

int pt_canvas_write_png(const pt_canvas *canvas, const char *path) {
    if (!canvas || !path) {
        return -1;
    }
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    uint8_t *rgba = pt_alloc(count * 4);
    for (int y = 0; y < canvas->height; ++y) {
        for (int x = 0; x < canvas->width; ++x) {
            uint32_t pixel = canvas->pixels[(size_t)y * (size_t)canvas->stride + (size_t)x];
            uint8_t a = (uint8_t)(pixel >> 24);
            uint8_t r = (uint8_t)(pixel >> 16);
            uint8_t g = (uint8_t)(pixel >> 8);
            uint8_t b = (uint8_t)pixel;
            if (a != 0 && a != 255) {
                r = (uint8_t)pt_min_float(255.0f, (float)r * 255.0f / (float)a + 0.5f);
                g = (uint8_t)pt_min_float(255.0f, (float)g * 255.0f / (float)a + 0.5f);
                b = (uint8_t)pt_min_float(255.0f, (float)b * 255.0f / (float)a + 0.5f);
            }
            size_t offset = ((size_t)y * (size_t)canvas->width + (size_t)x) * 4;
            rgba[offset + 0] = r;
            rgba[offset + 1] = g;
            rgba[offset + 2] = b;
            rgba[offset + 3] = a;
        }
    }
    int result = pt_write_png_rgba(path, canvas->width, canvas->height, rgba);
    free(rgba);
    return result;
}
