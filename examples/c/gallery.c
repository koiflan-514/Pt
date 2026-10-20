/*
 * Pt widget gallery: modern controls in one window.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <string.h>

typedef struct gallery_state {
    pt_app *app;
    pt_window *window;
    pt_menu *menu;
    pt_dialog *dialog;
    pt_widget *greeting;
    pt_widget *counter;
    pt_widget *slider_value;
    pt_widget *progress;
    pt_widget *toggle_status;
    pt_widget *entry;
    pt_widget *table;
    pt_widget *tree;
    pt_widget *sidebar;
    pt_widget *data_status;
    pt_widget *nav_status;
    uint32_t sort_column;
    pt_sort_direction sort_direction;
    int clicks;
} gallery_state;

typedef struct file_row {
    const char *name;
    const char *kind;
    const char *modified;
    uint32_t size_kb;
} file_row;

static const file_row files[] = {
    {"main.c", "C source", "Today 09:42", 18},
    {"table.c", "C source", "Today 09:12", 42},
    {"tree.c", "C source", "Yesterday", 31},
    {"navigation.c", "C source", "Yesterday", 27},
    {"canvas.c", "C source", "Oct 16", 76},
    {"font.c", "C source", "Oct 15", 58},
    {"layout.c", "C source", "Oct 15", 64},
    {"README.md", "Markdown", "Today 08:30", 11},
    {"ROADMAP.md", "Markdown", "Oct 14", 24},
    {"bindings.md", "Markdown", "Oct 13", 32},
    {"api-policy.md", "Markdown", "Today", 14},
    {"gallery.png", "Image", "Oct 12", 486},
    {"table.png", "Image", "Oct 12", 312},
    {"icons.svg", "Vector", "Oct 11", 9},
    {"logo.svg", "Vector", "Oct 10", 6},
    {"pt.h", "Header", "Today 09:40", 8},
    {"widget.h", "Header", "Yesterday", 21},
    {"interaction.h", "Header", "Oct 14", 12},
    {"theme.json", "Data", "Oct 09", 3},
    {"layout.json", "Data", "Oct 09", 4},
    {"release.tar.gz", "Archive", "Oct 08", 1024},
    {"fonts.tar.gz", "Archive", "Oct 08", 2048},
    {"session.log", "Log", "Today 10:02", 128},
    {"build.log", "Log", "Yesterday", 96},
};

#define FILE_COUNT (sizeof(files) / sizeof(files[0]))
static uint32_t file_order[FILE_COUNT];

static void update_counter(gallery_state *state) {
    char text[64];
    snprintf(text, sizeof(text), "%d interaction%s", state->clicks,
             state->clicks == 1 ? "" : "s");
    pt_widget_set_text(state->counter, text);
}

static void update_greeting(gallery_state *state) {
    char message[192];
    const char *name = pt_widget_get_text(state->entry);
    if (name && *name) {
        snprintf(message, sizeof(message), "Hello, %s", name);
    } else {
        snprintf(message, sizeof(message), "Type a name to say hello.");
    }
    pt_widget_set_text(state->greeting, message);
}

static void on_say_hello(pt_widget *button, void *data) {
    (void)button;
    gallery_state *state = data;
    ++state->clicks;
    update_greeting(state);
    update_counter(state);
}

static void on_reset(pt_widget *button, void *data) {
    (void)button;
    gallery_state *state = data;
    state->clicks = 0;
    pt_widget_set_text(state->entry, "");
    update_greeting(state);
    update_counter(state);
}

static void on_entry_text(pt_widget *entry, const char *text, void *data) {
    (void)entry;
    (void)text;
    update_greeting(data);
}

static void on_submit(pt_widget *entry, void *data) {
    (void)entry;
    gallery_state *state = data;
    ++state->clicks;
    char message[192];
    snprintf(message, sizeof(message), "Welcome aboard, %s!",
             pt_widget_get_text(state->entry));
    pt_widget_set_text(state->greeting, message);
    update_counter(state);
    pt_window_show_toast(state->window, "Greeting submitted", pt_toast_success, 2400);
}

static void on_open_menu(pt_widget *button, void *data) {
    gallery_state *state = data;
    pt_menu_show_below(state->menu, button);
}

static void on_open_dialog(pt_widget *button, void *data) {
    (void)button;
    gallery_state *state = data;
    pt_dialog_show(state->dialog);
}

static void on_dialog_save(pt_widget *button, void *data) {
    (void)button;
    gallery_state *state = data;
    ++state->clicks;
    pt_widget_set_text(state->greeting, "Workspace renamed.");
    update_counter(state);
    pt_window_show_toast(state->window, "Workspace renamed", pt_toast_success, 2400);
}

static void set_status(pt_widget *label, const char *text) {
    pt_widget_set_text(label, text);
}

static void on_slider(pt_widget *slider, float value, void *data) {
    (void)slider;
    gallery_state *state = data;
    char text[32];
    snprintf(text, sizeof(text), "%d%%", (int)(value * 100.0f + 0.5f));
    pt_widget_set_text(state->slider_value, text);
    pt_progress_set_value(state->progress, value);
}

static void on_toggle(pt_widget *toggle, float value, void *data) {
    (void)toggle;
    gallery_state *state = data;
    set_status(state->toggle_status, value > 0.5f ? "On" : "Off");
}

static void on_theme(pt_widget *segmented, uint32_t index, void *data) {
    (void)segmented;
    gallery_state *state = data;
    pt_app_set_color_scheme(state->app,
                            index == 1 ? pt_color_scheme_light : pt_color_scheme_dark);
}

static void on_quit(pt_widget *button, void *data) {
    (void)button;
    gallery_state *state = data;
    pt_window_hide(state->window);
    pt_app_quit(state->app);
}

static void format_size(uint32_t size_kb, char *buffer, size_t capacity) {
    if (size_kb >= 1024) {
        snprintf(buffer, capacity, "%.1f MB", (double)size_kb / 1024.0);
    } else {
        snprintf(buffer, capacity, "%u KB", size_kb);
    }
}

static int compare_files(uint32_t left, uint32_t right, uint32_t column) {
    const file_row *a = &files[left];
    const file_row *b = &files[right];
    switch (column) {
    case 1:
        return strcmp(a->kind, b->kind);
    case 2:
        return (a->size_kb > b->size_kb) - (a->size_kb < b->size_kb);
    case 3:
        return strcmp(a->modified, b->modified);
    default:
        return strcmp(a->name, b->name);
    }
}

static void sort_file_rows(gallery_state *state) {
    for (uint32_t i = 0; i < FILE_COUNT; ++i) {
        file_order[i] = i;
    }
    if (state->sort_direction == pt_sort_none) {
        return;
    }
    int direction = state->sort_direction == pt_sort_descending ? -1 : 1;
    for (uint32_t i = 1; i < FILE_COUNT; ++i) {
        uint32_t key = file_order[i];
        uint32_t j = i;
        while (j > 0 &&
               compare_files(file_order[j - 1], key, state->sort_column) * direction > 0) {
            file_order[j] = file_order[j - 1];
            --j;
        }
        file_order[j] = key;
    }
}

static void bind_file_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                           void *data) {
    (void)table;
    (void)data;
    if (row >= FILE_COUNT) {
        return;
    }
    const file_row *file = &files[file_order[row]];
    char text[64];
    switch (column) {
    case 0:
        pt_widget_set_text(cell, file->name);
        pt_widget_set_font_weight(cell, pt_font_weight_medium);
        pt_widget_set_icon(cell, pt_icon_document);
        break;
    case 1:
        pt_widget_set_text(cell, file->kind);
        break;
    case 2:
        format_size(file->size_kb, text, sizeof(text));
        pt_widget_set_text(cell, text);
        break;
    case 3:
        pt_widget_set_text(cell, file->modified);
        break;
    default:
        break;
    }
}

static void on_file_select(pt_widget *table, uint32_t row, void *data) {
    gallery_state *state = data;
    if (row >= FILE_COUNT) {
        pt_widget_set_text(state->data_status, "No file selected");
        return;
    }
    const file_row *file = &files[file_order[row]];
    char text[192];
    char size[32];
    format_size(file->size_kb, size, sizeof(size));
    snprintf(text, sizeof(text), "%s · %s · %s · selected row %u", file->name, file->kind,
             size, row);
    pt_widget_set_text(state->data_status, text);
    (void)table;
}

static void on_file_activate(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    gallery_state *state = data;
    if (row >= FILE_COUNT) {
        return;
    }
    char text[128];
    snprintf(text, sizeof(text), "Opened %s", files[file_order[row]].name);
    pt_window_show_toast(state->window, text, pt_toast_info, 2200);
}

static void on_file_sort(pt_widget *table, uint32_t column, pt_sort_direction direction,
                         void *data) {
    gallery_state *state = data;
    state->sort_column = column;
    state->sort_direction = direction;
    sort_file_rows(state);
    pt_table_refresh(table);
    static const char *const names[] = {"Name", "Kind", "Size", "Modified"};
    const char *label = column < 4 ? names[column] : "?";
    const char *mode = direction == pt_sort_ascending    ? "ascending"
                       : direction == pt_sort_descending ? "descending"
                                                         : "unsorted";
    char text[128];
    snprintf(text, sizeof(text), "%u files · sorted by %s (%s)", (unsigned)FILE_COUNT, label,
             mode);
    pt_widget_set_text(state->data_status, text);
}

static void on_cell_edited(pt_widget *table, uint32_t row, uint32_t column, const char *text,
                           void *data) {
    (void)table;
    gallery_state *state = data;
    char message[192];
    snprintf(message, sizeof(message), "Committed “%s” to row %u, column %u", text, row,
             column);
    pt_widget_set_text(state->data_status, message);
}

static void on_columns_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    (void)table;
    gallery_state *state = data;
    char message[96];
    snprintf(message, sizeof(message), "Column %u moved to position %u", from, to);
    pt_widget_set_text(state->data_status, message);
}

static void on_row_drag(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    gallery_state *state = data;
    char message[96];
    snprintf(message, sizeof(message), "Dragging row %u", row);
    pt_widget_set_text(state->data_status, message);
}

static void on_tree_select(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    gallery_state *state = data;
    char text[160];
    snprintf(text, sizeof(text), "Selected “%s” at depth %u", pt_tree_node_get_text(node),
             pt_tree_node_get_depth(node));
    pt_widget_set_text(state->data_status, text);
}

static void on_tree_rename(pt_widget *tree, pt_tree_node *node, const char *text, void *data) {
    (void)tree;
    (void)node;
    gallery_state *state = data;
    char message[192];
    snprintf(message, sizeof(message), "Renamed node to “%s”", text);
    pt_widget_set_text(state->data_status, message);
}

static void on_chip_click(pt_widget *chip, void *data) {
    (void)data;
    pt_chip_set_selected(chip, !pt_chip_get_selected(chip));
}

static void on_chip_remove(pt_widget *chip, void *data) {
    gallery_state *state = data;
    pt_widget_set_visible(chip, false);
    pt_window_show_toast(state->window, "Filter cleared", pt_toast_info, 1800);
}

static void on_sidebar_select(pt_widget *sidebar, uint32_t index, void *data) {
    (void)sidebar;
    gallery_state *state = data;
    static const char *const views[] = {"Files", "Recent", "Shared", "Settings"};
    const char *name = index < 4 ? views[index] : "Unknown";
    char text[96];
    snprintf(text, sizeof(text), "Sidebar: %s selected", name);
    pt_widget_set_text(state->nav_status, text);
}

static void on_compact_sidebar(pt_widget *toggle, float value, void *data) {
    (void)toggle;
    gallery_state *state = data;
    pt_sidebar_set_compact(state->sidebar, value > 0.5f);
}

static pt_widget *muted_label(pt_widget *parent, const char *text) {
    pt_widget *label = pt_label_create(parent, text);
    pt_widget_set_font_size(label, 13.0f);
    return label;
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.gallery");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt Gallery", 960, 860);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }
    pt_window_set_min_size(window, 780, 620);
    /* Window-scoped token override: rounder cards without changing the app theme. */
    pt_window_set_theme_metric(window, pt_theme_metric_radius_medium, 12.0f);

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 16.0f);

    gallery_state state = {.app = app, .window = window};

    state.menu = pt_menu_create(window);
    pt_menu_add_item(state.menu, "Say hello", NULL, on_say_hello, &state);
    pt_menu_add_item(state.menu, "Reset form", NULL, on_reset, &state);
    pt_menu_add_separator(state.menu);
    pt_menu_add_item(state.menu, "Rename workspace…", NULL, on_open_dialog, &state);
    pt_menu_add_separator(state.menu);
    pt_menu_add_item(state.menu, "Quit", "Ctrl+Q", on_quit, &state);

    state.dialog = pt_dialog_create(
        window, "Rename workspace",
        "Dialogs dim the window, trap keyboard focus and close before an action "
        "callback runs.");
    pt_dialog_add_action(state.dialog, "Cancel", false, NULL, NULL);
    pt_dialog_add_action(state.dialog, "Rename", true, on_dialog_save, &state);

    /* Header */
    pt_widget *header = pt_row_create(root);
    pt_widget_set_gap(header, 16.0f);
    pt_widget *headings = pt_column_create(header);
    pt_widget_set_grow(headings, 1.0f);
    pt_widget_set_gap(headings, 3.0f);
    pt_widget *title = pt_label_create(headings, "Pt Gallery");
    pt_widget_set_font_size(title, 28.0f);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);
    muted_label(headings, "Modern controls, overlays and a Wayland-native toolkit.");
    const char *themes[] = {"Dark", "Light"};
    pt_widget *theme = pt_segmented_create(header, themes, 2);
    pt_widget_set_size(theme, 168.0f, 34.0f);
    pt_widget_set_align_self(theme, pt_align_center);
    pt_segmented_set_on_change(theme, on_theme, &state);
    pt_widget_set_tooltip(theme, "Switch between the dark and light design tokens");
    pt_widget *actions = pt_button_create(header, "Actions");
    pt_button_set_variant(actions, pt_button_secondary);
    pt_widget_set_align_self(actions, pt_align_center);
    pt_button_set_on_click(actions, on_open_menu, &state);
    pt_widget_set_tooltip(actions, "Open the in-window popup menu");
    pt_widget_set_a11y_description(actions, "Opens the gallery actions menu");
    pt_widget *quit = pt_button_create(header, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_widget_set_align_self(quit, pt_align_center);
    pt_button_set_on_click(quit, on_quit, &state);
    pt_widget_set_tooltip(quit, "Close the gallery");

    /* Tabbed shell */
    pt_widget *tabs = pt_tabs_create(root);
    pt_widget_set_grow(tabs, 1.0f);
    pt_tabs_set_strip_height(tabs, 46.0f);
    pt_widget *controls_page = pt_tabs_add_page(tabs, "Controls");
    pt_widget_set_padding(controls_page, 14.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(controls_page, 16.0f);

    /* Two feature cards */
    pt_widget *cards = pt_row_create(controls_page);
    pt_widget_set_gap(cards, 16.0f);

    pt_widget *form_card = pt_card_create(cards);
    pt_widget_set_grow(form_card, 1.0f);
    pt_widget_set_padding(form_card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(form_card, 9.0f);
    pt_widget *form_title = pt_label_create(form_card, "Forms & input");
    pt_widget_set_font_size(form_title, 17.0f);
    pt_widget_set_font_weight(form_title, pt_font_weight_semibold);
    muted_label(form_card, "Inline validation, selection and keyboard editing.");
    state.entry = pt_entry_create(form_card, "Your name");
    pt_entry_set_on_change(state.entry, on_entry_text, &state);
    pt_entry_set_on_submit(state.entry, on_submit, &state);
    pt_widget_set_a11y_description(state.entry, "Name used to build the greeting");
    pt_widget *email = pt_entry_create(form_card, "you@example.com");
    pt_widget_set_input_purpose(email, pt_input_purpose_email);
    pt_widget_set_a11y_name(email, "Email address");
    pt_widget_set_invalid(email, true);
    pt_widget_set_tooltip(email, "Inline validation uses the invalid state token");
    muted_label(form_card, "The email field asks the input method for an email layout.");
    state.greeting = pt_label_create(form_card, "Type a name to say hello.");
    state.counter = muted_label(form_card, "0 interactions");

    pt_widget *form_actions = pt_row_create(form_card);
    pt_widget_set_gap(form_actions, 8.0f);
    pt_widget *say = pt_button_create(form_actions, "Say hello");
    pt_button_set_on_click(say, on_say_hello, &state);
    pt_widget_set_tooltip(say, "Update the greeting and the interaction counter");
    pt_widget *reset = pt_button_create(form_actions, "Reset");
    pt_button_set_variant(reset, pt_button_soft);
    pt_button_set_on_click(reset, on_reset, &state);
    pt_widget *rename = pt_button_create(form_actions, "Rename…");
    pt_button_set_variant(rename, pt_button_ghost);
    pt_button_set_on_click(rename, on_open_dialog, &state);
    pt_widget_set_tooltip(rename, "Open the modal rename dialog");

    pt_divider_create(form_card);
    pt_widget *subscribe = pt_checkbox_create(form_card, "Subscribe to release notes");
    pt_widget_set_checked(subscribe, true);
    pt_checkbox_create(form_card, "Enable beta channel");

    muted_label(form_card, "Row density");
    pt_widget *radios = pt_row_create(form_card);
    pt_widget_set_gap(radios, 18.0f);
    pt_widget *comfort = pt_radio_create(radios, "Comfort");
    pt_radio_create(radios, "Compact");
    pt_radio_create(radios, "Dense");
    pt_widget_set_checked(comfort, true);

    pt_widget *live_card = pt_card_create(cards);
    pt_widget_set_grow(live_card, 1.0f);
    pt_widget_set_padding(live_card, 18.0f, 18.0f, 18.0f, 18.0f);
    pt_widget_set_gap(live_card, 9.0f);
    pt_widget *live_title = pt_label_create(live_card, "Live controls");
    pt_widget_set_font_size(live_title, 17.0f);
    pt_widget_set_font_weight(live_title, pt_font_weight_semibold);
    muted_label(live_card, "Animated states with tonal surfaces and soft accents.");
    pt_widget *styled = pt_label_create(live_card, "Weight, slant, color, underline");
    pt_widget_set_label_wrap(styled, true);
    pt_text_span spans[] = {
        {.start = 0, .length = 6, .flags = pt_text_span_has_weight, .weight = pt_font_weight_bold},
        {.start = 8, .length = 5, .flags = pt_text_span_has_slant, .slant = pt_font_slant_italic},
        {.start = 15,
         .length = 5,
         .flags = pt_text_span_has_color,
         .color = pt_rgb(0x9ECE6A)},
        {.start = 22, .length = 9, .flags = pt_text_span_underline},
    };
    pt_widget_set_text_spans(styled, spans, sizeof(spans) / sizeof(spans[0]));

    pt_widget *toggle_row = pt_row_create(live_card);
    pt_widget_set_gap(toggle_row, 10.0f);
    pt_widget *toggle = pt_toggle_create(toggle_row);
    pt_widget_set_align_self(toggle, pt_align_center);
    pt_toggle_set_checked(toggle, true);
    pt_toggle_set_on_change(toggle, on_toggle, &state);
    pt_widget *toggle_label = pt_label_create(toggle_row, "Live preview");
    pt_widget_set_align_self(toggle_label, pt_align_center);
    state.toggle_status = pt_label_create(toggle_row, "On");
    pt_widget_set_align_self(state.toggle_status, pt_align_center);

    pt_widget *slider_row = pt_row_create(live_card);
    pt_widget_set_gap(slider_row, 12.0f);
    pt_widget *slider = pt_slider_create(slider_row);
    pt_widget_set_grow(slider, 1.0f);
    pt_widget_set_align_self(slider, pt_align_center);
    pt_slider_set_value(slider, 0.62f);
    pt_slider_set_on_change(slider, on_slider, &state);
    state.slider_value = pt_label_create(slider_row, "62%");
    pt_widget_set_font_weight(state.slider_value, pt_font_weight_medium);
    pt_widget_set_align_self(state.slider_value, pt_align_center);

    pt_widget *progress_row = pt_row_create(live_card);
    pt_widget_set_gap(progress_row, 8.0f);
    pt_widget *progress_label = pt_label_create(progress_row, "Upload progress");
    pt_widget_set_grow(progress_label, 1.0f);
    state.progress = pt_progress_create(progress_row);
    pt_widget_set_width(state.progress, 110.0f);
    pt_widget_set_align_self(state.progress, pt_align_center);
    pt_progress_set_value(state.progress, 0.62f);

    pt_widget *sync_row = pt_row_create(live_card);
    pt_widget_set_gap(sync_row, 10.0f);
    pt_widget *spinner = pt_spinner_create(sync_row);
    pt_widget_set_align_self(spinner, pt_align_center);
    pt_widget *sync_label = pt_label_create(sync_row, "Syncing workspace");
    pt_widget_set_align_self(sync_label, pt_align_center);
    pt_widget *indeterminate = pt_progress_create(sync_row);
    pt_widget_set_grow(indeterminate, 1.0f);
    pt_widget_set_height(indeterminate, 6.0f);
    pt_widget_set_align_self(indeterminate, pt_align_center);
    pt_progress_set_indeterminate(indeterminate, true);

    const char *densities[] = {"Comfort", "Cozy", "Compact"};
    pt_widget *density = pt_segmented_create(live_card, densities, 3);
    pt_segmented_set_selected(density, 1);

    pt_widget *variants = pt_row_create(live_card);
    pt_widget_set_gap(variants, 8.0f);
    pt_widget *soft = pt_button_create(variants, "Soft");
    pt_button_set_variant(soft, pt_button_soft);
    pt_widget *secondary = pt_button_create(variants, "Secondary");
    pt_button_set_variant(secondary, pt_button_secondary);
    pt_widget *ghost = pt_button_create(variants, "Ghost");
    pt_button_set_variant(ghost, pt_button_ghost);
    pt_widget *disabled = pt_button_create(variants, "Disabled");
    pt_widget_set_enabled(disabled, false);

    pt_widget *chips = pt_row_create(live_card);
    pt_widget_set_gap(chips, 8.0f);
    pt_widget *chip_source = pt_chip_create(chips, "Source");
    pt_chip_set_icon(chip_source, pt_icon_document);
    pt_chip_set_removable(chip_source, true);
    pt_chip_set_on_remove(chip_source, on_chip_remove, &state);
    pt_widget *chip_filter = pt_chip_create(chips, "Filter active");
    pt_chip_set_selected(chip_filter, true);
    pt_chip_set_on_click(chip_filter, on_chip_click, &state);
    pt_widget_set_tooltip(chip_filter, "Click to toggle the selected chip state");
    pt_widget *skeleton = pt_skeleton_create(live_card);
    pt_widget_set_height(skeleton, 12.0f);
    pt_widget_set_tooltip(skeleton, "Skeleton placeholder while content loads");

    /* Component index */
    pt_widget *list_title = pt_label_create(controls_page, "Component index");
    pt_widget_set_font_size(list_title, 17.0f);
    pt_widget_set_font_weight(list_title, pt_font_weight_semibold);
    pt_widget *scroll = pt_scroll_create(controls_page);
    pt_widget_set_height(scroll, 160.0f);
    pt_widget *list = pt_column_create(scroll);
    pt_widget_set_gap(list, 0.0f);
    static const char *items[] = {
        "Checkbox, radio and segmented controls",
        "Determinate and indeterminate progress",
        "Animated spinner with tonal track",
        "Text selection, cut/copy/paste and undo",
        "Double-click word selection and triple-click select-all",
        "Public clipboard access with pt_window_clipboard_*",
        "Scroll offsets through pt_scroll_get_offset",
        "Focus rings, soft buttons and modern design tokens",
        "Fractional scaling through wp_viewporter",
        "HarfBuzz shaping and FreeType rasterising",
        "UTF-8 editing with IME preedit support",
        "Shared-memory buffering with headless tests",
        "Popover menus with focus scopes and shortcuts",
        "Modal dialogs, stacking toasts and delayed tooltips",
        "Accessibility roles, names, states and change events",
        "Damage-tracked partial repaint with a text shape cache",
        "Touch input mapped onto pointer semantics",
        "External drag and drop with MIME text targets",
        "Entry drag sources and cross-window text drops",
        "Compositor swipe, pinch and hold gestures",
        "Long press synthesis and kinetic scrolling",
        "Virtualized list rows with recycling and bind callbacks",
        "Multiline text areas with wrapping and grow-to-content",
        "Responsive wrap and grid containers",
        "Public easing curves, transitions and reduced motion",
        "List and text-frame accessibility roles",
        "Virtualized tables with sortable, resizable columns",
        "Tree views with expansion, hierarchy and selection",
        "Tabs with overflow and an animated indicator",
        "Sidebars with icon rows and compact mode",
        "Built-in vector icons and accessible icon buttons",
        "text/uri-list drag sources and drag images",
        "Multi-contact pinch/rotate and stylus pressure",
        "Semantic tokens with window-scoped overrides",
        "Chips, skeletons and busy/invalid surface states",
        "Inline table cell editing with undo-aware entries",
        "Tree renaming with F2 and focus-loss commit",
        "Shift-select, marquee selection and row dragging",
        "Reorderable table columns with model callbacks",
        "Per-run font family and size in styled text",
    };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        pt_widget *item = pt_label_create(list, items[i]);
        pt_widget_set_height(item, 28.0f);
        pt_divider_create(list);
    }

    /* Data view: virtualized table and tree */
    pt_widget *data_page = pt_tabs_add_page(tabs, "Data");
    pt_widget_set_padding(data_page, 14.0f, 0.0f, 0.0f, 0.0f);
    pt_widget_set_gap(data_page, 10.0f);
    pt_widget *data_row = pt_row_create(data_page);
    pt_widget_set_grow(data_row, 1.0f);
    pt_widget_set_gap(data_row, 16.0f);

    pt_widget *table_card = pt_card_create(data_row);
    pt_widget_set_grow(table_card, 2.0f);
    pt_widget_set_padding(table_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(table_card, 8.0f);
    pt_widget *table_title = pt_label_create(table_card, "Table");
    pt_widget_set_font_size(table_title, 17.0f);
    pt_widget_set_font_weight(table_title, pt_font_weight_semibold);
    muted_label(table_card,
                "24 files · sort, resize, reorder, drag rows and double-click to edit.");
    state.table = pt_table_create(table_card);
    pt_widget_set_grow(state.table, 1.0f);
    pt_table_set_column_count(state.table, 4);
    static const char *column_titles[] = {"Name", "Kind", "Size", "Modified"};
    static const float column_widths[] = {214.0f, 116.0f, 88.0f, 138.0f};
    for (uint32_t column = 0; column < 4; ++column) {
        pt_table_set_column_title(state.table, column, column_titles[column]);
        pt_table_set_column_width(state.table, column, column_widths[column]);
        pt_table_set_column_resizable(state.table, column, true);
    }
    pt_table_set_column_align(state.table, 2, pt_text_align_end);
    pt_table_set_row_height(state.table, 32.0f);
    pt_table_set_header_height(state.table, 38.0f);
    pt_table_set_on_bind_cell(state.table, bind_file_cell, NULL);
    pt_table_set_on_select(state.table, on_file_select, &state);
    pt_table_set_on_activate(state.table, on_file_activate, &state);
    pt_table_set_on_sort(state.table, on_file_sort, &state);
    pt_table_set_editable(state.table, true);
    pt_table_set_on_cell_edited(state.table, on_cell_edited, &state);
    pt_table_set_columns_reorderable(state.table, true);
    pt_table_set_on_columns_reordered(state.table, on_columns_reordered, &state);
    pt_table_set_on_row_drag(state.table, on_row_drag, &state);
    pt_table_set_row_count(state.table, FILE_COUNT);
    state.sort_column = 0;
    state.sort_direction = pt_sort_ascending;
    sort_file_rows(&state);
    pt_table_set_sort(state.table, 0, pt_sort_ascending);
    pt_widget_set_a11y_name(state.table, "Workspace files");

    pt_widget *tree_card = pt_card_create(data_row);
    pt_widget_set_grow(tree_card, 1.0f);
    pt_widget_set_padding(tree_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(tree_card, 8.0f);
    pt_widget *tree_title = pt_label_create(tree_card, "Tree");
    pt_widget_set_font_size(tree_title, 17.0f);
    pt_widget_set_font_weight(tree_title, pt_font_weight_semibold);
    muted_label(tree_card, "Expand branches; press F2 to rename the selected node.");
    state.tree = pt_tree_create(tree_card);
    pt_widget_set_grow(state.tree, 1.0f);
    pt_tree_set_row_height(state.tree, 30.0f);
    pt_tree_set_indent(state.tree, 16.0f);
    pt_tree_set_on_select(state.tree, on_tree_select, &state);
    pt_tree_set_renamable(state.tree, true);
    pt_tree_set_on_rename(state.tree, on_tree_rename, &state);
    pt_tree_node *workspace_node = pt_tree_add_node(state.tree, NULL, "workspace");
    pt_tree_node *src_node = pt_tree_add_node(state.tree, workspace_node, "src");
    pt_tree_node *table_node = pt_tree_add_node(state.tree, src_node, "table.c");
    pt_tree_add_node(state.tree, src_node, "tree.c");
    pt_tree_add_node(state.tree, src_node, "navigation.c");
    pt_tree_node *docs_node = pt_tree_add_node(state.tree, workspace_node, "docs");
    pt_tree_add_node(state.tree, docs_node, "quickstart.md");
    pt_tree_add_node(state.tree, docs_node, "api-policy.md");
    pt_tree_node *assets_node = pt_tree_add_node(state.tree, workspace_node, "assets");
    pt_tree_add_node(state.tree, assets_node, "icons.svg");
    pt_tree_add_node(state.tree, assets_node, "gallery.png");
    pt_tree_node_set_expanded(workspace_node, true);
    pt_tree_node_set_expanded(src_node, true);
    pt_tree_node_set_expanded(docs_node, true);
    pt_widget_set_a11y_name(state.tree, "Workspace tree");

    state.data_status = muted_label(data_page, "24 files · sorted by Name (ascending)");
    pt_table_set_selected(state.table, 0);
    pt_tree_set_selected(state.tree, table_node);

    /* Navigation shell: sidebar, icons and icon buttons */
    pt_widget *nav_page = pt_tabs_add_page(tabs, "Navigation");
    pt_widget_set_padding(nav_page, 14.0f, 0.0f, 0.0f, 0.0f);
    pt_widget *nav_row = pt_row_create(nav_page);
    pt_widget_set_gap(nav_row, 16.0f);
    state.sidebar = pt_sidebar_create(nav_row);
    pt_sidebar_set_header(state.sidebar, "Workspace");
    pt_sidebar_add_item(state.sidebar, pt_icon_home, "Files");
    pt_sidebar_add_item(state.sidebar, pt_icon_document, "Recent");
    pt_sidebar_add_item(state.sidebar, pt_icon_folder, "Shared");
    pt_sidebar_add_item(state.sidebar, pt_icon_settings, "Settings");
    pt_sidebar_set_active(state.sidebar, 0);
    pt_sidebar_set_on_select(state.sidebar, on_sidebar_select, &state);

    pt_widget *nav_content = pt_card_create(nav_row);
    pt_widget_set_grow(nav_content, 1.0f);
    pt_widget_set_padding(nav_content, 20.0f, 20.0f, 20.0f, 20.0f);
    pt_widget_set_gap(nav_content, 14.0f);
    pt_widget *nav_title = pt_label_create(nav_content, "Navigation shell");
    pt_widget_set_font_size(nav_title, 20.0f);
    pt_widget_set_font_weight(nav_title, pt_font_weight_semibold);
    muted_label(nav_content,
                "A sidebar with icons and labels, built-in glyphs, icon buttons "
                "and a compact mode that keeps accessible names.");

    pt_widget *glyph_row = pt_row_create(nav_content);
    pt_widget_set_gap(glyph_row, 12.0f);
    static const struct {
        pt_icon glyph;
        const char *name;
    } glyphs[] = {
        {pt_icon_chevron_down, "Chevron"}, {pt_icon_search, "Search"},
        {pt_icon_menu, "Menu"},            {pt_icon_close, "Close"},
        {pt_icon_check, "Check"},          {pt_icon_folder, "Folder"},
        {pt_icon_document, "Document"},    {pt_icon_settings, "Settings"},
        {pt_icon_bell, "Notifications"},   {pt_icon_refresh, "Refresh"},
    };
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        pt_widget *glyph = pt_icon_create(glyph_row, glyphs[i].glyph);
        pt_widget_set_tooltip(glyph, glyphs[i].name);
        pt_widget_set_a11y_name(glyph, glyphs[i].name);
    }

    pt_widget *icon_button_row = pt_row_create(nav_content);
    pt_widget_set_gap(icon_button_row, 8.0f);
    pt_widget *search_button =
        pt_icon_button_create(icon_button_row, pt_icon_search, "Search workspace");
    pt_icon_button_create(icon_button_row, pt_icon_plus, "New file");
    pt_icon_button_create(icon_button_row, pt_icon_refresh, "Refresh");
    pt_icon_button_create(icon_button_row, pt_icon_trash, "Move to trash");
    pt_widget_set_tooltip(search_button, "Search the workspace");

    pt_widget *compact_row = pt_row_create(nav_content);
    pt_widget_set_gap(compact_row, 10.0f);
    pt_widget *compact = pt_toggle_create(compact_row);
    pt_widget_set_align_self(compact, pt_align_center);
    pt_toggle_set_on_change(compact, on_compact_sidebar, &state);
    pt_widget *compact_label = pt_label_create(compact_row, "Compact sidebar");
    pt_widget_set_align_self(compact_label, pt_align_center);
    muted_label(nav_content, "Icons stay centered; labels are hidden but still exposed.");
    state.nav_status = muted_label(nav_content, "Sidebar: Files selected");

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_dialog_destroy(state.dialog);
    pt_menu_destroy(state.menu);
    pt_app_destroy(app);
    return status;
}
