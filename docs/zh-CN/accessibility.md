# 无障碍

[English](../accessibility.md) | 简体中文

Pt 提供与平台无关的无障碍模型，并为 Wayland 桌面附带可选的 AT-SPI2
桥接层。模型即契约：控件根据自身类型和内容推导出角色、名称、描述、状态、
文本和值，应用程序可以覆盖推导出的值。桥接层是该模型的一个使用方；可以
在不修改控件的情况下，用相同的 getter 和事件构建另一个平台适配器。

模型位于 [`pt/a11y.h`](../../include/pt/a11y.h)。AT-SPI 桥接层位于
`src/a11y_bridge.c`，按应用启用。

## 启用桥接层

```c
pt_app *app = pt_app_create("dev.example.notes");
if (!pt_app_enable_a11y(app)) {
    /* No D-Bus build, no session bus or no accessibility bus. The app still
     * runs; report the status if the UI has a diagnostics page. */
}

pt_a11y_status status = pt_app_get_a11y_status(app);
/* pt_a11y_status_disabled, _unavailable or _active */
```

`pt_app_enable_a11y` 在以下情况返回 `false`：应用为 `NULL`、构建未包含
D-Bus，或无障碍总线不可达。应用可以安全地继续运行。桥接层需要显式启用，
因此核心在运行时并不硬性依赖守护进程；`pt_app_disable_a11y` 会关闭桥接层。
事件回调独立于桥接层，在安装之前保持休眠：

```c
pt_app_set_a11y_callback(app, on_a11y_event, state);
```

回调在 UI 线程上同步运行。它不得阻塞。当桥接层处于活动状态时，同一事件
也会转发给 AT-SPI 桥接层，因此自定义适配器和内置桥接层可以共存。

## 角色

具有显式角色的控件使用该角色。否则，角色从控件类型推导。下表展示 Pt 角色
以及 AT-SPI 桥接层发布的内容。“覆盖”是指角色通常由覆盖层或自定义控件设置，
而不是由类型分支设置。

| 控件类型 | 默认 `pt_a11y_role` | AT-SPI 角色 |
| --- | --- | --- |
| `pt_widget_label` | `label` | label |
| `pt_widget_button` | `push_button` | button |
| 菜单中的按钮 | `menu_item` | menu item |
| `pt_widget_entry` | `text_frame` | entry |
| `pt_widget_text_area` | `text_frame` | entry |
| `pt_widget_list` | `list` | list |
| `pt_widget_list_row` | `list_item` | list item |
| `pt_widget_toggle` | `switch` | toggle button |
| `pt_widget_slider` | `slider` | slider |
| `pt_widget_scroll` | `scroll_pane` | scroll pane |
| `pt_widget_divider` | `separator` | separator |
| `pt_widget_card` | `panel` | panel |
| `pt_widget_spacer` | `none` | unknown |
| `pt_widget_checkbox` | `check_box` | check box |
| `pt_widget_radio` | `radio_button` | radio button |
| `pt_widget_progress` | `progress_bar` | progress bar |
| `pt_widget_spinner` | `spinner` | progress bar |
| `pt_widget_segmented` | `group` | grouping |
| `pt_widget_icon` | `icon` | icon |
| `pt_widget_table` | `table` | table |
| `pt_widget_table_header` | `generic` | filler |
| `pt_widget_table_header_cell` | `column_header` | table column header |
| `pt_widget_table_row` | `table_row` | table row |
| `pt_widget_table_cell` | `table_cell` | table cell |
| `pt_widget_tree` | `tree` | tree |
| `pt_widget_tree_row` | `tree_item` | tree item |
| `pt_widget_tabs` | `tab_list` | page tab list |
| `pt_widget_tab_page` | `tab_panel` | panel |
| `pt_widget_sidebar` | `list` | list |
| `pt_widget_sidebar_item` | `list_item` | list item |
| `pt_widget_combobox` | `combo_box` | combo box |
| `pt_widget_box` 及默认值 | `generic` | filler |
| 菜单覆盖层 | `menu`（覆盖） | popup menu |
| 对话框覆盖层 | `dialog`（覆盖） | dialog |
| Toast 覆盖层 | `alert`（覆盖） | alert |
| 工具提示覆盖层 | `tooltip`（覆盖） | tool tip |

组合框弹出面板及其滚动包装器由组合框实现设置为 `list_box`，AT-SPI 将其
发布为列表框，而不是普通列表。菜单项和覆盖层面板带有显式角色，因为它们由
普通按钮和卡片构建。

`pt_widget_set_a11y_role(widget, role)` 为一个控件替换推导出的角色。传入
`pt_a11y_role_none` 可恢复推导。角色变更会发出结构事件。当自定义控件绘制
出的含义无法由类型捕获时，使用角色覆盖；不要用它让按钮假装成复选框。

## 状态

`pt_widget_get_a11y_state` 返回 `pt_a11y_state_*` 位的掩码。可见和已启用状态
是继承的：只有当控件及其每个祖先都可见时，控件才可见；只有当控件及其每个
祖先都已启用时，控件才已启用。

| Pt 状态 | 推导方式 |
| --- | --- |
| `visible` | 控件及所有祖先可见 |
| `enabled` | 控件及所有祖先已启用 |
| `focusable` | `pt_widget_set_focusable` 为 true |
| `focused` | 控件当前拥有键盘焦点 |
| `checked` | 切换控件、复选框或单选按钮处于勾选状态 |
| `selected` | 行位于选择集合中；分段控件已选中；活动标签页已选中 |
| `editable` | 输入框或文本区域已启用 |
| `busy` | 旋转指示器始终繁忙；进度条在不确定时繁忙 |
| `expanded` | 树节点已展开；组合框弹出层已打开 |
| `expandable` | 树节点有子节点；组合框始终如此 |
| `selectable` | 已启用的数据视图行、侧边栏项或标签页 |
| `multi_selectable` | 列表、表格或树容器 |
| `modal` | 控件的显式角色为 `dialog` |

AT-SPI 桥接层还会添加少量并非独立 Pt 位的状态：

- visible 还会设置 `SHOWING`；enabled 还会设置 `SENSITIVE`；focused 还会
  在控件及其窗口上设置 `ACTIVE`。
- 切换控件、复选框和单选按钮在勾选时除 `CHECKED` 外还报告 `CHECKABLE`
  和 `SELECTABLE`。分段控件报告 `SELECTABLE`。
- 不确定状态的进度条报告 `INDETERMINATE`；非空工具提示报告 `HAS_TOOLTIP`；
  处于按下状态的控件报告 `PRESSED`。
- 输入框报告 `SELECTABLE_TEXT`；标签、按钮或文本区域这类非输入框控件报告
  `READ_ONLY`。
- 可展开但尚未展开的树项报告 `COLLAPSED`，因此屏幕阅读器会播报关闭状态，
  而不是保持沉默。

注意，`selectable` 与 `selected` 是分开的：多选视图中的每个已启用行都是
可选择的，只有集合中的行才是已选中的。这样屏幕阅读器才能在未选中的行上
提供“添加到选择”。

## 名称、描述、文本和值

`pt_widget_get_a11y_name` 按以下顺序推导名称：

1. 来自 `pt_widget_set_a11y_name` 的应用程序覆盖；
2. 树行的树节点文本；
3. 控件自身的文本（非空时）；
4. 侧边栏的标题；
5. 输入框或文本区域的占位符；
6. 空字符串。

描述没有推导：它是来自 `pt_widget_set_a11y_description` 的覆盖，或空
字符串。设置 `NULL` 或空字符串会清除任一覆盖并恢复推导值。两个 setter
都会发出名称变更事件。

`pt_widget_get_a11y_text` 返回借用文本以及 UTF-8 **字节**偏移：

- 标签和按钮返回其文本，插入符号和选择偏移为零；
- 输入框和文本区域返回文本、插入符号、选择起点和选择终点；
- 密码输入框返回 `false`；AT-SPI 桥接层仍会发布密码角色和插入符号位置，
  但对文本查询返回空字符串。字符计数反映真实长度，因此除非应用程序认为
  长度敏感，适配器不应将其视为内容泄漏；
- 没有文本的控件返回 `false`。

`pt_widget_get_a11y_value` 为滑块和进度条（`[0, 1]`）、切换控件、复选框
和单选按钮（`0` 或 `1`）以及分段控件（选中索引）返回浮点数。其他类型返回
`false`。

## 实践中的覆盖

```c
pt_widget *save = pt_button_create(form, "Save");
pt_widget_set_a11y_name(save, "Save note");
pt_widget_set_a11y_description(save, "Writes the current note to disk");

pt_widget *details = pt_card_create(form);
pt_widget_set_a11y_role(details, pt_a11y_role_group);
pt_widget_set_a11y_name(details, "Note details");

pt_widget *search = pt_entry_create(toolbar, "Search");
pt_widget_set_a11y_name(search, "Search notes");
```

覆盖值会被复制。它们能在控件重新设置样式和主题变更后保留，并随控件释放。
重建的控件会丢失其覆盖，因此应在构建控件的代码中应用它们，而不是在启动时
一次性应用。

AT-SPI 桥接层从窗口标题推导窗口的无障碍名称，并从 `pt_app_create` 发布
应用程序名称。它还会为表头和单元格发布 `label-for`/`labelled-by` 关系，
为数据视图和树节点发布 `member-of`/`node-child-of`/`node-parent-of` 关系，
为窗口发布 `subwindow-of`，为弹出层表面发布 `popup-for`。

## 事件

`pt_a11y_event_type` 是与平台无关的事件流：

| 事件 | 触发时机 | AT-SPI 信号 |
| --- | --- | --- |
| `focus_changed` | 焦点进入或离开控件 | `Focus` 加上 `focused` 状态变更 |
| `state_changed` | enabled、checked、busy 或类似状态变化 | 针对受影响状态的 `StateChanged` |
| `text_changed` | 可编辑文本或插入符号变化 | 带新文本的 `TextChanged` |
| `selection_changed` | 数据视图的选择移动 | `SelectionChanged` 加上主行的 `selected` 状态变更 |
| `value_changed` | 滑块、进度或分段值变化 | 针对 `accessible-value` 的 `PropertyChange` |
| `name_changed` | 名称或描述覆盖变化 | 针对 `accessible-name` 的 `PropertyChange` |
| `structure_changed` | 角色覆盖、覆盖层打开/关闭或子项顺序变化 | 带 `reorder` 的 `ChildrenChanged` |

Toast 使用 AT-SPI 公告信号，礼貌级别由 Toast 公布（polite 或 assertive）。
焦点变更针对获得焦点的控件发出；窗口的焦点状态从应用程序对象和窗口自身的
`Component` 状态读取。事件是同步的，并携带控件的当前状态，因此适配器收到
事件后无需轮询。

## AT-SPI 桥接层发布的内容

每个对象都实现 `org.a11y.atspi.Accessible`。窗口和控件实现 `Component`；
应用程序根是唯一实现 `Application` 的对象。其余接口取决于控件：

| 接口 | 为谁发布 |
| --- | --- |
| `Accessible` | 每个应用、窗口和控件 |
| `Application` | 仅应用根 |
| `Component` | 每个窗口和控件 |
| `Action` | 已启用的可操作控件（`click`），以及可聚焦但不可操作的控件（`focus`） |
| `Text` | 输入框、文本区域、标签和按钮 |
| `EditableText` | 输入框和文本区域（禁用时写入方法返回 false） |
| `Value` | 滑块、进度条、切换控件、复选框、单选按钮和分段控件 |
| `Table` | 表格 |
| `TableCell` | 单元格（不包括表头单元格） |
| `Selection` | 列表、表格、树、侧边栏和标签栏 |

代表性方法：

- **Accessible：** `GetChildCount`、`GetChildAtIndex`、`GetChildren`、
  `GetIndexInParent`、`GetRole`、`GetRoleName`、`GetState`、
  `GetAttributes`、`GetRelationSet`、`GetInterfaces`、`GetApplication`，
  以及名称和描述查询。
- **Component：** `Contains`、`GetAccessibleAtPoint`、`GetExtents`、
  `GetPosition`、`GetSize`、`GetLayer`、`GrabFocus`、`ScrollTo`。修改
  范围、位置和大小的操作返回 `false`。
- **Action：** `GetActions` 和 `DoAction`；索引 0 在可操作控件上是
  `click`，否则是 `focus`。
- **Text：** `GetText`、`GetCharacterCount`、`GetStringAtOffset`、
  `GetTextBeforeOffset`/`AtOffset`/`AfterOffset`、`GetCharacterAtOffset`、
  `GetAttributeValue`、`GetCharacterExtents`、`GetOffsetAtPoint`、
  `GetNSelections`、`GetSelection`、`AddSelection`、`SetSelection`、
  `RemoveSelection`、`SetCaretOffset`。偏移在 D-Bus 上是 Unicode 字符
  偏移，内部转换为 UTF-8 字节。
- **EditableText：** `SetTextContents`、`InsertText`、`DeleteText`、
  `CopyText`、`CutText`、`PasteText`。对禁用的文本输入返回 `false`。
- **Value：** `GetMinimumValue`、`GetMaximumValue`、`GetMinimumIncrement`、
  `GetCurrentValue`、`SetCurrentValue`。
- **Table：** 行/列计数、`GetAccessibleAt`、`GetIndexAt`、
  `GetRowAtIndex`/`GetColumnAtIndex`、行/列描述、范围、表头、选择查询
  以及行/列选择。
- **TableCell：** 行/列索引、范围、跨度和表头单元格。
- **Selection：** `GetSelectedChild`、`SelectChild`、`DeselectChild`、
  `DeselectSelectedChild`、`IsChildSelected`、`SelectAll`、
  `ClearSelection`。

桥接层会跳过虚拟化池中未绑定的行控件，因此 10,000 行的表格只暴露当前作为
控件存在的行。覆盖层面板在无障碍树中追加在窗口根之后，与其绘制顺序一致。
树暴露可见节点；折叠的分支在展开之前不会被枚举。

## 构建不同的适配器

非 AT-SPI 适配器需要实现相同的概念性接口。最低契约是：

1. **树。** 从每个窗口开始，然后是根控件，再然后是真实子项。在根之后包含
   打开的覆盖层面板，跳过未绑定的行占位符，并将树行解析到其节点。控件指针
   在控件由活动窗口持有期间有效；结构事件会告诉你重新读取子项列表。
2. **语义。** 使用公共 getter 获取角色、名称、描述、状态、文本和值，而
   不是读取控件内部。应用相同的可见/启用祖先规则，因为 getter 已经这样做。
3. **操作。** 公共 API 暴露焦点（`pt_widget_focus`），但没有通用的激活
   入口点。为实现同等的激活能力，将平台命令路由到应用程序自己的回调中，
   或启用 AT-SPI 桥接层，其 `DoAction` 使用内部激活路径。
4. **文本编辑。** 使用公共文本 setter 和 `pt_entry_set_selection` /
   `pt_text_area_set_selection` 处理插入符号和选择。在平台期望字符偏移的地方，
   不要用字节偏移调用模型；在边界处转换。
5. **事件。** 安装 `pt_app_set_a11y_callback` 并转发七种事件类型。不要在
   回调中阻塞。在 `state_changed` 时重新读取状态，在 `structure_changed`
   时重新读取子项列表。
6. **线程。** 每个 getter 和 setter 都仅限 UI 线程，包括 getter。在其他
   线程收到请求的适配器必须自行将工作投递到 UI 线程；Pt 1.0 没有
   `pt_app_invoke`。
7. **生命周期。** 句柄是借用引用。不要释放控件，也不要在结构变更后保留控件
   指针而不重新验证。桥接层为此保留自己的 ID 和对象路径。

## 测试

- `tests/test_a11y.c` 以无头方式覆盖角色和状态推导、覆盖、文本和值暴露、
  密码行为以及事件传递。
- `tests/test_a11y_bridge.c` 在会话总线可用时启动真实 D-Bus 桥接层，并
  断言接口、角色、状态、名称、文本、值、操作、关系和选择操作。文本断言覆盖
  只读标签、具有 UTF-8 偏移的可选择和可编辑多行文本区域，以及文本和字符
  代码保持隐藏但字符计数仍可用的密码输入框。没有总线时它会正常跳过。
- `examples/c/accessibility.c` 是一个可运行的表单，包含名称、描述、角色、
  实时区域和状态读数；`make run-a11y` 会启动它。
- 要测试自定义适配器，构建无头应用（`PT_HEADLESS=1`），安装事件回调，
  通过公共 setter 修改 UI，并断言事件和 getter。真实 AT-SPI 会话还需要
  `dbus-daemon` 和无障碍总线；`tools/release-check.sh` 将桥接层报告为
  实时检查，而不是 `make check` 的一部分。

## 1.0 中的限制

- 桥接层需要显式启用，且要求构建中包含 `dbus-1`。没有可访问的无障碍总线
  时，`pt_app_enable_a11y` 返回 `false`，应用将在没有辅助技术暴露的情况下
  运行。
- 模型描述的是控件，而不是任意绘制内容。自行绘制内部控件的自定义控件必须自己
  暴露角色、名称和状态；桥接层无法从像素推断含义。
- 没有公共激活函数。`pt_widget_focus` 是公共的；操作分发是 AT-SPI 桥接层
  的功能。
- 密码输入框隐藏其文本，但报告插入符号和字符计数。将密码长度视为敏感的
  应用程序应在其威胁模型中考虑这一点。
- 双向文本段落布局是 1.0 之后的工作。文本偏移在存储的 UTF-8 字符串中是字节
  偏移，在 D-Bus 上是字符偏移；未来的双向文本实现必须保持两者为逻辑偏移，
  并为插入符号和命中测试提供单独的视觉映射。[RTL 计划](rtl.md) 明确了该
  要求，包括 AT-SPI 方向属性和密码检查。
- 桥接层为正文单元格发布 `TableCell`，而不是为表头单元格。表头单元格通过
  `Table` 表头查询和 `column_header` 角色出现，而不是作为 `TableCell`
  实现。
- 除了发出的事件之外，没有无障碍树差异协议。错过事件的适配器必须重新枚举
  受影响的子树。
