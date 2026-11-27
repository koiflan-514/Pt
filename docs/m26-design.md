# M26 design: 2.4.0 Wayland protocol extensions (proposed)

Status: **proposal and research complete, no feature code** (2026-11-27). This
document is the design and decision record for the next minor release. It was
written on the 2.3.0 tree `7a5b1cc` (branch `m26/scaffold`) and does not change
the library, headers, bindings, tests or build.

Baseline at design time: `api/pt.api` reads **581 functions / 960 entries**,
`ABI_MAJOR = 1`, SONAME `libpt.so.1`, version 2.3.0. The proposed surface is
**587 functions / 968 entries** (6 functions and 2 callbacks, no enum, struct or
define changes), with `ABI_MAJOR` and SONAME unchanged.

## Decision summary

M26 delivers two protocol extensions from the post-2.0 "additional protocols"
item in [ROADMAP.md](ROADMAP.md):

- **xdg-activation-v1** (staging, interface v1): activation tokens for startup
  notification and for passing focus between cooperating clients.
- **xdg-foreign-unstable-v2** (unstable, interface v1): exported toplevel
  handles and imported parents for out-of-process dialogs and stacked windows.

The same release **defers** three candidates with evidence:

- **color-management-v1**: staging interface v3, no HDR output path in the
  software renderer, and no global on the live/CI compositor (niri 26.04).
- **Vulkan/EGL renderer**: the existing 4K measurements do not show a missed
  frame budget on the documented damage-driven target; the 4K full repaint is
  known to be outside the software renderer's comfortable range and would need
  a product requirement before a parity-tested GPU backend starts.
- **standalone theme editor**: it is outside the core ABI and needs a theme
  serialization format that does not exist yet; it belongs to a separate tools
  track.

The scope fallback is explicit: if the xdg-foreign wave does not pass its live
and fallback gates, M26 ships xdg-activation only and the baseline becomes
**583 functions / 963 entries** (2 functions + 1 callback). The foreign API is
then removed before the release, not left half-implemented.

## Evidence and method

Research was read-only against the repository and the installed Wayland stack.
No source, protocol XML or build file was changed by this wave.

Commands and sources used:

```text
git -C <worktree> log --oneline -1                 7a5b1cc
pkg-config --variable=pkgdatadir wayland-protocols /usr/share/wayland-protocols
pkg-config --modversion wayland-protocols          1.50
niri --version / niri msg version                  26.04 (8ed0da4)
```

The installed protocol directory contains:

| Protocol | Installed path | State | Interfaces used |
| --- | --- | --- | --- |
| xdg-activation | `staging/xdg-activation/xdg-activation-v1.xml` | staging, 200 lines | `xdg_activation_v1` v1, `xdg_activation_token_v1` v1 |
| xdg-foreign v1 | `unstable/xdg-foreign/xdg-foreign-unstable-v1.xml` | unstable, 200 lines | `zxdg_exporter_v1`, `zxdg_importer_v1`, `zxdg_exported_v1`, `zxdg_imported_v1` v1 |
| xdg-foreign v2 | `unstable/xdg-foreign/xdg-foreign-unstable-v2.xml` | unstable, 200 lines | `zxdg_exporter_v2`, `zxdg_importer_v2`, `zxdg_exported_v2`, `zxdg_imported_v2` v1 |
| color-management | `staging/color-management/color-management-v1.xml` | staging, 1,756 lines | `wp_color_manager_v1` v3 and eight companion interfaces v3/v1 |
| xdg-shell | `stable/xdg-shell/xdg-shell.xml` | stable v7 | same interface versions as `protocols/xdg-shell.xml` |

`xdg-activation-v1` is no longer in `unstable`. In wayland-protocols 1.50 it is
**staging**: the XML still carries a testing-phase warning, but staging permits
backward-compatible additions and requires a new major version for incompatible
changes. It is not a stable protocol and must not be described as one.
xdg-foreign is only in `unstable`; both v1 and v2 XML files are installed. v1 is
the deprecated predecessor and v2 is the current spelling. M26 vendors and
binds **v2 only**.

### Live compositor evidence

A throwaway client (compiled under `/tmp/opencode/m26probe`, not committed) used
the system XML and the generated client code to query the running niri 26.04
registry and exercise the exact request sequences. The same probe was run
against the parent session and through `tools/nested-wayland.sh`, which is the
live CI environment:

```text
globals: activation=yes exporter=yes importer=yes wm=yes
activation token: done=1 len=32 empty=0
foreign export: done=1 len=32 empty=0
foreign import: destroyed=0
foreign set_parent_of: rc=2
final roundtrip ok
```

The probe created only unmapped `xdg_toplevel` surfaces, requested one
activation token with `set_app_id` + `set_surface` + `commit`, exported the
surface, imported the returned handle in the same client, and called
`set_parent_of` on a second surface. All requests completed without a protocol
error. The token and handle values are opaque 32-character strings; they are not
printed or parsed, and the probe does not claim that focus visibly moved,
because an unmapped surface was used.

The nested niri registry contains:

```text
xdg_wm_base                       7
xdg_activation_v1                 1
zxdg_exporter_v2                  1
zxdg_importer_v2                  1
wp_fractional_scale_manager_v1    1
```

It does **not** contain `wp_color_manager_v1` (and does not contain
`xdg_toplevel_icon_manager_v1`). This is empirical evidence for the deferral,
not an inference from the documentation.

### Matrix evidence from wayland.app

The public matrix (retrieved 2026-11-27, grade C) reports the following for the
eight targets already tracked in [compositor-matrix.md](compositor-matrix.md):

| Compositor (version) | xdg-activation | xdg-foreign v2 | color-management |
| --- | --- | --- | --- |
| Mutter 51 (GNOME) | 1 | 1 | 2 |
| KWin 6.7 (Plasma) | 1 | 1 | 2 |
| niri 26.04 | 1 (live B) | 1 (live B) | — (live B) |
| Sway 1.11 (wlroots) | 1 | 1 | — |
| Hyprland 0.57.2 | 1 | 1 | 3 |
| COSMIC 1.11.0 | 1 | 1 | — |
| Weston 15.0.1 | — | — | — |
| Muffin 6.7.4 (Cinnamon) | 1 | 1 | — |

The niri row is upgraded to grade B by the live registry/probe run above. The
other rows stay grade C and are recorded as such.

## Current code read for this evaluation

| Area | File / function | Observed behavior |
| --- | --- | --- |
| Registry binding | `src/wayland/platform.c:2992-3054` (`registry_global`) | One `else if` per interface. Optional globals are bound when present and left NULL otherwise; every consumer checks the pointer. This is the best-effort pattern M26 must extend. |
| Staging precedent | `src/wayland/platform.c:3013-3018`, `src/wayland/window.c:69-91,838-840` | `xdg-toplevel-icon-v1` binds at version 1, is kept in `pt_wayland_window`, and is a silent no-op when the global is absent. M26 can use the same lifecycle, but no icon buffer is involved. |
| Platform teardown | `src/wayland/platform.c:3119-3245` | Every bound global is destroyed in reverse order. New activation/exporter/importer objects must join this list. |
| Platform start | `src/wayland/platform.c:3078-3117` | `pt_app_create` starts the platform before it returns, so an environment token can be adopted after the first registry roundtrip and before any window exists. |
| Seat / pointer / keyboard serials | `src/wayland/platform.c:164-172,236-244,363-431,176-191`; `src/internal.h:1663-1668` | `button_serial`, `key_serial`, `enter_serial` and `touch_serial` are already tracked per source. `platform_latest_trigger_serial` (`platform.c:2213-2219`) prefers the pointer button and falls back to the last key press. Activation tokens can use the same rule; no new serial tracking is needed. |
| Window surface lifecycle | `src/wayland/window.c:782-843` (init), `891-911` (show), `476-575` (draw), `845-889` (destroy) | The `wl_surface`, `xdg_surface` and `xdg_toplevel` exist from `pt_window_create`. The first buffer is attached and committed in `pt_wl_window_draw`; `wl->mapped` becomes true at line 573. That is the first safe point to consume a startup activation token. |
| Public window focus | `include/pt/window.h:170-175`; `src/window.c:703`; `src/window.c:2002-2025` | The focus callback reports compositor keyboard focus. There is no activation request, no token, and no way to ask the compositor to raise a surface. `pt_window_focus` is widget focus, not toplevel activation. |
| Window public API location | `src/window.c`, plus feature modules `src/dnd.c`, `src/interaction.c`, `src/icon.c` | Public `pt_window_*` functions are not required to live in `window.c`; feature modules already define public window functions. New protocol APIs can live in their own core modules. |
| Fake compositor | `tests/fake_compositor.h:62-85` options, `89-136` kinds, `561-615` interface tables, `3027-3073` globals | Header-only wire replay. Optional globals are opt-in bit flags; interface kinds append at the end so old numeric values stay stable. M26 adds two options and six kinds the same way. |
| Scanner rules | `Makefile:57-71,163-169` | `PROTO_NAMES` drives `-client-protocol.h` and `-protocol.c` from `protocols/%.xml`; generated files land in `build/protocols` and are included by `src/wayland/wayland_internal.h:16-24`. |
| Nested live environment | `tools/nested-wayland.sh:1-138` | Starts a private niri, exports its display and `NIRI_SOCKET`, and runs the command there. Both new protocols are present in that nested compositor (verified above). |
| Matrix document | `docs/compositor-matrix.md:29-36,70-80` | The interface table and protocol-semantics table are the places a new row must be added. |

## Feasibility verdicts

### 1. xdg-activation-v1 - feasible, low-medium risk

**Real need.** A newly launched process does not get to decide that it should be
focused. The established Wayland path is a token created by the focused
launcher and delivered through `XDG_ACTIVATION_TOKEN` (or a D-Bus
`activation-token` field). A Pt application launched from a desktop menu,
file manager or single-instance handoff cannot participate today: it has no
token API and does not read the environment variable. The second need is the
reverse direction: a Pt launcher that spawns another client needs a token to
pass to that child. Both are ordinary application-launcher behavior, not a
special compositor feature.

**Protocol facts that constrain the design.**

- The token object is asynchronous: `get_activation_token`, optional
  `set_serial`, `set_app_id`, `set_surface`, `commit`, then a `done` event.
  The token is a string and is one-shot at activation time.
- `set_serial` is optional, but some compositors refuse activation without a
  valid, recent event serial. Pt should always attach the latest input serial
  when one exists; this is the same rule already used for the clipboard
  (`platform_latest_trigger_serial`).
- `activate(token, surface)` is a request, not a guarantee. Invalid or stale
  tokens are ignored by the compositor. The API must not promise focus.
- The environment variable must be unset after it is read so it is not
  propagated to unrelated children.

**Verdict.** Feasible with one asynchronous callback and two functions. The
protocol object is small and the integration point is the registry plus the
first mapped frame. The risk is semantic, not structural: Pt can send the
request but cannot know whether the compositor honoured it. The header and
tests must say "request", never "focus".

### 2. xdg-foreign-unstable-v2 - feasible, medium risk

**Real need.** Out-of-process dialogs are the concrete case in the protocol:
a sandboxed application exports its toplevel, passes the opaque handle to an
unsandboxed helper, and the helper imports it and sets its own dialog as a
child so the compositor stacks and positions it with the caller. Mult-process
Pt applications and plugin hosts have the same shape. This is not embedding a
foreign surface into a Pt widget; xdg-foreign only expresses a parent/child
toplevel relationship. The API must not promise embedding.

**Protocol facts that constrain the design.**

- The handle is opaque and compositor-defined. It must never be parsed or
  persisted beyond the exported object's lifetime.
- Export is asynchronous (`export_toplevel` then `handle`), and the
  `zxdg_exported_v2` object must stay alive for the handle to remain valid.
  Destroying it invalidates every import that used the handle.
- Import is a `zxdg_imported_v2`; `set_parent_of(child_surface)` applies the
  same stacking/positioning semantics as `xdg_toplevel.set_parent`. An invalid
  handle produces `destroyed`, and the imported object must still be destroyed
  by the client.
- The protocol is explicitly experimental. v1 and v2 are both installed;
  niri 26.04 exposes only v2. M26 vendors v2 only and never binds v1.

**Verdict.** Feasible with four functions and one callback, but it carries the
highest protocol risk in the release. The live probe shows niri accepts
same-client export, import and `set_parent_of` without a protocol error. The
implementation is isolated in `src/foreign.c` and
`src/wayland/foreign.c` so the scope can be dropped without touching the
activation path. The public API is best-effort: every failure is a `false`
return or a silent invalid-handle path, never a crash or a protocol error.

### 3. color-management-v1 - defer, with evidence

The installed XML is staging with manager interface **v3** and a 1,756-line
surface spanning ICC creators, parametric creators (transfer function,
primaries, luminances, mastering display, content light levels), surface
feedback, image descriptions and output descriptions. A correct implementation
is not "bind a global and set a surface property"; it is a color pipeline.

The current renderer has no path for it:

- `include/pt/base.h:70-71` and `src/canvas.c:1-3` define pixels as
  premultiplied ARGB8888 in sRGB.
- `src/wayland/window.c:167,376` creates all buffers as
  `WL_SHM_FORMAT_ARGB8888`.
- There is no linear-light, floating-point or 10/12-bit buffer format, no
  output color-space negotiation and no tone-mapping stage. `docs/theming.md`
  already states that the built-in palette assumes sRGB output and the software
  renderer does no color management.
- The live and nested niri 26.04 sessions do not advertise
  `wp_color_manager_v1`, so the project's primary live target cannot exercise
  even the sRGB subset.
- There is no application request or measured defect that color management
  would fix. HDR output would be a new renderer and buffer contract, not a
  protocol binding.

**Decision: defer past 2.4.** Re-open only when (a) the protocol is stable or
has a frozen staging subset the project can target, (b) a 10/12-bit or
floating-point buffer path exists behind the canvas contract, and (c) a target
compositor in the matrix exposes it and a live test can assert the result.

### 4. Vulkan/EGL renderer - defer, with measurements

The repository's archived release-gate range is in
[performance.md](performance.md), lines 157-206:

```text
4K full paint:        37.657-47.808 ms/frame (about 21-27 fps)
4K one-row damage:     3.291-4.070 ms/frame (8.4-8.7% of the full frame)
dense 1280x800 paint: 14.318-17.876 ms
100k table seek:      11.531-14.921 ms of a 20 ms ceiling
```

A fresh guard run on this host on 2026-11-27 (the research worktree,
`PT_HEADLESS=1 build/bench-guards`) measured:

```text
dense full paint:      9.142 ms   limit 100.0 ms
4K full paint:        13.367 ms   limit 158.0 ms
4K one-row damage:     0.933 ms   limit  12.0 ms
100k table scroll:     8.112 ms   limit  20.0 ms
```

Every ceiling held. The machine is faster than the archived gate round, but the
shape is the same: the damage path is sub-millisecond to a few milliseconds and
the 4K full frame is far below its derived guard. The 4K full-frame guard
ceiling is derived from the same run's dense frame (`dense paint x 14 + 30 ms`,
158.0 ms in this run), so it is a regression guard, not a latency promise. The
normal update path is damage, and the documented target is damage-driven or
static 4K content.

No current measurement shows a missed frame budget on the documented target.
A GPU backend would need to preserve the `pt_paint_*` canvas contract, pixel
parity, the software fallback, and a deterministic test path, and it would
introduce driver/context/shader-compilation risk. There is no requirement yet
for continuous full-window 4K animation. The single-digit "4K paint ~9.9 ms"
figure is not a recorded 4K full-paint value; the closest fresh number is the
dense 1280x800 frame at 9.142 ms, while the fresh 4K full frame is 13.367 ms and
the 4K **damage** path is 0.933 ms. The deferral rests on the damage path, the
unchanged guard contract and the absence of a product requirement, not on a
single-digit full-frame number.

**Decision: defer.** Re-open with a concrete continuous-animation requirement,
a repeatable measurement that misses its budget, and an agreed parity/fallback
test plan.

### 5. Standalone theme editor - defer to a tools track

The theme is a core enum of **38 roles** and **10 metrics**
(`api/pt.api` `pt_theme_token_count = 38`,
`pt_theme_metric_count = 10`), with defaults in `src/app.c` and resolution in
`src/theme.c`. `docs/theming.md:265-280` states there is no theme file parser or
visual editor and that a theme is code that calls the setters. An editor that
exports C/JSON and runs WCAG checks needs a serialization format, a parser, a
schema/versioning story, and a UI that previews both schemes without altering
the core ABI. That is a separate tool with its own tests, not a protocol
extension.

**Decision: defer to a standalone `tools/theme-editor` proposal.** It must stay
outside `libpt.so`; it can consume the token names/defaults and the existing
contrast formula, but it must not add a theme parser to the core.

### 6. xdg-foreign v1 - reject

Both v1 and v2 exist in the installed tree, but v1 is the deprecated spelling
and niri 26.04 exposes only v2. Supporting both would double the object
lifecycle and the test matrix for no user-visible gain. M26 vendors, binds and
tests **v2 only**. A compositor that exposes only v1 simply falls back to the
no-op path, exactly like a compositor with no xdg-foreign global.

## Proposed API

The declarations append to `include/pt/window.h`; no existing enum value,
struct field or signature moves. The functions are implemented in new feature
modules, not in `src/window.c` (the `dnd.c`/`interaction.c`/`icon.c` precedent).

### Activation

```c
/* Since 2.4. */
typedef void (*pt_activation_token_callback)(pt_window *window, const char *token,
                                             void *user_data);

/* Asks the compositor for an activation token that can be passed to another
 * client. app_id may be NULL; when non-NULL it is the app_id of the client
 * that will be activated. The callback runs once on the UI thread with a token
 * valid only during the call; an empty string means the compositor returned an
 * invalid token. Returns false without creating an object when the window is
 * NULL, the callback is NULL, the activation global is absent, or headless. */
pt_api bool pt_window_request_activation_token(pt_window *window, const char *app_id,
                                               pt_activation_token_callback callback,
                                               void *user_data);

/* Requests activation of this window with a token received out of band.
 * Returns false for NULL/empty tokens, no activation global, or headless. A
 * true return means the request was sent, not that the compositor honoured
 * it. The token is not retained. */
pt_api bool pt_window_activate(pt_window *window, const char *token);
```

Startup behavior is automatic and has no extra function: when
`xdg_activation_v1` is bound, `pt_platform_start` copies
`XDG_ACTIVATION_TOKEN` into the platform and unsets it. The first window that
reaches the mapped state consumes the token once with `activate`. If the global
is absent, the environment variable is neither read nor unset, so an
unsupported compositor sees no behavioral change at all.

### Foreign surfaces

```c
/* Since 2.4. */
typedef void (*pt_foreign_handle_callback)(pt_window *window, const char *handle,
                                           void *user_data);

/* Exports this toplevel and delivers its opaque handle once. The handle is
 * valid only while the export is alive; the callback string is valid only
 * during the call. Re-exporting first destroys the previous export and drops
 * its pending callback. Returns false when the exporter global is absent,
 * headless, or the window is NULL. */
pt_api bool pt_window_export_foreign_handle(pt_window *window,
                                            pt_foreign_handle_callback callback,
                                            void *user_data);

/* Revokes the export. Any imported relationship built from its handle is
 * invalidated by the compositor. No-op when nothing is exported. */
pt_api void pt_window_unexport_foreign_handle(pt_window *window);

/* Imports an opaque handle and sets this window as its child (the same
 * stacking/positioning semantics as xdg_toplevel.set_parent). Replaces a
 * previous imported parent. Returns false for NULL/empty handles, no importer
 * global, headless, or a NULL window. An invalid handle is cleared silently
 * when the compositor sends destroyed. */
pt_api bool pt_window_set_foreign_parent(pt_window *window, const char *handle);

/* Destroys the imported parent object and invalidates the relationship.
 * No-op when no parent is imported. */
pt_api void pt_window_clear_foreign_parent(pt_window *window);
```

### Naming and convention review

- `request` is used where the compositor may decline (`request_activation_token`,
  `activate`); the docs say "request", not "focus".
- The callback parameter order matches `pt_focus_callback` and the other window
  callbacks: `(window, value, user_data)`.
- The zero value convention is preserved: there is no new enum, so no zero
  value question arises. `false` is the unavailable/failed path.
- `const char *` inputs are read synchronously and copied where ownership is
  needed; no API returns caller-owned memory. Callback strings are borrowed for
  the call, consistent with the key-event `utf8` rule.
- The name `pt_window_activate` cannot be confused with widget activation
  (`pt_widget_activate`) because the prefix and parameter types differ.

### Baseline arithmetic

| Category | 2.3.0 | M26 | Delta |
| --- | ---: | ---: | ---: |
| functions | 581 | 587 | +6 |
| callbacks | 43 | 45 | +2 |
| enums | 305 | 305 | 0 |
| structs | 12 | 12 | 0 |
| defines/opaque/type | 19 | 19 | 0 |
| **entries** | **960** | **968** | **+8** |

No enum or public struct is added, so `tests/test_abi.c` needs no new static
assertion beyond the existing append-only checks. `make api-update` is run once
in W1 after the stubs exist.

## Integration design

### New platform state

`src/wayland/wayland_internal.h` gains, behind the existing includes:

```c
/* in struct pt_wayland_window */
struct xdg_activation_token_v1 *activation_token;
pt_activation_token_callback activation_token_callback;
void *activation_token_data;
struct zxdg_exported_v2 *foreign_exported;
pt_foreign_handle_callback foreign_export_callback;
void *foreign_export_data;
struct zxdg_imported_v2 *foreign_parent;

/* in struct pt_platform */
struct xdg_activation_v1 *activation;
struct zxdg_exporter_v2 *foreign_exporter;
struct zxdg_importer_v2 *foreign_importer;
char *pending_activation_token;
bool pending_activation_consumed;
```

All pointers are zero on allocation (`pt_alloc_zeroed` for the platform and the
window record), so the absent-global path needs no initialization branch.

### Activation flow

1. `registry_global` binds `xdg_activation_v1` at version 1 when advertised.
2. After the first registry roundtrip, `pt_platform_start` calls
   `pt_activation_adopt_environment`. It reads and unsets
   `XDG_ACTIVATION_TOKEN` only when `platform->activation != NULL`.
3. `pt_window_request_activation_token` creates a token object on the window.
   It calls `set_serial(platform->button_serial ?: key_serial, platform->seat)`
   when a serial exists, `set_app_id(app_id)` when non-empty, `set_surface` with
   the window's `wl_surface`, and `commit`.
4. The `done` listener copies the token, clears the window fields, destroys the
   token object, then invokes the callback with the copy and frees it. Clearing
   before the callback makes re-entrant requests safe.
5. The first successful `pt_wl_window_draw` commit calls
   `pt_activation_window_mapped(window)`. If a pending startup token exists and
   has not been consumed, the hook sends `activate(token, surface)`, frees the
   token, and sets the consumed flag. The token is consumed exactly once,
   regardless of which window maps first.
6. `pt_window_activate` is a direct request with the caller's token. It does not
   consume the startup token; a caller that already handles startup explicitly
   can still call it.
7. Window destruction destroys any pending token object and drops its callback;
   platform destruction frees the pending string and destroys the global.

### Foreign flow

1. `registry_global` binds `zxdg_exporter_v2` and `zxdg_importer_v2`
   independently. One may be present without the other; the corresponding API
   returns false.
2. Export creates `zxdg_exported_v2` for the window's `wl_surface`, adds the
   `handle` listener, and keeps the object alive. The handle callback delivers a
   copy; the export remains alive until unexport or window destruction.
3. Re-export destroys the old exported object first (invalidating old handles),
   drops the old callback, then creates a new object.
4. Import destroys any previous imported object, creates a new
   `zxdg_imported_v2`, adds the `destroyed` listener, and immediately sends
   `set_parent_of(window->surface)`. The imported object is kept alive.
5. `destroyed` destroys the imported object and clears the field. A later
   `set_foreign_parent` can create a new import.
6. Window destruction destroys exported and imported objects; platform
   destruction destroys the exporter and importer globals.

No paint, layout, hit-testing, accessibility, LTR/RTL or serial behavior is
touched. The protocols change toplevel relationships and focus requests only.

### Core/platform split and file ownership

The public functions are thin wrappers in `src/activation.c` and
`src/foreign.c`; the Wayland protocol code is in `src/wayland/activation.c` and
`src/wayland/foreign.c`. The hooks are declared in `src/internal.h` and
`src/wayland/wayland_internal.h` in W1:

```c
bool pt_platform_activation_token_request(pt_window *window, const char *app_id,
                                          pt_activation_token_callback callback,
                                          void *user_data);
bool pt_platform_activation_activate(pt_window *window, const char *token);
void pt_activation_adopt_environment(pt_platform *platform);
void pt_activation_window_mapped(pt_window *window);
void pt_activation_window_destroy(pt_window *window);
void pt_activation_platform_destroy(pt_platform *platform);

bool pt_platform_foreign_export(pt_window *window, pt_foreign_handle_callback callback,
                                void *user_data);
void pt_platform_foreign_unexport(pt_window *window);
bool pt_platform_foreign_set_parent(pt_window *window, const char *handle);
void pt_platform_foreign_clear_parent(pt_window *window);
void pt_foreign_window_destroy(pt_window *window);
void pt_foreign_platform_destroy(pt_platform *platform);
```

W1 ships these as stubs so the headers, baseline and build are coherent before
the protocol waves start. W3 replaces the activation stubs, W4 the foreign
stubs, and W5 performs the two edits in `platform.c` and `wayland/window.c`
that call the hooks.

## Zero-regression argument

1. **Absent globals.** Every new object pointer is NULL. Public functions return
   false or no-op before creating any protocol object. The fake-compositor
   fallback tests assert zero new requests.
2. **Environment.** `XDG_ACTIVATION_TOKEN` is read and unset only when
   `xdg_activation_v1` is bound. On an unsupported compositor the process
   environment is unchanged.
3. **No rendering changes.** New code never calls `pt_window_damage_*`,
   `pt_widget_damage`, layout or canvas APIs. Pixels, damage tiles and
   snapshots are byte-identical at the same size, scale and text. LTR and RTL
   are both unaffected because no direction-dependent path is reached.
4. **No enum/struct movement.** The 305 enum entries, 12 struct layouts and 19
   defines/opaque/type entries are unchanged. `tests/test_abi.c` and the ABI
   audit see only six appended symbols and two callback typedefs.
5. **No new serial semantics.** Activation reuses the existing
   button/key serial rule; it never changes the primary-selection or clipboard
   serials. Foreign surfaces do not consume input serials.
6. **No behavior change when the API is unused.** On a compositor that exposes
   the globals, no request is sent unless an app calls the new API or a launcher
   has set the startup environment token. The startup token is the one intended
   observable behavior change, and it is the reason the protocol exists.

The release gate repeats the existing pixel gates:
`tools/ltr-compat.sh 1.4.0 HEAD` and `tools/ltr-compat.sh 2.2.0 HEAD` must both
remain `11/11 SAME`, and existing snapshot tests must be unchanged.

## Test matrix

### Fake compositor extension

`tests/fake_compositor.h` gains two opt-in flags, appended after the existing
bits so `FC_OPT_ALL` and every old value stay stable:

```c
#define FC_OPT_ACTIVATION (1u << 11)
#define FC_OPT_FOREIGN    (1u << 12)
```

`FC_OPT_ALL` deliberately does not include them, matching the touch/tablet
rule. Six interface kinds append after `FC_K_TABLET_TOOL`:

```text
FC_K_ACTIVATION, FC_K_ACTIVATION_TOKEN,
FC_K_EXPORTER, FC_K_EXPORTED, FC_K_IMPORTER, FC_K_IMPORTED
```

The request/event tables mirror the XML exactly:

| Interface | Requests (opcode) | Events |
| --- | --- | --- |
| `xdg_activation_v1` | destroy 0, get_activation_token 1, activate 2 | none |
| `xdg_activation_token_v1` | set_serial 0, set_app_id 1, set_surface 2, commit 3, destroy 4 | done 0 |
| `zxdg_exporter_v2` | destroy 0, export_toplevel 1 | none |
| `zxdg_exported_v2` | destroy 0 | handle 0 |
| `zxdg_importer_v2` | destroy 0, import_toplevel 1 | none |
| `zxdg_imported_v2` | destroy 0, set_parent_of 1 | destroyed 0 |

New helpers inject `done(token)`, `handle(handle)`, and `destroyed()`, and the
existing request log asserts the exact token/app_id/serial/surface/handle
arguments. `tests/test_protocol_wire.c` is the facility test: it checks the
appended kind numbers, the table opcodes, the optional-global behavior and that
old `FC_OPT_ALL` consumers are unaffected.

### Activation cases (`tests/test_activation.c`)

| Case | Expected |
| --- | --- |
| No global / headless | `request_activation_token` and `activate` return false; callback not called; zero activation requests |
| Token request | `get_activation_token`, `set_serial` (after a pointer/key event), `set_surface`, `commit`; `done` delivers the token once; token object destroyed |
| No recent serial | `get_activation_token`, `set_surface`, `commit`; no `set_serial` |
| `app_id` NULL vs non-empty | no `set_app_id` vs the exact string |
| Empty/invalid token | callback receives `""`; `pt_window_activate(window, "")` returns false and sends nothing |
| Explicit activate | `activate(token, surface)` exactly once; true return |
| Startup env, global present | env read and unset at start; no request before map; first mapped frame sends `activate` once; a second window does not repeat |
| Startup env, global absent | env untouched; no request; pixels unchanged |
| Window destroyed with pending token | no callback; token object destroyed; no use-after-free |
| Re-entrant request in callback | new token object is independent; no clobbered callback |

### Foreign cases (`tests/test_foreign.c`)

| Case | Expected |
| --- | --- |
| No exporter / headless | export returns false; callback not called; zero requests |
| No importer / headless | set parent returns false; zero requests |
| Export | `exporter.export_toplevel(surface)`; injected `handle` delivers once; object stays alive |
| Unexport | `exported.destroy`; no later handle; second unexport is a no-op |
| Re-export | old `exported.destroy` precedes the new `export_toplevel`; old callback dropped |
| Import | `importer.import_toplevel(handle)` then `imported.set_parent_of(window surface)`; true |
| Replace import | old `imported.destroy` before the new import |
| Invalid handle | injected `destroyed`; state cleared; no crash; a later import works |
| Window destroy | exported/imported objects destroyed; pending callbacks dropped |
| Opaque handle | the exact string is passed through; no parsing, no logging of its contents |

### Live niri cases (`tests/test_wayland.c`)

The live test reads the globals first and skips the assertions when a global is
absent:

- Activation: create/show a window, request a token with the app_id, wait for
  the callback, assert a non-empty token, call `pt_window_activate`, and assert
  the connection still dispatches (no protocol error).
- Foreign: export a window handle, assert non-empty, import it on a second
  window, `set_parent_of`, dispatch, clear both, and assert both windows are
  still open. The same-client sequence is the one verified by the research
  probe; if a future compositor rejects it, the test records the failure rather
  than hanging.
- Fallback: if either global is missing, the same calls return false and the
  test passes without sending requests.

The live suite runs under `tools/nested-wayland.sh --fullscreen`, which was
verified to expose both globals in this research wave.

### Binding cases

- C++: `window::request_activation_token`, `activate`,
  `export_foreign_handle`, `unexport_foreign_handle`,
  `set_foreign_parent`, `clear_foreign_parent`; callback trampolines keep the
  `user_data` convention and never retain the borrowed token.
- Python: ctypes callback types for both strings; wrappers append the trampoline
  to the callback roots to prevent GC; tests cover false on headless and a fake
  wire token.
- Java: Panama upcalls decode the native string with `nativeText`; wrappers
  cover the same six methods and callback lifetime.
- Parity must be **587/587** in all three languages; no allowlist entries.

### Regression and gate checks

- `make -j2 check` (all existing tests + three new test binaries) green.
- `make -j2 check-bindings` green; C++/Python/Java parity 587.
- `gen-api-reference.py --check` and `gen-binding-stubs.py --check --strict`
  green.
- `tools/ltr-compat.sh 1.4.0 HEAD` and `2.2.0 HEAD`: 11/11 `SAME`.
- ABI diff: 581 -> 587, 0 removed, 0 changed, 6 added; SONAME `libpt.so.1`.
- Serial-only sanitizer run (`make check-sanitize`) with `-j2` builds and
  serial test execution; no leak from token/exported/imported objects.
- `tools/nested-wayland.sh --fullscreen -- build/tests/test_wayland` green.
- `docs/compositor-matrix.md` rows and `docs/m26-design.md` status updated.
- `git status` clean at the release tag.

## Parallel wave decomposition

File ownership is exclusive while a wave is active. `platform.c` and
`wayland/window.c` are the serial hot spot and have one owner (W5). The
Makefile is edited sequentially: W1 owns the protocol/test lines, W7 owns the
example target after W1 is merged, W8 owns `VERSION` after W7.

| Wave | Scope | Files owned | Depends on | Runs in parallel with | Owner |
| --- | --- | --- | --- | --- | --- |
| W0 | Proposal, evidence, API, wave plan | `docs/ROADMAP.md`, `docs/m26-design.md` | - | - | scaffold |
| W1 | Vendor XML, scanner rules, headers, core/platform stubs, baseline 587/968, test stubs | `protocols/xdg-activation-v1.xml` (new), `protocols/xdg-foreign-unstable-v2.xml` (new), `Makefile` (PROTO_NAMES + TEST_NAMES), `include/pt/window.h`, `src/internal.h`, `src/wayland/wayland_internal.h`, `src/activation.c` (new), `src/foreign.c` (new), `src/wayland/activation.c` (new), `src/wayland/foreign.c` (new), `tests/test_protocol_wire.c` (new), `tests/test_activation.c` (new), `tests/test_foreign.c` (new), `api/pt.api`, `docs/api-reference.md`, `THIRD_PARTY.md`, `NOTICE` | W0 | none | DuMate |
| W2 | Fake compositor protocol tables, globals, injection helpers, facility test | `tests/fake_compositor.h`, `tests/test_protocol_wire.c` | W1 | none (blocks W3/W4) | WB-CN (DuMate reviews) |
| W3 | Activation public + Wayland core + fallback/wire tests | `src/activation.c`, `src/wayland/activation.c`, `tests/test_activation.c` | W2 | W4 | DuMate |
| W4 | Foreign public + Wayland core + fallback/wire tests | `src/foreign.c`, `src/wayland/foreign.c`, `tests/test_foreign.c` | W2 | W3 | Loomy (single agent) |
| W5 | Registry bind/destroy, startup env adoption, first-map hook, live test, matrix rows | `src/wayland/platform.c`, `src/wayland/window.c`, `tests/test_wayland.c`, `docs/compositor-matrix.md` | W3, W4 | none (serial hot spot) | DuMate |
| W6a | C++ wrappers and tests | `bindings/cpp/pt/pt.hpp`, `bindings/cpp/test_pt.cpp` | W5 | W6b, W7 | Q-CN |
| W6b | Python and Java wrappers and tests | `bindings/python/pt.py`, `bindings/python/test_pt.py`, `bindings/java/src/pt/Pt.java`, `bindings/java/test/pt/PtTest.java` | W5 | W6a, W7 | WB-CN |
| W7 | Guides, README, examples, bilingual pairs, example target | `README.md`, `README.zh-CN.md`, `docs/quickstart.md` + zh, `docs/architecture.md` + zh, `docs/bindings.md` + zh, `examples/` (new files), `Makefile` (example target only) | W5 | W6a, W6b | Lobster |
| W8 | Merge, full gate, 2.4.0 version/release record, final ROADMAP/design/matrix status | `include/pt/base.h` (version macros), `Makefile` (`VERSION`), `docs/release.md`, `docs/ROADMAP.md`, `docs/m26-design.md`, `docs/compositor-matrix.md` (final status) | W1-W7 | none | Lobster + DuMate |

### Dependency graph and merge strategy

```text
W0 -> W1 -> W2 -> { W3 || W4 } -> W5 -> { W6a || W6b || W7 } -> W8
```

Each wave uses its own worktree and branch (`m26/w1-foundation`,
`m26/w2-fake`, `m26/w3-activation`, `m26/w4-foreign`, `m26/w5-integration`,
`m26/w6a-cpp`, `m26/w6b-python-java`, `m26/w7-docs`, `m26/w8-gate`). Reviewers
merge explicit tips with `git merge --no-ff <tip>` into `m26/integration`; no
wave pushes to `main` or tags. W3 and W4 must be rebased on the merged W2 tip
before review. W5 merges W3 and W4 first, then edits `platform.c` and
`wayland/window.c` once. W6a/W6b/W7 branch from the W5 tip.

### Serial domains and why the split is safe

- **Foundation headers:** W1 is the only wave that edits `include/pt/window.h`,
  `src/internal.h` and `src/wayland/wayland_internal.h`. All fields and hook
  prototypes exist before W3/W4 start, so the two protocol waves never touch a
  shared header.
- **Protocol cores:** W3 owns only activation files; W4 owns only foreign files.
  They share the fake compositor (W2, already merged) but not source files.
- **Registry and mapping:** W5 is the only wave that edits `platform.c` and
  `wayland/window.c`. No two active agents can collide on the protocol serial
  hot spot.
- **Bindings:** C++ is a single file/target; Python+Java are two files plus
  tests. They do not overlap and do not edit the C library.
- **Docs/examples:** W7 owns the public guides, README and examples. The new
  internal design page is not a bilingual pair, and W7 adds protocol text to
  existing paired guides rather than creating an eleventh pair. The bilingual
  checker therefore stays at 10 pairs.
- **Baseline:** `api/pt.api` and `docs/api-reference.md` are W1-only. W3/W4 add
  no declarations, so they never regenerate the baseline. If W4 is dropped at
  the W5 gate, W8 runs `make api-update` once to the activation-only baseline
  (583/963) and records the decision.

### Review strategy

Every wave is independently reviewed PASS before its dependants merge: W1 by
Lobster, W2 by DuMate, W3 by Lobster, W4 by DuMate, W5 by Lobster + Q-CN, W6a
by DuMate, W6b by Q-CN, W7 by DuMate, W8 by Lobster + DuMate. Reviewers check
the exact diff plus the wave's own gate; a wave that cannot pass is split or
reverted, never merged partially. The W5 review is the scope gate for foreign:
if the live import/parent path fails or produces a protocol error, W4 is
dropped and the activation-only fallback is recorded.

### Provider allocation and budget

Estimated **850-950 credits**. W0 ~60, W1 ~110, W2 ~90, W3 ~140, W4 ~140,
W5 ~100, W6a ~60, W6b ~70, W7 ~70, W8 ~80. DuMate carries W1 + W3 + W5 (about
40%, including the only serial hot spot); WB-CN carries W2 + W6b (about 20%);
Loomy runs W4 as a single agent (about 15%, no child agents); Q-CN carries W6a
(about 8%, with DuMate as backup because free concurrency is unstable);
Lobster carries W7 + W8 (about 17%). Dropping foreign at the W5 gate saves the
W4 budget and part of W8, and the release lands at 583/963.

## Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| xdg-activation is staging, not stable | certain | low | Bind v1 only; isolate in `activation.c`; document staging status; a new major version would be a new design, not a silent upgrade |
| Token is ineffective despite a successful request | medium | low | API says "request"; tests assert protocol requests and live acceptance, never visible focus; callers can fall back to their own UX |
| No recent input serial on token request | medium | medium | Omit `set_serial` and document that some compositors may decline; expose the request result, not a promise |
| Function re-entrancy or callback lifetime | low | medium | Clear window fields and copy the token before invoking the callback; callback string is borrowed only for the call; tests cover re-entrancy |
| xdg-foreign is unstable | certain | medium | v2 only, version 1, opaque handles, no parsing, isolated module, live + fake tests; scope fallback to activation only |
| Invalid handle produces `destroyed` | medium | low | Treat as a silent clear; destroy the imported object; test invalid-handle recovery; never crash |
| Re-export invalidates old handles | low | medium | Destroy old export before creating a new one; callback dropped; documented in the header |
| Protocol object leak on window/platform destroy | medium | medium | Explicit destroy in window/platform teardown; sanitizer run; fake tests assert destroy requests |
| Startup token activates the wrong window | low | medium | Consume exactly once on the first mapped window; no other window can consume it; test with two windows |
| Bindings retain borrowed token strings | medium | high | Copy into the wrapper's callback scope; append trampolines to roots; C++ keeps raw callback pointers; parity and ASan tests |
| Makefile merge conflict | low | low | Sequential ownership: W1 protocol/test lines, W7 example target, W8 version |

## Open questions and adjudications

1. **Should startup activation be automatic or explicit?**
   Adjudicated automatic, because a launcher cannot call Pt on behalf of a child
   and the environment token is only useful if the child consumes it. It is
   gated on the activation global and consumes exactly once.
2. **Should `pt_window_request_activation_token` take `app_id`?**
   Yes. `set_app_id` describes the client being activated; a launcher may know
   it, a self-activation may pass its own app id, and NULL is the documented
   omission path. A separate setter would add API without adding capability.
3. **Should xdg-foreign export return the handle synchronously?**
   No. The protocol delivers it asynchronously and the export object must stay
   alive. A synchronous wrapper would require a nested dispatch and could
   deadlock inside a callback.
4. **Should imported-parent `destroyed` be exposed as a callback?**
   No for 2.4. It is a silent invalidation; the application can re-import. A
   callback would add an ABI entry and a lifetime contract for little value.
5. **Should xdg-foreign v1 be supported for older compositors?**
   No. niri exposes only v2, v1 is deprecated, and supporting both doubles the
   lifecycle surface. A v1-only compositor gets the documented no-op fallback.
6. **Is `platform.c` still a serial hotspot?**
   Yes, but only for registry bind, teardown and the startup hook. The protocol
   logic lives in the new files; W5's edits are small and reviewed as one wave.

## Completion criteria for 2.4.0

- `api/pt.api` reads 587 / 968 (583 / 963 if xdg-foreign is dropped at W5);
  binding parity 587/587 in C++, Python and Java; `ABI_MAJOR` 1 and SONAME
  `libpt.so.1`; no existing enum value or struct layout moves.
- Activation: a token request produces the exact protocol sequence, the
  callback delivers the token, `activate` sends the request, and the absent/
  headless path returns false with zero requests. Startup env is read and
  unset only when the global is present and consumed once on the first mapped
  window.
- Foreign: export delivers an opaque handle and keeps the export alive;
  unexport/revoke works; import + `set_parent_of` works; invalid handles clear
  silently; window destruction destroys every object.
- `tools/nested-wayland.sh --fullscreen -- build/tests/test_wayland` passes with
  both globals present and with the fallback path; the matrix rows for
  xdg-activation and xdg-foreign v2 are recorded with niri live evidence.
- Text, layout, hit-testing, accessibility, LTR and RTL are byte-identical:
  `tools/ltr-compat.sh 1.4.0 HEAD` and `2.2.0 HEAD` are 11/11 `SAME`, existing
  snapshots are unchanged, and `make -j2 check` is green.
- `make -j2 check-bindings`, `gen-api-reference.py --check`,
  `gen-binding-stubs.py --check --strict`, the serial sanitizer pass, the
  staged-install consumers and the reproducible tarball are green; `git status`
  is clean.
- The deferred items (color-management, Vulkan/EGL, theme editor) remain
  recorded in ROADMAP with their evidence and a re-open trigger; no partial
  protocol code ships.

## Deferred register (for ROADMAP)

| Item | Evidence | Re-open trigger |
| --- | --- | --- |
| color-management-v1 | staging manager v3, 1,756-line XML; ARGB8888 sRGB canvas; no HDR format/output path; niri 26.04 has no global; no measured need | stable/frozen staging subset + 10/12-bit or float canvas + a matrix compositor live test |
| Vulkan/EGL | 4K full 37.657-47.808 ms; 4K damage 3.291-4.070 ms; 100k seek 11.531-14.921 of 20 ms; ceilings hold; damage is the documented target | concrete continuous 4K full-window animation requirement + a repeatable budget miss + parity/fallback plan |
| Theme editor | 38 tokens/10 metrics; no theme file parser or serialization; `docs/theming.md` limits; outside core ABI | a separate tools proposal with a versioned theme format, C/JSON export, WCAG checks and preview |
| xdg-foreign v1 | deprecated; niri only v2; adds a second lifecycle for no user gain | a real v1-only target that cannot use v2 |