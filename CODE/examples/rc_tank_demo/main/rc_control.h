/**
 * @file rc_control.h
 * @brief RC Tank Demo - 控制层
 *
 * 坦克: 接收控制包 → 电机执行 + 安全停止监控
 * 遥控器: 摇杆解析 → 发送控制包
 */

#pragma once

#include "esp_err.h"
#include "rc_tank_common.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 坦克侧: 电机控制 ========== */

/**
 * @brief 初始化电机 (坦克用)
 * @return ESP_OK 成功
 */
esp_err_t rc_motor_init(void);

/**
 * @brief 应用连续角度/力度运动指令 (坦克用)
 * @param command 控制命令
 */
void rc_motor_apply(const rc_ctrl_command_t *command);

/**
 * @brief 强制停止电机 (坦克用)
 */
void rc_motor_stop(void);

/**
 * @brief 发布网络连接状态
 *
 * Tank断连时立即停车；Remote断连时暂停发送，重连后等待摇杆释放再解锁。
 */
void rc_control_set_network_connected(bool connected);

/**
 * @brief 启动控制接收任务 (坦克用)
 * @return ESP_OK 成功
 */
esp_err_t rc_control_start_tank(void);

/* ========== 遥控器侧: 摇杆控制 ========== */

/**
 * @brief 初始化摇杆 (遥控器用, LVGL 依赖)
 * @param parent LVGL 父对象
 * @return ESP_OK 成功
 */
esp_err_t rc_joystick_init(void *parent);

/**
 * @brief 启动控制发送任务 (遥控器用)
 * @return ESP_OK 成功
 */
esp_err_t rc_control_start_remote(void);

/**
 * @brief 获取当前摇杆连续命令 (遥控器用)
 * @param command [out] 当前规范化命令
 */
void rc_joystick_get_command(rc_ctrl_command_t *command);

/**
 * @brief 获取摇杆状态用于叠加绘制 (遥控器用)
 * @param knob_dx [out] 摇杆头 X 偏移（相对底座中心）
 * @param knob_dy [out] 摇杆头 Y 偏移（相对底座中心）
 * @param active  [out] 是否正在触摸
 */
void rc_joystick_get_state(int *knob_dx, int *knob_dy, bool *active);

#ifdef __cplusplus
}
#endif
