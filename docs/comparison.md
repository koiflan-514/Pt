# Pt and other GUI toolkits

English | [简体中文](zh-CN/comparison.md)

This is a qualitative comparison of Pt 1.0 with GTK 4, Qt Quick, Slint, egui,
LVGL and Nuklear. It is written from the perspective of a C application on
Linux/Wayland that wants native-looking windows, a small binding surface and
ownership in C, C++, Python or Java.

No cross-toolkit benchmark is claimed here. Different toolkits make different
trade-offs and are usually measured on different workloads; the numbers in
[performance.md](performance.md) describe Pt only.

## At a glance

| | Pt 1.0 | GTK 4 | Qt Quick | Slint | egui | LVGL | Nuklear |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Core language | C23 | C (GObject) | C++ / QML | Rust | Rust | C | C (single header) |
| UI model | retained widgets | retained widgets | retained declarative scene | retained declarative UI | immediate mode | retained widgets | immediate mode |
| Linux/Wayland | native, only target | native | native | native | native window via winit | via board/Linux drivers | via GLFW/SDL/other |
| Renderer | software `wl_shm`, damage-tracked | GPU (GSK), software fallbacks | GPU RHI (Vulkan/GL/Metal/D3D) | GPU or software | GPU (wgpu/glow) | board framebuffer/GPU drivers | renderer supplied by user |
| Windows/macOS | no | experimental/limited | yes | yes (desktop) | yes (native/web) | embedded | embedded/desktop via backend |
| CSS/theming | 38 semantic tokens + 10 metrics, per-window overrides | CSS | QML styling | `.slint` styling | programmatic style | styles | programmatic |
| Controls | entries, text area, combobox (select-only and filterable), checkboxes, radios, switch, slider, progress, chips, skeleton, icon buttons | broad | broad | broad | moderate | basic | minimal |
| Text editing | single- and multi-line, selection, clipboard, undo, styled runs with per-run family/size | full | full | good | basic | basic | minimal |
| Data views | virtualized list, table and tree with stable keys, multi-column sort, hidden columns, in-cell editing, rename, reorder, range/toggle selection and a rubber band in all three | models/views | models/views | repeated components | immediate loops | basic | none |
| Accessibility | semantic model + events, opt-in AT-SPI2 bridge with Table/TableCell/Selection, combo-box and list-box roles, expandable/selectable/multi-selectable states | mature | mature | in progress | limited | no | no |
| Menus/dialogs | native popup menus, tooltips and select-only combobox lists, modal dialogs, toasts | yes | yes | minimal | no | no | no |
| Input | pointer, touch, stylus, async multi-MIME DnD with URIs, swipe/pinch/hold, momentum | broad | broad | good | basic | basic | minimal |
| Bindings | C/C++/Python/Java | GI (many languages) | C++, QML, Python | Rust/C++/Python/JS | Rust-first | C, third-party | C, third-party |
| License | Apache-2.0 | LGPL-2.1+ | LGPLv3/GPL/commercial | GPLv3/commercial | MIT/Apache-2.0 | MIT | PD/MIT |

The table is a map, not a scoreboard. For example, LVGL is outstanding at its
job (MCUs with no compositor), and that job is not Pt's.

## Where Pt differs

### 1. The C ABI is the product

Pt's stable unit is a flat C ABI: opaque handles, value-shaped enums, packed
color integers and C function pointers with `void *user_data`. C++, Python and
Java do not wrap a C++ object model through a bridge; they call the same
symbols directly. This makes the binding layers small (the whole Python binding
is one file, Java is one class) and keeps lifetime rules identical across
languages.

GTK also has a C core and introspection, but its bindings inherit GObject
conventions and a much larger API surface. Qt's natural home is C++; Python is
well supported, but Java is not an official first-class target. Nuklear is
also a C ABI, but it is immediate-mode and renderer-agnostic.

### 2. Wayland is the platform, not one backend

Pt implements `xdg-shell` (including `xdg_popup`), `xdg-decoration`,
`fractional-scale-v1`, `viewporter`, `text-input-v3`, `wl_data_device`,
`pointer-gestures-v1`, `tablet-v2`, client-side title bars and `wl_shm`
directly. There is no X11 fallback, no Win32 layer and no GL context to
configure. Client-side
decorations are drawn by Pt, which matters on tiling compositors such as niri
and wlroots setups.

Qt and GTK support many platforms and therefore carry a platform abstraction
layer. That is the right choice for them; Pt deliberately trades portability
for a small, predictable Linux desktop stack.

### 3. Software rendering with on-demand frames

Pt's renderer is predicated on GUI work being mostly flat surfaces, borders,
rounded cards and text. It draws into a shared-memory buffer and commits only
when state changes. Damage is accumulated into a small index of coalesced
tiles, each repainted under its own clip on a persistent backing canvas and
submitted as `wl_surface.damage_buffer`, so a localized update costs roughly 7%
of a full dense-window repaint and walks a quarter of the visible tree. Layout
invalidation records the dirty subtree and relayouts from its pinned ancestor
when that is safe. Long data
sets use virtualized lists, tables and trees: only viewport rows plus one
overscan row are materialized, so layout, painting and row memory scale with
the viewport rather than the model. There is no
GPU driver dependency, no GL context loss, no shader compilation on first frame,
and no texture atlas to manage. The measured idle loop uses under 0.3% of a
core, a dense 1280x800 full repaint runs at about 55-70 fps, and a dense 4K
full repaint takes about 38-48 ms on the reference laptop, while one damaged row at
that size takes 3.3-4.1 ms. The 4K figure is a
damage-driven or static-content target rather than a continuous-animation one.
Idle means idle on a real compositor too, not just in the headless harness: as of
0.10.0 a focused entry no longer answers the input method's acknowledgement with a
fresh state push, which had been redrawing a touched-by-nobody window about 43% of
one core. The 1.0 live guard holds a focused-entry 5 s slice under 1.5 s of CPU
and retries or skips when the desktop injects input.

GTK 4 and Qt Quick are GPU-first and will beat Pt decisively on large-scale
transforms, blur, video, 3D content and high-resolution continuous animation.
egui is also GPU-first but immediate-mode: it rebuilds and repaints UI every
frame, which is a natural fit for tools and visualizations and a poor fit for
battery-sensitive desktop utilities. LVGL's software renderer is optimized for
much smaller displays and often has no compositor at all.

### 4. A small, opinionated widget set

Pt offers box/row/column, label, button, entry, combobox, text area, toggle,
checkbox, radio, slider, progress, spinner, segmented control, scroll, wrap,
grid, virtualized list, table, tree, tabs, sidebar, icons, icon buttons, chips,
skeleton, divider, card and spacer, plus native popup menus, tooltips and
select-only combobox lists, modal dialogs and toasts. The
design language is token-driven: tonal control surfaces, soft accent buttons,
animated check marks and switches, focus rings and unified dark/light palettes.
Entries and text areas support selection, cut/copy/paste, word/select-all
gestures, undo/redo and drag-out sources; drop targets that introspect a
multi-MIME offer and stream the accepted type asynchronously, URI-list file
drags,
drag images, swipe/pinch/hold gestures, stylus input, kinetic scrolling and
public easing with reduced-motion awareness complete the interaction layer.
That is enough for utility windows, settings panels, counters, dashboards, note
tools, file browsers and small data tools; it is not a document editor or
spreadsheet toolkit. GTK and Qt have years of widgets, dialogs, full model/view
stacks, menus and platform integration. Slint's component set is also growing
quickly and has a declarative language; Pt uses plain imperative construction
with fluent bindings.

The 1.0 limitations are listed honestly in the
[README](../README.md#limitations-in-10): dialogs and toasts stay inside the
window, AT-SPI exposure is opt-in, streamed drop data arrives as bytes the
application decodes and a source drag needs an active pointer press, touch
pinch/rotate synthesizes from two contacts, table columns keep application-set
widths and can be hidden but the application still owns row order, multi-column
sorting reports up to four priorities without reordering the model, tree nodes
hold plain text, rows are owned by Pt and text areas edit plain text with styled
runs but no bidi layout. Stylus
tool, eraser and barrel buttons are delivered as input data; drawing and
gesture semantics remain application-defined. Projects that need more than
that today should use GTK or Qt.

### 5. Licensing and dependency shape

Pt is Apache-2.0, including the bindings and examples. GTK is LGPL-2.1+; Qt is
LGPLv3/GPLv3 or commercial depending on module and use; Slint is GPLv3 or
commercial. Apache-2.0 is friendly to static linking in closed products and
carries an explicit patent grant. Pt still depends on system font and Wayland
libraries, so the final binary is not dependency-free, but none of them are a
GUI toolkit or a language runtime.

egui and Nuklear are permissively licensed too; if Rust or immediate mode is a
good fit, they are excellent choices. LVGL's MIT license is ideal for embedded
products but its architecture targets bare-metal or RTOS displays rather than
Wayland desktop sessions.

### 6. Bindings without a runtime bridge

- **GTK:** GObject Introspection generates bindings for many languages; the
  runtime and type system are GObject.
- **Qt:** C++ plus QML; PySide/PyQt for Python. Java is not first party.
- **Slint:** Rust core with generated C++/Python/JS APIs from the `.slint` DSL.
- **egui:** Rust-first; non-Rust use is community-driven.
- **LVGL/Nuklear:** C APIs with community bindings.

Pt's Python and Java layers have no generated glue, no JNI and no GObject
runtime. They are small enough to audit and debug, and since 0.7 both wrap every
public C function — 412 at 1.0 — as does the C++ header; see
[bindings.md](bindings.md#binding-coverage).

## Where each toolkit is the better choice

- **Choose GTK 4** for a full GNOME-integrated application, accessibility,
  platform dialogs, broad widgets and a large contributor ecosystem.
- **Choose Qt Quick** for GPU-heavy, animated, cross-platform applications, or
  when QML and Qt's commercial support fit the team.
- **Choose Slint** for a declarative Rust/C++ UI with a modern design language
  and a GPU renderer on desktop and embedded targets.
- **Choose egui** for developer tools, debuggers, visualizers and Rust projects
  where immediate-mode iteration matters more than native platform integration.
- **Choose LVGL** for microcontrollers and embedded displays with tight RAM and
  no desktop compositor.
- **Choose Nuklear** for a tiny immediate-mode UI with a renderer you already
  control, such as a game engine.
- **Choose Pt** for a small Linux/Wayland-native utility or tool written in C,
  C++, Python or Java, where Apache-2.0, a flat ABI, a consistent client-side
  title bar, crisp text and a low idle cost matter more than a huge widget
  catalog or GPU effects.

## Pt's place in 1.0

Pt is intentionally narrower than the established toolkits, but 1.0 freezes a
credible workspace shell: virtualized tables and trees with stable-keyed
selection, batch invalidation, multi-column sorting and hidden columns join
tabs, sidebars, vector icons and multiline text areas, and in 0.9 a table cell
became editable, rows took a selectable range, columns could be dragged into a
new order and a tree label renames in place. In 0.10 the same multi-row model
reaches lists and trees — range and toggle primitives, shift-click, Ctrl-click,
Shift+arrow and a rubber band that starts on a row or in empty viewport space
and selects what it intersects in every view —
and a combobox joins the entries, in a dropdown mode and a mode that filters its
own list as the user types. Theming is no longer a global
switch: 38 semantic color roles and 10 geometric metrics resolve through a
per-window override table, so an application can restyle one surface the way
GTK and Slint restyle one node, and chips, skeletons, busy and invalid controls
cover the states a settings pane or data view actually shows. Drop targets can
introspect a multi-MIME offer and stream the accepted type asynchronously while
sources offer several types at once and learn which action the destination
performed; labels and text areas take styled runs with per-run family and size;
tables and trees answer real AT-SPI `Table`, `TableCell` and `Selection`
interfaces. On the renderer side a coalesced damage index with per-tile clipped
repaint, subtree-scoped layout invalidation, a squared-distance
rounded-rectangle path and a hashed font face table replaced the four costs 0.7
documented as its ceiling, the guard suite measures a marquee drag, a popup
surface, a 4K damaged frame and a 100,000-row table seek, and a deterministic
snapshot harness pins the painted result across theme round trips. The 1.0
freeze removes the two deprecated drag helpers, fixes direct typing while
text-input-v3 is enabled, and extends the accessibility bridge to read-only
labels and text areas without leaking password characters. It remains a working
framework with a tested
renderer, Wayland integration, four language surfaces, runnable examples and
reproducible performance data. Its main risks are maturity and scope, not
architecture: the C ABI is small enough to evolve under the compatibility
policy — 0.9 ran the first real deprecation cycle end to end, 1.0 records the
freeze in the policy and turns the checklist into
`tools/release-check.sh` — and the
renderer, layout engine, data views and bindings are separated behind
interfaces that leave room for bidi paragraph layout, richer text models and
eventually a GPU backend.
