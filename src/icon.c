/*
 * Raster icons: bounded PNG loading, hicolor lookup, a bounded per-size cache
 * and the button/window icon slots.
 * Copyright 2026 The Pt Authors.
 * Licensed under the Apache License, Version 2.0.
 */

#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define pt_icon_input_max_bytes (16u * 1024u * 1024u)

/* ---------- cache ---------- */

typedef struct pt_icon_cache_entry {
    uint32_t *pixels;
    int width;
    int height;
    size_t pixel_count;
    struct pt_icon_image *owner;
    struct pt_icon_cache_entry *lru_prev;
    struct pt_icon_cache_entry *lru_next;
    struct pt_icon_cache_entry *owner_next;
} pt_icon_cache_entry;

struct pt_icon_image {
    int references;
    int width;
    int height;
    pt_color *pixels;
    pt_icon_cache_entry *entries;
    unsigned int entry_count;
};

static pt_icon_cache_entry *icon_lru_head;
static pt_icon_cache_entry *icon_lru_tail;
static size_t icon_cache_entry_count;
static size_t icon_cache_pixel_count;

size_t pt_icon_cache_entry_count(void) {
    return icon_cache_entry_count;
}

size_t pt_icon_cache_pixel_count(void) {
    return icon_cache_pixel_count;
}

static void icon_cache_unlink(pt_icon_cache_entry *entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        icon_lru_head = entry->lru_next;
    }
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        icon_lru_tail = entry->lru_prev;
    }
    entry->lru_prev = NULL;
    entry->lru_next = NULL;

    pt_icon_cache_entry **link = &entry->owner->entries;
    while (*link && *link != entry) {
        link = &(*link)->owner_next;
    }
    if (*link) {
        *link = entry->owner_next;
    }
    entry->owner_next = NULL;
    if (entry->owner->entry_count > 0) {
        --entry->owner->entry_count;
    }

    if (icon_cache_entry_count > 0) {
        --icon_cache_entry_count;
    }
    if (icon_cache_pixel_count >= entry->pixel_count) {
        icon_cache_pixel_count -= entry->pixel_count;
    } else {
        icon_cache_pixel_count = 0;
    }
}

static void icon_cache_evict(pt_icon_cache_entry *entry) {
    icon_cache_unlink(entry);
    free(entry->pixels);
    free(entry);
}

static void icon_cache_touch(pt_icon_cache_entry *entry) {
    if (icon_lru_head == entry) {
        return;
    }
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    }
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        icon_lru_tail = entry->lru_prev;
    }
    entry->lru_prev = NULL;
    entry->lru_next = icon_lru_head;
    if (icon_lru_head) {
        icon_lru_head->lru_prev = entry;
    }
    icon_lru_head = entry;
    if (!icon_lru_tail) {
        icon_lru_tail = entry;
    }
}

/* Drops the oldest cached raster belonging to `image`. */
static void icon_cache_evict_oldest_owner(pt_icon_image *image) {
    for (pt_icon_cache_entry *entry = icon_lru_tail; entry; entry = entry->lru_prev) {
        if (entry->owner == image) {
            icon_cache_evict(entry);
            return;
        }
    }
}

static bool icon_cache_make_room(pt_icon_image *image, size_t pixel_count) {
    if (pixel_count > PT_ICON_CACHE_MAX_PIXELS) {
        return false;
    }
    while (image->entry_count >= PT_ICON_IMAGE_CACHE_MAX_ENTRIES && image->entries) {
        icon_cache_evict_oldest_owner(image);
    }
    while ((icon_cache_entry_count >= PT_ICON_CACHE_MAX_ENTRIES ||
            icon_cache_pixel_count + pixel_count > PT_ICON_CACHE_MAX_PIXELS) &&
           icon_lru_tail) {
        icon_cache_evict(icon_lru_tail);
    }
    return icon_cache_entry_count < PT_ICON_CACHE_MAX_ENTRIES &&
           icon_cache_pixel_count + pixel_count <= PT_ICON_CACHE_MAX_PIXELS;
}

static uint8_t icon_mul_u8(uint32_t a, uint32_t b) {
    return (uint8_t)((a * b + 127u) / 255u);
}

/* Bilinear resize of premultiplied ARGB8888 pixels. Premultiplied components
 * interpolate linearly, so no colour straightening is needed. */
static uint32_t *icon_resize(const pt_icon_image *image, int width, int height) {
    if (!image || width <= 0 || height <= 0 || !image->pixels) {
        return NULL;
    }
    size_t count = (size_t)width * (size_t)height;
    uint32_t *out = pt_alloc(count * sizeof(uint32_t));
    for (int y = 0; y < height; ++y) {
        float fy = ((float)y + 0.5f) * (float)image->height / (float)height - 0.5f;
        int y0 = (int)floorf(fy);
        float wy = fy - (float)y0;
        if (y0 < 0) {
            y0 = 0;
            wy = 0.0f;
        }
        int y1 = y0 + 1;
        if (y1 >= image->height) {
            y1 = y0;
            wy = 0.0f;
        }
        for (int x = 0; x < width; ++x) {
            float fx = ((float)x + 0.5f) * (float)image->width / (float)width - 0.5f;
            int x0 = (int)floorf(fx);
            float wx = fx - (float)x0;
            if (x0 < 0) {
                x0 = 0;
                wx = 0.0f;
            }
            int x1 = x0 + 1;
            if (x1 >= image->width) {
                x1 = x0;
                wx = 0.0f;
            }
            uint32_t p00 = image->pixels[(size_t)y0 * (size_t)image->width + (size_t)x0];
            uint32_t p10 = image->pixels[(size_t)y0 * (size_t)image->width + (size_t)x1];
            uint32_t p01 = image->pixels[(size_t)y1 * (size_t)image->width + (size_t)x0];
            uint32_t p11 = image->pixels[(size_t)y1 * (size_t)image->width + (size_t)x1];
            uint32_t result = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                float c00 = (float)((p00 >> shift) & 0xFFu);
                float c10 = (float)((p10 >> shift) & 0xFFu);
                float c01 = (float)((p01 >> shift) & 0xFFu);
                float c11 = (float)((p11 >> shift) & 0xFFu);
                float top = c00 + (c10 - c00) * wx;
                float bottom = c01 + (c11 - c01) * wx;
                float value = top + (bottom - top) * wy;
                uint32_t component = (uint32_t)(value + 0.5f);
                if (component > 255u) {
                    component = 255u;
                }
                result |= component << shift;
            }
            out[(size_t)y * (size_t)width + (size_t)x] = result;
        }
    }
    return out;
}

const uint32_t *pt_icon_image_raster(pt_icon_image *image, int pixel_width, int pixel_height,
                                     int *out_stride) {
    if (out_stride) {
        *out_stride = 0;
    }
    if (!image || pixel_width <= 0 || pixel_height <= 0 ||
        pixel_width > PT_ICON_MAX_DIMENSION || pixel_height > PT_ICON_MAX_DIMENSION) {
        return NULL;
    }
    for (pt_icon_cache_entry *entry = image->entries; entry; entry = entry->owner_next) {
        if (entry->width == pixel_width && entry->height == pixel_height) {
            icon_cache_touch(entry);
            if (out_stride) {
                *out_stride = entry->width;
            }
            return entry->pixels;
        }
    }
    size_t pixel_count = (size_t)pixel_width * (size_t)pixel_height;
    if (!icon_cache_make_room(image, pixel_count)) {
        return NULL;
    }
    uint32_t *pixels = icon_resize(image, pixel_width, pixel_height);
    if (!pixels) {
        return NULL;
    }
    pt_icon_cache_entry *entry = pt_alloc_zeroed(1, sizeof(*entry));
    entry->pixels = pixels;
    entry->width = pixel_width;
    entry->height = pixel_height;
    entry->pixel_count = pixel_count;
    entry->owner = image;

    entry->lru_next = icon_lru_head;
    if (icon_lru_head) {
        icon_lru_head->lru_prev = entry;
    }
    icon_lru_head = entry;
    if (!icon_lru_tail) {
        icon_lru_tail = entry;
    }
    entry->owner_next = image->entries;
    image->entries = entry;
    ++image->entry_count;
    ++icon_cache_entry_count;
    icon_cache_pixel_count += pixel_count;
    if (out_stride) {
        *out_stride = entry->width;
    }
    return entry->pixels;
}

/* ---------- drawing ---------- */

static uint32_t icon_blend(uint32_t dst, uint32_t src) {
    uint32_t alpha = src >> 24;
    if (alpha == 255u) {
        return src;
    }
    if (alpha == 0u) {
        return dst;
    }
    uint32_t inv = 255u - alpha;
    uint32_t out_alpha = alpha + (((dst >> 24) & 0xFFu) * inv + 127u) / 255u;
    uint32_t out_r = ((src >> 16) & 0xFFu) + ((((dst >> 16) & 0xFFu) * inv + 127u) / 255u);
    uint32_t out_g = ((src >> 8) & 0xFFu) + ((((dst >> 8) & 0xFFu) * inv + 127u) / 255u);
    uint32_t out_b = (src & 0xFFu) + (((dst & 0xFFu) * inv + 127u) / 255u);
    out_alpha = out_alpha > 255u ? 255u : out_alpha;
    out_r = out_r > 255u ? 255u : out_r;
    out_g = out_g > 255u ? 255u : out_g;
    out_b = out_b > 255u ? 255u : out_b;
    return (out_alpha << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

void pt_icon_draw_canvas(pt_canvas *canvas, pt_icon_image *image, float x, float y, float width,
                         float height, pt_color tint) {
    if (!canvas || !image || width <= 0.0f || height <= 0.0f) {
        return;
    }
    float scale = canvas->scale > 0.05f ? canvas->scale : 1.0f;
    int pixel_width = (int)lroundf(width * scale);
    int pixel_height = (int)lroundf(height * scale);
    if (pixel_width < 1) {
        pixel_width = 1;
    }
    if (pixel_height < 1) {
        pixel_height = 1;
    }
    int stride = 0;
    const uint32_t *src = pt_icon_image_raster(image, pixel_width, pixel_height, &stride);
    if (!src) {
        return;
    }
    int dest_x = (int)floorf(x * scale);
    int dest_y = (int)floorf(y * scale);
    int clip_x1 = (int)(canvas->clip.x + canvas->clip.w);
    int clip_y1 = (int)(canvas->clip.y + canvas->clip.h);
    int x0 = dest_x;
    int y0 = dest_y;
    int x1 = dest_x + pixel_width;
    int y1 = dest_y + pixel_height;
    if (x0 < (int)canvas->clip.x) {
        x0 = (int)canvas->clip.x;
    }
    if (y0 < (int)canvas->clip.y) {
        y0 = (int)canvas->clip.y;
    }
    if (x1 > clip_x1) {
        x1 = clip_x1;
    }
    if (y1 > clip_y1) {
        y1 = clip_y1;
    }
    if (x1 > canvas->width) {
        x1 = canvas->width;
    }
    if (y1 > canvas->height) {
        y1 = canvas->height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    uint32_t tint_alpha = tint & 0xFFu;
    bool tinted = tint_alpha != 0;
    uint32_t tint_r = (tint >> 24) & 0xFFu;
    uint32_t tint_g = (tint >> 16) & 0xFFu;
    uint32_t tint_b = (tint >> 8) & 0xFFu;
    for (int row = y0; row < y1; ++row) {
        uint32_t *dst = canvas->pixels + (size_t)row * (size_t)canvas->stride + (size_t)x0;
        const uint32_t *line = src + (size_t)(row - dest_y) * (size_t)stride +
                               (size_t)(x0 - dest_x);
        int count = x1 - x0;
        for (int i = 0; i < count; ++i) {
            uint32_t pixel = line[i];
            if (tinted) {
                uint32_t alpha = icon_mul_u8(pixel >> 24, tint_alpha);
                pixel = (alpha << 24) | (icon_mul_u8(tint_r, alpha) << 16) |
                        (icon_mul_u8(tint_g, alpha) << 8) | icon_mul_u8(tint_b, alpha);
            }
            dst[i] = icon_blend(dst[i], pixel);
        }
    }
}

/* ---------- loading ---------- */

/* Reads enough of the PNG header to reject an oversized image before the full
 * decode allocates width*height pixels. */
static bool icon_png_peek_dimensions(const uint8_t *data, size_t size, int *width, int *height) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!data || size < 24u || memcmp(data, signature, sizeof(signature)) != 0) {
        return false;
    }
    if (memcmp(data + 12, "IHDR", 4) != 0 || data[8] != 0 || data[9] != 0 ||
        data[10] != 0 || data[11] != 13) {
        return false;
    }
    uint32_t w = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
                 ((uint32_t)data[18] << 8) | (uint32_t)data[19];
    uint32_t h = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) |
                 ((uint32_t)data[22] << 8) | (uint32_t)data[23];
    if (w == 0 || h == 0 || w > (uint32_t)INT32_MAX || h > (uint32_t)INT32_MAX) {
        return false;
    }
    *width = (int)w;
    *height = (int)h;
    return true;
}

static pt_icon_image *icon_image_wrap(pt_color *pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) {
        free(pixels);
        return NULL;
    }
    pt_icon_image *image = pt_alloc_zeroed(1, sizeof(*image));
    image->references = 1;
    image->width = width;
    image->height = height;
    image->pixels = pixels;
    return image;
}

pt_icon_image *pt_icon_image_load_memory(const void *data, size_t size) {
    if (!data || size == 0 || size > pt_icon_input_max_bytes) {
        pt_set_error("invalid or oversized icon PNG data");
        return NULL;
    }
    int peek_width = 0;
    int peek_height = 0;
    if (!icon_png_peek_dimensions(data, size, &peek_width, &peek_height)) {
        pt_set_error("corrupt or unsupported icon PNG data");
        return NULL;
    }
    if (peek_width > PT_ICON_MAX_DIMENSION || peek_height > PT_ICON_MAX_DIMENSION) {
        pt_set_error("icon PNG dimensions exceed %d", PT_ICON_MAX_DIMENSION);
        return NULL;
    }
    int width = 0;
    int height = 0;
    pt_color *pixels = pt_png_decode_argb_premultiplied(data, size, &width, &height);
    if (!pixels) {
        return NULL;
    }
    if (width != peek_width || height != peek_height || width > PT_ICON_MAX_DIMENSION ||
        height > PT_ICON_MAX_DIMENSION) {
        free(pixels);
        pt_set_error("icon PNG dimensions are inconsistent");
        return NULL;
    }
    return icon_image_wrap(pixels, width, height);
}

pt_icon_image *pt_icon_image_load_file(const char *path) {
    if (!path || !*path) {
        pt_set_error("icon path is empty");
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        pt_set_error("cannot open icon '%s'", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        pt_set_error("cannot read icon '%s'", path);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || (unsigned long)length > pt_icon_input_max_bytes) {
        fclose(file);
        pt_set_error("icon '%s' is empty or oversized", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        pt_set_error("cannot read icon '%s'", path);
        return NULL;
    }
    size_t size = (size_t)length;
    uint8_t *data = pt_alloc(size);
    bool ok = fread(data, 1, size, file) == size;
    fclose(file);
    if (!ok) {
        free(data);
        pt_set_error("cannot read icon '%s'", path);
        return NULL;
    }
    pt_icon_image *image = pt_icon_image_load_memory(data, size);
    free(data);
    return image;
}

pt_icon_image *pt_icon_image_ref(pt_icon_image *image) {
    if (image) {
        ++image->references;
    }
    return image;
}

void pt_icon_image_destroy(pt_icon_image *image) {
    if (!image) {
        return;
    }
    if (image->references > 1) {
        --image->references;
        return;
    }
    while (image->entries) {
        icon_cache_evict(image->entries);
    }
    free(image->pixels);
    free(image);
}

int pt_icon_image_width(const pt_icon_image *image) {
    return image ? image->width : 0;
}

int pt_icon_image_height(const pt_icon_image *image) {
    return image ? image->height : 0;
}

/* ---------- hicolor lookup ---------- */

typedef struct icon_theme_dir {
    char name[256];
    int size;
} icon_theme_dir;

static bool icon_path_join(char *out, size_t capacity, const char *a, const char *b,
                           const char *c, const char *d) {
    int written = snprintf(out, capacity, "%s%s%s%s", a ? a : "", b ? b : "",
                           c ? c : "", d ? d : "");
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static bool icon_name_valid(const char *name) {
    if (!name || !*name) {
        return false;
    }
    size_t length = strlen(name);
    if (length > PT_ICON_NAME_MAX || name[0] == '.') {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch == '/' || ch == '\\' || ch < 0x20u) {
            return false;
        }
    }
    return true;
}

static int icon_base_dirs(char bases[PT_ICON_SEARCH_DIRS_MAX][PT_ICON_PATH_MAX]) {
    int count = 0;
    const char *data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char candidate[PT_ICON_PATH_MAX];
    if (data_home && *data_home) {
        if (icon_path_join(candidate, sizeof(candidate), data_home, "/icons", NULL, NULL)) {
            snprintf(bases[count++], PT_ICON_PATH_MAX, "%s", candidate);
        }
    } else if (home && *home) {
        if (icon_path_join(candidate, sizeof(candidate), home, "/.local/share/icons", NULL,
                           NULL)) {
            snprintf(bases[count++], PT_ICON_PATH_MAX, "%s", candidate);
        }
    }
    const char *data_dirs = getenv("XDG_DATA_DIRS");
    if (!data_dirs || !*data_dirs) {
        data_dirs = "/usr/local/share:/usr/share";
    }
    const char *cursor = data_dirs;
    while (*cursor && (unsigned)count < PT_ICON_SEARCH_DIRS_MAX) {
        const char *end = strchr(cursor, ':');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length > 0 && length < PT_ICON_PATH_MAX) {
            char base[PT_ICON_PATH_MAX];
            int written = snprintf(base, sizeof(base), "%.*s/icons", (int)length, cursor);
            if (written > 0 && (size_t)written < sizeof(base)) {
                bool duplicate = false;
                for (int i = 0; i < count; ++i) {
                    if (strcmp(bases[i], base) == 0) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    snprintf(bases[count++], PT_ICON_PATH_MAX, "%s", base);
                }
            }
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    if (home && *home && (unsigned)count < PT_ICON_SEARCH_DIRS_MAX) {
        if (icon_path_join(candidate, sizeof(candidate), home, "/.icons", NULL, NULL)) {
            bool duplicate = false;
            for (int i = 0; i < count; ++i) {
                if (strcmp(bases[i], candidate) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                snprintf(bases[count++], PT_ICON_PATH_MAX, "%s", candidate);
            }
        }
    }
    return count;
}

static int icon_theme_dir_compare(const void *left, const void *right, void *requested) {
    const icon_theme_dir *a = left;
    const icon_theme_dir *b = right;
    int want = *(const int *)requested;
    int da = a->size >= want ? a->size - want : want - a->size;
    int db = b->size >= want ? b->size - want : want - b->size;
    if (da != db) {
        return da < db ? -1 : 1;
    }
    if (a->size >= want && b->size < want) {
        return -1;
    }
    if (a->size < want && b->size >= want) {
        return 1;
    }
    return a->size < b->size ? -1 : (a->size > b->size ? 1 : 0);
}

static void icon_theme_add_dir(icon_theme_dir *dirs, int *count, const char *name, int size) {
    if (*count >= (int)PT_ICON_SEARCH_DIRS_MAX * 2 || size <= 0 || !name || !*name ||
        strlen(name) >= sizeof(dirs[0].name)) {
        return;
    }
    for (int i = 0; i < *count; ++i) {
        if (strcmp(dirs[i].name, name) == 0) {
            return;
        }
    }
    snprintf(dirs[*count].name, sizeof(dirs[*count].name), "%s", name);
    dirs[*count].size = size;
    ++*count;
}

/* Minimal index.theme reader: Directories= and one Size= per section. */
static int icon_theme_read_index(const char *theme_path, icon_theme_dir *dirs) {
    char path[PT_ICON_PATH_MAX];
    if (!icon_path_join(path, sizeof(path), theme_path, "/index.theme", NULL, NULL)) {
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return 0;
    }
    char line[512];
    char directories[2048];
    directories[0] = '\0';
    int count = 0;
    int pending_count = 0;
    char pending[PT_ICON_SEARCH_DIRS_MAX][256];
    char section[256] = {0};
    while (fgets(line, sizeof(line), file)) {
        char *text = line;
        while (*text == ' ' || *text == '\t') {
            ++text;
        }
        size_t length = strlen(text);
        while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
                              text[length - 1] == ' ' || text[length - 1] == '\t')) {
            text[--length] = '\0';
        }
        if (*text == '#') {
            continue;
        }
        if (*text == '[') {
            char *close = strchr(text, ']');
            if (close) {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", text + 1);
            }
            continue;
        }
        char *equals = strchr(text, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char *key = text;
        char *value = equals + 1;
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        if (strcmp(key, "Directories") == 0) {
            snprintf(directories, sizeof(directories), "%s", value);
        } else if (strcmp(key, "Size") == 0 && section[0]) {
            int size = atoi(value);
            icon_theme_add_dir(dirs, &count, section, size);
        }
    }
    fclose(file);
    if (count > 0) {
        return count;
    }
    /* index.theme may list Directories before the sections, or use a layout the
     * mini parser did not pair. Use the names and pick sizes from NxN. */
    char *cursor = directories;
    while (*cursor && pending_count < (int)(PT_ICON_SEARCH_DIRS_MAX * 2)) {
        char *comma = strchr(cursor, ',');
        size_t length = comma ? (size_t)(comma - cursor) : strlen(cursor);
        while (length > 0 && (*cursor == ' ' || *cursor == '\t')) {
            ++cursor;
            --length;
        }
        while (length > 0 && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t')) {
            --length;
        }
        if (length > 0 && length < sizeof(pending[0])) {
            snprintf(pending[pending_count], sizeof(pending[0]), "%.*s", (int)length, cursor);
            ++pending_count;
        }
        if (!comma) {
            break;
        }
        cursor = comma + 1;
    }
    for (int i = 0; i < pending_count; ++i) {
        const char *name = pending[i];
        const char *x = strchr(name, 'x');
        int size = x ? atoi(name) : 0;
        icon_theme_add_dir(dirs, &count, name, size);
    }
    return count;
}

static int icon_theme_scan(const char *theme_path, icon_theme_dir *dirs) {
    DIR *dir = opendir(theme_path);
    if (!dir) {
        return 0;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) && count < (int)PT_ICON_SEARCH_DIRS_MAX * 2) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const char *x = strchr(entry->d_name, 'x');
        if (!x) {
            continue;
        }
        char *end = NULL;
        long size = strtol(entry->d_name, &end, 10);
        if (end != x || size <= 0 || size > 4096) {
            continue;
        }
        char probe[PT_ICON_PATH_MAX];
        if (!icon_path_join(probe, sizeof(probe), theme_path, "/", entry->d_name, NULL)) {
            continue;
        }
        struct stat info;
        if (stat(probe, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        icon_theme_add_dir(dirs, &count, entry->d_name, (int)size);
    }
    closedir(dir);
    return count;
}

static pt_icon_image *icon_theme_load(const char *theme_path, const char *name,
                                      const icon_theme_dir *dirs, int dir_count, bool scanned) {
    static const char *const contexts[] = {"apps", "devices", "places", "mimetypes", "status",
                                           "actions"};
    for (int i = 0; i < dir_count; ++i) {
        char path[PT_ICON_PATH_MAX];
        if (!scanned) {
            if (!icon_path_join(path, sizeof(path), theme_path, "/", dirs[i].name, NULL)) {
                continue;
            }
            if (!icon_path_join(path + strlen(path), sizeof(path) - strlen(path), "/", name,
                                ".png", NULL)) {
                continue;
            }
            pt_icon_image *image = pt_icon_image_load_file(path);
            if (image) {
                return image;
            }
        } else {
            for (size_t c = 0; c < sizeof(contexts) / sizeof(contexts[0]); ++c) {
                if (!icon_path_join(path, sizeof(path), theme_path, "/", dirs[i].name, NULL)) {
                    continue;
                }
                if (!icon_path_join(path + strlen(path), sizeof(path) - strlen(path), "/",
                                    contexts[c], NULL, NULL)) {
                    continue;
                }
                if (!icon_path_join(path + strlen(path), sizeof(path) - strlen(path), "/", name,
                                    ".png", NULL)) {
                    continue;
                }
                pt_icon_image *image = pt_icon_image_load_file(path);
                if (image) {
                    return image;
                }
            }
        }
    }
    return NULL;
}

pt_icon_image *pt_icon_image_from_name(const char *name, int size) {
    if (!icon_name_valid(name)) {
        pt_set_error("invalid icon name");
        return NULL;
    }
    int requested = size > 0 ? size : 32;
    char bases[PT_ICON_SEARCH_DIRS_MAX][PT_ICON_PATH_MAX];
    int base_count = icon_base_dirs(bases);
    char theme_path[PT_ICON_PATH_MAX];
    for (int b = 0; b < base_count; ++b) {
        if (!icon_path_join(theme_path, sizeof(theme_path), bases[b], "/hicolor", NULL, NULL)) {
            continue;
        }
        icon_theme_dir dirs[PT_ICON_SEARCH_DIRS_MAX * 2];
        int dir_count = icon_theme_read_index(theme_path, dirs);
        bool scanned = false;
        if (dir_count == 0) {
            dir_count = icon_theme_scan(theme_path, dirs);
            scanned = true;
        }
        if (dir_count == 0) {
            continue;
        }
        qsort_r(dirs, (size_t)dir_count, sizeof(dirs[0]), icon_theme_dir_compare, &requested);
        pt_icon_image *image = icon_theme_load(theme_path, name, dirs, dir_count, scanned);
        if (image) {
            return image;
        }
    }
    pt_set_error("icon '%s' was not found in the hicolor theme", name);
    return NULL;
}

/* ---------- widget slot ---------- */

void pt_button_set_icon_image(pt_widget *button, pt_icon_image *image) {
    if (!button || button->kind != pt_widget_button) {
        return;
    }
    if (button->icon_image == image) {
        return;
    }
    pt_icon_image_ref(image);
    pt_icon_image_destroy(button->icon_image);
    button->icon_image = image;
    pt_widget_mark_dirty(button, true);
}

pt_icon_image *pt_button_get_icon_image(const pt_widget *button) {
    return button && button->kind == pt_widget_button ? button->icon_image : NULL;
}

void pt_button_set_icon_tint(pt_widget *button, pt_color tint) {
    if (!button || button->kind != pt_widget_button) {
        return;
    }
    button->icon_tint = tint;
    if (button->icon_image) {
        pt_widget_mark_dirty(button, false);
    }
}

/* ---------- window slot ---------- */

void pt_window_set_icon(pt_window *window, pt_icon_image *image) {
    if (!window) {
        return;
    }
    if (window->icon == image) {
        return;
    }
    pt_icon_image_ref(image);
    pt_icon_image_destroy(window->icon);
    window->icon = image;
    pt_platform_window_set_icon(window);
}

pt_icon_image *pt_window_get_icon(const pt_window *window) {
    return window ? window->icon : NULL;
}

bool pt_icon_window_buffer_geometry(int source_width, int source_height, float scale,
                                    int *out_size, int *out_scale) {
    if (out_size) {
        *out_size = 0;
    }
    if (out_scale) {
        *out_scale = 1;
    }
    if (source_width <= 0 || source_height <= 0 || !out_size || !out_scale) {
        return false;
    }
    int integer_scale = (int)lroundf(scale);
    if (integer_scale < 1) {
        integer_scale = 1;
    }
    if (integer_scale > PT_ICON_WINDOW_MAX_SCALE) {
        integer_scale = PT_ICON_WINDOW_MAX_SCALE;
    }
    /* xdg_toplevel_icon_v1 requires a square buffer. */
    *out_size = PT_ICON_WINDOW_SIZE * integer_scale;
    *out_scale = integer_scale;
    return true;
}
