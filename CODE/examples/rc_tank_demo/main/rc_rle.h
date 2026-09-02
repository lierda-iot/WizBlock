/**
 * @file rc_rle.h
 * @brief RC Tank Demo - RLE 差分编码/解码（P2 性能优化方案）
 *
 * 用于视频帧间差分压缩，静止区域高压缩比，动态区域退化为原始传输。
 * Tank 侧编码当前帧与上一帧的差异，Remote 侧解码差分并叠加到本地缓存帧。
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RLE 差分编码（Tank 侧）
 *
 * 计算当前帧与前一帧的差异，对连续相同像素进行 RLE 压缩。
 * 编码格式：[控制字节][数据...]
 *   - 控制字节最高位=0：低 7 位表示重复次数（1-128），后跟 1 个像素值（2 字节）
 *   - 控制字节最高位=1：低 7 位表示原始像素数（1-128），后跟对应数量像素（每个 2 字节）
 *
 * @param current 当前帧缓冲（RGB565 大端，320×240×2 字节）
 * @param previous 前一帧缓冲（RGB565 大端，320×240×2 字节，首帧传 NULL）
 * @param output 输出编码缓冲（需预分配足够空间，最坏情况约 153KB）
 * @param len 当前帧字节数（通常为 320×240×2 = 153600）
 * @return 编码后字节数；0 表示失败
 */
size_t rle_encode_diff(const uint8_t *current, const uint8_t *previous,
                       uint8_t *output, size_t len);

/**
 * @brief RLE 差分解码（Remote 侧）
 *
 * 将接收到的差分数据叠加到本地缓存帧，恢复当前完整帧。
 *
 * @param input 编码数据缓冲
 * @param output 输出帧缓冲（RGB565 大端，320×240×2 字节）
 * @param previous 本地缓存的前一帧（RGB565 大端，首帧传 NULL）
 * @param input_len 编码数据长度
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG 参数错误；ESP_FAIL 解码失败
 */
esp_err_t rle_decode_diff(const uint8_t *input, uint8_t *output,
                          const uint8_t *previous, size_t input_len);

#ifdef __cplusplus
}
#endif
