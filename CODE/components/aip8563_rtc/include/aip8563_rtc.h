#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint8_t year;
} aip8563_time_t;

esp_err_t aip8563_init(void);
esp_err_t aip8563_set_time(const aip8563_time_t *time);
esp_err_t aip8563_get_time(aip8563_time_t *time);
bool aip8563_power_lost(void);
