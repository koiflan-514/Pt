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

typedef struct insert_event_log {
    int text_changed;
    int structure_changed;
    pt_widget *last_widget;
} insert_event_log;

static insert_event_log insert_events;

static void on_insert_a11y_event(pt_window *window, pt_widget *widget,
                                 pt_a11y_event_type event, void *data) {
    (void)window;
    insert_event_log *log = data;
    log->last_widget = widget;
    if (event == pt_a11y_event_text_changed) {
        ++log->text_changed;
    } else if (event == pt_a11y_event_structure_changed) {
        ++log->structure_changed;
    }
}

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
    pt_icon_image_destroy(second);

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

static void test_null_discard_order(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "x" placeholder "y");
    pt_icon_image *a = make_icon();
    pt_icon_image *b = make_icon();
    pt_check(a != NULL && b != NULL);
    if (!a || !b) {
        pt_icon_image_destroy(a);
        pt_icon_image_destroy(b);
        pt_widget_destroy(label);
        return;
    }

    /* NULL entries are discarded first, so they never overwrite a valid
     * descriptor for the same placeholder. */
    pt_text_image pair[2];
    pair[0] = descriptor(1, a);
    pair[1] = descriptor(1, NULL);
    pt_check(pt_widget_set_text_images(label, pair, 2));
    pt_check(pt_widget_get_text_image_count(label) == 1);
    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(label, 0, &got) && got.image == a);

    pair[0] = descriptor(1, NULL);
    pair[1] = descriptor(1, b);
    pt_check(pt_widget_set_text_images(label, pair, 2));
    pt_check(pt_widget_get_text_image_count(label) == 1);
    pt_check(pt_widget_get_text_image(label, 0, &got) && got.image == b);

    /* Among valid entries the last one still wins. */
    pt_text_image triple[3];
    triple[0] = descriptor(1, a);
    triple[1] = descriptor(1, NULL);
    triple[2] = descriptor(1, b);
    pt_check(pt_widget_set_text_images(label, triple, 3));
    pt_check(pt_widget_get_text_image_count(label) == 1);
    pt_check(pt_widget_get_text_image(label, 0, &got) && got.image == b);

    /* An all-NULL table leaves the side table empty. */
    pt_text_image nulls[2];
    nulls[0] = descriptor(1, NULL);
    nulls[1] = descriptor(1, NULL);
    pt_check(pt_widget_set_text_images(label, nulls, 2));
    pt_check(pt_widget_get_text_image_count(label) == 0);

    pt_widget_clear_text_images(label);
    pt_icon_image_destroy(a);
    pt_icon_image_destroy(b);
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

static void test_insert_roundtrip(pt_widget *root) {
    size_t baseline = pt_icon_cache_entry_count();
    pt_widget *area = pt_text_area_create(root, "");
    pt_icon_image *icon = make_icon();
    pt_check(area != NULL && icon != NULL);
    if (!area || !icon) {
        pt_icon_image_destroy(icon);
        pt_widget_destroy(area);
        return;
    }
    int stride = 0;
    pt_check(pt_icon_image_raster(icon, 4, 4, &stride) != NULL);
    pt_check(stride == 4);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);

    pt_text_image entry = descriptor(999, icon);
    entry.width = 12.5f;
    entry.height = 6.0f;
    entry.align = pt_text_image_align_middle;
    memcpy(entry.alt, "chart", 6);

    int text_before = insert_events.text_changed;
    int structure_before = insert_events.structure_changed;
    uint32_t start = 123;
    pt_check(pt_text_area_insert_image(area, &entry, &start));
    pt_check(start == 0);
    pt_check(strcmp(pt_widget_get_text(area), placeholder) == 0);
    pt_check(pt_widget_get_text_image_count(area) == 1);
    pt_text_image got = {0};
    pt_check(pt_widget_get_text_image(area, 0, &got));
    pt_check(got.start == 0);
    pt_check(got.image == icon);
    pt_check_near(got.width, 12.5f, 0.0001);
    pt_check_near(got.height, 6.0f, 0.0001);
    pt_check(got.align == pt_text_image_align_middle);
    pt_check(strcmp(got.alt, "chart") == 0);

    /* The caret sits after the three placeholder bytes. */
    pt_check(area->cursor == 3);
    pt_check(area->selection_anchor == 3);

    /* One text-changed event from the normal insert path and one structure
     * event from the side table update. */
    pt_check(insert_events.text_changed == text_before + 1);
    pt_check(insert_events.structure_changed == structure_before + 1);
    pt_check(insert_events.last_widget == area);

    /* The widget owns its own reference and start may be NULL. */
    pt_icon_image_destroy(icon);
    pt_check(pt_icon_cache_entry_count() == baseline + 1);
    pt_check(pt_widget_get_text_image(area, 0, &got));
    pt_check(got.image == icon);
    pt_check(pt_icon_image_width(got.image) == 4);
    pt_check(pt_text_area_insert_image(area, &got, NULL));
    pt_check(pt_widget_get_text_image_count(area) == 2);
    pt_check(strcmp(pt_widget_get_text(area), placeholder placeholder) == 0);

    pt_widget_destroy(area);
    pt_check(pt_icon_cache_entry_count() == baseline);
}

static void test_insert_shift(pt_widget *root) {
    pt_widget *area = pt_text_area_create(root, "");
    pt_icon_image *icon = make_icon();
    pt_check(area != NULL && icon != NULL);
    if (!area || !icon) {
        pt_icon_image_destroy(icon);
        pt_widget_destroy(area);
        return;
    }
    pt_widget_set_text(area, "ab");

    /* Each insert lands at the caret; inserting before an existing image
     * shifts it, inserting after it leaves it alone. */
    pt_text_image item = descriptor(0, icon);
    pt_text_area_set_selection(area, 2, 2);
    pt_check(pt_text_area_insert_image(area, &item, NULL));
    pt_text_area_set_selection(area, 2, 2);
    pt_check(pt_text_area_insert_image(area, &item, NULL));
    pt_text_area_set_selection(area, 5, 5);
    pt_check(pt_text_area_insert_image(area, &item, NULL));
    pt_text_area_set_selection(area, 8, 8);
    pt_check(pt_text_area_insert_image(area, &item, NULL));
    pt_text_area_set_selection(area, 14, 14);
    pt_check(pt_text_area_insert_image(area, &item, NULL));

    pt_check(strcmp(pt_widget_get_text(area),
                    "ab" placeholder placeholder placeholder placeholder placeholder) == 0);
    pt_check(pt_widget_get_text_image_count(area) == 5);
    pt_text_image got = {0};
    uint32_t expected[5] = {2, 5, 8, 11, 14};
    for (uint32_t i = 0; i < 5; ++i) {
        pt_check(pt_widget_get_text_image(area, i, &got));
        pt_check(got.start == expected[i]);
        pt_check(got.image == icon);
    }

    /* Replacing a fully covered placeholder removes the old descriptor and
     * assigns the inserted one. */
    pt_widget_set_text(area, "ab" placeholder "cd");
    pt_icon_image *old = make_icon();
    pt_check(old != NULL);
    pt_text_image old_entry = descriptor(2, old);
    pt_check(pt_widget_set_text_images(area, &old_entry, 1));
    pt_icon_image_destroy(old);

    pt_icon_image *fresh = make_icon();
    pt_check(fresh != NULL);
    pt_text_image fresh_entry = descriptor(3, fresh);
    uint32_t start = 99;
    pt_text_area_set_selection(area, 0, 5);
    pt_check(pt_text_area_insert_image(area, &fresh_entry, &start));
    pt_check(start == 0);
    pt_check(strcmp(pt_widget_get_text(area), placeholder "cd") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 1);
    pt_check(pt_widget_get_text_image(area, 0, &got));
    pt_check(got.start == 0);
    pt_check(got.image == fresh);

    pt_icon_image_destroy(fresh);
    pt_widget_destroy(area);
    pt_icon_image_destroy(icon);
}

static void test_insert_rejections(pt_widget *root) {
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    pt_text_image entry = descriptor(0, icon);
    uint32_t start = 1;

    pt_check(!pt_text_area_insert_image(NULL, &entry, &start));
    pt_check(start == 1);

    pt_widget *label = pt_label_create(root, "label");
    pt_check(!pt_text_area_insert_image(label, &entry, &start));
    pt_check(pt_widget_get_text_image_count(label) == 0);
    pt_widget_destroy(label);

    pt_widget *single = pt_entry_create(root, "");
    pt_check(!pt_text_area_insert_image(single, &entry, &start));
    pt_widget_destroy(single);

    pt_widget *area = pt_text_area_create(root, "");
    pt_check(!pt_text_area_insert_image(area, NULL, &start));
    entry.image = NULL;
    pt_check(!pt_text_area_insert_image(area, &entry, &start));
    entry.image = icon;
    pt_check(strcmp(pt_widget_get_text(area), "") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 0);

    /* An insertion point inside the placeholder bytes is not a UTF-8
     * boundary; splitting the object is rejected before any mutation. */
    pt_widget_set_text(area, "ab" placeholder "cd");
    pt_check(pt_widget_set_text_images(area, &(pt_text_image){.start = 2, .image = icon}, 1));
    pt_check(pt_widget_get_text_image_count(area) == 1);
    pt_text_area_set_selection(area, 3, 3);
    pt_check(!pt_text_area_insert_image(area, &entry, &start));
    pt_text_area_set_selection(area, 4, 4);
    pt_check(!pt_text_area_insert_image(area, &entry, &start));
    pt_check(strcmp(pt_widget_get_text(area), "ab" placeholder "cd") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 1);

    /* W3c makes a selection atomic: an endpoint that lands inside the
     * placeholder snaps outward, so the whole object is selected and the
     * insert helper replaces it instead of splitting its bytes. */
    pt_text_area_set_selection(area, 0, 3);
    uint32_t snap_start = 1;
    uint32_t snap_end = 1;
    pt_check(pt_text_area_get_selection(area, &snap_start, &snap_end));
    pt_check(snap_start == 0 && snap_end == 5);
    pt_check(pt_text_area_insert_image(area, &entry, &start));
    pt_check(start == 0);
    pt_check(strcmp(pt_widget_get_text(area), placeholder "cd") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 1);
    pt_text_area_set_selection(area, 5, 5);

    /* An interior byte of any other multi-byte codepoint is rejected as well. */
    pt_widget_set_text(area, "\xC3\xA9");
    area->cursor = 1;
    area->selection_anchor = 1;
    pt_check(!pt_text_area_insert_image(area, &entry, &start));
    pt_check(strcmp(pt_widget_get_text(area), "\xC3\xA9") == 0);
    pt_check(pt_widget_get_text_image_count(area) == 0);

    /* The text byte cap matches the normal insert path. */
    pt_widget *bounded = pt_text_area_create(root, "");
    char *huge = malloc(pt_entry_max_bytes + 1u);
    pt_check(huge != NULL);
    if (huge) {
        memset(huge, 'a', pt_entry_max_bytes - 3u);
        huge[pt_entry_max_bytes - 3u] = '\0';
        pt_widget_set_text(bounded, huge);
        pt_check(strlen(pt_widget_get_text(bounded)) == pt_entry_max_bytes - 3u);
        pt_check(!pt_text_area_insert_image(bounded, &entry, &start));
        huge[pt_entry_max_bytes - 4u] = '\0';
        pt_widget_set_text(bounded, huge);
        pt_check(pt_text_area_insert_image(bounded, &entry, &start));
        pt_check(start == pt_entry_max_bytes - 4u);
        free(huge);
    }
    pt_widget_destroy(bounded);
    pt_widget_destroy(area);
    pt_icon_image_destroy(icon);
}

static void test_insert_undo_text(pt_widget *root) {
    pt_widget *area = pt_text_area_create(root, "");
    pt_icon_image *icon = make_icon();
    pt_check(area != NULL && icon != NULL);
    if (!area || !icon) {
        pt_icon_image_destroy(icon);
        pt_widget_destroy(area);
        return;
    }
    pt_widget_set_text(area, "ab");
    pt_text_area_set_selection(area, 2, 2);
    pt_text_image undo_entry = descriptor(0, icon);
    pt_check(pt_text_area_insert_image(area, &undo_entry, NULL));
    pt_check(strcmp(pt_widget_get_text(area), "ab" placeholder) == 0);

    /* The insert goes through the normal text path, so undo restores the text.
     * The image snapshot follows the same history in the editor wave. */
    pt_check(pt_text_area_undo(area));
    pt_check(strcmp(pt_widget_get_text(area), "ab") == 0);

    pt_icon_image_destroy(icon);
    pt_widget_destroy(area);
}

/* The descriptor enters the run/measure/wrap/map pipeline as a fontless atomic
 * run: no shaping, logical measure, device map extent, whole-placeholder
 * clusters. W3c wires painting but the geometry is complete here. */
static void test_pipeline_runs(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "x" placeholder "y");
    pt_icon_image *icon = make_icon();
    pt_check(icon != NULL);
    if (!icon) {
        return;
    }
    pt_text_image entry = descriptor(1, icon);
    entry.width = 16.0f;
    entry.height = 24.0f;
    entry.align = pt_text_image_align_middle;
    pt_check(pt_widget_set_text_images(label, &entry, 1));

    pt_layout_ctx ctx = {.scale = 1.0f, .font_family = "sans-serif"};
    pt_text_runs runs = {0};
    pt_check(pt_text_runs_build(label, &ctx, &runs));
    pt_check(runs.count == 3);
    pt_check(runs.items[1].start == 1 && runs.items[1].end == 4);
    pt_check(runs.items[1].font == NULL);
    pt_check(runs.items[1].image != NULL);
    pt_check_near(pt_text_runs_measure(&runs, 1, 4), 16.0f, 0.001f);
    pt_check(runs.ascent >= 12.0f);
    pt_check(runs.descent >= 12.0f);
    pt_check(runs.line_height >= 24.0f);

    pt_text_visual_map *map = NULL;
    pt_check(pt_text_visual_map_build_runs(&runs, 0, 5, pt_bidi_base_ltr, &map));
    pt_check(map != NULL);
    if (map) {
        bool saw_image = false;
        for (size_t i = 0; i < map->count; ++i) {
            if (map->clusters[i].start == 1) {
                saw_image = true;
                pt_check(map->clusters[i].end == 4);
                pt_check_near(map->clusters[i].x1 - map->clusters[i].x0, 16.0f, 0.01f);
            }
        }
        pt_check(saw_image);
        pt_check(!pt_text_visual_map_is_boundary(map, 2));
        pt_check(!pt_text_visual_map_is_boundary(map, 3));
        pt_text_visual_map_free(map);
    }

    pt_text_line *lines = NULL;
    int count = pt_text_runs_wrap(&runs, 12.0f, &lines);
    pt_check(count >= 2);
    for (int i = 0; i < count; ++i) {
        pt_check(!(lines[i].start > 1 && lines[i].start < 4));
        pt_check(!(lines[i].end > 1 && lines[i].end < 4));
    }
    free(lines);
    pt_text_runs_free(&runs);

    /* The text-area layout selects the run path on image_count alone even with
     * no spans, and its composed metrics include the image box. */
    pt_widget *area = pt_text_area_create(root, NULL);
    pt_widget_set_text(area, "x" placeholder "y");
    pt_check(pt_widget_set_text_images(area, &entry, 1));
    pt_text_layout layout = {0};
    pt_check(pt_text_layout_build(area, &ctx, 200.0f, &layout));
    pt_check(layout.has_runs);
    pt_check(layout.runs.count == 3);
    pt_check(layout.line_height >= 24.0f);
    pt_text_layout_free(&layout);
    pt_widget_destroy(area);

    pt_widget_destroy(label);
    pt_icon_image_destroy(icon);
}

/* W3c clipboard text: a contained descriptor becomes its alt text, an empty
 * alt keeps U+FFFC, a raw placeholder without a descriptor is untouched and a
 * defensive partial range still emits one whole U+FFFC. */
static void test_copy_text(pt_widget *root) {
    pt_widget *label = pt_label_create(root, "a" placeholder "b" placeholder "c");
    pt_icon_image *icon = make_icon();
    pt_check(label != NULL && icon != NULL);
    if (!label || !icon) {
        pt_icon_image_destroy(icon);
        pt_widget_destroy(label);
        return;
    }
    pt_text_image with_alt = descriptor(1, icon);
    memcpy(with_alt.alt, "ALT", 4);
    pt_check(pt_widget_set_text_images(label, &with_alt, 1));
    const char *text = pt_widget_get_text(label);
    size_t length = strlen(text);

    size_t copied_length = 0;
    char *copied = pt_widget_images_copy_text(label, 0, length, &copied_length);
    pt_check(copied != NULL);
    if (copied) {
        pt_check(strcmp(copied, "aALTb" placeholder "c") == 0);
        pt_check(copied_length == strlen(copied));
        free(copied);
    }

    /* A partial range at the leading edge of the placeholder becomes U+FFFC. */
    copied = pt_widget_images_copy_text(label, 0, 2, &copied_length);
    pt_check(copied != NULL);
    if (copied) {
        pt_check(strcmp(copied, "a" placeholder) == 0);
        pt_check(copied_length == 4);
        free(copied);
    }
    /* A range that begins inside the placeholder also emits it whole. */
    copied = pt_widget_images_copy_text(label, 3, length, &copied_length);
    pt_check(copied != NULL);
    if (copied) {
        pt_check(strcmp(copied, placeholder "b" placeholder "c") == 0);
        free(copied);
    }

    /* Empty alt keeps the raw object replacement character. */
    with_alt.alt[0] = '\0';
    pt_check(pt_widget_set_text_images(label, &with_alt, 1));
    copied = pt_widget_images_copy_text(label, 0, length, &copied_length);
    pt_check(copied != NULL);
    if (copied) {
        pt_check(strcmp(copied, text) == 0);
        free(copied);
    }

    /* With no descriptor at all the raw bytes are copied verbatim, and an
     * empty range has no clipboard text. */
    pt_widget_clear_text_images(label);
    copied = pt_widget_images_copy_text(label, 0, length, &copied_length);
    pt_check(copied != NULL);
    if (copied) {
        pt_check(strcmp(copied, text) == 0);
        free(copied);
    }
    pt_check(pt_widget_images_copy_text(label, 2, 2, NULL) == NULL);
    pt_check(pt_widget_images_copy_text(NULL, 0, 1, NULL) == NULL);

    pt_icon_image_destroy(icon);
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
    memset(&insert_events, 0, sizeof(insert_events));
    pt_app_set_a11y_callback(app, on_insert_a11y_event, &insert_events);
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
    test_null_discard_order(root);
    test_reference_counting(root);
    test_edit_hooks(root);
    test_insert_roundtrip(root);
    test_insert_shift(root);
    test_insert_rejections(root);
    test_insert_undo_text(root);
    test_pipeline_runs(root);
    test_copy_text(root);
    test_zero_regression(root);

    pt_window_free(window);
    pt_app_destroy(app);
    return pt_test_finish("text_image");
}