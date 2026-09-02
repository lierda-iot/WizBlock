#include "app_startup.h"
#include "bringup_test.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "AI companion robot framework starting");

    esp_err_t ret = app_startup_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "app startup failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "app startup completed");
    }

    ret = bringup_test_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bring-up test failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "bring-up test started");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
