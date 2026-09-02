#include "companion_doa.h"
#include "companion_doa_estimator.h"

#include "companion_audio.h"
#include "esp_doa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stddef.h>

#define COMPANION_DOA_CHUNK_FRAMES 512U
#define COMPANION_DOA_CHUNK_COUNT \
    (COMPANION_AUDIO_SNAPSHOT_FRAMES / COMPANION_DOA_CHUNK_FRAMES)
#define COMPANION_DOA_MIC_SPACING_METERS 0.041f
#define COMPANION_DOA_RESOLUTION_DEG 10.0f
#define COMPANION_DOA_ENERGY_THRESHOLD_DB 56.0f
#define COMPANION_DOA_QUEUE_DEPTH 1U
#define COMPANION_DOA_TASK_STACK 8192U
#define COMPANION_DOA_TASK_PRIORITY 5U
#define COMPANION_DOA_CANCEL_TIMEOUT_MS 500U
#define COMPANION_DOA_CANCEL_POLL_MS 10U

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t request_id;
} doa_request_t;

static const char *TAG = "companion_doa";
static companion_doa_config_t s_config;
static QueueHandle_t s_queue;
static doa_handle_t *s_doa;
static int16_t *s_mic1;
static int16_t *s_mic2;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_request_lock;
static bool s_available;
static bool s_starting;
static bool s_started;
static bool s_request_queued;
static uint32_t s_queued_request_id;
static uint32_t s_active_request_id;
static uint32_t s_cancelled_request_id;
static uint32_t s_next_request_id;

static bool request_is_cancelled(uint32_t request_id)
{
    bool cancelled = false;
    if (NULL != s_request_lock &&
        pdTRUE == xSemaphoreTake(s_request_lock, portMAX_DELAY)) {
        cancelled = request_id == s_cancelled_request_id;
        xSemaphoreGive(s_request_lock);
    }
    return cancelled;
}

static float energy_db(const int16_t *mic1, const int16_t *mic2, size_t frames)
{
    if (NULL == mic1 || NULL == mic2 || 0U == frames) {
        return -120.0f;
    }
    double sum = 0.0;
    for (size_t index = 0U; index < frames; ++index) {
        const double first = mic1[index];
        const double second = mic2[index];
        sum += first * first + second * second;
    }
    if (0.0 >= sum) {
        return -120.0f;
    }
    return (float)(10.0 * log10(sum / (double)(frames * 2U) + 1e-12));
}

static uint32_t rms(const int16_t *samples, size_t count)
{
    if (NULL == samples || 0U == count) {
        return 0U;
    }
    int64_t sum = 0;
    for (size_t index = 0U; index < count; ++index) {
        sum += (int64_t)samples[index] * samples[index];
    }
    return (uint32_t)sqrtf((float)sum / (float)count);
}

static void process_request(const doa_request_t *request, int16_t *mic1,
                            int16_t *mic2)
{
    companion_doa_result_t result = {
        .generation = request->generation,
        .wake_seq = request->wake_seq,
        .request_id = request->request_id,
        .result = ESP_FAIL,
    };
    if (request_is_cancelled(request->request_id)) {
        result.result = ESP_ERR_INVALID_STATE;
        s_config.on_result(&result, s_config.user_ctx);
        return;
    }
    result.result = companion_audio_copy_snapshot(
        request->wake_seq, mic1, mic2, COMPANION_AUDIO_SNAPSHOT_FRAMES,
        &result.snapshot_version);
    if (ESP_OK == result.result) {
        result.energy_db = energy_db(mic1, mic2,
                                     COMPANION_AUDIO_SNAPSHOT_FRAMES);
        result.mic1_rms = rms(mic1, COMPANION_AUDIO_SNAPSHOT_FRAMES);
        result.mic2_rms = rms(mic2, COMPANION_AUDIO_SNAPSHOT_FRAMES);
        float angles[COMPANION_DOA_CHUNK_COUNT] = {0};
        uint32_t qualified = 0U;
        size_t angle_count = 0U;
        for (size_t chunk = 0U; chunk < COMPANION_DOA_CHUNK_COUNT; ++chunk) {
            if (request_is_cancelled(request->request_id)) {
                result.valid = false;
                result.result = ESP_ERR_INVALID_STATE;
                break;
            }
            const size_t offset = chunk * COMPANION_DOA_CHUNK_FRAMES;
            const float chunk_energy = energy_db(&mic1[offset], &mic2[offset],
                                                 COMPANION_DOA_CHUNK_FRAMES);
            if (chunk_energy < COMPANION_DOA_ENERGY_THRESHOLD_DB) {
                continue;
            }
            qualified++;
            const float raw = esp_doa_process(s_doa, &mic1[offset],
                                              &mic2[offset]);
            angles[angle_count++] = raw;
        }
        companion_doa_estimate_t estimate = {0};
        const bool cancelled = request_is_cancelled(request->request_id);
        result.valid = !cancelled &&
            companion_doa_estimate(angles, angle_count, &estimate);
        if (cancelled) {
            result.result = ESP_ERR_INVALID_STATE;
        } else if (result.valid) {
            result.raw_deg = angles[angle_count - 1U];
            result.filtered_deg = estimate.angle_deg;
            result.relative_deg = estimate.relative_deg;
            result.result = ESP_OK;
            ESP_LOGI(TAG,
                     "angle_consensus generation=%lu wake_seq=%lu DIR=%u ANGLE=%.1f RAW_REL=%.1f ACTUAL_REL=%.1f VOTES=%u/%u MAD=%.1f",
                     (unsigned long)result.generation,
                     (unsigned long)result.wake_seq,
                     (unsigned int)estimate.direction, estimate.angle_deg,
                     estimate.raw_relative_deg, estimate.relative_deg,
                     (unsigned int)estimate.winning_votes,
                     (unsigned int)estimate.sample_count, estimate.mad_deg);
        } else {
            result.result = ESP_ERR_INVALID_RESPONSE;
        }
        ESP_LOGI(TAG,
                 "result generation=%lu wake_seq=%lu snapshot=%lu RAW=%.1f FILT=%.1f REL=%.1f QUALIFIED=%lu USED=%u energy=%.1f mic1_rms=%lu mic2_rms=%lu valid=%u error=%s",
                 (unsigned long)result.generation,
                 (unsigned long)result.wake_seq,
                 (unsigned long)result.snapshot_version, result.raw_deg,
                 result.filtered_deg, result.relative_deg,
                 (unsigned long)qualified, (unsigned int)estimate.sample_count,
                 result.energy_db,
                 (unsigned long)result.mic1_rms,
                 (unsigned long)result.mic2_rms, result.valid ? 1U : 0U,
                 esp_err_to_name(result.result));
    } else {
        ESP_LOGW(TAG,
                 "snapshot unavailable generation=%lu wake_seq=%lu error=%s",
                 (unsigned long)request->generation,
                 (unsigned long)request->wake_seq,
                 esp_err_to_name(result.result));
    }
    s_config.on_result(&result, s_config.user_ctx);
}

static void doa_task(void *arg)
{
    (void)arg;
    while (true) {
        doa_request_t request = {0};
        if (pdTRUE == xQueueReceive(s_queue, &request, portMAX_DELAY)) {
            (void)xSemaphoreTake(s_request_lock, portMAX_DELAY);
            if (s_request_queued &&
                request.request_id == s_queued_request_id) {
                s_request_queued = false;
                s_queued_request_id = 0U;
            }
            s_active_request_id = request.request_id;
            xSemaphoreGive(s_request_lock);
            process_request(&request, s_mic1, s_mic2);
            (void)xSemaphoreTake(s_request_lock, portMAX_DELAY);
            if (request.request_id == s_active_request_id) {
                s_active_request_id = 0U;
            }
            if (request.request_id == s_cancelled_request_id) {
                s_cancelled_request_id = 0U;
            }
            xSemaphoreGive(s_request_lock);
        }
    }
}

static void release_resources(void)
{
    if (NULL != s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (NULL != s_request_lock) {
        vSemaphoreDelete(s_request_lock);
        s_request_lock = NULL;
    }
    if (NULL != s_doa) {
        esp_doa_destroy(s_doa);
        s_doa = NULL;
    }
    heap_caps_free(s_mic1);
    heap_caps_free(s_mic2);
    s_mic1 = NULL;
    s_mic2 = NULL;
}

esp_err_t companion_doa_start(const companion_doa_config_t *config)
{
    if (NULL == config || NULL == config->on_result) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_state_lock);
    const bool available = s_available;
    const bool starting = s_starting;
    const bool started = s_started;
    if (!available && !starting && !started) {
        s_starting = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (available) {
        return ESP_OK;
    }
    if (starting) {
        return ESP_ERR_INVALID_STATE;
    }
    if (started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = *config;
    s_doa = esp_doa_create(COMPANION_AUDIO_SAMPLE_RATE_HZ,
                           COMPANION_DOA_RESOLUTION_DEG,
                           COMPANION_DOA_MIC_SPACING_METERS,
                           COMPANION_DOA_CHUNK_FRAMES);
    if (NULL == s_doa) {
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_FAIL;
    }
    s_queue = xQueueCreate(COMPANION_DOA_QUEUE_DEPTH, sizeof(doa_request_t));
    s_request_lock = xSemaphoreCreateMutex();
    if (NULL == s_queue || NULL == s_request_lock) {
        release_resources();
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_NO_MEM;
    }
    const size_t bytes = COMPANION_AUDIO_SNAPSHOT_FRAMES * sizeof(int16_t);
    s_mic1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    s_mic2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (NULL == s_mic1 || NULL == s_mic2) {
        release_resources();
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_NO_MEM;
    }
    (void)xSemaphoreTake(s_request_lock, portMAX_DELAY);
    s_request_queued = false;
    s_queued_request_id = 0U;
    s_active_request_id = 0U;
    s_cancelled_request_id = 0U;
    s_next_request_id = 0U;
    xSemaphoreGive(s_request_lock);
    BaseType_t task_result = xTaskCreatePinnedToCore(
        doa_task, "companion_doa", COMPANION_DOA_TASK_STACK, NULL,
        COMPANION_DOA_TASK_PRIORITY, NULL, 1);
    if (pdPASS != task_result) {
        release_resources();
        portENTER_CRITICAL(&s_state_lock);
        s_starting = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_state_lock);
    s_available = true;
    s_starting = false;
    s_started = true;
    portEXIT_CRITICAL(&s_state_lock);
    ESP_LOGI(TAG,
             "DOA ready sample=16000 chunk=512 chunks=%u snapshot=%ums spacing=0.041m resolution=10deg block_threshold=56dB consensus_window=12",
             COMPANION_DOA_CHUNK_COUNT,
             (COMPANION_AUDIO_SNAPSHOT_FRAMES * 1000U) /
                 COMPANION_AUDIO_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t companion_doa_request(uint32_t generation, uint32_t wake_seq)
{
    uint32_t request_id = 0U;
    return companion_doa_request_ex(generation, wake_seq, &request_id);
}

esp_err_t companion_doa_request_ex(uint32_t generation, uint32_t wake_seq,
                                   uint32_t *request_id)
{
    if (0U == generation || 0U == wake_seq || NULL == request_id) {
        return ESP_ERR_INVALID_ARG;
    }
    doa_request_t request = {0};
    esp_err_t result = ESP_OK;
    if (NULL == s_request_lock ||
        pdTRUE != xSemaphoreTake(s_request_lock, pdMS_TO_TICKS(100U))) {
        return ESP_ERR_TIMEOUT;
    }
    bool available = false;
    portENTER_CRITICAL(&s_state_lock);
    available = s_available;
    portEXIT_CRITICAL(&s_state_lock);
    if (!available || NULL == s_queue || s_request_queued ||
        0U != s_active_request_id) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_next_request_id++;
        if (0U == s_next_request_id) {
            s_next_request_id = 1U;
        }
        request = (doa_request_t){
            .generation = generation,
            .wake_seq = wake_seq,
            .request_id = s_next_request_id,
        };
        s_request_queued = true;
        s_queued_request_id = request.request_id;
        if (pdTRUE != xQueueSend(s_queue, &request, 0)) {
            s_request_queued = false;
            s_queued_request_id = 0U;
            result = ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreGive(s_request_lock);
    if (ESP_OK != result) {
        return result;
    }
    *request_id = request.request_id;
    return ESP_OK;
}

esp_err_t companion_doa_cancel(uint32_t request_id)
{
    if (0U == request_id) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_ERR_NOT_FOUND;
    bool wait_for_ack = false;
    if (NULL == s_request_lock ||
        pdTRUE != xSemaphoreTake(s_request_lock, pdMS_TO_TICKS(100U))) {
        return ESP_ERR_TIMEOUT;
    }
    if (request_id == s_active_request_id) {
        s_cancelled_request_id = request_id;
        result = ESP_OK;
        wait_for_ack = true;
    } else if (s_request_queued && request_id == s_queued_request_id &&
               NULL != s_queue) {
        if (0U < uxQueueMessagesWaiting(s_queue)) {
            xQueueReset(s_queue);
            s_request_queued = false;
            s_queued_request_id = 0U;
            s_cancelled_request_id = 0U;
        } else {
            s_cancelled_request_id = request_id;
            wait_for_ack = true;
        }
        result = ESP_OK;
    }
    xSemaphoreGive(s_request_lock);
    if (!wait_for_ack) {
        return result;
    }

    const TickType_t start_tick = xTaskGetTickCount();
    while (true) {
        bool pending = true;
        if (pdTRUE == xSemaphoreTake(s_request_lock,
                                     pdMS_TO_TICKS(100U))) {
            pending = request_id == s_active_request_id ||
                      (s_request_queued &&
                       request_id == s_queued_request_id);
            if (!pending && request_id == s_cancelled_request_id) {
                s_cancelled_request_id = 0U;
            }
            xSemaphoreGive(s_request_lock);
        }
        if (!pending) {
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - start_tick) >=
            pdMS_TO_TICKS(COMPANION_DOA_CANCEL_TIMEOUT_MS)) {
            portENTER_CRITICAL(&s_state_lock);
            s_available = false;
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGE(TAG,
                     "DOA cancel acknowledgement timeout request=%lu; capability unavailable",
                     (unsigned long)request_id);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(COMPANION_DOA_CANCEL_POLL_MS));
    }
}

bool companion_doa_is_available(void)
{
    bool available = false;
    portENTER_CRITICAL(&s_state_lock);
    available = s_available;
    portEXIT_CRITICAL(&s_state_lock);
    return available;
}
