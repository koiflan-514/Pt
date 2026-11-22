# Third-party notices

Pt vendors standard Wayland protocol XML files so that a source checkout builds
without depending on the exact wayland-protocols installation layout, one
header-only SVG library so that icons work without an external SVG dependency,
the SheenBidi Unicode bidi engine, and the Unicode conformance test data.
Each vendored file keeps its upstream license terms. The generated SheenBidi
lookup tables carry only the generator banner; the engine's `LICENSE` covers
them.

| Path | Upstream | License |
| --- | --- | --- |
| `protocols/xdg-shell.xml` | wayland-protocols (stable) | MIT |
| `protocols/xdg-decoration-unstable-v1.xml` | wayland-protocols (unstable) | MIT |
| `protocols/viewporter.xml` | wayland-protocols (stable) | MIT |
| `protocols/fractional-scale-v1.xml` | wayland-protocols (staging) | MIT |
| `protocols/text-input-unstable-v3.xml` | wayland-protocols (unstable) | MIT |
| `protocols/pointer-gestures-unstable-v1.xml` | wayland-protocols (unstable) | MIT |
| `src/nanosvg.h`, `src/nanosvgrast.h` | nanosvg (master) | zlib |
| `third_party/sheenbidi/` | SheenBidi v3.0.0 | Apache-2.0 |
| `tests/data/bidi/` | Unicode BidiTest / BidiCharacterTest 18.0.0 | Unicode License v3 |

## nanosvg

- Upstream: <https://github.com/memononen/nanosvg> (`master/src/nanosvg.h`,
  `master/src/nanosvgrast.h`)
- Fetched: 2026-11-22
- Vendored at: `src/nanosvg.h` and `src/nanosvgrast.h`
- SHA-256 of the fetched upstream files:
  - `nanosvg.h`: `e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0`
  - `nanosvgrast.h`: `79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8`
- Modifications: unmodified apart from a provenance banner prepended to each
  file. Pt's bounded integration (size and dimension caps, parsing policy)
  lives in `src/svg.c`, not in the vendored copies.

The upstream zlib license, reproduced verbatim:

```text
Copyright (c) 2013-14 Mikko Mononen memon@inside.org

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## SheenBidi

- Upstream: <https://github.com/Tehreer/SheenBidi>
- Tag: `v3.0.0`, commit `cfe430e7375a7845b679adae9d51dac6deaa8858`
- Fetched: 2026-11-22
- Vendored at: `third_party/sheenbidi/` (`Source/`, `Headers/`, `LICENSE`,
  `README.md`; no upstream tools or tests)
- Tarball SHA-256:
  `86c56014034739ba39a24c23eb00323b0bf6f737354f665786015fca842af786`
- License: Apache License 2.0. Upstream ships no separate NOTICE file, so no
  NOTICE is reproduced; the license text is in `third_party/sheenbidi/LICENSE`.
- Modifications: none. The sources and headers are byte-identical to the tag
  apart from the added `PROVENANCE.md`; hand-written files keep their upstream
  Apache-2.0 headers and the generated `Source/Data/*Lookup.c` files keep the
  upstream generator banner (see `PROVENANCE.md`). Pt compiles the engine as
  one unity translation unit
  (`-DSB_CONFIG_UNITY`) with third-party warnings isolated in the Makefile.
  UCD tables are from Unicode 17.0.0.
- Upgrade policy: see `third_party/sheenbidi/PROVENANCE.md`.

## Unicode bidi conformance data

- Upstream: Unicode Character Database 18.0.0,
  <https://www.unicode.org/Public/18.0.0/ucd/>
- Fetched: 2026-11-22
- Vendored at: `tests/data/bidi/`
- `BidiTest.txt.gz` — decompresses to
  `9af2f882a4ab50912e388f069a673b94eacd82fa6d07d20a3ff7f3c759e905aa`
  (source: <https://www.unicode.org/Public/18.0.0/ucd/BidiTest.txt>)
- `BidiCharacterTest.txt.gz` — decompresses to
  `045b24d2c8ab066951bd32fe8c6b4de34647f72b5b1c7df0265f24ab53573e01`
  (source: <https://www.unicode.org/Public/18.0.0/ucd/BidiCharacterTest.txt>)
- `LICENSE.unicode.txt` — Unicode License v3,
  <https://www.unicode.org/license.txt>,
  SHA-256 `e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96`
- The gzip streams are produced with `gzip -9n` so the compressed bytes are
  reproducible. The test data is versioned independently of the engine's
  17.0.0 UCD tables; the 18.0.0 files differ only in their header comments.

## System libraries

Required system libraries are not bundled. They are dynamically linked:
Wayland client/cursor, xkbcommon, Fontconfig, FreeType, HarfBuzz, zlib and libm.
When `dbus-1` development files are available, Pt also links libdbus and enables
the optional AT-SPI2 bridge (`PT_HAVE_DBUS`); without them the bridge reports
`pt_a11y_status_unavailable`.