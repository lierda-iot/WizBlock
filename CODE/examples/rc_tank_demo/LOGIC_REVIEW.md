# RC Tank Demo 固件逻辑审查报告

**审查时间**: 2026-08-17  
**审查范围**: 坦克和遥控器固件的纯 C 逻辑（不涉及硬件测试）  
**审查目标**: 识别可能导致崩溃、死锁、内存泄漏、资源竞争的代码缺陷

---

## 一、高风险问题（P0 - 必须修复）

### 1.1 WiFi 扫描错误处理缺失（遥控器）

**文件**: `rc_net.c:228`  
**代码**:
```c
ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
```

**问题**:
- `ESP_ERROR_CHECK` 宏在错误时调用 `abort()`，导致设备重启
- 如果 100ms 延迟不够，仍可能返回 `ESP_ERR_WIFI_STATE`
- 当前已添加延迟但未移除 `ESP_ERROR_CHECK`，一旦失败会进入无限重启循环

**修复建议**:
```c
esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(ret));
    free(ap_records);
    return ret;
}
```

**影响**: 遥控器无法启动，持续重启

---

### 1.2 TCP accept 永久阻塞（坦克）

**文件**: `rc_net.c:433-444` (accept_audio_client), `rc_net.c:407-418` (accept_video_client)  
**代码**:
```c
static int accept_audio_client(void)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    s_audio_client_sock = accept(s_audio_sock, (struct sockaddr *)&client_addr, &addr_len);
    // ... 阻塞直到客户端连接
}
```

**问题**:
- `accept()` 默认阻塞模式，如果遥控器未连接会永久卡住
- 被调用路径:
  - `rc_net_video_send()` → `accept_video_client()` (video_tx_task)
  - `rc_net_audio_recv()` → `accept_audio_client()` (audio_rx_task)
- 坦克的视频/音频任务会在遥控器未连接时永久阻塞

**修复建议**:
```c
// 在创建 listen socket 后设置接收超时
struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
setsockopt(s_video_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
setsockopt(s_audio_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

// 修改 accept_xxx_client() 返回值处理，超时返回 ESP_ERR_TIMEOUT
static int accept_video_client(void)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    s_video_client_sock = accept(s_video_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (s_video_client_sock < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 超时，正常情况
            return -1;
        }
        ESP_LOGE(TAG, "Video accept failed: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Video client connected");
    }
    return s_video_client_sock;
}
```

**影响**: 坦克视频/音频功能卡死，遥控器未连接时完全无响应

---

### 1.3 任务创建失败后的资源泄漏（坦克音频）

**文件**: `rc_audio.c:254-268`  
**代码**:
```c
esp_err_t ret = opus_codec_decoder_init(&dec_cfg);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Opus decoder init failed: %s", esp_err_to_name(ret));
    vTaskDelete(NULL);  // 删除当前任务
    return;
}

int16_t *pcm_buf = heap_caps_malloc(pcm_buf_size, MALLOC_CAP_SPIRAM);
if (!pcm_buf) {
    ESP_LOGE(TAG, "PCM buffer alloc failed");
    opus_codec_deinit();  // 正确清理
    vTaskDelete(NULL);
    return;
}
```

**问题**:
- `audio_play_task` 删除后，`s_audio_queue` 仍然存在
- `audio_rx_task` 会持续向队列投递数据（Line 366: `xQueueSend(s_audio_queue, ...)`)
- 队列满后会丢弃数据并泄漏内存（Line 368: `free(pkt_copy)`）
- 没有机制通知 `audio_rx_task` 停止工作

**修复建议**:
```c
// 方案1: 增加全局错误标志
static volatile bool s_audio_play_error = false;

// audio_play_task 失败时设置标志
if (ret != ESP_OK) {
    s_audio_play_error = true;
    opus_codec_deinit();
    vTaskDelete(NULL);
    return;
}

// audio_rx_task 检查标志
while (1) {
    if (s_audio_play_error) {
        ESP_LOGE(TAG, "Play task failed, stopping RX");
        break;
    }
    // ... 原有逻辑
}

// 方案2: 任务创建失败时清理队列
esp_err_t rc_audio_play_start(void)
{
    BaseType_t r1 = xTaskCreate(audio_rx_task, "audio_rx", 4096, NULL, ...);
    BaseType_t r2 = xTaskCreate(audio_play_task, "audio_play", 4096, NULL, ...);
    if (r1 != pdPASS || r2 != pdPASS) {
        if (s_audio_queue) {
            vQueueDelete(s_audio_queue);
            s_audio_queue = NULL;
        }
        // 删除已创建的任务（需要保存任务句柄）
        return ESP_FAIL;
    }
    return ESP_OK;
}
```

**影响**: 
- 坦克音频播放失败但接收任务仍运行
- 持续分配/释放内存，可能导致 PSRAM 碎片化
- 队列积压导致音频延迟

---

## 二、中风险问题（P1 - 强烈建议修复）

### 2.1 内存分配失败的部分清理（遥控器录音）

**文件**: `rc_audio.c:106-121`  
**代码**:
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
- 如果 `opus_buf` 分配失败，`pcm_buf` 已分配但逻辑上未使用
- 虽然 `free(NULL)` 是安全的，但编码风格不一致
- `opus_codec_encoder_init()` 已成功（Line 100），但在所有分支中都会调用 `opus_codec_deinit()`（Line 167），逻辑正确

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

**影响**: 内存分配失败时可能有几百字节泄漏（低概率）

---

### 2.2 控制任务创建失败的不完整清理（坦克）

**文件**: `rc_control.c:123-133`  
**代码**:
```c
esp_err_t rc_control_start_tank(void)
{
    s_last_ctrl_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    BaseType_t ret = xTaskCreate(ctrl_rx_task, "ctrl_rx", 3072, NULL,
                                  configMAX_PRIORITIES - 2, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ctrl_rx task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tank control started");
    return ESP_OK;
}
```

**问题**:
- `s_last_ctrl_ms` 已初始化，但任务创建失败
- 如果上层逻辑依赖该变量判断控制状态，可能误判

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

**影响**: 任务创建失败时状态不一致（低概率）

---

### 2.3 队列溢出时的内存泄漏防护已存在（坦克音频）

**文件**: `rc_audio.c:366-369`  
**代码**:
```c
if (xQueueSend(s_audio_queue, &pkt, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Audio queue full, dropping packet");
    free(pkt_copy);
}
```

**状态**: ✅ **已正确处理**  
- 队列满时正确释放了 `pkt_copy`
- 无内存泄漏

---

## 三、低风险问题（P2 - 建议优化）

### 3.1 Opus 编码器重复初始化（遥控器）

**文件**: `rc_audio.c:100-104, 167`  
**代码**:
```c
esp_err_t rc_audio_record_and_send(void)
{
    // 每次按下 SW3 都初始化编码器
    opus_encoder_config_t enc_cfg = { ... };
    opus_codec_encoder_init(&enc_cfg);
    
    // ... 录音逻辑
    
    opus_codec_deinit();  // 每次松开 SW3 都销毁
}
```

**问题**:
- 每次 PTT 都重新初始化 Opus 编码器，效率较低
- 如果用户频繁按下/松开 SW3，会增加 CPU 开销

**优化建议**:
```c
// 方案1: 在 rc_audio_record_init() 中初始化一次
static opus_encoder_config_t s_enc_cfg;
static bool s_encoder_ready = false;

esp_err_t rc_audio_record_init(void)
{
    // ... 原有初始化
    
    s_enc_cfg = (opus_encoder_config_t){
        .sample_rate = RC_AUDIO_SAMPLE_RATE,
        .channels = 1,
        .frame_duration_ms = OPUS_FRAME_MS,
        .bitrate = 16000,
        .complexity = 5,
        .enable_vbr = true,
        .enable_dtx = false,
    };
    
    esp_err_t ret = opus_codec_encoder_init(&s_enc_cfg);
    if (ret == ESP_OK) {
        s_encoder_ready = true;
    }
    return ret;
}

esp_err_t rc_audio_record_and_send(void)
{
    if (!s_encoder_ready) {
        ESP_LOGE(TAG, "Encoder not ready");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 直接使用已初始化的编码器
    // ... 录音逻辑，不调用 opus_codec_deinit()
}

// 方案2: 保持现状（更简单，但效率略低）
// 优点: 每次录音都是干净状态，不会有编码器状态残留
```

**影响**: 略微增加录音启动延迟（约 10ms）

---

### 3.2 app_main.c 中的致命错误 return 语句

**文件**: `app_main.c:70, 77, 103, 111, 126, 133, 152, 161`  
**代码**:
```c
static void rc_tank_role_run(void)
{
    esp_err_t ret = board_laiwfs300_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(ret));
        return;  // 返回到 app_main，进入无限循环
    }
    // ...
}
```

**问题**:
- 部分致命错误（板级初始化、网络初始化）失败后 `return`
- 返回到 `app_main()` 的无限循环（Line 211-214）
- 设备看似运行但实际无功能

**优化建议**:
```c
// 方案1: 记录错误并重启
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Board init failed, restarting in 5s...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}

// 方案2: 进入错误指示循环（闪烁 LED）
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Board init failed, entering error loop");
    while (1) {
        // 红色 LED 闪烁
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

**影响**: 错误状态下设备无明确行为，难以排查

---

## 四、已正确实现的部分 ✅

### 4.1 网络层 ESP_ERR_INVALID_STATE 处理
- `rc_control.c:106-109` 正确处理 socket 未就绪情况
- `rc_net.c:449` 正确返回 `ESP_ERR_INVALID_STATE`

### 4.2 WiFi 启动延迟
- `rc_net.c:216` 添加 100ms 延迟避免 `ESP_ERR_WIFI_STATE`（但后续 `ESP_ERROR_CHECK` 仍需修复）

### 4.3 音频队列溢出处理
- `rc_audio.c:366-369` 正确释放被丢弃的音频包

### 4.4 非致命错误的容错处理
- `app_main.c:83, 90, 96, 140, 146` 摄像头/显示/音频初始化失败后继续执行

---

## 五、修复优先级建议

### 立即修复（烧录前）
1. ✅ **1.1 WiFi 扫描错误处理** - 将 `ESP_ERROR_CHECK` 改为 `if (ret != ESP_OK) return`
2. ✅ **1.2 TCP accept 阻塞** - 设置 socket 超时或非阻塞模式

### 第一轮验证后修复
3. **1.3 音频任务资源泄漏** - 增加任务间错误通知机制
4. **2.1 内存分配逐步清理** - 改为逐个分配+清理

### 后续优化（不影响基本功能）
5. **2.2 控制任务创建顺序** - 先创建任务再初始化状态
6. **3.1 Opus 编码器复用** - 移到 init 阶段初始化
7. **3.2 致命错误处理** - 增加重启或错误指示

---

## 六、验证建议

### 6.1 单元测试覆盖
- [ ] `rc_net_ctrl_recv()` 超时/未就绪/正常接收
- [ ] `rc_audio_record_and_send()` 内存分配失败路径
- [ ] 音频队列满时的丢包逻辑

### 6.2 集成测试场景
- [ ] 遥控器先启动（坦克未上电） → accept 超时处理
- [ ] 遥控器 WiFi 扫描失败重试（100ms 延迟不够）
- [ ] 坦克音频播放任务崩溃后，接收任务行为
- [ ] 频繁按下/松开 SW3（Opus 编码器重复初始化）

### 6.3 压力测试
- [ ] 连续录音 10 次，检查 PSRAM 碎片化
- [ ] 音频队列满时持续发送，检查内存泄漏
- [ ] 遥控器反复连接/断开，检查 socket 状态

---

## 七、代码审查总结

### 整体评价
- ✅ **网络层时序处理**: 已添加必要延迟和状态检查
- ⚠️ **错误处理策略**: 部分使用 `ESP_ERROR_CHECK`（致命），部分使用 `continue`（容错），不一致
- ⚠️ **资源管理**: 内存分配/释放逻辑基本正确，但缺少异常路径的完整清理
- ❌ **阻塞调用**: TCP accept 未设置超时，会导致任务永久卡死

### 关键风险
1. **遥控器重启循环**: WiFi 扫描失败 → `ESP_ERROR_CHECK` → abort → 重启
2. **坦克视频/音频卡死**: accept 阻塞 → 任务无法继续
3. **音频任务孤岛**: 播放任务崩溃，接收任务继续运行

### 建议修复顺序
1. 先修复 **1.1** 和 **1.2**（阻塞问题）
2. 烧录测试，观察日志中是否有 `ESP_ERROR_CHECK failed` 或任务卡死
3. 根据实际情况决定是否修复 **1.3**（音频任务协同）
