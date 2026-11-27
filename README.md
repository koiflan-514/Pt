# Pt

English | [简体中文](README.zh-CN.md)

**Pt** is a small, fast, Wayland-native GUI framework written in modern C23.
Applications can be written in C, C++, Python or Java against one flat C ABI.
The name is the chemical symbol for platinum: small, dense and unreactive.

Status: **2.3.0**. The inline image runs release: a label or text area can place
a reference-counted raster image inside its UTF-8 text through a U+FFFC object
replacement character, backed by a frozen side table. It adds **5 public
functions, 2 enum values and 1 struct**, so the surface becomes **581 functions
and 960 baseline entries**, 0 removed and 0 changed against 2.2.0.
The surface is a 581-function C ABI wrapped in C++/Python/Java with
binding parity enforced;
SONAME stays `libpt.so.1` because 2.3 is additive and every widget without an
image keeps the 2.2 text path byte for byte (`ABI_MAJOR` is tracked
separately from `VERSION`). The
release record is tag-ready with the gate run markers filled in; see
[docs/release.md](docs/release.md). The API
is tracked by a reviewed baseline, a generated function reference and a
compatibility policy; see [docs/api-reference.md](docs/api-reference.md),
[docs/api-policy.md](docs/api-policy.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

## Why Pt

- Wayland first: `xdg-shell`, decoration negotiation that is client-side by
  default, `fractional-scale-v1` and `text-input-v3`, with no X11 or GL runtime
  baggage.
- A retained-mode widget tree and a flat, opaque C ABI that is pleasant to bind.
- Crisp FreeType + HarfBuzz text, including CJK shaping and fractional scaling.
- Right-to-left text done properly: the full Unicode Bidirectional Algorithm,
  Arabic and Hebrew shaping with automatic bracket mirroring, mirrored rows,
  columns-kept-logical tables, visual caret movement by default with an opt-in
  logical policy, a locale-to-direction helper, bidi control scanning and
  sanitization for untrusted text, logical start/end padding, and an accessible
  direction attribute — with unchanged pixels for pure LTR interfaces.
- A modern, tokenized design language: 38 semantic color roles and 10 geometric
  metrics, deep dark and clean light palettes, tonal control surfaces, soft
  accents, focus-visible rings and restrained motion, overridable per window.
- Controls that feel complete: checkboxes and radios, progress and spinners,
  segmented switchers, chips and skeletons, selection-aware entries, comboboxes
  that pick from a list or filter one, multiline text areas, cut/copy/paste and
  undo/redo.
- Content-heavy surfaces stay cheap: virtualized lists, tables and trees recycle
  visible rows and share one multi-row selection model, wrap and grid containers
  adapt to the window, and a text area wraps notes and logs with caret-goal
  navigation and grow-to-content sizing.
- A navigation shell in the box: tabs with overflow and a gliding indicator,
  sidebars with icon rows and a compact mode, and 21 built-in vector glyphs with
  accessible icon buttons.
- Motion with a contract: public easing curves, interruption-safe transitions
  and app-level reduced-motion support that framework controls honour.
- Menus, modal dialogs, stacking toasts and delayed tooltips that share the
  widget, focus and paint pipeline instead of spawning a second UI system;
  menus and tooltips use native `xdg_popup` surfaces when available.
- An accessibility model with roles, names, descriptions, states, text/value
  exposure and an opt-in AT-SPI2 D-Bus bridge for external screen readers.
- External drag and drop with drop targets that either read one MIME type
  synchronously or introspect an offer and stream it asynchronously, draggable
  entry text, multi-type `pt_window_begin_drag` sources with a completion
  action, `text/uri-list`
  file drags and drag images, plus compositor swipe/pinch gestures, multi-contact
  pinch/rotate, stylus pressure and tilt, long press and kinetic scrolling
  through one normalized gesture stream.
- Damage-tracked partial repaint over a persistent backing canvas with a coalesced
  tile index, subtree-scoped layout invalidation, a hashed font face table and a
  bounded text shape cache, plus touch input mapped onto pointer semantics.
- Software rendering over `wl_shm`: fast cold start, under 0.3% of one core idle
  with an unfocused window, no GPU driver dependency.
- Apache-2.0, snake_case identifiers throughout, no hidden global state.

## What is new in 2.3.0

2.3.0 is the inline image runs release. A label or text area can place a
reference-counted raster image inside its UTF-8 text through a **U+FFFC object
replacement character** (`EF BF BC`); the image descriptor lives in a sorted
side table keyed by that byte offset, so the text stays the single source of
truth and no existing offset shifts. The release adds **5 public functions, 2
enum values and 1 struct** and the surface becomes **581 functions and 960
baseline entries**, 0 removed and 0 changed against 2.2.0. The API is additive:
`pt_text_span` is frozen, `ABI_MAJOR` stays 1 and the SONAME stays
`libpt.so.1`. A widget that never sets an image takes the same layout, measure
and paint path as 2.2.0, byte for byte, and the LTR compatibility gate reports
no difference against 1.4.0 and 2.2.0.

- **Side-table model, frozen text struct.** `pt_widget_set_text_images`,
  `pt_widget_clear_text_images`, `pt_widget_get_text_image_count`,
  `pt_widget_get_text_image` and `pt_text_area_insert_image` manage a sorted
  descriptor table; `pt_text_image` carries the byte offset, a
  reference-counted image handle, logical size, alignment and a UTF-8 alt
  string. Ordinary edits shift, clamp and drop descriptors with the text.
- **Fontless runs in the text pipeline.** Measurement, wrapping, the visual map
  and paint treat the placeholder as one atomic box that bypasses shaping. The
  box has logical and device units, so fractional scaling stays crisp, and
  baseline or middle alignment grows the line's ascent or descent.
- **Atomic editing and selection.** Caret movement, selection, deletion, paste
  and IME never split the three-byte placeholder; a selection endpoint inside a
  placeholder snaps outward before copy or cut. Copy and cut write the alt text
  (or U+FFFC when alt is empty), and undo/redo restores the text and the image
  table together with a deep copy.
- **Embedded-object accessibility.** The AT-SPI bridge exposes one virtual image
  child per run with role `image` and the alt name, real Component extents and
  hit testing, and CutText/DeleteText through the normal text-delete path so the
  edit is one undo step. The Text interface still reports raw U+FFFC and
  unchanged character offsets.
- **Bindings and documentation.** C++, Python and Java wrap the new functions
  with the same ownership rules (binding parity is 581/581), the bilingual
  inline-images guide records the delivered contract, and C, C++, Python and
  Java examples embed a PNG, set aligned placeholders and insert one at the
  caret.

## What is new in 2.2.0

2.2.0 is the RTL editing-polish release. It changes behavior only, inside
existing declarations: **no public function, enum or struct is added**, so the
surface stays at **576 functions and 952 baseline entries**, 0 removed and 0
changed against 2.1.0. The SONAME stays `libpt.so.1`. A zero-initialized
application keeps the 2.1 pixels and editing behavior for LTR input, and the
LTR compatibility gate reports no pixel or event difference against 1.4.0 and
2.1.0.

- **Multi-line selection paints its visual edge.** A mapped selection that
  spans several wrapped rows no longer leaves the collapsed boundary edge
  unpainted in RTL: the highlight rectangles follow the visual edge at the
  mapped boundary.
- **Shift crosses a soft wrap in RTL.** One Shift+arrow press that reaches a
  soft wrap now moves one visual row instead of stalling inside the line, so a
  selection can be extended row by row in RTL and in mixed-direction text.
- **The horizontal wheel axis reaches tables.** `wl_pointer`'s
  `horizontal_scroll` has existed since protocol v1 and was previously
  discarded; 2.2 routes it through an internal axis-kind path to the table
  (delta × 48), flips the sign for RTL tables, fills `pt_pointer_event.delta_x`,
  and lets it fall through overlays instead of being swallowed as a vertical
  scroll. The public four-argument `pt_window_input_axis` is unchanged.
  Whether a device emits the axis is hardware-dependent: tilt wheels and
  trackpad sideways swipes do, a plain vertical wheel does not.

## What is new in 2.1.0

2.1.0 is the RTL ergonomics and safety release. It closes the four gaps that
2.0 documented as deliberate limitations and changes no default: it adds **12
public functions, 3 enums (15 values) and 1 struct** while every feature is
opt-in, so the surface becomes **576 functions and 952 baseline entries**, 0
removed and 0 changed against 2.0.0. The SONAME stays `libpt.so.1`: the release
is purely additive and a zero-initialized application keeps the 2.0 pixels,
caret, padding and text path. The annotated tag follows the final gate run
recorded in [release.md](docs/release.md).

- **Caret movement policy.** `pt_caret_movement`
  (`inherit`/`visual`/`logical`) with app, window and widget setters/getters —
  six functions. Resolution is the widget's explicit value, then the nearest
  explicit ancestor, then the window, then the application, then **visual**, so
  `inherit` (the zero value) keeps 2.0 behavior exactly. In `logical` mode Left
  and Right move by UTF-8 character boundary in the stored string and
  Home/End go to the logical line ends; vertical movement, Shift selection,
  Backspace/Delete and the IME caret rectangle keep their 2.0 rules. The 2.0
  window key callback still works and is no longer the only way to get logical
  arrows.
- **A locale hint instead of a guess.** `pt_locale_ui_direction(locale)` is a
  bounded, ICU-free, thread-safe lookup of the language and script subtags of a
  BCP-47 or POSIX locale against a conservative RTL table (Arabic, Hebrew,
  Farsi, Urdu and the `Arab`/`Hebr`/`Thaa`/`Nkoo`/`Syrc` scripts, with the
  script winning so `az-Arab` is RTL while `az-Latn` stays LTR). `pt_app_set_locale`
  stores the resolved string as a hint and `pt_app_get_locale` returns the
  stored hint; an explicit `pt_app_set_ui_direction` still wins in either call
  order. Nothing reads the environment, and an unset locale resolves to LTR, so
  a zero-initialized application is unchanged.
- **Bidi control scanning and sanitization (Trojan Source).** A new public
  header, `include/pt/bidi.h`, adds
  `pt_bidi_control_inventory(text, length, spans, capacity)`, which reports the
  nine explicit directional controls (U+202A–U+202E and U+2066–U+2069) with
  byte-exact decoding, byte offsets, lengths and kinds — allocation-free, and
  it still returns the total count when the span buffer is too small — and
  `pt_bidi_sanitize(text, length, mode, &out_length)`, which returns a
  caller-owned buffer (`pt_free`) with the controls **stripped** or
  **neutralized** to U+FFFD (worst case `3n+1` bytes). Both are **opt-in**: no
  widget, clipboard, drag or accessibility path sanitizes automatically, so
  stored bytes and every byte offset stay exactly as the application supplied
  them. The public inventory is narrower than the internal
  `pt_bidi_is_removed` X9 paint predicate by design.
- **Logical start/end padding.** `pt_widget_set_padding_logical(top, start,
  bottom, end)` stores the logical edges and a layout/measure prepass writes the
  resolved physical edges from the widget's resolved UI direction on every
  layout, so RTL `start` is the right edge and `end` the left, and a later
  direction change — explicit or inherited — is honoured without another call.
  `pt_widget_set_padding` stays physical and the last call wins; margins stay
  physical, and a widget that never calls the new setter lays out exactly as in
  2.0.
- **LTR is still protected by both hard gates.** The 2.0 goldens are unchanged:
  the pure-LTR PNG snapshots are byte-identical, `tools/ltr-compat.sh` reports
  **11/11 `SAME`** against 1.4.0, the Unicode conformance counts stay 770,241
  + 91,707 with 0 failures, and every 2.0 test assertion is kept. Parity is
  **576/576** in C++, Python and Java with an empty allowlist.
- **Examples.** `examples/c/ergonomics_gallery.c` switches the caret policy,
  sets a locale hint, pads a card with logical start/end and sanitizes an
  untrusted string; `examples/cpp/ergonomics.cpp`,
  `examples/python/ergonomics.py` and `examples/java/Ergonomics.java` are
  smaller 2.1 notebooks for each binding.

## What is new in 2.0.0

2.0.0 is the first structural release since 1.0 and the bidi/right-to-left
milestone. It adds **8 public functions and 2 enums (6 values)** with
zero-value defaults that preserve LTR behavior: the surface is **564 functions
and 924 baseline entries**, 0 removed and 0 changed. The SONAME stays
`libpt.so.1`: the release marks a rendering correction and an additive API, not
a binary break, so the Makefile tracks `ABI_MAJOR` separately from `VERSION`.
The annotated tag follows the final gate run recorded in
[release.md](docs/release.md).

- **A real bidi engine.** 2.0 vendors SheenBidi v3.0.0 (Apache-2.0) as one
  unity-compiled object and drives it through a UTF-32 adapter that keeps a
  byte-offset table beside the code points. The adapter exists because the
  upstream UTF-8 entry point has an L1 line-end whitespace defect; the internal
  path avoids it without patching the engine. The Unicode Bidirectional
  Algorithm is now complete: explicit embeddings and overrides, isolates, weak
  and neutral resolution, paired brackets, implicit levels and the line-level
  rules after wrapping.
- **100% conformance, in `make check`.** The vendored Unicode 18.0.0
  `BidiTest.txt` and `BidiCharacterTest.txt` data (Unicode License v3, gzipped
  for size) runs **770,241 + 91,707 cases with 0 failures** in about 0.5 s.
  `THIRD_PARTY.md` records the engine tag, digest, license and upgrade policy.
- **Direction API without a new editing model.** `pt_ui_direction`
  (`inherit`/`ltr`/`rtl`) and `pt_text_direction` (`auto`/`ltr`/`rtl`) plus
  app, window and widget UI setters/getters and widget text setters/getters
  make eight functions. Resolution is widget → nearest explicit ancestor →
  window → app → LTR; auto follows UBA P2/P3 per paragraph and falls back to
  the resolved UI direction for neutral-only text. Getters return the explicit
  value, and `inherit`/`auto` are zero, so zero-initialized widgets behave as
  in 1.x.
- **Visual editing by default.** Left/Right move to the visually adjacent
  cluster boundary (which can raise or lower the logical offset inside an RTL
  run), Home/End go to the visual line ends, Ctrl+Home/End stay document
  logical, Shift keeps a logical anchor, and Backspace/Delete stay logical.
  Soft-wrap boundaries carry caret affinity, selections paint one rectangle per
  visual run, IME rectangles are visual, and password bullets derive their
  direction from the stored text. Applications that want logical movement
  intercept keys in the existing window key callback; there is no ninth API.
- **Mirrored UI geometry and chrome.** Flex, wrap, grid and scroll geometry
  mirror; tabs, sidebars, picker popups, client-side decoration, overlays and
  menus place their chrome from the resolved UI direction. Scrollbars move to
  the RTL left and tree chevrons to the right. Table and tree **columns keep
  logical order** through an internal opt-out, an RTL entry is right-anchored
  (at `scroll_x = 0` it shows the logical start at the visual right), and
  padding/margin stay physical with no start/end API.
- **Accessibility direction.** Every accessible object publishes its resolved
  UI direction as a `direction` attribute, and text objects publish the
  paragraph direction through `Text.GetAttributes` and
  `Text.GetDefaultAttributes` with a scalar range. The C-level
  `pt_widget_get_a11y_text` keeps **byte** offsets while the D-Bus `Text`
  interface uses **scalar** offsets; passwords report direction but no
  characters. The older wording that called every accessibility getter scalar
  was corrected.
- **LTR is protected by two hard gates.** The 17 pure-LTR PNG snapshots in
  `test_visuals` are byte-identical to 1.4, and `tools/ltr-compat.sh` builds
  1.4.0 and the 2.0 tree on one machine and reports **10/10 `SAME`** across ten
  LTR scenes. No golden hash is checked in because font rasterization differs
  between machines; building both revisions together holds it constant.
- **Honest performance wording.** The LTR fast path classifies text from the
  engine's Unicode bidi types (any R/AL character or bidi format control needs
  UBA) and keeps the legacy single-run path; a 292,531-assigned-code-point
  sweep found 0 mismatches. Bidi analysis is O(n) per rebuild, allocations are
  bounded by line length, and the shape cache is keyed by direction. The old
  "no per-frame allocation" promise is withdrawn because 1.4 already rebuilt
  line layout on every paint; the contract is now **bounded per rebuild**.
- **Examples in all four languages.** `examples/c/rtl_gallery.c` shows a mixed
  Arabic/Hebrew/Latin label, an RTL entry and text area, a mirrored container,
  a nested LTR panel and a direction switch; C++, Python and Java each carry a
  smaller direction switch that exercises the wrappers.

## What is new in 1.4.0

1.4.0 closes the remaining appendable items carried past 1.3 without touching
the 2.0 structural horizon. It adds **no public function, enum or struct**: the
surface stays at 556 functions and 910 baseline entries, exactly as in 1.3.0.
The work is capability extension, test infrastructure and one intentional
behaviour correction. The annotated tag is created after the final gate run
recorded in [release.md](docs/release.md).

- **SVGZ icon sources.** `pt_icon_image_load_file` and
  `pt_icon_image_load_memory` accept a gzip-compressed SVG document by content
  sniffing: the gzip magic `0x1f 0x8b` is checked before the text sniff, so a
  compressed document never has to look like XML at the top level. Inflation is
  bounded in both directions — the compressed input keeps the existing 16 MiB
  cap and the inflated document is capped at 16 MiB as well, which rejects a
  decompression bomb instead of growing it; the inflated bytes must then pass
  the same SVG sniff, so a gzip archive that is not SVG is refused. The handle
  keeps the inflated source, not the compressed bytes, and re-parses it per
  requested size like a plain SVG handle. Hicolor `scalable/` lookup now tries
  `.svg`, then `.svgz`, then `.png`; fixed-size directories remain `.png` only.
  Zero new public functions: the existing loaders gained the format, and
  `pt/icon.h` marks the contract "Since 1.2; SVGZ since 1.4". `test_icon` grew
  from 330 to 800 checks, including mutation and garbage fuzzing of the
  decompressor.
- **Input wire matrix completion.** The fake compositor now injects and
  replays the rest of the input protocol surface. `wl_touch` covers
  down/up/motion/frame/cancel, multiple contacts and pinch, and capability
  removal; tablet-v2 covers the seat/tool lifecycle, proximity, pressure, tilt,
  rotation, slider, wheel and buttons; drag and drop covers `data_device`
  enter/motion/drop/leave, offer accept/receive over an `SCM_RIGHTS` fd and
  finish, plus source target/action/cancelled/`dnd_finished`; primary selection
  gains injection and replay. The new `test_wire_input` slot passes 595
  checks, a `wait_object` helper removes the server-state race (200 consecutive
  runs, zero failures), and the facility's tablet-manager request table had
  `get_tablet_seat`/`destroy` written in the wrong order, now fixed. This is
  test infrastructure: no public header, symbol or ABI changes.
- **Multi-touch focus correction.** `src/wayland/platform.c` used to clear
  `touch_focus` unconditionally when the first contact lifted, so motion and up
  for the remaining fingers were dropped, a pinch could leave active state
  behind, cancel could not clean up, and a repeated pinch failed. The internal
  `pt_window_input_touch_up` now returns the remaining contact count (a `void`
  to `uint32_t` internal interface change, not a public ABI change) and the
  platform clears focus only at zero; a surviving single contact updates the
  emulated pointer again instead of being ignored. `pinch` still never clicks,
  and `test_damage` remains 98 green. This is an intentional behaviour
  correction: repeated pinches and a mid-gesture cancel now behave correctly.
  `test_wire_input` asserts the correct behaviour rather than recording the old
  limitation.
- **Live IME loop for niri + fcitx5.** A runtime registry check showed niri
  26.04 advertising `zwp_input_method_manager_v2` v1 and fcitx5 5.1.23 running
  its pinyin engine. Typing `nihao ` now drives a live composition: the preedit
  advances `n` → `ni` → `ni h` → `ni ha` → `ni hao` and the space commits `你好`
  through text-input-v3 into Pt (`PREEDIT_COUNT=5`). The decisive detail is
  that a virtual keyboard created on the nested display bypasses the
  input-method grab, so the harness injects into the parent display and lets
  the nested niri receive the keys as a real keyboard would. The harness is
  `tools/live-ime.sh` plus `tests/ime_live_probe.c` (public API only), runs as
  `make check-ime-live`, skips honestly with rc 0 when there is no parent niri,
  isolates fcitx5 on a private session bus so the user's instance is untouched,
  and hardens the run with bounded waits, signal-triggered teardown and a focus
  recheck immediately before injection. It is wired into the `ci.sh` live
  section. KWin+fcitx5 and Mutter+IBus live composition still lack their
  environments and remain listed as open.

## What is new in 1.3.0

1.3.0 closes the appendable items carried past 1.2 without touching the 2.0
structural horizon. It keeps every 1.2 signature, adds two functions and one
accessibility enum value, and adds a protocol-level test facility that changes
no public ABI. The annotated tag is created after the final gate run recorded in
[release.md](docs/release.md).

- **Fake-compositor wire verification.** `tests/fake_compositor.h` is an
  in-process Wayland server on a private socket: Pt connects to it as an
  ordinary client, so the shipped connection path runs unchanged. It decodes and
  records every request (object, interface, opcode, typed arguments) and injects
  server events, letting headless tests assert popup placement wire parameters,
  record the toplevel-icon submission, replay decoration modes, exchanges and
  drive text-input IME traffic including the v1 suppression path. A small C
  bridge, `tests/wire_driver.c`, exposes the facility to the C++, Python and Java
  suites, so raw key and pointer events now reach wrapped callbacks end to end in
  all three bindings instead of only being registration-tested.
- **IME visual completion.** The seven `preedit_hint` kinds render as graded
  second underlines (composition, selection, prefix/suffix, prediction, spelling
  and compose errors), and a preedit without hints is pixel-identical to 1.2.
  `pt_window_show_input_panel` and `pt_window_hide_input_panel` forward the
  text-input-v3 v2 requests, remember the desired state while IME is inactive or
  v1-only, replay it on reactivation, and report whether the state changed.
- **SVG icon sources.** `pt_icon_image` accepts SVG content alongside PNG through
  a vendored nanosvg decoder (zlib licensed, recorded in `THIRD_PARTY.md`), and
  hicolor lookup resolves `scalable/` directories. Rasterisation feeds the same
  per-size/scale cache; the supported SVG subset and the rejection rule for
  unsupported features are documented rather than implied. No public function
  was added: content is sniffed and the existing loaders are unchanged.
- **Spin-button accessibility role.** `pt_a11y_role_spin_button` is appended as
  value 36, moving the `pt_a11y_role_count` sentinel to 37. The number picker
  reports the spin-button role with no expandable state and the AT-SPI bridge
  maps it, which is the correction the 1.2 record anticipated.
- **C++ hardening.** The five owned getters free their C buffers through an RAII
  guard so a `std::bad_alloc` during a string or buffer copy cannot leak, and the
  `pt::popup` wrapper documents that it must be destroyed before its parent
  window.

## What is new in 1.2.0

1.2.0 is the first post-1.0 horizon wave: the controls and platform plumbing
most applications had to build themselves. It keeps every 1.1 signature and
adds 106 functions; the annotated tag is created after the final gate run
recorded in [release.md](docs/release.md).

- **Application-owned popups.** `pt_popup_create` builds a popup on the existing
  `xdg_popup` backend from a parent window and a content widget. It shows at an
  anchor rect in parent logical coordinates with flip/slide constrained
  placement, repositions, and dismisses on an outside click, Escape or a broken
  grab, reporting the dismissal through `pt_popup_set_on_dismiss`. Headless runs
  use the same in-window fallback as menus and tooltips.
- **Per-run font fallback.** `pt_app_set_font_fallbacks` installs an app-level
  chain and `pt_app_set_font_fallbacks_for_family` overrides it for one primary
  family. Text is split into HarfBuzz-script segments and each segment is shaped
  with the first face that covers it, falling back to Fontconfig's order when no
  chain is installed. Metrics take the tallest face in a line;
  `pt_app_font_uncovered_count` reports codepoints that still render as tofu.
- **Clipboard media.** `pt_window_clipboard_set_image_png` and
  `pt_window_clipboard_get_image_png` read and write `image/png` with the
  existing 1.5 s / 16 MiB transfer caps plus an 8192×8192 decoded-pixel cap;
  decode failures return NULL. The conversion between premultiplied ARGB and
  the straight-alpha PNG format is documented.
  `pt_window_clipboard_set_uri_list` writes CRLF, percent-encoded `file://`
  URIs with the drag-source encoder.
- **Column-layout persistence.** `pt_table_columns_save` and
  `pt_table_columns_restore` round-trip a versioned blob of column widths, order,
  visibility and keys for the shared table/tree column model. The application
  owns and stores the blob; unknown versions and corrupt input are rejected.
- **Icon images and toplevel icons.** `pt_icon_image_load_file`,
  `pt_icon_image_load_memory` and `pt_icon_image_from_name` create a
  reference-counted raster image (PNG, hicolor theme lookup, per-size/scale
  cache) for an icon button's image slot with optional tint, or for the window
  icon. `pt_window_set_icon` publishes it through `xdg-toplevel-icon-v1` where
  the compositor exposes that protocol; otherwise it is best-effort and the
  application still owns the image.
- **Pickers.** A searchable `pt_select` (popup list with item data, query
  filtering, "no matches" and a candidate cap), `pt_date_picker` (typed date plus
  a six-week calendar), `pt_number_picker` (spin box with range and step) and
  `pt_color_picker` (hex field, preview and preset palette). All four are
  editor fields with full keyboard paths, entry focus while the popup is open,
  silent programmatic setters, and accessibility value/expanded-state
  assertions. They are combo-box widgets at the core; the filterable combobox
  remains the separate in-window chooser.

## What is new in 1.1.0

1.1.0 keeps every 1.0 signature and adds an append-only extension surface for
applications that draw their own content, own input or talk to the compositor
directly. The source tree, tests, examples and documentation are complete; the
annotated tag is created after the final gate run recorded in
[release.md](docs/release.md).

- **Decoration negotiation.** The default is explicit client-side decoration.
  The enum is `pt_decoration_mode {pt_decoration_auto, pt_decoration_client,
  pt_decoration_server}`. `pt_window_set_decorated(false)` now means only "Pt
  draws no header"; it never requests server-side decoration.
  `pt_window_set_decoration_mode` chooses the request,
  `pt_window_get_decoration_mode` returns it, and
  `pt_window_get_effective_decoration_mode` returns what `configure` made
  authoritative. `pt_window_maximize` returns false when the compositor did not
  advertise the maximize capability, and the header button is disabled in the
  same case. See [Decoration policy](#decoration-policy).
- **Primary selection.** `pt_window_primary_set` and `pt_window_primary_get`
  back middle-click paste through `wp_primary_selection_unstable_v1` with the
  serial of the triggering event, and fall back to an in-process buffer off
  Wayland. `pt_window_primary_set_serial` passes an explicit serial from inside
  an event callback; a programmatic selection with no trigger serial returns
  false and sends nothing.
- **Custom paint.** `pt_widget_set_paint_callback` hands a widget a borrowed
  `pt_paint_context` with clip, rectangle, line, rounded-rectangle and text
  helpers (`pt_paint_*`), plus widget-scoped redraw requests. The callback is
  pure and idempotent, and the clip and reentrancy guards are always active, not
  behind a debug switch.
- **Offscreen snapshots.** `pt_window_paint_pixels` renders a full frame into a
  caller-owned `0xAARRGGBB` buffer without damage, redraw bookkeeping or overlay
  collection, so custom paint is testable headless.
  `pt_window_paint_pixels_ex` takes a `pt_paint_pixels_desc` with stride, scale
  and `pt_pixel_format` (only `pt_pixel_format_argb8888_premultiplied`) for
  fractional-scale and padded buffers.
- **Custom IME.** `pt_window_set_ime_callbacks`, `pt_window_set_ime_enabled` and
  `pt_window_set_ime_cursor_rect` let a terminal or canvas receive composed text
  without pretending to be an entry.
- **Descriptor watches.** `pt_app_watch_fd` folds pipes, eventfds and timerfds
  into the app's poll loop with borrowed-descriptor semantics;
  `pt_app_watch_fd_owned` transfers the descriptor and closes it on unwatch or
  app destroy. A second watch of the same descriptor is rejected, and
  `POLLHUP`/`POLLERR`/`POLLNVAL` always reach the callback as an error event.
- **Raw input callbacks.** A focus callback, a key callback that carries the xkb
  UTF-8 text and a normalized pointer-event callback run before widget
  dispatch, with a defined consume scope. Returning true consumes keys and
  pointer down/up/axis; enter, leave and motion are advisory and their return
  value is ignored.
- **Behaviour changes.** The default decoration request moved from server-side
  to explicit client-side; `pt_window_set_decorated(false)` no longer sends a
  decoration mode; snapshots no longer consume damage or deferred overlays;
  repeated fd watches are rejected; and paint-callback self-destruction, clip
  imbalance and reentrant snapshots have defined outcomes. The complete list is
  in the [1.1.0 release record](docs/release.md#110-validation-record-2026-11-22).

## What is new in 1.0

- **The frozen surface.** `VERSION` and `pt_version_string` are `1.0.0`, the
  SONAME is `libpt.so.1`, and the 412-function C ABI is the reviewed baseline in
  `api/pt.api`. The two symbols deprecated in 0.9 were removed by this freeze
  rather than carried into the 1.x series, so the 1.0 ABI is a deliberate subset
  of 0.10 and needs the new object name. Every addition after 1.0 is append-only.
- **Contracts in the headers.** Every public header opens with a `Header
  contract` block that states the UI-thread rule (getters included), ownership
  and lifetime of returned pointers, callback lifetimes, the single error model
  and the family exceptions: the native popup grab in
  `window.h`/`overlay.h`, offer and stream lifetimes in `dnd.h`, the three
  text-input state-push moments in `widget.h`, and the any-thread value helpers.
  The generated [API reference](docs/api-reference.md) renders the block as each
  module's preamble, and [architecture.md](docs/architecture.md) publishes the
  threading table by entry-point class.
- **The append-only ABI gate.** `tests/test_abi.c` pins every public enum value,
  accessibility state and modifier bit, sentinel and limit, the field order of
  the public structs, and version-macro agreement, so a renumbering fails
  `make check` instead of reaching a release. The audit's baseline diff remains
  the second half of the guard.
- **The three guides.** [Theming](docs/theming.md) documents all 38 color roles
  and 10 metrics, the resolution order, accent derivation and the measured
  WCAG contrast floors; [accessibility](docs/accessibility.md) covers role and
  state derivation, names, text, values, events, the AT-SPI interfaces and the
  contract for a different adapter; [migrating to 1.0](docs/migration-1.0.md)
  replaces the two removed drag helpers in C, C++, Python and Java and explains
  the token and selection changes since 0.9.
- **Text interfaces and a written RTL plan.** The AT-SPI bridge now treats
  labels and text areas as first-class text objects: labels are read-only,
  text areas are selectable and editable, and both preserve logical character
  offsets. Password entries report their length without exposing a character.
  [The RTL plan](docs/rtl.md) records the post-1.0 API shape, UBA and shaping
  pipeline, UI mirroring rules, offset mapping and conformance/test obligations;
  it is a plan, not a 1.0 promise.
- **A contrast-corrected palette.** The 1.0 defaults move the dark accent to
  `3568E0` and the light accent to `2559E6`, and darken the light danger,
  success and warning roles, so body text clears WCAG AAA and muted text,
  accent text, status text and the focus ring clear AA. `test_theme` asserts
  the floors rather than describing them.

## What changed in 1.0

1.0 freezes the 0.10 feature set without adding public API: the surface is 412
functions, the object is `libpt.so.1`, and `api/pt.api` is the reviewed
baseline. The release's own changes are:

- **Direct typing with text-input-v3.** Ordinary `wl_keyboard` text now reaches
  a focused entry even while text input is enabled; before the fix only an
  input-method commit could insert text, so typing looked dead until the IME
  composed something. Accelerator combinations are still excluded, and
  composition commits still handle preedit. `make check-typing` drives a focused
  entry through a virtual keyboard in a nested compositor and asserts the exact
  text.
- **Read-only text in the accessibility bridge.** Labels and buttons expose
  `org.a11y.atspi.Text` with `READ_ONLY`; entries and text areas expose
  `EditableText` and `SELECTABLE_TEXT`; a password's `GetCharacterAtOffset`
  reads the redacted visible string instead of leaking a character. The live
  bridge suite covers all three text paths.
- **A frozen, reviewed ABI.** The two deprecated drag helpers are gone, the
  baseline is `724 entries; 0 removed, 0 changed, 0 added`, and every binding is
  at 412/412 with an empty allowlist. `tools/release-check.sh` checks the
  version, SONAME, draft markers, baseline, reference and deprecations.
- **Live guards for what headless tests cannot see.** `make check-wayland` runs
  the popup checks and a 5 s focused-entry idle slice in a fullscreen nested
  compositor; `make check-typing` covers direct typing; a marquee-drag frame and
  a native popup surface joined the performance ceilings.
- **The 1.0 guides.** The migration, accessibility and theming guides are
  checked against the headers, and the RTL plan is written for post-1.0 work.

### Carried from 0.10

- **A combobox.** `pt_combobox_create` gives a field that carries a list of
  choices: `pt_combobox_select_only` behaves like a dropdown, and
  `pt_combobox_filterable` edits like an entry and filters the popup with a
  case-insensitive substring match. Items are managed with
  `pt_combobox_add_item`, `pt_combobox_insert_item`, `pt_combobox_remove_item`,
  `pt_combobox_clear` and per-item text, disabled flag and borrowed data pointer
  (`pt_combobox_set_item_data` / `pt_combobox_get_item_data`). The field carries
  `pt_combobox_set_text`, `pt_combobox_set_placeholder` and the two callbacks —
  `pt_combobox_set_on_change` for a committed choice and
  `pt_combobox_set_on_text_change` for every edit — and the popup with
  `pt_combobox_open`, `_close`, `_is_open` and `pt_combobox_set_popup_max_height`
  (260 px by default). Keyboard: F4 or Alt+Down opens, arrows and Page keys move
  the highlight, Enter or Space commits and closes, Escape closes unchanged, Tab
  closes and moves focus, and a select-only box does type-ahead over the item
  text while a filterable one leaves Home, End and space to the editor. A
  select-only box opens on a native `xdg_popup` when the compositor allows it, so
  the list can overflow the window edge; a filterable one stays in-window so the
  editor keeps focus.
- **One multi-row selection model for all three views.** Lists and trees gained
  the primitives the table already had — `pt_list_view_select_range`,
  `_select_toggle`, `_get_selection`, `_get_selection_count`, `_is_row_selected`,
  `_clear_selection` and the `pt_tree_*` mirrors, plus `pt_table_select_toggle` —
  with the same contract: both ends of a range are inclusive and clamped, the
  setter replaces the set and leaves the anchor and the primary row at its first
  row, toggling off the primary row moves it to the next selected row, and
  `pt_list_view_clear_selection` and `pt_tree_clear_selection` leave it at
  `pt_list_index_none`. The set stays separate from the primary row, and
  `on_select` fires only for the gestures that actually move it.
- **Gestures that match the model.** In a list, a table and a tree alike:
  Shift-click extends from the anchor, Ctrl-click toggles one row, a rubber band
  started on a row or in empty viewport space selects every row it
  geometrically intersects (and Ctrl held during the drag adds to the set
  instead of replacing it), Shift+arrow extends from the anchor, an unmodified
  arrow collapses the set to the row it lands on, and Ctrl+A selects the whole
  model. `tests/test_selection.c` drives the same
  gesture script, including bands that start on a row and in empty viewport
  space, through all three views, so a divergence fails the suite.
- **The states a screen reader needs.** New roles `pt_a11y_role_combo_box` and
  `pt_a11y_role_list_box` — a combobox reports the first, its popup panel the
  second — and new states `pt_a11y_state_expandable`, `pt_a11y_state_selectable`
  and `pt_a11y_state_multi_selectable`. The AT-SPI2 bridge maps them onto the
  numbers assistive tech expects, and the live test asserts multiselectable on
  the views, selectable and selected on a row, expandable/expanded on a branch
  and collapsed after a fold, and `GetRelationSet` on the data views.
- **Binding parity.** All 412 public functions are wrapped in C++23, Python
  ctypes and Java FFM, the audit gates the 0.10 surface (40 functions) per
  binding with an empty allowlist, and the suites run 344 C++ checks, 33 Python
  tests and 298 Java checks.
- **A 1.0 readiness gate.** `tools/release-check.sh` turns the checklist in
  [docs/api-policy.md](docs/api-policy.md) into a pass/fail report — version
  agreement, draft markers, baseline and reference counts, parity, deprecation
  inventory, examples — and prints the items only a person can close.
- **A quiet text input.** An empty `zwp_text_input_v3.done` is the input method
  acknowledging the surrounding text and caret rectangle Pt sends it; answering
  that acknowledgement asks for another one. Pt now ignores it unless a
  composition is in flight, so a window with a focused entry no longer redraws
  itself continuously and no longer floods assistive technology with selection
  changes: the same 5 s live slice went from 67,499 `selection_changed` events and
  2.13 s of CPU to 1 event and 0.65 s.

## What is new in 0.9

- **Design tokens, app-wide and per window.** The semantic color table grows
  from 24 to 38 roles (`pt_theme_surface_container`, `_surface_container_high`,
  `_outline`, `_outline_variant`, `_text_disabled`, `_accent_pressed`,
  `_control_pressed`, `_danger_soft`, `_success_soft`, `_warning_soft`,
  `_scrim`, `_skeleton`, `_skeleton_highlight`, `_selection_text`) and ten
  geometric metrics cover radii, focus ring width and offset, elevation, chip
  height and control height. `pt_app_set_theme_metric` sets them globally;
  `pt_window_set_theme_color`, `pt_window_set_theme_metric` and
  `pt_window_clear_theme_overrides` restyle one window without touching the app
  theme, and overrides resolve at paint time.
- **A complete state matrix.** Built-in controls now paint hover, pressed,
  disabled, busy and invalid states from the same tokens —
  `pt_widget_set_busy` and `pt_widget_set_invalid` — and keyboard focus draws a
  focus-visible ring instead of a plain outline. New `pt_chip_create` gives a
  pill with an optional glyph, selected state and remove button;
  `pt_skeleton_create` gives the shimmering loading placeholder. Segmented
  controls glide an indicator, sidebars draw a selection pill, and cards, menus
  and dialogs share the elevation tokens.
- **Data views you can edit in place.** Tables open an entry over a cell with
  `pt_table_begin_edit` (or a double click), commit through
  `pt_table_set_on_cell_edited` and route the editor through the normal undo
  path; `pt_table_select_range`, `pt_table_get_selection` and marquee or
  Shift-click selection keep a multi-row set independent of the primary row.
  Headers can be dragged into a new order through
  `pt_table_set_columns_reorderable` and `pt_table_set_on_columns_reordered`,
  and `pt_table_set_on_row_drag` hands a row drag to the application while the
  implicit grab is still alive. Trees rename nodes in place with
  `pt_tree_set_renamable`, F2 or a slow double click, and report the commit
  through `pt_tree_set_on_rename`.
- **Per-run typography.** A `pt_text_span` can now carry its own family
  (`pt_text_span_has_family`) and size (`pt_text_span_has_size`), and mixed-size
  runs share one baseline, so a heading run, a monospace run and a small
  superscript line up inside the same label.
- **The first deprecation.** `pt_window_begin_text_drag` and
  `pt_window_begin_uri_drag` were marked `pt_deprecated` with the migration path
  to `pt_window_begin_drag`, proving the API policy's deprecation cycle end to
  end: header attribute, audit, bindings, tests and documentation. The 1.0 freeze
  executed the recorded removal window, so both names and their three wrappers
  are gone and `pt_window_begin_drag` is the only drag source.
- **Visual regression.** `tests/test_visuals.c` paints chips, states, focus
  rings and styled runs headlessly and writes deterministic PNG snapshots under
  `build/snapshots` (`PT_SNAPSHOT_DIR`), asserting that a repeated paint is
  byte-identical and that the light palette differs from the dark one.
- **Binding parity.** All 374 public functions are wrapped in C++23, Python
  ctypes and Java FFM, and the audit now gates the 0.9 surface (44 functions)
  per binding. The binding suites run 286 C++ checks, 32 Python tests and
  248 Java checks.

## What is new in 0.8

- **Asynchronous structured drag data.** A drop target can now be registered
  with offer callbacks: `pt_drop_offer_mime_count`, `pt_drop_offer_mime`,
  `pt_drop_offer_has_mime`, `pt_drop_offer_actions` and
  `pt_drop_offer_selected_mime` describe everything the source advertises, and
  `pt_drop_offer_read` streams the accepted MIME type back in chunks through a
  `pt_drop_stream` (`pt_drop_stream_mime`, `_size`, `_active`, `_cancel`) on the
  event loop instead of inside the drop callback. A `max_bytes` cap turns a
  huge source into an error rather than an out-of-memory. On the sending side
  `pt_window_begin_drag` offers several MIME types at once and
  `pt_window_set_drag_finished_callback` reports the action the destination
  actually performed. The synchronous one-text drop path still works.
- **Styled text runs.** `pt_widget_set_text_spans` applies weight, slant, color
  and underline to byte ranges of a label or text area; spans are copied and
  clamped on set, survive wrapping and measurement, take part in selection and
  caret movement, and read back through `pt_widget_get_text_span`.
  `pt_widget_clear_text_spans` returns a widget to plain text. Full bidi
  paragraph layout stays post-1.0.
- **Deeper `text-input-v3`.** Widgets declare an input purpose
  (`pt_widget_set_input_purpose`: email, URL, phone, number, password,
  terminal), and preedit, commit, surrounding text, delete-surrounding-text and
  cursor-rectangle updates follow the protocol with IME-aware undo grouping and
  caret placement. A scripted protocol helper drives preedit, commit and
  cancellation headlessly, so the path is tested without a running input
  method.
- **Real AT-SPI data-view interfaces.** Tables and trees answer
  `org.a11y.atspi.Table`, `TableCell` and `Selection` with usable row, column
  and selection counts and object relationships, not only attributes; the live
  D-Bus test asserts the interfaces themselves.
- **Renderer and layout scaling.** The window keeps a damage index of up to
  eight coalesced tiles and repaints each under its own clip, so a localized
  update skips most of the visible-tree traversal instead of only most of the
  pixels. Layout invalidation records the dirty subtree and relayouts from the
  pinned ancestor when it is safe to. Rounded rectangles take a
  squared-distance fast path that calls `sqrtf` only inside the anti-aliasing
  band, and font face lookup is a hash table keyed by family, weight, slant and
  size. `bench-guards` adds a 4K damaged-frame profile and a 100,000-row table
  scroll profile.
- **Binding parity.** Every one of the 330 public functions is wrapped in
  C++23, Python ctypes and Java FFM, and `make check` fails on any gap. The
  binding suites now run 242 C++ checks, 31 Python tests and 216 Java checks.

## What is new in 0.7

- **Stable-keyed data views.** Lists and tables accept a non-zero `uint64_t`
  key per row. Selection follows the key across row-count changes, sorting and
  model refreshes; `pt_*_get_selected_key` and `pt_*_set_selected_key` expose
  it directly. A row-text callback supplies labels to keyboard type-ahead, and
  `pt_*_refresh_rows(first, count)` rebinds only the affected window without
  rebuilding the row pool.
- **Multi-column sorting and column control.** Tables report up to four sort
  columns with priorities and directions through
  `pt_table_set_on_sort_columns`; Shift-clicking headers extends the sort.
  Columns can be hidden, keep their width and cells while hidden, and retain
  application-set widths across rebinds. Header cells, row/column indices, sort
  keys and tree levels are published through AT-SPI.
- **Complete input lifecycle.** Windows report the active drag, cancel it
  programmatically and enforce a configurable drag timeout. Stylus eraser and
  barrel buttons join pressure, tilt and rotation. Trees support Home, End and
  Page Up/Page Down in addition to parent/child traversal.
- **Full binding parity.** Every one of the 311 public functions was wrapped in
  C++23, Python ctypes and Java FFM, and `make check` enforced 311/311 as a hard
  gate.
- **Documentation as a build artifact.**
  [docs/api-reference.md](docs/api-reference.md) is generated from the public
  headers with ownership, valid widget kinds, callback threading and failure
  contracts for every function; `make api-check` fails when it drifts. The
  compatibility policy includes a worked deprecation example and an expanded
  1.0 checklist.
- **New examples.** [data.c](examples/c/data.c) exercises keyed selection,
  batch invalidation and multi-column sort against a live model;
  [accessibility.c](examples/c/accessibility.c) shows roles, states,
  text/value exposure, event delivery and the AT-SPI bridge status.

## What is new in 0.6

- `pt_table_create`: a virtualized table on the list row recycler. Columns own
  titles, widths, alignment and resize handles; clicking a header cycles
  none → ascending → descending and calls the application sort callback, which
  owns the model and calls `pt_table_refresh`. Rows and cells are rebound per
  viewport, selection and activation are shared with the list, and the header,
  row and column roles are exposed to accessibility clients.
- `pt_tree_create`: collapsible hierarchy with stable node handles, expansion,
  depth and child accessors, application data per node, selection, keyboard
  Left/Right traversal and virtualized rows. Node text, hierarchy and expanded
  state are exposed as tree/tree-item roles.
- `pt_tabs_create`, `pt_sidebar_create`, `pt_icon_create` and
  `pt_icon_button_create`: a navigation shell with scrolling tab strips, an
  animated active indicator, sidebar selection with compact mode, and 21
  built-in vector glyphs that stay crisp at any scale. Icon buttons carry
  accessible names and tooltips.
- Interaction completion: `text/uri-list` drag sources with an RGBA drag image,
  two-contact pinch/rotate gestures that leave single-finger scrolling alone,
  and stylus pressure, tilt, rotation and tool events on the existing pointer
  and gesture paths.
- API-freeze preparation: a reviewed [API baseline](api/pt.api), exported-symbol
  and binding-parity audit run by `make check`, a documented
  [compatibility policy](docs/api-policy.md), and frozen 0.5 data-view and
  motion names.
- A [tabbed gallery](examples/c/gallery.c) with table, tree and navigation
  pages, plus workspace examples in
  [C++](examples/cpp/workspace.cpp), [Python](examples/python/workspace.py) and
  [Java](examples/java/Workspace.java).

## Requirements

- Linux with a Wayland compositor
- A C23 compiler (reference: GCC 16.2.1) and GNU Make
- wayland-client, wayland-cursor, xkbcommon, Fontconfig, FreeType, HarfBuzz and
  zlib development packages
- Optional: dbus-1 for the AT-SPI2 bridge, Python 3.10+, JDK 22+, and a C++23
  compiler for the bindings

On Arch Linux:

```sh
sudo pacman -S base-devel wayland wayland-protocols xkbcommon fontconfig \
    freetype2 harfbuzz zlib dbus
```

## Build and test

```sh
make -j"$(nproc)"          # library and C/C++ examples
make check                 # all headless C suites, binding suites, API audit and docs
make check-bindings        # only the C++ wrapper, Python ctypes and Java FFM tests
make check-wayland         # live suite: nested compositor, popups and idle guard
make check-typing          # hermetically inject keys into a nested compositor
make check-ime-live        # live niri+fcitx5 composition through text-input-v3
make bench                 # headless layout, full/partial paint and startup benchmarks
make perf-check            # benchmarks plus regression ceilings (4K, large-tree scroll)
make install-check         # staged install, SONAME/ABI and four-language consumers
tools/ci.sh                # CI-equivalent local gate, including sanitizers and tarball
tools/release-check.sh     # readiness report for the frozen release
```

`make check` runs headless and needs no compositor. It includes the C test
suites, the C++/Python/Java binding suites, the exported-symbol and API
baseline audit, the hard 576/576 binding-parity gate, the per-round 0.6–0.10
surface audits, a check that `docs/api-reference.md` matches the headers, and
the randomized property suite for text editing, URI parsing, data-view indices
and damage rectangles.
The live Wayland smoke test is not part of `make check`. `make check-wayland`
builds and runs it; when `niri` is installed it starts a fullscreen nested
compositor, focuses the nested window in the parent session and runs the test as
the only client of that seat, so the popup checks and the 5 s focused-entry idle
guard retry or skip instead of racing the desktop. It skips cleanly without a
display.
`make check-typing` guards the ordinary keyboard path while text-input-v3 is
enabled: when `niri` is installed it starts a nested compositor, focuses an
entry and injects keys through a virtual keyboard, asserting the entry received
exactly the typed text; it skips without a live session or niri.
`tools/ci.sh --quick` skips the sanitizer and tarball rebuilds; `make
perf-check` fails instead of just printing when a median regresses beyond its
guard ceiling. `tools/release-check.sh` is not part of `make check`: it reads
the tree and reports whether the 1.0 checklist in
[docs/api-policy.md](docs/api-policy.md) can be signed off, listing the items
only a person can close.

## Examples

| Language | Example | Run |
| --- | --- | --- |
| C | [hello.c](examples/c/hello.c) | `./build/examples/c/hello` |
| C | [gallery.c](examples/c/gallery.c) | `./build/examples/c/gallery` |
| C | [interaction.c](examples/c/interaction.c) | `make run-interaction` |
| C | [data.c](examples/c/data.c) | `make run-data` |
| C | [accessibility.c](examples/c/accessibility.c) | `make run-a11y` |
| C | [notes.c](examples/c/notes.c) | `make run-notes` |
| C | [combobox.c](examples/c/combobox.c) | `make run-combobox` |
| C | [rtl_gallery.c](examples/c/rtl_gallery.c) | `make run-rtl-gallery` |
| C | [ergonomics_gallery.c](examples/c/ergonomics_gallery.c) | `make run-ergonomics-gallery` |
| C++23 | [counter.cpp](examples/cpp/counter.cpp) | `make run-counter` |
| C++23 | [workspace.cpp](examples/cpp/workspace.cpp) | `make run-cpp-workspace` |
| C++23 | [combobox.cpp](examples/cpp/combobox.cpp) | `make run-cpp-combobox` |
| C++23 | [rtl_direction.cpp](examples/cpp/rtl_direction.cpp) | `make run-cpp-rtl-direction` |
| C++23 | [ergonomics.cpp](examples/cpp/ergonomics.cpp) | `make run-cpp-ergonomics` |
| Python | [counter.py](examples/python/counter.py) | `make run-python` |
| Python | [notes.py](examples/python/notes.py) | `python examples/python/notes.py` |
| Python | [workspace.py](examples/python/workspace.py) | `make run-python-workspace` |
| Python | [combobox.py](examples/python/combobox.py) | `make run-python-combobox` |
| Python | [rtl_direction.py](examples/python/rtl_direction.py) | `make run-python-rtl-direction` |
| Python | [ergonomics.py](examples/python/ergonomics.py) | `make run-python-ergonomics` |
| Java 25 | [HelloPt.java](examples/java/HelloPt.java) | `make run-java` |
| Java 25 | [Workspace.java](examples/java/Workspace.java) | `make run-java-workspace` |
| Java 25 | [Combobox.java](examples/java/Combobox.java) | `make run-java-combobox` |
| Java 25 | [RtlDirection.java](examples/java/RtlDirection.java) | `make run-java-rtl-direction` |
| Java 25 | [Ergonomics.java](examples/java/Ergonomics.java) | `make run-java-ergonomics` |

The C and C++ examples are built by `make`; the Python and Java run targets
build or locate the library automatically. All examples run both natively and
under a headless test environment.

## Use from C

```c
#include <pt/pt.h>

static void quit(pt_widget *button, void *data) {
    (void)button;
    pt_app_quit((pt_app *)data);
}

int main(void) {
    pt_app *app = pt_app_create("dev.pt.hello");
    if (!app) {
        return 1;
    }
    pt_window *window = pt_window_create(app, "Hello, Pt", 680, 440);
    pt_widget *card = pt_card_create(pt_window_root(window));
    pt_widget_set_padding(card, 28, 28, 28, 28);
    pt_widget_set_gap(card, 16);
    pt_label_create(card, "Hello, Pt");

    pt_widget *button = pt_button_create(card, "Quit");
    pt_button_set_on_click(button, quit, app);

    pt_window_show(window);
    int status = pt_app_run(app);
    pt_app_destroy(app);
    return status;
}
```

Build against an installed copy:

```sh
cc hello.c $(pkg-config --cflags --libs pt) -o hello
```

See [docs/quickstart.md](docs/quickstart.md) for layout, callbacks, entries,
themes, lifetime rules and headless testing.

## Bindings

The C ABI is the contract. All three bindings are hand-written, small, and
covered by `make check-bindings`.

```cpp
#include <pt/pt.hpp>

pt::app app("dev.pt.cpp");
pt::window window(app, "Hello", 480, 320);
pt::label(window.root(), "Hello, Pt");
window.show();
return app.run();
```

```python
import pt

app = pt.App("dev.pt.python")
window = pt.Window(app, "Hello", 480, 320)
pt.label(window.root, "Hello, Pt")
window.show()
raise SystemExit(app.run())
```

```java
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import pt.Pt;

try (Arena arena = Arena.ofConfined()) {
    MemorySegment app = Pt.app_create("dev.pt.java");
    MemorySegment window = Pt.window_create(app, "Hello", 480, 320);
    Pt.label(Pt.window_root(window), "Hello, Pt");
    Pt.window_show(window);
    Pt.app_run(app);
    Pt.app_destroy(app);
}
```

See [docs/bindings.md](docs/bindings.md) for callback lifetimes, compile and
run commands, and the exact wrapper surfaces.

## Install

```sh
make install PREFIX="$HOME/.local"
pkg-config --modversion pt
```

The install rules place the shared library, static archive, C and C++ headers,
relocatable `pt.pc`, and the Python/Java binding sources under the prefix. The
pkg-config file uses `${pcfiledir}` and is therefore relocatable and compatible
with `DESTDIR` staging. Link statically with `pkg-config --static --libs pt`.

Source tarballs can be produced with `make dist`, which also writes a
`.tar.gz.sha256` sidecar next to the archive; see
[docs/release.md](docs/release.md) for the release checklist.

## Performance

Headless reference measurements on an Intel i7-13700H (GCC 16.2.1, `-O2`,
1280x800). The closing 1.0.0 guard pass measured a 1,506-node tree with 300 dense
scroll rows at 2.292 ms layout and 13.727 ms full paint; a localized update that
damages one dense row repainted in 1.017 ms, 7.4% of the full-window cost, and
painted 15 of the 61 nodes a full frame visits. A 10,200-node expanded tree
scrolled in 2.562 ms per frame while materializing 13 row widgets, a
100,000-row table seek cost 10.763 ms over 22 materialized rows, and a dense 4K
full repaint took 35.941 ms with a 3.175 ms damaged row. The two M17 profiles
measured an 8.8-9.3 ms marquee-drag frame and a 1.20-1.29 ms popup-panel frame
against their 20 ms ceilings. Cold first frame, including Fontconfig/FreeType
initialization, was 8.990-10.760 ms at 800x600, and 300 ms of idle loop consumed
0.582-0.700 ms of CPU. `make perf-check` enforces regression ceilings for these
scenes; on a live compositor `make check-wayland` enforces the focused-entry
idle slice (0.6-14.9% of one core in the measured runs, under the 1.5 s per 5 s
ceiling), and a focused entry no longer redraws and re-announces its selection
continuously, which 0.10.0 fixed. Details, methodology and memory caveats are in
[docs/performance.md](docs/performance.md).

## Documentation

- [Quickstart](docs/quickstart.md)
- [C API reference](docs/api-reference.md)
- [Bindings](docs/bindings.md)
- [Architecture](docs/architecture.md)
- [Theming](docs/theming.md)
- [Accessibility](docs/accessibility.md)
- [Right-to-left and bidirectional text](docs/rtl.md)
- [Inline images in text](docs/inline-images.md)
- [Migrating to 1.0](docs/migration-1.0.md)
- [API compatibility policy](docs/api-policy.md)
- [Performance](docs/performance.md)
- [Comparison with other toolkits](docs/comparison.md)
- [Roadmap](docs/ROADMAP.md)
- [Release process](docs/release.md)
- [Engineering standards](docs/standards.md)
- [Compositor and protocol matrix](docs/compositor-matrix.md)

The quickstart, architecture, bindings, theming, accessibility, RTL/bidi,
migration and comparison guides also have
[Simplified Chinese translations](README.zh-CN.md); the generated API reference,
release records, roadmap, performance notes and policy pages remain
English-only.

## Limitations in 2.1

- Wayland only; there is no X11, Windows or macOS backend.
- The 1.x ABI is append-only: removals require a major release and a SONAME
  bump. The checked-in baseline, symbol audit and `tests/test_abi.c` make any
  drift explicit. Layout, painting and the event loop run on the calling
  thread only.
- The renderer is CPU-only. Dense full-window animation is comfortable on
  desktop-sized windows but is not a 4K/120 Hz GPU path; an accelerated backend
  is a post-1.0 candidate.
- Menus, tooltips, select-only combobox popups, application-owned popups and
  the picker popups use native `xdg_popup` surfaces when the backend supports
  them; dialogs, toasts and filterable combobox popups remain composited inside
  the window because they are modal, stacked or need the editor to keep focus.
  Native popups still need a configured xdg-shell window, so headless runs use
  the in-window fallback.
- Icon images are raster: PNG, SVG and SVGZ decoding plus hicolor theme lookup
  with a per-size/scale cache. The SVG subset is honest rather than complete —
  text, raster images, filters, masks, `clipPath`, `use`/`symbol`, markers,
  animation and the full CSS cascade are ignored, and a document with no
  drawable shape is rejected. `pt_window_set_icon` is best-effort:
  without `xdg-toplevel-icon-v1` the compositor may show its own fallback, and
  the application keeps ownership of the image.
- The pickers do not read the process locale. The date format is an explicit
  `pt_date_format`, the number field always uses `.` as the committed decimal
  separator (both `.` and `,` are accepted on input), and the colour field is
  hex. The select model is capped at 1024 items and 256 candidate rows, the
  colour palette at 64 presets, and the calendar always renders six weeks, so
  localization at the edges is the application's job.
- Font fallback chains are process-wide because the font cache is shared. An
  explicit chain is authoritative: a character none of its families covers
  renders as tofu even when another installed font would have it. Installing a
  chain applies to text laid out afterwards and repaints the calling app.
- Table column layouts are saved as an opaque versioned blob that the
  application stores. Widths, order, visibility and keys round-trip; there is no
  schema migration across versions, so an unknown version is rejected and the
  current layout stays in place.
- The AT-SPI2 bridge is opt-in (`pt_app_enable_a11y`), requires a build with
  `dbus-1`, and exposes the widget tree rather than arbitrary painted
  content. Screen readers see Pt widgets but not custom drawing without
  accessibility metadata.
- Drag sources and structured drop offers need the same implicit pointer grab
  as any Wayland drag, so `pt_window_begin_drag` only succeeds while a press is
  active, and a headless run has no drag compositor to talk to. Streamed reads
  deliver bytes; decoding JSON or a URI list is the application's job. Touch
  pinch and rotate measure the first two tracked contacts; extra contacts are
  tracked but do not change the gesture metrics. Stylus support depends on
  `tablet-v2` from the compositor.
- Styled runs cover weight, slant, color, underline, per-run font family and
  per-run size over byte ranges of labels and text areas. There are no inline
  images, no per-run stretch or optical alignment, and no per-run bidi
  direction: `pt_text_span` stays frozen as styling only. Paragraph direction
  comes from the widget or window, and an opposite-direction phrase inside a
  paragraph uses Unicode isolate controls. The text area edits plain text with
  one logical range while wrapped lines may have different paragraph bases.
- Bidi and RTL have documented limits rather than hidden fallbacks. Table
  horizontal wheel scrolling is device-dependent as of 2.2: the axis is routed
  to tables (delta × 48, sign flipped for RTL), and tilt wheels or trackpad
  sideways swipes emit it while a plain vertical wheel does not; the scrollbar
  and keyboard always work. Shift+arrow across a soft wrap in RTL and a
  multi-line mapped selection are resolved in 2.2. `pt_text_span` has no
  direction field. Missing Arabic/Hebrew glyphs are documented tofu with
  `pt_app_font_uncovered_count`, not a hard error; install a covering font such
  as Noto Sans Arabic/Hebrew or DejaVu Sans. Bidi control characters can
  reorder displayed text (the Trojan-Source class of problem): 2.1 exposes
  `pt_bidi_control_inventory` and `pt_bidi_sanitize` for it, but sanitization
  is opt-in and no text path calls it automatically, so an application that
  accepts untrusted text has to sanitize before display, logging or storage.
  The locale hint only selects a default direction; text, dates, numbers and
  pickers stay locale-independent. Details are in the
  [RTL and bidi guide](docs/rtl.md).
- Table columns keep application-set widths that survive rebinds and row
  recycling, can be hidden, are resizable by dragging and can be reordered by
  the user. Sorting reports up to four priority-ordered columns, and cell
  editing plus multi-row selection are in place, but the application still owns
  and reorders the model and the table never sorts rows by itself. The rubber
  band selects every row it intersects, in a list, a table and a tree alike, and
  it can start on a row or in empty viewport space below the last row; a plain
  press there clears the old set and never activates the row that was primary
  before it, while Ctrl keeps the set as the additive base. In a tree it ranges
  over visible rows, so a collapsed branch is swept as the one
  row it shows. Tree nodes hold plain text and rename in place; they do not
  carry custom cell widgets.
- The combobox popup materializes one row per matching item inside a scroll
  area, so a model with tens of thousands of choices belongs in a list view;
  filtering is what keeps the popup short. Item data pointers are borrowed and
  move with the item when another is inserted or removed.
- List, table and tree rows are materialized by Pt and must not be destroyed by
  the application; custom cells are added through the row widget passed to the
  bind callback.
- Fontconfig startup costs dominate first-frame memory: 24.7-27.8 MiB RSS but
  10.2-12.8 MiB PSS on the reference machine, because font data is shared with the
  system. Steady-state widget/UI memory itself is small.

## Decoration policy

Pt draws its own 44 px title bar, and it treats "who decorates the surface" and
"does Pt draw a header" as separate decisions. The default is explicit
client-side decoration (`pt_decoration_client`): Pt asks `xdg-decoration`
for client-side mode at the first commit, because its header, edge resize zones
and hit testing are built for it.

`pt_window_set_decorated(window, false)` is a drawing switch. The window gets no
Pt header and no Pt edge zones, and it never asks the compositor for a
decoration. Server-side decoration is an explicit opt-in:

```c
pt_window_set_decoration_mode(window, pt_decoration_server);
```

`pt_decoration_auto` follows the framework default, which is client-side
decoration through the 1.x series. The request is never a guarantee: the mode returned by
`configure` is authoritative, and `pt_window_get_effective_decoration_mode`
reports it separately from the requested mode returned by
`pt_window_get_decoration_mode`. Sway forces server-side decoration on tiled
windows even after a client-side request, niri honours client-side but draws a
focus ring or border rather than a title bar for server-side, and Mutter and
Weston expose no decoration manager at all, so they get Pt's header. Fullscreen
hides the header in every mode, and tiled windows drop their corner radius and
shadow. Borderless client-side windows keep their 6 px edge hot-zone so they
stay mouse-resizable. Per-compositor evidence and quirk ids are in
[compositor-matrix.md](docs/compositor-matrix.md).

## License

Apache License 2.0. Vendored Wayland protocol XML retains its MIT headers; see
[NOTICE](NOTICE) and [THIRD_PARTY.md](THIRD_PARTY.md).
