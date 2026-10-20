# Architecture

English | [简体中文](zh-CN/architecture.md)

## Goals and non-goals

Pt is a Wayland-native GUI framework for applications whose logic may be
written in C, C++, Python or Java. It targets Linux desktops and prioritizes a
small dependency surface, predictable memory ownership, crisp text and a
polished default look.

It is not a general-purpose 3D toolkit, a compositor, a web runtime, or a
cross-platform abstraction over X11/Win32/macOS. Linux/Wayland is the only
backing platform in the 0.x series.

## Layering

```
application code (C / C++ / Python / Java)
                  |
             public C ABI
                  |
  widgets -> layout -> damage -> paint -> backing canvas -> wl_shm buffer
   |  |                 |          |
   |  data views /      |      text shape cache
   |  table & tree       overlay stack
   |  text area / icons  |
   |                    |
  input            menus / dialogs / toasts / tooltips
   |
  accessibility model and event stream
   |
 Wayland seat (pointer, keyboard, touch, tablet-v2), xdg-shell,
 fractional-scale, xdg-decoration, data-device, text-input-v3
```

The public C ABI is flat and opaque: no structs with platform handles are
exposed, callbacks carry `void *user_data`, and colors are packed integers.
This keeps ctypes and the Java Foreign Function & Memory API bindings small.
The library is built with `-fvisibility=hidden`; only functions marked `pt_api`
are exported (412 in 1.0, 414 in 0.10.0). Internal headers
live in `src/` and are not
installed.

## Design decisions

### Retained-mode widgets

A retained tree makes state and lifetimes explicit and lets every language
binding hold an opaque handle. Immediate-mode UI would simplify C drawing but
push more binding logic across the FFI boundary (every frame would cross the
language boundary), which is the wrong trade-off for Python and Java backends.

### Software renderer over wl_shm

`wl_shm` avoids a GL context, EGL vendor coupling and a compositor-dependent
swapchain for the default path. GUI workloads are dominated by rounded cards,
text and flat surfaces; a scanline/coverage renderer handles these well.
Frames are only drawn when state changes or an animation is active, and
Wayland frame callbacks provide pacing. A GPU renderer can be added behind the
same canvas API later; it is not required for the first release.

The canvas uses premultiplied ARGB8888 in memory (0xAARRGGBB on little-endian
systems), matching `WL_SHM_FORMAT_ARGB8888` semantics. Public colors are agreed
as 0xRRGGBBAA and converted at the boundary.

### Text

FreeType provides hinted glyph rasterization, HarfBuzz provides shaping
(ligatures, combining marks, complex scripts, CJK) and Fontconfig resolves
families and fallbacks. Glyph coverage is cached per face/size in a small hash
table. Text is measured and drawn in logical coordinates; physical font sizes
are multiplied by the window scale so fractional scaling stays sharp.

Drawing text directly from coverage avoids an atlas texture and keeps the
renderer independent of GPU APIs. Large text-heavy documents may benefit from
an atlas later; the cache boundary is internal.

Measured and shaped runs are cached per font. Keys combine an FNV-1a hash with
a full byte comparison, so collisions cannot produce wrong glyphs. Each font
keeps a 256-entry LRU capped at 256 KiB of retained HarfBuzz buffers and 8 KiB
per text; repeated labels, scroll frames and partial repaints reuse the shaped
run instead of re-running HarfBuzz. The cache is internal and has no public
retention contract.

Fontconfig initialization is the dominant cold-start cost. `ensure_library`
calls `FcInit` lazily on the first text operation, then caches one `pt_font`
per family/weight/pixel-size triple in a process-wide list. FreeType faces and
HarfBuzz fonts are kept for the life of the process. This is a deliberate
latency/memory trade-off: the first frame pays for font discovery and glyph
rasterization, later frames reuse them. See
[performance.md](performance.md) for measured RSS/PSS.

### Multiline editing

The text area reuses the entry's editing state: a UTF-8 byte caret, an
independent selection anchor, 32-snapshot undo and the same clipboard and IME
paths. Layout greedily breaks logical lines at the widget width in
`text_layout.c`, producing visual lines with byte ranges and a uniform line
height; wrapping never splits a UTF-8 sequence. Paint iterates only the visual
lines intersecting the internal clip and maps selection byte offsets back to
line rectangles.

The caret remains a byte offset. Vertical movement converts the current caret
x-position into a goal that is applied to the visual line above or below, so
moving through short and long wrapped lines does not drift horizontally.
Home/End operate on visual-line bounds; Enter inserts `\n`. Scrolling is
internal to the widget and requested by edits and `scroll_to_caret`, not by
paint: painting never mutates scroll state, which keeps damage passes
idempotent. Grow-to-content ignores a requested height during layout and
reports the wrapped text height instead. Entries remain single-line with
horizontal caret scrolling.

### Scaling

`fractional-scale-v1` preferred scale is interpreted as a numerator with
denominator 120. When available, the buffer is allocated at
`ceil(logical * scale)`, `wl_surface.set_buffer_scale(1)` and
`wp_viewport.set_destination(logical_size)` is used. Without fractional scale,
`wl_surface.preferred_buffer_scale` (compositor v6) or a conservative integer
scale is used instead. Buffers are recreated on scale changes.

### Decorations

Pt negotiates `zxdg_toplevel_decoration_v1`. It requests client-side mode so
the default title bar has a consistent look. If the compositor insists on
server-side decorations, Pt omits its header bar. The client header handles
move, resize, maximize and close through xdg-shell requests. This is important
on tiling compositors such as niri and wlroots-based desktops where server-side
decorations are not always shown.

The 44 px logical header contains a title, minimize/maximize/close controls and
6 px edge/corner resize zones outside the content area. Hover tracking and hit
testing for those zones are recomputed on pointer motion and produce resize
cursors from the Xcursor theme.

### Input, IME and clipboard

Each seat provides a pointer, keyboard and data device. Pointer events enter
the window, hit-test the header, then the widget tree; widgets receive enter,
leave, down, up and drag in window coordinates. Scroll containers consume
wheel axis events up to their bounds. Cursors are loaded from the cursor theme
and set on the pointer surface; the title bar exposes resize cursors for the
eight resize directions.

Open overlays sit between the pointer and the widget tree. A menu, dialog or
tooltip panel receives hit testing first; clicking outside a menu dismisses it,
a modal dialog consumes outside clicks and traps focus, and tooltips are
passive. Keyboard input follows the same priority: overlay focus scopes handle
Tab/Shift+Tab and arrows, Escape dismisses menus and dialogs, and focus is
restored to the previously focused widget afterwards.

A `wl_touch` capability from the seat feeds the same internal pointer path:
touch down/up/motion/cancel become pointer down/up/motion/cancel, and
cancellation releases at an off-widget position so no action fires. Tap, drag
and drag-based scrolling therefore work unchanged. Pt tracks up to ten active
touch ids; a second contact cancels the in-flight pointer interaction and
switches to a synthesized two-contact pinch/rotate stream unless the compositor
already reports pinch gestures for the seat. Each frame derives the centroid,
distance and angle from the tracked contacts, normalizes them into the same
`pt_gesture_event` shape as compositor gestures, and ends the gesture when
fewer than two contacts remain.

`zwp_tablet_manager_v2` tools provide stylus input. Proximity, tip down/up,
motion, pressure, distance, tilt and rotation are collected until the tool's
`frame` event, then delivered as one `pt_stylus_event` in logical window
coordinates. The same events are mirrored into the pointer path, so widgets
receive hover and click semantics without any app code; the stylus callback is
an additive stream for pressure-sensitive tools. Pressure and distance are
normalized to `[0, 1]`, tilt is in degrees and rotation is converted from the
protocol's degrees to radians.

`zwp_pointer_gestures_v1` supplies swipe and pinch events (including rotation),
and hold v3 supplies compositor three-finger holds when available. Pt also
synthesizes a single-finger long press from a pointer press that stays within
an 8 px slop for a configurable delay (500 ms by default); the compositor hold
is independent of that delay. All of this is normalized into one
`pt_gesture_event` stream: swipe translation accumulates in `dx`/`dy`, pinch
`scale` is relative to the gesture start, rotation accumulates in radians, and
the target is the widget under the gesture centroid when the gesture begins.
The interaction layer also turns continuous touchpad axis sources into kinetic
scrolling after axis stop, with velocity estimated from protocol timestamps and
exponential friction. A new axis event, pointer press, leave or touch cancel
stops momentum, and scrolling clamps at the container bounds. The application
loop includes the next long-press deadline in its poll timeout so the hold
fires on time without a busy loop.

Keyboard handling uses an xkbcommon keymap supplied by the compositor.
Keysyms and UTF-8 text are delivered to a window-level key callback first, then
to the focused widget after Tab/Shift+Tab traversal. The event loop implements
key repeat for compositors that do not repeat for the client: keymap
`repeat_info` rate and delay are honored, with 500 ms/25 Hz fallbacks. Repeat
uses the application timer queue, not a busy loop.

`zwp_text_input_v3` is enabled when an entry gains focus and disabled on leave.
Enabling text input does not hand every key to the input method: keys that the
input method does not consume still arrive as `wl_keyboard` events and are
inserted from their xkb UTF-8 text, while `commit_string` carries composed text.
The protocol requires this split, because it counts ordinary keyboard typing as
a text change that must be reflected in surrounding text just like an input
method commit. Preedit and commit strings are collected per `done` event,
applied to the focused entry, and the caret rectangle is reported back to the
compositor for the candidate window. Pt pushes state at three moments, and 1.0
freezes them:
when focus enters a text input (enable, content type, surrounding text, caret
rectangle); whenever a keystroke, pointer gesture or programmatic edit changes
the text, caret or selection (surrounding text and caret rectangle); and when a
composition ends, whether by commit, preedit application or cancellation (the
same update, with the caret placed where the composition ended). Every push
ends with a `commit`. A `done` that carries no commit string, no preedit and no
deletion range is the input method acknowledging the state Pt just pushed, so an
entry with no composition in flight ignores it rather than answering: the answer
would push another state update and therefore ask for another acknowledgement,
and on a live desktop the two sides then keep each other busy — measured at
13,500 rounds per second, each with a redraw and a `selection_changed` event, for
a focused entry. An empty `done` that arrives mid-composition is still the only
end-of-composition signal, so it clears the preedit and redraws as before. The
same three moments are stated in `include/pt/widget.h` and pinned by
`test_ime`, because an input-method engine written against 1.0 will assume a
reply to every event it sends.

Entry editing is modelled as UTF-8 byte positions with a caret and an
independent selection anchor. The anchor and cursor are normalized before every
edit, so selections can be made in either direction. Mouse down starts a
selection, drag extends it, double-click selects a word (ASCII alphanumerics,
`_`, and non-ASCII code points except the ideographic space) and triple-click
selects all. Shift+arrows,
Shift+Home/End and Ctrl+A extend or replace the selection; inserting text,
Backspace, Delete, cut and paste replace it. Undo/redo uses up to 32 snapshots
of text plus caret/anchor state; a new edit clears the redo branch. IME preedit
is drawn with an underline and never enters the undo history until committed.

Clipboard support uses `wl_data_device` and `wl_data_source`. Outgoing text is
offered as `text/plain;charset=utf-8`, `text/plain` and `UTF8_STRING`; incoming
selection data is read through a non-blocking pipe with a 1.5 second timeout
and a 16 MiB cap. Cut/copy/paste and the public
`pt_window_clipboard_set/get` API share this path. Returned clipboard strings
are malloc'd and released with `pt_free`.

External drag and drop reuses the data-device path. A drag enter hits the
deepest registered drop target under the pointer; the enter callback chooses
whether to accept the offered MIME type, motion and leave callbacks track the
hover, and drop reads text-like data with the same timeout and size cap. Drop
targets are inherited by descendants, and `pt_widget_get_drop_hover` exposes
the current state. Entries become drag sources once a selection is dragged a
few pixels, and `pt_window_begin_drag` starts a drag for a custom widget while
the pointer's implicit grab is valid, offering text, a URI list or
caller-supplied bytes with a MIME type; the destination inspects the whole offer
through `pt_drop_offer_mime_count`, `pt_drop_offer_mime`,
`pt_drop_offer_has_mime` and `pt_drop_offer_actions`, picks one type in
`enter_offer` and streams it with `pt_drop_offer_read`.
`pt_window_begin_text_drag` and `pt_window_begin_uri_drag` were the older
single-type forms; deprecated in 0.9, they were removed by the 1.0 freeze
candidate, so a `pt_drag_data` entry is the only source. An
RGBA drag thumbnail can
replace the built-in one through `pt_window_set_drag_image`. The
`pt_uri_list_*` helpers parse CRLF-tolerant URI lists and skip comments and
blank lines, returning each URI as a newly allocated string.

### Layout and painting

Layout is a single retained-mode flex pass over visible children: padding,
margin, gap, explicit/auto sizes, min/max constraints, grow weights, cross-axis
alignment and six justify modes. Text-bearing widgets measure through
FreeType/HarfBuzz during layout, including wrapped label height. Scroll
containers lay out their content at full width minus the scrollbar gutter and
clip during paint. Direction is logical in 1.0: text is stored and indexed in
UTF-8 byte order, row children are placed from the first child to the last, and
`start`/`end` alignment is physical left/right. HarfBuzz guesses the script and
direction of the single run it shapes, but there is no paragraph reordering or
bidirectional offset map. [rtl.md](rtl.md) specifies the post-1.0 direction
model, UBA pipeline, UI mirroring and the logical-to-visual mapping that editing
will require.

Wrap and grid are measured in the same pass. Wrap measures each child and
starts a line when the next child plus the column gap would exceed the inner
width; each line is justified independently and its height is the tallest
child. Grid divides the inner width into equal columns, honors per-child spans
(clamped to the column count) and gives every row the height of its tallest
cell. Both stretch children on the cross axis by default, so intrinsic-width
children such as chips set `align_self` to start. The containers recompute on
any width change, which makes window resizes the only reflow trigger.

The virtualized list is also an ordinary widget with a layout traversal, but its
children are a fixed pool rather than a row per data item. Layout derives the
visible index range from `scroll_y`, row height and viewport height, adds one
row of overscan, and rebinds pooled rows through the application callback.
Rows outside the range are hidden and reused. Layout and paint cost therefore
scale with viewport height instead of `row_count`; the application data itself
stays outside the widget tree. Selection is list state, so recycled rows do not
lose it; keyboard navigation scrolls the selected row into view. Rows are
list-owned containers, and custom cells are normal widget trees inside them.

The table and tree reuse the same pool. A table owns a header row plus a pool
of row widgets, each holding one cell per column. Layout derives the first
visible row from `scroll_y`, binds the pooled cells through the application
callback and hides the rest, so row and cell widgets scale with the viewport.
Column widths are stored per column in the table state, so they survive row
recycling and binding, and can be resized by dragging the header dividers.
Columns can be hidden without discarding their width, title or cells; hidden
columns are skipped by layout, paint, hit-testing and accessibility. Clicking a
header reports the full priority-ordered sort list (up to four columns);
Shift-clicking appends a column, and the header paints a chevron for each sorted
column. A row key callback provides stable `uint64_t` identities. The table
records the selected key, resolves it after row-count changes and refreshes,
and reports row keys through AT-SPI; a row text callback feeds keyboard
type-ahead without materializing offscreen rows. Batch invalidation unbinds only
the rows intersecting a range, so a model edit does not rebuild the pool. A
tree owns a node list (text, parent/children, expanded flag, depth, user data)
outside the widget tree and flattens it into a visible-node array whenever
structure or expansion changes. Its layout then binds the same kind of pooled
rows. Selection and keyboard navigation are data view state, not row state, so
recycling never loses them; Home, End and Page keys move by viewport, and
AT-SPI reports row/column indices, sort state, tree level and expanded state.

Editing lives inside that pool rather than beside it. `pt_table_begin_edit`
detaches any open editor, creates one child entry whose `editor_owner` is the
table, seeds it with the cell text and names it after the column title, then
focuses and selects all of it, so keys, IME, clipboard and undo run through the
same entry path a normal field uses. Commit copies the draft into the cell,
fires the edited callback and emits a value-changed AT-SPI event; Escape
detaches without writing. Trees use the same shape for a node:
`pt_tree_begin_rename` seeds the node text and commit replaces it before the
rename callback runs. Multi-row selection is a byte bitset in the table state
with its own anchor and count. `pt_table_select` collapses the set to the
primary row, while Shift-click down and a drag started on a row or in empty
viewport space extend it through `pt_table_select_range`. That drag also moves
a logical marquee rectangle which the paint pass clips to the table's inner
area, and past a
four-pixel vertical threshold it becomes a row drag whose final index is
reported at pointer up. Header drags either resize a column at its divider or,
when reordering is enabled, track a drop target from the midpoints of the
visible header cells. Committing a reorder swaps width, title, alignment,
resizable and hidden flags as adjacent pairs, remaps the sort priority list and
rebuilds the header, so a column's geometry never gets separated from its
identity.

Tabs and sidebars are layout containers rather than separate render paths. A
tab strip draws its titles and manages page visibility; pages are ordinary
child trees clipped to the content area. A sidebar lays out icon-and-label
items, switches to an icon-only rail in compact mode and emits selection like a
list. The 21 built-in icons are stroked polylines in a 24-unit coordinate
space, scaled to the requested pixel size and drawn through the canvas line
routine, so navigation needs no image assets.

Paint walks the tree with a clip stack. Before painting a widget,
`intersects_clip` rejects subtrees that cannot contribute pixels: the widget
rectangle is transformed to device space and expanded by 48 logical pixels to
account for shadows and focus rings. Scroll containers push their content clip
before painting children, so offscreen rows are skipped.

Painting is damage-driven. Setters, focus/hover changes, timers and text edits
mark logical rectangles through `pt_widget_damage`, `pt_window_damage_rect` or
`pt_window_damage_all`; the window unions them until the next frame. A forced
full paint resets the region, while `pt_window_paint_damage` clips to the union
and clears only that area before repainting. The Wayland backend keeps a
persistent backing canvas at device resolution. Each `wl_shm` buffer records
the damage accumulated since it was last refreshed; when a buffer is selected,
the union of the current frame and its pending region is copied from the
backing canvas, and other buffers accumulate the current frame for later reuse.
Surface version 4+ receives `wl_surface.damage_buffer` for that pixel region;
older surfaces fall back to a full `wl_surface.damage`. A frame with no damage
attaches and commits nothing, so idle windows and caret timers do not produce
empty swaps.

The title bar, cards, buttons, entries, toggles, checkboxes, radios, sliders,
progress bars, spinners, segmented controls, tabs, sidebars, tables, trees,
scrollbars and focus rings use anti-aliased rounded-rectangle SDF coverage,
gradients, borders and soft shadows. Icons share the same color and damage
path; table headers and tree rows reuse the list row painting primitives. All
colors come from 38 semantic roles (surface tiers, container and sunken
variants, borders and outlines, text and disabled text, accent tiers, control
and track surfaces, danger/success/warning with soft variants, focus,
selection, scrim, skeleton pair and shadow) and geometry from 10 metrics
(radii, focus width and offset, elevation, chip and control height). Lookup
walks the per-window override table, then the app theme, then the palette
defaults, so dark and light palettes and per-window restyling share one
painting path; a widget that pins its own background or foreground still
bypasses the table. Toggle knobs, checkbox check marks, spinner phase and
indeterminate progress are time-based mixes that keep frames scheduled only
while they are moving. Programmatic state setters snap animation phases so a
freshly built UI paints in its final state.

### Motion

Motion is expressed as values over time rather than as an animation runtime.
`pt_ease` maps normalized time through a fixed curve set; `pt_transition`
stores value, start, target, elapsed duration and an active flag, with no
allocation or registration. Retargeting while active restarts from the current
value, so interruptions are continuous. Applications advance transitions from
their own timers and request the repaint; framework controls use the same math
for hover, checks and expand/collapse.

Reduced motion is app state. When enabled, the built-in control animations snap
instead of interpolating; the internal controls read the app flag directly
rather than wrapping their decoration in a public transition.
`PT_REDUCED_MOTION` supplies the initial value. This keeps the preference
observable through the public API without a hidden global or a second
animation scheduler.

### Overlays

Menus, dialogs, toasts and tooltips share one overlay model. Dialogs and
toasts are composited inside the owning window; menus and tooltips use native
`xdg_popup` surfaces when the backend supports them and fall back to the
window-local path otherwise. `pt_window_set_popup_policy` selects `auto`,
`native` or `window` explicitly.
Each overlay is described by a `pt_overlay` record: kind, open state,
passive/modal flags, panel widget, logical rectangle, focus scope, timer and
queued action list. The window keeps one z-ordered list (`toasts < menus <
dialogs < tooltips`), so painting, hit testing, focus traversal and damage all
consult the same ordering. Panels are ordinary widget trees: menu items are
ghost buttons with a `menu_item` role and an optional shortcut string, and
dialog actions are buttons. This reuses the existing layout, painting,
keyboard and animation paths instead of a second widget system.

Menu items and dialog actions attach a small action record. When clicked, the
overlay is removed first and the callback runs afterwards, so callbacks can
safely open another overlay, mutate the tree or destroy the handle. Removed
panels and action records are collected at a deferred point after painting.
Opening a menu dismisses other menus and the active tooltip. Tooltips arm a
per-widget delay timer on hover and are passive for focus. Toasts stack in the
bottom-right corner, expose an alert role and expire through window-owned
timers.

Dialogs paint a modal scrim, center their card, trap Tab/Shift+Tab between
actions, close on Escape and restore the previous focus. Dialog and toast
panels stay window-local, so they can be clipped at its bounds; menus and
tooltips can cross the window edge through a native popup.

A native popup owns a child `xdg_surface`/`xdg_popup`, an optional
`wp_viewport` for fractional scaling and its own triple-buffered `wl_shm`
buffers. The positioner anchors at the bottom-right, asks the compositor for
slide and flip constraint adjustment, and can be repositioned when the anchor
moves. Menus grab the seat while open; tooltips set an empty input region so
clicks pass through. The grab serial is the window's last pointer serial, or the
seat's most recent serial when no pointer event has landed on that window yet, so a
menu opened from a key handler grabs with whatever the seat last saw. The
compositor's `popup_done` dismisses the overlay, and
local point translation keeps hit testing and painting identical on both
paths.

### Accessibility

Widgets derive an accessibility model from their kind and content: role,
accessible name, description, state bits (`visible`, `enabled`, `focusable`,
`focused`, `checked`, `selected`, `editable`, `busy`, `expanded`, `expandable`,
`modal`, `selectable`, `multi_selectable`), text with caret/selection offsets
and a normalized numeric value. Applications can override role, name and
description through the public API. Entries expose UTF-8 byte offsets; text
areas add the text-frame role and the same offsets; password entries hide their
text but keep the caret and selection shape. Lists expose list and list-item
roles, the selected row and focusable rows. Tables report table, row and cell
roles with selected row, row key, one-based row and column indices and the
priority-ordered sort columns with directions; trees report tree and tree-item
roles, the one-based level, position in set, set size and expanded state; tabs
and sidebars expose their navigation roles and the active item; icons use the
icon role. A combo box reports combo-box with `expandable`, plus `expanded`
while its popup is open, and the popup itself is a list box of list items.
Labels and buttons report zeroed offsets. Dialog panels and menu items get their
dialog/menu roles automatically.

Selection is reported as a set rather than a single row. Every enabled row of a
list, table, tree or sidebar carries `selectable`, and the rows the model has
picked carry `selected` on top of it, so a reader can announce an unselected row
as a choice the user can still make. The three data views additionally carry
`multi_selectable`, because their selection can hold more than one row; a
sidebar and a tab strip offer one choice at a time and deliberately do not. A
tree branch carries `expandable` and, while open, `expanded`; a leaf carries
neither. Disabling a view or a row drops `selectable` from the rows underneath
it while the set itself is kept, so `selected` stays on and the container keeps
saying what it is.

The app-level callback receives synchronous focus, state, text, selection,
value, name and structure events with the originating window and widget. The
model has no D-Bus or AT-SPI dependency, so it stays usable in headless tests
and server tooling.

`pt_app_enable_a11y` starts an optional bridge that mirrors the widget tree to
the AT-SPI2 registry over D-Bus. Pt exposes one derived accessible object per
widget, registers the root through `org.a11y.atspi.Socket.Embed`, and serves
`Accessible`, `Application`, `Component` (including absolute extents),
`Action`, `Text`, `EditableText`, `Value`, plus `Table`, `TableCell` and
`Selection` on the data views. The selection side walks the real row set:
`GetSelectedChild` takes an index into it, `DeselectSelectedChild` drops one
row, `SelectAll` selects the whole model, and `Table.GetSelectedRows` and the
`NSelectedRows`/`NSelectedChildren` counts report every selected row. State
bits map onto their AT-SPI names, `selectable` to `STATE_SELECTABLE` and
`multi_selectable` to `STATE_MULTISELECTABLE`; the folded half of the expand
pair is spelled out, so a branch that can open but is shut arrives as
`STATE_COLLAPSED`. `STATE_SELECTABLE` on a row means what it does in the model:
an unselected row is still a choice. A `SelectionChanged` event is followed by a
`selected` state event on the row the view treats as its caret, which is the row
a gesture just moved to rather than the lowest one in the set. Focus, state,
text, selection, value, name and structure changes emit AT-SPI events.
The bridge is compiled only when `dbus-1` is available. `pt_app_enable_a11y`
returns false and `pt_app_get_a11y_status` reports
`pt_a11y_status_unavailable` when the build or session has no accessibility
bus; the in-process model and Pt widgets continue to work. Disabling the bridge
leaves the in-process accessibility model untouched.

### Event loop and timers

`pt_app_run` and `pt_app_run_for` use a Wayland-aware blocking loop:
`wl_display_dispatch_pending`, `wl_display_flush`, `wl_display_prepare_read`,
`poll` on the display fd and the optional AT-SPI D-Bus fd, then
`wl_display_read_events`. Frame callbacks are requested only when a window is
dirty or animating after a paint. The poll timeout is 250 ms when idle, 16 ms
when a window wants frames, and shortened to the next timer or interaction
deadline (such as a pending long press). Headless mode uses a plain `poll` on
no descriptors so tests exercise the same timer and cleanup logic.

Timers are one-shot, owned by the app, identified by a 64-bit id, and cancelled
on destruction. Close requests are checked by a close callback; accepted closes
are queued and freed at a safe point in the loop, which prevents use-after-free
from widget or window callbacks. The last accepted close quits the loop unless
`pt_app_set_quit_on_last_window_close(app, false)` is set.

### Bindings

The flat ABI is wrapped in four places:

- C++23: header-only non-owning handles plus a move-only `pt::app` that owns the
  native graph (RAII), with fluent setters and factories.
- Python 3.10+: one ctypes module. Module-level registries retain window
  wrappers and callback trampolines; `user_data` stays `NULL`. Row-key
  trampolines return `uint64_t`, and row-text trampolines retain a borrowed
  Python-encoded buffer for the duration of the lookup.
- Java 22+: one FFM class using `Linker.downcallHandle` and `upcallStub`. Handler
  interfaces are explicit, every registration takes an `Arena`, and strings
  are allocated as UTF-8 in confined arenas for the duration of each call. Row
  text uses a persistent 256-byte upcall buffer owned by the arena.
- C: the ABI itself.

No binding touches internal structs or duplicates layout/paint logic. All 412
public functions are wrapped in all three bindings and the audit enforces that
parity as part of `make check`. See [bindings.md](bindings.md) and
[api-reference.md](api-reference.md).

### Testing strategy

Core, canvas, font, layout, widget and window tests are headless executables
with a small assertion harness. They use internal headers where needed to
verify layout geometry, pixels and event semantics. The window suite covers
client-side decoration hit zones, cursors, fractional paint scaling, resize
clamping, animations and scroll painting. The Wayland suite maps a real
window, exercises clipboard text and checks that configure produced a valid
buffer scale; it skips with a message when no compositor is available.

At 1.0 the headless C suite is 29 executables: 27 regular suites carrying 2,968
checks, the 454,380-check randomized property fuzzer and the AT-SPI bridge (236
checks when a live accessibility bus is present, 11 otherwise). The live Wayland
smoke test is not part of `make check`: `make check-wayland` builds it and runs
its 48 checks under a compositor, skipping with a message when none is available.
The three bindings add C++ (344 checks), Python (33 unittest cases) and Java (298
checks) suites. `test_overlay` covers
menu navigation and dismissal, dialog
focus trapping and action ordering, toast stacking/expiry and tooltip timing;
`test_gestures` covers compositor gestures, long press and kinetic scrolling;
`test_dnd` covers external drag-and-drop enter, motion, leave and drop state;
`test_a11y` covers roles, names, states, text/value exposure and event
delivery; `test_a11y_bridge` serves the AT-SPI object tree over D-Bus;
`test_damage` covers damage unions, partial paint isolation, the shape-cache
bounds and touch-to-pointer mapping. The 0.5 suites add `test_flex_grid`
(wrap/grid geometry, gaps and spans), `test_text_area` (wrapping, selection,
caret goals, undo and grow-to-content), `test_data_view` (row recycling, pool
bounds, selection, keyboard navigation and a11y roles) and `test_motion`
(easing curves, transition retargeting and reduced motion). The 0.6 suites add
`test_table` (column layout, sorting, recycling and selection clamping),
`test_tree` (hierarchy, expansion, navigation and node data) and
`test_navigation` (icons, icon buttons, tab pages and sidebar compact state);
`test_dnd` also covers URI-list parsing and drag images. The 0.7 suites extend
`test_table` (multi-column sort filtering, hidden columns, keyed selection and
batch refresh), `test_tree` (Home/End/Page navigation and keyed rows),
`test_data_view` (keyed list selection and refresh ranges) and `test_gestures`
(drag cancellation/timeout and eraser/barrel buttons); the live AT-SPI test
checks row/column indices, sort state, tree level and expanded state. The 0.8
suites add `test_ime` (text-input-v3 preedit, commit, surrounding text,
delete-surrounding-text, cursor rectangle, undo grouping and the rule that an
empty `done` neither redraws nor re-announces the selection) and
`test_rich_text` (span clamping and clearing, run building, measurement,
wrapping and painting, edit maintenance across styled runs and the hashed font
cache); `test_dnd` covers streamed offers, chunk reads, cancellation and
`max_bytes` truncation, and `test_a11y_bridge` asserts the Table, TableCell
and Selection interfaces rather than only attributes. The 0.9 suites add
`test_theme` (token and metric defaults, dark and light palettes, accent
derivation, app setters, window overrides and override clearing),
`test_editing` (table cell editing and commit, multi-row selection ranges,
column reorder, row drag, and tree rename commit versus cancel) and
`test_visuals` (chip and skeleton paints, busy and invalid state draws,
per-run family and size baselines, and the snapshot harness that renders a
deterministic gallery into `build/snapshots` and compares frame hashes across
repeats, theme round trips and window overrides). The 0.10 suites add
`test_combobox` (item model, select-only and filterable modes, popup geometry
inside the window edges, keyboard choice and the popup's list-box roles) and
`test_selection` (one gesture vocabulary driven across the list, the table and
the tree: set helpers, Ctrl-click toggle, Shift ranges from the anchor, the
rubber band with and without Ctrl from a row and from empty viewport space,
Shift+arrow sweeps, Ctrl+A and the anchor rules that survive a shrinking
model). `test_visuals` gained a second gallery:
an unselected frame, a multi-row selection in all three views, a rubber band
caught mid-drag, an open combo box popup and a light-theme pass, each hashed and
compared against the others so a state that stops painting shows up as two
identical frames. `test_a11y` now covers the data-view role and state matrix, and
`test_a11y_bridge` asserts the multi-row Selection and Table reporting,
`SelectAll`, per-row deselect and the `SELECTABLE`, `MULTISELECTABLE` and
`COLLAPSED` state bits. The
C++/Python/Java tests exercise the corresponding binding surfaces. Renderer and
font tests also write PNGs to `build/tests/` for manual inspection; they are
not golden-file comparisons because font rasterization legitimately varies
across font packages and FreeType versions. `test_visuals` instead compares a
single process against itself, so it catches nondeterminism in the paint
pipeline without pinning pixels to one machine's font stack.

### Threading

Pt has no internal locks. The UI thread that created the app is the only thread
that may touch an application, window, widget, overlay, accessibility model,
drag, gesture or theme state. That includes getters: a read that races layout or
paint is the same data race as a write, so "read-only" does not mean
"thread-safe". Callbacks and timers run on that thread, and a callback may call
the API.

| Entry-point class | Thread | Notes |
| --- | --- | --- |
| App lifecycle, windows, widgets, overlays, theme get/set | UI thread | Getters are not thread-safe either |
| Mutations, constructors, destroy, focus, show/hide | UI thread | Setters are no-ops for NULL, wrong kind or bad index |
| Accessibility model and event callback | UI thread | Events are delivered synchronously |
| Drag/drop, gesture and stylus state | UI thread | Callback pointers are borrowed for the call |
| Framework callbacks and `pt_app_add_timeout` timers | UI thread | Must not block; user_data stays valid until replaced |
| `pt_ease`, `pt_ease_value`, `pt_transition_*` | any thread | Operate only on caller-owned storage |
| `pt_rgba`, `pt_rgb`, `pt_color_alpha` | any thread | Pure value helpers |
| `pt_uri_list_count`, `pt_uri_list_get` | any thread | Parse caller-owned input; the get allocates and reports through the thread-local error |
| `pt_free` | any thread | Plain `free` |
| `pt_get_last_error` | calling thread | Thread-local buffer, valid until the next Pt call on that thread |

There is no `pt_app_invoke` in 1.0. Cross-thread work belongs in an
application-owned queue drained by a UI-thread timer; invoking the UI from a
worker with Pt calls is undefined behavior. The C++, Python and Java bindings
inherit the same rule: a callback that runs on the UI thread may call into any
binding, and a worker thread may not.

### Memory and errors

Objects have unambiguous owners: the app owns windows; a window owns its
widget tree and toast handles; menus and dialogs are allocated by the caller
through `pt_menu_create`/`pt_dialog_create` and released with their matching
destroy function, independently of the window that hosts them. Overlay panels
are window-owned widget trees and are collected after dismissal. A list owns its
recycled row widgets; bind callbacks configure rows but must not destroy them.
Setters copy strings. The only global is the thread-local last-error string and
immutable version data. Allocation failures abort with a diagnostic rather
than leaving the tree half-valid; errors from external systems are reported
through `pt_get_last_error`.

## Dependencies

| Dependency | Role | Required |
| --- | --- | --- |
| wayland-client, wayland-cursor | transport, cursors | yes |
| xkbcommon | keyboard decoding | yes |
| fontconfig, freetype2 | font lookup and rasterization | yes |
| harfbuzz | shaping | yes |
| zlib | PNG snapshot compression | yes |
| wayland-protocols / wayland-scanner | generated protocol code; XML is vendored | build-time only |
| libm | geometry | yes |

No GL, GTK, Qt, GLib, X11, Rust or C++ toolchain is required to build the C
library. `pt.pc` keeps the Wayland/font/zlib modules in `Requires.private`;
public headers expose no dependency types, so shared-library consumers only
need `-lpt` (plus libm when linking statically).

## Research references

- Wayland Book, shared-memory buffers, damage and frame callbacks:
  https://wayland-book.com/surfaces/shared-memory.html
- wayland.app, fractional-scale-v1 and xdg-decoration protocol descriptions:
  https://wayland.app/protocols/fractional-scale-v1
  https://wayland.app/protocols/xdg-decoration-unstable-v1
- Win32/Wayland fractional scaling strategies in winit:
  https://github.com/rust-windowing/winit
- HarfBuzz shaping and FreeType rasterization:
  https://harfbuzz.github.io/ and https://freetype.org/
- Java FFM API (JEP 454):
  https://openjdk.org/jeps/454
- Python ctypes callback and buffer documentation:
  https://docs.python.org/3/library/ctypes.html
- Accessibility and text rendering background:
  https://behdad.org/text2024/
