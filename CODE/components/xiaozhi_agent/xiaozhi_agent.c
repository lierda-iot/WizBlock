#include "xiaozhi_agent.h"
#include "xiaozhi_agent_listen_mode_policy.h"
#include "xiaozhi_agent_tts_barrier_policy.h"
#include "xiaozhi_agent_vad_stop_policy.h"
#include "xiaozhi_agent_ws_start_policy.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

static const char *TAG = "xiaozhi_agent";

#define WS_BUFFER_SIZE          15032
#define WS_TASK_STACK_SIZE      (4U * 1024U)
#define WS_START_RETRY_DELAY_MS 100U
#define JSON_BUF_SIZE           512
#define MIN_TX_FRAMES_BEFORE_VAD_END  XIAOZHI_AGENT_VAD_MIN_TX_FRAMES
#define AGENT_TASK_STACK        (16 * 1024)
#define AGENT_TASK_PRIO         4
#define WS_CONNECT_TIMEOUT_MS   15000
#define HELLO_TIMEOUT_MS        10000
#define RETRY_BASE_MS           1000
#define RETRY_MAX_MS            30000
#define PROTO_VERSION           3
#define PROTO_HDR_SIZE          4
#define PROCESSING_TIMEOUT_MS   15000
#define AUDIO_TX_FAIL_CLOSE_THRESHOLD  3
#define AGENT_STOP_DEFAULT_TIMEOUT_MS 3000U
#define AGENT_STOP_POLL_MS 10U
#define VERSION_CHECK_TIMEOUT_MS 2000U
#define AGENT_EVENT_QUEUE_LENGTH 16U
#define AGENT_CANCEL_REQUEST_CAPACITY (AGENT_EVENT_QUEUE_LENGTH + 1U)

typedef enum {
    EVT_WAKE_WORD = 0,
    EVT_VAD_START,
    EVT_VAD_END,
    EVT_WS_CONNECTED,
    EVT_WS_DISCONNECTED,
    EVT_WS_TEXT,
    EVT_TX_PROGRESS,
    EVT_TX_ERROR,
    EVT_CANCEL_REQUEST,
} agent_evt_type_t;

typedef struct {
    agent_evt_type_t type;
    uint8_t *data;
    int len;
    uint32_t session_epoch;
    uint32_t request_id;
} agent_evt_t;

typedef struct {
    uint32_t session_epoch;
    uint32_t request_id;
} ws_callback_binding_t;

static xiaozhi_agent_config_t s_config;
static esp_websocket_client_handle_t s_ws_client;
static ws_callback_binding_t *s_ws_binding;
static SemaphoreHandle_t s_ws_lock;
static volatile xiaozhi_agent_state_t s_state = XIAOZHI_STATE_IDLE;
static char s_ws_url[256];
static char s_ws_token[256];
static char s_session_id[64];
static QueueHandle_t s_evt_queue;
static TaskHandle_t s_agent_task;
static volatile bool s_initialized;
static bool s_initializing;
static volatile bool s_running;
static volatile bool s_worker_active;
static bool s_starting;
static bool s_stop_requested;
static bool s_critical_event_failed;
static uint32_t s_critical_failure_session_epoch;
static uint32_t s_critical_failure_request_id;
static volatile uint32_t s_session_epoch;
static volatile uint32_t s_active_request_id;
static uint32_t s_cancel_request_ids[AGENT_CANCEL_REQUEST_CAPACITY];
static uint32_t s_legacy_request_id;
static volatile bool s_vad_stop_pending;
static volatile bool s_vad_stop_sent;
static xiaozhi_agent_vad_stop_policy_t s_vad_stop_policy;
static volatile uint32_t s_tx_frame_count;
static volatile uint32_t s_tx_fail_count;
static volatile uint32_t s_tx_consecutive_fail_count;
static volatile uint32_t s_tx_min_opus_len;
static volatile uint32_t s_tx_max_opus_len;
static volatile uint32_t s_rx_audio_ignored_count;
static xiaozhi_agent_tts_barrier_t s_tts_barrier;
static uint32_t s_tts_barrier_text_drop_count;
static uint32_t s_tts_barrier_audio_drop_count;
static TickType_t s_processing_start_tick;
static portMUX_TYPE s_lifecycle_lock = portMUX_INITIALIZER_UNLOCKED;

static xiaozhi_agent_tts_barrier_t tts_barrier_snapshot(void)
{
    xiaozhi_agent_tts_barrier_t snapshot = {0};
    portENTER_CRITICAL(&s_lifecycle_lock);
    snapshot = s_tts_barrier;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return snapshot;
}

static void reset_tts_barrier(uint32_t session_epoch, uint32_t request_id)
{
    portENTER_CRITICAL(&s_lifecycle_lock);
    xiaozhi_agent_tts_barrier_reset(&s_tts_barrier, session_epoch,
                                    request_id);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    ESP_LOGI(TAG,
             "[DEBUG-AI-P0] phase=barrier_reset epoch=%lu request=%lu valid=%u",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (0U != session_epoch && 0U != request_id) ? 1U : 0U);
}

static void mark_tts_barrier_ready(uint32_t session_epoch,
                                   uint32_t request_id)
{
    xiaozhi_agent_tts_barrier_t snapshot = {0};
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (session_epoch == s_tts_barrier.session_epoch &&
        request_id == s_tts_barrier.request_id) {
        xiaozhi_agent_tts_barrier_on_listening_ready(&s_tts_barrier);
    }
    snapshot = s_tts_barrier;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    ESP_LOGI(TAG,
             "[DEBUG-AI-P0] phase=barrier_ready epoch=%lu request=%lu matched=%u ready=%u stop=%u evidence=%u",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (session_epoch == snapshot.session_epoch &&
              request_id == snapshot.request_id) ? 1U : 0U,
             snapshot.listening_ready ? 1U : 0U,
             snapshot.listen_stop_sent ? 1U : 0U,
             snapshot.processing_evidence ? 1U : 0U);
}

static void mark_tts_barrier_listen_stop(uint32_t session_epoch,
                                         uint32_t request_id)
{
    xiaozhi_agent_tts_barrier_t snapshot = {0};
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (session_epoch == s_tts_barrier.session_epoch &&
        request_id == s_tts_barrier.request_id) {
        xiaozhi_agent_tts_barrier_on_listen_stop(&s_tts_barrier);
    }
    snapshot = s_tts_barrier;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    ESP_LOGI(TAG,
             "[DEBUG-AI-P0] phase=barrier_stop epoch=%lu request=%lu matched=%u ready=%u stop=%u evidence=%u",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (session_epoch == snapshot.session_epoch &&
              request_id == snapshot.request_id) ? 1U : 0U,
             snapshot.listening_ready ? 1U : 0U,
             snapshot.listen_stop_sent ? 1U : 0U,
             snapshot.processing_evidence ? 1U : 0U);
}

static void mark_tts_barrier_processing_evidence(uint32_t session_epoch,
                                                 uint32_t request_id,
                                                 const char *source)
{
    xiaozhi_agent_tts_barrier_t snapshot = {0};
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (session_epoch == s_tts_barrier.session_epoch &&
        request_id == s_tts_barrier.request_id) {
        xiaozhi_agent_tts_barrier_on_processing_evidence(&s_tts_barrier);
    }
    snapshot = s_tts_barrier;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    const bool open = xiaozhi_agent_tts_barrier_accepts(
        &snapshot, session_epoch, request_id);
    ESP_LOGI(TAG,
             "[DEBUG-AI-P0] phase=barrier_evidence epoch=%lu request=%lu source=%s open=%u ready=%u stop=%u evidence=%u",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (NULL != source) ? source : "?", open ? 1U : 0U,
             snapshot.listening_ready ? 1U : 0U,
             snapshot.listen_stop_sent ? 1U : 0U,
             snapshot.processing_evidence ? 1U : 0U);
}

static bool tts_barrier_accepts(uint32_t session_epoch,
                                uint32_t request_id,
                                xiaozhi_agent_tts_barrier_t *snapshot)
{
    const xiaozhi_agent_tts_barrier_t current = tts_barrier_snapshot();
    if (NULL != snapshot) {
        *snapshot = current;
    }
    return xiaozhi_agent_tts_barrier_accepts(
        &current, session_epoch, request_id);
}

static uint32_t count_tts_barrier_drop(bool audio)
{
    uint32_t count = 0U;
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (audio) {
        s_tts_barrier_audio_drop_count++;
        count = s_tts_barrier_audio_drop_count;
    } else {
        s_tts_barrier_text_drop_count++;
        count = s_tts_barrier_text_drop_count;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return count;
}

static void mark_critical_event_failed(uint32_t session_epoch,
                                       uint32_t request_id)
{
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_critical_event_failed = true;
    s_critical_failure_session_epoch = session_epoch;
    s_critical_failure_request_id = request_id;
    portEXIT_CRITICAL(&s_lifecycle_lock);
}

static bool take_critical_event_failure(void)
{
    bool failed = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    failed = s_critical_event_failed;
    s_critical_event_failed = false;
    const bool current = failed &&
        s_critical_failure_session_epoch == s_session_epoch &&
        s_critical_failure_request_id == s_active_request_id;
    s_critical_failure_session_epoch = 0U;
    s_critical_failure_request_id = 0U;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return current;
}

static uint32_t next_nonzero(uint32_t value)
{
    value++;
    return (0U == value) ? 1U : value;
}

static void set_active_request_id(uint32_t request_id)
{
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_active_request_id = request_id;
    portEXIT_CRITICAL(&s_lifecycle_lock);
}

static uint32_t active_request_id_snapshot(void)
{
    uint32_t request_id = 0U;
    portENTER_CRITICAL(&s_lifecycle_lock);
    request_id = s_active_request_id;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return request_id;
}

static bool request_cancelled_locked(uint32_t request_id)
{
    if (0U == request_id) {
        return false;
    }
    for (size_t index = 0U; index < AGENT_CANCEL_REQUEST_CAPACITY; index++) {
        if (request_id == s_cancel_request_ids[index]) {
            return true;
        }
    }
    return false;
}

static bool cancel_requested(uint32_t request_id)
{
    bool requested = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    requested = request_cancelled_locked(request_id);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return requested;
}

static bool request_is_current_and_live(uint32_t session_epoch,
                                        uint32_t request_id)
{
    bool current = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    current = 0U != session_epoch && 0U != request_id &&
              session_epoch == s_session_epoch &&
              request_id == s_active_request_id &&
              s_running &&
              !request_cancelled_locked(request_id);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return current;
}

static bool request_is_current(uint32_t session_epoch, uint32_t request_id)
{
    bool current = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    current = 0U != session_epoch && 0U != request_id &&
              session_epoch == s_session_epoch &&
              request_id == s_active_request_id;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return current;
}

static bool snapshot_live_request(uint32_t session_epoch,
                                  uint32_t *request_id)
{
    bool live = false;
    uint32_t current_request_id = 0U;
    portENTER_CRITICAL(&s_lifecycle_lock);
    current_request_id = s_active_request_id;
    live = 0U != session_epoch && 0U != current_request_id &&
           session_epoch == s_session_epoch && s_running &&
           !request_cancelled_locked(current_request_id);
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (NULL != request_id) {
        *request_id = current_request_id;
    }
    return live;
}

static bool refresh_live_request(uint32_t session_epoch, uint32_t *request_id,
                                 const char *source)
{
    if (NULL == request_id) {
        return false;
    }
    uint32_t current_request_id = 0U;
    if (!snapshot_live_request(session_epoch, &current_request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=open_request_drop source=%s epoch=%lu request=%lu current=%lu",
                 (NULL != source) ? source : "?",
                 (unsigned long)session_epoch,
                 (unsigned long)*request_id,
                 (unsigned long)current_request_id);
        return false;
    }
    if (*request_id != current_request_id) {
        ESP_LOGI(TAG,
                 "[DEBUG-AI-P0] phase=open_request_refresh source=%s epoch=%lu request=%lu->%lu",
                 (NULL != source) ? source : "?",
                 (unsigned long)session_epoch,
                 (unsigned long)*request_id,
                 (unsigned long)current_request_id);
        *request_id = current_request_id;
    }
    return true;
}

static bool activate_request_if_live(uint32_t request_id, const char *source)
{
    bool accepted = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_running && 0U != request_id &&
        !request_cancelled_locked(request_id)) {
        s_active_request_id = request_id;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (!accepted) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=request_activate_drop source=%s request=%lu current=%lu running=%u cancelled=%u",
                 (NULL != source) ? source : "?",
                 (unsigned long)request_id,
                 (unsigned long)active_request_id_snapshot(),
                 s_running ? 1U : 0U,
                 cancel_requested(request_id) ? 1U : 0U);
    }
    return accepted;
}

static bool rebind_request_if_current(uint32_t session_epoch,
                                      uint32_t previous_request_id,
                                      uint32_t request_id,
                                      const char *source)
{
    bool accepted = false;
    uint32_t actual_session_epoch = 0U;
    uint32_t actual_request_id = 0U;
    portENTER_CRITICAL(&s_lifecycle_lock);
    actual_session_epoch = s_session_epoch;
    actual_request_id = s_active_request_id;
    if (s_running && 0U != session_epoch && 0U != request_id &&
        request_id != previous_request_id &&
        session_epoch == actual_session_epoch &&
        previous_request_id == actual_request_id &&
        !request_cancelled_locked(request_id)) {
        s_active_request_id = request_id;
        if (NULL != s_ws_binding) {
            s_ws_binding->request_id = request_id;
        }
        accepted = true;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (!accepted) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=request_rebind_drop source=%s epoch=%lu/%lu old_request=%lu/%lu new_request=%lu cancelled=%u",
                 (NULL != source) ? source : "?",
                 (unsigned long)session_epoch,
                 (unsigned long)actual_session_epoch,
                 (unsigned long)previous_request_id,
                 (unsigned long)actual_request_id,
                 (unsigned long)request_id,
                 cancel_requested(request_id) ? 1U : 0U);
    }
    return accepted;
}

static bool set_cancel_request(uint32_t request_id)
{
    bool stored = false;
    size_t free_index = AGENT_CANCEL_REQUEST_CAPACITY;
    portENTER_CRITICAL(&s_lifecycle_lock);
    for (size_t index = 0U; index < AGENT_CANCEL_REQUEST_CAPACITY; index++) {
        if (request_id == s_cancel_request_ids[index]) {
            stored = true;
            break;
        }
        if (0U == s_cancel_request_ids[index] &&
            AGENT_CANCEL_REQUEST_CAPACITY == free_index) {
            free_index = index;
        }
    }
    if (!stored && AGENT_CANCEL_REQUEST_CAPACITY != free_index) {
        s_cancel_request_ids[free_index] = request_id;
        stored = true;
    }
    if (!stored && request_id == s_active_request_id) {
        for (size_t index = 0U; index < AGENT_CANCEL_REQUEST_CAPACITY; index++) {
            if (s_active_request_id != s_cancel_request_ids[index]) {
                s_cancel_request_ids[index] = request_id;
                stored = true;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return stored;
}

static void clear_cancel_request(uint32_t request_id)
{
    portENTER_CRITICAL(&s_lifecycle_lock);
    for (size_t index = 0U; index < AGENT_CANCEL_REQUEST_CAPACITY; index++) {
        if (request_id == s_cancel_request_ids[index]) {
            s_cancel_request_ids[index] = 0U;
            break;
        }
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
}

static void notify_semantic(xiaozhi_agent_event_type_t type,
                            uint32_t session_epoch, uint32_t request_id,
                            esp_err_t result)
{
    if (NULL == s_config.on_event) {
        return;
    }
    const xiaozhi_agent_event_t event = {
        .type = type,
        .raw_state = s_state,
        .session_epoch = session_epoch,
        .request_id = request_id,
        .result = result,
    };
    s_config.on_event(&event, s_config.user_ctx);
}

static void set_state(xiaozhi_agent_state_t new_state)
{
    xiaozhi_agent_state_t previous_state = XIAOZHI_STATE_IDLE;
    bool changed = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    previous_state = s_state;
    if (previous_state != new_state) {
        s_state = new_state;
        changed = true;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (!changed) {
        return;
    }
    ESP_LOGI(TAG, "state: %d -> %d", (int)previous_state,
             (int)new_state);
    if (NULL != s_config.on_state_change) {
        s_config.on_state_change(new_state, s_config.user_ctx);
    }
}

static bool set_state_if_current(xiaozhi_agent_state_t expected_state,
                                 xiaozhi_agent_state_t new_state,
                                 uint32_t session_epoch,
                                 uint32_t request_id,
                                 const char *source)
{
    bool accepted = false;
    bool changed = false;
    xiaozhi_agent_state_t actual_state = XIAOZHI_STATE_IDLE;
    uint32_t actual_session_epoch = 0U;
    uint32_t actual_request_id = 0U;
    portENTER_CRITICAL(&s_lifecycle_lock);
    actual_state = s_state;
    actual_session_epoch = s_session_epoch;
    actual_request_id = s_active_request_id;
    if (0U != session_epoch && 0U != request_id &&
        session_epoch == actual_session_epoch &&
        request_id == actual_request_id &&
        s_running &&
        expected_state == s_state &&
        !request_cancelled_locked(request_id)) {
        accepted = true;
        changed = new_state != s_state;
        s_state = new_state;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (!accepted) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=state_commit_drop source=%s epoch=%lu/%lu request=%lu/%lu expected=%d actual=%d next=%d",
                 (NULL != source) ? source : "?",
                 (unsigned long)session_epoch,
                 (unsigned long)actual_session_epoch,
                 (unsigned long)request_id,
                 (unsigned long)actual_request_id,
                 (int)expected_state, (int)actual_state, (int)new_state);
        return false;
    }
    if (changed) {
        ESP_LOGI(TAG, "state: %d -> %d", (int)actual_state,
                 (int)new_state);
        if (NULL != s_config.on_state_change) {
            s_config.on_state_change(new_state, s_config.user_ctx);
        }
    }
    return true;
}

static void notify_audio_play(const uint8_t *data, int len,
                              uint32_t session_epoch, uint32_t request_id)
{
    if (NULL != s_config.on_audio_play) {
        s_config.on_audio_play(data, len, s_config.user_ctx);
    }
    if (NULL != s_config.on_audio_event) {
        const xiaozhi_audio_event_t event = {
            .type = XIAOZHI_AUDIO_EVENT_PLAY,
            .opus_data = data,
            .len = len,
            .session_epoch = session_epoch,
            .request_id = request_id,
        };
        s_config.on_audio_event(&event, s_config.user_ctx);
    }
}

static void notify_audio_stop(uint32_t session_epoch, uint32_t request_id)
{
    if (NULL != s_config.on_audio_stop) {
        s_config.on_audio_stop(s_config.user_ctx);
    }
    if (NULL != s_config.on_audio_event) {
        const xiaozhi_audio_event_t event = {
            .type = XIAOZHI_AUDIO_EVENT_STOP,
            .session_epoch = session_epoch,
            .request_id = request_id,
        };
        s_config.on_audio_event(&event, s_config.user_ctx);
    }
}

static void reset_tx_stats(void)
{
    s_tx_frame_count = 0;
    s_tx_fail_count = 0;
    s_tx_consecutive_fail_count = 0;
    s_tx_min_opus_len = 0;
    s_tx_max_opus_len = 0;
    s_vad_stop_pending = false;
    s_vad_stop_sent = false;
    xiaozhi_agent_vad_stop_policy_reset(&s_vad_stop_policy,
                                        s_active_request_id);
}

static esp_err_t post_evt(agent_evt_type_t type, const uint8_t *data, int len,
                          uint32_t session_epoch, uint32_t request_id,
                          bool critical)
{
    if (NULL == s_evt_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    agent_evt_t evt = {
        .type = type,
        .data = NULL,
        .len = 0,
        .session_epoch = session_epoch,
        .request_id = request_id,
    };
    if (NULL != data && len > 0) {
        evt.data = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
        if (NULL == evt.data) {
            if (critical && session_epoch == s_session_epoch &&
                request_id == s_active_request_id) {
                mark_critical_event_failed(session_epoch, request_id);
            }
            return ESP_ERR_NO_MEM;
        }
        memcpy(evt.data, data, (size_t)len);
        evt.len = len;
    }
    if (pdTRUE != xQueueSend(s_evt_queue, &evt, 0)) {
        ESP_LOGW(TAG, "Event queue full, dropping evt type=%d", (int)type);
        if (NULL != evt.data) {
            heap_caps_free(evt.data);
        }
        if (critical && session_epoch == s_session_epoch &&
            request_id == s_active_request_id) {
            mark_critical_event_failed(session_epoch, request_id);
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void discard_pending_events(void)
{
    if (NULL == s_evt_queue) {
        return;
    }
    agent_evt_t event = {0};
    while (pdTRUE == xQueueReceive(s_evt_queue, &event, 0U)) {
        if (NULL != event.data) {
            heap_caps_free(event.data);
        }
        event = (agent_evt_t){0};
    }
}

static void ws_binding_snapshot(const ws_callback_binding_t *binding,
                                uint32_t *session_epoch,
                                uint32_t *request_id)
{
    if (NULL == session_epoch || NULL == request_id) {
        return;
    }
    *session_epoch = 0U;
    *request_id = 0U;
    if (NULL == binding) {
        return;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    *session_epoch = binding->session_epoch;
    *request_id = binding->request_id;
    portEXIT_CRITICAL(&s_lifecycle_lock);
}

typedef struct {
    char *buf;
    int capacity;
    int len;
} http_response_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_buf_t *resp = (http_response_buf_t *)evt->user_data;
    if (NULL == resp) {
        return ESP_OK;
    }
    if (HTTP_EVENT_ON_DATA == evt->event_id) {
        if (evt->data_len <= 0) {
            return ESP_OK;
        }
        if (NULL == resp->buf || resp->capacity <= 0) {
            return ESP_FAIL;
        }
        if (evt->data_len > resp->capacity - resp->len - 1) {
            ESP_LOGE(TAG, "http response buffer overflow");
            return ESP_FAIL;
        }
        memcpy(resp->buf + resp->len, evt->data, (size_t)evt->data_len);
        resp->len += evt->data_len;
        resp->buf[resp->len] = '\0';
    }
    return ESP_OK;
}

static esp_err_t version_check(void)
{
    cJSON *root = cJSON_CreateObject();
    if (NULL == root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "language", s_config.lang);
    cJSON_AddNumberToObject(root, "flash_size", 8388608);
    cJSON_AddStringToObject(root, "mac_address", s_config.device_mac);
    cJSON_AddStringToObject(root, "chip_model_name", "esp32s3");

    cJSON *app = cJSON_AddObjectToObject(root, "application");
    if (NULL == app) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(app, "name", "xiaozhi_ai_demo");
    cJSON_AddStringToObject(app, "version", "0.1.0");
    cJSON_AddStringToObject(app, "idf_version", "v5.5.4");

    cJSON *board = cJSON_AddObjectToObject(root, "board");
    if (NULL == board) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(board, "type", "laiwfs300");
    cJSON_AddStringToObject(board, "name", "L-AIWFS300");

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (NULL == post_data) {
        return ESP_ERR_NO_MEM;
    }

    http_response_buf_t resp_ctx = {
        .buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM),
        .capacity = 1024,
        .len = 0,
    };
    if (NULL == resp_ctx.buf) {
        cJSON_free(post_data);
        return ESP_ERR_NO_MEM;
    }
    resp_ctx.buf[0] = '\0';

    esp_http_client_config_t http_cfg = {
        .url = s_config.ota_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = VERSION_CHECK_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (NULL == client) {
        cJSON_free(post_data);
        heap_caps_free(resp_ctx.buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", s_config.device_mac);
    esp_http_client_set_header(client, "Client-Id", s_config.client_id);
    esp_http_client_set_header(client, "Accept-Language", s_config.lang);
    esp_http_client_set_header(client, "actvation-Version", "1");

    char ua_buf[64];
    snprintf(ua_buf, sizeof(ua_buf), "%s/%s",
             s_config.board_name ? s_config.board_name : "laiwfs300",
             s_config.app_version ? s_config.app_version : "0.1.0");
    esp_http_client_set_header(client, "User-Agent", ua_buf);

    esp_http_client_set_post_field(client, post_data, (int)strlen(post_data));

    ESP_LOGI(TAG, "version_check: POST %s", s_config.ota_url);
    esp_err_t err = esp_http_client_perform(client);
    cJSON_free(post_data);

    if (ESP_OK != err) {
        ESP_LOGE(TAG, "version_check HTTP failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(resp_ctx.buf);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "version_check: status=%d, resp_len=%d", status, resp_ctx.len);
    esp_http_client_cleanup(client);

    if (200 != status) {
        heap_caps_free(resp_ctx.buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "version_check resp: %s", resp_ctx.buf);

    cJSON *resp = cJSON_Parse(resp_ctx.buf);
    heap_caps_free(resp_ctx.buf);
    if (NULL == resp) {
        ESP_LOGE(TAG, "version_check: JSON parse failed");
        return ESP_FAIL;
    }

    s_ws_url[0] = '\0';
    s_ws_token[0] = '\0';

    cJSON *ws_obj = cJSON_GetObjectItem(resp, "websocket");
    if (NULL != ws_obj && cJSON_IsObject(ws_obj)) {
        cJSON *url_item = cJSON_GetObjectItem(ws_obj, "url");
        if (NULL != url_item && cJSON_IsString(url_item)) {
            strncpy(s_ws_url, url_item->valuestring, sizeof(s_ws_url) - 1);
            s_ws_url[sizeof(s_ws_url) - 1] = '\0';
        }
        cJSON *token_item = cJSON_GetObjectItem(ws_obj, "token");
        if (NULL != token_item && cJSON_IsString(token_item)) {
            strncpy(s_ws_token, token_item->valuestring, sizeof(s_ws_token) - 1);
            s_ws_token[sizeof(s_ws_token) - 1] = '\0';
        }
    }

    cJSON *activation = cJSON_GetObjectItem(resp, "activation");
    if (NULL != activation && cJSON_IsObject(activation)) {
        cJSON *code = cJSON_GetObjectItem(activation, "code");
        if (NULL != code && cJSON_IsString(code)) {
            ESP_LOGW(TAG, "=== ACTIVATION REQUIRED ===");
            ESP_LOGW(TAG, "Activation code: %s", code->valuestring);
            ESP_LOGW(TAG, "Please activate at the XiaoZhi platform.");
        }
    }

    cJSON_Delete(resp);

    if (s_ws_url[0] == '\0') {
        ESP_LOGE(TAG, "version_check: no websocket URL in response");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "WS URL: %s", s_ws_url);
    return ESP_OK;
}

static void handle_binary_in_ws_context(const uint8_t *data, int len,
                                        uint32_t session_epoch,
                                        uint32_t request_id);

static void ws_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ws_data =
        (esp_websocket_event_data_t *)event_data;
    const ws_callback_binding_t *binding =
        (const ws_callback_binding_t *)arg;
    uint32_t session_epoch = 0U;
    uint32_t request_id = 0U;
    ws_binding_snapshot(binding, &session_epoch, &request_id);
    if (0U == session_epoch || 0U == request_id) {
        return;
    }

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        (void)post_evt(EVT_WS_CONNECTED, NULL, 0, session_epoch,
                       request_id, true);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        (void)post_evt(EVT_WS_DISCONNECTED, NULL, 0, session_epoch,
                       request_id, true);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (0x01 == ws_data->op_code) {
            (void)post_evt(EVT_WS_TEXT,
                           (const uint8_t *)ws_data->data_ptr,
                           ws_data->data_len, session_epoch,
                           request_id, true);
        } else if (0x02 == ws_data->op_code) {
            handle_binary_in_ws_context(
                (const uint8_t *)ws_data->data_ptr, ws_data->data_len,
                session_epoch, request_id);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        (void)post_evt(EVT_WS_DISCONNECTED, NULL, 0, session_epoch,
                       request_id, true);
        break;
    default:
        break;
    }
}

static esp_err_t ws_connect(uint32_t session_epoch, uint32_t request_id)
{
    if (!request_is_current_and_live(session_epoch, request_id) ||
        s_ws_url[0] == '\0' || NULL == s_ws_lock ||
        pdTRUE != xSemaphoreTake(s_ws_lock, portMAX_DELAY)) {
        return ESP_ERR_INVALID_STATE;
    }

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_ws_token);

    char proto_ver[4];
    snprintf(proto_ver, sizeof(proto_ver), "%d", PROTO_VERSION);

    esp_websocket_client_config_t ws_cfg = {
        .uri = s_ws_url,
        .task_name = "xiaozhi_ws",
        .task_stack = WS_TASK_STACK_SIZE,
        .buffer_size = WS_BUFFER_SIZE,
        .network_timeout_ms = WS_CONNECT_TIMEOUT_MS,
        .disable_auto_reconnect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ws_callback_binding_t *binding = heap_caps_calloc(
        1U, sizeof(*binding), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (NULL == binding) {
        xSemaphoreGive(s_ws_lock);
        return ESP_ERR_NO_MEM;
    }
    binding->session_epoch = session_epoch;
    binding->request_id = request_id;

    ESP_LOGI(TAG,
             "[DEBUG-WSRAM] stage=before_init attempt=0 result=%s ws_stack=%lu internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
             esp_err_to_name(ESP_OK), (unsigned long)WS_TASK_STACK_SIZE,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_minimum_free_size(
                 MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (NULL == s_ws_client) {
        ESP_LOGE(TAG,
                 "[DEBUG-WSRAM] stage=after_init attempt=0 result=NULL ws_stack=%lu internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
                 (unsigned long)WS_TASK_STACK_SIZE,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_minimum_free_size(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        heap_caps_free(binding);
        xSemaphoreGive(s_ws_lock);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "[DEBUG-WSRAM] stage=after_init attempt=0 result=%s ws_stack=%lu internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
             esp_err_to_name(ESP_OK), (unsigned long)WS_TASK_STACK_SIZE,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_minimum_free_size(
                 MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    portENTER_CRITICAL(&s_lifecycle_lock);
    s_ws_binding = binding;
    portEXIT_CRITICAL(&s_lifecycle_lock);

    esp_websocket_client_append_header(s_ws_client, "Authorization", auth_header);
    esp_websocket_client_append_header(s_ws_client, "Protocol-Version", proto_ver);
    esp_websocket_client_append_header(s_ws_client, "Device-Id", s_config.device_mac);
    esp_websocket_client_append_header(s_ws_client, "Client-Id", s_config.client_id);

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, binding);

    ESP_LOGI(TAG, "WS connecting to: %s", s_ws_url);
    esp_err_t ret = ESP_FAIL;
    for (uint32_t attempt = 1U;
         attempt <= XIAOZHI_AGENT_WS_START_MAX_ATTEMPTS; attempt++) {
        if (!s_running ||
            !request_is_current_and_live(session_epoch, request_id)) {
            break;
        }
        ESP_LOGI(TAG,
                 "[DEBUG-WSRAM] stage=before_start attempt=%lu result=%s ws_stack=%lu internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
                 (unsigned long)attempt, esp_err_to_name(ESP_OK),
                 (unsigned long)WS_TASK_STACK_SIZE,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_minimum_free_size(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        ret = esp_websocket_client_start(s_ws_client);
        ESP_LOGI(TAG,
                 "[DEBUG-WSRAM] stage=after_start attempt=%lu result=%s ws_stack=%lu internal_free=%lu internal_largest=%lu internal_min=%lu psram_free=%lu",
                 (unsigned long)attempt, esp_err_to_name(ret),
                 (unsigned long)WS_TASK_STACK_SIZE,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_largest_free_block(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_minimum_free_size(
                     MALLOC_CAP_INTERNAL),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        const xiaozhi_agent_ws_start_action_t action =
            xiaozhi_agent_ws_start_decide(ret, attempt);
        if (XIAOZHI_AGENT_WS_START_SUCCEEDED == action) {
            break;
        }
        if (XIAOZHI_AGENT_WS_START_RETRY != action || !s_running ||
            !request_is_current_and_live(session_epoch, request_id)) {
            break;
        }
        ESP_LOGW(TAG,
                 "[DEBUG-WSRAM] retry websocket start after %lums attempt=%lu/%u",
                 (unsigned long)WS_START_RETRY_DELAY_MS,
                 (unsigned long)attempt,
                 (unsigned int)XIAOZHI_AGENT_WS_START_MAX_ATTEMPTS);
        vTaskDelay(pdMS_TO_TICKS(WS_START_RETRY_DELAY_MS));
    }
    if (ESP_OK == ret &&
        !request_is_current_and_live(session_epoch, request_id)) {
        ret = ESP_ERR_INVALID_STATE;
    }
    if (ESP_OK != ret) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_ws_binding = NULL;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        heap_caps_free(binding);
        xSemaphoreGive(s_ws_lock);
        return ret;
    }
    xSemaphoreGive(s_ws_lock);
    return ESP_OK;
}

static void ws_disconnect(void)
{
    if (NULL == s_ws_lock ||
        pdTRUE != xSemaphoreTake(s_ws_lock, portMAX_DELAY)) {
        return;
    }
    if (NULL != s_ws_client) {
        ws_callback_binding_t *binding = s_ws_binding;
        esp_websocket_client_close(s_ws_client, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_ws_binding = NULL;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        heap_caps_free(binding);
    }
    xSemaphoreGive(s_ws_lock);
}

static bool ws_is_connected(void)
{
    if (NULL == s_ws_lock ||
        pdTRUE != xSemaphoreTake(s_ws_lock, portMAX_DELAY)) {
        return false;
    }
    const bool connected = NULL != s_ws_client &&
                           esp_websocket_client_is_connected(s_ws_client);
    xSemaphoreGive(s_ws_lock);
    return connected;
}

static esp_err_t ws_send_hello(void)
{
    cJSON *hello = cJSON_CreateObject();
    if (NULL == hello) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(hello, "type", "hello");
    cJSON_AddNumberToObject(hello, "version", PROTO_VERSION);
    cJSON_AddStringToObject(hello, "transport", "websocket");

    cJSON *audio = cJSON_AddObjectToObject(hello, "audio_params");
    if (NULL == audio) {
        cJSON_Delete(hello);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddNumberToObject(audio, "frame_duration", 60);

    char *json_str = cJSON_PrintUnformatted(hello);
    cJSON_Delete(hello);
    if (NULL == json_str) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending hello: %s", json_str);
    int sent = -1;
    if (NULL != s_ws_lock &&
        pdTRUE == xSemaphoreTake(s_ws_lock, portMAX_DELAY)) {
        if (NULL != s_ws_client &&
            esp_websocket_client_is_connected(s_ws_client)) {
            sent = esp_websocket_client_send_text(
                s_ws_client, json_str, (int)strlen(json_str),
                pdMS_TO_TICKS(5000));
        }
        xSemaphoreGive(s_ws_lock);
    }
    cJSON_free(json_str);
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ws_send_json_msg(const char *type, const char *extra_fields)
{
    if (NULL == type) {
        return ESP_ERR_INVALID_ARG;
    }

    char buf[JSON_BUF_SIZE];
    int len;
    if (s_session_id[0] != '\0' && NULL != extra_fields) {
        len = snprintf(buf, sizeof(buf), "{\"session_id\":\"%s\",\"type\":\"%s\",%s}",
                       s_session_id, type, extra_fields);
    } else if (s_session_id[0] != '\0') {
        len = snprintf(buf, sizeof(buf), "{\"session_id\":\"%s\",\"type\":\"%s\"}",
                       s_session_id, type);
    } else if (NULL != extra_fields) {
        len = snprintf(buf, sizeof(buf), "{\"type\":\"%s\",%s}", type, extra_fields);
    } else {
        len = snprintf(buf, sizeof(buf), "{\"type\":\"%s\"}", type);
    }
    if (0 > len || sizeof(buf) <= (size_t)len) {
        ESP_LOGE(TAG, "ws_send_json_msg: JSON overflow type=%s len=%d", type,
                 len);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "TX JSON: %s", buf);
    if (NULL == s_ws_lock ||
        pdTRUE != xSemaphoreTake(s_ws_lock, portMAX_DELAY)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == s_ws_client ||
        !esp_websocket_client_is_connected(s_ws_client)) {
        xSemaphoreGive(s_ws_lock);
        ESP_LOGW(TAG, "ws_send_json_msg: not connected, dropping type=%s", type);
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_text(
        s_ws_client, buf, len, pdMS_TO_TICKS(2000));
    xSemaphoreGive(s_ws_lock);
    if (sent <= 0) {
        ESP_LOGW(TAG, "ws_send_json_msg: send failed for type=%s", type);
    }
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t ws_send_audio_frame(const uint8_t *opus_data, int opus_len,
                                     uint32_t session_epoch,
                                     uint32_t request_id)
{
    if (NULL == opus_data || 0 >= opus_len || UINT16_MAX < opus_len) {
        ESP_LOGW(TAG, "ws_send_audio_frame: invalid opus_len=%d", opus_len);
        return ESP_ERR_INVALID_ARG;
    }
    if (!request_is_current_and_live(session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t hdr[PROTO_HDR_SIZE];
    hdr[0] = 0;
    hdr[1] = 0;
    uint16_t plen = htons((uint16_t)opus_len);
    memcpy(&hdr[2], &plen, 2);

    int total = PROTO_HDR_SIZE + opus_len;
    uint8_t *frame = heap_caps_malloc((size_t)total, MALLOC_CAP_SPIRAM);
    if (NULL == frame) {
        ESP_LOGW(TAG, "ws_send_audio_frame: alloc failed, total=%d", total);
        return ESP_ERR_NO_MEM;
    }
    memcpy(frame, hdr, PROTO_HDR_SIZE);
    memcpy(frame + PROTO_HDR_SIZE, opus_data, (size_t)opus_len);

    if (NULL == s_ws_lock ||
        pdTRUE != xSemaphoreTake(s_ws_lock, portMAX_DELAY)) {
        heap_caps_free(frame);
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == s_ws_client ||
        !esp_websocket_client_is_connected(s_ws_client)) {
        xSemaphoreGive(s_ws_lock);
        heap_caps_free(frame);
        ESP_LOGW(TAG, "ws_send_audio_frame: not connected, opus_len=%d", opus_len);
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_bin(
        s_ws_client, (const char *)frame, total, pdMS_TO_TICKS(1000));
    xSemaphoreGive(s_ws_lock);
    heap_caps_free(frame);
    if (!request_is_current_and_live(session_epoch, request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=audio_send_drop epoch=%lu request=%lu reason=stale_or_cancelled sent=%d",
                 (unsigned long)session_epoch, (unsigned long)request_id,
                 sent);
        return ESP_ERR_INVALID_STATE;
    }
    if (sent != total) {
        ESP_LOGW(TAG, "ws_send_audio_frame: send failed, sent=%d expected=%d opus_len=%d",
                 sent, total, opus_len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void handle_server_hello(cJSON *root)
{
    cJSON *sid = cJSON_GetObjectItem(root, "session_id");
    if (NULL != sid && cJSON_IsString(sid)) {
        strncpy(s_session_id, sid->valuestring, sizeof(s_session_id) - 1);
        s_session_id[sizeof(s_session_id) - 1] = '\0';
        ESP_LOGI(TAG, "Server hello, session_id=%s", s_session_id);
    }

    cJSON *audio = cJSON_GetObjectItem(root, "audio_params");
    if (NULL != audio && cJSON_IsObject(audio)) {
        cJSON *format = cJSON_GetObjectItem(audio, "format");
        cJSON *sample_rate = cJSON_GetObjectItem(audio, "sample_rate");
        cJSON *channels = cJSON_GetObjectItem(audio, "channels");
        cJSON *frame_duration = cJSON_GetObjectItem(audio, "frame_duration");
        ESP_LOGI(TAG, "Server audio_params: format=%s sample_rate=%d channels=%d frame_duration=%d",
                 (NULL != format && cJSON_IsString(format)) ? format->valuestring : "?",
                 (NULL != sample_rate && cJSON_IsNumber(sample_rate)) ? sample_rate->valueint : -1,
                 (NULL != channels && cJSON_IsNumber(channels)) ? channels->valueint : -1,
                 (NULL != frame_duration && cJSON_IsNumber(frame_duration)) ? frame_duration->valueint : -1);
    }
}

static void close_session_with_result(esp_err_t result)
{
    const uint32_t session_epoch = s_session_epoch;
    const uint32_t request_id = s_active_request_id;
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=close epoch=%lu request=%lu state=%d result=%s",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (int)s_state, esp_err_to_name(result));
    ESP_LOGI(TAG, "Closing session epoch=%lu request=%lu result=%s",
             (unsigned long)session_epoch, (unsigned long)request_id,
             esp_err_to_name(result));
    reset_tts_barrier(0U, 0U);
    notify_audio_stop(session_epoch, request_id);
    ws_disconnect();
    s_session_id[0] = '\0';
    set_state(XIAOZHI_STATE_IDLE);
    if (0U != request_id) {
        notify_semantic((ESP_OK == result) ? XIAOZHI_AGENT_EVENT_CLOSED :
                                            XIAOZHI_AGENT_EVENT_FAILED,
                        session_epoch, request_id, result);
    }
    clear_cancel_request(request_id);
    set_active_request_id(0U);
    reset_tx_stats();
}

static bool close_session_if_current(uint32_t session_epoch,
                                     uint32_t request_id,
                                     esp_err_t result,
                                     bool require_live,
                                     const char *source)
{
    const bool accepted = require_live ?
        request_is_current_and_live(session_epoch, request_id) :
        request_is_current(session_epoch, request_id);
    if (!accepted) {
        uint32_t actual_session_epoch = 0U;
        uint32_t actual_request_id = 0U;
        portENTER_CRITICAL(&s_lifecycle_lock);
        actual_session_epoch = s_session_epoch;
        actual_request_id = s_active_request_id;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=close_drop source=%s epoch=%lu/%lu request=%lu/%lu live_required=%u result=%s",
                 (NULL != source) ? source : "?",
                 (unsigned long)session_epoch,
                 (unsigned long)actual_session_epoch,
                 (unsigned long)request_id,
                 (unsigned long)actual_request_id,
                 require_live ? 1U : 0U, esp_err_to_name(result));
        return false;
    }
    close_session_with_result(result);
    return true;
}

static void close_session(void)
{
    close_session_with_result(ESP_OK);
}

static esp_err_t start_listening_or_close(uint32_t request_id)
{
    const uint32_t session_epoch = s_session_epoch;
    const xiaozhi_agent_state_t expected_state = s_state;
    if (!request_is_current_and_live(session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=listen_start_send epoch=%lu request=%lu state=%d",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (int)expected_state);
    esp_err_t ret = ws_send_json_msg(
        "listen", xiaozhi_agent_listen_start_fields(
                      s_config.client_manages_listen_stop));
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "listen start failed: %s", esp_err_to_name(ret));
        (void)close_session_if_current(session_epoch, request_id, ret, true,
                                       "listen_start_send");
        return ret;
    }

    if (!set_state_if_current(expected_state, XIAOZHI_STATE_LISTENING,
                              session_epoch, request_id, "listen_start")) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!request_is_current_and_live(session_epoch, request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=semantic_drop source=listen_start epoch=%lu request=%lu",
                 (unsigned long)session_epoch, (unsigned long)request_id);
        return ESP_ERR_INVALID_STATE;
    }
    if (!xiaozhi_agent_vad_stop_policy_set_listening(
            &s_vad_stop_policy, request_id, true)) {
        ESP_LOGW(TAG,
                 "VAD stop policy rejected LISTENING request=%lu",
                 (unsigned long)request_id);
        return ESP_ERR_INVALID_STATE;
    }
    mark_tts_barrier_ready(session_epoch, request_id);
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=listen_start_done epoch=%lu request=%lu state=%d result=%s",
             (unsigned long)session_epoch, (unsigned long)request_id,
             (int)s_state, esp_err_to_name(ret));
    notify_semantic(XIAOZHI_AGENT_EVENT_LISTENING_READY, session_epoch,
                    request_id, ESP_OK);
    return ESP_OK;
}

static esp_err_t finish_pending_vad_stop(void)
{
    const uint32_t session_epoch = s_session_epoch;
    const uint32_t request_id = s_active_request_id;
    if (!xiaozhi_agent_vad_stop_policy_ready(
            &s_vad_stop_policy, request_id) ||
        !s_vad_stop_pending || s_vad_stop_sent ||
        XIAOZHI_STATE_LISTENING != s_state ||
        !request_is_current_and_live(session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG,
             "VAD stop ready epoch=%lu request=%lu sent_frames=%lu",
             (unsigned long)session_epoch,
             (unsigned long)request_id,
             (unsigned long)s_tx_frame_count);
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=listen_stop_send epoch=%lu request=%lu state=%d sent_frames=%lu",
             (unsigned long)session_epoch,
             (unsigned long)request_id, (int)s_state,
             (unsigned long)s_tx_frame_count);
    const esp_err_t result = ws_send_json_msg(
        "listen", "\"state\":\"stop\"");
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "listen stop failed: %s", esp_err_to_name(result));
        (void)close_session_if_current(session_epoch, request_id, result,
                                       true, "listen_stop_send");
        return result;
    }
    if (!set_state_if_current(XIAOZHI_STATE_LISTENING,
                              XIAOZHI_STATE_PROCESSING, session_epoch,
                              request_id, "listen_stop")) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!xiaozhi_agent_vad_stop_policy_mark_stop_sent(
            &s_vad_stop_policy, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_vad_stop_pending = false;
    s_vad_stop_sent = true;
    mark_tts_barrier_listen_stop(session_epoch, request_id);
    s_processing_start_tick = xTaskGetTickCount();
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=listen_stop_done epoch=%lu request=%lu state=%d result=%s",
             (unsigned long)session_epoch,
             (unsigned long)request_id, (int)s_state,
             esp_err_to_name(result));
    if (!request_is_current_and_live(session_epoch, request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=semantic_drop source=listen_stop epoch=%lu request=%lu",
                 (unsigned long)session_epoch, (unsigned long)request_id);
        return ESP_ERR_INVALID_STATE;
    }
    notify_semantic(XIAOZHI_AGENT_EVENT_PROCESSING, session_epoch,
                    request_id, ESP_OK);
    return ESP_OK;
}

static esp_err_t accept_vad_end(uint32_t request_id)
{
    if (!request_is_current_and_live(s_session_epoch, request_id) ||
        s_vad_stop_sent || XIAOZHI_STATE_PROCESSING == s_state ||
        XIAOZHI_STATE_SPEAKING == s_state ||
        XIAOZHI_STATE_LISTENING != s_state ||
        !xiaozhi_agent_vad_stop_policy_accept_vad_end(
            &s_vad_stop_policy, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_vad_stop_pending = true;
    if (xiaozhi_agent_vad_stop_policy_ready(
            &s_vad_stop_policy, request_id)) {
        return finish_pending_vad_stop();
    }
    ESP_LOGI(TAG,
             "VAD stop pending epoch=%lu request=%lu state=%d sent_frames=%lu threshold=%d",
             (unsigned long)s_session_epoch, (unsigned long)request_id,
             (int)s_state, (unsigned long)s_tx_frame_count,
             MIN_TX_FRAMES_BEFORE_VAD_END);
    return ESP_OK;
}

static esp_err_t rebind_opening_request(uint32_t request_id)
{
    if (0U == request_id || request_id == s_active_request_id ||
        cancel_requested(request_id)) {
        if (cancel_requested(request_id)) {
            clear_cancel_request(request_id);
            ESP_LOGW(TAG,
                     "[DEBUG-AI-P0] phase=wake_rebind_drop request=%lu reason=cancelled",
                     (unsigned long)request_id);
        }
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t previous_request_id = s_active_request_id;
    const uint32_t session_epoch = s_session_epoch;
    if (!rebind_request_if_current(session_epoch, previous_request_id,
                                   request_id, "opening_wake")) {
        if (cancel_requested(request_id)) {
            clear_cancel_request(request_id);
        }
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG,
             "Rebinding opening session epoch=%lu request=%lu->%lu",
             (unsigned long)session_epoch,
             (unsigned long)previous_request_id,
             (unsigned long)request_id);
    if (0U != previous_request_id) {
        notify_semantic(XIAOZHI_AGENT_EVENT_CLOSED, session_epoch,
                        previous_request_id, ESP_OK);
        clear_cancel_request(previous_request_id);
    }
    reset_tx_stats();
    reset_tts_barrier(session_epoch, request_id);
    if (!request_is_current_and_live(session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    notify_semantic(XIAOZHI_AGENT_EVENT_CONNECTING, session_epoch,
                    request_id, ESP_OK);
    return ESP_OK;
}

static esp_err_t accept_vad_start(uint32_t request_id)
{
    if (!request_is_current_and_live(s_session_epoch, request_id) ||
        XIAOZHI_STATE_LISTENING != s_state || s_vad_stop_sent ||
        !xiaozhi_agent_vad_stop_policy_accept_vad_start(
            &s_vad_stop_policy, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_vad_stop_pending = false;
    ESP_LOGI(TAG,
             "VAD start cleared pending stop epoch=%lu request=%lu sent_frames=%lu",
             (unsigned long)s_session_epoch, (unsigned long)request_id,
             (unsigned long)s_tx_frame_count);
    return ESP_OK;
}

static esp_err_t handle_opening_control_event(agent_evt_t *event)
{
    if (NULL == event) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->session_epoch != s_session_epoch &&
        EVT_WAKE_WORD != event->type && EVT_CANCEL_REQUEST != event->type) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=open_control_drop type=%d epoch=%lu current_epoch=%lu request=%lu",
                 (int)event->type, (unsigned long)event->session_epoch,
                 (unsigned long)s_session_epoch,
                 (unsigned long)event->request_id);
        return ESP_ERR_INVALID_STATE;
    }
    if (EVT_WAKE_WORD == event->type) {
        return rebind_opening_request(event->request_id);
    }
    if (EVT_VAD_START == event->type) {
        return accept_vad_start(event->request_id);
    }
    if (EVT_VAD_END == event->type) {
        return accept_vad_end(event->request_id);
    }
    if (EVT_CANCEL_REQUEST == event->type &&
        event->request_id == s_active_request_id) {
        close_session();
        return ESP_ERR_INVALID_STATE;
    }
    if (EVT_CANCEL_REQUEST == event->type) {
        clear_cancel_request(event->request_id);
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t handle_opening_websocket_event(agent_evt_t *event,
                                                bool *session_opened)
{
    if (NULL == event || NULL == session_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    *session_opened = false;
    if (event->session_epoch != s_session_epoch) {
        ESP_LOGW(TAG,
                 "Ignoring stale open event type=%d epoch=%lu request=%lu current=%lu/%lu",
                 (int)event->type, (unsigned long)event->session_epoch,
                 (unsigned long)event->request_id,
                 (unsigned long)s_session_epoch,
                 (unsigned long)s_active_request_id);
        return ESP_OK;
    }
    const uint32_t current_session_epoch = s_session_epoch;
    const uint32_t current_request_id = s_active_request_id;
    if (!request_is_current_and_live(current_session_epoch,
                                     current_request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=open_event_drop type=%d epoch=%lu request=%lu reason=cancelled",
                 (int)event->type, (unsigned long)current_session_epoch,
                 (unsigned long)current_request_id);
        return ESP_ERR_INVALID_STATE;
    }
    if (event->request_id != s_active_request_id) {
        if (EVT_WS_DISCONNECTED == event->type) {
            /* A disconnect queued before an opening-request rebind belongs
             * to the retired logical request.  Let the current transport
             * report its own disconnect; consuming this stale event here
             * would tear down the replacement request. */
            ESP_LOGW(TAG,
                     "Ignoring stale opening disconnect epoch=%lu event_request=%lu current_request=%lu",
                     (unsigned long)event->session_epoch,
                     (unsigned long)event->request_id,
                     (unsigned long)s_active_request_id);
            return ESP_OK;
        }
        ESP_LOGI(TAG,
                 "Accepting connection event after request rebind type=%d epoch=%lu event_request=%lu current_request=%lu",
                 (int)event->type, (unsigned long)event->session_epoch,
                 (unsigned long)event->request_id,
                 (unsigned long)s_active_request_id);
    }
    if (EVT_WS_CONNECTED == event->type) {
        ESP_LOGI(TAG, "WS connected, sending hello");
        const esp_err_t result = ws_send_hello();
        if (ESP_OK != result ||
            !request_is_current_and_live(current_session_epoch,
                                         current_request_id)) {
            return (ESP_OK == result) ? ESP_ERR_INVALID_STATE : result;
        }
        return ESP_OK;
    }
    if (EVT_WS_DISCONNECTED == event->type) {
        ESP_LOGW(TAG, "WS disconnected during open_session");
        return ESP_FAIL;
    }
    if (EVT_WS_TEXT != event->type || NULL == event->data) {
        return ESP_OK;
    }

    char *json_str = heap_caps_malloc((size_t)(event->len + 1),
                                      MALLOC_CAP_SPIRAM);
    if (NULL == json_str) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(json_str, event->data, (size_t)event->len);
    json_str[event->len] = '\0';
    cJSON *root = cJSON_Parse(json_str);
    heap_caps_free(json_str);
    if (NULL == root) {
        return ESP_OK;
    }
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (NULL != type_item && cJSON_IsString(type_item) &&
        0 == strcmp(type_item->valuestring, "hello")) {
        if (!request_is_current_and_live(current_session_epoch,
                                         current_request_id)) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_STATE;
        }
        handle_server_hello(root);
        *session_opened = true;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t open_session(void)
{
    uint32_t request_id = active_request_id_snapshot();
    const xiaozhi_agent_state_t expected_state = s_state;
    if (!request_is_current_and_live(s_session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_session_epoch = next_nonzero(s_session_epoch);
    const uint32_t session_epoch = s_session_epoch;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    reset_tts_barrier(session_epoch, request_id);
    ESP_LOGI(TAG,
             "Opening new session epoch=%lu request=%lu (version_check + ws_connect)",
             (unsigned long)session_epoch,
             (unsigned long)request_id);
    if (!set_state_if_current(expected_state, XIAOZHI_STATE_CONNECTING,
                              session_epoch, request_id,
                              "open_session")) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!request_is_current_and_live(session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    notify_semantic(XIAOZHI_AGENT_EVENT_CONNECTING, session_epoch,
                    request_id, ESP_OK);

    uint32_t retry_delay_ms = RETRY_BASE_MS;
    while (s_running) {
        if (!refresh_live_request(session_epoch, &request_id,
                                  "version_check_begin")) {
            return ESP_ERR_INVALID_STATE;
        }
        if (take_critical_event_failure()) {
            close_session_with_result(ESP_ERR_NO_MEM);
            return ESP_ERR_NO_MEM;
        }
        esp_err_t ret = version_check();
        if (!refresh_live_request(session_epoch, &request_id,
                                  "version_check_done")) {
            return ESP_ERR_INVALID_STATE;
        }
        if (take_critical_event_failure()) {
            close_session_with_result(ESP_ERR_NO_MEM);
            return ESP_ERR_NO_MEM;
        }
        if (ESP_OK == ret) {
            break;
        }
        ESP_LOGW(TAG, "version_check failed, retry in %lu ms", (unsigned long)retry_delay_ms);
        uint32_t remaining_ms = retry_delay_ms;
        while (s_running && 0U < remaining_ms) {
            if (!refresh_live_request(session_epoch, &request_id,
                                      "version_retry_wait")) {
                return ESP_ERR_INVALID_STATE;
            }
            if (take_critical_event_failure()) {
                close_session_with_result(ESP_ERR_NO_MEM);
                return ESP_ERR_NO_MEM;
            }
            const uint32_t slice_ms = (remaining_ms > 100U) ? 100U :
                                                               remaining_ms;
            agent_evt_t event = {0};
            if (pdTRUE == xQueueReceive(s_evt_queue, &event,
                                        pdMS_TO_TICKS(slice_ms))) {
                const esp_err_t event_result =
                    handle_opening_control_event(&event);
                if (NULL != event.data) {
                    heap_caps_free(event.data);
                }
                if (ESP_ERR_INVALID_STATE == event_result &&
                    EVT_CANCEL_REQUEST == event.type) {
                    return event_result;
                }
                if (!refresh_live_request(session_epoch, &request_id,
                                          "version_retry_event")) {
                    return ESP_ERR_INVALID_STATE;
                }
            }
            remaining_ms -= slice_ms;
        }
        retry_delay_ms = retry_delay_ms * 2;
        if (retry_delay_ms > RETRY_MAX_MS) {
            retry_delay_ms = RETRY_MAX_MS;
        }
    }

    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!refresh_live_request(session_epoch, &request_id,
                              "ws_connect_begin")) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ws_connect(session_epoch, request_id);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "WS connect failed");
        return ret;
    }
    if (!refresh_live_request(session_epoch, &request_id,
                              "ws_connect_done")) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Wait for WS connected + server hello with timeout */
    TickType_t phase_start = xTaskGetTickCount();
    bool ws_connected = false;
    while (s_running) {
        if (!refresh_live_request(session_epoch, &request_id,
                                  "ws_open_wait")) {
            return ESP_ERR_INVALID_STATE;
        }
        if (take_critical_event_failure()) {
            close_session_with_result(ESP_ERR_NO_MEM);
            return ESP_ERR_NO_MEM;
        }
        agent_evt_t evt;
        if (pdTRUE != xQueueReceive(s_evt_queue, &evt, pdMS_TO_TICKS(500))) {
            if (take_critical_event_failure()) {
                close_session_with_result(ESP_ERR_NO_MEM);
                return ESP_ERR_NO_MEM;
            }
            if (!refresh_live_request(session_epoch, &request_id,
                                      "ws_open_wait_timeout")) {
                return ESP_ERR_INVALID_STATE;
            }
            const uint32_t timeout_ms = ws_connected ?
                                        HELLO_TIMEOUT_MS :
                                        WS_CONNECT_TIMEOUT_MS;
            if ((xTaskGetTickCount() - phase_start) >
                pdMS_TO_TICKS(timeout_ms)) {
                ESP_LOGE(TAG, "%s timeout",
                         ws_connected ? "Hello" : "WS connect");
                ws_disconnect();
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }

        const bool websocket_event = EVT_WS_CONNECTED == evt.type ||
                                     EVT_WS_DISCONNECTED == evt.type ||
                                     EVT_WS_TEXT == evt.type;
        if (websocket_event) {
            bool session_opened = false;
            const esp_err_t event_result =
                handle_opening_websocket_event(&evt, &session_opened);
            if (ESP_OK != event_result) {
                if (NULL != evt.data) {
                    heap_caps_free(evt.data);
                    evt.data = NULL;
                }
                close_session_with_result(event_result);
                return event_result;
            }
            if (EVT_WS_CONNECTED == evt.type) {
                ws_connected = true;
                phase_start = xTaskGetTickCount();
            }
            if (session_opened) {
                if (NULL != evt.data) {
                    heap_caps_free(evt.data);
                }
                ESP_LOGI(TAG, "Session opened successfully");
                return ESP_OK;
            }
        } else {
            const esp_err_t event_result =
                handle_opening_control_event(&evt);
            if (ESP_ERR_INVALID_STATE == event_result &&
                EVT_CANCEL_REQUEST == evt.type) {
                if (NULL != evt.data) {
                    heap_caps_free(evt.data);
                    evt.data = NULL;
                }
                return event_result;
            }
        }

        if (NULL != evt.data) {
            heap_caps_free(evt.data);
        }

        if (!refresh_live_request(session_epoch, &request_id,
                                  "ws_open_event")) {
            return ESP_ERR_INVALID_STATE;
        }

        if (take_critical_event_failure()) {
            close_session_with_result(ESP_ERR_NO_MEM);
            return ESP_ERR_NO_MEM;
        }

        const uint32_t timeout_ms = ws_connected ?
                                    HELLO_TIMEOUT_MS : WS_CONNECT_TIMEOUT_MS;
        if ((xTaskGetTickCount() - phase_start) >
            pdMS_TO_TICKS(timeout_ms)) {
            ESP_LOGE(TAG, "%s timeout (after events)",
                     ws_connected ? "Hello" : "WS connect");
            ws_disconnect();
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_ERR_INVALID_STATE;
}

static esp_err_t open_session_and_start_listening(const char *reason)
{
    ESP_LOGI(TAG, "Open session for listening: %s", (NULL != reason) ? reason : "?");
    esp_err_t ret = open_session();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "open_session failed: %s", esp_err_to_name(ret));
        const uint32_t failure_request_id = active_request_id_snapshot();
        (void)close_session_if_current(s_session_epoch, failure_request_id,
                                       ret, false,
                                       "open_session_failure");
        return ret;
    }
    const uint32_t session_epoch = s_session_epoch;
    const uint32_t request_id = s_active_request_id;
    if (!request_is_current_and_live(session_epoch, request_id)) {
        (void)close_session_if_current(session_epoch, request_id,
                                       ESP_ERR_INVALID_STATE, false,
                                       "open_session_cancelled");
        return ESP_ERR_INVALID_STATE;
    }
    return start_listening_or_close(request_id);
}

static void restart_listening_after_wake(uint32_t request_id,
                                         bool stop_audio)
{
    const uint32_t session_epoch = s_session_epoch;
    const uint32_t previous_request_id = active_request_id_snapshot();
    if (0U == request_id || request_id == previous_request_id ||
        cancel_requested(request_id) ||
        !request_is_current(session_epoch, previous_request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=abort_drop epoch=%lu old_request=%lu new_request=%lu old_cancelled=%u new_cancelled=%u",
                 (unsigned long)session_epoch,
                 (unsigned long)previous_request_id,
                 (unsigned long)request_id,
                 cancel_requested(previous_request_id) ? 1U : 0U,
                 cancel_requested(request_id) ? 1U : 0U);
        (void)close_session_if_current(session_epoch, previous_request_id,
                                       ESP_ERR_INVALID_STATE, false,
                                       "abort_precondition");
        return;
    }
    if (stop_audio) {
        notify_audio_stop(session_epoch, previous_request_id);
    }
    ESP_LOGI(TAG,
             "Restarting listening in current session (abort + listen), state=%d request=%lu->%lu",
             (int)s_state, (unsigned long)previous_request_id,
             (unsigned long)request_id);
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=abort_send epoch=%lu old_request=%lu new_request=%lu state=%d",
             (unsigned long)session_epoch,
             (unsigned long)previous_request_id,
             (unsigned long)request_id, (int)s_state);
    const esp_err_t abort_result =
        ws_send_json_msg("abort", "\"reason\":\"wake_word_detected\"");
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=abort_done epoch=%lu old_request=%lu new_request=%lu result=%s",
             (unsigned long)session_epoch,
             (unsigned long)previous_request_id,
             (unsigned long)request_id, esp_err_to_name(abort_result));
    if (ESP_OK != abort_result) {
        ESP_LOGW(TAG, "abort failed, reopening session: %s",
                 esp_err_to_name(abort_result));
        (void)close_session_if_current(session_epoch, previous_request_id,
                                       abort_result, false,
                                       "abort_send_failure");
        if (!activate_request_if_live(request_id, "abort_reopen")) {
            return;
        }
        reset_tx_stats();
        (void)open_session_and_start_listening("wake interrupt abort failed");
        return;
    }

    if (!rebind_request_if_current(session_epoch, previous_request_id,
                                   request_id, "abort_success")) {
        (void)close_session_if_current(session_epoch, previous_request_id,
                                       ESP_ERR_INVALID_STATE, false,
                                       "abort_rebind_failure");
        return;
    }
    notify_semantic(XIAOZHI_AGENT_EVENT_CLOSED, session_epoch,
                    previous_request_id, ESP_OK);
    clear_cancel_request(previous_request_id);
    reset_tx_stats();
    reset_tts_barrier(session_epoch, request_id);
    const esp_err_t listen_result = start_listening_or_close(request_id);
    ESP_LOGI(TAG,
             "[DEBUG-AI-TXN] phase=abort_listen_done epoch=%lu old_request=%lu new_request=%lu result=%s",
             (unsigned long)session_epoch,
             (unsigned long)previous_request_id,
             (unsigned long)request_id, esp_err_to_name(listen_result));
    if (ESP_OK != listen_result) {
        (void)close_session_if_current(session_epoch, request_id,
                                       listen_result, false,
                                       "abort_listen_failure");
    }
}

static void handle_text_message(const uint8_t *data, int len,
                                uint32_t session_epoch,
                                uint32_t request_id)
{
    if (NULL == data || 0 >= len) {
        return;
    }
    char *json_str = heap_caps_malloc((size_t)(len + 1), MALLOC_CAP_SPIRAM);
    if (NULL == json_str) {
        return;
    }
    memcpy(json_str, data, (size_t)len);
    json_str[len] = '\0';

    ESP_LOGI(TAG, "RX JSON: %s", json_str);

    cJSON *root = cJSON_Parse(json_str);
    heap_caps_free(json_str);
    if (NULL == root) {
        ESP_LOGW(TAG, "RX JSON parse failed");
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (NULL == type_item || !cJSON_IsString(type_item)) {
        ESP_LOGW(TAG, "RX JSON missing string type");
        cJSON_Delete(root);
        return;
    }
    const char *type = type_item->valuestring;
    if (!request_is_current_and_live(session_epoch, request_id)) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=text_drop type=%s epoch=%lu/%lu request=%lu/%lu reason=stale_or_cancelled",
                 type, (unsigned long)session_epoch,
                 (unsigned long)s_session_epoch,
                 (unsigned long)request_id,
                 (unsigned long)s_active_request_id);
        cJSON_Delete(root);
        return;
    }

    if (0 == strcmp(type, "tts")) {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (NULL != state && cJSON_IsString(state)) {
            xiaozhi_agent_tts_barrier_t barrier = {0};
            if (!tts_barrier_accepts(session_epoch, request_id, &barrier)) {
                const uint32_t drop_count = count_tts_barrier_drop(false);
                ESP_LOGW(TAG,
                         "[DEBUG-AI-P0] phase=barrier_drop kind=tts.%s epoch=%lu request=%lu state=%d ready=%u stop=%u evidence=%u text_drops=%lu",
                         state->valuestring, (unsigned long)session_epoch,
                         (unsigned long)request_id, (int)s_state,
                         barrier.listening_ready ? 1U : 0U,
                         barrier.listen_stop_sent ? 1U : 0U,
                         barrier.processing_evidence ? 1U : 0U,
                         (unsigned long)drop_count);
                cJSON_Delete(root);
                return;
            }
            if (xiaozhi_agent_tts_state_starts_speaking(
                    state->valuestring)) {
                const bool sentence_start =
                    0 == strcmp(state->valuestring, "sentence_start");
                if (sentence_start) {
                    ESP_LOGI(TAG, "TTS sentence_start");
                }
                if (XIAOZHI_STATE_PROCESSING == s_state) {
                    ESP_LOGI(TAG, "TTS start source=%s",
                             state->valuestring);
                    ESP_LOGI(TAG,
                             "[DEBUG-AI-TXN] phase=tts_start_recv epoch=%lu request=%lu state=%d source=%s processing_ms=%lu",
                             (unsigned long)session_epoch,
                             (unsigned long)request_id, (int)s_state,
                             state->valuestring,
                             (unsigned long)((XIAOZHI_STATE_PROCESSING == s_state) ?
                                                 pdTICKS_TO_MS(xTaskGetTickCount() -
                                                               s_processing_start_tick) :
                                                 0U));
                    if (set_state_if_current(
                            XIAOZHI_STATE_PROCESSING,
                            XIAOZHI_STATE_SPEAKING, session_epoch,
                            request_id, "tts_start") &&
                        request_is_current_and_live(session_epoch,
                                                    request_id)) {
                        ESP_LOGI(TAG,
                                 "[DEBUG-AI-TXN] phase=tts_start_commit epoch=%lu request=%lu state=%d source=%s",
                                 (unsigned long)session_epoch,
                                 (unsigned long)request_id, (int)s_state,
                                 state->valuestring);
                        notify_semantic(XIAOZHI_AGENT_EVENT_SPEAKING,
                                        session_epoch, request_id, ESP_OK);
                    }
                } else if (XIAOZHI_STATE_SPEAKING != s_state) {
                    ESP_LOGW(TAG, "Ignoring TTS start in state=%d",
                             (int)s_state);
                }
            } else if (0 == strcmp(state->valuestring, "stop")) {
                if (XIAOZHI_STATE_SPEAKING == s_state) {
                    ESP_LOGI(TAG, "TTS stop -> closing session");
                    ESP_LOGI(TAG,
                             "[DEBUG-AI-TXN] phase=tts_stop epoch=%lu request=%lu state=%d",
                             (unsigned long)session_epoch,
                             (unsigned long)request_id,
                             (int)s_state);
                    (void)close_session_if_current(
                        session_epoch, request_id, ESP_OK, true,
                        "tts_stop");
                } else {
                    ESP_LOGW(TAG, "Ignoring stale TTS stop in state=%d",
                             (int)s_state);
                }
            } else if (0 == strcmp(state->valuestring, "sentence_end")) {
                ESP_LOGI(TAG, "TTS sentence_end");
            } else {
                ESP_LOGW(TAG, "Unknown tts state: %s", state->valuestring);
            }
        } else {
            ESP_LOGW(TAG, "tts message missing state");
        }
    } else if (0 == strcmp(type, "stt")) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        if (NULL != text && cJSON_IsString(text)) {
            mark_tts_barrier_processing_evidence(
                session_epoch, request_id, "stt");
            ESP_LOGI(TAG, "STT: %s", text->valuestring);
            ESP_LOGI(TAG,
                     "[DEBUG-AI-TXN] phase=stt epoch=%lu request=%lu state=%d processing_ms=%lu",
                     (unsigned long)s_session_epoch,
                     (unsigned long)s_active_request_id, (int)s_state,
                     (unsigned long)((XIAOZHI_STATE_PROCESSING == s_state) ?
                                         pdTICKS_TO_MS(xTaskGetTickCount() -
                                                       s_processing_start_tick) :
                                         0U));
        }
    } else if (0 == strcmp(type, "llm")) {
        if (xiaozhi_agent_tts_barrier_type_is_processing_evidence(type)) {
            mark_tts_barrier_processing_evidence(
                session_epoch, request_id, "llm");
        }
    } else if (0 == strcmp(type, "goodbye")) {
        ESP_LOGI(TAG, "Server goodbye -> closing session");
        (void)close_session_if_current(session_epoch, request_id, ESP_OK,
                                       true, "goodbye");
    } else if (0 == strcmp(type, "hello")) {
        handle_server_hello(root);
    } else if (0 == strcmp(type, "error")) {
        cJSON *message = cJSON_GetObjectItem(root, "message");
        ESP_LOGW(TAG, "Server error: %s",
                 (NULL != message && cJSON_IsString(message)) ? message->valuestring : "(no message)");
        (void)close_session_if_current(session_epoch, request_id, ESP_FAIL,
                                       true, "server_error");
    } else {
        ESP_LOGW(TAG, "Unknown RX JSON type: %s", type);
    }

    cJSON_Delete(root);
}

static void handle_binary_in_ws_context(const uint8_t *data, int len,
                                        uint32_t session_epoch,
                                        uint32_t request_id)
{
    if (NULL == data || len < PROTO_HDR_SIZE ||
        !request_is_current_and_live(session_epoch, request_id)) {
        return;
    }

    uint8_t frame_type = data[0];
    uint16_t payload_size;
    memcpy(&payload_size, &data[2], 2);
    payload_size = ntohs(payload_size);

    if ((size_t)payload_size > (size_t)(len - PROTO_HDR_SIZE)) {
        return;
    }

    const uint8_t *payload = data + PROTO_HDR_SIZE;

    if (0 == frame_type) {
        xiaozhi_agent_tts_barrier_t barrier = {0};
        if (!tts_barrier_accepts(session_epoch, request_id, &barrier)) {
            const uint32_t drop_count = count_tts_barrier_drop(true);
            if (1U == drop_count || 0U == (drop_count % 20U)) {
                ESP_LOGW(TAG,
                         "[DEBUG-AI-P0] phase=barrier_drop kind=audio epoch=%lu request=%lu state=%d payload=%u ready=%u stop=%u evidence=%u audio_drops=%lu",
                         (unsigned long)session_epoch,
                         (unsigned long)request_id, (int)s_state,
                         payload_size,
                         barrier.listening_ready ? 1U : 0U,
                         barrier.listen_stop_sent ? 1U : 0U,
                         barrier.processing_evidence ? 1U : 0U,
                         (unsigned long)drop_count);
            }
        } else if (XIAOZHI_STATE_SPEAKING == s_state &&
                   request_is_current_and_live(session_epoch, request_id)) {
            notify_audio_play(payload, (int)payload_size, session_epoch,
                              request_id);
        } else {
            s_rx_audio_ignored_count++;
            if (1 == s_rx_audio_ignored_count || 0 == (s_rx_audio_ignored_count % 20)) {
                ESP_LOGW(TAG, "Ignoring audio frame in state=%d, payload=%u ignored=%lu",
                         (int)s_state, payload_size, (unsigned long)s_rx_audio_ignored_count);
            }
        }
    } else if (1 == frame_type) {
        (void)post_evt(EVT_WS_TEXT, payload, (int)payload_size,
                       session_epoch, request_id, true);
    } else {
        ESP_LOGW(TAG, "Unknown binary frame type=%u len=%d payload=%u",
                 frame_type, len, payload_size);
    }
}

static void print_heap_info(void)
{
    ESP_LOGI(TAG, "Heap free: internal=%lu, PSRAM=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void agent_task(void *arg)
{
    (void)arg;
    while (true) {
        bool starting = false;
        portENTER_CRITICAL(&s_lifecycle_lock);
        starting = s_starting;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        if (!starting) {
            break;
        }
        vTaskDelay(1U);
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_worker_active = true;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    ESP_LOGI(TAG, "agent_task started");
    print_heap_info();

    TickType_t last_heap_print = xTaskGetTickCount();

    while (s_running) {
        agent_evt_t evt;
        if (pdTRUE != xQueueReceive(s_evt_queue, &evt, pdMS_TO_TICKS(500))) {
            if ((xTaskGetTickCount() - last_heap_print) > pdMS_TO_TICKS(30000)) {
                print_heap_info();
                last_heap_print = xTaskGetTickCount();
            }
            /* PROCESSING timeout check */
            if (s_state == XIAOZHI_STATE_PROCESSING) {
                if ((xTaskGetTickCount() - s_processing_start_tick) > pdMS_TO_TICKS(PROCESSING_TIMEOUT_MS)) {
                    ESP_LOGW(TAG, "PROCESSING timeout (%d ms), closing session", PROCESSING_TIMEOUT_MS);
                    ESP_LOGW(TAG,
                             "[DEBUG-AI-TXN] phase=processing_timeout epoch=%lu request=%lu state=%d elapsed_ms=%lu",
                             (unsigned long)s_session_epoch,
                             (unsigned long)s_active_request_id,
                             (int)s_state,
                             (unsigned long)pdTICKS_TO_MS(
                                 xTaskGetTickCount() -
                                 s_processing_start_tick));
                    close_session_with_result(ESP_ERR_TIMEOUT);
                }
            }
            if (take_critical_event_failure()) {
                close_session_with_result(ESP_ERR_NO_MEM);
            }
            (void)finish_pending_vad_stop();
            continue;
        }

        if (cancel_requested(s_active_request_id) &&
            EVT_CANCEL_REQUEST != evt.type) {
            close_session();
        }

        switch (evt.type) {
        case EVT_WS_CONNECTED:
            ESP_LOGI(TAG, "WS connected event (unexpected in main loop)");
            break;

        case EVT_WS_DISCONNECTED:
            if (evt.session_epoch != s_session_epoch ||
                evt.request_id != s_active_request_id) {
                ESP_LOGW(TAG,
                         "Ignoring stale disconnect epoch=%lu request=%lu current=%lu/%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)evt.request_id,
                         (unsigned long)s_session_epoch,
                         (unsigned long)s_active_request_id);
                break;
            }
            ESP_LOGW(TAG, "WS disconnected");
            if (s_state != XIAOZHI_STATE_IDLE) {
                close_session_with_result(ESP_FAIL);
            }
            break;

        case EVT_WS_TEXT:
            if (evt.session_epoch == s_session_epoch &&
                evt.request_id == s_active_request_id && NULL != evt.data) {
                handle_text_message(evt.data, evt.len, evt.session_epoch,
                                    evt.request_id);
            } else {
                ESP_LOGW(TAG,
                         "Ignoring stale WS text epoch=%lu request=%lu current=%lu/%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)evt.request_id,
                         (unsigned long)s_session_epoch,
                         (unsigned long)s_active_request_id);
            }
            break;

        case EVT_WAKE_WORD:
            if (evt.session_epoch != s_session_epoch ||
                0U == evt.request_id ||
                (0U != s_active_request_id &&
                 evt.request_id == s_active_request_id)) {
                ESP_LOGW(TAG,
                         "[DEBUG-AI-P0] phase=wake_drop epoch=%lu current_epoch=%lu request=%lu current_request=%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)s_session_epoch,
                         (unsigned long)evt.request_id,
                         (unsigned long)s_active_request_id);
                if (evt.session_epoch == s_session_epoch) {
                    notify_semantic(XIAOZHI_AGENT_EVENT_FAILED,
                                    s_session_epoch, evt.request_id,
                                    ESP_ERR_INVALID_STATE);
                }
                break;
            }
            ESP_LOGI(TAG, "Wake word received request=%lu state=%d",
                     (unsigned long)evt.request_id, (int)s_state);
            if (s_state == XIAOZHI_STATE_SPEAKING ||
                s_state == XIAOZHI_STATE_PROCESSING) {
                if (!ws_is_connected()) {
                    ESP_LOGW(TAG, "Wake interrupt while WS disconnected, reopening session");
                    close_session_with_result(ESP_ERR_INVALID_STATE);
                    if (!activate_request_if_live(evt.request_id,
                                                  "wake_reopen_disconnected")) {
                        break;
                    }
                    reset_tx_stats();
                    (void)open_session_and_start_listening("wake interrupt after disconnect");
                    break;
                }
                restart_listening_after_wake(evt.request_id, true);
            } else if (s_state == XIAOZHI_STATE_LISTENING &&
                       s_config.allow_listening_rewake) {
                if (!ws_is_connected()) {
                    ESP_LOGW(TAG,
                             "Listening re-wake while WS disconnected, reopening session");
                    close_session_with_result(ESP_ERR_INVALID_STATE);
                    if (!activate_request_if_live(
                            evt.request_id, "listening_rewake_disconnected")) {
                        break;
                    }
                    reset_tx_stats();
                    (void)open_session_and_start_listening(
                        "listening re-wake after disconnect");
                    break;
                }
                restart_listening_after_wake(evt.request_id, false);
            } else if (s_state == XIAOZHI_STATE_IDLE) {
                if (!activate_request_if_live(evt.request_id,
                                              "wake_idle")) {
                    break;
                }
                reset_tx_stats();
                (void)open_session_and_start_listening("wake word");
            } else {
                ESP_LOGW(TAG, "Wake word ignored in state=%d", (int)s_state);
                notify_semantic(XIAOZHI_AGENT_EVENT_FAILED, s_session_epoch,
                                evt.request_id, ESP_ERR_INVALID_STATE);
            }
            break;

        case EVT_VAD_START:
            if (evt.session_epoch == s_session_epoch) {
                (void)accept_vad_start(evt.request_id);
            } else {
                ESP_LOGW(TAG,
                         "[DEBUG-AI-P0] phase=vad_start_drop epoch=%lu current_epoch=%lu request=%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)s_session_epoch,
                         (unsigned long)evt.request_id);
            }
            break;

        case EVT_VAD_END:
            if (evt.session_epoch == s_session_epoch) {
                (void)accept_vad_end(evt.request_id);
            } else {
                ESP_LOGW(TAG,
                         "[DEBUG-AI-P0] phase=vad_end_drop epoch=%lu current_epoch=%lu request=%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)s_session_epoch,
                         (unsigned long)evt.request_id);
            }
            break;

        case EVT_TX_PROGRESS:
            if (evt.session_epoch == s_session_epoch &&
                evt.request_id == s_active_request_id) {
                ESP_LOGI(TAG,
                         "[DEBUG-AI-TXN] phase=vad_stop_progress epoch=%lu request=%lu sent_frames=%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)evt.request_id,
                         (unsigned long)s_tx_frame_count);
                (void)finish_pending_vad_stop();
            }
            break;

        case EVT_TX_ERROR:
            if (evt.session_epoch != s_session_epoch ||
                evt.request_id != s_active_request_id) {
                ESP_LOGW(TAG,
                         "Ignoring stale TX error epoch=%lu request=%lu current=%lu/%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)evt.request_id,
                         (unsigned long)s_session_epoch,
                         (unsigned long)s_active_request_id);
                break;
            }
            ESP_LOGW(TAG, "Audio TX error threshold reached, closing session");
            close_session_with_result(ESP_FAIL);
            break;

        case EVT_CANCEL_REQUEST:
            if (evt.session_epoch != s_session_epoch) {
                ESP_LOGW(TAG,
                         "Ignoring stale cancel epoch=%lu request=%lu current=%lu/%lu",
                         (unsigned long)evt.session_epoch,
                         (unsigned long)evt.request_id,
                         (unsigned long)s_session_epoch,
                         (unsigned long)s_active_request_id);
                clear_cancel_request(evt.request_id);
            } else if (evt.request_id == s_active_request_id) {
                close_session_with_result(ESP_ERR_INVALID_STATE);
            } else {
                clear_cancel_request(evt.request_id);
            }
            break;
        }

        if (NULL != evt.data) {
            heap_caps_free(evt.data);
        }
        if (take_critical_event_failure()) {
            close_session_with_result(ESP_ERR_NO_MEM);
        }
        (void)finish_pending_vad_stop();
    }

    ws_disconnect();
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_running = false;
    s_worker_active = false;
    s_agent_task = NULL;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    ESP_LOGI(TAG, "agent_task exiting");
    vTaskDelete(NULL);
}

esp_err_t xiaozhi_agent_init(const xiaozhi_agent_config_t *config)
{
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_initialized) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_OK;
    }
    if (s_initializing || s_starting || s_running || s_worker_active) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_initializing = true;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    s_config = *config;
    s_state = XIAOZHI_STATE_IDLE;
    s_ws_url[0] = '\0';
    s_ws_token[0] = '\0';
    s_session_id[0] = '\0';
    s_running = false;
    s_worker_active = false;
    s_starting = false;
    s_stop_requested = false;
    s_ws_binding = NULL;
    s_critical_event_failed = false;
    s_critical_failure_session_epoch = 0U;
    s_critical_failure_request_id = 0U;
    s_session_epoch = 0U;
    s_active_request_id = 0U;
    memset(s_cancel_request_ids, 0, sizeof(s_cancel_request_ids));
    s_legacy_request_id = 0U;
    reset_tx_stats();
    s_rx_audio_ignored_count = 0;
    xiaozhi_agent_tts_barrier_reset(&s_tts_barrier, 0U, 0U);
    s_tts_barrier_text_drop_count = 0U;
    s_tts_barrier_audio_drop_count = 0U;

    s_ws_lock = xSemaphoreCreateMutex();
    s_evt_queue = xQueueCreate(AGENT_EVENT_QUEUE_LENGTH, sizeof(agent_evt_t));
    if (NULL == s_ws_lock || NULL == s_evt_queue) {
        if (NULL != s_evt_queue) {
            vQueueDelete(s_evt_queue);
            s_evt_queue = NULL;
        }
        if (NULL != s_ws_lock) {
            vSemaphoreDelete(s_ws_lock);
            s_ws_lock = NULL;
        }
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_initializing = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_initialized = true;
    s_initializing = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return ESP_OK;
}

esp_err_t xiaozhi_agent_start(void)
{
    if (!s_initialized || NULL == s_evt_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_starting || s_running || s_worker_active || NULL != s_agent_task) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_starting = true;
    s_stop_requested = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);

    discard_pending_events();

    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_stop_requested) {
        s_starting = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_critical_event_failed = false;
    s_critical_failure_session_epoch = 0U;
    s_critical_failure_request_id = 0U;
    s_active_request_id = 0U;
    memset(s_cancel_request_ids, 0, sizeof(s_cancel_request_ids));
    s_session_epoch = next_nonzero(s_session_epoch);
    reset_tx_stats();
    xiaozhi_agent_tts_barrier_reset(&s_tts_barrier, 0U, 0U);
    s_tts_barrier_text_drop_count = 0U;
    s_tts_barrier_audio_drop_count = 0U;
    s_running = true;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    TaskHandle_t created_task = NULL;
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(
        agent_task, "agent_main", AGENT_TASK_STACK, NULL,
        AGENT_TASK_PRIO, &created_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (pdPASS != ret) {
        s_running = false;
        s_agent_task = NULL;
    } else {
        s_agent_task = created_task;
    }
    const bool cancelled = s_stop_requested;
    s_starting = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (cancelled) {
        return ESP_ERR_INVALID_STATE;
    }
    return (pdPASS == ret) ? ESP_OK : ESP_FAIL;
}

esp_err_t xiaozhi_agent_stop(void)
{
    return xiaozhi_agent_stop_ex(AGENT_STOP_DEFAULT_TIMEOUT_MS);
}

esp_err_t xiaozhi_agent_stop_ex(uint32_t timeout_ms)
{
    if (0U == timeout_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_stop_requested = true;
    s_running = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    const TickType_t start_tick = xTaskGetTickCount();
    while (true) {
        bool active = false;
        portENTER_CRITICAL(&s_lifecycle_lock);
        active = s_starting || s_worker_active || NULL != s_agent_task;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        if (!active) {
            break;
        }
        if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(timeout_ms)) {
            ESP_LOGE(TAG, "agent worker stop timeout after %lums",
                     (unsigned long)timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(AGENT_STOP_POLL_MS));
    }
    discard_pending_events();
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_active_request_id = 0U;
    memset(s_cancel_request_ids, 0, sizeof(s_cancel_request_ids));
    s_stop_requested = false;
    s_critical_event_failed = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    reset_tx_stats();
    reset_tts_barrier(0U, 0U);
    set_state(XIAOZHI_STATE_IDLE);
    return ESP_OK;
}

xiaozhi_agent_state_t xiaozhi_agent_get_state(void)
{
    return s_state;
}

esp_err_t xiaozhi_agent_send_audio(const uint8_t *data, int len)
{
    if (s_state != XIAOZHI_STATE_LISTENING || 0U == s_active_request_id) {
        return ESP_OK;
    }
    return xiaozhi_agent_send_audio_ex(s_active_request_id, data, len);
}

esp_err_t xiaozhi_agent_send_audio_ex(uint32_t request_id,
                                      const uint8_t *data, int len)
{
    if (!request_is_current_and_live(s_session_epoch, request_id) ||
        s_state != XIAOZHI_STATE_LISTENING) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t session_epoch = s_session_epoch;
    esp_err_t ret = ws_send_audio_frame(data, len, session_epoch,
                                        request_id);
    if (ESP_OK != ret) {
        s_tx_fail_count++;
        s_tx_consecutive_fail_count++;
        ESP_LOGW(TAG, "Audio TX failed: ret=%s opus_len=%d fail=%lu consecutive=%lu ok=%lu",
                 esp_err_to_name(ret), len,
                 (unsigned long)s_tx_fail_count,
                 (unsigned long)s_tx_consecutive_fail_count,
                 (unsigned long)s_tx_frame_count);
        if (s_tx_consecutive_fail_count >= AUDIO_TX_FAIL_CLOSE_THRESHOLD) {
            (void)post_evt(EVT_TX_ERROR, NULL, 0, s_session_epoch,
                           request_id, true);
        }
        return ret;
    }

    s_tx_frame_count++;
    s_tx_consecutive_fail_count = 0;
    const bool vad_stop_ready =
        xiaozhi_agent_vad_stop_policy_note_tx_success(
            &s_vad_stop_policy, request_id);
    if (vad_stop_ready && s_vad_stop_pending) {
        ESP_LOGI(TAG,
                 "[DEBUG-AI-TXN] phase=vad_stop_progress_queued epoch=%lu request=%lu sent_frames=%lu",
                 (unsigned long)s_session_epoch, (unsigned long)request_id,
                 (unsigned long)s_tx_frame_count);
        (void)post_evt(EVT_TX_PROGRESS, NULL, 0, s_session_epoch,
                       request_id, true);
    }
    if (0 == s_tx_min_opus_len || (uint32_t)len < s_tx_min_opus_len) {
        s_tx_min_opus_len = (uint32_t)len;
    }
    if ((uint32_t)len > s_tx_max_opus_len) {
        s_tx_max_opus_len = (uint32_t)len;
    }
    if (1 == s_tx_frame_count || 0 == (s_tx_frame_count % 50)) {
        ESP_LOGI(TAG, "Audio TX ok: frames=%lu opus_len=%d min=%lu max=%lu fail=%lu",
                 (unsigned long)s_tx_frame_count, len,
                 (unsigned long)s_tx_min_opus_len,
                 (unsigned long)s_tx_max_opus_len,
                 (unsigned long)s_tx_fail_count);
    }
    return ESP_OK;
}

esp_err_t xiaozhi_agent_notify_vad_end(void)
{
    if (0U == s_active_request_id) {
        return ESP_OK;
    }
    return xiaozhi_agent_notify_vad_end_ex(s_active_request_id);
}

esp_err_t xiaozhi_agent_notify_vad_start(void)
{
    if (0U == s_active_request_id) {
        return ESP_OK;
    }
    return xiaozhi_agent_notify_vad_start_ex(s_active_request_id);
}

esp_err_t xiaozhi_agent_notify_vad_start_ex(uint32_t request_id)
{
    ESP_LOGI(TAG, "notify_vad_start request=%lu current=%lu state=%d",
             (unsigned long)request_id,
             (unsigned long)s_active_request_id, (int)s_state);
    if (!request_is_current_and_live(s_session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    return post_evt(EVT_VAD_START, NULL, 0, s_session_epoch, request_id, true);
}

esp_err_t xiaozhi_agent_notify_vad_end_ex(uint32_t request_id)
{
    ESP_LOGI(TAG, "notify_vad_end request=%lu current=%lu state=%d",
             (unsigned long)request_id,
             (unsigned long)s_active_request_id, (int)s_state);
    if (!request_is_current_and_live(s_session_epoch, request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    return post_evt(EVT_VAD_END, NULL, 0, s_session_epoch, request_id, true);
}

esp_err_t xiaozhi_agent_notify_wake_word(void)
{
    s_legacy_request_id = next_nonzero(s_legacy_request_id);
    return xiaozhi_agent_notify_wake_word_ex(s_legacy_request_id);
}

esp_err_t xiaozhi_agent_notify_wake_word_ex(uint32_t request_id)
{
    ESP_LOGI(TAG, "notify_wake_word request=%lu current=%lu state=%d",
             (unsigned long)request_id,
             (unsigned long)s_active_request_id, (int)s_state);
    if (0U == request_id || !s_running || cancel_requested(request_id)) {
        return ESP_ERR_INVALID_STATE;
    }
    return post_evt(EVT_WAKE_WORD, NULL, 0, s_session_epoch, request_id, true);
}

esp_err_t xiaozhi_agent_cancel_request(uint32_t request_id)
{
    if (0U == request_id || !s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool snapshot_stored = set_cancel_request(request_id);
    const esp_err_t result = post_evt(EVT_CANCEL_REQUEST, NULL, 0,
                                      s_session_epoch, request_id, true);
    if (ESP_OK != result && snapshot_stored) {
        ESP_LOGW(TAG,
                 "cancel queue unavailable request=%lu; worker will observe cancel snapshot",
                 (unsigned long)request_id);
    } else if (ESP_OK != result) {
        ESP_LOGE(TAG,
                 "cancel rejected request=%lu: snapshot full and queue unavailable",
                 (unsigned long)request_id);
        return result;
    }
    return ESP_OK;
}
