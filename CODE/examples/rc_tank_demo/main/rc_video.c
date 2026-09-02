/**
 * @file rc_video.c
 * @brief RC Tank Demo - 视频层实现
 *
 * R1 风险: ESP32-S3 软件 JPEG 编码性能待实机验证
 */

#include "rc_video.h"
#include "rc_net.h"
#include "rc_tank_common.h"
#include "rc_tank_screen.h"
#include "rc_capture_pool.h"
#include "rc_capture_pool_target.h"
#include "rc_video_format.h"
#include "rc_video_scale.h"
#include "rc_video_yuv_scale.h"
#include "rc_video_synthetic.h"
#include "rc_video_display_plan.h"
#include "rc_video_theory.h"
#include "rc_video_policy.h"
#include "rc_remote_display_policy.h"
#include "rc_display_orientation.h"
#include "rc_lcd_color.h"
#include "rc_joystick.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "camera_hal.h"
#include "display_hal.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_system.h"
#if defined(CONFIG_RC_TANK_ROLE_TANK) && defined(CONFIG_RC_TANK_STABLE_CAPTURE)
#include "rc_dvp_staged_override.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// esp_new_jpeg 编码解码器
#include "esp_jpeg_enc.h"
#include "esp_jpeg_dec.h"

#include <string.h>

static const char *TAG = "rc_video";

#define CAM_H_RES 640
#define CAM_V_RES 480
#define CAM_PIXEL_BYTES 2U
#define CAM_INPUT_COLOR_TYPE CAM_CTLR_COLOR_YUV422
#define CAM_SENSOR_FORMAT_NAME "VYUY"
#define JPEG_H_RES RC_VIDEO_ENCODE_WIDTH
#define JPEG_V_RES RC_VIDEO_ENCODE_HEIGHT
#define FB_COUNT RC_CAPTURE_POOL_COUNT
#define CAMERA_FRAME_BYTES ((size_t)CAM_H_RES * CAM_V_RES * CAM_PIXEL_BYTES)
#define RC_REMOTE_VIDEO_X 80U
#define RC_REMOTE_VIDEO_Y 0U
#define RC_TANK_C16O_PAUSE_ACK_TIMEOUT_MS 50U
#define RC_TANK_FORMAL_REPAIR_FIRST_Y 147U
#define RC_TANK_FORMAL_REPAIR_ROWS 3U
#define RC_TANK_FORMAL_LUMA_GAIN_PERCENT 100U
#define RC_TANK_VIDEO_STATS_PERIOD_FRAMES 300U
#define RC_VIDEO_ERROR_LOG_PERIOD 30U

#if defined(CONFIG_RC_TANK_ROLE_TANK) && defined(CONFIG_RC_TANK_STABLE_CAPTURE)
#define RC_TANK_USE_STAGED_DVP 1
#endif

#define CAMERA_FRAME_ALLOC_BYTES CAMERA_FRAME_BYTES

#if defined(CONFIG_RC_TANK_ROLE_REMOTE)
typedef struct {
    bool enabled;
    int knob_dx;
    int knob_dy;
    bool joy_active;
} rc_lcd_presenter_overlay_t;

static esp_err_t rc_lcd_presenter_submit_chunk(
    uint16_t *chunk,
    uint32_t x,
    uint32_t width,
    uint32_t y,
    uint32_t lines)
{
    if (NULL == chunk || 0U == width || 0U == lines ||
        !rc_remote_lcd_chunk_fits_dma(width, lines)) {
        return ESP_ERR_INVALID_ARG;
    }

    while (ESP_OK == display_hal_wait_pending(0)) {
    }
    esp_err_t ret = display_hal_draw_bitmap_rgb565(
        (int)x, (int)y, (int)width, (int)lines, chunk);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "Presenter draw failed: y=%lu lines=%lu err=%s",
                 (unsigned long)y, (unsigned long)lines,
                 esp_err_to_name(ret));
        return ret;
    }
    ret = display_hal_wait_pending(200);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "Presenter DMA wait failed: y=%lu err=%s",
                 (unsigned long)y, esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t rc_lcd_presenter_present_canvas(
    const uint16_t *canvas,
    uint16_t *chunk,
    uint32_t width,
    uint32_t height,
    uint32_t chunk_lines,
    const rc_lcd_presenter_overlay_t *overlay)
{
    if (NULL == canvas || NULL == chunk ||
        0U == width || 0U == height || 0U == chunk_lines ||
        chunk_lines > height) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t y = 0U; y < height; y += chunk_lines) {
        const uint32_t remaining = height - y;
        const uint32_t lines = (remaining < chunk_lines) ? remaining : chunk_lines;
        if (!rc_remote_lcd_chunk_fits_dma(width, lines)) {
            ESP_LOGE(TAG, "Presenter chunk exceeds DMA limit: y=%lu lines=%lu",
                     (unsigned long)y, (unsigned long)lines);
            return ESP_ERR_INVALID_SIZE;
        }

        memcpy(chunk,
               canvas + (size_t)y * width,
               (size_t)lines * width * sizeof(uint16_t));

        if (NULL != overlay && overlay->enabled) {
            rc_joystick_render_overlay_region(
                chunk, (int)width, (int)lines, 0, (int)y,
                overlay->knob_dx, overlay->knob_dy, overlay->joy_active);
        }

        const esp_err_t ret = rc_lcd_presenter_submit_chunk(
            chunk, 0U, width, y, lines);
        if (ESP_OK != ret) {
            return ret;
        }
    }
    return ESP_OK;
}
#endif

#if defined(CONFIG_RC_TANK_ROLE_TANK)

/* ========== 坦克侧: 采集+编码+发送 ========== */

static esp_cam_ctlr_handle_t s_cam_ctlr = NULL;
static uint8_t *s_fb[FB_COUNT];
static SemaphoreHandle_t s_frame_ready;
static rc_capture_pool_t s_capture_pool;
static portMUX_TYPE s_capture_pool_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_video_tx_task = NULL;
static volatile uint32_t s_capture_cache_sync_errors = 0U;

static esp_err_t rc_video_sync_captured_frame_for_cpu(uint8_t *frame)
{
    if (NULL == frame) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
    const int flags = ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                      ESP_CACHE_MSYNC_FLAG_INVALIDATE;
#else
    int flags = ESP_CACHE_MSYNC_FLAG_DIR_M2C;
#endif

    const esp_err_t ret = esp_cache_msync(frame, CAMERA_FRAME_BYTES, flags);
    if (ESP_OK != ret) {
        s_capture_cache_sync_errors++;
        if ((1U == s_capture_cache_sync_errors) ||
            (0U == (s_capture_cache_sync_errors % 30U))) {
            ESP_LOGE(TAG,
                     "Capture cache sync failed count=%lu err=%s",
                     (unsigned long)s_capture_cache_sync_errors,
                     esp_err_to_name(ret));
        }
    }
    return ret;
}
static TaskHandle_t s_tank_display_task = NULL;
static volatile bool s_tank_wifi_connected = false;
static volatile bool s_capture_started = false;
// DVP 采集计数器（ISR 累加，消费任务定期读取以独立统计"采集帧率"，与"编码/发送帧率"区分）
static volatile uint32_t s_capture_count = 0;
static volatile uint32_t s_capture_identity_errors = 0;
static volatile uint32_t s_capture_size_errors = 0;
static volatile bool s_synthetic_video = false;
static uint8_t *s_capture_backup_buffer = NULL;




#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
static volatile bool s_stable_capture_pause_after_frame = false;
#endif

static int capture_slot_from_buffer(const void *buffer)
{
    for (int i = 0; i < FB_COUNT; ++i) {
        if (buffer == s_fb[i]) {
            return i;
        }
    }
    return -1;
}


static bool on_trans_finished(esp_cam_ctlr_handle_t handle,
                              esp_cam_ctlr_trans_t *trans,
                              void *user_data)
{
    (void)handle;
    (void)user_data;
    const int slot = (NULL != trans) ? capture_slot_from_buffer(trans->buffer) : -1;
    BaseType_t woken = pdFALSE;

    if (NULL != trans && trans->buffer == s_capture_backup_buffer) {
        return false;
    }

    const bool complete_size = (NULL != trans) &&
                               (CAMERA_FRAME_BYTES == trans->received_size);
    portENTER_CRITICAL_ISR(&s_capture_pool_lock);
    const bool completed = complete_size &&
                           rc_capture_pool_finish_write(&s_capture_pool, slot);
    if (completed) {
        s_capture_count++;
#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
        rc_dvp_staged_request_quiesce();
        s_stable_capture_pause_after_frame = true;
#endif
    } else if (!complete_size && rc_capture_pool_abort_write(&s_capture_pool, slot)) {
        s_capture_size_errors++;
    } else {
        s_capture_identity_errors++;
    }
    portEXIT_CRITICAL_ISR(&s_capture_pool_lock);

    if (completed) {
        xSemaphoreGiveFromISR(s_frame_ready, &woken);
    }
    return (woken == pdTRUE);
}

static bool on_get_new_trans(esp_cam_ctlr_handle_t handle,
                             esp_cam_ctlr_trans_t *trans,
                             void *user_data)
{
    (void)handle;
    (void)user_data;
#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
    if (s_stable_capture_pause_after_frame) {
        s_stable_capture_pause_after_frame = false;
        trans->buffer = NULL;
        trans->buflen = 0U;
        return false;
    }
#endif
    portENTER_CRITICAL_ISR(&s_capture_pool_lock);
    int next = -1;
    const rc_capture_target_t target = rc_capture_pool_select_target(
        &s_capture_pool, NULL != s_capture_backup_buffer, &next);
    portEXIT_CRITICAL_ISR(&s_capture_pool_lock);
    if (RC_CAPTURE_TARGET_SLOT == target && next >= 0) {
        trans->buffer = s_fb[next];
        trans->buflen = CAMERA_FRAME_BYTES;
    } else if (RC_CAPTURE_TARGET_BACKUP == target) {
        trans->buffer = s_capture_backup_buffer;
        trans->buflen = CAMERA_FRAME_BYTES;
    }
    return false;
}

static esp_err_t tank_dvp_controller_create(void)
{
    static const esp_cam_ctlr_dvp_pin_config_t dvp_pins = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {
            BOARD_LAIWFS300_GPIO_CAMERA_D0, BOARD_LAIWFS300_GPIO_CAMERA_D1,
            BOARD_LAIWFS300_GPIO_CAMERA_D2, BOARD_LAIWFS300_GPIO_CAMERA_D3,
            BOARD_LAIWFS300_GPIO_CAMERA_D4, BOARD_LAIWFS300_GPIO_CAMERA_D5,
            BOARD_LAIWFS300_GPIO_CAMERA_D6, BOARD_LAIWFS300_GPIO_CAMERA_D7,
            GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
            GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
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
        .input_data_color_type = CAM_INPUT_COLOR_TYPE,
        .cam_data_width = 8,
        .external_xtal = 1,
        .pin = &dvp_pins,
        .dma_burst_size = 64,
    };

    esp_err_t ret = ESP_OK;
#if defined(RC_TANK_USE_STAGED_DVP)
    ret = rc_cam_ctlr_dvp_init_staged(
        dvp_cfg.ctlr_id, dvp_cfg.clk_src, &dvp_pins);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "DVP GPIO matrix init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    dvp_cfg.pin_dont_init = 1;
    ret = rc_cam_new_dvp_ctlr_staged(&dvp_cfg, &s_cam_ctlr);
#else
    ret = esp_cam_new_dvp_ctlr(&dvp_cfg, &s_cam_ctlr);
#endif
    if (ESP_OK != ret) {
        s_cam_ctlr = NULL;
        return ret;
    }

    const esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = on_get_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    ret = esp_cam_ctlr_register_event_callbacks(s_cam_ctlr, &cbs, NULL);
    if (ESP_OK == ret) {
        ret = esp_cam_ctlr_enable(s_cam_ctlr);
    }
    if (ESP_OK != ret) {
#if defined(RC_TANK_USE_STAGED_DVP)
        (void)rc_cam_del_dvp_ctlr_staged(s_cam_ctlr);
#else
        (void)esp_cam_ctlr_del(s_cam_ctlr);
#endif
        s_cam_ctlr = NULL;
    }
    return ret;
}

esp_err_t rc_video_capture_init(void)
{
    ESP_LOGI(TAG, "Initializing camera (SP0A39 %dx%d %s)",
             CAM_H_RES, CAM_V_RES, CAM_SENSOR_FORMAT_NAME);

    // 初始化摄像头传感器
    esp_err_t ret = board_laiwfs300_camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera sensor init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // 分配帧缓冲 (PSRAM)
    const size_t fb_size = CAMERA_FRAME_ALLOC_BYTES;
    for (int i = 0; i < FB_COUNT; i++) {
        s_fb[i] = heap_caps_aligned_alloc(
            64, fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!s_fb[i]) {
            ESP_LOGE(TAG, "FB %d alloc failed", i);
            return ESP_ERR_NO_MEM;
        }
        memset(s_fb[i], 0, CAMERA_FRAME_BYTES);
    }
    ESP_LOGI(TAG,
             "  %d DMA-capable PSRAM framebuffers allocated (%u KB each)",
             FB_COUNT, (unsigned)(fb_size / 1024));
    s_capture_backup_buffer = heap_caps_aligned_alloc(
        64, fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (NULL == s_capture_backup_buffer) {
        ESP_LOGW(TAG, "Capture backup buffer alloc failed; DVP backpressure fallback unavailable");
    }
    rc_capture_pool_init(&s_capture_pool);


    s_frame_ready = xSemaphoreCreateBinary();
    if (!s_frame_ready) {
        ESP_LOGE(TAG, "Semaphore alloc failed");
        return ESP_ERR_NO_MEM;
    }

    // 配置并启用 DVP 控制器
    ret = tank_dvp_controller_create();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DVP controller create failed: %s", esp_err_to_name(ret));
        return ret;
    }
#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
    ESP_LOGI(TAG,
             "[STABLE-CAPTURE] staged DVP controller created dma=%u half=%u",
             (unsigned)RC_DVP_STAGED_DMA_BUFFER_BYTES,
             (unsigned)RC_DVP_STAGED_DMA_HALF_BYTES);
#endif

    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

static esp_err_t tank_capture_start_once(bool *started_now)
{
    if (NULL == started_now) {
        return ESP_ERR_INVALID_ARG;
    }
    *started_now = false;
    if (s_synthetic_video) {
        return ESP_OK;
    }
    if (!rc_video_controller_ready(s_cam_ctlr)) {
        ESP_LOGE(TAG, "DVP start skipped: camera controller is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_capture_started) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
    ret = rc_dvp_staged_prepare_resume();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "[STABLE-CAPTURE] initial prepare failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
#endif

    ret = esp_cam_ctlr_start(s_cam_ctlr);
    if (ESP_OK == ret) {
        s_capture_started = true;
        *started_now = true;
    }
    return ret;
}


#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
static esp_err_t tank_stable_capture_quiesce(void)
{
    if (!rc_video_controller_ready(s_cam_ctlr) || !s_capture_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)RC_TANK_C16O_PAUSE_ACK_TIMEOUT_MS * 1000LL);
    while (s_stable_capture_pause_after_frame &&
           (esp_timer_get_time() < deadline_us)) {
        vTaskDelay(1U);
    }
    if (s_stable_capture_pause_after_frame) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = rc_dvp_staged_wait_quiesced(
        RC_TANK_C16O_PAUSE_ACK_TIMEOUT_MS);
    if (ESP_OK != ret) {
        return ret;
    }

    ret = esp_cam_ctlr_stop(s_cam_ctlr);
    if (ESP_OK != ret) {
        return ret;
    }
    s_capture_started = false;

    ret = esp_cam_ctlr_disable(s_cam_ctlr);
    if (ESP_OK != ret) {
        return ret;
    }

    return ESP_OK;
}

static esp_err_t tank_stable_capture_resume(void)
{
    if (!rc_video_controller_ready(s_cam_ctlr) || s_capture_started) {
        return ESP_ERR_INVALID_STATE;
    }

    while ((NULL != s_frame_ready) &&
           (pdTRUE == xSemaphoreTake(s_frame_ready, 0U))) {
    }

    portENTER_CRITICAL(&s_capture_pool_lock);
    rc_capture_pool_init(&s_capture_pool);
    portEXIT_CRITICAL(&s_capture_pool_lock);

    esp_err_t ret = esp_cam_ctlr_enable(s_cam_ctlr);
    if (ESP_OK != ret) {
        return ret;
    }

    ret = rc_dvp_staged_prepare_resume();
    if (ESP_OK != ret) {
        (void)esp_cam_ctlr_disable(s_cam_ctlr);
        return ret;
    }

    ret = esp_cam_ctlr_start(s_cam_ctlr);
    if (ESP_OK != ret) {
        (void)esp_cam_ctlr_disable(s_cam_ctlr);
        return ret;
    }

    s_capture_started = true;
    return ESP_OK;
}

static bool tank_prepare_formal_ycbycr(uint8_t *frame,
                                       uint32_t width,
                                       uint32_t height)
{
    if ((NULL == frame) || (0U == width) || (0U != (width & 1U)) ||
        (height <= (RC_TANK_FORMAL_REPAIR_FIRST_Y +
                    RC_TANK_FORMAL_REPAIR_ROWS))) {
        return false;
    }

    const size_t row_bytes = (size_t)width * 2U;
    const size_t frame_bytes = row_bytes * height;
    for (size_t offset = 0U; offset < frame_bytes; offset += 4U) {
        const uint32_t y0 =
            ((uint32_t)frame[offset] * RC_TANK_FORMAL_LUMA_GAIN_PERCENT + 50U) /
            100U;
        const uint32_t y1 =
            ((uint32_t)frame[offset + 2U] *
             RC_TANK_FORMAL_LUMA_GAIN_PERCENT + 50U) / 100U;
        frame[offset] = (uint8_t)((255U < y0) ? 255U : y0);
        frame[offset + 2U] = (uint8_t)((255U < y1) ? 255U : y1);
    }

    const uint8_t *before = frame +
        (size_t)(RC_TANK_FORMAL_REPAIR_FIRST_Y - 1U) * row_bytes;
    const uint8_t *after = frame +
        (size_t)(RC_TANK_FORMAL_REPAIR_FIRST_Y +
                 RC_TANK_FORMAL_REPAIR_ROWS) * row_bytes;
    const uint32_t denominator = RC_TANK_FORMAL_REPAIR_ROWS + 1U;
    for (uint32_t row = 0U; row < RC_TANK_FORMAL_REPAIR_ROWS; ++row) {
        uint8_t *target = frame +
            (size_t)(RC_TANK_FORMAL_REPAIR_FIRST_Y + row) * row_bytes;
        const uint32_t before_weight = RC_TANK_FORMAL_REPAIR_ROWS - row;
        const uint32_t after_weight = row + 1U;
        for (size_t byte = 0U; byte < row_bytes; ++byte) {
            const uint32_t value =
                (uint32_t)before[byte] * before_weight +
                (uint32_t)after[byte] * after_weight + denominator / 2U;
            target[byte] = (uint8_t)(value / denominator);
        }
    }

    return true;
}
#endif


// 视频发送任务
static void video_tx_task(void *arg)
{
    (void)arg;

    const size_t subsample_size = JPEG_H_RES * JPEG_V_RES * 2U;
    uint8_t *subsample_buf = jpeg_calloc_align(subsample_size, 16);
    if (NULL == subsample_buf) {
        ESP_LOGE(TAG, "Subsample buffer alloc failed");
        vTaskDelete(NULL);
        return;
    }

    const size_t jpeg_buf_size = JPEG_H_RES * JPEG_V_RES;
    uint8_t *jpeg_buf = heap_caps_malloc(jpeg_buf_size, MALLOC_CAP_SPIRAM);
    if (NULL == jpeg_buf) {
        ESP_LOGE(TAG, "JPEG buffer alloc failed");
        jpeg_free_align(subsample_buf);
        vTaskDelete(NULL);
        return;
    }

    jpeg_enc_handle_t jpeg_handle = NULL;
    jpeg_enc_config_t enc_cfg = {
        .width = JPEG_H_RES,
        .height = JPEG_V_RES,
        .src_type = JPEG_PIXEL_FORMAT_YCbYCr,
        .subsampling = JPEG_SUBSAMPLE_422,
        .quality = RC_VIDEO_JPEG_QUALITY,
        .rotate = JPEG_ROTATE_0D,
        .task_enable = false,
    };
    const jpeg_error_t open_ret = jpeg_enc_open(&enc_cfg, &jpeg_handle);
    if ((JPEG_ERR_OK != open_ret) || (NULL == jpeg_handle)) {
        ESP_LOGE(TAG, "JPEG encoder open failed: %d", open_ret);
        jpeg_free_align(subsample_buf);
        heap_caps_free(jpeg_buf);
        vTaskDelete(NULL);
        return;
    }

    const size_t send_buf_size = rc_video_tx_buffer_bytes(
        (uint32_t)jpeg_buf_size, (uint32_t)sizeof(rc_video_header_t));
    uint8_t *send_buf = heap_caps_malloc(
        send_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == send_buf) {
        ESP_LOGE(TAG, "Send buffer alloc failed: %u bytes",
                 (unsigned)send_buf_size);
        jpeg_free_align(subsample_buf);
        heap_caps_free(jpeg_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Video TX task started (%ux%u Q=%d)",
             JPEG_H_RES, JPEG_V_RES, RC_VIDEO_JPEG_QUALITY);

    uint32_t frame_count = 0U;
    uint32_t send_error_count = 0U;
    uint32_t capture_timeout_count = 0U;
    uint32_t last_capture_count = s_capture_count;
    uint16_t seq = 0U;
    int64_t last_stats_time = esp_timer_get_time();

    while (1) {
        bool prepared = false;
        if (s_synthetic_video) {
            const uint32_t synthetic_frame = s_capture_count++;
            prepared = rc_video_synthetic_fill_ycbycr(
                subsample_buf, JPEG_H_RES, JPEG_V_RES, synthetic_frame);
        } else {
            if (pdTRUE != xSemaphoreTake(
                    s_frame_ready, pdMS_TO_TICKS(2000))) {
                capture_timeout_count++;
                if ((1U == capture_timeout_count) ||
                    (0U == (capture_timeout_count %
                            RC_VIDEO_ERROR_LOG_PERIOD))) {
                    ESP_LOGW(TAG, "Capture timeout count=%lu",
                             (unsigned long)capture_timeout_count);
                }
                continue;
            }

            portENTER_CRITICAL(&s_capture_pool_lock);
            const int fb_idx =
                rc_capture_pool_acquire_latest(&s_capture_pool);
            portEXIT_CRITICAL(&s_capture_pool_lock);
            if (0 > fb_idx) {
                continue;
            }

#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
            const esp_err_t quiesce_ret = tank_stable_capture_quiesce();
            if (ESP_OK != quiesce_ret) {
                portENTER_CRITICAL(&s_capture_pool_lock);
                (void)rc_capture_pool_release_read(
                    &s_capture_pool, fb_idx);
                portEXIT_CRITICAL(&s_capture_pool_lock);
                ESP_LOGE(TAG,
                         "[STABLE-CAPTURE] quiesce failed err=%s; restarting",
                         esp_err_to_name(quiesce_ret));
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
#endif

            (void)rc_video_sync_captured_frame_for_cpu(s_fb[fb_idx]);
            prepared = rc_video_scale_vyuy_to_ycbycr(
                s_fb[fb_idx], CAM_H_RES, CAM_V_RES,
                subsample_buf, JPEG_H_RES, JPEG_V_RES);
#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
            if (prepared) {
                prepared = tank_prepare_formal_ycbycr(
                    subsample_buf, JPEG_H_RES, JPEG_V_RES);
            }
#endif

            portENTER_CRITICAL(&s_capture_pool_lock);
            const bool released =
                rc_capture_pool_release_read(&s_capture_pool, fb_idx);
            portEXIT_CRITICAL(&s_capture_pool_lock);
            if (!released) {
                ESP_LOGE(TAG, "Capture buffer release failed: slot=%d",
                         fb_idx);
            }

#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
            const esp_err_t resume_ret = tank_stable_capture_resume();
            if (ESP_OK != resume_ret) {
                ESP_LOGE(TAG,
                         "[STABLE-CAPTURE] resume failed err=%s; restarting",
                         esp_err_to_name(resume_ret));
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
#endif
        }

        if (!prepared) {
            ESP_LOGE(TAG, "YUV source preparation failed");
            continue;
        }

        int jpeg_size = 0;
        const jpeg_error_t encode_ret = jpeg_enc_process(
            jpeg_handle, subsample_buf, subsample_size,
            jpeg_buf, jpeg_buf_size, &jpeg_size);
        if ((JPEG_ERR_OK != encode_ret) || (0 >= jpeg_size)) {
            ESP_LOGW(TAG, "JPEG encode failed: %d", encode_ret);
            continue;
        }

        const rc_video_header_t hdr = {
            .magic = RC_VIDEO_MAGIC,
            .length = (uint16_t)jpeg_size,
            .seq = seq++,
            .reserved = 0,
        };
        memcpy(send_buf, &hdr, sizeof(hdr));
        memcpy(send_buf + sizeof(hdr), jpeg_buf, (size_t)jpeg_size);
        const esp_err_t send_ret = rc_net_video_send(
            send_buf, sizeof(hdr) + (size_t)jpeg_size);
        if (ESP_OK != send_ret) {
            send_error_count++;
            if ((1U == send_error_count) ||
                (0U == (send_error_count % RC_VIDEO_ERROR_LOG_PERIOD))) {
                ESP_LOGW(TAG, "Video send failed count=%lu err=%s",
                         (unsigned long)send_error_count,
                         esp_err_to_name(send_ret));
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        frame_count++;
        if (0U == (frame_count % RC_TANK_VIDEO_STATS_PERIOD_FRAMES)) {
            const int64_t now = esp_timer_get_time();
            const uint32_t capture_now = s_capture_count;
            const float window_s =
                (float)(now - last_stats_time) / 1000000.0f;
            const float capture_fps = (0.0f < window_s)
                ? (float)(capture_now - last_capture_count) / window_s
                : 0.0f;
            const float send_fps = (0.0f < window_s)
                ? (float)RC_TANK_VIDEO_STATS_PERIOD_FRAMES / window_s
                : 0.0f;
            ESP_LOGI(TAG,
                     "Video TX frames=%lu cap_fps=%.1f send_fps=%.1f jpeg=%uB size_err=%lu identity_err=%lu",
                     (unsigned long)frame_count, capture_fps, send_fps,
                     (unsigned)jpeg_size,
                     (unsigned long)s_capture_size_errors,
                     (unsigned long)s_capture_identity_errors);
            last_capture_count = capture_now;
            last_stats_time = now;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t rc_video_start_tank(void)
{
    if (NULL != s_video_tx_task) {
        return ESP_OK;
    }

    bool capture_started_now = false;
    esp_err_t ret = tank_capture_start_once(&capture_started_now);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "DVP start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const BaseType_t task_ret = xTaskCreate(
        video_tx_task, "video_tx", 8192, NULL,
        configMAX_PRIORITIES - 3, &s_video_tx_task);
    if (pdPASS != task_ret) {
        ESP_LOGE(TAG, "video_tx task create failed");
        if (capture_started_now && (NULL != s_cam_ctlr)) {
            (void)esp_cam_ctlr_stop(s_cam_ctlr);
            s_capture_started = false;
        }
        s_video_tx_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tank video started (%s source)",
             s_synthetic_video ? "synthetic" : "camera");
    return ESP_OK;
}

#endif

void rc_video_enable_synthetic(bool enabled)
{
#if defined(CONFIG_RC_TANK_ROLE_TANK)
    s_synthetic_video = enabled;
    if (enabled) {
        ESP_LOGW(TAG, "Synthetic video source enabled (camera unavailable)");
    }
#else
    (void)enabled;
#endif
}

#if defined(CONFIG_RC_TANK_ROLE_REMOTE)

/* ========== 遥控器侧: 接收+解码+显示 ========== */

#include "display_hal.h"
#include "rc_joystick.h"
#include "rc_control.h"
#include "rc_video_latest_frame.h"

static TaskHandle_t s_video_rx_task = NULL;
static TaskHandle_t s_video_net_rx_task = NULL;

#define RC_VIDEO_RX_SLOT_COUNT 3U
#define RC_REMOTE_WIFI_ICON_X 10U
#define RC_REMOTE_WIFI_ICON_Y 10U
#define RC_REMOTE_WIFI_ICON_W 20U
#define RC_REMOTE_WIFI_ICON_H 15U
#define RC_REMOTE_VIDEO_STATS_PERIOD_FRAMES 300U

typedef struct {
    uint8_t *jpeg_buf;
    size_t jpeg_len;
    uint16_t seq;
} rc_video_rx_slot_t;

static rc_video_rx_slot_t s_video_rx_slots[RC_VIDEO_RX_SLOT_COUNT];
static QueueHandle_t s_video_rx_free_queue = NULL;
static QueueHandle_t s_video_rx_ready_queue = NULL;
static volatile uint32_t s_video_queue_ready_count = 0;
static volatile uint32_t s_video_queue_drop_count = 0;
static volatile uint32_t s_video_stale_drop_count = 0;
static portMUX_TYPE s_remote_wifi_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_remote_wifi_connected = false;
static uint32_t s_remote_wifi_epoch = 0U;
static void video_rx_task(void *arg);

static void remote_video_base_draw_wifi(uint8_t *video_base_buf,
                                        bool connected)
{
    if (NULL == video_base_buf) {
        return;
    }

    uint16_t *canvas = (uint16_t *)video_base_buf;
    const uint16_t color = connected
                               ? rc_lcd_rgb565_be(0, 63, 0)
                               : rc_lcd_rgb565_be(31, 0, 0);
    for (uint32_t y = RC_REMOTE_WIFI_ICON_Y;
         y < (RC_REMOTE_WIFI_ICON_Y + RC_REMOTE_WIFI_ICON_H);
         ++y) {
        uint16_t *row = canvas + (size_t)y * RC_REMOTE_LCD_LOGICAL_W;
        for (uint32_t x = RC_REMOTE_WIFI_ICON_X;
             x < (RC_REMOTE_WIFI_ICON_X + RC_REMOTE_WIFI_ICON_W);
             ++x) {
            row[x] = color;
        }
    }
}

static esp_err_t remote_video_pipeline_prepare(void)
{
    if (s_video_rx_free_queue && s_video_rx_ready_queue) {
        return ESP_OK;
    }

    s_video_rx_free_queue = xQueueCreate(RC_VIDEO_RX_SLOT_COUNT, sizeof(rc_video_rx_slot_t *));
    s_video_rx_ready_queue = xQueueCreate(RC_VIDEO_RX_SLOT_COUNT, sizeof(rc_video_rx_slot_t *));
    if (!s_video_rx_free_queue || !s_video_rx_ready_queue) {
        ESP_LOGE(TAG, "Video RX queue alloc failed");
        return ESP_ERR_NO_MEM;
    }

    const size_t jpeg_buf_size = JPEG_H_RES * JPEG_V_RES;
    for (uint32_t i = 0U; i < RC_VIDEO_RX_SLOT_COUNT; ++i) {
        s_video_rx_slots[i].jpeg_buf = jpeg_calloc_align(jpeg_buf_size, 16);
        s_video_rx_slots[i].jpeg_len = 0U;
        s_video_rx_slots[i].seq = 0U;
        if (!s_video_rx_slots[i].jpeg_buf) {
            ESP_LOGE(TAG, "Video RX slot %lu alloc failed", (unsigned long)i);
            return ESP_ERR_NO_MEM;
        }
        rc_video_rx_slot_t *slot = &s_video_rx_slots[i];
        (void)xQueueSend(s_video_rx_free_queue, &slot, 0);
    }
    return ESP_OK;
}

esp_err_t rc_video_display_init(void)
{
    ESP_LOGI(TAG, "Initializing video display");

    // 初始化 LCD + 背光（board 层完整初始化）
    esp_err_t ret = board_laiwfs300_display_init_with_config(
        RC_REMOTE_LCD_PIXEL_CLOCK_HZ, RC_REMOTE_LCD_INIT_DRAW_BUFFER_LINES);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display init at %u Hz failed, fallback to board default: %s",
                 RC_REMOTE_LCD_PIXEL_CLOCK_HZ, esp_err_to_name(ret));
        ret = board_laiwfs300_display_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board display init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 先在初始化的 240×320 传输几何下填充，避免旋转后超过初始 DMA 上限。
    ret = board_laiwfs300_display_fill_rgb565(0x0000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Fill test failed: %s", esp_err_to_name(ret));
    }

    // 适配坦克摄像头 JPEG 输出 320x240，同时保留既有触摸坐标契约。
    const rc_display_orientation_t orientation =
        rc_display_orientation_for_role(RC_DISPLAY_ROLE_REMOTE);
    ret = display_hal_set_orientation(orientation.swap_xy,
                                      orientation.mirror_x,
                                      orientation.mirror_y);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set landscape orientation failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Display initialized (landscape 320×240) and backlight ON");

    ret = remote_video_pipeline_prepare();
    if (ret != ESP_OK) {
        return ret;
    }
    if (s_video_rx_task == NULL) {
        BaseType_t task_ret = xTaskCreate(video_rx_task, "video_rx", 8192, NULL,
                                          configMAX_PRIORITIES - 3, &s_video_rx_task);
        if (task_ret != pdPASS) {
            s_video_rx_task = NULL;
            ESP_LOGE(TAG, "video_rx display task create failed");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static void video_net_rx_task(void *arg)
{
    (void)arg;

    const size_t jpeg_buf_size = JPEG_H_RES * JPEG_V_RES;
    while (1) {
        rc_video_rx_slot_t *slot = NULL;
        if (xQueueReceive(s_video_rx_free_queue, &slot, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        size_t recv_len = 0;
        uint16_t recv_seq = 0U;
        esp_err_t ret = rc_net_video_recv(slot->jpeg_buf, jpeg_buf_size,
                                          &recv_len, &recv_seq);
        if (ret != ESP_OK || recv_len == 0U) {
            (void)xQueueSend(s_video_rx_free_queue, &slot, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        slot->jpeg_len = recv_len;
        slot->seq = recv_seq;
        if (xQueueSend(s_video_rx_ready_queue, &slot, 0) != pdTRUE) {
            rc_video_rx_slot_t *old_slot = NULL;
            if (xQueueReceive(s_video_rx_ready_queue, &old_slot, 0) == pdTRUE) {
                s_video_queue_drop_count++;
                (void)xQueueSend(s_video_rx_free_queue, &old_slot, 0);
            }
            if (xQueueSend(s_video_rx_ready_queue, &slot, 0) != pdTRUE) {
                s_video_queue_drop_count++;
                (void)xQueueSend(s_video_rx_free_queue, &slot, 0);
            } else {
                s_video_queue_ready_count++;
            }
        } else {
            s_video_queue_ready_count++;
        }
    }
}

// 视频解码与显示任务；网络接收独立运行，显示只消费最新完整帧。
static void video_rx_task(void *arg)
{
    (void)arg;

    const size_t decoded_buf_size = JPEG_H_RES * JPEG_V_RES * 2U;
    const size_t display_buf_size =
        RC_REMOTE_LCD_LOGICAL_W * RC_REMOTE_LCD_LOGICAL_H * 2U;
    uint8_t *decoded_buf = jpeg_calloc_align(decoded_buf_size, 16);
    uint8_t *video_base_buf = jpeg_calloc_align(display_buf_size, 16);
    uint16_t *display_chunk_buf =
        (uint16_t *)heap_caps_aligned_alloc(
            64,
            RC_REMOTE_LCD_LOGICAL_W * RC_REMOTE_LCD_CHUNK_LINES *
                sizeof(uint16_t),
            MALLOC_CAP_DMA);

    if ((NULL == decoded_buf) || (NULL == video_base_buf) ||
        (NULL == display_chunk_buf)) {
        ESP_LOGE(TAG, "Video RX buffer alloc failed");
        if (NULL != decoded_buf) {
            jpeg_free_align(decoded_buf);
        }
        if (NULL != video_base_buf) {
            jpeg_free_align(video_base_buf);
        }
        if (NULL != display_chunk_buf) {
            heap_caps_free(display_chunk_buf);
        }
        vTaskDelete(NULL);
        return;
    }

    memset(video_base_buf, 0, display_buf_size);
    ESP_LOGI(TAG,
             "Video RX/display task started (240x180 at x=%u y=%u)",
             RC_REMOTE_VIDEO_X, RC_REMOTE_VIDEO_Y);

    uint32_t frame_count = 0U;
    uint32_t stats_start_frame = 0U;
    int64_t stats_start_us = esp_timer_get_time();
    rc_remote_display_policy_t display_policy = {0};

    while (1) {
        bool video_updated = false;
        rc_video_rx_slot_t *slot = NULL;
        if (pdTRUE == xQueueReceive(
                s_video_rx_ready_queue, &slot, pdMS_TO_TICKS(20))) {
            rc_video_rx_slot_t *newer_slot = NULL;
            while (pdTRUE == xQueueReceive(
                    s_video_rx_ready_queue, &newer_slot, 0)) {
                s_video_stale_drop_count++;
                (void)xQueueSend(s_video_rx_free_queue, &slot, 0);
                slot = newer_slot;
            }

            jpeg_dec_config_t dec_cfg = {
                .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,
                .rotate = JPEG_ROTATE_0D,
            };
            jpeg_dec_handle_t dec = NULL;
            jpeg_error_t decode_ret = JPEG_ERR_OK;
            jpeg_dec_header_info_t header = {0};
            if ((JPEG_ERR_OK != jpeg_dec_open(&dec_cfg, &dec)) ||
                (NULL == dec)) {
                ESP_LOGW(TAG, "JPEG decoder open failed");
                decode_ret = JPEG_ERR_INVALID_PARAM;
            } else {
                jpeg_dec_io_t io = {
                    .inbuf = slot->jpeg_buf,
                    .inbuf_len = (int)slot->jpeg_len,
                    .outbuf = decoded_buf,
                };
                decode_ret = jpeg_dec_parse_header(dec, &io, &header);
                if (JPEG_ERR_OK == decode_ret) {
                    decode_ret = jpeg_dec_process(dec, &io);
                }
                jpeg_dec_close(dec);
            }

            if (JPEG_ERR_OK != decode_ret) {
                ESP_LOGW(TAG, "JPEG decode failed: %d", decode_ret);
            } else if ((JPEG_H_RES != header.width) ||
                       (JPEG_V_RES != header.height)) {
                ESP_LOGW(TAG, "Unsupported video geometry %dx%d",
                         header.width, header.height);
            } else {
                const uint16_t *pixels =
                    (const uint16_t *)decoded_buf;
                for (uint32_t row = 0U; row < JPEG_V_RES; ++row) {
                    memcpy(
                        (uint16_t *)video_base_buf +
                            (RC_REMOTE_VIDEO_Y + row) *
                                RC_REMOTE_LCD_LOGICAL_W +
                            RC_REMOTE_VIDEO_X,
                        pixels + row * JPEG_H_RES,
                        JPEG_H_RES * sizeof(uint16_t));
                }
                video_updated = true;
            }
            (void)xQueueSend(s_video_rx_free_queue, &slot, 0);
        }

        int knob_dx = 0;
        int knob_dy = 0;
        bool joy_active = false;
        bool wifi_connected = false;
        uint32_t wifi_epoch = 0U;
        rc_joystick_get_state(&knob_dx, &knob_dy, &joy_active);
        portENTER_CRITICAL(&s_remote_wifi_state_lock);
        wifi_connected = s_remote_wifi_connected;
        wifi_epoch = s_remote_wifi_epoch;
        portEXIT_CRITICAL(&s_remote_wifi_state_lock);

        const bool wifi_changed =
            !display_policy.presented ||
            (display_policy.network_epoch != wifi_epoch);
        if (wifi_changed) {
            remote_video_base_draw_wifi(
                video_base_buf, wifi_connected);
            ESP_LOGI(TAG,
                     "Remote WiFi icon: connected=%d epoch=%lu",
                     wifi_connected ? 1 : 0,
                     (unsigned long)wifi_epoch);
        }
        if (!rc_remote_display_should_present(
                &display_policy, video_updated,
                knob_dx, knob_dy, joy_active, wifi_epoch)) {
            continue;
        }

        const rc_lcd_presenter_overlay_t overlay = {
            .enabled = true,
            .knob_dx = knob_dx,
            .knob_dy = knob_dy,
            .joy_active = joy_active,
        };
        const esp_err_t present_ret =
            rc_lcd_presenter_present_canvas(
                (const uint16_t *)video_base_buf,
                display_chunk_buf,
                RC_REMOTE_LCD_LOGICAL_W,
                RC_REMOTE_LCD_LOGICAL_H,
                RC_REMOTE_LCD_CHUNK_LINES,
                &overlay);
        if (ESP_OK != present_ret) {
            display_policy.presented = false;
            continue;
        }

        if (!video_updated) {
            continue;
        }

        frame_count++;
        if (0U == (frame_count %
                   RC_REMOTE_VIDEO_STATS_PERIOD_FRAMES)) {
            const int64_t now = esp_timer_get_time();
            const uint32_t stats_frames =
                frame_count - stats_start_frame;
            const float elapsed_s =
                (float)(now - stats_start_us) / 1000000.0f;
            const float fps = (0.0f < elapsed_s)
                ? (float)stats_frames / elapsed_s : 0.0f;
            ESP_LOGI(TAG,
                     "Video RX frames=%lu fps=%.1f queue_ready=%lu queue_drop=%lu stale_drop=%lu",
                     (unsigned long)frame_count, fps,
                     (unsigned long)s_video_queue_ready_count,
                     (unsigned long)s_video_queue_drop_count,
                     (unsigned long)s_video_stale_drop_count);
            stats_start_frame = frame_count;
            stats_start_us = now;
        }
    }
}

esp_err_t rc_video_start_remote(void)
{
    if (!s_video_rx_free_queue || !s_video_rx_ready_queue || !s_video_rx_task) {
        ESP_LOGE(TAG, "Remote video display pipeline is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_video_net_rx_task != NULL) {
        ESP_LOGW(TAG, "video_net_rx task already running");
        return ESP_OK;
    }

    BaseType_t r = xTaskCreate(video_net_rx_task, "video_net_rx", 6144, NULL,
                               configMAX_PRIORITIES - 2, &s_video_net_rx_task);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "video_net_rx task create failed");
        s_video_net_rx_task = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Remote video started");
    return ESP_OK;
}

#endif

/* ========== 跨角色占位 ========== */

#if defined(CONFIG_RC_TANK_ROLE_TANK)
static esp_err_t tank_draw_framebuffer(const uint16_t *fb, uint16_t *chunk)
{
    if (NULL == fb || NULL == chunk) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint32_t y = 0U; y < RC_TANK_STATUS_LCD_H;
         y += RC_TANK_STATUS_LCD_CHUNK_LINES) {
        const uint32_t lines = rc_tank_status_lcd_chunk_lines(y);
        for (uint32_t row = 0U; row < lines; ++row) {
            memcpy(chunk + row * RC_TANK_STATUS_LCD_W,
                   fb + (y + row) * RC_TANK_STATUS_LCD_W,
                   RC_TANK_STATUS_LCD_W * sizeof(uint16_t));
        }
        while (display_hal_wait_pending(0) == ESP_OK) {
        }
        esp_err_t ret = display_hal_draw_bitmap_rgb565(
            0, (int)y, RC_TANK_STATUS_LCD_W, (int)lines, chunk);
        if (ESP_OK != ret) {
            return ret;
        }
        ret = display_hal_wait_pending(200);
        if (ESP_OK != ret) {
            return ret;
        }
    }
    return ESP_OK;
}



static void tank_display_task(void *arg)
{
    (void)arg;
    uint16_t *fb = (uint16_t *)heap_caps_malloc(
        RC_TANK_SCREEN_W * RC_TANK_SCREEN_H * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == fb) {
        ESP_LOGE(TAG, "Tank display framebuffer alloc failed");
        s_tank_display_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    uint16_t *chunk = (uint16_t *)heap_caps_aligned_alloc(
        64, RC_TANK_STATUS_LCD_W * RC_TANK_STATUS_LCD_CHUNK_LINES *
            sizeof(uint16_t),
        MALLOC_CAP_DMA);
    if (NULL == chunk) {
        ESP_LOGE(TAG, "Tank display DMA buffer alloc failed");
        heap_caps_free(fb);
        s_tank_display_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Tank display task started");
    while (1) {
        rc_tank_screen_render(fb, RC_TANK_SCREEN_W, RC_TANK_SCREEN_H,
                              s_tank_wifi_connected, 75);
        const esp_err_t ret = tank_draw_framebuffer(fb, chunk);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "Tank status display failed: %s",
                     esp_err_to_name(ret));
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    }
}


void rc_video_set_network_connected(bool connected)
{
    s_tank_wifi_connected = connected;
    if (NULL != s_tank_display_task) {
        xTaskNotifyGive(s_tank_display_task);
    }
}

esp_err_t rc_video_display_init(void)
{
    // Tank 侧显示像素坦克 + WiFi/电量状态（REQ-035-006B）
    ESP_LOGI(TAG, "Initializing Tank display");
    /* The landscape path submits 320x60 chunks after swap_xy.  Initialize
     * with 80 portrait lines so the SPI DMA limit is at least 240x80x2,
     * which is exactly 320x60x2 after rotation. */
    esp_err_t ret = board_laiwfs300_display_init_with_config(
        RC_TANK_LCD_PIXEL_CLOCK_HZ,
        RC_VIDEO_LCD_INIT_DRAW_BUFFER_LINES);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board display init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // swap_xy 后镜像 Y 轴，修正该安装方向上的物理水平翻转。
    const rc_display_orientation_t orientation =
        rc_display_orientation_for_role(RC_DISPLAY_ROLE_TANK);
    ret = display_hal_set_orientation(orientation.swap_xy,
                                      orientation.mirror_x,
                                      orientation.mirror_y);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tank landscape orientation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (s_tank_display_task == NULL) {
        BaseType_t task_ret = xTaskCreate(tank_display_task, "tank_display", 4096, NULL,
                                          tskIDLE_PRIORITY + 1, &s_tank_display_task);
        if (task_ret != pdPASS) {
            s_tank_display_task = NULL;
            ESP_LOGE(TAG, "Tank status display task create failed");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Tank display initialized (status task owns LCD)");
    return ESP_OK;
}
esp_err_t rc_video_start_remote(void) { return ESP_ERR_NOT_SUPPORTED; }
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
esp_err_t rc_video_capture_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t rc_video_start_tank(void) { return ESP_ERR_NOT_SUPPORTED; }
void rc_video_set_network_connected(bool connected)
{
    portENTER_CRITICAL(&s_remote_wifi_state_lock);
    if (s_remote_wifi_connected != connected) {
        s_remote_wifi_connected = connected;
        s_remote_wifi_epoch++;
    }
    portEXIT_CRITICAL(&s_remote_wifi_state_lock);
}
#endif
