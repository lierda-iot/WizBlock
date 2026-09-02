/* SPDX-FileCopyrightText: 2022-2025 lierda CO LTD
 *
 */

#ifndef LSD_NET_MGMT_H
#define LSD_NET_MGMT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LSD_NET_NONE = -1,
    LSD_IF_WIFI = 0,
    LSD_IF_4G   = 1,
} lsd_net_if_t;

// 网络事件消息类型
typedef enum {
    NET_WIFI_EVENT_CONNECTED = 1,
    NET_WIFI_EVENT_DISCONNECTED,
    NET_4G_EVENT_CONNECTED,
    NET_4G_EVENT_DISCONNECTED,
} net_event_type_t;

/**
 * @brief 默认网卡切换通知回调类型
 *
 * 当默认网卡发生变更时被调用。
 * @param new_if  切换后的接口类型（LSD_IF_WIFI / LSD_IF_4G / LSD_NET_NONE）
 */
typedef void (*lsd_net_switch_cb_t)(lsd_net_if_t new_if);



/**
 * @brief 网络管理模块初始化
 *
 * 初始化网络管理模块，根据配置启用相应的网络接口。
 * 若 enable_4g 为 true，将同时初始化 4G 模块驱动（lierda模组）。
 *
 * @return
 *      - ESP_OK              成功
 *      - ESP_FAIL            初始化失败
 */
esp_err_t lsd_network_mgmt_init(bool enable_4g);


/**
 * @brief 网络管理模块反初始化
 *
 * 停止所有网络接口，释放资源，取消所有注册的回调。
 * 调用后模块恢复为未初始化状态。
 */
void lsd_network_mgmt_deinit(void);

/**
 * @brief 注册默认网卡切换通知回调
 *
 * 每次默认网卡发生变更时，组件内部会调用该回调通知用户。
 * 传入 NULL 可取消注册。
 * @note 回调在网络管理任务上下文中执行，不应阻塞。
 * @param cb  回调函数指针，参数为切换后的接口类型
 */
void lsd_net_register_switch_cb(lsd_net_switch_cb_t cb);



/**
 * @brief 获取当前默认网络接口类型
 * @return LSD_IF_WIFI / LSD_IF_4G / LSD_NET_NONE 
 */
lsd_net_if_t lsd_netif_get(void);



/**
 * @brief 获取当前网络是否可用（是否通外网）
 *
 * 非阻塞式检查，返回当前网络状态摘要。
 * 
 * @note 不区分 4G/Wi-Fi，只要有任意链路通外网即返回 true
 * 
 * @return true   网络可用，可通外网
 * @return false  网络不可用，无外网连接
 */
bool lsd_network_is_ready(void);


/**
 * @brief 向网络事件队列发送事件
 * @param type  net_event_type_t 中定义的事件类型
 * 
 * @return ESP_OK 成功；
 * @return ESP_FAIL 队列未就绪或已满
 */
esp_err_t lsd_net_send_event(net_event_type_t type);



#ifdef __cplusplus
}

#endif

#endif // LSD_NET_MGMT_H
