/*
 * ESP-IDF facade for the network manager.
 *
 * ESP event callbacks capture raw facts only. A single network worker owns the
 * runtime models, policy effects, configuration transactions, and committed
 * snapshot. A separate dispatcher invokes user callbacks so callback latency
 * cannot block recovery progress.
 */
#include "network_manager.h"

#include "network_manager_cellular_runtime_model.h"
#include "network_manager_dual_runtime_model.h"
#include "network_manager_event_journal.h"
#include "network_manager_policy.h"
#include "network_manager_storage_nvs.h"
#include "network_manager_wifi_config.h"
#include "network_manager_wifi_scan.h"
#include "network_manager_wifi_runtime_model.h"

#include "esp_event.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#if CONFIG_NETWORK_MANAGER_TASK_STACK_IN_PSRAM
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#endif
#include "lsd_net_mgmt.h"
#include "lte_hal.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stddef.h>
#include <string.h>

#define NETWORK_MANAGER_COMMAND_QUEUE_LENGTH 8U
#define NETWORK_MANAGER_WORKER_STACK         6144U
#define NETWORK_MANAGER_WORKER_PRIORITY      3U
#define NETWORK_MANAGER_DISPATCHER_STACK     4096U
#define NETWORK_MANAGER_DISPATCHER_PRIORITY  2U
#define NETWORK_MANAGER_TICK_MS              100U
#define NETWORK_MANAGER_WIFI_SCAN_RAW_LIMIT   40U

/*
 * Task stack placement. When CONFIG_NETWORK_MANAGER_TASK_STACK_IN_PSRAM is set,
 * worker/dispatcher stacks (~10KB) move from internal DRAM to external PSRAM to
 * relieve internal memory pressure on consumers that also host AFE/Opus/TLS.
 * WithCaps-created tasks MUST be released with vTaskDeleteWithCaps() (ESP-IDF
 * v5.5 contract); the self-delete path relies on the runtime's temporary
 * cleanup task to reclaim the PSRAM stack/TCB. Default keeps the original
 * xTaskCreate/vTaskDelete behaviour so existing consumers are unaffected.
 */
#if CONFIG_NETWORK_MANAGER_TASK_STACK_IN_PSRAM
#define NETWORK_MANAGER_TASK_CREATE(fn, name, stack, param, prio, handle) \
    xTaskCreatePinnedToCoreWithCaps((fn), (name), (stack), (param), (prio), \
                                    (handle), tskNO_AFFINITY, MALLOC_CAP_SPIRAM)
#define NETWORK_MANAGER_TASK_DELETE(handle) vTaskDeleteWithCaps((handle))
#else
#define NETWORK_MANAGER_TASK_CREATE(fn, name, stack, param, prio, handle) \
    xTaskCreate((fn), (name), (stack), (param), (prio), (handle))
#define NETWORK_MANAGER_TASK_DELETE(handle) vTaskDelete((handle))
#endif

static const char *TAG = "network_manager";

typedef enum {
    NETWORK_MANAGER_COMMAND_SET_CONFIG = 1,
    NETWORK_MANAGER_COMMAND_CLEAR_CONFIG = 2,
    NETWORK_MANAGER_COMMAND_RECONNECT = 3,
    NETWORK_MANAGER_COMMAND_DISCONNECT = 4,
    NETWORK_MANAGER_COMMAND_WIFI_SCAN = 5,
} network_manager_command_type_t;

typedef struct {
    network_manager_command_type_t type;
    uint32_t operation_id;
    bool persist;
    network_manager_wifi_config_t config;
} network_manager_command_t;

typedef struct {
    bool used;
    uint32_t id;
    network_manager_event_cb_t callback;
    void *user_ctx;
} network_manager_subscription_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static network_manager_mode_t s_mode;
static QueueHandle_t s_command_queue;
static TaskHandle_t s_worker_task;
static TaskHandle_t s_dispatcher_task;
static TaskHandle_t s_start_wait_task;
static bool s_initial_attempt_complete;
static esp_err_t s_initial_attempt_result = ESP_ERR_INVALID_STATE;
static nvs_handle_t s_nvs_handle;
static bool s_persistence_enabled;
static bool s_wifi_driver_started;
static bool s_raw_wifi_link;
static bool s_raw_wifi_ipv4;
static bool s_wifi_disconnect_pending;
static bool s_raw_cellular_link;
static bool s_raw_cellular_ipv4;
static bool s_raw_internet_reachable;
static int32_t s_wifi_raw_reason;
static network_manager_interface_t s_raw_active_interface =
    NETWORK_MANAGER_INTERFACE_NONE;
static uint32_t s_reconnect_operation;
static uint32_t s_disconnect_operation;
static uint32_t s_wifi_scan_operation;
static bool s_wifi_scan_request_pending;
static bool s_wifi_scan_driver_done;
static esp_err_t s_wifi_scan_driver_result;
static uint32_t s_operation_counter;
static uint32_t s_subscription_counter;
static network_manager_wifi_config_t s_current_wifi_config;
static network_manager_wifi_config_t s_persisted_wifi_config;
static network_manager_wifi_scan_list_t s_wifi_scan_list;
static wifi_ap_record_t
    s_wifi_scan_records[NETWORK_MANAGER_WIFI_SCAN_RAW_LIMIT];
static network_manager_wifi_runtime_model_t s_wifi_model;
static network_manager_cellular_runtime_model_t s_cellular_model;
static network_manager_policy_state_t s_policy;
static network_manager_event_journal_t s_journal;
static network_manager_fault_history_t s_fault_history;
static network_manager_snapshot_t s_snapshot;
static network_manager_subscription_t s_subscriptions[
    NETWORK_MANAGER_MAX_SUBSCRIBERS];

static void run_initial_attempt(void);

static network_manager_mode_t default_mode(void)
{
#if defined(CONFIG_NETWORK_MANAGER_DEFAULT_WIFI_ONLY)
    return NETWORK_MANAGER_MODE_WIFI_ONLY;
#elif defined(CONFIG_NETWORK_MANAGER_DEFAULT_DUAL_AUTO)
    return NETWORK_MANAGER_MODE_DUAL_AUTO;
#else
    return NETWORK_MANAGER_MODE_4G_ONLY;
#endif
}

static bool mode_is_valid(network_manager_mode_t mode)
{
    return NETWORK_MANAGER_MODE_WIFI_ONLY == mode ||
           NETWORK_MANAGER_MODE_4G_ONLY == mode ||
           NETWORK_MANAGER_MODE_DUAL_AUTO == mode;
}

static uint32_t next_nonzero(uint32_t value)
{
    return UINT32_MAX == value ? 1U : value + 1U;
}

static bool mode_uses_wifi(network_manager_mode_t mode)
{
    return NETWORK_MANAGER_MODE_4G_ONLY != mode;
}

static bool mode_uses_cellular(network_manager_mode_t mode)
{
    return NETWORK_MANAGER_MODE_WIFI_ONLY != mode;
}

static void ensure_initialized_locked(void)
{
    /* Caller holds s_lock; this creates only in-memory, pre-start state. */
    if (s_initialized) {
        return;
    }
    s_mode = default_mode();
    s_snapshot = (network_manager_snapshot_t){
        .lifecycle = NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED,
        .mode = s_mode,
        .wifi_phase = NETWORK_MANAGER_WIFI_DISABLED,
        .cellular_phase = NETWORK_MANAGER_4G_DISABLED,
        .revision = 1U,
    };
    network_manager_event_journal_init(&s_journal);
    network_manager_fault_history_init(&s_fault_history);
    network_manager_wifi_runtime_model_init(&s_wifi_model);
    network_manager_cellular_runtime_model_init(&s_cellular_model);
    network_manager_policy_init(&s_policy, s_mode);
    s_operation_counter = 0U;
    s_subscription_counter = 0U;
    s_initialized = true;
}

static void notify_dispatcher(void)
{
    if (NULL != s_dispatcher_task) {
        xTaskNotifyGive(s_dispatcher_task);
    }
}

static bool event_is_critical(network_manager_event_type_t type,
                              network_manager_operation_status_t status)
{
    return NETWORK_MANAGER_EVENT_FAULT == type ||
           NETWORK_MANAGER_EVENT_INTERFACE_RETRY_EXHAUSTED == type ||
           NETWORK_MANAGER_EVENT_ALL_RETRY_EXHAUSTED == type ||
           (NETWORK_MANAGER_EVENT_START_RESULT == type &&
            NETWORK_MANAGER_OPERATION_FAILED == status) ||
           (NETWORK_MANAGER_EVENT_WIFI_CONFIG_RESULT == type &&
            NETWORK_MANAGER_OPERATION_FAILED == status);
}

static void publish_event_locked(network_manager_event_type_t type,
                                 uint32_t operation_id,
                                 network_manager_operation_status_t status,
                                 network_manager_interface_t interface,
                                 network_manager_wifi_config_action_t action,
                                 bool current_applied,
                                 bool persisted_saved)
{
    /* Capture the already-committed snapshot in the same critical section. */
    network_manager_event_t event = {
        .operation_id = operation_id,
        .type = type,
        .operation_status = status,
        .interface = interface,
        .wifi_config_action = action,
        .current_config_applied = current_applied,
        .persisted_config_saved = persisted_saved,
        .snapshot = s_snapshot,
    };
    event.persistence_requested =
        NETWORK_MANAGER_WIFI_CONFIG_SAVE == action;
    (void)network_manager_event_journal_publish(
        &s_journal, &event, event_is_critical(type, status));
    s_snapshot.event_sequence = s_journal.last_sequence;
    s_snapshot.event_overflow_count = s_journal.overflow_count;
    s_snapshot.fault_overwrite_count = s_fault_history.overwritten_count;
    notify_dispatcher();
}

static void record_fault_locked(network_manager_interface_t interface,
                                 network_manager_fault_code_t code,
                                 esp_err_t source_error,
                                 int32_t raw_reason)
{
    /* Fault history is authoritative even if event delivery later overflows. */
    network_manager_fault_record_t record;
    if (!network_manager_fault_history_record(&s_fault_history,
                                              interface,
                                              code,
                                              source_error,
                                              raw_reason,
                                              &record)) {
        return;
    }
    s_snapshot.last_fault = record;
    s_snapshot.active_fault = code;
    s_snapshot.fault_overwrite_count = s_fault_history.overwritten_count;
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    network_manager_event_t event = {
        .type = NETWORK_MANAGER_EVENT_FAULT,
        .interface = interface,
        .fault = record,
        .snapshot = s_snapshot,
    };
    (void)network_manager_event_journal_publish(&s_journal, &event, true);
    s_snapshot.event_sequence = s_journal.last_sequence;
    s_snapshot.event_overflow_count = s_journal.overflow_count;
    notify_dispatcher();
}

static void mark_fault_recovered_locked(
    network_manager_interface_t interface,
    network_manager_fault_code_t code)
{
    /* Recovery closes the active record but deliberately preserves history. */
    network_manager_fault_record_t record;
    if (!network_manager_fault_history_mark_inactive(
            &s_fault_history, interface, code, &record)) {
        return;
    }
    s_snapshot.last_fault = record;
    s_snapshot.active_fault =
        network_manager_fault_history_latest_active(&s_fault_history);
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    network_manager_event_t event = {
        .type = NETWORK_MANAGER_EVENT_RECOVERY_SUCCEEDED,
        .interface = interface,
        .fault = record,
        .snapshot = s_snapshot,
    };
    (void)network_manager_event_journal_publish(&s_journal, &event, false);
    s_snapshot.event_sequence = s_journal.last_sequence;
    s_snapshot.event_overflow_count = s_journal.overflow_count;
    notify_dispatcher();
}

static uint32_t allocate_operation_id_locked(void)
{
    s_operation_counter = next_nonzero(s_operation_counter);
    return s_operation_counter;
}

static uint32_t allocate_subscription_id_locked(void)
{
    s_subscription_counter = next_nonzero(s_subscription_counter);
    return s_subscription_counter;
}

static network_manager_interface_t map_net_interface(lsd_net_if_t interface)
{
    if (LSD_IF_WIFI == interface) {
        return NETWORK_MANAGER_INTERFACE_WIFI;
    }
    if (LSD_IF_4G == interface) {
        return NETWORK_MANAGER_INTERFACE_4G;
    }
    return NETWORK_MANAGER_INTERFACE_NONE;
}

static network_manager_fault_code_t map_wifi_disconnect_reason(
    int32_t reason)
{
    if (WIFI_REASON_AUTH_EXPIRE == reason ||
        WIFI_REASON_AUTH_FAIL == reason ||
        WIFI_REASON_802_1X_AUTH_FAILED == reason) {
        return NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED;
    }
    if (WIFI_REASON_NO_AP_FOUND == reason ||
        WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY == reason ||
        WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD == reason ||
        WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD == reason) {
        return NETWORK_MANAGER_FAULT_WIFI_AP_NOT_FOUND;
    }
    if (WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT == reason ||
        WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT == reason ||
        WIFI_REASON_IE_IN_4WAY_DIFFERS == reason ||
        WIFI_REASON_HANDSHAKE_TIMEOUT == reason) {
        return NETWORK_MANAGER_FAULT_WIFI_HANDSHAKE_TIMEOUT;
    }
    if (WIFI_REASON_BEACON_TIMEOUT == reason) {
        return NETWORK_MANAGER_FAULT_WIFI_BEACON_TIMEOUT;
    }
    return NETWORK_MANAGER_FAULT_WIFI_DISCONNECTED;
}

static void on_net_interface_switch(lsd_net_if_t interface)
{
    portENTER_CRITICAL(&s_lock);
    s_raw_active_interface = map_net_interface(interface);
    s_raw_internet_reachable = LSD_NET_NONE != interface;
    portEXIT_CRITICAL(&s_lock);
    if (NULL != s_worker_task) {
        xTaskNotifyGive(s_worker_task);
    }
}

esp_err_t __real_lsd_net_send_event(net_event_type_t type);

esp_err_t __wrap_lsd_net_send_event(net_event_type_t type)
{
    const esp_err_t result = __real_lsd_net_send_event(type);

    if (NET_4G_EVENT_CONNECTED != type &&
        NET_4G_EVENT_DISCONNECTED != type) {
        return result;
    }

    portENTER_CRITICAL(&s_lock);
    const bool connected = NET_4G_EVENT_CONNECTED == type;
    s_raw_cellular_link = connected;
    s_raw_cellular_ipv4 = connected;
    if (!connected && NETWORK_MANAGER_INTERFACE_4G ==
                          s_raw_active_interface) {
        s_raw_internet_reachable = false;
    }
    portEXIT_CRITICAL(&s_lock);

    if (NULL != s_worker_task) {
        xTaskNotifyGive(s_worker_task);
    }
    return result;
}

static void network_event_handler(void *arg,
                                  esp_event_base_t event_base,
                                  int32_t event_id,
                                  void *event_data)
{
    /* Keep the ESP event-loop callback short: record facts and wake the owner. */
    (void)arg;

    (void)event_data;
    portENTER_CRITICAL(&s_lock);
    if (WIFI_EVENT == event_base) {
        if (WIFI_EVENT_STA_CONNECTED == event_id) {
            s_raw_wifi_link = true;
        } else if (WIFI_EVENT_STA_DISCONNECTED == event_id) {
            s_raw_wifi_link = false;
            s_raw_wifi_ipv4 = false;
            s_wifi_disconnect_pending = true;
            if (NULL != event_data) {
                const wifi_event_sta_disconnected_t *disconnected =
                    (const wifi_event_sta_disconnected_t *)event_data;
                s_wifi_raw_reason = (int32_t)disconnected->reason;
            }
        } else if (WIFI_EVENT_SCAN_DONE == event_id) {
            const wifi_event_sta_scan_done_t *scan_done =
                (const wifi_event_sta_scan_done_t *)event_data;
            s_wifi_scan_driver_result =
                (NULL != scan_done && 0U != scan_done->status) ?
                ESP_FAIL : ESP_OK;
            s_wifi_scan_driver_done = true;
        }
    } else if (IP_EVENT == event_base) {
        if (IP_EVENT_STA_GOT_IP == event_id) {
            s_raw_wifi_link = true;
            s_raw_wifi_ipv4 = true;
        } else if (IP_EVENT_STA_LOST_IP == event_id) {
            s_raw_wifi_ipv4 = false;
        } else if (IP_EVENT_ETH_GOT_IP == event_id) {
            s_raw_cellular_link = true;
            s_raw_cellular_ipv4 = true;
        } else if (IP_EVENT_ETH_LOST_IP == event_id) {
            s_raw_cellular_ipv4 = false;
            if (NETWORK_MANAGER_INTERFACE_4G == s_raw_active_interface) {
                s_raw_internet_reachable = false;
            }
        }
    } else if (ETH_EVENT == event_base) {
        if (ETHERNET_EVENT_CONNECTED == event_id) {
            s_raw_cellular_link = true;
        } else if (ETHERNET_EVENT_DISCONNECTED == event_id) {
            s_raw_cellular_link = false;
            s_raw_cellular_ipv4 = false;
            if (NETWORK_MANAGER_INTERFACE_4G == s_raw_active_interface) {
                s_raw_internet_reachable = false;
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (NULL != s_worker_task) {
        xTaskNotifyGive(s_worker_task);
    }
}

static void complete_wifi_scan(void)
{
    network_manager_wifi_scan_list_t next = {0};
    esp_err_t result = ESP_OK;
    uint16_t raw_count = 0U;
    uint16_t returned_count = NETWORK_MANAGER_WIFI_SCAN_RAW_LIMIT;

    portENTER_CRITICAL(&s_lock);
    result = s_wifi_scan_driver_result;
    portEXIT_CRITICAL(&s_lock);
    if (ESP_OK == result) {
        result = esp_wifi_scan_get_ap_num(&raw_count);
    }
    if (ESP_OK == result && 0U != raw_count) {
        returned_count = (raw_count < returned_count) ?
                         raw_count : returned_count;
        result = esp_wifi_scan_get_ap_records(&returned_count,
                                              s_wifi_scan_records);
    } else {
        returned_count = 0U;
    }
    if (ESP_OK == result) {
        for (uint16_t index = 0U; index < returned_count; ++index) {
            size_t ssid_len = 0U;
            while (ssid_len < NETWORK_MANAGER_WIFI_SSID_MAX_BYTES &&
                   '\0' != s_wifi_scan_records[index].ssid[ssid_len]) {
                ++ssid_len;
            }
            (void)network_manager_wifi_scan_list_add(
                &next, s_wifi_scan_records[index].ssid, ssid_len,
                s_wifi_scan_records[index].rssi,
                WIFI_AUTH_OPEN != s_wifi_scan_records[index].authmode);
        }
        network_manager_wifi_scan_list_finalize(&next);
    }
    (void)esp_wifi_clear_ap_list();

    portENTER_CRITICAL(&s_lock);
    next.raw_count = raw_count;
    next.operation_id = s_wifi_scan_operation;
    next.result = result;
    next.revision = next_nonzero(s_snapshot.wifi_scan_revision);
    s_wifi_scan_list = next;
    s_snapshot.wifi_scan_revision = next.revision;
    s_snapshot.wifi_scan_in_progress = false;
    s_wifi_scan_operation = 0U;
    s_wifi_scan_driver_done = false;
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    publish_event_locked(NETWORK_MANAGER_EVENT_WIFI_SCAN_RESULT,
                         next.operation_id,
                         ESP_OK == result ? NETWORK_MANAGER_OPERATION_COMPLETED :
                                            NETWORK_MANAGER_OPERATION_FAILED,
                         NETWORK_MANAGER_INTERFACE_WIFI,
                         NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                         false,
                         false);
    portEXIT_CRITICAL(&s_lock);
}

static void process_wifi_scan_state(void)
{
    bool request_pending = false;
    bool driver_done = false;

    portENTER_CRITICAL(&s_lock);
    request_pending = s_wifi_scan_request_pending;
    if (request_pending) {
        s_wifi_scan_request_pending = false;
    }
    driver_done = s_wifi_scan_driver_done;
    portEXIT_CRITICAL(&s_lock);

    if (request_pending) {
        const wifi_scan_config_t scan_config = {
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };
        const esp_err_t result = esp_wifi_scan_start(&scan_config, false);
        if (ESP_OK != result) {
            portENTER_CRITICAL(&s_lock);
            s_wifi_scan_driver_result = result;
            s_wifi_scan_driver_done = true;
            portEXIT_CRITICAL(&s_lock);
            driver_done = true;
        }
    }
    if (driver_done) {
        complete_wifi_scan();
    }
}

static esp_err_t apply_wifi_config(const network_manager_wifi_config_t *config)
{
    /* The public contract is length-delimited; zero-fill unused driver bytes. */
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password,
           config->password,
           sizeof(wifi_config.sta.password));
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

static network_manager_wifi_phase_t current_wifi_phase_locked(void)
{
    if (!mode_uses_wifi(s_mode)) {
        return NETWORK_MANAGER_WIFI_DISABLED;
    }
    if (s_snapshot.manual_offline) {
        return NETWORK_MANAGER_WIFI_IDLE;
    }
    if (!s_snapshot.current_wifi_config_present) {
        return NETWORK_MANAGER_WIFI_CONFIG_MISSING;
    }
    if (s_wifi_model.retry_exhausted) {
        return NETWORK_MANAGER_WIFI_EXHAUSTED;
    }
    if (s_wifi_model.stable_ready) {
        return NETWORK_MANAGER_WIFI_READY;
    }
    if (s_wifi_model.retry_deadline_active) {
        return NETWORK_MANAGER_WIFI_BACKOFF;
    }
    if (s_wifi_model.raw_link_up && s_wifi_model.raw_ipv4_ready) {
        return NETWORK_MANAGER_WIFI_WAIT_STABLE;
    }
    return NETWORK_MANAGER_WIFI_CONNECTING;
}

static network_manager_4g_phase_t current_cellular_phase_locked(void)
{
    if (!mode_uses_cellular(s_mode) || s_snapshot.manual_offline) {
        return NETWORK_MANAGER_4G_DISABLED;
    }
    return s_cellular_model.phase;
}

static void set_interface_snapshot_locked(void)
{
    s_snapshot.wifi_phase = current_wifi_phase_locked();
    s_snapshot.cellular_phase = current_cellular_phase_locked();
    s_snapshot.wifi.enabled = mode_uses_wifi(s_mode);
    s_snapshot.wifi.raw_link_up = s_raw_wifi_link;
    s_snapshot.wifi.raw_ipv4_ready = s_raw_wifi_ipv4;
    s_snapshot.wifi.stable_link_up = s_wifi_model.stable_link_up;
    s_snapshot.wifi.stable_ipv4_ready = s_wifi_model.stable_ipv4_ready;
    s_snapshot.wifi.retry_attempt = s_wifi_model.retry_attempt;
    s_snapshot.wifi.retry_limit = NETWORK_MANAGER_WIFI_RETRY_LIMIT;
    s_snapshot.wifi.retry_exhausted = s_wifi_model.retry_exhausted;
    s_snapshot.wifi.last_raw_reason = s_wifi_raw_reason;

    s_snapshot.cellular.enabled = mode_uses_cellular(s_mode);
    s_snapshot.cellular.raw_link_up = s_raw_cellular_link;
    s_snapshot.cellular.raw_ipv4_ready = s_raw_cellular_ipv4;
    s_snapshot.cellular.stable_link_up = s_raw_cellular_link;
    s_snapshot.cellular.stable_ipv4_ready = s_raw_cellular_ipv4;
    s_snapshot.cellular.retry_attempt = 0U;
    s_snapshot.cellular.retry_limit = 0U;
    s_snapshot.cellular.retry_exhausted = false;
    s_snapshot.cellular.last_raw_reason = 0;
}

static void recompute_snapshot(uint32_t now_ms)
{
    /*
     * Copy callback-owned raw facts under the lock, then advance pure models
     * and execute any requested hardware effects in worker context.
     */
    bool raw_wifi_link;
    bool raw_wifi_ipv4;
    bool raw_cellular_link;
    bool raw_cellular_ipv4;
    bool internet;
    bool wifi_disconnect_pending;
    network_manager_interface_t raw_active;
    network_manager_mode_t mode;
    bool manual_offline;
    network_manager_interface_t previous_stable;
    portENTER_CRITICAL(&s_lock);
    raw_wifi_link = s_raw_wifi_link;
    raw_wifi_ipv4 = s_raw_wifi_ipv4;
    raw_cellular_link = s_raw_cellular_link;
    raw_cellular_ipv4 = s_raw_cellular_ipv4;
    internet = s_raw_internet_reachable;
    wifi_disconnect_pending = s_wifi_disconnect_pending;
    s_wifi_disconnect_pending = false;
    raw_active = s_raw_active_interface;
    mode = s_mode;
    manual_offline = s_snapshot.manual_offline;
    previous_stable = s_snapshot.stable_active_interface;
    portEXIT_CRITICAL(&s_lock);

    network_manager_wifi_runtime_output_t wifi_update_output;
    network_manager_wifi_runtime_output_t wifi_tick_output;
    network_manager_wifi_runtime_output_t wifi_retry_output = {0};
    network_manager_cellular_runtime_output_t cellular_update_output;
    network_manager_cellular_runtime_output_t cellular_tick_output;
    network_manager_wifi_runtime_model_update_raw(&s_wifi_model,
                                                  raw_wifi_link,
                                                  raw_wifi_ipv4,
                                                  internet,
                                                  now_ms,
                                                  &wifi_update_output);
    if (wifi_disconnect_pending && !manual_offline &&
        mode_uses_wifi(mode)) {
        network_manager_wifi_runtime_model_on_connect_failed(
            &s_wifi_model, now_ms, &wifi_retry_output);
    }
    network_manager_wifi_runtime_model_tick(&s_wifi_model,
                                            now_ms,
                                            &wifi_tick_output);
    network_manager_cellular_runtime_model_update_raw(&s_cellular_model,
                                                       raw_cellular_link,
                                                       raw_cellular_ipv4,
                                                       internet,
                                                       now_ms,
                                                       &cellular_update_output);
    network_manager_cellular_runtime_model_tick(&s_cellular_model,
                                                now_ms,
                                                &cellular_tick_output);

    bool wifi_retry_exhausted_changed =
        wifi_retry_output.retry_exhausted_changed ||
        wifi_tick_output.retry_exhausted_changed;
    const bool cellular_initial_ipv4_timeout =
        cellular_tick_output.initial_ipv4_timeout;

    /* Automatic effects are suppressed immediately while manually offline. */
    if (!manual_offline && wifi_tick_output.connect_retry &&
        mode_uses_wifi(mode)) {
        const esp_err_t result = esp_wifi_connect();
        if (ESP_OK != result) {
            network_manager_wifi_runtime_model_on_connect_failed(
                &s_wifi_model, now_ms, &wifi_retry_output);
            wifi_retry_exhausted_changed =
                wifi_retry_exhausted_changed ||
                wifi_retry_output.retry_exhausted_changed;
        }
    }
    (void)wifi_update_output;
    (void)wifi_retry_output;
    (void)cellular_update_output;

    portENTER_CRITICAL(&s_lock);
    const network_manager_snapshot_t old_snapshot = s_snapshot;
    set_interface_snapshot_locked();
    s_snapshot.mode = mode;
    s_snapshot.raw_active_interface = raw_active;
    s_snapshot.internet_reachable = internet;
    /* Reduce per-path facts into one authoritative component-wide snapshot. */
    network_manager_dual_runtime_input_t aggregate_input = {
        .mode = mode,
        .manual_offline = manual_offline,
        .wifi_config_present = s_snapshot.current_wifi_config_present,
        .wifi_raw_link_up = s_wifi_model.raw_link_up,
        .wifi_raw_ipv4_ready = s_wifi_model.raw_ipv4_ready,
        .wifi_stable_ready = s_wifi_model.stable_ready,
        .wifi_retry_exhausted = s_wifi_model.retry_exhausted,
        .cellular_raw_link_up = s_cellular_model.raw_link_up,
        .cellular_raw_ipv4_ready = s_cellular_model.raw_ipv4_ready,
        .cellular_stable_ready = s_cellular_model.raw_ipv4_ready && internet,
        .internet_reachable = internet,
        .raw_active_interface = raw_active,
        .previous_stable_active_interface = previous_stable,
    };
    network_manager_dual_runtime_output_t aggregate_output;
    network_manager_dual_runtime_model_reduce(&aggregate_input,
                                              &aggregate_output);
    s_snapshot.raw_ready = aggregate_output.raw_ready;
    s_snapshot.stable_ready = aggregate_output.stable_ready;
    s_snapshot.stable_active_interface =
        aggregate_output.stable_active_interface;
    s_snapshot.interface_switch_in_progress =
        aggregate_output.interface_switch_in_progress;
    s_snapshot.all_retry_exhausted = aggregate_output.all_retry_exhausted;
    s_snapshot.reconnect_in_progress = 0U != s_reconnect_operation;
    s_snapshot.disconnect_in_progress = 0U != s_disconnect_operation;

    const bool changed = 0 != memcmp(&old_snapshot,
                                     &s_snapshot,
                                     sizeof(s_snapshot));
    const bool ready_changed = old_snapshot.stable_ready !=
                               s_snapshot.stable_ready;
    const bool active_changed = old_snapshot.stable_active_interface !=
                                s_snapshot.stable_active_interface;
    const bool raw_changed = old_snapshot.raw_ready != s_snapshot.raw_ready ||
                             old_snapshot.wifi.raw_link_up !=
                                 s_snapshot.wifi.raw_link_up ||
                             old_snapshot.wifi.raw_ipv4_ready !=
                                 s_snapshot.wifi.raw_ipv4_ready ||
                             old_snapshot.cellular.raw_link_up !=
                                 s_snapshot.cellular.raw_link_up ||
                             old_snapshot.cellular.raw_ipv4_ready !=
                                 s_snapshot.cellular.raw_ipv4_ready;
    if (changed) {
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(ready_changed ?
                                 NETWORK_MANAGER_EVENT_READY_CHANGED :
                             active_changed ?
                                 NETWORK_MANAGER_EVENT_ACTIVE_INTERFACE_CHANGED :
                             raw_changed ? NETWORK_MANAGER_EVENT_RAW_STATE_CHANGED :
                                           NETWORK_MANAGER_EVENT_SNAPSHOT_SYNC,
                             0U,
                             NETWORK_MANAGER_OPERATION_COMPLETED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
    }
    if (cellular_initial_ipv4_timeout) {
        record_fault_locked(
            NETWORK_MANAGER_INTERFACE_4G,
            NETWORK_MANAGER_FAULT_CELLULAR_INITIAL_IPV4_TIMEOUT,
            ESP_ERR_TIMEOUT,
            0);
    }
    /* Report WiFi connected to lsd_net_mgmt after stability window completes. */
    if (!manual_offline && mode_uses_wifi(mode) &&
        (wifi_update_output.report_connected ||
         wifi_tick_output.report_connected)) {
        const esp_err_t result = lsd_net_send_event(NET_WIFI_EVENT_CONNECTED);
        if (ESP_OK != result) {
            ESP_LOGW("network_manager",
                     "Failed to report WiFi connected to lsd_net_mgmt: %d",
                     result);
        }
    }
    if (!manual_offline && mode_uses_wifi(mode) &&
        (wifi_disconnect_pending ||
         wifi_update_output.report_disconnected)) {
        /* Report WiFi disconnected to lsd_net_mgmt immediately on raw disconnect
           so the closed-source manager can switch to 4G without delay. */
        const esp_err_t result = lsd_net_send_event(NET_WIFI_EVENT_DISCONNECTED);
        if (ESP_OK != result) {
            ESP_LOGW("network_manager",
                     "Failed to report WiFi disconnected to lsd_net_mgmt: %d",
                     result);
        }
        record_fault_locked(
            NETWORK_MANAGER_INTERFACE_WIFI,
            raw_wifi_link ? NETWORK_MANAGER_FAULT_WIFI_IP_LOST :
                            map_wifi_disconnect_reason(s_wifi_raw_reason),
            ESP_FAIL,
            s_wifi_raw_reason);
    }
    if (!manual_offline && mode_uses_cellular(mode) &&
        old_snapshot.cellular.raw_ipv4_ready &&
        !s_snapshot.cellular.raw_ipv4_ready) {
        record_fault_locked(
            NETWORK_MANAGER_INTERFACE_4G,
            raw_cellular_link ? NETWORK_MANAGER_FAULT_CELLULAR_IP_LOST :
                                NETWORK_MANAGER_FAULT_CELLULAR_LINK_LOST,
            ESP_FAIL,
            0);
    }
    if (!manual_offline && old_snapshot.internet_reachable && !internet &&
        (s_snapshot.wifi.raw_ipv4_ready ||
         s_snapshot.cellular.raw_ipv4_ready)) {
        record_fault_locked(NETWORK_MANAGER_INTERFACE_NONE,
                            NETWORK_MANAGER_FAULT_INTERNET_UNREACHABLE,
                            ESP_FAIL,
                            0);
    }
    if (!manual_offline && wifi_retry_exhausted_changed) {
        record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                            NETWORK_MANAGER_FAULT_WIFI_RETRY_EXHAUSTED,
                            ESP_ERR_TIMEOUT,
                            s_wifi_raw_reason);
        publish_event_locked(NETWORK_MANAGER_EVENT_INTERFACE_RETRY_EXHAUSTED,
                             0U,
                             NETWORK_MANAGER_OPERATION_COMPLETED,
                             NETWORK_MANAGER_INTERFACE_WIFI,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
    }
    if (!manual_offline && !old_snapshot.all_retry_exhausted &&
        s_snapshot.all_retry_exhausted) {
        record_fault_locked(NETWORK_MANAGER_INTERFACE_NONE,
                            NETWORK_MANAGER_FAULT_ALL_RETRY_EXHAUSTED,
                            ESP_ERR_TIMEOUT,
                            0);
        publish_event_locked(NETWORK_MANAGER_EVENT_ALL_RETRY_EXHAUSTED,
                             0U,
                             NETWORK_MANAGER_OPERATION_COMPLETED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
    }
    if (ready_changed && s_snapshot.stable_ready) {
        if (NETWORK_MANAGER_INTERFACE_WIFI ==
            s_snapshot.stable_active_interface) {
            static const network_manager_fault_code_t wifi_faults[] = {
                NETWORK_MANAGER_FAULT_WIFI_CONFIG_MISSING,
                NETWORK_MANAGER_FAULT_WIFI_CONFIG_APPLY_FAILED,
                NETWORK_MANAGER_FAULT_WIFI_CONNECT_FAILED,
                NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED,
                NETWORK_MANAGER_FAULT_WIFI_AP_NOT_FOUND,
                NETWORK_MANAGER_FAULT_WIFI_HANDSHAKE_TIMEOUT,
                NETWORK_MANAGER_FAULT_WIFI_BEACON_TIMEOUT,
                NETWORK_MANAGER_FAULT_WIFI_DISCONNECTED,
                NETWORK_MANAGER_FAULT_WIFI_IP_LOST,
                NETWORK_MANAGER_FAULT_WIFI_RETRY_EXHAUSTED,
            };
            for (size_t index = 0U;
                 index < sizeof(wifi_faults) / sizeof(wifi_faults[0]);
                 ++index) {
                mark_fault_recovered_locked(
                    NETWORK_MANAGER_INTERFACE_WIFI, wifi_faults[index]);
            }
        } else if (NETWORK_MANAGER_INTERFACE_4G ==
                   s_snapshot.stable_active_interface) {
            static const network_manager_fault_code_t cellular_faults[] = {
                NETWORK_MANAGER_FAULT_LTE_POWER_ON_FAILED,
                NETWORK_MANAGER_FAULT_LTE_POWER_OFF_FAILED,
                NETWORK_MANAGER_FAULT_CELLULAR_LINK_LOST,
                NETWORK_MANAGER_FAULT_CELLULAR_IP_LOST,
                NETWORK_MANAGER_FAULT_CELLULAR_INITIAL_IPV4_TIMEOUT,
            };
            for (size_t index = 0U;
                 index < sizeof(cellular_faults) /
                             sizeof(cellular_faults[0]);
                 ++index) {
                mark_fault_recovered_locked(
                    NETWORK_MANAGER_INTERFACE_4G,
                    cellular_faults[index]);
            }
        }
        mark_fault_recovered_locked(
            NETWORK_MANAGER_INTERFACE_NONE,
            NETWORK_MANAGER_FAULT_INTERNET_UNREACHABLE);
        mark_fault_recovered_locked(
            NETWORK_MANAGER_INTERFACE_NONE,
            NETWORK_MANAGER_FAULT_ALL_RETRY_EXHAUSTED);
    }
    if (s_reconnect_operation != 0U && s_snapshot.stable_ready) {
        const uint32_t operation = s_reconnect_operation;
        const network_manager_policy_input_t stable_input = {
            .type = NETWORK_MANAGER_POLICY_INPUT_STABLE_READY,
            .interface = s_snapshot.stable_active_interface,
            .value = true,
        };
        network_manager_policy_output_t policy_output;
        (void)network_manager_policy_apply(
            &s_policy, &stable_input, &policy_output);
        s_reconnect_operation = 0U;
        s_snapshot.reconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_RECONNECT_STATUS,
                             operation,
                             NETWORK_MANAGER_OPERATION_COMPLETED,
                             s_snapshot.stable_active_interface,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
    }
    if (s_reconnect_operation != 0U &&
        !mode_uses_cellular(s_mode) && s_snapshot.all_retry_exhausted) {
        const uint32_t operation = s_reconnect_operation;
        const network_manager_policy_input_t terminal_input = {
            .type = NETWORK_MANAGER_POLICY_INPUT_RECONNECT_TERMINAL_FAILURE,
        };
        network_manager_policy_output_t policy_output;
        (void)network_manager_policy_apply(
            &s_policy, &terminal_input, &policy_output);
        s_reconnect_operation = 0U;
        s_snapshot.reconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_RECONNECT_STATUS,
                             operation,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
    }
    if (s_disconnect_operation != 0U && !s_snapshot.raw_ready &&
        !s_snapshot.wifi.raw_link_up && !s_snapshot.cellular.raw_link_up) {
        const uint32_t operation = s_disconnect_operation;
        s_disconnect_operation = 0U;
        s_snapshot.disconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_DISCONNECT_STATUS,
                             operation,
                             NETWORK_MANAGER_OPERATION_COMPLETED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
    }
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t execute_policy_effect(
    network_manager_policy_effect_t effect,
    const network_manager_wifi_config_t *config)
{
    /* Execute exactly one reducer-selected effect before reporting feedback. */
    if (NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG == effect) {
        return NULL == config ? ESP_ERR_INVALID_STATE :
                                apply_wifi_config(config);
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI == effect) {
        return esp_wifi_connect();
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI == effect) {
        return esp_wifi_disconnect();
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_CONNECTED == effect) {
        return lsd_net_send_event(NET_WIFI_EVENT_CONNECTED);
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_DISCONNECTED == effect) {
        return lsd_net_send_event(NET_WIFI_EVENT_DISCONNECTED);
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE == effect) {
        return lte_hal_power_off();
    }
    if (NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE == effect) {
        /* Reconnect and automatic recovery share the build-time LTE off hold. */
        vTaskDelay(pdMS_TO_TICKS(
            NETWORK_MANAGER_CELLULAR_POWER_OFF_HOLD_MS));
        return lte_hal_power_on();
    }
    return ESP_ERR_INVALID_STATE;
}

static void record_effect_failure_locked(
    network_manager_policy_effect_t effect,
    esp_err_t result)
{
    network_manager_interface_t interface = NETWORK_MANAGER_INTERFACE_NONE;
    network_manager_fault_code_t code = NETWORK_MANAGER_FAULT_INTERNAL;
    if (NETWORK_MANAGER_POLICY_EFFECT_APPLY_WIFI_CONFIG == effect) {
        interface = NETWORK_MANAGER_INTERFACE_WIFI;
        code = NETWORK_MANAGER_FAULT_WIFI_CONFIG_APPLY_FAILED;
    } else if (NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI == effect) {
        interface = NETWORK_MANAGER_INTERFACE_WIFI;
        code = NETWORK_MANAGER_FAULT_WIFI_CONNECT_FAILED;
    } else if (NETWORK_MANAGER_POLICY_EFFECT_DISCONNECT_WIFI == effect) {
        interface = NETWORK_MANAGER_INTERFACE_WIFI;
        code = NETWORK_MANAGER_FAULT_WIFI_DISCONNECTED;
    } else if (NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_CONNECTED == effect) {
        interface = NETWORK_MANAGER_INTERFACE_WIFI;
        code = NETWORK_MANAGER_FAULT_NET_MGMT_EVENT_FAILED;
    } else if (NETWORK_MANAGER_POLICY_EFFECT_REPORT_WIFI_DISCONNECTED ==
               effect) {
        interface = NETWORK_MANAGER_INTERFACE_WIFI;
        code = NETWORK_MANAGER_FAULT_NET_MGMT_EVENT_FAILED;
    } else if (NETWORK_MANAGER_POLICY_EFFECT_POWER_OFF_LTE == effect) {
        interface = NETWORK_MANAGER_INTERFACE_4G;
        code = NETWORK_MANAGER_FAULT_LTE_POWER_OFF_FAILED;
    } else if (NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE == effect) {
        interface = NETWORK_MANAGER_INTERFACE_4G;
        code = NETWORK_MANAGER_FAULT_LTE_POWER_ON_FAILED;
    }
    record_fault_locked(interface, code, result, 0);
}

static void sync_policy_inputs_locked(void)
{
    /* Synchronize observable runtime facts before starting an operation. */
    s_policy.mode = s_mode;
    s_policy.current_wifi_config_present =
        s_snapshot.current_wifi_config_present;
    s_policy.manual_offline = s_snapshot.manual_offline;
    s_policy.stable_ready = s_snapshot.stable_ready;
    s_policy.stable_active_interface = s_snapshot.stable_active_interface;
    s_policy.wifi_retry_attempt = s_wifi_model.retry_attempt;
    s_policy.wifi_retry_exhausted = s_wifi_model.retry_exhausted;
}

static void perform_disconnect(uint32_t operation_id)
{
    /* The reducer is the sole owner of mode-specific disconnect ordering. */
    network_manager_policy_output_t output;
    const network_manager_policy_input_t request = {
        .type = NETWORK_MANAGER_POLICY_INPUT_DISCONNECT_REQUEST,
    };
    portENTER_CRITICAL(&s_lock);
    sync_policy_inputs_locked();
    const network_manager_policy_result_t policy_result =
        network_manager_policy_apply(&s_policy, &request, &output);
    if (NETWORK_MANAGER_POLICY_OK != policy_result) {
        s_disconnect_operation = 0U;
        s_snapshot.disconnect_in_progress = false;
        record_fault_locked(NETWORK_MANAGER_INTERFACE_NONE,
                            NETWORK_MANAGER_FAULT_INTERNAL,
                            ESP_ERR_INVALID_STATE,
                            0);
        publish_event_locked(NETWORK_MANAGER_EVENT_DISCONNECT_STATUS,
                             operation_id,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_snapshot.manual_offline = s_policy.manual_offline;
    s_snapshot.stable_ready = s_policy.stable_ready;
    s_snapshot.stable_active_interface =
        s_policy.stable_active_interface;
    if (output.operation_status_valid &&
        NETWORK_MANAGER_OPERATION_NO_ACTION == output.operation_status) {
        s_disconnect_operation = 0U;
        s_snapshot.disconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_DISCONNECT_STATUS,
                             operation_id,
                             NETWORK_MANAGER_OPERATION_NO_ACTION,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    publish_event_locked(NETWORK_MANAGER_EVENT_DISCONNECT_STATUS,
                         operation_id,
                         NETWORK_MANAGER_OPERATION_ACCEPTED,
                         NETWORK_MANAGER_INTERFACE_NONE,
                         NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                         false,
                         false);
    portEXIT_CRITICAL(&s_lock);

    network_manager_wifi_runtime_model_set_automatic_recovery(
        &s_wifi_model, false);
    /* Continue independent Dual effects even if one physical action fails. */
    while (NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect) {
        const network_manager_policy_effect_t effect = output.effect;
        const esp_err_t result = execute_policy_effect(effect, NULL);
        if (ESP_OK != result) {
            portENTER_CRITICAL(&s_lock);
            record_effect_failure_locked(effect, result);
            portEXIT_CRITICAL(&s_lock);
        }
        const network_manager_policy_input_t effect_result = {
            .type = ESP_OK == result ?
                NETWORK_MANAGER_POLICY_INPUT_EFFECT_SUCCEEDED :
                NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED,
            .source_error = result,
        };
        (void)network_manager_policy_apply(
            &s_policy, &effect_result, &output);
    }
    if (output.operation_status_valid &&
        NETWORK_MANAGER_OPERATION_FAILED == output.operation_status) {
        portENTER_CRITICAL(&s_lock);
        s_disconnect_operation = 0U;
        s_snapshot.disconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_DISCONNECT_STATUS,
                             operation_id,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
    }
}

static void perform_reconnect(uint32_t operation_id)
{
    /* Reconnect rebuilds all mode-enabled paths from current in-memory config. */
    network_manager_wifi_config_t config;
    bool config_present;
    network_manager_policy_output_t output;
    const network_manager_policy_input_t request = {
        .type = NETWORK_MANAGER_POLICY_INPUT_RECONNECT_REQUEST,
    };
    portENTER_CRITICAL(&s_lock);
    config_present = s_snapshot.current_wifi_config_present;
    config = s_current_wifi_config;
    sync_policy_inputs_locked();
    const network_manager_policy_result_t policy_result =
        network_manager_policy_apply(&s_policy, &request, &output);
    s_snapshot.manual_offline = s_policy.manual_offline;
    s_snapshot.stable_ready = s_policy.stable_ready;
    s_snapshot.stable_active_interface =
        s_policy.stable_active_interface;
    s_snapshot.wifi.retry_attempt = 0U;
    s_snapshot.cellular.retry_attempt = 0U;
    s_snapshot.wifi.retry_exhausted = false;
    s_snapshot.cellular.retry_exhausted = false;
    if (mode_uses_wifi(s_mode) && !config_present) {
        record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                            NETWORK_MANAGER_FAULT_WIFI_CONFIG_MISSING,
                            ESP_ERR_NOT_FOUND,
                            0);
    }
    if (NETWORK_MANAGER_POLICY_OK != policy_result ||
        (output.operation_status_valid &&
         NETWORK_MANAGER_OPERATION_FAILED == output.operation_status)) {
        s_reconnect_operation = 0U;
        s_snapshot.reconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_RECONNECT_STATUS,
                             operation_id,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    publish_event_locked(NETWORK_MANAGER_EVENT_RECONNECT_STATUS,
                         operation_id,
                         NETWORK_MANAGER_OPERATION_ACCEPTED,
                         NETWORK_MANAGER_INTERFACE_NONE,
                         NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                         false,
                         false);
    portEXIT_CRITICAL(&s_lock);

    /* Reset the WiFi budget and the observed cellular snapshot. */
    network_manager_wifi_runtime_model_reset_retry(&s_wifi_model);
    network_manager_wifi_runtime_model_set_automatic_recovery(
        &s_wifi_model, true);
    network_manager_cellular_runtime_model_init(&s_cellular_model);
    while (NETWORK_MANAGER_POLICY_EFFECT_NONE != output.effect) {
        const network_manager_policy_effect_t effect = output.effect;
        const esp_err_t result = execute_policy_effect(
            effect, config_present ? &config : NULL);
        if (ESP_OK != result) {
            portENTER_CRITICAL(&s_lock);
            record_effect_failure_locked(effect, result);
            portEXIT_CRITICAL(&s_lock);
        }
        if (NETWORK_MANAGER_POLICY_EFFECT_CONNECT_WIFI == effect &&
            ESP_OK != result) {
            network_manager_wifi_runtime_model_on_connect_failed(
                &s_wifi_model,
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                &(network_manager_wifi_runtime_output_t){0});
        }
        if (NETWORK_MANAGER_POLICY_EFFECT_POWER_ON_LTE == effect &&
            ESP_OK == result) {
            network_manager_cellular_runtime_model_manager_initialized(
                &s_cellular_model,
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                &(network_manager_cellular_runtime_output_t){0});
        }
        const network_manager_policy_input_t effect_result = {
            .type = ESP_OK == result ?
                NETWORK_MANAGER_POLICY_INPUT_EFFECT_SUCCEEDED :
                NETWORK_MANAGER_POLICY_INPUT_EFFECT_FAILED,
            .source_error = result,
        };
        (void)network_manager_policy_apply(
            &s_policy, &effect_result, &output);
    }
    if (output.operation_status_valid &&
        NETWORK_MANAGER_OPERATION_FAILED == output.operation_status) {
        portENTER_CRITICAL(&s_lock);
        s_reconnect_operation = 0U;
        s_snapshot.reconnect_in_progress = false;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_RECONNECT_STATUS,
                             operation_id,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
    }
}

static void process_command(const network_manager_command_t *command)
{
    /* All mutating commands are serialized here by the single worker queue. */
    if (NETWORK_MANAGER_COMMAND_SET_CONFIG == command->type) {
        const esp_err_t apply_result =
            mode_uses_wifi(s_mode) ? apply_wifi_config(&command->config) :
                                     ESP_OK;
        bool saved = false;
        esp_err_t save_result = ESP_OK;
        if (command->persist && s_persistence_enabled) {
            save_result = network_manager_storage_nvs_save(
                s_nvs_handle, &command->config);
            saved = ESP_OK == save_result;
        }
        portENTER_CRITICAL(&s_lock);
        /* Current config is accepted even if apply or persistence fails. */
        s_current_wifi_config = command->config;
        s_snapshot.current_wifi_config_present = true;
        s_policy.current_wifi_config_present = true;
        s_snapshot.wifi_config_revision =
            next_nonzero(s_snapshot.wifi_config_revision);
        if (saved) {
            s_persisted_wifi_config = command->config;
            s_snapshot.persisted_wifi_config_present = true;
        }
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        const esp_err_t result = ESP_OK != apply_result ? apply_result :
                                (ESP_OK != save_result ? save_result : ESP_OK);
        if (ESP_OK != apply_result) {
            record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                                NETWORK_MANAGER_FAULT_WIFI_CONFIG_APPLY_FAILED,
                                apply_result,
                                0);
        } else if (ESP_OK != save_result) {
            record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                                NETWORK_MANAGER_FAULT_WIFI_CONFIG_WRITE_FAILED,
                                save_result,
                                0);
        }
        publish_event_locked(NETWORK_MANAGER_EVENT_WIFI_CONFIG_RESULT,
                             command->operation_id,
                             ESP_OK == result ? NETWORK_MANAGER_OPERATION_COMPLETED :
                                                NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_WIFI,
                             command->persist ? NETWORK_MANAGER_WIFI_CONFIG_SAVE :
                                                NETWORK_MANAGER_WIFI_CONFIG_APPLY,
                             ESP_OK == apply_result,
                             saved);
        portEXIT_CRITICAL(&s_lock);
        if (ESP_OK == apply_result && mode_uses_wifi(s_mode) &&
            s_wifi_driver_started) {
            (void)esp_wifi_connect();
        }
    } else if (NETWORK_MANAGER_COMMAND_CLEAR_CONFIG == command->type) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (s_persistence_enabled) {
            result = network_manager_storage_nvs_clear(s_nvs_handle);
        }
        portENTER_CRITICAL(&s_lock);
        if (ESP_OK == result) {
            s_snapshot.persisted_wifi_config_present = false;
            s_persisted_wifi_config = (network_manager_wifi_config_t){0};
        } else {
            record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                                NETWORK_MANAGER_FAULT_WIFI_CONFIG_CLEAR_FAILED,
                                result,
                                0);
        }
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        publish_event_locked(NETWORK_MANAGER_EVENT_WIFI_CONFIG_RESULT,
                             command->operation_id,
                             ESP_OK == result ? NETWORK_MANAGER_OPERATION_COMPLETED :
                                                NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_WIFI,
                             NETWORK_MANAGER_WIFI_CONFIG_CLEAR,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
    } else if (NETWORK_MANAGER_COMMAND_DISCONNECT == command->type) {
        perform_disconnect(command->operation_id);
    } else if (NETWORK_MANAGER_COMMAND_RECONNECT == command->type) {
        perform_reconnect(command->operation_id);
    } else if (NETWORK_MANAGER_COMMAND_WIFI_SCAN == command->type) {
        portENTER_CRITICAL(&s_lock);
        s_wifi_scan_request_pending = true;
        portEXIT_CRITICAL(&s_lock);
    }
}

#ifdef NETWORK_MANAGER_HOST_TEST
bool network_manager_host_test_process_one(void)
{
    network_manager_command_t command;
    if (!s_initial_attempt_complete) {
        run_initial_attempt();
    }
    if (NULL == s_command_queue ||
        pdTRUE != xQueueReceive(s_command_queue, &command, 0U)) {
        return false;
    }
    process_command(&command);
    process_wifi_scan_state();
    recompute_snapshot(0U);
    return true;
}

void network_manager_host_test_process_runtime(uint32_t now_ms)
{
    if (!s_initial_attempt_complete) {
        run_initial_attempt();
    }
    process_wifi_scan_state();
    recompute_snapshot(now_ms);
}
#endif

static void network_worker_task(void *argument)
{
    /* Periodic ticks advance debounce and recovery deadlines without timers. */
    (void)argument;
    run_initial_attempt();
    portENTER_CRITICAL(&s_lock);
    const bool initial_attempt_failed = ESP_OK != s_initial_attempt_result;
    if (initial_attempt_failed) {
        s_worker_task = NULL;
    }
    portEXIT_CRITICAL(&s_lock);
    if (initial_attempt_failed) {
        NETWORK_MANAGER_TASK_DELETE(NULL);
        return;
    }
    for (;;) {
        network_manager_command_t command;
        if (xQueueReceive(s_command_queue,
                          &command,
                          pdMS_TO_TICKS(NETWORK_MANAGER_TICK_MS)) == pdTRUE) {
            process_command(&command);
        }
        process_wifi_scan_state();
        recompute_snapshot((uint32_t)(xTaskGetTickCount() *
                                      portTICK_PERIOD_MS));
    }
}

static void dispatch_pending_events(void)
{
    /* Critical events are drained before ordinary FIFO events. */
    for (;;) {
        network_manager_event_t event;
        bool available;
        portENTER_CRITICAL(&s_lock);
        available = network_manager_event_journal_take_critical(
            &s_journal, &event);
        if (!available) {
            available = network_manager_event_journal_take_next(
                &s_journal, &event);
        }
        /* Invoke callbacks outside s_lock using a stable subscriber snapshot. */
        network_manager_subscription_t subscriptions[
            NETWORK_MANAGER_MAX_SUBSCRIBERS];
        memcpy(subscriptions, s_subscriptions, sizeof(subscriptions));
        portEXIT_CRITICAL(&s_lock);
        if (!available) {
            break;
        }
        for (size_t index = 0U;
             index < NETWORK_MANAGER_MAX_SUBSCRIBERS;
             ++index) {
            if (subscriptions[index].used &&
                NULL != subscriptions[index].callback) {
                subscriptions[index].callback(&event,
                                              subscriptions[index].user_ctx);
            }
        }
    }
}

static void network_dispatcher_task(void *argument)
{
    (void)argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NETWORK_MANAGER_TICK_MS));
        dispatch_pending_events();
    }
}

#ifdef NETWORK_MANAGER_HOST_TEST
void network_manager_host_test_dispatch_all(void)
{
    dispatch_pending_events();
}
#endif

static void cleanup_start_resources(void)
{
    /* Roll back owned tasks, queue, and handlers; never deinit net_mgmt. */
    if (NULL != s_worker_task) {
        NETWORK_MANAGER_TASK_DELETE(s_worker_task);
        s_worker_task = NULL;
    }
    if (NULL != s_dispatcher_task) {
        NETWORK_MANAGER_TASK_DELETE(s_dispatcher_task);
        s_dispatcher_task = NULL;
    }
    if (NULL != s_command_queue) {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
    }
    (void)esp_event_handler_unregister(WIFI_EVENT,
                                       ESP_EVENT_ANY_ID,
                                       &network_event_handler);
    (void)esp_event_handler_unregister(IP_EVENT,
                                       ESP_EVENT_ANY_ID,
                                       &network_event_handler);
    (void)esp_event_handler_unregister(ETH_EVENT,
                                       ESP_EVENT_ANY_ID,
                                       &network_event_handler);
    lsd_net_register_switch_cb(NULL);
}

static esp_err_t initialize_hardware(void)
{
    /*
     * Build shared ESP resources and initialize net_mgmt exactly once. NVS is
     * optional; loss of persistence does not prevent volatile connectivity.
     */
    esp_err_t result = nvs_flash_init();
    if (ESP_OK == result) {
        s_persistence_enabled = ESP_OK == nvs_open(
            NETWORK_MANAGER_STORAGE_NVS_NAMESPACE,
            NVS_READWRITE,
            &s_nvs_handle);
    } else {
        s_persistence_enabled = false;
        ESP_LOGW(TAG, "NVS unavailable; persistence disabled (%s)",
                 esp_err_to_name(result));
    }
    if (s_persistence_enabled) {
        bool present = false;
        uint32_t generation = 0U;
        network_manager_wifi_config_t loaded = {0};
        result = network_manager_storage_nvs_load(s_nvs_handle,
                                                  &loaded,
                                                  &present,
                                                  &generation);
        if (ESP_OK == result && present) {
            s_current_wifi_config = loaded;
            s_persisted_wifi_config = loaded;
            s_snapshot.current_wifi_config_present = true;
            s_snapshot.persisted_wifi_config_present = true;
        } else if (ESP_OK != result) {
            portENTER_CRITICAL(&s_lock);
            record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                                NETWORK_MANAGER_FAULT_WIFI_CONFIG_READ_FAILED,
                                result,
                                0);
            portEXIT_CRITICAL(&s_lock);
        }
    }

    result = esp_netif_init();
    if (ESP_ERR_INVALID_STATE != result && ESP_OK != result) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (ESP_ERR_INVALID_STATE != result && ESP_OK != result) {
        return result;
    }
    esp_netif_t *wifi_netif = esp_netif_create_default_wifi_sta();
    if (NULL == wifi_netif) {
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_init);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ESP_OK != result) {
        return result;
    }
    /*
     * Keep Wi-Fi config in RAM only so the driver does not auto-connect using a
     * stale SSID/password persisted in NVS from a previous session. Otherwise
     * esp_wifi_start() in start_wifi_driver() kicks off an NVS-based connect that
     * races apply_wifi_config() (observed as "sta is connecting, cannot set
     * config"). The manager always applies the intended config explicitly.
     */
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_event_handler_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &network_event_handler,
                                        NULL);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_event_handler_register(IP_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &network_event_handler,
                                        NULL);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_event_handler_register(ETH_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &network_event_handler,
                                        NULL);
    if (ESP_OK != result) {
        return result;
    }

    lsd_net_register_switch_cb(&on_net_interface_switch);
    if (mode_uses_cellular(s_mode)) {
        /* Slow LTE power-on and the one-shot manager init belong to the worker. */
        result = lte_hal_init();
    } else {
        result = lsd_network_mgmt_init(false);
    }
    if (ESP_OK != result) {
        return result;
    }

    /*
     * Wi-Fi start is deferred to start_wifi_driver(), invoked by the worker only
     * after cellular USB host registration completes. esp_wifi_start() allocates
     * internal DMA-capable RAM; starting it before the 4G USB host client is
     * registered starves lsd_4g's usb_host_client_register() (INVALID_STATE) and
     * breaks 4G enumeration in DUAL mode. esp_wifi_init() above only allocates
     * static control buffers and is safe to keep here.
     */
    return ESP_OK;
}

static void start_wifi_driver(void)
{
    /*
     * Bring the Wi-Fi data path up. Called from the worker after any cellular
     * USB host registration has finished, so the WiFi DMA allocations no longer
     * race the 4G USB host client. Safe to skip when the mode excludes Wi-Fi.
     */
    if (!mode_uses_wifi(s_mode)) {
        return;
    }
    esp_err_t result = esp_wifi_start();
    if (ESP_OK != result && ESP_ERR_INVALID_STATE != result) {
        portENTER_CRITICAL(&s_lock);
        record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                            NETWORK_MANAGER_FAULT_WIFI_CONNECT_FAILED,
                            result,
                            0);
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_wifi_driver_started = true;
    if (s_snapshot.current_wifi_config_present) {
        result = apply_wifi_config(&s_current_wifi_config);
        if (ESP_OK == result) {
            result = esp_wifi_connect();
        }
        if (ESP_OK != result) {
            portENTER_CRITICAL(&s_lock);
            record_fault_locked(NETWORK_MANAGER_INTERFACE_WIFI,
                                NETWORK_MANAGER_FAULT_WIFI_CONNECT_FAILED,
                                result,
                                0);
            portEXIT_CRITICAL(&s_lock);
        }
    } else {
        portENTER_CRITICAL(&s_lock);
        s_snapshot.wifi_phase = NETWORK_MANAGER_WIFI_CONFIG_MISSING;
        portEXIT_CRITICAL(&s_lock);
    }
}

static void complete_initial_attempt(
    esp_err_t result,
    network_manager_fault_code_t failure_code)
{
    const uint32_t now_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (ESP_OK == result) {
        if (mode_uses_cellular(s_mode)) {
            network_manager_cellular_runtime_model_manager_initialized(
                &s_cellular_model,
                now_ms,
                &(network_manager_cellular_runtime_output_t){0});
        }
        portENTER_CRITICAL(&s_lock);
        mark_fault_recovered_locked(NETWORK_MANAGER_INTERFACE_NONE,
                                    NETWORK_MANAGER_FAULT_START_BARRIER_TIMEOUT);
        s_snapshot.lifecycle = NETWORK_MANAGER_LIFECYCLE_RUNNING;
        s_policy.started = true;
        s_policy.current_wifi_config_present =
            s_snapshot.current_wifi_config_present;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        portEXIT_CRITICAL(&s_lock);
        recompute_snapshot(now_ms);
        portENTER_CRITICAL(&s_lock);
        publish_event_locked(NETWORK_MANAGER_EVENT_START_RESULT,
                             0U,
                             NETWORK_MANAGER_OPERATION_COMPLETED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
    } else {
        portENTER_CRITICAL(&s_lock);
        s_snapshot.lifecycle = NETWORK_MANAGER_LIFECYCLE_START_FAILED;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        record_fault_locked(NETWORK_MANAGER_INTERFACE_4G,
                            failure_code,
                            result,
                            0);
        publish_event_locked(NETWORK_MANAGER_EVENT_START_RESULT,
                             0U,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_4G,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
    }
    TaskHandle_t wait_task;
    portENTER_CRITICAL(&s_lock);
    s_initial_attempt_result = result;
    s_initial_attempt_complete = true;
    wait_task = s_start_wait_task;
    portEXIT_CRITICAL(&s_lock);
    if (NULL != wait_task) {
        xTaskNotifyGive(wait_task);
    }
}

static void run_initial_attempt(void)
{
    /* Only the worker may perform the one-shot LTE manager initialization. */
    if (!mode_uses_cellular(s_mode)) {
        /* WIFI_ONLY: no cellular init, so start Wi-Fi right away. */
        start_wifi_driver();
        complete_initial_attempt(ESP_OK, NETWORK_MANAGER_FAULT_NONE);
        return;
    }
    ESP_LOGI(TAG, "[NM_DIAG] Starting LTE power-on sequence");
    esp_err_t result = lte_hal_power_on();
    ESP_LOGI(TAG, "[NM_DIAG] lte_hal_power_on() returned: %s (0x%x)",
             esp_err_to_name(result), result);
    if (ESP_OK != result) {
        complete_initial_attempt(result,
                                 NETWORK_MANAGER_FAULT_LTE_POWER_ON_FAILED);
        return;
    }
    ESP_LOGI(TAG, "[NM_DIAG] Starting lsd_network_mgmt_init(true)");
    result = lsd_network_mgmt_init(true);
    ESP_LOGI(TAG, "[NM_DIAG] lsd_network_mgmt_init(true) returned: %s (0x%x)",
             esp_err_to_name(result), result);
    /*
     * Now that the cellular USB host registration has finished (or there was no
     * cellular init because mode is WIFI_ONLY), it is safe to start Wi-Fi without
     * starving the USB host client of internal DMA-capable RAM.
     */
    start_wifi_driver();
    complete_initial_attempt(result,
                             NETWORK_MANAGER_FAULT_MANAGER_INIT_FAILED);
}

esp_err_t network_manager_set_mode(network_manager_mode_t mode)
{
    /* Mode becomes immutable as soon as start leaves UNINITIALIZED. */
    if (!mode_is_valid(mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED != s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_mode = mode;
    s_snapshot.mode = mode;
    network_manager_policy_init(&s_policy, mode);
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t network_manager_get_mode(network_manager_mode_t *mode)
{
    /* Pre-start queries expose the Kconfig default or API override. */
    if (NULL == mode) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    *mode = s_mode;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t network_manager_start(void)
{
    /* STARTING/RUNNING calls are idempotent; START_FAILED is reboot-only. */
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (NETWORK_MANAGER_LIFECYCLE_RUNNING == s_snapshot.lifecycle ||
        NETWORK_MANAGER_LIFECYCLE_STARTING == s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    if (NETWORK_MANAGER_LIFECYCLE_START_FAILED == s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_snapshot.mode = s_mode;
    s_snapshot.lifecycle = NETWORK_MANAGER_LIFECYCLE_STARTING;
    s_initial_attempt_complete = false;
    s_initial_attempt_result = ESP_ERR_INVALID_STATE;
    s_start_wait_task = xTaskGetCurrentTaskHandle();
    portEXIT_CRITICAL(&s_lock);

    const esp_err_t setup_result = initialize_hardware();
    if (ESP_OK != setup_result) {
        portENTER_CRITICAL(&s_lock);
        s_start_wait_task = NULL;
        s_snapshot.lifecycle = NETWORK_MANAGER_LIFECYCLE_START_FAILED;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        record_fault_locked(NETWORK_MANAGER_INTERFACE_NONE,
                            NETWORK_MANAGER_FAULT_INTERNAL,
                            setup_result,
                            0);
        publish_event_locked(NETWORK_MANAGER_EVENT_START_RESULT,
                             0U,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
        return setup_result;
    }
    s_command_queue = xQueueCreate(NETWORK_MANAGER_COMMAND_QUEUE_LENGTH,
                                   sizeof(network_manager_command_t));
    if (NULL == s_command_queue ||
        pdPASS != NETWORK_MANAGER_TASK_CREATE(network_dispatcher_task,
                                               "net_dispatch",
                                               NETWORK_MANAGER_DISPATCHER_STACK,
                                               NULL,
                                               NETWORK_MANAGER_DISPATCHER_PRIORITY,
                                               &s_dispatcher_task) ||
        pdPASS != NETWORK_MANAGER_TASK_CREATE(network_worker_task,
                                               "net_worker",
                                               NETWORK_MANAGER_WORKER_STACK,
                                               NULL,
                                               NETWORK_MANAGER_WORKER_PRIORITY,
                                               &s_worker_task)) {
        cleanup_start_resources();
        portENTER_CRITICAL(&s_lock);
        s_start_wait_task = NULL;
        s_snapshot.lifecycle = NETWORK_MANAGER_LIFECYCLE_START_FAILED;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        record_fault_locked(NETWORK_MANAGER_INTERFACE_NONE,
                            NETWORK_MANAGER_FAULT_TASK_CREATE_FAILED,
                            ESP_ERR_NO_MEM,
                            0);
        publish_event_locked(NETWORK_MANAGER_EVENT_START_RESULT,
                             0U,
                             NETWORK_MANAGER_OPERATION_FAILED,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    const uint32_t initial_attempt_notified = ulTaskNotifyTake(
        pdTRUE,
        pdMS_TO_TICKS(NETWORK_MANAGER_INITIAL_ATTEMPT_TIMEOUT_MS));
    portENTER_CRITICAL(&s_lock);
    s_start_wait_task = NULL;
    if (0U == initial_attempt_notified && !s_initial_attempt_complete) {
        /* Timeout is observable but never cancels the worker-owned attempt. */
        record_fault_locked(NETWORK_MANAGER_INTERFACE_NONE,
                            NETWORK_MANAGER_FAULT_START_BARRIER_TIMEOUT,
                            ESP_ERR_TIMEOUT,
                            0);
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

bool network_manager_is_ready(void)
{
    /* Applications consume debounced readiness, never a raw link shortcut. */
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    const bool ready = s_snapshot.stable_ready;
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

esp_err_t network_manager_get_snapshot(network_manager_snapshot_t *snapshot)
{
    /* Copy complete state atomically so related fields share one revision. */
    if (NULL == snapshot) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    *snapshot = s_snapshot;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static esp_err_t enqueue_command(network_manager_command_t *command)
{
    /* Mutating APIs are non-blocking; queue saturation is visible to callers. */
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    const bool accepted = NULL != s_command_queue &&
                          s_snapshot.lifecycle !=
                              NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED &&
                          s_snapshot.lifecycle !=
                              NETWORK_MANAGER_LIFECYCLE_START_FAILED;
    portEXIT_CRITICAL(&s_lock);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pdTRUE != xQueueSend(s_command_queue, command, 0U)) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t network_manager_wifi_set_config(
    const network_manager_wifi_config_t *config,
    bool persist,
    uint32_t *operation_id)
{
    /* Validate and copy credentials before crossing the async boundary. */
    if (NULL == config || NULL == operation_id ||
        !network_manager_wifi_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    network_manager_command_t command = {
        .type = NETWORK_MANAGER_COMMAND_SET_CONFIG,
        .persist = persist,
    };
    memcpy(&command.config, config, sizeof(command.config));
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    command.operation_id = allocate_operation_id_locked();
    portEXIT_CRITICAL(&s_lock);
    const esp_err_t result = enqueue_command(&command);
    if (ESP_OK == result) {
        *operation_id = command.operation_id;
    }
    return result;
}

esp_err_t network_manager_wifi_get_current_config(
    network_manager_wifi_config_t *config)
{
    /* Credentials remain query-only and are never copied into public events. */
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED == s_snapshot.lifecycle ||
        NETWORK_MANAGER_LIFECYCLE_START_FAILED == s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_snapshot.current_wifi_config_present) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *config = s_current_wifi_config;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t network_manager_wifi_get_persisted_config(
    network_manager_wifi_config_t *config)
{
    /* Return the last successfully loaded or saved persistent value. */
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED == s_snapshot.lifecycle ||
        NETWORK_MANAGER_LIFECYCLE_START_FAILED == s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_snapshot.persisted_wifi_config_present) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *config = s_persisted_wifi_config;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t network_manager_wifi_scan_start(uint32_t *operation_id)
{
    network_manager_command_t command = {
        .type = NETWORK_MANAGER_COMMAND_WIFI_SCAN,
    };

    if (NULL == operation_id) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (!mode_uses_wifi(s_mode) || s_snapshot.wifi_scan_in_progress ||
        NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED == s_snapshot.lifecycle ||
        NETWORK_MANAGER_LIFECYCLE_START_FAILED == s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    command.operation_id = allocate_operation_id_locked();
    s_snapshot.wifi_scan_in_progress = true;
    s_wifi_scan_operation = command.operation_id;
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    portEXIT_CRITICAL(&s_lock);
    const esp_err_t result = enqueue_command(&command);
    if (ESP_OK != result) {
        portENTER_CRITICAL(&s_lock);
        s_snapshot.wifi_scan_in_progress = false;
        s_wifi_scan_operation = 0U;
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        portEXIT_CRITICAL(&s_lock);
        return result;
    }
    *operation_id = command.operation_id;
    return ESP_OK;
}

esp_err_t network_manager_wifi_scan_get_latest(
    network_manager_wifi_scan_list_t *list)
{
    if (NULL == list) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (0U == s_wifi_scan_list.revision) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    *list = s_wifi_scan_list;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t network_manager_wifi_clear_persisted_config(uint32_t *operation_id)
{
    /* Clearing persistence is asynchronous and leaves current config intact. */
    if (NULL == operation_id) {
        return ESP_ERR_INVALID_ARG;
    }
    network_manager_command_t command = {
        .type = NETWORK_MANAGER_COMMAND_CLEAR_CONFIG,
    };
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    command.operation_id = allocate_operation_id_locked();
    portEXIT_CRITICAL(&s_lock);
    const esp_err_t result = enqueue_command(&command);
    if (ESP_OK == result) {
        *operation_id = command.operation_id;
    }
    return result;
}

esp_err_t network_manager_subscribe(network_manager_event_cb_t callback,
                                    void *user_ctx,
                                    uint32_t *subscription_id)
{
    /* Subscription is legal before start so startup results are observable. */
    if (NULL == callback || NULL == subscription_id) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    size_t free_index = NETWORK_MANAGER_MAX_SUBSCRIBERS;
    for (size_t index = 0U; index < NETWORK_MANAGER_MAX_SUBSCRIBERS; ++index) {
        if (!s_subscriptions[index].used) {
            free_index = index;
            break;
        }
    }
    if (NETWORK_MANAGER_MAX_SUBSCRIBERS == free_index) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    const uint32_t id = allocate_subscription_id_locked();
    s_subscriptions[free_index] = (network_manager_subscription_t){
        .used = true,
        .id = id,
        .callback = callback,
        .user_ctx = user_ctx,
    };
    *subscription_id = id;
    publish_event_locked(NETWORK_MANAGER_EVENT_SNAPSHOT_SYNC,
                         0U,
                         NETWORK_MANAGER_OPERATION_COMPLETED,
                         NETWORK_MANAGER_INTERFACE_NONE,
                         NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                         false,
                         false);
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t network_manager_unsubscribe(uint32_t subscription_id)
{
    /* A callback already copied by the dispatcher may still finish once. */
    if (0U == subscription_id) {
        return ESP_ERR_NOT_FOUND;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    for (size_t index = 0U; index < NETWORK_MANAGER_MAX_SUBSCRIBERS; ++index) {
        if (s_subscriptions[index].used &&
            subscription_id == s_subscriptions[index].id) {
            s_subscriptions[index] = (network_manager_subscription_t){0};
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t network_manager_get_fault_history(
    network_manager_fault_history_t *history)
{
    /* Copy history atomically in its documented oldest-to-newest order. */
    if (NULL == history) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    *history = s_fault_history;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static esp_err_t request_operation(network_manager_command_type_t type,
                                   uint32_t *operation_id)
{
    /* Opposite operations are rejected; same-type duplicates report by event. */
    if (NULL == operation_id) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    ensure_initialized_locked();
    if (NULL == s_command_queue ||
        NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED == s_snapshot.lifecycle) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (NETWORK_MANAGER_COMMAND_DISCONNECT == type &&
        0U != s_reconnect_operation) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (NETWORK_MANAGER_COMMAND_RECONNECT == type &&
        0U != s_disconnect_operation) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t id = allocate_operation_id_locked();
    if ((NETWORK_MANAGER_COMMAND_DISCONNECT == type &&
         0U != s_disconnect_operation) ||
        (NETWORK_MANAGER_COMMAND_RECONNECT == type &&
         0U != s_reconnect_operation)) {
        publish_event_locked(NETWORK_MANAGER_COMMAND_DISCONNECT == type ?
                                 NETWORK_MANAGER_EVENT_DISCONNECT_STATUS :
                                 NETWORK_MANAGER_EVENT_RECONNECT_STATUS,
                             id,
                             NETWORK_MANAGER_OPERATION_ALREADY_IN_PROGRESS,
                             NETWORK_MANAGER_INTERFACE_NONE,
                             NETWORK_MANAGER_WIFI_CONFIG_LOAD,
                             false,
                             false);
        *operation_id = id;
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    network_manager_command_t command = {
        .type = type,
        .operation_id = id,
    };
    if (NETWORK_MANAGER_COMMAND_DISCONNECT == type) {
        s_disconnect_operation = id;
        s_snapshot.disconnect_in_progress = true;
    } else {
        s_reconnect_operation = id;
        s_snapshot.reconnect_in_progress = true;
    }
    s_snapshot.revision = next_nonzero(s_snapshot.revision);
    portEXIT_CRITICAL(&s_lock);
    if (pdTRUE != xQueueSend(s_command_queue, &command, 0U)) {
        /* Undo the reserved operation state when the queue rejects the command. */
        portENTER_CRITICAL(&s_lock);
        if (NETWORK_MANAGER_COMMAND_DISCONNECT == type &&
            id == s_disconnect_operation) {
            s_disconnect_operation = 0U;
            s_snapshot.disconnect_in_progress = false;
        } else if (NETWORK_MANAGER_COMMAND_RECONNECT == type &&
                   id == s_reconnect_operation) {
            s_reconnect_operation = 0U;
            s_snapshot.reconnect_in_progress = false;
        }
        s_snapshot.revision = next_nonzero(s_snapshot.revision);
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_TIMEOUT;
    }
    *operation_id = id;
    return ESP_OK;
}

esp_err_t network_manager_request_disconnect(uint32_t *operation_id)
{
    return request_operation(NETWORK_MANAGER_COMMAND_DISCONNECT,
                             operation_id);
}

esp_err_t network_manager_request_reconnect(uint32_t *operation_id)
{
    return request_operation(NETWORK_MANAGER_COMMAND_RECONNECT,
                             operation_id);
}
