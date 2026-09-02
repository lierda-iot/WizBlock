#pragma once

#include "companion_logic.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMPANION_CAPABILITY_MOTION = 0,
    COMPANION_CAPABILITY_INPUT,
    COMPANION_CAPABILITY_TOUCH,
    COMPANION_CAPABILITY_UI,
    COMPANION_CAPABILITY_AUDIO,
    COMPANION_CAPABILITY_DOA,
    COMPANION_CAPABILITY_AGENT,
    COMPANION_CAPABILITY_COUNT,
} companion_capability_t;

typedef enum {
    COMPANION_CONTROLLER_ERROR_NONE = 0,
    COMPANION_CONTROLLER_ERROR_STARTUP,
    COMPANION_CONTROLLER_ERROR_AUDIO,
    COMPANION_CONTROLLER_ERROR_AGENT,
    COMPANION_CONTROLLER_ERROR_INVARIANT,
} companion_controller_error_reason_t;

typedef enum {
    COMPANION_CONTROLLER_MOTION_IDLE = 0,
    COMPANION_CONTROLLER_MOTION_LOCATING,
    COMPANION_CONTROLLER_MOTION_TURNING,
    COMPANION_CONTROLLER_MOTION_STOPPING,
    COMPANION_CONTROLLER_MOTION_COMPLETE,
    COMPANION_CONTROLLER_MOTION_ERROR,
    COMPANION_CONTROLLER_MOTION_STATE_COUNT,
} companion_controller_motion_state_t;

typedef struct {
    companion_product_state_t product_state;
    companion_controller_motion_state_t motion_state;
    uint32_t generation;
    uint32_t wake_seq;
    bool roam_enabled;
    bool upload_gate_open;
    bool agent_notified;
    bool agent_effect_attempted;
    bool agent_session_started;
    bool agent_session_active;
    bool wake_reserved;
    bool wake_requires_localization;
    bool startup_complete;
    bool pending_vad_end;
    bool listen_voice_confirmed;
    uint64_t listen_voice_started_ms;
    bool restart_required;
    companion_controller_error_reason_t error_reason;
    uint32_t agent_request_id;
    uint32_t agent_session_epoch;
    uint64_t state_entered_ms;
    uint64_t state_deadline_ms;
    uint64_t speaking_absolute_deadline_ms;
    bool tts_activity_seen;
    uint64_t tts_last_activity_ms;
    uint64_t motion_state_entered_ms;
    uint64_t motion_deadline_ms;
    uint64_t idle_entry_ms;
    bool network_ready;
    uint32_t network_revision;
    bool capabilities[COMPANION_CAPABILITY_COUNT];
    uint32_t capability_revisions[COMPANION_CAPABILITY_COUNT];
    esp_err_t capability_errors[COMPANION_CAPABILITY_COUNT];
} companion_controller_model_t;

typedef companion_controller_model_t companion_controller_snapshot_t;

typedef enum {
    COMPANION_AGENT_SEMANTIC_CONNECTING = 0,
    COMPANION_AGENT_SEMANTIC_LISTENING_READY,
    COMPANION_AGENT_SEMANTIC_PROCESSING,
    COMPANION_AGENT_SEMANTIC_SPEAKING,
    COMPANION_AGENT_SEMANTIC_CLOSED,
    COMPANION_AGENT_SEMANTIC_FAILED,
} companion_agent_semantic_t;

typedef enum {
    COMPANION_CONTROLLER_DEADLINE_STATE = 0,
    COMPANION_CONTROLLER_DEADLINE_MOTION,
    COMPANION_CONTROLLER_DEADLINE_LISTEN_FIRST_VOICE,
    COMPANION_CONTROLLER_DEADLINE_LISTEN_UTTERANCE_ABSOLUTE,
    COMPANION_CONTROLLER_DEADLINE_SPEAKING_INACTIVITY,
    COMPANION_CONTROLLER_DEADLINE_SPEAKING_ABSOLUTE,
} companion_controller_deadline_reason_t;

typedef struct {
    companion_product_state_t state;
    companion_controller_motion_state_t motion_state;
    bool motion_plane;
    companion_controller_deadline_reason_t reason;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t agent_request_id;
} companion_controller_deadline_t;

typedef enum {
    COMPANION_CONTROLLER_DECISION_NONE = 0,
    COMPANION_CONTROLLER_DECISION_START_TURN,
    /* Compatibility-only: the current model never emits this decision. */
    COMPANION_CONTROLLER_DECISION_NOTIFY_AGENT,
} companion_controller_decision_type_t;

typedef struct {
    companion_controller_decision_type_t type;
    companion_turn_plan_t turn;
} companion_controller_decision_t;

typedef enum {
    COMPANION_CONTROLLER_INPUT_STARTUP_COMPLETE = 0,
    COMPANION_CONTROLLER_INPUT_RESERVE_WAKE,
    COMPANION_CONTROLLER_INPUT_LOCATING_ACCEPTED,
    COMPANION_CONTROLLER_INPUT_DOA_COMPLETED,
    COMPANION_CONTROLLER_INPUT_MOTION_ACCEPTED,
    COMPANION_CONTROLLER_INPUT_MOTION_COMPLETED,
    COMPANION_CONTROLLER_INPUT_AGENT_ACCEPTED,
    COMPANION_CONTROLLER_INPUT_AGENT_FINISHED,
    COMPANION_CONTROLLER_INPUT_AGENT_SEMANTIC,
    COMPANION_CONTROLLER_INPUT_SPEECH_CONFIRMED,
    COMPANION_CONTROLLER_INPUT_VAD_START,
    COMPANION_CONTROLLER_INPUT_VAD_END,
    COMPANION_CONTROLLER_INPUT_TTS_ACTIVITY,
    COMPANION_CONTROLLER_INPUT_NETWORK_SNAPSHOT,
    COMPANION_CONTROLLER_INPUT_CAPABILITY_SNAPSHOT,
    COMPANION_CONTROLLER_INPUT_SW3_CLICK,
    COMPANION_CONTROLLER_INPUT_CANCEL_SESSION,
    COMPANION_CONTROLLER_INPUT_ENTER_ERROR,
    COMPANION_CONTROLLER_INPUT_RECOVER_ERROR,
    COMPANION_CONTROLLER_INPUT_COUNT,
} companion_controller_input_type_t;

typedef struct {
    companion_controller_input_type_t type;
    uint32_t generation;
    uint32_t wake_seq;
    uint64_t now_ms;
    union {
        struct {
            bool core_ready;
        } startup;
        struct {
            bool valid;
            float relative_deg;
        } doa;
        struct {
            uint32_t request_id;
        } agent_accepted;
        struct {
            uint32_t session_epoch;
            uint32_t request_id;
            companion_agent_semantic_t semantic;
            esp_err_t result;
        } agent;
        struct {
            bool ready;
            uint32_t revision;
        } network;
        struct {
            bool agent_listening;
        } vad;
        struct {
            uint32_t session_epoch;
            uint32_t request_id;
        } tts_activity;
        struct {
            companion_capability_t capability;
            bool available;
            esp_err_t error;
            uint32_t revision;
        } capability;
        struct {
            companion_controller_error_reason_t reason;
            bool restart_required;
            bool core_ready;
        } error;
    } data;
} companion_controller_input_t;

typedef struct {
    companion_controller_decision_t decision;
    uint32_t generation;
    uint32_t wake_seq;
    bool notify_agent;
    bool wake_requires_localization;
    bool roam_enabled;
    bool capability_changed;
} companion_controller_output_t;

void companion_controller_model_init(companion_controller_model_t *model,
                                     uint64_t now_ms);
esp_err_t companion_controller_model_apply(
    companion_controller_model_t *model,
    const companion_controller_input_t *input,
    companion_controller_output_t *output);
bool companion_controller_model_tick(
    companion_controller_model_t *model, uint64_t now_ms,
    companion_controller_deadline_t *expired);
void companion_controller_model_snapshot(
    const companion_controller_model_t *model,
    companion_controller_snapshot_t *snapshot);
bool companion_controller_motion_state_is_active(
    companion_controller_motion_state_t state);
