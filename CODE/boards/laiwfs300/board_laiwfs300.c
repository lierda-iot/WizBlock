#include "board_laiwfs300.h"

#include "board_adc.h"
#include "board_module_map.h"
#include "board_power.h"
#include "bus_i2c.h"
#include "bus_spi.h"
#include "capability_registry.h"
#include "io_expander.h"
#include "module_detect.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "board_laiwfs300";

static esp_err_t register_default_detection_rules(void)
{
    const module_detect_adc_rule_t d0_rule = {
        .id = MODULE_ID_MOTOR_D0,
        .nominal_mv = BOARD_LAIWFS300_D0_ADC_NOMINAL_MV,
        .tolerance_mv = BOARD_LAIWFS300_D0_ADC_TOLERANCE_MV,
        .source = "C0/D0 ADC",
    };

    return module_detect_register_adc_rule(&d0_rule);
}

static esp_err_t register_default_capabilities(void)
{
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_AUDIO_IO, CAPABILITY_STATE_PENDING_DRIVER), TAG, "audio capability");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_TRACK_MOTION, CAPABILITY_STATE_PENDING_DRIVER), TAG, "motion capability");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_DISPLAY, CAPABILITY_STATE_PENDING_DRIVER), TAG, "display capability");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_TOUCH, CAPABILITY_STATE_PENDING_DRIVER), TAG, "touch capability");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_CAMERA, CAPABILITY_STATE_PENDING_DRIVER), TAG, "camera capability");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_LTE, CAPABILITY_STATE_PENDING_DRIVER), TAG, "lte capability");
    ESP_RETURN_ON_ERROR(capability_registry_set_state(CAPABILITY_STORAGE, CAPABILITY_STATE_PENDING_DRIVER), TAG, "storage capability");
    return ESP_OK;
}

esp_err_t board_laiwfs300_init(void)
{
    ESP_LOGI(TAG, "init board support package");

    ESP_RETURN_ON_ERROR(capability_registry_init(), TAG, "capability registry init failed");
    ESP_RETURN_ON_ERROR(bus_i2c_init(), TAG, "i2c init failed");
    ESP_RETURN_ON_ERROR(bus_spi_init(), TAG, "spi init failed");
    ESP_RETURN_ON_ERROR(board_adc_init(), TAG, "adc init failed");
    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "power init failed");
    ESP_RETURN_ON_ERROR(io_expander_init(), TAG, "io expander init failed");
    ESP_RETURN_ON_ERROR(module_detect_init(), TAG, "module detect init failed");
    ESP_RETURN_ON_ERROR(register_default_detection_rules(), TAG, "detection rule registration failed");
    ESP_RETURN_ON_ERROR(register_default_capabilities(), TAG, "capability registration failed");

    return ESP_OK;
}
