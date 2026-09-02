#include "module_detect.h"

#include <string.h>

#define MODULE_DETECT_MAX_ADC_RULES 8

static module_detect_adc_rule_t s_adc_rules[MODULE_DETECT_MAX_ADC_RULES];
static size_t s_adc_rule_count;
static bool s_initialized;

esp_err_t module_detect_init(void)
{
    memset(s_adc_rules, 0, sizeof(s_adc_rules));
    s_adc_rule_count = 0;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t module_detect_register_adc_rule(const module_detect_adc_rule_t *rule)
{
    if (!s_initialized || NULL == rule || MODULE_ID_NONE == rule->id) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adc_rule_count >= MODULE_DETECT_MAX_ADC_RULES) {
        return ESP_ERR_NO_MEM;
    }

    s_adc_rules[s_adc_rule_count] = *rule;
    ++s_adc_rule_count;
    return ESP_OK;
}

esp_err_t module_detect_scan_once(module_detect_result_t *results, size_t capacity, size_t *out_count)
{
    if (!s_initialized || NULL == out_count) {
        return ESP_ERR_INVALID_STATE;
    }
    if (capacity > 0 && NULL == results) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0;
    return ESP_OK;
}

const char *module_detect_id_name(module_id_t id)
{
    switch (id) {
    case MODULE_ID_NONE:
        return "none";
    case MODULE_ID_MOTOR_D0:
        return "motor_d0";
    case MODULE_ID_DISPLAY_TFT:
        return "display_tft";
    case MODULE_ID_CAMERA_DVP:
        return "camera_dvp";
    case MODULE_ID_MIC_ANALOG:
        return "mic_analog";
    default:
        return "unknown";
    }
}
