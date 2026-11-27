# 协议扩展：激活与外部顶层窗口

[English](../protocol-extensions.md) | 简体中文

状态：2.4.0 已交付。两个可选的 Wayland 协议扩展了窗口生命周期，但不改变
文本、布局或绘制路径：**xdg-activation-v1**（staging）请求合成器激活一个
顶层窗口，**xdg-foreign-unstable-v2**（unstable，实验性）把一个顶层窗口链接
到另一进程中的父窗口。本文档描述 `include/pt/window.h` 中已冻结的契约；
[m26-design.md](../m26-design.md) 记录设计输入，
[compositor-matrix.md](../compositor-matrix.md) 记录实测证据与降级行。

相关阅读：[bindings.md](bindings.md) 介绍绑定层的回调与字符串规则，
[standards.md](../standards.md) 第 8 节介绍可选全局的协商，
[architecture.md](architecture.md) 说明平台层在工具包中的位置。

## 尽力而为的设计

- **没有全局，就不发请求。** 每个新调用都先检查可选全局。缺少
  `xdg_activation_v1`、`zxdg_exporter_v2` 或 `zxdg_importer_v2` 时，它在创建
  任何协议对象之前返回 `false`（或静默 no-op）。无头平台走同一条路径。
- **应用无需分支。** 应用可以无条件调用这些 API：缺少协议只是行为降级，
  不是错误。窗口仍然作为普通顶层窗口打开。
- **请求而非承诺。** `pt_window_activate` 只报告请求已发送。合成器可以忽略
  过期令牌；Pt 无法观察焦点是否真的移动。观察到的键盘焦点请使用焦点回调。
- **字符串是借出的。** 传给回调的令牌或句柄只在本次调用期间有效。如果它
  需要活得更久，请复制；空字符串表示合成器返回了无效值。
- **拆除是幂等的。** 没有导出或导入时，
  `pt_window_unexport_foreign_handle` 与
  `pt_window_clear_foreign_parent` 都是 no-op；窗口销毁会释放全部对象。

## 激活令牌（xdg-activation-v1，staging）

启动方请求一个一次性令牌，并通过 `XDG_ACTIVATION_TOKEN` 或带外通道（例如
D-Bus 的 `activation-token` 字段）把它交给应当被激活的客户端。接收方调用
`pt_window_activate`。令牌对象是异步的：请求在 `done` 之前就返回，并且每个
窗口同时只能有一个请求在途。

- `pt_window_request_activation_token` 创建令牌对象。`app_id` 可以为 NULL；
  非 NULL 时它命名将被激活的客户端。请求在存在最近一次指针按键或键盘按下
  的 serial 时会附带它，否则省略；因此要求近期 serial 的合成器仍可能拒绝
  没有输入事件时发起的请求。
- 回调在 UI 线程上运行一次。同时只能有一个请求在途：新的请求会销毁挂起的
  令牌对象并丢弃其回调。
- `pt_window_activate` 只发送一次令牌，不保留它。空令牌和 NULL 令牌、缺少
  全局以及无头平台都返回 `false`。
- 在回调内部再次发起请求是安全的，因为窗口字段在回调运行之前就已清空。

## 来自环境的启动令牌

启动激活是自动的，没有额外函数。第一次 registry 往返之后，如果
`xdg_activation_v1` 已绑定，平台会读取 `XDG_ACTIVATION_TOKEN` 并将其 unset，
这样它不会被子进程意外继承。第一个进入 mapped 状态的窗口恰好消费该令牌
一次；第二个窗口不会重复请求。

没有该全局时，环境变量既不会被读取也不会被 unset，因此不支持的合成器观察
不到任何行为变化。`pt_window_activate` 不消费启动令牌，所以显式处理启动的
应用仍然可以调用它。

## 快速上手：请求并激活

```c
#include <pt/pt.h>

#include <stdio.h>

static void on_token(pt_window *window, const char *token, void *user_data) {
    (void)user_data;
    /* The token is borrowed for this call. A launcher copies it and passes the
     * copy to the child out of band; here it activates the same window. */
    if (token && token[0] != '\0') {
        pt_window_activate(window, token);
    }
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.activation");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *window = pt_window_create(app, "Activation", 480, 320);
    if (!window) {
        pt_app_destroy(app);
        return 1;
    }
    pt_window_show(window);

    /* A false return means no protocol object was created: the global is
     * absent, the platform is headless, or window/callback is NULL. */
    if (!pt_window_request_activation_token(window, "dev.pt.activation",
                                            on_token, NULL)) {
        fprintf(stderr, "activation: request unavailable\n");
    }

    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

`app_id` 描述的是将被激活的客户端，不一定等于调用方。知道子进程 app id 的
启动方传入它；自激活可以传自己的 id 或 NULL。即使合成器返回空令牌，回调也
会被调用一次，因此空字符串就是“没有令牌”的信号。

## 外部顶层窗口句柄（xdg-foreign-unstable-v2，实验性）

导出方为一个顶层窗口共享一个不透明句柄；导入方把它设为另一个窗口的父窗口。
该关系与 `xdg_toplevel.set_parent` 具有相同的堆叠与定位语义。它不是嵌入，
不是控件重挂载，也不传输像素。

- `pt_window_export_foreign_handle` 创建导出对象并交付一次句柄。导出对象在
  `pt_window_unexport_foreign_handle` 或窗口销毁之前一直存活。再次导出会先
  销毁旧的导出，丢弃其挂起回调，并使基于旧句柄的每个导入失效。
- 句柄是不透明且由合成器定义的：绝不要解析、记录日志，或把它保存到导出
  对象生命周期之外。
- `pt_window_set_foreign_parent` 导入句柄并让本窗口成为其子窗口。它会替换
  之前导入的父窗口，因此一个窗口最多只有一个父窗口。NULL 或空句柄、缺少
  导入全局以及无头平台返回 `false`；合成器发送 `destroyed` 时，无效句柄会
  被静默清除，之后仍可重新导入。
- `pt_window_unexport_foreign_handle` 与 `pt_window_clear_foreign_parent`
  撤销协议对象，并且是幂等的。窗口销毁也会销毁两端；合成器异步地使导入
  失效。
- 只绑定 v2。v1 是已废弃的前身；只暴露 v1（或两者都没有）的合成器得到
  no-op 降级。

## 快速上手：导出并设置父窗口

```c
#include <pt/pt.h>

#include <stdio.h>

static void on_handle(pt_window *exporter, const char *handle, void *user_data) {
    pt_window *dialog = user_data;
    (void)exporter;
    /* The handle is opaque and borrowed. Import it before returning; another
     * process would receive the same string out of band. */
    if (handle && handle[0] != '\0') {
        pt_window_set_foreign_parent(dialog, handle);
    }
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.foreign");
    if (!app) {
        fprintf(stderr, "pt: %s\n", pt_get_last_error());
        return 1;
    }
    pt_window *caller = pt_window_create(app, "Caller", 640, 400);
    pt_window *dialog = pt_window_create(app, "Dialog", 360, 220);
    if (!caller || !dialog) {
        pt_app_destroy(app);
        return 1;
    }
    pt_window_show(caller);
    pt_window_show(dialog);

    /* A false return means there is no exporter global or the platform is
     * headless; the dialog stays an independent toplevel. */
    if (!pt_window_export_foreign_handle(caller, on_handle, dialog)) {
        fprintf(stderr, "foreign: export unavailable\n");
    }

    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

真正跨进程的对话框通过自己的 IPC 通道接收句柄，在子进程中导入并调用
`pt_window_set_foreign_parent`。上面的同客户端序列正是实测覆盖的路径；协议
并不要求两个窗口共享同一个连接。

## 所有权与生命周期

- `pt_window_request_activation_token` 与
  `pt_window_export_foreign_handle` 恰好交付一次回调。回调中的令牌或句柄
  字符串在本次调用期间借出，就像按键事件的 `utf8` 参数；回调返回后即失效。
- 句柄的有效性由导出对象而非字符串维持。取消导出或销毁窗口会销毁该对象，
  并使导入失效。
- `pt_window_activate` 与 `pt_window_set_foreign_parent` 同步读取字符串，
  不保留任何内容，因此调用返回后调用方即可释放它。
- 导入对象属于子窗口。下一次 `pt_window_set_foreign_parent` 会替换它，
  `pt_window_clear_foreign_parent` 会清除它，窗口销毁时它也随之销毁。

## 合成器支持

表中的行来自 [compositor-matrix.md](../compositor-matrix.md)。niri 26.04 为
B 级：嵌套 registry 同时公布了 activation 与 foreign v2，且实测客户端完成了
令牌请求/激活以及 export/import/set_parent_of 往返。其余行是公共支持矩阵的
C 级。Weston 在本快照中缺少这两个全局。

| 合成器（版本） | xdg-activation | xdg-foreign v2 | Pt 行为 |
| --- | --- | --- | --- |
| niri 26.04 | 1（实测 B） | 1（实测 B） | 发送请求 |
| Mutter 51 (GNOME) | 1 | 1 | 发送请求 |
| KWin 6.7 (Plasma) | 1 | 1 | 发送请求 |
| Sway 1.11 (wlroots) | 1 | 1 | 发送请求 |
| Hyprland 0.57.2 | 1 | 1 | 发送请求 |
| COSMIC 1.11.0 | 1 | 1 | 发送请求 |
| Weston 15.0.1 | — | — | false / no-op |
| Muffin 6.7.4 (Cinnamon) | 1 | 1 | 发送请求 |

可用不等于有效：已绑定的全局仍可能忽略激活请求或拒绝导入。降级是行为层面
的退让，不是错误，也从不改变像素、damage 或输入处理。

## 绑定

C++ 把这六个调用暴露为 `pt::window` 方法。令牌与句柄输入使用
`std::string_view`；包装层在调用 C 函数之前复制它们，因此 view 无需活过
本次调用。回调保持为原始 `pt_*_callback` 指针，沿用相同的 `user_data`
约定，并且从不保留借出的令牌或句柄。

Python 暴露 `request_activation_token`、`activate`、
`export_foreign_handle`、`unexport_foreign_handle`、`set_foreign_parent` 和
`clear_foreign_parent`。它的 `CFUNCTYPE` 跳板会留在回调根集合中直到窗口
销毁，因此稍后到达的令牌或句柄不会调用已被回收的函数；回调参数是仅在调用
期间有效的 Python `str`。

Java 用 `ActivationTokenHandler` 与 `ForeignHandleHandler` 函数式接口暴露
同样的六个方法。upcall 解码出回调作用域内的 `String`，传给请求的 `Arena`
必须活过回调，这与绑定中其他所有 Java 回调一致。

## 示例

- [C 协议扩展](../../examples/c/protocol_extensions.c)
- [C++ 协议扩展](../../examples/cpp/protocol_extensions.cpp)
- [Python 协议扩展](../../examples/python/protocol_extensions.py)
- [Java 协议扩展](../../examples/java/ProtocolExtensions.java)

每个示例都会请求激活令牌、导出顶层窗口句柄并在第二个窗口上导入它。设置
`PT_HEADLESS=1` 后，无需合成器即可运行降级检查：每个请求都返回 `false`，
回调从不被调用，幂等拆除调用是安全的，进程以 0 退出。只有在合成器提供这些
全局时才会走实机路径。

## 兼容性与限制

- 作为六个函数和两个回调在 2.4.0 追加。枚举、结构体和 define 均不改变；
  `ABI_MAJOR` 保持 1，SONAME 保持 `libpt.so.1`。
- xdg-activation 是 **staging**，不是稳定协议。不兼容的变更需要新的主接口
  版本，并且会是一次新设计，而不是静默升级。
- xdg-foreign 是 **unstable 且实验性**的。协议可能变化；不支持 v1。句柄
  不能跨合成器移植，也不得持久化。
- 激活从不保证焦点，xdg-foreign 也从不嵌入像素、重挂载控件或传输输入。
  两者都只是顶层窗口层面的请求。
- 在不支持或无头的平台上，每个调用都返回 `false` 或 no-op；不会创建协议
  对象，也不会改变任何渲染路径。