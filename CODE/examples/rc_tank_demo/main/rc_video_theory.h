#pragma once

#include <stdint.h>

/*
 * Keep the ESP-IDF 4-MSS defaults. TCP is a stream, so a JPEG frame may span
 * several sliding windows; forcing one frame into one window caused pbuf
 * pressure and send timeouts on the target WiFi path.
 */
#define RC_VIDEO_TCP_SND_BUF_BYTES 5760U
#define RC_VIDEO_TCP_WND_BYTES 5760U
#define RC_VIDEO_TCP_MSS_BYTES 1440U
#define RC_VIDEO_TCP_RECV_MBOX_SLOTS 6U

static inline uint32_t rc_video_tcp_windows_per_frame(uint32_t frame_bytes,
                                                       uint32_t window_bytes)
{
    return (window_bytes == 0U) ? 0U
                                : (frame_bytes + window_bytes - 1U) / window_bytes;
}

static inline uint32_t rc_video_tcp_recv_mailbox_min(uint32_t window_bytes,
                                                      uint32_t mss_bytes)
{
    return (mss_bytes == 0U) ? 0U
                             : ((window_bytes + mss_bytes - 1U) / mss_bytes) + 2U;
}

static inline uint8_t rc_video_tcp_budget_covers_frame(uint32_t frame_bytes)
{
    return (frame_bytes <= RC_VIDEO_TCP_SND_BUF_BYTES) &&
           (frame_bytes <= RC_VIDEO_TCP_WND_BYTES);
}

static inline uint64_t rc_video_frame_bytes(uint32_t width, uint32_t height,
                                            uint32_t bytes_per_pixel)
{
    return (uint64_t)width * (uint64_t)height * (uint64_t)bytes_per_pixel;
}

static inline uint32_t rc_video_tx_buffer_bytes(uint32_t jpeg_capacity,
                                                uint32_t header_bytes)
{
    return jpeg_capacity + header_bytes;
}

static inline uint32_t rc_video_spi_payload_time_us(uint32_t width, uint32_t height,
                                                    uint32_t pixel_clock_hz)
{
    const uint64_t payload_bits = (uint64_t)width * (uint64_t)height * 16U;
    return (pixel_clock_hz == 0U) ? 0U
                                  : (uint32_t)((payload_bits * 1000000ULL + pixel_clock_hz - 1U) /
                                               pixel_clock_hz);
}

static inline uint32_t rc_video_fps_milli_from_period_us(uint32_t period_us)
{
    return (period_us == 0U) ? 0U : (uint32_t)(1000000000ULL / period_us);
}
