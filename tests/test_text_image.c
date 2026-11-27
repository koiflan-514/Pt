/*
 * Inline image side table: descriptor validation, sorting, ownership and the
 * edit shift/clamp helpers.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

#define placeholder "\xEF\xBF\xBC"

static pt_icon_image *make_icon(void) {
    size_t size = 0;
    uint32_t pixels[16];
    for (int i = 0; i < 16; ++i) {
        pixels[i] = 0x80FF0000u | (uint32_t)i;
    }
    uint8_t *png = pt_png_encode_argb_premultiplied(pixels, 4, 4, &size);
    pt_check(png != NULL && size > 0);
    pt_icon_image *image = pt_icon_image_load_memory(png, size);
    free(png);
    return image;
}

static pt_text_image descriptor(uint32_t start, pt_icon_image *image) {
    pt_text_image image_run = {0};
    image_run.start = start;
    image_run.image = image;
    return image_run;
}

static void raw_set_text(pt_widget *widget, const char *text) {
    free(widget->text);
    widget->text = pt_strdup(text);
}

static void test_set_get_roundtrip(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "A" placeholder "B");
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    char alt[201];
    memset(alt, 'x', 200);
    alt[200] = '\0';

    pt_text_image entry = descriptor(1, icon);
    entry.width = 12.5f;
    entry.height = -2.0f;
    entry.align = pt_text_image_align_middle;
    memcpy(entry.alt, alt, sizeof(entry.alt));
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 1);

    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 1);
    pt_check(got.image == icon);
    pt_check_near(got.width, 12.5f, 0.0001);
    pt_check_near(got.height, -2.0f, 0.0001);
    pt_check(got.align == pt_text_image_align_middle);
    pt_check(strlen(got.alt) == 127);
    pt_check(memcmp(got.alt, alt, 127) == 0);

    pt_check(!pt_widget_get_text_image(label, 1, &got));
    pt_check(!pt_widget_get_text_image(label, 0, NULL));
    pt_check(!pt_widget_get_text_image(NULL, 0, &got));

    /* The widget keeps the reference after the caller drops its own. */
    pt_icon_image_destroy(icon);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.image == icon);
    pt_check(pt_icon_image_width(got.image) == 4);

    pt_widget_clear_text_images(label);
    pt_check(pt_widget_get_text_image_count(label) == 0);
    pt_widget_clear_text_images(label);
    pt_widget_clear_text_images(NULL);

    /* A zero-initialised descriptor is the default alignment and intrinsic. */
    pt_icon_image *second = make_icon();
    pt_check(second != NULL);
    pt_text_image zero = descriptor(1, second);
    zero.width = 0.0f;
    zero.height = 0.0f;
    pt_check(pt_widget_set_text_images(label, &zero, 1));
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.align == pt_text_image_align_baseline);
    pt_check(got.width == 0.0f && got.height == 0.0f);
    pt_check(got.alt[0] == '\0');
    pt_widget_clear_text_images(label);

    pt_check(!pt_widget_set_text_images(NULL, &zero, 1));
    pt_check(pt_widget_set_text_images(label, NULL, 0));
    pt_check(pt_widget_get_text_image_count(label) == 0);
    pt_widget_destroy(label);
}

static void test_alt_truncation(pt_widget *root) {
    pt_widget *label = pt_label_create(root, placeholder);
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    pt_text_image entry = descriptor(0, icon);

    memset(entry.alt, 'a', sizeof(entry.alt));
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(strlen(got.alt) == 127);
    pt_check(got.alt[127] == '\0');

    /* A two-byte sequence straddling the 127-byte cap is dropped whole. */
    memset(entry.alt, 'a', sizeof(entry.alt));
    entry.alt[126] = '\xC3';
    entry.alt[127] = '\xA9';
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(strlen(got.alt) == 126);

    /* A sequence ending exactly at the cap is kept. */
    memset(entry.alt, 'a', sizeof(entry.alt));
    entry.alt[125] = '\xC3';
    entry.alt[126] = '\xA9';
    entry.alt[127] = '\0';
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(strlen(got.alt) == 127);
    pt_check(memcmp(got.alt + 125, "\xC3\xA9", 2) == 0);

    /* An unterminated alt buffer is read defensively, never past 128 bytes. */
    memset(entry.alt, 'b', sizeof(entry.alt));
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(strlen(got.alt) == 127);

    pt_widget_clear_text_images(label);
    pt_icon_image_destroy(icon);
    pt_widget_destroy(label);
}

static void test_validation(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "abcdef");
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    pt_text_image entry = descriptor(0, icon);

    /* No placeholder anywhere. */
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);

    raw_set_text(label, "x" placeholder "y");
    /* Overlapping / interior / past-the-end starts are all rejected. */
    entry = descriptor(0, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);
    entry = descriptor(2, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);
    entry = descriptor(3, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);
    entry = descriptor(4, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);
    entry = descriptor(100, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* A valid start with a NULL handle is dropped. */
    entry = descriptor(1, NULL);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* Duplicate starts keep the last input entry. */
    pt_text_image pair[2];
    pair[0] = descriptor(1, icon);
    pair[0].width = 10.0f;
    memcpy(pair[0].alt, "first", 6);
    pair[1] = descriptor(1, icon);
    pair[1].width = 20.0f;
    memcpy(pair[1].alt, "second", 7);
    pt_check(pt_widget_set_text_images(label, pair, 2));
    pt_check(pt_widget_get_text_image_count(label) == 1);
    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check_near(got.width, 20.0f, 0.0001);
    pt_check(strcmp(got.alt, "second") == 0);

    /* Unsorted input is stored sorted by start. */
    pt_icon_image *second = make_icon();
    pt_check(second != NULL);
    raw_set_text(label, placeholder "ab" placeholder);
    pt_text_image unsorted[2];
    unsorted[0] = descriptor(5, icon);
    unsorted[1] = descriptor(0, second);
    pt_check(pt_widget_set_text_images(label, unsorted, 2));
    pt_check(pt_widget_get_text_image_count(label) == 2);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 0 && got.image == second);
    pt_check(pt_widget_get_text_image(label, 1, &got));
    pt_check(got.start == 5 && got.image == icon);

    /* One invalid plus one valid entry keeps only the valid one. */
    pt_text_image mixed[2];
    mixed[0] = descriptor(0, icon);
    mixed[1] = descriptor(4, second);
    pt_check(pt_widget_set_text_images(label, mixed, 2));
    pt_check(pt_widget_get_text_image_count(label) == 1);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 0 && got.image == icon);

    /* Non-label/text-area kinds reject the setter without touching state. */
    pt_widget *button = pt_button_create(root, "button");
    pt_check(!pt_widget_set_text_images(button, mixed, 2));
    pt_check(pt_widget_get_text_image_count(button) == 0);
    pt_widget_clear_text_images(button);
    pt_widget_destroy(button);

    pt_widget_clear_text_images(label);
    pt_icon_image_destroy(icon);
    pt_icon_image_destroy(second);
    pt_widget_destroy(label);
}

static void test_reference_counting(pt_widget *root) {
    size_t baseline = pt_icon_cache_entry_count();

    /* clear() drops the widget's last reference. */
    pt_widget *label = pt_label_create(root, placeholder);
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    int stride = 0;
    pt_check(pt_icon_image_raster(icon, 4, 4, &stride) != NULL);
    pt_check(stride == 4);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_text_image entry = descriptor(0, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_icon_image_destroy(icon);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_check(pt_widget_get_text_image_count(label) == 1);
    pt_widget_clear_text_images(label);
    pt_check(pt_icon_cache_entry_count() == baseline);

    /* Replacement releases the old image and retains the new one. */
    pt_icon_image *first = make_icon();
    pt_icon_image *second = make_icon();
    pt_check(first != NULL && second != NULL);
    if (!first || !second) {
        return;
    }
    pt_check(pt_icon_image_raster(first, 5, 5, &stride) != NULL);
    entry = descriptor(0, first);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_icon_image_destroy(first);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_check(pt_icon_image_raster(second, 6, 6, &stride) != NULL);
    entry = descriptor(0, second);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_icon_image_destroy(second);
    pt_widget_clear_text_images(label);
    pt_check(pt_icon_cache_entry_count() == baseline);

    /* A widget destroyed with a live descriptor releases it. */
    pt_icon_image *third = make_icon();
    pt_check(third != NULL);
    if (!third) {
        return;
    }
    pt_check(pt_icon_image_raster(third, 7, 7, &stride) != NULL);
    pt_widget *temporary = pt_label_create(root, placeholder);
    entry = descriptor(0, third);
    pt_check(pt_widget_set_text_images(temporary, &entry, 1));
    pt_icon_image_destroy(third);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_widget_destroy(temporary);
    pt_check(pt_icon_cache_entry_count() == baseline);

    /* The same handle used twice owns two references. */
    pt_icon_image *shared = make_icon();
    pt_check(shared != NULL);
    if (!shared) {
        return;
    }
    pt_check(pt_icon_image_raster(shared, 8, 8, &stride) != NULL);
    raw_set_text(label, placeholder "ab" placeholder);
    pt_text_image two[2];
    two[0] = descriptor(0, shared);
    two[1] = descriptor(5, shared);
    pt_check(pt_widget_set_text_images(label, two, 2));
    pt_icon_image_destroy(shared);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_widget_clear_text_images(label);
    pt_check(pt_icon_cache_entry_count() == baseline);

    /* set_text() invalidates and releases the table. */
    pt_icon_image *fourth = make_icon();
    pt_check(fourth != NULL);
    if (!fourth) {
        return;
    }
    pt_check(pt_icon_image_raster(fourth, 9, 9, &stride) != NULL);
    raw_set_text(label, placeholder);
    entry = descriptor(0, fourth);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_icon_image_destroy(fourth);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_widget_set_text(label, "plain");
    pt_check(pt_widget_get_text_image_count(label) == 0);
    pt_check(pt_icon_cache_entry_count() == baseline);

    pt_widget_destroy(label);
}

static void test_edit_hooks(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "ab" placeholder "cdXXXXXXXX");
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    pt_text_image entry = descriptor(2, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));

    /* Insert before the start shifts it. */
    pt_widget_images_on_insert(label, 0, 2);
    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 4);

    /* Insert exactly at the start shifts it too. */
    pt_widget_images_on_insert(label, 4, 1);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 5);

    /* Insert after the placeholder leaves it alone. */
    pt_widget_images_on_insert(label, 8, 3);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 5);

    /* Inserting inside the three bytes breaks the object: it is released. */
    pt_widget_images_on_insert(label, 6, 1);
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* Delete before the start shifts it back. */
    raw_set_text(label, "abcdef" placeholder "gh");
    entry = descriptor(6, icon);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_widget_images_on_delete(label, 0, 2);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 4);

    /* Delete ending exactly at the start shifts by the removed length. */
    pt_widget_images_on_delete(label, 1, 4);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 1);

    /* Delete starting exactly at the image end leaves it alone. */
    pt_widget_images_on_delete(label, 4, 5);
    pt_check(pt_widget_get_text_image(label, 0, &got));
    pt_check(got.start == 1);

    /* Any overlap with the atomic placeholder removes it. */
    pt_widget_images_on_delete(label, 2, 3);
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* A fully covering deletion removes it. */
    pt_icon_image *second = make_icon();
    pt_check(second != NULL);
    raw_set_text(label, "ab" placeholder "cd");
    entry = descriptor(2, second);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_widget_images_on_delete(label, 2, 5);
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* clamp keeps a descriptor only over a complete U+FFFC. */
    raw_set_text(label, "Xab" placeholder "cd");
    entry = descriptor(3, second);
    pt_check(pt_widget_set_text_images(label, &entry, 1));
    pt_widget_images_clamp(label);
    pt_check(pt_widget_get_text_image_count(label) == 1);
    raw_set_text(label, "abcdefgh");
    pt_widget_images_clamp(label);
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* Empty hooks and a NULL image table are no-ops. */
    pt_widget_images_on_insert(label, 0, 1);
    pt_widget_images_on_delete(label, 0, 1);
    pt_widget_images_clamp(label);
    pt_widget_images_on_insert(NULL, 0, 1);
    pt_widget_images_on_delete(NULL, 0, 1);
    pt_widget_images_clamp(NULL);

    pt_icon_image_destroy(icon);
    pt_icon_image_destroy(second);
    pt_widget_destroy(label);
}

static void test_zero_regression(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "plain text");
    pt_check(pt_widget_get_text_image_count(label) == 0);
    pt_text_image got = {0};
    pt_check(!pt_widget_get_text_image(label, 0, &got));
    pt_widget_set_text(label, "still plain");
    pt_check(pt_widget_get_text_image_count(label) == 0);

    /* Text with a raw placeholder but no descriptor keeps the raw bytes. */
    pt_widget_set_text(label, "a" placeholder "b");
    pt_check(pt_widget_get_text_image_count(label) == 0);
    pt_check(strstr(pt_widget_get_text(label), placeholder) != NULL);
    pt_widget_destroy(label);
}

int main(void) {
    setenv("PT_HEADLESS", "1", 1);
    pt_app *app = pt_app_create("pt.tests.text_image");
    pt_check(app != NULL);
    if (!app) {
        return pt_test_finish("text_image");
    }
    pt_window *window = pt_window_create(app, "text image", 320, 200);
    pt_check(window != NULL);
    if (!window) {
        pt_app_destroy(app);
        return pt_test_finish("text_image");
    }
    pt_widget *root = pt_window_root(window);

    test_set_get_roundtrip(root);
    test_alt_truncation(root);
    test_validation(root);
    test_reference_counting(root);
    test_edit_hooks(root);
    test_zero_regression(root);

    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("text_image");
}