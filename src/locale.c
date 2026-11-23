/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <string.h>

/* Bounded locale-to-direction lookup (since 2.1).
 *
 * The table is deliberately conservative and ICU-free: it is a short list of
 * scripts and languages whose default direction is RTL. A locale the table does
 * not know is LTR. No allocation, no setlocale, no environment access and no
 * global state: the helper only reads the caller's string. */

static const char *const locale_rtl_scripts[] = {
    "adlm", "arab", "hebr", "mand", "mend", "nkoo", "rohg", "samr", "syrc", "thaa",
};

static const char *const locale_rtl_languages[] = {
    "ar", "arc", "bal", "bgn", "ckb", "dv",  "fa", "glk", "he", "iw",
    "khw", "ks", "lrc", "mzn", "nqo", "prs", "ps", "rhg", "sd", "syr",
    "ug", "ur", "yi", "ji",
};

static bool locale_tag_in_table(const char *tag, size_t length,
                                const char *const *table, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const char *word = table[i];
        size_t j = 0;
        for (; j < length; ++j) {
            char c = tag[j];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            if (word[j] == '\0' || word[j] != c) {
                break;
            }
        }
        if (j == length && word[j] == '\0') {
            return true;
        }
    }
    return false;
}

static bool locale_is_alpha(const char *tag, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        char c = tag[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            return false;
        }
    }
    return true;
}

pt_ui_direction pt_locale_ui_direction(const char *locale) {
    if (!locale || !*locale) {
        return pt_ui_direction_ltr;
    }
    /* At most 64 bytes are examined; the codeset after '.' and the modifier
     * after '@' are ignored. POSIX separators '_' are treated like '-'. */
    char buffer[65];
    size_t length = 0;
    while (length < sizeof(buffer) - 1 && locale[length] &&
           locale[length] != '.' && locale[length] != '@') {
        char c = locale[length];
        if (c == '_') {
            c = '-';
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        buffer[length] = c;
        ++length;
    }
    buffer[length] = '\0';
    if (length == 0) {
        return pt_ui_direction_ltr;
    }

    size_t language_length = 0;
    while (language_length < length && buffer[language_length] != '-') {
        ++language_length;
    }
    if (language_length == 0 || !locale_is_alpha(buffer, language_length)) {
        return pt_ui_direction_ltr;
    }

    /* A four-letter alphabetic subtag is a script and wins over the language:
     * az-Arab is RTL while plain az and az-Latn are LTR. */
    size_t start = language_length + 1;
    while (start < length) {
        size_t end = start;
        while (end < length && buffer[end] != '-') {
            ++end;
        }
        size_t sub_length = end - start;
        if (sub_length == 4 && locale_is_alpha(buffer + start, sub_length)) {
            return locale_tag_in_table(buffer + start, sub_length, locale_rtl_scripts,
                                       sizeof(locale_rtl_scripts) /
                                           sizeof(locale_rtl_scripts[0]))
                       ? pt_ui_direction_rtl
                       : pt_ui_direction_ltr;
        }
        start = end + 1;
    }

    return locale_tag_in_table(buffer, language_length, locale_rtl_languages,
                               sizeof(locale_rtl_languages) /
                                   sizeof(locale_rtl_languages[0]))
               ? pt_ui_direction_rtl
               : pt_ui_direction_ltr;
}
