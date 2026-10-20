# Pt

English | [简体中文](README.zh-CN.md)

**Pt** is a small, fast, Wayland-native GUI framework written in modern C23.
Applications can be written in C, C++, Python or Java against one flat C ABI.
The name is the chemical symbol for platinum: small, dense and unreactive.

Status: **1.0.0**. The frozen release: a combobox with select-only and
filterable modes, native popup surfaces and keyboard and type-ahead navigation;
one multi-row selection model across lists, tables and trees — range and toggle
primitives, shift-click, Ctrl-click, Shift+arrow, Ctrl+A and a geometric rubber
band in every view; the AT-SPI roles and states a screen reader needs to drive
them; per-header ownership, lifetime, error and threading contracts backed by an
append-only ABI test; and `tools/release-check.sh`, which turns the 1.0 readiness
checklist into a report — on a 412-function C ABI wrapped in C++/Python/Java with
binding parity enforced. The API is tracked by a reviewed baseline, a generated
function reference and a compatibility policy; see
[docs/api-reference.md](docs/api-reference.md),
[docs/api-policy.md](docs/api-policy.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

## Why Pt

- Wayland first: `xdg-shell`, client-side decorations, `fractional-scale-v1`
  and `text-input-v3`, with no X11 or GL runtime baggage.
- A retained-mode widget tree and a flat, opaque C ABI that is pleasant to bind.
- Crisp FreeType + HarfBuzz text, including CJK shaping and fractional scaling.
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
make bench                 # headless layout, full/partial paint and startup benchmarks
make perf-check            # benchmarks plus regression ceilings (4K, large-tree scroll)
make install-check         # staged install, SONAME/ABI and four-language consumers
tools/ci.sh                # CI-equivalent local gate, including sanitizers and tarball
tools/release-check.sh     # readiness report for the frozen release
```

`make check` runs headless and needs no compositor. It includes the C test
suites, the C++/Python/Java binding suites, the exported-symbol and API
baseline audit, the hard 412/412 binding-parity gate, the per-round 0.6–0.10
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
| C++23 | [counter.cpp](examples/cpp/counter.cpp) | `make run-counter` |
| C++23 | [workspace.cpp](examples/cpp/workspace.cpp) | `make run-cpp-workspace` |
| C++23 | [combobox.cpp](examples/cpp/combobox.cpp) | `make run-cpp-combobox` |
| Python | [counter.py](examples/python/counter.py) | `make run-python` |
| Python | [notes.py](examples/python/notes.py) | `python examples/python/notes.py` |
| Python | [workspace.py](examples/python/workspace.py) | `make run-python-workspace` |
| Python | [combobox.py](examples/python/combobox.py) | `make run-python-combobox` |
| Java 25 | [HelloPt.java](examples/java/HelloPt.java) | `make run-java` |
| Java 25 | [Workspace.java](examples/java/Workspace.java) | `make run-java-workspace` |
| Java 25 | [Combobox.java](examples/java/Combobox.java) | `make run-java-combobox` |

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
- [Right-to-left text plan](docs/rtl.md)
- [Migrating to 1.0](docs/migration-1.0.md)
- [API compatibility policy](docs/api-policy.md)
- [Performance](docs/performance.md)
- [Comparison with other toolkits](docs/comparison.md)
- [Roadmap](docs/ROADMAP.md)
- [Release process](docs/release.md)

The quickstart, architecture, bindings, theming, accessibility, RTL plan,
migration and comparison guides also have
[Simplified Chinese translations](README.zh-CN.md); the generated API reference,
release records, roadmap, performance notes and policy pages remain
English-only.

## Limitations in 1.0

- Wayland only; there is no X11, Windows or macOS backend.
- The 1.0 ABI is frozen: additions after 1.0 are append-only and removals
  require a major release and a SONAME bump. The checked-in baseline, symbol
  audit and `tests/test_abi.c` make any drift explicit. Layout, painting and
  the event loop run on the calling thread only.
- The renderer is CPU-only. Dense full-window animation is comfortable on
  desktop-sized windows but is not a 4K/120 Hz GPU path; an accelerated backend
  is a post-1.0 candidate.
- Menus, tooltips and select-only combobox popups use native `xdg_popup`
  surfaces when the backend supports them; dialogs, toasts and filterable
  combobox popups remain composited inside the window because they are modal,
  stacked or need the editor to keep focus. Native popups still need a
  configured xdg-shell window, so headless runs use the in-window fallback.
- The AT-SPI2 bridge is opt-in (`pt_app_enable_a11y`), requires a build with
  `dbus-1`, and exposes the widget tree rather than arbitrary painted
  content. Screen readers see Pt widgets but not custom drawing without
  accessibility metadata.
- Drag sources and structured drop offers need the same implicit pointer grab
  as any Wayland drag, so `pt_window_begin_drag` only succeeds while a press is
  active, and a headless run has no drag compositor to talk to. Streamed reads
  deliver bytes; decoding JSON or a URI list is the application's job. Touch
  gestures use two contacts; additional contacts are ignored. Stylus support
  depends on `tablet-v2` from the compositor.
- Styled runs cover weight, slant, color, underline, per-run font family and
  per-run size over byte ranges of labels and text areas. There is no bidi
  paragraph layout, no inline images and no per-run stretch or optical
  alignment, and the text area still edits plain text with uniform paragraph
  geometry. The post-1.0 design, compatibility rules and test obligations are
  written down in the [RTL plan](docs/rtl.md).
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

## License

Apache License 2.0. Vendored Wayland protocol XML retains its MIT headers; see
[NOTICE](NOTICE) and [THIRD_PARTY.md](THIRD_PARTY.md).
