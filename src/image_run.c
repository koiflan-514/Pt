/*
 * Inline image runs: the U+FFFC side table, validation, reference counting and
 * the edit shift/clamp helpers that mirror the styled-span rules.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define pt_image_placeholder_bytes 3u
#define pt_image_alt_capacity 128u

/* ---------- helpers ---------- */

static size_t image_text_length(const pt_widget *widget) {
    return widget->text ? strlen(widget->text) : 0;
}

static bool image_placeholder_at(const char *text, size_t length, size_t start) {
    if (!text || start > length ||
        length - start < pt_image_placeholder_bytes) {
        return false;
    }
    const unsigned char *bytes = (const unsigned char *)text + start;
    return bytes[0] == 0xEFu && bytes[1] == 0xBFu && bytes[2] == 0xBCu;
}

static void image_release_array(pt_text_image *images, uint32_t count) {
    if (!images) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        pt_icon_image_destroy(images[i].image);
    }
}

/* Copies alt into the fixed descriptor buffer, truncating on a UTF-8 boundary
 * at no more than 127 bytes so the field always stays NUL-terminated. */
static void image_copy_alt(char out[pt_image_alt_capacity], const char *alt) {
    memset(out, 0, pt_image_alt_capacity);
    if (!alt) {
        return;
    }
    const char *nul = memchr(alt, '\0', pt_image_alt_capacity);
    size_t length = nul ? (size_t)(nul - alt) : pt_image_alt_capacity;
    if (length >= pt_image_alt_capacity) {
        length = pt_image_alt_capacity - 1u;
    }
    memcpy(out, alt, length);
    size_t cut = length;
    while (cut > 0 && ((unsigned char)out[cut - 1u] & 0xC0u) == 0x80u) {
        --cut;
    }
    if (cut > 0) {
        unsigned char lead = (unsigned char)out[cut - 1u];
        size_t sequence = 1;
        if ((lead & 0xE0u) == 0xC0u) {
            sequence = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            sequence = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            sequence = 4;
        }
        if (cut - 1u + sequence > length) {
            length = cut - 1u;
        }
    }
    out[length] = '\0';
}

/* ---------- storage ---------- */

void pt_widget_images_clear(pt_widget *widget) {
    if (!widget) {
        return;
    }
    image_release_array(widget->images, widget->image_count);
    free(widget->images);
    widget->images = NULL;
    widget->image_count = 0;
    widget->image_capacity = 0;
}

bool pt_widget_set_text_images(pt_widget *widget, const pt_text_image *images,
                               uint32_t count) {
    if (!pt_widget_supports_spans(widget)) {
        return false;
    }
    if (!images || count == 0) {
        pt_widget_images_clear(widget);
        pt_widget_mark_dirty(widget, true);
        pt_widget_a11y_emit(widget, pt_a11y_event_structure_changed);
        return true;
    }
    const char *text = widget->text ? widget->text : "";
    size_t length = strlen(text);
    pt_text_image *copy = pt_alloc_zeroed(count, sizeof(*copy));
    if (!copy) {
        return false;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        /* A NULL image means "drop this entry", so it is discarded before the
         * duplicate resolution below and never clears a valid descriptor for
         * the same placeholder. */
        if (!images[i].image) {
            continue;
        }
        size_t start = images[i].start;
        if (!image_placeholder_at(text, length, start)) {
            continue;
        }
        pt_text_image entry = images[i];
        entry.start = (uint32_t)start;
        image_copy_alt(entry.alt, images[i].alt);
        uint32_t at = 0;
        while (at < kept && copy[at].start < entry.start) {
            ++at;
        }
        if (at < kept && copy[at].start == entry.start) {
            /* One placeholder maps to at most one image; the last input entry
             * wins, matching the span setter. */
            copy[at] = entry;
        } else {
            memmove(&copy[at + 1u], &copy[at],
                    (size_t)(kept - at) * sizeof(*copy));
            copy[at] = entry;
            ++kept;
        }
    }
    for (uint32_t i = 0; i < kept; ++i) {
        pt_icon_image_ref(copy[i].image);
    }
    image_release_array(widget->images, widget->image_count);
    free(widget->images);
    widget->images = kept ? copy : NULL;
    widget->image_count = kept;
    widget->image_capacity = kept;
    if (!kept) {
        free(copy);
    }
    pt_widget_mark_dirty(widget, true);
    pt_widget_a11y_emit(widget, pt_a11y_event_structure_changed);
    return true;
}

void pt_widget_clear_text_images(pt_widget *widget) {
    if (!widget || !pt_widget_supports_spans(widget)) {
        return;
    }
    bool had = widget->image_count > 0;
    pt_widget_images_clear(widget);
    if (had) {
        pt_widget_mark_dirty(widget, true);
        pt_widget_a11y_emit(widget, pt_a11y_event_structure_changed);
    }
}

uint32_t pt_widget_get_text_image_count(const pt_widget *widget) {
    return widget ? widget->image_count : 0;
}

bool pt_widget_get_text_image(const pt_widget *widget, uint32_t index,
                              pt_text_image *image) {
    if (!widget || !image || index >= widget->image_count) {
        return false;
    }
    *image = widget->images[index];
    return true;
}

/* Inserts one descriptor at start, keeping the table sorted. The text has
 * already been changed and the existing descriptors have been shifted by the
 * caller, so this never sees a duplicate start; a duplicate is still replaced
 * defensively. Takes a reference to the handle. */
void pt_widget_images_attach(pt_widget *widget, size_t start, const pt_text_image *image) {
    if (!widget || !image || !image->image) {
        return;
    }
    pt_text_image entry = *image;
    entry.start = (uint32_t)start;
    image_copy_alt(entry.alt, image->alt);
    uint32_t at = 0;
    while (at < widget->image_count && widget->images[at].start < entry.start) {
        ++at;
    }
    if (at < widget->image_count && widget->images[at].start == entry.start) {
        pt_icon_image_destroy(widget->images[at].image);
        pt_icon_image_ref(entry.image);
        widget->images[at] = entry;
        return;
    }
    if (widget->image_count == widget->image_capacity) {
        uint32_t capacity = widget->image_capacity ? widget->image_capacity * 2u : 4u;
        widget->images = pt_realloc_mem(widget->images,
                                        (size_t)capacity * sizeof(*widget->images));
        widget->image_capacity = capacity;
    }
    memmove(&widget->images[at + 1u], &widget->images[at],
            (size_t)(widget->image_count - at) * sizeof(*widget->images));
    pt_icon_image_ref(entry.image);
    widget->images[at] = entry;
    ++widget->image_count;
}

/* ---------- edit hooks ---------- */

void pt_widget_images_on_insert(pt_widget *widget, size_t position, size_t length) {
    if (!widget || widget->image_count == 0 || length == 0) {
        return;
    }
    size_t text_length = image_text_length(widget);
    if (position > text_length) {
        position = text_length;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < widget->image_count; ++i) {
        pt_text_image entry = widget->images[i];
        size_t start = entry.start;
        size_t end = start + pt_image_placeholder_bytes;
        if (position <= start) {
            entry.start = (uint32_t)(start + length);
            widget->images[kept++] = entry;
        } else if (position < end) {
            /* An insertion inside the three-byte placeholder breaks the
             * sequence; the descriptor can no longer be valid. */
            pt_icon_image_destroy(entry.image);
        } else {
            widget->images[kept++] = entry;
        }
    }
    widget->image_count = kept;
}

void pt_widget_images_on_delete(pt_widget *widget, size_t start, size_t end) {
    if (!widget || widget->image_count == 0 || start >= end) {
        return;
    }
    size_t removed = end - start;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < widget->image_count; ++i) {
        pt_text_image entry = widget->images[i];
        size_t image_start = entry.start;
        size_t image_end = image_start + pt_image_placeholder_bytes;
        if (image_end <= start) {
            widget->images[kept++] = entry;
        } else if (image_start >= end) {
            entry.start = (uint32_t)(image_start - removed);
            widget->images[kept++] = entry;
        } else {
            /* Deletion overlaps any part of the placeholder; the object is
             * atomic and cannot be shrunk, so it goes away with its bytes. */
            pt_icon_image_destroy(entry.image);
        }
    }
    widget->image_count = kept;
}

void pt_widget_images_clamp(pt_widget *widget) {
    if (!widget || widget->image_count == 0) {
        return;
    }
    const char *text = widget->text ? widget->text : "";
    size_t length = strlen(text);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < widget->image_count; ++i) {
        pt_text_image entry = widget->images[i];
        if (!image_placeholder_at(text, length, entry.start)) {
            pt_icon_image_destroy(entry.image);
            continue;
        }
        widget->images[kept++] = entry;
    }
    widget->image_count = kept;
}