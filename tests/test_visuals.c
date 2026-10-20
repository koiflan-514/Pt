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
    pt_app_destroy(app);
    return pt_test_finish("visuals");
}
