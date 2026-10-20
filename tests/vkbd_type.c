/*
 * Virtual-keyboard text injector for the live typing test.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * Sends keys through zwp_virtual_keyboard_v1 so a compositor routes them like
 * a physical keyboard. Only level-0 printable ASCII is supported; lowercase
 * letters, digits and space are enough for the typing probe.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *manager;

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    (void)data;
    (void)version;
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface,
                      zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        manager = wl_registry_bind(registry, name,
                                   &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static uint32_t keycode_for(struct xkb_keymap *keymap, xkb_keysym_t want) {
    xkb_keycode_t min = xkb_keymap_min_keycode(keymap);
    xkb_keycode_t max = xkb_keymap_max_keycode(keymap);
    for (xkb_keycode_t keycode = min; keycode <= max; ++keycode) {
        const xkb_keysym_t *syms = NULL;
        int count = xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, 0, &syms);
        for (int i = 0; i < count; ++i) {
            if (syms[i] == want) {
                return (uint32_t)keycode;
            }
        }
    }
    return 0;
}

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

static void sleep_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s TEXT\n", argv[0]);
        return 2;
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "vkbd_type: cannot connect to compositor\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) < 0 || !seat || !manager) {
        fprintf(stderr, "vkbd_type: seat or virtual-keyboard manager missing\n");
        return 1;
    }
    struct zwp_virtual_keyboard_v1 *vkbd =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager, seat);
    if (!vkbd || wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "vkbd_type: cannot create virtual keyboard\n");
        return 1;
    }

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!ctx || !keymap) {
        fprintf(stderr, "vkbd_type: cannot build keymap\n");
        return 1;
    }
    char *map = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!map) {
        fprintf(stderr, "vkbd_type: cannot serialize keymap\n");
        return 1;
    }
    size_t size = strlen(map) + 1;
    int fd = memfd_create("pt-vkbd-keymap", MFD_CLOEXEC);
    if (fd < 0 || write(fd, map, size) != (ssize_t)size) {
        fprintf(stderr, "vkbd_type: cannot stage keymap: %s\n", strerror(errno));
        return 1;
    }
    zwp_virtual_keyboard_v1_keymap(vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                                   (uint32_t)size);
    close(fd);
    free(map);
    zwp_virtual_keyboard_v1_modifiers(vkbd, 0, 0, 0, 0);
    if (wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "vkbd_type: keymap upload failed\n");
        return 1;
    }
    sleep_ms(120);

    for (const char *p = argv[1]; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch > 0x7e) {
            fprintf(stderr, "vkbd_type: only printable ASCII is supported\n");
            return 2;
        }
        uint32_t keycode = keycode_for(keymap, xkb_utf32_to_keysym(ch));
        if (!keycode) {
            fprintf(stderr, "vkbd_type: no level-0 keycode for '%c'\n", ch);
            return 1;
        }
        zwp_virtual_keyboard_v1_key(vkbd, now_ms(), keycode - 8u,
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
        wl_display_flush(display);
        sleep_ms(14);
        zwp_virtual_keyboard_v1_key(vkbd, now_ms(), keycode - 8u,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
        wl_display_flush(display);
        sleep_ms(14);
    }
    if (wl_display_roundtrip(display) < 0) {
        fprintf(stderr, "vkbd_type: key delivery failed\n");
        return 1;
    }
    sleep_ms(150);
    zwp_virtual_keyboard_v1_destroy(vkbd);
    zwp_virtual_keyboard_manager_v1_destroy(manager);
    wl_seat_destroy(seat);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    return 0;
}
