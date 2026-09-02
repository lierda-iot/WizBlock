#pragma once

#include "esp_err.h"

#define ESP_LOGW(tag, format, ...) ((void)(tag))
#define ESP_LOGI(tag, format, ...) ((void)(tag))
#define ESP_LOGE(tag, format, ...) ((void)(tag))

static inline const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "fake";
}

