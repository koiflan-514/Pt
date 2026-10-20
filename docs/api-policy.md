# API compatibility policy

This document defines how the Pt C API may change before and after 1.0, how
deprecations are handled, and how the checked-in API baseline and binding
parity report are maintained. It applies to the public headers in
`include/pt/`, the shared library `libpt.so`, and the C++, Python and Java
bindings shipped in this repository.

## What is frozen

The public headers are the source of truth. For every declared entry point the
following is part of the API contract:

- the C function name and its signature, including parameter types, constness
  and return type;
- the numeric value of every public enum constant and numeric `#define`
  (`pt_auto`, `pt_color_transparent`, `pt_list_index_none`,
  `pt_table_column_none`, `pt_row_key_none`, `PT_TABLE_MAX_SORT_COLUMNS`);
- the layout of public structs (`pt_gesture_event`, `pt_stylus_event`,
  `pt_drop_target`, `pt_transition`) including field order and types;
- callback typedef names and their parameter lists;
- opaque handle names and the typedef aliases (`pt_color`).

Private declarations, internal headers under `src/`, and undocumented symbols
are not API. Only functions annotated with `pt_api` are exported; the audit
rejects any additional `pt_*` symbol in the shared library.

## Header contracts at the freeze

Every public header opens with a `Header contract` block that states, for that
family, the threading rule, ownership and lifetime of returned pointers,
callback lifetime, error behavior and any family-specific exception. The
canonical wording lives in `include/pt/base.h`; the other headers add only what
their family needs (popup grabs in `window.h`/`overlay.h`, offer and stream
lifetimes in `dnd.h`, the text-input state-push moments in `widget.h`, and the
caller-owned storage of `motion.h`). `tools/gen-api-reference.py` extracts those
blocks into the module preamble of [api-reference.md](api-reference.md) and the
per-function contract notes continue to cover ownership, valid widget kinds,
callback threading and failure behavior.

`tests/test_abi.c` turns the append-only rules into a build gate. It pins the
numeric value of every public enum constant, every state and modifier bit,
every sentinel (`pt_list_index_none`, `pt_table_column_none`, `pt_row_key_none`,
`pt_color_transparent`, `PT_TABLE_MAX_SORT_COLUMNS`), the field order of the
public structs and the version-macro agreement. Renumbering a widget kind,
moving an accessibility state bit, inserting a theme token in the middle of the
table or reordering a callback struct now fails a headless test rather than
silently breaking an application built against 1.0. Adding a value at the end
and extending a struct at the end still require a reviewed `make api-update`.

The audit's baseline comparison is the second half of the guard: a changed
numeric value or struct layout appears as a `changed` entry and fails
`make api-check` until the baseline is deliberately refreshed.

## Versioning and ABI

Pt uses semantic versioning with a pre-1.0 caveat.

- Patch releases (0.x.Y) must remain source- and ABI-compatible: no removals,
  no signature changes, no enum value changes, no struct layout changes, no
  new exported symbols other than additions approved for that patch.
- Minor releases before 1.0 (0.X.0) may break the ABI, but any break must be
  listed in the release notes and mirrored by a baseline update. Additions are
  expected to be source-compatible. The project aims to make minor releases
  compatible unless a documented design defect forces a change.
- From 1.0 on, the same major version keeps source and ABI compatibility.
  Removing or changing an existing declaration requires a major version and a
  SONAME bump.
- The SONAME is `libpt.so.0` for the entire 0.x series and becomes
  `libpt.so.1` at 1.0.0. The freeze removes the two symbols deprecated in 0.9,
  so the 1.0 ABI is a deliberate subset of the 0.10 ABI rather than a strict
  superset: the object name has to change so a binary linked against
  `libpt.so.0` cannot be loaded silently against the frozen library.
- From 1.0 on, removing or changing an existing declaration requires a major
  version and a matching SONAME bump. Additions inside 1.x keep `libpt.so.1`.
- Enum constants are append-only. New values get fresh numbers; existing
  numbers are never reused or reordered. Structs are extended only at the end
  and only in a minor release that also records the new layout in the baseline.
  Applications compiled against an older header continue to work because
  handles are opaque and structs are only used for input events.

## Deprecation policy

1. A deprecated declaration stays in the headers and remains functional for at
   least one minor release before removal. Before 1.0 removal follows the
   minor-release rules above; after 1.0 removal requires a major release.
2. Deprecations are announced in the release notes with the replacement
   spelling and the planned removal version. Headers gain a short comment and,
   once available, a compiler deprecation attribute.
3. Bindings mirror deprecations with their native mechanism (`[[deprecated]]`
   for C++, `DeprecationWarning` for Python, `@Deprecated` for Java) in the
   release that deprecates the C symbol.
4. Silent changes in behavior that are observable through the API are treated
   as compatibility events: they need a release note and, if they can break
   existing applications, the same deprecation window.

### Worked example

Suppose a release introduces `pt_slider_set_range(slider, minimum, maximum)`
and decides that the old `[0, 1]`-only setter should eventually be replaced.
The change is additive, so that release can ship both.

1. **Header.** Keep the old declaration and document the replacement and the
   planned removal point:

   ```c
   /* Deprecated since 0.x; use pt_slider_set_range. Removal is planned for
    * 1.0 at the earliest. */
   #if defined(__GNUC__) || defined(__clang__)
   __attribute__((deprecated("use pt_slider_set_range")))
   #endif
   pt_api void pt_slider_set_value(pt_widget *slider, float value);
   ```

2. **Baseline and reference.** `make api-check` reports a baseline addition and
   a reference change, not an error, because the declaration still exists.
   `make api-update` freezes the new function and regenerates
   `docs/api-reference.md`, which now shows both entries.

3. **Release note.** State: "`pt_slider_set_value` is deprecated in 0.x. Use
   `pt_slider_set_range`; the old setter still maps to `[0, 1]` and will be
   removed no earlier than 1.0."

4. **Bindings.** In the same release, mark the C++ wrapper `[[deprecated]]`,
   raise `DeprecationWarning` from the Python setter, and add `@Deprecated` to
   the Java method. Wrappers for the replacement ship with tests.

5. **Removal.** No earlier than the next minor release (or the next major
   release after 1.0), delete the declaration, implementation, wrapper methods
   and tests in one change; `make api-update` records the baseline removal and
   the release notes call it out as breaking. Removal before 1.0 requires a
   documented reason under the minor-release rules above.

The process deliberately separates deprecation from removal: a deprecated
symbol is never dropped in the same release that announces it, and the audit
must show the removal as an explicit baseline diff rather than an accidental
drift.

### The 0.9.0 deprecation and its 1.0 removal

0.9.0 deprecated `pt_window_begin_text_drag` and `pt_window_begin_uri_drag` in
favour of `pt_window_begin_drag`, which already covers both through a
`text/plain;charset=utf-8` entry and a `text/uri-list` entry. The cycle was run
through every step of the process above:

- the declarations stayed in `include/pt/dnd.h`, marked with the internal
  `pt_deprecated("use pt_window_begin_drag ...")` attribute macro and a comment
  naming the removal window;
- `make api-check` treated them as unchanged baseline entries, because the
  symbols still existed and were still exported; `pt_deprecated` is an attribute
  macro, not a function, and never enters the baseline;
- all three bindings marked their wrappers in the same release: `[[deprecated]]`
  in C++, `warnings.warn(..., DeprecationWarning, stacklevel=2)` in Python and
  `@Deprecated` with a Javadoc `@deprecated` tag in Java;
- the C++ and Java wrappers still called the deprecated C entry points —
  suppressed by a scoped `#pragma GCC diagnostic` in C++ and by a direct
  `MethodHandle` bind in Java — so the textual parity scan saw the name and
  the two functions stayed inside the parity gate (374/374 at 0.9.0, 414/414 at
  0.10.0) instead of being pushed into the allowlist; the in-tree tests kept
  calling them too, the C++ ones under the
  same scoped pragma and the Python ones inside `assertWarns(DeprecationWarning)`,
  so a future accidental removal of the warning fails the suite;
- `docs/release.md` recorded the deprecation and its removal window.

**Removal window (recorded at 0.10.0, executed at 1.0.0).** Both functions were
removed by the 1.0 release rather than shipped into `1.0.0`: a removal after 1.0
needs a major release, so carrying them through `1.0.0` would have kept them for
the whole 1.x series. The freeze change deleted
the two declarations from `include/pt/dnd.h` and their implementations in
`src/dnd.c` — the only code behind them was the `text/plain;charset=utf-8` call
and the CRLF join, both of which a caller now expresses through
`pt_window_begin_drag` — together with the three wrappers, the
`pt_deprecated` attributes, the scoped pragmas, the `assertWarns` blocks, the
Java `@Deprecated` methods and their `MethodHandle` binds in one change. The
test call sites moved to `pt_window_begin_drag` instead of disappearing, so the
headless "no drag source" path is still asserted; the five argument guards of
the old URI joiner became three, since composing the payload is now the caller's
job, and the C suite moved from 457,457 checks to 457,455 at that point. The
later text-interface additions and the behavior-only empty-viewport marquee
follow-up bring the 1.0 gate total to 457,584; the marquee added no API.

`make api-update` recorded the removal as the explicit diff the policy asks
for: against the 0.10.0 baseline the audit reported
`726 entries; 2 removed, 0 changed, 0 added` and named both functions as an
error, and the refreshed baseline reads
`724 entries; 0 removed, 0 changed, 0 added` with `412 pt_* symbols; 0 missing,
0 unexpected` exported and `412/412` parity in each binding. The 0.6 per-round
surface dropped from 125 functions to 124. `tools/release-check.sh`, which
derives its deprecation inventory from the `pt_deprecated(` attributes in the
headers, now finds none.

Nothing else observable to an existing application changed: the surviving entry
point starts the same drag with the same payload encoding.

## API baseline and symbol audit

`api/pt.api` is the reviewed baseline of every public name. It is generated
from the headers by `tools/api-audit.py` and checked into the source tree. The
audit:

- parses functions, enums, callback typedefs, public structs, opaque types,
  aliases and numeric defines from `include/pt/*.h`;
- compares the result with the baseline; removals, signature or value changes
  and unapproved additions are errors;
- reads `nm -D --defined-only build/libpt.so` and verifies that exactly the
  declared functions are exported and no other `pt_*` symbol leaks;
- verifies the frozen 0.5 data-view and motion names still exist;
- scans the C++, Python and Java sources and fails on any missing wrapper;
- verifies that `docs/api-reference.md` matches the headers byte for byte;
- writes `build/api-report.txt` with export, baseline, reference and
  binding-parity data;
- runs as part of `make check`, so CI and release validation cannot skip it.

`docs/api-reference.md` is generated by `tools/gen-api-reference.py` from the
public headers. It contains one entry per function with its signature, header
documentation and generated contract notes for ownership, valid widget kinds,
callback threading and failure behavior, plus an appendix of public enums,
callbacks, structs, opaque handles and constants. `make api-docs` rewrites it;
`make api-check` fails with a diff when it is stale. Do not edit the generated
file by hand.

Workflow for an intentional API change:

```sh
# 1. Change the headers and implementation.
# 2. Review the report; additions and changes must be deliberate.
make api-check
# 3. Freeze the reviewed surface and regenerate the reference.
make api-update
```

`make api-update` rewrites `api/pt.api` and `docs/api-reference.md` from the
headers. It is never run implicitly by `make check` or by the build; a stale
baseline or reference must fail loudly.

## Binding parity

The C++ header (`bindings/cpp/pt/pt.hpp`), Python module
(`bindings/python/pt.py`) and Java FFM binding
(`bindings/java/src/pt/Pt.java`) are part of the supported surface. The audit
scans comment-stripped sources for every declared function name and reports
per-language coverage plus the missing names.

The policy is:

- every declared function must be wrapped by all three bindings; this is a hard
  gate and the audit fails the build on any missing name;
- an intentionally C-only function needs an entry in the reviewed allowlist in
  `tools/api-audit.py` and a reason in this document; the allowlist is still
  empty at 0.10.0;
- every function added in a release ships with all three wrappers and tests in
  the same change;
- parity is reported as counts and as the explicit missing list in
  `build/api-report.txt`, so a regression names the exact function;
- at 1.0 the parity report must show zero missing wrappers and an empty
  allowlist.

At the 0.10 baseline the report showed 414/414 for C++, Python and Java, with no
missing or allowlisted functions. The 0.6 prefix family — reported as 125
functions then — the 0.7
addition surface (19 functions), the 0.8 streamed-drag and styled-text surface
(19 functions), the 0.9 theme, chip/skeleton and data-view editing surface
(44 functions) and the 0.10 combobox and multi-row selection surface (40
functions) were each complete in all three wrappers. After the freeze removals
the same report reads 412/412 with an empty allowlist, the 0.6 prefix family at
124 functions and the other four surfaces unchanged. The 0.7 gate
closed the legacy 28/5/21 C++/Python/Java gaps that existed at 0.6.

### Frozen 0.5 data-view and motion names

The following names were frozen while building the 0.6 table so the
virtualized-list and animation contracts cannot drift silently:

`pt_list_view_create`, `pt_list_view_set_row_count`,
`pt_list_view_get_row_count`, `pt_list_view_set_row_height`,
`pt_list_view_get_row_height`, `pt_list_view_set_on_bind_row`,
`pt_list_view_set_on_select`, `pt_list_view_set_on_activate`,
`pt_list_view_get_selected`, `pt_list_view_set_selected`,
`pt_list_view_refresh`, `pt_list_view_scroll_to_row`, `pt_ease`,
`pt_ease_value`, `pt_transition_init`, `pt_transition_to`,
`pt_transition_advance`, `pt_transition_set`, `pt_transition_active`,
`pt_app_set_reduced_motion`, `pt_app_get_reduced_motion`.

The 0.6 table and tree reuse the list row recycler instead of replacing it;
their additions are separate functions and do not change these signatures.

## 1.0 readiness checklist

- Headers contain no draft or experimental markers; every exported symbol is
  documented and covered by the baseline.
- The audit reports zero missing or unexpected exports and a reviewed
  `api/pt.api` diff for the release.
- `docs/api-reference.md` matches the headers and covers all public functions,
  enums, callback typedefs, structs and opaque handles.
- All three bindings report 100% parity with an empty allowlist; each public
  function has at least one binding test or is exercised by a shared example.
- Deprecations from the 0.x series have either been removed through the
  documented process or remain functional with replacements; the release notes
  list every deprecation and its removal window. 0.9.0 deprecated
  `pt_window_begin_text_drag` and `pt_window_begin_uri_drag` in favour of
  `pt_window_begin_drag`, and the 1.0 freeze executed the recorded window rather
  than shipping them into `1.0.0`: after 1.0 a removal needs a major release, so
  keeping them through `1.0.0` would have made them permanent for the whole 1.x
  series. The freeze change deleted the two declarations, their implementations
  and the C++, Python and Java wrappers in one change, and `make api-update`
  recorded the pair as a `2 removed` baseline diff. The header inventory is now
  empty: nothing is deprecated, so a 1.0 application has no migration work
  beyond these two calls.
- `tools/release-check.sh` reports zero failures on the tree being frozen. It
  checks version agreement across the Makefile, `include/pt/base.h`, the README
  and `docs/api-reference.md`, the SONAME rule, draft markers in the public
  headers, the audit result and binding parity with an empty allowlist,
  reference coverage for every baseline function, the deprecation inventory and
  the presence of the release examples. It cannot decide the freeze commit and
  tag (see the last item) or the live-compositor smoke tests in
  [release.md](release.md).
- `make check`, `make check-bindings`, a `werror=1` build, sanitizer runs, the
  CI-equivalent local script and the clean-tarball build pass on the frozen
  tree.
- Installed consumers for C, C++, Python and Java build and run against a
  staged prefix; `pkg-config --modversion pt` reports the release version and
  the SONAME remains compatible.
- `make dist` is reproducible with `SOURCE_DATE_EPOCH`, produces the same
  archive digest for the same tree, and writes that digest as a `.sha256`
  sidecar next to the artifact instead of quoting it inside the release record.
- The freeze commit and annotated tag exist (currently pending because no Git
  author identity is configured on the build machine; Pt never changes Git
  configuration automatically).
