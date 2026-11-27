/*
 * Pt theme editor - preview window.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * The preview is a real top-level Pt window that shows a representative sample
 * of the built-in controls. It is restyled only through the public per-window
 * override API: pt_window_set_theme_color() and pt_window_set_theme_metric().
 * The editor's own window keeps the neutral default palette, so a theme being
 * edited never makes the editing controls unreadable. Only public headers are
 * used; there is no private Pt state and no way to reach the editor window from
 * here.
 *
 * Sample coverage follows the M27 design (docs/m27-design.md, "Preview
 * window"): a heading label and a muted label, primary/secondary/danger
 * buttons, an entry, a combobox, a checkbox, a toggle, a slider, a small tabs
 * strip, a small table and one focused control. Every control paints from the
 * window palette, so applying 38 colours plus 10 metrics restyles the whole
 * sample at once.
 */

#include "preview.h"

#include <stddef.h>
#include <stdlib.h>

/* Data rows shown by the sample table. Enough to fill the viewport without
 * making the preview scroll; the values are cosmetic and only exercise the
 * table palette (header, row, cell, selection). */
#define TE_PREVIEW_TABLE_ROWS 3u
#define TE_PREVIEW_TABLE_COLUMNS 2u

struct te_preview {
    pt_window *window;
};

/* Fills one data cell from the static sample rows. The bind callback receives
 * the zero-based data row: the header is a separate widget driven by
 * pt_table_set_column_title, never a bound row. The table paints its own
 * header, row stripes and selection from the window palette; the cells only
 * own their text, which keeps the preview free of widget-level explicit colours
 * that would defeat a theme override. */
static void te_preview_bind_cell(pt_widget *table, pt_widget *cell, uint32_t row,
                                 uint32_t column, void *data) {
    (void)table;
    (void)data;
    static const char *const names[TE_PREVIEW_TABLE_ROWS] = {
        "background",
        "text_muted",
        "accent",
    };
    static const char *const values[TE_PREVIEW_TABLE_ROWS] = {
        "#0B0E14FF",
        "#8B98AAFF",
        "#4C8DFFFF",
    };
    if (column >= TE_PREVIEW_TABLE_COLUMNS || row >= TE_PREVIEW_TABLE_ROWS) {
        pt_widget_set_text(cell, "");
        return;
    }
    pt_widget_set_text(cell, column == 0 ? names[row] : values[row]);
}

/* Builds the sample controls under root. Order follows the design list so the
 * editor can point at a region when explaining a role. */
static void te_preview_build(pt_widget *root) {
    if (root == NULL) {
        return;
    }
    pt_widget_set_padding(root, 20.0f, 20.0f, 20.0f, 20.0f);
    pt_widget_set_gap(root, 12.0f);

    /* Labels: a heading in the default text role and a smaller muted caption.
     * Neither sets an explicit colour, so both track the window override. */
    pt_widget *heading = pt_label_create(root, "Preview");
    pt_widget_set_font_size(heading, 22.0f);
    pt_widget_set_font_weight(heading, pt_font_weight_semibold);
    pt_widget_set_a11y_name(heading, "Preview heading");

    pt_widget *caption =
        pt_label_create(root, "Controls in this window use only the edited palette.");
    pt_widget_set_font_size(caption, 13.0f);
    pt_widget_set_label_wrap(caption, true);
    pt_widget_set_a11y_name(caption, "Preview caption");

    /* Buttons: one of each semantic variant so accent, accent text and the
     * danger role are all visible at once. */
    pt_widget *actions = pt_row_create(root);
    pt_widget_set_gap(actions, 8.0f);
    pt_widget *primary = pt_button_create(actions, "Primary");
    pt_button_set_variant(primary, pt_button_primary);
    pt_widget_set_a11y_name(primary, "Primary button");
    pt_widget *secondary = pt_button_create(actions, "Secondary");
    pt_button_set_variant(secondary, pt_button_secondary);
    pt_widget_set_a11y_name(secondary, "Secondary button");
    pt_widget *danger = pt_button_create(actions, "Danger");
    pt_button_set_variant(danger, pt_button_danger);
    pt_widget_set_a11y_name(danger, "Danger button");

    /* Text input and closed pick list. */
    pt_widget *entry = pt_entry_create(root, "Type here");
    pt_widget_set_a11y_name(entry, "Preview entry");
    pt_widget *combo = pt_combobox_create(root);
    pt_widget_set_a11y_name(combo, "Preview combobox");
    pt_combobox_add_item(combo, "Dark");
    pt_combobox_add_item(combo, "Light");
    pt_combobox_add_item(combo, "Custom");
    pt_combobox_set_selected(combo, 0);

    /* Selection and range controls, pre-set so their active track and check
     * marks paint immediately. */
    pt_widget *check = pt_checkbox_create(root, "Enable motion");
    pt_widget_set_a11y_name(check, "Preview checkbox");
    pt_widget_set_checked(check, true);

    pt_widget *toggle_row = pt_row_create(root);
    pt_widget_set_gap(toggle_row, 10.0f);
    pt_widget *toggle = pt_toggle_create(toggle_row);
    pt_widget_set_a11y_name(toggle, "Preview toggle");
    pt_toggle_set_checked(toggle, true);
    pt_widget *toggle_label = pt_label_create(toggle_row, "Compact density");
    pt_widget_set_font_size(toggle_label, 13.0f);

    pt_widget *slider = pt_slider_create(root);
    pt_widget_set_a11y_name(slider, "Preview slider");
    pt_slider_set_value(slider, 0.6f);

    /* Tabs: a two-page strip showing the active indicator and tab text. */
    pt_widget *tabs = pt_tabs_create(root);
    pt_widget_set_a11y_name(tabs, "Preview tabs");
    pt_tabs_add_page(tabs, "Colours");
    pt_tabs_add_page(tabs, "Metrics");
    pt_tabs_set_active(tabs, 0);

    /* A small table: the column titles drive the real header and the bind
     * callback fills only the three data rows. Header cells, body cells, the
     * selected row and the selection all paint from the surface, border, text
     * and selection roles. */
    pt_widget *table = pt_table_create(root);
    pt_widget_set_a11y_name(table, "Preview table");
    pt_widget_set_min_height(table, 132.0f);
    pt_table_set_column_count(table, TE_PREVIEW_TABLE_COLUMNS);
    pt_table_set_column_title(table, 0, "Role");
    pt_table_set_column_title(table, 1, "Value");
    pt_table_set_column_width(table, 0, 160.0f);
    pt_table_set_column_width(table, 1, 160.0f);
    pt_table_set_row_height(table, 30.0f);
    pt_table_set_header_height(table, 32.0f);
    pt_table_set_on_bind_cell(table, te_preview_bind_cell, NULL);
    pt_table_set_row_count(table, TE_PREVIEW_TABLE_ROWS);
    /* The first data row (background) is selected, matching the old three-row
     * intent where header-plus-data indexing selected that same row. */
    pt_table_set_selected(table, 0);

    /* One focused control so the focus ring and focus token are visible without
     * the user having to tab. pt_widget_focus returns false when the window is
     * not yet shown; that is fine, the control simply shows its resting state.
     */
    (void)pt_widget_focus(entry);
}

te_preview *te_preview_create(pt_app *app, const te_theme_doc *initial) {
    if (app == NULL) {
        return NULL;
    }
    te_preview *preview = calloc(1, sizeof(*preview));
    if (preview == NULL) {
        return NULL;
    }
    preview->window = pt_window_create(app, "Theme preview", 360, 560);
    if (preview->window == NULL) {
        free(preview);
        return NULL;
    }
    pt_widget *root = pt_window_root(preview->window);
    te_preview_build(root);
    if (initial != NULL) {
        te_preview_apply(preview, initial);
    }
    return preview;
}

void te_preview_apply(te_preview *preview, const te_theme_doc *doc) {
    if (preview == NULL || preview->window == NULL || doc == NULL) {
        return;
    }
    for (int i = 0; i < pt_theme_token_count; ++i) {
        pt_window_set_theme_color(preview->window, (pt_theme_token)i, doc->colors[i]);
    }
    for (int i = 0; i < pt_theme_metric_count; ++i) {
        pt_window_set_theme_metric(preview->window, (pt_theme_metric)i, doc->metrics[i]);
    }
}

void te_preview_reset(te_preview *preview) {
    if (preview == NULL || preview->window == NULL) {
        return;
    }
    pt_window_clear_theme_overrides(preview->window);
}

pt_window *te_preview_window(const te_preview *preview) {
    return preview != NULL ? preview->window : NULL;
}

void te_preview_destroy(te_preview *preview) {
    if (preview == NULL) {
        return;
    }
    if (preview->window != NULL) {
        /* The application owns every window; pt_window_close is the public
         * request and pt_app_destroy releases the surface. */
        pt_window_close(preview->window);
    }
    free(preview);
}
