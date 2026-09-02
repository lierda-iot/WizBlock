#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "holocubic_weather.h"

typedef struct {
    holocubic_weather_t *weather;
    SemaphoreHandle_t weather_mutex;
    bool ready;
    bool manager_start_failed;
    bool sntp_started;
    uint32_t subscription_id;
} holocubic_network_t;

bool holocubic_network_init(holocubic_network_t *network,
                            holocubic_weather_t *weather,
                            SemaphoreHandle_t weather_mutex);
void holocubic_network_task(void *argument);
