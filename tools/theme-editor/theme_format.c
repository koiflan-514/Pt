/*
 * Pt theme editor - pt-theme/1 document model (W0 scaffold).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This file is the W0 stub. It owns the frozen name tables and the safe
 * argument/error paths; W1 replaces the reader, writer and C exporter bodies.
 */

#include "theme_format.h"

#include <ctype.h>
#include <stdio.h>
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

static te_format_error te_report(te_parse_report *report, te_format_error error,
                                 const char *message) {
    if (report != NULL) {
        report->error = error;
        report->line = 0;
        report->column = 0;
        if (message != NULL) {
            (void)snprintf(report->message, sizeof report->message, "%s", message);
        } else {
            report->message[0] = 0;
        }
    }
    return error;
}

te_format_error te_parse_json(te_theme_doc *doc, const char *text, size_t length,
                              const te_parse_options *options, te_parse_report *report) {
    (void)options;
    if (doc == NULL || (text == NULL && length != 0)) {
        return te_report(report, TE_ERR_ARGUMENT, "null document or input");
    }
    if (text == NULL) {
        return te_report(report, TE_ERR_SYNTAX, "empty input");
    }
    return te_report(report, TE_ERR_NOT_IMPLEMENTED,
                     "pt-theme/1 reader is not built in this W0 scaffold");
}

te_format_error te_load_json_file(te_theme_doc *doc, const char *path,
                                  const te_parse_options *options,
                                  te_parse_report *report) {
    (void)options;
    if (doc == NULL || path == NULL) {
        return te_report(report, TE_ERR_ARGUMENT, "null document or path");
    }
    return te_report(report, TE_ERR_NOT_IMPLEMENTED,
                     "pt-theme/1 file reader is not built in this W0 scaffold");
}

size_t te_serialize_json_size(const te_theme_doc *doc) {
    if (doc == NULL) {
        return 0;
    }
    return 0;
}

te_format_error te_serialize_json(const te_theme_doc *doc, char *buffer,
                                  size_t capacity, size_t *written) {
    if (written != NULL) {
        *written = 0;
    }
    if (doc == NULL || buffer == NULL || capacity == 0) {
        return TE_ERR_ARGUMENT;
    }
    return TE_ERR_NOT_IMPLEMENTED;
}

te_format_error te_save_json_file(const te_theme_doc *doc, const char *path) {
    if (doc == NULL || path == NULL) {
        return TE_ERR_ARGUMENT;
    }
    return TE_ERR_NOT_IMPLEMENTED;
}

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