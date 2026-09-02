#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "fake_host.h"
#include "lsd_net_mgmt.h"
#include "lte_hal.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stddef.h>
#include <stdint.h>

esp_err_t __wrap_lsd_net_send_event(net_event_type_t type);

typedef struct {
    unsigned int length;
    unsigned int item_size;
    unsigned int count;
    unsigned char items[8U * 512U];
} fake_queue_t;

static fake_queue_t s_queue;
static int s_task_tokens[2];
static lsd_net_switch_cb_t s_switch_callback;
static esp_netif_t s_netif;
static esp_event_handler_t s_wifi_handler;
static void *s_wifi_handler_argument;
static esp_event_handler_t s_ip_handler;
static void *s_ip_handler_argument;
static esp_event_handler_t s_eth_handler;
static void *s_eth_handler_argument;
static lsd_net_if_t s_active_interface = LSD_NET_NONE;
static bool s_network_ready;
static unsigned int s_lsd_netif_query_count;
static unsigned int s_lsd_ready_query_count;
static lte_state_t s_lte_state = LTE_STATE_READY;
static unsigned int s_lte_state_query_count;
/* 模拟 SIM 卡是否在位。真实硬件 power_on 后，只有 SIM 在位才能达到 READY；
 * SIM 缺失时上电会停留在非 READY（POWERING_ON/ERROR）。默认在位。 */
static bool s_lte_sim_present = true;
static unsigned int s_net_event_counts[NET_4G_EVENT_DISCONNECTED + 1];
static unsigned int s_wifi_disconnect_count;
static unsigned int s_lte_power_off_count;
static TickType_t s_last_task_delay_ticks;
static esp_err_t s_lte_power_off_result = ESP_OK;
static esp_err_t s_wifi_config_result = ESP_OK;
static esp_err_t s_wifi_connect_result = ESP_OK;
static unsigned int s_task_create_count;
static unsigned int s_task_create_fail_on_call;
static unsigned int s_task_delete_count;
static unsigned int s_queue_delete_count;
static unsigned int s_event_unregister_count;

const char fake_wifi_event_base[] = "wifi";
const char fake_ip_event_base[] = "ip";
const char fake_eth_event_base[] = "eth";

QueueHandle_t xQueueCreate(unsigned int length, unsigned int item_size)
{
    if (length > 8U || item_size > 512U) {
        return NULL;
    }
    s_queue.length = length;
    s_queue.item_size = item_size;
    s_queue.count = 0U;
    return &s_queue;
}

BaseType_t xQueueSend(QueueHandle_t queue,
                      const void *item,
                      TickType_t timeout)
{
    (void)timeout;
    fake_queue_t *fake = (fake_queue_t *)queue;
    if (NULL == fake || fake->count >= fake->length) {
        return pdFALSE;
    }
    unsigned char *destination =
        &fake->items[fake->count * fake->item_size];
    const unsigned char *source = (const unsigned char *)item;
    for (unsigned int index = 0U; index < fake->item_size; ++index) {
        destination[index] = source[index];
    }
    ++fake->count;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *item,
                         TickType_t timeout)
{
    (void)timeout;
    fake_queue_t *fake = (fake_queue_t *)queue;
    if (NULL == fake || 0U == fake->count) {
        return pdFALSE;
    }
    const unsigned char *source = fake->items;
    unsigned char *destination = (unsigned char *)item;
    for (unsigned int index = 0U; index < fake->item_size; ++index) {
        destination[index] = source[index];
    }
    for (unsigned int offset = 1U; offset < fake->count; ++offset) {
        unsigned char *slot = &fake->items[offset * fake->item_size];
        const unsigned char *previous =
            &fake->items[(offset - 1U) * fake->item_size];
        for (unsigned int index = 0U; index < fake->item_size; ++index) {
            ((unsigned char *)previous)[index] = slot[index];
        }
    }
    --fake->count;
    return pdTRUE;
}

BaseType_t xTaskCreate(TaskFunction_t function,
                       const char *name,
                       unsigned int stack_depth,
                       void *argument,
                       unsigned int priority,
                       TaskHandle_t *task_handle)
{
    (void)function;
    (void)name;
    (void)stack_depth;
    (void)argument;
    (void)priority;
    ++s_task_create_count;
    if (0U != s_task_create_fail_on_call &&
        s_task_create_count == s_task_create_fail_on_call) {
        return pdFALSE;
    }
    if (NULL != task_handle) {
        *task_handle = &s_task_tokens[s_task_create_count - 1U];
    }
    return pdPASS;
}

void xTaskNotifyGive(TaskHandle_t task)
{
    (void)task;
}

uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit,
                          TickType_t ticks_to_wait)
{
    (void)clear_count_on_exit;
    (void)ticks_to_wait;
    return 0U;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return &s_task_tokens[0];
}

TickType_t xTaskGetTickCount(void)
{
    return 0U;
}

void vTaskDelay(TickType_t ticks)
{
    s_last_task_delay_ticks = ticks;
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
    ++s_task_delete_count;
}

void vQueueDelete(QueueHandle_t queue)
{
    (void)queue;
    ++s_queue_delete_count;
}

esp_err_t nvs_flash_init(void)
{
    return ESP_OK;
}

esp_err_t nvs_open(const char *namespace_name,
                  int open_mode,
                  nvs_handle_t *handle)
{
    (void)namespace_name;
    (void)open_mode;
    if (NULL == handle) {
        return ESP_ERR_INVALID_ARG;
    }
    *handle = 1U;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char *key,
                       void *value,
                       size_t *length)
{
    (void)handle;
    (void)key;
    (void)value;
    (void)length;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char *key,
                       const void *value,
                       size_t length)
{
    (void)handle;
    (void)key;
    (void)value;
    (void)length;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle,
                     const char *key,
                     uint8_t *value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_u8(nvs_handle_t handle,
                     const char *key,
                     uint8_t value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t esp_netif_init(void)
{
    return ESP_OK;
}

esp_netif_t *esp_netif_create_default_wifi_sta(void)
{
    return &s_netif;
}

esp_err_t esp_event_loop_create_default(void)
{
    return ESP_OK;
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t handler,
                                     void *argument)
{
    (void)event_id;
    if (WIFI_EVENT == event_base) {
        s_wifi_handler = handler;
        s_wifi_handler_argument = argument;
    } else if (IP_EVENT == event_base) {
        s_ip_handler = handler;
        s_ip_handler_argument = argument;
    } else if (ETH_EVENT == event_base) {
        s_eth_handler = handler;
        s_eth_handler_argument = argument;
    }
    return ESP_OK;
}

esp_err_t esp_event_handler_unregister(esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_t handler)
{
    (void)event_id;
    (void)handler;
    if (WIFI_EVENT == event_base) {
        s_wifi_handler = NULL;
    } else if (IP_EVENT == event_base) {
        s_ip_handler = NULL;
    } else if (ETH_EVENT == event_base) {
        s_eth_handler = NULL;
    }
    ++s_event_unregister_count;
    return ESP_OK;
}

void fake_esp_emit_event(esp_event_base_t event_base,
                         int32_t event_id,
                         void *event_data)
{
    if (WIFI_EVENT == event_base && NULL != s_wifi_handler) {
        s_wifi_handler(s_wifi_handler_argument,
                       event_base,
                       event_id,
                       event_data);
    } else if (IP_EVENT == event_base && NULL != s_ip_handler) {
        s_ip_handler(s_ip_handler_argument,
                     event_base,
                     event_id,
                     event_data);
    } else if (ETH_EVENT == event_base && NULL != s_eth_handler) {
        s_eth_handler(s_eth_handler_argument,
                      event_base,
                      event_id,
                      event_data);
    }
}

esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t esp_wifi_set_mode(int mode)
{
    (void)mode;
    return ESP_OK;
}

esp_err_t esp_wifi_set_storage(int storage)
{
    (void)storage;
    return ESP_OK;
}

esp_err_t esp_wifi_start(void)
{
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config)
{
    (void)interface;
    (void)config;
    return s_wifi_config_result;
}

esp_err_t esp_wifi_connect(void)
{
    return s_wifi_connect_result;
}

esp_err_t esp_wifi_disconnect(void)
{
    ++s_wifi_disconnect_count;
    wifi_event_sta_disconnected_t disconnected = {0};
    fake_esp_emit_event(WIFI_EVENT,
                        WIFI_EVENT_STA_DISCONNECTED,
                        &disconnected);
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
    wifi_event_sta_scan_done_t done = {0};

    (void)config;
    (void)block;
    fake_esp_emit_event(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &done);
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number)
{
    if (NULL == number) {
        return ESP_ERR_INVALID_ARG;
    }
    *number = 0U;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number,
                                       wifi_ap_record_t *records)
{
    (void)records;
    if (NULL == number) {
        return ESP_ERR_INVALID_ARG;
    }
    *number = 0U;
    return ESP_OK;
}

esp_err_t esp_wifi_clear_ap_list(void)
{
    return ESP_OK;
}

esp_err_t lte_hal_init(void)
{
    return ESP_OK;
}

esp_err_t lte_hal_power_on(void)
{
    /* 真实建模：上电后只有 SIM 在位才能达到 READY。SIM 缺失时保持 ERROR，
     * 使 power_cycle 恢复序列无法成功，最终触发 retry_exhausted（复现轮次2）。 */
    s_lte_state = s_lte_sim_present ? LTE_STATE_READY : LTE_STATE_ERROR;
    return ESP_OK;
}

esp_err_t lte_hal_power_off(void)
{
    ++s_lte_power_off_count;
    if (ESP_OK != s_lte_power_off_result) {
        return s_lte_power_off_result;
    }
    s_lte_state = LTE_STATE_OFF;
    fake_network_set(LSD_NET_NONE, false);
    fake_esp_emit_event(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, NULL);
    return ESP_OK;
}

lte_state_t lte_hal_get_state(void)
{
    ++s_lte_state_query_count;
    return s_lte_state;
}

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
    ++s_lsd_netif_query_count;
    return s_active_interface;
}

bool lsd_network_is_ready(void)
{
    ++s_lsd_ready_query_count;
    return s_network_ready;
}

esp_err_t lsd_net_send_event(net_event_type_t type)
{
    if (type < NET_WIFI_EVENT_CONNECTED ||
        type > NET_4G_EVENT_DISCONNECTED) {
        return ESP_ERR_INVALID_ARG;
    }
    ++s_net_event_counts[type];
    return ESP_OK;
}

esp_err_t __real_lsd_net_send_event(net_event_type_t type)
{
    const esp_err_t result = lsd_net_send_event(type);
    if (ESP_OK != result) {
        return result;
    }
    if (NET_4G_EVENT_CONNECTED == type &&
        LSD_NET_NONE == s_active_interface) {
        fake_network_set(LSD_IF_4G, true);
    } else if (NET_4G_EVENT_DISCONNECTED == type &&
               LSD_IF_4G == s_active_interface) {
        /* Match Round 8: active switches to NONE while the query stays stale. */
        s_active_interface = LSD_NET_NONE;
        if (NULL != s_switch_callback) {
            s_switch_callback(LSD_NET_NONE);
        }
    }
    return result;
}

esp_err_t fake_lsd_emit_event(net_event_type_t type)
{
    return __wrap_lsd_net_send_event(type);
}

void fake_network_set(lsd_net_if_t active_interface, bool ready)
{
    s_active_interface = active_interface;
    s_network_ready = ready;
    if (NULL != s_switch_callback) {
        s_switch_callback(active_interface);
    }
}

unsigned int fake_net_event_count(net_event_type_t type)
{
    if (type < NET_WIFI_EVENT_CONNECTED ||
        type > NET_4G_EVENT_DISCONNECTED) {
        return 0U;
    }
    return s_net_event_counts[type];
}

unsigned int fake_lsd_netif_query_count(void)
{
    return s_lsd_netif_query_count;
}

unsigned int fake_lsd_ready_query_count(void)
{
    return s_lsd_ready_query_count;
}

unsigned int fake_wifi_disconnect_count(void)
{
    return s_wifi_disconnect_count;
}

unsigned int fake_lte_power_off_count(void)
{
    return s_lte_power_off_count;
}

uint32_t fake_last_task_delay_ticks(void)
{
    return s_last_task_delay_ticks;
}

void fake_set_lte_power_off_result(esp_err_t result)
{
    s_lte_power_off_result = result;
}

void fake_set_lte_state(lte_state_t state)
{
    s_lte_state = state;
    /* 同步 SIM 在位标志：READY 隐含 SIM 在位，ERROR/OFF 隐含缺失（或故障） */
    s_lte_sim_present = (LTE_STATE_READY == state);
}

unsigned int fake_lte_state_query_count(void)
{
    return s_lte_state_query_count;
}

void fake_set_lte_sim_present(bool present)
{
    s_lte_sim_present = present;
    /* 如果设置为"SIM 缺失"但当前状态为 READY，降级到 ERROR（不一致状态修正） */
    if (!present && LTE_STATE_READY == s_lte_state) {
        s_lte_state = LTE_STATE_ERROR;
    }
    /* 如果设置为"SIM 在位"但当前为 ERROR，可选择恢复 READY（或保持 ERROR）。
     * 这里保守处理：不自动恢复，由后续 power_on 或显式 fake_set_lte_state 驱动。 */
}

void fake_set_wifi_config_result(esp_err_t result)
{
    s_wifi_config_result = result;
}

void fake_set_wifi_connect_result(esp_err_t result)
{
    s_wifi_connect_result = result;
}

void fake_set_task_create_fail_on_call(unsigned int call)
{
    s_task_create_fail_on_call = call;
}

unsigned int fake_task_delete_count(void)
{
    return s_task_delete_count;
}

unsigned int fake_queue_delete_count(void)
{
    return s_queue_delete_count;
}

unsigned int fake_event_unregister_count(void)
{
    return s_event_unregister_count;
}
