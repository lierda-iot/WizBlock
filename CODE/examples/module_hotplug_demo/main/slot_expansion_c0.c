#include "hotplug_manager.h"
#include "board_hotplug_pins.h"
#include "hotplug_adc_model.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

static const char *TAG = "slot_c0";
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_cali_handle;
static bool s_adc_ready;
static bool s_adc_calibration_ready;
static hotplug_adc_classification_t s_latest_classification = HOTPLUG_ADC_CLASS_UNKNOWN;

typedef struct {
    int raw;
    int raw_min;
    int raw_max;
    int calibrated_mv;
    bool calibration_valid;
    bool saturated;
    hotplug_adc_classification_t classification;
} hotplug_adc_measurement_t;

static int compare_int(const void *a, const void *b)
{
    return (*(const int *)a) - (*(const int *)b);
}

static esp_err_t adc_read_measurement(hotplug_adc_measurement_t *measurement)
{
    if (!s_adc_ready || NULL == measurement) {
        return ESP_ERR_INVALID_STATE;
    }

    int readings[HOTPLUG_ADC_SAMPLES] = {0};
    for (int i = 0; i < HOTPLUG_ADC_SAMPLES; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, HOTPLUG_ADC_CHANNEL, &raw);
        if (ESP_OK != ret) {
            return ret;
        }
        readings[i] = raw;
        if (HOTPLUG_ADC_SAMPLES - 1 > i) {
            vTaskDelay(pdMS_TO_TICKS(HOTPLUG_ADC_SAMPLE_INTERVAL_MS));
        }
    }

    qsort(readings, HOTPLUG_ADC_SAMPLES, sizeof(int), compare_int);
    measurement->raw_min = readings[0];
    measurement->raw_max = readings[HOTPLUG_ADC_SAMPLES - 1];
    measurement->raw = readings[HOTPLUG_ADC_SAMPLES / 2];
    measurement->calibrated_mv = -1;
    measurement->calibration_valid = false;
    measurement->saturated = hotplug_adc_is_saturated(measurement->raw);

    if (s_adc_calibration_ready) {
        esp_err_t ret = adc_cali_raw_to_voltage(s_adc_cali_handle,
                                                measurement->raw,
                                                &measurement->calibrated_mv);
        if (ESP_OK == ret) {
            measurement->calibration_valid = true;
        } else {
            ESP_LOGW(TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
        }
    }

    measurement->classification = hotplug_adc_classify(measurement->raw,
                                                        measurement->calibrated_mv,
                                                        measurement->calibration_valid);
    int spread = measurement->raw_max - measurement->raw_min;
    if (HOTPLUG_ADC_UNSTABLE_SPREAD_RAW < spread) {
        ESP_LOGW(TAG, "ADC unstable: raw_spread=%d (min=%d max=%d median=%d)",
                 spread, measurement->raw_min, measurement->raw_max,
                 measurement->raw);
    }
    return ESP_OK;
}

static hotplug_state_t c0_detect(void)
{
    hotplug_adc_measurement_t measurement = {0};
    esp_err_t ret = adc_read_measurement(&measurement);
    if (ESP_OK != ret) {
        s_latest_classification = HOTPLUG_ADC_CLASS_UNKNOWN;
        ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return HOTPLUG_STATE_UNKNOWN;
    }
    s_latest_classification = measurement.classification;

    static int s_log_counter = 0;
    s_log_counter++;
    if (s_log_counter % 10 == 1) {
        ESP_LOGI(TAG,
                 "ADC GPIO1 raw=%d range=%d..%d cal_mv=%d cal_ok=%d saturated=%d class=%s "
                 "thresholds={bare<=%dmV,d0=%d..%dmV,sat_raw>=%d}",
                 measurement.raw, measurement.raw_min, measurement.raw_max,
                 measurement.calibrated_mv, measurement.calibration_valid,
                 measurement.saturated,
                 hotplug_adc_classification_name(measurement.classification),
                 HOTPLUG_ADC_BARE_MAX_MV, HOTPLUG_ADC_C0_D0_MIN_MV,
                 HOTPLUG_ADC_C0_D0_MAX_MV, HOTPLUG_ADC_SATURATION_RAW_MIN);
    }

    if (HOTPLUG_ADC_CLASS_BARE == measurement.classification) {
        return HOTPLUG_STATE_ABSENT;
    }
    if (HOTPLUG_ADC_CLASS_C0_ONLY == measurement.classification ||
        HOTPLUG_ADC_CLASS_C0_D0 == measurement.classification) {
        return HOTPLUG_STATE_PRESENT;
    }
    return HOTPLUG_STATE_UNKNOWN;
}

esp_err_t slot_expansion_c0_init(adc_oneshot_unit_handle_t adc_handle)
{
    if (NULL == adc_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    s_adc_handle = adc_handle;
    s_adc_ready = true;

    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = HOTPLUG_ADC_UNIT,
        .chan = HOTPLUG_ADC_CHANNEL,
        .atten = HOTPLUG_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t calibration_ret = adc_cali_create_scheme_curve_fitting(
        &calibration_config, &s_adc_cali_handle);
    if (ESP_OK == calibration_ret) {
        s_adc_calibration_ready = true;
        ESP_LOGI(TAG, "ADC curve fitting calibration ready");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable (%s), using raw fallback windows",
                 esp_err_to_name(calibration_ret));
    }

    static const hotplug_slot_t slot = {
        .name = HOTPLUG_SLOT_NAME_C0,
        .detect_fn = c0_detect,
    };
    return hotplug_manager_register_slot(&slot);
}

hotplug_adc_classification_t slot_expansion_c0_latest_classification(void)
{
    return s_latest_classification;
}
