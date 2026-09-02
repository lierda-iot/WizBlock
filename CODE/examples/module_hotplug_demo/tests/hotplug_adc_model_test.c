#include "hotplug_adc_model.h"

#include <assert.h>
#include <stdio.h>

static void test_saturated_high_level_is_c0_only(void)
{
    assert(hotplug_adc_is_saturated(4095));
    assert(HOTPLUG_ADC_CLASS_C0_ONLY == hotplug_adc_classify(4095, 2500, true));
}

static void test_calibrated_1450mv_is_c0_with_d0(void)
{
    assert(HOTPLUG_ADC_CLASS_C0_D0 == hotplug_adc_classify(1687, 1450, true));
    assert(HOTPLUG_ADC_CLASS_C0_D0 ==
           hotplug_adc_classify(1687, HOTPLUG_ADC_C0_D0_MIN_MV, true));
    assert(HOTPLUG_ADC_CLASS_C0_D0 ==
           hotplug_adc_classify(1687, HOTPLUG_ADC_C0_D0_MAX_MV, true));
}

static void test_low_level_is_bare(void)
{
    assert(HOTPLUG_ADC_CLASS_BARE == hotplug_adc_classify(350, 220, true));
    assert(HOTPLUG_ADC_CLASS_BARE ==
           hotplug_adc_classify(1200, HOTPLUG_ADC_BARE_MAX_MV, true));
}

static void test_uncertain_level_does_not_report_a_board(void)
{
    assert(HOTPLUG_ADC_CLASS_UNKNOWN == hotplug_adc_classify(2500, 2000, true));
    assert(HOTPLUG_ADC_CLASS_UNKNOWN == hotplug_adc_classify(-1, -1, false));
    assert(HOTPLUG_ADC_CLASS_UNKNOWN == hotplug_adc_classify(4096, 3300, true));
}

static void test_raw_fallback_only_accepts_known_windows(void)
{
    assert(HOTPLUG_ADC_CLASS_BARE == hotplug_adc_classify(500, -1, false));
    assert(HOTPLUG_ADC_CLASS_C0_D0 == hotplug_adc_classify(1687, -1, false));
    assert(HOTPLUG_ADC_CLASS_UNKNOWN == hotplug_adc_classify(3000, -1, false));
}

int main(void)
{
    test_saturated_high_level_is_c0_only();
    test_calibrated_1450mv_is_c0_with_d0();
    test_low_level_is_bare();
    test_uncertain_level_does_not_report_a_board();
    test_raw_fallback_only_accepts_known_windows();
    puts("hotplug_adc_model_test: PASS");
    return 0;
}
