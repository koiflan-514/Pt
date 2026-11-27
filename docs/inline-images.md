# Inline images in text

English | [简体中文](zh-CN/inline-images.md)

Status: delivered in 2.3.0. Labels and text areas can place a reference-counted
raster image inside their UTF-8 text without changing the text model, the byte
offsets or the bidirectional pipeline. This guide describes the public contract
frozen in `include/pt/base.h` and `include/pt/widget.h`; [m25-design.md](m25-design.md)
records the design input and [accessibility.md](accessibility.md) the assistive
technology contract.

Related reading: [rtl.md](rtl.md) explains why U+FFFC behaves as one bidi
character, [bindings.md](bindings.md) covers the wrapper types, and
[architecture.md](architecture.md) places the run pipeline in the toolkit.

## Guarantees and contracts

- A **U+FFFC object replacement character** in the widget's UTF-8 text is the
  placeholder. It is encoded as the three bytes `EF BF BC`, exactly as Unicode
  defines it. The caller writes the placeholder, or `pt_text_area_insert_image`
  inserts one at the caret.
- The image itself lives in a **sorted side table** keyed by the byte offset of
  that placeholder. One placeholder maps to at most one descriptor.
- The text is the **single source of truth**. `pt_widget_get_text` returns the
  raw `EF BF BC` bytes, every public offset stays a UTF-8 byte offset, and
  adding an image never shifts an existing offset.
- U+FFFC is one Unicode codepoint and one atomic cluster. Selection, caret
  movement, deletion and UTF-8 stepping never enter the middle of the three
  bytes. U+FFFC has bidi class ON, so the Unicode Bidirectional Algorithm places
  it in the surrounding embedding level; the visual order stays presentation
  only.
- Images are **opt-in at the descriptor level**. Text that contains U+FFFC but
  no descriptor keeps the 2.2.0 behaviour: the placeholder is shaped like any
  other uncovered codepoint. A widget that never sets an image takes the same
  layout, measure and paint path as before.
- The line's ascent and descent grow to contain the image box; in 2.3.0 a tall
  image raises the metrics of every line in the widget, like a large styled font
  run. Per-line metrics remain a future addition.
- The API is additive. `pt_text_span`, the existing ABI and the SONAME
  `libpt.so.1` do not change.

## The model

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

`start` is always resolved against the current text. The setter copies the
descriptor array, validates every entry, sorts by `start`, keeps one entry per
placeholder and takes a reference to each non-NULL image. A normal text edit
mirrors the table: an insertion before a placeholder shifts its `start`, a
deletion that overlaps the placeholder removes the descriptor, and a deletion
before it shifts the offset down. `pt_widget_set_text` clears the whole table,
just as it clears styled spans.

## Accessibility (2.3.0)

The accessibility tree exposes each image as a **virtual child** of the owning
label or text area, ordered by side-table index. The child has role `image`
(AT-SPI role 27) and its name is the `alt` text, falling back to the image
description when `alt` is empty. The child is a real `Component`: `GetExtents`,
`GetPosition` and `GetSize` return the placeholder's screen rectangle, and
`GetAccessibleAtPoint` resolves a point inside that rectangle to the image child
rather than to the owning text widget. The Text interface keeps returning the
raw U+FFFC and unchanged character offsets, so an application or AT that does
not understand embedded objects still observes a placeholder in the right
position. Setting, clearing or editing a placeholder emits a structure-changed
accessibility event; the usual text-changed event still reports the byte change.

## Quick start

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

A NULL image handle is dropped by the setter, so a theme lookup miss leaves the
placeholder undecorated rather than failing the call. `pt_text_image` is
zero-initialisable: `align` zero is baseline, `width` and `height` zero mean the
intrinsic icon size, and a NULL image means "drop this entry".

## Ownership and lifetimes

- `pt_widget_set_text_images` copies the descriptor array and takes its own
  reference to every non-NULL image. Passing NULL or a zero count is equivalent
  to `pt_widget_clear_text_images`.
- A successful set releases the previous table's references. The old
  descriptor array must not be reused to restore the old images, because those
  handles may already have been freed. Keep explicit references if a caller
  needs to rebuild an earlier table.
- `pt_widget_get_text_image` copies one descriptor. The returned `image` handle
  is **borrowed**: it is valid only while the side table is alive and the entry
  has not been replaced or cleared. Call `pt_icon_image_ref` to keep it longer.
- `pt_text_area_insert_image` ignores `image->start`, takes its own reference to
  `image->image`, and records the text change. The caller may destroy its own
  handle immediately after a successful call.
- A text replacement, or any edit that removes a placeholder, releases the
  associated image. Widget destruction releases the remaining table.

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

## Sizing and alignment

`width` and `height` are logical pixels. A value `<= 0`, or a non-finite value,
means the intrinsic icon size. If exactly one dimension is positive, the other
preserves the icon's aspect ratio; if both are positive the image is drawn at
that box. The raster request is bounded by the existing icon cache
(`PT_ICON_CACHE_MAX_PIXELS`): a request that cannot fit draws nothing, exactly
like an oversized button icon.

| Value | Geometry | Ascent contribution | Descent contribution |
| --- | --- | --- | --- |
| `pt_text_image_align_baseline` (0) | bottom edge on the text baseline | `height` | `0` |
| `pt_text_image_align_middle` | vertical centre on the text baseline | `height / 2` | `height / 2` |

Baseline alignment is the default and suits icons that sit on the text line,
such as a status glyph. Middle alignment centres the image on the baseline and
extends the line above and below, which suits a badge or an avatar. The painter
uses `bottom = baseline + descent` and clips to the widget, so an image wider
than the content box stays alone on its line and one that exceeds the line box
is clipped rather than rescaled.

## Alt text

`alt` is copied into the descriptor at set time. The field holds at most 127
bytes plus the terminating NUL; longer input is truncated on a UTF-8 sequence
boundary so the stored string is always valid UTF-8 and NUL-terminated. The alt
text is what a copied selection and the accessibility tree expose in place of
U+FFFC. An empty alt keeps U+FFFC in a copied selection and lets the accessible
name fall back to the image description.

## Inserting at the caret

`pt_text_area_insert_image` is the editing entry point. It inserts one U+FFFC at
the caret, replaces the current selection and attaches a descriptor to the new
placeholder.

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

The call returns false without changing the text or the table when the widget is
NULL or not a text area, the image or its handle is NULL, either selection
endpoint is not a UTF-8 boundary (this includes the interior of an existing
placeholder, so an object is never split), or the insertion would exceed the
text byte limit. A selection that fully covers an existing placeholder removes
that descriptor together with its bytes; the descriptor for the new placeholder
takes its place. On success the byte offset of the new placeholder is written to
`start` when `start` is not NULL.

## Validation rules

- Each `start` must point at a complete `EF BF BC` sequence in the current text.
  An offset at or past the text length, a partial sequence, or a different
  codepoint is dropped.
- A NULL `image` handle is dropped before duplicate resolution, so it never
  clears a valid descriptor for the same placeholder.
- When several entries name the same placeholder, the last input entry wins,
  matching the styled-span setter.
- The stored table is sorted by `start`; a getter index therefore follows text
  order.
- A descriptor array is copied, so the caller may release the input array after
  the call. The image handles are the only borrowed-by-value fields.
- `pt_widget_set_text` clears the table. An edit that invalidates a placeholder
  clamps the table to the new text and releases the affected references.

## Copy and cut (2.3.0)

Copying a selection writes the alt text in place of each fully contained
U+FFFC. A run without alt text keeps the three placeholder bytes. A partial
selection that starts or ends inside a placeholder is snapped outward before
the copy, so an object is copied whole or not at all. Cut removes the raw
placeholder and its descriptor; the clipboard receives plain text. Deleting or
cutting through the accessibility `EditableText` interface goes through the same
text-delete path, so only the descriptors overlapping the removed bytes are
dropped and the deletion is a single undoable step. Pasting an image, or a rich
clipboard type that recreates descriptors, is not part of 2.3.0.

## Bindings

The C++ wrapper exposes `set_text_images`, `clear_text_images`,
`text_image_count`, `text_image` (returning `std::optional<pt_text_image>`) and
`insert_image` (returning `std::optional<uint32_t>`). Python exposes the same
methods with `TextImage` and `text_image_align_*`; Java exposes
`Pt.TextImage`, `Pt.widget_set_text_images`, `Pt.widget_get_text_image` and
`Pt.text_area_insert_image`, with `-1` as the rejected insertion result. The
wrapper types carry the same ownership rules as the C handles.

## Examples

- [C inline images](../examples/c/inline_images.c)
- [C++ inline images](../examples/cpp/inline_images.cpp)
- [Python inline images](../examples/python/inline_images.py)
- [Java inline images](../examples/java/InlineImages.java)

Each example embeds a small PNG, sets two placeholders with different alignment
and alt text, inserts a third image at the caret and checks the side table. Set
`PT_HEADLESS=1` to run the model checks and a short event-loop slice without a
compositor; the examples never assert rendered pixels.

## Compatibility and limitations

- Delivered in 2.3.0 as an additive API. No existing struct layout, enum value
  or function signature changes; `ABI_MAJOR` stays 1 and the SONAME stays
  `libpt.so.1`.
- A widget with no descriptors keeps the 2.2.0 text path byte for byte. A
  placeholder without a descriptor is shaped as an uncovered codepoint, exactly
  as before.
- 2.3.0 composes one line height for the whole widget, so a tall image raises
  every line. Per-line metrics are not implemented.
- Images are single logical boxes: there is no fractional scaling of a partially
  visible run, no image pasting and no rich clipboard MIME type yet.
- The image handle is a raster icon (`pt_icon_image`); vector runs and animated
  images are not part of this feature.