#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP3_CATALOG_MAX_SONGS 128U
#define MP3_TITLE_MAX_BYTES 95U
#define MP3_PATH_MAX_BYTES 255U

typedef struct {
    char title[MP3_TITLE_MAX_BYTES + 1U];
    char audio_path[MP3_PATH_MAX_BYTES + 1U];
    char lrc_path[MP3_PATH_MAX_BYTES + 1U];
    char cover_path[MP3_PATH_MAX_BYTES + 1U];
    bool has_lrc;
    bool has_cover;
} mp3_song_t;

typedef struct {
    size_t found_count;
    size_t valid_count;
    size_t rejected_count;
    size_t truncated_count;
} mp3_catalog_stats_t;

typedef struct {
    mp3_song_t *songs;
    size_t count;
    size_t capacity;
    mp3_catalog_stats_t stats;
} mp3_catalog_t;

typedef enum {
    MP3_CATALOG_ADD_STORED = 0,
    MP3_CATALOG_ADD_TRUNCATED,
    MP3_CATALOG_ADD_REJECTED,
} mp3_catalog_add_result_t;

typedef enum {
    MP3_CATALOG_REJECT_NONE = 0,
    MP3_CATALOG_REJECT_NOT_DIRECTORY,
    MP3_CATALOG_REJECT_NO_AUDIO,
    MP3_CATALOG_REJECT_INVALID_NAME,
    MP3_CATALOG_REJECT_EMPTY_TITLE,
    MP3_CATALOG_REJECT_TITLE_TOO_LONG,
    MP3_CATALOG_REJECT_PATH_TOO_LONG,
    MP3_CATALOG_REJECT_DUPLICATE,
} mp3_catalog_reject_reason_t;

bool mp3_catalog_init(mp3_catalog_t *catalog, mp3_song_t *storage,
                      size_t capacity);

mp3_catalog_add_result_t mp3_catalog_add_candidate(
    mp3_catalog_t *catalog, const char *root_path, const char *directory_name,
    bool is_directory, bool has_audio, bool has_lrc, bool has_cover,
    mp3_catalog_reject_reason_t *reject_reason);

bool mp3_catalog_is_valid_utf8(const uint8_t *text, size_t length);
