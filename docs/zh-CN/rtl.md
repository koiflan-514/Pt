# 从右到左与双向文本

[English](../rtl.md) | 简体中文

状态：2.0.0 已交付，2.1.0 扩展。本文档是 2.0 随版本发布的设计契约，加上 2.1
的人体工学与安全新增内容，而不是 1.0 之后的计划。每项新增都是追加式的，文本模型
与偏移契约保持不变，纯 LTR 应用逐字节保留 1.4 的像素。下文每一节都说明已落地的
内容、构建过程中作出的决策，以及仍然存在的限制。2.1 各节已如此标注；
[m23-design.md](../m23-design.md) 记录其设计输入，[release.md](../release.md)
记录发布。

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
`pt_widget_set_ui_direction(editor, pt_ui_direction_ltr)` 覆盖它。Pt 不会从
进程环境猜测 UI 方向，因为错误的猜测会镜像每个窗口。2.1 改为提供显式提示：
应用传入它已经解析好的区域设置，纯函数把它映射为方向，结果作为应用回退值加入
解析链。自动文本方向是安全的，因为它按段落应用，并且不会移动界面元素。

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
- 内边距可以按任一顺序设置。`pt_widget_set_padding` 保持物理语义并且是默认；
  `pt_widget_set_padding_logical`（2.1 起）接收 top/start/bottom/end，并在每次
  布局时按控件的 UI 方向解析 start/end，因此 RTL 控件的 start 在右侧，之后的方向
  变化无需再次调用即可重新解析。两个 setter 共享同样的四个边缘，last-call-wins。
  外边距仍是物理的，没有逻辑 setter：镜像行翻转子项顺序和对齐，而不是四个外边距值
  的含义。
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
- 逻辑移动是策略，而不是拦截。2.1 新增 `pt_caret_movement`（见下一节），因此
  需要逻辑方向键的应用直接设置它；需要其他行为（自定义移动、额外按键）的应用仍可
  在 `pt_window_set_key_callback` 中拦截 Left、Right、Home 和 End，该回调先于
  获焦控件运行。回调的优先级仍然高于策略。

## 插入符号移动策略（2.1）

2.0 默认选择视觉移动，并把逻辑移动留给窗口按键回调。2.1 保留默认值，并让替代方案
成为声明式：

```c
typedef enum pt_caret_movement {
    pt_caret_movement_inherit = 0,
    pt_caret_movement_visual,
    pt_caret_movement_logical
} pt_caret_movement;

void pt_app_set_caret_movement(pt_app *app, pt_caret_movement movement);
pt_caret_movement pt_app_get_caret_movement(const pt_app *app);
void pt_window_set_caret_movement(pt_window *window, pt_caret_movement movement);
pt_caret_movement pt_window_get_caret_movement(const pt_window *window);
void pt_widget_set_caret_movement(pt_widget *widget, pt_caret_movement movement);
pt_caret_movement pt_widget_get_caret_movement(const pt_widget *widget);
```

解析顺序为控件的显式值，然后是具有显式值的最近祖先，然后是窗口，然后是应用，
最后是**视觉**。`inherit` 是零值，因此零初始化的应用和控件保持 2.0 行为，默认值
就是上文描述的视觉编辑。

- **视觉**（默认）与 2.0 的移动完全一致。
- **逻辑**按存储字符串中的 UTF-8 字符边界移动左/右方向键，Home/End 前往逻辑行末。
  它仅作用于输入框和文本区域。
- **两种模式下都不变：** Up/Down/Page Up/Page Down 保持视觉目标 x，Shift+方向键
  保持固定的逻辑锚点并报告逻辑字节范围，Backspace/Delete 保持逻辑语义，单击通过
  双向映射，输入法和无障碍插入符号矩形保持视觉。
- 密码输入框在两种模式下都根据存储文本派生圆点方向，因此该策略绝不会泄露内容的
  方向。
- setter 忽略 NULL 和越界值；getter 返回存储的显式值。变更不会使布局失效，也不会
  重绘：它只影响下一次按键事件。

```c
/* A source editor inside an RTL shell: logical arrows for the editor, the
 * visual default everywhere else. */
pt_app_set_locale(app, "he_IL.UTF-8");      /* application fallback: RTL */
pt_widget_set_caret_movement(editor, pt_caret_movement_logical);
```

## 区域设置提示（2.1）

`pt_locale_ui_direction(const char *locale)` 把 BCP-47 或 POSIX 区域设置名称映射为
默认界面方向。它是有界的、不依赖 ICU 的查找：解析在 `.`（代码集）和 `@`（修饰符）
处停止，剩余标签按 `-` 或 `_` 拆分，ASCII 转为小写，最多检查 64 字节。语言和可选的
四字母脚本子标签会与一份保守的表匹配。

- RTL 脚本：`Arab`、`Hebr`、`Thaa`、`Nkoo`、`Syrc`、`Samr`、`Mand`、`Mend`、
  `Adlm`、`Rohg`。
- 未给出脚本时的 RTL 语言：`ar`、`arc`、`bal`、`bgn`、`ckb`、`dv`、`fa`、`glk`、
  `he`、`iw`、`khw`、`ks`、`lrc`、`mzn`、`nqo`、`prs`、`ps`、`rhg`、`sd`、`syr`、
  `ug`、`ur`、`yi`、`ji`。
- 脚本子标签优先于语言：`az-Arab`、`ku-Arab`、`pa-Arab` 和 `ms-Arab` 为 RTL，
  而纯 `az`、`ku`、`pa` 和 `ms` 为 LTR。
- 其他一切——包括 NULL、空字符串、`C` 和 `POSIX`——都是 LTR。

`pt_app_set_locale` 把字符串复制为提示，`pt_app_get_locale` 借用它直到下一次设置或
`pt_app_destroy`；NULL 或空字符串清除提示。解析链变为控件 → 最近显式祖先 → 窗口 →
**应用显式值 → 应用区域设置提示** → LTR，因此显式的 `pt_app_set_ui_direction`
无论调用顺序如何都优先。该辅助函数线程安全且从不读取环境：应用传入它已经解析好的
区域设置。未设置区域设置时解析为 LTR，与 2.0 完全相同。

```c
pt_app_set_locale(app, "ar_EG.UTF-8");        /* app fallback RTL */
pt_ui_direction guess = pt_locale_ui_direction("az-Arab"); /* rtl: script wins */
```

## 双向控制字符安全（2.1）

双向控制字符可以在不改变存储字节的情况下重排读者看到的内容，这就是 Trojan Source
一类攻击（CVE-2021-42574）。Pt 为整形把控制符保留在存储字符串中，并在绘制时跳过；
它绝不会静默移除它们。2.1 在新的公共头 `include/pt/bidi.h` 中提供应用所需的基础
原语：

```c
typedef enum pt_bidi_control_kind {
    pt_bidi_control_none = 0,
    pt_bidi_control_lre,  pt_bidi_control_rle,  pt_bidi_control_pdf,
    pt_bidi_control_lro,  pt_bidi_control_rlo,  pt_bidi_control_lri,
    pt_bidi_control_rli,  pt_bidi_control_fsi,  pt_bidi_control_pdi
} pt_bidi_control_kind;

typedef struct pt_bidi_control_span {
    size_t offset;                    /* UTF-8 byte offset in the input */
    size_t length;                    /* bytes occupied (2 or 3) */
    pt_bidi_control_kind kind;
    uint32_t codepoint;
} pt_bidi_control_span;

typedef enum pt_bidi_sanitize_mode {
    pt_bidi_sanitize_strip = 0,       /* remove the controls */
    pt_bidi_sanitize_neutralize       /* replace each with U+FFFD */
} pt_bidi_sanitize_mode;

size_t pt_bidi_control_inventory(const char *text, size_t length,
                                 pt_bidi_control_span *spans, size_t capacity);
char *pt_bidi_sanitize(const char *text, size_t length,
                       pt_bidi_sanitize_mode mode, size_t *out_length);
```

- 清单报告**九个显式方向格式控制符**：U+202A–U+202E（`LRE`、`RLE`、`PDF`、`LRO`、
  `RLO`）和 U+2066–U+2069（`LRI`、`RLI`、`FSI`、`PDI`）。匹配是逐字节精确的，
  因此可在非法 UTF-8 上工作，不分配内存，也不会被续字节混淆。返回值始终是总数；
  当 `spans` 非 NULL 时，最多写入 `capacity` 个条目，按逻辑顺序，因此可以先用一次
  仅计数的调用确定缓冲大小。
- 清理函数返回新分配的以 NUL 结尾的副本，由调用方用 `pt_free` 释放；`out_length`
  接收字节长度。`strip` 移除控制符，`neutralize` 把每个替换为 U+FFFD，让读者看到
  有内容被移除。最坏情况为 `3 * length + 1` 字节。其他所有字节（包括非法 UTF-8）
  原样复制。
- 两者都是**可选**的。没有任何文本 setter、输入框、文本区域、剪贴板、拖动或无障碍
  路径调用它们，因此存储字节和每个字节偏移都保持应用提供时的原样。接受不可信文本的
  应用——聊天输入、日志查看器、文件预览、任何会进入源码管理的内容——应在显示、
  记日志或存储之前调用清单或清理。
- 公共清单按设计比内部 `pt_bidi_is_removed` 范围更窄，后者保持其更广的 X9 含义
  （显式控制符加 BN 字符，如 ZWSP、软连字符、ZWJ/ZWNJ 和 BOM）作为绘制和整形谓词。
  零宽 BN 字符对公共 API 而言是普通文本，不是 Trojan Source 控制符。
- `pt_bidi_sanitize` 是缓解措施，不是校验器：它不检查其余 UTF-8，也不承诺剩余文本
  是安全的。

```c
/* Untrusted text from the network or a file. */
size_t clean_length = 0;
char *shown = pt_bidi_sanitize(untrusted, untrusted_length,
                               pt_bidi_sanitize_neutralize, &clean_length);
if (shown) {
    pt_widget_set_text(preview, shown);   /* U+FFFD marks every removed control */
    pt_free(shown);
}
/* Audit instead of rewrite: count first, then decide. */
size_t controls = pt_bidi_control_inventory(untrusted, untrusted_length, NULL, 0);
```

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

2.0 的方向函数封装于 C++、Python 和 Java，2.1 的表面在同一次变更中封装：功能
对等闸门对每种语言报告 **576/576**，允许列表为空。封装层是普通的值
setter/getter；C++ 的 setter 返回构建器，Python 暴露属性加 `set_*` 方法，Java
以 `int` 接收 C 枚举值。纯 `pt_locale_ui_direction` 辅助函数、
`pt_bidi_control_inventory` 和 `pt_bidi_sanitize` 按各语言惯例暴露：C++ 返回
`std::vector<pt_bidi_control_span>` 和 `std::string`，Python 返回文本段列表和
`str`，Java 返回 `BidiControlSpan` 数组和 `String`。

C 示例画廊包含一个 RTL 屏幕（`examples/c/rtl_gallery.c`），其中有镜像行、
阿拉伯文/希伯来文/拉丁文混排标签、RTL 输入框、RTL 文本区域、嵌套 LTR 面板和
方向切换。`examples/cpp/rtl_direction.cpp`、
`examples/python/rtl_direction.py` 和 `examples/java/RtlDirection.java` 是更小的
切换示例，用于演练绑定层的 setter 和 getter。2.1 人体工学画廊
（`examples/c/ergonomics_gallery.c`）切换插入符号策略、设置区域设置提示、用逻辑
起始/结束内边距为卡片设置内边距并清理不可信字符串；各绑定有更小的示范：
`examples/cpp/ergonomics.cpp`、`examples/python/ergonomics.py` 和
`examples/java/Ergonomics.java`。

## 兼容性与推出

双向文本在 API 层面是追加式的，但对于已经在屏幕上放置 RTL 文本的应用程序，
会改变像素。这是渲染修正，而不是新的布局契约。2.0 保持 SONAME `libpt.so.1`：
Makefile 将 `ABI_MAJOR` 与 `VERSION` 分开跟踪，方向 API 只新增声明，不改变任何
现有声明。`tools/release-check.sh` 将 SONAME 与 `ABI_MAJOR` 比较，因此仅行为
变化的主版本不会被误报为二进制破坏。

两道硬门保护 LTR：

- `test_visuals` 中 17 张纯 LTR PNG 快照与其 1.4 输出逐字节一致。
- `tools/ltr-compat.sh` 在同一台机器上构建 1.4.0 和 2.0 树，并逐字节比较 11 个
  LTR 场景。发布闸门要求 11/11 `SAME`。不签入 golden hash，因为 FreeType、
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

- 表格水平滚轮滚动是**设备相关**的，而不是不支持的。`wl_pointer` 协议从 v1 起就有
  `horizontal_scroll`（轴值 1），2.2 已把它路由到表格（delta × 48，RTL 下取反）。
  真实设备是否发出取决于硬件：倾斜滚轮和触控板横滑会发出，普通垂直滚轮不会。
  无论硬件如何，滚动条和键盘都照常可用。
- Shift+方向键跨软换行已在 **2.2 解决**：RTL 下每按一次跨一个视觉行，跨多个映射行的
  选择不再在映射边界处向逻辑边缘折叠。绘制的矩形与扩展后的范围一致。
- `pt_text_span` 没有方向字段并保持冻结。段落方向来自控件，嵌入式反向短语在
  文本中使用 Unicode 隔离控制字符。
- 阿拉伯文和希伯来文需要覆盖它们的字体。缺失字形是记录在案的字体回退/豆腐块
  结果，由 `pt_app_font_uncovered_count` 报告，而不是硬性配置错误；为示例安装
  Noto Sans Arabic/Hebrew 或 DejaVu Sans。
- 双向控制字符可以重排读者看到的内容，这属于 Trojan Source 一类问题。2.1 为此
  提供 `pt_bidi_control_inventory` 和 `pt_bidi_sanitize`，但清理仍是可选的：没有
  任何文本路径调用其中任一函数，因此接受不可信文本的应用仍须在显示、记日志或存储
  之前自行调用。
- 区域设置表按设计保持小而来自 CLDR。缺失的语言或脚本解析为 LTR，且该提示只选择
  默认 UI 方向；它不会本地化文本、日期、数字、选择器或颜色字段。

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
- **LTR 兼容性。** 17 张快照 golden 和 `tools/ltr-compat.sh` 的 11 场景跨版本
  比较是发布闸门，而不是可选检查。
- **2.1 人体工学与安全。** `test_direction` 覆盖插入符号策略链、区域设置表、
  脚本优先用例以及两种调用顺序下的应用优先级；`test_editing` 和 `test_text_area`
  在同一段 RTL 文本上断言视觉与逻辑移动，并保留每一个 2.0 断言；`test_bidi` 覆盖
  九个控制符、偏移、容量截断、非法 UTF-8、剥离与替换，以及 `pt_widget_set_text`
  原样存储控制符；布局、flex/grid 和视觉套件中的逻辑内边距用例覆盖 LTR
  start→left、RTL start→right、设置后的方向变化、测量/布局一致、
  last-call-wins 以及表格/树退出选项。

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

2.1 的人体工学与安全工作流记录在 [m23-design.md](../m23-design.md) 和
[路线图](../ROADMAP.md) 中：

1. 插入符号移动策略与区域设置提示（W1）。
2. 新的 `pt/bidi.h` 中的双向控制符清单与清理（W2）。
3. 带布局前置过程的逻辑起始/结束内边距（W3）。
4. 本文档、其中文对照、四语言示例和生成的参考（W4）。