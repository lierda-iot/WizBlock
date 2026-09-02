#pragma once

#include "network_manager.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEMO_NET_STATE_NONE = -1,
    DEMO_NET_STATE_STARTING = 0,
    DEMO_NET_STATE_WIFI_CONNECTING,
    DEMO_NET_STATE_WIFI_CHECKING,
    DEMO_NET_STATE_WIFI_CONNECTED,
    DEMO_NET_STATE_4G_CONNECTING,
    DEMO_NET_STATE_4G_CONNECTED,
    DEMO_NET_STATE_SWITCHING_TO_WIFI,
    DEMO_NET_STATE_NO_NETWORK,
    DEMO_NET_STATE_ERROR,
} demo_net_state_t;

typedef enum {
    DEMO_NET_DETAIL_NONE = 0,
    DEMO_NET_DETAIL_WIFI_AUTH_FAILED,
    DEMO_NET_DETAIL_WIFI_NOT_FOUND,
    DEMO_NET_DETAIL_WIFI_DISCONNECTED,
    DEMO_NET_DETAIL_WIFI_NO_INTERNET,
    DEMO_NET_DETAIL_4G_REMAINS_ONLINE,
    DEMO_NET_DETAIL_CONFIG_SAVE_FAILED,
    DEMO_NET_DETAIL_LTE_INIT_FAILED,
    DEMO_NET_DETAIL_NETWORK_INIT_FAILED,
} demo_net_detail_t;

typedef void (*demo_net_state_cb_t)(demo_net_state_t state,
                                    demo_net_detail_t detail,
                                    network_manager_mode_t mode,
                                    void *user_ctx);

typedef struct {
    demo_net_state_cb_t state_cb;
    void *user_ctx;
} demo_network_config_t;

esp_err_t demo_network_init(const demo_network_config_t *config);

esp_err_t demo_network_set_wifi_and_start(const uint8_t *ssid,
                                          uint8_t ssid_len,
                                          const uint8_t *password,
                                          uint8_t password_len);

network_manager_mode_t demo_network_get_mode(void);

bool demo_network_is_ready(void);

#ifdef __cplusplus
}
#endif
