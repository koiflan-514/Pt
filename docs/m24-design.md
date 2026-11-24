# M24 - 2.2.0 design: documented RTL editing limitations

Status: **delivered** (2.2.0, tag-ready on `m24/w3-gate`, 2026-11-24). The
three editing limitations this document proved feasible are implemented in W1
(`7b47558`) and W2 (`56bbd95`); W3 closed the implementation-review
observations (see [Implementation review decisions](#implementation-review-decisions)).
This document is kept as the feasibility and scope record it was written as:
what was read, the verdicts, the waves and the evidence each wave had to
re-confirm before it changed behavior.

Baseline on this tree: `api/pt.api` reads **576 functions / 952 entries**,
`ABI_MAJOR = 1`, SONAME `libpt.so.1`, version 2.1.0. M24 targets 2.2.0 with the
same ABI major. All three candidates below are intended as **zero-new-API
behavior refinements**; if none of them requires a public symbol, the M24
baseline stays 576/952.

## Residual limitations, as recorded

From `docs/rtl.md` (lines 451-460) at 2.1.0:

1. **Table horizontal wheel** - "needs a platform axis that Wayland's
   `wl_pointer` does not provide in the tested compositors"; the table scrolls
   with its scrollbar and keyboard.
2. **Shift+arrow across a soft wrap in RTL** - "stays a visual move within the
   line; crossing the wrap with Shift expands by the visual rule, not by a
   document-order run."
3. **Multi-line mapped selection** - "collapses toward the logical edge at the
   mapped boundary; the painted rectangles are correct, but a programmatic
   range across lines should be set from logical offsets."
4. `pt_text_span` has no direction field and is frozen (ABI decision).
5. Missing Arabic/Hebrew glyphs are a documented tofu fallback reported by
   `pt_app_font_uncovered_count` (not a defect).
6. Bidi sanitization is opt-in (deliberate 2.1 policy).
7. The locale table is small by design.

Only items 1-3 are editing behavior. Items 4-7 are explicit product/ABI
decisions and stay out of M24 (see "Out of scope" at the end).

## Current code, read for this evaluation

| Area | File / function | Observed behavior |
| --- | --- | --- |
| Horizontal caret | `src/widget.c` `text_area_horizontal` (4183-4261) | The `mapped && !shift` **no-selection** branch steps with `visual_caret_step` and, on a visual-line edge of an LTR-base line, crosses the soft wrap through the line break (4221-4232). The `mapped && shift` branch (4233-4241) only calls `visual_caret_step`; when it returns false the caret is left where it is. RTL-base lines never cross even without Shift (4224 keeps the "document edge"). |
| Selection collapse after a plain arrow | `text_area_horizontal` 4196-4213 | Single mapped line: collapses to the visual start/end of the selection. Multi-line: "logical edge" (`right ? end : start`) - the logical top of the range, not the visual one. |
| Vertical caret | `text_area_vertical` (4085-4138) | Keeps a visual `caret_goal_x`, picks the affinity nearest the goal, then pins the landing to the target line with `pt_text_layout_bias_for_line`. This is the existing building block a cross-line horizontal move can reuse. |
| Line/affinity helpers | `src/text_layout.c` 283-328 | `pt_text_layout_line_for_position_bias` resolves a shared soft-wrap boundary by affinity; `pt_text_layout_bias_for_line` forces a landing onto a given line(s). |
| Cursor set | `src/widget.c` `entry_set_cursor` (255-263) | Stores the byte offset, clears `caret_goal_x` and **resets `caret_affinity` to leading**; `extend` keeps the anchor. Callers re-assign `area->caret_affinity` after it. |
| Selection painting | `src/paint.c` 975-1035 | The painted range is always the logical min/max of `selection_anchor`/`cursor`, split per layout line; mapped lines paint `paint_map_range` and full lines a full-width rect. Painting is already consistent with a logical range. |
| Wheel input | `src/wayland/platform.c` `pointer_axis` (431-454) | `if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL ... ) return;` - the horizontal axis is **discarded**, and the vertical delta is routed as a single `delta` into `pt_window_input_axis`. |
| Wheel dispatch | `src/window.c` `pt_window_input_axis` (1738-1758) | Single scalar `delta`; overlay first (`src/overlay.c` `pt_window_overlays_axis` 1659-1675), then the window `pointer_callback` axis event (only `delta_y` is set), then `pt_widget_wheel(hit, delta)`. |
| Widget scroll | `src/widget.c` `pt_widget_scroll_pixels` (5020-5078) | Walks parents; a table hits `pt_table_scroll` (vertical only) and `continue`s to its parent when the table does not move. |
| Table horizontal scroll | `src/table.c` `pt_table_scroll_horizontal` (1542-1558) | Public in `pt/table.h`, clamps to `table_content_width - viewport`, marks dirty. **Zero callers in `src/`, `tests/`, `examples/`, `bindings/`, `tools/`**. |
| Pointer event ABI | `include/pt/window.h` 198-219 | `pt_pointer_event` already carries `float delta_x`; only motion fills it (`src/window.c` 1467-1476). No ABI change is needed for a horizontal axis. |

## Feasibility verdicts

### 1. Shift+arrow across a soft wrap in RTL - feasible, medium risk

**Mechanism.** `text_area_horizontal` already computes the caret's line with
`pt_text_layout_line_for_position_bias` and already owns the visual-edge
column-mapping code for the no-Shift case. The missing piece is only the
*forward* step out of a horizontal move reached a visual line edge: the hit
test `visual_caret_step(...)` returned false while the caret is at a *visual*
edge (not a document edge). For a mapped line, `visual_line_edge(map, left,
...)` returns the byte the caret renders at, in the requested direction. At a
mapped line's visual left edge and an RTL-base line, that byte is the line's
logical start (the right-most character); at a mapped line's visual right edge,
it is the line's logical end (the left-most character). Hence the existing
affinity check is enough to distinguish a visual line edge from a document edge:
**no new helper is strictly required.** A move to the adjacent line then reuses
the `caret_goal_x` + `pt_text_layout_position_for_x` + affinity-nearest-x
pattern from `text_area_vertical`, with a column from the current line and a
landing on the next/previous line.

Spec (logical content is stable, only the caret's visual journey changes):

- Which line comes next: **physical** line order (`line ± 1`), as for LTR. This
  is the default `pt_caret_movement_visual`; `pt_caret_movement_logical` keeps
  the current logical run and is untouched.
- Horizontal direction moves by the visual rule **within** the line
  (`visual_caret_step`) and, when that step fails at a visual line edge, crosses
  to the next/previous physical line by the same rule: the caret enters the next
  row at the column nearest the current x. Cross-line moves do not invent a
  "goal x" column jump; a later Left/Right after the row change re-derives the
  column from the new line.
- **Shift is a property of the anchor, not of the step.** The existing
  `entry_set_cursor(area, position, shift)` + final `area->caret_affinity = bias`
  already implement that; the new branch must assign the bias like
  `text_area_line_edge` does. The selection range stays logical min/max.
- If `visual_caret_step` fails and the caret is at a *document* edge, the move
  still does nothing (as today): an RTL paragraph's logical start is the
  document-start stop for Home, but Left/Right from it must not wrap into an
  LTR neighbour that has no visual continuation. (Settles the "RTL document
  edge" caveat: the no-wrap rule stays for a document edge, the wrap is added
  for a *soft-wrap* visual edge.)
- If the current line is not mapped (no bidi content), do not change anything:
  the LTR `!mapped` branch (4242-4253) is byte-identical.

**Risk.** The step must not fire twice in one press: today the RTL visual line's
*left* edge is also the line's logical start, and the 2.1 no-Shift path treats
it as the document edge. The new branch therefore has to occur *before* the
`visual_caret_step` call (or replace the `else if` chain in that branch), not
after it, and must pass through `pt_text_layout_bias_for_line` for the landing.
Main risk is confusing the visual edge with a document edge on mixed
bidirectional single-line paragraphs: gate the cross-line path on a **soft-wrap
boundary** (`layout.lines[line].end == layout.lines[line+1].start` for
next/prev) and on `line ± 1` being in range, not on "no document offset".

**Regression argument.** LTR text-only and logical caret movement: path
untouched. LTR-mapped text: no Shift branch is byte-identical only if the new
branch is gated on `text_area_line_is_mapped` + a visual *soft-wrap* edge; a
single-line LTR text area has `line_count = 1`, so no cross-line candidate. RTL
mapped Shift currently does nothing at a wrap; after the change it crosses one
row per press, which is the documented-intent fix, not an LTR change.

**Tests.** Extend `tests/test_text_area.c` `test_soft_wrap_affinity` /
`test_rtl_editing` with: (a) placed caret at the visual wrap edge, Shift+Left
crosses to the previous line and the selection is the logical range covering
that edge; (b) the same with Shift+Right; (c) LTR pair that keeps
`selection_anchor` and `cursor` unchanged; (d) logical mode stays on the line
and grows the logical run. Check the caret's affinity lands on the reported
line via `pt_widget_ime_rect` y.

### 2. Multi-line mapped selection collapse - feasible, low risk, no painting change

**Mechanism.** The collapse code (4196-4213) already has a mapped single-line
branch. The multi-line `else` is a logical fallback that assumes visual line
order equals logical line order, which is true for the wrapped *lines* but not
for the *edges* of a mapped range. The visual start/end of a multi-line
selection are, respectively, the visual start of the start line and the visual
end of the end line (top-to-bottom visual order of wrapped lines), and both are
already computed by `visual_line_edge(layout.maps[i], ...)`.

Spec: plain Left (no Shift) on a multi-line selection collapses to the
selection's **visual** left edge, Right to its **visual** right edge; the
logical stored range is unchanged and painting is unchanged. For each endpoint
line, use the visual edge byte (`visual_line_edge`) with the existing leading
affinity. This is the same byte `text_area_line_edge` uses for Home/End on a
mapped line, so Home/End and collapse stay consistent.

- LTR (unmapped) multi-line selections: untouched - only the mapped
  multi-line branch changes.
- Default LTR and logical-movement: the `mapped && !shift` condition already
  requires `pt_caret_movement_visual` and a mapped current line; no default
  path changes.
- Edge case to decide in W1: a selection whose start or end line is unmapped
  (bidi text next to ASCII inside one paragraph) - use the logical line edge
  there and the visual edge only on mapped lines.

**Risk.** Low. The method already repaints per logical range, so no golden or
paint change is expected. Tests that assert `cursor` after collapse on RTL
multi-line selections do not exist today (grep: only `selection_anchor` checks
at 155, 539, 600 - all single-line or non-bidi); new assertions are additive.
The only behavioral surprise is that a collapse can now land on a byte that is
not the logical start of the range, which is exactly the fixed bug.

**Tests.** `tests/test_text_area.c`: a wrapped RTL selection spanning 2-3 rows,
Left collapses to the visual left (this is the byte at the left edge of the
last visual row of the range) and Right to the visual right; LTR pair keeps the
logical collapse; painting before collapse asserts selection pixels on both
rows (uses existing `pt_canvas_get_pixel`) so a "logical collapse happened early"
regression is caught.

This item needs **no `widget.c` structural change** beyond the multi-line branch
itself: `visual_line_edge` is already local to `widget.c` and both endpoint maps
are built by `text_area_build_layout`. It is expected to be the smallest of the
three and to land first inside W1 so W1's commit boundary is clean if the budget
runs out.

### 3. Table horizontal wheel - feasible at the platform level, with a protocol caveat

**Protocol verdict.** `wl_pointer.axis` has carried both axes since wayland 1.0
(unversioned, `wl_pointer` min version 1, protocol version 10 on this system):

```
/usr/share/wayland/wayland.xml
2270  <enum name="axis">
2271    <entry name="vertical_scroll" value="0" summary="vertical axis"/>
2272    <entry name="horizontal_scroll" value="1" summary="horizontal axis"/>
```

and the client header agrees (`/usr/include/wayland-client-protocol.h:4825`,
`WL_POINTER_AXIS_HORIZONTAL_SCROLL = 1`). The protocol **does** provide the
horizontal axis; libwayland's `wl_seat_get_pointer` binds `wl_pointer` at the
seat's advertised version (Pt adds no explicit bind), and Pt's listener table
simply drops the axis value != 0 at
`platform.c:435`. So the old limitation's premise ("wl_pointer does not
provide the axis") is **wrong at the protocol level**: this is a Pt routing
gap, not a protocol gap. Whether a given compositor *emits* horizontal axis
events depends on the wheel/device (a plain vertical wheel sends only the
vertical axis; a tilt wheel, trackpad horizontal swipe and some mice send
`horizontal_scroll`), so the work is feasible and the limitation must be
narrowed to "depends on the input device", not removed.

**Mechanism (zero new API, additive):**

| Layer | Change | Zero-regression note |
| --- | --- | --- |
| `platform.c` `pointer_axis` | Accept both axes, pass the axis kind through: keep vertical momentum logic keyed on the vertical axis only, forward `horizontal` as a horizontal delta. | Vertical path byte-identical when axis == 0; horizontal events currently return early, so nothing else observes them. |
| `src/window.c` `pt_window_input_axis` | New internal `pt_window_input_axis_h(v, ...)` (or an `axis` enum argument in the internal header) that produces `delta_x` instead of `delta_y`. Public function keeps its 4-arg, vertical-only contract. | Public signature unchanged; existing calls set `delta_x = 0`. |
| `pt_pointer_event` fill | Axis path fills `delta_x` for horizontal, `delta_y` for vertical (field already exists). | Only axis events change; the C++/C tests read `events[4].delta_y == 15` for a vertical call, unchanged. |
| Overlay dispatch | `pt_window_overlays_axis` gets the axis kind so popup lists consume vertical only; horizontal falls through (or is consumed as no-op) same as today. | Overlays still consume all axes for widget dispatch; this keeps the existing "overlay eats the wheel" rule. |
| Hit widget | `pt_widget_wheel(widget, delta)` stays; add an internal horizontal dispatch reached only from the horizontal axis: text area / list ignore it, **table calls `pt_table_scroll_horizontal(hit, dx * 48)`**. | Vertical wheel behavior untouched; the table's existing "vertical wheel at the end bubbles to the parent" rule is preserved by returning true only when the table actually moved. |
| RTL direction | Convert the physical horizontal wheel into logical scroll: when `pt_widget_resolved_ui_direction(table) == rtl`, scroll the other way so a leftward table motion stays "forward" (logical start on the right, matching `rtl.md` layout rules). Still a local behavior improvement, not a new flag. | LTR tables are unchanged; the RTL table scroll convention must be documented in `rtl.md`. |
| Keyboard | Already works (`rtl.md` says keyboard horizontal scroll exists); no change. | — |

Table content wider than the viewport is required (`table_content_width`);
if it is not, `pt_table_scroll_horizontal` returns false and the parent chain
can keep the vertical fallback - the horizontal dispatch must then simply not
consume, so the horizontal wheel does nothing, which is the correct result.

**Compositor caveat (the one thing W2 must confirm).** On the tested
compositor (weston/swayx in the dev bench) the bench mouse is a plain vertical
wheel; a synthetic `wl_pointer.axis` with horizontal value can be driven by a
`weston` test plugin or an `wl_seat` test client, so **the routing can be
tested headlessly** through `pt_window_input_axis_h`, and **live** through
`tests/rtl_live_probe.c` if the test compositor accepts the synthetic axis.
If no compositor in the bench emits horizontal axis events even synthetically,
W2's live evidence is marked "headless only; device-dependent live"; the
feature is still delivered (the routing is correct and the compositor/device
chooses to send it), and `rtl.md` wording is narrowed, not deleted.

**Risk.** Medium-low. The axis field is already public and already has
`delta_x`; the main risk is double-consuming a diagonal gesture
(axis + frame) and overlay precedence. The internal dispatch must be one
scalar, one axis - vector combination is out of scope, as it is today.

**Tests.** `tests/test_data_view.c` (table without a viewport overflow must
ignore the horizontal axis; with overflow it scrolls and clamps) and
`tests/test_window.c` or the C++ pointer capture (the axis callback
`delta_x` is set for a horizontal axis and `delta_y` stays 0). `make check`
runs headless; no pixel golden for the table needs to change because the table
is already painted from `scroll_x`.

## Implementation review decisions

The W3 gate resolved the three observations the W1/W2 review raised. They are
recorded here because they settle the design's open points:

- **Dead horizontal velocity state removed.** W2 added an `axis_h_velocity`
  accumulator with a comment claiming it was "only reported while a continuous
  horizontal source is live", but nothing read it: no fling is started for the
  horizontal axis (`pointer_axis_stop` flings the vertical axis only). The
  field, its two timing companions and the false
  comment are removed; a horizontal wheel stays **discrete per event**
  (delta × 48), which is the documented semantics. Removing dead state is
  cheaper and more honest than wiring a symmetric horizontal momentum that no
  widget consumes.
- **Overlay consumption is axis-kind aware.** The design note above left the
  overlay rule as "consume all axes or no-op same as today". The chosen reading
  is the one that cannot surprise: `pt_window_overlays_axis` takes the axis
  kind and returns false for `pt_axis_horizontal`, so a horizontal wheel is
  never consumed by an open overlay as a vertical scroll. It falls through to
  the window, where a table inside the overlay (or under it) pans sideways.
  Vertical wheel behavior over an overlay is unchanged.
- **Gate builds default to `-j2`.** `tools/ltr-compat.sh` hard-coded `-j8` and
  `tools/ci.sh` defaulted to `nproc`; both now default to two jobs because the
  gate machine is memory-bound and parallel sanitizer/consumer links spiked past
  its RAM budget. `JOBS` still overrides `ci.sh`.

## Out of scope (recorded, not planned)

- **`pt_text_span` frozen**: ABI/product decision in 2.1; changing it needs a
  struct field, a new ABI minor export and a migration note. Not M24.
- **Arabic/Hebrew tofu**: a documented font-coverage fallback with
  `pt_app_font_uncovered_count`; no behavior to refine. Not M24.
- **Bidi sanitization opt-in**: a security policy decision (no text path calls
  it by design). Not M24.
- **Small locale table**: deliberate CLDR subset. Not M24.

## Proposed M24 scope and waves

Baseline: 576 functions / 952 entries, `ABI_MAJOR = 1`, SONAME `libpt.so.1`.
Target: **2.2.0**, same ABI major. Budget ~250 credits on a single main
workstream; waves are sized so each can land and commit independently.

| Wave | Scope | Files (edit domain) | New API | Tests |
| --- | --- | --- | --- | --- |
| **W1 - cross-line editing** | (1) Shift+arrow across soft wrap (visual line edge, affinity pinned to the landing line); (2) multi-line mapped selection collapses to the visual edge. | `src/widget.c` (`text_area_horizontal`, possibly one new internal helper next to `text_area_line_edge`), `tests/test_text_area.c` | 0 (internal static helper only) | RTL/LTR pairs, wrap-edge cases, logical-mode unchanged, painted range before collapse |
| **W2 - table horizontal wheel** | Route `WL_POINTER_AXIS_HORIZONTAL_SCROLL` through the platform, window (internal axis-kind path), overlay and hit-test to `pt_table_scroll_horizontal`, with RTL direction mapping; narrow `rtl.md`'s wording. | `src/wayland/platform.c`, `src/window.c`, `src/internal.h`, `src/overlay.c` (axis kind), `src/widget.c` (horizontal wheel dispatch), `tests/test_data_view.c` / `tests/test_window.c`, doc wording | 0 (public 4-arg `pt_window_input_axis` unchanged; `delta_x` reused) | headless axis routing for table + list; C++ pointer axis `delta_x`; optional live probe |
| **W3 - docs, tests, hardening, landing** | `rtl.md` + `zh-CN/rtl.md` limitation rewrite (drop items 1-3, narrow item 3 to device dependence), README pair "What is new in 2.2.0", `api/pt.api` no change (expect 576/952), `VERSION` 2.2.0, gate run. | `docs/rtl.md`, `docs/zh-CN/rtl.md`, `README.md`, `README.zh-CN.md`, `VERSION`, `docs/release.md`, `docs/ROADMAP.md` | 0 | `make check`, `gen-api-reference.py --check`, `check-bilingual-docs.py`, `ltr-compat.sh` 11/11 |

If W2 turns out to be unverifiable even synthetically (no compositor accepts a
horizontal axis), W2 keeps its code (the routing is protocol-correct) but its
completion criterion changes to "headless routing only, documented device
dependence"; W3 proceeds and the wave is not dropped. If W2 were dropped entirely, its
remaining value (documenting the keyboard path and adding a table
horizontal-key test) is merged into W3; the proposed table keeps W2.

**Completion criteria for M24:**

- Default LTR text areas, logical caret movement and vertical-only wheel
  behavior are byte-identical 2.1; `tools/ltr-compat.sh` stays 11/11 `SAME`.
- The three limitation bullets in `rtl.md` are rewritten to the delivered
  behavior (or narrowed for the device-dependent one); `check-bilingual-docs.py`
  passes with the `rtl.md` pair in sync.
- `api/pt.api` still reads 576/952 and binding parity stays 576/576 in C++,
  Python and Java; `ABI_MAJOR` stays 1 (SONAME `libpt.so.1`).
- `make check` green with additive tests only; no golden update is expected
  for painting.
- `VERSION` reads 2.2.0 and `tools/release-check.sh` passes on the frozen tree.

## Risk register

- **Affinity reset**: `entry_set_cursor` clears `caret_affinity` to leading;
  every new cross-line path must set the bias after the call (as
  `text_area_line_edge` does), or the caret will render on the wrong line at a
  shared soft-wrap boundary.
- **Double step**: the mapped Shift branch must not fall through to the visual
  step and then also cross the line; structure it as one decision per press.
- **One-sided wrap**: a next-line candidate must exist (`line + 1 <
  line_count`, `line - 1 >= 0`) and its shared boundary must be a soft wrap
  (`lines[line].end == lines[line+1].start`), not a hard newline (a hard
  newline has its own byte).
- **Unmapped neighbours**: a wrapped line may be mapped while its neighbour is
  ASCII-only; the landing uses the same `pt_text_layout_x_for_position_bias`
  path as vertical motion, which handles both.
- **Wheel double consume**: one axis event must dispatch to exactly one
  scalar; overlays keep precedence, and the table must return "not consumed"
  when it cannot scroll so the parent chain still works.
- **`delta_x` consumers**: `pt_pointer_event.delta_x` is currently always 0
  for axis events. An application that reads `delta_x` without checking the
  phase keeps reading 0 on a vertical wheel; only a horizontal wheel changes
  it, which is the new value, not a regression. This is the one externally
  observable change and must be stated in the W3 docs and in the 2.2.0 README.
- **RTL table direction**: the wheel-to-logical mapping must be local to the
  table (UI direction resolved on the widget), not a new public flag.

## Open questions for the implementation review

1. W1: should a single Shift+Left at a visual wrap edge cross one row per
   press (chosen) or jump to the line edge first and require a second press?
   The chosen rule matches LTR's no-Shift wrap crossing, which already crosses
   in one press.
2. W2: does the bench compositor accept a synthetic horizontal axis from a
   test client? If not, W2's live evidence is "device-dependent" and the
   wording in `rtl.md` is narrowed accordingly.
3. W3: README "What is new" gets the three behavior refinements and no new
   API; if any wave discovers a needed public symbol, it must come back to this
   document for a scope re-estimate (the 576/952 baseline would change).

## Budget

- Research/scaffold (this wave): ~60 credits, one session, read-only on `src`.
- W1: ~80 credits (one edit file, one test file).
- W2: ~70 credits (one platform file, one internal header, two test files).
- W3: ~40 credits (docs pair, version, gate).
- Total: ~250 credits, matching the single-main budget. W3 is the only wave
  that may need a second review pass; W1 and W2 land independently if the
  budget runs short.
