# 从右到左与双向文本

[English](../rtl.md) | 简体中文

状态：2.0.0 已交付。本文档是 2.0 随版本发布的设计契约，而不是 1.0 之后的
计划。方向 API 是追加式的，文本模型与偏移契约保持不变，纯 LTR 应用逐字节
保留 1.4 的像素。下文每一节都说明已落地的内容、构建过程中作出的决策，以及
仍然存在的限制。

相关阅读：[architecture.md](architecture.md) 描述布局和绘制中的双向文本管线，
[accessibility.md](accessibility.md) 定义辅助技术使用的字符偏移和方向契约，
[api-policy.md](../api-policy.md) 记录 2.0 为何保持 SONAME `libpt.so.1`。

## 保证与契约

- 公共文本 setter、getter、文本段和选择使用 UTF-8 字节。进程内
  `pt_widget_get_a11y_text` 同样使用字节偏移；AT-SPI `Text` 接口在 D-Bus 上
  转换为 Unicode 标量值偏移。添加双向文本时这两种单位都不改变：方向是呈现
  属性。
- 段落算法是完整的 Unicode 双向算法（UBA），包括显式嵌入和覆盖、隔离、弱字符
  和中性字符解析、配对括号、隐式层级以及换行后的行级规则。Pt 不再把混合方向
  的行当作单个猜测方向的文本段来整形。
- 存储的字符串、选择模型和每个公共偏移都保持逻辑顺序。视觉顺序只存在于测量、
  绘制、命中测试和插入符号放置内部，由内部的逻辑/视觉映射支撑。
- UI 方向和文本方向互相独立。LTR 代码编辑器可以位于 RTL 外壳内，反之亦然。
- 从不设置方向的纯 LTR 应用与 1.4 行为完全一致：相同布局、相同像素，并且没有
  新增的逐帧工作。

## 术语

- **逻辑顺序**是 UTF-8 字符串和选择模型中字符的顺序。文本显示时它永远不会
  改变。
- **视觉顺序**是 UBA 分配嵌入层级并重排序之后，一行上字形文本段的顺序。
- **文本方向**控制文本的段落排序、字形整形和起始/结束对齐。
- **UI 方向**控制行子项的顺序、起始/结束对齐、滚动条位置，以及可镜像的界面
  元素，例如箭头和排序箭头。
- **自动**文本方向根据段落的第一个强字符解析每个段落，如 UBA 规则 P2/P3
  所规定。空段落或仅含中性字符的段落回退到解析后的 UI 方向。

## 方向模型

公共表面是两个枚举和八个函数。`pt_direction` 仍是无关的行/列布局轴。

```c
typedef enum pt_ui_direction {
    pt_ui_direction_inherit = 0,
    pt_ui_direction_ltr,
    pt_ui_direction_rtl
} pt_ui_direction;

typedef enum pt_text_direction {
    pt_text_direction_auto = 0,   /* first strong character per paragraph */
    pt_text_direction_ltr,
    pt_text_direction_rtl
} pt_text_direction;

void pt_app_set_ui_direction(pt_app *app, pt_ui_direction direction);
pt_ui_direction pt_app_get_ui_direction(const pt_app *app);
void pt_window_set_ui_direction(pt_window *window, pt_ui_direction direction);
pt_ui_direction pt_window_get_ui_direction(const pt_window *window);
void pt_widget_set_ui_direction(pt_widget *widget, pt_ui_direction direction);
pt_ui_direction pt_widget_get_ui_direction(const pt_widget *widget);
void pt_widget_set_text_direction(pt_widget *widget, pt_text_direction direction);
pt_text_direction pt_widget_get_text_direction(const pt_widget *widget);
```

UI 解析顺序为控件的显式值，然后是具有显式值的最近祖先，然后是窗口，然后是
应用，最后是 LTR。文本解析顺序为控件的显式值，然后是具有显式值的最近祖先，
然后是自动；自动仅在中性字符段落中把解析后的 UI 方向作为最后强字符回退。
setter 忽略 NULL 和越界值，在变更时使布局失效并请求重绘。getter 返回存储的
显式值，而不是解析后的值：未设置的控件报告 `pt_ui_direction_inherit` 或
`pt_text_direction_auto`。

`inherit` 和 `auto` 是零值，因此零初始化的控件保持 1.x 行为；想要 RTL 外壳的
应用程序在应用或窗口上设置一次 UI 方向。嵌套编辑器用
`pt_widget_set_ui_direction(editor, pt_ui_direction_ltr)` 覆盖它。本版本不会
从进程区域设置猜测 UI 方向；1.0 没有区域设置 API，错误的猜测会镜像每个窗口。
自动文本方向是安全的，因为它按段落应用，并且不会移动界面元素。

```c
pt_app_set_ui_direction(app, pt_ui_direction_rtl);
pt_widget *editor = pt_text_area_create(root, "Write a note");
pt_widget_set_ui_direction(editor, pt_ui_direction_ltr);
pt_widget_set_text_direction(editor, pt_text_direction_auto);
```

## 引擎与管线

2.0 在 `third_party/sheenbidi` 下随源码附带 **SheenBidi v3.0.0**（Apache-2.0，
标签提交 `cfe430e`，UCD 17.0.0），作为单个 unity 对象编译，隔离其警告并隐藏
所有 `SB*` 符号。FriBidi 被否决：其 LGPL 条款会限制静态链接和分发。
`PROVENANCE.md` 记录标签、获取摘要和升级政策；
[THIRD_PARTY.md](../../THIRD_PARTY.md) 记录许可证。

上游 UTF-8 入口存在 L1 行末空白缺陷，因此 Pt 的适配层先把 UTF-8 解码为 UTF-32，
在码点旁保留字节偏移表，并在该内部路径上驱动引擎。`src/bidi.c` 拥有适配层；
其外部看不到 SheenBidi 类型。

对每个段落：

1. 显式文本方向选择基础层级。自动在逻辑文本上运行 UBA P2/P3，跳过隔离区内的
   字符，并对仅含中性字符或空的段落回退到解析后的 UI 方向。
2. 引擎解析嵌入层级并应用规则 X9 语义：双向控制字符保留在存储字符串和整形输入
   中，但绘制时跳过。
3. 文本段按层级重排序。配对字符（圆括号、方括号、角引号和 Unicode 镜像表）
   由 HarfBuzz 在绘制时镜像；Pt 从不预镜像字节或字形槽。
4. 文本段在层级、文字系统和字体边界处拆分，并使用显式的 `direction`、`script`
   和 `language` 整形。阿拉伯连接和拉丁字距在混合方向行中保持不变。
5. 换行按逻辑顺序生成候选行；随后对每一行重排序、测量和绘制。规则 L1 重置视觉
   行末的空白，软换行边界让插入符号亲和性落在正确的行上。
6. 逻辑字节偏移和视觉 x 位置之间的双向映射供测量、换行、插入符号放置、命中
   测试、选择绘制和省略号使用。省略号位于行的视觉末端，因此被截断的 RTL 行
   绝不会切掉逻辑第一个字符。

换行标签和文本区域按行重排序，并以每个段落的基础层级为准，因此一个文本区域
可以显示相反方向的段落，而插入符号和选择仍是一个逻辑范围。整形缓存以文本、
字体、方向和层级键为键，因此相同字节按 LTR 和 RTL 整形不会冲突。

## 布局与绘制

- 具有 RTL UI 方向的容器从右到左布局 flex 行子项，将
  `pt_align_start`/`pt_justify_start` 映射到右边缘，并对换行、网格和滚动几何
  应用相同规则。
- 表格和树的列保持逻辑顺序；只有水平起始边缘移动。这是通用行镜像之上的内部
  逐控件退出选项，因为列被翻转的排序数据表不再是同一张表。
- 标签页、侧边栏、选择器弹出层、客户端装饰、覆盖层和菜单根据解析后的 UI 方向
  放置其界面元素。滚动条位于 RTL 左侧，树展开箭头位于右侧。
- RTL 输入框右锚定：在 `scroll_x = 0` 时显示其文本的逻辑开头，也就是视觉右端；
  键入或滚动使文本向左移动。1.4 将逻辑末端锚定在左侧的行为，正是 2.0 记录的
  有意修正。
- 起始/结束对齐根据段落方向解析，包括多段落文本区域。居中对齐不变。
- 内边距和外边距保持物理方向，没有 start/end API。镜像行翻转子项顺序和对齐，
  而不是四个内边距值的含义。这是 2.0 记录的决策，而不是隐含保证。
- 省略号和裁剪留在视觉末端。用户提供的图像绝不会自动镜像。

## 编辑、选择与命中测试

默认采用**视觉插入符号移动**，这是在真实键盘上针对 RTL 运行测试后作出的决定：

- 左/右方向键移动到视觉相邻的 cluster 边界，在 RTL 文本段内可能增大或减小
  逻辑字节偏移。视觉 cluster 边规则防止插入符号落在 cluster 中间。
- Home/End 前往行的视觉起始和结束。Ctrl+Home/Ctrl+End 前往文档的逻辑首尾。
- Shift+方向键从固定的逻辑锚点扩展，因此尽管插入符号按视觉移动，报告的选择
  始终是逻辑字节范围。
- Backspace 和 Delete 保持逻辑语义：删除存储字符串中前一个或后一个字符，
  绝不是视觉相邻字符。
- Up/Down 和 Page Up/Page Down 跨行保持视觉目标 x。在换行的 RTL 行上，
  Shift+方向键保持在该行的视觉顺序内；用 Shift 跨软换行仍是行内视觉移动。
- 鼠标单击通过带插入符号亲和位的双向映射，把视觉 x 映射到最近的逻辑边界。
  跨文本段的选择为每个视觉文本段绘制一个矩形，而不是一个反向区间。
- 发送给 `text-input-v3` 和无障碍客户端的插入符号矩形是逻辑插入符号处的视觉
  矩形。周边文本和删除周边文本保持逻辑字节；预编辑按其插入上下文的方向整形。
- 密码输入框按存储的文本方向绘制圆点，因此圆点绝不会通过视觉顺序泄露内容的
  方向。
- 需要逻辑移动的应用程序可在 `pt_window_set_key_callback` 中拦截 Left、Right、
  Home 和 End，该回调先于获焦控件运行，并消费这些按键。这个逃生通道是 2.0 的
  答案；没有第九个方向 API，也没有逐控件插入符号模式属性。

## 无障碍

- AT-SPI `Text` 方法按逻辑顺序报告字符偏移。屏幕阅读器读取逻辑字符串，而不是
  视觉重排序后的结果。
- 每个无障碍对象把解析后的 UI 方向发布为无障碍属性（`direction` 为
  `ltr`/`rtl`），文本对象通过 `Text.GetAttributes` 和
  `Text.GetDefaultAttributes` 发布段落方向，并用覆盖可见文本的标量范围表示。
- 字节/标量的分工是有意的：C 层 `pt_widget_get_a11y_text` 继续返回**字节**
  偏移，而 D-Bus `Text` 接口转换为**标量**偏移。本指南旧措辞称“无障碍 getter
  使用标量偏移”，这对 C getter 是错误的，此处更正。
- 角色、名称和状态不变。方向是呈现元数据；它永远不会改变角色或状态。
- 密码文本在每个方向下都保持隐藏。桥接层报告方向和插入符号，但不报告字符，
  字符计数安全行为不变，并且没有双向文本路径会通过属性或事件暴露密码字符。

## 绑定与示例

八个函数在同一次变更中全部封装于 C++、Python 和 Java，功能对等闸门对每种语言
报告 564/564，允许列表为空。封装层是普通的值 setter/getter；C++ 的 setter
返回构建器，Python 暴露属性加 `set_*` 方法，Java 以 `int` 接收 C 枚举值。

C 示例画廊包含一个 RTL 屏幕（`examples/c/rtl_gallery.c`），其中有镜像行、
阿拉伯文/希伯来文/拉丁文混排标签、RTL 输入框、RTL 文本区域、嵌套 LTR 面板和
方向切换。`examples/cpp/rtl_direction.cpp`、
`examples/python/rtl_direction.py` 和 `examples/java/RtlDirection.java` 是更小的
切换示例，用于演练绑定层的 setter 和 getter。

## 兼容性与推出

双向文本在 API 层面是追加式的，但对于已经在屏幕上放置 RTL 文本的应用程序，
会改变像素。这是渲染修正，而不是新的布局契约。2.0 保持 SONAME `libpt.so.1`：
Makefile 将 `ABI_MAJOR` 与 `VERSION` 分开跟踪，方向 API 只新增声明，不改变任何
现有声明。`tools/release-check.sh` 将 SONAME 与 `ABI_MAJOR` 比较，因此仅行为
变化的主版本不会被误报为二进制破坏。

两道硬门保护 LTR：

- `test_visuals` 中 17 张纯 LTR PNG 快照与其 1.4 输出逐字节一致。
- `tools/ltr-compat.sh` 在同一台机器上构建 1.4.0 和 2.0 树，并逐字节比较 10 个
  LTR 场景。发布闸门要求 10/10 `SAME`。不签入 golden hash，因为 FreeType、
  HarfBuzz 和已安装字体在不同机器上会合理变化；在同一台机器上构建两个版本可以
  让这些因素保持恒定。

现有枚举值不变。两个新枚举追加，`pt_ui_direction_inherit` 和
`pt_text_direction_auto` 是零值，因此零初始化的控件保持旧行为。公共字节偏移
和 AT-SPI 字符偏移契约不变。

## 性能

LTR 路径是常见路径，并获得快速路径：适配层根据引擎的 Unicode 双向类型分类文本
（任何 R/AL 字符或双向格式控制字符都需要 UBA），而不是根据文字系统猜测；无法
重排序的文本使用旧的单文本段整形路径，零额外分配，零逐帧成本。该分类已对
292,531 个已分配码点扫描，与引擎自身的类别比较有 0 处失配。

每次重建（布局重建、宽度变化、方向变化或文本编辑）的双向分析为 UTF-8 字节的
O(n)；文本段/映射分配受行长度限制，并随文本布局释放。整形缓存以方向为键，
因此相同字节的 LTR 与 RTL 整形是独立且有界的条目。重排序和测量为每行增加一个
很小的常数；守卫套件在 LTR 配置旁运行一个混合文字系统的密集段落和一个 RTL
表格行；任一方向出现退化都会让 `make perf-check` 失败。

早先“未更改的行不产生逐帧分配”的承诺被撤回：1.4 本就每次绘制都重建行布局，
因此它从未成立。2.0 的契约是诚实的——**每次重建有界**——未更改的帧仍然不做
任何工作，因为绘制是损坏驱动的。

## 已知限制

以下是记录在案的行为，而不是隐藏的意外：

- 表格水平滚轮滚动需要 Wayland `wl_pointer` 在已测试合成器中不提供的平台轴。
  表格通过滚动条和键盘水平滚动；表格上的水平滚轮不是 2.0 的功能。
- 在 RTL 中，Shift+方向键选择到达软换行时仍保持行内视觉移动；用 Shift 跨换行
  按视觉规则扩展，而不是按文档顺序文本段扩展。设置之后，多行选择仍是逻辑的。
- 跨多个映射行的选择会在映射边界处向逻辑边缘折叠；绘制的矩形是正确的，但跨行
  的程序化范围应从逻辑偏移设置。
- `pt_text_span` 没有方向字段并保持冻结。段落方向来自控件，嵌入式反向短语在
  文本中使用 Unicode 隔离控制字符。
- 阿拉伯文和希伯来文需要覆盖它们的字体。缺失字形是记录在案的字体回退/豆腐块
  结果，由 `pt_app_font_uncovered_count` 报告，而不是硬性配置错误；为示例安装
  Noto Sans Arabic/Hebrew 或 DejaVu Sans。
- 双向控制字符可以重排读者看到的内容，这属于 Trojan Source 一类问题。Pt 不
  自动清理文本；接受不可信文本的应用程序应自行剥离或显示控制字符。未来的可选
  API 可能暴露控制字符清单。

## 测试计划

- **UBA 一致性。** Unicode 18.0.0 `BidiTest.txt` 和 `BidiCharacterTest.txt`
  连同 Unicode License v3 以 gzip 形式随源码附带在 `tests/data/bidi`。
  `test_bidi_conformance` 在 `make check` 中运行 770,241 + 91,707 个用例，0 失败
  （约 0.5 秒），并打印 Unicode 版本。
- **引擎与字节映射。** `test_bidi` 覆盖段落基础层级、字节映射、为整形保留但
  绘制时跳过的 X9 控制字符、显式文本段方向、视觉文本段顺序、括号镜像、阿拉伯
  连接、LTR 快速路径预扫描以及整形缓存的方向键。
- **方向 API。** `test_direction` 覆盖八个函数、经由控件/祖先/窗口/应用的继承、
  显式 getter、零默认值、带隔离区的自动 P2/P3、中性回退和非法输入拒绝。
- **布局与编辑。** `test_rich_text`、`test_text_area` 和 `test_visuals` 覆盖
  换行 RTL 行、逐行基础层对齐、L1 空白、视觉末端省略号、视觉插入符号边界、
  软换行亲和性、多矩形选择、输入法矩形和密码圆点，以及 RTL 快照。
- **镜像。** `test_table`、`test_tree`、`test_navigation` 和视觉套件覆盖行顺序、
  滚动条一侧、箭头、表格/树逻辑列和嵌套 LTR 面板。
- **无障碍。** `test_a11y` 和实时 `test_a11y_bridge` 覆盖对象级方向属性、文本
  属性方向和带方向的密码用例。
- **LTR 兼容性。** 17 张快照 golden 和 `tools/ltr-compat.sh` 的 10 场景跨版本
  比较是发布闸门，而不是可选检查。

## 已交付波次

原计划中的交付顺序成为 2.0 工作流：

1. 方向属性、解析和自动检测；无头 API、继承和默认行为测试（W1）。
2. 标签和单行文本的 UBA 层级、重排序、镜像和按文本段整形，LTR 快照保持不变
   （W0/W2）。
3. 换行标签和文本区域：断行、省略号、对齐以及逻辑/视觉映射（W3）。
4. 编辑：插入符号、选择绘制、命中测试、键盘移动和输入法矩形（W4）。
5. UI 镜像：flex/换行/网格/滚动几何、表格/树逻辑列、导航、选择器、装饰、
   覆盖层和绘制界面元素（W5a/W5b）。
6. 无障碍属性、绑定和示例（W6a/W6b）。
7. 一致性、模糊测试、性能加固和 LTR 字节兼容闸门（W7）。