# Pt roadmap

This roadmap is a living document. A milestone is only closed when its code,
documentation and automated tests are in the repository and passing on the
reference toolchain.

Reference environment: Linux/Wayland, GCC 16.2.1, C23, GNU Make, Python 3.14,
OpenJDK 25, niri 26.04.

## Product constraints

- **Small by design.** One shared library, no GL/GTK/Qt runtime, no daemon.
- **Predictable.** Retained-mode widget tree, single UI thread, explicit
  lifetimes, no hidden global state in the public API.
- **Crisp.** FreeType/HarfBuzz text, fractional scaling, integer fallbacks.
- **Contemporary.** Token-driven visual design, complete control states,
  keyboard and pointer parity, restrained motion rather than decoration.
- **Bindable.** A small flat C ABI is the contract for C++, Python and Java.
- **Test-first infrastructure.** Every layer has headless tests; display tests
  are a thin smoke layer.
- **Naming.** Public and private C identifiers use snake_case. The only
  exception is the reserved `PT_` acronym in documentation.

## Status at the 1.0 freeze

| Milestone | Status |
| --- | --- |
| M0 Research and architecture | done |
| M1 Repository and core | done |
| M2 Renderer and typography | done |
| M3 Wayland platform | done |
| M4 Layout and widgets | done |
| M5 Text input, IME and clipboard | done |
| M6 Bindings and examples | done |
| M7 Release readiness | done; owner Git commit/tag pending |
| M8 Modern controls and editing | done |
| M9 Overlays, accessibility and damage | done |
| M10 Native popups, accessibility bridge, DnD and gestures | done (0.4.0) |
| M11 Data views, multiline text and motion | done (0.5.0) |
| M12 Tables, navigation shell and interaction completion | done (0.6.0) |
| M13 Binding parity, API documentation and 1.0 stabilization | done (0.7.0) |
| M14 Text input depth, accessible data views and renderer scaling | done (0.8.0) |
| M15 Design tokens, data-view editing and the first deprecation | done (0.9.0) |
| M16 Surface completeness, editing polish and the 1.0 readiness gate | done (0.10.0) |
| M17 The 1.0 freeze: rc, removals and the guide sweep | done (1.0.0) |

## M0 - Research and architecture (done)

- Surveyed Wayland client architecture, `wl_shm`, `xdg-shell`, damage/frame
  pacing, fractional scaling, decorations, text shaping and FFI approaches.
- Selected a retained-mode design with a software renderer for v0.
- Recorded decisions and rejected alternatives in
  [architecture.md](architecture.md).

## M1 - Repository and core (done)

- Apache-2.0 repository, Makefile, install/uninstall, `make dist`, pkg-config
  and SONAME versioning.
- Public header layout (`include/pt/*.h`), opaque types, thread-local error
  handling, version macros.
- Color math, geometry, time, bounded strings, logging, allocation helpers.
- Headless test harness with deterministic teardown.

## M2 - Renderer and typography (done)

- Premultiplied ARGB8888 canvas with clipping, rounded rectangles, borders,
  circles, lines, gradients, soft shadows and PNG output.
- FreeType rasterization, HarfBuzz shaping, Fontconfig fallback, per-face glyph
  cache and UTF-8 wrapping/ellipsis/baseline handling.
- Fractional-scale painting: device-space pixels, logical layout coordinates.

## M3 - Wayland platform (done)

- Registry, compositor, `wl_shm`, `xdg-shell`, frame-callback pacing.
- Three-buffer shm pool with release tracking and resize-safe recreation.
- Integer scaling plus `fractional-scale-v1` and `viewporter`.
- `xdg-decoration` negotiation with a Pt-drawn 44 px client-side header when
  the compositor permits it.
- Seat: keyboard, pointer, wheel, xkb keymap, key repeat, cursor themes.
- Window move/resize/maximize/minimize/close and close-callback vetoes.
- One-shot timers, `pt_app_run_for` and a blocking `poll()` loop.
- Damage on state change; no idle repaint.

## M4 - Layout and widgets (done)

- Flex row/column layout with gap, padding, margin, grow, min/max sizes.
- Stretch/start/center/end alignment and six main-axis justify modes.
- Core widgets, focus traversal, hit testing, dark/light themes, accent tokens,
  hover/press animations and focus rings.
- Paint culling against the active clip with a 48 px shadow/focus margin.

## M5 - Text input, IME and clipboard (done)

- Editable entry: caret, UTF-8-aware editing, Home/End, Backspace/Delete,
  submit, password bullets, horizontal caret scrolling.
- `zwp_text_input_v3` preedit/commit handling and caret rectangle reporting.
- `wl_data_device`/`wl_data_source` clipboard with UTF-8 MIME negotiation,
  timeout and size caps.
- Window-level key callback, Tab/Shift+Tab traversal, XKB keysyms/modifiers and
  client-side key repeat.

## M6 - Bindings and examples (done)

- C++23 header-only wrapper, Python ctypes binding and Java FFM binding.
- Examples for all four language surfaces, including the full C gallery.
- `make check-bindings` runs all three binding suites headlessly.
- Installed bindings and C++ header are part of the prefix layout.

## M7 - Release readiness (validation complete; tag pending)

Completed:

- Quickstart, bindings, architecture, performance and comparison documents.
- `make install`/`uninstall`, relocatable pkg-config, SONAME and tarball target.
- Benchmarks for startup, idle CPU, memory, layout and paint with documented
  methodology and caveats.
- Comparison with GTK 4, Qt Quick, Slint, egui, LVGL and Nuklear.
- Clean-build, sanitizer, staged-install and tarball validation recorded in
  [release.md](release.md).

Remaining for the tag:

- Create the initial Git commit and annotated tag. The repository currently has
  no commits because no Git author identity is configured on the build machine;
  the owner must set `user.name`/`user.email` (or provide an equivalent identity
  policy). Build scripts never change Git configuration.

## M8 - Modern controls and editing (done, 0.2.0)

Design system:

- Refreshed dark and light palettes with deeper backgrounds, tonal surfaces and
  a calmer accent; added `surface_sunken`, `control`, `control_hover`,
  `accent_soft`, `selection` and `track` tokens.
- Accent-derived hover/focus/soft/selection colors, a softer focus ring drawn
  outside the widget bounds, card gradients and a new `pt_button_soft` variant.

Controls:

- Checkbox and radio with animated check marks, sibling radio exclusivity and
  one shared checked-state API.
- Determinate and indeterminate progress, an animated spinner and a segmented
  control with keyboard focus and index callbacks.
- Tightened entry surfaces, toggle track/knob rendering and slider fill glow.

Text editing:

- UTF-8 byte-offset selections with drag, double-click word and triple-click
  select-all; Shift+arrows/Home/End; select-aware insert, delete, cut and paste.
- Ctrl+A/C/X/V/Z/Y, 32-snapshot undo/redo, IME preedit integration and a public
  clipboard API with explicit `pt_free` ownership.
- Scroll offset get/set/max APIs for programmatic navigation and tests.

Verification and bindings:

- New `test_controls` suite plus expanded C++/Python/Java coverage; 302 C checks
  across eight executables and 67 binding checks plus 10 Python test cases.
- Gallery rebuilt around the new controls and both color schemes.
- All documentation, version strings and benchmarks updated to 0.2.0.

## M9 - Overlays, accessibility and damage (done, 0.3.0)

Goal: make Pt apps feel like first-class desktop citizens without growing the
core into a full platform abstraction.

Overlay layer:

- Window-owned z-ordered overlay stack (`toasts < menus < dialogs < tooltips`)
  composited in the parent window with modal scrim, clipping and damage.
- Public menus (items, shortcuts, separators, anchor placement, Escape and
  outside-click dismissal), modal dialogs (trap Tab, Escape, actions, focus
  restore) and a stacking, auto-expiring toast queue.
- Per-widget delayed tooltips, dismissed on pointer leave, menu opening or
  window blur.
- Action callbacks run after the overlay has closed and overlays use deferred
  garbage collection, so callbacks may reopen or destroy overlays safely.
- Keyboard traversal for menu items and dialog actions, plus Text/IME routing
  to the focused widget inside a dialog.

Accessibility model:

- Roles derived from widget kind, with application overrides for role, name
  and description.
- State bits for visible/enabled/focusable/focused/checked/selected/editable/
  expanded/busy/modal; text and caret/selection exposure; slider, progress,
  checkbox and segmented values.
- Synchronous app-level focus, state, text, selection, value, name and
  structure events, including dialogs and menu items.
- Password entries hide text but expose the caret. Headless role/name/state/
  text/value/event tests cover the model.

Damage tracking and paint reuse:

- Logical damage rectangles collected from widget setters, hover/focus changes,
  typing, animations and overlay changes; per-frame union with a sticky
  full-window flag.
- Persistent backing canvas, clipped partial repaint and per-`wl_shm`-buffer
  pending damage so triple buffering stays correct; `wl_surface.damage_buffer`
  on v4+ with a full-surface fallback, and no attach/commit for empty frames.
- Per-font LRU text shape cache keyed by FNV-1a hash plus byte comparison,
  bounded to 256 entries, 256 KiB and 8 KiB per text.
- New damage and shape-cache tests; localized row repaint measures about 1.1 ms
  versus 15.0 ms for a full 1280x800 frame in the dense benchmark.

Touch:

- `wl_touch` down/up/motion/cancel mapped onto existing pointer semantics, with
  single-touch id tracking, cancellation release and clean capability removal.
- Touch-to-pointer tests, including ignored second touches and no action on
  cancellation.

Verification and bindings:

- 485 C checks across eleven executables; C++ 49, Python 13 unittest cases and
  Java 54 checks, all headless.
- Gallery shows menus, a modal dialog, toasts, tooltips, accessible names and
  descriptions; the C++, Python and Java examples exercise overlays too.
- All documentation, version strings and benchmarks updated to 0.3.0.

Deferred to 0.4:

- Native `xdg_popup` surfaces and popup grabs; 0.3 overlays are clipped to the
  parent window.
- The AT-SPI2 D-Bus bridge and a live accessibility-bus smoke test; 0.3 ships
  the platform-neutral model and event stream.
- External drag-and-drop and richer touch gestures (pinch, rotate, momentum).
- A real popup path in the Wayland smoke test.

## M10 - Native popups, accessibility bridge, DnD and gestures (done, 0.4.0)

Goal: connect the 0.3 in-window model to the desktop: real popup surfaces, a
real accessibility bus and real cross-application drag and drop.

### 1. Native popup surfaces

- `xdg_popup` for menus and tooltips with anchor/gravity, flip/slide
  constraints, dismiss-on-click-outside and popup grabs.
- Shared overlay content and lifecycle with the in-window fallback; choose the
  surface path per overlay kind and compositor capabilities.
- Keyboard routing and focus restoration across popup and parent surfaces;
  multi-window ownership and scale changes.
- Wayland smoke coverage for a real popup where the compositor supports it.

### 2. AT-SPI2 bridge

- Opt-in D-Bus bridge at app creation, so the core keeps no hard runtime
  dependency on accessibility daemons.
- Map roles, names, descriptions, states, text/caret/selection and values to
  AT-SPI2 objects; forward the existing event stream.
- Keyboard-only traversal and modal semantics verified against a live bus in a
  test that skips cleanly when none is available.

### 3. External drag and drop

- `wl_data_device` drag enter/motion/leave/drop with MIME negotiation and drag
  images.
- Public drop-target widget API and drag source integration for entries and
  custom widgets, with clear ownership for received data.
- Clipboard and DnD share the existing data-source path without regressing
  copy/cut/paste.

### 4. Input refinements

- Pinch/rotate gesture events, momentum scrolling and long-press where the
  pointer path cannot express them.
- Tablet/stylus tool events and pressure as a stretch goal.

### 5. Completion criteria

- Native popup, accessibility-bus and DnD suites pass headlessly; live smoke
  tests cover a popup and the AT-SPI bus when available.
- Gallery demonstrates a native menu and tooltip, an external drop target and
  an accessible form.
- C++, Python and Java bindings expose the new APIs with tests.
- Documentation and benchmarks updated; version strings become 0.4.0.
- No regression in the 0.3 suites, idle CPU or localized damage performance.

### Delivered

- `xdg_popup` menus and tooltips with positioning, flip/slide constraints,
  popup grabs, viewport scaling and repositioning, plus the in-window fallback
  and `pt_window_set_popup_policy`.
- An opt-in AT-SPI2 bridge over D-Bus serving Accessible, Application,
  Component, Action, Text, EditableText and Value, with change events and a
  live-bus smoke test that skips when no session bus is available.
- External drag and drop with drop targets, drag-enabled entries,
  `pt_window_begin_text_drag` and MIME text negotiation.
- Swipe/pinch/hold gestures, synthesized long press and kinetic scrolling in a
  unified interaction layer, with precise event-loop wakeups.
- C++, Python and Java bindings, the interaction example, the expanded gallery
  index and `test_gestures` (132 checks).

Deferred to 0.5: drag images, file-list transfers, touchscreen multi-contact
gestures and stylus/tablet tools.

## M11 - Data views, multiline text and motion (done, 0.5.0)

Goal: make Pt comfortable for content-heavy tools without giving up the small
C ABI or the software-rendered, on-demand frame model.

### 1. Virtualized data views

- A model-driven list/table with row and cell callbacks, recycling and
  viewport-only layout so thousands of rows stay cheap.
- Selection, keyboard navigation, accessible rows/cells and scroll integration
  shared with the existing scroll container.
- A minimal default row renderer plus application-defined cells.

### 2. Multi-line text editing

- A wrapped text area sharing the entry editing core: caret/selection,
  word/select-all gestures, clipboard, undo/redo, IME preedit and scrolling.
- Vertical caret movement, Home/End behavior across visual lines and a
  grow-to-content mode for notes and logs.
- Accessible Text/EditableText coverage for the new widget.

### 3. Responsive layout

- Flex wrap and grid containers with gap, span and alignment rules for
  responsive cards, forms and dashboards.
- Layout tests for fractional scales, min/max constraints and nested clipping.

### 4. Motion and design language

- Public easing curves and value-transition helpers for custom widgets,
  reduced-motion awareness and interruption-safe animation state.
- Refreshed control states and tokens where the new layouts expose gaps.

### 5. Interaction and accessibility completion

- Drag images and `text/uri-list` file transfers; drag-and-drop polish across
  menus and popups.
- Touchscreen multi-contact gestures, stylus pressure/tool events and
  long-press context-menu ergonomics.
- AT-SPI action activation and editable-text write methods; validate with a
  live Orca session when available.

### 6. Completion criteria

- New data-view, text-area, wrap/grid and animation suites pass headlessly.
- Bindings and examples cover the new widgets; gallery gains a data view and a
  notes panel.
- No regression in the 0.4 suites, idle CPU, localized damage performance or
  popup behavior; version strings become 0.5.0.

### Delivered

- `pt_list_view_create`: a viewport-virtualized flat list with row recycling,
  one row of overscan, bind/select/activate callbacks, keyboard navigation,
  selection persistence, `pt_list_view_scroll_to_row` and list/list-item
  accessibility roles. Rows are list-owned widget trees, so custom cells reuse
  the ordinary widget and layout path.
- `pt_text_area_create`: multiline editing on the shared entry core with greedy
  wrapping, UTF-8 byte offsets, selection and word gestures, clipboard,
  undo/redo, IME preedit, caret-goal Up/Down, scrolling, caret visibility on
  typing only and a grow-to-content height mode. Entries and text areas expose
  the text-frame role.
- `pt_wrap_create` and `pt_grid_create`: responsive containers with independent
  row/column gaps, equal-width grid columns and per-child grid spans, composing
  with min/max sizes, nested clipping and the existing alignment rules.
- `pt_ease`, `pt_ease_value`, caller-owned `pt_transition` and app-level
  reduced motion (`PT_REDUCED_MOTION` supplies the default). Transitions
  retarget from the current value, and framework controls snap when reduced
  motion is enabled.
- Binding parity in C++, Python and Java, a C notes example and a Python
  counterpart, gallery index entries, and `test_flex_grid`, `test_text_area`,
  `test_data_view` and `test_motion`. The core suite is 1,019 checks in 18
  binaries; bindings run 86 C++ checks, 18 Python tests and 84 Java checks.

Deferred to 0.6: tables with columns/headers/sorting, tree expansion, drag
images and `text/uri-list`, multi-contact touch and stylus events.

## M12 - Tables, navigation shell and interaction completion (done, 0.6.0)

Goal: finish the content-heavy and navigation surfaces that turn Pt from a
widget kit into a comfortable desktop-tool framework, without widening the
core ABI beyond what the bindings can audit.

### 1. Table and tree data views

- Extend the virtualized list into a table with columns, headers, per-column
  alignment and resize-by-drag, sharing row recycling and selection.
- Header click sorting with an application sort callback and stable visual
  sort indicators; multi-column models stay application-owned.
- A collapsible tree view built on the same row recycler, with expand/collapse
  state, indentation, keyboard Left/Right and accessible hierarchy.
- Table/tree accessibility roles, row/column indices and header labeling.

### 2. Navigation shell

- Tabs with keyboard traversal, overflow and an active-indicator animation
  that honours reduced motion.
- A sidebar/navigation list pattern with icon and label rows, selection and
  compact/expanded states.
- Built-in vector glyphs for common actions (chevron, search, menu, close,
  check, folder, document) so navigation does not require image assets.
- Icon buttons with accessible names and consistent hit targets.

### 3. Interaction completion

- Drag images and `text/uri-list` file-list transfers; drop targets receive
  URI lists with explicit ownership.
- Multi-contact touch: pinch/rotate from two touch contacts and a gesture
  stream that does not steal single-finger scrolling.
- Stylus/tablet tool events and pressure, mapped into the existing pointer
  and gesture paths without a second input model.

### 4. API freeze preparation

- Publish a deprecation and compatibility policy for the 1.0 ABI; audit
  exported symbols and enum stability.
- Generate a binding-parity report from the headers so missing C++/Python/Java
  wrappers are visible in CI.
- Freeze names for the 0.5 data-view and motion APIs and resolve any awkward
  signatures found while building the table.

### 5. Completion criteria

- Table, tree, tab, sidebar and icon-glyph suites pass headlessly; drag images
  and URI lists are exercised against a live compositor when available.
- Bindings and examples cover tables and navigation; the gallery gains a
  table/tree view and a tabbed shell.
- No regression in the 0.5 suites, idle CPU, localized damage performance,
  popup behavior or virtualized-list memory bounds; version strings become
  0.6.0.
- The 1.0 compatibility policy and symbol audit are documented and checked by
  `make check`.

### Delivered

- `pt_table_create`: virtualized header/row/cell pool, fixed column widths with
  drag resizing, per-column alignment and titles, row height and header height,
  bind/select/activate/sort callbacks, sort direction and chevron indicators,
  selection clamping, keyboard navigation, horizontal and vertical scrolling,
  and table/row/cell accessibility roles.
- `pt_tree_create`: application-owned node hierarchy with text, parent/children,
  depth, expanded state and user data; a flattened visible-node array rendered
  through a recycled row pool; expand/collapse, selection, Left/Right keyboard
  navigation, row height and indent, scroll-to-node, and tree/tree-item roles.
- `pt_tabs_create` and `pt_sidebar_create`: keyboard-navigable tab strip with
  removable pages, page titles and change callbacks; sidebar icon/label items,
  header, compact icon-rail mode and selection.
- 21 built-in vector glyphs, `pt_icon_create`, `pt_icon_button_create` and icon
  setters on regular buttons, all with accessible names and shared hit targets.
- Interaction completion: `text/uri-list` drag sources, URI parsing helpers,
  custom RGBA drag images, multi-contact touch that synthesizes pinch/rotate
  without stealing single-finger scrolling, and tablet-v2 stylus events with
  pressure, distance, tilt and rotation mirrored into the pointer path.
- API freeze tooling: `api/pt.api` baseline (529 entries), `tools/api-audit.py`,
  `make api-check`, exported-symbol verification, a binding-parity report and
  [api-policy.md](api-policy.md). The 84 functions added in 0.6 are wrapped in
  all three bindings.
- Tests: `test_table`, `test_tree`, `test_navigation`, expanded
  `test_dnd`/`test_gestures`, and binding suites for all 0.6 surfaces. The core
  suite runs 1,474 checks in 21 binaries; bindings run 147 C++ checks, 24
  Python tests and 142 Java checks.
- Examples and docs: the C gallery gains Data and Navigation tabs; the
  C++/Python/Java workspace examples build a tabbed table/tree shell;
  quickstart, bindings, architecture, comparison, performance, release and API
  policy documents are updated.
- Performance: `bench/views.c` measures a 10,000-row table plus a 1,000-node
  tree at 93 materialized widgets, 0.005-0.006 ms/frame layout, 10.2-11.7
  ms/frame full paint and 0.150-0.180 ms/frame one-cell damage paint (1.5%).

Deferred beyond 0.6: multi-column sorting, keyed row updates, variable row
heights, styled text, a complete function-by-function API reference and
closing the older binding gaps.

## M13 - Binding parity, API documentation and 1.0 stabilization (done, 0.7.0)

Goal: make every documented public function reachable from every binding,
finish the reference documentation and harden the ABI and performance envelope
so 1.0 can be a stability release rather than a feature release.

Delivered:

### 1. Complete binding parity

- All 311 public functions are wrapped in C++23, Python ctypes and Java FFM;
  the parity report is a hard `make check` gate with an empty reviewed
  allowlist, and the suites carry 205 C++ checks, 25 Python tests and 188 Java
  checks.
- The new wrapper groups include window controls, bounds and measurement,
  image/drag helpers and overlay lifetimes. The raw-C callback convention is
  unchanged: no runtime bridge, JNI or generated glue was introduced.

### 2. Data-view and input depth

- Keys, keyed selection and batch refresh: selection follows a stable
  `uint64_t` key across reorder, refresh and row-count changes while the key
  exists; `pt_*_refresh_rows` rebinds only the visible intersecting rows.
- Multi-column sort state with priority-ordered callbacks, Shift-click header
  behavior and visible sort reporting; columns have persistent widths, can be
  hidden and keep their cells while hidden.
- AT-SPI reports table sort columns and header/cell indices, row selection and
  tree level/expanded state; the live bridge test asserts these attributes.
- Type-ahead selection with row-text callbacks, tree Home/End/Page Up/Page Down
  traversal, stylus eraser and barrel buttons, and drag active/cancel/timeout
  APIs.

### 3. API reference and examples

- `docs/api-reference.md` is generated from the public headers for all 311
  `pt_api` functions, with ownership, valid widget kinds, callback threading
  and failure contracts; `make api-check` fails when it drifts.
- Runnable data-view and accessibility examples join the gallery and workspace
  examples; the C++/Python/Java bindings have full-surface examples and tests.
- Quickstart, bindings, architecture, comparison, performance, release and API
  policy documents are at the 0.7 baseline. The policy includes a worked
  deprecation example and the 1.0 checklist.

### 4. Hardening and release engineering

- `tools/ci.sh` runs the werror build, all suites, API/binding audits, the
  performance guards, sanitizers, staged-install four-language consumers,
  reproducible dist and an extracted-tarball build.
- `tests/test_property.c` adds 454,380 deterministic property checks for UTF-8
  editing, URI-list parsing, table/tree index clamping and damage arithmetic.
- `bench/guards.c` records median dense, startup, data-view, 4K and large-tree
  scroll frames and fails `make perf-check` beyond generous regression
  ceilings.
- `make dist` is reproducible under `SOURCE_DATE_EPOCH`; `tools/install-check.sh`
  verifies the SONAME, exported ABI, relocatable pkg-config and installed
  C/C++/Python/Java consumers.

### 5. Completion criteria

- All bindings report 311/311, and `make check` fails on parity or ABI drift.
- The API reference covers all public functions and matches the checked-in
  baseline; examples exercise every widget family.
- Sanitizer, installed-consumer, tarball and performance checks pass on the
  reference environment; no 0.6/0.7 regression in idle CPU or damage cost.
- Version strings are 0.7.0 and the 1.0 release checklist is documented.

## M14 - Text input depth, accessible data views and renderer scaling (done, 0.8.0)

Goal: close the remaining interaction gaps that 0.7 documents as limitations
and remove the largest renderer and layout overheads before the 1.0 ABI freeze.

Delivered:

### 1. Text input depth

- `text-input-v3` is complete for the in-window editing path: preedit with an
  explicit cursor range, commit, surrounding-text query, delete-surrounding-text
  and cursor-rectangle updates reach the focused entry, with the 0.7
  preedit/commit behaviour as fallback when no input method is attached.
- Widgets declare what they expect through `pt_widget_set_input_purpose` and
  `pt_widget_get_input_purpose` (normal, email, URL, phone, number, password,
  terminal); password masking stays on `pt_entry_set_password`.
- An IME commit group is one undo step, and the caret lands where the
  composition ended. `test_ime` drives preedit, commit and cancellation through
  a scripted protocol helper, so the path is covered headlessly (62 checks).

### 2. Rich text foundation

- `pt_widget_set_text_spans`, `pt_widget_clear_text_spans`,
  `pt_widget_get_text_span_count` and `pt_widget_get_text_span` apply weight,
  slant, color and underline to byte ranges of labels and text areas. Offsets
  are snapped to UTF-8 boundaries, clamped to the text, and ranges that collapse
  are dropped; the setter returns false for widget kinds that take no spans.
- Runs feed measurement, wrapping, selection and caret movement, spans follow
  edits, and `pt_widget_set_text` clears them because the offsets no longer
  describe the new string. Default plain-text constructors are unchanged.
- Bidi paragraph layout, inline images and per-run font families remain
  post-1.0 candidates and are stated as limitations in the README.

### 3. Asynchronous and structured drag data

- Targets can inspect an offer instead of one selected MIME type:
  `pt_drop_offer_mime_count`, `pt_drop_offer_mime`, `pt_drop_offer_has_mime`,
  `pt_drop_offer_selected_mime`, `pt_drop_offer_actions` and
  `pt_drop_offer_read`. `enter_offer`/`drop_offer` take precedence over the
  legacy `enter`/`drop` pair, which still works.
- `pt_drop_stream_cancel`, `pt_drop_stream_mime`, `pt_drop_stream_size` and
  `pt_drop_stream_active` cover a read that delivers chunks on the UI thread,
  ends in `data`/`done`/`cancelled`/`error`, honours `max_bytes` (overrun ends
  the stream with an error) and allows concurrent reads. `text/plain` and
  `text/uri-list` both stream.
- Sources offer several types at once with `pt_window_begin_drag`, learn what
  the destination did through `pt_window_set_drag_finished_callback` and can
  re-read it with `pt_window_get_drag_result`; the 0.7 drag timeout and
  cancellation cover the watchdog path.

### 4. Accessible data views and overlays

- Tables and trees serve `org.a11y.atspi.Table`, `TableCell` and `Selection`
  with real row, column and selection counts, cell-to-object relationships and
  selection operations (`GetSelection`, `AddSelection`, `RemoveSelection`,
  `AddRowSelection`, `AddColumnSelection`), on top of the 0.7 attributes.
- Toasts announce through a live region with the advertised politeness, and
  focus changes reach the bridge as `focus:` events, so overlay, tab and sidebar
  updates are observable without polling.
- The live D-Bus test asserts the interfaces and their methods rather than only
  attribute strings (178 checks).

### 5. Renderer and layout scaling

- A window coalesces damage into up to eight tiles and repaints each under its
  own clip, so a localized update skips most of the visible-tree traversal: the
  dense scene paints 61 nodes per full frame against 15 per damaged frame, a
  24.59% traversal share.
- Layout invalidation records the dirty subtree and relayouts from the pinned
  ancestor, rounded rectangles take a squared-distance test with `sqrtf` only
  inside the anti-aliasing band, and font face lookup is a hash keyed by family,
  weight, slant and size.
- `bench/guards.c` adds 4K damage and 100,000-row table scroll profiles. The
  0.8 gate holds every ceiling, including the 0.7 ones: 4K full paint 34.419 ms,
  4K one-row damage 2.967 ms, 100,000-row scroll 9.834 ms with 140 widgets and
  22 materialized rows, and idle CPU 1.239 ms per 300 ms.

### 6. Release engineering and 1.0 preparation

- The public surface grew from 311 to 330 functions and 330 exported symbols;
  `api/pt.api` records 603 frozen entries, `docs/api-reference.md` regenerates
  from the headers, and the parity report now prints the 0.6 (118), 0.7 (19) and
  0.8 (19) addition surfaces separately, each complete in C++, Python and Java.
- `tools/ci.sh` ran clean end to end: werror build, all suites, perf guards,
  staged-install consumers, ASan/UBSan/LeakSanitizer over C, C++, Python and
  Java, a reproducible tarball and an extracted-tarball build.
- The first run failed LeakSanitizer; `pt_widget_free_tree` did not release the
  span array and a setter whose ranges all collapsed kept its buffer. Both are
  fixed and covered by new `test_rich_text` assertions.
- Not delivered: "execute one deprecation through the documented cycle". No 0.7
  function became redundant during 0.8, so the policy stays documented-but-unused
  and the first real deprecation moves to M15.

### Completion criteria

- Text-input, rich-text, async drag and accessible-table tests pass headlessly
  and in the live bridge suite; all 19 new public functions are wrapped in every
  binding and counted by the parity gate at 330/330.
- Performance guards hold for all scene profiles, including the new 4K damage
  and 100,000-row scroll measurements.
- Documentation, examples, API reference and release record describe 0.8.0
  without claiming behavior that the tests do not cover.
- Version strings are 0.8.0, the tarball reproduces, and the commit/tag step is
  left to the repository owner.

## M15 - Design tokens, data-view editing and the first deprecation (done, 0.9.0)

Goal: make the visual language customizable and consistent enough to ship a
frozen ABI, finish the interaction half of the data views, and put the first
real symbol through the deprecation cycle so 1.0 can freeze without carrying
dead weight.

### 1. Design tokens and control appearance

- Split the current hardcoded palette into a token table with semantic roles
  (surface, elevated surface, container, outline, on-surface variants, accent,
  success/warning/danger), expose light and dark presets, and let an
  application override tokens per app or per window without touching internals.
- Audit every control against one state matrix — rest, hover, focus-visible,
  pressed, drag-over, disabled, error, busy — and close the gaps where a widget
  paints only some of them. Focus rings become a token-driven outline rather
  than a per-widget drawing choice.
- Adopt the contemporary details that the current controls still miss: chips
  with leading/trailing slots, segmented controls with a sliding indicator,
  sidebar items with a selection pill, table headers with a sort glyph that
  matches direction, skeletons or spinners for busy state, and elevation
  shadows driven by the same radius and opacity tokens.
- Add a snapshot harness that renders every control in every state into PNGs
  under `build/tests/`, so a palette or metrics change is reviewable as a diff
  instead of a screenshot hunt.

### 2. Data-view interaction completion

- In-place cell editing for tables and rename for tree nodes, routed through the
  existing entry/undo path so keyboard and IME semantics stay identical.
- Column reordering by drag with a drop indicator, and row dragging out of and
  into a view through the 0.8 structured source and offer APIs.
- Selection extension by shift-click and marquee selection inside a scroll
  viewport, with the stable-key model from 0.7.

### 3. Styled text follow-up

- Per-run font family and size, while keeping the plain-text editing contract
  of the text area. Inline image or icon runs move to M16.
- Baseline-aware line metrics so a bold or large run does not shift the line it
  sits on; bidi remains post-1.0 and stays documented as a limitation.

### 4. First deprecation and 1.0 freeze preparation

- Deprecate at least one real symbol through the documented cycle — headers,
  release note, bindings and the baseline all marked in the same release — and
  verify no in-tree caller is left warning under `werror=1`.
- Re-run the API audit against the frozen surface and record the 1.0 checklist
  gaps: ownership rules, error contract, threading contract and the callback
  lifetime table.
- Keep `tools/ci.sh` as the only release gate and hold the parity gate at full
  coverage with an empty allowlist.

### Completion criteria

- Every control renders the full state matrix in both presets, and the snapshot
  harness is part of `make check` output review.
- Table cell and tree editing, column reordering and row drag work in the
  headless suites, and the 0.8 drag APIs carry them.
- One deprecation completes the documented cycle with no internal callers left
  using the deprecated symbol.
- All new functions are wrapped in the three bindings and pass the parity gate,
  guards hold, and the release record for 0.9.0 is written from a clean
  `tools/ci.sh` run.

### Delivered at 0.9.0

- The palette became a token table: 38 semantic color roles (surface tiers,
  container and sunken variants, outlines, disabled text, accent tiers,
  pressed and soft status roles, scrim, the skeleton pair, selection text and
  shadow) plus 10 geometric metrics (four radii, focus width and offset, two
  elevations, chip and control height). App-level setters and per-window
  override tables resolve at paint time through `pt_theme_color` /
  `pt_theme_metric_value`, and `pt_window_clear_theme_overrides` returns a
  window to the app palette. No widget paints a literal theme color any more.
- The state matrix is closed. Widgets carry `busy` and `invalid` flags on top of
  rest, hover, focus-visible, pressed and disabled; focus rings, container
  fills, outline weights and pressed tiers come from tokens, and the
  chip and skeleton widget kinds add the leading/trailing-slot filter chip and
  the shimmering loading placeholder that the modern look needs. Sidebar
  selection pills, direction-correct header sort chevrons and elevation-token
  shadows were audited in the same pass.
- Data views finished their interaction half. Tables take in-place cell editing
  through the existing entry/undo path (`pt_table_set_editable`,
  `pt_table_begin_edit`, `pt_table_commit_edit`, `pt_table_cancel_edit`,
  `pt_table_is_editing` and the edited callback), multi-row selection with
  shift-click extension (`pt_table_select_range`, `pt_table_get_selection`,
  `pt_table_is_row_selected`, `pt_table_clear_selection`), user column
  reordering (`pt_table_set_columns_reorderable`, `pt_table_move_column` plus a
  reordered callback) and row drag-out (`pt_table_set_on_row_drag`). Pressing a
  row and dragging paints a rubber band clipped to the viewport and sweeps the
  range selection to the row under the pointer, which
  `tests/test_editing.c:201-219` asserts end to end. Trees
  rename in place (`pt_tree_set_renamable`, `pt_tree_begin_rename`,
  `pt_tree_commit_rename`, `pt_tree_cancel_rename`, `pt_tree_is_renaming`).
- Styled runs gained `family` and `size` with `pt_text_span_has_family` and
  `pt_text_span_has_size`. Line metrics are baseline-aware: one pass acquires
  each run's face from the hashed face table and widens the line's ascent and
  descent, so a large or bold run no longer shifts the runs beside it.
- The deprecation cycle ran end to end for the first time.
  `pt_window_begin_text_drag` and `pt_window_begin_uri_drag` are marked with the
  new `pt_deprecated(...)` attribute macro, documented with their removal
  window, mirrored as `[[deprecated]]` / `DeprecationWarning` / `@Deprecated`
  in the three bindings, recorded in `docs/api-policy.md` and in the release
  notes, and still exported so `make api-check` sees no baseline removal. No
  in-tree caller warns under `werror=1`: the wrapper implementations and the
  tests that deliberately exercise them use scoped
  `#pragma GCC diagnostic` guards.
- A snapshot harness landed in `tests/test_visuals.c`: it renders a
  deterministic control gallery into `build/snapshots` and compares frame
  hashes across repeats, dark→light→dark round trips and window overrides, so a
  palette or metric change is reviewable as a PNG diff. `test_theme` and
  `test_editing` cover the new token resolution and editing surfaces.
- The surface grew from 330 to 374 functions and 374 exported symbols. The
  parity gate holds at 374/374 for C++, Python and Java with an empty
  allowlist, and the audit now also reports a 0.9 addition surface of 44
  functions, complete in all three bindings. `api/pt.api` and
  `docs/api-reference.md` were regenerated from the headers.

### Not delivered

- Inline image or icon runs inside styled text. The span model carries font
  family and size; embedding an image would change the editing contract, so it
  moves to M16 as its own design task.
- Marquee selection is table-only and row-anchored. The rubber band starts on a
  row and sweeps the range to the row under the pointer; pressing empty
  viewport space does not start one, the band selects by row index rather than
  geometric intersection, and lists and trees have no marquee at all. Keyboard
  arrows move the primary row and reset the set, and there is no Ctrl-click
  toggle, so Shift+arrow extension and additive selection are missing too.
- A theme editor on top of the token table stays a post-1.0 candidate.

## M16 - Surface completeness, editing polish and the 1.0 readiness gate (done, 0.10.0)

Goal: close the last gaps that a real application hits, freeze the C ABI with
confidence, and turn the documented 1.0 checklist into a gate that fails when a
criterion is unmet.

### 1. Pointer and editing completeness

- Generalize the 0.9 table rubber band: start a marquee from empty viewport
  space as well as from a row, select every row the band intersects rather than
  the index range between anchor and pointer, and carry the same gesture to
  lists and trees on the 0.9 selection bitset and stable-key model, with the
  band painted from the selection tokens and clipped to the scroll viewport.
- Additive and keyboard selection: a Ctrl-click toggle and Shift+arrow
  extension over the same bitset, so arrow navigation no longer has to collapse
  the set to one row, plus the range and toggle primitives for lists and trees.
- Column resize and reorder persistence helpers so an application can store
  layout state without re-implementing the column model.
- Inline image or icon runs in styled text: a run slot that reserves box and
  baseline from the same line-metric pass, keeping the text area's plain-text
  editing contract intact.
- Clipboard images and `text/uri-list` writes through `wl_data_device`, plus a
  primary-selection path where the compositor offers `primary-selection-v1`.

### 2. Surface completeness

- Window popups: application-owned child surfaces (`xdg_popup` parents from a
  window) so menus, comboboxes and tooltips can overflow the window edge on
  every backend, with the in-window overlay kept as the fallback.
- Modal dialogs and toasts on a native popup when the compositor permits it,
  removing the last "stays inside the window" limitation.
- Combobox, date/number pickers, colour picker and a searchable select built
  from the existing entry, list and overlay primitives rather than a second
  widget system.
- Icon slots for arbitrary painted content, so a custom `pt_icon` provider can
  supply glyphs without recompiling the built-in table.

### 3. Accessibility and internationalization

- AT-SPI states for busy, invalid, editable text and chip/selected-row roles,
  and text interfaces on labels and text areas rather than only entries.
- Expand the live bridge test to assert actions (`AtspiAction`) and relations
  (`AtspiRelationSet`) for data views.
- Font fallback chains per run (a missing glyph resolves through fontconfig
  rather than drawing a box) and a documented right-to-left experiment; full
  bidi paragraph layout remains post-1.0 but must ship with a written plan.

### 4. 1.0 freeze preparation

- Decide the disposition of every 0.9 deprecation: remove, or keep with a
  documented window through 1.0. Record it in `docs/api-policy.md`.
- Freeze pass over the headers: ownership and lifetime wording for every
  callback and returned buffer, one error contract per function family, and a
  documented threading table. Any signature that is not 1.0-worthy must change
  in the rc, not after.
- Turn the 1.0 readiness checklist into `tools/release-check.sh` so a release
  cannot be cut while a criterion is unmet, and keep `tools/ci.sh` as the only
  build gate it calls.
- Documentation sweep: a migration guide for 0.x applications, an
  accessibility guide and a theming guide built from the real token table.

### Completion criteria

- Generalized marquee selection, popup surfaces, combobox-class controls and
  image runs ship headless-tested, wrapped in all three bindings and
  demonstrated in at least one example per language.
- The live AT-SPI bridge asserts states, text interfaces, actions and relations
  for the data views.
- The 1.0 checklist is enforced by a script, every deprecation has a recorded
  disposition, and `api/pt.api` is reviewed as the frozen surface.
- Performance guards hold, including a new marquee-drag and popup-surface
  profile, and `tools/ci.sh` runs clean end to end including sanitizers and the
  reproducible tarball.

### Delivered at 0.10.0

- The combobox. `pt_combobox_create` plus 26 functions give a field with a
  popup list in two modes: `pt_combobox_select_only` (a dropdown whose field is
  not an editor) and `pt_combobox_filterable` (an entry that filters the popup
  with a case-insensitive substring match). Items carry text, a disabled flag
  and a borrowed data pointer that moves with the item on insert and remove;
  `pt_combobox_set_selected` and `pt_combobox_set_text` are silent so loading a
  saved value never re-enters a callback; `pt_combobox_set_on_change` reports a
  committed index and `pt_combobox_set_on_text_change` every edit. The keyboard
  model is F4 / Alt+Down / Alt+Up to open, arrows and Page keys to move the
  highlight, Enter or Space to commit, Escape to close unchanged, Tab to close
  and move focus, Home/End to jump, and type-ahead in the select-only mode. The
  popup is capped by `pt_combobox_set_popup_max_height` (260 px by default),
  clamps and flips inside the window, and opens on a native `xdg_popup` in the
  select-only mode.
- Selection parity across the data views. 13 new functions —
  `pt_list_view_select_range`, `_select_toggle`, `_get_selection`,
  `_get_selection_count`, `_is_row_selected`, `_clear_selection`, the six
  `pt_tree_*` mirrors and `pt_table_select_toggle` — with one contract:
  inclusive clamped ranges, silent set helpers, the set separate from the
  primary row, toggling off the primary row moving it to the next selected row,
  and an empty set naming `pt_list_index_none`. Shift-click, Ctrl-click,
  Shift+arrow, Ctrl+A and a rubber band that selects by geometric intersection
  (`pt_marquee_hit`) work the same way in a list, a table and a tree, and
  `tests/test_selection.c` runs one gesture script through all three views, so
  a divergence between them fails the suite (189 checks).
- Accessibility for the new surfaces. `pt_a11y_role_combo_box` and
  `pt_a11y_role_list_box` join the enum (append-only, 34 and 35) and
  `pt_a11y_state_expandable`, `pt_a11y_state_selectable` and
  `pt_a11y_state_multi_selectable` join the state bits. The model layer marks a
  combobox expandable and expanded while its popup is open, gives its panel the
  list-box role, marks all three views multi-selectable and their rows
  selectable, and the bridge maps them onto the AT-SPI numbers assistive tech
  expects. `test_a11y_bridge` (200 checks) asserts `MULTISELECTABLE`,
  `SELECTABLE`, `SELECTED`, `EXPANDABLE`, `EXPANDED` and `COLLAPSED`, the
  `Selection` methods including `SelectAll` and per-row deselect, `DoAction` on
  a button, and `GetRelationSet` between cells, headers, rows, tree items and
  their owning views.
- Test and visual coverage. The C suite is 29 executables: 26 headless suites
  carrying 2,837 assertions, the 454,380-check randomized property suite, the
  200-check live bridge and the 40-check live Wayland smoke test, whose native
  popup and tooltip checks now retry an attempt the compositor ended (`popup_done`)
  inside the wait instead of asserting on the first slice.
  `test_combobox` (185 checks) covers
  the item model, both modes, popup geometry inside the window edges, keyboard
  choice and the popup's list-box role. `test_visuals` (119 checks) gained a
  second gallery: an unselected frame, a multi-row selection in all three
  views, a rubber band caught mid-drag, an open combo box popup and a
  light-theme pass, each hashed and compared so a state that stops painting
  shows up as two identical frames.
- Bindings, examples and the gate. 414/414 with an empty allowlist, the 0.10
  surface (40 functions) complete per language, 345 C++ checks, 33 Python tests
  and 298 Java checks. Four combobox examples —
  [c/combobox.c](../examples/c/combobox.c),
  [cpp/combobox.cpp](../examples/cpp/combobox.cpp),
  [python/combobox.py](../examples/python/combobox.py),
  [java/Combobox.java](../examples/java/Combobox.java) — each drive the two
  modes, the item operations, the callbacks and the multi-row gestures, run
  headless and are wired to `make run-combobox`, `make run-cpp-combobox`,
  `make run-python-combobox` and `make run-java-combobox`, which
  `tools/release-check.sh` checks for. `tools/release-check.sh` turns the 1.0
  checklist
  into a report and the deprecation disposition is recorded in
  [api-policy.md](api-policy.md).
- A live text-input round trip that only shows up on a real compositor. An empty
  `zwp_text_input_v3.done` — no commit, no preedit, no deletion — ran the whole
  composition path: caret reset, `selection_changed`, a fresh
  `set_surrounding_text` and `commit` back to the input method, and a redraw. The
  answer asks the input method for another acknowledgement, which asks for
  another answer, so any window with a focused entry redrew and re-announced its
  selection continuously: the accessibility example printed 67,499
  `selection_changed` events and burned about 2.1 s of CPU in a 5 s live run, and
  it did so with the AT-SPI bus unreachable too, so it was never an assistive-tech
  problem. `pt_window_input_ime_compose` now ignores an empty `done` unless a
  composition is in flight, which leaves the only real end-of-composition signal
  working; the same 5 s window prints 1 event and 0.65 s. `test_ime` (73 checks)
  pins both halves.

### Not delivered

- The rubber band still had to start on a row. Pressing the empty area below
  the last row opened no band, and in a tree the band ranges over visible rows,
  so a collapsed branch counts as the one row it shows.
- Application-owned child surfaces did not ship as a public API. The combobox
  reuses the existing overlay path, so a select-only box can overflow the
  window on a native popup and a filterable one, a dialog and a toast still
  cannot.
- Date, number and colour pickers, a searchable select beyond the filterable
  combobox, and icon slots for arbitrary painted content.
- Per-run font fallback chains. (The written right-to-left plan and the
  label/text-area text interfaces in the bridge were finished by M17; see
  section 5 of the 1.0 freeze.)
- Clipboard images and `text/uri-list` writes through `wl_data_device`, and a
  primary-selection path.
- Column resize and reorder persistence helpers, and inline image or icon runs
  in styled text (carried from M15).
- The two new performance profiles the completion criteria asked for: a
  marquee-drag frame and a native popup surface. The existing guards hold,
  including the 10,200-node tree scroll and the 100,000-row table seek.
- A guard for what a compositor only shows. The text-input round trip fixed at the
  end of this round was found by running an example and reading its console, and
  the numbers quoted for it in [release.md](release.md) come from hand-run slices
  on this desktop, not from `make perf-check`. M17 turns that into a live check.
- The freeze pass over the headers (per-callback ownership and lifetime wording,
  one error contract per function family, a threading table) and the migration,
  accessibility and theming guides.

### 1.0 release criteria

- After the 0.9.0 baseline the only planned break is the removal of the two
  0.9-deprecated drag helpers, executed by the 1.0 release; everything
  else is additions that follow the policy.
- All four language surfaces, headless tests, accessibility bridge, examples
  and documentation are complete and reproducible from the tarball.
- The renderer, data views and event loop have documented performance
  envelopes, and known limitations are explicit release notes rather than
  surprises.
- API reference and migration notes are complete enough that a new user can
  build, test and ship without reading internal headers.

## M17 - The 1.0 freeze: rc, removals and the guide sweep (done, 1.0.0)

Goal: turn 0.10.0 into a release that can be frozen and left alone. M17 spends
its surface budget on the two things that are impossible to fix after 1.0
without a major release — the recorded removals and the wording of the
contracts — and moves everything additive to post-1.0.

### 1. Execute the recorded removals (done)

- `pt_window_begin_text_drag` and `pt_window_begin_uri_drag` are gone: the two
  declarations in `include/pt/dnd.h` and their `pt_deprecated` attributes, the
  implementations in `src/dnd.c`, the `[[deprecated]]` C++ wrappers with their
  scoped diagnostic pragmas, the two Python methods with their
  `DeprecationWarning` blocks and unused `warnings` import, the two Java
  `@Deprecated` methods with their `MethodHandle` binds, and the audit's 0.6
  round list. The internal single-MIME helper `pt_window_drag_source_begin`
  stays, because the entry drag path in `src/window.c` is its only remaining
  caller.
- Call sites moved to `pt_window_begin_drag` rather than vanishing, so the
  headless "no drag compositor" path is still asserted in C, C++, Python and
  Java. The old URI helper's five argument guards became three checks that a
  caller-composed CRLF payload parses with `pt_uri_list_count` and still fails
  headlessly; the C suite moved 457,457 → 457,455 and the C++ suite 345 → 344,
  while Java stayed at 298 and Python at 33 tests.
- `make api-update` recorded the removals as the review artifact: against the
  0.10.0 baseline the audit reported `726 entries; 2 removed, 0 changed,
  0 added` and named both functions, and the refreshed baseline reads
  `724 entries; 0 removed, 0 changed, 0 added` with `412 pt_* symbols; 0
  missing, 0 unexpected` and `412/412` parity in each binding. The generated
  reference dropped the two `Deprecated since 0.9.0` sections and dnd.h from 27
  functions to 25.
- `tools/release-check.sh` flipped its own deprecation section to
  "nothing is deprecated; the removal window is closed" and lost the matching
  human `open` item, so the readiness gate now ends with 2 open items instead of
  3.
- The Java lint surface is clean: `javac -Xlint:all` over `Pt.java` and `PtTest.java`
  now prints nothing, where 0.9.0 recorded one deprecation warning per
  `@Deprecated` method at the test call sites.
- The release notes carry the removal as the only breaking change of the cycle.

### 2. The header freeze pass (done)

- Ownership and lifetime wording for every callback argument and every returned
  buffer: who allocates, how long the pointer stays valid, whether the callee
  copies. Ambiguity here is what makes a later correction breaking, so this pass
  is rc-blocking while a missing feature is not.
- One error contract per function family: setters that cannot fail, setters that
  return `false` on bad input, getters that clamp versus getters that report
  `pt_list_index_none`. Each family names its contract once in the header.
- A threading table in [architecture.md](architecture.md): which entry points
  are safe off the UI thread, which are never safe, and what
  `pt_post_*` is for.
- Name the native popup grab contract in the headers. A grabbed `xdg_popup` is the
  compositor's to end: `popup_done` closes the overlay instead of falling back to
  the in-window path, and the serial used for the grab is the last pointer serial
  on the window, or the seat's most recent one when no pointer event landed there.
  `pt_menu_show_at`, `pt_widget_set_tooltip` and `pt_combobox_open` all inherit
  that behavior, so the header has to state it before 1.0 fixes it in place.
- Name the text-input state-push contract in `include/pt/widget.h` and
  [architecture.md](architecture.md). Pt answers a real edit, a caret move, a
  focus change and a composition end with `set_surrounding_text`, a caret
  rectangle and a `commit`; it ignores an empty `done`, which is the input
  method's acknowledgement rather than a request. Input-method engines written
  against 1.0 will assume a reply to every event they send, so the moments that
  do produce a push have to be stated in prose before the surface freezes.
- Re-verify the append-only contracts that 1.x has to respect: enum values in
  `include/pt/a11y.h` and the widget kinds, the theme token indices, and the
  `pt_drag_action` bits. Anything that could still be reordered must be settled
  in the rc.
- Wording-only changes are allowed in the rc; a signature change is allowed only
  in the rc, never after.

Delivered:

- Every public header now opens with a machine-extracted `Header contract`
  block. It names the UI-thread rule (including getters), ownership of every
  borrowed handle and caller-owned buffer, the single error model, callback
  lifetime and the family exceptions: popup grabs in `window.h`/`overlay.h`,
  offer, stream and drag-payload lifetimes in `dnd.h`, the three text-input
  state-push moments in `widget.h`, and the any-thread value helpers in
  `base.h`/`motion.h`. `tools/gen-api-reference.py` renders the block as the
  module preamble of `docs/api-reference.md`, and `make api-check` still fails
  on drift.
- [architecture.md](architecture.md) publishes the threading table by
  entry-point class. Only the pure value helpers, easing/transition math,
  URI-list parsing, `pt_free` and the thread-local `pt_get_last_error` are safe
  off the UI thread; there is no locking and no `pt_app_invoke` in 1.0. The IME
  section now states the three push moments and the empty-`done`
  acknowledgement rule in prose, matching the header.
- `tests/test_abi.c` is a new headless gate for the append-only contract. It
  pins every public enum value, every accessibility state and modifier bit,
  every sentinel and limit, the field order of the public structs, and the
  version-macro agreement. It joins `make check` as the thirtieth C executable,
  so a renumbering fails the build instead of reaching a release.
- The 1.0 compatibility policy records how the header blocks, the ABI test and
  the baseline diff divide the freeze work between prose and enforcement.

### 3. Freeze the version and the object name (done)

- `VERSION := 1.0.0` in the Makefile, `PT_VERSION_*` in `include/pt/base.h`, the
  README status line and the `docs/api-reference.md` header, then
  `tools/release-check.sh` green as the gate.
- SONAME becomes `libpt.so.1` and `docs/api-policy.md` is rewritten to say so.
  Its current sentence — 1.0 may keep `libpt.so.0` if the ABI is a strict
  superset — is already false for this release, because the removals make the
  frozen ABI a subset.
- A `1.0.0-rc1` tag and the freeze commit are the repository owner's action.
  M17 prepares the tree and names the tag; it does not create either.

Delivered:

- The tree reports `1.0.0` everywhere: `VERSION` in the Makefile, the four
  `pt_version_*` macros, the README status line and the generated reference
  header. `tools/release-check.sh` accepts the pair `1.0.0` / `libpt.so.1` and
  rejects either half on its own.
- `SONAME := libpt.so.1`, so the install rules produce
  `libpt.so.1.0.0`, `libpt.so.1` and `libpt.so`. `tools/install-check.sh` now
  reads the SONAME from the Makefile instead of assuming `libpt.so.0`, verifies
  the `readelf` value, and checks the staged symlink and the four-language
  consumers against the staged prefix.
- [api-policy.md](api-policy.md) states why the object name changes: the frozen
  ABI is a subset of 0.10 because the two deprecated drag helpers are gone, so a
  `libpt.so.0` binary must not be loadable against `libpt.so.1`. The policy also
  says additions inside 1.x keep `libpt.so.1`.
- The 0.9.0 and 0.10.0 validation records in [release.md](release.md) keep their
  historical `libpt.so.0` rows; only the current release statement and the
  version checklist moved to the 1.x wording.

### 4. The guide sweep (done)

- A migration guide for 0.x applications: the 0.9 drag consolidation, the 0.9
  hard-coded-color-to-token move, the 0.10 selection vocabulary and the 1.0
  removals, with before/after code per language.
- An accessibility guide: role and state derivation, what the AT-SPI bridge
  publishes for each widget kind, how an application overrides a derived field,
  and what a platform adapter other than AT-SPI has to implement.
- A theming guide built from the real token table: the 38 colors and 10
  metrics, app setters versus per-window overrides, resolution order at paint
  time, dark-mode pairing rules and contrast guidance.
- Every guide is checked against the code as written, in the same way the
  reference doc is: no claim survives that the headers do not support.

Delivered:

- [theming.md](theming.md) lists all 38 color roles with both reference values
  and all 10 metrics with their defaults, documents the window → app → compiled
  default resolution order, the app-versus-window override APIs, the accent
  derivation formulas from `src/app.c`, the dark/light pairing rules and the
  measured WCAG floors enforced by `test_theme`. It also records the two
  footguns the code actually has: `pt_app_set_color_scheme` replaces app
  overrides, and a direct `pt_theme_accent` override does not recompute the
  derived accent family.
- [accessibility.md](accessibility.md) maps every widget kind to its derived
  `pt_a11y_role` and AT-SPI role, derives every state bit and the extra AT-SPI
  states (`SHOWING`, `SENSITIVE`, `ACTIVE`, `CHECKABLE`, `COLLAPSED`,
  `INDETERMINATE`, `HAS_TOOLTIP`, `PRESSED`, `SELECTABLE_TEXT`, `READ_ONLY`),
  covers name/description/text/value derivation, the seven events and their
  AT-SPI signals, the interface matrix and representative methods, the
  virtualized-row and overlay tree rules, and the contract a non-AT-SPI adapter
  must implement. It states the 1.0 limits, including the absent public
  activation entry point and the password character-count behavior.
- [migration-1.0.md](migration-1.0.md) gives before/after C, C++, Python and
  Java replacements for the two removed drag helpers, a caller-side CRLF URI
  join that preserves the old validation policy, the 0.9 literal-to-token move
  for built-in widgets, the 0.10 range/toggle/get/clear vocabulary and its
  primary-row contract, and the SONAME/recompile steps.
- README, quickstart, architecture and the API policy link the three guides;
  README's limitations section now says 1.0 rather than 0.10 and describes the
  frozen ABI. The generated reference remains the per-function source of truth.
- The palette pass that the guide documents is enforced, not just described:
  `test_theme` now checks `selection_text` against the selection role
  composited over both surface and background in each scheme, and the light
  `selection_text` default is dark text instead of white. The earlier palette
  adjustment moved the dark and light accents and the light status roles to
  clear their floors, which the guide records as the one visual change in 1.0.

### 5. Performance guards and the live idle check (done)

- `bench/guards.c` gained the two profiles M16 asked for. The marquee scene holds
  a drag across a 400-row virtualized list in a 960x640 window; every measured
  frame moves the band, applies the new selection to the visible pool and
  repaints the damaged region, with a 20 ms ceiling. The popup scene lays out
  the 240x332 combo panel (80 options, 82 widgets) in surface-local coordinates
  and clears and repaints it into its own canvas, the expensive half of the
  native popup path, also under 20 ms. Both are headless and run in
  `make perf-check`, so a regression fails the gate instead of only changing a
  published number.
- The closing 1.0.0 guard passes measured an 8.800-9.335 ms marquee frame and a
  1.202-1.287 ms popup panel frame against those 20 ms ceilings. The other guard
  inputs stayed inside their 0.10.0 ranges: dense layout 2.205-2.326 ms and full
  paint 13.252-14.074 ms, one-row damage 1.007-1.065 ms (7.411-7.600%), 4K paint
  34.313-35.941 ms and 4K damage 3.039-3.175 ms (8.835-8.924%), large-tree scroll
  2.480-2.562 ms, 100,000-row seek 10.411-10.504 ms, startup first frame
  8.990-10.760 ms and 300 ms of idle loop 0.582-0.700 ms. Every ceiling held.
- The live idle guard is in `tests/test_wayland.c` and runs under
  `make check-wayland`. A window with a focused entry runs for a 5,000 ms slice;
  process CPU comes from `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` and must stay
  under 1,500 ms. The target starts a **fullscreen nested niri** through
  `tools/nested-wayland.sh`, focuses its window in the parent session and runs
  the test as the only client of that compositor's seat. Physical input still
  reaches the focused nested window; a slice with a key, an entry-text change, a
  focus change or pointer movement is discarded and retried, and four busy
  slices report a skip instead of a false regression. The five quiet manual runs
  measured 22.8-56.6 ms; gated runs under desktop activity measured
  56.6-745.6 ms, all below the ceiling. The 0.10.0 state-push loop at 2.13 s in
  the same slice is 1.4x the ceiling, so the guard catches the regression it was
  added for.
- `make check` is now the headless gate: the live `test_wayland` executable left
  `TEST_NAMES`, so `make check` no longer steals focus or depends on the desktop,
  and `make check-wayland` owns the live suite. The tree still builds 30 C test
  executables; `make check` runs the 29 headless ones, while `check-wayland`
  builds and runs the thirtieth. The live bridge suite still skips cleanly when
  its bus is absent.

### 6. Scope decided against the freeze

Additive gaps do not hold the freeze. They move to post-1.0 with the reasons
kept where a reader can find them: application-owned child surfaces for
filterable comboboxes, dialogs and toasts, pickers and a searchable select, a
`pt_icon` provider, clipboard images and `text/uri-list` writes, column layout
persistence helpers and inline image runs. Two items kept their rc slot because deferring them would have been a
design debt rather than a feature: text interfaces on labels and text areas in
the bridge, which had to agree with the entry path before the accessibility
contract was frozen, and a written right-to-left plan, because the
paragraph-ordering surface it implies is exactly the kind of thing that cannot
be introduced later without breaking layouts. Both are delivered.

Delivered:

- The bridge now treats every text-bearing kind consistently. Labels and
  buttons expose `org.a11y.atspi.Text` and the `READ_ONLY` state; entries and
  text areas expose `Text` and `EditableText` and report `SELECTABLE_TEXT`, not
  `READ_ONLY`. The state rule no longer tests for `pt_widget_entry` alone, so a
  multi-line text area is not mislabeled as read-only.
- `GetCharacterAtOffset` reads the password-safe visible string, closing a path
  that could return a password character code. Character counts still report
  the real length, and `GetText`, string-at-offset and change events remain
  empty for a password entry.
- `test_a11y_bridge` grew from 200 to 236 live checks: label `Text` without
  `EditableText`, read-only state, `GetText`, `CharacterCount`,
  `GetStringAtOffset` and `GetCharacterAtOffset`; a text area with
  `Text`/`EditableText`, `SELECTABLE_TEXT`, UTF-8 slices, selection conversion,
  caret placement, `InsertText` and `DeleteText`; and a password entry whose
  `GetText` is empty and whose character code is zero while its count is six.
  `test_a11y` covers the corresponding model text, selection, caret and
  editable-state behavior (146 checks).
- [rtl.md](rtl.md) is the written plan: it separates text direction from UI
  direction, proposes the additive post-1.0 properties without adding them to
  the frozen surface, specifies the UBA/reordering/shaping pipeline and the
  logical-to-visual byte map, keeps public UTF-8 and AT-SPI character offsets
  logical, covers mirrored layout and chrome, and lists Unicode conformance,
  editing, accessibility, screenshot, LTR-compatibility and performance
  obligations. README, quickstart, architecture and accessibility link it.
- The accessibility guide now names the label/text-area/password assertions
  and states that bidi keeps offsets logical.
- A live typing bug found during the freeze was fixed: while text-input-v3 was
  enabled Pt discarded the UTF-8 text of every ordinary key, so a focused entry
  accepted only input-method commits. Ordinary `wl_keyboard` text now reaches
  the key path even with text input enabled (accelerator combinations are still
  excluded), and `make check-typing` starts a nested niri compositor and injects
  keys through a virtual keyboard to keep the path covered. The nested runner now
  focuses that window in the parent session before injecting; a nested compositor
  drops its own keyboard focus when the parent window is not focused, which
  silently sent the first version's keys nowhere. The guard passes with
  `ptdirect`, `hello world`, `abcdefghij` and `pt 1.0`.
- After the validation pass, the empty-viewport marquee moved into 1.0.0 as a
  behavior-only follow-up: a press below the last row starts the shared band,
  clears the set unless Ctrl is held, and drags select by geometric intersection
  in the list, table and tree. `tests/test_selection.c` grew from 189 to 274
  checks. No header, API baseline or binding changed.

### Completion criteria

- The two deprecated functions are gone from headers, sources, bindings, tests
  and docs, `make api-check` shows the removal diff, and the tree builds and
  passes `make check` and `make check-bindings` from that state.
- `tools/release-check.sh --full` is green with no `open` line left that the
  release notes do not answer, and `tools/ci.sh` runs clean end to end
  including sanitizers, the live Wayland and bridge suites and the reproducible
  tarball.
- Every public header states ownership, lifetime and its error contract, and the
  threading table is published.
- The live Wayland suite carries an idle guard: a window with a focused entry is
  run for a 5,000 ms slice, the process's own CPU time is read from
  `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`, and the slice has to stay under the
  1,500 ms ceiling. The text-input round trip that only a compositor can trigger
  is invisible to every headless measurement, so the freeze needs one check that
  is not headless. `make check-wayland` runs it in a fullscreen nested niri with
  a private seat and a parent-focused window; slices disturbed by user input are
  discarded and retried. Clean slices measured 22.8-56.6 ms and every gated slice
  stayed below 745.6 ms, while the 0.10.0 loop measured 2.13 s.
- The live typing guard passes: a nested compositor opens a focused entry,
  virtual-keyboard keys reach it while text-input-v3 is enabled, the nested
  window is focused in the parent session so its keyboard focus survives, and
  the entry contains exactly the injected text. It passes with `ptdirect`,
  `hello world`, `abcdefghij` and `pt 1.0`.
- The migration, accessibility and theming guides ship, and README,
  quickstart, comparison, architecture, bindings, performance and release docs
  carry 1.0 wording with the SONAME bump reflected in all of them.
- Performance guards hold with the marquee-drag and popup-surface profiles
  landed: 9.128-9.335 ms and 1.254-1.287 ms across the closing gate runs,
  against their 20 ms ceilings, with every other guard input green in the
  closing `make perf-check`.
- `api/pt.api` is reviewed and declared the frozen 1.0 baseline, and the tree is
  ready for the owner to tag.
- The 1.0.0 validation record in [release.md](release.md) records the full
  `tools/ci.sh` and `tools/release-check.sh --full` passes, the 412/412 audit,
  the 48-check live Wayland suite, the live typing guard and the final
  performance figures.

## Post-1.0 candidates

- Vulkan/EGL accelerated renderer behind the same canvas contract.
- Rich-text editing with bidi paragraph layout and spell-check integration.
- Formal C ABI description with generated binding stubs.
- A visual theme editor on top of the 0.9 token table.
- X11 or other platform backends only if they can be maintained without
  compromising the Wayland-first core.
