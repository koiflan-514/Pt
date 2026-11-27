/*
 * Pt theme editor - pt-theme/1 document model, bounded codec and C exporter.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This module is deliberately self-contained: it includes only the frozen
 * theme_format.h (and therefore the public theme token table), allocates no
 * memory while parsing and never reads a locale. Numbers are parsed by hand and
 * formatted through a decimal-point sanitiser, so a host application's locale
 * cannot change the meaning or the bytes of a theme file.
 */

#include "theme_format.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A core role addition must update this file and its tables before the tool
 * builds again; that is the intended maintenance gate. */
static_assert(pt_theme_token_count == 38, "update the theme-editor colour name table");
static_assert(pt_theme_metric_count == 10, "update the theme-editor metric name table");

static const char *const te_color_names[pt_theme_token_count] = {
    [pt_theme_background] = "background",
    [pt_theme_surface] = "surface",
    [pt_theme_surface_raised] = "surface_raised",
    [pt_theme_surface_hover] = "surface_hover",
    [pt_theme_border] = "border",
    [pt_theme_border_strong] = "border_strong",
    [pt_theme_text] = "text",
    [pt_theme_text_muted] = "text_muted",
    [pt_theme_accent] = "accent",
    [pt_theme_accent_hover] = "accent_hover",
    [pt_theme_accent_text] = "accent_text",
    [pt_theme_danger] = "danger",
    [pt_theme_success] = "success",
    [pt_theme_warning] = "warning",
    [pt_theme_shadow] = "shadow",
    [pt_theme_titlebar] = "titlebar",
    [pt_theme_scrollbar] = "scrollbar",
    [pt_theme_focus] = "focus",
    [pt_theme_surface_sunken] = "surface_sunken",
    [pt_theme_control] = "control",
    [pt_theme_control_hover] = "control_hover",
    [pt_theme_accent_soft] = "accent_soft",
    [pt_theme_selection] = "selection",
    [pt_theme_track] = "track",
    [pt_theme_surface_container] = "surface_container",
    [pt_theme_surface_container_high] = "surface_container_high",
    [pt_theme_outline] = "outline",
    [pt_theme_outline_variant] = "outline_variant",
    [pt_theme_text_disabled] = "text_disabled",
    [pt_theme_accent_pressed] = "accent_pressed",
    [pt_theme_control_pressed] = "control_pressed",
    [pt_theme_danger_soft] = "danger_soft",
    [pt_theme_success_soft] = "success_soft",
    [pt_theme_warning_soft] = "warning_soft",
    [pt_theme_scrim] = "scrim",
    [pt_theme_skeleton] = "skeleton",
    [pt_theme_skeleton_highlight] = "skeleton_highlight",
    [pt_theme_selection_text] = "selection_text",
};

static const char *const te_metric_names[pt_theme_metric_count] = {
    [pt_theme_metric_radius_small] = "radius_small",
    [pt_theme_metric_radius_medium] = "radius_medium",
    [pt_theme_metric_radius_large] = "radius_large",
    [pt_theme_metric_radius_pill] = "radius_pill",
    [pt_theme_metric_focus_width] = "focus_width",
    [pt_theme_metric_focus_offset] = "focus_offset",
    [pt_theme_metric_elevation_low] = "elevation_low",
    [pt_theme_metric_elevation_high] = "elevation_high",
    [pt_theme_metric_chip_height] = "chip_height",
    [pt_theme_metric_control_height] = "control_height",
};

const char *te_color_name(pt_theme_token token) {
    if (token < 0 || token >= pt_theme_token_count) {
        return NULL;
    }
    return te_color_names[token];
}

const char *te_metric_name(pt_theme_metric metric) {
    if (metric < 0 || metric >= pt_theme_metric_count) {
        return NULL;
    }
    return te_metric_names[metric];
}

const char *te_scheme_name(pt_color_scheme scheme) {
    switch (scheme) {
    case pt_color_scheme_dark:
        return "dark";
    case pt_color_scheme_light:
        return "light";
    }
    return NULL;
}

static bool te_name_matches(const char *name, size_t length, const char *candidate) {
    return name != NULL && candidate != NULL && strlen(candidate) == length &&
           memcmp(name, candidate, length) == 0;
}

bool te_color_from_name(const char *name, size_t length, pt_theme_token *out) {
    if (name == NULL) {
        return false;
    }
    for (int i = 0; i < pt_theme_token_count; ++i) {
        if (te_name_matches(name, length, te_color_names[i])) {
            if (out != NULL) {
                *out = (pt_theme_token)i;
            }
            return true;
        }
    }
    return false;
}

bool te_metric_from_name(const char *name, size_t length, pt_theme_metric *out) {
    if (name == NULL) {
        return false;
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        if (te_name_matches(name, length, te_metric_names[i])) {
            if (out != NULL) {
                *out = (pt_theme_metric)i;
            }
            return true;
        }
    }
    return false;
}

bool te_scheme_from_name(const char *name, size_t length, pt_color_scheme *out) {
    if (te_name_matches(name, length, "dark")) {
        if (out != NULL) {
            *out = pt_color_scheme_dark;
        }
        return true;
    }
    if (te_name_matches(name, length, "light")) {
        if (out != NULL) {
            *out = pt_color_scheme_light;
        }
        return true;
    }
    return false;
}

const char *te_format_error_string(te_format_error error) {
    switch (error) {
    case TE_ERR_OK:
        return "ok";
    case TE_ERR_ARGUMENT:
        return "invalid argument";
    case TE_ERR_IO:
        return "file input/output error";
    case TE_ERR_TOO_LARGE:
        return "input exceeds the 1 MiB limit";
    case TE_ERR_DEPTH:
        return "JSON nesting exceeds the depth limit";
    case TE_ERR_SYNTAX:
        return "malformed JSON";
    case TE_ERR_SCHEMA:
        return "missing or unsupported schema";
    case TE_ERR_SCHEME:
        return "missing or invalid scheme";
    case TE_ERR_MISSING_KEY:
        return "missing required key";
    case TE_ERR_DUPLICATE_KEY:
        return "duplicate key";
    case TE_ERR_RANGE:
        return "metric out of range";
    case TE_ERR_NAME:
        return "invalid or oversized theme name";
    case TE_ERR_UNKNOWN_KEY:
        return "unknown key";
    case TE_ERR_NOT_IMPLEMENTED:
        return "not implemented";
    }
    return "unknown error";
}

void te_slugify_name(const char *name, char *out, size_t out_capacity) {
    if (out == NULL || out_capacity == 0) {
        return;
    }
    size_t used = 0;
    if (name != NULL) {
        for (const unsigned char *p = (const unsigned char *)name; *p != 0; ++p) {
            if (used + 1 >= out_capacity) {
                break;
            }
            unsigned char c = *p;
            if (c >= 'A' && c <= 'Z') {
                c = (unsigned char)(c - 'A' + 'a');
            }
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
                c = '_';
            }
            out[used++] = (char)c;
        }
    }
    out[used] = 0;
    if (used == 0) {
        (void)snprintf(out, out_capacity, "custom_theme");
    }
}

/* ------------------------------------------------------------------------- */
/* Bounded JSON reader                                                        */
/* ------------------------------------------------------------------------- */

#define TE_MAX_OBJECT_KEYS 64u
#define TE_KEY_MAX 128u
#define TE_COLOR_TEXT_MAX 32u
#define TE_SCHEMA_TEXT_MAX 16u
#define TE_SCHEME_TEXT_MAX 8u

typedef struct te_parser {
    const unsigned char *data;
    size_t length;
    size_t offset;
    size_t line;
    size_t column;
    bool strict;
    te_format_error error;
    te_parse_report *report;
    size_t key_line;
    size_t key_column;
} te_parser;

static int te_peek(const te_parser *p) {
    return p->offset < p->length ? (int)p->data[p->offset] : -1;
}

static void te_advance(te_parser *p) {
    if (p->offset >= p->length) {
        return;
    }
    if (p->data[p->offset] == '\n') {
        ++p->line;
        p->column = 1;
    } else {
        ++p->column;
    }
    ++p->offset;
}

static bool te_fail_at(te_parser *p, te_format_error error, size_t line, size_t column,
                       const char *format, ...) {
    if (p->error == TE_ERR_OK) {
        p->error = error;
        if (p->report != NULL) {
            p->report->error = error;
            p->report->line = line;
            p->report->column = column;
            va_list args;
            va_start(args, format);
            (void)vsnprintf(p->report->message, sizeof p->report->message, format, args);
            va_end(args);
        }
    }
    return false;
}

static bool te_fail(te_parser *p, te_format_error error, const char *format, ...) {
    if (p->error == TE_ERR_OK) {
        p->error = error;
        if (p->report != NULL) {
            p->report->error = error;
            p->report->line = p->line;
            p->report->column = p->column;
            va_list args;
            va_start(args, format);
            (void)vsnprintf(p->report->message, sizeof p->report->message, format, args);
            va_end(args);
        }
    }
    return false;
}

static void te_skip_ws(te_parser *p) {
    while (p->offset < p->length) {
        unsigned char c = p->data[p->offset];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            te_advance(p);
        } else {
            break;
        }
    }
}

static bool te_utf8_decode(const unsigned char *text, size_t length, size_t *index,
                           uint32_t *codepoint) {
    size_t i = *index;
    if (i >= length) {
        return false;
    }
    unsigned char c0 = text[i];
    if (c0 < 0x80) {
        *codepoint = c0;
        *index = i + 1;
        return true;
    }
    if (c0 < 0xC2) {
        return false;
    }
    if (c0 < 0xE0) {
        if (i + 1 >= length) {
            return false;
        }
        unsigned char c1 = text[i + 1];
        if ((c1 & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
        *index = i + 2;
        return true;
    }
    if (c0 < 0xF0) {
        if (i + 2 >= length) {
            return false;
        }
        unsigned char c1 = text[i + 1];
        unsigned char c2 = text[i + 2];
        uint32_t cp = ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6) |
                      (uint32_t)(c2 & 0x3Fu);
        if ((c2 & 0xC0) != 0x80) {
            return false;
        }
        if (c0 == 0xE0) {
            if (c1 < 0xA0 || c1 > 0xBF) {
                return false;
            }
        } else if (c0 == 0xED) {
            if (c1 < 0x80 || c1 > 0x9F) {
                return false;
            }
        } else if ((c1 & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = cp;
        *index = i + 3;
        return true;
    }
    if (c0 < 0xF5) {
        if (i + 3 >= length) {
            return false;
        }
        unsigned char c1 = text[i + 1];
        unsigned char c2 = text[i + 2];
        unsigned char c3 = text[i + 3];
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            return false;
        }
        if (c0 == 0xF0) {
            if (c1 < 0x90 || c1 > 0xBF) {
                return false;
            }
        } else if (c0 == 0xF4) {
            if (c1 < 0x80 || c1 > 0x8F) {
                return false;
            }
        } else if ((c1 & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = ((uint32_t)(c0 & 0x07u) << 18) | ((uint32_t)(c1 & 0x3Fu) << 12) |
                     ((uint32_t)(c2 & 0x3Fu) << 6) | (uint32_t)(c3 & 0x3Fu);
        *index = i + 4;
        return true;
    }
    return false;
}

static size_t te_utf8_encode(uint32_t cp, unsigned char out[4]) {
    if (cp < 0x80) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0u | (cp >> 6));
        out[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0u | (cp >> 12));
        out[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (unsigned char)(0xF0u | (cp >> 18));
    out[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
    return 4;
}

static bool te_hex4(te_parser *p, uint32_t *value) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        int c = te_peek(p);
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A' + 10);
        } else {
            return te_fail(p, TE_ERR_SYNTAX, "invalid \\u escape");
        }
        result = (result << 4) | digit;
        te_advance(p);
    }
    *value = result;
    return true;
}

/* Decodes one JSON string at the current quote into out (bounded, NUL
 * terminated). out may be NULL when the value is skipped. Invalid UTF-8 in a
 * theme name maps to TE_ERR_NAME; every other lexical failure is TE_ERR_SYNTAX.
 * Buffer overflow is reported through *overflow_out and the whole string is
 * still validated, so callers choose the error code and position. */
static bool te_parse_string(te_parser *p, char *out, size_t capacity, size_t *out_length,
                            bool *overflow_out, bool name_mode) {
    if (te_peek(p) != '"') {
        return te_fail(p, TE_ERR_SYNTAX, "expected a string");
    }
    te_advance(p);
    size_t used = 0;
    bool overflow = false;
    if (overflow_out != NULL) {
        *overflow_out = false;
    }
    for (;;) {
        if (p->offset >= p->length) {
            return te_fail(p, TE_ERR_SYNTAX, "unterminated string");
        }
        unsigned char c = p->data[p->offset];
        unsigned char encoded[4];
        size_t encoded_length = 0;
        if (c == '"') {
            te_advance(p);
            break;
        }
        if (c == '\\') {
            te_advance(p);
            int escape = te_peek(p);
            if (escape < 0) {
                return te_fail(p, TE_ERR_SYNTAX, "unterminated escape");
            }
            te_advance(p);
            switch (escape) {
            case '"':
                encoded[0] = '"';
                encoded_length = 1;
                break;
            case '\\':
                encoded[0] = '\\';
                encoded_length = 1;
                break;
            case '/':
                encoded[0] = '/';
                encoded_length = 1;
                break;
            case 'b':
                encoded[0] = '\b';
                encoded_length = 1;
                break;
            case 'f':
                encoded[0] = '\f';
                encoded_length = 1;
                break;
            case 'n':
                encoded[0] = '\n';
                encoded_length = 1;
                break;
            case 'r':
                encoded[0] = '\r';
                encoded_length = 1;
                break;
            case 't':
                encoded[0] = '\t';
                encoded_length = 1;
                break;
            case 'u': {
                uint32_t cp;
                if (!te_hex4(p, &cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (te_peek(p) != '\\') {
                        return te_fail(p, TE_ERR_SYNTAX, "unpaired high surrogate");
                    }
                    te_advance(p);
                    if (te_peek(p) != 'u') {
                        return te_fail(p, TE_ERR_SYNTAX, "unpaired high surrogate");
                    }
                    te_advance(p);
                    uint32_t low;
                    if (!te_hex4(p, &low)) {
                        return false;
                    }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        return te_fail(p, TE_ERR_SYNTAX, "unpaired high surrogate");
                    }
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return te_fail(p, TE_ERR_SYNTAX, "unpaired low surrogate");
                }
                encoded_length = te_utf8_encode(cp, encoded);
                break;
            }
            default:
                return te_fail(p, TE_ERR_SYNTAX, "invalid escape sequence");
            }
        } else if (c < 0x20) {
            return te_fail(p, TE_ERR_SYNTAX, "control character in string");
        } else if (c < 0x80) {
            encoded[0] = c;
            encoded_length = 1;
            te_advance(p);
        } else {
            size_t index = p->offset;
            uint32_t cp;
            if (!te_utf8_decode(p->data, p->length, &index, &cp)) {
                if (name_mode) {
                    return te_fail(p, TE_ERR_NAME, "theme name is not valid UTF-8");
                }
                return te_fail(p, TE_ERR_SYNTAX, "invalid UTF-8 in string");
            }
            size_t consumed = index - p->offset;
            memcpy(encoded, p->data + p->offset, consumed);
            encoded_length = consumed;
            for (size_t i = 0; i < consumed; ++i) {
                te_advance(p);
            }
        }
        if (out != NULL && !overflow) {
            if (used + encoded_length >= capacity) {
                overflow = true;
            } else {
                memcpy(out + used, encoded, encoded_length);
                used += encoded_length;
            }
        }
    }
    if (out_length != NULL) {
        *out_length = used;
    }
    if (out != NULL) {
        out[used] = 0;
    }
    if (overflow_out != NULL) {
        *overflow_out = overflow;
    }
    return true;
}/* ------------------------------------------------------------------------- */
/* Scalar validation                                                          */
/* ------------------------------------------------------------------------- */

static void te_format_float(float value, char *buffer, size_t capacity);

static int te_hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Accepts [#]RRGGBB, [#]RRGGBBAA, 0xRRGGBB and 0xRRGGBBAA, any case, with
 * surrounding JSON whitespace. A 6-digit value gets alpha FF. */
static bool te_parse_color_text(const char *text, size_t length, pt_color *out) {
    size_t begin = 0;
    size_t end = length;
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                           text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    if (begin + 1 < end && text[begin] == '0' &&
        (text[begin + 1] == 'x' || text[begin + 1] == 'X')) {
        begin += 2;
    } else if (begin < end && text[begin] == '#') {
        ++begin;
    }
    size_t digits = end - begin;
    if (digits != 6 && digits != 8) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = begin; i < end; ++i) {
        int digit = te_hex_digit((unsigned char)text[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    if (digits == 6) {
        value = (value << 8) | 0xFFu;
    }
    *out = (pt_color)value;
    return true;
}

/* Validates a JSON number at the cursor. Metric numbers reject exponent
 * notation; numbers inside ignored values follow full JSON grammar. */
static bool te_scan_number(te_parser *p, bool allow_exponent) {
    int c = te_peek(p);
    if (c == '-') {
        te_advance(p);
        c = te_peek(p);
    }
    if (c == '0') {
        te_advance(p);
        int next = te_peek(p);
        if (next >= '0' && next <= '9') {
            return te_fail(p, TE_ERR_SYNTAX, "leading zero in number");
        }
    } else if (c >= '1' && c <= '9') {
        while (te_peek(p) >= '0' && te_peek(p) <= '9') {
            te_advance(p);
        }
    } else {
        return te_fail(p, TE_ERR_SYNTAX, "expected a digit");
    }
    if (te_peek(p) == '.') {
        te_advance(p);
        if (te_peek(p) < '0' || te_peek(p) > '9') {
            return te_fail(p, TE_ERR_SYNTAX, "expected a digit after the decimal point");
        }
        while (te_peek(p) >= '0' && te_peek(p) <= '9') {
            te_advance(p);
        }
    }
    c = te_peek(p);
    if (c == 'e' || c == 'E') {
        if (!allow_exponent) {
            return te_fail(p, TE_ERR_SYNTAX,
                           "exponent notation is not allowed for metrics");
        }
        te_advance(p);
        c = te_peek(p);
        if (c == '+' || c == '-') {
            te_advance(p);
        }
        if (te_peek(p) < '0' || te_peek(p) > '9') {
            return te_fail(p, TE_ERR_SYNTAX, "expected an exponent digit");
        }
        while (te_peek(p) >= '0' && te_peek(p) <= '9') {
            te_advance(p);
        }
    }
    return true;
}

static bool te_parse_metric_value(te_parser *p, float *out) {
    size_t line = p->line;
    size_t column = p->column;
    int c = te_peek(p);
    if (c != '-' && (c < '0' || c > '9')) {
        return te_fail(p, TE_ERR_SYNTAX, "metric value must be a JSON number");
    }
    size_t start = p->offset;
    if (!te_scan_number(p, false)) {
        return false;
    }
    size_t end = p->offset;
    bool negative = p->data[start] == '-';
    size_t i = negative ? start + 1 : start;
    double value = 0.0;
    bool overflow = false;
    while (i < end && p->data[i] >= '0' && p->data[i] <= '9') {
        if (!overflow) {
            value = value * 10.0 + (double)(p->data[i] - '0');
            if (value > 1.0e38) {
                overflow = true;
            }
        }
        ++i;
    }
    if (i < end && p->data[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < end && p->data[i] >= '0' && p->data[i] <= '9') {
            if (!overflow) {
                value += (double)(p->data[i] - '0') * scale;
                if (value > 1.0e38) {
                    overflow = true;
                }
            }
            scale *= 0.1;
            if (scale < 1.0e-300) {
                scale = 0.0;
            }
            ++i;
        }
    }
    if (overflow || value > (double)FLT_MAX) {
        return te_fail_at(p, TE_ERR_RANGE, line, column, "metric is too large");
    }
    if (negative && value != 0.0) {
        return te_fail_at(p, TE_ERR_RANGE, line, column, "metric must not be negative");
    }
    float result = (float)value;
    if (!isfinite(result)) {
        return te_fail_at(p, TE_ERR_RANGE, line, column, "metric must be finite");
    }
    if (result == 0.0f && value != 0.0) {
        return te_fail_at(p, TE_ERR_RANGE, line, column, "metric underflows float");
    }
    char formatted[64];
    te_format_float(result, formatted, sizeof formatted);
    if (strchr(formatted, 'e') != NULL || strchr(formatted, 'E') != NULL) {
        return te_fail_at(p, TE_ERR_RANGE, line, column,
                          "metric has no canonical fixed-point representation");
    }
    *out = result;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Container reader                                                           */
/* ------------------------------------------------------------------------- */

typedef bool (*te_member_fn)(te_parser *p, const char *key, size_t key_length, void *context,
                             size_t depth);

static bool te_parse_value(te_parser *p, size_t depth);
static bool te_parse_skipped_object(te_parser *p, size_t depth);

static bool te_parse_array(te_parser *p, size_t open_depth) {
    te_advance(p); /* '[' */
    te_skip_ws(p);
    if (te_peek(p) == ']') {
        te_advance(p);
        return true;
    }
    for (;;) {
        if (!te_parse_value(p, open_depth)) {
            return false;
        }
        te_skip_ws(p);
        int c = te_peek(p);
        if (c == ',') {
            te_advance(p);
            te_skip_ws(p);
            continue;
        }
        if (c == ']') {
            te_advance(p);
            return true;
        }
        return te_fail(p, TE_ERR_SYNTAX, "expected ',' or ']' in array");
    }
}

static bool te_parse_object(te_parser *p, size_t open_depth, te_member_fn member,
                            void *context) {
    te_advance(p); /* '{' */
    te_skip_ws(p);
    if (te_peek(p) == '}') {
        te_advance(p);
        return true;
    }
    char seen[TE_MAX_OBJECT_KEYS][TE_KEY_MAX];
    size_t count = 0;
    for (;;) {
        if (te_peek(p) != '"') {
            return te_fail(p, TE_ERR_SYNTAX, "expected an object key");
        }
        size_t key_line = p->line;
        size_t key_column = p->column;
        char key[TE_KEY_MAX];
        size_t key_length = 0;
        bool overflow = false;
        if (!te_parse_string(p, key, sizeof key, &key_length, &overflow, false)) {
            return false;
        }
        if (overflow) {
            return te_fail_at(p, TE_ERR_SYNTAX, key_line, key_column,
                              "object key exceeds %u bytes", TE_KEY_MAX - 1u);
        }
        if (memchr(key, 0, key_length) != NULL) {
            return te_fail_at(p, TE_ERR_SYNTAX, key_line, key_column,
                              "object key contains a NUL");
        }
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(key, seen[i]) == 0) {
                return te_fail_at(p, TE_ERR_DUPLICATE_KEY, key_line, key_column,
                                  "duplicate key \"%.*s\"", (int)key_length, key);
            }
        }
        if (count >= TE_MAX_OBJECT_KEYS) {
            return te_fail_at(p, TE_ERR_SYNTAX, key_line, key_column,
                              "object has more than %u keys", TE_MAX_OBJECT_KEYS);
        }
        memcpy(seen[count], key, key_length + 1);
        ++count;
        te_skip_ws(p);
        if (te_peek(p) != ':') {
            return te_fail(p, TE_ERR_SYNTAX, "expected ':' after object key");
        }
        te_advance(p);
        te_skip_ws(p);
        p->key_line = key_line;
        p->key_column = key_column;
        if (!member(p, key, key_length, context, open_depth)) {
            return false;
        }
        te_skip_ws(p);
        int c = te_peek(p);
        if (c == ',') {
            te_advance(p);
            te_skip_ws(p);
            continue;
        }
        if (c == '}') {
            te_advance(p);
            return true;
        }
        return te_fail(p, TE_ERR_SYNTAX, "expected ',' or '}' in object");
    }
}

static bool te_parse_object_value(te_parser *p, size_t depth, te_member_fn member,
                                  void *context) {
    if (depth >= TE_THEME_MAX_DEPTH) {
        return te_fail(p, TE_ERR_DEPTH, "JSON nesting exceeds %u containers",
                       TE_THEME_MAX_DEPTH);
    }
    if (te_peek(p) != '{') {
        return te_fail(p, TE_ERR_SYNTAX, "expected an object");
    }
    return te_parse_object(p, depth + 1, member, context);
}

static bool te_parse_array_value(te_parser *p, size_t depth) {
    if (depth >= TE_THEME_MAX_DEPTH) {
        return te_fail(p, TE_ERR_DEPTH, "JSON nesting exceeds %u containers",
                       TE_THEME_MAX_DEPTH);
    }
    if (te_peek(p) != '[') {
        return te_fail(p, TE_ERR_SYNTAX, "expected an array");
    }
    return te_parse_array(p, depth + 1);
}

static bool te_parse_literal(te_parser *p, const char *word) {
    size_t length = strlen(word);
    if (p->length - p->offset < length ||
        memcmp(p->data + p->offset, word, length) != 0) {
        return te_fail(p, TE_ERR_SYNTAX, "invalid literal");
    }
    for (size_t i = 0; i < length; ++i) {
        te_advance(p);
    }
    return true;
}

static bool te_parse_value(te_parser *p, size_t depth) {
    te_skip_ws(p);
    int c = te_peek(p);
    if (c == '{') {
        return te_parse_skipped_object(p, depth);
    }
    if (c == '[') {
        return te_parse_array_value(p, depth);
    }
    if (c == '"') {
        size_t dummy_length = 0;
        bool dummy_overflow = false;
        return te_parse_string(p, NULL, 0, &dummy_length, &dummy_overflow, false);
    }
    if (c == 't') {
        return te_parse_literal(p, "true");
    }
    if (c == 'f') {
        return te_parse_literal(p, "false");
    }
    if (c == 'n') {
        return te_parse_literal(p, "null");
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return te_scan_number(p, true);
    }
    if (c < 0) {
        return te_fail(p, TE_ERR_SYNTAX, "unexpected end of input");
    }
    return te_fail(p, TE_ERR_SYNTAX, "unexpected character '%c'", (char)c);
}

/* A generic object inside an ignored value has no member callback; skip every
 * value and still reject duplicate keys lexically. */
static bool te_skip_member(te_parser *p, const char *key, size_t key_length, void *context,
                           size_t depth) {
    (void)key;
    (void)key_length;
    (void)context;
    return te_parse_value(p, depth);
}

/* te_parse_object_value() with the generic skip callback. */
static bool te_parse_skipped_object(te_parser *p, size_t depth) {
    if (depth >= TE_THEME_MAX_DEPTH) {
        return te_fail(p, TE_ERR_DEPTH, "JSON nesting exceeds %u containers",
                       TE_THEME_MAX_DEPTH);
    }
    if (te_peek(p) != '{') {
        return te_fail(p, TE_ERR_SYNTAX, "expected an object");
    }
    return te_parse_object(p, depth + 1, te_skip_member, NULL);
}

static void te_warn(te_parser *p, const char *format, ...) {
    if (p->report == NULL || p->report->warning_count >= TE_MAX_WARNINGS) {
        return;
    }
    va_list args;
    va_start(args, format);
    (void)vsnprintf(p->report->warnings[p->report->warning_count], TE_THEME_WARNING_MAX,
                    format, args);
    va_end(args);
    ++p->report->warning_count;
}

static bool te_unknown_key(te_parser *p, const char *where, const char *key,
                           size_t key_length) {
    if (p->strict) {
        return te_fail_at(p, TE_ERR_UNKNOWN_KEY, p->key_line, p->key_column,
                          "unknown %s key \"%.*s\"", where, (int)key_length, key);
    }
    te_warn(p, "unknown %s key \"%.*s\" ignored", where, (int)key_length, key);
    return true;
}

/* ------------------------------------------------------------------------- */
/* pt-theme/1 schema                                                          */
/* ------------------------------------------------------------------------- */

typedef struct te_top_context {
    te_theme_doc *doc;
    bool schema_seen;
    bool name_seen;
    bool scheme_seen;
    bool colors_seen;
    bool metrics_seen;
} te_top_context;

typedef struct te_colors_context {
    te_theme_doc *doc;
    bool seen[pt_theme_token_count];
} te_colors_context;

typedef struct te_metrics_context {
    te_theme_doc *doc;
    bool seen[pt_theme_metric_count];
} te_metrics_context;

static bool te_colors_member(te_parser *p, const char *key, size_t key_length, void *context,
                             size_t depth) {
    te_colors_context *ctx = context;
    pt_theme_token token;
    if (!te_color_from_name(key, key_length, &token)) {
        if (!te_unknown_key(p, "colour", key, key_length)) {
            return false;
        }
        return te_parse_value(p, depth);
    }
    size_t line = p->line;
    size_t column = p->column;
    if (te_peek(p) != '"') {
        return te_fail(p, TE_ERR_SYNTAX, "colour value must be a string");
    }
    char text[TE_COLOR_TEXT_MAX];
    size_t length = 0;
    bool overflow = false;
    if (!te_parse_string(p, text, sizeof text, &length, &overflow, false)) {
        return false;
    }
    if (overflow) {
        return te_fail_at(p, TE_ERR_SYNTAX, line, column, "colour value is too long");
    }
    if (length == 0 || memchr(text, 0, length) != NULL) {
        return te_fail_at(p, TE_ERR_SYNTAX, line, column, "colour value is empty");
    }
    pt_color color;
    if (!te_parse_color_text(text, length, &color)) {
        return te_fail_at(p, TE_ERR_SYNTAX, line, column,
                          "invalid colour value \"%.*s\"", (int)(length > 40 ? 40 : length),
                          text);
    }
    if (ctx->seen[token]) {
        return te_fail_at(p, TE_ERR_DUPLICATE_KEY, p->key_line, p->key_column,
                          "duplicate colour \"%s\"", te_color_names[token]);
    }
    ctx->seen[token] = true;
    ctx->doc->colors[token] = color;
    return true;
}

static bool te_metrics_member(te_parser *p, const char *key, size_t key_length, void *context,
                              size_t depth) {
    te_metrics_context *ctx = context;
    pt_theme_metric metric;
    if (!te_metric_from_name(key, key_length, &metric)) {
        if (!te_unknown_key(p, "metric", key, key_length)) {
            return false;
        }
        return te_parse_value(p, depth);
    }
    float value;
    if (!te_parse_metric_value(p, &value)) {
        return false;
    }
    if (ctx->seen[metric]) {
        return te_fail_at(p, TE_ERR_DUPLICATE_KEY, p->key_line, p->key_column,
                          "duplicate metric \"%s\"", te_metric_names[metric]);
    }
    ctx->seen[metric] = true;
    ctx->doc->metrics[metric] = value;
    return true;
}

static bool te_check_colors_complete(te_parser *p, const te_colors_context *ctx) {
    for (int i = 0; i < pt_theme_token_count; ++i) {
        if (!ctx->seen[i]) {
            return te_fail_at(p, TE_ERR_MISSING_KEY, 0, 0, "missing colour \"%s\"",
                              te_color_names[i]);
        }
    }
    return true;
}

static bool te_check_metrics_complete(te_parser *p, const te_metrics_context *ctx) {
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        if (!ctx->seen[i]) {
            return te_fail_at(p, TE_ERR_MISSING_KEY, 0, 0, "missing metric \"%s\"",
                              te_metric_names[i]);
        }
    }
    return true;
}

static bool te_top_member(te_parser *p, const char *key, size_t key_length, void *context,
                          size_t depth) {
    te_top_context *ctx = context;
    if (te_name_matches(key, key_length, "schema")) {
        size_t line = p->line;
        size_t column = p->column;
        if (te_peek(p) != '"') {
            return te_fail(p, TE_ERR_SCHEMA, "schema must be a string");
        }
        char text[TE_SCHEMA_TEXT_MAX];
        size_t length = 0;
        bool overflow = false;
        if (!te_parse_string(p, text, sizeof text, &length, &overflow, false)) {
            return false;
        }
        if (overflow || length != strlen(TE_THEME_SCHEMA_V1) ||
            memcmp(text, TE_THEME_SCHEMA_V1, length) != 0) {
            return te_fail_at(p, TE_ERR_SCHEMA, line, column,
                              "unsupported schema (expected \"%s\")", TE_THEME_SCHEMA_V1);
        }
        ctx->schema_seen = true;
        return true;
    }
    if (te_name_matches(key, key_length, "name")) {
        size_t line = p->line;
        size_t column = p->column;
        if (te_peek(p) != '"') {
            return te_fail(p, TE_ERR_NAME, "theme name must be a string");
        }
        char text[TE_THEME_NAME_MAX];
        size_t length = 0;
        bool overflow = false;
        if (!te_parse_string(p, text, sizeof text, &length, &overflow, true)) {
            return false;
        }
        if (overflow) {
            return te_fail_at(p, TE_ERR_NAME, line, column,
                              "theme name exceeds %u UTF-8 bytes",
                              TE_THEME_NAME_MAX_BYTES);
        }
        if (memchr(text, 0, length) != NULL) {
            return te_fail_at(p, TE_ERR_NAME, line, column,
                              "theme name contains a NUL");
        }
        memcpy(ctx->doc->name, text, length + 1);
        ctx->name_seen = true;
        return true;
    }
    if (te_name_matches(key, key_length, "scheme")) {
        size_t line = p->line;
        size_t column = p->column;
        if (te_peek(p) != '"') {
            return te_fail(p, TE_ERR_SCHEME, "scheme must be a string");
        }
        char text[TE_SCHEME_TEXT_MAX];
        size_t length = 0;
        bool overflow = false;
        if (!te_parse_string(p, text, sizeof text, &length, &overflow, false)) {
            return false;
        }
        pt_color_scheme scheme;
        if (overflow || !te_scheme_from_name(text, length, &scheme)) {
            return te_fail_at(p, TE_ERR_SCHEME, line, column,
                              "scheme must be \"dark\" or \"light\"");
        }
        ctx->doc->scheme = scheme;
        ctx->scheme_seen = true;
        return true;
    }
    if (te_name_matches(key, key_length, "colors")) {
        te_colors_context colors;
        memset(&colors, 0, sizeof colors);
        colors.doc = ctx->doc;
        if (te_peek(p) != '{') {
            return te_fail(p, TE_ERR_SYNTAX, "\"colors\" must be an object");
        }
        if (!te_parse_object_value(p, depth, te_colors_member, &colors)) {
            return false;
        }
        if (!te_check_colors_complete(p, &colors)) {
            return false;
        }
        ctx->colors_seen = true;
        return true;
    }
    if (te_name_matches(key, key_length, "metrics")) {
        te_metrics_context metrics;
        memset(&metrics, 0, sizeof metrics);
        metrics.doc = ctx->doc;
        if (te_peek(p) != '{') {
            return te_fail(p, TE_ERR_SYNTAX, "\"metrics\" must be an object");
        }
        if (!te_parse_object_value(p, depth, te_metrics_member, &metrics)) {
            return false;
        }
        if (!te_check_metrics_complete(p, &metrics)) {
            return false;
        }
        ctx->metrics_seen = true;
        return true;
    }
    if (!te_unknown_key(p, "top-level", key, key_length)) {
        return false;
    }
    return te_parse_value(p, depth);
}

static te_format_error te_report_error(te_parse_report *report, te_format_error error,
                                       size_t line, size_t column, const char *message) {
    if (report != NULL) {
        memset(report, 0, sizeof *report);
        report->error = error;
        report->line = line;
        report->column = column;
        (void)snprintf(report->message, sizeof report->message, "%s",
                       message != NULL ? message : "");
    }
    return error;
}

te_format_error te_parse_json(te_theme_doc *doc, const char *text, size_t length,
                              const te_parse_options *options, te_parse_report *report) {
    if (report != NULL) {
        memset(report, 0, sizeof *report);
        report->error = TE_ERR_OK;
    }
    if (doc == NULL) {
        return te_report_error(report, TE_ERR_ARGUMENT, 0, 0,
                               "null document or input");
    }
    if (text == NULL) {
        if (length != 0) {
            return te_report_error(report, TE_ERR_ARGUMENT, 0, 0,
                                   "null input with a non-zero length");
        }
        return te_report_error(report, TE_ERR_SYNTAX, 1, 1, "empty input");
    }
    if (length > TE_THEME_MAX_FILE_SIZE) {
        return te_report_error(report, TE_ERR_TOO_LARGE, 0, 0,
                               "input exceeds the 1 MiB limit");
    }

    te_parser p;
    memset(&p, 0, sizeof p);
    p.data = (const unsigned char *)text;
    p.length = length;
    p.line = 1;
    p.column = 1;
    p.strict = options != NULL && options->strict;
    p.error = TE_ERR_OK;
    p.report = report;

    if (length >= 3 && p.data[0] == 0xEF && p.data[1] == 0xBB && p.data[2] == 0xBF) {
        (void)te_fail(&p, TE_ERR_SYNTAX, "UTF-8 byte order mark is not allowed");
        goto done;
    }
    te_skip_ws(&p);
    if (p.offset >= p.length) {
        (void)te_fail(&p, TE_ERR_SYNTAX, "empty input or only whitespace");
        goto done;
    }
    if (te_peek(&p) != '{') {
        (void)te_fail(&p, TE_ERR_SYNTAX, "top-level value must be an object");
        goto done;
    }

    te_theme_doc parsed;
    memset(&parsed, 0, sizeof parsed);
    te_top_context ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.doc = &parsed;
    if (!te_parse_object_value(&p, 0, te_top_member, &ctx)) {
        goto done;
    }
    te_skip_ws(&p);
    if (p.offset != p.length) {
        (void)te_fail(&p, TE_ERR_SYNTAX, "trailing data after the top-level object");
        goto done;
    }
    if (!ctx.schema_seen) {
        (void)te_fail_at(&p, TE_ERR_SCHEMA, 0, 0, "missing \"schema\"");
        goto done;
    }
    if (!ctx.name_seen) {
        (void)te_fail_at(&p, TE_ERR_NAME, 0, 0, "missing theme name");
        goto done;
    }
    if (!ctx.scheme_seen) {
        (void)te_fail_at(&p, TE_ERR_SCHEME, 0, 0, "missing \"scheme\"");
        goto done;
    }
    if (!ctx.colors_seen) {
        (void)te_fail_at(&p, TE_ERR_MISSING_KEY, 0, 0, "missing \"colors\" object");
        goto done;
    }
    if (!ctx.metrics_seen) {
        (void)te_fail_at(&p, TE_ERR_MISSING_KEY, 0, 0, "missing \"metrics\" object");
        goto done;
    }
    *doc = parsed;

done:
    if (p.error == TE_ERR_OK && report != NULL) {
        report->line = 0;
        report->column = 0;
        report->message[0] = 0;
    }
    return p.error;
}

te_format_error te_load_json_file(te_theme_doc *doc, const char *path,
                                  const te_parse_options *options,
                                  te_parse_report *report) {
    if (doc == NULL || path == NULL) {
        return te_report_error(report, TE_ERR_ARGUMENT, 0, 0,
                               "null document or path");
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return te_report_error(report, TE_ERR_IO, 0, 0, "could not open the file");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return te_report_error(report, TE_ERR_IO, 0, 0, "could not seek the file");
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return te_report_error(report, TE_ERR_IO, 0, 0, "could not size the file");
    }
    if ((unsigned long)size > TE_THEME_MAX_FILE_SIZE) {
        fclose(file);
        return te_report_error(report, TE_ERR_TOO_LARGE, 0, 0,
                               "file exceeds the 1 MiB limit");
    }
    rewind(file);
    char *buffer = malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return te_report_error(report, TE_ERR_IO, 0, 0, "out of memory");
    }
    size_t got = fread(buffer, 1, (size_t)size, file);
    bool failed = ferror(file) != 0 || got != (size_t)size;
    fclose(file);
    if (failed) {
        free(buffer);
        return te_report_error(report, TE_ERR_IO, 0, 0, "could not read the file");
    }
    te_format_error error = te_parse_json(doc, buffer, (size_t)size, options, report);
    free(buffer);
    return error;
}/* ------------------------------------------------------------------------- */
/* Canonical writer                                                           */
/* ------------------------------------------------------------------------- */

typedef struct te_output {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} te_output;

static void te_out_bytes(te_output *out, const char *text, size_t length) {
    if (out->overflow) {
        return;
    }
    if (out->buffer == NULL) {
        if (out->length > SIZE_MAX - length) {
            out->overflow = true;
            return;
        }
        out->length += length;
        return;
    }
    if (out->capacity == 0 || length > out->capacity - 1 - out->length) {
        out->overflow = true;
        return;
    }
    memcpy(out->buffer + out->length, text, length);
    out->length += length;
}

static void te_out_char(te_output *out, char c) {
    te_out_bytes(out, &c, 1);
}

static void te_out_cstr(te_output *out, const char *text) {
    te_out_bytes(out, text, strlen(text));
}

static void te_outf(te_output *out, const char *format, ...) {
    char temp[128];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(temp, sizeof temp, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof temp) {
        out->overflow = true;
        return;
    }
    te_out_bytes(out, temp, (size_t)count);
}

/* %.9g with a locale-neutral decimal point and a positive-zero canonical form.
 * The only locale-dependent byte printf may introduce is the decimal
 * separator, so any byte outside the JSON/C number alphabet is folded to '.'.
 * A value whose %.9g form uses an exponent is rejected by the metric reader,
 * which keeps every parsed document canonicalisable and re-readable. */
static void te_format_float(float value, char *buffer, size_t capacity) {
    if (buffer == NULL || capacity == 0) {
        return;
    }
    if (!isfinite(value) || value == 0.0f) {
        (void)snprintf(buffer, capacity, "0");
        return;
    }
    char temp[64];
    int count = snprintf(temp, sizeof temp, "%.9g", (double)value);
    if (count <= 0 || (size_t)count >= sizeof temp) {
        (void)snprintf(buffer, capacity, "0");
        return;
    }
    size_t used = 0;
    bool separator = false;
    for (int i = 0; i < count; ++i) {
        char c = temp[i];
        bool allowed = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e' ||
                       c == 'E' || c == '.';
        if (allowed) {
            if (used + 1 < capacity) {
                buffer[used++] = c;
            }
        } else if (!separator) {
            if (used + 1 < capacity) {
                buffer[used++] = '.';
            }
            separator = true;
        }
    }
    buffer[used] = 0;
    if (used == 0) {
        (void)snprintf(buffer, capacity, "0");
    }
}

static const char *te_scheme_or_dark(pt_color_scheme scheme) {
    const char *name = te_scheme_name(scheme);
    return name != NULL ? name : "dark";
}

/* Escapes a theme name for a JSON string. Names are at most 95 bytes by
 * contract, but the writer stops at the array bound even for a malformed doc. */
static void te_json_string(te_output *out, const char *name) {
    te_out_char(out, '"');
    for (size_t i = 0; i < TE_THEME_NAME_MAX_BYTES && name[i] != 0; ++i) {
        unsigned char c = (unsigned char)name[i];
        switch (c) {
        case '"':
            te_out_cstr(out, "\\\"");
            break;
        case '\\':
            te_out_cstr(out, "\\\\");
            break;
        case '\b':
            te_out_cstr(out, "\\b");
            break;
        case '\f':
            te_out_cstr(out, "\\f");
            break;
        case '\n':
            te_out_cstr(out, "\\n");
            break;
        case '\r':
            te_out_cstr(out, "\\r");
            break;
        case '\t':
            te_out_cstr(out, "\\t");
            break;
        default:
            if (c < 0x20) {
                te_outf(out, "\\u%04x", (unsigned)c);
            } else {
                te_out_char(out, (char)c);
            }
            break;
        }
    }
    te_out_char(out, '"');
}

static void te_write_json(const te_theme_doc *doc, te_output *out) {
    te_out_cstr(out, "{\n");
    te_out_cstr(out, "  \"schema\": \"");
    te_out_cstr(out, TE_THEME_SCHEMA_V1);
    te_out_cstr(out, "\",\n");
    te_out_cstr(out, "  \"name\": ");
    te_json_string(out, doc->name);
    te_out_cstr(out, ",\n");
    te_out_cstr(out, "  \"scheme\": \"");
    te_out_cstr(out, te_scheme_or_dark(doc->scheme));
    te_out_cstr(out, "\",\n");
    te_out_cstr(out, "  \"colors\": {\n");
    for (int i = 0; i < pt_theme_token_count; ++i) {
        te_outf(out, "    \"%s\": \"%08X\"", te_color_names[i],
                (unsigned)(doc->colors[i] & 0xFFFFFFFFu));
        if (i + 1 < pt_theme_token_count) {
            te_out_char(out, ',');
        }
        te_out_char(out, '\n');
    }
    te_out_cstr(out, "  },\n");
    te_out_cstr(out, "  \"metrics\": {\n");
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        te_outf(out, "    \"%s\": ", te_metric_names[i]);
        char metric[64];
        te_format_float(doc->metrics[i], metric, sizeof metric);
        te_out_cstr(out, metric);
        if (i + 1 < pt_theme_metric_count) {
            te_out_char(out, ',');
        }
        te_out_char(out, '\n');
    }
    te_out_cstr(out, "  }\n");
    te_out_cstr(out, "}\n");
}

size_t te_serialize_json_size(const te_theme_doc *doc) {
    if (doc == NULL) {
        return 0;
    }
    te_output out;
    memset(&out, 0, sizeof out);
    te_write_json(doc, &out);
    if (out.overflow || out.length == SIZE_MAX) {
        return 0;
    }
    return out.length + 1;
}

te_format_error te_serialize_json(const te_theme_doc *doc, char *buffer,
                                  size_t capacity, size_t *written) {
    if (written != NULL) {
        *written = 0;
    }
    if (doc == NULL || buffer == NULL || capacity == 0) {
        return TE_ERR_ARGUMENT;
    }
    size_t required = te_serialize_json_size(doc);
    if (required == 0 || capacity < required) {
        return TE_ERR_ARGUMENT;
    }
    te_output out;
    memset(&out, 0, sizeof out);
    out.buffer = buffer;
    out.capacity = capacity;
    te_write_json(doc, &out);
    if (out.overflow || out.length + 1 > capacity) {
        buffer[0] = 0;
        return TE_ERR_ARGUMENT;
    }
    buffer[out.length] = 0;
    if (written != NULL) {
        *written = out.length;
    }
    return TE_ERR_OK;
}

static te_format_error te_write_text_file(const char *path, const char *text, size_t length) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return TE_ERR_IO;
    }
    size_t wrote = fwrite(text, 1, length, file);
    bool failed = wrote != length || ferror(file) != 0;
    if (fclose(file) != 0) {
        failed = true;
    }
    return failed ? TE_ERR_IO : TE_ERR_OK;
}

te_format_error te_save_json_file(const te_theme_doc *doc, const char *path) {
    if (doc == NULL || path == NULL) {
        return TE_ERR_ARGUMENT;
    }
    size_t required = te_serialize_json_size(doc);
    if (required == 0) {
        return TE_ERR_ARGUMENT;
    }
    char *buffer = malloc(required);
    if (buffer == NULL) {
        return TE_ERR_IO;
    }
    size_t written = 0;
    te_format_error error = te_serialize_json(doc, buffer, required, &written);
    if (error == TE_ERR_OK) {
        error = te_write_text_file(path, buffer, written);
    }
    free(buffer);
    return error;
}/* ------------------------------------------------------------------------- */

size_t te_export_c_size(const te_theme_doc *doc) {
    if (doc == NULL) {
        return 0;
    }
    return 0;
}

te_format_error te_export_c(const te_theme_doc *doc, char *buffer, size_t capacity,
                            size_t *written) {
    if (written != NULL) {
        *written = 0;
    }
    if (doc == NULL || buffer == NULL || capacity == 0) {
        return TE_ERR_ARGUMENT;
    }
    return TE_ERR_NOT_IMPLEMENTED;
}

te_format_error te_export_c_file(const te_theme_doc *doc, const char *path) {
    if (doc == NULL || path == NULL) {
        return TE_ERR_ARGUMENT;
    }
    return TE_ERR_NOT_IMPLEMENTED;
}