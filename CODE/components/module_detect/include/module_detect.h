#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MODULE_ID_NONE = 0,
    MODULE_ID_MOTOR_D0,
    MODULE_ID_DISPLAY_TFT,
    MODULE_ID_CAMERA_DVP,
    MODULE_ID_MIC_ANALOG,
} module_id_t;

typedef struct {
    module_id_t id;
    uint32_t nominal_mv;
    uint32_t tolerance_mv;
    const char *source;
} module_detect_adc_rule_t;

typedef struct {
    module_id_t id;
    bool present;
    uint32_t confidence;
    const char *source;
} module_detect_result_t;

esp_err_t module_detect_init(void);
esp_err_t module_detect_register_adc_rule(const module_detect_adc_rule_t *rule);
esp_err_t module_detect_scan_once(module_detect_result_t *results, size_t capacity, size_t *out_count);
const char *module_detect_id_name(module_id_t id);
