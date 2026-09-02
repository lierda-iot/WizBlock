/**
 * @file rc_audio.h
 * @brief RC Tank Demo - 音频层接口
 *
 * P4: 遥控器录音→编码发送 / 坦克接收解码→播放
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 遥控器侧：录音与编码 ========== */

/**
 * @brief 初始化录音子系统（ES7210 ADC + SW3 按键检测）
 * @return ESP_OK on success
 */
esp_err_t rc_audio_record_init(void);

/**
 * @brief 查询 SW3 按键状态（GPIO8 ADC）
 * @return true=按下(<3000), false=松开(>3500)
 */
bool rc_audio_sw3_pressed(void);

/**
 * @brief 录音→编码→发送（阻塞至 SW3 松开或达到最大时长）
 * @return ESP_OK on success
 * @note 时长限制 0.5s ~ 10s
 */
esp_err_t rc_audio_record_and_send(void);

/* ========== 坦克侧：解码与播放 ========== */

/**
 * @brief 初始化播放子系统（ES8311 DAC）
 * @return ESP_OK on success
 */
esp_err_t rc_audio_play_init(void);

/**
 * @brief 启动接收→解码→播放任务
 * @return ESP_OK on success
 */
esp_err_t rc_audio_play_start(void);

#ifdef __cplusplus
}
#endif
