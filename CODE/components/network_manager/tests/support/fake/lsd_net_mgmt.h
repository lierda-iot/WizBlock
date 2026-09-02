#pragma once

#include "esp_err.h"

#include <stdbool.h>

typedef enum {
    LSD_NET_NONE = -1,
    LSD_IF_WIFI = 0,
    LSD_IF_4G = 1,
} lsd_net_if_t;

typedef enum {
    NET_WIFI_EVENT_CONNECTED = 1,
    NET_WIFI_EVENT_DISCONNECTED,
    NET_4G_EVENT_CONNECTED,
    NET_4G_EVENT_DISCONNECTED,
} net_event_type_t;

typedef void (*lsd_net_switch_cb_t)(lsd_net_if_t interface);

esp_err_t lsd_network_mgmt_init(bool enable_4g);
void lsd_net_register_switch_cb(lsd_net_switch_cb_t callback);
lsd_net_if_t lsd_netif_get(void);
bool lsd_network_is_ready(void);
esp_err_t lsd_net_send_event(net_event_type_t type);
