#include <stddef.h>
#include <stdint.h>

#include "rc_video_yuv_scale.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_vyuy_macro_pixels_scale_without_chroma_misalignment(void)
{
    /* 8x4 source: each 4-byte VYUY macro pixel carries a unique marker. */
    uint8_t source[8U * 4U * 2U];
    for (uint32_t y = 0U; y < 4U; ++y) {
        for (uint32_t macro = 0U; macro < 4U; ++macro) {
            const size_t offset = (size_t)y * 16U + macro * 4U;
            const uint8_t marker = (uint8_t)(y * 16U + macro * 4U);
            source[offset + 0U] = (uint8_t)(0x80U + marker); /* V */
            source[offset + 1U] = (uint8_t)(0x10U + marker); /* Y0 */
            source[offset + 2U] = (uint8_t)(0x40U + marker); /* U */
            source[offset + 3U] = (uint8_t)(0x20U + marker); /* Y1 */
        }
    }

    uint8_t output[4U * 2U * 2U] = {0};
    TEST_ASSERT(rc_video_scale_vyuy_to_ycbycr(source, 8U, 4U,
                                               output, 4U, 2U));

    /* Row 0 takes source macros 0 and 2. */
    TEST_ASSERT(0x10U == output[0] && 0x40U == output[1]);
    TEST_ASSERT(0x20U == output[2] && 0x80U == output[3]);
    TEST_ASSERT(0x18U == output[4] && 0x48U == output[5]);
    TEST_ASSERT(0x28U == output[6] && 0x88U == output[7]);

    /* Row 1 takes source row 2, preserving the same macro boundaries. */
    TEST_ASSERT(0x30U == output[8] && 0x60U == output[9]);
    TEST_ASSERT(0x40U == output[10] && 0xA0U == output[11]);
    TEST_ASSERT(0x38U == output[12] && 0x68U == output[13]);
    TEST_ASSERT(0x48U == output[14] && 0xA8U == output[15]);
    return 0;
}

static int test_rejects_odd_or_null_geometry(void)
{
    uint8_t pixel[4] = {0};
    TEST_ASSERT(!rc_video_scale_vyuy_to_ycbycr(NULL, 2U, 1U,
                                                pixel, 2U, 1U));
    TEST_ASSERT(!rc_video_scale_vyuy_to_ycbycr(pixel, 1U, 1U,
                                                pixel, 2U, 1U));
    TEST_ASSERT(!rc_video_scale_vyuy_to_ycbycr(pixel, 2U, 1U,
                                                pixel, 1U, 1U));
    return 0;
}

static int test_vyuy_red_remains_red_in_camera_rgb565_contract(void)
{
    /* Limited-range BT.601 red: V,Y0,U,Y1. */
    const uint8_t source[4] = {240U, 82U, 90U, 82U};
    uint16_t output[2] = {0U, 0U};

    TEST_ASSERT(rc_video_scale_vyuy_to_lcd_bgr565(
        source, 2U, 1U, output, 2U, 1U, 2U));
    TEST_ASSERT(0x00F8U == output[0]);
    TEST_ASSERT(0x00F8U == output[1]);
    return 0;
}

static int test_region_uses_full_output_coordinates(void)
{
    /* Each source row has a distinct luma so a partial conversion can prove
     * that output_y_start is applied before source scaling. */
    const uint8_t source[4U * 4U * 2U] = {
        0x80U, 0x20U, 0x80U, 0x20U, 0x80U, 0x20U, 0x80U, 0x20U,
        0x80U, 0x40U, 0x80U, 0x40U, 0x80U, 0x40U, 0x80U, 0x40U,
        0x80U, 0x60U, 0x80U, 0x60U, 0x80U, 0x60U, 0x80U, 0x60U,
        0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U,
    };
    uint16_t output[2U * 1U] = {0U, 0U};

    TEST_ASSERT(rc_video_scale_vyuy_to_lcd_bgr565_region(
        source, 4U, 4U, output, 2U, 4U, 2U, 1U, 2U));
    /* output row 2 samples source row 2 (Y=0x60), not source row 0. */
    TEST_ASSERT(output[0] != 0U && output[0] == output[1]);
    TEST_ASSERT(!rc_video_scale_vyuy_to_lcd_bgr565_region(
        source, 4U, 4U, output, 2U, 4U, 4U, 1U, 2U));
    return 0;
}

int main(void)
{
    int ret = test_vyuy_macro_pixels_scale_without_chroma_misalignment();
    if (0 != ret) return ret;
    ret = test_vyuy_red_remains_red_in_camera_rgb565_contract();
    if (0 != ret) return ret;
    ret = test_region_uses_full_output_coordinates();
    if (0 != ret) return ret;
    return test_rejects_odd_or_null_geometry();
}
