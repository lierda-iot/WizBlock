#ifndef LAUNCHER_BOOT_H
#define LAUNCHER_BOOT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "storage_hal.h"

typedef void (*launcher_boot_complete_cb_t)(void *user_ctx);

typedef struct {
    storage_hal_config_t storage_config;
    const char *animation_path;
    const char *audio_path;
    SemaphoreHandle_t lvgl_mutex;
    SemaphoreHandle_t spi_mutex;
} launcher_boot_config_t;

esp_err_t launcher_boot_start(const launcher_boot_config_t *config,
                              launcher_boot_complete_cb_t complete_cb, void *user_ctx);

#endif
