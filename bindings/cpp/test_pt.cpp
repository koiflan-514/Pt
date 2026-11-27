/*
 * Headless tests for the header-only C++ binding.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include <pt/pt.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

/* Allocation-failure injection for the owned-API hardening test (M20). The
 * budget stays -1 (disabled) for the whole suite except inside
 * run_bad_alloc_hardening_test(), so the rest of the checks see the normal
 * allocator. */
namespace {
std::atomic<long long> allocation_budget{-1};
std::atomic<long long> allocation_attempts{0};
} // namespace

/* The replacement allocator intentionally pairs operator new with malloc and
 * operator delete with free; GCC's cross-function check cannot see that and
 * reports a false mismatch at inline call sites. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void *operator new(std::size_t size) {
    const long long budget = allocation_budget.load(std::memory_order_relaxed);
    if (budget >= 0) {
        const long long attempt = allocation_attempts.fetch_add(1, std::memory_order_relaxed);
        if (attempt >= budget) {
            throw std::bad_alloc();
        }
    }
    if (void *memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

/* Internal UI-thread hooks. The C suite uses the same entry points to drive
 * synthetic compositor input; they are not part of the public ABI, but the
 * binding test links the static library so raw focus/key/pointer and IME
 * dispatch can be observed without a compositor. Declared locally so the
 * binding itself does not depend on <src/internal.h>. */
extern "C" {
void pt_window_input_pointer_enter(pt_window *window, float x, float y);
void pt_window_input_pointer_motion(pt_window *window, float x, float y, uint32_t time);
void pt_window_input_pointer_leave(pt_window *window);
void pt_window_input_pointer_button(pt_window *window, uint32_t button, bool pressed, float x,
                                    float y, uint32_t serial, uint32_t time);
void pt_window_input_axis(pt_window *window, float x, float y, float delta);
void pt_window_input_keyboard_focus(pt_window *window, bool focused, uint32_t serial);
void pt_window_input_key(pt_window *window, uint32_t keysym, uint32_t modifiers,
                         const char *utf8, bool pressed);
void pt_window_input_ime_compose(pt_window *window, const char *commit, const char *preedit,
                                 int32_t cursor_begin, int32_t cursor_end,
                                 uint32_t delete_before, uint32_t delete_after);
/* Synthetic compositor capability update; used to exercise the maximize
 * failure path without a live xdg_toplevel configure. */
void pt_window_apply_wm_capabilities(pt_window *window, bool maximize, bool minimize,
                                     bool fullscreen);
/* Fake-compositor wire driver (tests/wire_driver.c). The driver is compiled as
 * C because tests/fake_compositor.h is C-only; this test links the object so
 * a real Wayland key/pointer event can reach the C++ wrapper. */
int wd_start(unsigned int options);
void wd_stop(void);
const char *wd_error(void);
int wd_sync(void *app);
int wd_settle(void *app);
int wd_wait_configured(void *app, void *window, int attempts);
int wd_wait_object(void *app, uint32_t kind, int present, int attempts);
uint32_t wd_toplevel_surface(void);
uint32_t wd_live_id(uint32_t kind);
int wd_global_bound(uint32_t kind);
int wd_request_count(uint32_t kind, uint32_t opcode);
int wd_request_overflowed(void);
int wd_pending_fd_count(void);
int wd_activation_token_done(const char *token);
int wd_exported_handle(const char *handle);
void wd_keyboard_enter(uint32_t serial, uint32_t surface);
void wd_keyboard_leave(uint32_t serial, uint32_t surface);
void wd_keyboard_key(uint32_t serial, uint32_t time, uint32_t keycode, int pressed);
void wd_keyboard_modifiers(uint32_t serial, uint32_t depressed, uint32_t latched,
                           uint32_t locked, uint32_t group);
void wd_pointer_enter(uint32_t serial, uint32_t surface, double x, double y);
void wd_pointer_leave(uint32_t serial, uint32_t surface);
void wd_pointer_motion(uint32_t time, double x, double y);
void wd_pointer_button(uint32_t serial, uint32_t time, uint32_t button, int pressed);
void wd_pointer_axis(uint32_t time, uint32_t axis, double value);
/* Headless offscreen canvas from src/internal.h, declared locally for the 1.2
 * picker and popup sections. Tests only paint through it; the binding itself
 * never exposes it. */
struct pt_canvas;
pt_canvas *pt_canvas_create(int width, int height, float scale);
void pt_canvas_destroy(pt_canvas *canvas);
bool pt_window_paint(pt_window *window, pt_canvas *canvas);
}

namespace {

int checks = 0;
int failures = 0;

#define check(expression)                                                                        \
    do {                                                                                         \
        ++checks;                                                                                \
        if (!(expression)) {                                                                     \
            ++failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);           \
        }                                                                                        \
    } while (0)

void on_timer(pt_app *app, void *data) {
    (void)app;
    *static_cast<bool *>(data) = true;
}

bool drop_enter(pt_widget *widget, const char *mime_type, void *data) {
    (void)widget;
    (void)mime_type;
    (void)data;
    return false;
}

void drop_motion(pt_widget *widget, float x, float y, void *data) {
    (void)widget;
    (void)x;
    (void)y;
    (void)data;
}

void drop_leave(pt_widget *widget, void *data) {
    (void)widget;
    (void)data;
}

void drop_receive(pt_widget *widget, const char *mime_type, const char *text, float x, float y,
                  void *data) {
    (void)widget;
    (void)mime_type;
    (void)text;
    (void)x;
    (void)y;
    (void)data;
}

struct offer_capture {
    uint32_t mime_count = 0;
    uint32_t actions = 0;
    size_t finished_size = 0;
    bool still_active = true;
    bool dropped = false;
    std::string selected;
    std::string payload;
};

void offer_stream(pt_widget *widget, pt_drop_stream *raw_stream, pt::drop_stream_state state,
                  const char *chunk, size_t length, void *data) {
    (void)widget;
    auto *capture = static_cast<offer_capture *>(data);
    const pt::drop_stream stream(raw_stream);
    if (state == pt_drop_stream_data) {
        capture->payload.append(chunk, length);
        return;
    }
    capture->finished_size = stream.size();
    capture->still_active = stream.active();
}

const char *offer_enter(pt_widget *widget, pt_drop_offer *raw_offer, void *data) {
    (void)widget;
    auto *capture = static_cast<offer_capture *>(data);
    const pt::drop_offer offer(raw_offer);
    capture->mime_count = offer.mime_count();
    capture->actions = offer.actions();
    capture->selected.clear();
    if (offer.has_mime("text/plain;charset=utf-8")) {
        capture->selected = "text/plain;charset=utf-8";
    } else if (offer.mime_count() > 0) {
        capture->selected = std::string(offer.mime(0));
    }
    return capture->selected.empty() ? nullptr : capture->selected.c_str();
}

void offer_drop(pt_widget *widget, pt_drop_offer *raw_offer, const char *mime_type, float x,
                float y, void *data) {
    (void)widget;
    (void)x;
    (void)y;
    auto *capture = static_cast<offer_capture *>(data);
    const pt::drop_offer offer(raw_offer);
    capture->dropped = true;
    const std::string_view wanted =
        mime_type ? std::string_view(mime_type) : offer.selected_mime();
    const pt::drop_stream stream = offer.read(wanted, 4096, offer_stream, capture);
    if (stream.active()) {
        stream.cancel();
    }
}

void drag_finished(pt_window *window, bool dropped, pt_drag_action action, void *data) {
    (void)window;
    (void)action;
    *static_cast<bool *>(data) = dropped;
}

void on_gesture(pt_window *window, const pt_gesture_event *event, void *data) {
    (void)window;
    (void)event;
    (void)data;
}

void bind_row(pt_widget *list, pt_widget *row, uint32_t index, void *data) {
    (void)list;
    (void)data;
    char text[32];
    std::snprintf(text, sizeof(text), "row %u", index);
    pt_widget_set_text(row, text);
}

void on_row(pt_widget *list, uint32_t index, void *data) {
    (void)list;
    *static_cast<uint32_t *>(data) = index;
}

void on_combo_text(pt_widget *combobox, const char *text, void *data) {
    (void)combobox;
    *static_cast<std::string *>(data) = text ? text : "";
}

void bind_cell(pt_widget *table, pt_widget *cell, uint32_t row, uint32_t column, void *data) {
    (void)table;
    (void)data;
    char text[48];
    std::snprintf(text, sizeof(text), "%u:%u", row, column);
    pt_widget_set_text(cell, text);
}

void on_sort(pt_widget *table, uint32_t column, pt_sort_direction direction, void *data) {
    (void)table;
    (void)direction;
    *static_cast<uint32_t *>(data) = column;
}

void on_tree(pt_widget *tree, pt_tree_node *node, void *data) {
    (void)tree;
    *static_cast<pt_tree_node **>(data) = node;
}

void on_stylus(pt_window *window, const pt_stylus_event *event, void *data) {
    (void)window;
    (void)event;
    (void)data;
}

uint64_t row_key(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    (void)data;
    return 1000u + row * 10u;
}

const char *row_text(pt_widget *view, uint32_t row, void *data) {
    (void)view;
    (void)data;
    static char text[32];
    std::snprintf(text, sizeof(text), "item %u", row);
    return text;
}

struct sort_capture {
    uint32_t count = 0;
    uint32_t columns[PT_TABLE_MAX_SORT_COLUMNS] = {};
    pt_sort_direction directions[PT_TABLE_MAX_SORT_COLUMNS] = {};
};

void on_sort_columns(pt_widget *table, uint32_t count, const uint32_t *columns,
                     const pt_sort_direction *directions, void *data) {
    (void)table;
    auto *capture = static_cast<sort_capture *>(data);
    capture->count = count;
    for (uint32_t i = 0; i < count && i < PT_TABLE_MAX_SORT_COLUMNS; ++i) {
        capture->columns[i] = columns[i];
        capture->directions[i] = directions[i];
    }
}

struct edit_capture {
    uint32_t row = pt_list_index_none;
    uint32_t column = pt_table_column_none;
    std::string text;
};

struct reorder_capture {
    uint32_t from = pt_table_column_none;
    uint32_t to = pt_table_column_none;
};

struct rename_capture {
    pt_tree_node *node = nullptr;
    std::string text;
};

void on_cell_edited(pt_widget *table, uint32_t row, uint32_t column, const char *text,
                    void *data) {
    (void)table;
    auto *capture = static_cast<edit_capture *>(data);
    capture->row = row;
    capture->column = column;
    capture->text = text ? text : "";
}

void on_columns_reordered(pt_widget *table, uint32_t from, uint32_t to, void *data) {
    (void)table;
    auto *capture = static_cast<reorder_capture *>(data);
    capture->from = from;
    capture->to = to;
}

void on_row_drag(pt_widget *table, uint32_t row, void *data) {
    (void)table;
    *static_cast<uint32_t *>(data) = row;
}

void on_tree_rename(pt_widget *tree, pt_tree_node *node, const char *text, void *data) {
    (void)tree;
    auto *capture = static_cast<rename_capture *>(data);
    capture->node = node;
    capture->text = text ? text : "";
}

/* ---- 1.1 extension probes: custom paint, raw input, IME, fd watches ---- */

constexpr pt_color paint_fill = 0xAA3300FFu; // pt_rgb(0xAA3300)

struct paint_capture {
    int calls = 0;
    pt_widget *widget = nullptr;
    float width = 0.0f;
    float height = 0.0f;
    float scale = 0.0f;
    bool has_clip = false;
    pt::rect clip{};
    float measured = 0.0f;
    bool has_metrics = false;
    pt::font_metrics metrics{};
    bool glyph = false;
};

void paint_probe(pt_widget *widget, pt_paint_context *raw, void *data) {
    auto *capture = static_cast<paint_capture *>(data);
    pt::paint_context context(raw);
    capture->calls += 1;
    capture->widget = widget;
    capture->width = context.width();
    capture->height = context.height();
    capture->scale = context.scale();
    if (const auto clip = context.clip()) {
        capture->has_clip = true;
        capture->clip = *clip; // copy while the context is borrowed
    }
    capture->measured =
        context.measure_text("sans", 12.0f, pt_font_weight_regular, pt_font_slant_upright, "Pt");
    if (const auto metrics = context.metrics("sans", 12.0f, pt_font_weight_regular,
                                             pt_font_slant_upright)) {
        capture->has_metrics = true;
        capture->metrics = *metrics;
    }
    capture->glyph =
        context.font_has_glyph("sans", 12.0f, static_cast<uint32_t>('P'));
    context.push_clip(0.0f, 0.0f, 40.0f, 20.0f)
        .fill_rect(0.0f, 0.0f, 40.0f, 20.0f, paint_fill)
        .pop_clip()
        .fill_rounded_rect(0.0f, 0.0f, 8.0f, 8.0f, 2.0f, pt_rgb(0x00AA33))
        .stroke_rect(1.0f, 1.0f, 6.0f, 6.0f, 1.0f, pt_rgb(0x0033AA))
        .draw_line(0.0f, 0.0f, 4.0f, 4.0f, 1.0f, pt_rgb(0x333333))
        .draw_text("sans", 12.0f, pt_font_weight_regular, pt_font_slant_upright, 2.0f, 10.0f,
                   "Pt", pt_rgb(0x111111));
}

struct focus_capture {
    pt_window *window = nullptr;
    bool focused = false;
    int calls = 0;
};

void focus_probe(pt_window *window, bool focused, void *data) {
    auto *capture = static_cast<focus_capture *>(data);
    capture->window = window;
    capture->focused = focused;
    capture->calls += 1;
}

struct key_event_capture {
    pt_window *window = nullptr;
    uint32_t key = 0;
    uint32_t modifiers = 0;
    std::string text;
    int calls = 0;
    bool consume = true;
};

bool key_event_probe(pt_window *window, uint32_t key, uint32_t modifiers, const char *utf8,
                     void *data) {
    auto *capture = static_cast<key_event_capture *>(data);
    capture->window = window;
    capture->key = key;
    capture->modifiers = modifiers;
    capture->text = utf8 ? utf8 : ""; // copy: payload is borrowed
    capture->calls += 1;
    return capture->consume;
}

struct legacy_key_capture {
    int calls = 0;
    bool consume = true;
};

bool legacy_key_probe(pt_window *window, uint32_t key, uint32_t modifiers, void *data) {
    (void)window;
    (void)key;
    (void)modifiers;
    auto *capture = static_cast<legacy_key_capture *>(data);
    ++capture->calls;
    return capture->consume;
}

struct pointer_capture {
    pt_window *window = nullptr;
    std::vector<pt_pointer_event> events; // each entry is a copy of the payload
    bool consume = true;
};

bool pointer_probe(pt_window *window, const pt_pointer_event *event, void *data) {
    auto *capture = static_cast<pointer_capture *>(data);
    capture->window = window;
    capture->events.push_back(*event); // copy: payload is borrowed
    return capture->consume;
}

struct ime_capture {
    pt_window *window = nullptr;
    std::string commit;
    std::string preedit;
    int32_t begin = -2;
    int32_t end = -2;
    int commits = 0;
    int preedits = 0;
};

void ime_commit_probe(pt_window *window, const char *text, void *data) {
    auto *capture = static_cast<ime_capture *>(data);
    capture->window = window;
    capture->commit = text ? text : ""; // copy: payload is borrowed
    capture->commits += 1;
}

void ime_preedit_probe(pt_window *window, const char *text, int32_t cursor_begin,
                       int32_t cursor_end, void *data) {
    auto *capture = static_cast<ime_capture *>(data);
    capture->window = window;
    capture->preedit = text ? text : "";
    capture->begin = cursor_begin;
    capture->end = cursor_end;
    capture->preedits += 1;
}

struct fd_capture {
    int calls = 0;
    uint32_t last = 0;
};

void fd_probe(pt_app *app, uint32_t events, void *data) {
    (void)app;
    auto *capture = static_cast<fd_capture *>(data);
    capture->calls += 1;
    capture->last = events;
}

struct fd_self_capture {
    pt_app *app = nullptr;
    uint64_t watch_id = 0;
    int calls = 0;
    uint32_t last = 0;
};

void fd_self_probe(pt_app *app, uint32_t events, void *data) {
    auto *capture = static_cast<fd_self_capture *>(data);
    capture->calls += 1;
    capture->last = events;
    pt_app_unwatch_fd(app, capture->watch_id);
}

/* ---- 1.2 M19 probes: popup, icons, pickers, persistence ---- */

/* 2x2 opaque-red PNG used to exercise the raster icon path without depending
 * on the host's icon theme. */
constexpr uint8_t icon_png[] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 2, 0, 0, 0, 2, 8, 6,
    0, 0, 0, 114, 182, 13, 36, 0, 0, 0, 17, 73, 68, 65, 84, 120, 156, 99, 248, 207, 192, 240,
    31, 132, 25, 96, 12, 0, 71, 202, 7, 249, 103, 89, 110, 183, 0, 0, 0, 0, 73, 69, 78, 68,
    174, 66, 96, 130,
};

void click_at(pt_window *window, float x, float y) {
    pt_window_input_pointer_enter(window, x, y);
    pt_window_input_pointer_button(window, 0x110u, true, x, y, 1, 10);
    pt_window_input_pointer_button(window, 0x110u, false, x, y, 2, 20);
}

void type_key(pt_window *window, uint32_t keysym, const char *utf8 = nullptr) {
    pt_window_input_key(window, keysym, 0, utf8, true);
}

struct popup_probe {
    int dismissals = 0;
    bool open_at_dismiss = true;
};

void popup_dismissed(pt_popup *popup, void *data) {
    auto *probe = static_cast<popup_probe *>(data);
    probe->dismissals += 1;
    probe->open_at_dismiss = pt_popup_is_open(popup);
}

struct select_probe {
    int changes = 0;
    uint32_t index = pt_list_index_none;
};

void select_changed(pt_widget *widget, uint32_t index, void *data) {
    (void)widget;
    auto *probe = static_cast<select_probe *>(data);
    probe->changes += 1;
    probe->index = index;
}

struct date_probe {
    int changes = 0;
    pt_date value{};
};

void date_changed(pt_widget *widget, pt_date value, void *data) {
    (void)widget;
    auto *probe = static_cast<date_probe *>(data);
    probe->changes += 1;
    probe->value = value;
}

struct number_probe {
    int changes = 0;
    double value = 0.0;
};

void number_changed(pt_widget *widget, double value, void *data) {
    (void)widget;
    auto *probe = static_cast<number_probe *>(data);
    probe->changes += 1;
    probe->value = value;
}

struct color_probe {
    int changes = 0;
    pt_color value = 0;
};

void color_changed(pt_widget *widget, pt_color color, void *data) {
    (void)widget;
    auto *probe = static_cast<color_probe *>(data);
    probe->changes += 1;
    probe->value = color;
}

/* M26 2.4 callback contracts. Both typedefs receive the window, a string that
 * is borrowed only for the call, and the user data pointer. The probes copy the
 * string inside the callback, which is the documented way to keep it. */
static_assert(std::is_same_v<pt_activation_token_callback,
                             void (*)(pt_window *, const char *, void *)>);
static_assert(std::is_same_v<pt_foreign_handle_callback,
                             void (*)(pt_window *, const char *, void *)>);

struct activation_token_probe {
    int calls = 0;
    pt_window *window = nullptr;
    std::string token;
};

void activation_token_ready(pt_window *window, const char *token, void *data) {
    auto *probe = static_cast<activation_token_probe *>(data);
    probe->calls += 1;
    probe->window = window;
    probe->token = token ? token : ""; // copy while the string is borrowed
}

struct foreign_handle_probe {
    int calls = 0;
    pt_window *window = nullptr;
    std::string handle;
};

void foreign_handle_ready(pt_window *window, const char *handle, void *data) {
    auto *probe = static_cast<foreign_handle_probe *>(data);
    probe->calls += 1;
    probe->window = window;
    probe->handle = handle ? handle : ""; // copy while the string is borrowed
}

/* End-to-end wire test: a private Wayland compositor injects real key and
 * pointer events, the C library translates them, and the header-only C++
 * wrapper's raw callbacks observe them. This is the live-seat replacement for
 * M19's "binding raw-callback dispatch still needs a live seat" item. */
void run_wire_dispatch_test() {
    const char *saved_headless = getenv("PT_HEADLESS");
    std::string saved_value = saved_headless ? saved_headless : "";
    const bool had_headless = saved_headless != nullptr;
    unsetenv("PT_HEADLESS");

    /* FC_OPT_ALL from tests/fake_compositor.h: mandatory globals plus every
     * optional one, so the client binds its normal full runtime path. */
    constexpr unsigned int wire_options = 0x1ffu;
    check(wd_start(wire_options) == 0);
    if (const char *error = wd_error()) {
        std::fprintf(stderr, "wire driver start failed: %s\n", error);
        if (had_headless) {
            setenv("PT_HEADLESS", saved_value.c_str(), 1);
        }
        return;
    }

    {
        pt::app wire_application("pt.tests.cpp.wire");
        check(wire_application.valid());
        if (wire_application.valid()) {
            check(wd_settle(wire_application.get()) == 0);
            check(wd_error() == nullptr);

            pt::window wire_window(wire_application, "cpp wire", 320, 240);
            check(wire_window.valid());
            pt::widget wire_root = wire_window.root();
            pt::widget wire_entry = pt::entry(wire_root, "");
            check(wire_entry.focus());

            key_event_capture wire_key;
            legacy_key_capture wire_legacy;
            pointer_capture wire_pointer;
            wire_key.consume = false;
            wire_legacy.consume = false; // let the focused entry see the text
            wire_pointer.consume = false;
            wire_window.on_key_event(key_event_probe, &wire_key)
                .on_key(legacy_key_probe, &wire_legacy)
                .on_pointer(pointer_probe, &wire_pointer);

            wire_window.show();
            check(wd_wait_configured(wire_application.get(), wire_window.get(), 300) == 1);
            check(wd_settle(wire_application.get()) == 0);

            const uint32_t wire_surface = wd_toplevel_surface();
            check(wire_surface != 0);

            /* Keyboard: enter, keymap and modifiers arrive over the wire, then
             * a press of KEY_A produces the keysym, modifiers and text the C++
             * callbacks report, and the focused entry receives the character. */
            wd_keyboard_enter(7100u, wire_surface);
            wd_keyboard_modifiers(7101u, 0u, 0u, 0u, 0u);
            wd_keyboard_key(7102u, 20u, 30u, 1); /* KEY_A press */
            check(wd_sync(wire_application.get()) == 0);
            check(wire_key.calls == 1);
            check(wire_key.window == wire_window.get());
            check(wire_key.key == static_cast<uint32_t>(XKB_KEY_a));
            check(wire_key.modifiers == 0u);
            check(wire_key.text == "a");
            check(wire_legacy.calls == 1);
            check(wire_entry.text() == "a");

            /* Shift modifier over the wire: the raw callback sees the shift key
             * modifier and the shifted keysym/text. */
            wd_keyboard_modifiers(7103u, 1u, 0u, 0u, 0u); /* depressed = shift */
            wd_keyboard_key(7104u, 21u, 30u, 1);        /* KEY_A with shift */
            check(wd_sync(wire_application.get()) == 0);
            check(wire_key.calls == 2);
            check(wire_key.key == static_cast<uint32_t>(XKB_KEY_A));
            check(wire_key.modifiers == static_cast<uint32_t>(pt_key_modifier_shift));
            check(wire_key.text == "A");
            check(wire_legacy.calls == 2);
            check(wire_entry.text() == "aA");
            wd_keyboard_key(7105u, 22u, 30u, 0); /* release */
            wd_keyboard_modifiers(7106u, 0u, 0u, 0u, 0u);
            check(wd_sync(wire_application.get()) == 0);
            wd_keyboard_leave(7107u, wire_surface);

            /* Pointer: enter, motion, down/up and axis reach the raw callback
             * with wire coordinates, button, click count and deltas. */
            wd_pointer_enter(7001u, wire_surface, 40.0, 50.0);
            check(wd_sync(wire_application.get()) == 0);
            check(wire_pointer.events.size() == 1);
            if (wire_pointer.events.size() == 1) {
                check(wire_pointer.events[0].phase == pt_pointer_phase_enter);
                check(wire_pointer.events[0].x == 40.0f);
                check(wire_pointer.events[0].y == 50.0f);
            }
            wd_pointer_motion(17u, 60.0, 80.0);
            check(wd_sync(wire_application.get()) == 0);
            check(wire_pointer.events.size() == 2);
            if (wire_pointer.events.size() == 2) {
                check(wire_pointer.events[1].phase == pt_pointer_phase_motion);
                check(wire_pointer.events[1].x == 60.0f);
                check(wire_pointer.events[1].y == 80.0f);
                check(wire_pointer.events[1].delta_x == 20.0f);
                check(wire_pointer.events[1].delta_y == 30.0f);
            }
            wd_pointer_button(7002u, 18u, 0x110u, 1); /* BTN_LEFT down */
            check(wd_sync(wire_application.get()) == 0);
            check(wire_pointer.events.size() == 3);
            if (wire_pointer.events.size() == 3) {
                check(wire_pointer.events[2].phase == pt_pointer_phase_down);
                check(wire_pointer.events[2].button == 0x110u);
                check(wire_pointer.events[2].pressed);
                check(wire_pointer.events[2].click_count == 1u);
            }
            wd_pointer_button(7003u, 19u, 0x110u, 0); /* BTN_LEFT up */
            check(wd_sync(wire_application.get()) == 0);
            check(wire_pointer.events.size() == 4);
            if (wire_pointer.events.size() == 4) {
                check(wire_pointer.events[3].phase == pt_pointer_phase_up);
                check(wire_pointer.events[3].button == 0x110u);
                check(!wire_pointer.events[3].pressed);
            }
            wd_pointer_axis(20u, 0u, 150.0); /* vertical scroll, 150/10 = 15 */
            check(wd_sync(wire_application.get()) == 0);
            check(wire_pointer.events.size() == 5);
            if (wire_pointer.events.size() == 5) {
                check(wire_pointer.events[4].phase == pt_pointer_phase_axis);
                check(wire_pointer.events[4].delta_y == 15.0f);
            }
            wd_pointer_leave(7004u, wire_surface);
            check(wd_sync(wire_application.get()) == 0);
            check(wire_pointer.events.size() == 6);
            if (wire_pointer.events.size() == 6) {
                check(wire_pointer.events[5].phase == pt_pointer_phase_leave);
            }

            check(wd_request_overflowed() == 0);
            check(wd_pending_fd_count() == 0);
            check(wd_error() == nullptr);
        }
    }
    wd_stop();
    if (had_headless) {
        setenv("PT_HEADLESS", saved_value.c_str(), 1);
    } else {
        unsetenv("PT_HEADLESS");
    }
}

/* Wire support path for the six 2.4 methods. The fake-compositor driver exposes
 * request counters, live-object queries and the two event injectors, so this
 * section covers both directions: the globals bind, each call emits the
 * expected request, token/handle delivery reaches the callback with an owned
 * copy, a repeated request destroys the previous object, and unexport/clear
 * are idempotent. */
void run_activation_foreign_wire_test() {
    const char *saved_headless = getenv("PT_HEADLESS");
    std::string saved_value = saved_headless ? saved_headless : "";
    const bool had_headless = saved_headless != nullptr;
    unsetenv("PT_HEADLESS");

    /* FC_OPT_ALL | FC_OPT_ACTIVATION | FC_OPT_FOREIGN. The two M26 options are
     * deliberately outside FC_OPT_ALL, so only this session advertises them. */
    constexpr unsigned int wire_options = 0x1ffu | (1u << 11) | (1u << 12);
    /* fc_kind values from tests/fake_compositor.h. New kinds are appended
     * before FC_K_COUNT, so these indices stay stable. */
    constexpr uint32_t kind_activation = 44;
    constexpr uint32_t kind_token = 45;
    constexpr uint32_t kind_exporter = 46;
    constexpr uint32_t kind_exported = 47;
    constexpr uint32_t kind_importer = 48;
    constexpr uint32_t kind_imported = 49;

    check(wd_start(wire_options) == 0);
    if (const char *error = wd_error()) {
        std::fprintf(stderr, "wire driver start failed: %s\n", error);
        if (had_headless) {
            setenv("PT_HEADLESS", saved_value.c_str(), 1);
        }
        return;
    }

    {
        pt::app wire_application("pt.tests.cpp.m26");
        check(wire_application.valid());
        if (wire_application.valid()) {
            check(wd_settle(wire_application.get()) == 0);
            pt::window wire_window(wire_application, "cpp m26 wire", 240, 160);
            check(wire_window.valid());
            wire_window.show();
            check(wd_wait_configured(wire_application.get(), wire_window.get(), 300) == 1);
            check(wd_settle(wire_application.get()) == 0);
            check(wd_global_bound(kind_activation) == 1);
            check(wd_global_bound(kind_exporter) == 1);
            check(wd_global_bound(kind_importer) == 1);

            activation_token_probe token_probe;
            foreign_handle_probe handle_probe;

            /* A NULL callback is rejected before any protocol object exists. */
            check(!wire_window.request_activation_token("pt.tests.cpp", nullptr,
                                                        &token_probe));
            check(!wire_window.export_foreign_handle(nullptr, &handle_probe));
            check(wd_live_id(kind_token) == 0);
            check(wd_live_id(kind_exported) == 0);

            /* A token request queues get_activation_token, set_app_id and
             * commit; delivery waits for the compositor's done event. */
            check(wire_window.request_activation_token("pt.tests.cpp",
                                                       activation_token_ready,
                                                       &token_probe));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_activation, 1) == 1); // get_activation_token
            check(wd_request_count(kind_token, 1) == 1);      // set_app_id
            check(wd_request_count(kind_token, 3) == 1);      // commit
            check(wd_live_id(kind_token) != 0);
            check(token_probe.calls == 0);

            /* A second request destroys the pending token (opcode 4) and drops
             * the previous callback; an empty app_id omits set_app_id. */
            check(wire_window.request_activation_token("", activation_token_ready,
                                                       &token_probe));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_activation, 1) == 2);
            check(wd_request_count(kind_token, 4) == 1);
            check(wd_request_count(kind_token, 3) == 2);
            check(wd_request_count(kind_token, 1) == 1);
            check(token_probe.calls == 0);

            /* The done event delivers the borrowed token to the callback; the
             * wrapper copies it into an owned std::string, so the value stays
             * valid after the wire buffer is recycled. */
            check(wd_activation_token_done("cpp-token") == 1);
            check(wd_sync(wire_application.get()) == 0);
            check(token_probe.calls == 1);
            check(token_probe.window == wire_window.get());
            check(token_probe.token == "cpp-token");
            /* The token object is gone after delivery, so a late done event is
             * a no-op rather than a stale callback. The destroy request is
             * queued while the done event dispatches, so it only reaches the
             * server on a later flush: wait for the live table first, then
             * inject the late event. */
            check(wd_wait_object(wire_application.get(), 45, 0, 8) == 1 &&
                  wd_live_id(45) == 0);
            check(wd_activation_token_done("late-token") == 0);
            check(wd_sync(wire_application.get()) == 0);
            check(token_probe.calls == 1);

            /* activate sends the token once; an empty token is a local reject. */
            check(wire_window.activate("opaque-token"));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_activation, 2) == 1);
            check(!wire_window.activate(""));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_activation, 2) == 1);

            /* Export; re-export destroys the old object; unexport revokes the
             * current one and is idempotent. */
            check(wire_window.export_foreign_handle(foreign_handle_ready,
                                                    &handle_probe));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_exporter, 1) == 1);
            check(wd_live_id(kind_exported) != 0);
            check(handle_probe.calls == 0);
            /* The exported-handle event delivers the borrowed handle; the
             * callback copies it into an owned std::string. A late event after
             * unexport finds no live object and stays a no-op. */
            check(wd_exported_handle("cpp-handle") == 1);
            check(wd_sync(wire_application.get()) == 0);
            check(handle_probe.calls == 1);
            check(handle_probe.window == wire_window.get());
            check(handle_probe.handle == "cpp-handle");
            check(wire_window.export_foreign_handle(foreign_handle_ready,
                                                    &handle_probe));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_exporter, 1) == 2);
            check(wd_request_count(kind_exported, 0) == 1);
            /* Re-export installed a fresh callback/object; a second handle
             * event reaches it and updates the owned copy. */
            check(wd_exported_handle("cpp-handle-2") == 1);
            check(wd_sync(wire_application.get()) == 0);
            check(handle_probe.calls == 2);
            check(handle_probe.handle == "cpp-handle-2");
            wire_window.unexport_foreign_handle();
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_exported, 0) == 2);
            check(wd_live_id(kind_exported) == 0);
            check(wd_exported_handle("late-handle") == 0);
            check(wd_sync(wire_application.get()) == 0);
            check(handle_probe.calls == 2);
            wire_window.unexport_foreign_handle();
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_exported, 0) == 2);

            /* Import a parent; a second import replaces it; clear destroys the
             * current import and is idempotent; an empty handle is a reject. */
            check(!wire_window.set_foreign_parent(""));
            check(wire_window.set_foreign_parent("opaque-handle"));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_importer, 1) == 1);
            check(wd_request_count(kind_imported, 1) == 1); // set_parent_of
            check(wd_live_id(kind_imported) != 0);
            check(wire_window.set_foreign_parent("opaque-handle-2"));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_importer, 1) == 2);
            check(wd_request_count(kind_imported, 0) == 1);
            check(wd_request_count(kind_imported, 1) == 2);
            wire_window.clear_foreign_parent();
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_imported, 0) == 2);
            check(wd_live_id(kind_imported) == 0);
            wire_window.clear_foreign_parent();
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_imported, 0) == 2);

            /* A NULL app id is the empty app id, not a NULL dereference: the
             * request is queued (without set_app_id) and creates a token
             * object. NULL token/handle reject locally without touching the
             * wire. */
            check(wire_window.request_activation_token(nullptr, activation_token_ready,
                                                       &token_probe));
            check(wd_sync(wire_application.get()) == 0);
            check(wd_request_count(kind_activation, 1) == 3);
            check(wd_request_count(kind_token, 1) == 1);
            check(wd_live_id(kind_token) != 0);
            check(!wire_window.activate(nullptr));
            check(!wire_window.set_foreign_parent(nullptr));
            check(wd_request_count(kind_activation, 2) == 1);
            check(wd_request_count(kind_importer, 1) == 2);
            check(wd_pending_fd_count() == 0);

            check(wd_request_overflowed() == 0);
            check(wd_pending_fd_count() == 0);
            check(wd_error() == nullptr);
        }
    }
    wd_stop();
    if (had_headless) {
        setenv("PT_HEADLESS", saved_value.c_str(), 1);
    } else {
        unsetenv("PT_HEADLESS");
    }
}

/* Headless and invalid-wrapper fallback for the six 2.4 methods: every bool
 * action reports false without calling its callback, and the void revocations
 * are safe no-ops that stay idempotent. Empty tokens/handles and NULL
 * callbacks are rejected by the wrapper or the C layer. */
void run_activation_foreign_headless_test(pt::app &application) {
    activation_token_probe token_probe;
    foreign_handle_probe handle_probe;

    pt::window null_window;
    check(!null_window.request_activation_token("pt.tests.cpp", activation_token_ready,
                                                 &token_probe));
    check(!null_window.request_activation_token("", nullptr, &token_probe));
    check(!null_window.activate("opaque-token"));
    check(!null_window.export_foreign_handle(foreign_handle_ready, &handle_probe));
    check(!null_window.export_foreign_handle(nullptr, &handle_probe));
    null_window.unexport_foreign_handle();
    check(!null_window.set_foreign_parent("opaque-handle"));
    check(!null_window.request_activation_token(nullptr, activation_token_ready,
                                                &token_probe));
    check(!null_window.activate(nullptr));
    check(!null_window.set_foreign_parent(nullptr));
    null_window.clear_foreign_parent();
    check(token_probe.calls == 0);
    check(handle_probe.calls == 0);

    pt::window headless_window(application, "cpp 2.4 fallback", 200, 120);
    check(headless_window.valid());
    check(!headless_window.request_activation_token("pt.tests.cpp",
                                                     activation_token_ready, &token_probe));
    check(!headless_window.request_activation_token("", activation_token_ready,
                                                     &token_probe));
    check(!headless_window.request_activation_token("pt.tests.cpp", nullptr,
                                                     &token_probe));
    check(!headless_window.activate("opaque-token"));
    check(!headless_window.activate(""));
    check(!headless_window.export_foreign_handle(foreign_handle_ready, &handle_probe));
    check(!headless_window.export_foreign_handle(nullptr, &handle_probe));
    headless_window.unexport_foreign_handle();
    headless_window.unexport_foreign_handle(); // idempotent
    check(!headless_window.set_foreign_parent("opaque-handle"));
    check(!headless_window.set_foreign_parent(""));
    /* NULL C strings must not be dereferenced: an empty app id still goes
     * through the headless reject, while a NULL token/handle is a local
     * reject exactly like the old const char* API. */
    check(!headless_window.request_activation_token(nullptr, activation_token_ready,
                                                     &token_probe));
    check(!headless_window.activate(nullptr));
    check(!headless_window.set_foreign_parent(nullptr));
    headless_window.clear_foreign_parent();
    headless_window.clear_foreign_parent(); // idempotent
    check(token_probe.calls == 0);
    check(handle_probe.calls == 0);
}

/* Runs `call` once per allocation budget 0..N, forcing std::bad_alloc at each
 * successive allocation. The wrapper's RAII guard must free the C allocation
 * on the throwing path, so the sanitized gate (LeakSanitizer) proves the
 * native buffer is released even when the target container cannot be built. */
template <typename Call>
int count_bad_alloc_paths(Call call) {
    int thrown = 0;
    for (long long budget = 0; budget < 40; ++budget) {
        allocation_attempts.store(0, std::memory_order_relaxed);
        allocation_budget.store(budget, std::memory_order_relaxed);
        try {
            call();
        } catch (const std::bad_alloc &) {
            ++thrown;
        }
        allocation_budget.store(-1, std::memory_order_relaxed);
    }
    return thrown;
}

/* Every owned getter needs a payload longer than the small-string buffer so the
 * injected failure really happens during the copy, not in SSO storage. */
void run_bad_alloc_hardening_test(pt::window &window, pt::widget &table) {
    const std::string long_text =
        "owned getter payload that defeats the small string buffer 0123456789";

    check(window.set_clipboard(long_text));
    check(count_bad_alloc_paths([&window] { (void)window.clipboard(); }) > 0);
    check(window.clipboard() == long_text);

    table.set_sort_columns({{0, pt_sort_ascending}});
    const std::string layout = table.columns_save();
    check(!layout.empty());
    check(count_bad_alloc_paths([&table] { (void)table.columns_save(); }) > 0);
    check(table.columns_save() == layout);

    static constexpr std::string_view uri_text =
        "file:///tmp/bad-alloc-owned-path.txt\r\n";
    check(count_bad_alloc_paths([] { (void)pt::uri_list_get(uri_text, 0); }) > 0);
    check(pt::uri_list_get(uri_text, 0) == "file:///tmp/bad-alloc-owned-path.txt");

    check(window.set_primary_serial(long_text, 23));
    check(count_bad_alloc_paths([&window] { (void)window.primary(); }) > 0);
    check(window.primary() == long_text);

    const pt_color pixels[4] = {pt_rgb(0x11223344), pt_rgb(0x55667788),
                                pt_rgb(0x99AABBCC), pt_rgb(0xDDEEFF00)};
    check(window.set_clipboard_image(std::span<const pt_color>(pixels, 4), 2, 2));
    check(count_bad_alloc_paths([&window] { (void)window.clipboard_image_png(); }) > 0);
    const std::optional<pt::clipboard_image> decoded = window.clipboard_image_png();
    check(decoded.has_value());
    if (decoded) {
        check(decoded->width == 2 && decoded->height == 2);
        check(decoded->pixels.size() == 4);
    }
}

} // namespace

int main() {
    setenv("PT_HEADLESS", "1", 1);
    unsetenv("PT_REDUCED_MOTION");

    run_wire_dispatch_test();
    run_activation_foreign_wire_test();
    setenv("PT_HEADLESS", "1", 1);

    pt::app application("pt.tests.cpp");
    check(application.valid());
    if (!application.valid()) {
        return 1;
    }
    application.set_color_scheme(pt_color_scheme_light);
    application.set_accent_color(pt_rgb(0x4C9AFF));
    application.set_font_family("monospace");
    check(application.theme_color(pt_theme_accent) == pt_rgb(0x4C9AFF));
    check(application.theme_color(pt_theme_control) !=
          application.theme_color(pt_theme_track));
    check(std::fabs(application.theme_metric(pt_theme_metric_radius_small) - 6.0f) <
          0.0001f);
    application.set_theme_metric(pt_theme_metric_chip_height, 30.0f);
    check(std::fabs(application.theme_metric(pt_theme_metric_chip_height) - 30.0f) <
          0.0001f);
    check(!application.reduced_motion());
    application.set_reduced_motion(true);
    check(application.reduced_motion());
    application.set_reduced_motion(false);
    check(application.a11y_status() == pt_a11y_status_disabled);
    const bool a11y_enabled = application.enable_a11y();
    check(a11y_enabled == (application.a11y_status() == pt_a11y_status_active));
    check(application.a11y_status() != pt_a11y_status_disabled);
    application.disable_a11y();
    check(application.a11y_status() == pt_a11y_status_disabled);
    check(std::fabs(pt::ease(pt_easing_linear, 0.5f) - 0.5f) < 0.0001f);
    check(std::fabs(pt::ease_value(pt_easing_linear, 10.0f, 30.0f, 0.25f) - 15.0f) <
          0.0001f);

    pt::window window(application, "cpp binding", 420, 320);
    check(window.valid());
    check(window.width() == 420);
    check(window.height() == 320);
    check(window.scale() > 0.0f);
    check(window.theme_color(pt_theme_background) ==
          application.theme_color(pt_theme_background));
    window.set_theme_color(pt_theme_background, pt_rgb(0x102030));
    window.set_theme_metric(pt_theme_metric_focus_width, 3.0f);
    check(window.theme_color(pt_theme_background) == pt_rgb(0x102030));
    check(std::fabs(window.theme_metric(pt_theme_metric_focus_width) - 3.0f) < 0.0001f);
    check(application.theme_color(pt_theme_background) != pt_rgb(0x102030));
    window.clear_theme_overrides();
    check(window.theme_color(pt_theme_background) ==
          application.theme_color(pt_theme_background));

    pt::widget root = window.root();
    check(root.valid());
    pt::widget column = pt::column(root.get());
    column.set_gap(8.0f).set_padding(12.0f, 12.0f, 12.0f, 12.0f);

    pt::widget title = pt::label(column.get(), "hello");
    /* ---- 2.0 direction properties: zero defaults and explicit storage ---- */
    check(application.ui_direction() == pt_ui_direction_inherit);
    check(window.ui_direction() == pt_ui_direction_inherit);
    check(column.ui_direction() == pt_ui_direction_inherit);
    check(title.ui_direction() == pt_ui_direction_inherit);
    check(title.text_direction() == pt_text_direction_auto);
    check(pt_ui_direction_inherit == 0 && pt_ui_direction_ltr == 1 &&
          pt_ui_direction_rtl == 2);
    check(pt_text_direction_auto == 0 && pt_text_direction_ltr == 1 &&
          pt_text_direction_rtl == 2);
    /* 2.1 logical padding: the builder form compiles and chains; the mapping
     * itself is asserted in tests/test_layout.c. */
    pt::widget logical_card = pt::column(root.get());
    logical_card.set_width(200.0f).set_height(80.0f);
    logical_card.set_padding_logical(4.0f, 16.0f, 6.0f, 8.0f);
    logical_card.set_ui_direction(pt_ui_direction_rtl);
    const pt::rect logical_bounds = logical_card.bounds();
    check(logical_bounds.width >= 0.0f);
    logical_card.set_padding(1.0f, 2.0f, 3.0f, 4.0f);

    application.set_ui_direction(pt_ui_direction_rtl);
    check(application.ui_direction() == pt_ui_direction_rtl);
    window.set_ui_direction(pt_ui_direction_ltr);
    check(window.ui_direction() == pt_ui_direction_ltr);
    column.set_ui_direction(pt_ui_direction_rtl).set_text_direction(pt_text_direction_rtl);
    check(column.ui_direction() == pt_ui_direction_rtl);
    check(column.text_direction() == pt_text_direction_rtl);
    title.set_ui_direction(pt_ui_direction_ltr).set_text_direction(pt_text_direction_ltr);
    check(title.ui_direction() == pt_ui_direction_ltr);
    check(title.text_direction() == pt_text_direction_ltr);
    // Out-of-range values are rejected by the C entry point.
    application.set_ui_direction(static_cast<pt_ui_direction>(99));
    check(application.ui_direction() == pt_ui_direction_rtl);
    window.set_ui_direction(static_cast<pt_ui_direction>(99));
    check(window.ui_direction() == pt_ui_direction_ltr);
    column.set_text_direction(static_cast<pt_text_direction>(99));
    check(column.text_direction() == pt_text_direction_rtl);
    // Restore the zero values for the remaining checks.
    application.set_ui_direction(pt_ui_direction_inherit);
    window.set_ui_direction(pt_ui_direction_inherit);
    column.set_ui_direction(pt_ui_direction_inherit).set_text_direction(pt_text_direction_auto);
    title.set_ui_direction(pt_ui_direction_inherit).set_text_direction(pt_text_direction_auto);
    check(application.ui_direction() == pt_ui_direction_inherit);
    check(title.text_direction() == pt_text_direction_auto);

    title.set_font_size(20.0f).set_font_weight(pt_font_weight_bold).set_wrap(true);
    check(title.text() == "hello");
    title.set_text("changed");
    check(title.text() == "changed");
    title.set_text("styled title");
    pt_text_span title_span{};
    title_span.start = 0;
    title_span.length = 6;
    title_span.weight = pt_font_weight_bold;
    title_span.flags = pt_text_span_underline | pt_text_span_has_weight;
    title.set_text_spans({title_span});
    check(title.text_span_count() == 1);
    const std::optional<pt_text_span> found = title.text_span(0);
    check(found.has_value());
    check(found && found->length == 6 && found->weight == pt_font_weight_bold);
    check(found && (found->flags & pt_text_span_underline) != 0);
    check(!title.text_span(1).has_value());
    title.clear_text_spans();
    check(title.text_span_count() == 0);

    /* ---- 2.3 inline image runs: descriptor mapping, round trip, lifetimes ---- */
    check(pt_text_image_align_baseline == 0);
    check(pt_text_image_align_middle == 1);
    check(sizeof(pt_text_image::alt) == 128);
    check(offsetof(pt_text_image, image) > offsetof(pt_text_image, start));
    check(offsetof(pt_text_image, align) > offsetof(pt_text_image, height));

    pt::icon_image run_icon =
        pt::icon_image::load_memory(std::span<const uint8_t>(icon_png, sizeof(icon_png)));
    check(run_icon.valid());
    {
        /* set/get round trip: alt, size and alignment survive the copy. */
        pt::widget runs = pt::label(column.get(), "a\xEF\xBF\xBC" "b\xEF\xBF\xBC" "c");
        pt_text_image first{};
        first.start = 1;
        first.image = run_icon.get();
        first.width = 12.0f;
        first.height = 18.0f;
        first.align = pt_text_image_align_middle;
        std::strcpy(first.alt, "first");
        pt_text_image second{};
        second.start = 5;
        second.image = run_icon.get();
        second.width = 0.0f;
        second.height = -1.0f;
        second.align = pt_text_image_align_baseline;
        std::strcpy(second.alt, "second");
        check(runs.set_text_images({first, second}));
        check(runs.text_image_count() == 2);
        const std::optional<pt_text_image> got_first = runs.text_image(0);
        check(got_first.has_value());
        if (got_first) {
            check(got_first->start == 1);
            check(got_first->image == run_icon.get());
            check(got_first->width == 12.0f && got_first->height == 18.0f);
            check(got_first->align == pt_text_image_align_middle);
            check(std::strcmp(got_first->alt, "first") == 0);
        }
        const std::optional<pt_text_image> got_second = runs.text_image(1);
        check(got_second.has_value());
        if (got_second) {
            check(got_second->start == 5);
            check(got_second->width == 0.0f && got_second->height == -1.0f);
            check(got_second->align == pt_text_image_align_baseline);
            check(std::strcmp(got_second->alt, "second") == 0);
        }
        /* Out-of-range indices are the optional empty state. */
        check(!runs.text_image(2).has_value());
        check(!runs.text_image(99).has_value());

        /* A vector input goes through the same path as the initializer list. */
        std::vector<pt_text_image> vector_input{second};
        check(runs.set_text_images(vector_input));
        check(runs.text_image_count() == 1);
        check(runs.text_image(0).has_value());
        if (const std::optional<pt_text_image> only = runs.text_image(0)) {
            check(only->start == 5 && std::strcmp(only->alt, "second") == 0);
        }

        /* alt is truncated on a UTF-8 boundary at 127 bytes. */
        pt_text_image long_alt{};
        long_alt.start = 1;
        long_alt.image = run_icon.get();
        for (int i = 0; i < 127; ++i) {
            long_alt.alt[i] = 'x';
        }
        long_alt.alt[127] = '\0';
        check(runs.set_text_images({long_alt}));
        const std::optional<pt_text_image> truncated = runs.text_image(0);
        check(truncated.has_value());
        if (truncated) {
            check(std::strlen(truncated->alt) == 127);
        }

        /* An empty vector clears the table. */
        check(runs.set_text_images(std::vector<pt_text_image>{}));
        check(runs.text_image_count() == 0);
        check(!runs.text_image(0).has_value());

        /* The explicit clear is a no-op on an empty table and drops entries. */
        runs.clear_text_images();
        check(runs.text_image_count() == 0);
        check(runs.set_text_images({first}));
        check(runs.text_image_count() == 1);
        runs.clear_text_images();
        check(runs.text_image_count() == 0);
        check(!runs.text_image(0).has_value());

        /* A NULL image is dropped rather than clearing the whole table. */
        pt_text_image null_entry{};
        null_entry.start = 1;
        null_entry.image = nullptr;
        check(runs.set_text_images({null_entry, first}));
        check(runs.text_image_count() == 1);
        if (const std::optional<pt_text_image> kept = runs.text_image(0)) {
            check(kept->start == 1 && std::strcmp(kept->alt, "first") == 0);
        }
        /* A start that is not a placeholder is dropped too. */
        pt_text_image misaligned = first;
        misaligned.start = 0;
        check(runs.set_text_images({misaligned}));
        check(runs.text_image_count() == 0);
        runs.clear_text_images();

        /* The setter rejects widgets that cannot hold runs and leaves the
         * previous table in place. */
        pt::widget plain_button = pt::button(column.get(), "no runs");
        check(!plain_button.set_text_images({first}));
        check(plain_button.text_image_count() == 0);
        check(runs.set_text_images({first}));
        check(runs.text_image_count() == 1);
        check(!plain_button.set_text_images({first}));
        check(runs.text_image_count() == 1);

        /* Ownership: the setter takes its own reference, so releasing the
         * caller's handle keeps the run usable, and the widget drops it on
         * clear. */
        pt::icon_image caller_handle = run_icon.ref();
        check(caller_handle.get() == run_icon.get());
        pt_text_image owned = first;
        owned.image = caller_handle.get();
        check(runs.set_text_images({owned}));
        const pt_icon_image *borrowed = nullptr;
        if (const std::optional<pt_text_image> entry = runs.text_image(0)) {
            borrowed = entry->image;
        }
        check(borrowed != nullptr);
        caller_handle.reset();
        check(!caller_handle.valid());
        check(run_icon.valid());
        /* Borrowed pointer is still alive (shared with run_icon). */
        check(borrowed != nullptr && pt_icon_image_width(borrowed) == 2);
        /* The same handle may fill more than one descriptor. */
        pt_text_image twice_a = first;
        pt_text_image twice_b = second;
        twice_a.image = run_icon.get();
        twice_b.image = run_icon.get();
        check(runs.set_text_images({twice_a, twice_b}));
        check(runs.text_image_count() == 2);
        if (const std::optional<pt_text_image> a = runs.text_image(0)) {
            check(a->image == twice_b.image);
        }
        runs.clear_text_images();
        check(run_icon.valid());

        /* ---- 2.3 insert_image: offset, placeholder text, shifting ---- */
        pt::widget area = pt::text_area(column.get());
        area.set_text("ab");
        area.set_selection(1, 1);
        check(area.text() == "ab");
        pt_text_image inserted{};
        inserted.image = run_icon.get();
        inserted.width = 10.0f;
        inserted.height = 0.0f;
        inserted.align = pt_text_image_align_middle;
        std::strcpy(inserted.alt, "inline");
        inserted.start = 999; /* ignored by insert_image */
        const std::optional<uint32_t> at = area.insert_image(inserted);
        check(at.has_value());
        check(at && *at == 1);
        check(area.text() == "a\xEF\xBF\xBC" "b");
        check(area.text_image_count() == 1);
        if (const std::optional<pt_text_image> entry = area.text_image(0)) {
            check(entry->start == 1);
            check(entry->width == 10.0f && entry->height == 0.0f);
            check(entry->align == pt_text_image_align_middle);
            check(std::strcmp(entry->alt, "inline") == 0);
        }

        /* A second insert inside a run shifts the byte offsets of later runs. */
        area.set_selection(0, 0);
        pt_text_image head{};
        head.image = run_icon.get();
        std::strcpy(head.alt, "head");
        const std::optional<uint32_t> head_at = area.insert_image(head);
        check(head_at.has_value());
        check(head_at && *head_at == 0);
        check(area.text_image_count() == 2);
        if (const std::optional<pt_text_image> entry = area.text_image(0)) {
            check(entry->start == 0 && std::strcmp(entry->alt, "head") == 0);
        }
        if (const std::optional<pt_text_image> entry = area.text_image(1)) {
            check(entry->start == 4 && std::strcmp(entry->alt, "inline") == 0);
        }

        /* Ownership: after a successful insert the caller may release its
         * handle; the text area keeps the run alive. */
        pt::icon_image insert_handle = run_icon.ref();
        area.set_selection(area.text().size(), area.text().size());
        pt_text_image tail{};
        tail.image = insert_handle.get();
        std::strcpy(tail.alt, "tail");
        const std::optional<uint32_t> tail_at = area.insert_image(tail);
        check(tail_at.has_value());
        insert_handle.reset();
        check(!insert_handle.valid());
        check(area.text_image_count() == 3);
        if (const std::optional<pt_text_image> entry = area.text_image(2)) {
            check(std::strcmp(entry->alt, "tail") == 0);
            check(entry->image != nullptr && entry->image == run_icon.get());
        }

        /* Failure states leave the text and table untouched. */
        const std::string before = area.text();
        const uint32_t count_before = area.text_image_count();
        pt_text_image null_image{};
        null_image.image = nullptr;
        check(!area.insert_image(null_image).has_value());
        check(area.text() == before);
        check(area.text_image_count() == count_before);
        /* Not a text area. */
        check(!runs.insert_image(head).has_value());
        check(!plain_button.insert_image(head).has_value());
        check(area.text() == before);
        check(area.text_image_count() == count_before);
        /* An endpoint inside a placeholder sequence is rejected. */
        area.set_selection(1, 1);
        check(!area.insert_image(head).has_value());
        check(area.text() == before);
        check(area.text_image_count() == count_before);
        area.clear_text_images();
        check(area.text_image_count() == 0);
        check(area.text().find("\xEF\xBF\xBC") != std::string::npos);
    }

    pt::widget button = pt::button(column.get(), "Press");
    button.set_button_variant(pt_button_primary).on_click(nullptr);
    pt::widget entry = pt::entry(column.get(), "placeholder");
    entry.set_password(true).on_change(nullptr).on_submit(nullptr);
    entry.set_input_purpose(pt_input_purpose_email);
    check(entry.input_purpose() == pt_input_purpose_email);

    pt::widget toggle = pt::toggle(column.get());
    toggle.set_checked(true);
    check(toggle.checked());
    toggle.set_checked(false);
    check(!toggle.checked());

    pt::widget checkbox = pt::checkbox(column.get(), "Subscribe");
    checkbox.set_checked(true);
    check(checkbox.checked());
    checkbox.on_checked(nullptr);

    pt::widget radio_a = pt::radio(column.get(), "Comfort");
    pt::widget radio_b = pt::radio(column.get(), "Compact");
    radio_a.set_checked(true);
    check(radio_a.checked());
    radio_b.set_checked(true);
    check(radio_b.checked());
    check(!radio_a.checked());

    pt::widget slider = pt::slider(column.get());
    slider.set_value(0.5f);
    check(std::fabs(slider.value() - 0.5f) < 0.0001f);
    slider.on_value_change(nullptr);

    pt::widget progress = pt::progress(column.get());
    progress.set_progress(0.625f);
    check(std::fabs(progress.progress() - 0.625f) < 0.0001f);
    progress.set_indeterminate(true);
    check(progress.indeterminate());
    check(pt::spinner(column.get()).valid());

    pt::widget segmented = pt::segmented(column.get(), {"Dense", "Cozy", "Compact"});
    segmented.select_segment(1).on_segment_change(nullptr);
    check(segmented.segment() == 1);

    entry.set_text("alpha beta gamma").select_all();
    check((entry.selection() == std::pair<uint32_t, uint32_t>{0, 16}));
    entry.set_selection(10, 6);
    check((entry.selection() == std::pair<uint32_t, uint32_t>{6, 10}));
    check(!entry.undo());
    check(!entry.redo());
    check(entry.drag_enabled());
    entry.set_drag_enabled(false);
    check(!entry.drag_enabled());
    entry.set_drag_enabled(true);

    const pt_drop_target target = {drop_enter, drop_motion, drop_leave, drop_receive,
                                   nullptr, nullptr, nullptr};
    entry.set_drop_target(target);
    check(!entry.drop_hover());
    entry.clear_drop_target();

    /* Structured targets use the offer callbacks; headless routing never runs
     * them, so registering and clearing must both be side-effect free. */
    offer_capture incoming;
    entry.set_drop_target(offer_enter, offer_drop, drop_motion, drop_leave, &incoming);
    check(!entry.drop_hover());
    check(!incoming.dropped);
    entry.clear_drop_target();
    check(!entry.drop_hover());
    check(incoming.mime_count == 0);

    window.on_gesture(on_gesture).set_long_press_delay(320);
    check(window.momentum_scrolling());
    window.set_momentum_scrolling(false);
    check(!window.momentum_scrolling());
    window.set_momentum_scrolling(true);
    check(window.popup_policy() == pt_popup_policy_auto);
    window.set_popup_policy(pt_popup_policy_window);
    check(window.popup_policy() == pt_popup_policy_window);
    window.set_popup_policy(pt_popup_policy_auto);
    check(!window.drag_active());
    check(window.drag_timeout() == 0);
    window.set_drag_timeout(1500);
    check(window.drag_timeout() == 1500);
    window.cancel_drag();
    check(!window.drag_active());
    window.set_drag_timeout(0);

    pt::drag_data plain_type{};
    plain_type.mime_type = "text/plain;charset=utf-8";
    plain_type.data = "structured drag";
    plain_type.size = 0;
    const pt::drag_data uri_type{"text/uri-list", "file:///tmp/a.txt\r\n", 0};
    const pt::drag_data types[] = {plain_type, uri_type};
    const std::string error_before_drag = pt::last_error();
    check(!window.begin_drag(types)); // headless has no drag source
    check(!window.begin_drag({plain_type, uri_type}));
    check(!window.begin_drag(std::span<const pt::drag_data>()));
    check(!window.drag_active());
    check(window.drag_result() == pt_drag_action_none);
    check(pt::last_error() == error_before_drag);
    bool drag_reported = false;
    window.set_drag_finished_callback(drag_finished, &drag_reported);
    window.cancel_drag(); // no source drag was started, so nothing notifies
    check(!drag_reported);
    window.set_drag_finished_callback(nullptr);
    check(window.drag_result() == pt_drag_action_none);

    const pt::drop_offer empty_offer;
    check(!empty_offer.valid());
    check(empty_offer.get() == nullptr);
    check(empty_offer.mime_count() == 0);
    check(empty_offer.mime(0).empty());
    check(empty_offer.mime(9).empty());
    check(!empty_offer.has_mime("text/plain"));
    check(empty_offer.selected_mime().empty());
    check(empty_offer.actions() == 0u);
    const pt::drop_stream empty_stream =
        empty_offer.read("text/plain", 64, offer_stream, &incoming);
    check(!empty_stream.valid());
    check(!empty_stream.active());
    check(empty_stream.size() == 0);
    check(empty_stream.mime().empty());
    empty_stream.cancel();

    const pt::drop_offer null_offer(nullptr);
    check(!null_offer.read("").valid());
    check(null_offer.read("text/plain").get() == nullptr);
    check(!null_offer.read("text/uri-list", 0, offer_stream, &incoming).active());
    const pt::drop_stream null_stream(nullptr);
    null_stream.cancel();
    check(!null_stream.valid());
    /* No offer, drop or stream callback runs without a compositor. */
    check(incoming.payload.empty());
    check(incoming.finished_size == 0);

    pt::widget scroll = pt::scroll(column.get());
    scroll.set_scroll_offset(24.0f);
    check(scroll.scroll_offset() >= 0.0f);
    check(scroll.max_scroll_offset() >= 0.0f);

    pt::widget notes = pt::text_area(column.get(), "Notes");
    check(notes.valid());
    check(notes.kind() == pt_widget_text_area);
    notes.set_text("alpha\nbeta").set_grow_to_content(true);
    check(notes.grow_to_content());
    notes.set_grow_to_content(false);
    notes.on_change(nullptr);
    notes.set_placeholder("Write");
    notes.select_all();
    check((notes.selection() == std::pair<uint32_t, uint32_t>{0, 10}));
    notes.set_selection(6, 3);
    check((notes.selection() == std::pair<uint32_t, uint32_t>{3, 6}));
    check(!notes.undo());
    check(!notes.redo());
    notes.scroll_to_caret();
    check(notes.a11y_role() == pt_a11y_role_text_frame);

    pt::widget wrapping = pt::wrap(column.get());
    wrapping.set_wrap_gap(8.0f, 6.0f);
    check(wrapping.kind() == pt_widget_wrap);
    pt::widget tile = pt::card(wrapping.get());
    tile.set_width(80.0f).set_height(40.0f).set_align_self(pt_align_start);

    pt::widget gridded = pt::grid(column.get(), 3);
    gridded.set_grid_gap(10.0f, 12.0f);
    check(gridded.grid_columns() == 3);
    gridded.set_grid_columns(2);
    check(gridded.grid_columns() == 2);
    pt::widget wide = pt::card(gridded.get());
    wide.set_grid_span(2);
    check(wide.grid_span() == 2);
    wide.set_grid_span(1);
    check(wide.grid_span() == 1);

    pt::widget rows = pt::list_view(column.get());
    check(rows.kind() == pt_widget_list);
    rows.set_row_height(36.0f);
    check(std::fabs(rows.row_height() - 36.0f) < 0.0001f);
    rows.set_row_count(50).refresh();
    check(rows.row_count() == 50);
    check(rows.selected_row() == pt_list_index_none);
    uint32_t active = pt_list_index_none;
    rows.on_bind_row(bind_row).on_row_select(on_row, &active).on_row_activate(on_row, &active);
    rows.set_selected_row(7);
    check(rows.selected_row() == 7);
    rows.scroll_to_row(49).refresh();
    check(active == pt_list_index_none);
    rows.set_row_key_callback(row_key);
    check(rows.selected_key() == 1070);
    rows.set_row_count(50);
    check(rows.selected_row() == 7);
    rows.set_selected_row(3);
    check(rows.selected_key() == 1030);
    check(!rows.set_selected_key(999999));
    check(rows.selected_row() == 3);
    check(rows.set_selected_key(1040));
    check(rows.selected_row() == 4);
    check(rows.selected_key() == 1040);
    rows.refresh_rows(0, 4);
    check(rows.row_count() == 50);
    check(rows.set_selected_key(pt_row_key_none));
    check(rows.selected_row() == pt_list_index_none);
    check(rows.selected_key() == pt_row_key_none);
    check(rows.set_selected_key(1030));
    rows.set_row_count(2);
    check(rows.selected_row() == pt_list_index_none);
    rows.set_selected_row(1);
    check(rows.selected_key() == 1010);
    rows.set_row_count(2);
    check(rows.selected_row() == 1);
    rows.set_row_text_callback(row_text);
    rows.set_selected_row(pt_list_index_none);
    rows.set_row_count(2);
    check(rows.selected_row() == pt_list_index_none);
    check(rows.a11y_role() == pt_a11y_role_list);
    rows.set_row_count(8);
    check(rows.list_selection_count() == 0);
    check(rows.list_selection().empty());
    rows.list_select_range(2, 4);
    check(rows.list_selection_count() == 3);
    check(rows.list_selection() == std::vector<uint32_t>({2, 3, 4}));
    check(rows.is_list_row_selected(3));
    check(!rows.is_list_row_selected(5));
    check(rows.selected_row() == 2);
    rows.list_select_toggle(6);
    check(rows.list_selection() == std::vector<uint32_t>({2, 3, 4, 6}));
    rows.list_select_toggle(3);
    check(rows.list_selection() == std::vector<uint32_t>({2, 4, 6}));
    rows.clear_list_selection();
    check(rows.list_selection_count() == 0);
    check(rows.selected_row() == pt_list_index_none);

    pt::widget icon = pt::icon(column.get(), pt_icon_search);
    check(icon.kind() == pt_widget_icon);
    check(icon.icon() == pt_icon_search);
    check(std::fabs(icon.icon_size() - 20.0f) < 0.0001f);
    icon.set_icon_size(28.0f).set_icon(pt_icon_bell);
    check(icon.icon() == pt_icon_bell);
    check(std::fabs(icon.icon_size() - 28.0f) < 0.0001f);
    check(icon.a11y_role() == pt_a11y_role_icon);
    pt::widget icon_button = pt::icon_button(column.get(), pt_icon_plus, "Add item");
    check(icon_button.a11y_name() == "Add item");
    check(icon_button.a11y_role() == pt_a11y_role_push_button);
    button.set_icon(pt_icon_trash);
    check(button.icon() == pt_icon_trash);
    button.set_busy(true);
    check(button.busy());
    button.set_busy(false);
    check(!button.busy());
    button.set_invalid(true);
    check(button.invalid());
    button.set_invalid(false);
    check(!button.invalid());

    pt::widget chip = pt::chip(column.get(), "filter");
    check(chip.kind() == pt_widget_chip);
    chip.set_chip_icon(pt_icon_search).set_chip_removable(true);
    check(chip.chip_removable());
    check(!chip.chip_selected());
    chip.set_chip_selected(true);
    check(chip.chip_selected());
    chip.on_chip_click(nullptr).on_chip_remove(nullptr);
    pt::widget skeleton = pt::skeleton(column.get());
    check(skeleton.kind() == pt_widget_skeleton);

    pt::widget table = pt::table(column.get());
    check(table.kind() == pt_widget_table);
    check(table.a11y_role() == pt_a11y_role_table);
    table.set_column_count(3).set_header_height(34.0f);
    check(table.column_count() == 3);
    table.set_column_title(0, "Name").set_column_title(1, "Kind");
    table.set_column_width(0, 160.0f).set_column_align(1, pt_text_align_center);
    table.set_column_resizable(2, false);
    check(std::fabs(table.column_width(0) - 160.0f) < 0.0001f);
    table.set_table_row_count(40).set_table_row_height(32.0f);
    check(table.table_row_count() == 40);
    check(std::fabs(table.table_row_height() - 32.0f) < 0.0001f);
    uint32_t sorted_column = pt_table_column_none;
    uint32_t table_active = pt_list_index_none;
    table.on_bind_cell(bind_cell)
        .on_table_select(on_row, &table_active)
        .on_table_activate(on_row, &table_active)
        .on_table_sort(on_sort, &sorted_column);
    table.set_table_selected(3);
    check(table.table_selected() == 3);
    table.set_sort(1, pt_sort_ascending);
    uint32_t sort_column = pt_table_column_none;
    check(table.sort(&sort_column) == pt_sort_ascending);
    check(sort_column == 1);
    check(!table.column_hidden(1));
    table.set_column_hidden(1, true);
    check(table.column_hidden(1));
    table.set_column_hidden(1, false);
    check(!table.column_hidden(1));
    check(!table.column_hidden(7));
    sort_capture sort_state;
    table.on_sort_columns(on_sort_columns, &sort_state);
    table.set_sort_columns({{2, pt_sort_descending}, {0, pt_sort_ascending}});
    std::vector<pt::sort_key> sort_keys = table.sort_columns();
    check(sort_keys.size() == 2);
    if (sort_keys.size() == 2) {
        check(sort_keys[0].column == 2);
        check(sort_keys[0].direction == pt_sort_descending);
        check(sort_keys[1].column == 0);
        check(sort_keys[1].direction == pt_sort_ascending);
    }
    uint32_t primary_column = pt_table_column_none;
    check(table.sort(&primary_column) == pt_sort_descending);
    check(primary_column == 2);
    table.set_sort_columns({{1, pt_sort_ascending}, {1, pt_sort_descending},
                            {7, pt_sort_ascending}});
    sort_keys = table.sort_columns();
    check(sort_keys.size() == 1);
    if (!sort_keys.empty()) {
        check(sort_keys[0].column == 1);
    }
    table.set_table_row_key_callback(row_key);
    check(table.table_selected() == 3);
    check(table.table_selected_key() == 1030);
    table.set_table_row_count(40);
    check(table.table_selected() == 3);
    table.set_table_selected(0);
    check(table.table_selected_key() == 1000);
    check(!table.set_table_selected_key(999999));
    check(table.table_selected() == 0);
    check(table.set_table_selected_key(1050));
    check(table.table_selected() == 5);
    check(table.table_selected_key() == 1050);
    check(table.set_table_selected_key(pt_row_key_none));
    check(table.table_selected() == pt_list_index_none);
    check(table.set_table_selected_key(1010));
    check(table.table_selected() == 1);
    table.table_refresh_rows(0, 3);
    check(table.table_row_count() == 40);
    table.set_table_row_text_callback(row_text);
    table.set_table_row_key_callback(nullptr);
    table.set_table_selected(3);
    check(!table.table_editable());
    table.set_table_editable(true);
    check(table.table_editable());
    edit_capture edit_state;
    table.on_cell_edited(on_cell_edited, &edit_state);
    check(!table.table_editing());
    check(table.begin_cell_edit(1, 0));
    check(table.table_editing());
    check(table.commit_cell_edit());
    check(!table.table_editing());
    check(edit_state.row == 1);
    check(edit_state.column == 0);
    table.select_range(1, 3);
    check(table.table_selection_count() == 3);
    check(table.is_row_selected(2));
    check(table.table_selection() == std::vector<uint32_t>({1, 2, 3}));
    table.select_toggle(5);
    check(table.table_selection() == std::vector<uint32_t>({1, 2, 3, 5}));
    check(table.table_selected() == 5);
    table.select_toggle(2);
    check(table.table_selection() == std::vector<uint32_t>({1, 3, 5}));
    table.clear_table_selection();
    check(table.table_selection_count() == 0);
    check(table.table_selected() == pt_list_index_none);
    reorder_capture reorder_state;
    uint32_t dragged_row = pt_list_index_none;
    table.on_columns_reordered(on_columns_reordered, &reorder_state)
        .on_row_drag(on_row_drag, &dragged_row);
    check(!table.columns_reorderable());
    table.set_columns_reorderable(true);
    check(table.columns_reorderable());
    check(table.move_column(0, 2));
    check(reorder_state.from == 0);
    check(reorder_state.to == 2);
    table.scroll_to_column(2).table_scroll_to_row(39).table_refresh();
    table.set_table_row_count(2);
    check(table.table_selected() == pt_list_index_none);

    pt::widget tree = pt::tree(column.get());
    check(tree.kind() == pt_widget_tree);
    check(tree.a11y_role() == pt_a11y_role_tree);
    pt::tree_node *alpha = tree.add_tree_node(nullptr, "alpha");
    pt::tree_node *beta = tree.add_tree_node(nullptr, "beta");
    pt::tree_node *child = tree.add_tree_node(alpha, "child");
    check(tree.tree_root_count() == 2);
    check(pt::tree_text(alpha) == "alpha");
    check(pt::tree_has_children(alpha));
    check(pt::tree_child_count(alpha) == 1);
    check(pt::tree_child(alpha, 0) == child);
    check(pt::tree_parent(child) == alpha);
    check(pt::tree_depth(child) == 1);
    int payload = 7;
    pt::set_tree_data(child, &payload);
    check(pt::tree_data(child) == &payload);
    check(tree.tree_visible_count() == 2);
    pt::set_tree_expanded(alpha, true);
    check(tree.tree_visible_count() == 3);
    check(tree.tree_visible_node(1) == child);
    pt::tree_node *selected_node = nullptr;
    tree.on_tree_select(on_tree, &selected_node).on_tree_activate(on_tree, &selected_node);
    tree.set_tree_selected(child);
    check(tree.tree_selected() == child);
    tree.scroll_to_tree_node(beta).tree_refresh();
    pt::set_tree_text(beta, "beta renamed");
    check(pt::tree_text(beta) == "beta renamed");
    check(!tree.tree_renamable());
    check(!tree.tree_renaming());
    tree.set_tree_renamable(true);
    check(tree.tree_renamable());
    rename_capture rename_state;
    tree.on_tree_rename(on_tree_rename, &rename_state);
    check(tree.begin_tree_rename(beta));
    check(tree.tree_renaming());
    check(tree.commit_tree_rename());
    check(!tree.tree_renaming());
    check(rename_state.node == beta);
    check(rename_state.text == "beta renamed");
    check(!tree.commit_tree_rename());
    check(tree.tree_selection_count() == 1);
    check(tree.tree_selection() == std::vector<pt::tree_node *>({beta}));
    tree.tree_select_range(0, 1);
    check(tree.tree_selection_count() == 2);
    check(tree.tree_selection() == std::vector<pt::tree_node *>({alpha, child}));
    check(tree.is_tree_node_selected(child));
    check(!tree.is_tree_node_selected(beta));
    check(tree.tree_selected() == alpha);
    tree.tree_select_toggle(beta);
    check(tree.tree_selection_count() == 3);
    check(tree.tree_selected() == beta);
    tree.tree_select_toggle(child);
    check(tree.tree_selection_count() == 2);
    check(tree.tree_selection() == std::vector<pt::tree_node *>({alpha, beta}));
    tree.clear_tree_selection();
    check(tree.tree_selection_count() == 0);
    check(tree.tree_selected() == nullptr);
    tree.clear_tree();
    check(tree.tree_root_count() == 0);

    pt::widget tabs = pt::tabs(column.get());
    check(tabs.kind() == pt_widget_tabs);
    pt::widget page_a = tabs.add_tab("General");
    pt::widget page_b = tabs.add_tab("Advanced");
    check(page_a.kind() == pt_widget_tab_page);
    check(page_b.kind() == pt_widget_tab_page);
    check(tabs.tab_count() == 2);
    check(tabs.tab_title(0) == "General");
    tabs.set_tab_title(1, "Options");
    check(tabs.tab_title(1) == "Options");
    check(tabs.active_tab() == 0);
    tabs.set_active_tab(1);
    check(tabs.active_tab() == 1);
    uint32_t changed_tab = pt_list_index_none;
    tabs.on_tab_change(on_row, &changed_tab).set_tab_strip_height(40.0f);
    tabs.remove_tab(0);
    check(tabs.tab_count() == 1);
    check(tabs.active_tab() == 0);
    check(tabs.tab_page(1).get() == nullptr);

    pt::widget sidebar = pt::sidebar(column.get());
    check(sidebar.kind() == pt_widget_sidebar);
    pt::widget nav_home = sidebar.add_sidebar_item(pt_icon_home, "Home");
    check(nav_home.kind() == pt_widget_sidebar_item);
    sidebar.add_sidebar_item(pt_icon_folder, "Files");
    check(sidebar.sidebar_item_count() == 2);
    check(sidebar.active_sidebar_item() == pt_list_index_none);
    sidebar.set_active_sidebar_item(1);
    check(sidebar.active_sidebar_item() == 1);
    sidebar.set_sidebar_header("Workspace");
    check(sidebar.sidebar_header() == "Workspace");
    check(!sidebar.sidebar_compact());
    sidebar.set_sidebar_compact(true);
    check(sidebar.sidebar_compact());
    uint32_t picked_item = pt_list_index_none;
    sidebar.on_sidebar_select(on_row, &picked_item);

    pt::widget colour = pt::combobox(column.get());
    check(colour.kind() == pt_widget_combobox);
    check(colour.a11y_role() == pt_a11y_role_combo_box);
    check(colour.item_count() == 0);
    check(colour.combo_selected() == pt_list_index_none);
    check(colour.combo_text().empty());
    check(colour.combo_placeholder().empty());
    check(colour.combo_mode() == pt_combobox_select_only);
    check(!colour.is_popup_open());
    check(std::fabs(colour.popup_max_height() - 260.0f) < 0.0001f);
    colour.set_combo_placeholder("Colour").add_item("Red").add_item("Green").insert_item(
        1, "Yellow");
    check(colour.item_count() == 3);
    check(colour.item_text(0) == "Red");
    check(colour.item_text(1) == "Yellow");
    check(colour.combo_placeholder() == "Colour");
    colour.set_item_text(2, "Blue");
    check(colour.item_text(2) == "Blue");
    int colour_payload = 0x1234;
    colour.set_item_data(2, &colour_payload);
    check(colour.item_data(2) == &colour_payload);
    check(colour.item_data(0) == nullptr);
    check(!colour.is_item_disabled(1));
    colour.set_item_disabled(1, true);
    check(colour.is_item_disabled(1));
    colour.set_item_disabled(1, false);
    uint32_t chosen = pt_list_index_none;
    std::string typed;
    colour.on_combo_change(on_row, &chosen).on_combo_text_change(on_combo_text, &typed);
    colour.set_combo_selected(2);
    check(colour.combo_selected() == 2);
    check(colour.combo_text() == "Blue");
    check(chosen == pt_list_index_none);
    colour.set_combo_mode(pt_combobox_filterable);
    check(colour.combo_mode() == pt_combobox_filterable);
    colour.set_combo_text("gr");
    check(colour.combo_text() == "gr");
    check(typed.empty());
    colour.open_popup();
    check(colour.is_popup_open());
    colour.set_popup_max_height(120.0f);
    check(std::fabs(colour.popup_max_height() - 120.0f) < 0.0001f);
    colour.close_popup();
    check(!colour.is_popup_open());
    colour.remove_item(0);
    check(colour.item_count() == 2);
    check(colour.item_text(0) == "Yellow");
    colour.clear_items();
    check(colour.item_count() == 0);
    check(colour.combo_selected() == pt_list_index_none);

    const std::string uri_list = "file:///tmp/a.txt\r\nfile:///tmp/b%20c.txt\r\n";
    check(pt::uri_list_count(uri_list) == 2);
    check(pt::uri_list_get(uri_list, 0) == "file:///tmp/a.txt");
    check(pt::uri_list_get(uri_list, 1) == "file:///tmp/b%20c.txt");
    check(pt::uri_list_count("") == 0);
    check(!window.begin_drag({pt::drag_data{"text/uri-list", uri_list.c_str(), 0}}));
    pt_color drag_pixels[4] = {pt_rgb(0xFF0000), pt_rgb(0x00FF00), pt_rgb(0x0000FF),
                               pt_rgb(0xFFFFFF)};
    window.set_drag_image(drag_pixels, 2, 2);
    window.clear_drag_image();
    window.on_stylus(on_stylus);
    check((pt_a11y_state_expandable & pt_a11y_state_expandable) != 0);
    check(pt_a11y_role_spin_button == 36);
    check(pt_icon_warning > pt_icon_none);

    pt::transition fade(0.0f);
    fade.to(10.0f, 100, pt_easing_linear);
    check(fade.active());
    fade.advance(0.05f);
    check(std::fabs(fade.value() - 5.0f) < 0.01f);
    fade.to(20.0f, 100, pt_easing_linear);
    fade.advance(0.05f);
    check(fade.value() > 5.0f);
    fade.set(30.0f);
    check(!fade.active());
    check(std::fabs(fade.value() - 30.0f) < 0.0001f);
    check(fade.raw() != nullptr);

    pt::widget tip_target = pt::button(column.get(), "Tip");
    tip_target.set_tooltip("Helpful hint").set_tooltip_delay(0);
    check(tip_target.a11y_role() == pt_a11y_role_push_button);
    check(tip_target.a11y_name() == "Tip");
    tip_target.set_a11y_name("Tip button").set_a11y_description("Shows a tooltip");
    check(tip_target.a11y_name() == "Tip button");
    check(tip_target.a11y_description() == "Shows a tooltip");
    check((tip_target.a11y_state() & pt_a11y_state_focusable) != 0);
    slider.set_a11y_name("Amount");
    check(slider.a11y_value().has_value());
    const std::optional<pt::a11y_text_info> notes_text = notes.a11y_text();
    check(notes_text.has_value());
    if (notes_text) {
        check(notes_text->text == "alpha\nbeta");
        check(notes_text->selection_start == 3);
        check(notes_text->selection_end == 6);
    }
    check(!entry.a11y_text().has_value()); // password text is not exposed

    pt::menu file_menu(window);
    check(file_menu.valid());
    file_menu.add_item("Open", "Ctrl+O").add_separator().add_item("Quit", "Ctrl+Q");
    file_menu.show_at(20.0f, 80.0f);
    check(file_menu.is_open());
    file_menu.dismiss();
    check(!file_menu.is_open());

    pt::dialog dialog(window, "Delete", "This cannot be undone.");
    check(dialog.valid());
    dialog.add_action("Cancel", false).add_action("Delete", true).on_close(nullptr);
    dialog.show();
    check(dialog.is_open());
    dialog.set_title("Delete file").set_message("Updated message");
    dialog.close();
    check(!dialog.is_open());

    pt::toast notice = window.show_toast("Saved", pt_toast_success, 0);
    check(notice.valid());
    check(notice.visible());
    notice.dismiss();
    check(!notice.visible());

    check(window.set_clipboard("copied text"));
    check(window.clipboard() == "copied text");

    /* ---- 1.2 font fallback chains (process-wide, read back here) ---- */
    application.set_font_fallbacks(std::span<const std::string_view>{});
    check(application.font_fallback_count() == 0);
    check(!application.font_fallback(0).has_value());
    application.set_font_fallbacks({"DejaVu Sans", "Noto Sans"});
    check(application.font_fallback_count() == 2);
    check(application.font_fallback(0).value_or("") == "DejaVu Sans");
    check(application.font_fallback(1).value_or("") == "Noto Sans");
    check(!application.font_fallback(2).has_value());
    check(!application.font_fallback(-1).has_value());
    application.set_font_fallbacks_for_family("serif", {"DejaVu Sans"});
    check(application.font_fallback_count_for_family("serif") == 1);
    check(application.font_fallback_for_family("serif", 0).value_or("") == "DejaVu Sans");
    check(!application.font_fallback_for_family("serif", 1).has_value());
    check(!application.font_fallback_for_family("sans-serif", 0).has_value());
    check(application.font_fallback_count() == 2); // default untouched
    application.set_font_fallbacks_for_family("serif", std::span<const std::string_view>{});
    check(application.font_fallback_count_for_family("serif") == 0);

    /* The public cap is enforced by Pt; the wrapper passes the whole list. */
    const std::vector<std::string_view> many = {"f0", "f1", "f2", "f3", "f4",
                                                "f5", "f6", "f7", "f8", "f9"};
    application.set_font_fallbacks(std::span<const std::string_view>(many));
    check(application.font_fallback_count() == PT_FONT_FALLBACK_MAX_FAMILIES);
    check(application.font_fallback(7).value_or("") == "f7");
    check(!application.font_fallback(8).has_value());
    check(application.font_uncovered_count("DejaVu Sans", "") == 0);
    application.set_font_fallbacks(std::span<const std::string_view>{});

    /* ---- 1.2 clipboard media: owned image copy and uri-list write ---- */
    const pt_color clipboard_pixels[] = {
        0x00000000u, 0x00FFFFFFu, 0x01FFFFFFu, 0x80402010u,
        0x7F3F1F0Fu, 0xFF102030u, 0xFFFFFFFFu, 0x80FF0000u,
    };
    check(window.set_clipboard_image(
        std::span<const pt_color>(clipboard_pixels, 8), 4, 2));
    check(window.clipboard().empty()); // the clipboard holds one type at a time
    const std::optional<pt::clipboard_image> decoded = window.clipboard_image_png();
    check(decoded.has_value());
    if (decoded) {
        check(decoded->width == 4 && decoded->height == 2);
        check(decoded->pixels.size() == 8);
        check(decoded->pixels.size() == 8 && decoded->pixels[0] == 0x00000000u);
        check(decoded->pixels.size() == 8 && decoded->pixels[5] == 0xFF102030u);
        check(decoded->pixels.size() == 8 && decoded->pixels[7] == 0x80800000u);
    }
    /* A span shorter than width * height is rejected before the C call. */
    check(!window.set_clipboard_image(std::span<const pt_color>(clipboard_pixels, 7), 4, 2));
    check(!window.set_clipboard_image(std::span<const pt_color>(), 0, 0));
    check(!window.set_clipboard_image(std::span<const pt_color>(clipboard_pixels, 8), 0, 2));
    check(!window.set_clipboard_image(std::span<const pt_color>(clipboard_pixels, 1),
                                      pt_clipboard_image_max_dimension + 1, 1));
    pt::window no_window;
    check(!no_window.set_clipboard_image(
        std::span<const pt_color>(clipboard_pixels, 8), 4, 2));

    check(window.set_clipboard_uri_list(
        {"/tmp/a b.txt", "file:///tmp/already%20encoded.txt"}));
    check(window.clipboard().empty());
    check(!window.clipboard_image_png().has_value());
    check(!window.set_clipboard_uri_list({}));
    check(!window.set_clipboard_uri_list({"relative/a.txt"}));
    check(!window.set_clipboard_uri_list({"/tmp/a\rb.txt"}));
    check(window.set_clipboard("text again"));
    check(window.clipboard() == "text again");

    /* ---- 1.2 column identity and layout persistence ---- */
    table.set_column_key(0, "id").set_column_key(1, "name").set_column_key(2, "kind");
    check(table.column_key(0) == "id");
    check(table.column_key(1) == "name");
    check(table.column_key(2) == "kind");
    check(table.column_key(9).empty()); // never NULL from the C getter
    table.set_column_width(0, 100.0f)
        .set_column_width(1, 220.5f)
        .set_column_width(2, 77.25f)
        .set_column_hidden(2, true);
    table.set_sort_columns({{1, pt_sort_ascending}, {2, pt_sort_descending}});
    const std::string layout = table.columns_save();
    check(!layout.empty());
    check(layout.rfind("PTCOLS 1\nC 3\n", 0) == 0);
    table.set_column_width(0, 42.0f).set_column_hidden(2, false);
    table.set_sort_columns({{0, pt_sort_ascending}});
    check(table.columns_restore(layout));
    check(std::fabs(table.column_width(0) - 100.0f) < 0.0001f);
    check(std::fabs(table.column_width(1) - 220.5f) < 0.0001f);
    check(std::fabs(table.column_width(2) - 77.25f) < 0.0001f);
    check(table.column_hidden(2));
    const std::vector<pt::sort_key> restored_sort = table.sort_columns();
    check(restored_sort.size() == 2);
    if (restored_sort.size() == 2) {
        check(restored_sort[0].column == 1 &&
              restored_sort[0].direction == pt_sort_ascending);
        check(restored_sort[1].column == 2 &&
              restored_sort[1].direction == pt_sort_descending);
    }
    /* Rejection is atomic and leaves the applied layout alone. */
    check(!table.columns_restore("garbage"));
    check(!table.columns_restore(""));
    check(std::fabs(table.column_width(0) - 100.0f) < 0.0001f);
    check(title.columns_save().empty()); // not a table
    check(!title.columns_restore(layout));
    pt::widget no_widget;
    check(no_widget.columns_save().empty());
    check(!no_widget.columns_restore(layout));

    /* ---- 1.2 raster icons: reference counted RAII handle and slots ---- */
    pt::icon_image image =
        pt::icon_image::load_memory(std::span<const uint8_t>(icon_png, sizeof(icon_png)));
    check(image.valid());
    check(image.width() == 2);
    check(image.height() == 2);
    pt::icon_image shared = image.ref();
    check(shared.valid());
    check(shared.get() == image.get()); // same reference-counted object
    button.set_icon_image(image).set_icon_tint(0xFF3366FFu);
    check(button.icon_image_raw() == image.get());
    image.reset();
    check(!image.valid());
    check(shared.valid());
    check(shared.width() == 2); // the extra reference kept the pixels alive
    pt::icon_image copy = shared;
    check(copy.get() == shared.get());
    pt::icon_image retained = button.icon_image_ref();
    check(retained.valid());
    check(retained.get() == shared.get());
    button.clear_icon_image();
    check(button.icon_image_raw() == nullptr);
    window.set_icon(shared);
    check(window.icon_image_raw() == shared.get());
    check(window.icon_image_ref().valid());
    window.clear_icon();
    check(window.icon_image_raw() == nullptr);
    check(!pt::icon_image::load_file("/definitely/not/here.png").valid());
    check(!pt::icon_image::from_name("../escape", 32).valid());
    check(!pt::icon_image::load_memory(std::span<const uint8_t>()).valid());
    pt::icon_image empty_image;
    check(!empty_image.valid());
    check(empty_image.width() == 0 && empty_image.height() == 0);

    /* ---- 1.2 popup surface: owned handle, placement, dismiss callback ---- */
    pt::popup empty_popup;
    check(!empty_popup.valid());
    check(!empty_popup.show(0.0f, 0.0f, 1.0f, 1.0f));
    check(!empty_popup.is_open());
    {
        pt::widget popup_content = pt::button(nullptr, "Inside");
        pt::popup surface(window, popup_content);
        check(surface.valid());
        check(surface.placement() == pt_popup_placement_below);
        surface.set_placement(pt_popup_placement_right);
        check(surface.placement() == pt_popup_placement_right);
        popup_probe probe;
        surface.on_dismiss(popup_dismissed, &probe);
        check(surface.show(10.0f, 10.0f, 20.0f, 20.0f));
        check(surface.is_open());
        check(surface.content().get() == popup_content.get());
        check(surface.reposition(pt::popup_anchor{12.0f, 12.0f, 30.0f, 30.0f}));
        check(surface.reposition(pt::rect{14.0f, 14.0f, 30.0f, 30.0f}));
        check(!surface.show(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f, 1.0f));
        surface.dismiss();
        check(!surface.is_open());
        check(probe.dismissals == 1);
        check(!probe.open_at_dismiss); // is_open is already false in the callback
        check(!surface.reposition(0.0f, 0.0f, 1.0f, 1.0f)); // closed
        check(surface.show(pt::rect{20.0f, 20.0f, 40.0f, 10.0f}));
        check(surface.is_open());
        surface.reset(); // destroy is not a dismissal
        check(!surface.valid());
        check(probe.dismissals == 1);
    }
    {
        pt::popup first;
        pt::widget content = pt::label(nullptr, "moved");
        pt::popup second(window, content);
        check(second.valid());
        first = std::move(second);
        check(!second.valid());
        check(first.valid());
    }

    /* ---- 1.1 extension API: decoration requests and maximize gates ---- */
    check(window.decoration_mode() == pt_decoration_client);
    check(window.effective_decoration_mode() == pt_decoration_client);
    window.set_decorated(false);
    // Header ownership only: false never requests server-side decoration.
    check(window.decoration_mode() == pt_decoration_client);
    check(window.effective_decoration_mode() == pt_decoration_client);
    window.set_decorated(true);
    window.set_decoration_mode(pt_decoration_server);
    check(window.decoration_mode() == pt_decoration_server);
    // Headless has no configure to wait for, so the effective mode follows.
    check(window.effective_decoration_mode() == pt_decoration_server);
    window.set_decoration_mode(pt_decoration_auto);
    check(window.decoration_mode() == pt_decoration_auto);
    check(window.effective_decoration_mode() == pt_decoration_client);
    // An out-of-range mode is ignored by the C entry point.
    window.set_decoration_mode(static_cast<pt_decoration_mode>(99));
    check(window.decoration_mode() == pt_decoration_auto);
    check(window.effective_decoration_mode() == pt_decoration_client);

    check(window.maximize()); // resizable and the capability is not restricted
    pt_window_apply_wm_capabilities(window.get(), false, true, true);
    check(!window.maximize()); // no maximize capability
    pt_window_apply_wm_capabilities(window.get(), true, true, true);
    window.set_resizable(false);
    check(!window.maximize()); // not resizable
    window.set_resizable(true);
    check(window.maximize());

    pt::window null_window;
    check(null_window.decoration_mode() == pt_decoration_auto);
    check(null_window.effective_decoration_mode() == pt_decoration_auto);
    check(!null_window.maximize());

    /* ---- 1.1 extension API: paint context and offscreen snapshot ---- */
    pt::window paint_window(application, "cpp paint", 320, 240);
    check(paint_window.valid());
    pt::widget paint_root = paint_window.root();
    pt::widget paint_column = pt::column(paint_root.get());
    pt::widget painted = pt::box(paint_column.get(), pt_direction_column);
    painted.set_size(40.0f, 20.0f);
    paint_capture paint_state;
    painted.set_paint_callback(paint_probe, &paint_state);
    check(paint_state.calls == 0); // installing the callback paints nothing

    const int frame_width = paint_window.width();
    const int frame_height = paint_window.height();
    std::vector<uint32_t> frame(static_cast<size_t>(frame_width) *
                                static_cast<size_t>(frame_height));
    const std::span<uint32_t> frame_span(frame);
    check(paint_window.paint_pixels(frame_span, frame_width, frame_height));
    check(paint_state.calls >= 1);
    check(paint_state.widget == painted.get());
    check(paint_state.width >= 40.0f);
    check(paint_state.height >= 20.0f);
    check(paint_state.scale > 0.0f);
    check(paint_state.has_clip);
    check(paint_state.clip.width >= 40.0f);
    check(paint_state.clip.height >= 20.0f);
    check(paint_state.measured > 0.0f);
    check(paint_state.has_metrics);
    check(paint_state.metrics.line_height > 0.0f);
    check(paint_state.glyph);
    const uint32_t painted_value = ((paint_fill & 0xFFu) << 24) | (paint_fill >> 8);
    check(std::find(frame.begin(), frame.end(), painted_value) != frame.end());

    const int first_calls = paint_state.calls;
    const paint_capture first_paint = paint_state; // copied out of the callback
    std::vector<uint32_t> frame_again(frame.size());
    check(paint_window.paint_pixels(std::span<uint32_t>(frame_again), frame_width, frame_height));
    check(paint_state.calls > first_calls);
    check(paint_state.width == first_paint.width);
    check(paint_state.height == first_paint.height);
    check(paint_state.clip.width == first_paint.clip.width);
    check(paint_state.clip.height == first_paint.clip.height);

    // A span smaller than width * height is rejected by the wrapper before the
    // C entry point can write past its end.
    check(!paint_window.paint_pixels(std::span<uint32_t>(frame.data(), frame.size() - 1),
                               frame_width, frame_height));
    check(!paint_window.paint_pixels(frame_span, 0, frame_height));
    check(!paint_window.paint_pixels(frame_span, frame_width, -1));

    /* Extended snapshot: an explicit stride produces the same rows as a tight
     * snapshot, a device scale changes the logical canvas, and reserved words
     * are validated before the C call. */
    const int ex_width = paint_window.width();
    const int ex_height = paint_window.height();
    const int ex_stride = ex_width + 16;
    std::vector<uint32_t> padded(static_cast<size_t>(ex_stride) * ex_height);
    pt::paint_pixels_desc ex_desc{ex_stride, 0.0f, pt_pixel_format_argb8888_premultiplied,
                                  {0u, 0u, 0u}};
    check(paint_window.paint_pixels_ex(std::span<uint32_t>(padded), ex_width, ex_height,
                                       ex_desc));
    std::vector<uint32_t> tight_ex(static_cast<size_t>(ex_width) * ex_height);
    check(paint_window.paint_pixels_ex(std::span<uint32_t>(tight_ex), ex_width, ex_height,
                                       pt::paint_pixels_desc{}));
    bool stride_rows_match = true;
    bool ex_marked = false;
    for (int row = 0; row < ex_height; ++row) {
        for (int col = 0; col < ex_width; ++col) {
            const uint32_t value =
                padded[static_cast<size_t>(row) * ex_stride + static_cast<size_t>(col)];
            stride_rows_match = stride_rows_match &&
                                value == tight_ex[static_cast<size_t>(row) * ex_width +
                                                  static_cast<size_t>(col)];
            ex_marked = ex_marked || value == painted_value;
        }
    }
    check(stride_rows_match);
    check(ex_marked);

    std::vector<uint32_t> scaled(static_cast<size_t>(ex_width) * 2u * ex_height * 2u);
    pt::paint_pixels_desc scale_desc{0, 2.0f, pt_pixel_format_argb8888_premultiplied,
                                     {0u, 0u, 0u}};
    check(paint_window.paint_pixels_ex(std::span<uint32_t>(scaled), ex_width * 2,
                                       ex_height * 2, scale_desc));
    check(std::find(scaled.begin(), scaled.end(), painted_value) != scaled.end());

    check(!paint_window.paint_pixels_ex(std::span<uint32_t>(padded), ex_width, ex_height,
                                        pt::paint_pixels_desc{ex_width - 1, 0.0f,
                                                              pt_pixel_format_argb8888_premultiplied,
                                                              {0u, 0u, 0u}}));
    check(!paint_window.paint_pixels_ex(std::span<uint32_t>(padded), ex_width, ex_height,
                                        pt::paint_pixels_desc{0, -1.0f,
                                                              pt_pixel_format_argb8888_premultiplied,
                                                              {0u, 0u, 0u}}));
    check(!paint_window.paint_pixels_ex(std::span<uint32_t>(padded), ex_width, ex_height,
                                        pt::paint_pixels_desc{0, 1.0f,
                                                              static_cast<pt_pixel_format>(1),
                                                              {0u, 0u, 0u}}));
    check(!paint_window.paint_pixels_ex(std::span<uint32_t>(padded), ex_width, ex_height,
                                        pt::paint_pixels_desc{0, 0.0f,
                                                              pt_pixel_format_argb8888_premultiplied,
                                                              {1u, 0u, 0u}}));
    // A span that only holds width * height cannot back a wider stride.
    std::vector<uint32_t> tight(static_cast<size_t>(ex_width) * ex_height);
    check(!paint_window.paint_pixels_ex(std::span<uint32_t>(tight), ex_width, ex_height,
                                        ex_desc));
    check(!paint_window.paint_pixels_ex(std::span<uint32_t>(padded), 0, ex_height, ex_desc));
    pt::window null_paint_window;
    check(!null_paint_window.paint_pixels_ex(std::span<uint32_t>(padded), ex_width,
                                             ex_height, ex_desc));

    painted.clear_paint_callback();
    const int cleared_calls = paint_state.calls;
    check(paint_window.paint_pixels(frame_span, frame_width, frame_height));
    check(paint_state.calls == cleared_calls);
    painted.request_redraw();
    painted.request_redraw_rect(-4.0f, -4.0f, 8.0f, 8.0f);
    painted.request_redraw_rect(0.0f, 0.0f, 0.0f, 0.0f);

    // A null context is the failure path for every helper: geometry is zero,
    // optional queries are empty and drawing is a no-op.
    pt::paint_context null_paint(nullptr);
    check(null_paint.get() == nullptr);
    check(null_paint.width() == 0.0f);
    check(null_paint.height() == 0.0f);
    check(null_paint.scale() == 1.0f);
    check(!null_paint.clip().has_value());
    check(null_paint.measure_text("sans", 12.0f, pt_font_weight_regular,
                                  pt_font_slant_upright, "Pt") == 0.0f);
    check(!null_paint.metrics("sans", 12.0f, pt_font_weight_regular,
                              pt_font_slant_upright)
               .has_value());
    check(!null_paint.font_has_glyph("sans", 12.0f, static_cast<uint32_t>('P')));
    null_paint.push_clip(0.0f, 0.0f, 1.0f, 1.0f)
        .fill_rect(0.0f, 0.0f, 1.0f, 1.0f, paint_fill)
        .fill_rounded_rect(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, paint_fill)
        .stroke_rect(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, paint_fill)
        .draw_line(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, paint_fill)
        .draw_text("sans", 12.0f, pt_font_weight_regular, pt_font_slant_upright, 0.0f, 0.0f,
                   "Pt", paint_fill)
        .pop_clip();

    /* ---- primary selection: copied, freed string instead of raw malloc ---- */
    // A programmatic set has no triggering event serial, so it publishes
    // nothing and returns false; the explicit serial path is the fallback.
    check(!window.set_primary("primary payload"));
    check(window.primary().empty());
    check(!window.set_primary_serial("primary payload", 0));
    check(window.primary().empty());
    check(window.set_primary_serial("primary payload", 7));
    check(window.primary() == "primary payload");
    const std::string unicode_primary = "primary \xE4\xB8\x96 na\xC3\xAFve";
    check(window.set_primary_serial(unicode_primary, 9));
    check(window.primary() == unicode_primary);
    check(window.set_primary_serial(std::string_view{}, 11));
    check(window.primary().empty());
    for (int i = 0; i < 8; ++i) {
        check(window.primary().empty());
    }

    /* ---- raw focus/key/pointer callbacks: real dispatch through C ---- */
    focus_capture focus_state;
    window.on_focus(focus_probe, &focus_state);
    pt_window_input_keyboard_focus(window.get(), true, 7);
    check(focus_state.calls == 1);
    check(focus_state.window == window.get());
    check(focus_state.focused);
    pt_window_input_keyboard_focus(window.get(), false, 8);
    check(focus_state.calls == 2);
    check(!focus_state.focused);
    window.on_focus(nullptr);
    pt_window_input_keyboard_focus(window.get(), true, 9);
    check(focus_state.calls == 2);
    pt_window_input_keyboard_focus(window.get(), false, 10);

    key_event_capture key_state;
    legacy_key_capture legacy_state;
    window.on_key_event(key_event_probe, &key_state);
    window.on_key(legacy_key_probe, &legacy_state);
    pt_window_input_key(window.get(), 0x61u, static_cast<uint32_t>(pt_key_modifier_ctrl), "a",
                        true);
    check(key_state.calls == 1);
    check(key_state.window == window.get());
    check(key_state.key == 0x61u);
    check(key_state.modifiers == static_cast<uint32_t>(pt_key_modifier_ctrl));
    check(key_state.text == "a");
    check(legacy_state.calls == 0); // consumed before the legacy callback
    key_state.consume = false;
    pt_window_input_key(window.get(), 0xFF09u, 0, nullptr, true);
    check(key_state.calls == 2);
    check(key_state.text.empty());
    check(legacy_state.calls == 1); // not consumed, so legacy sees it
    window.on_key_event(nullptr);
    window.on_key(nullptr, nullptr);

    pointer_capture pointer_state;
    window.on_pointer(pointer_probe, &pointer_state);
    pt_window_input_pointer_enter(window.get(), 10.0f, 20.0f);
    pt_window_input_pointer_motion(window.get(), 12.0f, 22.0f, 1);
    pt_window_input_pointer_button(window.get(), 0x110u, true, 12.0f, 22.0f, 5, 100);
    pt_window_input_pointer_button(window.get(), 0x110u, false, 12.0f, 22.0f, 0, 101);
    pt_window_input_axis(window.get(), 12.0f, 22.0f, 15.0f);
    pt_window_input_pointer_leave(window.get());
    check(pointer_state.window == window.get());
    check(pointer_state.events.size() == 6);
    if (pointer_state.events.size() == 6) {
        check(pointer_state.events[0].phase == pt_pointer_phase_enter);
        check(pointer_state.events[0].x == 10.0f);
        check(pointer_state.events[0].y == 20.0f);
        check(pointer_state.events[1].phase == pt_pointer_phase_motion);
        check(pointer_state.events[1].delta_x == 2.0f);
        check(pointer_state.events[1].delta_y == 2.0f);
        check(pointer_state.events[2].phase == pt_pointer_phase_down);
        check(pointer_state.events[2].button == 0x110u);
        check(pointer_state.events[2].pressed);
        check(pointer_state.events[2].click_count == 1);
        check(pointer_state.events[3].phase == pt_pointer_phase_up);
        check(!pointer_state.events[3].pressed);
        check(pointer_state.events[4].phase == pt_pointer_phase_axis);
        check(pointer_state.events[4].delta_y == 15.0f);
        check(pointer_state.events[5].phase == pt_pointer_phase_leave);
    }
    window.on_pointer(nullptr);

    /* ---- custom IME callbacks: dispatch, payload copy, clear ---- */
    ime_capture ime_state;
    window.set_ime_callbacks(ime_commit_probe, ime_preedit_probe, &ime_state)
        .set_ime_enabled(true)
        .set_ime_cursor_rect(4, 8, 12, 16);
    pt_window_input_ime_compose(window.get(), "hello \xE4\xB8\x96", "ni", 0, 2, 0, 0);
    check(ime_state.commits == 1);
    check(ime_state.commit == "hello \xE4\xB8\x96");
    check(ime_state.window == window.get());
    check(ime_state.preedits == 1);
    check(ime_state.preedit == "ni");
    check(ime_state.begin == 0);
    check(ime_state.end == 2);
    pt_window_input_ime_compose(window.get(), nullptr, "", -1, -1, 0, 0);
    check(ime_state.commits == 1);
    check(ime_state.preedits == 2);
    check(ime_state.preedit.empty());
    check(ime_state.begin == -1);
    check(ime_state.end == -1);
    window.set_ime_callbacks();
    pt_window_input_ime_compose(window.get(), "ignored", "ignored", 0, 0, 0, 0);
    check(ime_state.commits == 1);
    check(ime_state.preedits == 2);
    window.set_ime_enabled(false);

    /* ---- input panel requests: state transitions and invalid wrapper ---- */
    check(window.show_input_panel());
    check(!window.show_input_panel()); // desired state already shown
    check(window.hide_input_panel());
    check(!window.hide_input_panel()); // desired state already hidden
    pt::window invalid_window;
    check(!invalid_window.show_input_panel());
    check(!invalid_window.hide_input_panel());

    /* ---- fd watches: real pipe readiness, hangup, self-unwatch ---- */
    int pipe_fds[2] = {-1, -1};
    check(pipe(pipe_fds) == 0);
    if (pipe_fds[0] >= 0 && pipe_fds[1] >= 0) {
        fd_capture fd_state;
        const uint64_t read_watch =
            application.watch_fd(pipe_fds[0], pt_fd_event_read, fd_probe, &fd_state);
        check(read_watch != 0);
        check(::write(pipe_fds[1], "x", 1) == 1);
        check(application.run_for(80) == 0);
        check(fd_state.calls >= 1);
        check((fd_state.last & pt_fd_event_read) != 0);
        application.unwatch_fd(read_watch);
        fd_state.calls = 0;
        check(::write(pipe_fds[1], "y", 1) == 1);
        check(application.run_for(40) == 0);
        check(fd_state.calls == 0);

        // One watch per descriptor: a second watch on the same fd is rejected.
        fd_capture first_watch_state;
        fd_capture duplicate_watch_state;
        const uint64_t first_watch =
            application.watch_fd(pipe_fds[0], pt_fd_event_read, fd_probe, &first_watch_state);
        check(first_watch != 0);
        check(application.watch_fd(pipe_fds[0], pt_fd_event_read, fd_probe,
                                   &duplicate_watch_state) == 0);
        check(::write(pipe_fds[1], "z", 1) == 1);
        check(application.run_for(80) == 0);
        check(first_watch_state.calls >= 1);
        check(duplicate_watch_state.calls == 0);
        application.unwatch_fd(first_watch);

        fd_self_capture self_state;
        self_state.app = application.get();
        self_state.watch_id = application.watch_fd(pipe_fds[0], pt_fd_event_read, fd_self_probe,
                                                   &self_state);
        check(self_state.watch_id != 0);
        check(application.run_for(80) == 0);
        check(self_state.calls == 1);
        check((self_state.last & pt_fd_event_read) != 0);
        check(::write(pipe_fds[1], "w", 1) == 1);
        check(application.run_for(40) == 0);
        check(self_state.calls == 1);

        fd_capture owned_state;
        int owned_pipe[2] = {-1, -1};
        check(::pipe(owned_pipe) == 0);
        const uint64_t owned_watch = application.watch_fd_owned(
            owned_pipe[0], pt_fd_event_read, fd_probe, &owned_state);
        check(owned_watch != 0);
        // The duplicate check covers owned descriptors too and must not close
        // the fd that the first watch owns.
        fd_capture owned_duplicate;
        check(application.watch_fd(owned_pipe[0], pt_fd_event_read, fd_probe,
                                   &owned_duplicate) == 0);
        check(::fcntl(owned_pipe[0], F_GETFD) != -1);
        check(::write(owned_pipe[1], "o", 1) == 1);
        check(application.run_for(80) == 0);
        check(owned_state.calls >= 1);
        application.unwatch_fd(owned_watch);
        errno = 0;
        check(::fcntl(owned_pipe[0], F_GETFD) == -1 && errno == EBADF);
        check(::close(owned_pipe[1]) == 0);

        // Destroying the app closes every owned descriptor.
        int destroy_pipe[2] = {-1, -1};
        check(::pipe(destroy_pipe) == 0);
        {
            pt::app owned_app("pt.tests.cpp.owned");
            check(owned_app.valid());
            check(owned_app.watch_fd_owned(destroy_pipe[0], pt_fd_event_read, fd_probe,
                                           &owned_state) != 0);
        }
        errno = 0;
        check(::fcntl(destroy_pipe[0], F_GETFD) == -1 && errno == EBADF);
        check(::close(destroy_pipe[1]) == 0);

        fd_capture hangup_state;
        const uint64_t hangup_watch =
            application.watch_fd(pipe_fds[0], pt_fd_event_read, fd_probe, &hangup_state);
        check(hangup_watch != 0);
        check(::close(pipe_fds[1]) == 0);
        pipe_fds[1] = -1;
        check(application.run_for(80) == 0);
        check(hangup_state.calls >= 1);
        check((hangup_state.last & (pt_fd_event_hangup | pt_fd_event_error)) != 0);
        application.unwatch_fd(hangup_watch);

        fd_capture never_state;
        check(application.watch_fd(-1, pt_fd_event_read, fd_probe, &never_state) == 0);
        check(application.watch_fd(pipe_fds[0], 0, fd_probe, &never_state) == 0);
        application.unwatch_fd(0);
        check(::close(pipe_fds[0]) == 0);
        pipe_fds[0] = -1;
    }

    pt::widget card = pt::card(column.get());
    card.set_radius(12.0f).set_background(pt_rgb(0x161D28)).set_gap(4.0f);
    pt::widget row = pt::row(card.get());
    row.box_align(pt_align_center).box_justify(pt_justify_center);
    pt::spacer(row.get());
    pt::divider(row.get());

    bool fired = false;
    const uint64_t timer = pt_app_add_timeout(application.get(), 5, on_timer, &fired);
    check(timer != 0);
    window.show();
    check(application.run_for(80) == 0);
    check(fired);
    check(window.is_open());
    check(application.get() != nullptr);

    /* ---- 1.2 pickers: model, value round trips, headless popups, callbacks ---- */
    pt::window picker_window(application, "cpp pickers", 640, 520);
    check(picker_window.valid());
    pt::widget picker_root = picker_window.root();
    pt::widget picker_column = pt::column(picker_root.get());
    picker_column.set_gap(6.0f).set_padding(8.0f, 8.0f, 8.0f, 8.0f);
    picker_window.show();
    pt_canvas *canvas = pt_canvas_create(640, 520, 1.0f);
    check(canvas != nullptr);

    /* searchable select: model, query, programmatic open/close, commit event */
    pt::widget sel = pt::select(picker_column.get());
    check(sel.valid());
    check(sel.kind() == pt_widget_combobox);
    sel.set_width(260.0f);
    check(sel.select_placeholder().empty());
    sel.set_select_placeholder("Region");
    check(sel.select_placeholder() == "Region");
    int select_marker = 7;
    int select_marker_two = 8;
    sel.add_select_item("North")
        .add_select_item("Norfolk")
        .add_select_item_with_data("South", &select_marker);
    check(sel.select_item_count() == 3);
    check(sel.select_item_text(0) == "North");
    check(sel.select_item_text(1) == "Norfolk");
    check(sel.select_item_text(2) == "South");
    check(sel.select_item_text(9).empty());
    check(sel.select_item_data(2) == &select_marker);
    check(sel.select_item_data(9) == nullptr);
    sel.set_select_item_data(0, &select_marker_two);
    check(sel.select_item_data(0) == &select_marker_two);
    sel.insert_select_item(1, "Amber");
    check(sel.select_item_count() == 4);
    check(sel.select_item_text(1) == "Amber");
    sel.remove_select_item(1);
    check(sel.select_item_count() == 3);
    check(sel.select_item_text(1) == "Norfolk");
    check(sel.select_selected() == pt_list_index_none);
    sel.set_select_selected(2);
    check(sel.select_selected() == 2);
    sel.set_select_selected(99);
    check(sel.select_selected() == pt_list_index_none);
    sel.set_select_selected(1);
    sel.clear_select_selection();
    check(sel.select_selected() == pt_list_index_none);
    sel.set_select_query("nor");
    check(!sel.is_select_open()); // a programmatic query does not open
    check(sel.select_match_count() == 2);
    check(sel.select_match_item(0) == 0);
    check(sel.select_match_item(1) == 1);
    check(sel.select_query() == "nor");
    sel.set_select_query("");
    check(sel.select_match_count() == 3);
    check(sel.select_max_popup_rows() == 8);
    sel.set_select_max_popup_rows(0);
    check(sel.select_max_popup_rows() == 1);
    sel.set_select_max_popup_rows(9999);
    check(sel.select_max_popup_rows() == PT_PICKER_MAX_CANDIDATES);
    sel.set_select_max_popup_rows(8);
    check(pt_window_paint(picker_window.get(), canvas));
    sel.open_select();
    check(sel.is_select_open());
    sel.close_select();
    check(!sel.is_select_open());
    select_probe select_state;
    sel.on_select_change(select_changed, &select_state);
    const pt::rect select_bounds = sel.bounds();
    check(select_bounds.width > 0.0f);
    click_at(picker_window.get(), select_bounds.x + select_bounds.width - 8.0f,
             select_bounds.y + select_bounds.height * 0.5f);
    check(sel.is_select_open());
    type_key(picker_window.get(), XKB_KEY_Return);
    check(!sel.is_select_open());
    check(select_state.changes == 1);
    check(select_state.index == 0);
    check(sel.select_selected() == 0);

    /* date picker: value/format/text round trips and a calendar commit event */
    pt::widget date_field = pt::date_picker(picker_column.get());
    check(date_field.valid());
    date_field.set_width(240.0f);
    check(pt::date_valid({2026, 11, 22}));
    check(pt::date_valid({2024, 2, 29}));
    check(!pt::date_valid({2026, 13, 1}));
    check(!pt::date_valid({2026, 2, 30}));
    check(!pt::date_valid({2026, 0, 1}));
    const pt::date today_date = pt::date_today();
    check(!pt::date_valid(today_date) ||
          (today_date.year >= 1 && today_date.year <= 9999));
    check(date_field.date_value().year == 0);
    check(date_field.set_date(pt::date{2026, 11, 22}));
    const pt::date stored_date = date_field.date_value();
    check(stored_date.year == 2026 && stored_date.month == 11 &&
          stored_date.day == 22);
    check(!date_field.set_date(pt::date{2026, 13, 1}));
    const pt::date unchanged_date = date_field.date_value();
    check(unchanged_date.year == 2026 && unchanged_date.month == 11 &&
          unchanged_date.day == 22);
    check(date_field.date_format() == pt_date_format_iso);
    date_field.set_date_format(pt_date_format_dmy);
    check(date_field.date_text() == "22/11/2026");
    date_field.set_date_format(pt_date_format_iso);
    check(date_field.date_text() == "2026-11-22");
    date_field.set_date_placeholder("Pick a date");
    check(date_field.date_placeholder() == "Pick a date");
    check(date_field.set_date_text("2024-02-29"));
    check(date_field.date_value().year == 2024 && date_field.date_value().month == 2 &&
          date_field.date_value().day == 29);
    check(!date_field.set_date_text("2024-02-30"));
    check(date_field.date_value().year == 2024 && date_field.date_value().day == 29);
    check(date_field.set_date_text(""));
    check(date_field.date_value().year == 0);
    check(date_field.date_text().empty());
    date_field.clear_date();
    check(date_field.date_value().year == 0);
    check(date_field.set_date(pt::date{2026, 11, 22}));
    check(pt_window_paint(picker_window.get(), canvas));
    date_field.open_date_picker();
    check(date_field.is_date_picker_open());
    date_field.close_date_picker();
    check(!date_field.is_date_picker_open());
    date_probe date_state;
    date_field.on_date_change(date_changed, &date_state);
    const pt::rect date_bounds = date_field.bounds();
    check(date_bounds.width > 0.0f);
    click_at(picker_window.get(), date_bounds.x + date_bounds.width * 0.5f,
             date_bounds.y + date_bounds.height * 0.5f);
    check(date_field.is_date_picker_open());
    type_key(picker_window.get(), XKB_KEY_Right);
    type_key(picker_window.get(), XKB_KEY_Return);
    check(!date_field.is_date_picker_open());
    check(date_state.changes == 1);
    check(date_state.value.year == 2026 && date_state.value.month == 11 &&
          date_state.value.day == 23);
    check(date_field.date_value().day == 23);

    /* number picker: range/step/value clamping, text commit, step event */
    pt::widget number_field = pt::number_picker(picker_column.get());
    check(number_field.valid());
    number_field.set_width(200.0f);
    check(number_field.number_min() == 0.0);
    check(number_field.number_max() == 100.0);
    check(number_field.number_step() == 1.0);
    check(!number_field.number_has_value());
    check(number_field.number_value() == 0.0); // empty reads as 0
    number_field.set_number_range(0.0, 10.0);
    check(number_field.number_min() == 0.0);
    check(number_field.number_max() == 10.0);
    number_field.set_number_range(10.0, 0.0); // rejected: min > max
    check(number_field.number_min() == 0.0 && number_field.number_max() == 10.0);
    number_field.set_number_step(2.0);
    check(number_field.number_step() == 2.0);
    number_field.set_number_step(-1.0); // rejected: not positive
    check(number_field.number_step() == 2.0);
    number_field.set_number_value(7.0);
    check(number_field.number_has_value());
    check(number_field.number_value() == 7.0);
    check(number_field.number_text() == "7");
    number_field.set_number_value(99.0); // clamped
    check(number_field.number_value() == 10.0);
    number_field.set_number_value(std::numeric_limits<double>::quiet_NaN());
    check(number_field.number_value() == 10.0); // non-finite ignored
    number_field.step_number_by(-1.0);
    check(number_field.number_value() == 8.0);
    check(number_field.set_number_text("3.5"));
    check(number_field.number_value() == 3.5);
    check(!number_field.set_number_text("not a number"));
    check(number_field.number_value() == 3.5);
    check(number_field.number_text() == "3.5");
    number_field.set_number_placeholder("Amount");
    check(number_field.number_placeholder() == "Amount");
    check(number_field.set_number_text(""));
    check(!number_field.number_has_value());
    number_field.set_number_value(5.0);
    number_field.clear_number();
    check(!number_field.number_has_value());
    check(number_field.number_text().empty());
    check(pt_window_paint(picker_window.get(), canvas));
    number_probe number_state;
    number_field.on_number_change(number_changed, &number_state);
    const pt::rect number_bounds = number_field.bounds();
    check(number_bounds.width > 0.0f);
    click_at(picker_window.get(), number_bounds.x + number_bounds.width * 0.5f,
             number_bounds.y + number_bounds.height * 0.5f);
    type_key(picker_window.get(), XKB_KEY_Up);
    check(number_state.changes == 1);
    check(number_state.value == 2.0); // empty steps from clamped zero, then +2
    check(number_field.number_value() == 2.0);

    /* colour picker: hex round trips, palette cap, preset commit event */
    pt::widget color_field = pt::color_picker(picker_column.get());
    check(color_field.valid());
    color_field.set_width(240.0f);
    check(color_field.color() == 0x000000FFu);
    check(color_field.color_hex() == "#000000");
    color_field.set_color(0x123456FFu);
    check(color_field.color() == 0x123456FFu);
    check(color_field.color_hex() == "#123456");
    check(color_field.set_color_hex("#ABCDEF"));
    check(color_field.color() == 0xABCDEFFFu);
    check(color_field.color_hex() == "#ABCDEF");
    check(!color_field.set_color_hex("bogus"));
    check(color_field.color() == 0xABCDEFFFu);
    check(color_field.set_color_hex("#11223344"));
    check(color_field.color() == 0x11223344u);
    check(color_field.color_hex() == "#11223344");
    check(color_field.color_preset_count() == 24);
    color_field.add_color_preset(0x010203FFu);
    check(color_field.color_preset_count() == 25);
    check(color_field.color_preset(24) == 0x010203FFu);
    for (uint32_t i = 0; i < PT_COLOR_MAX_PRESETS + 2u; ++i) {
        color_field.add_color_preset(0x040506FFu);
    }
    check(color_field.color_preset_count() == PT_COLOR_MAX_PRESETS);
    color_field.clear_color_presets();
    check(color_field.color_preset_count() == 0);
    color_field.add_color_preset(0x123456FFu);
    color_field.add_color_preset(0x654321FFu);
    check(color_field.color_preset(1) == 0x654321FFu);
    check(pt_window_paint(picker_window.get(), canvas));
    color_field.open_color_picker();
    check(color_field.is_color_picker_open());
    color_field.close_color_picker();
    check(!color_field.is_color_picker_open());
    color_field.set_color(0xFFFFFFFFu); // differ from the presets so the commit fires
    color_probe color_state;
    color_field.on_color_change(color_changed, &color_state);
    const pt::rect color_bounds = color_field.bounds();
    check(color_bounds.width > 0.0f);
    click_at(picker_window.get(), color_bounds.x + color_bounds.width * 0.5f,
             color_bounds.y + color_bounds.height * 0.5f);
    check(color_field.is_color_picker_open());
    type_key(picker_window.get(), XKB_KEY_Right);
    type_key(picker_window.get(), XKB_KEY_Return);
    check(!color_field.is_color_picker_open());
    check(color_state.changes == 1);
    check(color_state.value == 0x123456FFu || color_state.value == 0x654321FFu);
    check(color_field.color() == color_state.value);

    /* NULL handles are no-ops and report the defaults. */
    pt::widget null_picker;
    check(null_picker.select_item_count() == 0);
    check(null_picker.select_selected() == pt_list_index_none);
    check(null_picker.number_min() == 0.0 && null_picker.number_max() == 0.0);
    check(null_picker.color_preset_count() == 0);
    check(null_picker.date_value().year == 0);
    check(!null_picker.set_date(pt::date{2026, 1, 1}));
    check(!null_picker.set_number_text("1"));
    check(!null_picker.set_color_hex("#000000"));
    check(!null_picker.is_select_open());

    /* ---- 2.4 activation / foreign fallback and wrapper rejection ---- */
    run_activation_foreign_headless_test(application);

    /* ---- owned getters: bad_alloc must not leak the C allocation ---- */
    run_bad_alloc_hardening_test(window, table);

    pt_canvas_destroy(canvas);

    const int result = failures == 0 ? 0 : 1;
    std::printf("cpp binding: %d checks, %d failure(s)\n", checks, failures);
    return result;
}
