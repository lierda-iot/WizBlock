#include "mp3_spi_lock.h"

#include "esp_timer.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MP3_SPI_LOCK_DEFAULT_TIMEOUT_MS 1000U

static void increment_saturated(uint32_t *value)
{
    if (NULL != value && UINT32_MAX > *value) {
        ++(*value);
    }
}

bool mp3_spi_lock_init(mp3_spi_lock_t *lock)
{
    if (NULL == lock) {
        return false;
    }
    memset(lock, 0, sizeof(*lock));
    lock->stats_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    lock->mutex = xSemaphoreCreateMutex();
    return NULL != lock->mutex;
}

bool mp3_spi_lock_acquire(mp3_spi_lock_t *lock, uint32_t timeout_ms)
{
    int64_t start_us = 0;
    uint64_t wait_us = 0U;

    if (NULL == lock || NULL == lock->mutex) {
        return false;
    }
    start_us = esp_timer_get_time();
    if (pdTRUE != xSemaphoreTake(lock->mutex, pdMS_TO_TICKS(timeout_ms))) {
        portENTER_CRITICAL(&lock->stats_lock);
        increment_saturated(&lock->timeout_count);
        portEXIT_CRITICAL(&lock->stats_lock);
        return false;
    }
    wait_us = (uint64_t)(esp_timer_get_time() - start_us);
    portENTER_CRITICAL(&lock->stats_lock);
    increment_saturated(&lock->acquire_count);
    if (wait_us > lock->max_wait_us) {
        lock->max_wait_us = wait_us;
    }
    portEXIT_CRITICAL(&lock->stats_lock);
    return true;
}

void mp3_spi_lock_release(mp3_spi_lock_t *lock)
{
    if (NULL == lock || NULL == lock->mutex) {
        return;
    }
    portENTER_CRITICAL(&lock->stats_lock);
    increment_saturated(&lock->release_count);
    portEXIT_CRITICAL(&lock->stats_lock);
    (void)xSemaphoreGive(lock->mutex);
}

bool mp3_spi_lock_gmf_callback(bool acquire, void *context)
{
    mp3_spi_lock_t *lock = context;

    if (acquire) {
        return mp3_spi_lock_acquire(lock, MP3_SPI_LOCK_DEFAULT_TIMEOUT_MS);
    }
    mp3_spi_lock_release(lock);
    return true;
}

void mp3_spi_lock_get_stats(mp3_spi_lock_t *lock,
                            mp3_spi_lock_stats_t *stats)
{
    if (NULL == lock || NULL == stats) {
        return;
    }
    portENTER_CRITICAL(&lock->stats_lock);
    stats->max_wait_us = lock->max_wait_us;
    stats->acquire_count = lock->acquire_count;
    stats->release_count = lock->release_count;
    stats->timeout_count = lock->timeout_count;
    portEXIT_CRITICAL(&lock->stats_lock);
}
