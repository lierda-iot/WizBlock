/**
 * Camera Display Demo: live preview + hardware diagnostics
 */

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "camera_hal.h"
#include "display_hal.h"

#include "driver/gpio.h"
#include "esp_cache.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "cam_disp_demo";

#define CAM_H_RES           640
#define CAM_V_RES           480
#define LCD_H_RES           BOARD_LAIWFS300_LCD_H_RES
#define LCD_V_RES           BOARD_LAIWFS300_LCD_V_RES
#define PREVIEW_H_RES       LCD_H_RES
#define PREVIEW_V_RES       LCD_V_RES
#define PREVIEW_X_OFFSET    0
#define PREVIEW_Y_OFFSET    0
#define FB_COUNT            3
#define LCD_CHUNK_BUF_COUNT 2
#define LCD_DRAW_WAIT_TIMEOUT_MS 200

/*
 * Stable preview baseline validated on 2026-07-03:
 * - pixel format: VYUY
 * - fullscreen preview
 * - LCD pixel clock: 20 MHz
 * - chunk lines: 80
 * - wait for each LCD transfer to complete
 *
 * Observed on device:
 * - capture ~= 25.2 fps
 * - display ~= 8.3 fps
 * - avg_convert ~= 55 ms
 * - avg_draw ~= 64 ms
 * - no mirrored image, no color issue, no tearing in long run
 */
#define LCD_STABLE_CHUNK_LINES           80
#define LCD_STABLE_PREVIEW_PIXEL_CLOCK_HZ 20000000

/*
 * Keep active tuning knobs separate from the validated baseline so we can
 * always restore the known-good configuration quickly.
 */
#define LCD_ACTIVE_CHUNK_LINES           LCD_STABLE_CHUNK_LINES
#define LCD_ACTIVE_PREVIEW_PIXEL_CLOCK_HZ LCD_STABLE_PREVIEW_PIXEL_CLOCK_HZ

typedef enum {
    PIXFMT_YUYV = 0,
    PIXFMT_UYVY,
    PIXFMT_YVYU,
    PIXFMT_VYUY,
    PIXFMT_COUNT,
} yuv422_pixfmt_t;

static esp_cam_ctlr_handle_t s_cam_ctlr;
static uint8_t *s_fb[FB_COUNT];
static uint16_t *s_rgb_chunk_buf[LCD_CHUNK_BUF_COUNT];
static SemaphoreHandle_t s_frame_ready;
static volatile int s_active_fb;
static volatile int s_locked_fb = -1;
static volatile uint32_t s_frame_count;
static volatile uint32_t s_error_count;
static uint32_t s_display_frame_count;
static const yuv422_pixfmt_t s_pixfmt = PIXFMT_VYUY;

static int32_t s_y_term_lut[256];
static int32_t s_u_to_b_lut[256];
static int32_t s_u_to_g_lut[256];
static int32_t s_v_to_r_lut[256];
static int32_t s_v_to_g_lut[256];

static const char *pixfmt_name(yuv422_pixfmt_t pixfmt)
{
    switch (pixfmt) {
    case PIXFMT_YUYV:
        return "YUYV";
    case PIXFMT_UYVY:
        return "UYVY";
    case PIXFMT_YVYU:
        return "YVYU";
    case PIXFMT_VYUY:
        return "VYUY";
    default:
        return "UNKNOWN";
    }
}

static void init_yuv_to_rgb_lut(void)
{
    for (int i = 0; i < 256; ++i) {
        int delta = i - 128;
        int y = i - 16;
        if (y < 0) {
            y = 0;
        }
        s_y_term_lut[i] = 298 * y + 128;
        s_u_to_b_lut[i] = 516 * delta;
        s_u_to_g_lut[i] = -100 * delta;
        s_v_to_r_lut[i] = 409 * delta;
        s_v_to_g_lut[i] = -208 * delta;
    }
}

static inline uint16_t yuv_to_rgb565_be(uint8_t y, uint8_t u, uint8_t v)
{
    int r = (s_y_term_lut[y] + s_v_to_r_lut[v]) >> 8;
    int g = (s_y_term_lut[y] + s_u_to_g_lut[u] + s_v_to_g_lut[v]) >> 8;
    int b = (s_y_term_lut[y] + s_u_to_b_lut[u]) >> 8;

    if (r < 0) {
        r = 0;
    } else if (r > 255) {
        r = 255;
    }
    if (g < 0) {
        g = 0;
    } else if (g > 255) {
        g = 255;
    }
    if (b < 0) {
        b = 0;
    } else if (b > 255) {
        b = 255;
    }

    uint16_t pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((pixel >> 8) | (pixel << 8));
}

static bool on_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle;
    (void)trans;
    (void)user_data;
    s_frame_count++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_ready, &woken);
    return (woken == pdTRUE);
}

static bool on_get_new_trans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle;
    (void)user_data;
    int next = (s_active_fb + 1) % FB_COUNT;
    if (next == s_locked_fb) {
        next = (next + 1) % FB_COUNT;
    }
    trans->buffer = s_fb[next];
    trans->buflen = CAM_H_RES * CAM_V_RES * 2;
    s_active_fb = next;
    return false;
}

static void vyuy_subsample_to_rgb565_chunk(const uint8_t *yuv, uint16_t *rgb,
                                           uint32_t dst_y_start, uint32_t chunk_lines)
{
    const uint32_t src_stride = CAM_H_RES * 2U;
    const uint32_t src_x_byte_off = dst_y_start * 4U;

    /*
     * Keep the verified-correct preview mapping:
     * display(x, y) <- source(x = y * 2, y = x * 2), pixel format VYUY.
     *
     * Iterating per output column makes each inner loop read contiguous bytes
     * from one source line, which is friendlier to PSRAM/cache than the old
     * per-pixel full-frame conversion order.
     */
    for (uint32_t dx = 0; dx < PREVIEW_H_RES; ++dx) {
        const uint8_t *src = yuv + (dx * 2U) * src_stride + src_x_byte_off;
        uint16_t *dst = rgb + dx;
        for (uint32_t row = 0; row < chunk_lines; ++row) {
            dst[row * PREVIEW_H_RES] = yuv_to_rgb565_be(src[1], src[2], src[0]);
            src += 4;
        }
    }
}

static int dvp_check_signal(gpio_num_t pin, const char *name)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int changes = 0;
    int last = gpio_get_level(pin);
    for (int i = 0; i < 100000; i++) {
        int cur = gpio_get_level(pin);
        if (cur != last) {
            changes++;
            last = cur;
        }
    }
    ESP_LOGI(TAG, "  %s (GPIO%d): %d transitions / 100k samples", name, pin, changes);
    return changes;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Camera Display Demo ===");
    ESP_LOGI(TAG, "Camera: SP0A39 %dx%d YUV422 DVP", CAM_H_RES, CAM_V_RES);
    ESP_LOGI(TAG, "Display: ST7789V3 %dx%d RGB565 SPI, preview %dx%d @ (%d,%d)",
             LCD_H_RES, LCD_V_RES, PREVIEW_H_RES, PREVIEW_V_RES, PREVIEW_X_OFFSET, PREVIEW_Y_OFFSET);
    ESP_LOGI(TAG, "Waiting 15s for serial monitor...");
    vTaskDelay(pdMS_TO_TICKS(15000));

    esp_err_t ret = board_laiwfs300_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "--- Phase 1: Display Init ---");
    ret = board_laiwfs300_display_init_with_config(LCD_ACTIVE_PREVIEW_PIXEL_CLOCK_HZ, LCD_ACTIVE_CHUNK_LINES);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "display init at %d Hz failed, fallback to default: %s",
                 LCD_ACTIVE_PREVIEW_PIXEL_CLOCK_HZ, esp_err_to_name(ret));
        ret = board_laiwfs300_display_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "display init FAILED: %s", esp_err_to_name(ret));
        return;
    }
    display_hal_fill_rgb565(0x0000);
    ESP_LOGI(TAG, "  display ready");

    ESP_LOGI(TAG, "--- Phase 2: Camera Sensor Init ---");
    ret = board_laiwfs300_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera sensor init FAILED: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Likely cause: MCLK crystal not reaching sensor (C0 board HW issue)");
        ESP_LOGE(TAG, "Check camera_hal logs above for detailed diagnostics");
        display_hal_fill_rgb565(0xF800);
        ESP_LOGE(TAG, "Screen shows RED = camera hardware failure");

        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGI(TAG, "[diag] re-checking DVP signals...");
            int pclk = dvp_check_signal(BOARD_LAIWFS300_GPIO_CAMERA_PCLK, "PCLK");
            int vsync = dvp_check_signal(BOARD_LAIWFS300_GPIO_CAMERA_VSYNC, "VSYNC");
            if (pclk > 0 && vsync > 0) {
                ESP_LOGI(TAG, "[diag] DVP signals active! Restart device to proceed.");
            }
        }
    }
    ESP_LOGI(TAG, "  sensor initialized and registers written");
    ESP_ERROR_CHECK(camera_hal_log_sensor_output_regs());

    ESP_LOGI(TAG, "--- Phase 3: DVP Controller Init ---");

    size_t fb_size = CAM_H_RES * CAM_V_RES * 2;
    for (int i = 0; i < FB_COUNT; i++) {
        s_fb[i] = heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM);
        if (s_fb[i] == NULL) {
            ESP_LOGE(TAG, "framebuffer %d alloc failed (%u bytes)", i, (unsigned)fb_size);
            return;
        }
        memset(s_fb[i], 0, fb_size);
    }
    ESP_LOGI(TAG, "  %d framebuffers allocated (%u KB each)", FB_COUNT, (unsigned)(fb_size / 1024));

    size_t chunk_buf_size = PREVIEW_H_RES * LCD_ACTIVE_CHUNK_LINES * sizeof(uint16_t);
    for (int i = 0; i < LCD_CHUNK_BUF_COUNT; ++i) {
        s_rgb_chunk_buf[i] = heap_caps_aligned_alloc(64, chunk_buf_size, MALLOC_CAP_DMA);
        if (s_rgb_chunk_buf[i] == NULL) {
            ESP_LOGE(TAG, "RGB chunk buffer %d alloc failed (%u bytes)", i, (unsigned)chunk_buf_size);
            return;
        }
    }
    init_yuv_to_rgb_lut();

    s_frame_ready = xSemaphoreCreateBinary();
    if (s_frame_ready == NULL) {
        ESP_LOGE(TAG, "frame semaphore alloc failed");
        return;
    }

    esp_cam_ctlr_dvp_pin_config_t dvp_pins = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {
            [0] = BOARD_LAIWFS300_GPIO_CAMERA_D0,
            [1] = BOARD_LAIWFS300_GPIO_CAMERA_D1,
            [2] = BOARD_LAIWFS300_GPIO_CAMERA_D2,
            [3] = BOARD_LAIWFS300_GPIO_CAMERA_D3,
            [4] = BOARD_LAIWFS300_GPIO_CAMERA_D4,
            [5] = BOARD_LAIWFS300_GPIO_CAMERA_D5,
            [6] = BOARD_LAIWFS300_GPIO_CAMERA_D6,
            [7] = BOARD_LAIWFS300_GPIO_CAMERA_D7,
            [8] = GPIO_NUM_NC,
            [9] = GPIO_NUM_NC,
            [10] = GPIO_NUM_NC,
            [11] = GPIO_NUM_NC,
            [12] = GPIO_NUM_NC,
            [13] = GPIO_NUM_NC,
            [14] = GPIO_NUM_NC,
            [15] = GPIO_NUM_NC,
        },
        .vsync_io = BOARD_LAIWFS300_GPIO_CAMERA_VSYNC,
        .de_io = BOARD_LAIWFS300_GPIO_CAMERA_HSYNC,
        .pclk_io = BOARD_LAIWFS300_GPIO_CAMERA_PCLK,
        .xclk_io = GPIO_NUM_NC,
    };

    esp_cam_ctlr_dvp_config_t dvp_cfg = {
        .ctlr_id = 0,
        .clk_src = CAM_CLK_SRC_DEFAULT,
        .h_res = CAM_H_RES,
        .v_res = CAM_V_RES,
        .input_data_color_type = CAM_CTLR_COLOR_YUV422,
        .cam_data_width = 8,
        .external_xtal = 1,
        .pin = &dvp_pins,
        .dma_burst_size = 64,
    };

    ret = esp_cam_new_dvp_ctlr(&dvp_cfg, &s_cam_ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DVP controller create failed: %s", esp_err_to_name(ret));
        display_hal_fill_rgb565(0xFFE0);
        ESP_LOGE(TAG, "Screen shows YELLOW = DVP controller init failure");
        return;
    }
    ESP_LOGI(TAG, "  DVP controller created");

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = on_get_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(s_cam_ctlr, &cbs, NULL));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(s_cam_ctlr));

    s_active_fb = 0;
    ESP_ERROR_CHECK(esp_cam_ctlr_start(s_cam_ctlr));
    ESP_LOGI(TAG, "  DVP capture started");

    ESP_LOGI(TAG, "--- Phase 4: Live Preview ---");
    uint32_t last_stats_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t last_capture_count = s_frame_count;
    uint32_t last_display_count = s_display_frame_count;
    uint64_t window_convert_us = 0;
    uint64_t window_draw_us = 0;
    int chunk_buf_idx = 0;
    ESP_LOGI(TAG, "[preview] fixed pixel format %s", pixfmt_name(s_pixfmt));

    while (1) {
        if (pdTRUE != xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(2000))) {
            s_error_count++;
            ESP_LOGW(TAG, "[preview] frame timeout #%lu (total frames=%lu)",
                     (unsigned long)s_error_count, (unsigned long)s_frame_count);
            if (s_error_count > 10 && s_frame_count == 0) {
                ESP_LOGE(TAG, "no frames received after 20s - hardware issue");
                display_hal_fill_rgb565(0xF81F);
                ESP_LOGE(TAG, "Screen shows MAGENTA = no frame data");
                break;
            }
            continue;
        }

        int fb_idx = (s_active_fb + FB_COUNT - 1) % FB_COUNT;
        s_locked_fb = fb_idx;
        esp_cache_msync(s_fb[fb_idx], CAM_H_RES * CAM_V_RES * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

        static int s_dump_count = 0;
        if (s_dump_count < 1 && s_frame_count >= 5) {
            s_dump_count++;
            const uint8_t *p = s_fb[fb_idx];
            ESP_LOGI(TAG, "=== RAW FRAME DUMP (frame #%lu) ===", (unsigned long)s_frame_count);
            ESP_LOGI(TAG, "  bytes[0..7]:   %02x %02x %02x %02x  %02x %02x %02x %02x",
                     p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
            ESP_LOGI(TAG, "  bytes[8..15]:  %02x %02x %02x %02x  %02x %02x %02x %02x",
                     p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
            ESP_LOGI(TAG, "  bytes[16..23]: %02x %02x %02x %02x  %02x %02x %02x %02x",
                     p[16], p[17], p[18], p[19], p[20], p[21], p[22], p[23]);
            uint32_t mid = (CAM_V_RES / 2) * CAM_H_RES * 2 + (CAM_H_RES / 2) * 2;
            ESP_LOGI(TAG, "  mid-frame[%lu]: %02x %02x %02x %02x  %02x %02x %02x %02x",
                     (unsigned long)mid,
                     p[mid], p[mid + 1], p[mid + 2], p[mid + 3],
                     p[mid + 4], p[mid + 5], p[mid + 6], p[mid + 7]);
        }

        uint64_t frame_convert_us = 0;
        uint64_t frame_draw_us = 0;
        for (int y = 0; y < PREVIEW_V_RES; y += LCD_ACTIVE_CHUNK_LINES) {
            int lines = LCD_ACTIVE_CHUNK_LINES;
            if (y + lines > PREVIEW_V_RES) {
                lines = PREVIEW_V_RES - y;
            }

            uint64_t convert_start_us = esp_timer_get_time();
            vyuy_subsample_to_rgb565_chunk(s_fb[fb_idx], s_rgb_chunk_buf[chunk_buf_idx],
                                           (uint32_t)y, (uint32_t)lines);
            frame_convert_us += esp_timer_get_time() - convert_start_us;

            // Drain any stale completion signal so the wait below matches this chunk.
            while (display_hal_wait_pending(0) == ESP_OK) {
            }

            uint64_t draw_start_us = esp_timer_get_time();
            ESP_ERROR_CHECK(display_hal_draw_bitmap_rgb565(PREVIEW_X_OFFSET, PREVIEW_Y_OFFSET + y,
                                                           PREVIEW_H_RES, lines,
                                                           s_rgb_chunk_buf[chunk_buf_idx]));
            ESP_ERROR_CHECK(display_hal_wait_pending(LCD_DRAW_WAIT_TIMEOUT_MS));
            frame_draw_us += esp_timer_get_time() - draw_start_us;

            chunk_buf_idx = (chunk_buf_idx + 1) % LCD_CHUNK_BUF_COUNT;
        }

        window_convert_us += frame_convert_us;
        window_draw_us += frame_draw_us;
        s_display_frame_count++;
        s_locked_fb = -1;

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - last_stats_time >= 3000) {
            uint32_t elapsed_ms = now_ms - last_stats_time;
            uint32_t capture_frames = s_frame_count - last_capture_count;
            uint32_t display_frames = s_display_frame_count - last_display_count;
            uint32_t dropped_frames = (capture_frames > display_frames) ? (capture_frames - display_frames) : 0;
            float capture_fps = (float)capture_frames * 1000.0f / (float)elapsed_ms;
            float display_fps = (float)display_frames * 1000.0f / (float)elapsed_ms;
            uint64_t avg_convert_ms = (display_frames > 0) ? (window_convert_us / display_frames) / 1000 : 0;
            uint64_t avg_draw_ms = (display_frames > 0) ? (window_draw_us / display_frames) / 1000 : 0;

            ESP_LOGI(TAG,
                     "[preview] capture=%.1f fps, display=%.1f fps, total_capture=%lu, total_display=%lu, dropped=%lu, errors=%lu, pixfmt=%s, avg_convert=%llums, avg_draw=%llums",
                     capture_fps,
                     display_fps,
                     (unsigned long)s_frame_count,
                     (unsigned long)s_display_frame_count,
                     (unsigned long)dropped_frames,
                     (unsigned long)s_error_count,
                     pixfmt_name(s_pixfmt),
                     (unsigned long long)avg_convert_ms,
                     (unsigned long long)avg_draw_ms);

            last_stats_time = now_ms;
            last_capture_count = s_frame_count;
            last_display_count = s_display_frame_count;
            window_convert_us = 0;
            window_draw_us = 0;
        }
    }

    esp_cam_ctlr_stop(s_cam_ctlr);
    esp_cam_ctlr_disable(s_cam_ctlr);
    ESP_LOGI(TAG, "demo stopped");
}
