#include "rc_video_display_plan.h"
#include "rc_video_theory.h"

#define TEST_ASSERT(cond) do { if (!(cond)) return __LINE__; } while (0)

static int test_chunks_cover_frame(void)
{
    uint32_t total_lines = 0;
    uint32_t chunks = rc_remote_lcd_chunk_count(RC_REMOTE_LCD_LOGICAL_H);
    TEST_ASSERT(chunks == 4U);
    for (uint32_t y = 0; y < RC_REMOTE_LCD_LOGICAL_H; y += RC_REMOTE_LCD_CHUNK_LINES) {
        uint32_t lines = rc_remote_lcd_chunk_lines(RC_REMOTE_LCD_LOGICAL_H, y);
        TEST_ASSERT(lines > 0U && lines <= RC_REMOTE_LCD_CHUNK_LINES);
        TEST_ASSERT(rc_remote_lcd_chunk_fits_dma(RC_REMOTE_LCD_LOGICAL_W, lines));
        total_lines += lines;
    }
    TEST_ASSERT(total_lines == RC_REMOTE_LCD_LOGICAL_H);
    TEST_ASSERT(0U == rc_remote_lcd_chunk_lines(RC_REMOTE_LCD_LOGICAL_H,
                                                RC_REMOTE_LCD_LOGICAL_H));
    return 0;
}

static int test_tank_status_chunks_cover_frame(void)
{
    uint32_t total_lines = 0U;
    TEST_ASSERT(rc_tank_status_lcd_chunk_count() == 4U);
    for (uint32_t y = 0U; y < RC_TANK_STATUS_LCD_H; y += RC_TANK_STATUS_LCD_CHUNK_LINES) {
        const uint32_t lines = rc_tank_status_lcd_chunk_lines(y);
        TEST_ASSERT(lines > 0U && lines <= RC_TANK_STATUS_LCD_CHUNK_LINES);
        TEST_ASSERT(rc_remote_lcd_chunk_fits_dma(RC_TANK_STATUS_LCD_W, lines));
        total_lines += lines;
    }
    TEST_ASSERT(total_lines == RC_TANK_STATUS_LCD_H);
    return 0;
}

static int test_theoretical_spi_timing(void)
{
    uint32_t full_20mhz = rc_video_spi_payload_time_us(320U, 240U, 20000000U);
    uint32_t full_10mhz = rc_video_spi_payload_time_us(320U, 240U, 10000000U);
    uint32_t chunk_20mhz = rc_video_spi_payload_time_us(320U, 40U, 20000000U);
    uint32_t large_chunk_20mhz = rc_video_spi_payload_time_us(320U, 60U, 20000000U);
    TEST_ASSERT(full_20mhz == 61440U);
    TEST_ASSERT(full_10mhz == 122880U);
    TEST_ASSERT(chunk_20mhz == 10240U);
    TEST_ASSERT(large_chunk_20mhz == 15360U);
    TEST_ASSERT(rc_video_fps_milli_from_period_us(full_20mhz) == 16276U);
    TEST_ASSERT(rc_video_frame_bytes(320U, 240U, 2U) == 153600U);
    TEST_ASSERT(rc_video_tx_buffer_bytes(76800U, 8U) == 76808U);
    return 0;
}

static int test_theoretical_tcp_budget(void)
{
    TEST_ASSERT(!rc_video_tcp_budget_covers_frame(20000U));
    TEST_ASSERT(rc_video_tcp_budget_covers_frame(RC_VIDEO_TCP_SND_BUF_BYTES));
    TEST_ASSERT(!rc_video_tcp_budget_covers_frame(RC_VIDEO_TCP_SND_BUF_BYTES + 1U));
    TEST_ASSERT(RC_VIDEO_TCP_SND_BUF_BYTES == 5760U);
    TEST_ASSERT(RC_VIDEO_TCP_WND_BYTES == 5760U);
    TEST_ASSERT(rc_video_tcp_windows_per_frame(20000U, RC_VIDEO_TCP_WND_BYTES) == 4U);
    TEST_ASSERT(rc_video_tcp_recv_mailbox_min(RC_VIDEO_TCP_WND_BYTES,
                                              RC_VIDEO_TCP_MSS_BYTES) == 6U);
    TEST_ASSERT(RC_VIDEO_TCP_RECV_MBOX_SLOTS >=
                rc_video_tcp_recv_mailbox_min(RC_VIDEO_TCP_WND_BYTES,
                                              RC_VIDEO_TCP_MSS_BYTES));
    TEST_ASSERT(rc_video_tcp_recv_mailbox_min(RC_VIDEO_TCP_WND_BYTES, 0U) == 0U);
    return 0;
}

int main(void)
{
    TEST_ASSERT(20000000U == RC_TANK_LCD_PIXEL_CLOCK_HZ);
    TEST_ASSERT(40000000U == RC_REMOTE_LCD_PIXEL_CLOCK_HZ);
    TEST_ASSERT(80U == RC_VIDEO_LCD_INIT_DRAW_BUFFER_LINES);
    TEST_ASSERT((240U * 80U * 2U) == RC_REMOTE_LCD_MAX_TRANSFER_BYTES);
    TEST_ASSERT((320U * 60U * 2U) == RC_REMOTE_LCD_MAX_TRANSFER_BYTES);

    int ret = test_chunks_cover_frame();
    if (ret != 0) return ret;
    ret = test_tank_status_chunks_cover_frame();
    if (ret != 0) return ret;
    ret = test_theoretical_spi_timing();
    if (ret != 0) return ret;
    return test_theoretical_tcp_budget();
}
