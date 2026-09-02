/**
 * test_rc_tank_screen_render.c
 * 纯 C 测试 — rc_tank_screen.c 像素坦克渲染几何
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "rc_tank_screen.h"

/* 最小 assert */
#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

/* 引入实现（纯几何函数，无 ESP-IDF 依赖）*/
#include "rc_tank_screen.c"

/* 静态帧缓冲（避免大栈数组触发 Windows __chkstk_ms）*/
static uint16_t g_test_fb[RC_TANK_SCREEN_W * RC_TANK_SCREEN_H];

/* 测试辅助 — 检查颜色 */
static int color_at(uint16_t *fb, int w, int x, int y) {
    return (x >= 0 && x < w && y >= 0 && y < RC_TANK_SCREEN_H) ? fb[y * w + x] : -1;
}

/* 测试 1：背景全黑 */
static int test_background_black(void) {
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, -1);

    /* 检查四角都是黑色 */
    TEST_ASSERT(g_test_fb[0] == COLOR_BG);
    TEST_ASSERT(g_test_fb[RC_TANK_SCREEN_W - 1] == COLOR_BG);
    TEST_ASSERT(g_test_fb[(RC_TANK_SCREEN_H - 1) * RC_TANK_SCREEN_W] == COLOR_BG);
    TEST_ASSERT(g_test_fb[RC_TANK_SCREEN_H * RC_TANK_SCREEN_W - 1] == COLOR_BG);
    return 0;
}

/* 测试 2：像素坦克车体在屏幕中心 */
static int test_tank_body_centered(void) {
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, -1);

    int cx = RC_TANK_SCREEN_W / 2;  /* 160 */
    int cy = RC_TANK_SCREEN_H / 2;  /* 120 */

    /* 车体左侧（避开炮管与炮塔，检查车体颜色）*/
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, cx - 20, cy) == COLOR_TANK_BODY);
    return 0;
}

/* 测试 3：履带颜色正确 */
static int test_tank_tracks(void) {
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, -1);

    int cx = RC_TANK_SCREEN_W / 2;
    int cy = RC_TANK_SCREEN_H / 2;

    /* 上履带（cy - TANK_BODY_H/2 - TANK_TRACK_H/2）*/
    int track_y = cy - TANK_BODY_H/2 - TANK_TRACK_H/2;
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, cx, track_y) == COLOR_TANK_TRACK);

    /* 下履带（cy + TANK_BODY_H/2 + TANK_TRACK_H/2）*/
    track_y = cy + TANK_BODY_H/2 + TANK_TRACK_H/2;
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, cx, track_y) == COLOR_TANK_TRACK);
    return 0;
}

/* 测试 4：炮管向右伸出 */
static int test_gun_barrel_right(void) {
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, -1);

    int cx = RC_TANK_SCREEN_W / 2;
    int cy = RC_TANK_SCREEN_H / 2;

    /* 炮管起点（cx, cy）颜色应为 COLOR_TANK_GUN */
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, cx + 10, cy) == COLOR_TANK_GUN);
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, cx + 30, cy) == COLOR_TANK_GUN);
    return 0;
}

/* 测试 5：WiFi 图标位置（右上角）*/
static int test_wifi_icon_position(void) {
    /* WiFi 断开（红色）*/
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, -1);
    int icon_cx = WIFI_ICON_X + WIFI_ICON_W/2;
    int icon_cy = WIFI_ICON_Y + WIFI_ICON_H/2;
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, icon_cx, icon_cy) == 0x1F00);

    /* WiFi 连接（绿色）*/
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, true, -1);
    TEST_ASSERT(color_at(g_test_fb, RC_TANK_SCREEN_W, icon_cx, icon_cy) == 0xE007);
    return 0;
}

static int assert_glyph(int x, int y, const uint8_t glyph[7])
{
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            const bool expected = (glyph[row] & (uint8_t)(1U << (4 - col))) != 0;
            const int actual = color_at(g_test_fb, RC_TANK_SCREEN_W, x + col, y + row);
            TEST_ASSERT(actual == (expected ? COLOR_TEXT : COLOR_BG));
        }
    }
    return 0;
}

/* Test 9: the visible 75% status uses row-oriented 5x7 glyphs and a real %. */
static int test_battery_75_exact_bitmap(void)
{
    static const uint8_t glyph_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t glyph_5[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_percent[7] = {0x19, 0x19, 0x04, 0x08, 0x13, 0x13, 0x00};

    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, 75);
    TEST_ASSERT(assert_glyph(BATTERY_TEXT_X, BATTERY_TEXT_Y, glyph_7) == 0);
    TEST_ASSERT(assert_glyph(BATTERY_TEXT_X + 6, BATTERY_TEXT_Y, glyph_5) == 0);
    TEST_ASSERT(assert_glyph(BATTERY_TEXT_X + 12, BATTERY_TEXT_Y, glyph_percent) == 0);
    return 0;
}

/* 测试 6：电量百分比渲染（检查非零像素存在）*/
static int test_battery_percentage_render(void) {
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, 85);

    /* 电量文字区域应有白色像素（非黑色）*/
    int found_white = 0;
    for (int dy = 0; dy < 10; dy++) {
        for (int dx = 0; dx < 30; dx++) {
            int c = color_at(g_test_fb, RC_TANK_SCREEN_W, BATTERY_TEXT_X + dx, BATTERY_TEXT_Y + dy);
            if (c == COLOR_TEXT) {
                found_white = 1;
                break;
            }
        }
        if (found_white) break;
    }
    TEST_ASSERT(found_white == 1);
    return 0;
}

/* 测试 7：电量 -1 时不渲染 */
static int test_battery_negative_skips_render(void) {
    rc_tank_screen_render(g_test_fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H, false, -1);

    /* 电量文字区域应全黑（背景色）*/
    for (int dy = 0; dy < 10; dy++) {
        for (int dx = 0; dx < 30; dx++) {
            int c = color_at(g_test_fb, RC_TANK_SCREEN_W, BATTERY_TEXT_X + dx, BATTERY_TEXT_Y + dy);
            TEST_ASSERT(c == COLOR_BG);
        }
    }
    return 0;
}

/* 测试 8：边界越界保护（不崩溃）*/
static int test_fill_rect_bounds_protection(void) {
    uint16_t fb[100];  /* 小缓冲区 10×10 */
    memset(fb, 0xFF, sizeof(fb));

    /* 越界填充应不改变缓冲区 */
    fill_rect(fb, 10, 10, -5, -5, 10, 10, 0x0000);
    fill_rect(fb, 10, 10, 8, 8, 10, 10, 0x0000);

    /* 缓冲区应仍为 0xFF */
    TEST_ASSERT(fb[0] == 0xFFFF);
    TEST_ASSERT(fb[99] == 0xFFFF);
    return 0;
}

int main(void) {
    int failed = 0;
    failed += test_background_black();
    failed += test_tank_body_centered();
    failed += test_tank_tracks();
    failed += test_gun_barrel_right();
    failed += test_wifi_icon_position();
    failed += test_battery_percentage_render();
    failed += test_battery_negative_skips_render();
    failed += test_fill_rect_bounds_protection();
    failed += test_battery_75_exact_bitmap();
    return failed;
}
