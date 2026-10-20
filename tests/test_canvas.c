#include "../src/internal.h"
#include "harness.h"

#include <stdio.h>

static uint32_t alpha_of(pt_canvas *canvas, int x, int y) {
    return pt_canvas_get_pixel(canvas, x, y) & 0xFFu;
}

static uint64_t snapshot_hash(const pt_canvas *canvas) {
    uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < canvas->height; ++y) {
        for (int x = 0; x < canvas->width; ++x) {
            hash ^= pt_canvas_get_pixel(canvas, x, y);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

/* Golden snapshot of the anti-aliased rounded edge produced by the
 * squared-distance fast path. Values change only if the coverage model changes. */
static void test_rounded_edge_snapshot(void) {
    pt_canvas *canvas = pt_canvas_create(40, 32, 1.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        return;
    }
    pt_canvas_clear(canvas, 0x00000000u);
    pt_canvas_fill_rounded_rect(canvas, 4.25f, 3.75f, 28.5f, 22.25f, 9.5f, 0xFFFFFFFFu);
    pt_canvas_stroke_rounded_rect(canvas, 6.5f, 6.0f, 20.0f, 14.0f, 5.0f, 2.0f, 0xFF0000FFu);

    pt_check(snapshot_hash(canvas) == 0x3c5a15ffeb755e03ull);
    pt_check(alpha_of(canvas, 1, 1) == 0);
    pt_check(alpha_of(canvas, 20, 16) == 255);
    pt_check(pt_canvas_get_pixel(canvas, 8, 15) == 0xFF7575FFu);
    pt_check(alpha_of(canvas, 12, 3) == 43);
    pt_check(alpha_of(canvas, 13, 3) == 63);
    pt_check(alpha_of(canvas, 9, 4) == 69);
    pt_check(alpha_of(canvas, 10, 4) == 170);
    pt_check(alpha_of(canvas, 11, 4) == 246);
    pt_check(alpha_of(canvas, 7, 5) == 11);
    pt_check(alpha_of(canvas, 8, 5) == 163);
    pt_check(alpha_of(canvas, 6, 6) == 24);
    pt_check(alpha_of(canvas, 7, 6) == 204);
    pt_check(alpha_of(canvas, 6, 7) == 190);
    pt_check(alpha_of(canvas, 5, 8) == 122);
    pt_check(alpha_of(canvas, 4, 9) == 5);
    pt_check(alpha_of(canvas, 5, 9) == 239);
    pt_check(alpha_of(canvas, 4, 10) == 89);
    pt_check(alpha_of(canvas, 4, 11) == 149);
    pt_check(alpha_of(canvas, 4, 12) == 184);
    pt_check(alpha_of(canvas, 4, 13) == 191);

    /* Every AA step stays monotonic along the corner, so the fast path cannot
     * skip the band or clamp it to a hard edge. */
    pt_check(alpha_of(canvas, 5, 8) < alpha_of(canvas, 5, 9));
    pt_check(alpha_of(canvas, 5, 9) < alpha_of(canvas, 5, 10));
    pt_check(alpha_of(canvas, 12, 3) < alpha_of(canvas, 11, 4));
    pt_canvas_destroy(canvas);
}

int main(void) {
    pt_canvas *canvas = pt_canvas_create(64, 64, 2.0f);
    pt_check(canvas != NULL);
    if (!canvas) {
        return pt_test_finish("canvas");
    }

    pt_canvas_clear(canvas, 0x101820FFu);
    pt_canvas_fill_rect(canvas, 10.0f, 10.0f, 10.0f, 10.0f, 0xFF0000FFu);
    pt_check(pt_canvas_get_pixel(canvas, 25, 25) == 0xFF0000FFu);
    pt_check(pt_canvas_get_pixel(canvas, 18, 18) == 0x101820FFu);

    pt_canvas_clear(canvas, 0x0000FFFFu);
    pt_canvas_fill_rect(canvas, 4.0f, 4.0f, 20.0f, 20.0f, 0xFF000080u);
    uint32_t blended = pt_canvas_get_pixel(canvas, 20, 20);
    pt_check(alpha_of(canvas, 20, 20) == 255);
    pt_check(((blended >> 24) & 0xFFu) > 100);
    pt_check(((blended >> 8) & 0xFFu) > 100);

    pt_canvas_clear(canvas, 0xFFFFFFFFu);
    pt_canvas_fill_rounded_rect(canvas, 8.0f, 8.0f, 20.0f, 20.0f, 10.0f, 0x00FF00FFu);
    pt_check(pt_canvas_get_pixel(canvas, 36, 36) == 0x00FF00FFu);
    pt_check(pt_canvas_get_pixel(canvas, 17, 17) == 0xFFFFFFFFu);

    pt_canvas_clear(canvas, 0x000000FFu);
    pt_canvas_push_clip(canvas, 5.0f, 5.0f, 5.0f, 5.0f);
    pt_canvas_fill_rect(canvas, 0.0f, 0.0f, 30.0f, 30.0f, 0xFFFFFFFFu);
    pt_canvas_pop_clip(canvas);
    pt_check(pt_canvas_get_pixel(canvas, 10, 10) == 0xFFFFFFFFu);
    pt_check(pt_canvas_get_pixel(canvas, 3, 10) == 0x000000FFu);
    pt_check(pt_canvas_get_pixel(canvas, 22, 22) == 0x000000FFu);

    pt_canvas_clear(canvas, 0x00000000u);
    pt_canvas_draw_shadow(canvas, 20.0f, 20.0f, 24.0f, 20.0f, 8.0f, 6.0f, 0x00000080u);
    pt_check(pt_canvas_get_pixel(canvas, 32, 30) != 0);
    pt_check(pt_canvas_get_pixel(canvas, 2, 2) == 0);

    pt_canvas_clear(canvas, 0x202830FFu);
    pt_canvas_fill_circle(canvas, 16.0f, 16.0f, 12.0f, pt_rgb(0xFFFFFF));
    pt_check(pt_canvas_get_pixel(canvas, 32, 32) == 0xFFFFFFFFu);

    test_rounded_edge_snapshot();

    int status = pt_canvas_write_png(canvas, "build/tests/canvas.png");
    pt_check(status == 0);
    FILE *file = fopen("build/tests/canvas.png", "rb");
    pt_check(file != NULL);
    if (file) {
        unsigned char signature[4] = {0};
        size_t read = fread(signature, 1, 4, file);
        fclose(file);
        pt_check(read == 4 && signature[0] == 0x89 && signature[1] == 'P' &&
                 signature[2] == 'N' && signature[3] == 'G');
    }

    pt_canvas_destroy(canvas);
    return pt_test_finish("canvas");
}
