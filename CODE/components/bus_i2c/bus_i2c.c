#include "bus_i2c.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "bus_i2c";

static i2c_master_bus_handle_t s_i2c_bus;
static bool s_initialized;

esp_err_t bus_i2c_init(const bus_i2c_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(NULL != config, ESP_ERR_INVALID_ARG, TAG, "missing config");
    ESP_RETURN_ON_FALSE(config->sda_gpio_num >= 0 && config->scl_gpio_num >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid pins");

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_gpio_num,
        .scl_io_num = config->scl_gpio_num,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = config->enable_internal_pullups,
        },
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "create master bus");
    s_initialized = true;
    ESP_LOGI(TAG, "I2C%d initialized: SDA=GPIO%d SCL=GPIO%d speed=%luHz",
             config->port,
             config->sda_gpio_num,
             config->scl_gpio_num,
             (unsigned long)config->clk_hz);
    return ESP_OK;
}

i2c_master_bus_handle_t bus_i2c_master_bus(void)
{
    return s_i2c_bus;
}
