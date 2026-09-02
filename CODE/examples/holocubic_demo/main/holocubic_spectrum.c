#include "holocubic_spectrum.h"

#include "audio_spatial_spectrum_doa_filter.h"
#include "audio_spatial_spectrum_processor.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "holocubic_startup_policy.h"

#include "esp_doa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define HOLO_SPECTRUM_SAMPLE_RATE_HZ BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define HOLO_SPECTRUM_TDM_CHANNELS 4U
#define HOLO_SPECTRUM_MIC1_SLOT 0U
#define HOLO_SPECTRUM_MIC2_SLOT 2U
#define HOLO_SPECTRUM_MIC_SPACING_M 0.041f
#define HOLO_SPECTRUM_DOA_RESOLUTION_DEG 10.0f
#define HOLO_SPECTRUM_DOA_ENERGY_THRESHOLD_DB 56.0f
#define HOLO_SPECTRUM_DOA_DISPLAY_GAIN 2.0f
#define HOLO_SPECTRUM_DOA_IDLE_HOLD_MS 400U
#define HOLO_SPECTRUM_TASK_STACK 12288U
#define HOLO_SPECTRUM_TASK_PRIORITY 3U
#define HOLO_SPECTRUM_REPORT_MS 2000U

static const char *TAG = "holocubic_spectrum";
static SemaphoreHandle_t s_snapshot_mutex;
static holocubic_spectrum_snapshot_t s_snapshot;
static holocubic_spectrum_raster_state_t s_raster_visual;
static bool s_started;

static void delete_current_spectrum_task(void)
{
    if (HOLO_TASK_STACK_EXTERNAL ==
        holocubic_startup_task_stack(HOLO_STARTUP_STEP_SPECTRUM)) {
        vTaskDeleteWithCaps(NULL);
    } else {
        vTaskDelete(NULL);
    }
}

void holocubic_spectrum_draw(uint16_t *canvas,
                             const holocubic_spectrum_snapshot_t *snapshot,
                             holocubic_spectrum_mode_t mode,
                             uint32_t now_ms)
{
    holocubic_spectrum_raster_draw(canvas, snapshot, mode, now_ms,
                                   &s_raster_visual);
}

static void publish_snapshot(const audio_spatial_spectrum_result_t *spectrum,
                             bool doa_active, float relative_angle_deg)
{
    if (NULL == spectrum || NULL == s_snapshot_mutex ||
        pdTRUE != xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(10U))) {
        return;
    }
    memcpy(s_snapshot.combined_levels, spectrum->combined_levels,
           sizeof(s_snapshot.combined_levels));
    memcpy(s_snapshot.combined_peaks, spectrum->combined_peaks,
           sizeof(s_snapshot.combined_peaks));
    memcpy(s_snapshot.mic1_levels, spectrum->mic1_levels,
           sizeof(s_snapshot.mic1_levels));
    memcpy(s_snapshot.mic1_peaks, spectrum->mic1_peaks,
           sizeof(s_snapshot.mic1_peaks));
    memcpy(s_snapshot.mic2_levels, spectrum->mic2_levels,
           sizeof(s_snapshot.mic2_levels));
    memcpy(s_snapshot.mic2_peaks, spectrum->mic2_peaks,
           sizeof(s_snapshot.mic2_peaks));
    s_snapshot.energy_db = spectrum->energy_db;
    s_snapshot.energy_dbfs = spectrum->energy_dbfs;
    s_snapshot.mic1_level = spectrum->mic1_level;
    s_snapshot.mic2_level = spectrum->mic2_level;
    s_snapshot.mic1_rms = spectrum->mic1_rms;
    s_snapshot.mic2_rms = spectrum->mic2_rms;
    s_snapshot.relative_angle_deg = relative_angle_deg;
    s_snapshot.doa_active = doa_active;
    s_snapshot.available = true;
    s_snapshot.revision++;
    if (0U == s_snapshot.revision) s_snapshot.revision = 1U;
    xSemaphoreGive(s_snapshot_mutex);
}

static void spectrum_task(void *argument)
{
    esp_err_t result = ESP_OK;
    doa_handle_t *doa = NULL;
    int16_t *tdm_samples = NULL;
    int16_t *mic1_samples = NULL;
    int16_t *mic2_samples = NULL;
    spatial_doa_filter_t doa_filter = {0};
    audio_spatial_spectrum_result_t spectrum = {0};
    float filtered_angle_deg = 90.0f;
    float relative_angle_deg = 0.0f;
    TickType_t last_active_tick = 0U;
    TickType_t last_report_tick = 0U;
    bool have_direction = false;

    (void)argument;
    result = audio_spatial_spectrum_processor_init(HOLO_SPECTRUM_SAMPLE_RATE_HZ);
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "processor init failed: %s", esp_err_to_name(result));
        delete_current_spectrum_task();
        return;
    }
    doa = esp_doa_create(HOLO_SPECTRUM_SAMPLE_RATE_HZ,
                         HOLO_SPECTRUM_DOA_RESOLUTION_DEG,
                         HOLO_SPECTRUM_MIC_SPACING_M,
                         AUDIO_SPATIAL_FFT_SIZE);
    if (NULL == doa) {
        ESP_LOGE(TAG, "DOA init failed");
        delete_current_spectrum_task();
        return;
    }
    tdm_samples = heap_caps_malloc(
        AUDIO_SPATIAL_FFT_SIZE * HOLO_SPECTRUM_TDM_CHANNELS * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    mic1_samples = heap_caps_malloc(AUDIO_SPATIAL_FFT_SIZE * sizeof(int16_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    mic2_samples = heap_caps_malloc(AUDIO_SPATIAL_FFT_SIZE * sizeof(int16_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == tdm_samples || NULL == mic1_samples || NULL == mic2_samples) {
        ESP_LOGE(TAG, "audio buffer allocation failed");
        heap_caps_free(tdm_samples);
        heap_caps_free(mic1_samples);
        heap_caps_free(mic2_samples);
        esp_doa_destroy(doa);
        delete_current_spectrum_task();
        return;
    }
    ESP_LOGI(TAG, "ready 16kHz fft=512 bands=24 mic1=slot0 mic2=slot2");
    for (;;) {
        result = board_laiwfs300_audio_read_tdm_4ch(tdm_samples,
                                                    AUDIO_SPATIAL_FFT_SIZE);
        if (ESP_OK != result) {
            ESP_LOGW(TAG, "TDM read failed: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }
        for (size_t index = 0U; index < AUDIO_SPATIAL_FFT_SIZE; ++index) {
            mic1_samples[index] = tdm_samples[
                index * HOLO_SPECTRUM_TDM_CHANNELS + HOLO_SPECTRUM_MIC1_SLOT];
            mic2_samples[index] = tdm_samples[
                index * HOLO_SPECTRUM_TDM_CHANNELS + HOLO_SPECTRUM_MIC2_SLOT];
        }
        result = audio_spatial_spectrum_processor_process(
            mic1_samples, mic2_samples, &spectrum);
        if (ESP_OK != result) {
            ESP_LOGW(TAG, "process failed: %s", esp_err_to_name(result));
            continue;
        }
        const float raw_angle_deg = esp_doa_process(doa, mic1_samples,
                                                    mic2_samples);
        const bool angle_valid = isfinite(raw_angle_deg) &&
                                 0.0f <= raw_angle_deg &&
                                 180.0f >= raw_angle_deg;
        const bool voice_active =
            spectrum.energy_db >= HOLO_SPECTRUM_DOA_ENERGY_THRESHOLD_DB &&
            angle_valid;
        bool angle_used = false;
        const TickType_t current_tick = xTaskGetTickCount();
        if (voice_active) {
            angle_used = spatial_doa_filter_update(
                &doa_filter, raw_angle_deg, &filtered_angle_deg);
        }
        if (angle_used) {
            relative_angle_deg = audio_spectrum_doa_relative(
                filtered_angle_deg, HOLO_SPECTRUM_DOA_DISPLAY_GAIN);
            have_direction = true;
            last_active_tick = current_tick;
        } else if (voice_active && have_direction) {
            last_active_tick = current_tick;
        } else if (have_direction &&
                   (current_tick - last_active_tick) >=
                       pdMS_TO_TICKS(HOLO_SPECTRUM_DOA_IDLE_HOLD_MS)) {
            spatial_doa_filter_reset(&doa_filter);
            have_direction = false;
        }
        const bool doa_active = have_direction &&
            (voice_active ||
             (current_tick - last_active_tick) <
                 pdMS_TO_TICKS(HOLO_SPECTRUM_DOA_IDLE_HOLD_MS));
        publish_snapshot(&spectrum, doa_active, relative_angle_deg);
        if ((current_tick - last_report_tick) >=
            pdMS_TO_TICKS(HOLO_SPECTRUM_REPORT_MS)) {
            last_report_tick = current_tick;
            ESP_LOGI(TAG, "energy=%.1f dbfs=%.1f mic1=%lu mic2=%lu doa=%s rel=%.1f",
                     spectrum.energy_db, spectrum.energy_dbfs,
                     (unsigned long)spectrum.mic1_rms,
                     (unsigned long)spectrum.mic2_rms,
                     doa_active ? "active" : "idle", relative_angle_deg);
        }
    }
}

esp_err_t holocubic_spectrum_start(void)
{
    esp_err_t result = ESP_OK;

    if (s_started) return ESP_OK;
    result = board_laiwfs300_audio_init();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "audio init failed: %s", esp_err_to_name(result));
        return result;
    }
    result = board_laiwfs300_audio_open_input_all_channels();
    if (ESP_OK != result) {
        ESP_LOGE(TAG, "audio input failed: %s", esp_err_to_name(result));
        return result;
    }
    if (NULL == s_snapshot_mutex) {
        s_snapshot_mutex = xSemaphoreCreateMutex();
        if (NULL == s_snapshot_mutex) return ESP_ERR_NO_MEM;
    }
    s_snapshot = (holocubic_spectrum_snapshot_t){.revision = 1U};
    holocubic_spectrum_raster_reset(&s_raster_visual);
    BaseType_t task_created = pdFAIL;
    const holocubic_task_stack_memory_t stack_memory =
        holocubic_startup_task_stack(HOLO_STARTUP_STEP_SPECTRUM);
    if (HOLO_TASK_STACK_EXTERNAL == stack_memory) {
        task_created = xTaskCreateWithCaps(
            spectrum_task, "holo_spectrum", HOLO_SPECTRUM_TASK_STACK, NULL,
            HOLO_SPECTRUM_TASK_PRIORITY, NULL,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        task_created = xTaskCreate(spectrum_task, "holo_spectrum",
                                   HOLO_SPECTRUM_TASK_STACK, NULL,
                                   HOLO_SPECTRUM_TASK_PRIORITY, NULL);
    }
    if (pdPASS != task_created) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "task stack=%s bytes=%u",
             HOLO_TASK_STACK_EXTERNAL == stack_memory ? "psram" : "internal",
             (unsigned)HOLO_SPECTRUM_TASK_STACK);
    s_started = true;
    return ESP_OK;
}

bool holocubic_spectrum_snapshot(holocubic_spectrum_snapshot_t *snapshot)
{
    if (NULL == snapshot || NULL == s_snapshot_mutex ||
        pdTRUE != xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(10U))) {
        return false;
    }
    *snapshot = s_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
    return snapshot->available;
}
