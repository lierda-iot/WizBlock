#include "ofdm_audio.h"
#include "ofdm_fec.h"
#include "ofdm_link.h"
#include "ofdm_phy.h"
#include "ofdm_sync.h"
#include "ofdm_ui.h"

#include "board_laiwfs300.h"
#include "display_hal.h"

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define OFDM_MAIN_LOOP_DELAY_MS 10U
#define OFDM_MAIN_HEALTH_INTERVAL_US INT64_C(10000000)
#define OFDM_MAIN_AUDIO_DIAGNOSTIC_ON_BOOT 1
#define OFDM_MAIN_SERIAL_RX_BUFFER_BYTES 256U
#define OFDM_MAIN_SERIAL_TRIGGER_LOWER 's'
#define OFDM_MAIN_SERIAL_TRIGGER_UPPER 'S'
#define OFDM_MAIN_SERIAL_RX_CAL_LOWER 'r'
#define OFDM_MAIN_SERIAL_RX_CAL_UPPER 'R'
#define OFDM_MAIN_SERIAL_TX_CAL_LOWER 't'
#define OFDM_MAIN_SERIAL_TX_CAL_UPPER 'T'
#define OFDM_MAIN_SERIAL_STOP_CAL_LOWER 'x'
#define OFDM_MAIN_SERIAL_STOP_CAL_UPPER 'X'

static const char *TAG = "audio_ofdm_text_demo";

static void log_boot_error(const char *stage, esp_err_t error)
{
    if (NULL == stage) {
        return;
    }
    ESP_LOGE(TAG, "OFDM_BOOT stage=%s result=FAIL error=%s", stage,
             esp_err_to_name(error));
}

static void log_health(const ofdm_link_snapshot_t *snapshot)
{
    if (NULL == snapshot) {
        return;
    }
    ofdm_link_health_t health = {0};
    ofdm_link_take_health(&health);
    const uint32_t rx_rms_max = (uint32_t)lrintf(
        sqrtf((float)health.rx_mean_square_max));
    ESP_LOGI(TAG,
             "OFDM_HEALTH state=%s rx_drop=%lu rx_q_peak=%lu tx_underrun=%lu rx_rms_max=%lu rx_peak_max=%lu rx_clip=%lu chirp_max=%u.%03u chirp_hits=%lu sync_ok=%lu sync_fail=%lu rs_fixed=%lu crc_fail=%lu dsp_us_max=%lu heap=%u psram=%u lcd=%lu touch=%lu",
             ofdm_link_state_name(snapshot->state),
             (unsigned long)health.rx_drop,
             (unsigned long)health.rx_queue_peak,
             (unsigned long)health.tx_underrun,
             (unsigned long)rx_rms_max,
             (unsigned long)health.rx_peak_max,
             (unsigned long)health.rx_clip_samples,
             (unsigned int)(health.chirp_score_max_milli /
                            OFDM_LINK_CHIRP_SCORE_MAX_MILLI),
             (unsigned int)(health.chirp_score_max_milli %
                            OFDM_LINK_CHIRP_SCORE_MAX_MILLI),
             (unsigned long)health.chirp_hits,
             (unsigned long)health.sync_ok,
             (unsigned long)health.sync_fail,
             (unsigned long)health.rs_fixed,
             (unsigned long)health.crc_fail,
             (unsigned long)health.dsp_us_max,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                                   MALLOC_CAP_8BIT),
             (unsigned long)ofdm_ui_get_lcd_error_count(),
             (unsigned long)ofdm_ui_get_touch_error_count());
}

static bool init_serial_trigger(void)
{
    const esp_err_t result = uart_driver_install(
        UART_NUM_0, OFDM_MAIN_SERIAL_RX_BUFFER_BYTES, 0, 0, NULL, 0);
    if (ESP_OK != result && ESP_ERR_INVALID_STATE != result) {
        ESP_LOGW(TAG,
                 "OFDM_BOOT stage=serial_trigger result=DEGRADED error=%s",
                 esp_err_to_name(result));
        return false;
    }
    ESP_LOGI(TAG,
             "OFDM_BOOT stage=serial_trigger result=OK commands=s,r,t,x");
    return true;
}

static void process_serial_trigger(bool available)
{
    if (!available) {
        return;
    }

    uint8_t command = 0U;
    while (0 < uart_read_bytes(UART_NUM_0, &command, 1U, 0U)) {
        if (OFDM_MAIN_SERIAL_TRIGGER_LOWER == command ||
            OFDM_MAIN_SERIAL_TRIGGER_UPPER == command) {
            const bool queued = ofdm_link_request_send();
            ESP_LOGI(TAG, "OFDM_TX action=serial result=%s",
                     queued ? "QUEUED" : "REJECTED");
        } else if (OFDM_MAIN_SERIAL_RX_CAL_LOWER == command ||
                   OFDM_MAIN_SERIAL_RX_CAL_UPPER == command) {
            const bool queued = ofdm_link_request_rx_calibration();
            ESP_LOGI(TAG, "OFDM_CAL action=RX_START result=%s",
                     queued ? "QUEUED" : "REJECTED");
        } else if (OFDM_MAIN_SERIAL_TX_CAL_LOWER == command ||
                   OFDM_MAIN_SERIAL_TX_CAL_UPPER == command) {
            const bool queued = ofdm_link_request_tx_calibration();
            ESP_LOGI(TAG, "OFDM_CAL action=TX_START result=%s",
                     queued ? "QUEUED" : "REJECTED");
        } else if (OFDM_MAIN_SERIAL_STOP_CAL_LOWER == command ||
                   OFDM_MAIN_SERIAL_STOP_CAL_UPPER == command) {
            const bool queued = ofdm_link_request_stop_calibration();
            ESP_LOGI(TAG, "OFDM_CAL action=STOP result=%s",
                     queued ? "QUEUED" : "REJECTED");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "OFDM_BOOT stage=start rate=%u fft=%u cp=%u bins=%u..%u data=%u mic=slot0 lcd_hz=%u",
             (unsigned int)OFDM_SAMPLE_RATE_HZ,
             (unsigned int)OFDM_FFT_SIZE,
             (unsigned int)OFDM_CP_SAMPLES,
             (unsigned int)OFDM_FIRST_CARRIER_BIN,
             (unsigned int)OFDM_LAST_CARRIER_BIN,
             (unsigned int)OFDM_DATA_CARRIER_COUNT,
             (unsigned int)OFDM_UI_LCD_PIXEL_CLOCK_HZ);

    esp_err_t result = board_laiwfs300_init();
    if (ESP_OK != result) {
        log_boot_error("board", result);
        return;
    }
    ESP_LOGI(TAG, "OFDM_BOOT stage=board result=OK");

    result = board_laiwfs300_display_init_with_config(
        OFDM_UI_LCD_PIXEL_CLOCK_HZ, OFDM_UI_LCD_BUFFER_LINES);
    if (ESP_OK == result) {
        result = display_hal_set_orientation(true, false, true);
    }
    if (ESP_OK != result) {
        log_boot_error("lcd", result);
        return;
    }
    ESP_LOGI(TAG, "OFDM_BOOT stage=lcd result=OK wait_per_flush=1");

    result = board_laiwfs300_touch_init();
    const bool touch_available = ESP_OK == result;
    if (touch_available) {
        ESP_LOGI(TAG, "OFDM_BOOT stage=touch result=OK");
    } else {
        ESP_LOGW(TAG, "OFDM_BOOT stage=touch result=DEGRADED error=%s",
                 esp_err_to_name(result));
    }

    const ofdm_fec_result_t fec_result = ofdm_fec_init();
    if (OFDM_FEC_OK != fec_result) {
        ESP_LOGE(TAG, "OFDM_BOOT stage=fec result=FAIL code=%d",
                 (int)fec_result);
        return;
    }
    const ofdm_phy_result_t phy_result = ofdm_phy_init();
    if (OFDM_PHY_OK != phy_result) {
        ESP_LOGE(TAG, "OFDM_BOOT stage=phy result=FAIL code=%d",
                 (int)phy_result);
        return;
    }
    const ofdm_sync_result_t sync_result = ofdm_sync_init();
    if (OFDM_SYNC_OK != sync_result) {
        ESP_LOGE(TAG, "OFDM_BOOT stage=sync result=FAIL code=%d",
                 (int)sync_result);
        return;
    }
    ESP_LOGI(TAG, "OFDM_BOOT stage=dsp result=OK");

    result = ofdm_audio_init();
    if (ESP_OK != result) {
        log_boot_error("audio", result);
        return;
    }
    ESP_LOGI(TAG, "OFDM_BOOT stage=audio result=OK");
#if OFDM_MAIN_AUDIO_DIAGNOSTIC_ON_BOOT
    ofdm_audio_diagnostic_t audio_diagnostic = {0};
    result = ofdm_audio_run_diagnostic(&audio_diagnostic);
    if (ESP_OK != result) {
        log_boot_error("audio_diag", result);
        return;
    }
#endif

    result = ofdm_link_init();
    if (ESP_OK != result) {
        log_boot_error("link", result);
        return;
    }
    result = ofdm_ui_init(touch_available);
    if (ESP_OK != result) {
        log_boot_error("ui", result);
        return;
    }
    ESP_LOGI(TAG, "OFDM_BOOT stage=ui result=OK touch=%s",
             touch_available ? "ready" : "disabled");

    result = ofdm_link_start();
    if (ESP_OK != result) {
        log_boot_error("tasks", result);
        return;
    }
    const bool serial_trigger_available = init_serial_trigger();
    ESP_LOGI(TAG, "OFDM_BOOT stage=ready result=OK");

    ofdm_link_snapshot_t current_snapshot = {0};
    ofdm_link_snapshot_t displayed_snapshot = {0};
    bool has_displayed_snapshot = false;
    int64_t next_health_us = esp_timer_get_time() +
                             OFDM_MAIN_HEALTH_INTERVAL_US;
    while (true) {
        if (ofdm_link_get_snapshot(&current_snapshot) &&
            (!has_displayed_snapshot ||
             0 != memcmp(&current_snapshot, &displayed_snapshot,
                         sizeof(current_snapshot)))) {
            ofdm_ui_update(&current_snapshot);
            displayed_snapshot = current_snapshot;
            has_displayed_snapshot = true;
        }
        ofdm_ui_process();
        process_serial_trigger(serial_trigger_available);

        const int64_t now_us = esp_timer_get_time();
        if (has_displayed_snapshot && now_us >= next_health_us) {
            log_health(&current_snapshot);
            next_health_us = now_us + OFDM_MAIN_HEALTH_INTERVAL_US;
        }
        vTaskDelay(pdMS_TO_TICKS(OFDM_MAIN_LOOP_DELAY_MS));
    }
}
