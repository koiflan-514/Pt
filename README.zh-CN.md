# Pt

[English](README.md) | 简体中文

**Pt** 是一个小型、快速、原生 Wayland 的 GUI 框架，使用现代 C23 编写。
应用可以使用 C、C++、Python 或 Java 编写，面向同一套扁平 C ABI。
这个名字是铂的化学符号：小而致密、不易反应。

状态：**1.0.0**。冻结版本：具有仅选择和可过滤两种模式的组合框，原生弹出层表面以及键盘和键入预选导航；列表、表格和树共享一套多行选择模型——范围与切换原语、Shift+单击、Ctrl+单击、Shift+方向键、Ctrl+A，以及每个视图中的几何橡皮筋框选；屏幕阅读器驱动它们所需的 AT-SPI 角色和状态；每个头文件的所有权、生命周期、错误和线程契约，并由只追加式 ABI 测试保障；以及 `tools/release-check.sh`，它将 1.0 就绪检查清单转化为报告——这套 C ABI 包含 412 个函数，由 C++/Python/Java 封装，并强制要求绑定功能对等。API 由经审阅的基线、生成的函数参考和兼容性策略共同跟踪；参见
[docs/api-reference.md](docs/api-reference.md)、
[docs/api-policy.md](docs/api-policy.md) 和 [docs/ROADMAP.md](docs/ROADMAP.md)。

## 为什么选择 Pt

- Wayland 优先：`xdg-shell`、客户端装饰、`fractional-scale-v1`
  和 `text-input-v3`，不背负 X11 或 GL 运行时包袱。
- 保留模式控件树，以及一套扁平、不透明、便于绑定的 C ABI。
- 清晰的 FreeType + HarfBuzz 文本，包括 CJK 字形整形和分数缩放。
- 现代令牌化设计语言：38 个语义颜色角色和 10 个几何度量，
  深邃的深色调色板与干净的浅色调色板，色调化控件表面，柔和强调色，
  焦点可见环和克制的动效，可按窗口覆盖。
- 体验完整的控件：复选框和单选框、进度条和加载指示器、
  分段切换器、标签片和骨架屏、感知选择的输入框、可从列表选择或过滤列表的组合框、
  多行文本区域、剪切/复制/粘贴和撤销/重做。
- 内容密集型界面依然开销低廉：虚拟化列表、表格和树回收可见行，
  并共享一套多行选择模型；换行和网格容器会适应窗口；
  文本区域可对笔记和日志换行，并支持按光标目标导航和随内容增长调整尺寸。
- 开箱即用的导航外壳：带溢出和滑动指示器的标签页，
  带图标行和紧凑模式的侧边栏，以及 21 个内置矢量字形和无障碍图标按钮。
- 有契约的动效：公开的缓动曲线、可安全中断的过渡，
  以及框架控件会遵循的应用级减少动效支持。
- 菜单、模态对话框、堆叠式通知和延迟显示的工具提示共享控件、焦点和绘制管线，
  而不是另起一套 UI 系统；可用时，菜单和工具提示使用原生 `xdg_popup` 表面。
- 无障碍模型，包含角色、名称、描述、状态、文本/值暴露，
  以及供外部屏幕阅读器使用的可选 AT-SPI2 D-Bus 桥。
- 外部拖放，其放置目标可以同步读取一种 MIME 类型，或探查一项拖放提供内容并异步流式读取；
  可拖动的输入框文本；带完成动作的多类型 `pt_window_begin_drag` 源；`text/uri-list`
  文件拖动和拖动图像；以及合成器滑动/捏合手势、多触点捏合/旋转、
  触控笔压力和倾斜、长按和惯性滚动，全部通过一条归一化手势流提供。
- 在持久后备画布上进行损坏跟踪的部分重绘，配有合并式瓦片索引、
  子树范围的布局失效、哈希字体面表和容量受限的文本整形缓存，
  并将触摸输入映射到指针语义。
- 基于 `wl_shm` 的软件渲染：冷启动快，窗口未获焦时空闲占用不到单核的 0.3%，
  不依赖 GPU 驱动。
- Apache-2.0，全程使用 snake_case 标识符，没有隐藏的全局状态。

## 1.0 的新特性

- **冻结的接口表面。** `VERSION` 和 `pt_version_string` 为 `1.0.0`，
  SONAME 为 `libpt.so.1`，412 个函数的 C ABI 是 `api/pt.api` 中经审阅的基线。
  0.9 中弃用的两个符号已在此次冻结中移除，而不会带入 1.x 系列，
  因此 1.0 ABI 是 0.10 的有意子集，并且需要使用新的对象名。
  1.0 之后的所有新增均为只追加。
- **头文件中的契约。** 每个公共头文件都以 `Header contract` 块开头，
  其中说明 UI 线程规则（包括 getter）、返回指针的所有权和生命周期、
  回调生命周期、单一错误模型以及若干系列例外：`window.h`/`overlay.h` 中的原生弹出层抓取、
  `dnd.h` 中的数据提供内容与流生命周期、`widget.h` 中三个文本输入状态推送时机，
  以及任意线程的值辅助函数。生成的 [API 参考](docs/api-reference.md)
  将该块呈现为每个模块的前言，[架构](docs/zh-CN/architecture.md)
  按入口点类别公布线程表。
- **只追加 ABI 闸门。** `tests/test_abi.c` 固定每个公共枚举值、
  无障碍状态和修饰键位、哨兵值和限制值、公共结构体的字段顺序以及版本宏一致性，
  因此重新编号会让 `make check` 失败，而不会流入发布。
  审计的基线差异仍是这道防线的后半部分。
- **三份指南。** [主题](docs/zh-CN/theming.md)记录了全部 38 个颜色角色和 10 个度量、
  解析顺序、强调色派生以及实测的 WCAG 对比度下限；
  [无障碍](docs/zh-CN/accessibility.md)涵盖角色和状态派生、名称、文本、值、事件、
  AT-SPI 接口，以及面向其他适配器的契约；
  [迁移到 1.0](docs/zh-CN/migration-1.0.md)替换了 C、C++、Python 和 Java 中
  两个已移除的拖动辅助函数，并说明自 0.9 以来的令牌和选择变更。
- **文本接口与成文的 RTL 计划。** AT-SPI 桥现在将标签和文本区域视为一等文本对象：
  标签为只读，文本区域可选择且可编辑，两者都保留逻辑字符偏移。
  密码输入框报告其长度，而不暴露任何字符。
  [RTL 计划](docs/zh-CN/rtl.md)记录了 1.0 之后的 API 形态、UBA 与整形管线、
  UI 镜像规则、偏移映射以及一致性和测试义务；它是一项计划，不是 1.0 的承诺。
- **经对比度校正的调色板。** 1.0 默认值将深色强调色改为 `3568E0`，
  将浅色强调色改为 `2559E6`，并加深浅色主题中的危险、成功和警告角色，
  使正文文本通过 WCAG AAA，并使柔化文本、强调文本、状态文本和焦点环通过 AA。
  `test_theme` 会断言这些下限，而不只是描述它们。

## 1.0 的变更

1.0 在不新增公共 API 的情况下冻结 0.10 的功能集：接口表面包含 412 个函数，
对象为 `libpt.so.1`，`api/pt.api` 是经审阅的基线。本次发布自身的变更包括：

- **使用 text-input-v3 直接输入。** 即使启用了文本输入，普通 `wl_keyboard`
  文本现在也能到达获焦的输入框；修复前只有输入法提交才能插入文本，
  因此在输入法组合出内容之前，键入看起来毫无反应。快捷键组合仍被排除，
  组合提交仍处理预编辑文本。`make check-typing` 在嵌套合成器中通过虚拟键盘
  驱动一个获焦输入框，并断言文本完全一致。
- **无障碍桥中的只读文本。** 标签和按钮以 `READ_ONLY` 暴露
  `org.a11y.atspi.Text`；输入框和文本区域暴露 `EditableText` 和 `SELECTABLE_TEXT`；
  密码的 `GetCharacterAtOffset` 读取脱敏后的可见字符串，而不是泄露字符。
  实时桥接测试套件覆盖全部三条文本路径。
- **冻结且经审阅的 ABI。** 两个已弃用的拖动辅助函数已移除，
  基线为 `724 entries; 0 removed, 0 changed, 0 added`，每个绑定都达到 412/412，
  且允许列表为空。`tools/release-check.sh` 检查版本、SONAME、草稿标记、基线、参考文档和弃用项。
- **针对无头测试无法观察之处的实时防线。** `make check-wayland` 在完整全屏嵌套合成器中
  运行弹出层检查和一个 5 秒的获焦输入框空闲切片；`make check-typing` 覆盖直接输入；
  一个橡皮筋框选拖动帧和一个原生弹出层表面加入了性能上限。
- **1.0 指南。** 迁移、无障碍和主题指南已对照头文件进行校验，
  RTL 计划则面向 1.0 之后的工作编写。

### 从 0.10 延续

- **组合框。** `pt_combobox_create` 提供一个携带选项列表的字段：
  `pt_combobox_select_only` 的行为类似下拉框，
  `pt_combobox_filterable` 像输入框一样编辑，并以不区分大小写的子串匹配过滤弹出层。
  选项通过 `pt_combobox_add_item`、`pt_combobox_insert_item`、`pt_combobox_remove_item`、
  `pt_combobox_clear` 以及逐项文本、禁用标志和借用数据指针
  （`pt_combobox_set_item_data` / `pt_combobox_get_item_data`）进行管理。
  该字段提供 `pt_combobox_set_text`、`pt_combobox_set_placeholder` 和两个回调——
  `pt_combobox_set_on_change` 用于已提交的选择，
  `pt_combobox_set_on_text_change` 用于每次编辑——弹出层则通过
  `pt_combobox_open`、`_close`、`_is_open` 和 `pt_combobox_set_popup_max_height`
  控制（默认 260 px）。键盘：F4 或 Alt+Down 打开，方向键和 Page 键移动高亮项，
  Enter 或 Space 提交并关闭，Escape 关闭且保持原值，Tab 关闭并移动焦点；
  仅选择框会对选项文本执行键入预选，而可过滤框将 Home、End 和空格留给编辑器。
  合成器允许时，仅选择框会在原生 `xdg_popup` 上打开，因此列表可以溢出窗口边缘；
  可过滤框则留在窗口内，以便编辑器保持焦点。
- **所有三个视图共用一套多行选择模型。** 列表和树获得了表格已有的原语——
  `pt_list_view_select_range`、`_select_toggle`、`_get_selection`、
  `_get_selection_count`、`_is_row_selected`、`_clear_selection`
  以及 `pt_tree_*` 对应项，另有 `pt_table_select_toggle`——并遵循相同契约：
  范围两端均为包含式并会被钳制，设置函数会替换集合，并将锚点和主行留在其第一行，
  取消主行的选中会将其移动到下一个已选行，而
  `pt_list_view_clear_selection` 和 `pt_tree_clear_selection` 会使其保持在
  `pt_list_index_none`。集合与主行保持分离，`on_select`
  只有在手势真正移动主行时才会触发。
- **与模型匹配的手势。** 在列表、表格和树中均是如此：
  Shift+单击从锚点扩展，Ctrl+单击切换单行，从某一行或视口空白处开始的橡皮筋框选
  会选择其几何相交的每一行（拖动期间按住 Ctrl 会追加到集合，而不是替换它），
  Shift+方向键从锚点扩展，未按修饰键的方向键会将集合折叠到其落点所在行，
  Ctrl+A 选择整个模型。`tests/test_selection.c` 在全部三种视图中
  驱动同一套手势脚本，包括从行上和视口空白处开始的框选，因此任何分歧都会使测试套件失败。
- **屏幕阅读器所需的状态。** 新增角色 `pt_a11y_role_combo_box` 和
  `pt_a11y_role_list_box`——组合框报告前者，其弹出层面板报告后者——
  以及新增状态 `pt_a11y_state_expandable`、`pt_a11y_state_selectable`
  和 `pt_a11y_state_multi_selectable`。AT-SPI2 桥将它们映射到辅助技术所期望的数值，
  实时测试会断言视图上的 multiselectable、行上的 selectable 和 selected、
  分支上的 expandable/expanded 以及折叠后的 collapsed，还有数据视图上的 `GetRelationSet`。
- **绑定功能对等。** 全部 412 个公共函数都封装于 C++23、Python ctypes 和 Java FFM，
  审计按绑定对 0.10 表面（40 个函数）设门禁，且允许列表为空，
  测试套件运行 344 项 C++ 检查、33 项 Python 测试和 298 项 Java 检查。
- **1.0 就绪闸门。** `tools/release-check.sh` 将
  [docs/api-policy.md](docs/api-policy.md) 中的检查清单转化为通过/失败报告——
  版本一致性、草稿标记、基线和参考计数、功能对等、弃用清单、示例——
  并打印只有人工才能关闭的项。
- **安静的文本输入。** 空的 `zwp_text_input_v3.done` 表示输入法确认了
  Pt 发送给它的周边文本和光标矩形；回应这一确认又会要求另一次确认。
  Pt 现在会忽略它，除非有组合正在进行，因此带有获焦输入框的窗口
  不再持续重绘自身，也不再向辅助技术大量发送选择变更：
  同样的 5 秒实时切片从 67,499 个 `selection_changed` 事件和 2.13 秒 CPU
  降至 1 个事件和 0.65 秒。

## 0.9 的新特性

- **设计令牌，应用级与按窗口。** 语义颜色表从 24 个角色扩展到 38 个
  （`pt_theme_surface_container`、`_surface_container_high`、`_outline`、
  `_outline_variant`、`_text_disabled`、`_accent_pressed`、`_control_pressed`、
  `_danger_soft`、`_success_soft`、`_warning_soft`、`_scrim`、`_skeleton`、
  `_skeleton_highlight`、`_selection_text`），十个几何度量覆盖圆角半径、
  焦点环宽度和偏移、阴影层级、标签片高度和控件高度。
  `pt_app_set_theme_metric` 会全局设置它们；
  `pt_window_set_theme_color`、`pt_window_set_theme_metric` 和
  `pt_window_clear_theme_overrides` 可在不触碰应用主题的情况下为单个窗口重新设定样式，
  覆盖项在绘制时解析。
- **完整的状态矩阵。** 内置控件现在使用相同令牌绘制悬停、按下、禁用、忙碌和无效状态——
  `pt_widget_set_busy` 和 `pt_widget_set_invalid`——键盘焦点会绘制焦点可见环，
  而不是普通轮廓。新增 `pt_chip_create`，提供一个带可选字形、选中状态和移除按钮的
  胶囊形控件；`pt_skeleton_create` 提供闪烁的加载占位符。
  分段控件会滑动指示器，侧边栏绘制选中胶囊，卡片、菜单和对话框共享阴影层级令牌。
- **可就地编辑的数据视图。** 表格通过 `pt_table_begin_edit`（或双击）
  在单元格上打开输入框，通过 `pt_table_set_on_cell_edited` 提交，
  并让编辑器走正常撤销路径；`pt_table_select_range`、`pt_table_get_selection`
  以及橡皮筋框选或 Shift+单击选择，使多行集合独立于主行。
  表头可通过 `pt_table_set_columns_reorderable` 和
  `pt_table_set_on_columns_reordered` 拖动到新顺序，
  `pt_table_set_on_row_drag` 在隐式抓取仍然有效时把行拖动交给应用。
  树通过 `pt_tree_set_renamable`、F2 或缓慢双击就地重命名节点，
  并通过 `pt_tree_set_on_rename` 报告提交。
- **逐段排版。** `pt_text_span` 现在可以携带自己的字体族
  （`pt_text_span_has_family`）和字号（`pt_text_span_has_size`），
  不同字号的文本段共享一条基线，因此标题段、等宽段和小号上标
  可以在同一标签内对齐。
- **首次弃用。** `pt_window_begin_text_drag` 和
  `pt_window_begin_uri_drag` 被标记为 `pt_deprecated`，并给出到
  `pt_window_begin_drag` 的迁移路径，端到端验证了 API 策略的弃用周期：
  头文件属性、审计、绑定、测试和文档。1.0 冻结执行了记录的移除窗口，
  因此两个名称及其三个封装均已移除，`pt_window_begin_drag` 成为唯一的拖动源。
- **视觉回归。** `tests/test_visuals.c` 无头绘制标签片、状态、焦点环和样式化文本段，
  并在 `build/snapshots`（`PT_SNAPSHOT_DIR`）下写出确定性 PNG 快照，
  断言重复绘制字节完全一致，且浅色调色板不同于深色调色板。
- **绑定功能对等。** 全部 374 个公共函数都封装于 C++23、Python ctypes 和 Java FFM，
  审计现在按绑定对 0.9 表面（44 个函数）设门禁。
  绑定测试套件运行 286 项 C++ 检查、32 项 Python 测试和 248 项 Java 检查。

## 0.8 的新特性

- **异步结构化拖动数据。** 放置目标现在可以通过提供内容回调注册：
  `pt_drop_offer_mime_count`、`pt_drop_offer_mime`、
  `pt_drop_offer_has_mime`、`pt_drop_offer_actions` 和
  `pt_drop_offer_selected_mime` 描述源所声明的全部内容，而
  `pt_drop_offer_read` 通过 `pt_drop_stream`（`pt_drop_stream_mime`、`_size`、
  `_active`、`_cancel`）在事件循环上以块的形式流式返回已接受的 MIME 类型，
  而不是在放置回调内部进行。`max_bytes` 上限会将超大源转为错误，
  而不是内存耗尽。在发送端，`pt_window_begin_drag` 一次提供多种 MIME 类型，
  `pt_window_set_drag_finished_callback` 报告目标实际执行的动作。
  同步的单文本放置路径仍然可用。
- **样式化文本段。** `pt_widget_set_text_spans` 对标签或文本区域的字节范围
  应用字重、倾斜、颜色和下划线；文本段在设置时被复制并钳制，
  在换行和测量后保留，参与选择和光标移动，并可通过
  `pt_widget_get_text_span` 读回。`pt_widget_clear_text_spans`
  使控件恢复为纯文本。完整的双向文本段落布局仍留待 1.0 之后。
- **更深入的 `text-input-v3`。** 控件可以声明输入用途
  （`pt_widget_set_input_purpose`：电子邮件、URL、电话、数字、密码、终端），
  预编辑、提交、周边文本、删除周边文本和光标矩形更新都遵循协议，
  并具备输入法感知的撤销分组和光标放置。脚本化协议辅助器
  在无头环境中驱动预编辑、提交和取消，因此无需运行输入法即可测试该路径。
- **真实的 AT-SPI 数据视图接口。** 表格和树以可用的行数、列数和选择数
  以及对象关系响应 `org.a11y.atspi.Table`、`TableCell` 和 `Selection`，
  而不仅仅是属性；实时 D-Bus 测试会断言接口本身。
- **渲染器与布局扩展。** 窗口维护一个最多包含八个合并瓦片的损坏索引，
  并在各自裁剪区内重绘每个瓦片，因此局部更新会跳过大部分可见树遍历，
  而不仅仅是大部分像素。布局失效会记录脏子树，并在安全时从固定的祖先节点重新布局。
  圆角矩形采用平方距离快速路径，仅在抗锯齿带内调用 `sqrtf`，
  字体面查找是以字体族、字重、倾斜和字号为键的哈希表。
  `bench-guards` 增加了 4K 损坏帧配置和 100,000 行表格滚动配置。
- **绑定功能对等。** 全部 330 个公共函数中的每一个都封装于
  C++23、Python ctypes 和 Java FFM，任何缺口都会让 `make check` 失败。
  绑定测试套件现在运行 242 项 C++ 检查、31 项 Python 测试和 216 项 Java 检查。

## 0.7 的新特性

- **稳定键数据视图。** 列表和表格接受每行一个非零 `uint64_t` 键。
  选择会跟随该键，在行数变化、排序和模型刷新后依然保持；
  `pt_*_get_selected_key` 和 `pt_*_set_selected_key` 直接暴露该键。
  行文本回调为键盘键入预选提供标签，`pt_*_refresh_rows(first, count)`
  只重新绑定受影响的窗口，而不重建行池。
- **多列排序和列控制。** 表格通过 `pt_table_set_on_sort_columns`
  报告最多四个带优先级和方向的排序列；Shift+单击表头会扩展排序。
  列可以隐藏，隐藏时保留宽度和单元格，并在重新绑定后保留应用设置的宽度。
  表头单元格、行/列索引、排序键和树层级通过 AT-SPI 发布。
- **完整的输入生命周期。** 窗口报告活动拖动，可通过编程取消，
  并强制执行可配置的拖动超时。触控笔橡皮擦和笔杆按钮加入压力、倾斜和旋转。
  树除父子遍历外，还支持 Home、End 和 Page Up/Page Down。
- **完整的绑定功能对等。** 全部 311 个公共函数中的每一个都封装于
  C++23、Python ctypes 和 Java FFM，`make check` 将 311/311 作为硬门禁强制执行。
- **作为构建产物的文档。**
  [docs/api-reference.md](docs/api-reference.md) 从公共头文件生成，
  包含每个函数的所有权、有效控件类型、回调线程和失败契约；
  当它发生漂移时，`make api-check` 会失败。兼容性策略包含一个完整的弃用示例
  和一份扩展的 1.0 检查清单。
- **新示例。** [data.c](examples/c/data.c) 针对实时模型演练键控选择、
  批量失效和多列排序；[accessibility.c](examples/c/accessibility.c)
  展示角色、状态、文本/值暴露、事件传递和 AT-SPI 桥状态。

## 0.6 的新特性

- `pt_table_create`：基于列表行回收器的虚拟化表格。列拥有标题、宽度、
  对齐和调整大小手柄；单击表头会循环 无 → 升序 → 降序，并调用应用排序回调，
  该回调拥有模型并调用 `pt_table_refresh`。行和单元格按视口重新绑定，
  选择与激活与列表共享，表头、行和列角色暴露给无障碍客户端。
- `pt_tree_create`：可折叠层级，带稳定节点句柄、展开、深度和子节点访问器、
  每节点应用数据、选择、键盘左/右遍历和虚拟化行。
  节点文本、层级和展开状态以树/树项角色暴露。
- `pt_tabs_create`、`pt_sidebar_create`、`pt_icon_create` 和
  `pt_icon_button_create`：导航外壳，带可滚动标签条、动画活动指示器、
  带紧凑模式的侧边栏选择，以及 21 个在任何缩放下都保持清晰的内置矢量字形。
  图标按钮带有无障碍名称和工具提示。
- 交互完善：带 RGBA 拖动图像的 `text/uri-list` 拖动源，
  不干扰单指滚动的双触点捏合/旋转手势，以及在现有指针和手势路径上的
  触控笔压力、倾斜、旋转和工具事件。
- API 冻结准备：经审阅的 [API 基线](api/pt.api)，
  由 `make check` 运行的导出符号和绑定功能对等审计，一份成文的
  [兼容性策略](docs/api-policy.md)，以及冻结的 0.5 数据视图和动效名称。
- 一个[带标签页的展示程序](examples/c/gallery.c)，包含表格、树和导航页面，
  以及 [C++](examples/cpp/workspace.cpp)、[Python](examples/python/workspace.py) 和
  [Java](examples/java/Workspace.java) 工作区示例。

## 要求

- Linux 与 Wayland 合成器
- C23 编译器（参考：GCC 16.2.1）和 GNU Make
- wayland-client、wayland-cursor、xkbcommon、Fontconfig、FreeType、HarfBuzz 和
  zlib 开发包
- 可选：用于 AT-SPI2 桥的 dbus-1、Python 3.10+、JDK 22+，以及用于绑定的
  C++23 编译器

在 Arch Linux 上：

```sh
sudo pacman -S base-devel wayland wayland-protocols xkbcommon fontconfig \
    freetype2 harfbuzz zlib dbus
```

## 构建与测试

```sh
make -j"$(nproc)"          # library and C/C++ examples
make check                 # all headless C suites, binding suites, API audit and docs
make check-bindings        # only the C++ wrapper, Python ctypes and Java FFM tests
make check-wayland         # live suite: nested compositor, popups and idle guard
make check-typing          # hermetically inject keys into a nested compositor
make bench                 # headless layout, full/partial paint and startup benchmarks
make perf-check            # benchmarks plus regression ceilings (4K, large-tree scroll)
make install-check         # staged install, SONAME/ABI and four-language consumers
tools/ci.sh                # CI-equivalent local gate, including sanitizers and tarball
tools/release-check.sh     # readiness report for the frozen release
```

`make check` 以无头方式运行，不需要合成器。它包含 C 测试套件、
C++/Python/Java 绑定测试套件、导出符号和 API 基线审计、
硬性的 412/412 绑定功能对等闸门、按轮次的 0.6–0.10 表面审计、
`docs/api-reference.md` 与头文件一致的检查，以及针对文本编辑、URI 解析、
数据视图索引和损坏矩形的随机属性测试套件。
实时 Wayland 冒烟测试不属于 `make check`。`make check-wayland`
会构建并运行它；安装 `niri` 后，它会启动全屏嵌套合成器，
在父会话中聚焦嵌套窗口，并作为该 seat 的唯一客户端运行测试，
因此弹出层检查和 5 秒获焦输入框空闲守卫会重试或跳过，而不是与桌面竞争。
没有显示环境时，它会干净地跳过。
`make check-typing` 在启用 text-input-v3 时守卫普通键盘路径：
安装 `niri` 后，它会启动嵌套合成器、聚焦输入框并通过虚拟键盘注入按键，
断言输入框收到的文本与键入内容完全一致；没有实时会话或 niri 时它会跳过。
`tools/ci.sh --quick` 跳过 sanitizer 和 tarball 重建；
当某项中位数退化超过其守卫上限时，`make perf-check` 会失败，而不只是打印结果。
`tools/release-check.sh` 不属于 `make check`：它读取目录树并报告
[docs/api-policy.md](docs/api-policy.md) 中的 1.0 检查清单是否可以签核，
并列出只有人工才能关闭的项。

## 示例

| 语言 | 示例 | 运行 |
| --- | --- | --- |
| C | [hello.c](examples/c/hello.c) | `./build/examples/c/hello` |
| C | [gallery.c](examples/c/gallery.c) | `./build/examples/c/gallery` |
| C | [interaction.c](examples/c/interaction.c) | `make run-interaction` |
| C | [data.c](examples/c/data.c) | `make run-data` |
| C | [accessibility.c](examples/c/accessibility.c) | `make run-a11y` |
| C | [notes.c](examples/c/notes.c) | `make run-notes` |
| C | [combobox.c](examples/c/combobox.c) | `make run-combobox` |
| C++23 | [counter.cpp](examples/cpp/counter.cpp) | `make run-counter` |
| C++23 | [workspace.cpp](examples/cpp/workspace.cpp) | `make run-cpp-workspace` |
| C++23 | [combobox.cpp](examples/cpp/combobox.cpp) | `make run-cpp-combobox` |
| Python | [counter.py](examples/python/counter.py) | `make run-python` |
| Python | [notes.py](examples/python/notes.py) | `python examples/python/notes.py` |
| Python | [workspace.py](examples/python/workspace.py) | `make run-python-workspace` |
| Python | [combobox.py](examples/python/combobox.py) | `make run-python-combobox` |
| Java 25 | [HelloPt.java](examples/java/HelloPt.java) | `make run-java` |
| Java 25 | [Workspace.java](examples/java/Workspace.java) | `make run-java-workspace` |
| Java 25 | [Combobox.java](examples/java/Combobox.java) | `make run-java-combobox` |

C 和 C++ 示例由 `make` 构建；Python 和 Java 运行目标会自动构建或定位库。
所有示例既可原生运行，也可在无头测试环境中运行。

## 从 C 使用

```c
#include <pt/pt.h>

static void quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.hello");
    if (!app) {
        return 1;
    }
    pt_window *window = pt_window_create(app, "Hello, Pt", 680, 440);
    pt_widget *card = pt_card_create(pt_window_root(window));
    pt_widget_set_padding(card, 28, 28, 28, 28);
    pt_widget_set_gap(card, 16);
    pt_label_create(card, "Hello, Pt");

    pt_widget *button = pt_button_create(card, "Quit");
    pt_button_set_on_click(button, quit, app);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

针对已安装的副本构建：

```sh
cc hello.c $(pkg-config --cflags --libs pt) -o hello
```

布局、回调、输入框、主题、生命周期规则和无头测试参见
[docs/quickstart.md](docs/zh-CN/quickstart.md)。

## 绑定

C ABI 就是契约。三个绑定均为手写、体积小，并由 `make check-bindings` 覆盖。

```cpp
#include <pt/pt.hpp>

pt::app app("dev.pt.cpp");
pt::window window(app, "Hello", 480, 320);
pt::label(window.root(), "Hello, Pt");
window.show();
return app.run();
```

```python
import pt

app = pt.App("dev.pt.python")
window = pt.Window(app, "Hello", 480, 320)
pt.label(window.root, "Hello, Pt")
window.show()
raise SystemExit(app.run())
```

```java
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import pt.Pt;

try (Arena arena = Arena.ofConfined()) {
    MemorySegment app = Pt.app_create("dev.pt.java");
    MemorySegment window = Pt.window_create(app, "Hello", 480, 320);
    Pt.label(Pt.window_root(window), "Hello, Pt");
    Pt.window_show(window);
    Pt.app_run(app);
    Pt.app_destroy(app);
}
```

回调生命周期、编译和运行命令，以及确切的封装接口参见
[docs/bindings.md](docs/zh-CN/bindings.md)。

## 安装

```sh
make install PREFIX="$HOME/.local"
pkg-config --modversion pt
```

安装规则会将共享库、静态归档、C 和 C++ 头文件、可重定位的 `pt.pc`
以及 Python/Java 绑定源放到前缀下。pkg-config 文件使用 `${pcfiledir}`，
因此可重定位，并兼容 `DESTDIR` 暂存。使用
`pkg-config --static --libs pt` 进行静态链接。

可用 `make dist` 生成源码压缩包，它还会在归档旁写出 `.tar.gz.sha256`
附属文件；发布检查清单参见 [docs/release.md](docs/release.md)。

## 性能

在 Intel i7-13700H（GCC 16.2.1、`-O2`、1280x800）上的无头参考测量。
收尾的 1.0.0 守卫测试测得：一棵 1,506 节点的树，带 300 个密集滚动行，
布局耗时 2.292 ms，完整绘制耗时 13.727 ms；一次仅损坏一个密集行的局部更新
重绘耗时 1.017 ms，为全窗口成本的 7.4%，并绘制了完整帧所访问 61 个节点中的 15 个。
一棵展开的 10,200 节点树滚动时每帧耗时 2.562 ms，同时实例化 13 个行控件；
100,000 行表格定位在 22 个实例化行上耗时 10.763 ms；
密集 4K 完整重绘耗时 35.941 ms，其中损坏行耗时 3.175 ms。
两个 M17 配置测得橡皮筋框选拖动帧为 8.8-9.3 ms，
弹出层面板帧为 1.20-1.29 ms，均低于其 20 ms 上限。
包含 Fontconfig/FreeType 初始化的冷启动首帧在 800x600 下为 8.990-10.760 ms，
300 ms 空闲循环消耗 0.582-0.700 ms CPU。`make perf-check` 为这些场景
强制执行回归上限；在实时合成器上，`make check-wayland` 强制检查获焦输入框空闲切片
（测量运行中为单核的 0.6-14.9%，低于每 5 秒 1.5 秒的上限），
并且获焦输入框不再持续重绘和重复通告其选择，这一点由 0.10.0 修复。
细节、方法和内存注意事项见 [docs/performance.md](docs/performance.md)。

## 文档

- [快速入门](docs/zh-CN/quickstart.md)
- [C API 参考](docs/api-reference.md)
- [绑定](docs/zh-CN/bindings.md)
- [架构](docs/zh-CN/architecture.md)
- [主题](docs/zh-CN/theming.md)
- [无障碍](docs/zh-CN/accessibility.md)
- [从右到左文本计划](docs/zh-CN/rtl.md)
- [迁移到 1.0](docs/zh-CN/migration-1.0.md)
- [API 兼容性策略](docs/api-policy.md)
- [性能](docs/performance.md)
- [与其他工具包的比较](docs/zh-CN/comparison.md)
- [路线图](docs/ROADMAP.md)
- [发布流程](docs/release.md)

快速入门、架构、绑定、主题、无障碍、RTL 计划、迁移和比较指南提供简体中文版；
生成的 API 参考、发布记录、路线图、性能说明和政策页面保留英文。

<a id="limitations-in-10"></a>

## 1.0 中的限制

- 仅支持 Wayland；没有 X11、Windows 或 macOS 后端。
- 1.0 ABI 已冻结：1.0 之后的新增均为只追加，移除需要主版本发布和
  SONAME 提升。签入的基线、符号审计和 `tests/test_abi.c` 会让任何漂移显式化。
  布局、绘制和事件循环仅在调用线程上运行。
- 渲染器仅使用 CPU。密集的全窗口动画在桌面尺寸窗口上表现舒适，
  但不是 4K/120 Hz 的 GPU 路径；加速后端是 1.0 之后的候选。
- 后端支持时，菜单、工具提示和仅选择组合框弹出层使用原生 `xdg_popup` 表面；
  对话框、通知和可过滤组合框弹出层仍合成在窗口内，因为它们是模态的、堆叠的，
  或需要编辑器保持焦点。原生弹出层仍需要一个已配置的 xdg-shell 窗口，
  因此无头运行使用窗口内回退方案。
- AT-SPI2 桥是可选的（`pt_app_enable_a11y`），需要以 `dbus-1` 构建，
  并暴露控件树而不是任意绘制内容。屏幕阅读器可以看到 Pt 控件，
  但看不到没有无障碍元数据的自定义绘制。
- 拖动源和结构化放置提供内容需要与任何 Wayland 拖动相同的隐式指针抓取，
  因此 `pt_window_begin_drag` 仅在按压处于活动状态时成功，
  而无头运行没有可通信的拖动合成器。流式读取传递字节；
  解码 JSON 或 URI 列表是应用的工作。触摸手势使用两个触点；额外触点会被忽略。
  触控笔支持取决于合成器提供的 `tablet-v2`。
- 样式化文本段覆盖标签和文本区域字节范围内的字重、倾斜、颜色、下划线、
  逐段字体族和逐段字号。没有双向文本段落布局，没有内联图像，
  也没有逐段拉伸或光学对齐，文本区域仍以统一的段落几何编辑纯文本。
  1.0 之后的设计、兼容性规则和测试义务写在 [RTL 计划](docs/zh-CN/rtl.md)中。
- 表格列保留应用设置的宽度，这些宽度在重新绑定和行回收后仍然有效；
  列可以隐藏、通过拖动调整大小，也可以由用户重新排序。排序报告最多四个
  按优先级排列的列，单元格编辑加多行选择也已就位，
  但应用仍然拥有并重排模型，表格从不自行排序行。
  橡皮筋框选会选择其相交的每一行，在列表、表格和树中均是如此，
  并且可以从某一行或最后一行下方的视口空白处开始；
  在空白处普通按压会清除旧集合，且绝不激活此前的主行，
  而 Ctrl 会将集合保留为追加式基础。在树中，它按可见行划定范围，
  因此折叠分支会按其显示的那一行被扫过。树节点保存纯文本并就地重命名；
  它们不携带自定义单元格控件。
- 组合框弹出层在滚动区域内为每个匹配项实例化一行，
  因此包含数万个选项的模型应放在列表视图中；过滤才是让弹出层保持简短的手段。
  项数据指针是借用的，并在插入或移除其他项时随该项移动。
- 列表、表格和树的行由 Pt 实例化，应用不得销毁它们；
  自定义单元格通过传给绑定回调的行控件添加。
- Fontconfig 启动成本主导首帧内存：在参考机器上为 24.7-27.8 MiB RSS，
  但 PSS 为 10.2-12.8 MiB，因为字体数据与系统共享。
  稳态下的控件/UI 内存本身很小。

## 许可证

Apache License 2.0。随附的 Wayland 协议 XML 保留其 MIT 头文件；
参见 [NOTICE](NOTICE) 和 [THIRD_PARTY.md](THIRD_PARTY.md)。
