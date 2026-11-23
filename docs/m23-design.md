# M23 design proposal — 2.1.0 RTL ergonomics and safety

Status: **proposed**, 2026-11-23. This document is the design input for the
M23 implementation waves. It contains no shipped API; every signature below is
a proposal to be reviewed before W1 starts. The delivered record belongs in
[release.md](release.md) once 2.1.0 is tagged.

2.0 delivered the bidi engine, the direction properties and visual editing.
M23 is a minor release that closes four ergonomic gaps that 2.0 documented as
deliberate limitations:

1. caret movement is visual-only, with logical movement available only through
   a window key callback (`rtl.md`, "Editing");
2. the UI direction is never inferred from the process locale (`rtl.md:89`);
3. bidi control characters are not inventoried or sanitized (`rtl.md:299`);
4. padding is physical-only, with no start/end spelling (`rtl.md:159-160`).

The release is **additive**. No existing declaration, enum value, struct
layout, byte offset or default changes. A zero-initialized application keeps
2.0 behavior: visual caret, no locale hint, no sanitization, physical padding.
SONAME stays `libpt.so.1` (`ABI_MAJOR := 1`).

## Baseline projection

The 2.0 baseline is **564 functions / 924 entries** (`func` 564, `enum` 288,
`callback` 43, `struct` 10, `opaque` 12, `define` 6, `type` 1).

| Feature | Functions | Enums (values) | Structs |
| --- | --- | --- | --- |
| 1. Caret movement | 4 | 1 (3) | 0 |
| 2. Locale helper | 3 | 0 | 0 |
| 3. Bidi safety | 2 | 2 (12) | 1 |
| 4. Logical padding | 1 | 0 | 0 |
| **Total** | **10** | **3 (15)** | **1** |

Projected baseline: **574 functions / 950 entries** — 574 `func`, 303 `enum`,
43 `callback`, 11 `struct`, 12 `opaque`, 6 `define`, 1 `type`. All ten
functions carry a `Since 2.1.` marker. No function is removed or changed.

## Feature 1 — caret movement policy

### Problem

2.0's default visual arrows are correct for reading and for ordinary RTL
editing, but an application that edits source code, structured data or
identifiers wants logical arrows. The only 2.0 answer is to intercept
Left/Right/Home/End in `pt_window_set_key_callback` and reproduce the movement
logic. That is a full editor responsibility, not a policy switch.

### Proposed surface

```c
/* Caret movement policy (since 2.1). The zero value inherits: a widget takes
 * the nearest explicit ancestor's value, then the application's, then visual.
 * visual moves Left/Right to the visually adjacent cluster boundary and
 * Home/End to the visual line ends; logical moves Left/Right by UTF-8
 * character boundary and Home/End to the logical line ends. Vertical movement,
 * Shift selection, Backspace/Delete and the IME caret rectangle are unchanged
 * in both modes. */
typedef enum pt_caret_movement {
    pt_caret_movement_inherit = 0,
    pt_caret_movement_visual,
    pt_caret_movement_logical
} pt_caret_movement;

pt_api void pt_app_set_caret_movement(pt_app *app, pt_caret_movement movement);
pt_api pt_caret_movement pt_app_get_caret_movement(const pt_app *app);
pt_api void pt_widget_set_caret_movement(pt_widget *widget, pt_caret_movement movement);
pt_api pt_caret_movement pt_widget_get_caret_movement(const pt_widget *widget);
```

`inherit` is the zero value so a zero-initialized application and widget keep
the 2.0 visual behavior. The resolved default is visual, exactly like
`pt_ui_direction` resolves to LTR.

### Resolution and override scope

The chain is **widget explicit → nearest explicit ancestor → application →
visual**. A window-level override is deliberately not added: setting the
policy on a window's root widget has the same effect for every widget in that
window, and it keeps one fewer inherited level to document and test. This
matches the direction chain minus the window step; the rationale is recorded
here so it is not later mistaken for an omission.

Setters ignore NULL and out-of-range values. Getters return the stored
explicit value (`inherit` when unset). A change does not invalidate layout and
does not repaint: it affects the next key event only.

### Integration points

- `include/pt/base.h`: the enum.
- `include/pt/app.h`, `include/pt/widget.h`: declarations and contract text.
- `src/internal.h`: `uint8_t caret_movement` in `struct pt_app` and
  `struct pt_widget`; `pt_caret_movement pt_widget_resolved_caret_movement(
  const pt_widget *widget);`.
- `src/app.c`: the app setter/getter, mirroring `pt_app_set_ui_direction`
  without the repaint loop.
- `src/widget.c`: the widget setter/getter and the resolver; branch in
  `entry_horizontal`, `entry_line_edge`, `text_area_horizontal` and
  `text_area_line_edge`.
- Logical branches use `pt_utf8_next_boundary`/`pt_utf8_prev_boundary` on the
  stored text (not the password bullet display), `0`/`entry_length` for an
  entry's Home/End and `layout.lines[line].start/end` for a text area's
  Home/End. Selection collapse without Shift uses the logical start/end, which
  is already the fallback in the non-mapped path.
- `text_area_vertical` keeps the visual goal-x rule in both modes; the policy
  governs horizontal movement and line edges only.
- The 2.0 window-callback escape hatch remains valid and now becomes optional
  for applications that only want logical arrows.

### Zero-regression argument

With no setter, both fields are zero (`inherit`), the resolver returns
`pt_caret_movement_visual`, and every existing branch is taken. LTR text has
identical logical and visual cluster order, so both modes produce the same
bytes for pure LTR input. The 17 LTR snapshots and the 11-scene LTR gate are
unaffected because no layout or paint code changes.

### Tests

- `tests/test_direction.c`: NULL/invalid rejection, explicit storage, the
  widget → ancestor → app → visual chain, and the fresh-app/widget `inherit`
  getter.
- `tests/test_editing.c`: an RTL entry with mixed runs; visual and logical
  arrows land on different logical offsets; logical mode matches
  `pt_utf8_prev/next_boundary`; Home/End semantics; Shift still reports a
  logical range; password bullets do not leak the stored text.
- `tests/test_text_area.c`: logical Home/End per wrapped line, Ctrl+Home/End
  unchanged, visual mode unchanged.
- `tests/test_abi.c`: enum values are pinned (inherit 0, visual 1, logical 2).

### Bindings

C++ returns the builder from setters and exposes `caret_movement()`;
Python exposes a property plus `set_caret_movement`; Java takes the C enum as
an `int` and returns an `int`. Four functions, one enum.

## Feature 2 — locale to default UI direction

### Problem

`rtl.md:89` records that 2.0 does not guess the UI direction from the process
locale. The reason was sound — a wrong guess mirrors every window — but an
application that wants the common behavior must currently parse `LC_ALL`,
`LC_MESSAGES` and `LANG` itself and call `pt_app_set_ui_direction`. M23 adds a
bounded, opt-in helper rather than reading the environment implicitly.

### Proposed surface

```c
/* Maps a locale name to a default interface direction (since 2.1). This is a
 * bounded, ICU-free lookup: the language and script subtags of a BCP-47 or
 * POSIX locale are matched against a conservative RTL table; everything else,
 * including NULL, empty, "C" and "POSIX", returns pt_ui_direction_ltr. It never
 * returns inherit and never reads the process environment. Thread-safe: it
 * touches caller-owned data only. */
pt_api pt_ui_direction pt_locale_ui_direction(const char *locale);

/* Stores a locale hint (since 2.1). The string is copied; NULL or an empty
 * string clears the hint. When the application's explicit UI direction is
 * inherit, the hint supplies the application fallback in the resolution chain;
 * an explicit pt_app_set_ui_direction always wins, regardless of call order.
 * A change invalidates layout and repaints only when it changes the resolved
 * application fallback. */
pt_api void pt_app_set_locale(pt_app *app, const char *locale);

/* Returns the stored locale hint, or NULL when none is set (since 2.1). The
 * pointer is owned by the app and stays valid until the next set_locale or
 * pt_app_destroy. */
pt_api const char *pt_app_get_locale(const pt_app *app);
```

The explicit setter wins by storing the two values separately: `ui_direction`
remains the explicit property, and a new `locale_ui_direction` field is the
derived fallback. Resolution becomes widget → nearest explicit ancestor →
window → application explicit → application locale hint → LTR. A caller that
sets the locale first and the direction later, or the reverse, gets the same
result.

### Bounded mapping

Parsing stops at `.` (codeset) and `@` (modifier), splits the remaining tag on
`-` or `_`, lowercases ASCII, and reads the language and optional four-letter
script subtag. At most 64 bytes are examined; no allocation is needed for the
pure helper.

- RTL scripts: `Arab`, `Hebr`, `Thaa`, `Nkoo`, `Syrc`, `Samr`, `Mand`, `Mend`,
  `Adlm`, `Rohg`.
- RTL languages (when no script is given): `ar`, `arc`, `bal`, `bgn`, `ckb`,
  `dv`, `fa`, `glk`, `he`, `iw`, `khw`, `ks`, `lrc`, `mzn`, `nqo`, `prs`,
  `ps`, `rhg`, `sd`, `syr`, `ug`, `ur`, `yi`, `ji`.
- A script subtag wins over the language: `az-Arab`, `ku-Arab`, `pa-Arab` and
  `ms-Arab` are RTL while plain `az`, `ku`, `pa` and `ms` are LTR.
- Everything else is LTR. The table is conservative and documented; a wrong
  entry is corrected in a future minor, never by reading the environment.

No ICU, no locale database, no `setlocale` call and no global state.

### Integration points

- `include/pt/base.h`: `pt_locale_ui_direction` beside the direction enums.
- `include/pt/app.h`: the app setter/getter.
- `src/internal.h`: `char *locale; uint8_t locale_ui_direction;` in
  `struct pt_app`.
- `src/app.c`: copy/clear with the existing string helpers; derive with
  `pt_locale_ui_direction`; free in `pt_app_destroy`; invalidate windows only
  when the derived fallback changes and the app has no explicit direction.
- `src/widget.c`: `pt_widget_resolved_ui_direction` adds the app locale hint
  after the app explicit value and before the LTR default.
- `src/app.c` does not read the environment; the application passes the string
  it already resolved.

### Zero-regression argument

No locale is set by default, so `locale_ui_direction` is 0 and resolution is
unchanged. The pure helper is new code and is not called by any layout, paint
or editing path. An application that sets no direction and no locale keeps the
1.4/2.0 LTR pixels.

### Tests

- `tests/test_direction.c` or a new `tests/test_locale.c`: every listed
  language and script, case-insensitivity, `-`/`_`, codeset/modifier suffixes,
  `C`/`POSIX`/empty/NULL/malformed, and the script-wins cases.
- Precedence in both call orders; window and widget overrides still win;
  clearing with NULL restores LTR; invalid values rejected; destroy is clean
  under ASan.
- A layout/repaint mark check for a hint change under an inherited app
  direction, and no invalidation when an explicit app direction shadows it.

### Bindings

Three functions; Python exposes a `locale` property and a static
`locale_ui_direction` helper; C++ and Java mirror the C surface. The pure
helper is safe to call before `pt_app_create`.

## Feature 3 — bidi control inventory and sanitization

### Problem

Bidi control characters can reorder what a reader sees without changing the
stored bytes — the Trojan-Source class of attack. 2.0 keeps the controls in
the stored string for shaping and skips them when painting (`pt_bidi_is_removed`
also covers BN characters). It does not sanitize, and `rtl.md:296-299` records
that an application accepting untrusted text must handle them itself. M23
provides the missing primitives; it does not change any existing text path.

### Proposed surface

A new public header `include/pt/bidi.h` (included from `pt.h`) owns the
surface. It includes `pt/base.h` and follows the base header contract.

```c
/* The nine explicit directional formatting controls of the Unicode bidi
 * algorithm. BN characters (ZWSP, soft hyphen, ZWJ/ZWNJ, BOM) are not
 * reported here; they are not Trojan-Source controls. */
typedef enum pt_bidi_control_kind {
    pt_bidi_control_none = 0,
    pt_bidi_control_lre,   /* U+202A */
    pt_bidi_control_rle,   /* U+202B */
    pt_bidi_control_pdf,   /* U+202C */
    pt_bidi_control_lro,   /* U+202D */
    pt_bidi_control_rlo,   /* U+202E */
    pt_bidi_control_lri,   /* U+2066 */
    pt_bidi_control_rli,   /* U+2067 */
    pt_bidi_control_fsi,   /* U+2068 */
    pt_bidi_control_pdi    /* U+2069 */
} pt_bidi_control_kind;

typedef struct pt_bidi_control_span {
    uint32_t offset;   /* UTF-8 byte offset in the input */
    uint32_t length;   /* bytes occupied (2 or 3) */
    pt_bidi_control_kind kind;
} pt_bidi_control_span;

/* Reports every explicit control in text in logical order. The return value is
 * the total count; when spans is non-NULL, up to capacity entries are written.
 * A NULL spans or zero capacity counts only. Byte-exact matching keeps the
 * scan bounded and works on invalid UTF-8 without allocating. Thread-safe:
 * caller-owned data only. Since 2.1. */
pt_api size_t pt_bidi_control_inventory(const char *text, size_t length,
                                        pt_bidi_control_span *spans, size_t capacity);

typedef enum pt_bidi_sanitize_mode {
    pt_bidi_sanitize_strip = 0,      /* remove the controls */
    pt_bidi_sanitize_neutralize      /* replace each with U+FFFD */
} pt_bidi_sanitize_mode;

/* Returns a newly allocated NUL-terminated copy with the nine controls removed
 * or replaced by U+FFFD (three bytes each). The caller frees it with pt_free;
 * out_length receives the byte length excluding the NUL. Invalid arguments
 * return NULL; an empty input returns an allocated empty string. Other bytes,
 * including invalid UTF-8, are copied unchanged. This function is never called
 * automatically by a text setter. Thread-safe: caller-owned data only.
 * Since 2.1. */
pt_api char *pt_bidi_sanitize(const char *text, size_t length,
                              pt_bidi_sanitize_mode mode, size_t *out_length);
```

The byte scanner matches the exact UTF-8 encodings: U+202A–U+202E are
`E2 80 AA`–`E2 80 AE`, U+2066–U+2069 are `E2 81 A6`–`E2 81 A9`. This is O(n)
in bytes, allocates nothing for the inventory, and cannot be confused by
invalid continuation bytes. `pt_bidi_sanitize` is O(n) and allocates at most
`3 * length + 1` bytes (worst case all-neutralize); the allocation-failure
rule is Pt's usual fatal diagnostic.

### Security semantics

- `LRE`/`RLE`/`LRO`/`RLO`/`PDF` are the legacy embedding/override controls;
  `LRI`/`RLI`/`FSI`/`PDI` are the isolate controls. All nine can change the
  visual order of surrounding text without changing the bytes.
- `pt_bidi_control_inventory` is an audit primitive: an editor can highlight,
  count or reject untrusted input before display, logging or source control.
- `pt_bidi_sanitize` is a mitigation primitive: `strip` removes the controls,
  `neutralize` leaves a visible U+FFFD in their place so a reader can see that
  something was removed. Neither validates the rest of the UTF-8.
- The existing `pt_bidi_is_removed` stays internal and keeps its broader X9
  meaning (BN plus explicit controls) for shaping and painting. The public
  inventory deliberately reports only the nine explicit controls; documenting
  the difference prevents an application from assuming the inventory is a
  paint-skip predicate.
- Nothing in `pt_widget_set_text`, the entry, text area, clipboard, drag or
  accessibility paths calls the sanitizer. Stored bytes and all byte offsets
  remain exactly as the application supplied them.

### Integration points

- `include/pt/bidi.h` (new), `include/pt/pt.h` (umbrella include),
  `src/bidi.c` (implementation), `tools/gen-api-reference.py`
  (`MODULE_SUMMARIES["bidi.h"]`).
- No change to `src/bidi.c`'s engine adapter, `pt_bidi_is_removed`, shaping,
  layout or painting.
- `tests/test_bidi.c`: the nine controls in mixed text, offsets/lengths,
  capacity truncation and count-only mode, invalid UTF-8, strip/neutralize
  lengths and content, empty/NULL/invalid-mode cases, and an assertion that
  `pt_widget_set_text` stores the controls unchanged.

### Zero-regression argument

All new code; no existing path calls it. The default `pt_bidi_sanitize_strip`
value is the zero value for callers that zero-initialize the mode. The LTR fast
path, conformance suite and paint output are untouched.

### Bindings

Two functions and two enums. C++ returns `std::vector<pt_bidi_control_span>`
for the inventory and `std::string` for sanitize; Python returns a list of
`(kind, offset, length)` tuples and a `str`; Java returns an array of a small
record-like holder and a `String`. The parity scanner checks the C names; the
wrappers must not allocate hidden global state.

## Feature 4 — logical start/end padding

### Problem

2.0 keeps padding and margin physical by decision (`rtl.md:159-160`). A
mirrored row flips child order and alignment but not the meaning of the four
padding values. An RTL card that wants 16 px at the start and 8 px at the end
must know the resolved direction and swap the values itself — and re-swap when
an ancestor direction changes.

### Proposed surface

```c
/* Sets padding in logical order (since 2.1). top and bottom are physical;
 * start is the edge where content begins in the resolved UI direction and end
 * is the opposite edge. In LTR start maps to left and end to right; in RTL
 * start maps to right and end to left. The mapping is re-evaluated on every
 * layout, so a later direction change (including one inherited from an
 * ancestor, window or application) is honoured without another call.
 *
 * pt_widget_set_padding remains physical. The two setters share the same four
 * edges: the last call wins. A physical call clears the logical mode; a
 * logical call sets it. Since 2.1. */
pt_api void pt_widget_set_padding_logical(pt_widget *widget, float top, float start,
                                          float bottom, float end);
```

No getter is proposed: `pt_widget_set_padding` has no getter either, and the
resolved physical values are internal layout state, not a public query.

### Integration points

- `include/pt/widget.h`: declaration beside `pt_widget_set_padding`.
- `src/internal.h`: `float padding_start; float padding_end; bool
  padding_logical;` in `struct pt_widget`.
- `src/widget.c`: `pt_widget_set_padding_logical` writes top/bottom and
  start/end, sets `padding_logical`, and marks layout dirty.
  `pt_widget_set_padding` clears `padding_logical` and writes the four physical
  fields (last call wins).
- `src/layout.c`: a side-effect-bounded prepass
  `pt_widget_resolve_logical_padding(pt_widget *root)` walks the subtree,
  computes the resolved UI direction once per widget, and writes
  `padding_left`/`padding_right` from start/end. It runs at the entry of
  `pt_widget_layout`, `pt_widget_layout_child`, `pt_widget_measure` and
  `pt_widget_measure_child`, so a standalone measure sees the same geometry as
  a layout. The prepass allocates nothing and returns immediately for a
  subtree with no logical padding.
- `src/layout.c` and `src/widget.c` keep reading `padding_left`/`padding_right`
  as before; no call site changes.
- Paint runs after layout and therefore sees the resolved physical values.
  A damage-only repaint does not change padding.

### Zero-regression argument

`padding_logical` is false in every existing widget (zero-initialized), so the
prepass performs no writes and the physical setters behave exactly as before.
The four physical values keep their 2.0 meaning. A pure LTR application that
never calls the new setter produces byte-identical layout, pixels and
performance; the LTR snapshot and cross-version gates are unchanged.

### Tests

- `tests/test_layout.c` or `tests/test_flex_grid.c`: LTR start→left/end→right,
  RTL start→right/end→left, a nested LTR widget inside an RTL parent, a
  direction change after the setter re-resolves on the next layout, measure and
  layout agree, and last-call-wins between physical and logical setters.
- `tests/test_visuals.c`: an RTL card golden with asymmetric logical padding;
  the existing 17 LTR snapshots stay byte-identical.
- `tests/test_abi.c` is not affected (internal fields only).

### Bindings

One function; C++ returns the builder, Python a `set_padding_logical` method,
Java a `void` method. No new enum or struct.

## Cross-cutting rules

- **Naming.** Public functions are `pt_<domain>_<verb>_<noun>`; enums are
  `pt_<domain>_<noun>` with `_<value>` constants; zero is inherit/default.
- **Errors.** Setters are no-ops for NULL, wrong widget kind or an out-of-range
  enum. Getters clamp and return a default. The inventory returns 0 for invalid
  input; the sanitizer returns NULL only for invalid arguments. Allocation
  failure is fatal, as everywhere in Pt.
- **Strings.** `pt_app_set_locale` copies; `pt_app_get_locale` borrows until
  the next set or destroy; `pt_bidi_sanitize` returns caller-owned memory freed
  with `pt_free`.
- **Threading.** The two new pure helpers (`pt_locale_ui_direction`,
  `pt_bidi_control_inventory`) and `pt_bidi_sanitize` touch caller-owned data
  only and are added to the base-header thread exception list; everything else
  stays UI-thread-only.
- **API baseline.** `make api-update` is run once per wave after its headers,
  implementation, wrappers and tests are complete. `api/pt.api` and
  `docs/api-reference.md` are regenerated, never hand-edited.
- **ABI.** Ten added declarations, three enums, one struct; no removal or
  signature change. `ABI_MAJOR` stays 1 and SONAME stays `libpt.so.1`. The
  struct is a new public struct (not an extension of an existing one), so no
  existing layout moves.

## Waves

The waves are sequential branches merged into `m23/integration`. Sequential
order avoids three-way conflicts in `bindings/` (one file per language) and in
`widget.c`/`internal.h`, and each wave can run the full `make check` gate with
its own wrappers and baseline update. W2 is self-contained enough to run in
parallel with W1 if the channel budget allows, but its binding wrappers land
after W1's to keep the merge clean.

| Wave | Scope | Files owned | Exit gate |
| --- | --- | --- | --- |
| **W1** | Caret movement policy + locale helper (4 + 3 functions, 1 enum) | `include/pt/base.h`, `include/pt/app.h`, `include/pt/widget.h`, `src/app.c`, `src/widget.c`, `src/internal.h`, three bindings, `tests/test_direction.c`, `tests/test_editing.c`, `tests/test_text_area.c`, `tests/test_abi.c`, `api/pt.api`, `docs/api-reference.md` | `make check`, `make check-bindings`, 571/934 baseline with full parity, LTR snapshots unchanged |
| **W2** | Bidi control inventory + sanitizer (2 functions, 2 enums, 1 struct) | `include/pt/bidi.h` (new), `include/pt/pt.h`, `src/bidi.c`, `tools/gen-api-reference.py`, three bindings, `tests/test_bidi.c`, `api/pt.api`, `docs/api-reference.md` | `make check` (conformance still 770,241 + 91,707), sanitizer tests, ASan clean; 573/949 cumulative |
| **W3** | Logical start/end padding (1 function) | `include/pt/widget.h`, `src/widget.c`, `src/internal.h`, `src/layout.c`, three bindings, `tests/test_layout.c`, `tests/test_flex_grid.c`, `tests/test_visuals.c`, `api/pt.api`, `docs/api-reference.md` | `make check`, `make perf-check`, 17 LTR snapshots and 11/11 LTR gate unchanged; 574/950 cumulative |
| **W4** | Public guides, examples, generated reference | `docs/rtl.md`, `docs/zh-CN/rtl.md`, `docs/api-reference.md` (regen only), `examples/c/rtl_gallery.c`, `examples/cpp/rtl_direction.cpp`, `examples/python/rtl_direction.py`, `examples/java/RtlDirection.java`, `Makefile` example targets, `docs/m23-design.md` (status) | `tools/check-bilingual-docs.py`, `tools/gen-api-reference.py --check`, four-language example smoke |
| **W5** | Hardening, 2.1.0 landing | `include/pt/base.h` (version macros), `Makefile` (`VERSION`), `README.md`, `docs/release.md`, `docs/ROADMAP.md`, `docs/standards.md` (if needed) | `werror=1`, `make check`, `check-bindings`, `perf-check`, sanitizers, staged install, reproducible tarball, four live suites, `tools/ci.sh`, `tools/release-check.sh --full`, tag `2.1.0` |

W4 and W5 are documentation/landing waves and must not add behavior. W5 is the
only wave that changes the version and the release record; the tag itself is
the owner's step, as in 2.0.

### File-domain notes

- `include/pt/base.h` is touched by W1 (caret enum, locale helper) and W5
  (version macros), never by W2/W3.
- `include/pt/widget.h`, `src/widget.c` and `src/internal.h` are touched by W1
  and W3; they are sequential.
- `bindings/` files are touched by W1, W2 and W3; sequential order and a final
  parity run keep them conflict-free.
- `api/pt.api` and `docs/api-reference.md` are generated; each wave updates
  them after its own header change, and W4 re-runs the check.
- `tools/gen-api-reference.py` is touched only by W2 (new module summary) and
  W5 (no change expected).

## Budget and review

DuMate is exhausted and is not a dependency. The plan assumes the free
opencode channel as the main implementer, Lingxi GLM-5.3-Flash as a secondary
implementer/reviewer, and the small providers (WB-CN, CodeArts, Trae, Lobster)
for bounded independent checks. Suggested allocation:

- **W1 — 30%.** Code-heavy and the largest surface; main channel. Bounded
  review of the resolver and the editing branches by one small provider.
- **W2 — 15%.** Self-contained pure functions; Lingxi or main. Review the byte
  scanner and the 3× bound; CodeArts/WB-CN can reproduce `make check`.
- **W3 — 20%.** Layout-sensitive; main channel. Review the prepass call sites
  and the LTR gate; Trae/Lobster can review the geometry tests.
- **W4 — 20%.** Documentation and examples; main channel for the English guide,
  a small provider for the Chinese pair review, and a bounded example smoke.
- **W5 — 15%.** Final gate; main channel plus independent reproduction on a
  second provider. No new code in this wave beyond the version bump.

A wave that cannot finish its exit gate is split (for example W1a app/widget
storage + resolver, W1b editing branches + tests) rather than merged partially.
No wave may lower a 2.0 gate (LTR 17 snapshots, 11/11 cross-version, Unicode
conformance, 564/564 parity) to make room for a 2.1 feature.

## Completion criteria for 2.1.0

- **574 functions / 950 entries** in `api/pt.api`; all 574 have a since marker
  and are covered by the generated reference.
- 574/574 binding parity in C++, Python and Java with an empty allowlist.
- Caret movement defaults to visual; locale is unset; padding is physical;
  sanitization is opt-in. LTR output is byte-identical to 2.0 (17 snapshots and
  11/11 `ltr-compat.sh`), and the Unicode conformance counts are unchanged.
- `pt_bidi_control_inventory` reports all nine explicit controls with byte
  offsets and lengths; `pt_bidi_sanitize` strips or neutralizes them and is
  never called by a text setter.
- `rtl.md` and its Chinese pair describe the four features and remove the
  corresponding "future API" limitations; `check-bilingual-docs.py` is green.
- `make check`, `make check-bindings`, `make perf-check`, sanitizer runs, the
  staged install, the reproducible tarball, the four live suites and
  `tools/ci.sh` pass on the 2.1.0 tree; SONAME stays `libpt.so.1`.
- The release record is published in `release.md`, the ROADMAP M23 section is
  closed, and the annotated `2.1.0` tag is created and pushed by the owner.

## Open questions for review

1. **Window-level caret movement.** This proposal omits it in favour of a
   root-widget override. If review prefers exact symmetry with
   `pt_ui_direction`, adding the window pair costs two functions and one field;
   the baseline becomes 576 / 952.
2. **Neutralize replacement.** U+FFFD is visible and length-changing. A
   zero-width replacement (for example U+2060 WORD JOINER) would preserve
   columns but hide the removal; the security argument favours U+FFFD.
3. **Locale table ownership.** The list is deliberately small and CLDR-derived.
   A future release can extend it without an ABI change; the pure helper's
   return contract is stable.
4. **Logical padding and table/tree internal containers.** The prepass applies
   to every widget, including the table/tree column containers that opt out of
   row mirroring. Their padding is internal and currently symmetric, so the
   observable result is unchanged; W3 should add a regression test.
