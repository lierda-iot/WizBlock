/**
 * @file network_manager.h
 * @brief Public API for startup-locked Wi-Fi, 4G, and dual-network control.
 *
 * The component owns network policy after network_manager_start(). Wi-Fi
 * configuration and disconnect/reconnect APIs enqueue work and report final
 * results through events and snapshots. Public APIs are task-context APIs and
 * are not safe to call from an ISR.
 */
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum SSID length in bytes; SSIDs are not NUL-terminated strings. */
#define NETWORK_MANAGER_WIFI_SSID_MAX_BYTES       32U
/** Maximum password length in bytes, including a 64-character hexadecimal PSK. */
#define NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES   64U
/** Maximum number of simultaneously registered event subscriptions. */
#define NETWORK_MANAGER_MAX_SUBSCRIBERS             4U
/** Number of fault records retained in the queryable history. */
#define NETWORK_MANAGER_FAULT_HISTORY_CAPACITY      16U
/** Maximum number of deduplicated access points retained per scan. */
#define NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS        20U

/** Network topology selected before the first start. */
typedef enum {
    NETWORK_MANAGER_MODE_WIFI_ONLY = 1, /**< Enable only the Wi-Fi path. */
    NETWORK_MANAGER_MODE_4G_ONLY = 2,   /**< Enable only the cellular path. */
    NETWORK_MANAGER_MODE_DUAL_AUTO = 3, /**< Enable Wi-Fi and cellular paths. */
} network_manager_mode_t;

/** Component lifecycle. A START_FAILED instance requires a controlled reboot. */
typedef enum {
    NETWORK_MANAGER_LIFECYCLE_UNINITIALIZED = 0, /**< Start not accepted yet. */
    NETWORK_MANAGER_LIFECYCLE_STARTING = 1,      /**< Resources are being created. */
    NETWORK_MANAGER_LIFECYCLE_RUNNING = 2,       /**< Worker and dispatcher are active. */
    NETWORK_MANAGER_LIFECYCLE_START_FAILED = 3,  /**< Startup failed irrecoverably. */
} network_manager_lifecycle_t;

/** Logical network interface used by snapshots, events, and faults. */
typedef enum {
    NETWORK_MANAGER_INTERFACE_NONE = 0, /**< No interface is selected. */
    NETWORK_MANAGER_INTERFACE_WIFI = 1, /**< Wi-Fi station interface. */
    NETWORK_MANAGER_INTERFACE_4G = 2,   /**< LTE/ECM cellular interface. */
} network_manager_interface_t;

/** Observable Wi-Fi state-machine phase. */
typedef enum {
    NETWORK_MANAGER_WIFI_DISABLED = 0,
    NETWORK_MANAGER_WIFI_CONFIG_MISSING = 1,
    NETWORK_MANAGER_WIFI_IDLE = 2,
    NETWORK_MANAGER_WIFI_CONNECTING = 3,
    NETWORK_MANAGER_WIFI_WAIT_STABLE = 4,
    NETWORK_MANAGER_WIFI_READY = 5,
    NETWORK_MANAGER_WIFI_BACKOFF = 6,
    NETWORK_MANAGER_WIFI_EXHAUSTED = 7,
    NETWORK_MANAGER_WIFI_FAULT = 8,
} network_manager_wifi_phase_t;

/** Observable cellular state-machine phase. */
typedef enum {
    NETWORK_MANAGER_4G_DISABLED = 0,
    NETWORK_MANAGER_4G_POWERING_ON = 1,
    NETWORK_MANAGER_4G_MANAGER_INIT = 2,
    NETWORK_MANAGER_4G_WAIT_LINK = 3,
    NETWORK_MANAGER_4G_WAIT_IPV4 = 4,
    NETWORK_MANAGER_4G_WAIT_INTERNET = 5,
    NETWORK_MANAGER_4G_READY = 6,
    NETWORK_MANAGER_4G_DISCONNECT_GRACE = 7,
    NETWORK_MANAGER_4G_POWER_CYCLE = 8,
    NETWORK_MANAGER_4G_WAIT_RECOVERY_IPV4 = 9,
    NETWORK_MANAGER_4G_BACKOFF = 10,
    NETWORK_MANAGER_4G_EXHAUSTED = 11,
    NETWORK_MANAGER_4G_FAULT = 12,
} network_manager_4g_phase_t;

/** Stable, queryable fault categories. source_error retains the raw error. */
typedef enum {
    NETWORK_MANAGER_FAULT_NONE = 0,
    NETWORK_MANAGER_FAULT_INVALID_ARGUMENT = 1,
    NETWORK_MANAGER_FAULT_INVALID_MODE = 2,
    NETWORK_MANAGER_FAULT_COMMAND_QUEUE_FULL = 3,
    NETWORK_MANAGER_FAULT_INTERNAL = 4,
    NETWORK_MANAGER_FAULT_NVS_INIT_FAILED = 10,
    NETWORK_MANAGER_FAULT_NETIF_INIT_FAILED = 11,
    NETWORK_MANAGER_FAULT_EVENT_LOOP_INIT_FAILED = 12,
    NETWORK_MANAGER_FAULT_EVENT_HANDLER_FAILED = 13,
    NETWORK_MANAGER_FAULT_TASK_CREATE_FAILED = 14,
    NETWORK_MANAGER_FAULT_START_BARRIER_TIMEOUT = 15,
    NETWORK_MANAGER_FAULT_WIFI_CONFIG_MISSING = 20,
    NETWORK_MANAGER_FAULT_WIFI_CONFIG_INVALID = 21,
    NETWORK_MANAGER_FAULT_WIFI_CONFIG_READ_FAILED = 22,
    NETWORK_MANAGER_FAULT_WIFI_CONFIG_WRITE_FAILED = 23,
    NETWORK_MANAGER_FAULT_WIFI_CONFIG_CLEAR_FAILED = 24,
    NETWORK_MANAGER_FAULT_WIFI_CONFIG_APPLY_FAILED = 25,
    NETWORK_MANAGER_FAULT_WIFI_NETIF_CREATE_FAILED = 30,
    NETWORK_MANAGER_FAULT_WIFI_INIT_FAILED = 31,
    NETWORK_MANAGER_FAULT_WIFI_MODE_FAILED = 32,
    NETWORK_MANAGER_FAULT_WIFI_START_FAILED = 33,
    NETWORK_MANAGER_FAULT_WIFI_CONNECT_FAILED = 34,
    NETWORK_MANAGER_FAULT_NET_MGMT_EVENT_FAILED = 35,
    NETWORK_MANAGER_FAULT_WIFI_AUTH_FAILED = 40,
    NETWORK_MANAGER_FAULT_WIFI_AP_NOT_FOUND = 41,
    NETWORK_MANAGER_FAULT_WIFI_HANDSHAKE_TIMEOUT = 42,
    NETWORK_MANAGER_FAULT_WIFI_BEACON_TIMEOUT = 43,
    NETWORK_MANAGER_FAULT_WIFI_DISCONNECTED = 44,
    NETWORK_MANAGER_FAULT_WIFI_IP_LOST = 45,
    NETWORK_MANAGER_FAULT_WIFI_RETRY_EXHAUSTED = 46,
    NETWORK_MANAGER_FAULT_LTE_INIT_FAILED = 50,
    NETWORK_MANAGER_FAULT_LTE_POWER_ON_FAILED = 51,
    NETWORK_MANAGER_FAULT_LTE_POWER_OFF_FAILED = 52,
    NETWORK_MANAGER_FAULT_MANAGER_INIT_FAILED = 53,
    NETWORK_MANAGER_FAULT_CELLULAR_LINK_LOST = 54,
    NETWORK_MANAGER_FAULT_CELLULAR_IP_LOST = 55,
    NETWORK_MANAGER_FAULT_CELLULAR_INITIAL_IPV4_TIMEOUT = 56,
    NETWORK_MANAGER_FAULT_CELLULAR_RECOVERY_TIMEOUT = 57,
    NETWORK_MANAGER_FAULT_CELLULAR_RETRY_EXHAUSTED = 58,
    NETWORK_MANAGER_FAULT_INTERNET_UNREACHABLE = 60,
    NETWORK_MANAGER_FAULT_ALL_RETRY_EXHAUSTED = 61,
    NETWORK_MANAGER_FAULT_EVENT_STREAM_OVERFLOW = 62,
} network_manager_fault_code_t;

/**
 * Length-delimited WPA/WPA2 Personal credentials.
 *
 * ssid_len must be 1..32. password_len must be 8..63 for a printable
 * passphrase or exactly 64 for a hexadecimal PSK. Unused bytes are ignored.
 */
typedef struct {
    uint8_t ssid[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES]; /**< Opaque SSID bytes. */
    uint8_t password[NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES]; /**< Credential bytes. */
    uint8_t ssid_len;     /**< Number of valid bytes in ssid. */
    uint8_t password_len; /**< Number of valid bytes in password. */
} network_manager_wifi_config_t;

/** One length-delimited access point returned by a Wi-Fi scan. */
typedef struct {
    uint8_t ssid[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES];
    uint8_t ssid_len;
    int8_t rssi;
    bool secure;
} network_manager_wifi_scan_entry_t;

/** Latest copied Wi-Fi scan result, ordered by RSSI then SSID bytes. */
typedef struct {
    network_manager_wifi_scan_entry_t
        entries[NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS];
    size_t count;
    uint16_t raw_count;
    uint32_t revision;
    uint32_t operation_id;
    esp_err_t result;
} network_manager_wifi_scan_list_t;

/** Per-interface state included in network_manager_snapshot_t. */
typedef struct {
    bool enabled;            /**< The locked mode enables this path. */
    bool raw_link_up;        /**< Latest physical/link-layer observation. */
    bool raw_ipv4_ready;     /**< Latest IPv4 observation. */
    bool stable_link_up;     /**< Debounced link state. */
    bool stable_ipv4_ready;  /**< Debounced IPv4 state. */
    uint8_t retry_attempt;   /**< Attempts consumed in the current budget. */
    uint8_t retry_limit;     /**< Maximum attempts in the current budget. */
    bool retry_exhausted;    /**< Automatic recovery is stopped for this path. */
    int32_t last_raw_reason; /**< Latest driver reason, or zero if unavailable. */
} network_manager_interface_snapshot_t;

/** One occurrence in the bounded fault history. */
typedef struct {
    uint32_t sequence; /**< Nonzero sequence assigned when this record was added. */
    uint32_t occurrence_count; /**< Count for the same code and interface. */
    network_manager_interface_t interface; /**< Interface associated with the fault. */
    network_manager_fault_code_t code; /**< Stable component fault category. */
    esp_err_t source_error; /**< Unmodified underlying ESP-IDF/adapter error. */
    int32_t raw_reason; /**< Driver reason value when one exists. */
    bool active; /**< False after a matching recovery has been observed. */
} network_manager_fault_record_t;

/** Bounded fault history returned oldest-to-newest in records[0..count). */
typedef struct {
    network_manager_fault_record_t
        records[NETWORK_MANAGER_FAULT_HISTORY_CAPACITY];
    size_t count;              /**< Number of valid records. */
    uint32_t next_sequence;    /**< Internal next nonzero sequence value. */
    uint32_t overwritten_count; /**< Saturating count of evicted old records. */
} network_manager_fault_history_t;

/** Authoritative component state; events carry a copy of this structure. */
typedef struct {
    network_manager_lifecycle_t lifecycle;
    network_manager_mode_t mode; /**< Startup-locked mode. */
    network_manager_wifi_phase_t wifi_phase;
    network_manager_4g_phase_t cellular_phase;
    network_manager_interface_t raw_active_interface;
    network_manager_interface_t stable_active_interface;
    network_manager_interface_snapshot_t wifi;
    network_manager_interface_snapshot_t cellular;
    bool internet_reachable; /**< Global observation provided by net_mgmt. */
    bool raw_ready; /**< Active raw path has link, IPv4, and internet. */
    bool stable_ready; /**< Debounced application-facing readiness. */
    bool interface_switch_in_progress;
    bool reconnect_in_progress;
    bool disconnect_in_progress;
    bool manual_offline; /**< Explicit full-offline state; not persisted. */
    bool all_retry_exhausted;
    bool wifi_scan_in_progress;
    uint32_t wifi_scan_revision;
    bool current_wifi_config_present;
    bool persisted_wifi_config_present;
    uint32_t wifi_config_revision; /**< Increments on current-config changes. */
    uint32_t revision; /**< Nonzero revision of public state. */
    uint32_t event_sequence; /**< Most recently published event sequence. */
    uint32_t event_overflow_count;
    uint32_t fault_overwrite_count;
    network_manager_fault_code_t active_fault; /**< Latest active fault code. */
    network_manager_fault_record_t last_fault; /**< Latest fault/recovery record. */
} network_manager_snapshot_t;

/** Event categories delivered by the dispatcher task. */
typedef enum {
    NETWORK_MANAGER_EVENT_SNAPSHOT_SYNC = 0,
    NETWORK_MANAGER_EVENT_START_RESULT = 1,
    NETWORK_MANAGER_EVENT_MODE_SELECTED = 2,
    NETWORK_MANAGER_EVENT_WIFI_CONFIG_RESULT = 3,
    NETWORK_MANAGER_EVENT_RAW_STATE_CHANGED = 4,
    NETWORK_MANAGER_EVENT_STABLE_STATE_CHANGED = 5,
    NETWORK_MANAGER_EVENT_INTERFACE_CANDIDATE_CHANGED = 6,
    NETWORK_MANAGER_EVENT_ACTIVE_INTERFACE_CHANGED = 7,
    NETWORK_MANAGER_EVENT_READY_CHANGED = 8,
    NETWORK_MANAGER_EVENT_FAULT = 9,
    NETWORK_MANAGER_EVENT_RECOVERY_STARTED = 10,
    NETWORK_MANAGER_EVENT_RECOVERY_SUCCEEDED = 11,
    NETWORK_MANAGER_EVENT_INTERFACE_RETRY_EXHAUSTED = 12,
    NETWORK_MANAGER_EVENT_ALL_RETRY_EXHAUSTED = 13,
    NETWORK_MANAGER_EVENT_RECONNECT_STATUS = 14,
    NETWORK_MANAGER_EVENT_STREAM_OVERFLOW = 15,
    NETWORK_MANAGER_EVENT_DISCONNECT_STATUS = 16,
    NETWORK_MANAGER_EVENT_WIFI_SCAN_RESULT = 17,
} network_manager_event_type_t;

/** Status carried by asynchronous operation-result events. */
typedef enum {
    NETWORK_MANAGER_OPERATION_ACCEPTED = 0, /**< Worker accepted the operation. */
    NETWORK_MANAGER_OPERATION_ALREADY_IN_PROGRESS = 1, /**< No duplicate flow started. */
    NETWORK_MANAGER_OPERATION_NO_ACTION = 2, /**< Requested state already holds. */
    NETWORK_MANAGER_OPERATION_COMPLETED = 3, /**< Operation reached its success condition. */
    NETWORK_MANAGER_OPERATION_FAILED = 4, /**< Operation reached a terminal failure. */
} network_manager_operation_status_t;

/** Wi-Fi configuration stage reported by WIFI_CONFIG_RESULT events. */
typedef enum {
    NETWORK_MANAGER_WIFI_CONFIG_LOAD = 0,
    NETWORK_MANAGER_WIFI_CONFIG_APPLY = 1,
    NETWORK_MANAGER_WIFI_CONFIG_SAVE = 2,
    NETWORK_MANAGER_WIFI_CONFIG_CLEAR = 3,
} network_manager_wifi_config_action_t;

/**
 * Dispatcher event.
 *
 * operation_status is meaningful for START_RESULT, WIFI_CONFIG_RESULT,
 * WIFI_SCAN_RESULT, RECONNECT_STATUS, and DISCONNECT_STATUS. Wi-Fi result fields are meaningful
 * only for WIFI_CONFIG_RESULT. fault is meaningful for FAULT and
 * RECOVERY_SUCCEEDED. snapshot is always the committed state at publication
 * time.
 */
typedef struct {
    uint32_t sequence;    /**< Nonzero ordered event sequence. */
    uint32_t operation_id; /**< Correlates an asynchronous mutating request. */
    network_manager_event_type_t type;
    network_manager_operation_status_t operation_status;
    network_manager_interface_t interface;
    network_manager_wifi_config_action_t wifi_config_action;
    bool current_config_applied;
    bool persistence_requested;
    bool persisted_config_saved;
    network_manager_fault_record_t fault;
    network_manager_snapshot_t snapshot;
} network_manager_event_t;

/**
 * Event callback invoked by the component dispatcher task.
 *
 * The event pointer is valid only for the duration of the callback. Copy it if
 * it must outlive the call. The callback may use queries and non-blocking
 * disconnect/reconnect requests, but must not block for network progress.
 */
typedef void (*network_manager_event_cb_t)(
    const network_manager_event_t *event,
    void *user_ctx);

/**
 * Override the Kconfig default mode for the current boot.
 *
 * @param mode One of the three defined network_manager_mode_t values.
 * @retval ESP_OK Mode selected.
 * @retval ESP_ERR_INVALID_ARG mode is not valid.
 * @retval ESP_ERR_INVALID_STATE Start has already been accepted.
 */
esp_err_t network_manager_set_mode(network_manager_mode_t mode);

/**
 * Get the current default, override, or startup-locked mode.
 *
 * @param[out] mode Receives the current mode.
 * @retval ESP_OK Mode copied.
 * @retval ESP_ERR_INVALID_ARG mode is NULL.
 */
esp_err_t network_manager_get_mode(network_manager_mode_t *mode);

/**
 * Initialize component resources and lock the selected mode.
 *
 * For a cellular mode, this call waits at most the build-time configured
 * initial-attempt timeout for worker-owned LTE power-on and the sole manager
 * initialization attempt. A timeout records START_BARRIER_TIMEOUT but returns
 * ESP_OK without cancelling the worker; lifecycle remains STARTING until the
 * worker publishes START_RESULT. Repeated calls while STARTING or RUNNING are
 * idempotent. Link, IPv4, internet readiness, and asynchronous startup failure
 * must be observed through snapshots or events.
 *
 * @retval ESP_OK Start was accepted or the component is already started.
 * @retval ESP_ERR_INVALID_STATE A previous start reached START_FAILED.
 * @retval ESP_ERR_NO_MEM A queue or task could not be created.
 * @return Another ESP-IDF/adapter error if synchronous setup fails.
 */
esp_err_t network_manager_start(void);

/**
 * Return the current stable_ready value. This query is valid before start.
 *
 * @return true only when the committed snapshot is stably network-ready.
 */
bool network_manager_is_ready(void);

/**
 * Copy the authoritative snapshot. This query is valid before start.
 *
 * @param[out] snapshot Receives an atomic copy of component state.
 * @retval ESP_OK Snapshot copied.
 * @retval ESP_ERR_INVALID_ARG snapshot is NULL.
 */
esp_err_t network_manager_get_snapshot(network_manager_snapshot_t *snapshot);

/**
 * Validate, copy, and asynchronously apply a Wi-Fi configuration.
 *
 * @param config Length-delimited credentials; copied before return.
 * @param persist If true, also save the configuration transactionally.
 * @param operation_id Receives a nonzero ID only when the command is queued.
 * @note A valid current config is accepted even if later apply or persistence
 *       fails; WIFI_CONFIG_RESULT exposes both outcomes independently.
 * @retval ESP_OK Command queued; observe WIFI_CONFIG_RESULT.
 * @retval ESP_ERR_INVALID_ARG A pointer or credential field is invalid.
 * @retval ESP_ERR_INVALID_STATE Start has not been accepted or has failed.
 * @retval ESP_ERR_TIMEOUT The command queue is full.
 */
esp_err_t network_manager_wifi_set_config(
    const network_manager_wifi_config_t *config,
    bool persist,
    uint32_t *operation_id);

/**
 * Copy the current in-memory Wi-Fi configuration.
 *
 * @param[out] config Receives the current credentials.
 * @retval ESP_OK Configuration copied.
 * @retval ESP_ERR_INVALID_ARG config is NULL.
 * @retval ESP_ERR_INVALID_STATE Start has not been accepted or has failed.
 * @retval ESP_ERR_NOT_FOUND No current configuration exists.
 */
esp_err_t network_manager_wifi_get_current_config(
    network_manager_wifi_config_t *config);

/**
 * Copy the Wi-Fi configuration loaded from or saved to component NVS.
 *
 * @param[out] config Receives the persisted credentials.
 * @retval ESP_OK Configuration copied.
 * @retval ESP_ERR_INVALID_ARG config is NULL.
 * @retval ESP_ERR_INVALID_STATE Start has not been accepted or has failed.
 * @retval ESP_ERR_NOT_FOUND No persisted configuration exists.
 */
esp_err_t network_manager_wifi_get_persisted_config(
    network_manager_wifi_config_t *config);

/**
 * Asynchronously start one Wi-Fi station scan.
 *
 * The component owns all esp_wifi_scan_* calls. Completion is reported by a
 * WIFI_SCAN_RESULT event; copy the result with
 * network_manager_wifi_scan_get_latest().
 *
 * @param[out] operation_id Receives a nonzero ID only when queued.
 * @retval ESP_OK Scan command queued.
 * @retval ESP_ERR_INVALID_ARG operation_id is NULL.
 * @retval ESP_ERR_INVALID_STATE Wi-Fi is disabled, startup is incomplete, or
 *         a scan is already queued/running.
 * @retval ESP_ERR_TIMEOUT The command queue is full.
 */
esp_err_t network_manager_wifi_scan_start(uint32_t *operation_id);

/**
 * Copy the most recently completed Wi-Fi scan result.
 *
 * SSIDs are length-delimited opaque bytes. Entries are deduplicated by exact
 * SSID bytes, retain the strongest RSSI, and are ordered by descending RSSI
 * then ascending SSID bytes.
 *
 * @param[out] list Receives an atomic copy of the latest result.
 * @retval ESP_OK A completed result was copied; inspect list->result.
 * @retval ESP_ERR_INVALID_ARG list is NULL.
 * @retval ESP_ERR_NOT_FOUND No scan has completed yet.
 */
esp_err_t network_manager_wifi_scan_get_latest(
    network_manager_wifi_scan_list_t *list);

/**
 * Asynchronously erase only the component's persisted Wi-Fi keys.
 *
 * The current in-memory configuration and active connection are unchanged.
 *
 * @param[out] operation_id Receives a nonzero ID only when queued.
 * @retval ESP_OK Command queued; observe WIFI_CONFIG_RESULT.
 * @retval ESP_ERR_INVALID_ARG operation_id is NULL.
 * @retval ESP_ERR_INVALID_STATE Start has not been accepted or has failed.
 * @retval ESP_ERR_TIMEOUT The command queue is full.
 */
esp_err_t network_manager_wifi_clear_persisted_config(
    uint32_t *operation_id);

/**
 * Register an event callback, including before start.
 *
 * A SNAPSHOT_SYNC event is queued after registration. The same callback and
 * context may be registered more than once as independent subscriptions.
 *
 * @param callback Function invoked on the dispatcher task.
 * @param user_ctx Opaque value passed back to callback; may be NULL.
 * @param[out] subscription_id Receives a nonzero subscription ID.
 * @retval ESP_OK Subscription created.
 * @retval ESP_ERR_INVALID_ARG callback or subscription_id is NULL.
 * @retval ESP_ERR_NO_MEM All subscription slots are occupied.
 */
esp_err_t network_manager_subscribe(
    network_manager_event_cb_t callback,
    void *user_ctx,
    uint32_t *subscription_id);

/**
 * Remove a subscription.
 *
 * A callback already copied by the dispatcher may finish after this returns.
 *
 * @param subscription_id Nonzero ID returned by network_manager_subscribe().
 * @retval ESP_OK Subscription removed.
 * @retval ESP_ERR_NOT_FOUND subscription_id is zero or unknown.
 */
esp_err_t network_manager_unsubscribe(uint32_t subscription_id);

/**
 * Copy fault history in oldest-to-newest order.
 *
 * An empty history is returned as ESP_OK with count equal to zero.
 *
 * @param[out] history Receives a complete bounded history copy.
 * @retval ESP_OK History copied.
 * @retval ESP_ERR_INVALID_ARG history is NULL.
 */
esp_err_t network_manager_get_fault_history(
    network_manager_fault_history_t *history);

/**
 * Request complete manual offline for every path enabled by the locked mode.
 *
 * A duplicate disconnect request returns a new ID and reports
 * ALREADY_IN_PROGRESS without starting another flow. An opposite reconnect
 * operation causes ESP_ERR_INVALID_STATE and is never queued.
 *
 * @retval ESP_OK Request queued or duplicate status scheduled.
 * @retval ESP_ERR_INVALID_ARG operation_id is NULL.
 * @retval ESP_ERR_INVALID_STATE Not started, start failed, or reconnect active.
 * @retval ESP_ERR_TIMEOUT The command queue is full.
 */
esp_err_t network_manager_request_disconnect(uint32_t *operation_id);

/**
 * Reinitialize connection state and rebuild every path enabled by the mode.
 *
 * The operation reapplies the current in-memory Wi-Fi configuration, resets
 * retry budgets, and performs LTE power off/on without manager deinit/re-init.
 * A duplicate reconnect reports ALREADY_IN_PROGRESS; an active disconnect is
 * rejected with ESP_ERR_INVALID_STATE.
 *
 * @param[out] operation_id Receives a nonzero ID when accepted.
 * @retval ESP_OK Request queued or duplicate status scheduled.
 * @retval ESP_ERR_INVALID_ARG operation_id is NULL.
 * @retval ESP_ERR_INVALID_STATE Not started, start failed, or disconnect active.
 * @retval ESP_ERR_TIMEOUT The command queue is full.
 */
esp_err_t network_manager_request_reconnect(uint32_t *operation_id);


#ifdef __cplusplus
}
#endif
