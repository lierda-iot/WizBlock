#pragma once

#include "esp_err.h"

esp_err_t hotplug_motor_runtime_init(void);
esp_err_t hotplug_motor_runtime_forward_100(void);
esp_err_t hotplug_motor_runtime_deinit(void);
