#include "lsd_net_mgmt.h"

static lsd_net_switch_cb_t s_switch_callback;

esp_err_t lsd_network_mgmt_init(bool enable_4g)
{
    (void)enable_4g;
    return ESP_OK;
}

void lsd_net_register_switch_cb(lsd_net_switch_cb_t callback)
{
    s_switch_callback = callback;
}

lsd_net_if_t lsd_netif_get(void)
{
    (void)s_switch_callback;
    return LSD_NET_NONE;
}

bool lsd_network_is_ready(void)
{
    return false;
}

esp_err_t lsd_net_send_event(net_event_type_t type)
{
    (void)type;
    return ESP_OK;
}
