# Pt quickstart

English | [简体中文](zh-CN/quickstart.md)

This guide covers the C API. The same object model is exposed by the C++,
Python and Java bindings; see [bindings.md](bindings.md).

## Hello, Pt

```c
#include <pt/pt.h>

#include <stdio.h>

static void on_quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.hello");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }

    pt_window *window = pt_window_create(app, "Hello, Pt", 640, 440);
    pt_widget *root = pt_window_root(window);
    pt_widget_set_padding(root, 32, 32, 32, 32);

    pt_widget *card = pt_card_create(root);
    pt_widget_set_gap(card, 16);

    pt_widget *title = pt_label_create(card, "Hello, Pt");
    pt_widget_set_font_size(title, 32);
    pt_widget_set_font_weight(title, pt_font_weight_semibold);

    pt_widget *quit = pt_button_create(card, "Quit");
    pt_button_set_on_click(quit, on_quit, app);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

```sh
cc hello.c $(pkg-config --cflags --libs pt) -o hello
./hello
```

`pt_app_create` connects to the compositor unless `PT_HEADLESS=1` is set and
owns all windows created from it. `pt_app_run` returns when the last window is
closed, when `pt_app_quit` is called, or on a connection failure.

## Colors and the theme

Public colors are packed as `0xRRGGBBAA`. Helpers make this less error-prone:

```c
pt_color accent = pt_rgb(0x7AA2F7);          /* opaque */
pt_color veil   = pt_rgba(0, 0, 0, 128);     /* 50% black */
pt_color dim    = pt_color_alpha(accent, 96);
```

The default theme follows the app-level color scheme and accent color:

```c
pt_app_set_color_scheme(app, pt_color_scheme_dark);
pt_app_set_accent_color(app, pt_rgb(0x9ECE6A));
pt_app_set_theme_color(app, pt_theme_surface, pt_rgb(0x10151E));
pt_app_set_font_family(app, "Inter");
```

`pt_theme_accent` and the derived `accent_hover`, `accent_text`,
`accent_soft`, `focus` and `selection` tokens follow the accent color. The
The control tokens are `pt_theme_control` and `pt_theme_control_hover` for
input surfaces, `pt_theme_track` for switch and slider grooves,
`pt_theme_surface_sunken` for recessed areas and `pt_theme_accent_soft` for
tonal buttons. The 0.9 set adds container, outline, pressed and soft-status
roles — `pt_theme_surface_container`, `pt_theme_surface_container_high`,
`pt_theme_outline`, `pt_theme_outline_variant`, `pt_theme_text_disabled`,
`pt_theme_accent_pressed`, `pt_theme_control_pressed`, `pt_theme_danger_soft`,
`pt_theme_success_soft`, `pt_theme_warning_soft`, `pt_theme_scrim`,
`pt_theme_skeleton`, `pt_theme_skeleton_highlight` and
`pt_theme_selection_text`. Read any token with `pt_app_get_theme_color`.

Geometry is tokenized too. `pt_app_get_theme_metric` and
`pt_app_set_theme_metric` read and write the ten metrics that control the
look: `pt_theme_metric_radius_small`, `_radius_medium`, `_radius_large`,
`_radius_pill`, `_focus_width`, `_focus_offset`, `_elevation_low`,
`_elevation_high`, `_chip_height` and `_control_height`.

A window can override individual tokens without changing the app theme, which
is how a dialog, a palette-heavy panel or a brand-tinted window opts out of the
global colors:

```c
pt_window_set_theme_color(window, pt_theme_background, pt_rgb(0x10151E));
pt_window_set_theme_metric(window, pt_theme_metric_radius_medium, 12.0f);
pt_color background = pt_window_get_theme_color(window, pt_theme_background);
pt_window_clear_theme_overrides(window); /* back to app defaults */
```

Overrides resolve at paint time; unset tokens fall through to the app theme.

Widget-level colors override the theme. Theme changes request a redraw of open
windows automatically. [theming.md](theming.md) has the complete token and
metric table, the accent derivation formulas, the dark/light pairing rules and
the contrast floors asserted by `test_theme`.

## Layout

The window root is a vertical column. `pt_box_create` creates either a row or a
column; `pt_row_create` and `pt_column_create` are shorthands. Layout is a flex
subset:

```c
pt_widget *row = pt_row_create(parent);
pt_widget_set_gap(row, 12);
pt_box_set_align(row, pt_align_center);       /* cross axis */
pt_box_set_justify(row, pt_justify_space_between); /* main axis */

pt_widget *grow = pt_label_create(row, "Takes the free space");
pt_widget_set_grow(grow, 1);

pt_widget *fixed = pt_button_create(row, "Fixed");
pt_widget_set_size(fixed, 120, 36);
```

- Width/height and min/max sizes are logical pixels. `pt_auto` (a negative
  value) requests content-based sizing.
- `pt_widget_set_margin` adds space outside a widget;
  `pt_widget_set_padding` adds space inside a container.
- `pt_widget_set_align_self` overrides the container's cross-axis alignment for
  one child. `pt_align_stretch` is the default for containers.
- Main-axis justification supports start, center, end, space-between,
  space-around and space-evenly.
- `pt_scroll_create` creates a clipped vertical viewport. Wheel events scroll
  it; content taller than the viewport gets a Pt-drawn scrollbar.
  `pt_scroll_set_offset`, `pt_scroll_get_offset` and
  `pt_scroll_get_max_offset` expose the logical-pixel offset; setters clamp to
  the laid-out content.
- `pt_spacer_create` is an empty, growing spacer useful for pushing content to
  the end of a row or column.

### Responsive containers

`pt_wrap_create` lays children into rows that fit the available width.
`pt_grid_create(parent, columns)` divides the width into equal columns. Both
take independent row and column gaps and reflow when the window resizes:

```c
pt_widget *tags = pt_wrap_create(card);
pt_wrap_set_gap(tags, 6, 6); /* row gap, column gap */

pt_widget *chip = pt_label_create(tags, "design");
pt_widget_set_padding(chip, 4, 10, 4, 10);
pt_widget_set_align_self(chip, pt_align_start); /* keep intrinsic width */

pt_widget *stats = pt_grid_create(card, 3);
pt_grid_set_gap(stats, 8, 8);

pt_widget *wide = pt_card_create(stats);
pt_widget_set_grid_span(wide, 2); /* next cell starts in column 3 */
```

- Wrap measures each child and starts a new line when the next child no longer
  fits. Main-axis justification applies per line, so `space-between` spreads
  the final row too.
- Grid cells share the row height of their tallest child. `pt_widget_set_grid_span`
  defaults to 1 and clamps to the column count; a span that does not fit starts
  the next row.
- Both containers default to `pt_align_stretch` on the cross axis. Use
  `pt_widget_set_align_self(child, pt_align_start)` for chips, buttons or other
  intrinsic-width children instead of relying on their requested size.

Widgets are appended to their parent in creation order. `pt_widget_destroy`
removes a subtree. Prefer destroying and recreating a subtree over mutating
arrays of child pointers while callbacks are active.

## Widgets and callbacks

Available widgets: box/row/column, label, button, entry, combobox, text area,
toggle, checkbox, radio, slider, progress, spinner, segmented control, chip,
skeleton, scroll view, virtualized list, table, tree, tabs, sidebar, icon, icon
button, wrap, grid, divider, card and spacer.

```c
static void on_click(pt_widget *button, void *data) {
    counter *state = data;
    ++state->count;
    char text[32];
    snprintf(text, sizeof(text), "%d", state->count);
    pt_widget_set_text(state->value, text);
}

static void on_text(pt_widget *entry, const char *text, void *data) {
    (void)entry;
    (void)data;
    printf("entry: %s\n", text);
}

static void on_submit(pt_widget *entry, void *data) {
    (void)entry;
    (void)data;
    puts("submitted");
}

static void on_toggle(pt_widget *toggle, float checked, void *data) {
    (void)toggle;
    (void)data;
    printf("toggle: %s\n", checked > 0.5f ? "on" : "off");
}

static void on_slider(pt_widget *slider, float value, void *data) {
    (void)slider;
    (void)data;
    printf("slider: %.2f\n", value);
}

pt_widget *button = pt_button_create(card, "Count");
pt_button_set_on_click(button, on_click, &state);

pt_widget *entry = pt_entry_create(card, "Type here");
pt_entry_set_on_change(entry, on_text, NULL);
pt_entry_set_on_submit(entry, on_submit, NULL);

pt_widget *toggle = pt_toggle_create(card);
pt_toggle_set_checked(toggle, true);
pt_toggle_set_on_change(toggle, on_toggle, NULL);

pt_widget *slider = pt_slider_create(card);
pt_slider_set_value(slider, 0.4f);
pt_slider_set_on_change(slider, on_slider, NULL);
```

Button variants are `pt_button_primary`, `pt_button_secondary`,
`pt_button_ghost`, `pt_button_danger` and `pt_button_soft` (a tonal accent
surface). Slider and progress values are in `[0, 1]`. Callbacks receive the
widget and the `user_data` pointer registered with the setter. `user_data` and
anything it points to must remain valid until the callback is replaced or the
app is destroyed.

Password entries replace their contents with bullets:

```c
pt_entry_set_password(entry, true);
```

## Modern controls

Checkboxes, radios and toggles share one checked-state API. Radios are
mutually exclusive with siblings under the same parent:

```c
static void on_checked(pt_widget *widget, float checked, void *data) {
    (void)widget;
    (void)data;
    printf("%s\n", checked > 0.5f ? "on" : "off");
}

pt_widget *subscribe = pt_checkbox_create(card, "Subscribe to notes");
pt_widget_set_checked(subscribe, true);
pt_widget_set_on_checked(subscribe, on_checked, NULL);

pt_widget *comfort = pt_radio_create(card, "Comfort");
pt_radio_create(card, "Compact");
pt_widget_set_checked(comfort, true);
```

Progress bars animate on their own when indeterminate; spinners always animate
while visible:

```c
pt_widget *upload = pt_progress_create(card);
pt_progress_set_value(upload, 0.62f);

pt_widget *sync = pt_progress_create(card);
pt_progress_set_indeterminate(sync, true);

pt_spinner_create(card);
```

Segmented controls copy their labels and report the selected index through
`pt_index_callback`:

```c
static void on_density(pt_widget *segmented, uint32_t index, void *data) {
    (void)segmented;
    (void)data;
    printf("density %u\n", index);
}

static const char *densities[] = {"Comfort", "Cozy", "Compact"};
pt_widget *density = pt_segmented_create(card, densities, 3);
pt_segmented_set_selected(density, 1);
pt_segmented_set_on_change(density, on_density, NULL);
```

Chips are compact, pill-shaped toggles or filters. They carry an optional
glyph, an optional remove button and a selected state:

```c
static void on_remove(pt_widget *chip, void *data) {
    pt_widget_set_visible(chip, false);
    (void)data;
}

pt_widget *filter = pt_chip_create(card, "Source");
pt_chip_set_icon(filter, pt_icon_document);
pt_chip_set_removable(filter, true);
pt_chip_set_on_remove(filter, on_remove, NULL);

pt_widget *selected = pt_chip_create(card, "Pinned");
pt_chip_set_selected(selected, true);
bool pinned = pt_chip_get_selected(selected);
```

A skeleton is an empty shimmering block used while content loads:

```c
pt_widget *placeholder = pt_skeleton_create(card);
pt_widget_set_height(placeholder, 12);
```

Two widget-wide states cover the rest of the modern state matrix. `busy`
paints a widget as waiting for work, `invalid` tints its border with the danger
tokens, and keyboard-driven widgets draw a focus-visible ring sized by
`pt_theme_metric_focus_width` and `pt_theme_metric_focus_offset`:

```c
pt_widget_set_busy(save, true);
pt_widget_set_invalid(email, true);
bool waiting = pt_widget_get_busy(save);
```

### Combo box

A combo box is a field that carries a list of choices. The default mode is
`pt_combobox_select_only`: the field is not an editor, clicking it opens the
list, and choosing an item reports the index through `pt_index_callback`.
`pt_combobox_filterable` turns the field into an entry and filters the popup
with a case-insensitive substring match as the user types.

```c
static void on_region(pt_widget *combo, uint32_t index, void *data) {
    (void)data;
    printf("region %u: %s\n", index, pt_combobox_get_item_text(combo, index));
}

static void on_font_text(pt_widget *combo, const char *text, void *data) {
    (void)combo;
    (void)data;
    printf("filter: %s\n", text);
}

pt_widget *region = pt_combobox_create(card);
pt_combobox_add_item(region, "Europe");
pt_combobox_insert_item(region, 1, "Asia");
pt_combobox_set_item_disabled(region, 1, true);
pt_combobox_set_item_data(region, 0, &europe);   /* borrowed pointer */
pt_combobox_set_selected(region, 0);             /* silent: no callback */
pt_combobox_set_on_change(region, on_region, NULL);

pt_widget *font = pt_combobox_create(card);
pt_combobox_set_mode(font, pt_combobox_filterable);
pt_combobox_set_placeholder(font, "Filter fonts");
pt_combobox_set_on_text_change(font, on_font_text, NULL);
pt_combobox_set_popup_max_height(font, 190.0f);
```

Item text is copied, `pt_combobox_set_item_data` is not: the pointer is borrowed
and moves with the item when another one is inserted or removed. Selection is
`pt_list_index_none` while nothing is chosen, and both `pt_combobox_set_selected`
and `pt_combobox_set_text` are silent, so loading a saved value never re-enters
the callback. Disabled items stay in the list but the highlight skips them.

Keyboard: F4 or Alt+Down/Alt+Up opens, Up/Down move the highlight (and open a
filterable box), Page_Up/Page_Down step a popup page, Home/End jump, Enter or
Space commits the highlighted row and closes, Escape closes unchanged, Tab closes
and moves on. A select-only box also does type-ahead over the item text; a
filterable one leaves Home, End and space to the editor. The popup is an overlay
like the menu: a select-only box gets a native `xdg_popup` when the compositor
offers one, so the list can overflow the window edge, and a filterable box stays
inside the window so the editor keeps focus. In-window placement flips above the
field or clamps to the edges when it would not fit below.

## Text editing

Entries are single-line editors with UTF-8 byte-offset selections. Mouse drag
selects, double-click selects a word and triple-click selects all. The keyboard
model is:

| Shortcut | Action |
| --- | --- |
| Shift+Left/Right | Extend selection by character |
| Shift+Home/End | Extend selection to line edges |
| Ctrl+A | Select all |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy, cut, paste |
| Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y | Undo, redo |
| Backspace/Delete | Replace or clear the selection |
| Enter | Submit |

Undo keeps 32 snapshots and is cleared when text is replaced wholesale through
`pt_widget_set_text`. Selection state is also available programmatically:

```c
pt_entry_select_all(entry);
pt_entry_set_selection(entry, 6, 10);

uint32_t start, end;
if (pt_entry_get_selection(entry, &start, &end)) {
    printf("selection %u..%u\n", start, end);
}
pt_entry_undo(entry);
pt_entry_redo(entry);
```

Cut and copy write to the display clipboard; paste reads it. Dragging a
non-empty selection out of the entry starts an external text drag (disable with
`pt_entry_set_drag_enabled(entry, false)`). Applications can use the same
clipboard directly. The returned string is malloc'd and must be released with
`pt_free`:

```c
pt_window_clipboard_set(window, "copied text");

char *text = pt_window_clipboard_get(window);
if (text) {
    puts(text);
    pt_free(text);
}
```

Clipboard transfer is asynchronous on real Wayland sessions; the getter waits
for the selection with a bounded timeout and returns `NULL` when no text is
available. In headless tests it behaves synchronously.

### Multiline text areas

A text area shares the entry editing model but wraps at the widget width. Enter
inserts a newline, Up/Down keep a horizontal caret goal across wrapped lines,
and Home/End operate on the visual line:

```c
pt_widget *editor = pt_text_area_create(card, "Write a note…");
pt_text_area_set_on_change(editor, on_text, NULL);
pt_text_area_set_grow_to_content(editor, true);

pt_text_area_select_all(editor);
pt_text_area_set_selection(editor, 0, 18);
pt_text_area_undo(editor);
pt_text_area_redo(editor);
```

Text is read and replaced with `pt_widget_get_text` and `pt_widget_set_text`.
A text area has a default height of 160 px; grow-to-content ignores an explicit
height and sizes the widget to its wrapped text, which suits notes and logs.
When the content exceeds the visible area, typing scrolls to the caret;
`pt_text_area_scroll_to_caret` does the same programmatically. Text areas edit
plain text only and expose the `text_frame` accessibility role.

### Styled runs

Labels and text areas accept byte-range styles. Each `pt_text_span` names the
range and sets the flag bits for the fields that apply, so an unset weight,
slant or color is never read:

```c
pt_text_span spans[] = {
    {.start = 0, .length = 5, .flags = pt_text_span_has_weight,
     .weight = pt_font_weight_bold},
    {.start = 6, .length = 6, .flags = pt_text_span_has_slant,
     .slant = pt_font_slant_italic},
    {.start = 13, .length = 5, .flags = pt_text_span_has_color,
     .color = pt_rgb(0x9ECE6A)},
    {.start = 19, .length = 4, .flags = pt_text_span_underline},
};
if (!pt_widget_set_text_spans(label, spans, 4)) {
    puts("this widget kind does not take spans");
}

uint32_t count = pt_widget_get_text_span_count(label);
pt_text_span first;
if (count && pt_widget_get_text_span(label, 0, &first)) {
    printf("span %u+%u\n", first.start, first.length);
}
pt_widget_clear_text_spans(label);
```

Offsets are UTF-8 byte offsets: the setter snaps them to character boundaries,
clamps them to the current text and drops ranges that end up empty, then copies
what is left. A run can also change the typeface: set
`pt_text_span_has_family` with `family` (a NUL-terminated name, up to 63 bytes)
and/or `pt_text_span_has_size` with `size` in points. Mixed-size runs share one
baseline, so a large heading run and a small superscript run sit on the same
line. Spans survive wrapping, measurement, selection and caret movement, and
follow the text while the user types. Replacing the whole string with
`pt_widget_set_text` clears them because the byte offsets no longer describe
the new text. Spans are styling only: they carry no links or embedded objects,
and full bidi paragraph layout remains post-1.0. The direction model, proposed
additive API, logical-to-visual offset mapping and conformance tests are
specified in the [RTL plan](rtl.md).

### Input purpose

`pt_widget_set_input_purpose` tells the compositor and the input method what an
entry expects so it can offer the right keyboard, completions and auto-caps.
Password masking stays a separate concern and is controlled by
`pt_entry_set_password`:

```c
pt_widget_set_input_purpose(email, pt_input_purpose_email);
pt_widget_set_input_purpose(site, pt_input_purpose_url);
pt_widget_set_input_purpose(secret, pt_input_purpose_password);
pt_entry_set_password(secret, true);
```

Purposes are hints: a compositor is free to ignore them. Preedit, commit,
surrounding-text and delete-surrounding-text follow `text-input-v3` for the
focused widget, and IME commits group into a single undo step.

## Virtualized lists

`pt_list_view_create` builds a flat list whose rows exist only while they are
visible. Set the row count and fixed row height, then bind application data in
the bind callback. The default renderer shows the row's text:

```c
static const char *notes[10000]; /* filled by the application */

static void bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    (void)list;
    (void)data;
    pt_widget_set_text(row, notes[index] ? notes[index] : "");
}

static void select_row(pt_widget *list, uint32_t index, void *data) {
    (void)list;
    (void)data;
    printf("selected %u\n", index);
}

pt_widget *list = pt_list_view_create(card);
pt_list_view_set_row_height(list, 44);
pt_list_view_set_row_count(list, 10000);
pt_list_view_set_on_bind_row(list, bind_row, NULL);
pt_list_view_set_on_select(list, select_row, NULL);
```

Pt materializes the viewport rows plus one row of overscan and rebinds recycled
rows while scrolling, so the widget count stays constant. The bind callback
runs whenever a row is rebound: configure text, styling or custom cells there
rather than keeping row pointers. For custom rows, clear the default text and
children, then add ordinary widgets:

```c
pt_widget_set_text(row, "");
pt_widget_clear_children(row);
pt_label_create(row, title);
pt_widget *badge = pt_label_create(row, "new");
pt_widget_set_grow(badge, 1);
```

Rows are row-direction containers owned by the list; never destroy them.
Selection uses `pt_list_index_none` for "no row":

```c
pt_list_view_set_selected(list, 42);           /* callback is not fired */
uint32_t index = pt_list_view_get_selected(list);
pt_list_view_set_selected(list, pt_list_index_none);
```

Clicking a row selects it and fires the select callback. A focused list moves
the selection with Up/Down, Page Up/Page Down, Home and End and reveals it;
Enter or Space fires the activate callback for the selected row. After mutating
application data, call `pt_list_view_refresh` to rebind visible rows;
`pt_list_view_scroll_to_row` reveals an index.

Selection can follow application identity instead of a row number. Register a
key callback that returns a stable non-zero key per row:

```c
static uint64_t note_key(pt_widget *list, uint32_t row, void *data) {
    (void)list;
    (void)data;
    return notes[row].id;
}

static const char *note_title(pt_widget *list, uint32_t row, void *data) {
    (void)list;
    (void)data;
    return notes[row].title; /* borrowed for the duration of the call */
}

pt_list_view_set_row_key_callback(list, note_key, NULL);
pt_list_view_set_row_text_callback(list, note_title, NULL);
bool found = pt_list_view_set_selected_key(list, 7101);
uint64_t key = pt_list_view_get_selected_key(list); /* pt_row_key_none */
```

With keys in place the selection survives reordering, refreshes and row-count
changes as long as the key still exists; lookups fail and the reported
selection becomes `pt_row_key_none` when the row disappears. Without a key
callback, changing the row count clamps or clears a selection that is now out
of range, as before.

The row text callback also feeds type-ahead. Typing printable characters
without Ctrl, Alt or Logo selects the next row whose label starts with the
typed prefix and wraps around the end; typing the same character repeatedly
cycles through matches, and the prefix resets after 700 ms. Without a text
callback, type-ahead only sees text already bound to materialized rows.

After editing a handful of model rows, rebind just those rows with
`pt_list_view_refresh_rows(list, first, count)`. It rebinds the visible rows
intersecting the range and leaves other bindings and the row pool untouched,
which is cheaper than a full refresh for in-place edits.

The list exposes `list` and `list_item` accessibility roles and reports the
selected row.

## Tables, trees and navigation

A table shares the list recycler but adds columns, headers and sorting. Pt does
not reorder application data; the sort callback updates the model and refreshes
the table:

```c
static void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row,
                      uint32_t column, void *data) {
    (void)table;
    const struct file *file = &files[order[row]];
    pt_widget_set_text(cell, column == 0 ? file->name
                        : column == 1  ? file->kind
                        : column == 2  ? file->size
                                       : file->modified);
    if (column == 0) {
        pt_widget_set_icon(cell, pt_icon_document);
    }
}

static uint64_t file_key(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    (void)data;
    return files[order[row]].id;
}

static const char *file_title(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    (void)data;
    return files[order[row]].name;
}

static void on_sort_columns(pt_widget *table, uint32_t count,
                            const uint32_t *columns,
                            const pt_sort_direction *directions, void *data) {
    (void)data;
    sort_model(columns, directions, count); /* application-owned */
    pt_table_refresh(table);
}

pt_widget *table = pt_table_create(page);
pt_table_set_column_count(table, 4);
pt_table_set_column_title(table, 0, "Name");
pt_table_set_column_width(table, 0, 220);
pt_table_set_column_resizable(table, 0, true);
pt_table_set_column_align(table, 2, pt_text_align_end); /* Size */
pt_table_set_row_count(table, file_count);
pt_table_set_row_height(table, 32);
pt_table_set_on_bind_cell(table, bind_cell, NULL);
pt_table_set_on_select(table, on_file_select, NULL);
pt_table_set_on_activate(table, on_file_open, NULL);
pt_table_set_row_key_callback(table, file_key, NULL);
pt_table_set_row_text_callback(table, file_title, NULL);
pt_table_set_on_sort_columns(table, on_sort_columns, NULL);

static const uint32_t sort_columns[] = {1, 0}; /* kind, then name */
static const pt_sort_direction sort_directions[] = {
    pt_sort_ascending, pt_sort_ascending};
pt_table_set_sort_columns(table, 2, sort_columns, sort_directions);
```

Clicking a header cycles ascending → descending → none. Shift-clicking adds or
updates a secondary column; up to `PT_TABLE_MAX_SORT_COLUMNS` (4) columns are
kept in priority order, with the first entry as the primary key. The
`on_sort_columns` callback receives the complete state and is the preferred
hook; `pt_table_set_on_sort` remains for code that only handles one column.
`pt_table_set_sort_columns` sets the indicator state without firing callbacks,
and `pt_table_get_sort_columns` reads it back.

Dragging a column edge resizes it when the column is resizable. Widths set by
code or dragging are stored per column and survive refreshes; read them with
`pt_table_get_column_width`. Hide a column with
`pt_table_set_column_hidden(table, 3, true)`. Hidden columns keep their width,
title and cell bindings but are skipped by layout, painting, hit-testing and
the accessibility tree.

Selection supports stable keys exactly like the list:

```c
pt_table_set_selected_key(table, 7101);
uint64_t key = pt_table_get_selected_key(table); /* pt_row_key_none */
```

With a row key and row text callback, selection and type-ahead follow model
rows through reorders and refreshes even when the visible range changes.
`pt_table_refresh_rows(table, first, count)` rebinds only the visible rows in
a range after a bulk model edit. `pt_table_set_selected` and
`pt_table_get_selected` still use `pt_list_index_none` for "no row"; use
`pt_table_scroll_to_row` or `pt_table_scroll_to_column` to reveal content.

Tables expose `table`, `column_header`, `row` and `table_cell` accessibility
roles. The AT-SPI bridge publishes row and column indices on rows and cells and
the active sort columns, directions and priority on the table and its headers.
Every enabled row also carries `STATE_SELECTABLE` and the rows in the set carry
`STATE_SELECTED`, while the table itself carries `STATE_MULTISELECTABLE`, so a
screen reader knows an unselected row can still be added to the selection.

In-place editing turns any cell into an entry. `pt_table_set_editable` enables
it, `pt_table_begin_edit(table, row, column)` opens the editor over a visible
cell (double-clicking a cell does the same), Enter or focus loss commits and
Escape cancels:

```c
static void on_cell_edited(pt_widget *table, uint32_t row, uint32_t column,
                           const char *text, void *data) {
    (void)table;
    model[model_row(row)][column] = text; /* application-owned storage */
}

pt_table_set_editable(table, true);
pt_table_set_on_cell_edited(table, on_cell_edited, NULL);
pt_table_begin_edit(table, 2, 0);
bool editing = pt_table_is_editing(table);
pt_table_commit_edit(table); /* false when no editor is open */
pt_table_cancel_edit(table);
```

The editor is a real entry, so selection, clipboard and undo behave exactly as
in a normal field. Commit copies the text into the cell and fires the callback;
rebind the model in the callback if the cell content is derived.

A table also keeps a multi-row selection independent of the primary row:

```c
pt_table_select_range(table, 4, 9);            /* shift-click and marquee do this */
uint32_t count = pt_table_get_selection_count(table);
uint32_t rows[16];
uint32_t n = pt_table_get_selection(table, rows, 16);
bool marked = pt_table_is_row_selected(table, 5);
pt_table_clear_selection(table);
```

A pointer drag that starts on a row, or in the empty viewport below the last
row, sweeps the rubber band as far as the pointer moves: without a modifier the
rows the band intersects replace the selection, with Ctrl it is added to. A
plain press in that empty space clears the selection and never activates the
previous primary row; Ctrl keeps the existing set as the additive base. The band selects by geometry, not by the index range
between the anchor and the pointer, so it picks up every row it touches even
when rows are hidden or the drag crosses the viewport edge. Ctrl-click
toggles one row and Shift-click extends from the selection anchor; both adjust
the set without the plain-click replacement that happens on release. The
keyboard matches it: Shift+arrow sweeps a range from the anchor, an unmodified
arrow collapses the set to the row it lands on, and Ctrl+A selects every row.
The public set helpers are silent so an
application can rebuild a selection without echoing; every gesture that moves
the primary row reports it through `pt_table_set_on_select`, and Ctrl+A does not
when the primary row was already the highlighted one. The
primary selection still follows stable keys as above. The same vocabulary works
on lists (`pt_list_view_select_range`, `pt_list_view_select_toggle`,
`pt_list_view_get_selection`, `pt_list_view_get_selection_count`,
`pt_list_view_is_row_selected`, `pt_list_view_clear_selection`) and trees
(`pt_tree_select_range`, `pt_tree_select_toggle`, `pt_tree_get_selection`,
`pt_tree_get_selection_count`, `pt_tree_is_selected`,
`pt_tree_clear_selection`). There the range works over visible rows while the
getters and the toggle address nodes by handle, so a collapsed branch counts as
the one row it shows.

Headers can be reordered by dragging once reordering is enabled, and rows can
start a drag. Both fire callbacks so the application can apply the same
permutation to its model; `pt_table_move_column` performs the same change
programmatically:

```c
static void on_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    (void)table;
    model_move_column(from, to);
}

static void on_row_drag(pt_widget *table, uint32_t row, void *data) {
    state *app = data;
    /* still inside the implicit grab: offer the row's payload */
    pt_drag_data entry = {.mime_type = "application/x-row", .data = row_text(row)};
    pt_window_begin_drag(app->window, &entry, 1);
}

pt_table_set_columns_reorderable(table, true);
pt_table_set_on_columns_reordered(table, on_reordered, NULL);
pt_table_set_on_row_drag(table, on_row_drag, app);
pt_table_move_column(table, 0, 2);
```

### Renaming tree nodes

A tree can rename nodes in place. `pt_tree_set_renamable` enables the gesture,
F2 or a slow double-click opens an editor over the node label, and the rename
callback reports the committed text:

```c
static void on_rename(pt_widget *tree, pt_tree_node *node, const char *text,
                      void *data) {
    (void)tree;
    pt_tree_node_set_data(node, model_track(text));
}

pt_tree_set_renamable(tree, true);
pt_tree_set_on_rename(tree, on_rename, NULL);
pt_tree_begin_rename(tree, node);
bool renaming = pt_tree_is_renaming(tree);
pt_tree_commit_rename(tree); /* false when no editor is open */
pt_tree_cancel_rename(tree);
```

Committing writes the text into the node before the callback runs, so
`pt_tree_node_get_text` already reports the new label.


A tree stores nodes in the widget, so handles stay stable until you clear the
tree or remove an ancestor. Only expanded branches are materialized:

```c
pt_widget *tree = pt_tree_create(page);
pt_tree_set_row_height(tree, 30);
pt_tree_node *root = pt_tree_add_node(tree, NULL, "workspace");
pt_tree_node *src = pt_tree_add_node(tree, root, "src");
pt_tree_node *main_c = pt_tree_add_node(tree, src, "main.c");
pt_tree_node_set_expanded(root, true);
pt_tree_node_set_expanded(src, true);
pt_tree_set_selected(tree, main_c);
pt_tree_set_on_select(tree, on_tree_select, NULL);
```

Use `pt_tree_node_set_data` for a model pointer and
`pt_tree_get_visible_node` for viewport access. `pt_tree_refresh` rebuilds the
visible rows after changing text or expansion. A focused tree moves with
Up/Down, jumps with Home/End, pages with Page Up/Page Down, expands with Right
and collapses or moves to the parent with Left. Typing a prefix selects the
next visible node whose text starts with it, using the same 700 ms cycle as
lists and tables. For visible tree rows, the AT-SPI bridge publishes `level`,
`expanded` and `selected` accessibility attributes. Each row carries
`STATE_SELECTABLE` and the selected ones `STATE_SELECTED`, the tree carries
`STATE_MULTISELECTABLE`, and a branch that can open carries `STATE_EXPANDED`
while it is open or `STATE_COLLAPSED` while it is shut.

Tabs and sidebars form the navigation shell. Pages are ordinary child widgets:

```c
pt_widget *tabs = pt_tabs_create(root);
pt_widget *files_page = pt_tabs_add_page(tabs, "Files");
pt_widget *tree_page = pt_tabs_add_page(tabs, "Tree");
pt_tabs_set_on_change(tabs, on_tab_change, NULL);

pt_widget *sidebar = pt_sidebar_create(root);
pt_sidebar_set_header(sidebar, "Workspace");
pt_sidebar_add_item(sidebar, pt_icon_home, "Files");
pt_sidebar_add_item(sidebar, pt_icon_document, "Recent");
pt_sidebar_add_item(sidebar, pt_icon_settings, "Settings");
pt_sidebar_set_active(sidebar, 0);
pt_sidebar_set_on_select(sidebar, on_sidebar_select, NULL);
pt_sidebar_set_compact(sidebar, true); /* icons only, names stay accessible */
```

The tab strip scrolls horizontally when titles overflow, and the active
indicator glides unless reduced motion is enabled. Icons are drawn from a 24×24
outline (`pt_icon_chevron_down`, `pt_icon_search`, `pt_icon_menu`,
`pt_icon_close`, `pt_icon_check`, `pt_icon_folder`, `pt_icon_document` and 14
more). `pt_icon_button_create(parent, glyph, "Accessible name")` builds an
icon-only button with a tooltip, and `pt_widget_set_icon` decorates labels,
buttons and table cells. Tabs expose `tab_list`, `tab` and `tab_panel` roles;
sidebars expose selectable items with names.

## Motion and reduced motion

Easing helpers map normalized time to progress. `pt_transition` is a
caller-owned value that retargets from its current value, so interrupted
animations do not jump:

```c
float t = pt_ease(pt_easing_out_cubic, progress);
float overshoot = pt_ease_value(pt_easing_out_back, 0, 1, progress);

pt_transition fade;
pt_transition_init(&fade, 0.0f);
pt_transition_to(&fade, 1.0f, 180, pt_easing_out_cubic);

/* From your own timer: advance by the elapsed seconds. */
bool running = pt_transition_advance(&fade, dt);
if (!running) {
    pt_transition_set(&fade, 1.0f);
}
```

Available curves are `linear`, `in_quad`, `out_quad`, `in_out_quad`,
`out_cubic`, `in_out_cubic` and `out_back`. Reduced motion is an app-level
preference. Framework controls snap when it is enabled; custom widgets should
finish transitions instead of interpolating them:

```c
pt_app_set_reduced_motion(app, true);
bool reduced = pt_app_get_reduced_motion(app);
```

The default comes from the `PT_REDUCED_MOTION` environment variable (`1`,
`true` or `yes`), so a session-wide preference can be applied at startup.

## Focus and keyboard input

Click or press Tab to focus interactive widgets. Shift+Tab moves backwards.
Space or Enter activates buttons, toggles, checkboxes and radios; a focused
entry handles caret movement, selection, Home/End, Backspace, Delete, Enter to
submit, and the clipboard shortcuts above. Sliders use the arrow keys (Shift
for larger steps), Home and End.

A window-level key callback runs before focused-widget handling and can consume
a key:

```c
static bool on_key(pt_window *window, uint32_t key, uint32_t modifiers,
                   void *data) {
    (void)data;
    if (key == 0xFF1B) { /* Escape keysym */
        pt_window_close(window);
        return true;
    }
    return false;
}

pt_window_set_key_callback(window, on_key, NULL);
```

Modifier masks are `pt_key_modifier_shift`, `ctrl`, `alt` and `logo`. Keys are
XKB keysyms, so `0xFF1B` is Escape, `0xFF09` is Tab and `0xFF0D` is Return.

`pt_window_focus(window, widget)` focuses a widget programmatically;
`pt_widget_has_focus` queries it. `pt_widget_set_enabled(widget, false)`
excludes a widget from hit testing and focus traversal.

## Windows and events

```c
pt_window_set_title(window, "Renamed");
pt_window_set_size(window, 900, 640);
pt_window_set_min_size(window, 480, 320);
pt_window_set_resizable(window, true);
pt_window_set_decorated(window, true);
pt_window_request_redraw(window);
```

Decorated windows get a Pt-drawn 44 px title bar when the compositor allows
client-side decorations. If the compositor insists on server-side decorations,
the header is omitted and the compositor draws the frame.

Close callbacks return `false` to veto a close:

```c
static bool confirm_close(pt_window *window, void *data) {
    (void)window;
    bool *dirty = data;
    return !*dirty;
}

pt_window_set_close_callback(window, confirm_close, &dirty);
```

When the app loop is running, a close request is deferred until the current
callback finishes. If the app is not running, `pt_window_close` may destroy the
window immediately; query `pt_window_is_open` rather than keeping raw window
pointers beyond a close.

`pt_app_set_quit_on_last_window_close(app, false)` keeps the loop alive after
the last window closes. Call `pt_app_quit` explicitly in that mode.

## Menus, dialogs and toasts

Menus and tooltips use native `xdg_popup` surfaces when the backend supports
them, so they can extend beyond the window edge and stack above other
applications. Dialogs and toasts stay inside the window and reuse the same
overlay layer above the normal widget tree. A menu is anchored to a point or a
widget:

```c
static void on_rename(pt_widget *item, void *data) {
    (void)item;
    open_rename_dialog(data);
}

pt_menu *menu = pt_menu_create(window);
pt_menu_add_item(menu, "Rename", "F2", on_rename, state);
pt_menu_add_separator(menu);
pt_menu_add_item(menu, "Quit", "Ctrl+Q", on_quit, app);
pt_menu_show_below(menu, actions_button);
```

Menu callbacks run after the menu has closed, so they may open a dialog or
another menu. Menus are caller-owned: keep the pointer for as long as the menu
may be shown and call `pt_menu_destroy` before the window or app is destroyed.

Dialogs dim the window behind them, trap Tab focus and close on Escape. An
action closes the dialog before its callback runs:

```c
pt_dialog *dialog = pt_dialog_create(window, "Discard changes?",
                                     "This cannot be undone.");
pt_dialog_add_action(dialog, "Cancel", false, NULL, NULL);
pt_dialog_add_action(dialog, "Discard", true, on_discard, state);
pt_dialog_set_on_close(dialog, on_dialog_closed, state);
pt_dialog_show(dialog);
```

Toasts stack at the bottom-right corner and expire automatically; a duration of
0 keeps one visible until dismissed. The window owns the returned handle, so do
not destroy it yourself:

```c
pt_window_show_toast(window, "Workspace renamed", pt_toast_success, 2500);
pt_window_show_toast(window, "Offline mode", pt_toast_warning, 0);
```

Any widget can carry a tooltip. The default delay is 600 ms; setting 0 shows it
as soon as the pointer rests on the widget:

```c
pt_widget_set_tooltip(actions_button, "Open the actions menu");
pt_widget_set_tooltip_delay(actions_button, 350);
```

Choose the popup backend per window. `auto` prefers native surfaces and falls
back to in-window rendering, which is also what headless windows use:

```c
pt_window_set_popup_policy(window, pt_popup_policy_auto);
pt_window_set_popup_policy(window, pt_popup_policy_window); /* force in-window */
```

Dialogs and toasts remain clipped to the window bounds because they are modal
or stacked relative to its content. Menus and tooltips are not.

## Drag and drop, gestures and momentum

Register a drop target on any widget. The enter callback picks whether to
accept the offered MIME type; motion and leave track the hover, and drop
receives UTF-8 text:

```c
static bool on_drop_enter(pt_widget *widget, const char *mime, void *data) {
    (void)widget;
    (void)data;
    return strncmp(mime, "text/", 5) == 0;
}

static void on_drop(pt_widget *widget, const char *mime, const char *text,
                    float x, float y, void *data) {
    (void)widget;
    (void)mime;
    (void)x;
    (void)y;
    printf("dropped: %s\n", text);
}

pt_drop_target target = {
    .enter = on_drop_enter,
    .motion = NULL,
    .leave = NULL,
    .drop = on_drop,
    .user_data = state,
};
pt_widget_set_drop_target(drop_zone, &target);
```

Drop targets are inherited by descendants and positions are relative to the
target's top-left corner. Entries become drag sources automatically once a
selection is dragged a few pixels; disable that with
`pt_entry_set_drag_enabled(entry, false)`, or start a custom drag with
`pt_window_begin_drag` while a pointer press is active.
`pt_window_begin_text_drag` and `pt_window_begin_uri_drag` were deprecated in
0.9.0 and removed at the 1.0 freeze: pass one `text/plain;charset=utf-8` or
`text/uri-list` entry to `pt_window_begin_drag` instead, joining a URI list with
CRLF yourself.

Gestures arrive on one window callback. Swipe translation is accumulated,
pinch scale is relative to the gesture start, rotation is in radians, and
`target` is the widget under the centroid when the gesture began:

```c
static void on_gesture(pt_window *window, const pt_gesture_event *event, void *data) {
    (void)window;
    if (event->kind == pt_gesture_kind_swipe) {
        printf("swipe dx=%.1f dy=%.1f phase=%d\n", event->dx, event->dy,
               event->phase);
    }
}

pt_window_set_gesture_callback(window, on_gesture, state);
pt_window_set_long_press_delay(window, 450); /* 0 disables synthesized holds */
```

Long press is synthesized from a pointer hold that stays within an 8 px slop;
compositor three-finger holds arrive regardless of the delay. Kinetic scrolling
is on by default for continuous touchpad sources and can be controlled
globally:

```c
pt_window_set_momentum_scrolling(window, true);
bool enabled = pt_window_get_momentum_scrolling(window);
```

File managers and similar apps can offer `text/uri-list` instead of plain text.
Each URI is written on its own line; the drag image is optional and replaces the
built-in thumbnail. Both calls need the implicit grab of an active press, so
invoke them from a pointer or touch handler:

```c
static const char *uris =
    "file:///home/user/Documents/report.pdf\n"
    "file:///home/user/Documents/budget.ods\n";
pt_drag_data uri_offer = {.mime_type = "text/uri-list", .data = uris, .size = 0};
pt_window_set_drag_image(window, thumbnail_pixels, 96, 72);
pt_window_begin_drag(window, &uri_offer, 1);
```

The receiving side checks for `text/uri-list`, parses the payload and frees each
URI. Comments and blank lines are ignored; CRLF line endings are accepted:

```c
static bool on_drop_enter(pt_widget *widget, const char *mime, void *data) {
    (void)widget;
    (void)data;
    return strcmp(mime, "text/uri-list") == 0;
}

static void on_drop(pt_widget *widget, const char *mime, const char *text,
                    float x, float y, void *data) {
    (void)widget;
    (void)mime;
    (void)x;
    (void)y;
    uint32_t count = pt_uri_list_count(text);
    for (uint32_t i = 0; i < count; ++i) {
        char *uri = pt_uri_list_get(text, i);
        printf("dropped uri: %s\n", uri);
        pt_free(uri);
    }
}
```

Structured payloads use the offer surface instead. Registering `enter_offer`
takes precedence over `enter` and `drop_offer` over `drop`, so a target can see
every MIME type the source advertises, choose one, and then read it in chunks on
the event loop instead of inside the drop callback. The chunks are not
NUL-terminated; concatenate them for text formats. A stream is released after
its final `done`, `cancelled` or `error` callback returns, and reads may run
concurrently:

```c
static const char *on_offer_enter(pt_widget *widget, pt_drop_offer *offer, void *data) {
    (void)widget;
    (void)data;
    if (pt_drop_offer_has_mime(offer, "application/json")) {
        return "application/json";
    }
    return pt_drop_offer_mime(offer, 0); /* NULL declines the drag */
}

static void on_stream(pt_widget *widget, pt_drop_stream *stream, pt_drop_stream_state state,
                      const char *data, size_t size, void *user_data) {
    (void)widget;
    if (state == pt_drop_stream_data) {
        fwrite(data, 1, size, stdout);
        if (!pt_drop_stream_active(stream)) {
            printf("read %zu bytes of %s\n", pt_drop_stream_size(stream),
                   pt_drop_stream_mime(stream));
        }
    }
}

static void on_offer_drop(pt_widget *widget, pt_drop_offer *offer, const char *mime,
                          float x, float y, void *data) {
    (void)widget;
    (void)x;
    (void)y;
    /* max_bytes 4096 caps the read; zero means unlimited. */
    pt_drop_offer_read(offer, mime, 4096, on_stream, data);
}

pt_drop_target target = {
    .user_data = state,
    .enter_offer = on_offer_enter,
    .drop_offer = on_offer_drop,
};
pt_widget_set_drop_target(zone, &target);
```

`pt_drop_offer_actions` reports the source's advertised action mask, and
`pt_drop_offer_selected_mime` returns the type accepted during enter. On the
sending side, `pt_window_begin_drag` offers several types in one drag and still
needs the implicit grab of an active press:

```c
static const char *json = "{\"rows\":3}";
pt_drag_data types[] = {
    {.mime_type = "application/json", .data = json, .size = 0},
    {.mime_type = "text/plain", .data = "three rows", .size = 0},
};
pt_window_set_drag_finished_callback(window, on_drag_finished, state);
pt_window_begin_drag(window, types, 2);
```

`size` zero means the payload is NUL-terminated text. The completion callback
fires once per successful drag with `dropped` false when the destination
rejected or the user cancelled, and the performed action otherwise, which
`pt_window_get_drag_result` also reports after the fact.

Pinch and rotate arrive as `pt_gesture_kind_pinch` with the contact count in
`fingers`. Scale is measured against the start of the gesture and rotation is
in radians, so applying both relative to a saved transform is stable across
updates:

```c
static void on_gesture(pt_window *window, const pt_gesture_event *event, void *data) {
    state *app = data;
    (void)window;
    if (event->kind == pt_gesture_kind_pinch && event->phase == pt_gesture_phase_update) {
        app->zoom = app->zoom_at_start * event->scale;
        app->angle = app->angle_at_start + event->rotation;
    }
}
```

Poll `pt_window_get_drag_active` to show drag feedback. `pt_window_cancel_drag`
aborts a source drag, hides the thumbnail and clears any pending entry drag; it
is safe when no drag is active. A non-zero timeout cancels a drag that the
destination never finishes, which guards against unresponsive drop targets:

```c
pt_window_set_drag_timeout(window, 15000); /* 0 leaves cancel to the compositor */
if (pt_window_get_drag_active(window)) {
    /* the drag is still awaiting a drop */
}
pt_window_cancel_drag(window);
```

Stylus input from tablet-v2 is delivered through its own callback and mirrors
the pointer stream, so widgets keep receiving ordinary pointer events. A pen in
proximity without contact reports `enter` and `motion`; contact adds `down` and
`up`. Pressure and distance are normalized to `[0, 1]`, tilt is in degrees per
axis and rotation is in radians. `tool` distinguishes pen, eraser, brush,
pencil and airbrush, while `buttons` carries the barrel and auxiliary buttons:

```c
static void on_stylus(pt_window *window, const pt_stylus_event *event, void *data) {
    (void)window;
    (void)data;
    if (event->phase == pt_stylus_phase_down) {
        printf("tool=%d pressure=%.2f tilt=%.1f/%.1f\n", event->tool,
               event->pressure, event->tilt_x, event->tilt_y);
    }
    if (event->tool == pt_stylus_tool_eraser) {
        /* erase stroke under (event->x, event->y) */
    }
    if (event->buttons & PT_STYLUS_BUTTON_BARREL) {
        /* barrel button held: open a context action */
    }
}

pt_window_set_stylus_callback(window, on_stylus, state);
```

## Accessibility

Every built-in widget derives an accessibility role, name, description and
state from its kind and content. Entries and text areas expose their text,
caret and selection offsets; sliders, progress bars and segmented controls
expose their value. Password entries expose the caret but not the text. Lists
use the `list` role, rows the `list_item` role and report the selected row;
text areas use `text_frame`. A combo box reports `combo_box` with the
`expandable` state and gains `expanded` while its popup is open, and the popup
panel itself reports `list_box`. A view that accepts a multi-row set adds
`multi_selectable`, each of its rows adds `selectable`, and a branch that can
open adds `expandable` together with `expanded` while it is open. Applications can
override the derived fields:

```c
pt_widget *save = pt_button_create(card, "Save changes");
pt_widget_set_a11y_description(save, "Writes the form to disk");

pt_widget *details = pt_label_create(card, "Advanced options");
pt_widget_set_a11y_role(details, pt_a11y_role_group);

uint32_t state = pt_widget_get_a11y_state(save);
if (state & pt_a11y_state_focused) {
    /* ... */
}
```

An app-level callback receives focus, state, text, selection, value, name and
structure changes synchronously on the UI thread. It is the bridge point for a
platform accessibility adapter:

```c
static void on_a11y(pt_window *window, pt_widget *widget,
                    pt_a11y_event_type event, void *data) {
    (void)window;
    (void)widget;
    (void)data;
    switch (event) {
    case pt_a11y_event_focus_changed:
        break;
    case pt_a11y_event_text_changed:
        break;
    default:
        break;
    }
}

pt_app_set_a11y_callback(app, on_a11y, state);
```

Text and value accessors use out parameters and return `false` when the widget
has no such data:

```c
const char *text;
uint32_t caret, start, end;
if (pt_widget_get_a11y_text(entry, &text, &caret, &start, &end)) {
    printf("%.*s\n", (int)(end - start), text + start);
}

float value;
if (pt_widget_get_a11y_value(slider, &value)) {
    printf("slider value %.2f\n", value);
}
```

The in-process model always works. To let external screen readers see the
window, enable the optional AT-SPI2 bridge, which connects to the session bus
through libdbus:

```c
if (!pt_app_enable_a11y(app)) {
    /* dbus-1 was not available at build time or the bus is unreachable */
}
pt_a11y_status status = pt_app_get_a11y_status(app);
```

`status` is `pt_a11y_status_disabled` before enabling,
`pt_a11y_status_unavailable` when the bridge cannot run and
`pt_a11y_status_active` once it is serving. The bridge mirrors the widget tree
and serves `Accessible`, `Application`, `Component`, `Action`, `Text`,
`EditableText` and `Value`; change events are forwarded to AT-SPI listeners.
Table and tree metadata is published as accessibility attributes: tables expose
their sort columns, headers expose `sort` and `sort-priority`, table cells and
rows expose `row-index`/`column-index`, and tree rows expose `level`, `expanded`
and `selected`. The bridge is opt-in and can be stopped with
`pt_app_disable_a11y(app)`. See [accessibility.md](accessibility.md) for the
derivation tables, the full AT-SPI interface matrix, the adapter contract and
the 1.0 limitations.

## Timers and embedded loops

```c
static void tick(pt_app *app, void *data) {
    (void)app;
    int *ticks = data;
    ++*ticks;
}

uint64_t id = pt_app_add_timeout(app, 250, tick, &ticks);
pt_app_cancel_timeout(app, id); /* no-op if it already fired */
```

Timers are one-shot and main-thread. `pt_app_run_for(app, milliseconds)` pumps
events for at most the given duration, which is useful for tests and for
integrating Pt into another event loop. `pt_app_is_running` reports whether a
loop is active.

## Sizing, scale and painting

`pt_window_get_width` and `pt_window_get_height` return logical pixels.
`pt_window_get_scale` returns the current rendering scale negotiated through
`fractional-scale-v1` or the compositor's preferred buffer scale. Widget bounds
from `pt_widget_get_bounds` are in window logical coordinates.

Pt repaints on demand: state changes mark the affected widget or window dirty,
animations keep a frame callback active only while they run, and idle windows
do not repaint. Efficient partial repaint relies on tracking a logical damage
rectangle through layout and state changes. The renderer keeps a persistent
backing canvas, re-clips to the damaged region and submits the matching
`wl_surface.damage_buffer` rectangle, so a spinner or a hovered button does not
repaint the whole window. A window with no damage is neither painted nor
committed. There is no public "draw now" call; `pt_window_request_redraw`
forces a full-window frame when that is what you want.

Touch input is supported on compositors that expose `wl_touch`: the first
active touch follows the pointer hit-testing, enter/leave, press, drag and
release path, and a cancelled touch releases the pressed widget. Touchpad
swipe/pinch gestures come from `pointer-gestures-v1`, and tablet-v2 stylus
events carry tool, pressure, tilt, rotation and buttons; see the gesture and
stylus callbacks above.

Damage rectangles are not public API. Widgets and windows manage damage
automatically, and tests exercise input and painting through the internal
headers used by the repository suites. Gesture, drag and stylus events are
public.

## Errors

Constructors return `NULL` on failure and set a thread-local error string:

```c
pt_window *window = pt_window_create(app, "Title", 800, 600);
if (!window) {
    fprintf(stderr, "window: %s\n", pt_get_last_error());
}
```

The error string is owned by Pt and is valid until the next Pt call on the same
thread. Allocation failure in internal state aborts with a diagnostic rather
than leaving a partially valid tree. Wayland disconnects surface through the
return value of `pt_app_run`/`pt_app_run_for` and `pt_get_last_error`.

## Lifetime and threading rules

- `pt_app` owns its windows. `pt_app_destroy` destroys all windows and their
  widget trees. Do not use any handle after that call.
- A window owns its root widget and the whole tree below it. Change structure
  with widget constructors, `pt_widget_destroy` or `pt_widget_clear_children`.
- Strings passed to setters are copied. Strings returned by getters are owned by
  the widget and remain valid until the next text change or destruction.
- All public calls, except documented read-only queries, must run on the thread
  that drives the app loop. There is no internal locking. Use a pipe or an
  application-owned queue to marshal work from other threads.
- Do not free a `user_data` pointer while a callback that uses it is still
  registered. Clearing the callback to `NULL` is safe.

## Headless testing

Set `PT_HEADLESS=1` to create an app with no Wayland connection:

```sh
PT_HEADLESS=1 ./my_ui_test
```

Headless apps support windows, widgets, timers, callbacks and
`pt_app_run_for`, so state and event logic can be tested on CI. Painting is
exercised by the repository tests through internal canvas entry points; the
public API deliberately does not expose synthetic input or offscreen painting.
The bundled tests show the pattern:

```sh
make check
make check-bindings
```

For stricter local validation:

```sh
make clean
make -j"$(nproc)" werror=1
make check werror=1
make check-bindings werror=1
```

The sanitizer matrix, including the required ASan preload for Python and Java,
is documented in [release.md](release.md#validation-matrix).

## Next steps

- [Bindings](bindings.md) for C++, Python and Java.
- [Architecture](architecture.md) for layout, painting and Wayland internals.
- [Performance](performance.md) for reproducible benchmarks and budgets.
- [Comparison](comparison.md) for how Pt differs from other toolkits.
