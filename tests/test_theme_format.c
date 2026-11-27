/*
 * pt-theme/1 document model, name tables and W0 stub error paths.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "harness.h"

#include <stdbool.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
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

    /* A real reader still leaves the destination untouched on failure and
     * fills the report. The W0 scaffold's NOT_IMPLEMENTED code is gone. */
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
    pt_check(te_serialize_json(&doc, buffer, sizeof buffer, &written) == TE_ERR_ARGUMENT);
    pt_check(written == 0);
    pt_check(te_save_json_file(NULL, "x.json") == TE_ERR_ARGUMENT);
    pt_check(te_save_json_file(&doc, NULL) == TE_ERR_ARGUMENT);

    pt_check(te_export_c_size(NULL) == 0);
    pt_check(te_export_c(NULL, buffer, sizeof buffer, NULL) == TE_ERR_ARGUMENT);
    pt_check(te_export_c(&doc, NULL, sizeof buffer, &written) == TE_ERR_ARGUMENT);
    pt_check(written == 0);
    pt_check(te_export_c(&doc, buffer, sizeof buffer, &written) == TE_ERR_ARGUMENT);
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

/* ------------------------------------------------------------------------- */
/* W1 parser, serializer and exporter tests                                   */
/* ------------------------------------------------------------------------- */

static char test_buffer[65536];
static char test_buffer2[65536];

static bool replace_once(char *buffer, size_t capacity, const char *needle,
                         const char *replacement) {
    char *at = strstr(buffer, needle);
    if (at == NULL) {
        return false;
    }
    size_t old_length = strlen(buffer);
    size_t needle_length = strlen(needle);
    size_t replacement_length = strlen(replacement);
    if (old_length - needle_length + replacement_length + 1 > capacity) {
        return false;
    }
    size_t tail = old_length - (size_t)(at - buffer) - needle_length;
    memmove(at + replacement_length, at + needle_length, tail + 1);
    memcpy(at, replacement, replacement_length);
    return true;
}

static bool remove_once(char *buffer, size_t capacity, const char *needle) {
    return replace_once(buffer, capacity, needle, "");
}

static void line_col_at(const char *text, size_t index, size_t *line, size_t *column) {
    *line = 1;
    *column = 1;
    for (size_t i = 0; i < index; ++i) {
        if (text[i] == '\n') {
            ++*line;
            *column = 1;
        } else {
            ++*column;
        }
    }
}

static size_t count_substr(const char *text, const char *needle) {
    size_t count = 0;
    size_t length = strlen(needle);
    for (const char *at = text; (at = strstr(at, needle)) != NULL; at += length) {
        ++count;
    }
    return count;
}

static te_theme_doc sentinel_doc(void) {
    te_theme_doc doc;
    memset(&doc, 0, sizeof doc);
    doc.name[0] = 'S';
    doc.name[1] = 'E';
    doc.name[2] = 'N';
    doc.name[3] = 'T';
    doc.name[4] = 0;
    doc.scheme = pt_color_scheme_light;
    doc.colors[0] = 0x11223344u;
    doc.colors[pt_theme_token_count - 1] = 0xAABBCCDDu;
    doc.metrics[0] = 1.5f;
    doc.metrics[pt_theme_metric_count - 1] = 42.0f;
    return doc;
}

static void expect_parse_error_at(const char *text, te_format_error error, size_t line,
                                  size_t column, const char *needle) {
    te_theme_doc doc = sentinel_doc();
    te_theme_doc before = doc;
    te_parse_report report;
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&doc, text, strlen(text), NULL, &report) == error);
    pt_check(report.error == error);
    pt_check(report.line == line);
    pt_check(report.column == column);
    pt_check(report.message[0] != 0);
    if (needle != NULL) {
        pt_check(strstr(report.message, needle) != NULL);
    }
    pt_check(memcmp(&doc, &before, sizeof doc) == 0);
}

static void expect_parse_error_index(const char *text, te_format_error error, size_t index,
                                     const char *needle) {
    size_t line;
    size_t column;
    line_col_at(text, index, &line, &column);
    expect_parse_error_at(text, error, line, column, needle);
}

static bool parse_text(const char *text, te_theme_doc *doc, te_parse_report *report) {
    memset(report, 0, sizeof *report);
    return te_parse_json(doc, text, strlen(text), NULL, report) == TE_ERR_OK;
}

static void test_golden_round_trip(void) {
    const char *paths[] = {"tools/theme-editor/themes/dark.json",
                           "tools/theme-editor/themes/light.json"};
    for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); ++p) {
        pt_check(load_fixture(paths[p]));
        size_t length = strlen(fixture_text);
        pt_check(length > 0);
        if (length == 0) {
            continue;
        }
        te_theme_doc doc;
        te_parse_report report;
        memset(&doc, 0, sizeof doc);
        memset(&report, 0, sizeof report);
        pt_check(te_parse_json(&doc, fixture_text, length, NULL, &report) == TE_ERR_OK);
        pt_check(report.error == TE_ERR_OK);
        pt_check(report.warning_count == 0);
        pt_check(report.message[0] == 0);
        pt_check(doc.name[0] != 0);
        pt_check(doc.scheme == (p == 0 ? pt_color_scheme_dark : pt_color_scheme_light));

        if (p == 0) {
            pt_check(doc.colors[pt_theme_background] == 0x0B0E14FFu);
            pt_check(doc.colors[pt_theme_selection] == 0x3568E059u);
            pt_check(doc.metrics[pt_theme_metric_radius_pill] == 999.0f);
            pt_check(doc.metrics[pt_theme_metric_control_height] == 34.0f);
        } else {
            pt_check(doc.colors[pt_theme_background] == 0xF4F6FAFFu);
            pt_check(doc.colors[pt_theme_accent] == 0x2559E6FFu);
            pt_check(doc.metrics[pt_theme_metric_radius_medium] == 10.0f);
        }

        size_t required = te_serialize_json_size(&doc);
        pt_check(required == length + 1);
        size_t written = 0;
        pt_check(te_serialize_json(&doc, test_buffer, sizeof test_buffer, &written) ==
                 TE_ERR_OK);
        pt_check(written == length);
        pt_check(test_buffer[written] == 0);
        pt_check(memcmp(test_buffer, fixture_text, length) == 0);
        pt_check(test_buffer[length - 1] == '\n');

        te_theme_doc doc2;
        te_parse_report report2;
        pt_check(parse_text(test_buffer, &doc2, &report2));
        pt_check(report2.warning_count == 0);
        size_t written2 = 0;
        pt_check(te_serialize_json(&doc2, test_buffer2, sizeof test_buffer2, &written2) ==
                 TE_ERR_OK);
        pt_check(written2 == written);
        pt_check(memcmp(test_buffer, test_buffer2, written) == 0);
        pt_check(memcmp(&doc, &doc2, sizeof doc) == 0);
    }
}

static void test_canonical_output(void) {
    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc doc;
    te_parse_report report;
    pt_check(parse_text(fixture_text, &doc, &report));

    size_t required = te_serialize_json_size(&doc);
    pt_check(required > 128);
    pt_check(te_serialize_json_size(NULL) == 0);

    size_t written = 0;
    pt_check(te_serialize_json(&doc, test_buffer, sizeof test_buffer, &written) == TE_ERR_OK);
    pt_check(written + 1 == required);
    pt_check(test_buffer[written - 1] == '\n');
    pt_check(test_buffer[written] == 0);
    pt_check(strstr(test_buffer, "\n  \"schema\": \"pt-theme/1\",\n") != NULL);
    pt_check(strstr(test_buffer, "\n    \"background\": \"0B0E14FF\",\n") != NULL);
    pt_check(strstr(test_buffer, "\n    \"radius_small\": 6,\n") != NULL);
    pt_check(strstr(test_buffer, "0b0e14ff") == NULL);
    pt_check(strstr(test_buffer, "\"},") == NULL);

    const char *cursor = test_buffer;
    for (int i = 0; i < pt_theme_token_count; ++i) {
        const char *name = te_color_name((pt_theme_token)i);
        pt_check(name != NULL);
        const char *found = name != NULL ? strstr(cursor, name) : NULL;
        pt_check(found != NULL && found >= cursor);
        if (found == NULL) {
            break;
        }
        cursor = found + strlen(name);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        const char *name = te_metric_name((pt_theme_metric)i);
        const char *found = name != NULL ? strstr(cursor, name) : NULL;
        pt_check(found != NULL && found >= cursor);
        if (found == NULL) {
            break;
        }
        cursor = found + strlen(name);
    }

    size_t written_again = 0;
    pt_check(te_serialize_json(&doc, test_buffer2, sizeof test_buffer2, &written_again) ==
             TE_ERR_OK);
    pt_check(written_again == written);
    pt_check(memcmp(test_buffer, test_buffer2, written) == 0);

    memset(test_buffer2, 0x5A, sizeof test_buffer2);
    char untouched[64];
    memset(untouched, 0x5A, sizeof untouched);
    written = 123;
    pt_check(te_serialize_json(&doc, untouched, required - 1, &written) ==
             TE_ERR_ARGUMENT);
    pt_check(written == 0);
    for (size_t i = 0; i < sizeof untouched; ++i) {
        pt_check((unsigned char)untouched[i] == 0x5A);
    }

    /* A zero document is still a valid pt-theme/1 document: empty name,
     * transparent black colours, zero metrics. */
    te_theme_doc zero;
    memset(&zero, 0, sizeof zero);
    size_t zero_written = 0;
    pt_check(te_serialize_json(&zero, test_buffer, sizeof test_buffer, &zero_written) ==
             TE_ERR_OK);
    pt_check(strstr(test_buffer, "\"name\": \"\",") != NULL);
    pt_check(strstr(test_buffer, "\"background\": \"00000000\"") != NULL);
    pt_check(strstr(test_buffer, "\"radius_small\": 0,") != NULL);
    te_theme_doc zero_again;
    te_parse_report zero_report;
    pt_check(parse_text(test_buffer, &zero_again, &zero_report));
    pt_check(zero_again.name[0] == 0);
    pt_check(zero_again.scheme == pt_color_scheme_dark);
    for (int i = 0; i < pt_theme_token_count; ++i) {
        pt_check(zero_again.colors[i] == 0);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        pt_check(zero_again.metrics[i] == 0.0f);
    }
}

static void test_import_tolerance(void) {
    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc base;
    te_parse_report base_report;
    pt_check(parse_text(fixture_text, &base, &base_report));
    pt_check(te_serialize_json(&base, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);

    struct color_case {
        const char *replacement;
        pt_color expected;
    } cases[] = {
        {"\"#0b0e14ff\"", 0x0B0E14FFu},
        {"\"#0B0E14\"", 0x0B0E14FFu},
        {"\"0x0B0E14AA\"", 0x0B0E14AAu},
        {"\"0b0e14ff\"", 0x0B0E14FFu},
        {"\"  #0B0E14FF  \"", 0x0B0E14FFu},
        {"\"#0B0E14ff\"", 0x0B0E14FFu},
        {"\"0X0B0E14\"", 0x0B0E14FFu},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
        pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                              "\"0B0E14FF\"", cases[i].replacement));
        te_theme_doc doc;
        te_parse_report report;
        pt_check(parse_text(test_buffer2, &doc, &report));
        pt_check(doc.colors[pt_theme_background] == cases[i].expected);
        pt_check(report.warning_count == 0);
    }

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 6.0"));
    te_theme_doc doc;
    te_parse_report report;
    pt_check(parse_text(test_buffer2, &doc, &report));
    pt_check(doc.metrics[pt_theme_metric_radius_small] == 6.0f);

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 0.001"));
    pt_check(parse_text(test_buffer2, &doc, &report));
    pt_check_near(doc.metrics[pt_theme_metric_radius_small], 0.001, 1e-7);

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": -0"));
    pt_check(parse_text(test_buffer2, &doc, &report));
    pt_check(doc.metrics[pt_theme_metric_radius_small] == 0.0f);

    /* JSON escapes and raw UTF-8 in the name. */
    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"name\": \"Default Dark\"",
                          "\"name\": \"Caf\\u00e9 \\\"X\\\" \\\\ \\/\""));
    pt_check(parse_text(test_buffer2, &doc, &report));
    pt_check(strcmp(doc.name, "Caf\xC3\xA9 \"X\" \\ /") == 0);
    size_t written = 0;
    pt_check(te_serialize_json(&doc, test_buffer2, sizeof test_buffer2, &written) ==
             TE_ERR_OK);
    te_theme_doc doc2;
    te_parse_report report2;
    pt_check(parse_text(test_buffer2, &doc2, &report2));
    pt_check(strcmp(doc.name, doc2.name) == 0);

    /* Unknown values may use full JSON grammar, including exponent notation,
     * arrays and nested objects; only the schema numbers are constrained. */
    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "{\n  \"schema\"",
                          "{\n  \"future\": {\"a\": [1e3, true, null, \"x\"]},\n  \"schema\""));
    pt_check(parse_text(test_buffer2, &doc, &report));
    pt_check(report.warning_count == 1);
    pt_check(strstr(report.warnings[0], "future") != NULL);
}static void test_rejections(void) {
    expect_parse_error_at("", TE_ERR_SYNTAX, 1, 1, "empty");
    expect_parse_error_at("  \n\t", TE_ERR_SYNTAX, 2, 2, "empty");
    expect_parse_error_at("\xEF\xBB\xBF{}", TE_ERR_SYNTAX, 1, 1, "byte order mark");
    expect_parse_error_at("[]", TE_ERR_SYNTAX, 1, 1, "top-level");
    expect_parse_error_at("{}", TE_ERR_SCHEMA, 0, 0, "schema");
    expect_parse_error_at("{\"schema\":\"pt-theme/2\"}", TE_ERR_SCHEMA, 1, 11,
                         "unsupported schema");
    expect_parse_error_at("{\"schema\":\"pt-theme/1\",}", TE_ERR_SYNTAX, 1, 24,
                         "object key");
    expect_parse_error_at("{\"schema\":\"pt-theme/1", TE_ERR_SYNTAX, 1, 22,
                         "unterminated");

    const char *duplicate = "{\"schema\":\"pt-theme/1\",\"schema\":\"pt-theme/1\"}";
    const char *second = strstr(duplicate, "\"schema\"");
    second = second != NULL ? strstr(second + 1, "\"schema\"") : NULL;
    pt_check(second != NULL);
    expect_parse_error_index(duplicate, TE_ERR_DUPLICATE_KEY,
                             second != NULL ? (size_t)(second - duplicate) : 0,
                             "duplicate key");

    const char *duplicate_unknown = "{\"zzz\":0,\"zzz\":1}";
    const char *second_zzz = strstr(duplicate_unknown, "\"zzz\"");
    second_zzz = second_zzz != NULL ? strstr(second_zzz + 1, "\"zzz\"") : NULL;
    pt_check(second_zzz != NULL);
    expect_parse_error_index(duplicate_unknown, TE_ERR_DUPLICATE_KEY,
                             second_zzz != NULL ? (size_t)(second_zzz - duplicate_unknown) : 0,
                             "duplicate key");

    const char *depth = "{\"x\":[[[[[[[[0]]]]]]]]}";
    expect_parse_error_at(depth, TE_ERR_DEPTH, 1, 13, "nesting");

    char long_name[160];
    memset(long_name, 'A', 96);
    long_name[96] = 0;
    char name_doc[256];
    (void)snprintf(name_doc, sizeof name_doc,
                   "{\"schema\":\"pt-theme/1\",\"name\":\"%s\"}", long_name);
    const char *name_value = strstr(name_doc, "\"name\":\"");
    pt_check(name_value != NULL);
    expect_parse_error_index(name_doc, TE_ERR_NAME,
                             name_value != NULL
                                 ? (size_t)(name_value - name_doc) + strlen("\"name\":")
                                 : 0,
                             "exceeds");

    const char *bad_utf8 = "{\"schema\":\"pt-theme/1\",\"name\":\"\xC3\"}";
    const char *bad_byte = strstr(bad_utf8, "\xC3");
    pt_check(bad_byte != NULL);
    expect_parse_error_index(bad_utf8, TE_ERR_NAME,
                             bad_byte != NULL ? (size_t)(bad_byte - bad_utf8) : 0, "UTF-8");

    const char *nul_name = "{\"schema\":\"pt-theme/1\",\"name\":\"a\\u0000b\"}";
    const char *nul_value = strstr(nul_name, "\"name\":\"");
    pt_check(nul_value != NULL);
    expect_parse_error_index(nul_name, TE_ERR_NAME,
                             nul_value != NULL
                                 ? (size_t)(nul_value - nul_name) + strlen("\"name\":")
                                 : 0,
                             "NUL");

    const char *high_surrogate = "{\"schema\":\"pt-theme/1\",\"name\":\"\\uD800\"}";
    const char *high_escape = strstr(high_surrogate, "\\uD800");
    pt_check(high_escape != NULL);
    expect_parse_error_index(high_surrogate, TE_ERR_SYNTAX,
                             high_escape != NULL ? (size_t)(high_escape - high_surrogate) + 6
                                                 : 0,
                             "surrogate");

    const char *low_surrogate = "{\"schema\":\"pt-theme/1\",\"name\":\"\\uDC00\"}";
    const char *low_escape = strstr(low_surrogate, "\\uDC00");
    pt_check(low_escape != NULL);
    expect_parse_error_index(low_surrogate, TE_ERR_SYNTAX,
                             low_escape != NULL ? (size_t)(low_escape - low_surrogate) + 6 : 0,
                             "surrogate");

    const char *unknown_escape = "{\"x\":\"\\q\"}";
    const char *escape_at = strstr(unknown_escape, "\\q");
    pt_check(escape_at != NULL);
    expect_parse_error_index(unknown_escape, TE_ERR_SYNTAX,
                             escape_at != NULL ? (size_t)(escape_at - unknown_escape) + 2 : 0,
                             "escape");

    const char *bad_utf8_value = "{\"x\":\"\xC3\"}";
    const char *bad_value_byte = strstr(bad_utf8_value, "\xC3");
    pt_check(bad_value_byte != NULL);
    expect_parse_error_index(bad_utf8_value, TE_ERR_SYNTAX,
                             bad_value_byte != NULL
                                 ? (size_t)(bad_value_byte - bad_utf8_value)
                                 : 0,
                             "UTF-8");

    te_theme_doc doc = sentinel_doc();
    te_theme_doc before = doc;
    te_parse_report report;
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&doc, "{}", TE_THEME_MAX_FILE_SIZE + 1, NULL, &report) ==
             TE_ERR_TOO_LARGE);
    pt_check(report.error == TE_ERR_TOO_LARGE);
    pt_check(report.line == 0 && report.column == 0);
    pt_check(memcmp(&doc, &before, sizeof doc) == 0);

    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc base;
    te_parse_report base_report;
    pt_check(parse_text(fixture_text, &base, &base_report));
    pt_check(te_serialize_json(&base, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);

    /* Trailing byte after the closing brace. */
    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    size_t base_length = strlen(test_buffer2);
    test_buffer2[base_length] = 'x';
    test_buffer2[base_length + 1] = 0;
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX, base_length, "trailing");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"scheme\": \"dark\"", "\"scheme\": \"dim\""));
    const char *dim = strstr(test_buffer2, "\"dim\"");
    pt_check(dim != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SCHEME,
                             dim != NULL ? (size_t)(dim - test_buffer2) : 0, "scheme must");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"scheme\": \"dark\"", "\"scheme\": 1"));
    const char *scheme_value = strstr(test_buffer2, "\"scheme\": ");
    pt_check(scheme_value != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SCHEME,
                             scheme_value != NULL
                                 ? (size_t)(scheme_value - test_buffer2) + strlen("\"scheme\": ")
                                 : 0,
                             "must be a string");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(remove_once(test_buffer2, sizeof test_buffer2, "  \"scheme\": \"dark\",\n"));
    expect_parse_error_at(test_buffer2, TE_ERR_SCHEME, 0, 0, "missing");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2, "\"colors\": {",
                          "\"not_colors\": {"));
    expect_parse_error_at(test_buffer2, TE_ERR_MISSING_KEY, 0, 0, "\"colors\"");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2, "\"metrics\": {",
                          "\"not_metrics\": {"));
    expect_parse_error_at(test_buffer2, TE_ERR_MISSING_KEY, 0, 0, "\"metrics\"");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(remove_once(test_buffer2, sizeof test_buffer2,
                         "    \"background\": \"0B0E14FF\",\n"));
    expect_parse_error_at(test_buffer2, TE_ERR_MISSING_KEY, 0, 0, "background");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(remove_once(test_buffer2, sizeof test_buffer2,
                         "    \"radius_small\": 6,\n"));
    expect_parse_error_at(test_buffer2, TE_ERR_MISSING_KEY, 0, 0, "radius_small");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "    \"background\": \"0B0E14FF\",\n",
                          "    \"background\": \"0B0E14FF\",\n"
                          "    \"background\": \"111111FF\",\n"));
    const char *first_background = strstr(test_buffer2, "\"background\"");
    const char *second_background =
        first_background != NULL ? strstr(first_background + 1, "\"background\"") : NULL;
    pt_check(second_background != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_DUPLICATE_KEY,
                             second_background != NULL
                                 ? (size_t)(second_background - test_buffer2)
                                 : 0,
                             "duplicate");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 1e3"));
    const char *exponent = strstr(test_buffer2, "1e3");
    pt_check(exponent != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             exponent != NULL ? (size_t)(exponent - test_buffer2) + 1 : 0,
                             "exponent");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": NaN"));
    const char *nan = strstr(test_buffer2, "NaN");
    pt_check(nan != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             nan != NULL ? (size_t)(nan - test_buffer2) : 0, "number");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": Infinity"));
    const char *infinity = strstr(test_buffer2, "Infinity");
    pt_check(infinity != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             infinity != NULL ? (size_t)(infinity - test_buffer2) : 0,
                             "number");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": -1"));
    const char *negative = strstr(test_buffer2, "-1");
    pt_check(negative != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_RANGE,
                             negative != NULL ? (size_t)(negative - test_buffer2) : 0,
                             "negative");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 1000000000"));
    const char *huge = strstr(test_buffer2, "1000000000");
    pt_check(huge != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_RANGE,
                             huge != NULL ? (size_t)(huge - test_buffer2) : 0, "canonical");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 0.00001"));
    const char *tiny = strstr(test_buffer2, "0.00001");
    pt_check(tiny != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_RANGE,
                             tiny != NULL ? (size_t)(tiny - test_buffer2) : 0, "canonical");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 0.0000000000000000000000000000000000000000000000000000000000001"));
    const char *underflow = strstr(test_buffer2, "0.0000000000000000000000000000000000000000000000000000000000001");
    pt_check(underflow != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_RANGE,
                             underflow != NULL ? (size_t)(underflow - test_buffer2) : 0,
                             "underflow");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 06"));
    const char *leading = strstr(test_buffer2, "06");
    pt_check(leading != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             leading != NULL ? (size_t)(leading - test_buffer2) + 1 : 0,
                             "leading zero");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": \"6\""));
    const char *metric_string = strstr(test_buffer2, "\"radius_small\": \"6\"");
    pt_check(metric_string != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             metric_string != NULL
                                 ? (size_t)(metric_string - test_buffer2) +
                                       strlen("\"radius_small\": ")
                                 : 0,
                             "number");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"background\": \"0B0E14FF\"", "\"background\": \"GGGGGG\""));
    const char *bad_color = strstr(test_buffer2, "\"GGGGGG\"");
    pt_check(bad_color != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             bad_color != NULL ? (size_t)(bad_color - test_buffer2) : 0,
                             "invalid colour");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"background\": \"0B0E14FF\"", "\"background\": \"12345\""));
    const char *short_color = strstr(test_buffer2, "\"12345\"");
    pt_check(short_color != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             short_color != NULL ? (size_t)(short_color - test_buffer2) : 0,
                             "invalid colour");

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"background\": \"0B0E14FF\"", "\"background\": 123456"));
    const char *numeric_color = strstr(test_buffer2, "\"background\": 123456");
    pt_check(numeric_color != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             numeric_color != NULL
                                 ? (size_t)(numeric_color - test_buffer2) +
                                       strlen("\"background\": ")
                                 : 0,
                             "must be a string");

    /* A locale-style comma decimal is two JSON tokens and never a number. */
    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "\"radius_small\": 6", "\"radius_small\": 6,5"));
    const char *comma = strstr(test_buffer2, "6,5");
    pt_check(comma != NULL);
    expect_parse_error_index(test_buffer2, TE_ERR_SYNTAX,
                             comma != NULL ? (size_t)(comma - test_buffer2) + 2 : 0,
                             "object key");
}

static void test_unknown_keys(void) {
    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc base;
    te_parse_report base_report;
    pt_check(parse_text(fixture_text, &base, &base_report));
    pt_check(te_serialize_json(&base, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);

    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2, "{\n  \"schema\"",
                          "{\n  \"future_top\": {\"a\": [1, 2, {\"b\": true}]},\n"
                          "  \"schema\""));
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "    \"background\": \"0B0E14FF\",\n",
                          "    \"background\": \"0B0E14FF\",\n"
                          "    \"future_colour\": \"112233FF\",\n"));
    pt_check(replace_once(test_buffer2, sizeof test_buffer2,
                          "    \"radius_small\": 6,\n",
                          "    \"radius_small\": 6,\n"
                          "    \"future_metric\": 5,\n"));

    te_theme_doc doc;
    te_parse_report report;
    pt_check(parse_text(test_buffer2, &doc, &report));
    pt_check(report.error == TE_ERR_OK);
    pt_check(report.warning_count == 3);
    pt_check(strstr(report.warnings[0], "future_top") != NULL);
    pt_check(strstr(report.warnings[1], "future_colour") != NULL);
    pt_check(strstr(report.warnings[2], "future_metric") != NULL);
    pt_check(strcmp(doc.name, base.name) == 0);
    pt_check(doc.colors[pt_theme_background] == base.colors[pt_theme_background]);
    pt_check(doc.metrics[pt_theme_metric_radius_small] ==
             base.metrics[pt_theme_metric_radius_small]);

    te_parse_options strict = {.strict = true};
    te_theme_doc strict_doc = sentinel_doc();
    te_theme_doc strict_before = strict_doc;
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&strict_doc, test_buffer2, strlen(test_buffer2), &strict,
                           &report) == TE_ERR_UNKNOWN_KEY);
    pt_check(report.error == TE_ERR_UNKNOWN_KEY);
    pt_check(report.warning_count == 0);
    const char *strict_key = strstr(test_buffer2, "\"future_top\"");
    size_t strict_line = 0;
    size_t strict_column = 0;
    if (strict_key != NULL) {
        line_col_at(test_buffer2, (size_t)(strict_key - test_buffer2), &strict_line,
                    &strict_column);
    }
    pt_check(strict_key != NULL);
    pt_check(report.line == strict_line && report.column == strict_column);
    pt_check(strstr(report.message, "future_top") != NULL);
    pt_check(memcmp(&strict_doc, &strict_before, sizeof strict_doc) == 0);

    /* Warnings are bounded to TE_MAX_WARNINGS entries, all NUL terminated. */
    char replacement[4096];
    size_t used = 0;
    used += (size_t)snprintf(replacement + used, sizeof replacement - used, "{\n");
    for (int i = 0; i < 40; ++i) {
        used += (size_t)snprintf(replacement + used, sizeof replacement - used,
                                 "  \"k%02d\": 0,\n", i);
    }
    used += (size_t)snprintf(replacement + used, sizeof replacement - used,
                             "  \"schema\"");
    pt_check(used < sizeof replacement);
    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2, "{\n  \"schema\"", replacement));
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&doc, test_buffer2, strlen(test_buffer2), NULL, &report) ==
             TE_ERR_OK);
    pt_check(report.warning_count == TE_MAX_WARNINGS);
    for (unsigned i = 0; i < report.warning_count; ++i) {
        pt_check(report.warnings[i][TE_THEME_WARNING_MAX - 1] == 0);
        pt_check(report.warnings[i][0] != 0);
    }

    /* Seven nested arrays plus the root object is exactly the depth limit. */
    memcpy(test_buffer2, test_buffer, sizeof test_buffer2);
    pt_check(replace_once(test_buffer2, sizeof test_buffer2, "{\n  \"schema\"",
                          "{\n  \"nest\": [[[[[[[0]]]]]]],\n  \"schema\""));
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&doc, test_buffer2, strlen(test_buffer2), NULL, &report) ==
             TE_ERR_OK);
    pt_check(report.warning_count == 1);
    pt_check(strstr(report.warnings[0], "nest") != NULL);

    /* A successful parse clears warnings left by an earlier report. */
    memset(&report, 0, sizeof report);
    pt_check(te_parse_json(&doc, test_buffer, strlen(test_buffer), NULL, &report) ==
             TE_ERR_OK);
    pt_check(report.warning_count == 0);
}static void test_c_export(void) {
    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc doc;
    te_parse_report report;
    pt_check(parse_text(fixture_text, &doc, &report));

    size_t required = te_export_c_size(&doc);
    pt_check(required > 512);
    size_t written = 0;
    pt_check(te_export_c(&doc, test_buffer, sizeof test_buffer, &written) == TE_ERR_OK);
    pt_check(written + 1 == required);
    pt_check(test_buffer[written] == 0);
    pt_check(strstr(test_buffer,
                    "/* Generated by pt-theme-editor; schema pt-theme/1; "
                    "theme \"Default Dark\". */\n") == test_buffer);
    pt_check(strstr(test_buffer, "#include <pt/pt.h>\n\n") != NULL);
    pt_check(strstr(test_buffer,
                    "void theme_apply_default_dark(pt_app *app) {\n") != NULL);
    pt_check(strstr(test_buffer,
                    "    pt_app_set_color_scheme(app, pt_color_scheme_dark);\n")
             != NULL);
    pt_check(count_substr(test_buffer, "pt_app_set_theme_color(") == 38);
    pt_check(count_substr(test_buffer, "pt_app_set_theme_metric(") == 10);

    const char *scheme_line = strstr(test_buffer, "pt_app_set_color_scheme(");
    const char *first_color = strstr(test_buffer, "pt_app_set_theme_color(");
    const char *first_metric = strstr(test_buffer, "pt_app_set_theme_metric(");
    pt_check(scheme_line != NULL && first_color != NULL && first_metric != NULL);
    pt_check(scheme_line < first_color && first_color < first_metric);

    const char *cursor = test_buffer;
    for (int i = 0; i < pt_theme_token_count; ++i) {
        char expected[256];
        (void)snprintf(expected, sizeof expected,
                       "    pt_app_set_theme_color(app, pt_theme_%s, 0x%08Xu);\n",
                       te_color_name((pt_theme_token)i),
                       (unsigned)(doc.colors[i] & 0xFFFFFFFFu));
        const char *found = strstr(cursor, expected);
        pt_check(found != NULL);
        if (found == NULL) {
            break;
        }
        cursor = found + strlen(expected);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        char expected[256];
        (void)snprintf(expected, sizeof expected,
                       "    pt_app_set_theme_metric(app, pt_theme_metric_%s, ",
                       te_metric_name((pt_theme_metric)i));
        const char *found = strstr(cursor, expected);
        pt_check(found != NULL);
        if (found == NULL) {
            break;
        }
        cursor = found + strlen(expected);
    }
    pt_check(strstr(test_buffer, "pt_theme_metric_radius_small, 6.0f);") != NULL);
    pt_check(strstr(test_buffer, "pt_theme_metric_radius_pill, 999.0f);") != NULL);
    pt_check(test_buffer[written - 1] == '\n' && test_buffer[written - 2] == '}');

    char untouched[64];
    memset(untouched, 0x5A, sizeof untouched);
    written = 99;
    pt_check(te_export_c(&doc, untouched, required - 1, &written) == TE_ERR_ARGUMENT);
    pt_check(written == 0);
    for (size_t i = 0; i < sizeof untouched; ++i) {
        pt_check((unsigned char)untouched[i] == 0x5A);
    }
    pt_check(te_export_c_size(NULL) == 0);

    /* Light scheme and slug boundaries. */
    pt_check(load_fixture("tools/theme-editor/themes/light.json"));
    te_theme_doc light;
    te_parse_report light_report;
    pt_check(parse_text(fixture_text, &light, &light_report));
    pt_check(te_export_c(&light, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);
    pt_check(strstr(test_buffer,
                    "    pt_app_set_color_scheme(app, pt_color_scheme_light);\n")
             != NULL);
    pt_check(strstr(test_buffer,
                    "void theme_apply_default_light(pt_app *app) {\n") != NULL);

    te_theme_doc slug_doc = doc;
    (void)snprintf(slug_doc.name, sizeof slug_doc.name, "%s", "Ocean Blue");
    pt_check(te_export_c(&slug_doc, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);
    pt_check(strstr(test_buffer,
                    "void theme_apply_ocean_blue(pt_app *app) {\n") != NULL);
    pt_check(strstr(test_buffer, "theme \"Ocean Blue\". */") != NULL);

    slug_doc.name[0] = 0;
    pt_check(te_export_c(&slug_doc, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);
    pt_check(strstr(test_buffer,
                    "void theme_apply_custom_theme(pt_app *app) {\n") != NULL);

    (void)snprintf(slug_doc.name, sizeof slug_doc.name, "%s", "***");
    pt_check(te_export_c(&slug_doc, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);
    pt_check(strstr(test_buffer,
                    "void theme_apply____(pt_app *app) {\n") != NULL);

    char long_slug_name[96];
    memset(long_slug_name, 'A', 95);
    long_slug_name[95] = 0;
    (void)snprintf(slug_doc.name, sizeof slug_doc.name, "%s", long_slug_name);
    size_t long_written = 0;
    pt_check(te_export_c(&slug_doc, test_buffer, sizeof test_buffer, &long_written) ==
             TE_ERR_OK);
    pt_check(long_written > 0 && test_buffer[long_written] == 0);
    const char *long_function = strstr(test_buffer, "void theme_apply_");
    pt_check(long_function != NULL);
    pt_check(strstr(test_buffer, "(pt_app *app) {\n") != NULL);

    /* A name that tries to close the banner comment cannot inject code. */
    (void)snprintf(slug_doc.name, sizeof slug_doc.name, "%s",
                   "evil */ void hack(void) { } /*");
    pt_check(te_export_c(&slug_doc, test_buffer, sizeof test_buffer, NULL) == TE_ERR_OK);
    pt_check(count_substr(test_buffer, "*/") == 1);
    pt_check(strstr(test_buffer, "evil * / void hack") != NULL);
    pt_check(strstr(test_buffer, "void theme_apply_evil") != NULL);

    /* The generated translation unit is accepted by a C23 compiler. */
    const char *c_path = "/tmp/opencode/m27-w1-theme-export.c";
    FILE *file = fopen(c_path, "wb");
    pt_check(file != NULL);
    if (file != NULL) {
        size_t exported_length = 0;
        pt_check(te_export_c(&doc, test_buffer, sizeof test_buffer, &exported_length) ==
                 TE_ERR_OK);
        pt_check(fwrite(test_buffer, 1, exported_length, file) == exported_length);
        pt_check(fclose(file) == 0);
        pt_check(system("cc -std=c23 -Iinclude -fsyntax-only "
                        "/tmp/opencode/m27-w1-theme-export.c") == 0);
        (void)remove(c_path);
    }
}

static void test_file_io(void) {
    const char *json_path = "/tmp/opencode/m27-w1-theme.json";
    const char *c_path = "/tmp/opencode/m27-w1-theme.c";
    const char *missing_path = "/tmp/opencode/m27-w1-missing.json";
    const char *bad_dir_path = "/tmp/opencode/m27-w1-no-such-dir/theme.json";
    const char *large_path = "/tmp/opencode/m27-w1-large.json";

    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc doc;
    te_parse_report report;
    pt_check(parse_text(fixture_text, &doc, &report));
    size_t canonical_length = 0;
    pt_check(te_serialize_json(&doc, test_buffer, sizeof test_buffer, &canonical_length) ==
             TE_ERR_OK);

    pt_check(te_save_json_file(&doc, json_path) == TE_ERR_OK);
    te_theme_doc loaded;
    te_parse_report load_report;
    memset(&loaded, 0, sizeof loaded);
    memset(&load_report, 0, sizeof load_report);
    pt_check(te_load_json_file(&loaded, json_path, NULL, &load_report) == TE_ERR_OK);
    pt_check(load_report.error == TE_ERR_OK);
    pt_check(memcmp(&doc, &loaded, sizeof doc) == 0);

    pt_check(load_fixture(json_path));
    pt_check(strlen(fixture_text) == canonical_length);
    pt_check(memcmp(fixture_text, test_buffer, canonical_length) == 0);

    size_t exported_length = 0;
    pt_check(te_export_c(&doc, test_buffer, sizeof test_buffer, &exported_length) ==
             TE_ERR_OK);
    pt_check(te_export_c_file(&doc, c_path) == TE_ERR_OK);
    pt_check(load_fixture(c_path));
    pt_check(strlen(fixture_text) == exported_length);
    pt_check(memcmp(fixture_text, test_buffer, exported_length) == 0);

    memset(&report, 0, sizeof report);
    pt_check(te_load_json_file(&loaded, missing_path, NULL, &report) == TE_ERR_IO);
    pt_check(report.error == TE_ERR_IO && report.line == 0 && report.column == 0);
    pt_check(te_save_json_file(&doc, bad_dir_path) == TE_ERR_IO);
    pt_check(te_export_c_file(&doc, bad_dir_path) == TE_ERR_IO);

    FILE *large = fopen(large_path, "wb");
    pt_check(large != NULL);
    if (large != NULL) {
        pt_check(fseek(large, (long)TE_THEME_MAX_FILE_SIZE, SEEK_SET) == 0);
        pt_check(fputc('x', large) == 'x');
        pt_check(fclose(large) == 0);
        memset(&report, 0, sizeof report);
        pt_check(te_load_json_file(&loaded, large_path, NULL, &report) ==
                 TE_ERR_TOO_LARGE);
        pt_check(report.error == TE_ERR_TOO_LARGE);
        (void)remove(large_path);
    }

    (void)remove(json_path);
    (void)remove(c_path);

    /* The read path parses exactly the bytes the writer produced. */
    size_t check_length = 0;
    pt_check(te_serialize_json(&doc, test_buffer, sizeof test_buffer, &check_length) ==
             TE_ERR_OK);
    pt_check(check_length == canonical_length);
    pt_check(parse_text(test_buffer, &loaded, &load_report));
    pt_check(load_report.warning_count == 0);
}

static void test_locale_independence(void) {
    pt_check(load_fixture("tools/theme-editor/themes/dark.json"));
    te_theme_doc doc;
    te_parse_report report;
    pt_check(parse_text(fixture_text, &doc, &report));
    doc.metrics[pt_theme_metric_radius_small] = 6.5f;

    size_t written = 0;
    pt_check(te_serialize_json(&doc, test_buffer, sizeof test_buffer, &written) ==
             TE_ERR_OK);
    pt_check(strstr(test_buffer, "\"radius_small\": 6.5,\n") != NULL);
    pt_check(strstr(test_buffer, "6,5") == NULL);

    char saved[128];
    const char *current = setlocale(LC_NUMERIC, NULL);
    if (current != NULL) {
        (void)snprintf(saved, sizeof saved, "%s", current);
        const char *changed = setlocale(LC_NUMERIC, "de_DE.UTF-8");
        if (changed == NULL) {
            changed = setlocale(LC_NUMERIC, "fr_FR.UTF-8");
        }
        if (changed == NULL) {
            changed = setlocale(LC_NUMERIC, "de_DE.utf8");
        }
        if (changed != NULL) {
            pt_check(te_serialize_json(&doc, test_buffer, sizeof test_buffer, &written) ==
                     TE_ERR_OK);
            pt_check(strstr(test_buffer, "\"radius_small\": 6.5,\n") != NULL);
            pt_check(strstr(test_buffer, "6,5") == NULL);
            te_theme_doc reparsed;
            te_parse_report reparse_report;
            pt_check(parse_text(test_buffer, &reparsed, &reparse_report));
            pt_check(reparsed.metrics[pt_theme_metric_radius_small] == 6.5f);
        }
        (void)setlocale(LC_NUMERIC, saved);
    }
}int main(void) {
    test_signatures();
    test_color_names();
    test_metric_names();
    test_scheme_names();
    test_error_strings();
    test_slugify();
    test_stub_error_paths();
    test_fixtures();
    test_golden_round_trip();
    test_canonical_output();
    test_import_tolerance();
    test_rejections();
    test_unknown_keys();
    test_c_export();
    test_file_io();
    test_locale_independence();
    return pt_test_finish("theme_format");
}