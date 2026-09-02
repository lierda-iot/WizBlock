/**
 * @file rc_net.h
 * @brief RC Tank Demo - 网络连接层
 *
 * 坦克: SoftAP (SSID: RC_TANK_<MAC后6位>) + 三通道服务端
 * 遥控器: STA (连接到坦克 AP) + 三通道客户端
 *
 * 三通道:
 * - 控制流: UDP 8001 (遥控器→坦克)
 * - 视频流: UDP 8002 (坦克→遥控器)
 * - 音频流: TCP 8003 (遥控器→坦克)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 网络事件回调
 * @param connected true=已连接, false=断开
 * @param peer_ip 对端 IP (仅 connected=true 时有效)
 */
typedef void (*rc_net_event_cb_t)(bool connected, uint32_t peer_ip);

/**
 * @brief 初始化并启动网络 (根据角色自动选择 SoftAP 或 STA)
 *
 * Remote返回ESP_OK表示持续扫描/重连任务已启动，不表示已经获得IP。
 * @param event_cb 连接状态回调 (可选)
 * @return ESP_OK 成功
 */
esp_err_t rc_net_init(rc_net_event_cb_t event_cb);

/**
 * @brief 启动 socket 通道 (WiFi 连接成功后调用)
 * @return ESP_OK 成功
 */
esp_err_t rc_net_start_channels(void);

/**
 * @brief 停止网络
 */
void rc_net_deinit(void);

/**
 * @brief 获取对端 IP
 * @return 对端 IP (网络字节序), 0=未连接
 */
uint32_t rc_net_get_peer_ip(void);

/**
 * @brief 获取本机 IP
 * @return 本机 IP (网络字节序), 0=未获取
 */
uint32_t rc_net_get_local_ip(void);

/**
 * @brief 检查是否已连接
 */
bool rc_net_is_connected(void);

/* ========== 控制通道 (UDP 8001) ========== */

/**
 * @brief 发送控制包 (遥控器用)
 * @param data 数据缓冲
 * @param len 数据长度
 * @return ESP_OK 成功
 */
esp_err_t rc_net_ctrl_send(const uint8_t *data, size_t len);

/**
 * @brief 接收控制包 (坦克用)
 * @param buf 接收缓冲
 * @param buflen 缓冲大小
 * @param out_len 实际接收长度 (输出)
 * @param timeout_ms 超时(ms), 0=非阻塞
 * @return ESP_OK 成功, ESP_ERR_TIMEOUT 超时
 */
esp_err_t rc_net_ctrl_recv(uint8_t *buf, size_t buflen, size_t *out_len, uint32_t timeout_ms);

/* ========== 视频通道 (UDP 8002) ========== */

/**
 * @brief 发送视频帧 (坦克用)
 *
 * UDP 传输: JPEG 按不超过 MTU 的 datagram 分片发送，8 字节帧头的
 * reserved 字段编码分片序号和总数。发送失败时丢弃当前帧。
 *
 * @param frame 帧数据 (含 8 字节帧头)
 * @param len 帧长度 (头 + JPEG)
 * @return ESP_OK 成功
 */
esp_err_t rc_net_video_send(const uint8_t *frame, size_t len);

/**
 * @brief 接收视频帧 (遥控器用)
 *
 * UDP 传输: 按 seq 和 reserved 分片元数据重组完整 JPEG，只在全部分片
 * 到齐后写入 buf 并返回。缺片帧由后续新帧替换，不阻塞控制通道。
 *
 * @param buf 接收缓冲 (仅存放 JPEG 负载)
 * @param buflen 缓冲大小
 * @param out_len 实际接收 JPEG 长度 (输出)
 * @param out_seq 完整 JPEG 对应的帧序号 (输出)
 * @return ESP_OK 成功, ESP_ERR_TIMEOUT 超时
 */
esp_err_t rc_net_video_recv(uint8_t *buf, size_t buflen, size_t *out_len,
                            uint16_t *out_seq);

/* ========== 音频通道 (TCP 8003) ========== */

/**
 * @brief 发送音频段 (遥控器用)
 * @param data 音频数据
 * @param len 数据长度
 * @return ESP_OK 成功
 */
esp_err_t rc_net_audio_send(const uint8_t *data, size_t len);

/**
 * @brief 接收音频段 (坦克用)
 * @param buf 接收缓冲
 * @param buflen 缓冲大小
 * @param out_len 实际接收长度 (输出)
 * @return ESP_OK 成功
 */
esp_err_t rc_net_audio_recv(uint8_t *buf, size_t buflen, size_t *out_len);

#ifdef __cplusplus
}
#endif
