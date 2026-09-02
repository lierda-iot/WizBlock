#include "holocubic_network.h"

#include "holocubic_network_policy.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "nvs.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "holocubic_net";

#define HOLO_HTTP_MAX_RESPONSE (HOLO_WEATHER_JSON_MAX_BYTES + 1U)
#define HOLO_HTTP_TIMEOUT_MS 10000
#define HOLO_NETWORK_POLL_MS 1000U
#define HOLO_WEATHER_NVS_NAMESPACE "holo_weather"
#define HOLO_WEATHER_NVS_KEY "snapshot"
#define HOLO_WEATHER_CACHE_MAGIC 0x484F4C4FU

typedef struct {
    uint32_t magic;
    holocubic_weather_t weather;
    int64_t fetched_unix;
} holocubic_weather_cache_t;

typedef struct {
    char *buffer;
    size_t length;
    bool overflow;
    bool content_type_valid;
} holocubic_http_context_t;

static void network_event_cb(const network_manager_event_t *event,
                             void *user_ctx)
{
    (void)user_ctx;
    if (NULL == event) {
        return;
    }
    if (NETWORK_MANAGER_LIFECYCLE_START_FAILED == event->snapshot.lifecycle) {
        ESP_LOGE(TAG, "network manager entered start-failed state");
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    holocubic_http_context_t *context = NULL;

    if (NULL == event || NULL == event->user_data) {
        return ESP_OK;
    }
    context = (holocubic_http_context_t *)event->user_data;
    if (HTTP_EVENT_ON_HEADER == event->event_id &&
        NULL != event->header_key && NULL != event->header_value &&
        0 == strcasecmp(event->header_key, "Content-Type")) {
        context->content_type_valid =
            0 == strncasecmp(event->header_value, "application/json", 16U);
    }
    if (HTTP_EVENT_ON_DATA == event->event_id && event->data_len > 0) {
        if (context->length + (size_t)event->data_len >= HOLO_HTTP_MAX_RESPONSE) {
            context->overflow = true;
            return ESP_FAIL;
        }
        memcpy(context->buffer + context->length, event->data,
               (size_t)event->data_len);
        context->length += (size_t)event->data_len;
        context->buffer[context->length] = '\0';
    }
    return ESP_OK;
}

static bool fetch_weather(holocubic_network_t *network)
{
    char response_buffer[HOLO_HTTP_MAX_RESPONSE] = {0};
    holocubic_http_context_t context = {.buffer = response_buffer};
    holocubic_weather_t parsed = {0};
    static const char *url =
        "https://api.open-meteo.com/v1/forecast?latitude=30.2741&longitude="
        "120.1551&current=temperature_2m,relative_humidity_2m,weather_code&"
        "daily=temperature_2m_max,temperature_2m_min&timezone=Asia%2FShanghai&"
        "forecast_days=1";
    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HOLO_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &context,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = NULL;
    esp_err_t result = ESP_OK;
    int status = 0;

    if (NULL == network || NULL == network->weather ||
        NULL == network->weather_mutex) {
        return false;
    }
    client = esp_http_client_init(&config);
    if (NULL == client) {
        return false;
    }
    result = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (ESP_OK != result || status < 200 || status >= 300 ||
        context.overflow || !context.content_type_valid ||
        !holocubic_weather_parse(response_buffer, context.length,
                                  (uint64_t)esp_timer_get_time() / 1000ULL,
                                  &parsed)) {
        return false;
    }
    if (pdTRUE == xSemaphoreTake(network->weather_mutex, pdMS_TO_TICKS(100))) {
        parsed.revision = network->weather->revision + 1U;
        *network->weather = parsed;
        xSemaphoreGive(network->weather_mutex);
    }
    return true;
}

static void restore_weather_cache(holocubic_network_t *network)
{
    nvs_handle_t handle = 0;
    holocubic_weather_cache_t cache = {0};
    size_t size = sizeof(cache);
    time_t now = time(NULL);

    if (NULL == network ||
        ESP_OK != nvs_open(HOLO_WEATHER_NVS_NAMESPACE, NVS_READWRITE, &handle)) {
        return;
    }
    if (ESP_OK != nvs_get_blob(handle, HOLO_WEATHER_NVS_KEY, &cache, &size) ||
        size != sizeof(cache) || cache.magic != HOLO_WEATHER_CACHE_MAGIC ||
        cache.fetched_unix <= 0 || now < cache.fetched_unix) {
        nvs_close(handle);
        return;
    }
    cache.weather.fetched_at_ms =
        (uint64_t)esp_timer_get_time() / 1000ULL -
        (uint64_t)(now - cache.fetched_unix) * 1000ULL;
    if (pdTRUE == xSemaphoreTake(network->weather_mutex, pdMS_TO_TICKS(100))) {
        *network->weather = cache.weather;
        xSemaphoreGive(network->weather_mutex);
    }
    nvs_close(handle);
}

static void save_weather_cache(const holocubic_weather_t *weather)
{
    nvs_handle_t handle = 0;
    holocubic_weather_cache_t cache = {.magic = HOLO_WEATHER_CACHE_MAGIC};

    if (NULL == weather ||
        ESP_OK != nvs_open(HOLO_WEATHER_NVS_NAMESPACE, NVS_READWRITE, &handle)) {
        return;
    }
    cache.weather = *weather;
    cache.fetched_unix = (int64_t)time(NULL);
    if (ESP_OK == nvs_set_blob(handle, HOLO_WEATHER_NVS_KEY, &cache,
                                sizeof(cache))) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static bool read_network_snapshot(
    holocubic_network_t *network,
    network_manager_snapshot_t *snapshot)
{
    if (NULL == network || NULL == snapshot ||
        ESP_OK != network_manager_get_snapshot(snapshot)) {
        return false;
    }
    const holocubic_network_observation_t observation = {
        .manager_start_failed =
            NETWORK_MANAGER_LIFECYCLE_START_FAILED == snapshot->lifecycle,
        .stable_ready = snapshot->stable_ready,
        .cellular_active =
            NETWORK_MANAGER_INTERFACE_4G == snapshot->stable_active_interface,
    };
    const holocubic_network_state_t state =
        holocubic_network_decide(&observation);
    network->manager_start_failed = HOLO_NETWORK_OFFLINE == state;
    network->ready = HOLO_NETWORK_ONLINE == state;
    return true;
}

bool holocubic_network_init(holocubic_network_t *network,
                            holocubic_weather_t *weather,
                            SemaphoreHandle_t weather_mutex)
{
    esp_err_t result = ESP_OK;

    if (NULL == network || NULL == weather || NULL == weather_mutex) {
        return false;
    }
    *network = (holocubic_network_t){
        .weather = weather,
        .weather_mutex = weather_mutex,
    };
    restore_weather_cache(network);

    result = network_manager_set_mode(NETWORK_MANAGER_MODE_4G_ONLY);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "network manager mode failed: %s", esp_err_to_name(result));
        network->manager_start_failed = true;
        return false;
    }
    result = network_manager_subscribe(network_event_cb, network,
                                       &network->subscription_id);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "network manager subscribe failed: %s",
                 esp_err_to_name(result));
        network->manager_start_failed = true;
        return false;
    }
    result = network_manager_start();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "network manager start failed: %s", esp_err_to_name(result));
        network->manager_start_failed = true;
        return false;
    }

    ESP_LOGI(TAG, "network mode=4G-only");
    (void)setenv("TZ", "CST-8", 1);
    tzset();
    return true;
}

void holocubic_network_task(void *argument)
{
    holocubic_network_t *network = (holocubic_network_t *)argument;
    holocubic_network_schedule_t schedule = {0};

    if (NULL == network) {
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        network_manager_snapshot_t snapshot = {0};
        bool online = false;
        if (pdTRUE == xSemaphoreTake(network->weather_mutex,
                                      pdMS_TO_TICKS(100))) {
            holocubic_weather_mark_stale(
                network->weather, (uint64_t)esp_timer_get_time() / 1000ULL);
            xSemaphoreGive(network->weather_mutex);
        }
        if (read_network_snapshot(network, &snapshot)) {
            online = network->ready;
            if (online && !network->sntp_started) {
                esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "pool.ntp.org");
                esp_sntp_init();
                network->sntp_started = true;
            }
        }
        if (!online && network->sntp_started) {
            esp_sntp_stop();
            network->sntp_started = false;
        }
        const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
        if (online && holocubic_network_weather_due(&schedule, now_ms)) {
            const bool success = fetch_weather(network);
            holocubic_network_weather_result(&schedule, now_ms, success);
            if (success) {
                holocubic_weather_t weather_snapshot = {0};
                if (pdTRUE == xSemaphoreTake(network->weather_mutex,
                                              pdMS_TO_TICKS(100))) {
                    weather_snapshot = *network->weather;
                    xSemaphoreGive(network->weather_mutex);
                }
                save_weather_cache(&weather_snapshot);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(HOLO_NETWORK_POLL_MS));
    }
}
