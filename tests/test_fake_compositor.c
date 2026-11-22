/*
 * In-process Wayland wire replay facility self-test and first consumers
 * (1.3 wave 1).
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "fake_compositor.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#define wl_surface_attach 1u
#define wl_surface_damage 2u
#define wl_surface_frame 3u
#define wl_surface_commit 6u
#define xdg_surface_ack_configure 4u
#define xdg_wm_base_pong 3u

static pt_icon_image *make_test_icon(void) {
    uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) {
        pixels[i] = 0x80FF0000u | (uint32_t)i;
    }
    size_t size = 0;
    uint8_t *png = pt_png_encode_argb_premultiplied(pixels, 4, 4, &size);
    pt_check(png != NULL && size > 0);
    if (!png) {
        return NULL;
    }
    pt_icon_image *image = pt_icon_image_load_memory(png, size);
    pt_check(image != NULL);
    free(png);
    return image;
}

static void wait_configured(fake_compositor *fc, pt_app *app, pt_window *window) {
    for (int i = 0; i < 300 && window && !window->configured; ++i) {
        fake_compositor_dispatch(fc, app, 5);
    }
}

static size_t collect_buffers(fake_compositor *fc, uint32_t *ids, size_t capacity) {
    size_t count = 0;
    size_t total = fake_compositor_request_count(fc);
    for (size_t i = 0; i < total && count < capacity; ++i) {
        fc_request request;
        if (!fake_compositor_request_at(fc, i, &request)) {
            continue;
        }
        if (request.kind == FC_K_POOL && request.opcode == 0) {
            ids[count++] = request.new_id;
        }
    }
    return count;
}

static bool id_in(const uint32_t *ids, size_t count, uint32_t id) {
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

/* ---------- handshake, globals, event injection ---------- */

static void test_handshake_and_events(void) {
    fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.fake.handshake");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_check(fake_compositor_error(&fc) == NULL);

    /* Every global platform.c binds is advertised and bound. */
    static const uint32_t globals[] = {
        FC_K_COMPOSITOR, FC_K_SHM,     FC_K_SEAT,        FC_K_WM_BASE,
        FC_K_ICON_MANAGER, FC_K_DECORATION_MANAGER, FC_K_FRACTIONAL_MANAGER,
        FC_K_VIEWPORTER, FC_K_TEXT_MANAGER, FC_K_GESTURES, FC_K_DATA_MANAGER,
        FC_K_PRIMARY_MANAGER, FC_K_TABLET_MANAGER,
    };
    for (size_t i = 0; i < sizeof(globals) / sizeof(globals[0]); ++i) {
        pt_check(fake_compositor_global_bound(&fc, globals[i]));
    }
    size_t binds = fake_compositor_count_requests(&fc, FC_K_REGISTRY, 0);
    pt_check(binds >= sizeof(globals) / sizeof(globals[0]));

    /* wl_display.sync is answered and the roundtrip completes. */
    size_t syncs = fake_compositor_count_requests(&fc, FC_K_DISPLAY, 0);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_DISPLAY, 0) > syncs);

    pt_window *window = pt_window_create(app, "fake-wire", 400, 300);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_widget *root = pt_window_root(window);
    pt_widget *entry = pt_entry_create(root, "wire");
    pt_check(entry != NULL);
    pt_check(pt_widget_focus(entry));
    pt_window_show(window);
    wait_configured(&fc, app, window);
    pt_check(window->configured);
    pt_check(window->platform_window != NULL);
    pt_check(fake_compositor_settle(&fc, app));

    /* Toplevel role request sequence and decoration preference. */
    pt_check(fake_compositor_count_requests(&fc, FC_K_WM_BASE, 2) >= 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_XDG_SURFACE, 1) >= 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_TOPLEVEL, 2) >= 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_TOPLEVEL, 3) >= 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_XDG_SURFACE,
                                            xdg_surface_ack_configure) >= 1);
    fc_request decoration;
    pt_check(fake_compositor_last_request(&fc, FC_K_DECORATION, 1, &decoration));
    pt_check(decoration.nargs == 1 && decoration.args[0].u == 1u); /* client-side */
    uint32_t surface = fake_compositor_toplevel_surface(&fc);
    uint32_t toplevel = fake_compositor_toplevel(&fc);
    pt_check(surface != 0 && toplevel != 0);

    /* Frame lifecycle: paint drives attach/damage/frame/commit, and the
     * callback.done the server sends keeps the render loop alive. */
    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    fake_compositor_pump(&fc, app, 120);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(fake_compositor_count_requests(&fc, FC_K_SURFACE, wl_surface_attach) >= 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_SURFACE, wl_surface_frame) >= 1);
    pt_check(fake_compositor_count_requests(&fc, FC_K_SURFACE, wl_surface_commit) >= 2);
    pt_check(fake_compositor_count_requests(&fc, FC_K_SURFACE, wl_surface_damage) +
                 fake_compositor_count_requests(&fc, FC_K_SURFACE, 9) >=
             1);

    /* Pointer events: enter/motion/button reach the window with wire serials. */
    fake_compositor_pointer_enter(&fc, 7001u, surface, 40.0, 50.0);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check_near(window->pointer_x, 40.0f, 0.5);
    pt_check_near(window->pointer_y, 50.0f, 0.5);
    pt_check(window->pointer_inside);
    pt_check(app->platform->pointer_serial == 7001u);
    pt_check(app->platform->enter_serial == 7001u);

    fake_compositor_pointer_motion(&fc, 17u, 60.0, 80.0);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check_near(window->pointer_x, 60.0f, 0.5);
    pt_check_near(window->pointer_y, 80.0f, 0.5);

    fake_compositor_pointer_button(&fc, 7002u, 18u, 0x110u, true);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->pointer_serial == 7002u);
    pt_check((window->pointer_buttons & (1u << (0x110u & 31u))) != 0u);
    fake_compositor_pointer_button(&fc, 7003u, 19u, 0x110u, false);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check((window->pointer_buttons & (1u << (0x110u & 31u))) == 0u);

    /* Keyboard: keymap, enter, key press and modifiers reach the entry. */
    fake_compositor_keyboard_enter(&fc, 7100u, surface);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(window->keyboard_focused);
    pt_check(window->focused == entry);
    pt_check(app->platform->xkb_state != NULL);
    pt_check(app->platform->xkb_keymap != NULL);
    fake_compositor_keyboard_modifiers(&fc, 7101u, 0u, 0u, 0u, 0u);
    fake_compositor_keyboard_key(&fc, 7102u, 20u, 30u, true); /* KEY_A */
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(strcmp(pt_widget_get_text(entry), "a") == 0);
    pt_check(window->key_serial == 7102u);
    fake_compositor_keyboard_key(&fc, 7103u, 21u, 30u, false);
    fake_compositor_keyboard_leave(&fc, 7104u, surface);
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(!window->keyboard_focused);

    /* xdg_wm_base.ping is answered with pong carrying the same serial. */
    fake_compositor_wm_ping(&fc, 0x7788u);
    pt_check(fake_compositor_sync(&fc, app));
    fc_request pong;
    pt_check(fake_compositor_last_request(&fc, FC_K_WM_BASE, xdg_wm_base_pong, &pong));
    pt_check(pong.nargs == 1 && pong.args[0].u == 0x7788u);

    /* Wave-2 interface: a selection offer reaches the clipboard state. */
    static const char *const mimes[] = {"text/plain;charset=utf-8", "text/plain"};
    pt_check(fake_compositor_selection_offer(&fc, mimes, 2, "wire clipboard"));
    pt_check(fake_compositor_sync(&fc, app));
    pt_check(app->platform->selection != NULL);
    if (app->platform->selection) {
        pt_check(app->platform->selection->mime_count >= 2);
        pt_check(strcmp(app->platform->selection->mimes[0],
                        "text/plain;charset=utf-8") == 0);
    }
    char *clipboard = pt_platform_clipboard_get(window);
    pt_check(clipboard != NULL && strcmp(clipboard, "wire clipboard") == 0);
    free(clipboard);

    pt_check(!fake_compositor_request_overflowed(&fc));
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_check(fake_compositor_error(&fc) == NULL);

    pt_canvas_destroy(canvas);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- toplevel-icon submission ---------- */

static void test_icon_no_manager(void) {
    fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL & ~FC_OPT_ICON));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.fake.noicon");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_check(!fake_compositor_global_bound(&fc, FC_K_ICON_MANAGER));
    pt_window *window = pt_window_create(app, "no-icon-manager", 320, 240);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_icon_image *icon = make_test_icon();
    pt_check(icon != NULL);
    if (icon) {
        fake_compositor_clear(&fc);
        pt_window_set_icon(window, icon);
        pt_check(fake_compositor_sync(&fc, app));
        pt_check(fake_compositor_count_requests(&fc, FC_K_ICON_MANAGER, 1) == 0);
        pt_check(fake_compositor_count_requests(&fc, FC_K_ICON_MANAGER, 2) == 0);
        pt_check(fake_compositor_count_requests(&fc, FC_K_ICON, 2) == 0);
        pt_icon_image_destroy(icon);
    }
    pt_check(fake_compositor_error(&fc) == NULL);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

static void test_icon_submission(void) {
    fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.fake.icon");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_check(fake_compositor_global_bound(&fc, FC_K_ICON_MANAGER));
    pt_window *window = pt_window_create(app, "icon-wire", 400, 300);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    pt_window_show(window);
    wait_configured(&fc, app, window);
    pt_check(window->configured);
    pt_canvas *canvas = pt_canvas_create(400, 300, 1.0f);
    pt_check(canvas != NULL);
    pt_check(pt_window_paint(window, canvas));
    fake_compositor_pump(&fc, app, 100);
    pt_check(fake_compositor_sync(&fc, app));

    /* Frame buffers exist before the icon; the icon must get its own buffer. */
    uint32_t frame_buffers[16];
    size_t frame_count = collect_buffers(&fc, frame_buffers, 16);
    pt_check(frame_count >= 3);
    uint32_t toplevel = fake_compositor_toplevel(&fc);
    pt_check(toplevel != 0);

    pt_icon_image *icon1 = make_test_icon();
    pt_check(icon1 != NULL);
    if (!icon1) {
        pt_canvas_destroy(canvas);
        pt_window_free(window);
        pt_app_destroy(app);
        fake_compositor_stop(&fc);
        return;
    }
    fake_compositor_clear(&fc);
    pt_window_set_icon(window, icon1);
    pt_check(fake_compositor_sync(&fc, app));

    fc_request create_icon;
    pt_check(fake_compositor_last_request(&fc, FC_K_ICON_MANAGER, 1, &create_icon));
    uint32_t icon_id1 = create_icon.new_id;
    pt_check(icon_id1 != 0);
    fc_request icon_buffer;
    pt_check(fake_compositor_last_request(&fc, FC_K_ICON, 2, &icon_buffer));
    pt_check(icon_buffer.nargs == 2);
    pt_check(icon_buffer.args[1].i == 1); /* window scale */
    uint32_t icon_buffer1 = icon_buffer.args[0].u;

    fc_request create_buffer;
    pt_check(fake_compositor_last_request(&fc, FC_K_POOL, 0, &create_buffer));
    pt_check(create_buffer.new_id == icon_buffer1);
    pt_check(create_buffer.args[2].i == PT_ICON_WINDOW_SIZE);
    pt_check(create_buffer.args[3].i == PT_ICON_WINDOW_SIZE);
    pt_check(create_buffer.args[4].i == PT_ICON_WINDOW_SIZE * 4);
    pt_check(create_buffer.args[5].u == 0u); /* WL_SHM_FORMAT_ARGB8888 */
    /* A frame buffer and the icon buffer are different wl_buffer objects. */
    pt_check(!id_in(frame_buffers, frame_count, icon_buffer1));

    fc_request set_icon;
    pt_check(fake_compositor_last_request(&fc, FC_K_ICON_MANAGER, 2, &set_icon));
    pt_check(set_icon.nargs == 2);
    pt_check(set_icon.args[0].u == toplevel);
    pt_check(set_icon.args[1].u == icon_id1);
    pt_check(window->icon == icon1);
    pt_check(((pt_wayland_window *)window->platform_window)->toplevel_icon_submissions ==
             1);

    /* Replacing the icon releases the old icon and buffer as a pair, then
     * submits a fresh immutable buffer. */
    pt_icon_image *icon2 = make_test_icon();
    pt_check(icon2 != NULL);
    if (icon2) {
        fake_compositor_clear(&fc);
        pt_window_set_icon(window, icon2);
        pt_check(fake_compositor_sync(&fc, app));
        fc_request destroyed_icon;
        pt_check(fake_compositor_last_request(&fc, FC_K_ICON, 0, &destroyed_icon));
        pt_check(destroyed_icon.object == icon_id1);
        fc_request destroyed_buffer;
        pt_check(fake_compositor_last_request(&fc, FC_K_BUFFER, 0, &destroyed_buffer));
        pt_check(destroyed_buffer.object == icon_buffer1);
        fc_request create_icon2;
        pt_check(fake_compositor_last_request(&fc, FC_K_ICON_MANAGER, 1, &create_icon2));
        pt_check(create_icon2.new_id != 0 && create_icon2.new_id != icon_id1);
        fc_request icon_buffer2;
        pt_check(fake_compositor_last_request(&fc, FC_K_ICON, 2, &icon_buffer2));
        pt_check(icon_buffer2.args[0].u != icon_buffer1);
        pt_check(icon_buffer2.args[1].i == 1);
        fc_request set_icon2;
        pt_check(fake_compositor_last_request(&fc, FC_K_ICON_MANAGER, 2, &set_icon2));
        pt_check(set_icon2.args[1].u == create_icon2.new_id);
        pt_check(window->icon == icon2);

        /* Clearing the slot sends set_icon(NULL) and releases the pair. */
        fake_compositor_clear(&fc);
        pt_window_set_icon(window, NULL);
        pt_check(fake_compositor_sync(&fc, app));
        fc_request cleared;
        pt_check(fake_compositor_last_request(&fc, FC_K_ICON_MANAGER, 2, &cleared));
        pt_check(cleared.args[1].u == 0u);
        fc_request destroyed_icon2;
        pt_check(fake_compositor_last_request(&fc, FC_K_ICON, 0, &destroyed_icon2));
        pt_check(destroyed_icon2.object == create_icon2.new_id);
        fc_request destroyed_buffer2;
        pt_check(fake_compositor_last_request(&fc, FC_K_BUFFER, 0, &destroyed_buffer2));
        pt_check(destroyed_buffer2.object == icon_buffer2.args[0].u);

        pt_icon_image_destroy(icon2);
    }
    pt_icon_image_destroy(icon1);
    pt_check(fake_compositor_error(&fc) == NULL);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_canvas_destroy(canvas);
    pt_window_free(window);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

/* ---------- resource stability ---------- */

static void test_fd_cycles(void) {
    fake_compositor fc;
    pt_check(fake_compositor_start(&fc, FC_OPT_ALL));
    if (fake_compositor_error(&fc)) {
        fprintf(stderr, "fake compositor start failed: %s\n",
                fake_compositor_error(&fc));
        return;
    }
    unsetenv("PT_HEADLESS");
    pt_app *app = pt_app_create("pt.tests.fake.fds");
    pt_check(app != NULL);
    if (!app) {
        fake_compositor_stop(&fc);
        return;
    }
    pt_check(fake_compositor_settle(&fc, app));
    pt_canvas *canvas = pt_canvas_create(240, 180, 1.0f);
    pt_check(canvas != NULL);

    /* Warm-up: font and fontconfig caches open files on first use. */
    pt_window *warm = pt_window_create(app, "warm", 240, 180);
    pt_window_show(warm);
    wait_configured(&fc, app, warm);
    pt_check(pt_window_paint(warm, canvas));
    fake_compositor_pump(&fc, app, 60);
    pt_window_free(warm);
    pt_check(fake_compositor_settle(&fc, app));

    int before = fake_compositor_fd_count();
    pt_check(before > 0);
    for (int i = 0; i < 24; ++i) {
        pt_window *window = pt_window_create(app, "cycle", 240, 180);
        pt_check(window != NULL);
        if (!window) {
            break;
        }
        pt_window_show(window);
        for (int spin = 0; spin < 20 && !window->configured; ++spin) {
            fake_compositor_dispatch(&fc, app, 3);
        }
        pt_check(pt_window_paint(window, canvas));
        fake_compositor_dispatch(&fc, app, 3);
        fake_compositor_clear(&fc);
        pt_window_free(window);
        pt_check(fake_compositor_sync(&fc, app));
    }
    pt_check(fake_compositor_settle(&fc, app));
    int after = fake_compositor_fd_count();
    pt_check(after > 0);
    pt_check(after <= before + 1);
    pt_check(fake_compositor_pending_fd_count(&fc) == 0);
    pt_check(!fake_compositor_request_overflowed(&fc));
    pt_check(fake_compositor_error(&fc) == NULL);

    pt_canvas_destroy(canvas);
    pt_app_destroy(app);
    fake_compositor_stop(&fc);
}

int main(void) {
    unsetenv("PT_HEADLESS");
    test_handshake_and_events();
    test_icon_no_manager();
    test_icon_submission();
    test_fd_cycles();
    return pt_test_finish("fake_compositor");
}