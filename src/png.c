/*
 * Minimal PNG codec: an in-memory encoder used by snapshots and the clipboard,
 * and a bounded decoder for clipboard images.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* The clipboard transfer cap is 16 MiB; a PNG larger than that cannot arrive
 * through the clipboard path, so the decoder refuses it before parsing. */
#define pt_png_input_max_bytes (16u * 1024u * 1024u)
#define pt_png_max_dimension 8192

static void write_be32(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t *buffer) {
    return ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
}

static bool size_mul(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool size_add(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a) {
        return false;
    }
    *out = a + b;
    return true;
}

/* Premultiplied 0xAARRGGBB to straight RGBA. Transparent pixels have no
 * meaningful colour and encode as zero; a component that exceeds its alpha is
 * clamped, so malformed input cannot produce an out-of-range component. */
static uint8_t png_unpremultiply(uint8_t component, uint8_t alpha) {
    if (alpha == 0) {
        return 0;
    }
    uint32_t value = ((uint32_t)component * 255u + (uint32_t)alpha / 2u) /
                     (uint32_t)alpha;
    return value > 255u ? 255u : (uint8_t)value;
}

static uint8_t png_premultiply(uint8_t component, uint8_t alpha) {
    if (alpha == 255) {
        return component;
    }
    return (uint8_t)(((uint32_t)component * (uint32_t)alpha + 127u) / 255u);
}

static uint8_t *png_encode_rows(const uint8_t *rgba, int width, int height,
                                size_t *out_size) {
    size_t row_bytes = 0;
    size_t raw_size = 0;
    if (!size_mul((size_t)width, 4u, &row_bytes) ||
        !size_add(row_bytes, 1u, &row_bytes) ||
        !size_mul(row_bytes, (size_t)height, &raw_size)) {
        pt_set_error("PNG image is too large");
        return NULL;
    }
    uint8_t *raw = pt_alloc(raw_size);
    for (int row = 0; row < height; ++row) {
        size_t dst_offset = (size_t)row * row_bytes;
        raw[dst_offset] = 0;
        memcpy(raw + dst_offset + 1, rgba + (size_t)row * (size_t)width * 4u,
               (size_t)width * 4u);
    }
    uLongf compressed_size = compressBound((uLong)raw_size);
    uint8_t *compressed = pt_alloc(compressed_size);
    int z_result = compress2(compressed, &compressed_size, raw, (uLong)raw_size, 6);
    free(raw);
    if (z_result != Z_OK) {
        free(compressed);
        pt_set_error("zlib failed to compress PNG data");
        return NULL;
    }

    size_t total = 0;
    if (!size_add(8u, 12u + 13u, &total) ||
        !size_add(total, 12u + (size_t)compressed_size, &total) ||
        !size_add(total, 12u, &total)) {
        free(compressed);
        pt_set_error("PNG image is too large");
        return NULL;
    }
    uint8_t *png = pt_alloc(total);
    uint8_t *cursor = png;
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(cursor, signature, sizeof(signature));
    cursor += sizeof(signature);

    uint8_t ihdr[13];
    write_be32(ihdr, (uint32_t)width);
    write_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    write_be32(cursor, 13);
    memcpy(cursor + 4, "IHDR", 4);
    memcpy(cursor + 8, ihdr, sizeof(ihdr));
    uint32_t ihdr_crc = (uint32_t)crc32(0L, Z_NULL, 0);
    ihdr_crc = (uint32_t)crc32(ihdr_crc, cursor + 4, 4);
    ihdr_crc = (uint32_t)crc32(ihdr_crc, ihdr, sizeof(ihdr));
    write_be32(cursor + 8 + sizeof(ihdr), ihdr_crc);
    cursor += 12 + sizeof(ihdr);

    write_be32(cursor, (uint32_t)compressed_size);
    memcpy(cursor + 4, "IDAT", 4);
    if (compressed_size > 0) {
        memcpy(cursor + 8, compressed, compressed_size);
    }
    uint32_t idat_crc = (uint32_t)crc32(0L, Z_NULL, 0);
    idat_crc = (uint32_t)crc32(idat_crc, cursor + 4, 4);
    if (compressed_size > 0) {
        idat_crc = (uint32_t)crc32(idat_crc, compressed, (uInt)compressed_size);
    }
    write_be32(cursor + 8 + compressed_size, idat_crc);
    cursor += 12 + compressed_size;

    write_be32(cursor, 0);
    memcpy(cursor + 4, "IEND", 4);
    uint32_t iend_crc = (uint32_t)crc32(0L, Z_NULL, 0);
    iend_crc = (uint32_t)crc32(iend_crc, cursor + 4, 4);
    write_be32(cursor + 8, iend_crc);
    cursor += 12;

    free(compressed);
    *out_size = (size_t)(cursor - png);
    return png;
}

int pt_write_png_rgba(const char *path, int width, int height, const uint8_t *rgba) {
    if (!path || !rgba || width <= 0 || height <= 0) {
        pt_set_error("invalid PNG arguments");
        return -1;
    }
    size_t png_size = 0;
    uint8_t *png = png_encode_rows(rgba, width, height, &png_size);
    if (!png) {
        return -1;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(png);
        pt_set_error("cannot open '%s' for writing", path);
        return -1;
    }
    bool ok = fwrite(png, 1, png_size, file) == png_size;
    free(png);
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        pt_set_error("failed to write PNG '%s'", path);
        return -1;
    }
    return 0;
}

uint8_t *pt_png_encode_argb_premultiplied(const uint32_t *pixels, int width,
                                          int height, size_t *out_size) {
    if (!pixels || width <= 0 || height <= 0 || !out_size) {
        pt_set_error("invalid PNG image arguments");
        return NULL;
    }
    if (width > pt_png_max_dimension || height > pt_png_max_dimension ||
        (size_t)width > SIZE_MAX / (size_t)height) {
        pt_set_error("PNG image dimensions exceed %d", pt_png_max_dimension);
        return NULL;
    }
    size_t count = (size_t)width * (size_t)height;
    if (count > SIZE_MAX / 4u) {
        pt_set_error("PNG image is too large");
        return NULL;
    }
    uint8_t *rgba = pt_alloc(count * 4u);
    for (size_t i = 0; i < count; ++i) {
        uint32_t pixel = pixels[i];
        uint8_t a = (uint8_t)(pixel >> 24);
        rgba[i * 4u + 0u] = png_unpremultiply((uint8_t)(pixel >> 16), a);
        rgba[i * 4u + 1u] = png_unpremultiply((uint8_t)(pixel >> 8), a);
        rgba[i * 4u + 2u] = png_unpremultiply((uint8_t)pixel, a);
        rgba[i * 4u + 3u] = a;
    }
    uint8_t *png = png_encode_rows(rgba, width, height, out_size);
    free(rgba);
    return png;
}

typedef struct png_decode_state {
    const uint8_t *input;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t interlace;
    const uint8_t *palette;
    size_t palette_count;
    const uint8_t *transparency;
    size_t transparency_count;
    const uint8_t *idat;
    size_t idat_size;
} png_decode_state;

static bool png_chunk_crc_ok(const uint8_t *chunk, uint32_t length) {
    uint32_t expected = read_be32(chunk + 8u + length);
    uint32_t actual = (uint32_t)crc32(0L, Z_NULL, 0);
    actual = (uint32_t)crc32(actual, chunk + 4u, 4);
    if (length > 0) {
        actual = (uint32_t)crc32(actual, chunk + 8u, length);
    }
    return expected == actual;
}

static bool png_parse(png_decode_state *state) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!state || !state->input || state->size < sizeof(signature) ||
        memcmp(state->input, signature, sizeof(signature)) != 0) {
        return false;
    }
    size_t pos = sizeof(signature);
    bool have_ihdr = false;
    bool have_idat = false;
    bool have_iend = false;
    size_t idat_total = 0;
    while (pos + 12u <= state->size) {
        const uint8_t *chunk = state->input + pos;
        uint32_t length = read_be32(chunk);
        if (length > state->size - pos - 12u) {
            return false;
        }
        const uint8_t *type = chunk + 4;
        const uint8_t *data = chunk + 8;
        if (!png_chunk_crc_ok(chunk, length)) {
            return false;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (have_ihdr || length != 13) {
                return false;
            }
            state->width = read_be32(data);
            state->height = read_be32(data + 4);
            state->bit_depth = data[8];
            state->color_type = data[9];
            state->interlace = data[12];
            if (data[10] != 0 || data[11] != 0) {
                return false;
            }
            have_ihdr = true;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (length == 0 || length % 3u != 0 || length > 256u * 3u) {
                return false;
            }
            state->palette = data;
            state->palette_count = length / 3u;
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (length > 256u) {
                return false;
            }
            state->transparency = data;
            state->transparency_count = length;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (!have_ihdr) {
                return false;
            }
            if (!size_add(idat_total, length, &idat_total)) {
                return false;
            }
            have_idat = true;
        } else if (memcmp(type, "IEND", 4) == 0) {
            if (length != 0) {
                return false;
            }
            have_iend = true;
            pos += 12u;
            break;
        } else if ((type[0] & 0x20u) == 0) {
            /* Unknown critical chunk: a decoder must not ignore it. */
            return false;
        }
        pos += 12u + length;
    }
    if (!have_ihdr || !have_idat || !have_iend || idat_total == 0) {
        return false;
    }
    state->idat = pt_alloc(idat_total);
    state->idat_size = 0;
    pos = sizeof(signature);
    while (pos + 12u <= state->size) {
        const uint8_t *chunk = state->input + pos;
        uint32_t length = read_be32(chunk);
        if (length > state->size - pos - 12u) {
            break;
        }
        if (memcmp(chunk + 4, "IDAT", 4) == 0) {
            memcpy((uint8_t *)state->idat + state->idat_size, chunk + 8, length);
            state->idat_size += length;
        }
        if (memcmp(chunk + 4, "IEND", 4) == 0) {
            break;
        }
        pos += 12u + length;
    }
    return true;
}

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

static bool png_unfilter_row(uint8_t *row, const uint8_t *previous, size_t row_bytes,
                             size_t bpp, uint8_t filter) {
    for (size_t x = 0; x < row_bytes; ++x) {
        uint8_t left = x >= bpp ? row[x - bpp] : 0;
        uint8_t up = previous ? previous[x] : 0;
        uint8_t up_left = previous && x >= bpp ? previous[x - bpp] : 0;
        switch (filter) {
        case 0:
            break;
        case 1:
            row[x] = (uint8_t)(row[x] + left);
            break;
        case 2:
            row[x] = (uint8_t)(row[x] + up);
            break;
        case 3:
            row[x] = (uint8_t)(row[x] + ((uint16_t)left + up) / 2u);
            break;
        case 4:
            row[x] = (uint8_t)(row[x] + paeth_predictor(left, up, up_left));
            break;
        default:
            return false;
        }
    }
    return true;
}

pt_color *pt_png_decode_argb_premultiplied(const uint8_t *data, size_t size,
                                           int *out_width, int *out_height) {
    if (out_width) {
        *out_width = 0;
    }
    if (out_height) {
        *out_height = 0;
    }
    if (!data || size < 8u || size > pt_png_input_max_bytes) {
        pt_set_error("invalid or oversized PNG data");
        return NULL;
    }
    png_decode_state state = {.input = data, .size = size};
    if (!png_parse(&state)) {
        pt_set_error("corrupt PNG data");
        return NULL;
    }
    uint8_t channels = 0;
    switch (state.color_type) {
    case 0:
        channels = 1;
        break;
    case 2:
        channels = 3;
        break;
    case 3:
        channels = 1;
        break;
    case 4:
        channels = 2;
        break;
    case 6:
        channels = 4;
        break;
    default:
        pt_set_error("unsupported PNG colour type");
        free((void *)state.idat);
        return NULL;
    }
    if (state.bit_depth != 8 || state.interlace != 0) {
        pt_set_error("only non-interlaced 8-bit PNG images are supported");
        free((void *)state.idat);
        return NULL;
    }
    if (state.color_type == 3 && (!state.palette || state.palette_count == 0)) {
        pt_set_error("indexed PNG has no palette");
        free((void *)state.idat);
        return NULL;
    }
    if (state.width == 0 || state.height == 0 ||
        state.width > pt_png_max_dimension || state.height > pt_png_max_dimension) {
        pt_set_error("PNG dimensions exceed %d", pt_png_max_dimension);
        free((void *)state.idat);
        return NULL;
    }
    size_t row_bytes = 0;
    size_t raw_size = 0;
    if (!size_mul((size_t)state.width, channels, &row_bytes) ||
        !size_add(row_bytes, 1u, &row_bytes) ||
        !size_mul(row_bytes, (size_t)state.height, &raw_size)) {
        pt_set_error("PNG image is too large");
        free((void *)state.idat);
        return NULL;
    }
    uint8_t *raw = pt_alloc(raw_size);
    z_stream stream = {0};
    if (inflateInit(&stream) != Z_OK) {
        free(raw);
        free((void *)state.idat);
        pt_set_error("cannot initialise the PNG decoder");
        return NULL;
    }
    stream.next_in = (Bytef *)state.idat;
    stream.avail_in = (uInt)state.idat_size;
    stream.next_out = raw;
    stream.avail_out = (uInt)raw_size;
    int z_result = inflate(&stream, Z_FINISH);
    bool inflated = z_result == Z_STREAM_END && stream.total_out == raw_size;
    inflateEnd(&stream);
    free((void *)state.idat);
    if (!inflated) {
        free(raw);
        pt_set_error("corrupt or oversized PNG image data");
        return NULL;
    }

    size_t count = 0;
    size_t pixel_bytes = 0;
    if (!size_mul((size_t)state.width, (size_t)state.height, &count) ||
        !size_mul(count, sizeof(pt_color), &pixel_bytes)) {
        free(raw);
        pt_set_error("PNG image is too large");
        return NULL;
    }
    pt_color *pixels = pt_alloc(pixel_bytes);
    size_t stride = (size_t)state.width * channels;
    uint8_t *previous = NULL;
    for (uint32_t y = 0; y < state.height; ++y) {
        size_t offset = (size_t)y * (stride + 1u);
        uint8_t filter = raw[offset];
        uint8_t *row = raw + offset + 1u;
        if (!png_unfilter_row(row, previous, stride, channels, filter)) {
            free(pixels);
            free(raw);
            pt_set_error("corrupt PNG row filter");
            return NULL;
        }
        for (uint32_t x = 0; x < state.width; ++x) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 255;
            switch (state.color_type) {
            case 0:
                r = g = b = row[x];
                break;
            case 2:
                r = row[(size_t)x * 3u + 0u];
                g = row[(size_t)x * 3u + 1u];
                b = row[(size_t)x * 3u + 2u];
                break;
            case 3: {
                uint8_t index = row[x];
                if (index >= state.palette_count) {
                    free(pixels);
                    free(raw);
                    pt_set_error("PNG palette index out of range");
                    return NULL;
                }
                r = state.palette[(size_t)index * 3u + 0u];
                g = state.palette[(size_t)index * 3u + 1u];
                b = state.palette[(size_t)index * 3u + 2u];
                if (state.transparency && index < state.transparency_count) {
                    a = state.transparency[index];
                }
                break;
            }
            case 4:
                r = g = b = row[(size_t)x * 2u + 0u];
                a = row[(size_t)x * 2u + 1u];
                break;
            case 6:
                r = row[(size_t)x * 4u + 0u];
                g = row[(size_t)x * 4u + 1u];
                b = row[(size_t)x * 4u + 2u];
                a = row[(size_t)x * 4u + 3u];
                break;
            default:
                break;
            }
            pixels[(size_t)y * state.width + x] =
                ((uint32_t)a << 24) | ((uint32_t)png_premultiply(r, a) << 16) |
                ((uint32_t)png_premultiply(g, a) << 8) | png_premultiply(b, a);
        }
        previous = row;
    }
    free(raw);
    if (out_width) {
        *out_width = (int)state.width;
    }
    if (out_height) {
        *out_height = (int)state.height;
    }
    return pixels;
}