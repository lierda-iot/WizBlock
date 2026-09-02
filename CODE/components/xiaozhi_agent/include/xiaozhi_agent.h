#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    XIAOZHI_STATE_IDLE = 0,
    XIAOZHI_STATE_CONNECTING,
    XIAOZHI_STATE_CONNECTED,
    XIAOZHI_STATE_LISTENING,
    XIAOZHI_STATE_PROCESSING,
    XIAOZHI_STATE_SPEAKING,
    XIAOZHI_STATE_ERROR,
} xiaozhi_agent_state_t;

typedef void (*xiaozhi_state_cb_t)(xiaozhi_agent_state_t state, void *user_ctx);
typedef void (*xiaozhi_audio_play_cb_t)(const uint8_t *opus_data, int len, void *user_ctx);
typedef void (*xiaozhi_audio_stop_cb_t)(void *user_ctx);

typedef enum {
    XIAOZHI_AGENT_EVENT_CONNECTING = 0,
    XIAOZHI_AGENT_EVENT_LISTENING_READY,
    XIAOZHI_AGENT_EVENT_PROCESSING,
    XIAOZHI_AGENT_EVENT_SPEAKING,
    XIAOZHI_AGENT_EVENT_CLOSED,
    XIAOZHI_AGENT_EVENT_FAILED,
} xiaozhi_agent_event_type_t;

typedef struct {
    xiaozhi_agent_event_type_t type;
    xiaozhi_agent_state_t raw_state;
    uint32_t session_epoch;
    uint32_t request_id;
    esp_err_t result;
} xiaozhi_agent_event_t;

typedef enum {
    XIAOZHI_AUDIO_EVENT_PLAY = 0,
    XIAOZHI_AUDIO_EVENT_STOP,
} xiaozhi_audio_event_type_t;

typedef struct {
    xiaozhi_audio_event_type_t type;
    const uint8_t *opus_data;
    int len;
    uint32_t session_epoch;
    uint32_t request_id;
} xiaozhi_audio_event_t;

typedef void (*xiaozhi_agent_event_cb_t)(const xiaozhi_agent_event_t *event,
                                         void *user_ctx);
typedef void (*xiaozhi_audio_event_cb_t)(const xiaozhi_audio_event_t *event,
                                         void *user_ctx);

typedef struct {
    const char *ota_url;
    const char *activation_url;
    const char *device_mac;
    const char *client_id;
    const char *lang;
    const char *board_name;
    const char *app_version;
    xiaozhi_state_cb_t on_state_change;
    xiaozhi_audio_play_cb_t on_audio_play;
    xiaozhi_audio_stop_cb_t on_audio_stop;
    xiaozhi_agent_event_cb_t on_event;
    xiaozhi_audio_event_cb_t on_audio_event;
    bool allow_listening_rewake;
    bool client_manages_listen_stop;
    void *user_ctx;
} xiaozhi_agent_config_t;

esp_err_t xiaozhi_agent_init(const xiaozhi_agent_config_t *config);
esp_err_t xiaozhi_agent_start(void);
esp_err_t xiaozhi_agent_stop(void);
esp_err_t xiaozhi_agent_stop_ex(uint32_t timeout_ms);
xiaozhi_agent_state_t xiaozhi_agent_get_state(void);
esp_err_t xiaozhi_agent_send_audio(const uint8_t *data, int len);
esp_err_t xiaozhi_agent_send_audio_ex(uint32_t request_id,
                                      const uint8_t *data, int len);
esp_err_t xiaozhi_agent_notify_vad_start(void);
esp_err_t xiaozhi_agent_notify_vad_start_ex(uint32_t request_id);
esp_err_t xiaozhi_agent_notify_vad_end(void);
esp_err_t xiaozhi_agent_notify_vad_end_ex(uint32_t request_id);
esp_err_t xiaozhi_agent_notify_wake_word(void);
esp_err_t xiaozhi_agent_notify_wake_word_ex(uint32_t request_id);
esp_err_t xiaozhi_agent_cancel_request(uint32_t request_id);
