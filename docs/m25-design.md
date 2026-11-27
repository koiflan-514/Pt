# M25 - 2.3.0 design: inline image runs (U+FFFC)

Status: **delivered** (2.3.0, final gate 2026-11-27, tag-ready). The design was
implemented on the 2.2.0 tree `7c476bc` and closed on `m25/w6-gate`; every wave
was independently reviewed PASS. This is the design and decision record, not the
release checklist — the measurements are in [release.md](release.md).

Baseline at design time: `api/pt.api` read **576 functions / 952 entries**,
`ABI_MAJOR = 1`, SONAME `libpt.so.1`, version 2.2.0. The delivered surface is
**581 functions / 960 entries** (5 functions, 1 enum with 2 values and 1 struct
added), with `ABI_MAJOR` and SONAME unchanged.

## Scope

Deliver the first item of the post-2.0 structural horizon in
[ROADMAP.md](ROADMAP.md): inline image runs inside styled text and the text
area.

- A **U+FFFC object replacement character** in the widget's UTF-8 text is the
  placeholder. The image itself is a side table keyed by the byte offset of that
  character, so text editing, byte offsets, bidi and accessibility character
  offsets keep their existing contract.
- A bounded image descriptor: image handle, logical width/height, vertical
  alignment (baseline or middle), and alt text.
- Measurement and wrapping account for the image box; the line's ascent and
  descent grow to contain it.
- Atomic caret/selection/deletion over the placeholder; selection paints the
  image as one rectangle through the existing visual-map path.
- Copying a selection writes the alt text in place of U+FFFC; a run without alt
  text stays U+FFFC.
- AT-SPI exposes each inline image as a virtual embedded child with the alt text
  as its accessible name.
- Reuse `pt_icon_image` for decoding and the bounded raster cache; no new image
  decoder, no new cache.

Out of scope for 2.3.0:

- Extending `pt_text_span`. Its layout is frozen by the 2.0 ABI decision
  (`docs/rtl.md`, `docs/m24-design.md`); it has no handle field and no room for
  one.
- Images in single-line entries (only labels and text areas). The entry paint
  path is a separate, simpler contract; it can gain images later additively.
- Per-line metrics. A tall image raises the composed metrics for the whole
  widget, exactly as a large styled run already does. Per-line metrics would
  change existing styled-text layout and is a separate structural item.
- Image tint, opacity, links or animation. The 2.3.0 descriptor is deliberately
  small; future attributes need a new struct or function rather than a layout
  change.
- Pasting `image/png` into a text area, or a rich clipboard MIME type. Copying a
  selection stays plain text; `pt_window_clipboard_set_image_png` is unchanged.

## Current code, read for this evaluation

| Area | File / function | Observed behavior |
| --- | --- | --- |
| Public span model | `include/pt/base.h` `pt_text_span` (192-201) | A byte range with weight/slant/color/family/size. Offsets are bytes and are snapped to UTF-8 boundaries; spans may overlap and the last wins. No object/handle field. |
| Span storage | `src/internal.h` `pt_widget` (979-991) | `pt_text_span *spans; uint32_t span_count;` lives directly on the widget. `pt_widget_set_text` (`src/widget.c:2326-2335`) frees them and resets the offsets. |
| Span shift | `src/rich_text.c` `pt_widget_spans_on_insert` / `_on_delete` (160-209) | Byte-range arithmetic over the stored string. This is the model an image side table must mirror. |
| Run build | `src/rich_text.c` `pt_text_runs_build` (383-590) | Builds runs from span boundaries; LTR fast path keeps the pre-2.0 logical order, the bidi path emits per-paragraph visual runs. `rich_append_piece` measures the run's font and grows `ascent`/`descent`/`line_height`. |
| Measurement | `src/rich_text.c` `pt_text_runs_measure` (616-640), `pt_text_runs_wrap` (703-766) | Runs are summed over logical ranges; the wrapper advances one UTF-8 codepoint at a time and breaks at spaces. A U+FFFC is one codepoint. |
| Per-line map | `src/rich_text.c` `pt_text_visual_map_build_runs` (859-886), `src/font.c` `pt_text_visual_map_build` (1356-1448) | Segments are shaped and turned into glyph clusters with logical start/end and visual x0/x1. A segment with no font is skipped (`!segment->font`). |
| Layout | `src/text_layout.c` `pt_text_layout_build` (128-283) | Runs are built only when `span_count > 0` (line 152); line height is `max(text metrics, runs.line_height)` (157-158); one map per line is built. `pt_text_layout_x_for_position_bias` and `pt_text_layout_position_for_x` delegate to the map. |
| Painting | `src/paint.c` `paint_rich_single_line` (151-166), `paint_rich_wrapped` (169-191), `paint_text_area` (961-1080) | Rich label paths are selected by `span_count > 0` (198, 424). Selection rectangles come from `paint_map_range` (550-589), which merges cluster x-spans. Caret height is the line height. |
| Image drawing | `src/icon.c` `pt_icon_image_raster` (214-260), `pt_icon_draw_canvas` (291-345) | Reference-counted PNG/SVG handle, bounded per-size/scale raster cache, premultiplied ARGB blend into the canvas. This is the draw primitive an image run needs. |
| Editing | `src/widget.c` `entry_insert_text` (846-880), `entry_delete_range` (765-782), `pt_widget_text_selection_set` (1258-1284) | Text mutations shift spans; selection offsets are clamped to the text length but not snapped to cluster boundaries. `entry_selected_text` (414-425) and `entry_copy_selection` (427-449) copy a raw byte range. |
| Caret movement | `src/widget.c` `entry_horizontal` (3899-3980), `text_area_horizontal` (4290-4380), `visual_caret_step` | Visual mode steps through visual-map boundaries; logical mode steps through UTF-8 codepoints. Either way U+FFFC is skipped as one unit if it stays a single codepoint. |
| Undo | `src/widget.c` `pt_edit_state` (19-28), `edit_state_push` (264-290), `entry_history_step` (323-353) | Snapshots text, cursor/anchor and spans only. An image side table must be part of the same snapshot or undo/redo desynchronises it. |
| Accessibility tree | `src/a11y_bridge.c` `a11y_child_count` (314-325), `a11y_child_at` (426-466), `pt_a11y_obj` (115-119) | The AT-SPI tree enumerates real widgets only; paths are `/w<id>/n<widget-id>`. Text is exposed as Unicode character offsets (`a11y_text_view_for`, 1584-1620); the Text selection copy path at 3703 copies raw bytes. |
| Clipboard | `src/window.c` 301-365, `src/wayland/platform.c` | There is no `src/clipboard.c`. Text/image/URI writes go through `pt_platform_clipboard_set` and the window helpers. Alt-text copying is therefore an editing (`widget.c`) and accessibility (`a11y_bridge.c`) change, not a clipboard-backend change. |
| Bidi | `src/bidi.c`, `docs/rtl.md` | U+FFFC is Unicode bidi class ON (other neutral), not BN. It takes the surrounding embedding level from the UBA. It is one codepoint, so a visual cluster or UTF-8 step cannot split it. |

## Feasibility verdicts

### 1. U+FFFC data model - feasible, medium risk

Do **not** extend `pt_text_span`. The 2.0 ABI froze its eight-field layout
including `char family[64]`; adding a handle, a width or an alignment enum would
move every existing offset and break binaries compiled against 2.0. Reusing
existing fields (`size`, `color`) to smuggle a handle is unsound and would make
the frozen struct meaningless.

Use a parallel, sorted side table on `pt_widget`:

```c
pt_text_image *images;   /* sorted by start, one entry per U+FFFC */
uint32_t image_count;
```

The text remains the single source of truth: the placeholder is the three bytes
`EF BF BC` (U+FFFC) in `widget->text`. The side table stores only the descriptor.
This preserves:

- byte offsets through the public API and AT-SPI (a placeholder is 3 bytes, and
  every existing offset helper already works on UTF-8 boundaries);
- plain-text editing: insert/delete/backspace/selection act on the placeholder,
  and the side table mirrors the mutation with insert/delete/clamp rules copied
  from the span helpers;
- bidi: U+FFFC stays in the paragraph and receives the UBA level around it;
- copy/paste and `pt_widget_get_text`: the raw text contains U+FFFC, exactly what
  an application that does not understand images would expect.

The side table is validated so one placeholder maps to at most one image: the
setter snaps to the text length, checks `EF BF BC`, sorts by start, drops
duplicates (last wins, matching the span rule) and drops entries whose bytes are
not a placeholder. `pt_widget_set_text` clears the table, because it already
clears spans.

An image descriptor is not a run in the span sense, but it must enter the run
pipeline. The internal `pt_text_run` and `pt_text_visual_segment` gain an
optional image payload (handle, width/height, alignment, alt). A run with a
image payload has `font == NULL`; this is the marker the map builder, measure and
painter branch on. Existing font runs are untouched.

**Why one codepoint is enough.** The requirement is an object replacement
character, not arbitrary inline markup. One codepoint means no range can be
split by UTF-8 boundaries, combining marks, or a span boundary (spans snap to
UTF-8 boundaries), so "atomic cluster" is structural rather than a special
case. A future multi-codepoint object run would need a new descriptor rather
than a changed `pt_text_span`.

### 2. Baseline alignment and measurement - feasible, medium risk

Width and height are logical pixels. A zero or non-finite dimension means the
intrinsic icon size; if exactly one dimension is positive the other preserves
the aspect ratio. The raster request is bounded by the existing icon cache
(`PT_ICON_CACHE_MAX_PIXELS`); a request that cannot fit draws nothing, exactly
like an oversized button icon. The descriptor never owns or decodes pixels.

Two alignments are enough for the requirement:

| Value | Geometry | Ascent contribution | Descent contribution |
| --- | --- | --- | --- |
| `pt_text_image_align_baseline` (0) | bottom edge on the text baseline | `height` | `0` |
| `pt_text_image_align_middle` | vertical centre on the text baseline | `height / 2` | `height / 2` |

During `pt_text_runs_build`, each image raises the composed metrics:

```
ascent   = max(ascent, image_above)
descent  = max(descent, image_below)
line_height = ascent + descent
```

That is the same composition the file already performs for styled font runs
(`src/rich_text.c:300-320`), so a tall image behaves like a large font run: it
raises every line of the widget. This is deliberate for 2.3.0 and is stated as a
limitation; per-line metrics would change existing styled-text output.

Unit convention matters here. The descriptor stores logical pixels, while
`pt_text_runs_measure` sums the device-pixel advances reported by HarfBuzz and
divides by `runs->scale` at the end, and `pt_text_visual_map_build` keeps its
cluster x coordinates in device pixels (`entry_position_from_x` at
`src/widget.c:746` divides a logical target by `scale` before asking the map).
The implementation must therefore:

- keep the run's logical image width/height for the painter and for logical
  ascent/descent;
- add `image_width * runs->scale` inside `pt_text_runs_measure` before the final
  division, so the returned width is logical like every other range;
- advance the map pen and set the cluster x0/x1 by `image_width * scale`, so the
  map's device convention is preserved.

Because the object is atomic, a measure or paint range that partially overlaps
an image counts and paints the whole image, never a fraction; the public
selection setter snaps such a range outward before it reaches the painter.

The wrapper already advances by UTF-8 codepoints, so a wide image is either kept
whole on the line or moved to the next line; an image wider than the content
box stays alone on its line, matching the existing "one oversized cluster" rule.
Ellipsis truncation uses the same measure and therefore never cuts an image in
half; if the image does not fit before the ellipsis it is dropped with the rest
of the truncated range.

Drawing uses `bottom = baseline + image_below`, `y = bottom - height`; the
painter reuses `pt_icon_draw_canvas` at the pen position and advances the pen by
the image width. The existing clip in `paint_rich_single_line` and
`paint_rich_wrapped`/`paint_text_area` keeps the image inside the widget.

### 3. Painting - feasible, low-medium risk

`pt_text_runs_draw_line_base` (`src/rich_text.c:933-1072`) already walks visual
segments in order and advances a pen by measured segment width. Add one branch:
an image segment draws the handle and advances by its width; it has no
underline, no font color and no glyphs. No change to `pt_canvas`.

Caret and selection are already geometric:

- `pt_text_area_caret_box` uses the line height, which grows with the image, so
  the caret is as tall as the image row.
- Editor selection paints from the per-line visual map (`paint_map_range`),
  which turns an image cluster into one x-span rectangle. A full-line selection
  already fills the whole line height.
- `pt_text_layout_x_for_position_bias` and `pt_text_layout_position_for_x` use
  the map, so hit-testing inside the image resolves to its start or end edge,
  never an interior byte.

The only paint-path wiring is the `span_count > 0` test at `src/paint.c:198` and
`:424`, which must become `span_count > 0 || image_count > 0`; without an image
the condition is unchanged and the existing fast path is byte-identical.

### 4. Atomic selection and alt-text copy - feasible, medium risk

Interactive movement is already atomic: U+FFFC is one UTF-8 codepoint, and
visual movement steps by visual-map cluster boundaries. Two gaps remain:

1. `pt_widget_text_selection_set` clamps to the text length but can place an
   offset inside the 3-byte placeholder. Add a snap helper: a position strictly
   inside an image range becomes its start when it is a selection start or a
   trailing affinity, and its end when it is a selection end or a leading
   affinity. Apply it in `pt_widget_text_selection_set` and before IME deletion
   ranges. `entry_set_cursor` is already fed by boundary movement, but the same
   helper can guard programmatic paths.
2. Copy must replace the placeholder with alt text. A pure helper over the
   widget text and side table walks the selection: copy ordinary bytes, and for
   each fully contained image range append `alt` (or the three U+FFFC bytes when
   `alt` is empty), skipping the placeholder. Both copy sites call it:
   `entry_copy_selection` in `src/widget.c` and the AT-SPI Text copy at
   `src/a11y_bridge.c:3703`. Cut still removes the raw placeholder and the side
   table entry; the clipboard gets plain text.

A partial selection that starts or ends inside an image is snapped outward by
the API, so the helper only needs the fully-contained case; if a caller reaches
the helper with a partial range (defensive), it emits U+FFFC for that image
rather than dropping the object.

### 5. Embedded-object accessibility - feasible, medium risk

The bridge's object model is widget-only (`pt_a11y_obj` at
`src/a11y_bridge.c:115-119`): a type, window and widget. Add a fourth type,
`pt_a11y_obj_image`, that carries the owning widget and the image index. It is a
**virtual child**, not a widget:

- `a11y_child_count` adds `widget->image_count` for label/text-area objects.
- `a11y_child_at` maps the tail indices to image objects in side-table order.
- `a11y_obj_path` extends the parent path with `/i<ordinal>`; the ordinal is the
  sorted side-table index, stable while the set is unchanged.
- The virtual object exposes role `image` (AT-SPI 27; Pt's public
  `pt_a11y_role_icon` stays unchanged), name = alt text/description fallback,
  parent relation, and the text widget's states. A Component interface with the
  image's laid-out extents is optional and can follow once the text layout is
  available; the minimum contract is an accessible, named child.
- The Text interface keeps exposing the raw U+FFFC and unchanged character
  offsets, so an AT that does not understand the child still sees a placeholder
  at the right position. `GetText`/`GetTextAfter`/selection offsets do not move.
- `pt_widget_set_text_images`, clear and any edit that adds/removes a
  placeholder emit `pt_a11y_event_structure_changed`; the existing
  `text_changed` event still fires for the byte change. AT that caches the child
  count refreshes on the structure event.

No public accessibility API is added. This is the lowest-ABI-risk way to meet
"embedded object accessibility" and keeps the 1.x bridge contracts.

### 6. Public API surface and ABI - feasible, additive

Add four functions in W1 and one editing entry point in W3, one enum, one
struct. No existing declaration changes; `pt_text_span` is untouched; no
existing struct layout moves. The new struct is a separate type, so append-only
compatibility holds. `ABI_MAJOR` stays 1 and SONAME stays `libpt.so.1`.

Estimated baseline:

| Wave | Functions | Enum values | Structs | `api/pt.api` |
| --- | --- | --- | --- | --- |
| W1 | +4 | +2 (`pt_text_image_align_baseline`, `_middle`) | +1 (`pt_text_image`) | 580 / 959 |
| W3 | +1 (`pt_text_area_insert_image`) | 0 | 0 | 581 / 960 |

If review decides the insert helper is out of 2.3.0, the final baseline is
580 / 959 and image creation is "set the text, then set the descriptors". The
design keeps the helper because an editor otherwise has to rebuild the whole
side table after every typed character.

## Proposed API

### Placement

- `include/pt/base.h`, immediately after `pt_text_span` (or after
  `pt_text_direction`), so text descriptors live together. The struct refers to
  the opaque image tag with `struct pt_icon_image *`; `icon.h` already defines
  the typedef, and C treats the tag and typedef as the same type. No new header
  and no duplicate typedef.
- `include/pt/widget.h`, next to the styled-span block (318-329), because the
  setters are widget-level like `pt_widget_set_text_spans`. The text-area insert
  helper goes with the text-area block (450-471).

### Declarations

```c
/* Vertical placement of an inline image relative to the text baseline. The zero
 * value is the default: the image's bottom edge sits on the baseline. middle
 * centres the image on the baseline and extends the line above and below. */
typedef enum pt_text_image_align {
    pt_text_image_align_baseline = 0,
    pt_text_image_align_middle
} pt_text_image_align;

/* One inline image run over a U+FFFC object replacement character. start is the
 * byte offset of the three-byte EF BF BC sequence in the widget text; the
 * placeholder is inserted by the caller or by pt_text_area_insert_image.
 * width and height are logical pixels; <= 0 or non-finite means intrinsic size
 * (one positive dimension preserves the aspect ratio). alt is a NUL-terminated
 * UTF-8 string copied into the descriptor and truncated at 127 bytes; it is
 * what a copied selection and AT-SPI expose instead of U+FFFC. image is
 * reference counted: the setter takes a reference and releases it on replace,
 * clear, text replacement or widget destruction. A getter returns the handle
 * borrowed from the widget. */
typedef struct pt_text_image {
    uint32_t start;
    struct pt_icon_image *image;
    float width;
    float height;
    pt_text_image_align align;
    char alt[128];
} pt_text_image;
```

```c
/* Inline image runs on labels and text areas. The setter copies the array,
 * validates each start against the current text (a non-U+FFFC entry is
 * dropped), sorts by start, keeps the last entry for a duplicate placeholder,
 * takes references to non-NULL images and drops NULL entries. NULL or count 0
 * is equivalent to pt_widget_clear_text_images. A text set or a text edit that
 * removes a placeholder releases its image. The setter marks layout dirty and
 * emits a structure-changed accessibility event. Since 2.3. */
pt_api bool pt_widget_set_text_images(pt_widget *widget, const pt_text_image *images,
                                      uint32_t count);
/* Since 2.3. */
pt_api void pt_widget_clear_text_images(pt_widget *widget);
/* Since 2.3. */
pt_api uint32_t pt_widget_get_text_image_count(const pt_widget *widget);
/* Copies one descriptor; the returned image handle is borrowed. Since 2.3. */
pt_api bool pt_widget_get_text_image(const pt_widget *widget, uint32_t index,
                                     pt_text_image *image);

/* Inserts one U+FFFC at the caret, replacing any selection, and attaches the
 * descriptor. image->start is ignored; the byte offset is returned through
 * start when non-NULL. The text area takes a reference and records the change in
 * undo history, so undo/redo restore the placeholder and its image together.
 * Since 2.3. */
pt_api bool pt_text_area_insert_image(pt_widget *area, const pt_text_image *image,
                                      uint32_t *start);
```

### Semantics and conventions

- **Kinds.** The setter accepts labels and text areas, matching
  `pt_widget_supports_spans`. Entries return false/no-op. The insert helper is
  text-area-only.
- **Errors.** Setters are no-ops for NULL objects or a mismatched kind; they
  never abort on a bad descriptor. `pt_widget_get_text_image` returns false for
  an out-of-range index. `pt_text_area_insert_image` returns false for a bad
  kind, a NULL image or a text cap (`pt_entry_max_bytes`), and sets the usual
  thread-local error only if it documents one; the simplest contract is a bool
  with no new error text, like the span setter.
- **Strings.** The text is UTF-8; `alt` is copied at set time. `alt` is bounded
  (127 bytes) because it is stored inline in a public struct, the same trade-off
  `pt_text_span.family[64]` already makes.
- **Threading.** UI thread only, like every text widget operation. The pure
  UTF-8/alt helper is internal and used only on the UI thread.
- **Ownership.** The widget owns image references. `pt_widget_get_text_image`
  returns a borrowed handle valid until the descriptor is replaced, the text is
  changed, or the widget is destroyed. `pt_text_area_insert_image` does not take
  ownership of the caller's reference; the caller may destroy its own handle
  immediately after the call.
- **Clipping and bounds.** Logical width/height are not clipped by the text
  box; the painter clips to the widget. The raster request is bounded by the
  icon cache. A non-finite or non-positive pair falls back to intrinsic size.
- **Zero values.** `pt_text_image_align_baseline` is zero, `width == 0` and
  `height == 0` mean intrinsic, `image == NULL` means "drop this entry", so a
  zero-initialised descriptor is harmless. This follows the project's zero-value
  convention.

### Why not `pt_text_span` with a new flag

`pt_text_span` has no handle, no width and no alignment. A flag plus a
reinterpreted field would be invisible to existing binaries and break the
frozen layout contract. A separate descriptor also keeps the "spans are style,
images are objects" distinction clear: spans can overlap arbitrarily, an image
run is exactly one placeholder.

## Integration points

| File / function | Change | Owner wave |
| --- | --- | --- |
| `include/pt/base.h` | enum and struct after the span type | W1 |
| `include/pt/widget.h` | four descriptor functions beside spans; insert helper in W3 | W1 / W3 |
| `src/internal.h` `pt_widget` | `images`, `image_count`; declarations for image shift/clamp/copy helpers | W1 |
| `src/internal.h` `pt_text_run`, `pt_text_visual_segment` | optional image payload; `font == NULL` marks an image segment | W1 (fields) / W3 (use) |
| `src/image_run.c` (new) | validation, refcount, sort/dedup, insert/delete/clamp, alt-copy, public set/get/clear | W1 |
| `src/widget.c` destroy (1621) and `pt_widget_set_text` (2333) | release image references and clear the table | W1 |
| `src/rich_text.c` `pt_text_runs_build` (383) | add image boundaries and image runs, including the bidi visual loop; grow metrics | W3 |
| `src/rich_text.c` measure/wrap (616, 703) | add image width to the overlapping range; no font shaping of the placeholder | W3 |
| `src/rich_text.c` map build (859) and draw (933) | emit image segments; draw the handle and advance the pen | W3 |
| `src/text_layout.c` (152-158, 240-270) | build runs when `image_count > 0`; line height already picks up `runs.line_height` | W3 |
| `src/font.c` `pt_text_visual_map_build` (1356) | one cluster for a fontless image segment; advance the device-pixel pen by `image_width * scale` and set x0/x1/start_x/end_x | W3 |
| `src/paint.c` (198, 424) | rich path for labels with images | W3 |
| `src/paint.c` `paint_text_area` (961) | selection/caret already map- and line-height-driven; verify only | W3 |
| `src/widget.c` history (19-28, 264-353) | snapshot/restore the image table with references | W3 |
| `src/widget.c` edit/selection/copy (414-449, 765, 846, 1258) | shift/delete images, snap selection, alt-copy, insert helper | W3 |
| `src/a11y_bridge.c` (314, 426, 1584, 3703, 4538) | virtual image child, path, name/role, alt copy, structure event | W2 |
| `src/a11y.c` | only if the role/name mapping needs a helper; no public API | W2 |
| `bindings/cpp`, `bindings/python`, `bindings/java` | wrappers for five functions, enum, struct | W4 |
| `tests/` | see the test matrix | W1-W5 |
| `api/pt.api`, `docs/api-reference.md` | generated after each wave's headers/implementation | W1 / W3 |

## Zero-regression argument

For a widget that never sets an image (`image_count == 0`):

1. `pt_widget_set_text_images` is never called, so no boundary is added and no
   image run exists. `pt_text_runs_build`'s boundary array and run list are the
   same as 2.2.0; the LTR fast path and the bidi path are byte-for-byte the same
   code path.
2. `pt_text_runs_measure` adds zero image terms; `pt_text_runs_wrap`,
   `pt_text_visual_map_build`, `pt_text_layout_build` and the ellipsis loop take
   the same branches. `out->line_height` is the same maximum.
3. `paint.c`'s rich-path test is `span_count > 0 || image_count > 0`, which is
   false exactly when it was false before. `paint_text_area` already keys on
   `has_runs`; that flag is unchanged when no image exists.
4. `pt_widget_set_text` clears an empty table (no-op), destroy releases no
   references, and history snapshots an empty table (no allocation). Widget
   struct size grows internally, but no public layout and no serialized state
   changes.
5. The accessibility tree adds zero virtual children when `image_count == 0`.
6. `tools/ltr-compat.sh` must stay 11/11 `SAME`; the 2.2.0 snapshots must not
   move. A new image golden is additive.

For text that contains U+FFFC but no descriptor, behaviour is unchanged: the
placeholder is shaped like any uncovered codepoint (tofu), exactly as 2.2.0.
This keeps the feature opt-in at the descriptor level.

For RTL text with images, no image-less run changes. With images, the U+FFFC
remains in the UBA input as a neutral; the image segment carries the resolved
level. The master run list stays visual-order, so the painter's pen and the
map's start_x/end_x convention are preserved. The new RTL tests must assert the
same image rectangle and caret x as the LTR mirror of the same content.

## Test matrix

| Wave | Test file | Cases |
| --- | --- | --- |
| W1 | `tests/test_text_image.c` (new) | set/get/clear on label and text area; U+FFFC validation; duplicate last-wins; NULL image removal; refcount survives caller destroy; `set_text` clears; invalid kind; alt truncation; empty table; ASan no leak |
| W1 | `tests/test_abi.c` | enum values stay 0/1; struct field order/size stays; no existing enum moved |
| W3 | `tests/test_rich_text.c` | image width adds to measure; wrap before/after; line height grows for baseline and middle; ellipsis never splits; bidi mixed line; span + image boundaries |
| W3 | `tests/test_text_area.c` | caret skips the whole placeholder both directions; Backspace/Delete remove placeholder and descriptor; selection set inside snaps; painted selection is one rectangle; undo/redo restores text + image; insert helper replaces selection; caret box height |
| W3 | `tests/test_editing.c` | copy selection returns alt text; empty alt returns U+FFFC; cut leaves text without placeholder; paste of plain text does not create an image |
| W3 | `tests/test_visuals.c` | additive image goldens; no existing golden moves |
| W2 | `tests/test_a11y_bridge.c` | virtual child count/index; role image; name is alt; text still contains U+FFFC; structure event on set/edit; selection copy uses alt |
| W4 | binding tests | C++/Python/Java construction, ownership (caller drops after set), getter borrowed, insert helper, enum/struct layout |
| W5 | examples smoke | C/C++/Python/Java inline-image example builds and runs headless |

Sanitizer runs are serial and one suite at a time. All builds use `-j2`; never
`-j8`.

## Parallel wave decomposition

The 2.0 precedent is explicit: the text pipeline is a serial hot spot
(`docs/ROADMAP.md` M22 W3), while geometry/chrome and independent domains ran
in parallel (W5a). M25 follows the same shape. Files are the unit of ownership;
no two active agents touch the same file.

### Waves

| Wave | Scope | Files owned (exclusive) | Depends on | Parallel with | Owner |
| --- | --- | --- | --- | --- | --- |
| **W0** | This design and the ROADMAP proposal | `docs/m25-design.md`, `docs/ROADMAP.md` | - | - | scaffold author |
| **W1** | Descriptor model, `src/image_run.c`, refcount/lifecycle, set/get/clear, baseline arithmetic, tests, baseline 580/959 | `include/pt/base.h`, `include/pt/widget.h` (four functions), `src/internal.h`, `src/image_run.c` (new), `src/widget.c` (destroy 1621 + set_text 2333 only), `tests/test_text_image.c`, `tests/test_abi.c`, `api/pt.api`, `docs/api-reference.md` | W0 | none (foundation) | DuMate |
| **W2** | AT-SPI virtual image child, role/name, structure events, selection copy via alt helper | `src/a11y_bridge.c`, `src/a11y.c`, `tests/test_a11y_bridge.c` | W1 | W3, W4, W5 | Loomy (one agent, no subagents) |
| **W3a** | Header/API freeze: insert declaration, any internal marker fields; no behavior | `include/pt/widget.h` (insert), `src/internal.h` (if not final in W1) | W1 | W4, W5 (after commit) | DuMate |
| **W3b** | Run/measure/wrap/map: image runs, metrics, fontless map clusters | `src/rich_text.c`, `src/font.c`, `src/text_layout.c`, `tests/test_rich_text.c`, `tests/test_font.c` | W1, W3a | W2, W4, W5 | DuMate (serial) |
| **W3c** | Paint/editor: rich path, history, atomic selection, insert/delete, alt copy, goldens; final baseline 581/960 | `src/paint.c`, `src/widget.c` (all edits), `tests/test_text_area.c`, `tests/test_editing.c`, `tests/test_visuals.c`, `api/pt.api`, `docs/api-reference.md` | W3b | W4, W5 (tests compile after W3c) | DuMate (serial) |
| **W4a** | C++ wrappers + C++ tests | `bindings/cpp/pt/pt.hpp`, `bindings/cpp/test_pt.cpp` | W3a | W3b/W3c, W4b, W5 | Q-CN |
| **W4b** | Python and Java wrappers + tests | `bindings/python/pt.py`, `bindings/python/test_pt.py`, `bindings/java/src/pt/Pt.java`, `bindings/java/test/pt/PtTest.java` | W3a | W3b/W3c, W4a, W5 | WB-CN |
| **W5** | Public guide/example updates, four-language inline-image example, bilingual pairs, README "what is new", generated-reference check | `docs/quickstart.md` + `docs/zh-CN/quickstart.md`, `docs/architecture.md` + zh, `docs/accessibility.md` + zh, `docs/bindings.md` + zh, `README.md` + `README.zh-CN.md`, `examples/c/`, `examples/cpp/`, `examples/python/`, `examples/java/`, `Makefile` (example targets at end only), `docs/m25-design.md` (status) | W3a | W2, W3b/W3c, W4 | Lobster |
| **W6** | Merge, full gate, version 2.3.0, release/ROADMAP status, tag-ready (not tagged) | `include/pt/base.h` (version macros only), `Makefile` (`VERSION`), `docs/release.md`, `docs/ROADMAP.md`, `docs/m25-design.md` (final status) | W1-W5 | none | Lobster + DuMate |

### Dependency graph

```
W0
 └─ W1 (foundation: headers, internal fields, image_run.c, lifecycle)
     ├─ W2 (a11y)  ──────────────────────────────┐
     └─ W3a (insert/header freeze)                │
         ├─ W3b (runs/measure/wrap/map) ─ W3c ────┤
         ├─ W4a (C++ wrappers)                    ├─ W6 (integration/release)
         ├─ W4b (Python/Java wrappers)            │
         └─ W5  (docs/examples) ──────────────────┘
```

W2 is the only wave that can run **fully independently** of the text pipeline
after W1; it reads the descriptor getters and never touches `rich_text.c`,
`text_layout.c`, `font.c`, `paint.c` or `widget.c`. W4a/W4b/W5 can start as soon
as W3a declares the insert signature; their generated/compiled tests run after
W3c, and W6 does not merge a binding/example before its code compiles.

### File-domain rules

- **Serial hot spot (one agent, never two):** `src/rich_text.c`,
  `src/text_layout.c`, `src/font.c`, `src/paint.c`, `src/widget.c`,
  `src/internal.h`, `include/pt/base.h`, `include/pt/widget.h`. W1 and W3 are
  the only code waves allowed here; they are sequential. W3b and W3c are the
  same owner and the same branch, split only so the gate can run between them.
- **Independent domains:** `src/image_run.c` (W1), `src/a11y_bridge.c` +
  `src/a11y.c` (W2), each binding language file (W4a/W4b), docs and examples
  (W5). These are disjoint and can run concurrently.
- **Generated files:** `api/pt.api` and `docs/api-reference.md` are updated only
  after the owning header and implementation are complete. W1 updates them to
  580/959; W3c updates them to 581/960. No other wave edits them.
- **Test files:** one owner per file. W1 takes `test_text_image.c` and
  `test_abi.c`; W2 takes `test_a11y_bridge.c`; W3 takes `test_rich_text.c`,
  `test_font.c`, `test_text_area.c`, `test_editing.c`, `test_visuals.c`; W4 owns
  the binding tests; W5 owns examples. No wave adds assertions to another
  wave's file after that wave closes.
- **Binding files:** one file per language, so C++ and Python can run in
  parallel; a single language file has one owner for the whole wave.
- **Docs:** W5 owns the public guide pairs and README; W6 owns release/ROADMAP
  status. `check-bilingual-docs.py` only pairs the public docs listed in the
  tool, so `m25-design.md` (English internal) needs no Chinese pair, exactly like
  `m23-design.md` and `m24-design.md`.

### Review strategy

- W1's refcount and validation get a bounded review by Loomy before W2 starts.
- W2's virtual-child path and structure events get a review by Q-CN; the review
  must confirm character offsets are unchanged and no image-less tree changes.
- W3's run/map invariants (visual order, pen advance, level, line metrics) get a
  review by Lobster; the reviewer reproduces `tools/ltr-compat.sh` and the
  image/RTL tests on a separate worktree.
- W4 gets a cross-language parity check by the provider that did not write the
  file (WB-CN reviews C++, Q-CN reviews Python/Java) plus
  `tools/gen-binding-stubs.py --check --strict`.
- W6 reproduces the gate on a clean worktree and a second provider runs
  `tools/ci.sh` independently. No wave may lower the 2.0 LTR gate or the 2.2
  RTL gate to land images.

### Provider allocation and budget

Estimated total: **~750-850 credits**, larger than M24's ~250 because this is a
new structural text feature. The allocation assumes DuMate is the main heavy
channel, Loomy runs at most one agent at a time, Q-CN/WB-CN take bounded
parallel domains, and Lobster integrates.

| Provider | Waves | Share | Notes |
| --- | --- | --- | --- |
| DuMate | W1, W3a, W3b, W3c | ~50% | Text pipeline is the serial hot spot; W1 and W3 are sequential, so do not split them across providers. |
| Loomy | W2 only | ~15% | Exactly one agent, no fan-out; a11y is independent of W3. If W2 blocks, it waits for W1 rather than touching the pipeline. |
| Q-CN | W4a (C++) | ~8% | Independent binding file; also reviews W2. |
| WB-CN | W4b (Python + Java) | ~10% | Two independent files; also reviews the C++ wrappers. |
| Lobster | W5, W6 | ~17% | Docs/examples parallel, then integration and the release gate; reviews W3. |

Splitting rule: if W3c cannot finish its gate in one wave, split W3c1 (history +
selection/copy + insert) and W3c2 (paint/goldens) on the same owner, never run
two agents in `widget.c`/`paint.c`. If W2 cannot land live AT-SPI verification,
it still lands the virtual-child implementation and headless introspection
tests; the live check is marked best-effort, not a reason to drop the wave. If
W4 or W5 is blocked on a missing signature, it waits for W3a rather than
inventing a parallel API.

## Risks and mitigations

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Run/map split leaves the image partly in a text run, so HarfBuzz shapes U+FFFC and the measured width differs from the drawn box | High | Image start/end are hard boundaries; an image range is one codepoint and spans snap to codepoint boundaries. A build invariant asserts no font run overlaps an image range; unit test compares measure and painted x. |
| Bidi odd-level sub-run reversal puts the image in the wrong visual slot | High | The image is a run in the same master visual list as styled pieces; W3b tests an RTL paragraph with image between Hebrew letters and asserts the rectangle order and caret x. |
| Undo/redo restores text without the image table (or double-frees handles) | High | Extend `pt_edit_state` with a deep image snapshot that takes references; ASan/LeakSanitizer on every undo test. |
| A tall image changes all line heights, surprising styled-text users | Medium | Documented limitation; no-image and span-only output is unchanged. Per-line metrics stays a separate item. |
| Programmatic selection can still create an interior offset | Medium | Snap helper at `pt_widget_text_selection_set` and IME deletion; paint/map already resolve interior clicks to edges. |
| AT-SPI virtual child breaks character-offset expectations | Medium | Text interface content and offsets stay raw; child is additive; structure event on every set/change. |
| Image handle lifetime makes a getter dangling | Medium | Widget takes references; getter is borrowed with the same contract as button/window icon getters; tests drop the caller handle after set. |
| Oversized image request exhausts the raster cache | Low | Reuse the existing bounded cache and pixel cap; a request that cannot fit draws nothing, like an oversized button icon. |
| New public struct freezes a too-small alt buffer | Low | 127 bytes is enough for a short description; a longer alt belongs in an application side channel or a future descriptor, not a layout change. |
| Binding parity breaks because a struct contains a pointer | Medium | Hand-written wrappers follow the existing `pt_text_span`/icon-handle conventions; `gen-binding-stubs.py --check` plus four-language tests are the gate. |

## Open questions for review

1. **Alt buffer size.** 128 bytes follows `family[64]` but may be tight for
   long descriptions. The alternative is an opaque descriptor with an internal
   string; that costs more API and wrapper surface. Recommendation: keep 128 for
   2.3.0 and document truncation.
2. **Insert helper or setter only.** Recommendation: keep
   `pt_text_area_insert_image`; otherwise every keystroke before an image forces
   the application to rebuild all offsets.
3. **Component extents for the virtual child.** Recommendation: minimum
   accessible child first; Component extents can be a follow-up if AT requires
   geometry. This keeps W2 out of the text-layout files.
4. **Entry images.** Out of scope; the entry path has no spans or layout maps
   and would be a separate design.
5. **Image tint.** Out of scope; untinted drawing preserves the source alpha and
   matches `pt_icon_draw_canvas(..., 0)`.

## Implementation review decisions

The implementation review settled these points after the waves landed. They are
the record of what 2.3.0 actually does where the proposal left a choice.

- **`noinline` for the copy helper.** The alt-copy helper is marked `noinline`
  to stop GCC from folding it into `pt_widget_images_copy_text` and emitting a
  false `-Wstringop-overflow` under `werror=1`. This is the first use of the
  attribute in the tree; the review reproduced the warning with it removed, so
  the attribute is a compiler workaround, not a behavioral change.
- **One copy helper.** The alt-range substitution lives in one shared helper
  used by both the widget copy/cut path and the AT-SPI `CopyText`/`CutText`
  path. The a11y bridge does not carry a second copy of the logic.
- **Accessibility deletes use the normal editor.** `CutText` and `DeleteText`
  call `pt_widget_text_delete_range`, so they drop only the descriptors that
  overlap the removed bytes and record one undo step; there is no separate
  accessibility delete path.
- **Extents and hit testing were implemented, not deferred.** Open question 3
  proposed "minimum accessible child first"; review found the text layout
  already exposes the rectangle, so the virtual child publishes real
  `GetExtents`/`GetPosition`/`GetSize` and `GetAccessibleAtPoint` resolves to it.
- **Java UTF-8 truncation fixed.** The Java wrapper's alt setter truncates on a
  UTF-8 sequence boundary, matching the C copy rule instead of cutting a
  multibyte sequence.
- **W3a insertion double-shift fixed.** `pt_text_area_insert_image` no longer
  shifts the descriptor table twice when it replaces a selection; the regression
  is covered by `test_text_area`.
- **Three old assertions updated by design.** Selection snap-out means a
  programmatic endpoint inside a placeholder expands to the object edge; the
  three assertions that expected the old interior offset were rewritten to the
  intended semantics, and no other existing test changed.
- **Open questions closed as recommended.** Alt stays at 127 bytes plus NUL;
  the insert helper stays public; entry images, per-line metrics, tint and
  animation stay out of scope.

## Completion criteria for 2.3.0

- `api/pt.api` reads 581 / 960 (or 580 / 959 if the insert helper is deferred),
  binding parity is 581/581 in C++, Python and Java, `ABI_MAJOR` stays 1 with
  SONAME `libpt.so.1`, and no existing enum/struct moves.
- Text without images is byte-identical to 2.2.0: `tools/ltr-compat.sh` 11/11
  `SAME`, existing snapshots unchanged, `make check` green with additive tests
  only.
- Inline images render on labels (single-line and wrapped) and text areas, wrap
  as one unit, align to baseline or middle, raise line metrics, paint a single
  selection rectangle, and keep caret movement atomic.
- Copying a selection replaces U+FFFC with alt text (or keeps U+FFFC when alt is
  empty); cut removes the placeholder and its descriptor.
- AT-SPI exposes one named image child per run; the text interface keeps raw
  U+FFFC and unchanged character offsets; a structure event fires when the set
  changes.
- `check-bilingual-docs.py`, `gen-api-reference.py --check`,
  `tools/gen-binding-stubs.py --check --strict` and the staged-install consumers
  pass; sanitizer runs are serial; `git status` is clean.