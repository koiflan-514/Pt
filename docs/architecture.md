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
are exported (412 in the frozen 1.0 ABI, 448 in 1.1, 554 in 1.2 and 556 in
1.3; later minors append to the same SONAME). Internal headers live in `src/`
and are not installed.

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

### Font fallback pipeline

A UTF-8 run is segmented by HarfBuzz script, and each segment is shaped with
the primary face when that face covers every codepoint, otherwise with the
first family in the fallback chain that does. `pt_app_set_font_fallbacks`
installs the app-wide chain and `pt_app_set_font_fallbacks_for_family` replaces
it for one primary family; an empty chain restores Fontconfig's own sort order.
An explicit chain is authoritative: if none of its families covers a codepoint,
the segment keeps the `.notdef` (tofu) glyph even when another installed font
would have it, and `pt_app_font_uncovered_count` counts those codepoints.

The cache is process-wide, so a chain installation repaints the calling app and
applies to text laid out afterwards. Block metrics compose over the faces a line
actually uses: ascent is the maximum ascent and descent the maximum descent,
and the line height is `max(ascent + descent, primary line height)`. Caret,
selection and hit-test offsets remain UTF-8 byte offsets in the original string,
and the accessibility text is unchanged. The override table is bounded by
`PT_FONT_FALLBACK_MAX_OVERRIDES` and evicts the least recently set family.

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

Pt negotiates `zxdg_toplevel_decoration_v1` through `xdg-decoration`, but the
public API separates who decorates the surface from whether Pt draws a header.
The default is explicit client-side mode: Pt draws its 44 px header and asks for
`CLIENT_SIDE` at the first commit, because the header, its edge resize zones and
hit testing are built for it. `pt_window_set_decorated(false)` only removes the
header and its edge zones; it never turns the request into server-side.
`pt_window_set_decoration_mode` selects `auto`, `client` or `server`, where
`auto` follows the framework default and `server` is the explicit SSD opt-in.

The request is a preference. Pt tracks the requested and the effective mode
separately, sends `set_mode` only when the request changes and obeys the
`configure` mode. Sway forces `server_side` on tiled windows even after a
client-side request (Q-DEC-001), niri honours client-side but draws only a
border or focus ring for server-side (Q-DEC-002), and Mutter and Weston expose
no manager at all, so they get Pt's header. Fullscreen hides the header in every
mode, tiling drops the corner radius and shadow, and `configure_bounds` plus
`wm_capabilities` clamp size requests and disable unavailable maximize or close
buttons. Content area, hit zones and header ownership are laid out for the
effective mode, never for the requested one.

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

Window-level raw callbacks sit in front of widget dispatch. A focus callback
reports gain and loss; a key-event callback receives the keysym, modifiers and
the xkb UTF-8 text; a pointer callback receives a normalized
`pt_pointer_event` with a phase, window coordinates, modifiers, button,
per-button click count and press state. Dispatch order is overlay key handling,
the raw key callback, the legacy key callback, then the focused widget.
Consuming a key through the raw callback stops widget dispatch but not internal
focus or serial accounting, so a popup grab that follows still sees the serial
the seat does, and a consumed key is not re-delivered to text input as a
commit. Pointer callbacks run before overlay and widget pointer dispatch; enter,
leave and motion are advisory, their return value is ignored so hover tracking
is unaffected, and down/up/axis can be consumed. Serials are kept per source —
focus/enter, pointer button and key press — because `wp_primary_selection`
requires the serial of the event that triggered the selection; non-left buttons
track their own press state and click count so a right or middle click does not
disturb a left-button double-click.

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

A custom widget can opt into the same input method without being an entry.
`pt_window_set_ime_callbacks` installs commit and preedit callbacks,
`pt_window_set_ime_enabled` activates text input and
`pt_window_set_ime_cursor_rect` places the candidate window. The callbacks own
insertion, preedit replacement (an empty string ends it) and redraw; cursor
offsets are UTF-8 byte offsets into the preedit or -1. Pt keeps re-sending full
state on every change instead of relying on incremental acknowledgement, which
mitigates the KWin/fcitx5 and sway focus desyncs (Q-IME-001, Q-IME-002), and
treats an empty `done` as an acknowledgement rather than a request to answer.

The v2 `preedit_hint` ranges are retained with the current preedit rather than
added to the callback ABI. Ranges are byte offsets into the submitted preedit,
clamped to it and snapped to UTF-8 boundaries; zero-length or out-of-range
ranges are dropped and at most 256 ranges are kept per preedit. Painting adds a
second underline beneath the plain preedit caret underline: `whole` (1.5 px
accent), `selection` (2.5 px accent), `prefix`/`suffix` (1.0 px muted at alpha
0xC0), `prediction` (1.0 px muted) and `spelling_error`/`compose_error`
(2.0 px danger). Overlapping ranges paint in arrival order, unknown values are
stored but draw nothing, and a preedit without hints is pixel-identical to 1.2.
`pt_window_show_input_panel` and `pt_window_hide_input_panel` add the v2
input-panel requests: the desired boolean is recorded even when no text-input
object is active, the call returns whether it changed, the request is forwarded
when IME activates, and a v1-only compositor or headless window never sends
one. The protocol makes a repeated request matching the compositor's state
harmless, so the reactivation replay needs no extra tracking.

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

Primary selection (middle-click paste) uses
`zwp_primary_selection_device_manager_v1` when the seat exposes it and falls
back to an in-process buffer otherwise, so `pt_window_primary_set/get` behaves
the same in a headless run. A selection set from a pointer drag uses the
button-release serial and a keyboard selection uses the last key-press serial; a
programmatic set with no triggering event does not send to the compositor and
reports failure, and an application that mirrors a selection created outside its
own input callback passes the serial explicitly with
`pt_window_primary_set_serial`. Primary reads are bounded by
the same 1.5 s / 16 MiB limits as the clipboard, and the getter returns malloc'd
UTF-8 owned by the caller. The two buffers stay separate: the clipboard serves
Ctrl+C/Ctrl+V, the primary selection serves middle-click paste, and Pt applies
neither policy on its own.

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

`pt_paint_*` exposes that paint pass to applications. A widget paint callback
runs after the widget's own background and content pass with a context clipped
to the widget and expressed in widget-local logical pixels. The callback is
pure: it can run 0..N times per frame (once per damage tile), must produce
identical pixels regardless of tile order or count, must not mutate the tree or
trigger layout, must balance `push_clip`/`pop_clip`, and must not call
`pt_window_paint_pixels` re-entrantly. A context is valid only inside the call;
`pt_widget_request_redraw` and `pt_widget_request_redraw_rect` queue work for the
next frame instead. The guards are always compiled in, not selected by an
environment variable: a reentrant snapshot fails and sets the thread-local
error, and a callback that leaves the clip stack unbalanced has the depth and
clip restored before the frame continues, with a warning and an error string.
Using the context after the callback returns is a caller bug rather than a
detectable state, so the header forbids it; the context owns no storage.

`pt_window_paint_pixels` renders one frame into a caller-owned buffer in
premultiplied `0xAARRGGBB`, row-major, scale 1.0. It is a pure snapshot: it does
not damage, clear `needs_draw`/`redraw_scheduled`, touch the last paint time or
collect deferred overlays, so a snapshot of a mapped window leaves the next live
frame intact. `pt_window_paint_pixels_ex` takes a `pt_paint_pixels_desc` with
stride, scale and `pt_pixel_format`; width and height are device pixels, a
zero stride means `width`, a zero scale means 1.0, only premultiplied ARGB8888
is defined, and the padding pixels of an oversized stride are left untouched.
Custom paint and golden images, including fractional scales, are therefore
testable headless.

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

### Application-owned popups and pickers

`pt_popup` is the public form of the popup machinery the menus and tooltips
already use. It takes a parent window and a content widget, adopts the content
tree, and owns a native `xdg_surface`/`xdg_popup` with its own buffers when the
platform supports it; otherwise it behaves as a window-local overlay. The anchor
is a rectangle in parent logical pixels and the positioner asks for flip and
slide constraints, so the surface moves to the opposite side and slides along
the other axis rather than leaving the work area. A per-window open limit
(`PT_POPUP_MAX_OPEN`) rejects a seventeenth popup without disturbing the open
ones. An outside click, Escape or a `popup_done` closes the popup and runs the
dismiss callback after the open state is cleared; a menu-style seat grab is
taken for the open popup, and the window's overlay ordering keeps painting, hit
testing and focus identical across the native and fallback paths.

The 1.2 pickers are editor fields built from an entry, a popup and the existing
grid/list primitives rather than a second widget kind: searchable select, date
picker, number picker (which has no popup) and colour picker. They report
`pt_widget_combobox` as their kind so the shipped keyboard, pointer,
measurement and accessibility dispatch applies unchanged; the `pt_combobox_*`
item and mode API is a no-op on a picker, and only `pt/picker.h` functions
apply. The entry keeps focus while a popup is open, so typing still filters,
Tab closes and continues the focus order, and dismiss restores the committed
value. The select list lives in the same popup surface, the calendar renders a
fixed six-week grid, and the colour palette is a fixed-size preset grid.
Programmatic setters are silent; only user input fires the change callback.

### Icon images and caching

`pt_icon_image` is a reference-counted raster handle. PNG loads reuse the
bounded clipboard decoder (16 MiB input, 8192² decode) with an icon-specific
4096 px source cap; hicolor lookup reads `index.theme` and scans `NxN`
directories under the XDG data directories when the index is unusable. The
button and window slots take their own reference. Rasterisations are cached per
size and scale in a process-wide LRU bounded by
`PT_ICON_CACHE_MAX_ENTRIES`/`PT_ICON_CACHE_MAX_PIXELS` with at most
`PT_ICON_IMAGE_CACHE_MAX_ENTRIES` per handle; a miss that cannot fit draws the
untinted fallback instead of growing the cache.

The loader sniffs the payload after an optional UTF-8 BOM and whitespace: an
`<svg` or `<?xml` prefix takes the SVG path and every other stream stays on the
byte-for-byte PNG path. SVG is parsed by the vendored zlib-licensed nanosvg and
rasterised on demand at each requested device size through `src/svg.c`; a
hicolor `scalable/` directory is searched for `.svg` before fixed-size
directories for `.png`. The supported subset is paths (including arcs), basic
shapes, groups, transforms, fill and stroke paints with width/cap/join/dash,
opacity, fill-rule, linear/radial gradients, `defs`, inline `style` and simple
class rules; text, images, filters, masks, `clipPath`, `use`/`symbol`, markers,
animation and the full CSS cascade are ignored by the parser, whose descendants
are still visited. A document with no complete shape is rejected. Nanosvg
returns straight RGBA, so `src/svg.c` premultiplies each pixel into Pt's
`0xAARRGGBB` blend layout instead of repacking; a zero alpha stays transparent.
Rasterisation preserves the intrinsic aspect ratio and centres the artwork in
the requested box, and the result enters the same per-size/scale cache as PNG.

`pt_window_set_icon` is best-effort: when the compositor advertises
`xdg_toplevel_icon_v1`, Pt rasterises the image to a dedicated immutable
`wl_shm` buffer (never a frame buffer), attaches it with the window scale and
keeps it alive until the icon or window is replaced, resubmitting on a scale
change. Without the global the call is a silent no-op that still retains the
image, so a getter and a later submission see it.

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
while its popup is open, and the popup itself is a list box of list items. The
number picker reports the appended `pt_a11y_role_spin_button` role with no
expandable state, and the bridge maps it to AT-SPI `SPIN_BUTTON`; `spin_button`
is value 36 and moves the `pt_a11y_role_count` sentinel to 37.
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

`pt_app_watch_fd` adds application descriptors to the same poll set as the
display and the AT-SPI bridge. The mask is
`pt_fd_event_read`/`write`/`error`/`hangup`, the callback runs on the UI thread,
and `POLLHUP`, `POLLERR` and `POLLNVAL` surface as the error event with at least
one callback. `pt_app_watch_fd` borrows the descriptor (Pt never closes it)
while `pt_app_watch_fd_owned` takes it and closes it on unwatch or app destroy.
Watches may be removed from inside their own callback because the free is
deferred, reject a duplicate watch of the same descriptor, and shorten the
`pt_app_run_for` timeout exactly like a timer. Sanitizer runs assert that
repeated create/destroy cycles do not grow the descriptor or memory count.

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

No binding touches internal structs or duplicates layout/paint logic. Every
function in `api/pt.api` is wrapped in all three bindings and the audit enforces
that parity as part of `make check`. The 1.1 extension surface — paint callbacks
and the borrowed context, offscreen snapshots, primary selection, custom IME, fd
watches and the raw input callbacks — is wrapped under the same rule, so the
binding rules are about ownership and the exception boundary rather than a
separate API. See [bindings.md](bindings.md) and
[api-reference.md](api-reference.md).

### Testing strategy

Core, canvas, font, layout, widget and window tests are headless executables
with a small assertion harness. They use internal headers where needed to
verify layout geometry, pixels and event semantics. The window suite covers
client-side decoration hit zones, cursors, fractional paint scaling, resize
clamping, animations and scroll painting. The Wayland suite maps a real
window, exercises clipboard text and checks that configure produced a valid
buffer scale; it skips with a message when no compositor is available.

The 1.3 wire facility makes protocol sequences a headless gate instead of a
live-only check. `tests/fake_compositor.h` is a header-only Wayland server that
speaks the wire protocol on a private unix socket from a helper thread; Pt
connects through `WAYLAND_DISPLAY` as an ordinary client, so the shipped
connection, registry and request paths run unchanged and libwayland-server is
not linked into the tests. The server advertises the mandatory globals plus
optional capability bits (`FC_OPT_*`: icon, decoration, fractional scale,
viewporter, text input, gestures, data device, primary selection, tablet), so a
test can omit one and assert its fallback; `FC_OPT_TEXT_INPUT_V1` clamps
text-input to v1 to exercise the v2 suppression path. Every received request is
decoded and recorded with object id, interface, opcode and typed arguments, and
tests inject server events and synchronise with a display roundtrip. First
consumers assert the popup positioner's placement parameters on the wire, record
the toplevel-icon submission, replay decoration mode exchanges, drive IME
text-input traffic and inject raw key and pointer events. `tests/wire_driver.c`
is a small C-only adapter that exposes the facility as a flat C ABI, so the C++,
Python and Java binding suites load it (`libwire_driver.so`) and assert that a
real wire key or pointer event reaches a wrapped callback in each language. The
facility is test infrastructure: it adds no public header, symbol or ABI.

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
| Paint callbacks and `pt_window_paint_pixels`/`_ex` | UI thread | Context borrowed for the call; a snapshot has no side effects |
| fd watch callbacks (`pt_app_watch_fd`, `_owned`) | UI thread | Borrowed, or transferred and closed by Pt; must not block |
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
