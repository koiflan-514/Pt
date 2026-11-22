# Right-to-left and bidirectional text

English | [简体中文](zh-CN/rtl.md)

Status: delivered in 2.0.0. This document is the design contract that 2.0
ships, not a post-1.0 plan. The direction API is additive, the text model and
the offset contracts are unchanged, and a pure LTR application keeps the 1.4
pixels byte for byte. Every section below states what landed, the decisions
that were taken while building it, and the limitations that remain.

Related reading: [architecture.md](architecture.md) describes the bidi pipeline
inside layout and painting, [accessibility.md](accessibility.md) defines the
character-offset and direction contract used by assistive technology, and
[api-policy.md](api-policy.md) records why 2.0 keeps SONAME `libpt.so.1`.

## Guarantees and contracts

- Public text setters, getters, spans and selections use UTF-8 bytes. The
  in-process `pt_widget_get_a11y_text` getter also uses byte offsets; the AT-SPI
  `Text` interface converts to Unicode scalar-value offsets on D-Bus. Neither
  unit changes when bidi is added: direction is a presentation property.
- The paragraph algorithm is the full Unicode Bidirectional Algorithm (UBA),
  including explicit embeddings and overrides, isolates, weak and neutral
  resolution, paired brackets, implicit levels and the line-level rules after
  wrapping. Pt no longer shapes a mixed-direction line as one guessed run.
- The stored string, the selection model and every public offset stay logical.
  Visual order exists only inside measurement, painting, hit testing and caret
  placement, behind an internal logical-to-visual map.
- UI direction and text direction are independent. An LTR code editor can sit
  inside an RTL shell and vice versa.
- A pure LTR application that never sets a direction behaves exactly as it did
  in 1.4: the same layout, the same pixels and no new per-frame work.

## Terms

- **Logical order** is the order of characters in the UTF-8 string and in the
  selection model. It never changes when the text is displayed.
- **Visual order** is the order of glyph runs on a line after the UBA has
  assigned embedding levels and reordered them.
- **Text direction** controls paragraph ordering, glyph shaping and
  start/end alignment for text.
- **UI direction** controls the order of row children, start/end alignment,
  scrollbar placement and mirrorable chrome such as chevrons and sort arrows.
- **Auto** text direction resolves each paragraph from its first strong
  character, as UBA rules P2/P3 specify. An empty or neutral-only paragraph
  falls back to the resolved UI direction.

## Direction model

The public surface is two enums and eight functions. `pt_direction` remains the
unrelated row/column layout axis.

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

void pt_app_set_ui_direction(pt_app *app, pt_ui_direction direction);
pt_ui_direction pt_app_get_ui_direction(const pt_app *app);
void pt_window_set_ui_direction(pt_window *window, pt_ui_direction direction);
pt_ui_direction pt_window_get_ui_direction(const pt_window *window);
void pt_widget_set_ui_direction(pt_widget *widget, pt_ui_direction direction);
pt_ui_direction pt_widget_get_ui_direction(const pt_widget *widget);
void pt_widget_set_text_direction(pt_widget *widget, pt_text_direction direction);
pt_text_direction pt_widget_get_text_direction(const pt_widget *widget);
```

UI resolution is the widget's explicit value, then the nearest ancestor with an
explicit value, then the window, then the application, then LTR. Text resolution
is the widget's explicit value, then the nearest ancestor, then auto; auto uses
the resolved UI direction only as the last-strong fallback for a neutral-only
paragraph. Setters ignore NULL and out-of-range values, invalidate layout and
request a repaint on a change. Getters return the stored explicit value, never
the resolved one: an unset widget reports `pt_ui_direction_inherit` or
`pt_text_direction_auto`.

`inherit` and `auto` are the zero values, so a zero-initialized widget keeps the
1.x behavior and an application that wants an RTL shell sets the UI direction
once on the app or window. A nested editor overrides it with
`pt_widget_set_ui_direction(editor, pt_ui_direction_ltr)`. The release does not
guess the UI direction from the process locale; 1.0 has no locale API and a
wrong guess would mirror every window. Auto text direction is safe because it
applies per paragraph and does not move UI chrome.

```c
pt_app_set_ui_direction(app, pt_ui_direction_rtl);
pt_widget *editor = pt_text_area_create(root, "Write a note");
pt_widget_set_ui_direction(editor, pt_ui_direction_ltr);
pt_widget_set_text_direction(editor, pt_text_direction_auto);
```

## The engine and the pipeline

2.0 vendors **SheenBidi v3.0.0** (Apache-2.0, tag commit `cfe430e`, UCD 17.0.0)
under `third_party/sheenbidi`, compiled as one unity object with its warnings
isolated and every `SB*` symbol hidden. FriBidi was rejected: its LGPL terms
would constrain static linking and distribution. `PROVENANCE.md` records the
tag, the fetched digest and the upgrade policy; [THIRD_PARTY.md](../THIRD_PARTY.md)
records the license.

The upstream UTF-8 entry point has an L1 line-end whitespace defect, so Pt's
adapter decodes UTF-8 once into UTF-32, keeps a byte-offset table beside the code
points, and drives the engine on that internal path. `src/bidi.c` owns the
adapter; nothing outside it sees SheenBidi types.

Per paragraph:

1. An explicit text direction selects the base level. Auto runs UBA P2/P3 over
   the logical text, skipping characters inside isolates, and falls back to the
   resolved UI direction for a neutral-only or empty paragraph.
2. The engine resolves embedding levels and applies rule X9 semantics: bidi
   control characters stay in the stored string and in the shaping input, but
   painting skips them.
3. Runs are reordered by level. Paired characters (parentheses, brackets, angle
   quotes and the Unicode mirroring table) are mirrored by HarfBuzz at paint
   time; Pt never pre-mirrors bytes or glyph slots.
4. Runs are split at level, script and font boundaries and shaped with an
   explicit `direction`, `script` and `language`. Arabic joining and Latin
   kerning survive mixed-direction lines.
5. Wrapping happens in logical order into candidate lines; each line is then
   reordered, measured and painted. Rule L1 resets trailing whitespace at the
   visual line end, and the soft-wrap boundary keeps caret affinity on the
   correct line.
6. A bidirectional map between logical byte offsets and visual x positions
   feeds measurement, wrapping, caret placement, hit testing, selection painting
   and ellipsis. Ellipsis sits on the visual end of the line, so a truncated RTL
   line never cuts its logical first character.

Wrapped labels and text areas reorder per line with a per-paragraph base level,
so one text area can show opposite paragraph directions while the caret and
selection remain one logical range. The shape cache is keyed by text, font,
direction and level key, so the same bytes shaped LTR and RTL never collide.

## Layout and painting

- A container with RTL UI direction lays out flex row children from right to
  left, maps `pt_align_start`/`pt_justify_start` to the right edge and applies
  the same rule to wrap, grid and scroll geometry.
- Table and tree columns keep logical order; only the horizontal start edge
  moves. This is an internal per-widget opt-out from the generic row mirror,
  because a sorted data table whose columns flip is not the same table.
- Tabs, sidebars, picker popups, client-side decoration, overlays and menus
  place their chrome from the resolved UI direction. Scrollbars sit on the RTL
  left, tree disclosure chevrons on the right.
- An RTL entry is right-anchored: at `scroll_x = 0` it shows the logical start
  of its text, which is the visual right end, and typing or scrolling moves the
  text leftwards. The 1.4 behavior of anchoring the logical end at the left is
  the intentional correction 2.0 records.
- Start/end alignment resolves against the paragraph direction, including
  multi-paragraph text areas. Center alignment is unchanged.
- Padding and margin stay physical, with no start/end API. A mirrored row flips
  child order and alignment, not the meaning of the four padding values. This
  is the recorded decision for 2.0 rather than an implied guarantee.
- Ellipsis and clipping stay on the visual end. User-supplied images are never
  mirrored automatically.

## Editing, selection and hit testing

The default is **visual caret movement**, decided after testing a real keyboard
in an RTL run:

- Left and Right move to the visually adjacent cluster boundary, which can
  raise or lower the logical byte offset inside an RTL run. A visual cluster
  edge rule keeps the caret from landing mid-cluster.
- Home/End go to the visual start and end of the line. Ctrl+Home/Ctrl+End go to
  the logical document start and end.
- Shift+arrow extends from a fixed logical anchor, so the reported selection is
  always a logical byte range even though the caret moves visually.
- Backspace and Delete stay logical: they remove the preceding or following
  character in the stored string, never the visually adjacent one.
- Up/Down and Page Up/Page Down keep a visual goal x across lines. On a wrapped
  RTL line, Shift+arrow stays within the line's visual order; crossing a soft
  wrap with Shift remains an in-line visual move.
- A mouse click maps visual x to the nearest logical boundary through the bidi
  map with a caret affinity bit. A selection across runs paints one rectangle
  per visual run, not one reversed span.
- The caret rectangle sent to `text-input-v3` and to accessibility clients is
  the visual rectangle at the logical caret. Surrounding text and
  delete-surrounding-text stay logical bytes; preedit is shaped in the
  direction of its insertion context.
- A password entry paints bullets from the stored text direction, so the
  bullets never reveal the content's direction through the visual order.
- An application that needs logical movement can intercept Left, Right, Home
  and End in `pt_window_set_key_callback`, which runs before the focused
  widget, and consume them. That escape hatch is the 2.0 answer; there is no
  ninth direction API and no per-widget caret-mode property.

## Accessibility

- AT-SPI `Text` methods report character offsets in logical order. A screen
  reader reads the logical string, not the visual reordering.
- Every accessible object publishes its resolved UI direction as an
  accessibility attribute (`direction` = `ltr`/`rtl`), and text objects publish
  the paragraph direction through `Text.GetAttributes` and
  `Text.GetDefaultAttributes` with a scalar range covering the visible text.
- The byte/scalar split is deliberate: the C-level
  `pt_widget_get_a11y_text` continues to return **byte** offsets, while the
  D-Bus `Text` interface converts to **scalar** offsets. The older wording in
  this guide that said "accessibility getters use scalar offsets" was wrong for
  the C getter and is corrected here.
- Roles, names and states are unchanged. Direction is presentation metadata; it
  never changes a role or a state.
- Password text remains hidden in every direction. The bridge reports the
  direction and caret but no character, count-safe behavior is unchanged, and
  no bidi path exposes password characters through attributes or events.

## Bindings and examples

All eight functions are wrapped in C++, Python and Java in the same change, and
the parity gate reports 564/564 for every language with an empty allowlist. The
wrappers are ordinary value setters/getters; C++ returns the builder from
setters, Python exposes properties plus `set_*` methods, Java takes the C enum
value as an `int`.

The C gallery carries an RTL screen (`examples/c/rtl_gallery.c`) with a mirrored
row, a mixed Arabic/Hebrew/Latin label, an RTL entry, an RTL text area, a nested
LTR panel and a direction switch. `examples/cpp/rtl_direction.cpp`,
`examples/python/rtl_direction.py` and `examples/java/RtlDirection.java` are
smaller switches that exercise the binding setters and getters.

## Compatibility and rollout

Bidi is additive at the API level but changes pixels for applications that
already put RTL text on screen. That is a rendering correction, not a new layout
contract. 2.0 keeps SONAME `libpt.so.1`: the Makefile tracks `ABI_MAJOR`
separately from `VERSION`, the direction API only adds declarations, and no
existing declaration changes. `tools/release-check.sh` compares the SONAME with
`ABI_MAJOR`, so a behavior-only major cannot be misreported as a binary break.

Two hard gates protect LTR:

- The 17 pure-LTR PNG snapshots in `test_visuals` are byte-identical to their
  1.4 output.
- `tools/ltr-compat.sh` builds 1.4.0 and the 2.0 tree on the same machine and
  compares 11 LTR scenes byte for byte. The release gate requires 11/11 `SAME`.
  No golden hash is checked in, because FreeType, HarfBuzz and the installed
  fonts legitimately differ between machines; building both revisions on one
  machine holds them constant.

No existing enum value changes. The two new enums are appended, and
`pt_ui_direction_inherit` and `pt_text_direction_auto` are the zero values, so a
zero-initialized widget keeps the old behavior. The public byte-offset and
AT-SPI character-offset contracts do not move.

## Performance

The LTR path is the common path and gets a fast path: the adapter classifies the
text from the engine's Unicode bidi types (any R/AL character or bidi format
control needs UBA) rather than from a script guess, and text that cannot reorder
uses the legacy single-run shaping path with zero extra allocations and no
per-frame cost. The classification was swept over 292,531 assigned code points
with 0 mismatches against the engine's own classes.

Bidi analysis is O(n) in UTF-8 bytes per rebuild (layout rebuild, width change,
direction change or text edit), and the run/map allocations are bounded by line
length and released with the text layout. The shape cache is keyed by direction,
so an LTR and an RTL shaping of the same bytes are separate bounded entries.
Reordering and measurement add a small constant per line; the guard suite runs
one mixed-script dense paragraph and one RTL table row alongside the LTR
profiles, and a regression in either direction fails `make perf-check`.

The earlier promise of "no per-frame allocation on an unchanged line" is
withdrawn: 1.4 already rebuilt line layout on every paint, so it was never true.
2.0's contract is the honest one — **bounded per rebuild** — and an unchanged
frame still does no work because painting is damage-driven.

## Known limitations

These are documented behavior, not hidden surprises:

- Table horizontal wheel scrolling needs a platform axis that Wayland's
  `wl_pointer` does not provide in the tested compositors. The table scrolls
  horizontally with its scrollbar and keyboard; a horizontal wheel over a table
  is not a 2.0 feature.
- A Shift+arrow selection that reaches a soft wrap in RTL stays a visual move
  within the line; crossing the wrap with Shift expands by the visual rule, not
  by a document-order run. Multi-line selection is logical once it is set.
- A selection that spans multiple mapped lines collapses toward the logical
  edge at the mapped boundary; the painted rectangles are correct, but a
  programmatic range across lines should be set from logical offsets.
- `pt_text_span` has no direction field and stays frozen. Paragraph direction
  comes from the widget, and an embedded opposite-direction phrase uses Unicode
  isolate controls in the text.
- Arabic and Hebrew need a font that covers them. A missing glyph is a
  documented fallback/tofu result with `pt_app_font_uncovered_count` reporting
  it, not a hard configuration error; install Noto Sans Arabic/Hebrew or DejaVu
  Sans for the examples.
- Bidi control characters can reorder what a reader sees, which is the
  Trojan-Source class of problem. Pt does not sanitize text automatically; an
  application that accepts untrusted text should strip or display controls
  itself. A future optional API may expose the control inventory.

## Test plan

- **UBA conformance.** Unicode 18.0.0 `BidiTest.txt` and
  `BidiCharacterTest.txt` are vendored gzipped under `tests/data/bidi` with the
  Unicode License v3. `test_bidi_conformance` runs 770,241 + 91,707 cases with 0
  failures in `make check` (about 0.5 s) and prints the Unicode version.
- **Engine and byte maps.** `test_bidi` covers paragraph bases, byte maps, X9
  controls kept for shaping but skipped when painted, explicit run directions,
  visual run order, bracket mirroring, Arabic joining, the LTR fast-path
  pre-scan and the shape-cache direction key.
- **Direction API.** `test_direction` covers the eight functions, inheritance
  through widget, ancestor, window and app, explicit getters, zero defaults,
  auto P2/P3 with isolates, neutral fallback and invalid-input rejection.
- **Layout and editing.** `test_rich_text`, `test_text_area` and `test_visuals`
  cover wrapped RTL lines, per-line base alignment, L1 whitespace, ellipsis on
  the visual end, visual caret edges, affinity at soft wraps, multi-rectangle
  selection, IME rectangles and password bullets, plus RTL goldens.
- **Mirroring.** `test_table`, `test_tree`, `test_navigation` and the visual
  suite cover row order, scrollbar side, chevrons, table/tree logical columns
  and nested LTR panels.
- **Accessibility.** `test_a11y` and the live `test_a11y_bridge` cover the
  object-level direction attribute, text-attribute direction and the
  password-with-direction case.
- **LTR compatibility.** The 17 snapshot goldens and `tools/ltr-compat.sh`'s
  11-scene cross-version comparison are release gates, not optional checks.

## Delivered waves

The delivery order in the original plan became the 2.0 workstreams:

1. Direction properties, resolution and auto detection; headless API,
   inheritance and default tests (W1).
2. UBA levels, reordering, mirroring and per-run shaping for labels and
   single-line text, with LTR snapshots unchanged (W0/W2).
3. Wrapped labels and text areas: line breaks, ellipsis, alignment and the
   logical/visual map (W3).
4. Editing: caret, selection painting, hit testing, keyboard movement and IME
   rectangles (W4).
5. UI mirroring: flex/wrap/grid/scroll geometry, table/tree logical columns,
   navigation, pickers, decoration, overlays and paint chrome (W5a/W5b).
6. Accessibility attributes, bindings and examples (W6a/W6b).
7. Conformance, fuzz, performance hardening and the LTR byte-compatibility gate
   (W7).