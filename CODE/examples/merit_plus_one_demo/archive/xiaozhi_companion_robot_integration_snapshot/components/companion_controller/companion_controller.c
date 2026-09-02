#include "companion_controller.h"

#include "companion_controller_model.h"
#include "companion_ui.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CONTROLLER_QUEUE_DEPTH 24U
#define CONTROLLER_TASK_STACK 8192U
#define CONTROLLER_TASK_PRIORITY 6U
#define CONTROLLER_LOOP_MS 100U
#define CONTROLLER_UPLOAD_LOG_INTERVAL 50U
#define CONTROLLER_PROMPT_URL "file://spiffs_data/dingding.wav"
#define CONTROLLER_MERIT_PROMPT_URL "file://spiffs_data/merit_tap.wav"
#define CONTROLLER_ROAM_RETRY_MS 100U
#define CONTROLLER_CORE_STOP_TIMEOUT_MS 3000U
#define CONTROLLER_SEQUENCE_HALF_RANGE 0x80000000U

typedef enum {
    CONTROLLER_EVENT_STARTUP_COMPLETE = 0,
    CONTROLLER_EVENT_AUDIO_WAKE,
    CONTROLLER_EVENT_VAD_END,
    CONTROLLER_EVENT_DOA,
    CONTROLLER_EVENT_MOTION_PROGRESS,
    CONTROLLER_EVENT_MOTION_DONE,
    CONTROLLER_EVENT_AGENT_SEMANTIC,
    CONTROLLER_EVENT_NETWORK,
    CONTROLLER_EVENT_CAPABILITY,
    CONTROLLER_EVENT_MERIT_TAP,
} controller_event_type_t;

typedef struct {
    controller_event_type_t type;
    uint32_t generation;
    uint32_t wake_seq;
    union {
        struct {
            bool core_ready;
        } startup;
        struct {
            esp_err_t result;
        } wake;
        companion_doa_result_t doa;
        struct {
            companion_motion_command_t command;
            companion_motion_progress_t progress;
        } motion_progress;
        struct {
            companion_motion_command_t command;
            companion_motion_result_t result;
        } motion;
        companion_agent_event_t agent_semantic;
        struct {
            companion_network_snapshot_t snapshot;
        } network;
        struct {
            companion_capability_t capability;
            bool available;
            esp_err_t error;
            uint32_t revision;
        } capability;
        struct {
            companion_merit_result_t result;
            uint64_t timestamp_us;
        } merit;
    } data;
} controller_event_t;

typedef enum {
    CONTROLLER_ROAM_DISABLED = 0,
    CONTROLLER_ROAM_BLOCKED,
    CONTROLLER_ROAM_DELAYED,
    CONTROLLER_ROAM_RUNNING,
    CONTROLLER_ROAM_COOLDOWN,
} controller_roam_state_t;

static const char *TAG = "companion_ctrl";
static companion_controller_model_t s_model;
static companion_controller_config_t s_config;
static companion_controller_stats_t s_stats;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_model_lock;
static bool s_capability_latest_available[COMPANION_CAPABILITY_COUNT];
static esp_err_t s_capability_latest_error[COMPANION_CAPABILITY_COUNT];
static uint32_t s_capability_latest_revisions[COMPANION_CAPABILITY_COUNT];
static volatile bool s_started;
static bool s_starting;
static volatile bool s_network_ready;
static companion_network_snapshot_t s_network_snapshot;
static companion_network_snapshot_t s_network_latest_snapshot;
static volatile bool s_upload_gate_open;
static volatile uint32_t s_upload_generation;
static volatile uint32_t s_upload_wake_seq;
static volatile uint32_t s_upload_request_id;
static controller_roam_state_t s_roam_state;
static uint32_t s_roam_stop_ms;
static uint64_t s_next_roam_ms;
static companion_turn_direction_t s_look_direction;
static uint32_t s_agent_request_id;
static uint32_t s_agent_session_epoch;
static uint32_t s_agent_generation;
static uint32_t s_agent_wake_seq;
static uint32_t s_doa_request_id;
static uint32_t s_next_motion_request_id;
static uint32_t s_wake_motion_request_id;
static uint32_t s_roam_motion_request_id;
static bool s_emergency_cancel_requested;
static uint32_t s_sw3_latest_revision;
static uint32_t s_sw3_applied_revision;
static bool s_doa_debug_valid;
static int16_t s_doa_remaining_deg;
static bool s_agent_recovery_attempted;
static bool s_invariant_fault_latched;
static bool s_merit_bubble_active;
static uint32_t s_merit_bubble_epoch;
static uint64_t s_merit_bubble_start_ms;
static uint32_t s_merit_bubble_repeat_count;
static uint32_t s_merit_prompt_seq;
static bool s_merit_sample_valid;
static uint32_t s_merit_last_sample_seq;
static uint32_t s_merit_sample_generation;
static uint32_t s_merit_sample_wake_seq;
static portMUX_TYPE s_fact_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t session_epoch;
    uint32_t request_id;
} controller_agent_binding_t;

static void apply_decision(const companion_controller_decision_t *decision,
                           uint32_t generation, uint32_t wake_seq);

static void request_emergency_exit(void)
{
    portENTER_CRITICAL(&s_fact_lock);
    s_emergency_cancel_requested = true;
    portEXIT_CRITICAL(&s_fact_lock);
}

static bool take_emergency_exit(void)
{
    bool requested = false;
    portENTER_CRITICAL(&s_fact_lock);
    requested = s_emergency_cancel_requested;
    s_emergency_cancel_requested = false;
    portEXIT_CRITICAL(&s_fact_lock);
    return requested;
}

static bool merit_sample_is_newer(uint32_t sample_seq,
                                  uint32_t previous_seq)
{
    if (0U == sample_seq || sample_seq == previous_seq) {
        return false;
    }
    return (uint32_t)(sample_seq - previous_seq) <
           CONTROLLER_SEQUENCE_HALF_RANGE;
}

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static esp_err_t apply_model_input(
    const companion_controller_input_t *input,
    companion_controller_output_t *output)
{
    companion_controller_output_t ignored = {0};
    return companion_controller_model_apply(
        &s_model, input, (NULL != output) ? output : &ignored);
}

static esp_err_t model_finish_startup(bool core_ready, uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_STARTUP_COMPLETE,
        .now_ms = timestamp_ms,
        .data.startup.core_ready = core_ready,
    };
    return apply_model_input(&input, NULL);
}

static esp_err_t model_reserve_wake(uint64_t timestamp_ms,
                                    uint32_t *generation,
                                    uint32_t *wake_seq)
{
    if (NULL == generation || NULL == wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_RESERVE_WAKE,
        .now_ms = timestamp_ms,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = apply_model_input(&input, &output);
    if (ESP_OK == result) {
        *generation = output.generation;
        *wake_seq = output.wake_seq;
    }
    return result;
}

static esp_err_t model_mark_locating_started(uint32_t generation,
                                             uint32_t wake_seq,
                                             uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_LOCATING_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = timestamp_ms,
    };
    return apply_model_input(&input, NULL);
}

static esp_err_t model_on_doa(uint32_t generation, uint32_t wake_seq,
                              bool valid, float relative_deg,
                              companion_controller_decision_t *decision)
{
    if (NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_DOA_COMPLETED,
        .generation = generation,
        .wake_seq = wake_seq,
        .data.doa = {
            .valid = valid,
            .relative_deg = relative_deg,
        },
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = apply_model_input(&input, &output);
    *decision = output.decision;
    return result;
}

static esp_err_t model_mark_motion_started(uint32_t generation,
                                           uint32_t wake_seq,
                                           uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_MOTION_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = timestamp_ms,
    };
    return apply_model_input(&input, NULL);
}

static esp_err_t model_on_motion_done(
    uint32_t generation, uint32_t wake_seq,
    companion_controller_decision_t *decision)
{
    if (NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_MOTION_COMPLETED,
        .generation = generation,
        .wake_seq = wake_seq,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = apply_model_input(&input, &output);
    *decision = output.decision;
    return result;
}

static esp_err_t model_mark_agent_accepted(uint32_t generation,
                                           uint32_t wake_seq,
                                           uint32_t request_id,
                                           uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_ACCEPTED,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = timestamp_ms,
        .data.agent_accepted.request_id = request_id,
    };
    return apply_model_input(&input, NULL);
}

static esp_err_t model_on_agent_semantic(
    uint32_t generation, uint32_t wake_seq, uint32_t session_epoch,
    uint32_t request_id, companion_agent_semantic_t semantic,
    esp_err_t semantic_result, uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_AGENT_SEMANTIC,
        .generation = generation,
        .wake_seq = wake_seq,
        .now_ms = timestamp_ms,
        .data.agent = {
            .session_epoch = session_epoch,
            .request_id = request_id,
            .semantic = semantic,
            .result = semantic_result,
        },
    };
    return apply_model_input(&input, NULL);
}

static esp_err_t model_on_vad_end(uint32_t generation, uint32_t wake_seq,
                                  bool *notify_agent)
{
    if (NULL == notify_agent) {
        return ESP_ERR_INVALID_ARG;
    }
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_VAD_END,
        .generation = generation,
        .wake_seq = wake_seq,
    };
    companion_controller_output_t output = {0};
    const esp_err_t result = apply_model_input(&input, &output);
    *notify_agent = output.notify_agent;
    return result;
}

static bool model_toggle_roam(void)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_SW3_CLICK,
    };
    companion_controller_output_t output = {0};
    return ESP_OK == apply_model_input(&input, &output) &&
           output.roam_enabled;
}

static esp_err_t model_set_network(bool ready, uint32_t revision,
                                   uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT,
        .now_ms = timestamp_ms,
        .data.network = {
            .ready = ready,
            .revision = revision,
        },
    };
    return apply_model_input(&input, NULL);
}

static esp_err_t model_set_capability_snapshot(
    companion_capability_t capability, bool available, esp_err_t error,
    uint32_t revision, uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT,
        .now_ms = timestamp_ms,
        .data.capability = {
            .capability = capability,
            .available = available,
            .error = error,
            .revision = revision,
        },
    };
    return apply_model_input(&input, NULL);
}

static void model_cancel_session(uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_CANCEL_SESSION,
        .now_ms = timestamp_ms,
    };
    (void)apply_model_input(&input, NULL);
}

static esp_err_t model_enter_error(
    companion_controller_error_reason_t reason, bool restart_required,
    uint64_t timestamp_ms)
{
    const companion_controller_input_t input = {
        .type = COMPANION_CONTROLLER_INPUT_ENTER_ERROR,
        .now_ms = timestamp_ms,
        .data.error = {
            .reason = reason,
            .restart_required = restart_required,
        },
    };
    return apply_model_input(&input, NULL);
}

static uint32_t allocate_motion_request_id(void)
{
    s_next_motion_request_id++;
    if (0U == s_next_motion_request_id) {
        s_next_motion_request_id = 1U;
    }
    return s_next_motion_request_id;
}

static controller_agent_binding_t agent_binding_snapshot(void)
{
    controller_agent_binding_t binding = {0};
    portENTER_CRITICAL(&s_fact_lock);
    binding.generation = s_agent_generation;
    binding.wake_seq = s_agent_wake_seq;
    binding.session_epoch = s_agent_session_epoch;
    binding.request_id = s_agent_request_id;
    portEXIT_CRITICAL(&s_fact_lock);
    return binding;
}

static void set_agent_binding(uint32_t generation, uint32_t wake_seq,
                              uint32_t session_epoch, uint32_t request_id)
{
    portENTER_CRITICAL(&s_fact_lock);
    s_agent_generation = generation;
    s_agent_wake_seq = wake_seq;
    s_agent_session_epoch = session_epoch;
    s_agent_request_id = request_id;
    portEXIT_CRITICAL(&s_fact_lock);
}

static void clear_agent_binding(void)
{
    set_agent_binding(0U, 0U, 0U, 0U);
}

static void update_agent_session_epoch(uint32_t request_id,
                                       uint32_t session_epoch)
{
    portENTER_CRITICAL(&s_fact_lock);
    if (0U != request_id && request_id == s_agent_request_id) {
        s_agent_session_epoch = session_epoch;
    }
    portEXIT_CRITICAL(&s_fact_lock);
}

static companion_audio_token_t audio_token_from_binding(
    const controller_agent_binding_t *binding)
{
    if (NULL == binding) {
        return (companion_audio_token_t){0};
    }
    return (companion_audio_token_t){
        .generation = binding->generation,
        .wake_seq = binding->wake_seq,
        .session_epoch = binding->session_epoch,
        .request_id = binding->request_id,
    };
}

static bool audio_token_is_valid(const companion_audio_token_t *token)
{
    return NULL != token && 0U != token->generation &&
           0U != token->wake_seq && 0U != token->request_id;
}

static uint32_t update_capability_fact(companion_capability_t capability,
                                       bool available, esp_err_t error,
                                       bool *changed)
{
    uint32_t revision = 0U;
    if (NULL != changed) {
        *changed = false;
    }
    portENTER_CRITICAL(&s_fact_lock);
    const bool value_changed =
        0U == s_capability_latest_revisions[capability] ||
        available != s_capability_latest_available[capability] ||
        error != s_capability_latest_error[capability];
    if (value_changed) {
        s_capability_latest_available[capability] = available;
        s_capability_latest_error[capability] = error;
        s_capability_latest_revisions[capability]++;
        if (0U == s_capability_latest_revisions[capability]) {
            s_capability_latest_revisions[capability] = 1U;
        }
        if (NULL != changed) {
            *changed = true;
        }
    }
    revision = s_capability_latest_revisions[capability];
    portEXIT_CRITICAL(&s_fact_lock);
    return revision;
}

static esp_err_t stop_audio_output_for_binding(
    const controller_agent_binding_t *binding)
{
    const companion_audio_token_t token = audio_token_from_binding(binding);
    if (audio_token_is_valid(&token)) {
        const esp_err_t result = companion_audio_play_stop_ex(&token);
        if (ESP_OK == result || ESP_ERR_INVALID_STATE == result) {
            return result;
        }
        ESP_LOGW(TAG, "token audio stop failed request=%lu error=%s",
                 (unsigned long)token.request_id, esp_err_to_name(result));
        s_stats.module_errors++;
        (void)companion_controller_set_capability(
            COMPANION_CAPABILITY_AUDIO, false, result);
        return result;
    }
    const esp_err_t result = companion_audio_play_stop();
    if (ESP_OK != result && ESP_ERR_INVALID_STATE != result) {
        s_stats.module_errors++;
        (void)companion_controller_set_capability(
            COMPANION_CAPABILITY_AUDIO, false, result);
    }
    return result;
}

static esp_err_t stop_motion_safely(const char *reason)
{
    const esp_err_t result = companion_motion_stop(reason);
    if (ESP_OK != result) {
        s_stats.module_errors++;
        (void)update_capability_fact(COMPANION_CAPABILITY_MOTION, false,
                                     result, NULL);
        ESP_LOGE(TAG,
                 "motion safe stop failed error=%s; capability fact disabled",
                 esp_err_to_name(result));
    }
    return result;
}

static esp_err_t cancel_doa_safely(uint32_t request_id, const char *reason)
{
    if (0U == request_id) {
        return ESP_OK;
    }
    const esp_err_t result = companion_doa_cancel(request_id);
    if (ESP_OK == result || ESP_ERR_NOT_FOUND == result) {
        return result;
    }
    s_stats.module_errors++;
    (void)companion_controller_set_capability(COMPANION_CAPABILITY_DOA,
                                               false, result);
    ESP_LOGE(TAG, "DOA cancel failed reason=%s request=%lu error=%s",
             (NULL != reason) ? reason : "?", (unsigned long)request_id,
             esp_err_to_name(result));
    return result;
}

static void close_upload_gate(void)
{
    portENTER_CRITICAL(&s_fact_lock);
    s_upload_gate_open = false;
    s_upload_generation = 0U;
    s_upload_wake_seq = 0U;
    s_upload_request_id = 0U;
    portEXIT_CRITICAL(&s_fact_lock);
}

static void refresh_upload_gate_locked(void)
{
    bool runtime_ready = false;
    portENTER_CRITICAL(&s_fact_lock);
    runtime_ready = s_network_ready &&
        s_capability_latest_available[COMPANION_CAPABILITY_AUDIO] &&
        s_capability_latest_available[COMPANION_CAPABILITY_AGENT];
    portEXIT_CRITICAL(&s_fact_lock);
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(&s_model, &snapshot);
    const bool open = runtime_ready && snapshot.upload_gate_open &&
                      0U != snapshot.agent_request_id;
    portENTER_CRITICAL(&s_fact_lock);
    s_upload_generation = open ? snapshot.generation : 0U;
    s_upload_wake_seq = open ? snapshot.wake_seq : 0U;
    s_upload_request_id = open ? snapshot.agent_request_id : 0U;
    s_upload_gate_open = open;
    portEXIT_CRITICAL(&s_fact_lock);
}

static const char *state_name(companion_product_state_t state)
{
    switch (state) {
    case COMPANION_PRODUCT_BOOTING: return "BOOTING";
    case COMPANION_PRODUCT_WAIT_NETWORK: return "WAIT_NETWORK";
    case COMPANION_PRODUCT_IDLE: return "IDLE";
    case COMPANION_PRODUCT_LOCATING: return "LOCATING";
    case COMPANION_PRODUCT_TURNING: return "TURNING";
    case COMPANION_PRODUCT_CONNECTING: return "CONNECTING";
    case COMPANION_PRODUCT_LISTENING: return "LISTENING";
    case COMPANION_PRODUCT_PROCESSING: return "PROCESSING";
    case COMPANION_PRODUCT_SPEAKING: return "SPEAKING";
    case COMPANION_PRODUCT_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static bool network_loss_requires_session_stop(
    const companion_controller_snapshot_t *snapshot)
{
    if (NULL == snapshot) {
        return false;
    }
    return snapshot->wake_reserved ||
           COMPANION_PRODUCT_LOCATING == snapshot->product_state ||
           COMPANION_PRODUCT_TURNING == snapshot->product_state ||
           COMPANION_PRODUCT_CONNECTING == snapshot->product_state ||
           COMPANION_PRODUCT_LISTENING == snapshot->product_state ||
           COMPANION_PRODUCT_PROCESSING == snapshot->product_state ||
           COMPANION_PRODUCT_SPEAKING == snapshot->product_state;
}

static void update_queue_peak(void)
{
    const UBaseType_t waiting = uxQueueMessagesWaiting(s_queue);
    if ((uint32_t)waiting > s_stats.queue_peak) {
        s_stats.queue_peak = (uint32_t)waiting;
    }
}

static esp_err_t post_event(const controller_event_t *event, bool critical)
{
    if (NULL == event || NULL == s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    const BaseType_t result = xQueueSendToBack(s_queue, event, 0U);
    if (pdTRUE == result) {
        update_queue_peak();
        return ESP_OK;
    }
    s_stats.queue_drops++;
    if (critical) {
        close_upload_gate();
        request_emergency_exit();
        (void)stop_motion_safely("critical controller queue full");
        ESP_LOGE(TAG, "critical event queue full type=%d drops=%lu",
                 (int)event->type, (unsigned long)s_stats.queue_drops);
    }
    return ESP_ERR_NO_MEM;
}

static void sync_ui(void)
{
    bool ui_available = false;
    bool network_ready = false;
    bool doa_debug_valid = false;
    int16_t doa_remaining_deg = 0;
    portENTER_CRITICAL(&s_fact_lock);
    ui_available =
        s_capability_latest_available[COMPANION_CAPABILITY_UI];
    network_ready = s_network_ready;
    doa_debug_valid = s_doa_debug_valid;
    doa_remaining_deg = s_doa_remaining_deg;
    portEXIT_CRITICAL(&s_fact_lock);
    if (NULL == s_model_lock) {
        return;
    }
    companion_ui_state_t state = {0};
    if (pdTRUE != xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        return;
    }
    companion_controller_snapshot_t snapshot = {0};
    companion_controller_model_snapshot(&s_model, &snapshot);
    state.product_state = snapshot.product_state;
    state.look_direction =
        (COMPANION_PRODUCT_TURNING == snapshot.product_state) ?
        s_look_direction : COMPANION_TURN_NONE;
    state.roam_enabled = snapshot.roam_enabled;
    state.network_ready = network_ready;
    state.doa_debug_valid = doa_debug_valid;
    state.doa_remaining_deg = doa_remaining_deg;
    state.generation = snapshot.generation;
    state.wake_seq = snapshot.wake_seq;
    const uint64_t current_ms = now_ms();
    if (COMPANION_PRODUCT_IDLE != snapshot.product_state) {
        s_merit_sample_valid = false;
    }
    if (COMPANION_PRODUCT_IDLE != snapshot.product_state &&
        s_merit_bubble_active) {
        s_merit_bubble_active = false;
        s_merit_bubble_repeat_count = 0U;
        s_merit_bubble_epoch++;
        if (0U == s_merit_bubble_epoch) {
            s_merit_bubble_epoch = 1U;
        }
    } else if (s_merit_bubble_active && current_ms >=
               s_merit_bubble_start_ms + 800U) {
        s_merit_bubble_active = false;
        s_merit_bubble_repeat_count = 0U;
    }
    state.merit_bubble_active = s_merit_bubble_active;
    state.merit_bubble_epoch = s_merit_bubble_epoch;
    state.merit_bubble_start_ms = s_merit_bubble_start_ms;
    state.merit_bubble_repeat_count = s_merit_bubble_repeat_count;
    xSemaphoreGive(s_model_lock);
    const esp_err_t merit_gate_result = companion_motion_set_merit_tap_gate(
        COMPANION_PRODUCT_IDLE == snapshot.product_state,
        snapshot.generation, snapshot.wake_seq);
    if (ESP_OK != merit_gate_result) {
        ESP_LOGI(TAG,
                 "[DEBUG-MERIT-GATE] sync state=%s generation=%lu wake_seq=%lu result=%s",
                 state_name(snapshot.product_state),
                 (unsigned long)snapshot.generation,
                 (unsigned long)snapshot.wake_seq,
                 esp_err_to_name(merit_gate_result));
    }
    if (!ui_available) {
        return;
    }
    (void)companion_ui_set_state(&state);
}

static void log_state_transition(companion_product_state_t before,
                                 const char *event_name, esp_err_t result)
{
    companion_product_state_t after = COMPANION_PRODUCT_ERROR;
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    if (pdTRUE == xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        after = snapshot.product_state;
        generation = snapshot.generation;
        wake_seq = snapshot.wake_seq;
        xSemaphoreGive(s_model_lock);
    }
    ESP_LOGI(TAG,
             "event=%s state=%s->%s generation=%lu wake_seq=%lu result=%s upload_gate=%u",
             event_name, state_name(before), state_name(after),
             (unsigned long)generation, (unsigned long)wake_seq,
             esp_err_to_name(result), s_upload_gate_open ? 1U : 0U);
}

static void stop_runtime_effects(const char *reason)
{
    const controller_agent_binding_t binding = agent_binding_snapshot();
    close_upload_gate();
    if (0U != s_doa_request_id) {
        (void)cancel_doa_safely(s_doa_request_id, reason);
        s_doa_request_id = 0U;
    }
    if (0U != binding.request_id) {
        (void)companion_agent_adapter_cancel(binding.generation,
                                             binding.wake_seq,
                                             binding.request_id);
    }
    stop_audio_output_for_binding(&binding);
    clear_agent_binding();
    (void)stop_motion_safely(reason);
    s_roam_state = CONTROLLER_ROAM_BLOCKED;
    s_roam_motion_request_id = 0U;
    s_wake_motion_request_id = 0U;
    s_look_direction = COMPANION_TURN_NONE;
}

static void prepare_active_rewake(const char *reason)
{
    const controller_agent_binding_t binding = agent_binding_snapshot();
    close_upload_gate();
    if (0U != s_doa_request_id) {
        (void)cancel_doa_safely(s_doa_request_id, reason);
        s_doa_request_id = 0U;
    }
    stop_audio_output_for_binding(&binding);
    clear_agent_binding();
    const esp_err_t roam_stop = companion_motion_stop_role(
        COMPANION_MOTION_ROLE_ROAM, reason);
    if (ESP_OK != roam_stop && companion_motion_is_available()) {
        ESP_LOGW(TAG, "active re-wake ROAM stop result=%s",
                 esp_err_to_name(roam_stop));
    }
    s_roam_state = CONTROLLER_ROAM_BLOCKED;
    s_roam_motion_request_id = 0U;
    s_look_direction = COMPANION_TURN_NONE;
}

static void cancel_session(const char *reason)
{
    stop_runtime_effects(reason);
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        model_cancel_session(now_ms());
        xSemaphoreGive(s_model_lock);
    }
    s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
}

static bool audio_token_matches_binding(
    const companion_audio_token_t *token,
    const controller_agent_binding_t *binding)
{
    if (NULL == token || NULL == binding || !audio_token_is_valid(token)) {
        return false;
    }
    return token->generation == binding->generation &&
           token->wake_seq == binding->wake_seq &&
           token->request_id == binding->request_id &&
           (0U == token->session_epoch || 0U == binding->session_epoch ||
            token->session_epoch == binding->session_epoch);
}

static bool validate_runtime_invariants(void)
{
    companion_controller_snapshot_t model_snapshot = {0};
    if (pdTRUE != xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        return true;
    }
    companion_controller_model_snapshot(&s_model, &model_snapshot);
    xSemaphoreGive(s_model_lock);

    const companion_product_state_t state = model_snapshot.product_state;
    const uint32_t model_request_id = model_snapshot.agent_request_id;
    const bool wake_reserved = model_snapshot.wake_reserved;
    const bool roam_requested = model_snapshot.roam_enabled;

    bool upload_gate_open = false;
    uint32_t upload_generation = 0U;
    uint32_t upload_wake_seq = 0U;
    uint32_t upload_request_id = 0U;
    bool network_ready = false;
    bool applied_network_ready = false;
    uint32_t applied_network_revision = 0U;
    bool audio_available = false;
    bool agent_available = false;
    portENTER_CRITICAL(&s_fact_lock);
    upload_gate_open = s_upload_gate_open;
    upload_generation = s_upload_generation;
    upload_wake_seq = s_upload_wake_seq;
    upload_request_id = s_upload_request_id;
    network_ready = s_network_ready;
    applied_network_ready = s_network_snapshot.ready;
    applied_network_revision = s_network_snapshot.revision;
    audio_available =
        s_capability_latest_available[COMPANION_CAPABILITY_AUDIO];
    agent_available =
        s_capability_latest_available[COMPANION_CAPABILITY_AGENT];
    portEXIT_CRITICAL(&s_fact_lock);

    const controller_agent_binding_t binding = agent_binding_snapshot();
    companion_audio_stats_t audio = {0};
    companion_audio_get_stats(&audio);
    const bool audio_token_valid =
        audio_token_is_valid(&audio.output_token);
    const bool audio_state_valid =
        (COMPANION_AUDIO_OUTPUT_SILENT == audio.output_owner &&
         COMPANION_AUDIO_OUTPUT_IDLE == audio.output_phase &&
         !audio_token_valid) ||
        ((COMPANION_AUDIO_OUTPUT_PROMPT == audio.output_owner ||
          COMPANION_AUDIO_OUTPUT_TTS == audio.output_owner) &&
         (COMPANION_AUDIO_OUTPUT_ACTIVE == audio.output_phase ||
          COMPANION_AUDIO_OUTPUT_STOPPING == audio.output_phase) &&
         audio_token_valid);
    const bool audio_unavailable_stopping =
        !audio_available &&
        COMPANION_AUDIO_OUTPUT_STOPPING == audio.output_phase;

    const char *violation = NULL;
    if (upload_gate_open && COMPANION_PRODUCT_LISTENING != state) {
        violation = "upload outside LISTENING";
    } else if (upload_gate_open &&
               (!network_ready || !audio_available || !agent_available)) {
        violation = "upload while runtime gate unavailable";
    } else if (upload_gate_open &&
               (upload_generation != binding.generation ||
                upload_wake_seq != binding.wake_seq ||
                upload_request_id != binding.request_id)) {
        violation = "upload token mismatch";
    } else if (model_snapshot.network_revision != applied_network_revision ||
               model_snapshot.network_ready != applied_network_ready) {
        violation = "network snapshot mismatch";
    } else if (!audio_state_valid) {
        violation = "invalid audio owner/phase/token";
    } else if (!audio_unavailable_stopping &&
               COMPANION_AUDIO_OUTPUT_TTS == audio.output_owner &&
               COMPANION_PRODUCT_SPEAKING != state) {
        violation = "TTS outside SPEAKING";
    } else if (!audio_unavailable_stopping &&
               (COMPANION_AUDIO_OUTPUT_PROMPT == audio.output_owner ||
                COMPANION_AUDIO_OUTPUT_TTS == audio.output_owner) &&
               !audio_token_matches_binding(&audio.output_token, &binding)) {
        violation = "audio token mismatch";
    } else if (COMPANION_PRODUCT_LOCATING == state &&
               0U == s_doa_request_id) {
        violation = "LOCATING without DOA request";
    } else if (COMPANION_PRODUCT_LOCATING != state &&
               0U != s_doa_request_id) {
        violation = "DOA request outside LOCATING";
    } else if (COMPANION_PRODUCT_TURNING == state &&
               (0U == s_wake_motion_request_id ||
                COMPANION_TURN_NONE == s_look_direction)) {
        violation = "TURNING without WAKE_TURN";
    } else if (COMPANION_PRODUCT_TURNING != state &&
               0U != s_wake_motion_request_id) {
        violation = "WAKE_TURN outside TURNING";
    } else if (CONTROLLER_ROAM_RUNNING == s_roam_state &&
               (COMPANION_PRODUCT_IDLE != state || !roam_requested)) {
        violation = "Roam running while gate closed";
    } else if (!wake_reserved &&
               (COMPANION_PRODUCT_CONNECTING == state ||
                COMPANION_PRODUCT_LISTENING == state ||
                COMPANION_PRODUCT_PROCESSING == state ||
                COMPANION_PRODUCT_SPEAKING == state) &&
               (0U == model_request_id ||
                model_request_id != binding.request_id)) {
        violation = "Agent transaction binding mismatch";
    }
    if (NULL == violation || s_invariant_fault_latched) {
        return NULL == violation;
    }

    s_invariant_fault_latched = true;
    ESP_LOGE(TAG,
             "runtime invariant failed reason=%s state=%s model_request=%lu binding=%lu doa=%lu wake_motion=%lu roam=%d upload=%u audio=%d/%d",
             violation, state_name(state), (unsigned long)model_request_id,
             (unsigned long)binding.request_id,
             (unsigned long)s_doa_request_id,
             (unsigned long)s_wake_motion_request_id, (int)s_roam_state,
             upload_gate_open ? 1U : 0U, (int)audio.output_owner,
             (int)audio.output_phase);
    stop_runtime_effects("runtime invariant failure");
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        (void)model_enter_error(COMPANION_CONTROLLER_ERROR_INVARIANT, true,
                                now_ms());
        xSemaphoreGive(s_model_lock);
    }
    return false;
}

static void handle_emergency_exit(void)
{
    companion_product_state_t state = COMPANION_PRODUCT_ERROR;
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    bool wake_reserved = false;
    bool network_ready = false;
    bool audio_available = false;
    bool agent_available = false;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        state = snapshot.product_state;
        generation = snapshot.generation;
        wake_seq = snapshot.wake_seq;
        wake_reserved = snapshot.wake_reserved;
        xSemaphoreGive(s_model_lock);
    }
    portENTER_CRITICAL(&s_fact_lock);
    network_ready = s_network_ready;
    audio_available =
        s_capability_latest_available[COMPANION_CAPABILITY_AUDIO];
    agent_available =
        s_capability_latest_available[COMPANION_CAPABILITY_AGENT];
    portEXIT_CRITICAL(&s_fact_lock);
    ESP_LOGE(TAG,
             "controller emergency exit state=%s generation=%lu wake_seq=%lu reserved=%u",
             state_name(state), (unsigned long)generation,
             (unsigned long)wake_seq, wake_reserved ? 1U : 0U);

    if (COMPANION_PRODUCT_ERROR == state) {
        stop_runtime_effects("ERROR emergency cleanup");
        return;
    }

    if (COMPANION_PRODUCT_BOOTING == state) {
        stop_runtime_effects("BOOTING emergency");
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            (void)model_enter_error(COMPANION_CONTROLLER_ERROR_STARTUP,
                                    true, now_ms());
            xSemaphoreGive(s_model_lock);
        }
        return;
    }
    if (!audio_available || !agent_available) {
        stop_runtime_effects("core capability emergency");
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            const companion_controller_error_reason_t reason =
                !audio_available ? COMPANION_CONTROLLER_ERROR_AUDIO :
                                   COMPANION_CONTROLLER_ERROR_AGENT;
            (void)model_enter_error(reason, !audio_available, now_ms());
            xSemaphoreGive(s_model_lock);
        }
        return;
    }
    if (!network_ready &&
        (wake_reserved || COMPANION_PRODUCT_LOCATING == state ||
         COMPANION_PRODUCT_TURNING == state ||
         COMPANION_PRODUCT_CONNECTING == state ||
         COMPANION_PRODUCT_LISTENING == state ||
         COMPANION_PRODUCT_PROCESSING == state ||
         COMPANION_PRODUCT_SPEAKING == state)) {
        cancel_session("network unavailable during emergency");
        return;
    }

    if (COMPANION_PRODUCT_LOCATING == state) {
        if (0U != s_doa_request_id) {
            (void)cancel_doa_safely(s_doa_request_id,
                                    "LOCATING emergency");
            s_doa_request_id = 0U;
        }
        companion_controller_decision_t decision = {0};
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            result = model_on_doa(generation, wake_seq, false, 0.0f,
                                  &decision);
            xSemaphoreGive(s_model_lock);
        }
        if (ESP_OK == result) {
            apply_decision(&decision, generation, wake_seq);
        } else {
            cancel_session("LOCATING emergency fallback failed");
        }
        return;
    }
    if (COMPANION_PRODUCT_TURNING == state) {
        (void)stop_motion_safely("TURNING emergency");
        s_wake_motion_request_id = 0U;
        s_look_direction = COMPANION_TURN_NONE;
        companion_controller_decision_t decision = {0};
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            result = model_on_motion_done(generation, wake_seq, &decision);
            xSemaphoreGive(s_model_lock);
        }
        if (ESP_OK == result) {
            apply_decision(&decision, generation, wake_seq);
        } else {
            cancel_session("TURNING emergency fallback failed");
        }
        return;
    }
    if (wake_reserved || COMPANION_PRODUCT_CONNECTING == state ||
        COMPANION_PRODUCT_LISTENING == state ||
        COMPANION_PRODUCT_PROCESSING == state ||
        COMPANION_PRODUCT_SPEAKING == state) {
        cancel_session("controller critical event delivery failed");
        return;
    }
    stop_runtime_effects("controller emergency idle cleanup");
}

static void notify_agent(uint32_t generation, uint32_t wake_seq)
{
    uint32_t request_id = 0U;
    esp_err_t result = companion_agent_adapter_begin(
        generation, wake_seq, &request_id);
    ESP_LOGI(TAG,
             "agent notify generation=%lu wake_seq=%lu request=%lu result=%s",
             (unsigned long)generation, (unsigned long)wake_seq,
             (unsigned long)request_id,
             esp_err_to_name(result));
    if (ESP_OK != result) {
        s_stats.module_errors++;
        cancel_session("agent wake notify failed");
        return;
    }
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        result = model_mark_agent_accepted(generation, wake_seq, request_id,
                                           now_ms());
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != result) {
        (void)companion_agent_adapter_cancel(generation, wake_seq, request_id);
        s_stats.stale_events++;
        cancel_session("agent accept model mismatch");
        return;
    }
    set_agent_binding(generation, wake_seq, 0U, request_id);
    const controller_agent_binding_t binding = agent_binding_snapshot();
    const companion_audio_token_t token = audio_token_from_binding(&binding);
    const esp_err_t prompt_result = companion_audio_prompt_play_ex(
        CONTROLLER_PROMPT_URL, &token);
    if (ESP_OK != prompt_result) {
        s_stats.module_errors++;
        ESP_LOGW(TAG,
                 "prompt start failed generation=%lu wake_seq=%lu request=%lu error=%s",
                 (unsigned long)generation, (unsigned long)wake_seq,
                 (unsigned long)request_id, esp_err_to_name(prompt_result));
    }
}

static void apply_decision(const companion_controller_decision_t *decision,
                           uint32_t generation, uint32_t wake_seq)
{
    if (NULL == decision) {
        return;
    }
    if (COMPANION_CONTROLLER_DECISION_NOTIFY_AGENT == decision->type) {
        notify_agent(generation, wake_seq);
        return;
    }
    if (COMPANION_CONTROLLER_DECISION_START_TURN != decision->type) {
        return;
    }
    const companion_motion_command_t command = {
        .action = (COMPANION_TURN_LEFT == decision->turn.direction) ?
                  COMPANION_ACTION_TURN_LEFT : COMPANION_ACTION_TURN_RIGHT,
        .role = COMPANION_MOTION_ROLE_WAKE_TURN,
        .duration_ms = decision->turn.duration_ms,
        .target_deg = fabsf(decision->turn.relative_deg),
        .generation = generation,
        .wake_seq = wake_seq,
        .request_id = allocate_motion_request_id(),
    };
    bool motion_available = false;
    portENTER_CRITICAL(&s_fact_lock);
    motion_available =
        s_capability_latest_available[COMPANION_CAPABILITY_MOTION];
    portEXIT_CRITICAL(&s_fact_lock);
    esp_err_t result = motion_available ? companion_motion_submit(&command) :
                                          ESP_ERR_INVALID_STATE;
    if (ESP_OK == result) {
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            result = model_mark_motion_started(generation, wake_seq,
                                               now_ms());
            xSemaphoreGive(s_model_lock);
        }
        if (ESP_OK != result) {
            (void)stop_motion_safely("wake turn model mismatch");
            s_stats.stale_events++;
            cancel_session("wake turn model mismatch");
            return;
        }
        s_look_direction = decision->turn.direction;
        s_wake_motion_request_id = command.request_id;
        ESP_LOGI(TAG,
                 "wake turn generation=%lu wake_seq=%lu request=%lu direction=%d relative=%.1f duration=%lums",
                 (unsigned long)generation, (unsigned long)wake_seq,
                 (unsigned long)command.request_id,
                 (int)decision->turn.direction, decision->turn.relative_deg,
                 (unsigned long)decision->turn.duration_ms);
        return;
    }
    const bool runtime_available = companion_motion_is_available();
    ESP_LOGW(TAG,
             "wake turn submit skipped generation=%lu wake_seq=%lu request=%lu direction=%d target=%.1f result=%s capability=%u runtime=%u; continuing Agent",
             (unsigned long)generation, (unsigned long)wake_seq,
             (unsigned long)command.request_id,
             (int)decision->turn.direction, (double)command.target_deg,
             esp_err_to_name(result), motion_available ? 1U : 0U,
             runtime_available ? 1U : 0U);
    if (!runtime_available) {
        (void)companion_controller_set_capability(
            COMPANION_CAPABILITY_MOTION, false, result);
    }
    s_stats.module_errors++;
    (void)stop_motion_safely("wake turn unavailable");
    s_look_direction = COMPANION_TURN_NONE;
    notify_agent(generation, wake_seq);
}

static void handle_merit_tap(const controller_event_t *event)
{
    if (NULL == event || NULL == s_model_lock) {
        return;
    }
    companion_controller_snapshot_t snapshot = {0};
    if (pdTRUE != xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        s_stats.stale_events++;
        return;
    }
    companion_controller_model_snapshot(&s_model, &snapshot);
    xSemaphoreGive(s_model_lock);
    const uint32_t sample_seq = event->data.merit.result.sample_seq;
    if (COMPANION_PRODUCT_IDLE != snapshot.product_state) {
        s_merit_sample_valid = false;
    }
    const bool same_sample_epoch = s_merit_sample_valid &&
        s_merit_sample_generation == snapshot.generation &&
        s_merit_sample_wake_seq == snapshot.wake_seq;
    if (COMPANION_PRODUCT_IDLE != snapshot.product_state ||
        event->generation != snapshot.generation ||
        event->wake_seq != snapshot.wake_seq ||
        !event->data.merit.result.hit || 0U == sample_seq ||
        (same_sample_epoch &&
         !merit_sample_is_newer(sample_seq, s_merit_last_sample_seq))) {
        s_stats.stale_events++;
        ESP_LOGI(TAG,
                 "[DEBUG-MERIT-EVENT] rejected state=%s sample=%lu hit=%u event=%lu/%lu current=%lu/%lu same_epoch=%u last_sample=%lu",
                 state_name(snapshot.product_state),
                 (unsigned long)event->data.merit.result.sample_seq,
                 event->data.merit.result.hit ? 1U : 0U,
                 (unsigned long)event->generation,
                 (unsigned long)event->wake_seq,
                 (unsigned long)snapshot.generation,
                 (unsigned long)snapshot.wake_seq,
                 same_sample_epoch ? 1U : 0U,
                 (unsigned long)s_merit_last_sample_seq);
        return;
    }

    s_merit_sample_valid = true;
    s_merit_last_sample_seq = sample_seq;
    s_merit_sample_generation = snapshot.generation;
    s_merit_sample_wake_seq = snapshot.wake_seq;

    const uint64_t current_ms = now_ms();
    const bool bubble_was_active = s_merit_bubble_active && current_ms <
        s_merit_bubble_start_ms + 800U;
    if (!bubble_was_active) {
        s_merit_bubble_epoch++;
        if (0U == s_merit_bubble_epoch) {
            s_merit_bubble_epoch = 1U;
        }
        s_merit_bubble_active = true;
        s_merit_bubble_start_ms = current_ms;
        s_merit_bubble_repeat_count = 0U;
    } else {
        s_merit_bubble_repeat_count++;
    }

    s_merit_prompt_seq++;
    if (0U == s_merit_prompt_seq) {
        s_merit_prompt_seq = 1U;
    }
    const companion_audio_token_t token = {
        .generation = (0U != snapshot.generation) ? snapshot.generation : 1U,
        .wake_seq = (0U != snapshot.wake_seq) ? snapshot.wake_seq : 1U,
        .request_id = s_merit_prompt_seq,
    };
    const esp_err_t prompt_result = companion_audio_prompt_play_ex(
        CONTROLLER_MERIT_PROMPT_URL, &token);
    if (ESP_OK != prompt_result) {
        s_stats.module_errors++;
    }
    ESP_LOGI(TAG,
             "[DEBUG-MERIT-EVENT] accepted state=%s sample=%lu accel=%ld gyro=%ld bubble_epoch=%lu repeat=%lu detector_repeat=%lu prompt_result=%s",
             state_name(snapshot.product_state),
             (unsigned long)event->data.merit.result.sample_seq,
             (long)event->data.merit.result.accel_peak_raw,
             (long)event->data.merit.result.gyro_peak_raw,
             (unsigned long)s_merit_bubble_epoch,
             (unsigned long)s_merit_bubble_repeat_count,
             (unsigned long)event->data.merit.result.repeat_count,
             esp_err_to_name(prompt_result));
}

static void handle_audio_wake(const controller_event_t *event)
{
    companion_product_state_t before = COMPANION_PRODUCT_ERROR;
    bool current = false;
    bool requires_localization = false;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        before = snapshot.product_state;
        current = snapshot.generation == event->generation &&
                  snapshot.wake_seq == event->wake_seq &&
                  snapshot.wake_reserved;
        requires_localization = snapshot.wake_requires_localization;
        xSemaphoreGive(s_model_lock);
    }
    if (!current) {
        s_stats.stale_events++;
        return;
    }
    const bool idle_wake = COMPANION_PRODUCT_IDLE == before;
    if (idle_wake) {
        stop_runtime_effects("idle wake accepted");
    } else {
        prepare_active_rewake("active re-wake accepted");
    }

    esp_err_t result = event->data.wake.result;
    bool doa_available = false;
    portENTER_CRITICAL(&s_fact_lock);
    doa_available =
        s_capability_latest_available[COMPANION_CAPABILITY_DOA];
    portEXIT_CRITICAL(&s_fact_lock);
    if (requires_localization && ESP_OK == result && doa_available) {
        uint32_t request_id = 0U;
        result = companion_doa_request_ex(event->generation, event->wake_seq,
                                          &request_id);
        if (ESP_OK == result) {
            if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
                result = model_mark_locating_started(
                    event->generation, event->wake_seq, now_ms());
                xSemaphoreGive(s_model_lock);
            }
        }
        if (ESP_OK == result) {
            s_doa_request_id = request_id;
            ESP_LOGI(TAG,
                     "DOA accepted generation=%lu wake_seq=%lu request=%lu",
                     (unsigned long)event->generation,
                     (unsigned long)event->wake_seq,
                     (unsigned long)request_id);
            log_state_transition(before, "wake_request_doa", result);
            return;
        }
        if (0U != request_id) {
            (void)cancel_doa_safely(request_id, "wake DOA setup failed");
        }
    }
    companion_controller_decision_t decision = {0};
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        result = model_on_doa(event->generation, event->wake_seq, false,
                              0.0f, &decision);
        xSemaphoreGive(s_model_lock);
    }
    apply_decision(&decision, event->generation, event->wake_seq);
    log_state_transition(before,
                         idle_wake ? "wake_skip_doa" :
                                     "active_rewake_skip_doa",
                         result);
}

static void handle_doa(const controller_event_t *event)
{
    companion_controller_decision_t decision = {0};
    companion_product_state_t before = COMPANION_PRODUCT_ERROR;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (0U == s_doa_request_id ||
        event->data.doa.request_id != s_doa_request_id) {
        s_stats.stale_events++;
        return;
    }
    s_doa_request_id = 0U;
    const bool doa_valid = event->data.doa.valid &&
                           ESP_OK == event->data.doa.result &&
                           isfinite(event->data.doa.relative_deg);
    portENTER_CRITICAL(&s_fact_lock);
    s_doa_debug_valid = doa_valid;
    s_doa_remaining_deg = doa_valid ?
        (int16_t)lroundf(event->data.doa.relative_deg) : 0;
    portEXIT_CRITICAL(&s_fact_lock);
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        before = snapshot.product_state;
        result = model_on_doa(
            event->generation, event->wake_seq,
            event->data.doa.valid && ESP_OK == event->data.doa.result,
            event->data.doa.relative_deg, &decision);
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != result) {
        s_stats.stale_events++;
        return;
    }
    apply_decision(&decision, event->generation, event->wake_seq);
    log_state_transition(before, "doa_done", result);
}

static void handle_motion_progress(const controller_event_t *event)
{
    const companion_motion_command_t *command =
        &event->data.motion_progress.command;
    const companion_motion_progress_t *progress =
        &event->data.motion_progress.progress;
    if (COMPANION_MOTION_ROLE_WAKE_TURN != command->role ||
        0U == s_wake_motion_request_id ||
        command->request_id != s_wake_motion_request_id ||
        !isfinite(progress->remaining_deg)) {
        return;
    }
    int direction_sign = 0;
    if (COMPANION_ACTION_TURN_LEFT == command->action) {
        direction_sign = 1;
    } else if (COMPANION_ACTION_TURN_RIGHT == command->action) {
        direction_sign = -1;
    } else {
        return;
    }
    const int remaining_deg = (int)ceilf(
        fmaxf(0.0f, progress->remaining_deg));
    portENTER_CRITICAL(&s_fact_lock);
    s_doa_debug_valid = true;
    s_doa_remaining_deg = (int16_t)(direction_sign * remaining_deg);
    portEXIT_CRITICAL(&s_fact_lock);
}

static void handle_motion_done(const controller_event_t *event)
{
    if (COMPANION_MOTION_ROLE_ROAM == event->data.motion.command.role) {
        if (0U == s_roam_motion_request_id ||
            event->data.motion.command.request_id !=
                s_roam_motion_request_id) {
            s_stats.stale_events++;
            return;
        }
        s_roam_motion_request_id = 0U;
        s_roam_state = CONTROLLER_ROAM_COOLDOWN;
        s_next_roam_ms = now_ms() + s_roam_stop_ms;
        if (COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE ==
            event->data.motion.result.classification ||
            !event->data.motion.result.available) {
            (void)companion_controller_set_capability(
                COMPANION_CAPABILITY_MOTION, false,
                event->data.motion.result.error);
            s_stats.module_errors++;
        }
        return;
    }
    if (0U == s_wake_motion_request_id ||
        event->data.motion.command.request_id != s_wake_motion_request_id) {
        s_stats.stale_events++;
        return;
    }
    s_wake_motion_request_id = 0U;
    s_look_direction = COMPANION_TURN_NONE;
    if (ESP_OK == event->data.motion.result.error) {
        portENTER_CRITICAL(&s_fact_lock);
        s_doa_debug_valid = true;
        s_doa_remaining_deg = 0;
        portEXIT_CRITICAL(&s_fact_lock);
    }
    const esp_err_t stop_result =
        stop_motion_safely("WAKE_TURN completion");
    companion_controller_decision_t decision = {0};
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        result = model_on_motion_done(event->generation, event->wake_seq,
                                      &decision);
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != result) {
        s_stats.stale_events++;
        return;
    }
    if (COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE ==
        event->data.motion.result.classification ||
        !event->data.motion.result.available || ESP_OK != stop_result) {
        (void)companion_controller_set_capability(
            COMPANION_CAPABILITY_MOTION, false,
            (ESP_OK != stop_result) ? stop_result :
                                      event->data.motion.result.error);
        s_stats.module_errors++;
    } else if (ESP_OK != event->data.motion.result.error) {
        s_stats.module_errors++;
        ESP_LOGW(TAG,
                 "wake turn recoverable generation=%lu wake_seq=%lu request=%lu class=%d error=%s availability=%u; continuing Agent",
                 (unsigned long)event->generation,
                 (unsigned long)event->wake_seq,
                 (unsigned long)event->data.motion.command.request_id,
                 (int)event->data.motion.result.classification,
                 esp_err_to_name(event->data.motion.result.error),
                 event->data.motion.result.available ? 1U : 0U);
    }
    ESP_LOGI(TAG,
             "wake turn completion generation=%lu wake_seq=%lu request=%lu class=%d error=%s stop=%s availability=%u next=Agent",
             (unsigned long)event->generation,
             (unsigned long)event->wake_seq,
             (unsigned long)event->data.motion.command.request_id,
             (int)event->data.motion.result.classification,
             esp_err_to_name(event->data.motion.result.error),
             esp_err_to_name(event->data.motion.result.stop_error),
             event->data.motion.result.available ? 1U : 0U);
    apply_decision(&decision, event->generation, event->wake_seq);
}

static companion_agent_semantic_t model_agent_semantic(
    companion_agent_event_type_t type)
{
    switch (type) {
    case COMPANION_AGENT_EVENT_CONNECTING:
        return COMPANION_AGENT_SEMANTIC_CONNECTING;
    case COMPANION_AGENT_EVENT_LISTENING_READY:
        return COMPANION_AGENT_SEMANTIC_LISTENING_READY;
    case COMPANION_AGENT_EVENT_PROCESSING:
        return COMPANION_AGENT_SEMANTIC_PROCESSING;
    case COMPANION_AGENT_EVENT_SPEAKING:
        return COMPANION_AGENT_SEMANTIC_SPEAKING;
    case COMPANION_AGENT_EVENT_CLOSED:
        return COMPANION_AGENT_SEMANTIC_CLOSED;
    case COMPANION_AGENT_EVENT_FAILED:
    default:
        return COMPANION_AGENT_SEMANTIC_FAILED;
    }
}

static void handle_agent_semantic(const controller_event_t *event)
{
    const companion_agent_event_t *agent = &event->data.agent_semantic;
    companion_product_state_t before = COMPANION_PRODUCT_ERROR;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    bool notify_pending_vad = false;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        before = snapshot.product_state;
        result = model_on_agent_semantic(
            agent->generation, agent->wake_seq, agent->session_epoch,
            agent->request_id, model_agent_semantic(agent->type),
            agent->result, now_ms());
        if (ESP_OK == result) {
            refresh_upload_gate_locked();
            companion_controller_model_snapshot(&s_model, &snapshot);
            if (COMPANION_AGENT_EVENT_LISTENING_READY == agent->type &&
                snapshot.pending_vad_end) {
                (void)model_on_vad_end(agent->generation, agent->wake_seq,
                                       &notify_pending_vad);
            }
        }
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != result) {
        s_stats.stale_events++;
        return;
    }
    update_agent_session_epoch(agent->request_id, agent->session_epoch);
    if (notify_pending_vad) {
        const esp_err_t vad_result =
            companion_agent_adapter_notify_vad_end(
                agent->generation, agent->wake_seq, agent->request_id);
        ESP_LOGI(TAG,
                 "replay pending VAD generation=%lu wake_seq=%lu request=%lu result=%s",
                 (unsigned long)agent->generation,
                 (unsigned long)agent->wake_seq,
                 (unsigned long)agent->request_id,
                 esp_err_to_name(vad_result));
        if (ESP_OK != vad_result) {
            cancel_session("pending VAD notify failed");
        }
    }
    if (COMPANION_AGENT_EVENT_CLOSED == agent->type ||
        COMPANION_AGENT_EVENT_FAILED == agent->type) {
        const controller_agent_binding_t binding = agent_binding_snapshot();
        close_upload_gate();
        stop_audio_output_for_binding(&binding);
        clear_agent_binding();
        (void)stop_motion_safely("agent transaction closed");
        s_roam_state = CONTROLLER_ROAM_DELAYED;
        s_look_direction = COMPANION_TURN_NONE;
        s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
    }
    log_state_transition(before, "agent_semantic", result);
}

static void handle_vad_end(const controller_event_t *event)
{
    bool notify_agent = false;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        result = model_on_vad_end(event->generation, event->wake_seq,
                                  &notify_agent);
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != result) {
        s_stats.stale_events++;
        return;
    }
    if (!notify_agent) {
        ESP_LOGI(TAG,
                 "VAD end pending generation=%lu wake_seq=%lu awaiting_ready=1",
                 (unsigned long)event->generation,
                 (unsigned long)event->wake_seq);
        return;
    }
    const controller_agent_binding_t binding = agent_binding_snapshot();
    if (binding.generation != event->generation ||
        binding.wake_seq != event->wake_seq || 0U == binding.request_id) {
        s_stats.stale_events++;
        return;
    }
    result = companion_agent_adapter_notify_vad_end(
        event->generation, event->wake_seq, binding.request_id);
    ESP_LOGI(TAG,
             "VAD end generation=%lu wake_seq=%lu request=%lu result=%s awaiting_agent_state=1",
             (unsigned long)event->generation,
             (unsigned long)event->wake_seq,
             (unsigned long)binding.request_id, esp_err_to_name(result));
    if (ESP_OK != result) {
        cancel_session("VAD end notify failed");
    }
}

static void handle_network(const controller_event_t *event)
{
    companion_product_state_t controller_state = COMPANION_PRODUCT_ERROR;
    companion_controller_snapshot_t model_before = {0};
    const companion_network_snapshot_t *snapshot =
        &event->data.network.snapshot;
    uint32_t current_revision = 0U;
    uint32_t latest_revision = 0U;
    portENTER_CRITICAL(&s_fact_lock);
    current_revision = s_network_snapshot.revision;
    latest_revision = s_network_latest_snapshot.revision;
    portEXIT_CRITICAL(&s_fact_lock);
    if (0U == snapshot->revision || snapshot->revision <= current_revision) {
        return;
    }
    if (snapshot->revision != latest_revision) {
        ESP_LOGD(TAG,
                 "drop superseded network revision=%lu latest=%lu",
                 (unsigned long)snapshot->revision,
                 (unsigned long)latest_revision);
        return;
    }
    const bool network_ready = snapshot->ready &&
        COMPANION_NETWORK_RUNNING == snapshot->lifecycle &&
        COMPANION_NETWORK_INTERFACE_4G == snapshot->interface;
    portENTER_CRITICAL(&s_fact_lock);
    s_network_snapshot = *snapshot;
    s_network_snapshot.ready = network_ready;
    s_network_ready = network_ready;
    portEXIT_CRITICAL(&s_fact_lock);
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_model_snapshot(&s_model, &model_before);
        xSemaphoreGive(s_model_lock);
    }
    if (!network_ready && network_loss_requires_session_stop(&model_before)) {
        close_upload_gate();
        stop_runtime_effects("network unavailable");
    }
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        (void)model_set_network(network_ready, snapshot->revision, now_ms());
        companion_controller_snapshot_t model_snapshot = {0};
        companion_controller_model_snapshot(&s_model, &model_snapshot);
        controller_state = model_snapshot.product_state;
        xSemaphoreGive(s_model_lock);
    }
    ESP_LOGI(TAG,
             "network event lifecycle=%d phase=%d link=%u ipv4=%u internet=%u ready=%u interface=%d revision=%lu attempt=%lu error=%s controller=%s",
             (int)snapshot->lifecycle, (int)snapshot->phase,
             snapshot->link_up ? 1U : 0U,
             snapshot->ipv4_ready ? 1U : 0U,
             snapshot->internet_reachable ? 1U : 0U,
             network_ready ? 1U : 0U,
             (int)snapshot->interface, (unsigned long)snapshot->revision,
             (unsigned long)snapshot->recovery_attempt,
             esp_err_to_name(snapshot->error), state_name(controller_state));
}

static void reconcile_network(void)
{
    companion_network_snapshot_t snapshot = {0};
    portENTER_CRITICAL(&s_fact_lock);
    snapshot = s_network_latest_snapshot;
    portEXIT_CRITICAL(&s_fact_lock);
    if (0U == snapshot.revision ||
        snapshot.revision <= s_network_snapshot.revision) {
        return;
    }
    const controller_event_t event = {
        .type = CONTROLLER_EVENT_NETWORK,
        .data.network.snapshot = snapshot,
    };
    handle_network(&event);
}

static void handle_sw3(void)
{
    bool enabled = false;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        enabled = model_toggle_roam();
        xSemaphoreGive(s_model_lock);
    }
    if (!enabled) {
        s_roam_motion_request_id = 0U;
        const esp_err_t stop_result = companion_motion_stop_role(
            COMPANION_MOTION_ROLE_ROAM, "SW3 roam off");
        ESP_LOGI(TAG,
                 "SW3 ROAM execution stop requested result=%s wake_turn_preserved=1",
                 esp_err_to_name(stop_result));
    }
    if (enabled) {
        s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
        s_roam_state = CONTROLLER_ROAM_DELAYED;
    } else {
        s_roam_state = CONTROLLER_ROAM_DISABLED;
    }
    ESP_LOGI(TAG, "SW3 roam_enabled=%u", enabled ? 1U : 0U);
}

static bool reconcile_sw3(void)
{
    uint32_t latest_revision = 0U;
    portENTER_CRITICAL(&s_fact_lock);
    latest_revision = s_sw3_latest_revision;
    portEXIT_CRITICAL(&s_fact_lock);
    const uint32_t pending = latest_revision - s_sw3_applied_revision;
    if (0U == pending) {
        return false;
    }
    s_sw3_applied_revision = latest_revision;
    if (0U != (pending & 1U)) {
        handle_sw3();
    }
    ESP_LOGI(TAG, "SW3 reconciled clicks=%lu revision=%lu",
             (unsigned long)pending, (unsigned long)latest_revision);
    return true;
}

static void handle_capability(const controller_event_t *event)
{
    const companion_capability_t capability =
        event->data.capability.capability;
    if (COMPANION_CAPABILITY_COUNT <= capability) {
        return;
    }
    if (0U == event->data.capability.revision) {
        return;
    }
    uint32_t latest_revision = 0U;
    portENTER_CRITICAL(&s_fact_lock);
    latest_revision = s_capability_latest_revisions[capability];
    portEXIT_CRITICAL(&s_fact_lock);
    if (event->data.capability.revision != latest_revision) {
        ESP_LOGD(TAG,
                 "drop superseded capability=%d revision=%lu latest=%lu",
                 (int)capability,
                 (unsigned long)event->data.capability.revision,
                 (unsigned long)latest_revision);
        return;
    }

    companion_controller_snapshot_t model_before = {0};
    esp_err_t model_result = ESP_ERR_INVALID_STATE;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_model_snapshot(&s_model, &model_before);
        model_result = model_set_capability_snapshot(
            capability, event->data.capability.available,
            event->data.capability.error,
            event->data.capability.revision, now_ms());
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != model_result) {
        s_stats.stale_events++;
        return;
    }
    bool restart_agent = false;
    if (!event->data.capability.available) {
        s_stats.module_errors++;
        if (COMPANION_CAPABILITY_INPUT == capability) {
            if (CONTROLLER_ROAM_RUNNING == s_roam_state) {
                s_roam_motion_request_id = 0U;
                (void)stop_motion_safely("input unavailable");
            }
            s_roam_state = CONTROLLER_ROAM_BLOCKED;
        }
        const companion_product_state_t product_state =
            model_before.product_state;
        const uint32_t generation = model_before.generation;
        const uint32_t wake_seq = model_before.wake_seq;
        if (COMPANION_CAPABILITY_DOA == capability &&
            COMPANION_PRODUCT_LOCATING == product_state) {
            if (0U != s_doa_request_id) {
                (void)cancel_doa_safely(s_doa_request_id,
                                        "DOA capability unavailable");
                s_doa_request_id = 0U;
            }
            companion_controller_decision_t decision = {0};
            esp_err_t result = ESP_ERR_INVALID_STATE;
            if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
                result = model_on_doa(generation, wake_seq, false, 0.0f,
                                      &decision);
                xSemaphoreGive(s_model_lock);
            }
            if (ESP_OK == result) {
                apply_decision(&decision, generation, wake_seq);
            } else {
                cancel_session("DOA capability loss fallback failed");
            }
        }
        if (COMPANION_CAPABILITY_MOTION == capability &&
            COMPANION_PRODUCT_TURNING == product_state) {
            const esp_err_t stop_result = companion_motion_stop(
                "Motion capability unavailable");
            if (ESP_OK != stop_result) {
                s_stats.module_errors++;
                ESP_LOGE(TAG, "motion stop after capability loss failed: %s",
                         esp_err_to_name(stop_result));
            }
            s_wake_motion_request_id = 0U;
            s_look_direction = COMPANION_TURN_NONE;
            companion_controller_decision_t decision = {0};
            esp_err_t result = ESP_ERR_INVALID_STATE;
            if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
                result = model_on_motion_done(generation, wake_seq,
                                              &decision);
                xSemaphoreGive(s_model_lock);
            }
            if (ESP_OK == result) {
                apply_decision(&decision, generation, wake_seq);
            } else {
                cancel_session("Motion capability loss fallback failed");
            }
        }
        if (COMPANION_CAPABILITY_AUDIO == capability ||
            COMPANION_CAPABILITY_AGENT == capability) {
            if (model_before.startup_complete) {
                stop_runtime_effects("core capability unavailable");
                if (COMPANION_CAPABILITY_AUDIO == capability) {
                    const esp_err_t stop_result = companion_audio_stop_ex(
                        CONTROLLER_CORE_STOP_TIMEOUT_MS);
                    if (ESP_OK != stop_result) {
                        ESP_LOGE(TAG,
                                 "audio worker group exit failed error=%s restart_required=1",
                                 esp_err_to_name(stop_result));
                    }
                }
                if (COMPANION_CAPABILITY_AGENT == capability &&
                    !s_agent_recovery_attempted &&
                    (COMPANION_PRODUCT_ERROR != model_before.product_state ||
                     (COMPANION_CONTROLLER_ERROR_AGENT ==
                          model_before.error_reason &&
                      !model_before.restart_required))) {
                    s_agent_recovery_attempted = true;
                    restart_agent = true;
                }
            }
        }
    }
    if (event->data.capability.available &&
        (COMPANION_CAPABILITY_AUDIO == capability ||
         COMPANION_CAPABILITY_AGENT == capability)) {
        if (COMPANION_CAPABILITY_AGENT == capability) {
            s_agent_recovery_attempted = false;
        }
        bool core_ready = false;
        portENTER_CRITICAL(&s_fact_lock);
        core_ready =
            s_capability_latest_available[COMPANION_CAPABILITY_AUDIO] &&
            s_capability_latest_available[COMPANION_CAPABILITY_AGENT];
        portEXIT_CRITICAL(&s_fact_lock);
        if (core_ready) {
            s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
        }
    }
    if (restart_agent) {
        const esp_err_t restart_result = companion_agent_adapter_restart();
        ESP_LOGI(TAG, "agent bounded recovery result=%s",
                 esp_err_to_name(restart_result));
        if (ESP_OK != restart_result &&
            pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            (void)model_enter_error(COMPANION_CONTROLLER_ERROR_AGENT, true,
                                    now_ms());
            xSemaphoreGive(s_model_lock);
        }
        (void)companion_controller_set_capability(
            COMPANION_CAPABILITY_AGENT, ESP_OK == restart_result,
            restart_result);
    }
    ESP_LOGI(TAG, "capability=%d available=%u error=%s",
             (int)capability, event->data.capability.available ? 1U : 0U,
             esp_err_to_name(event->data.capability.error));
}

static void reconcile_capabilities(void)
{
    companion_controller_snapshot_t model_snapshot = {0};
    if (pdTRUE != xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        return;
    }
    companion_controller_model_snapshot(&s_model, &model_snapshot);
    xSemaphoreGive(s_model_lock);
    for (int index = 0; index < COMPANION_CAPABILITY_COUNT; ++index) {
        bool available = false;
        esp_err_t error = ESP_OK;
        uint32_t revision = 0U;
        portENTER_CRITICAL(&s_fact_lock);
        available = s_capability_latest_available[index];
        error = s_capability_latest_error[index];
        revision = s_capability_latest_revisions[index];
        portEXIT_CRITICAL(&s_fact_lock);
        if (0U == revision ||
            revision <= model_snapshot.capability_revisions[index]) {
            continue;
        }
        const controller_event_t event = {
            .type = CONTROLLER_EVENT_CAPABILITY,
            .data.capability = {
                .capability = (companion_capability_t)index,
                .available = available,
                .error = error,
                .revision = revision,
            },
        };
        handle_capability(&event);
    }
}

static void handle_startup_complete(const controller_event_t *event)
{
    companion_product_state_t before = COMPANION_PRODUCT_ERROR;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        before = snapshot.product_state;
        result = model_finish_startup(event->data.startup.core_ready,
                                      now_ms());
        xSemaphoreGive(s_model_lock);
    }
    if (ESP_OK != result) {
        close_upload_gate();
        (void)companion_audio_play_stop();
        (void)stop_motion_safely("startup core unavailable");
        s_stats.module_errors++;
    }
    s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
    log_state_transition(before, "startup_complete", result);
}

static void handle_deadline(void)
{
    companion_controller_deadline_t expired = {0};
    bool has_expired = false;
    if (pdTRUE == xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        has_expired = companion_controller_model_tick(
            &s_model, now_ms(), &expired);
        xSemaphoreGive(s_model_lock);
    }
    if (!has_expired) {
        return;
    }
    ESP_LOGE(TAG,
             "state deadline expired state=%s generation=%lu wake_seq=%lu request=%lu",
             state_name(expired.state), (unsigned long)expired.generation,
             (unsigned long)expired.wake_seq,
             (unsigned long)expired.agent_request_id);
    close_upload_gate();
    if (COMPANION_PRODUCT_BOOTING == expired.state) {
        stop_runtime_effects("BOOTING deadline");
        sync_ui();
        return;
    }
    if (COMPANION_PRODUCT_LOCATING == expired.state) {
        if (0U != s_doa_request_id) {
            (void)cancel_doa_safely(s_doa_request_id,
                                    "LOCATING deadline");
            s_doa_request_id = 0U;
        }
        companion_controller_decision_t decision = {0};
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            result = model_on_doa(expired.generation, expired.wake_seq,
                                  false, 0.0f, &decision);
            xSemaphoreGive(s_model_lock);
        }
        if (ESP_OK == result) {
            apply_decision(&decision, expired.generation, expired.wake_seq);
        } else {
            cancel_session("LOCATING deadline fallback failed");
        }
        sync_ui();
        return;
    }
    if (COMPANION_PRODUCT_TURNING == expired.state) {
        (void)stop_motion_safely("TURNING deadline");
        s_wake_motion_request_id = 0U;
        s_look_direction = COMPANION_TURN_NONE;
        companion_controller_decision_t decision = {0};
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (pdTRUE == xSemaphoreTake(s_model_lock, portMAX_DELAY)) {
            result = model_on_motion_done(expired.generation,
                                          expired.wake_seq, &decision);
            xSemaphoreGive(s_model_lock);
        }
        if (ESP_OK == result) {
            apply_decision(&decision, expired.generation, expired.wake_seq);
        } else {
            cancel_session("TURNING deadline fallback failed");
        }
        sync_ui();
        return;
    }
    if (COMPANION_PRODUCT_CONNECTING == expired.state ||
        COMPANION_PRODUCT_LISTENING == expired.state ||
        COMPANION_PRODUCT_PROCESSING == expired.state ||
        COMPANION_PRODUCT_SPEAKING == expired.state) {
        const controller_agent_binding_t binding = agent_binding_snapshot();
        (void)companion_agent_adapter_cancel(
            expired.generation, expired.wake_seq,
            expired.agent_request_id);
        stop_audio_output_for_binding(&binding);
        clear_agent_binding();
        (void)stop_motion_safely("Agent state deadline");
        s_roam_state = CONTROLLER_ROAM_DELAYED;
        s_look_direction = COMPANION_TURN_NONE;
        s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
    }
    sync_ui();
}

static void schedule_roam(void)
{
    bool requested = false;
    bool idle = false;
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    if (pdTRUE == xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(10))) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        idle = COMPANION_PRODUCT_IDLE == snapshot.product_state;
        requested = snapshot.roam_enabled;
        generation = snapshot.generation;
        wake_seq = snapshot.wake_seq;
        xSemaphoreGive(s_model_lock);
    }
    if (!requested) {
        if (CONTROLLER_ROAM_RUNNING == s_roam_state) {
            s_roam_motion_request_id = 0U;
            (void)companion_motion_stop_role(COMPANION_MOTION_ROLE_ROAM,
                                             "roam disabled");
        }
        s_roam_state = CONTROLLER_ROAM_DISABLED;
        return;
    }
    bool motion_available = false;
    bool input_available = false;
    portENTER_CRITICAL(&s_fact_lock);
    motion_available =
        s_capability_latest_available[COMPANION_CAPABILITY_MOTION];
    input_available =
        s_capability_latest_available[COMPANION_CAPABILITY_INPUT];
    portEXIT_CRITICAL(&s_fact_lock);
    const bool gate_open = idle && motion_available && input_available;
    if (!gate_open) {
        if (CONTROLLER_ROAM_RUNNING == s_roam_state) {
            s_roam_motion_request_id = 0U;
            (void)companion_motion_stop_role(COMPANION_MOTION_ROLE_ROAM,
                                             "roam gate closed");
        }
        s_roam_state = CONTROLLER_ROAM_BLOCKED;
        return;
    }
    if (CONTROLLER_ROAM_DISABLED == s_roam_state ||
        CONTROLLER_ROAM_BLOCKED == s_roam_state) {
        s_roam_state = CONTROLLER_ROAM_DELAYED;
        s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
        return;
    }
    if (CONTROLLER_ROAM_RUNNING == s_roam_state ||
        now_ms() < s_next_roam_ms) {
        return;
    }
    companion_action_plan_t plan = {0};
    esp_err_t result = companion_behavior_plan(
        &s_config.roam_config, esp_random(), esp_random(), esp_random(), &plan);
    if (ESP_OK != result) {
        s_stats.module_errors++;
        s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
        return;
    }
    const companion_motion_command_t command = {
        .action = plan.action,
        .role = COMPANION_MOTION_ROLE_ROAM,
        .duration_ms = plan.duration_ms,
        .generation = generation,
        .wake_seq = wake_seq,
        .request_id = allocate_motion_request_id(),
    };
    result = companion_motion_submit(&command);
    if (ESP_OK == result) {
        s_roam_state = CONTROLLER_ROAM_RUNNING;
        s_roam_motion_request_id = command.request_id;
        s_roam_stop_ms = plan.stop_ms;
        ESP_LOGI(TAG, "roam action=%d duration_ms=%lu next_stop_ms=%lu",
                 (int)plan.action, (unsigned long)plan.duration_ms,
                 (unsigned long)plan.stop_ms);
    } else {
        s_stats.module_errors++;
        if (!companion_motion_is_available()) {
            (void)companion_controller_set_capability(
                COMPANION_CAPABILITY_MOTION, false, result);
            s_roam_state = CONTROLLER_ROAM_BLOCKED;
        } else {
            s_roam_state = CONTROLLER_ROAM_DELAYED;
            s_next_roam_ms = now_ms() + CONTROLLER_ROAM_RETRY_MS;
        }
    }
}

static void controller_task(void *arg)
{
    (void)arg;
    while (true) {
        controller_event_t event = {0};
        if (pdTRUE == xQueueReceive(s_queue, &event,
                                    pdMS_TO_TICKS(CONTROLLER_LOOP_MS))) {
            s_stats.events_processed++;
            switch (event.type) {
            case CONTROLLER_EVENT_STARTUP_COMPLETE:
                handle_startup_complete(&event);
                break;
            case CONTROLLER_EVENT_AUDIO_WAKE: handle_audio_wake(&event); break;
            case CONTROLLER_EVENT_VAD_END: handle_vad_end(&event); break;
            case CONTROLLER_EVENT_DOA: handle_doa(&event); break;
            case CONTROLLER_EVENT_MOTION_PROGRESS:
                handle_motion_progress(&event);
                break;
            case CONTROLLER_EVENT_MOTION_DONE: handle_motion_done(&event); break;
            case CONTROLLER_EVENT_AGENT_SEMANTIC:
                handle_agent_semantic(&event);
                break;
            case CONTROLLER_EVENT_NETWORK: handle_network(&event); break;
            case CONTROLLER_EVENT_CAPABILITY: handle_capability(&event); break;
            case CONTROLLER_EVENT_MERIT_TAP: handle_merit_tap(&event); break;
            default: break;
            }
            (void)validate_runtime_invariants();
            sync_ui();
        }
        if (take_emergency_exit()) {
            handle_emergency_exit();
            (void)validate_runtime_invariants();
            sync_ui();
        }
        handle_deadline();
        reconcile_network();
        reconcile_capabilities();
        if (reconcile_sw3()) {
            sync_ui();
        }
        schedule_roam();
        if (!validate_runtime_invariants()) {
            sync_ui();
        }
    }
}

void companion_controller_config_default(companion_controller_config_t *config)
{
    if (NULL != config) {
        companion_roam_config_default(&config->roam_config);
    }
}

esp_err_t companion_controller_start(const companion_controller_config_t *config)
{
    if (NULL == config ||
        ESP_OK != companion_roam_config_validate(&config->roam_config)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_fact_lock);
    if (s_started) {
        portEXIT_CRITICAL(&s_fact_lock);
        return ESP_OK;
    }
    if (s_starting) {
        portEXIT_CRITICAL(&s_fact_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_starting = true;
    portEXIT_CRITICAL(&s_fact_lock);
    s_config = *config;
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_capability_latest_available, 0,
           sizeof(s_capability_latest_available));
    memset(s_capability_latest_error, 0,
           sizeof(s_capability_latest_error));
    memset(s_capability_latest_revisions, 0,
           sizeof(s_capability_latest_revisions));
    s_network_snapshot = (companion_network_snapshot_t){0};
    s_network_latest_snapshot = (companion_network_snapshot_t){0};
    s_agent_recovery_attempted = false;
    s_invariant_fault_latched = false;
    s_doa_request_id = 0U;
    s_wake_motion_request_id = 0U;
    s_roam_motion_request_id = 0U;
    s_merit_bubble_active = false;
    s_merit_bubble_epoch = 0U;
    s_merit_bubble_start_ms = 0ULL;
    s_merit_bubble_repeat_count = 0U;
    s_merit_prompt_seq = 0U;
    s_merit_sample_valid = false;
    s_merit_last_sample_seq = 0U;
    s_merit_sample_generation = 0U;
    s_merit_sample_wake_seq = 0U;
    s_look_direction = COMPANION_TURN_NONE;
    portENTER_CRITICAL(&s_fact_lock);
    s_network_ready = false;
    s_upload_gate_open = false;
    s_upload_generation = 0U;
    s_upload_wake_seq = 0U;
    s_upload_request_id = 0U;
    s_emergency_cancel_requested = false;
    s_sw3_latest_revision = 0U;
    s_sw3_applied_revision = 0U;
    s_doa_debug_valid = false;
    s_doa_remaining_deg = 0;
    portEXIT_CRITICAL(&s_fact_lock);
    clear_agent_binding();
    companion_controller_model_init(&s_model, now_ms());
    s_model_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(CONTROLLER_QUEUE_DEPTH, sizeof(controller_event_t));
    if (NULL == s_model_lock || NULL == s_queue) {
        if (NULL != s_queue) {
            vQueueDelete(s_queue);
            s_queue = NULL;
        }
        if (NULL != s_model_lock) {
            vSemaphoreDelete(s_model_lock);
            s_model_lock = NULL;
        }
        portENTER_CRITICAL(&s_fact_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_fact_lock);
        return ESP_ERR_NO_MEM;
    }
    s_next_roam_ms = now_ms() + s_config.roam_config.initial_delay_ms;
    s_roam_state = CONTROLLER_ROAM_BLOCKED;
    const BaseType_t result = xTaskCreatePinnedToCore(
        controller_task, "companion_ctrl", CONTROLLER_TASK_STACK, NULL,
        CONTROLLER_TASK_PRIORITY, NULL, 1);
    if (pdPASS != result) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        vSemaphoreDelete(s_model_lock);
        s_model_lock = NULL;
        portENTER_CRITICAL(&s_fact_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_fact_lock);
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_fact_lock);
    s_started = true;
    s_starting = false;
    portEXIT_CRITICAL(&s_fact_lock);
    ESP_LOGI(TAG,
             "controller ready queue=%u roam=enabled stop_ms=[%lu,%lu] initial_delay_ms=%lu",
             CONTROLLER_QUEUE_DEPTH,
             (unsigned long)s_config.roam_config.min_stop_ms,
             (unsigned long)s_config.roam_config.max_stop_ms,
             (unsigned long)s_config.roam_config.initial_delay_ms);
    return ESP_OK;
}

esp_err_t companion_controller_finish_startup(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    bool core_ready = false;
    portENTER_CRITICAL(&s_fact_lock);
    core_ready =
        s_capability_latest_available[COMPANION_CAPABILITY_AUDIO] &&
        s_capability_latest_available[COMPANION_CAPABILITY_AGENT];
    portEXIT_CRITICAL(&s_fact_lock);
    const controller_event_t event = {
        .type = CONTROLLER_EVENT_STARTUP_COMPLETE,
        .data.startup.core_ready = core_ready,
    };
    return post_event(&event, true);
}

esp_err_t companion_controller_set_capability(companion_capability_t capability,
                                               bool available,
                                               esp_err_t error)
{
    if (COMPANION_CAPABILITY_COUNT <= capability) {
        return ESP_ERR_INVALID_ARG;
    }
    bool changed = false;
    const uint32_t revision =
        update_capability_fact(capability, available, error, &changed);
    if (!changed) {
        return ESP_OK;
    }
    if (!available &&
        (COMPANION_CAPABILITY_AUDIO == capability ||
         COMPANION_CAPABILITY_AGENT == capability)) {
        close_upload_gate();
    }
    const controller_event_t event = {
        .type = CONTROLLER_EVENT_CAPABILITY,
        .data.capability = {
            .capability = capability,
            .available = available,
            .error = error,
            .revision = revision,
        },
    };
    const bool critical = !available &&
        (COMPANION_CAPABILITY_AUDIO == capability ||
         COMPANION_CAPABILITY_AGENT == capability);
    return post_event(&event, critical);
}

void companion_controller_on_network(bool ready, const char *interface_name,
                                     void *user_ctx)
{
    (void)user_ctx;
    companion_network_snapshot_t snapshot = {
        .lifecycle = COMPANION_NETWORK_RUNNING,
        .interface = (ready && NULL != interface_name &&
                      0 == strcmp(interface_name, "4G")) ?
                     COMPANION_NETWORK_INTERFACE_4G :
                     COMPANION_NETWORK_INTERFACE_NONE,
        .phase = (ready && NULL != interface_name &&
                  0 == strcmp(interface_name, "4G")) ?
                 COMPANION_NETWORK_PHASE_READY_4G :
                 COMPANION_NETWORK_PHASE_WAIT_4G,
        .ready = ready && NULL != interface_name &&
                 0 == strcmp(interface_name, "4G"),
        .error = ESP_OK,
    };
    snapshot.link_up = snapshot.ready;
    snapshot.ipv4_ready = snapshot.ready;
    snapshot.internet_reachable = snapshot.ready;
    portENTER_CRITICAL(&s_fact_lock);
    snapshot.revision = s_network_latest_snapshot.revision + 1U;
    if (0U == snapshot.revision) {
        snapshot.revision = 1U;
    }
    portEXIT_CRITICAL(&s_fact_lock);
    companion_controller_on_network_snapshot(&snapshot, user_ctx);
}

void companion_controller_on_network_snapshot(
    const companion_network_snapshot_t *snapshot, void *user_ctx)
{
    (void)user_ctx;
    if (NULL == snapshot || 0U == snapshot->revision) {
        return;
    }
    bool accepted = false;
    bool latest_ready = false;
    portENTER_CRITICAL(&s_fact_lock);
    if (snapshot->revision > s_network_latest_snapshot.revision) {
        s_network_latest_snapshot = *snapshot;
        s_network_latest_snapshot.ready =
            snapshot->ready &&
            COMPANION_NETWORK_RUNNING == snapshot->lifecycle &&
            COMPANION_NETWORK_INTERFACE_4G == snapshot->interface;
        accepted = true;
    }
    latest_ready = s_network_latest_snapshot.ready;
    s_network_ready = latest_ready;
    portEXIT_CRITICAL(&s_fact_lock);
    if (!accepted) {
        return;
    }
    if (!latest_ready) {
        close_upload_gate();
    }
    bool active_transaction = false;
    if (!latest_ready && NULL != s_model_lock &&
        pdTRUE == xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        companion_controller_snapshot_t model_snapshot = {0};
        companion_controller_model_snapshot(&s_model, &model_snapshot);
        active_transaction =
            network_loss_requires_session_stop(&model_snapshot);
        xSemaphoreGive(s_model_lock);
    }
    const controller_event_t event = {
        .type = CONTROLLER_EVENT_NETWORK,
        .data.network.snapshot = *snapshot,
    };
    (void)post_event(&event, active_transaction);
}

esp_err_t companion_controller_reserve_wake(uint32_t *generation,
                                            uint32_t *wake_seq,
                                            void *user_ctx)
{
    (void)user_ctx;
    bool network_ready = false;
    bool audio_available = false;
    bool agent_available = false;
    portENTER_CRITICAL(&s_fact_lock);
    network_ready = s_network_ready;
    audio_available =
        s_capability_latest_available[COMPANION_CAPABILITY_AUDIO];
    agent_available =
        s_capability_latest_available[COMPANION_CAPABILITY_AGENT];
    portEXIT_CRITICAL(&s_fact_lock);
    if (!s_started || !network_ready || !audio_available ||
        !agent_available) {
        s_stats.wakes_rejected++;
        return ESP_ERR_INVALID_STATE;
    }
    if (pdTRUE != xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        s_stats.wakes_rejected++;
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = model_reserve_wake(now_ms(), generation,
                                                wake_seq);
    if (ESP_OK == result) {
        close_upload_gate();
        s_stats.wakes_accepted++;
    } else {
        s_stats.wakes_rejected++;
    }
    xSemaphoreGive(s_model_lock);
    return result;
}

void companion_controller_on_audio_event(const companion_audio_event_t *event,
                                         void *user_ctx)
{
    (void)user_ctx;
    if (NULL == event) {
        return;
    }
    controller_event_t message = {0};
    if (COMPANION_AUDIO_EVENT_WAKE == event->type) {
        message.type = CONTROLLER_EVENT_AUDIO_WAKE;
        message.generation = event->generation;
        message.wake_seq = event->wake_seq;
        message.data.wake.result = event->result;
        (void)post_event(&message, true);
    } else if (COMPANION_AUDIO_EVENT_VAD_END == event->type) {
        message.type = CONTROLLER_EVENT_VAD_END;
        message.generation = event->generation;
        message.wake_seq = event->wake_seq;
        (void)post_event(&message, true);
    } else if (COMPANION_AUDIO_EVENT_ERROR == event->type) {
        s_stats.module_errors++;
        ESP_LOGW(TAG, "audio runtime error=%s", esp_err_to_name(event->result));
    } else if (COMPANION_AUDIO_EVENT_FATAL == event->type) {
        ESP_LOGE(TAG, "audio task stopped error=%s; capability disabled",
                 esp_err_to_name(event->result));
        (void)companion_controller_set_capability(COMPANION_CAPABILITY_AUDIO,
                                                   false, event->result);
    }
}

void companion_controller_on_opus(const uint8_t *data, int length,
                                  void *user_ctx)
{
    (void)user_ctx;
    bool gate_open = false;
    uint32_t generation = 0U;
    uint32_t wake_seq = 0U;
    uint32_t request_id = 0U;
    portENTER_CRITICAL(&s_fact_lock);
    gate_open = s_upload_gate_open;
    generation = s_upload_generation;
    wake_seq = s_upload_wake_seq;
    request_id = s_upload_request_id;
    portEXIT_CRITICAL(&s_fact_lock);
    if (NULL == data || 0 >= length || !gate_open ||
        !companion_agent_adapter_is_listening(
            generation, wake_seq, request_id)) {
        s_stats.upload_drops++;
        return;
    }
    const esp_err_t result = companion_agent_adapter_send_audio(
        generation, wake_seq, request_id, data, length);
    if (ESP_OK == result) {
        s_stats.upload_frames++;
    } else {
        s_stats.upload_drops++;
        if (1U == s_stats.upload_drops ||
            0U == (s_stats.upload_drops % CONTROLLER_UPLOAD_LOG_INTERVAL)) {
            ESP_LOGW(TAG, "audio upload drop generation=%lu error=%s count=%lu",
                     (unsigned long)s_upload_generation,
                     esp_err_to_name(result),
                     (unsigned long)s_stats.upload_drops);
        }
    }
}

void companion_controller_on_doa(const companion_doa_result_t *result,
                                 void *user_ctx)
{
    (void)user_ctx;
    if (NULL == result) {
        return;
    }
    controller_event_t event = {
        .type = CONTROLLER_EVENT_DOA,
        .generation = result->generation,
        .wake_seq = result->wake_seq,
        .data.doa = *result,
    };
    (void)post_event(&event, true);
}

void companion_controller_on_motion_done(
    const companion_motion_command_t *command,
    const companion_motion_result_t *result,
    void *user_ctx)
{
    (void)user_ctx;
    if (NULL == command || NULL == result) {
        return;
    }
    controller_event_t event = {
        .type = CONTROLLER_EVENT_MOTION_DONE,
        .generation = command->generation,
        .wake_seq = command->wake_seq,
        .data.motion = {.command = *command, .result = *result},
    };
    (void)post_event(&event, true);
}

void companion_controller_on_motion_progress(
    const companion_motion_command_t *command,
    const companion_motion_progress_t *progress,
    void *user_ctx)
{
    (void)user_ctx;
    if (NULL == command || NULL == progress) {
        return;
    }
    const controller_event_t event = {
        .type = CONTROLLER_EVENT_MOTION_PROGRESS,
        .generation = command->generation,
        .wake_seq = command->wake_seq,
        .data.motion_progress = {
            .command = *command,
            .progress = *progress,
        },
    };
    (void)post_event(&event, false);
}

void companion_controller_on_merit_tap(
    const companion_merit_result_t *result, uint32_t generation,
    uint32_t wake_seq, uint64_t timestamp_us, void *user_ctx)
{
    (void)user_ctx;
    if (NULL == result || !result->hit) {
        return;
    }
    const controller_event_t event = {
        .type = CONTROLLER_EVENT_MERIT_TAP,
        .generation = generation,
        .wake_seq = wake_seq,
        .data.merit = {
            .result = *result,
            .timestamp_us = timestamp_us,
        },
    };
    (void)post_event(&event, false);
}

void companion_controller_on_agent_event(const companion_agent_event_t *event,
                                         void *user_ctx)
{
    (void)user_ctx;
    if (NULL == event) {
        return;
    }
    const controller_event_t message = {
        .type = CONTROLLER_EVENT_AGENT_SEMANTIC,
        .generation = event->generation,
        .wake_seq = event->wake_seq,
        .data.agent_semantic = *event,
    };
    (void)post_event(&message, true);
}

void companion_controller_on_agent_audio_event(
    const companion_agent_audio_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    const controller_agent_binding_t binding = agent_binding_snapshot();
    if (NULL == event || 0U == event->request_id ||
        event->generation != binding.generation ||
        event->wake_seq != binding.wake_seq ||
        event->request_id != binding.request_id ||
        (0U != binding.session_epoch &&
         event->session_epoch != binding.session_epoch)) {
        s_stats.stale_events++;
        return;
    }
    const companion_audio_token_t token = {
        .generation = event->generation,
        .wake_seq = event->wake_seq,
        .session_epoch = event->session_epoch,
        .request_id = event->request_id,
    };
    if (event->stop) {
        const esp_err_t stop_result = companion_audio_play_stop_ex(&token);
        if (ESP_OK != stop_result && ESP_ERR_INVALID_STATE != stop_result) {
            s_stats.module_errors++;
        }
        return;
    }
    companion_product_state_t state = COMPANION_PRODUCT_ERROR;
    if (pdTRUE == xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(10))) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        state = snapshot.product_state;
        xSemaphoreGive(s_model_lock);
    }
    if (COMPANION_PRODUCT_SPEAKING != state || NULL == event->data ||
        0 >= event->length) {
        s_stats.stale_events++;
        return;
    }
    const esp_err_t result = companion_audio_play_opus_ex(
        event->data, event->length, &token);
    if (ESP_OK != result) {
        s_stats.module_errors++;
    }
}

void companion_controller_on_sw3_click(void *user_ctx)
{
    (void)user_ctx;
    portENTER_CRITICAL(&s_fact_lock);
    s_sw3_latest_revision++;
    portEXIT_CRITICAL(&s_fact_lock);
}

void companion_controller_on_input_error(esp_err_t error, void *user_ctx)
{
    (void)user_ctx;
    (void)companion_controller_set_capability(COMPANION_CAPABILITY_INPUT,
                                               false, error);
}

void companion_controller_on_touch(bool pressed, void *user_ctx)
{
    (void)user_ctx;
    ESP_LOGD(TAG, "touch active=%u product state unchanged",
             pressed ? 1U : 0U);
}

void companion_controller_get_stats(companion_controller_stats_t *stats)
{
    if (NULL == stats) {
        return;
    }
    *stats = s_stats;
    if (NULL != s_model_lock &&
        pdTRUE == xSemaphoreTake(s_model_lock, pdMS_TO_TICKS(20))) {
        companion_controller_snapshot_t snapshot = {0};
        companion_controller_model_snapshot(&s_model, &snapshot);
        stats->product_state = snapshot.product_state;
        stats->generation = snapshot.generation;
        stats->wake_seq = snapshot.wake_seq;
        stats->roam_enabled = snapshot.roam_enabled;
        xSemaphoreGive(s_model_lock);
    }
    portENTER_CRITICAL(&s_fact_lock);
    stats->upload_gate_open = s_upload_gate_open;
    stats->network_ready = s_network_ready;
    stats->network_link_up = s_network_snapshot.link_up;
    stats->network_ipv4_ready = s_network_snapshot.ipv4_ready;
    stats->network_internet_reachable =
        s_network_snapshot.internet_reachable;
    stats->network_lifecycle = s_network_snapshot.lifecycle;
    stats->network_interface = s_network_snapshot.interface;
    stats->network_phase = s_network_snapshot.phase;
    stats->network_revision = s_network_snapshot.revision;
    stats->network_recovery_attempt =
        s_network_snapshot.recovery_attempt;
    stats->network_error = s_network_snapshot.error;
    portEXIT_CRITICAL(&s_fact_lock);
}
