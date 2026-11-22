/*
 * Modern visual states, rich-text run metrics and PNG snapshot regression.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct chip_log {
    int clicks;
    int removes;
} chip_log;

static void chip_clicked(pt_widget *chip, void *data) {
    (void)chip;
    chip_log *log = data;
    ++log->clicks;
}

static void chip_removed(pt_widget *chip, void *data) {
    (void)chip;
    chip_log *log = data;
    ++log->removes;
}

static void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 0);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 0);
}

static void test_chip_states(pt_app *app) {
    pt_window *window = pt_window_create(app, "chips", 360, 160);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget *chip = pt_chip_create(root, "Filter");
    pt_check(chip != NULL);
    pt_widget_set_align_self(chip, pt_align_start);
    chip_log log = {0};
    pt_chip_set_icon(chip, pt_icon_check);
    pt_chip_set_removable(chip, true);
    pt_chip_set_on_click(chip, chip_clicked, &log);
    pt_chip_set_on_remove(chip, chip_removed, &log);
    pt_check(pt_chip_get_removable(chip));
    pt_chip_set_selected(chip, true);
    pt_check(pt_chip_get_selected(chip));

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 160, 1.0f);
    pt_check(canvas && pt_window_paint(window, canvas));

    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    pt_widget_get_bounds(chip, &x, &y, &w, &h);
    pt_check(w > 40.0f && h > 20.0f);
    click_at(window, x + 12.0f, y + h * 0.5f);
    pt_check(log.clicks == 1);
    pt_check(log.removes == 0);
    click_at(window, x + w - 8.0f, y + h * 0.5f);
    pt_check(log.removes == 1);
    pt_check(pt_window_paint(window, canvas));

    /* Keyboard focus shows the visible ring; pointer focus hides it. */
    pt_check(pt_widget_focus(chip));
    pt_check(chip->focus_visible);
    pt_widget *entry = pt_entry_create(root, "text");
    pt_check(entry != NULL);
    pt_widget_set_size(entry, 120.0f, 30.0f);
    pt_check(pt_window_paint(window, canvas));
    pt_window_input_pointer_enter(window, entry->x + 10.0f, entry->y + 10.0f);
    pt_window_input_pointer_button(window, 0x110u, true, entry->x + 10.0f,
                                   entry->y + 10.0f, 3, 40);
    pt_window_input_pointer_button(window, 0x110u, false, entry->x + 10.0f,
                                   entry->y + 10.0f, 4, 50);
    pt_check(chip->focused == false);
    pt_check(entry->focused);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static void test_rich_text_runs(pt_app *app) {
    pt_window *window = pt_window_create(app, "rich", 420, 160);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *label = pt_label_create(pt_window_root(window), "Hello World");
    pt_text_span span = {0};
    span.start = 6;
    span.length = 5;
    span.flags = pt_text_span_has_size | pt_text_span_has_family;
    span.size = 24.0f;
    snprintf(span.family, sizeof(span.family), "serif");
    pt_check(pt_widget_set_text_spans(label, &span, 1));
    pt_check(pt_widget_get_text_span_count(label) == 1);

    pt_layout_ctx ctx = pt_layout_context_for(label);
    pt_text_runs runs = {0};
    pt_check(pt_text_runs_build(label, &ctx, &runs));
    pt_check(runs.count == 2);
    pt_check(runs.scale > 0.0f);
    if (runs.count == 2) {
        pt_check(runs.items[0].start == 0 && runs.items[0].end == 6);
        pt_check(runs.items[1].start == 6 && runs.items[1].end == 11);
        pt_check(runs.items[1].font->pixel_size > runs.items[0].font->pixel_size);
        pt_check(runs.ascent >= runs.items[1].font->ascent / runs.scale - 0.01f);
        pt_check(runs.line_height >= runs.ascent + runs.descent - 0.01f);
        pt_check(runs.line_height > runs.items[0].font->line_height / runs.scale - 0.01f);
    }
    pt_text_runs_free(&runs);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 160, 1.0f);
    pt_check(canvas != NULL && pt_window_paint(window, canvas));
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

static uint64_t pixels_hash(const pt_canvas *canvas) {
    uint64_t hash = 1469598103934665603ull;
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) {
        hash ^= canvas->pixels[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static pt_window *build_gallery(pt_app *app) {
    pt_window *window = pt_window_create(app, "snapshots", 720, 560);
    pt_check(window != NULL);
    if (!window) {
        return NULL;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 20.0f, 20.0f, 20.0f, 20.0f);
    pt_widget_set_gap(root, 14.0f);
    pt_widget *card = pt_card_create(root);
    pt_widget_set_padding(card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(card, 12.0f);
    pt_widget_set_align_self(card, pt_align_stretch);

    pt_widget *chips = pt_row_create(card);
    pt_widget_set_gap(chips, 8.0f);
    pt_widget *stable = pt_chip_create(chips, "Stable");
    pt_chip_set_icon(stable, pt_icon_check);
    pt_chip_set_selected(stable, true);
    pt_widget *removable = pt_chip_create(chips, "Removable");
    pt_chip_set_removable(removable, true);
    pt_widget *muted = pt_chip_create(chips, "Muted");
    pt_widget_set_enabled(muted, false);

    const char *segments[] = {"Day", "Week", "Month", "Year"};
    pt_widget *segmented = pt_segmented_create(card, segments, 4);
    pt_widget_set_align_self(segmented, pt_align_start);
    pt_segmented_set_selected(segmented, 2);
    pt_widget_set_width(segmented, 360.0f);

    pt_widget *skeleton_row = pt_row_create(card);
    pt_widget_set_gap(skeleton_row, 10.0f);
    pt_widget *avatar = pt_skeleton_create(skeleton_row);
    pt_widget_set_size(avatar, 36.0f, 36.0f);
    pt_widget *lines = pt_column_create(skeleton_row);
    pt_widget_set_gap(lines, 6.0f);
    pt_widget *line_one = pt_skeleton_create(lines);
    pt_widget_set_size(line_one, 220.0f, 12.0f);
    pt_widget *line_two = pt_skeleton_create(lines);
    pt_widget_set_size(line_two, 150.0f, 12.0f);

    pt_widget *busy_button = pt_button_create(card, "Sync now");
    pt_widget_set_align_self(busy_button, pt_align_start);
    pt_widget_set_busy(busy_button, true);
    pt_check(pt_widget_get_busy(busy_button));

    pt_widget *invalid = pt_entry_create(card, "name@example");
    pt_widget_set_size(invalid, 240.0f, 32.0f);
    pt_widget_set_align_self(invalid, pt_align_start);
    pt_widget_set_invalid(invalid, true);
    pt_check(pt_widget_get_invalid(invalid));

    pt_widget *progress = pt_progress_create(card);
    pt_widget_set_width(progress, 320.0f);
    pt_widget_set_align_self(progress, pt_align_start);
    pt_progress_set_value(progress, 0.62f);

    pt_widget *toggle_row = pt_row_create(card);
    pt_widget_set_gap(toggle_row, 16.0f);
    pt_checkbox_create(toggle_row, "Auto update");
    pt_radio_create(toggle_row, "Stable channel");

    pt_widget *label = pt_label_create(card, "Semantic tokens, state matrix and motion");
    pt_text_span accent = {0};
    accent.start = 0;
    accent.length = 8;
    accent.flags = pt_text_span_has_weight;
    accent.weight = pt_font_weight_semibold;
    pt_widget_set_text_spans(label, &accent, 1);
    pt_widget_set_align_self(label, pt_align_start);
    return window;
}

static void snapshot_paint(pt_window *window, pt_canvas *canvas, const char *directory,
                           const char *name, uint64_t *hash_out) {
    pt_check(pt_window_paint(window, canvas));
    *hash_out = pixels_hash(canvas);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", directory, name);
    pt_check(pt_canvas_write_png(canvas, path) == 0);
    FILE *file = fopen(path, "rb");
    pt_check(file != NULL);
    if (file) {
        pt_check(fseek(file, 0, SEEK_END) == 0);
        pt_check(ftell(file) > 64);
        fclose(file);
    }
}

static const char *snapshot_directory(void) {
    static char directory[512];
    const char *from_env = getenv("PT_SNAPSHOT_DIR");
    snprintf(directory, sizeof(directory), "%s",
             from_env && *from_env ? from_env : "build/snapshots");
    if (mkdir(directory, 0755) != 0) {
        pt_check(errno == EEXIST);
    }
    return directory;
}

static void test_snapshot_gallery(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = build_gallery(app);
    if (!window) {
        return;
    }
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(720, 560, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }

    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    uint64_t dark = 0;
    snapshot_paint(window, canvas, directory, "gallery-dark", &dark);
    uint64_t dark_again = 0;
    snapshot_paint(window, canvas, directory, "gallery-dark-repeat", &dark_again);
    pt_check(dark == dark_again);

    pt_app_set_color_scheme(app, pt_color_scheme_light);
    uint64_t light = 0;
    snapshot_paint(window, canvas, directory, "gallery-light", &light);
    pt_check(light != dark);

    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    uint64_t round_trip = 0;
    snapshot_paint(window, canvas, directory, "gallery-dark-roundtrip", &round_trip);
    pt_check(round_trip == dark);

    /* A window override reaches the framebuffer. */
    pt_window_set_theme_color(window, pt_theme_background, 0x102030FFu);
    pt_check(pt_window_paint(window, canvas));
    pt_color expected = 0xFF102030u;  /* canvas stores premultiplied ABGR */
    bool painted = false;
    size_t pixels = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < pixels; ++i) {
        if (canvas->pixels[i] == expected) {
            painted = true;
            break;
        }
    }
    pt_check(painted);
    pt_window_clear_theme_overrides(window);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* The 0.10 surfaces: a combo box with its popup and three data views carrying a
 * multi-row selection and a rubber band mid-drag. Each frame is hashed so a
 * regression in any of them shows up as a changed snapshot, and every state
 * transition is asserted to move pixels. */
static void test_snapshot_views(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "views", 720, 560);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 12.0f);

    pt_widget *combobox = pt_combobox_create(root);
    pt_widget_set_size(combobox, 220.0f, 34.0f);
    pt_widget_set_align_self(combobox, pt_align_start);
    pt_combobox_set_placeholder(combobox, "Colour");
    const char *colours[] = {"Red", "Green", "Blue"};
    for (size_t i = 0; i < 3; ++i) {
        pt_combobox_add_item(combobox, colours[i]);
    }
    pt_combobox_set_selected(combobox, 1);

    pt_widget *row = pt_row_create(root);
    pt_widget_set_gap(row, 12.0f);
    pt_widget_set_align_self(row, pt_align_stretch);

    pt_widget *list = pt_list_view_create(row);
    pt_widget_set_size(list, 200.0f, 300.0f);
    pt_list_view_set_row_height(list, 40.0f);
    pt_list_view_set_row_count(list, 8);

    pt_widget *table = pt_table_create(row);
    pt_widget_set_size(table, 240.0f, 300.0f);
    pt_table_set_column_count(table, 2);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Size");
    pt_table_set_column_width(table, 0, 130.0f);
    pt_table_set_column_width(table, 1, 90.0f);
    pt_table_set_row_height(table, 30.0f);
    pt_table_set_header_height(table, 30.0f);
    pt_table_set_row_count(table, 8);

    pt_widget *tree = pt_tree_create(row);
    pt_widget_set_size(tree, 220.0f, 300.0f);
    pt_tree_set_row_height(tree, 30.0f);
    pt_tree_node *folder = pt_tree_add_node(tree, NULL, "src");
    pt_tree_node *leaf = pt_tree_add_node(tree, folder, "main.c");
    (void)leaf;
    pt_tree_add_node(tree, folder, "util.c");
    pt_tree_add_node(tree, NULL, "README");
    pt_tree_node_set_expanded(folder, true);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(720, 560, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);

    uint64_t plain = 0;
    snapshot_paint(window, canvas, directory, "views-plain", &plain);

    pt_list_view_select_range(list, 1, 3);
    pt_table_select_range(table, 1, 3);
    pt_tree_select_range(tree, 1, 2);
    uint64_t selected = 0;
    snapshot_paint(window, canvas, directory, "views-selected", &selected);
    pt_check(selected != plain);
    uint64_t selected_again = 0;
    snapshot_paint(window, canvas, directory, "views-selected-repeat", &selected_again);
    pt_check(selected == selected_again);

    /* Drag a band across the list without releasing: the frame has to carry it,
     * because the band is the only feedback that a range is being chosen. */
    float list_x = 0.0f;
    float list_y = 0.0f;
    float list_w = 0.0f;
    float list_h = 0.0f;
    pt_widget_get_bounds(list, &list_x, &list_y, &list_w, &list_h);
    float start_x = list_x + list_w * 0.5f;
    float start_y = list_y + 20.0f;
    float end_y = list_y + 140.0f;
    pt_window_input_pointer_enter(window, start_x, start_y);
    pt_window_input_pointer_button(window, 0x110u, true, start_x, start_y, 5, 200);
    pt_window_input_pointer_motion(window, start_x, end_y, 220);
    uint64_t band = 0;
    snapshot_paint(window, canvas, directory, "views-marquee", &band);
    pt_check(band != selected);
    pt_window_input_pointer_button(window, 0x110u, false, start_x, end_y, 6, 240);
    pt_check(pt_list_view_get_selection_count(list) == 4);
    for (uint32_t index = 0; index < 4; ++index) {
        pt_check(pt_list_view_is_row_selected(list, index));
    }
    pt_check(!pt_list_view_is_row_selected(list, 4));

    pt_combobox_open(combobox);
    pt_check(pt_combobox_is_open(combobox));
    uint64_t popup = 0;
    snapshot_paint(window, canvas, directory, "views-combobox", &popup);
    pt_check(popup != band);
    pt_combobox_state *combo_state = combobox->extra;
    pt_check(combo_state && combo_state->popup && combo_state->popup->panel);
    if (combo_state && combo_state->popup) {
        pt_check(combo_state->popup->rect.x >= 0.0f &&
                 combo_state->popup->rect.x + combo_state->popup->rect.w <=
                     (float)window->width);
        pt_check(combo_state->popup->rect.y >= 0.0f &&
                 combo_state->popup->rect.y + combo_state->popup->rect.h <=
                     (float)window->height);
    }
    pt_combobox_close(combobox);

    uint64_t dark_final = 0;
    snapshot_paint(window, canvas, directory, "views-dark-final", &dark_final);
    pt_app_set_color_scheme(app, pt_color_scheme_light);
    uint64_t light = 0;
    snapshot_paint(window, canvas, directory, "views-light", &light);
    pt_check(light != dark_final);
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    uint64_t back_to_dark = 0;
    snapshot_paint(window, canvas, directory, "views-dark-roundtrip", &back_to_dark);
    pt_check(back_to_dark == dark_final);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Icon slot goldens: icon+text and icon-only buttons at 1x and 2x, with a tint
 * state that must move pixels. The icon is generated in memory so the golden
 * does not depend on installed themes. */
static pt_icon_image *snapshot_icon_image(void) {
    uint32_t pixels[16 * 16];
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint32_t alpha = (x < 2 || y < 2 || x > 13 || y > 13) ? 96u : 255u;
            uint32_t red = (uint32_t)(x * 16);
            uint32_t green = (uint32_t)(y * 16);
            uint32_t blue = (uint32_t)(255 - (x + y) * 8);
            pixels[(size_t)y * 16 + (size_t)x] =
                (alpha << 24) | ((red * alpha / 255u) << 16) |
                ((green * alpha / 255u) << 8) | (blue * alpha / 255u);
        }
    }
    size_t size = 0;
    uint8_t *png = pt_png_encode_argb_premultiplied(pixels, 16, 16, &size);
    pt_check(png != NULL && size > 0);
    pt_icon_image *image = png ? pt_icon_image_load_memory(png, size) : NULL;
    free(png);
    return image;
}

static void test_snapshot_icon(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "icon-snapshots", 360, 160);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 12.0f);
    pt_icon_image *image = snapshot_icon_image();
    pt_check(image != NULL);
    if (!image) {
        pt_window_free(window);
        return;
    }
    pt_widget *row = pt_row_create(root);
    pt_widget_set_gap(row, 12.0f);
    pt_widget *with_text = pt_button_create(row, "Export");
    pt_button_set_icon_image(with_text, image);
    pt_widget *icon_only = pt_button_create(row, "");
    pt_button_set_icon_image(icon_only, image);
    pt_button_set_icon_tint(icon_only, 0xFFB04AFFu);
    pt_widget *secondary = pt_button_create(row, "Share");
    pt_button_set_variant(secondary, pt_button_secondary);
    pt_button_set_icon_image(secondary, image);
    pt_icon_image_destroy(image);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(360, 160, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    uint64_t one_x = 0;
    snapshot_paint(window, canvas, directory, "icon-1x", &one_x);
    uint64_t one_x_repeat = 0;
    snapshot_paint(window, canvas, directory, "icon-1x-repeat", &one_x_repeat);
    pt_check(one_x == one_x_repeat);

    pt_button_set_icon_tint(icon_only, 0);
    uint64_t untinted = 0;
    snapshot_paint(window, canvas, directory, "icon-untinted", &untinted);
    pt_check(untinted != one_x);
    pt_button_set_icon_tint(icon_only, 0xFFB04AFFu);

    pt_canvas *hidpi = pt_canvas_create(720, 320, 2.0f);
    pt_check(hidpi != NULL);
    if (hidpi) {
        uint64_t two_x = 0;
        snapshot_paint(window, hidpi, directory, "icon-2x", &two_x);
        uint64_t two_x_repeat = 0;
        snapshot_paint(window, hidpi, directory, "icon-2x-repeat", &two_x_repeat);
        pt_check(two_x == two_x_repeat);
        pt_check(two_x != one_x);
        pt_canvas_destroy(hidpi);
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Single-line RTL goldens (2.0 wave 2): forced RTL controls, an auto-direction
 * Hebrew label under an RTL root, and a mixed Latin/Hebrew label. The same
 * widgets are repainted under an LTR root base so the changed golden proves the
 * reordering reaches pixels. Existing LTR snapshots must stay byte-identical. */
static void test_snapshot_rtl(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "rtl-snapshots", 520, 240);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 10.0f);

    pt_widget *forced = pt_label_create(root, "\xD7\x90\xD7\x91\xD7\x92 abc");
    pt_widget_set_text_direction(forced, pt_text_direction_rtl);
    pt_widget_set_align_self(forced, pt_align_start);

    pt_widget *automatic = pt_label_create(root, "\xD7\x90\xD7\x91\xD7\x92 abc");
    pt_widget_set_align_self(automatic, pt_align_start);

    pt_widget *mixed = pt_label_create(root, "abc \xD7\x90\xD7\x91\xD7\x92 xyz");
    pt_widget_set_align_self(mixed, pt_align_start);

    pt_widget *entry = pt_entry_create(root, "\xD7\x90\xD7\x91\xD7\x92 abc");
    pt_widget_set_text_direction(entry, pt_text_direction_rtl);
    pt_widget_set_size(entry, 260.0f, 32.0f);
    pt_widget_set_align_self(entry, pt_align_start);

    pt_widget *button = pt_button_create(root, "\xD7\x90\xD7\x91\xD7\x92 abc");
    pt_widget_set_text_direction(button, pt_text_direction_rtl);
    pt_widget_set_align_self(button, pt_align_start);

    pt_widget_set_text_direction(root, pt_text_direction_rtl);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 240, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    uint64_t rtl = 0;
    snapshot_paint(window, canvas, directory, "rtl-single-line", &rtl);
    uint64_t rtl_repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-single-line-repeat", &rtl_repeat);
    pt_check(rtl == rtl_repeat);

    /* Flip the inherited base: the auto Hebrew label must reorder. */
    pt_widget_set_text_direction(root, pt_text_direction_ltr);
    uint64_t ltr_base = 0;
    snapshot_paint(window, canvas, directory, "rtl-single-line-ltr-base", &ltr_base);
    pt_check(ltr_base != rtl);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* Wrapped RTL goldens (2.0 wave 3): a wrapped mixed-script label and a
 * multi-paragraph text area with align_end, so per-paragraph base resolution
 * and per-line reordering reach pixels. Existing LTR goldens stay unchanged. */
static void test_snapshot_rtl_wrapped(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "rtl-wrapped-snapshots", 520, 300);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 10.0f);

    pt_widget *wrapped = pt_label_create(
        root,
        "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 abc \xD7\x96\xD7\x97\xD7\x98 "
        "\xD7\x99\xD7\x9A\xD7\x9B xyz \xD7\x9C\xD7\x9D\xD7\x9E");
    pt_widget_set_label_wrap(wrapped, true);
    pt_widget_set_width(wrapped, 220.0f);
    pt_widget_set_text_direction(wrapped, pt_text_direction_rtl);
    pt_widget_set_align_self(wrapped, pt_align_start);

    pt_widget *area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 300.0f, 120.0f);
    pt_widget_set_text_direction(area, pt_text_direction_auto);
    pt_widget_set_text(area, "abc\n\xD7\x90\xD7\x91\xD7\x92");
    pt_widget_set_text_align(area, pt_text_align_end);
    pt_widget_set_align_self(area, pt_align_start);

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 300, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    uint64_t rtl_wrapped = 0;
    snapshot_paint(window, canvas, directory, "rtl-wrapped", &rtl_wrapped);
    uint64_t repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-wrapped-repeat", &repeat);
    pt_check(rtl_wrapped == repeat);
    /* Forcing an LTR base reorders both wrapped widgets, proving the golden
     * really exercises per-line bidi rather than a direction-neutral layout. */
    pt_widget_set_text_direction(wrapped, pt_text_direction_ltr);
    pt_widget_set_text_direction(area, pt_text_direction_ltr);
    uint64_t ltr_base = 0;
    snapshot_paint(window, canvas, directory, "rtl-wrapped-ltr-base", &ltr_base);
    pt_check(ltr_base != rtl_wrapped);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* RTL editing goldens (2.0 wave 4): a focused mixed-direction entry and a
 * wrapped RTL text area, each with the caret at a mapped visual position.
 * Repainting the same state hashes identically; forcing the opposite base
 * changes pixels, so the golden really carries bidi caret geometry. */
static void test_snapshot_rtl_editing(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "rtl-editing-snapshots", 520, 260);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 10.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);

    pt_widget *entry = pt_entry_create(root, "");
    pt_widget_set_text(entry, "abc \xD7\x90\xD7\x91\xD7\x92 123");
    pt_widget_set_size(entry, 440.0f, 34.0f);
    pt_widget_set_align_self(entry, pt_align_start);
    pt_widget *area = pt_text_area_create(root, "note");
    pt_widget_set_size(area, 440.0f, 150.0f);
    pt_widget_set_align_self(area, pt_align_start);
    pt_widget_set_text(area,
                       "\xD7\x90\xD7\x91\xD7\x92 \xD7\x93\xD7\x94\xD7\x95 \xD7\x96\xD7\x97\xD7\x98 "
                       "\xD7\x99\xD7\x9A\xD7\x9B abc \xD7\x9C\xD7\x9D\xD7\x9E \xD7\xA0\xD7\xA1\xD7\xA2 "
                       "\xD7\xA3\xD7\xA4\xD7\xA5");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 260, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);

    pt_check(pt_widget_focus(entry));
    pt_entry_set_selection(entry, 4, 4);
    window->caret_visible = true;
    uint64_t entry_hash = 0;
    snapshot_paint(window, canvas, directory, "rtl-editing-entry", &entry_hash);
    uint64_t entry_repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-editing-entry-repeat", &entry_repeat);
    pt_check(entry_hash == entry_repeat);

    /* The caret in the text area sits inside the level-2 Latin run of its
     * wrapped RTL line, a position only the map can place. */
    pt_check(pt_widget_focus(area));
    size_t latin = 0;
    const char *latin_at = strstr(pt_widget_get_text(area), "abc");
    pt_check(latin_at != NULL);
    if (latin_at) {
        latin = (size_t)(latin_at - pt_widget_get_text(area)) + 1;
    }
    pt_text_area_set_selection(area, latin, latin);
    window->caret_visible = true;
    uint64_t area_hash = 0;
    snapshot_paint(window, canvas, directory, "rtl-editing-area", &area_hash);
    uint64_t area_repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-editing-area-repeat", &area_repeat);
    pt_check(area_hash == area_repeat);

    pt_widget_set_text_direction(entry, pt_text_direction_ltr);
    pt_widget_set_text_direction(area, pt_text_direction_ltr);
    pt_check(pt_widget_focus(entry));
    pt_entry_set_selection(entry, 4, 4);
    window->caret_visible = true;
    uint64_t flipped = 0;
    snapshot_paint(window, canvas, directory, "rtl-editing-entry-ltr-base", &flipped);
    pt_check(flipped != entry_hash);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* RTL chrome goldens (2.0 wave 5b): the chrome painted around text must use
 * the same mirrored geometry as layout — scrollbar in the left gutter, sidebar
 * accent on the item's right edge, tree disclosure on the right, mirrored
 * tabs/table, and CSD buttons on the left. Repainting hashes identically and
 * flipping the root back to LTR changes pixels. */
static void test_snapshot_rtl_chrome(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "rtl-chrome", 760, 560);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(root, 12.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);

    pt_widget *row = pt_row_create(root);
    pt_widget_set_gap(row, 12.0f);
    pt_widget_set_align_self(row, pt_align_stretch);

    pt_widget *sidebar = pt_sidebar_create(row);
    pt_widget_set_size(sidebar, 180.0f, 240.0f);
    pt_sidebar_set_header(sidebar, "Workspace");
    pt_sidebar_add_item(sidebar, pt_icon_home, "Home");
    pt_sidebar_add_item(sidebar, pt_icon_document, "Documents");
    pt_sidebar_add_item(sidebar, pt_icon_settings, "Settings");
    pt_sidebar_add_item(sidebar, pt_icon_bell, "Notifications");
    pt_sidebar_add_item(sidebar, pt_icon_user, "Account");
    pt_sidebar_add_item(sidebar, pt_icon_info, "About");
    pt_sidebar_set_active(sidebar, 1);

    pt_widget *tree = pt_tree_create(row);
    pt_widget_set_size(tree, 200.0f, 240.0f);
    pt_tree_set_row_height(tree, 30.0f);
    pt_tree_node *folder = pt_tree_add_node(tree, NULL, "src");
    pt_tree_node_set_expanded(folder, true);
    pt_tree_add_node(tree, folder, "main.c");
    pt_tree_add_node(tree, folder, "util.c");
    pt_tree_add_node(tree, folder, "paint.c");
    pt_tree_add_node(tree, NULL, "README");
    pt_tree_add_node(tree, NULL, "docs");
    pt_tree_add_node(tree, NULL, "build");
    pt_tree_add_node(tree, NULL, "tests");
    pt_tree_add_node(tree, NULL, "tools");
    pt_tree_add_node(tree, NULL, "examples");
    pt_tree_add_node(tree, NULL, "LICENSE");
    pt_tree_add_node(tree, NULL, "Makefile");

    pt_widget *table = pt_table_create(row);
    pt_widget_set_size(table, 300.0f, 240.0f);
    pt_table_set_column_count(table, 3);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Modified");
    pt_table_set_column_title(table, 2, "Size");
    pt_table_set_column_width(table, 0, 120.0f);
    pt_table_set_column_width(table, 1, 120.0f);
    pt_table_set_column_width(table, 2, 90.0f);
    pt_table_set_row_height(table, 30.0f);
    pt_table_set_header_height(table, 30.0f);
    pt_table_set_row_count(table, 8);
    pt_table_select_range(table, 1, 2);

    pt_widget *tabs = pt_tabs_create(root);
    pt_widget_set_size(tabs, 500.0f, 130.0f);
    pt_tabs_add_page(tabs, "Overview");
    pt_tabs_add_page(tabs, "Activity");
    pt_tabs_add_page(tabs, "Settings");
    pt_tabs_set_active(tabs, 1);

    /* A scroll view whose content overflows paints its vertical bar in the
     * left gutter. */
    pt_widget *scroll = pt_scroll_create(root);
    pt_widget_set_size(scroll, 240.0f, 70.0f);
    for (int i = 0; i < 6; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Item %d", i);
        pt_widget *item = pt_label_create(scroll, label);
        pt_widget_set_height(item, 28.0f);
    }

    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(760, 560, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    uint64_t rtl = 0;
    snapshot_paint(window, canvas, directory, "rtl-chrome", &rtl);
    uint64_t rtl_repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-chrome-repeat", &rtl_repeat);
    pt_check(rtl == rtl_repeat);

    /* The scrollbar lives in the 10px left gutter, not the right one. */
    pt_rect inner = {scroll->x + scroll->padding_left, scroll->y + scroll->padding_top,
                     scroll->width - scroll->padding_left - scroll->padding_right,
                     scroll->height - scroll->padding_top - scroll->padding_bottom};
    pt_color bar = pt_app_get_theme_color(app, pt_theme_scrollbar);
    uint32_t bar_pixel = bar;
    uint32_t left_pixels = 0;
    uint32_t right_pixels = 0;
    for (int y = (int)inner.y + 6; y < (int)(inner.y + inner.h) - 6; ++y) {
        for (int x = (int)inner.x + 2; x < (int)inner.x + 8; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) == bar_pixel) {
                ++left_pixels;
            }
        }
        for (int x = (int)(inner.x + inner.w) - 8; x < (int)(inner.x + inner.w) - 2; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) == bar_pixel) {
                ++right_pixels;
            }
        }
    }
    pt_check(left_pixels > 20);
    pt_check(right_pixels == 0);

    /* The selected sidebar item carries its accent bar on the right edge. */
    pt_widget *active = pt_sidebar_get_item(sidebar, 1);
    pt_check(active != NULL);
    if (active) {
        pt_color accent = pt_app_get_theme_color(app, pt_theme_accent);
        uint32_t expected = accent;
        int bar_y = (int)(active->y + active->height * 0.5f);
        pt_check(pt_canvas_get_pixel(canvas, (int)(active->x + active->width - 2.0f),
                                     bar_y) == expected);
        pt_check(pt_canvas_get_pixel(canvas, (int)(active->x + 2.0f), bar_y) != expected);
    }

    pt_widget_set_ui_direction(root, pt_ui_direction_ltr);
    uint64_t ltr_base = 0;
    snapshot_paint(window, canvas, directory, "rtl-chrome-ltr-base", &ltr_base);
    pt_check(ltr_base != rtl);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    pt_canvas_destroy(canvas);
    pt_window_free(window);

    /* Client-side decorations: the button cluster mirrors to the left. */
    pt_window *deco_rtl = pt_window_create(app, "rtl-csd", 520, 260);
    pt_check(deco_rtl != NULL);
    if (deco_rtl) {
        pt_window_set_ui_direction(deco_rtl, pt_ui_direction_rtl);
        pt_label_create(pt_window_root(deco_rtl), "Decorations");
        pt_window_show(deco_rtl);
        pt_canvas *csd_canvas = pt_canvas_create(520, 260, 1.0f);
        pt_check(csd_canvas != NULL);
        if (csd_canvas) {
            uint64_t csd_rtl = 0;
            snapshot_paint(deco_rtl, csd_canvas, directory, "rtl-csd", &csd_rtl);
            uint64_t csd_repeat = 0;
            snapshot_paint(deco_rtl, csd_canvas, directory, "rtl-csd-repeat", &csd_repeat);
            pt_check(csd_rtl == csd_repeat);
            pt_window_set_ui_direction(deco_rtl, pt_ui_direction_ltr);
            uint64_t csd_ltr = 0;
            snapshot_paint(deco_rtl, csd_canvas, directory, "rtl-csd-ltr-base", &csd_ltr);
            pt_check(csd_ltr != csd_rtl);
            pt_canvas_destroy(csd_canvas);
        }
        pt_window_free(deco_rtl);
    }
}

/* CSD titles carry a bidi paragraph: the window has no widget direction, so
 * the title resolves its first strong character and falls back to the window's
 * UI direction. A mixed title must match a manual bidi draw and differ from
 * the legacy single-direction draw, which is what the old path used. */
static pt_rect probe_csd_minimize_rect(const pt_window *window) {
    bool rtl = pt_window_get_ui_direction(window) == pt_ui_direction_rtl;
    float y = (window->header_height - 30.0f) * 0.5f;
    if (y < 0.0f) {
        y = 0.0f;
    }
    float close_x = rtl ? 8.0f : (float)window->width - 8.0f - 36.0f;
    float maximize_x = close_x + (rtl ? 36.0f : -36.0f);
    return (pt_rect){maximize_x + (rtl ? 36.0f : -36.0f), y, 36.0f, 30.0f};
}

static pt_bidi_base probe_title_base(const pt_window *window, const char *title,
                                     size_t len) {
    pt_bidi_base strong = pt_bidi_first_strong_direction(title, len);
    if (strong != pt_bidi_base_auto) {
        return strong;
    }
    return pt_window_get_ui_direction(window) == pt_ui_direction_rtl ? pt_bidi_base_rtl
                                                                    : pt_bidi_base_ltr;
}

static uint64_t region_hash(const pt_canvas *canvas, int x0, int x1, int y0, int y1) {
    uint64_t hash = 1469598103934665603ull;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            hash ^= pt_canvas_get_pixel(canvas, x, y);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

static void test_csd_title_bidi(pt_app *app) {
    const char *directory = snapshot_directory();
    /* Hebrew first, so the paragraph base is RTL and the embedded Latin run
     * moves to the visual left; the legacy single-direction painter keeps the
     * Latin run on the right. */
    const char *title = "\xD7\x90\xD7\x91\xD7\x92 abc";
    pt_window *window = pt_window_create(app, title, 520, 260);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_window_set_ui_direction(window, pt_ui_direction_rtl);
    pt_label_create(pt_window_root(window), "Decorated");
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(520, 260, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    uint64_t rtl = 0;
    snapshot_paint(window, canvas, directory, "rtl-csd-title", &rtl);
    uint64_t repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-csd-title-repeat", &repeat);
    pt_check(rtl == repeat);

    /* Rebuild the title band the same way paint_csd does, once through the
     * bidi painter and once through the legacy painter. */
    float scale = window->render_scale > 0.0f ? window->render_scale : 1.0f;
    const char *family = window->app && window->app->theme.font_family[0]
                             ? window->app->theme.font_family
                             : "sans-serif";
    pt_font *font = pt_font_acquire(family, pt_font_weight_semibold,
                                    pt_font_slant_upright, 13.0f * scale);
    pt_check(font != NULL);
    if (!font) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        return;
    }
    pt_rect minimize = probe_csd_minimize_rect(window);
    float left = minimize.x + minimize.w + 16.0f;
    float title_width = pt_max_float(0.0f, (float)window->width - 8.0f - left);
    if (title_width < 20.0f) {
        title_width = (float)window->width - 16.0f;
        left = 8.0f;
    }
    float line_height = font->line_height / scale;
    float baseline = (window->header_height - line_height) * 0.5f +
                     font->ascent / scale;
    pt_color titlebar = pt_app_get_theme_color(app, pt_theme_titlebar);
    pt_color text = pt_app_get_theme_color(app, pt_theme_text);
    if (!window->keyboard_focused) {
        text = pt_color_alpha(text, 190);
    }
    pt_canvas *bidi_ref = pt_canvas_create(520, 260, 1.0f);
    pt_canvas *legacy_ref = pt_canvas_create(520, 260, 1.0f);
    pt_check(bidi_ref && legacy_ref);
    uint64_t painted_hash = 0;
    if (bidi_ref && legacy_ref) {
        pt_canvas_fill_rect(bidi_ref, 0.0f, 0.0f, 520.0f, window->header_height,
                            titlebar);
        pt_canvas_fill_rect(legacy_ref, 0.0f, 0.0f, 520.0f, window->header_height,
                            titlebar);
        pt_canvas_push_clip(bidi_ref, left, 0.0f, title_width,
                            window->header_height - 1.0f);
        pt_canvas_draw_text_line_bidi(bidi_ref, font, window->title,
                                      strlen(window->title), left, baseline,
                                      title_width, pt_text_align_center, text, true,
                                      probe_title_base(window, window->title,
                                                       strlen(window->title)));
        pt_canvas_pop_clip(bidi_ref);
        pt_canvas_push_clip(legacy_ref, left, 0.0f, title_width,
                            window->header_height - 1.0f);
        pt_canvas_draw_text_line(legacy_ref, font, window->title,
                                 strlen(window->title), left, baseline, title_width,
                                 pt_text_align_center, text, true);
        pt_canvas_pop_clip(legacy_ref);

        int band0 = (int)left;
        int band1 = (int)pt_min_float((float)window->width, left + title_width) - 1;
        pt_check(band1 >= band0);
        int band_y1 = (int)window->header_height - 2;
        painted_hash = region_hash(canvas, band0, band1, 0, band_y1);
        pt_check(painted_hash == region_hash(bidi_ref, band0, band1, 0, band_y1));
        pt_check(painted_hash != region_hash(legacy_ref, band0, band1, 0, band_y1));
        pt_canvas_destroy(bidi_ref);
        pt_canvas_destroy(legacy_ref);
    }

    /* A pure Hebrew title resolves an RTL base and still matches the bidi
     * painter; its golden repeats identically. */
    pt_window *hebrew = pt_window_create(app, "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
                                               "\xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D",
                                         480, 240);
    pt_check(hebrew != NULL);
    if (hebrew) {
        pt_window_set_ui_direction(hebrew, pt_ui_direction_rtl);
        pt_label_create(pt_window_root(hebrew), "Decorated");
        pt_window_show(hebrew);
        pt_canvas *he_canvas = pt_canvas_create(480, 240, 1.0f);
        pt_check(he_canvas != NULL);
        if (he_canvas) {
            uint64_t he_hash = 0;
            snapshot_paint(hebrew, he_canvas, directory, "rtl-csd-title-hebrew",
                           &he_hash);
            uint64_t he_repeat = 0;
            snapshot_paint(hebrew, he_canvas, directory,
                           "rtl-csd-title-hebrew-repeat", &he_repeat);
            pt_check(he_hash == he_repeat);
            pt_check(he_hash != 0);
            pt_canvas_destroy(he_canvas);
        }
        pt_window_free(hebrew);
    }

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

/* The combo box chevron mirrors to the leading (left) edge in RTL together
 * with its reserve; the value keeps the trailing padding. The field is empty
 * so the only chrome in the side strips is the chevron. */
static void test_combobox_rtl_chevron(pt_app *app) {
    const char *directory = snapshot_directory();
    pt_window *window = pt_window_create(app, "combo-rtl", 420, 200);
    pt_check(window != NULL);
    if (!window) {
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_ui_direction(root, pt_ui_direction_rtl);
    pt_widget *combobox = pt_combobox_create(root);
    pt_check(combobox != NULL);
    pt_widget_set_size(combobox, 220.0f, 34.0f);
    pt_widget_set_align_self(combobox, pt_align_start);
    pt_window_show(window);
    pt_canvas *canvas = pt_canvas_create(420, 200, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        pt_window_free(window);
        return;
    }
    pt_check(pt_window_paint(window, canvas));
    uint64_t rtl = 0;
    snapshot_paint(window, canvas, directory, "rtl-combobox-chevron", &rtl);
    uint64_t repeat = 0;
    snapshot_paint(window, canvas, directory, "rtl-combobox-chevron-repeat", &repeat);
    pt_check(rtl == repeat);

    pt_color background = pt_app_get_theme_color(app, pt_theme_control);
    int mid = (int)(combobox->y + combobox->height * 0.5f);
    int reserve = (int)pt_combobox_chevron_reserve;
    int left0 = (int)(combobox->x + combobox->padding_left);
    int right1 = (int)(combobox->x + combobox->width - combobox->padding_right);
    int left_ink = 0;
    int right_ink = 0;
    for (int y = mid - 8; y <= mid + 8; ++y) {
        for (int x = left0; x < left0 + reserve; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != background) {
                ++left_ink;
            }
        }
        for (int x = right1 - reserve; x < right1; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != background) {
                ++right_ink;
            }
        }
    }
    pt_check(left_ink > 0);
    pt_check(right_ink == 0);

    pt_widget_set_ui_direction(root, pt_ui_direction_ltr);
    pt_check(pt_window_paint(window, canvas));
    background = pt_app_get_theme_color(app, pt_theme_control);
    left0 = (int)(combobox->x + combobox->padding_left);
    right1 = (int)(combobox->x + combobox->width - combobox->padding_right);
    left_ink = 0;
    right_ink = 0;
    for (int y = mid - 8; y <= mid + 8; ++y) {
        for (int x = left0; x < left0 + reserve; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != background) {
                ++left_ink;
            }
        }
        for (int x = right1 - reserve; x < right1; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != background) {
                ++right_ink;
            }
        }
    }
    pt_check(left_ink == 0);
    pt_check(right_ink > 0);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.visuals");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("visuals");
    }
    test_chip_states(app);
    test_rich_text_runs(app);
    test_snapshot_gallery(app);
    test_snapshot_views(app);
    test_snapshot_icon(app);
    test_snapshot_rtl(app);
    test_snapshot_rtl_wrapped(app);
    test_snapshot_rtl_editing(app);
    test_snapshot_rtl_chrome(app);
    test_csd_title_bidi(app);
    test_combobox_rtl_chevron(app);
    pt_app_destroy(app);
    return pt_test_finish("visuals");
}
