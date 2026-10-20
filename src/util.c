/*
 * Pt utilities: errors, memory, time, colors and UTF-8.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static _Thread_local char pt_error_buffer[512];

void pt_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(pt_error_buffer, sizeof(pt_error_buffer), fmt, args);
    va_end(args);
}

const char *pt_get_last_error(void) {
    return pt_error_buffer;
}

void pt_free(void *pointer) {
    free(pointer);
}

static int log_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *value = getenv("PT_LOG");
        cached = value != NULL && strcmp(value, "debug") == 0;
    }
    return cached;
}

void pt_log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("pt: warning: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

void pt_log_debug(const char *fmt, ...) {
    if (!log_enabled()) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    fputs("pt: debug: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

void *pt_alloc(size_t size) {
    void *memory = malloc(size == 0 ? 1 : size);
    if (!memory) {
        pt_set_error("out of memory");
        fprintf(stderr, "pt: fatal: out of memory\n");
        abort();
    }
    return memory;
}

void *pt_alloc_zeroed(size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        pt_set_error("allocation size overflow");
        fprintf(stderr, "pt: fatal: allocation size overflow\n");
        abort();
    }
    void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    if (!memory) {
        pt_set_error("out of memory");
        fprintf(stderr, "pt: fatal: out of memory\n");
        abort();
    }
    return memory;
}

void *pt_realloc_mem(void *ptr, size_t size) {
    void *memory = realloc(ptr, size == 0 ? 1 : size);
    if (!memory) {
        pt_set_error("out of memory");
        fprintf(stderr, "pt: fatal: out of memory\n");
        abort();
    }
    return memory;
}

char *pt_strdup(const char *text) {
    if (!text) {
        return NULL;
    }
    return pt_strdup_len(text, strlen(text));
}

char *pt_strdup_len(const char *text, size_t len) {
    char *copy = pt_alloc(len + 1);
    if (len > 0) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    return copy;
}

int64_t pt_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

double pt_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

float pt_ease_towards(float current, float target, float rate, float dt) {
    float factor = pt_clamp_float(rate * dt, 0.0f, 1.0f);
    float next = current + (target - current) * factor;
    if (fabsf(target - next) < pt_animation_epsilon) {
        next = target;
    }
    return next;
}

bool pt_rect_contains(pt_rect rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

bool pt_rect_intersects(pt_rect a, pt_rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

pt_rect pt_rect_intersect(pt_rect a, pt_rect b) {
    float x0 = pt_max_float(a.x, b.x);
    float y0 = pt_max_float(a.y, b.y);
    float x1 = pt_min_float(a.x + a.w, b.x + b.w);
    float y1 = pt_min_float(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) {
        return (pt_rect){x0, y0, 0.0f, 0.0f};
    }
    return (pt_rect){x0, y0, x1 - x0, y1 - y0};
}

void pt_marquee_begin(pt_marquee *marquee, float x, float y) {
    marquee->pressed = true;
    marquee->dragging = false;
    marquee->x0 = x;
    marquee->y0 = y;
    marquee->x1 = x;
    marquee->y1 = y;
}

bool pt_marquee_move(pt_marquee *marquee, float x, float y) {
    if (!marquee->pressed) {
        return false;
    }
    marquee->x1 = x;
    marquee->y1 = y;
    float dx = marquee->x1 - marquee->x0;
    float dy = marquee->y1 - marquee->y0;
    if (!marquee->dragging && dx * dx + dy * dy >= 16.0f) {
        marquee->dragging = true;
    }
    return marquee->dragging;
}

bool pt_marquee_end(pt_marquee *marquee) {
    bool was_dragging = marquee->dragging;
    marquee->pressed = false;
    marquee->dragging = false;
    return was_dragging;
}

pt_rect pt_marquee_rect(const pt_marquee *marquee) {
    float x0 = pt_min_float(marquee->x0, marquee->x1);
    float y0 = pt_min_float(marquee->y0, marquee->y1);
    return (pt_rect){x0, y0, pt_max_float(marquee->x0, marquee->x1) - x0,
                     pt_max_float(marquee->y0, marquee->y1) - y0};
}

bool pt_marquee_hit(const pt_marquee *marquee, pt_rect area) {
    return marquee->dragging && pt_rect_intersects(area, pt_marquee_rect(marquee));
}

static float mix_channel(float a, float b, float t) {
    return a + (b - a) * t;
}

pt_color pt_color_blend(pt_color a, pt_color b, float t) {
    t = pt_clamp_float(t, 0.0f, 1.0f);
    float ar = (float)((a >> 24) & 0xFFu);
    float ag = (float)((a >> 16) & 0xFFu);
    float ab = (float)((a >> 8) & 0xFFu);
    float aa = (float)(a & 0xFFu);
    float br = (float)((b >> 24) & 0xFFu);
    float bg = (float)((b >> 16) & 0xFFu);
    float bb = (float)((b >> 8) & 0xFFu);
    float ba = (float)(b & 0xFFu);
    uint32_t r = (uint32_t)lroundf(mix_channel(ar, br, t));
    uint32_t g = (uint32_t)lroundf(mix_channel(ag, bg, t));
    uint32_t bch = (uint32_t)lroundf(mix_channel(ab, bb, t));
    uint32_t alpha = (uint32_t)lroundf(mix_channel(aa, ba, t));
    return (r << 24) | (g << 16) | (bch << 8) | alpha;
}

pt_color pt_color_lighten(pt_color color, float amount) {
    if (amount < 0.0f) {
        return (pt_color_blend(color, (color & 0xFFFFFF00u) | 0xFFu, -amount) &
                0xFFFFFF00u) |
               (color & 0xFFu);
    }
    pt_color white = (color & 0xFFu) | 0xFFFFFF00u;
    pt_color result = pt_color_blend(color, white, amount);
    return (result & 0xFFFFFF00u) | (color & 0xFFu);
}

uint32_t pt_utf8_decode(const char *text, size_t len, size_t pos, size_t *next) {
    if (!text || pos >= len) {
        if (next) {
            *next = len;
        }
        return 0;
    }
    const unsigned char *s = (const unsigned char *)text;
    unsigned char c = s[pos];
    if (c < 0x80) {
        if (next) {
            *next = pos + 1;
        }
        return c;
    }
    size_t need = 0;
    uint32_t cp = 0;
    uint32_t minimum = 0;
    if ((c & 0xE0u) == 0xC0u) {
        need = 1;
        cp = c & 0x1Fu;
        minimum = 0x80;
    } else if ((c & 0xF0u) == 0xE0u) {
        need = 2;
        cp = c & 0x0Fu;
        minimum = 0x800;
    } else if ((c & 0xF8u) == 0xF0u) {
        need = 3;
        cp = c & 0x07u;
        minimum = 0x10000;
    } else {
        if (next) {
            *next = pos + 1;
        }
        return 0xFFFD;
    }
    if (pos + need >= len) {
        if (next) {
            *next = len;
        }
        return 0xFFFD;
    }
    for (size_t i = 1; i <= need; ++i) {
        unsigned char continuation = s[pos + i];
        if ((continuation & 0xC0u) != 0x80u) {
            if (next) {
                *next = pos + 1;
            }
            return 0xFFFD;
        }
        cp = (cp << 6) | (continuation & 0x3Fu);
    }
    if (cp < minimum || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        if (next) {
            *next = pos + 1;
        }
        return 0xFFFD;
    }
    if (next) {
        *next = pos + need + 1;
    }
    return cp;
}

uint32_t pt_utf8_decode_prev(const char *text, size_t pos, size_t *prev) {
    if (!text || pos == 0) {
        if (prev) {
            *prev = 0;
        }
        return 0;
    }
    size_t start = pos - 1;
    while (start > 0 && ((unsigned char)text[start] & 0xC0u) == 0x80u) {
        --start;
    }
    size_t next;
    uint32_t cp = pt_utf8_decode(text, pos, start, &next);
    if (next != pos) {
        cp = 0xFFFD;
        next = pos;
    }
    if (prev) {
        *prev = start;
    }
    return cp;
}

size_t pt_utf8_next_boundary(const char *text, size_t len, size_t pos) {
    size_t next;
    (void)pt_utf8_decode(text, len, pos, &next);
    return next;
}

size_t pt_utf8_prev_boundary(const char *text, size_t len, size_t pos) {
    (void)len;
    size_t prev;
    (void)pt_utf8_decode_prev(text, pos, &prev);
    return prev;
}

size_t pt_utf8_count(const char *text, size_t len) {
    size_t count = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t next;
        (void)pt_utf8_decode(text, len, pos, &next);
        if (next <= pos) {
            next = pos + 1;
        }
        pos = next;
        ++count;
    }
    return count;
}

bool pt_utf8_is_cjk(uint32_t codepoint) {
    return (codepoint >= 0x2E80u && codepoint <= 0x9FFFu) ||
           (codepoint >= 0xAC00u && codepoint <= 0xD7AFu) ||
           (codepoint >= 0xF900u && codepoint <= 0xFAFFu) ||
           (codepoint >= 0xFF00u && codepoint <= 0xFFEFu) ||
           (codepoint >= 0x20000u && codepoint <= 0x3FFFFu);
}
