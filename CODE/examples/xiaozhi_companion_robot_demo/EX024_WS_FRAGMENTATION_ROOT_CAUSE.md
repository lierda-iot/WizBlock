# EX-024 WebSocket 任务创建失败根因记录

**更新时间**：2026-08-11

## 问题现象

xiaozhi_companion_robot_demo 网络恢复后唤醒从 CONNECTING 状态切到 IDLE，但 WebSocket 会话无法建立。

## 已验证根因

基于 robotlog/555.txt 日志和 network_manager 源码逐行核对：

**LTE power cycle 触发闭源 `lsd_net_mgmt` 库的 USB CDC-ECM 链路重建，在 internal DMA RAM 产生不可逆碎片化。**

### 数据证据

- 首次 WebSocket 成功时：`internal_largest=8704` 字节
- 8 次 power cycle 后：`internal_largest=1920` 字节（锁定）
- WebSocket 任务栈需求：4096 字节
- 失败时 `internal_free=14391` 字节（总量充足）

**结论**：纯碎片化问题，非内存耗尽。

### 已排除因素

- ❌ WiFi 栈：4G-only 模式不调用 `esp_wifi_start()`
- ❌ network_manager 自身：power cycle 代码零堆分配
- ❌ power cycle 执行：仅操作 GPIO 电源脚
- ❌ mbedTLS：已迁移至 PSRAM

## 根本解决方案

**WebSocket 任务栈静态化**：

1. 使用 `StackType_t` 静态数组 + `StaticTask_t` 结构体
2. 调用 `xTaskCreateStatic()` 替代 `xTaskCreate()`
3. 启动时预留 4KB 栈空间在 `.bss` 段，不参与堆分配
4. 任务删除时栈不释放，终身复用

### 实施路径

Fork `managed_components/espressif__esp_websocket_client` 到 `CODE/components/esp_websocket_client_static`，修改任务创建逻辑。

## 当前状态

**⏸️ 待办（暂不执行）**

用户已于 2026-08-11 联系才福原厂确认：
- 闭源库是否存在 internal heap 碎片化问题
- 是否有官方修复方案

等待原厂反馈后决定执行路径。

## 相关文档

- [NETWORK_RECOVERY_MEMORY_ANALYSIS.md](NETWORK_RECOVERY_MEMORY_ANALYSIS.md) — 完整分析过程
- [WEBSOCKET_STATIC_STACK_SOLUTION.md](WEBSOCKET_STATIC_STACK_SOLUTION.md) — 方案技术细节
