/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_widget_h
#define pt_widget_h

#include <pt/base.h>

/* Header contract
 *
 * Threading. UI thread only, including getters and accessibility queries.
 * Ownership. Constructors append the new widget to parent and transfer
 * ownership to the parent's subtree. A NULL parent returns a detached tree owned
 * by the caller and released with pt_widget_destroy. Handles returned by
 * pt_widget_parent, the pt_tree_* lookups, pt_tabs_get_page and
 * pt_sidebar_get_item are borrowed. Text returned by a getter is borrowed from
 * the widget and invalidated by the next mutation of that text. List, table and
 * tree row/cell widgets are materialized and owned by the view: a bind callback
 * must configure them but never destroy or reparent them.
 * Errors. Constructors return NULL and set pt_get_last_error. Setters are
 * no-ops for NULL, a mismatched widget kind or an out-of-range index. Functions
 * returning bool return false for an invalid object or an operation that does
 * not apply. Getters clamp and return a default. Selection and checked-state
 * setters are silent: they never fire on_select, on_change or on_checked,
 * matching every set_selected in the API.
 * Callbacks. All callbacks run on the UI thread. user_data must stay valid until
 * the callback is replaced or the owning widget is destroyed. A text pointer
 * passed to a callback is valid only for that call unless its typedef says
 * otherwise. Callbacks may call the API and may destroy their own widget after
 * returning; they must not block.
 * Text input. Pt pushes text-input-v3 state when focus enters a text input,
 * when a keystroke or pointer gesture changes the text or caret, and when a
 * composition ends: content type, surrounding text, caret rectangle, then
 * commit. An empty done with no composition in flight is the input method's
 * acknowledgement and is ignored, because replying to it would ask for another
 * acknowledgement. The push moments are part of the frozen contract.
 * Text. Strings are UTF-8 and offsets are byte positions. Selection endpoints
 * may be given in either order and are clamped. Strings passed to setters and
 * span arrays are copied.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pt_widget_kind {
    pt_widget_box = 0,
    pt_widget_label,
    pt_widget_button,
    pt_widget_entry,
    pt_widget_toggle,
    pt_widget_slider,
    pt_widget_scroll,
    pt_widget_divider,
    pt_widget_card,
    pt_widget_spacer,
    pt_widget_checkbox,
    pt_widget_radio,
    pt_widget_progress,
    pt_widget_spinner,
    pt_widget_segmented,
    pt_widget_text_area,
    pt_widget_wrap,
    pt_widget_grid,
    pt_widget_list,
    pt_widget_list_row,
    pt_widget_icon,
    pt_widget_table,
    pt_widget_table_header,
    pt_widget_table_header_cell,
    pt_widget_table_row,
    pt_widget_table_cell,
    pt_widget_tree,
    pt_widget_tree_row,
    pt_widget_tabs,
    pt_widget_tab_page,
    pt_widget_sidebar,
    pt_widget_sidebar_item,
    pt_widget_chip,
    pt_widget_skeleton,
    pt_widget_combobox
} pt_widget_kind;

typedef enum pt_button_variant {
    pt_button_primary = 0,
    pt_button_secondary,
    pt_button_ghost,
    pt_button_danger,
    pt_button_soft
} pt_button_variant;

/* Informs the compositor and input method about the expected content of an
 * entry or text area. Masking a password is controlled separately with
 * pt_entry_set_password. */
typedef enum pt_input_purpose {
    pt_input_purpose_normal = 0,
    pt_input_purpose_email,
    pt_input_purpose_url,
    pt_input_purpose_phone,
    pt_input_purpose_number,
    pt_input_purpose_password,
    pt_input_purpose_terminal
} pt_input_purpose;

/* Constructors append the new widget to parent. A NULL parent creates a
 * detached tree, useful for tests. Container constructors accept children.
 * Since 1.0. */
pt_api pt_widget *pt_box_create(pt_widget *parent, pt_direction direction);
/* Since 1.0. */
pt_api pt_widget *pt_row_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_column_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_label_create(pt_widget *parent, const char *text);
/* Since 1.0. */
pt_api pt_widget *pt_button_create(pt_widget *parent, const char *text);
/* Since 1.0. */
pt_api pt_widget *pt_entry_create(pt_widget *parent, const char *placeholder);
/* Since 1.0. */
pt_api pt_widget *pt_toggle_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_slider_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_scroll_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_divider_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_card_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_spacer_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_checkbox_create(pt_widget *parent, const char *text);
/* Since 1.0. */
pt_api pt_widget *pt_radio_create(pt_widget *parent, const char *text);
/* Since 1.0. */
pt_api pt_widget *pt_progress_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_spinner_create(pt_widget *parent);
/* Chips are compact pill controls with an optional leading icon and trailing
 * remove action. A removable chip sends on_remove when the trailing region is
 * clicked and on_click for the rest of the chip. Chips can be selected to
 * show an accent-soft filter state. Since 1.0. */
pt_api pt_widget *pt_chip_create(pt_widget *parent, const char *text);

/* A skeleton placeholder draws a token-colored block with a moving highlight.
 * Size it with the normal layout helpers; the default is 120x12. Since 1.0. */
pt_api pt_widget *pt_skeleton_create(pt_widget *parent);

/* Segmented controls copy labels and own them. The callback receives the
 * selected index. Radio siblings that share a parent are mutually exclusive.
 * Since 1.0. */
pt_api pt_widget *pt_segmented_create(pt_widget *parent, const char *const *labels,
                                      uint32_t count);
/* Since 1.0. */
pt_api void pt_segmented_set_on_change(pt_widget *segmented, pt_index_callback callback,
                                       void *user_data);
/* Since 1.0. */
pt_api uint32_t pt_segmented_get_selected(const pt_widget *segmented);
/* Since 1.0. */
pt_api void pt_segmented_set_selected(pt_widget *segmented, uint32_t index);

/* Destroys the widget and its subtree and detaches it from its parent. Since 1.0. */
pt_api void pt_widget_destroy(pt_widget *widget);
/* Since 1.0. */
pt_api void pt_widget_clear_children(pt_widget *widget);
/* Since 1.0. */
pt_api pt_widget *pt_widget_parent(const pt_widget *widget);
/* Since 1.0. */
pt_api pt_widget_kind pt_widget_get_kind(const pt_widget *widget);

/* Layout. Since 1.0. */
pt_api void pt_widget_set_width(pt_widget *widget, float width);
/* Since 1.0. */
pt_api void pt_widget_set_height(pt_widget *widget, float height);
/* Since 1.0. */
pt_api void pt_widget_set_size(pt_widget *widget, float width, float height);
/* Since 1.0. */
pt_api void pt_widget_set_min_width(pt_widget *widget, float width);
/* Since 1.0. */
pt_api void pt_widget_set_min_height(pt_widget *widget, float height);
/* Since 1.0. */
pt_api void pt_widget_set_max_width(pt_widget *widget, float width);
/* Since 1.0. */
pt_api void pt_widget_set_max_height(pt_widget *widget, float height);
/* Since 1.0. */
pt_api void pt_widget_set_margin(pt_widget *widget, float top, float right, float bottom,
                                 float left);
/* Since 1.0. */
pt_api void pt_widget_set_padding(pt_widget *widget, float top, float right, float bottom,
                                  float left);
/* Since 1.0. */
pt_api void pt_widget_set_gap(pt_widget *widget, float gap);
/* Since 1.0. */
pt_api void pt_widget_set_grow(pt_widget *widget, float grow);
/* Since 1.0. */
pt_api void pt_widget_set_align_self(pt_widget *widget, pt_align align);
/* Since 1.0. */
pt_api void pt_box_set_direction(pt_widget *box, pt_direction direction);
/* Since 1.0. */
pt_api void pt_box_set_gap(pt_widget *box, float gap);
/* Since 1.0. */
pt_api void pt_box_set_align(pt_widget *box, pt_align align);
/* Since 1.0. */
pt_api void pt_box_set_justify(pt_widget *box, pt_justify justify);

/* Appearance. Colors are 0xRRGGBBAA. Explicit colors override the theme. Since 1.0. */
pt_api void pt_widget_set_background(pt_widget *widget, pt_color color);
/* Since 1.0. */
pt_api void pt_widget_set_foreground(pt_widget *widget, pt_color color);
/* Since 1.0. */
pt_api void pt_widget_set_border_color(pt_widget *widget, pt_color color);
/* Since 1.0. */
pt_api void pt_widget_set_border_width(pt_widget *widget, float width);
/* Since 1.0. */
pt_api void pt_widget_set_radius(pt_widget *widget, float radius);
/* Since 1.0. */
pt_api void pt_widget_set_font_size(pt_widget *widget, float size);
/* Since 1.0. */
pt_api void pt_widget_set_font_weight(pt_widget *widget, pt_font_weight weight);
/* Since 1.0. */
pt_api void pt_widget_set_text_align(pt_widget *widget, pt_text_align align);

/* Direction properties (since 2.0). UI direction orders row children, resolves
 * start/end alignment, padding and margins, scrollbar sides and mirrorable
 * chrome; text direction controls paragraph ordering and glyph shaping. They
 * are independent, so an LTR editor can sit inside an RTL shell.
 *
 * UI resolution is the widget's explicit value, then the nearest ancestor with
 * an explicit value, then the window, then the application, then LTR. Text
 * resolution is the widget's explicit value, then the nearest ancestor, then
 * auto. Auto follows UBA P2/P3: the first strong character in a paragraph
 * selects RTL for R/AL and LTR for L, text inside an isolate is skipped, and an
 * empty or neutral-only paragraph falls back to the resolved UI direction.
 *
 * Setters store the explicit value, ignore NULL or an out-of-range enum, and
 * invalidate layout and repaint on a change. Getters return the stored explicit
 * value, never the resolved one: an unset widget reports
 * pt_ui_direction_inherit or pt_text_direction_auto. Both zero values keep the
 * 1.x LTR behaviour. Direction is a presentation property; it never changes
 * UTF-8 byte offsets, selections or accessibility character offsets. Since 2.0. */
pt_api void pt_widget_set_ui_direction(pt_widget *widget, pt_ui_direction direction);
/* Since 2.0. */
pt_api pt_ui_direction pt_widget_get_ui_direction(const pt_widget *widget);
/* Since 2.0. Paragraph text direction. It is stored independently from the UI
 * direction, so an LTR editor can sit inside an RTL shell and vice versa.
 * pt_text_direction_auto (the zero value) resolves each paragraph from UBA
 * P2/P3: the first strong character selects RTL for R/AL and LTR for L, text
 * inside an isolate is skipped, and an empty or neutral-only first paragraph
 * falls back to the resolved UI direction. pt_text_direction_ltr and
 * pt_text_direction_rtl force the paragraph base and are not overridden by the
 * UI direction. The setter stores the explicit value, ignores NULL or an
 * out-of-range enum, and invalidates layout and repaint on a change. The getter
 * returns the stored explicit value (auto when unset), never the resolved one.
 * Direction is presentation only: it never changes stored UTF-8 bytes,
 * selections or accessibility character offsets. */
pt_api void pt_widget_set_text_direction(pt_widget *widget, pt_text_direction direction);
/* Since 2.0. */
pt_api pt_text_direction pt_widget_get_text_direction(const pt_widget *widget);

/* Since 1.0. */
pt_api void pt_widget_set_visible(pt_widget *widget, bool visible);
/* Since 1.0. */
pt_api void pt_widget_set_enabled(pt_widget *widget, bool enabled);
/* Since 1.0. */
pt_api void pt_widget_set_label_wrap(pt_widget *widget, bool wrap);

/* Since 1.0. */
pt_api void pt_widget_set_text(pt_widget *widget, const char *text);
/* Since 1.0. */
pt_api const char *pt_widget_get_text(const pt_widget *widget);

/* Styled ranges on labels and text areas. The setter copies and clamps the
 * spans; querying returns the stored ranges. Passing NULL or zero count is
 * equivalent to pt_widget_clear_text_spans. Since 1.0. */
pt_api bool pt_widget_set_text_spans(pt_widget *widget, const pt_text_span *spans,
                                     uint32_t count);
/* Since 1.0. */
pt_api void pt_widget_clear_text_spans(pt_widget *widget);
/* Since 1.0. */
pt_api uint32_t pt_widget_get_text_span_count(const pt_widget *widget);
/* Since 1.0. */
pt_api bool pt_widget_get_text_span(const pt_widget *widget, uint32_t index,
                                    pt_text_span *span);

/* Tooltips appear after the delay while the pointer rests over the widget.
 * The default delay is 600 ms. Passing NULL or an empty string removes it.
 * Since 1.0. */
pt_api void pt_widget_set_tooltip(pt_widget *widget, const char *text);
/* Since 1.0. */
pt_api void pt_widget_set_tooltip_delay(pt_widget *widget, uint32_t milliseconds);

/* Buttons. Since 1.0. */
pt_api void pt_button_set_variant(pt_widget *button, pt_button_variant variant);
/* Since 1.0. */
pt_api void pt_button_set_on_click(pt_widget *button, pt_click_callback callback,
                                   void *user_data);

/* Chips. Since 1.0. */
pt_api void pt_chip_set_removable(pt_widget *chip, bool removable);
/* Since 1.0. */
pt_api bool pt_chip_get_removable(const pt_widget *chip);
/* Since 1.0. */
pt_api void pt_chip_set_selected(pt_widget *chip, bool selected);
/* Since 1.0. */
pt_api bool pt_chip_get_selected(const pt_widget *chip);
/* Since 1.0. */
pt_api void pt_chip_set_on_click(pt_widget *chip, pt_click_callback callback,
                                 void *user_data);
/* Since 1.0. */
pt_api void pt_chip_set_on_remove(pt_widget *chip, pt_click_callback callback,
                                  void *user_data);

/* Entries. Since 1.0. */
pt_api void pt_entry_set_placeholder(pt_widget *entry, const char *placeholder);
/* Since 1.0. */
pt_api void pt_entry_set_password(pt_widget *entry, bool password);
/* Since 1.0. */
pt_api void pt_widget_set_input_purpose(pt_widget *widget, pt_input_purpose purpose);
/* Since 1.0. */
pt_api pt_input_purpose pt_widget_get_input_purpose(const pt_widget *widget);
/* Since 1.0. */
pt_api void pt_entry_set_on_change(pt_widget *entry, pt_text_callback callback,
                                   void *user_data);
/* Since 1.0. */
pt_api void pt_entry_set_on_submit(pt_widget *entry, pt_click_callback callback,
                                   void *user_data);

/* Selection offsets are UTF-8 byte positions, clamped to the text length.
 * Endpoints may be given in either order. Since 1.0. */
pt_api void pt_entry_select_all(pt_widget *entry);
/* Since 1.0. */
pt_api void pt_entry_set_selection(pt_widget *entry, uint32_t start, uint32_t end);
/* Since 1.0. */
pt_api bool pt_entry_get_selection(const pt_widget *entry, uint32_t *start, uint32_t *end);
/* Since 1.0. */
pt_api bool pt_entry_undo(pt_widget *entry);
/* Since 1.0. */
pt_api bool pt_entry_redo(pt_widget *entry);
/* Since 1.0. */
pt_api bool pt_widget_focus(pt_widget *widget);
/* Since 1.0. */
pt_api bool pt_widget_has_focus(const pt_widget *widget);

/* Busy and invalid states apply to every control. A busy widget blocks
 * activation and paints a token-driven progress hint; an invalid widget is
 * normally an entry, text area or form control and paints a danger outline.
 * Since 1.0. */
pt_api void pt_widget_set_busy(pt_widget *widget, bool busy);
/* Since 1.0. */
pt_api bool pt_widget_get_busy(const pt_widget *widget);
/* Since 1.0. */
pt_api void pt_widget_set_invalid(pt_widget *widget, bool invalid);
/* Since 1.0. */
pt_api bool pt_widget_get_invalid(const pt_widget *widget);

/* Checked controls: toggle, checkbox and radio. Since 1.0. */
pt_api bool pt_widget_get_checked(const pt_widget *widget);
/* Since 1.0. */
pt_api void pt_widget_set_checked(pt_widget *widget, bool checked);
/* Since 1.0. */
pt_api void pt_widget_set_on_checked(pt_widget *widget, pt_value_callback callback,
                                     void *user_data);

/* Toggles. Since 1.0. */
pt_api bool pt_toggle_get_checked(const pt_widget *toggle);
/* Since 1.0. */
pt_api void pt_toggle_set_checked(pt_widget *toggle, bool checked);
/* Since 1.0. */
pt_api void pt_toggle_set_on_change(pt_widget *toggle, pt_value_callback callback,
                                    void *user_data);

/* Sliders use values in [0, 1]. Since 1.0. */
pt_api float pt_slider_get_value(const pt_widget *slider);
/* Since 1.0. */
pt_api void pt_slider_set_value(pt_widget *slider, float value);
/* Since 1.0. */
pt_api void pt_slider_set_on_change(pt_widget *slider, pt_value_callback callback,
                                    void *user_data);

/* Progress bars use values in [0, 1]; indeterminate bars animate on their own.
 * Since 1.0. */
pt_api float pt_progress_get_value(const pt_widget *progress);
/* Since 1.0. */
pt_api void pt_progress_set_value(pt_widget *progress, float value);
/* Since 1.0. */
pt_api bool pt_progress_get_indeterminate(const pt_widget *progress);
/* Since 1.0. */
pt_api void pt_progress_set_indeterminate(pt_widget *progress, bool indeterminate);

/* Scroll view helpers. Offsets are logical pixels and clamp to content. Since 1.0. */
pt_api void pt_scroll_set_offset(pt_widget *scroll, float offset);
/* Since 1.0. */
pt_api float pt_scroll_get_offset(const pt_widget *scroll);
/* Since 1.0. */
pt_api float pt_scroll_get_max_offset(const pt_widget *scroll);

/* Bounds are in window logical coordinates. Since 1.0. */
pt_api void pt_widget_get_bounds(const pt_widget *widget, float *x, float *y, float *width,
                                 float *height);

/* Multi-line text editor. It shares the entry editing model: selection offsets
 * are UTF-8 byte positions, Enter inserts a newline, and lines wrap to the
 * widget width. Grow-to-content sizes the widget to its wrapped text. Since 1.0. */
pt_api pt_widget *pt_text_area_create(pt_widget *parent, const char *placeholder);
/* Since 1.0. */
pt_api void pt_text_area_set_placeholder(pt_widget *area, const char *placeholder);
/* Since 1.0. */
pt_api void pt_text_area_set_on_change(pt_widget *area, pt_text_callback callback,
                                       void *user_data);
/* Since 1.0. */
pt_api void pt_text_area_set_grow_to_content(pt_widget *area, bool enabled);
/* Since 1.0. */
pt_api bool pt_text_area_get_grow_to_content(const pt_widget *area);
/* Since 1.0. */
pt_api void pt_text_area_scroll_to_caret(pt_widget *area);
/* Since 1.0. */
pt_api void pt_text_area_select_all(pt_widget *area);
/* Since 1.0. */
pt_api void pt_text_area_set_selection(pt_widget *area, uint32_t start, uint32_t end);
/* Since 1.0. */
pt_api bool pt_text_area_get_selection(const pt_widget *area, uint32_t *start, uint32_t *end);
/* Since 1.0. */
pt_api bool pt_text_area_undo(pt_widget *area);
/* Since 1.0. */
pt_api bool pt_text_area_redo(pt_widget *area);

/* Responsive containers. Wrap lays children into rows that fit the available
 * width; grid places them in equal-width columns. Defaults to stretch
 * children; use pt_widget_set_align_self for a different cross-axis rule. Since 1.0. */
pt_api pt_widget *pt_wrap_create(pt_widget *parent);
/* Since 1.0. */
pt_api void pt_wrap_set_gap(pt_widget *wrap, float row_gap, float column_gap);
/* Since 1.0. */
pt_api pt_widget *pt_grid_create(pt_widget *parent, uint32_t columns);
/* Since 1.0. */
pt_api void pt_grid_set_columns(pt_widget *grid, uint32_t columns);
/* Since 1.0. */
pt_api uint32_t pt_grid_get_columns(const pt_widget *grid);
/* Since 1.0. */
pt_api void pt_grid_set_gap(pt_widget *grid, float row_gap, float column_gap);
/* Since 1.0. */
pt_api void pt_widget_set_grid_span(pt_widget *widget, uint32_t span);
/* Since 1.0. */
pt_api uint32_t pt_widget_get_grid_span(const pt_widget *widget);

/* Virtualized list. Pt materializes and recycles only the row widgets needed
 * for the viewport plus one row of overscan. The bind callback configures a
 * row for an index; use pt_widget_set_text on the row for the default
 * renderer, or clear its children and add custom cells. Row widgets are owned
 * by the list and must not be destroyed by the application. */
#define pt_list_index_none ((uint32_t)-1)

/* Stable row identities for keyed data views. A key callback returns a
 * non-zero app-defined identifier for a model row. Views remember the
 * selected key so selection survives row-count changes and refreshes.
 * pt_row_key_none means "no selection". */
#define pt_row_key_none ((uint64_t)-1)

typedef uint64_t (*pt_row_key_callback)(pt_widget *view, uint32_t row, void *user_data);
/* Returns a borrowed row label used by keyboard type-ahead. The string only
 * needs to stay valid for the duration of the call. */
typedef const char *(*pt_row_text_callback)(pt_widget *view, uint32_t row,
                                            void *user_data);

typedef void (*pt_list_bind_callback)(pt_widget *list, pt_widget *row, uint32_t index,
                                      void *user_data);

/* Since 1.0. */
pt_api pt_widget *pt_list_view_create(pt_widget *parent);
/* Since 1.0. */
pt_api void pt_list_view_set_row_count(pt_widget *list, uint32_t count);
/* Since 1.0. */
pt_api uint32_t pt_list_view_get_row_count(const pt_widget *list);
/* Since 1.0. */
pt_api void pt_list_view_set_row_height(pt_widget *list, float height);
/* Since 1.0. */
pt_api float pt_list_view_get_row_height(const pt_widget *list);
/* Since 1.0. */
pt_api void pt_list_view_set_on_bind_row(pt_widget *list, pt_list_bind_callback callback,
                                         void *user_data);
/* Since 1.0. */
pt_api void pt_list_view_set_on_select(pt_widget *list, pt_index_callback callback,
                                       void *user_data);
/* Since 1.0. */
pt_api void pt_list_view_set_on_activate(pt_widget *list, pt_index_callback callback,
                                         void *user_data);
/* Since 1.0. */
pt_api uint32_t pt_list_view_get_selected(const pt_widget *list);
/* Since 1.0. */
pt_api void pt_list_view_set_selected(pt_widget *list, uint32_t index);
/* Since 1.0. */
pt_api void pt_list_view_refresh(pt_widget *list);
/* Rebinds only the rows in [first, first + count) on the next layout. Rows
 * outside the range keep their existing bindings; no pool is rebuilt. Since 1.0. */
pt_api void pt_list_view_refresh_rows(pt_widget *list, uint32_t first, uint32_t count);
/* Since 1.0. */
pt_api void pt_list_view_scroll_to_row(pt_widget *list, uint32_t index);
/* Since 1.0. */
pt_api void pt_list_view_set_row_key_callback(pt_widget *list,
                                              pt_row_key_callback callback,
                                              void *user_data);
/* Since 1.0. */
pt_api uint64_t pt_list_view_get_selected_key(const pt_widget *list);
/* Since 1.0. */
pt_api bool pt_list_view_set_selected_key(pt_widget *list, uint64_t key);
/* Since 1.0. */
pt_api void pt_list_view_set_row_text_callback(pt_widget *list,
                                               pt_row_text_callback callback,
                                               void *user_data);

/* Multi-row selection, mirroring the table API. The primary row stays
 * available through pt_list_view_get_selected; the set helpers add or remove
 * rows without disturbing the rest of the selection. Range selection is by
 * model index, and a Ctrl-click or pt_list_view_select_toggle adds a row. Since 1.0. */
pt_api uint32_t pt_list_view_get_selection_count(const pt_widget *list);
/* Since 1.0. */
pt_api bool pt_list_view_is_row_selected(const pt_widget *list, uint32_t row);
/* Since 1.0. */
pt_api uint32_t pt_list_view_get_selection(const pt_widget *list, uint32_t *rows,
                                           uint32_t capacity);
/* Since 1.0. */
pt_api void pt_list_view_select_range(pt_widget *list, uint32_t first, uint32_t last);
/* Since 1.0. */
pt_api void pt_list_view_select_toggle(pt_widget *list, uint32_t row);
/* Since 1.0. */
pt_api void pt_list_view_clear_selection(pt_widget *list);

/* Built-in vector icons. Glyphs are drawn from a 24x24 outline and stay crisp
 * at any scale. Icon widgets size themselves to pt_icon_set_size (20 px by
 * default). The same glyph can decorate buttons and table cells through
 * pt_widget_set_icon. Icon-only buttons take an accessible name and show it
 * as a tooltip. */
typedef enum pt_icon {
    pt_icon_none = 0,
    pt_icon_chevron_right,
    pt_icon_chevron_down,
    pt_icon_chevron_left,
    pt_icon_chevron_up,
    pt_icon_search,
    pt_icon_menu,
    pt_icon_close,
    pt_icon_check,
    pt_icon_folder,
    pt_icon_document,
    pt_icon_plus,
    pt_icon_settings,
    pt_icon_home,
    pt_icon_bell,
    pt_icon_user,
    pt_icon_refresh,
    pt_icon_trash,
    pt_icon_arrow_right,
    pt_icon_arrow_left,
    pt_icon_info,
    pt_icon_warning
} pt_icon;

/* Since 1.0. */
pt_api void pt_chip_set_icon(pt_widget *chip, pt_icon icon);

/* Since 1.0. */
pt_api pt_widget *pt_icon_create(pt_widget *parent, pt_icon icon);
/* Since 1.0. */
pt_api void pt_widget_set_icon(pt_widget *widget, pt_icon icon);
/* Since 1.0. */
pt_api pt_icon pt_widget_get_icon(const pt_widget *widget);
/* Since 1.0. */
pt_api void pt_icon_set_size(pt_widget *icon, float size);
/* Since 1.0. */
pt_api float pt_icon_get_size(const pt_widget *icon);
/* Since 1.0. */
pt_api pt_widget *pt_icon_button_create(pt_widget *parent, pt_icon icon,
                                        const char *accessible_name);

/* Virtualized table. Columns own their titles and widths; rows and cells are
 * materialized per viewport and recycled like list rows. Bind fills a reusable
 * cell for a row/column pair; use pt_widget_set_text for the built-in renderer,
 * or set colors/icons and child widgets for richer cells. Clicking a resizable
 * header edge starts a column resize; clicking a header cycles its sort
 * direction and calls the sort callback. Pt does not reorder data itself:
 * update the model and call pt_table_refresh from the callback. */
#define pt_table_column_none ((uint32_t)-1)

/* Column limits shared by the layout model and the persistence blob. A table
 * clamps pt_table_set_column_count to PT_TABLE_MAX_COLUMNS, a column key is
 * truncated to PT_TABLE_COLUMN_KEY_MAX bytes, and a serialized layout never
 * exceeds PT_TABLE_COLUMN_LAYOUT_MAX bytes. */
#define PT_TABLE_MAX_COLUMNS 1024u
#define PT_TABLE_COLUMN_KEY_MAX 64u
#define PT_TABLE_COLUMN_LAYOUT_MAX 262144u

typedef enum pt_sort_direction {
    pt_sort_none = 0,
    pt_sort_ascending,
    pt_sort_descending
} pt_sort_direction;

typedef void (*pt_table_bind_cell_callback)(pt_widget *table, pt_widget *cell, uint32_t row,
                                            uint32_t column, void *user_data);
typedef void (*pt_table_sort_callback)(pt_widget *table, uint32_t column,
                                       pt_sort_direction direction, void *user_data);

/* Multi-column sorting. Columns are ordered by priority: the first entry is
 * the primary key. Pt does not reorder rows; applications read the full sort
 * state in the callback, update their model and call pt_table_refresh. */
#define PT_TABLE_MAX_SORT_COLUMNS 4

typedef void (*pt_table_sort_columns_callback)(pt_widget *table, uint32_t count,
                                               const uint32_t *columns,
                                               const pt_sort_direction *directions,
                                               void *user_data);

/* Since 1.0. */
pt_api pt_widget *pt_table_create(pt_widget *parent);
/* Since 1.0. */
pt_api void pt_table_set_column_count(pt_widget *table, uint32_t count);
/* Since 1.0. */
pt_api uint32_t pt_table_get_column_count(const pt_widget *table);
/* Since 1.0. */
pt_api void pt_table_set_column_title(pt_widget *table, uint32_t column, const char *title);
/* Since 1.0. */
pt_api void pt_table_set_column_width(pt_widget *table, uint32_t column, float width);
/* Since 1.0. */
pt_api float pt_table_get_column_width(const pt_widget *table, uint32_t column);
/* Since 1.0. */
pt_api void pt_table_set_column_align(pt_widget *table, uint32_t column, pt_text_align align);
/* Since 1.0. */
pt_api void pt_table_set_column_resizable(pt_widget *table, uint32_t column, bool resizable);
/* Hidden columns keep their width, title and cells; they are skipped by
 * layout, painting, hit-testing and accessibility. Since 1.0. */
pt_api void pt_table_set_column_hidden(pt_widget *table, uint32_t column, bool hidden);
/* Since 1.0. */
pt_api bool pt_table_get_column_hidden(const pt_widget *table, uint32_t column);
/* Since 1.0. */
pt_api void pt_table_set_row_count(pt_widget *table, uint32_t count);
/* Since 1.0. */
pt_api uint32_t pt_table_get_row_count(const pt_widget *table);
/* Since 1.0. */
pt_api void pt_table_set_row_height(pt_widget *table, float height);
/* Since 1.0. */
pt_api float pt_table_get_row_height(const pt_widget *table);
/* Since 1.0. */
pt_api void pt_table_set_header_height(pt_widget *table, float height);
/* Since 1.0. */
pt_api void pt_table_set_on_bind_cell(pt_widget *table, pt_table_bind_cell_callback callback,
                                      void *user_data);
/* Since 1.0. */
pt_api void pt_table_set_on_select(pt_widget *table, pt_index_callback callback,
                                   void *user_data);
/* Since 1.0. */
pt_api void pt_table_set_on_activate(pt_widget *table, pt_index_callback callback,
                                     void *user_data);
/* Since 1.0. */
pt_api void pt_table_set_on_sort(pt_widget *table, pt_table_sort_callback callback,
                                 void *user_data);
/* Since 1.0. */
pt_api void pt_table_set_on_sort_columns(pt_widget *table,
                                         pt_table_sort_columns_callback callback,
                                         void *user_data);
/* Since 1.0. */
pt_api pt_sort_direction pt_table_get_sort(const pt_widget *table, uint32_t *column);
/* Since 1.0. */
pt_api void pt_table_set_sort(pt_widget *table, uint32_t column, pt_sort_direction direction);
/* Since 1.0. */
pt_api void pt_table_set_sort_columns(pt_widget *table, uint32_t count,
                                      const uint32_t *columns,
                                      const pt_sort_direction *directions);
/* Since 1.0. */
pt_api uint32_t pt_table_get_sort_columns(const pt_widget *table, uint32_t *columns,
                                          pt_sort_direction *directions);
/* Since 1.0. */
pt_api uint32_t pt_table_get_selected(const pt_widget *table);
/* Since 1.0. */
pt_api void pt_table_set_selected(pt_widget *table, uint32_t row);
/* Since 1.0. */
pt_api void pt_table_set_row_key_callback(pt_widget *table, pt_row_key_callback callback,
                                          void *user_data);
/* Since 1.0. */
pt_api uint64_t pt_table_get_selected_key(const pt_widget *table);
/* Since 1.0. */
pt_api bool pt_table_set_selected_key(pt_widget *table, uint64_t key);
/* Since 1.0. */
pt_api void pt_table_set_row_text_callback(pt_widget *table,
                                           pt_row_text_callback callback,
                                           void *user_data);
/* Since 1.0. */
pt_api void pt_table_refresh(pt_widget *table);
/* Rebinds only the visible rows intersecting [first, first + count) without
 * rebuilding the row pool. Use for single-row or bulk model edits. Since 1.0. */
pt_api void pt_table_refresh_rows(pt_widget *table, uint32_t first, uint32_t count);
/* Since 1.0. */
pt_api void pt_table_scroll_to_row(pt_widget *table, uint32_t row);
/* Since 1.0. */
pt_api void pt_table_scroll_to_column(pt_widget *table, uint32_t column);

/* In-place editing. Begin creates an entry over the visible cell, routed
 * through the normal entry editing and undo path; Enter or focus loss commits
 * and Escape cancels. The callback receives the committed UTF-8 text. */
typedef void (*pt_table_cell_edited_callback)(pt_widget *table, uint32_t row,
                                              uint32_t column, const char *text,
                                              void *user_data);

/* Since 1.0. */
pt_api void pt_table_set_editable(pt_widget *table, bool editable);
/* Since 1.0. */
pt_api bool pt_table_get_editable(const pt_widget *table);
/* Since 1.0. */
pt_api bool pt_table_begin_edit(pt_widget *table, uint32_t row, uint32_t column);
/* Since 1.0. */
pt_api bool pt_table_commit_edit(pt_widget *table);
/* Since 1.0. */
pt_api void pt_table_cancel_edit(pt_widget *table);
/* Since 1.0. */
pt_api bool pt_table_is_editing(const pt_widget *table);
/* Since 1.0. */
pt_api void pt_table_set_on_cell_edited(pt_widget *table,
                                        pt_table_cell_edited_callback callback,
                                        void *user_data);

/* Multi-row selection. The primary row stays available through
 * pt_table_get_selected; the range helpers extend the highlighted set while
 * shift-clicking and shift-dragging rows. Since 1.0. */
pt_api uint32_t pt_table_get_selection_count(const pt_widget *table);
/* Since 1.0. */
pt_api bool pt_table_is_row_selected(const pt_widget *table, uint32_t row);
/* Since 1.0. */
pt_api uint32_t pt_table_get_selection(const pt_widget *table, uint32_t *rows,
                                       uint32_t capacity);
/* Since 1.0. */
pt_api void pt_table_select_range(pt_widget *table, uint32_t first, uint32_t last);
/* Since 1.0. */
pt_api void pt_table_select_toggle(pt_widget *table, uint32_t row);
/* Since 1.0. */
pt_api void pt_table_clear_selection(pt_widget *table);

/* Column reordering and row drag-out. Reordering permutes widths, titles,
 * alignment and visibility; the callback reports the old and new indices so
 * the application can apply the same permutation to its model. The row drag
 * callback fires once the pointer moves past the drag threshold and can call
 * pt_window_begin_drag while the implicit grab is still active. */
typedef void (*pt_table_columns_reordered_callback)(pt_widget *table, uint32_t from,
                                                    uint32_t to, void *user_data);
typedef void (*pt_table_row_drag_callback)(pt_widget *table, uint32_t row,
                                           void *user_data);

/* Since 1.0. */
pt_api void pt_table_set_columns_reorderable(pt_widget *table, bool reorderable);
/* Since 1.0. */
pt_api bool pt_table_get_columns_reorderable(const pt_widget *table);
/* Since 1.0. */
pt_api bool pt_table_move_column(pt_widget *table, uint32_t from, uint32_t to);
/* Since 1.0. */
pt_api void pt_table_set_on_columns_reordered(
    pt_widget *table, pt_table_columns_reordered_callback callback, void *user_data);
/* Since 1.0. */
pt_api void pt_table_set_on_row_drag(pt_widget *table, pt_table_row_drag_callback callback,
                                     void *user_data);

/* Column identity and layout persistence, since 1.2. A column key is an
 * application-owned stable identifier (UTF-8, at most PT_TABLE_COLUMN_KEY_MAX
 * bytes, truncated with a warning when longer). Keys are matched across
 * reorders and saved layouts; an empty key means "match by position". Keys
 * must be unique among non-empty keys, so setting a key already used by
 * another column is rejected with a warning and leaves that column unchanged.
 * pt_table_get_column_key never returns NULL and the returned string is owned
 * by the table and invalidated by the next key change or column move. Since 1.2. */
pt_api void pt_table_set_column_key(pt_widget *table, uint32_t column, const char *key);
/* Since 1.2. */
pt_api const char *pt_table_get_column_key(const pt_widget *table, uint32_t column);

/* Versioned column-layout persistence. pt_table_columns_save serializes the
 * current display order plus each column's width, hidden flag and the
 * multi-column sort state. Titles, alignment and resizability are model
 * configuration and are not saved. The result is a NUL-terminated ASCII blob
 * owned by the caller (release with pt_free), or NULL when the table is not a
 * table, a column width is not finite/positive/representable, or the blob
 * would exceed PT_TABLE_COLUMN_LAYOUT_MAX. The application decides where to
 * store it (file, configuration, database); Pt never touches the filesystem
 * here. The format is:
 *
 *     PTCOLS 1
 *     C <count>
 *     <key> <width> <hidden>        count times, in display order
 *     S <sort_count>
 *     <ordinal> <direction>         sort_count times
 *
 * <key> is '-' for a column without a key, otherwise the key bytes as
 * lowercase hex (empty keys are matched by position). <width> is the width in
 * thousandths of a logical pixel (120000 is 120 px) and must be positive;
 * values below the implementation minimum are clamped on restore. <hidden> is
 * 0 or 1. <ordinal> indexes the column lines above (so sorting follows a
 * column across reorders) and <direction> is 1 (ascending) or 2 (descending).
 * A later version may append fields, but the version number changes and older
 * readers reject it.
 *
 * pt_table_columns_restore validates the whole blob before touching the table.
 * It returns false for a NULL table or blob, an oversized blob, an unknown
 * magic or version, a truncated or malformed line, a non-finite or
 * non-positive width, an unknown hidden flag, a bad key, more than
 * PT_TABLE_MAX_COLUMNS columns, more than PT_TABLE_MAX_SORT_COLUMNS sort
 * entries, a duplicate sort ordinal, a direction other than ascending or
 * descending, or trailing junk. Column
 * matching is strict: the saved count must equal the current column count;
 * each non-empty saved key must match exactly one current column; each empty
 * saved key is matched by its display position. A missing or duplicate key,
 * or a mixed set that cannot form a permutation, rejects the blob. On failure
 * the current layout is untouched (atomic); on success the saved widths,
 * visibility, order and sort state are applied and the table rebinds. Since 1.2. */
pt_api char *pt_table_columns_save(const pt_widget *table);
/* Since 1.2. */
pt_api bool pt_table_columns_restore(pt_widget *table, const char *blob);

/* Hierarchical tree view. Nodes are owned by the tree; handles stay valid
 * until the node, its ancestor or the tree is cleared. Only expanded branches
 * are materialized, and rows are virtualized and recycled. Callbacks receive
 * the stable node pointer, so applications can attach arbitrary model data. */
typedef struct pt_tree_node pt_tree_node;

typedef void (*pt_tree_node_callback)(pt_widget *tree, pt_tree_node *node, void *user_data);

/* Since 1.0. */
pt_api pt_widget *pt_tree_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_tree_node *pt_tree_add_node(pt_widget *tree, pt_tree_node *parent_node,
                                      const char *text);
/* Since 1.0. */
pt_api void pt_tree_clear(pt_widget *tree);
/* Since 1.0. */
pt_api uint32_t pt_tree_get_root_count(const pt_widget *tree);
/* Since 1.0. */
pt_api pt_tree_node *pt_tree_get_root_node(const pt_widget *tree, uint32_t index);
/* Since 1.0. */
pt_api const char *pt_tree_node_get_text(const pt_tree_node *node);
/* Since 1.0. */
pt_api void pt_tree_node_set_text(pt_tree_node *node, const char *text);
/* Since 1.0. */
pt_api bool pt_tree_node_get_expanded(const pt_tree_node *node);
/* Since 1.0. */
pt_api void pt_tree_node_set_expanded(pt_tree_node *node, bool expanded);
/* Since 1.0. */
pt_api bool pt_tree_node_has_children(const pt_tree_node *node);
/* Since 1.0. */
pt_api uint32_t pt_tree_node_get_depth(const pt_tree_node *node);
/* Since 1.0. */
pt_api pt_tree_node *pt_tree_node_parent(const pt_tree_node *node);
/* Since 1.0. */
pt_api uint32_t pt_tree_node_get_child_count(const pt_tree_node *node);
/* Since 1.0. */
pt_api pt_tree_node *pt_tree_node_get_child(const pt_tree_node *node, uint32_t index);
/* Since 1.0. */
pt_api void *pt_tree_node_get_data(const pt_tree_node *node);
/* Since 1.0. */
pt_api void pt_tree_node_set_data(pt_tree_node *node, void *data);
/* Since 1.0. */
pt_api uint32_t pt_tree_get_visible_count(const pt_widget *tree);
/* Since 1.0. */
pt_api pt_tree_node *pt_tree_get_visible_node(const pt_widget *tree, uint32_t row);
/* Since 1.0. */
pt_api pt_tree_node *pt_tree_get_selected(const pt_widget *tree);
/* Since 1.0. */
pt_api void pt_tree_set_selected(pt_widget *tree, pt_tree_node *node);
/* Since 1.0. */
pt_api void pt_tree_set_row_height(pt_widget *tree, float height);
/* Since 1.0. */
pt_api void pt_tree_set_indent(pt_widget *tree, float indent);
/* Since 1.0. */
pt_api void pt_tree_set_on_select(pt_widget *tree, pt_tree_node_callback callback,
                                  void *user_data);
/* Since 1.0. */
pt_api void pt_tree_set_on_activate(pt_widget *tree, pt_tree_node_callback callback,
                                    void *user_data);
/* Since 1.0. */
pt_api void pt_tree_refresh(pt_widget *tree);
/* Since 1.0. */
pt_api void pt_tree_scroll_to_node(pt_widget *tree, pt_tree_node *node);

/* Multi-node selection, mirroring the list and table APIs. Selection lives on
 * the nodes, so it survives expand, collapse and refresh; a node that is
 * removed or cleared simply stops being selected. The primary node stays
 * available through pt_tree_get_selected, and range selection works on
 * visible row indices because collapsed branches have no rows. Since 1.0. */
pt_api uint32_t pt_tree_get_selection_count(const pt_widget *tree);
/* Since 1.0. */
pt_api bool pt_tree_is_selected(const pt_widget *tree, const pt_tree_node *node);
/* Since 1.0. */
pt_api uint32_t pt_tree_get_selection(const pt_widget *tree, pt_tree_node **nodes,
                                      uint32_t capacity);
/* Since 1.0. */
pt_api void pt_tree_select_range(pt_widget *tree, uint32_t first_row, uint32_t last_row);
/* Since 1.0. */
pt_api void pt_tree_select_toggle(pt_widget *tree, pt_tree_node *node);
/* Since 1.0. */
pt_api void pt_tree_clear_selection(pt_widget *tree);

/* Inline renaming. begin_rename opens an editor over the node's label and
 * returns false when the node is not visible. F2 and a slow double click
 * start renaming on a renamable tree; focus loss commits, Escape cancels. */
typedef void (*pt_tree_rename_callback)(pt_widget *tree, pt_tree_node *node,
                                        const char *text, void *user_data);

/* Since 1.0. */
pt_api void pt_tree_set_renamable(pt_widget *tree, bool renamable);
/* Since 1.0. */
pt_api bool pt_tree_get_renamable(const pt_widget *tree);
/* Since 1.0. */
pt_api bool pt_tree_begin_rename(pt_widget *tree, pt_tree_node *node);
/* Since 1.0. */
pt_api bool pt_tree_commit_rename(pt_widget *tree);
/* Since 1.0. */
pt_api void pt_tree_cancel_rename(pt_widget *tree);
/* Since 1.0. */
pt_api bool pt_tree_is_renaming(const pt_widget *tree);
/* Since 1.0. */
pt_api void pt_tree_set_on_rename(pt_widget *tree, pt_tree_rename_callback callback,
                                  void *user_data);

/* Tabbed navigation. Pages are real child widgets owned by the tabs widget.
 * The strip scrolls horizontally with overflow arrows, and the active
 * indicator glides between tabs unless reduced motion is enabled. Since 1.0. */
pt_api pt_widget *pt_tabs_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_tabs_add_page(pt_widget *tabs, const char *title);
/* Since 1.0. */
pt_api void pt_tabs_remove_page(pt_widget *tabs, uint32_t index);
/* Since 1.0. */
pt_api uint32_t pt_tabs_get_page_count(const pt_widget *tabs);
/* Since 1.0. */
pt_api pt_widget *pt_tabs_get_page(const pt_widget *tabs, uint32_t index);
/* Since 1.0. */
pt_api void pt_tabs_set_page_title(pt_widget *tabs, uint32_t index, const char *title);
/* Since 1.0. */
pt_api const char *pt_tabs_get_page_title(const pt_widget *tabs, uint32_t index);
/* Since 1.0. */
pt_api uint32_t pt_tabs_get_active(const pt_widget *tabs);
/* Since 1.0. */
pt_api void pt_tabs_set_active(pt_widget *tabs, uint32_t index);
/* Since 1.0. */
pt_api void pt_tabs_set_on_change(pt_widget *tabs, pt_index_callback callback,
                                  void *user_data);
/* Since 1.0. */
pt_api void pt_tabs_set_strip_height(pt_widget *tabs, float height);

/* Navigation sidebar. Items are real child widgets with an icon and label;
 * compact mode hides labels and centers icons while keeping accessible names.
 * The active item gets a rounded selection pill and an accent rail. Since 1.0. */
pt_api pt_widget *pt_sidebar_create(pt_widget *parent);
/* Since 1.0. */
pt_api pt_widget *pt_sidebar_add_item(pt_widget *sidebar, pt_icon icon, const char *label);
/* Since 1.0. */
pt_api uint32_t pt_sidebar_get_item_count(const pt_widget *sidebar);
/* Since 1.0. */
pt_api pt_widget *pt_sidebar_get_item(const pt_widget *sidebar, uint32_t index);
/* Since 1.0. */
pt_api uint32_t pt_sidebar_get_active(const pt_widget *sidebar);
/* Since 1.0. */
pt_api void pt_sidebar_set_active(pt_widget *sidebar, uint32_t index);
/* Since 1.0. */
pt_api bool pt_sidebar_get_compact(const pt_widget *sidebar);
/* Since 1.0. */
pt_api void pt_sidebar_set_compact(pt_widget *sidebar, bool compact);
/* Since 1.0. */
pt_api void pt_sidebar_set_header(pt_widget *sidebar, const char *text);
/* Since 1.0. */
pt_api const char *pt_sidebar_get_header(const pt_widget *sidebar);
/* Since 1.0. */
pt_api void pt_sidebar_set_on_select(pt_widget *sidebar, pt_index_callback callback,
                                     void *user_data);

/* Combo box. A field that owns a popup list of choices, built from the same
 * overlay and list primitives as the rest of the toolkit rather than a second
 * widget system. pt_combobox_select_only shows the chosen label and only
 * accepts a choice; pt_combobox_filterable puts a real editor in the field,
 * narrows the popup as the user types and matches typed prefixes from the
 * keyboard. An empty selection reports pt_list_index_none. The popup flips
 * above the field when the bottom of the window is too close, opens as a
 * native surface where the compositor allows it and falls back to an
 * in-window panel under PT_HEADLESS. Item data pointers are borrowed: the
 * application keeps them alive for the lifetime of the item. */
typedef enum pt_combobox_mode {
    pt_combobox_select_only = 0,
    pt_combobox_filterable
} pt_combobox_mode;

/* Reports the index chosen by pt_combobox_set_on_change, or the field text
 * after every keystroke that changes it in a filterable box. */
typedef void (*pt_combobox_text_callback)(pt_widget *combobox, const char *text,
                                          void *user_data);

/* Since 1.0. */
pt_api pt_widget *pt_combobox_create(pt_widget *parent);
/* Since 1.0. */
pt_api void pt_combobox_add_item(pt_widget *combobox, const char *text);
/* Since 1.0. */
pt_api void pt_combobox_insert_item(pt_widget *combobox, uint32_t index, const char *text);
/* Since 1.0. */
pt_api void pt_combobox_remove_item(pt_widget *combobox, uint32_t index);
/* Since 1.0. */
pt_api void pt_combobox_clear(pt_widget *combobox);
/* Since 1.0. */
pt_api uint32_t pt_combobox_get_item_count(const pt_widget *combobox);
/* Since 1.0. */
pt_api const char *pt_combobox_get_item_text(const pt_widget *combobox, uint32_t index);
/* Since 1.0. */
pt_api void pt_combobox_set_item_text(pt_widget *combobox, uint32_t index, const char *text);
/* Since 1.0. */
pt_api void pt_combobox_set_item_data(pt_widget *combobox, uint32_t index, void *data);
/* Since 1.0. */
pt_api void *pt_combobox_get_item_data(const pt_widget *combobox, uint32_t index);
/* Since 1.0. */
pt_api void pt_combobox_set_item_disabled(pt_widget *combobox, uint32_t index, bool disabled);
/* Since 1.0. */
pt_api bool pt_combobox_get_item_disabled(const pt_widget *combobox, uint32_t index);
/* Since 1.0. */
pt_api void pt_combobox_set_mode(pt_widget *combobox, pt_combobox_mode mode);
/* Since 1.0. */
pt_api pt_combobox_mode pt_combobox_get_mode(const pt_widget *combobox);
/* Since 1.0. */
pt_api uint32_t pt_combobox_get_selected(const pt_widget *combobox);
/* Selecting an index also puts its label in the field; passing
 * pt_list_index_none clears both. Setting a selection does not fire
 * pt_combobox_set_on_change, matching every other set_selected in the API.
 * Since 1.0. */
pt_api void pt_combobox_set_selected(pt_widget *combobox, uint32_t index);
/* Since 1.0. */
pt_api const char *pt_combobox_get_text(const pt_widget *combobox);
/* Since 1.0. */
pt_api void pt_combobox_set_text(pt_widget *combobox, const char *text);
/* Since 1.0. */
pt_api const char *pt_combobox_get_placeholder(const pt_widget *combobox);
/* Since 1.0. */
pt_api void pt_combobox_set_placeholder(pt_widget *combobox, const char *text);
/* Since 1.0. */
pt_api void pt_combobox_open(pt_widget *combobox);
/* Since 1.0. */
pt_api void pt_combobox_close(pt_widget *combobox);
/* Since 1.0. */
pt_api bool pt_combobox_is_open(const pt_widget *combobox);
/* Since 1.0. */
pt_api void pt_combobox_set_popup_max_height(pt_widget *combobox, float height);
/* Since 1.0. */
pt_api float pt_combobox_get_popup_max_height(const pt_widget *combobox);
/* Since 1.0. */
pt_api void pt_combobox_set_on_change(pt_widget *combobox, pt_index_callback callback,
                                      void *user_data);
/* Since 1.0. */
pt_api void pt_combobox_set_on_text_change(pt_widget *combobox,
                                           pt_combobox_text_callback callback,
                                           void *user_data);

#ifdef __cplusplus
}
#endif

#endif
