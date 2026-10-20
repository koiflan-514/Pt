/*
 * Pt data views: stable keys, batch invalidation and multi-column sort.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_COUNT 18
#define ITEM_VISIBLE 12

typedef struct data_item {
    uint64_t key;
    const char *name;
    const char *kind;
    const char *owner;
    uint32_t size_kb;
} data_item;

static data_item items[ITEM_COUNT] = {
    {7101, "main.c", "C source", "ada", 18},
    {7107, "table.c", "C source", "ada", 42},
    {7113, "tree.c", "C source", "grace", 31},
    {7119, "navigation.c", "C source", "grace", 27},
    {7125, "canvas.c", "C source", "linus", 76},
    {7131, "font.c", "C source", "linus", 58},
    {7137, "layout.c", "C source", "ada", 64},
    {7143, "README.md", "Markdown", "grace", 11},
    {7149, "ROADMAP.md", "Markdown", "linus", 24},
    {7155, "bindings.md", "Markdown", "ada", 32},
    {7161, "api-policy.md", "Markdown", "grace", 14},
    {7167, "gallery.png", "Image", "linus", 486},
    {7173, "table.png", "Image", "ada", 312},
    {7179, "icons.svg", "Vector", "grace", 9},
    {7185, "logo.svg", "Vector", "linus", 6},
    {7191, "pt.h", "Header", "ada", 8},
    {7197, "widget.h", "Header", "grace", 21},
    {7203, "release.tar.gz", "Archive", "linus", 1024},
};

typedef struct data_state {
    pt_app *app;
    pt_window *window;
    pt_widget *table;
    pt_widget *status;
    pt_widget *owner_button;
    pt_widget *trim_button;
    uint32_t order[ITEM_COUNT];
    uint32_t sort_columns[PT_TABLE_MAX_SORT_COLUMNS];
    pt_sort_direction sort_directions[PT_TABLE_MAX_SORT_COLUMNS];
    uint32_t sort_count;
    uint32_t visible_count;
    bool owner_hidden;
    uint32_t edits;
} data_state;

static uint64_t row_key(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    data_state *state = data;
    if (row >= state->visible_count) {
        return pt_row_key_none;
    }
    return items[state->order[row]].key;
}

static const char *row_text(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    data_state *state = data;
    if (row >= state->visible_count) {
        return "";
    }
    return items[state->order[row]].name;
}

static int compare_items(uint32_t left, uint32_t right, const data_state *state) {
    const data_item *a = &items[left];
    const data_item *b = &items[right];
    for (uint32_t i = 0; i < state->sort_count; ++i) {
        int result = 0;
        switch (state->sort_columns[i]) {
        case 0:
            result = strcmp(a->name, b->name);
            break;
        case 1:
            result = strcmp(a->kind, b->kind);
            break;
        case 2:
            result = a->size_kb < b->size_kb ? -1 : a->size_kb > b->size_kb ? 1 : 0;
            break;
        case 3:
            result = strcmp(a->owner, b->owner);
            break;
        default:
            break;
        }
        if (result != 0) {
            return state->sort_directions[i] == pt_sort_descending ? -result : result;
        }
    }
    return 0;
}

static void sort_rows(data_state *state) {
    for (uint32_t i = 0; i < ITEM_COUNT; ++i) {
        state->order[i] = i;
    }
    if (state->sort_count == 0) {
        return;
    }
    for (uint32_t i = 1; i < ITEM_COUNT; ++i) {
        uint32_t key = state->order[i];
        uint32_t j = i;
        while (j > 0 && compare_items(state->order[j - 1], key, state) > 0) {
            state->order[j] = state->order[j - 1];
            --j;
        }
        state->order[j] = key;
    }
}

static const char *sort_name(const data_state *state) {
    if (state->sort_count == 0) {
        return "natural order";
    }
    if (state->sort_count >= 2 && state->sort_columns[0] == 1 &&
        state->sort_columns[1] == 0) {
        return "kind, then name";
    }
    switch (state->sort_columns[0]) {
    case 0:
        return state->sort_directions[0] == pt_sort_descending ? "name, descending" : "name";
    case 1:
        return "kind";
    case 2:
        return "size";
    default:
        return "owner";
    }
}

static void update_status(data_state *state, const char *prefix) {
    uint64_t key = pt_table_get_selected_key(state->table);
    char selection[48];
    if (key == pt_row_key_none) {
        snprintf(selection, sizeof(selection), "no keyed selection");
    } else {
        snprintf(selection, sizeof(selection), "selected key %" PRIu64, key);
    }
    char text[256];
    snprintf(text, sizeof(text), "%s · %s · %s · %u of %u rows%s", prefix, sort_name(state),
             selection, (unsigned)state->visible_count, (unsigned)ITEM_COUNT,
             state->owner_hidden ? " · owner hidden" : "");
    pt_widget_set_text(state->status, text);
}

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *data) {
    (void)table;
    data_state *state = data;
    if (row >= state->visible_count) {
        pt_widget_set_text(cell, "");
        return;
    }
    const data_item *item = &items[state->order[row]];
    char text[64];
    switch (column) {
    case 0:
        pt_widget_set_text(cell, item->name);
        pt_widget_set_font_weight(cell, pt_font_weight_medium);
        pt_widget_set_icon(cell, pt_icon_document);
        break;
    case 1:
        pt_widget_set_text(cell, item->kind);
        break;
    case 2:
        snprintf(text, sizeof(text), "%u KB", (unsigned)item->size_kb);
        pt_widget_set_text(cell, text);
        break;
    case 3:
        pt_widget_set_text(cell, item->owner);
        break;
    default:
        break;
    }
}

static void on_select(pt_widget *table, uint32_t row, void *data) {
    data_state *state = data;
    if (row >= state->visible_count) {
        update_status(state, "Selection cleared");
        return;
    }
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "Selected %s", items[state->order[row]].name);
    update_status(state, prefix);
    (void)table;
}

static void on_activate(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    data_state *state = data;
    if (row >= state->visible_count) {
        return;
    }
    char text[160];
    snprintf(text, sizeof(text), "Open %s", items[state->order[row]].name);
    pt_window_show_toast(state->window, text, pt_toast_info, 2200);
}

static void on_sort_columns(pt_widget *table, uint32_t count, const uint32_t *columns,
                            const pt_sort_direction *directions, void *data) {
    (void)table;
    data_state *state = data;
    state->sort_count = count > PT_TABLE_MAX_SORT_COLUMNS ? PT_TABLE_MAX_SORT_COLUMNS : count;
    for (uint32_t i = 0; i < state->sort_count; ++i) {
        state->sort_columns[i] = columns[i];
        state->sort_directions[i] = directions[i];
    }
    sort_rows(state);
    pt_table_refresh(state->table);
    update_status(state, "Sort changed");
}

static void on_sort_kind_name(pt_widget *button, void *data) {
    (void)button;
    data_state *state = data;
    uint32_t columns[2] = {1, 0};
    pt_sort_direction directions[2] = {pt_sort_ascending, pt_sort_ascending};
    state->sort_count = 2;
    memcpy(state->sort_columns, columns, sizeof(columns));
    memcpy(state->sort_directions, directions, sizeof(directions));
    pt_table_set_sort_columns(state->table, 2, columns, directions);
    sort_rows(state);
    pt_table_refresh(state->table);
    update_status(state, "Applied multi-column sort");
}

static void on_batch_edit(pt_widget *button, void *data) {
    (void)button;
    data_state *state = data;
    uint32_t edited = 0;
    for (uint32_t row = 2; row < 6 && row < state->visible_count; ++row) {
        items[state->order[row]].size_kb += 17;
        ++edited;
    }
    ++state->edits;
    pt_table_refresh_rows(state->table, 2, edited);
    char prefix[96];
    snprintf(prefix, sizeof(prefix), "Batch edit #%u touched %u rows", (unsigned)state->edits,
             (unsigned)edited);
    update_status(state, prefix);
}

static void on_toggle_owner(pt_widget *button, void *data) {
    (void)button;
    data_state *state = data;
    state->owner_hidden = !state->owner_hidden;
    pt_table_set_column_hidden(state->table, 3, state->owner_hidden);
    pt_widget_set_text(state->owner_button, state->owner_hidden ? "Show owner" : "Hide owner");
    update_status(state, state->owner_hidden ? "Owner column hidden" : "Owner column shown");
}

static void on_select_seventh(pt_widget *button, void *data) {
    (void)button;
    data_state *state = data;
    uint32_t row = state->visible_count > 7 ? 7 : 0;
    uint64_t key = items[state->order[row]].key;
    bool selected = pt_table_set_selected_key(state->table, key);
    char prefix[96];
    snprintf(prefix, sizeof(prefix), selected ? "Selected row 7 by key" : "Key not found");
    update_status(state, prefix);
}

static void on_clear_selection(pt_widget *button, void *data) {
    (void)button;
    data_state *state = data;
    pt_table_set_selected_key(state->table, pt_row_key_none);
    update_status(state, "Selection cleared by key");
}

static void on_trim_rows(pt_widget *button, void *data) {
    (void)button;
    data_state *state = data;
    state->visible_count = state->visible_count == ITEM_VISIBLE ? ITEM_COUNT : ITEM_VISIBLE;
    pt_table_set_row_count(state->table, state->visible_count);
    pt_widget_set_text(state->trim_button,
                       state->visible_count == ITEM_VISIBLE ? "Show all rows" : "Trim to 12 rows");
    update_status(state, "Row count changed");
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.data");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Pt data views", 920, 640);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 26.0f, 26.0f, 26.0f, 26.0f);
    pt_widget_set_gap(root, 14.0f);

    pt_widget *eyebrow = pt_label_create(root, "DATA VIEWS  ·  0.7");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x4C8DFF));

    pt_widget *heading = pt_label_create(root, "Stable keys, cheap updates");
    pt_widget_set_font_size(heading, 30.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "Selection follows an application key instead of a row index. "
              "Batch edits rebind only the affected rows, and header clicks report "
              "the full multi-column sort state.");
    pt_widget_set_label_wrap(intro, true);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA4B2));

    pt_widget *table_card = pt_card_create(root);
    pt_widget_set_grow(table_card, 1.0f);
    pt_widget_set_padding(table_card, 16.0f, 16.0f, 16.0f, 16.0f);
    pt_widget_set_gap(table_card, 12.0f);

    data_state state = {
        .app = app,
        .window = window,
        .visible_count = ITEM_COUNT,
        .sort_count = 0,
    };

    pt_widget *table = pt_table_create(table_card);
    state.table = table;
    pt_widget_set_grow(table, 1.0f);
    pt_widget_set_min_height(table, 260.0f);
    pt_widget_set_a11y_name(table, "Workspace files");
    pt_widget_set_a11y_description(
        table, "Multi-column sortable file table with keyed selection and batch refresh.");
    pt_table_set_column_count(table, 4);
    pt_table_set_column_title(table, 0, "Name");
    pt_table_set_column_title(table, 1, "Kind");
    pt_table_set_column_title(table, 2, "Size");
    pt_table_set_column_title(table, 3, "Owner");
    pt_table_set_column_width(table, 0, 250.0f);
    pt_table_set_column_width(table, 1, 150.0f);
    pt_table_set_column_width(table, 2, 100.0f);
    pt_table_set_column_width(table, 3, 130.0f);
    pt_table_set_column_align(table, 2, pt_text_align_end);
    pt_table_set_row_height(table, 34.0f);
    pt_table_set_header_height(table, 38.0f);
    pt_table_set_row_count(table, ITEM_COUNT);
    pt_table_set_on_bind_cell(table, bind_cell, &state);
    pt_table_set_on_select(table, on_select, &state);
    pt_table_set_on_activate(table, on_activate, &state);
    pt_table_set_on_sort_columns(table, on_sort_columns, &state);
    pt_table_set_row_key_callback(table, row_key, &state);
    pt_table_set_row_text_callback(table, row_text, &state);

    sort_rows(&state);
    pt_table_set_selected_key(table, items[state.order[3]].key);

    pt_widget *tools = pt_wrap_create(table_card);
    pt_wrap_set_gap(tools, 8.0f, 8.0f);
    pt_widget *sort_button = pt_button_create(tools, "Sort kind + name");
    pt_button_set_variant(sort_button, pt_button_soft);
    pt_button_set_on_click(sort_button, on_sort_kind_name, &state);
    pt_widget *edit_button = pt_button_create(tools, "Batch edit rows 2-5");
    pt_button_set_variant(edit_button, pt_button_secondary);
    pt_button_set_on_click(edit_button, on_batch_edit, &state);
    state.owner_button = pt_button_create(tools, "Hide owner");
    pt_button_set_variant(state.owner_button, pt_button_secondary);
    pt_button_set_on_click(state.owner_button, on_toggle_owner, &state);
    state.trim_button = pt_button_create(tools, "Trim to 12 rows");
    pt_button_set_variant(state.trim_button, pt_button_secondary);
    pt_button_set_on_click(state.trim_button, on_trim_rows, &state);
    pt_widget *select_button = pt_button_create(tools, "Select row 7 by key");
    pt_button_set_variant(select_button, pt_button_ghost);
    pt_button_set_on_click(select_button, on_select_seventh, &state);
    pt_widget *clear_button = pt_button_create(tools, "Clear selection");
    pt_button_set_variant(clear_button, pt_button_ghost);
    pt_button_set_on_click(clear_button, on_clear_selection, &state);

    state.status = pt_label_create(table_card, "");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x8B98AA));
    pt_widget_set_a11y_name(state.status, "Table status");
    pt_widget_set_a11y_role(state.status, pt_a11y_role_label);
    update_status(&state, "Ready");

    pt_widget *footer = pt_label_create(
        root, "Click a header to sort; Shift-click adds a secondary column. "
              "Type a name to jump to the next matching row. Enter activates.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_window_show(window);
    pt_widget_focus(table);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        on_sort_kind_name(NULL, &state);
        on_batch_edit(NULL, &state);
        on_toggle_owner(NULL, &state);
        on_select_seventh(NULL, &state);
        on_trim_rows(NULL, &state);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_app_destroy(app);
    return status;
}
