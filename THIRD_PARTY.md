# Third-party notices

Pt vendors standard Wayland protocol XML files so that a source checkout builds
without depending on the exact wayland-protocols installation layout, one
header-only SVG library so that icons work without an external SVG dependency,
the SheenBidi Unicode bidi engine, and the Unicode conformance test data.
Each vendored file retains its upstream license header.

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
  apart from the added `PROVENANCE.md`, and keep their upstream Apache-2.0
  headers. Pt compiles the engine as one unity translation unit
  (`-DSB_CONFIG_UNITY`) with third-party warnings isolated in the Makefile.
  UCD tables are from Unicode 17.0.0.
- Upgrade policy: see `third_party/sheenbidi/PROVENANCE.md`.

## System libraries

Required system libraries are not bundled. They are dynamically linked:
Wayland client/cursor, xkbcommon, Fontconfig, FreeType, HarfBuzz, zlib and libm.
When `dbus-1` development files are available, Pt also links libdbus and enables
the optional AT-SPI2 bridge (`PT_HAVE_DBUS`); without them the bridge reports
`pt_a11y_status_unavailable`.