#pragma once

#include "esp_err.h"

#include <stdint.h>

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *,
                                    esp_event_base_t,
                                    int32_t,
                                    void *);

extern const char fake_wifi_event_base[];
extern const char fake_ip_event_base[];
extern const char fake_eth_event_base[];

#define WIFI_EVENT fake_wifi_event_base
#define IP_EVENT fake_ip_event_base
#define ETH_EVENT fake_eth_event_base
#define ESP_EVENT_ANY_ID (-1)
#define WIFI_EVENT_STA_CONNECTED 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define IP_EVENT_STA_LOST_IP 4
#define IP_EVENT_ETH_GOT_IP 5
#define IP_EVENT_ETH_LOST_IP 6
#define ETHERNET_EVENT_CONNECTED 7
#define ETHERNET_EVENT_DISCONNECTED 8
#define WIFI_EVENT_SCAN_DONE 9

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t handler,
                                     void *argument);
esp_err_t esp_event_handler_unregister(esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_t handler);
