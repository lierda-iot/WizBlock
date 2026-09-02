#include "companion_agent_adapter.h"
#include "companion_agent_binding_policy.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "xiaozhi_agent.h"

#include <stddef.h>

static const char *TAG = "companion_agent";
static companion_agent_adapter_config_t s_config;
static bool s_started;
static bool s_starting;
static bool s_start_failed;
static volatile bool s_agent_operational;
static volatile uint32_t s_generation;
static volatile uint32_t s_wake_seq;
static volatile uint32_t s_request_id;
static uint32_t s_next_request_id;
static portMUX_TYPE s_binding_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t request_id;
} agent_binding_t;

static agent_binding_t binding_snapshot(void)
{
    agent_binding_t binding = {0};
    portENTER_CRITICAL(&s_binding_lock);
    binding.generation = s_generation;
    binding.wake_seq = s_wake_seq;
    binding.request_id = s_request_id;
    portEXIT_CRITICAL(&s_binding_lock);
    return binding;
}

static uint32_t allocate_request_id_locked(void)
{
    s_next_request_id++;
    if (0U == s_next_request_id) {
        s_next_request_id = 1U;
    }
    return s_next_request_id;
}

static companion_agent_event_type_t map_agent_event(
    xiaozhi_agent_event_type_t type)
{
    switch (type) {
    case XIAOZHI_AGENT_EVENT_CONNECTING:
        return COMPANION_AGENT_EVENT_CONNECTING;
    case XIAOZHI_AGENT_EVENT_LISTENING_READY:
        return COMPANION_AGENT_EVENT_LISTENING_READY;
    case XIAOZHI_AGENT_EVENT_PROCESSING:
        return COMPANION_AGENT_EVENT_PROCESSING;
    case XIAOZHI_AGENT_EVENT_SPEAKING:
        return COMPANION_AGENT_EVENT_SPEAKING;
    case XIAOZHI_AGENT_EVENT_CLOSED:
        return COMPANION_AGENT_EVENT_CLOSED;
    case XIAOZHI_AGENT_EVENT_FAILED:
    default:
        return COMPANION_AGENT_EVENT_FAILED;
    }
}

static void on_agent_event(const xiaozhi_agent_event_t *event,
                           void *user_ctx)
{
    (void)user_ctx;
    if (NULL == event) {
        return;
    }
    const agent_binding_t binding = binding_snapshot();
    if (0U == event->request_id ||
        event->request_id != binding.request_id) {
        ESP_LOGW(TAG,
                 "drop stale semantic request=%lu current=%lu epoch=%lu type=%d",
                 (unsigned long)event->request_id,
                 (unsigned long)binding.request_id,
                 (unsigned long)event->session_epoch, (int)event->type);
        return;
    }
    const companion_agent_event_t mapped = {
        .type = map_agent_event(event->type),
        .generation = binding.generation,
        .wake_seq = binding.wake_seq,
        .session_epoch = event->session_epoch,
        .request_id = event->request_id,
        .result = event->result,
    };
    if (NULL != s_config.on_event) {
        s_config.on_event(&mapped, s_config.user_ctx);
    }
    if (COMPANION_AGENT_EVENT_LISTENING_READY == mapped.type ||
        COMPANION_AGENT_EVENT_PROCESSING == mapped.type ||
        COMPANION_AGENT_EVENT_SPEAKING == mapped.type) {
        s_agent_operational = true;
    }
    if (COMPANION_AGENT_EVENT_CLOSED == mapped.type ||
        COMPANION_AGENT_EVENT_FAILED == mapped.type) {
        portENTER_CRITICAL(&s_binding_lock);
        if (event->request_id == s_request_id) {
            s_generation = 0U;
            s_wake_seq = 0U;
            s_request_id = 0U;
        }
        portEXIT_CRITICAL(&s_binding_lock);
    }
}

static void on_audio_event(const xiaozhi_audio_event_t *event,
                           void *user_ctx)
{
    (void)user_ctx;
    if (NULL == event || NULL == s_config.on_audio_event) {
        return;
    }
    const agent_binding_t binding = binding_snapshot();
    const companion_agent_binding_id_t current_id = {
        .generation = binding.generation,
        .wake_seq = binding.wake_seq,
        .request_id = binding.request_id,
    };
    const bool stop = XIAOZHI_AUDIO_EVENT_STOP == event->type;
    const companion_agent_binding_route_t route =
        companion_agent_binding_route_audio(&current_id, event->request_id);
    if (COMPANION_AGENT_BINDING_DROP == route) {
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=adapter_audio_drop request=%lu current=%lu stop=%u",
                 (unsigned long)event->request_id,
                 (unsigned long)binding.request_id, stop ? 1U : 0U);
        return;
    }
    const companion_agent_audio_event_t mapped = {
        .data = event->opus_data,
        .length = event->len,
        .generation = binding.generation,
        .wake_seq = binding.wake_seq,
        .session_epoch = event->session_epoch,
        .request_id = event->request_id,
        .stop = stop,
    };
    s_config.on_audio_event(&mapped, s_config.user_ctx);
}

esp_err_t companion_agent_adapter_start(
    const companion_agent_adapter_config_t *config)
{
    if (NULL == config || NULL == config->ota_url ||
        NULL == config->activation_url || NULL == config->device_mac ||
        NULL == config->client_id || NULL == config->lang ||
        NULL == config->board_name || NULL == config->app_version ||
        NULL == config->on_event || NULL == config->on_audio_event ||
        NULL == config->on_error) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_binding_lock);
    if (s_started) {
        portEXIT_CRITICAL(&s_binding_lock);
        return ESP_OK;
    }
    if (s_starting || s_start_failed) {
        portEXIT_CRITICAL(&s_binding_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_starting = true;
    portEXIT_CRITICAL(&s_binding_lock);
    esp_err_t result = ESP_OK;
    s_config = *config;
    portENTER_CRITICAL(&s_binding_lock);
    s_generation = 0U;
    s_wake_seq = 0U;
    s_request_id = 0U;
    s_next_request_id = 0U;
    portEXIT_CRITICAL(&s_binding_lock);
    const xiaozhi_agent_config_t agent_config = {
        .ota_url = config->ota_url,
        .activation_url = config->activation_url,
        .device_mac = config->device_mac,
        .client_id = config->client_id,
        .lang = config->lang,
        .board_name = config->board_name,
        .app_version = config->app_version,
        .on_event = on_agent_event,
        .on_audio_event = on_audio_event,
        .allow_listening_rewake = true,
        .client_manages_listen_stop = true,
        .user_ctx = NULL,
    };
    result = xiaozhi_agent_init(&agent_config);
    if (ESP_OK == result) {
        result = xiaozhi_agent_start();
    }
    if (ESP_OK == result) {
        portENTER_CRITICAL(&s_binding_lock);
        s_started = true;
        s_agent_operational = true;
        s_starting = false;
        portEXIT_CRITICAL(&s_binding_lock);
        ESP_LOGI(TAG,
                 "agent adapter ready mac=%s product_deadline_owner=controller",
                 config->device_mac);
    } else {
        portENTER_CRITICAL(&s_binding_lock);
        s_starting = false;
        s_start_failed = true;
        s_agent_operational = false;
        portEXIT_CRITICAL(&s_binding_lock);
    }
    return result;
}

esp_err_t companion_agent_adapter_begin(uint32_t generation,
                                        uint32_t wake_seq,
                                        uint32_t *request_id)
{
    if (NULL == request_id || 0U == generation || 0U == wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started || !s_agent_operational) {
        return ESP_ERR_INVALID_STATE;
    }
    agent_binding_t previous = {0};
    uint32_t reserved_request_id = 0U;
    portENTER_CRITICAL(&s_binding_lock);
    previous.generation = s_generation;
    previous.wake_seq = s_wake_seq;
    previous.request_id = s_request_id;
    s_generation = generation;
    s_wake_seq = wake_seq;
    reserved_request_id = allocate_request_id_locked();
    s_request_id = reserved_request_id;
    portEXIT_CRITICAL(&s_binding_lock);
    const esp_err_t result =
        xiaozhi_agent_notify_wake_word_ex(reserved_request_id);
    if (ESP_OK != result) {
        bool cancel_previous = false;
        portENTER_CRITICAL(&s_binding_lock);
        if (reserved_request_id == s_request_id) {
            s_generation = 0U;
            s_wake_seq = 0U;
            s_request_id = 0U;
            cancel_previous = 0U != previous.request_id;
        }
        portEXIT_CRITICAL(&s_binding_lock);
        if (cancel_previous) {
            (void)xiaozhi_agent_cancel_request(previous.request_id);
        }
        ESP_LOGW(TAG,
                 "[DEBUG-AI-P0] phase=agent_submit_reject new_request=%lu previous_request=%lu result=%s previous_cancel=%u",
                 (unsigned long)reserved_request_id,
                 (unsigned long)previous.request_id,
                 esp_err_to_name(result), cancel_previous ? 1U : 0U);
        return result;
    }
    *request_id = reserved_request_id;
    return ESP_OK;
}

esp_err_t companion_agent_adapter_notify_vad_end(uint32_t generation,
                                                 uint32_t wake_seq,
                                                 uint32_t request_id)
{
    const agent_binding_t binding = binding_snapshot();
    if (!s_started || !s_agent_operational ||
        generation != binding.generation || wake_seq != binding.wake_seq ||
        request_id != binding.request_id) {
        return ESP_ERR_INVALID_STATE;
    }
    return xiaozhi_agent_notify_vad_end_ex(request_id);
}

esp_err_t companion_agent_adapter_notify_vad_start(uint32_t generation,
                                                   uint32_t wake_seq,
                                                   uint32_t request_id)
{
    const agent_binding_t binding = binding_snapshot();
    if (!s_started || 0U == request_id ||
        generation != binding.generation || wake_seq != binding.wake_seq ||
        request_id != binding.request_id) {
        return ESP_ERR_INVALID_STATE;
    }
    return xiaozhi_agent_notify_vad_start_ex(request_id);
}

esp_err_t companion_agent_adapter_send_audio(uint32_t generation,
                                             uint32_t wake_seq,
                                             uint32_t request_id,
                                             const uint8_t *data,
                                             int length)
{
    const agent_binding_t binding = binding_snapshot();
    if (NULL == data || 0 >= length || generation != binding.generation ||
        wake_seq != binding.wake_seq || request_id != binding.request_id) {
        return ESP_ERR_INVALID_STATE;
    }
    return (s_started && s_agent_operational) ?
           xiaozhi_agent_send_audio_ex(request_id, data, length) :
           ESP_ERR_INVALID_STATE;
}

esp_err_t companion_agent_adapter_cancel(uint32_t generation,
                                         uint32_t wake_seq,
                                         uint32_t request_id)
{
    const agent_binding_t binding = binding_snapshot();
    if (!s_started || generation != binding.generation ||
        wake_seq != binding.wake_seq || request_id != binding.request_id) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Retire the adapter binding before the asynchronous cancel completes. */
    portENTER_CRITICAL(&s_binding_lock);
    if (generation == s_generation && wake_seq == s_wake_seq &&
        request_id == s_request_id) {
        s_generation = 0U;
        s_wake_seq = 0U;
        s_request_id = 0U;
    }
    portEXIT_CRITICAL(&s_binding_lock);
    const esp_err_t result = xiaozhi_agent_cancel_request(request_id);
    ESP_LOGI(TAG,
             "[DEBUG-AI-P0] phase=agent_binding_retired generation=%lu wake_seq=%lu request=%lu result=%s",
             (unsigned long)generation, (unsigned long)wake_seq,
             (unsigned long)request_id, esp_err_to_name(result));
    return result;
}

esp_err_t companion_agent_adapter_retire_binding(uint32_t generation,
                                                 uint32_t wake_seq,
                                                 uint32_t request_id)
{
    bool retired = false;
    portENTER_CRITICAL(&s_binding_lock);
    companion_agent_binding_id_t current = {
        .generation = s_generation,
        .wake_seq = s_wake_seq,
        .request_id = s_request_id,
    };
    const companion_agent_binding_id_t expected = {
        .generation = generation,
        .wake_seq = wake_seq,
        .request_id = request_id,
    };
    if (s_started && companion_agent_binding_retire_if_current(
                         &current, &expected)) {
        s_generation = current.generation;
        s_wake_seq = current.wake_seq;
        s_request_id = current.request_id;
        retired = true;
    }
    portEXIT_CRITICAL(&s_binding_lock);
    const esp_err_t result = retired ? ESP_OK : ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG,
             "[DEBUG-AI-P0] phase=agent_binding_retired generation=%lu wake_seq=%lu request=%lu transport=preserved result=%s",
             (unsigned long)generation, (unsigned long)wake_seq,
             (unsigned long)request_id, esp_err_to_name(result));
    return result;
}

esp_err_t companion_agent_adapter_restart(void)
{
    const esp_err_t stop_result = xiaozhi_agent_stop_ex(3000U);
    if (ESP_OK != stop_result) {
        s_agent_operational = false;
        s_config.on_error(stop_result, s_config.user_ctx);
        return stop_result;
    }
    const esp_err_t start_result = xiaozhi_agent_start();
    s_agent_operational = ESP_OK == start_result;
    if (ESP_OK != start_result) {
        s_config.on_error(start_result, s_config.user_ctx);
    }
    if (ESP_OK == start_result) {
        portENTER_CRITICAL(&s_binding_lock);
        s_generation = 0U;
        s_wake_seq = 0U;
        s_request_id = 0U;
        portEXIT_CRITICAL(&s_binding_lock);
    }
    return start_result;
}

bool companion_agent_adapter_is_listening(uint32_t generation,
                                          uint32_t wake_seq,
                                          uint32_t request_id)
{
    const agent_binding_t binding = binding_snapshot();
    return s_started && s_agent_operational &&
           generation == binding.generation &&
           wake_seq == binding.wake_seq && request_id == binding.request_id &&
           XIAOZHI_STATE_LISTENING == xiaozhi_agent_get_state();
}
