# Pt 快速入门

[English](../quickstart.md) | 简体中文

本指南介绍 C API。C++、Python 和 Java 绑定暴露相同的对象模型；请参阅
[bindings.md](bindings.md)。

## 你好，Pt

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

`pt_app_create` 会连接到合成器（除非设置了 `PT_HEADLESS=1`），并拥有
由它创建的所有窗口。`pt_app_run` 在最后一个窗口关闭、调用 `pt_app_quit` 或
连接失败时返回。

## 颜色与主题

公共颜色打包为 `0xRRGGBBAA`。辅助函数可以降低出错的概率：

```c
pt_color accent = pt_rgb(0x7AA2F7);          /* opaque */
pt_color veil   = pt_rgba(0, 0, 0, 128);     /* 50% black */
pt_color dim    = pt_color_alpha(accent, 96);
```

默认主题遵循应用级配色方案和强调色：

```c
pt_app_set_color_scheme(app, pt_color_scheme_dark);
pt_app_set_accent_color(app, pt_rgb(0x9ECE6A));
pt_app_set_theme_color(app, pt_theme_surface, pt_rgb(0x10151E));
pt_app_set_font_family(app, "Inter");
```

`pt_theme_accent` 以及派生的 `accent_hover`、`accent_text`、`accent_soft`、
`focus` 和 `selection` 令牌都跟随强调色。控件令牌包括用于输入表面的
`pt_theme_control` 和 `pt_theme_control_hover`，用于开关和滑块轨道的
`pt_theme_track`，用于凹陷区域的 `pt_theme_surface_sunken`，以及用于色调
按钮的 `pt_theme_accent_soft`。0.9 的令牌集增加了容器、轮廓、按下态和柔和状态
角色——`pt_theme_surface_container`、`pt_theme_surface_container_high`、
`pt_theme_outline`、`pt_theme_outline_variant`、`pt_theme_text_disabled`、
`pt_theme_accent_pressed`、`pt_theme_control_pressed`、`pt_theme_danger_soft`、
`pt_theme_success_soft`、`pt_theme_warning_soft`、`pt_theme_scrim`、
`pt_theme_skeleton`、`pt_theme_skeleton_highlight` 和
`pt_theme_selection_text`。可用 `pt_app_get_theme_color` 读取任意令牌。

几何参数也同样令牌化。`pt_app_get_theme_metric` 和
`pt_app_set_theme_metric` 读取和写入控制外观的十个度量：
`pt_theme_metric_radius_small`、`_radius_medium`、`_radius_large`、
`_radius_pill`、`_focus_width`、`_focus_offset`、`_elevation_low`、
`_elevation_high`、`_chip_height` 和 `_control_height`。

窗口可以覆盖单个令牌而不更改应用主题，对话框、大量使用调色板的面板或带有
品牌色调的窗口就是这样避开全局颜色的：

```c
pt_window_set_theme_color(window, pt_theme_background, pt_rgb(0x10151E));
pt_window_set_theme_metric(window, pt_theme_metric_radius_medium, 12.0f);
pt_color background = pt_window_get_theme_color(window, pt_theme_background);
pt_window_clear_theme_overrides(window); /* back to app defaults */
```

覆盖在绘制时解析；未设置的令牌会回退到应用主题。

控件级颜色会覆盖主题。主题更改会自动请求重绘已打开的窗口。
[theming.md](theming.md) 提供完整的令牌和度量表、强调色推导公式、深色/浅色
配对规则，以及 `test_theme` 断言的对比度下限。

## 布局

窗口根节点是一个垂直列。`pt_box_create` 创建行或列；`pt_row_create` 和
`pt_column_create` 是简写。布局是 Flex 的一个子集：

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

- 宽度/高度以及最小/最大尺寸都是逻辑像素。`pt_auto`（一个负值）请求基于
  内容的尺寸。
- `pt_widget_set_margin` 在控件外部添加空间；`pt_widget_set_padding` 在容器
  内部添加空间。
- `pt_widget_set_align_self` 为单个子控件覆盖容器的交叉轴对齐。容器的默认
  值是 `pt_align_stretch`。
- 主轴对齐支持 start、center、end、space-between、space-around 和
  space-evenly。
- `pt_scroll_create` 创建一个裁剪的垂直视口。滚轮事件会滚动它；高于视口的
  内容会获得由 Pt 绘制的滚动条。`pt_scroll_set_offset`、
  `pt_scroll_get_offset` 和 `pt_scroll_get_max_offset` 暴露逻辑像素偏移；
  设置函数会将其限制在已布局的内容范围内。
- `pt_spacer_create` 是一个空的、可增长的间隔器，适合将内容推到行或列的
  末尾。

### 响应式容器

`pt_wrap_create` 将子控件排布到适合可用宽度的行中。`pt_grid_create(parent,
columns)` 将宽度分成相等的列。两者都接受独立的行间距和列间距，并在窗口
调整大小时重排：

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

- 换行容器会测量每个子控件，当下一个子控件不再适合时开始新的一行。主轴
  对齐按行应用，因此 `space-between` 也会分散最后一行。
- 网格单元格共享其最高子控件的行高。`pt_widget_set_grid_span` 默认为 1，
  并限制为列数；无法容纳的跨度会开始下一行。
- 两个容器在交叉轴上都默认为 `pt_align_stretch`。对于标签片、按钮或其他
  固有宽度子控件，请使用 `pt_widget_set_align_self(child, pt_align_start)`，
  而不是依赖它们请求的尺寸。

控件按创建顺序追加到父控件。`pt_widget_destroy` 移除子树。在回调活动期间，
优先销毁并重新创建子树，而不是修改子指针数组。

## 控件与回调

可用控件：盒/行/列、标签、按钮、输入框、组合框、文本区域、切换、复选框、
单选按钮、滑块、进度条、旋转指示器、分段控件、标签片、骨架屏、滚动视图、
虚拟化列表、表格、树、选项卡、侧边栏、图标、图标按钮、换行容器、网格、
分隔线、卡片和间隔器。

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

按钮变体有 `pt_button_primary`、`pt_button_secondary`、`pt_button_ghost`、
`pt_button_danger` 和 `pt_button_soft`（一种色调强调表面）。滑块和进度值位于
`[0, 1]` 范围内。回调接收控件以及通过设置函数注册的 `user_data` 指针。
`user_data` 及其指向的任何内容都必须保持有效，直到回调被替换或应用被销毁。

密码输入框会用圆点替换其内容：

```c
pt_entry_set_password(entry, true);
```

## 现代控件

复选框、单选按钮和切换共用一个选中状态 API。同一父控件下的同级单选按钮
互斥：

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

进度条在不确定状态下会自行播放动画；旋转指示器在可见时始终播放动画：

```c
pt_widget *upload = pt_progress_create(card);
pt_progress_set_value(upload, 0.62f);

pt_widget *sync = pt_progress_create(card);
pt_progress_set_indeterminate(sync, true);

pt_spinner_create(card);
```

分段控件会复制其标签，并通过 `pt_index_callback` 报告选中的索引：

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

标签片是紧凑的药丸形切换控件或筛选器。它们带有可选字形、可选移除按钮和
选中状态：

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

骨架屏是一个空的闪烁块，在内容加载期间使用：

```c
pt_widget *placeholder = pt_skeleton_create(card);
pt_widget_set_height(placeholder, 12);
```

两个控件级状态覆盖了现代状态矩阵的其余部分。`busy` 将控件绘制为正在等待
任务，`invalid` 用危险令牌给边框着色，键盘驱动的控件会绘制一个焦点可见环，
其尺寸由 `pt_theme_metric_focus_width` 和 `pt_theme_metric_focus_offset`
决定：

```c
pt_widget_set_busy(save, true);
pt_widget_set_invalid(email, true);
bool waiting = pt_widget_get_busy(save);
```

### 组合框

组合框是一个带有选项列表的字段。默认模式是 `pt_combobox_select_only`：该
字段不是编辑器，点击它会打开列表，选择某项会通过 `pt_index_callback` 报告
索引。`pt_combobox_filterable` 将字段变成输入框，并在用户键入时用不区分
大小写的子串匹配筛选弹出层。

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

项文本会被复制，`pt_combobox_set_item_data` 则不会：该指针是借用的，当插入
或移除其他项时，该指针会随该项一起移动。未选择任何内容时，选择为
`pt_list_index_none`，并且 `pt_combobox_set_selected` 和
`pt_combobox_set_text` 都是静默的，因此加载已保存的值绝不会再次进入回调。
禁用项仍留在列表中，但高亮会跳过它们。

键盘：F4 或 Alt+Down/Alt+Up 打开列表，Up/Down 移动高亮（并打开可筛选的
框），Page_Up/Page_Down 将弹出层翻一页，Home/End 跳转，Enter 或 Space
提交高亮行并关闭，Escape 关闭且不改变选择，Tab 关闭并将焦点移到下一处。
仅选择框还会对项文本执行键入预选；可筛选框则把 Home、End 和空格留给
编辑器。弹出层像菜单一样是覆盖层：当合成器提供原生 `xdg_popup` 时，仅
选择框会获得一个，因此列表可以溢出窗口边缘；可筛选框则留在窗口内，以便
编辑器保持焦点。当窗口内放置无法容纳在下方时，会翻转到字段上方或限制到
边缘。

## 弹出层与选择器

### 应用自有弹出层

`pt_popup` 在菜单和工具提示所用的同一 `xdg_popup` 后端上打开应用内容。
弹出层会接管内容树，因此交接后不要销毁或复用该内容。锚点使用父窗口逻辑像素；
合成器会翻转并滑动原生表面以保持可见，无头回退则在窗口内执行相同行为。

```c
static void on_popup_dismiss(pt_popup *popup, void *data) {
    (void)popup;
    (void)data;
}

pt_widget *panel = pt_card_create(NULL);
pt_label_create(panel, "Popup content");

pt_popup *popup = pt_popup_create(window, panel);
pt_popup_set_on_dismiss(popup, on_popup_dismiss, NULL);
pt_popup_anchor anchor = {.x = 24.0f, .y = 96.0f, .width = 160.0f, .height = 32.0f};
pt_popup_show(popup, anchor);
```

通过外部点击、Escape 或抓取中断触发的关闭会在 `pt_popup_is_open` 变为 false
之后调用回调。`pt_popup_destroy` 会关闭弹出层并释放内容，但不调用回调。
父窗口拥有弹出层，因此销毁窗口会使该句柄失效。

### 可搜索选择与选择器

1.2 的四个选择器都是编辑器字段，拥有相同的焦点、键盘和无障碍模型。它们的
控件类型报告为 `pt_widget_combobox`，但只有 `pt/picker.h` 中的函数适用；
调用 `pt_combobox_*` 项 API 是空操作。程序化设置函数是静默的，无效的文本提交
返回 false，保留原值并记录 `pt_get_last_error`。

```c
pt_widget *city = pt_select_create(card);
pt_select_set_placeholder(city, "Choose a city");
pt_select_add_item_with_data(city, "Berlin", &berlin);
pt_select_add_item_with_data(city, "Lisbon", &lisbon);
pt_select_set_max_popup_rows(city, 6);

pt_widget *deadline = pt_date_picker_create(card);
pt_date date = {.year = 2026, .month = 11, .day = 22};
pt_date_picker_set_date(deadline, date);
pt_date_picker_set_format(deadline, pt_date_format_iso);

pt_widget *retries = pt_number_picker_create(card);
pt_number_picker_set_range(retries, 0.0, 10.0);
pt_number_picker_set_step(retries, 1.0);
pt_number_picker_set_value(retries, 3.0);

pt_widget *accent = pt_color_picker_create(card);
pt_color_picker_set_hex(accent, "#336699");
```

在选择器中键入会按 ASCII 大小写不敏感的子串过滤；Down、Up、F4 和
Alt+Down/Alt+Up 打开列表，Enter 提交，Escape 恢复已提交值，空查询下按
Delete 清除。日期字段解析所选图式（始终接受 ISO），弹出层是固定的六周日历。
数字字段会钳制到其范围，并以 `.` 作为提交的小数分隔符。颜色字段接受
`#RRGGBB` 和 `#RRGGBBAA`，调色板以 24 种常用颜色开始。无障碍值分别是
选中索引、`YYYYMMDD`、数字或归一化 RGB。数字选择器报告步进按钮角色
（`pt_a11y_role_spin_button`），没有展开状态；而三个带弹出层的选择器在关闭时
是组合框，在打开时是列表框。

## 媒体、图标与持久化

### 图标图像

`pt_icon_image` 是引用计数的栅格图像，可从 PNG 或 SVG 文件、内存或 hicolor
主题名加载。控件会持有自己的引用，因此加载句柄可以立即释放。加载器嗅探内容：
`<svg` 或 `<?xml` 前缀选择 SVG 路径，其余字节流仍走 PNG。

```c
pt_icon_image *image = pt_icon_image_from_name("document-open", 18);
if (image) {
    pt_widget *open = pt_icon_button_create(card, pt_icon_none, "Open");
    pt_button_set_icon_image(open, image);
    pt_button_set_icon_tint(open, 0xFF3366FFu);
    pt_window_set_icon(window, image);
    pt_icon_image_destroy(image);
}
```

同一槽位也接受 SVG 源；当主题只提供矢量图标时，hicolor 查找优先使用
`scalable/*.svg`：

```c
const char *mark = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
                   "<path d=\"M4 12l6 6 10-12\" fill=\"none\" stroke=\"#fff\" "
                   "stroke-width=\"2\"/></svg>";
pt_icon_image *vector = pt_icon_image_load_memory(mark, strlen(mark), 24);
if (vector) {
    pt_widget *done = pt_icon_button_create(card, pt_icon_none, "Done");
    pt_button_set_icon_image(done, vector);
    pt_icon_image_destroy(vector);
}
```

SVG 支持包括路径（含弧）、基本形状、组、变换、带宽度/端点/连接/虚线的
填充和描边、不透明度、填充规则、线性/径向渐变、`defs`、内联 `style`
和简单 class 规则。不支持文本、图像、滤镜、遮罩、`clipPath`、
`use`/`symbol`、标记、动画以及完整 CSS 级联：解析器会忽略该元素，但仍会访问
其后代。没有可绘制形状的文档会通过 `pt_get_last_error` 报告并被拒绝。
光栅化保留固有宽高比并在请求的像素框中居中；结果进入与 PNG 相同的按尺寸和
缩放缓存。

窗口图标通过 `xdg-toplevel-icon-v1` 尽力而为；没有该全局的合成器会忽略它，
且不记录错误。

### 剪贴板图像与 URI 列表

`pt_window_clipboard_set_image_png` 接受预乘 `0xAARRGGBB` 像素并写入
`image/png`；获取函数返回调用方拥有的同布局像素。两个方向都受文本传输上限和
8192×8192 解码像素上限约束，解码失败返回 NULL。

```c
pt_color pixels[4] = {0xFF336699u, 0xFF336699u, 0xFF336699u, 0xFF336699u};
if (pt_window_clipboard_set_image_png(window, pixels, 2, 2)) {
    int width = 0;
    int height = 0;
    pt_color *copy = pt_window_clipboard_get_image_png(window, &width, &height);
    pt_free(copy);
}

const char *files[] = {"/home/user/notes.txt"};
pt_window_clipboard_set_uri_list(window, files, 1);
```

`pt_window_clipboard_set_uri_list` 写入 CRLF 行和百分号编码的 `file://` URI。
设置一种剪贴板类型会替换之前的类型。

### 字体回退链

`pt_app_set_font_fallbacks` 安装应用级回退链；
`pt_app_set_font_fallbacks_for_family` 为某个主字体族覆盖它。文本被拆分为
字形段，每段使用第一个覆盖它的字体族。回退链是进程范围的，显式链是权威的。

```c
const char *fallbacks[] = {"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic"};
pt_app_set_font_fallbacks(app, fallbacks, 3);

const char *mono[] = {"Noto Sans Mono", "Noto Sans Mono CJK SC"};
pt_app_set_font_fallbacks_for_family(app, "monospace", mono, 2);

int missing = pt_app_font_uncovered_count(app, NULL, mixed_text, strlen(mixed_text));
```

`pt_app_font_uncovered_count` 返回仍渲染为豆腐块的码点数。family 传 NULL 表示
应用配置的默认字体族。

### 列布局持久化

为每一列提供稳定的键，然后保存和恢复带版本的布局 blob。blob 由应用拥有；
标题、对齐和可调整大小属于模型配置，不在其中。

```c
pt_table_set_column_key(table, 0, "name");
pt_table_set_column_key(table, 1, "size");

char *layout = pt_table_columns_save(table);
if (layout) {
    /* store layout where the application keeps configuration */
    pt_table_columns_restore(table, layout);
    pt_free(layout);
}
```

`pt_table_columns_save` 记录显示顺序、列宽、隐藏标志和多列排序状态。
`pt_table_columns_restore` 在改动任何内容之前校验整个 blob，并拒绝未知版本、
格式错误的行、缺失或重复的键以及列数不匹配，同时保持当前布局不变。

## 文本编辑

输入框是单行编辑器，选择使用 UTF-8 字节偏移。鼠标拖动选择，双击选择一个
单词，三击选择全部。键盘模型如下：

| 快捷键 | 操作 |
| --- | --- |
| Shift+Left/Right | 按字符扩展选择 |
| Shift+Home/End | 将选择扩展到行首/行尾 |
| Ctrl+A | 全选 |
| Ctrl+C / Ctrl+X / Ctrl+V | 复制、剪切、粘贴 |
| Ctrl+Z / Ctrl+Shift+Z 或 Ctrl+Y | 撤销、重做 |
| Backspace/Delete | 替换或清除选择 |
| Enter | 提交 |

撤销保留 32 个快照，并在通过 `pt_widget_set_text` 整体替换文本时清除。
选择状态也可以通过编程方式访问：

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

剪切和复制写入显示剪贴板；粘贴读取它。将非空选择拖出输入框会启动外部文本
拖动（可通过 `pt_entry_set_drag_enabled(entry, false)` 禁用）。应用可以直接
使用同一个剪贴板。返回的字符串由 malloc 分配，必须用 `pt_free` 释放：

```c
pt_window_clipboard_set(window, "copied text");

char *text = pt_window_clipboard_get(window);
if (text) {
    puts(text);
    pt_free(text);
}
```

在真实的 Wayland 会话中，剪贴板传输是异步的；获取函数会以有界超时等待
选择，并在没有可用文本时返回 `NULL`。在无头测试中，它同步运行。

### 多行文本区域

文本区域共享输入框的编辑模型，但会在控件宽度处换行。Enter 插入换行，
Up/Down 在换行后的行之间保持水平光标目标，Home/End 作用于视觉行：

```c
pt_widget *editor = pt_text_area_create(card, "Write a note…");
pt_text_area_set_on_change(editor, on_text, NULL);
pt_text_area_set_grow_to_content(editor, true);

pt_text_area_select_all(editor);
pt_text_area_set_selection(editor, 0, 18);
pt_text_area_undo(editor);
pt_text_area_redo(editor);
```

文本通过 `pt_widget_get_text` 和 `pt_widget_set_text` 读取和替换。文本区域的
默认高度为 160 px；随内容增长会忽略显式高度，并将控件调整为其换行后文本的
大小，适合笔记和日志。当内容超出可见区域时，键入会滚动到光标处；
`pt_text_area_scroll_to_caret` 以编程方式执行相同操作。文本区域仅编辑纯
文本，并暴露 `text_frame` 无障碍角色。

### 带样式的文本段

标签和文本区域接受字节范围样式。每个 `pt_text_span` 指定范围，并为适用的
字段设置标志位，因此未设置的粗细、倾斜或颜色绝不会被读取：

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

偏移是 UTF-8 字节偏移：设置函数会将它们吸附到字符边界，限制到当前文本，
并丢弃最终为空的范围，然后复制剩余部分。文本段还可以更改字体：设置
`pt_text_span_has_family` 和 `family`（以 NUL 结尾的名称，最多 63 字节）
和/或 `pt_text_span_has_size` 和 `size`（以磅为单位）。混合字号的文本段
共享一个基线，因此大标题文本段和小上标文本段会位于同一行。文本段在换行、
测量、选择和光标移动后仍然存在，并在用户键入时跟随文本。用
`pt_widget_set_text` 替换整个字符串会清除它们，因为字节偏移不再描述新
文本。文本段仅用于样式：它们不携带链接或嵌入对象，完整的双向段落布局仍
留待 1.0 之后。方向模型、提议的追加式 API、逻辑到视觉偏移映射和一致性
测试在 [RTL 方案](rtl.md) 中说明。

### 输入用途

`pt_widget_set_input_purpose` 告诉合成器和输入法输入框期望什么，以便提供
正确的键盘、补全和自动大写。密码掩码仍是一个独立问题，由
`pt_entry_set_password` 控制：

```c
pt_widget_set_input_purpose(email, pt_input_purpose_email);
pt_widget_set_input_purpose(site, pt_input_purpose_url);
pt_widget_set_input_purpose(secret, pt_input_purpose_password);
pt_entry_set_password(secret, true);
```

用途只是提示：合成器可以自由忽略它们。预编辑、提交、周围文本和删除周围
文本会针对聚焦控件遵循 `text-input-v3`，输入法提交会合并为单个撤销步骤。

### 自定义控件的自定义输入法

输入框会自动获得文本输入。自行拥有文本的控件——终端、代码编辑器或画布——
可以通过窗口级回调接入同一套输入法：

```c
static void on_ime_commit(pt_window *window, const char *text, void *data) {
    (void)window;
    (void)data;
    append_text(text); /* the application owns insertion and redraw */
}

static void on_ime_preedit(pt_window *window, const char *text, int32_t begin,
                           int32_t end, void *data) {
    (void)window;
    (void)begin;
    (void)end;
    (void)data;
    set_preedit(text); /* an empty string ends the composition */
}

pt_window_set_ime_callbacks(window, on_ime_commit, on_ime_preedit, state);
pt_window_set_ime_enabled(window, true);
pt_window_set_ime_cursor_rect(window, caret_x, caret_y, 1, 18);

pt_window_show_input_panel(window);  /* ask for an on-screen keyboard */
pt_window_hide_input_panel(window);  /* withdraw the request */
```

Pt 不会替你插入文本：提交回调负责插入，预编辑回调替换当前组合内容。光标
偏移是预编辑文本中的 UTF-8 字节偏移，未指定时为 -1。光标矩形采用窗口逻辑
像素；在输入法禁用期间它会被记住，并在启用时推送。回调在 UI 线程上运行，
文本仅在本次调用期间有效。Pt 在每次更改时重新发送完整周边状态，而不是依赖
增量确认，这正是避免 KWin/fcitx5 和 sway 焦点失步丢提交的手段。调用
`pt_window_set_ime_enabled(window, false)` 可再次关闭自定义路径；之后输入框
获得焦点时会自行启用文本输入。

`pt_window_show_input_panel` 和 `pt_window_hide_input_panel` 为窗口的活跃
text-input 表面转发 text-input-v3 v2 请求，并在记录的期望状态改变时返回 true。
它们只是提示：合成器决定是否显示面板，Pt 无法观察结果。当窗口没有活跃的
text-input 对象、合成器仅支持 v1 或窗口处于无头模式时，请求不会立即发送；
期望状态会被记住，并在 IME 下次激活时（包括焦点变化之后）转发一次。协议将
与当前状态相同的重复请求视为无害，因此重放没有副作用。对 NULL 窗口调用任一
函数返回 false。

## 主选区

主选区是鼠标中键粘贴背后的 X11 风格缓冲区。Pt 以与剪贴板一对函数相同的
形态暴露它：

```c
pt_window_primary_set(window, "selected text");

char *text = pt_window_primary_get(window);
if (text) {
    pt_widget_set_text(entry, text); /* middle-click paste */
    pt_free(text);
}
```

两侧在没有协议时也可用：当 seat 不暴露
`wp_primary_selection_unstable_v1`，或在无头运行时，Pt 回退到进程内缓冲区。
在 Wayland 上，选区使用触发事件的 serial 设置——拖动选择对应指针释放，
键盘选择对应最后一次按键。没有触发事件的编程设置不会发送给合成器；如果
应用要镜像在自己的输入回调之外产生的选区，可通过
`pt_window_primary_set_serial(window, text, serial)` 显式传入 serial。

`pt_window_primary_get` 返回由调用方拥有的 malloc 分配的 UTF-8 字符串；
用 `pt_free` 释放。中键粘贴是应用策略——Pt 不会自行粘贴到输入框——并且主
选区与 Ctrl+C/Ctrl+V 和 `pt_window_clipboard_set/get` 使用的剪贴板彼此独立。

## 虚拟化列表

`pt_list_view_create` 构建一个平面列表，其行仅在可见时存在。设置行数和固定
行高，然后在绑定回调中绑定应用数据。默认渲染器显示该行的文本：

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

Pt 会实例化视口内的行，再加一行额外缓冲（overscan），并在滚动时重新绑定
回收的行，因此控件数量保持不变。绑定回调在每次重新绑定行时运行：在这里
配置文本、样式或自定义单元格，而不是保留行指针。对于自定义行，清除默认
文本和子控件，然后添加普通控件：

```c
pt_widget_set_text(row, "");
pt_widget_clear_children(row);
pt_label_create(row, title);
pt_widget *badge = pt_label_create(row, "new");
pt_widget_set_grow(badge, 1);
```

行是列表拥有的行方向容器；绝不要销毁它们。选择使用 `pt_list_index_none`
表示“无行”：

```c
pt_list_view_set_selected(list, 42);           /* callback is not fired */
uint32_t index = pt_list_view_get_selected(list);
pt_list_view_set_selected(list, pt_list_index_none);
```

点击一行会选中它并触发选择回调。聚焦的列表用 Up/Down、Page Up/Page Down、
Home 和 End 移动选择并显示它；Enter 或 Space 为选中的行触发激活回调。修改
应用数据后，调用 `pt_list_view_refresh` 重新绑定可见行；
`pt_list_view_scroll_to_row` 显示某个索引。

选择可以跟随应用标识而不是行号。注册一个键回调，为每行返回一个稳定的非
零键：

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

有了键之后，只要键仍然存在，选择就能在重新排序、刷新和行数变化后保留；
当行消失时，查找失败，报告的选择变为 `pt_row_key_none`。没有键回调时，
更改行数会像以前一样限制或清除现在超出范围的选择。

行文本回调也会为键入预选提供数据。在不按 Ctrl、Alt 或 Logo 的情况下键入
可打印字符，会选择下一个标签以所键入前缀开头的行，并在末尾回绕；重复键入
同一字符会在匹配项之间循环，前缀会在 700 ms 后重置。没有文本回调时，键入
预选只能看到已绑定到实例化行的文本。

编辑少量模型行后，用 `pt_list_view_refresh_rows(list, first, count)` 仅重新
绑定这些行。它会重新绑定与该范围相交的可见行，并保持其他绑定和行池不变，
对于就地编辑来说比完整刷新更便宜。

列表暴露 `list` 和 `list_item` 无障碍角色，并报告选中的行。

## 表格、树与导航

表格共享列表回收器，但增加了列、表头和排序。Pt 不会重新排列应用数据；
排序回调会更新模型并刷新表格：

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

点击表头会在升序 → 降序 → 无之间循环。Shift 单击会添加或更新次要列；最多
保留 `PT_TABLE_MAX_SORT_COLUMNS`（4）列，按优先级排序，第一项作为主排序键。
`on_sort_columns` 回调接收完整状态，是首选钩子；`pt_table_set_on_sort`
仍保留给只处理一列的代码。`pt_table_set_sort_columns` 设置指示器状态而不
触发回调，`pt_table_get_sort_columns` 将其读回。

当列可调整大小时，拖动列边缘会调整其大小。通过代码或拖动设置的宽度按列
存储，并在刷新后保留；用 `pt_table_get_column_width` 读取它们。用
`pt_table_set_column_hidden(table, 3, true)` 隐藏列。隐藏列保留宽度、标题和
单元格绑定，但会被布局、绘制、命中测试和无障碍树跳过。

选择像列表一样支持稳定键：

```c
pt_table_set_selected_key(table, 7101);
uint64_t key = pt_table_get_selected_key(table); /* pt_row_key_none */
```

有了行键和行文本回调，即使可见范围发生变化，选择和键入预选也会在重新排序
和刷新后跟随模型行。批量模型编辑后，
`pt_table_refresh_rows(table, first, count)` 仅重新绑定某个范围内的可见行。
`pt_table_set_selected` 和 `pt_table_get_selected` 仍用
`pt_list_index_none` 表示“无行”；使用 `pt_table_scroll_to_row` 或
`pt_table_scroll_to_column` 显示内容。

表格暴露 `table`、`column_header`、`row` 和 `table_cell` 无障碍角色。AT-SPI
桥接层会在行和单元格上发布行索引和列索引，并在表格及其表头上发布当前排序列、
排序方向和优先级。每个启用的行还带有 `STATE_SELECTABLE`，集合中的行带有
`STATE_SELECTED`，而表格本身带有 `STATE_MULTISELECTABLE`，因此屏幕阅读器
知道未选中的行仍可被添加到选择中。

就地编辑会将任何单元格变成输入框。`pt_table_set_editable` 启用它，
`pt_table_begin_edit(table, row, column)` 在可见单元格上打开编辑器（双击
单元格也会这样做），Enter 或焦点丢失会提交，Escape 会取消：

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

编辑器是真正的输入框，因此选择、剪贴板和撤销的行为与普通字段完全相同。
提交会将文本复制到单元格并触发回调；如果单元格内容是派生的，请在回调中
重新绑定模型。

表格还维护一个独立于主行的多行选择：

```c
pt_table_select_range(table, 4, 9);            /* shift-click and marquee do this */
uint32_t count = pt_table_get_selection_count(table);
uint32_t rows[16];
uint32_t n = pt_table_get_selection(table, rows, 16);
bool marked = pt_table_is_row_selected(table, 5);
pt_table_clear_selection(table);
```

从某一行或最后一行下方的空白视口中开始的指针拖动，会随指针移动扫出
橡皮筋框选：不按修饰键时，与框选相交的行会替换选择；按住 Ctrl 时则追加到
选择中。
在该空白区域普通按下会清除选择，并且绝不会激活先前的主行；Ctrl 会将现有
集合保留为追加式基础。框选按几何位置选择，而不是按锚点与指针之间的索引
范围，因此它会选中它触及的每一行，即使行被隐藏或拖动越过视口边缘也是
如此。Ctrl 单击切换一行，Shift 单击从选择锚点扩展；两者都会调整集合，而
不会像松开时的普通单击那样替换选择。键盘操作与此对应：Shift+方向键从锚点
扫出一个范围，未按修饰键的方向键会将集合收缩到它落在的行，Ctrl+A 选择所有
行。
公开的选择设置辅助函数是静默的，因此应用可以重建选择而不产生回调回声；每个
移动主行的手势都会通过 `pt_table_set_on_select` 报告它，而当主行已经是
高亮行时，Ctrl+A 不会报告。主选择仍如上所述跟随稳定键。同一套词汇适用于
列表（`pt_list_view_select_range`、`pt_list_view_select_toggle`、
`pt_list_view_get_selection`、`pt_list_view_get_selection_count`、
`pt_list_view_is_row_selected`、`pt_list_view_clear_selection`）和树
（`pt_tree_select_range`、`pt_tree_select_toggle`、`pt_tree_get_selection`、
`pt_tree_get_selection_count`、`pt_tree_is_selected`、
`pt_tree_clear_selection`）。在那里，范围作用于可见行，而获取函数和切换函数
通过句柄寻址节点，因此折叠的分支只算作它显示的一行。

启用重新排序后，可以通过拖动重新排列表头，行也可以开始拖动。两者都会触发
回调，以便应用将相同的排列应用到其模型；`pt_table_move_column` 以编程方式
执行相同的更改：

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

### 重命名树节点

树可以就地重命名节点。`pt_tree_set_renamable` 启用该手势，F2 或缓慢双击会
在节点标签上打开编辑器，重命名回调会报告提交的文本：

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

提交会在回调运行前将文本写入节点，因此 `pt_tree_node_get_text` 已经报告
新标签。

树将节点存储在控件中，因此句柄会保持稳定，直到清除树或移除祖先。只有
展开的分支会被实例化：

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

使用 `pt_tree_node_set_data` 存储模型指针，使用 `pt_tree_get_visible_node`
访问视口。更改文本或展开状态后，`pt_tree_refresh` 会重建可见行。聚焦的树
用 Up/Down 移动，用 Home/End 跳转，用 Page Up/Page Down 翻页，用 Right
展开，用 Left 折叠或移动到父节点。键入前缀会选择下一个文本以它开头的可见
节点，使用与列表和表格相同的 700 ms 循环。对于可见的树行，AT-SPI 桥接层会
发布 `level`、`expanded` 和 `selected` 无障碍属性。每一行带有
`STATE_SELECTABLE`，选中的行带有 `STATE_SELECTED`，树带有
`STATE_MULTISELECTABLE`，可展开的分支在打开时带有 `STATE_EXPANDED`，在
关闭时带有 `STATE_COLLAPSED`。

选项卡和侧边栏构成导航外壳。页面是普通子控件：

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

当标题溢出时，选项卡栏会水平滚动；除非启用了减少动态效果，活动指示器会
滑动。图标取自一套 24×24 的轮廓图标（`pt_icon_chevron_down`、
`pt_icon_search`、`pt_icon_menu`、`pt_icon_close`、`pt_icon_check`、
`pt_icon_folder`、`pt_icon_document` 以及另外 14 个）。
`pt_icon_button_create(parent, glyph, "Accessible name")` 构建一个带工具提示
的纯图标按钮，`pt_widget_set_icon` 可装饰标签、按钮和表格单元格。选项卡
暴露 `tab_list`、`tab` 和 `tab_panel` 角色；侧边栏暴露带名称的可选条目。

## 动效与减少动态效果

缓动辅助函数将归一化时间映射为进度。`pt_transition` 是一个由调用者拥有的
值，会从其当前值重新设定目标，因此被中断的动画不会跳变：

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

可用曲线有 `linear`、`in_quad`、`out_quad`、`in_out_quad`、`out_cubic`、
`in_out_cubic` 和 `out_back`。减少动态效果是应用级偏好。启用后，框架控件
会立即切换；自定义控件应完成过渡，而不是对它们进行插值：

```c
pt_app_set_reduced_motion(app, true);
bool reduced = pt_app_get_reduced_motion(app);
```

默认值来自 `PT_REDUCED_MOTION` 环境变量（`1`、`true` 或 `yes`），因此可以
在启动时应用会话范围的偏好。

## 焦点与键盘输入

单击或按 Tab 可聚焦交互控件。Shift+Tab 向后移动。Space 或 Enter 激活按钮、
切换、复选框和单选按钮；聚焦的输入框处理光标移动、选择、Home/End、
Backspace、Delete、Enter 提交，以及上面的剪贴板快捷键。滑块使用方向键
（Shift 用于更大的步长）、Home 和 End。

窗口级按键回调在聚焦控件处理之前运行，并且可以消费按键：

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

修饰键掩码有 `pt_key_modifier_shift`、`ctrl`、`alt` 和 `logo`。键是 XKB
键符，因此 `0xFF1B` 是 Escape，`0xFF09` 是 Tab，`0xFF0D` 是 Return。

`pt_window_focus(window, widget)` 以编程方式聚焦控件；`pt_widget_has_focus`
查询它。`pt_widget_set_enabled(widget, false)` 将控件从命中测试和焦点遍历中
排除。

## 窗口级输入回调

三个窗口级回调在控件分发之前暴露原始事件流。它们在 UI 线程上运行，并且
可以调用 API：

```c
static void on_focus(pt_window *window, bool focused, void *data) {
    (void)window;
    (void)focused;
    (void)data;
}

static bool on_key_event(pt_window *window, uint32_t key, uint32_t modifiers,
                         const char *utf8, void *data) {
    (void)window;
    (void)key;
    (void)modifiers;
    (void)utf8;
    (void)data;
    return false; /* false lets the focused widget handle the key */
}

static bool on_pointer(pt_window *window, const pt_pointer_event *event,
                       void *data) {
    (void)window;
    (void)event;
    (void)data;
    return false;
}

pt_window_set_focus_callback(window, on_focus, NULL);
pt_window_set_key_event_callback(window, on_key_event, NULL);
pt_window_set_pointer_callback(window, on_pointer, NULL);
```

分发顺序是：覆盖层按键处理、原始按键回调、传统
`pt_window_set_key_callback`，然后是聚焦控件。原始回调返回 `true` 会为控件
分发消费该按键，但内部焦点和 serial 记账仍会完成，因此弹出层抓取绝不会
看到过期的 serial；被消费的按键也不会再作为提交送达文本输入。

`pt_pointer_event` 携带阶段（enter、leave、motion、down、up、axis、cancel）、
窗口逻辑坐标、down/up 时的 `BTN_*` 按钮码（其他情况为 0）、修饰键、逐按钮
的 `click_count`，以及针对该按钮的 `pressed`。指针回调在覆盖层和控件指针
分发之前运行。返回 `true` 会消费 down/up/axis；enter、leave 和 motion 是
建议性的，其返回值被忽略，因此悬停跟踪仍然有效。非左键不会干扰左键按压
状态或双击计数。

## 窗口与事件

```c
pt_window_set_title(window, "Renamed");
pt_window_set_size(window, 900, 640);
pt_window_set_min_size(window, 480, 320);
pt_window_set_resizable(window, true);
pt_window_set_decorated(window, true);
pt_window_set_decoration_mode(window, pt_decoration_client);
pt_window_request_redraw(window);
```

装饰是两个独立的决定。`pt_window_set_decorated` 控制 Pt 是否绘制自己的
44 px 标题栏；`pt_window_set_decoration_mode` 控制谁装饰表面。默认是显式的
客户端装饰（`pt_decoration_client`），因为标题栏、其边缘调整大小区域
和命中测试都是为此构建的。`pt_window_set_decorated(window, false)` 会移除
标题栏及其边缘区域，并且绝不向合成器请求边框。服务端装饰是显式选择：

```c
pt_window_set_decoration_mode(window, pt_decoration_server);
```

`pt_decoration_auto` 遵循框架默认策略，在 1.2 中即客户端装饰。请求只是
偏好：Pt 分别跟踪请求模式和生效模式，`pt_window_get_decoration_mode` 报告
请求，`pt_window_get_effective_decoration_mode` 报告 `configure` 最终决定的
模式，窗口按生效模式布局。Sway 会对平铺窗口强制服务端装饰，而 Mutter 和
Weston 不提供装饰管理器，因此客户端请求仍可能得到合成器绘制的边框，或得到
Pt 的标题栏。全屏会在任何模式下隐藏标题栏。窗口不可调整大小，或合成器未
声明最大化能力时，`pt_window_maximize` 返回 false，标题栏的最大化按钮也会
同时禁用。各合成器的行为记录在
[compositor-matrix.md](../compositor-matrix.md) 中。

关闭回调返回 `false` 以否决关闭：

```c
static bool confirm_close(pt_window *window, void *data) {
    (void)window;
    bool *dirty = data;
    return !*dirty;
}

pt_window_set_close_callback(window, confirm_close, &dirty);
```

当应用循环正在运行时，关闭请求会延迟到当前回调完成。如果应用未运行，
`pt_window_close` 可能会立即销毁窗口；请查询 `pt_window_is_open`，而不是
在关闭后保留原始窗口指针。

`pt_app_set_quit_on_last_window_close(app, false)` 在最后一个窗口关闭后保持
循环存活。在该模式下显式调用 `pt_app_quit`。

## 菜单、对话框与浮层通知

当后端支持这些表面时，菜单和工具提示使用原生 `xdg_popup` 表面，因此它们可以
延伸到窗口边缘之外，并堆叠在其他应用之上。对话框和浮层通知留在窗口内，并复用
普通控件树上方的同一覆盖层。菜单锚定到一个点或一个控件：

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

菜单回调在菜单关闭后运行，因此它们可以打开对话框或另一个菜单。菜单由
调用者拥有：只要菜单可能被显示，就保留指针，并在窗口或应用被销毁前调用
`pt_menu_destroy`。

对话框会使其后面的窗口变暗，捕获 Tab 焦点，并在 Escape 时关闭。操作会在
其回调运行前关闭对话框：

```c
pt_dialog *dialog = pt_dialog_create(window, "Discard changes?",
                                     "This cannot be undone.");
pt_dialog_add_action(dialog, "Cancel", false, NULL, NULL);
pt_dialog_add_action(dialog, "Discard", true, on_discard, state);
pt_dialog_set_on_close(dialog, on_dialog_closed, state);
pt_dialog_show(dialog);
```

浮层通知堆叠在右下角并自动过期；持续时间为 0 会保持一个通知可见，直到被
关闭。窗口拥有返回的句柄，因此不要自行销毁它：

```c
pt_window_show_toast(window, "Workspace renamed", pt_toast_success, 2500);
pt_window_show_toast(window, "Offline mode", pt_toast_warning, 0);
```

任何控件都可以带有工具提示。默认延迟是 600 ms；设置为 0 会在指针停留在
控件上时立即显示：

```c
pt_widget_set_tooltip(actions_button, "Open the actions menu");
pt_widget_set_tooltip_delay(actions_button, 350);
```

按窗口选择弹出层后端。`auto` 优先使用原生表面，并回退到窗口内渲染，无头
窗口也使用这种方式：

```c
pt_window_set_popup_policy(window, pt_popup_policy_auto);
pt_window_set_popup_policy(window, pt_popup_policy_window); /* force in-window */
```

对话框和浮层通知仍被裁剪到窗口边界内，因为它们是模态的，或相对于窗口内容
堆叠。菜单和工具提示则不受此限制。

## 拖放、手势与惯性

在任何控件上注册放置目标。进入回调选择是否接受所提供的 MIME 类型；移动和
离开跟踪悬停，放置接收 UTF-8 文本：

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

放置目标会由后代继承，位置相对于目标的左上角。一旦选择被拖动几个像素，
输入框就会自动成为拖放源；用 `pt_entry_set_drag_enabled(entry, false)`
禁用，或在指针按下处于活动状态时用 `pt_window_begin_drag` 启动自定义拖动。
`pt_window_begin_text_drag` 和 `pt_window_begin_uri_drag` 在 0.9.0 中已
弃用，并在 1.0 冻结时移除：改为向 `pt_window_begin_drag` 传递一个
`text/plain;charset=utf-8` 或 `text/uri-list` 条目，并自行用 CRLF 连接
URI 列表。

手势通过一个窗口回调到达。滑动的位移是累积的，捏合缩放相对于手势开始，
旋转以弧度为单位，`target` 是手势开始时质心下方的控件：

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

长按由指针按住合成，按住位置需保持在 8 px 容差内；合成器的三指按住会无视
延迟到达。对于连续触摸板来源，动能滚动默认开启，并可以全局控制：

```c
pt_window_set_momentum_scrolling(window, true);
bool enabled = pt_window_get_momentum_scrolling(window);
```

文件管理器和类似应用可以提供 `text/uri-list` 而不是纯文本。每个 URI 单独
占一行；拖动图像是可选的，并会替换内置缩略图。两个调用都需要活动按下的
隐式抓取，因此请从指针或触摸处理程序中调用它们：

```c
static const char *uris =
    "file:///home/user/Documents/report.pdf\n"
    "file:///home/user/Documents/budget.ods\n";
pt_drag_data uri_offer = {.mime_type = "text/uri-list", .data = uris, .size = 0};
pt_window_set_drag_image(window, thumbnail_pixels, 96, 72);
pt_window_begin_drag(window, &uri_offer, 1);
```

接收方检查 `text/uri-list`，解析载荷并释放每个 URI。注释和空行会被忽略；
接受 CRLF 行结尾：

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

结构化载荷改用 offer 表面。注册 `enter_offer` 会优先于 `enter`，
`drop_offer` 优先于 `drop`，因此目标可以看到源所声明的每种 MIME 类型，
选择一种，然后在事件循环上分块读取，而不是在放置回调内部读取。数据块不
以 NUL 结尾；对于文本格式请将它们连接起来。流会在其最终的 `done`、
`cancelled` 或 `error` 回调返回后释放，读取可以并发运行：

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

`pt_drop_offer_actions` 报告源声明的操作掩码，`pt_drop_offer_selected_mime`
返回进入期间接受的类型。在发送侧，`pt_window_begin_drag` 可以在一次拖动中
提供多种类型，并且仍然需要活动按下的隐式抓取：

```c
static const char *json = "{\"rows\":3}";
pt_drag_data types[] = {
    {.mime_type = "application/json", .data = json, .size = 0},
    {.mime_type = "text/plain", .data = "three rows", .size = 0},
};
pt_window_set_drag_finished_callback(window, on_drag_finished, state);
pt_window_begin_drag(window, types, 2);
```

`size` 为零表示载荷是以 NUL 结尾的文本。完成回调会在每次成功拖动时触发
一次：当目标拒绝或用户取消时，`dropped` 为 false；否则为执行的操作，
`pt_window_get_drag_result` 也会在事后报告该操作。

捏合和旋转以 `pt_gesture_kind_pinch` 到达，接触点数量在 `fingers` 中。缩放
是相对于手势开始测量的，旋转以弧度为单位，因此将两者相对于保存的变换
应用，在多次更新之间是稳定的：

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

轮询 `pt_window_get_drag_active` 以显示拖动反馈。`pt_window_cancel_drag`
中止源拖动、隐藏缩略图并清除任何挂起的输入框拖动；在没有活动拖动时也是
安全的。非零超时会取消目标永远无法完成的拖动，以防范放置目标无响应的
情况：

```c
pt_window_set_drag_timeout(window, 15000); /* 0 leaves cancel to the compositor */
if (pt_window_get_drag_active(window)) {
    /* the drag is still awaiting a drop */
}
pt_window_cancel_drag(window);
```

来自 tablet-v2 的触控笔输入通过其自己的回调传递，并镜像指针流，因此控件
会继续接收普通指针事件。未接触但接近的笔会报告 `enter` 和 `motion`；接触
会增加 `down` 和 `up`。压力和距离归一化到 `[0, 1]`，倾斜以每轴度数表示，
旋转以弧度为单位。`tool` 区分笔、橡皮擦、画笔、铅笔和喷枪，而 `buttons`
携带笔杆按钮和辅助按钮：

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

## 无障碍

每个内置控件都会根据其种类和内容推导出无障碍角色、名称、描述和状态。输入框
和文本区域暴露其文本、光标和选择偏移；滑块、进度条和分段控件暴露其值。密码
输入框暴露光标但不暴露文本。列表使用 `list` 角色，行使用 `list_item` 角色，
并报告选中的行；文本区域使用 `text_frame`。组合框报告 `combo_box` 和
`expandable` 状态，并在其弹出层打开时获得 `expanded`，弹出层面板本身报告
`list_box`。接受多行集合的视图会添加 `multi_selectable`，其每一行添加
`selectable`，可展开的分支会添加 `expandable`，并在打开时添加 `expanded`。
应用可以覆盖推导出的字段：

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

应用级回调会在 UI 线程上同步接收焦点、状态、文本、选择、值、名称和结构变化。
它是平台无障碍适配器的桥接点：

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

文本和值访问函数使用输出参数，并在控件没有此类数据时返回 `false`：

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

进程内模型始终可用。要让外部屏幕阅读器看到窗口，请启用可选的 AT-SPI2
桥接层，它通过 libdbus 连接到会话总线：

```c
if (!pt_app_enable_a11y(app)) {
    /* dbus-1 was not available at build time or the bus is unreachable */
}
pt_a11y_status status = pt_app_get_a11y_status(app);
```

`status` 在启用前为 `pt_a11y_status_disabled`，在桥接层无法运行时为
`pt_a11y_status_unavailable`，在开始服务后为 `pt_a11y_status_active`。桥接层
会镜像控件树，并提供 `Accessible`、`Application`、`Component`、`Action`、
`Text`、`EditableText` 和 `Value`；更改事件会转发给 AT-SPI 监听器。表格和
树元数据会作为无障碍属性发布：表格暴露其排序列，表头暴露 `sort` 和
`sort-priority`，表格单元格和行暴露 `row-index`/`column-index`，树行暴露
`level`、`expanded` 和 `selected`。桥接层是选择性启用的，可以用
`pt_app_disable_a11y(app)` 停止。有关推导表、完整 AT-SPI 接口矩阵、适配器
约定和 1.0 限制，请参阅 [accessibility.md](accessibility.md)。

## 定时器与嵌入循环

```c
static void tick(pt_app *app, void *data) {
    (void)app;
    int *ticks = data;
    ++*ticks;
}

uint64_t id = pt_app_add_timeout(app, 250, tick, &ticks);
pt_app_cancel_timeout(app, id); /* no-op if it already fired */
```

定时器是一次性的，并且在主线程上运行。`pt_app_run_for(app, milliseconds)`
最多在给定时长内泵送事件，这对测试以及将 Pt 集成到另一个事件循环很有用。
`pt_app_is_running` 报告循环是否活动。

### 监视文件描述符

`pt_app_watch_fd` 将应用描述符并入与 Wayland 显示相同的 poll 循环，因此
管道、eventfd 或 timerfd 会像定时器一样唤醒 UI 线程：

```c
static void on_ready(pt_app *app, uint32_t events, void *data) {
    (void)app;
    if (events & pt_fd_event_read) {
        drain_pipe(data);
    }
}

uint64_t watch = pt_app_watch_fd(app, fd, pt_fd_event_read, on_ready, state);
pt_app_unwatch_fd(app, watch); /* also safe from inside the callback */
```

掩码组合 `pt_fd_event_read`、`pt_fd_event_write`、`pt_fd_event_error` 和
`pt_fd_event_hangup`。`POLLHUP`、`POLLERR` 和 `POLLNVAL` 以错误事件到达，
并且回调至少会触发一次。回调在 UI 线程上运行且不得阻塞；回调可以取消自身
的监视，Pt 会将释放推迟到分发返回之后。描述符是借用的——Pt 从不关闭它——
并且每个应用对同一描述符只监视一次；重复监视会被拒绝并返回 0。需要由应用
接管描述符时使用 `pt_app_watch_fd_owned`：它在取消监视或应用销毁时关闭
描述符，并遵循相同的重复拒绝规则。监视像定时器一样计入
`pt_app_run_for` 的超时，因此即使没有其他事件，运行也会为它们唤醒。

## 尺寸、缩放与绘制

`pt_window_get_width` 和 `pt_window_get_height` 返回逻辑像素。
`pt_window_get_scale` 返回通过 `fractional-scale-v1` 或合成器首选缓冲区缩放
协商的当前渲染缩放。来自 `pt_widget_get_bounds` 的控件边界采用窗口逻辑
坐标。

Pt 按需重绘：状态更改会将受影响的控件或窗口标记为脏，动画仅在运行时保持
帧回调活动，空闲窗口不会重绘。高效的部分重绘依赖于在布局和状态更改过程中
跟踪逻辑损坏区域矩形。渲染器保留持久后备画布，重新裁剪到损坏区域，并提交
匹配的 `wl_surface.damage_buffer` 矩形，因此旋转指示器或悬停按钮不会重绘
整个窗口。没有损坏区域的窗口既不会绘制也不会提交。没有公开的“立即绘制”
调用；`pt_window_request_redraw` 在需要时强制生成全窗口帧，
`pt_window_paint_pixels` 则在不触碰实时帧的情况下渲染离屏快照。

在暴露 `wl_touch` 的合成器上支持触摸输入：第一个活动触摸会遵循指针的命中
测试、进入/离开、按下、拖动和释放路径，被取消的触摸会释放被按下的控件。
触摸板滑动/捏合手势来自 `pointer-gestures-v1`，tablet-v2 触控笔事件携带
工具、压力、倾斜、旋转和按钮；请参阅上面的手势和触控笔回调。

损坏区域矩形不属于公开 API。控件和窗口会自动管理损坏区域，测试通过仓库
测试套件使用的内部头文件来检验输入和绘制。手势、拖动和触控笔事件是公开的。

### 自定义绘制与离屏快照

任何控件都可以通过 `pt_widget_set_paint_callback` 绘制自己的内容。回调在
控件正常的背景和内容绘制之后运行，接收以控件局部逻辑像素表示的借用
`pt_paint_context`，并使用 `pt_paint_*` 辅助函数绘制：

```c
static void paint_gauge(pt_widget *widget, pt_paint_context *context, void *data) {
    (void)widget;
    float level = *(float *)data;
    float width = pt_paint_width(context);
    float height = pt_paint_height(context);
    pt_paint_fill_rounded_rect(context, 0, 0, width, height, 4, pt_rgb(0x3568E0));
    pt_paint_fill_rect(context, 0, 0, width * level, height, pt_rgb(0x8AB4FF));
    pt_paint_draw_text(context, "sans", 12, pt_font_weight_regular,
                       pt_font_slant_upright, 6, height * 0.7f, "42 %", 4,
                       pt_rgb(0xFFFFFF));
}

pt_widget_set_paint_callback(gauge, paint_gauge, &level);
```

上下文仅在本次调用内有效。绘制回调是纯函数且幂等：同一控件每帧可能被绘制
0..N 次（每个损坏瓦片一次），并且无论瓦片顺序或数量如何都必须产生相同像素。
它不得更改树或触发布局，必须平衡 `pt_paint_push_clip` 和
`pt_paint_pop_clip`，并且不得重入 `pt_window_paint_pixels`。这些守卫始终
开启，而不是藏在调试开关后面：重入快照会失败、设置线程局部错误并保持实时
帧不变；不平衡的裁剪会在帧继续之前被纠正，并通过 `pt_get_last_error` 与
警告记录下来。使用 `pt_widget_request_redraw` 或
`pt_widget_request_redraw_rect` 排队下一帧；
矩形形式只损坏控件局部子矩形，适合光标或插入符。`pt_paint_scale` 是设备
像素比，`pt_paint_clip_rect` 报告当前损坏瓦片，使回调可以跳过其外的工作。
自定义绘制不会自动产生无障碍语义：请通过常规控件 API 声明角色、名称和
状态，否则屏幕阅读器看到的只是一个空框。

`pt_window_paint_pixels` 将完整一帧以 1.0 缩放渲染到调用方拥有的 `uint32_t`
缓冲区（预乘 `0xAARRGGBB`，行优先，每行 `width` 像素）。它是纯快照：不会
损坏窗口、清除 `needs_draw`/`redraw_scheduled`、触碰上次绘制时间或收集延迟
覆盖层，因此在已映射窗口上调用它不会吞掉下一帧实时画面。

`pt_window_paint_pixels_ex` 接受相同的窗口和缓冲区，另加一个
`pt_paint_pixels_desc`：

```c
pt_paint_pixels_desc desc = {
    .stride = buffer_width,   /* pixels per row; 0 means width */
    .scale = 1.5f,            /* device-pixel ratio; 0 means 1.0 */
    .format = pt_pixel_format_argb8888_premultiplied,
};
pt_window_paint_pixels_ex(window, pixels, device_width, device_height, &desc);
```

`width` 和 `height` 是设备像素，因此 1.5 的缩放需要逻辑尺寸的 1.5 倍；
大于 `width` 的 stride 会保持填充像素不变。目前只定义预乘 `argb8888`，
`reserved` 必须为零。stride、scale、format 或缓冲区无效时返回 false。
两个快照入口都会拒绝从绘制回调内部发起的调用。自定义绘制和分数缩放黄金
图像就是这样在没有合成器的情况下测试的。

## 错误

构造函数在失败时返回 `NULL` 并设置线程局部错误字符串：

```c
pt_window *window = pt_window_create(app, "Title", 800, 600);
if (!window) {
    fprintf(stderr, "window: %s\n", pt_get_last_error());
}
```

错误字符串由 Pt 拥有，并在同一线程上的下一次 Pt 调用之前有效。内部状态
分配失败会以诊断信息中止，而不是留下部分有效的树。Wayland 断开连接会通过
`pt_app_run`/`pt_app_run_for` 的返回值和 `pt_get_last_error` 报告。

## 生命周期与线程规则

- `pt_app` 拥有其窗口。`pt_app_destroy` 销毁所有窗口及其控件树。在该调用
  之后不要使用任何句柄。
- 窗口拥有其根控件及其下方的整棵树。使用控件构造函数、`pt_widget_destroy`
  或 `pt_widget_clear_children` 更改结构。
- 传递给设置函数的字符串会被复制。获取函数返回的字符串由控件拥有，并在
  下一次文本更改或销毁之前保持有效。
- 除文档化的只读查询外，所有公开调用都必须在驱动应用循环的线程上运行。
  没有内部锁定。使用管道或应用自有的队列将其他线程的工作转交过来。
- 当使用某个 `user_data` 指针的回调仍注册时，不要释放该指针。将回调清除
  为 `NULL` 是安全的。

## 无头测试

设置 `PT_HEADLESS=1` 以创建没有 Wayland 连接的应用：

```sh
PT_HEADLESS=1 ./my_ui_test
```

无头应用支持窗口、控件、定时器、回调和 `pt_app_run_for`，因此可以在 CI 上
测试状态和事件逻辑。公共 API 不暴露合成输入；但绘制仍可通过
`pt_window_paint_pixels` 和控件绘制回调在无头环境中驱动，仓库测试正是这样
在没有合成器的情况下渲染帧的。随附的测试展示了该模式：

```sh
make check
make check-bindings
```

如需更严格的本地验证：

```sh
make clean
make -j"$(nproc)" werror=1
make check werror=1
make check-bindings werror=1
```

Sanitizer 矩阵（包括 Python 和 Java 所需的 ASan 预加载）记录在
[release.md](../release.md#validation-matrix) 中。

## 下一步

- [绑定](bindings.md)：C++、Python 和 Java。
- [架构](architecture.md)：布局、绘制和 Wayland 内部实现。
- [性能](../performance.md)：可复现的基准测试和预算。
- [比较](../comparison.md)：Pt 与其他工具包的区别。
