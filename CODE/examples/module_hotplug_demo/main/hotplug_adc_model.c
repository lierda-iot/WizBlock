#include "hotplug_adc_model.h"

bool hotplug_adc_is_saturated(int raw)
{
    return HOTPLUG_ADC_SATURATION_RAW_MIN <= raw;
}

hotplug_adc_classification_t hotplug_adc_classify(int raw,
                                                  int calibrated_mv,
                                                  bool calibration_valid)
{
    if (0 > raw || HOTPLUG_ADC_RAW_MAX < raw) {
        return HOTPLUG_ADC_CLASS_UNKNOWN;
    }
    if (hotplug_adc_is_saturated(raw)) {
        return HOTPLUG_ADC_CLASS_C0_ONLY;
    }

    if (calibration_valid) {
        if (HOTPLUG_ADC_BARE_MAX_MV >= calibrated_mv) {
            return HOTPLUG_ADC_CLASS_BARE;
        }
        if (HOTPLUG_ADC_C0_D0_MIN_MV <= calibrated_mv &&
            HOTPLUG_ADC_C0_D0_MAX_MV >= calibrated_mv) {
            return HOTPLUG_ADC_CLASS_C0_D0;
        }
        return HOTPLUG_ADC_CLASS_UNKNOWN;
    }

    if (HOTPLUG_ADC_FALLBACK_BARE_RAW_MAX >= raw) {
        return HOTPLUG_ADC_CLASS_BARE;
    }
    if (HOTPLUG_ADC_FALLBACK_C0_D0_RAW_MIN <= raw &&
        HOTPLUG_ADC_FALLBACK_C0_D0_RAW_MAX >= raw) {
        return HOTPLUG_ADC_CLASS_C0_D0;
    }
    return HOTPLUG_ADC_CLASS_UNKNOWN;
}

const char *hotplug_adc_classification_name(hotplug_adc_classification_t classification)
{
    switch (classification) {
    case HOTPLUG_ADC_CLASS_BARE:
        return "BARE";
    case HOTPLUG_ADC_CLASS_C0_ONLY:
        return "C0_ONLY";
    case HOTPLUG_ADC_CLASS_C0_D0:
        return "C0_D0";
    case HOTPLUG_ADC_CLASS_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}
