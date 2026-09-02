#include "board_laiwfs300.h"
#include "lte_hal.h"
#include "lsd_net_mgmt.h"
#include "xiaozhi_agent.h"
#include "xiaozhi_audio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "xiaozhi_ai_demo";

#define WIFI_SSID     "lierda-guest"
#define WIFI_PASSWORD "lsd920249"
#define WIFI_MAX_RETRY 10
#define WIFI_STABLE_REPORT_MS 10000

static int s_wifi_retry_num = 0;
static char s_client_id[37];
static esp_timer_handle_t s_wifi_debounce_timer;

static const char *netif_name(lsd_net_if_t netif)
{
    if (LSD_IF_WIFI == netif) {
        return "WiFi";
    }
    if (LSD_IF_4G == netif) {
        return "4G";
    }
    return "NONE";
}

static void log_net_status(const char *reason)
{
    lsd_net_if_t current = lsd_netif_get();
    bool ready = lsd_network_is_ready();
    ESP_LOGI(TAG, "net status [%s]: if=%s ready=%s",
             (NULL != reason) ? reason : "?",
             netif_name(current),
             ready ? "YES" : "NO");
}

static void load_or_generate_client_id(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("xiaozhi", NVS_READWRITE, &handle);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "nvs_open failed, generating ephemeral client_id");
        goto gen;
    }

    size_t len = sizeof(s_client_id);
    ret = nvs_get_str(handle, "client_id", s_client_id, &len);
    if (ESP_OK == ret && len == sizeof(s_client_id)) {
        nvs_close(handle);
        ESP_LOGI(TAG, "Client-Id loaded: %s", s_client_id);
        return;
    }

gen:
    ;
    uint8_t uuid[16];
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uint8_t)((uuid[6] & 0x0FU) | 0x40U);
    uuid[8] = (uint8_t)((uuid[8] & 0x3FU) | 0x80U);

    snprintf(s_client_id, sizeof(s_client_id),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3],
             uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);

    if (ESP_OK == nvs_open("xiaozhi", NVS_READWRITE, &handle)) {
        nvs_set_str(handle, "client_id", s_client_id);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Client-Id generated: %s", s_client_id);
}

static void wifi_debounce_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "WiFi stable 10s, reporting CONNECTED to net_mgmt");
    lsd_net_send_event(NET_WIFI_EVENT_CONNECTED);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (WIFI_EVENT == event_base) {
        if (WIFI_EVENT_STA_START == event_id) {
            esp_wifi_connect();
        } else if (WIFI_EVENT_STA_DISCONNECTED == event_id) {
            if (NULL != s_wifi_debounce_timer) {
                esp_timer_stop(s_wifi_debounce_timer);
            }
            /* 不向 lsd_net_mgmt 报告 DISCONNECTED，避免触发网络切换 */
            if (s_wifi_retry_num < WIFI_MAX_RETRY) {
                esp_wifi_connect();
                s_wifi_retry_num++;
            }
        }
    } else if (IP_EVENT == event_base && IP_EVENT_STA_GOT_IP == event_id) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        if (NULL != s_wifi_debounce_timer) {
            esp_timer_stop(s_wifi_debounce_timer);
            esp_timer_start_once(s_wifi_debounce_timer,
                                 (uint64_t)WIFI_STABLE_REPORT_MS * 1000ULL);
        }
    }
}

static void on_agent_state_change(xiaozhi_agent_state_t state, void *user_ctx)
{
    (void)user_ctx;
    const char *s = "unknown";
    switch (state) {
    case XIAOZHI_STATE_CONNECTED:  s = "connected"; break;
    case XIAOZHI_STATE_LISTENING:  s = "listening"; break;
    case XIAOZHI_STATE_SPEAKING:   s = "speaking"; break;
    case XIAOZHI_STATE_IDLE:       s = "idle"; break;
    case XIAOZHI_STATE_PROCESSING: s = "processing"; break;
    case XIAOZHI_STATE_ERROR:      s = "error"; break;
    default: break;
    }
    ESP_LOGI(TAG, "agent state: %s", s);
    log_net_status(s);
}

static void on_audio_opus_recv(const uint8_t *opus_data, int len, void *user_ctx)
{
    (void)user_ctx;
    xiaozhi_agent_send_audio(opus_data, len);
}

static void on_audio_event(int event, void *user_ctx)
{
    (void)user_ctx;
    switch (event) {
    case XIAOZHI_AUDIO_EVENT_WAKE_WORD:
        ESP_LOGI(TAG, "Wake word detected");
        log_net_status("wake");
        xiaozhi_audio_prompt_play("file://spiffs_data/dingding.wav");
        xiaozhi_agent_notify_wake_word();
        break;
    case XIAOZHI_AUDIO_EVENT_VAD_END:
        ESP_LOGI(TAG, "VAD end");
        log_net_status("vad_end");
        xiaozhi_agent_notify_vad_end();
        break;
    default:
        break;
    }
}

static void on_agent_audio_play(const uint8_t *opus_data, int len, void *user_ctx)
{
    (void)user_ctx;
    xiaozhi_audio_play_opus(opus_data, len);
}

static void on_agent_audio_stop(void *user_ctx)
{
    (void)user_ctx;
    xiaozhi_audio_play_stop();
}

static void net_switch_cb(lsd_net_if_t new_if)
{
    ESP_LOGI(TAG, "network switched to: %s", netif_name(new_if));
    log_net_status("switch");
}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:    return "UNKNOWN";
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT";
    case ESP_RST_SW:         return "SW";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "WDT";
    case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_EFUSE:      return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default:                 return "UNRECOGNIZED";
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== XiaoZhi AI Demo ===");
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_name(reset_reason), (int)reset_reason);
    ESP_LOGI(TAG, "XiaoZhi Device MAC: %s", CONFIG_XIAOZHI_DEVICE_MAC);

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = lte_hal_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "lte_hal_init failed: %s", esp_err_to_name(ret));
        return;
    }
    lte_hal_power_on();

    ret = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == ret || ESP_ERR_NVS_NEW_VERSION_FOUND == ret) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    load_or_generate_client_id();

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    /* 纯 4G 验证模式：启用 4G（true），WiFi 通过下方 #if 0 禁用。
     * 原因：lsd_net_mgmt 闭源库内部 WiFi quality 检测在信号弱时每秒触发切换，
     * 严重影响 WS 连接稳定性。待换稳定 WiFi 环境后恢复双网。 */
    ESP_ERROR_CHECK(lsd_network_mgmt_init(true));
    lsd_net_register_switch_cb(net_switch_cb);

#if 0  /* WiFi 禁用期间不需要 debounce timer 和 WiFi STA 连接 */
    const esp_timer_create_args_t debounce_args = {
        .callback = wifi_debounce_timer_cb,
        .name = "wifi_deb",
    };
    esp_timer_create(&debounce_args, &s_wifi_debounce_timer);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
#endif

    ESP_LOGI(TAG, "Waiting for network...");
    while (!lsd_network_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "Network ready, starting audio and agent...");
    log_net_status("network_ready");

    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path = "/spiffs_data",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    ret = esp_vfs_spiffs_register(&spiffs_cfg);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted at /spiffs_data");
    }

    xiaozhi_audio_config_t audio_cfg = {
        .on_opus_recv = on_audio_opus_recv,
        .on_event = on_audio_event,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(xiaozhi_audio_init(&audio_cfg));
    ESP_ERROR_CHECK(xiaozhi_audio_start());

    xiaozhi_agent_config_t agent_cfg = {
        .ota_url = CONFIG_XIAOZHI_OTA_URL,
        .activation_url = CONFIG_XIAOZHI_ACTIVATION_URL,
        .device_mac = CONFIG_XIAOZHI_DEVICE_MAC,
        .client_id = s_client_id,
        .lang = CONFIG_XIAOZHI_LANG,
        .board_name = "L-AIWFS300",
        .app_version = "0.1.0",
        .on_state_change = on_agent_state_change,
        .on_audio_play = on_agent_audio_play,
        .on_audio_stop = on_agent_audio_stop,
        .allow_listening_rewake = true,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(xiaozhi_agent_init(&agent_cfg));
    ESP_ERROR_CHECK(xiaozhi_agent_start());

    ESP_LOGI(TAG, "XiaoZhi AI demo running. Say '%s' to start.", "你好小智");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
