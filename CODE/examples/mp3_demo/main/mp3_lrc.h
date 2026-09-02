#pragma once

#include <stddef.h>
#include <stdint.h>

#define MP3_LRC_MAX_FILE_BYTES (256U * 1024U)
#define MP3_LRC_MAX_LINES 2048U
#define MP3_LRC_INDEX_NONE SIZE_MAX

typedef void *(*mp3_lrc_alloc_fn_t)(void *context, size_t size);
typedef void (*mp3_lrc_free_fn_t)(void *context, void *memory);

typedef struct {
    mp3_lrc_alloc_fn_t alloc;
    mp3_lrc_free_fn_t free;
    void *context;
} mp3_lrc_allocator_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t text_offset;
    uint32_t text_length;
} mp3_lrc_line_t;

typedef struct {
    char *text_pool;
    size_t text_pool_size;
    mp3_lrc_line_t *lines;
    size_t line_count;
    size_t malformed_count;
    size_t truncated_count;
    mp3_lrc_allocator_t allocator;
} mp3_lrc_t;

typedef enum {
    MP3_LRC_OK = 0,
    MP3_LRC_INVALID_ARGUMENT,
    MP3_LRC_FILE_TOO_LARGE,
    MP3_LRC_NO_MEMORY,
} mp3_lrc_result_t;

mp3_lrc_result_t mp3_lrc_parse(const uint8_t *data, size_t data_size,
                               const mp3_lrc_allocator_t *allocator,
                               mp3_lrc_t *lyrics);

void mp3_lrc_release(mp3_lrc_t *lyrics);

size_t mp3_lrc_find_line(const mp3_lrc_t *lyrics, uint64_t position_ms);

const char *mp3_lrc_get_text(const mp3_lrc_t *lyrics, size_t line_index);
