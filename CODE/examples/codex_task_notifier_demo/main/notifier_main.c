#include "notifier_model.h"
#include "notifier_protocol.h"
#include "notifier_secrets.h"
#include "notifier_ui.h"
#include "notifier_wifi_config.h"

#include "board_laiwfs300.h"

#include "esp_codec_dev.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "codex_notifier";

#define WIFI_CONNECTED_BIT       BIT0
#define BRIDGE_HOST_MAX_BYTES    96U
#define BRIDGE_TOKEN_MIN_BYTES   32U
#define BRIDGE_TOKEN_MAX_BYTES   128U
#define BRIDGE_URL_MAX_BYTES     256U
#define HTTP_TIMEOUT_MS          900
#define POLL_INTERVAL_MS         1000U
#define POLL_TASK_STACK          14336U
#define POLL_TASK_PRIORITY       3U
#define ALERT_QUEUE_LENGTH       4U
#define ALERT_TASK_STACK         3072U
#define ALERT_TASK_PRIORITY      2U
#define ALERT_CHECK_INTERVAL_MS  100U
#define AUDIO_TASK_STACK         4096U
#define AUDIO_TASK_PRIORITY      2U
#define ALERT_TONE_COUNT         3U
#define ALERT_TONE_DURATION_MS   160U
#define ALERT_TONE_GAP_MS        60U
#define ALERT_OUTPUT_VOLUME      70
#define HEALTH_INTERVAL_MS       30000U
#define MODEL_LOCK_TIMEOUT_MS    100U
#define NVS_NAMESPACE            "codex_notify"
#define NVS_EVENT_SEQ_KEY        "event_seq"
#define NVS_WIFI_SSID_KEY        "wifi_ssid"
#define NVS_WIFI_PASSWORD_KEY    "wifi_pass"
#define WIFI_CONFIG_QUEUE_LENGTH 1U
#define WIFI_CONFIG_TASK_STACK   4096U
#define WIFI_CONFIG_TASK_PRIORITY 3U
#define WIFI_SCAN_QUEUE_LENGTH   1U
#define WIFI_SCAN_TASK_STACK     4096U
#define WIFI_SCAN_TASK_PRIORITY  3U
#define WIFI_SCAN_RAW_RECORD_LIMIT 64U
#define WIFI_SCAN_ACTIVE_MIN_MS  60U
#define WIFI_SCAN_ACTIVE_MAX_MS  120U
#define WIFI_SCAN_HOME_DWELL_MS  30U
#define CONTENT_TYPE_JSON        "application/json"

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
    bool content_type_seen;
    bool content_type_valid;
} http_response_context_t;

typedef struct {
    uint32_t poll_success;
    uint32_t poll_failure;
    uint32_t http_error;
    uint32_t auth_error;
    uint32_t status_error;
    uint32_t parse_error;
    uint32_t response_overflow;
    uint32_t nvs_error;
    uint32_t alert_queued;
    uint32_t alert_played;
    uint32_t alert_skipped;
    uint32_t alert_failed;
} health_stats_t;

static notifier_model_t s_model;
static SemaphoreHandle_t s_model_mutex;
static EventGroupHandle_t s_wifi_event_group;
static QueueHandle_t s_wifi_config_queue;
static QueueHandle_t s_wifi_scan_request_queue;
static QueueHandle_t s_alert_queue;
static bool s_audio_ready;
static bool s_wifi_configured;
static bool s_wifi_reconfiguring;
static bool s_wifi_scan_busy;
static portMUX_TYPE s_wifi_state_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static health_stats_t s_stats;
static uint32_t s_wifi_disconnect_logs;
static const uint32_t s_alert_tone_hz[ALERT_TONE_COUNT] = {
    880U,
    1175U,
    1568U,
};

static void increment_counter(uint32_t *counter)
{
    if (NULL == counter) {
        return;
    }
    portENTER_CRITICAL(&s_stats_lock);
    if (UINT32_MAX != *counter) {
        (*counter)++;
    }
    portEXIT_CRITICAL(&s_stats_lock);
}

static health_stats_t copy_health_stats(void)
{
    health_stats_t stats = {0};

    portENTER_CRITICAL(&s_stats_lock);
    stats = s_stats;
    portEXIT_CRITICAL(&s_stats_lock);
    return stats;
}

static bool visible_ascii_string(const char *value, size_t minimum,
                                 size_t maximum)
{
    size_t length = 0U;

    if (NULL == value) {
        return false;
    }
    length = strlen(value);
    if (length < minimum || length > maximum) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        uint8_t byte = (uint8_t)value[index];
        if (byte < 0x21U || byte > 0x7EU) {
            return false;
        }
    }
    return true;
}

static bool configuration_is_valid(void)
{
    size_t host_length = strlen(NOTIFIER_BRIDGE_HOST);

    if (0U == host_length || host_length > BRIDGE_HOST_MAX_BYTES ||
        NOTIFIER_BRIDGE_PORT <= 0 || NOTIFIER_BRIDGE_PORT > 65535 ||
        !visible_ascii_string(NOTIFIER_BRIDGE_TOKEN,
                              BRIDGE_TOKEN_MIN_BYTES,
                              BRIDGE_TOKEN_MAX_BYTES) ||
        NULL != strstr(NOTIFIER_BRIDGE_TOKEN, "replace_me")) {
        return false;
    }
    return true;
}

static bool copy_model_for_ui(notifier_model_t *destination, void *user_context)
{
    (void)user_context;
    if (NULL == destination || NULL == s_model_mutex) {
        return false;
    }
    if (pdTRUE != xSemaphoreTake(s_model_mutex,
                                 pdMS_TO_TICKS(MODEL_LOCK_TIMEOUT_MS))) {
        return false;
    }
    *destination = s_model;
    xSemaphoreGive(s_model_mutex);
    return true;
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t result = nvs_flash_init();

    if (ESP_ERR_NVS_NO_FREE_PAGES == result ||
        ESP_ERR_NVS_NEW_VERSION_FOUND == result) {
        result = nvs_flash_erase();
        if (ESP_OK == result) {
            result = nvs_flash_init();
        }
    }
    return result;
}

static uint64_t load_event_seq(void)
{
    nvs_handle_t handle = 0;
    uint64_t event_seq = 0U;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (ESP_ERR_NVS_NOT_FOUND == result) {
        return 0U;
    }
    if (ESP_OK != result) {
        increment_counter(&s_stats.nvs_error);
        ESP_LOGW(TAG, "[event] NVS open for read failed: %s",
                 esp_err_to_name(result));
        return 0U;
    }
    result = nvs_get_u64(handle, NVS_EVENT_SEQ_KEY, &event_seq);
    nvs_close(handle);
    if (ESP_ERR_NVS_NOT_FOUND == result) {
        return 0U;
    }
    if (ESP_OK != result) {
        increment_counter(&s_stats.nvs_error);
        ESP_LOGW(TAG, "[event] NVS read failed: %s", esp_err_to_name(result));
        return 0U;
    }
    return event_seq;
}

static esp_err_t persist_event_seq(uint64_t event_seq)
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (ESP_OK != result) {
        return result;
    }
    result = nvs_set_u64(handle, NVS_EVENT_SEQ_KEY, event_seq);
    if (ESP_OK == result) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static esp_err_t load_wifi_credentials(
    notifier_wifi_credentials_t *credentials, bool *has_credentials)
{
    nvs_handle_t handle = 0;
    size_t ssid_size = 0U;
    size_t password_size = 0U;
    esp_err_t result = ESP_OK;
    esp_err_t ssid_result = ESP_OK;
    esp_err_t password_result = ESP_OK;
    notifier_wifi_config_result_t validation = NOTIFIER_WIFI_CONFIG_OK;

    if (NULL == credentials || NULL == has_credentials) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(credentials, 0, sizeof(*credentials));
    *has_credentials = false;
    result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ESP_ERR_NVS_NOT_FOUND == result) {
        ESP_LOGI(TAG, "[wifi] config source=none");
        return ESP_OK;
    }
    if (ESP_OK != result) {
        return result;
    }

    ssid_size = sizeof(credentials->ssid);
    password_size = sizeof(credentials->password);
    ssid_result = nvs_get_str(handle, NVS_WIFI_SSID_KEY,
                              credentials->ssid, &ssid_size);
    password_result = nvs_get_str(handle, NVS_WIFI_PASSWORD_KEY,
                                  credentials->password, &password_size);
    nvs_close(handle);
    if (ESP_OK != ssid_result || ESP_OK != password_result) {
        memset(credentials, 0, sizeof(*credentials));
        ESP_LOGW(TAG,
                 "[wifi] stored config incomplete ssid_err=%s pass_err=%s",
                 esp_err_to_name(ssid_result),
                 esp_err_to_name(password_result));
        return ESP_OK;
    }

    validation = notifier_wifi_config_validate(credentials->ssid,
                                                credentials->password);
    if (NOTIFIER_WIFI_CONFIG_OK != validation) {
        memset(credentials, 0, sizeof(*credentials));
        ESP_LOGW(TAG, "[wifi] stored config invalid reason=%d",
                 (int)validation);
        return ESP_OK;
    }
    *has_credentials = true;
    ESP_LOGI(TAG, "[wifi] config source=nvs ssid_bytes=%u pass_bytes=%u",
             (unsigned)strlen(credentials->ssid),
             (unsigned)strlen(credentials->password));
    return ESP_OK;
}

static esp_err_t save_wifi_credentials(
    const notifier_wifi_credentials_t *credentials)
{
    nvs_handle_t handle = 0;
    esp_err_t result = ESP_OK;

    if (NULL == credentials ||
        NOTIFIER_WIFI_CONFIG_OK != notifier_wifi_config_validate(
            credentials->ssid, credentials->password)) {
        return ESP_ERR_INVALID_ARG;
    }
    result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ESP_OK != result) {
        return result;
    }
    result = nvs_set_str(handle, NVS_WIFI_SSID_KEY, credentials->ssid);
    if (ESP_OK == result) {
        result = nvs_set_str(handle, NVS_WIFI_PASSWORD_KEY,
                             credentials->password);
    }
    if (ESP_OK == result) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static esp_err_t submit_wifi_credentials_from_ui(
    const notifier_wifi_credentials_t *credentials, void *user_context)
{
    esp_err_t result = ESP_OK;

    (void)user_context;
    if (NULL == credentials || NULL == s_wifi_config_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    result = save_wifi_credentials(credentials);
    if (ESP_OK != result) {
        increment_counter(&s_stats.nvs_error);
        ESP_LOGE(TAG, "[wifi] config save failed: %s",
                 esp_err_to_name(result));
        return result;
    }
    if (pdPASS != xQueueOverwrite(s_wifi_config_queue, credentials)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "[wifi] config saved ssid_bytes=%u pass_bytes=%u",
             (unsigned)strlen(credentials->ssid),
             (unsigned)strlen(credentials->password));
    return ESP_OK;
}

static void set_wifi_runtime_state(bool configured, bool reconfiguring)
{
    portENTER_CRITICAL(&s_wifi_state_lock);
    s_wifi_configured = configured;
    s_wifi_reconfiguring = reconfiguring;
    portEXIT_CRITICAL(&s_wifi_state_lock);
}

static bool wifi_connect_is_allowed(void)
{
    bool allowed = false;

    portENTER_CRITICAL(&s_wifi_state_lock);
    allowed = s_wifi_configured && !s_wifi_reconfiguring;
    portEXIT_CRITICAL(&s_wifi_state_lock);
    return allowed;
}

static void set_wifi_scan_busy(bool busy)
{
    portENTER_CRITICAL(&s_wifi_state_lock);
    s_wifi_scan_busy = busy;
    portEXIT_CRITICAL(&s_wifi_state_lock);
}

static esp_err_t request_wifi_scan_from_ui(void *user_context)
{
    uint8_t request = 1U;
    bool accepted = false;

    (void)user_context;
    if (NULL == s_wifi_scan_request_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_wifi_state_lock);
    if (!s_wifi_scan_busy) {
        s_wifi_scan_busy = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_wifi_state_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pdPASS != xQueueOverwrite(s_wifi_scan_request_queue, &request)) {
        set_wifi_scan_busy(false);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "[wifi] scan requested");
    return ESP_OK;
}

static esp_err_t apply_wifi_credentials(
    const notifier_wifi_credentials_t *credentials)
{
    wifi_config_t configuration = {0};
    size_t ssid_length = 0U;
    size_t password_length = 0U;

    if (NULL == credentials ||
        NOTIFIER_WIFI_CONFIG_OK != notifier_wifi_config_validate(
            credentials->ssid, credentials->password)) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid_length = strlen(credentials->ssid);
    password_length = strlen(credentials->password);
    memcpy(configuration.sta.ssid, credentials->ssid, ssid_length);
    memcpy(configuration.sta.password, credentials->password,
           password_length);
    configuration.sta.threshold.authmode =
        (0U == password_length) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    configuration.sta.pmf_cfg.capable = true;
    configuration.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &configuration);
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    if (WIFI_EVENT == event_base && WIFI_EVENT_STA_START == event_id) {
        if (wifi_connect_is_allowed()) {
            ESP_LOGI(TAG, "[wifi] connecting source=stored");
            esp_err_t result = esp_wifi_connect();

            if (ESP_OK != result) {
                ESP_LOGE(TAG, "[wifi] connect start failed: %s",
                         esp_err_to_name(result));
            }
        } else {
            ESP_LOGI(TAG, "[wifi] waiting for configuration");
        }
        return;
    }
    if (WIFI_EVENT == event_base && WIFI_EVENT_STA_DISCONNECTED == event_id) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = (NULL != event) ? event->reason : 0U;
        EventBits_t previous_bits = xEventGroupClearBits(s_wifi_event_group,
                                                         WIFI_CONNECTED_BIT);

        if (0U != (previous_bits & WIFI_CONNECTED_BIT) ||
            s_wifi_disconnect_logs < 3U) {
            ESP_LOGW(TAG, "[wifi] disconnected reason=%u",
                     (unsigned)reason);
            if (UINT32_MAX != s_wifi_disconnect_logs) {
                s_wifi_disconnect_logs++;
            }
        }
        if (wifi_connect_is_allowed()) {
            esp_err_t result = esp_wifi_connect();

            if (ESP_OK != result) {
                ESP_LOGW(TAG, "[wifi] reconnect start failed: %s",
                         esp_err_to_name(result));
            }
        }
        return;
    }
    if (IP_EVENT == event_base && IP_EVENT_STA_GOT_IP == event_id) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_wifi_disconnect_logs = 0U;
        ESP_LOGI(TAG, "[wifi] online");
    }
}

static esp_err_t initialize_wifi(
    const notifier_wifi_credentials_t *credentials, bool has_credentials)
{
    esp_err_t result = esp_netif_init();
    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();

    if (ESP_OK != result) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (ESP_OK != result) {
        return result;
    }
    if (NULL == esp_netif_create_default_wifi_sta()) {
        return ESP_ERR_NO_MEM;
    }
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    result = esp_wifi_init(&initialization);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_event_handler, NULL,
                                                  NULL);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_event_handler_instance_register(IP_EVENT,
                                                  IP_EVENT_STA_GOT_IP,
                                                  wifi_event_handler, NULL,
                                                  NULL);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ESP_OK != result) {
        return result;
    }

    set_wifi_runtime_state(has_credentials, false);
    if (has_credentials) {
        result = apply_wifi_credentials(credentials);
        if (ESP_OK != result) {
            set_wifi_runtime_state(false, false);
            return result;
        }
    }
    result = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ESP_OK != result) {
        return result;
    }
    return esp_wifi_start();
}

static void wifi_config_task(void *argument)
{
    notifier_wifi_credentials_t credentials = {0};

    (void)argument;
    while (true) {
        esp_err_t result = ESP_OK;
        esp_err_t disconnect_result = ESP_OK;

        if (pdTRUE != xQueueReceive(s_wifi_config_queue, &credentials,
                                    portMAX_DELAY)) {
            continue;
        }
        set_wifi_runtime_state(true, true);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        disconnect_result = esp_wifi_disconnect();
        if (ESP_OK != disconnect_result &&
            ESP_ERR_WIFI_NOT_CONNECT != disconnect_result) {
            ESP_LOGW(TAG, "[wifi] disconnect for reconfigure failed: %s",
                     esp_err_to_name(disconnect_result));
        }
        result = apply_wifi_credentials(&credentials);
        set_wifi_runtime_state(true, false);
        if (ESP_OK != result) {
            ESP_LOGE(TAG, "[wifi] config apply failed: %s",
                     esp_err_to_name(result));
            continue;
        }
        result = esp_wifi_connect();
        if (ESP_OK != result) {
            ESP_LOGE(TAG, "[wifi] reconnect failed: %s",
                     esp_err_to_name(result));
            continue;
        }
        ESP_LOGI(TAG, "[wifi] reconnect requested");
    }
}

static esp_err_t collect_wifi_scan_results(
    notifier_wifi_scan_list_t *networks, uint16_t *raw_count)
{
    wifi_ap_record_t *records = NULL;
    uint16_t found_count = 0U;
    uint16_t requested_count = 0U;
    uint16_t returned_count = 0U;
    esp_err_t result = ESP_OK;

    if (NULL == networks || NULL == raw_count) {
        return ESP_ERR_INVALID_ARG;
    }
    notifier_wifi_scan_list_init(networks);
    *raw_count = 0U;
    result = esp_wifi_scan_get_ap_num(&found_count);
    if (ESP_OK != result) {
        (void)esp_wifi_clear_ap_list();
        return result;
    }
    *raw_count = found_count;
    requested_count = found_count;
    if (WIFI_SCAN_RAW_RECORD_LIMIT < requested_count) {
        requested_count = WIFI_SCAN_RAW_RECORD_LIMIT;
    }
    if (0U == requested_count) {
        (void)esp_wifi_clear_ap_list();
        return ESP_OK;
    }

    records = heap_caps_calloc(requested_count, sizeof(*records),
                               MALLOC_CAP_8BIT);
    if (NULL == records) {
        (void)esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    returned_count = requested_count;
    result = esp_wifi_scan_get_ap_records(&returned_count, records);
    if (ESP_OK == result) {
        uint16_t index = 0U;

        for (index = 0U; index < returned_count; index++) {
            char ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 1U] = {0};

            memcpy(ssid, records[index].ssid,
                   NOTIFIER_WIFI_SSID_MAX_BYTES);
            (void)notifier_wifi_scan_list_add(networks, ssid,
                                               records[index].rssi);
        }
        notifier_wifi_scan_list_finalize(networks, NULL);
    } else {
        (void)esp_wifi_clear_ap_list();
    }
    heap_caps_free(records);
    return result;
}

static void wifi_scan_task(void *argument)
{
    const wifi_scan_config_t scan_configuration = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0U,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = WIFI_SCAN_ACTIVE_MIN_MS,
                .max = WIFI_SCAN_ACTIVE_MAX_MS,
            },
            .passive = 0U,
        },
        .home_chan_dwell_time = WIFI_SCAN_HOME_DWELL_MS,
    };
    uint8_t request = 0U;

    (void)argument;
    while (true) {
        notifier_wifi_scan_list_t networks = {0};
        uint16_t raw_count = 0U;
        esp_err_t result = ESP_OK;
        esp_err_t publish_result = ESP_OK;

        if (pdTRUE != xQueueReceive(s_wifi_scan_request_queue, &request,
                                    portMAX_DELAY)) {
            continue;
        }
        result = esp_wifi_scan_start(&scan_configuration, true);
        if (ESP_OK == result) {
            result = collect_wifi_scan_results(&networks, &raw_count);
        }
        set_wifi_scan_busy(false);
        publish_result = notifier_ui_publish_wifi_scan(&networks, result);
        if (ESP_OK != result) {
            ESP_LOGW(TAG, "[wifi] scan failed err=%s",
                     esp_err_to_name(result));
        } else {
            ESP_LOGI(TAG, "[wifi] scan complete raw=%u listed=%u",
                     (unsigned)raw_count, (unsigned)networks.count);
        }
        if (ESP_OK != publish_result) {
            ESP_LOGE(TAG, "[wifi] scan publish failed: %s",
                     esp_err_to_name(publish_result));
        }
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_context_t *response = NULL;

    if (NULL == event || NULL == event->user_data) {
        return ESP_OK;
    }
    response = (http_response_context_t *)event->user_data;
    if (HTTP_EVENT_ON_HEADER == event->event_id &&
        NULL != event->header_key && NULL != event->header_value &&
        0 == strcasecmp(event->header_key, "Content-Type")) {
        response->content_type_seen = true;
        response->content_type_valid =
            0 == strncasecmp(event->header_value, CONTENT_TYPE_JSON,
                             strlen(CONTENT_TYPE_JSON));
        return ESP_OK;
    }
    if (HTTP_EVENT_ON_DATA != event->event_id || event->data_len <= 0) {
        return ESP_OK;
    }
    if (response->length + (size_t)event->data_len >= response->capacity) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->buffer + response->length, event->data,
           (size_t)event->data_len);
    response->length += (size_t)event->data_len;
    response->buffer[response->length] = '\0';
    return ESP_OK;
}

static uint32_t snapshot_state_hash(const notifier_snapshot_t *snapshot)
{
    uint32_t hash = 2166136261U;

#define HASH_BYTE(value)                \
    do {                                \
        hash ^= (uint8_t)(value);       \
        hash *= 16777619U;              \
    } while (0)

    if (NULL == snapshot) {
        return 0U;
    }
    HASH_BYTE(snapshot->aggregate_state);
    HASH_BYTE(snapshot->total_count);
    HASH_BYTE(snapshot->total_count >> 8U);
    HASH_BYTE(snapshot->running_count);
    HASH_BYTE(snapshot->done_count);
    HASH_BYTE(snapshot->stop_count);
    HASH_BYTE(snapshot->overflow_count);
    HASH_BYTE(snapshot->task_count);
    for (uint8_t index = 0U; index < snapshot->task_count; ++index) {
        const notifier_task_t *task = &snapshot->tasks[index];
        for (size_t offset = 0U; '\0' != task->id[offset]; ++offset) {
            HASH_BYTE(task->id[offset]);
        }
        for (size_t offset = 0U; '\0' != task->project[offset]; ++offset) {
            HASH_BYTE(task->project[offset]);
        }
        HASH_BYTE(task->surface);
        HASH_BYTE(task->status);
    }
#undef HASH_BYTE
    return hash;
}

static bool event_should_alert(const notifier_snapshot_t *snapshot,
                               const notifier_event_t *event)
{
    return NULL != snapshot && NULL != event && event->notify &&
           NOTIFIER_EVENT_TURN_COMPLETED == event->type &&
           snapshot->generated_at_ms >= event->occurred_at_ms &&
           snapshot->generated_at_ms - event->occurred_at_ms <=
               NOTIFIER_EVENT_MAX_AGE_MS;
}

static void log_new_events(const notifier_snapshot_t *snapshot,
                           uint64_t previous_event_seq)
{
    if (snapshot->events_truncated) {
        ESP_LOGW(TAG, "[event] ring truncated after_seq=%" PRIu64,
                 previous_event_seq);
    }
    for (uint8_t index = 0U; index < snapshot->event_count; ++index) {
        const notifier_event_t *event = &snapshot->events[index];
        uint64_t age_ms = UINT64_MAX;

        if (event->seq <= previous_event_seq) {
            continue;
        }
        if (snapshot->generated_at_ms >= event->occurred_at_ms) {
            age_ms = snapshot->generated_at_ms - event->occurred_at_ms;
        }
        ESP_LOGI(TAG,
                 "[event] seq=%" PRIu64 " task=%.8s type=%s notify=%d "
                 "age_ms=%" PRIu64 " decision=%s",
                 event->seq, event->task_id,
                 (NOTIFIER_EVENT_TURN_COMPLETED == event->type)
                     ? "TURN_COMPLETED"
                     : "TURN_STOPPED",
                 event->notify ? 1 : 0, age_ms,
                 event_should_alert(snapshot, event) ? "batch" : "skip");
    }
}

static void record_poll_failure(const char *reason)
{
    uint8_t failures = 0U;
    bool was_online = false;
    bool now_online = false;

    increment_counter(&s_stats.poll_failure);
    if (pdTRUE == xSemaphoreTake(s_model_mutex,
                                 pdMS_TO_TICKS(MODEL_LOCK_TIMEOUT_MS))) {
        was_online = notifier_model_is_online(&s_model);
        notifier_model_record_poll_failure(&s_model);
        now_online = notifier_model_is_online(&s_model);
        failures = notifier_model_consecutive_failures(&s_model);
        xSemaphoreGive(s_model_mutex);
    }
    if (failures <= NOTIFIER_OFFLINE_FAILURES) {
        ESP_LOGW(TAG, "[bridge] poll failed reason=%s consecutive=%u",
                 (NULL != reason) ? reason : "unknown",
                 (unsigned)failures);
    }
    if (was_online && !now_online) {
        ESP_LOGW(TAG, "[state] bridge=OFFLINE retain_last_snapshot=1");
    }
}

static void log_health(void)
{
    health_stats_t stats = copy_health_stats();
    uint32_t revision = 0U;
    uint16_t task_count = 0U;
    uint64_t event_seq = 0U;

    if (pdTRUE != xSemaphoreTake(s_model_mutex,
                                 pdMS_TO_TICKS(MODEL_LOCK_TIMEOUT_MS))) {
        return;
    }
    revision = s_model.revision;
    task_count = s_model.snapshot.total_count;
    event_seq = s_model.last_event_seq;
    xSemaphoreGive(s_model_mutex);
    ESP_LOGI(TAG,
             "[health] heap=%lu min_heap=%lu psram=%lu poll_ok=%lu "
             "poll_fail=%lu http=%lu auth=%lu status=%lu parse=%lu "
             "overflow=%lu nvs=%lu revision=%lu tasks=%u event_seq=%" PRIu64
             " alert_queue=%lu played=%lu skipped=%lu failed=%lu lcd=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)stats.poll_success,
             (unsigned long)stats.poll_failure,
             (unsigned long)stats.http_error,
             (unsigned long)stats.auth_error,
             (unsigned long)stats.status_error,
             (unsigned long)stats.parse_error,
             (unsigned long)stats.response_overflow,
             (unsigned long)stats.nvs_error,
             (unsigned long)revision,
             (unsigned)task_count,
             event_seq,
             (unsigned long)stats.alert_queued,
             (unsigned long)stats.alert_played,
             (unsigned long)stats.alert_skipped,
             (unsigned long)stats.alert_failed,
             (unsigned long)notifier_ui_flush_error_count());
}

static void poll_task(void *argument)
{
    char *response_buffer = NULL;
    uint64_t next_health_ms = 0U;
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;
    response_buffer = heap_caps_malloc(
        NOTIFIER_PROTOCOL_MAX_RESPONSE_BYTES + 1U,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == response_buffer) {
        ESP_LOGE(TAG, "[bridge] response buffer allocation failed");
        vTaskDelete(NULL);
        return;
    }
    next_health_ms = (uint64_t)(esp_timer_get_time() / 1000) +
                     HEALTH_INTERVAL_MS;

    while (true) {
        uint64_t cursor = 0U;
        bool old_online = false;
        uint32_t old_state_hash = 0U;

        if (pdTRUE == xSemaphoreTake(s_model_mutex,
                                     pdMS_TO_TICKS(MODEL_LOCK_TIMEOUT_MS))) {
            cursor = s_model.last_event_seq;
            old_online = notifier_model_is_online(&s_model);
            if (s_model.has_snapshot) {
                old_state_hash = snapshot_state_hash(&s_model.snapshot);
            }
            xSemaphoreGive(s_model_mutex);
        }

        EventBits_t wifi_bits = xEventGroupGetBits(s_wifi_event_group);
        if (0U == (wifi_bits & WIFI_CONNECTED_BIT)) {
            record_poll_failure("wifi");
            goto cycle_complete;
        }

        char url[BRIDGE_URL_MAX_BYTES] = {0};
        int url_length = snprintf(
            url, sizeof(url),
            "http://%s:%d/api/v1/state?after_event_seq=%" PRIu64,
            NOTIFIER_BRIDGE_HOST, NOTIFIER_BRIDGE_PORT, cursor);
        if (url_length <= 0 || (size_t)url_length >= sizeof(url)) {
            increment_counter(&s_stats.http_error);
            record_poll_failure("url");
            goto cycle_complete;
        }

        http_response_context_t response = {
            .buffer = response_buffer,
            .capacity = NOTIFIER_PROTOCOL_MAX_RESPONSE_BYTES + 1U,
            .length = 0U,
            .overflow = false,
            .content_type_seen = false,
            .content_type_valid = false,
        };
        response_buffer[0] = '\0';
        const esp_http_client_config_t configuration = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = HTTP_TIMEOUT_MS,
            .event_handler = http_event_handler,
            .user_data = &response,
            .buffer_size = 1024,
            .disable_auto_redirect = true,
        };
        esp_http_client_handle_t client =
            esp_http_client_init(&configuration);
        if (NULL == client) {
            increment_counter(&s_stats.http_error);
            record_poll_failure("http_init");
            goto cycle_complete;
        }
        esp_http_client_set_header(client, "Accept", CONTENT_TYPE_JSON);
        esp_http_client_set_header(client, "X-Codex-Notifier-Token",
                                   NOTIFIER_BRIDGE_TOKEN);
        esp_err_t http_result = esp_http_client_perform(client);
        int status_code = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (response.overflow) {
            increment_counter(&s_stats.response_overflow);
            record_poll_failure("response_overflow");
            goto cycle_complete;
        }
        if (ESP_OK != http_result) {
            increment_counter(&s_stats.http_error);
            record_poll_failure("http");
            goto cycle_complete;
        }
        if (401 == status_code) {
            increment_counter(&s_stats.auth_error);
            record_poll_failure("auth");
            goto cycle_complete;
        }
        if (200 != status_code) {
            increment_counter(&s_stats.status_error);
            record_poll_failure("http_status");
            goto cycle_complete;
        }
        if (!response.content_type_seen || !response.content_type_valid) {
            increment_counter(&s_stats.status_error);
            record_poll_failure("content_type");
            goto cycle_complete;
        }

        notifier_snapshot_t parsed_snapshot = {0};
        notifier_protocol_error_t parse_error = NOTIFIER_PROTOCOL_OK;
        if (!notifier_protocol_parse(response_buffer, response.length,
                                     &parsed_snapshot, &parse_error)) {
            char reason[40] = {0};

            increment_counter(&s_stats.parse_error);
            snprintf(reason, sizeof(reason), "protocol_%s",
                     notifier_protocol_error_name(parse_error));
            record_poll_failure(reason);
            goto cycle_complete;
        }

        notifier_commit_result_t commit = {0};
        if (pdTRUE != xSemaphoreTake(s_model_mutex,
                                     pdMS_TO_TICKS(MODEL_LOCK_TIMEOUT_MS))) {
            record_poll_failure("model_lock");
            goto cycle_complete;
        }
        commit = notifier_model_commit(
            &s_model, &parsed_snapshot,
            (uint64_t)(esp_timer_get_time() / 1000));
        xSemaphoreGive(s_model_mutex);
        increment_counter(&s_stats.poll_success);

        if (!old_online) {
            ESP_LOGI(TAG, "[state] bridge=ONLINE failures_cleared=1");
        }
        if (old_state_hash != snapshot_state_hash(&parsed_snapshot)) {
            ESP_LOGI(TAG,
                     "[state] aggregate=%s total=%u run=%u done=%u stop=%u "
                     "visible=%u overflow=%u",
                     (NOTIFIER_AGGREGATE_RUNNING ==
                      parsed_snapshot.aggregate_state)
                         ? "RUNNING"
                         : "IDLE",
                     (unsigned)parsed_snapshot.total_count,
                     (unsigned)parsed_snapshot.running_count,
                     (unsigned)parsed_snapshot.done_count,
                     (unsigned)parsed_snapshot.stop_count,
                     (unsigned)parsed_snapshot.task_count,
                     (unsigned)parsed_snapshot.overflow_count);
        }
        log_new_events(&parsed_snapshot, cursor);
        if (commit.cursor_changed) {
            esp_err_t persist_result =
                persist_event_seq(commit.last_event_seq);
            if (ESP_OK != persist_result) {
                increment_counter(&s_stats.nvs_error);
                ESP_LOGW(TAG, "[event] persist seq failed: %s",
                         esp_err_to_name(persist_result));
            }
        }

cycle_complete:
        {
            uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
            if (now_ms >= next_health_ms) {
                log_health();
                next_health_ms = now_ms + HEALTH_INTERVAL_MS;
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static void alert_scheduler_task(void *argument)
{
    (void)argument;
    while (true) {
        bool alert_due = false;
        uint8_t alert_item = 1U;

        if (pdTRUE == xSemaphoreTake(s_model_mutex,
                                     pdMS_TO_TICKS(MODEL_LOCK_TIMEOUT_MS))) {
            alert_due = notifier_model_take_alert(
                &s_model, (uint64_t)(esp_timer_get_time() / 1000));
            xSemaphoreGive(s_model_mutex);
        }
        if (alert_due) {
            if (pdTRUE == xQueueSend(s_alert_queue, &alert_item, 0U)) {
                increment_counter(&s_stats.alert_queued);
                ESP_LOGI(TAG, "[alert] batch queued");
            } else {
                increment_counter(&s_stats.alert_skipped);
                ESP_LOGW(TAG, "[alert] queue full, batch skipped");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ALERT_CHECK_INTERVAL_MS));
    }
}

static esp_err_t play_alert_tones(void)
{
    for (uint8_t index = 0U; index < ALERT_TONE_COUNT; ++index) {
        esp_err_t result = board_laiwfs300_audio_play_tone(
            s_alert_tone_hz[index], ALERT_TONE_DURATION_MS);

        if (ESP_OK != result) {
            return result;
        }
        if (index + 1U < ALERT_TONE_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(ALERT_TONE_GAP_MS));
        }
    }
    return ESP_OK;
}

static void audio_task(void *argument)
{
    uint8_t alert_item = 0U;

    (void)argument;
    while (true) {
        if (pdTRUE != xQueueReceive(s_alert_queue, &alert_item,
                                    portMAX_DELAY)) {
            continue;
        }
        if (!s_audio_ready) {
            increment_counter(&s_stats.alert_skipped);
            ESP_LOGW(TAG, "[alert] audio unavailable, batch skipped");
            continue;
        }
        esp_err_t result = play_alert_tones();
        if (ESP_OK == result) {
            increment_counter(&s_stats.alert_played);
            ESP_LOGI(TAG,
                     "[alert] played pattern=ascending3 volume=%d "
                     "tone_ms=%u gap_ms=%u",
                     ALERT_OUTPUT_VOLUME, (unsigned)ALERT_TONE_DURATION_MS,
                     (unsigned)ALERT_TONE_GAP_MS);
        } else {
            increment_counter(&s_stats.alert_failed);
            ESP_LOGE(TAG, "[alert] play failed: %s",
                     esp_err_to_name(result));
        }
    }
}

static void initialize_audio(void)
{
    esp_err_t result = board_laiwfs300_audio_init();

    if (ESP_OK != result) {
        ESP_LOGW(TAG, "[alert] audio initialization failed: %s",
                 esp_err_to_name(result));
        s_audio_ready = false;
        return;
    }
    esp_codec_dev_handle_t output =
        board_laiwfs300_audio_get_output_dev();
    if (NULL != output) {
        result = esp_codec_dev_set_out_vol(output, ALERT_OUTPUT_VOLUME);
    }
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "[alert] output volume failed: %s",
                 esp_err_to_name(result));
        s_audio_ready = false;
        return;
    }
    s_audio_ready = true;
    ESP_LOGI(TAG, "[alert] audio ready volume=%d", ALERT_OUTPUT_VOLUME);
}

void app_main(void)
{
    BaseType_t task_result = pdFAIL;
    esp_err_t result = ESP_OK;
    uint64_t persisted_event_seq = 0U;
    notifier_wifi_credentials_t wifi_credentials = {0};
    bool has_wifi_credentials = false;

    ESP_LOGI(TAG, "Codex task notifier starting");
    if (!configuration_is_valid()) {
        ESP_LOGE(TAG, "local notifier_secrets.h is invalid");
        return;
    }
    result = initialize_nvs();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "NVS initialization failed: %s",
                 esp_err_to_name(result));
        return;
    }
    result = load_wifi_credentials(&wifi_credentials,
                                   &has_wifi_credentials);
    if (ESP_OK != result) {
        increment_counter(&s_stats.nvs_error);
        ESP_LOGE(TAG, "[wifi] config load failed: %s",
                 esp_err_to_name(result));
        return;
    }
    result = board_laiwfs300_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "board initialization failed: %s",
                 esp_err_to_name(result));
        return;
    }

    s_model_mutex = xSemaphoreCreateMutex();
    s_wifi_event_group = xEventGroupCreate();
    s_wifi_config_queue = xQueueCreate(WIFI_CONFIG_QUEUE_LENGTH,
                                       sizeof(notifier_wifi_credentials_t));
    s_wifi_scan_request_queue = xQueueCreate(WIFI_SCAN_QUEUE_LENGTH,
                                              sizeof(uint8_t));
    s_alert_queue = xQueueCreate(ALERT_QUEUE_LENGTH, sizeof(uint8_t));
    if (NULL == s_model_mutex || NULL == s_wifi_event_group ||
        NULL == s_wifi_config_queue || NULL == s_wifi_scan_request_queue ||
        NULL == s_alert_queue) {
        ESP_LOGE(TAG, "runtime primitive allocation failed");
        return;
    }
    persisted_event_seq = load_event_seq();
    notifier_model_init(&s_model, persisted_event_seq);
    ESP_LOGI(TAG, "[event] restored_seq=%" PRIu64, persisted_event_seq);

    result = notifier_ui_start(copy_model_for_ui, NULL,
                               has_wifi_credentials ? &wifi_credentials : NULL,
                               has_wifi_credentials,
                               submit_wifi_credentials_from_ui,
                               request_wifi_scan_from_ui, NULL);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "UI startup failed: %s", esp_err_to_name(result));
        return;
    }

    initialize_audio();
    task_result = xTaskCreate(audio_task, "notifier_audio",
                              AUDIO_TASK_STACK, NULL,
                              AUDIO_TASK_PRIORITY, NULL);
    if (pdPASS != task_result) {
        s_audio_ready = false;
        ESP_LOGW(TAG, "[alert] audio task unavailable");
    }
    task_result = xTaskCreate(alert_scheduler_task, "notifier_alert",
                              ALERT_TASK_STACK, NULL,
                              ALERT_TASK_PRIORITY, NULL);
    if (pdPASS != task_result) {
        ESP_LOGW(TAG, "[alert] scheduler task unavailable");
    }

    result = initialize_wifi(&wifi_credentials, has_wifi_credentials);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "[wifi] initialization failed: %s",
                 esp_err_to_name(result));
        return;
    }
    task_result = xTaskCreate(wifi_config_task, "notifier_wifi_cfg",
                              WIFI_CONFIG_TASK_STACK, NULL,
                              WIFI_CONFIG_TASK_PRIORITY, NULL);
    if (pdPASS != task_result) {
        ESP_LOGE(TAG, "[wifi] config task creation failed");
        return;
    }
    task_result = xTaskCreate(wifi_scan_task, "notifier_wifi_scan",
                              WIFI_SCAN_TASK_STACK, NULL,
                              WIFI_SCAN_TASK_PRIORITY, NULL);
    if (pdPASS != task_result) {
        ESP_LOGE(TAG, "[wifi] scan task creation failed");
        return;
    }
    task_result = xTaskCreatePinnedToCore(
        poll_task, "notifier_poll", POLL_TASK_STACK, NULL,
        POLL_TASK_PRIORITY, NULL, 0);
    if (pdPASS != task_result) {
        ESP_LOGE(TAG, "[bridge] poll task creation failed");
        return;
    }
    ESP_LOGI(TAG,
             "Codex task notifier ready poll_ms=%u timeout_ms=%u "
             "offline_failures=%u",
             (unsigned)POLL_INTERVAL_MS, (unsigned)HTTP_TIMEOUT_MS,
             (unsigned)NOTIFIER_OFFLINE_FAILURES);
}
