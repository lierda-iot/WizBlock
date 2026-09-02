#include "board_laiwfs300.h"

#include "board_pins.h"
#include "touch_hal.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_touch";

esp_err_t board_laiwfs300_touch_init(void)
{
    if (!board_laiwfs300_ioex_available()) {
        ESP_LOGW(TAG, "IOEX not available, cannot reset touch IC");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(touch_panel_init(), TAG, "touch_panel_init");
    return ESP_OK;
}

esp_err_t board_laiwfs300_touch_verify(void)
{
    touch_panel_info_t info = {0};
    esp_err_t ret = touch_panel_read_info(&info);
    if (ESP_OK == ret) {
        ESP_LOGI(TAG, "CST836U verify OK: module_id=0x%02X fw=0x%02X chip_type=0x%02X%02X",
                 info.chip_id, info.firmware_ver, info.lib_ver_h, info.lib_ver_l);
    } else {
        ESP_LOGW(TAG, "CST836U verify failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
