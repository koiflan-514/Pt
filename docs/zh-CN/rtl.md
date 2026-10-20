# 从右到左与双向文本

[English](../rtl.md) | 简体中文

状态：1.0 之后的计划。本文档中的任何内容都不属于 1.0.0 公共表面。1.0 的
限制是有意为之：文本模型、布局过程和无障碍偏移已经公开，无法在 1.x 版本
中重新设计而不破坏每个存储或索引文本的应用程序。本文档在这项工作开始之前
确定设计方向和测试义务。

相关阅读：[architecture.md](architecture.md) 描述了当前的布局和绘制过程，
[accessibility.md](accessibility.md) 定义了辅助技术使用的字符偏移契约，
[api-policy.md](../api-policy.md) 规定了本文描述的追加式 API 如何在 1.0
之后落地。

## 1.0 保证的内容

- 公共文本 setter、getter、文本段和选择使用 UTF-8 字节。无障碍 getter 使用
  Unicode 标量值偏移。添加双向文本时这些单位不会改变；方向是呈现属性。
- HarfBuzz 将一行整形为一个文本段。它会猜测该文本段的文字系统和方向，但 Pt
  不应用 Unicode 双向算法（UBA），不重排序混合方向的文本段，也不隔离嵌入的
  方向。换行和插入符号计算将行视为从左到右。
- `pt_text_align_start` 和 `pt_text_align_end` 解析为左和右。
  `pt_align_start`/`pt_align_end` 和 `pt_justify_start`/`pt_justify_end` 在布局中
  做同样的事。容器行从第一个子项到最后一个子项构建，从左到右。
- 标签、按钮、输入框和文本区域都使用相同的单文本段绘制路径。密码输入框在
  无障碍桥接层中隐藏其文本，但这与方向无关。
- 没有公共方向属性、没有区域设置查询，也没有镜像图标 API。在 1.0 中，
  应用程序无法要求 Pt 布局 RTL 界面。

## 术语

- **逻辑顺序**是 UTF-8 字符串和选择模型中字符的顺序。文本显示时它永远不会
  改变。
- **视觉顺序**是 UBA 分配嵌入层级并重排序之后，一行上字形文本段的顺序。
- **文本方向**控制文本的段落排序、字形整形和起始/结束对齐。
- **UI 方向**控制行子项的顺序、起始/结束对齐、内边距和外边距的解释、滚动条
  位置，以及可镜像的界面元素，例如前进/后退箭头和排序箭头。
- **自动**文本方向根据段落的第一个强字符解析每个段落，如 UBA 规则 P2/P3
  所规定。空段落或仅含中性字符的段落继承控件解析后的文本方向。

文本方向和 UI 方向是分开的。英文表单中的希伯来语句子很常见，而 RTL 应用
程序中的嵌入式代码编辑器必须能够保持从左到右，而不翻转其周围的布局。

## 提议的公共形态

以下名称是设计草图，而不是声明。它们必须通过 [API 政策](../api-policy.md)
审查，并在同一次变更中添加到所有三个绑定。选择这些名称是为了避开现有的
`pt_direction`，后者是行/列布局轴。

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

/* Proposed, additive after 1.0. */
void pt_app_set_ui_direction(pt_app *app, pt_ui_direction direction);
pt_ui_direction pt_app_get_ui_direction(const pt_app *app);
void pt_window_set_ui_direction(pt_window *window, pt_ui_direction direction);
pt_ui_direction pt_window_get_ui_direction(const pt_window *window);
void pt_widget_set_ui_direction(pt_widget *widget, pt_ui_direction direction);
pt_ui_direction pt_widget_get_ui_direction(const pt_widget *widget);
void pt_widget_set_text_direction(pt_widget *widget, pt_text_direction direction);
pt_text_direction pt_widget_get_text_direction(const pt_widget *widget);
```

解析顺序为控件的显式值，然后是具有显式值的最近祖先，然后是窗口，然后是
应用。编译期默认值为 `pt_ui_direction_ltr` 和 `pt_text_direction_auto`；因此
`inherit` 是零值，并使现有代码保持不变。想要 RTL 外壳的应用程序在应用或
窗口上设置一次 UI 方向，嵌套编辑器则设置
`pt_widget_set_ui_direction(editor, pt_ui_direction_ltr)`。

首个版本不会从区域设置猜测 UI 方向。1.0 没有区域设置 API，`LANG` 解析在
不同桌面和启动环境之间有所不同，错误的猜测会镜像每个窗口。后续的追加式
步骤可能会添加显式的 `pt_app_set_locale` 或启动辅助函数，但它必须保留应用
程序覆盖，并且必须记录为行为变更。自动文本方向是安全的，因为它按段落应用，
并且不会移动界面元素。

## 双向文本引擎

段落算法是无法用反转字符串循环近似的那部分。它需要完整的 UBA：显式嵌入和
覆盖、隔离、弱字符和中性字符解析、配对括号、隐式层级，以及换行后的行级
规则（尤其是规则 L1）。

实现选择是两条经过审查的路径之一：

1. **将 FriBidi 作为构建依赖。** 它实现 UBA 和镜像，被广泛打包，并为项目
   提供经过测试的参考实现。许可证为 LGPL-2.1-or-later；动态链接使 Pt 自身
   的代码保持在其 Apache-2.0 条款下。依赖成本和分发政策必须在代码依赖它
   之前，在 [THIRD_PARTY.md](../../THIRD_PARTY.md)、pkg-config 文件和发布
   说明中确定。
2. **随源码附带且经过测试的 UBA 核心。** 一个与 Pt 具有相同宽松许可证的小型
   实现可以避免新的共享库依赖，但项目随后将拥有 Unicode 一致性测试套件，并
   负责未来每次 Unicode 版本升级。只有在 CI 中运行生成的 `BidiTest.txt` 和
   `BidiCharacterTest.txt` 一致性测试时，这才可接受。

两种选择都会产生相同的内部形态：

```c
typedef struct pt_bidi_run {
    size_t start;        /* logical UTF-8 byte offset */
    size_t end;
    size_t visual_start; /* glyph slot after reordering */
    uint8_t level;
    bool rtl;
} pt_bidi_run;
```

对每个段落：

1. 去除或遵循控件的显式方向。对于自动，在逻辑文本上运行 P2/P3。
2. 解析嵌入层级，并对不得绘制的控制字符应用规则 X9 的移除/恢复语义。
3. 按层级重排序文本段。仅在视觉过程中镜像配对字符（圆括号、方括号、角
   引号和 Unicode 镜像表）；存储的字符串和偏移保持逻辑顺序。
4. 在文字系统和字体边界处拆分文本段，然后使用显式的 `direction`、`script`
   和 `language` 通过 HarfBuzz 整形每个文本段。绝不要让
   `hb_buffer_guess_segment_properties` 决定 RTL 文本段的方向。
5. 在逻辑字节偏移和视觉 x 位置之间维护双向映射。测量、换行、插入符号
   放置、命中测试、选择绘制和省略号都使用该映射，而不是假定 x 随字节偏移
   增长。

换行按逻辑顺序进行，生成候选行，然后对每一行重排序并测量。断行器必须
理解行末的 UBA L1 空白。首个实现可以保留 Pt 当前仅按空白断行的做法；完整
的 UAX #14 断行是一个独立的质量改进步骤，不得静默改变逻辑偏移。

## 布局与绘制

- 当控件使用自动方向时，`pt_text_align_start` 和 `pt_text_align_end` 根据
  段落方向解析。`pt_text_align_center` 不变。多段落文本区域可以将不同段落
  对齐到相反方向；插入符号和选择仍是一个逻辑范围。
- 具有 RTL UI 方向的容器从右到左布局行子项，将 `pt_align_start` 映射到右
  边缘，将 `pt_justify_start` 映射到右边缘，并镜像起始/结束内边距和外边距的
  含义。物理左/右值对于需要它们的应用程序仍然是物理的。
- 滚动条、滚动阴影和过滚动应位于行内起始侧或行内结束侧，而不是无条件地位
  于右侧。表格和树的表头保持逻辑列顺序；只有水平起始边缘改变。
- 可镜像字形（前进/后退、下一个/上一个、排序箭头、展开箭头及类似界面元素）
  由绘制代码根据解析后的 UI 方向选择。用户提供的图像绝不会自动镜像；未来
  的图像 API 可以暴露显式镜像标志。
- 省略号和裁剪留在行的视觉末端。被截断的 RTL 行不得仅仅因为逻辑第一个字符
  绘制在右侧就将其切掉。
- 字体回退和双向文本是相邻的两个问题。混合拉丁文、阿拉伯文和希伯来文的段落
  除了方向之外，还需要按文本段选择字体族；现有的字体回退工作必须在 RTL
  被宣布为完整之前落地。在此之前，应用程序可以选择覆盖其文字系统的字体族。

## 编辑、选择与命中测试

- `pt_widget_get_text`、`pt_widget_set_text`、`pt_widget_get_text_span` 和选择
  API 保持其字节单位。可以在具有不同嵌入层级的文本段之间进行选择，而无需
  重排序存储的字节。
- 方向键按逻辑字符偏移移动，但将插入符号绘制在相应的视觉边界处。左/右
  方向键遵循当前文本段的视觉方向，除非平台惯例要求逻辑移动；Home/End 映射
  到视觉行末端。所选行为必须针对真实键盘布局进行测试，并记录在
  `pt/widget.h` 中。
- 鼠标单击通过双向映射将视觉 x 映射到最近的逻辑边界。文本段边界处的插入
  符号必须绘制在边界的正确一侧，跨文本段拖动会绘制具有多个矩形的选择，而
  不是一个反向区间。
- 发送给 `text-input-v3` 的插入符号矩形是逻辑插入符号处的视觉矩形。周围
  文本和删除周围文本保持逻辑字节。输入法预编辑按插入上下文的方向整形。
- `pt_text_layout_*` 中的坐标是内部的；如果其中任何坐标变为公共，它们必须
  说明是逻辑还是视觉。在此计划中它们是逻辑的。

## 无障碍

- AT-SPI `Text` 方法继续按逻辑顺序报告字符偏移。屏幕阅读器读取逻辑字符串，
  而不是视觉重排序后的结果。
- 桥接层通过现有属性路径将段落方向发布为文本属性（例如
  `direction:rtl`），并保持 `GetText`、`GetCharacterAtOffset`、
  `GetStringAtOffset` 和选择与逻辑偏移一致。
- 无障碍树保持相同的角色和名称。方向是呈现元数据；它永远不会改变角色或
  状态。
- 密码文本在每个方向下都保持隐藏。双向文本不得创建通过
  `GetCharacterAtOffset`、属性或变更事件暴露密码字符的路径。

## 绑定与示例

所有提议的函数都是普通的值 setter/getter，因此 C++ 包装器、Python ctypes 类
和 Java FFM 包装器遵循现有的生成绑定模式。一致性审计是硬性门禁：在所有
四个表面报告相同的数量、并且 C++ 的 `[[nodiscard]]`/错误约定匹配之前，API
不会被合并。

C 示例画廊获得一个 RTL 屏幕，其中包含 RTL 行、混合方向标签、包含希伯来文和
拉丁文的文本区域，以及嵌套的 LTR 面板。C++、Python 和 Java 示例各自获得
一个较小的方向切换示例，因此绑定不仅被编译，而且被实际演练。

## 兼容性与推出

双向文本在 API 层面是追加式的，但对于已经在屏幕上放置 RTL 文本的应用
程序，可能会改变像素。这是渲染修正，而不是新的布局契约，发布说明必须说明
这一点。现有 LTR 应用程序在 `pt_ui_direction_ltr` 和
`pt_text_direction_ltr` 下必须逐字节稳定。

公共字节偏移和无障碍字符偏移契约不会改变。现有枚举值不变。
`pt_text_direction_auto` 和 `pt_ui_direction_ltr` 是零值，因此零初始化的
控件保持今天的行为。未来的主要版本可能会改变 UI 自动检测，但 1.x 版本不会。

## 测试计划

- **UBA 一致性。** 连同其许可证导入 Unicode `BidiTest.txt` 和
  `BidiCharacterTest.txt` 数据，并在无头测试中运行每个适用的行。在测试输出
  和 `THIRD_PARTY.md` 中保留 Unicode 版本。
- **段落用例。** 覆盖首个强字符检测、空段落和仅含中性字符的段落、RTL 文本
  周围的数字、隔离和覆盖、配对括号、组合标记、希伯来文/阿拉伯文与拉丁文
  混排，以及 RTL 散文中的嵌入式 LTR 代码。
- **布局用例。** 按词换行、行末空白、省略号、居中/起始/结束对齐、混合文字
  系统的行，以及在分数缩放下同时包含 LTR 和 RTL 文本段的行。
- **编辑用例。** 方向键、Home/End、Shift+方向键、鼠标单击和拖动、双击选词、
  文本段边界处的插入符号、跨段落选择，以及 RTL 文本段内的输入法预编辑。
  断言存储的字节偏移和绘制的插入符号矩形。
- **无障碍用例。** 对逻辑 `GetText`、`GetCharacterAtOffset`、
  `GetStringAtOffset`、选择和方向属性进行实时 AT-SPI 检查。在两种方向下
  演练密码输入框，并断言没有字符被暴露。
- **镜像用例。** RTL 行顺序、起始/结束内边距、滚动条一侧、前进/后退和排序
  字形，以及 RTL 窗口内的嵌套 LTR 面板。
- **兼容性用例。** 在强制 LTR 下运行现有的 LTR 截图和属性套件，并与 1.0
  基线比较。LTR 快速路径不得使当前的守卫计时出现回退。
- **实时合成器。** 向 `test_wayland` 添加一个 RTL 窗口，并断言在真实单击和
  焦点变化之后，通过 `text-input-v3` 发送的插入符号矩形与绘制的选择一致。

## 性能

LTR 路径是常见路径，并获得快速路径：如果每个段落都解析为 LTR 且不包含
双向控制字符，则跳过 UBA 并使用现有的单文本段整形。双向文本结果按行缓存，
以文本、方向、字体和宽度为键，像当前 HarfBuzz 整形缓存一样有界。守卫套件
获得一个混合文字系统的密集段落和一个 RTL 表格行性能剖析场景；LTR 剖析场景
的性能退化会像双向文本剖析失败一样阻止该变更。

文本段、层级以及逻辑/视觉映射的额外分配必须受行长度限制，并随文本布局
释放。未更改的行不允许逐帧分配。

## 交付顺序

1. 方向属性、解析和自动检测；尚无视觉重排序。无头测试固定 API、继承和默认
   行为。
2. 标签和单行文本的 UBA 层级、重排序、镜像和按文本段整形。LTR 快照必须保持
   不变。
3. 换行标签和文本区域：断行、省略号、对齐以及逻辑/视觉映射。
4. 编辑：插入符号、选择绘制、命中测试、键盘移动和输入法矩形。
5. UI 镜像：行、起始/结束几何、滚动条和方向性图标。
6. 无障碍属性和逻辑偏移断言、绑定和示例。
7. 一致性、模糊测试和性能加固；只有到那时才从 1.0 限制中移除双向文本。

## 待定决策

- FriBidi 依赖与随源码附带的 UBA 核心之间的选择，包括许可证、打包和 Unicode
  更新政策。
- 方向键默认采用逻辑还是视觉插入符号移动，以及应用程序如何在不给每个控件
  新增 API 的情况下选择另一种行为。
- 样式化文本段是否可以按文本段覆盖方向。最初的答案是否定的：样式化文本段
  控制样式，而不是段落结构；这使公共 `pt_text_span` 布局保持冻结。
- 阿拉伯文和希伯来文的最低字体覆盖保证，以及缺失字形是记录在案的字体回退
  失败还是硬性配置错误。
