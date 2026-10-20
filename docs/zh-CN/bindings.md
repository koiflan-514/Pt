# Pt 语言绑定

[English](../bindings.md) | 简体中文

Pt 对外暴露一个扁平的 C ABI。绑定是建立在该 ABI 之上的轻量级手写封装层，因此控件在每种语言中的行为都完全一致。0.10 表面新增了组合框控件——模式、条目列表、逐条目数据和禁用标志、字段文本、占位符、弹出层控制以及 change/text 回调——并让列表视图、表格和树视图具备一致的多行选择能力，以及匹配的无障碍角色和状态，同时保持一致性门禁零缺口：全部 412 个函数——比 0.10 发布时少两个，因为 1.0 冻结移除了已弃用的拖拽辅助函数——均可从 C++、Python 和 Java 使用。0.9 表面——带窗口级覆盖的扩展主题令牌和度量、标签片和骨架屏、busy/invalid 状态矩阵、表格单元格编辑、多行选择、列重排和行拖拽、树重命名以及按文本段排版——仍被完整封装。

| 绑定 | 技术 | 最低要求 | 所有权 |
| --- | --- | --- | --- |
| C | 直接调用 | C23 编译器 | 调用方拥有应用 |
| C++ | 仅头文件 RAII 辅助层 | C++23 | `pt::app` 拥有原生对象图 |
| Python | `ctypes` | 3.10 | `pt.App` 拥有窗口；包装器保持 trampoline 存活 |
| Java | Foreign Function & Memory API | JDK 22（在 25 上测试） | `MemorySegment` 句柄由原生对象图拥有 |

所有绑定都仅限主线程，所有回调都在执行 `app_run`/`app_run_for` 的线程上运行。绑定为 C 的 `user_data` 参数传入 `NULL`，改为在回调对象中捕获语言侧状态。

## C++

封装层位于 `bindings/cpp/pt/pt.hpp`，安装为 `<pt/pt.hpp>`。控件和窗口是非拥有式句柄；`pt::app` 拥有原生对象图，并在其离开作用域时销毁该对象图。

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

类：`pt::app`、`pt::window`、`pt::widget`、`pt::menu`、`pt::dialog`、`pt::toast` 和 `pt::transition`。工厂函数按值返回 `pt::widget`：`box`、`row`、`column`、`label`、`button`、`entry`、`text_area`、`toggle`、`checkbox`、`radio`、`slider`、`progress`、`spinner`、`segmented`、`scroll`、`wrap`、`grid`、`list_view`、`divider`、`card`、`spacer`、`table`、`tree`、`tabs`、`sidebar`、`icon` 和 `icon_button`。大多数 `pt::widget` setter 都支持链式调用，并返回 `widget &`。0.2 新增：

- `set_checked`、`checked`、`on_checked` 适用于切换开关、复选框和单选按钮。
- `set_progress`、`progress`、`set_indeterminate`、`indeterminate`。
- `segmented(parent, {"A", "B"})`、`select_segment`、`segment`、`on_segment_change`，使用原始 `pt_index_callback` 签名。
- `select_all`、`set_selection`、`selection()`（返回 `std::optional<std::pair<uint32_t, uint32_t>>`）、`undo`、`redo`。
- `set_scroll_offset`、`scroll_offset`、`max_scroll_offset`。
- `window.set_clipboard` / `window.clipboard()`；getter 会为你释放原生字符串。
- `app.theme_color(token)` / `app.set_theme_color(token, color)`。

0.3 新增：

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

- `pt::menu` 和 `pt::dialog` 是仅可移动的 RAII 所有者；请在窗口和 app 之前销毁它们。`pt::toast` 是由窗口拥有的非拥有式句柄。
- 菜单项回调在菜单关闭后运行。对话框操作会在其回调运行前关闭对话框，按 Escape 或关闭按钮会触发 `on_close`。
- `pt::widget` 新增 `set_tooltip`、`set_tooltip_delay`、`set_a11y_role`、`set_a11y_name`、`set_a11y_description`、`a11y_role`、`a11y_name`、`a11y_description`、`a11y_state` 和 `a11y_value`（`std::optional<float>`）。
- `pt::app::on_a11y_event(callback, data)` 注册原生 `pt_a11y_event_callback` 签名。

0.4 新增：

```cpp
pt_drop_target target{drop_enter, drop_motion, drop_leave, drop_receive, &state};
drop_zone.set_drop_target(target);
entry.set_drag_enabled(true);
window.on_gesture(on_gesture, &state).set_long_press_delay(450);
window.set_momentum_scrolling(true);
window.begin_text_drag("selected text"); // removed in 1.0, see begin_drag
```

- `pt::widget` 新增 `set_drop_target(const pt_drop_target&)`、`clear_drop_target()`、`drop_hover()`、`set_drag_enabled(bool)` 和 `drag_enabled()`。
- `pt::window` 新增 `on_gesture(pt_gesture_callback, void *)`、`set_long_press_delay(uint32_t)`、`set_momentum_scrolling(bool)`、`momentum_scrolling()` 和 `begin_text_drag(std::string_view)`，最后一项在 1.0 中移除。
- 手势回调接收扁平的 `pt_gesture_event`；使用 `pt_gesture_kind_*` 和 `pt_gesture_phase_*` 来解释它。目标是手势开始时位于手势质心下方的控件。

0.5 新增：

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

- `pt::widget::kind()` 返回 `pt_widget_kind`；文本区域方法根据它进行分派。因此 `set_placeholder`、`on_change`、`select_all`、`set_selection`、`selection`、`undo` 和 `redo` 对输入框和文本区域都有效，而 `set_grow_to_content`、`grow_to_content` 和 `scroll_to_caret` 仅适用于文本区域。
- 布局方法是 `pt::widget` 上的 `wrap(parent)`、`grid(parent, columns)` 以及 `set_wrap_gap`、`set_grid_columns`、`grid_columns`、`set_grid_gap`、`set_grid_span` 和 `grid_span`。
- 列表方法是 `list_view(parent)`、`set_row_count`、`row_count`、`set_row_height`、`row_height`、`on_bind_row`（原始 `pt_list_bind_callback`）、`on_row_select`/`on_row_activate`（`pt_index_callback`）、`selected_row`、`set_selected_row`、`refresh` 和 `scroll_to_row`。`pt_list_index_none` 清除选择。
- `pt::ease` 和 `pt::ease_value` 封装曲线集合。`pt::transition` 是调用方拥有的值，提供 `to`、`advance`、`set`、`value`、`active` 和 `raw` 以便与 C 互操作。
- `app.set_reduced_motion(bool)` 和 `app.reduced_motion()` 暴露应用偏好；`PT_REDUCED_MOTION` 提供默认值。

0.6 新增：

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

- 工厂函数 `table`、`tree`、`tabs`、`sidebar`、`icon` 和 `icon_button`；`pt::tree_node` 是不透明节点句柄的别名，`pt::stylus_event` 是触控笔记录的别名。
- 表格方法：`set_column_count`/`column_count`、`set_column_title`、`set_column_width`/`column_width`、`set_column_align`、`set_column_resizable`、`set_table_row_count`/`table_row_count`、`set_table_row_height`/`table_row_height`、`set_header_height`、`on_bind_cell`（`pt_table_bind_cell_callback`）、`on_table_select`、`on_table_activate`、`on_table_sort`、`set_sort`/`sort`、`table_selected`/`set_table_selected`、`table_refresh`、`scroll_to_column` 和 `table_scroll_to_row`。
- 树方法：`tree_root_count`、`tree_root`、`add_tree_node`、`clear_tree`、`tree_visible_count`、`tree_visible_node`、`tree_selected`、`set_tree_selected`、`set_tree_row_height`、`set_tree_indent`、`on_tree_select`、`on_tree_activate`、`tree_refresh` 和 `scroll_to_tree_node`。节点辅助函数是 `pt::tree_text`、`set_tree_text`、`tree_expanded`、`set_tree_expanded`、`tree_has_children`、`tree_depth`、`tree_parent`、`tree_child_count`、`tree_child`、`tree_data` 和 `set_tree_data`。
- 标签页方法：`add_tab`、`remove_tab`、`tab_count`、`tab_page`、`set_tab_title`、`tab_title`、`active_tab`、`set_active_tab`、`on_tab_change` 和 `set_tab_strip_height`。侧边栏方法：`add_sidebar_item`、`sidebar_item_count`、`sidebar_item`、`active_sidebar_item`、`set_active_sidebar_item`、`sidebar_compact`、`set_sidebar_compact`、`set_sidebar_header`、`sidebar_header` 和 `on_sidebar_select`。
- `pt::widget` 新增 `set_icon`/`icon` 和 `set_icon_size`/`icon_size`。
- `pt::window` 新增 `begin_uri_drag(std::initializer_list<std::string_view>)`（在 1.0 中移除）、`set_drag_image(const pt_color *, int, int)`、`clear_drag_image()` 和 `on_stylus(pt_stylus_callback, void *)`。
- 自由辅助函数 `pt::uri_list_count(std::string_view)` 和 `pt::uri_list_get(std::string_view, uint32_t)` 返回解析后的 URI；getter 会复制并释放原生字符串。

0.7 新增：

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

- `pt::app` 新增 `enable_a11y()`、`disable_a11y()` 和 `a11y_status()`。
- `pt::window` 新增 `set_popup_policy`/`popup_policy`、`drag_active()`、`cancel_drag()`、`set_drag_timeout`/`drag_timeout`。
- 列表方法是 `refresh_rows(first, count)`、`set_row_key_callback`（`pt_row_key_callback`）、`selected_key()`、`set_selected_key(uint64_t)`、`set_row_text_callback` 以及已有的 `refresh`/`scroll_to_row`。使用 `pt_row_key_none` 清除带键选择。
- 表格方法是 `set_column_hidden`/`column_hidden`、`on_sort_columns`（`pt_table_sort_columns_callback`）、`set_sort_columns(std::initializer_list<sort_key>)` 和按优先级顺序返回 `std::vector<sort_key>` 的 `sort_columns()`，其中 `sort_key` 具有 `column` 和 `direction` 字段。带键选择是 `set_table_row_key_callback`、`table_selected_key()`、`set_table_selected_key(uint64_t)`，批量失效是 `table_refresh_rows(first, count)`。直接调用 `set_sort_columns` 不会调用排序回调；点击表头会调用。

0.8 新增：

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

- `pt::drop_offer` 和 `pt::drop_stream` 是带有空安全访问器的非拥有式句柄。`drop_offer` 新增 `mime_count()`、`mime(index)`、`has_mime(string_view)`、`selected_mime()`、`actions()` 和 `read(mime, max_bytes = 0, callback = nullptr, data = nullptr)`；`drop_stream` 新增 `mime()`、`size()`（迄今已交付的字节数）、`active()` 和 `cancel()`。该 offer 仅在自身的回调内有效；由它打开的流会存活到回调结束之后。
- `widget::set_drop_target(enter_offer, drop_offer, motion, leave, data)` 会为你填充 `pt_drop_target`；`set_drop_target(pt_drop_target)` 和 `clear_drop_target()` 仍然可用。
- 带样式的文本段通过 `set_text_spans(vector|initializer_list)`、`clear_text_spans()`、`text_span_count()` 和 `text_span(index)` 设置，后者返回 `std::optional<pt_text_span>`。文本段使用 UTF-8 文本中的字节偏移，并被限制在文本范围内；`pt_text_span_*` 标志选择 `weight`、`slant` 和 `color` 中哪些生效。
- `widget::set_input_purpose`/`input_purpose()` 使用 `pt_input_purpose_*`。
- 窗口新增 `begin_drag(span|initializer_list<pt_drag_data>)`、`set_drag_finished_callback` 和 `drag_result()`。`pt_drag_data` 的大小为零表示以 NUL 结尾的数据。`drag_data`、`drop_stream_state` 和 `text_span` 是 C 结构体和枚举的别名。

0.9 新增：

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

- 主题查找按语言绑定提供：`app::set/get_theme_color`、`app::set/get_theme_metric`、`window::set/get_theme_color`、`window::set/get_theme_metric` 和 `window::clear_theme_overrides()`。当令牌未被覆盖时，窗口 getter 会回退到应用值。
- `pt::chip(parent, text)` 和 `pt::skeleton(parent)` 是类似 `pt::label` 的工厂函数；标签片状态是 `set_chip_icon`、`set_chip_removable`、`set_chip_selected`，以及 `chip_removable`/`chip_selected` getter，并且 `on_chip_click`/`on_chip_remove` 复用 `pt_click_callback`。
- 表格编辑是 `set_table_editable`、`on_cell_edited`、`begin_cell_edit`、`commit_cell_edit` 和 `cancel_cell_edit`；选择是 `select_range`、`table_selection()`（一个 `std::vector<uint32_t>`）、`table_selection_count()`、`is_row_selected` 和 `clear_table_selection()`；列顺序是 `set_columns_reorderable` 与 `move_column` 和 `on_columns_reordered`，而 `on_row_drag` 报告被拖拽的行。
- 树重命名是 `set_tree_renamable`、`begin_tree_rename`、`commit_tree_rename`、`cancel_tree_rename`、`tree_renaming()` 和 `on_tree_rename`；`commit_tree_rename()` 仅在没有编辑器打开时返回 `false`。
- `pt_text_span` 新增了 `family` 和 `size`，由 `pt_text_span_has_family` 和 `pt_text_span_has_size` 选择。
- `window::begin_text_drag` 和 `begin_uri_drag` 在 0.9 中被标记为 `[[deprecated]]`；封装层在诊断 pragma 内转发到已弃用的 C 入口点，以便一致性扫描仍能看到它们。两者在 1.0 冻结时被移除，因此带 `pt_drag_data` 列表的 `begin_drag` 是唯一的拖拽源。

0.10 新增：

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

- `pt::combobox(parent)` 是类似 `pt::label` 的工厂函数。`add_item`、`insert_item`、`remove_item` 和 `clear_items` 维护列表；`item_count()`、`item_text`、`set_item_text`、`set_item_data`、`item_data`、`set_item_disabled` 和 `is_item_disabled` 是逐条目访问器。
- 模式是 `set_combo_mode`/`combo_mode()`，类型为 `pt::combo_mode`（`pt_combobox_mode` 的别名）。getter 和 setter 命名为 `combo_*`，因为 `widget` 类已经带有表格 `column` 和树访问器；在类内部，返回类型和参数类型必须写成 `pt::combo_mode`，因为名为 `combo_mode` 的成员会在类作用域中隐藏该别名。
- `set_combo_selected(index)` 和 `set_combo_text(text)` 与 API 中其他所有 `set_selected` 一样是静默的，并且两者在空选择时都报告 `pt_list_index_none`。`combo_text()` 和 `combo_placeholder()` 返回 `std::string`，并将 `NULL` 映射为空字符串。`open_popup`、`close_popup`、`is_popup_open()`、`set_popup_max_height`/`popup_max_height()` 控制面板。`on_combo_change` 复用 `pt_index_callback`；`on_combo_text_change` 采用新的 `pt_combobox_text_callback`。
- 多行选择现在在三个数据视图之间保持一致。列表使用 `list_select_range`、`list_select_toggle`、`list_selection()`（一个 `std::vector<uint32_t>`）、`list_selection_count()`、`is_list_row_selected` 和 `clear_list_selection()`；表格在 0.9 的 `select_range`/`table_selection()` 配对旁边新增 `select_toggle`；树使用 `tree_select_range`、`tree_select_toggle`、`tree_selection()`（按文档顺序的 `std::vector<pt::tree_node *>`，包含隐藏节点）、`tree_selection_count()`、`is_tree_node_selected` 和 `clear_tree_selection()`。

重要细节：

- `pt::app` 仅可移动，并在其析构函数中调用 `pt_app_destroy`。请在所有控件和窗口句柄不再使用后销毁它。
- `pt::window` 和 `pt::widget` 没有析构函数，也不拥有任何内容。`window.root()` 返回指向窗口拥有的根控件的句柄。
- 回调使用原始 C 函数指针签名，例如 `void (*)(pt_widget *, void *)`。在 `void *data` 参数中捕获状态；无捕获的 lambda 可以转换，但 `std::function` 不行。
- `window.on_close(callback, data)` 返回 `pt::window &`；回调返回 `false` 以否决关闭。
- `pt::app::run_for` 可用于测试和嵌入式循环。

针对源码树构建：

```sh
g++ -std=c++23 -Iinclude -Ibindings/cpp app.cpp \
    build/libpt.a $(pkg-config --libs wayland-client wayland-cursor xkbcommon \
    fontconfig freetype2 harfbuzz zlib) -lm -o app
```

针对已安装副本构建：

```sh
g++ -std=c++23 app.cpp $(pkg-config --cflags --libs pt) -o app
```

C++ 封装层由 `bindings/cpp/test_pt.cpp` 覆盖，并作为 `make check-bindings` 的一部分运行。

## Python

`bindings/python/pt.py` 是一个自包含的 ctypes 绑定。它没有第三方依赖，并且需要 Python 3.10 或更高版本。

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

Python 回调签名刻意隐藏了原生参数：

| 方法 | Python 回调 |
| --- | --- |
| `Widget.on_click` | `callback()` |
| `Widget.on_submit` | `callback()` |
| `Widget.on_change` | `callback(text: str)`；输入框和文本区域 |
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
| `Window.on_close` | `callback(window) -> bool`（返回 `False` 以否决） |
| `Window.on_resize` | `callback(window, width, height)` |
| `Window.on_key` | `callback(window, key, modifiers) -> bool` |
| `Window.on_gesture` | `callback(window, event)`；event 是一个 `GestureEvent` |
| `Window.on_stylus` | `callback(window, event)`；event 是一个 `StylusEvent` |
| `Widget.set_drop_target` | `enter(widget, mime) -> bool`、`motion(widget, x, y)`、`leave(widget)`、`drop(widget, mime, text, x, y)` |
| `Menu.add_item` | 菜单关闭后 `callback()` |
| `Dialog.add_action` | 对话框关闭后 `callback()` |
| `Dialog.on_close` | `callback(dialog)` |
| `App.on_a11y_event` | `callback(window, widget, event)`；window/widget 可能为 `None` |
| `App.add_timeout` | `callback()` |

该绑定在模块级表中保留 ctypes trampoline 和窗口包装器。不要自行丢弃它们。计时器在触发时会移除自己的 trampoline；`App.cancel_timeout` 会显式移除它。

`Widget` 暴露 `text`、`bounds`（一个 `(x, y, width, height)` 元组）、`checked`、`value`、`progress`、`indeterminate`、`selected`、`selection`（一个 `(start, end)` 元组或 `None`）、`scroll_offset`、`max_scroll_offset`、`focused`、`kind`、`parent`、`drop_hover` 和 `drag_enabled`，以及用于布局、外观和回调的链式 setter。输入框辅助方法是 `select_all`、`set_selection`、`undo`、`redo` 和 `set_drag_enabled`。

0.4 交互表面是 `Widget.set_drop_target(enter, motion, leave, drop)`（不传回调调用它会清除目标）、`Window.on_gesture`、`Window.set_long_press_delay(milliseconds)`、`Window.momentum_scrolling` 属性以及 `Window.begin_text_drag(text)`（在 1.0 中移除）。手势回调接收一个 `GestureEvent`，其中包含 `kind`、`phase`、`fingers`、`x`、`y`、`dx`、`dy`、`scale`、`rotation`、`target` 以及 `target_widget` 辅助属性。将 `kind` 和 `phase` 与 `pt.gesture_kind_*` 和 `pt.gesture_phase_*` 比较。

0.5 表面是 `pt.text_area(parent, placeholder)`、`pt.wrap(parent)`、`pt.grid(parent, columns)` 和 `pt.list_view(parent)`。`Widget` 新增 `grow_to_content`、`grid_columns`、`grid_span`、`row_count`、`row_height` 和 `selected_row` 属性，以及 `set_grow_to_content`、`scroll_to_caret`、`set_wrap_gap`、`set_grid_columns`、`set_grid_gap`、`set_grid_span`、`set_row_count`、`set_row_height`、`set_selected_row`、`scroll_to_row`、`refresh`、`on_bind_row`、`on_row_select` 和 `on_row_activate`。`set_*` 布局和列表方法返回控件，因此它们可以在工厂函数中链式调用。未选择任何行时，`selected_row` 是 `pt.list_index_none`。当控件类型是文本区域时，`on_change`、`set_placeholder`、`select_all`、`set_selection`、`selection`、`undo` 和 `redo` 分派到文本区域 API。

动画通过 `pt.ease(easing, t)`、`pt.ease_value(easing, start, end, t)`、`pt.easing_*` 常量以及调用方拥有的 `Transition` 类暴露，后者提供 `value`、`active`、`to(target, duration_ms, easing)`、`advance(dt)` 和 `set(value)`。`App.reduced_motion` 读取并写入应用偏好。

0.6 工厂函数是 `pt.icon(parent, glyph)`、`pt.icon_button(parent, glyph, accessible_name)`、`pt.table(parent)`、`pt.tree(parent)`、`pt.tabs(parent)` 和 `pt.sidebar(parent)`。`Widget` 属性是 `icon`、`icon_size`、`column_count`、`table_row_count`、`table_row_height`、`table_selected`、`sort`（一个 `(direction, column)` 元组）、`active_tab`、`tab_count`、`sidebar_item_count`、`active_sidebar_item`、`sidebar_compact` 和 `sidebar_header`。链式方法与 C 名称对应：`set_icon`、`set_icon_size`、`set_column_count`、`set_column_title`、`set_column_width`、`column_width`、`set_column_align`、`set_column_resizable`、`set_header_height`、`set_table_row_count`、`set_table_row_height`、`set_table_selected`、`set_sort`、`table_refresh`、`table_scroll_to_row`、`scroll_to_column`、`add_tree_node`、`tree_root`、`tree_visible_node`、`set_tree_selected`、`set_tree_row_height`、`set_tree_indent`、`tree_refresh`、`scroll_to_tree_node`、`clear_tree`、`add_tab`、`remove_tab`、`tab_page`、`set_tab_title`、`tab_title`、`set_active_tab`、`set_tab_strip_height`、`add_sidebar_item`、`sidebar_item`、`set_active_sidebar_item`、`set_sidebar_compact` 和 `set_sidebar_header`。

`TreeNode` 包装一个不透明节点句柄，提供 `text`、`expanded`、`has_children`、`depth`、`parent`、`child_count`、`children`、`data`、`child(index)` 和 `set_expanded(expanded=True)`。通过拥有该节点的控件的 `add_tree_node(parent, text)` 添加子节点。

0.6 交互新增包括 `Window.begin_uri_drag(uris: list[str])`（在 1.0 中移除）、`Window.set_drag_image(pixels, width, height)` 和 `Window.clear_drag_image()`，以及 `Window.on_stylus`。`StylusEvent` 携带 `phase`、`tool`、`x`、`y`、`pressure`、`distance`、`tilt_x`、`tilt_y` 和 `rotation`。模块辅助函数 `pt.uri_list_count(text)`、`pt.uri_list_get(text, index)` 和 `pt.uri_list(text)` 解析 `text/uri-list` 载荷。图标常量是 `pt.icon_*`，排序常量是 `pt.sort_*`，`pt.table_column_none` 清除表格排序。

0.7 新增：

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

- `App.enable_a11y()`、`App.disable_a11y()` 和 `App.a11y_status` 暴露桥接；状态常量是 `pt.a11y_status_*`。
- `Window.popup_policy`、`drag_active`、`drag_timeout` 和 `cancel_drag()` 涵盖原生弹出层策略和拖拽生命周期。
- 列表方法是 `refresh_rows(first, count)`、`set_row_key_callback(callback)`、`selected_key` 属性、`set_selected_key(key)` 和 `set_row_text_callback(callback)`。
- 表格方法是 `set_column_hidden(column, hidden=True)`、`column_hidden(column)`、`on_sort_columns(callback)`、`set_sort_columns([(column, direction), ...])`、`sort_columns` 属性（按优先级顺序的 `(column, direction)` 元组列表）、`table_refresh_rows`、`set_table_row_key_callback`、`table_selected_key` 属性、`set_table_selected_key` 和 `set_table_row_text_callback`。
- 键回调返回非零 `int`；`pt.row_key_none` 清除带键选择。文本回调返回 `str`，仅在键入预选查找期间被借用。直接调用 `set_sort_columns` 不会调用排序回调；点击表头会调用。

0.8 新增：

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

- `set_drop_target` 新增了关键字对 `enter_offer`/`drop_offer`，其优先级高于 `enter`/`drop`。`enter_offer` 返回要接受的 MIME 类型（从 offer 中取得的 `str`，或 `None` 表示拒绝），`drop_offer` 接收一个 `DropOffer`。
- `DropOffer` 暴露 `mime_count`、`selected_mime` 和 `actions` 属性，以及 `mime(index)`、`has_mime(str)` 和 `read(mime, max_bytes=0, callback=None)`。`read` 返回一个 `DropStream` 或 `None`，其回调接收 `(widget, stream, state, bytes)`。`DropStream` 具有 `mime`、`size`（迄今已交付的字节数）、`active` 和 `cancel()`。Pt 在其最终状态回调后释放原生流，句柄随后变为空安全。
- `pt.TextSpan(start, length, weight, slant, color, flags)` 通过 `set_text_spans(list)` 设置，并通过 `text_span_count()` 和 `text_span(index)` 读回；`clear_text_spans()` 移除它们。`set_text()` 也会清除它们，因为字节偏移不再描述新字符串。
- 输入用途使用 `pt.input_purpose_*`，并且是方法对，而不是属性。拖拽源使用 `begin_drag`，可传入 `(mime, data)` 对、`(mime, data, size)` 三元组、映射或 `pt.DragData`；大小为零表示以 NUL 结尾的文本，因此对于可能包含内嵌 NUL 字节的载荷，请传入显式大小。常量是 `pt.drag_action_*` 和 `pt.drop_stream_*`；颜色通过 `pt.rgb`/`pt.rgba` 构建。

0.9 新增：

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

- `App` 新增 `set_theme_metric`/`get_theme_metric`；`Window` 新增 `set_theme_color`/`get_theme_color`、`set_theme_metric`/`get_theme_metric` 和 `clear_theme_overrides()`。当令牌未被覆盖时，窗口 getter 返回应用值。
- 工厂函数 `pt.chip(parent, text)` 和 `pt.skeleton(parent)` 加入其余函数。标签片状态是 `set_chip_icon`、`set_chip_removable`、`set_chip_selected`，带有 `chip_removable`/`chip_selected` 属性，回调是无参数的 `on_chip_click`/`on_chip_remove`。
- `Widget.busy` 和 `Widget.invalid` 是属性，带有可链式调用的 `set_busy`/`set_invalid`。
- 表格编辑是 `set_table_editable`、`begin_cell_edit`、`commit_cell_edit`、`cancel_cell_edit` 和 `on_cell_edited`；选择是 `select_range`、`table_selection()`、`table_selection_count()`、`is_row_selected()` 和 `clear_table_selection()`；重排是 `set_columns_reorderable`、`move_column`、`on_columns_reordered` 和 `on_row_drag`。
- 树重命名是 `set_tree_renamable`、`begin_tree_rename`、`commit_tree_rename`、`cancel_tree_rename`、`tree_renaming()` 和 `on_tree_rename`。
- `pt.TextSpan` 携带 `family`（用于 `c_char * 64` 字段的 `bytes` 值）和 `size`，由 `pt.text_span_has_family` 和 `pt.text_span_has_size` 选择。
- `Window.begin_text_drag` 和 `begin_uri_drag` 在 0.9 中由包装器本身发出 `DeprecationWarning`，因此 `python -W error` 和 pytest 的默认过滤器会暴露迁移到 `begin_drag` 的必要性，绑定测试在 `assertWarns(DeprecationWarning)` 中调用它们。这两个方法在 1.0 冻结时连同它们封装的原生 C 入口点一起被移除。

0.10 新增：

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

- `pt.combobox(parent)` 是类似 `pt.label` 的工厂函数。`add_item`、`insert_item`、`remove_item` 和 `clear_items` 维护列表；`item_count`、`item_text`、`set_item_text`、`set_item_data`、`item_data`、`set_item_disabled` 和 `is_item_disabled` 是逐条目访问器。
- `pt.combobox_select_only` 和 `pt.combobox_filterable` 加入其他模块常量。`combo_mode`、`combo_selected`、`combo_text`、`combo_placeholder` 和 `popup_max_height` 是属性，并带有可链式调用的 `set_combo_mode`/`set_combo_selected`/`set_combo_text`/`set_combo_placeholder`/`set_popup_max_height` 对应方法；`open_popup` 和 `close_popup` 可链式调用，`popup_open` 是只读的。通过属性设置选择或文本永远不会触发回调，空选择读回为 `pt.list_index_none`。
- `set_item_data(index, data)` 在控件上保留一个以条目索引为键的 Python 对象 `dict`，并在插入或移除条目时重新映射键，因此映射会跟随 C 的位移；`data=None` 会删除该条目。`item_data(index)` 在 Python 设置过该对象时返回该对象，否则将原始原生指针作为 `int` 返回——包装器从不为 C 侧存储的值猜测 Python 对象。
- `on_combo_change` 分派 `(widget, index)`，`on_combo_text_change` 分派 `(widget, text)`，其中 `char *` 被解码为 `str`；两者都会在控件上保持其 ctypes trampoline 存活。
- 选择访问器在数据视图之间保持平行：列表上的 `list_select_range`、`list_select_toggle`、`list_selection()`、`list_selection_count`、`is_list_row_selected()` 和 `clear_list_selection()`；表格在其 0.9 的配对旁边新增 `select_toggle`；树上的 `tree_select_range`（可见行）、`tree_select_toggle`、`tree_selection()`、`tree_selection_count`、`is_tree_node_selected()` 和 `clear_tree_selection()`。`pt.a11y_role_combo_box`、`pt.a11y_role_list_box`、`pt.a11y_state_selectable` 和 `pt.a11y_state_multi_selectable` 暴露新的无障碍值。

0.3 的属性是 `a11y_role`、`a11y_name`、`a11y_description` 和 `a11y_state`（位掩码），外加只读的 `a11y_text`（一个 `(text, caret, selection_start, selection_end)` 元组或 `None`）和 `a11y_value`（`float` 或 `None`）。`set_tooltip(text)` 和 `set_tooltip_delay(milliseconds)` 附加延迟显示的工具提示。

`Window` 暴露 `root`、`is_open`、`width`、`height`、`scale`、`clipboard`、`show`、`hide`、`close`、`set_title`、`set_size`、`set_min_size`、`set_resizable`、`set_decorated`、`request_redraw`、`focus`、`on_gesture`、`set_long_press_delay`、`momentum_scrolling`、`begin_drag(types)`、`set_drag_image`、`clear_drag_image`、`on_stylus` 和返回 `Toast` 的 `show_toast(text, kind, duration_ms)`。`Menu(window)` 提供 `add_item`、`add_separator`、`show_at`、`show_below`、`dismiss`、`is_open` 和 `destroy`；`Dialog(window, title, message)` 提供 `set_title`、`set_message`、`add_action`、`on_close`、`show`、`close`、`is_open` 和 `destroy`。菜单和对话框由调用方拥有；在它们被关闭后、窗口关闭之前调用 `destroy()`。

`App` 暴露 `run`、`run_for`、`quit`、`running`、`add_timeout`、`cancel_timeout`、`color_scheme`、`accent_color`、`reduced_motion`、`set_theme_color`、`get_theme_color`、`on_a11y_event`、`enable_a11y`、`disable_a11y` 和 `a11y_status`。无障碍事件常量是 `pt.a11y_event_*`，角色是 `pt.a11y_role_*`，状态是 `pt.a11y_state_*`；使用按位或组合状态。浮层通知类型是 `pt.toast_info`、`pt.toast_success`、`pt.toast_warning` 和 `pt.toast_error`。

颜色和枚举是模块常量；`pt.rgb(0xRRGGBB)` 和 `pt.rgba(r, g, b, a)` 构建打包的 `0xRRGGBBAA` 值，`pt.theme_*` 常量命名每个设计令牌。工厂函数是 `box`、`row`、`column`、`label`、`button`、`entry`、`text_area`、`toggle`、`checkbox`、`radio`、`slider`、`progress`、`spinner`、`segmented`、`scroll`、`wrap`、`grid`、`list_view`、`divider`、`card`、`spacer`、`icon`、`icon_button`、`table`、`tree`、`tabs` 和 `sidebar`。

从源码树运行：

```sh
make run-python
# or
PT_LIBRARY="$PWD/build/libpt.so" PYTHONPATH=bindings/python \
    python3 examples/python/counter.py
```

加载器依次检查 `PT_LIBRARY`、`<repo>/build/libpt.so`、`./build/libpt.so`，然后是系统加载器路径。安装后，模块位于 `$PREFIX/share/pt/bindings/python/pt.py`；请将该目录加入 `PYTHONPATH`，或把 `pt.py` 复制到应用程序旁边。如果库安装在非标准前缀中，请设置 `LD_LIBRARY_PATH`。

无头测试：

```sh
PT_HEADLESS=1 PT_LIBRARY="$PWD/build/libpt.so" PYTHONPATH=bindings/python \
    python3 -m unittest discover -s bindings/python -p 'test_*.py'
```

`make check-bindings` 运行的正是这个。

## Java

`bindings/java/src/pt/Pt.java` 使用 Foreign Function & Memory API。没有 JNI、没有生成的胶水代码，除 JDK 外没有运行时依赖。该 API 是 C ABI 的直接 snake_case 镜像；句柄是 `MemorySegment`，字符串以 NUL 结尾的 UTF-8 形式跨边界传递。

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

回调是显式的函数式接口，每个注册方法都接收一个 `Arena`：

| 注册方法 | 接口 |
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

upcall stub 以及任何被捕获的 Java 对象必须在原生回调可能触发的整个期间保持可达。请使用存活于整个应用生命周期的 arena；不要使用在 `app_run` 返回前就关闭的受限 arena 注册回调。关闭和按键处理程序返回 `boolean`；从关闭处理程序返回 `false` 会否决关闭。

0.2 表面新增 `checkbox`、`radio`、`progress`、`spinner` 和 `segmented` 工厂函数（标签以 `String[]` 传递）、泛型 `widget_get_checked` / `widget_set_checked` 对、进度和滚动访问器、`entry_select_all`、`entry_set_selection`、`entry_get_selection`（返回 `int[]` 或 `null`）、`entry_undo`、`entry_redo`、`window_clipboard_set/get`、`app_get/set_theme_color` 以及 `theme_*` 令牌常量。

0.3 表面新增：

- `menu_create`、`menu_add_item`、`menu_add_separator`、`menu_show_at`、`menu_show_below`、`menu_dismiss`、`menu_is_open` 和 `menu_destroy`。
- `dialog_create`、`dialog_set_title`、`dialog_set_message`、`dialog_add_action`、`dialog_show`、`dialog_close`、`dialog_is_open`、`dialog_set_on_close` 和 `dialog_destroy`。
- `window_show_toast`、`toast_dismiss`、`toast_is_visible` 以及 `toast_*` 类型常量。
- `widget_set_tooltip`、`widget_set_tooltip_delay`、`widget_get/set_a11y_role`、`widget_get/set_a11y_name`、`widget_get/set_a11y_description`、`widget_get_a11y_state`、`widget_get_a11y_text`（返回 `A11yText` 记录或 `null`）和 `widget_get_a11y_value`（返回 `Float` 或 `null`）。
- `app_set_a11y_handler`，其中 `A11yHandler.handle(window, widget, event, data)`。`A11yText` 携带文本以及插入符和选择的字节偏移。

通过 `menu_add_item` 和 `dialog_add_action` 注册的回调使用现有的 `ClickHandler`。菜单和对话框是原生对象；不再需要它们时，调用相应的 `*_destroy` 方法。返回的浮层通知句柄是由窗口持有的非拥有式句柄。

0.4 表面新增：

- `entry_set_drag_enabled`、`entry_get_drag_enabled` 和 `window_begin_text_drag`（在 1.0 中移除）。
- `widget_get_drop_hover`、`widget_set_drop_target(widget, enter, motion, leave, drop, arena)` 和 `widget_clear_drop_target`。拖放处理程序以 Java `String` 接收 MIME 类型和拖放的文本。
- `window_set_gesture_handler`，其中 `GestureHandler.handle(window, event, data)`。`GestureEvent` 携带 `kind`、`phase`、`fingers`、`x`、`y`、`dx`、`dy`、`scale`、`rotation` 和原生 `target` 段。
- `window_set_long_press_delay`、`window_set_momentum_scrolling` 和 `window_get_momentum_scrolling`。
- `gesture_kind_*` 和 `gesture_phase_*` 常量。

0.5 表面新增：

- 工厂函数 `text_area(parent, placeholder)`、`wrap(parent)`、`grid(parent, columns)` 和 `list_view(parent)`。
- 显式文本区域方法：`text_area_set_placeholder`、`text_area_set_on_change`、`text_area_set/get_grow_to_content`、`text_area_scroll_to_caret`、`text_area_select_all`、`text_area_set_selection`、`text_area_get_selection`（一个 `int[]` 或 `null`）和 `text_area_undo`/`text_area_redo`。
- `wrap_set_gap`、`grid_set_columns`、`grid_get_columns`、`grid_set_gap`、`widget_set_grid_span` 和 `widget_get_grid_span`。
- 列表方法 `list_view_set/get_row_count`、`list_view_set/get_row_height`、`list_view_set_on_bind_row`、`list_view_set_on_select`、`list_view_set_on_activate`、`list_view_get/set_selected`、`list_view_refresh` 和 `list_view_scroll_to_row`。`ListBindHandler` 是 `handle(list, row, index, data)`；选择和激活使用现有的 `IndexHandler`。`list_index_none`（`-1`）清除选择。
- `app_set_reduced_motion`、`app_get_reduced_motion`、`ease`、`ease_value`、`easing_*` 常量和 `Pt.Transition`。transition 拥有一个受限 `Arena` 并实现 `AutoCloseable`；`value()`、`active()`、`to(target, duration_ms, easing)`、`advance(dt)`、`set(value)` 和 `state()` 暴露原生 28 字节记录。

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

只有在使用其状态的最后一次原生调用之后，才能关闭 transition；原生结构体由该 transition 的 arena 拥有。

0.6 表面新增：

- 工厂函数 `icon(parent, glyph)`、`icon_button(parent, glyph, accessibleName)`、`table(parent)`、`tree(parent)`、`tabs(parent)` 和 `sidebar(parent)`，以及 `icon_*`、`sort_*` 和 `table_column_none` 常量。
- 表格函数 `table_set/get_column_count`、`table_set_column_title`、`table_set/get_column_width`、`table_set_column_align`、`table_set_column_resizable`、`table_set/get_row_count`、`table_set/get_row_height`、`table_set_header_height`、`table_set_on_bind_cell`、`table_set_on_select`、`table_set_on_activate`、`table_set_on_sort`、`table_set_sort`、`table_get_sort`（一个 `int[]{direction, column}`）、`table_get_selected`、`table_set_selected`、`table_refresh`、`table_scroll_to_row` 和 `table_scroll_to_column`。`TableBindHandler` 是 `handle(table, cell, row, column, data)`，`SortHandler` 是 `handle(table, column, direction, data)`。
- 树函数 `tree_add_node`、`tree_clear`、`tree_get_root_count`、`tree_get_root_node`、`tree_node_get/set_text`、`tree_node_get/set_expanded`、`tree_node_has_children`、`tree_node_get_depth`、`tree_node_parent`、`tree_node_get_child_count`、`tree_node_get_child`、`tree_node_get/set_data`、`tree_get_visible_count`、`tree_get_visible_node`、`tree_get/set_selected`、`tree_set_row_height`、`tree_set_indent`、`tree_set_on_select`、`tree_set_on_activate`、`tree_refresh` 和 `tree_scroll_to_node`。`TreeNodeHandler` 是 `handle(tree, node, data)`。
- 标签页函数 `tabs_add_page`、`tabs_remove_page`、`tabs_get_page_count`、`tabs_get_page`、`tabs_set/get_page_title`、`tabs_get/set_active`、`tabs_set_on_change` 和 `tabs_set_strip_height`。侧边栏函数 `sidebar_add_item`、`sidebar_get_item_count`、`sidebar_get_item`、`sidebar_get/set_active`、`sidebar_get/set_compact`、`sidebar_set/get_header` 和 `sidebar_set_on_select`。
- `widget_set_icon`、`widget_get_icon`、`icon_set_size` 和 `icon_get_size`。
- `window_begin_uri_drag(window, String[])`（在 1.0 中移除）、`window_set_drag_image(window, int[] rgba, width, height)`（打包的 `0xRRGGBBAA`）、`window_clear_drag_image` 和 `window_set_stylus_handler`。`StylusEvent` 是一个记录，包含 `phase`、`tool`、`x`、`y`、`pressure`、`distance`、`tilt_x`、`tilt_y` 和 `rotation`。
- `uri_list_count(text)`、`uri_list_get(text, index)` 和 `uri_list(text)` 解析 `text/uri-list` 载荷。

0.7 新增：

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

- `RowKeyHandler.handle(view, row, data)` 返回 `long`；`RowTextHandler` 返回一个 `String`，Pt 会在查找期间将其复制到持久的 256 字节缓冲区中。`SortColumnsHandler.handle` 接收原始的 `MemorySegment` 数组，其中包含 `int` 类型的列和方向。
- `list_view_refresh_rows`、`list_view_set_row_key_handler`、`list_view_get/set_selected_key` 和 `list_view_set_row_text_handler` 加入列表表面。`row_key_none` 清除带键选择。
- 表格新增：`table_set/get_column_hidden`、`table_set_on_sort_columns`、`table_set/get_sort_columns`（展平的列/方向对）、`table_set_row_key_handler`、`table_get/set_selected_key`、`table_set_row_text_handler` 和 `table_refresh_rows`。`table_max_sort_columns` 是 4。
- `app_enable_a11y`、`app_disable_a11y` 和 `app_get_a11y_status` 使用 `a11y_status_*` 常量。窗口新增是 `window_set/get_popup_policy`、`window_get_drag_active`、`window_cancel_drag` 和 `window_set/get_drag_timeout`。

0.8 新增：

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

- `widget_set_drop_target` 有一个七参数结构化重载，在传统四个参数之后接收 `DropOfferEnterHandler` 和 `DropOfferHandler`；为两个处理程序都传入 `null` 会保留传统配对，而 `null` enter 和 enter_offer 会清除目标。
- Offer 访问器是 `drop_offer_mime_count`、`drop_offer_mime(offer, index)`（一个 `String` 或 `null`）、`drop_offer_has_mime`、`drop_offer_selected_mime` 和 `drop_offer_actions`。`drop_offer_read(offer, mime, max_bytes, handler, arena)` 返回一个流句柄，无法开始读取时为 `MemorySegment.NULL`，并且 `DropStreamHandler.handle` 接收 `(widget, stream, state, byte[] chunk, mime, data)`。`drop_stream_cancel/mime/size/active` 作用于该句柄；Pt 在其最终状态回调返回后释放流。
- `TextSpan(int start, int length, int weight, int slant, int color, int flags)` 作为数组通过 `widget_set_text_spans` 设置，对于不接受文本段的控件类型会返回 `false`。`DragData(String mime_type, String data, long size)` 同样作为数组传入；大小为零表示以 NUL 结尾的数据。
- 常量是 `text_span_underline|has_weight|has_slant|has_color`、`input_purpose_*`、`font_weight_*`、`font_slant_*` 和 `drag_action_*`，以及流状态 `drop_stream_data|done|cancelled|error`。颜色使用 `Pt.rgb` 和 `Pt.rgba`。

0.9 新增：

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

- `app_set/get_theme_metric` 和 `window_set/get_theme_metric`、`window_set/get_theme_color` 和 `window_clear_theme_overrides` 镜像 C 配对；窗口 getter 会回退到应用值。
- `Pt.chip(parent, text)` 和 `Pt.skeleton(parent)` 是类似 `Pt.label` 的工厂函数；标签片状态 setter 和 `chip_get_removable`/`chip_get_selected` 为 `chip_set_on_click`/`chip_set_on_remove` 复用 `ClickHandler`。
- 表格编辑是 `table_set/get_editable`、`table_begin_edit`、`table_commit_edit`、`table_cancel_edit`、`table_is_editing` 和带 `TableEditedHandler` 的 `table_set_on_cell_edited`；选择是 `table_select_range`、`table_get_selection`（一个 `int[]`）、`table_get_selection_count`、`table_is_row_selected` 和 `table_clear_selection`；重排是 `table_set_columns_reorderable`、`table_move_column`、带 `TableReorderHandler` 的 `table_set_on_columns_reordered` 和带 `TableRowDragHandler` 的 `table_set_on_row_drag`。
- 树重命名是 `tree_set/get_renamable`、`tree_begin_rename`、`tree_commit_rename`、`tree_cancel_rename`、`tree_is_renaming` 和带 `TreeRenameHandler` 的 `tree_set_on_rename`；处理程序在节点文本已经更新后触发。
- `TextSpan` 是一个八分量记录，带有六参数兼容构造函数；当设置了 `text_span_has_family`/`text_span_has_size` 时，会读取 `family` 和 `size`。
- `window_begin_text_drag` 和 `window_begin_uri_drag` 在 0.9 中是 `@Deprecated`（URI 版本接收 `(window, String[] uris)` 并分配自己的受限 arena）。两者在 1.0 冻结时被移除；传给 `window_begin_drag` 的 `DragData` 数组是唯一的拖拽源。

0.10 新增：

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

- `Pt.combobox(parent)` 是类似 `Pt.label` 的工厂函数。每个组合框函数都保留 C 名称：`combobox_add_item`、`combobox_insert_item`、`combobox_remove_item`、`combobox_clear`、`combobox_get_item_count`、`combobox_get/set_item_text`、`combobox_get/set_item_data`、`combobox_get/set_item_disabled`、`combobox_get/set_mode`、`combobox_get/set_selected`、`combobox_get/set_text`、`combobox_get/set_placeholder`、`combobox_open`、`combobox_close`、`combobox_is_open`、`combobox_get/set_popup_max_height`、`combobox_set_on_change` 和 `combobox_set_on_text_change`。
- 文本参数接收 `String`，并被编码到受限 arena 中；文本返回值是 `String`，并将 `NULL` 指针映射为 `""`。`Pt.combobox_get_text` 和 `combobox_get_placeholder` 就是这种行为。
- `Pt.combobox_set_item_data` 接收 `MemorySegment` 指针，因此调用方传入 `arena.allocate(...)` 或 `MemorySegment.NULL` 给出的内容；不会代其进行任何编组。
- `combobox_set_on_change` 复用 `IndexHandler`——`(widget, index)`——而 `combobox_set_on_text_change` 复用 `TextHandler`，其第二个参数是作为 `MemorySegment` 的原生 `char *`；两者都接收拥有 upcall stub 的 `Arena`，并接受 `null` 以清除回调。
- `Pt.combobox_select_only`/`combobox_filterable`、`Pt.a11y_role_combo_box`、`Pt.a11y_role_list_box`、`Pt.a11y_state_selectable` 和 `Pt.a11y_state_multi_selectable` 是新常量。空选择是 `Pt.list_index_none`，在 Java 中为 `-1`，因为 getter 为 C 的 `uint32_t` 返回有符号 `int`。
- 选择一致性镜像 C 名称：`list_view_select_range`、`list_view_select_toggle`、`list_view_get_selection`（一个 `int[]`）、`list_view_get_selection_count`、`list_view_is_row_selected`、`list_view_clear_selection`、新的 `table_select_toggle`，以及 `tree_select_range`、`tree_select_toggle`、`tree_get_selection`（一个 `MemorySegment[]`）、`tree_get_selection_count`、`tree_is_selected`、`tree_clear_selection`。

从源码树编译并运行：

```sh
javac -Xlint:all -d build/java bindings/java/src/pt/Pt.java Hello.java
java --enable-native-access=ALL-UNNAMED \
    -Dpt.library="$PWD/build/libpt.so" -cp build/java Hello
```

库的查找顺序是 `-Dpt.library`、`PT_LIBRARY`，然后是系统加载器路径。JDK 25 需要 `--enable-native-access=ALL-UNNAMED` 才能进行链接器 upcall；Makefile 和测试会显式传入它。安装后，请针对 `$PREFIX/share/pt/bindings/java/src/pt/Pt.java` 编译。

Java 测试是 `bindings/java/test/pt/PtTest.java`；使用 `make check-bindings` 或 `make java-test` 运行它。

<a id="binding-coverage"></a>

## 绑定覆盖范围

公共 C ABI 中的全部 412 个函数都已在 C++、Python 和 Java 中封装。一致性审计是硬性的 `make check` 门禁：它会扫描去除注释后的绑定源码以查找每个声明的函数名，允许零个未经审查的缺口，并为每种语言报告 `412/412`。经审查的允许列表为空。0.6 表面中的 124 个函数（比 0.9 所包含的少一个，因为 `pt_window_begin_uri_drag` 在 1.0 冻结时被移除）、为 0.7 数据视图、拖拽和输入表面引入的 19 个函数、为 0.8 结构化拖拽、流式 offer 和带样式文本表面引入的 19 个函数、为 0.9 主题、标签片/骨架屏、数据视图编辑和重命名表面引入的 44 个函数，以及为 0.10 组合框和多行选择表面引入的 40 个函数，全部存在于三个封装层中。两个在 0.9 中弃用的拖拽辅助函数在其弃用窗口期内，以弃用封装层的形式存在于每个绑定中，而不是被加入允许列表而绕过，因此任何接口在离开 ABI 之前都必须先经过每个绑定；现在它们已在同一变更中从 C ABI 和三个绑定中消失。

`make check` 运行 C 测试套件、`make check-bindings` 以及 API/ABI 检查。绑定套件是无头的：344 项 C++ 检查、33 个 Python unittest 用例和 298 项 Java 检查。覆盖层和无障碍测试覆盖菜单/对话框回调、浮层通知生命周期、工具提示、事件传递、启用/禁用状态以及文本/值暴露。0.10 测试覆盖组合框默认值和模式切换，条目添加/插入/移除/清除以及文本和数据访问器和禁用条目，静默的 `set_selected`/`set_text` 行为，占位符和字段文本，弹出层打开/关闭状态和最大高度往返，索引和文本回调，列表上通过范围、切换和清除实现的多行选择以及主索引的后续变化，额外的表格切换路径，按文档顺序进行的树范围、切换和清除，以及组合框和列表框角色及其可选择和多可选择状态。0.9 测试覆盖应用度量、窗口级令牌和度量覆盖及其清除、标签片创建和回调、骨架屏尺寸、busy 和 invalid 标志、表格单元格编辑和提交、多行选择范围、列重排和行拖拽回调、树重命名提交和第二次无编辑器提交，以及按文本段的字体族和字号。0.8 测试覆盖由 offer 驱动的拖放目标及其对传统配对的回退、带取消和在 `max_bytes` 处截断的分块流读取、多 MIME 源拖拽及其完成回调、带限制和清除的文本段往返，以及输入用途 setter/getter 对。0.7 测试覆盖跨行数变化和刷新的带键列表/表格选择、批量 `refresh_rows`、多列排序过滤和报告、隐藏列、行键/文本回调、弹出层策略、拖拽激活/取消/超时以及无障碍状态。0.6 测试覆盖表格列设置、绑定和排序，树构建和选择，标签页和侧边栏状态，图标 setter，URI 列表解析，拖拽图像写入和触控笔回调注册。0.5 测试检查列表行绑定、选择和激活，文本区域选择和内容自适应，wrap/grid 间距和跨度，缓动和 transition 重定向，减少动态效果切换以及新的无障碍角色。
