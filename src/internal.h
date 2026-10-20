/*
 * Pt internal interfaces. Not installed.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_internal_h
#define pt_internal_h

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include <pt/pt.h>

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
    pt_overlay_kind_combobox
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
    char *text;
    size_t len;
    hb_buffer_t *buffer;
    struct pt_shape_entry *newer;
    struct pt_shape_entry *older;
} pt_shape_entry;

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
} pt_text_line;

typedef struct pt_text_run {
    size_t start;
    size_t end;
    pt_font *font;
    pt_color color;
    uint32_t flags;
} pt_text_run;

typedef struct pt_text_runs {
    const char *text;
    size_t length;
    pt_text_run *items;
    size_t count;
    float scale;
    float ascent;
    float descent;
    float line_height;
} pt_text_runs;

bool pt_widget_supports_spans(const pt_widget *widget);
void pt_widget_spans_on_insert(pt_widget *widget, size_t position, size_t length);
void pt_widget_spans_on_delete(pt_widget *widget, size_t start, size_t end);
void pt_widget_spans_clamp(pt_widget *widget);
bool pt_text_runs_build(const pt_widget *widget, const pt_layout_ctx *ctx, pt_text_runs *out);
void pt_text_runs_free(pt_text_runs *runs);
float pt_text_runs_measure(const pt_text_runs *runs, size_t start, size_t end);
float pt_text_runs_natural_width(const pt_text_runs *runs);
int pt_text_runs_wrap(const pt_text_runs *runs, float max_width, pt_text_line **out_lines);
void pt_text_runs_draw_line(pt_canvas *canvas, const pt_text_runs *runs, size_t start,
                            size_t end, float x, float baseline, float max_width,
                            pt_text_align align, pt_color color, bool ellipsis);
size_t pt_text_runs_position_for_x(const pt_text_runs *runs, size_t start, size_t end,
                                   float local_x);

pt_font *pt_font_acquire(const char *family, int weight, pt_font_slant slant,
                         float pixel_size);
void pt_font_manager_shutdown(void);
float pt_font_measure(const pt_font *font, const char *text, size_t len);
int pt_font_wrap_lines(const pt_font *font, const char *text, size_t len, float max_width,
                       pt_text_line **out_lines);
float pt_font_wrapped_height(const pt_font *font, const char *text, float max_width);
void pt_canvas_draw_text_line(pt_canvas *canvas, const pt_font *font, const char *text,
                              size_t len, float x, float baseline, float max_width,
                              pt_text_align align, pt_color color, bool ellipsis);
void pt_canvas_draw_text_wrapped(pt_canvas *canvas, const pt_font *font, const char *text,
                                 float x, float top, float max_width, float max_height,
                                 pt_text_align align, pt_color color);
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
} pt_combobox_state;

/* ---------- widgets ---------- */

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
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;
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
} pt_widget;

static inline bool pt_widget_kind_is_text_input(pt_widget_kind kind) {
    return kind == pt_widget_entry || kind == pt_widget_text_area;
}

typedef struct pt_text_layout {
    pt_font *font;
    float scale;
    float line_height;
    char *display;
    size_t display_len;
    size_t caret;
    pt_text_line *lines;
    int line_count;
    bool has_runs;
    pt_text_runs runs;
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
int pt_text_layout_line_for_position(const pt_text_layout *layout, size_t position);
float pt_text_layout_x_for_position(const pt_text_layout *layout, int line, size_t position);
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

struct pt_app {
    char *app_id;
    pt_theme_data theme;
    pt_window *first_window;
    pt_timer *timers;
    uint64_t next_timer_id;
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
    bool csd;
    bool resizable;
    bool keyboard_focused;
    uint32_t modifiers;
    bool maximized;
    bool fullscreen;
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
    uint32_t last_click_time;
    float last_click_x;
    float last_click_y;
    uint32_t click_count;
    uint32_t pointer_serial;
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

    struct pt_wayland_window *platform_window;
};

pt_window *pt_window_alloc(pt_app *app, const char *title, int width, int height);
void pt_window_free(pt_window *window);
void pt_window_apply_configure(pt_window *window, int width, int height);
bool pt_window_paint(pt_window *window, pt_canvas *canvas);
void pt_window_input_pointer_enter(pt_window *window, float x, float y);
void pt_window_input_pointer_motion(pt_window *window, float x, float y, uint32_t time);
void pt_window_input_pointer_leave(pt_window *window);
void pt_window_input_pointer_button(pt_window *window, uint32_t button, bool pressed, float x,
                                    float y, uint32_t serial, uint32_t time);
void pt_window_input_axis(pt_window *window, float x, float y, float delta);
void pt_window_input_keyboard_focus(pt_window *window, bool focused, uint32_t serial);
void pt_window_input_key(pt_window *window, uint32_t keysym, uint32_t modifiers,
                         const char *utf8, bool pressed);
void pt_window_set_input_modifiers(pt_window *window, uint32_t modifiers);
void pt_window_drag_finished(pt_window *window);
bool pt_window_drag_source_begin(pt_window *window, const char *mime_type,
                                 const char *payload);
bool pt_window_drag_source_begin_types(pt_window *window, const pt_drag_data *types,
                                       uint32_t count);
void pt_window_drag_finish_notify(pt_window *window, bool dropped, pt_drag_action action);
void pt_window_input_touch_down(pt_window *window, int32_t id, float x, float y, uint32_t time,
                                uint32_t serial);
void pt_window_input_touch_motion(pt_window *window, int32_t id, float x, float y,
                                  uint32_t time);
void pt_window_input_touch_up(pt_window *window, int32_t id, uint32_t time);
void pt_window_input_touch_cancel(pt_window *window);
void pt_window_stylus_input(pt_window *window, pt_stylus_phase phase, pt_stylus_tool tool,
                            float x, float y, float pressure, float distance, float tilt_x,
                            float tilt_y, float rotation, uint32_t buttons);
void pt_window_input_ime_commit(pt_window *window, const char *text);
void pt_window_input_ime_preedit(pt_window *window, const char *text);
void pt_window_input_ime_compose(pt_window *window, const char *commit, const char *preedit,
                                 int32_t cursor_begin, int32_t cursor_end,
                                 uint32_t delete_before, uint32_t delete_after);
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
void pt_window_update_caret_timer(pt_window *window);
bool pt_window_has_editable_focus(const pt_window *window);

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
bool pt_window_overlays_axis(pt_window *window, float x, float y, float delta);
void pt_window_overlays_update_tooltip(pt_window *window);
void pt_window_overlays_dismiss_tooltip(pt_window *window);
pt_widget *pt_window_overlays_focus_scope(const pt_window *window);
bool pt_window_overlays_focus_next(pt_window *window, bool backwards);
void pt_window_overlay_native_done(pt_overlay *overlay);

/* Owner-built popup surfaces. A combo box allocates the overlay inside its own
 * state and links it here, so menus, dialogs and combo popups share focus
 * restoration, native surfaces and panel garbage collection. The panel is a
 * detached widget; the overlay adopts it. */
void pt_window_popup_show(pt_overlay *overlay, pt_window *window, pt_widget *owner,
                          pt_widget *panel);
void pt_window_popup_hide(pt_overlay *overlay);
void pt_window_popup_replace_panel(pt_overlay *overlay, pt_widget *panel);

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
void pt_platform_window_set_min_size(pt_window *window);
void pt_platform_window_set_resizable(pt_window *window);
void pt_platform_window_set_decorated(pt_window *window);
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
int pt_platform_dispatch(pt_app *app, int timeout_ms);
int pt_platform_dispatch_once(pt_app *app);
void pt_platform_flush(pt_app *app);

/* App-internal timer helper used by the caret blink implementation. */
uint64_t pt_app_add_timeout_internal(pt_app *app, uint32_t ms, pt_timer_callback callback,
                                     void *data);

#endif
