# RC Tank Demo 第 1 轮调试记录

## 时间
2026-08-18 12:35

## 问题现象
Remote 固件触摸屏初始化失败，导致摇杆无法工作，设备进入 5 秒重启循环。

错误日志：
```
W (11572) touch_hal: CST836U not found at 0x15
E (11576) board_touch: board_laiwfs300_touch_init(18): touch_panel_init
E (11582) rc_control: Touch init failed: ESP_ERR_NOT_FOUND
E (11587) rc_tank: Joystick init failed: ESP_ERR_NOT_FOUND
E (11593) rc_tank: Role init failed (ESP_ERR_NOT_FOUND), restarting in 5s...
```

## 根因分析
对比历史成功日志（round10_remote_aligned.log），发现：
- **历史成功版本**：board init 完成后 **225ms** 才初始化触摸屏，触摸屏成功响应
- **当前失败版本**：board init 完成后 **5ms** 就尝试初始化触摸屏，触摸屏未就绪

原因：`touch_hal.c` 中触摸屏初始化逻辑在执行 TP_RST 硬件复位**之前**先 probe I2C，导致触摸屏未就绪时直接失败。

## 修复方案
修改 `CODE/components/touch_hal/touch_hal.c` 的 `touch_panel_init()` 函数：

1. **调整初始化顺序**：先执行 TP_RST 硬件复位，再 probe I2C 设备
2. **增加复位时序延迟**：
   - 复位低电平保持：20ms → 50ms
   - 复位后等待稳定：200ms → 300ms

修改后代码：
```c
esp_err_t touch_panel_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bus_i2c_master_bus();
    ESP_RETURN_ON_FALSE(NULL != bus, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    /* Reset touch via IOEX TP_RST (active low) BEFORE probing */
    io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_TP_RST_PORT, BOARD_LAIWFS300_IOEX_TP_RST_PIN, true);
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_TP_RST_PORT, BOARD_LAIWFS300_IOEX_TP_RST_PIN, false);
    vTaskDelay(pdMS_TO_TICKS(50));   // 复位低电平 50ms
    io_expander_write_pin(BOARD_LAIWFS300_IOEX_TP_RST_PORT, BOARD_LAIWFS300_IOEX_TP_RST_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(300));  // 复位后等待 300ms

    /* Probe after reset */
    esp_err_t ret = i2c_master_probe(bus, BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT, 100);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "CST836U not found at 0x%02X after reset", BOARD_LAIWFS300_CST836U_I2C_ADDR_7BIT);
        return ESP_ERR_NOT_FOUND;
    }
    
    // ... 后续代码不变
}
```

## 构建问题
修改代码后尝试重新构建 Remote 固件，遇到问题：
1. 从 Git Bash 调用 PowerShell/CMD 执行 idf.py 时，ESP-IDF 检测到 MSys/Mingw 环境并拒绝执行
2. 增量构建可能使用了缓存，touch_hal.c 的修改未生效

## 待执行步骤
需要在**纯 CMD 窗口**（不通过 Bash）执行以下步骤：

1. 进入目录：
   ```cmd
   cd /d e:\10__AIProject\7_AI陪伴机器人\CODE\examples\rc_tank_demo
   ```

2. 执行构建脚本：
   ```cmd
   rebuild_remote_clean.bat
   ```

3. 构建成功后，烧录固件：
   ```cmd
   flash_rc_tank.ps1 -Port COM7 -Role REMOTE
   ```

4. 抓取日志验证触摸屏是否初始化成功

## 其他修复（已完成）
- ✅ P0 屏幕撕裂：rc_video.c 添加 DMA 完成检查
- ✅ P1 摇杆方向/位置/尺寸：rc_joystick.c/h 修正
- ✅ P1 Tank 图标乱码：rc_tank_screen.c 修正字体位序和字节序
- ✅ P2 UDP 传输：rc_net.c 替换 TCP 为 UDP
- ✅ Tank 固件：已成功构建并烧录，所有模块正常初始化

## 状态
当前轮次：第 1 轮（授权最多 10 轮）
阻塞原因：需要用户在纯 CMD 环境执行构建脚本
