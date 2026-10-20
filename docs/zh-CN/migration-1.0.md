# 迁移到 Pt 1.0

[English](../migration-1.0.md) | 简体中文

本指南面向针对 0.8、0.9 或 0.10 编写的应用程序。1.0 中恰好有**一处**
源代码和二进制层面的破坏性变更：0.9 弃用的两个拖放辅助函数在冻结时被移除。
本文档中的其他所有内容都是新增、默认值变更或发行说明中已经记录的行为。

如果你要开发新应用程序，请阅读 [quickstart.md](quickstart.md)；本文档只
描述增量。

## 版本和对象名称

1.0 是新的发行系列：

- `PT_VERSION_MAJOR/MINOR/PATCH` 和 `pt_version_string` 报告 `1.0.0`。
- SONAME 从 `libpt.so.0` 变为 `libpt.so.1`，因为冻结的 ABI 是 0.10 的
  有意子集，而不是超集。针对 `libpt.so.0` 链接的二进制文件无法加载到新库。
- 重新构建并重新链接每个使用方。对于已安装的前缀：

  ```sh
  pkg-config --modversion pt          # 1.0.0
  pkg-config --cflags --libs pt
  ```

- 静态归档、pkg-config 文件、头文件、C++ 头文件以及 Python/Java 绑定源文件
  保持相同的前缀布局。没有 include 路径移动。

## 1.0 中的移除

`pt_window_begin_text_drag` 和 `pt_window_begin_uri_drag` 已从 C 头文件、
库以及 C++、Python 和 Java 包装器中移除。`pt_window_begin_drag` 是唯一的
拖动源。它自 0.8 起可用，并通过调用方提供的 MIME 类型覆盖这两种情况。

### C

之前（0.8–0.10）：

```c
/* While a pointer press is active. */
pt_window_begin_text_drag(window, "line one\nline two");
pt_window_begin_uri_drag(window, uris, uri_count);
```

之后（1.0）：

```c
pt_drag_data text_offer = {
    .mime_type = "text/plain;charset=utf-8",
    .data = "line one\nline two",
    .size = 0, /* 0 means NUL-terminated */
};
pt_window_begin_drag(window, &text_offer, 1);
```

URI 列表现为由调用方组合。旧辅助函数用 CRLF 连接 URI，并跳过空条目和包含
`\r` 或 `\n` 的条目；如果你保留该策略，替代实现会做同样的事：

```c
static bool begin_uri_drag(pt_window *window, const char *const *uris, uint32_t count) {
    size_t total = 1;
    for (uint32_t i = 0; i < count; ++i) {
        const char *uri = uris[i];
        if (!uri || !*uri || strpbrk(uri, "\r\n")) {
            continue;
        }
        total += strlen(uri) + 2;
    }
    char *list = malloc(total);
    if (!list) {
        return false;
    }
    char *write = list;
    for (uint32_t i = 0; i < count; ++i) {
        const char *uri = uris[i];
        if (!uri || !*uri || strpbrk(uri, "\r\n")) {
            continue;
        }
        size_t length = strlen(uri);
        memcpy(write, uri, length);
        write += length;
        *write++ = '\r';
        *write++ = '\n';
    }
    *write = '\0';
    pt_drag_data offer = {.mime_type = "text/uri-list", .data = list, .size = 0};
    bool started = pt_window_begin_drag(window, &offer, 1);
    free(list); /* Pt copies the payload before returning. */
    return started;
}
```

### C++

之前：

```cpp
window.begin_text_drag("line one\nline two");
window.begin_uri_drag({"file:///home/user/notes.txt"});
```

之后：

```cpp
window.begin_drag({pt::drag_data{"text/plain;charset=utf-8", "line one\nline two", 0}});
window.begin_drag({pt::drag_data{"text/uri-list", "file:///home/user/notes.txt\r\n", 0}});
```

`pt::window::begin_drag` 接受 `std::span<const pt_drag_data>` 或初始化列表；
多个条目表示一次拖放提供多种 MIME 类型。

### Python

之前：

```python
window.begin_text_drag("line one\nline two")
window.begin_uri_drag(["file:///home/user/notes.txt"])
```

之后：

```python
window.begin_drag([("text/plain;charset=utf-8", "line one\nline two")])
window.begin_drag([("text/uri-list", "file:///home/user/notes.txt\r\n")])
```

每个条目可以是 `(mime_type, data)` 对、`(mime_type, data, size)` 三元组、
映射或 `pt.DragData`。旧方法不再引发 `DeprecationWarning`；它们已不存在。

### Java

之前：

```java
Pt.window_begin_text_drag(window, "line one\nline two");
Pt.window_begin_uri_drag(window, new String[] {"file:///home/user/notes.txt"});
```

之后：

```java
Pt.window_begin_drag(window, new Pt.DragData[] {
    new Pt.DragData("text/plain;charset=utf-8", "line one\nline two", 0)
});
Pt.window_begin_drag(window, new Pt.DragData[] {
    new Pt.DragData("text/uri-list", "file:///home/user/notes.txt\r\n", 0)
});
```

### 为什么这是移除而不是保留

在 1.0 之后移除将需要一次大版本发布，因此将这两个名称保留到 `1.0.0` 会使
它们在整个 1.x 系列中继续存在。冻结机制改为按 0.9.0 时记录的时间窗口执行
移除。`make api-update` 将该变更记录为包含两个条目的基线差异，库现在导出
412 个符号，全部由三个绑定封装。

## 0.9：硬编码颜色变为令牌

0.9 没有移除颜色 API。它将内置绘制从复制的字面量迁移到语义表，新增了 14 个
角色和 10 个度量，并添加了公共解析辅助函数。自定义控件的实际迁移是：

之前（0.8），将调色板的字面量副本固定到内置控件上：

```c
pt_widget_set_background(card, 0x11151EFFu);
pt_widget_set_foreground(title, 0xE9EEF7FFu);
```

之后，最好完全不要固定，让控件跟随当前主题：

```c
/* No call: the card and label use pt_theme_surface and pt_theme_text. */
```

当应用程序确实需要在运行时获取值——例如构建自己的类似画布的自定义表面，
或为以后存储颜色——应从应用或窗口读取，而不是复制表：

```c
pt_color surface = pt_app_get_theme_color(app, pt_theme_surface);
pt_color outline = pt_window_get_theme_color(window, pt_theme_outline);
float radius = pt_window_get_theme_metric(window, pt_theme_metric_radius_medium);
```

0.8 令牌集保持其数值。0.9 新增的角色包括容器、轮廓、禁用文本、按下强调色、
柔和状态、遮罩、骨架和选择文本，此外还有度量表。主题指南列出了所有这些：
[theming.md](theming.md)。

两点行为说明：

- 1.0 默认调色板**不是** 0.9 调色板。强调色发生了变化（深色 `5B8CFF` →
  `3568E0`，浅色 `2F6BFF` → `2559E6`），浅色状态颜色也发生了变化
  （`DC3D48` → `B92C38`，`16A34A` → `0F7A37`，`B7790A` → `8A5A00`），
  因为 0.9 的值与其表面相比测得 3.2:1 到 4.4:1。如果你固定了字面量以匹配
  旧主题，请重新检查；如果你调用 `pt_app_set_accent_color`，推导出的角色会
  跟随你的颜色，该修复不适用于你。
- `pt_app_set_accent_color` 从强调色和活动方案推导 `accent_hover`、
  `accent_pressed`、`accent_text`、`accent_soft`、`focus` 和 `selection`。
  如果你还为其中一个角色调用 `pt_app_set_theme_color`，顺序很重要：最后
  一次调用生效，而后续的 `pt_app_set_color_scheme` 会连同其他每个应用覆盖
  一起重置它们。

窗口覆盖是 0.9 的新增功能，在 1.0 中未变：

```c
pt_window_set_theme_color(window, pt_theme_accent, pt_rgb(0x9ECE6A));
pt_window_set_theme_metric(window, pt_theme_metric_radius_medium, 12.0f);
pt_window_clear_theme_overrides(window);
```

## 0.10：选择词汇

0.10 新增了 13 个函数，为列表、表格和树提供统一的多行选择模型。没有移除任何
内容，因此 0.9 应用程序可以继续编译；迁移的重点是使用选择集合，而不是伪造
它。

之前（0.9），只有表格范围和单个主行：

```c
pt_table_set_selected(table, row);
/* No supported way to ask "which rows are highlighted?" */
```

之后（1.0），在列表、表格或树上进行相同调用：

```c
pt_list_view_select_range(list, 2, 5);       /* inclusive, clamped */
pt_list_view_select_toggle(list, 7);         /* Ctrl-click semantics */
uint32_t rows[16];
uint32_t count = pt_list_view_get_selection(list, rows, 16);
bool is_selected = pt_list_view_is_row_selected(list, 7);
pt_list_view_clear_selection(list);

/* The table and tree mirrors are pt_table_select_toggle,
 * pt_tree_select_range, pt_tree_select_toggle, pt_tree_is_selected,
 * pt_tree_get_selection and pt_tree_clear_selection. */
```

一段话概括契约：范围包含端点并经过钳制；setter 替换集合，并将锚点和主行
留在第一行；切换会移动主行；将主行切换为未选中时，主行会移动到下一个选中
行；`clear` 将视图留在 `pt_list_index_none`。集合与主行是分开的，因此
`pt_list_view_get_selected` 仍用于指明插入符号所在行。公共集合辅助函数是
静默的，因此应用程序可以重建选择而不会触发事件回显；键盘、指针和橡皮筋框选
手势通过 `on_select` 报告新的主行。`tests/test_selection.c` 用一个手势脚本
驱动所有三个视图。

无障碍模型在同一版本中新增了 `selectable` 和 `multi_selectable`，因此屏幕
阅读器可以在未选中的行上提供“添加到选择”。无障碍指南描述了映射：
[accessibility.md](accessibility.md)。

## 1.0 中无需迁移的新增内容

- 每个公共头文件现在都有一个 `Header contract` 块，包含线程规则、所有权、
  生命周期和错误行为。这些规则是现有函数已经遵循的规则；这些文字说明使它们
  可供审查。生成的参考文档将每个块渲染为其模块前言。
- `tests/test_abi.c` 固定每个公共枚举值、状态位、哨兵和结构体布局。应用
  程序代码不受影响；它的存在是为了让未来版本在重新编号某些内容之前先在
  自己的构建中失败。
- SONAME、版本宏和 pkg-config 在 `1.0.0` / `libpt.so.1` 上一致。
- 生成的 API 参考和兼容性策略描述了冻结的表面：
  [api-reference.md](../api-reference.md)、[api-policy.md](../api-policy.md)。

## 迁移构建

1. 阅读移除部分；替换这两个调用。
2. 使用你采用的 `werror=1` 设置针对 1.0 头文件重新构建，这样过时的原型会
   成为编译错误，而不是运行时意外。
3. 重新链接并重新部署 `libpt.so.1` 对象；移除任何指定 `libpt.so.0` 的
   版本固定。
4. 如果你要交付 UI 截图或对比度敏感表面，请在深色和浅色模式下各运行一次
   测试，以审查调色板变更。
5. 如果你启用了 AT-SPI 桥接层，请重新运行辅助技术冒烟测试；标签和文本区域
   上的文本接口是 1.0 的一部分。

## 如果你继续使用 0.x

0.10 仍是最后一个 0.x 发行版，且不再提供修复维护。没有兼容层：如果不
重新引入冻结移除的符号，就无法恢复这两个被移除的包装器。固定
`libpt.so.0` 可以让未修改的 0.x 应用程序继续在旧库上运行，但新工作应以
1.0 为目标。
