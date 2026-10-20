# Theming

English | [简体中文](zh-CN/theming.md)

Pt paints from a semantic token table rather than from per-widget literals.
A theme is 38 color roles and 10 geometric metrics. Applications change the
table at two levels:

- **App level:** `pt_app_set_theme_color` / `pt_app_set_theme_metric` affect
  every window created from that app.
- **Window level:** `pt_window_set_theme_color` /
  `pt_window_set_theme_metric` override individual roles for one window and
  `pt_window_clear_theme_overrides` returns it to the app theme.

The enums are append-only ABI. Existing numeric values never change; new roles
get fresh numbers. `tests/test_abi.c` pins the values, and
[tests/test_theme.c](../tests/test_theme.c) pins the defaults and the contrast
floors quoted below.

## Resolution order

Every built-in paint resolves a color in this order:

1. the window's override for that token, if `pt_window_set_theme_color` set it;
2. otherwise the app palette, changed by `pt_app_set_color_scheme`,
   `pt_app_set_accent_color` or `pt_app_set_theme_color`;
3. otherwise the compiled default. A resolved window always has an app palette,
   so the compiled default is only a fallback for a missing window.

Metrics use the same order:

1. the window's override;
2. the app metric;
3. the compiled default.

The public read side is `pt_app_get_theme_color`,
`pt_window_get_theme_color`, `pt_app_get_theme_metric` and
`pt_window_get_theme_metric`. There is no public per-widget token getter; a
custom paint uses the app or window getter that owns the widget, or accepts the
resolved colors as input from the application.

A widget can still pin an explicit background or foreground color; that widget
bypasses the token table for the pinned role. Theme changes request a redraw of
every open window, and a metric change also invalidates layout.

Two calls are easy to misread:

- `pt_app_set_color_scheme(app, scheme)` loads the complete dark or light
  palette and **replaces every app color and metric override**. Call it before
  applying brand colors, or re-apply them afterwards.
- `pt_app_set_accent_color(app, color)` rewrites the accent family only
  (`accent`, `accent_hover`, `accent_pressed`, `accent_text`, `accent_soft`,
  `focus`, `selection`). It does not change danger, success or warning. The
  derivation depends on the active scheme, so set the scheme first.

## Color tokens

Colors are `0xRRGGBBAA`. The alpha byte is meaningful: `accent_soft`,
`selection`, the soft status roles, `shadow`, `scrim` and the selection fill are
translucent and are composited by the renderer.

| Token | Dark | Light | Use |
| --- | --- | --- | --- |
| `pt_theme_background` | `0B0E14FF` | `F4F6FAFF` | Window backdrop behind cards and panes |
| `pt_theme_surface` | `11151EFF` | `FFFFFFFF` | Default widget and card surface |
| `pt_theme_surface_raised` | `161B26FF` | `FFFFFFFF` | Menus, dialogs, raised cards |
| `pt_theme_surface_hover` | `1D2431FF` | `F0F3F8FF` | Hover fill for low-emphasis surfaces |
| `pt_theme_surface_sunken` | `080B11FF` | `EDF0F5FF` | Recessed wells and code/log backgrounds |
| `pt_theme_surface_container` | `141924FF` | `EDF0F5FF` | Container tier for grouped content |
| `pt_theme_surface_container_high` | `1A2130FF` | `E5EAF2FF` | Highest container tier, input chrome |
| `pt_theme_border` | `232B3AFF` | `E1E6EEFF` | Hairline separators and default outlines |
| `pt_theme_border_strong` | `39445AFF` | `C8D0DDFF` | Emphasized borders and control edges |
| `pt_theme_outline` | `39445AFF` | `C8D0DDFF` | Semantic alias used by focusable controls |
| `pt_theme_outline_variant` | `232B3AFF` | `E1E6EEFF` | Quiet outlines inside containers |
| `pt_theme_text` | `E9EEF7FF` | `111827FF` | Primary text and icons |
| `pt_theme_text_muted` | `94A1B8FF` | `5F6B7FFF` | Secondary labels, hints, placeholders |
| `pt_theme_text_disabled` | `5B6577FF` | `A0A9B8FF` | Disabled labels and inactive controls |
| `pt_theme_selection_text` | `FFFFFFFF` | `111827FF` | Foreground for custom opaque selection fills |
| `pt_theme_accent` | `3568E0FF` | `2559E6FF` | Primary action and selected control fill |
| `pt_theme_accent_hover` | `3D6FE8FF` | `1E56E8FF` | Primary action, pointer over |
| `pt_theme_accent_pressed` | `2F5BC0FF` | `1C4ECFFF` | Primary action, pressed |
| `pt_theme_accent_text` | `FFFFFFFF` | `FFFFFFFF` | Text and glyphs on an accent fill |
| `pt_theme_accent_soft` | `3568E033` | `2559E626` | Tonal button and selected-row wash |
| `pt_theme_selection` | `3568E059` | `2559E640` | Text selection wash over surface |
| `pt_theme_focus` | `8CB0FFFF` | `2559E6FF` | Focus-visible ring and caret |
| `pt_theme_control` | `1A2130FF` | `F8FAFCFF` | Entry, toggle and slider rest fill |
| `pt_theme_control_hover` | `222B3CFF` | `EFF3F9FF` | Entry and control hover fill |
| `pt_theme_control_pressed` | `2A3447FF` | `E4EAF3FF` | Control pressed fill |
| `pt_theme_track` | `2A3344FF` | `DCE3EDFF` | Switch and slider groove |
| `pt_theme_scrollbar` | `4A566EFF` | `ADB8C8FF` | Scrollbar thumb |
| `pt_theme_titlebar` | `0E121BFF` | `FBFCFEFF` | Client-side title bar |
| `pt_theme_danger` | `F16B75FF` | `B92C38FF` | Destructive text, icons and borders |
| `pt_theme_success` | `4ACB8DFF` | `0F7A37FF` | Success text and icons |
| `pt_theme_warning` | `E5B454FF` | `8A5A00FF` | Warning text and icons |
| `pt_theme_danger_soft` | `F16B7526` | `B92C3820` | Error banner or invalid-field wash |
| `pt_theme_success_soft` | `4ACB8D26` | `0F7A3720` | Success banner wash |
| `pt_theme_warning_soft` | `E5B45426` | `8A5A0020` | Warning banner wash |
| `pt_theme_shadow` | `00000073` | `0F172A24` | Elevation shadow |
| `pt_theme_scrim` | `000000A6` | `0F172A66` | Modal scrim |
| `pt_theme_skeleton` | `1D2431FF` | `E7EBF1FF` | Loading placeholder base |
| `pt_theme_skeleton_highlight` | `2A3447FF` | `F3F5F9FF` | Loading placeholder shimmer |

`pt_theme_selection_text` is not used by the built-in entry and text-area
painters: they draw the translucent `pt_theme_selection` wash and keep the
normal text color, which preserves contrast on both schemes. Use the token when
a custom widget paints an opaque selection fill and needs the matching
foreground.

## Metrics

Metrics are logical pixels. Negative values are clamped to zero by the setters
and by resolution. `radius_pill` is allowed to be large because each shape
clamps it to half of its shorter side.

| Metric | Default | Use |
| --- | ---: | --- |
| `pt_theme_metric_radius_small` | 6 | Chips, check marks, small controls |
| `pt_theme_metric_radius_medium` | 10 | Buttons, entries, cards |
| `pt_theme_metric_radius_large` | 16 | Dialogs, menus, large panels |
| `pt_theme_metric_radius_pill` | 999 | Pills and switch tracks |
| `pt_theme_metric_focus_width` | 2 | Focus-visible ring stroke |
| `pt_theme_metric_focus_offset` | 2 | Gap between widget and ring |
| `pt_theme_metric_elevation_low` | 8 | Cards and subtle raised surfaces |
| `pt_theme_metric_elevation_high` | 20 | Menus, dialogs, popovers |
| `pt_theme_metric_chip_height` | 28 | Filter-chip height |
| `pt_theme_metric_control_height` | 34 | Standard button and entry height |

Font family is app state, not a metric: `pt_app_set_font_family` selects the
family used when a widget does not carry an explicit face.

## Accent derivation

`pt_app_set_accent_color` writes seven roles. The formulas are the ones in
`src/app.c`; `accent_text` is chosen by `pt_theme_contrast_text`, which compares
the accent's relative luminance with 0.62 and returns `101418FF` for light
accents and `FFFFFFFF` otherwise.

| Role | Dark scheme | Light scheme |
| --- | --- | --- |
| `accent` | the supplied color | the supplied color |
| `accent_hover` | lighten 14% | blend 14% black |
| `accent_pressed` | blend 18% black | blend 22% black |
| `accent_text` | contrast text | contrast text |
| `focus` | lighten 22% | blend 6% black |
| `accent_soft` | accent at alpha `33` (20%) | accent at alpha `26` (15%) |
| `selection` | accent at alpha `59` (35%) | accent at alpha `40` (25%) |

If you override `accent` directly with `pt_app_set_theme_color`, the derived
roles are **not** recomputed. Override each derived role explicitly or use
`pt_app_set_accent_color`.

## Contrast

The default palettes are held to these floors by `test_contrast()` in
`tests/test_theme.c`. Ratios use the WCAG 2.x relative-luminance formula.

| Pair | Dark | Light | Floor |
| --- | ---: | ---: | ---: |
| `text` on `background` | 16.59 | 16.40 | 7.0 (AAA) |
| `text` on `surface` | 15.69 | 17.74 | 7.0 |
| `text` on `surface_raised` | 14.80 | 17.74 | 7.0 |
| `text_muted` on `background` | 7.40 | 4.98 | 4.5 (AA) |
| `text_muted` on `surface` | 7.00 | 5.39 | 4.5 |
| `accent_text` on `accent` | 4.99 | 5.75 | 4.5 |
| `accent_text` on `accent_hover` | 4.53 | 5.91 | 4.5 |
| `accent_text` on `accent_pressed` | 6.22 | 6.91 | 4.5 |
| `danger` on `surface` | 6.17 | 6.03 | 4.5 |
| `success` on `surface` | 8.89 | 5.44 | 4.5 |
| `warning` on `surface` | 9.56 | 5.93 | 4.5 |
| `focus` on `background` | 8.99 | 5.32 | 4.5 |

Guidance that follows from the table:

- Use `text` for anything a user must read, `text_muted` for secondary
  information, and reserve `text_disabled` for controls that are genuinely
  inactive. Disabled text is deliberately below AA (3.11 dark, 2.37 light on
  `surface`); do not use it for enabled content or status messages.
- `outline`, `border` and the container tiers are decorative separators and do
  not meet 3:1 against `surface`. Do not encode meaning in a hairline alone;
  pair it with text, an icon or a filled state.
- White text on the light accent is 5.75:1. If you supply a new accent through
  `pt_app_set_accent_color`, the derived `accent_text` is chosen for you, but
  re-check `accent_hover` and `accent_pressed` if you then override them.
- The accent is the weakest pair by design (about 5:1). Do not lower it below
  4.5:1, and do not use accent text at small sizes with reduced opacity.
- Focus is checked against `background`, not against every surface. A custom
  window whose background is unusually light or dark should override `focus`
  and verify the ring against the actual backdrop.

When adding a custom pair, measure it instead of eyeballing it. The test file
contains `srgb_channel`, `relative_luminance` and `contrast_ratio` helpers that
can be copied into an application test.

## Dark and light pairing rules

The two palettes are not simple inversions. Keep these relationships when
adapting a palette:

- **Surface direction:** dark surfaces get lighter as they rise
  (`surface_sunken` → `surface` → `surface_container` → `surface_raised`).
  Light surfaces get slightly darker or stay white; a raised light surface may
  be the same white as the base.
- **Text direction:** dark uses near-white text; light uses near-black text.
  Muted text moves toward the background, not toward the accent.
- **Accent text:** choose it from accent luminance, not from the scheme. A
  light accent in a dark palette and a dark accent in a light palette both
  need dark text.
- **Translucency:** keep the alpha families aligned but not identical. The
  reference table uses `accent_soft` at 20%/15%, `selection` at 35%/25%, soft
  status at `26`/`20`, and scrim at `A6`/`66` (dark/light). Copying a dark
  alpha into a light palette usually produces a muddy wash.
- **Shadows and scrims:** use a cool near-black with alpha rather than a gray.
  The light palette's shadow and scrim are tinted `0F172A`, matching the
  background family.
- **Status roles:** dark status colors are lighter so they read on dark
  surfaces; light status colors are darker so they read on white. Keep the
  soft variants at the same hue and let alpha carry the emphasis.

A useful check is to render the same screen in both schemes and compare the
state matrix, not just the resting frame: hover, pressed, focus, selected,
disabled, invalid and busy each have their own token combinations.

## Overrides in practice

An app-wide brand pass, then a single preview window that stays neutral:

```c
/* Set the scheme first: it replaces app colors and metrics. */
pt_app_set_color_scheme(app, pt_color_scheme_dark);
pt_app_set_accent_color(app, pt_rgb(0x3D7BFF));
pt_app_set_theme_metric(app, pt_theme_metric_radius_medium, 12.0f);

/* One window opts out of the brand accent. */
pt_window_set_theme_color(preview, pt_theme_accent, pt_rgb(0x6B7280));
pt_window_set_theme_color(preview, pt_theme_accent_hover, pt_rgb(0x7B8494));
pt_window_set_theme_color(preview, pt_theme_accent_pressed, pt_rgb(0x5B6270));
pt_window_set_theme_color(preview, pt_theme_accent_text, 0xFFFFFFFFu);
pt_window_set_theme_metric(preview, pt_theme_metric_radius_medium, 8.0f);
```

`pt_window_clear_theme_overrides(preview)` returns every role and metric to the
app values. Window overrides are resolved at paint time, so the same widget can
be moved between windows only if the application manages the resulting visual
change itself.

A theme switch is app state. If an app follows the desktop light/dark
preference, apply `pt_app_set_color_scheme` and re-apply any brand overrides
afterwards; do not assume the previous custom colors survive.

## Testing a palette

- `make check` runs `test_theme` (defaults, both schemes, accent derivation,
  metric clamping, window override fallthrough and the contrast floors) and
  `test_visuals` (deterministic snapshots of controls in rest, hover, focus,
  selected, disabled, invalid and busy states).
- Set `PT_SNAPSHOT_DIR` to choose where `test_visuals` writes PNGs. The default
  is `build/snapshots`.
- `test_visuals` compares repeated paints, dark → light → dark round trips and
  window overrides, so a token that stops affecting paint shows up as two
  identical frames rather than as a silent visual regression.
- A new role must be appended to `pt_theme_token` before
  `pt_theme_token_count`; `tests/test_abi.c` fails if an existing value is
  renumbered. A new metric follows the same rule.

## Limits

- The table is global per app or per window. There are no per-widget token
  overrides beyond the existing explicit color setters, and no theme
  inheritance chain between windows.
- There is no theme file parser or visual editor in 1.0. A theme is code that
  calls the setters.
- `pt_app_set_color_scheme` is a wholesale palette load, not a merge. This is
  intentional: a partial switch would mix dark and light surfaces.
- The built-in palette assumes sRGB output. The software renderer does not do
  color management, so wide-gamut displays see the numeric sRGB values.
