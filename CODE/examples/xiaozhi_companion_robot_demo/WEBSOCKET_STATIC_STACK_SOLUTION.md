# WebSocket 任务栈静态化方案（待办）

更新时间：2026-08-11

## 状态

**待执行** — 等待原厂确认闭源 net_mgmt 库的 internal heap 碎片化问题后再决策。

## 问题背景

详见 `NETWORK_RECOVERY_MEMORY_ANALYSIS.md`。

**根因**：LTE power cycle 触发闭源 `lsd_net_mgmt` 的 USB CDC-ECM 重建，在 internal heap 产生不可逆碎片化，最终导致 WebSocket 任务栈（4096 字节）无法分配。

**数据**：
- 首次成功：`internal_largest=8704`
- 8次 power cycle 后：`internal_largest=1920 < ws_stack=4096` → 任务创建失败
- 真实场景必现：网络质量波动 → power cycle 累积 → 碎片化达临界 → AI 会话永久失效

## 根本解决方案

**核心思路**：WebSocket 任务栈在系统启动时（internal heap 未碎片化）一次性静态预留，后续所有会话复用，不再走 `xTaskCreate` 的动态分配。

### 技术实现

#### 1. 添加静态栈缓冲（`xiaozhi_agent.c`）

```c
// 文件作用域静态变量
static StackType_t s_ws_task_stack[WS_TASK_STACK_SIZE / sizeof(StackType_t)] 
    __attribute__((aligned(8)));
static StaticTask_t s_ws_task_buffer;
static bool s_ws_task_preallocated = false;
```

#### 2. 修改任务创建（Fork `esp_websocket_client`）

**实施路径A（推荐）**：
```bash
# 1. 复制 managed component 到项目组件
cp -r managed_components/espressif__esp_websocket_client \
      CODE/components/esp_websocket_client_static

# 2. 修改 xiaozhi_agent/CMakeLists.txt 依赖本地版本
# 3. 移除 idf_component.yml 中的 esp_websocket_client 依赖
```

**代码修改**（`esp_websocket_client.c:1081` 附近）：
```c
// 原代码：
xTaskCreate(esp_websocket_client_task, 
            client->config->task_name, 
            client->config->task_stack, 
            client, 
            client->config->task_prio, 
            &client->task_handle)

// 改为：
if (s_ws_task_preallocated) {
    client->task_handle = xTaskCreateStatic(
        esp_websocket_client_task,
        client->config->task_name,
        WS_TASK_STACK_SIZE / sizeof(StackType_t),
        client,
        client->config->task_prio,
        s_ws_task_stack,      // 使用预留栈
        &s_ws_task_buffer     // 静态 TCB
    );
    return (client->task_handle != NULL) ? ESP_OK : ESP_FAIL;
} else {
    s_ws_task_preallocated = true;
    // 继续原 xTaskCreate 路径...
}
```

#### 3. 任务删除时保留栈（`esp_websocket_client_destroy()`）

```c
if (client->task_handle) {
    vTaskDelete(client->task_handle);
    client->task_handle = NULL;
    // s_ws_task_preallocated 保持 true，栈空间不释放
}
```

### 关键特性

1. **静态栈在 .bss 段**：链接时分配，不走堆，不参与碎片化
2. **一次性预留，终身复用**：首次成功后栈空间永久保留
3. **任务可删除重建，栈不释放**：网络恢复后 `xTaskCreateStatic` 复用同一块栈
4. **TCB 也静态化**：避免 TCB（~200字节）的堆分配

### 资源成本

- **.bss 增加**：4096 字节（WS_TASK_STACK_SIZE）
- **优势**：WebSocket 创建永不受 internal heap 碎片化影响
- **权衡**：牺牲 4KB 静态内存，换取功能在真实场景下的稳定性

### 为什么这是根本解决

1. ✅ **切断碎片化传导**：WebSocket 栈不再从碎片化 heap 分配
2. ✅ **零运行时成本**：静态栈在链接时分配，无分配/释放开销
3. ✅ **永久有效**：不受后续 power cycle 次数影响
4. ✅ **不改闭源库**：net_mgmt 继续碎片化其区域，但不影响 WebSocket
5. ✅ **真实场景适用**：网络质量差、频繁 power cycle 下依然稳定

## 验证方法

1. Clean build，确认 `.bss` 增加约 4KB
2. COM7 全片擦除 + 五段烧录
3. **复现测试**：
   - 插 SIM 卡，等待首次 WebSocket 创建成功
   - 拔卡，等待多次 LTE power cycle（观察 `internal_largest` 下降）
   - 重新插卡，网络恢复后唤醒
   - **预期**：WebSocket 任务创建成功（使用静态栈）
4. **长期稳定性**：连续拔插 10+ 次，每次恢复后 AI 会话正常

## 待原厂确认事项

1. **net_mgmt USB CDC-ECM 重建是否确实在 internal heap 产生碎片化**
2. **是否有官方修复计划**（更新 net_mgmt 库版本）
3. **是否有其他推荐的规避方案**（如调整 USB host stack 配置）

## 后续决策

根据原厂反馈：
- **如原厂确认问题且无官方修复**：立即实施本方案
- **如原厂提供库更新或配置调整**：评估后择优
- **如原厂否认问题**：补充更多证据或寻找其他碎片化源

## 相关文档

- 完整根因分析：`NETWORK_RECOVERY_MEMORY_ANALYSIS.md`
- 测试日志：`CODE/robotlog/555.txt`
- 项目记忆：`.claude/memory/4g-only-wifi-not-fragmentation-cause.md`
