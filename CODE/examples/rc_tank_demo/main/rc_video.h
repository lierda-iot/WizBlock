/**
 * @file rc_video.h
 * @brief RC Tank Demo - 视频层
 *
 * 坦克: 摄像头采集 → JPEG 编码 → 网络发送
 * 遥控器: 网络接收 → JPEG 解码 → 显示
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 坦克侧: 视频采集与编码 ========== */

/**
 * @brief 初始化摄像头采集 (坦克用)
 * @return ESP_OK 成功
 */
esp_err_t rc_video_capture_init(void);

/** Enable the deterministic fallback source when the camera is unavailable. */
void rc_video_enable_synthetic(bool enabled);

/**
 * @brief 启动视频采集+编码+发送任务 (坦克用)
 * @return ESP_OK 成功
 */
esp_err_t rc_video_start_tank(void);

/* ========== 遥控器侧: 视频接收与显示 ========== */

/**
 * @brief 初始化视频显示 (遥控器用)
 * @return ESP_OK 成功
 */
esp_err_t rc_video_display_init(void);

/**
 * @brief Publish Tank WiFi state; the display task performs the actual redraw.
 */
void rc_video_set_network_connected(bool connected);

/**
 * @brief 启动视频接收+解码+显示任务 (遥控器用)
 * @return ESP_OK 成功
 */
esp_err_t rc_video_start_remote(void);

#ifdef __cplusplus
}
#endif
