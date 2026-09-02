/**
 * @file rc_joystick.h
 * @brief RC Tank Demo - 遥控器悬浮摇杆 (REQ-035-005)
 *
 * 纯逻辑方向映射 + 摇杆状态 + RGB565 帧缓冲叠加绘制。
 * 显示层级: 视频铺底 → 摇杆叠加在上（合成到解码后帧缓冲，再刷 LCD）。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "rc_ctrl_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 摇杆几何参数 (横屏 320×240, REQ-035-005) ===== */
#define RC_JOY_SCREEN_W     320
#define RC_JOY_SCREEN_H     240
#define RC_JOY_BASE_CX      108   /* 左下区域，给边缘操作留出余量 */
#define RC_JOY_BASE_CY      136   /* 上移，避免摇杆头触及屏幕底边 */
#define RC_JOY_BASE_R       72
#define RC_JOY_KNOB_R       34
#define RC_JOY_DEADZONE     10    /* 死区半径（< 10px 视为停止）*/
#define RC_JOY_MAX_TRAVEL   62
#define RC_JOY_TOUCH_R      108   /* 初次按下捕获半径；捕获后允许拖出此范围 */
#define RC_JOY_TOUCH_PANEL_W 240  /* 原始触摸面板宽（竖屏） */
#define RC_JOY_TOUCH_PANEL_H 320  /* 原始触摸面板高（竖屏） */

typedef struct {
    int knob_dx;
    int knob_dy;
    bool active;
    rc_ctrl_command_t command;
} rc_joystick_input_t;

/** @brief 将摇杆偏移解析为硬件无关的连续角度/力度命令。 */
void rc_joystick_command_from_offset(int dx, int dy,
                                     rc_ctrl_command_t *command);

/**
 * @brief 将竖屏触摸原始坐标映射为横屏显示坐标（纯逻辑）
 * @param raw_x 原始触摸 X（0..239）
 * @param raw_y 原始触摸 Y（0..319）
 * @param screen_x [out] 横屏 X（0..319）
 * @param screen_y [out] 横屏 Y（0..239）
 */
void rc_joystick_map_touch_to_screen(int raw_x, int raw_y,
                                     int *screen_x, int *screen_y);

/**
 * @brief 将一次物理触摸解析为完整摇杆状态（纯逻辑）
 * @param touched 当前是否有触摸
 * @param raw_x   原始触摸 X
 * @param raw_y   原始触摸 Y
 * @param input   [out] 偏移、激活状态和连续命令；释放时回中 STOP
 */
void rc_joystick_resolve_touch(bool touched, int raw_x, int raw_y,
                               rc_joystick_input_t *input);

/**
 * @brief 限制摇杆头偏移在最大行程内（纯逻辑）
 * @param dx        [in/out] X 偏移
 * @param dy        [in/out] Y 偏移
 * @param max_travel 最大行程半径
 */
void rc_joystick_clamp_offset(int *dx, int *dy, int max_travel);

/**
 * @brief 将摇杆叠加绘制到 RGB565 帧缓冲（大端，匹配 LCD 输出）
 * @param fb       帧缓冲 (RGB565_BE, w*h)
 * @param w        帧宽
 * @param h        帧高
 * @param knob_dx  摇杆头相对底座中心的 X 偏移（已 clamp）
 * @param knob_dy  摇杆头相对底座中心的 Y 偏移（已 clamp）
 * @param active   是否正在触摸（true 时摇杆头高亮）
 */
void rc_joystick_render_overlay(uint16_t *fb, int w, int h,
                                int knob_dx, int knob_dy, bool active);

/**
 * @brief 将摇杆叠加绘制到一个 LCD 分块区域。
 * @param origin_x 分块左上角在整屏中的 X
 * @param origin_y 分块左上角在整屏中的 Y
 */
void rc_joystick_render_overlay_region(uint16_t *fb, int w, int h,
                                       int origin_x, int origin_y,
                                       int knob_dx, int knob_dy, bool active);

#ifdef __cplusplus
}
#endif
