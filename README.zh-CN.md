# Pt

[English](README.md) | 简体中文

**Pt** 是一个小型、快速、原生 Wayland 的 GUI 框架，使用现代 C23 编写。
应用可以使用 C、C++、Python 或 Java 编写，面向同一套扁平 C ABI。
这个名字是铂的化学符号：小而致密、不易反应。

状态：**2.0.0**。从右到左与双向文本版本：通过随源码附带的 SheenBidi 引擎实现
完整的 Unicode 双向算法并达到 100% 一致性，新增八个方向函数，视觉插入符号移动
与选择，镜像 UI 几何和界面元素，无障碍方向属性，以及逐字节的 LTR 兼容闸门。
表面为包含 564 个函数的 C ABI，由 C++/Python/Java 封装，并强制要求绑定功能对等；
SONAME 保持 `libpt.so.1`，因为 2.0 只新增声明并修正双向文本渲染，不破坏二进制
接口（`ABI_MAJOR` 与 `VERSION` 分开跟踪）。
发布记录已写好，最终门禁运行与带注释的标签随后进行；参见
[docs/release.md](docs/release.md)。API 由经审阅的基线、生成的函数参考和
兼容性策略共同跟踪；参见 [docs/api-reference.md](docs/api-reference.md)、
[docs/api-policy.md](docs/api-policy.md) 和 [docs/ROADMAP.md](docs/ROADMAP.md)。

## 为什么选择 Pt

- Wayland 优先：`xdg-shell`、默认采用客户端装饰的装饰协商、
  `fractional-scale-v1` 和 `text-input-v3`，不背负 X11 或 GL 运行时包袱。
- 保留模式控件树，以及一套扁平、不透明、便于绑定的 C ABI。
- 清晰的 FreeType + HarfBuzz 文本，包括 CJK 字形整形和分数缩放。
- 正确实现从右到左文本：完整的 Unicode 双向算法、带自动括号镜像的阿拉伯文和
  希伯来文整形、镜像行、列序保持逻辑的表格、视觉插入符号移动，以及无障碍方向
  属性——纯 LTR 界面的像素保持不变。
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

## 2.0.0 的新特性

2.0.0 是 1.0 以来第一个结构性版本，也是双向文本/从右到左里程碑。它新增
**8 个公共函数和 2 个枚举（6 个值）**，零值默认保持 LTR 行为：表面为
**564 个函数和 924 条基线条目**，0 移除、0 变更。SONAME 保持
`libpt.so.1`：本版本标记渲染修正和追加式 API，而不是二进制破坏，因此 Makefile
将 `ABI_MAJOR` 与 `VERSION` 分开跟踪。带注释的标签在最终门禁运行后创建，记录见
[release.md](docs/release.md)。

- **真正的双向文本引擎。** 2.0 随源码附带 SheenBidi v3.0.0（Apache-2.0），作为
  单个 unity 对象编译，并通过 UTF-32 适配层驱动，在码点旁保留字节偏移表。适配层
  存在的原因是上游 UTF-8 入口存在 L1 行末空白缺陷；内部路径避开它，且不修改
  引擎。Unicode 双向算法现在完整：显式嵌入和覆盖、隔离、弱字符和中性字符解析、
  配对括号、隐式层级以及换行后的行级规则。
- **100% 一致性，纳入 `make check`。** 随源码附带的 Unicode 18.0.0
  `BidiTest.txt` 和 `BidiCharacterTest.txt` 数据（Unicode License v3，gzip 压缩）
  运行 **770,241 + 91,707 个用例，0 失败**，耗时约 0.5 秒。`THIRD_PARTY.md`
  记录引擎标签、摘要、许可证和升级政策。
- **方向 API 不引入新的编辑模型。** `pt_ui_direction`（`inherit`/`ltr`/`rtl`）
  和 `pt_text_direction`（`auto`/`ltr`/`rtl`）加上应用、窗口和控件的 UI
  setter/getter 以及控件的文本 setter/getter，共八个函数。解析顺序为控件 →
  最近显式祖先 → 窗口 → 应用 → LTR；自动按段落遵循 UBA P2/P3，对仅含中性字符的
  文本回退到解析后的 UI 方向。getter 返回显式值，`inherit`/`auto` 是零值，
  因此零初始化的控件与 1.x 行为一致。
- **默认视觉编辑。** 左/右方向键移动到视觉相邻的 cluster 边界（在 RTL 文本段内
  可能增大或减小逻辑偏移），Home/End 前往视觉行末，Ctrl+Home/End 保持文档逻辑
  首尾，Shift 保持逻辑锚点，Backspace/Delete 保持逻辑语义。软换行边界携带插入
  符号亲和性，选择为每个视觉文本段绘制一个矩形，输入法矩形是视觉的，密码圆点
  根据存储文本派生方向。需要逻辑移动的应用在现有窗口按键回调中拦截按键；没有
  第九个 API。
- **镜像 UI 几何与界面元素。** flex、换行、网格和滚动几何镜像；标签页、侧边栏、
  选择器弹出层、客户端装饰、覆盖层和菜单根据解析后的 UI 方向放置界面元素。
  滚动条移到 RTL 左侧，树箭头移到右侧。表格和树**列序保持逻辑**（内部退出
  选项），RTL 输入框右锚定（`scroll_x = 0` 时在视觉右端显示逻辑开头），
  内边距/外边距保持物理方向，没有 start/end API。
- **无障碍方向。** 每个无障碍对象把解析后的 UI 方向发布为 `direction` 属性，
  文本对象通过 `Text.GetAttributes` 和 `Text.GetDefaultAttributes` 以标量范围
  发布段落方向。C 层 `pt_widget_get_a11y_text` 保持**字节**偏移，而 D-Bus
  `Text` 接口使用**标量**偏移；密码报告方向但不报告字符。旧措辞把所有无障碍
  getter 称为标量，已更正。
- **LTR 由两道硬门保护。** `test_visuals` 中 17 张纯 LTR PNG 快照与 1.4 逐字节
  一致，`tools/ltr-compat.sh` 在同一台机器上构建 1.4.0 和 2.0 树，并在十个 LTR
  场景中报告 **10/10 `SAME`**。不签入 golden hash，因为字体光栅化在不同机器上
  有差异；同时构建两个版本可让该因素保持恒定。
- **诚实的性能措辞。** LTR 快速路径根据引擎的 Unicode 双向类型分类文本（任何
  R/AL 字符或双向格式控制字符都需要 UBA），并保留旧的单文本段路径；对
  292,531 个已分配码点的扫描发现 0 处失配。双向分析每次重建为 O(n)，分配受行
  长度限制，整形缓存以方向为键。旧的“无逐帧分配”承诺被撤回，因为 1.4 本就每次
  绘制都重建行布局；契约现在是**每次重建有界**。
- **四种语言的示例。** `examples/c/rtl_gallery.c` 展示阿拉伯文/希伯来文/拉丁文
  混排标签、RTL 输入框和文本区域、镜像容器、嵌套 LTR 面板和方向切换；
  C++、Python 和 Java 各有一个更小的方向切换，用于演练封装层。

## 1.4.0 的新特性

1.4.0 收尾了 1.3 之后遗留的可追加事项，同时不触碰 2.0 的结构性边界。
它**不新增任何公共函数、枚举或结构**：表面仍为 556 个函数和 910 条基线条目，
与 1.3.0 完全一致。本版工作是能力扩展、测试基础设施，以及一处有意的行为修正。
带注释的标签将在最终门禁运行后创建，记录见 [release.md](docs/release.md)。

- **SVGZ 图标源。** `pt_icon_image_load_file` 和
  `pt_icon_image_load_memory` 通过内容嗅探接受 gzip 压缩的 SVG 文档：
  gzip 魔数 `0x1f 0x8b` 先于文本嗅探检查，因此压缩文档的顶层字节不必看起来像 XML。
  解压在两个方向都有界——压缩输入沿用现有 16 MiB 上限，解压后文档同样限制在
  16 MiB，超出即拒绝而不是继续增长（解压炸弹守卫）；解压后的字节还必须通过
  同一 SVG 嗅探，因此并非 SVG 的 gzip 归档会被拒绝。句柄保留解压后的源字节而非
  压缩字节，并像普通 SVG 句柄一样按每个请求尺寸重新解析。hicolor `scalable/`
  查找现在依次尝试 `.svg`、`.svgz`、`.png`；固定尺寸目录仍仅查找 `.png`。
  零新增公共函数：既有加载器获得该格式，`pt/icon.h` 将契约标注为
  “Since 1.2; SVGZ since 1.4”。`test_icon` 从 330 增至 800 项检查，包含对解压器的
  变异与垃圾输入模糊测试。
- **输入线路矩阵补全。** 伪合成器现在注入并回放剩余输入协议表面。`wl_touch`
  覆盖 down/up/motion/frame/cancel、多接触点与捏合，以及能力移除；tablet-v2
  覆盖 seat/tool 生命周期、proximity、压力、倾斜、旋转、滑块、滚轮和按键；
  拖放覆盖 `data_device` enter/motion/drop/leave、经 `SCM_RIGHTS` fd 的 offer
  accept/receive 与 finish，以及 source target/action/cancelled/`dnd_finished`；
  主选区增加注入与回放。新的 `test_wire_input` 槽通过 595 项检查，
  `wait_object` 辅助函数消除了服务端状态竞态（连续 200 次运行零失败），
  设施的手写板管理器请求表中 `get_tablet_seat`/`destroy` 顺序写反的问题也已修正。
  这属于测试基础设施：不改变公共头文件、符号或 ABI。
- **多触点焦点修正。** `src/wayland/platform.c` 曾在第一个接触点抬起时无条件清除
  `touch_focus`，导致其余手指的 motion 和 up 被丢弃、捏合遗留活动状态、
  cancel 无法清理，重复捏合也会失效。内部 `pt_window_input_touch_up` 现在返回
  剩余接触点数（内部接口从 `void` 改为 `uint32_t`，不是公共 ABI 变更），
  platform 仅在剩余数为零时清除焦点；幸存的单接触点会重新更新模拟指针，
  而不再被忽略。捏合仍然绝不触发点击，`test_damage` 保持 98 项通过。
  这是一处有意的行为修正：重复捏合与中途 cancel 现在行为正确。
  `test_wire_input` 已从“记录旧限制”升级为“断言正确行为”。
- **niri + fcitx5 实时 IME 闭环。** 运行时 registry 检查显示 niri 26.04 广告
  `zwp_input_method_manager_v2` v1，fcitx5 5.1.23 运行其 pinyin 引擎。
  键入 `nihao ` 现在可驱动实时组合：预编辑依次演进 `n` → `ni` → `ni h` →
  `ni ha` → `ni hao`，空格经 text-input-v3 将 `你好` 提交到 Pt
  （`PREEDIT_COUNT=5`）。关键点是：在嵌套 display 上创建的虚拟键盘会绕过
  input-method 抓取，因此 harness 改为向父 display 注入，让嵌套 niri 像接收
  真实键盘一样收到按键。harness 由 `tools/live-ime.sh` 与
  `tests/ime_live_probe.c`（仅使用公开 API）组成，以 `make check-ime-live` 运行；
  没有父 niri 时诚实地以 rc 0 跳过；将 fcitx5 隔离在私有会话总线上，不触碰
  用户实例；并通过有界等待、信号即退和注键前焦点复核加固流程。它已接入
  `ci.sh` 的实时段。KWin+fcitx5 与 Mutter+IBus 实时组合仍缺环境，保留为遗留项。

## 1.3.0 的新特性

1.3.0 收尾了 1.2 之后遗留的可追加事项，同时不触碰 2.0 的结构性边界。
它保留全部 1.2 签名，新增两个函数和一个无障碍枚举值，并加入一套不改变公共 ABI 的
协议级测试设施。带注释的标签将在最终门禁运行后创建，记录见
[release.md](docs/release.md)。

- **伪合成器线路验证。** `tests/fake_compositor.h` 是运行在私有套接字上的
  进程内 Wayland 服务器：Pt 作为普通客户端连接它，因此实际发布的连接路径原样运行。
  它解码并记录每个请求（对象、接口、操作码、带类型参数），并注入服务器事件，
  使无头测试可以断言弹出层定位的线路参数、录制顶层窗口图标提交、回放装饰模式交换，
  并驱动 text-input IME 流量（包括 v1 抑制路径）。小型 C 桥接层
  `tests/wire_driver.c` 将该设施暴露给 C++、Python 和 Java 套件，因此原始按键和指针
  事件现在会在三种绑定中端到端到达封装回调，而不再只做注册测试。
- **IME 视觉补全。** 七种 `preedit_hint` 类型渲染为分级第二下划线（组合、选区、
  前缀/后缀、预测、拼写错误和组合错误），而没有 hint 的预编辑与 1.2 逐像素一致。
  `pt_window_show_input_panel` 和 `pt_window_hide_input_panel` 转发 text-input-v3 v2
  请求，在 IME 未激活或仅 v1 时记住期望状态，在重新激活时重放，并报告状态是否改变。
- **SVG 图标源。** `pt_icon_image` 通过内置的 nanosvg 解码器（zlib 许可，归属记录在
  `THIRD_PARTY.md`）在 PNG 之外接受 SVG 内容，hicolor 查找可解析 `scalable/` 目录。
  光栅化进入原有的按尺寸/缩放缓存；受支持的 SVG 子集和对不支持特性的拒绝规则都有
  诚实文档，而不是隐含。没有新增公共函数：内容通过嗅探识别，既有加载器签名不变。
- **数值步进按钮无障碍角色。** `pt_a11y_role_spin_button` 追加为值 36，
  使 `pt_a11y_role_count` 哨兵变为 37。数字选择器报告该步进按钮角色且没有展开状态，
  AT-SPI 桥会映射它，这正是 1.2 记录预期的修正。
- **C++ 加固。** 五个拥有型 getter 通过 RAII 守卫释放其 C 缓冲区，
  因此字符串或缓冲区复制期间的 `std::bad_alloc` 不会泄漏；`pt::popup` 封装层
  记录了它必须先于父窗口销毁。

## 1.2.0 的新特性

1.2.0 是 1.0 之后的第一个常用表面波次：应用原本需要自行实现的控件与平台管线。
它保留全部 1.1 签名并新增 106 个函数；带注释的标签将在最终门禁运行后创建，
记录见 [release.md](docs/release.md)。

- **应用自有弹出层。** `pt_popup_create` 基于现有的 `xdg_popup` 后端，
  从父窗口和内容控件创建弹出层。它按父窗口逻辑坐标中的锚点矩形显示，
  采用翻转/滑动约束定位，可重新定位，并在外部点击、Escape 或抓取中断时
  关闭，通过 `pt_popup_set_on_dismiss` 报告关闭原因。无头运行使用与菜单和
  工具提示相同的窗口内回退方案。
- **逐字形段字体回退。** `pt_app_set_font_fallbacks` 安装应用级回退链，
  `pt_app_set_font_fallbacks_for_family` 为某个主字体族覆盖它。文本被拆分为
  HarfBuzz 字形段，每段使用第一个覆盖它的字体面整形；未安装回退链时使用
  Fontconfig 自身的排序。行度量取该行最高字体面；
  `pt_app_font_uncovered_count` 报告仍显示为豆腐块的码点。
- **剪贴板媒体。** `pt_window_clipboard_set_image_png` 和
  `pt_window_clipboard_get_image_png` 读写 `image/png`，沿用现有的
  1.5 秒 / 16 MiB 传输上限，并增加 8192×8192 解码像素上限；解码失败返回
  NULL。预乘 ARGB 与直通 alpha PNG 格式之间的转换已写入文档。
  `pt_window_clipboard_set_uri_list` 使用拖动源编码器写入 CRLF、
  百分号编码的 `file://` URI。
- **列布局持久化。** `pt_table_columns_save` 和 `pt_table_columns_restore`
  为共享的表格/树列模型往返读写带版本的 blob，包含列宽、顺序、可见性和键。
  应用拥有并存储该 blob；未知版本和损坏输入会被拒绝。
- **图标图像与顶层窗口图标。** `pt_icon_image_load_file`、
  `pt_icon_image_load_memory` 和 `pt_icon_image_from_name` 创建引用计数的
  栅格图像（PNG、hicolor 主题查找、按尺寸/缩放缓存），可用于图标按钮的图像
  槽位并可选着色，或用于窗口图标。在合成器暴露相应协议时，
  `pt_window_set_icon` 通过 `xdg-toplevel-icon-v1` 发布它；否则为尽力而为，
  图像仍由应用拥有。
- **选择器。** 可搜索的 `pt_select`（带项数据、查询过滤、“无匹配”和候选上限
  的弹出列表）、`pt_date_picker`（可键入日期加六周日历）、
  `pt_number_picker`（带范围和步长的微调框）以及 `pt_color_picker`
  （十六进制输入、预览和预设调色板）。四者都是编辑器字段，拥有完整键盘路径、
  弹出层打开时输入框保持焦点、静默的程序化设置函数，以及无障碍值/展开状态
  断言。它们在核心上是组合框控件；可过滤组合框仍是独立的窗口内选择器。

## 1.1.0 的新特性

1.1.0 保留全部 1.0 签名，并为需要自行绘制内容、接管输入或直接与合成器交互
的应用新增一层只追加式扩展表面。源码树、测试、示例和文档均已完成；带注释
的标签将在最终门禁运行后创建，记录见 [release.md](docs/release.md)。

- **装饰协商。** 默认是显式的客户端装饰。枚举为
  `pt_decoration_mode {pt_decoration_auto, pt_decoration_client,
  pt_decoration_server}`。`pt_window_set_decorated(false)` 现在只表示
  “Pt 不绘制标题栏”，绝不再请求服务端装饰。`pt_window_set_decoration_mode`
  选择请求模式，`pt_window_get_decoration_mode` 返回该请求，
  `pt_window_get_effective_decoration_mode` 返回 `configure` 最终决定的
  生效模式。合成器未声明最大化能力时，`pt_window_maximize` 返回 false，
  标题栏按钮也会同时禁用。参见 [装饰政策](#装饰政策)。
- **主选区。** `pt_window_primary_set` 和 `pt_window_primary_get` 通过
  `wp_primary_selection_unstable_v1` 为鼠标中键粘贴提供支持，并使用触发事件
  的 serial；在 Wayland 之外则回退到进程内缓冲区。
  `pt_window_primary_set_serial` 允许在事件回调内显式传入 serial；
  没有触发 serial 的程序化选择返回 false 且不发送任何内容。
- **自定义绘制。** `pt_widget_set_paint_callback` 向控件提供一个借用的
  `pt_paint_context`，其中包含裁剪、矩形、线条、圆角矩形和文本辅助函数
  （`pt_paint_*`），以及控件范围的请求重绘入口。回调是纯函数且幂等，
  裁剪和重入守卫始终开启，而不是藏在调试开关后面。
- **离屏快照。** `pt_window_paint_pixels` 将完整一帧渲染到调用方拥有的
  `0xAARRGGBB` 缓冲区，不会产生损坏、重绘簿记或覆盖层收集，因此自定义绘制
  可以在无头环境中测试。`pt_window_paint_pixels_ex` 接受
  `pt_paint_pixels_desc`，其中包含 stride、scale 和 `pt_pixel_format`
  （目前只有 `pt_pixel_format_argb8888_premultiplied`），用于分数缩放和
  带填充的缓冲区。
- **自定义输入法。** `pt_window_set_ime_callbacks`、
  `pt_window_set_ime_enabled` 和 `pt_window_set_ime_cursor_rect` 让终端或画布
  无需伪装成输入框即可接收组合后的文本。
- **描述符监视。** `pt_app_watch_fd` 以借用描述符语义把管道、eventfd 和
  timerfd 纳入应用的 poll 循环；`pt_app_watch_fd_owned` 接管描述符，并在
  取消监视或应用销毁时关闭它。同一描述符的第二次监视会被拒绝，
  `POLLHUP`/`POLLERR`/`POLLNVAL` 始终以错误事件到达回调。
- **原始输入回调。** 焦点回调、携带 xkb UTF-8 文本的按键回调，以及归一化
  指针事件回调都在控件分发之前运行，并具有明确的消费范围。返回 true 会消费
  按键和指针 down/up/axis；enter、leave 和 motion 是建议性的，其返回值被忽略。
- **行为变化。** 默认装饰请求从服务端改为显式客户端；
  `pt_window_set_decorated(false)` 不再发送装饰模式；快照不再消费损坏区域或
  延迟覆盖层；重复的描述符监视会被拒绝；绘制回调内自毁、裁剪不平衡以及重入
  快照都有明确结果。完整清单见
  [1.1.0 发布记录](docs/release.md#110-validation-record-2026-11-22)。

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
make check-ime-live        # live niri+fcitx5 composition through text-input-v3
make bench                 # headless layout, full/partial paint and startup benchmarks
make perf-check            # benchmarks plus regression ceilings (4K, large-tree scroll)
make install-check         # staged install, SONAME/ABI and four-language consumers
tools/ci.sh                # CI-equivalent local gate, including sanitizers and tarball
tools/release-check.sh     # readiness report for the frozen release
```

`make check` 以无头方式运行，不需要合成器。它包含 C 测试套件、
C++/Python/Java 绑定测试套件、导出符号和 API 基线审计、
硬性的 564/564 绑定功能对等闸门、按轮次的 0.6–0.10 表面审计、
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
| C | [rtl_gallery.c](examples/c/rtl_gallery.c) | `make run-rtl-gallery` |
| C++23 | [counter.cpp](examples/cpp/counter.cpp) | `make run-counter` |
| C++23 | [workspace.cpp](examples/cpp/workspace.cpp) | `make run-cpp-workspace` |
| C++23 | [combobox.cpp](examples/cpp/combobox.cpp) | `make run-cpp-combobox` |
| C++23 | [rtl_direction.cpp](examples/cpp/rtl_direction.cpp) | `make run-cpp-rtl-direction` |
| Python | [counter.py](examples/python/counter.py) | `make run-python` |
| Python | [notes.py](examples/python/notes.py) | `python examples/python/notes.py` |
| Python | [workspace.py](examples/python/workspace.py) | `make run-python-workspace` |
| Python | [combobox.py](examples/python/combobox.py) | `make run-python-combobox` |
| Python | [rtl_direction.py](examples/python/rtl_direction.py) | `make run-python-rtl-direction` |
| Java 25 | [HelloPt.java](examples/java/HelloPt.java) | `make run-java` |
| Java 25 | [Workspace.java](examples/java/Workspace.java) | `make run-java-workspace` |
| Java 25 | [Combobox.java](examples/java/Combobox.java) | `make run-java-combobox` |
| Java 25 | [RtlDirection.java](examples/java/RtlDirection.java) | `make run-java-rtl-direction` |

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
- [从右到左与双向文本](docs/zh-CN/rtl.md)
- [迁移到 1.0](docs/zh-CN/migration-1.0.md)
- [API 兼容性策略](docs/api-policy.md)
- [性能](docs/performance.md)
- [与其他工具包的比较](docs/zh-CN/comparison.md)
- [路线图](docs/ROADMAP.md)
- [发布流程](docs/release.md)
- [工程规范](docs/standards.md)
- [合成器与协议矩阵](docs/compositor-matrix.md)

快速入门、架构、绑定、主题、无障碍、RTL/双向文本、迁移和比较指南提供简体中文版；
生成的 API 参考、发布记录、路线图、性能说明和政策页面保留英文。

<a id="limitations-in-20"></a>

## 2.0 中的限制

- 仅支持 Wayland；没有 X11、Windows 或 macOS 后端。
- 1.x ABI 为只追加：移除需要主版本发布和 SONAME 提升。
  签入的基线、符号审计和 `tests/test_abi.c` 会让任何漂移显式化。
  布局、绘制和事件循环仅在调用线程上运行。
- 渲染器仅使用 CPU。密集的全窗口动画在桌面尺寸窗口上表现舒适，
  但不是 4K/120 Hz 的 GPU 路径；加速后端是 1.0 之后的候选。
- 后端支持时，菜单、工具提示、仅选择组合框弹出层、应用自有弹出层和选择器
  弹出层使用原生 `xdg_popup` 表面；对话框、通知和可过滤组合框弹出层仍合成
  在窗口内，因为它们是模态的、堆叠的，或需要编辑器保持焦点。原生弹出层仍
  需要一个已配置的 xdg-shell 窗口，因此无头运行使用窗口内回退方案。
- 图标图像是栅格的：PNG、SVG 和 SVGZ 解码加 hicolor 主题查找，并带按尺寸/缩放
  缓存。SVG 子集诚实而非完整——文本、栅格图像、滤镜、遮罩、`clipPath`、
  `use`/`symbol`、标记、动画以及完整 CSS 级联会被忽略，没有可绘制形状的文档
  会被拒绝。`pt_window_set_icon` 是尽力而为：没有 `xdg-toplevel-icon-v1` 时合成器可能
  显示自己的回退图标，图像仍由应用拥有。
- 选择器不读取进程 locale。日期格式是显式的 `pt_date_format`，
  数字字段提交时始终使用 `.` 作为小数分隔符（输入时 `.` 和 `,` 都接受），
  颜色字段为十六进制。选择模型上限为 1024 项和 256 个候选行，
  颜色调色板上限为 64 个预设，日历始终渲染六周，因此边缘处的本地化由应用负责。
- 字体回退链是进程范围的，因为字体缓存是共享的。显式回退链是权威的：
  若其中没有任何字体族覆盖某字符，该字符会渲染为豆腐块，即使其他已安装字体
  能够覆盖。安装回退链会影响之后布局的文本，并重绘调用方应用。
- 表格列布局保存为不透明的带版本 blob，由应用存储。列宽、顺序、可见性和键
  可往返读写；不同版本之间没有架构迁移，因此未知版本会被拒绝，
  当前布局保持不变。
- AT-SPI2 桥是可选的（`pt_app_enable_a11y`），需要以 `dbus-1` 构建，
  并暴露控件树而不是任意绘制内容。屏幕阅读器可以看到 Pt 控件，
  但看不到没有无障碍元数据的自定义绘制。
- 拖动源和结构化放置提供内容需要与任何 Wayland 拖动相同的隐式指针抓取，
  因此 `pt_window_begin_drag` 仅在按压处于活动状态时成功，
  而无头运行没有可通信的拖动合成器。流式读取传递字节；
  解码 JSON 或 URI 列表是应用的工作。触摸捏合与旋转测量前两个被跟踪的触点；
  额外触点会被跟踪，但不改变手势度量。触控笔支持取决于合成器提供的 `tablet-v2`。
- 样式化文本段覆盖标签和文本区域字节范围内的字重、倾斜、颜色、下划线、
  逐段字体族和逐段字号。没有内联图像、没有逐段拉伸或光学对齐，也没有逐段双向
  文本方向：`pt_text_span` 作为纯样式保持冻结。段落方向来自控件或窗口，段落内
  的反向短语使用 Unicode 隔离控制字符。文本区域编辑纯文本并保持一个逻辑范围，
  而换行后的不同行可以有不同的段落基础方向。
- 双向文本和 RTL 有限制记录在案，而不是隐藏的回退。表格需要滚动条或键盘进行
  水平滚动，因为已测试的 Wayland 合成器不提供水平滚轮轴。在 RTL 中，Shift+
  方向键跨软换行仍是行内视觉移动，跨多个映射行的选择会在映射边界处向逻辑边缘
  折叠，而绘制的矩形保持正确。`pt_text_span` 没有方向字段。缺失的阿拉伯文/
  希伯来文字形是记录在案的豆腐块，由 `pt_app_font_uncovered_count` 报告，
  不是硬错误；请安装覆盖它们的字体，例如 Noto Sans Arabic/Hebrew 或
  DejaVu Sans。双向控制字符可以重排显示文本（Trojan Source 一类问题），且不会
  被自动清理；接受不可信文本的应用应自行剥离或显示它们。详情见
  [RTL 与双向文本指南](docs/zh-CN/rtl.md)。
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

## 装饰政策

Pt 会绘制自己的 44 px 标题栏，并把“谁装饰表面”和“Pt 是否绘制标题栏”当作
两个独立决定。默认是显式的客户端装饰（`pt_decoration_client`）：
Pt 在首次提交时向 `xdg-decoration` 请求客户端模式，因为它的标题栏、边缘
调整大小区域和命中测试都是为此构建的。

`pt_window_set_decorated(window, false)` 只是一个绘制开关。窗口不再有 Pt
标题栏和 Pt 边缘区域，也绝不向合成器请求装饰。服务端装饰需要显式选择：

```c
pt_window_set_decoration_mode(window, pt_decoration_server);
```

`pt_decoration_auto` 遵循框架默认策略，在整个 1.x 系列中即客户端装饰。请求
从来不是保证：`configure` 返回的模式才是权威结果，
`pt_window_get_effective_decoration_mode` 会单独报告它，而
`pt_window_get_decoration_mode` 返回请求模式。Sway 即使收到客户端
请求，也会对平铺窗口强制服务端装饰；niri 尊重客户端请求，但在服务端模式下
只绘制焦点环或边框，而不是标题栏；Mutter 和 Weston 根本不提供装饰管理器，
因此会使用 Pt 的标题栏。全屏会在任何模式下隐藏标题栏，平铺窗口会放弃圆角
和阴影。无边框客户端窗口保留 6 px 边缘热区，因此仍可用鼠标调整大小。
各合成器的证据和已知怪癖编号见
[compositor-matrix.md](docs/compositor-matrix.md)。

## 许可证

Apache License 2.0。随附的 Wayland 协议 XML 保留其 MIT 头文件；
参见 [NOTICE](NOTICE) 和 [THIRD_PARTY.md](THIRD_PARTY.md)。
