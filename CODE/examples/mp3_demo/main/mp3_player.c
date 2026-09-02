#include "mp3_player.h"

#include "esp_player.h"
#include "freertos/FreeRTOS.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    esp_player_handle_t handle;
    mp3_player_event_cb_t callback;
    void *callback_context;
    portMUX_TYPE state_lock;
    uint32_t generation;
    uint16_t song_index;
    mp3_player_state_t state;
    mp3_player_state_t state_before_seek;
    bool initialized;
} mp3_player_context_t;

static mp3_player_context_t s_player;

static esp_err_t convert_error(esp_player_err_t error)
{
    return (ESP_PLAYER_ERR_OK == error) ? ESP_OK : ESP_FAIL;
}

static esp_player_err_t player_event_callback(esp_player_event_msg_t *message,
                                              void *context)
{
    mp3_player_context_t *player = context;
    mp3_player_event_t event = {0};
    bool publish = true;

    if (NULL == player || NULL == message) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&player->state_lock);
    event.generation = player->generation;
    event.song_index = player->song_index;
    switch (message->event_type) {
    case ESP_PLAYER_EVENT_PLAYED:
        event.type = MP3_PLAYER_EVENT_PLAYED;
        player->state = MP3_PLAYER_STATE_PLAYING;
        break;
    case ESP_PLAYER_EVENT_PAUSED:
        event.type = MP3_PLAYER_EVENT_PAUSED;
        player->state = MP3_PLAYER_STATE_PAUSED;
        break;
    case ESP_PLAYER_EVENT_STOPPED:
        event.type = MP3_PLAYER_EVENT_STOPPED;
        player->state = MP3_PLAYER_STATE_STOPPED;
        break;
    case ESP_PLAYER_EVENT_SEEK_DONE:
        event.type = MP3_PLAYER_EVENT_SEEK_DONE;
        player->state = player->state_before_seek;
        break;
    case ESP_PLAYER_EVENT_FINISHED:
        event.type = MP3_PLAYER_EVENT_FINISHED;
        player->state = MP3_PLAYER_STATE_STOPPED;
        break;
    case ESP_PLAYER_EVENT_ERROR:
        event.type = MP3_PLAYER_EVENT_ERROR;
        player->state = MP3_PLAYER_STATE_ERROR;
        if (NULL != message->data &&
            message->data_len >= sizeof(esp_player_error_source_t)) {
            event.error_source = *(esp_player_error_source_t *)message->data;
        }
        break;
    default:
        publish = false;
        break;
    }
    portEXIT_CRITICAL(&player->state_lock);

    if (publish && NULL != player->callback) {
        player->callback(&event, player->callback_context);
    }
    return ESP_PLAYER_ERR_OK;
}

esp_err_t mp3_player_init(esp_audio_render_stream_handle_t render_stream,
                          mp3_player_event_cb_t callback, void *context)
{
    if (NULL == render_stream || s_player.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_player, 0, sizeof(s_player));
    s_player.state_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    s_player.callback = callback;
    s_player.callback_context = context;
    s_player.state = MP3_PLAYER_STATE_EMPTY;

    esp_player_config_t config = ESP_PLAYER_CONFIG_DEFAULT();
    config.audio_render_hd = render_stream;
    if (ESP_PLAYER_ERR_OK != esp_player_init(&config, &s_player.handle)) {
        memset(&s_player, 0, sizeof(s_player));
        return ESP_FAIL;
    }
    if (ESP_PLAYER_ERR_OK != esp_player_set_event_cb(
            s_player.handle, player_event_callback, &s_player)) {
        esp_player_deinit(s_player.handle);
        memset(&s_player, 0, sizeof(s_player));
        return ESP_FAIL;
    }
    s_player.initialized = true;
    return ESP_OK;
}

esp_err_t mp3_player_play(const char *path, uint16_t song_index,
                          uint32_t generation)
{
    if (!s_player.initialized || NULL == path || '\0' == path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ESP_PLAYER_ERR_OK != esp_player_stop(s_player.handle)) {
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_player.state_lock);
    s_player.generation = generation;
    s_player.song_index = song_index;
    s_player.state = MP3_PLAYER_STATE_LOADING;
    portEXIT_CRITICAL(&s_player.state_lock);

    esp_player_data_src_t source =
        ESP_PLAYER_DATA_SRC(path, ESP_PLAYER_MASK_AUDIO);
    esp_player_err_t result = esp_player_set_data_src(s_player.handle, &source);
    if (ESP_PLAYER_ERR_OK == result) {
        result = esp_player_run(s_player.handle);
    }
    if (ESP_PLAYER_ERR_OK != result) {
        portENTER_CRITICAL(&s_player.state_lock);
        s_player.state = MP3_PLAYER_STATE_ERROR;
        portEXIT_CRITICAL(&s_player.state_lock);
    }
    return convert_error(result);
}

esp_err_t mp3_player_pause(void)
{
    if (!s_player.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return convert_error(esp_player_pause(s_player.handle));
}

esp_err_t mp3_player_resume(void)
{
    if (!s_player.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return convert_error(esp_player_resume(s_player.handle));
}

esp_err_t mp3_player_seek(uint32_t generation, uint64_t target_ms)
{
    mp3_player_state_t previous_state = MP3_PLAYER_STATE_EMPTY;

    if (!s_player.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_player.state_lock);
    bool current = generation == s_player.generation &&
                   (MP3_PLAYER_STATE_PLAYING == s_player.state ||
                    MP3_PLAYER_STATE_PAUSED == s_player.state);
    if (current) {
        previous_state = s_player.state;
        s_player.state_before_seek = previous_state;
        s_player.state = MP3_PLAYER_STATE_SEEKING;
    }
    portEXIT_CRITICAL(&s_player.state_lock);
    if (!current) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = convert_error(
        esp_player_seek(s_player.handle, target_ms));
    if (ESP_OK != result) {
        portENTER_CRITICAL(&s_player.state_lock);
        if (generation == s_player.generation &&
            MP3_PLAYER_STATE_SEEKING == s_player.state) {
            s_player.state = previous_state;
        }
        portEXIT_CRITICAL(&s_player.state_lock);
    }
    return result;
}

esp_err_t mp3_player_stop(void)
{
    if (!s_player.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return convert_error(esp_player_stop(s_player.handle));
}

bool mp3_player_get_snapshot(mp3_player_snapshot_t *snapshot)
{
    uint64_t position_ms = 0U;
    uint64_t duration_ms = 0U;

    if (!s_player.initialized || NULL == snapshot) {
        return false;
    }
    portENTER_CRITICAL(&s_player.state_lock);
    snapshot->generation = s_player.generation;
    snapshot->song_index = s_player.song_index;
    snapshot->state = s_player.state;
    portEXIT_CRITICAL(&s_player.state_lock);

    if (ESP_PLAYER_ERR_OK ==
        esp_player_get_play_time(s_player.handle, &position_ms)) {
        snapshot->position_ms = position_ms;
    } else {
        snapshot->position_ms = 0U;
    }
    if (ESP_PLAYER_ERR_OK ==
        esp_player_get_duration(s_player.handle, &duration_ms)) {
        snapshot->duration_ms = duration_ms;
    } else {
        snapshot->duration_ms = 0U;
    }
    return true;
}

void mp3_player_deinit(void)
{
    if (!s_player.initialized) {
        return;
    }
    (void)esp_player_set_event_cb(s_player.handle, NULL, NULL);
    (void)esp_player_stop(s_player.handle);
    (void)esp_player_deinit(s_player.handle);
    memset(&s_player, 0, sizeof(s_player));
}
