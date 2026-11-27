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

## M18 - 1.1.0: contract hardening (delivered; tag pending)

Goal: make the paths 1.0 already promised explainable, testable and fallback-able
on real compositors, and finish the extension surface that has landed in the
1.1.0 work-in-progress. 1.1 adds no new widgets; it hardens contracts and closes
the gaps that make `tools/release-check.sh` red. The engineering rules this
milestone must satisfy are consolidated in [standards.md](standards.md); the
protocol evidence is in [compositor-matrix.md](compositor-matrix.md).

Status (2026-11-22): the five development branches are merged at `d552baf` and
the baseline is 448 functions / 785 entries. C contracts, ABI pins, the
generated reference, examples, binding tests and the bilingual guides are
delivered. The remaining work is the last seven wrapper functions in all three
bindings, the `Makefile` `VERSION` line, and the final gate run; the annotated
tag stays uncreated until those land. Item status and evidence follow.

### 1. Scope decision

The work-in-progress (commits `2b2796b`, `0a54af5` and the uncommitted tree)
already added 29 public functions / 49 baseline entries beyond the 412-function
1.0 surface: server-side decoration negotiation, primary selection, the
`pt_paint_*` custom-draw context and helpers, offscreen `pt_window_paint_pixels`,
custom IME callbacks, fd watches, and raw focus/key/pointer callbacks. The form
is append-only and ABI-compliant.

Decision: **1.1.0 keeps this extension surface but treats it as "harden and
complete", not "ship as-is".** No release until the contracts, Java parity,
generated reference, documentation, examples and tests below are closed. If
completion slips, the fallback is to narrow 1.1.0 to decoration + primary
selection + lifecycle fixes and move the custom-draw/IME/fd/raw-input API to
1.2.0; that split is a scope change, not a silent partial tag.

In scope for 1.1.0:

- Decoration negotiation rework and window-state consumption.
- Side-effect-free offscreen painting.
- Paint-callback, IME, fd-watch and raw-input contracts.
- Primary-selection serial sourcing.
- Binding parity restoration (Java), generated-reference automation, version
  bump, ABI-test coverage, documentation and bilingual sync, examples.
- Live and fake-compositor tests for every protocol path above.

Out of scope (moved to the horizons below): application-owned popups/subsurfaces,
pickers, searchable select, icon slots, clipboard images, `text/uri-list`
writes, per-run font fallback, column persistence, inline images, RTL/bidi, GPU
renderer, theme editor.

### 2. Current state (verified 2026-11-22, integration head `d552baf`)

- The C surface is hardened and pinned: decoration rework (`7b69bda`), primary
  serials (`ac0ee22`), paint contract (`c46d57b`), pure snapshot and `_ex`
  (`8168d9d`), IME surface (`5c47fec`), fd ownership (`da4cdda`), raw consume
  scope (`d96cb1b`, `6000e25`) and ABI pins (`0d4ec5b`).
- `api/pt.api` reads 448 functions and 785 entries; the README, architecture and
  bindings counts, the four version macros and `Makefile` `VERSION` all say
  1.1.0.
- The generated reference is derived from `include/pt/*.h` (`df89ba6`) and was
  regenerated for the 448-function surface (`d552baf`, `39061d3`), so `custom.h`
  can no longer be silently omitted.
- C++, Python and Java each report 448/448 with empty allowlists: the final
  seven wrappers (owned fd watch, decoration setter and two getters, maximize,
  primary serial, snapshot `_ex`) landed with dispatch checks in `5945267`,
  `973bb3a` and `ff59fc5`.
- The combined `make check` passes end to end, as do `make check-bindings`
  (C++ 517 checks, Python 48 tests, Java 372 checks), `make perf-check` (all
  ceilings), `make check-wayland` (48 checks) and `make check-typing` on the
  merged tree. The bilingual documentation check reports 9 public guides with
  no structural drift.
- No fake-compositor wire-replay facility exists. Decoration, text-input,
  primary-selection and popup orders are covered by headless state tests plus
  live niri runs; the wire-level replay remains open.
- `preedit_hint` visual presentation and `show_input_panel`/`hide_input_panel`
  are deferred to 1.2; the custom IME event surface and full-state resend are
  implemented.

### 3. Backlog

P0 — a 1.1.0 tag is impossible without these:

1. **Version and release surface — delivered (`b8a21c4`, `4e4f26f`, `f2633cf`).**
   `include/pt/base.h` and `Makefile` `VERSION` are 1.1.0; README, reference
   header, architecture/bindings counts and this milestone carry the same
   version; the 1.1.0 release record is in [release.md](release.md). SONAME
   stays `libpt.so.1`.
2. **Generator and reference — delivered (`df89ba6`, `d552baf`).**
   The generator now enumerates the
   public headers (or a derived manifest) so a missing module fails `--check`;
   regenerate `docs/api-reference.md` and `api/pt.api` with `make api-update`;
   both now cover 448 functions and the reference names every baseline function.
   Do not hand-edit the reference.
3. **Binding parity — delivered (`b220410`, `5945267`, `973bb3a`, `ff59fc5`).**
   Java gained 29 wrappers in `b220410`; the final seven functions were then
   wrapped in all three languages with `PtTest`, C++ and Python dispatch
   checks. All three report 448/448 with empty allowlists. The C++ methods and
   Python names in [bindings.md](bindings.md) were confirmed against the
   landed wrappers. No function is C-only; the allowlist stays empty.
4. **Decoration semantics — delivered (`7b69bda`); wire replay open.**
   Default to explicit `CLIENT_SIDE`; add
   `pt_decoration_mode {auto, client, server}` and
   `pt_window_set_decoration_mode()`. Split `pt_window_set_decorated(bool)` (Pt
   draws its own header) from the decoration mode so `false` never requests
   server-side. Track requested vs effective mode; obey `configure`; consume
   `fullscreen`/`tiled_*`/`configure_bounds`/`wm_capabilities`. Fix the
   `src/wayland/window.c` comment that claims niri draws a title bar. Cover the
   six fake-compositor sequences and live-smoke Mutter/KWin/niri; record Weston
   as a known double-border limitation (Q-DEC-003). The requested/effective
   split, `set_decorated(false)` and the state consumption are implemented and
   headless-tested; the six wire sequences are not in the tree and the live
   result is the 48-check niri run.
5. **Offscreen snapshot — delivered (`8168d9d`).**
   `pt_window_paint_pixels` is side-effect-free (no
   damage, no clearing `needs_draw`/`redraw_scheduled`, no overlay collection);
   add `pt_window_paint_pixels_ex` with stride/scale/format; document
   premultiplied `0xAARRGGBB`. `tests/test_window.c` keeps a mapped window's next
   frame and deferred overlays intact across a snapshot, and covers the
   descriptor stride/scale/format failures; `test_visuals` uses the entry point
   for the goldens.
6. **Primary-selection serial — delivered (`ac0ee22`).**
   Split enter/pointer-button/key-press serials;
   `set_selection` uses the triggering event's serial; programmatic selection
   without a trigger returns false and does not send; add an explicit
   `..._set_serial` entry point. Test keyboard select, drag-select (one send per
   completed drag) and programmatic select; two-client live middle-click paste.
   The headless paths are covered; the two-client live paste is recorded in
   [compositor-matrix.md](compositor-matrix.md) and the live suite, not as a
   fake-compositor wire test.
7. **Input semantics — delivered (`6000e25`, `d96cb1b`).**
   Restore button filtering so right/middle do not disturb
   left press/double-click; track `click_count` per button; make the motion
   callback return value match the header's "advisory" contract (or document and
   test the consume behaviour explicitly).
8. **ABI pins — delivered (`0d4ec5b`).**
   Add `_Static_assert`/`offsetof` for `pt_fd_event_*`,
   `pt_pointer_phase_*` and `pt_pointer_event` to `tests/test_abi.c`.

P1 — required before the tag, may land alongside P0:

9. **Paint-callback contract — delivered (`c46d57b`).**
   Write the purity/idempotence, no-tree-mutation,
   context-lifetime, clip-balance and no-reentrancy rules into
   `include/pt/custom.h` and the guide; add the tile-consistency golden test
   (1/3/unordered tiles produce identical pixels), the destroy-self-during-paint
   deferral test, and 1x/1.5x/2x goldens. `tests/test_window.c` covers tile
   order/count equality, self-destruction, reentrant snapshot rejection and
   clip imbalance restoration; the goldens live in `test_visuals`.
10. **IME contract — delivered, two visual items deferred to 1.2
    (`5c47fec`).** Define the custom-IME event surface and the full-state
    resend rule; treat empty `done` as ack; keep a local composition buffer;
    handle v2 `action`/`preedit_hint` without faking an Enter key; document the
    `delete_surrounding_text` byte/codepoint boundary. Fake text-input replay
    plus live niri+fcitx5 (v1), KWin+fcitx5 (v2), Mutter+IBus (v2).
    `preedit_hint` visual presentation and `show_input_panel`/`hide_input_panel`
    move to 1.2; the event surface, local composition buffer, v2 `action`
    handling and empty-`done` acknowledgement are in `tests/test_ime.c`. Wire
    replay and the three live IME combinations remain open.
11. **fd watch and raw input contracts — delivered (`da4cdda`, `d96cb1b`).**
    Borrowed/owned fd semantics,
    self-unwatch deferral, error-event guarantee, `run_for` timeout inclusion;
    raw-input dispatch order, consume scope and IME interaction. pipe/eventfd/
    timerfd tests and create/destroy fd-count stability.
12. **Documentation and bilingual sync — delivered (`572d3e3`, `c4f89a4`,
    `da0dfd7`, `b676843` and the M18 docs finalization).**
    Add custom-draw/IME/fd/raw-input/
    primary sections to quickstart, bindings, architecture and README; correct
    the stale "client-side" decoration wording and the 412/412 parity claims;
    sync every changed public guide into `README.zh-CN.md` and `docs/zh-CN/*`
    (including the Decoration policy section) and pass
    `tools/check-bilingual-docs.py`.
13. **Examples — delivered (`9bef87b`, `e8fff55`, `2456223`, `c9c2318`).**
    A C custom-paint/terminal-like example, a primary selection example and an
    fd-watch example, plus C++/Python equivalents and the Java trio
    (`CustomPaint`, `PrimarySelection`, `FdWatch`), all smoke-run under a
    nested niri.
14. **Binding tests — delivered where headless input allows (`e61391b`,
    `ce06814`, `b220410`).** Python and C++ have dispatch-level extension tests
    and Java has `PtTest` coverage for the landed wrappers. Raw callback
    dispatch still needs a real seat, so those assertions stay a live-suite
    item; registration and callback-free paths are covered headless.
15. **Commit hygiene and full gate — delivered.**
    Work landed split by theme with baseline, reference and docs alongside
    their code. `tools/ci.sh` passes end to end (werror, sanitizers,
    install-check, reproducible tarball, live suites) and
    `tools/release-check.sh --full` passes its automated gates; the two
    remaining human items are the freeze commit/tag and live-suite sign-off.
    The release-tree performance medians are recorded in
    [release.md](release.md).

### 4. Completion criteria

- `tools/release-check.sh --full` reports zero failures on the 1.1.0 tree:
  version agreement, 448/448 parity with an empty allowlist, generated
  reference covering every baseline function, and the bilingual check green.
  Status: delivered; the automated gates pass and only the owner's freeze
  commit/tag and live-suite sign-off remain as human items.
- `make check`, `make check-bindings`, `make perf-check`, `make check-wayland`,
  `make check-typing` and `tools/ci.sh` pass, including sanitizers, installed
  consumers and the reproducible tarball rebuild. Status: delivered; the full
  `ci.sh` run passed on the integration tree.
- Every protocol path added or changed in 1.1 has a fake-compositor sequence
  test and a live-compositor smoke result recorded against
  [compositor-matrix.md](compositor-matrix.md); known limitations are written
  there, not left implicit. Status: the fake-compositor facility is absent and
  stays open; the live result is the 48-check niri run.
- The new enums/struct are pinned in `tests/test_abi.c` (delivered). The
  release-time ABI comparison against `1.0.0` was done at symbol level
  (`nm -D`: 412 → 448, 0 removed, 36 added; SONAME `libpt.so.1` unchanged)
  because libabigail is not installed on the reference machine; `abidiff`
  remains the preferred check where available.
- The decoration, offscreen, primary-selection, paint-callback, IME, fd-watch
  and raw-input contracts are stated in the headers and exercised by tests that
  include failure paths (delivered).
- The 1.1.0 release record in [release.md](release.md) captures the audit,
  parity, live suites, performance figures and the ABI/behaviour diff summary
  (delivered, including the full `ci.sh` result); the tag stays uncreated
  until the owner freezes the release.

### 5. Risks and open questions

- Decoration appearance is a product decision, not a protocol one: no unified
  title bar can be promised across Mutter/KWin/niri/Sway/Weston. The acceptance
  target is "content area always correct, header ownership explainable".
- `xdg-decoration` v3 / `server_side_border` is not in the public XML at the
  retrieval date; 1.1 must not depend on it (compositor-matrix "To verify").
- KWin/fcitx5 `commit_string` loss is mitigated by full-state resend, not by a
  root-cause fix; the exact trigger is unconfirmed.
- The canvas render optimisation is validated by the golden images and the
  guard suite: the work-in-progress measurement is dense 4.275 ms and 4K
  8.429 ms for a full frame, both far below the ceilings. The remaining risk is
  behavioural, not performance: a future change to damage or snapshot semantics
  must keep the pure-snapshot tests green.
- The seven unwrapped functions are a release-blocking gap, not a scope risk.
  They are already declared, implemented and C-tested; only the wrappers and
  their dispatch tests remain before the tag.

## M19 - 1.2.0: common-but-missing surface (delivered; tagged 1.2.0)

Goal: ship the first post-1.0 horizon wave. 1.2.0 stays append-only within
`libpt.so.1`; every new function follows [standards.md](standards.md) section 2.
The engineering process that worked for M18 repeated: parallel C workstreams in
isolated worktrees, then an integration phase that regenerates the baseline and
reference, adds the three-language wrappers, examples and bilingual docs, bumps
the version and runs the full gate before the tag.

Status (2026-11-22): delivered on the final gate tree `859233f`; only the
annotated tag is left to the repository owner. The tree exports **554
functions / 907 baseline entries** (554 `func`, 281 `enum`, 43 `callback`, 10
`struct`, 12 `opaque`, 6 `define`, 1 `type`); `VERSION` and the four macros,
README, reference and this milestone say 1.2.0. All 554 declarations carry a
since marker (412 × 1.0, 36 × 1.1, 106 × 1.2) and the generated reference
exposes it per entry. The 35 headless C suites total 6,913 checks (the six new
suites contribute 3,434); the three-language wrappers reach 554/554 in C++,
Python and Java with an empty allowlist (C++ 763 checks, Python 64 tests, Java
620 checks); the bilingual check is green. `tools/ci.sh` passed end to end
(exit 0) and `tools/release-check.sh --full` reports only its two human items;
the measured figures are in [release.md](release.md).

### Wave 1 (parallel C workstreams, delivered)

1. **App-owned popups — delivered (`e4978f4`, anchor fix `6e9a616`).**
   `pt_popup` creates from a parent window and content widget, shows at an
   anchor rect with flip/slide constrained placement, repositions, dismisses on
   outside click/Escape/grab break with a dismiss callback, keeps the overlay
   family's headless fallback, enforces `PT_POPUP_MAX_OPEN`, and exposes a panel
   role while open. `tests/test_popup.c` passes 314 checks.
2. **Per-run font fallback — delivered (`c5a3dd5`, storage/contract note
   `12cb876`).** HarfBuzz script segmentation plus Fontconfig charset coverage
   selects a face per run; the app chain and per-family override are bounded,
   the cache is process-wide, metrics compose over the faces used, and
   `pt_app_font_uncovered_count` reports tofu codepoints. `test_font_fallback`
   passes 131 checks and degrades gracefully when a script font is absent.
3. **Clipboard media — delivered (`03f3f9a`, `12cac9f`).** `image/png` read and
   write with the 1.5 s / 16 MiB transfer caps and the 8192×8192 decoded-pixel
   cap, documented premultiplied-ARGB to straight-alpha conversion, and
   `text/uri-list` writes with CRLF and percent-encoded `file://` URIs.
   `test_clipboard_media` passes 871 checks including decode bombs, bounds and
   colour conversion.
4. **Column-layout persistence — delivered (`8816881`).** Versioned save/restore
   for widths, display order, visibility, keys and sort state; corrupt input,
   unknown versions, missing/duplicate keys and count mismatches are rejected
   atomically. `test_column_persist` passes 1,390 checks.

Integration landed the umbrella include, contract fix and wave-2 test slots in
`1d18d67`, regenerated the 1.2 baseline in `160a808`, refreshed baseline and
reference after icons/pickers in `997055e`, and derived the stub self-test from
the audit in `a5179a1`.

### Wave 2 (delivered)

- **Searchable select, date, number and colour pickers — delivered
  (`5e31e24`, `a8814cb`, `764be6a`, `5cc6043`, edge tightening `67c1083`).**
  The 70 functions in `pt/picker.h` cover a popup-backed searchable select with
  item data and a candidate cap, a typed date field with a fixed six-week
  calendar, a range/step spin box and a hex colour field with a preset palette.
  All four are editor fields on the shipped combo-box host kind; the
  `pt_combobox_*` model API is a no-op on them and ordinary comboboxes are
  unaffected. `test_picker` passes 550 checks covering keyboard, text commit,
  caps, silent setters and accessibility state/value.
- **Icon images and toplevel icons — delivered (`b03b629`, protocol
  generation `4280e74`).** The 12 functions add a reference-counted raster
  handle (PNG file/memory, hicolor lookup), the button image slot with tint,
  the window icon slot, a bounded per-size/scale LRU cache and best-effort
  `xdg-toplevel-icon-v1` submission. `test_icon` passes 178 checks. SVG is
  deferred (see open items).
- **Generated binding stubs from the API manifest — delivered (`b79e556`).**
  `tools/gen-binding-stubs.py` generates the C++/Python/Java skeleton from
  `api/pt.api`, and `tools/test_gen_binding_stubs.py` passes its 16 cases.

### Final integration (delivered)

- **C++ wrapper — delivered (`d2a1609`, `fa361be`, `360ad25`).** 554/554 with
  an empty allowlist; `bindings/cpp/test_pt.cpp` passes 763 checks covering the
  popup, font-fallback, clipboard, column, icon and picker paths plus the 1.2
  ABI pins.
- **Python wrapper — delivered (`b987f32`, `8dafc1b`, `3f48c04`).** 554/554;
  the unittest suite runs 64 tests over popup paths, fallback chains, clipboard
  media, column blobs, raster icons, picker values and callback dispatch.
- **Java wrapper — delivered (`c67ae29`, `8db2ec8`, `3948e77`).** 554/554; the
  Java test binary reports 620 checks with 0 failures.
- **1.2 examples — delivered (`0c6b1f7`, `f013ac3`).** 13 new examples, every
  one with a `run-*` target: six C (popup, pickers, icon button, clipboard
  image, font fallback, column persistence), two C++ (pickers, popup), two
  Python (pickers, popup) and three Java (pickers, popup, icon button).
- **Docs final — delivered (`fda270a` and the version/reference/guide commits
  above).** The binding tables in README and the guides name the 554 wrappers
  in each language, and the 1.2.0 release record carries the gate figures.

### Documentation and release surface (delivered)

- Header audit `6cc4e68` marks every declaration with 1.0/1.1/1.2 and pins the
  remaining `pt_date` offsets in `tests/test_abi.c` (13 checks).
- Version and reference `34c69c3` bumps `VERSION`/the macros to 1.2.0 and
  regenerates `docs/api-reference.md` at 554 functions with since markers.
- README `e0e0f34` and the public guides `bf01726` describe the six groups in
  English and Simplified Chinese; the bilingual check reports 9 clean pairs.
- The 1.2.0 release record in [release.md](release.md) carries the final gate
  figures measured on `859233f`; nothing is left as `pending final gate run`.

### Open items (carried past M19)

- **Fake-compositor replay is still absent.** Popup, font-fallback and clipboard
  paths have headless state tests plus the live suite; the in-process protocol
  replay facility is the same M18 gap, not silently claimed here.
  **Closed in M20:** `tests/fake_compositor.h` landed in `21a9e32`/`898b43b`
  and was hardened in `2ef3d67`; see the M20 wave 1 record.
- **SVG icons are deferred.** `pt_icon_image` accepts PNG only; a scalable-only
  hicolor theme does not resolve. The header and release record say so.
  **Closed in M20 (`b24c4b1`):** SVG is accepted by content sniffing through
  vendored nanosvg and `scalable/` resolves. SVGZ and the unsupported part of
  the SVG subset remain open.
- **The number picker has no spin role.** It reports the combo-box role and no
  expandable state because the 1.2 accessibility enum has no spin-box role;
  appending one is a later minor addition.
  **Closed in M20 (`f6331d0`):** `pt_a11y_role_spin_button` is value 36 and the
  count sentinel is 37.
- **Pickers are locale-independent by design.** The date format is explicit, the
  committed number decimal separator is `.`, and no localized month or group
  separator is read from the process locale. This remains a documented
  contract, not an open defect.
- **The toplevel-icon path has no live compositor recording.** The matrix
  records KWin/Labwc/Muffin/Jay/Pila as exposing `xdg-toplevel-icon-v1`, but no
  live recording was made in this pass; the submission path is covered headless
  and niri/Sway/Weston take the tested no-op path. **Still open after M20:**
  the wire test records the submission, but no live recording was added.
- **Binding raw-callback dispatch still needs a live seat.** Registration and
  callback-free paths are headless-tested for all three bindings; asserting that
  an actual raw key or pointer event reaches a wrapped callback remains a live
  suite item. **Closed at wire level in M20 (`08103d2`):**
  `tests/wire_driver.c` injects real wire events into the C++, Python and Java
  callbacks; a physical-seat run remains optional live coverage.
- **M18's two IME visual items remain deferred.** `preedit_hint` presentation and
  `show_input_panel`/`hide_input_panel` were moved to 1.2 but are not part of
  this wave; the event surface and full-state resend remain implemented.
  **Closed in M20 (`3a13680`, `2966daa`):** the seven hint kinds render as
  graded underlines and the two input-panel requests are since 1.3.

### Completion criteria

Same gate set as M18 section 4. Final status on `859233f`:

- `make check` / `make check-bindings`: pass — 35 C suites with 6,913 checks;
  C++ 763 checks, Python 64 tests, Java 620 checks, 0 failures; binding parity
  554/554 in all three languages with an empty allowlist; exported-symbol,
  baseline and generated-reference audits clean.
- `make perf-check`: pass — every guard ceiling holds; dense full paint
  6.078 ms, 4K full paint 8.571 ms, first frame 13.494 ms, idle 300 ms CPU
  0.451 ms.
- Live suites: `make check-wayland` 48 checks under niri (focused-entry idle
  26.6 ms over 5000 ms after one external-input retry); `make check-typing`
  PASS (ptdirect, text-input-v3).
- `tools/ci.sh`: pass end to end, exit 0 (`CI verification passed for Pt
  1.2.0`) — werror build, check, perf, staged install with the four consumers,
  ASan+UBSan including the bindings, a reproducible tarball
  `54ce8abd…c521ac` and an extracted rebuild that passes `make check`.
- `tools/release-check.sh --full`: every automated gate passes for 1.2.0; the
  two open items are the human ones it always reports (freeze commit/tag and
  live-suite sign-off — the live suites above were run on this tree).
- Bilingual sync, version agreement, 554-function baseline and reference
  coverage: **delivered and passing**; the reference names all 554 functions.
- ABI against 1.1.0: append-only at the symbol level — 448 → 554 exported
  `pt_*` symbols, 0 removed, 106 added, no type changes among the 448 common
  symbols, SONAME `libpt.so.1` on both builds. `abidiff` is not installed on
  the reference machine (no libabigail, no elevated privileges to add it), so
  the comparison is symbol-level; a type-level run is still recommended where
  libabigail is available.
- A 1.2.0 release record: **delivered** in [release.md](release.md) with the
  figures above and no `pending final gate run` markers left.

## M20 - 1.3.0: wire verification and contract completion (delivered; tagged 1.3.0)

Goal: close the appendable items carried past M19 without touching the 2.0
structural horizon. 1.3.0 stays append-only within `libpt.so.1`; new functions
are marked since 1.3. The M18/M19 process repeated: parallel C workstreams in
isolated worktrees with pre-registered test slots, an integration phase that
regenerated the baseline and reference, then wire consumers, C++ hardening,
bilingual docs and the full gate before the tag.

Status (2026-11-22): delivered and gated on the final integration tree
`fafbcae`; the annotated tag awaits the owner's freeze confirmation. The tree
exports **556 functions / 910 baseline entries** (556 `func`, 282 `enum`, 43
`callback`, 10 `struct`, 12 `opaque`, 6 `define`, 1 `type`); `VERSION` and the
four macros, README, reference and this milestone say 1.3.0. All 556
declarations carry a since marker (412 × 1.0, 36 × 1.1, 106 × 1.2, 2 × 1.3) and
the generated reference exposes it per entry. The 35 headless C suites total
7,702 checks plus the 454,380-check property sweep (36 executables); the
three-language wrappers cover the whole surface at 556/556 with an empty
allowlist (C++ 844, Python 67, Java 685 checks). `tools/ci.sh` exits 0 end to
end and `tools/release-check.sh --full` is green apart from its two human open
items; the measured figures are in [release.md](release.md).

### Wave 1 (parallel C workstreams, delivered)

1. **Fake-compositor replay facility — delivered (`21a9e32`, `898b43b`,
   hardening `2ef3d67`).** `tests/fake_compositor.h` is a header-only Wayland
   server on a private unix socket running on a helper thread; Pt connects
   through `WAYLAND_DISPLAY` as an ordinary client, so the shipped connection
   path runs unchanged and no libwayland-server is linked. It advertises the
   globals Pt uses plus optional capability bits (`FC_OPT_*`) and
   `FC_OPT_TEXT_INPUT_V1` for the v2 suppression path, decodes and records every
   request with typed arguments, injects server events and synchronises with a
   display roundtrip. First consumers assert the popup positioner's placement
   wire parameters, record the toplevel-icon submission, replay decoration
   configure/tiled state, drive IME text-input traffic and inject raw
   key/pointer/selection events. `test_fake_compositor` passes 275 checks; test
   infrastructure only, no public ABI change.
2. **IME visual completion — delivered (`3a13680`, `2966daa`).**
   `preedit_hint` ranges are stored with the preedit and painted as graded second
   underlines (whole, selection, prefix/suffix, prediction, spelling/compose
   errors); a hint-free preedit is pixel-identical to 1.2. The two input-panel
   requests are appended as `pt_window_show_input_panel`/`hide_input_panel`
   (since 1.3) with boolean desired-state semantics, v1 suppression and replay
   on reactivation. `test_ime` passes 314 checks.
3. **SVG icons — delivered (`b24c4b1`, protocol-free vendoring recorded in
   `THIRD_PARTY.md`).** The vendored zlib-licensed nanosvg accepts SVG alongside
   PNG by content sniffing, hicolor `scalable/` lookup resolves `.svg`, and
   rasterisation feeds the existing per-size/scale cache. The supported subset
   and the ignore-descendants rule for text/images/filters/masks/clipPath/
   use/symbol/markers/animation/CSS cascade are documented in `pt/icon.h`;
   straight-alpha output is premultiplied into Pt's `0xAARRGGBB` layout.
   `test_icon` passes 330 checks (up from 178).
4. **Spin-button accessibility role — delivered (`f6331d0`).**
   `pt_a11y_role_spin_button` is appended as value 36, moving
   `pt_a11y_role_count` to 37; the number picker reports it with no expandable
   state and the AT-SPI bridge maps it to `SPIN_BUTTON`. `test_picker` passes 553
   checks, `test_a11y` 148 and `test_abi` pins the new sentinel.

Integration `2c257ce` merged the wave, and `0509883` refreshed the 1.3 baseline
and generated reference (556 functions / 910 entries).

### Wave 2 (delivered)

- **Wire consumers — delivered (`9535ef0`, `8004446`, `0cb2610`).**
  Text-input v3 event and input-panel injection, composition replay, and
  decoration configure/tiled-state replay run on the fake compositor, closing
  the protocol-sequence gaps M19 recorded.
- **Binding wire dispatch — delivered (`08103d2`, `968107b`, `97645cd`).**
  `tests/wire_driver.c` exposes the header-only C facility as a flat C ABI
  (`libwire_driver.so`), so the C++, Python and Java suites assert that a real
  wire key or pointer event reaches a wrapped callback in every language; the
  M19 live-seat item is closed at wire level. The five owned-string/buffer
  getters release through an RAII guard so `std::bad_alloc` cannot leak, and
  `pt::popup` documents that it must be destroyed before its parent window.
  At that integration merge (`c9f7385`) C++ passed 837 checks, Python 66 tests
  and Java 676 checks; the final tree closes parity at 556/556 with C++ 844,
  Python 67 and Java 685.

### Open items (carried past M20, annotated after M21)

- **Live IME composition matrix is still unrecorded.** The wire replay covers
  text-input v3 composition, preedit hints and input panels, but a live
  niri+fcitx5 and KWin/Mutter composition run is still missing; the environment
  was not available in this round and the record says so rather than implying
  it. **Closed for niri+fcitx5 in M21 (`b019b8c`):** the live preedit sequence
  and the committed `你好` are recorded through text-input-v3. KWin+fcitx5 v2
  and Mutter+IBus v2 remain open with no environment.
- **The SVG subset is honest, not complete.** SVGZ (gzip-compressed SVG) and
  text, raster images, filters, masks, clipPath, use/symbol, markers, animation
  and the full CSS cascade are unsupported; the parser ignores them and visits
  their descendants. A document with no drawable shape is rejected.
  **Closed for SVGZ in M21 (`2dddd11`):** both loaders inflate it with bounded
  zlib and hicolor lookup resolves `.svgz`. The non-SVGZ subset above remains
  ignored by design.
- **Touch and tablet wire injection are not covered.** The fake compositor
  injects key, pointer, popup, decoration, selection and IME traffic; the
  `wl_touch` and tablet paths still rely on headless state tests plus the live
  suite. **Closed in M21 (`389d527`):** `test_wire_input` injects both matrices
  and passes 595 checks, including drag-and-drop and primary selection.
- **The toplevel-icon submission has no live compositor recording.** Headless
  submission and the request parameters are asserted on the wire; the
  matrix's KWin/Labwc/Muffin/Jay/Pila exposure was not run, and niri/Sway/Weston
  take the tested no-op path. **Still open after M21:** no live recording was
  added.
- **Binding raw dispatch is closed at wire level, not on a live seat.** The
  wrapped callbacks now receive injected wire events in all three languages; a
  physical-seat run remains optional live coverage. **Still optional live
  coverage after M21.**
- **Pickers are locale-independent by design.** The date picture is explicit,
  the committed number separator is `.`, and the process locale is never read.
  This is a documented contract, not an unfinished translation path.
- **A multi-touch focus bug survived the 1.3 wire work.** Clearing
  `touch_focus` on the first contact lift orphaned the remaining contacts.
  **Closed in M21 (`b947a6e`):** focus is held until the last contact, the
  surviving contact drives the emulated pointer, and the correction is listed
  as a behaviour change in the 1.4.0 record.

### Completion criteria

Same gate set as M19: `make check`, `make check-bindings`, `make perf-check`,
live suites, `tools/ci.sh`, `tools/release-check.sh --full`, bilingual sync,
three-language parity with an empty allowlist, ABI pins and an append-only diff
against `1.2.0`, and a 1.3.0 release record.

Measured on the final gate tree `fafbcae` (`tools/ci.sh` exit 0):

- `make check` C suites: 36 executables (35 suites plus the property sweep),
  7,702 checks plus the 454,380-check property sweep; `test_fake_compositor`
  275, `test_ime` 314, `test_popup` 466, `test_icon` 330, `test_picker` 553,
  `test_abi` 13.
- Binding suites: C++ 844 checks, Python 67 tests, Java 685 checks, 0 failures.
  Parity is 556/556 with an empty allowlist.
- Version/bilingual/reference: 1.3.0 agrees across `Makefile`, `base.h`, the
  README status line and the reference header; the reference names 556
  functions; the bilingual check reports 9 clean pairs.
- `make perf-check`: every ceiling holds; dense full paint 5.262 ms, 4K full
  paint 8.469 ms, first frame 14.512 ms, idle 300 ms CPU 0.598 ms.
- Live suites: `check-wayland` 48 checks under niri (focused-entry idle
  25.1 ms over 5000 ms after one external-input retry); `check-typing` PASS
  (ptdirect, text-input-v3).
- Sanitizer/staged install/tarball: the separate `build/ci/sanitize` tree is
  rebuilt from empty with ASan+UBSan (all 36 C binaries, `test_cpp` 844,
  Python 67, Java 685; `ldd` shows libasan/libubsan); the staged install runs
  the C/C++/Python/Java consumers and `uninstall`; two `dist` runs reproduce
  the gate-time archive
  `e989e6a4c837145f2bc92247cdad27ded9ffa500331518bf904693260ea36a62` and the
  extracted archive rebuilds and passes `make check`.
- ABI against 1.2.0: 554 → 556 exported `pt_*` symbols, 0 removed, 2 added
  (`pt_window_show_input_panel`/`hide_input_panel`), no type changes among the
  554 common symbols, SONAME `libpt.so.1` unchanged; `abidiff` is unavailable
  on the reference machine, so the run is symbol-level.
- `tools/release-check.sh --full`: every automated gate passes; the two open
  items are the human ones (freeze commit/tag and live-suite sign-off).
- A 1.3.0 release record: **delivered** in [release.md](release.md) with the
  figures above and no `pending final gate run` markers left.

## M21 - 1.4.0: input wire matrix completion and format polish (delivered; tag pending)

Goal: close the remaining appendable items carried past M20 without touching
the 2.0 structural horizon. 1.4.0 stays append-only within `libpt.so.1`; the
public-symbol delta is zero (SVGZ extends the existing loaders by content
sniffing, the wire work is test infrastructure). The M18/M19/M20 process
repeated: parallel workstreams in isolated worktrees with a pre-registered test
slot, integration, bilingual docs and the full gate before the tag.

Status (2026-11-22): delivered and integrated on `e1104c2`, which merges the
`m21/docs-final` documentation branch (version bump, generated reference and
bilingual guides, `1460643`) and the SVGZ example refresh from `m21/examples`
(`043c0d8`) on top of the wave-1-plus-wave-2 tree `b947a6e`. The final gate run
on `e1104c2` is complete: `make check`, `make check-bindings`,
`make perf-check`, the live suites including `make check-ime-live`,
`tools/ci.sh` and `tools/release-check.sh --full` all pass. The tree exports
**556 functions / 910 baseline entries** (556 `func`, 282 `enum`, 43
`callback`, 10 `struct`, 12 `opaque`, 6 `define`, 1 `type`), exactly as 1.3.0,
and every declaration keeps its since marker (412 × 1.0, 36 × 1.1, 106 × 1.2,
2 × 1.3). 1.4.0 adds no public function, enum or struct; the annotated tag
awaits the owner's freeze confirmation.

### Wave 1 (parallel workstreams, delivered)

1. **Input wire completion — delivered (`389d527`, with the touch, tablet, drag
   and primary replays under it).** The fake compositor gained `wl_touch`
   (down/up/motion/frame/cancel, multiple contacts and pinch, capability
   removal), tablet-v2 (seat/tool lifecycle, proximity, pressure, tilt,
   rotation, slider, wheel and buttons), `wl_data_device` drag traffic
   (enter/motion/drop/leave, offer accept/receive over an `SCM_RIGHTS`
   descriptor and finish, source target/action/cancelled/`dnd_finished`) and
   primary-selection injection and replay. The pre-registered `test_wire_input`
   slot passes 595 checks; a `wait_object` helper removes the server-state race
   (200 consecutive runs, zero failures); and the facility's tablet-manager
   request table was corrected (`get_tablet_seat`/`destroy` had been
   transposed). Test infrastructure only: no public header, symbol or ABI.
2. **SVGZ icons — delivered (`2dddd11`).** Both loaders sniff the gzip magic
   `0x1f 0x8b` before the text sniff, inflate through the already-linked zlib
   with a 16 MiB bound on the compressed input and the inflated document (a
   decompression-bomb guard), and require the inflated bytes to pass the SVG
   sniff. A non-SVG gzip archive is rejected; the handle retains the inflated
   source and re-parses it per size. Hicolor `scalable/` lookup tries `.svg`,
   then `.svgz`, then `.png`; fixed-size directories stay `.png` only.
   `pt/icon.h` marks the contract "Since 1.2; SVGZ since 1.4". Zero new public
   functions; `test_icon` passes 800 checks, up from 330.
3. **Live IME resolution — delivered (`b019b8c`, harness `4b3b9eb`).** A
   runtime registry check showed niri 26.04 advertising
   `zwp_input_method_manager_v2` v1 and fcitx5 5.1.23 running its pinyin
   engine. Typing `nihao ` advances the preedit `n` → `ni` → `ni h` →
   `ni ha` → `ni hao` and the space commits `你好` through text-input-v3 into
   Pt (`PREEDIT_COUNT=5`). The decisive finding is that a virtual keyboard
   created on the nested display bypasses the input-method grab, so the harness
   injects into the parent display and lets the nested niri receive the keys as
   a real keyboard would. The harness is `tools/live-ime.sh` plus
   `tests/ime_live_probe.c` (public API only), runs as `make check-ime-live`,
   skips with rc 0 when no parent niri is available, isolates fcitx5 on a
   private session bus, and hardens the run with bounded waits,
   signal-triggered teardown and a focus recheck immediately before injection.
   It is wired into the live section of `tools/ci.sh`. KWin+fcitx5 and
   Mutter+IBus remain absent environments.

Workstream rules: as M20 — implementation + C tests landed together;
`test_wire_input` was pre-registered; `src/*.c` is wildcard-built; workstreams
did not edit bindings, generated artifacts or public guides.

### Wave 2 (delivered)

- **Multi-touch focus correction — delivered (`b947a6e`).**
  `src/wayland/platform.c` cleared `touch_focus` unconditionally when the
  first contact lifted, so motion and up for the remaining fingers were
  dropped, pinch active state could survive, cancel could not clean up, and a
  repeated pinch failed. The internal `pt_window_input_touch_up` now returns
  the remaining contact count (`void` to `uint32_t`, not a public ABI change),
  the platform clears focus only at zero, and `src/window.c` routes a surviving
  single contact's motion into the emulated pointer. Pinch still never clicks;
  `test_damage` stays at 98 checks. This is an intentional behaviour
  correction, and `test_wire_input` now asserts the correct behaviour (focus
  survives the first lift, the survivor moves and releases cleanly, a new pinch
  starts from a fresh baseline) instead of recording the old limitation.

### Integration (delivered)

`44c265f` merged the wave and refreshed the generated reference, adding the
live IME target to the gate; `b947a6e` is the integration tree on top of it.
`e1104c2` merges the version bump, reference refresh and bilingual guides from
`m21/docs-final` (`1460643`) and the SVGZ examples from `m21/examples`
(`043c0d8`), with no conflicts. No public API moved at any step.

### M20 open items closed or still carried

- **Live IME composition matrix:** the niri+fcitx5 cell is recorded
  (`b019b8c`); KWin+fcitx5 v2 and Mutter+IBus v2 still lack their environments
  and remain open.
- **SVG format support:** SVGZ is supported (`2dddd11`); the rest of the SVG
  subset (text, raster images, filters, masks, `clipPath`, `use`/`symbol`,
  markers, animation and the full CSS cascade) is still ignored by the parser.
- **Touch and tablet wire injection:** built (`389d527`, `test_wire_input`
  595 checks); the paths no longer rely only on headless state tests.
- **Toplevel-icon live recording:** still open. The wire test asserts the
  submission and niri/Sway/Weston take the tested no-op path; the matrix's
  KWin/Labwc/Muffin/Jay/Pila exposures were not run.
- **Binding raw dispatch on a physical seat:** still optional live coverage;
  wire dispatch is closed in all three languages.
- **Multi-touch focus bug:** fixed (`b947a6e`), recorded as a behaviour
  change in the release notes.
- **Pickers are locale-independent by design:** retained as a documented
  contract, not an open defect.

### Completion criteria

Same gate set as M20, measured on `e1104c2` and recorded in
[release.md](release.md): `make check` passes 37 executables (36 suites plus the
property sweep) at 8,991 checks plus the 454,380-check property suite;
`make check-bindings` passes C++ 844, Python 67 and Java 685 with 0 failures;
three-language parity is 556/556 with an empty allowlist; `make perf-check`
holds every ceiling (dense full paint 5.435 ms, 4K 7.585 ms, first frame
14.34 ms, idle 300 ms CPU 0.537 ms); the live suites pass (`check-wayland` 48,
`check-typing` ptdirect, `check-ime-live` `你好` with `PREEDIT_COUNT=5`);
`tools/ci.sh` exits 0 with the ASan+UBSan build, staged install and
reproducible tarball clean; and `tools/release-check.sh --full` reports every
automated gate green. The bilingual pair set is in sync. The ABI pin suite
passes and the symbol-level diff against `1.3.0` is empty: 556 → 556 exported
`pt_*` symbols, 0 added, 0 removed, no type changes, SONAME `libpt.so.1`
unchanged. The baseline stays at 556 functions with zero new symbols. The only
step left is the owner's freeze confirmation and tag.

## M22 - 2.0.0: RTL and bidirectional text (delivered)

Goal: implement the Unicode Bidirectional Algorithm and RTL layout/editing per
[rtl.md](rtl.md), the first structural major since 1.0. The public surface is
additive: 8 direction functions and 2 enums (6 values); zero-value defaults
preserve LTR behavior, and the byte-offset and accessibility character-offset
contracts do not move. 2.0.0 keeps SONAME `libpt.so.1` (the major marks the bidi
rendering correction, not a binary break); `ABI_MAJOR` decouples the SONAME from
`VERSION` in the Makefile and release-check.

Status (2026-11-23): **delivered** on `m22/integration`. The integration tree
`a02a40e` (the merge of `m22/w5b-fix` `6553270` and `m22/w6b-docs` `b4bdf99`)
was fast-forwarded to the W7 hardening tip `91176c2` (12 commits, `a361d5b`
through `91176c2`), and the final gate ran on that tree. It exports **564
functions / 924 baseline entries**; the three bindings report 564/564 with an
empty allowlist; Unicode 18.0.0 conformance passes 770,241 + 91,707 cases with
0 failures; SONAME is `libpt.so.1` with `ABI_MAJOR := 1`. The full gate record
is in [release.md](release.md): 41 C executables (585,878 checks), C++ 862,
Python 68, Java 701, perf ceilings green including the three bidi profiles,
sanitizers clean with `libasan`/`libubsan` linked, staged install plus
four-language consumers, reproducible tarball with an extracted rebuild, four
live suites under nested niri (including the new RTL caret/selection check),
11/11 LTR `SAME` against 1.4.0, and the symbol ABI diff 556 → 564 with 0
removed / 0 changed / 8 added. The annotated tag `2.0.0` (object `cdd78c3`)
was created from `12dbbf6` and pushed to `origin`; M22 is fully closed.

Decisions (researched 2026-11-22, kept as taken):

- Bidi engine: vendored SheenBidi v3.0.0 (Apache-2.0, tag commit `cfe430e`,
  UCD 17.0.0), unity-compiled to one object under `third_party/sheenbidi`; no
  FriBidi (LGPL static-link cost). The UTF-8 entry point has an upstream L1
  line-end whitespace bug, so Pt's adapter decodes to UTF-32 with a
  byte-offset table and drives the internal path; `PROVENANCE.md` records it.
- Conformance: Unicode `BidiTest.txt` + `BidiCharacterTest.txt` (18.0.0,
  Unicode License v3) vendored gzipped under `tests/data/bidi`; a headless
  conformance suite runs in `make check` (770,241 + 91,707 cases, 0 failures,
  about 0.5 s).
- Caret: visual arrow movement by default; logical movement via the existing
  window key callback escape hatch; no new caret-mode API in 2.0.
- Spans: `pt_text_span` stays frozen (style only, no direction field).
- Fonts: missing Arabic/Hebrew glyphs are documented fallback/tofu, not a hard
  error; tests use DejaVu Sans / Noto with skip.
- `padding`/`margin` stay physical; table/tree keep logical column order.
- Accessibility: object-level `direction` plus `Text.GetAttributes` and
  `GetDefaultAttributes` direction; the C getter keeps byte offsets and only the
  D-Bus `Text` interface uses scalar offsets. The earlier rtl.md sentence that
  called every accessibility getter scalar was corrected in this wrap-up.

### Delivered waves and evidence

| Wave | Scope | Evidence |
| --- | --- | --- |
| W0 | SheenBidi vendoring, UTF-32 adapter, conformance suite | `52d6034` |
| W1 | Direction enums/API, resolution, bindings, 564/924 baseline | `25b5b6c` |
| W2 | UBA levels, per-run shaping, single-line visual rendering | `2098382` |
| W3 | Wrapping, per-line base, logical/visual maps, L1 whitespace, ellipsis | `3bb7f2d` |
| W4 | Visual caret/selection/IME, affinity, password bullets, multi-rect paint | `d8654f6` |
| W5a | Flex/wrap/grid/scroll geometry, table/tree logical columns, navigation/picker/CSD/overlay mirroring (non-paint) | `08d796f` |
| W5b | Entry right-anchor, scrollbar side, tree chevron, menu first-open anchor, paint chrome | `6553270` |
| W6a | AT-SPI object and text direction attributes, password-safe path | `69db766` |
| W6b | README/guides/rtl.md final, ROADMAP/release records, RTL examples | `b4bdf99` |
| W7prep | Cross-version LTR byte-compatibility gate (`tools/ltr-compat.sh`, 10 scenes; 11 after W7) | `43cfad6` |
| W7 | Fuzz/perf hardening, live RTL consistency, ci.sh LTR stage, a11y bus isolation, CSD-title and entry-shaping fixes, final gate, ABI diff | `91176c2` (12 commits, `a361d5b` through `91176c2`) |
| Integration | W0-W4 + W5a + W6a + W6b + W5b + W7prep on `m22/integration` | `a02a40e`, fast-forwarded to W7 tip `91176c2`; final gate record backfilled at `a36b1c9` |

Landed detail per wave:

- **W0.** `third_party/sheenbidi` is byte-identical to v3.0.0 apart from
  `PROVENANCE.md`; the Makefile compiles it with `-DSB_CONFIG_UNITY`, isolates
  its warnings and keeps `-fvisibility=hidden` so no `SB_*` symbol is exported.
  `src/bidi.c` owns the UTF-32 adapter and byte maps; no engine type escapes it.
  `test_bidi_conformance` runs the gzipped Unicode 18.0.0 data.
- **W1.** `pt_ui_direction` (`inherit`/`ltr`/`rtl`) and `pt_text_direction`
  (`auto`/`ltr`/`rtl`) plus app, window and widget UI setters/getters and
  widget text setters/getters. Resolution is widget → nearest explicit ancestor
  → window → app → LTR; text is widget → ancestor → auto, with neutral-only
  fallback to the resolved UI direction. Getters return the explicit value.
  Baseline and reference read 564 functions / 924 entries; the three bindings
  wrap 564/564 with an empty allowlist.
- **W2.** Levels, X9 removal/restoration semantics (controls stay in shaping and
  are skipped when painted), reorder, per-run explicit HarfBuzz direction,
  automatic bracket mirroring, Arabic joining, per-paragraph base, the LTR fast
  path and the shape-cache direction key. LTR snapshots stay byte-identical.
- **W3.** Logical wrapping into candidate lines, per-line visual reorder with a
  per-paragraph base, L1 trailing-whitespace reset, visual-end ellipsis,
  RTL counter and bidi goldens, and the logical/visual byte map that measurement
  and hit testing consume.
- **W4.** Visual caret edges, affinity at soft-wrap boundaries, Home/End visual
  line ends, Ctrl+Home/End document logical, Shift logical anchor,
  Backspace/Delete logical, multi-rectangle selection per visual run, visual IME
  rectangles, password bullets from the stored text direction, and the soft-wrap
  boundary fix (`d8654f6`) that assigns a caret to the correct line.
- **W5a.** Flex row/wrap/grid placement, start/end alignment and justify,
  scrollbar side, tabs, sidebar, picker popups, client-side decoration and
  overlay chrome mirror from the resolved UI direction; table and tree columns
  keep logical order through an internal opt-out; padding/margin remain
  physical.
- **W6a.** Every accessible object publishes the resolved UI direction; text
  objects publish `direction` through `GetAttributes` and
  `GetDefaultAttributes` with a scalar range; a password entry reports direction
  and caret but no character.
- **W5b.** Entry text is right-anchored in RTL at `scroll_x = 0`, the scrollbar,
  tree chevron and menu first-open anchor follow the resolved UI direction, and
  the CSD/combo chrome paints on the mirrored side. LTR snapshots stay
  byte-identical.
- **W6b.** The README and public guides describe the delivered behavior,
  `rtl.md` is rewritten as a delivered design in English and Simplified
  Chinese, and the four-language RTL examples ship with `run-*` targets.
- **W7prep.** `tools/ltr-compat.sh` builds 1.4.0 and the current tree in
  isolated temporary trees, renders a fixed LTR corpus and compares PPM images
  byte for byte. It checks in no golden hash because font rasterization differs
  between machines. The required result is 11/11 `SAME`.
- **W7 hardening.** `test_bidi_fuzz` runs a fixed-seed mixed-script corpus
  through paragraph/runs, the visual map and the entry editing path; `make
  perf-check` gains three bidi/RTL ceilings; `tools/check-rtl-live.sh` verifies
  live IME rectangle/caret-map agreement; `test_a11y_bridge` runs on a private
  accessibility bus; `tools/ci.sh` runs the cross-version LTR gate and the live
  RTL check. W7 also restored the 1.4.0 CSD-title origin and mirrored it for
  RTL, kept a mark's own script when attaching runs (shaping idempotence), and
  added the decorated-title scene, so the LTR gate compares **11** scenes at
  11/11 `SAME`.

### State on the final gate tree

Verified on `91176c2` (the integration tree `a02a40e` fast-forwarded to the W7
hardening tip):

- `make -j8 werror=1` builds without warnings; `make check` is green
  (41 C executables, 585,878 checks, including `api-check`); `make
  check-bindings` reports C++ 862, Python 68 and Java 701 with 0 failures.
- Conformance: Unicode 18.0.0 `BidiTest` 770,241 cases and
  `BidiCharacterTest` 91,707 cases, 0 failures.
- API: 564 functions / 924 frozen entries; binding parity 564/564 (100%) for
  C++/Python/Java, 0 missing, 0 allowlisted; the generated reference reads
  Version 2.0.0 · 564 functions.
- `test_direction` (105), `test_bidi` (351), `test_bidi_conformance` (8),
  `test_bidi_fuzz` (121,050) and `test_rich_text` (345) pass, as do the RTL
  cases in `test_text_area`, `test_visuals`, `test_table`, `test_tree` and
  `test_navigation`; the LTR visual goldens are unchanged.
- ABI: SONAME `libpt.so.1`, 564 `pt_*` exports, no `SB_*` exports; against
  1.4.0 (`e09fef4`) the symbol diff is 556 → 564, 0 removed, 0 changed,
  8 added.
- `ASAN_OPTIONS=detect_leaks=1` sanitizer runs of 41 C binaries plus
  `test_cpp` report nothing; `ldd` confirms `libasan.so.8` and `libubsan.so.1`
  on the test binary, the library and the wire driver; the sanitized Python and
  Java suites are clean.
- `make perf-check` holds every ceiling plus the new bidi/RTL ceilings
  (mixed paragraph wrap+paint 0.874 ms / 2.0 ms, 100-row RTL table paint
  2.896 ms / 7.0 ms, 1000 RTL caret moves 43.385 ms / 100 ms).
- The staged install and four-language consumers pass, two `dist` runs at
  `SOURCE_DATE_EPOCH=1767225600` reproduce one digest and the extracted
  tarball rebuilds (`werror=1`) and passes `make check`.
- `test_a11y_bridge` on a private bus, the four live suites under nested niri
  (`check-wayland` 48 checks, `check-typing` PASS, `check-ime-live` PASS with
  `你好`, `check-rtl-live` PASS) and `tools/ltr-compat.sh 1.4.0 HEAD`
  (11/11 `SAME`) pass.

Closed at the final gate:

- `tools/release-check.sh --full` reports every automated gate green and the
  two human open items (freeze commit/tag and live-suite sign-off, both
  recorded).
- The two gate-script corrections are in the tag tree (`1992f86`):
  `tools/install-check.sh` expands `$(ABI_MAJOR)` the way
  `tools/release-check.sh` does, and `tools/ci.sh` creates `build/tests` and
  points `PT_WIRE_DRIVER` at the driver it built. Bare `tools/ci.sh` on
  `6775485` exits 0 (`CI verification passed for Pt 2.0.0`) through every
  stage, including the sanitizer, staged-install, tarball and live suites.

### Completion criteria

Achieved on the final gate tree `91176c2`:

- `make check` includes `test_direction`, `test_bidi` and
  `test_bidi_conformance`; the baseline is 564 functions / 924 entries and the
  generated reference covers all of them.
- Three-language parity is 564/564 with an empty allowlist.
- UBA conformance is 770,241 + 91,707 cases with 0 failures.
- `rtl.md` is rewritten as a delivered design (decision records, entry
  right-anchoring, visual caret, logical columns, physical padding, bounded
  per-rebuild performance wording, byte/scalar correction and limitations) in
  English and Simplified Chinese.
- The LTR double gate is in place: 17 pure-LTR snapshots unchanged and
  `tools/ltr-compat.sh` at 11/11 `SAME`.
- The RTL examples exist in all four languages with `run-*` targets and a
  headless smoke result.
- SONAME stays `libpt.so.1` through `ABI_MAJOR`, and `api-policy.md` records
  why a behavior-only major does not bump the object name.
- The final gate ran every suite, the bidi performance profiles, the four live
  suites, `tools/ci.sh` stages and `tools/release-check.sh --full`; the ABI
  diff against 1.4.0 is 8 added, 0 removed, 0 changed; every
  `pending final gate run` marker in [release.md](release.md) is replaced.

**M22 is closed.** 2.0.0 is tagged and pushed: the annotated tag `2.0.0`
(object `cdd78c3e3beb3e7d934cadc488fc561b430324fe`) points at commit `12dbbf6`,
and `git ls-remote origin refs/tags/2.0.0` returns that object. M22 is fully
closed. The gate-facility debt was fixed rather than carried: `1992f86` in the
tag tree expands `$(ABI_MAJOR)` in `tools/install-check.sh` and gives
`tools/ci.sh` its test paths, and
[release.md](release.md#gate-transcript) records both corrections.

## M23 - 2.1.0: RTL ergonomics and safety (delivered; tag-ready)

Goal: close the four RTL ergonomics and safety gaps that 2.0 documented as
deliberate limitations, in a minor release that is purely additive. The full
design, signatures and zero-regression arguments are in
[m23-design.md](m23-design.md); the release record is in [release.md](release.md).

Status (2026-11-23): **W1-W5 delivered; the tree is tag-ready and M23 is
closed.** The branches are `m23/w1-caret-locale`, `m23/w2-bidi-safety`,
`m23/w3-logical-padding`, `m23/w4-docs` (tip `67e87b7`) and `m23/w5-gate`
(the final gate). The baseline
is **576 functions / 952 entries** (576 `func`, 303 `enum`, 43 `callback`, 11
`struct`, 12 `opaque`, 6 `define`, 1 `type`): 12 new functions, 3 new enums (15
values) and 1 new public struct. Parity is 576/576 in C++, Python and Java with
an empty allowlist. SONAME stays `libpt.so.1`; `ABI_MAJOR` stays 1 because
nothing is removed or changed.

The design projected 574/950 and 10 functions. Review closed the open questions
in the direction of the larger surface (recorded below), so the delivered
baseline is 576/952.

### Delivered surface

1. **Caret movement policy (6 functions, 1 enum: 3 values).**
   `pt_caret_movement` is `inherit=0`/`visual=1`/`logical=2`; app, window and
   widget setters/getters resolve widget → nearest explicit ancestor → window
   → app → visual. Visual remains the default, so 2.0 behavior and the LTR
   path are unchanged. Logical mode switches Left/Right to UTF-8 character
   boundaries and Home/End to logical line ends; vertical movement, Shift
   selection, Backspace/Delete and the IME caret rectangle keep their 2.0
   rules. The window level was added in review (see the adjudications below).
2. **Locale helper (3 functions).** `pt_locale_ui_direction` is a bounded,
   ICU-free BCP-47/POSIX language/script lookup; `pt_app_set_locale` copies a
   hint and `pt_app_get_locale` borrows it. Resolution is widget → ancestor →
   window → app explicit → app locale hint → LTR, so an explicit setter wins in
   either call order. No environment is read; no locale means LTR.
3. **Bidi control inventory and sanitization (2 functions, 2 enums: 12
   values, 1 struct).** A new `include/pt/bidi.h` reports the nine explicit
   directional controls with byte offsets, lengths and kinds
   (`pt_bidi_control_inventory`) and returns a caller-owned copy with them
   stripped or replaced by U+FFFD (`pt_bidi_sanitize`). Opt-in: no widget,
   clipboard, drag or accessibility path sanitizes automatically. The public
   inventory is narrower than the internal `pt_bidi_is_removed`, which keeps
   its BN-plus-X9 paint meaning.
4. **Logical start/end padding (1 function).** `pt_widget_set_padding_logical`
   stores the logical edges and a layout/measure prepass re-resolves them from
   the resolved UI direction on every layout, including an inherited direction
   change. Physical `pt_widget_set_padding` remains and the last call wins;
   margins stay physical; the logical flag defaults false.

### Waves

| Wave | Scope | Evidence | Exit gate |
| --- | --- | --- | --- |
| W1 | Caret policy and locale helper; app/window/widget storage, resolver, editing branches, three bindings, tests, baseline | `cd8e63b` (setters + fallback), `825a227` (bindings) | `make check`, `make check-bindings`, 571/934 at the code commit, LTR snapshots unchanged |
| W2 | Bidi inventory/sanitizer, new `bidi.h`, byte scanner, wrappers, tests, baseline | `423d9da` (API), `ce6730a` (bindings), `e99b1a6` (baseline + reference), `56e3904` (tests) | conformance unchanged (770,241 + 91,707), ASan clean, 573/949 cumulative |
| W3 | Logical padding storage, layout/measure prepass, wrappers, geometry tests, baseline, RTL golden | `4af041c` (setter + prepass), `34108f1` (layout, flex/grid, opt-out, goldens) | `make perf-check`, 17 LTR snapshots and 11/11 cross-version gate unchanged, 574/950 before review, 576/952 after review |
| W4 | `rtl.md` + Chinese pair, README pair, quickstart/architecture/bindings/comparison/accessibility pairs, four-language examples, generated reference check, example targets | `43a1f7f` (bilingual README and RTL guide), `279291d` (four-language examples and `run-*` targets) | `check-bilingual-docs.py` green, `gen-api-reference.py --check` clean, four-language smoke |
| W5 | Hardening, version 2.1.0, release record finalisation, final gate | `607b847` (version bump and follow-ups), `c80e21a` (final release record) | `werror=1`, full `tools/ci.sh` exit 0, `release-check.sh --full` green, staged install, reproducible tarball, four live suites, 564 → 576 ABI diff; tag `2.1.0` by the owner |

Waves were sequential so `bindings/`, `widget.c` and `internal.h` never
conflicted. File ownership and the detailed test plan are in
[m23-design.md](m23-design.md#waves).

### Review adjudications (open questions closed)

The four open questions in the design were closed during W1-W3 review. Each is
recorded in [m23-design.md](m23-design.md) with its outcome:

1. **Window-level caret movement: added.** Symmetry with `pt_ui_direction` and
   a per-window policy without touching a root widget won; the caret surface is
   six functions and the baseline is 576/952, matching the design's own
   projection for that choice.
2. **Neutralize replacement: U+FFFD.** A visible U+FFFD is the security
   argument: a reader must see that something was removed. A zero-width
   replacement would preserve columns and hide the removal.
3. **Locale table ownership: a small CLDR-derived table in the library.** No
   ICU, no locale database, no `setlocale` and no global state; a missing entry
   resolves to LTR and the table can grow in a future minor without an ABI
   change.
4. **Logical padding and the table/tree opt-out containers: covered by a
   regression test.** W3 adds the table/tree logical-padding and opt-out cases
   to the layout and flex/grid suites.

The `pt_bidi_control_span` fields are `size_t` offsets and lengths plus the
kind and the code point, as the public headers do elsewhere; this was settled
in W2 when the struct landed.

### Budget

DuMate is exhausted and is not a dependency. Allocation used: W1 30% (main free
channel), W2 15% (main, bounded scanner review), W3 20% (main, layout review),
W4 20% (main English guide and examples, Chinese pair in the same write), W5
15% (main plus an independent gate reproduction on a second provider). A wave
that cannot pass its exit gate is split rather than merged partially, and no
2.0 gate is lowered to make room for a 2.1 feature.

### Completion criteria

Achieved on `m23/w4-docs` (W1-W4):

- `api/pt.api` reads **576 functions / 952 entries**, and the generated
  reference covers all 576 (`tools/gen-api-reference.py --check`).
- **576/576** binding parity in C++, Python and Java with an empty allowlist.
- Defaults preserve 2.0: visual caret, no locale, physical padding, no
  automatic sanitization; the 2.0 snapshots are byte-identical and
  `tools/ltr-compat.sh` stays 11/11 `SAME`; conformance stays 770,241 + 91,707.
- Locale mapping, logical caret movement, control inventory/sanitize and
  logical padding each have headless tests, including LTR/RTL pairs and
  invalid/NULL inputs.
- `rtl.md` and `zh-CN/rtl.md` document the four features, mark them delivered
  in 2.1 and drop the three matching limitations; `check-bilingual-docs.py`
  and `gen-api-reference.py --check` pass.
- The README pair reports 2.1.0 and 576 with a "What is new in 2.1.0" section
  and the 2.0.0 history intact; the four-language 2.1 examples build with
  `werror=1`, exit 0 headless and 124 in the nested smoke.

Achieved on `m23/w5-gate` (W5, all of the above):

- `VERSION` and the `pt_version_*` macros read **2.1.0**
  (`pt_version_major/minor/patch` = 2/1/0), `tools/release-check.sh` reports
  every automated gate green on the frozen tree, and `ABI_MAJOR` stays 1 with
  SONAME `libpt.so.1`.
- `tools/ci.sh` exits 0 on `607b847` (`CI verification passed for Pt 2.1.0`);
  `make check` passes 41 C executables at 586,441 checks, C++ 863, Python 68
  and Java 701 with 0 failures; `make perf-check` holds every ceiling including
  the bidi profiles; ASan+UBSan report nothing with `libasan.so.8` and
  `libubsan.so.1` linked; staged install reports pkg-config 2.1.0, the baseline
  ABI, the four consumers and a clean uninstall; two `make dist` runs at
  `SOURCE_DATE_EPOCH=1767225600` reproduce sha256
  `9580b56fd92f3fac2fe2c7346b3fdf590663e340ace211d7a9e0a2ccc0370850`, and the
  extracted tree rebuilds and passes; the four live suites pass.
- Every `pending final gate run` marker in [release.md](release.md) is
  replaced with a measured figure; the owner creates and pushes the annotated
  `2.1.0` tag from this tag-ready tree.
- The three W5 follow-ups are closed: the version bump, the locale-unset
  excess repaint (only windows that inherit the app direction repaint) and the
  missing trailing newline in `src/bidi.c`.

**M23 is closed.** The tree on `m23/w5-gate` is tag-ready; tag creation and
the landing on `main` are the owner's step, exactly as for 2.0.

## M24 - 2.2.0: RTL editing behavior polish (delivered — M24 closed)

**Status: delivered on `m24/w3-gate` (2026-11-24), tag-ready for 2.2.0.**
Wave evidence: W1 `5dc3f57` (multi-line selection paints its visual edge) and
`7b47558` (Shift crosses a soft wrap one visual row in RTL), W2 `dd18db0`
(horizontal axis routed to table columns) and `56bbd95` (headless routing
tests), W3 `m24/w3-gate`
(review observations closed: dead `axis_h_velocity` state removed, axis-kind
aware overlay consumption, `-j2` gate builds; docs rewrite; version 2.2.0;
`tools/ci.sh` exit 0; ABI 576 → 576 / 0 / 0 / 0; reproducible tarball). All
completion criteria below are met: LTR 11/11 `SAME` against 1.4.0 and 2.1.0,
`api/pt.api` still 576/952, binding parity 576/576 in three languages,
`ABI_MAJOR` 1 with SONAME `libpt.so.1`, `make check` green with additive tests
only, the three `rtl.md` bullets rewritten (the wheel one narrowed to device
dependence), `VERSION` and `pt_version_*` at 2.2.0 and
`tools/release-check.sh` green.

The scope and wave contract below is the historical design record.

Target release **2.2.0**, `ABI_MAJOR` stays 1 (SONAME `libpt.so.1`). Baseline
on the landed 2.1.0 tree (`a3aea38`): `api/pt.api` reads **576 functions /
952 entries**. The behavior design, code evidence and risk register live in
[m24-design.md](m24-design.md); this section is the scope and wave contract.

### Residual limitations reviewed

The three editing bullets in [rtl.md](rtl.md#known-limitations) were read
against `src/widget.c`, `src/text_layout.c`, `src/wayland/platform.c` and
`src/table.c`:

| Limitation | Verdict | Reason |
| --- | --- | --- |
| Shift+arrow across a soft wrap in RTL | **feasible**, medium risk | `text_area_horizontal`'s mapped Shift branch only calls `visual_caret_step`; the visual-line-edge test and the `caret_goal_x` pattern from `text_area_vertical` already exist. A cross-line step at a soft-wrap visual edge is a local change in one function. |
| Multi-line mapped selection collapse | **feasible**, low risk, no paint change | The collapse already has a mapped single-line branch; `visual_line_edge` on the start and end line maps to the visual edges, and the painted range is already logical. |
| Table horizontal wheel | **feasible**, premise corrected | Wayland's `wl_pointer.axis` has `horizontal_scroll` (value 1) since v1 (`wayland.xml` enum, matching `WL_POINTER_AXIS_HORIZONTAL_SCROLL`). Pt drops it at `platform.c:435`, so this is a routing gap, not a protocol gap. Whether a device emits it depends on the hardware; the limitation becomes device-dependent, not "unsupported". |
| `pt_text_span` frozen / Arabic-Hebrew tofu / sanitization opt-in / locale table | out of scope | ABI and product decisions, not editing behavior. Recorded in [m24-design.md](m24-design.md#out-of-scope-recorded-not-planned). |

### Waves

| Wave | Scope | Edit domain | New API |
| --- | --- | --- | --- |
| W1 | Multi-line mapped selection collapses to the visual edge; Shift+arrow crosses a soft wrap at a visual line edge (affinity pinned to the landing line). | `src/widget.c`, `tests/test_text_area.c` | 0 (static helper only) |
| W2 | Route `WL_POINTER_AXIS_HORIZONTAL_SCROLL` from the platform through an internal axis-kind path to `pt_table_scroll_horizontal`, with RTL direction mapping and overlay precedence; fill the existing `pt_pointer_event.delta_x` on horizontal axis events; narrow the limitation wording. | `src/wayland/platform.c`, `src/window.c`, `src/internal.h`, `src/overlay.c`, `src/widget.c`, `tests/test_data_view.c`, `tests/test_window.c` | 0 (public 4-arg `pt_window_input_axis` unchanged) |
| W3 | `rtl.md` + `zh-CN/rtl.md` limitation rewrite, README pair "What is new in 2.2.0", `VERSION` 2.2.0, gate run. | docs, `VERSION`, `docs/release.md` | 0 |

### Completion criteria

- Default LTR text areas, logical caret movement and vertical-only wheel behavior
  are byte-identical to 2.1; `tools/ltr-compat.sh` stays 11/11 `SAME` and the
  2.1 snapshots do not move.
- `api/pt.api` still reads **576 / 952** and binding parity stays 576/576 in
  C++, Python and Java; `ABI_MAJOR` stays 1 with SONAME `libpt.so.1`.
- `make check` green with additive tests only; no painting golden needs an
  update.
- The three `rtl.md` bullets describe the delivered behavior (the wheel one is
  narrowed to device dependence); `check-bilingual-docs.py` and
  `gen-api-reference.py --check` pass.
- `VERSION` and the `pt_version_*` macros read 2.2.0 and
  `tools/release-check.sh` passes on the frozen tree.

### Budget

~250 credits on a single main workstream: scaffold/research ~60 (this wave and
this document), W1 ~80, W2 ~70, W3 ~40. If W2 cannot be verified live because the
bench compositor/device never emits a horizontal axis, the routing still lands
and W2's evidence is marked headless-only; the wave is not dropped.

## M25 - 2.3.0: inline image runs (proposed)

**Status: proposed, design only (2026-11-27).** This section and
[m25-design.md](m25-design.md) are the scaffold for the next implementation
round; no source, header, binding, test or generated baseline file is changed on
this branch. The landed tree is `7c476bc` (2.2.0): `api/pt.api` reads **576
functions / 952 entries**, `ABI_MAJOR = 1`, SONAME `libpt.so.1`.

M25 delivers the first item of the post-2.0 structural horizon: inline image
runs in styled text and the text area. The placeholder is the U+FFFC object
replacement character in the widget's UTF-8 text; image descriptors live in a
separate, sorted side table keyed by the placeholder's byte offset. This keeps
text editing, byte offsets, bidi and accessibility character offsets unchanged,
and leaves the frozen `pt_text_span` layout alone.

### Scope

- Inline image runs on labels and text areas: U+FFFC placeholder, image handle,
  logical width/height, baseline or middle alignment, alt text.
- Measurement, wrapping and line metrics include the image box; a wide image is
  never split and a tall image raises the composed line metrics (the existing
  styled-run rule).
- Atomic caret, selection and deletion over the placeholder; selection paints
  one rectangle through the existing visual map.
- Copying a selection writes alt text in place of U+FFFC; an empty alt keeps
  U+FFFC.
- AT-SPI exposes a virtual named image child for each run; the Text interface
  keeps raw U+FFFC and unchanged character offsets.
- Reuse `pt_icon_image` and the bounded raster cache; no new decoder or cache.
- Additive public surface: 5 functions, 1 enum, 1 struct. Final estimate **581 /
  960** (W1 reaches 580 / 959, W3 adds the editing entry point). `pt_text_span`
  is untouched; `ABI_MAJOR` and SONAME do not move.

Out of scope: entries, per-line metrics, image tint/animation, rich clipboard
MIME types, and any change to the frozen span struct. The full scope, feasibility
evidence, API and risks are in [m25-design.md](m25-design.md).

### Feasibility summary

| Area | Verdict | Reason |
| --- | --- | --- |
| U+FFFC data model | **feasible**, medium risk | A parallel `pt_text_image` side table mirrors the existing span shift/clamp rules; the placeholder is one UTF-8 codepoint, so it is atomic by construction. Extending `pt_text_span` would break the 2.0 ABI. |
| Baseline and measurement | **feasible**, medium risk | `pt_text_runs_build` already composes ascent/descent from runs; image extents join that maximum. `pt_text_runs_measure` adds a constant width; the wrapper already steps by codepoints. |
| Painting | **feasible**, low-medium risk | `pt_text_runs_draw_line_base` already walks visual segments with a pen; one fontless image branch reuses `pt_icon_draw_canvas`. Selection already paints per-line map rectangles. |
| Atomic selection and alt copy | **feasible**, medium risk | Interactive movement and visual clusters cannot split a codepoint. Programmatic selection needs a snap helper; copying needs a pure alt-range helper used by `widget.c` and the AT-SPI copy path. There is no `src/clipboard.c`; the platform clipboard API is unchanged. |
| Embedded-object accessibility | **feasible**, medium risk | The AT-SPI object tree is widget-only; add a virtual image object type, ordinal path and named child. No public a11y API is added. |
| Public API and ABI | **feasible**, additive | New enum, new struct and new symbols append to `libpt.so.1`; no existing layout or signature moves. |

### Waves

The text pipeline is the serial hot spot, as in M22 W3. Independent domains run
in parallel after the foundation lands. File ownership is exclusive: no two
active agents touch the same file.

| Wave | Scope | Files owned | Depends on | Runs in parallel with | Owner |
| --- | --- | --- | --- | --- | --- |
| W0 | This proposal and the design record | `docs/ROADMAP.md`, `docs/m25-design.md` | - | - | scaffold |
| W1 | Descriptor model, `src/image_run.c`, refcount/lifecycle, set/get/clear, alt helper, tests, baseline 580/959 | `include/pt/base.h`, `include/pt/widget.h`, `src/internal.h`, `src/image_run.c` (new), `src/widget.c` (destroy/set-text only), `tests/test_text_image.c`, `tests/test_abi.c`, `api/pt.api`, `docs/api-reference.md` | W0 | none (foundation) | DuMate |
| W2 | AT-SPI virtual image child, role/name, structure events, alt-copy path | `src/a11y_bridge.c`, `src/a11y.c`, `tests/test_a11y_bridge.c` | W1 | W3, W4, W5 | Loomy (single agent) |
| W3a | Header freeze for `pt_text_area_insert_image`; no behavior | `include/pt/widget.h` | W1 | W4, W5 (after commit) | DuMate |
| W3b | Image runs, measure/wrap, fontless map clusters | `src/rich_text.c`, `src/font.c`, `src/text_layout.c`, `tests/test_rich_text.c`, `tests/test_font.c` | W1, W3a | W2, W4, W5 | DuMate (serial) |
| W3c | Paint, history, atomic editing/selection, alt copy, goldens, baseline 581/960 | `src/paint.c`, `src/widget.c`, `tests/test_text_area.c`, `tests/test_editing.c`, `tests/test_visuals.c`, `api/pt.api`, `docs/api-reference.md` | W3b | W4, W5 | DuMate (serial) |
| W4a | C++ wrappers and tests | `bindings/cpp/pt/pt.hpp`, `bindings/cpp/test_pt.cpp` | W3a | W3b, W3c, W4b, W5 | Q-CN |
| W4b | Python and Java wrappers and tests | `bindings/python/pt.py`, `bindings/python/test_pt.py`, `bindings/java/src/pt/Pt.java`, `bindings/java/test/pt/PtTest.java` | W3a | W3b, W3c, W4a, W5 | WB-CN |
| W5 | Public guides, four-language example, bilingual pairs, README "what is new" | `docs/quickstart.md` + zh, `docs/architecture.md` + zh, `docs/accessibility.md` + zh, `docs/bindings.md` + zh, `README.md` + `README.zh-CN.md`, `examples/`, `Makefile` (example targets only), `docs/m25-design.md` (status) | W3a | W2, W3b, W3c, W4 | Lobster |
| W6 | Merge, full gate, 2.3.0 version/release record, tag-ready | `include/pt/base.h` (version macros only), `Makefile` (`VERSION`), `docs/release.md`, `docs/ROADMAP.md`, `docs/m25-design.md` (final) | W1-W5 | none | Lobster + DuMate |

`src/rich_text.c`, `src/text_layout.c`, `src/font.c`, `src/paint.c`, `src/widget.c`,
`src/internal.h`, `include/pt/base.h` and `include/pt/widget.h` are the serial
domain: W1 and W3 are sequential and the same owner. `src/image_run.c`,
`src/a11y_bridge.c`, each binding language file and the docs/examples tree are
independent and can run concurrently. `api/pt.api` and
`docs/api-reference.md` are updated only by W1 and W3c, after implementation.

### Completion criteria

- `api/pt.api` reads 581 / 960 (580 / 959 if the insert helper is deferred);
  binding parity 581/581 in C++, Python and Java; `ABI_MAJOR` 1 and SONAME
  `libpt.so.1`; no existing enum value or struct layout moves.
- Text without images is byte-identical to 2.2.0: `tools/ltr-compat.sh` 11/11
  `SAME`, existing snapshots unchanged, `make check` green with additive tests
  only.
- Labels and text areas render, wrap, measure, baseline-align and select inline
  images atomically; caret movement skips the whole placeholder; the painted
  selection is one rectangle; undo/redo restores text and image together.
- Copying a selection replaces U+FFFC with alt text (or keeps U+FFFC when alt is
  empty); cut removes the placeholder and descriptor.
- AT-SPI exposes one named image child per run, keeps raw U+FFFC and unchanged
  character offsets, and emits a structure event when the set changes.
- `check-bilingual-docs.py`, `gen-api-reference.py --check`,
  `gen-binding-stubs.py --check --strict`, the staged-install consumers and the
  serial sanitizer pass are green; `git status` is clean.

### Budget

~750-850 credits. W0 ~60, W1 ~150, W2 ~120, W3 ~260 (serial hot spot), W4 ~70,
W5 ~70, W6 ~70. DuMate carries W1 + W3 (~50%); Loomy runs W2 as a single agent
(~15%); Q-CN and WB-CN split the binding files in parallel (~18%); Lobster
takes docs/examples and integration/release (~17%). No wave may lower the 2.0
LTR gate, the 2.2 RTL gate or binding parity to land images.

## Post-1.0 horizons

Candidates are grouped by the horizon the external research (2026-11-22)
recommends, with the reason each is deferred. Sources and evidence grades are in
[compositor-matrix.md](compositor-matrix.md); the engineering rules are in
[standards.md](standards.md).

### 1.2 — common but missing (delivered in M19; tagged 1.2.0)

- Application-owned popups via the existing `xdg_popup` backend (content widget,
  anchor, dismiss, reposition). This is the prerequisite for searchable select
  and pickers. Subsurfaces are a different need (embedding another surface) and
  stay at 2.0.
- Clipboard images (`image/png` read/write, bounded decode) and clipboard
  `text/uri-list` writes (CRLF, percent-encoded `file://`). The drag-source
  URI-list already exists; this is the clipboard path.
- Date/number/colour pickers and a searchable select beyond the filterable
  combobox, built on entry + popup + grid, with full keyboard paths and
  accessibility assertions.
- Icon slots (`pt_icon`: PNG, hicolor lookup, per-size/scale cache; SVG
  deferred) and best-effort `xdg-toplevel-icon-v1` where the compositor
  exposes it.
- Per-run font fallback chains (HarfBuzz script segmentation + Fontconfig
  charset matching), the prerequisite for readable CJK/Arabic/Devanagari mixing
  and for RTL.
- Column-layout persistence helpers (versioned serialize/deserialize blob, the
  application stores it) and generated binding stubs from an API manifest.

### 2.0 — structural

- RTL/bidi paragraph layout per [rtl.md](rtl.md) (delivered in M22 as 2.0.0,
  tagged): a UBA implementation
  (SheenBidi, Apache-2.0, to avoid FriBidi's LGPL static-link constraint),
  bidi-run shaping, logical offsets through the public API and AT-SPI, mirrored
  layout/chrome, Unicode BidiTest conformance. Changes layout and hit-testing,
  so it could not land in a minor.
- Inline image runs (U+FFFC object replacement, baseline alignment, atomic
  selection, alt-text copy, embedded-object accessibility).
- Optional Vulkan/EGL renderer behind the same canvas contract, software by
  default, pixel-parity tested, automatic fallback; only if 4K/complex-animation
  measurements justify it. The CPU `pt_paint_*` API and the GPU backend share
  geometry/colour/text so applications do not rewrite to switch.
- A standalone visual theme editor on the token table, exporting C/JSON with
  built-in WCAG contrast checks; it stays out of the core ABI.
- Additional protocols (`xdg-activation`, `color-management` for HDR,
  `xdg-foreign`) only with a real need and matrix rows plus fallback tests.
- X11 or other platform backends only if maintainable without compromising the
  Wayland-first core.

