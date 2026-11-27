# 主题

[English](../theming.md) | 简体中文

Pt 从语义令牌表绘制，而不是从每个控件各自的字面量绘制。一个主题是 38 个
颜色角色和 10 个几何度量。应用程序在两个级别修改该表：

- **应用级：** `pt_app_set_theme_color` / `pt_app_set_theme_metric` 影响
  从该应用创建的每个窗口。
- **窗口级：** `pt_window_set_theme_color` /
  `pt_window_set_theme_metric` 为一个窗口覆盖单个角色，
  `pt_window_clear_theme_overrides` 将其恢复为应用主题。

这些枚举属于仅追加式 ABI。现有数值永不改变；新角色获得新编号。
`tests/test_abi.c` 固定这些值，[tests/test_theme.c](../../tests/test_theme.c)
固定下文引用的默认值和对比度下限。

## 解析顺序

每个内置绘制按以下顺序解析颜色：

1. 窗口对该令牌的覆盖（如果 `pt_window_set_theme_color` 设置了它）；
2. 否则是应用调色板，由 `pt_app_set_color_scheme`、
   `pt_app_set_accent_color` 或 `pt_app_set_theme_color` 更改；
3. 否则是编译期默认值。解析后的窗口始终有应用调色板，因此编译期默认值只是
   缺失窗口时的后备。

度量使用相同顺序：

1. 窗口的覆盖；
2. 应用度量；
3. 编译期默认值。

公共读取接口是 `pt_app_get_theme_color`、`pt_window_get_theme_color`、
`pt_app_get_theme_metric` 和 `pt_window_get_theme_metric`。没有公共的
按控件的令牌 getter；自定义绘制使用拥有该控件的应用或窗口 getter，或从应用
程序接收解析后的颜色作为输入。

控件仍然可以固定显式的背景或前景色；该控件对固定的角色绕过令牌表。主题
变更会请求重绘所有打开的窗口，度量变更还会使布局失效。

有两个调用容易误读：

- `pt_app_set_color_scheme(app, scheme)` 加载完整的深色或浅色调色板，
  并**替换每个应用颜色和度量覆盖**。在应用品牌颜色之前调用它，或在之后重新
  应用它们。
- `pt_app_set_accent_color(app, color)` 仅重写强调色系列（`accent`、
  `accent_hover`、`accent_pressed`、`accent_text`、`accent_soft`、
  `focus`、`selection`）。它不会更改 danger、success 或 warning 角色。推导
  取决于活动方案，因此先设置方案。

## 颜色令牌

颜色是 `0xRRGGBBAA`。alpha 字节有意义：`accent_soft`、`selection`、
柔和状态角色、`shadow`、`scrim` 和选择填充是半透明的，由渲染器合成。

| 令牌 | 深色 | 浅色 | 用途 |
| --- | --- | --- | --- |
| `pt_theme_background` | `0B0E14FF` | `F4F6FAFF` | 卡片和窗格后的窗口背景 |
| `pt_theme_surface` | `11151EFF` | `FFFFFFFF` | 默认控件和卡片表面 |
| `pt_theme_surface_raised` | `161B26FF` | `FFFFFFFF` | 菜单、对话框、凸起卡片 |
| `pt_theme_surface_hover` | `1D2431FF` | `F0F3F8FF` | 低强调表面的悬停填充 |
| `pt_theme_surface_sunken` | `080B11FF` | `EDF0F5FF` | 凹陷区域以及代码/日志背景 |
| `pt_theme_surface_container` | `141924FF` | `EDF0F5FF` | 分组内容的容器层级 |
| `pt_theme_surface_container_high` | `1A2130FF` | `E5EAF2FF` | 最高容器层级，输入控件外框 |
| `pt_theme_border` | `232B3AFF` | `E1E6EEFF` | 细线分隔符和默认轮廓 |
| `pt_theme_border_strong` | `39445AFF` | `C8D0DDFF` | 强调边框和控件边缘 |
| `pt_theme_outline` | `39445AFF` | `C8D0DDFF` | 可聚焦控件使用的语义别名 |
| `pt_theme_outline_variant` | `232B3AFF` | `E1E6EEFF` | 容器内的低调轮廓 |
| `pt_theme_text` | `E9EEF7FF` | `111827FF` | 主要文本和图标 |
| `pt_theme_text_muted` | `94A1B8FF` | `5F6B7FFF` | 次要标签、提示、占位符 |
| `pt_theme_text_disabled` | `5B6577FF` | `A0A9B8FF` | 禁用标签和非活动控件 |
| `pt_theme_selection_text` | `FFFFFFFF` | `111827FF` | 自定义不透明选择填充的前景 |
| `pt_theme_accent` | `3568E0FF` | `2559E6FF` | 主要操作和选中控件填充 |
| `pt_theme_accent_hover` | `3D6FE8FF` | `1E56E8FF` | 主要操作，指针悬停 |
| `pt_theme_accent_pressed` | `2F5BC0FF` | `1C4ECFFF` | 主要操作，按下 |
| `pt_theme_accent_text` | `FFFFFFFF` | `FFFFFFFF` | 强调色填充上的文本和字形 |
| `pt_theme_accent_soft` | `3568E033` | `2559E626` | 色调按钮和选中行底色 |
| `pt_theme_selection` | `3568E059` | `2559E640` | 表面上的文本选择底色 |
| `pt_theme_focus` | `8CB0FFFF` | `2559E6FF` | 焦点可见环和插入符号 |
| `pt_theme_control` | `1A2130FF` | `F8FAFCFF` | 输入框、切换控件和滑块静止填充 |
| `pt_theme_control_hover` | `222B3CFF` | `EFF3F9FF` | 输入框和控件悬停填充 |
| `pt_theme_control_pressed` | `2A3447FF` | `E4EAF3FF` | 控件按下填充 |
| `pt_theme_track` | `2A3344FF` | `DCE3EDFF` | 开关和滑块轨道 |
| `pt_theme_scrollbar` | `4A566EFF` | `ADB8C8FF` | 滚动条滑块 |
| `pt_theme_titlebar` | `0E121BFF` | `FBFCFEFF` | 客户端标题栏 |
| `pt_theme_danger` | `F16B75FF` | `B92C38FF` | 破坏性文本、图标和边框 |
| `pt_theme_success` | `4ACB8DFF` | `0F7A37FF` | 成功文本和图标 |
| `pt_theme_warning` | `E5B454FF` | `8A5A00FF` | 警告文本和图标 |
| `pt_theme_danger_soft` | `F16B7526` | `B92C3820` | 错误横幅或无效字段底色 |
| `pt_theme_success_soft` | `4ACB8D26` | `0F7A3720` | 成功横幅底色 |
| `pt_theme_warning_soft` | `E5B45426` | `8A5A0020` | 警告横幅底色 |
| `pt_theme_shadow` | `00000073` | `0F172A24` | 高度阴影 |
| `pt_theme_scrim` | `000000A6` | `0F172A66` | 模态遮罩 |
| `pt_theme_skeleton` | `1D2431FF` | `E7EBF1FF` | 加载占位符基础 |
| `pt_theme_skeleton_highlight` | `2A3447FF` | `F3F5F9FF` | 加载占位符微光 |

`pt_theme_selection_text` 不被内置的输入框和文本区域绘制器使用：它们绘制半透明
的 `pt_theme_selection` 底色并保持正常文本颜色，从而在两种方案上都保持
对比度。当自定义控件绘制不透明选择填充并需要匹配的前景时，使用该令牌。

## 度量

度量是逻辑像素。负值会被 setter 和解析过程钳制为零。`radius_pill` 允许
很大，因为每种形状都会将其钳制为较短边的一半。

| 度量 | 默认值 | 用途 |
| --- | ---: | --- |
| `pt_theme_metric_radius_small` | 6 | 标签片、对勾标记、小型控件 |
| `pt_theme_metric_radius_medium` | 10 | 按钮、输入框、卡片 |
| `pt_theme_metric_radius_large` | 16 | 对话框、菜单、大面板 |
| `pt_theme_metric_radius_pill` | 999 | 胶囊和开关轨道 |
| `pt_theme_metric_focus_width` | 2 | 焦点可见环描边 |
| `pt_theme_metric_focus_offset` | 2 | 控件与环之间的间隙 |
| `pt_theme_metric_elevation_low` | 8 | 卡片和细微凸起表面 |
| `pt_theme_metric_elevation_high` | 20 | 菜单、对话框、弹出框 |
| `pt_theme_metric_chip_height` | 28 | 筛选标签片高度 |
| `pt_theme_metric_control_height` | 34 | 标准按钮和输入框高度 |

字体族是应用状态，不是度量：`pt_app_set_font_family` 选择当控件未携带
显式字体时使用的字体族。

## 强调色推导

`pt_app_set_accent_color` 写入七个角色。公式位于 `src/app.c`；
`accent_text` 由 `pt_theme_contrast_text` 选择，它将强调色的相对亮度与
0.62 比较，对浅色强调色返回 `101418FF`，否则返回 `FFFFFFFF`。

| 角色 | 深色方案 | 浅色方案 |
| --- | --- | --- |
| `accent` | 提供的颜色 | 提供的颜色 |
| `accent_hover` | 提亮 14% | 混合 14% 黑色 |
| `accent_pressed` | 混合 18% 黑色 | 混合 22% 黑色 |
| `accent_text` | 对比文本 | 对比文本 |
| `focus` | 提亮 22% | 混合 6% 黑色 |
| `accent_soft` | 强调色，alpha 为 `33`（20%） | 强调色，alpha 为 `26`（15%） |
| `selection` | 强调色，alpha 为 `59`（35%） | 强调色，alpha 为 `40`（25%） |

如果直接用 `pt_app_set_theme_color` 覆盖 `accent`，推导出的角色**不会**
重新计算。请显式覆盖每个推导角色，或使用 `pt_app_set_accent_color`。

## 对比度

默认调色板由 `tests/test_theme.c` 中的 `test_contrast()` 保持在这些下限
之上。比率使用 WCAG 2.x 相对亮度公式。

| 配对 | 深色 | 浅色 | 下限 |
| --- | ---: | ---: | ---: |
| `background` 上的 `text` | 16.59 | 16.40 | 7.0（AAA） |
| `surface` 上的 `text` | 15.69 | 17.74 | 7.0 |
| `surface_raised` 上的 `text` | 14.80 | 17.74 | 7.0 |
| `background` 上的 `text_muted` | 7.40 | 4.98 | 4.5（AA） |
| `surface` 上的 `text_muted` | 7.00 | 5.39 | 4.5 |
| `accent` 上的 `accent_text` | 4.99 | 5.75 | 4.5 |
| `accent_hover` 上的 `accent_text` | 4.53 | 5.91 | 4.5 |
| `accent_pressed` 上的 `accent_text` | 6.22 | 6.91 | 4.5 |
| `surface` 上的 `danger` | 6.17 | 6.03 | 4.5 |
| `surface` 上的 `success` | 8.89 | 5.44 | 4.5 |
| `surface` 上的 `warning` | 9.56 | 5.93 | 4.5 |
| `background` 上的 `focus` | 8.99 | 5.32 | 4.5 |

由该表得出的指导：

- 对用户必须阅读的任何内容使用 `text`，对次要信息使用 `text_muted`，并将
  `text_disabled` 保留给真正非活动的控件。禁用文本有意低于 AA（在
  `surface` 上深色 3.11，浅色 2.37）；不要将其用于已启用内容或状态消息。
- `outline`、`border` 和容器层级是装饰性分隔符，在 `surface` 上达不到
  3:1。不要仅用细线编码含义；将其与文本、图标或填充状态配合使用。
- 浅色强调色上的白色文本为 5.75:1。如果通过 `pt_app_set_accent_color`
  提供新强调色，系统会为你选择推导出的 `accent_text`，但如果你随后覆盖
  `accent_hover` 和 `accent_pressed`，请重新检查它们。
- 强调色按设计是最弱的一对（约 5:1）。不要将其降到 4.5:1 以下，也不要
  在小尺寸和降低不透明度的情况下使用强调色文本。
- 焦点是针对 `background` 检查的，而不是针对每个表面。背景异常浅或深的
  定制窗口应覆盖 `focus`，并针对实际背景验证环。

添加自定义颜色对时，应测量而不是凭肉眼判断。测试文件包含 `srgb_channel`、
`relative_luminance` 和 `contrast_ratio` 辅助函数，可以复制到应用程序
测试中。

## 深色与浅色配对规则

两个调色板不是简单的反转。调整调色板时保持以下关系：

- **表面方向：** 深色表面随着层级升高而变亮（`surface_sunken` →
  `surface` → `surface_container` → `surface_raised`）。浅色表面略微
  变暗或保持白色；凸起的浅色表面可以与基础表面同为白色。
- **文本方向：** 深色使用近白色文本；浅色使用近黑色文本。弱化文本向背景
  靠拢，而不是向强调色靠拢。
- **强调色文本：** 根据强调色亮度选择，而不是根据方案。深色调色板中的浅色
  强调色和浅色调色板中的深色强调色都需要深色文本。
- **半透明：** 保持 alpha 系列一致但不完全相同。参考表使用 `accent_soft`
  为 20%/15%，`selection` 为 35%/25%，柔和状态为 `26`/`20`，遮罩为
  `A6`/`66`（深色/浅色）。将深色 alpha 复制到浅色调色板通常会产生浑浊的
  底色。
- **阴影和遮罩：** 使用带 alpha 的冷色近黑色，而不是灰色。浅色调色板的
  阴影和遮罩带有 `0F172A` 色调，与背景色系一致。
- **状态角色：** 深色状态颜色较浅，以便在深色表面上可读；浅色状态颜色较深，
  以便在白色上可读。柔和变体保持相同色相，让 alpha 承载强调。

一个有用的检查是在两种方案中渲染同一屏幕，并比较状态矩阵，而不仅仅是静止
帧：悬停、按下、焦点、选中、禁用、无效和繁忙各自都有其令牌组合。

## 实践中的覆盖

先进行应用范围的品牌调整，然后让单个预览窗口保持中性：

```c
/* Set the scheme first: it replaces app colors and metrics. */
pt_app_set_color_scheme(app, pt_color_scheme_dark);
pt_app_set_accent_color(app, pt_rgb(0x3D7BFF));
pt_app_set_theme_metric(app, pt_theme_metric_radius_medium, 12.0f);

/* One window opts out of the brand accent. */
pt_window_set_theme_color(preview, pt_theme_accent, pt_rgb(0x6B7280));
pt_window_set_theme_color(preview, pt_theme_accent_hover, pt_rgb(0x7B8494));
pt_window_set_theme_color(preview, pt_theme_accent_pressed, pt_rgb(0x5B6270));
pt_window_set_theme_color(preview, pt_theme_accent_text, 0xFFFFFFFFu);
pt_window_set_theme_metric(preview, pt_theme_metric_radius_medium, 8.0f);
```

`pt_window_clear_theme_overrides(preview)` 会将每个角色和度量恢复为应用
值。窗口覆盖在绘制时解析，因此同一控件只有在应用程序自行管理由此产生的视觉
变化时，才能在窗口之间移动。

主题切换是应用状态。如果应用遵循桌面的浅色/深色偏好，请应用
`pt_app_set_color_scheme`，然后重新应用任何品牌覆盖；不要假设之前的自定义
颜色会保留。

## 测试调色板

- `make check` 运行 `test_theme`（默认值、两种方案、强调色推导、度量
  钳制、窗口覆盖回退和对比度下限）以及 `test_visuals`（控件在静止、悬停、
  焦点、选中、禁用、无效和繁忙状态下的确定性快照）。
- 设置 `PT_SNAPSHOT_DIR` 以选择 `test_visuals` 写入 PNG 的位置。默认值是
  `build/snapshots`。
- `test_visuals` 比较重复绘制、深色 → 浅色 → 深色往返以及窗口覆盖，因此
  不再影响绘制的令牌会显示为两个相同的帧，而不是静默的视觉回归。
- 新角色必须在 `pt_theme_token_count` 之前追加到 `pt_theme_token`；如果
  现有值被重新编号，`tests/test_abi.c` 会失败。新度量遵循相同规则。

## 限制

- 该表按应用或按窗口全局生效。除了现有的显式颜色 setter 之外，没有按控件
  的令牌覆盖，窗口之间也没有主题继承链。
- 1.0 没有主题文件解析器或可视化编辑器，主题就是调用这些 setter 的代码。
  **2.5.0 交付了独立的主题编辑器**，见下文「主题编辑器（2.5.0）」一节；解析
  器与编辑器完全位于该工具内，不增加核心 API。
- `pt_app_set_color_scheme` 是整体调色板加载，而不是合并。这是有意为之：
  部分切换会混合深色和浅色表面。
- 内置调色板假定 sRGB 输出。软件渲染器不进行颜色管理，因此广色域显示器会
  看到数值上的 sRGB 值。

## 主题编辑器（2.5.0）

Pt 2.5.0 在 `tools/theme-editor/` 下附带一个独立的主题编辑器。它本身就是
一个 Pt 应用程序——编辑器是对公共控件集的自举使用——并且完全位于核心 ABI
之外：令牌表、setter 与 getter 以及 `api/pt.api` 均未改变，该工具不新增任何
核心符号。工具自带有界的 pt-theme/1 读取器与写入器、C 导出器以及纯粹的
WCAG 2.x 模块；核心的近似 `pt_theme_contrast_text` 被有意保持不变。

编辑器让令牌表无需手写 48 次 setter 调用即可编辑：主题可以在图形界面中
编排、以无头方式校验、以 `pt-theme/1` JSON 文件保存，并导出为 C 代码。

### 构建、运行与校验

该工具不属于 `make all`，需显式构建：

- `make theme-editor` 从工具源码编译 `build/theme-editor`，仅通过公共头
  文件链接 `build/libpt.a`，因此任何工具符号都不会进入 `libpt.so`。
- `make run-theme-editor` 在需要时先构建，然后用内置深色调色板打开图形
  界面。
- `make check-theme-editor` 运行无头校验：对两个黄金样例
  `tools/theme-editor/themes/dark.json` 与
  `tools/theme-editor/themes/light.json` 执行 `--check`。它会打印 WCAG
  配对表，不需要显示环境，任何一对低于下限即失败。

命令行模式（`theme-editor --help` 打印相同文本）：

```text
Usage: theme-editor [options] [file.json]

Standalone visual editor for the Pt theme token table.

Modes:
  theme-editor                              open the editor with the built-in dark palette
  theme-editor <file.json>                  open the editor with an imported document
  theme-editor --check [--strict] <file>    validate and print the WCAG pair table
  theme-editor --export-json <in> <out>     write canonical pt-theme/1 JSON
  theme-editor --export-c <in> <out.c>      write a compilable theme_apply_<slug> function

Options:
  --strict    promote unknown-key warnings to errors (--check)
  --help      show this help and exit
  --version   show the tool version and exit

Exit codes: 0 success, 1 a floored WCAG pair failed, 2 malformed input
or usage error. Headless modes never open a window.
```

### pt-theme/1 格式

主题文件是单个 JSON 对象，`schema` 必须恰为 `pt-theme/1`。`scheme` 为
`dark` 或 `light`；它记录生成该文档时所基于的调色板，以及 C 导出器首先
应用的方案，且绝不会从颜色值中推断。`colors` 将 38 个角色名映射为 8 位
十六进制 `RRGGBBAA` 值，`metrics` 将 10 个度量名映射为有限、非负的数值。
角色名是上文令牌表中去掉 `pt_theme_` 前缀的短名，度量名去掉
`pt_theme_metric_` 前缀。每个已知角色和度量都必须存在。

导入是宽容的，因此文件可以手工编辑，也可以使用颜色选择器自身的拼写：颜色
可写成 `#RRGGBBAA`、`#RRGGBB`（alpha 为 `FF`）、`0xRRGGBBAA` 或小写。下
文附带的黄金深色调色板是规范参考，其完整内容为：

```json
{
  "schema": "pt-theme/1",
  "name": "Default Dark",
  "scheme": "dark",
  "colors": {
    "background": "0B0E14FF",
    "surface": "11151EFF",
    "surface_raised": "161B26FF",
    "surface_hover": "1D2431FF",
    "border": "232B3AFF",
    "border_strong": "39445AFF",
    "text": "E9EEF7FF",
    "text_muted": "94A1B8FF",
    "accent": "3568E0FF",
    "accent_hover": "3D6FE8FF",
    "accent_text": "FFFFFFFF",
    "danger": "F16B75FF",
    "success": "4ACB8DFF",
    "warning": "E5B454FF",
    "shadow": "00000073",
    "titlebar": "0E121BFF",
    "scrollbar": "4A566EFF",
    "focus": "8CB0FFFF",
    "surface_sunken": "080B11FF",
    "control": "1A2130FF",
    "control_hover": "222B3CFF",
    "accent_soft": "3568E033",
    "selection": "3568E059",
    "track": "2A3344FF",
    "surface_container": "141924FF",
    "surface_container_high": "1A2130FF",
    "outline": "39445AFF",
    "outline_variant": "232B3AFF",
    "text_disabled": "5B6577FF",
    "accent_pressed": "2F5BC0FF",
    "control_pressed": "2A3447FF",
    "danger_soft": "F16B7526",
    "success_soft": "4ACB8D26",
    "warning_soft": "E5B45426",
    "scrim": "000000A6",
    "skeleton": "1D2431FF",
    "skeleton_highlight": "2A3447FF",
    "selection_text": "FFFFFFFF"
  },
  "metrics": {
    "radius_small": 6,
    "radius_medium": 10,
    "radius_large": 16,
    "radius_pill": 999,
    "focus_width": 2,
    "focus_offset": 2,
    "elevation_low": 8,
    "elevation_high": 20,
    "chip_height": 28,
    "control_height": 34
  }
}
```

### 规范输出、安全限制与 --strict

导出是确定且规范的：

- 颜色为不带 `#` 的大写 8 位十六进制；
- 度量以 `%.9g` 打印；
- 键按枚举顺序（即上文各表的顺序），两空格缩进，恰好一个末尾换行；
- 同一文档导出两次逐字节一致，重新解析导出结果可恢复每个值。

读取器是有界的，未信任的文件无法耗尽工具：输入最多 1 MiB，JSON 嵌套最多
8 层，主题名最多 95 个 UTF-8 字节，顶层对象之后的尾随字节被拒绝，重复键
被拒绝，度量必须有限且非负——负度量是错误，而非静默钳制。数值解析不使用
`strtod`，因此本地化的小数分隔符不会改变文档内容。畸形 JSON、未知转义、
未配对的代理项和无效 UTF-8 都会被拒绝。

未知键——例如未来 schema 将新增的角色——会被忽略并给出警告，因此更新的
文件仍可打开。`--strict` 将这些警告提升为错误，这是 CI 的用法：与当前
v1 形态不完全一致的文件会失败，而不是带着警告通过。

### C 导出

`theme-editor --export-c <in.json> <out.c>` 写出一个完整、可编译的 C23
翻译单元，内含一个 apply 函数——不含头文件——因此代码片段可以直接粘贴到
应用程序中或直接编译。该函数首先应用方案，然后通过公共 setter 按枚举顺序
应用全部 38 个颜色和全部 10 个度量。对上文的黄金深色调色板，输出为：

```c
/* Generated by pt-theme-editor; schema pt-theme/1; theme "Default Dark". */
#include <pt/pt.h>

void theme_apply_default_dark(pt_app *app) {
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_theme_color(app, pt_theme_background, 0x0B0E14FFu);
    pt_app_set_theme_color(app, pt_theme_surface, 0x11151EFF);
    pt_app_set_theme_color(app, pt_theme_surface_raised, 0x161B26FF);
    pt_app_set_theme_color(app, pt_theme_surface_hover, 0x1D2431FF);
    pt_app_set_theme_color(app, pt_theme_border, 0x232B3AFF);
    pt_app_set_theme_color(app, pt_theme_border_strong, 0x39445AFF);
    pt_app_set_theme_color(app, pt_theme_text, 0xE9EEF7FF);
    pt_app_set_theme_color(app, pt_theme_text_muted, 0x94A1B8FF);
    pt_app_set_theme_color(app, pt_theme_accent, 0x3568E0FF);
    pt_app_set_theme_color(app, pt_theme_accent_hover, 0x3D6FE8FF);
    pt_app_set_theme_color(app, pt_theme_accent_text, 0xFFFFFFFF);
    pt_app_set_theme_color(app, pt_theme_danger, 0xF16B75FF);
    pt_app_set_theme_color(app, pt_theme_success, 0x4ACB8DFF);
    pt_app_set_theme_color(app, pt_theme_warning, 0xE5B454FF);
    pt_app_set_theme_color(app, pt_theme_shadow, 0x00000073);
    pt_app_set_theme_color(app, pt_theme_titlebar, 0x0E121BFF);
    pt_app_set_theme_color(app, pt_theme_scrollbar, 0x4A566EFF);
    pt_app_set_theme_color(app, pt_theme_focus, 0x8CB0FFFF);
    pt_app_set_theme_color(app, pt_theme_surface_sunken, 0x080B11FF);
    pt_app_set_theme_color(app, pt_theme_control, 0x1A2130FF);
    pt_app_set_theme_color(app, pt_theme_control_hover, 0x222B3CFF);
    pt_app_set_theme_color(app, pt_theme_accent_soft, 0x3568E033);
    pt_app_set_theme_color(app, pt_theme_selection, 0x3568E059);
    pt_app_set_theme_color(app, pt_theme_track, 0x2A3344FF);
    pt_app_set_theme_color(app, pt_theme_surface_container, 0x141924FF);
    pt_app_set_theme_color(app, pt_theme_surface_container_high, 0x1A2130FF);
    pt_app_set_theme_color(app, pt_theme_outline, 0x39445AFF);
    pt_app_set_theme_color(app, pt_theme_outline_variant, 0x232B3AFF);
    pt_app_set_theme_color(app, pt_theme_text_disabled, 0x5B6577FF);
    pt_app_set_theme_color(app, pt_theme_accent_pressed, 0x2F5BC0FF);
    pt_app_set_theme_color(app, pt_theme_control_pressed, 0x2A3447FF);
    pt_app_set_theme_color(app, pt_theme_danger_soft, 0xF16B7526);
    pt_app_set_theme_color(app, pt_theme_success_soft, 0x4ACB8D26);
    pt_app_set_theme_color(app, pt_theme_warning_soft, 0xE5B45426);
    pt_app_set_theme_color(app, pt_theme_scrim, 0x000000A6);
    pt_app_set_theme_color(app, pt_theme_skeleton, 0x1D2431FF);
    pt_app_set_theme_color(app, pt_theme_skeleton_highlight, 0x2A3447FF);
    pt_app_set_theme_color(app, pt_theme_selection_text, 0xFFFFFFFF);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_small, 6.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_medium, 10.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_large, 16.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_pill, 999.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_focus_width, 2.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_focus_offset, 2.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_elevation_low, 8.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_elevation_high, 20.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_chip_height, 28.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_control_height, 34.0f);
}
```

函数名是 `theme_apply_` 加上主题名的 slug：仅小写 ASCII 字母、数字和
`_`，其余每个字节替换为 `_`；空名导出为 `theme_apply_custom_theme`。图形
界面从不隐式写文件——导出是显式操作。各文件之间的主题名不要求唯一，因此名
称冲突由应用程序自行负责。

### WCAG 检查

`--check` 打印每一对的实测比率、等级和通过/失败结论，当所有下限配对通过
时退出码为 0，某对失败时为 1，文件畸形或用法错误时为 2；`--check
--strict` 还会在解析警告时失败。下表包含上文[对比度](#对比度)一节中的 12
个文档配对，加上在 `surface` 与 `background` 上测量的两个选择合成——共
14 个下限配对——以及一个信息性行（`surface` 上的 `text_disabled`，预期
低于 AA，永不失败）：

| 配对 | 下限 |
| --- | ---: |
| `background` 上的 `text` | 7.0（AAA） |
| `surface` 上的 `text` | 7.0 |
| `surface_raised` 上的 `text` | 7.0 |
| `background` 上的 `text_muted` | 4.5（AA） |
| `surface` 上的 `text_muted` | 4.5 |
| `accent` 上的 `accent_text` | 4.5 |
| `accent_hover` 上的 `accent_text` | 4.5 |
| `accent_pressed` 上的 `accent_text` | 4.5 |
| `surface` 上的 `danger` | 4.5 |
| `surface` 上的 `success` | 4.5 |
| `surface` 上的 `warning` | 4.5 |
| `background` 上的 `focus` | 4.5 |
| `surface` 上 `selection` 复合后的 `selection_text` | 4.5 |
| `background` 上 `selection` 复合后的 `selection_text` | 4.5 |

任何配对都不会被静默跳过：每一行都会以 `pass`、`AA`、`AAA` 或 `fail`
报告。等级遵循 WCAG 2.x 级别——7.0 及以上为 AAA，4.5 至 7.0 以下为 AA，
低于 4.5 为失败。选择行在测量前先将半透明的 `selection` 底色与背景合成，
因此报告的比率就是渲染器实际产生的比率。

核心的 `pt_theme_contrast_text`——`pt_app_set_accent_color` 用它选择
`accent_text`——是一个快速的 gamma 编码加权和（对 8 位通道做
`0.2126R + 0.7152G + 0.0722B`，阈值 `0.62`），并非 WCAG 亮度计算。在该
阈值附近，两者可能给出不同结果。核心助手在 2.5.0 中不做修正，因为改动它
会改变强调色推导和既有像素。因此编辑器用自带的纯粹 WCAG 2.x 模块——sRGB
线性化、相对亮度与对比度公式——测量配对并报告真实比率；当核心会选择不同
的 `accent_text` 时，对比度面板会显示失败，用户可以显式覆盖 `accent_text`。
这是快速的核心启发式与符合标准的精确检查之间的已记录差异，两者都并非缺陷。

### 编辑器工作流

图形界面是一个 Pt 应用程序：

- 主窗口显示主题名、方案（创建后只读）以及新建深色、新建浅色、导入 JSON、
  导出 JSON、导出 C 和预览等操作。可滚动的颜色列表为每个角色配有一个
  `pt_color_picker`，旁边是角色名；选择器的十六进制输入接受包含 alpha 的
  `#RRGGBBAA`，其弹出窗口为预设调色板。度量部分编辑十个度量；对比度部分
  显示配对表及比率与状态。
- 新建深色/新建浅色通过公共应用 getter 从内置调色板生成文档；此后文档与
  应用相互独立。
- 预览是第二个顶层 Pt 窗口，包含代表性控件样例。每次编辑仅通过
  `pt_window_set_theme_color` / `pt_window_set_theme_metric` 应用到该窗口，
  因此编辑器自身的窗口保持中性默认调色板，而预览走真实的按窗口覆盖路径。
  颜色选择器的改动会立即更新模型与预览。
- 导入替换整个文档；导出将其规范化。图形界面中没有直接的主题文件写入。
- 每个选择器、数字输入和按钮都带有无障碍名称，标签顺序遵循视觉顺序，状态
  由文本加颜色表达——绝不只依赖颜色。

### 工具的限制

- 编辑器是工具而非核心 API：它仅通过公共头文件链接静态库，不向 `libpt.so`
  添加任何符号。不构建该工具的应用程序不受影响。
- 格式为 `pt-theme/1`，一个文件恰好承载一种方案。多方案捆绑与读取桌面实时
  主题属于未来 schema 的候选。
- `--check` 无头校验文件中的调色板，不打开图形界面，也不接触合成器。自定义
  调色板的像素级验证仍由应用程序的视觉测试完成（见上文
  [测试调色板](#测试调色板)）。
