/**
 * @file test_rc_rle.c
 * @brief 测试 rc_rle.c 的 RLE 差分编码/解码逻辑
 *
 * 覆盖场景:
 *   1. 编码/解码完全相同像素（首帧全黑）
 *   2. 编码/解码全帧变化（首帧到第二帧）
 *   3. 编码/解码部分变化（差分帧）
 *   4. 往返验证（编码后解码应还原原始帧）
 */

#include "../main/rc_rle.h"
#include <stdint.h>
#include <stddef.h>

/* ========== 外部依赖声明 ========== */
void *memset(void *destination, int value, size_t count);
void *memcpy(void *destination, const void *source, size_t count);
int memcmp(const void *left, const void *right, size_t count);

/* ========== ESP-IDF 错误码定义 ========== */
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102

/* ========== 断言宏 ========== */
static int s_test_failed = 0;
static int s_test_count = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        s_test_failed = 1; \
        return; \
    } \
} while (0)

#define RUN_TEST(test_func) do { \
    s_test_count++; \
    test_func(); \
    if (s_test_failed) return s_test_count; \
} while (0)

/* ========== 全局缓冲区（避免大栈帧触发 __chkstk_ms） ========== */
static uint8_t g_previous[320 * 240 * 2];
static uint8_t g_current[320 * 240 * 2];
static uint8_t g_encoded[320 * 240 * 2 * 2];
static uint8_t g_decoded[320 * 240 * 2];

/* ========== 测试用例 ========== */

/**
 * @brief 测试场景 1: 编码/解码完全相同像素（首帧全黑）
 *
 * 验证首帧全黑场景下 RLE 编码能够高效压缩，解码后还原
 */
static void test_encode_decode_identical_pixels(void)
{
    const size_t frame_size = 320 * 240 * 2;  /* RGB565 帧大小 */
    uint8_t *current = g_current;
    uint8_t *encoded = g_encoded;
    uint8_t *decoded = g_decoded;

    /* 全黑帧（所有像素 0x0000） */
    memset(current, 0x00, frame_size);

    /* 编码首帧（previous = NULL） */
    size_t encoded_size = rle_encode_diff(current, NULL, encoded, frame_size);
    ASSERT(encoded_size > 0);
    ASSERT(encoded_size < frame_size);  /* 应该压缩 */

    /* 解码首帧 */
    int result = rle_decode_diff(encoded, decoded, NULL, encoded_size);
    ASSERT(result == ESP_OK);

    /* 验证解码后与原始帧一致 */
    ASSERT(memcmp(current, decoded, frame_size) == 0);
}

/**
 * @brief 测试场景 2: 编码/解码全帧变化
 *
 * 验证从全黑帧到亮色帧的全帧变化编码和解码
 * 注意：避免使用 0xFFFF（白色），因为它被用作"保持不变"的特殊标记
 */
static void test_encode_decode_full_frame_change(void)
{
    const size_t frame_size = 320 * 240 * 2;
    uint8_t *previous = g_previous;
    uint8_t *current = g_current;
    uint8_t *encoded = g_encoded;
    uint8_t *decoded = g_decoded;

    /* 前一帧：全黑 */
    memset(previous, 0x00, frame_size);

    /* 当前帧：亮绿色（RGB565 绿色 = 0x07E0，避开 0xFFFF） */
    for (size_t i = 0; i < frame_size; i += 2) {
        current[i] = 0x07;
        current[i + 1] = 0xE0;
    }

    /* 编码差分帧 */
    size_t encoded_size = rle_encode_diff(current, previous, encoded, frame_size);
    ASSERT(encoded_size > 0);
    ASSERT(encoded_size < frame_size);  /* RLE 应该压缩 */

    /* 解码差分帧 */
    int result = rle_decode_diff(encoded, decoded, previous, encoded_size);
    ASSERT(result == ESP_OK);

    /* 验证解码后与当前帧一致 */
    ASSERT(memcmp(current, decoded, frame_size) == 0);
}

/**
 * @brief 测试场景 3: 编码/解码部分变化
 *
 * 验证帧间只有部分区域变化时的编码和解码
 */
static void test_encode_decode_partial_change(void)
{
    const size_t frame_size = 320 * 240 * 2;
    uint8_t *previous = g_previous;
    uint8_t *current = g_current;
    uint8_t *encoded = g_encoded;
    uint8_t *decoded = g_decoded;

    /* 前一帧：全部为 0x1234 */
    for (size_t i = 0; i < frame_size; i += 2) {
        previous[i] = 0x12;
        previous[i + 1] = 0x34;
    }

    /* 当前帧：复制前一帧，然后修改前 100 个像素为 0x5678 */
    memcpy(current, previous, frame_size);
    for (size_t i = 0; i < 100 * 2; i += 2) {
        current[i] = 0x56;
        current[i + 1] = 0x78;
    }

    /* 编码差分帧（只有前 100 像素变化） */
    size_t encoded_size = rle_encode_diff(current, previous, encoded, frame_size);
    ASSERT(encoded_size > 0);
    /* 差分编码应该远小于全帧 */
    ASSERT(encoded_size < frame_size / 10);

    /* 解码差分帧 */
    int result = rle_decode_diff(encoded, decoded, previous, encoded_size);
    ASSERT(result == ESP_OK);

    /* 验证解码后与当前帧一致 */
    ASSERT(memcmp(current, decoded, frame_size) == 0);
}

/**
 * @brief 测试场景 4: 往返验证（多种模式混合）
 *
 * 验证包含连续相同、不连续变化等多种模式的复杂帧的编码解码往返
 */
static void test_roundtrip_verification(void)
{
    const size_t frame_size = 320 * 240 * 2;
    uint8_t *previous = g_previous;
    uint8_t *current = g_current;
    uint8_t *encoded = g_encoded;
    uint8_t *decoded = g_decoded;

    /* 构造复杂前一帧：分区域填充不同像素 */
    for (size_t i = 0; i < frame_size; i += 2) {
        size_t pixel_idx = i / 2;
        if (pixel_idx < 1000) {
            /* 前 1000 像素：全黑 */
            previous[i] = 0x00;
            previous[i + 1] = 0x00;
        } else if (pixel_idx < 2000) {
            /* 1000-2000 像素：全白 */
            previous[i] = 0xFF;
            previous[i + 1] = 0xFF;
        } else {
            /* 其余像素：递增模式 */
            uint8_t val = (uint8_t)(pixel_idx & 0xFF);
            previous[i] = val;
            previous[i + 1] = (uint8_t)(~val);
        }
    }

    /* 构造复杂当前帧：部分保持、部分变化 */
    memcpy(current, previous, frame_size);
    /* 修改 500-600 像素为 0xAAAA */
    for (size_t i = 500 * 2; i < 600 * 2; i += 2) {
        current[i] = 0xAA;
        current[i + 1] = 0xAA;
    }
    /* 修改 1500-1550 像素为递减模式 */
    for (size_t i = 1500 * 2; i < 1550 * 2; i += 2) {
        size_t pixel_idx = i / 2;
        uint8_t val = (uint8_t)(255 - (pixel_idx & 0xFF));
        current[i] = val;
        current[i + 1] = val;
    }

    /* 编码差分帧 */
    size_t encoded_size = rle_encode_diff(current, previous, encoded, frame_size);
    ASSERT(encoded_size > 0);
    ASSERT(encoded_size < frame_size);

    /* 解码差分帧 */
    int result = rle_decode_diff(encoded, decoded, previous, encoded_size);
    ASSERT(result == ESP_OK);

    /* 验证往返后完全一致 */
    ASSERT(memcmp(current, decoded, frame_size) == 0);
}

/**
 * @brief 测试参数验证
 *
 * 验证编码和解码函数对非法参数的处理
 */
static void test_invalid_parameters(void)
{
    uint8_t buffer[100];
    uint8_t output[200];
    size_t result;

    /* 编码：NULL current */
    result = rle_encode_diff(NULL, NULL, output, 100);
    ASSERT(result == 0);

    /* 编码：NULL output */
    result = rle_encode_diff(buffer, NULL, NULL, 100);
    ASSERT(result == 0);

    /* 编码：len = 0 */
    result = rle_encode_diff(buffer, NULL, output, 0);
    ASSERT(result == 0);

    /* 编码：len 非偶数（不是完整像素） */
    result = rle_encode_diff(buffer, NULL, output, 99);
    ASSERT(result == 0);

    /* 解码：NULL input */
    int decode_result = rle_decode_diff(NULL, output, NULL, 100);
    ASSERT(decode_result == ESP_ERR_INVALID_ARG);

    /* 解码：NULL output */
    decode_result = rle_decode_diff(buffer, NULL, NULL, 100);
    ASSERT(decode_result == ESP_ERR_INVALID_ARG);

    /* 解码：input_len = 0 */
    decode_result = rle_decode_diff(buffer, output, NULL, 0);
    ASSERT(decode_result == ESP_ERR_INVALID_ARG);
}

/**
 * @brief 测试首帧编码（无前一帧）
 *
 * 验证首帧（previous = NULL）的编码和解码
 */
static void test_first_frame_encoding(void)
{
    const size_t frame_size = 320 * 240 * 2;
    uint8_t *current = g_current;
    uint8_t *encoded = g_encoded;
    uint8_t *decoded = g_decoded;

    /* 构造渐变帧 */
    for (size_t i = 0; i < frame_size; i += 2) {
        uint8_t val = (uint8_t)((i / 2) % 256);
        current[i] = val;
        current[i + 1] = (uint8_t)(255 - val);
    }

    /* 编码首帧 */
    size_t encoded_size = rle_encode_diff(current, NULL, encoded, frame_size);
    ASSERT(encoded_size > 0);

    /* 解码首帧 */
    int result = rle_decode_diff(encoded, decoded, NULL, encoded_size);
    ASSERT(result == ESP_OK);

    /* 验证解码后与原始帧一致 */
    ASSERT(memcmp(current, decoded, frame_size) == 0);
}

/**
 * @brief 测试连续多帧编码（验证状态机连续性）
 *
 * 验证连续编码多帧的正确性
 */
static void test_continuous_frames(void)
{
    const size_t frame_size = 320 * 240 * 2;
    /* 复用全局缓冲区 - 需要 4 个帧缓冲，但只有 2 个 previous/current
       改为顺序测试，复用缓冲区 */
    uint8_t *frame = g_current;
    uint8_t *encoded = g_encoded;
    uint8_t *decoded = g_decoded;
    uint8_t *prev = g_previous;

    /* 帧 0：全黑 */
    memset(frame, 0x00, frame_size);

    /* 编码帧 0（首帧） */
    size_t enc0_size = rle_encode_diff(frame, NULL, encoded, frame_size);
    ASSERT(enc0_size > 0);
    int result = rle_decode_diff(encoded, decoded, NULL, enc0_size);
    ASSERT(result == ESP_OK);
    ASSERT(memcmp(frame, decoded, frame_size) == 0);

    /* 保存帧 0 到 prev，准备帧 1 */
    memcpy(prev, frame, frame_size);

    /* 帧 1：前半部分变为蓝色（避开 0xFFFF） */
    for (size_t i = 0; i < frame_size / 2; i += 2) {
        frame[i] = 0x00;
        frame[i + 1] = 0x1F;
    }

    /* 编码帧 1（差分自帧 0） */
    size_t enc1_size = rle_encode_diff(frame, prev, encoded, frame_size);
    ASSERT(enc1_size > 0);
    result = rle_decode_diff(encoded, decoded, prev, enc1_size);
    ASSERT(result == ESP_OK);
    ASSERT(memcmp(frame, decoded, frame_size) == 0);

    /* 保存帧 1 到 prev，准备帧 2 */
    memcpy(prev, frame, frame_size);

    /* 帧 2：全红色（避开 0xFFFF） */
    for (size_t i = 0; i < frame_size; i += 2) {
        frame[i] = 0xF8;
        frame[i + 1] = 0x00;
    }

    /* 编码帧 2（差分自帧 1） */
    size_t enc2_size = rle_encode_diff(frame, prev, encoded, frame_size);
    ASSERT(enc2_size > 0);
    result = rle_decode_diff(encoded, decoded, prev, enc2_size);
    ASSERT(result == ESP_OK);
    ASSERT(memcmp(frame, decoded, frame_size) == 0);
}

/* ========== 主函数 ========== */

int main(void)
{
    RUN_TEST(test_encode_decode_identical_pixels);
    RUN_TEST(test_encode_decode_full_frame_change);
    RUN_TEST(test_encode_decode_partial_change);
    RUN_TEST(test_roundtrip_verification);
    RUN_TEST(test_invalid_parameters);
    RUN_TEST(test_first_frame_encoding);
    RUN_TEST(test_continuous_frames);

    return s_test_failed;
}
