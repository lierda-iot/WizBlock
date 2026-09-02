#include "holocubic_frames.h"

#include "holocubic_periodic.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "holocubic_frames";
#define HOLO_FRAME_PARTITION_LABEL "holo_frames"
#define HOLO_FRAME_PARTITION_SUBTYPE 0x40U
#define HOLO_FRAME_READ_CHUNK_BYTES 4096U
#define HOLO_FRAME_STATS_PERIOD_MS 5000U

static SemaphoreHandle_t s_frames_mutex;

static bool load_partition_cache(holocubic_frames_t *frames)
{
    uint8_t header[HOLO_FRAME_IMAGE_HEADER_BYTES] = {0};
    holocubic_frame_image_info_t image_info = {0};
    const esp_partition_t *partition = NULL;
    uint8_t *cache_bytes = NULL;
    size_t read_offset = 0U;
    uint32_t crc_state = holocubic_frame_crc32_begin();
    const uint64_t started_ms = (uint64_t)esp_timer_get_time() / 1000ULL;

    if (NULL == frames || NULL == s_frames_mutex) return false;
    partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        (esp_partition_subtype_t)HOLO_FRAME_PARTITION_SUBTYPE,
        HOLO_FRAME_PARTITION_LABEL);
    if (NULL == partition) {
        ESP_LOGW(TAG, "Flash animation partition missing label=%s",
                 HOLO_FRAME_PARTITION_LABEL);
        return false;
    }
    if (ESP_OK != esp_partition_read(partition, 0U, header, sizeof(header)) ||
        !holocubic_frame_image_parse(header, sizeof(header), partition->size,
                                     &image_info)) {
        ESP_LOGW(TAG, "Flash animation header invalid partition_bytes=%u",
                 (unsigned int)partition->size);
        return false;
    }
    cache_bytes = heap_caps_malloc(image_info.payload_bytes,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == cache_bytes) {
        ESP_LOGW(TAG, "PSRAM animation allocation failed bytes=%u",
                 (unsigned int)image_info.payload_bytes);
        return false;
    }
    while (read_offset < image_info.payload_bytes) {
        const size_t remaining = image_info.payload_bytes - read_offset;
        const size_t read_bytes = remaining > HOLO_FRAME_READ_CHUNK_BYTES ?
            HOLO_FRAME_READ_CHUNK_BYTES : remaining;
        const esp_err_t read_result = esp_partition_read(
            partition, image_info.payload_offset + read_offset,
            cache_bytes + read_offset, read_bytes);
        if (ESP_OK != read_result) {
            ESP_LOGW(TAG,
                     "Flash animation read failed offset=%u bytes=%u error=%s",
                     (unsigned int)read_offset, (unsigned int)read_bytes,
                     esp_err_to_name(read_result));
            heap_caps_free(cache_bytes);
            return false;
        }
        crc_state = holocubic_frame_crc32_update(
            crc_state, cache_bytes + read_offset, read_bytes);
        read_offset += read_bytes;
    }
    const uint32_t actual_crc32 = holocubic_frame_crc32_finish(crc_state);
    if (image_info.payload_crc32 != actual_crc32) {
        ESP_LOGW(TAG,
                 "Flash animation CRC mismatch actual=%08lx expected=%08lx",
                 (unsigned long)actual_crc32,
                 (unsigned long)image_info.payload_crc32);
        heap_caps_free(cache_bytes);
        return false;
    }
    if (pdTRUE != xSemaphoreTake(s_frames_mutex, pdMS_TO_TICKS(50))) {
        heap_caps_free(cache_bytes);
        return false;
    }
    frames->cache = (uint16_t *)cache_bytes;
    frames->cache_index = 0U;
    frames->frame_count = image_info.frame_count;
    frames->next_frame = holocubic_frame_next_index(0U, frames->frame_count);
    frames->revision = holocubic_frame_revision_next(frames->revision);
    frames->frame_ready = true;
    frames->resource_ready = true;
    xSemaphoreGive(s_frames_mutex);
    const uint64_t elapsed_ms =
        ((uint64_t)esp_timer_get_time() / 1000ULL) - started_ms;
    ESP_LOGI(TAG,
             "Flash animation ready frames=%u bytes=%u crc32=%08lx load_ms=%llu",
             frames->frame_count, (unsigned int)image_info.payload_bytes,
             (unsigned long)actual_crc32, (unsigned long long)elapsed_ms);
    return true;
}

static bool publish_cached_frame(holocubic_frames_t *frames)
{
    if (NULL == frames || !frames->resource_ready || NULL == frames->cache ||
        NULL == s_frames_mutex ||
        pdTRUE != xSemaphoreTake(s_frames_mutex, pdMS_TO_TICKS(50))) {
        return false;
    }
    frames->cache_index = frames->next_frame;
    frames->next_frame = holocubic_frame_next_index(frames->cache_index,
                                                    frames->frame_count);
    frames->revision = holocubic_frame_revision_next(frames->revision);
    frames->frame_ready = true;
    xSemaphoreGive(s_frames_mutex);
    return true;
}

esp_err_t holocubic_frames_prepare(holocubic_frames_t *frames)
{
    if (NULL == frames) return ESP_ERR_INVALID_ARG;
    memset(frames, 0, sizeof(*frames));
    s_frames_mutex = xSemaphoreCreateMutex();
    return NULL == s_frames_mutex ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t holocubic_frames_load(holocubic_frames_t *frames)
{
    if (NULL == frames || NULL == s_frames_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!load_partition_cache(frames)) {
        ESP_LOGW(TAG, "Flash animation unavailable, using built-in animation");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t holocubic_frames_init(holocubic_frames_t *frames)
{
    const esp_err_t prepare_result = holocubic_frames_prepare(frames);
    return ESP_OK == prepare_result ? holocubic_frames_load(frames) :
                                      prepare_result;
}

void holocubic_frames_deinit(holocubic_frames_t *frames)
{
    if (NULL == frames) return;
    heap_caps_free(frames->cache);
    frames->cache = NULL;
    frames->resource_ready = false;
    frames->frame_ready = false;
    if (NULL != s_frames_mutex) {
        vSemaphoreDelete(s_frames_mutex);
        s_frames_mutex = NULL;
    }
}

void holocubic_frames_task(void *argument)
{
    holocubic_frames_t *frames = (holocubic_frames_t *)argument;
    holocubic_periodic_t periodic = {0};
    uint64_t report_started_ms = 0U;
    uint32_t published_frames = 0U;

    if (NULL == frames) {
        vTaskDelete(NULL);
        return;
    }
    report_started_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    (void)holocubic_periodic_init(&periodic, report_started_ms,
                                  HOLO_FRAME_PERIOD_MS);
    for (;;) {
        const uint64_t before_publish_ms =
            (uint64_t)esp_timer_get_time() / 1000ULL;
        const uint32_t delay_ms =
            holocubic_periodic_next_delay(&periodic, before_publish_ms);
        if (0U < delay_ms) vTaskDelay(pdMS_TO_TICKS(delay_ms));
        if (publish_cached_frame(frames)) published_frames++;

        const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
        const uint64_t elapsed_ms = now_ms - report_started_ms;
        if (HOLO_FRAME_STATS_PERIOD_MS <= elapsed_ms) {
            if (frames->resource_ready) {
                const uint32_t fps_x10 = 0U < elapsed_ms ?
                    (uint32_t)(((uint64_t)published_frames * 10000ULL) /
                               elapsed_ms) : 0U;
                ESP_LOGI(TAG,
                         "playback source=flash-psram frames=%lu fps=%lu.%lu",
                         (unsigned long)published_frames,
                         (unsigned long)(fps_x10 / 10U),
                         (unsigned long)(fps_x10 % 10U));
            }
            published_frames = 0U;
            report_started_ms = now_ms;
        }
    }
}

const uint16_t *holocubic_frames_current(const holocubic_frames_t *frames)
{
    holocubic_frame_snapshot_t snapshot = {0};
    return holocubic_frames_snapshot(frames, &snapshot) ? snapshot.pixels : NULL;
}

bool holocubic_frames_snapshot(const holocubic_frames_t *frames,
                               holocubic_frame_snapshot_t *snapshot)
{
    if (NULL == frames || NULL == snapshot || NULL == s_frames_mutex ||
        pdTRUE != xSemaphoreTake(s_frames_mutex, pdMS_TO_TICKS(10))) {
        return false;
    }
    *snapshot = (holocubic_frame_snapshot_t){0};
    if (frames->frame_ready && NULL != frames->cache) {
        snapshot->pixels = frames->cache +
            ((size_t)frames->cache_index * HOLO_FRAME_PIXELS);
        snapshot->revision = frames->revision;
    }
    xSemaphoreGive(s_frames_mutex);
    return NULL != snapshot->pixels;
}
