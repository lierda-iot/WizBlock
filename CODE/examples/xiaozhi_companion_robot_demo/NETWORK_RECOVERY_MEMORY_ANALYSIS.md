# 网络恢复后 WebSocket 任务创建失败根因分析

更新时间：2026-08-11

## 一、问题现象

**测试日志**：robotlog/555.txt（827KB, 13205行）

**症状**：
- SIM卡拔插与网络恢复功能正常（已验证）
- 网络恢复后唤醒触发，但系统从 CONNECTING 直接跳转 IDLE，无法建立 AI 会话
- 错误日志：`websocket_client: Error create websocket task`

**时间点**：
- 首次 WebSocket 创建成功：~50s
- 网络恢复后创建失败：~565s（经历8次 LTE power cycle）

## 二、内存快照对比

| 指标 | 首次成功 (~50s) | 网络恢复后失败 (~565s) | 变化 |
|------|----------------|----------------------|------|
| **internal_largest** | **8704 → 4864** | **1920** | **-78%（严重碎片化）** |
| internal_free | 10139 → 5483 | 14435 → 14391 | 总量反而更高 |
| internal_min | 9639 | 4899 | 历史最低降至 4899 |
| psram_free | ~6.9MB | ~6.9MB | 稳定 |
| ws_stack 需求 | 4096 | 4096 | 恒定 |
| 任务创建结果 | **ESP_OK** | **ESP_FAIL** | **失败：1920 < 4096** |

**关键发现**：
- `internal_free` 总量充足（14435 字节），但**最大连续块仅 1920 字节**
- WebSocket 任务栈需要 **4096 字节连续块**（`esp_websocket_client` 使用 `xTaskCreate`，只能从 internal heap 分配）
- **典型的内存碎片化，而非内存耗尽**

## 三、碎片化演进时间线

| 时间(s) | internal_largest | 事件 | 说明 |
|---------|------------------|------|------|
| ~48 | 8704 | 首次唤醒前 | 基线，0次 power cycle |
| ~50 | 8704 → 4864 | WS 创建成功 | 任务栈分配导致下降 |
| ~148 | **2688** | LTE cycle #1 后 | **首次大幅下降 -69%** |
| ~484 | **2048** | LTE cycle #5~#7 | 持续恶化 |
| ~494 | **1920** | LTE cycle #8 后 | **锁定，不再恢复** |
| ~565 | 1920 | WS 创建失败 | 1920 < 4096，无法分配 |

**LTE Power Cycle 统计**：
- 总次数：**8次**（初始启动 + 8次循环）
- 每次间隔：~26~41秒
- Network quality "F Bad" 警告：**546次**
- Network quality "CRITICAL" 触发：**542次**（net_mgmt 尝试切换接口）
- Network quality "B Good" 恢复：**16次**

**强相关性**：每次 LTE power cycle 后，`internal_largest` 不可逆衰减，且永不回升。

## 四、根因定位

### 4.1 已排除因素

经逐行核对 `network_manager` 组件全部生产源码（9个 .c 文件）：

1. **❌ WiFi 栈占用 DMA RAM**
   - 4G_ONLY 模式下 `esp_wifi_init()` 被调用，但 `esp_wifi_start()` 被隔离
   - `mode_uses_wifi(MODE_4G_ONLY)` 返回 false，`start_wifi_driver()` 早返回
   - `esp_wifi_init()` 仅分配静态控制缓冲（.bss），不分配 DMA-capable RAM
   - **结论**：WiFi 不是碎片化元凶

2. **❌ network_manager 组件自身分配**
   - 运行期**零堆分配**，无 malloc/calloc/heap_caps_malloc
   - 运行态数据全部为文件作用域静态变量（.bss）
   - 固定分配：命令队列 ~864B、dispatcher 栈 4KB、worker 栈 6KB（启动时一次性）
   - **结论**：组件自身不产生碎片化

3. **❌ LTE power cycle 代码路径**
   - `lte_hal_power_off/on` 仅操作 IOEX 电源脚
   - cellular_runtime_model 的 power cycle 流程内不做堆分配
   - NM-D-006 设计约束：**禁止 manager deinit/re-init**
   - **结论**：power cycle 代码本身不分配堆

4. **❌ mbedTLS 内存占用**
   - 已配置 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`，TLS 分配迁移到 PSRAM
   - **结论**：mbedTLS 不占用 internal heap

### 4.2 锁定真凶

**内存碎片化与 LTE power cycle 存在 1:1 强相关性**，但 network_manager 侧 power cycle 代码不分配堆。

**唯一在每次 power cycle 时重复分配/释放 internal DMA-capable RAM 的组件**：

👉 **闭源库 `lsd_net_mgmt` 内部的 USB CDC-ECM 链路重建过程**

**机制**：
1. LTE 断电（`lte_hal_power_off`）→ USB CDC-ECM 连接被 USB host stack 拆除
2. LTE 上电（`lte_hal_power_on`）→ USB 重新枚举 → CDC-ECM 设备注册 → lwIP netif 重建
3. 每次重建过程在 internal heap 分配 DMA-capable 缓冲区（USB 传输描述符、CDC-ECM 缓冲）
4. 释放时与其他长期存活对象（任务栈、静态缓冲）交错，产生**永久空洞**
5. 多次 power cycle 累积后，internal heap 呈现"瑞士奶酪"状态：总量充足但无大块连续区域

**为什么是 internal heap**：
- USB host stack 的 DMA 传输必须使用 **DMA-capable RAM**（ESP32-S3 上为 internal SRAM）
- PSRAM 不支持 DMA，USB/CDC-ECM 缓冲无法放 PSRAM
- `esp_websocket_client` 的任务栈通过 `xTaskCreate` 分配，**也只能从 internal heap 分配**

### 4.3 闭源库行为验证

从日志观察到的 `lsd_net_mgmt` 行为：
- 542 次 `Network quality CRITICAL! Triggering switch...`（net_mgmt 内部质量监控触发）
- 546 次 `network quality F Bad!`（net_mgmt 自主判定网络差）
- 8 次完整 LTE power cycle（由 network_manager cellular_runtime_model 驱动，响应 IPv4 丢失）

**net_mgmt 的活跃行为**（虽然我们不能修改其源码，但可以观察）：
- 持续 ping 探测外网可达性（每次 ping 需临时缓冲）
- 动态路由表更新（接口切换时申请/释放路由项）
- USB host 的 transfer 描述符池管理（每次 power cycle 重建）

## 五、为什么旧实现没有这个问题

用户确认：
> "在我使用这个组件之前，我的AI会话没有因为内存问题出过这个现象"

关键事实：
- Git 历史里的旧 `companion_network` **还未实现拔插恢复**（用户确认）
- 旧版本可能没有频繁 power cycle 机制，或恢复策略不同
- **新 network_manager 引入了主动 power cycle 恢复机制** → 触发 net_mgmt USB 重建 → 产生碎片化

## 六、问题严重性评估

**必现性**：
- 真实使用场景下，网络质量波动、信号弱、移动中断网都会触发 LTE power cycle
- 每次 power cycle 累积碎片化，**无法避免、无法恢复、单向递增**
- 达到临界点后（largest < 4096），所有后续 WebSocket 创建永久失败
- **不是偶发问题，是必然累积到达的系统性故障**

**影响范围**：
- 任何需要在网络恢复后创建新 WebSocket 会话的场景（AI 对话、OTA、云端连接）
- 首次创建成功后，后续恢复创建全部失败 → 产品功能永久失效
- 用户体验：拔卡重新插入可恢复网络，但 AI 对话彻底死亡，必须重启设备

## 七、错误日志原文（第1轮失败示例）

```
I (565793) xiaozhi_agent: [DEBUG-WSRAM] stage=before_init attempt=0 result=ESP_OK ws_stack=4096 internal_free=14435 internal_largest=1920 internal_min=4899 psram_free=6951676

I (565844) xiaozhi_agent: [DEBUG-WSRAM] stage=before_start attempt=1 result=ESP_OK ws_stack=4096 internal_free=14435 internal_largest=1920 internal_min=4899 psram_free=6951676

E (565875) websocket_client: Error create websocket task

I (565879) xiaozhi_agent: [DEBUG-WSRAM] stage=after_start attempt=1 result=ESP_FAIL ws_stack=4096 internal_free=14391 internal_largest=1920 internal_min=4899 psram_free=6950216

E (566008) websocket_client: Error create websocket task
I (566012) xiaozhi_agent: [DEBUG-WSRAM] stage=after_start attempt=2 result=ESP_FAIL ws_stack=4096 internal_free=14391 internal_largest=1920 internal_min=4899 psram_free=6950216

E (566142) websocket_client: Error create websocket task
I (566147) xiaozhi_agent: [DEBUG-WSRAM] stage=after_start attempt=3 result=ESP_FAIL ws_stack=4096 internal_free=14391 internal_largest=1920 internal_min=4899 psram_free=6950216

E (566169) xiaozhi_agent: WS connect failed
W (566170) xiaozhi_agent: open_session failed: ESP_FAIL
```

**失败时状态**：
- 3次重试全部因内存碎片化失败
- `internal_free=14391` 字节（总量充足）
- `internal_largest=1920` 字节（**连续块不足，1920 < 4096**）
- 网络质量同时报 `F Bad`，但根本原因是内存问题而非网络问题

## 八、技术约束

1. **闭源库不可修改**：`lsd_net_mgmt` 为才福预编译库，无源码，无法修改其 USB CDC-ECM 重建的分配策略
2. **DMA 限制**：USB host stack 必须使用 internal SRAM（DMA-capable），无法迁移到 PSRAM
3. **xTaskCreate 限制**：`esp_websocket_client` 使用普通 `xTaskCreate` 创建任务，栈+TCB 只能从 internal heap 分配，无法直接迁移到 PSRAM
4. **ESP-IDF 堆管理**：ESP-IDF 的 multi_heap 不支持在线碎片整理（defragmentation）
5. **真实场景必现**：网络质量波动 → power cycle 触发 → 碎片化累积，无法规避

## 九、相关文件路径

- 测试日志：`CODE/robotlog/555.txt`
- network_manager 组件：`CODE/components/network_manager/`
- xiaozhi_agent（WS 客户端）：`CODE/components/xiaozhi_agent/xiaozhi_agent.c`
- WebSocket 客户端：`managed_components/espressif__esp_websocket_client/`
- LTE HAL：`CODE/components/lte_hal/lte_hal.c`
- 闭源库：`CODE/components/net_mgmt/lib/libnet_mgmt_esp32s3_idf5_5_4.a`

## 十、已验证的设计事实

1. network_manager 组件按 NM-D-006 设计约束**禁止 manager deinit/re-init**（避免 ECM deinit abort）
2. 4G_ONLY 模式下**不启动 WiFi driver**（`start_wifi_driver()` 早返回）
3. cellular_runtime_model 的 power cycle 流程**仅操作电源脚，不分配堆**
4. LTE power cycle 恢复机制：disconnect_grace 5s → power_off → hold 1s → power_on（内部阻塞3s）→ recovery_ipv4_wait 20s
5. 重试预算：最多4次 power cycle，退避 5/10/20/30s 封顶，耗尽后 self-check 每120s自动重起一轮

---

**总结**：网络恢复后 WebSocket 任务创建失败的根本原因是 **LTE power cycle 触发闭源 net_mgmt 库的 USB CDC-ECM 重建，在 internal heap 产生不可逆碎片化**，最终导致无法分配 4KB 连续栈空间。这是真实场景下的必现问题，需要从根本上解决碎片化源头或规避对大块连续内存的依赖。
