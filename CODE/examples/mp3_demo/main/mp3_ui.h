#pragma once

#include "mp3_catalog.h"
#include "mp3_cover.h"
#include "mp3_lrc.h"
#include "mp3_player.h"
#include "mp3_progress.h"
#include "mp3_spi_lock.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP3_UI_LCD_PIXEL_CLOCK_HZ 40000000U
#define MP3_UI_LCD_BUFFER_LINES 80U
#define MP3_UI_COVER_WIDTH 96U
#define MP3_UI_COVER_HEIGHT 96U

typedef enum {
    MP3_UI_COMMAND_PREVIOUS = 0,
    MP3_UI_COMMAND_TOGGLE_PLAYBACK,
    MP3_UI_COMMAND_NEXT,
    MP3_UI_COMMAND_SELECT_SONG,
    MP3_UI_COMMAND_SEEK,
} mp3_ui_command_type_t;

typedef struct {
    mp3_ui_command_type_t type;
    uint16_t song_index;
    mp3_seek_request_t seek;
} mp3_ui_command_t;

typedef void (*mp3_ui_command_cb_t)(const mp3_ui_command_t *command,
                                    void *context);

typedef struct {
    const mp3_song_t *songs;
    size_t song_count;
    mp3_spi_lock_t *spi_lock;
    mp3_ui_command_cb_t command_callback;
    void *command_context;
    bool touch_available;
} mp3_ui_config_t;

esp_err_t mp3_ui_init(const mp3_ui_config_t *config);

esp_err_t mp3_ui_set_catalog(const mp3_song_t *songs, size_t song_count);

void mp3_ui_process(void);

void mp3_ui_show_status(const char *title, const char *message,
                        bool is_error);

void mp3_ui_clear_song(uint32_t generation, uint16_t song_index,
                       const char *title);

void mp3_ui_show_song(uint32_t generation, uint16_t song_index,
                      const char *title, const mp3_lrc_t *lyrics,
                      const mp3_cover_t *cover);

void mp3_ui_update_snapshot(const mp3_player_snapshot_t *snapshot);

void mp3_ui_seek_completed(uint32_t generation, esp_err_t result);

uint32_t mp3_ui_get_lcd_error_count(void);

uint32_t mp3_ui_get_touch_error_count(void);
