/*
 * Pt theme editor - CLI and app shell.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Headless modes (--check, --export-json, --export-c) never create a pt_app,
 * so they run on a display-less host. They use the same parser, serializer and
 * WCAG module as the GUI; there is no second code path to drift.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pt/pt.h>

#include "preview.h"
#include "theme_format.h"
#include "theme_wcag.h"

#define TE_TOOL_NAME "theme-editor"
#define TE_TOOL_VERSION "2.5.0"
#define TE_PATH_MAX 512u

static void te_print_usage(FILE *out) {
    fprintf(out,
            "Usage: %s [options] [file.json]\n"
            "\n"
            "Standalone visual editor for the Pt theme token table (pt-theme/1).\n"
            "\n"
            "Modes:\n"
            "  %s                              open the editor with the built-in dark palette\n"
            "  %s <file.json>                  open the editor with an imported document\n"
            "  %s --check [--strict] [file...] validate and print the WCAG pair table\n"
            "  %s --export-json <in> [out]     write canonical pt-theme/1 JSON\n"
            "  %s --export-c <in> [out.c]      write a compilable theme_apply_<slug> function\n"
            "\n"
            "Options:\n"
            "  --strict    promote unknown-key warnings to errors (--check)\n"
            "  -o <path>   output path for --export-json / --export-c (same as a positional out)\n"
            "  --help, -h  show this help and exit\n"
            "  --version   show the tool version and exit\n"
            "\n"
            "--check without a file validates the bundled dark and light themes.\n"
            "--export-json and --export-c write to standard output when no output path\n"
            "is given. Headless modes never open a window.\n"
            "\n"
            "Exit codes: 0 success, 1 a floored WCAG pair failed, 2 malformed input,\n"
            "unsupported usage or an I/O error.\n",
            TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME,
            TE_TOOL_NAME);
}

static bool te_is_help(const char *arg) {
    return arg != NULL && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0);
}

static bool te_is_option(const char *arg) {
    return arg != NULL && arg[0] == '-' && arg[1] != '\0';
}

static int te_usage_error(const char *message) {
    if (message != NULL) {
        fprintf(stderr, "%s: %s\n", TE_TOOL_NAME, message);
    }
    fprintf(stderr, "%s: try --help for usage\n", TE_TOOL_NAME);
    return 2;
}

/* Positional diagnostics are "<path>:<line>:<column>: <message>". A non
 * positional error prints the stable code instead of a fake position. */
static void te_print_parse_error(const char *path, const te_parse_report *report) {
    const char *message = report->message[0] != '\0' ? report->message
                                                     : te_format_error_string(report->error);
    if (report->line > 0) {
        fprintf(stderr, "%s: %s:%zu:%zu: %s\n", TE_TOOL_NAME, path, report->line,
                report->column > 0 ? report->column : 1u, message);
    } else {
        fprintf(stderr, "%s: %s: %s\n", TE_TOOL_NAME, path, message);
    }
}

static void te_print_warnings(const char *path, const te_parse_report *report) {
    for (unsigned i = 0; i < report->warning_count; ++i) {
        fprintf(stderr, "%s: %s: warning: %s\n", TE_TOOL_NAME, path, report->warnings[i]);
    }
}

static void te_print_file_error(const char *path, te_format_error error) {
    fprintf(stderr, "%s: %s: %s\n", TE_TOOL_NAME, path, te_format_error_string(error));
}

/* Loads a document and reports diagnostics. Returns true only on success. */
static bool te_load_document(const char *path, const te_parse_options *options,
                             te_theme_doc *doc) {
    te_parse_report report = {0};
    te_format_error error = te_load_json_file(doc, path, options, &report);
    if (error != TE_ERR_OK) {
        te_print_parse_error(path, &report);
        return false;
    }
    te_print_warnings(path, &report);
    return true;
}

/* Resolves the bundled fixture when --check is called without a file. The
 * repository root and the tool directory are both accepted, so the default
 * works from either the top-level make invocation or a direct build run. */
static bool te_resolve_default_theme(const char *name, char *out, size_t capacity) {
    static const char *const prefixes[] = {"themes/", "tools/theme-editor/themes/"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        int written = snprintf(out, capacity, "%s%s.json", prefixes[i], name);
        if (written > 0 && (size_t)written < capacity) {
            FILE *file = fopen(out, "rb");
            if (file != NULL) {
                fclose(file);
                return true;
            }
        }
    }
    (void)snprintf(out, capacity, "%s%s.json", prefixes[0], name);
    return false;
}

static int te_check_one(const char *path, bool strict) {
    te_parse_options options = {.strict = strict};
    te_theme_doc doc = {0};
    te_parse_report report = {0};
    te_format_error error = te_load_json_file(&doc, path, &options, &report);
    if (error != TE_ERR_OK) {
        te_print_parse_error(path, &report);
        return 2;
    }
    te_print_warnings(path, &report);

    te_wcag_report wcag = {0};
    bool passed = te_wcag_check_palette(doc.colors, &wcag);
    const char *scheme = te_scheme_name(doc.scheme);

    printf("File:  %s\n", path);
    printf("Theme: %s (%s)\n", doc.name[0] != '\0' ? doc.name : "(unnamed)",
           scheme != NULL ? scheme : "unknown");
    printf("%-46s %8s  %-5s  %5s  %s\n", "Pair", "Ratio", "Grade", "Floor", "Status");
    printf("%-46s %8s  %-5s  %5s  %s\n", "----------------------------------------------",
           "--------", "-----", "-----", "------");
    unsigned informational = 0;
    for (unsigned i = 0; i < wcag.count; ++i) {
        const te_wcag_result *row = &wcag.rows[i];
        if (row->informational) {
            ++informational;
        }
        const char *status = row->informational ? "info" : (row->pass ? "pass" : "FAIL");
        printf("%-46s %8.3f  %-5s  %5.1f  %s\n",
               row->pair->label != NULL ? row->pair->label : "?", row->ratio,
               te_wcag_grade_name(row->grade), row->pair->floor, status);
    }
    printf("%u pairs checked, %u informational, %u failed\n", wcag.count, informational,
           wcag.failed);
    printf("Result: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

static int te_run_check(int argc, char **argv) {
    bool strict = false;
    const char *files[64];
    size_t file_count = 0;
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--strict") == 0) {
            strict = true;
        } else if (te_is_help(arg)) {
            te_print_usage(stdout);
            return 0;
        } else if (te_is_option(arg)) {
            fprintf(stderr, "%s: unknown option '%s' for --check\n", TE_TOOL_NAME, arg);
            return te_usage_error(NULL);
        } else if (file_count < sizeof(files) / sizeof(files[0])) {
            files[file_count++] = arg;
        } else {
            return te_usage_error("too many files for --check");
        }
    }

    char dark_path[TE_PATH_MAX];
    char light_path[TE_PATH_MAX];
    if (file_count == 0) {
        (void)te_resolve_default_theme("dark", dark_path, sizeof(dark_path));
        (void)te_resolve_default_theme("light", light_path, sizeof(light_path));
        const char *defaults[] = {dark_path, light_path};
        int status = 0;
        for (size_t i = 0; i < 2; ++i) {
            if (i > 0) {
                printf("\n");
            }
            int one = te_check_one(defaults[i], strict);
            if (one == 2) {
                status = 2;
            } else if (one == 1 && status != 2) {
                status = 1;
            }
        }
        return status;
    }

    int status = 0;
    for (size_t i = 0; i < file_count; ++i) {
        if (i > 0) {
            printf("\n");
        }
        int one = te_check_one(files[i], strict);
        if (one == 2) {
            status = 2;
        } else if (one == 1 && status != 2) {
            status = 1;
        }
    }
    return status;
}

static bool te_write_stdout(const char *data, size_t length) {
    if (length == 0) {
        return true;
    }
    size_t written = fwrite(data, 1, length, stdout);
    if (written != length || fflush(stdout) != 0) {
        fprintf(stderr, "%s: cannot write standard output\n", TE_TOOL_NAME);
        return false;
    }
    return true;
}

/* Parses argv[2..] for the export modes: one input, an optional output given
 * positionally or after -o. Returns 0 when parsing succeeded and set *input and
 * *output (NULL output means standard output). */
static int te_parse_export_args(int argc, char **argv, const char **input,
                                const char **output) {
    *input = NULL;
    *output = NULL;
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (te_is_help(arg)) {
            te_print_usage(stdout);
            return 0;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc || *output != NULL) {
                return te_usage_error("expected one output path after -o");
            }
            *output = argv[++i];
        } else if (te_is_option(arg)) {
            fprintf(stderr, "%s: unknown option '%s' for an export mode\n", TE_TOOL_NAME, arg);
            return te_usage_error(NULL);
        } else if (*input == NULL) {
            *input = arg;
        } else if (*output == NULL) {
            *output = arg;
        } else {
            return te_usage_error("too many arguments for an export mode");
        }
    }
    if (*input == NULL) {
        return te_usage_error("an export mode needs an input file");
    }
    return 1;
}

static int te_run_export_json(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    int parsed = te_parse_export_args(argc, argv, &input, &output);
    if (parsed <= 0) {
        return parsed == 0 ? 0 : 2;
    }
    te_theme_doc doc = {0};
    if (!te_load_document(input, NULL, &doc)) {
        return 2;
    }
    if (output != NULL) {
        te_format_error error = te_save_json_file(&doc, output);
        if (error != TE_ERR_OK) {
            te_print_file_error(output, error);
            return 2;
        }
        return 0;
    }
    size_t capacity = te_serialize_json_size(&doc);
    if (capacity == 0) {
        fprintf(stderr, "%s: cannot serialize %s\n", TE_TOOL_NAME, input);
        return 2;
    }
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        fprintf(stderr, "%s: out of memory\n", TE_TOOL_NAME);
        return 2;
    }
    size_t written = 0;
    te_format_error error = te_serialize_json(&doc, buffer, capacity, &written);
    int status = 0;
    if (error != TE_ERR_OK) {
        te_print_file_error(input, error);
        status = 2;
    } else if (!te_write_stdout(buffer, written)) {
        status = 2;
    }
    free(buffer);
    return status;
}

static int te_run_export_c(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    int parsed = te_parse_export_args(argc, argv, &input, &output);
    if (parsed <= 0) {
        return parsed == 0 ? 0 : 2;
    }
    te_theme_doc doc = {0};
    if (!te_load_document(input, NULL, &doc)) {
        return 2;
    }
    if (output != NULL) {
        te_format_error error = te_export_c_file(&doc, output);
        if (error != TE_ERR_OK) {
            te_print_file_error(output, error);
            return 2;
        }
        return 0;
    }
    size_t capacity = te_export_c_size(&doc);
    if (capacity == 0) {
        fprintf(stderr, "%s: cannot export %s\n", TE_TOOL_NAME, input);
        return 2;
    }
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        fprintf(stderr, "%s: out of memory\n", TE_TOOL_NAME);
        return 2;
    }
    size_t written = 0;
    te_format_error error = te_export_c(&doc, buffer, capacity, &written);
    int status = 0;
    if (error != TE_ERR_OK) {
        te_print_file_error(input, error);
        status = 2;
    } else if (!te_write_stdout(buffer, written)) {
        status = 2;
    }
    free(buffer);
    return status;
}

/* ------------------------------------------------------------------------- */
/* GUI                                                                       */
/* ------------------------------------------------------------------------- */

typedef struct te_editor_state te_editor_state;

typedef struct te_color_row {
    te_editor_state *state;
    pt_theme_token token;
} te_color_row;

typedef struct te_metric_row {
    te_editor_state *state;
    pt_theme_metric metric;
} te_metric_row;

struct te_editor_state {
    pt_app *app;
    pt_window *window;
    te_preview *preview;
    te_theme_doc doc;
    bool updating;

    te_color_row color_rows[pt_theme_token_count];
    te_metric_row metric_rows[pt_theme_metric_count];
    pt_widget *color_pickers[pt_theme_token_count];
    pt_widget *metric_pickers[pt_theme_metric_count];

    pt_widget *name_entry;
    pt_widget *scheme_label;
    pt_widget *preview_button;
    pt_widget *json_path;
    pt_widget *c_path;
    pt_widget *status_label;
    pt_widget *contrast_summary;
    pt_widget *contrast_rows[TE_WCAG_MAX_PAIRS];
};

static void te_editor_status(te_editor_state *state, const char *format, ...) {
    if (state->status_label == NULL) {
        return;
    }
    char message[320];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    pt_widget_set_text(state->status_label, message);
}

/* Copies a user-entered name into the fixed document buffer. The entry can
 * exceed the 95-byte format limit; keep the longest valid UTF-8 prefix so the
 * document never carries a truncated sequence. */
static void te_editor_copy_name(char destination[TE_THEME_NAME_MAX], const char *source) {
    size_t length = source != NULL ? strlen(source) : 0;
    if (length >= TE_THEME_NAME_MAX) {
        length = TE_THEME_NAME_MAX - 1;
        while (length > 0 && ((unsigned char)source[length] & 0xC0u) == 0x80u) {
            --length;
        }
    }
    memcpy(destination, source != NULL ? source : "", length);
    destination[length] = '\0';
}

static void te_editor_update_contrast(te_editor_state *state) {
    te_wcag_report report = {0};
    bool passed = te_wcag_check_palette(state->doc.colors, &report);
    unsigned informational = 0;
    for (unsigned i = 0; i < report.count && i < TE_WCAG_MAX_PAIRS; ++i) {
        const te_wcag_result *row = &report.rows[i];
        if (row->informational) {
            ++informational;
        }
        if (state->contrast_rows[i] == NULL) {
            continue;
        }
        char line[256];
        const char *status = row->informational ? "info" : (row->pass ? "pass" : "FAIL");
        (void)snprintf(line, sizeof(line), "%-46s %6.3f  %-5s  %5.1f  %s",
                       row->pair->label != NULL ? row->pair->label : "?", row->ratio,
                       te_wcag_grade_name(row->grade), row->pair->floor, status);
        pt_widget_set_text(state->contrast_rows[i], line);
    }
    if (state->contrast_summary != NULL) {
        char summary[160];
        (void)snprintf(summary, sizeof(summary), "WCAG: %u pairs, %u informational, %u failed - %s",
                       report.count, informational, report.failed, passed ? "PASS" : "FAIL");
        pt_widget_set_text(state->contrast_summary, summary);
    }
}

/* Applies the live document to the preview window. If the user closed the
 * preview window, the stale handle is retired first. */
static void te_editor_apply_preview(te_editor_state *state, bool reset_first) {
    if (state->preview == NULL) {
        return;
    }
    pt_window *window = te_preview_window(state->preview);
    if (window == NULL || !pt_window_is_open(window)) {
        te_preview_destroy(state->preview);
        state->preview = NULL;
        if (state->preview_button != NULL) {
            pt_widget_set_text(state->preview_button, "Show preview");
        }
        return;
    }
    if (reset_first) {
        te_preview_reset(state->preview);
    }
    te_preview_apply(state->preview, &state->doc);
}

static void te_editor_document_changed(te_editor_state *state) {
    te_editor_apply_preview(state, false);
    te_editor_update_contrast(state);
}

/* Rewrites every editor control from the document. Programmatic picker setters
 * are silent, but pt_widget_set_text() on an entry reports a change, so the
 * updating guard keeps the model stable. */
static void te_editor_refresh(te_editor_state *state) {
    state->updating = true;
    if (state->name_entry != NULL) {
        pt_widget_set_text(state->name_entry, state->doc.name);
    }
    if (state->scheme_label != NULL) {
        const char *scheme = te_scheme_name(state->doc.scheme);
        pt_widget_set_text(state->scheme_label, scheme != NULL ? scheme : "unknown");
    }
    for (int i = 0; i < pt_theme_token_count; ++i) {
        pt_color_picker_set_color(state->color_pickers[i], state->doc.colors[i]);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        pt_number_picker_set_value(state->metric_pickers[i], (double)state->doc.metrics[i]);
    }
    state->updating = false;
    te_editor_apply_preview(state, true);
    te_editor_update_contrast(state);
}

/* Seeds the document from the built-in palette through the public getters.
 * The editor's own window keeps this neutral scheme; the document is copied
 * out and never writes back to the application palette. */
static void te_editor_seed(te_editor_state *state, pt_color_scheme scheme) {
    pt_app_set_color_scheme(state->app, scheme);
    state->doc.scheme = scheme;
    (void)snprintf(state->doc.name, sizeof(state->doc.name), "Default %s",
                   scheme == pt_color_scheme_light ? "Light" : "Dark");
    for (int i = 0; i < pt_theme_token_count; ++i) {
        state->doc.colors[i] = pt_app_get_theme_color(state->app, (pt_theme_token)i);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        state->doc.metrics[i] = pt_app_get_theme_metric(state->app, (pt_theme_metric)i);
    }
}

static void te_on_color_changed(pt_widget *picker, pt_color color, void *user_data) {
    te_color_row *row = user_data;
    te_editor_state *state = row->state;
    if (state->updating) {
        return;
    }
    state->doc.colors[row->token] = color;
    te_editor_document_changed(state);
    te_editor_status(state, "Colour %s = %s", te_color_name(row->token),
                     pt_color_picker_get_hex(picker));
}

static void te_on_metric_changed(pt_widget *picker, double value, void *user_data) {
    (void)picker;
    te_metric_row *row = user_data;
    te_editor_state *state = row->state;
    if (state->updating) {
        return;
    }
    float metric = (float)value;
    if (metric < 0.0f) {
        metric = 0.0f;
    }
    state->doc.metrics[row->metric] = metric;
    te_editor_document_changed(state);
    te_editor_status(state, "Metric %s = %.9g", te_metric_name(row->metric), (double)metric);
}

static void te_on_name_changed(pt_widget *entry, const char *text, void *user_data) {
    (void)entry;
    te_editor_state *state = user_data;
    if (state->updating) {
        return;
    }
    te_editor_copy_name(state->doc.name, text);
    pt_window_set_title(state->window, state->doc.name[0] != '\0' ? state->doc.name
                                                                  : "Pt theme editor");
    te_editor_status(state, "Theme name: %s",
                     state->doc.name[0] != '\0' ? state->doc.name : "(unnamed)");
}

static void te_editor_new_palette(te_editor_state *state, pt_color_scheme scheme) {
    te_editor_seed(state, scheme);
    te_editor_refresh(state);
    te_editor_status(state, "Started a new %s palette from the built-in defaults",
                     te_scheme_name(scheme));
}

static void te_on_new_dark(pt_widget *button, void *user_data) {
    (void)button;
    te_editor_new_palette(user_data, pt_color_scheme_dark);
}

static void te_on_new_light(pt_widget *button, void *user_data) {
    (void)button;
    te_editor_new_palette(user_data, pt_color_scheme_light);
}

static void te_on_import_json(pt_widget *button, void *user_data) {
    (void)button;
    te_editor_state *state = user_data;
    const char *path = pt_widget_get_text(state->json_path);
    if (path == NULL || path[0] == '\0') {
        te_editor_status(state, "Import failed: enter a JSON file path first");
        return;
    }
    te_theme_doc imported = {0};
    te_parse_report report = {0};
    te_format_error error = te_load_json_file(&imported, path, NULL, &report);
    if (error != TE_ERR_OK) {
        if (report.line > 0) {
            te_editor_status(state, "Import failed: %s:%zu:%zu: %s", path, report.line,
                             report.column > 0 ? report.column : 1u,
                             report.message[0] != '\0' ? report.message
                                                       : te_format_error_string(error));
        } else {
            te_editor_status(state, "Import failed: %s: %s", path,
                             te_format_error_string(error));
        }
        return;
    }
    state->doc = imported;
    pt_app_set_color_scheme(state->app, state->doc.scheme);
    te_editor_refresh(state);
    te_editor_status(state, "Imported %s (%s)", path, te_scheme_name(state->doc.scheme));
}

static void te_on_export_json(pt_widget *button, void *user_data) {
    (void)button;
    te_editor_state *state = user_data;
    const char *path = pt_widget_get_text(state->json_path);
    if (path == NULL || path[0] == '\0') {
        te_editor_status(state, "Export failed: enter an output JSON path first");
        return;
    }
    te_format_error error = te_save_json_file(&state->doc, path);
    if (error != TE_ERR_OK) {
        te_editor_status(state, "Export failed: %s: %s", path, te_format_error_string(error));
        return;
    }
    te_editor_status(state, "Exported canonical JSON to %s", path);
}

static void te_on_export_c(pt_widget *button, void *user_data) {
    (void)button;
    te_editor_state *state = user_data;
    const char *path = pt_widget_get_text(state->c_path);
    if (path == NULL || path[0] == '\0') {
        te_editor_status(state, "Export failed: enter an output C path first");
        return;
    }
    te_format_error error = te_export_c_file(&state->doc, path);
    if (error != TE_ERR_OK) {
        te_editor_status(state, "Export failed: %s: %s", path, te_format_error_string(error));
        return;
    }
    te_editor_status(state, "Exported C source to %s", path);
}

static void te_on_toggle_preview(pt_widget *button, void *user_data) {
    (void)button;
    te_editor_state *state = user_data;
    if (state->preview != NULL) {
        te_preview_destroy(state->preview);
        state->preview = NULL;
        pt_widget_set_text(state->preview_button, "Show preview");
        te_editor_status(state, "Preview window closed");
        return;
    }
    state->preview = te_preview_create(state->app, &state->doc);
    if (state->preview == NULL) {
        te_editor_status(state, "Preview failed: %s", pt_get_last_error());
        return;
    }
    pt_window *window = te_preview_window(state->preview);
    if (window != NULL) {
        pt_window_show(window);
    }
    pt_widget_set_text(state->preview_button, "Hide preview");
    te_editor_status(state, "Preview window opened; edits apply through per-window overrides");
}

static bool te_on_main_close(pt_window *window, void *user_data) {
    (void)window;
    te_editor_state *state = user_data;
    if (state->preview != NULL) {
        te_preview_destroy(state->preview);
        state->preview = NULL;
    }
    return true;
}

static pt_widget *te_editor_muted_label(pt_widget *parent, const char *text) {
    pt_widget *label = pt_label_create(parent, text);
    pt_widget_set_font_size(label, 11.0f);
    pt_widget_set_label_wrap(label, true);
    return label;
}

static void te_editor_build_colors(te_editor_state *state, pt_widget *page) {
    pt_widget_set_padding(page, 12.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(page, 8.0f);
    te_editor_muted_label(
        page, "One picker per semantic role. The hex field accepts #RRGGBB and "
              "#RRGGBBAA; the palette popup offers presets.");
    pt_widget *scroll = pt_scroll_create(page);
    pt_widget_set_grow(scroll, 1.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_padding(list, 0.0f, 4.0f, 8.0f, 4.0f);
    pt_widget_set_gap(list, 4.0f);

    for (int i = 0; i < pt_theme_token_count; ++i) {
        te_color_row *row = &state->color_rows[i];
        row->state = state;
        row->token = (pt_theme_token)i;

        pt_widget *line = pt_row_create(list);
        pt_widget_set_gap(line, 10.0f);
        pt_widget_set_align_self(line, pt_align_stretch);

        pt_widget *label = pt_label_create(line, te_color_name(row->token));
        pt_widget_set_width(label, 200.0f);
        pt_widget_set_font_size(label, 12.0f);

        pt_widget *picker = pt_color_picker_create(line);
        pt_widget_set_width(picker, 250.0f);
        char accessible[96];
        (void)snprintf(accessible, sizeof(accessible), "Colour %s", te_color_name(row->token));
        pt_widget_set_a11y_name(picker, accessible);
        pt_color_picker_set_color(picker, state->doc.colors[row->token]);
        pt_color_picker_set_on_change(picker, te_on_color_changed, row);
        state->color_pickers[i] = picker;
    }
}

static void te_editor_build_metrics(te_editor_state *state, pt_widget *page) {
    pt_widget_set_padding(page, 12.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(page, 8.0f);
    te_editor_muted_label(
        page, "Ten geometric metrics in logical pixels. Interactive values clamp at zero; "
              "the format parser rejects negative values rather than clamping them.");
    pt_widget *scroll = pt_scroll_create(page);
    pt_widget_set_grow(scroll, 1.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_padding(list, 0.0f, 4.0f, 8.0f, 4.0f);
    pt_widget_set_gap(list, 6.0f);

    for (int i = 0; i < pt_theme_metric_count; ++i) {
        te_metric_row *row = &state->metric_rows[i];
        row->state = state;
        row->metric = (pt_theme_metric)i;

        pt_widget *line = pt_row_create(list);
        pt_widget_set_gap(line, 10.0f);
        pt_widget_set_align_self(line, pt_align_stretch);

        pt_widget *label = pt_label_create(line, te_metric_name(row->metric));
        pt_widget_set_width(label, 200.0f);
        pt_widget_set_font_size(label, 12.0f);

        pt_widget *picker = pt_number_picker_create(line);
        pt_widget_set_width(picker, 160.0f);
        char accessible[96];
        (void)snprintf(accessible, sizeof(accessible), "Metric %s", te_metric_name(row->metric));
        pt_widget_set_a11y_name(picker, accessible);
        pt_number_picker_set_range(picker, 0.0, 512.0);
        pt_number_picker_set_step(picker, 1.0);
        pt_number_picker_set_value(picker, (double)state->doc.metrics[row->metric]);
        pt_number_picker_set_on_change(picker, te_on_metric_changed, row);
        state->metric_pickers[i] = picker;
    }
}

static void te_editor_build_contrast(te_editor_state *state, pt_widget *page) {
    pt_widget_set_padding(page, 12.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(page, 8.0f);
    te_editor_muted_label(
        page, "WCAG 2.x ratios measured on the edited colours: AAA at 7.0, AA at 4.5. "
              "text_disabled is informational and is expected below AA.");
    state->contrast_summary = pt_label_create(page, "");
    pt_widget_set_font_size(state->contrast_summary, 13.0f);
    pt_widget_set_font_weight(state->contrast_summary, pt_font_weight_semibold);

    pt_widget *scroll = pt_scroll_create(page);
    pt_widget_set_grow(scroll, 1.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_padding(list, 0.0f, 4.0f, 8.0f, 4.0f);
    pt_widget_set_gap(list, 3.0f);
    for (unsigned i = 0; i < TE_WCAG_MAX_PAIRS; ++i) {
        pt_widget *label = pt_label_create(list, "");
        pt_widget_set_font_size(label, 11.0f);
        state->contrast_rows[i] = label;
    }
}

static void te_editor_build(te_editor_state *state) {
    pt_widget *root = pt_window_root(state->window);
    pt_widget_set_padding(root, 18.0f, 18.0f, 16.0f, 18.0f);
    pt_widget_set_gap(root, 10.0f);

    pt_widget *title = pt_label_create(root, "Pt theme editor");
    pt_widget_set_font_size(title, 18.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);
    te_editor_muted_label(
        root, "Edit the 38 colour roles and 10 metrics, inspect the WCAG pair table and "
              "apply the result to a separate preview window. This window keeps the "
              "neutral palette.");

    pt_widget *identity = pt_row_create(root);
    pt_widget_set_gap(identity, 8.0f);
    pt_widget *name_caption = pt_label_create(identity, "Name");
    pt_widget_set_width(name_caption, 46.0f);
    state->name_entry = pt_entry_create(identity, "Theme name");
    pt_widget_set_width(state->name_entry, 220.0f);
    pt_widget_set_a11y_name(state->name_entry, "Theme name");
    pt_widget_set_text(state->name_entry, state->doc.name);
    pt_entry_set_on_change(state->name_entry, te_on_name_changed, state);

    pt_widget *scheme_caption = pt_label_create(identity, "Scheme");
    pt_widget_set_width(scheme_caption, 54.0f);
    state->scheme_label = pt_label_create(identity, "");
    pt_widget_set_width(state->scheme_label, 60.0f);
    pt_widget_set_font_weight(state->scheme_label, pt_font_weight_medium);

    pt_widget *new_dark = pt_button_create(identity, "New dark");
    pt_button_set_variant(new_dark, pt_button_secondary);
    pt_button_set_on_click(new_dark, te_on_new_dark, state);
    pt_widget *new_light = pt_button_create(identity, "New light");
    pt_button_set_variant(new_light, pt_button_secondary);
    pt_button_set_on_click(new_light, te_on_new_light, state);

    state->preview_button = pt_button_create(identity, "Show preview");
    pt_button_set_on_click(state->preview_button, te_on_toggle_preview, state);
    pt_widget_set_tooltip(state->preview_button, "Open the live per-window preview");

    pt_widget *files = pt_row_create(root);
    pt_widget_set_gap(files, 8.0f);
    pt_widget *json_caption = pt_label_create(files, "JSON file");
    pt_widget_set_width(json_caption, 66.0f);
    state->json_path = pt_entry_create(files, "theme.json");
    pt_widget_set_width(state->json_path, 280.0f);
    pt_widget_set_a11y_name(state->json_path, "JSON file path");
    pt_widget_set_text(state->json_path, "theme.json");
    pt_widget *import = pt_button_create(files, "Import JSON");
    pt_button_set_variant(import, pt_button_secondary);
    pt_button_set_on_click(import, te_on_import_json, state);
    pt_widget *export_json = pt_button_create(files, "Export JSON");
    pt_button_set_on_click(export_json, te_on_export_json, state);

    pt_widget *c_caption = pt_label_create(files, "C file");
    pt_widget_set_width(c_caption, 46.0f);
    state->c_path = pt_entry_create(files, "theme.c");
    pt_widget_set_width(state->c_path, 200.0f);
    pt_widget_set_a11y_name(state->c_path, "C output file path");
    pt_widget_set_text(state->c_path, "theme.c");
    pt_widget *export_c = pt_button_create(files, "Export C");
    pt_button_set_variant(export_c, pt_button_secondary);
    pt_button_set_on_click(export_c, te_on_export_c, state);

    pt_widget *tabs = pt_tabs_create(root);
    pt_widget_set_grow(tabs, 1.0f);
    pt_widget_set_min_height(tabs, 380.0f);
    pt_tabs_set_strip_height(tabs, 40.0f);
    te_editor_build_colors(state, pt_tabs_add_page(tabs, "Colours"));
    te_editor_build_metrics(state, pt_tabs_add_page(tabs, "Metrics"));
    te_editor_build_contrast(state, pt_tabs_add_page(tabs, "Contrast"));

    state->status_label = pt_label_create(root, "");
    pt_widget_set_label_wrap(state->status_label, true);
    pt_widget_set_font_size(state->status_label, 11.0f);
}

static int te_run_gui(const char *path) {
    te_theme_doc initial = {0};
    bool have_initial = false;
    if (path != NULL) {
        te_parse_report report = {0};
        te_format_error error = te_load_json_file(&initial, path, NULL, &report);
        if (error != TE_ERR_OK) {
            te_print_parse_error(path, &report);
            return 2;
        }
        te_print_warnings(path, &report);
        have_initial = true;
    }

    pt_app *app = pt_app_create("dev.pt.theme-editor");
    if (app == NULL) {
        fprintf(stderr, "%s: cannot open a display: %s\n", TE_TOOL_NAME, pt_get_last_error());
        return 2;
    }

    pt_window *window = pt_window_create(app, "Pt theme editor", 1180, 820);
    if (window == NULL) {
        fprintf(stderr, "%s: cannot create the editor window: %s\n", TE_TOOL_NAME,
                pt_get_last_error());
        pt_app_destroy(app);
        return 2;
    }

    te_editor_state state = {0};
    state.app = app;
    state.window = window;
    if (have_initial) {
        state.doc = initial;
        pt_app_set_color_scheme(app, state.doc.scheme);
    } else {
        te_editor_seed(&state, pt_color_scheme_dark);
    }

    te_editor_build(&state);
    pt_window_set_close_callback(window, te_on_main_close, &state);
    pt_window_show(window);
    te_editor_refresh(&state);
    te_editor_status(&state,
                     "Ready. Edits apply to the preview window only; this window keeps the "
                     "neutral palette.");

    int result = pt_app_run(app);
    if (state.preview != NULL) {
        te_preview_destroy(state.preview);
        state.preview = NULL;
    }
    pt_app_destroy(app);
    if (result < 0) {
        fprintf(stderr, "%s: the compositor connection was lost\n", TE_TOOL_NAME);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        return te_run_gui(NULL);
    }
    if (te_is_help(argv[1]) && argc == 2) {
        te_print_usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("%s %s (pt-theme/1)\n", TE_TOOL_NAME, TE_TOOL_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "--check") == 0) {
        return te_run_check(argc, argv);
    }
    if (strcmp(argv[1], "--export-json") == 0) {
        return te_run_export_json(argc, argv);
    }
    if (strcmp(argv[1], "--export-c") == 0) {
        return te_run_export_c(argc, argv);
    }
    if (argc == 2 && argv[1][0] != '-') {
        return te_run_gui(argv[1]);
    }
    if (te_is_help(argv[1])) {
        fprintf(stderr, "%s: --help takes no arguments\n", TE_TOOL_NAME);
        return 2;
    }
    if (te_is_option(argv[1])) {
        fprintf(stderr, "%s: unknown option '%s'\n", TE_TOOL_NAME, argv[1]);
    } else {
        fprintf(stderr, "%s: unexpected argument '%s'\n", TE_TOOL_NAME, argv[1]);
    }
    return te_usage_error(NULL);
}