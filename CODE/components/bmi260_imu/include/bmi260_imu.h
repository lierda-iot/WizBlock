#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} bmi260_raw_data_t;

esp_err_t bmi260_init(void);
esp_err_t bmi260_read_accel(bmi260_raw_data_t *data);
esp_err_t bmi260_read_gyro(bmi260_raw_data_t *data);
