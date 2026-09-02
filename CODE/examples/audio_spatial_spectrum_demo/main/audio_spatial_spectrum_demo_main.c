#include "audio_spatial_spectrum_doa_filter.h"
#include "audio_spatial_spectrum_math.h"
#include "audio_spatial_spectrum_processor.h"
#include "audio_spatial_spectrum_ui.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "launcher_return.h"

#include "esp_doa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "audio_spatial_spectrum";

#define SAMPLE_RATE_HZ              BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define TDM_CHANNEL_COUNT           4U
#define MIC1_TDM_SLOT               0U
#define MIC2_TDM_SLOT               2U
#define MIC_SPACING_METERS          0.041f
#define DOA_RESOLUTION_DEG          10.0f
#define DOA_ENERGY_THRESHOLD_DB     56.0f
#define DOA_DISPLAY_GAIN            2.0f
#define DOA_IDLE_HOLD_MS            400U
#define REPORT_INTERVAL_MS          500U
#define PROCESS_TASK_STACK          12288U
#define PROCESS_TASK_PRIORITY       3U

static const char *direction_text(spatial_doa_direction_t direction)
{
    switch (direction) {
    case SPATIAL_DOA_DIRECTION_LEFT:
        return "LEFT";
    case SPATIAL_DOA_DIRECTION_CENTER:
        return "CENTER";
    case SPATIAL_DOA_DIRECTION_RIGHT:
        return "RIGHT";
    case SPATIAL_DOA_DIRECTION_IDLE:
    default:
        return "IDLE";
    }
}

static void copy_spectrum_to_ui(const audio_spatial_spectrum_result_t *spectrum,
                                audio_spatial_spectrum_ui_state_t *ui_state)
{
    for (size_t index = 0; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
        ui_state->combined_levels[index] = spectrum->combined_levels[index];
        ui_state->combined_peaks[index] = spectrum->combined_peaks[index];
        ui_state->mic1_levels[index] = spectrum->mic1_levels[index];
        ui_state->mic1_peaks[index] = spectrum->mic1_peaks[index];
        ui_state->mic2_levels[index] = spectrum->mic2_levels[index];
        ui_state->mic2_peaks[index] = spectrum->mic2_peaks[index];
    }
    ui_state->energy_db = spectrum->energy_db;
    ui_state->energy_dbfs = spectrum->energy_dbfs;
    ui_state->mic1_level = spectrum->mic1_level;
    ui_state->mic2_level = spectrum->mic2_level;
    ui_state->mic1_rms = spectrum->mic1_rms;
    ui_state->mic2_rms = spectrum->mic2_rms;
}

static void spatial_spectrum_task(void *arg)
{
    const bool ui_available = *(const bool *)arg;

    esp_err_t ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "audio init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = board_laiwfs300_audio_open_input_all_channels();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "open all input channels failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = audio_spatial_spectrum_processor_init(SAMPLE_RATE_HZ);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "spectrum processor init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    doa_handle_t *doa = esp_doa_create(SAMPLE_RATE_HZ,
                                       DOA_RESOLUTION_DEG,
                                       MIC_SPACING_METERS,
                                       AUDIO_SPATIAL_FFT_SIZE);
    if (NULL == doa) {
        ESP_LOGE(TAG, "esp_doa_create failed");
        vTaskDelete(NULL);
        return;
    }

    int16_t *tdm_samples = heap_caps_malloc(
        AUDIO_SPATIAL_FFT_SIZE * TDM_CHANNEL_COUNT * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *mic1_samples = heap_caps_malloc(
        AUDIO_SPATIAL_FFT_SIZE * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *mic2_samples = heap_caps_malloc(
        AUDIO_SPATIAL_FFT_SIZE * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == tdm_samples || NULL == mic1_samples || NULL == mic2_samples) {
        ESP_LOGE(TAG, "audio buffer allocation failed");
        heap_caps_free(tdm_samples);
        heap_caps_free(mic1_samples);
        heap_caps_free(mic2_samples);
        esp_doa_destroy(doa);
        vTaskDelete(NULL);
        return;
    }

    spatial_doa_filter_t doa_filter = {0};
    spatial_doa_direction_t direction = SPATIAL_DOA_DIRECTION_IDLE;
    audio_spatial_spectrum_result_t spectrum = {0};
    float filtered_angle_deg = 90.0f;
    float relative_angle_deg = 0.0f;
    TickType_t last_active_tick = 0U;
    TickType_t last_report_tick = 0U;
    bool have_direction = false;

    ESP_LOGI(TAG,
             "started: 16kHz, 512-point FFT, 24 bands, MIC1=slot0, MIC2=slot2, spacing=41mm");
    while (true) {
        ret = board_laiwfs300_audio_read_tdm_4ch(tdm_samples,
                                                 AUDIO_SPATIAL_FFT_SIZE);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "TDM read failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        for (size_t index = 0; index < AUDIO_SPATIAL_FFT_SIZE; index++) {
            mic1_samples[index] =
                tdm_samples[index * TDM_CHANNEL_COUNT + MIC1_TDM_SLOT];
            mic2_samples[index] =
                tdm_samples[index * TDM_CHANNEL_COUNT + MIC2_TDM_SLOT];
        }

        ret = audio_spatial_spectrum_processor_process(mic1_samples,
                                                       mic2_samples,
                                                       &spectrum);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "spectrum process failed: %s", esp_err_to_name(ret));
            continue;
        }

        const float raw_angle_deg = esp_doa_process(doa,
                                                    mic1_samples,
                                                    mic2_samples);
        const bool angle_valid = isfinite(raw_angle_deg) &&
                                 0.0f <= raw_angle_deg &&
                                 180.0f >= raw_angle_deg;
        const bool voice_active = spectrum.energy_db >= DOA_ENERGY_THRESHOLD_DB &&
                                  angle_valid;
        bool angle_used = false;
        const TickType_t now = xTaskGetTickCount();

        if (voice_active) {
            angle_used = spatial_doa_filter_update(&doa_filter,
                                                   raw_angle_deg,
                                                   &filtered_angle_deg);
        }
        if (angle_used) {
            relative_angle_deg = audio_spectrum_doa_relative(filtered_angle_deg,
                                                              DOA_DISPLAY_GAIN);
            direction = spatial_doa_direction_update(direction,
                                                     filtered_angle_deg);
            have_direction = true;
            last_active_tick = now;
        } else if (voice_active && have_direction) {
            last_active_tick = now;
        } else if (have_direction &&
                   (now - last_active_tick) >= pdMS_TO_TICKS(DOA_IDLE_HOLD_MS)) {
            spatial_doa_filter_reset(&doa_filter);
            direction = SPATIAL_DOA_DIRECTION_IDLE;
            have_direction = false;
        }

        audio_spatial_spectrum_ui_state_t ui_state = {0};
        copy_spectrum_to_ui(&spectrum, &ui_state);
        if (have_direction &&
            (voice_active ||
             (now - last_active_tick) < pdMS_TO_TICKS(DOA_IDLE_HOLD_MS))) {
            ui_state.doa_active = true;
            ui_state.relative_angle_deg = relative_angle_deg;
            ui_state.direction = direction;
        }

        if (ui_available) {
            ret = audio_spatial_spectrum_ui_update(&ui_state);
            if (ESP_OK != ret) {
                ESP_LOGW(TAG, "UI update failed: %s", esp_err_to_name(ret));
            }
        }

        if ((now - last_report_tick) >= pdMS_TO_TICKS(REPORT_INTERVAL_MS)) {
            last_report_tick = now;
            ESP_LOGI(TAG,
                     "E=%.1f MIC1=%lu MIC2=%lu DOA=%s RAW=%.1f FILT=%.1f REL=%.1f",
                     spectrum.energy_db,
                     (unsigned long)spectrum.mic1_rms,
                     (unsigned long)spectrum.mic2_rms,
                     have_direction ? direction_text(direction) : "IDLE",
                     raw_angle_deg,
                     filtered_angle_deg,
                     relative_angle_deg);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Audio Spatial Spectrum Demo");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = launcher_return_start_default();
    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) {
        ESP_LOGW(TAG, "launcher return unavailable: %s", esp_err_to_name(ret));
    }

    static bool ui_available;
    ret = audio_spatial_spectrum_ui_init();
    ui_available = ESP_OK == ret;
    if (!ui_available) {
        ESP_LOGW(TAG, "UI init failed, continuing with serial output: %s",
                 esp_err_to_name(ret));
    }

    const BaseType_t created = xTaskCreate(spatial_spectrum_task,
                                           "spatial_spectrum",
                                           PROCESS_TASK_STACK,
                                           &ui_available,
                                           PROCESS_TASK_PRIORITY,
                                           NULL);
    if (pdPASS != created) {
        ESP_LOGE(TAG, "processing task creation failed");
    }
}
