/*
 * app_main.c - 遥控坦克 Demo 入口
 *
 * 职责: 启动延迟、角色初始化、网络通道启动和Tank实载健康自愈。
 */
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "rc_tank_common.h"
#include "rc_net.h"
#include "rc_control.h"
#include "rc_video.h"
#include "rc_audio.h"
#include "rc_dvp_staged_override.h"
#include "board_laiwfs300.h"

static const char *TAG = "rc_tank";
static bool s_channels_started = false;

#if defined(CONFIG_RC_TANK_ROLE_TANK) && \
    defined(CONFIG_RC_TANK_STABLE_CAPTURE)
#define RC_TANK_DVP_HEALTH_MAGIC 0x44565049U
#define RC_TANK_DVP_HEALTH_NVS_NAMESPACE "dvp_health"
#define RC_TANK_DVP_HEALTH_SAMPLE_MS 8000U
#define RC_TANK_DVP_HEALTH_MIN_EVENTS 40U
#define RC_TANK_DVP_HEALTH_BAD_PERCENT 5U
#define RC_TANK_DVP_HEALTH_RETRY_LIMIT 5U

typedef struct {
    uint32_t magic;
    uint32_t retry_count;
} rc_tank_dvp_health_state_t;

static rc_tank_dvp_health_state_t s_dvp_health_state = {0};
static TaskHandle_t s_dvp_health_task = NULL;

static esp_err_t rc_tank_dvp_health_save(void)
{
    nvs_handle_t handle = 0U;
    esp_err_t ret = nvs_open(RC_TANK_DVP_HEALTH_NVS_NAMESPACE,
                             NVS_READWRITE, &handle);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = nvs_set_blob(handle, "state", &s_dvp_health_state,
                       sizeof(s_dvp_health_state));
    if (ESP_OK == ret) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t rc_tank_dvp_health_init(void)
{
    nvs_handle_t handle = 0U;
    esp_err_t ret = nvs_open(RC_TANK_DVP_HEALTH_NVS_NAMESPACE,
                             NVS_READONLY, &handle);
    if (ESP_OK == ret) {
        size_t state_size = sizeof(s_dvp_health_state);
        ret = nvs_get_blob(handle, "state", &s_dvp_health_state,
                           &state_size);
        nvs_close(handle);
        if ((ESP_OK == ret) &&
            (sizeof(s_dvp_health_state) != state_size)) {
            ret = ESP_ERR_NVS_INVALID_LENGTH;
        }
    }

    if ((ESP_ERR_NVS_NOT_FOUND == ret) ||
        (ESP_ERR_NVS_INVALID_LENGTH == ret) ||
        ((ESP_OK == ret) &&
         ((RC_TANK_DVP_HEALTH_MAGIC != s_dvp_health_state.magic) ||
          (RC_TANK_DVP_HEALTH_RETRY_LIMIT <
           s_dvp_health_state.retry_count)))) {
        s_dvp_health_state.magic = RC_TANK_DVP_HEALTH_MAGIC;
        s_dvp_health_state.retry_count = 0U;
        return rc_tank_dvp_health_save();
    }
    return ret;
}

static void rc_tank_dvp_health_monitor_task(void *arg)
{
    (void)arg;
    rc_dvp_staged_stats_t health_before = {0};
    rc_dvp_staged_stats_t health_after = {0};
    rc_dvp_staged_get_stats(&health_before);
    vTaskDelay(pdMS_TO_TICKS(RC_TANK_DVP_HEALTH_SAMPLE_MS));
    rc_dvp_staged_get_stats(&health_after);

    const uint32_t health_complete =
        health_after.sync_end_complete_candidates -
        health_before.sync_end_complete_candidates;
    const uint32_t health_incomplete =
        health_after.sync_end_incomplete_events -
        health_before.sync_end_incomplete_events;
    const uint32_t health_total = health_complete + health_incomplete;
    const bool health_bad =
        (RC_TANK_DVP_HEALTH_MIN_EVENTS > health_total) ||
        (((uint64_t)health_incomplete * 100ULL) >
         ((uint64_t)health_total * RC_TANK_DVP_HEALTH_BAD_PERCENT));

    ESP_LOGI(TAG,
             "[DVP-HEALTH] retry=%lu complete=%lu incomplete=%lu total=%lu verdict=%s",
             (unsigned long)s_dvp_health_state.retry_count,
             (unsigned long)health_complete,
             (unsigned long)health_incomplete,
             (unsigned long)health_total,
             health_bad ? "reject" : "accept");

    ESP_LOGI(TAG,
             "[DEBUG-DVP-LIMIT] ctrl_rx=%lu recv=%lu recovered10=%lu eof_match=%lu eof_mismatch=%lu eof_invalid=%lu near_control=0_2:%lu,2_10:%lu,gt10:%lu",
             (unsigned long)(health_after.control_rx_events -
                             health_before.control_rx_events),
             (unsigned long)(health_after.recv_data_events -
                             health_before.recv_data_events),
             (unsigned long)(health_after.sync_end_recovered_penultimate -
                             health_before.sync_end_recovered_penultimate),
             (unsigned long)(health_after.eof_desc_matches_source -
                             health_before.eof_desc_matches_source),
             (unsigned long)(health_after.eof_desc_mismatches_source -
                             health_before.eof_desc_mismatches_source),
             (unsigned long)(health_after.eof_desc_invalid -
                             health_before.eof_desc_invalid),
             (unsigned long)(health_after.incomplete_within_2ms_of_control -
                             health_before.incomplete_within_2ms_of_control),
             (unsigned long)(health_after.incomplete_within_10ms_of_control -
                             health_before.incomplete_within_10ms_of_control),
             (unsigned long)(health_after.incomplete_after_10ms_of_control -
                             health_before.incomplete_after_10ms_of_control));
    ESP_LOGI(TAG,
             "[DEBUG-DVP-LIMIT] sync_blocks 0=%lu 9=%lu 10=%lu 11=%lu 12plus=%lu",
             (unsigned long)(health_after.sync_end_received_blocks[0] - health_before.sync_end_received_blocks[0]),
             (unsigned long)(health_after.sync_end_received_blocks[9] - health_before.sync_end_received_blocks[9]),
             (unsigned long)(health_after.sync_end_received_blocks[10] - health_before.sync_end_received_blocks[10]),
             (unsigned long)(health_after.sync_end_received_blocks[11] - health_before.sync_end_received_blocks[11]),
             (unsigned long)(health_after.sync_end_received_blocks[12] - health_before.sync_end_received_blocks[12]));
    ESP_LOGI(TAG,
             "[DEBUG-DVP-LIMIT] eof_mismatch_block 7=%lu 8=%lu 9=%lu 10=%lu 11plus=%lu",
             (unsigned long)(health_after.eof_desc_mismatch_by_block[7] - health_before.eof_desc_mismatch_by_block[7]),
             (unsigned long)(health_after.eof_desc_mismatch_by_block[8] - health_before.eof_desc_mismatch_by_block[8]),
             (unsigned long)(health_after.eof_desc_mismatch_by_block[9] - health_before.eof_desc_mismatch_by_block[9]),
             (unsigned long)(health_after.eof_desc_mismatch_by_block[10] - health_before.eof_desc_mismatch_by_block[10]),
             (unsigned long)(health_after.eof_desc_mismatch_by_block[11] - health_before.eof_desc_mismatch_by_block[11]));

    if (health_bad) {
        if (RC_TANK_DVP_HEALTH_RETRY_LIMIT >
            s_dvp_health_state.retry_count) {
            s_dvp_health_state.retry_count++;
            const esp_err_t save_ret = rc_tank_dvp_health_save();
            if (ESP_OK != save_ret) {
                s_dvp_health_state.retry_count--;
                ESP_LOGE(TAG,
                         "[DVP-HEALTH] retry save failed: %s; restart stopped",
                         esp_err_to_name(save_ret));
                s_dvp_health_task = NULL;
                vTaskDelete(NULL);
                return;
            }
            ESP_LOGW(TAG, "[DVP-HEALTH] retry=%lu/%u; restarting in 1s",
                     (unsigned long)s_dvp_health_state.retry_count,
                     (unsigned)RC_TANK_DVP_HEALTH_RETRY_LIMIT);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            esp_restart();
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGE(TAG, "[DVP-HEALTH] retry limit reached; restart stopped");
        s_dvp_health_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_dvp_health_state.retry_count = 0U;
    const esp_err_t save_ret = rc_tank_dvp_health_save();
    if (ESP_OK != save_ret) {
        ESP_LOGE(TAG, "[DVP-HEALTH] accepted-state save failed: %s",
                 esp_err_to_name(save_ret));
    }
    ESP_LOGI(TAG, "[DVP-HEALTH] accepted; normal runtime continues");
    s_dvp_health_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t rc_tank_dvp_health_start_monitor(void)
{
    if (NULL != s_dvp_health_task) {
        return ESP_OK;
    }
    if (pdPASS != xTaskCreate(rc_tank_dvp_health_monitor_task,
                              "dvp_health", 4096, NULL, 2,
                              &s_dvp_health_task)) {
        s_dvp_health_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif

static void net_event_callback(bool connected, uint32_t peer_ip)
{
    rc_video_set_network_connected(connected);
    rc_control_set_network_connected(connected);
    if (!connected) {
        ESP_LOGW(TAG, "Network disconnected");
        return;
    }

    ESP_LOGI(TAG, "Network connected, peer: %d.%d.%d.%d",
             (int)(peer_ip & 0xFF), (int)((peer_ip >> 8) & 0xFF),
             (int)((peer_ip >> 16) & 0xFF), (int)((peer_ip >> 24) & 0xFF));

    if (s_channels_started) {
#if defined(CONFIG_RC_TANK_ROLE_TANK) && \
    defined(CONFIG_RC_TANK_STABLE_CAPTURE)
        const esp_err_t health_ret = rc_tank_dvp_health_start_monitor();
        if (ESP_OK != health_ret) {
            ESP_LOGE(TAG, "[DVP-HEALTH] reconnect monitor failed: %s",
                     esp_err_to_name(health_ret));
        }
#endif
        return;
    }

    esp_err_t ret = rc_net_start_channels();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Channel start failed: %s", esp_err_to_name(ret));
        return;
    }

#if defined(CONFIG_RC_TANK_ROLE_TANK)
    ret = rc_video_start_tank();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Tank video start failed: %s", esp_err_to_name(ret));
        return;
    }
#if defined(CONFIG_RC_TANK_AUDIO_ENABLED)
    ret = rc_audio_play_start();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Tank audio start failed: %s", esp_err_to_name(ret));
    }
#endif
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
    ret = rc_video_start_remote();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Remote video start failed: %s", esp_err_to_name(ret));
        return;
    }
#endif

    s_channels_started = true;
#if defined(CONFIG_RC_TANK_ROLE_TANK) && \
    defined(CONFIG_RC_TANK_STABLE_CAPTURE)
    ret = rc_tank_dvp_health_start_monitor();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "[DVP-HEALTH] monitor start failed: %s",
                 esp_err_to_name(ret));
    }
#endif
}

#if defined(CONFIG_RC_TANK_ROLE_TANK)
static esp_err_t rc_tank_role_run(void)
{
    ESP_LOGI(TAG, "Starting Tank role");
    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc_motor_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Motor init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc_video_display_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Display init failed: %s (continue)",
                 esp_err_to_name(ret));
    }

    ret = rc_video_capture_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Camera init failed: %s (use fallback source)",
                 esp_err_to_name(ret));
        rc_video_enable_synthetic(true);
    }

#if defined(CONFIG_RC_TANK_AUDIO_ENABLED)
    ret = rc_audio_play_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Audio play init failed: %s (continue)",
                 esp_err_to_name(ret));
    }
#endif

#if defined(CONFIG_RC_TANK_STABLE_CAPTURE)
    ret = rc_tank_dvp_health_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "[DVP-HEALTH] NVS init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }
#endif

    ret = rc_net_init(net_event_callback);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc_control_start_tank();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Control start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Tank ready; waiting for Remote");
    return ESP_OK;
}
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
static esp_err_t rc_remote_role_run(void)
{
    ESP_LOGI(TAG, "Starting Remote role");
    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc_joystick_init(NULL);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Joystick init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc_video_display_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Video display init failed: %s (continue)",
                 esp_err_to_name(ret));
    }

#if defined(CONFIG_RC_TANK_AUDIO_ENABLED)
    ret = rc_audio_record_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Audio record init failed: %s (continue)",
                 esp_err_to_name(ret));
    }
#endif

    ret = rc_net_init(net_event_callback);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Network init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = rc_control_start_remote();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "Control start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Remote ready; network recovery active");
#if defined(CONFIG_RC_TANK_AUDIO_ENABLED)
    while (1) {
        if (rc_audio_sw3_pressed()) {
            rc_audio_record_and_send();
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
#else
    return ESP_OK;
#endif
}
#endif

static void startup_delay(void)
{
    const int sec = CONFIG_RC_TANK_STARTUP_DELAY_SEC;
    if (0 < sec) {
        ESP_LOGI(TAG, "Starting in %d s", sec);
        vTaskDelay(pdMS_TO_TICKS((uint32_t)sec * 1000U));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if ((ESP_ERR_NVS_NO_FREE_PAGES == ret) ||
        (ESP_ERR_NVS_NEW_VERSION_FOUND == ret)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== RC Tank Demo (EX-035) Role: %s ===", RC_ROLE_NAME);
    startup_delay();

#if defined(CONFIG_RC_TANK_ROLE_TANK)
    const esp_err_t role_ret = rc_tank_role_run();
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
    const esp_err_t role_ret = rc_remote_role_run();
#endif
    if (ESP_OK != role_ret) {
        ESP_LOGE(TAG, "Role init failed (%s), restarting in 5s",
                 esp_err_to_name(role_ret));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
