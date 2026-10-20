/*
 * Minimal PNG writer used for tests, snapshots and visual regression.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void write_be32(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)value;
}

static int write_chunk(FILE *file, const char type[4], const uint8_t *data, uint32_t length) {
    uint8_t header[8];
    write_be32(header, length);
    memcpy(header + 4, type, 4);
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        return -1;
    }
    if (length > 0 && fwrite(data, 1, length, file) != length) {
        return -1;
    }
    uLong crc = crc32(0L, (const Bytef *)type, 4);
    if (length > 0) {
        crc = crc32(crc, data, length);
    }
    uint8_t crc_bytes[4];
    write_be32(crc_bytes, (uint32_t)crc);
    if (fwrite(crc_bytes, 1, sizeof(crc_bytes), file) != sizeof(crc_bytes)) {
        return -1;
    }
    return 0;
}

int pt_write_png_rgba(const char *path, int width, int height, const uint8_t *rgba) {
    if (!path || !rgba || width <= 0 || height <= 0) {
        pt_set_error("invalid PNG arguments");
        return -1;
    }
    size_t row_bytes = (size_t)width * 4u;
    size_t raw_size = (size_t)height * (row_bytes + 1u);
    uint8_t *raw = pt_alloc(raw_size);
    for (int row = 0; row < height; ++row) {
        size_t dst_offset = (size_t)row * (row_bytes + 1u);
        raw[dst_offset] = 0;
        memcpy(raw + dst_offset + 1, rgba + (size_t)row * row_bytes, row_bytes);
    }
    uLongf compressed_size = compressBound((uLong)raw_size);
    uint8_t *compressed = pt_alloc(compressed_size);
    int z_result = compress2(compressed, &compressed_size, raw, (uLong)raw_size, 6);
    free(raw);
    if (z_result != Z_OK) {
        free(compressed);
        pt_set_error("zlib failed to compress PNG data");
        return -1;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        free(compressed);
        pt_set_error("cannot open '%s' for writing", path);
        return -1;
    }
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    int status = 0;
    if (fwrite(signature, 1, sizeof(signature), file) != sizeof(signature)) {
        status = -1;
    }
    uint8_t ihdr[13];
    write_be32(ihdr, (uint32_t)width);
    write_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (status == 0) {
        status = write_chunk(file, "IHDR", ihdr, sizeof(ihdr));
    }
    if (status == 0) {
        status = write_chunk(file, "IDAT", compressed, (uint32_t)compressed_size);
    }
    if (status == 0) {
        status = write_chunk(file, "IEND", NULL, 0);
    }
    if (fclose(file) != 0 && status == 0) {
        status = -1;
    }
    free(compressed);
    if (status != 0) {
        pt_set_error("failed to write PNG '%s'", path);
    }
    return status;
}
