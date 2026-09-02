#pragma once

#include "esp_event.h"
#include "esp_wifi.h"
#include "lsd_net_mgmt.h"
#include "lte_hal.h"

#include <stdbool.h>
#include <stdint.h>

void fake_esp_emit_event(esp_event_base_t event_base,
                         int32_t event_id,
                         void *event_data);
void fake_network_set(lsd_net_if_t active_interface, bool ready);
esp_err_t fake_lsd_emit_event(net_event_type_t type);
unsigned int fake_net_event_count(net_event_type_t type);
unsigned int fake_lsd_netif_query_count(void);
unsigned int fake_lsd_ready_query_count(void);
unsigned int fake_wifi_disconnect_count(void);
unsigned int fake_lte_power_off_count(void);
uint32_t fake_last_task_delay_ticks(void);
void fake_set_lte_power_off_result(esp_err_t result);
void fake_set_lte_state(lte_state_t state);
unsigned int fake_lte_state_query_count(void);
void fake_set_lte_sim_present(bool present);
void fake_set_wifi_config_result(esp_err_t result);
void fake_set_wifi_connect_result(esp_err_t result);
void fake_set_task_create_fail_on_call(unsigned int call);
unsigned int fake_task_delete_count(void);
unsigned int fake_queue_delete_count(void);
unsigned int fake_event_unregister_count(void);
