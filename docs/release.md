# Release process

Pt follows semantic versioning. The shared object carries SONAME `libpt.so.1`
from 1.0.0 onward; the 0.x series used `libpt.so.0`. The pkg-config version
tracks `pt_version_string`.

## Version checklist

Before tagging a release:

1. Update `VERSION` in the Makefile and `pt_version_major`,
   `pt_version_minor`, `pt_version_patch` and `pt_version_string` in
   `include/pt/base.h`.
2. Update the status line and any new limitations in `README.md`,
   `docs/ROADMAP.md`, `docs/performance.md` and `docs/comparison.md`.
3. Run the validation matrix below from a clean tree. For a 1.0 release also
   run `tools/release-check.sh --full`, which turns the machine-verifiable half
   of the checklist in [api-policy.md](api-policy.md) into a pass/fail report.
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
  API-baseline audit, 412/412 binding parity, the 0.6-0.10 surface audits,
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
  while text-input-v3 is enabled.

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

## Packaging notes

- Runtime dependencies are wayland-client, wayland-cursor, xkbcommon,
  fontconfig, freetype2, harfbuzz, zlib and libm. When built with the optional
  AT-SPI2 bridge, libdbus is also linked and listed in `Requires.private`.
- The pkg-config file puts those in `Requires.private` and only `-lm` in
  `Libs.private`; public headers expose no external types.
- The install layout is:
  - `$PREFIX/lib/libpt.so.1.0.0`, `libpt.so.1`, `libpt.so`, `libpt.a`
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
