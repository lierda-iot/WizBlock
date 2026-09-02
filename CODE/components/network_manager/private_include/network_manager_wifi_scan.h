#pragma once

#include "network_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void network_manager_wifi_scan_list_init(
    network_manager_wifi_scan_list_t *list);
bool network_manager_wifi_scan_list_add(
    network_manager_wifi_scan_list_t *list,
    const uint8_t *ssid,
    size_t ssid_len,
    int8_t rssi,
    bool secure);
void network_manager_wifi_scan_list_finalize(
    network_manager_wifi_scan_list_t *list);
