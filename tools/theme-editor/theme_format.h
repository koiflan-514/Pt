/*
 * Pt theme editor - pt-theme/1 document model, bounded codec and C exporter.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_theme_editor_theme_format_h
#define pt_theme_editor_theme_format_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pt/theme.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frozen interface for the format module (M27 W0).
 *
 * The document mirrors the public theme table so the editor never invents a
 * token: pt_theme_token and pt_theme_metric are the only role/metric indices.
 * Colours use the public 0xRRGGBBAA layout and metrics are logical pixels.
 *
 * Ownership: every function takes caller-owned storage or a caller-owned
 * document and transfers no allocation. te_parse_json() writes into *doc only
 * after a complete valid document has been decoded; on error the destination
 * is left untouched. Output buffers are borrowed from the caller and are never
 * retained. No function calls into Pt state, opens a window or reads a locale.
 *
 * Bounded parser: the input is at most TE_THEME_MAX_FILE_SIZE bytes (1 MiB),
 * JSON nesting is at most TE_THEME_MAX_DEPTH (8), the theme name is at most
 * TE_THEME_NAME_MAX_BYTES (95) UTF-8 bytes, every known role and metric is
 * required, duplicate keys are rejected, and trailing bytes after the top-level
 * object are rejected. Unknown keys are warnings unless options->strict is set.
 * Numbers are parsed without strtod(), so a locale decimal separator cannot
 * change the document.
 */

/* Schema version written and accepted by this module. */
#define TE_THEME_SCHEMA_V1 "pt-theme/1"

/* Limits, also documented in tools/theme-editor/README.md and ->theming.md. */
#define TE_THEME_NAME_MAX 96u      /* array size, including the NUL */
#define TE_THEME_NAME_MAX_BYTES 95u /* maximum UTF-8 bytes, excluding the NUL */
#define TE_THEME_MAX_FILE_SIZE (1024u * 1024u)
#define TE_THEME_MAX_DEPTH 8u

/* Parse diagnostics. Every warning is a full message; the count is capped. */
#define TE_MAX_WARNINGS 32u
#define TE_THEME_MESSAGE_MAX 192u
#define TE_THEME_WARNING_MAX 160u

/* The in-memory pt-theme/1 document.
 *
 * name is a NUL-terminated UTF-8 string of at most 95 bytes (the parser rejects
 * a longer value). scheme records the base palette that seeded the document and
 * is the scheme the C exporter applies first; it is never inferred from the
 * colours. colors has one entry per pt_theme_token in enum order and metrics has
 * one finite, non-negative value per pt_theme_metric in enum order. A document
 * is plain data and may be copied by value.
 */
typedef struct te_theme_doc {
    char name[TE_THEME_NAME_MAX];
    pt_color_scheme scheme;
    pt_color colors[pt_theme_token_count];
    float metrics[pt_theme_metric_count];
} te_theme_doc;

/* Stable error codes. New values are appended at the end; existing values do
 * not move. TE_ERR_NOT_IMPLEMENTED is reserved for the W0 scaffold and is not
 * returned by the finished reader. */
typedef enum te_format_error {
    TE_ERR_OK = 0,
    TE_ERR_ARGUMENT,        /* NULL document, buffer, path or colours */
    TE_ERR_IO,              /* could not read or write the file */
    TE_ERR_TOO_LARGE,       /* input exceeded TE_THEME_MAX_FILE_SIZE */
    TE_ERR_DEPTH,           /* JSON nesting exceeded TE_THEME_MAX_DEPTH */
    TE_ERR_SYNTAX,          /* malformed JSON, bad escape, bad UTF-8,
                               unpaired surrogate or trailing garbage */
    TE_ERR_SCHEMA,          /* missing or wrong "schema" */
    TE_ERR_SCHEME,          /* missing or wrong "scheme" */
    TE_ERR_MISSING_KEY,     /* a known role or metric is absent */
    TE_ERR_DUPLICATE_KEY,   /* the same key appears twice in one object */
    TE_ERR_RANGE,           /* metric is negative or not finite */
    TE_ERR_NAME,            /* theme name is too long or not valid UTF-8 */
    TE_ERR_UNKNOWN_KEY,     /* unknown key under strict mode */
    TE_ERR_NOT_IMPLEMENTED, /* W0 scaffold only */
} te_format_error;

/* Parse diagnostics. Zero-initialise before the call; the parser fills it.
 * line and column are 1-based and are 0 when the error is not positional.
 * message and warnings are always NUL-terminated when set.
 *
 * Warnings are unknown-key notices collected in document order. Under
 * options->strict the first warning becomes TE_ERR_UNKNOWN_KEY and no warning
 * is recorded. A report may be NULL; diagnostics are then discarded.
 */
typedef struct te_parse_report {
    te_format_error error;
    size_t line;
    size_t column;
    char message[TE_THEME_MESSAGE_MAX];
    unsigned warning_count;
    char warnings[TE_MAX_WARNINGS][TE_THEME_WARNING_MAX];
} te_parse_report;

/* Parser options. strict promotes unknown-key warnings to a TE_ERR_UNKNOWN_KEY
 * error, which is what the CLI --strict mode uses. All other validation is
 * always fatal. */
typedef struct te_parse_options {
    bool strict;
} te_parse_options;

/* Parses exactly one pt-theme/1 JSON object from text.
 *
 * text need not be NUL-terminated; length is authoritative. A trailing byte
 * after the closing brace (other than JSON whitespace) is TE_ERR_SYNTAX.
 * options may be NULL (non-strict). On TE_ERR_OK *doc holds a complete
 * document, else *doc is untouched. Returns the same value as report->error
 * when report is non-NULL. Safe with text == NULL only when length == 0 and
 * then returns TE_ERR_SYNTAX; a NULL doc is TE_ERR_ARGUMENT.
 */
te_format_error te_parse_json(te_theme_doc *doc, const char *text, size_t length,
                              const te_parse_options *options, te_parse_report *report);

/* Reads path (bounded to TE_THEME_MAX_FILE_SIZE), then parses it. The file is
 * opened read-only and closed before returning. A read failure is TE_ERR_IO and
 * an oversized file is TE_ERR_TOO_LARGE without reading the whole file. */
te_format_error te_load_json_file(te_theme_doc *doc, const char *path,
                                  const te_parse_options *options,
                                  te_parse_report *report);

/* Canonical JSON output.
 *
 * Canonical form is fixed token/metric enum order, two-space indent, uppercase
 * 8-digit colours without '#', metrics with "%.9g", one trailing newline and no
 * other whitespace freedom. Exporting the same document twice is byte-identical
 * and re-parsing the output recovers every value. te_serialize_json_size()
 * returns the required capacity in bytes including the terminating NUL; it is
 * 0 for a NULL document.
 *
 * te_serialize_json() writes at most capacity bytes and NUL-terminates. It
 * returns TE_ERR_ARGUMENT for a NULL doc/buffer or a too-small capacity and
 * leaves the buffer untouched on error. On success *written (when non-NULL) is
 * the number of bytes before the NUL, including the trailing newline.
 */
size_t te_serialize_json_size(const te_theme_doc *doc);
te_format_error te_serialize_json(const te_theme_doc *doc, char *buffer,
                                  size_t capacity, size_t *written);

/* Canonical JSON written to path. Creates or truncates the file. */
te_format_error te_save_json_file(const te_theme_doc *doc, const char *path);

/* C source export.
 *
 * The output is one complete, plain C23 translation unit: a banner comment,
 * #include <pt/pt.h> and void theme_apply_<slug>(pt_app *app) that sets the
 * scheme, every colour and every metric through public API in enum order. The
 * slug is te_slugify_name(name). Sizes and buffer rules match the JSON writer;
 * te_export_c_size() returns capacity including the NUL. On success *written is
 * the byte count before the NUL.
 */
size_t te_export_c_size(const te_theme_doc *doc);
te_format_error te_export_c(const te_theme_doc *doc, char *buffer, size_t capacity,
                            size_t *written);
te_format_error te_export_c_file(const te_theme_doc *doc, const char *path);

/* Writes the C identifier slug of name into out: lowercase ASCII letters,
 * digits and '_' only; every other byte becomes '_', runs are not folded, and
 * an empty result becomes "custom_theme". Always NUL-terminates when
 * out_capacity > 0 and never writes more than out_capacity - 1 bytes. */
void te_slugify_name(const char *name, char *out, size_t out_capacity);

/* Name tables, indexed by enum in enum order.
 *
 * te_color_name()/te_metric_name() return a static borrowed string, or NULL for
 * an out-of-range index. The reverse functions compare exactly `length` bytes
 * (pass strlen(name) for a NUL-terminated key) and return false for an unknown
 * name or a NULL name. Names are unique, non-empty and stable; the .c file
 * static-asserts the 38/10 counts so a core role addition breaks the build.
 */
const char *te_color_name(pt_theme_token token);
const char *te_metric_name(pt_theme_metric metric);
const char *te_scheme_name(pt_color_scheme scheme);
bool te_color_from_name(const char *name, size_t length, pt_theme_token *out);
bool te_metric_from_name(const char *name, size_t length, pt_theme_metric *out);
bool te_scheme_from_name(const char *name, size_t length, pt_color_scheme *out);

/* Stable English description of an error code; never NULL. Unknown codes return
 * "unknown error". */
const char *te_format_error_string(te_format_error error);

#ifdef __cplusplus
}
#endif

#endif