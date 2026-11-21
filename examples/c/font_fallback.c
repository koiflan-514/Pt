/*
 * Pt per-run font fallback: one mixed Latin/CJK/Arabic/Greek line, an editable
 * fallback chain and a coverage counter.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/app.h):
 *
 *  - A UTF-8 run is split into script segments. A segment is shaped with the
 *    primary family when it covers every character, otherwise with the first
 *    family in the fallback chain that does, and finally with Fontconfig's own
 *    order when no explicit chain is installed.
 *  - The chain is process-wide and copied. A NULL/empty list restores the
 *    built-in Fontconfig order; an explicit chain is authoritative, so a
 *    character that none of its families covers stays tofu even when another
 *    installed face would have it (the DejaVu-only button proves this).
 *  - A per-family override replaces the app default for that exact primary
 *    family string ("sans-serif" here). Clearing it falls back to the default.
 *  - pt_app_font_uncovered_count() counts codepoints, not bytes, and reports 0
 *    for NULL/empty text.
 *
 * Font names were checked with `fc-list` on the development machine:
 *   Noto Sans CJK SC, Noto Sans Arabic, Noto Color Emoji, DejaVu Sans.
 * When a face is missing the example still runs: the label shows tofu and the
 * coverage line explains which families to install.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_TEXT                                                                               \
    "Latin · Ελληνικά · 中文与日本語 · العربية · emoji 😀 · Кириллица"

typedef struct fallback_state {
    pt_app *app;
    pt_window *window;
    pt_widget *sample;
    pt_widget *chain;
    pt_widget *coverage;
    pt_widget *status;
    unsigned actions;
} fallback_state;

static const char *const cjk_arabic_chain[] = {
    "Noto Sans CJK SC",
    "Noto Sans Arabic",
    "Noto Color Emoji",
    "DejaVu Sans",
};

static const char *const dejavu_only_chain[] = {"DejaVu Sans"};

static const char *const cjk_only_override[] = {"Noto Sans CJK SC"};

static void reflow(fallback_state *state) {
    /* Re-setting the same text invalidates the shaped runs, which is the
     * documented way to apply a freshly installed chain to existing widgets. */
    pt_widget_set_text(state->sample, SAMPLE_TEXT);
    pt_widget_request_redraw(state->sample);
}

static void append(char *buffer, size_t capacity, size_t *used, const char *format, ...) {
    if (*used + 1 >= capacity) {
        return;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    *used += (size_t)written;
    if (*used >= capacity) {
        *used = capacity - 1;
    }
}

static void describe_chain(fallback_state *state, const char *what) {
    char text[512];
    size_t used = 0;
    int count = pt_app_get_font_fallback_count(state->app);
    append(text, sizeof(text), &used, "%s · default chain: ", what);
    if (count == 0) {
        append(text, sizeof(text), &used, "built-in Fontconfig order");
    }
    for (int index = 0; index < count; ++index) {
        const char *family = pt_app_get_font_fallback(state->app, index);
        append(text, sizeof(text), &used, "%s%s", index == 0 ? "" : " → ",
               family ? family : "?");
    }
    int override = pt_app_get_font_fallback_count_for_family(state->app, "sans-serif");
    append(text, sizeof(text), &used, " · sans-serif override: %d famil%s", override,
           override == 1 ? "y" : "ies");
    for (int index = 0; index < override; ++index) {
        const char *family =
            pt_app_get_font_fallback_for_family(state->app, "sans-serif", index);
        append(text, sizeof(text), &used, "%s%s", index == 0 ? " (" : ", ",
               family ? family : "?");
        if (index == override - 1) {
            append(text, sizeof(text), &used, ")");
        }
    }
    pt_widget_set_text(state->chain, text);
}

static void describe_coverage(fallback_state *state) {
    int uncovered = pt_app_font_uncovered_count(state->app, "sans-serif", SAMPLE_TEXT,
                                                strlen(SAMPLE_TEXT));
    char text[320];
    if (uncovered == 0) {
        snprintf(text, sizeof(text),
                 "Coverage: every codepoint in the sample is covered (%d uncovered)",
                 uncovered);
    } else {
        snprintf(text, sizeof(text),
                 "Coverage: %d codepoint%s uncovered; install Noto Sans CJK SC, "
                 "Noto Sans Arabic and Noto Color Emoji for the full sample",
                 uncovered, uncovered == 1 ? " is" : "s are");
    }
    pt_widget_set_text(state->coverage, text);
    char status[384];
    snprintf(status, sizeof(status), "Action #%u · %s", ++state->actions, text);
    pt_widget_set_text(state->status, status);
    printf("font_fallback: %s\n", text);
    fflush(stdout);
}

static void on_install_chain(pt_widget *button, void *user_data) {
    (void)button;
    fallback_state *state = user_data;
    pt_app_set_font_fallbacks(state->app, cjk_arabic_chain,
                              (int)(sizeof(cjk_arabic_chain) / sizeof(cjk_arabic_chain[0])));
    reflow(state);
    describe_chain(state, "Installed CJK + Arabic + emoji chain");
    describe_coverage(state);
}

static void on_default_chain(pt_widget *button, void *user_data) {
    (void)button;
    fallback_state *state = user_data;
    pt_app_set_font_fallbacks(state->app, NULL, 0);
    reflow(state);
    describe_chain(state, "Restored the Fontconfig default");
    describe_coverage(state);
}

static void on_dejavu_only(pt_widget *button, void *user_data) {
    (void)button;
    fallback_state *state = user_data;
    pt_app_set_font_fallbacks(state->app, dejavu_only_chain, 1);
    reflow(state);
    describe_chain(state, "Authoritative DejaVu-only chain");
    describe_coverage(state);
}

static void on_override_sans(pt_widget *button, void *user_data) {
    (void)button;
    fallback_state *state = user_data;
    pt_app_set_font_fallbacks_for_family(state->app, "sans-serif", cjk_only_override, 1);
    reflow(state);
    describe_chain(state, "sans-serif override → CJK only");
    describe_coverage(state);
}

static void on_clear_override(pt_widget *button, void *user_data) {
    (void)button;
    fallback_state *state = user_data;
    pt_app_set_font_fallbacks_for_family(state->app, "sans-serif", NULL, 0);
    reflow(state);
    describe_chain(state, "sans-serif override cleared");
    describe_coverage(state);
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.font_fallback");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0xE0AF68));
    pt_app_set_font_family(app, "sans-serif");

    pt_window *window = pt_window_create(app, "Pt Font Fallback", 820, 620);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    fallback_state state = {.app = app, .window = window};
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "TEXT  ·  PER-RUN FALLBACK  ·  1.2");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0xE0AF68));

    pt_widget *heading = pt_label_create(root, "One line, many scripts");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "Pt segments the run by script and shapes each segment with the first family that "
              "covers it. The chain below is process-wide and copied; clearing it restores "
              "Fontconfig's own order.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 20.0f, 20.0f, 20.0f, 20.0f);
    pt_widget_set_gap(card, 12.0f);

    state.sample = pt_label_create(card, SAMPLE_TEXT);
    pt_widget_set_font_size(state.sample, 26.0f);
    pt_widget_set_font_weight(state.sample, pt_font_weight_medium);
    pt_widget_set_label_wrap(state.sample, true);
    pt_widget_set_a11y_name(state.sample, "Mixed-script sample");
    pt_widget_set_a11y_description(
        state.sample,
        "Latin, Greek, Chinese, Japanese, Arabic, emoji and Cyrillic in one UTF-8 run.");

    state.chain = pt_label_create(card, "");
    pt_widget_set_font_size(state.chain, 12.0f);
    pt_widget_set_foreground(state.chain, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(state.chain, true);

    state.coverage = pt_label_create(card, "");
    pt_widget_set_font_size(state.coverage, 12.0f);
    pt_widget_set_foreground(state.coverage, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.coverage, true);

    pt_widget *tools = pt_wrap_create(root);
    pt_wrap_set_gap(tools, 8.0f, 8.0f);
    pt_widget *install = pt_button_create(tools, "Install CJK + Arabic chain");
    pt_button_set_variant(install, pt_button_primary);
    pt_button_set_on_click(install, on_install_chain, &state);
    pt_widget_set_tooltip(install, "Noto Sans CJK SC → Noto Sans Arabic → emoji → DejaVu Sans");
    pt_widget *fallback_default = pt_button_create(tools, "Fontconfig order");
    pt_button_set_variant(fallback_default, pt_button_secondary);
    pt_button_set_on_click(fallback_default, on_default_chain, &state);
    pt_widget *dejavu = pt_button_create(tools, "DejaVu only (authoritative)");
    pt_button_set_variant(dejavu, pt_button_soft);
    pt_button_set_on_click(dejavu, on_dejavu_only, &state);
    pt_widget_set_tooltip(dejavu, "An explicit chain wins: CJK and Arabic become tofu");
    pt_widget *override = pt_button_create(tools, "Override sans-serif → CJK");
    pt_button_set_variant(override, pt_button_ghost);
    pt_button_set_on_click(override, on_override_sans, &state);
    pt_widget *clear = pt_button_create(tools, "Clear sans-serif override");
    pt_button_set_variant(clear, pt_button_ghost);
    pt_button_set_on_click(clear, on_clear_override, &state);

    state.status = pt_label_create(root, "Ready · choose a chain to reshape the sample");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0xE0AF68));
    pt_widget_set_label_wrap(state.status, true);
    pt_widget_set_a11y_name(state.status, "Fallback status");

    pt_widget *footer = pt_label_create(
        root, "Metrics compose across the faces actually used, so a mixed line reserves room for "
              "the tallest fallback. Caret and selection offsets stay in logical UTF-8 bytes. "
              "Missing faces leave tofu; the coverage line names the packages to install.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(install);
    describe_chain(&state, "Initial state");
    describe_coverage(&state);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        on_install_chain(NULL, &state);
        on_override_sans(NULL, &state);
        on_clear_override(NULL, &state);
        on_dejavu_only(NULL, &state);
        on_default_chain(NULL, &state);
        printf("font_fallback: default chain count=%d\n",
               pt_app_get_font_fallback_count(app));
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}
