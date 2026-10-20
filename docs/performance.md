# Performance

Pt uses a CPU renderer over `wl_shm`. The measurements below are reproducible,
headless, and reported without rounding up. They describe the 1.0.0 renderer and
retain the 0.10.0 baseline sections because 1.0 freezes the renderer and the two
figures should agree. They are a baseline for future work, not a claim about
every application.

## Reference environment

| Item | Value |
| --- | --- |
| CPU | Intel Core i7-13700H |
| Compiler | GCC 16.2.1, C23 |
| Build | `-O2 -g`, `make` defaults |
| Session | niri 26.04, Wayland, 1x output scale |
| Python / Java | 3.14.8 / OpenJDK 25.0.2 |
| Kernel | Linux 7.2.9 (CachyOS) |
| Library | `libpt.so.1.0.0`, software renderer |

The machine also ran a desktop session during the measurements. Treat
percentages as shape, not as absolute guarantees for another CPU.

## How to reproduce

```sh
make bench
PT_HEADLESS=1 ./build/bench
PT_HEADLESS=1 ./build/bench-startup
PT_HEADLESS=1 ./build/bench-views
PT_HEADLESS=1 ./build/bench-guards
```

`make run-bench` runs the three reporting benchmarks; `make perf-check` runs
those plus the guard binary, which fails on order-of-magnitude regressions.
`PT_PERF_GUARD_SCALE=2` multiplies every time ceiling for a slower machine; the
guard ceilings are intentionally loose and are not a cross-machine benchmark.
`make check-wayland` runs the live idle guard in a nested compositor when one is
available; it is not part of `make perf-check` because it needs a real seat.
The dense benchmark builds a 1,506-node tree: 300
rows, each with a title label, a spacer, a status label and a ghost button,
inside a 560 px scroll viewport in a 1280x800 window. It warms up with 20
paints, then measures 200 layout-only frames, 200 full-window paints and 200
localized damage frames with `clock_gettime(CLOCK_MONOTONIC)`. Layout timing
excludes paint; paint timing includes the backend clear, title bar, cards, text
and culling. The damage loop marks one visible row dirty each frame and calls
`pt_window_paint_damage`, which repaints only that tile of the persistent
backing canvas. All loops use the same tree and an already-warm glyph cache.

The startup benchmark creates a minimal app and a small card, paints once at
800x600, then runs the event loop for 500 ms with no input and no focused
entry. CPU time comes from `getrusage(RUSAGE_SELF)`. RSS comes from
`/proc/self/status`; PSS from `/proc/self/smaps_rollup`.

The data-view benchmark builds a 10,000-row, four-column table and a
1,000-node expanded tree in one 1280x800 window. It warms up the pools with 20
paints, then measures 200 layout-only frames, 200 full paints and 200 damage
frames on one table cell. Reporting the widget count after warmup shows how
many row and cell widgets the virtualization actually materializes.

## Dense tree: layout and paint

The closing run, from the last `tools/ci.sh` gate pass of 0.10.0 — the pass taken
over the code as it ships, and the source of every block quoted below:

```
nodes: 1506, rows: 300, window: 1280x800
build: 0.74 ms
layout: 3.209 ms/frame (312 frames/s from layout)
paint:  18.241 ms/frame (55 frames/s)
damage: 1.319 ms/frame (7.2% of full paint)
memory: 51032 KiB RSS / 18968 KiB PSS after 220 frames (start: 5288, tree: 6876, warm: 51024)
```

Across the eleven 0.10.0 samples of this binary on this machine — seven archived
gate runs and four direct repeats:

| Metric | Range |
| --- | --- |
| App + window + tree construction | 0.5-2.2 ms |
| Full-tree layout | 2.33-3.21 ms/frame |
| Full-window paint | 14.3-18.2 ms/frame |
| Localized damage paint (one row) | 1.08-1.33 ms/frame |
| Layout throughput (layout only) | 312-430 fps |
| Full repaint throughput | 55-70 fps |
| Localized paint share | 6.9-8.1% of full paint |

Layout is a single pass over 1,506 retained widgets. Paint culls widgets whose
bounds, plus a 48 px logical margin for shadows and focus rings, do not
intersect the active clip. In this scene about a dozen of the 300 rows are
visible inside the scroll viewport; offscreen rows are skipped. The remaining
cost is dominated by full-surface coverage: the 1280x800 background and card,
rounded-rectangle coverage, borders and shading for visible rows, and
anti-aliased shaped text. Rounded surfaces take a squared-distance fast path, so
`sqrtf` is evaluated only for pixels inside the one-pixel anti-aliasing band.

A localized update to one visible row is about 7.2% of a full repaint in this
scene (6.9-8.1% across the round's samples). The window keeps a damage index of
up to eight coalesced tiles and
repaints each tile under its own clip, so the culled traversal visits 15 of the
61 nodes a full frame walks (`bench-guards` reports that ratio directly). The
persistent backing canvas already holds the rest of the frame. On a Wayland
surface the `wl_surface.damage_buffer` rectangles are the only regions the
compositor needs to refresh.

The virtualized list is the alternative for long data sets. Its layout pass
walks one pooled row per visible row plus one overscan row, independent of the
data count, and its paint pass visits only visible rows. The dense-tree numbers
above therefore do not grow with a 10,000-row model: the application cost is the
bind callback for visible rows, not 10,000 widget nodes. The list memory bound
is covered by `test_data_view`, which asserts that the number of materialized
row widgets stays within the viewport-plus-overscan bound while scrolling.

## Data views: 10,000-row table and 1,000-node tree

Same gate pass:

```
table rows: 10000, tree nodes: 1000, visible tree rows: 1000,
widgets: 93, window: 1280x800
build: 0.26 ms
layout: 0.020 ms/frame (49048 frames/s from layout)
paint:  13.545 ms/frame (74 frames/s)
damage: 0.185 ms/frame (1.4% of full paint)
memory: 34200 KiB RSS / 12717 KiB PSS after 220 frames
        (start: 5220, tree: 5580, warm: 34200)
```

Across those eleven samples, layout was
0.004-0.022 ms/frame, full paint 10.8-13.5 ms/frame, one-cell damage
0.162-0.193 ms/frame (1.4-1.5% of full paint) and PSS stayed at 12.4-14.7 MiB. The
10,000-row table and 1,000-node tree materialize
only 93 widgets
in total: two table rows' worth of cells fit the 470 px viewport, the tree pool
covers its 220 px viewport, and the tree's 1,000 visible nodes stay in its node
array rather than the widget tree. The bind callback runs for visible rows
only.

Paint is the dominant cost because the whole 1280x800 window is repainted each
frame, including the table header, the visible row striping and the tree rows.
The damage pass touches one cell and stays at 1.4% of full paint (1.4-1.5% across
the round's samples), which is the
practical case for selection, sorting animations or a live status cell. In a
normal application, damage is the common path; full paint happens on resize or
theme changes.

The full repaint is a worst-case-style measurement: every frame repaints the
whole window, and layout runs every frame even though the tree does not change.
Normal Pt applications paint on state changes and animate only the widgets that
are animating, so a static settings or utility window typically spends almost no
time in the renderer. A dense list that animates continuously is the case where
the 14-18 ms full frame matters. At 1280x800 it holds roughly 55-70 fps; 4K
output with a
similarly dense full-window animation is outside the comfortable range of the
0.10 CPU renderer.

## 4K paint, 4K damage and large-tree scrolling

`bench-guards` repeats the 1,506-node dense scene in a 3840x2160 window,
scrolls a 10,200-node expanded tree and scrolls a 100,000-row table. The closing
gate pass reported:

```
startup first 800x600 frame:     14.966 ms   (limit 250.0 ms)
idle 300 ms CPU:                  0.751 ms   (limit  50.0 ms)
dense nodes / layout / paint:   1506 / 3.172 / 17.876 ms
dense one-row damage:             1.303 ms   (7.291% of full paint)
dense damage traversal:          15 of 61 nodes (24.590%)
4K full paint:                 47.808 ms/frame (2.67x the 1280x800 frame)
4K one-row damage:              4.031 ms/frame (8.4% of the 4K full frame)
4K damage traversal:             15 of 61 nodes painted per frame
large-tree scroll frame:          3.161 ms/frame (layout + paint)
visible tree nodes:              10200, materialized tree row widgets: 13
100k table scroll frame:         13.144 ms/frame (reveal + layout + full paint)
100k table one-cell damage:       0.147 ms/frame (1.1% of the scroll frame)
materialized table rows:          22 of 100000
```

The guard run also reports the two views ceilings: 93 materialized widgets of a
130 limit, 1000 visible tree nodes at the limit itself, a 13.295 ms full paint, a
0.097 ms damaged cell (0.727% of the frame).

The 4K frame covers 8.1x the pixels but costs only 2.67x, because the extra
area is mostly a flat background fill while text and row surfaces stay at their
fixed sizes inside the scroll viewport. It is still a 21 fps full-window
animation, so 4K remains a damage-driven or static-content target rather than a
continuous-animation target. A single damaged row at 4K costs 4.0 ms because the
damage index clips to that tile instead of clearing 8.3 million pixels: the same
update at 1280x800 costs 1.30 ms, so the cost grows with the tile, not with the
surface. The scroll scene keeps all 10,200 nodes in the tree's visible array but
materializes only the 13 row widgets that fit the 320 px viewport plus overscan;
each scroll step lays out that same small pool and paints the window in about
3.2 ms. These numbers are the guard inputs: regressions beyond the ceilings in
`bench/guards.c` fail `make perf-check`.

Guard runs of this round on this machine — seven archived `tools/ci.sh` gate runs
and four direct runs of the same binary — put the dense 1280x800 frame at
14.318-17.876 ms, the 4K frame at 37.657-47.808 ms, 4K one-row damage at
3.291-4.070 ms, the large-tree scroll at 2.690-3.509 ms and the 100,000-row seek
at 11.531-14.921 ms, with every ceiling holding in every one of them. The
narrowest margin of the whole set is a time guard: the 100,000-row seek at
14.921 ms of its 20 ms ceiling, 1.34x. The tightest ratio guard is the 4K damaged
frame as a share of its own full frame, 8.425-8.741% of its 12% ceiling and 1.37x
at its narrowest; in the closing pass that guard measured 8.432% (1.42x) and the
seek 13.144 ms (1.52x), so that pass's narrowest margin was 1.42x. The damage traversal
share never moved from 24.590% of 35% in any of the eleven runs. The 4K paint
ceiling is also the only limit
that moves, because `bench/guards.c`
derives it from the same run's dense frame (`dense paint x 14 + 30 ms`, hence 230.4 ms
in the fastest pass and 280.3 ms in the slowest); every other ceiling is a constant.
These figures are therefore a range on this desktop, not a promise; a repeatable
breach of a ceiling is what fails the gate.

The 100,000-row profile is the virtualization ceiling check. Jumping to an
arbitrary row (`pt_table_scroll_to_row` at rows spread across the whole model),
relaying out and repainting costs 13.1 ms per frame at 1280x800 — the same order
as a full paint of the 10,000-row scene (13.3 ms in this pass), because only 22
row widgets exist and
the model is arithmetic, never materialized. Building the 100,000-row table is
sub-millisecond (`row_count` is a count, not an allocation), and damaging one
visible cell inside that scene costs 0.15 ms. The row-widget ceiling of 40 in
`bench/guards.c` is what fails first if virtualization ever regresses.

## 1.0 guard pass

The two closing 1.0.0 `make perf-check` runs on the same machine added the two
profiles M17 required. The marquee profile holds a drag across a 400-row
virtualized list in a 960x640 window: each measured frame moves the band,
applies the new selection to the visible pool and repaints the damaged region.
The popup profile lays out the 240x332 combo panel (80 options, 82 widgets) in
surface-local coordinates and clears and repaints it into its own canvas, which
is the expensive half of the native popup path before the shm copy. Both are
headless and both fail the gate above 20 ms.

```
startup first 800x600 frame        8.990 ms   limit    250.0 ms
idle 300 ms CPU                    0.582 ms   limit     50.0 ms
marquee scene: 27 widgets, 10 selected rows
marquee drag frame                 9.128 ms   limit     20.0 ms
popup scene: 240x332 surface, 82 widgets
popup surface frame                1.254 ms   limit     20.0 ms
dense layout / full paint          2.292 / 13.727 ms
dense one-row damage               1.017 ms   (7.411% of full paint)
4K full paint                     35.941 ms   (2.62x the 1280x800 frame)
4K one-row damage                  3.175 ms   (8.835%)
large-tree scroll frame            2.562 ms
100k table scroll / cell damage   10.763 / 0.124 ms
views full paint / cell damage    10.251 / 0.081 ms
```

The 1.0 renderer is the 0.10 renderer, so the dense and 4K figures sit inside
the 0.10.0 ranges quoted above; the new profiles establish their own first
baseline. The marquee frame is dominated by the selection update and the
damaged-region paint, not by layout of the full 400-row model, because only the
visible pool exists. The popup frame includes the panel's own clear, layout and
paint, but not the compositor's surface commit. The second closing pass measured
a 9.335 ms marquee frame, a 1.287 ms popup frame, dense layout 2.326 ms and
paint 14.074 ms with 1.065 ms one-row damage (7.567%), 4K paint 34.313 ms with
3.039 ms damage (8.856%), large-tree scroll 2.480 ms, 100,000-row seek 10.411 ms
with 0.118 ms cell damage (1.134%), startup 9.678 ms and idle 300 ms 0.613 ms;
every ceiling held. The behavior-only empty-viewport marquee follow-up reran the
same gate on the final tree: marquee 8.800 ms, popup 1.202 ms, dense layout and
paint 2.205/13.252 ms with 1.007 ms damage (7.600%), 4K paint 34.936 ms with
3.118 ms damage (8.924%), large-tree scroll 2.502 ms, 100,000-row seek 10.504 ms
with 0.121 ms damage (1.153%), startup 10.760 ms, 300 ms idle 0.700 ms and live
focused-entry idle 29.9-130.0 ms over 5,000 ms across the final gate reruns, each after one retry. Every ceiling held.

## Startup, idle CPU and memory

The 0.10.0 gate pass, retained for the memory baseline (the 1.0 guard pass
above is the current startup figure):

```
app create: 0.04 ms (rss 5288 KiB)
window+tree: 0.03 ms (rss 5452 KiB)
first frame: 23.74 ms (rss 28504 KiB, pss 10492 KiB)
idle 500 ms: wall 499.5 ms, cpu 1.101 ms
```

Across the eleven 0.10.0 samples the first 800x600 frame measured 11.6-23.7 ms and
500 ms of idle event-loop time cost 0.92-1.35 ms
of CPU. Idle CPU stays below 0.3% of one core
in this configuration. The loop blocks in `poll()` with a 250 ms timeout when no
window wants frames; the measured CPU is mostly periodic timer and event-loop
bookkeeping. `bench-guards` measures the same startup path with a 300 ms idle
window, where the eleven runs cost 0.53-0.75 ms, and holds the idle ceiling at
50 ms scaled.

The first frame is not representative of steady-state paint time: it initializes
Fontconfig, FreeType, a font face, HarfBuzz and the first glyph rasterizations.
Subsequent small frames are much faster. The headless first-frame number also
excludes Wayland buffer allocation, `wl_surface.commit`, compositor scheduling
and presentation, so the latency until pixels appear on screen is higher.

Idle CPU has a headless blind spot, because no input method is attached under
`PT_HEADLESS=1` and the 50 ms idle guard therefore measures only the event loop's
own bookkeeping. On a real compositor a focused entry used to keep the window
redrawing by itself: the input method's empty `done` acknowledgement was answered
with a fresh `set_surrounding_text`, caret rectangle and `commit`, which asked the
input method for another acknowledgement. The accessibility example on niri
measured 67,499 `selection_changed` events and 2.13 s of CPU (1.857 s user, 0.277 s
system) in a 5 s slice — 43% of one core for a window nobody touched — and 36,241
events in 4 s with `AT_SPI_BUS_ADDRESS` pointed at a socket that does not exist,
which rules out assistive technology as the cause. `pt_window_input_ime_compose`
now ignores an empty `done` unless a composition is in flight, and the same 5 s
slice is 1 event and 0.65 s; what is left of that 0.65 s is font shaping and the
example's own timers, not a round trip. `make perf-check` cannot see what only a
compositor can trigger, so M17 added a live idle guard to `tests/test_wayland.c`
and runs it from `make check-wayland`. A window with a focused entry runs for a
5,000 ms slice; process CPU is read from
`clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` and must stay under 1,500 ms. The target
starts a fullscreen nested niri through `tools/nested-wayland.sh`, focuses the
nested window in the parent session and runs the test as the only client of that
compositor's seat. Physical input still reaches the focused nested window, so the
popup checks detect it and retry or skip; a slice that receives a key, an
entry-text change, a focus change or pointer movement is discarded and retried,
and four busy slices report a skip instead of a false regression. The five quiet
manual runs measured 22.8-56.6 ms (0.5-1.1% of one core); gated runs under
desktop activity measured 56.6-745.6 ms, all below the 1,500 ms ceiling. The
0.10.0 state-push loop at 2.13 s in the same slice is 1.4x the ceiling, so the
guard catches the regression it was added for. The live guard also keeps a direct
path for compositors without niri, where a busy desktop can turn the slice into a
skip.

### Where the memory goes

The startup RSS jumps from 5.2 MiB after app creation to 27.8 MiB after
the first frame in the closing pass, but PSS at that point is 10.2 MiB. The gap comes from
file-backed pages shared with the rest of the system: Fontconfig/FreeType font
data, shared library text and similar mappings. RSS counts every mapped page;
PSS divides each shared page among its users. Across the round's eleven samples
first-frame PSS spans 10.2-12.8 MiB, so 10-13 MiB is a more
realistic first-order memory figure for a small Pt process after text
rendering. The dense tree adds widget storage, glyph caches and a 4 MiB
1280x800 canvas: PSS there is 18.5-21.0 MiB across this round's runs while RSS
reads 49.8 MiB (51,032 KiB) in the quoted pass and spans 40.6-49.8 MiB over the
seven archived gate runs — and 65.9-66.1 MiB (67,520-67,732 KiB) in the three
0.9.0 passes, where PSS read 18.4-18.5 MiB. RSS is therefore the figure that moves with what the desktop happens
to share, not with what Pt retains. The
10,000-row table plus 1,000-node tree stays at 12.4-14.7 MiB PSS, 12.4 MiB in the
quoted pass, because only 93
widgets and a small row pool exist; the model data stays in the tree's node
array and in application storage, and the same holds for a 100,000-row table. The
text shape
cache is bounded per font at 256 entries or 256 KiB of retained HarfBuzz
buffers, so it does not grow with scrolling history.

Consequences:

- Pt is not a "2 MiB total memory" toolkit. The font stack is the fixed cost.
- Pt is light at runtime in the sense that the widget tree, layout and paint
  state are small and proportional to the UI, not in the sense that it embeds
  its own tiny font engine.
- If the first-frame memory or initial latency becomes critical, the obvious
  future directions are a leaner font-provider abstraction, platform font
  services, or optional pre-baked atlases. Those are post-1.0 design choices.

## Interpreting the numbers

- Layout and paint are measured in a headless canvas. They cover Pt's own work,
  not compositor upload or GPU composition.
- The damage measurement calls `pt_window_paint_damage` directly; it excludes
  the `wl_shm` region copies, attach and commit that the Wayland backend adds for
  the dirty tiles, but includes the clipped canvas rasterization.
- The 1x output scale uses a 1280x800 buffer. Fractional scale increases pixel
  count and paint time roughly with area; layout stays in logical units.
- The 4K numbers use a 3840x2160 logical window at 1x, not a fractional-scale
  buffer, and the 4K damage figure is the same scene with one visible row marked
  dirty. The large-tree scroll number times scroll reveal, layout and full
  paint; it is not a compositor-side scroll measurement. The 100,000-row table
  number is a jump-to-row seek, not a continuous gesture.
- The benchmark's text is short and repeated, so the glyph cache is warm and
  shape/measure work is modest. Long unique paragraphs shift cost toward
  shaping and wrapping.
- Idle CPU depends on whether an entry has focus (caret blink uses frame
  pacing) and on timers. A static, unfocused window is the low-CPU case; the
  focused-entry live guard is the opposite case and is enforced separately by
  `make check-wayland`.
- PSS and RSS vary with the desktop's font set, Fontconfig cache state and
  kernel accounting. Compare runs on the same machine, not across distros.

## Optimization status

Committed improvements in this release:

- Rounded rectangles take a squared-distance fast path: interior pixels and
  exterior pixels beyond the anti-aliasing band resolve without `sqrtf`, and the
  exact signed distance is only used where a blur falloff needs it (shadows) or
  where a pixel is inside the one-pixel coverage band. `test_canvas` locks the
  anti-aliased edge against a golden pixel hash so the fast path cannot drift.
- The window keeps a damage index of up to eight coalesced tiles instead of one
  union rectangle, and repaints each tile under its own clip. Far-apart updates
  no longer widen into a window-sized repaint, and the traversal shrinks with the
  clip (guarded as "damage traversal share").
- Layout invalidation records a dirty subtree. When the changed widget's slot is
  pinned by explicit requested dimensions and was not shrunk by its parent,
  relayout restarts at the highest such ancestor and damage is limited to it;
  anything else falls back to a full-window relayout.
- Font face lookup is an open-addressed hash table keyed by family, weight,
  slant and quantized pixel size, grown at a 70% load factor, so acquiring an
  already-seen face is a probe rather than a walk of a global list. `test_font`
  asserts identity across the rehash.
- Wayland drawing is guarded against reentrancy: bind callbacks and widget
  updates issued during layout or paint request another frame instead of
  recursing into the renderer from `pt_widget_mark_dirty`.
- Native popup surfaces keep their own three-buffer shm canvas and repaint only
  the popup panel; gesture and momentum work schedules frames only while a
  scroll is moving.
- The virtualized list, table and tree materialize viewport rows plus one
  overscan row, so layout, paint and row memory are independent of the model
  size. The table reuses pooled row and cell widgets; the tree flattens visible
  nodes into a reusable array. Text-area paint clips to the visible visual lines
  and does not mutate scroll state.
- Keyed selection and `pt_*_refresh_rows` rebind only the visible rows that
  intersect the requested window; a sort or model update no longer rebuilds the
  row pool. Hidden table columns are skipped by layout, paint and hit testing.
- A combo box popup rebuild is one panel swap. Filtering or a model change
builds option widgets for the *matching* items only and hands the new panel to
`pt_window_popup_replace_panel`, which defers freeing the old one to the next
paint, so a keystroke costs one allocation pass and one traversal rather than a
surface teardown. The popup is not virtualized, which is the documented cost of
a long unfiltered model.
- Theme resolution stays O(1) after 0.9 widened it. Each window owns fixed-size
  `theme_colors[38]`/`theme_metrics[10]` arrays plus a parallel "is set" array,
  so an override is two indexed reads and a clear resets flags without copying a
  palette; a token lookup never walks a list or allocates.
- Mixed-size styled runs reuse the hashed font face table: a run's family,
  weight, slant and quantized pixel size is a probe, and one pass over the runs
  widens the line's ascent and descent, so a line with a large heading run and
  small body runs shares one baseline instead of measuring per glyph.
- Skeleton shimmer advances a phase value and paints one clipped gradient band;
  like the other time-based controls it schedules frames only while it is
  visible, so a loading placeholder costs nothing at idle.
- `bench-guards` enforces median layout, paint, damage, 4K full and 4K damaged
  paint, marquee drag, popup surface, large-tree scroll and 100,000-row table
  scroll ceilings, so a renderer regression fails `make perf-check` instead of
  only changing a published number.
- Paint culling against the current clip with a fixed logical margin.
- Three-buffer `wl_shm` pool with per-buffer pending damage, a persistent
  backing canvas, clipped partial repaint and `wl_surface.damage_buffer` on
  surface version 4 or newer.
- A per-font LRU text shape cache keyed by an FNV-1a hash plus a full byte
  comparison, bounded to 256 entries, 256 KiB and 8 KiB per text.
- Glyph coverage cache keyed by face, weight, size and glyph id.
- One-shot timers and a blocking `poll()` event loop with no idle polling at
  16 ms.

Known costs deliberately left for later:

- Partial repaint still walks the visible widget tree once per damage tile to
  test clip intersections; the traversal shrinks with the clip but is not
  indexed per tile, and more than eight concurrent dirty regions collapse into
  the least-growing tile;
- A dirty subtree relayouts from the pinned ancestor down, but a wrap, grid or
  content-sized ancestor still costs a full-window pass;
- Shadows keep the per-pixel signed-distance form because the blur falloff needs
  the true distance;
- No GPU path.

Each of these has a clear place behind the current canvas and widget
interfaces, so they can be improved without breaking the public ABI.
