/* Network adapter layer - bridges network_manager to demo UI */
#include "demo_network.h"
#include "lte_net_state_view.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "demo_network";

#define POLL_INTERVAL_MS 500
#define NO_NETWORK_TIMEOUT_MS 60000

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static demo_net_state_cb_t s_state_cb;
static void *s_user_ctx;
static uint32_t s_subscription_id;
static TickType_t s_unavailable_since_tick;
static bool s_unavailable_timer_active;
static network_manager_mode_t s_mode;
static bool s_initialized;
static demo_net_state_t s_last_notified_state = DEMO_NET_STATE_NONE;
static demo_net_detail_t s_last_notified_detail = DEMO_NET_DETAIL_NONE;

static bool check_no_network_timeout(bool stable_ready)
{
    TickType_t now = xTaskGetTickCount();
    bool timed_out = false;

    portENTER_CRITICAL(&s_lock);
    if (stable_ready) {
        s_unavailable_timer_active = false;
    } else {
        if (!s_unavailable_timer_active) {
            s_unavailable_since_tick = now;
            s_unavailable_timer_active = true;
        }
        timed_out = (now - s_unavailable_since_tick) >=
                    pdMS_TO_TICKS(NO_NETWORK_TIMEOUT_MS);
    }
    portEXIT_CRITICAL(&s_lock);
    return timed_out;
}

static void notify_state_change(demo_net_state_t state,
                               demo_net_detail_t detail,
                               network_manager_mode_t mode)
{
    demo_net_state_cb_t cb = NULL;
    void *ctx = NULL;
    bool changed = false;

    portENTER_CRITICAL(&s_lock);
    cb = s_state_cb;
    ctx = s_user_ctx;
    changed = state != s_last_notified_state ||
              detail != s_last_notified_detail;
    if (changed) {
        s_last_notified_state = state;
        s_last_notified_detail = detail;
    }
    portEXIT_CRITICAL(&s_lock);

    if (changed) {
        ESP_LOGI(TAG, "UI network state: state=%d detail=%d mode=%d",
                 (int)state, (int)detail, (int)mode);
    }
    if (NULL != cb) {
        cb(state, detail, mode, ctx);
    }
}

static void network_event_cb(const network_manager_event_t *event,
                             void *user_ctx)
{
    (void)user_ctx;
    if (NULL == event) {
        return;
    }

    demo_net_detail_t detail = DEMO_NET_DETAIL_NONE;
    bool timeout = check_no_network_timeout(event->snapshot.stable_ready);
    demo_net_state_t state = lte_net_state_view_map(&event->snapshot,
                                                   timeout,
                                                   &detail);

    portENTER_CRITICAL(&s_lock);
    network_manager_mode_t mode = s_mode;
    portEXIT_CRITICAL(&s_lock);

    notify_state_change(state, detail, mode);
}

static void network_poll_task(void *arg)
{
    (void)arg;
    TickType_t last_log_tick = 0;

    while (true) {
        network_manager_snapshot_t snap;
        if (ESP_OK == network_manager_get_snapshot(&snap)) {
            bool timeout = check_no_network_timeout(snap.stable_ready);
            demo_net_detail_t detail = DEMO_NET_DETAIL_NONE;
            demo_net_state_t state = lte_net_state_view_map(&snap,
                                                           timeout,
                                                           &detail);

            portENTER_CRITICAL(&s_lock);
            network_manager_mode_t mode = s_mode;
            portEXIT_CRITICAL(&s_lock);

            notify_state_change(state, detail, mode);

            TickType_t now = xTaskGetTickCount();
            if ((now - last_log_tick) >= pdMS_TO_TICKS(5000)) {
                last_log_tick = now;
                const char *if_name = "NONE";
                if (NETWORK_MANAGER_INTERFACE_WIFI == snap.stable_active_interface) {
                    if_name = "WiFi";
                } else if (NETWORK_MANAGER_INTERFACE_4G == snap.stable_active_interface) {
                    if_name = "4G";
                }
                ESP_LOGI(TAG, "net: if=%s ready=%s state=%d "
                         "lc=%d wifi_ph=%d cell_ph=%d fault=%d "
                         "wifi(en=%d link=%d ip=%d) "
                         "cell(en=%d ip=%d) raw_rdy=%d sw=%d",
                         if_name,
                         snap.stable_ready ? "Y" : "N",
                         (int)state,
                         (int)snap.lifecycle,
                         (int)snap.wifi_phase,
                         (int)snap.cellular_phase,
                         (int)snap.active_fault,
                         snap.wifi.enabled,
                         snap.wifi.raw_link_up,
                         snap.wifi.raw_ipv4_ready,
                         snap.cellular.enabled,
                         snap.cellular.raw_ipv4_ready,
                         snap.raw_ready,
                         snap.interface_switch_in_progress);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static network_manager_mode_t get_configured_mode(void)
{
#if defined(CONFIG_DEMO_NETWORK_MODE_WIFI_ONLY)
    return NETWORK_MANAGER_MODE_WIFI_ONLY;
#elif defined(CONFIG_DEMO_NETWORK_MODE_4G_ONLY)
    return NETWORK_MANAGER_MODE_4G_ONLY;
#else
    return NETWORK_MANAGER_MODE_DUAL_AUTO;
#endif
}

esp_err_t demo_network_init(const demo_network_config_t *config)
{
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    if (s_initialized) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_state_cb = config->state_cb;
    s_user_ctx = config->user_ctx;
    s_mode = get_configured_mode();
    s_unavailable_timer_active = false;
    s_last_notified_state = DEMO_NET_STATE_NONE;
    s_last_notified_detail = DEMO_NET_DETAIL_NONE;
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);

    esp_err_t ret = network_manager_set_mode(s_mode);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "network_manager_set_mode failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = network_manager_subscribe(network_event_cb, NULL, &s_subscription_id);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "network_manager_subscribe failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    BaseType_t ok = xTaskCreate(network_poll_task,
                                "demo_net_poll",
                                3072,
                                NULL,
                                4,
                                NULL);
    if (pdPASS != ok) {
        ESP_LOGE(TAG, "failed to create poll task");
        return ESP_ERR_NO_MEM;
    }

    const char *mode_str = "UNKNOWN";
    if (NETWORK_MANAGER_MODE_WIFI_ONLY == s_mode) {
        mode_str = "WiFi Only";
    } else if (NETWORK_MANAGER_MODE_4G_ONLY == s_mode) {
        mode_str = "4G Only";
    } else if (NETWORK_MANAGER_MODE_DUAL_AUTO == s_mode) {
        mode_str = "Dual Auto";
    }

    ret = network_manager_start();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "network_manager_start failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_lock);
    s_unavailable_since_tick = xTaskGetTickCount();
    s_unavailable_timer_active = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "demo_network initialized: mode=%s", mode_str);
    return ESP_OK;
}

esp_err_t demo_network_set_wifi_and_start(const uint8_t *ssid,
                                          uint8_t ssid_len,
                                          const uint8_t *password,
                                          uint8_t password_len)
{
    if (NULL == ssid || NULL == password ||
        0 == ssid_len || ssid_len > NETWORK_MANAGER_WIFI_SSID_MAX_BYTES ||
        0 == password_len || password_len > NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_wifi_config_t config = {0};
    memcpy(config.ssid, ssid, ssid_len);
    memcpy(config.password, password, password_len);
    config.ssid_len = ssid_len;
    config.password_len = password_len;

    uint32_t operation_id = 0;
    esp_err_t ret = network_manager_wifi_set_config(&config, true, &operation_id);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi config applied, operation_id=%lu",
             (unsigned long)operation_id);
    return ESP_OK;
}

network_manager_mode_t demo_network_get_mode(void)
{
    network_manager_mode_t mode;
    portENTER_CRITICAL(&s_lock);
    mode = s_mode;
    portEXIT_CRITICAL(&s_lock);
    return mode;
}

bool demo_network_is_ready(void)
{
    return network_manager_is_ready();
}
