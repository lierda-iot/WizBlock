#pragma once

#include "demo_network.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_UI_WIFI_SSID_MAX_LEN     32
#define DEMO_UI_WIFI_PASSWORD_MAX_LEN 63

typedef struct {
    char ssid[DEMO_UI_WIFI_SSID_MAX_LEN + 1];
    char password[DEMO_UI_WIFI_PASSWORD_MAX_LEN + 1];
} demo_ui_wifi_settings_t;

typedef void (*demo_ui_confirm_cb_t)(const demo_ui_wifi_settings_t *settings,
                                     void *user_ctx);

typedef struct {
    demo_ui_confirm_cb_t confirm_cb;
    void *user_ctx;
} demo_ui_config_t;

esp_err_t demo_ui_init(const demo_ui_config_t *config,
                      const demo_ui_wifi_settings_t *initial_settings);

void demo_ui_update_network_state(demo_net_state_t state,
                                 demo_net_detail_t detail,
                                 network_manager_mode_t mode);

void demo_ui_show_settings_page(void);

esp_err_t demo_ui_start_wifi_scan(void);

#ifdef __cplusplus
}
#endif
