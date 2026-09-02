#pragma once

#include "network_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LTE_NET_WIFI_OTHER_OPTION "Other network..."
#define LTE_NET_WIFI_OPTIONS_BUFFER_SIZE                                      \
    ((NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS + 1U) *                           \
         (NETWORK_MANAGER_WIFI_SSID_MAX_BYTES + 1U) +                         \
     sizeof(LTE_NET_WIFI_OTHER_OPTION))

typedef struct {
    size_t source_indices[NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS];
    size_t visible_scan_count;
    bool preferred_appended;
    uint8_t preferred_ssid[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES];
    uint8_t preferred_ssid_len;
    size_t preferred_index;
    size_t other_index;
    size_t selected_index;
} lte_net_wifi_options_t;

bool lte_net_wifi_options_build(
    const network_manager_wifi_scan_list_t *scan,
    const char *preferred_ssid,
    char *options,
    size_t options_capacity,
    lte_net_wifi_options_t *model);

bool lte_net_wifi_options_copy_selection(
    const network_manager_wifi_scan_list_t *scan,
    const lte_net_wifi_options_t *model,
    size_t selected_index,
    char *ssid,
    size_t ssid_capacity);

bool lte_net_wifi_scan_result_matches(uint32_t pending_operation_id,
                                      bool scan_pending,
                                      const network_manager_wifi_scan_list_t *scan);

#ifdef __cplusplus
}
#endif
