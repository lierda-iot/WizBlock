#include "board_laiwfs300.h"
#include "bringup_test.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "integrated_demo";

void app_main(void)
{
    ESP_LOGI(TAG, "Integrated demo starting (all peripherals)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = bringup_test_start();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "bringup test failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "bringup test started");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
