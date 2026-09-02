#include "companion_agent_adapter.h"
#include "companion_audio.h"
#include "companion_controller.h"
#include "companion_core.h"
#include "companion_doa.h"
#include "companion_input.h"
#include "companion_motion.h"
#include "companion_network.h"
#include "companion_ui.h"
#include "board_laiwfs300.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>

#if CONFIG_XIAOZHI_COMPANION_SELF_TEST
#include "companion_core_test_cases.h"
#endif

#define COMPANION_STARTUP_DELAY_MS 10000U
#define COMPANION_HEARTBEAT_MS 10000U
#define COMPANION_SW3_PRESSED_RAW_MAX 3000
#define COMPANION_SW3_RELEASED_RAW_MIN 3500
#define COMPANION_SW3_SAMPLE_MS 10U
#define COMPANION_SW3_DEBOUNCE_MS 30U
#define COMPANION_SW3_MAX_CLICK_MS 800U
#define COMPANION_TOUCH_PRESS_DEBOUNCE_MS 30U
#define COMPANION_TOUCH_DECISION_MS 150U
#define COMPANION_TOUCH_RELEASE_DEBOUNCE_MS 120U
#define COMPANION_TOUCH_TAP_FEEDBACK_MS 220U
#define COMPANION_SWIPE_INTENT_HORIZONTAL_PX 12U
#define COMPANION_SWIPE_MIN_HORIZONTAL_PX 30U
#define COMPANION_SWIPE_MAX_VERTICAL_PX 32U
#define COMPANION_SWIPE_MAX_DURATION_MS 700U

static const char *TAG = "companion_demo";

static void report_capability(companion_capability_t capability,
                              esp_err_t result)
{
    (void)companion_controller_set_capability(capability, ESP_OK == result,
                                               result);
}

static void on_ui_error(esp_err_t error, void *user_ctx)
{
    (void)user_ctx;
    (void)companion_controller_set_capability(COMPANION_CAPABILITY_UI,
                                               false, error);
}

static void on_agent_error(esp_err_t error, void *user_ctx)
{
    (void)user_ctx;
    (void)companion_controller_set_capability(COMPANION_CAPABILITY_AGENT,
                                               false, error);
}

static void on_input_health(bool available, esp_err_t error, void *user_ctx)
{
    (void)user_ctx;
    (void)companion_controller_set_capability(COMPANION_CAPABILITY_INPUT,
                                               available, error);
}

static void on_touch_health(bool available, esp_err_t error, void *user_ctx)
{
    (void)user_ctx;
    (void)companion_controller_set_capability(COMPANION_CAPABILITY_TOUCH,
                                               available, error);
}

static esp_err_t mount_spiffs(void)
{
    const esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs_data",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    const esp_err_t result = esp_vfs_spiffs_register(&config);
    ESP_LOGI(TAG, "SPIFFS mount result=%s", esp_err_to_name(result));
    return result;
}

static void start_product(void)
{
    esp_err_t result = board_laiwfs300_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "board init failed: %s; product capabilities unavailable",
                 esp_err_to_name(result));
        return;
    }

    companion_controller_config_t controller_config = {0};
    companion_controller_config_default(&controller_config);
    result = companion_controller_start(&controller_config);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "controller start failed: %s", esp_err_to_name(result));
        return;
    }

    companion_merit_tap_config_t merit_tap_config = {0};
    companion_merit_tap_config_default(&merit_tap_config);
    const companion_motion_config_t motion_config = {
        .on_done = companion_controller_on_motion_done,
        .on_progress = companion_controller_on_motion_progress,
        .on_merit_tap = companion_controller_on_merit_tap,
        .merit_tap_config = merit_tap_config,
        .user_ctx = NULL,
    };
    result = companion_motion_start(&motion_config);
    if (ESP_OK == result) {
        result = companion_motion_stop("startup safety stop");
    }
    report_capability(COMPANION_CAPABILITY_MOTION, result);

    const companion_ui_config_t ui_config = {
        .touch_press_debounce_ms = COMPANION_TOUCH_PRESS_DEBOUNCE_MS,
        .touch_decision_ms = COMPANION_TOUCH_DECISION_MS,
        .touch_release_debounce_ms = COMPANION_TOUCH_RELEASE_DEBOUNCE_MS,
        .touch_tap_feedback_ms = COMPANION_TOUCH_TAP_FEEDBACK_MS,
        .swipe_intent_horizontal_px =
            COMPANION_SWIPE_INTENT_HORIZONTAL_PX,
        .swipe_min_horizontal_px = COMPANION_SWIPE_MIN_HORIZONTAL_PX,
        .swipe_max_vertical_px = COMPANION_SWIPE_MAX_VERTICAL_PX,
        .swipe_max_duration_ms = COMPANION_SWIPE_MAX_DURATION_MS,
        .on_touch = companion_controller_on_touch,
        .on_error = on_ui_error,
        .on_touch_health = on_touch_health,
        .user_ctx = NULL,
    };
    result = companion_ui_start(&ui_config);
    report_capability(COMPANION_CAPABILITY_UI, result);

    const companion_input_config_t input_config = {
        .pressed_raw_max = COMPANION_SW3_PRESSED_RAW_MAX,
        .released_raw_min = COMPANION_SW3_RELEASED_RAW_MIN,
        .sample_ms = COMPANION_SW3_SAMPLE_MS,
        .debounce_ms = COMPANION_SW3_DEBOUNCE_MS,
        .max_click_ms = COMPANION_SW3_MAX_CLICK_MS,
        .on_click = companion_controller_on_sw3_click,
        .on_error = NULL,
        .on_health = on_input_health,
        .user_ctx = NULL,
    };
    result = companion_input_start(&input_config);
    report_capability(COMPANION_CAPABILITY_INPUT, result);
    ESP_LOGI(TAG, "SW3 raw window pressed<=%d released>=%d",
             COMPANION_SW3_PRESSED_RAW_MAX, COMPANION_SW3_RELEASED_RAW_MIN);

    (void)mount_spiffs();

    const companion_network_config_t network_config = {
        .on_state = NULL,
        .on_snapshot = companion_controller_on_network_snapshot,
        .user_ctx = NULL,
    };
    result = companion_network_start(&network_config);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "network start failed: %s", esp_err_to_name(result));
        ESP_LOGE(TAG, "network remains unavailable until controlled restart");
    }

    const char *client_id = companion_network_client_id();
    const companion_agent_adapter_config_t agent_config = {
        .ota_url = CONFIG_XIAOZHI_OTA_URL,
        .activation_url = CONFIG_XIAOZHI_ACTIVATION_URL,
        .device_mac = CONFIG_XIAOZHI_DEVICE_MAC,
        .client_id = client_id,
        .lang = CONFIG_XIAOZHI_LANG,
        .board_name = "L-AIWFS300",
        .app_version = "0.2.0",
        .on_error = on_agent_error,
        .on_event = companion_controller_on_agent_event,
        .on_audio_event = companion_controller_on_agent_audio_event,
        .user_ctx = NULL,
    };
    result = (NULL != client_id) ?
             companion_agent_adapter_start(&agent_config) :
             ESP_ERR_INVALID_STATE;
    report_capability(COMPANION_CAPABILITY_AGENT, result);

    const companion_doa_config_t doa_config = {
        .on_result = companion_controller_on_doa,
        .user_ctx = NULL,
    };
    result = companion_doa_start(&doa_config);
    report_capability(COMPANION_CAPABILITY_DOA, result);

    const companion_audio_config_t audio_config = {
        .reserve_wake = companion_controller_reserve_wake,
        .on_event = companion_controller_on_audio_event,
        .on_opus = companion_controller_on_opus,
        .user_ctx = NULL,
    };
    result = companion_audio_init(&audio_config);
    if (ESP_OK == result) {
        result = companion_audio_start();
    }
    report_capability(COMPANION_CAPABILITY_AUDIO, result);

    const esp_err_t startup_result = companion_controller_finish_startup();
    if (ESP_OK != startup_result) {
        ESP_LOGE(TAG, "startup completion delivery failed: %s",
                 esp_err_to_name(startup_result));
        const esp_err_t stop_result = companion_motion_stop(
            "startup completion failure");
        if (ESP_OK != stop_result) {
            report_capability(COMPANION_CAPABILITY_MOTION, stop_result);
        }
        (void)companion_audio_play_stop();
    }

    ESP_LOGI(TAG,
             "xiaozhi companion robot running wake_word=你好小智 mac=%s audio=16k/4slot/MM opus=16k->24k network=4G-only",
             CONFIG_XIAOZHI_DEVICE_MAC);
}

static void log_heartbeat(void)
{
    companion_controller_stats_t controller = {0};
    companion_audio_stats_t audio = {0};
    companion_controller_get_stats(&controller);
    companion_audio_get_stats(&audio);
    ESP_LOGI(TAG,
             "heartbeat state=%d generation=%lu wake_seq=%lu roam=%u net=%u net_link=%u net_ipv4=%u net_internet=%u net_lifecycle=%d net_phase=%d net_if=%d net_rev=%lu net_attempt=%lu net_err=%s gate=%u events=%lu queue_peak=%lu queue_drop=%lu stale=%lu wake_ok=%lu wake_reject=%lu upload=%lu upload_drop=%lu errors=%lu audio_feed=%lu audio_fetch=%lu encoded=%lu read_err=%lu play_drop=%lu audio_owner=%d audio_phase=%d audio_token=%lu/%lu/%lu/%lu heap_internal=%lu heap_internal_largest=%lu heap_psram=%lu min_heap=%lu",
             (int)controller.product_state,
             (unsigned long)controller.generation,
             (unsigned long)controller.wake_seq,
             controller.roam_enabled ? 1U : 0U,
             controller.network_ready ? 1U : 0U,
             controller.network_link_up ? 1U : 0U,
             controller.network_ipv4_ready ? 1U : 0U,
             controller.network_internet_reachable ? 1U : 0U,
             (int)controller.network_lifecycle,
             (int)controller.network_phase,
             (int)controller.network_interface,
             (unsigned long)controller.network_revision,
             (unsigned long)controller.network_recovery_attempt,
             esp_err_to_name(controller.network_error),
             controller.upload_gate_open ? 1U : 0U,
             (unsigned long)controller.events_processed,
             (unsigned long)controller.queue_peak,
             (unsigned long)controller.queue_drops,
             (unsigned long)controller.stale_events,
             (unsigned long)controller.wakes_accepted,
             (unsigned long)controller.wakes_rejected,
             (unsigned long)controller.upload_frames,
             (unsigned long)controller.upload_drops,
             (unsigned long)controller.module_errors,
             (unsigned long)audio.feed_blocks,
             (unsigned long)audio.fetch_blocks,
             (unsigned long)audio.encoded_frames,
             (unsigned long)audio.read_errors,
             (unsigned long)audio.playback_drops,
             (int)audio.output_owner,
             (int)audio.output_phase,
             (unsigned long)audio.output_token.generation,
             (unsigned long)audio.output_token.wake_seq,
             (unsigned long)audio.output_token.session_epoch,
             (unsigned long)audio.output_token.request_id,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)esp_get_minimum_free_heap_size());
}

void app_main(void)
{
    ESP_LOGI(TAG, "startup delay begin: %u ms for log capture",
             (unsigned int)COMPANION_STARTUP_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(COMPANION_STARTUP_DELAY_MS));
    ESP_LOGI(TAG, "startup delay complete");

    ESP_LOGI(TAG, "xiaozhi companion robot integrated build starting");
    ESP_LOGI(TAG, "reset reason=%d device_mac=%s", (int)esp_reset_reason(),
             CONFIG_XIAOZHI_DEVICE_MAC);

#if CONFIG_XIAOZHI_COMPANION_SELF_TEST
    const int failures = companion_core_run_tests();
    if (0 != failures) {
        ESP_LOGE(TAG, "companion core self-test failed: %d", failures);
        return;
    }
    ESP_LOGI(TAG, "companion core self-test passed");
#else
    ESP_LOGI(TAG, "self-test disabled in default configuration");
#endif

    start_product();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(COMPANION_HEARTBEAT_MS));
        log_heartbeat();
    }
}
