/*
 * Pt theme editor - CLI and app shell.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Headless modes (--check, --export-json, --export-c) never create a pt_app,
 * so they run on a display-less host. They use the same parser, serializer and
 * WCAG module as the GUI; there is no second code path to drift.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int te_run_gui(const char *path) {
    (void)path;
    fprintf(stderr, "%s: the GUI is not implemented in this scaffold\n", TE_TOOL_NAME);
    return 2;
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