#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t i2c_addr_7bit;
    uint32_t scl_speed_hz;
} io_expander_config_t;

esp_err_t io_expander_init(const io_expander_config_t *config);
esp_err_t io_expander_set_pin_direction(uint8_t port, uint8_t pin, bool output);
esp_err_t io_expander_write_pin(uint8_t port, uint8_t pin, bool level);
esp_err_t io_expander_read_pin(uint8_t port, uint8_t pin, bool *level);
