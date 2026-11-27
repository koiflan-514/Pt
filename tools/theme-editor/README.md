# Pt theme editor

A standalone visual editor for the Pt theme token table: 38 color roles and
10 geometric metrics. It opens a real Pt main window and a separate preview
window, edits the palette with the public color and number pickers, imports and
exports `pt-theme/1` files, and can run headless to validate a theme in CI.

## What it is (and is not)

- It is a **tool**, not part of the library. It lives under `tools/theme-editor/`
  and its sources are never in `LIB_SOURCES`, so `libpt.so` keeps exactly the
  587 `pt_*` symbols and gains no `te_*` symbol.
- It is **core-ABI free**. Every theme operation goes through the public
  setters and getters: `pt_app_set_theme_color`, `pt_app_set_theme_metric`,
  `pt_window_set_theme_color`, `pt_window_set_theme_metric`,
  `pt_window_clear_theme_overrides`, and the matching getters, plus the public
  widget set and pickers. Nothing in `include/`, `src/`, `bindings/` or `api/`
  changes for it.
- It is the **self-hosting (dogfooding) showcase**. The editor is a Pt
  application, and its preview is a real Pt window restyled only through the
  public per-window override API. The editor's own window keeps the neutral
  default palette, so editing never makes the editing controls unreadable.

The token model and the meaning of each role are documented in
[docs/theming.md](../../docs/theming.md). The design of this tool is in
[docs/m27-design.md](../../docs/m27-design.md).

## Build and run

```sh
make theme-editor        # build build/theme-editor (GUI + CLI)
make run-theme-editor    # build and launch the editor
make check-theme-editor  # headless: validate the shipped dark and light fixtures
```

The GUI is only compiled by `make theme-editor`; it is not part of `make all`
or `make check`. Two pure-module unit tests (`test_theme_format`,
`test_theme_wcag`) do join `make check`, so the parser, the exporter and the
WCAG math stay covered even when the GUI is not built.

The CLI and the two unit tests never create a `pt_app`, so they run on a
display-less CI host with no compositor. Only the GUI mode (no arguments) needs
a Wayland compositor.

## CLI

```text
theme-editor                          open the GUI with the built-in dark palette
theme-editor <file.json>              open the GUI with an imported document
theme-editor --check [--strict] [file...]
                                      validate, print the WCAG pair table, no window
theme-editor --export-json <in> [out]
theme-editor --export-c <in> [out.c]
theme-editor --help                   usage
theme-editor --version                tool and schema version
```

Exit codes:

| Code | Meaning |
| --- | --- |
| `0` | success, and every floored WCAG pair passed (`--check`) |
| `1` | a floored WCAG pair failed (`--check`) |
| `2` | malformed input, a usage error, or an I/O error |

`--check` accepts zero or more files. With no file it validates the bundled
`themes/dark.json` and `themes/light.json` in one run; otherwise each file is
reported in turn, separated by a blank line. It prints the pair table and exits
`0` when every floored pair passes, `1` when one fails, and `2` for a malformed
file. When several files are checked, `2` wins over `1`. `--strict` promotes
parser warnings (for example an unknown key from a newer schema) to errors.

For `--export-json` and `--export-c` the output path is optional and may be
given either positionally or after `-o`; it cannot be given both ways. With no
output path the canonical text is written to standard output. An input or output
I/O failure, including a failed write to standard output, also exits `2`. The
exporter never writes a file implicitly in GUI mode; the headless
`--export-json` / `--export-c` forms are the only way to write a file.

## The `pt-theme/1` format

A theme file is one JSON object with five keys:

```json
{
  "schema": "pt-theme/1",
  "name": "Ocean",
  "scheme": "dark",
  "colors": { "background": "0B0E14FF", "...": "..." },
  "metrics": { "radius_medium": 10, "...": "..." }
}
```

- `colors` has one entry per role; keys are the short role names
  (`background`, `text_muted`, `accent`, ...), values are 8-digit `RRGGBBAA`
  hex without `#` (a 6-digit value means alpha `FF`). See
  [docs/theming.md](../../docs/theming.md) for the full role table.
- `metrics` has one entry per metric; keys are the short metric names
  (`radius_small`, `focus_width`, `control_height`, ...), values are finite
  JSON numbers in logical pixels with a canonical `%.9g` fixed-point form (see
  below; exponent-form values are rejected).
- `scheme` is `dark` or `light` and records the base palette that seeded the
  document; it is never inferred from the colors.
- Every known role and metric must be present. Unknown keys are warnings
  (`--strict` makes them errors). Duplicate keys are errors.
- Canonical output is deterministic: role/metric enum order, two-space indent,
  uppercase colors, metrics printed with `%.9g`, one trailing newline.
  Exporting the same document twice is byte-identical and re-parses to the same
  values.
- Limits: file size 1 MiB, JSON nesting depth 8, theme name 95 UTF-8 bytes, and
  no trailing bytes after the top-level object apart from JSON whitespace
  (space, tab, carriage return, line feed). Any other trailing byte is
  `TE_ERR_SYNTAX`. The parser is hand-written and bounded; it does not use
  `strtod`, so a user's decimal separator cannot change a value.

The C export (`--export-c`) writes one complete C23 translation unit with
`#include <pt/pt.h>` and `void theme_apply_<slug>(pt_app *app)` that sets the
scheme, every color and every metric through public API. The slug is a
lowercase `[a-z0-9_]` form of `name` (empty becomes `custom_theme`).

### Metric numbers and the fixed-point limit

Metrics are logical pixels and the writer serializes them with `%.9g`. The
reader accepts a metric only when that canonical `%.9g` form is fixed-point:
a finite JSON number whose printed form does not use an exponent. A value whose
`%.9g` form would use an exponent is rejected with `TE_ERR_RANGE` and the
message "metric has no canonical fixed-point representation". In practice that
means values below roughly `0.0001` and values at or above `1e9` are rejected:
the float nearest `0.0001` is `9.99999975e-05`, so even the literal `0.0001`
has an exponent form, while `1000000000` prints as `1e+09`. Negative metrics,
non-finite values, overflow and underflow to zero are rejected as well.

This is a format invariant, not a recommendation for the UI: metrics are
logical pixels, and a value below `0.001` px has no visible effect. Pickers and
generated themes should stay in ordinary pixel ranges. Because the reader
enforces exactly the `%.9g` canonical form that the writer emits, every
document that parses can be exported canonically and re-read to the same
values; a parsed document never carries a metric that the writer would turn
into exponent notation.

## WCAG checks

The WCAG module is a tool-local, pure implementation of WCAG 2.x contrast:
sRGB linearisation, relative luminance, contrast ratio and alpha compositing.
It reports the **actual** WCAG ratio of each documented text pair, grades it
`fail` / `AA` / `AAA`, and shows the floor (4.5 or 7.0) next to every row.
`text_disabled` is reported as informational and is expected below AA; borders,
outlines, containers, shadows and scrims are decorative and carry no text floor.
A pair is never silently skipped.

This is the real WCAG ratio. It differs from the core's internal
`pt_theme_contrast_text`, which picks a foreground with an approximate
gamma-encoded weighted sum and a `0.62` threshold. Near that threshold the core
may choose a different foreground; the editor shows the true ratio so the
failure is visible, and the user can override `accent_text` explicitly. The
core helper is intentionally not changed by this tool.

## Preview window

The preview is a second top-level Pt window owned by the application. It shows
a representative sample of the built-in controls so a palette change is visible
across the whole token table at once:

- a heading label and a muted caption
- primary, secondary and danger buttons
- an entry and a combobox
- a checkbox, a toggle and a slider
- a two-page tabs strip
- a small table (header, rows and a selected row)
- one focused control for the focus ring

Every control paints from the window palette; none sets an explicit widget
color. Applying a theme sets all 38 colors and all 10 metrics as window
overrides on the preview only, and reset clears them with
`pt_window_clear_theme_overrides`. The editor's own window is never restyled.

## Limits

- Format: `pt-theme/1` only (see the limits above). There is no `pt-theme/2`
  reader yet; the append-only token table means a newer file with an appended
  role still opens with a warning.
- GUI mode needs a Wayland compositor. The `--check`, `--export-json` and
  `--export-c` modes and the unit tests run headless.
- The C export writes one apply function per file; names are not made unique,
  so the application owns any collision between generated functions.
