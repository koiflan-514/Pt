#include "../src/internal.h"
#include "harness.h"

#include <string.h>

int main(void) {
    pt_check(pt_rgba(0x11, 0x22, 0x33, 0x44) == 0x11223344u);
    pt_check(pt_rgb(0x123456) == 0x123456FFu);
    pt_check(pt_color_alpha(0x11223344u, 0xAA) == 0x112233AAu);

    pt_color blended = pt_color_blend(0x000000FFu, 0xFFFFFFFFu, 0.5f);
    pt_check_near((blended >> 24) & 0xFFu, 127.0, 1.0);
    pt_check_near(blended & 0xFFu, 255.0, 0.0);

    pt_rect a = {0.0f, 0.0f, 10.0f, 10.0f};
    pt_rect b = {5.0f, 5.0f, 10.0f, 10.0f};
    pt_check(pt_rect_contains(a, 5.0f, 5.0f));
    pt_check(!pt_rect_contains(a, 10.0f, 5.0f));
    pt_check(pt_rect_intersects(a, b));
    pt_rect intersection = pt_rect_intersect(a, b);
    pt_check_near(intersection.x, 5.0, 0.001);
    pt_check_near(intersection.w, 5.0, 0.001);

    const char *text = "H\xC3\xA9\xE4\xB8\x96!";
    size_t len = strlen(text);
    size_t pos = 0;
    size_t next = 0;
    pt_check(pt_utf8_decode(text, len, pos, &next) == 'H');
    pt_check(next == 1);
    pos = next;
    pt_check(pt_utf8_decode(text, len, pos, &next) == 0xE9);
    pt_check(next == 3);
    pos = next;
    pt_check(pt_utf8_decode(text, len, pos, &next) == 0x4E16);
    pt_check(next == 6);
    pt_check(pt_utf8_count(text, len) == 4);
    size_t prev = len;
    pt_check(pt_utf8_decode_prev(text, prev, &prev) == '!');
    pt_check(prev == 6);
    pt_check(pt_utf8_decode_prev(text, prev, &prev) == 0x4E16);
    pt_check(prev == 3);
    pt_check(pt_utf8_is_cjk(0x4E16));
    pt_check(pt_utf8_is_cjk(0x3042));
    pt_check(!pt_utf8_is_cjk('A'));

    const char *invalid = "\xFF" "A";
    pos = 0;
    pt_check(pt_utf8_decode(invalid, 2, 0, &next) == 0xFFFD);
    pt_check(next == 1);
    pos = next;
    pt_check(pt_utf8_decode(invalid, 2, pos, &next) == 'A');

    pt_check(pt_ease_towards(0.0f, 1.0f, 10.0f, 1.0f) == 1.0f);
    pt_check(pt_now_ms() > 0);

    return pt_test_finish("core");
}
