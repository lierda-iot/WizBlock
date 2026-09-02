/**
 * @file rc_net.c
 * @brief RC Tank Demo - 网络连接层实现
 */

#include "rc_net.h"
#include "rc_net_stream.h"
#include "rc_tank_common.h"
#include "rc_video_udp_transport.h"
#include "rc_net_send_policy.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "rc_net";

#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_DISCONNECTED_BIT BIT1
#define RC_NET_VIDEO_STATS_PERIOD_FRAMES 300U

static EventGroupHandle_t s_wifi_event_group;
static rc_net_event_cb_t s_event_cb = NULL;
static uint32_t s_peer_ip = 0;
static uint32_t s_local_ip = 0;
static bool s_is_connected = false;

/* Socket 通道 */
static int s_ctrl_sock = -1;      // UDP 8001
static int s_video_sock = -1;     // UDP 8002
static int s_audio_sock = -1;     // TCP 8003

#if defined(CONFIG_RC_TANK_ROLE_TANK)
static int s_audio_client_sock = -1;  // 坦克侧接受的音频客户端
#endif

#if defined(CONFIG_RC_TANK_ROLE_TANK)
static void wifi_event_handler_ap(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "STA connected, MAC: " MACSTR, MAC2STR(event->mac));

    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "STA disconnected, MAC: " MACSTR, MAC2STR(event->mac));
        s_is_connected = false;
        s_peer_ip = 0;
        if (s_event_cb) {
            s_event_cb(false, 0);
        }
    }
}

static void ip_event_handler_ap(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        s_peer_ip = event->ip.addr;
        s_is_connected = true;
        ESP_LOGI(TAG, "STA assigned IP: " IPSTR, IP2STR(&event->ip));
        if (s_event_cb) {
            s_event_cb(true, s_peer_ip);
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)

#define REMOTE_CONNECT_ATTEMPTS       3U
#define REMOTE_CONNECT_TIMEOUT_MS 10000U
#define REMOTE_RETRY_DELAY_MS      5000U
#define REMOTE_SCAN_SETTLE_MS       100U
#define REMOTE_NETWORK_TASK_STACK   4096U

static TaskHandle_t s_remote_network_task = NULL;

static esp_err_t remote_scan_and_configure(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    ESP_LOGI(TAG, "Scanning for RC_TANK_* APs...");
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "WiFi scan start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint16_t ap_count = 0U;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "Get AP count failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (0U == ap_count) {
        ESP_LOGW(TAG, "No APs found; retry scan in %u ms",
                 (unsigned)REMOTE_RETRY_DELAY_MS);
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t *ap_records =
        malloc((size_t)ap_count * sizeof(wifi_ap_record_t));
    if (NULL == ap_records) {
        ESP_LOGE(TAG, "AP record allocation failed: count=%u", (unsigned)ap_count);
        return ESP_ERR_NO_MEM;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "Get scan records failed: %s", esp_err_to_name(ret));
        free(ap_records);
        return ret;
    }

    const size_t prefix_len = strlen(RC_WIFI_SSID_PREFIX);
    int best_index = -1;
    for (uint16_t i = 0U; i < ap_count; ++i) {
        if ((0 == strncmp((const char *)ap_records[i].ssid,
                          RC_WIFI_SSID_PREFIX, prefix_len)) &&
            ((best_index < 0) ||
             (ap_records[i].rssi > ap_records[best_index].rssi))) {
            best_index = (int)i;
        }
    }

    if (best_index < 0) {
        ESP_LOGW(TAG, "No RC_TANK_* AP found; retry scan in %u ms",
                 (unsigned)REMOTE_RETRY_DELAY_MS);
        free(ap_records);
        return ESP_ERR_NOT_FOUND;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = false,
                .required = false,
            },
        },
    };
    strlcpy((char *)wifi_config.sta.ssid,
            (const char *)ap_records[best_index].ssid,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, RC_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    ESP_LOGI(TAG, "Found tank AP: %s, RSSI: %d",
             wifi_config.sta.ssid, ap_records[best_index].rssi);
    free(ap_records);

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "Set STA config failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void remote_network_recovery_task(void *arg)
{
    (void)arg;
    bool need_scan = true;

    vTaskDelay(pdMS_TO_TICKS(REMOTE_SCAN_SETTLE_MS));
    ESP_LOGI(TAG,
             "Remote recovery task started (scan loop, %u direct attempts, %u ms interval)",
             (unsigned)REMOTE_CONNECT_ATTEMPTS,
             (unsigned)REMOTE_RETRY_DELAY_MS);

    while (1) {
        if (need_scan) {
            const esp_err_t disconnect_ret = esp_wifi_disconnect();
            if ((ESP_OK != disconnect_ret) &&
                (ESP_ERR_WIFI_NOT_CONNECT != disconnect_ret)) {
                ESP_LOGW(TAG, "Pre-scan disconnect failed: %s",
                         esp_err_to_name(disconnect_ret));
            }
            vTaskDelay(pdMS_TO_TICKS(REMOTE_SCAN_SETTLE_MS));

            const esp_err_t scan_ret = remote_scan_and_configure();
            if (ESP_OK != scan_ret) {
                vTaskDelay(pdMS_TO_TICKS(REMOTE_RETRY_DELAY_MS));
                continue;
            }
            need_scan = false;
        }

        bool connected = false;
        for (uint32_t attempt = 1U;
             attempt <= REMOTE_CONNECT_ATTEMPTS;
             ++attempt) {
            xEventGroupClearBits(s_wifi_event_group,
                                 WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT);
            ESP_LOGI(TAG, "Connecting to tank AP, attempt %u/%u",
                     (unsigned)attempt, (unsigned)REMOTE_CONNECT_ATTEMPTS);
            const esp_err_t connect_ret = esp_wifi_connect();
            if (ESP_OK == connect_ret) {
                const EventBits_t bits = xEventGroupWaitBits(
                    s_wifi_event_group,
                    WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
                    pdTRUE,
                    pdFALSE,
                    pdMS_TO_TICKS(REMOTE_CONNECT_TIMEOUT_MS));
                if (((bits & WIFI_CONNECTED_BIT) != 0U) && s_is_connected) {
                    connected = true;
                    break;
                }
                if (0U == (bits & WIFI_DISCONNECTED_BIT)) {
                    ESP_LOGW(TAG, "Connection attempt %u timed out",
                             (unsigned)attempt);
                }
            } else {
                ESP_LOGW(TAG, "esp_wifi_connect attempt %u failed: %s",
                         (unsigned)attempt, esp_err_to_name(connect_ret));
            }

            if (attempt < REMOTE_CONNECT_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(REMOTE_RETRY_DELAY_MS));
            }
        }

        if (!connected) {
            ESP_LOGW(TAG,
                     "Direct reconnect attempts exhausted; return to AP scan");
            need_scan = true;
            vTaskDelay(pdMS_TO_TICKS(REMOTE_RETRY_DELAY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Connected to tank AP; waiting for disconnect");
        (void)xEventGroupWaitBits(s_wifi_event_group,
                                  WIFI_DISCONNECTED_BIT,
                                  pdTRUE,
                                  pdFALSE,
                                  portMAX_DELAY);
        ESP_LOGW(TAG, "Tank AP disconnected; retry saved AP first");
        need_scan = false;
    }
}

static void wifi_event_handler_sta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started; recovery task owns scan/connect");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const bool was_connected = s_is_connected;
        s_is_connected = false;
        s_peer_ip = 0;
        s_local_ip = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        if (was_connected && s_event_cb) {
            s_event_cb(false, 0);
        }
    }
}

static void ip_event_handler_sta(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_local_ip = event->ip_info.ip.addr;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // 对端 IP 是网关 (AP)
        s_peer_ip = event->ip_info.gw.addr;
        const bool was_connected = s_is_connected;
        s_is_connected = true;

        xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (!was_connected && s_event_cb) {
            s_event_cb(true, s_peer_ip);
        }
    }
}
#endif

esp_err_t rc_net_init(rc_net_event_cb_t event_cb)
{
    s_event_cb = event_cb;
    s_wifi_event_group = xEventGroupCreate();

    // 初始化 TCP/IP 栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if defined(CONFIG_RC_TANK_ROLE_TANK)
    ESP_LOGI(TAG, "Initializing as SoftAP (Tank)");

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    // 设置 AP 静态 IP (192.168.4.1)
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    s_local_ip = ip_info.ip.addr;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler_ap,
                                                         NULL,
                                                         NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_AP_STAIPASSIGNED,
                                                         &ip_event_handler_ap,
                                                         NULL,
                                                         NULL));

    // 生成 SSID: RC_TANK_<MAC后6位>
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X%02X",
             RC_WIFI_SSID_PREFIX, mac[3], mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .channel = 1,
            .password = RC_WIFI_PASSWORD,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .beacon_interval = 100,
            .pmf_cfg = {
                .capable = false,
                .required = false,
            },
        },
    };
    memcpy(wifi_config.ap.ssid, ssid, strlen(ssid));

    ESP_LOGI(TAG, "AP set_mode/config, SSID=%s ch=%d", ssid, wifi_config.ap.channel);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    esp_err_t start_ret = esp_wifi_start();
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_ret));
        return start_ret;
    }

    ESP_LOGI(TAG, "SoftAP started, SSID: %s", ssid);

    return ESP_OK;

#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)
    ESP_LOGI(TAG, "Initializing as STA (Remote)");

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 禁用 NVS 自动连接，避免扫描时与后台连接竞态
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler_sta,
                                                         NULL,
                                                         NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &ip_event_handler_sta,
                                                         NULL,
                                                         NULL));

    // WiFi事件只发布状态；独立恢复任务负责扫描、直连重试和重新扫描。
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    const esp_err_t ret = esp_wifi_start();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_is_connected = false;
    s_peer_ip = 0U;
    s_local_ip = 0U;
    if (s_event_cb) {
        s_event_cb(false, 0U);
    }

    if (NULL == s_remote_network_task) {
        const BaseType_t task_ret = xTaskCreate(
            remote_network_recovery_task,
            "wifi_recover",
            REMOTE_NETWORK_TASK_STACK,
            NULL,
            tskIDLE_PRIORITY + 3,
            &s_remote_network_task);
        if (pdPASS != task_ret) {
            s_remote_network_task = NULL;
            ESP_LOGE(TAG, "Remote network recovery task create failed");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Remote network recovery active; init does not wait for AP");
    return ESP_OK;

#else
    #error "Must define CONFIG_RC_TANK_ROLE_TANK or CONFIG_RC_TANK_ROLE_REMOTE"
#endif
}

void rc_net_deinit(void)
{
#if defined(CONFIG_RC_TANK_ROLE_REMOTE)
    if (NULL != s_remote_network_task) {
        vTaskDelete(s_remote_network_task);
        s_remote_network_task = NULL;
    }
#endif
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    s_is_connected = false;
    s_peer_ip = 0U;
    s_local_ip = 0U;
}

uint32_t rc_net_get_peer_ip(void)
{
    return s_peer_ip;
}

uint32_t rc_net_get_local_ip(void)
{
    return s_local_ip;
}

bool rc_net_is_connected(void)
{
    return s_is_connected;
}

/* ==================== Socket 通道实现 ==================== */

// recv() 原语适配器: 把 socket 读取结果映射为 rc_stream 抽象语义
static rc_stream_read_result_t socket_read_adapter(void *ctx, uint8_t *buf,
                                                   size_t want, size_t *out_n)
{
    int sock = *(int *)ctx;
    int r = recv(sock, buf, want, 0);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return RC_STREAM_READ_TIMEOUT;
        }
        return RC_STREAM_READ_ERROR;
    }
    if (r == 0) {
        return RC_STREAM_READ_CLOSED;
    }
    *out_n = (size_t)r;
    return RC_STREAM_READ_DATA;
}

// 完整读取 n 字节 (TCP)
// 返回: n=成功, 0=对端关闭, -1=错误, -2=超时且未读到任何数据
// 核心状态机见 rc_net_stream.h (与 host 测试共享同一逻辑)
static int recv_all(int sock, uint8_t *buf, size_t n)
{
    int sock_ctx = sock;
    return rc_stream_recv_all(socket_read_adapter, &sock_ctx, buf, n);
}

// 完整发送 n 字节 (TCP)
static int send_all(int sock, const uint8_t *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int s = send(sock, buf + sent, n - sent, 0);
        if (s <= 0) {
            return s;
        }
        sent += s;
    }
    return sent;
}

#if defined(CONFIG_RC_TANK_ROLE_TANK)

esp_err_t rc_net_start_channels(void)
{
    // 控制通道: UDP 服务端 bind 8001
    s_ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_ctrl_sock < 0) {
        ESP_LOGE(TAG, "Failed to create ctrl UDP socket: errno %d", errno);
        return ESP_FAIL;
    }
    struct sockaddr_in ctrl_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(RC_PORT_CTRL),
    };
    if (bind(s_ctrl_sock, (struct sockaddr *)&ctrl_addr, sizeof(ctrl_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind ctrl socket: errno %d", errno);
        close(s_ctrl_sock);
        s_ctrl_sock = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Ctrl UDP socket bound to port %d", RC_PORT_CTRL);

    // 视频通道: UDP 服务端 bind 8002 (坦克向遥控器发送)
    s_video_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_video_sock < 0) {
        ESP_LOGE(TAG, "Failed to create video UDP socket: errno %d", errno);
        return ESP_FAIL;
    }
    struct sockaddr_in video_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(RC_PORT_VIDEO),
    };
    if (bind(s_video_sock, (struct sockaddr *)&video_addr, sizeof(video_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind video socket: errno %d", errno);
        close(s_video_sock);
        s_video_sock = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Video UDP socket bound to port %d", RC_PORT_VIDEO);

    // 音频通道: TCP 监听 8003 (可容忍延迟，设置超时避免阻塞)
    s_audio_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_audio_sock >= 0) {
        int opt = 1;
        setsockopt(s_audio_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 设置 accept 超时 1 秒（避免永久阻塞）
        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        setsockopt(s_audio_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in audio_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_ANY),
            .sin_port = htons(RC_PORT_AUDIO),
        };
        if (bind(s_audio_sock, (struct sockaddr *)&audio_addr, sizeof(audio_addr)) == 0 &&
            listen(s_audio_sock, 1) == 0) {
            ESP_LOGI(TAG, "Audio TCP listening on port %d", RC_PORT_AUDIO);
        } else {
            ESP_LOGE(TAG, "Failed to setup audio listen socket: errno %d", errno);
            close(s_audio_sock);
            s_audio_sock = -1;
        }
    }

    return ESP_OK;
}

// 坦克: accept 音频客户端连接 (带超时，超时返回 -1 但不打印错误)
static int accept_audio_client(void)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    s_audio_client_sock = accept(s_audio_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (s_audio_client_sock < 0) {
        // EAGAIN/EWOULDBLOCK = accept 超时（客户端尚未连接），属正常等待
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "Audio accept failed: errno %d", errno);
        }
    } else {
        // 设置接收超时 2s（PTT 模式，等待语音包时可超时返回避免永久阻塞）
        struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
        setsockopt(s_audio_client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ESP_LOGI(TAG, "Audio client connected");
    }
    return s_audio_client_sock;
}

// 坦克: 接收控制包 (UDP)
esp_err_t rc_net_ctrl_recv(uint8_t *buf, size_t buflen, size_t *out_len, uint32_t timeout_ms)
{
    if (s_ctrl_sock < 0) return ESP_ERR_INVALID_STATE;

    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(s_ctrl_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    int r = recvfrom(s_ctrl_sock, buf, buflen, 0,
                     (struct sockaddr *)&src_addr, &addr_len);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ESP_ERR_TIMEOUT;
        }
        return ESP_FAIL;
    }
    if (out_len) *out_len = r;
    return ESP_OK;
}

// 坦克: 发送视频帧 (UDP)
esp_err_t rc_net_video_send(const uint8_t *frame, size_t len)
{
    if (s_video_sock < 0 || s_peer_ip == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!frame || len < sizeof(rc_video_header_t)) {
        return ESP_ERR_INVALID_ARG;
    }

    const rc_video_header_t *frame_header = (const rc_video_header_t *)frame;
    const size_t jpeg_len = len - sizeof(*frame_header);
    if (RC_VIDEO_MAGIC != frame_header->magic ||
        frame_header->length != jpeg_len) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t chunk_count = rc_video_udp_chunk_count(jpeg_len);
    if (0U == chunk_count) {
        return ESP_ERR_INVALID_SIZE;
    }

    struct sockaddr_in remote_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = s_peer_ip,
        .sin_port = htons(RC_PORT_VIDEO),
    };

    uint8_t datagram[sizeof(rc_video_header_t) + RC_VIDEO_UDP_CHUNK_DATA_MAX];
    for (uint8_t chunk_index = 0U; chunk_index < chunk_count; ++chunk_index) {
        const size_t chunk_len = rc_video_udp_chunk_payload_len(jpeg_len, chunk_index);
        rc_video_header_t chunk_header = *frame_header;
        chunk_header.length = (uint16_t)chunk_len;
        chunk_header.reserved = rc_video_udp_pack_chunk_meta(chunk_index, chunk_count);
        memcpy(datagram, &chunk_header, sizeof(chunk_header));
        memcpy(datagram + sizeof(chunk_header),
               frame + sizeof(*frame_header) +
                   (size_t)chunk_index * RC_VIDEO_UDP_CHUNK_DATA_MAX,
               chunk_len);

        const size_t datagram_len = sizeof(chunk_header) + chunk_len;
        int sent = -1;
        int send_errno = 0;
        for (uint32_t attempt = 0U; attempt <= RC_NET_VIDEO_SEND_RETRY_LIMIT; ++attempt) {
            sent = sendto(s_video_sock, datagram, datagram_len, 0,
                          (struct sockaddr *)&remote_addr, sizeof(remote_addr));
            if (sent == (int)datagram_len) {
                break;
            }

            send_errno = errno;
            if (!rc_net_video_send_should_retry(send_errno, attempt)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        if (sent != (int)datagram_len) {
            static int64_t s_last_video_send_error_us = 0;
            const int64_t now_us = esp_timer_get_time();
            if (0 == s_last_video_send_error_us ||
                now_us - s_last_video_send_error_us >= 1000000) {
                ESP_LOGW(TAG,
                         "Video UDP send failed: seq=%u chunk=%u/%u bytes=%u sent=%d errno=%d attempts=%u",
                         (unsigned)frame_header->seq,
                         (unsigned)(chunk_index + 1U), (unsigned)chunk_count,
                         (unsigned)datagram_len, sent, send_errno,
                         (unsigned)(RC_NET_VIDEO_SEND_RETRY_LIMIT + 1U));
                s_last_video_send_error_us = now_us;
            }
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// 坦克: 接收音频段 (TCP)
esp_err_t rc_net_audio_recv(uint8_t *buf, size_t buflen, size_t *out_len)
{
    if (s_audio_client_sock < 0) {
        if (accept_audio_client() < 0) return ESP_ERR_INVALID_STATE;
    }
    // 先读 8 字节头 (rc_audio_header_t)
    rc_audio_header_t hdr;
    int ret = recv_all(s_audio_client_sock, (uint8_t *)&hdr, sizeof(hdr));
    if (ret == -2) {
        // 超时且未读到数据（正常等待语音包），不关闭连接
        return ESP_ERR_TIMEOUT;
    }
    if (ret <= 0) {
        // 连接关闭或读取错误
        close(s_audio_client_sock);
        s_audio_client_sock = -1;
        return ESP_FAIL;
    }
    if (hdr.magic != RC_AUDIO_MAGIC) {
        ESP_LOGW(TAG, "Audio bad magic: 0x%04X", hdr.magic);
        return ESP_FAIL;
    }
    if (hdr.length > buflen) {
        ESP_LOGE(TAG, "Audio payload %lu > buf %u", (unsigned long)hdr.length, (unsigned)buflen);
        return ESP_FAIL;
    }
    ret = recv_all(s_audio_client_sock, buf, hdr.length);
    if (ret <= 0) {
        // 读取负载失败（包括超时），关闭连接
        close(s_audio_client_sock);
        s_audio_client_sock = -1;
        return ESP_FAIL;
    }
    if (out_len) *out_len = hdr.length;
    return ESP_OK;
}

// 坦克侧未使用的接口 (占位, 保证链接)
esp_err_t rc_net_ctrl_send(const uint8_t *data, size_t len) { (void)data; (void)len; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t rc_net_video_recv(uint8_t *buf, size_t buflen, size_t *out_len, uint16_t *out_seq) { (void)buf; (void)buflen; (void)out_len; (void)out_seq; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t rc_net_audio_send(const uint8_t *data, size_t len) { (void)data; (void)len; return ESP_ERR_NOT_SUPPORTED; }

#elif defined(CONFIG_RC_TANK_ROLE_REMOTE)

esp_err_t rc_net_start_channels(void)
{
    if (!s_is_connected || s_peer_ip == 0) {
        ESP_LOGE(TAG, "Not connected to tank AP");
        return ESP_ERR_INVALID_STATE;
    }

    struct sockaddr_in tank_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = s_peer_ip,
    };

    // 控制通道: UDP 客户端
    s_ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_ctrl_sock < 0) {
        ESP_LOGE(TAG, "Failed to create ctrl UDP socket: errno %d", errno);
        return ESP_FAIL;
    }
    // UDP 不需要 connect，sendto 时指定目标
    ESP_LOGI(TAG, "Ctrl UDP socket ready");

    // 视频通道: UDP 客户端 bind 本地端口接收
    s_video_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_video_sock < 0) {
        ESP_LOGE(TAG, "Failed to create video UDP socket: errno %d", errno);
        return ESP_FAIL;
    }
    struct sockaddr_in local_video_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(RC_PORT_VIDEO),
    };
    if (bind(s_video_sock, (struct sockaddr *)&local_video_addr, sizeof(local_video_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind video socket: errno %d", errno);
        close(s_video_sock);
        s_video_sock = -1;
        return ESP_FAIL;
    }
    // 设置接收超时 1s（视频流应连续到达，超时说明连接异常）
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(s_video_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "Video UDP socket bound to port %d", RC_PORT_VIDEO);

    // 音频通道: TCP 连接到坦克 8003（可容忍延迟，设置发送超时避免阻塞）
    s_audio_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_audio_sock >= 0) {
        tank_addr.sin_port = htons(RC_PORT_AUDIO);
        if (connect(s_audio_sock, (struct sockaddr *)&tank_addr, sizeof(tank_addr)) == 0) {
            // 设置发送超时 2s（语音包一次性发送，超时放弃避免永久阻塞）
            struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
            setsockopt(s_audio_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            ESP_LOGI(TAG, "Audio TCP connected to tank port %d", RC_PORT_AUDIO);
        } else {
            ESP_LOGE(TAG, "Audio connect failed: errno %d", errno);
            close(s_audio_sock);
            s_audio_sock = -1;
        }
    }

    return ESP_OK;
}

// 遥控器: 发送控制包 (UDP)
esp_err_t rc_net_ctrl_send(const uint8_t *data, size_t len)
{
    if (s_ctrl_sock < 0 || s_peer_ip == 0) return ESP_ERR_INVALID_STATE;

    struct sockaddr_in tank_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = s_peer_ip,
        .sin_port = htons(RC_PORT_CTRL),
    };
    int s = sendto(s_ctrl_sock, data, len, 0,
                   (struct sockaddr *)&tank_addr, sizeof(tank_addr));
    if (s < 0) {
        ESP_LOGE(TAG, "Ctrl sendto failed: errno %d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 遥控器: 接收视频帧 (UDP)
esp_err_t rc_net_video_recv(uint8_t *buf, size_t buflen, size_t *out_len,
                            uint16_t *out_seq)
{
    if (s_video_sock < 0) return ESP_ERR_INVALID_STATE;
    if (!buf || !out_len || !out_seq) return ESP_ERR_INVALID_ARG;

    static rc_video_udp_reassembly_t reassembly;
    uint8_t datagram[sizeof(rc_video_header_t) + RC_VIDEO_UDP_CHUNK_DATA_MAX];

    while (1) {
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        const int r = recvfrom(s_video_sock, datagram, sizeof(datagram), 0,
                               (struct sockaddr *)&src_addr, &addr_len);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                rc_video_udp_reassembly_reset(&reassembly);
                return ESP_ERR_TIMEOUT;
            }
            rc_video_udp_reassembly_reset(&reassembly);
            return ESP_FAIL;
        }

        if ((size_t)r < sizeof(rc_video_header_t)) {
            continue;
        }
        const rc_video_header_t *header = (const rc_video_header_t *)datagram;
        const uint8_t *payload = datagram + sizeof(*header);
        const size_t payload_len = (size_t)r - sizeof(*header);
        const rc_video_udp_push_result_t result = rc_video_udp_reassembly_push(
            &reassembly, header, payload, payload_len,
            buf, buflen, out_len, out_seq);
        if (RC_VIDEO_UDP_FRAME_COMPLETE == result) {
            static bool have_seq = false;
            static uint16_t last_seq = 0U;
            static uint32_t complete_frames = 0U;
            static uint32_t missing_frames = 0U;
            if (have_seq) {
                const uint16_t delta = (uint16_t)(*out_seq - last_seq);
                if (delta > 1U && delta < 0x8000U) {
                    missing_frames += (uint32_t)delta - 1U;
                }
            }
            have_seq = true;
            last_seq = *out_seq;
            complete_frames++;
            if (0U == (complete_frames %
                       RC_NET_VIDEO_STATS_PERIOD_FRAMES)) {
                ESP_LOGI(TAG, "Video RX complete=%lu missing=%lu last_seq=%u",
                         (unsigned long)complete_frames,
                         (unsigned long)missing_frames,
                         (unsigned)last_seq);
            }
            return ESP_OK;
        }
    }
}

// 遥控器: 发送音频段 (TCP)
esp_err_t rc_net_audio_send(const uint8_t *data, size_t len)
{
    if (s_audio_sock < 0) return ESP_ERR_INVALID_STATE;

    if (send_all(s_audio_sock, data, len) <= 0) {
        ESP_LOGW(TAG, "Audio send failed");
        close(s_audio_sock);
        s_audio_sock = -1;
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 遥控器侧未使用的接口 (占位)
esp_err_t rc_net_ctrl_recv(uint8_t *buf, size_t buflen, size_t *out_len, uint32_t timeout_ms) { (void)buf; (void)buflen; (void)out_len; (void)timeout_ms; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t rc_net_video_send(const uint8_t *frame, size_t len) { (void)frame; (void)len; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t rc_net_audio_recv(uint8_t *buf, size_t buflen, size_t *out_len) { (void)buf; (void)buflen; (void)out_len; return ESP_ERR_NOT_SUPPORTED; }

#endif
