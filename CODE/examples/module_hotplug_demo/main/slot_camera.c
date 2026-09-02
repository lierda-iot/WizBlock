#include "hotplug_manager.h"
#include "board_hotplug_pins.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "io_expander.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "slot_cam";

static i2c_master_bus_handle_t s_i2c_bus;
static bool s_sensor_powered;

static hotplug_state_t camera_detect(void)
{
    if (HOTPLUG_STATE_ABSENT == hotplug_manager_get_state(HOTPLUG_SLOT_NAME_C0)) {
        if (s_sensor_powered) {
            io_expander_write_pin(BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PORT,
                                  BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PIN, true);
            s_sensor_powered = false;
        }
        return HOTPLUG_STATE_ABSENT;
    }
    if (!s_sensor_powered) {
        ESP_LOGI(TAG, "C0 present, releasing camera PWDN...");
        io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PORT,
                                      BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PIN, true);
        io_expander_write_pin(BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PORT,
                              BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PIN, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        io_expander_write_pin(BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PORT,
                              BOARD_LAIWFS300_IOEX_CAMERA_PWDN_PIN, false);
        vTaskDelay(pdMS_TO_TICKS(200));
        io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_CAMERA_RESET_PORT,
                                      BOARD_LAIWFS300_IOEX_CAMERA_RESET_PIN, true);
        io_expander_write_pin(BOARD_LAIWFS300_IOEX_CAMERA_RESET_PORT,
                              BOARD_LAIWFS300_IOEX_CAMERA_RESET_PIN, false);
        vTaskDelay(pdMS_TO_TICKS(20));
        io_expander_write_pin(BOARD_LAIWFS300_IOEX_CAMERA_RESET_PORT,
                              BOARD_LAIWFS300_IOEX_CAMERA_RESET_PIN, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        s_sensor_powered = true;
        ESP_LOGI(TAG, "sensor power-up done");
    }
    esp_err_t ret = i2c_master_probe(s_i2c_bus, HOTPLUG_SP0A39_ADDR, 50);
    return (ESP_OK == ret) ? HOTPLUG_STATE_PRESENT : HOTPLUG_STATE_ABSENT;
}

esp_err_t slot_camera_init(i2c_master_bus_handle_t i2c_bus)
{
    s_i2c_bus = i2c_bus;
    static const hotplug_slot_t slot = {
        .name = HOTPLUG_SLOT_NAME_CAMERA,
        .detect_fn = camera_detect,
    };
    return hotplug_manager_register_slot(&slot);
}
