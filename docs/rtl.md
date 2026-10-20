# Right-to-left and bidirectional text

English | [简体中文](zh-CN/rtl.md)

Status: a post-1.0 plan. Nothing in this document is part of the 1.0.0 public
surface. The 1.0 limitation is deliberate: the text model, the layout pass and
the accessibility offsets are already public and cannot be redesigned in a
1.x release without breaking every application that stores or indexes text.
This document fixes the design direction and the test obligations before that
work starts.

Related reading: [architecture.md](architecture.md) describes the current
layout and painting pass, [accessibility.md](accessibility.md) defines the
character-offset contract used by assistive technology, and
[api-policy.md](api-policy.md) governs how the additive API described here can
land after 1.0.

## What 1.0 guarantees

- Public text setters, getters, spans and selections use UTF-8 bytes. The
  accessibility getters use Unicode scalar-value offsets. These units do not
  change when bidi is added; direction is a presentation property.
- HarfBuzz shapes a line as one run. It guesses the script and direction for
  that run, but Pt does not apply the Unicode Bidirectional Algorithm (UBA),
  does not reorder mixed-direction runs, and does not isolate embedded
  directions. Wrapping and caret math treat the line left-to-right.
- `pt_text_align_start` and `pt_text_align_end` resolve to left and right.
  `pt_align_start`/`pt_align_end` and `pt_justify_start`/`pt_justify_end` do the
  same in layout. Container rows are built from the first child to the last,
  left-to-right.
- Labels, buttons, entries and text areas all use the same single-run paint
  path. Password entries hide their text in the accessibility bridge, but that
  is independent of direction.
- There is no public direction property, no locale query and no mirrored icon
  API. An application cannot ask Pt to lay out an RTL interface in 1.0.

## Terms

- **Logical order** is the order of characters in the UTF-8 string and in the
  selection model. It never changes when the text is displayed.
- **Visual order** is the order of glyph runs on a line after the UBA has
  assigned embedding levels and reordered them.
- **Text direction** controls paragraph ordering, glyph shaping and
  start/end alignment for text.
- **UI direction** controls the order of row children, start/end alignment,
  padding and margin interpretation, scrollbar placement and mirrorable
  chrome such as back/forward chevrons and sort arrows.
- **Auto** text direction resolves each paragraph from its first strong
  character, as UBA rules P2/P3 specify. An empty or neutral-only paragraph
  inherits the widget's resolved text direction.

Text direction and UI direction are separate. A Hebrew sentence inside an
English form is common, and an embedded code editor inside an RTL application
must be able to stay left-to-right without flipping its surrounding layout.

## Proposed public shape

Names below are a design sketch, not declarations. They must pass the
[API policy](api-policy.md) review and be added to all three bindings in the
same change. They are chosen to avoid the existing `pt_direction`, which is the
row/column layout axis.

```c
typedef enum pt_ui_direction {
    pt_ui_direction_inherit = 0,
    pt_ui_direction_ltr,
    pt_ui_direction_rtl
} pt_ui_direction;

typedef enum pt_text_direction {
    pt_text_direction_auto = 0,   /* first strong character per paragraph */
    pt_text_direction_ltr,
    pt_text_direction_rtl
} pt_text_direction;

/* Proposed, additive after 1.0. */
void pt_app_set_ui_direction(pt_app *app, pt_ui_direction direction);
pt_ui_direction pt_app_get_ui_direction(const pt_app *app);
void pt_window_set_ui_direction(pt_window *window, pt_ui_direction direction);
pt_ui_direction pt_window_get_ui_direction(const pt_window *window);
void pt_widget_set_ui_direction(pt_widget *widget, pt_ui_direction direction);
pt_ui_direction pt_widget_get_ui_direction(const pt_widget *widget);
void pt_widget_set_text_direction(pt_widget *widget, pt_text_direction direction);
pt_text_direction pt_widget_get_text_direction(const pt_widget *widget);
```

Resolution order is the widget's explicit value, then the nearest ancestor
with an explicit value, then the window, then the application. The compiled
defaults are `pt_ui_direction_ltr` and `pt_text_direction_auto`; `inherit` is
therefore the zero value and leaves existing code unchanged. An application
that wants an RTL shell sets the UI direction once on the app or window, and a
nested editor sets `pt_widget_set_ui_direction(editor, pt_ui_direction_ltr)`.

The first release does not guess the UI direction from the locale. 1.0 has no
locale API, `LANG` parsing differs between desktops and launch environments,
and a wrong guess would mirror every window. A later additive step may add an
explicit `pt_app_set_locale` or a startup helper, but it must keep an
application override and must be documented as a behavior change. Auto text
direction is safe because it applies per paragraph and does not move UI chrome.

## Bidi engine

The paragraph algorithm is the part that cannot be approximated with a
reverse-the-string loop. It needs the full UBA: explicit embeddings and
overrides, isolates, weak and neutral resolution, paired brackets, implicit
levels, and the line-level rules (especially rule L1) after wrapping.

The implementation choice is one of two reviewed paths:

1. **FriBidi as a build dependency.** It implements UBA and mirroring, is
   widely packaged, and gives the project a tested reference implementation.
   The license is LGPL-2.1-or-later; dynamic linking keeps Pt's own code under
   its Apache-2.0 terms. The dependency cost and distribution policy must be
   settled in [THIRD_PARTY.md](../THIRD_PARTY.md), the pkg-config file and the
   release notes before code depends on it.
2. **A vendored, tested UBA core.** A small implementation with the same
   permissive license as Pt avoids a new shared-library dependency, but the
   project then owns the Unicode conformance test suite and every future
   Unicode version bump. This is only acceptable with the generated
   `BidiTest.txt` and `BidiCharacterTest.txt` conformance runs in CI.

Either choice produces the same internal shape:

```c
typedef struct pt_bidi_run {
    size_t start;        /* logical UTF-8 byte offset */
    size_t end;
    size_t visual_start; /* glyph slot after reordering */
    uint8_t level;
    bool rtl;
} pt_bidi_run;
```

Per paragraph:

1. Strip or honor the widget's explicit direction. For auto, run P2/P3 over
   the logical text.
2. Resolve embedding levels and apply rule X9 removal/restoration semantics
   for controls that must not be painted.
3. Reorder runs by level. Mirror paired characters (parentheses, brackets,
   angle quotes and the Unicode mirroring table) only in the visual pass; the
   stored string and offsets stay logical.
4. Split runs at script and font boundaries, then shape each run with
   HarfBuzz using explicit `direction`, `script` and `language`. Never let
   `hb_buffer_guess_segment_properties` decide the direction of an RTL run.
5. Keep a bidirectional map between logical byte offsets and visual x
   positions. Measurement, wrapping, caret placement, hit testing, selection
   painting and ellipsis all consume that map rather than assuming x grows
   with the byte offset.

Wrapping happens in logical order into candidate lines, then each line is
reordered and measured. The line breaker must understand UBA L1 whitespace at
line ends. The first implementation may keep Pt's current whitespace-only line
breaking; full UAX #14 line breaking is a separate quality step and must not
silently change logical offsets.

## Layout and painting

- `pt_text_align_start` and `pt_text_align_end` resolve against the paragraph
  direction when the widget uses auto direction. `pt_text_align_center` is
  unchanged. A multi-paragraph text area may align different paragraphs in
  opposite directions; the caret and selection remain one logical range.
- A container with RTL UI direction lays out row children from right to left,
  maps `pt_align_start` to the right edge, maps `pt_justify_start` to the right
  edge and mirrors the meaning of start/end padding and margins. Physical
  left/right values remain physical for applications that need them.
- Scrollbars, scroll shadows and overscroll belong on the inline-start or
  inline-end side, not unconditionally on the right. Table and tree headers
  keep logical column order; only the horizontal start edge changes.
- Mirrorable glyphs (back/forward, next/previous, sort chevrons, disclosure
  arrows and similar chrome) are selected by the paint code from the resolved
  UI direction. User-supplied images are never mirrored automatically; a
  future image API can expose an explicit mirror flag.
- Ellipsis and clipping stay on the visual end of the line. A truncated RTL
  line must not cut the logical first character merely because it is drawn on
  the right.
- Font fallback and bidi are adjacent problems. A paragraph mixing Latin,
  Arabic and Hebrew needs per-run family selection in addition to direction;
  the existing font fallback work must land before RTL is advertised as
  complete. Until then, an application can select a family that covers its
  scripts.

## Editing, selection and hit testing

- `pt_widget_get_text`, `pt_widget_set_text`, `pt_widget_get_text_span` and
  selection APIs keep their byte units. A selection can be made across runs
  with different embedding levels without reordering the stored bytes.
- Arrow keys move by logical character offset but paint the caret at the
  corresponding visual boundary. Left/right arrows follow the visual direction
  of the current run unless the platform convention requires logical movement;
  Home/End map to the visual line ends. The chosen behavior must be tested
  against a real keyboard layout and documented in `pt/widget.h`.
- A mouse click maps visual x to the nearest logical boundary through the
  bidi map. The caret at a run boundary must be drawn on the correct side of
  the boundary, and dragging across runs paints a selection with multiple
  rectangles, not one reversed span.
- The caret rectangle sent to `text-input-v3` is the visual rectangle at the
  logical caret. Surrounding text and delete-surrounding-text stay in logical
  bytes. IME preedit is shaped in the direction of the insertion context.
- Coordinates in `pt_text_layout_*` are internal; if any become public, they
  must state whether they are logical or visual. They are logical in this plan.

## Accessibility

- AT-SPI `Text` methods continue to report character offsets in logical order.
  A screen reader reads the logical string, not the visual reordering.
- The bridge publishes the paragraph direction as a text attribute (for
  example `direction:rtl`) through the existing attribute path, and keeps
  `GetText`, `GetCharacterAtOffset`, `GetStringAtOffset` and selection
  consistent with the logical offsets.
- The accessibility tree keeps the same roles and names. Direction is
  presentation metadata; it never changes a role or a state.
- Password text remains hidden in every direction. Bidi must not create a
  path that exposes password characters through `GetCharacterAtOffset`,
  attributes or change events.

## Bindings and examples

All proposed functions are ordinary value setters/getters, so the C++ wrapper,
Python ctypes class and Java FFM wrapper follow the existing generated-binding
pattern. The parity audit is a hard gate: the API is not merged until all four
surfaces report the same count and the C++ `[[nodiscard]]`/error conventions
match.

The C gallery gets one RTL screen with an RTL row, a mixed-direction label, a
text area containing Hebrew and Latin, and a nested LTR panel. The C++, Python
and Java examples each get a smaller direction switch so the bindings are not
only compiled but exercised.

## Compatibility and rollout

Bidi is additive at the API level but can change pixels for applications that
already put RTL text on screen. That is a rendering correction, not a new
layout contract, and the release notes must say so. Existing LTR applications
must be byte-for-byte stable under `pt_ui_direction_ltr` and
`pt_text_direction_ltr`.

The public byte-offset and accessibility character-offset contracts do not
move. No existing enum value changes. `pt_text_direction_auto` and
`pt_ui_direction_ltr` are the zero values, so a zero-initialized widget keeps
today's behavior. A future major release may change UI auto-detection, but a
1.x release may not.

## Test plan

- **UBA conformance.** Import the Unicode `BidiTest.txt` and
  `BidiCharacterTest.txt` data with its license, and run every applicable line
  in a headless test. Keep the Unicode version in the test output and in
  `THIRD_PARTY.md`.
- **Paragraph cases.** Cover first-strong detection, empty and neutral-only
  paragraphs, numbers around RTL text, isolates and overrides, paired
  brackets, combining marks, Hebrew/Arabic with Latin, and embedded LTR code
  inside RTL prose.
- **Layout cases.** Word wrap, line-end whitespace, ellipsis, center/start/end
  alignment, mixed-script lines, and a line with both LTR and RTL runs at
  fractional scale.
- **Editing cases.** Arrow keys, Home/End, Shift+arrow, mouse click and drag,
  double-click word selection, caret at a run boundary, selection across
  paragraphs, and IME preedit inside an RTL run. Assert both stored byte
  offsets and painted caret rectangles.
- **Accessibility cases.** Live AT-SPI checks for logical `GetText`,
  `GetCharacterAtOffset`, `GetStringAtOffset`, selection and the direction
  attribute. Exercise a password entry in both directions and assert that no
  character is exposed.
- **Mirroring cases.** RTL row order, start/end padding, scrollbar side,
  back/forward and sort glyphs, and a nested LTR panel inside an RTL window.
- **Compatibility cases.** Run the existing LTR screenshot and property suites
  with forced LTR and compare against the 1.0 baseline. The LTR fast path must
  not regress the current guard timings.
- **Live compositor.** Add an RTL window to `test_wayland` and assert that the
  caret rectangle sent through `text-input-v3` and the painted selection agree
  after a real click and a focus change.

## Performance

The LTR path is the common path and gets a fast path: if every paragraph is
resolved LTR and contains no bidi controls, skip UBA and use the existing
single-run shaping. Bidi results are cached per line keyed by text, direction,
font and width, bounded like the current HarfBuzz shape cache. The guard suite
gets one mixed-script dense paragraph and one RTL table-row profile; a
regression in the LTR profile blocks the change just as a bidi profile failure
does.

The extra allocations for runs, levels and the logical/visual map must be
bounded by line length and released with the text layout. No per-frame
allocation is allowed on an unchanged line.

## Delivery order

1. Direction properties, resolution and auto detection; no visual reorder yet.
   Headless tests pin the API, inheritance and default behavior.
2. UBA levels, reordering, mirroring and per-run shaping for labels and
   single-line text. LTR snapshots must stay unchanged.
3. Wrapped labels and text areas: line breaks, ellipsis, alignment and the
   logical/visual map.
4. Editing: caret, selection painting, hit testing, keyboard movement and IME
   rectangles.
5. UI mirroring: rows, start/end geometry, scrollbars and directional icons.
6. Accessibility attributes and logical-offset assertions, bindings and
   examples.
7. Conformance, fuzz and performance hardening; only then remove bidi from the
   1.0 limitations.

## Open decisions

- FriBidi dependency versus a vendored UBA core, including the license,
  packaging and Unicode update policy.
- Whether logical or visual caret movement is the default for arrow keys, and
  how an application can choose the other behavior without a new API for every
  widget.
- Whether text spans can override direction per run. The initial answer is no:
  a span controls style, not paragraph structure; that keeps the public
  `pt_text_span` layout frozen.
- The minimum font coverage guarantee for Arabic and Hebrew, and whether a
  missing glyph is a documented fallback failure or a hard configuration
  error.
