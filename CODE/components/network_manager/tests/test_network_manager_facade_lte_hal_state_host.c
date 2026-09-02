#include "network_manager.h"
#include "fake_host.h"

void network_manager_host_test_process_runtime(uint32_t now_ms);

int main(void)
{
    network_manager_snapshot_t snapshot;

    /* 启动 network_manager，初始状态应为 STARTING */
    if (ESP_OK != network_manager_start()) {
        return 1;
    }
    if (ESP_OK != network_manager_get_snapshot(&snapshot) ||
        NETWORK_MANAGER_LIFECYCLE_STARTING != snapshot.lifecycle) {
        return 2;
    }

    /* 模拟 4G 模组正常上电并建立连接 */
    fake_set_lte_state(LTE_STATE_READY);
    fake_network_set(LSD_IF_4G, true);
    fake_esp_emit_event(ETH_EVENT, ETHERNET_EVENT_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);
    network_manager_host_test_process_runtime(0U);

    /* 验证 4G 已连接 */
    if (ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 3;
    }
    if (NETWORK_MANAGER_4G_READY != snapshot.cellular_phase) {
        return 4;
    }

    /* A modem-state change without a network event is not a component input. */
    fake_set_lte_state(LTE_STATE_ERROR);

    /* Runtime processing must not query modem state or synthesize a disconnect. */
    network_manager_host_test_process_runtime(100U);

    if (ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 5;
    }
    if (NETWORK_MANAGER_4G_READY != snapshot.cellular_phase ||
        0U != fake_lte_state_query_count()) {
        return 6;
    }

    /* Long idle processing remains passive and does not query or effect LTE. */
    network_manager_host_test_process_runtime(5200U);
    if (ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 8;
    }
    if (NETWORK_MANAGER_4G_READY != snapshot.cellular_phase ||
        0U != fake_lte_state_query_count()) {
        return 7;
    }

    /* A later real ETH/IP report, not modem polling, restores READY. */
    fake_set_lte_state(LTE_STATE_READY);
    fake_network_set(LSD_IF_4G, true);
    fake_esp_emit_event(ETH_EVENT, ETHERNET_EVENT_CONNECTED, NULL);
    fake_esp_emit_event(IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);

    /* 推进到恢复时刻：ipv4_ready=true 会取消所有待处理定时器并回到 READY */
    network_manager_host_test_process_runtime(5300U);

    if (ESP_OK != network_manager_get_snapshot(&snapshot)) {
        return 8;
    }
    if (NETWORK_MANAGER_4G_READY != snapshot.cellular_phase) {
        return 9;
    }

    return 0;  /* 测试通过 */
}
