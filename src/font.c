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

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FT_Library font_library;
static pt_font *font_list;
static pt_font **font_table;
static size_t font_table_capacity;
static size_t font_table_size;

/* Installed fallback configuration. The font cache is process-wide and keyed by
 * family/weight/slant/size, so the chain is too: one install applies to every
 * face acquired afterwards. Shape and per-codepoint caches carry the generation
 * so a change invalidates them lazily without freeing memory a caller holds. */
typedef struct pt_font_chain {
    char **families;
    int count;
} pt_font_chain;

typedef struct pt_font_override {
    char *family;
    pt_font_chain chain;
    uint64_t stamp;
} pt_font_override;

static pt_font_chain fallback_default;
static pt_font_override fallback_overrides[PT_FONT_FALLBACK_MAX_OVERRIDES];
static int fallback_override_count;
static uint64_t fallback_stamp;
static uint64_t fallback_generation = 1;

static void chain_clear(pt_font_chain *chain) {
    if (!chain) {
        return;
    }
    for (int i = 0; i < chain->count; ++i) {
        free(chain->families[i]);
    }
    free(chain->families);
    memset(chain, 0, sizeof(*chain));
}

static void chain_assign(pt_font_chain *chain, const char *const *families, int count) {
    char **names = NULL;
    int used = 0;
    if (families && count > 0) {
        if (count > PT_FONT_FALLBACK_MAX_FAMILIES) {
            count = PT_FONT_FALLBACK_MAX_FAMILIES;
        }
        names = pt_alloc_zeroed((size_t)count, sizeof(*names));
        for (int i = 0; i < count; ++i) {
            if (families[i] && families[i][0]) {
                names[used++] = pt_strdup(families[i]);
            }
        }
        if (used == 0) {
            free(names);
            names = NULL;
        }
    }
    chain_clear(chain);
    chain->families = names;
    chain->count = used;
}

static const pt_font_chain *chain_for_family(const char *family) {
    if (family) {
        for (int i = 0; i < fallback_override_count; ++i) {
            if (strcmp(fallback_overrides[i].family, family) == 0) {
                return &fallback_overrides[i].chain;
            }
        }
    }
    if (fallback_default.count > 0) {
        return &fallback_default;
    }
    return NULL;
}

static void fallback_generation_bump(void) {
    ++fallback_generation;
    if (fallback_generation == 0) {
        fallback_generation = 1;
    }
}

/* Repaints the windows of the app that installed the change. The configuration
 * is shared, so a second application in the same process must request its own
 * redraw after installing a different chain. */
static void fallback_repaint(pt_app *app) {
    if (!app) {
        return;
    }
    for (pt_window *window = app->first_window; window; window = window->next) {
        pt_window_invalidate_layout(window, NULL);
        window->needs_draw = true;
        pt_window_damage_all(window);
        pt_platform_window_schedule_draw(window);
    }
}

static char *family_dup(const char *family) {
    return family && family[0] ? pt_strdup(family) : NULL;
}

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
/* Per-face codepoint resolution cache. Fixed, so a string of thousands of
 * distinct codepoints cannot grow it; beyond the load factor resolution still
 * happens, just uncached. */
#define fallback_cache_capacity 512u

static void shaped_run_clear(pt_shaped_run *run) {
    if (!run) {
        return;
    }
    if (run->buffer) {
        hb_buffer_destroy(run->buffer);
    }
    memset(run, 0, sizeof(*run));
}

static void shape_entry_clear(pt_shape_entry *entry) {
    if (!entry) {
        return;
    }
    for (unsigned int i = 0; i < entry->run_count; ++i) {
        shaped_run_clear(&entry->runs[i]);
    }
    free(entry->runs);
    free(entry->text);
    free(entry);
}

static void shape_cache_clear(pt_font *font) {
    pt_shape_entry *entry = font->shapes.newest;
    while (entry) {
        pt_shape_entry *older = entry->older;
        shape_entry_clear(entry);
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
    shape_entry_clear(entry);
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

static void fallback_cache_clear(pt_font *font) {
    free(font->fallbacks.slots);
    memset(&font->fallbacks, 0, sizeof(font->fallbacks));
}

static void font_destroy(pt_font *font) {
    if (!font) {
        return;
    }
    shape_cache_clear(font);
    fallback_cache_clear(font);
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
    chain_clear(&fallback_default);
    for (int i = 0; i < fallback_override_count; ++i) {
        free(fallback_overrides[i].family);
        chain_clear(&fallback_overrides[i].chain);
    }
    memset(fallback_overrides, 0, sizeof(fallback_overrides));
    fallback_override_count = 0;
    fallback_stamp = 0;
    fallback_generation_bump();
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
                      FT_Face *out_face, bool report_error) {
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
    if (!loaded && report_error) {
        pt_set_error("no usable font found for '%s'", family ? family : "sans-serif");
    }
    if (match) {
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return loaded;
}

static pt_font *pt_font_acquire_internal(const char *family, int weight, pt_font_slant slant,
                                         float pixel_size, bool report_error) {
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
    if (!load_face(name, weight, (int)slant, size_26_6, &font->face, report_error)) {
        font_destroy(font);
        return NULL;
    }
    font->hb_font = hb_ft_font_create_referenced(font->face);
    if (!font->hb_font) {
        font_destroy(font);
        if (report_error) {
            pt_set_error("HarfBuzz font creation failed");
        }
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

pt_font *pt_font_acquire(const char *family, int weight, pt_font_slant slant,
                         float pixel_size) {
    return pt_font_acquire_internal(family, weight, slant, pixel_size, true);
}

static uint64_t shape_hash(const char *text, size_t len, uint8_t direction_key) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 1099511628211ull;
    }
    hash ^= ((uint64_t)len << 32);
    hash ^= (uint64_t)direction_key << 56;
    hash ^= fallback_generation * 1099511628211ull;
    return hash;
}

static pt_shape_entry *shape_cache_lookup(pt_font *font, const char *text, size_t len,
                                          uint64_t hash, uint8_t direction_key) {
    pt_shape_entry *entry = font->shapes.newest;
    while (entry) {
        if (entry->hash == hash && entry->len == len &&
            entry->direction_key == direction_key &&
            entry->generation == fallback_generation && memcmp(entry->text, text, len) == 0) {
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

bool pt_font_has_glyph(const pt_font *font, uint32_t codepoint) {
    if (!font || !font->face) {
        return false;
    }
    return FT_Get_Char_Index(font->face, codepoint) != 0;
}

/* ---------- fallback resolution ---------- */

static size_t fallback_hash(uint32_t codepoint, size_t capacity) {
    return ((size_t)codepoint * 2654435761u) & (capacity - 1);
}

static void fallback_cache_prepare(pt_font *font) {
    if (font->fallbacks.capacity == 0) {
        font->fallbacks.slots =
            pt_alloc_zeroed(fallback_cache_capacity, sizeof(*font->fallbacks.slots));
        font->fallbacks.capacity = fallback_cache_capacity;
        font->fallbacks.generation = fallback_generation;
        return;
    }
    if (font->fallbacks.generation != fallback_generation) {
        memset(font->fallbacks.slots, 0,
               font->fallbacks.capacity * sizeof(*font->fallbacks.slots));
        font->fallbacks.count = 0;
        font->fallbacks.generation = fallback_generation;
    }
}

static pt_fallback_slot *fallback_cache_find(pt_font *font, uint32_t codepoint) {
    size_t index = fallback_hash(codepoint, font->fallbacks.capacity);
    for (size_t probe = 0; probe < font->fallbacks.capacity; ++probe) {
        pt_fallback_slot *slot = &font->fallbacks.slots[index];
        if (!slot->resolved) {
            return NULL;
        }
        if (slot->codepoint == codepoint) {
            return slot;
        }
        index = (index + 1) & (font->fallbacks.capacity - 1);
    }
    return NULL;
}

static pt_fallback_slot *fallback_cache_insert(pt_font *font, uint32_t codepoint) {
    /* Leave headroom so probe chains stay short; past it resolution is correct
     * but uncached, which keeps the table bounded. */
    if ((font->fallbacks.count + 1) * 10 >= font->fallbacks.capacity * 7) {
        return NULL;
    }
    size_t index = fallback_hash(codepoint, font->fallbacks.capacity);
    for (;;) {
        pt_fallback_slot *slot = &font->fallbacks.slots[index];
        if (!slot->resolved) {
            slot->codepoint = codepoint;
            slot->resolved = true;
            font->fallbacks.count++;
            return slot;
        }
        index = (index + 1) & (font->fallbacks.capacity - 1);
    }
}

/* Fontconfig's own sort order for a character set, used only when no explicit
 * chain is installed. Returns a copied family name, or NULL when no face covers
 * every requested character. */
static char *fontconfig_family_for_charset(FcCharSet *charset, int weight, int slant,
                                           int size_26_6) {
    if (!ensure_library() || !charset) {
        return NULL;
    }
    FcPattern *pattern = FcPatternCreate();
    if (!pattern) {
        return NULL;
    }
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
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
    FcFontSet *set = FcFontSort(NULL, pattern, FcTrue, NULL, &result);
    char *family = NULL;
    if (set) {
        for (int i = 0; i < set->nfont && !family; ++i) {
            FcCharSet *candidate = NULL;
            if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &candidate) !=
                    FcResultMatch ||
                !candidate || !FcCharSetIsSubset(charset, candidate)) {
                continue;
            }
            FcChar8 *name = NULL;
            if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &name) == FcResultMatch &&
                name && name[0]) {
                family = pt_strdup((const char *)name);
            }
        }
        FcFontSetDestroy(set);
    }
    FcPatternDestroy(pattern);
    return family;
}

static char *fontconfig_system_family(int weight, int slant, int size_26_6,
                                      uint32_t codepoint) {
    FcCharSet *charset = FcCharSetCreate();
    if (!charset) {
        return NULL;
    }
    FcCharSetAddChar(charset, codepoint);
    char *family = fontconfig_family_for_charset(charset, weight, slant, size_26_6);
    FcCharSetDestroy(charset);
    return family;
}

static char *fontconfig_system_family_span(int weight, int slant, int size_26_6,
                                           const char *text, size_t start, size_t end,
                                           bool ignore_removed) {
    FcCharSet *charset = FcCharSetCreate();
    if (!charset) {
        return NULL;
    }
    size_t pos = start;
    while (pos < end) {
        size_t next = 0;
        uint32_t codepoint = pt_utf8_decode(text, end, pos, &next);
        if (next <= pos) {
            next = pos + 1;
        }
        if (!(ignore_removed && pt_bidi_is_removed(codepoint))) {
            FcCharSetAddChar(charset, codepoint);
        }
        pos = next;
    }
    char *family = fontconfig_family_for_charset(charset, weight, slant, size_26_6);
    FcCharSetDestroy(charset);
    return family;
}

/* Resolves one codepoint through the primary face, the installed chain (or the
 * Fontconfig sort order when no chain is installed). Returns the face to shape
 * with; `*covered` is false when that face is the primary tofu fallback. */
static pt_font *resolve_face(pt_font *base, uint32_t codepoint, bool *covered) {
    fallback_cache_prepare(base);
    pt_fallback_slot *slot = fallback_cache_find(base, codepoint);
    if (slot) {
        *covered = slot->covered;
        return slot->font ? slot->font : base;
    }

    pt_font *chosen = NULL;
    if (pt_font_has_glyph(base, codepoint)) {
        chosen = base;
    } else {
        const pt_font_chain *chain = chain_for_family(base->family);
        if (chain) {
            for (int i = 0; i < chain->count && !chosen; ++i) {
                if (strcmp(chain->families[i], base->family) == 0) {
                    continue;
                }
                pt_font *candidate =
                    pt_font_acquire_internal(chain->families[i], base->weight,
                                             (pt_font_slant)base->slant, base->pixel_size,
                                             false);
                if (candidate && pt_font_has_glyph(candidate, codepoint)) {
                    chosen = candidate;
                }
            }
        } else {
            int size_26_6 = (int)lroundf(base->pixel_size * 64.0f);
            char *family = fontconfig_system_family(base->weight, base->slant, size_26_6,
                                                     codepoint);
            if (family) {
                pt_font *candidate =
                    pt_font_acquire_internal(family, base->weight, (pt_font_slant)base->slant,
                                             base->pixel_size, false);
                if (candidate && pt_font_has_glyph(candidate, codepoint)) {
                    chosen = candidate;
                }
                free(family);
            }
        }
    }

    *covered = chosen != NULL;
    slot = fallback_cache_insert(base, codepoint);
    if (slot) {
        slot->font = chosen ? chosen : base;
        slot->covered = *covered;
    }
    return chosen ? chosen : base;
}

/* ---------- script segmentation and shaping ---------- */

static hb_script_t codepoint_script(uint32_t codepoint) {
    return hb_unicode_script(hb_unicode_funcs_get_default(), codepoint);
}

static bool script_is_neutral(hb_script_t script) {
    return script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED ||
           script == HB_SCRIPT_UNKNOWN || script == HB_SCRIPT_INVALID;
}

static bool codepoint_attaches_to_previous(uint32_t codepoint) {
    if (codepoint == 0x200Du || (codepoint >= 0xFE00u && codepoint <= 0xFE0Fu) ||
        (codepoint >= 0xE0100u && codepoint <= 0xE01EFu)) {
        return true;
    }
    return hb_unicode_combining_class(hb_unicode_funcs_get_default(), codepoint) != 0;
}

typedef struct run_builder {
    pt_shaped_run *runs;
    unsigned int count;
    unsigned int capacity;
    /* Direction context applied to every run pushed while building one UBA
     * segment. The legacy LTR path leaves level 0 / HB_DIRECTION_INVALID. */
    uint8_t level;
    hb_direction_t direction;
} run_builder;

static pt_shaped_run *run_push(run_builder *builder, size_t start, size_t end, pt_font *font,
                               hb_script_t script, bool covered) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 4;
        builder->runs = pt_realloc_mem(builder->runs, builder->capacity * sizeof(*builder->runs));
        memset(builder->runs + builder->count, 0,
               (builder->capacity - builder->count) * sizeof(*builder->runs));
    }
    pt_shaped_run *run = &builder->runs[builder->count++];
    run->start = start;
    run->end = end;
    run->font = font;
    run->script = script;
    run->covered = covered;
    run->level = builder->level;
    run->direction = builder->direction;
    return run;
}

/* A maximal same-script span formed before any font is chosen. Neutral
 * characters inherit the preceding script; combining marks are inherited by
 * definition and therefore land in the preceding span. */
typedef struct script_span {
    size_t start;
    size_t end;
    hb_script_t script;
} script_span;

typedef struct span_builder {
    script_span *items;
    size_t count;
    size_t capacity;
} span_builder;

static void span_push(span_builder *builder, size_t start, size_t end, hb_script_t script) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 4;
        builder->items =
            pt_realloc_mem(builder->items, builder->capacity * sizeof(*builder->items));
    }
    builder->items[builder->count++] = (script_span){start, end, script};
}

static void build_script_spans(const char *text, size_t start, size_t end,
                               span_builder *builder) {
    hb_script_t inherited = HB_SCRIPT_INVALID;
    size_t pos = start;
    while (pos < end) {
        size_t next = 0;
        uint32_t codepoint = pt_utf8_decode(text, end, pos, &next);
        if (next <= pos) {
            next = pos + 1;
        }
        hb_script_t script = codepoint_script(codepoint);
        if (script_is_neutral(script)) {
            script = inherited;
        } else {
            inherited = script;
        }
        if (builder->count > 0 && builder->items[builder->count - 1].script == script) {
            builder->items[builder->count - 1].end = next;
        } else {
            span_push(builder, pos, next, script);
        }
        pos = next;
    }
}

static bool span_covered_by(const pt_font *font, const char *text, size_t start, size_t end,
                            bool ignore_removed) {
    if (!font) {
        return false;
    }
    size_t pos = start;
    while (pos < end) {
        size_t next = 0;
        uint32_t codepoint = pt_utf8_decode(text, end, pos, &next);
        if (next <= pos) {
            next = pos + 1;
        }
        if (ignore_removed && pt_bidi_is_removed(codepoint)) {
            pos = next;
            continue;
        }
        if (!pt_font_has_glyph(font, codepoint)) {
            return false;
        }
        pos = next;
    }
    return true;
}

/* Chooses one face for a whole script span: the primary when it covers every
 * character, otherwise the first chain family that does, otherwise the first
 * family Fontconfig's sort order offers. Returns NULL when no single face
 * covers the span, in which case the caller splits it per codepoint. */
static pt_font *face_for_span(pt_font *base, const char *text, size_t start, size_t end,
                              bool ignore_removed, bool *covered) {
    *covered = false;
    if (span_covered_by(base, text, start, end, ignore_removed)) {
        *covered = true;
        return base;
    }
    const pt_font_chain *chain = chain_for_family(base->family);
    if (chain) {
        for (int i = 0; i < chain->count; ++i) {
            if (strcmp(chain->families[i], base->family) == 0) {
                continue;
            }
            pt_font *candidate =
                pt_font_acquire_internal(chain->families[i], base->weight,
                                         (pt_font_slant)base->slant, base->pixel_size, false);
            if (candidate && span_covered_by(candidate, text, start, end, ignore_removed)) {
                *covered = true;
                return candidate;
            }
        }
        return NULL;
    }
    int size_26_6 = (int)lroundf(base->pixel_size * 64.0f);
    char *family = fontconfig_system_family_span(base->weight, base->slant, size_26_6, text,
                                                 start, end, ignore_removed);
    if (!family) {
        return NULL;
    }
    pt_font *candidate = pt_font_acquire_internal(family, base->weight, (pt_font_slant)base->slant,
                                                  base->pixel_size, false);
    free(family);
    if (candidate && span_covered_by(candidate, text, start, end, ignore_removed)) {
        *covered = true;
        return candidate;
    }
    return NULL;
}

/* Splits [start, end) into contiguous script/face runs. A script span becomes
 * one run when a single face covers it; otherwise it is split per codepoint so
 * a stray symbol can still resolve. Joiners, variation selectors, combining
 * marks and (in bidi mode) X9 controls stay with the face of the preceding run
 * when it can render them, so shaping keeps Arabic joining and the controls in
 * one buffer. */
static void build_runs_between(pt_font *base, const char *text, size_t start, size_t end,
                               bool bidi, run_builder *builder) {
    span_builder spans = {0};
    build_script_spans(text, start, end, &spans);
    for (size_t index = 0; index < spans.count; ++index) {
        script_span span = spans.items[index];
        bool covered = false;
        pt_font *face = face_for_span(base, text, span.start, span.end, bidi, &covered);
        if (face) {
            run_push(builder, span.start, span.end, face, span.script, covered);
            continue;
        }
        size_t pos = span.start;
        while (pos < span.end) {
            size_t next = 0;
            uint32_t codepoint = pt_utf8_decode(text, span.end, pos, &next);
            if (next <= pos) {
                next = pos + 1;
            }
            hb_script_t script = span.script;
            pt_font *fallback_face = NULL;
            bool fallback_covered = false;
            bool attached = false;
            bool removed = bidi && pt_bidi_is_removed(codepoint);
            if ((codepoint_attaches_to_previous(codepoint) || removed) && builder->count > 0) {
                pt_shaped_run *previous = &builder->runs[builder->count - 1];
                if (previous->end == pos &&
                    (removed || codepoint == 0x200Du ||
                     (codepoint >= 0xFE00u && codepoint <= 0xFE0Fu) ||
                     (codepoint >= 0xE0100u && codepoint <= 0xE01EFu) ||
                     pt_font_has_glyph(previous->font, codepoint))) {
                    fallback_face = previous->font;
                    fallback_covered = previous->covered;
                    /* A mark keeps its own strong script (a Hebrew point after
                     * an Arabic letter stays Hebrew). Only neutral marks and
                     * joiners/selectors inherit the preceding run's script, so
                     * shaping the bytes in isolation resolves the same scripts
                     * as shaping them inside the whole line. */
                    if (script_is_neutral(script)) {
                        script = previous->script;
                    }
                    attached = true;
                }
            }
            if (!attached) {
                fallback_face = resolve_face(base, codepoint, &fallback_covered);
            }
            if (builder->count > 0) {
                pt_shaped_run *run = &builder->runs[builder->count - 1];
                if (run->end == pos && run->font == fallback_face && run->script == script &&
                    run->covered == fallback_covered) {
                    run->end = next;
                    pos = next;
                    continue;
                }
            }
            run_push(builder, pos, next, fallback_face, script, fallback_covered);
            pos = next;
        }
    }
    free(spans.items);
}

static hb_buffer_t *shape_run(pt_font *font, const char *text, size_t start, size_t end,
                              hb_script_t script, hb_direction_t direction) {
    hb_buffer_t *buffer = hb_buffer_create();
    if (!buffer) {
        return NULL;
    }
    hb_buffer_add_utf8(buffer, text + start, (int)(end - start), 0, (int)(end - start));
    hb_buffer_guess_segment_properties(buffer);
    if (!script_is_neutral(script)) {
        hb_buffer_set_script(buffer, script);
        if (direction == HB_DIRECTION_INVALID) {
            hb_direction_t guessed = hb_script_get_horizontal_direction(script);
            if (guessed != HB_DIRECTION_INVALID) {
                hb_buffer_set_direction(buffer, guessed);
            }
        }
    }
    /* Bidi runs carry the direction resolved from their embedding level; never
     * let the guess override an RTL run (UAX #9 reordering needs it, and HB
     * mirrors paired brackets from the direction). */
    if (direction != HB_DIRECTION_INVALID) {
        hb_buffer_set_direction(buffer, direction);
    }
    hb_shape(font->hb_font, buffer, NULL, 0);
    return buffer;
}

/* Shape-cache direction keys. Zero is the legacy LTR/auto guess path, which
 * must keep matching the pre-2.0 cache layout for LTR text. */
enum {
    pt_shape_key_base_auto = 1,
    pt_shape_key_base_ltr = 2,
    pt_shape_key_base_rtl = 3,
    pt_shape_key_run_ltr = 4,
    pt_shape_key_run_rtl = 5
};

static hb_direction_t direction_for_level(uint8_t level) {
    return (level & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
}

/* Reverses the sub-runs just appended for one RTL (odd level) embedding run.
 * Script/face sub-runs inside a single UBA run are kept in logical order by the
 * splitter, but painting advances the pen left to right and each sub-run is
 * shaped RTL already (HarfBuzz returns its glyphs in visual order). Odd-level
 * sub-runs therefore have to be emitted in reverse logical order; even-level
 * runs stay logical, which is also their visual order. */
static void reverse_run_range(run_builder *builder, size_t first) {
    if (!builder || builder->count <= first + 1) {
        return;
    }
    for (size_t left = first, right = builder->count - 1; left < right; ++left, --right) {
        pt_shaped_run temporary = builder->runs[left];
        builder->runs[left] = builder->runs[right];
        builder->runs[right] = temporary;
    }
}

static pt_shape_entry *shape_entry_alloc(pt_font *font, const char *text, size_t len,
                                         uint8_t direction_key) {
    pt_shape_entry *entry = pt_alloc_zeroed(1, sizeof(*entry));
    entry->generation = fallback_generation;
    entry->direction_key = direction_key;
    entry->text = pt_alloc(len + 1);
    memcpy(entry->text, text, len);
    entry->text[len] = '\0';
    entry->len = len;
    entry->ascent = font->ascent;
    entry->descent = font->descent;
    return entry;
}

static void shape_entry_finish(pt_shape_entry *entry, run_builder *builder) {
    if (builder->count == 0) {
        /* Callers build the fallback run before asking for shaping. */
        return;
    }
    entry->runs = builder->runs;
    entry->run_count = builder->count;
    for (unsigned int i = 0; i < entry->run_count; ++i) {
        pt_shaped_run *run = &entry->runs[i];
        run->buffer =
            shape_run(run->font, entry->text, run->start, run->end, run->script, run->direction);
        if (run->font->ascent > entry->ascent) {
            entry->ascent = run->font->ascent;
        }
        if (run->font->descent > entry->descent) {
            entry->descent = run->font->descent;
        }
    }
    entry->hash = shape_hash(entry->text, entry->len, entry->direction_key);
}

static pt_shape_entry *shape_entry_build(pt_font *font, const char *text, size_t len,
                                         uint8_t direction_key, bool bidi) {
    pt_shape_entry *entry = shape_entry_alloc(font, text, len, direction_key);
    run_builder builder = {0};
    pt_bidi_paragraph *paragraph = NULL;
    pt_bidi_line line = {0};
    bool reordered = false;
    bool base_mode = direction_key == pt_shape_key_base_auto ||
                     direction_key == pt_shape_key_base_ltr ||
                     direction_key == pt_shape_key_base_rtl;
    if (bidi && base_mode) {
        pt_bidi_base base = pt_bidi_base_auto;
        switch (direction_key) {
        case pt_shape_key_base_ltr:
            base = pt_bidi_base_ltr;
            break;
        case pt_shape_key_base_rtl:
            base = pt_bidi_base_rtl;
            break;
        default:
            base = pt_bidi_base_auto;
            break;
        }
        /* A single-line request may still receive embedded newlines (a label
         * with explicit breaks). Each UBA paragraph reorders independently;
         * the paragraphs stay concatenated in logical order on the one painted
         * line, which is the best single-line reading of the text. */
        uint8_t first_base = 0;
        bool saw_paragraph = false;
        size_t at = 0;
        while (at < len) {
            size_t remaining = len - at;
            if (!pt_bidi_paragraph_analyze(text + at, remaining, base, &paragraph)) {
                break;
            }
            size_t paragraph_length = pt_bidi_paragraph_length(paragraph);
            if (paragraph_length == 0 || paragraph_length > remaining) {
                pt_bidi_paragraph_free(paragraph);
                paragraph = NULL;
                break;
            }
            if (pt_bidi_line_create(paragraph, 0, paragraph_length, &line)) {
                if (!saw_paragraph) {
                    first_base = line.base_level;
                    saw_paragraph = true;
                }
                for (size_t i = 0; i < line.run_count; ++i) {
                    const pt_bidi_run *run = &line.runs[i];
                    size_t first = builder.count;
                    builder.level = run->level;
                    builder.direction = direction_for_level(run->level);
                    build_runs_between(font, text, at + run->offset,
                                       at + run->offset + run->length, true, &builder);
                    if (run->level & 1u) {
                        reverse_run_range(&builder, first);
                    }
                }
                pt_bidi_line_free(&line);
            } else {
                /* Keep the bytes when a paragraph line cannot be created; the
                 * paragraph base still drives their direction. */
                if (!saw_paragraph) {
                    first_base = pt_bidi_paragraph_base_level(paragraph);
                    saw_paragraph = true;
                }
                builder.level = pt_bidi_paragraph_base_level(paragraph);
                builder.direction = direction_for_level(builder.level);
                size_t first = builder.count;
                build_runs_between(font, text, at, at + paragraph_length, true, &builder);
                if (builder.level & 1u) {
                    reverse_run_range(&builder, first);
                }
            }
            pt_bidi_paragraph_free(paragraph);
            paragraph = NULL;
            at += paragraph_length;
        }
        if (saw_paragraph) {
            if (at < len) {
                builder.level = first_base;
                builder.direction = direction_for_level(first_base);
                size_t first = builder.count;
                build_runs_between(font, text, at, len, true, &builder);
                if (builder.level & 1u) {
                    reverse_run_range(&builder, first);
                }
            }
            if (builder.count == 0) {
                /* Degenerate all-removed text still shapes under the paragraph
                 * base so empty/control-only input is not guessed LTR. */
                builder.level = first_base;
                builder.direction = direction_for_level(first_base);
            }
            reordered = true;
        }
    }
    if (!reordered) {
        builder.level = 0;
        builder.direction = HB_DIRECTION_INVALID;
        if (direction_key == pt_shape_key_run_rtl) {
            builder.level = 1;
            builder.direction = HB_DIRECTION_RTL;
        } else if (direction_key == pt_shape_key_run_ltr) {
            builder.level = 0;
            builder.direction = HB_DIRECTION_LTR;
        } else if (direction_key == pt_shape_key_base_rtl) {
            builder.level = 1;
            builder.direction = HB_DIRECTION_RTL;
        }
        size_t first = builder.count;
        build_runs_between(font, text, 0, len, bidi, &builder);
        if (bidi && (builder.level & 1u)) {
            reverse_run_range(&builder, first);
        }
    }
    if (builder.count == 0) {
        run_push(&builder, 0, len, font, HB_SCRIPT_INVALID, true);
    }
    shape_entry_finish(entry, &builder);
    if (reordered) pt_bidi_line_free(&line);
    if (paragraph) pt_bidi_paragraph_free(paragraph);
    return entry;
}

/* Returns a cached entry, or a caller-owned one for text beyond the cache
 * limit. `*cached` tells the caller which. */
static pt_shape_entry *shape_entry_get(pt_font *font, const char *text, size_t len,
                                       uint8_t direction_key, bool bidi, bool *cached) {
    uint64_t hash = shape_hash(text, len, direction_key);
    pt_shape_entry *entry = shape_cache_lookup(font, text, len, hash, direction_key);
    if (entry) {
        *cached = true;
        return entry;
    }
    entry = shape_entry_build(font, text, len, direction_key, bidi);
    if (len > shape_cache_max_text) {
        *cached = false;
        return entry;
    }
    shape_cache_insert(font, entry);
    *cached = true;
    return entry;
}

static bool bidi_fast_path(pt_bidi_base base, const char *text, size_t len) {
    if (base == pt_bidi_base_rtl) return false;
    return !pt_bidi_text_may_reorder(text, len);
}

static uint8_t bidi_direction_key(pt_bidi_base base) {
    switch (base) {
    case pt_bidi_base_ltr:
        return pt_shape_key_base_ltr;
    case pt_bidi_base_rtl:
        return pt_shape_key_base_rtl;
    case pt_bidi_base_auto:
    default:
        return pt_shape_key_base_auto;
    }
}

static float shape_entry_measure(const pt_shape_entry *entry) {
    int64_t total = 0;
    for (unsigned int run_index = 0; run_index < entry->run_count; ++run_index) {
        hb_buffer_t *buffer = entry->runs[run_index].buffer;
        if (!buffer) {
            continue;
        }
        unsigned int glyph_count = 0;
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
        for (unsigned int i = 0; i < glyph_count; ++i) {
            total += positions[i].x_advance;
        }
    }
    return (float)total / 64.0f;
}

static float shape_entry_measure_and_release(pt_font *font, const char *text, size_t len,
                                             uint8_t direction_key, bool bidi) {
    bool cached = false;
    pt_shape_entry *entry = shape_entry_get(font, text, len, direction_key, bidi, &cached);
    float width = shape_entry_measure(entry);
    if (!cached) {
        shape_entry_clear(entry);
    }
    return width;
}

float pt_font_measure(const pt_font *font, const char *text, size_t len) {
    if (!font || !text || len == 0) {
        return 0.0f;
    }
    return shape_entry_measure_and_release((pt_font *)font, text, len, 0, false);
}

float pt_font_measure_bidi(const pt_font *font, const char *text, size_t len,
                           pt_bidi_base base) {
    if (!font || !text || len == 0) {
        return 0.0f;
    }
    if (bidi_fast_path(base, text, len)) {
        return shape_entry_measure_and_release((pt_font *)font, text, len, 0, false);
    }
    return shape_entry_measure_and_release((pt_font *)font, text, len, bidi_direction_key(base),
                                           true);
}

float pt_font_measure_direction(const pt_font *font, const char *text, size_t len,
                                hb_direction_t direction) {
    if (!font || !text || len == 0) {
        return 0.0f;
    }
    if (direction == HB_DIRECTION_INVALID) {
        return shape_entry_measure_and_release((pt_font *)font, text, len, 0, false);
    }
    uint8_t key =
        direction == HB_DIRECTION_RTL ? pt_shape_key_run_rtl : pt_shape_key_run_ltr;
    return shape_entry_measure_and_release((pt_font *)font, text, len, key, true);
}

/* ---------- logical <-> visual map (2.0 wave 3) ---------- */

typedef struct glyph_group {
    size_t offset;
    float x0;
    float x1;
} glyph_group;

typedef struct group_builder {
    glyph_group *items;
    size_t count;
    size_t capacity;
} group_builder;

static void group_push(group_builder *builder, size_t offset, float x0, float x1) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity ? builder->capacity * 2 : 16;
        builder->items = pt_realloc_mem(builder->items,
                                        builder->capacity * sizeof(*builder->items));
    }
    builder->items[builder->count++] = (glyph_group){offset, x0, x1};
}

static int compare_groups(const void *a, const void *b) {
    const glyph_group *ga = a;
    const glyph_group *gb = b;
    if (ga->offset < gb->offset) return -1;
    if (ga->offset > gb->offset) return 1;
    return 0;
}

/* Collects glyph clusters for one visual segment. HarfBuzz reports glyphs in
 * visual order and shaped entries keep their sub-runs in visual order too (odd
 * levels are reversed when the entry is built), so walking the runs forward
 * advances the pen left to right. Cluster values are run-relative and shifted
 * back to segment offsets. */
static bool segment_glyph_groups(pt_font *font, const char *text, size_t len,
                                 hb_direction_t direction, float origin,
                                 group_builder *groups, float *out_width) {
    uint8_t key = direction == HB_DIRECTION_RTL ? pt_shape_key_run_rtl
                                                : pt_shape_key_run_ltr;
    bool cached = false;
    pt_shape_entry *entry = shape_entry_get(font, text, len, key, true, &cached);
    float pen = origin;
    for (unsigned int step = 0; step < entry->run_count; ++step) {
        const pt_shaped_run *run = &entry->runs[step];
        hb_buffer_t *buffer = run->buffer;
        if (!buffer) {
            continue;
        }
        unsigned int glyph_count = 0;
        hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
        size_t open_offset = 0;
        float open_x0 = pen;
        bool open = false;
        for (unsigned int i = 0; i < glyph_count; ++i) {
            size_t offset = run->start + (size_t)infos[i].cluster;
            if (!open || offset != open_offset) {
                if (open) {
                    group_push(groups, open_offset, open_x0, pen);
                }
                open = true;
                open_offset = offset;
                open_x0 = pen;
            }
            pen += (float)positions[i].x_advance / 64.0f;
        }
        if (open) {
            group_push(groups, open_offset, open_x0, pen);
        }
    }
    *out_width = pen - origin;
    if (!cached) {
        shape_entry_clear(entry);
    }
    return true;
}

/* Clusters are appended segment by segment in visual order, so their logical
 * starts are not monotonic on mixed lines. The map contract, the binary lookup
 * and the two end shortcuts all assume logical order; a stable insertion sort
 * (same logical start => zero-width removed clusters keep relative order) makes
 * the array monotonic again. Insertion sort is linear on the already sorted
 * pure LTR and single-run RTL arrays. */
static void clusters_sort_by_start(pt_text_visual_map *map) {
    for (size_t i = 1; i < map->count; ++i) {
        pt_text_visual_cluster cluster = map->clusters[i];
        size_t at = i;
        while (at > 0 && map->clusters[at - 1].start > cluster.start) {
            map->clusters[at] = map->clusters[at - 1];
            --at;
        }
        map->clusters[at] = cluster;
    }
}

bool pt_text_visual_map_build(const char *text, size_t len,
                              const pt_text_visual_segment *segments, size_t count,
                              uint8_t base_level, pt_text_visual_map **out) {
    if (!out) {
        return false;
    }
    *out = NULL;
    if (!text && len != 0) {
        return false;
    }
    pt_text_visual_map *map = pt_alloc_zeroed(1, sizeof(*map));
    map->start = 0;
    map->end = len;
    map->base_level = base_level;
    if (len == 0 || count == 0) {
        *out = map;
        return true;
    }
    size_t capacity = 0;
    for (size_t i = 0; i < count; ++i) {
        if (segments[i].end > segments[i].start) {
            capacity += segments[i].end - segments[i].start;
        }
    }
    if (capacity == 0) {
        *out = map;
        return true;
    }
    map->clusters = pt_alloc_zeroed(capacity, sizeof(*map->clusters));
    float visual_x = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const pt_text_visual_segment *segment = &segments[i];
        if (segment->end <= segment->start || !segment->font) {
            continue;
        }
        const char *segment_text = text + segment->start;
        size_t segment_len = segment->end - segment->start;
        group_builder groups = {0};
        float width = 0.0f;
        if (!segment_glyph_groups(segment->font, segment_text, segment_len, segment->direction,
                                  visual_x, &groups, &width)) {
            free(groups.items);
            pt_text_visual_map_free(map);
            return false;
        }
        qsort(groups.items, groups.count, sizeof(*groups.items), compare_groups);
        bool rtl = segment->direction == HB_DIRECTION_RTL;
        for (size_t g = 0; g < groups.count; ++g) {
            /* Cluster extent ends at the next group start in logical order; the
             * final one ends at the segment end. Groups with the same logical
             * offset (reordered marks) are merged by taking the union. */
            size_t cluster_start = groups.items[g].offset;
            size_t cluster_end = g + 1 < groups.count ? groups.items[g + 1].offset
                                                      : segment_len;
            if (cluster_end <= cluster_start) {
                cluster_end = segment_len;
            }
            float x0 = groups.items[g].x0;
            float x1 = groups.items[g].x1;
            if (x1 < x0) {
                float swap = x0;
                x0 = x1;
                x1 = swap;
            }
            pt_text_visual_cluster *cluster = &map->clusters[map->count++];
            cluster->start = segment->start + cluster_start;
            cluster->end = segment->start + cluster_end;
            cluster->x0 = x0;
            cluster->x1 = x1;
            cluster->level = segment->level;
            if (rtl) {
                cluster->start_x = x1;
                cluster->end_x = x0;
            } else {
                cluster->start_x = x0;
                cluster->end_x = x1;
            }
        }
        free(groups.items);
        visual_x += width;
    }
    clusters_sort_by_start(map);
    map->width = visual_x;
    *out = map;
    return true;
}

bool pt_text_visual_map_build_bidi(const pt_font *font, const char *text, size_t len,
                                   pt_bidi_base base, pt_text_visual_map **out) {
    if (!out) {
        return false;
    }
    *out = NULL;
    if (!font || (!text && len != 0)) {
        return false;
    }
    pt_font_run *runs = NULL;
    int run_count = pt_font_runs_bidi(font, text, len, base, &runs);
    if (len > 0 && run_count <= 0) {
        return false;
    }
    pt_text_visual_segment *segments =
        run_count > 0 ? pt_alloc_zeroed((size_t)run_count, sizeof(*segments)) : NULL;
    /* The map's base is the paragraph's resolved base, never the smallest run
     * level: an all-LTR paragraph under a forced RTL base has only level 2 runs,
     * and Hebrew under a forced LTR base only level 1 runs. Auto resolves
     * P2/P3 once for the whole range. */
    uint8_t base_level = base == pt_bidi_base_rtl ? 1 : 0;
    if (base == pt_bidi_base_auto) {
        pt_bidi_paragraph *paragraph = NULL;
        if (pt_bidi_paragraph_analyze(text, len, pt_bidi_base_auto, &paragraph)) {
            base_level = pt_bidi_paragraph_base_level(paragraph);
            pt_bidi_paragraph_free(paragraph);
        }
    }
    for (int i = 0; i < run_count; ++i) {
        segments[i].start = runs[i].start;
        segments[i].end = runs[i].end;
        segments[i].font = runs[i].font;
        segments[i].level = runs[i].level;
        segments[i].direction =
            runs[i].direction != HB_DIRECTION_INVALID
                ? runs[i].direction
                : ((runs[i].level & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    }
    bool ok = pt_text_visual_map_build(text, len, segments, (size_t)run_count, base_level, out);
    free(segments);
    pt_font_runs_free(runs);
    return ok;
}

void pt_text_visual_map_free(pt_text_visual_map *map) {
    if (!map) {
        return;
    }
    free(map->clusters);
    free(map);
}

float pt_text_visual_map_width(const pt_text_visual_map *map) {
    return map ? map->width : 0.0f;
}

static const pt_text_visual_cluster *map_cluster_at(const pt_text_visual_map *map,
                                                    size_t position) {
    if (!map || map->count == 0) {
        return NULL;
    }
    size_t low = 0;
    size_t high = map->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (map->clusters[middle].start <= position) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0) {
        return &map->clusters[0];
    }
    return &map->clusters[low - 1];
}

float pt_text_visual_map_x_for_position(const pt_text_visual_map *map, size_t position,
                                        pt_text_bias bias) {
    if (!map || map->count == 0) {
        return 0.0f;
    }
    if (position <= map->start) {
        return map->clusters[0].start_x;
    }
    if (position >= map->end) {
        const pt_text_visual_cluster *last = &map->clusters[map->count - 1];
        return last->end_x;
    }
    const pt_text_visual_cluster *cluster = map_cluster_at(map, position);
    if (!cluster) {
        return 0.0f;
    }
    if (position == cluster->start) {
        if (bias == pt_text_bias_trailing && cluster > map->clusters) {
            return (cluster - 1)->end_x;
        }
        return cluster->start_x;
    }
    return bias == pt_text_bias_trailing ? cluster->end_x : cluster->start_x;
}

size_t pt_text_visual_map_position_for_x(const pt_text_visual_map *map, float x,
                                         pt_text_bias bias) {
    if (!map || map->count == 0) {
        return map ? map->start : 0;
    }
    size_t best = map->start;
    float best_distance = FLT_MAX;
    bool best_leading = bias == pt_text_bias_leading;
    bool have_best = false;
    for (size_t i = 0; i < map->count; ++i) {
        const pt_text_visual_cluster *cluster = &map->clusters[i];
        float distance = fabsf(x - cluster->start_x);
        if (!have_best || distance < best_distance ||
            (distance == best_distance && bias == pt_text_bias_leading)) {
            best = cluster->start;
            best_distance = distance;
            best_leading = true;
            have_best = true;
        }
        distance = fabsf(x - cluster->end_x);
        if (!have_best || distance < best_distance ||
            (distance == best_distance && bias == pt_text_bias_trailing)) {
            best = cluster->end;
            best_distance = distance;
            best_leading = false;
            have_best = true;
        }
    }
    (void)best_leading;
    return best;
}

bool pt_text_visual_map_is_boundary(const pt_text_visual_map *map, size_t position) {
    if (!map || position < map->start || position > map->end) {
        return false;
    }
    if (position == map->start || position == map->end) {
        return true;
    }
    for (size_t i = 0; i < map->count; ++i) {
        if (map->clusters[i].start == position || map->clusters[i].end == position) {
            return true;
        }
    }
    return false;
}

size_t pt_text_visual_map_next_boundary(const pt_text_visual_map *map, size_t position) {
    if (!map) {
        return position;
    }
    size_t best = map->end;
    for (size_t i = 0; i < map->count; ++i) {
        const pt_text_visual_cluster *cluster = &map->clusters[i];
        if (cluster->end > position && cluster->end < best) {
            best = cluster->end;
        }
        if (cluster->start > position && cluster->start < best) {
            best = cluster->start;
        }
    }
    return best;
}

size_t pt_text_visual_map_prev_boundary(const pt_text_visual_map *map, size_t position) {
    if (!map) {
        return position;
    }
    size_t best = map->start;
    for (size_t i = 0; i < map->count; ++i) {
        const pt_text_visual_cluster *cluster = &map->clusters[i];
        if (cluster->start < position && cluster->start > best) {
            best = cluster->start;
        }
        if (cluster->end < position && cluster->end > best) {
            best = cluster->end;
        }
    }
    return best;
}

void pt_font_metrics_for_text(const pt_font *font, const char *text, size_t len,
                              float *ascent, float *descent) {
    float out_ascent = font ? font->ascent : 0.0f;
    float out_descent = font ? font->descent : 0.0f;
    if (font && text && len > 0) {
        bool cached = false;
        pt_shape_entry *entry = shape_entry_get((pt_font *)font, text, len, 0, false, &cached);
        out_ascent = entry->ascent;
        out_descent = entry->descent;
        if (!cached) {
            shape_entry_clear(entry);
        }
    }
    if (ascent) {
        *ascent = out_ascent;
    }
    if (descent) {
        *descent = out_descent;
    }
}

void pt_font_metrics_for_text_bidi(const pt_font *font, const char *text, size_t len,
                                   pt_bidi_base base, float *ascent, float *descent) {
    if (!font || !text || len == 0 || bidi_fast_path(base, text, len)) {
        pt_font_metrics_for_text(font, text, len, ascent, descent);
        return;
    }
    bool cached = false;
    pt_shape_entry *entry = shape_entry_get((pt_font *)font, text, len, bidi_direction_key(base),
                                            true, &cached);
    if (ascent) *ascent = entry->ascent;
    if (descent) *descent = entry->descent;
    if (!cached) {
        shape_entry_clear(entry);
    }
}

float pt_font_line_height_for(const pt_font *font, const char *text, size_t len) {
    if (!font) {
        return 0.0f;
    }
    float ascent = 0.0f;
    float descent = 0.0f;
    pt_font_metrics_for_text(font, text, len, &ascent, &descent);
    return pt_max_float(ascent + descent, font->line_height);
}

static int font_runs_with(pt_font *font, const char *text, size_t len, uint8_t direction_key,
                          bool bidi, pt_font_run **out_runs) {
    bool cached = false;
    pt_shape_entry *entry = shape_entry_get(font, text, len, direction_key, bidi, &cached);
    pt_font_run *runs = pt_alloc_zeroed(entry->run_count, sizeof(*runs));
    for (unsigned int i = 0; i < entry->run_count; ++i) {
        runs[i].start = entry->runs[i].start;
        runs[i].end = entry->runs[i].end;
        runs[i].font = entry->runs[i].font;
        runs[i].script = entry->runs[i].script;
        runs[i].covered = entry->runs[i].covered;
        runs[i].level = entry->runs[i].level;
        runs[i].direction = entry->runs[i].direction;
        runs[i].visual_index = i;
    }
    int count = (int)entry->run_count;
    if (!cached) {
        shape_entry_clear(entry);
    }
    *out_runs = runs;
    return count;
}

int pt_font_runs(const pt_font *font, const char *text, size_t len, pt_font_run **out_runs) {
    if (!out_runs) {
        return 0;
    }
    *out_runs = NULL;
    if (!font || !text || len == 0) {
        return 0;
    }
    return font_runs_with((pt_font *)font, text, len, 0, false, out_runs);
}

int pt_font_runs_bidi(const pt_font *font, const char *text, size_t len, pt_bidi_base base,
                      pt_font_run **out_runs) {
    if (!out_runs) {
        return 0;
    }
    *out_runs = NULL;
    if (!font || !text || len == 0) {
        return 0;
    }
    if (bidi_fast_path(base, text, len)) {
        return font_runs_with((pt_font *)font, text, len, 0, false, out_runs);
    }
    return font_runs_with((pt_font *)font, text, len, bidi_direction_key(base), true, out_runs);
}

void pt_font_runs_free(pt_font_run *runs) {
    free(runs);
}

size_t pt_font_shape_cache_count(const pt_font *font) {
    return font ? font->shapes.count : 0;
}

size_t pt_font_fallback_cache_count(const pt_font *font) {
    return font ? font->fallbacks.count : 0;
}

size_t pt_font_manager_face_count(void) {
    return font_table_size;
}

/* ---------- installed fallback chains (public, since 1.2) ---------- */

static pt_font_override *fallback_override_find(const char *family) {
    if (!family) {
        return NULL;
    }
    for (int i = 0; i < fallback_override_count; ++i) {
        if (strcmp(fallback_overrides[i].family, family) == 0) {
            return &fallback_overrides[i];
        }
    }
    return NULL;
}

static void fallback_override_remove(const char *family) {
    pt_font_override *override = fallback_override_find(family);
    if (!override) {
        return;
    }
    int index = (int)(override - fallback_overrides);
    free(override->family);
    chain_clear(&override->chain);
    for (int i = index; i + 1 < fallback_override_count; ++i) {
        fallback_overrides[i] = fallback_overrides[i + 1];
    }
    --fallback_override_count;
    memset(&fallback_overrides[fallback_override_count], 0,
           sizeof(fallback_overrides[fallback_override_count]));
}

static pt_font_override *fallback_override_ensure(const char *family) {
    pt_font_override *override = fallback_override_find(family);
    if (override) {
        return override;
    }
    int slot = fallback_override_count;
    if (slot >= PT_FONT_FALLBACK_MAX_OVERRIDES) {
        slot = 0;
        for (int i = 1; i < fallback_override_count; ++i) {
            if (fallback_overrides[i].stamp < fallback_overrides[slot].stamp) {
                slot = i;
            }
        }
        free(fallback_overrides[slot].family);
        chain_clear(&fallback_overrides[slot].chain);
        memset(&fallback_overrides[slot], 0, sizeof(fallback_overrides[slot]));
    } else {
        ++fallback_override_count;
    }
    override = &fallback_overrides[slot];
    override->family = family_dup(family);
    override->stamp = ++fallback_stamp;
    return override;
}

void pt_app_set_font_fallbacks(pt_app *app, const char *const *families, int count) {
    if (!app) {
        return;
    }
    chain_assign(&fallback_default, families, count);
    fallback_generation_bump();
    fallback_repaint(app);
}

int pt_app_get_font_fallback_count(const pt_app *app) {
    return app ? fallback_default.count : 0;
}

const char *pt_app_get_font_fallback(const pt_app *app, int index) {
    if (!app || index < 0 || index >= fallback_default.count) {
        return NULL;
    }
    return fallback_default.families[index];
}

void pt_app_set_font_fallbacks_for_family(pt_app *app, const char *family,
                                           const char *const *families, int count) {
    if (!app) {
        return;
    }
    pt_font_chain next = {0};
    chain_assign(&next, families, count);
    if (!family || !family[0]) {
        chain_clear(&next);
        return;
    }
    if (next.count == 0) {
        fallback_override_remove(family);
    } else {
        pt_font_override *override = fallback_override_ensure(family);
        if (!override || !override->family) {
            chain_clear(&next);
            return;
        }
        chain_clear(&override->chain);
        override->chain = next;
        override->stamp = ++fallback_stamp;
    }
    fallback_generation_bump();
    fallback_repaint(app);
}

int pt_app_get_font_fallback_count_for_family(const pt_app *app, const char *family) {
    if (!app) {
        return 0;
    }
    const pt_font_override *override = fallback_override_find(family);
    return override ? override->chain.count : 0;
}

const char *pt_app_get_font_fallback_for_family(const pt_app *app, const char *family,
                                                int index) {
    if (!app || index < 0) {
        return NULL;
    }
    const pt_font_override *override = fallback_override_find(family);
    if (!override || index >= override->chain.count) {
        return NULL;
    }
    return override->chain.families[index];
}

int pt_app_font_uncovered_count(const pt_app *app, const char *family, const char *text,
                                size_t length) {
    if (!app || !text || length == 0) {
        return 0;
    }
    const char *name = family && family[0] ? family : app->theme.font_family;
    if (!name || !name[0]) {
        name = "sans-serif";
    }
    pt_font *font = pt_font_acquire_internal(name, pt_font_weight_regular,
                                             pt_font_slant_upright,
                                             pt_default_font_size, false);
    if (!font) {
        return 0;
    }
    int uncovered = 0;
    size_t pos = 0;
    while (pos < length) {
        size_t next = 0;
        uint32_t codepoint = pt_utf8_decode(text, length, pos, &next);
        if (next <= pos) {
            next = pos + 1;
        }
        bool covered = false;
        resolve_face(font, codepoint, &covered);
        if (!covered) {
            ++uncovered;
        }
        pos = next;
    }
    return uncovered;
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
    uint8_t base_level;
} line_builder;

static void line_push(line_builder *builder, size_t start, size_t end) {
    if (builder->count == builder->capacity) {
        builder->capacity = builder->capacity == 0 ? 16 : builder->capacity * 2;
        builder->lines = pt_realloc_mem(builder->lines, builder->capacity * sizeof(pt_text_line));
    }
    builder->lines[builder->count++] =
        (pt_text_line){start, end, builder->base_level};
}

/* Width measurement for the logical-order line breaker. The LTR fast path
 * calls the pre-2.0 prefix measurement unchanged; a bidi paragraph sums the
 * UBA runs that overlap the logical range, so widths already reflect run
 * direction and fallback shaping. */
typedef struct wrap_measure {
    const pt_font *font;
    const char *text;
    const pt_bidi_line *line;
    bool bidi;
} wrap_measure;

static float wrap_measure_range(const wrap_measure *measure, size_t from, size_t to) {
    if (to <= from) {
        return 0.0f;
    }
    if (!measure->bidi || !measure->line) {
        return pt_font_measure(measure->font, measure->text + from, to - from);
    }
    float total = 0.0f;
    for (size_t i = 0; i < measure->line->run_count; ++i) {
        const pt_bidi_run *run = &measure->line->runs[i];
        size_t run_end = run->offset + run->length;
        size_t a = from > run->offset ? from : run->offset;
        size_t b = to < run_end ? to : run_end;
        if (a >= b) {
            continue;
        }
        hb_direction_t direction = (run->level & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        total += pt_font_measure_direction(measure->font, measure->text + a, b - a, direction);
    }
    return total;
}

static void wrap_paragraph(const pt_font *font, const char *text, size_t start, size_t end,
                           float max_width, const wrap_measure *measure,
                           line_builder *builder) {
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
        float width = wrap_measure_range(measure, line_start, candidate_end);
        if (width <= max_width || !has_content) {
            if (width > max_width && !has_content && candidate_end > line_start) {
                size_t fit_end = line_start;
                size_t scan = line_start;
                while (scan < candidate_end) {
                    size_t next = pt_utf8_next_boundary(text, candidate_end, scan);
                    if (next <= scan) {
                        next = scan + 1;
                    }
                    float prefix = wrap_measure_range(measure, line_start, next);
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
            float prefix = wrap_measure_range(measure, line_start, next);
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

int pt_font_wrap_lines_bidi(const pt_font *font, const char *text, size_t len, float max_width,
                            pt_bidi_base base, bool rtl_default,
                            pt_text_line **out_lines) {
    line_builder builder = {0};
    if (!text) {
        len = 0;
    }
    if (!out_lines) {
        return 0;
    }
    if (!font || len == 0) {
        builder.base_level =
            base == pt_bidi_base_rtl || (base == pt_bidi_base_auto && rtl_default) ? 1 : 0;
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
        size_t content_len = content_end - paragraph_start;

        wrap_measure measure = {.font = font, .text = text, .bidi = false};
        pt_bidi_paragraph *paragraph = NULL;
        pt_bidi_line line = {0};
        bool bidi = base == pt_bidi_base_rtl || rtl_default ||
                    (content_len > 0 &&
                     pt_bidi_text_may_reorder(text + paragraph_start, content_len));
        uint8_t base_level =
            base == pt_bidi_base_rtl ? 1 : (base == pt_bidi_base_ltr ? 0 : (rtl_default ? 1 : 0));
        if (bidi && content_len > 0) {
            bool analyzed =
                base == pt_bidi_base_auto
                    ? pt_bidi_paragraph_analyze_default(text + paragraph_start, content_len,
                                                        rtl_default, &paragraph)
                    : pt_bidi_paragraph_analyze(text + paragraph_start, content_len, base,
                                                &paragraph);
            if (analyzed) {
                base_level = pt_bidi_paragraph_base_level(paragraph);
                if (pt_bidi_line_create(paragraph, 0, content_len, &line)) {
                    /* Line offsets in `line` are relative to the paragraph;
                     * shift the measurement window with the paragraph start. */
                    measure.text = text + paragraph_start;
                    measure.line = &line;
                    measure.bidi = true;
                    line_builder shifted = builder;
                    shifted.base_level = base_level;
                    size_t before = builder.count;
                    wrap_paragraph(font, measure.text, 0, content_len, max_width, &measure,
                                   &shifted);
                    for (size_t i = before; i < shifted.count; ++i) {
                        shifted.lines[i].start += paragraph_start;
                        shifted.lines[i].end += paragraph_start;
                    }
                    builder = shifted;
                    pt_bidi_line_free(&line);
                    pt_bidi_paragraph_free(paragraph);
                    paragraph = NULL;
                    if (paragraph_end >= len) {
                        break;
                    }
                    paragraph_start = paragraph_end + 1;
                    if (paragraph_start == len) {
                        builder.base_level = base_level;
                        line_push(&builder, len, len);
                        break;
                    }
                    continue;
                }
            }
            if (paragraph) {
                pt_bidi_paragraph_free(paragraph);
            }
        }
        builder.base_level = base_level;
        wrap_paragraph(font, text, paragraph_start, content_end, max_width, &measure, &builder);
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

int pt_font_wrap_lines(const pt_font *font, const char *text, size_t len, float max_width,
                       pt_text_line **out_lines) {
    return pt_font_wrap_lines_bidi(font, text, len, max_width, pt_bidi_base_auto, false,
                                   out_lines);
}

float pt_font_wrapped_height(const pt_font *font, const char *text, float max_width) {
    if (!font) {
        return 0.0f;
    }
    pt_text_line *lines = NULL;
    int count = pt_font_wrap_lines(font, text ? text : "", text ? strlen(text) : 0, max_width,
                                   &lines);
    free(lines);
    float line_height =
        pt_font_line_height_for(font, text ? text : "", text ? strlen(text) : 0);
    return (float)count * line_height;
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

    bool cached = false;
    pt_shape_entry *entry = shape_entry_get((pt_font *)font, draw_text, draw_len, 0, false,
                                            &cached);
    float pen_x = x * canvas->scale;
    float pen_y = baseline * canvas->scale;
    for (unsigned int run_index = 0; run_index < entry->run_count; ++run_index) {
        pt_shaped_run *run = &entry->runs[run_index];
        hb_buffer_t *buffer = run->buffer;
        if (!buffer) {
            continue;
        }
        unsigned int glyph_count = 0;
        hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
        for (unsigned int i = 0; i < glyph_count; ++i) {
            pt_glyph *glyph = glyph_get(run->font, infos[i].codepoint);
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
    }
    if (!cached) {
        shape_entry_clear(entry);
    }
    free(ellipsized);
}

/* Direction-aware single-line painting (2.0 wave 2). The legacy entry point
 * above stays untouched so LTR output is byte-identical; these wrappers shape
 * with an explicit direction, skip X9-removed controls at paint time, and walk
 * the resulting runs left to right (UBA runs already arrive in visual order). */
static bool shaped_glyph_removed(const pt_shape_entry *entry, const pt_shaped_run *run,
                                 unsigned int cluster) {
    size_t at = run->start + (size_t)cluster;
    if (at >= entry->len) {
        return false;
    }
    size_t next = 0;
    uint32_t codepoint = pt_utf8_decode(entry->text, entry->len, at, &next);
    return pt_bidi_is_removed(codepoint);
}

static void draw_shaped_entry(pt_canvas *canvas, const pt_shape_entry *entry, float x,
                              float baseline, pt_color color, bool skip_removed) {
    float pen_x = x * canvas->scale;
    float pen_y = baseline * canvas->scale;
    for (unsigned int run_index = 0; run_index < entry->run_count; ++run_index) {
        const pt_shaped_run *run = &entry->runs[run_index];
        hb_buffer_t *buffer = run->buffer;
        if (!buffer) {
            continue;
        }
        unsigned int glyph_count = 0;
        hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
        for (unsigned int i = 0; i < glyph_count; ++i) {
            pt_glyph *glyph = glyph_get(run->font, infos[i].codepoint);
            float glyph_x = pen_x + (float)positions[i].x_offset / 64.0f;
            float glyph_y = pen_y - (float)positions[i].y_offset / 64.0f;
            if (glyph && glyph->coverage &&
                !(skip_removed && shaped_glyph_removed(entry, run, infos[i].cluster))) {
                int draw_x = (int)lroundf(glyph_x) + glyph->bearing_x;
                int draw_y = (int)lroundf(glyph_y) - glyph->bearing_y;
                pt_canvas_blend_mask(canvas, draw_x, draw_y, glyph->coverage, glyph->width,
                                     glyph->height, glyph->width, color, 1.0f);
            }
            pen_x += (float)positions[i].x_advance / 64.0f;
            pen_y -= (float)positions[i].y_advance / 64.0f;
        }
    }
}

/* Measurement mode for the directional painters. `directional` selects an
 * explicit per-run direction (HB_DIRECTION_INVALID means legacy); otherwise the
 * paragraph base drives a bidi measurement. */
typedef struct line_mode {
    const pt_font *font;
    pt_bidi_base base;
    hb_direction_t direction;
    bool directional;
} line_mode;

static float line_mode_measure(const line_mode *mode, const char *text, size_t len) {
    if (mode->directional) {
        return pt_font_measure_direction(mode->font, text, len, mode->direction);
    }
    return pt_font_measure_bidi(mode->font, text, len, mode->base);
}

static char *line_mode_ellipsize(const line_mode *mode, const char *text, size_t len,
                                 float max_width, size_t *out_len, float *out_width) {
    static const char ellipsis[] = "\xE2\x80\xA6";
    float ellipsis_width = line_mode_measure(mode, ellipsis, sizeof(ellipsis) - 1);
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
        float width =
            line_mode_measure(mode, buffer, candidate + sizeof(ellipsis) - 1);
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

static bool paragraph_resolves_rtl(pt_bidi_base base, const char *text, size_t len) {
    if (base == pt_bidi_base_rtl) return true;
    if (base == pt_bidi_base_ltr) return false;
    pt_bidi_paragraph *paragraph = NULL;
    if (!pt_bidi_paragraph_analyze(text, len, pt_bidi_base_auto, &paragraph)) return false;
    bool rtl = pt_bidi_paragraph_base_level(paragraph) != 0;
    pt_bidi_paragraph_free(paragraph);
    return rtl;
}

void pt_canvas_draw_text_line_bidi(pt_canvas *canvas, const pt_font *font, const char *text,
                                   size_t len, float x, float baseline, float max_width,
                                   pt_text_align align, pt_color color, bool ellipsis,
                                   pt_bidi_base base) {
    if (!canvas || !font || !text || len == 0 || (color & 0xFFu) == 0) {
        return;
    }
    if (bidi_fast_path(base, text, len)) {
        pt_canvas_draw_text_line(canvas, font, text, len, x, baseline, max_width, align, color,
                                 ellipsis);
        return;
    }
    line_mode mode = {.font = font, .base = base, .directional = false};
    const char *draw_text = text;
    size_t draw_len = len;
    char *ellipsized = NULL;
    float total_width = line_mode_measure(&mode, text, len);
    float max_width_px = max_width > 0.0f ? max_width * canvas->scale : 0.0f;
    if (max_width_px > 0.0f && total_width > max_width_px) {
        if (!ellipsis) {
            return;
        }
        size_t new_len = 0;
        float new_width = 0.0f;
        ellipsized = line_mode_ellipsize(&mode, text, len, max_width_px, &new_len, &new_width);
        draw_text = ellipsized;
        draw_len = new_len;
        total_width = new_width;
    }
    if (draw_len == 0) {
        free(ellipsized);
        return;
    }
    bool rtl = paragraph_resolves_rtl(base, text, len);
    if (max_width > 0.0f) {
        float total_logical = total_width / canvas->scale;
        if (align == pt_text_align_center) {
            x += (max_width - total_logical) * 0.5f;
        } else if ((align == pt_text_align_start && rtl) ||
                   (align == pt_text_align_end && !rtl)) {
            x += max_width - total_logical;
        }
    }
    bool cached = false;
    pt_shape_entry *entry = shape_entry_get((pt_font *)font, draw_text, draw_len,
                                            bidi_direction_key(base), true, &cached);
    draw_shaped_entry(canvas, entry, x, baseline, color, true);
    if (!cached) {
        shape_entry_clear(entry);
    }
    free(ellipsized);
}

void pt_canvas_draw_text_line_direction(pt_canvas *canvas, const pt_font *font,
                                        const char *text, size_t len, float x, float baseline,
                                        float max_width, pt_text_align align, pt_color color,
                                        bool ellipsis, hb_direction_t direction) {
    if (direction == HB_DIRECTION_INVALID) {
        pt_canvas_draw_text_line(canvas, font, text, len, x, baseline, max_width, align, color,
                                 ellipsis);
        return;
    }
    if (!canvas || !font || !text || len == 0 || (color & 0xFFu) == 0) {
        return;
    }
    line_mode mode = {.font = font, .direction = direction, .directional = true};
    const char *draw_text = text;
    size_t draw_len = len;
    char *ellipsized = NULL;
    float total_width = line_mode_measure(&mode, text, len);
    float max_width_px = max_width > 0.0f ? max_width * canvas->scale : 0.0f;
    if (max_width_px > 0.0f && total_width > max_width_px) {
        if (!ellipsis) {
            return;
        }
        size_t new_len = 0;
        float new_width = 0.0f;
        ellipsized = line_mode_ellipsize(&mode, text, len, max_width_px, &new_len, &new_width);
        draw_text = ellipsized;
        draw_len = new_len;
        total_width = new_width;
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
    bool cached = false;
    uint8_t key =
        direction == HB_DIRECTION_RTL ? pt_shape_key_run_rtl : pt_shape_key_run_ltr;
    pt_shape_entry *entry =
        shape_entry_get((pt_font *)font, draw_text, draw_len, key, true, &cached);
    draw_shaped_entry(canvas, entry, x, baseline, color, true);
    if (!cached) {
        shape_entry_clear(entry);
    }
    free(ellipsized);
}

void pt_canvas_draw_text_wrapped_bidi(pt_canvas *canvas, const pt_font *font, const char *text,
                                       float x, float top, float max_width, float max_height,
                                       pt_text_align align, pt_color color, pt_bidi_base base,
                                       bool rtl_default) {
    if (!canvas || !font || !text) {
        return;
    }
    size_t len = strlen(text);
    pt_text_line *lines = NULL;
    int count =
        pt_font_wrap_lines_bidi(font, text, len, max_width * canvas->scale, base, rtl_default,
                                &lines);
    float ascent_px = 0.0f;
    float descent_px = 0.0f;
    pt_font_metrics_for_text_bidi(font, text, len, base, &ascent_px, &descent_px);
    float ascent = ascent_px / canvas->scale;
    float line_height = pt_max_float(ascent_px + descent_px, font->line_height) / canvas->scale;
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
            pt_bidi_base line_base =
                (lines[i].base_level & 1u) ? pt_bidi_base_rtl : pt_bidi_base_ltr;
            pt_canvas_draw_text_line_bidi(canvas, font, text + start, end - start, x,
                                          line_top + ascent, max_width, align, color, false,
                                          line_base);
        }
    }
    free(lines);
}

void pt_canvas_draw_text_wrapped(pt_canvas *canvas, const pt_font *font, const char *text,
                                 float x, float top, float max_width, float max_height,
                                 pt_text_align align, pt_color color) {
    pt_canvas_draw_text_wrapped_bidi(canvas, font, text, x, top, max_width, max_height, align,
                                     color, pt_bidi_base_auto, false);
}

