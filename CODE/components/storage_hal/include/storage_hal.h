#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    const char *mount_point;
    spi_host_device_t spi_host;
    int cs_gpio_num;
    int max_freq_khz;
    int max_files;
    size_t allocation_unit_size;
    bool format_if_mount_failed;
} storage_hal_config_t;

esp_err_t storage_hal_init(const storage_hal_config_t *config);
esp_err_t storage_hal_deinit(void);
esp_err_t storage_hal_probe(void);
bool storage_hal_is_mounted(void);
uint64_t storage_hal_capacity_bytes(void);
