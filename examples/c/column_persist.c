/*
 * Pt column layout persistence: resize, reorder and hide table columns, then
 * save the display layout to a versioned blob, show it in a text area and
 * restore it.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Contract demonstrated here (include/pt/widget.h):
 *
 *  - pt_table_set_column_key() gives a column a stable identity. Keys move with
 *    columns and are matched by pt_table_columns_restore(), so a saved layout
 *    follows a column across a reorder. Empty keys match by position.
 *  - pt_table_columns_save() serializes the display order, each key, width and
 *    hidden flag plus the multi-column sort state. The caller owns the
 *    NUL-terminated blob and releases it with pt_free().
 *  - pt_table_columns_restore() validates the whole blob before touching the
 *    table. On failure the current layout is untouched (atomic); the example
 *    keeps the default blob so Reset always has a known-good target.
 *  - Titles, alignment and resizability are model configuration and are not
 *    part of the blob. Pt never touches the filesystem here; the application
 *    decides where to store the string.
 */

#include <pt/pt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLUMN_COUNT 6u
#define ROW_COUNT 14u

typedef struct file_row {
    const char *name;
    const char *kind;
    const char *size;
    const char *owner;
    const char *status;
    const char *path;
} file_row;

static const file_row rows[ROW_COUNT] = {
    {"app.c", "C source", "18 KB", "ada", "clean", "src/app.c"},
    {"canvas.c", "C source", "42 KB", "ada", "clean", "src/canvas.c"},
    {"font.c", "C source", "31 KB", "grace", "dirty", "src/font.c"},
    {"icon.c", "C source", "27 KB", "grace", "clean", "src/icon.c"},
    {"picker.c", "C source", "48 KB", "linus", "clean", "src/picker.c"},
    {"popup.c", "C source", "22 KB", "linus", "clean", "src/popup.c"},
    {"table.c", "C source", "58 KB", "ada", "dirty", "src/table.c"},
    {"window.c", "C source", "64 KB", "grace", "clean", "src/window.c"},
    {"pt.h", "Header", "12 KB", "ada", "clean", "include/pt/pt.h"},
    {"popup.h", "Header", "4 KB", "linus", "clean", "include/pt/popup.h"},
    {"picker.h", "Header", "9 KB", "linus", "clean", "include/pt/picker.h"},
    {"column_persist.c", "Example", "6 KB", "grace", "new", "examples/c/column_persist.c"},
    {"README.md", "Docs", "40 KB", "ada", "clean", "README.md"},
    {"NOTICE", "Legal", "1 KB", "ada", "clean", "NOTICE"},
};

static const char *const column_keys[COLUMN_COUNT] = {
    "name", "kind", "size", "owner", "status", "path",
};
static const char *const column_titles[COLUMN_COUNT] = {
    "Name", "Kind", "Size", "Owner", "Status", "Path",
};
static const float column_widths[COLUMN_COUNT] = {190.0f, 110.0f, 80.0f, 90.0f, 90.0f, 240.0f};

typedef struct persist_state {
    pt_app *app;
    pt_window *window;
    pt_widget *table;
    pt_widget *owner_button;
    pt_widget *blob_view;
    pt_widget *status;
    char *saved_blob;
    char *default_blob;
    bool owner_hidden;
    unsigned actions;
} persist_state;

static const file_row *row_at(uint32_t row) {
    return row < ROW_COUNT ? &rows[row] : NULL;
}

static const char *field_for_key(const file_row *row, const char *key) {
    if (!row || !key) {
        return "";
    }
    if (strcmp(key, "name") == 0) {
        return row->name;
    }
    if (strcmp(key, "kind") == 0) {
        return row->kind;
    }
    if (strcmp(key, "size") == 0) {
        return row->size;
    }
    if (strcmp(key, "owner") == 0) {
        return row->owner;
    }
    if (strcmp(key, "status") == 0) {
        return row->status;
    }
    if (strcmp(key, "path") == 0) {
        return row->path;
    }
    return "";
}

static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column,
                      void *user_data) {
    (void)user_data;
    const file_row *item = row_at(row);
    const char *key = pt_table_get_column_key(table, column);
    pt_widget_set_text(cell, field_for_key(item, key));
    if (item && strcmp(field_for_key(item, key), "dirty") == 0) {
        pt_widget_set_foreground(cell, pt_rgb(0xF7768E));
    } else if (item && strcmp(field_for_key(item, key), "new") == 0) {
        pt_widget_set_foreground(cell, pt_rgb(0x9ECE6A));
    } else if (item && strcmp(key ? key : "", "path") == 0) {
        pt_widget_set_foreground(cell, pt_rgb(0x7AA2F7));
    }
}

static void report(persist_state *state, const char *what, bool ok) {
    char text[320];
    snprintf(text, sizeof(text), "Action #%u · %s · %s", ++state->actions, what,
             ok ? "ok" : pt_get_last_error());
    pt_widget_set_text(state->status, text);
    printf("column_persist: %s\n", text);
    fflush(stdout);
}

static void show_blob(persist_state *state, const char *blob, const char *what) {
    if (!blob) {
        pt_text_area_set_placeholder(state->blob_view, "(save failed)");
        return;
    }
    pt_widget_set_text(state->blob_view, blob);
    char text[160];
    snprintf(text, sizeof(text), "%s · %zu-byte blob shown in the text area", what, strlen(blob));
    pt_widget_set_text(state->status, text);
}

static void on_narrow(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    float width = pt_table_get_column_width(state->table, 0);
    float next = width > 120.0f ? width - 40.0f : 280.0f;
    pt_table_set_column_width(state->table, 0, next);
    char what[96];
    snprintf(what, sizeof(what), "Column 0 width %.0f → %.0f px", (double)width, (double)next);
    report(state, what, true);
}

static void on_hide_owner(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    state->owner_hidden = !state->owner_hidden;
    /* The owner column keeps its key, so hide by key rather than by display
     * order: find the current position first. */
    uint32_t owner_column = pt_table_column_none;
    for (uint32_t column = 0; column < pt_table_get_column_count(state->table); ++column) {
        if (strcmp(pt_table_get_column_key(state->table, column), "owner") == 0) {
            owner_column = column;
            break;
        }
    }
    if (owner_column == pt_table_column_none) {
        report(state, "Owner column not found", false);
        return;
    }
    pt_table_set_column_hidden(state->table, owner_column, state->owner_hidden);
    pt_widget_set_text(state->owner_button,
                       state->owner_hidden ? "Show Owner" : "Hide Owner");
    report(state, state->owner_hidden ? "Owner hidden" : "Owner shown", true);
}

static void on_move_size(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    uint32_t size_column = pt_table_column_none;
    for (uint32_t column = 0; column < pt_table_get_column_count(state->table); ++column) {
        if (strcmp(pt_table_get_column_key(state->table, column), "size") == 0) {
            size_column = column;
            break;
        }
    }
    if (size_column == pt_table_column_none) {
        report(state, "Size column not found", false);
        return;
    }
    if (size_column == 0) {
        report(state, "Size is already first", true);
        return;
    }
    bool moved = pt_table_move_column(state->table, size_column, 0);
    report(state, "Moved Size to display position 0", moved);
}

static void on_sort_size(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    uint32_t size_column = pt_table_column_none;
    for (uint32_t column = 0; column < pt_table_get_column_count(state->table); ++column) {
        if (strcmp(pt_table_get_column_key(state->table, column), "size") == 0) {
            size_column = column;
            break;
        }
    }
    pt_sort_direction direction = pt_sort_descending;
    if (size_column == pt_table_column_none) {
        report(state, "Size column not found", false);
        return;
    }
    pt_table_set_sort_columns(state->table, 1, &size_column, &direction);
    report(state, "Primary sort: Size descending", true);
}

static void on_save(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    char *blob = pt_table_columns_save(state->table);
    if (!blob) {
        report(state, "pt_table_columns_save failed", false);
        return;
    }
    pt_free(state->saved_blob);
    state->saved_blob = blob;
    show_blob(state, blob, "Saved the current display layout");
}

static void on_restore(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    if (!state->saved_blob) {
        report(state, "Save a layout before restoring", false);
        return;
    }
    bool restored = pt_table_columns_restore(state->table, state->saved_blob);
    report(state, restored ? "Restored the saved display layout"
                           : "Restore rejected the blob; layout untouched",
           restored);
    if (restored) {
        show_blob(state, state->saved_blob, "Restored the saved display layout");
    }
}

static void on_reset(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    if (!state->default_blob) {
        report(state, "No built-in default layout", false);
        return;
    }
    bool restored = pt_table_columns_restore(state->table, state->default_blob);
    report(state, restored ? "Reset to the startup layout"
                           : "Reset blob rejected; layout untouched",
           restored);
    if (restored) {
        state->owner_hidden = false;
        pt_widget_set_text(state->owner_button, "Hide Owner");
    }
}

static void on_bad_restore(pt_widget *button, void *user_data) {
    (void)button;
    persist_state *state = user_data;
    /* A bad magic or truncated line must be rejected atomically. */
    bool restored = pt_table_columns_restore(state->table, "PTCOLS 9\nC 6\n");
    char what[128];
    snprintf(what, sizeof(what), "Malformed blob correctly %s",
             restored ? "accepted (unexpected)" : "rejected");
    report(state, what, !restored);
}

static void on_columns_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    persist_state *state = data;
    (void)table;
    char what[160];
    snprintf(what, sizeof(what), "Columns reordered %u → %u; keys follow the columns", from, to);
    report(state, what, true);
}

static void on_sort_columns(pt_widget *table, uint32_t count, const uint32_t *columns,
                            const pt_sort_direction *directions, void *user_data) {
    (void)table;
    persist_state *state = user_data;
    char what[160];
    size_t used = 0;
    used += (size_t)snprintf(what + used, sizeof(what) - used, "Sort:");
    for (uint32_t index = 0; index < count && used + 1 < sizeof(what); ++index) {
        used += (size_t)snprintf(what + used, sizeof(what) - used, " col%u=%s", columns[index],
                                 directions[index] == pt_sort_ascending   ? "asc"
                                 : directions[index] == pt_sort_descending ? "desc"
                                                                           : "none");
    }
    report(state, what, true);
}

static void on_quit(pt_widget *button, void *user_data) {
    (void)button;
    pt_app_quit((pt_app *)user_data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.examples.column_persist");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_accent_color(app, pt_rgb(0x7AA2F7));

    pt_window *window = pt_window_create(app, "Pt Column Persistence", 980, 720);
    if (!window) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        pt_app_destroy(app);
        return 1;
    }

    persist_state state = {.app = app, .window = window};
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 24.0f, 24.0f, 24.0f, 24.0f);
    pt_widget_set_gap(root, 12.0f);

    pt_widget *eyebrow = pt_label_create(root, "TABLE  ·  COLUMN LAYOUT  ·  1.2");
    pt_widget_set_font_size(eyebrow, 11.0f);
    pt_widget_set_font_weight(eyebrow, pt_font_weight_semibold);
    pt_widget_set_foreground(eyebrow, pt_rgb(0x7AA2F7));

    pt_widget *heading = pt_label_create(root, "Save and restore the display layout");
    pt_widget_set_font_size(heading, 28.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);

    pt_widget *intro = pt_label_create(
        root, "Resize a header edge, drag a column, hide Owner or sort by Size, then Save. The "
              "blob in the text area carries the display order by key, widths, hidden flags and "
              "sort state. Restore validates everything before touching the table.");
    pt_widget_set_font_size(intro, 13.0f);
    pt_widget_set_foreground(intro, pt_rgb(0x9AA7B8));
    pt_widget_set_label_wrap(intro, true);

    pt_widget *table_card = pt_card_create(root);
    pt_widget_set_grow(table_card, 1.0f);
    pt_widget_set_padding(table_card, 14.0f, 14.0f, 14.0f, 14.0f);
    pt_widget_set_gap(table_card, 10.0f);

    state.table = pt_table_create(table_card);
    pt_widget_set_grow(state.table, 1.0f);
    pt_widget_set_min_height(state.table, 240.0f);
    pt_widget_set_a11y_name(state.table, "Files with persistent column layout");
    pt_table_set_column_count(state.table, COLUMN_COUNT);
    for (uint32_t column = 0; column < COLUMN_COUNT; ++column) {
        pt_table_set_column_key(state.table, column, column_keys[column]);
        pt_table_set_column_title(state.table, column, column_titles[column]);
        pt_table_set_column_width(state.table, column, column_widths[column]);
        pt_table_set_column_resizable(state.table, column, true);
        pt_table_set_column_align(state.table, column,
                                  strcmp(column_keys[column], "size") == 0 ? pt_text_align_end
                                                                           : pt_text_align_start);
    }
    pt_table_set_row_height(state.table, 32.0f);
    pt_table_set_header_height(state.table, 38.0f);
    pt_table_set_row_count(state.table, ROW_COUNT);
    pt_table_set_on_bind_cell(state.table, bind_cell, &state);
    pt_table_set_columns_reorderable(state.table, true);
    pt_table_set_on_columns_reordered(state.table, on_columns_reordered, &state);
    pt_table_set_on_sort_columns(state.table, on_sort_columns, &state);

    /* A known-good startup layout is the target for Reset. */
    state.default_blob = pt_table_columns_save(state.table);
    if (!state.default_blob) {
        fprintf(stderr, "pt: default layout save: %s\n", pt_get_last_error());
    }

    pt_widget *tools = pt_wrap_create(table_card);
    pt_wrap_set_gap(tools, 8.0f, 8.0f);
    pt_widget *narrow = pt_button_create(tools, "Narrow Name");
    pt_button_set_variant(narrow, pt_button_secondary);
    pt_button_set_on_click(narrow, on_narrow, &state);
    state.owner_button = pt_button_create(tools, "Hide Owner");
    pt_button_set_variant(state.owner_button, pt_button_secondary);
    pt_button_set_on_click(state.owner_button, on_hide_owner, &state);
    pt_widget *move = pt_button_create(tools, "Move Size first");
    pt_button_set_variant(move, pt_button_secondary);
    pt_button_set_on_click(move, on_move_size, &state);
    pt_widget *sort = pt_button_create(tools, "Sort Size ↓");
    pt_button_set_variant(sort, pt_button_soft);
    pt_button_set_on_click(sort, on_sort_size, &state);
    pt_widget *save = pt_button_create(tools, "Save layout");
    pt_button_set_variant(save, pt_button_primary);
    pt_button_set_on_click(save, on_save, &state);
    pt_widget *restore = pt_button_create(tools, "Restore layout");
    pt_button_set_variant(restore, pt_button_secondary);
    pt_button_set_on_click(restore, on_restore, &state);
    pt_widget *reset = pt_button_create(tools, "Reset layout");
    pt_button_set_variant(reset, pt_button_ghost);
    pt_button_set_on_click(reset, on_reset, &state);
    pt_widget *bad = pt_button_create(tools, "Try malformed blob");
    pt_button_set_variant(bad, pt_button_ghost);
    pt_button_set_on_click(bad, on_bad_restore, &state);
    pt_widget_set_tooltip(bad, "Proves restore is atomic: the layout must not change");

    state.blob_view = pt_text_area_create(table_card, "Saved layout blob appears here");
    pt_widget_set_height(state.blob_view, 124.0f);
    pt_widget_set_a11y_name(state.blob_view, "Saved layout blob");
    if (state.default_blob) {
        pt_widget_set_text(state.blob_view, state.default_blob);
    }

    state.status = pt_label_create(table_card, "Ready · change a column, then Save");
    pt_widget_set_font_size(state.status, 12.0f);
    pt_widget_set_foreground(state.status, pt_rgb(0x9ECE6A));
    pt_widget_set_label_wrap(state.status, true);
    pt_widget_set_a11y_name(state.status, "Layout status");

    pt_widget *footer = pt_label_create(
        root, "The blob is plain ASCII: PTCOLS 1, a column count, then key/width/hidden lines in "
              "display order and the sort ordinals. An application persists the string; Pt "
              "never writes files.");
    pt_widget_set_font_size(footer, 12.0f);
    pt_widget_set_foreground(footer, pt_rgb(0x6F7C8F));
    pt_widget_set_label_wrap(footer, true);

    pt_widget *quit = pt_button_create(root, "Quit");
    pt_button_set_variant(quit, pt_button_ghost);
    pt_button_set_on_click(quit, on_quit, app);
    pt_widget_set_align_self(quit, pt_align_start);

    pt_window_show(window);
    pt_widget_focus(state.table);

    const char *headless = getenv("PT_HEADLESS");
    int status;
    if (headless && *headless && strcmp(headless, "0") != 0) {
        printf("column_persist: default blob %zu bytes\n",
               state.default_blob ? strlen(state.default_blob) : 0u);
        on_save(NULL, &state);
        on_narrow(NULL, &state);
        on_hide_owner(NULL, &state);
        on_move_size(NULL, &state);
        on_sort_size(NULL, &state);
        on_save(NULL, &state);
        printf("column_persist: saved blob %zu bytes\n",
               state.saved_blob ? strlen(state.saved_blob) : 0u);
        on_bad_restore(NULL, &state);
        on_restore(NULL, &state);
        on_reset(NULL, &state);
        uint32_t sort_column = pt_table_column_none;
        pt_sort_direction sort_direction = pt_table_get_sort(state.table, &sort_column);
        printf("column_persist: final columns=%u owner_hidden=%d sort=%d column=%d\n",
               pt_table_get_column_count(state.table),
               pt_table_get_column_hidden(state.table, 3) ? 1 : 0, (int)sort_direction,
               (int)sort_column);
        status = pt_app_run_for(app, 250);
    } else {
        status = pt_app_run(app);
    }
    pt_free(state.saved_blob);
    pt_free(state.default_blob);
    pt_app_destroy(app);
    return status;
}
