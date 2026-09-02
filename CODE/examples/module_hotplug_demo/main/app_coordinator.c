#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "app_coord";

#define CAM_H_RES       640
#define CAM_V_RES       480
#define LCD_H_RES       BOARD_LAIWFS300_LCD_H_RES
#define LCD_V_RES       BOARD_LAIWFS300_LCD_V_RES
#define LCD_CHUNK_LINES 80
#define CHUNK_BUF_COUNT 2

extern bool slot_lcd_is_available(void);
extern bool slot_camera_is_ready(void);
extern esp_err_t slot_camera_acquire_frame(uint8_t **out_buf, size_t *out_len);
extern void slot_camera_release_frame(void);

static TaskHandle_t s_coord_task;
static uint16_t *s_rgb_chunk[CHUNK_BUF_COUNT];
static bool s_chunk_allocated;

static int32_t s_y_lut[256];
static int32_t s_u_to_b_lut[256];
static int32_t s_u_to_g_lut[256];
static int32_t s_v_to_r_lut[256];
static int32_t s_v_to_g_lut[256];
static bool s_lut_ready;

static void init_yuv_lut(void)
{
    for (int i = 0; i < 256; ++i) {
        int delta = i - 128;
        int y = i - 16;
        if (y < 0) { y = 0; }
        s_y_lut[i] = 298 * y + 128;
        s_u_to_b_lut[i] = 516 * delta;
        s_u_to_g_lut[i] = -100 * delta;
        s_v_to_r_lut[i] = 409 * delta;
        s_v_to_g_lut[i] = -208 * delta;
    }
    s_lut_ready = true;
}

static inline uint16_t yuv_to_rgb565_be(uint8_t y, uint8_t u, uint8_t v)
{
    int r = (s_y_lut[y] + s_v_to_r_lut[v]) >> 8;
    int g = (s_y_lut[y] + s_u_to_g_lut[u] + s_v_to_g_lut[v]) >> 8;
    int b = (s_y_lut[y] + s_u_to_b_lut[u]) >> 8;
    if (r < 0) { r = 0; } else if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } else if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } else if (b > 255) { b = 255; }
    uint16_t pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((pixel >> 8) | (pixel << 8));
}

static void vyuy_chunk_to_rgb565(const uint8_t *yuv, uint16_t *rgb,
                                  uint32_t dst_y_start, uint32_t chunk_lines)
{
    const uint32_t src_stride = CAM_H_RES * 2U;
    const uint32_t src_x_byte_off = dst_y_start * 4U;
    for (uint32_t dx = 0; dx < (uint32_t)LCD_H_RES; ++dx) {
        const uint8_t *src = yuv + (dx * 2U) * src_stride + src_x_byte_off;
        uint16_t *dst = rgb + dx;
        for (uint32_t row = 0; row < chunk_lines; ++row) {
            dst[row * LCD_H_RES] = yuv_to_rgb565_be(src[1], src[2], src[0]);
            src += 4;
        }
    }
}

static bool ensure_chunk_buffers(void)
{
    if (s_chunk_allocated) { return true; }
    size_t sz = LCD_H_RES * LCD_CHUNK_LINES * sizeof(uint16_t);
    for (int i = 0; i < CHUNK_BUF_COUNT; i++) {
        s_rgb_chunk[i] = heap_caps_aligned_alloc(64, sz, MALLOC_CAP_DMA);
        if (NULL == s_rgb_chunk[i]) {
            for (int j = 0; j < i; j++) { heap_caps_free(s_rgb_chunk[j]); s_rgb_chunk[j] = NULL; }
            return false;
        }
    }
    s_chunk_allocated = true;
    return true;
}

static void coordinator_task(void *arg)
{
    (void)arg;
    bool show_white = true;
    uint32_t preview_count = 0;
    uint32_t last_stats = 0;
    int chunk_idx = 0;
    while (1) {
        bool lcd_ok = slot_lcd_is_available();
        bool cam_ok = slot_camera_is_ready();

        if (cam_ok && lcd_ok) {
            if (!s_lut_ready) { init_yuv_lut(); }
            if (!ensure_chunk_buffers()) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            uint8_t *frame = NULL;
            size_t len = 0;
            esp_err_t ret = slot_camera_acquire_frame(&frame, &len);
            if (ESP_OK == ret && NULL != frame) {
                for (int y = 0; y < LCD_V_RES; y += LCD_CHUNK_LINES) {
                    int lines = LCD_CHUNK_LINES;
                    if (y + lines > LCD_V_RES) { lines = LCD_V_RES - y; }
                    vyuy_chunk_to_rgb565(frame, s_rgb_chunk[chunk_idx], (uint32_t)y, (uint32_t)lines);
                    while (display_hal_wait_pending(0) == ESP_OK) {}
                    display_hal_draw_bitmap_rgb565(0, y, LCD_H_RES, lines, s_rgb_chunk[chunk_idx]);
                    display_hal_wait_pending(200);
                    chunk_idx = (chunk_idx + 1) % CHUNK_BUF_COUNT;
                }
                slot_camera_release_frame();
                preview_count++;
                uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                if (now - last_stats >= 5000) {
                    ESP_LOGI(TAG, "preview: %lu frames displayed", (unsigned long)preview_count);
                    last_stats = now;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } else if (lcd_ok) {
            uint16_t color = show_white ? 0xFFFF : 0xF800;
            board_laiwfs300_display_fill_rgb565(color);
            show_white = !show_white;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void app_coordinator_start(void)
{
    xTaskCreate(coordinator_task, "app_coord", 8192, NULL, 4, &s_coord_task);
    ESP_LOGI(TAG, "coordinator started");
}
