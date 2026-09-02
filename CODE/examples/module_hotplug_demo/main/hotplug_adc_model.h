#pragma once

#include <stdbool.h>

#define HOTPLUG_ADC_RAW_MAX                 4095
#define HOTPLUG_ADC_SATURATION_RAW_MIN      4000
#define HOTPLUG_ADC_BARE_MAX_MV             800
#define HOTPLUG_ADC_C0_D0_MIN_MV            1200
#define HOTPLUG_ADC_C0_D0_MAX_MV            1700
#define HOTPLUG_ADC_FALLBACK_BARE_RAW_MAX   1200
#define HOTPLUG_ADC_FALLBACK_C0_D0_RAW_MIN  1400
#define HOTPLUG_ADC_FALLBACK_C0_D0_RAW_MAX  2200

typedef enum {
    HOTPLUG_ADC_CLASS_UNKNOWN = 0,
    HOTPLUG_ADC_CLASS_BARE,
    HOTPLUG_ADC_CLASS_C0_ONLY,
    HOTPLUG_ADC_CLASS_C0_D0,
} hotplug_adc_classification_t;

hotplug_adc_classification_t hotplug_adc_classify(int raw,
                                                  int calibrated_mv,
                                                  bool calibration_valid);
bool hotplug_adc_is_saturated(int raw);
const char *hotplug_adc_classification_name(hotplug_adc_classification_t classification);
