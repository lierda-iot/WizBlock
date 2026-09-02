#pragma once

/** ESP-IDF NVS adapter for the pure two-slot storage transaction. */

#include "network_manager.h"

#include "nvs.h"

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_MANAGER_STORAGE_NVS_NAMESPACE  "net_mgr"
#define NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_A "wifi_a"
#define NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_B "wifi_b"
#define NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR "wifi_sel"

/** Load the selected valid config; absence is ESP_OK with present=false. */
esp_err_t network_manager_storage_nvs_load(
    nvs_handle_t handle,
    network_manager_wifi_config_t *config,
    bool *present,
    uint32_t *generation);
/** Transactionally save config without invalidating the prior selected slot. */
esp_err_t network_manager_storage_nvs_save(
    nvs_handle_t handle,
    const network_manager_wifi_config_t *config);
/** Erase only the three NVS keys owned by network_manager. */
esp_err_t network_manager_storage_nvs_clear(nvs_handle_t handle);
