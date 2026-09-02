#include "companion_controller_model.h"

#include <stddef.h>
#include <string.h>

#define COMPANION_BOOT_DEADLINE_MS 30000ULL
#define COMPANION_LOCATING_DEADLINE_MS 2000ULL
#define COMPANION_TURNING_DEADLINE_MS 14000ULL
#define COMPANION_CONNECTING_DEADLINE_MS 30000ULL
#define COMPANION_LISTENING_DEADLINE_MS 20000ULL
#define COMPANION_LISTENING_UTTERANCE_ABSOLUTE_DEADLINE_MS 300000ULL
#define COMPANION_PROCESSING_DEADLINE_MS 17000ULL
#define COMPANION_SPEAKING_INACTIVITY_DEADLINE_MS 15000ULL
#define COMPANION_SPEAKING_ABSOLUTE_DEADLINE_MS 300000ULL
#define COMPANION_TTS_ACTIVITY_REPORT_INTERVAL_MS 1000ULL

static void model_cancel_session(companion_controller_model_t *model,
                                 uint64_t now_ms);
static void model_finish_agent_session(companion_controller_model_t *model,
                                       uint64_t now_ms);

static void clear_speaking_timing(companion_controller_model_t *model)
{
    if (NULL == model) {
        return;
    }
    model->speaking_absolute_deadline_ms = 0ULL;
    model->tts_activity_seen = false;
    model->tts_last_activity_ms = 0ULL;
}

static uint64_t deadline_duration(companion_product_state_t state)
{
    switch (state) {
    case COMPANION_PRODUCT_BOOTING:
        return COMPANION_BOOT_DEADLINE_MS;
    case COMPANION_PRODUCT_LOCATING:
    case COMPANION_PRODUCT_TURNING:
        return 0ULL;
    case COMPANION_PRODUCT_CONNECTING:
        return COMPANION_CONNECTING_DEADLINE_MS;
    case COMPANION_PRODUCT_LISTENING:
        return COMPANION_LISTENING_DEADLINE_MS;
    case COMPANION_PRODUCT_PROCESSING:
        return COMPANION_PROCESSING_DEADLINE_MS;
    case COMPANION_PRODUCT_SPEAKING:
        return COMPANION_SPEAKING_INACTIVITY_DEADLINE_MS;
    default:
        return 0ULL;
    }
}

static void set_product_state(companion_controller_model_t *model,
                              companion_product_state_t state,
                              uint64_t now_ms)
{
    if (NULL == model || state == model->product_state) {
        return;
    }
    model->product_state = state;
    model->upload_gate_open = COMPANION_PRODUCT_LISTENING == state;
    model->state_entered_ms = now_ms;
    const uint64_t duration = deadline_duration(state);
    model->state_deadline_ms = (0ULL == duration) ? 0ULL : now_ms + duration;
    clear_speaking_timing(model);
    model->speaking_absolute_deadline_ms =
        (COMPANION_PRODUCT_SPEAKING == state) ?
        now_ms + COMPANION_SPEAKING_ABSOLUTE_DEADLINE_MS : 0ULL;
}

static void reset_agent_transaction(companion_controller_model_t *model)
{
    model->agent_notified = false;
    model->agent_effect_attempted = false;
    model->agent_session_started = false;
    model->agent_session_active = false;
    model->wake_reserved = false;
    model->wake_requires_localization = false;
    model->agent_request_id = 0U;
    model->agent_session_epoch = 0U;
    model->pending_vad_end = false;
    model->listen_voice_confirmed = false;
    model->listen_voice_started_ms = 0ULL;
}

static void reset_motion_transaction(companion_controller_model_t *model)
{
    model->motion_state = COMPANION_CONTROLLER_MOTION_IDLE;
    model->motion_state_entered_ms = 0ULL;
    model->motion_deadline_ms = 0ULL;
}

void companion_controller_model_init(companion_controller_model_t *model,
                                     uint64_t now_ms)
{
    if (NULL == model) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->product_state = COMPANION_PRODUCT_BOOTING;
    model->roam_enabled = true;
    model->startup_complete = false;
    model->restart_required = false;
    model->error_reason = COMPANION_CONTROLLER_ERROR_NONE;
    reset_agent_transaction(model);
    reset_motion_transaction(model);
    model->idle_entry_ms = 0U;
    model->state_entered_ms = now_ms;
    model->state_deadline_ms = now_ms + COMPANION_BOOT_DEADLINE_MS;
}

static bool model_event_is_current(
    const companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq)
{
    return NULL != model && model->generation == generation &&
           model->wake_seq == wake_seq;
}

static esp_err_t model_finish_startup(
    companion_controller_model_t *model, bool core_ready, uint64_t now_ms)
{
    if (NULL == model) {
        return ESP_ERR_INVALID_ARG;
    }
    if (model->startup_complete ||
        COMPANION_PRODUCT_BOOTING != model->product_state) {
        return ESP_ERR_INVALID_STATE;
    }
    model->startup_complete = true;
    if (!core_ready) {
        model->restart_required = true;
        model->error_reason = COMPANION_CONTROLLER_ERROR_STARTUP;
        set_product_state(model, COMPANION_PRODUCT_ERROR, now_ms);
        return ESP_FAIL;
    }
    model->error_reason = COMPANION_CONTROLLER_ERROR_NONE;
    set_product_state(model, COMPANION_PRODUCT_IDLE, now_ms);
    model->idle_entry_ms = now_ms;
    return ESP_OK;
}

static esp_err_t model_reserve_wake(
    companion_controller_model_t *model, uint64_t timestamp_ms,
    uint32_t *generation, uint32_t *wake_seq)
{
    if (NULL == model || NULL == generation || NULL == wake_seq) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!model->network_ready ||
        !model->capabilities[COMPANION_CAPABILITY_AUDIO] ||
        !model->capabilities[COMPANION_CAPABILITY_AGENT] ||
        /* A reservation is already the accepted wake for this controller
         * turn.  Reject duplicate detector edges until its event is
         * consumed, so an unhandled second edge cannot overwrite the token
         * of the first transaction. */
        model->wake_reserved ||
        (COMPANION_PRODUCT_IDLE != model->product_state &&
         COMPANION_PRODUCT_SPEAKING != model->product_state) ||
        timestamp_ms < model->idle_entry_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    reset_agent_transaction(model);
    reset_motion_transaction(model);
    model->generation++;
    model->wake_seq++;
    if (0U == model->generation) {
        model->generation = 1U;
    }
    if (0U == model->wake_seq) {
        model->wake_seq = 1U;
    }
    model->upload_gate_open = false;
    model->wake_reserved = true;
    model->wake_requires_localization = true;
    model->state_entered_ms = timestamp_ms;
    model->state_deadline_ms = 0ULL;
    clear_speaking_timing(model);
    *generation = model->generation;
    *wake_seq = model->wake_seq;
    return ESP_OK;
}

static esp_err_t model_mark_locating_started(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    if (NULL == model ||
        !model->wake_requires_localization ||
        COMPANION_CONTROLLER_MOTION_IDLE != model->motion_state ||
        !model_event_is_current(model, generation, wake_seq)) {
        return ESP_ERR_INVALID_STATE;
    }
    model->wake_reserved = false;
    model->wake_requires_localization = false;
    model->motion_state = COMPANION_CONTROLLER_MOTION_LOCATING;
    model->motion_state_entered_ms = now_ms;
    model->motion_deadline_ms = now_ms + COMPANION_LOCATING_DEADLINE_MS;
    return ESP_OK;
}

static esp_err_t model_on_doa(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool valid, float relative_deg,
    companion_controller_decision_t *decision)
{
    if (NULL == model || NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    *decision = (companion_controller_decision_t){0};
    const bool reserved_skip =
        COMPANION_CONTROLLER_MOTION_IDLE == model->motion_state &&
        model->wake_requires_localization && !valid;
    if (!model_event_is_current(model, generation, wake_seq) ||
        (!reserved_skip &&
         COMPANION_CONTROLLER_MOTION_LOCATING != model->motion_state)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (reserved_skip) {
        model->wake_reserved = false;
        model->wake_requires_localization = false;
    }
    if (valid) {
        esp_err_t result = companion_turn_plan_from_relative(relative_deg,
                                                              &decision->turn);
        if (ESP_OK != result) {
            return result;
        }
        if (COMPANION_TURN_NONE != decision->turn.direction) {
            decision->type = COMPANION_CONTROLLER_DECISION_START_TURN;
            return ESP_OK;
        }
    }
    model->motion_state = COMPANION_CONTROLLER_MOTION_COMPLETE;
    model->motion_state_entered_ms = 0ULL;
    model->motion_deadline_ms = 0ULL;
    decision->type = COMPANION_CONTROLLER_DECISION_NONE;
    return ESP_OK;
}

static esp_err_t model_on_motion_done(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, companion_controller_decision_t *decision)
{
    if (NULL == model || NULL == decision) {
        return ESP_ERR_INVALID_ARG;
    }
    *decision = (companion_controller_decision_t){0};
    if (!model_event_is_current(model, generation, wake_seq) ||
        COMPANION_CONTROLLER_MOTION_TURNING != model->motion_state) {
        return ESP_ERR_INVALID_STATE;
    }
    model->motion_state = COMPANION_CONTROLLER_MOTION_COMPLETE;
    model->motion_state_entered_ms = 0ULL;
    model->motion_deadline_ms = 0ULL;
    decision->type = COMPANION_CONTROLLER_DECISION_NONE;
    return ESP_OK;
}

static esp_err_t model_mark_motion_started(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    if (NULL == model ||
        !model_event_is_current(model, generation, wake_seq) ||
        COMPANION_CONTROLLER_MOTION_LOCATING != model->motion_state) {
        return ESP_ERR_INVALID_STATE;
    }
    model->motion_state = COMPANION_CONTROLLER_MOTION_TURNING;
    model->motion_state_entered_ms = now_ms;
    model->motion_deadline_ms = now_ms + COMPANION_TURNING_DEADLINE_MS;
    return ESP_OK;
}

static esp_err_t model_mark_agent_accepted(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t request_id, uint64_t now_ms)
{
    if (NULL == model || 0U == request_id) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool motion_prepared =
        model->wake_reserved ||
        COMPANION_CONTROLLER_MOTION_LOCATING == model->motion_state ||
        COMPANION_CONTROLLER_MOTION_TURNING == model->motion_state ||
        COMPANION_CONTROLLER_MOTION_COMPLETE == model->motion_state;
    if (!model_event_is_current(model, generation, wake_seq) ||
        model->agent_notified || !motion_prepared) {
        return ESP_ERR_INVALID_STATE;
    }
    model->wake_reserved = false;
    model->agent_notified = true;
    model->agent_effect_attempted = true;
    model->agent_session_started = true;
    model->agent_request_id = request_id;
    set_product_state(model, COMPANION_PRODUCT_CONNECTING, now_ms);
    return ESP_OK;
}

static esp_err_t model_on_agent_semantic(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t session_epoch, uint32_t request_id,
    companion_agent_semantic_t semantic, esp_err_t result,
    uint64_t now_ms)
{
    if (NULL == model || 0U == generation || 0U == wake_seq ||
        0U == session_epoch || 0U == request_id ||
        COMPANION_AGENT_SEMANTIC_CONNECTING > semantic ||
        COMPANION_AGENT_SEMANTIC_FAILED < semantic) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!model_event_is_current(model, generation, wake_seq) ||
        !model->agent_notified || request_id != model->agent_request_id) {
        return ESP_ERR_INVALID_STATE;
    }
    if (0U != model->agent_session_epoch &&
        session_epoch != model->agent_session_epoch) {
        return ESP_ERR_INVALID_STATE;
    }
    if (0U == model->agent_session_epoch) {
        model->agent_session_epoch = session_epoch;
    }

    const companion_product_state_t current = model->product_state;
    switch (semantic) {
    case COMPANION_AGENT_SEMANTIC_CONNECTING:
        return (COMPANION_PRODUCT_CONNECTING == current) ? ESP_OK :
                                                          ESP_ERR_INVALID_STATE;
    case COMPANION_AGENT_SEMANTIC_LISTENING_READY:
        if (COMPANION_PRODUCT_LISTENING == current) {
            model->agent_session_active = true;
            return ESP_OK;
        }
        if (COMPANION_PRODUCT_CONNECTING != current) {
            return ESP_ERR_INVALID_STATE;
        }
        model->agent_session_active = true;
        set_product_state(model, COMPANION_PRODUCT_LISTENING, now_ms);
        if (model->listen_voice_confirmed) {
            model->state_deadline_ms =
                model->listen_voice_started_ms +
                COMPANION_LISTENING_UTTERANCE_ABSOLUTE_DEADLINE_MS;
        }
        return ESP_OK;
    case COMPANION_AGENT_SEMANTIC_PROCESSING:
        if (COMPANION_PRODUCT_PROCESSING == current) {
            model->agent_session_active = true;
            return ESP_OK;
        }
        if (COMPANION_PRODUCT_CONNECTING != current &&
            COMPANION_PRODUCT_LISTENING != current) {
            return ESP_ERR_INVALID_STATE;
        }
        model->agent_session_active = true;
        set_product_state(model, COMPANION_PRODUCT_PROCESSING, now_ms);
        return ESP_OK;
    case COMPANION_AGENT_SEMANTIC_SPEAKING:
        if (COMPANION_PRODUCT_SPEAKING == current) {
            model->agent_session_active = true;
            return ESP_OK;
        }
        if (COMPANION_PRODUCT_CONNECTING != current &&
            COMPANION_PRODUCT_LISTENING != current &&
            COMPANION_PRODUCT_PROCESSING != current) {
            return ESP_ERR_INVALID_STATE;
        }
        model->agent_session_active = true;
        set_product_state(model, COMPANION_PRODUCT_SPEAKING, now_ms);
        return ESP_OK;
    case COMPANION_AGENT_SEMANTIC_CLOSED:
        model_finish_agent_session(model, now_ms);
        return ESP_OK;
    case COMPANION_AGENT_SEMANTIC_FAILED:
        if (ESP_OK == result) {
            return ESP_ERR_INVALID_ARG;
        }
        model_finish_agent_session(model, now_ms);
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t model_on_vad_end(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool agent_listening, bool *notify_agent)
{
    if (NULL == model || NULL == notify_agent) {
        return ESP_ERR_INVALID_ARG;
    }
    *notify_agent = false;
    if (!model_event_is_current(model, generation, wake_seq)) {
        return ESP_ERR_INVALID_STATE;
    }
    const companion_product_state_t state = model->product_state;
    const bool reserved_prelisten =
        COMPANION_PRODUCT_IDLE == state && model->wake_reserved;
    if (!reserved_prelisten &&
        COMPANION_PRODUCT_LOCATING != state &&
        COMPANION_PRODUCT_TURNING != state &&
        COMPANION_PRODUCT_CONNECTING != state &&
        COMPANION_PRODUCT_LISTENING != state) {
        return ESP_ERR_INVALID_STATE;
    }
    if (COMPANION_PRODUCT_LISTENING == state &&
        0U != model->agent_request_id) {
        model->pending_vad_end = false;
        *notify_agent = true;
    } else if (COMPANION_PRODUCT_CONNECTING == state &&
               0U != model->agent_request_id && agent_listening) {
        /* The Agent can reach LISTENING before its semantic event reaches
         * Controller. Preserve that one event for the queue-order race. */
        model->pending_vad_end = true;
    } else {
        model->pending_vad_end = true;
    }
    return ESP_OK;
}

static esp_err_t model_on_speech_confirmed(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint64_t now_ms)
{
    if (NULL == model || !model_event_is_current(
            model, generation, wake_seq)) {
        return ESP_ERR_INVALID_STATE;
    }
    const companion_product_state_t state = model->product_state;
    const bool reserved_prelisten =
        COMPANION_PRODUCT_IDLE == state && model->wake_reserved;
    if (!reserved_prelisten &&
        COMPANION_PRODUCT_LOCATING != state &&
        COMPANION_PRODUCT_TURNING != state &&
        COMPANION_PRODUCT_CONNECTING != state &&
        COMPANION_PRODUCT_LISTENING != state) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((COMPANION_PRODUCT_CONNECTING == state ||
         COMPANION_PRODUCT_LISTENING == state) &&
        (0ULL == model->state_deadline_ms ||
         model->state_deadline_ms <= now_ms)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (model->listen_voice_confirmed) {
        return (now_ms < model->listen_voice_started_ms) ?
            ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (now_ms < model->state_entered_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    if (UINT64_MAX - COMPANION_LISTENING_UTTERANCE_ABSOLUTE_DEADLINE_MS <
        now_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    model->listen_voice_confirmed = true;
    model->listen_voice_started_ms = now_ms;
    if (COMPANION_PRODUCT_LISTENING == state) {
        model->state_deadline_ms =
            now_ms + COMPANION_LISTENING_UTTERANCE_ABSOLUTE_DEADLINE_MS;
    }
    return ESP_OK;
}

static esp_err_t model_on_vad_start(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, bool agent_listening, bool *notify_agent)
{
    if (NULL == model || NULL == notify_agent) {
        return ESP_ERR_INVALID_ARG;
    }
    *notify_agent = false;
    if (!model_event_is_current(model, generation, wake_seq)) {
        return ESP_ERR_INVALID_STATE;
    }
    model->pending_vad_end = false;
    if (!agent_listening || 0U == model->agent_request_id ||
        (COMPANION_PRODUCT_CONNECTING != model->product_state &&
         COMPANION_PRODUCT_LISTENING != model->product_state)) {
        return ESP_OK;
    }
    *notify_agent = true;
    return ESP_OK;
}

static esp_err_t model_on_tts_activity(
    companion_controller_model_t *model, uint32_t generation,
    uint32_t wake_seq, uint32_t session_epoch, uint32_t request_id,
    uint64_t now_ms)
{
    if (NULL == model || 0U == generation || 0U == wake_seq ||
        0U == session_epoch || 0U == request_id) {
        return ESP_ERR_INVALID_ARG;
    }
    if (COMPANION_PRODUCT_SPEAKING != model->product_state ||
        !model_event_is_current(model, generation, wake_seq) ||
        session_epoch != model->agent_session_epoch ||
        request_id != model->agent_request_id ||
        0ULL == model->state_deadline_ms ||
        0ULL == model->speaking_absolute_deadline_ms ||
        now_ms < model->state_entered_ms ||
        model->state_deadline_ms <= now_ms ||
        model->speaking_absolute_deadline_ms <= now_ms ||
        UINT64_MAX - COMPANION_SPEAKING_INACTIVITY_DEADLINE_MS < now_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    if (model->tts_activity_seen) {
        if (now_ms < model->tts_last_activity_ms) {
            return ESP_ERR_INVALID_STATE;
        }
        if (COMPANION_TTS_ACTIVITY_REPORT_INTERVAL_MS >
            now_ms - model->tts_last_activity_ms) {
            return ESP_OK;
        }
    }
    model->tts_activity_seen = true;
    model->tts_last_activity_ms = now_ms;
    model->state_deadline_ms =
        now_ms + COMPANION_SPEAKING_INACTIVITY_DEADLINE_MS;
    return ESP_OK;
}

bool companion_controller_model_tick(
    companion_controller_model_t *model, uint64_t now_ms,
    companion_controller_deadline_t *expired)
{
    if (NULL == model || NULL == expired) {
        return false;
    }
    uint64_t effective_session_deadline_ms = model->state_deadline_ms;
    const bool speaking_absolute_is_effective =
        COMPANION_PRODUCT_SPEAKING == model->product_state &&
        0ULL != model->speaking_absolute_deadline_ms &&
        (0ULL == effective_session_deadline_ms ||
         model->speaking_absolute_deadline_ms <=
         effective_session_deadline_ms);
    if (speaking_absolute_is_effective) {
        effective_session_deadline_ms =
            model->speaking_absolute_deadline_ms;
    }
    const bool session_expired =
        0ULL != effective_session_deadline_ms &&
        effective_session_deadline_ms <= now_ms;
    const bool motion_expired =
        0ULL != model->motion_deadline_ms &&
        model->motion_deadline_ms <= now_ms;
    if (!session_expired && !motion_expired) {
        return false;
    }
    const bool report_motion = motion_expired &&
        (!session_expired ||
         model->motion_deadline_ms < effective_session_deadline_ms);
    const companion_controller_deadline_reason_t session_reason =
        (COMPANION_PRODUCT_LISTENING == model->product_state) ?
            (model->listen_voice_confirmed ?
             COMPANION_CONTROLLER_DEADLINE_LISTEN_UTTERANCE_ABSOLUTE :
             COMPANION_CONTROLLER_DEADLINE_LISTEN_FIRST_VOICE) :
        ((COMPANION_PRODUCT_SPEAKING == model->product_state) ?
         (speaking_absolute_is_effective ?
          COMPANION_CONTROLLER_DEADLINE_SPEAKING_ABSOLUTE :
          COMPANION_CONTROLLER_DEADLINE_SPEAKING_INACTIVITY) :
         COMPANION_CONTROLLER_DEADLINE_STATE);
    *expired = (companion_controller_deadline_t){
        .state = report_motion ?
            ((COMPANION_CONTROLLER_MOTION_LOCATING == model->motion_state) ?
             COMPANION_PRODUCT_LOCATING : COMPANION_PRODUCT_TURNING) :
            model->product_state,
        .motion_state = model->motion_state,
        .motion_plane = report_motion,
        .reason = report_motion ?
            COMPANION_CONTROLLER_DEADLINE_MOTION :
            session_reason,
        .generation = model->generation,
        .wake_seq = model->wake_seq,
        .agent_request_id = model->agent_request_id,
    };
    if (report_motion) {
        model->motion_deadline_ms = 0ULL;
        return true;
    }
    model->state_deadline_ms = 0ULL;
    model->speaking_absolute_deadline_ms = 0ULL;
    if (COMPANION_PRODUCT_BOOTING == expired->state) {
        model->restart_required = true;
        model->error_reason = COMPANION_CONTROLLER_ERROR_STARTUP;
        set_product_state(model, COMPANION_PRODUCT_ERROR, now_ms);
    }
    /* Agent state deadlines are Controller-owned: leave the state and token
     * observable until its side effects have been retired. */
    return true;
}

static bool model_toggle_roam(
    companion_controller_model_t *model)
{
    if (NULL == model) {
        return false;
    }
    model->roam_enabled = !model->roam_enabled;
    return model->roam_enabled;
}

static bool network_loss_cancels_state(companion_product_state_t state)
{
    return COMPANION_PRODUCT_LOCATING == state ||
           COMPANION_PRODUCT_TURNING == state ||
           COMPANION_PRODUCT_CONNECTING == state ||
           COMPANION_PRODUCT_LISTENING == state ||
           COMPANION_PRODUCT_PROCESSING == state ||
           COMPANION_PRODUCT_SPEAKING == state;
}

bool companion_controller_motion_state_is_active(
    companion_controller_motion_state_t state)
{
    return COMPANION_CONTROLLER_MOTION_LOCATING == state ||
           COMPANION_CONTROLLER_MOTION_TURNING == state ||
           COMPANION_CONTROLLER_MOTION_STOPPING == state;
}

static esp_err_t model_set_network(
    companion_controller_model_t *model, bool ready, uint32_t revision,
    uint64_t now_ms)
{
    if (NULL == model || 0U == revision) {
        return ESP_ERR_INVALID_ARG;
    }
    if (revision <= model->network_revision) {
        return ESP_ERR_INVALID_STATE;
    }
    model->network_ready = ready;
    model->network_revision = revision;
    if (!ready) {
        model->upload_gate_open = false;
        if (model->wake_reserved ||
            network_loss_cancels_state(model->product_state) ||
            companion_controller_motion_state_is_active(
                model->motion_state)) {
            model_cancel_session(model, now_ms);
        } else if (COMPANION_PRODUCT_WAIT_NETWORK ==
                   model->product_state) {
            set_product_state(model, COMPANION_PRODUCT_IDLE, now_ms);
            model->idle_entry_ms = now_ms;
        }
    } else if (COMPANION_PRODUCT_WAIT_NETWORK ==
               model->product_state) {
        set_product_state(model, COMPANION_PRODUCT_IDLE, now_ms);
        model->idle_entry_ms = now_ms;
    }
    return ESP_OK;
}

static void model_cancel_session(
    companion_controller_model_t *model, uint64_t now_ms)
{
    if (NULL == model) {
        return;
    }
    model->generation++;
    if (0U == model->generation) {
        model->generation = 1U;
    }
    reset_agent_transaction(model);
    reset_motion_transaction(model);
    set_product_state(model, COMPANION_PRODUCT_IDLE, now_ms);
    model->idle_entry_ms = now_ms;
}

static void model_finish_agent_session(
    companion_controller_model_t *model, uint64_t now_ms)
{
    if (NULL == model) {
        return;
    }
    const bool motion_pending = model->wake_requires_localization ||
        COMPANION_CONTROLLER_MOTION_LOCATING == model->motion_state ||
        COMPANION_CONTROLLER_MOTION_TURNING == model->motion_state ||
        COMPANION_CONTROLLER_MOTION_STOPPING == model->motion_state;
    if (!motion_pending) {
        model_cancel_session(model, now_ms);
        return;
    }
    model->agent_notified = false;
    model->agent_effect_attempted = true;
    model->agent_session_active = false;
    model->agent_request_id = 0U;
    model->agent_session_epoch = 0U;
    model->pending_vad_end = false;
    model->listen_voice_confirmed = false;
    model->listen_voice_started_ms = 0ULL;
    set_product_state(model, COMPANION_PRODUCT_IDLE, now_ms);
    model->idle_entry_ms = now_ms;
}

static esp_err_t model_enter_error(
    companion_controller_model_t *model,
    companion_controller_error_reason_t reason,
    bool restart_required, uint64_t now_ms)
{
    if (NULL == model || COMPANION_CONTROLLER_ERROR_NONE >= reason ||
        COMPANION_CONTROLLER_ERROR_INVARIANT < reason) {
        return ESP_ERR_INVALID_ARG;
    }
    model->generation++;
    if (0U == model->generation) {
        model->generation = 1U;
    }
    reset_agent_transaction(model);
    reset_motion_transaction(model);
    model->restart_required = restart_required;
    model->error_reason = reason;
    set_product_state(model, COMPANION_PRODUCT_ERROR, now_ms);
    return ESP_OK;
}

static esp_err_t model_recover_error(
    companion_controller_model_t *model,
    companion_controller_error_reason_t reason,
    bool core_ready, uint64_t now_ms)
{
    if (NULL == model || COMPANION_CONTROLLER_ERROR_NONE >= reason ||
        COMPANION_CONTROLLER_ERROR_INVARIANT < reason) {
        return ESP_ERR_INVALID_ARG;
    }
    if (COMPANION_PRODUCT_ERROR != model->product_state ||
        model->restart_required || !model->startup_complete || !core_ready ||
        reason != model->error_reason) {
        return ESP_ERR_INVALID_STATE;
    }
    model->error_reason = COMPANION_CONTROLLER_ERROR_NONE;
    model_cancel_session(model, now_ms);
    return ESP_OK;
}

static esp_err_t model_set_capability(
    companion_controller_model_t *model, companion_capability_t capability,
    bool available, esp_err_t error, uint32_t revision, uint64_t now_ms,
    bool *changed)
{
    if (NULL == model || NULL == changed ||
        COMPANION_CAPABILITY_MOTION > capability ||
        COMPANION_CAPABILITY_COUNT <= capability || 0U == revision) {
        return ESP_ERR_INVALID_ARG;
    }
    *changed = false;
    if (revision <= model->capability_revisions[capability]) {
        return ESP_ERR_INVALID_STATE;
    }

    model->capability_revisions[capability] = revision;
    model->capabilities[capability] = available;
    model->capability_errors[capability] = error;
    *changed = true;
    if (!model->startup_complete) {
        return ESP_OK;
    }

    if (!available && COMPANION_CAPABILITY_AUDIO == capability) {
        if (COMPANION_PRODUCT_ERROR == model->product_state &&
            model->restart_required) {
            return ESP_OK;
        }
        return model_enter_error(model, COMPANION_CONTROLLER_ERROR_AUDIO,
                                 true, now_ms);
    }
    if (!available && COMPANION_CAPABILITY_AGENT == capability) {
        if (COMPANION_PRODUCT_ERROR == model->product_state) {
            return ESP_OK;
        }
        return model_enter_error(model, COMPANION_CONTROLLER_ERROR_AGENT,
                                 false, now_ms);
    }
    if (available && COMPANION_CAPABILITY_AGENT == capability &&
        model->capabilities[COMPANION_CAPABILITY_AUDIO] &&
        COMPANION_PRODUCT_ERROR == model->product_state &&
        COMPANION_CONTROLLER_ERROR_AGENT == model->error_reason &&
        !model->restart_required) {
        return model_recover_error(model, COMPANION_CONTROLLER_ERROR_AGENT,
                                   true, now_ms);
    }
    return ESP_OK;
}

esp_err_t companion_controller_model_apply(
    companion_controller_model_t *model,
    const companion_controller_input_t *input,
    companion_controller_output_t *output)
{
    if (NULL == model || NULL == input || NULL == output ||
        COMPANION_CONTROLLER_INPUT_STARTUP_COMPLETE > input->type ||
        COMPANION_CONTROLLER_INPUT_COUNT <= input->type) {
        return ESP_ERR_INVALID_ARG;
    }

    *output = (companion_controller_output_t){0};
    esp_err_t result = ESP_OK;
    switch (input->type) {
    case COMPANION_CONTROLLER_INPUT_STARTUP_COMPLETE:
        result = model_finish_startup(model, input->data.startup.core_ready,
                                      input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_RESERVE_WAKE:
        result = model_reserve_wake(model, input->now_ms,
                                    &output->generation,
                                    &output->wake_seq);
        output->wake_requires_localization =
            model->wake_requires_localization;
        break;
    case COMPANION_CONTROLLER_INPUT_LOCATING_ACCEPTED:
        result = model_mark_locating_started(model, input->generation,
                                             input->wake_seq,
                                             input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_DOA_COMPLETED:
        result = model_on_doa(model, input->generation, input->wake_seq,
                              input->data.doa.valid,
                              input->data.doa.relative_deg,
                              &output->decision);
        break;
    case COMPANION_CONTROLLER_INPUT_MOTION_ACCEPTED:
        result = model_mark_motion_started(model, input->generation,
                                           input->wake_seq,
                                           input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_MOTION_COMPLETED:
        result = model_on_motion_done(model, input->generation,
                                      input->wake_seq,
                                      &output->decision);
        break;
    case COMPANION_CONTROLLER_INPUT_AGENT_ACCEPTED:
        result = model_mark_agent_accepted(
            model, input->generation, input->wake_seq,
            input->data.agent_accepted.request_id, input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_AGENT_FINISHED:
        if (!model_event_is_current(model, input->generation,
                                    input->wake_seq)) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        model_finish_agent_session(model, input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_AGENT_SEMANTIC:
        result = model_on_agent_semantic(
            model, input->generation, input->wake_seq,
            input->data.agent.session_epoch,
            input->data.agent.request_id,
            input->data.agent.semantic, input->data.agent.result,
            input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_SPEECH_CONFIRMED:
        result = model_on_speech_confirmed(
            model, input->generation, input->wake_seq, input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_VAD_START:
        result = model_on_vad_start(model, input->generation,
                                    input->wake_seq,
                                    input->data.vad.agent_listening,
                                    &output->notify_agent);
        break;
    case COMPANION_CONTROLLER_INPUT_VAD_END:
        result = model_on_vad_end(model, input->generation,
                                  input->wake_seq,
                                  input->data.vad.agent_listening,
                                  &output->notify_agent);
        break;
    case COMPANION_CONTROLLER_INPUT_TTS_ACTIVITY:
        result = model_on_tts_activity(
            model, input->generation, input->wake_seq,
            input->data.tts_activity.session_epoch,
            input->data.tts_activity.request_id, input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT:
        result = model_set_network(model, input->data.network.ready,
                                   input->data.network.revision,
                                   input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT:
        result = model_set_capability(
            model, input->data.capability.capability,
            input->data.capability.available,
            input->data.capability.error,
            input->data.capability.revision, input->now_ms,
            &output->capability_changed);
        break;
    case COMPANION_CONTROLLER_INPUT_SW3_CLICK:
        output->roam_enabled = model_toggle_roam(model);
        break;
    case COMPANION_CONTROLLER_INPUT_CANCEL_SESSION:
        model_cancel_session(model, input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_ENTER_ERROR:
        result = model_enter_error(model, input->data.error.reason,
                                   input->data.error.restart_required,
                                   input->now_ms);
        break;
    case COMPANION_CONTROLLER_INPUT_RECOVER_ERROR:
        result = model_recover_error(model, input->data.error.reason,
                                     input->data.error.core_ready,
                                     input->now_ms);
        break;
    default:
        result = ESP_ERR_INVALID_ARG;
        break;
    }

    if (ESP_OK == result) {
        if (0U == output->generation) {
            output->generation = model->generation;
        }
        if (0U == output->wake_seq) {
            output->wake_seq = model->wake_seq;
        }
        if (COMPANION_CONTROLLER_INPUT_SW3_CLICK != input->type) {
            output->roam_enabled = model->roam_enabled;
        }
    }
    return result;
}

void companion_controller_model_snapshot(
    const companion_controller_model_t *model,
    companion_controller_snapshot_t *snapshot)
{
    if (NULL != model && NULL != snapshot) {
        *snapshot = *model;
    }
}
