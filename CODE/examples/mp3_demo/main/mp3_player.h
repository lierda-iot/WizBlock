#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_audio_render.h"
#include "esp_err.h"

typedef enum {
    MP3_PLAYER_STATE_EMPTY = 0,
    MP3_PLAYER_STATE_LOADING,
    MP3_PLAYER_STATE_PLAYING,
    MP3_PLAYER_STATE_PAUSED,
    MP3_PLAYER_STATE_SEEKING,
    MP3_PLAYER_STATE_STOPPED,
    MP3_PLAYER_STATE_ERROR,
} mp3_player_state_t;

typedef enum {
    MP3_PLAYER_EVENT_PLAYED = 0,
    MP3_PLAYER_EVENT_PAUSED,
    MP3_PLAYER_EVENT_STOPPED,
    MP3_PLAYER_EVENT_SEEK_DONE,
    MP3_PLAYER_EVENT_FINISHED,
    MP3_PLAYER_EVENT_ERROR,
} mp3_player_event_type_t;

typedef struct {
    mp3_player_event_type_t type;
    uint32_t generation;
    uint16_t song_index;
    int32_t error_source;
} mp3_player_event_t;

typedef struct {
    uint32_t generation;
    uint16_t song_index;
    mp3_player_state_t state;
    uint64_t position_ms;
    uint64_t duration_ms;
} mp3_player_snapshot_t;

typedef void (*mp3_player_event_cb_t)(const mp3_player_event_t *event,
                                      void *context);

esp_err_t mp3_player_init(esp_audio_render_stream_handle_t render_stream,
                          mp3_player_event_cb_t callback, void *context);

esp_err_t mp3_player_play(const char *path, uint16_t song_index,
                          uint32_t generation);

esp_err_t mp3_player_pause(void);

esp_err_t mp3_player_resume(void);

esp_err_t mp3_player_seek(uint32_t generation, uint64_t target_ms);

esp_err_t mp3_player_stop(void);

bool mp3_player_get_snapshot(mp3_player_snapshot_t *snapshot);

void mp3_player_deinit(void);
