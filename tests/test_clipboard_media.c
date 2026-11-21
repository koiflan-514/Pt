/*
 * Clipboard media checks: image/png round trips with a bounded decode and
 * text/uri-list writes.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int open_fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        return -1;
    }
    int count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

static void put_be32(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)value;
}

static size_t put_chunk(uint8_t *buffer, const char type[4], const uint8_t *data,
                        uint32_t length) {
    put_be32(buffer, length);
    memcpy(buffer + 4, type, 4);
    if (length > 0) {
        memcpy(buffer + 8, data, length);
    }
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, buffer + 4, 4);
    if (length > 0) {
        crc = (uint32_t)crc32(crc, data, length);
    }
    put_be32(buffer + 8 + length, crc);
    return 12u + length;
}

/* Builds a syntactically valid PNG whose IHDR dimensions are arbitrary. The
 * payload is a single uncompressed filter byte so the decoder reaches the
 * dimension check without needing a real image. */
static size_t make_png_with_size(uint32_t width, uint32_t height, uint8_t *buffer,
                                 size_t capacity) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (capacity < 8u + 25u + 13u + 12u) {
        return 0;
    }
    uint8_t ihdr[13];
    put_be32(ihdr, width);
    put_be32(ihdr + 4, height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    const uint8_t idat[1] = {0};
    size_t offset = 0;
    memcpy(buffer, signature, sizeof(signature));
    offset += sizeof(signature);
    offset += put_chunk(buffer + offset, "IHDR", ihdr, sizeof(ihdr));
    offset += put_chunk(buffer + offset, "IDAT", idat, sizeof(idat));
    offset += put_chunk(buffer + offset, "IEND", NULL, 0);
    return offset;
}

static size_t make_png_with_inflated_payload(uint32_t width, uint32_t height,
                                             const uint8_t *raw, size_t raw_size,
                                             uint8_t *buffer, size_t capacity) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uLongf compressed_size = compressBound((uLong)raw_size);
    if (capacity < 8u + 25u + 12u + compressed_size + 12u) {
        return 0;
    }
    uint8_t *compressed = malloc(compressed_size + 1u);
    if (!compressed) {
        return 0;
    }
    if (compress2(compressed, &compressed_size, raw, (uLong)raw_size, 6) != Z_OK) {
        free(compressed);
        return 0;
    }
    uint8_t ihdr[13];
    put_be32(ihdr, width);
    put_be32(ihdr + 4, height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    size_t offset = 0;
    memcpy(buffer, signature, sizeof(signature));
    offset += sizeof(signature);
    offset += put_chunk(buffer + offset, "IHDR", ihdr, sizeof(ihdr));
    offset += put_chunk(buffer + offset, "IDAT", compressed, (uint32_t)compressed_size);
    offset += put_chunk(buffer + offset, "IEND", NULL, 0);
    free(compressed);
    return offset;
}

static void test_png_roundtrip(pt_app *app) {
    pt_window *window = pt_window_create(app, "clipboard png", 320, 200);
    pt_check(window != NULL);

    const pt_color pixels[] = {
        0x00000000u, /* fully transparent */
        0x00FFFFFFu, /* transparent with colour: encodes as zero */
        0x01FFFFFFu, /* alpha 1 with components above alpha: clamps */
        0x80402010u, /* half alpha */
        0x7F3F1F0Fu, /* just below half alpha */
        0xFF102030u, /* opaque */
        0xFFFFFFFFu, /* opaque white */
        0x80FF0000u, /* component above alpha: clamps to alpha */
    };
    pt_check(pt_window_clipboard_set_image_png(window, pixels, 4, 2));
    pt_check(pt_platform_clipboard_mime(window) != NULL);
    pt_check(strcmp(pt_platform_clipboard_mime(window), "image/png") == 0);
    pt_check(pt_window_clipboard_get(window) == NULL);

    int width = -1;
    int height = -1;
    pt_color *decoded = pt_window_clipboard_get_image_png(window, &width, &height);
    pt_check(decoded != NULL);
    pt_check(width == 4);
    pt_check(height == 2);
    if (decoded) {
        const pt_color expected[] = {
            0x00000000u, 0x00000000u, 0x01010101u, 0x80402010u,
            0x7F3F1F0Fu, 0xFF102030u, 0xFFFFFFFFu, 0x80800000u,
        };
        for (int i = 0; i < 8; ++i) {
            pt_check(decoded[i] == expected[i]);
        }
        pt_free(decoded);
    }

    /* A successful decode with NULL out parameters is allowed. */
    decoded = pt_window_clipboard_get_image_png(window, NULL, NULL);
    pt_check(decoded != NULL);
    pt_free(decoded);

    /* The clipboard holds one payload at a time. */
    pt_check(pt_window_clipboard_set(window, "replaces the image"));
    char *text = pt_window_clipboard_get(window);
    pt_check(text && strcmp(text, "replaces the image") == 0);
    pt_free(text);
    pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
    pt_check(width == 0 && height == 0);
    pt_check(strlen(pt_get_last_error()) > 0);

    /* Clearing removes every representation. */
    pt_check(pt_window_clipboard_set(window, NULL));
    pt_check(pt_platform_clipboard_mime(window) == NULL);
    pt_check(pt_window_clipboard_get(window) == NULL);
    pt_check(pt_window_clipboard_get_image_png(window, NULL, NULL) == NULL);

    pt_window_free(window);
}

static void test_png_errors(pt_app *app) {
    pt_window *window = pt_window_create(app, "clipboard png errors", 320, 200);
    pt_check(window != NULL);
    pt_color pixel = 0xFFFFFFFFu;

    pt_check(!pt_window_clipboard_set_image_png(NULL, &pixel, 1, 1));
    pt_check(strlen(pt_get_last_error()) > 0);
    pt_check(!pt_window_clipboard_set_image_png(window, NULL, 1, 1));
    pt_check(!pt_window_clipboard_set_image_png(window, &pixel, 0, 1));
    pt_check(!pt_window_clipboard_set_image_png(window, &pixel, 1, 0));
    pt_check(!pt_window_clipboard_set_image_png(window, &pixel, -1, 1));
    pt_check(!pt_window_clipboard_set_image_png(window, &pixel, 1, -1));
    pt_check(!pt_window_clipboard_set_image_png(window, &pixel,
                                               pt_clipboard_image_max_dimension + 1, 1));
    pt_check(!pt_window_clipboard_set_image_png(window, &pixel, 1,
                                               pt_clipboard_image_max_dimension + 1));
    pt_check(pt_platform_clipboard_mime(window) == NULL);

    pt_check(pt_window_clipboard_get_image_png(NULL, NULL, NULL) == NULL);
    pt_check(strlen(pt_get_last_error()) > 0);

    /* Corrupt signature. */
    static const uint8_t corrupt[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x00,
                                      0x00, 0x00, 0x00, 0x00};
    pt_check(pt_platform_clipboard_set_data(window, "image/png", corrupt,
                                            sizeof(corrupt)));
    int width = -1;
    int height = -1;
    pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
    pt_check(width == 0 && height == 0);
    pt_check(strlen(pt_get_last_error()) > 0);

    /* A valid PNG header one pixel over the decoded cap is rejected. */
    uint8_t png[1024];
    size_t size = make_png_with_size(pt_clipboard_image_max_dimension + 1u, 1u, png,
                                     sizeof(png));
    pt_check(size > 0);
    pt_check(pt_platform_clipboard_set_data(window, "image/png", png, size));
    pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
    pt_check(width == 0 && height == 0);
    pt_check(strlen(pt_get_last_error()) > 0);

    /* A dimension exactly at the cap passes the dimension check (the payload
     * is not a real image, so the decode still fails later). */
    size = make_png_with_size(pt_clipboard_image_max_dimension, 1u, png, sizeof(png));
    pt_check(size > 0);
    pt_check(pt_platform_clipboard_set_data(window, "image/png", png, size));
    pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
    pt_check(width == 0 && height == 0);

    /* A decompression bomb: a 1x1 image whose stream inflates to 256 bytes. */
    uint8_t bomb_raw[256] = {0};
    size = make_png_with_inflated_payload(1, 1, bomb_raw, sizeof(bomb_raw), png,
                                          sizeof(png));
    pt_check(size > 0);
    pt_check(pt_platform_clipboard_set_data(window, "image/png", png, size));
    pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
    pt_check(width == 0 && height == 0);
    pt_check(strlen(pt_get_last_error()) > 0);

    /* A PNG with a damaged CRC is rejected even when zlib could decode it. */
    size_t valid_size = 0;
    uint8_t *valid = pt_png_encode_argb_premultiplied(&pixel, 1, 1, &valid_size);
    pt_check(valid != NULL);
    if (valid) {
        valid[valid_size - 1] ^= 0xFFu; /* IEND CRC */
        pt_check(pt_platform_clipboard_set_data(window, "image/png", valid, valid_size));
        pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
        pt_check(width == 0 && height == 0);
        free(valid);
    }

    /* A non-image payload in the slot is reported, not decoded. */
    pt_check(pt_platform_clipboard_set_data(window, "text/plain", "not a png", 9));
    pt_check(pt_window_clipboard_get_image_png(window, &width, &height) == NULL);
    pt_check(width == 0 && height == 0);

    pt_window_free(window);
}

static void test_uri_list(pt_app *app) {
    const char *paths[] = {
        "/tmp/a b.txt",
        "/tmp/caf\xc3\xa9.txt",
        "/tmp/a#b?c%d[1].txt",
    };
    char *encoded = pt_uri_list_encode(paths, 3);
    const char *expected =
        "file:///tmp/a%20b.txt\r\n"
        "file:///tmp/caf%C3%A9.txt\r\n"
        "file:///tmp/a%23b%3Fc%25d%5B1%5D.txt\r\n";
    pt_check(encoded != NULL);
    if (encoded) {
        pt_check(strcmp(encoded, expected) == 0);
        free(encoded);
    }

    const char *preformed[] = {"file:///tmp/already%20encoded.txt",
                               "https://example.test/a?b=c"};
    encoded = pt_uri_list_encode(preformed, 2);
    pt_check(encoded != NULL);
    if (encoded) {
        pt_check(strcmp(encoded,
                        "file:///tmp/already%20encoded.txt\r\n"
                        "https://example.test/a?b=c\r\n") == 0);
        free(encoded);
    }

    pt_check(pt_uri_list_encode(NULL, 1) == NULL);
    pt_check(pt_uri_list_encode(paths, 0) == NULL);
    const char *relative[] = {"tmp/a.txt"};
    pt_check(pt_uri_list_encode(relative, 1) == NULL);
    const char *newline[] = {"/tmp/a\rb.txt"};
    pt_check(pt_uri_list_encode(newline, 1) == NULL);
    const char *empty[] = {""};
    pt_check(pt_uri_list_encode(empty, 1) == NULL);

    pt_window *window = pt_window_create(app, "clipboard uri", 320, 200);
    pt_check(window != NULL);
    pt_check(pt_window_clipboard_set_uri_list(window, paths, 3));
    pt_check(pt_platform_clipboard_mime(window) != NULL);
    pt_check(strcmp(pt_platform_clipboard_mime(window), "text/uri-list") == 0);
    size_t size = 0;
    const char *data = pt_platform_clipboard_data(window, &size);
    pt_check(data != NULL);
    pt_check(size == strlen(expected));
    if (data) {
        pt_check(strcmp(data, expected) == 0);
    }
    pt_check(pt_window_clipboard_get(window) == NULL);
    pt_check(pt_window_clipboard_get_image_png(window, NULL, NULL) == NULL);

    pt_check(!pt_window_clipboard_set_uri_list(NULL, paths, 3));
    pt_check(!pt_window_clipboard_set_uri_list(window, NULL, 3));
    pt_check(!pt_window_clipboard_set_uri_list(window, paths, 0));
    pt_check(!pt_window_clipboard_set_uri_list(window, relative, 1));

    pt_window_free(window);
}

static void test_mime_and_text(pt_app *app) {
    pt_window *window = pt_window_create(app, "clipboard mime", 320, 200);
    pt_check(window != NULL);
    pt_check(pt_window_clipboard_set(window, "plain text"));
    pt_check(pt_platform_clipboard_mime(window) != NULL);
    pt_check(strcmp(pt_platform_clipboard_mime(window),
                    "text/plain;charset=utf-8") == 0);
    char *text = pt_window_clipboard_get(window);
    pt_check(text && strcmp(text, "plain text") == 0);
    pt_free(text);

    pt_check(!pt_window_clipboard_set(NULL, "no window"));
    pt_check(pt_window_clipboard_get(NULL) == NULL);
    pt_check(pt_platform_clipboard_set_data(NULL, "image/png", "x", 1) == false);
    pt_check(pt_platform_clipboard_clear(NULL) == false);
    pt_check(pt_platform_clipboard_mime(NULL) == NULL);
    pt_check(pt_platform_clipboard_data(NULL, NULL) == NULL);

    /* Empty text is a valid payload; NULL clears. */
    pt_check(pt_window_clipboard_set(window, ""));
    pt_check(strcmp(pt_platform_clipboard_mime(window), "text/plain;charset=utf-8") == 0);
    text = pt_window_clipboard_get(window);
    pt_check(text && text[0] == '\0');
    pt_free(text);

    pt_window_free(window);
}

static void test_churn(pt_app *app) {
    pt_window *window = pt_window_create(app, "clipboard churn", 320, 200);
    pt_check(window != NULL);
    const pt_color pixels[] = {0x00000000u, 0xFFFFFFFFu, 0x80402010u, 0xFF102030u};
    const char *uris[] = {"/tmp/a b.txt", "/tmp/b#c.txt"};
    int before = open_fd_count();
    for (int i = 0; i < 128; ++i) {
        pt_check(pt_window_clipboard_set(window, "churn"));
        char *text = pt_window_clipboard_get(window);
        pt_check(text != NULL);
        pt_free(text);

        pt_check(pt_window_clipboard_set_image_png(window, pixels, 2, 2));
        int width = 0;
        int height = 0;
        pt_color *decoded = pt_window_clipboard_get_image_png(window, &width, &height);
        pt_check(decoded != NULL && width == 2 && height == 2);
        pt_free(decoded);

        pt_check(pt_window_clipboard_set_uri_list(window, uris, 2));
        size_t size = 0;
        pt_check(pt_platform_clipboard_data(window, &size) != NULL && size > 0);
    }
    int after = open_fd_count();
    if (before >= 0 && after >= 0) {
        pt_check(after == before);
    }
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.clipboard_media");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("clipboard_media");
    }
    test_png_roundtrip(app);
    test_png_errors(app);
    test_uri_list(app);
    test_mime_and_text(app);
    test_churn(app);
    pt_app_destroy(app);
    return pt_test_finish("clipboard_media");
}