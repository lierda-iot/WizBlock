/**
 * rc_tank_screen.c
 * Tank 侧屏幕像素坦克渲染（纯几何，可独立测试）
 */
#include "rc_tank_screen.h"
#include "rc_lcd_color.h"
#include <string.h>

/* Colors are logical RGB values encoded for the LCD BGR565 wire contract. */
#define COLOR_BG         rc_lcd_rgb565_be(0, 0, 0)
#define COLOR_TANK_BODY  rc_lcd_rgb565_be(15, 31, 15)
#define COLOR_TANK_TRACK rc_lcd_rgb565_be(7, 31, 7)
#define COLOR_TANK_GUN   rc_lcd_rgb565_be(31, 32, 0)
#define COLOR_WIFI_ON    rc_lcd_rgb565_be(0, 63, 0)
#define COLOR_WIFI_OFF   rc_lcd_rgb565_be(31, 0, 0)
#define COLOR_TEXT       rc_lcd_rgb565_be(31, 63, 31)

/* 像素坦克几何（屏幕中心）*/
#define TANK_BODY_W   60
#define TANK_BODY_H   40
#define TANK_TURRET_W 30
#define TANK_TURRET_H 20
#define TANK_GUN_W    40
#define TANK_GUN_H    6
#define TANK_TRACK_W  TANK_BODY_W
#define TANK_TRACK_H  8

/* WiFi 图标（右上角）*/
#define WIFI_ICON_X   280
#define WIFI_ICON_Y   10
#define WIFI_ICON_W   20
#define WIFI_ICON_H   15

/* 电量文字（右上角 WiFi 下方）*/
#define BATTERY_TEXT_X  265
#define BATTERY_TEXT_Y  35

/**
 * 填充矩形（纯函数）
 */
static void fill_rect(uint16_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > fb_w || y + h > fb_h) {
        return;  /* 越界保护 */
    }
    for (int dy = 0; dy < h; dy++) {
        uint16_t *row = fb + (y + dy) * fb_w + x;
        for (int dx = 0; dx < w; dx++) {
            row[dx] = color;
        }
    }
}

/**
 * 绘制 5×7 点阵简单数字（0-9）
 * font: 5×7 bitmap per digit (7 rows × 5 cols)
 * 每行一个字节，bit4=最左列，bit0=最右列（MSB-first列顺序）
 */
static const uint8_t font_5x7[10][7] = {
    /* 0 */ {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F},
    /* 1 */ {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    /* 2 */ {0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F},
    /* 3 */ {0x1F, 0x01, 0x01, 0x1F, 0x01, 0x01, 0x1F},
    /* 4 */ {0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01},
    /* 5 */ {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    /* 6 */ {0x0F, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    /* 7 */ {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    /* 8 */ {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    /* 9 */ {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x1E},
};

static const uint8_t font_percent[7] = {0x19, 0x19, 0x04, 0x08, 0x13, 0x13, 0x00};

/**
 * 绘制单个数字（5×7）
 * 修正：使用 MSB-first 位序（bit4=col0, bit0=col4）
 */
static void draw_digit(uint16_t *fb, int fb_w, int fb_h, int x, int y, int digit, uint16_t color)
{
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = font_5x7[digit][row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {  /* MSB-first: bit4→col0, bit0→col4 */
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h) {
                    fb[py * fb_w + px] = color;
                }
            }
        }
    }
}

static void draw_battery_percent(uint16_t *fb, int fb_w, int fb_h,
                                  int x, int y, int num, uint16_t color)
{
    if (num < 0) num = 0;
    if (num > 100) num = 100;

    if (num == 100) {
        draw_digit(fb, fb_w, fb_h, x, y, 1, color);
        draw_digit(fb, fb_w, fb_h, x + 6, y, 0, color);
        draw_digit(fb, fb_w, fb_h, x + 12, y, 0, color);
        return;
    }

    draw_digit(fb, fb_w, fb_h, x, y, num / 10, color);
    draw_digit(fb, fb_w, fb_h, x + 6, y, num % 10, color);
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = font_percent[row];
        for (int col = 0; col < 5; ++col) {
            if (bits & (uint8_t)(1U << (4 - col))) {
                int px = x + 12 + col;
                int py = y + row;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h) {
                    fb[py * fb_w + px] = color;
                }
            }
        }
    }
}

/**
 * 渲染像素坦克到帧缓冲（REQ-035-006B）
 */
void rc_tank_screen_render(uint16_t *fb, int w, int h, bool wifi_connected, int battery_percent)
{
    /* 1. 黑色背景 */
    memset(fb, 0x00, w * h * sizeof(uint16_t));

    /* 2. 像素坦克（屏幕中心）*/
    int cx = w / 2;
    int cy = h / 2;

    /* 上履带 */
    fill_rect(fb, w, h, cx - TANK_TRACK_W/2, cy - TANK_BODY_H/2 - TANK_TRACK_H,
              TANK_TRACK_W, TANK_TRACK_H, COLOR_TANK_TRACK);
    /* 下履带 */
    fill_rect(fb, w, h, cx - TANK_TRACK_W/2, cy + TANK_BODY_H/2,
              TANK_TRACK_W, TANK_TRACK_H, COLOR_TANK_TRACK);
    /* 车体 */
    fill_rect(fb, w, h, cx - TANK_BODY_W/2, cy - TANK_BODY_H/2,
              TANK_BODY_W, TANK_BODY_H, COLOR_TANK_BODY);
    /* 炮塔 */
    fill_rect(fb, w, h, cx - TANK_TURRET_W/2, cy - TANK_TURRET_H/2,
              TANK_TURRET_W, TANK_TURRET_H, COLOR_TANK_BODY);
    /* 炮管（向右伸出）*/
    fill_rect(fb, w, h, cx, cy - TANK_GUN_H/2,
              TANK_GUN_W, TANK_GUN_H, COLOR_TANK_GUN);

    /* 3. WiFi 状态图标（右上角，简单矩形）*/
    uint16_t wifi_color = wifi_connected ? COLOR_WIFI_ON : COLOR_WIFI_OFF;
    fill_rect(fb, w, h, WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_W, WIFI_ICON_H, wifi_color);

    /* 4. 电量百分比（WiFi 下方，5×7 点阵两位数 + '%'）*/
    if (battery_percent >= 0 && battery_percent <= 100) {
        draw_battery_percent(fb, w, h, BATTERY_TEXT_X, BATTERY_TEXT_Y,
                             battery_percent, COLOR_TEXT);
    }
}
