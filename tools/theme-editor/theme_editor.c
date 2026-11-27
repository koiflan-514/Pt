/*
 * Pt theme editor - CLI and app shell (W0 scaffold).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * This is the W0 skeleton: argument parsing and --help only. W3a owns the app
 * shell, the editors, import/export wiring and the full headless dispatch.
 * Headless modes do not create a pt_app, so this file stays display-free.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "theme_format.h"

#define TE_TOOL_NAME "theme-editor"

static void te_print_usage(FILE *out) {
    fprintf(out,
            "Usage: %s [options] [file.json]\n"
            "\n"
            "Standalone visual editor for the Pt theme token table.\n"
            "\n"
            "Modes:\n"
            "  %s                              open the editor with the built-in dark palette\n"
            "  %s <file.json>                  open the editor with an imported document\n"
            "  %s --check [--strict] <file>    validate and print the WCAG pair table\n"
            "  %s --export-json <in> <out>     write canonical pt-theme/1 JSON\n"
            "  %s --export-c <in> <out.c>      write a compilable theme_apply_<slug> function\n"
            "\n"
            "Options:\n"
            "  --strict    promote unknown-key warnings to errors (--check)\n"
            "  --help      show this help and exit\n"
            "  --version   show the tool version and exit\n"
            "\n"
            "Exit codes: 0 success, 1 a floored WCAG pair failed, 2 malformed input\n"
            "or usage error. Headless modes never open a window.\n",
            TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME, TE_TOOL_NAME);
}

static bool te_is_help(const char *arg) {
    return arg != NULL && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0);
}

int main(int argc, char **argv) {
    if (argc == 2 && te_is_help(argv[1])) {
        te_print_usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("%s (pt-theme/1, W0 scaffold)\n", TE_TOOL_NAME);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--check") == 0) {
        fprintf(stderr, "%s: --check is not implemented in this W0 scaffold\n", TE_TOOL_NAME);
        return 2;
    }
    if (argc >= 2 && strcmp(argv[1], "--export-json") == 0) {
        fprintf(stderr, "%s: --export-json is not implemented in this W0 scaffold\n",
                TE_TOOL_NAME);
        return 2;
    }
    if (argc >= 2 && strcmp(argv[1], "--export-c") == 0) {
        fprintf(stderr, "%s: --export-c is not implemented in this W0 scaffold\n", TE_TOOL_NAME);
        return 2;
    }
    if (argc == 1 || (argc == 2 && argv[1][0] != '-')) {
        fprintf(stderr, "%s: the GUI is not implemented in this W0 scaffold\n", TE_TOOL_NAME);
        return 2;
    }
    fprintf(stderr, "%s: unknown option; try --help\n", TE_TOOL_NAME);
    return 2;
}