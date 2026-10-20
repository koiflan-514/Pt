# Pt language bindings

Pt exposes one flat C ABI. The bindings are thin, hand-written layers over that
ABI so the behaviour of a widget is identical from every language. The 0.10
surface adds the combobox widget — modes, item list and per-item data and
disabled flags, field text, placeholder, popup control and the change/text
callbacks — together with multi-row selection parity across the list, table and
tree views and the matching accessibility roles and states, and keeps the
parity gate closed: all 412 functions — two fewer than 0.10 shipped, because the
1.0 freeze removed the deprecated drag helpers — are available from C++, Python
and Java.
The 0.9 surface — the expanded theme tokens and metrics with window-level
overrides, chips and skeletons, the busy/invalid state matrix, table cell
editing, multi-row selection, column reorder and row drag, tree rename and
per-run typography — remains wrapped in full.

| Binding | Technology | Minimum | Ownership |
| --- | --- | --- | --- |
| C | direct | C23 compiler | caller owns the app |
| C++ | header-only RAII helpers | C++23 | `pt::app` owns the native graph |
| Python | `ctypes` | 3.10 | `pt.App` owns windows; wrappers keep trampolines alive |
| Java | Foreign Function & Memory API | JDK 22 (tested on 25) | `MemorySegment` handles owned by the native graph |

All bindings are main-thread only and all callbacks run on the thread executing
`app_run`/`app_run_for`. The bindings pass `NULL` for the C `user_data`
argument and capture language state in the callback object instead.

## C++

The wrapper lives in `bindings/cpp/pt/pt.hpp` and is installed as
`<pt/pt.hpp>`. Widgets and windows are non-owning handles; `pt::app` owns the
native object graph and destroys it when it goes out of scope.

```cpp
#include <pt/pt.hpp>

#include <cstdio>

struct counter {
    int value = 0;
    pt_widget *label = nullptr;
};

static void increment(pt_widget *, void *data) {
    auto *state = static_cast<counter *>(data);
    ++state->value;
    char text[32];
    std::snprintf(text, sizeof(text), "%d", state->value);
    pt_widget_set_text(state->label, text);
}

int main() {
    pt::app app("dev.pt.cpp");
    if (!app.valid()) {
        std::fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }

    pt::window window(app, "Counter", 480, 320);
    auto root = window.root();
    auto column = pt::column(root).set_padding(24, 24, 24, 24).set_gap(12);

    counter state;
    pt::label(column, "Counter").set_font_size(24);
    state.label = pt::label(column, "0").set_font_size(40).get();
    pt::button(column, "Add").set_button_variant(pt_button_primary)
        .on_click(increment, &state);

    window.show();
    return app.run();
}
```

Classes: `pt::app`, `pt::window`, `pt::widget`, `pt::menu`, `pt::dialog`,
`pt::toast` and `pt::transition`. Factories return `pt::widget` by value: `box`,
`row`, `column`, `label`, `button`, `entry`, `text_area`, `toggle`, `checkbox`,
`radio`, `slider`, `progress`, `spinner`, `segmented`, `scroll`, `wrap`, `grid`,
`list_view`, `divider`, `card`, `spacer`, `table`, `tree`, `tabs`, `sidebar`,
`icon` and `icon_button`. Most `pt::widget` setters are fluent and return
`widget &`. New in 0.2:

- `set_checked`, `checked`, `on_checked` work for toggles, checkboxes and
  radios.
- `set_progress`, `progress`, `set_indeterminate`, `indeterminate`.
- `segmented(parent, {"A", "B"})`, `select_segment`, `segment`,
  `on_segment_change` with the raw `pt_index_callback` signature.
- `select_all`, `set_selection`, `selection()` (returns
  `std::optional<std::pair<uint32_t, uint32_t>>`), `undo`, `redo`.
- `set_scroll_offset`, `scroll_offset`, `max_scroll_offset`.
- `window.set_clipboard` / `window.clipboard()`; the getter frees the native
  string for you.
- `app.theme_color(token)` / `app.set_theme_color(token, color)`.

New in 0.3:

```cpp
pt::menu menu(window);
menu.add_item("Rename", "F2", on_rename, &state)
    .add_separator()
    .add_item("Quit", "Ctrl+Q", on_quit, &app)
    .show_below(button);

pt::dialog dialog(window, "Discard changes?", "This cannot be undone.");
dialog.add_action("Cancel", false).add_action("Discard", true, on_discard, &state);
dialog.show();

pt::toast notice = window.show_toast("Saved", pt_toast_success, 2500);
button.set_tooltip("Rename this item").set_tooltip_delay(400);
button.set_a11y_name("Rename item").set_a11y_description("Opens the rename dialog");
```

- `pt::menu` and `pt::dialog` are move-only RAII owners; destroy them before
  the window and app. `pt::toast` is a non-owning handle owned by the window.
- Menu item callbacks run after the menu closes. Dialog actions close the
  dialog before their callback runs, and Escape or the close button triggers
  `on_close`.
- `pt::widget` adds `set_tooltip`, `set_tooltip_delay`, `set_a11y_role`,
  `set_a11y_name`, `set_a11y_description`, `a11y_role`, `a11y_name`,
  `a11y_description`, `a11y_state` and `a11y_value`
  (`std::optional<float>`).
- `pt::app::on_a11y_event(callback, data)` registers the native
  `pt_a11y_event_callback` signature.

New in 0.4:

```cpp
pt_drop_target target{drop_enter, drop_motion, drop_leave, drop_receive, &state};
drop_zone.set_drop_target(target);
entry.set_drag_enabled(true);
window.on_gesture(on_gesture, &state).set_long_press_delay(450);
window.set_momentum_scrolling(true);
window.begin_text_drag("selected text"); // removed in 1.0, see begin_drag
```

- `pt::widget` adds `set_drop_target(const pt_drop_target&)`,
  `clear_drop_target()`, `drop_hover()`, `set_drag_enabled(bool)` and
  `drag_enabled()`.
- `pt::window` adds `on_gesture(pt_gesture_callback, void *)`,
  `set_long_press_delay(uint32_t)`, `set_momentum_scrolling(bool)`,
  `momentum_scrolling()` and `begin_text_drag(std::string_view)`, the last of
  these removed in 1.0.
- Gesture callbacks receive the flat `pt_gesture_event`; use
  `pt_gesture_kind_*` and `pt_gesture_phase_*` to interpret it. The target is
  the widget under the gesture centroid when the gesture begins.

New in 0.5:

```cpp
auto list = pt::list_view(sidebar)
                .set_row_height(44)
                .set_row_count(10000)
                .on_bind_row(bind_row, &state)
                .on_row_select(select_row, &state);
list.set_selected_row(42).scroll_to_row(42);
list.set_selected_row(pt_list_index_none); // clear the selection

pt::text_area(editor_pane, "Write a note…")
    .set_grow_to_content(true)
    .on_change(on_text, &state);

pt::wrap(tag_bar).set_wrap_gap(6, 6);
pt::grid(stats, 3).set_grid_gap(8, 8);
badge.set_grid_span(2);

pt::transition fade;
fade.to(1.0f, 180, pt_easing_out_cubic);
bool running = fade.advance(dt);
float eased = pt::ease_value(pt_easing_out_back, 0.0f, 1.0f, t);
app.set_reduced_motion(true);
```

- `pt::widget::kind()` returns the `pt_widget_kind`; text-area methods dispatch
  on it. `set_placeholder`, `on_change`, `select_all`, `set_selection`,
  `selection`, `undo` and `redo` therefore work for entries and text areas,
  while `set_grow_to_content`, `grow_to_content` and `scroll_to_caret` are
  text-area specific.
- Layout methods are `wrap(parent)`, `grid(parent, columns)` and
  `set_wrap_gap`, `set_grid_columns`, `grid_columns`, `set_grid_gap`,
  `set_grid_span` and `grid_span` on `pt::widget`.
- List methods are `list_view(parent)`, `set_row_count`, `row_count`,
  `set_row_height`, `row_height`, `on_bind_row` (raw
  `pt_list_bind_callback`), `on_row_select`/`on_row_activate`
  (`pt_index_callback`), `selected_row`, `set_selected_row`, `refresh` and
  `scroll_to_row`. `pt_list_index_none` clears the selection.
- `pt::ease` and `pt::ease_value` wrap the curve set. `pt::transition` is a
  caller-owned value with `to`, `advance`, `set`, `value`, `active` and `raw`
  for C interop.
- `app.set_reduced_motion(bool)` and `app.reduced_motion()` expose the app
  preference; `PT_REDUCED_MOTION` supplies the default.

New in 0.6:

```cpp
auto shell = pt::sidebar(root).set_sidebar_header("Workspace");
pt::widget files_item = shell.add_sidebar_item(pt_icon_folder, "Files");

auto tabs = pt::tabs(content);
pt::widget data_page = tabs.add_tab("Data");

pt::widget table = pt::table(data_page)
                       .set_column_count(4)
                       .set_column_title(0, "Name")
                       .set_column_width(0, 214)
                       .set_column_resizable(0, true)
                       .set_table_row_count(rows.size())
                       .on_bind_cell(bind_cell, &state)
                       .on_table_select(select_row, &state)
                       .on_table_sort(sort_rows, &state)
                       .set_sort(0, pt_sort_ascending);

pt::widget tree = pt::tree(data_page);
pt_tree_node *src = tree.add_tree_node(nullptr, "src");
pt::set_tree_expanded(src, true);

pt::widget check = pt::icon(data_page, pt_icon_check).set_icon_size(16);
pt::icon_button(data_page, pt_icon_plus, "Add file").on_click(add_file, &state);

window.begin_uri_drag({"file:///tmp/report.txt"}); // removed in 1.0
window.set_drag_image(pixels, 96, 72);
window.on_stylus(on_stylus, &state);
std::string uri = pt::uri_list_get(payload, 0);
```

- Factories `table`, `tree`, `tabs`, `sidebar`, `icon` and `icon_button`;
  `pt::tree_node` aliases the opaque node handle and `pt::stylus_event` the
  stylus record.
- Table methods: `set_column_count`/`column_count`, `set_column_title`,
  `set_column_width`/`column_width`, `set_column_align`,
  `set_column_resizable`, `set_table_row_count`/`table_row_count`,
  `set_table_row_height`/`table_row_height`, `set_header_height`,
  `on_bind_cell` (`pt_table_bind_cell_callback`), `on_table_select`,
  `on_table_activate`, `on_table_sort`, `set_sort`/`sort`,
  `table_selected`/`set_table_selected`, `table_refresh`,
  `scroll_to_column` and `table_scroll_to_row`.
- Tree methods: `tree_root_count`, `tree_root`, `add_tree_node`, `clear_tree`,
  `tree_visible_count`, `tree_visible_node`, `tree_selected`,
  `set_tree_selected`, `set_tree_row_height`, `set_tree_indent`,
  `on_tree_select`, `on_tree_activate`, `tree_refresh` and
  `scroll_to_tree_node`. Node helpers are `pt::tree_text`, `set_tree_text`,
  `tree_expanded`, `set_tree_expanded`, `tree_has_children`, `tree_depth`,
  `tree_parent`, `tree_child_count`, `tree_child`, `tree_data` and
  `set_tree_data`.
- Tabs methods: `add_tab`, `remove_tab`, `tab_count`, `tab_page`,
  `set_tab_title`, `tab_title`, `active_tab`, `set_active_tab`,
  `on_tab_change` and `set_tab_strip_height`. Sidebar methods:
  `add_sidebar_item`, `sidebar_item_count`, `sidebar_item`,
  `active_sidebar_item`, `set_active_sidebar_item`, `sidebar_compact`,
  `set_sidebar_compact`, `set_sidebar_header`, `sidebar_header` and
  `on_sidebar_select`.
- `pt::widget` adds `set_icon`/`icon` and `set_icon_size`/`icon_size`.
- `pt::window` adds `begin_uri_drag(std::initializer_list<std::string_view>)`
  (removed in 1.0), `set_drag_image(const pt_color *, int, int)`,
  `clear_drag_image()` and `on_stylus(pt_stylus_callback, void *)`.
- Free helpers `pt::uri_list_count(std::string_view)` and
  `pt::uri_list_get(std::string_view, uint32_t)` return parsed URIs; the getter
  copies and frees the native string.

New in 0.7:

```cpp
uint64_t key_of(pt_widget *, uint32_t row, void *data);
const char *label_of(pt_widget *, uint32_t row, void *data);

auto table = pt::table(page)
                 .set_column_hidden(3, true)
                 .set_table_row_key_callback(key_of, &model)
                 .set_table_row_text_callback(label_of, &model)
                 .on_sort_columns(on_sort, &model)
                 .set_sort_columns({{1, pt_sort_ascending},
                                    {0, pt_sort_ascending}})
                 .table_refresh_rows(2, 4);
std::vector<pt::sort_key> keys = table.sort_columns(); // priority order
uint64_t key = table.table_selected_key();
bool found = table.set_table_selected_key(key);

auto list = pt::list_view(page)
                .set_row_key_callback(key_of, &model)
                .set_row_text_callback(label_of, &model)
                .refresh_rows(0, 20);
list.set_selected_key(7107);

window.set_popup_policy(pt_popup_policy_window)
    .set_drag_timeout(800);
bool moving = window.drag_active();
window.cancel_drag();
pt_a11y_status status = app.a11y_status(); // enable_a11y() / disable_a11y()
```

- `pt::app` adds `enable_a11y()`, `disable_a11y()` and `a11y_status()`.
- `pt::window` adds `set_popup_policy`/`popup_policy`,
  `drag_active()`, `cancel_drag()`, `set_drag_timeout`/`drag_timeout`.
- List methods are `refresh_rows(first, count)`, `set_row_key_callback`
  (`pt_row_key_callback`), `selected_key()`, `set_selected_key(uint64_t)`,
  `set_row_text_callback` and the existing `refresh`/`scroll_to_row`. Use
  `pt_row_key_none` to clear a keyed selection.
- Table methods are `set_column_hidden`/`column_hidden`,
  `on_sort_columns` (`pt_table_sort_columns_callback`),
  `set_sort_columns(std::initializer_list<sort_key>)` and `sort_columns()`
  returning `std::vector<sort_key>` in priority order, where `sort_key` has
  `column` and `direction` fields. Keyed selection is
  `set_table_row_key_callback`, `table_selected_key()`,
  `set_table_selected_key(uint64_t)`, and batch invalidation is
  `table_refresh_rows(first, count)`. Direct `set_sort_columns` does not invoke
  the sort callback; header clicks do.

New in 0.8:

```cpp
/* Inspect every advertised MIME type, then stream the payload so a slow source
 * never blocks the UI thread. */
const char *pick_offer(pt_widget *, pt_drop_offer *raw, void *) {
    pt::drop_offer offer(raw);
    for (uint32_t i = 0; i < offer.mime_count(); ++i) {
        std::string_view mime = offer.mime(i);
        if (mime == "text/uri-list" || mime == "text/plain") {
            return pt_drop_offer_mime(raw, i); // the return value must be offered
        }
    }
    return nullptr;                            // decline the drag
}

void on_stream(pt_widget *, pt_drop_stream *raw, pt_drop_stream_state state,
               const char *data, size_t size, void *buffer) {
    auto *text = static_cast<std::string *>(buffer);
    if (state == pt_drop_stream_data) {
        text->append(data, size);              // chunks are not NUL-terminated
        return;
    }
    /* done/cancelled/error is the final callback; Pt releases the stream. */
}

void on_offer_drop(pt_widget *, pt_drop_offer *raw, const char *mime, float, float,
                   void *buffer) {
    auto *text = static_cast<std::string *>(buffer);
    text->clear();
    pt::drop_offer(raw).read(mime, 0, on_stream, text); // 0 means unlimited
}

card.set_drop_target(pick_offer, on_offer_drop);        // motion/leave default to null

label.set_text_spans({
    {0, 5, pt_font_weight_bold, pt_font_slant_upright, pt_rgb(0xdc8a1c),
     pt_text_span_has_weight | pt_text_span_has_color},
    {19, 4, pt_font_weight_regular, pt_font_slant_upright, 0,
     pt_text_span_underline}});
uint32_t spans = label.text_span_count();
std::optional<pt_text_span> first = label.text_span(0);
label.clear_text_spans();
entry.set_input_purpose(pt_input_purpose_email);        // entry.input_purpose()

window.set_drag_finished_callback(
    [](pt_window *, bool dropped, pt_drag_action action, void *) {
        if (dropped && action == pt_drag_action_move) {
            clear_source();
        }
    });
window.begin_drag({{"text/plain", payload.c_str(), payload.size()},
                   {"application/json", json.c_str(), json.size()}});
bool moving = window.drag_active();
pt_drag_action result = window.drag_result();
```

- `pt::drop_offer` and `pt::drop_stream` are non-owning handles with null-safe
  accessors. `drop_offer` adds `mime_count()`, `mime(index)`,
  `has_mime(string_view)`, `selected_mime()`, `actions()` and
  `read(mime, max_bytes = 0, callback = nullptr, data = nullptr)`;
  `drop_stream` adds `mime()`, `size()` (bytes delivered so far), `active()`
  and `cancel()`. An offer is only valid inside its callbacks; streams opened
  from it outlive them.
- `widget::set_drop_target(enter_offer, drop_offer, motion, leave, data)` fills
  a `pt_drop_target` for you; `set_drop_target(pt_drop_target)` and
  `clear_drop_target()` remain available.
- Styled runs are `set_text_spans(vector|initializer_list)`,
  `clear_text_spans()`, `text_span_count()` and `text_span(index)`, which
  returns `std::optional<pt_text_span>`. Spans use byte offsets into the UTF-8
  text and are clamped to it; the `pt_text_span_*` flags select which of
  `weight`, `slant` and `color` are honoured.
- `widget::set_input_purpose`/`input_purpose()` uses `pt_input_purpose_*`.
- Window additions are `begin_drag(span|initializer_list<pt_drag_data>)`,
  `set_drag_finished_callback` and `drag_result()`. A `pt_drag_data` size of
  zero means NUL-terminated data. `drag_data`, `drop_stream_state` and
  `text_span` are aliases of the C structs and enum.

New in 0.9:

```cpp
app.set_theme_metric(pt_theme_metric_chip_height, 30.0f);
float radius = app.theme_metric(pt_theme_metric_radius_small);

window.set_theme_color(pt_theme_background, pt_rgb(0x102030));
window.set_theme_metric(pt_theme_metric_focus_width, 3.0f);
window.clear_theme_overrides();                 // back to the app palette

auto language = pt::chip(root, "C++23");        // pt::chip / pt::skeleton factories
language.set_chip_icon(pt_icon_folder).set_chip_removable(true);
language.on_chip_click(show_filter).on_chip_remove(remove_filter);
pt::skeleton(root).set_height(12);

button.set_busy(true).set_invalid(false);

table.set_table_editable(true)
     .on_cell_edited(on_cell_edited)
     .set_columns_reorderable(true)
     .on_columns_reordered(on_columns_reordered)
     .on_row_drag(on_row_drag);
table.begin_cell_edit(1, 0);
table.commit_cell_edit();
table.select_range(1, 3);
std::vector<uint32_t> rows = table.table_selection();
table.clear_table_selection();
table.move_column(0, 2);

tree.set_tree_renamable(true).on_tree_rename(on_tree_rename);
tree.begin_tree_rename(beta);
tree.commit_tree_rename();

label.set_text_spans({{0, 5, pt_font_weight_bold, pt_font_slant_upright, 0,
                       pt_text_span_has_family | pt_text_span_has_size,
                       "JetBrains Mono", 15.0f}});
```

- Theme lookups are per language: `app::set/get_theme_color`,
  `app::set/get_theme_metric`, `window::set/get_theme_color`,
  `window::set/get_theme_metric` and `window::clear_theme_overrides()`. A
  window getter falls through to the app value when the token is not
  overridden.
- `pt::chip(parent, text)` and `pt::skeleton(parent)` are factories like
  `pt::label`; chip state is `set_chip_icon`, `set_chip_removable`,
  `set_chip_selected` plus the `chip_removable`/`chip_selected` getters, and
  `on_chip_click`/`on_chip_remove` reuse `pt_click_callback`.
- Table editing is `set_table_editable`, `on_cell_edited`, `begin_cell_edit`,
  `commit_cell_edit` and `cancel_cell_edit`; selection is `select_range`,
  `table_selection()` (a `std::vector<uint32_t>`), `table_selection_count()`,
  `is_row_selected` and `clear_table_selection()`; column order is
  `set_columns_reorderable` with `move_column` and `on_columns_reordered`, and
  `on_row_drag` reports the dragged row.
- Tree renaming is `set_tree_renamable`, `begin_tree_rename`,
  `commit_tree_rename`, `cancel_tree_rename`, `tree_renaming()` and
  `on_tree_rename`; `commit_tree_rename()` returns `false` only when no editor
  is open.
- `pt_text_span` gained `family` and `size`, selected by
  `pt_text_span_has_family` and `pt_text_span_has_size`.
- `window::begin_text_drag` and `begin_uri_drag` were marked `[[deprecated]]` in
  0.9; the wrappers forwarded to the deprecated C entry points inside a
  diagnostic pragma so the parity scan saw them. Both were removed at the 1.0
  freeze, so `begin_drag` with a `pt_drag_data` list is the only drag source.

New in 0.10:

```cpp
auto region = pt::combobox(root);             // pt::combobox factory
region.add_item("Europe").insert_item(1, "Asia");
region.set_item_text(0, "Europe/Berlin").set_item_disabled(1, true);
region.set_item_data(0, &continent);
std::string first = region.item_text(0);
void *payload = region.item_data(0);
uint32_t count = region.item_count();
bool skipped = region.is_item_disabled(1);

region.set_combo_mode(pt_combobox_filterable);
pt::combo_mode mode = region.combo_mode();
region.set_combo_selected(2);                 // silent: no on_change
region.set_combo_text("Ber");
region.set_combo_placeholder("Choose a region");
region.open_popup();
bool open = region.is_popup_open();
region.set_popup_max_height(320.0f);
float max_height = region.popup_max_height();
region.close_popup();
region.on_combo_change(choose_region).on_combo_text_change(typed_region);
region.clear_items();

list.list_select_range(2, 4).list_select_toggle(6);
std::vector<uint32_t> rows = list.list_selection();
bool held = list.is_list_row_selected(3);
uint32_t held_count = list.list_selection_count();
list.clear_list_selection();

table.select_toggle(4);                       // extends select_range(1, 3)

tree.tree_select_range(1, 3).tree_select_toggle(beta);
std::vector<pt::tree_node *> nodes = tree.tree_selection();
bool node_held = tree.is_tree_node_selected(alpha);
tree.clear_tree_selection();
```

- `pt::combobox(parent)` is a factory like `pt::label`. `add_item`,
  `insert_item`, `remove_item` and `clear_items` maintain the list;
  `item_count()`, `item_text`, `set_item_text`, `set_item_data`, `item_data`,
  `set_item_disabled` and `is_item_disabled` are the per-item accessors.
- Mode is `set_combo_mode`/`combo_mode()` typed as `pt::combo_mode` (an alias
  of `pt_combobox_mode`). The getter and setter are named `combo_*` because the
  `widget` class already carries table `column` and tree accessors; inside the
  class the return and parameter types must be written `pt::combo_mode`, since
  a member named `combo_mode` hides the alias in class scope.
- `set_combo_selected(index)` and `set_combo_text(text)` are silent like every
  other `set_selected` in the API, and both report `pt_list_index_none` for an
  empty selection. `combo_text()` and `combo_placeholder()` return
  `std::string` and map `NULL` to the empty string. `open_popup`, `close_popup`,
  `is_popup_open()`, `set_popup_max_height`/`popup_max_height()` drive the
  panel. `on_combo_change` reuses `pt_index_callback`;
  `on_combo_text_change` takes the new `pt_combobox_text_callback`.
- Multi-row selection now matches across the three data views. The list uses
  `list_select_range`, `list_select_toggle`, `list_selection()` (a
  `std::vector<uint32_t>`), `list_selection_count()`, `is_list_row_selected`
  and `clear_list_selection()`; the table gains `select_toggle` next to its
  0.9 `select_range`/`table_selection()` pair; the tree uses
  `tree_select_range`, `tree_select_toggle`, `tree_selection()` (a
  `std::vector<pt::tree_node *>` in document order, hidden nodes included),
  `tree_selection_count()`, `is_tree_node_selected` and
  `clear_tree_selection()`.

Important details:

- `pt::app` is move-only and calls `pt_app_destroy` in its destructor. Destroy
  it after all widget and window handles are no longer used.
- `pt::window` and `pt::widget` have no destructor and do not own anything.
  `window.root()` returns a handle to the window's owned root.
- Callbacks use the raw C function-pointer signatures, for example
  `void (*)(pt_widget *, void *)`. Capture state in the `void *data` argument;
  lambdas without captures convert, but `std::function` does not.
- `window.on_close(callback, data)` returns a `pt::window &`; the callback
  returns `false` to veto a close.
- `pt::app::run_for` is available for tests and embedded loops.

Build against the source tree:

```sh
g++ -std=c++23 -Iinclude -Ibindings/cpp app.cpp \
    build/libpt.a $(pkg-config --libs wayland-client wayland-cursor xkbcommon \
    fontconfig freetype2 harfbuzz zlib) -lm -o app
```

Build against an installed copy:

```sh
g++ -std=c++23 app.cpp $(pkg-config --cflags --libs pt) -o app
```

The C++ wrapper is covered by `bindings/cpp/test_pt.cpp` and runs as part of
`make check-bindings`.

## Python

`bindings/python/pt.py` is a self-contained ctypes binding. It has no
third-party dependencies and requires Python 3.10 or newer.

```python
import pt


def main() -> int:
    app = pt.App("dev.pt.python")
    app.color_scheme = pt.color_scheme_dark
    app.accent_color = pt.rgb(0x9ECE6A)

    window = pt.Window(app, "Counter", 480, 320)
    box = pt.column(window.root).set_padding(24, 24, 24, 24).set_gap(12)

    state = {"count": 0}
    value = pt.label(box, "0").set_font_size(40)

    def add() -> None:
        state["count"] += 1
        value.text = str(state["count"])

    pt.label(box, "Counter").set_font_size(24)
    pt.button(box, "Add").set_button_variant(pt.button_primary).on_click(add)

    def on_key(_window, key, _modifiers) -> bool:
        if key == 0xFF1B:
            window.close()
            return True
        return False

    window.on_key(on_key)
    window.show()
    return app.run()


if __name__ == "__main__":
    raise SystemExit(main())
```

Python callback signatures deliberately hide the native arguments:

| Method | Python callback |
| --- | --- |
| `Widget.on_click` | `callback()` |
| `Widget.on_submit` | `callback()` |
| `Widget.on_change` | `callback(text: str)`; entries and text areas |
| `Widget.on_toggle` / `Widget.on_checked` | `callback(checked: bool)` |
| `Widget.on_value_change` | `callback(value: float)` |
| `Widget.on_select` | `callback(index: int)` |
| `Widget.on_bind_row` | `callback(row: Widget, index: int)` |
| `Widget.on_row_select` / `on_row_activate` | `callback(index: int)` |
| `Widget.on_bind_cell` | `callback(cell: Widget, row: int, column: int)` |
| `Widget.on_table_select` / `on_table_activate` | `callback(row: int)` |
| `Widget.on_table_sort` | `callback(column: int, direction: int)` |
| `Widget.on_sort_columns` | `callback(count: int, columns: list[int], directions: list[int])` |
| `Widget.set_row_key_callback` | `callback(view: Widget, row: int) -> int` |
| `Widget.set_row_text_callback` | `callback(view: Widget, row: int) -> str` |
| `Widget.on_tree_select` / `on_tree_activate` | `callback(node: TreeNode)` |
| `Widget.on_tab_change` / `on_sidebar_select` | `callback(index: int)` |
| `Window.on_close` | `callback(window) -> bool` (return `False` to veto) |
| `Window.on_resize` | `callback(window, width, height)` |
| `Window.on_key` | `callback(window, key, modifiers) -> bool` |
| `Window.on_gesture` | `callback(window, event)`; event is a `GestureEvent` |
| `Window.on_stylus` | `callback(window, event)`; event is a `StylusEvent` |
| `Widget.set_drop_target` | `enter(widget, mime) -> bool`, `motion(widget, x, y)`, `leave(widget)`, `drop(widget, mime, text, x, y)` |
| `Menu.add_item` | `callback()` after the menu closes |
| `Dialog.add_action` | `callback()` after the dialog closes |
| `Dialog.on_close` | `callback(dialog)` |
| `App.on_a11y_event` | `callback(window, widget, event)`; window/widget may be `None` |
| `App.add_timeout` | `callback()` |

The binding retains ctypes trampolines and window wrappers in module-level
tables. Do not discard them yourself. Timers remove their own trampoline when
fired; `App.cancel_timeout` removes it explicitly.

`Widget` exposes `text`, `bounds` (a `(x, y, width, height)` tuple), `checked`,
`value`, `progress`, `indeterminate`, `selected`, `selection` (a
`(start, end)` tuple or `None`), `scroll_offset`, `max_scroll_offset`,
`focused`, `kind`, `parent`, `drop_hover` and `drag_enabled`, plus fluent
setters for layout, appearance and callbacks. Entry helpers are `select_all`,
`set_selection`, `undo`, `redo` and `set_drag_enabled`.

The 0.4 interaction surface is `Widget.set_drop_target(enter, motion, leave,
drop)` (calling it with no callbacks clears the target), `Window.on_gesture`,
`Window.set_long_press_delay(milliseconds)`, the `Window.momentum_scrolling`
property and `Window.begin_text_drag(text)` (removed in 1.0). Gesture callbacks
receive a
`GestureEvent` with `kind`, `phase`, `fingers`, `x`, `y`, `dx`, `dy`, `scale`,
`rotation`, `target` and the `target_widget` helper property. Compare `kind`
and `phase` against `pt.gesture_kind_*` and `pt.gesture_phase_*`.

The 0.5 surface is `pt.text_area(parent, placeholder)`, `pt.wrap(parent)`,
`pt.grid(parent, columns)` and `pt.list_view(parent)`. `Widget` gains
`grow_to_content`, `grid_columns`, `grid_span`, `row_count`, `row_height` and
`selected_row` properties plus `set_grow_to_content`, `scroll_to_caret`,
`set_wrap_gap`, `set_grid_columns`, `set_grid_gap`, `set_grid_span`,
`set_row_count`, `set_row_height`, `set_selected_row`, `scroll_to_row`,
`refresh`, `on_bind_row`, `on_row_select` and `on_row_activate`. The `set_*`
layout and list methods return the widget, so they chain in factories.
`selected_row` is `pt.list_index_none` when no row is selected. `on_change`,
`set_placeholder`, `select_all`, `set_selection`, `selection`, `undo` and
`redo` dispatch to the text-area API when the widget kind is a text area.

Motion is exposed as `pt.ease(easing, t)`, `pt.ease_value(easing, start, end,
t)`, the `pt.easing_*` constants and a caller-owned `Transition` class with
`value`, `active`, `to(target, duration_ms, easing)`, `advance(dt)` and
`set(value)`. `App.reduced_motion` reads and writes the app preference.

The 0.6 factories are `pt.icon(parent, glyph)`, `pt.icon_button(parent, glyph,
accessible_name)`, `pt.table(parent)`, `pt.tree(parent)`, `pt.tabs(parent)` and
`pt.sidebar(parent)`. `Widget` properties are `icon`, `icon_size`,
`column_count`, `table_row_count`, `table_row_height`, `table_selected`,
`sort` (a `(direction, column)` tuple), `active_tab`, `tab_count`,
`sidebar_item_count`, `active_sidebar_item`, `sidebar_compact` and
`sidebar_header`. Fluent methods mirror the C names: `set_icon`,
`set_icon_size`, `set_column_count`, `set_column_title`, `set_column_width`,
`column_width`, `set_column_align`, `set_column_resizable`,
`set_header_height`, `set_table_row_count`, `set_table_row_height`,
`set_table_selected`, `set_sort`, `table_refresh`, `table_scroll_to_row`,
`scroll_to_column`, `add_tree_node`, `tree_root`, `tree_visible_node`,
`set_tree_selected`, `set_tree_row_height`, `set_tree_indent`, `tree_refresh`,
`scroll_to_tree_node`, `clear_tree`, `add_tab`, `remove_tab`, `tab_page`,
`set_tab_title`, `tab_title`, `set_active_tab`, `set_tab_strip_height`,
`add_sidebar_item`, `sidebar_item`, `set_active_sidebar_item`,
`set_sidebar_compact` and `set_sidebar_header`.

`TreeNode` wraps an opaque node handle with `text`, `expanded`,
`has_children`, `depth`, `parent`, `child_count`, `children`, `data`,
`child(index)` and `set_expanded(expanded=True)`. Add children through the
owning widget's `add_tree_node(parent, text)`.

The 0.6 interaction additions are `Window.begin_uri_drag(uris: list[str])`
(removed in 1.0),
`Window.set_drag_image(pixels, width, height)` and
`Window.clear_drag_image()`, plus `Window.on_stylus`. `StylusEvent` carries
`phase`, `tool`, `x`, `y`, `pressure`, `distance`, `tilt_x`, `tilt_y` and
`rotation`. Module helpers `pt.uri_list_count(text)`, `pt.uri_list_get(text,
index)` and `pt.uri_list(text)` parse `text/uri-list` payloads. Icon constants
are `pt.icon_*`, sort constants `pt.sort_*` and `pt.table_column_none` clears
the table sort.

New in 0.7:

```python
def key_of(view, row):
    return items[row].id

def label_of(view, row):
    return items[row].name

table = pt.table(page)
table.set_column_hidden(3, True)
table.set_table_row_key_callback(key_of)
table.set_table_row_text_callback(label_of)
table.on_sort_columns(lambda count, columns, directions: resort(columns, directions))
table.set_sort_columns([(1, pt.sort_ascending), (0, pt.sort_ascending)])
table.table_refresh_rows(2, 4)
state = table.sort_columns          # [(column, direction), ...] in priority order
key = table.table_selected_key      # pt.row_key_none when unselected
table.set_table_selected_key(key)

rows = pt.list_view(page)
rows.set_row_key_callback(key_of)
rows.set_row_text_callback(label_of)
rows.refresh_rows(0, 20)
rows.set_selected_key(7107)

window.popup_policy = pt.popup_policy_window
window.drag_timeout = 800
window.cancel_drag()
moving = window.drag_active
status = app.a11y_status()                       # enable_a11y() / disable_a11y()
```

- `App.enable_a11y()`, `App.disable_a11y()` and `App.a11y_status` expose the
  bridge; status constants are `pt.a11y_status_*`.
- `Window.popup_policy`, `drag_active`, `drag_timeout` and `cancel_drag()` cover
  native-popup policy and the drag lifecycle.
- List methods are `refresh_rows(first, count)`,
  `set_row_key_callback(callback)`, the `selected_key` property,
  `set_selected_key(key)` and `set_row_text_callback(callback)`.
- Table methods are `set_column_hidden(column, hidden=True)`,
  `column_hidden(column)`, `on_sort_columns(callback)`,
  `set_sort_columns([(column, direction), ...])`, the `sort_columns` property
  (a list of `(column, direction)` tuples in priority order), `table_refresh_rows`,
  `set_table_row_key_callback`, the `table_selected_key` property,
  `set_table_selected_key` and `set_table_row_text_callback`.
- Key callbacks return a non-zero `int`; `pt.row_key_none` clears a keyed
  selection. Text callbacks return `str` and are borrowed only for the duration
  of the type-ahead lookup. Direct `set_sort_columns` does not invoke the sort
  callback; header clicks do.

New in 0.8:

```python
def pick_offer(widget, offer):
    for index in range(offer.mime_count):
        if offer.mime(index) in ("text/uri-list", "text/plain"):
            return offer.mime(index)      # None declines the drag
    return None

def on_offer_drop(widget, offer, mime, x, y):
    received = bytearray()

    def on_stream(widget, stream, state, data):
        if state == pt.drop_stream_data:
            received.extend(data)         # raw bytes, never decoded
        else:                             # done / cancelled / error
            print(f"{mime} -> {len(received)} bytes")

    offer.read(mime, 0, on_stream)        # max_bytes 0 means unlimited

card.set_drop_target(enter_offer=pick_offer, drop_offer=on_offer_drop)
hovering = card.drop_hover

label.set_text_spans([
    pt.TextSpan(start=0, length=5, weight=pt.font_weight_bold,
                slant=pt.font_slant_upright, color=pt.rgb(0xDC8A1C),
                flags=pt.text_span_has_weight | pt.text_span_has_color),
    pt.TextSpan(start=19, length=4, flags=pt.text_span_underline),
])
label.text_span_count()                   # int
label.text_span(0)                        # TextSpan or None
label.clear_text_spans()
entry.set_input_purpose(pt.input_purpose_email)
entry.input_purpose()                     # int

window.set_drag_finished_callback(
    lambda win, dropped, action: win.cancel_drag() if not dropped else None)
window.begin_drag([("text/plain", payload_text),
                   ("application/x-pt-row", row_bytes, len(row_bytes))])
window.drag_result                        # pt.drag_action_*
```

- `set_drop_target` gained the keyword pair `enter_offer`/`drop_offer`, which
  takes precedence over `enter`/`drop`. `enter_offer` returns the MIME type to
  accept (a `str` drawn from the offer, or `None` to decline) and `drop_offer`
  receives a `DropOffer`.
- `DropOffer` exposes the `mime_count`, `selected_mime` and `actions` properties,
  `mime(index)`, `has_mime(str)` and `read(mime, max_bytes=0, callback=None)`.
  `read` returns a `DropStream` or `None`, and its callback receives
  `(widget, stream, state, bytes)`. `DropStream` has `mime`, `size` (bytes
  delivered so far), `active` and `cancel()`. Pt releases the native stream
  after its final state callback, and the handle then becomes NULL-safe.
- `pt.TextSpan(start, length, weight, slant, color, flags)` is set with
  `set_text_spans(list)` and read back with `text_span_count()` and
  `text_span(index)`; `clear_text_spans()` removes them. `set_text()` also
  clears them because the byte offsets no longer describe the new string.
- Input purpose uses `pt.input_purpose_*` and is a method pair, not a property.
  Drag sources use `begin_drag` with `(mime, data)` pairs,
  `(mime, data, size)` triples, mappings or `pt.DragData`; a size of zero means
  NUL-terminated text, so pass an explicit size for payloads that may contain
  embedded NUL bytes. Constants are `pt.drag_action_*` and
  `pt.drop_stream_*`; colors are built with `pt.rgb`/`pt.rgba`.

New in 0.9:

```python
app.set_theme_metric(pt.theme_metric_chip_height, 30.0)
app.get_theme_metric(pt.theme_metric_radius_small)     # float

window.set_theme_color(pt.theme_background, pt.rgb(0x102030))
window.set_theme_metric(pt.theme_metric_focus_width, 3.0)
window.clear_theme_overrides()

chips = pt.row(root)
tag = pt.chip(chips, "design").set_chip_icon(pt.icon_folder)
tag.set_chip_removable(True)
tag.chip_selected = True                    # property or setter
tag.on_chip_click(lambda: print("filter"))
tag.on_chip_remove(lambda: tag.set_visible(False))
pt.skeleton(chips).set_height(12)

button.busy = True
email.invalid = True

table.set_table_editable(True)
table.on_cell_edited(lambda table, row, column, text: update(row, column, text))
table.begin_cell_edit(1, 0)
table.commit_cell_edit()
table.select_range(1, 3)
table.table_selection()                     # [1, 2, 3]
table.clear_table_selection()
table.set_columns_reorderable(True)
table.on_columns_reordered(lambda table, source, target: reorder(source, target))
table.on_row_drag(lambda table, row: move(row))

tree.set_tree_renamable(True)
tree.begin_tree_rename(node)
tree.commit_tree_rename()                   # False when no editor is open

label.set_text_spans([pt.TextSpan(
    start=0, length=5, flags=pt.text_span_has_family | pt.text_span_has_size,
    family=b"DejaVu Serif", size=15.0)])
```

- `App` adds `set_theme_metric`/`get_theme_metric`; `Window` adds
  `set_theme_color`/`get_theme_color`, `set_theme_metric`/`get_theme_metric`
  and `clear_theme_overrides()`. A window getter returns the app value when the
  token is not overridden.
- Factories `pt.chip(parent, text)` and `pt.skeleton(parent)` join the rest.
  Chip state is `set_chip_icon`, `set_chip_removable`, `set_chip_selected` with
  `chip_removable`/`chip_selected` properties, and the callbacks are
  zero-argument `on_chip_click`/`on_chip_remove`.
- `Widget.busy` and `Widget.invalid` are properties with `set_busy`/
  `set_invalid` chaining.
- Table editing is `set_table_editable`, `begin_cell_edit`,
  `commit_cell_edit`, `cancel_cell_edit` and `on_cell_edited`; selection is
  `select_range`, `table_selection()`, `table_selection_count()`,
  `is_row_selected()` and `clear_table_selection()`; ordering is
  `set_columns_reorderable`, `move_column`, `on_columns_reordered` and
  `on_row_drag`.
- Tree renaming is `set_tree_renamable`, `begin_tree_rename`,
  `commit_tree_rename`, `cancel_tree_rename`, `tree_renaming()` and
  `on_tree_rename`.
- `pt.TextSpan` carries `family` (a `bytes` value for the `c_char * 64` field)
  and `size`, selected by `pt.text_span_has_family` and
  `pt.text_span_has_size`.
- `Window.begin_text_drag` and `begin_uri_drag` raised `DeprecationWarning` from
  the wrapper itself in 0.9, so `python -W error` and pytest's default filters
  surfaced the migration to `begin_drag`, and the binding tests called them
  inside `assertWarns(DeprecationWarning)`. Both methods were removed at the 1.0
  freeze together with the C entry points they wrapped.

New in 0.10:

```python
region = pt.combobox(root)                    # pt.combobox factory
region.add_item("Europe").insert_item(1, "Asia")
region.set_item_text(0, "Europe/Berlin").set_item_disabled(1, True)
region.set_item_data(0, model_row)            # the object stays alive
region.item_text(0)                           # 'Europe/Berlin'
region.item_data(0)                           # model_row
region.item_count                             # 2
region.is_item_disabled(1)                    # True

region.combo_mode = pt.combobox_filterable
region.combo_selected = 2                     # silent: no on_change
region.combo_text = "Ber"
region.combo_placeholder = "Choose a region"
region.open_popup()
region.popup_open                             # True
region.popup_max_height = 320.0
region.close_popup()
region.on_combo_change(lambda combo, index: choose(index))
region.on_combo_text_change(lambda combo, text: print(text))
region.remove_item(0)
region.clear_items()
region.combo_selected                         # pt.list_index_none once empty

rows.list_select_range(2, 4).list_select_toggle(6)
rows.list_selection()                         # [2, 3, 4, 6]
rows.is_list_row_selected(3)                  # True
rows.list_selection_count                     # 4
rows.clear_list_selection()

table.select_toggle(4)                        # extends select_range(1, 3)

tree.tree_select_range(1, 3).tree_select_toggle(beta)
tree.tree_selection()                         # TreeNode objects, document order
tree.is_tree_node_selected(alpha)
tree.tree_selection_count
tree.clear_tree_selection()
```

- `pt.combobox(parent)` is a factory like `pt.label`. `add_item`,
  `insert_item`, `remove_item` and `clear_items` maintain the list;
  `item_count`, `item_text`, `set_item_text`, `set_item_data`, `item_data`,
  `set_item_disabled` and `is_item_disabled` are the per-item accessors.
- `pt.combobox_select_only` and `pt.combobox_filterable` join the other module
  constants. `combo_mode`, `combo_selected`, `combo_text`,
  `combo_placeholder` and `popup_max_height` are properties with a chaining
  `set_combo_mode`/`set_combo_selected`/`set_combo_text`/
  `set_combo_placeholder`/`set_popup_max_height` twin; `open_popup` and
  `close_popup` chain and `popup_open` is read-only. Setting a selection or
  text through the property never fires a callback, and an empty selection
  reads back as `pt.list_index_none`.
- `set_item_data(index, data)` keeps a `dict` of Python objects on the widget
  keyed by item index and re-keys it when items are inserted or removed, so the
  mapping follows the C shift; `data=None` drops the entry. `item_data(index)`
  returns that object when Python set it, otherwise the raw native pointer as an
  `int` — the wrapper never guesses a Python object for a value the C side
  stored.
- `on_combo_change` dispatches `(widget, index)` and `on_combo_text_change`
  dispatches `(widget, text)` with the `char *` decoded to `str`; both keep
  their ctypes trampolines alive on the widget.
- Selection accessors are parallel across the data views: `list_select_range`,
  `list_select_toggle`, `list_selection()`, `list_selection_count`,
  `is_list_row_selected()` and `clear_list_selection()` on the list; the table
  gains `select_toggle` beside its 0.9 pair; `tree_select_range` (visible rows),
  `tree_select_toggle`, `tree_selection()`, `tree_selection_count`,
  `is_tree_node_selected()` and `clear_tree_selection()` on the tree.
  `pt.a11y_role_combo_box`, `pt.a11y_role_list_box`, `pt.a11y_state_selectable`
  and `pt.a11y_state_multi_selectable` expose the new accessibility values.

The 0.3 properties are `a11y_role`, `a11y_name`, `a11y_description` and
`a11y_state` (bitmask), plus the read-only `a11y_text` (a
`(text, caret, selection_start, selection_end)` tuple or `None`) and
`a11y_value` (`float` or `None`). `set_tooltip(text)` and
`set_tooltip_delay(milliseconds)` attach a delayed tooltip.

`Window` exposes `root`, `is_open`, `width`, `height`, `scale`, `clipboard`,
`show`, `hide`, `close`, `set_title`, `set_size`, `set_min_size`,
`set_resizable`, `set_decorated`, `request_redraw`, `focus`, `on_gesture`,
`set_long_press_delay`, `momentum_scrolling`, `begin_drag(types)`,
`set_drag_image`, `clear_drag_image`, `on_stylus` and
`show_toast(text, kind, duration_ms)` returning a `Toast`. `Menu(window)`
provides `add_item`, `add_separator`, `show_at`, `show_below`, `dismiss`,
`is_open` and `destroy`; `Dialog(window, title, message)` provides
`set_title`, `set_message`, `add_action`, `on_close`, `show`, `close`,
`is_open` and `destroy`. Menus and dialogs are caller-owned; call `destroy()`
once they are dismissed and before the window closes.

`App` exposes `run`, `run_for`, `quit`, `running`, `add_timeout`,
`cancel_timeout`, `color_scheme`, `accent_color`, `reduced_motion`,
`set_theme_color`, `get_theme_color`, `on_a11y_event`, `enable_a11y`,
`disable_a11y` and `a11y_status`. The accessibility event constants are
`pt.a11y_event_*`, roles are `pt.a11y_role_*` and states are `pt.a11y_state_*`;
combine states with bitwise or. Toast kinds are `pt.toast_info`,
`pt.toast_success`, `pt.toast_warning` and `pt.toast_error`.

Colors and enums are module constants; `pt.rgb(0xRRGGBB)` and
`pt.rgba(r, g, b, a)` build packed `0xRRGGBBAA` values, and the
`pt.theme_*` constants name every design token. Factory functions are `box`,
`row`, `column`, `label`, `button`, `entry`, `text_area`, `toggle`, `checkbox`,
`radio`, `slider`, `progress`, `spinner`, `segmented`, `scroll`, `wrap`,
`grid`, `list_view`, `divider`, `card`, `spacer`, `icon`, `icon_button`,
`table`, `tree`, `tabs` and `sidebar`.

Run from the source tree:

```sh
make run-python
# or
PT_LIBRARY="$PWD/build/libpt.so" PYTHONPATH=bindings/python \
    python3 examples/python/counter.py
```

The loader checks `PT_LIBRARY`, then `<repo>/build/libpt.so`, then
`./build/libpt.so`, then the system loader path. After installation the module
is at `$PREFIX/share/pt/bindings/python/pt.py`; either add that directory to
`PYTHONPATH` or copy `pt.py` next to your application. Set `LD_LIBRARY_PATH` if
the library is installed in a non-standard prefix.

Headless tests:

```sh
PT_HEADLESS=1 PT_LIBRARY="$PWD/build/libpt.so" PYTHONPATH=bindings/python \
    python3 -m unittest discover -s bindings/python -p 'test_*.py'
```

`make check-bindings` runs exactly this.

## Java

`bindings/java/src/pt/Pt.java` uses the Foreign Function & Memory API. There is
no JNI, no generated glue and no runtime dependency beyond the JDK. The API is
a direct snake_case mirror of the C ABI; handles are `MemorySegment`s and
strings cross as NUL-terminated UTF-8.

```java
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import pt.Pt;

public final class Hello {
    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment app = Pt.app_create("dev.pt.java");
            if (app.address() == 0) {
                System.err.println(Pt.last_error());
                return;
            }

            MemorySegment window = Pt.window_create(app, "Counter", 480, 320);
            MemorySegment box = Pt.column(Pt.window_root(window));
            Pt.widget_set_padding(box, 24.0f, 24.0f, 24.0f, 24.0f);
            Pt.widget_set_gap(box, 12.0f);

            MemorySegment value = Pt.label(box, "0");
            Pt.widget_set_font_size(value, 40.0f);
            int[] count = {0};

            MemorySegment add = Pt.button(box, "Add");
            Pt.button_set_on_click(add, (widget, data) -> {
                count[0] += 1;
                Pt.widget_set_text(value, Integer.toString(count[0]));
            }, arena);

            Pt.window_show(window);
            int status = Pt.app_run(app);
            Pt.app_destroy(app);
            if (status != 0) {
                System.exit(status);
            }
        }
    }
}
```

Callbacks are explicit functional interfaces and every registration method
takes an `Arena`:

| Registration | Interface |
| --- | --- |
| `button_set_on_click(button, handler, arena)` | `ClickHandler` |
| `entry_set_on_change(entry, handler, arena)` | `TextHandler` |
| `text_area_set_on_change(area, handler, arena)` | `TextHandler` |
| `list_view_set_on_bind_row(list, handler, arena)` | `ListBindHandler` |
| `list_view_set_on_select/activate(list, handler, arena)` | `IndexHandler` |
| `table_set_on_bind_cell(table, handler, arena)` | `TableBindHandler` |
| `table_set_on_select/activate(table, handler, arena)` | `IndexHandler` |
| `table_set_on_sort(table, handler, arena)` | `SortHandler` |
| `tree_set_on_select/activate(tree, handler, arena)` | `TreeNodeHandler` |
| `tabs_set_on_change` / `sidebar_set_on_select` | `IndexHandler` |
| `window_set_stylus_handler(window, handler, arena)` | `StylusHandler` |
| `toggle_set_on_change(toggle, handler, arena)` | `ValueHandler` |
| `widget_set_on_checked(widget, handler, arena)` | `ValueHandler` |
| `slider_set_on_change(slider, handler, arena)` | `ValueHandler` |
| `segmented_set_on_change(segmented, handler, arena)` | `IndexHandler` |
| `window_set_close_handler(window, handler, arena)` | `CloseHandler` |
| `window_set_key_handler(window, handler, arena)` | `KeyHandler` |
| `menu_add_item(menu, label, shortcut, handler, arena)` | `ClickHandler` |
| `dialog_add_action(dialog, label, primary, handler, arena)` | `ClickHandler` |
| `dialog_set_on_close(dialog, handler, arena)` | `DialogHandler` |
| `app_set_a11y_handler(app, handler, arena)` | `A11yHandler` |
| `app_add_timeout(app, ms, handler, arena)` | `TimerHandler` |

The upcall stubs and any captured Java objects must remain reachable for as
long as the native callback may fire. Use an arena that lives for the whole app
lifetime; do not register a callback with a confined arena that is closed
before `app_run` returns. The close and key handlers return `boolean`;
returning `false` from the close handler vetoes the close.

The 0.2 surface adds `checkbox`, `radio`, `progress`, `spinner` and
`segmented` factories (labels cross as a `String[]`), the generic
`widget_get_checked` / `widget_set_checked` pair, progress and scroll
accessors, `entry_select_all`, `entry_set_selection`,
`entry_get_selection` (returns an `int[]` or `null`), `entry_undo`,
`entry_redo`, `window_clipboard_set/get`, `app_get/set_theme_color` and the
`theme_*` token constants.

The 0.3 surface adds:

- `menu_create`, `menu_add_item`, `menu_add_separator`, `menu_show_at`,
  `menu_show_below`, `menu_dismiss`, `menu_is_open` and `menu_destroy`.
- `dialog_create`, `dialog_set_title`, `dialog_set_message`,
  `dialog_add_action`, `dialog_show`, `dialog_close`, `dialog_is_open`,
  `dialog_set_on_close` and `dialog_destroy`.
- `window_show_toast`, `toast_dismiss`, `toast_is_visible` and the
  `toast_*` kind constants.
- `widget_set_tooltip`, `widget_set_tooltip_delay`,
  `widget_get/set_a11y_role`, `widget_get/set_a11y_name`,
  `widget_get/set_a11y_description`, `widget_get_a11y_state`,
  `widget_get_a11y_text` (returns an `A11yText` record or `null`) and
  `widget_get_a11y_value` (returns a `Float` or `null`).
- `app_set_a11y_handler`, with `A11yHandler.handle(window, widget, event,
  data)`. `A11yText` carries the text plus caret and selection byte offsets.

Callbacks registered through `menu_add_item` and `dialog_add_action` use the
existing `ClickHandler`. Menus and dialogs are native objects; call the
corresponding `*_destroy` method when they are no longer needed. The returned
toast segment is a non-owning handle owned by the window.

The 0.4 surface adds:

- `entry_set_drag_enabled`, `entry_get_drag_enabled` and
  `window_begin_text_drag` (removed in 1.0).
- `widget_get_drop_hover`, `widget_set_drop_target(widget, enter, motion,
  leave, drop, arena)` and `widget_clear_drop_target`. Drop handlers receive
  MIME types and dropped text as Java `String`s.
- `window_set_gesture_handler`, with `GestureHandler.handle(window, event,
  data)`. `GestureEvent` carries `kind`, `phase`, `fingers`, `x`, `y`, `dx`,
  `dy`, `scale`, `rotation` and the native `target` segment.
- `window_set_long_press_delay`, `window_set_momentum_scrolling` and
  `window_get_momentum_scrolling`.
- The `gesture_kind_*` and `gesture_phase_*` constants.

The 0.5 surface adds:

- Factories `text_area(parent, placeholder)`, `wrap(parent)`,
  `grid(parent, columns)` and `list_view(parent)`.
- Explicit text-area methods: `text_area_set_placeholder`,
  `text_area_set_on_change`, `text_area_set/get_grow_to_content`,
  `text_area_scroll_to_caret`, `text_area_select_all`,
  `text_area_set_selection`, `text_area_get_selection` (an `int[]` or `null`)
  and `text_area_undo`/`text_area_redo`.
- `wrap_set_gap`, `grid_set_columns`, `grid_get_columns`, `grid_set_gap`,
  `widget_set_grid_span` and `widget_get_grid_span`.
- List methods `list_view_set/get_row_count`, `list_view_set/get_row_height`,
  `list_view_set_on_bind_row`, `list_view_set_on_select`,
  `list_view_set_on_activate`, `list_view_get/set_selected`,
  `list_view_refresh` and `list_view_scroll_to_row`. `ListBindHandler` is
  `handle(list, row, index, data)`; select and activate use the existing
  `IndexHandler`. `list_index_none` (`-1`) clears the selection.
- `app_set_reduced_motion`, `app_get_reduced_motion`, `ease`, `ease_value`, the
  `easing_*` constants and `Pt.Transition`. A transition owns a confined
  `Arena` and implements `AutoCloseable`; `value()`, `active()`,
  `to(target, duration_ms, easing)`, `advance(dt)`, `set(value)` and `state()`
  expose the native 28-byte record.

```java
try (Pt.Transition fade = new Pt.Transition(0.0f);
     Arena arena = Arena.ofConfined()) {
    MemorySegment list = Pt.list_view(sidebar);
    Pt.list_view_set_row_height(list, 44.0f);
    Pt.list_view_set_row_count(list, 10000);
    Pt.list_view_set_on_bind_row(list, (l, row, index, data) ->
        Pt.widget_set_text(row, "Row " + index), arena);

    MemorySegment statsGrid = Pt.grid(stats, 3);
    Pt.widget_set_grid_span(Pt.card(statsGrid), 2);
    Pt.text_area_set_grow_to_content(Pt.text_area(pane, "Note"), true);
    Pt.app_set_reduced_motion(app, true);
    fade.to(1.0f, 180, Pt.easing_out_cubic);
}
```

Close a transition only after the last native call that uses its state; the
native struct is owned by the transition's arena.

The 0.6 surface adds:

- Factories `icon(parent, glyph)`, `icon_button(parent, glyph, accessibleName)`,
  `table(parent)`, `tree(parent)`, `tabs(parent)` and `sidebar(parent)`, plus the
  `icon_*`, `sort_*` and `table_column_none` constants.
- Table functions `table_set/get_column_count`, `table_set_column_title`,
  `table_set/get_column_width`, `table_set_column_align`,
  `table_set_column_resizable`, `table_set/get_row_count`,
  `table_set/get_row_height`, `table_set_header_height`,
  `table_set_on_bind_cell`, `table_set_on_select`, `table_set_on_activate`,
  `table_set_on_sort`, `table_set_sort`, `table_get_sort` (an
  `int[]{direction, column}`), `table_get_selected`, `table_set_selected`,
  `table_refresh`, `table_scroll_to_row` and `table_scroll_to_column`.
  `TableBindHandler` is `handle(table, cell, row, column, data)` and
  `SortHandler` is `handle(table, column, direction, data)`.
- Tree functions `tree_add_node`, `tree_clear`, `tree_get_root_count`,
  `tree_get_root_node`, `tree_node_get/set_text`, `tree_node_get/set_expanded`,
  `tree_node_has_children`, `tree_node_get_depth`, `tree_node_parent`,
  `tree_node_get_child_count`, `tree_node_get_child`, `tree_node_get/set_data`,
  `tree_get_visible_count`, `tree_get_visible_node`, `tree_get/set_selected`,
  `tree_set_row_height`, `tree_set_indent`, `tree_set_on_select`,
  `tree_set_on_activate`, `tree_refresh` and `tree_scroll_to_node`.
  `TreeNodeHandler` is `handle(tree, node, data)`.
- Tabs functions `tabs_add_page`, `tabs_remove_page`, `tabs_get_page_count`,
  `tabs_get_page`, `tabs_set/get_page_title`, `tabs_get/set_active`,
  `tabs_set_on_change` and `tabs_set_strip_height`. Sidebar functions
  `sidebar_add_item`, `sidebar_get_item_count`, `sidebar_get_item`,
  `sidebar_get/set_active`, `sidebar_get/set_compact`, `sidebar_set/get_header`
  and `sidebar_set_on_select`.
- `widget_set_icon`, `widget_get_icon`, `icon_set_size` and `icon_get_size`.
- `window_begin_uri_drag(window, String[])` (removed in 1.0),
  `window_set_drag_image(window, int[] rgba, width, height)` (packed
  `0xRRGGBBAA`), `window_clear_drag_image` and `window_set_stylus_handler`.
  `StylusEvent` is a record with `phase`, `tool`, `x`, `y`, `pressure`,
  `distance`, `tilt_x`, `tilt_y` and `rotation`.
- `uri_list_count(text)`, `uri_list_get(text, index)` and
  `uri_list(text)` parse `text/uri-list` payloads.

New in 0.7:

```java
Pt.table_set_column_hidden(table, 3, true);
Pt.table_set_row_key_handler(table, (view, row_index, data) ->
    7100L + row_index, arena);
Pt.table_set_row_text_handler(table, (view, row_index, data) ->
    "Row " + row_index, arena);
Pt.table_set_on_sort_columns(table, (t, count, columns, directions, data) -> {
    for (int i = 0; i < count; i++) {
        int column = columns.get(java.lang.foreign.ValueLayout.JAVA_INT, i * 4L);
        int direction = directions.get(java.lang.foreign.ValueLayout.JAVA_INT, i * 4L);
        // apply sort priority i
    }
}, arena);
Pt.table_set_sort_columns(table, new int[]{1, 0},
        new int[]{Pt.sort_ascending, Pt.sort_ascending});
int[] keys = Pt.table_get_sort_columns(table); // column/direction pairs
Pt.table_refresh_rows(table, 2, 4);
long key = Pt.table_get_selected_key(table);   // Pt.row_key_none if unset
boolean found = Pt.table_set_selected_key(table, key);

Pt.list_view_set_row_key_handler(list, (view, row_index, data) ->
    7100L + row_index, arena);
Pt.list_view_set_row_text_handler(list, (view, row_index, data) ->
    "Row " + row_index, arena);
Pt.list_view_refresh_rows(list, 0, 20);
Pt.list_view_set_selected_key(list, 7107L);

Pt.window_set_popup_policy(window, Pt.popup_policy_window);
Pt.window_set_drag_timeout(window, 800);
boolean moving = Pt.window_get_drag_active(window);
Pt.window_cancel_drag(window);
int status = Pt.app_get_a11y_status(app);      // app_enable/disable_a11y
```

- `RowKeyHandler.handle(view, row, data)` returns `long`; `RowTextHandler`
  returns a `String` that Pt copies into a persistent 256-byte buffer for the
  duration of the lookup. `SortColumnsHandler.handle` receives raw
  `MemorySegment` arrays of `int` columns and directions.
- `list_view_refresh_rows`, `list_view_set_row_key_handler`,
  `list_view_get/set_selected_key` and `list_view_set_row_text_handler` join
  the list surface. `row_key_none` clears a keyed selection.
- Table additions: `table_set/get_column_hidden`,
  `table_set_on_sort_columns`, `table_set/get_sort_columns` (flattened
  column/direction pairs), `table_set_row_key_handler`,
  `table_get/set_selected_key`, `table_set_row_text_handler` and
  `table_refresh_rows`. `table_max_sort_columns` is 4.
- `app_enable_a11y`, `app_disable_a11y` and `app_get_a11y_status` use the
  `a11y_status_*` constants. Window additions are
  `window_set/get_popup_policy`, `window_get_drag_active`,
  `window_cancel_drag` and `window_set/get_drag_timeout`.

New in 0.8:

```java
Pt.widget_set_drop_target(card, null, null, null, null,
        (widget, offer, data) -> {                 // DropOfferEnterHandler
            for (int i = 0; i < Pt.drop_offer_mime_count(offer); i++) {
                String mime = Pt.drop_offer_mime(offer, i);
                if ("text/uri-list".equals(mime) || "text/plain".equals(mime)) {
                    return mime;                   // null declines the drag
                }
            }
            return null;
        },
        (widget, offer, mime, x, y, data) -> {     // DropOfferHandler
            ByteArrayOutputStream received = new ByteArrayOutputStream();
            Pt.drop_offer_read(offer, mime, 0, (w, stream, state, chunk, smime, d) -> {
                if (state == Pt.drop_stream_data) {
                    received.writeBytes(chunk);    // chunks are raw bytes
                } else {                           // done / cancelled / error
                    System.out.println(smime + ": " + received.size() + " bytes");
                }
            }, arena);
        }, arena);

Pt.widget_set_text_spans(label, new Pt.TextSpan[]{
        new Pt.TextSpan(0, 5, Pt.font_weight_bold, Pt.font_slant_upright,
                Pt.rgb(0xDC8A1C), Pt.text_span_has_weight | Pt.text_span_has_color),
        new Pt.TextSpan(19, 4, Pt.font_weight_regular, Pt.font_slant_upright,
                0, Pt.text_span_underline)});
int spans = Pt.widget_get_text_span_count(label);
Pt.TextSpan first = Pt.widget_get_text_span(label, 0); // null when out of range
Pt.widget_clear_text_spans(label);
Pt.widget_set_input_purpose(entry, Pt.input_purpose_email);
int purpose = Pt.widget_get_input_purpose(entry);

Pt.window_set_drag_finished_handler(window, (w, dropped, action, data) -> {
    if (!dropped) {
        System.out.println("drag cancelled");
    } else if (action == Pt.drag_action_move) {
        clearSource();
    }
}, arena);
Pt.window_begin_drag(window, new Pt.DragData[]{
        new Pt.DragData("text/plain", payload, 0),
        new Pt.DragData("application/json", json, json.length())});
int action = Pt.window_get_drag_result(window);
```

- `widget_set_drop_target` has a seven-argument structured overload that takes
  `DropOfferEnterHandler` and `DropOfferHandler` after the legacy four; passing
  `null` for both handlers keeps the legacy pair, and `null` enter and enter_offer
  clears the target.
- Offer accessors are `drop_offer_mime_count`, `drop_offer_mime(offer, index)`
  (a `String` or `null`), `drop_offer_has_mime`, `drop_offer_selected_mime` and
  `drop_offer_actions`. `drop_offer_read(offer, mime, max_bytes, handler, arena)`
  returns a stream handle that is `MemorySegment.NULL` when no read could start,
  and `DropStreamHandler.handle` receives `(widget, stream, state, byte[] chunk,
  mime, data)`. `drop_stream_cancel/mime/size/active` work on that handle; Pt
  frees the stream once its final state callback returns.
- `TextSpan(int start, int length, int weight, int slant, int color, int flags)`
  is set as an array with `widget_set_text_spans`, which returns `false` for a
  widget kind that takes no spans. `DragData(String mime_type, String data,
  long size)` is likewise an array; a size of zero means NUL-terminated data.
- Constants are `text_span_underline|has_weight|has_slant|has_color`,
  `input_purpose_*`, `font_weight_*`, `font_slant_*` and `drag_action_*`, and the
  stream states `drop_stream_data|done|cancelled|error`. Colors use `Pt.rgb` and
  `Pt.rgba`.

New in 0.9:

```java
Pt.app_set_theme_metric(app, Pt.theme_metric_chip_height, 30.0f);
float radius = Pt.app_get_theme_metric(app, Pt.theme_metric_radius_small);

Pt.window_set_theme_color(window, Pt.theme_background, Pt.rgb(0x102030));
Pt.window_set_theme_metric(window, Pt.theme_metric_focus_width, 3.0f);
Pt.window_clear_theme_overrides(window);

MemorySegment tag = Pt.chip(row, "design");
Pt.chip_set_icon(tag, Pt.icon_folder);
Pt.chip_set_removable(tag, true);
Pt.chip_set_on_click(tag, (widget, data) -> filter(), arena);
Pt.chip_set_on_remove(tag, (widget, data) -> removeFilter(), arena);
MemorySegment placeholder = Pt.skeleton(row);
Pt.widget_set_height(placeholder, 12);

Pt.widget_set_busy(button, true);
Pt.widget_set_invalid(email, true);

Pt.table_set_editable(table, true);
Pt.table_set_on_cell_edited(table, (t, row, column, text, data) ->
        updateModel(row, column, text), arena);
Pt.table_begin_edit(table, 1, 0);
Pt.table_commit_edit(table);
Pt.table_select_range(table, 1, 3);
int[] rows = Pt.table_get_selection(table);           // [1, 2, 3]
Pt.table_clear_selection(table);

Pt.table_set_columns_reorderable(table, true);
Pt.table_set_on_columns_reordered(table, (t, from, to, data) ->
        reorder(from, to), arena);
Pt.table_set_on_row_drag(table, (t, row, data) -> moveRow(row), arena);
Pt.table_move_column(table, 0, 2);

Pt.tree_set_renamable(tree, true);
Pt.tree_set_on_rename(tree, (t, node, text, data) -> rename(node, text), arena);
Pt.tree_begin_rename(tree, node);
boolean committed = Pt.tree_commit_rename(tree);

Pt.widget_set_text_spans(label, new Pt.TextSpan[]{
        new Pt.TextSpan(0, 5, Pt.font_weight_bold, Pt.font_slant_upright, 0,
                Pt.text_span_has_family | Pt.text_span_has_size, "DejaVu Serif", 15.0f)});
```

- `app_set/get_theme_metric` and `window_set/get_theme_metric`,
  `window_set/get_theme_color` and `window_clear_theme_overrides` mirror the C
  pair; a window getter falls through to the app value.
- `Pt.chip(parent, text)` and `Pt.skeleton(parent)` are factories like
  `Pt.label`; chip state setters and `chip_get_removable`/`chip_get_selected`
  reuse `ClickHandler` for `chip_set_on_click`/`chip_set_on_remove`.
- Table editing is `table_set/get_editable`, `table_begin_edit`,
  `table_commit_edit`, `table_cancel_edit`, `table_is_editing` and
  `table_set_on_cell_edited` with `TableEditedHandler`; selection is
  `table_select_range`, `table_get_selection` (an `int[]`),
  `table_get_selection_count`, `table_is_row_selected` and
  `table_clear_selection`; ordering is `table_set_columns_reorderable`,
  `table_move_column`, `table_set_on_columns_reordered` with
  `TableReorderHandler` and `table_set_on_row_drag` with `TableRowDragHandler`.
- Tree renaming is `tree_set/get_renamable`, `tree_begin_rename`,
  `tree_commit_rename`, `tree_cancel_rename`, `tree_is_renaming` and
  `tree_set_on_rename` with `TreeRenameHandler`; the handler fires after the
  node text is already updated.
- `TextSpan` is an eight-component record with a six-argument compatibility
  constructor; `family` and `size` are read when
  `text_span_has_family`/`text_span_has_size` is set.
- `window_begin_text_drag` and `window_begin_uri_drag` were `@Deprecated` in 0.9
  (the URI one took `(window, String[] uris)` and allocated its own confined
  arena). Both were removed at the 1.0 freeze; a `DragData` array passed to
  `window_begin_drag` is the only drag source.

New in 0.10:

```java
MemorySegment region = Pt.combobox(root);      // Pt.combobox factory
Pt.combobox_add_item(region, "Europe");
Pt.combobox_insert_item(region, 1, "Asia");
Pt.combobox_set_item_text(region, 0, "Europe/Berlin");
Pt.combobox_set_item_data(region, 0, payload); // a MemorySegment, or NULL
Pt.combobox_set_item_disabled(region, 1, true);
String first = Pt.combobox_get_item_text(region, 0);
MemorySegment stored = Pt.combobox_get_item_data(region, 0);
int items = Pt.combobox_get_item_count(region);
boolean skipped = Pt.combobox_get_item_disabled(region, 1);

Pt.combobox_set_mode(region, Pt.combobox_filterable);
Pt.combobox_set_selected(region, 2);           // silent: no on_change
Pt.combobox_set_text(region, "Ber");
Pt.combobox_set_placeholder(region, "Choose a region");
Pt.combobox_open(region);
boolean open = Pt.combobox_is_open(region);
Pt.combobox_set_popup_max_height(region, 320.0f);
Pt.combobox_close(region);
Pt.combobox_set_on_change(region, (widget, index, data) -> choose(index), arena);
Pt.combobox_set_on_text_change(region, (widget, text, data) -> System.out.println(
        text.address() == 0 ? "" : text.reinterpret(Long.MAX_VALUE).getString(0)), arena);
Pt.combobox_remove_item(region, 0);
Pt.combobox_clear(region);
int none = Pt.combobox_get_selected(region);   // Pt.list_index_none

Pt.list_view_select_range(rows, 2, 4);
Pt.list_view_select_toggle(rows, 6);
int[] heldRows = Pt.list_view_get_selection(rows);        // [2, 3, 4, 6]
boolean row3 = Pt.list_view_is_row_selected(rows, 3);
int rowCount = Pt.list_view_get_selection_count(rows);
Pt.list_view_clear_selection(rows);

Pt.table_select_toggle(table, 4);              // extends table_select_range

Pt.tree_select_range(tree, 1, 3);
Pt.tree_select_toggle(tree, beta);
MemorySegment[] nodes = Pt.tree_get_selection(tree);      // document order
boolean alpha = Pt.tree_is_selected(tree, alphaNode);
Pt.tree_clear_selection(tree);
```

- `Pt.combobox(parent)` is a factory like `Pt.label`. Every combobox function
  keeps the C name: `combobox_add_item`, `combobox_insert_item`,
  `combobox_remove_item`, `combobox_clear`, `combobox_get_item_count`,
  `combobox_get/set_item_text`, `combobox_get/set_item_data`,
  `combobox_get/set_item_disabled`, `combobox_get/set_mode`,
  `combobox_get/set_selected`, `combobox_get/set_text`,
  `combobox_get/set_placeholder`, `combobox_open`, `combobox_close`,
  `combobox_is_open`, `combobox_get/set_popup_max_height`,
  `combobox_set_on_change` and `combobox_set_on_text_change`.
- Text arguments take a `String` and are encoded into a confined arena; text
  returns are `String` and map a `NULL` pointer to `""`. `Pt.combobox_get_text`
  and `combobox_get_placeholder` behave this way.
- `Pt.combobox_set_item_data` takes a `MemorySegment` pointer, so callers pass
  what `arena.allocate(...)` or a `MemorySegment.NULL` gives them; nothing is
  marshalled on their behalf.
- `combobox_set_on_change` reuses `IndexHandler` — `(widget, index)` — and
  `combobox_set_on_text_change` reuses `TextHandler`, whose second argument is
  the native `char *` as a `MemorySegment`; both take the `Arena` that owns the
  upcall stub and accept `null` to clear the callback.
- `Pt.combobox_select_only`/`combobox_filterable`, `Pt.a11y_role_combo_box`,
  `Pt.a11y_role_list_box`, `Pt.a11y_state_selectable` and
  `Pt.a11y_state_multi_selectable` are new constants. An empty selection is
  `Pt.list_index_none`, which is `-1` in Java because the getters return a
  signed `int` for the C `uint32_t`.
- Selection parity mirrors the C names: `list_view_select_range`,
  `list_view_select_toggle`, `list_view_get_selection` (an `int[]`),
  `list_view_get_selection_count`, `list_view_is_row_selected`,
  `list_view_clear_selection`, the new `table_select_toggle`, and
  `tree_select_range`, `tree_select_toggle`, `tree_get_selection` (a
  `MemorySegment[]`), `tree_get_selection_count`, `tree_is_selected`,
  `tree_clear_selection`.

Compile and run from the source tree:

```sh
javac -Xlint:all -d build/java bindings/java/src/pt/Pt.java Hello.java
java --enable-native-access=ALL-UNNAMED \
    -Dpt.library="$PWD/build/libpt.so" -cp build/java Hello
```

The library is looked up from `-Dpt.library`, then `PT_LIBRARY`, then the
system loader path. JDK 25 requires `--enable-native-access=ALL-UNNAMED` for
the linker upcalls; the Makefile and tests pass it explicitly. After
installation, compile against
`$PREFIX/share/pt/bindings/java/src/pt/Pt.java`.

The Java test is `bindings/java/test/pt/PtTest.java`; run it with
`make check-bindings` or `make java-test`.

## Binding coverage

All 412 functions in the public C ABI are wrapped in C++, Python and Java. The
parity audit is a hard `make check` gate: it scans the comment-stripped binding
sources for every declared function name, allows zero unreviewed gaps and
reports `412/412` for each language. The reviewed allowlist is empty. The 124
functions in the 0.6 surface (one fewer than 0.9 carried, because
`pt_window_begin_uri_drag` was removed at the 1.0 freeze), the 19 functions
introduced for the 0.7 data-view,
drag and input surface, the 19 functions introduced for the 0.8 structured
drag, streamed offer and styled-text surface, the 44 functions introduced
for the 0.9 theme, chip/skeleton, data-view editing and rename surface and the
40 functions introduced for the 0.10 combobox and multi-row selection surface
are all present in all three wrappers. The two 0.9-deprecated drag helpers spent
their deprecation window wrapped as deprecated wrappers in every binding rather
than allowlisted away, so nothing left the ABI without passing through every
binding first; they are now gone from the C ABI and from all three bindings in
the same change.

`make check` runs the C suites, `make check-bindings` and the API/ABI checks.
The binding suites are headless: 344 C++ checks, 33 Python unittest cases and
298 Java checks. Overlay and accessibility tests cover menu/dialog callbacks,
toast lifetime, tooltips, event delivery, enable/disable status and text/value
exposure. The 0.10 tests cover the combobox defaults and mode switch, item
add/insert/remove/clear with the text and data accessors and the disabled item,
the silent `set_selected`/`set_text` behaviour, placeholder and field text, the
popup open/close state and max-height round trip, the index and text callbacks,
multi-row selection on the list through range, toggle and clear with the
primary-index follow-on, the extra table toggle path, tree range, toggle and
clear in document order, and the combo-box and list-box roles with the
selectable and multi-selectable states. The 0.9 tests cover app metrics,
window-level token and metric
overrides with clear, chip creation and callbacks, skeleton sizing, busy and
invalid flags, table cell editing and commit, multi-row selection ranges,
column reorder and row drag callbacks, tree rename commit and the second
no-editor commit, and per-run family and size spans. The 0.8 tests cover offer-driven drop targets with a fallback to the
legacy pair, chunked stream reads with cancellation and truncation at
`max_bytes`, multi-MIME source drags and their finished callback, span
round-trips with clamping and clearing, and the input purpose setter/getter
pair. The 0.7 tests cover keyed list/table selection across row-count
changes and refreshes, batch `refresh_rows`, multi-column sort filtering and
reporting, hidden columns, row key/text callbacks, popup policy, drag
active/cancel/timeout and a11y status. The 0.6 tests cover table column setup,
binding and sorting, tree construction and selection, tabs and sidebar state,
icon setters, URI-list parsing, drag-image pokes and stylus callback
registration. The 0.5 tests check list row binding, selection and activation,
text-area selection and grow-to-content, wrap/grid gaps and spans, easing and
transition retargeting, reduced-motion toggles and the new a11y roles.
