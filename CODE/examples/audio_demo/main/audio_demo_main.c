#include "board_laiwfs300.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_demo";

void app_main(void)
{
    ESP_LOGI(TAG, "Audio demo starting (ES8311 DAC + ES7210 dual MIC)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_audio_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "audio init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "starting record-play test: beep -> record 10s -> beep -> play 10s");
    ret = board_laiwfs300_audio_record_play_start();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "record-play start failed: %s", esp_err_to_name(ret));
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
