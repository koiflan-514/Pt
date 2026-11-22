/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_icon_h
#define pt_icon_h

#include <pt/base.h>

/* Header contract
 *
 * Threading. Every function in this header runs on the UI thread that owns the
 * application, including getters and reference counting. There is no locking.
 *
 * Naming. The public enum pt_icon names Pt's built-in vector glyphs (see
 * widget.h); the raster image handle is pt_icon_image. Names could not be
 * shared without breaking the 1.x ABI, so every new raster function carries the
 * pt_icon_image prefix.
 *
 * Ownership and lifetime. pt_icon_image is reference counted. A load or lookup
 * returns one reference owned by the caller. pt_icon_image_ref returns the same
 * pointer with one more reference and pt_icon_image_destroy drops one
 * reference; the last drop frees the decoded pixels and every cached
 * rasterisation. pt_button_set_icon_image and pt_window_set_icon take their own
 * reference, so the caller may drop its reference immediately after setting;
 * passing NULL clears the slot. Getter results are borrowed and become invalid
 * when the owning widget/window is destroyed, the slot is replaced or the
 * caller drops its own reference (whichever comes first).
 *
 * Errors. Loads and lookups return NULL and set pt_get_last_error on failure:
 * missing or unreadable files, corrupt, empty or unsupported PNG/SVG/SVGZ data,
 * dimensions above PT_ICON_MAX_DIMENSION, SVG documents with no drawable
 * shape, and theme names that do not resolve. A failed lookup never crashes or
 * aborts. Setters return void and ignore NULL objects.
 *
 * Image support. The loader sniffs the payload. A payload whose first two bytes
 * are the gzip magic 0x1f 0x8b takes the SVGZ path (see below); no other
 * compressed container is recognised. Every other payload follows the text
 * sniff: after an optional UTF-8 BOM and ASCII whitespace, a '<' that opens
 * `<svg` or an `<?xml` declaration selects the SVG path; every other byte
 * stream stays on the byte-for-byte PNG path.
 *
 * PNG. 8-bit non-interlaced colour types 0/2/3/4/6, decoded through the same
 * bounded decoder as clipboard images (16 MiB input cap, 8192x8192 decoder cap)
 * with an icon-specific PT_ICON_MAX_DIMENSION cap applied before decoding.
 *
 * SVG. Parsed by the vendored zlib-licensed nanosvg and rasterised at each
 * requested device size into the same per-size/scale cache. Supported: rect,
 * circle, ellipse, line, polyline, polygon and path (including arcs), groups,
 * transforms, fill and stroke paints, stroke width/cap/join/dash, opacity,
 * fill-rule, linear and radial gradients, defs, inline `style="..."`
 * declarations and simple `.class` rules from a `<style>` element. Not
 * supported: text, images, filters, masks, clipPath, use/symbol references,
 * markers, animation and the full CSS cascade (id, type and descendant
 * selectors). Unsupported elements are ignored by the parser; their
 * descendants are still visited, so a `<text>` child shape would draw. A
 * document with no usable intrinsic size, an empty document or a truncated
 * document with no complete shape is rejected with an error. SVG source is
 * capped at the same 16 MiB input limit and intrinsic dimensions at
 * PT_ICON_MAX_DIMENSION. Rasterisation preserves the intrinsic aspect ratio
 * and centres the artwork in the requested pixel box (PNG, by contrast, is
 * resized to the exact box).
 *
 * SVGZ. A gzip-compressed SVG document is accepted by both loaders and by
 * hicolor lookup. Compression is detected from the first two bytes (0x1f 0x8b),
 * never from a file name, and gzip is the only container understood. The
 * compressed input is subject to the same 16 MiB input cap, and the inflated
 * document is capped at 16 MiB as well: a stream that would inflate past that
 * bound is rejected rather than grown (the decompression-bomb guard). The
 * inflated bytes must pass the same SVG sniff and the same intrinsic-size,
 * shape and PT_ICON_MAX_DIMENSION rules, so a gzip-wrapped PNG, gzip-wrapped
 * non-SVG text or any other gzip payload is rejected with an error. Malformed,
 * truncated and bad-CRC gzip streams return NULL with an error and never crash.
 * The handle retains the inflated SVG source, bounded by the same 16 MiB cap,
 * and re-parses it at each requested size exactly as a plain SVG handle does;
 * the compressed bytes are not retained and there is no separate SVGZ cache or
 * tuning knob.
 *
 * Windows. pt_window_set_icon is best effort. When the compositor advertises
 * xdg_toplevel_icon_v1, Pt rasterises the image to a dedicated, immutable
 * wl_shm buffer (never a frame buffer), attaches it with the window's scale and
 * keeps the buffer alive until the icon is replaced or the window is destroyed.
 * Without the global, in headless mode and under compositors that do not expose
 * it (niri, Sway, Weston), the call is a silent no-op: no error is recorded and
 * the image is still retained so a getter and later submission see it. Pt
 * re-submits on the next window scale change.
 *
 * Caching. Rasterised per-size/scale results are cached. The cache is global
 * and bounded by PT_ICON_CACHE_MAX_ENTRIES and PT_ICON_CACHE_MAX_PIXELS across
 * all live handles, with at most PT_ICON_IMAGE_CACHE_MAX_ENTRIES per handle;
 * least-recently-used entries are evicted. A cache miss that cannot fit returns
 * the untinted fallback path (the icon simply does not draw) rather than
 * growing without bound.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Largest accepted source dimension on either side, for PNG pixels and for an
 * SVG's intrinsic width/height and raster request. Checked from the PNG IHDR
 * before the full decode, so an oversized header fails without allocating a
 * frame-sized buffer. */
#define PT_ICON_MAX_DIMENSION 4096

/* Bounds of the global raster cache. The pixel cap counts device pixels, not
 * bytes; at four bytes per pixel it is a 16 MiB ceiling. */
#define PT_ICON_CACHE_MAX_ENTRIES 32u
#define PT_ICON_CACHE_MAX_PIXELS 4194304u
#define PT_ICON_IMAGE_CACHE_MAX_ENTRIES 8u

/* hicolor lookup bounds. A candidate path never exceeds PT_ICON_PATH_MAX
 * bytes, at most PT_ICON_SEARCH_DIRS_MAX theme directories are inspected and an
 * icon name is at most PT_ICON_NAME_MAX bytes. */
#define PT_ICON_PATH_MAX 4096u
#define PT_ICON_SEARCH_DIRS_MAX 32u
#define PT_ICON_NAME_MAX 128u

/* Window-icon submission geometry: the logical box requested from the
 * compositor and the largest integer buffer scale Pt submits. */
#define PT_ICON_WINDOW_SIZE 32
#define PT_ICON_WINDOW_MAX_SCALE 4

/* Opaque reference-counted raster icon. */
typedef struct pt_icon_image pt_icon_image;

/* Loads a PNG, SVG or SVGZ file, sniffed by content. The file must be a regular
 * file no larger than the 16 MiB input cap. Since 1.2; SVGZ since 1.4. */
pt_api pt_icon_image *pt_icon_image_load_file(const char *path);

/* Loads PNG, SVG or SVGZ bytes from memory, sniffed by content. The data is
 * copied during the call; the caller keeps ownership of the input buffer.
 * Since 1.2; SVGZ since 1.4. */
pt_api pt_icon_image *pt_icon_image_load_memory(const void *data, size_t size);

/* Looks a name up in the hicolor theme, following the XDG base directories
 * ($XDG_DATA_HOME/icons, $XDG_DATA_DIRS/icons) plus ~/.icons. `size` is the
 * preferred logical size in pixels and selects the closest available size.
 * index.theme is parsed for its Directories/Size/Type fields; without a usable
 * index.theme the NxN and scalable directories are scanned. Fixed size
 * directories are searched for .png files only; scalable directories are
 * searched in the order .svg, .svgz, then .png. A name containing a path
 * separator, an empty name or a miss returns NULL. Since 1.2; .svgz lookup
 * since 1.4. */
pt_api pt_icon_image *pt_icon_image_from_name(const char *name, int size);

/* Adds a reference and returns the same handle. NULL is accepted and returns
 * NULL. Since 1.2. */
pt_api pt_icon_image *pt_icon_image_ref(pt_icon_image *image);

/* Drops a reference. NULL is accepted. The last drop frees the image.
 * Since 1.2. */
pt_api void pt_icon_image_destroy(pt_icon_image *image);

/* Source dimensions in pixels; 0 for NULL. Since 1.2. */
pt_api int pt_icon_image_width(const pt_icon_image *image);
/* Since 1.2. */
pt_api int pt_icon_image_height(const pt_icon_image *image);

/* Button icon slot. The image is laid out before the text when the button has
 * text, or centred alone for an icon-only button. The icon's logical size
 * follows the button height (button->icon_size when set through the built-in
 * glyph API, otherwise 18 logical pixels, clamped to the control height). The
 * button takes a reference; NULL clears the image and falls back to
 * pt_widget_set_icon's vector glyph. Since 1.2. */
pt_api void pt_button_set_icon_image(pt_widget *button, pt_icon_image *image);
/* Since 1.2. */
pt_api pt_icon_image *pt_button_get_icon_image(const pt_widget *button);

/* Optional theme tint for the button image slot. `tint` is 0xRRGGBBAA; passing
 * 0 (transparent) restores untinted drawing, preserving the image's own
 * colours. The tint replaces the image's RGB with the tint colour scaled by the
 * image alpha, so glyph-shaped masks are drawn in the theme foreground colour.
 * Applies to buttons only; ignored for other kinds. Since 1.2. */
pt_api void pt_button_set_icon_tint(pt_widget *button, pt_color tint);

/* Window icon (best effort, see the header contract). Takes a reference; NULL
 * removes the icon. Since 1.2. */
pt_api void pt_window_set_icon(pt_window *window, pt_icon_image *image);
/* Since 1.2. */
pt_api pt_icon_image *pt_window_get_icon(const pt_window *window);

#ifdef __cplusplus
}
#endif

#endif
