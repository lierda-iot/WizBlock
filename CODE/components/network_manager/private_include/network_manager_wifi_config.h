#pragma once

/** Length-aware Wi-Fi credential validation and canonical copying. */

#include "network_manager.h"

#include <stdbool.h>

/** Return true when config satisfies the component's closed credential domain. */
bool network_manager_wifi_config_is_valid(
    const network_manager_wifi_config_t *config);
/** Validate source, zero unused bytes, and copy it into destination. */
bool network_manager_wifi_config_copy(
    network_manager_wifi_config_t *destination,
    const network_manager_wifi_config_t *source);
