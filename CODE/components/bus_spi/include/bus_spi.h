#pragma once

#include <stdbool.h>

#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;
    int sclk_gpio_num;
    int mosi_gpio_num;
    int miso_gpio_num;
    int max_transfer_sz;
    spi_dma_chan_t dma_chan;
} bus_spi_config_t;

esp_err_t bus_spi_init(const bus_spi_config_t *config);
bool bus_spi_is_initialized(spi_host_device_t host);
