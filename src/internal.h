/*
 * Pt internal interfaces. Not installed.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_internal_h
#define pt_internal_h

#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include <pt/pt.h>
#include <pt/popup.h>

/* ---------- overlays ---------- */

typedef struct pt_rect {
    float x;
    float y;
    float w;
    float h;
} pt_rect;

typedef struct pt_overlay_action {
    pt_click_callback callback;
    void *user_data;
    struct pt_overlay *overlay;
    struct pt_overlay_action *next;
} pt_overlay_action;

typedef enum pt_overlay_kind {
    pt_overlay_kind_toast = 0,
    pt_overlay_kind_menu,
    pt_overlay_kind_dialog,
    pt_overlay_kind_tooltip,
    pt_overlay_kind_combobox,
    pt_overlay_kind_popup
} pt_overlay_kind;

typedef struct pt_wl_popup pt_wl_popup;

typedef struct pt_overlay {
    pt_overlay_kind kind;
    pt_window *window;
    pt_widget *panel;
    /* Set by popups built outside overlay.c, such as a combo box list, so the
     * generic dismiss paths can notify the owning control. */
    pt_widget *owner;
    pt_rect rect;
    float anchor_x;
    float anchor_y;
    bool open;
    bool passive;
    bool modal;
    bool native;
    pt_wl_popup *popup;
    uint64_t timer_id;
    pt_widget *previous_focus;
    pt_overlay_action *actions;
    struct pt_overlay *next;
} pt_overlay;

typedef struct pt_overlay_garbage {
    pt_widget *panel;
    pt_overlay_action *actions;
    struct pt_overlay_garbage *next;
} pt_overlay_garbage;

/* Application-owned popup (1.2). overlay must remain the first member so the
 * generic overlay layer can cast between the two without a side table. */
struct pt_popup {
    pt_overlay overlay;
    pt_widget *content;
    pt_popup_anchor anchor;
    pt_popup_placement placement;
    pt_popup_dismiss_callback on_dismiss;
    void *on_dismiss_data;
    bool dismissing;
    /* Set when the compositor's xdg_popup/xdg_surface configure supplied the
     * final native position. Until then layout computes the requested anchor
     * position; afterwards it keeps the compositor's flip/slide result. */
    bool native_placed;
};

/* A widget destroyed from inside a paint callback. The free is deferred until
 * the current paint pass returns so the traversal never touches freed memory. */
typedef struct pt_paint_garbage {
    pt_widget *widget;
    struct pt_paint_garbage *next;
} pt_paint_garbage;

typedef struct pt_menu_item {
    char *label;
    char *shortcut;
    pt_click_callback callback;
    void *user_data;
    bool separator;
} pt_menu_item;

struct pt_menu {
    pt_overlay overlay;
    pt_menu_item *items;
    size_t item_count;
    size_t item_capacity;
};

struct pt_dialog {
    pt_overlay overlay;
    char *title;
    char *message;
    pt_widget *title_label;
    pt_widget *message_label;
    pt_widget *actions_row;
    pt_dialog_callback on_close;
    void *on_close_data;
    bool closing;
};

struct pt_toast {
    pt_overlay overlay;
    pt_toast_kind kind;
    char *text;
    uint32_t duration_ms;
    struct pt_toast *next;
};

typedef struct pt_tooltip {
    pt_overlay overlay;
    pt_widget *target;
} pt_tooltip;

#if defined(__GNUC__) || defined(__clang__)
#define pt_printf_like(fmt_index, arg_index) \
    __attribute__((format(printf, fmt_index, arg_index)))
#else
#define pt_printf_like(fmt_index, arg_index)
#endif

#define pt_unused(x) ((void)(x))
#define pt_len(x) (sizeof(x) / sizeof((x)[0]))
#define pt_min_float(a, b) ((a) < (b) ? (a) : (b))
#define pt_max_float(a, b) ((a) > (b) ? (a) : (b))
#define pt_clamp_float(v, lo, hi) pt_max_float((lo), pt_min_float((v), (hi)))
#define pt_buffer_count 3
#define pt_clip_stack_max 32
#define pt_header_height 44.0f
#define pt_resize_margin 6.0f
#define pt_entry_max_bytes 65536u
#define pt_default_font_size 14.0f
#define pt_animation_epsilon 0.002f

typedef struct pt_layout_ctx {
    float scale;
    const char *font_family;
} pt_layout_ctx;

typedef struct pt_canvas pt_canvas;

typedef enum pt_cursor {
    pt_cursor_default = 0,
    pt_cursor_text,
    pt_cursor_pointer,
    pt_cursor_resize_h,
    pt_cursor_resize_v,
    pt_cursor_resize_nwse,
    pt_cursor_resize_nesw,
    pt_cursor_not_allowed
} pt_cursor;

typedef enum pt_csd_part {
    pt_csd_part_none = 0,
    pt_csd_part_titlebar,
    pt_csd_part_minimize,
    pt_csd_part_maximize,
    pt_csd_part_close,
    pt_csd_part_edge_top,
    pt_csd_part_edge_bottom,
    pt_csd_part_edge_left,
    pt_csd_part_edge_right,
    pt_csd_part_edge_top_left,
    pt_csd_part_edge_top_right,
    pt_csd_part_edge_bottom_left,
    pt_csd_part_edge_bottom_right
} pt_csd_part;

/* ---------- util ---------- */

pt_printf_like(1, 2) void pt_set_error(const char *fmt, ...);
pt_printf_like(1, 2) void pt_log_warn(const char *fmt, ...);
pt_printf_like(1, 2) void pt_log_debug(const char *fmt, ...);
void *pt_alloc(size_t size);
void *pt_alloc_zeroed(size_t count, size_t size);
void *pt_realloc_mem(void *ptr, size_t size);
char *pt_strdup(const char *text);
char *pt_strdup_len(const char *text, size_t len);
int64_t pt_now_ms(void);
double pt_now_seconds(void);
float pt_ease_towards(float current, float target, float rate, float dt);
bool pt_rect_contains(pt_rect rect, float x, float y);
bool pt_rect_intersects(pt_rect a, pt_rect b);
pt_rect pt_rect_intersect(pt_rect a, pt_rect b);
pt_color pt_color_blend(pt_color a, pt_color b, float t);
pt_color pt_color_lighten(pt_color color, float amount);
uint32_t pt_utf8_decode(const char *text, size_t len, size_t pos, size_t *next);
uint32_t pt_utf8_decode_prev(const char *text, size_t pos, size_t *prev);
size_t pt_utf8_next_boundary(const char *text, size_t len, size_t pos);
size_t pt_utf8_prev_boundary(const char *text, size_t len, size_t pos);
size_t pt_utf8_count(const char *text, size_t len);
bool pt_utf8_is_cjk(uint32_t codepoint);

/* ---------- font ---------- */

/* Paragraph base direction (declared early so the font direction helpers can
 * name it; the bidi adapter sits at the end of this header). `auto` runs UBA
 * rules P2-P3 (falling back to left-to-right when the paragraph has no strong
 * character); `ltr`/`rtl` pin the base level to 0/1. */
typedef enum pt_bidi_base {
    pt_bidi_base_auto = 0,
    pt_bidi_base_ltr,
    pt_bidi_base_rtl
} pt_bidi_base;

typedef struct pt_font pt_font;

typedef struct pt_glyph {
    uint32_t glyph_id;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance_x;
    uint8_t *coverage;
    bool used;
} pt_glyph;

typedef struct pt_glyph_cache {
    pt_glyph *slots;
    size_t capacity;
    size_t count;
} pt_glyph_cache;

typedef struct pt_shape_entry {
    uint64_t hash;
    /* Installed-fallback generation this entry was shaped under. A chain change
     * bumps the generation, so stale entries stop matching instead of being
     * freed while a caller still holds a pointer. */
    uint64_t generation;
    /* Direction signature of the shaped result (2.0). Zero is the legacy
     * auto/LTR fast path; nonzero keys pin a paragraph base or an explicit
     * per-run direction so RTL and LTR shaping of the same bytes never alias. */
    uint8_t direction_key;
    char *text;
    size_t len;
    struct pt_shaped_run *runs;
    unsigned int run_count;
    /* Composed maximum ascent/descent over the faces used, device pixels. */
    float ascent;
    float descent;
    struct pt_shape_entry *newer;
    struct pt_shape_entry *older;
} pt_shape_entry;

/* One HarfBuzz-script segment of a shaped run. `font` is the face the segment
 * was shaped with; `covered` is false when no installed face had the
 * characters, in which case `font` is the primary face and renders .notdef. */
typedef struct pt_shaped_run {
    size_t start;
    size_t end;
    pt_font *font;
    hb_script_t script;
    bool covered;
    /* Resolved embedding level and shaping direction (2.0). Legacy LTR entries
     * keep level 0 and HB_DIRECTION_INVALID, which preserves their previous
     * guess-the-direction shaping path. */
    uint8_t level;
    hb_direction_t direction;
    hb_buffer_t *buffer;
} pt_shaped_run;

/* Per-primary-face resolution cache: one slot per codepoint. Bounded at a fixed
 * capacity so a pathological string cannot grow it without limit; beyond the
 * load factor new codepoints are resolved without being cached. */
typedef struct pt_fallback_slot {
    uint32_t codepoint;
    pt_font *font;
    bool resolved;
    bool covered;
} pt_fallback_slot;

typedef struct pt_fallback_cache {
    pt_fallback_slot *slots;
    size_t capacity;
    size_t count;
    uint64_t generation;
} pt_fallback_cache;

typedef struct pt_shape_cache {
    pt_shape_entry *newest;
    pt_shape_entry *oldest;
    size_t count;
    size_t bytes;
} pt_shape_cache;

struct pt_font {
    FT_Face face;
    hb_font_t *hb_font;
    pt_glyph_cache glyphs;
    pt_shape_cache shapes;
    pt_fallback_cache fallbacks;
    char *family;
    int weight;
    int slant;
    float pixel_size;
    float ascent;
    float descent;
    float line_height;
    struct pt_font *next;
};

typedef struct pt_text_line {
    size_t start;
    size_t end;
    /* Resolved paragraph base level of this line (2.0). Wrapped lines inside
     * one UBA paragraph reuse the paragraph base; explicit '\n' paragraphs
     * resolve independently (rule P2/P3). Zero for the LTR fast path. */
    uint8_t base_level;
} pt_text_line;

/* Per-line logical byte offset <-> visual x map (2.0). A map covers one
 * rendered line, stores its clusters in logical order with both visual
 * extents, and keeps x relative to the line's start edge. */
typedef enum pt_text_bias {
    /* Caret affinity to the character starting at the position (downstream). */
    pt_text_bias_leading = 0,
    /* Caret affinity to the character ending at the position (upstream). */
    pt_text_bias_trailing
} pt_text_bias;

typedef struct pt_text_visual_cluster {
    size_t start;
    size_t end;
    /* Visual extents, x0 <= x1. For an RTL cluster `start_x` is the right
     * edge (logical start) and `end_x` the left edge (logical end). */
    float x0;
    float x1;
    float start_x;
    float end_x;
    uint8_t level;
} pt_text_visual_cluster;

typedef struct pt_text_visual_map {
    pt_text_visual_cluster *clusters;
    size_t count;
    size_t start;
    size_t end;
    float width;
    uint8_t base_level;
} pt_text_visual_map;

typedef struct pt_text_visual_segment {
    size_t start;
    size_t end;
    pt_font *font;
    hb_direction_t direction;
    uint8_t level;
} pt_text_visual_segment;

typedef struct pt_text_run {
    size_t start;
    size_t end;
    pt_font *font;
    pt_color color;
    uint32_t flags;
    /* Embedding level and shaping direction of this run (2.0). LTR fast-path
     * builds leave level 0 and HB_DIRECTION_INVALID. Bidi builds emit runs in
     * visual order, so `items` is no longer guaranteed to be in logical order;
     * measure still sums overlapping ranges, and drawing walks the array. */
    uint8_t level;
    hb_direction_t direction;
} pt_text_run;

typedef struct pt_text_runs {
    const char *text;
    size_t length;
    pt_text_run *items;
    size_t count;
    /* Allocated item slots; richer builds with per-level splits grow past the
     * span-boundary estimate. */
    size_t capacity;
    float scale;
    float ascent;
    float descent;
    float line_height;
    /* Resolved paragraph base level and whether the runs were reordered. */
    uint8_t base_level;
    bool bidi;
    /* Per-paragraph bases for bidi builds: content range (newline and a
     * preceding CR excluded) plus the resolved level. Width wrapping reuses
     * them for its per-line base. Empty for the LTR fast path. */
    pt_text_line *paragraphs;
    size_t paragraph_count;
} pt_text_runs;

bool pt_widget_supports_spans(const pt_widget *widget);
void pt_widget_spans_on_insert(pt_widget *widget, size_t position, size_t length);
void pt_widget_spans_on_delete(pt_widget *widget, size_t start, size_t end);
void pt_widget_spans_clamp(pt_widget *widget);
bool pt_text_runs_build(const pt_widget *widget, const pt_layout_ctx *ctx, pt_text_runs *out);
/* Resolved paragraph base for single-line text without running the full UBA:
 * explicit/auto widget text direction, then P2 first strong, then the resolved
 * UI direction. Shared by rich-text building and the paint fast path. */
pt_bidi_base pt_widget_text_base(const pt_widget *widget, const char *text, size_t len);
void pt_text_runs_free(pt_text_runs *runs);
float pt_text_runs_measure(const pt_text_runs *runs, size_t start, size_t end);
float pt_text_runs_natural_width(const pt_text_runs *runs);
int pt_text_runs_wrap(const pt_text_runs *runs, float max_width, pt_text_line **out_lines);
/* Per-line base for a byte position: the paragraph containing it, or the
 * layout-wide base level when the fast path skipped paragraph tracking. */
uint8_t pt_text_runs_base_at(const pt_text_runs *runs, size_t position);
void pt_text_runs_draw_line(pt_canvas *canvas, const pt_text_runs *runs, size_t start,
                            size_t end, float x, float baseline, float max_width,
                            pt_text_align align, pt_color color, bool ellipsis);
/* Same painter with an explicit per-line base: the UBA runs are recomputed for
 * the visible line range so rule L1 applies at the wrapped line end, and
 * ellipsis/alignment resolve against the line's own direction. */
void pt_text_runs_draw_line_base(pt_canvas *canvas, const pt_text_runs *runs, size_t start,
                                 size_t end, uint8_t base_level, float x, float baseline,
                                 float max_width, pt_text_align align, pt_color color,
                                 bool ellipsis);
size_t pt_text_runs_position_for_x(const pt_text_runs *runs, size_t start, size_t end,
                                   float local_x);

pt_font *pt_font_acquire(const char *family, int weight, pt_font_slant slant,
                         float pixel_size);
void pt_font_manager_shutdown(void);
float pt_font_measure(const pt_font *font, const char *text, size_t len);
/* True when the face itself has a glyph for the codepoint. Fallback resolution
 * is separate; this is the primary-face coverage test. */
bool pt_font_has_glyph(const pt_font *font, uint32_t codepoint);
/* Composed metrics for a UTF-8 run: the maximum ascent and descent over the
 * primary face and every fallback face the run resolves to. Device pixels. */
void pt_font_metrics_for_text(const pt_font *font, const char *text, size_t len,
                              float *ascent, float *descent);
float pt_font_line_height_for(const pt_font *font, const char *text, size_t len);
/* Script/face segmentation used by measurement and drawing. The returned array
 * is caller-owned and released with pt_font_runs_free; runs are contiguous and
 * cover the whole text. The legacy request emits them in logical order; the
 * bidi request emits them in visual order. Exposed for tests and diagnostics. */
typedef struct pt_font_run {
    size_t start;
    size_t end;
    pt_font *font;
    hb_script_t script;
    bool covered;
    /* Resolved embedding level and shaping direction (2.0). Legacy LTR runs
     * keep level 0 and HB_DIRECTION_INVALID. */
    uint8_t level;
    hb_direction_t direction;
    size_t visual_index;
} pt_font_run;
int pt_font_runs(const pt_font *font, const char *text, size_t len, pt_font_run **out_runs);
/* Bidi-aware segmentation: the paragraph is analysed from `base` and runs are
 * returned in visual order with their resolved level and explicit direction. */
int pt_font_runs_bidi(const pt_font *font, const char *text, size_t len, pt_bidi_base base,
                      pt_font_run **out_runs);
void pt_font_runs_free(pt_font_run *runs);
/* Directional measurement and painting (2.0).
 *
 * `_bidi` analyses the paragraph under `base` (auto runs P2/P3) and reorders.
 * The LTR fast path inside these functions calls the legacy implementations
 * unchanged whenever the resolved base is LTR and the text contains neither a
 * strong RTL character nor a bidi formatting control, so LTR pixels and widths
 * stay byte-identical.
 *
 * `_direction` shapes one already-segmented run with an explicit HarfBuzz
 * direction. HB_DIRECTION_INVALID selects the legacy path. */
float pt_font_measure_bidi(const pt_font *font, const char *text, size_t len,
                           pt_bidi_base base);
float pt_font_measure_direction(const pt_font *font, const char *text, size_t len,
                                hb_direction_t direction);
void pt_font_metrics_for_text_bidi(const pt_font *font, const char *text, size_t len,
                                   pt_bidi_base base, float *ascent, float *descent);
void pt_canvas_draw_text_line_bidi(pt_canvas *canvas, const pt_font *font, const char *text,
                                   size_t len, float x, float baseline, float max_width,
                                   pt_text_align align, pt_color color, bool ellipsis,
                                   pt_bidi_base base);
void pt_canvas_draw_text_line_direction(pt_canvas *canvas, const pt_font *font,
                                        const char *text, size_t len, float x, float baseline,
                                        float max_width, pt_text_align align, pt_color color,
                                        bool ellipsis, hb_direction_t direction);
/* Cache bounds used by the tests: entries currently held and faces in the
 * process font table. */
size_t pt_font_shape_cache_count(const pt_font *font);
size_t pt_font_fallback_cache_count(const pt_font *font);
size_t pt_font_manager_face_count(void);
int pt_font_wrap_lines(const pt_font *font, const char *text, size_t len, float max_width,
                       pt_text_line **out_lines);
/* Direction-aware wrapping: lines keep their logical byte ranges, carry the
 * resolved paragraph base, and are broken with bidi-aware widths. `base` auto
 * resolves each paragraph with P2/P3 and falls back to `rtl_default` for
 * neutral-only text; explicit ltr/rtl forces the base. */
int pt_font_wrap_lines_bidi(const pt_font *font, const char *text, size_t len, float max_width,
                            pt_bidi_base base, bool rtl_default,
                            pt_text_line **out_lines);
float pt_font_wrapped_height(const pt_font *font, const char *text, float max_width);
void pt_canvas_draw_text_line(pt_canvas *canvas, const pt_font *font, const char *text,
                              size_t len, float x, float baseline, float max_width,
                              pt_text_align align, pt_color color, bool ellipsis);
void pt_canvas_draw_text_wrapped(pt_canvas *canvas, const pt_font *font, const char *text,
                                 float x, float top, float max_width, float max_height,
                                 pt_text_align align, pt_color color);
void pt_canvas_draw_text_wrapped_bidi(pt_canvas *canvas, const pt_font *font, const char *text,
                                       float x, float top, float max_width, float max_height,
                                       pt_text_align align, pt_color color, pt_bidi_base base,
                                       bool rtl_default);
/* ---------- logical <-> visual map (2.0 wave 3) ---------- */

/* Builds a map over `text` from segments already in visual order (UBA runs
 * split by style/font boundaries). Segments must partition [0, len). */
bool pt_text_visual_map_build(const char *text, size_t len,
                              const pt_text_visual_segment *segments, size_t count,
                              uint8_t base_level, pt_text_visual_map **out);
/* Convenience builders: plain text resolves its own UBA runs under `base`;
 * rich runs take their span/style boundaries into account. */
bool pt_text_visual_map_build_bidi(const pt_font *font, const char *text, size_t len,
                                   pt_bidi_base base, pt_text_visual_map **out);
bool pt_text_visual_map_build_runs(const pt_text_runs *runs, size_t start, size_t end,
                                   pt_bidi_base base, pt_text_visual_map **out);
void pt_text_visual_map_free(pt_text_visual_map *map);
float pt_text_visual_map_width(const pt_text_visual_map *map);
/* x of the caret at `position`. `bias` resolves positions shared by two visual
 * runs (leading picks the character starting there, trailing the one ending
 * there). Positions inside a cluster snap to its start/end edge. */
float pt_text_visual_map_x_for_position(const pt_text_visual_map *map, size_t position,
                                        pt_text_bias bias);
/* Nearest logical position for a visual x; ties resolve with `bias`. */
size_t pt_text_visual_map_position_for_x(const pt_text_visual_map *map, float x,
                                         pt_text_bias bias);
/* Cluster / visual-run boundary navigation for editing (W4). These skip the
 * interior of a multi-byte cluster and X9-removed controls. */
bool pt_text_visual_map_is_boundary(const pt_text_visual_map *map, size_t position);
size_t pt_text_visual_map_next_boundary(const pt_text_visual_map *map, size_t position);
size_t pt_text_visual_map_prev_boundary(const pt_text_visual_map *map, size_t position);
/* Resolved base direction of the text for a widget (per paragraph when the
 * caller passes only that paragraph). */
bool pt_widget_explicit_text_base(const pt_widget *widget, pt_bidi_base *out);
bool pt_widget_rtl_default(const pt_widget *widget);
pt_font *pt_widget_font(const pt_widget *widget, float logical_size, pt_font_weight weight,
                        const pt_layout_ctx *ctx);
pt_font *pt_widget_font_styled(const pt_widget *widget, float logical_size,
                               pt_font_weight weight, pt_font_slant slant,
                               const pt_layout_ctx *ctx);

/* ---------- canvas ---------- */

struct pt_canvas {
    uint32_t *pixels;
    int width;
    int height;
    int stride;
    float scale;
    pt_rect clip;
    pt_rect clip_stack[pt_clip_stack_max];
    int clip_depth;
    uint32_t visit_count;
    bool owns_pixels;
};

/* Custom-widget paint context (see include/pt/custom.h). */
struct pt_paint_context {
    pt_canvas *canvas;
    pt_widget *widget;
};

void pt_canvas_init(pt_canvas *canvas, uint32_t *pixels, int width, int height, int stride,
                    float scale);
pt_canvas *pt_canvas_create(int width, int height, float scale);
void pt_canvas_destroy(pt_canvas *canvas);
void pt_canvas_clear(pt_canvas *canvas, pt_color color);
void pt_canvas_push_clip(pt_canvas *canvas, float x, float y, float w, float h);
void pt_canvas_pop_clip(pt_canvas *canvas);
void pt_canvas_fill_rect(pt_canvas *canvas, float x, float y, float w, float h, pt_color color);
void pt_canvas_fill_rounded_rect(pt_canvas *canvas, float x, float y, float w, float h,
                                 float radius, pt_color color);
void pt_canvas_fill_rounded_rect_gradient(pt_canvas *canvas, float x, float y, float w, float h,
                                          float radius, pt_color top, pt_color bottom);
void pt_canvas_stroke_rounded_rect(pt_canvas *canvas, float x, float y, float w, float h,
                                   float radius, float border_width, pt_color color);
void pt_canvas_fill_circle(pt_canvas *canvas, float cx, float cy, float radius, pt_color color);
void pt_canvas_draw_line(pt_canvas *canvas, float x0, float y0, float x1, float y1,
                         float width, pt_color color);
void pt_canvas_draw_shadow(pt_canvas *canvas, float x, float y, float w, float h, float radius,
                           float blur, pt_color color);
void pt_canvas_blend_mask(pt_canvas *canvas, int x, int y, const uint8_t *coverage, int width,
                          int height, int stride, pt_color color, float opacity);
/* Returns the pixel as an unpremultiplied pt_color (0xRRGGBBAA). */
uint32_t pt_canvas_get_pixel(const pt_canvas *canvas, int x, int y);
int pt_canvas_write_png(const pt_canvas *canvas, const char *path);
int pt_write_png_rgba(const char *path, int width, int height, const uint8_t *rgba);

/* Raster icon helpers (icon.c). The raster cache is keyed by device-pixel size
 * and bounded by the PT_ICON_CACHE_* limits; the returned buffer is borrowed and
 * valid until the next cache operation. The cache counters are exposed for the
 * boundary tests. */
const uint32_t *pt_icon_image_raster(pt_icon_image *image, int pixel_width, int pixel_height,
                                     int *out_stride);
void pt_icon_draw_canvas(pt_canvas *canvas, pt_icon_image *image, float x, float y, float width,
                         float height, pt_color tint);
size_t pt_icon_cache_entry_count(void);
size_t pt_icon_cache_pixel_count(void);
bool pt_icon_window_buffer_geometry(int source_width, int source_height, float scale,
                                    int *out_size, int *out_scale);

/* Bounded SVG icon integration (svg.c). Both functions parse a private copy of
 * the document through the vendored nanosvg parser, reject empty documents,
 * documents without a usable intrinsic size, documents above
 * PT_ICON_MAX_DIMENSION and inputs above the 16 MiB icon cap. The icon loader
 * inflates an SVGZ container before calling these, so both functions only ever
 * see plain SVG text. The rasteriser
 * preserves the intrinsic aspect ratio, centres the artwork in the requested
 * device-pixel box and returns caller-owned premultiplied 0xAARRGGBB pixels
 * (release with pt_free); NULL is returned with the error set on failure. */
bool pt_svg_measure(const void *data, size_t size, int *out_width, int *out_height);
pt_color *pt_svg_rasterize(const void *data, size_t size, int width, int height);

/* In-memory PNG codec shared by snapshots and clipboard images. The encoder
 * takes premultiplied 0xAARRGGBB pixels (pt_window_paint_pixels layout) and
 * produces a straight-alpha RGBA PNG. The decoder returns caller-owned
 * premultiplied 0xAARRGGBB pixels (release with pt_free), rejects corrupt,
 * oversized and decompression-bomb inputs, and leaves the out parameters at
 * zero on failure. */
uint8_t *pt_png_encode_argb_premultiplied(const uint32_t *pixels, int width,
                                          int height, size_t *out_size);
pt_color *pt_png_decode_argb_premultiplied(const uint8_t *data, size_t size,
                                           int *out_width, int *out_height);

/* ---------- data views and navigation ---------- */

/* A rubber band dragged across a data view. Coordinates are window space, the
 * same frame the row widgets report their bounds in. Pressed follows the button
 * down; dragging turns on only after the pointer leaves the click threshold, so
 * a press that never travels keeps the plain click selection it started with.
 * Every row view embeds one, which is why the geometry lives here. */
typedef struct pt_marquee {
    bool pressed;
    bool dragging;
    float x0;
    float y0;
    float x1;
    float y1;
} pt_marquee;

void pt_marquee_begin(pt_marquee *marquee, float x, float y);
/* Returns whether the band is dragging, which is when a view applies it. */
bool pt_marquee_move(pt_marquee *marquee, float x, float y);
/* Returns whether a drag was in progress, so the caller can swallow the click
 * that a release would otherwise report. */
bool pt_marquee_end(pt_marquee *marquee);
pt_rect pt_marquee_rect(const pt_marquee *marquee);
/* True only while dragging: a resting band must not claim any row. */
bool pt_marquee_hit(const pt_marquee *marquee, pt_rect area);

typedef struct pt_table_state {
    uint32_t column_count;
    float *column_widths;
    char **column_titles;
    /* Stable application keys for layout persistence; never NULL when a
     * column exists and empty for columns matched by position. */
    char **column_keys;
    pt_text_align *column_aligns;
    bool *column_resizable;
    bool *column_hidden;
    float header_height;
    uint32_t sort_columns[PT_TABLE_MAX_SORT_COLUMNS];
    pt_sort_direction sort_directions[PT_TABLE_MAX_SORT_COLUMNS];
    uint32_t sort_count;
    int32_t resizing_column;
    float resize_start_x;
    float resize_start_width;
    bool rebind;
    pt_widget *header;
    pt_table_bind_cell_callback on_bind_cell;
    void *bind_data;
    pt_table_sort_callback on_sort;
    void *sort_data;
    pt_table_sort_columns_callback on_sort_columns;
    void *sort_columns_data;

    bool editable;
    int32_t editing_row;
    int32_t editing_column;
    pt_widget *editor;
    pt_table_cell_edited_callback on_cell_edited;
    void *cell_edited_data;

    uint8_t *selection;
    uint32_t selection_capacity;
    uint32_t selection_count;
    uint32_t selection_anchor;
    pt_marquee marquee;
    /* The press that started the current gesture decided its selection mode, so
     * the release reports it instead of re-reading modifiers that may have
     * changed mid-drag. */
    bool shift_select;
    bool toggle_select;

    bool columns_reorderable;
    int32_t reorder_column;
    int32_t reorder_target;
    bool reorder_active;
    pt_table_columns_reordered_callback on_columns_reordered;
    void *columns_reordered_data;

    pt_table_row_drag_callback on_row_drag;
    void *row_drag_data;
    int32_t dragged_row;
    bool row_drag_active;
    float row_drag_start_y;

    bool sort_pending;
    uint32_t sort_pending_column;
    bool sort_pending_additive;
} pt_table_state;

/* List selection lives beside the base widget fields, which keep the primary
 * row, so only the additive set and the rubber band need per-widget storage. */
typedef struct pt_list_state {
    uint8_t *selection;
    uint32_t selection_capacity;
    uint32_t selection_count;
    uint32_t selection_anchor;
    pt_marquee marquee;
    bool shift_select;
    bool toggle_select;
} pt_list_state;

struct pt_tree_node {
    pt_widget *tree;
    struct pt_tree_node *parent;
    struct pt_tree_node *first_child;
    struct pt_tree_node *last_child;
    struct pt_tree_node *next_sibling;
    char *text;
    void *data;
    uint32_t depth;
    bool expanded;
    bool selected;
};

typedef struct pt_tree_state {
    pt_tree_node *first_root;
    pt_tree_node *last_root;
    pt_tree_node **visible;
    uint32_t visible_count;
    uint32_t visible_capacity;
    bool visible_dirty;
    pt_tree_node *selected;
    pt_tree_node *selection_anchor;
    uint32_t selection_count;
    pt_marquee marquee;
    bool shift_select;
    bool toggle_select;
    float indent;
    pt_tree_node_callback on_select;
    void *select_data;
    pt_tree_node_callback on_activate;
    void *activate_data;
    bool renamable;
    pt_tree_node *rename_node;
    pt_widget *rename_editor;
    pt_tree_rename_callback on_rename;
    void *rename_data;
} pt_tree_state;

typedef struct pt_tabs_state {
    char **titles;
    uint32_t count;
    uint32_t capacity;
    uint32_t active;
    float scroll_x;
    float content_width;
    float viewport_width;
    float strip_height;
    float *tab_x;
    float *tab_width;
    uint32_t tab_capacity;
    float indicator_x;
    float indicator_width;
    float target_x;
    float target_width;
    bool indicator_ready;
    bool overflow;
    pt_index_callback on_change;
    void *change_data;
} pt_tabs_state;

typedef struct pt_sidebar_state {
    uint32_t active;
    bool compact;
    char *header;
    float item_height;
    pt_index_callback on_select;
    void *select_data;
} pt_sidebar_state;

typedef struct pt_combobox_item {
    char *text;
    void *data;
    bool disabled;
} pt_combobox_item;

/* Combo box field and its popup. The popup overlay lives in the state so the
 * generic dismiss paths can notify the owner. The panel and its rows are
 * rebuilt whenever the items or the filter change while the popup is open, so
 * the row scroll is read back from the overlay instead of cached here. */
typedef struct pt_combobox_state {
    pt_combobox_item *items;
    uint32_t item_count;
    uint32_t item_capacity;
    uint32_t *visible;
    uint32_t visible_count;
    uint32_t visible_capacity;
    uint32_t highlighted;
    uint32_t selected;
    pt_combobox_mode mode;
    float popup_max_height;
    char *filter;
    pt_widget *field;
    pt_overlay *popup;
    /* True while the widget writes the field text itself, so the entry change
     * callback can tell a programmatic selection from a user keystroke. */
    bool applying;
    pt_index_callback on_change;
    void *change_data;
    pt_combobox_text_callback on_text_change;
    void *text_change_data;
    /* Non-NULL when this combo box is one of the 1.2 pickers. Every keyboard,
     * pointer, layout, measurement, popup and teardown hook then forwards to
     * src/picker.c; the combo-box item model stays unused. */
    struct pt_picker *picker;
} pt_combobox_state;

/* ---------- pickers (1.2) ---------- */

typedef enum pt_picker_kind {
    pt_picker_kind_select = 0,
    pt_picker_kind_date,
    pt_picker_kind_number,
    pt_picker_kind_color
} pt_picker_kind;

typedef struct pt_picker_option {
    char *text;
    void *data;
} pt_picker_option;

/* One control's whole private state. The picker is owned by the combo-box
 * extra of its host widget, so a destroy order that frees the extra after the
 * host's children is already anticipated: nothing below is used after
 * pt_picker_free starts, and the popup content is a separate tree. */
typedef struct pt_picker {
    pt_picker_kind kind;
    pt_widget *host;
    pt_widget *field;
    pt_popup *popup;
    pt_widget *content;
    /* The window that owns the popup, and its app, kept so teardown can tell a
     * live window from one that already released the popup in overlays_free. */
    pt_window *window;
    pt_app *app;
    uint32_t highlighted;
    /* True while the widget writes the field text itself. */
    bool applying;

    /* Searchable select model. */
    pt_picker_option *options;
    uint32_t option_count;
    uint32_t option_capacity;
    uint32_t *visible;
    uint32_t visible_count;
    uint32_t visible_capacity;
    uint32_t selected;
    char *query;
    uint32_t max_rows;

    pt_index_callback on_select;
    void *select_data;

    /* Date picker. */
    pt_date date;
    pt_date focus_date;
    int32_t view_year;
    uint32_t view_month;
    pt_date_format format;
    /* Committed display text; also the value Escape restores. */
    char display[64];
    pt_date_callback on_date;
    void *date_data;

    /* Number picker. */
    double min;
    double max;
    double step;
    double value;
    bool has_value;
    char number_display[64];
    pt_widget *up;
    pt_widget *down;
    pt_number_callback on_number;
    void *number_data;

    /* Colour picker. */
    pt_color color;
    char hex[16];
    pt_color *presets;
    uint32_t preset_count;
    uint32_t preset_capacity;
    pt_color_callback on_color;
    void *color_data;
} pt_picker;

/* The picker behind a widget, or NULL for anything else (including a plain
 * combo box). All helpers below validate the widget for their caller. */
pt_picker *pt_picker_of(const pt_widget *widget);
pt_picker *pt_picker_of_kind(const pt_widget *widget, pt_picker_kind kind);
bool pt_picker_key(pt_widget *host, uint32_t keysym, uint32_t modifiers, const char *utf8);
void pt_picker_pointer_down(pt_widget *host, float x, float y);
void pt_picker_layout(pt_widget *host, const pt_layout_ctx *ctx, pt_rect inner);
void pt_picker_preferred(pt_widget *host, const pt_layout_ctx *ctx, float *width,
                         float *height);
void pt_picker_open(pt_widget *host);
void pt_picker_close(pt_widget *host);
bool pt_picker_is_open(const pt_widget *host);
void pt_picker_choose(pt_widget *host, uint32_t position);
void pt_picker_free(pt_widget *host);
/* Closes the popup and returns true when Tab belongs to the picker's normal
 * focus order rather than to the popup's content order. Used by overlay.c. */
bool pt_picker_popup_tab(pt_widget *host, bool shift);
/* Accessibility hooks used by a11y.c for the host widget. */
bool pt_picker_a11y_value(const pt_widget *widget, float *value);
bool pt_picker_a11y_has_popup(const pt_widget *widget);
/* True when the picker currently exposes a Value interface (a number picker
 * only once it has a value). Used by the AT-SPI bridge's interface gating. */
bool pt_picker_a11y_has_value(const pt_widget *widget);

/* ---------- widgets ---------- */

/* text-input-v3 v2 preedit style hints. The values match the protocol enum and
 * are presentation only: they never change the composition buffer, the commit
 * text or the widget's text. Unknown values are accepted (stored) but have no
 * visual rule, so a future protocol value degrades to the plain preedit. */
typedef enum pt_ime_hint_kind {
    pt_ime_hint_whole = 1,
    pt_ime_hint_selection = 2,
    pt_ime_hint_prediction = 3,
    pt_ime_hint_prefix = 4,
    pt_ime_hint_suffix = 5,
    pt_ime_hint_spelling_error = 6,
    pt_ime_hint_compose_error = 7,
} pt_ime_hint_kind;

typedef struct pt_ime_hint {
    uint32_t start;
    uint32_t end;
    uint32_t hint;
} pt_ime_hint;

#define pt_ime_hint_max 256u

typedef struct pt_widget {
    pt_widget *parent;
    pt_widget *first_child;
    pt_widget *last_child;
    pt_widget *next_sibling;
    struct pt_window *window;

    pt_widget_kind kind;
    char *text;
    char *placeholder;
    char *preedit;
    pt_text_span *spans;
    uint32_t span_count;
    int32_t preedit_cursor_begin;
    int32_t preedit_cursor_end;
    pt_ime_hint *preedit_hints;
    uint32_t preedit_hint_count;
    bool ime_composing;
    bool ime_history_pushed;
    pt_input_purpose input_purpose;
    char **segment_labels;
    uint32_t segment_count;
    uint32_t segment_selected;
    struct pt_edit_history *edit_history;

    uint32_t grid_columns;
    uint32_t grid_span;
    float row_gap;
    uint32_t row_count;
    float row_height;
    uint32_t list_selected;
    bool grow_to_content;
    bool list_bound;
    uint32_t list_index;
    bool row_selected;
    float caret_goal_x;
    /* Visual affinity of the text caret (2.0 wave 4). A logical byte offset
     * can sit at two visual positions when two reordered bidi runs meet; 0
     * attaches it to the character starting at the offset, 1 to the character
     * ending there. Logical edits reset it to leading. */
    uint8_t caret_affinity;

    uint64_t selected_key;
    bool selected_key_valid;
    pt_row_key_callback row_key_callback;
    void *row_key_data;
    pt_row_text_callback row_text_callback;
    void *row_text_data;
    char typeahead[64];
    size_t typeahead_length;
    int64_t typeahead_at;

    void *extra;
    pt_icon icon;
    float icon_size;
    pt_icon_image *icon_image;
    pt_color icon_tint;
    uint32_t column_index;
    struct pt_tree_node *tree_node;
    pt_widget *inline_editor;
    struct pt_tree_node *rename_node;
    pt_widget *editor_owner;
    /* Row widgets inside an owner-built popup point back at the control that
     * opened it, so a click can be resolved without a lookup table. */
    pt_widget *popup_owner;
    /* Marks the option row that holds the combo box's committed value, which is
     * drawn with a check glyph while row_selected tracks the keyboard
     * highlight. */
    bool row_current;

    pt_color background;
    pt_color foreground;
    pt_color border_color;
    float border_width;
    float radius;
    float font_size;
    int font_weight;
    pt_text_align text_align;
    pt_button_variant button_variant;
    /* Explicit direction properties (since 2.0). Zero is inherit/auto. */
    uint8_t ui_direction;
    uint8_t text_direction;
    /* Explicit caret movement (since 2.1). Zero is inherit/visual. */
    uint8_t caret_movement;
    /* Set on the column containers a table or tree owns. In RTL their children
     * keep logical order (column 0 stays first in the child list) and the
     * container mirrors only its origin, so the data views place each column
     * themselves. Set by pt_widget_new for pt_widget_table_header and
     * pt_widget_table_row; never touches the public header. */
    bool rtl_logical_order;

    bool has_background;
    bool has_foreground;
    bool has_border_color;
    bool has_border_width;
    bool has_radius;
    bool has_font_size;
    bool has_font_weight;

    float requested_width;
    float requested_height;
    float min_width;
    float min_height;
    float max_width;
    float max_height;
    float margin_top;
    float margin_right;
    float margin_bottom;
    float margin_left;
    /* The four fields below are always the physical values layout and paint
     * read. A logical call (pt_widget_set_padding_logical) also stores its
     * start/end pair and sets padding_logical; the layout prepass re-derives
     * padding_left/padding_right from them on every layout and measure so a
     * direction change is honoured. padding_logical is false in every widget
     * that only ever used the physical setter, so the prepass is a no-op. */
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
    float padding_start;
    float padding_end;
    bool padding_logical;
    float grow;
    pt_align align_self;

    bool visible;
    bool enabled;
    bool focusable;
    bool wrap;
    bool hovered;
    bool pressed;
    bool focused;
    bool focus_visible;
    bool pressed_inside;
    bool busy;
    bool invalid;

    float hover_mix;
    float press_mix;
    float checked_mix;
    float phase;

    float x;
    float y;
    float width;
    float height;
    pt_rect clip;

    pt_direction direction;
    float gap;
    pt_align align_items;
    pt_justify justify;

    bool checked;
    bool indeterminate;
    float value;
    bool sliding;
    bool chip_removable;
    bool chip_selected;
    bool chip_remove_pressed;
    float segment_x;
    float segment_width;
    float segment_target_x;
    float segment_target_width;
    bool segment_indicator_ready;

    float scroll_y;
    float scroll_content_height;

    size_t cursor;
    size_t selection_anchor;
    bool selecting;
    float scroll_x;
    bool password;

    bool menu_item;
    char *shortcut;
    char *tooltip;
    uint32_t tooltip_delay;
    pt_a11y_role a11y_role;
    uint32_t a11y_id;
    char *a11y_name;
    char *a11y_description;

    pt_drop_target drop_target;
    bool drop_hover;
    bool drag_enabled;

    pt_click_callback on_click;
    void *on_click_data;
    pt_text_callback on_text;
    void *on_text_data;
    pt_value_callback on_value;
    void *on_value_data;
    pt_click_callback on_submit;
    void *on_submit_data;
    pt_click_callback on_remove;
    void *on_remove_data;
    pt_index_callback on_index;
    void *on_index_data;
    pt_list_bind_callback on_bind_row;
    void *on_bind_row_data;
    pt_index_callback on_select;
    void *on_select_data;
    pt_index_callback on_activate;
    void *on_activate_data;

    pt_paint_callback paint_callback;
    void *paint_callback_data;
} pt_widget;

static inline bool pt_widget_kind_is_text_input(pt_widget_kind kind) {
    return kind == pt_widget_entry || kind == pt_widget_text_area;
}

typedef struct pt_text_layout {
    pt_font *font;
    float scale;
    float line_height;
    /* Composed ascent over all fallback faces in the text, in logical pixels. */
    float ascent;
    char *display;
    size_t display_len;
    size_t caret;
    pt_text_line *lines;
    int line_count;
    bool has_runs;
    pt_text_runs runs;
    /* One map per line; NULL for an empty line or when the fast path had no
     * text to map. Released with the layout. */
    pt_text_visual_map **maps;
} pt_text_layout;

bool pt_text_layout_build(const pt_widget *area, const pt_layout_ctx *ctx, float width,
                          pt_text_layout *out);
void pt_text_layout_free(pt_text_layout *layout);
float pt_text_area_measure_height(const pt_widget *area, const pt_layout_ctx *ctx, float width);
float pt_text_area_content_width(const pt_widget *area);
void pt_text_area_ensure_caret_visible(pt_widget *area, const pt_layout_ctx *ctx);
void pt_text_area_caret_box(const pt_widget *area, const pt_layout_ctx *ctx, int *x, int *y,
                            int *w, int *h);
size_t pt_text_area_position_at(const pt_widget *area, const pt_layout_ctx *ctx, float x,
                                float y);
/* Click landing with the caret affinity that keeps the caret on the clicked
 * line when the hit lands on a shared soft-wrap boundary. */
size_t pt_text_area_position_at_bias(const pt_widget *area, const pt_layout_ctx *ctx, float x,
                                    float y, pt_text_bias *bias);
int pt_text_layout_line_for_position(const pt_text_layout *layout, size_t position);
/* Line assignment with an explicit caret affinity: at a soft-wrap boundary the
 * previous line's end and the next line's start are the same byte, so leading
 * affinity belongs to the next line and trailing to the previous one. A hard
 * newline keeps its own byte and is unaffected. */
int pt_text_layout_line_for_position_bias(const pt_text_layout *layout, size_t position,
                                          pt_text_bias bias);
/* The preferred affinity when it renders on `line`, else the other one when
 * that does, else `preferred`. Used by clicks and edge keys that already know
 * which line they act on. */
pt_text_bias pt_text_layout_bias_for_line(const pt_text_layout *layout, int line,
                                          size_t position, pt_text_bias preferred);
/* Resolved embedding level of the cluster a caret at `position` attaches to
 * (leading affinity), used for preedit direction and caret composition. */
uint8_t pt_text_layout_level_for_position(const pt_text_layout *layout, int line,
                                          size_t position);
float pt_text_layout_x_for_position(const pt_text_layout *layout, int line, size_t position);
/* Same lookup with an explicit caret affinity: at a reordered bidi run
 * boundary the same byte offset has a leading and a trailing visual x. */
float pt_text_layout_x_for_position_bias(const pt_text_layout *layout, int line,
                                         size_t position, pt_text_bias bias);
size_t pt_text_layout_position_for_x(const pt_text_layout *layout, int line, float local_x);

bool pt_widget_text_selection_get(const pt_widget *widget, uint32_t *start, uint32_t *end);
bool pt_widget_text_selection_set(pt_widget *widget, uint32_t start, uint32_t end);
void pt_widget_text_select_all(pt_widget *widget);
bool pt_widget_text_undo(pt_widget *widget);
bool pt_widget_text_redo(pt_widget *widget);

pt_widget *pt_widget_new(pt_widget *parent, pt_widget_kind kind);
void pt_widget_append(pt_widget *parent, pt_widget *child);
pt_window *pt_widget_window(const pt_widget *widget);
pt_app *pt_widget_app(const pt_widget *widget);

/* ---------- direction properties (since 2.0) ---------- */

/* Resolves the explicit values through widget -> nearest ancestor -> window ->
 * application -> LTR. Text auto resolution runs UBA P2/P3 through the vendored
 * engine (first paragraph only) and falls back to the resolved UI direction
 * when the paragraph is empty or has no strong character. Both are side-effect
 * free and never look at layout. */
pt_ui_direction pt_widget_resolved_ui_direction(const pt_widget *widget);
/* Resolves the caret movement policy: widget -> nearest explicit ancestor ->
 * window -> application -> visual. */
pt_caret_movement pt_widget_resolved_caret_movement(const pt_widget *widget);
/* Walks the subtree and re-resolves the physical left/right padding of every
 * widget whose padding was set logically (padding_logical), from the resolved UI
 * direction. Runs at the entry of layout and measure so both see the same
 * geometry; widgets that never called the logical setter are untouched, so the
 * pass allocates nothing and writes nothing for a pure physical tree. */
void pt_widget_resolve_logical_padding(pt_widget *root);
pt_text_direction pt_widget_resolved_text_direction(const pt_widget *widget,
                                                    const char *text, size_t length);
/* Liveness check usable after a user callback that may have destroyed widget:
 * it scans the window's live trees (root and overlay panels) instead of
 * dereferencing widget. A NULL window means "not attached". */
bool pt_widget_attached(const pt_window *window, const pt_widget *widget);
pt_layout_ctx pt_layout_context_for(const pt_widget *widget);
float pt_widget_font_size(const pt_widget *widget);
int pt_widget_font_weight(const pt_widget *widget);
pt_font *pt_widget_font_for(const pt_widget *widget, const pt_layout_ctx *ctx);
void pt_widget_mark_dirty(pt_widget *widget, bool layout_changed);
void pt_window_invalidate_layout(pt_window *window, pt_widget *subtree);
void pt_widget_apply_defaults(pt_widget *widget);
void pt_widget_free_tree(pt_widget *widget);
void pt_widget_layout(pt_widget *root, float x, float y, float width, float height,
                      const pt_layout_ctx *ctx);
void pt_widget_paint(pt_widget *widget, pt_canvas *canvas, const pt_layout_ctx *ctx);
bool pt_widget_update_animations(pt_widget *widget, float dt);
pt_widget *pt_widget_hit_test(pt_widget *root, float x, float y);
void pt_widget_focus_first(pt_window *window);
bool pt_widget_focus_next(pt_window *window, bool backwards);
bool pt_widget_activate(pt_widget *widget);
bool pt_widget_key(pt_widget *widget, uint32_t keysym, uint32_t modifiers,
                   const char *utf8);
bool pt_widget_text_input(pt_widget *widget, const char *text);
bool pt_widget_preedit(pt_widget *widget, const char *text);
bool pt_widget_preedit_ex(pt_widget *widget, const char *text, int32_t cursor_begin,
                          int32_t cursor_end);
/* Same composition update plus presentation-only hints. Offsets are UTF-8 byte
 * offsets into `text` (the unsanitized preedit); they are clamped, snapped to
 * codepoint boundaries and mapped onto the sanitized buffer. A drop of a
 * zero-length or fully-out-of-range range is not an error. */
bool pt_widget_preedit_hints_ex(pt_widget *widget, const char *text, int32_t cursor_begin,
                                int32_t cursor_end, const pt_ime_hint *hints,
                                uint32_t hint_count);
bool pt_widget_ime_apply(pt_widget *widget, const char *commit, uint32_t delete_before,
                         uint32_t delete_after, bool finalize);
bool pt_widget_ime_surrounding_text(const pt_widget *widget, char **text, uint32_t *cursor,
                                    uint32_t *anchor);
bool pt_widget_ime_has_state(const pt_widget *widget);
size_t pt_widget_preedit_caret(const pt_widget *widget);
void pt_widget_ime_rect(const pt_widget *widget, int *x, int *y, int *w, int *h);
void pt_widget_set_focus_visual(pt_widget *widget, bool focused);
void pt_widget_clear_focus_visual(pt_widget *widget);
void pt_widget_pointer_enter(pt_widget *widget, float x, float y);
void pt_widget_pointer_leave(pt_widget *widget);
void pt_widget_pointer_down(pt_widget *widget, float x, float y);
void pt_widget_pointer_up(pt_widget *widget, bool inside);
void pt_widget_pointer_drag(pt_widget *widget, float x, float y);
void pt_widget_pointer_multi_click(pt_widget *widget, float x, float y, uint32_t click_count);
pt_widget *pt_widget_interaction_target(pt_widget *widget);
void pt_widget_wheel(pt_widget *widget, float delta);
/* Horizontal wheel dispatch (a horizontal wl_pointer axis only). Tables scroll
 * their columns; everything else ignores the axis. Returns true when a table
 * in the ancestor chain actually moved. */
bool pt_widget_wheel_horizontal(pt_widget *widget, float delta);
bool pt_widget_scroll_pixels(pt_widget *widget, float pixels);
char *pt_entry_selection_text_at(pt_widget *entry, float x, float y);
pt_cursor pt_widget_cursor(const pt_widget *widget);
pt_widget *pt_widget_focusable_ancestor(pt_widget *widget);
uint32_t pt_a11y_alloc_id(void);
void pt_widget_entry_ensure_caret_visible(pt_widget *entry, float available_width,
                                          pt_font *font, float scale);
bool pt_widget_entry_changed(pt_widget *entry);
void pt_widget_invalidate_caret_rect(pt_window *window);
void pt_widget_measure(pt_widget *widget, const pt_layout_ctx *ctx, float available_width,
                       float available_height, float *out_width, float *out_height);
void pt_widget_layout_child(pt_widget *widget, float x, float y, float width, float height,
                            const pt_layout_ctx *ctx);
void pt_widget_measure_child(pt_widget *widget, const pt_layout_ctx *ctx, float avail_width,
                             float avail_height, float *out_width, float *out_height);
bool pt_widget_is_descendant(const pt_widget *widget, const pt_widget *root);
void pt_widget_extra_free(pt_widget *widget);
pt_cursor pt_widget_cursor_at(const pt_widget *widget, float x, float y);
void pt_widget_pointer_hover(pt_widget *widget, float x, float y);

/* ---------- data views and navigation ---------- */

void pt_table_layout(pt_widget *table, const pt_layout_ctx *ctx, pt_rect inner);
void pt_table_preferred(pt_widget *table, const pt_layout_ctx *ctx, float *width,
                        float *height);
void pt_table_select(pt_widget *table, uint32_t row, bool notify);
void pt_table_reveal_row(pt_widget *table, uint32_t row);
void pt_table_reveal_column(pt_widget *table, uint32_t column);
bool pt_table_key(pt_widget *table, uint32_t keysym, uint32_t modifiers);
uint64_t pt_row_key_at(const pt_widget *view, uint32_t row);
uint32_t pt_row_find_key(const pt_widget *view, uint64_t key);
void pt_data_view_sync_selected_key(pt_widget *view);
bool pt_data_view_typeahead(pt_widget *view, const char *utf8);
void pt_table_sort_changed(pt_widget *table, bool additive, uint32_t column);
void pt_table_header_down(pt_widget *table, pt_widget *cell, float x);
void pt_table_pointer_drag(pt_widget *table, float x);
void pt_table_pointer_up(pt_widget *table);
pt_cursor pt_table_cursor(const pt_widget *table, float x, float y);
void pt_table_free_extra(pt_widget *table);
bool pt_table_activate_selected(pt_widget *table);
pt_widget *pt_table_row_cell(pt_widget *table, uint32_t row_index, uint32_t column);
bool pt_table_scroll(pt_widget *table, float pixels);
bool pt_table_scroll_horizontal(pt_widget *table, float pixels);
void pt_table_row_down(pt_widget *table, pt_widget *row, float x, float y);
void pt_table_row_drag(pt_widget *table, float x, float y);
bool pt_table_row_up(pt_widget *table);
/* A press on the empty viewport starts the same band as a press on a row. */
void pt_table_view_down(pt_widget *table, float x, float y);
void pt_table_view_drag(pt_widget *table, float x, float y);
bool pt_table_view_up(pt_widget *table);

void pt_tree_layout(pt_widget *tree, const pt_layout_ctx *ctx, pt_rect inner);
void pt_tree_preferred(pt_widget *tree, const pt_layout_ctx *ctx, float *width, float *height);
void pt_tree_rebuild(pt_widget *tree);
void pt_tree_select(pt_widget *tree, pt_tree_node *node, bool notify);
void pt_tree_select_index(pt_widget *tree, uint32_t index, bool notify);
void pt_tree_reveal(pt_widget *tree, pt_tree_node *node);
uint32_t pt_tree_index_of(const pt_widget *tree, const pt_tree_node *node);
bool pt_tree_key(pt_widget *tree, uint32_t keysym, uint32_t modifiers);
/* A press on the chevron column folds the node; anywhere else it starts the
 * same click and rubber-band gesture the list and table use. */
void pt_tree_row_down(pt_widget *tree, pt_widget *row, float x, float y);
void pt_tree_row_drag(pt_widget *tree, float x, float y);
bool pt_tree_row_up(pt_widget *tree);
/* A press on the empty viewport starts the same band as a press on a row. */
void pt_tree_view_down(pt_widget *tree, float x, float y);
void pt_tree_view_drag(pt_widget *tree, float x, float y);
bool pt_tree_view_up(pt_widget *tree);
void pt_tree_free_extra(pt_widget *tree);
bool pt_tree_activate_selected(pt_widget *tree);
void pt_inline_editor_finish(pt_widget *editor, bool cancel);

void pt_tabs_layout(pt_widget *tabs, const pt_layout_ctx *ctx, pt_rect inner);
void pt_tabs_preferred(pt_widget *tabs, const pt_layout_ctx *ctx, float *width, float *height);
void pt_tabs_refresh(pt_widget *tabs);
void pt_tabs_select(pt_widget *tabs, uint32_t index, bool notify);
bool pt_tabs_key(pt_widget *tabs, uint32_t keysym);
void pt_tabs_pointer_down(pt_widget *tabs, float x, float y);
bool pt_tabs_update_animation(pt_widget *tabs, float dt);
uint32_t pt_tabs_page_count(const pt_widget *tabs);
pt_widget *pt_tabs_page_at(const pt_widget *tabs, uint32_t index);
void pt_tabs_sync_pages(pt_widget *tabs);
void pt_tabs_forget_page(pt_widget *tabs, pt_widget *page);
void pt_tabs_free_extra(pt_widget *tabs);

void pt_sidebar_layout(pt_widget *sidebar, const pt_layout_ctx *ctx, pt_rect inner);
void pt_sidebar_preferred(pt_widget *sidebar, const pt_layout_ctx *ctx, float *width,
                          float *height);
void pt_sidebar_select(pt_widget *sidebar, uint32_t index, bool notify);
bool pt_sidebar_key(pt_widget *sidebar, uint32_t keysym);
void pt_sidebar_free_extra(pt_widget *sidebar);

void pt_list_free_extra(pt_widget *list);
uint32_t pt_list_selection_count(const pt_widget *list);
bool pt_list_row_is_selected(const pt_widget *list, uint32_t row);
void pt_list_select_index(pt_widget *list, uint32_t index, bool notify);
void pt_list_row_down(pt_widget *list, pt_widget *row, float x, float y);
void pt_list_row_drag(pt_widget *list, float x, float y);
bool pt_list_row_up(pt_widget *list);
/* A press on the empty viewport starts the same band as a press on a row. */
void pt_list_view_down(pt_widget *list, float x, float y);
void pt_list_view_drag(pt_widget *list, float x, float y);
bool pt_list_view_up(pt_widget *list);

void pt_combobox_layout(pt_widget *combobox, const pt_layout_ctx *ctx, pt_rect inner);
/* Horizontal space the field reserves for its chevron. Measurement, layout and
 * painting share it so the label never slides under the glyph. */
#define pt_combobox_chevron_reserve 26.0f
void pt_combobox_preferred(pt_widget *combobox, const pt_layout_ctx *ctx, float *width,
                           float *height);
void pt_combobox_select(pt_widget *combobox, uint32_t index, bool notify);
void pt_combobox_open_popup(pt_widget *combobox);
void pt_combobox_close_popup(pt_widget *combobox);
void pt_combobox_popup_layout(pt_widget *combobox, pt_overlay *overlay,
                              const pt_layout_ctx *ctx);
bool pt_combobox_key(pt_widget *combobox, uint32_t keysym, uint32_t modifiers,
                     const char *utf8);
void pt_combobox_pointer_down(pt_widget *combobox, float x, float y);
/* Chooses the option at a visible position; option rows report that position in
 * list_index and point back at the combo box through popup_owner. */
void pt_combobox_choose_option(pt_widget *combobox, uint32_t position);
/* The embedded editor of a filterable box, or NULL in select-only mode. */
pt_widget *pt_combobox_field_widget(const pt_widget *combobox);
/* True when a press at window coordinates lands on the filterable editor, which
 * keeps the popup open so the click can reposition the caret. */
bool pt_combobox_field_hit(pt_widget *combobox, float x, float y);
void pt_combobox_free_extra(pt_widget *combobox);

void pt_paint_icon_glyph(pt_canvas *canvas, pt_icon icon, float cx, float cy, float size,
                         pt_color color);

/* ---------- accessibility ---------- */

void pt_widget_a11y_emit(pt_widget *widget, pt_a11y_event_type type);

/* ---------- accessibility bridge ---------- */

bool pt_a11y_bridge_start(pt_app *app);
void pt_a11y_bridge_stop(pt_app *app);
int pt_a11y_bridge_fd(const pt_app *app);
void pt_a11y_bridge_dispatch(pt_app *app);
void pt_a11y_bridge_emit(pt_widget *widget, pt_a11y_event_type type);
void pt_a11y_bridge_widget_state(pt_widget *widget, const char *state, bool value);
void pt_a11y_bridge_announce(pt_widget *widget, int politeness, const char *text);

/* ---------- app / window ---------- */

/* fd-watch integration used by the platform dispatch loop. The prepare call
 * fills the pollfd entries starting at offset; dispatch reports ready events
 * back to the callbacks. */
size_t pt_app_fd_watch_count(const pt_app *app);
void pt_app_fd_watch_prepare(const pt_app *app, struct pollfd *fds, size_t offset);
void pt_app_fd_watch_dispatch(pt_app *app, const struct pollfd *fds, size_t offset);

typedef struct pt_theme_data {
    pt_color colors[pt_theme_token_count];
    float metrics[pt_theme_metric_count];
    char font_family[96];
    pt_color_scheme scheme;
} pt_theme_data;

typedef struct pt_timer {
    uint64_t id;
    int64_t deadline_ms;
    pt_timer_callback callback;
    void *user_data;
    struct pt_timer *next;
} pt_timer;

typedef struct pt_fd_watch {
    uint64_t id;
    int fd;
    uint32_t events;
    pt_fd_callback callback;
    void *user_data;
    bool owned;
    struct pt_fd_watch *next;
} pt_fd_watch;

struct pt_app {
    char *app_id;
    pt_theme_data theme;
    pt_window *first_window;
    pt_timer *timers;
    pt_fd_watch *fd_watches;
    /* Watches removed during dispatch. Freeing (and closing an owned fd) waits
     * until the callback returns (standards section 11). */
    pt_fd_watch *fd_garbage;
    int fd_dispatch_depth;
    uint64_t next_timer_id;
    uint64_t next_fd_watch_id;
    /* Explicit application UI direction (since 2.0); zero means inherit/LTR. */
    uint8_t ui_direction;
    /* Explicit application caret movement (since 2.1); zero means inherit. */
    uint8_t caret_movement;
    /* Locale hint and its derived application fallback direction (since 2.1).
     * locale is owned; locale_ui_direction is 0 when unset (LTR). */
    char *locale;
    uint8_t locale_ui_direction;
    bool running;
    bool quit_requested;
    bool quit_on_last_window_close;
    bool reduced_motion;
    bool initialized;
    bool caret_timer_active;
    uint64_t caret_timer_id;
    pt_a11y_event_callback a11y_callback;
    void *a11y_data;
    pt_a11y_status a11y_status;
    struct pt_a11y_bridge *a11y_bridge;
    struct pt_platform *platform;
};

struct pt_window {
    pt_app *app;
    pt_window *next;
    char *title;
    pt_icon_image *icon;
    pt_widget *root;
    uint32_t a11y_id;

    bool theme_color_set[pt_theme_token_count];
    pt_color theme_colors[pt_theme_token_count];
    bool theme_metric_set[pt_theme_metric_count];
    float theme_metrics[pt_theme_metric_count];

    int width;
    int height;
    int requested_width;
    int requested_height;
    int min_width;
    int min_height;
    float scale;
    float render_scale;
    float header_height;

    bool open;
    bool shown;
    bool configured;
    bool close_requested;
    bool close_notified;
    bool needs_layout;
    bool needs_draw;
    bool decorated;
    /* Explicit window UI direction (since 2.0); zero means inherit from app. */
    uint8_t ui_direction;
    /* Explicit window caret movement (since 2.1); zero means inherit. */
    uint8_t caret_movement;
    pt_decoration_mode decoration_mode;
    pt_decoration_mode effective_decoration;
    bool csd;
    bool resizable;
    bool keyboard_focused;
    uint32_t modifiers;
    bool maximized;
    bool fullscreen;
    bool tiled_left;
    bool tiled_right;
    bool tiled_top;
    bool tiled_bottom;
    bool wm_capabilities_known;
    bool maximize_enabled;
    bool minimize_enabled;
    bool fullscreen_enabled;
    int bounds_max_width;
    int bounds_max_height;
    bool animating;
    bool redraw_scheduled;
    bool pointer_inside;

    pt_widget *hovered;
    pt_widget *focused;
    pt_widget *active_widget;
    pt_csd_part active_csd;
    pt_csd_part hovered_csd;
    pt_cursor current_cursor;
    float pointer_x;
    float pointer_y;
    double last_paint_time;
    /* Double-click tracking is per button: a right-click between two left
     * clicks must not reset the left click count (standards section 9). */
    uint32_t last_click_time[32];
    float last_click_x[32];
    float last_click_y[32];
    uint32_t click_count[32];
    uint32_t pointer_serial;
    uint32_t key_serial;
    /* Serial of the last pointer release or key press that can legally trigger
     * a primary-selection set. Programmatic sets without one return false. */
    uint32_t primary_serial;
    uint32_t pointer_buttons;
    int32_t touch_id;
    bool touch_active;
#define pt_touch_max 10
    struct {
        int32_t id;
        float x;
        float y;
    } touches[pt_touch_max];
    uint32_t touch_count;
    bool touch_pinch;
    bool touch_gesture;
    float touch_pinch_distance;
    float touch_pinch_angle;

    pt_close_callback close_callback;
    void *close_data;
    pt_resize_callback resize_callback;
    void *resize_data;
    pt_key_callback key_callback;
    void *key_data;
    pt_key_event_callback key_event_callback;
    void *key_event_data;
    pt_focus_callback focus_callback;
    void *focus_data;
    pt_pointer_callback pointer_callback;
    void *pointer_data;

    bool custom_ime_enabled;
    pt_ime_commit_callback custom_ime_commit;
    pt_ime_preedit_callback custom_ime_preedit;
    void *custom_ime_data;
    int custom_ime_x;
    int custom_ime_y;
    int custom_ime_width;
    int custom_ime_height;
    bool custom_ime_rect_valid;
    /* Desired text-input-v3 v2 input-panel (on-screen keyboard) state. It is
     * forwarded on the next activation when the window has no active
     * text-input object yet; the compositor remains free to ignore it. */
    bool input_panel_requested;

    pt_popup_policy popup_policy;

    pt_gesture_callback gesture_callback;
    void *gesture_data;
    uint32_t long_press_delay;
    uint64_t long_press_timer;
    bool long_press_fired;
    pt_widget *press_target;
    float press_x;
    float press_y;

    bool gesture_active;
    pt_gesture_kind gesture_kind;
    uint32_t gesture_fingers;
    float gesture_dx;
    float gesture_dy;
    float gesture_scale;
    float gesture_rotation;
    float gesture_x;
    float gesture_y;
    pt_widget *gesture_target;

    bool momentum_enabled;
    bool momentum_active;
    double momentum_last_time;
    float momentum_velocity;
    float momentum_x;
    float momentum_y;

    pt_widget *drop_hover;
    pt_drop_offer *drop_offer;
    pt_drag_finished_callback drag_finished;
    void *drag_finished_data;
    pt_drag_action drag_result;
    bool drag_notified;
    pt_widget *drag_widget;
    char *drag_text;
    pt_color *drag_image;
    int drag_image_width;
    int drag_image_height;
    float drag_start_x;
    float drag_start_y;
    bool drag_pending;
    bool drag_started;
    bool drag_active;
    uint32_t drag_timeout;
    int64_t drag_deadline;

    pt_stylus_callback stylus_callback;
    void *stylus_data;
    bool stylus_down;
    bool stylus_in_range;

    bool caret_visible;
    double caret_last_toggle;

    pt_overlay *overlays;
    pt_overlay_garbage *overlay_garbage;
    pt_paint_garbage *paint_garbage;
    pt_toast *first_toast;
    pt_widget *tooltip_target;
    uint64_t tooltip_timer;

    pt_canvas *backing;
    int backing_width;
    int backing_height;
    float backing_scale;

    pt_rect damage;
#define pt_damage_tile_capacity 8
    pt_rect damage_tiles[pt_damage_tile_capacity];
    uint8_t damage_tile_count;
    bool damage_valid;
    bool damage_full;
    pt_rect last_damage;
    pt_rect last_damage_tiles[pt_damage_tile_capacity];
    uint8_t last_damage_tile_count;
    bool last_damage_full;
    pt_widget *layout_root;
    bool last_layout_partial;
    uint32_t paint_visits;
    /* True while the window paint pass is running. Used to reject a re-entrant
     * pt_window_paint_pixels and to defer widget destruction (see paint.c). */
    bool in_paint;

    struct pt_wayland_window *platform_window;
};

pt_window *pt_window_alloc(pt_app *app, const char *title, int width, int height);
void pt_window_free(pt_window *window);
void pt_window_apply_configure(pt_window *window, int width, int height);
bool pt_window_paint(pt_window *window, pt_canvas *canvas);
/* Pure full-frame snapshot: paints without touching damage, needs_draw,
 * redraw_scheduled, last_paint_time or deferred overlay garbage. */
bool pt_window_paint_snapshot(pt_window *window, pt_canvas *canvas);
/* Frees widgets whose destruction was deferred by a paint callback. Runs at the
 * end of a paint pass and when the window is freed. */
void pt_window_paint_collect(pt_window *window);
void pt_window_input_pointer_enter(pt_window *window, float x, float y);
void pt_window_input_pointer_motion(pt_window *window, float x, float y, uint32_t time);
void pt_window_input_pointer_leave(pt_window *window);
void pt_window_input_pointer_button(pt_window *window, uint32_t button, bool pressed, float x,
                                    float y, uint32_t serial, uint32_t time);
void pt_window_input_axis(pt_window *window, float x, float y, float delta);
/* The axis kind an axis event belongs to. Only the vertical axis feeds the
 * public 4-arg pt_window_input_axis contract; the platform forwards a
 * horizontal wl_pointer axis here so the widget dispatch can route it without
 * touching the public struct or callbacks (delta_x stays 0 in the vertical
 * case, byte-identical to 2.1). */
typedef enum pt_axis_kind {
    pt_axis_vertical = 0,
    pt_axis_horizontal = 1,
} pt_axis_kind;
void pt_window_input_axis_kind(pt_window *window, pt_axis_kind axis, float x, float y,
                               float delta);
void pt_window_input_keyboard_focus(pt_window *window, bool focused, uint32_t serial);
/* Records the serial of a key press before raw/focused dispatch, so a consumed
 * key still updates the serial a primary-selection set can use. */
void pt_window_input_key_serial(pt_window *window, uint32_t serial);
void pt_window_input_key(pt_window *window, uint32_t keysym, uint32_t modifiers,
                         const char *utf8, bool pressed);
void pt_window_set_input_modifiers(pt_window *window, uint32_t modifiers);
void pt_window_drag_finished(pt_window *window);
bool pt_window_drag_source_begin(pt_window *window, const char *mime_type,
                                 const char *payload);
bool pt_window_drag_source_begin_types(pt_window *window, const pt_drag_data *types,
                                       uint32_t count);
/* Shared text/uri-list encoder used by the clipboard writer. Entries are
 * absolute filesystem paths (encoded as file:// URIs with percent-escaped
 * bytes) or already-formed URIs (copied verbatim). Lines end with CRLF.
 * Returns caller-owned memory; NULL on invalid input or the clipboard cap. */
char *pt_uri_list_encode(const char *const *uris, uint32_t count);
void pt_window_drag_finish_notify(pt_window *window, bool dropped, pt_drag_action action);
void pt_window_input_touch_down(pt_window *window, int32_t id, float x, float y, uint32_t time,
                                uint32_t serial);
void pt_window_input_touch_motion(pt_window *window, int32_t id, float x, float y,
                                  uint32_t time);
/* Removes one contact and returns how many contacts are still active, so the
 * platform layer can keep touch focus until the last contact lifts. */
uint32_t pt_window_input_touch_up(pt_window *window, int32_t id, uint32_t time);
void pt_window_input_touch_cancel(pt_window *window);
void pt_window_stylus_input(pt_window *window, pt_stylus_phase phase, pt_stylus_tool tool,
                            float x, float y, float pressure, float distance, float tilt_x,
                            float tilt_y, float rotation, uint32_t buttons);
void pt_window_input_ime_commit(pt_window *window, const char *text);
void pt_window_input_ime_preedit(pt_window *window, const char *text);
void pt_window_input_ime_compose(pt_window *window, const char *commit, const char *preedit,
                                 int32_t cursor_begin, int32_t cursor_end,
                                 uint32_t delete_before, uint32_t delete_after);
/* Compose variant carrying text-input-v3 v2 preedit hints. The hints are
 * presentation only; the commit/preedit/done rules are identical. */
void pt_window_input_ime_compose_ex(pt_window *window, const char *commit,
                                    const char *preedit, int32_t cursor_begin,
                                    int32_t cursor_end, uint32_t delete_before,
                                    uint32_t delete_after, const pt_ime_hint *hints,
                                    uint32_t hint_count);
/* Applies a text-input-v3 v2 action. Submit activates the focused entry through
 * its submit callback; it never synthesizes an Enter key or inserts "\n". */
void pt_window_input_ime_action(pt_window *window, uint32_t action);
void pt_window_gesture_input(pt_window *window, pt_gesture_kind kind, pt_gesture_phase phase,
                             uint32_t fingers, float dx, float dy, float scale,
                             float rotation);
void pt_window_press_begin(pt_window *window, pt_widget *target, float x, float y,
                           uint32_t button);
void pt_window_press_motion(pt_window *window, float x, float y);
void pt_window_press_end(pt_window *window, float x, float y);
void pt_window_press_cancel(pt_window *window);
bool pt_window_interaction_tick(pt_window *window, float dt);
int64_t pt_window_interaction_deadline(const pt_window *window);
void pt_window_momentum_start(pt_window *window, float velocity, float x, float y);
void pt_window_momentum_stop(pt_window *window);
void pt_window_momentum_advance(pt_window *window, float dt);
bool pt_window_dnd_enter(pt_window *window, const char *mime_type, float x, float y);
bool pt_window_dnd_motion(pt_window *window, const char *mime_type, float x, float y);
void pt_window_dnd_leave(pt_window *window);
bool pt_window_dnd_drop(pt_window *window, const char *mime_type, const char *data, float x,
                        float y);
void pt_window_dnd_cancel(pt_window *window);
bool pt_window_dnd_offer_enter(pt_window *window, pt_drop_offer *offer, float x, float y);
bool pt_window_dnd_offer_motion(pt_window *window, float x, float y);
bool pt_window_dnd_offer_drop(pt_window *window, float x, float y);

/* ---------- drop offers and streams ---------- */

typedef bool (*pt_drop_offer_start_read_fn)(pt_drop_offer *offer, const char *mime_type,
                                            int fd);
typedef char *(*pt_drop_offer_read_sync_fn)(pt_drop_offer *offer, const char *mime_type,
                                            uint32_t timeout_ms);
typedef void (*pt_drop_offer_finish_fn)(pt_drop_offer *offer, bool success);
typedef void (*pt_drop_offer_free_fn)(pt_drop_offer *offer);

typedef struct pt_drop_payload {
    char *mime_type;
    char *data;
    size_t size;
    uint32_t chunk_size;
    struct pt_drop_payload *next;
} pt_drop_payload;

struct pt_drop_offer {
    pt_app *app;
    pt_window *window;
    void *backend;
    char **mimes;
    uint32_t mime_count;
    uint32_t actions;
    char *selected_mime;
    pt_drop_offer_start_read_fn start_read;
    pt_drop_offer_read_sync_fn read_sync;
    pt_drop_offer_finish_fn finish;
    pt_drop_offer_free_fn free_backend;
    pt_drop_payload *payloads;
    pt_widget *target_widget;
    int refs;
    bool dropped;
    bool drop_accepted;
    bool finished;
};

pt_drop_offer *pt_drop_offer_new(pt_app *app, pt_window *window, const char *const *mimes,
                                 uint32_t count);
void pt_drop_offer_retain(pt_drop_offer *offer);
void pt_drop_offer_release(pt_drop_offer *offer);
void pt_drop_offer_add_mime(pt_drop_offer *offer, const char *mime_type);
void pt_drop_offer_add_payload(pt_drop_offer *offer, const char *mime_type, const void *data,
                               size_t size, uint32_t chunk_size);

/* The event loop pumps async streams; tests call this directly. */
void pt_dnd_pump(pt_app *app);
bool pt_dnd_pending(const pt_app *app);
void pt_dnd_widget_destroyed(pt_widget *widget);
void pt_dnd_window_destroyed(pt_window *window);

#define pt_dnd_timeout_ms 1500u
void pt_window_request_close(pt_window *window, bool force);
void pt_window_tick(pt_window *window);
void pt_window_set_fullscreen(pt_window *window, bool fullscreen);
void pt_window_set_maximized(pt_window *window, bool maximized);
void pt_window_set_csd_enabled(pt_window *window, bool enabled);
/* Applies a decoration mode from the compositor's configure. Never changes the
 * requested mode; the effective mode and header follow the answer. */
void pt_window_apply_decoration_mode(pt_window *window, pt_decoration_mode mode);
void pt_window_apply_toplevel_state(pt_window *window, bool maximized, bool fullscreen,
                                    bool tiled_left, bool tiled_right, bool tiled_top,
                                    bool tiled_bottom);
void pt_window_apply_configure_bounds(pt_window *window, int width, int height);
void pt_window_apply_wm_capabilities(pt_window *window, bool maximize, bool minimize,
                                     bool fullscreen);
void pt_window_update_caret_timer(pt_window *window);
bool pt_window_has_editable_focus(const pt_window *window);
bool pt_window_wants_ime(const pt_window *window);
void pt_window_restore_ime(pt_window *window);

void pt_theme_init(pt_theme_data *theme, pt_color_scheme scheme);
void pt_theme_set_defaults(pt_theme_data *theme, pt_color_scheme scheme);
pt_color pt_theme_contrast_text(pt_color background);
pt_color pt_theme_color(const pt_window *window, pt_theme_token token);
float pt_theme_metric_value(const pt_window *window, pt_theme_metric metric);
pt_color pt_widget_theme_color(const pt_widget *widget, pt_theme_token token);
float pt_widget_theme_metric_value(const pt_widget *widget, pt_theme_metric metric);
void pt_app_cleanup_windows(pt_app *app);

/* ---------- overlays ---------- */

void pt_window_overlays_layout(pt_window *window, const pt_layout_ctx *ctx);
void pt_window_overlays_paint(pt_window *window, pt_canvas *canvas);
void pt_window_overlays_free(pt_window *window);
void pt_window_overlays_collect(pt_window *window);
void pt_window_overlays_tick(pt_window *window, float dt);
void pt_window_overlays_forget_widget(pt_window *window, pt_widget *root);
bool pt_window_overlays_active(const pt_window *window);
bool pt_window_overlays_modal(const pt_window *window);
pt_widget *pt_window_overlay_hit_test(pt_window *window, float x, float y);
bool pt_window_overlays_pointer(pt_window *window, uint32_t button, bool pressed, float x,
                                float y, uint32_t serial, uint32_t time);
bool pt_window_overlays_key(pt_window *window, uint32_t keysym, uint32_t modifiers,
                            const char *utf8);
bool pt_window_overlays_axis(pt_window *window, pt_axis_kind axis, float x, float y,
                             float delta);
void pt_window_overlays_update_tooltip(pt_window *window);
void pt_window_overlays_dismiss_tooltip(pt_window *window);
pt_widget *pt_window_overlays_focus_scope(const pt_window *window);
bool pt_window_overlays_focus_next(pt_window *window, bool backwards);
/* Focuses the first focusable widget of an overlay panel; used when an
 * application popup opens. */
void pt_window_overlay_focus_first(pt_overlay *overlay);
void pt_window_overlay_native_done(pt_overlay *overlay);
/* Moves a detached panel/tree to the window's deferred-free list. Used by
 * app popups so destruction from inside a callback cannot free a tree that the
 * current widget dispatch is still walking. */
void pt_window_overlay_defer_panel_free(pt_window *window, pt_widget *panel);

/* Owner-built popup surfaces. A combo box allocates the overlay inside its own
 * state and links it here, so menus, dialogs and combo popups share focus
 * restoration, native surfaces and panel garbage collection. The panel is a
 * detached widget; the overlay adopts it. */
void pt_window_popup_show(pt_overlay *overlay, pt_window *window, pt_widget *owner,
                          pt_widget *panel);
void pt_window_popup_hide(pt_overlay *overlay);
void pt_window_popup_replace_panel(pt_overlay *overlay, pt_widget *panel);

/* Application popups (M19). The layout helper computes the overlay rect from
 * the anchor and placement, flipping and sliding inside the window when the
 * popup uses the in-window fallback. The free helper releases the content tree
 * and the popup without touching the window overlay list; the window teardown
 * path owns that list while it iterates. */
void pt_popup_overlay_layout(pt_popup *popup, pt_overlay *overlay,
                             const pt_layout_ctx *ctx);
void pt_popup_window_free(pt_popup *popup);

/* The xdg_positioner decision for an owner-built popup surface. anchor and
 * gravity hold xdg_positioner enum values and the Wayland backend fills a
 * positioner from this result. Keeping the decision pure lets tests assert the
 * four placements against the in-window fallback without a compositor. */
typedef struct pt_popup_positioner_spec {
    int anchor_x;
    int anchor_y;
    int anchor_width;
    int anchor_height;
    uint32_t anchor;
    uint32_t gravity;
    uint32_t constraint;
} pt_popup_positioner_spec;

void pt_popup_positioner_spec_for(pt_overlay_kind kind, pt_popup_placement placement,
                                  int anchor_x, int anchor_y, int anchor_width,
                                  int anchor_height, pt_popup_positioner_spec *out);

/* ---------- damage ---------- */

void pt_window_damage_rect(pt_window *window, pt_rect rect);
void pt_window_damage_all(pt_window *window);
void pt_widget_damage(pt_widget *widget, float margin);
bool pt_window_paint_damage(pt_window *window, pt_canvas *canvas);
void pt_canvas_clear_rect(pt_canvas *canvas, float x, float y, float w, float h,
                          pt_color color);

/* ---------- platform (Wayland backend) ---------- */

typedef struct pt_platform pt_platform;

bool pt_platform_start(pt_app *app);
void pt_platform_stop(pt_app *app);
bool pt_platform_window_init(pt_window *window);
void pt_platform_window_destroy(pt_window *window);
void pt_platform_window_show(pt_window *window);
void pt_platform_window_hide(pt_window *window);
void pt_platform_window_set_title(pt_window *window);
/* Best-effort xdg_toplevel_icon_v1 submission; a no-op without the global or
 * in headless mode (window->icon stays owned by the window). */
void pt_platform_window_set_icon(pt_window *window);
void pt_platform_window_set_min_size(pt_window *window);
void pt_platform_window_set_resizable(pt_window *window);
void pt_platform_window_set_decorated(pt_window *window);
void pt_platform_window_set_decoration_mode(pt_window *window);
void pt_platform_window_schedule_draw(pt_window *window);
void pt_platform_window_begin_move(pt_window *window, uint32_t serial);
void pt_platform_window_begin_resize(pt_window *window, uint32_t serial, uint32_t edges);
void pt_platform_window_minimize(pt_window *window);
void pt_platform_window_toggle_maximize(pt_window *window);
void pt_platform_window_set_fullscreen(pt_window *window, bool fullscreen);
void pt_platform_window_set_cursor(pt_window *window, pt_cursor cursor);
void pt_platform_window_set_size_hint(pt_window *window);
void pt_platform_clipboard_set(pt_window *window, const char *text);
char *pt_platform_clipboard_get(pt_window *window);
/* Generic clipboard slot used by the image and text/uri-list writers. The
 * payload is copied and capped at 16 MiB; the advertised MIME is either the
 * exact type or, for text, the usual text/plain family. Headless keeps the
 * in-process fallback. */
bool pt_platform_clipboard_set_data(pt_window *window, const char *mime,
                                    const void *data, size_t size);
bool pt_platform_clipboard_clear(pt_window *window);
const char *pt_platform_clipboard_mime(pt_window *window);
const void *pt_platform_clipboard_data(pt_window *window, size_t *size);
pt_color *pt_platform_clipboard_get_image(pt_window *window, int *width, int *height);
void pt_platform_primary_set(pt_window *window, const char *text, uint32_t serial);
char *pt_platform_primary_get(pt_window *window);
bool pt_platform_begin_drag_types(pt_window *window, const pt_drag_data *types,
                                  uint32_t count);
void pt_platform_cancel_drag(pt_window *window);
bool pt_platform_pump_io(pt_app *app);
bool pt_platform_popup_supported(pt_window *window);
void pt_platform_present_popups(pt_window *window, const pt_layout_ctx *ctx);
void pt_platform_popup_destroy(pt_overlay *overlay);
void pt_platform_popup_destroy_all(pt_window *window);
void pt_platform_ime_set_enabled(pt_window *window, bool enabled);
void pt_platform_ime_set_cursor_rect(pt_window *window, int x, int y, int width, int height);
/* Forwards a text-input-v3 v2 show/hide_input_panel request when this window
 * owns the active text-input object. Returns true when the request reached the
 * protocol object; false for headless, no focus or a v1 text-input. */
bool pt_platform_ime_input_panel(pt_window *window, bool show);
int pt_platform_dispatch(pt_app *app, int timeout_ms);
int pt_platform_dispatch_once(pt_app *app);
void pt_platform_flush(pt_app *app);

/* App-internal timer helper used by the caret blink implementation. */
uint64_t pt_app_add_timeout_internal(pt_app *app, uint32_t ms, pt_timer_callback callback,
                                     void *data);

/* ---------- bidi (2.0) ---------- */

/* One run of a line after rules L1-L2. Offsets and lengths are logical UTF-8
 * byte positions in the analysed text, so removed X9 controls still occupy
 * bytes (shaping keeps them; drawing skips them with pt_bidi_is_removed).
 * `level` is the resolved embedding level. Runs are emitted in visual order,
 * left to right, and `visual_index` is the run's position in that order. */
typedef struct pt_bidi_run {
    size_t offset;
    size_t length;
    uint8_t level;
    size_t visual_index;
} pt_bidi_run;

/* A reordered line. `offset`/`length` are the logical byte range passed to
 * pt_bidi_line_create; `base_level` is the resolved paragraph level. */
typedef struct pt_bidi_line {
    size_t offset;
    size_t length;
    uint8_t base_level;
    pt_bidi_run *runs;
    size_t run_count;
} pt_bidi_line;

/* Opaque analysed paragraph. The text is decoded once into an internal
 * code-point copy; callers may release their buffer afterwards. */
typedef struct pt_bidi_paragraph pt_bidi_paragraph;

/* Analyses the first paragraph in [0, len). On success `*out` owns the
 * paragraph and must be released with pt_bidi_paragraph_free. An empty input
 * succeeds with a paragraph of length zero and the base level implied by the
 * requested direction. */
bool pt_bidi_paragraph_analyze(const char *text, size_t len, pt_bidi_base base,
                               pt_bidi_paragraph **out);
/* Auto P2/P3 analysis with an explicit P3 default when no strong character is
 * found. Explicit pt_bidi_base_ltr/_rtl bypass P2/P3, so this is how direction
 * resolution tells "no strong character" apart from a leading L: analyse with
 * an LTR default first, then with an RTL default, and compare the base levels. */
bool pt_bidi_paragraph_analyze_default(const char *text, size_t len, bool rtl_default,
                                       pt_bidi_paragraph **out);
void pt_bidi_paragraph_free(pt_bidi_paragraph *paragraph);
uint8_t pt_bidi_paragraph_base_level(const pt_bidi_paragraph *paragraph);
size_t pt_bidi_paragraph_length(const pt_bidi_paragraph *paragraph);

/* Reorders `length` bytes of the paragraph starting at `offset` (rules L1-L2)
 * and returns the visual runs. The range must lie inside the paragraph. The
 * line owns its runs; release it with pt_bidi_line_free. */
bool pt_bidi_line_create(const pt_bidi_paragraph *paragraph, size_t offset, size_t length,
                         pt_bidi_line *out);
void pt_bidi_line_free(pt_bidi_line *line);

/* Resolved level of the byte at `byte_offset`, or -1 when outside the line.
 * Removed control bytes still return the level of their run, so callers must
 * check pt_bidi_is_removed before painting or measuring them. */
int pt_bidi_line_level_at(const pt_bidi_line *line, size_t byte_offset);

/* True for the X9 controls removed by the bidi algorithm (BN, LRE, RLE, LRO,
 * RLO, PDF). Their bytes stay in runs for shaping but must not be painted. */
bool pt_bidi_is_removed(uint32_t codepoint);
/* Cheap pre-scan used by the LTR fast path: true when the UTF-8 text contains
 * a strong right-to-left character (bidi class R or AL) or any bidi formatting
 * control (LRE/RLE/LRO/RLO/PDF/LRI/RLI/FSI/PDI). Text with none of those cannot
 * reorder under an LTR base, so it may skip the UBA entirely. */
bool pt_bidi_text_may_reorder(const char *text, size_t len);
/* P2 first-strong direction over the first paragraph: strong L returns
 * pt_bidi_base_ltr, strong R/AL returns pt_bidi_base_rtl, and text inside
 * isolates or with no strong character returns pt_bidi_base_auto. Cheap and
 * allocation-free, for resolve-before-paint paths. */
pt_bidi_base pt_bidi_first_strong_direction(const char *text, size_t len);
const char *pt_bidi_engine_version(void);
const char *pt_bidi_unicode_version(void);

#endif
