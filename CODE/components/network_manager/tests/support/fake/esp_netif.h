#pragma once

#include "esp_err.h"

typedef struct fake_esp_netif {
    int unused;
} esp_netif_t;

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
