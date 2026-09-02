#pragma once

#include "mp3_spi_lock.h"

#include "esp_err.h"

#include <stdint.h>

#define MP3_COVER_MAX_FILE_BYTES (512U * 1024U)
#define MP3_COVER_MAX_SOURCE_DIMENSION 512U

typedef struct {
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t source_width;
    uint16_t source_height;
} mp3_cover_t;

esp_err_t mp3_cover_load(const char *path, mp3_spi_lock_t *spi_lock,
                         uint16_t target_width, uint16_t target_height,
                         mp3_cover_t *cover);

void mp3_cover_release(mp3_cover_t *cover);
