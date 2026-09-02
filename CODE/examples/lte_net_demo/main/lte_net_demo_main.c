/* LTE Net Demo - Network Manager integration entry point */
#include "demo_network.h"
#include "demo_ui.h"

#include "board_laiwfs300.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "lte_net_demo";

#ifndef CONFIG_DEMO_DEFAULT_WIFI_SSID
#define CONFIG_DEMO_DEFAULT_WIFI_SSID "lierda-guest"
#endif
#ifndef CONFIG_DEMO_DEFAULT_WIFI_PASSWORD
#define CONFIG_DEMO_DEFAULT_WIFI_PASSWORD "lsd920249"
#endif

#define LEGACY_NVS_NAMESPACE    "lte_net_demo"
#define LEGACY_NVS_SSID_KEY     "wifi_ssid"
#define LEGACY_NVS_PASSWORD_KEY "wifi_pass"

static void load_initial_wifi_settings(demo_ui_wifi_settings_t *settings)
{
    if (NULL == settings) {
        return;
    }
    memset(settings, 0, sizeof(*settings));
    strncpy(settings->ssid, CONFIG_DEMO_DEFAULT_WIFI_SSID,
            sizeof(settings->ssid) - 1);
    strncpy(settings->password, CONFIG_DEMO_DEFAULT_WIFI_PASSWORD,
            sizeof(settings->password) - 1);

    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(LEGACY_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ESP_ERR_NVS_NOT_FOUND == ret) {
        ESP_LOGI(TAG, "no legacy WiFi config, using defaults");
        return;
    }
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "legacy NVS open failed: %s", esp_err_to_name(ret));
        return;
    }

    demo_ui_wifi_settings_t loaded = {0};
    size_t ssid_len = sizeof(loaded.ssid);
    size_t pw_len = sizeof(loaded.password);
    esp_err_t s_ret = nvs_get_str(nvs, LEGACY_NVS_SSID_KEY,
                                  loaded.ssid, &ssid_len);
    esp_err_t p_ret = nvs_get_str(nvs, LEGACY_NVS_PASSWORD_KEY,
                                  loaded.password, &pw_len);
    nvs_close(nvs);

    if (ESP_OK == s_ret && ESP_OK == p_ret &&
        strlen(loaded.ssid) > 0 && strlen(loaded.password) > 0) {
        *settings = loaded;
        ESP_LOGI(TAG, "loaded legacy WiFi config: ssid=%s", settings->ssid);
    }
}

static void on_wifi_confirm(const demo_ui_wifi_settings_t *settings,
                           void *user_ctx)
{
    (void)user_ctx;
    if (NULL == settings) {
        return;
    }

    uint8_t ssid_len = (uint8_t)strlen(settings->ssid);
    uint8_t pw_len = (uint8_t)strlen(settings->password);

    ESP_LOGI(TAG, "WiFi confirmed: ssid=%s, starting network...",
             settings->ssid);

    esp_err_t ret = demo_network_set_wifi_and_start(
        (const uint8_t *)settings->ssid, ssid_len,
        (const uint8_t *)settings->password, pw_len);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "network start failed: %s", esp_err_to_name(ret));
        demo_ui_update_network_state(DEMO_NET_STATE_ERROR,
                                    DEMO_NET_DETAIL_NETWORK_INIT_FAILED,
                                    demo_network_get_mode());
    } else {
        demo_ui_update_network_state(DEMO_NET_STATE_STARTING,
                                    DEMO_NET_DETAIL_NONE,
                                    demo_network_get_mode());
    }
}

static void on_network_state_change(demo_net_state_t state,
                                   demo_net_detail_t detail,
                                   network_manager_mode_t mode,
                                   void *user_ctx)
{
    (void)user_ctx;
    demo_ui_update_network_state(state, detail, mode);
}

void app_main(void)
{
    ESP_LOGI(TAG, "LTE Net Demo starting (network_manager)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == ret ||
        ESP_ERR_NVS_NEW_VERSION_FOUND == ret) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    demo_ui_wifi_settings_t initial_settings;
    load_initial_wifi_settings(&initial_settings);

    demo_ui_config_t ui_config = {
        .confirm_cb = on_wifi_confirm,
        .user_ctx = NULL,
    };
    ret = demo_ui_init(&ui_config, &initial_settings);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "UI init failed: %s", esp_err_to_name(ret));
        return;
    }

    demo_network_config_t net_config = {
        .state_cb = on_network_state_change,
        .user_ctx = NULL,
    };
    ret = demo_network_init(&net_config);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "demo_network_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = demo_ui_start_wifi_scan();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "initial WiFi scan unavailable: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "UI ready, waiting for user WiFi confirmation on screen");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
