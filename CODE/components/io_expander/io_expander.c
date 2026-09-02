#include "io_expander.h"

#include "bus_i2c.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "io_expander";

#define TPT29555A_REG_INPUT0 0x00
#define TPT29555A_REG_OUTPUT0 0x02
#define TPT29555A_REG_POLARITY0 0x04
#define TPT29555A_REG_CONFIG0 0x06

static i2c_master_dev_handle_t s_dev;
static uint8_t s_output[2] = {0xFF, 0xFF};
static uint8_t s_config[2] = {0xFF, 0xFF};
static bool s_initialized;

static bool port_pin_valid(uint8_t port, uint8_t pin)
{
    return port < 2 && pin < 8;
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, 100);
}

esp_err_t io_expander_init(const io_expander_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(NULL != config, ESP_ERR_INVALID_ARG, TAG, "missing config");
    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr_7bit,
        .scl_speed_hz = config->scl_speed_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_config, &s_dev), TAG, "add TPT29555A");

    ESP_RETURN_ON_ERROR(write_reg(TPT29555A_REG_POLARITY0, 0x00), TAG, "polarity p0");
    ESP_RETURN_ON_ERROR(write_reg(TPT29555A_REG_POLARITY0 + 1, 0x00), TAG, "polarity p1");
    ESP_RETURN_ON_ERROR(write_reg(TPT29555A_REG_OUTPUT0, s_output[0]), TAG, "output p0");
    ESP_RETURN_ON_ERROR(write_reg(TPT29555A_REG_OUTPUT0 + 1, s_output[1]), TAG, "output p1");
    ESP_RETURN_ON_ERROR(write_reg(TPT29555A_REG_CONFIG0, s_config[0]), TAG, "config p0");
    ESP_RETURN_ON_ERROR(write_reg(TPT29555A_REG_CONFIG0 + 1, s_config[1]), TAG, "config p1");

    s_initialized = true;
    ESP_LOGI(TAG, "TPT29555A initialized at 0x%02x", config->i2c_addr_7bit);
    return ESP_OK;
}

esp_err_t io_expander_set_pin_direction(uint8_t port, uint8_t pin, bool output)
{
    ESP_RETURN_ON_FALSE(s_initialized && port_pin_valid(port, pin), ESP_ERR_INVALID_ARG, TAG, "invalid direction request");

    if (output) {
        s_config[port] &= (uint8_t)~(1U << pin);
    } else {
        s_config[port] |= (uint8_t)(1U << pin);
    }
    return write_reg((uint8_t)(TPT29555A_REG_CONFIG0 + port), s_config[port]);
}

esp_err_t io_expander_write_pin(uint8_t port, uint8_t pin, bool level)
{
    ESP_RETURN_ON_FALSE(s_initialized && port_pin_valid(port, pin), ESP_ERR_INVALID_ARG, TAG, "invalid write request");

    if (level) {
        s_output[port] |= (uint8_t)(1U << pin);
    } else {
        s_output[port] &= (uint8_t)~(1U << pin);
    }
    return write_reg((uint8_t)(TPT29555A_REG_OUTPUT0 + port), s_output[port]);
}

esp_err_t io_expander_read_pin(uint8_t port, uint8_t pin, bool *level)
{
    ESP_RETURN_ON_FALSE(s_initialized && port_pin_valid(port, pin) && NULL != level,
                        ESP_ERR_INVALID_ARG, TAG, "invalid read request");

    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(read_reg((uint8_t)(TPT29555A_REG_INPUT0 + port), &value), TAG, "read input");
    *level = (value & (1U << pin)) != 0;
    return ESP_OK;
}
