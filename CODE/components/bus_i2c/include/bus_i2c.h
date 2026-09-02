#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    i2c_port_num_t port;
    int sda_gpio_num;
    int scl_gpio_num;
    uint32_t clk_hz;
    bool enable_internal_pullups;
} bus_i2c_config_t;

esp_err_t bus_i2c_init(const bus_i2c_config_t *config);
i2c_master_bus_handle_t bus_i2c_master_bus(void);
