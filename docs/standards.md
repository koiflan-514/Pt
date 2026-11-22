# Pt engineering standards

Status: living document, adopted for the 1.x series. This page consolidates the
cross-cutting engineering rules that the milestone records in
[ROADMAP.md](ROADMAP.md) and the release records in [release.md](release.md)
assume but never stated in one place. [api-policy.md](api-policy.md) remains the
authority for the C ABI itself; this page governs how changes are proposed,
built, tested, documented and released around that ABI.

It is an internal governance document and, like ROADMAP, api-policy, performance
and release, is maintained in English only. The bilingual requirement applies to
the public guides listed in `tools/check-bilingual-docs.py`.

The rules below are written to be enforceable. Where a rule already has a gate
(`make check`, `make api-check`, `tools/release-check.sh`, `make perf-check`,
`tools/check-bilingual-docs.py`) the gate is named; where it does not, the rule
states the check that must be added before it can be claimed.

## 1. Versioning and ABI discipline

- The C ABI is append-only within a major. A minor release may add functions,
  callback typedefs, opaque handles, enum constants and new structs; it may not
  remove or rename a symbol, change a signature, renumber an existing enum
  constant, or reorder the fields of an existing public struct.
- SONAME stays `libpt.so.1` for the whole 1.x series. Any removal or signature
  change requires a major bump to `libpt.so.2` and a migration guide; it is never
  smuggled into a minor.
- New structs that may grow later carry a size or reserved fields from their
  first release. New enum constants are appended and documented `since 1.x`.
- `tests/test_abi.c` pins every public enum constant, modifier/state bit,
  sentinel, limit and public struct field order with `_Static_assert`/`offsetof`.
  A new enum or struct is not complete until its values and layout are pinned
  there; `api/pt.api` alone is not a substitute because it is a release-time
  diff, not a build-time gate.
- A behaviour change that keeps every signature still breaks applications and
  must be listed in the release notes under "Behaviour changes" even when the
  ABI diff is empty.
- Release-time ABI comparison against the previous tag uses `abidiff`
  (libabigail) and only allows `Added` differences; `Changed`/`Removed` fail the
  release. This runs on the release tree, not on every pull request.

## 2. API addition workflow

Every new public function follows the same path, in one change:

1. **Proposal.** A short note stating the problem, the API sketch, ownership and
   lifetime, threading, the error contract, the accessibility surface, the
   binding plan, the test plan and the compatibility impact. Protocol-facing
   proposals attach the relevant rows of [compositor-matrix.md](compositor-matrix.md).
2. **Review.** At least the C ABI, binding, documentation and test perspectives
   are covered. The proposal is rejected if it cannot name its failure paths.
3. **Landing.** Header + implementation + generated reference + all three
   bindings + tests + examples + CHANGELOG land together. The function is tagged
   `since 1.x`. `make api-update` records the baseline diff in the same commit.
4. **Freeze.** Two weeks before a minor tag the tree accepts bug fixes only; new
   API moves to the next minor.

A commit that adds a public function without its baseline entry, reference
entry, bindings and tests is incomplete and must not be tagged. The 1.1.0
work-in-progress violated this (see ROADMAP M18); the rule exists because that
violation is exactly what makes a release gate go red after the fact.

## 3. Binding parity

- C++, Python and Java are part of the supported surface. Every declared
  function must be wrapped by all three; `make api-check` fails on any missing
  name. This is a hard gate, not a report.
- The reviewed allowlist in `tools/api-audit.py` is for genuinely C-only
  functions. Each entry needs a written reason in [api-policy.md](api-policy.md)
  **and** a `target_version` with a deadline. An entry that passes its deadline
  fails the gate. A silent allowlist added to make a build green is a policy
  violation, not a workaround.
- Every added function ships with binding tests in the same change: Python
  `argtypes/restype` and null/ownership cases, Java `MemorySegment`/`Arena`
  lifetime cases, C++ RAII and exception-to-error cases. Registration-only tests
  are not sufficient for callbacks that dispatch events.
- At a release the parity report shows 100% for all three languages and an empty
  allowlist, or an allowlist whose every entry is documented and within its
  deadline. `tools/release-check.sh` enforces the empty-allowlist form for a
  frozen minor.

## 4. Generated reference and documentation automation

- `docs/api-reference.md` is generated from the public headers and is the
  per-function source of truth. The generator must enumerate `include/pt/*.h`
  (or a manifest derived from it), not a hand-written module tuple. A new header
  that the generator does not know about must make `--check` fail, not pass
  silently. The 1.1.0 `custom.h` omission is the canonical failure this rule
  prevents.
- Every `pt_api` function has a reference entry with ownership, threading, error
  contract and `since`. `tools/release-check.sh` checks that every baseline
  function name appears in the reference; the generator check and the coverage
  check must agree.
- Counts quoted in README, architecture and bindings (function totals, parity
  figures) are derived from the baseline, not typed by hand, and are checked by
  `tools/release-check.sh`.

## 5. Documentation and bilingual policy

- Documentation follows Diátaxis: quickstart is the tutorial, the guides are
  how-to/explanation, the generated reference is reference. Each page states the
  version it applies to.
- Public guides are bilingual: `README.md` ↔ `README.zh-CN.md` and
  `docs/<guide>.md` ↔ `docs/zh-CN/<guide>.md`. The pairing is enforced by
  `tools/check-bilingual-docs.py`: language header present, fenced code blocks
  byte-identical, heading-level sequence and table-row counts equal, every local
  link resolvable.
- Structural parity is necessary but not sufficient. A change to a public
  guide's meaning (version, parity figures, decoration behaviour, a new section)
  must be reflected in both languages in the same change. The 1.1.0
  `## Decoration policy` section added to README without a Chinese counterpart
  is the failure this rule prevents.
- Internal governance documents (ROADMAP, api-policy, performance, release,
  this page, compositor-matrix) are English-only and are not part of the
  bilingual pairing.
- Each minor ships a "Migration and behaviour changes" page even when no API was
  removed; protocol-negotiation changes are written there.

## 6. Test matrix

A change is tested at the lowest layer that can observe it, and at every higher
layer that the behaviour reaches. "Which paths only a live compositor can test"
is answered explicitly, not assumed.

| Layer | Gate | Frequency | Notes |
| --- | --- | --- | --- |
| Headless unit/property | `make check` | every change | new API needs failure-path tests, not only happy paths |
| ABI pins | `tests/test_abi.c` | every change | new enum/struct pinned here |
| Golden image | `test_visuals` + offscreen snapshot | every change | 1x/1.5x/2x; per-pixel threshold; diff artifact on failure |
| Accessibility model | `test_a11y` | every change | every control asserts role/name/state/action; a new control without assertions is incomplete |
| Live AT-SPI bridge | `test_a11y_bridge` | when a bus exists | a skip is explicit and counted, never silent |
| Protocol sequences | fake-compositor replay | every change | decoration, text-input, primary-selection, popup event orders |
| Live compositor | `make check-wayland`, `make check-typing` | nightly / pre-release | Mutter/KWin/niri/Sway/Weston; missing capability skips explicitly |
| Fuzz | in-process targets | nightly | UTF-8 editing, PNG decode, URI parse, clipboard MIME, protocol decode |
| Performance | `make perf-check` | every change (headless) | hard ceilings, median of 3; live numbers are trend only |
| Sanitizer | `make check-sanitize` | weekly / pre-release | ASan+LSan+UBSan; fd count must not grow across create/destroy cycles |
| Installed consumers | `tools/install-check.sh` | pre-release | C/C++/Python/Java against staged install; SONAME/pkg-config; uninstall |
| Reproducible tarball | `tools/ci.sh` | pre-release | two `make dist` runs byte-identical; extracted tree rebuilds and passes `make check` |

Offscreen rendering used for golden images must be side-effect-free (section 7);
a snapshot that mutates live window state cannot be a regression oracle.

## 7. Callback lifecycle and reentrancy

One rule set governs every callback (click, select, paint, IME, fd, raw input,
drag, overlay):

- **Payload lifetime.** Pointers and strings handed to a callback are valid only
  for the duration of the call. A callback that needs the data later copies it.
  Each callback's header comment states "may destroy / may re-enter / payload
  lifetime".
- **Destruction during dispatch.** A callback may destroy the widget, window or
  overlay it is running on. The framework defers the free until dispatch returns
  and guards internal use with an attached/generation check (`pt_widget_attached`
  and equivalents). State is updated before the callback fires; notification is
  the last step.
- **Forbidden.** Recursively calling `pt_app_run_for`, recursively painting,
  calling any `pt_*` from another thread, and `longjmp`/C++ exceptions crossing
  the C boundary. Binding wrappers catch exceptions and convert them to the
  error path.
- **Deferred work.** Requesting a redraw, changing another widget, opening or
  closing an overlay from inside a callback is allowed but queued until after
  the current dispatch.

Paint callbacks additionally:

- are pure and idempotent: the same widget may be painted 0..N times per frame
  (once per damage tile) and must produce the same pixels regardless of tile
  order or count;
- must not change the tree or trigger layout; use `pt_widget_request_redraw*` to
  defer;
- receive a `pt_paint_context` valid only during the call, in widget-local
  logical pixels; `pt_paint_scale()` is the device-pixel ratio;
- balance `pt_paint_push_clip`/`pt_paint_pop_clip`; the paint pass checks the
  depth on return, restores the saved depth and clip if a callback left them
  unbalanced, and records a warning and the thread-local error;
- must not call `pt_window_paint_pixels` re-entrantly; an always-present
  `in_paint` flag rejects the call, records the error and leaves the live frame
  untouched.

`pt_window_paint_pixels` is a pure snapshot: it does not damage, clear
`needs_draw`/`redraw_scheduled`, touch `last_paint_time`, or collect deferred
overlays. Calling it on a mapped window must not swallow the next live frame.
Its pixel format is premultiplied `0xAARRGGBB`, row-major, stride in pixels; the
extended entry point takes stride/scale/format explicitly.

The guards are always compiled in; there is no debug-only switch and no
`PT_DEBUG_CALLBACKS` environment variable. Reentrancy and clip imbalance are
detected in every build. Use of a context after its callback returns is a
caller bug the header forbids rather than a state the library can detect,
because the context owns no storage.

## 8. Platform negotiation matrix

- Every optional Wayland global is a runtime capability bit, never a compile-time
  switch. Missing capability has a defined fallback that is itself tested.
- `xdg-decoration` `set_mode` is a preference; the `configure` mode is
  authoritative. The client tracks `requested_mode` and `effective_mode`
  separately, sends `set_mode` only on a change of request, and obeys whatever
  `configure` returns. "Requested CSD but received SSD" (Sway tiling) is a legal
  path and is tested.
- `pt_window_set_decorated(bool)` means only "does Pt draw its own header"; it
  never requests server-side decoration. Server-side decoration is an explicit
  `pt_window_set_decoration_mode(server)` choice. `false` therefore means "Pt
  draws nothing and does not ask the compositor to decorate" on every
  compositor.
- Window state (`fullscreen`, `tiled_*`, `configure_bounds`, `wm_capabilities`)
  is consumed: fullscreen hides the header independent of `decorated`; tiling
  drops corner radius/shadow; bounds clamp size requests; missing `maximize`
  capability disables the button and makes `pt_window_maximize()` return false.
- [compositor-matrix.md](compositor-matrix.md) records protocol × compositor ×
  version × behaviour with evidence grades. Each quirk has an id (e.g.
  `Q-DEC-001`) referenced by both the code comment and the test name; no
  unexplained compositor `if`.
- The 1.x protocol surface is frozen at the set 1.0 already uses plus the
  decoration/primary-selection/text-input hardening. A new global is added only
  with a real need and its matrix rows and fallback tests.

## 9. Input semantics contract

The input contract is written in the headers and tested, not left to the
implementation:

- **Serials are per source.** Focus/enter, pointer button and key press keep
  separate serials. `primary_selection.set_selection` uses the serial of the
  event that triggered the selection (pointer release for drag-select, last key
  press for keyboard select); a programmatic selection with no triggering event
  does not send and returns false. An explicit `..._set_serial` entry point lets
  an application pass the serial from inside its event callback.
- **Button specificity.** Non-left buttons do not disturb left-button press
  state or double-click counting; `click_count` and `last_click_*` are tracked
  per button. `pt_pointer_event.pressed` reflects the button in question.
- **Dispatch order.** Overlay key handling → raw input callback → legacy key
  callback → focused widget. A raw callback returning true consumes the event
  for widget dispatch but internal focus/serial accounting still completes, so
  popup grabs never see a stale serial. A consumed key does not also reach
  text-input as a commit.
- **Advisory callbacks.** Enter/leave/motion callback return values are ignored
  for hover tracking; the implementation must match the header. If motion is
  ever allowed to consume drag, the header says so explicitly and a test pins it.
- **Touch.** Touch ids are distinct; a second finger is not mapped to a pointer.
- **Cancellation.** Drag/scroll leaving the window or losing focus has a defined
  cancel path. Consume affects only the current event, never subsequent grabs.

## 10. Accessibility acceptance

- The acceptance target is WCAG 2.2 AA, expressed through AT-SPI2 semantics and
  events, not pixels. EN 301 549 V4.1.1 aligns with WCAG 2.2 AA; the legal OJEU
  baseline is still V3.2.1 (see compositor-matrix appendix for the citation
  status). Procurement-facing claims use WCAG 2.2 AA language.
- Every logical control exposes role, name, description, state, relations and
  the applicable `Action`/`Text`/`Value` interfaces, and emits the focus, caret,
  checked/selected/expanded, name and structure events Orca relies on.
- **Custom paint does not exempt a control from semantics.** A `pt_paint_*`
  user that does not declare role/name/state is an empty box to a screen reader.
  The framework provides the declaration path and the acceptance gate requires
  it; a new custom-drawn control without accessibility assertions is incomplete.
- Status messages (toast, save state) are exposed without requiring focus
  (WCAG 4.1.3). Dragging operations (slider, splitter, column resize) have a
  single-pointer/keyboard alternative (2.5.7). Interactive targets meet the
  24×24 minimum (2.5.8). Focus is not fully obscured by overlays/sticky headers
  (2.4.11). Password fields allow paste (3.3.8).
- Reduced motion is honoured from the desktop preference where available, not
  only from the app-level flag.

## 11. Resource limits and safety

- Clipboard and primary-selection reads use a bounded timeout and a transfer cap
  (the existing 1.5 s / 16 MiB constants); a decode path adds a decoded-pixel cap
  (e.g. 8192×8192) independent of the transfer cap, and fails to NULL rather than
  crashing on a decompression bomb.
- Data offers, table/grid columns and similar collections have explicit upper
  bounds (offers 64, columns 1024) that are tested at the boundary.
- xkb keymap parsing, shm geometry and protocol string decoding validate bounds
  before use.
- fd watches default to borrowed (Pt does not close); an owned fd is closed on
  unwatch/app destroy. A callback may unwatch itself (deferred free); watching
  the same fd twice is rejected. `POLLHUP/POLLERR/POLLNVAL` map to the error event
  and the callback fires at least once. fd watches are included in the
  `pt_app_run_for` timeout computation.
- Every new resource API has a "create/destroy N times, no fd/memory growth"
  sanitizer test.

## 12. Performance budgets

Headless ceilings are enforced by `make perf-check`; live numbers are trend only
and never block a merge. Suggested budgets (to be reconciled with the measured
1.0/1.1 baselines in [performance.md](performance.md)):

| Metric | Ceiling | Note |
| --- | --- | --- |
| 1280×800 full paint | 16 ms | do not regress the 1.0 baseline |
| single-row damage paint | 2 ms | |
| 4K full paint | 40 ms | software-rendering reality |
| startup first frame | 50 ms | headroom for cold font cache |
| idle CPU (500 ms) | 2 ms | |
| idle CPU (5 s, live focused entry) | 1500 ms | the existing live guard |
| empty-window RSS | 40 MB | large tables measured separately |
| event → damage | 1 ms | input latency, excludes compositor |

A render optimisation lands with its golden-image and guard evidence in the same
change, never bundled silently with a feature commit.

## 13. Scope management and commit hygiene

- Each minor has an explicit in/out list. "While we are here" additions move to
  the next minor. The 1.1.0 work-in-progress mixed four kinds of change
  (platform negotiation, extension API, render optimisation, lifecycle fixes);
  the rule is to separate them.
- Commits are split by theme: platform negotiation, extension API, render
  optimisation and lifecycle fixes are distinct commits, each keeping
  `make check` green. Baseline, generated reference and documentation land in the
  same commit as the code they describe, so no intermediate commit is
  self-inconsistent.
- A commit message describes the whole change, not just its headline feature.
- post-1.0 candidates carry a status (delivered / open / deferred) and a source
  reference; an item that ships moves out of "Not delivered" in the same change.

## 14. Release gate

A minor is taggable only when, on the release tree:

1. `make check`, `make check-bindings`, `make perf-check` and (with a display)
   `make check-wayland` and `make check-typing` pass.
2. `make api-check` shows the reviewed baseline diff, 100% three-language parity
   with an empty (or documented, in-deadline) allowlist, and a generated
   reference that covers every baseline function.
3. `abidiff` against the previous tag shows only additions.
4. `tools/release-check.sh --full` reports zero failures; every `open` item is
   answered by the release notes. The bilingual check passes.
5. `tools/ci.sh` runs clean end to end: sanitizers, installed consumers,
   reproducible tarball and the extracted-tree rebuild.
6. VERSION, the four version macros, the README status line, the reference
   header, ROADMAP and the release record agree on the new version; SONAME
   follows section 1.
7. The release record captures the audit, parity, live suites, performance
   figures and the ABI/behaviour diff summary.

After tagging, collect compositor/distribution feedback for a week; a hotfix
fixes regressions only and adds no feature.

## Current deviations

The 1.4.0 tree keeps the gaps this page was written around closed for the
shared C surface: the generated reference derives its module list from
`include/pt/*.h`, the baseline and reference agree on the 556-function /
910-entry surface, every declaration carries a since marker (412 × 1.0,
36 × 1.1, 106 × 1.2, 2 × 1.3), the bilingual pairs are in sync, the version is
1.4.0 everywhere, and the ABI pins cover the existing enums, structs and limits.
The three-language wrappers keep 556/556 with the reviewed allowlist empty; the
final 1.4.0 gate on `e1104c2` ran C++ 844 checks, Python 67 tests and Java 685
checks with 0 failures, the same counts as 1.3.0. 1.4.0 adds no public symbol,
so the parity and ABI surface is unchanged by construction. The remaining
deviations are explicit and tracked in [ROADMAP.md](ROADMAP.md) under M21:

- **The SVG subset is documented, not complete.** `pt_icon_image` accepts SVG
  and, since 1.4, SVGZ through vendored nanosvg and resolves hicolor
  `scalable/` in `.svg`, `.svgz`, `.png` order, but text, raster images,
  filters, masks, `clipPath`, `use`/`symbol`, markers, animation and the full
  CSS cascade are ignored by the parser. A document with no drawable shape is
  rejected. This is the same honest-subset rule the PNG path follows, not a
  claim of full SVG support.
- **Touch and tablet wire injection are covered.** Since 1.4 the fake
  compositor injects `wl_touch`, tablet-v2, drag-and-drop and primary-selection
  traffic, and `test_wire_input` asserts the resulting state and wire
  parameters; the earlier deviation is closed.
- **The toplevel-icon path has no live compositor recording.** The wire test
  asserts the submission parameters and niri/Sway/Weston take the tested no-op
  path, but none of the matrix's KWin/Labwc/Muffin/Jay/Pila exposures was run in
  this round.
- **The live IME composition matrix is partial.** The niri+fcitx5 cell is
  recorded by `make check-ime-live`; a KWin+fcitx5 v2 or Mutter+IBus v2 run is
  still unavailable and is recorded as open rather than implied.
- **The pickers are locale-independent.** The date picture is explicit, the
  committed number separator is `.`, and the process locale is never read. This
  is a documented contract, not an unfinished translation path.
- **Binding raw dispatch is closed at wire level, not on a live seat.**
  `tests/wire_driver.c` delivers real wire key and pointer events to the C++,
  Python and Java callbacks in the headless gate; a physical-seat run remains
  optional live coverage.
- **The 1.4.0 gate is complete on `e1104c2`; the tag is pending.** `make check`
  (37 executables, 8,991 checks plus the 454,380-check property suite),
  `make check-bindings`, `make perf-check`, the live suites including
  `make check-ime-live`, `tools/ci.sh` (exit 0) and
  `tools/release-check.sh --full` all pass and are recorded in
  [release.md](release.md); the symbol-level ABI diff against 1.3.0 is empty.
  The two open items are the human freeze commit/tag and live-suite sign-off.
  The 1.3.0 gate itself remains complete on `fafbcae`.

The 1.1.0 work-in-progress also violated the section 2 landing rule (headers,
bindings, reference and tests did not land atomically). That history is kept in
M18 so the rule is not quietly forgotten; it is not a current exception. M19,
M20 and M21 kept the rule: each wave workstream landed header, implementation,
C tests and ABI pins together, and the integration phase owned the baseline,
reference, version and guides. 1.4.0 extended that discipline to a wave that
added no public symbol: the header comment is the only contract change and the
generated reference, guides, roadmap and release record moved with it.
