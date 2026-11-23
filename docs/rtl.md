# Right-to-left and bidirectional text

English | [简体中文](zh-CN/rtl.md)

Status: delivered in 2.0.0, extended in 2.1.0. This document is the design
contract that 2.0 ships plus the 2.1 ergonomics and safety additions, not a
post-1.0 plan. Every addition is additive, the text model and the offset
contracts are unchanged, and a pure LTR application keeps the 1.4 pixels byte
for byte. Every section below states what landed, the decisions that were
taken while building it, and the limitations that remain. The 2.1 sections are
marked as such; [m23-design.md](m23-design.md) records their design input and
[release.md](release.md) the release.

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
`pt_widget_set_ui_direction(editor, pt_ui_direction_ltr)`. Pt does not guess
the UI direction from the process environment, because a wrong guess would
mirror every window. 2.1 offers an explicit hint instead: the application reads
the locale it already resolved, the pure helper maps it to a direction, and the
result joins the resolution chain as the application fallback. Auto text
direction is safe because it applies per paragraph and does not move UI
chrome.

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
- Padding can be set in either order. `pt_widget_set_padding` stays physical
  and is the default; `pt_widget_set_padding_logical` (since 2.1) takes
  top/start/bottom/end and resolves start/end against the widget's UI
  direction on every layout, so an RTL widget gets start on the right and a
  later direction change re-resolves without another call. The two setters
  share the same four edges and the last call wins. Margin is still physical
  and has no logical setter: a mirrored row flips child order and alignment,
  not the meaning of the four margin values.
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
- Logical movement is a policy, not an interception. 2.1 adds
  `pt_caret_movement` (see the section below), so an application that wants
  logical arrows sets it and an application that needs something else — a
  custom motion, extra keys — can still intercept Left, Right, Home and End in
  `pt_window_set_key_callback`, which runs before the focused widget. The
  callback still wins over the policy.

## Caret movement policy (2.1)

2.0 chose visual movement by default and left logical movement to the window key
callback. 2.1 keeps the default and makes the alternative declarative:

```c
typedef enum pt_caret_movement {
    pt_caret_movement_inherit = 0,
    pt_caret_movement_visual,
    pt_caret_movement_logical
} pt_caret_movement;

void pt_app_set_caret_movement(pt_app *app, pt_caret_movement movement);
pt_caret_movement pt_app_get_caret_movement(const pt_app *app);
void pt_window_set_caret_movement(pt_window *window, pt_caret_movement movement);
pt_caret_movement pt_window_get_caret_movement(const pt_window *window);
void pt_widget_set_caret_movement(pt_widget *widget, pt_caret_movement movement);
pt_caret_movement pt_widget_get_caret_movement(const pt_widget *widget);
```

Resolution is the widget's explicit value, then the nearest explicit ancestor,
then the window, then the application, then **visual**. `inherit` is the zero
value, so a zero-initialized application and widget keep 2.0 behavior and the
default is the same visual editing described above.

- **Visual** (default) is exactly the 2.0 movement.
- **Logical** moves Left/Right by UTF-8 character boundary in the stored
  string and Home/End to the logical line ends. It applies to entries and text
  areas only.
- **Unchanged in both modes:** Up/Down/Page Up/Page Down keep their visual goal
  x, Shift+arrow keeps a fixed logical anchor and reports a logical byte range,
  Backspace/Delete stay logical, clicks go through the bidi map, and the IME
  and accessibility caret rectangles stay visual.
- A password entry derives its bullet direction from the stored text in both
  modes, so the policy never leaks the content's direction.
- Setters ignore NULL and out-of-range values; getters return the stored
  explicit value. A change does not invalidate layout and does not repaint: it
  affects the next key event only.

```c
/* A source editor inside an RTL shell: logical arrows for the editor, the
 * visual default everywhere else. */
pt_app_set_locale(app, "he_IL.UTF-8");      /* application fallback: RTL */
pt_widget_set_caret_movement(editor, pt_caret_movement_logical);
```

## Locale hint (2.1)

`pt_locale_ui_direction(const char *locale)` maps a BCP-47 or POSIX locale name
to a default interface direction. It is a bounded, ICU-free lookup: parsing
stops at `.` (codeset) and `@` (modifier), the remaining tag splits on `-` or
`_`, ASCII is lowercased, and at most 64 bytes are examined. The language and
the optional four-letter script subtag are matched against a conservative
table.

- RTL scripts: `Arab`, `Hebr`, `Thaa`, `Nkoo`, `Syrc`, `Samr`, `Mand`, `Mend`,
  `Adlm`, `Rohg`.
- RTL languages when no script is given: `ar`, `arc`, `bal`, `bgn`, `ckb`,
  `dv`, `fa`, `glk`, `he`, `iw`, `khw`, `ks`, `lrc`, `mzn`, `nqo`, `prs`,
  `ps`, `rhg`, `sd`, `syr`, `ug`, `ur`, `yi`, `ji`.
- A script subtag wins over the language: `az-Arab`, `ku-Arab`, `pa-Arab` and
  `ms-Arab` are RTL while plain `az`, `ku`, `pa` and `ms` are LTR.
- Everything else — including NULL, empty, `C` and `POSIX` — is LTR.

`pt_app_set_locale` copies the string as a hint and `pt_app_get_locale` borrows
it until the next set or `pt_app_destroy`; NULL or an empty string clears the
hint. The resolution chain becomes widget → nearest explicit ancestor → window
→ **application explicit → application locale hint** → LTR, so an explicit
`pt_app_set_ui_direction` always wins regardless of call order. The helper is
thread-safe and never reads the environment: the application passes the locale
it already resolved. An unset locale resolves to LTR, exactly as in 2.0.

```c
pt_app_set_locale(app, "ar_EG.UTF-8");        /* app fallback RTL */
pt_ui_direction guess = pt_locale_ui_direction("az-Arab"); /* rtl: script wins */
```

## Bidi control safety (2.1)

Bidi control characters can reorder what a reader sees without changing the
stored bytes — the Trojan-Source class of attack (CVE-2021-42574). Pt keeps the
controls in the stored string for shaping and skips them when painting; it never
removes them silently. 2.1 adds the primitives an application needs, in the new
public header `include/pt/bidi.h`:

```c
typedef enum pt_bidi_control_kind {
    pt_bidi_control_none = 0,
    pt_bidi_control_lre,  pt_bidi_control_rle,  pt_bidi_control_pdf,
    pt_bidi_control_lro,  pt_bidi_control_rlo,  pt_bidi_control_lri,
    pt_bidi_control_rli,  pt_bidi_control_fsi,  pt_bidi_control_pdi
} pt_bidi_control_kind;

typedef struct pt_bidi_control_span {
    size_t offset;                    /* UTF-8 byte offset in the input */
    size_t length;                    /* bytes occupied (2 or 3) */
    pt_bidi_control_kind kind;
    uint32_t codepoint;
} pt_bidi_control_span;

typedef enum pt_bidi_sanitize_mode {
    pt_bidi_sanitize_strip = 0,       /* remove the controls */
    pt_bidi_sanitize_neutralize       /* replace each with U+FFFD */
} pt_bidi_sanitize_mode;

size_t pt_bidi_control_inventory(const char *text, size_t length,
                                 pt_bidi_control_span *spans, size_t capacity);
char *pt_bidi_sanitize(const char *text, size_t length,
                       pt_bidi_sanitize_mode mode, size_t *out_length);
```

- The inventory reports the **nine explicit directional formatting controls**:
  U+202A–U+202E (`LRE`, `RLE`, `PDF`, `LRO`, `RLO`) and U+2066–U+2069 (`LRI`,
  `RLI`, `FSI`, `PDI`). Matching is byte-exact, so it works on invalid UTF-8,
  allocates nothing and cannot be confused by continuation bytes. The return
  value is always the total count; when `spans` is non-NULL, up to `capacity`
  entries are written in logical order, so a count-only call sizes the buffer
  first.
- The sanitizer returns a newly allocated NUL-terminated copy that the caller
  frees with `pt_free`; `out_length` receives the byte length. `strip` removes
  the controls, `neutralize` replaces each with U+FFFD so a reader can see that
  something was removed. The worst case is `3 * length + 1` bytes. All other
  bytes, including invalid UTF-8, are copied unchanged.
- Both are **opt-in**. No text setter, entry, text area, clipboard, drag or
  accessibility path calls them, so stored bytes and every byte offset stay
  exactly as the application supplied them. An application that accepts
  untrusted text — a chat input, a log viewer, a file preview, anything that
  lands in source control — should inventory or sanitize before display,
  logging or storage.
- The public inventory is deliberately narrower than the internal
  `pt_bidi_is_removed`, which keeps its broader X9 meaning (explicit controls
  plus BN characters such as ZWSP, soft hyphen, ZWJ/ZWNJ and BOM) as the paint
  and shaping predicate. Zero-width BN characters are ordinary text to the
  public API and are not Trojan-Source controls.
- `pt_bidi_sanitize` is a mitigation, not a validator: it does not check the
  rest of the UTF-8 and it does not promise that the remaining text is safe.

```c
/* Untrusted text from the network or a file. */
size_t clean_length = 0;
char *shown = pt_bidi_sanitize(untrusted, untrusted_length,
                               pt_bidi_sanitize_neutralize, &clean_length);
if (shown) {
    pt_widget_set_text(preview, shown);   /* U+FFFD marks every removed control */
    pt_free(shown);
}
/* Audit instead of rewrite: count first, then decide. */
size_t controls = pt_bidi_control_inventory(untrusted, untrusted_length, NULL, 0);
```

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

The 2.0 direction functions are wrapped in C++, Python and Java, and the 2.1
surface is wrapped in the same change: the parity gate reports **576/576** for
every language with an empty allowlist. The wrappers are ordinary value
setters/getters; C++ returns the builder from setters, Python exposes properties
plus `set_*` methods, Java takes the C enum value as an `int`. The pure
`pt_locale_ui_direction` helper, `pt_bidi_control_inventory` and
`pt_bidi_sanitize` are exposed per language's idiom: C++ returns a
`std::vector<pt_bidi_control_span>` and a `std::string`, Python a list of spans
and a `str`, Java a `BidiControlSpan` array and a `String`.

The C gallery carries an RTL screen (`examples/c/rtl_gallery.c`) with a mirrored
row, a mixed Arabic/Hebrew/Latin label, an RTL entry, an RTL text area, a nested
LTR panel and a direction switch. `examples/cpp/rtl_direction.cpp`,
`examples/python/rtl_direction.py` and `examples/java/RtlDirection.java` are
smaller switches that exercise the binding setters and getters. The 2.1
ergonomics gallery (`examples/c/ergonomics_gallery.c`) switches the caret
policy, sets a locale hint, pads a card with logical start/end and sanitizes an
untrusted string, with smaller per-binding notebooks in
`examples/cpp/ergonomics.cpp`, `examples/python/ergonomics.py` and
`examples/java/Ergonomics.java`.

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
  Trojan-Source class of problem. 2.1 exposes `pt_bidi_control_inventory` and
  `pt_bidi_sanitize` for it, but sanitization stays opt-in: no text path calls
  either function, so an application that accepts untrusted text still has to
  use them itself before display, logging or storage.
- The locale table is small and CLDR-derived by design. A missing language or
  script resolves to LTR, and the hint selects a default UI direction only; it
  does not localize text, dates, numbers, pickers or the colour field.

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
- **2.1 ergonomics and safety.** `test_direction` covers the caret policy
  chain, the locale table, the script-wins cases and the app precedence in both
  call orders; `test_editing` and `test_text_area` assert visual and logical
  movement on the same RTL text and keep every 2.0 assertion; `test_bidi` covers
  the nine controls, offsets, capacity truncation, invalid UTF-8, strip and
  neutralize, and that `pt_widget_set_text` stores controls unchanged; the
  logical-padding cases in the layout, flex/grid and visual suites cover LTR
  start→left, RTL start→right, a direction change after the setter,
  measure/layout agreement, last-call-wins and the table/tree opt-out.

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

The 2.1 ergonomics and safety workstreams are recorded in
[m23-design.md](m23-design.md) and the [ROADMAP](ROADMAP.md):

1. Caret movement policy and the locale hint (W1).
2. Bidi control inventory and sanitization in a new `pt/bidi.h` (W2).
3. Logical start/end padding with a layout prepass (W3).
4. This guide, its Chinese pair, the four-language examples and the generated
   reference (W4).