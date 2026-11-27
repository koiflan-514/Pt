/*
 * pt-theme/1 document model, name tables and W0 stub error paths.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "harness.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../tools/theme-editor/theme_format.h"

/* The document is protected data: a core role addition must break this compile
 * until the tool tables are updated. */
static_assert(pt_theme_token_count == 38, "update the theme-editor colour table");
static_assert(pt_theme_metric_count == 10, "update the theme-editor metric table");
static_assert(sizeof(((te_theme_doc *)0)->colors) / sizeof(pt_color) == 38,
              "document colour array must follow the enum");
static_assert(sizeof(((te_theme_doc *)0)->metrics) / sizeof(float) == 10,
              "document metric array must follow the enum");
static_assert(sizeof(((te_theme_doc *)0)->name) == TE_THEME_NAME_MAX,
              "document name array must match the frozen limit");

/* Compile-time signature check: each initializer is a type mismatch if the
 * frozen interface moves. */
static void test_signatures(void) {
    te_format_error (*parse)(te_theme_doc *, const char *, size_t, const te_parse_options *,
                             te_parse_report *) = te_parse_json;
    te_format_error (*load)(te_theme_doc *, const char *, const te_parse_options *,
                            te_parse_report *) = te_load_json_file;
    size_t (*size)(const te_theme_doc *) = te_serialize_json_size;
    te_format_error (*write)(const te_theme_doc *, char *, size_t, size_t *) = te_serialize_json;
    te_format_error (*save)(const te_theme_doc *, const char *) = te_save_json_file;
    size_t (*csize)(const te_theme_doc *) = te_export_c_size;
    te_format_error (*cexport)(const te_theme_doc *, char *, size_t, size_t *) = te_export_c;
    te_format_error (*csave)(const te_theme_doc *, const char *) = te_export_c_file;
    void (*slug)(const char *, char *, size_t) = te_slugify_name;
    const char *(*cname)(pt_theme_token) = te_color_name;
    const char *(*mname)(pt_theme_metric) = te_metric_name;
    const char *(*sname)(pt_color_scheme) = te_scheme_name;
    bool (*cfrom)(const char *, size_t, pt_theme_token *) = te_color_from_name;
    bool (*mfrom)(const char *, size_t, pt_theme_metric *) = te_metric_from_name;
    bool (*sfrom)(const char *, size_t, pt_color_scheme *) = te_scheme_from_name;
    const char *(*estr)(te_format_error) = te_format_error_string;

    pt_check(parse != NULL);
    pt_check(load != NULL);
    pt_check(size != NULL);
    pt_check(write != NULL);
    pt_check(save != NULL);
    pt_check(csize != NULL);
    pt_check(cexport != NULL);
    pt_check(csave != NULL);
    pt_check(slug != NULL);
    pt_check(cname != NULL);
    pt_check(mname != NULL);
    pt_check(sname != NULL);
    pt_check(cfrom != NULL);
    pt_check(mfrom != NULL);
    pt_check(sfrom != NULL);
    pt_check(estr != NULL);
}

static void test_color_names(void) {
    for (int i = 0; i < pt_theme_token_count; ++i) {
        const char *name = te_color_name((pt_theme_token)i);
        pt_check(name != NULL);
        if (name == NULL) {
            continue;
        }
        pt_check(name[0] != 0);
        for (int j = 0; j < i; ++j) {
            const char *other = te_color_name((pt_theme_token)j);
            pt_check(other == NULL || strcmp(name, other) != 0);
        }
        pt_theme_token token = pt_theme_token_count;
        pt_check(te_color_from_name(name, strlen(name), &token));
        pt_check(token == (pt_theme_token)i);
    }
    pt_check(te_color_name(pt_theme_token_count) == NULL);
    pt_check(te_color_name((pt_theme_token)-1) == NULL);
    pt_check(!te_color_from_name(NULL, 0, NULL));
    pt_check(!te_color_from_name("no_such_role", strlen("no_such_role"), NULL));
}

static void test_metric_names(void) {
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        const char *name = te_metric_name((pt_theme_metric)i);
        pt_check(name != NULL);
        if (name == NULL) {
            continue;
        }
        pt_check(name[0] != 0);
        for (int j = 0; j < i; ++j) {
            const char *other = te_metric_name((pt_theme_metric)j);
            pt_check(other == NULL || strcmp(name, other) != 0);
        }
        pt_theme_metric metric = pt_theme_metric_count;
        pt_check(te_metric_from_name(name, strlen(name), &metric));
        pt_check(metric == (pt_theme_metric)i);
    }
    pt_check(te_metric_name(pt_theme_metric_count) == NULL);
    pt_check(te_metric_name((pt_theme_metric)-1) == NULL);
    pt_check(!te_metric_from_name(NULL, 0, NULL));
    pt_check(!te_metric_from_name("no_such_metric", strlen("no_such_metric"), NULL));
}

static void test_scheme_names(void) {
    pt_check(strcmp(te_scheme_name(pt_color_scheme_dark), "dark") == 0);
    pt_check(strcmp(te_scheme_name(pt_color_scheme_light), "light") == 0);

    pt_color_scheme scheme = pt_color_scheme_dark;
    pt_check(te_scheme_from_name("light", 5, &scheme));
    pt_check(scheme == pt_color_scheme_light);
    pt_check(te_scheme_from_name("dark", 4, &scheme));
    pt_check(scheme == pt_color_scheme_dark);
    pt_check(!te_scheme_from_name("dim", 3, &scheme));
    pt_check(!te_scheme_from_name(NULL, 0, &scheme));
}

static void test_error_strings(void) {
    for (int i = 0; i <= (int)TE_ERR_NOT_IMPLEMENTED; ++i) {
        const char *text = te_format_error_string((te_format_error)i);
        pt_check(text != NULL);
        pt_check(text != NULL && text[0] != 0);
    }
    pt_check(strcmp(te_format_error_string(TE_ERR_OK), "ok") == 0);
    pt_check(strcmp(te_format_error_string((te_format_error)999), "unknown error") == 0);
}

static void test_slugify(void) {
    char out[64];
    te_slugify_name("Ocean Blue", out, sizeof out);
    pt_check(strcmp(out, "ocean_blue") == 0);
    te_slugify_name("Ocean-Blue 2", out, sizeof out);
    pt_check(strcmp(out, "ocean_blue_2") == 0);
    te_slugify_name("", out, sizeof out);
    pt_check(strcmp(out, "custom_theme") == 0);
    te_slugify_name(NULL, out, sizeof out);
    pt_check(strcmp(out, "custom_theme") == 0);
    te_slugify_name("abc", out, 3);
    pt_check(strcmp(out, "ab") == 0);
    te_slugify_name("abc", NULL, 8);
}

static void test_stub_error_paths(void) {
    te_theme_doc doc;
    memset(&doc, 0, sizeof doc);
    doc.name[0] = 'X';
    doc.scheme = pt_color_scheme_light;
    doc.colors[pt_theme_background] = 0x112233FFu;

    te_parse_report report;
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(NULL, "{}", 2, NULL, &report) == TE_ERR_ARGUMENT);
    pt_check(te_parse_json(&doc, NULL, 0, NULL, &report) == TE_ERR_SYNTAX);
    pt_check(te_parse_json(NULL, NULL, 4, NULL, &report) == TE_ERR_ARGUMENT);

    /* The W0 reader is not implemented; the destination must stay untouched and
     * the report must describe the failure without crashing. */
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&doc, "{\"schema\":\"pt-theme/1\"}", 22, NULL, &report) ==
             TE_ERR_SYNTAX);
    pt_check(report.error == TE_ERR_SYNTAX);
    pt_check(report.message[0] != 0);
    pt_check(report.warning_count == 0);
    pt_check(doc.name[0] == 'X');
    pt_check(doc.scheme == pt_color_scheme_light);
    pt_check(doc.colors[pt_theme_background] == 0x112233FFu);

    te_parse_options strict = {.strict = true};
    pt_check(te_parse_json(&doc, "{}", 2, &strict, NULL) == TE_ERR_SCHEMA);

    pt_check(te_load_json_file(NULL, "x.json", NULL, &report) == TE_ERR_ARGUMENT);
    pt_check(te_load_json_file(&doc, NULL, NULL, &report) == TE_ERR_ARGUMENT);

    pt_check(te_serialize_json_size(NULL) == 0);
    pt_check(te_serialize_json(NULL, NULL, 0, NULL) == TE_ERR_ARGUMENT);
    char buffer[64];
    size_t written = 123;
    pt_check(te_serialize_json(&doc, NULL, sizeof buffer, &written) == TE_ERR_ARGUMENT);
    pt_check(written == 0);
    pt_check(te_serialize_json(&doc, buffer, 0, &written) == TE_ERR_ARGUMENT);
    pt_check(te_serialize_json(&doc, buffer, sizeof buffer, &written) ==
             TE_ERR_NOT_IMPLEMENTED);
    pt_check(written == 0);
    pt_check(te_save_json_file(NULL, "x.json") == TE_ERR_ARGUMENT);
    pt_check(te_save_json_file(&doc, NULL) == TE_ERR_ARGUMENT);

    pt_check(te_export_c_size(NULL) == 0);
    pt_check(te_export_c(NULL, buffer, sizeof buffer, NULL) == TE_ERR_ARGUMENT);
    pt_check(te_export_c(&doc, NULL, sizeof buffer, &written) == TE_ERR_ARGUMENT);
    pt_check(written == 0);
    pt_check(te_export_c(&doc, buffer, sizeof buffer, &written) == TE_ERR_NOT_IMPLEMENTED);
    pt_check(te_export_c_file(NULL, "x.c") == TE_ERR_ARGUMENT);
    pt_check(te_export_c_file(&doc, NULL) == TE_ERR_ARGUMENT);
}

/* The fixtures are canonical inputs for W1. W0 only asserts that both files are
 * readable and carry every frozen role and metric key exactly once per table
 * entry (a missing key is a fixture bug, not a parser bug). */
static char fixture_text[65536];

static bool load_fixture(const char *path) {
    fixture_text[0] = 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    size_t count = fread(fixture_text, 1, sizeof fixture_text - 1, file);
    fclose(file);
    fixture_text[count] = 0;
    return true;
}

static bool has_key(const char *name) {
    char key[128];
    int length = snprintf(key, sizeof key, "\"%s\":", name);
    return length > 0 && strstr(fixture_text, key) != NULL;
}

static void test_fixtures(void) {
    const char *paths[] = {"tools/theme-editor/themes/dark.json",
                           "tools/theme-editor/themes/light.json"};
    for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); ++p) {
        pt_check(load_fixture(paths[p]));
        if (fixture_text[0] == 0) {
            continue;
        }
        size_t length = strlen(fixture_text);
        pt_check(length > 0 && fixture_text[length - 1] == '\n');
        pt_check(strstr(fixture_text, "\"schema\": \"pt-theme/1\"") != NULL);
        for (int i = 0; i < pt_theme_token_count; ++i) {
            const char *name = te_color_name((pt_theme_token)i);
            pt_check(name != NULL && has_key(name));
        }
        for (int i = 0; i < pt_theme_metric_count; ++i) {
            const char *name = te_metric_name((pt_theme_metric)i);
            pt_check(name != NULL && has_key(name));
        }
    }
}

int main(void) {
    test_signatures();
    test_color_names();
    test_metric_names();
    test_scheme_names();
    test_error_strings();
    test_slugify();
    test_stub_error_paths();
    test_fixtures();
    return pt_test_finish("theme_format");
}