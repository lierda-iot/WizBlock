#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef int portMUX_TYPE;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
#define portMAX_DELAY UINT32_MAX
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
