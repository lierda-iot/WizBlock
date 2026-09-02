#include "hotplug_manager.h"
#include "board_hotplug_pins.h"
#include "board_laiwfs300.h"
#include "hotplug_motor_runtime.h"
#include "board_adc.h"
#include "board_pins.h"
#include "bus_i2c.h"
#include "camera_hal.h"
#include "display_hal.h"

#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "hotplug_demo";

extern esp_err_t slot_expansion_c0_init(adc_oneshot_unit_handle_t adc_handle);
extern esp_err_t slot_motor_d0_init(void);
extern esp_err_t slot_lcd_init(i2c_master_bus_handle_t i2c_bus);
extern esp_err_t slot_camera_init(i2c_master_bus_handle_t i2c_bus);
extern void slot_lcd_set_available(bool available);
extern bool slot_lcd_is_available(void);
extern bool slot_camera_is_ready(void);
extern void app_coordinator_start(void);

/* --- Camera DVP pipeline (app-layer) --- */
#define CAM_H_RES  640
#define CAM_V_RES  480
#define FB_COUNT   3

static esp_cam_ctlr_handle_t s_cam_ctlr;
static uint8_t *s_fb[FB_COUNT];
static SemaphoreHandle_t s_frame_ready;
static volatile int s_active_fb;
static volatile int s_locked_fb = -1;
static volatile uint32_t s_frame_count;
static bool s_camera_ready;

static bool on_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle; (void)trans; (void)user_data;
    s_frame_count++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_ready, &woken);
    return (woken == pdTRUE);
}

static bool on_get_new_trans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    (void)handle; (void)user_data;
    int next = (s_active_fb + 1) % FB_COUNT;
    if (next == s_locked_fb) {
        next = (next + 1) % FB_COUNT;
    }
    trans->buffer = s_fb[next];
    trans->buflen = CAM_H_RES * CAM_V_RES * 2;
    s_active_fb = next;
    return false;
}

static void camera_pipeline_start(void)
{
    ESP_LOGI(TAG, "Camera detected, initializing DVP pipeline...");
    esp_err_t ret = board_laiwfs300_camera_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "sensor init failed: %s", esp_err_to_name(ret));
        return;
    }

    size_t fb_size = CAM_H_RES * CAM_V_RES * 2;
    for (int i = 0; i < FB_COUNT; i++) {
        s_fb[i] = heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM);
        if (NULL == s_fb[i]) {
            ESP_LOGE(TAG, "framebuffer %d alloc failed", i);
            goto cleanup_fb;
        }
        memset(s_fb[i], 0, fb_size);
    }

    s_frame_ready = xSemaphoreCreateBinary();
    esp_cam_ctlr_dvp_pin_config_t dvp_pins = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io = {
            [0] = BOARD_LAIWFS300_GPIO_CAMERA_D0, [1] = BOARD_LAIWFS300_GPIO_CAMERA_D1,
            [2] = BOARD_LAIWFS300_GPIO_CAMERA_D2, [3] = BOARD_LAIWFS300_GPIO_CAMERA_D3,
            [4] = BOARD_LAIWFS300_GPIO_CAMERA_D4, [5] = BOARD_LAIWFS300_GPIO_CAMERA_D5,
            [6] = BOARD_LAIWFS300_GPIO_CAMERA_D6, [7] = BOARD_LAIWFS300_GPIO_CAMERA_D7,
            [8] = GPIO_NUM_NC, [9] = GPIO_NUM_NC, [10] = GPIO_NUM_NC, [11] = GPIO_NUM_NC,
            [12] = GPIO_NUM_NC, [13] = GPIO_NUM_NC, [14] = GPIO_NUM_NC, [15] = GPIO_NUM_NC,
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
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "DVP controller failed: %s", esp_err_to_name(ret));
        goto cleanup_sem;
    }

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = on_get_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    ESP_ERROR_CHECK(esp_cam_ctlr_register_event_callbacks(s_cam_ctlr, &cbs, NULL));
    ESP_ERROR_CHECK(esp_cam_ctlr_enable(s_cam_ctlr));
    s_active_fb = 0;
    s_frame_count = 0;
    ESP_ERROR_CHECK(esp_cam_ctlr_start(s_cam_ctlr));

    s_camera_ready = true;
    ESP_LOGI(TAG, "DVP capture running, camera READY");
    return;

cleanup_sem:
    if (NULL != s_frame_ready) { vSemaphoreDelete(s_frame_ready); s_frame_ready = NULL; }
cleanup_fb:
    for (int i = 0; i < FB_COUNT; i++) {
        if (NULL != s_fb[i]) { heap_caps_free(s_fb[i]); s_fb[i] = NULL; }
    }
    camera_hal_deinit();
}

static void camera_pipeline_stop(void)
{
    ESP_LOGI(TAG, "Camera removed, releasing DVP pipeline");
    s_camera_ready = false;
    if (NULL != s_cam_ctlr) {
        esp_cam_ctlr_stop(s_cam_ctlr);
        esp_cam_ctlr_disable(s_cam_ctlr);
        esp_cam_ctlr_del(s_cam_ctlr);
        s_cam_ctlr = NULL;
    }
    for (int i = 0; i < FB_COUNT; i++) {
        if (NULL != s_fb[i]) { heap_caps_free(s_fb[i]); s_fb[i] = NULL; }
    }
    if (NULL != s_frame_ready) { vSemaphoreDelete(s_frame_ready); s_frame_ready = NULL; }
    camera_hal_deinit();
    ESP_LOGI(TAG, "camera resources released");
}
bool slot_camera_is_ready(void)
{
    return s_camera_ready;
}

esp_err_t slot_camera_acquire_frame(uint8_t **out_buf, size_t *out_len)
{
    if (!s_camera_ready || NULL == s_frame_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pdTRUE != xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(2000))) {
        return ESP_ERR_TIMEOUT;
    }
    int fb_idx = (s_active_fb + FB_COUNT - 1) % FB_COUNT;
    s_locked_fb = fb_idx;
    esp_cache_msync(s_fb[fb_idx], CAM_H_RES * CAM_V_RES * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    *out_buf = s_fb[fb_idx];
    *out_len = CAM_H_RES * CAM_V_RES * 2;
    return ESP_OK;
}

void slot_camera_release_frame(void)
{
    s_locked_fb = -1;
}

static void motor_on_insert(void)
{
    ESP_LOGI(TAG, "D0 detected, initializing motor runtime");
    esp_err_t ret = hotplug_motor_runtime_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "motor runtime init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = hotplug_motor_runtime_forward_100();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "motor forward failed: %s", esp_err_to_name(ret));
        hotplug_motor_runtime_deinit();
    }
}

static void motor_on_remove(void)
{
    ESP_LOGI(TAG, "D0 motor board removed, stopping and deinitializing motor runtime");
    esp_err_t ret = hotplug_motor_runtime_deinit();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "motor runtime deinit completed with error: %s",
                 esp_err_to_name(ret));
    }
}

/* --- LCD init (app-layer) --- */
static void lcd_on_insert(void)
{
    ESP_LOGI(TAG, "LCD detected, initializing display");
    esp_err_t ret = board_laiwfs300_display_init_with_config(20000000, 80);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(ret));
        return;
    }
    slot_lcd_set_available(true);
    ESP_LOGI(TAG, "LCD READY");
}

static void lcd_on_remove(void)
{
    ESP_LOGI(TAG, "LCD removed, releasing display");
    slot_lcd_set_available(false);
    board_laiwfs300_display_deinit();
    ESP_LOGI(TAG, "display resources released");
}

/* --- Unified hotplug event handler (app-layer) --- */
static void on_hotplug_event(const char *slot_name, hotplug_state_t new_state, void *user_ctx)
{
    (void)user_ctx;
    if (0 == strcmp(slot_name, HOTPLUG_SLOT_NAME_LCD)) {
        if (HOTPLUG_STATE_PRESENT == new_state) {
            lcd_on_insert();
        } else {
            lcd_on_remove();
        }
    } else if (0 == strcmp(slot_name, HOTPLUG_SLOT_NAME_CAMERA)) {
        if (HOTPLUG_STATE_PRESENT == new_state) {
            camera_pipeline_start();
        } else {
            camera_pipeline_stop();
        }
    } else if (0 == strcmp(slot_name, HOTPLUG_SLOT_NAME_D0)) {
        if (HOTPLUG_STATE_PRESENT == new_state) {
            motor_on_insert();
        } else {
            motor_on_remove();
        }
    } else if (0 == strcmp(slot_name, HOTPLUG_SLOT_NAME_C0)) {
        if (HOTPLUG_STATE_PRESENT == new_state) {
            ESP_LOGI(TAG, "C0 expansion board detected");
        } else {
            ESP_LOGI(TAG, "C0 expansion board lost");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Module Hotplug Demo starting");
    ESP_LOGI(TAG, "startup delayed %d ms for log capture",
             HOTPLUG_STARTUP_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(HOTPLUG_STARTUP_DELAY_MS));
    ESP_LOGI(TAG, "startup delay complete, initializing demo");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_unit_handle_t adc_handle = board_adc_handle();
    if (NULL == adc_handle) {
        ESP_LOGE(TAG, "ADC handle not available from BSP");
        return;
    }

    i2c_master_bus_handle_t i2c_bus = bus_i2c_master_bus();
    if (NULL == i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not available");
        return;
    }

    ESP_LOGI(TAG, "Registering hotplug slots...");
    slot_expansion_c0_init(adc_handle);
    slot_motor_d0_init();
    slot_lcd_init(i2c_bus);
    slot_camera_init(i2c_bus);

    hotplug_manager_set_event_cb(on_hotplug_event, NULL);

    ESP_LOGI(TAG, "Starting hotplug manager (poll every %dms, debounce x3)",
             HOTPLUG_POLL_INTERVAL_MS);
    ret = hotplug_manager_start(HOTPLUG_POLL_INTERVAL_MS);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "hotplug manager start failed: %s", esp_err_to_name(ret));
        return;
    }

    app_coordinator_start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HOTPLUG_HEARTBEAT_MS));
        ESP_LOGI(TAG, "heartbeat - hotplug demo running");
    }
}
