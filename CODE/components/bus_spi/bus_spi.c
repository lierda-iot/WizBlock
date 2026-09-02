#include "bus_spi.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"

#include <stdbool.h>

static bool s_initialized;
static spi_host_device_t s_host;

esp_err_t bus_spi_init(const bus_spi_config_t *config)
{
    ESP_RETURN_ON_FALSE(NULL != config, ESP_ERR_INVALID_ARG, "bus_spi", "missing config");
    ESP_RETURN_ON_FALSE(config->sclk_gpio_num >= 0 && config->mosi_gpio_num >= 0 &&
                            config->miso_gpio_num >= 0 && config->max_transfer_sz > 0,
                        ESP_ERR_INVALID_ARG, "bus_spi", "invalid config");

    if (s_initialized) {
        return (s_host == config->host) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = config->sclk_gpio_num,
        .mosi_io_num = config->mosi_gpio_num,
        .miso_io_num = config->miso_gpio_num,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = config->max_transfer_sz,
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(config->host, &bus_config, config->dma_chan),
                        "bus_spi", "initialize SPI bus");
    s_host = config->host;
    s_initialized = true;
    return ESP_OK;
}

bool bus_spi_is_initialized(spi_host_device_t host)
{
    return s_initialized && s_host == host;
}
