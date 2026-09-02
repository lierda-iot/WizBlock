# RC Tank Demo P1/P2 问题深度检查报告

**检查时间**: 2026-08-17  
**检查范围**: 在 P0 修复完成后，对剩余 P1/P2 问题进行深度审查  
**检查方法**: 代码审查 + 执行路径分析

---

## P1 级别问题（强烈建议修复）

### P1.1 网络连接事件回调的重入风险（新发现）

**位置**: `app_main.c:22-58` (`net_event_callback`) + `rc_net.c:62-72, 104-118`

**问题**:
1. **坦克侧**: IP 事件 `IP_EVENT_AP_STAIPASSIGNED` 每次触发都会调用 `net_event_callback(true, ...)`
2. **回调中启动的资源**:
   - `rc_net_start_channels()` → 创建 3 个 socket (ctrl/video/audio)
   - `rc_video_start_tank()` → 创建 `video_tx` 任务
   - `rc_audio_play_start()` → 创建 `audio_rx` 和 `audio_play` 任务

3. **触发场景**:
   - 遥控器首次连接 → 触发 IP 分配事件
   - 遥控器断开后重连 → 再次触发 IP 分配事件
   - DHCP 租约更新 → 可能再次触发（取决于 ESP-IDF 实现）

4. **后果**:
   - Socket 重复创建，旧 socket 未关闭 → 文件描述符泄漏
   - 任务重复创建 → 多个相同任务并发运行，争抢同一 socket
   - 队列/信号量未防护 → `s_audio_queue` 被多个 `audio_rx` 任务并发访问

**证据**:
```c
// app_main.c:29-34
static void net_event_callback(bool connected, uint32_t peer_ip)
{
    if (connected) {
        // ... 无防重入检查，每次 connected=true 都会执行
        esp_err_t ret = rc_net_start_channels();  // 重复创建 socket
        // ...
        ret = rc_video_start_tank();  // 重复创建任务
        ret = rc_audio_play_start();  // 重复创建任务
    }
}
```

```c
// rc_net.c:378-397 (rc_net_start_channels)
esp_err_t rc_net_start_channels(void)
{
    // 无 "if (s_ctrl_sock >= 0) return ESP_OK;" 检查
    s_ctrl_sock = socket(...);  // 旧 socket 未 close，直接覆盖
    // ...
}
```

**影响**: 
- 遥控器断开重连后，坦克侧可能创建第 2 个 video_tx 任务
- 两个任务同时向 `s_video_client_sock` 写入 → 帧数据交错损坏
- Socket 泄漏 → 最终耗尽文件描述符（ESP32-S3 默认限制约 48 个）

**修复建议**:
```c
// 方案1: 在 net_event_callback 中添加静态标志
static bool s_channels_started = false;
static void net_event_callback(bool connected, uint32_t peer_ip)
{
    if (connected && !s_channels_started) {
        rc_net_start_channels();
        rc_video_start_tank();
        rc_audio_play_start();
        s_channels_started = true;
    } else if (!connected && s_channels_started) {
        // TODO: 清理 socket 和任务（需要新增接口）
        s_channels_started = false;
    }
}

// 方案2: 在 rc_net_start_channels 中防重入
esp_err_t rc_net_start_channels(void)
{
    if (s_ctrl_sock >= 0) {
        ESP_LOGW(TAG, "Channels already started");
        return ESP_OK;  // 或返回 ESP_ERR_INVALID_STATE
    }
    // ... 创建 socket
}
```

**优先级**: P1（遥控器重连是正常使用场景，必须支持）

---

### P1.2 遥控器侧 WiFi 重连的相同问题

**位置**: `rc_net.c:104-118` (ip_event_handler_sta)

**问题**: 
遥控器侧获得 IP 后也会触发 `net_event_callback(true, ...)`，导致：
- `rc_net_start_channels()` 重复连接到坦克 (TCP connect)
- `rc_video_start_remote()` 重复创建 `video_rx` 任务

**触发场景**: 
- WiFi 断线重连成功后再次获得 IP

**后果**: 
- 旧 TCP 连接未关闭，新连接覆盖 `s_video_sock` → 旧连接成为僵尸连接
- 多个 `video_rx` 任务竞争同一 socket → 解码错误或任务卡死

---

### P1.3 遥控器录音的内存分配失败路径不严谨

**位置**: `rc_audio.c:106-121`

**原代码**:
```c
int16_t *pcm_buf = heap_caps_malloc(pcm_buf_size, MALLOC_CAP_SPIRAM);
uint8_t *opus_buf = heap_caps_malloc(MAX_OPUS_FRAME_SIZE, MALLOC_CAP_SPIRAM);
uint8_t *audio_pkt = heap_caps_malloc(max_audio_size, MALLOC_CAP_SPIRAM);

if (!pcm_buf || !opus_buf || !audio_pkt) {
    ESP_LOGE(TAG, "Memory alloc failed");
    free(pcm_buf); free(opus_buf); free(audio_pkt);  // free(NULL) 安全但不严谨
    opus_codec_deinit();
    return ESP_ERR_NO_MEM;
}
```

**问题**: 
如果 `opus_buf` 分配失败，`pcm_buf` 已分配但逻辑上未使用（虽然 `free(NULL)` 安全）

**修复建议**:
```c
int16_t *pcm_buf = heap_caps_malloc(pcm_buf_size, MALLOC_CAP_SPIRAM);
if (!pcm_buf) {
    ESP_LOGE(TAG, "PCM buffer alloc failed");
    opus_codec_deinit();
    return ESP_ERR_NO_MEM;
}

uint8_t *opus_buf = heap_caps_malloc(MAX_OPUS_FRAME_SIZE, MALLOC_CAP_SPIRAM);
if (!opus_buf) {
    ESP_LOGE(TAG, "Opus buffer alloc failed");
    free(pcm_buf);
    opus_codec_deinit();
    return ESP_ERR_NO_MEM;
}

uint8_t *audio_pkt = heap_caps_malloc(max_audio_size, MALLOC_CAP_SPIRAM);
if (!audio_pkt) {
    ESP_LOGE(TAG, "Audio packet buffer alloc failed");
    free(pcm_buf);
    free(opus_buf);
    opus_codec_deinit();
    return ESP_ERR_NO_MEM;
}
```

**影响**: 低（内存分配失败概率低，且 `free(NULL)` 行为正确）

---

### P1.4 控制任务创建失败后的状态不一致

**位置**: `rc_control.c:123-133`

**原代码**:
```c
esp_err_t rc_control_start_tank(void)
{
    s_last_ctrl_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;  // 已初始化

    BaseType_t ret = xTaskCreate(ctrl_rx_task, "ctrl_rx", 3072, NULL,
                                  configMAX_PRIORITIES - 2, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ctrl_rx task");
        return ESP_FAIL;  // s_last_ctrl_ms 已被修改但任务未启动
    }
    // ...
}
```

**问题**: 任务创建失败时，`s_last_ctrl_ms` 已初始化但任务未运行

**修复建议**:
```c
esp_err_t rc_control_start_tank(void)
{
    BaseType_t ret = xTaskCreate(ctrl_rx_task, "ctrl_rx", 3072, NULL,
                                  configMAX_PRIORITIES - 2, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ctrl_rx task");
        return ESP_FAIL;
    }

    s_last_ctrl_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Tank control started");
    return ESP_OK;
}
```

**影响**: 极低（任务创建失败非常罕见）

---

## P2 级别问题（建议优化）

### P2.1 Opus 编码器每次录音重复初始化

**位置**: `rc_audio.c:100-104, 167`

**问题**: 
每次按下 SW3 都会调用 `opus_codec_encoder_init()` 和 `opus_codec_deinit()`

**优化建议**: 在 `rc_audio_record_init()` 中初始化一次，保持编码器生命周期

**影响**: 略增加 PTT 启动延迟（约 10ms）

---

### P2.2 致命错误后的无意义空循环

**位置**: `app_main.c:70, 77, 103, 111, 126, 133, 152, 161`

**问题**: 
部分致命错误（板级初始化、网络初始化）失败后 `return`，回到 `app_main()` 的无限循环（Line 211-214），设备看似运行但无功能

**优化建议**:
```c
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Critical init failed, restarting in 5s...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
```

**影响**: 错误状态下难以排查

---

### P2.3 WiFi 事件处理中的未检查错误

**位置**: `rc_net.c:83` (WIFI_EVENT_STA_START)

**代码**:
```c
if (event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();  // 未检查返回值
}
```

**问题**: `esp_wifi_connect()` 可能失败（例如配置错误），但未处理

**优化建议**:
```c
if (event_id == WIFI_EVENT_STA_START) {
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
}
```

---

### P2.4 遥控器侧 WiFi 重连次数未重置的潜在 Bug

**位置**: `rc_net.c:76-98`

**代码**:
```c
static int s_retry_num = 0;

static void wifi_event_handler_sta(...)
{
    // ...
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            // ...
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            // s_retry_num 未清零
        }
    }
}
```

**问题**: 达到最大重试次数后，`s_retry_num` 未重置，下次断线时无法重试

**修复建议**:
```c
} else {
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    ESP_LOGE(TAG, "Failed to connect to AP after %d attempts", MAX_RETRY);
    s_retry_num = 0;  // 重置计数器，允许下次重试
}
```

---

## P2.5 视频/音频任务分配失败后资源未清理

**位置**: 
- `rc_audio.c:373-382` (rc_audio_play_start)
- `rc_video.c:265-282` (rc_video_start_tank)
- `rc_video.c:385-395` (rc_video_start_remote)

**问题**: 
任务创建失败时返回 `ESP_FAIL`，但未清理已初始化的资源

**示例 (rc_audio.c)**:
```c
esp_err_t rc_audio_play_start(void)
{
    BaseType_t r1 = xTaskCreate(audio_rx_task, "audio_rx", 4096, NULL, ...);
    BaseType_t r2 = xTaskCreate(audio_play_task, "audio_play", 4096, NULL, ...);
    if (r1 != pdPASS || r2 != pdPASS) {
        ESP_LOGE(TAG, "Audio tasks create failed");
        // 如果 r1 成功但 r2 失败，r1 任务已启动但孤立运行
        // s_audio_queue 已在 rc_audio_play_init 中创建，未删除
        return ESP_FAIL;
    }
    // ...
}
```

**修复建议**: 需要保存任务句柄并在失败时清理：
```c
static TaskHandle_t s_audio_rx_task_handle = NULL;
static TaskHandle_t s_audio_play_task_handle = NULL;

esp_err_t rc_audio_play_start(void)
{
    BaseType_t r1 = xTaskCreate(audio_rx_task, "audio_rx", 4096, NULL, 
                                configMAX_PRIORITIES - 3, &s_audio_rx_task_handle);
    BaseType_t r2 = xTaskCreate(audio_play_task, "audio_play", 4096, NULL,
                                configMAX_PRIORITIES - 3, &s_audio_play_task_handle);
    if (r1 != pdPASS || r2 != pdPASS) {
        ESP_LOGE(TAG, "Audio tasks create failed");
        if (s_audio_rx_task_handle) {
            vTaskDelete(s_audio_rx_task_handle);
            s_audio_rx_task_handle = NULL;
        }
        if (s_audio_play_task_handle) {
            vTaskDelete(s_audio_play_task_handle);
            s_audio_play_task_handle = NULL;
        }
        if (s_audio_queue) {
            vQueueDelete(s_audio_queue);
            s_audio_queue = NULL;
        }
        return ESP_FAIL;
    }
    // ...
}
```

---

## 已正确实现的部分 ✅

### 控制层逻辑正确
- `rc_motor_apply` 有去重保护（Line 50）
- `rc_motor_stop` 正确更新 `s_last_cmd`
- 超时停车逻辑正确（`ctrl_rx_task` Line 113-119）

### 遥控器控制发送正确
- STOP 心跳机制正确（`ctrl_tx_task` Line 172）
- 100ms 周期符合实时性要求

### WiFi 事件处理基本正确
- 坦克侧 AP 事件正确处理连接/断开
- 遥控器侧 STA 事件有重试机制
- IP 分配后正确触发回调

---

## 修复优先级汇总

### 必须修复（影响正常使用）
1. **P1.1 + P1.2**: 网络重连时的重入问题 → 遥控器断开重连后功能异常
2. **P2.4**: WiFi 重连计数器未重置 → 第一次失败后永久无法重连

### 强烈建议修复
3. **P1.3**: 内存分配逐步清理 → 提高代码严谨性
4. **P1.4**: 任务创建失败后状态一致性 → 防御极端情况

### 后续优化
5. **P2.1**: Opus 编码器复用 → 略微提升 PTT 响应速度
6. **P2.2**: 致命错误处理 → 增加自动重启
7. **P2.3**: WiFi connect 错误检查 → 日志完整性
8. **P2.5**: 任务创建失败清理 → 资源管理严谨性

---

## 建议的修复顺序

1. **立即修复**: P1.1 + P1.2 + P2.4 (网络重连相关，影响基本使用)
2. **第一轮验证后**: P1.3 + P1.4 (内存和状态一致性)
3. **后续迭代**: P2.1 ~ P2.5 (优化项)
