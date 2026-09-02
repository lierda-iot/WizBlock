#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    SemaphoreHandle_t mutex;
    portMUX_TYPE stats_lock;
    uint64_t max_wait_us;
    uint32_t acquire_count;
    uint32_t release_count;
    uint32_t timeout_count;
} mp3_spi_lock_t;

typedef struct {
    uint64_t max_wait_us;
    uint32_t acquire_count;
    uint32_t release_count;
    uint32_t timeout_count;
} mp3_spi_lock_stats_t;

bool mp3_spi_lock_init(mp3_spi_lock_t *lock);

bool mp3_spi_lock_acquire(mp3_spi_lock_t *lock, uint32_t timeout_ms);

void mp3_spi_lock_release(mp3_spi_lock_t *lock);

bool mp3_spi_lock_gmf_callback(bool acquire, void *context);

void mp3_spi_lock_get_stats(mp3_spi_lock_t *lock,
                            mp3_spi_lock_stats_t *stats);
