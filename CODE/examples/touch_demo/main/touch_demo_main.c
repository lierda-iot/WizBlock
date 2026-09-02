#include "board_laiwfs300.h"
#include "touch_hal.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_demo";

void app_main(void)
{
    ESP_LOGI(TAG, "Touch demo starting (CST836U)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_touch_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "touch init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_touch_verify();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "touch verify failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "touch initialized, polling every 50ms...");

    while (true) {
        touch_panel_point_t point = {0};
        uint8_t count = 0;
        ret = touch_panel_read_point(&point, &count);
        if (ESP_OK == ret && count > 0) {
            ESP_LOGI(TAG, "TOUCH: x=%u y=%u weight=%u event=%u",
                     point.x, point.y, point.weight, point.event);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
