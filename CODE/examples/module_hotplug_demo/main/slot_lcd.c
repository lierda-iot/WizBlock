#include "hotplug_manager.h"
#include "board_hotplug_pins.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "slot_lcd";
static i2c_master_bus_handle_t s_i2c_bus;
static bool s_lcd_ready;

static hotplug_state_t lcd_detect(void)
{
    esp_err_t ret = i2c_master_probe(s_i2c_bus, HOTPLUG_CST836U_ADDR, 50);
    return (ESP_OK == ret) ? HOTPLUG_STATE_PRESENT : HOTPLUG_STATE_ABSENT;
}

esp_err_t slot_lcd_init(i2c_master_bus_handle_t i2c_bus)
{
    s_i2c_bus = i2c_bus;
    static const hotplug_slot_t slot = {
        .name = HOTPLUG_SLOT_NAME_LCD,
        .detect_fn = lcd_detect,
    };
    return hotplug_manager_register_slot(&slot);
}

bool slot_lcd_is_available(void)
{
    return s_lcd_ready;
}

void slot_lcd_set_available(bool available)
{
    s_lcd_ready = available;
}
