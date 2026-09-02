#include "board_laiwfs300.h"
#include "board_pins.h"
#include "audio_dual_mic_doa_filter.h"
#include "audio_dual_mic_doa_ui.h"
#include "launcher_return.h"

#include "esp_doa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "audio_dual_mic_doa";

#define SAMPLE_RATE_HZ           BOARD_LAIWFS300_I2S_SAMPLE_RATE
#define TDM_CHANNELS             4
#define MIC_SPACING_METERS       0.041f
#define DOA_RESOLUTION_DEG       10.0f
#define DOA_ENERGY_THRESHOLD_DB  56.0f
#define DOA_DISPLAY_RELATIVE_GAIN 2.0f
#define DOA_RELATIVE_ANGLE_MAX_DEG 90.0f
#define REPORT_INTERVAL_MS       500
#define UI_UPDATE_INTERVAL_MS    100
#define UI_IDLE_HOLD_MS          400

static const char *doa_direction_text(doa_ui_direction_t direction)
{
    switch (direction) {
    case DOA_UI_DIRECTION_LEFT:
        return "LEFT";
    case DOA_UI_DIRECTION_CENTER:
        return "CENTER";
    case DOA_UI_DIRECTION_RIGHT:
        return "RIGHT";
    case DOA_UI_DIRECTION_IDLE:
    default:
        return "IDLE";
    }
}

static float audio_frame_energy_db(const int16_t *left, const int16_t *right, size_t count)
{
    double energy_sum = 0.0;

    if (0 == count) {
        return -120.0f;
    }

    for (size_t i = 0; i < count; i++) {
        double l = left[i];
        double r = right[i];
        energy_sum += l * l;
        energy_sum += r * r;
    }

    if (energy_sum <= 0.0) {
        return -120.0f;
    }

    return (float)(10.0 * log10(energy_sum / (double)(count * 2) + 1e-12));
}

static float doa_to_display_relative_angle(float doa_raw)
{
    /* Current board mounting is mirrored relative to esp_doa's raw angle convention,
     * so invert the relative angle to match the physical left/right seen by the user.
     * The display gain is a temporary visual calibration under evaluation.
     */
    float relative = (DOA_RELATIVE_ANGLE_MAX_DEG - doa_raw) *
                     DOA_DISPLAY_RELATIVE_GAIN;
    if (-DOA_RELATIVE_ANGLE_MAX_DEG > relative) {
        relative = -DOA_RELATIVE_ANGLE_MAX_DEG;
    }
    if (DOA_RELATIVE_ANGLE_MAX_DEG < relative) {
        relative = DOA_RELATIVE_ANGLE_MAX_DEG;
    }
    return relative;
}

static doa_ui_direction_t doa_ui_direction_from_filter(doa_filter_direction_t direction)
{
    switch (direction) {
    case DOA_FILTER_DIRECTION_LEFT:
        return DOA_UI_DIRECTION_LEFT;
    case DOA_FILTER_DIRECTION_CENTER:
        return DOA_UI_DIRECTION_CENTER;
    case DOA_FILTER_DIRECTION_RIGHT:
        return DOA_UI_DIRECTION_RIGHT;
    case DOA_FILTER_DIRECTION_IDLE:
    default:
        return DOA_UI_DIRECTION_IDLE;
    }
}

static uint32_t audio_frame_rms(const int16_t *data, size_t count)
{
    int64_t rms_sum = 0;

    if (0 == count) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        rms_sum += (int64_t)data[i] * data[i];
    }

    return (uint32_t)sqrtf((float)rms_sum / (float)count);
}

static void dual_mic_doa_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "waiting 5s for serial connection...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "initializing audio...");
    esp_err_t ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "audio init FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = board_laiwfs300_audio_open_input_all_channels();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "open_input_all_channels FAILED: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    const size_t chunk_frames = 512;
    doa_handle_t *doa = esp_doa_create(SAMPLE_RATE_HZ, DOA_RESOLUTION_DEG, MIC_SPACING_METERS, (int)chunk_frames);
    if (NULL == doa) {
        ESP_LOGE(TAG, "esp_doa_create FAILED");
        vTaskDelete(NULL);
        return;
    }

    int16_t *tdm_buf = heap_caps_malloc(chunk_frames * TDM_CHANNELS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *mic1_buf = heap_caps_malloc(chunk_frames * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *mic2_buf = heap_caps_malloc(chunk_frames * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (NULL == tdm_buf || NULL == mic1_buf || NULL == mic2_buf) {
        ESP_LOGE(TAG, "buffer alloc FAILED");
        esp_doa_destroy(doa);
        heap_caps_free(tdm_buf);
        heap_caps_free(mic1_buf);
        heap_caps_free(mic2_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "MIC spacing fixed at %.1f cm based on current user confirmation", MIC_SPACING_METERS * 100.0f);
    ESP_LOGI(TAG, "Energy gate: threshold=%.1f dB, printing MIC1/MIC2 RMS separately", DOA_ENERGY_THRESHOLD_DB);
    ESP_LOGI(TAG, "DOA started: sample_rate=%d, chunk=%u, resolution=%.1f deg",
             SAMPLE_RATE_HZ, (unsigned)chunk_frames, DOA_RESOLUTION_DEG);

    TickType_t last_report = 0;
    TickType_t last_ui_update = 0;
    TickType_t last_active_tick = 0;
    doa_angle_filter_t angle_filter = {0};
    doa_filter_direction_t filtered_direction = DOA_FILTER_DIRECTION_IDLE;
    doa_ui_state_t last_valid_state = {
        .active = false,
        .direction = DOA_UI_DIRECTION_IDLE,
    };
    bool have_valid_direction = false;

    while (true) {
        ret = board_laiwfs300_audio_read_tdm_4ch(tdm_buf, chunk_frames);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "read_tdm_4ch failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        for (size_t i = 0; i < chunk_frames; i++) {
            mic1_buf[i] = tdm_buf[i * TDM_CHANNELS];
            mic2_buf[i] = tdm_buf[i * TDM_CHANNELS + 2];
        }

        float energy_db = audio_frame_energy_db(mic1_buf, mic2_buf, chunk_frames);
        float doa_raw = esp_doa_process(doa, mic1_buf, mic2_buf);
        float filtered_deg = last_valid_state.angle_deg;
        float relative_deg = last_valid_state.relative_deg;
        uint32_t rms1 = audio_frame_rms(mic1_buf, chunk_frames);
        uint32_t rms2 = audio_frame_rms(mic2_buf, chunk_frames);
        bool angle_valid = isfinite(doa_raw) && 0.0f <= doa_raw && 180.0f >= doa_raw;
        bool active = (energy_db >= DOA_ENERGY_THRESHOLD_DB) && angle_valid;
        bool angle_used = false;

        TickType_t now = xTaskGetTickCount();
        if (active) {
            angle_used = doa_angle_filter_update(&angle_filter, doa_raw, &filtered_deg);
        }
        if (angle_used) {
            relative_deg = doa_to_display_relative_angle(filtered_deg);
            filtered_direction = doa_direction_filter_update(filtered_direction, filtered_deg);
            last_active_tick = now;
            last_valid_state.active = true;
            last_valid_state.direction = doa_ui_direction_from_filter(filtered_direction);
            last_valid_state.angle_deg = filtered_deg;
            last_valid_state.relative_deg = relative_deg;
            last_valid_state.energy_db = energy_db;
            last_valid_state.mic1_rms = rms1;
            last_valid_state.mic2_rms = rms2;
            have_valid_direction = true;
        } else if (active && have_valid_direction) {
            last_active_tick = now;
            last_valid_state.energy_db = energy_db;
            last_valid_state.mic1_rms = rms1;
            last_valid_state.mic2_rms = rms2;
        } else if (have_valid_direction &&
                   (now - last_active_tick) >= pdMS_TO_TICKS(UI_IDLE_HOLD_MS)) {
            doa_angle_filter_reset(&angle_filter);
            filtered_direction = DOA_FILTER_DIRECTION_IDLE;
            have_valid_direction = false;
        }

        if ((now - last_report) >= pdMS_TO_TICKS(REPORT_INTERVAL_MS)) {
            last_report = now;

            if (active && have_valid_direction) {
                ESP_LOGI(TAG,
                         "DOA=%s RAW=%.1f FILT=%.1f REL=%.1f USED=%u E=%.1f MIC1_RMS=%lu MIC2_RMS=%lu",
                         doa_direction_text(last_valid_state.direction), doa_raw, filtered_deg,
                         relative_deg, angle_used ? 1U : 0U, energy_db,
                         (unsigned long)rms1, (unsigned long)rms2);
            } else {
                ESP_LOGI(TAG, "DOA_IDLE E=%.1f(<%.1f) MIC1_RMS=%lu MIC2_RMS=%lu",
                         energy_db, DOA_ENERGY_THRESHOLD_DB,
                         (unsigned long)rms1, (unsigned long)rms2);
            }
        }

        if ((now - last_ui_update) >= pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS)) {
            doa_ui_state_t ui_state = {
                .active = false,
                .direction = DOA_UI_DIRECTION_IDLE,
                .energy_db = energy_db,
                .mic1_rms = rms1,
                .mic2_rms = rms2,
            };

            last_ui_update = now;

            if (active && have_valid_direction) {
                ui_state = last_valid_state;
            } else if (have_valid_direction &&
                       (now - last_active_tick) < pdMS_TO_TICKS(UI_IDLE_HOLD_MS)) {
                ui_state = last_valid_state;
                ui_state.energy_db = energy_db;
                ui_state.mic1_rms = rms1;
                ui_state.mic2_rms = rms2;
            }

            ret = audio_dual_mic_doa_ui_update(&ui_state);
            if (ESP_OK != ret && ESP_ERR_INVALID_STATE != ret) {
                ESP_LOGW(TAG, "UI update failed: %s", esp_err_to_name(ret));
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Audio Dual MIC DOA Demo");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = launcher_return_start_default();
    if (ESP_OK != ret && ESP_ERR_NOT_SUPPORTED != ret) {
        ESP_LOGW(TAG, "launcher return unavailable: %s", esp_err_to_name(ret));
    }

    ret = audio_dual_mic_doa_ui_init();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "DOA UI init failed, keep log-only mode: %s", esp_err_to_name(ret));
    }

    xTaskCreate(dual_mic_doa_task, "doa_task", 8192, NULL, tskIDLE_PRIORITY + 3, NULL);
}
