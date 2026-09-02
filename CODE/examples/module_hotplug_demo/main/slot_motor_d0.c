#include "hotplug_manager.h"
#include "board_hotplug_pins.h"
#include "hotplug_adc_model.h"

#include "esp_log.h"

static const char *TAG = "slot_d0";

extern hotplug_adc_classification_t slot_expansion_c0_latest_classification(void);

static hotplug_state_t d0_detect(void)
{
    hotplug_adc_classification_t classification =
        slot_expansion_c0_latest_classification();
    static int s_log_counter = 0;
    s_log_counter++;
    if (s_log_counter % 10 == 1) {
        ESP_LOGI(TAG, "D0 detect: adc_class=%s c0_state=%d",
                 hotplug_adc_classification_name(classification),
                 hotplug_manager_get_state(HOTPLUG_SLOT_NAME_C0));
    }

    if (HOTPLUG_ADC_CLASS_C0_D0 == classification) {
        return HOTPLUG_STATE_PRESENT;
    }
    if (HOTPLUG_ADC_CLASS_UNKNOWN == classification) {
        return HOTPLUG_STATE_UNKNOWN;
    }
    return HOTPLUG_STATE_ABSENT;
}

esp_err_t slot_motor_d0_init(void)
{
    static const hotplug_slot_t slot = {
        .name = HOTPLUG_SLOT_NAME_D0,
        .detect_fn = d0_detect,
    };
    return hotplug_manager_register_slot(&slot);
}
