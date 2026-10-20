/*
 * FreeType rasterization, HarfBuzz shaping and Fontconfig matching.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 *
 * All font metrics exposed internally are device pixels. Text drawing
 * functions take canvas coordinates in logical units and scale internally.
 */

#include "internal.h"

#include <fontconfig/fontconfig.h>
#include <hb-ft.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FT_Library font_library;
static pt_font *font_list;
static pt_font **font_table;
static size_t font_table_capacity;
static size_t font_table_size;

static size_t glyph_hash(uint32_t key, size_t capacity) {
    return ((size_t)key * 2654435761u) & (capacity - 1);
}

static void glyph_cache_grow(pt_font *font) {
    size_t new_capacity = font->glyphs.capacity == 0 ? 128 : font->glyphs.capacity * 2;
    pt_glyph *old_slots = font->glyphs.slots;
    size_t old_capacity = font->glyphs.capacity;
    pt_glyph *new_slots = pt_alloc_zeroed(new_capacity, sizeof(pt_glyph));
    for (size_t i = 0; i < old_capacity; ++i) {
        if (!old_slots[i].used) {
            continue;
        }
        uint32_t key = old_slots[i].glyph_id + 1u;
        size_t index = glyph_hash(key, new_capacity);
        while (new_slots[index].used) {
            index = (index + 1) & (new_capacity - 1);
        }
        new_slots[index] = old_slots[i];
    }
    free(old_slots);
    font->glyphs.slots = new_slots;
    font->glyphs.capacity = new_capacity;
}

static void glyph_clear(pt_glyph *glyph) {
    free(glyph->coverage);
    memset(glyph, 0, sizeof(*glyph));
}

#define shape_cache_max_entries 256u
#define shape_cache_max_bytes (256u * 1024u)
#define shape_cache_max_text 8192u

static void shape_cache_clear(pt_font *font) {
    pt_shape_entry *entry = font->shapes.newest;
    while (entry) {
        pt_shape_entry *older = entry->older;
        hb_buffer_destroy(entry->buffer);
        free(entry->text);
        free(entry);
        entry = older;
    }
    memset(&font->shapes, 0, sizeof(font->shapes));
}

static void shape_cache_unlink(pt_font *font, pt_shape_entry *entry) {
    if (entry->newer) {
        entry->newer->older = entry->older;
    } else {
        font->shapes.newest = entry->older;
    }
    if (entry->older) {
        entry->older->newer = entry->newer;
    } else {
        font->shapes.oldest = entry->newer;
    }
    entry->newer = NULL;
    entry->older = NULL;
}

static void shape_cache_evict_oldest(pt_font *font) {
    pt_shape_entry *entry = font->shapes.oldest;
    if (!entry) {
        return;
    }
    shape_cache_unlink(font, entry);
    font->shapes.count--;
    font->shapes.bytes -= entry->len;
    hb_buffer_destroy(entry->buffer);
    free(entry->text);
    free(entry);
}

static void shape_cache_insert(pt_font *font, pt_shape_entry *entry) {
    entry->older = font->shapes.newest;
    if (font->shapes.newest) {
        font->shapes.newest->newer = entry;
    }
    font->shapes.newest = entry;
    if (!font->shapes.oldest) {
        font->shapes.oldest = entry;
    }
    font->shapes.count++;
    font->shapes.bytes += entry->len;
    while (font->shapes.count > shape_cache_max_entries ||
           font->shapes.bytes > shape_cache_max_bytes) {
        shape_cache_evict_oldest(font);
    }
}

static void font_destroy(pt_font *font) {
    if (!font) {
        return;
    }
    shape_cache_clear(font);
    if (font->glyphs.slots) {
        for (size_t i = 0; i < font->glyphs.capacity; ++i) {
            if (font->glyphs.slots[i].used) {
                glyph_clear(&font->glyphs.slots[i]);
            }
        }
        free(font->glyphs.slots);
    }
    if (font->hb_font) {
        hb_font_destroy(font->hb_font);
    }
    if (font->face) {
        FT_Done_Face(font->face);
    }
    free(font->family);
    free(font);
}

static uint64_t font_key_hash(const char *family, int weight, int slant, int size_26_6) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char *cursor = (const unsigned char *)family; *cursor; ++cursor) {
        hash ^= *cursor;
        hash *= 1099511628211ull;
    }
    hash ^= (uint64_t)(uint32_t)weight << 1;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)(uint32_t)slant << 2;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)(uint32_t)size_26_6 << 3;
    return hash;
}

static void font_table_rehash(size_t capacity) {
    pt_font **table = pt_alloc_zeroed(capacity, sizeof(*table));
    if (font_table) {
        for (size_t i = 0; i < font_table_capacity; ++i) {
            pt_font *font = font_table[i];
            if (!font) {
                continue;
            }
            uint64_t hash = font_key_hash(font->family, font->weight, font->slant,
                                          (int)lroundf(font->pixel_size * 64.0f));
            size_t slot = (size_t)hash & (capacity - 1);
            while (table[slot]) {
                slot = (slot + 1) & (capacity - 1);
            }
            table[slot] = font;
        }
    }
    free(font_table);
    font_table = table;
    font_table_capacity = capacity;
}

void pt_font_manager_shutdown(void) {
    pt_font *font = font_list;
    while (font) {
        pt_font *next = font->next;
        font_destroy(font);
        font = next;
    }
    font_list = NULL;
    free(font_table);
    font_table = NULL;
    font_table_capacity = 0;
    font_table_size = 0;
    if (font_library) {
        FT_Done_FreeType(font_library);
        font_library = NULL;
    }
}

static bool ensure_library(void) {
    if (font_library) {
        return true;
    }
    if (!FcInit()) {
        pt_set_error("fontconfig initialization failed");
        return false;
    }
    if (FT_Init_FreeType(&font_library) != 0) {
        pt_set_error("FreeType initialization failed");
        return false;
    }
    return true;
}

static int fontconfig_weight(int weight) {
    if (weight >= 700) {
        return FC_WEIGHT_BOLD;
    }
    if (weight >= 600) {
        return FC_WEIGHT_DEMIBOLD;
    }
    if (weight >= 500) {
        return FC_WEIGHT_MEDIUM;
    }
    if (weight <= 300) {
        return FC_WEIGHT_LIGHT;
    }
    return FC_WEIGHT_REGULAR;
}

static bool load_face(const char *family, int weight, int slant, int size_26_6,
                      FT_Face *out_face) {
    FcPattern *pattern = FcPatternCreate();
    if (!pattern) {
        return false;
    }
    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)(family && *family ? family : "sans-serif"));
    FcPatternAddInteger(pattern, FC_WEIGHT, fontconfig_weight(weight));
    FcPatternAddInteger(pattern, FC_SLANT,
                        slant == pt_font_slant_italic
                            ? FC_SLANT_ITALIC
                            : (slant == pt_font_slant_oblique ? FC_SLANT_OBLIQUE
                                                              : FC_SLANT_ROMAN));
    FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)size_26_6 / 64.0);
    FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_SLIGHT);
    FcPatternAddBool(pattern, FC_ANTIALIAS, FcTrue);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(NULL, pattern, &result);
    bool loaded = false;
    if (match && result == FcResultMatch) {
        FcChar8 *file = NULL;
        int index = 0;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch &&
            FcPatternGetInteger(match, FC_INDEX, 0, &index) == FcResultMatch) {
            if (FT_New_Face(font_library, (const char *)file, index, out_face) == 0) {
                FT_Select_Charmap(*out_face, FT_ENCODING_UNICODE);
                if (FT_Set_Char_Size(*out_face, 0, (FT_F26Dot6)size_26_6, 72, 72) == 0) {
                    loaded = true;
                } else {
                    FT_Done_Face(*out_face);
                    *out_face = NULL;
                }
            }
        }
    }
    if (!loaded) {
        pt_set_error("no usable font found for '%s'", family ? family : "sans-serif");
    }
    if (match) {
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return loaded;
}

pt_font *pt_font_acquire(const char *family, int weight, pt_font_slant slant,
                         float pixel_size) {
    if (!ensure_library()) {
        return NULL;
    }
    if (!isfinite(pixel_size) || pixel_size < 1.0f) {
        pixel_size = 1.0f;
    }
    if (slant < pt_font_slant_upright || slant > pt_font_slant_oblique) {
        slant = pt_font_slant_upright;
    }
    int size_26_6 = (int)lroundf(pixel_size * 64.0f);
    const char *name = family && *family ? family : "sans-serif";
    if (font_table_capacity == 0) {
        font_table_rehash(64);
    }
    uint64_t hash = font_key_hash(name, weight, (int)slant, size_26_6);
    size_t slot = (size_t)hash & (font_table_capacity - 1);
    while (font_table[slot]) {
        pt_font *font = font_table[slot];
        if (font->weight == weight && font->slant == (int)slant &&
            (int)lroundf(font->pixel_size * 64.0f) == size_26_6 &&
            strcmp(font->family, name) == 0) {
            return font;
        }
        slot = (slot + 1) & (font_table_capacity - 1);
    }

    pt_font *font = pt_alloc_zeroed(1, sizeof(*font));
    font->family = pt_strdup(name);
    font->weight = weight;
    font->slant = (int)slant;
    font->pixel_size = (float)size_26_6 / 64.0f;
    if (!load_face(name, weight, (int)slant, size_26_6, &font->face)) {
        font_destroy(font);
        return NULL;
    }
    font->hb_font = hb_ft_font_create_referenced(font->face);
    if (!font->hb_font) {
        font_destroy(font);
        pt_set_error("HarfBuzz font creation failed");
        return NULL;
    }
    hb_ft_font_set_load_flags(font->hb_font, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
    hb_font_set_scale(font->hb_font, size_26_6, size_26_6);
    font->ascent = (float)font->face->size->metrics.ascender / 64.0f;
    font->descent = (float)-font->face->size->metrics.descender / 64.0f;
    font->line_height = font->ascent + font->descent;
    if (font->line_height < 1.0f) {
        font->line_height = font->pixel_size;
    }

    font->next = font_list;
    font_list = font;
    if ((font_table_size + 1) * 10 >= font_table_capacity * 7) {
        font_table_rehash(font_table_capacity * 2);
        hash = font_key_hash(name, weight, (int)slant, size_26_6);
        slot = (size_t)hash & (font_table_capacity - 1);
        while (font_table[slot]) {
            slot = (slot + 1) & (font_table_capacity - 1);
        }
    }
    font_table[slot] = font;
    ++font_table_size;
    return font;
}

static uint64_t shape_hash(const char *text, size_t len) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 1099511628211ull;
    }
    return hash ^ ((uint64_t)len << 32);
}

static pt_shape_entry *shape_cache_lookup(pt_font *font, const char *text, size_t len,
                                          uint64_t hash) {
    pt_shape_entry *entry = font->shapes.newest;
    while (entry) {
        if (entry->hash == hash && entry->len == len &&
            memcmp(entry->text, text, len) == 0) {
            if (entry != font->shapes.newest) {
                shape_cache_unlink(font, entry);
                font->shapes.count--;
                font->shapes.bytes -= entry->len;
                shape_cache_insert(font, entry);
            }
            return entry;
        }
        entry = entry->older;
    }
    return NULL;
}

static hb_buffer_t *shape_text(pt_font *font, const char *text, size_t len) {
    uint64_t hash = shape_hash(text, len);
    pt_shape_entry *entry = shape_cache_lookup(font, text, len, hash);
    if (entry) {
        return entry->buffer;
    }
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text, (int)len, 0, (int)len);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font->hb_font, buffer, NULL, 0);
    if (len > shape_cache_max_text) {
        return buffer;
    }
    entry = pt_alloc_zeroed(1, sizeof(*entry));
    entry->hash = hash;
    entry->text = pt_alloc(len + 1);
    memcpy(entry->text, text, len);
    entry->text[len] = '\0';
    entry->len = len;
    entry->buffer = buffer;
    shape_cache_insert(font, entry);
    return buffer;
}

float pt_font_measure(const pt_font *font, const char *text, size_t len) {
    if (!font || !text || len == 0) {
        return 0.0f;
    }
    hb_buffer_t *buffer = shape_text((pt_font *)font, text, len);
    unsigned int glyph_count = 0;
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
    int64_t total = 0;
    for (unsigned int i = 0; i < glyph_count; ++i) {
        total += positions[i].x_advance;
    }
    if (len > shape_cache_max_text) {
        hb_buffer_destroy(buffer);
    }
    return (float)total / 64.0f;
}

static pt_glyph *glyph_lookup(pt_font *font, uint32_t glyph_id) {
    if (font->glyphs.capacity == 0) {
        glyph_cache_grow(font);
    }
    uint32_t key = glyph_id + 1u;
    size_t index = glyph_hash(key, font->glyphs.capacity);
    for (;;) {
        if (!font->glyphs.slots[index].used) {
            return &font->glyphs.slots[index];
        }
        if (font->glyphs.slots[index].glyph_id == glyph_id) {
            return &font->glyphs.slots[index];
        }
        index = (index + 1) & (font->glyphs.capacity - 1);
    }
}

static bool glyph_load(pt_font *font, uint32_t glyph_id, pt_glyph *glyph) {
    if (FT_Load_Glyph(font->face, glyph_id,
                      FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) != 0) {
        return false;
    }
    if (font->face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        if (FT_Render_Glyph(font->face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            return false;
        }
    }
    FT_GlyphSlot slot = font->face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;
    if (bitmap->width == 0 || bitmap->rows == 0) {
        glyph->glyph_id = glyph_id;
        glyph->used = true;
        return true;
    }
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY && bitmap->pixel_mode != FT_PIXEL_MODE_MONO) {
        glyph->glyph_id = glyph_id;
        glyph->used = true;
        return true;
    }
    uint8_t *coverage = pt_alloc_zeroed((size_t)bitmap->width, (size_t)bitmap->rows);
    int pitch = bitmap->pitch;
    if (pitch < 0) {
        pitch = -pitch;
    }
    for (unsigned int row = 0; row < bitmap->rows; ++row) {
        const uint8_t *src = bitmap->buffer + (size_t)row * (size_t)pitch;
        uint8_t *dst = coverage + (size_t)row * (size_t)bitmap->width;
        if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
            for (unsigned int column = 0; column < bitmap->width; ++column) {
                uint8_t bit = (uint8_t)((src[column >> 3] >> (7 - (column & 7))) & 1u);
                dst[column] = bit ? 255 : 0;
            }
        } else {
            memcpy(dst, src, bitmap->width);
        }
    }
    glyph->coverage = coverage;
    glyph->width = (int)bitmap->width;
    glyph->height = (int)bitmap->rows;
    glyph->bearing_x = slot->bitmap_left;
    glyph->bearing_y = slot->bitmap_top;
    glyph->advance_x = slot->advance.x;
    glyph->glyph_id = glyph_id;
    glyph->used = true;
    return true;
}

static pt_glyph *glyph_get(pt_font *font, uint32_t glyph_id) {
    if (font->glyphs.count * 10 >= font->glyphs.capacity * 7) {
        glyph_cache_grow(font);
    }
    pt_glyph *glyph = glyph_lookup(font, glyph_id);
    if (glyph->used) {
        return glyph;
    }
    if (!glyph_load(font, glyph_id, glyph)) {
        glyph->glyph_id = glyph_id;
        glyph->used = true;
    }
    ++font->glyphs.count;
    return glyph;
}

static size_t trim_ascii_spaces(const char *text, size_t start, size_t end) {
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    return end;
}

static size_t skip_ascii_spaces(const char *text, size_t pos, size_t end) {
    while (pos < end && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    return pos;
}

static bool cp_is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == 0x00A0u;
}

static bool cp_allows_break_after(uint32_t cp) {
    return cp_is_space(cp) || cp == '-' || cp == '/' || cp == 0x2014u || pt_utf8_is_cjk(cp);
}

static size_t next_unit_end(const char *text, size_t start, size_t end) {
    size_t cursor = start;
    while (cursor < end) {
        size_t next = 0;
        uint32_t cp = pt_utf8_decode(text, end, cursor, &next);
        if (next <= cursor) {
            next = cursor + 1;
        }
        if (cp_is_space(cp)) {
            cursor = next;
            while (cursor < end) {
                size_t following = 0;
                uint32_t space_cp = pt_utf8_decode(text, end, cursor, &following);
                if (!cp_is_space(space_cp)) {
                    break;
                }
                cursor = following;
            }
            return cursor;
        }
        if (cp_allows_break_after(cp)) {
            return next;
        }
        cursor = next;
    }
    return end;
}

typedef struct line_builder {
    pt_text_line *lines;
    size_t count;
    size_t capacity;
} line_builder;

static void line_push(line_builder *builder, size_t start, size_t end) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity == 0 ? 16 : builder->capacity * 2;
        builder->lines = pt_realloc_mem(builder->lines, builder->capacity * sizeof(pt_text_line));
    }
    builder->lines[builder->count++] = (pt_text_line){start, end};
}

static void wrap_paragraph(const pt_font *font, const char *text, size_t start, size_t end,
                           float max_width, line_builder *builder) {
    if (!font || !text) {
        line_push(builder, start, start);
        return;
    }
    if (end <= start) {
        line_push(builder, start, start);
        return;
    }
    if (max_width <= 0.0f || !isfinite(max_width)) {
        line_push(builder, start, trim_ascii_spaces(text, start, end));
        return;
    }
    size_t line_start = skip_ascii_spaces(text, start, end);
    size_t cursor = line_start;
    size_t line_end = line_start;
    bool has_content = false;

    while (cursor < end) {
        size_t unit_end = next_unit_end(text, cursor, end);
        if (unit_end <= cursor) {
            unit_end = cursor + 1;
        }
        size_t candidate_end = trim_ascii_spaces(text, line_start, unit_end);
        float width = pt_font_measure(font, text + line_start, candidate_end - line_start);
        if (width <= max_width || !has_content) {
            if (width > max_width && !has_content && candidate_end > line_start) {
                size_t fit_end = line_start;
                size_t scan = line_start;
                while (scan < candidate_end) {
                    size_t next = pt_utf8_next_boundary(text, candidate_end, scan);
                    if (next <= scan) {
                        next = scan + 1;
                    }
                    float prefix = pt_font_measure(font, text + line_start, next - line_start);
                    if (prefix > max_width && fit_end > line_start) {
                        break;
                    }
                    fit_end = next;
                    scan = next;
                }
                if (fit_end <= line_start) {
                    fit_end = pt_utf8_next_boundary(text, candidate_end, line_start);
                }
                line_push(builder, line_start, fit_end);
                line_start = fit_end;
                cursor = fit_end;
                line_end = line_start;
                has_content = false;
                continue;
            }
            line_end = candidate_end;
            has_content = has_content || candidate_end > line_start;
            cursor = unit_end;
            if (cursor >= end) {
                break;
            }
            continue;
        }

        if (line_end > line_start) {
            line_push(builder, line_start, line_end);
            line_start = skip_ascii_spaces(text, cursor, end);
            cursor = line_start;
            line_end = line_start;
            has_content = false;
            continue;
        }

        size_t fit_end = line_start;
        size_t scan = line_start;
        while (scan < unit_end) {
            size_t next = pt_utf8_next_boundary(text, unit_end, scan);
            if (next <= scan) {
                next = scan + 1;
            }
            float prefix = pt_font_measure(font, text + line_start, next - line_start);
            if (prefix > max_width && fit_end > line_start) {
                break;
            }
            fit_end = next;
            scan = next;
        }
        if (fit_end <= line_start) {
            fit_end = pt_utf8_next_boundary(text, end, line_start);
        }
        line_push(builder, line_start, fit_end);
        line_start = fit_end;
        cursor = fit_end;
        line_end = line_start;
        has_content = false;
    }
    if (has_content || line_end >= line_start) {
        if (line_end > line_start || has_content || builder->count == 0 ||
            builder->lines[builder->count - 1].end != line_start) {
            line_push(builder, line_start, line_end);
        }
    }
}

int pt_font_wrap_lines(const pt_font *font, const char *text, size_t len, float max_width,
                       pt_text_line **out_lines) {
    line_builder builder = {0};
    if (!text) {
        len = 0;
    }
    if (!font || len == 0) {
        line_push(&builder, 0, 0);
        *out_lines = builder.lines;
        return (int)builder.count;
    }
    size_t paragraph_start = 0;
    for (;;) {
        size_t paragraph_end = paragraph_start;
        while (paragraph_end < len && text[paragraph_end] != '\n') {
            ++paragraph_end;
        }
        bool drop_carriage_return = paragraph_end > paragraph_start &&
                                    text[paragraph_end - 1] == '\r';
        size_t content_end = drop_carriage_return ? paragraph_end - 1 : paragraph_end;
        wrap_paragraph(font, text, paragraph_start, content_end, max_width, &builder);
        if (paragraph_end >= len) {
            break;
        }
        paragraph_start = paragraph_end + 1;
        if (paragraph_start == len) {
            line_push(&builder, len, len);
            break;
        }
    }
    *out_lines = builder.lines;
    return (int)builder.count;
}

float pt_font_wrapped_height(const pt_font *font, const char *text, float max_width) {
    if (!font) {
        return 0.0f;
    }
    pt_text_line *lines = NULL;
    int count = pt_font_wrap_lines(font, text ? text : "", text ? strlen(text) : 0, max_width,
                                   &lines);
    free(lines);
    return (float)count * font->line_height;
}

static char *ellipsize_text(const pt_font *font, const char *text, size_t len, float max_width,
                            size_t *out_len, float *out_width) {
    static const char ellipsis[] = "\xE2\x80\xA6";
    float ellipsis_width = pt_font_measure(font, ellipsis, sizeof(ellipsis) - 1);
    if (ellipsis_width > max_width) {
        *out_len = 0;
        *out_width = 0.0f;
        return NULL;
    }
    size_t candidate = len;
    char *buffer = pt_alloc(len + sizeof(ellipsis));
    while (candidate > 0) {
        candidate = pt_utf8_prev_boundary(text, len, candidate);
        memcpy(buffer, text, candidate);
        memcpy(buffer + candidate, ellipsis, sizeof(ellipsis) - 1);
        float width = pt_font_measure(font, buffer, candidate + sizeof(ellipsis) - 1);
        if (width <= max_width) {
            *out_len = candidate + sizeof(ellipsis) - 1;
            *out_width = width;
            buffer[*out_len] = '\0';
            return buffer;
        }
    }
    free(buffer);
    *out_len = 0;
    *out_width = 0.0f;
    return NULL;
}

void pt_canvas_draw_text_line(pt_canvas *canvas, const pt_font *font, const char *text,
                              size_t len, float x, float baseline, float max_width,
                              pt_text_align align, pt_color color, bool ellipsis) {
    if (!canvas || !font || !text || len == 0 || (color & 0xFFu) == 0) {
        return;
    }
    const char *draw_text = text;
    size_t draw_len = len;
    char *ellipsized = NULL;
    float total_width = pt_font_measure(font, text, len);
    float max_width_px = max_width > 0.0f ? max_width * canvas->scale : 0.0f;
    if (max_width_px > 0.0f && total_width > max_width_px) {
        if (ellipsis) {
            size_t new_len = 0;
            float new_width = 0.0f;
            ellipsized = ellipsize_text(font, text, len, max_width_px, &new_len, &new_width);
            draw_text = ellipsized;
            draw_len = new_len;
            total_width = new_width;
        } else {
            return;
        }
    }
    if (draw_len == 0) {
        free(ellipsized);
        return;
    }
    if (max_width > 0.0f) {
        float total_logical = total_width / canvas->scale;
        if (align == pt_text_align_center) {
            x += (max_width - total_logical) * 0.5f;
        } else if (align == pt_text_align_end) {
            x += max_width - total_logical;
        }
    }

    hb_buffer_t *buffer = shape_text((pt_font *)font, draw_text, draw_len);
    unsigned int glyph_count = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
    float pen_x = x * canvas->scale;
    float pen_y = baseline * canvas->scale;
    for (unsigned int i = 0; i < glyph_count; ++i) {
        pt_glyph *glyph = glyph_get((pt_font *)font, infos[i].codepoint);
        float glyph_x = pen_x + (float)positions[i].x_offset / 64.0f;
        float glyph_y = pen_y - (float)positions[i].y_offset / 64.0f;
        if (glyph && glyph->coverage) {
            int draw_x = (int)lroundf(glyph_x) + glyph->bearing_x;
            int draw_y = (int)lroundf(glyph_y) - glyph->bearing_y;
            pt_canvas_blend_mask(canvas, draw_x, draw_y, glyph->coverage, glyph->width,
                                 glyph->height, glyph->width, color, 1.0f);
        }
        pen_x += (float)positions[i].x_advance / 64.0f;
        pen_y -= (float)positions[i].y_advance / 64.0f;
    }
    if (draw_len > shape_cache_max_text) {
        hb_buffer_destroy(buffer);
    }
    free(ellipsized);
}

void pt_canvas_draw_text_wrapped(pt_canvas *canvas, const pt_font *font, const char *text,
                                 float x, float top, float max_width, float max_height,
                                 pt_text_align align, pt_color color) {
    if (!canvas || !font || !text) {
        return;
    }
    size_t len = strlen(text);
    pt_text_line *lines = NULL;
    int count = pt_font_wrap_lines(font, text, len, max_width * canvas->scale, &lines);
    float ascent = font->ascent / canvas->scale;
    float line_height = font->line_height / canvas->scale;
    for (int i = 0; i < count; ++i) {
        float line_top = top + (float)i * line_height;
        if (max_height > 0.0f && line_top + line_height > top + max_height + 0.01f) {
            break;
        }
        size_t start = lines[i].start;
        size_t end = lines[i].end;
        while (start < end && (text[start] == ' ' || text[start] == '\t')) {
            ++start;
        }
        if (end > start) {
            pt_canvas_draw_text_line(canvas, font, text + start, end - start, x,
                                     line_top + ascent, max_width, align, color, false);
        }
    }
    free(lines);
}

