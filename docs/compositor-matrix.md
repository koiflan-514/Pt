# Compositor and protocol matrix

Status: reference snapshot, updated 2026-11-27 for the M26 protocol extensions.
This page records which Wayland globals the target compositors actually expose,
at which interface version, and the behaviour that matters to Pt. It is the
evidence base for the platform-negotiation rules in [standards.md](standards.md)
section 8, the decoration/primary-selection/text-input work in ROADMAP M18, and
the xdg-activation/xdg-foreign rows added by [m26-design.md](m26-design.md).

It is an internal reference document, maintained in English only.

## Evidence grades

| Grade | Meaning | Typical source |
| --- | --- | --- |
| A | protocol/standard text or official API documentation | wayland-protocols XML mirror, W3C, ETSI, docs.gtk.org |
| B | official release notes / changelog | project NEWS, Qt Releases, LVGL changelog |
| C | aggregator, secondary report, community discussion | wayland.app support matrix, Phoronix, issue threads |
| ? | not confirmable from a primary source | marked "to verify" below |

wayland.app's compositor matrix is auto-generated from each project's reported
support: it answers "who exposes the global", not "what a specific commit does".
Behavioural claims are graded separately and the uncertain ones are listed in
the appendix rather than stated as fact.

## Interface versions by compositor

Values are the interface version the compositor reports; `—` means the global is
absent (wayland.app records `x`). Source: the Compositor Support table on each
protocol page (grade C).

| Compositor (version) | xdg-decoration | text-input-v3 | primary-selection | fractional-scale | pointer-gestures | color-management | toplevel-icon | xdg-activation | xdg-foreign v2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Mutter 51 (GNOME) | — | 2 | 1 | 1 | 3 | 2 | — | 1 | 1 |
| KWin 6.7 (Plasma) | 2 | 2 | 1 | 1 | 3 | 2 | 1 | 1 | 1 |
| niri 26.04 | 1 | 1 | 1 | 1 | 3 | — | — | 1 | 1 |
| Sway 1.11 (wlroots) | 1 | 1 | 1 | 1 | 3 | — | — | 1 | 1 |
| Hyprland 0.57.2 | 2 | 1 | 1 | 1 | 3 | 3 | — | 1 | 1 |
| COSMIC 1.11.0 | 1 | 1 | 1 | 1 | 3 | — | — | 1 | 1 |
| Weston 15.0.1 | — | — | — | — | — | — | — | — | — |
| Muffin 6.7.4 (Cinnamon) | — | 1 | 1 | — | 3 | — | 1 | 1 | 1 |

The last two columns are M26 additions. The niri row is grade B for both: the
nested niri 26.04 registry advertised `xdg_activation_v1` v1,
`zxdg_exporter_v2` v1 and `zxdg_importer_v2` v1, and the live client exercised a
token request/activate and an export/import/set_parent_of roundtrip. Every other
row is grade C from the public matrix.

## Reading the matrix

- **Decoration.** Only KWin/Hyprland (and Jay) expose v2 among mainstream
  desktops; niri/Sway/COSMIC are v1. v1 does not mean "CSD only": niri negotiates
  SSD on request but draws a focus ring/border, not a title bar; Sway defaults
  tiled windows to `server_side` (a CSD request may be overridden) and honours
  the request only for floating windows. Neither draws a traditional title bar.
  Mutter and Weston lack the global, so the client must draw CSD. Weston's
  desktop-shell historically adds its own border around CSD clients (double
  decoration risk); 15.0.1 behaviour is to verify.
- **Text input.** Mutter/KWin are v2; niri/Sway/Hyprland are still v1. v2 adds
  `action` (none/submit) and `preedit_hint` (byte ranges within the preedit).
  Pt must not assume "modern desktop = v2".
- **Primary selection.** Present on essentially all targets except
  Weston/GameScope/Louvre, so middle-click paste can be a default 1.1 behaviour
  rather than an optional extra.
- **Fractional scale.** Mutter/KWin/niri/Sway all support it; Muffin/Weston do
  not, so the integer-scale fallback stays.
- **Window icon.** Only KWin/Labwc/Muffin/Jay/Pila expose `xdg-toplevel-icon-v1`;
  Mutter/niri/Sway do not. Window icons are best-effort: use the global when
  present, otherwise rely on `app_id` + `.desktop`.
- **Activation and foreign.** Both M26 protocols are best-effort optional
  globals. `xdg_activation_v1` describes a request, never a guarantee of focus;
  a compositor may ignore a stale token. `zxdg_exporter_v2`/`zxdg_importer_v2`
  express only a toplevel parent/child relationship and never embed pixels.
  On a compositor without either global Pt binds nothing and every new call
  returns false or no-ops silently, so the fallback is a behavioural downgrade,
  not an error. Weston lacks both in this snapshot.
- **Colour management.** niri 26.04 does not advertise
  `wp_color_manager_v1`; it is deferred in M26 and is not a capability Pt
  assumes, including on the other rows recorded as `—`.

## Protocol semantics that bind Pt

| Protocol | State / version | Semantics Pt must honour | Source |
| --- | --- | --- | --- |
| `xdg-shell` | stable, `xdg_wm_base` v7 | v7 adds `constrained_left/right/top/bottom`; `suspended` since 1.32; unknown states must be ignored; `configure_bounds`/`wm_capabilities` are usable for size/button policy | [xdg-shell](https://wayland.app/protocols/xdg-shell), [wayland-protocols 1.43](https://lists.freedesktop.org/archives/wayland-devel/2025-April/044134.html) |
| `xdg-decoration-unstable-v1` | unstable, manager v2 | `set_mode` is a preference; `configure` is authoritative; v1 requires decoration before buffer attach, v2 allows after | [xdg-decoration](https://wayland.app/protocols/xdg-decoration-unstable-v1) |
| `fractional-scale-v1` | staging | `preferred_scale` is a fraction over 120; client rounds to buffer size | [fractional-scale](https://wayland.app/protocols/fractional-scale-v1) |
| `viewporter` | stable | decouples logical size from buffer size | [viewporter](https://wayland.app/protocols/viewporter) |
| `text-input-unstable-v3` | unstable, `zwp_text_input_v3` v2 | double-buffered state; `commit` then `done` confirms; enter/disable invalidate state; `delete_surrounding_text` lengths are UTF-8 bytes on codepoint boundaries; v2 adds `action`/`preedit_hint` | [text-input-v3](https://wayland.app/protocols/text-input-unstable-v3) |
| `primary-selection-unstable-v1` | unstable v1 | `set_selection(source, serial)` serial must come from the triggering event; only the keyboard-focused client receives a non-NULL offer | [primary-selection](https://wayland.app/protocols/primary-selection-unstable-v1) |
| `pointer-gestures-unstable-v1` | unstable v3 | swipe/pinch/hold three-phase; hold has only begin/end | [pointer-gestures](https://wayland.app/protocols/pointer-gestures-unstable-v1) |
| `tablet-v2` | stable | tool/contact/pressure separated; eraser and dial | [tablet-v2](https://wayland.app/protocols/tablet-unstable-v2) |
| `color-management-v1` | staging, manager v2/v3 | surface image description; HDR/tonemapping incomplete; 1.x targets sRGB correctness only | [color-management](https://wayland.app/protocols/color-management-v1) |
| `xdg-toplevel-icon-v1` | staging v1 | square `wl_shm` buffer per size/scale; buffer must stay alive and unmodified; cannot reuse a frame buffer | [toplevel-icon](https://wayland.app/protocols/xdg-toplevel-icon-v1) |
| `xdg-activation-v1` | staging, `xdg_activation_v1` v1 | token request is async (`get_activation_token` + `commit` then `done`); attach the latest input serial when one exists; `activate` is a request, not a focus guarantee; absent global = false/no-op; startup token is read and unset only when bound | [xdg-activation](https://wayland.app/protocols/xdg-activation-v1) |
| `xdg-foreign-unstable-v2` | unstable, exporter/importer v2 | handle is opaque and valid only while the export lives; export/import are separate optional globals; `set_parent_of` gives xdg-toplevel parent semantics, not embedding; absent global = false/no-op | [xdg-foreign-v2](https://wayland.app/protocols/xdg-foreign-unstable-v2) |

## Quirk register

Each quirk has an id referenced by the code comment and the test name. New
quirks are added here before the workaround lands.

| Id | Area | Behaviour | Pt response | Grade |
| --- | --- | --- | --- | --- |
| Q-DEC-001 | decoration | Sway forces `server_side` on tiled windows even when CSD is requested | track requested vs effective mode; test "requested CSD, received SSD"; drop radius/shadow when tiled | C ([sway/xdg_decoration.c](https://github.com/swaywm/sway/blob/master/sway/xdg_decoration.c)) |
| Q-DEC-002 | decoration | niri SSD draws border/focus ring, no title bar; honours explicit CSD | never promise a unified title bar; content area always correct, header ownership explainable | C ([niri config](https://niri-wm.github.io/niri/Configuration%3A-Miscellaneous.html)) |
| Q-DEC-003 | decoration | Weston desktop-shell may add a border around CSD clients (double decoration) | offer `set_decorated(false)` escape; record 15.0.1 behaviour after live smoke | ? ([Weston #546](https://gitlab.freedesktop.org/wayland/weston/-/issues/546)) |
| Q-IME-001 | text input | KWin/fcitx5 focus desync can drop `commit_string` | resend full state on every change; keep a local composition buffer cleared only on commit; treat empty `done` as ack | ? ([KWin work item #306](https://invent.kde.org/plasma/kwin/-/work_items/306)) |
| Q-IME-002 | text input | sway/Chromium focus resync issues | same full-state resend strategy; do not depend on a root-cause fix | C ([sway #8276](https://github.com/swaywm/sway/issues/8276)) |
| Q-IME-003 | text input | GNOME routes fcitx5 through the IBus frontend; candidate popup behaviour is compositor-limited | document the path; do not assume a native fcitx5 popup on Mutter | C ([fcitx5 Wayland wiki](https://www.fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)) |
| Q-PRI-001 | primary selection | a single shared "last event" serial is not the triggering-event serial | split enter/pointer-button/key-press serials; programmatic selection without a trigger does not send | A ([primary-selection](https://wayland.app/protocols/primary-selection-unstable-v1)) |

## To verify

These are not stated as conclusions anywhere in the code or docs until confirmed.

| Item | Current understanding | Action |
| --- | --- | --- |
| `xdg-decoration` v3 / `server_side_border` | the public XML on wayland.app is still interface v2; a v3 proposal is discussed in the community | do not depend on it in 1.1; reassess after wayland-protocols merges it |
| EN 301 549 V4.1.1 OJEU citation | standard published 2026-09, aligned with WCAG 2.2 AA; harmonised citation unconfirmed | keep V3.2.1 as the legal baseline, V4.1.1 as the target |
| KWin/fcitx5 `commit_string` loss trigger | work item and community reports exist; not reproduced step by step | 1.1 uses full-state resend + local buffer, independent of root cause |
| niri SSD exact drawing | docs say focus ring/border, no solid background, honours explicit CSD; whether a title bar is ever drawn is untested | live-record the `configure` mode and a screenshot into this matrix |
| Weston 15 CSD double border | historical issue exists; 15.0.1 behaviour untested | live smoke; after the `set_decorated` split, document the `false` escape |
| colour emoji on the FreeType software path | depends on CBDT/sbix/COLR support and the font | spike before 2.0, then decide whether to promise it |

## Sources

Protocol pages are generated by wayland.app from the wayland-protocols XML.
Compositor behaviour references: [niri configuration](https://niri-wm.github.io/niri/Configuration%3A-Miscellaneous.html),
[sway xdg_decoration.c](https://github.com/swaywm/sway/blob/master/sway/xdg_decoration.c),
[Weston #546](https://gitlab.freedesktop.org/wayland/weston/-/issues/546),
[KWin work item #306](https://invent.kde.org/plasma/kwin/-/work_items/306),
[sway #8276](https://github.com/swaywm/sway/issues/8276),
[fcitx5 Wayland wiki](https://www.fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland).
Standards: [WCAG 2.2](https://www.w3.org/TR/WCAG22/),
[EN 301 549 V4.1.1](https://www.etsi.org/deliver/etsi_en/301500_301599/301549/04.01.01_60/en_301549v040101p.pdf),
[EAA Directive (EU) 2019/882](https://eur-lex.europa.eu/eli/dir/2019/882/oj).
Release cadence: [wayland-protocols releases](https://wayland.freedesktop.org/releases.html).
