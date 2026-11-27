# M27 design: 2.5.0 standalone theme editor (proposed)

Status: **proposed, not started** (2026-11-27). This document is the scope,
feasibility and wave-allocation record for a possible 2.5.0 release. It was
written on the 2.4.0 tree `82a5b79` (branch `m27/scaffold`). No implementation,
header, binding, tool source or build file is changed by this proposal.

Design-time baseline: `api/pt.api` reads **587 functions / 968 entries**,
`ABI_MAJOR = 1`, SONAME `libpt.so.1`, version 2.4.0. The proposed release keeps
that surface and only adds a standalone tool, its tests and documentation.

## Decision summary

M27 selects **candidate A, the standalone visual theme editor**, from the
post-1.0 candidate list in [ROADMAP.md](ROADMAP.md):

- It is an explicit, named post-1.0 candidate ("a standalone visual theme editor
  on the token table, exporting C/JSON with built-in WCAG contrast checks; it
  stays out of the core ABI").
- It needs **no core API or ABI change**: the tool links the static library and
  uses only the public theme setters/getters, the public colour picker and the
  public widget set. The token table is already 38 roles and 10 metrics with a
  complete public read/write surface.
- It is genuinely parallel: the format/serialization module, the WCAG module,
  the editor UI and the preview window can be owned by different agents with
  disjoint files.
- It is a dogfooding showcase: the editor itself is a Pt application, and its
  preview is a real Pt window restyled through the public per-window override
  API.

Two other candidates are evaluated and **deferred** with evidence:

- **B, rich clipboard paste of inline image runs**, needs a multi-MIME
  clipboard store, a bounded rich serialization, new public API and edits to
  the two most serial files in the platform (`src/wayland/platform.c` and
  `src/widget.c`). It is a core change, not a tools-track change; the read-only
  evidence is recorded below.
- **C, the remaining deferred feature list**, contains no better
  cost/risk/parallelism candidate: the 0.6 deferred list is mostly already
  delivered, and what remains is core layout or a third-party SVG parser.

Vulkan/EGL, color-management-v1, X11, xdg-foreign v1 and rich image paste stay
deferred. Their existing evidence is unchanged.

## Evidence and method

Research was read-only against the repository. Commands used:

```text
git -C <worktree> log --oneline -1       82a5b79
git -C <worktree> status --short         clean
pwd                                      .../worktree/a373e7/m27-scaffold
make -j2 check                           exit 0, all checks passed
```

The baseline `make -j2 check` is green before the proposal. `api/pt.api` reads
587 / 968 and the 2.4.0 release record in [release.md](release.md) records 47 C
test executables with 588,122 checks.

### The current theme model

| Fact | Evidence |
| --- | --- |
| 38 colour roles | `include/pt/theme.h:34-76`: 24 baseline values (`0`-`23`) plus 14 semantic roles (`24`-`37`), then `pt_theme_token_count` |
| 10 geometric metrics | `include/pt/theme.h:81-93`: radii, focus width/offset, elevations, chip/control heights, then `pt_theme_metric_count` |
| Append-only ABI | `include/pt/theme.h:12-24`, `docs/theming.md:9-13`; `tests/test_abi.c` pins the numbers |
| Internal storage | `src/internal.h:1532-1537` `pt_theme_data` holds `colors[38]`, `metrics[10]`, a 96-byte font family and the scheme |
| Default palettes | `src/theme.c:15-99` full dark and light arrays; `src/theme.c:101-112` default metrics |
| Resolution | `src/theme.c:114-155`: window override, then app palette, then compiled default; metrics clamp to zero |
| Public write side | `pt_app_set_color_scheme`, `pt_app_set_accent_color`, `pt_app_set_theme_color`, `pt_app_set_theme_metric` in `include/pt/app.h:98-112`; window overrides and `pt_window_clear_theme_overrides` in `include/pt/window.h:143-160` |
| Public read side | `pt_app_get_theme_color`, `pt_app_get_theme_metric`, `pt_window_get_theme_color`, `pt_window_get_theme_metric` |
| No parser, no serializer, no editor | `docs/theming.md:265-275`: "There is no theme file parser or visual editor in 1.0. A theme is code that calls the setters." The same limit is repeated in `docs/ROADMAP.md:770` and the 2.4.0 deferred register |
| Contrast helpers already exist in tests | `tests/test_theme.c:12-33`: `srgb_channel`, `relative_luminance`, `contrast_ratio`; `tests/test_theme.c:52-86` pins the palette floors |
| Core contrast choice is approximate | `src/theme.c:128-134` uses a gamma-encoded weighted sum (`0.2126r + 0.7152g + 0.0722b`) and a `0.62` threshold, while the contrast table in `docs/theming.md:155-169` and `tests/test_theme.c` use WCAG sRGB linearisation |

The last point is important for the editor, not a defect to fix in M27: the tool
must report the actual WCAG ratio of the chosen pair. If the core's approximate
`pt_theme_contrast_text` picks the wrong foreground near the threshold, the
contrast panel shows the failure and the user can override `accent_text`
explicitly. Changing the core helper would alter accent derivation, snapshots
and possibly existing pixels; it is not part of a tools-only release.

The WCAG module is therefore a **tool-local, pure implementation of WCAG 2.x**,
not a wrapper around the core helper.

### Candidate A: theme editor (selected)

The tool can be built entirely from the existing public surface:

- apply a complete palette to the preview window with
  `pt_window_set_theme_color` / `pt_window_set_theme_metric`;
- reset it with `pt_window_clear_theme_overrides`;
- read the built-in dark/light palettes through
  `pt_app_set_color_scheme` + `pt_app_get_theme_color` /
  `pt_app_get_theme_metric` (the editor owns its app, so reading defaults does
  not need a file parser in the core);
- edit colours with `pt_color_picker_create` plus
  `pt_color_picker_set_hex` / `pt_color_picker_get_hex`, which already accept
  `#RRGGBB` and `#RRGGBBAA` (`include/pt/picker.h:340-365`);
- edit metrics with `pt_number_picker_create`, which is locale-independent
  (`include/pt/picker.h:275-334`) or with a slider and a text field.

No new core type, function, enum or struct is required. The tool owns its JSON
document model, its parser and its C exporter.

### Candidate B: rich inline-image paste (deferred)

The 2.3.0 limitation is explicit in `docs/release.md:1460-1462`:

> Images are single logical boxes. There is no image pasting, no rich clipboard
> MIME type that recreates descriptors, no vector or animated run and no
> fractional scaling of a partially visible run.

The current implementation explains why this is a core change:

| Fact | Evidence |
| --- | --- |
| The clipboard stores one MIME type and one payload | `src/wayland/platform.c:2220-2253`: `clipboard_mime`, `clipboard_data`, `clipboard_size`; `clipboard_store` replaces the previous payload |
| The source offers one type, or five text spellings | `src/wayland/platform.c:2274-2285`: text offers five MIME spellings; otherwise one `wl_data_source_offer` |
| The source sends only the stored MIME (or any text MIME for a text payload) | `src/wayland/platform.c:1908-1928` |
| The text getter returns text only | `src/wayland/platform.c:2434-2450` |
| The image getter accepts only `image/png` | `src/wayland/platform.c:2452-2490` |
| Transfer limits | 16 MiB and 1.5 s (`src/wayland/platform.c:22-23`); image dimension cap 8192 (`include/pt/window.h:243`) |
| Paste is plain text only | `src/widget.c:4024-4035`: Ctrl+V calls `pt_platform_clipboard_get` and `entry_insert_text`; U+FFFC is inserted as a normal codepoint |
| Copy degrades images to alt text | `src/image_run.c:275-355`: `pt_widget_images_copy_text` replaces an image with its alt text, or U+FFFC if alt is empty |
| Descriptor shape | `include/pt/base.h:223-230`: `start`, borrowed `pt_icon_image *`, width/height, align, 127-byte alt |
| No public pixel readback | `include/pt/icon.h:127-160` exposes load/ref/destroy/width/height only; raster access is internal (`src/icon.c:214`) |

A faithful paste path would need all of the following:

1. A multi-payload clipboard store and a source that offers `text/plain` **and**
   the rich type in one selection; today one `wl_data_source` offers one payload.
2. A versioned, bounded rich MIME (for example
   `application/x-pt-image-runs;v=1`) carrying text plus per-run descriptors and
   PNG bytes. The bytes must be embedded because an image handle cannot cross a
   process boundary. Base64 or a chunked binary format adds size; the existing
   decode bomb and 8192-pixel guards can be reused.
3. Public set/get API for the rich payload, or a behaviour change in the
   built-in copy/paste path. The side table has no serializer and the icon
   handle has no public pixel export, so the core would need an internal PNG
   encode path or a new public icon-pixel API.
4. Paste insertion that rebuilds the side table at the right U+FFFC offsets,
   including edits, selection replacement, undo and RTL runs.
5. Tests across clipboard media, text/image editing, fake compositor and the
   live compositor path.

That is a core feature with new public symbols and edits to the clipboard
hot spot. It is not a tools-track item, and it is larger and riskier than the
whole M27 theme editor. **Decision: defer; do not include in M27.** Re-open with
a dedicated design when rich paste has a concrete product requirement.

### Candidate C: other deferred gaps (no better candidate)

The 0.6 "deferred beyond 0.6" line (`docs/ROADMAP.md:465`) is largely stale:

- **Multi-column sorting is already delivered.** `pt_table_set_sort_columns`,
  `pt_table_get_sort_columns` and the callback are in `include/pt/widget.h:690-757`;
  column layout persistence stores the sort state (`include/pt/widget.h:860-895`).
- **Keyed row updates are already delivered.** `pt_row_key_callback`,
  `pt_list_view_set_selected_key`, `pt_table_set_selected_key` and the key
  tables are in `include/pt/widget.h:540-592` and `:763-768`.
- **Styled text and the function-by-function reference are delivered.**
- **Variable row heights remain open.** Every view has one scalar row height
  (`pt_list_view_set_row_height`, `pt_table_set_row_height`,
  `pt_tree_set_row_height`, `include/pt/widget.h:562,726,950`). Supporting
  per-row heights changes measurement, recycling, scroll mapping, hit-testing
  and keyboard navigation across three views: core layout work with a wide test
  matrix, not a small tool.
- **The SVG subset remains intentionally incomplete.** Text, raster images,
  filters, masks, clipPath, use/symbol, markers, animation and the full CSS
  cascade are ignored by the vendored nanosvg parser
  (`docs/ROADMAP.md:1771-1777`, `docs/release.md:2185-2192`). Extending it means
  owning a third-party parser subset and its security surface; no product need
  is recorded.
- **Color-management, Vulkan/EGL and X11** already carry measured deferrals
  (`docs/ROADMAP.md:2719-2735`); nothing changed in 2.4.0 that re-opens them.

The theme editor is the only remaining item that is both a named candidate and
a zero-core-risk, parallel tool. **Decision: select A and register B/C items in
the deferral list.**

## Proposed design

### Placement and build

```text
tools/theme-editor/
  theme_format.h       document model, token/metric name tables, JSON/C API
  theme_format.c       bounded JSON reader/writer, canonical JSON, C exporter
  theme_wcag.h         pure WCAG 2.x helpers and the pair table
  theme_wcag.c         sRGB linearisation, luminance, contrast, alpha compositing
  preview.h            preview-window interface
  preview.c            the real Pt preview window and its sample controls
  theme_editor.c       app shell, token/metric editors, CLI dispatch
  README.md            build, CLI, JSON schema, limits
  themes/dark.json     golden full dark palette
  themes/light.json    golden full light palette
  themes/low-contrast.json  negative fixture for --strict
tests/test_theme_format.c
tests/test_theme_wcag.c
```

The root Makefile gains `theme-editor` and `run-theme-editor` targets and two
test targets. The tool is **not** part of `all`: a normal library build does not
compile GUI tool code. `make check` compiles and runs the two pure-module test
binaries; it does not open a window.

The tool links `build/libpt.a` and includes only public headers. No tool source
is added to `LIB_SOURCES`, which is `src/*.c` and `src/wayland/*.c` only.

### Document model

The tool-local model mirrors the public table, with protected sizes:

```c
typedef struct te_theme_doc {
    char name[96];
    pt_color_scheme scheme;
    pt_color colors[pt_theme_token_count];
    float metrics[pt_theme_metric_count];
} te_theme_doc;
```

`theme_format.c` owns a name table keyed by enum:

```c
static const char *const te_color_names[pt_theme_token_count] = {
    [pt_theme_background] = "background",
    [pt_theme_surface] = "surface",
    ...
};
static_assert(pt_theme_token_count == 38, "update the theme-editor name table");
static_assert(pt_theme_metric_count == 10, "update the theme-editor name table");
```

A future core role makes the `static_assert` fail until the tool is updated,
which is the intended maintenance gate. A unit test also checks that every
enum has a non-empty, unique name.

### JSON schema, version 1

```json
{
  "schema": "pt-theme/1",
  "name": "Ocean",
  "scheme": "dark",
  "colors": {
    "background": "0B0E14FF",
    "surface": "11151EFF",
    "...": "all 38 roles, one per line"
  },
  "metrics": {
    "radius_small": 6,
    "radius_medium": 10,
    "...": "all 10 metrics, one per line"
  }
}
```

Rules:

- `schema` is exactly `pt-theme/1`; a missing or different value is an error
  with a clear message. A future reader may add `pt-theme/2`; v1 files stay
  readable.
- `scheme` is `dark` or `light`. It records the base palette that seeded the
  document and the scheme the C exporter applies first. It is not inferred from
  the colour values.
- `colors` keys are the short role names above; values are 8 hex digits
  `RRGGBBAA`, uppercase on export. Import also accepts `#RRGGBBAA`,
  `#RRGGBB`, `0xRRGGBBAA`, and lowercase; a 6-digit value means alpha `FF`.
  This matches the colour picker's accepted spelling without making the file
  format ambiguous.
- `metrics` keys are the short metric names; values are finite JSON numbers.
  Negative values are rejected by the parser rather than silently clamped, so
  an exported file always round-trips to the same resolved value. The editor
  itself clamps interactive input at zero.
- Every known role and metric must be present. Unknown keys are ignored with a
  warning (so a newer file with an appended role still opens); `--strict`
  promotes warnings to errors. Duplicate keys are errors.
- Output is deterministic: fixed token order (enum order), two-space indent,
  colors uppercase, metrics printed with `%.9g`, one trailing newline. Exporting
  a parsed document twice is byte-identical.
- Limits: file size 1 MiB, nesting depth 8, name 95 UTF-8 bytes, no trailing
  garbage. The parser is a bounded hand-written recursive-descent reader with a
  locale-independent decimal parser; it does not use `strtod`, so a user's
  decimal separator cannot change the file.
- UTF-8 strings support the standard JSON escapes and `\uXXXX` surrogate pairs;
  unpaired surrogates and malformed UTF-8 are rejected.

### C export

The exporter writes one complete apply function, not a header, so the snippet
can be pasted into an application or compiled directly:

```c
/* Generated by pt-theme-editor; schema pt-theme/1; theme "Ocean". */
#include <pt/pt.h>

void theme_apply_ocean(pt_app *app) {
    pt_app_set_color_scheme(app, pt_color_scheme_dark);
    pt_app_set_theme_color(app, pt_theme_background, 0x0B0E14FFu);
    /* ... all 38 roles in enum order ... */
    pt_app_set_theme_metric(app, pt_theme_metric_radius_small, 6.0f);
    /* ... all 10 metrics ... */
}
```

The function name is `theme_apply_` plus a slug of `name` (`[a-z0-9_]`,
lowercase, empty -> `custom_theme`). Names are not unique across files; the
application owns the collision. The exporter never writes a file implicitly in
GUI mode; `--export-c <in.json> <out.c>` is the headless form. The generated
code is plain C23 and uses only public API.

### WCAG module

Pure functions, no Pt state and no allocation:

```c
double te_wcag_srgb_channel(uint8_t component);
double te_wcag_relative_luminance(pt_color color);       /* ignores alpha */
double te_wcag_contrast_ratio(pt_color a, pt_color b);
pt_color te_wcag_composite_over(pt_color fg, pt_color bg);
```

- `srgb_channel`: `c/255 <= 0.04045 ? c/255/12.92 : ((c/255+0.055)/1.055)^2.4`.
- Luminance: `0.2126 R + 0.7152 G + 0.0722 B` on linear channels.
- Ratio: `(Lmax + 0.05) / (Lmin + 0.05)`; range `1..21`.
- `composite_over` uses straight-alpha source-over with `pt_color`'s
  `0xRRGGBBAA` order, matching `tests/test_theme.c:35-50`; fully transparent
  foreground returns the background, opaque foreground returns the foreground.

The pair table covers the twelve floors documented in `docs/theming.md:158-169`
plus the two selection composites in `tests/test_theme.c:82-85`:

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
| `selection_text` on `selection` composited over `surface` | 4.5 |
| `selection_text` on `selection` composited over `background` | 4.5 |

`text_disabled` is reported as informational and is expected below AA
(`docs/theming.md:181-186`); borders, outlines, containers, shadows and scrims
are decorative and have no text floor. A pair is never silently skipped: the
report shows every row with "pass", "AA", "AAA" or "fail".

`--check` prints the table and exits 0 when every floored pair passes, 1 when a
pair fails, and 2 for a malformed file. `--strict` makes parser warnings fail
as well. This gives CI and reviewers a headless mode with no compositor.

### Editor UI

The editor uses Pt itself (`pt_app_create`, `pt_window_create`) and only the
public widget set:

- **Main window.** Header with theme name, scheme (read-only after creation),
  New dark / New light, Import JSON, Export JSON, Export C and Preview. A
  scrollable colour list has one `pt_color_picker` per role with the role name
  beside it; the picker's built-in hex entry handles `RRGGBBAA`, including
  alpha, and its popup is the preset palette. A metrics tab uses ten
  `pt_number_picker`s or sliders. A Contrast tab shows the pair table with
  ratio and status.
- **Preview window.** A second top-level Pt window with a representative
  sample: label and muted label, primary/secondary/danger buttons, entry,
  combobox, checkbox, toggle, slider, tabs, a small table and a focused control.
  Every edit is applied through `pt_window_set_theme_color` /
  `pt_window_set_theme_metric` on that window only. The editor's own window
  keeps the neutral default palette, so editing does not make the controls
  unreadable, and the preview exercises the real per-window override path.
- **Editing flow.** New dark/light seeds the document from the app getters,
  using `pt_app_set_color_scheme` on the editor app; the document then lives
  independently. Colour picker changes update the model and the preview.
  Import replaces the document; export canonicalises it. There is no direct
  theme-file write in GUI mode.
- **Accessibility.** Every picker, number field and button has an accessible
  name; tab order follows the visual order; status is text plus colour, not
  colour alone.

### Headless CLI

```text
theme-editor                       open the GUI with the built-in dark palette
theme-editor <file.json>           open the GUI with an imported document
theme-editor --check <file.json>   validate, print WCAG pairs, no window
theme-editor --check --strict ...  warnings and failed pairs are errors
theme-editor --export-json <in> <out>
theme-editor --export-c <in> <out.c>
```

Headless modes never call `pt_app_create`, so they run on a display-less CI
host. They use the same parser and exporter as the GUI, which is why the unit
tests and the CLI cannot diverge.

## Zero-core-ABI argument

- No file under `include/`, `src/`, `bindings/`, `api/` or `third_party/` is
  edited. `LIB_SOURCES` is unchanged, so `libpt.so` contains no tool symbol.
- The tool includes `<pt/pt.h>` and links `build/libpt.a`; every theme operation
  goes through the public setters/getters. The only new symbols live in the
  tool and test binaries.
- `api/pt.api` stays **587 / 968**, `ABI_MAJOR` stays `1`, SONAME stays
  `libpt.so.1`. `nm -D --defined-only build/libpt.so` still lists exactly the
  587 `pt_*` symbols and no `te_*` symbol.
- `tools/gen-api-reference.py --check`, `make api-check`, the binding parity
  check and the exported-symbol audit are unchanged.
- The release version may become 2.5.0 (`include/pt/base.h` macros and the
  Makefile `VERSION` only) because the tool ships in the project release. That
  changes the library filename `libpt.so.2.5.0`, not the SONAME or any ABI
  value. If the release owner prefers a tools-only tag without a library
  version bump, the same gate passes with 2.4.0; the design does not depend on
  the version string.

The core's approximate `pt_theme_contrast_text` is **not** changed. The editor
reports the real WCAG result and lets the user set `accent_text` explicitly.
A future core correction would be a separate visual/ABI-neutral change with
snapshot review, not part of M27.

## Test plan

### `tests/test_theme_format.c`

- Name tables: 38 colour names and 10 metric names are non-empty, unique and
  map 1:1 to the enums; the two `static_assert`s fail at compile time if the
  counts move.
- Golden dark/light files parse, export and re-parse to identical values.
- Canonical JSON: export twice is byte-identical; token order is enum order;
  colours are uppercase 8-digit; metrics are `%.9g`.
- Import tolerance: `#RRGGBB`, `#RRGGBBAA`, `0x...`, lowercase, whitespace and
  JSON escapes; a 6-digit colour becomes alpha `FF`.
- Rejections: missing/unknown schema, wrong scheme, missing role, duplicate key,
  unknown escape, unpaired surrogate, trailing garbage, over 1 MiB, depth over
  8, non-finite or negative metric, oversized name.
- Unknown role/metric: accepted with a warning by default, rejected under
  `--strict`.
- C export: golden function name, 38 setters and 10 metric calls in order,
  compiles with `cc -std=c23 -Iinclude -c <generated.c>`.
- CLI exit codes 0/1/2 on the bundled good and bad fixtures.

### `tests/test_theme_wcag.c`

- Fixed vectors: black/white `21.0`, `#777777` on white `4.4781`, `#767676` on
  white `4.5422`, identical colours `1.0` (all within `1e-4`).
- sRGB threshold: a component of 10 uses the linear branch, 11 uses the power
  branch; the function is monotonic.
- Alpha: opaque foreground equals foreground; transparent foreground equals
  background; 50% composite is the arithmetic midpoint for equal channels;
  compositing over an opaque background always yields alpha `FF`.
- Default palette: the 12 dark and 12 light pairs in `docs/theming.md:160-180`
  match the documented ratios within 0.01. `text_disabled` is below 4.5 and is
  informational, not a failure.
- Pair table: every floored pair resolves a valid token, both selection
  composites are present, every ratio is in `1..21`, and the selection rows
  composite the translucent wash before measuring.
- The golden dark/light fixtures applied to the table pass every floor; the
  headless CLI exit behaviour is covered by the gate checks.

### Gate checks

- `make -j2 theme-editor` builds with `werror=1`; no tool source includes
  `internal.h` or any private header.
- `make -j2 check` is green: the two new suites plus the existing 47 C
  executables and 588,122 checks. `api/pt.api` remains 587 / 968.
- `make -j2 check-bindings` remains 587/587 in C++, Python and Java; no binding
  is touched.
- `tools/gen-api-reference.py --check` is clean; `tools/check-bilingual-docs.py`
  reports 11 pairs (the editor docs extend the existing `theming.md` pair; no
  twelfth public guide is added).
- Headless CLI runs under a clean environment with no `WAYLAND_DISPLAY`:
  `--check` on both goldens, `--strict` on the negative fixture, `--export-c`
  output compiles.
- Serial ASan+UBSan pass (`make sanitize=1 -j2` builds; suites run one at a
  time) over both modules, the CLI fixtures and the tool binary's headless
  paths. No `-j8` build.
- Live smoke under `tools/nested-wayland.sh`: open the editor, change a role,
  see the preview update, import/export, close cleanly; no protocol errors.
- `git status` is clean after the gate commit.

## Parallel wave decomposition

Files are the unit of ownership; no two concurrently active agents touch the
same file. Only W0 touches the root Makefile. The tool directory is split by
module, so format, WCAG, editor and preview can run in parallel after W0 freezes
the interfaces.

### Waves

| Wave | Scope | Files owned (exclusive) | Depends on | Parallel with | Owner |
| --- | --- | --- | --- | --- | --- |
| **W0** | Interfaces and build skeleton: `te_theme_doc`, name-table declaration, JSON/C function signatures, WCAG signatures, preview interface; Makefile targets and test wiring; stub modules that compile | `tools/theme-editor/theme_format.h`, `theme_wcag.h`, `preview.h`, `theme_format.c` stub, `theme_wcag.c` stub, `preview.c` stub, `theme_editor.c` stub, `tests/test_theme_format.c` stub, `tests/test_theme_wcag.c` stub, `Makefile` (`theme-editor`, `run-theme-editor`, two test rules), `docs/m27-design.md` (this file) | - | - | DuMate |
| **W1** | Bounded JSON v1 reader/writer, canonical output, C exporter, golden and negative fixtures | `tools/theme-editor/theme_format.c`, `tools/theme-editor/themes/*.json`, `tests/test_theme_format.c` | W0 | W2, W3a, W3b, W4 | DuMate |
| **W2** | WCAG pure module: sRGB linearisation, luminance, ratio, alpha composite, pair table, vectors and default-palette checks | `tools/theme-editor/theme_wcag.c`, `tests/test_theme_wcag.c` | W0 | W1, W3a, W3b, W4 | WB-CN |
| **W3a** | Editor shell: app/window, colour list with `pt_color_picker`, metric editors, contrast panel, import/export wiring, headless CLI dispatch | `tools/theme-editor/theme_editor.c` | W0 (links W1/W2 at integration) | W1, W2, W3b, W4 | DuMate |
| **W3b** | Preview window: second Pt window, representative controls, all 38+10 per-window overrides, redraw on model change | `tools/theme-editor/preview.c`, `tools/theme-editor/README.md` | W0 | W1, W2, W3a, W4 | WB-CN (after W2) |
| **W4** | User documentation: theme editor section in the existing guide pair, JSON schema, CLI, limits, WCAG table, dogfooding note | `docs/theming.md`, `docs/zh-CN/theming.md`, `docs/m27-design.md` (implementation notes only after review) | W0 spec | W1, W2, W3a, W3b | Loomy (one agent, no subagents) |
| **W5** | Integration, review, full gate, 2.5.0 version/release record, ROADMAP/design status | `docs/ROADMAP.md`, `docs/release.md`, `docs/m27-design.md` (final status), `include/pt/base.h` (version macros only), `Makefile` (`VERSION` only) | W1-W4 | - | CodeArts review + DuMate gate |

### Dependency graph

```text
W0 (interfaces + build + stubs)
 ├─ W1 (format/serialization) ─┐
 ├─ W2 (WCAG) ─────────────────┤
 ├─ W3a (editor shell) ────────┼─ W5 (integration + review + release gate)
 ├─ W3b (preview) ─────────────┤
 └─ W4 (docs) ─────────────────┘
```

W1 and W2 are pure modules with no dependency on each other. W3a and W3b build
against the interface headers frozen by W0; they link the real W1/W2 modules
when the integration branch merges all tips. W4 writes against the frozen
schema and is corrected only at the W5 read-through if the implementation
differs.

### File-domain rules

- **Build hot spot:** `Makefile` is W0-only. It adds the tool target, the run
  target and the two custom test rules. Later waves add files that match the
  existing `wildcard` patterns; they never edit the Makefile. W5 owns only the
  `VERSION` line at release time.
- **Interfaces:** `theme_format.h`, `theme_wcag.h` and `preview.h` are W0-only.
  If W1/W2/W3 need a signature change, they raise it to W0's owner before
  editing; no concurrent header edits.
- **Implementation files:** W1 has `theme_format.c` and fixtures; W2 has
  `theme_wcag.c`; W3a has `theme_editor.c`; W3b has `preview.c` and the tool
  README. These are disjoint.
- **Tests:** W1 owns `test_theme_format.c`; W2 owns `test_theme_wcag.c`. W0's
  stubs are replaced by the owning wave; after that, no other wave adds
  assertions to those files.
- **Docs:** W4 owns `docs/theming.md` and its Chinese pair. W5 owns
  `docs/ROADMAP.md`, `docs/release.md` and the final status of this design.
  `docs/m27-design.md` is internal English and needs no Chinese pair, like
  `m23-design.md` through `m26-design.md`.
- **Forbidden:** `include/`, `src/`, `bindings/`, `api/`, `third_party/` and the
  existing tests are read-only for every M27 wave. A change there means the
  scope has drifted into core work; stop and re-adjudicate.

### Review strategy

- W1's parser gets a bounded security review by CodeArts before W3a links it:
  size/depth caps, duplicate keys, allocation overflow, surrogate handling,
  no `strtod`/locale dependency.
- W2's formula and alpha composite are checked by DuMate against the vectors
  and the `docs/theming.md` table.
- W3a and W3b get a cross-review by the provider that did not write the file
  (WB-CN reviews W3a; DuMate reviews W3b), focused on public-only includes,
  preview override completeness (38+10) and accessible names.
- W4's Chinese counterpart is checked by `tools/check-bilingual-docs.py`
  (headings, tables and fenced code must match) plus a human read-through.
- W5 reproduces the gate on a clean worktree; CodeArts runs the independent
  `--check`/export/sanitizer pass, DuMate fixes and writes the release record.
  No wave may lower the existing test count, API baseline or ABI.

### Provider allocation and budget

Estimated total: **~660-740 credits**.

| Wave | Credits | Provider | Notes |
| --- | ---: | --- | --- |
| W0 | 60 | DuMate | Interfaces, build, stubs |
| W1 | 130 | DuMate | Serialization and fixtures; parser security |
| W2 | 70 | WB-CN | Independent pure module, parallel with W1 |
| W3a | 150 | DuMate | Editor shell and CLI; links W1/W2 |
| W3b | 80 | WB-CN | Preview window, after W2; disjoint file |
| W4 | 70 | Loomy | Documentation, exactly one agent, no fan-out |
| W5 | 110 | CodeArts 85 + DuMate 25 | Independent review plus integration/release |
| Contingency | 60 | DuMate | Gate fixes, parser edge cases |

Provider shares: DuMate ~55-60% (W0, W1, W3a, gate fixes and contingency),
WB-CN ~20% (W2, W3b), Loomy ~10% (W4 only), CodeArts ~12% (W5 review).
Q-CN is the backup for W2/W4 if free concurrency is unstable. Lobster is not
assigned. Loomy runs one agent at a time: W4 does not overlap a second Loomy
wave.

If W3a misses its gate, split it into W3a1 (colour/metric editors and model
wiring) and W3a2 (import/export/contrast panel) on the same owner, never two
agents in `theme_editor.c`. If W1's parser review blocks W3a, W3a continues
against the W0 stubs and links at integration; it does not invent a second
parser.

## Risks and mitigations

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Scope drift into the core to make the tool "easier" | High | W0/W5 diff gate: no `include/`, `src/`, `bindings/`, `api/`, `third_party/` changes; public-only include check; API baseline 587/968 |
| Hand-written JSON parser bug or memory blow-up | High | Bounded reader, 1 MiB/8-depth caps, duplicate rejection, no locale-dependent `strtod`, malformed corpus, ASan/UBSan serial pass, CodeArts review |
| WCAG result disagrees with core `pt_theme_contrast_text` near 0.62 | Medium | Tool reports the real WCAG ratio; user can override `accent_text`; core remains unchanged; docs state the difference |
| Preview path not truly isolated from the editor palette | Medium | Preview is a separate `pt_window` with per-window overrides; model changes call only window setters; manual live check |
| UI scope creep (multi-scheme bundles, import from desktop portal, live system theme) | Medium | v1 is one scheme per file; no portal/system-theme reader; those are v2 candidates |
| Token enum grows after M27 | Low | `static_assert` counts and name-table completeness test fail the build until the tool is updated |
| Tool build slows CI | Low | not in `all`; only two pure test binaries join `make check`; GUI compiled only by `make theme-editor` |
| Colour picker alpha handling surprises users | Low | hex entry is authoritative and accepts `RRGGBBAA`; picker's existing contract already supports alpha |
| Docs and implementation drift during parallel waves | Low | W4 writes against the frozen schema; W5 read-through reconciles; bilingual structural check |

## Completion criteria for 2.5.0

- `tools/theme-editor` builds with `werror=1` and links only public headers;
  `make -j2 theme-editor` and `make -j2 check` are green.
- The JSON schema v1 round-trips both default palettes, exports canonical JSON
  deterministically, and rejects the documented malformed cases.
- The C export compiles as C23 against `<pt/pt.h>` and applies all 38 roles and
  10 metrics.
- WCAG checks reproduce the `docs/theming.md` floors within 0.01, expose
  `--check`/`--strict` exit codes, and do not require a display.
- The GUI smoke test under nested niri edits a role, updates the preview,
  imports/exports, and closes without protocol errors.
- `api/pt.api` remains **587 / 968**, `nm -D` shows 587 `pt_*` symbols and no
  `te_*`, SONAME is `libpt.so.1`, and the binding parity stays 587/587.
- `tools/gen-api-reference.py --check` is clean,
  `tools/check-bilingual-docs.py` reports 11 pairs, and `git status` is clean.
- The serial sanitizer pass is green; no `-j8` build is used.
- `docs/ROADMAP.md`, `docs/release.md` and this design carry the final status,
  and the deferred register records rich image paste, variable row heights and
  the SVG subset with their re-open triggers.

## Deferred register for this proposal

| Item | Why deferred | Re-open trigger |
| --- | --- | --- |
| Rich clipboard paste of inline image runs | Requires multi-MIME clipboard storage, a rich serialization, new public API and edits to `platform.c`/`widget.c`; a core feature, not a tool | A product requirement for rich paste plus a dedicated MIME/API design and a clipboard hot-spot budget |
| Variable row heights | Changes measurement, recycling, scroll mapping and hit-testing in list, table and tree | A concrete model need and a layout/recycling design with a performance guard |
| Remaining SVG subset | Vendored parser subset and security surface; text, raster images, filters, masks, clipPath, use/symbol, markers, animation, full CSS cascade are ignored by design | A real icon set that cannot be expressed with the current subset |
| Multi-scheme theme bundle (v2) | v1 deliberately ships one scheme per file; dual editing doubles the UI and test matrix | Users need one file that carries both schemes and a v2 schema is agreed |
| Live desktop theme import (portal/GSettings) | Out of scope for a token editor; adds a platform dependency and no core API | A cross-desktop preference API with a stable contract |
| color-management-v1 / Vulkan/EGL / X11 / xdg-foreign v1 | Existing 2.4.0 deferrals with measured or protocol evidence; nothing changed | Unchanged from `docs/ROADMAP.md:2719-2735` |