/*
 * rc_tank_common.h - 遥控坦克 Demo 公共定义
 *
 * 协议数据结构和端口常量。坦克与遥控器共享。
 * 详见 design.md 第 4-5 章。
 */
#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <stdint.h>

#include "rc_ctrl_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 角色标识 ===== */
#if defined(CONFIG_RC_TANK_ROLE_TANK)
#define RC_ROLE_NAME "TANK"
#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
#define RC_ROLE_NAME "REMOTE"
#else
#error "RC_TANK_ROLE not defined: select Tank or Remote in menuconfig"
#endif

/* ===== WiFi 组网参数 (design.md 4.1) ===== */
#define RC_WIFI_SSID_PREFIX   "RC_TANK_"   /* 坦克 SoftAP SSID 前缀 */
#define RC_WIFI_PASSWORD      "12345678"   /* 固定密码 (Q9) */
#define RC_WIFI_CHANNEL       1
#define RC_WIFI_MAX_STA       1            /* 1v1 固定绑定 */
#define RC_TANK_AP_IP         "192.168.4.1"

/* ===== 三通道端口 (design.md 4.2) ===== */
#define RC_PORT_CTRL          8001         /* 控制流 UDP: 遥控器->坦克 */
#define RC_PORT_VIDEO         8002         /* 视频流 UDP: 坦克->遥控器 */
#define RC_PORT_AUDIO         8003         /* 语音流 TCP: 遥控器->坦克 */

/* ===== 协议包魔数 ===== */
#define RC_VIDEO_MAGIC        0xAA55
#define RC_AUDIO_MAGIC        0xBB66

#pragma pack(push, 1)
/* ===== 视频帧头 (8 字节) (design.md 5.2) ===== */
typedef struct {
    uint16_t magic;       /* RC_VIDEO_MAGIC */
    uint16_t length;      /* JPEG 数据长度(不含头) */
    uint16_t seq;         /* 帧序号 */
    uint16_t reserved;    /* 对齐/预留 */
} rc_video_header_t;

/* ===== 语音段头 (8 字节) (design.md 5.3) ===== */
typedef struct {
    uint16_t magic;       /* RC_AUDIO_MAGIC */
    uint16_t sample_rate; /* 16000 */
    uint32_t length;      /* Opus 编码后总字节数 */
} rc_audio_header_t;
#pragma pack(pop)

/* ===== 关键参数 (design.md / requirements.md) ===== */
#define RC_CTRL_TIMEOUT_MS      300   /* 失联停车超时 (REQ-035-007) */
#define RC_CTRL_HEARTBEAT_MS    100   /* 遥控器心跳间隔 */
#define RC_VIDEO_WIDTH          240   /* 网络 JPEG 宽 */
#define RC_VIDEO_HEIGHT         180   /* 网络 JPEG 高 */
#define RC_AUDIO_SAMPLE_RATE    16000 /* Opus 采样率 (EX-009) */
#define RC_AUDIO_MIN_MS         500   /* 最短录音 */
#define RC_AUDIO_MAX_MS         10000 /* 最长录音 */
#define RC_TANK_PLAY_VOLUME     70    /* 坦克播放音量 % */
#define RC_UI_REFRESH_MS        5000  /* 电量/状态刷新间隔 */

#ifdef __cplusplus
}
#endif
