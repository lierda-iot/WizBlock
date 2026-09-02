#include "board_adc.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_adc";

static adc_oneshot_unit_handle_t s_adc1;
static bool s_initialized;

esp_err_t board_adc_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc1), TAG, "adc1 unit");

    s_initialized = true;
    ESP_LOGI(TAG, "ADC1 oneshot initialized");
    return ESP_OK;
}

adc_oneshot_unit_handle_t board_adc_handle(void)
{
    return s_adc1;
}
