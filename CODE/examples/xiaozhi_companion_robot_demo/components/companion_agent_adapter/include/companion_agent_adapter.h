#pragma once

#include "companion_core.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*companion_agent_error_cb_t)(esp_err_t error, void *user_ctx);

typedef enum {
    COMPANION_AGENT_EVENT_CONNECTING = 0,
    COMPANION_AGENT_EVENT_LISTENING_READY,
    COMPANION_AGENT_EVENT_PROCESSING,
    COMPANION_AGENT_EVENT_SPEAKING,
    COMPANION_AGENT_EVENT_CLOSED,
    COMPANION_AGENT_EVENT_FAILED,
} companion_agent_event_type_t;

typedef struct {
    companion_agent_event_type_t type;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t session_epoch;
    uint32_t request_id;
    esp_err_t result;
} companion_agent_event_t;

typedef struct {
    const uint8_t *data;
    int length;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t session_epoch;
    uint32_t request_id;
    bool stop;
} companion_agent_audio_event_t;

typedef void (*companion_agent_event_cb_t)(const companion_agent_event_t *event,
                                            void *user_ctx);
typedef void (*companion_agent_audio_event_cb_t)(
    const companion_agent_audio_event_t *event, void *user_ctx);

typedef struct {
    const char *ota_url;
    const char *activation_url;
    const char *device_mac;
    const char *client_id;
    const char *lang;
    const char *board_name;
    const char *app_version;
    companion_agent_error_cb_t on_error;
    companion_agent_event_cb_t on_event;
    companion_agent_audio_event_cb_t on_audio_event;
    void *user_ctx;
} companion_agent_adapter_config_t;

esp_err_t companion_agent_adapter_start(
    const companion_agent_adapter_config_t *config);
esp_err_t companion_agent_adapter_begin(uint32_t generation,
                                        uint32_t wake_seq,
                                        uint32_t *request_id);
esp_err_t companion_agent_adapter_notify_vad_start(uint32_t generation,
                                                   uint32_t wake_seq,
                                                   uint32_t request_id);
esp_err_t companion_agent_adapter_notify_vad_end(uint32_t generation,
                                                 uint32_t wake_seq,
                                                 uint32_t request_id);
esp_err_t companion_agent_adapter_send_audio(uint32_t generation,
                                             uint32_t wake_seq,
                                             uint32_t request_id,
                                             const uint8_t *data,
                                             int length);
esp_err_t companion_agent_adapter_cancel(uint32_t generation,
                                         uint32_t wake_seq,
                                         uint32_t request_id);
esp_err_t companion_agent_adapter_retire_binding(uint32_t generation,
                                                 uint32_t wake_seq,
                                                 uint32_t request_id);
esp_err_t companion_agent_adapter_restart(void);
bool companion_agent_adapter_is_listening(uint32_t generation,
                                          uint32_t wake_seq,
                                          uint32_t request_id);
