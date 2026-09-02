/**
 * rc_tank_screen.h
 * Tank 侧屏幕显示（像素坦克 + WiFi 状态 + 电量）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RC_TANK_SCREEN_W  320
#define RC_TANK_SCREEN_H  240

/**
 * 渲染像素坦克到 RGB565 帧缓冲
 * @param fb     RGB565 framebuffer (320×240)
 * @param w      帧缓冲宽度
 * @param h      帧缓冲高度
 * @param wifi_connected  WiFi 连接状态
 * @param battery_percent 电量百分比 (0-100)，-1 表示不可用
 */
void rc_tank_screen_render(uint16_t *fb, int w, int h, bool wifi_connected, int battery_percent);
