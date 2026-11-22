# Release process

Pt follows semantic versioning. The shared object carries SONAME `libpt.so.1`
from 1.0.0 onward; the 0.x series used `libpt.so.0`. From 2.0 the Makefile tracks
`ABI_MAJOR` separately from `VERSION`, so a release that only adds declarations
and corrects behavior may advance the version without bumping the SONAME. The
pkg-config version tracks `pt_version_string`.

## Version checklist

Before tagging a release:

1. Update `VERSION` in the Makefile and `pt_version_major`,
   `pt_version_minor`, `pt_version_patch` and `pt_version_string` in
   `include/pt/base.h`.
2. Update the status line and any new limitations in `README.md`,
   `docs/ROADMAP.md`, `docs/performance.md` and `docs/comparison.md`.
3. Run the validation matrix below from a clean tree. For every 1.x release
   also run `tools/release-check.sh --full`, which turns the machine-verifiable
   half of the checklist in [api-policy.md](api-policy.md) into a pass/fail
   report.
4. Produce the tarball with `make dist`, which also writes
   `pt-<version>.tar.gz.sha256` beside it, verify that a rebuild with the same
   `SOURCE_DATE_EPOCH` reproduces the digest, and build the release from an
   extracted copy. `tools/ci.sh` performs that sequence and fails if the sidecar
   does not match the archive it describes.
5. Commit the release state and tag it (`git tag -a "v$(VERSION)" -m "Pt
   $(VERSION)"`, with `VERSION` read from the Makefile).
   The repository owner must configure the Git identity and signing policy;
   build scripts never modify Git configuration.
6. Publish the tarball, checksum and release notes.
7. Bump `VERSION` to the next development version and update the version macros
   in a follow-up commit.

## Validation matrix

`tools/ci.sh` is the CI-equivalent local gate. It uses an isolated
`build/ci/` tree and runs:

- a clean `werror=1` build of the library, C/C++ examples and every C test;
- `make check` (C suites, C++/Python/Java binding suites, exported-symbol and
  API-baseline audit, 564/564 binding parity, the 0.6-0.10 surface audits,
  generated reference check);
- `make perf-check` (dense, startup, data-view and guard benchmarks);
- `tools/install-check.sh` (staged install, relocatable pkg-config, SONAME,
  exported ABI and installed C/C++/Python/Java consumers, then uninstall);
- a `sanitize=1` build with ASan/UBSan and LeakSanitizer across the C suites
  and the C++ test, followed by the Python and Java suites against the
  sanitized library with the ASan runtime preloaded;
- two `make dist` runs at a fixed `SOURCE_DATE_EPOCH` compared by SHA-256, with
  the generated `.sha256` sidecar checked against the archive, then an
  extracted-tarball build and `make check`;
- `make check-wayland` when `WAYLAND_DISPLAY` is set: it starts a fullscreen
  nested niri through `tools/nested-wayland.sh` when niri is installed, focuses
  the nested window in the parent session, and runs the live smoke test with the
  popup checks and the 5 s focused-entry idle guard; it falls back to a direct
  run when niri is absent;
- `make check-typing` when `WAYLAND_DISPLAY` is set and `niri` is installed:
  it starts a nested compositor, focuses its window in the parent session and
  asserts that keys injected through a virtual keyboard reach a focused entry
  while text-input-v3 is enabled;
- `make check-ime-live` when `WAYLAND_DISPLAY` is set and a parent niri,
  fcitx5 with pinyin and a private session bus are available: it starts a
  nested niri and an isolated fcitx5, injects into the parent display, and
  asserts the live preedit sequence and the committed CJK text through
  text-input-v3; without a parent niri it skips with exit code 0.

```sh
tools/ci.sh              # full local CI
tools/ci.sh --quick      # skips sanitizers and the tarball rebuild
JOBS=8 tools/ci.sh       # limit parallelism
```

The script destroys and recreates only its own build directory. For a slower
machine, raise the performance guard ceilings with
`PT_PERF_GUARD_SCALE=2 tools/ci.sh`.

### Manual pieces

The individual commands remain available:

```sh
make clean
make -j"$(nproc)" werror=1
make check werror=1
make check-bindings werror=1
make check-wayland
make check-typing
make perf-check
tools/install-check.sh build
```

Sanitizer pass without the script:

```sh
make -j"$(nproc)" BUILD=build/sanitize werror=1 sanitize=1 check-sanitize
make BUILD=build/sanitize werror=1 sanitize=1 lib
```

For Python and Java, a sanitizer-instrumented `libpt.so` loaded into an
uninstrumented interpreter needs the ASan runtime preloaded:

```sh
ASAN_LIB="$(cc -print-file-name=libasan.so)"
LD_PRELOAD="$ASAN_LIB" ASAN_OPTIONS=detect_leaks=0 \
    PT_LIBRARY="$PWD/build/sanitize/libpt.so" PYTHONPATH=bindings/python \
    PT_HEADLESS=1 python3 -m unittest discover -s bindings/python -p 'test_*.py'
LD_PRELOAD="$ASAN_LIB" ASAN_OPTIONS=detect_leaks=0 \
    PT_HEADLESS=1 java --enable-native-access=ALL-UNNAMED \
    -Dpt.library="$PWD/build/sanitize/libpt.so" -cp build/java pt.PtTest
```

Leak detection is disabled for the interpreted runs because the JVM, CPython
and Fontconfig intentionally retain global allocations. Leak-sensitive C/C++
paths are covered by the instrumented test binaries.

### Install and tarball check

`tools/install-check.sh build` performs the staged install, SONAME, ABI,
pkg-config and four-language consumer checks, then verifies that `make
uninstall` leaves no installed files. The equivalent manual sequence is:

```sh
V="$(sed -n 's/^VERSION := //p' Makefile)"   # the VERSION in the Makefile
STAGE="$(mktemp -d)"
make install PREFIX=/usr DESTDIR="$STAGE"
test -f "$STAGE/usr/lib/libpt.so.$V"
readelf -d "$STAGE/usr/lib/libpt.so.$V" | grep SONAME
PKG_CONFIG_PATH="$STAGE/usr/lib/pkgconfig" pkg-config --modversion pt
PKG_CONFIG_PATH="$STAGE/usr/lib/pkgconfig" pkg-config --cflags --libs pt

SOURCE_DATE_EPOCH=1767225600 make dist
cat "build/dist/pt-$V.tar.gz.sha256"
SOURCE_DATE_EPOCH=1767225600 make dist
# the second digest in the sidecar must equal the first

TARBALL_BUILD="$(mktemp -d)"
tar -xzf "build/dist/pt-$V.tar.gz" -C "$TARBALL_BUILD"
cd "$TARBALL_BUILD/pt-$V"
make -j"$(nproc)" werror=1
make check werror=1
```

`make dist` stages the working tree, then creates a GNU tar stream with
`--sort=name`, a fixed `--mtime`, numeric owner/group zero and `gzip -9n`.
`SOURCE_DATE_EPOCH` defaults to the last Git commit time, or to 2026-01-01
(1767225600) when the repository has no commits. Two invocations with the same
epoch produce identical bytes as long as the source tree is unchanged.
`make dist` also writes `pt-$V.tar.gz.sha256` beside the archive; the sidecar
lives outside the tarball because a tarball cannot contain its own digest, and
publishing it next to the artifact is preferable to quoting a digest in the
release record, since the archive contains the record itself.
`pt.pc` uses `${pcfiledir}` so it is relocatable and correct under
`DESTDIR`. `make install` also installs the C++ header and the Python/Java
binding sources under the prefix.

### Example smoke test

With a live Wayland session:

```sh
timeout 2 ./build/examples/c/hello
timeout 2 ./build/examples/c/gallery
timeout 2 ./build/examples/c/combobox
timeout 2 ./build/examples/cpp/counter
timeout 2 ./build/examples/cpp/workspace
timeout 2 ./build/examples/cpp/combobox
timeout 2 make run-python
timeout 2 make run-python-workspace
timeout 2 make run-python-combobox
timeout 2 make run-java
timeout 2 make run-java-workspace
timeout 2 make run-java-combobox
```

Exit status 124 means the example was still running when the timeout expired,
which is the expected result for an interactive window. Any earlier failure or
a crash is a release blocker. Repeat the C and C++ examples under 1x and a
fractional desktop scale if the test machine exposes both.

### 1.0 readiness gate

`tools/release-check.sh` automates the checklist in
[api-policy.md](api-policy.md) that can be decided from the tree alone, so the
freeze review starts from a report rather than from a blank checklist. It reads
the Makefile, so it needs no build flags:

```sh
tools/release-check.sh          # report only, uses build/libpt.so
tools/release-check.sh --full   # runs tools/ci.sh --quick first
PT_RELEASE_BUILD=build/ci/normal tools/release-check.sh
```

The sections, in order:

| Check | Failure condition |
| --- | --- |
| Version agreement | `VERSION`, `pt_version_string`, the README status line and the `docs/api-reference.md` header disagree, or the SONAME does not match the series (`libpt.so.0` for 0.x, `libpt.so.1` for 1.x) |
| Public header surface | any `include/pt/*.h` still carries `TODO`, `FIXME`, `XXX`, `experimental`, `pt_draft` or `PT_DRAFT` |
| API baseline | the function count quoted in the README or the generated reference differs from the `func` entries in `api/pt.api` |
| Audit and binding parity | `tools/api-audit.py` exits non-zero, or any language reports less than `100.0%`, a non-zero missing count, or a non-empty allowlist |
| Generated reference coverage | a baseline function name is absent from `docs/api-reference.md` |
| Deprecation inventory | a `pt_deprecated` symbol is not tracked in both `docs/api-policy.md` and `docs/release.md`, or is not wrapped by one of the three bindings |
| Examples | a current-round example file or its Makefile target is missing |

Anything a script cannot decide locally is printed as an `open` item instead of
being silently skipped, and the script exits 1 as soon as one `FAIL` line was
printed. The open items are the ones that need a person: the freeze commit and
annotated tag (the repository owner supplies the Git identity; Pt never changes
Git configuration), and, when `WAYLAND_DISPLAY` is present, `make check-wayland`,
`make check-typing` plus the live example smoke test above. While a deprecation
is still live the inventory adds a third open item naming its removal window; the 1.0 freeze
removed the two deprecated drag entry points, so on this tree that section
reports `nothing is deprecated; the removal window is closed` and the item is
gone. A green report means the automated
gates pass and the remaining `open` lines still need a human decision; it does
not by itself certify the release.

## 0.1.0 validation record (2026-10-18)

Environment:

- CachyOS, Linux 7.2.9-2-cachyos, niri 26.04 on Wayland (`wayland-1`).
- GCC 16.2.1, GNU Make, Python 3.14.8, OpenJDK 25.0.2.
- wayland-client 1.26.0, xkbcommon 1.13.2, fontconfig 2.18.3,
  FreeType 26.7.20, HarfBuzz 14.6.0, zlib 1.3.1.

Results:

| Check | Result |
| --- | --- |
| `make -j"$(nproc)" werror=1` from clean | pass, no warnings |
| C test suite | 214 checks in 7 binaries |
| C++ binding (`test_cpp`) | 16 checks |
| Python binding (`unittest`) | 7 tests |
| Java binding (`PtTest`) | 15 checks |
| ASan + UBSan (C/C++ tests, leak detection on `test_cpp`) | pass, no reports |
| ASan preload for Python and Java suites | pass, no reports |
| Live examples: C hello, C gallery, C++ counter, Python, Java | ran 2 s each, stopped by timeout (124) |
| Tarball clean extraction: build, C tests, C++/Python/Java bindings | pass |
| Staged install, relocatable pkg-config, SONAME | pass |
| Installed C and C++ consumers | compiled and ran |
| `make uninstall` | removed exactly the installed files |

The archive checksum is published next to the artifact as
`pt-0.1.0.tar.gz.sha256`. A tarball cannot contain its own digest, so the
checksum file is part of the release upload rather than the source tree.

The only uncompleted release step is the initial Git commit and annotated
`v0.1.0` tag. The repository owner must supply the Git author identity; see
the version checklist above.

## 0.2.0 validation record (2026-10-18)

Environment: same machine and toolchain as the 0.1.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2).

Results:

| Check | Result |
| --- | --- |
| `make -j"$(nproc)" werror=1` from clean | pass, no warnings; builds library and examples (`all` is the default goal) |
| C test suite | 302 checks in 8 binaries, including live Wayland |
| C++ binding (`test_cpp`) | 34 checks |
| Python binding (`unittest`) | 10 tests |
| Java binding (`PtTest`) | 33 checks |
| ASan + UBSan (C suite, C++ tests with leak detection) | pass, no reports |
| ASan preload for Python and Java suites | pass, no reports |
| Live examples: C hello, C gallery, C++ counter, Python, Java | ran 2 s each, stopped by timeout (124) |
| Tarball clean extraction: build, C tests, C++/Python/Java bindings | pass |
| Staged install, relocatable pkg-config (`0.2.0`), SONAME `libpt.so.0` | pass |
| `make uninstall` | removed exactly the installed files |

Headless benchmark on the same machine: 1,506-node tree, layout 2.871 ms,
full paint 16.262 ms, first 800x600 frame 13.06 ms, idle 500 ms CPU 1.377 ms.
The archive checksum is generated with the artifact as
`pt-0.2.0.tar.gz.sha256` and does not live in the source tree, because adding
it would change the archive.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.3.0 validation record (2026-10-18)

Environment: same machine and toolchain as the 0.2.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2).

Results:

| Check | Result |
| --- | --- |
| `make -j"$(nproc)" werror=1` from clean | pass, no warnings; library, C/C++ examples and 154 exported symbols |
| C test suite | 485 checks in 11 binaries, including live Wayland |
| C++ binding (`test_cpp`) | 49 checks |
| Python binding (`unittest`) | 13 tests |
| Java binding (`PtTest`) | 54 checks |
| ASan + UBSan (C suite, C++ tests with leak detection) | pass, no reports |
| ASan preload for Python and Java suites | pass, no reports |
| Live examples: C hello, C gallery, C++ counter, Python, Java | ran 3 s each, stopped by timeout (124) |
| Renderer and font PNG snapshots, offscreen overlay snapshots | reviewed; menu, modal scrim, dialog and toast render correctly |
| Tarball clean extraction: build, C tests, C++/Python/Java bindings | pass |
| Staged install, relocatable pkg-config (`0.3.0`), SONAME `libpt.so.0` | pass |
| Installed C and C++ consumers | compiled and ran headless |
| `make uninstall` | removed exactly the installed files and directories |
| `make dist` | `pt-0.3.0.tar.gz` created; checksum recorded next to the artifact |

Headless benchmark on the same machine: 1,506-node tree, layout 2.280 ms, full
paint 15.975 ms, localized one-row damage paint 1.152 ms (7.2% of full paint),
first 800x600 frame 12.48 ms, idle 500 ms CPU 1.418 ms. The validation archive's
SHA-256 is published as `pt-0.3.0.tar.gz.sha256` with the artifact; it is not
embedded in this file because the checksum changes whenever the archive is
rebuilt.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.4.0 validation record (2026-10-18)

Environment: same machine and toolchain as the 0.3.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2). A live session bus was available at `/run/user/1000/bus`.

Results:

| Check | Result |
| --- | --- |
| `make -j"$(nproc)" werror=1` from clean | pass, no warnings; library, C/C++ examples and 168 exported `pt_` symbols |
| C test suite | 757 checks in 14 binaries, including live Wayland and a live AT-SPI2 bridge |
| C++ binding (`test_cpp`) | 55 checks |
| Python binding (`unittest`) | 14 tests |
| Java binding (`PtTest`) | 60 checks |
| ASan + UBSan (C suite, C++ tests with leak detection) | pass, no reports after freeing the temporary paint canvas in the bridge suite |
| ASan preload for Python and Java suites | pass, no reports |
| Live examples: C hello, C gallery, C interaction, C++ counter, Python counter, Java HelloPt | each ran until the timeout stopped it |
| Tarball clean extraction: build, C tests, C++/Python/Java bindings | pass |
| Staged install, relocatable pkg-config (`0.4.0`), SONAME `libpt.so.0` | pass |
| Installed C and C++ consumers | compiled against `pt.pc` and ran headless |
| `make uninstall` | removed every staged file (0 remaining) |
| `make dist` | `pt-0.4.0.tar.gz` created; SHA-256 `ce9cfb85bddcee664ba49c625650f35b2d729cd9858d239d1cfef8560e525a2a` |

Headless benchmark on the same machine: 1,506-node tree, layout 2.202 ms, full
paint 14.071 ms, localized one-row damage paint 1.027 ms (7.3% of full paint),
first 800x600 frame 16.13 ms, idle 500 ms CPU 1.585 ms. The checksum above was
generated with this archive; rebuilds produce a different digest, so publish
the `.sha256` file next to the artifact rather than relying on this record.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.5.0 validation record (2026-10-18)

Environment: same machine and toolchain as the 0.4.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2). A live session bus was available at `/run/user/1000/bus`.

Results:

| Check | Result |
| --- | --- |
| `make -j"$(nproc)" werror=1` from clean | pass, no warnings; library, C/C++ examples and 208 exported `pt_` symbols |
| C test suite | 1,019 checks in 18 binaries, including live Wayland |
| C++ binding (`test_cpp`) | 86 checks |
| Python binding (`unittest`) | 18 tests |
| Java binding (`PtTest`) | 84 checks |
| ASan + UBSan (C suite, C++ tests with leak detection) | pass, no reports |
| ASan preload for Python and Java suites | pass, no reports |
| Live examples: C hello, gallery, notes, interaction; C++ counter; Python counter; Java HelloPt | each ran until the 2 s timeout stopped it |
| Tarball clean extraction: build, C tests, C++/Python/Java bindings | pass |
| Staged install, relocatable pkg-config (`0.5.0`), SONAME `libpt.so.0` | pass |
| Installed C and C++ consumers | compiled against `pt.pc` and ran headless |
| `make uninstall` | removed every staged file (0 remaining) |
| `make dist` | `pt-0.5.0.tar.gz` created; SHA-256 published as `pt-0.5.0.tar.gz.sha256` next to the artifact |

Headless benchmark on the same machine: 1,506-node tree, layout 2.235 ms, full
paint 15.609 ms, localized one-row damage paint 1.208 ms (7.7% of full paint),
first 800x600 frame 10.80 ms, idle 500 ms CPU 1.397 ms. Memory after the
benchmark: 67.1 MiB RSS / 25.1 MiB PSS. The digest is not embedded here because
it changes whenever the archive is rebuilt; publish the `.sha256` file with the
artifact.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.6.0 validation record (2026-10-18)

Environment: same machine and toolchain as the 0.5.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2). A live session bus was available at `/run/user/1000/bus`.

Results:

| Check | Result |
| --- | --- |
| `make -j"$(nproc)" werror=1` from clean | pass, no warnings; library, C/C++ examples and 292 exported `pt_` symbols |
| C test suite | 1,474 checks in 21 binaries, including live Wayland |
| C++ binding (`test_cpp`) | 147 checks |
| Python binding (`unittest`) | 24 tests |
| Java binding (`PtTest`) | 142 checks |
| ASan + UBSan (C suite, C++ tests with leak detection) | pass, no reports |
| ASan preload for Python and Java suites | pass, no reports |
| Live examples: C hello, gallery, notes, interaction; C++ counter, workspace; Python counter, workspace; Java HelloPt, Workspace | each ran until the 2 s timeout stopped it (124) |
| Tarball clean extraction: build, C tests, C++/Python/Java bindings | pass |
| Staged install, relocatable pkg-config (`0.6.0`), SONAME `libpt.so.0` | pass |
| Installed C and C++ consumers | compiled against `pt.pc` and ran headless |
| `make uninstall` | removed all staged files and symlinks (17 files, 2 symlinks; 0 remaining) |
| `make dist` | `pt-0.6.0.tar.gz` created; SHA-256 published as `pt-0.6.0.tar.gz.sha256` next to the artifact |

Headless benchmark on the same machine: 1,506-node tree, layout 2.132 ms, full
paint 14.553 ms, localized one-row damage paint 1.058 ms (7.3% of full paint),
first 800x600 frame 11.29 ms, idle 500 ms CPU 1.083 ms. Memory after the dense
benchmark: 67,360 KiB RSS / 25,248 KiB PSS. The 10,000-row table plus 1,000-node
tree materializes 93 widgets, with 0.005 ms layout, 11.648 ms full paint,
0.178 ms one-cell damage paint (1.5% of full paint) and 41,852 KiB RSS / 13,407
KiB PSS. The digest is not embedded here because it changes whenever the
archive is rebuilt; publish the `.sha256` file with the artifact.

Validation found and fixed two defects before this record was finalized: a
column-title leak in `pt_table_set_column_count` reported by LeakSanitizer, and
a Wayland draw-reentrancy path where a table/list bind callback that updates a
cell during paint could recurse into the renderer. `test_wayland` now exercises
callback-driven cell text during a live draw.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.7.0 validation record (2026-10-18)

Environment: same machine and toolchain as the 0.6.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2). A live session bus and `wayland-1` were available. The full
`tools/ci.sh` gate ran on an isolated `build/ci/` tree.

Results:

| Check | Result |
| --- | --- |
| `tools/ci.sh` clean werror build | pass; library, C/C++ examples and 311 exported `pt_` symbols |
| C test suite | 455,964 checks in 22 binaries, including 454,380 randomized property checks and the live Wayland smoke test |
| C++ binding (`test_cpp`) | 205 checks |
| Python binding (`unittest`) | 25 tests |
| Java binding (`PtTest`) | 188 checks |
| Binding parity and API audit | 311/311 in C++/Python/Java, empty allowlist, baseline and generated reference match |
| ASan + UBSan (C suites, C++ with leak detection) | pass, no reports |
| ASan preload Python and Java suites | 25 tests and 188 checks, pass, no reports |
| `make perf-check` | all guard ceilings hold |
| Live examples: C hello, gallery, notes, interaction, data, accessibility; C++ counter, workspace; Python counter, workspace; Java HelloPt, Workspace | all ran until the 2 s timeout stopped them (124) |
| Staged install, relocatable pkg-config (`0.7.0`), SONAME `libpt.so.0` | pass |
| Installed C, C++23, Python and Java consumers | compiled against the staged install and ran headless |
| `make uninstall` | removed every staged file and symlink (0 remaining) |
| Reproducible `make dist` | two runs at `SOURCE_DATE_EPOCH=1767225600` produced identical SHA-256; extracted tarball built and passed `make check` |

Headless benchmark on the same machine: 1,506-node tree, build 0.52 ms, layout
2.150 ms, full paint 14.734 ms, localized one-row damage paint 1.069 ms (7.3%
of full paint), 67,576 KiB RSS / 25,450 KiB PSS. The 10,000-row table plus
1,000-node tree materializes 93 widgets, with 0.005 ms layout, 10.917 ms full
paint, 0.163 ms one-cell damage paint (1.5% of full paint) and 41,824 KiB RSS /
13,465 KiB PSS. The first 800x600 frame takes 11.15 ms and 500 ms of idle
event-loop time consumes 1.581 ms of CPU. The guard binary measured a 43.9 ms
dense 4K full repaint and a 3.0 ms large-tree scroll frame while materializing
13 of 10,200 nodes. The digest is not embedded here because it changes whenever
the source tree changes; publish the `.sha256` file with the artifact.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.8.0 validation record (2026-10-18)

Environment: the same machine and toolchain as the 0.7.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2), with a live session bus and `wayland-1` available. The full
`tools/ci.sh` gate ran on an isolated `build/ci/` tree.

Results:

| Check | Result |
| --- | --- |
| `tools/ci.sh` clean werror build | pass; library, C/C++ examples and 330 exported `pt_` symbols |
| C test suite | 456,749 checks in 24 binaries: 2,152 assertions across 21 headless suites, 178 live AT-SPI bridge checks, a 39-check live Wayland smoke test and 454,380 randomized property checks |
| C++ binding (`test_cpp`) | 242 checks |
| Python binding (`unittest`) | 31 tests |
| Java binding (`PtTest`) | 216 checks |
| Binding parity and API audit | 330/330 in C++/Python/Java with an empty allowlist; the 19-function 0.8 surface is complete in all three; baseline and generated reference match |
| ASan + UBSan + LeakSanitizer (C suites, C++ with leak detection) | pass after the span-buffer fix below |
| ASan preload Python and Java suites | 31 tests and 216 checks, pass, no reports |
| `make perf-check` | all guard ceilings hold, including 4K damage and the 100,000-row table scroll |
| Live examples: C hello, gallery, notes, interaction, data, accessibility; C++ counter, workspace; Python counter, workspace; Java HelloPt, Workspace | all ran until the 2 s timeout stopped them (124) |
| Staged install, relocatable pkg-config (`0.8.0`), SONAME `libpt.so.0` | pass |
| Installed C, C++23, Python and Java consumers | compiled against the staged install and ran headless |
| `make uninstall` | removed every staged file and symlink |
| Reproducible `make dist` | two runs at `SOURCE_DATE_EPOCH=1767225600` produced byte-identical archives; in the gate run the extracted tarball built and passed `make check`; SHA-256 is published as `pt-0.8.0.tar.gz.sha256` next to the artifact, not embedded here |

The first full run failed the sanitizer stage: LeakSanitizer reported 168 bytes
in five allocations still owned by `pt_widget_set_text_spans`. Two defects were
behind it. `pt_widget_free_tree` released `text`, `placeholder`, `preedit` and
the tooltip but never the span array, and a setter whose ranges all collapsed
after UTF-8 snapping kept its allocated copy while reporting a count of zero, so
the clear path could no longer reach it. Both are fixed, and `test_rich_text`
now asserts the dropped-everything case and destroys a widget that still holds
spans (198 to 202 checks).

The live Wayland suite also changed, without a behavior change. Its tooltip
check raced the compositor: a live `wl_pointer` enter replaces the synthetic
hover and cancels the pending tip, so a fixed 100 ms delay could be restarted
indefinitely, and an earlier revision failed when the pointer simply was not in
the window. The check now arms with no delay, follows the live hover when it
moves, and retries a bounded number of slices, so the assertion is about the
native popup surface rather than about where a mouse happened to be (39 checks,
stable across five consecutive live runs).

Headless benchmark on the same machine: the 1,506-node scene builds in 0.60 ms
and settles at layout 1.993 ms, full paint 13.017 ms and localized one-row
damage 0.935 ms (7.2% of full paint) with 67,412 KiB RSS / 19,007 KiB PSS. The
10,000-row table plus 1,000-node tree materializes 93 widgets and costs 0.003 ms
layout, 10.009 ms full paint and 0.151 ms one-cell damage (1.5%) with 41,660 KiB
RSS / 12,066 KiB PSS. Startup is a 12.51 ms first 800x600 frame (37,372 KiB RSS /
10,367 KiB PSS) and 500 ms of idle loop consumes 2.034 ms of CPU. The guard
binary measured dense layout 2.020 ms and dense paint 12.978 ms with 0.963 ms
one-row damage (7.419%, 61 nodes per full frame against 15 per damaged frame,
traversal share 24.590%), a 34.419 ms 4K full paint with 2.967 ms 4K one-row
damage (8.619%), a 2.334 ms scroll frame over 10,200 visible nodes with 13 row
widgets, and a 9.834 ms 100,000-row table scroll that materializes 140 widgets
and 22 rows with 0.112 ms one-cell damage. Timings move by tens of percent
between runs on this desktop, so only a breach of the ceilings in
`bench/guards.c` fails the gate. The digest is not embedded here
because it changes whenever the source tree changes; publish the `.sha256` file
with the artifact.

Not delivered this round: the M14 item "execute one deprecation through the
documented cycle". No 0.7 function became redundant during 0.8, so the
deprecation policy stays documented-but-unused and the first real deprecation
moves to M15 rather than being invented for the release notes.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.9.0 validation record (2026-10-18)

Environment: the same machine and toolchain as the 0.8.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2), with a live session bus, `wayland-1` and a real display. The full
`tools/ci.sh` gate ran on an isolated `build/ci/` tree and exited 0.

Results:

| Check | Result |
| --- | --- |
| `tools/ci.sh` clean werror build | pass; library, C/C++ examples, 374 exported `pt_` symbols, 0 missing and 0 unexpected |
| C test suite | 456,916 checks in 27 executables: 2,319 assertions across 24 headless suites, the 454,380-check randomized property suite, 178 live AT-SPI bridge checks and a 39-check live Wayland smoke test |
| C++ binding (`test_cpp`) | 286 checks |
| Python binding (`unittest`) | 32 tests |
| Java binding (`PtTest`) | 248 checks |
| Binding parity and API audit | 374/374 in C++/Python/Java with an empty allowlist; the 0.6 (118), 0.7 (19), 0.8 (19) and 0.9 (44) addition surfaces are each complete in all three; baseline `678 entries; 0 removed, 0 changed, 0 added`; generated reference matches the headers |
| ASan + UBSan + LeakSanitizer (`check-sanitize`: 24 headless suites, the property suite, the live bridge, the live Wayland test and C++ with leak detection) | pass, no reports |
| ASan-preload Python and Java suites | 32 tests and 248 checks, pass, no reports |
| `make perf-check` | all guard ceilings hold, including 4K full and 4K damaged frames, the 10,200-node tree scroll and the 100,000-row table seek |
| Live examples: C hello, gallery, notes, interaction, data, accessibility; C++ counter, workspace; Python counter, workspace; Java HelloPt, Workspace | all 12 opened on the live compositor and ran until the 2 s timeout stopped them; `run-a11y` printed live `selection_changed` events on the way out |
| Staged install, relocatable pkg-config (`0.9.0`), SONAME `libpt.so.0` | pass; exported ABI matched the checked-in baseline |
| Installed C, C++23, Python and Java consumers | compiled against the staged install and ran headless |
| `make uninstall` | removed every staged file and symlink |
| Reproducible `make dist` | two runs at `SOURCE_DATE_EPOCH=1767225600` produced byte-identical archives and the gate verified that `pt-0.9.0.tar.gz.sha256`, which `make dist` now writes beside the artifact, matches the digest it computes; the extracted tarball built with `werror=1` and passed `make check`, including the 374/374 audit. No digest is quoted here because the archive contains this record, so editing the record invalidates any value written into it |

The first gate run failed at the werror C++ test build, which is exactly the
check M15 asked for: marking `pt_window_begin_text_drag` and
`pt_window_begin_uri_drag` as deprecated made `bindings/cpp/test_pt.cpp` — the
suite that deliberately exercises those two wrappers — a `-Werror=deprecated-declarations`
failure. The fix is scoped rather than global: each call site is wrapped in
`#pragma GCC diagnostic push` / `ignored "-Wdeprecated-declarations"` / `pop`,
so the library and every other translation unit still fail on a new deprecated
use, and the audit continues to count both symbols inside 374/374 instead of
allowlisting them away. The wrapper bodies in `pt.hpp` carry the same scoped
pragma and call the deprecated C entry points directly, which keeps the textual
parity scan honest.

The other two bindings report the same event their own way. `Window.begin_text_drag`
and `Window.begin_uri_drag` in `bindings/python/pt.py` now call
`warnings.warn(..., DeprecationWarning, stacklevel=2)`, and the three Python
call sites assert it with `assertWarns(DeprecationWarning)` rather than being
deleted, so `python -W error::DeprecationWarning` fails for an application that
has not migrated while the parity scan still counts the wrappers. On the Java
side nothing is silenced: `javac -Xlint:all` prints a deprecation warning for
each of the two `@Deprecated` `Pt` methods at the test call sites, which is the
compilation record that the deprecation is visible to users.

`test_visuals` reports 60 checks in the source tree and 59 inside the extracted
tarball. The difference is one branch, not a skipped assertion: the harness
`mkdir`s its snapshot directory and asserts success or `EEXIST`, so the first
run in a fresh directory takes the success branch and every later run takes the
`EEXIST` branch. The PNG output and the frame-hash comparisons are identical.

Headless benchmark from the same gate run: the 1,506-node scene builds in 0.60 ms
and settles at layout 2.570 ms, full paint 14.898 ms and localized one-row
damage 1.114 ms (7.5% of full paint) with 67,520 KiB RSS / 18,837 KiB PSS. The
10,000-row table plus 1,000-node tree materializes 93 widgets and costs 0.008 ms
layout, 10.683 ms full paint and 0.161 ms one-cell damage (1.5%) with 41,800 KiB
RSS / 11,860 KiB PSS. Startup is a 10.36 ms first 800x600 frame (37,520 KiB RSS /
10,259 KiB PSS) and 500 ms of idle loop consumes 0.936 ms of CPU. The guard
binary in the same run measured dense layout 2.511 ms, dense paint 14.895 ms
with 1.138 ms one-row damage (7.641%, 61 nodes per full frame against 15 per
damaged frame, traversal share 24.590%), a 39.283 ms 4K full paint with
3.392 ms 4K one-row damage (8.636%, 2.64x the 1280x800 frame), a 2.814 ms scroll
frame over 10,200 visible nodes with 13 row widgets, and a 11.770 ms 100,000-row
table scroll that materializes 140 widgets and 22 rows with 0.132 ms one-cell
damage (1.118%). Widening the theme to 38 colors and 10 metrics with a
per-window override table did not move any ceiling; the lookup is two indexed
array reads. `docs/performance.md` quotes this run, and every guard still had at
least a 2x margin. Timings still move by tens of percent between runs on this
desktop, so only a breach of the ceilings in `bench/guards.c` fails the gate.

Not delivered this round: inline image or icon runs inside styled text, which
is carried into M16 as its own design task. The table marquee did ship — a
row-anchored rubber band that sweeps the range selection while dragging and
reports the final row — but it is table-only and selects by row index, so
empty-space start, geometric intersection and the same gesture on lists and
trees remain M16 work, recorded under "Not delivered" in `docs/ROADMAP.md`.
The theme editor remains a post-1.0 candidate.

The initial Git commit and annotated tag remain pending because the repository
has no configured author identity. Build scripts never change Git
configuration.

## 0.10.0 validation record (2026-10-18)

Environment: the same machine and toolchain as the 0.9.0 record (CachyOS, Linux
7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8, OpenJDK
25.0.2), with a live session bus, `wayland-1` and a real display. The full
`tools/ci.sh` gate ran on an isolated `build/ci/` tree and exited 0. Four
documentation files were re-worded after that pass — README and three docs — and no
source, header, test, benchmark or build file changed after it, so the pass below is
the gate for the tree as it ships. It is the gate for `0.10.0` only: the 1.0 freeze
candidate that followed removed the two deprecated drag entry points, so that tree
exports 412 symbols against the 724-entry baseline, and its own record is written
when the rc is cut.

Results:

| Check | Result |
| --- | --- |
| `tools/ci.sh` clean werror build | pass; library, C/C++ examples, 414 exported `pt_` symbols, 0 missing and 0 unexpected |
| C test suite | 457,457 checks in 29 executables: 2,837 assertions across 26 headless suites, the 454,380-check randomized property suite, 200 live AT-SPI bridge checks and a 40-check live Wayland smoke test |
| C++ binding (`test_cpp`) | 345 checks |
| Python binding (`unittest`) | 33 tests |
| Java binding (`PtTest`) | 298 checks, with the two 0.9 deprecations still visible as `javac -Xlint:all` warnings |
| Binding parity and API audit | 414/414 in C++/Python/Java with an empty allowlist; the 0.6 (125), 0.7 (19), 0.8 (19), 0.9 (44) and 0.10 (40) addition surfaces are each complete in all three; baseline `726 entries; 0 removed, 0 changed, 0 added`; generated reference matches the headers |
| ASan + UBSan + LeakSanitizer (`check-sanitize`: 29 C suites, the property suite, the live bridge, the live Wayland test and C++ with leak detection) | pass, no reports; 30 sanitized runs in total |
| ASan-preload Python and Java suites | 33 tests and 298 checks, pass, no reports |
| `make perf-check` | all guard ceilings hold; in the closing pass the tightest margins were the two ratio guards — the 4K damaged frame at 8.432% of its 12% ceiling and the damage traversal share at 24.590% of 35%, both 1.42x — then the 100,000-row seek at 13.144 ms of its 20 ms ceiling (1.52x). The 4K paint ceiling is computed from the same run's dense frame, so it is the only limit that moves between passes: 280.3 ms here, against 230.4 ms in the fastest pass of the round |
| Live examples | 17 programs on the live compositor — the 7 C, 3 C++ and 4 Python examples and the 3 Java ones — each ran a 5 s slice and was then stopped by the harness, with no error output and no crash. On a real session the examples stay open until dismissed; their self-quitting timers arm only under `PT_HEADLESS=1`, which is why the same programs exit 0 on their own in the headless stages. Running this sweep is what surfaced the text-input round trip recorded at the end of this document: the accessibility example printed 67,499 `selection_changed` events in its slice before the fix and 1 after it |
| Staged install, relocatable pkg-config (`0.10.0`), SONAME `libpt.so.0` | pass; exported ABI matched the checked-in baseline |
| Installed C, C++23, Python and Java consumers | compiled against the staged install and ran headless |
| `make uninstall` | removed every staged file and symlink |
| Reproducible `make dist` | two runs at `SOURCE_DATE_EPOCH=1767225600` produced byte-identical archives, the gate verified that `pt-0.10.0.tar.gz.sha256` matches the digest it computes, and the extracted tarball built with `werror=1` and passed `make check`, including the 414/414 audit. No digest is quoted here because the archive contains this record, so editing the record invalidates any value written into it |
| `tools/release-check.sh` | exit 0, no `FAIL` line; three `open` items left for a human — the 1.0 removal window, the freeze commit and tag, and the live-compositor steps, the last of which this record answers |

Three gate runs failed before the pass quoted above, and a fourth problem — the
text-input round trip at the end of this record — was found by running the
examples rather than by a gate. All four were the checks doing their job or a gap
in what the checks can see, not release blockers. Seven `tools/ci.sh` runs of
0.10.0 left archives behind: six that ended with exit 0, the closing pass last,
and one that stopped in the live-Wayland stage of the extracted-tarball step after
`make check`, `perf-check` and the sanitizer stage had all run. The two earliest
failures were fixed and re-run under new archives, so their own logs are not among
the seven.

The first stopped at `api-check`. Editing the removal-window comments in
`include/pt/dnd.h` makes the generated reference differ from the headers, and
`make api-docs` had not been run since. The gate caught a documentation drift
that would otherwise have shipped, exactly as the policy intends.

The second stopped at `check-sanitize`, where ASan reported a
heap-use-after-free in `tests/test_combobox.c:407`. A combo box rebuilds its
popup on a model change: `refresh_popup` builds a new panel and
`pt_window_popup_replace_panel` parks the old one on the window's overlay garbage
list, which the next paint frees. The test had cached the scroll widget from the
panel built at open time and read `scroll_y` from it two paints and one
insert/remove pair later. The fix re-looks the widget up after the model change
instead of reusing the stale pointer. Nothing in the public API hands an
application a popup panel, so no application-facing lifetime rule is involved,
and the `make check` pass had no way to see it — the pointer was still inside a
freed-but-untouched region until the sanitizer build ran the suite.

`test_visuals` reports 119 checks in the source tree and 118 inside the extracted
tarball. The difference is one branch, not a skipped assertion: the harness
`mkdir`s its snapshot directory and asserts success or `EEXIST`, so the first
run in a fresh directory takes the success branch and every later run takes the
`EEXIST` branch. The PNG output and the frame-hash comparisons are identical.

Headless benchmark from the closing gate pass: the 1,506-node scene builds in 0.74 ms
and settles at layout 3.209 ms, full paint 18.241 ms and localized one-row damage
1.319 ms (7.2% of full paint) with 51,032 KiB RSS / 18,968 KiB PSS. The
10,000-row table plus 1,000-node tree materializes 93 widgets and costs 0.020 ms
layout, 13.545 ms full paint and 0.185 ms one-cell damage (1.4%) with 34,200 KiB
RSS / 12,717 KiB PSS. Startup is a 23.74 ms first 800x600 frame (28,504 KiB RSS /
10,492 KiB PSS) and 500 ms of idle loop consumes 1.101 ms of CPU. The guard binary
in the same pass measured dense layout 3.172 ms, dense paint 17.876 ms with 1.303 ms
one-row damage (7.291%, 61 nodes per full frame against 15 per damaged frame,
traversal share 24.590%), a 47.808 ms 4K full paint with 4.031 ms 4K one-row damage
(8.432%, 2.67x the 1280x800 frame), a 3.161 ms scroll frame over 10,200 visible
nodes with 13 row widgets, and a 13.144 ms 100,000-row table scroll that
materializes 140 widgets and 22 rows with 0.147 ms one-cell damage (1.116%).
`docs/performance.md` quotes this pass plus the six earlier archived gate runs and
four direct repeats of each reporting binary for the ranges. Measured against 0.9.0's
closing pass on this machine, the guard's dense frame read 14.895 ms and its 4K
frame 39.283 ms; the closing pass here reads 17.876 ms and 47.808 ms, about 20% and
22% higher. That is not established as code cost: the round's own spread for the
dense frame is 14.318-17.876 ms and for the 4K frame 37.657-47.808 ms, and both
ranges contain the 0.9.0 figures, so a single pass of this desktop cannot separate
the added 0.10 state work from the noise. Across the
seven archived gate runs of this round the guard's dense frame measured
14.318-17.876 ms and its 4K frame 37.657-47.808 ms, with every ceiling holding in
every one of them. The
narrowest margin the round produced is a time guard, the 100,000-row seek at
14.921 ms of its 20 ms ceiling (1.34x); the narrowest ratio guard is the 4K damaged
frame at 8.741% of the 12% ceiling (1.37x), and the damage traversal share sat at
24.590% of 35% in all eleven guard runs. Timings move by tens
of percent between runs here, so only a breach of the ceilings in `bench/guards.c`
fails the gate.

The run this record quoted before the documentation sweep agrees with the closing
pass on
every count that does not depend on timing: 414 exported `pt_` symbols, the
`726 entries; 0 removed, 0 changed, 0 added` baseline, 457,457 C checks in 29
executables (2,837 headless assertions, the 454,380-check property suite, 200 live
bridge checks, the 40-check live Wayland suite and the 73-check IME suite), 345 C++
and 298 Java
checks, 33 Python tests, 414/414 parity in all three bindings with an empty
allowlist and 30 sanitized runs with no reports. The five older archived runs carry
the pre-change figures: the C total read 457,445 in the first four of them, then
457,446 as the Wayland suite gained its retry assertion and 457,457 once the IME
suite gained the eleven checks for the empty `done`. Only the timings moved
otherwise. That is the result the packaging design expects: the two `make dist`
runs inside one gate pass are compared with each other, never with a digest stored
in this record, so a documentation edit changes the archive contents and
invalidates nothing.

The round's third failure came in the live Wayland smoke test of the
extracted-tarball stage: `tests/test_wayland.c` lost four of 37 checks, with the menu
popup and the tooltip popup both gone by the time they were read. The same binary had
passed 39 of 39 in the two live stages of the earlier passes, and six direct reruns
after it agreed. This is a property of the protocol, not of the build: a grabbed
`xdg_popup` is the compositor's to end, and when it sends `popup_done`,
`pt_window_overlay_native_done` dismisses the overlay instead of keeping a surface no
compositor will map. On a live desktop a focus change can land inside the 260 ms slice
the test waits through, and the suite asserted on that first attempt. It now retries an
attempt the compositor ended and asserts on the one that survived — for the menu popup
and the tooltip popup alike — and still fails if no attempt holds. Eight direct runs of
the hardened suite pass 40 of 40, every live stage of the pass quoted above reports 40
checks, and the extracted tarball built and passed `make check` from that state.

The fourth finding is the one no gate can see, because it needs a real compositor and
a real input method. `zwp_text_input_v3.done` with no commit string, no preedit and no
deletion range is the input method acknowledging the surrounding text and caret
rectangle Pt pushes to it. `pt_window_input_ime_compose` used to run the whole
composition path for it anyway: caret reset, `selection_changed`, a fresh
`set_surrounding_text` and `commit`, and a scheduled redraw — and that reply asks the
input method for another acknowledgement. The two sides answer each other for as long
as an entry holds focus. The accessibility example measured it on niri: 67,499
`selection_changed` events and 2.13 s of CPU (1.857 s user, 0.277 s system) in a 5 s
slice. Repeating the slice with `AT_SPI_BUS_ADDRESS` pointed at a socket that does not
exist printed 36,241 events in 4 s, so this was never assistive technology polling the
app; it is internal to Pt. An entry is the first thing most applications focus, so every
text-heavy Pt window would quietly burn 43% of a core and drown a screen
reader in selection changes. `pt_window_input_ime_compose` now returns before any of
that when the event carries nothing and `pt_widget_ime_has_state` says no composition is
in flight; an empty `done` that does arrive mid-composition still ends it, clears the
preedit and redraws, because that is the only end-of-composition signal the protocol
gives. The same 5 s slice now prints 1 event and 0.65 s of CPU. `test_ime` pins both
halves (73 checks): the empty `done` leaves `needs_draw` false and emits nothing, the
one after a preedit clears it and emits again, and a commit is never swallowed by the
guard. Nothing here touches the public API, so the removal window and the freeze list
are unchanged; what it does change is a contract an input-method engine can observe,
which is why M17 has to state in prose when Pt pushes state back.

Not delivered this round, recorded under "Not delivered" in `docs/ROADMAP.md`:
there are no application-owned child surfaces, so a filterable combobox, a dialog
and a toast still stay inside the window; date, number and colour pickers, a
searchable select beyond the filterable combobox and painted icon slots did not
ship; per-run font fallback, a written right-to-left plan and label/text-area text
interfaces in the bridge are open; clipboard images, `text/uri-list` writes and
primary selection are open; column layout persistence helpers and inline image
runs carried over from 0.9; and the two performance profiles M16 asked for — a
marquee-drag frame and a native popup surface — are not in `bench/guards.c`, so
those two paths have functional coverage but no guard. The same is true of the
text-input round trip above: it is now tested for behavior and measured by hand on
this desktop, and the live suite still has no idle-CPU measurement that would catch
a state push answering itself.

The initial Git commit and annotated tag are created from this tree after the
gate; the gate itself never changes Git configuration.

## 1.0.0 validation record (2026-10-20)

Environment: the same machine and toolchain as the 0.10.0 record (CachyOS,
Linux 7.2.9-2-cachyos, niri 26.04 on Wayland, GCC 16.2.1, Python 3.14.8,
OpenJDK 25.0.2), with a live session bus and a live accessibility bus on
`wayland-1`. The full `tools/ci.sh` gate ran on an isolated `build/ci/` tree and
exited 0. A behavior-only follow-up then made the rubber band start in empty
viewport space (see below); the same full gate was rerun on the final source and
test tree and exited 0. Documentation was reworded after that pass (README,
architecture, performance, comparison, ROADMAP and this record); no source,
header, test, benchmark or build file changed after it. The gate therefore
covers the code that ships, and the final `tools/release-check.sh --full` pass
covers the documented tree.

Results:

| Check | Result |
| --- | --- |
| `tools/ci.sh` clean werror build | pass; library, C/C++ examples, 412 exported `pt_` symbols, 0 missing and 0 unexpected |
| C test suite | 457,584 checks in 29 headless executables: 2,968 regular assertions, the 454,380-check randomized property suite and 236 live AT-SPI bridge checks. `test_wayland` is separate: 48 live checks under `make check-wayland` |
| C++ binding (`test_cpp`) | 344 checks |
| Python binding (`unittest`) | 33 tests |
| Java binding (`PtTest`) | 298 checks, clean under `javac -Xlint:all` |
| Binding parity and API audit | 412/412 in C++/Python/Java with an empty allowlist; baseline `724 entries; 0 removed, 0 changed, 0 added`; generated reference matches the headers; 412 exports |
| ASan + UBSan + LeakSanitizer (`check-sanitize`: 29 C suites, the property suite, the live bridge and C++ with leak detection) | pass, no reports; 30 sanitized runs in total |
| ASan-preload Python and Java suites | 33 tests and 298 checks, pass, no reports |
| `make perf-check` | all guard ceilings hold: marquee 8.800 ms, popup 1.202 ms, dense layout 2.205 ms and paint 13.252 ms with 1.007 ms one-row damage (7.600%), 4K paint 34.936 ms with 3.118 ms damage (8.924%), large-tree scroll 2.502 ms, 100,000-row seek 10.504 ms with 0.121 ms cell damage (1.153%), startup first frame 10.760 ms and 300 ms idle loop 0.700 ms |
| Live Wayland (`make check-wayland`) | 48 checks in a fullscreen nested niri: menu and tooltip popups held, and the focused-entry idle guard measured 29.9 ms in the full CI rerun and 130.0 ms in the final release-check rerun, each after one slice was discarded for external input |
| Live typing (`make check-typing`) | `ptdirect` exact; the manual variants `hello world`, `abcdefghij` and `pt 1.0` were also exact |
| Staged install, relocatable pkg-config (`1.0.0`), SONAME `libpt.so.1` | pass; exported ABI matched the checked-in baseline; C, C++23, Python and Java consumers compiled and ran; uninstall removed every staged file |
| Reproducible `make dist` | two runs at `SOURCE_DATE_EPOCH=1767225600` produced byte-identical archives and the sidecar matched the digest; the extracted tarball built with `werror=1` and passed `make check`, including 236 live bridge checks, 344 C++, 33 Python and 298 Java checks. No digest is quoted here because this record is inside the archive |
| Gallery smoke | `build/examples/c/gallery` ran a 4 s live slice with no error output and no crash |
| `tools/release-check.sh --full` | exit 0, no `FAIL` line; two `open` items left — the freeze commit/tag, now created from this tree, and the live-compositor steps, the latter answered by the live rows above |

The accessibility bridge row depends on a live accessibility bus. With one, the
suite runs 236 checks; without one, it reports 11 and skips the live half, so a
release gate should be run with the bus present. Starting a registry on the
accessibility bus was the only environment fix this pass needed.

The empty-viewport rubber band was the one item from the "Not delivered" list
that moved into 1.0.0 after the first gate. The change is behavior-only: no
public header, enum, API baseline or binding changed. A press in the empty
content box below the last row starts the shared band and clears the set unless
Ctrl is held; the drag then selects by geometric intersection in the list, the
table and the tree, and a plain press without a drag clears the selection and
never activates the row that was primary before it.
`tests/test_selection.c` grew from 189 to 274 checks, and the final full gate
above includes that run. The rest of the post-1.0 list is unchanged.

Three findings from the freeze round are worth keeping with the record:

- **Direct typing was broken while text-input-v3 was enabled.** When
  `text_input_enabled` was set and `text_input_window` matched the window,
  `keyboard_key()` and `repeat_fire()` discarded the UTF-8 text that xkbcommon
  produced for every ordinary key, so only an input-method `commit_string`
  could insert a character. A focused entry looked dead until the IME composed
  something. The fix passes the key text through unless Ctrl, Alt or Logo is
  held; IME commits still own composition. `tests/test_wayland.c` never covered
  ordinary keys, so the permanent guard is `tools/live-typing.sh` with
  `tests/vkbd_type.c` and `tests/typing_probe.c`. Its first version silently
  sent keys nowhere because a nested compositor clears its keyboard focus when
  the parent window is not focused; `tools/nested-wayland.sh` now focuses the
  nested window in the parent session before the probe runs and re-focuses it
  before injection.
- **The live idle guard and the two missing performance profiles are
  delivered.** `tests/test_wayland.c` runs a 5 s focused-entry slice and reads
  `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`; a clean slice must stay under
  1,500 ms. A slice disturbed by a key, an entry-text change, a focus change or
  pointer movement is retried, and four busy slices report a skip rather than a
  false regression. `make check-wayland` runs it in a fullscreen nested niri
  with a parent-focused window. `bench/guards.c` adds the marquee-drag frame
  (27 widgets, 10 selected rows) and the native popup panel (240x332, 82
  widgets), both under a 20 ms ceiling; the 1.0 gate runs measured 8.800-9.335 ms
  and 1.202-1.287 ms. `make check` is now the headless gate, while
  `check-wayland` owns the live suite and `check-typing` owns the direct-key
  path.
- **The text interfaces in the accessibility bridge agree.** Labels and buttons
  expose `Text` as read-only; entries and text areas expose `EditableText` and
  `SELECTABLE_TEXT`; a password's `GetCharacterAtOffset` reads the redacted
  visible string. `test_a11y_bridge` grew from 200 to 236 live checks, and the
  bridge's text-state round trip is part of that count.

Post-1.0 scope is recorded in `docs/ROADMAP.md` under "Scope decided against
the freeze": application-owned child surfaces, pickers and a searchable select,
per-run font fallback, clipboard images and `text/uri-list` writes, column
layout persistence and inline image runs. The empty-viewport marquee left that
list in the behavior-only follow-up above. The RTL/bidi plan is written but is not implemented in
1.0; its proposed names remain provisional until an API-policy review gives
binding parity. The git commit and annotated tag remain pending because the
repository has no configured author identity; build scripts never change Git
configuration.

## 1.1.0 validation record (2026-11-22)

Environment: the integrated `m18/integration` tree on the reference
machine (CachyOS, Wayland, niri 26.04) with the 1.0 toolchain (GCC 16.2.1,
Python 3.14.8, OpenJDK 25.0.2). The five development branches were merged before
this record: C contract hardening, Java/extension wrapping, examples, binding
tests and documentation; the final seven wrappers, the 1.1.0 version bump and
the documentation finalisation landed on top.

The source, tests, examples and documentation are complete on the integration
tree. Every figure below was measured on that tree, including the full
`tools/ci.sh` run; the only remaining steps before the tag are the owner's
freeze commit and annotated tag.

### Scope

1.1.0 keeps the 1.0 signatures and adds an append-only extension surface:

- Decoration negotiation: `pt_decoration_mode` (`auto`, `client`, `server`),
  `pt_window_set_decoration_mode`, `pt_window_get_decoration_mode`,
  `pt_window_get_effective_decoration_mode`, `pt_window_maximize`, and the
  requested/effective split with `configure` authoritative.
- Side-effect-free snapshots: `pt_window_paint_pixels_ex` plus
  `pt_pixel_format` and `pt_paint_pixels_desc` (stride, scale, format, reserved).
- Custom paint: `pt_widget_set_paint_callback` with the borrowed
  `pt_paint_context`, the `pt_paint_*` helpers and widget-level redraw requests.
- Primary selection: `pt_window_primary_set`, `_set_serial`, `_get` over
  `wp_primary_selection_unstable_v1` with an in-process fallback.
- Custom IME: the window-level commit/preedit callbacks, enable switch and
  caret rectangle.
- Descriptor watches: `pt_app_watch_fd`, `pt_app_watch_fd_owned`,
  `pt_app_unwatch_fd`, with the `pt_fd_event` mask.
- Raw input: focus, key-event and pointer-event callbacks that run before widget
  dispatch.

The public surface is **448 functions** in **785 baseline entries**
(448 `func`, 274 `enum`, 39 `callback`, 8 `struct`, 10 `opaque`, 5 `define`,
1 `type`). The diff against the 724-entry 1.0 baseline is 61 additions and no
removals, renames or signature changes; SONAME stays `libpt.so.1`.

### Behaviour changes

Every signature is compatible, but these observable behaviours changed and are
the section an upgrading application must read:

- The default decoration request moved from `SERVER_SIDE` to explicit
  `CLIENT_SIDE` (`pt_decoration_client`).
- `pt_window_set_decorated(false)` no longer sends a decoration mode at all; it
  only stops Pt from drawing its header and edge zones.
- `pt_window_primary_set` returns false and sends nothing when no pointer
  release or key press has supplied a trigger serial. Use
  `pt_window_primary_set_serial` from a callback that knows the serial.
- An offscreen snapshot no longer consumes damage, clears
  `needs_draw`/`redraw_scheduled`, updates the paint timestamp or collects
  deferred overlays. It never swallows the next live frame.
- Enter, leave and motion callback return values are ignored; only key and
  pointer down/up/axis are consumed.
- A duplicate watch of the same descriptor is rejected with a NULL/0 result.
  `POLLHUP`/`POLLERR`/`POLLNVAL` always reach the callback as an error event.
- A paint callback may destroy itself; the free is deferred until dispatch
  returns. An unbalanced clip stack is corrected and recorded. A reentrant
  snapshot returns false and records an error. These guards are always active;
  there is no `PT_DEBUG_CALLBACKS` switch.

### Results

| Check | Result |
| --- | --- |
| Version agreement | `include/pt/base.h`, `Makefile` `VERSION`, README and the generated reference all say 1.1.0 |
| API baseline | `api/pt.api` 448 functions / 785 entries; generated reference covers every baseline function |
| Binding parity | 448/448 in C++/Python/Java, 0 missing, empty allowlists |
| C suites, C++/Python/Java test binaries | pass on the integration tree |
| `make check` / `make check-bindings` | all checks passed; C++ 517 checks, Python 48 tests, Java 372 checks, 0 failures |
| `make perf-check` | all guard ceilings hold; release-tree medians: dense full paint 4.917 ms, 4K full paint 9.257 ms, first frame 22.32 ms, idle 500 ms CPU 0.968 ms |
| Live Wayland (`make check-wayland`) | 48 checks under niri; focused-entry idle guard 21.7 ms / 5000 ms after one external-input retry |
| Live typing (`make check-typing`) | pass under niri with text-input-v3 (ptdirect) |
| Sanitizer (`make check-sanitize`) | pass: ASan+UBSan C suites, sanitized C++ 517 checks / Python 48 tests / Java 372 checks, 0 failures |
| Staged install / reproducible tarball | pass: SONAME `libpt.so.1`, pkg-config 1.1.0, exported ABI matches the baseline, C/C++/Python/Java consumers, uninstall; two `make dist` runs at `SOURCE_DATE_EPOCH=1767225600` reproduced sha256 `0aa2b1c7…3a446`, extracted tree rebuilds and passes `make check` |
| `tools/ci.sh` | pass end to end (`CI verification passed for Pt 1.1.0`): werror build, check, perf, install-check, sanitizers, tarball, live suites |
| `tools/release-check.sh --full` | automated gates pass for 1.1.0; two human items remain (freeze commit/tag, live-suite sign-off — the live suites and example smoke tests above were run on this tree) |
| `tools/check-bilingual-docs.py` | pass: 9 public guides, no structural drift |

### ABI statement

The 1.1.0 change is append-only within `libpt.so.1`: new functions, callback
typedefs, enum constants and one public struct, with the existing enum values
and struct layouts unchanged. `tests/test_abi.c` pins the new
`pt_fd_event_*` bits, `pt_pointer_phase_*` values, `pt_pointer_event` offsets,
`pt_decoration_mode`, `pt_pixel_format` and `pt_paint_pixels_desc`.
`abidiff` (libabigail) is not installed on the reference machine and could not
be added without elevated privileges; the release-time comparison was done at
the symbol level instead: `nm -D --defined-only` on the 1.0.0 tag build and the
1.1.0 build shows 412 → 448 exported `pt_*` symbols, **0 removed, 36 added**,
and `readelf -d` confirms SONAME stays `libpt.so.1`. Run `abidiff` on a machine
with libabigail before publishing if a type-level comparison is wanted.

### Known issues

- The fake-compositor wire-replay facility does not exist yet. Decoration,
  text-input, primary-selection and popup event orders are covered by headless
  state tests and the live niri run; the in-process fake compositor is an open
  M18 item.
- `preedit_hint` visual presentation and `show_input_panel`/`hide_input_panel`
  are deferred to 1.2. The custom IME event surface, local composition buffer,
  full-state resend and empty-`done` acknowledgement are implemented.
- Binding raw-callback dispatch-level tests are limited by headless injection:
  registration and callback-free paths are tested, but an actual raw key or
  pointer event needs a live seat.
- KWin/fcitx5 `commit_string` loss is mitigated by full-state resend, not a
  confirmed root cause. The live KWin/Mutter IME matrix remains to be recorded.

The annotated tag is created from the final gate tree after the pending items
close; this record is inside the release archive, so it does not quote a digest.

## 1.2.0 validation record (2026-11-22)

Environment: the integrated `m19/integration` tree at `859233f` on the reference
machine (CachyOS, Wayland, niri 26.04) with the 1.0 toolchain (GCC 16.2.1,
Python 3.14.8, OpenJDK 25.0.2). The four wave-1 branches, the wave-2 picker and
icon branches, the stub generator, the three-language wrappers, the 1.2
examples (`0c6b1f7`, `f013ac3`) and the docs branch that carries the version
bump, the generated reference and the bilingual guides are merged on this tree.

Every figure below was measured by the final gate pass on `859233f`:
`tools/ci.sh` run to completion (exit 0) and `tools/release-check.sh --full`.
None is quoted from an earlier release. Only the annotated tag is left to the
repository owner.

### Scope

1.2.0 keeps every 1.1 signature and adds an append-only common-surface wave of
106 functions:

- Application-owned popups: `pt_popup_create`, `pt_popup_destroy`,
  `pt_popup_show`, `pt_popup_reposition`, `pt_popup_dismiss`,
  `pt_popup_is_open`, `pt_popup_content`, `pt_popup_set_on_dismiss`,
  `pt_popup_set_placement`, `pt_popup_get_placement`, the
  `pt_popup_anchor`/`pt_popup_placement` types and `PT_POPUP_MAX_OPEN`.
- Per-run font fallback: the app chain and per-family override setters/getters,
  `pt_app_font_uncovered_count` and the two chain bounds.
- Clipboard media: `pt_window_clipboard_set_image_png`,
  `pt_window_clipboard_get_image_png`, `pt_window_clipboard_set_uri_list` and
  `pt_clipboard_image_max_dimension`.
- Column persistence: `pt_table_set_column_key`, `pt_table_get_column_key`,
  `pt_table_columns_save`, `pt_table_columns_restore` and the column/key/blob
  limits.
- Icon images: `pt_icon_image_load_file`, `_load_memory`, `_from_name`, `_ref`,
  `_destroy`, `_width`, `_height`, `pt_button_set_icon_image`,
  `pt_button_get_icon_image`, `pt_button_set_icon_tint`,
  `pt_window_set_icon` and `pt_window_get_icon`.
- Pickers: the 70 `pt_select_*`, `pt_date_*`/`pt_date_picker_*`,
  `pt_number_picker_*` and `pt_color_picker_*` functions, the `pt_date` struct,
  `pt_date_format` and the three picker callbacks.

The public surface is **554 functions** in **907 baseline entries**
(554 `func`, 281 `enum`, 43 `callback`, 10 `struct`, 12 `opaque`, 6 `define`,
1 `type`). The diff against the 785-entry 1.1 baseline is 106 additions and no
removals, renames or signature changes; SONAME stays `libpt.so.1`. Every
declaration carries a since marker (412 × 1.0, 36 × 1.1, 106 × 1.2) and the
generated reference shows it per entry.

### Behaviour changes

The C ABI change is additive; no existing signature or enum value moved. The
one interaction with an existing control was checked explicitly:

- A picker is a new control that reports `pt_widget_combobox` as its kind so the
  shipped dispatch hooks apply. `src/combobox.c` forwards to `src/picker.c` only
  when the state carries a picker, so an ordinary combobox never takes the
  forwarding path; the 203-check `test_combobox` suite and the 274-check
  `test_selection` suite still pass unchanged. Calling the `pt_combobox_*` item
  or mode API on a picker is a documented no-op.
- Picker programmatic setters are silent, matching every other `set_selected`
  and setter in the API; only user input fires the change callback. Invalid
  text commits return false, keep the previous value and text, and record
  `pt_get_last_error`.
- Setting one clipboard type replaces the previous one, so a successful image
  set makes the text getter return NULL. Image pixels are premultiplied
  `0xAARRGGBB` on both sides, with the documented straight-alpha PNG conversion.
- Font fallback chains are process-wide; an explicit chain is authoritative and
  an uncovered codepoint stays tofu.
- The pickers do not read the process locale: the date picture is explicit, the
  committed decimal separator is `.` and the colour field is hex. This is new
  control behaviour, not a change to an existing one.

Anything that later changes an existing signature or an existing control's
observable behaviour must be listed here before the tag.

### Results

| Check | Result |
| --- | --- |
| Version agreement | pass: `include/pt/base.h`, `Makefile` `VERSION`, README and the generated reference all say 1.2.0; SONAME `libpt.so.1` |
| API baseline | pass: `api/pt.api` 554 functions / 907 entries; the reference names all 554 baseline functions |
| Since audit | pass: 554/554 declarations carry Since 1.0/1.1/1.2; generated entries expose them |
| C suites (headless) | pass: 35 suites, 6,913 checks; `test_popup` 314, `test_font_fallback` 131, `test_clipboard_media` 871, `test_column_persist` 1,390, `test_picker` 550, `test_icon` 178 |
| ABI pins | pass: `tests/test_abi.c` 13 checks, including the new popup/icon/picker limits and `pt_date` offsets |
| Bilingual documentation | pass: 9 public guides, no structural drift; code blocks byte-identical |
| Binding parity | pass: 554/554 (100.0%) in C++, Python and Java, 0 missing, 0 allowlisted; the 0.6-0.10 surface audits are complete |
| `make check` / `make check-bindings` | pass: all C suites green, C++ 763 checks, Python 64 tests, Java 620 checks, 0 failures; exported-symbol, baseline and generated-reference audits clean |
| `make perf-check` | pass: every guard ceiling holds; dense full paint 6.078 ms, 4K full paint 8.571 ms, first frame 13.494 ms, idle 300 ms CPU 0.451 ms (startup bench: first frame 12.40 ms, idle 500 ms CPU 0.772 ms) |
| Live Wayland / live typing | pass under niri: `check-wayland` 48 checks, focused-entry idle guard 26.6 ms over 5000 ms after one external-input retry; `check-typing` PASS live typing (ptdirect, text-input-v3) |
| Sanitizer, staged install, reproducible tarball, `tools/ci.sh` | pass: ASan+UBSan C suites with leak detection plus sanitized C++ 763 / Python 64 / Java 620, 0 failures; staged install reports SONAME `libpt.so.1`, pkg-config 1.2.0, exported ABI matching the baseline, C/C++/Python/Java consumers and uninstall; two `make dist` runs at `SOURCE_DATE_EPOCH=1767225600` reproduced sha256 `54ce8abd…c521ac` (sidecar matches), the extracted tree rebuilt with `werror=1` and passed `make check`; `tools/ci.sh` exited 0 with `CI verification passed for Pt 1.2.0` |
| `tools/release-check.sh --full` | automated gates pass for 1.2.0; two human items remain (freeze commit/tag, live-suite sign-off — the live suites above were run on this tree) |

### ABI statement

The 1.2.0 change is append-only within `libpt.so.1`: 106 new functions, three
callback typedefs, two enum types (`pt_date_format`, `pt_popup_placement`) with
appended constants, two public structs (`pt_date`, `pt_popup_anchor`) and two
opaque handles (`pt_popup`, `pt_icon_image`), plus the documented limit macros.
No existing enum value, struct field order or signature changed.
`tests/test_abi.c` pins the new caps, the date field offsets, the popup
placement values, the anchor layout and the callback pointer widths.
`abidiff` (libabigail) is still not installed on the reference machine (there is
no `libabigail` package and no elevated privileges to add one), so the
release-time comparison was done at the symbol level instead: `nm -D
--defined-only` on a fresh `make lib` build of the `1.1.0` tag (`e6ba0c1`,
checked out in a temporary worktree) and on the 1.2.0 build shows **448 → 554**
exported `pt_*` symbols, **0 removed, 106 added**, with no symbol type changes
among the 448 common symbols; `readelf -d` confirms SONAME stays `libpt.so.1`
on both. The 106 additions group as 70 picker functions, 12 icon-image
functions (7 `pt_icon_image_*`, 3 button image/tint, 2 window icon), 10 popup
functions, 7 font-fallback functions, 4 column-persistence functions and 3
clipboard-media functions (plus the limit macros, the two structs, the two
enums, the three callback typedefs and the two opaque handles that add no
symbols). A type-level comparison with `abidiff` is still recommended on a
machine that has libabigail before publishing.

### Final integration surface

The three-language wrappers, their dispatch tests and the 1.2 examples are on
this tree. `bindings/cpp`, `bindings/python` and `bindings/java` cover 554/554
with an empty allowlist (C++ 763 checks, Python 64 tests, Java 620 checks), and
13 new examples exercise the new surface in all four languages: `0c6b1f7` adds
the six C demos (popup, pickers, icon button, clipboard image, font fallback,
column persistence) and `f013ac3` adds the C++ pickers/popup, Python
pickers/popup and Java pickers/popup/icon ports.

### Known issues

- The fake-compositor wire-replay facility still does not exist. Popup event
  order, font fallback selection and clipboard media paths are covered by
  headless state tests and the live niri run; the in-process fake compositor is
  the inherited M18 open item.
- SVG icons are deferred. `pt_icon_image` decodes PNG only; a theme that ships
  scalable SVGs only does not resolve.
- The number picker reports the combo-box accessibility role and no expandable
  state; a dedicated spin-box role would be an append-only enum addition in a
  later minor.
- The toplevel-icon submission path has no live compositor recording. The
  matrix records KWin/Labwc/Muffin/Jay/Pila as exposing
  `xdg-toplevel-icon-v1`, but none of them was run in this pass; headless
  submission and the niri/Sway/Weston no-op path are tested.
- Pickers are locale-independent by design (explicit date picture, `.` decimal
  separator, hex colour). Localized month and group separators are the
  application's responsibility.
- M18's `preedit_hint` presentation and
  `show_input_panel`/`hide_input_panel` remain deferred; the custom IME event
  surface and full-state resend are unchanged.
- Binding raw-callback dispatch assertions still need a live seat; registration
  and callback-free paths are headless-tested for all three bindings.

The annotated tag is created from the final gate tree once the owner confirms the
freeze. Because this record ships inside the release archive, editing it changes
the archive digest: the sha256 quoted above is what the full gate measured for
`859233f`, and `make dist` on the tagged tree reproduces its own digest exactly
— re-measure there rather than trusting a digest quoted from inside the
archive.

## 2.0.0 validation record (2026-11-23, final — tag-ready)

Environment: the final gate tree `91176c2` on `m22/integration` on the
reference machine (CachyOS, Wayland, niri 26.04, fcitx5 5.1.23) with GCC
16.2.1, Python 3.14.10 and OpenJDK 25.0.2. The integration tree `a02a40e`
(W0-W4, W5a, W5b `6553270`, W6a, W6b `b4bdf99` and the W7prep LTR gate
`43cfad6`) was fast-forwarded to the W7 hardening tip `91176c2` (12 commits,
`a361d5b` through `91176c2`: nine hardening commits plus the CSD-title,
entry-shaping and fuzz-oracle corrections).

This is the **final gate record**. Every figure below was measured on
`91176c2`, including `make check`, `make check-bindings`, `make perf-check`,
the four live suites, the sanitizer and staged-install passes, the reproducible
tarball and extracted rebuild, the symbol-level ABI comparison against 1.4.0
and `tools/release-check.sh --full`. The only steps left before the tag are the
owner's freeze confirmation and the one frozen-gate-script correction documented
under [Gate transcript](#gate-transcript) (`tools/install-check.sh` does not
expand `$(ABI_MAJOR)`); the artifact itself is tag-ready.

### Scope

2.0.0 is the first structural release since 1.0 and the RTL/bidirectional-text
milestone. It adds **8 public functions and 2 enums (6 values)** and the
baseline becomes **564 functions / 924 entries** (564 `func`, 288 `enum`, 43
`callback`, 10 `struct`, 12 `opaque`, 6 `define`, 1 `type`), with 0 removed and
0 changed against 1.4.0. Every declaration carries a since
marker (412 × 1.0, 36 × 1.1, 106 × 1.2, 2 × 1.3, 8 × 2.0). SONAME stays
`libpt.so.1`: the Makefile tracks `ABI_MAJOR := 1` separately from `VERSION`,
because 2.0 only adds declarations and corrects bidi rendering.

- **Engine.** Vendored SheenBidi v3.0.0 (Apache-2.0, tag `cfe430e`, UCD 17.0.0)
  under `third_party/sheenbidi`, unity-compiled with isolated warnings and
  hidden `SB*` symbols. The adapter decodes UTF-8 to UTF-32 with a byte-offset
  table because the upstream UTF-8 path has an L1 line-end whitespace defect;
  `PROVENANCE.md` records the vendoring and the upgrade policy, and
  [THIRD_PARTY.md](../THIRD_PARTY.md) records the license.
- **Conformance.** Unicode 18.0.0 `BidiTest.txt` + `BidiCharacterTest.txt`
  (Unicode License v3) are vendored gzipped under `tests/data/bidi`;
  `test_bidi_conformance` runs 770,241 + 91,707 cases with 0 failures in
  `make check` (about 0.5 s).
- **Direction API.** `pt_ui_direction` (`inherit`/`ltr`/`rtl`) and
  `pt_text_direction` (`auto`/`ltr`/`rtl`), plus app, window and widget UI
  setters/getters and widget text setters/getters. Resolution is widget →
  nearest explicit ancestor → window → app → LTR; auto follows UBA P2/P3 per
  paragraph and falls back to the resolved UI direction for neutral-only text.
  Getters return the explicit value; both zero values inherit.
- **Rendering.** UBA levels and reorder, X9 controls kept for shaping but
  skipped when painted, single-line and wrapped/text-area visual order,
  per-paragraph base, L1 line-end whitespace, ellipsis on the visual end,
  HarfBuzz bracket mirroring with no pre-mirroring, Arabic joining,
  shape-cache direction keys and the data-driven LTR fast path.
- **Editing.** Visual caret movement by default (cluster edges, Home/End visual
  line ends, Ctrl+Home/End document logical, Shift logical anchor,
  Backspace/Delete logical), soft-wrap affinity, multi-rectangle selection,
  visual IME rectangles, and password bullets from the stored text direction.
  The logical-movement escape hatch is the existing window key callback; there
  is no ninth API.
- **UI mirroring.** Flex/wrap/grid/scroll geometry, tabs, sidebar, picker
  popups, client-side decoration, overlay and menu chrome; table/tree columns
  stay logical through an internal opt-out; scrollbar on the RTL left; tree
  chevron on the right; RTL entry right-anchored; padding and margin physical.
- **Accessibility.** Object-level `direction` attribute plus
  `Text.GetAttributes`/`GetDefaultAttributes` direction with a scalar range;
  passwords report direction but no character; the C getter keeps byte offsets
  and only the D-Bus `Text` interface uses scalar offsets.
- **LTR protection.** The 17 pure-LTR snapshots in `test_visuals` are unchanged
  and `tools/ltr-compat.sh` compares **11** LTR scenes between 1.4.0 and this
  tree, requiring 11/11 `SAME` with no checked-in golden hash; W7 added the
  decorated CSD-title scene so the 1.4.0 title origin is pinned by a pixel
  gate. `tools/ci.sh` runs that cross-version stage after the build and check.
- **Hardening.** `test_bidi_fuzz` pushes fixed-seed mixed-script sequences
  through paragraph/runs, the visual map and entry editing; `make perf-check`
  adds mixed-paragraph wrap+paint, a 100-row RTL table paint and 1000 RTL caret
  moves; `tools/check-rtl-live.sh` checks live IME-rectangle/map agreement;
  `test_a11y_bridge` runs on a private accessibility bus. W7 also restored the
  1.4.0 CSD-title origin (and mirrored it for RTL) and made run attachment keep
  a mark's own script, which removes the shaping non-idempotence seen when a
  styled run was re-shaped.
- **Examples.** `examples/c/rtl_gallery.c` plus
  `examples/cpp/rtl_direction.cpp`, `examples/python/rtl_direction.py` and
  `examples/java/RtlDirection.java`, each wired to a `run-*` target.

### Behaviour changes

The ABI delta is additive, but these observable behaviours changed and are the
section an upgrading application must read:

- **RTL text renders correctly instead of heuristically.** Mixed-direction
  lines, brackets, numbers and isolates now follow the UBA. An application that
  already displayed RTL text sees different pixels; this is the rendering
  correction the major marks. An application that sets nothing keeps the 1.4
  pixels, which the two LTR gates enforce.
- **The window CSD title keeps the 1.4.0 origin in LTR.** W7 caught a
  sub-pixel origin drift introduced by the 2.0 shaping path and restored the
  1.4.0 placement; the RTL title mirrors it. The decorated-title scene
  (`11-csd-title.ppm`) is byte-identical between 1.4.0 and this tree, so a
  title-only window that sets no direction is unchanged.
- **Direction properties are new and default to inherit.** `pt_ui_direction`
  and `pt_text_direction` are zero-valued in existing code; setters store the
  explicit value, and `auto` resolves per paragraph. Widgets that never set a
  direction resolve to LTR exactly as before.
- **Start/end alignment becomes effective in RTL.** `pt_text_align_start` and
  `pt_align_start`/`pt_justify_start` now resolve against the paragraph or UI
  direction rather than always left; a pure LTR widget resolves them to left as
  before. A multi-paragraph text area can therefore align opposite paragraphs
  in opposite directions while the caret and selection remain one logical
  range.
- **An RTL entry is right-anchored.** At `scroll_x = 0` it shows the logical
  start of its text at the visual right, and typing or scrolling moves text
  leftwards. The 1.4 left-anchored behavior for RTL content is the intentional
  correction (W5b); it does not affect LTR entries.
- **Caret movement is visual by default.** Left/Right move to the visually
  adjacent cluster boundary, which can raise or lower the logical byte offset
  inside an RTL run. Home/End are visual line ends, Ctrl+Home/End are document
  logical, Shift keeps a logical anchor, and Backspace/Delete stay logical.
  Applications that need logical arrows intercept the key in the existing
  window callback.
- **Table and tree columns keep logical order** when the UI direction is RTL;
  only the horizontal start edge changes. Padding and margin remain physical by
  decision, not by omission.
- **The earlier multi-touch focus correction is 1.4 history and is not repeated
  here.** This record lists it only to avoid implying a second change.
- **No existing declaration, enum value or offset unit moved.** Public text and
  selection offsets stay UTF-8 bytes; the C accessibility getter stays bytes;
  the D-Bus `Text` interface stays scalar.

### Results

| Check | Result |
| --- | --- |
| Version agreement | pass: `include/pt/base.h`, `Makefile` `VERSION`, the README status line and the generated reference say 2.0.0; SONAME `libpt.so.1` matches `ABI_MAJOR := 1` |
| API baseline | pass: `api/pt.api` reads 564 functions / 924 entries; the reference names all 564 |
| Since audit | pass: 564/564 declarations carry a since marker (412/36/106/2/8), zero missing |
| Unicode conformance | pass: BidiTest 770,241 cases and BidiCharacterTest 91,707 cases, 0 failures |
| Direction, bidi and rich-text suites | pass on `91176c2`: `test_direction` 105 checks, `test_bidi` 351, `test_bidi_conformance` 8 (770,241 + 91,707 cases), `test_bidi_fuzz` 121,050, `test_rich_text` 345, plus the RTL cases in `test_text_area` (234), `test_visuals` (326), `test_table` (187), `test_tree` (192) and `test_navigation` (180) |
| LTR byte stability | pass: 17 pure-LTR snapshots unchanged; `tools/ltr-compat.sh 1.4.0 HEAD` reports 11/11 `SAME`, including the decorated CSD-title scene (same-machine comparison, no checked-in hash) |
| Binding parity | pass: 564/564 (100.0%) in C++, Python and Java, 0 missing, 0 allowlisted |
| Bilingual documentation | pass: 9 public guides, including the rewritten `rtl.md`, with byte-identical code blocks |
| RTL examples | pass: C/C++/Python/Java examples build under `werror=1`, exit 0 with `PT_HEADLESS=1`, and exit 124 under the nested niri smoke timeout |
| `make check` / `make check-bindings` | pass: 41 C executables (40 suites at 131,498 checks plus the 454,380-check property sweep, 585,878 in total), 0 failures; C++ 862 checks, Python 68 tests, Java 701 checks, 0 failures; `api-check`, the exported-symbol audit and the generated reference are clean |
| `make perf-check` with bidi profiles | pass on `91176c2`: dense layout 2.809 ms/frame, dense paint 5.281 ms/frame, damage 0.171 ms (3.2%), startup first frame 10.86 ms, idle 500 ms CPU 0.959 ms; every guard ceiling holds; bidi profiles mixed paragraph wrap+paint 0.874 ms (ceiling 2.0 ms), 100-row RTL table paint 2.896 ms (ceiling 7.0 ms), 1000 RTL caret moves 43.385 ms (ceiling 100 ms) |
| Live suites | pass under nested niri on `91176c2`: `check-wayland` 48 checks with focused-entry idle 26.1 ms over 5000 ms after one external-input retry; `check-typing` PASS (`ptdirect`, text-input-v3); `check-ime-live` PASS with `PREEDIT_COUNT=5`, `LAST_PREEDIT=ni hao` and `COMMIT=你好`; `check-rtl-live` PASS — the IME rectangle matches the visual caret map within one pixel at every stop, Shift+Right keeps its anchor, both selection spans stay ordered and in bounds, and the entry rendered ink |
| Sanitizers, staged install, reproducible tarball, `tools/ci.sh` | pass with one frozen-script correction: `tools/ci.sh` on `91176c2` builds and runs every suite, perf and the 11/11 LTR gate, then stops at the staged-install step because `tools/install-check.sh` reads `libpt.so.$(ABI_MAJOR)` without expanding it; the corrected copy (temporary, repository file untouched) reports SONAME `libpt.so.1`, pkg-config 2.0.0, the baseline ABI and the C/C++/Python/Java consumers, and `uninstall` leaves no file. ASan+UBSan rebuild of 41 C binaries plus `test_cpp` (862 checks), the Python suite (68 tests) and the Java suite (701 checks) reports nothing; `ldd` lists `libasan.so.8` and `libubsan.so.1` on `tests/test_core`, `libpt.so.2.0.0` and `tests/libwire_driver.so`. Two `make dist` runs at `SOURCE_DATE_EPOCH=1767225600` reproduce one digest (sidecar matches), and the extracted tree rebuilds with `werror=1` and passes `make check` |
| ABI diff against 1.4.0 | pass at the symbol level on `91176c2`: 556 → 564 exported `pt_*` symbols, 0 removed, 0 changed, 8 added (`pt_app_{set,get}_ui_direction`, `pt_window_{set,get}_ui_direction`, `pt_widget_{set,get}_ui_direction`, `pt_widget_{set,get}_text_direction`), no `SB_*` exports, SONAME `libpt.so.1` on both; `readelf --dyn-syms` types agree on all 556 common symbols; `abidiff` is still unavailable on the reference machine |
| `tools/release-check.sh --full` | pass: every automated gate is green (version agreement, header surface, 564/924 baseline, 564/564 parity in three languages, reference coverage, deprecation inventory, examples, 9 bilingual guides); the two open items are the human freeze commit/tag and live-suite sign-off, both recorded above |

### ABI statement

2.0.0 adds eight declarations and two enums and removes or changes nothing. The
release major is not the ABI major: `ABI_MAJOR := 1` keeps SONAME
`libpt.so.1`, and [api-policy.md](api-policy.md) records that a
behavior-contract correction with additive declarations may advance the version
without a SONAME bump. The final gate compared a fresh `make lib` build of
`e09fef4` (1.4.0) against the gate build of `91176c2` with
`nm -D --defined-only`: both libraries define only `pt_*` dynamic symbols (556
and 564 total), the counts are **556 → 564**, and the sorted diff is
**0 removed, 0 changed, 8 added**:

```
pt_app_get_ui_direction      pt_app_set_ui_direction
pt_widget_get_text_direction pt_widget_set_text_direction
pt_widget_get_ui_direction   pt_widget_set_ui_direction
pt_window_get_ui_direction   pt_window_set_ui_direction
```

`readelf --dyn-syms` reports the same symbol type on every one of the 556
common names, no `pt_*` object symbol exists on either side, and no `SB_*`
symbol is exported by the 2.0 build. `readelf -d` reports `libpt.so.1` on both,
`objdump -p` agrees, and `Makefile` reads `ABI_MAJOR := 1` with `VERSION :=
2.0.0`. A header diff of `include/pt/` against 1.4.0 contains only the eight
new declarations, the two new enums and the version macros; no existing
declaration changed. `abidiff` (libabigail) is still unavailable on the
reference machine, so the release-time run is symbol-level; a type-level run is
recommended where libabigail exists.

### Known issues

- **Table horizontal scrolling needs the scrollbar or keyboard.** The tested
  Wayland compositors expose no horizontal wheel axis; this is a platform limit
  recorded in [rtl.md](rtl.md), not a table defect.
- **Shift+arrow across a soft wrap in RTL stays an in-line visual move**, and a
  multi-line mapped selection collapses toward its logical edge at the mapped
  boundary while the painted rectangles stay correct.
- **`pt_text_span` has no direction field** and stays frozen; paragraph
  direction comes from the widget or Unicode isolate controls.
- **Missing Arabic/Hebrew glyphs are fallback/tofu**, reported by
  `pt_app_font_uncovered_count`, not a hard configuration error. The examples
  use whatever covering font the machine has; Noto Sans Arabic/Hebrew or DejaVu
  Sans is recommended.
- **Bidi control characters are not sanitized automatically.** Untrusted text
  can reorder what a reader sees; an application that accepts it should strip
  or display the controls. A future optional API may expose the inventory.
- The 1.4 known issues that are unchanged still apply: KWin+fcitx5 v2 and
  Mutter+IBus v2 live composition are unrecorded, the SVG subset is honest, the
  toplevel-icon path and binding raw dispatch have no physical-seat run, and the
  pickers are locale-independent by design.

### Gate transcript

`tools/ci.sh` on `91176c2` ran the clean `werror=1` build, `make check`
(41 C executables, the binding suites and the API audit), `make perf-check`,
the 11/11 cross-version LTR gate and the staged-install consumers. Two frozen
harness assumptions had to be satisfied before the suites could run at all:
the Python wire tests and `test_canvas` resolve a repository-root `build/tests`
path, so the transcript used the `PT_WIRE_DRIVER` environment hook the test
documents (`$PWD/build/ci/normal/tests/libwire_driver.so`) and an existing
`build/tests` directory. `tools/ci.sh` builds into `build/ci/normal`, so a bare
invocation on a tree without a repository-root `build/` fails those two
lookups; this is a test-path assumption, not a library defect, and the
extracted tarball's own `make check` runs in `build/` and needs no hook.

The staged-install step then stopped on a second frozen-script gap:
`tools/install-check.sh` reads the literal `SONAME := libpt.so.$(ABI_MAJOR)`
from the Makefile without expanding it (unlike `tools/release-check.sh`, which
does expand it), so it looked for `libpt.so.$(ABI_MAJOR)` and exited 1 although
`make install` had produced `libpt.so.2.0.0` with the `libpt.so.1` SONAME
symlink. Running the same script with the one-line expansion in a temporary
copy — the repository file was **not** modified — reports `SONAME libpt.so.1`,
pkg-config 2.0.0, the baseline ABI, the C/C++/Python/Java consumers and a clean
`uninstall` (`install check passed`).

The stages after that point were run individually on the same frozen tree:

- **Sanitizers.** A `sanitize=1` rebuild from an empty `build/ci/sanitize`
  compiled 41 C binaries and `test_cpp` with
  `-fsanitize=address,undefined -fno-omit-frame-pointer`; executed with
  `ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1` they report
  nothing (41 `ok` lines, `test_cpp` 862 checks), and the sanitized Python
  (68 tests, `detect_leaks=0` for the interpreter as documented) and Java
  (701 checks) suites are clean. `ldd` lists `libasan.so.8` and
  `libubsan.so.1` on `tests/test_core`, `libpt.so.2.0.0` and
  `tests/libwire_driver.so`, so the run is instrumented rather than silently
  skipped.
- **Reproducible tarball.** Two `make dist` runs at
  `SOURCE_DATE_EPOCH=1767225600` write
  `e9e436b974a3473727465289d27e6141c2cc0d116f830e1efa888a28ce95c513`
  (the sidecar matches); the extracted archive builds with `werror=1` and
  passes `make check` (`all checks passed`).
- **Live suites.** `check-wayland` 48 checks (focused-entry idle 26.1 ms over
  5000 ms after one external-input retry); `check-typing` PASS (`ptdirect`);
  `check-ime-live` PASS with `PREEDIT_COUNT=5`, `LAST_PREEDIT=ni hao` and
  `COMMIT=你好`; `check-rtl-live` PASS with every IME-rectangle/caret-map stop
  within one pixel.
- **Readiness.** `tools/release-check.sh --full` (with the same temporary
  script copy) reports every automated gate green and the two human open items
  (freeze commit/tag and live-suite sign-off).

`tools/install-check.sh` still needs the same `$(ABI_MAJOR)` expansion
`tools/release-check.sh` already implements; that one-line change in the frozen
gate facility is the only item left before a clean exit-0 `tools/ci.sh`
transcript can be quoted. The artifact, the suite results, the live runs and
the ABI comparison are final on this tree; the annotated tag is created by the
owner from the frozen commit.

## 1.4.0 validation record (2026-11-22)

Environment: the final gate tree `e1104c2` on `m21/integration` on the
reference machine (CachyOS, Wayland, niri 26.04, fcitx5 5.1.23) with GCC 16.2.1,
Python 3.14.10 and OpenJDK 25.0.2. The four wave-1 workstreams and the wave-2
multi-touch fix are integrated at `b947a6e`; the version bump, generated
reference and bilingual guides merged from `m21/docs-final` (`1460643`) and the
SVGZ example refresh from `m21/examples` (`043c0d8`) form the gate tree.

This is the **final gate record**. Every figure below was measured on
`e1104c2`, including `make check`, `make check-bindings`, `make perf-check`,
the live suites, `tools/ci.sh` (sanitizer, staged install, reproducible tarball
and tarball rebuild), the symbol-level ABI comparison against 1.3.0 and
`tools/release-check.sh --full`. The only step left before the tag is the
owner's freeze confirmation.

### Scope

1.4.0 is a capability-extension and quality release: it adds **no public
function, enum or struct**, and the baseline stays at **556 functions / 910
entries** (556 `func`, 282 `enum`, 43 `callback`, 10 `struct`, 12 `opaque`,
6 `define`, 1 `type`), exactly as 1.3.0. Every declaration keeps its since
marker (412 × 1.0, 36 × 1.1, 106 × 1.2, 2 × 1.3). SONAME stays
`libpt.so.1`; the 1.4.0 ABI delta is empty by construction.

- **SVGZ icon sources.** `pt_icon_image_load_file` and
  `pt_icon_image_load_memory` accept gzip-compressed SVG by content sniffing:
  the gzip magic `0x1f 0x8b` is checked before the text sniff, and a
  non-SVG gzip archive is rejected after inflation. Compressed input and
  inflated output are both bounded at 16 MiB (decompression-bomb guard); the
  handle keeps the inflated source and re-parses it per size. Hicolor
  `scalable/` lookup tries `.svg`, `.svgz`, `.png`; fixed-size directories stay
  `.png` only. The contract is in `pt/icon.h` ("Since 1.2; SVGZ since 1.4"), not
  a new symbol.
- **Input wire matrix completion.** The fake compositor injects and replays
  `wl_touch` (down/up/motion/frame/cancel, multiple contacts, pinch, capability
  removal), tablet-v2 (seat/tool lifecycle, proximity, pressure, tilt,
  rotation, slider, wheel, buttons), drag and drop (`data_device`
  enter/motion/drop/leave, offer accept/receive over `SCM_RIGHTS` and finish,
  source target/action/cancelled/`dnd_finished`) and primary selection. The new
  `test_wire_input` slot passes 595 checks; a `wait_object` helper removes the
  server-state race (200 consecutive runs, zero failures); and the facility's
  tablet-manager request table had `get_tablet_seat`/`destroy` transposed, now
  fixed. Test infrastructure only: no public header, symbol or ABI.
- **Live IME loop for niri + fcitx5.** niri 26.04 advertises
  `zwp_input_method_manager_v2` v1 and fcitx5 5.1.23 runs its pinyin engine;
  typing `nihao ` advances the preedit `n` → `ni` → `ni h` → `ni ha` →
  `ni hao` and commits `你好` through text-input-v3 into Pt
  (`PREEDIT_COUNT=5`). The harness injects into the parent display because a
  virtual keyboard on the nested display bypasses the input-method grab. It is
  `tools/live-ime.sh` plus `tests/ime_live_probe.c` (public API only), runs as
  `make check-ime-live`, skips with rc 0 without a parent niri, isolates fcitx5
  on a private session bus, and hardens the run with bounded waits,
  signal-triggered teardown and a focus recheck before injection.

### Behaviour changes

The C ABI change is empty. These observable behaviours changed and are the
section an upgrading application must read:

- **Multi-touch focus is held until the last contact lifts.** In 1.3 the
  platform cleared `touch_focus` when the first contact lifted, so motion and
  up for the remaining fingers were dropped, pinch active state could survive,
  cancel could not clean up, and a repeated pinch failed. 1.4 clears focus only
  at zero remaining contacts, and a surviving single contact updates the
  emulated pointer. Repeated pinch and mid-gesture cancel now behave correctly.
  Pinch and rotate still never produce a click. The helper return type changed
  from `void` to `uint32_t` on an internal interface only; no public signature
  or enum moved, and `tests/test_damage` remains green at 98 checks.
- **SVGZ bytes that previously failed as PNG now load.** A payload starting
  with the gzip magic takes the SVGZ path, is inflated with the 16 MiB bounds,
  and must pass the SVG sniff. hicolor lookup also resolves `.svgz` in
  `scalable/`; a fixed-size directory still resolves `.png` only. Malformed,
  truncated, bad-CRC or non-SVG gzip data returns NULL with
  `pt_get_last_error`, like a corrupt PNG.
- **No other public behaviour changed.** The wire matrix and `wait_object` are
  test-only, and the live IME harness is an external tool.

### Results

| Check | Result |
| --- | --- |
| Version agreement | pass: `include/pt/base.h`, `Makefile` `VERSION`, the README status line and the generated reference all say 1.4.0; SONAME `libpt.so.1` |
| API baseline | pass: `api/pt.api` unchanged at 556 functions / 910 entries; the reference names all 556 baseline functions |
| Since audit | pass: 556/556 declarations carry Since 1.0/1.1/1.2/1.3 (412/36/106/2), zero missing |
| C suites (headless) | pass: 37 executables (36 suites plus the property sweep), 8,991 checks plus the 454,380-check property suite; `test_icon` 800 (up from 330), `test_wire_input` 595, `test_damage` 98, `test_fake_compositor` 275, `test_ime` 314, `test_popup` 466, `test_abi` 13; the AT-SPI bridge reports 236 |
| Bilingual documentation | pass: 9 public guides, no structural drift; code blocks byte-identical |
| Binding parity | pass: 556/556 (100.0%) in C++, Python and Java, 0 missing, 0 allowlisted, unchanged because no function was added |
| `make check` / `make check-bindings` | pass: 36 C suites at 8,991 checks plus the property sweep; C++ 844 checks, Python 67 tests, Java 685 checks, 0 failures; `api-check`, the exported-symbol audit and the generated reference are clean |
| `make perf-check` | pass: every ceiling holds; dense full paint 5.435 ms, dense layout 3.004 ms, 4K full paint 7.585 ms, first frame 14.34 ms, idle 300 ms CPU 0.537 ms (startup bench: idle 500 ms CPU 0.883 ms) |
| Live Wayland / live typing / live IME | pass under niri: `make check-wayland` 48 checks (focused-entry idle 30.6 ms over 5000 ms after one external-input retry); `make check-typing` PASS (ptdirect, text-input-v3); `make check-ime-live` PASS — preedit `n` → `ni` → `ni h` → `ni ha` → `ni hao` and the space commits `你好` into Pt (`PREEDIT_COUNT=5`, `LAST_PREEDIT=ni hao`) |
| Sanitizer, staged install, reproducible tarball, `tools/ci.sh` | pass: `tools/ci.sh` exited 0 (`CI verification passed for Pt 1.4.0`); ASan+UBSan clean rebuild of all 37 C test binaries plus `test_cpp`, the Python suite (67 tests) and the Java suite (685 checks), 0 failures; staged install reports SONAME `libpt.so.1`, pkg-config 1.4.0, an exported ABI matching the baseline, C/C++/Python/Java consumers and `uninstall`; two `make dist` runs at `SOURCE_DATE_EPOCH=1767225600` reproduced one digest (sidecar matches), and the extracted tree rebuilt with `werror=1` and passed `make check` |
| ABI diff against 1.3.0 | pass at the symbol level: 556 → 556 exported `pt_*` symbols, 0 added, 0 removed, no type changes, SONAME `libpt.so.1` unchanged; `abidiff` is still unavailable on the reference machine |
| `tools/release-check.sh --full` | pass: every automated gate is green; only the two human items remain open (freeze commit/tag and live-suite sign-off, both recorded above) |

### ABI statement

The 1.4.0 C surface is unchanged: no function, enum constant, struct, callback
typedef or opaque handle was added, removed or modified, so the ABI delta is
empty by construction. `abidiff` (libabigail) is still not installed on the
reference machine (no package, and `sudo` requires a password), so the
release-time comparison was run at the symbol level: a fresh `make lib` build
of the `1.3.0` tag (`58a561c`, checked out in a temporary worktree) and the gate
build of `e1104c2` were compared with `nm -D --defined-only`. Both export 556
`pt_*` symbols and the sorted symbol-and-type diff is empty: **0 removed, 0
added, no type changes**. `readelf -d` reports `libpt.so.1` on both. The
multi-touch fix changes an internal helper's return type
(`pt_window_input_touch_up`, `void` → `uint32_t`) in `src/internal.h`; it is not
an exported symbol. SVGZ and the wire matrix add no public API. A type-level
`abidiff` run is still recommended on a machine that has libabigail before
publishing.

### Gate transcripts

`tools/ci.sh` on `e1104c2` exited 0 (`CI verification passed for Pt 1.4.0`):

- werror build, `make check` and `make perf-check` in `build/ci/normal`; the
  staged install exercised the C, C++, Python and Java consumers, the
  pkg-config file (1.4.0), the SONAME (`libpt.so.1`) and the baseline, then
  `uninstall` removed the installed paths (`install check passed`).
- Sanitizer pass in a separate `build/ci/sanitize` tree. The script begins with
  `rm -rf build/ci`, so the ASan+UBSan objects are compiled from empty; the log
  shows the fresh `-fsanitize=address,undefined -fno-omit-frame-pointer`
  compiles, all 37 sanitized C test binaries pass, and `test_cpp` (844 checks),
  the Python suite (67 tests) and the Java suite (685 checks) run against the
  sanitized library with zero sanitizer reports. `ldd` on
  `build/ci/sanitize/tests/test_core`,
  `build/ci/sanitize/libpt.so.1.4.0` and
  `build/ci/sanitize/tests/libwire_driver.so` lists `libasan.so.8` and
  `libubsan.so.1`. No gate-script gap was found in this pass; the
  `wire-driver` link that the Java suite needs was already in place.
- Reproducible tarball: two `dist` runs produce
  `d785816c0541b645ffc428f8d4c13eee8585ccf01f821af7ea0b3e108f7aeada` (the
  sidecar matches); the extracted archive builds with `werror=1` and passes
  `make check`.
- Live suites (because `WAYLAND_DISPLAY` was set): `check-wayland` 48 checks
  under niri with focused-entry idle 30.6 ms over 5000 ms after one
  external-input retry; `check-typing` PASS (ptdirect, text-input-v3);
  `check-ime-live` PASS under the parent niri and fcitx5, with
  `PREEDIT_COUNT=5` and `COMMIT=你好`.

### Known issues

- KWin+fcitx5 v2 and Mutter+IBus v2 live composition remain unrecorded; the
  niri+fcitx5 cell is recorded, and the other environments are absent rather
  than declared passing.
- The SVG subset is honest, not complete: text, raster images, filters, masks,
  `clipPath`, `use`/`symbol`, markers, animation and the full CSS cascade are
  ignored by the parser, whose descendants are still visited. A document with
  no drawable shape is rejected.
- Adding a contact mid-pinch keeps the gesture baseline captured at begin
  rather than rebasing it; the first two tracked contacts supply the metrics.
  This is a design boundary, not a defect in the 1.4 focus correction.
- The toplevel-icon submission has no live compositor recording. The wire test
  asserts the submitted parameters and niri/Sway/Weston take the tested no-op
  path, but the matrix's KWin/Labwc/Muffin/Jay/Pila exposures were not run.
- Binding raw-callback dispatch is closed at wire level for all three
  languages; a physical-seat run remains optional live coverage.
- Pickers are locale-independent by design (explicit date picture, `.` decimal
  separator, hex colour). Localized month and group separators remain the
  application's responsibility.

### Committed-tree digest

The gate-time digest above was measured on `e1104c2` with this record still in
its draft (pending) form, before the figures were backfilled; editing this file
necessarily changes the archive. For a reproducible reference on the record
itself, the committed tree that carries the backfilled figures (`66f5e34`, the
parent of the commit that adds this note) produced
`5ad92a0f8f038d3c73d12631511be097131de3972702eb086b08c794773fb2b8` on two
consecutive `dist` runs. Adding this digest note changes `docs/release.md` once
more, so the tagged tree necessarily has a different digest; that is the
self-reference of shipping the record inside the archive, not nondeterminism.
`make dist` on the tagged tree reproduces its own digest exactly.

All sha256 digests in this record — the gate-time `d785816c…` and the
committed-tree `5ad92a0f…` — were measured with
`SOURCE_DATE_EPOCH=1767225600`, the reproducible-build epoch that
`tools/ci.sh` fixes. A bare `make dist` derives its epoch from the last commit
time instead, and that epoch lands in the mtime field of every tar header, so
the same tree digests differently. Extracting both archives gives
byte-identical trees; only the timestamp header differs. To reproduce a digest
quoted here, run `SOURCE_DATE_EPOCH=1767225600 make dist`, not a bare
`make dist`.

The annotated tag is created from the final gate tree once the owner confirms
the freeze. `tools/release-check.sh --full` reports every automated gate green;
the two open items it always reports are the human ones (freeze commit/tag and
live-suite sign-off), and the live suites above were run on this tree.

## 1.3.0 validation record (2026-11-22)

Environment: the final gate tree `fafbcae` on `m20/integration` on the
reference machine (CachyOS, Wayland, niri 26.04) with the 1.0 toolchain
(GCC 16.2.1, Python 3.14.8, OpenJDK 25.0.2). The four wave-1 workstreams, the
wave-2 wire consumers, the C++ hardening branch, the bind-final wrappers and
the integration baseline refresh are merged; the version bump, generated
reference and bilingual guides land on top in this documentation branch.

This is the **final gate record**. Every figure below was measured on
`fafbcae`, including the sanitizer pass, the staged install, the reproducible
tarball and the live suites. The only step left before the tag is the owner's
freeze confirmation.

### Scope

1.3.0 keeps every 1.2 signature and adds an append-only surface of two
functions and one accessibility enum value, plus a protocol-level test facility
that changes no public ABI:

- IME input panel: `pt_window_show_input_panel` and
  `pt_window_hide_input_panel` (since 1.3), forwarding the text-input-v3 v2
  requests with a remembered boolean desired state, false for an unchanged
  state or a NULL window, v1 suppression and one replay on IME reactivation.
- IME `preedit_hint` presentation: the seven hint kinds render as a second,
  graded underline over the plain preedit; a preedit without hints is
  pixel-identical to 1.2.
- SVG icon sources: `pt_icon_image` sniffs SVG content through vendored
  nanosvg, hicolor `scalable/` lookup resolves `.svg`, and rasterisation enters
  the existing per-size/scale cache. No new public function; the subset and the
  ignore rule are documented in `pt/icon.h`.
- Spin-button role: `pt_a11y_role_spin_button` is appended as value 36; the
  number picker reports it with no expandable state and the AT-SPI bridge maps
  it to `SPIN_BUTTON`.
- Fake-compositor wire verification: `tests/fake_compositor.h` is test-only
  infrastructure (no public header, symbol or ABI), and `tests/wire_driver.c`
  exposes it to the C++/Python/Java suites for real wire key and pointer
  dispatch.
- C++ hardening: the five owned-string/buffer getters use an RAII guard so
  `std::bad_alloc` cannot leak, and `pt::popup` documents its parent-window
  lifetime constraint.

The public surface is **556 functions** in **910 baseline entries** (556
`func`, 282 `enum`, 43 `callback`, 10 `struct`, 12 `opaque`, 6 `define`, 1
`type`). The diff against the 907-entry 1.2 baseline is two functions and one
enum constant, with no removals, renames or signature changes; SONAME stays
`libpt.so.1`. Every declaration carries a since marker (412 × 1.0, 36 × 1.1,
106 × 1.2, 2 × 1.3) and the generated reference shows it per entry.

### Behaviour changes

The C ABI change is additive; no existing signature or enum value moved. These
observable behaviours changed and are the section an upgrading application
must read:

- **The number picker now reports the spin-button accessibility role.** 1.2
  reported `pt_a11y_role_combo_box` with no expandable state; 1.3 reports
  `pt_a11y_role_spin_button` (value 36) and the AT-SPI bridge exposes
  `SPIN_BUTTON`. This is an intentional semantic correction, not an accident:
  a number field is a value plus step buttons, not a collapsible choice list.
  An application or test that matched the old combo-box role on a number picker
  must match the spin-button role. `pt_a11y_role_count` moves from 36 to 37, so
  code that used the sentinel as a bound sees one more value.
- **SVG bytes that previously failed as PNG now load.** The loader sniffs an
  optional BOM and whitespace, then treats an `<svg` or `<?xml` prefix as SVG;
  every other stream stays on the byte-for-byte PNG path. A malformed or
  shapeless SVG returns NULL with `pt_get_last_error`, like a corrupt PNG.
  hicolor lookup resolves `scalable/` icons that 1.2 could not find.
- **IME preedit hints are visual only.** Hint ranges are clamped to the preedit,
  snapped to UTF-8 boundaries, capped at 256 and painted in arrival order;
  unknown values draw nothing. They do not change commit text, the local
  composition buffer or the callback ABI, and a preedit without hints paints
  exactly as in 1.2.
- **Input-panel requests are hints with a state machine.** The functions return
  true only when the recorded desired state changed. With no active text-input
  object, a v1-only compositor or a headless window, no request is sent and the
  state is remembered; IME reactivation forwards it once. The protocol makes a
  repeated matching request harmless, so replay has no side effect. Whether an
  on-screen keyboard appears is not observable.
- **C++ owned getters cannot leak on `bad_alloc`,** and `pt::popup` must be
  destroyed before its parent window. Both are binding-level contracts; the C
  behaviour is unchanged.

### Results

| Check | Result |
| --- | --- |
| Version agreement | pass: `include/pt/base.h`, `Makefile` `VERSION`, README and the generated reference all say 1.3.0; SONAME `libpt.so.1` |
| API baseline | pass: `api/pt.api` 556 functions / 910 entries; the reference names all 556 baseline functions |
| Since audit | pass: 556/556 declarations carry Since 1.0/1.1/1.2/1.3 (412/36/106/2); generated entries expose them |
| C suites (headless) | pass: 36 executables (35 suites plus the property sweep), 7,702 checks plus the 454,380-check property suite; `test_fake_compositor` 275, `test_ime` 314, `test_popup` 466, `test_icon` 330, `test_picker` 553, `test_abi` 13; the AT-SPI bridge reports 11 |
| ABI pins | pass: `tests/test_abi.c` 13 checks, including `pt_a11y_role_spin_button` = 36 and the count sentinel = 37 |
| Bilingual documentation | pass: 9 public guides, no structural drift; code blocks byte-identical |
| Binding parity | pass: 556/556 (100.0%) in C++, Python and Java, 0 missing, 0 allowlisted |
| `make check` / `make check-bindings` | pass: 35 C suites at 7,702 checks plus the property sweep; C++ 844 checks, Python 67 tests, Java 685 checks, 0 failures; `api-check` clean |
| `make perf-check` | pass: every ceiling holds; dense full paint 5.262 ms, 4K full paint 8.469 ms, first frame 14.512 ms, idle 300 ms CPU 0.598 ms |
| Live Wayland / live typing | pass: `make check-wayland` 48 checks under niri (focused-entry idle 25.1 ms over 5000 ms after one external-input retry); `make check-typing` PASS (ptdirect, text-input-v3) |
| Sanitizer, staged install, reproducible tarball, `tools/ci.sh` | pass: `tools/ci.sh` exit 0 (`CI verification passed for Pt 1.3.0`); ASan+UBSan clean rebuild of all 36 C test binaries plus `test_cpp`, the Python suite (67 tests) and the Java suite (685 checks); staged install with C/C++/Python/Java consumers and `uninstall`; two `dist` runs reproduce one digest and the extracted tarball rebuilds and passes `make check` |
| ABI diff against 1.2.0 | pass at the symbol level: 554 → 556 exported `pt_*` symbols, 0 removed, 2 added, no type changes among the 554 common symbols, SONAME `libpt.so.1` unchanged; `abidiff` remains unavailable on the reference machine |
| `tools/release-check.sh --full` | pass: every automated gate is green; only the two human items remain open (freeze commit/tag and live-suite sign-off, both recorded above) |

### ABI statement

The 1.3.0 change is append-only within `libpt.so.1`: two new functions and one
appended enum constant. No existing enum value, struct field order, callback
typedef or signature changed. `tests/test_abi.c` pins the spin-button value and
the moved count sentinel. `abidiff` (libabigail) is still not installed on the
reference machine (no package and no elevated privileges to add one), so the
release-time comparison was run at the symbol level: a fresh `make lib` build of
the `1.2.0` tag (`fc7df8d`, in a temporary worktree) and the gate build of
`fafbcae` were compared with `nm -D --defined-only`. The 1.2 build exported 554
`pt_*` symbols and the 1.3 build exports 556: **0 removed, 2 added**
(`pt_window_show_input_panel` and `pt_window_hide_input_panel`), all 554 common
symbols keep the same `T` type, and `readelf -d` reports `libpt.so.1` on both.
The enum count sentinel (36 → 37) and the appended `pt_a11y_role_spin_button`
are baseline entries, not exported symbols, so they do not appear in the `nm`
comparison; `tests/test_abi.c` and the baseline/reference audits cover them. A
type-level `abidiff` run is still recommended on a machine that has libabigail
before publishing.

### Final integration surface

All 556 functions are wrapped in C++, Python and Java, with full parity
(556/556, 0 missing, empty allowlist): C++ 844 checks, Python 67 tests and Java
685 checks. The bind-final wrappers landed as
`window::show_input_panel`/`hide_input_panel`, `Window.show_input_panel`/
`hide_input_panel` and `Pt.window_show_input_panel`/`window_hide_input_panel`
with their wire dispatch tests, matching the names in [bindings.md](bindings.md).
The fake-compositor facility and its three-language driver are test
infrastructure and add no installable artifact.

### Gate transcripts

`tools/ci.sh` on `fafbcae` exited 0 (`CI verification passed for Pt 1.3.0`):

- werror build, `make check` and `make perf-check` in `build/ci/normal`; the
  staged install exercised the C, C++, Python and Java consumers, the
  pkg-config file, the SONAME and the baseline, then `uninstall` removed the
  installed paths (`install check passed`).
- Sanitizer pass in a separate `build/ci/sanitize` tree. The script begins with
  `rm -rf build/ci`, so the ASan+UBSan objects are compiled from empty; the log
  shows the fresh `-fsanitize=address,undefined -fno-omit-frame-pointer`
  compiles, all 36 sanitized test binaries pass, and `test_cpp` (844 checks),
  the Python suite (67 tests) and the Java suite (685 checks) run against the
  sanitized library. `ldd` on `build/ci/sanitize/tests/test_core`,
  `build/ci/sanitize/libpt.so.1.3.0` and
  `build/ci/sanitize/tests/libwire_driver.so` lists `libasan.so.8` and
  `libubsan.so.1`. One gate-script gap was fixed in this pass (not a library
  defect): the script now builds `wire-driver` in the sanitize tree before the
  Java suite, which loads `libwire_driver.so` from the library's sibling
  `tests/` directory.
- Reproducible tarball: two `dist` runs produce
  `e989e6a4c837145f2bc92247cdad27ded9ffa500331518bf904693260ea36a62` (the
  sidecar matches); the extracted archive builds with `werror=1` and passes
  `make check`.
- Live suites (because `WAYLAND_DISPLAY` was set): `check-wayland` 48 checks
  under niri and `check-typing` PASS (ptdirect).

Because this record ships inside the archive, the digest above is the gate-time
archive of the `fafbcae` tree with the `tools/ci.sh` sanitizer fix applied and
before this record was backfilled; editing this file necessarily changes the
digest. `tools/ci.sh` proves the archive is reproducible by building `dist`
twice and comparing the two digests, so the tagged tree reproduces its own
digest exactly — re-measure there rather than trusting a digest quoted from
inside the archive.

### Known issues

- Live IME composition under niri+fcitx5 or KWin/Mutter was not recorded. Wire
  replay covers text-input v3 composition, hints and input panels; this is not
  a live environment result.
- SVGZ and the unsupported part of the SVG subset (text, raster images,
  filters, masks, `clipPath`, `use`/`symbol`, markers, animation, full CSS
  cascade) are ignored by the parser, whose descendants are still visited. A
  document with no drawable shape is rejected.
- The fake compositor does not inject `wl_touch` or tablet sequences; those
  paths still rely on headless state tests and the live suite.
- The toplevel-icon submission has no live compositor recording. The wire test
  asserts the submitted parameters and niri/Sway/Weston take the tested no-op
  path, but the matrix's KWin/Labwc/Muffin/Jay/Pila exposures were not run.
- Binding raw-callback dispatch is closed at wire level for all three languages;
  a physical-seat run remains optional live coverage.
- Pickers are locale-independent by design (explicit date picture, `.` decimal
  separator, hex colour). Localized month and group separators remain the
  application's responsibility.

### Committed-tree digest

The gate-time digest above was measured on the `fafbcae` tree with the
`tools/ci.sh` fix applied, before this record was backfilled, and the edit
changes the archive. For a reproducible reference on
the record itself, the evidence commit `852983d` (this record backfilled, but
before this digest note was added) produced
`aa6eb67443b717be1d3510bb983f14c22e5dcb8409c4ef821a521a9762c94aab` on two
consecutive `dist` runs. Adding this note changes `docs/release.md` once more,
so the final tree necessarily has a different digest; that is the
self-reference of shipping the record inside the archive, not nondeterminism.
`make dist` on the tagged tree reproduces its own digest exactly.

All sha256 digests in this record — the gate-time `e989e6a4…`, the
evidence-commit `aa6eb674…` and the final tree `589d2c8d…` (the tagged tree
`58a561c`, which does not carry this note) — were measured with
`SOURCE_DATE_EPOCH=1767225600`, the reproducible-build epoch that
`tools/ci.sh` fixes. A bare `make dist` derives its epoch from the last
commit time instead (`1795320732` for `58a561c`), and that epoch lands in the
mtime field of every tar header, so the same tree digests to
`1790853da866d320a37671000f3db9d0f863d000d6f052bd809b9a63be2eab80`. Extracting
both archives gives byte-identical trees; only the timestamp header differs.
To reproduce a digest quoted here, run
`SOURCE_DATE_EPOCH=1767225600 make dist`, not a bare `make dist`.

The annotated tag is created from the final gate tree once the owner confirms
the freeze. `tools/release-check.sh --full` reports every automated gate green;
the two open items it always reports are the human ones (freeze commit/tag and
live-suite sign-off), and the live suites above were run on this tree.

## Packaging notes

- Runtime dependencies are wayland-client, wayland-cursor, xkbcommon,
  fontconfig, freetype2, harfbuzz, zlib and libm. When built with the optional
  AT-SPI2 bridge, libdbus is also linked and listed in `Requires.private`.
- The pkg-config file puts those in `Requires.private` and only `-lm` in
  `Libs.private`; public headers expose no external types.
- The install layout is:
  - `$PREFIX/lib/libpt.so.1.3.0`, `libpt.so.1`, `libpt.so`, `libpt.a`
  - `$PREFIX/include/pt/*.h`, including `pt.hpp`
  - `$PREFIX/lib/pkgconfig/pt.pc`
  - `$PREFIX/share/pt/bindings/python/pt.py`
  - `$PREFIX/share/pt/bindings/java/src/pt/Pt.java`
- `make uninstall` removes exactly those paths. It does not remove unrelated
  files under the prefix.
- `make dist` excludes `build/`, `.git/` and Python bytecode. Vendored protocol
  XML is included; generated protocol code is built during the tarball build.
  The tarball needs `wayland-scanner` at build time.

## Release notes template

```markdown
## Pt X.Y.Z

### Highlights
- ...

### Fixes
- ...

### Known issues
- ...

### Validation
- Toolchain:
- Test counts:
- Benchmarks:
- SHA-256:
```

Do not claim a platform or scale was tested unless the validation record says
so. Do not publish a tarball that fails the clean-tarball build.
