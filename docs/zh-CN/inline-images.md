# 文本中的内联图片

[English](../inline-images.md) | 简体中文

状态：2.3.0 已交付。标签和文本区域可以在 UTF-8 文本中放置带引用计数的位图，
而无需改变文本模型、字节偏移或双向文本管线。本文档描述
`include/pt/base.h` 与 `include/pt/widget.h` 中已冻结的公共契约；
[m25-design.md](../m25-design.md) 记录设计输入，
[accessibility.md](accessibility.md) 记录辅助技术契约。

相关阅读：[rtl.md](rtl.md) 解释 U+FFFC 为何表现为一个双向字符，
[bindings.md](bindings.md) 介绍各语言绑定类型，
[architecture.md](architecture.md) 说明文本段管线在工具包中的位置。

## 保证与契约

- 控件 UTF-8 文本中的 **U+FFFC 对象替换符**就是占位符。它按 Unicode 的定义
  编码为三个字节 `EF BF BC`。调用方可以直接写入占位符，也可以让
  `pt_text_area_insert_image` 在插入符号处插入一个。
- 图片本身存放在以该占位符字节偏移为键的**有序侧表**中。一个占位符最多对应
  一个描述符。
- 文本是**唯一事实源**。`pt_widget_get_text` 返回原始的 `EF BF BC` 字节，
  所有公共偏移仍然是 UTF-8 字节偏移，添加图片永远不会移动已有偏移。
- U+FFFC 是一个 Unicode 码点，也是一个原子簇。选择、插入符号移动、删除和
  UTF-8 步进都不会进入这三个字节的中间。U+FFFC 的双向类别是 ON，因此
  Unicode 双向算法把它放进周围的嵌入层级；视觉顺序仍然只属于呈现层。
- 图片在**描述符层面**选择启用。文本里有 U+FFFC 但没有描述符时，行为与
  2.2.0 相同：占位符像其他未覆盖的码点一样被整形。从不设置图片的控件走与
  以前相同的布局、测量和绘制路径。
- 行的上沿与下沿会扩展到足以容纳图片框；在 2.3.0 中，一张很高的图片会像
  大号样式字体文本段一样抬高控件内每一行的度量。逐行度量留待以后。
- API 是追加式的。`pt_text_span`、现有 ABI 和 SONAME `libpt.so.1` 均不改变。

## 模型

```c
typedef struct pt_text_image {
    uint32_t start;              /* byte offset of EF BF BC */
    struct pt_icon_image *image; /* reference-counted handle */
    float width;                 /* logical pixels, <= 0 means intrinsic */
    float height;
    pt_text_image_align align;
    char alt[128];               /* copied, truncated to 127 bytes */
} pt_text_image;
```

`start` 总是针对当前文本解析。setter 会复制描述符数组，逐项校验，按 `start`
排序，每个占位符只保留一项，并对每个非 NULL 图片取一次引用。普通的文本编辑
会同步维护侧表：在占位符之前插入会把它的 `start` 向后移，删除范围与占位符
相交会移除描述符，删除发生在它之前则把偏移调小。`pt_widget_set_text` 会清空
整张表，就像它清空样式文本段一样。

## 无障碍（2.3.0）

无障碍树把每张图片暴露为所属标签或文本区域的**虚拟子项**，顺序与侧表索引
一致。子项的角色是 `image`（AT-SPI 角色 27），名称取 `alt` 文本；`alt` 为空
时回退到图片描述。子项是真正的 `Component`：`GetExtents`、`GetPosition` 和
`GetSize` 返回占位符的屏幕矩形，`GetAccessibleAtPoint` 会把落在该矩形内的点
解析为图片子项而不是所属文本控件。Text 接口仍然返回原始 U+FFFC 和不变的字符
偏移，因此不认识嵌入对象的应用或辅助技术仍能看到位置正确的占位符。设置、
清空或编辑占位符会发出 structure-changed 无障碍事件；常规的 text-changed
事件仍报告字节变化。

## 快速上手

```c
#include <pt/pt.h>

#include <stdio.h>

/* U+FFFC is the three bytes EF BF BC. Each placeholder owns one descriptor. */
static const char status_text[] =
    "Build: \xEF\xBF\xBC passed \xEF\xBF\xBC pending";

int main(void) {
    pt_app *app = pt_app_create("dev.pt.inline_images");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Inline images", 640, 360);
    pt_widget *area = pt_text_area_create(pt_window_root(window), "Status");
    pt_widget_set_text(area, status_text);

    pt_icon_image *ok = pt_icon_image_from_name("emblem-ok-symbolic", 24);
    pt_icon_image *clock = pt_icon_image_from_name("appointment-soon-symbolic", 24);

    /* start is the byte offset of each placeholder in status_text. */
    pt_text_image images[2] = {
        {.start = 7, .image = ok, .width = 20, .height = 20,
         .align = pt_text_image_align_baseline, .alt = "Passed"},
        {.start = 18, .image = clock, .width = 24, .height = 24,
         .align = pt_text_image_align_middle, .alt = "Pending"},
    };
    pt_widget_set_text_images(area, images, 2);

    /* The widget holds its own references; the caller still owns these. */
    pt_icon_image_destroy(ok);
    pt_icon_image_destroy(clock);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

setter 会丢弃图片句柄为 NULL 的项，因此主题查找失败只会让占位符保持未装饰
状态，而不会让整次调用失败。`pt_text_image` 可以零初始化：`align` 为零表示
baseline，`width` 和 `height` 为零表示使用图标内在尺寸，图片为 NULL 表示
“丢弃这一项”。

## 所有权与生命周期

- `pt_widget_set_text_images` 复制描述符数组，并对每个非 NULL 图片取自己的
  引用。传入 NULL 或数量为零等价于 `pt_widget_clear_text_images`。
- 成功设置会释放上一张表的引用。旧描述符数组不能用来恢复旧图片，因为那些
  句柄可能已经被释放。如果调用方需要重建之前的表，应自己保留显式引用。
- `pt_widget_get_text_image` 复制一个描述符。返回的 `image` 句柄是**借出**的：
  仅在侧表存活且该项没有被替换或清除时有效。需要更长的生命周期时，调用
  `pt_icon_image_ref` 增加引用。
- `pt_text_area_insert_image` 忽略 `image->start`，对 `image->image` 取自己的
  引用，并记录文本改动。调用成功后，调用方可以立即销毁自己的句柄。
- 替换文本或任何移除占位符的编辑都会释放对应图片。控件销毁时释放侧表中剩余
  的引用。

```c
pt_text_image borrowed;
if (pt_widget_get_text_image(area, 0, &borrowed)) {
    /* borrowed.image is owned by the widget and not by this scope. */
    pt_icon_image *kept = pt_icon_image_ref(borrowed.image);
    /* Use kept while the side table may change, then drop the extra ref. */
    pt_icon_image_destroy(kept);
}

/* Clearing releases every descriptor and its reference. */
pt_widget_clear_text_images(area);
```

## 尺寸与对齐

`width` 和 `height` 使用逻辑像素。值 `<= 0` 或非有限值表示图标内在尺寸。
如果恰好一个维度为正，另一个维度保持图标的宽高比；如果两个都为正，图片就
按这个框绘制。位图请求受现有图标缓存（`PT_ICON_CACHE_MAX_PIXELS`）约束：
无法容纳的请求不会绘制任何内容，与尺寸过大的按钮图标一致。

| 取值 | 几何位置 | 上沿贡献 | 下沿贡献 |
| --- | --- | --- | --- |
| `pt_text_image_align_baseline`（0） | 底边落在文本基线上 | `height` | `0` |
| `pt_text_image_align_middle` | 垂直中心落在文本基线上 | `height / 2` | `height / 2` |

基线对齐是默认值，适合落在文本行上的状态图标。居中对齐以基线为中心，向行上
和行下扩展，适合徽标或头像。绘制时使用 `bottom = baseline + descent`，并裁剪
到控件范围；因此比内容框更宽的图片会独占一行，超出行的部分被裁剪而不会缩放。

## 替代文本

`alt` 在设置时复制进描述符。字段最多容纳 127 字节加结尾 NUL；更长的输入会在
UTF-8 序列边界处截断，保证存储的字符串始终是合法 UTF-8 且以 NUL 结尾。
复制选择时以及无障碍树中，替代文本就是 U+FFFC 的替身。`alt` 为空时，复制
选择仍保留 U+FFFC，无障碍名称则回退到图片描述。

## 在插入符号处插入

`pt_text_area_insert_image` 是编辑入口。它在插入符号处插入一个 U+FFFC，替换
当前选择，并把描述符附加到新的占位符上。

```c
pt_icon_image *spinner = pt_icon_image_from_name("process-working-symbolic", 24);
pt_text_image image = {
    .image = spinner,
    .width = 24,
    .height = 24,
    .align = pt_text_image_align_middle,
    .alt = "Working",
};

/* Place the caret at a UTF-8 boundary, then insert. */
pt_text_area_set_selection(area, 12, 12);
uint32_t start = 0;
if (!pt_text_area_insert_image(area, &image, &start)) {
    /* Not a text area, NULL image, caret inside U+FFFC, or byte limit. */
}
pt_icon_image_destroy(spinner);

/* Undo and redo restore the placeholder and its descriptor together. */
pt_text_area_undo(area);
```

当控件为 NULL 或不是文本区域、图片或其句柄为 NULL、任一选择端点不是 UTF-8
边界（包括已有占位符的内部，因此对象永远不会被拆开），或插入会超过文本字节
上限时，调用返回 false，文本和侧表都不改变。完全覆盖已有占位符的选择会连同
其字节一起移除旧描述符，新占位符的描述符取而代之。成功时，如果 `start` 非
NULL，新占位符的字节偏移会写入其中。

## 校验规则

- 每个 `start` 都必须指向当前文本中完整的 `EF BF BC` 序列。偏移等于或超过
  文本长度、只有部分序列，或指向其他码点时，该项被丢弃。
- 图片句柄为 NULL 的项在重复项解析之前就被丢弃，因此它不会清除同一占位符上
  已有的有效描述符。
- 多个项指向同一占位符时，最后输入的项胜出，与样式文本段 setter 一致。
- 存储后的表按 `start` 排序；因此 getter 索引遵循文本顺序。
- 描述符数组会被复制，调用方在调用后可以释放输入数组。唯一按值借出的字段是
  图片句柄。
- `pt_widget_set_text` 会清空侧表。使占位符失效的编辑会把侧表收窄到新文本，
  并释放受影响的引用。

## 复制与剪切（2.3.0）

复制选择时，每个被完整包含的 U+FFFC 都会以替代文本写入。没有替代文本的项
保留三个占位符字节。起点或终点落在占位符内部的部分选择会在复制前向外扩展到
对象边界，因此对象要么整体复制，要么完全不复制。剪切会移除原始占位符及其
描述符；剪贴板收到的是纯文本。剪切或通过无障碍 `EditableText` 接口删除时，
都会走同一套文本删除路径，因此只丢弃与删除字节相交的描述符，整次删除是一个
可撤销的步骤。粘贴图片，或用富剪贴板类型重建描述符，不属于 2.3.0 的范围。

## 绑定

C++ 包装提供 `set_text_images`、`clear_text_images`、`text_image_count`、
`text_image`（返回 `std::optional<pt_text_image>`）和 `insert_image`
（返回 `std::optional<uint32_t>`）。Python 提供同名方法和 `TextImage`、
`text_image_align_*`；Java 提供 `Pt.TextImage`、`Pt.widget_set_text_images`、
`Pt.widget_get_text_image` 和 `Pt.text_area_insert_image`，插入被拒绝时返回
`-1`。包装类型遵循与 C 句柄相同的所有权规则。

## 示例

- [C 内联图片](../../examples/c/inline_images.c)
- [C++ 内联图片](../../examples/cpp/inline_images.cpp)
- [Python 内联图片](../../examples/python/inline_images.py)
- [Java 内联图片](../../examples/java/InlineImages.java)

每个示例都内嵌一张小 PNG，设置两个对齐方式和替代文本不同的占位符，在插入
符号处插入第三张图片，并检查侧表。设置 `PT_HEADLESS=1` 后，无需合成器即可
运行模型检查和一小段事件循环；示例从不断言渲染像素。

## 兼容性与限制

- 作为追加式 API 在 2.3.0 交付。现有结构布局、枚举值和函数签名都不改变；
  `ABI_MAJOR` 保持 1，SONAME 保持 `libpt.so.1`。
- 没有描述符的控件逐字节保持 2.2.0 的文本路径。没有描述符的占位符会像未
  覆盖的码点一样被整形，与以前完全一致。
- 2.3.0 为整个控件合成一个行高，因此很高的图片会抬高每一行。逐行度量尚未
  实现。
- 图片是单个逻辑框：暂不支持对部分可见文本段做分数缩放，也不支持粘贴图片和
  富剪贴板 MIME 类型。
- 图片句柄是位图图标（`pt_icon_image`）；矢量文本段和动画图片不属于此功能。