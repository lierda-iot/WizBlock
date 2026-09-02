#pragma once

#include "esp_err.h"

esp_err_t holocubic_display_domain_init(void);
esp_err_t holocubic_display_domain_lock(int timeout_ms);
void holocubic_display_domain_unlock(void);
esp_err_t holocubic_storage_domain_lock(int timeout_ms);
void holocubic_storage_domain_unlock(void);
