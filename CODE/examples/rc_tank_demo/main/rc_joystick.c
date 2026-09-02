/**
 * @file rc_joystick.c
 * @brief RC Tank Demo - 摇杆逻辑与渲染
 */

#include "rc_joystick.h"
#include "rc_lcd_color.h"

static bool s_touch_captured = false;

/* ========== 纯逻辑函数 ========== */

#define RC_JOY_TAN_SCALE 1000000U

static const uint32_t s_tan_half_degree_thresholds[] = {
    8727U, 26186U, 43661U, 61163U, 78702U,
    96289U, 113936U, 131652U, 149451U, 167343U,
    185339U, 203452U, 221695U, 240079U, 258618U,
    277325U, 296213U, 315299U, 334595U, 354119U,
    373885U, 393910U, 414214U, 434812U, 455726U,
    476976U, 498582U, 520567U, 542956U, 565773U,
    589045U, 612801U, 637070U, 661886U, 687281U,
    713293U, 739961U, 767327U, 795436U, 824336U,
    854081U, 884725U, 916331U, 948965U, 982697U,
};

static uint32_t integer_sqrt_floor(uint32_t value)
{
    uint32_t result = 0U;
    uint32_t bit = 1UL << 30;

    while (bit > value) {
        bit >>= 2;
    }
    while (0U != bit) {
        if (value >= (result + bit)) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint32_t integer_sqrt_nearest(uint32_t value)
{
    const uint32_t floor_root = integer_sqrt_floor(value);
    const uint32_t midpoint_twice = (2U * floor_root) + 1U;
    return ((4U * value) > (midpoint_twice * midpoint_twice)) ?
        floor_root + 1U : floor_root;
}

static int16_t nearest_acute_angle(uint32_t horizontal,
                                   uint32_t vertical)
{
    uint32_t angle_deg = 0U;

    if (0U == horizontal) {
        return 0;
    }
    if (0U == vertical) {
        return 90;
    }
    if (horizontal > vertical) {
        return (int16_t)(90 - nearest_acute_angle(vertical, horizontal));
    }

    for (angle_deg = 0U;
         angle_deg < (uint32_t)(sizeof(s_tan_half_degree_thresholds) /
                                sizeof(s_tan_half_degree_thresholds[0]));
         ++angle_deg) {
        if (((uint64_t)horizontal * RC_JOY_TAN_SCALE) <
            ((uint64_t)vertical *
             s_tan_half_degree_thresholds[angle_deg])) {
            return (int16_t)angle_deg;
        }
    }
    return 45;
}

static int16_t angle_from_offset(int dx, int dy)
{
    const uint32_t horizontal = (uint32_t)((0 > dx) ? -dx : dx);
    const int forward = -dy;
    const uint32_t vertical = (uint32_t)((0 > forward) ? -forward : forward);
    const int16_t acute = nearest_acute_angle(horizontal, vertical);

    if (0 <= forward) {
        return (0 > dx) ? (int16_t)-acute : acute;
    }
    if (0 == dx) {
        return 180;
    }
    return (0 > dx) ? (int16_t)-(180 - acute) :
                      (int16_t)(180 - acute);
}

void rc_joystick_command_from_offset(int dx, int dy,
                                     rc_ctrl_command_t *command)
{
    uint32_t radius = 0U;
    uint32_t distance_squared = 0U;
    int magnitude_numerator = 0;

    if (NULL == command) {
        return;
    }
    *command = (rc_ctrl_command_t){
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 0,
        .magnitude_pct = 0U,
    };

    distance_squared = (uint32_t)((dx * dx) + (dy * dy));
    if (distance_squared <
        (uint32_t)(RC_JOY_DEADZONE * RC_JOY_DEADZONE)) {
        return;
    }
    radius = integer_sqrt_nearest(distance_squared);
    if (RC_JOY_MAX_TRAVEL < radius) {
        radius = RC_JOY_MAX_TRAVEL;
    }
    if (RC_JOY_DEADZONE > radius) {
        return;
    }

    command->angle_deg = angle_from_offset(dx, dy);
    magnitude_numerator = (int)(radius - RC_JOY_DEADZONE) *
                          (RC_CTRL_MAGNITUDE_MAX - 1);
    command->mode = RC_CTRL_MODE_DRIVE;
    command->magnitude_pct = (uint8_t)(1U +
        ((magnitude_numerator +
          ((RC_JOY_MAX_TRAVEL - RC_JOY_DEADZONE) / 2)) /
         (RC_JOY_MAX_TRAVEL - RC_JOY_DEADZONE)));
}

void rc_joystick_map_touch_to_screen(int raw_x, int raw_y,
                                     int *screen_x, int *screen_y)
{
    if (!screen_x || !screen_y) {
        return;
    }

    int mapped_x = (RC_JOY_SCREEN_W - 1) - raw_y;
    int mapped_y = raw_x;
    if (mapped_x < 0) mapped_x = 0;
    if (mapped_x >= RC_JOY_SCREEN_W) mapped_x = RC_JOY_SCREEN_W - 1;
    if (mapped_y < 0) mapped_y = 0;
    if (mapped_y >= RC_JOY_SCREEN_H) mapped_y = RC_JOY_SCREEN_H - 1;

    *screen_x = mapped_x;
    *screen_y = mapped_y;
}

void rc_joystick_clamp_offset(int *dx, int *dy, int max_travel)
{
    uint32_t distance_squared = 0U;
    uint32_t radius = 0U;
    const uint32_t max_squared = (uint32_t)(max_travel * max_travel);

    if (!dx || !dy) {
        return;
    }
    if (0 >= max_travel) {
        *dx = 0;
        *dy = 0;
        return;
    }

    distance_squared = (uint32_t)((*dx * *dx) + (*dy * *dy));
    if (max_squared >= distance_squared) {
        return;
    }

    radius = integer_sqrt_floor(distance_squared);
    if (0U == radius) {
        *dx = 0;
        *dy = 0;
        return;
    }
    *dx = (*dx * max_travel) / (int)radius;
    *dy = (*dy * max_travel) / (int)radius;

    while ((uint32_t)((*dx * *dx) + (*dy * *dy)) > max_squared) {
        const int absolute_dx = (0 > *dx) ? -*dx : *dx;
        const int absolute_dy = (0 > *dy) ? -*dy : *dy;
        int *component = (absolute_dx >= absolute_dy) ? dx : dy;
        *component += (0 < *component) ? -1 : 1;
    }
}

void rc_joystick_resolve_touch(bool touched, int raw_x, int raw_y,
                               rc_joystick_input_t *input)
{
    if (!input) {
        return;
    }

    input->knob_dx = 0;
    input->knob_dy = 0;
    input->active = false;
    input->command = (rc_ctrl_command_t){
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 0,
        .magnitude_pct = 0U,
    };
    if (!touched) {
        s_touch_captured = false;
        return;
    }

    int screen_x = -1;
    int screen_y = -1;
    rc_joystick_map_touch_to_screen(raw_x, raw_y, &screen_x, &screen_y);
    if (screen_x < 0 || screen_y < 0) {
        return;
    }

    const int touch_dx = screen_x - RC_JOY_BASE_CX;
    const int touch_dy = screen_y - RC_JOY_BASE_CY;
    const int touch_r_sq = RC_JOY_TOUCH_R * RC_JOY_TOUCH_R;
    if (!s_touch_captured &&
        (touch_dx * touch_dx + touch_dy * touch_dy) > touch_r_sq) {
        return;
    }
    s_touch_captured = true;

    input->knob_dx = touch_dx;
    input->knob_dy = touch_dy;
    rc_joystick_clamp_offset(&input->knob_dx, &input->knob_dy,
                             RC_JOY_MAX_TRAVEL);
    input->active = true;
    rc_joystick_command_from_offset(input->knob_dx,
                                    input->knob_dy,
                                    &input->command);
}

/* ========== 渲染函数 ========== */

#define COLOR_BASE_FILL    rc_lcd_rgb565_be(8, 16, 8)    // 半透明深绿（底座填充）
#define COLOR_BASE_STROKE  rc_lcd_rgb565_be(16, 32, 16)  // 绿色边缘（底座描边）
#define COLOR_KNOB_IDLE    rc_lcd_rgb565_be(20, 40, 20)  // 摇杆头（未激活）
#define COLOR_KNOB_ACTIVE  rc_lcd_rgb565_be(31, 63, 10)  // 摇杆头（激活，亮黄绿）

// 简单 alpha 混合（alpha 固定，避免浮点）
// dst = dst * (1 - alpha) + src * alpha, alpha = 1/2 简化为 (dst + src) / 2
static inline uint16_t blend_50(uint16_t bg, uint16_t fg)
{
    // LCD BGR565 大端：先还原 wire word，再按逻辑 RGB 通道混合。
    uint16_t bg_native = (uint16_t)((bg >> 8) | (bg << 8));
    uint16_t fg_native = (uint16_t)((fg >> 8) | (fg << 8));
    uint16_t r_bg = bg_native & 0x1F;
    uint16_t g_bg = (bg_native >> 5) & 0x3F;
    uint16_t b_bg = (bg_native >> 11) & 0x1F;
    uint16_t r_fg = fg_native & 0x1F;
    uint16_t g_fg = (fg_native >> 5) & 0x3F;
    uint16_t b_fg = (fg_native >> 11) & 0x1F;

    uint16_t r = (r_bg + r_fg) >> 1;
    uint16_t g = (g_bg + g_fg) >> 1;
    uint16_t b = (b_bg + b_fg) >> 1;

    return rc_lcd_rgb565_be(r, g, b);
}

static void draw_circle_filled_region(uint16_t *fb, int w, int h,
                                      int origin_x, int origin_y,
                                      int cx, int cy, int r,
                                      uint16_t color, bool blend)
{
    for (int dy = -r; dy <= r; dy++) {
        const int global_y = cy + dy;
        const int local_y = global_y - origin_y;
        if (local_y < 0 || local_y >= h) {
            continue;
        }

        int dx_max = 0;
        for (int dx = 0; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                dx_max = dx;
            } else {
                break;
            }
        }

        for (int dx = -dx_max; dx <= dx_max; dx++) {
            const int local_x = cx + dx - origin_x;
            if (local_x < 0 || local_x >= w) {
                continue;
            }

            const int idx = local_y * w + local_x;
            if (blend) {
                fb[idx] = blend_50(fb[idx], color);
            } else {
                fb[idx] = color;
            }
        }
    }
}

void rc_joystick_render_overlay_region(uint16_t *fb, int w, int h,
                                       int origin_x, int origin_y,
                                       int knob_dx, int knob_dy, bool active)
{
    if (!fb || w <= 0 || h <= 0) {
        return;
    }

    draw_circle_filled_region(fb, w, h, origin_x, origin_y,
                              RC_JOY_BASE_CX, RC_JOY_BASE_CY,
                              RC_JOY_BASE_R, COLOR_BASE_FILL, true);
    draw_circle_filled_region(fb, w, h, origin_x, origin_y,
                              RC_JOY_BASE_CX, RC_JOY_BASE_CY,
                              RC_JOY_BASE_R - 2, COLOR_BASE_STROKE, true);

    const int knob_x = RC_JOY_BASE_CX + knob_dx;
    const int knob_y = RC_JOY_BASE_CY + knob_dy;
    const uint16_t knob_color = active ? COLOR_KNOB_ACTIVE : COLOR_KNOB_IDLE;
    draw_circle_filled_region(fb, w, h, origin_x, origin_y,
                              knob_x, knob_y, RC_JOY_KNOB_R,
                              knob_color, false);
}

void rc_joystick_render_overlay(uint16_t *fb, int w, int h,
                                int knob_dx, int knob_dy, bool active)
{
    if (!fb || w != RC_JOY_SCREEN_W || h != RC_JOY_SCREEN_H) {
        return;  // 安全检查
    }

    rc_joystick_render_overlay_region(fb, w, h, 0, 0,
                                       knob_dx, knob_dy, active);
}
