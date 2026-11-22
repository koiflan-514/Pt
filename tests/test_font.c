#include "../src/internal.h"
#include "harness.h"

#include <stdlib.h>
#include <string.h>

static void test_face_cache(pt_font *first) {
    /* The face table is keyed by family, weight, slant and size, so the same
     * request must return the identical face instead of loading it twice. */
    pt_font *again = pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright,
                                     18.0f);
    pt_check(again == first);
    pt_font *bold = pt_font_acquire("sans-serif", pt_font_weight_bold, pt_font_slant_upright, 18.0f);
    pt_check(bold != NULL && bold != first);
    pt_check(pt_font_acquire("sans-serif", pt_font_weight_bold, pt_font_slant_upright, 18.0f) ==
             bold);
    /* Sub-pixel sizes are quantised, so they collapse onto one entry. */
    pt_check(pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright, 18.001f) ==
             first);

    enum { face_count = 64 };
    pt_font *faces[face_count];
    for (int i = 0; i < face_count; ++i) {
        faces[i] = pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright,
                                   20.0f + (float)i);
        pt_check(faces[i] != NULL);
    }
    /* Loading past the load factor rehashes the table; every face, including the
     * ones inserted first, must still resolve to the same pointer. */
    for (int i = 0; i < face_count; ++i) {
        pt_check(pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright,
                                 20.0f + (float)i) == faces[i]);
    }
    for (int i = 1; i < face_count; ++i) {
        pt_check(faces[i] != faces[i - 1]);
    }
    pt_check(faces[0] != faces[face_count - 1]);
    pt_check_near(pt_font_measure(faces[3], "Hello, Pt", strlen("Hello, Pt")),
                  pt_font_measure(pt_font_acquire("sans-serif", pt_font_weight_regular,
                                                  pt_font_slant_upright, 23.0f),
                                  "Hello, Pt", strlen("Hello, Pt")),
                  0.001f);
}

static void test_bidi_text(pt_font *font) {
    pt_font *dejavu = pt_font_acquire("DejaVu Sans", pt_font_weight_regular,
                                      pt_font_slant_upright, 18.0f);
    if (dejavu && pt_font_has_glyph(dejavu, 0x05D0u) && pt_font_has_glyph(dejavu, 0x0627u)) {
        font = dejavu;
    } else if (!pt_font_has_glyph(font, 0x05D0u) || !pt_font_has_glyph(font, 0x0627u)) {
        printf("skip font: no Hebrew/Arabic face; bidi shaping checks skipped\n");
        return;
    }
    const char *hebrew = "\xD7\x90\xD7\x91\xD7\x92";
    const char *mixed = "abc \xD7\x90\xD7\x91\xD7\x92";
    /* The LTR fast path must produce exactly the legacy width and run shape. */
    float hello = pt_font_measure(font, "Hello, Pt", strlen("Hello, Pt"));
    pt_check(pt_font_measure_bidi(font, "Hello, Pt", strlen("Hello, Pt"),
                                  pt_bidi_base_ltr) == hello);
    pt_font_run *runs = NULL;
    pt_check(pt_font_runs_bidi(font, "Hello, Pt", strlen("Hello, Pt"), pt_bidi_base_ltr,
                               &runs) == 1);
    pt_check(runs && runs[0].level == 0 && runs[0].direction == HB_DIRECTION_INVALID);
    pt_font_runs_free(runs);

    float he_rtl = pt_font_measure_bidi(font, hebrew, strlen(hebrew), pt_bidi_base_rtl);
    float he_ltr = pt_font_measure_bidi(font, hebrew, strlen(hebrew), pt_bidi_base_ltr);
    pt_check(he_rtl > 0.0f);
    pt_check_near(he_rtl, he_ltr, 0.001f); /* advances do not depend on direction */
    runs = NULL;
    pt_check(pt_font_runs_bidi(font, hebrew, strlen(hebrew), pt_bidi_base_auto, &runs) == 1);
    pt_check(runs && runs[0].level == 1 && runs[0].direction == HB_DIRECTION_RTL);
    pt_font_runs_free(runs);

    runs = NULL;
    pt_check(pt_font_runs_bidi(font, mixed, strlen(mixed), pt_bidi_base_ltr, &runs) == 2);
    pt_check(runs && runs[0].direction == HB_DIRECTION_LTR &&
             runs[1].direction == HB_DIRECTION_RTL);
    pt_check(runs && runs[0].end == 4 && runs[1].end == strlen(mixed));
    pt_font_runs_free(runs);
    pt_check(pt_font_measure_bidi(font, mixed, strlen(mixed), pt_bidi_base_ltr) > he_rtl);

    /* Arabic joining is confined to one UBA run. */
    const char *arabic = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    float joined = pt_font_measure_bidi(font, arabic, strlen(arabic), pt_bidi_base_rtl);
    float separate = pt_font_measure_bidi(font, "\xD8\xB3", 2, pt_bidi_base_rtl) +
                     pt_font_measure_bidi(font, "\xD9\x84", 2, pt_bidi_base_rtl) +
                     pt_font_measure_bidi(font, "\xD8\xA7", 2, pt_bidi_base_rtl) +
                     pt_font_measure_bidi(font, "\xD9\x85", 2, pt_bidi_base_rtl);
    pt_check(joined > 0.0f && joined + 1.0f < separate);
    runs = NULL;
    pt_check(pt_font_runs_bidi(font, arabic, strlen(arabic), pt_bidi_base_rtl, &runs) == 1);
    pt_check(runs && runs[0].direction == HB_DIRECTION_RTL);
    pt_font_runs_free(runs);

    /* Direction is part of the cache key: the two base directions coexist. */
    const char *cache = "RTL \xD7\x90\xD7\x91";
    size_t before = pt_font_shape_cache_count(font);
    (void)pt_font_measure_bidi(font, cache, strlen(cache), pt_bidi_base_ltr);
    size_t ltr_count = pt_font_shape_cache_count(font);
    (void)pt_font_measure_bidi(font, cache, strlen(cache), pt_bidi_base_rtl);
    pt_check(ltr_count > before);
    pt_check(pt_font_shape_cache_count(font) > ltr_count);
}

int main(void) {
    pt_font *font = pt_font_acquire("sans-serif", pt_font_weight_regular, pt_font_slant_upright, 18.0f);
    pt_check(font != NULL);
    if (!font) {
        fprintf(stderr, "font error: %s\n", pt_get_last_error());
        return pt_test_finish("font");
    }
    test_face_cache(font);
    pt_check(font->ascent > 0.0f);
    pt_check(font->line_height >= font->ascent);

    float hello_width = pt_font_measure(font, "Hello, Pt", strlen("Hello, Pt"));
    pt_check(hello_width > 10.0f);
    float cjk_width = pt_font_measure(font, "\xE4\xB8\x96\xE7\x95\x8C", 6);
    pt_check(cjk_width > 10.0f);

    pt_text_line *lines = NULL;
    const char *paragraph =
        "Pt is a small Wayland-native GUI framework with crisp text and a calm, modern look.";
    int line_count = pt_font_wrap_lines(font, paragraph, strlen(paragraph), 140.0f, &lines);
    pt_check(line_count >= 2);
    for (int i = 0; i < line_count; ++i) {
        pt_check(lines[i].end >= lines[i].start);
        float width = pt_font_measure(font, paragraph + lines[i].start,
                                      lines[i].end - lines[i].start);
        pt_check(width <= 141.0f);
    }
    free(lines);

    lines = NULL;
    line_count = pt_font_wrap_lines(font, "hello world\nsecond line", 23, 80.0f, &lines);
    pt_check(line_count >= 2);
    free(lines);

    float wrapped_height = pt_font_wrapped_height(font, paragraph, 140.0f);
    pt_check(wrapped_height >= font->line_height * 2.0f);

    test_bidi_text(font);

    pt_canvas *canvas = pt_canvas_create(520, 220, 1.0f);
    pt_check(canvas != NULL);
    pt_canvas_clear(canvas, 0x0B0E14FFu);
    pt_canvas_draw_text_line(canvas, font, "Hello, \xE4\xB8\x96\xE7\x95\x8C",
                             strlen("Hello, \xE4\xB8\x96\xE7\x95\x8C"), 24.0f, 54.0f, 300.0f,
                             pt_text_align_start, 0xFFFFFFFFu, true);
    pt_canvas_draw_text_wrapped(canvas, font, paragraph, 24.0f, 78.0f, 380.0f, 140.0f,
                                pt_text_align_start, 0xCCD6E4FFu);
    int painted = 0;
    for (int y = 0; y < canvas->height; ++y) {
        for (int x = 0; x < canvas->width; ++x) {
            if (pt_canvas_get_pixel(canvas, x, y) != 0x0B0E14FFu) {
                ++painted;
            }
        }
    }
    pt_check(painted > 400);
    pt_check(pt_canvas_write_png(canvas, "build/tests/font.png") == 0);

    pt_canvas_destroy(canvas);
    pt_font_manager_shutdown();
    return pt_test_finish("font");
}
