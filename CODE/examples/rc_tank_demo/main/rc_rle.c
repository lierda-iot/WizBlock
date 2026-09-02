/**
 * @file rc_rle.c
 * @brief RC Tank Demo - RLE 差分编码/解码实现
 */

#include "rc_rle.h"
#include <string.h>

#define RLE_MAX_RUN 127  // 最高位用作标志位，低 7 位最大值
#define RLE_LITERAL_FLAG 0x80

// RGB565 像素比较（大端格式，按 16 位比较）
static inline int pixels_equal(const uint8_t *p1, const uint8_t *p2)
{
    return (p1[0] == p2[0]) && (p1[1] == p2[1]);
}

size_t rle_encode_diff(const uint8_t *current, const uint8_t *previous,
                       uint8_t *output, size_t len)
{
    if (!current || !output || len == 0 || (len % 2) != 0) {
        return 0;
    }

    const size_t pixel_count = len / 2;  // RGB565 每像素 2 字节
    size_t out_idx = 0;
    size_t i = 0;

    // 首帧：previous 为 NULL，全部按原始数据编码
    if (!previous) {
        while (i < pixel_count) {
            // 查找连续相同像素（RLE 压缩）
            size_t run_start = i;
            while (i + 1 < pixel_count &&
                   pixels_equal(&current[i * 2], &current[(i + 1) * 2]) &&
                   (i - run_start) < RLE_MAX_RUN) {
                i++;
            }
            size_t run_len = i - run_start + 1;

            if (run_len >= 3) {
                // RLE 编码：控制字节（重复次数）+ 像素值
                output[out_idx++] = (uint8_t)(run_len - 1);
                output[out_idx++] = current[run_start * 2];
                output[out_idx++] = current[run_start * 2 + 1];
                i++;
            } else {
                // 原始数据：收集不重复像素
                size_t literal_start = run_start;
                i = run_start;
                while (i < pixel_count) {
                    size_t literal_len = i - literal_start + 1;
                    if (literal_len >= RLE_MAX_RUN) {
                        break;
                    }
                    // 前瞻：如果接下来有 3+ 连续相同像素，结束 literal 段
                    if (i + 2 < pixel_count &&
                        pixels_equal(&current[i * 2], &current[(i + 1) * 2]) &&
                        pixels_equal(&current[i * 2], &current[(i + 2) * 2])) {
                        break;
                    }
                    i++;
                }
                size_t literal_len = i - literal_start;
                output[out_idx++] = RLE_LITERAL_FLAG | (uint8_t)(literal_len - 1);
                for (size_t j = literal_start; j < i; j++) {
                    output[out_idx++] = current[j * 2];
                    output[out_idx++] = current[j * 2 + 1];
                }
            }
        }
        return out_idx;
    }

    // 差分编码：只传输与前一帧不同的像素
    while (i < pixel_count) {
        // 查找连续未变化像素（高压缩）
        size_t unchanged_start = i;
        while (i < pixel_count &&
               pixels_equal(&current[i * 2], &previous[i * 2]) &&
               (i - unchanged_start) < RLE_MAX_RUN) {
            i++;
        }
        size_t unchanged_len = i - unchanged_start;

        if (unchanged_len > 0) {
            // 编码未变化段：控制字节 + 无数据
            output[out_idx++] = (uint8_t)(unchanged_len - 1);
            output[out_idx++] = 0xFF;  // 特殊标记：表示"保持不变"
            output[out_idx++] = 0xFF;
        }

        if (i >= pixel_count) {
            break;
        }

        // 查找连续变化且相同的像素（RLE 压缩）
        size_t run_start = i;
        while (i + 1 < pixel_count &&
               !pixels_equal(&current[i * 2], &previous[i * 2]) &&
               pixels_equal(&current[i * 2], &current[(i + 1) * 2]) &&
               (i - run_start) < RLE_MAX_RUN) {
            i++;
        }
        size_t run_len = i - run_start + 1;

        if (run_len >= 3 && !pixels_equal(&current[run_start * 2], &previous[run_start * 2])) {
            // RLE 编码变化段
            output[out_idx++] = (uint8_t)(run_len - 1);
            output[out_idx++] = current[run_start * 2];
            output[out_idx++] = current[run_start * 2 + 1];
            i++;
        } else {
            // 原始数据：收集变化的不重复像素
            size_t literal_start = run_start;
            i = run_start;
            while (i < pixel_count) {
                if (pixels_equal(&current[i * 2], &previous[i * 2])) {
                    // 遇到未变化像素，结束 literal 段
                    break;
                }
                size_t literal_len = i - literal_start + 1;
                if (literal_len >= RLE_MAX_RUN) {
                    break;
                }
                // 前瞻：如果接下来有 3+ 连续相同像素，结束 literal 段
                if (i + 2 < pixel_count &&
                    pixels_equal(&current[i * 2], &current[(i + 1) * 2]) &&
                    pixels_equal(&current[i * 2], &current[(i + 2) * 2])) {
                    break;
                }
                i++;
            }
            size_t literal_len = i - literal_start;
            if (literal_len > 0) {
                output[out_idx++] = RLE_LITERAL_FLAG | (uint8_t)(literal_len - 1);
                for (size_t j = literal_start; j < i; j++) {
                    output[out_idx++] = current[j * 2];
                    output[out_idx++] = current[j * 2 + 1];
                }
            }
        }
    }

    return out_idx;
}

esp_err_t rle_decode_diff(const uint8_t *input, uint8_t *output,
                          const uint8_t *previous, size_t input_len)
{
    if (!input || !output || input_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 首帧：previous 为 NULL，直接解码到 output
    if (!previous) {
        size_t in_idx = 0;
        size_t out_idx = 0;

        while (in_idx < input_len) {
            if (in_idx + 1 > input_len) {
                return ESP_FAIL;  // 数据不完整
            }
            uint8_t ctrl = input[in_idx++];

            if (ctrl & RLE_LITERAL_FLAG) {
                // 原始数据段
                size_t count = (ctrl & 0x7F) + 1;
                if (in_idx + count * 2 > input_len) {
                    return ESP_FAIL;
                }
                for (size_t i = 0; i < count; i++) {
                    output[out_idx++] = input[in_idx++];
                    output[out_idx++] = input[in_idx++];
                }
            } else {
                // RLE 重复段
                size_t count = ctrl + 1;
                if (in_idx + 2 > input_len) {
                    return ESP_FAIL;
                }
                uint8_t pixel_hi = input[in_idx++];
                uint8_t pixel_lo = input[in_idx++];
                for (size_t i = 0; i < count; i++) {
                    output[out_idx++] = pixel_hi;
                    output[out_idx++] = pixel_lo;
                }
            }
        }
        return ESP_OK;
    }

    // 差分解码：先复制前一帧，再应用差分
    memcpy(output, previous, 320 * 240 * 2);

    size_t in_idx = 0;
    size_t out_idx = 0;

    while (in_idx < input_len) {
        if (in_idx + 1 > input_len) {
            return ESP_FAIL;
        }
        uint8_t ctrl = input[in_idx++];

        if (ctrl & RLE_LITERAL_FLAG) {
            // 原始数据段：更新变化像素
            size_t count = (ctrl & 0x7F) + 1;
            if (in_idx + count * 2 > input_len) {
                return ESP_FAIL;
            }
            for (size_t i = 0; i < count; i++) {
                output[out_idx++] = input[in_idx++];
                output[out_idx++] = input[in_idx++];
            }
        } else {
            // RLE 段
            size_t count = ctrl + 1;
            if (in_idx + 2 > input_len) {
                return ESP_FAIL;
            }
            uint8_t pixel_hi = input[in_idx++];
            uint8_t pixel_lo = input[in_idx++];

            if (pixel_hi == 0xFF && pixel_lo == 0xFF) {
                // 特殊标记：保持不变，跳过这些像素
                out_idx += count * 2;
            } else {
                // 更新变化像素
                for (size_t i = 0; i < count; i++) {
                    output[out_idx++] = pixel_hi;
                    output[out_idx++] = pixel_lo;
                }
            }
        }
    }

    return ESP_OK;
}
