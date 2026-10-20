# Third-party notices

Pt vendors only standard Wayland protocol XML files so that a source checkout
builds without depending on the exact wayland-protocols installation layout.
Each file retains its upstream MIT license header.

| Path | Upstream | License |
| --- | --- | --- |
| `protocols/xdg-shell.xml` | wayland-protocols (stable) | MIT |
| `protocols/xdg-decoration-unstable-v1.xml` | wayland-protocols (unstable) | MIT |
| `protocols/viewporter.xml` | wayland-protocols (stable) | MIT |
| `protocols/fractional-scale-v1.xml` | wayland-protocols (staging) | MIT |
| `protocols/text-input-unstable-v3.xml` | wayland-protocols (unstable) | MIT |
| `protocols/pointer-gestures-unstable-v1.xml` | wayland-protocols (unstable) | MIT |

Required system libraries are not bundled. They are dynamically linked:
Wayland client/cursor, xkbcommon, Fontconfig, FreeType, HarfBuzz, zlib and libm.
When `dbus-1` development files are available, Pt also links libdbus and enables
the optional AT-SPI2 bridge (`PT_HAVE_DBUS`); without them the bridge reports
`pt_a11y_status_unavailable`.
