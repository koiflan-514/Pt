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
- 1.0 shipped no theme file parser or visual editor: a theme was code that
  called the setters. **2.5.0 delivers the standalone theme editor** described
  in [Theme editor (2.5.0)](#theme-editor-250); the parser and the editor live
  entirely in the tool and add no core API.
- `pt_app_set_color_scheme` is a wholesale palette load, not a merge. This is
  intentional: a partial switch would mix dark and light surfaces.
- The built-in palette assumes sRGB output. The software renderer does not do
  color management, so wide-gamut displays see the numeric sRGB values.

## Theme editor (2.5.0)

Pt 2.5.0 ships a standalone theme editor under `tools/theme-editor/`. It is a
Pt application itself — the editor dogfoods the public widget set — and it
stays entirely outside the core ABI: the token table, the setters and getters
and `api/pt.api` are unchanged, and the tool adds no core symbol. The tool
owns its bounded pt-theme/1 reader and writer, its C exporter and a pure WCAG
2.x module; the core's approximate `pt_theme_contrast_text` is deliberately
left unchanged.

The editor makes the token table editable without hand-writing 48 setter
calls: a theme can be composed in the GUI, validated headlessly, stored as a
`pt-theme/1` JSON file and exported as C.

### Building, running and checking

The tool is not part of `make all`; build it explicitly:

- `make theme-editor` compiles `build/theme-editor` from the tool sources and
  links `build/libpt.a` through public headers only, so no tool symbol can
  enter `libpt.so`.
- `make run-theme-editor` builds it if needed and opens the GUI with the
  built-in dark palette.
- `make check-theme-editor` runs the headless gate: `--check` on both golden
  fixtures, `tools/theme-editor/themes/dark.json` and
  `tools/theme-editor/themes/light.json`. It prints the WCAG pair table,
  needs no display and fails if any floored pair misses its floor.

The CLI modes (`theme-editor --help` prints the same text):

```text
Usage: theme-editor [options] [file.json]

Standalone visual editor for the Pt theme token table.

Modes:
  theme-editor                              open the editor with the built-in dark palette
  theme-editor <file.json>                  open the editor with an imported document
  theme-editor --check [--strict] <file>    validate and print the WCAG pair table
  theme-editor --export-json <in> <out>     write canonical pt-theme/1 JSON
  theme-editor --export-c <in> <out.c>      write a compilable theme_apply_<slug> function

Options:
  --strict    promote unknown-key warnings to errors (--check)
  --help      show this help and exit
  --version   show the tool version and exit

Exit codes: 0 success, 1 a floored WCAG pair failed, 2 malformed input
or usage error. Headless modes never open a window.
```

### The pt-theme/1 format

A theme file is one JSON object whose `schema` is exactly `pt-theme/1`.
`scheme` is `dark` or `light`; it records the base palette that seeded the
document and the scheme the C exporter applies first, and is never inferred
from the colours. `colors` maps each of the 38 role names to an 8-hex-digit
`RRGGBBAA` value, and `metrics` maps each of the 10 metric names to a finite,
non-negative number. Role names are the short names from the token table above
without the `pt_theme_` prefix; metric names drop the `pt_theme_metric_`
prefix. Every known role and metric must be present.

Import is tolerant so a file can be hand-edited or produced with the colour
picker's own spellings: a colour may be written `#RRGGBBAA`, `#RRGGBB` (alpha
`FF`), `0xRRGGBBAA` or lowercase. The bundled golden dark palette below is the
canonical reference; its complete content is:

```json
{
  "schema": "pt-theme/1",
  "name": "Default Dark",
  "scheme": "dark",
  "colors": {
    "background": "0B0E14FF",
    "surface": "11151EFF",
    "surface_raised": "161B26FF",
    "surface_hover": "1D2431FF",
    "border": "232B3AFF",
    "border_strong": "39445AFF",
    "text": "E9EEF7FF",
    "text_muted": "94A1B8FF",
    "accent": "3568E0FF",
    "accent_hover": "3D6FE8FF",
    "accent_text": "FFFFFFFF",
    "danger": "F16B75FF",
    "success": "4ACB8DFF",
    "warning": "E5B454FF",
    "shadow": "00000073",
    "titlebar": "0E121BFF",
    "scrollbar": "4A566EFF",
    "focus": "8CB0FFFF",
    "surface_sunken": "080B11FF",
    "control": "1A2130FF",
    "control_hover": "222B3CFF",
    "accent_soft": "3568E033",
    "selection": "3568E059",
    "track": "2A3344FF",
    "surface_container": "141924FF",
    "surface_container_high": "1A2130FF",
    "outline": "39445AFF",
    "outline_variant": "232B3AFF",
    "text_disabled": "5B6577FF",
    "accent_pressed": "2F5BC0FF",
    "control_pressed": "2A3447FF",
    "danger_soft": "F16B7526",
    "success_soft": "4ACB8D26",
    "warning_soft": "E5B45426",
    "scrim": "000000A6",
    "skeleton": "1D2431FF",
    "skeleton_highlight": "2A3447FF",
    "selection_text": "FFFFFFFF"
  },
  "metrics": {
    "radius_small": 6,
    "radius_medium": 10,
    "radius_large": 16,
    "radius_pill": 999,
    "focus_width": 2,
    "focus_offset": 2,
    "elevation_low": 8,
    "elevation_high": 20,
    "chip_height": 28,
    "control_height": 34
  }
}
```

### Canonical output, safety limits and --strict

Exports are deterministic and canonical:

- colours are uppercase 8-digit hex without `#`;
- metrics are printed with `%.9g`;
- keys are in enum order (the order of the tables above), indented with two
  spaces, with exactly one trailing newline;
- exporting the same document twice is byte-identical, and re-parsing the
  output recovers every value.

The reader is bounded so an untrusted file cannot exhaust the tool: input is
at most 1 MiB, JSON nesting is at most 8 levels, the theme name is at most 95
UTF-8 bytes, trailing bytes after the top-level object are rejected, duplicate
keys are rejected, and metrics must be finite and non-negative — a negative
metric is an error, not a silent clamp. Numbers are parsed without `strtod`,
so a locale decimal separator cannot change the document. Malformed JSON,
unknown escapes, unpaired surrogates and invalid UTF-8 are rejected.

Unknown keys — for example a role a future schema will add — are ignored with
a warning, so a newer file still opens. `--strict` promotes those warnings to
errors, which is what CI uses: a file that is not exactly the current v1 shape
fails instead of passing with warnings.

### C export

`theme-editor --export-c <in.json> <out.c>` writes one complete, compilable
C23 translation unit with a single apply function — no header — so the
snippet can be pasted into an application or compiled directly. The function
applies the scheme first, then every colour and every metric through the
public setters in enum order. For the golden dark palette above the output is:

```c
/* Generated by pt-theme-editor; schema pt-theme/1; theme "Default Dark". */
#include <pt/pt.h>

void theme_apply_default_dark(pt_app *app) {
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_theme_color(app, pt_theme_background, 0x0B0E14FFu);
    pt_app_set_theme_color(app, pt_theme_surface, 0x11151EFF);
    pt_app_set_theme_color(app, pt_theme_surface_raised, 0x161B26FF);
    pt_app_set_theme_color(app, pt_theme_surface_hover, 0x1D2431FF);
    pt_app_set_theme_color(app, pt_theme_border, 0x232B3AFF);
    pt_app_set_theme_color(app, pt_theme_border_strong, 0x39445AFF);
    pt_app_set_theme_color(app, pt_theme_text, 0xE9EEF7FF);
    pt_app_set_theme_color(app, pt_theme_text_muted, 0x94A1B8FF);
    pt_app_set_theme_color(app, pt_theme_accent, 0x3568E0FF);
    pt_app_set_theme_color(app, pt_theme_accent_hover, 0x3D6FE8FF);
    pt_app_set_theme_color(app, pt_theme_accent_text, 0xFFFFFFFF);
    pt_app_set_theme_color(app, pt_theme_danger, 0xF16B75FF);
    pt_app_set_theme_color(app, pt_theme_success, 0x4ACB8DFF);
    pt_app_set_theme_color(app, pt_theme_warning, 0xE5B454FF);
    pt_app_set_theme_color(app, pt_theme_shadow, 0x00000073);
    pt_app_set_theme_color(app, pt_theme_titlebar, 0x0E121BFF);
    pt_app_set_theme_color(app, pt_theme_scrollbar, 0x4A566EFF);
    pt_app_set_theme_color(app, pt_theme_focus, 0x8CB0FFFF);
    pt_app_set_theme_color(app, pt_theme_surface_sunken, 0x080B11FF);
    pt_app_set_theme_color(app, pt_theme_control, 0x1A2130FF);
    pt_app_set_theme_color(app, pt_theme_control_hover, 0x222B3CFF);
    pt_app_set_theme_color(app, pt_theme_accent_soft, 0x3568E033);
    pt_app_set_theme_color(app, pt_theme_selection, 0x3568E059);
    pt_app_set_theme_color(app, pt_theme_track, 0x2A3344FF);
    pt_app_set_theme_color(app, pt_theme_surface_container, 0x141924FF);
    pt_app_set_theme_color(app, pt_theme_surface_container_high, 0x1A2130FF);
    pt_app_set_theme_color(app, pt_theme_outline, 0x39445AFF);
    pt_app_set_theme_color(app, pt_theme_outline_variant, 0x232B3AFF);
    pt_app_set_theme_color(app, pt_theme_text_disabled, 0x5B6577FF);
    pt_app_set_theme_color(app, pt_theme_accent_pressed, 0x2F5BC0FF);
    pt_app_set_theme_color(app, pt_theme_control_pressed, 0x2A3447FF);
    pt_app_set_theme_color(app, pt_theme_danger_soft, 0xF16B7526);
    pt_app_set_theme_color(app, pt_theme_success_soft, 0x4ACB8D26);
    pt_app_set_theme_color(app, pt_theme_warning_soft, 0xE5B45426);
    pt_app_set_theme_color(app, pt_theme_scrim, 0x000000A6);
    pt_app_set_theme_color(app, pt_theme_skeleton, 0x1D2431FF);
    pt_app_set_theme_color(app, pt_theme_skeleton_highlight, 0x2A3447FF);
    pt_app_set_theme_color(app, pt_theme_selection_text, 0xFFFFFFFF);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_small, 6.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_medium, 10.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_large, 16.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_radius_pill, 999.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_focus_width, 2.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_focus_offset, 2.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_elevation_low, 8.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_elevation_high, 20.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_chip_height, 28.0f);
    pt_app_set_theme_metric(app, pt_theme_metric_control_height, 34.0f);
}
```

The function name is `theme_apply_` plus the slug of the theme name: lowercase
ASCII letters, digits and `_`, with every other byte replaced by `_`; an
empty name exports as `theme_apply_custom_theme`. The GUI never writes a file
implicitly — export is an explicit action. Themes are not unique across files,
so the application owns any name collision.

### WCAG checking

`--check` prints every pair with its measured ratio, grade and pass/fail
verdict and exits 0 when every floored pair passes, 1 when a pair fails and
2 for a malformed file or a usage error; `--check --strict` also fails on
parser warnings. The table holds the 12 documentation pairs from
[Contrast](#contrast) above plus the two selection composites measured over
`surface` and `background` — 14 floored pairs — and one informational row
(`text_disabled` on `surface`, expected below AA, which never fails):

| Pair | Floor |
| --- | ---: |
| `text` on `background` | 7.0 (AAA) |
| `text` on `surface` | 7.0 |
| `text` on `surface_raised` | 7.0 |
| `text_muted` on `background` | 4.5 (AA) |
| `text_muted` on `surface` | 4.5 |
| `accent_text` on `accent` | 4.5 |
| `accent_text` on `accent_hover` | 4.5 |
| `accent_text` on `accent_pressed` | 4.5 |
| `danger` on `surface` | 4.5 |
| `success` on `surface` | 4.5 |
| `warning` on `surface` | 4.5 |
| `focus` on `background` | 4.5 |
| `selection_text` on `selection` over `surface` | 4.5 |
| `selection_text` on `selection` over `background` | 4.5 |

A pair is never skipped silently: every row is reported with `pass`, `AA`,
`AAA` or `fail`. Grades follow the WCAG 2.x levels — 7.0 and above is AAA,
4.5 up to below 7.0 is AA, below 4.5 fails. The selection rows composite the
translucent `selection` wash over the backdrop before measuring, so the
reported ratio is what the renderer actually produces.

The core's `pt_theme_contrast_text` — used by `pt_app_set_accent_color` to
pick `accent_text` — is a fast gamma-encoded weighted sum
(`0.2126R + 0.7152G + 0.0722B` over the 8-bit channels, threshold `0.62`),
not a WCAG luminance calculation. Near that threshold the two can disagree.
The core helper is not corrected in 2.5.0, because changing it would alter
accent derivation and existing pixels. The editor therefore measures pairs
with its own pure WCAG 2.x module — sRGB linearisation, the relative-luminance
and contrast formulas — and reports the true ratio; when the core would pick
a different `accent_text`, the contrast panel shows the failure and the user
can override `accent_text` explicitly. This is a documented difference
between a fast core heuristic and a standards-exact check, not a defect in
either.

### The editor workflow

The GUI is a Pt application:

- The main window shows the theme name, the scheme (read-only after
  creation) and the actions New dark, New light, Import JSON, Export JSON,
  Export C and Preview. A scrollable colour list has one `pt_color_picker`
  per role with the role name beside it; the picker's hex entry accepts
  `#RRGGBBAA` including alpha and its popup is the preset palette. A metrics
  section edits the ten metrics; a Contrast section shows the pair table
  with ratio and status.
- New dark / New light seeds the document from the built-in palette through
  the public app getters; the document then lives independently of the app.
- The preview is a second top-level Pt window with a representative sample of
  controls. Every edit is applied to that window only, through
  `pt_window_set_theme_color` / `pt_window_set_theme_metric`, so the editor's
  own window keeps the neutral default palette while the preview exercises the
  real per-window override path. Colour picker changes update the model and
  the preview immediately.
- Import replaces the whole document; Export canonicalises it. There is no
  direct theme-file write in GUI mode.
- Every picker, number field and button carries an accessible name, tab order
  follows the visual order, and status is text plus colour — never colour
  alone.

### Limits of the tool

- The editor is a tool, not a core API: it links the static library through
  public headers only and adds no symbol to `libpt.so`. Applications that do
  not build the tool are unaffected.
- The format is `pt-theme/1` and a file carries exactly one scheme.
  Multi-scheme bundles and reading a live desktop theme are future schema
  candidates.
- `--check` validates the palette in the file headlessly; it does not open the
  GUI or touch a compositor. Pixel-level verification of a custom palette is
  still the application's visual test (see
  [Testing a palette](#testing-a-palette) above).
