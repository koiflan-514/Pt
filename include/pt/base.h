/*
 * Pt - a small Wayland-native GUI framework.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef pt_base_h
#define pt_base_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Header contract
 *
 * Threading. Pt is single-threaded. Every function that reads or mutates an
 * application, window, widget, overlay, accessibility model, drag or gesture
 * state must run on the UI thread that owns the application, including getters
 * and queries. The exceptions are the value helpers declared in this header and
 * a short list of functions that touch only caller-owned data: pt_rgba, pt_rgb,
 * pt_color_alpha, pt_ease, pt_ease_value, the pt_transition_* functions,
 * pt_uri_list_count, pt_uri_list_get, pt_free and pt_get_last_error. Those keep
 * no shared mutable state; pt_get_last_error is thread-local. Callbacks always
 * run on the UI thread. There is no locking, so an off-thread call to anything
 * else is undefined behavior even when it only reads.
 *
 * Ownership. Constructors that take a parent append the new object to it and
 * transfer ownership to the parent's subtree. A handle returned by a container
 * or a lookup is borrowed and stays valid until the owning object is destroyed
 * or the specific function documents a shorter lifetime. Only buffers that Pt
 * explicitly hands to the caller are caller-owned and released with pt_free;
 * every other returned pointer is borrowed and must not be freed. Strings
 * passed to setters are copied unless the declaration says otherwise.
 *
 * Errors. One error model applies to the whole API. Creation and lookup
 * functions return NULL, false, zero or a documented sentinel on failure and
 * set the thread-local message returned by pt_get_last_error. Setters return
 * void and are no-ops for NULL objects, a mismatched widget kind or an
 * out-of-range index; they do not report an error. Functions returning bool
 * return false for an invalid object or an operation that does not apply.
 * Getters clamp their arguments and return a default instead of failing.
 * Allocation failure is fatal: Pt prints a diagnostic and aborts rather than
 * returning a half-initialized object.
 *
 * Strings and callbacks. Text is UTF-8 and text offsets are byte positions.
 * A pointer argument passed to a callback is valid only for the duration of
 * that callback unless the callback typedef says otherwise; copy the data to
 * keep it. user_data is passed through unchanged and must stay valid until the
 * callback is replaced or the owning object is destroyed.
 */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define pt_api __attribute__((visibility("default")))
#define pt_deprecated(message) __attribute__((deprecated(message)))
#else
#define pt_api
#define pt_deprecated(message)
#endif

#define pt_version_major 2
#define pt_version_minor 4
#define pt_version_patch 0
#define pt_version_string "2.4.0"

/* Public colors are packed as 0xRRGGBBAA. The renderer converts them to
 * premultiplied ARGB8888 internally. */
typedef uint32_t pt_color;

#define pt_color_transparent 0x00000000u

static inline pt_color pt_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

static inline pt_color pt_rgb(uint32_t rgb) {
    return (rgb << 8) | 0xFFu;
}

static inline pt_color pt_color_alpha(pt_color color, uint8_t alpha) {
    return (color & 0xFFFFFF00u) | (uint32_t)alpha;
}

typedef struct pt_app pt_app;
typedef struct pt_window pt_window;
typedef struct pt_widget pt_widget;

typedef enum pt_align {
    pt_align_auto = 0,
    pt_align_start,
    pt_align_center,
    pt_align_end,
    pt_align_stretch
} pt_align;

typedef enum pt_direction {
    pt_direction_row = 0,
    pt_direction_column
} pt_direction;

/* Interface direction (since 2.0). This is a presentation property: it orders
 * row children, resolves start/end alignment, padding and margins and selects
 * mirrored chrome. It never changes UTF-8 byte offsets or accessibility
 * character offsets. The zero value inherits from the nearest ancestor, then
 * the window, then the application, then the compiled LTR default; a widget
 * that never sets it keeps 1.x behaviour. pt_direction above is unrelated: it
 * is the row/column layout axis. */
typedef enum pt_ui_direction {
    pt_ui_direction_inherit = 0,
    pt_ui_direction_ltr,
    pt_ui_direction_rtl
} pt_ui_direction;

/* Paragraph text direction (since 2.0). auto resolves each paragraph from its
 * first strong character (UBA P2/P3); an empty or neutral-only paragraph falls
 * back to the resolved UI direction. The value is stored separately from the UI
 * direction so an LTR editor can live inside an RTL shell. The zero value
 * inherits from the nearest ancestor; the compiled default is auto. */
typedef enum pt_text_direction {
    pt_text_direction_auto = 0,
    pt_text_direction_ltr,
    pt_text_direction_rtl
} pt_text_direction;

/* Caret movement policy (since 2.1). The zero value inherits: a widget takes
 * the nearest explicit ancestor's value, then the window, then the application,
 * then visual. visual moves Left/Right to the visually adjacent cluster
 * boundary and Home/End to the visual line ends; logical moves Left/Right by
 * UTF-8 character boundary and Home/End to the logical line ends. Vertical
 * movement, Shift selection, Backspace/Delete and the IME caret rectangle are
 * unchanged in both modes. The policy affects the next key event only: it never
 * changes layout, stored bytes or accessibility character offsets. */
typedef enum pt_caret_movement {
    pt_caret_movement_inherit = 0,
    pt_caret_movement_visual,
    pt_caret_movement_logical
} pt_caret_movement;

/* Maps a locale name to a default interface direction (since 2.1). This is a
 * bounded, ICU-free lookup: the language and script subtags of a BCP-47 or
 * POSIX locale are matched against a conservative RTL table; everything else,
 * including NULL, empty, "C" and "POSIX", returns pt_ui_direction_ltr. It never
 * returns inherit and never reads the process environment. Thread-safe: it
 * touches caller-owned data only. */
pt_api pt_ui_direction pt_locale_ui_direction(const char *locale);

typedef enum pt_justify {
    pt_justify_start = 0,
    pt_justify_center,
    pt_justify_end,
    pt_justify_space_between,
    pt_justify_space_around,
    pt_justify_space_evenly
} pt_justify;

typedef enum pt_text_align {
    pt_text_align_start = 0,
    pt_text_align_center,
    pt_text_align_end
} pt_text_align;

typedef enum pt_font_weight {
    pt_font_weight_regular = 400,
    pt_font_weight_medium = 500,
    pt_font_weight_semibold = 600,
    pt_font_weight_bold = 700
} pt_font_weight;

typedef enum pt_font_slant {
    pt_font_slant_upright = 0,
    pt_font_slant_italic,
    pt_font_slant_oblique
} pt_font_slant;

typedef enum pt_text_span_flag {
    pt_text_span_underline = 1u << 0,
    pt_text_span_has_weight = 1u << 1,
    pt_text_span_has_slant = 1u << 2,
    pt_text_span_has_color = 1u << 3,
    pt_text_span_has_family = 1u << 4,
    pt_text_span_has_size = 1u << 5
} pt_text_span_flag;

/* A styled range over the UTF-8 text of a label or text area. Offsets and
 * lengths are bytes, clamped to the text. Spans may overlap; the last span
 * covering a byte wins. Unset attributes inherit from the widget. family is a
 * NUL-terminated Fontconfig family name, copied on set; size is in logical
 * pixels and must be positive to take effect. */
typedef struct pt_text_span {
    uint32_t start;
    uint32_t length;
    pt_font_weight weight;
    pt_font_slant slant;
    pt_color color;
    uint32_t flags;
    char family[64];
    float size;
} pt_text_span;

/* Vertical placement of an inline image relative to the text baseline. The zero
 * value is the default: the image's bottom edge sits on the baseline. middle
 * centres the image on the baseline and extends the line above and below. */
typedef enum pt_text_image_align {
    pt_text_image_align_baseline = 0,
    pt_text_image_align_middle
} pt_text_image_align;

struct pt_icon_image;

/* One inline image run over a U+FFFC object replacement character. start is the
 * byte offset of the three-byte EF BF BC sequence in the widget text; the
 * placeholder is inserted by the caller or by pt_text_area_insert_image.
 * width and height are logical pixels; <= 0 or non-finite means intrinsic size
 * (one positive dimension preserves the aspect ratio). alt is a NUL-terminated
 * UTF-8 string copied into the descriptor and truncated at 127 bytes; it is
 * what a copied selection and AT-SPI expose instead of U+FFFC. image is
 * reference counted: the setter takes a reference and releases it on replace,
 * clear, text replacement or widget destruction. A getter returns the handle
 * borrowed from the widget. */
typedef struct pt_text_image {
    uint32_t start;
    struct pt_icon_image *image;
    float width;
    float height;
    pt_text_image_align align;
    char alt[128];
} pt_text_image;

typedef enum pt_color_scheme {
    pt_color_scheme_dark = 0,
    pt_color_scheme_light
} pt_color_scheme;

typedef enum pt_key_modifier {
    pt_key_modifier_shift = 1u << 0,
    pt_key_modifier_ctrl = 1u << 1,
    pt_key_modifier_alt = 1u << 2,
    pt_key_modifier_logo = 1u << 3
} pt_key_modifier;

/* Return false from a close callback to prevent the close. */
typedef bool (*pt_close_callback)(pt_window *window, void *user_data);
typedef void (*pt_resize_callback)(pt_window *window, int width, int height, void *user_data);
typedef bool (*pt_key_callback)(pt_window *window, uint32_t key, uint32_t modifiers,
                                void *user_data);
typedef void (*pt_timer_callback)(pt_app *app, void *user_data);
typedef void (*pt_click_callback)(pt_widget *widget, void *user_data);
typedef void (*pt_text_callback)(pt_widget *widget, const char *text, void *user_data);
typedef void (*pt_value_callback)(pt_widget *widget, float value, void *user_data);
typedef void (*pt_index_callback)(pt_widget *widget, uint32_t index, void *user_data);

/* -1 (or any negative value) requests content-based sizing. */
#define pt_auto (-1.0f)

/* Returns the last error for the current thread. The string is owned by Pt and
 * remains valid until the next Pt call on the same thread. Since 1.0. */
pt_api const char *pt_get_last_error(void);

/* Frees memory returned by Pt to the caller, such as clipboard text. Since 1.0. */
pt_api void pt_free(void *pointer);

#ifdef __cplusplus
}
#endif

#endif
