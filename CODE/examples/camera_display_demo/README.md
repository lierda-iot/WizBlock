<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Camera Display Demo（摄像头实时预览）

SP0A39 摄像头 + ST7789V3 LCD 实时预览演示，验证 DVP 采集到 LCD 显示的完整链路。

## 功能

- SP0A39 SCCB/I2C 初始化、寄存器表写入
- DVP 8-bit 并口采集（640×480 VYUY）
- 按块 YUV422→RGB565 颜色转换
- ST7789V3 SPI 全屏显示（240×320）
- 多帧缓冲 + DMA 友好分块传输
- 串口日志输出 capture fps / display fps / 转换耗时

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh camera_display_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0）+ C0 扩展板 + E0 摄像头板 + LCD 板
- C0 板 20MHz 晶振正常工作
- DVP 数据线、I2C 总线、SPI 接口均通过 C0 连接

### 测试步骤与预期

1. 烧录后系统自动初始化摄像头和 LCD
2. LCD 显示摄像头实时画面（240×320 全屏预览）
3. 串口日志持续输出 `capture fps` 和 `display fps`
4. 判断通过：LCD 有实时画面显示且无 crash，日志中 fps 数值持续更新

## 设计要点

- 像素格式：VYUY（传感器实际输出字节序，非 YUYV）
- 外部 MCLK：C0 扩展板本地 20MHz 晶振，非 ESP32 LEDC 输出
- DVP 驱动使用 `esp_cam_ctlr_dvp` 纯回调模式（`bk_buffer_dis=1`，不使用 `receive()` 接口）
- LCD 分块传输：每次发送 80 行，发送后等待 `display_hal_wait_pending()` 完成再发下一块
- 摄像头 RESET/PWDN 通过 IOEX（TPT29555A P0_5/P0_7）控制

## 稳定基线参数

| 参数 | 值 |
| --- | --- |
| pixfmt | VYUY |
| 预览分辨率 | 240×320 全屏 |
| LCD pixel clock | 20 MHz |
| chunk lines | 80 |
| 等待传输完成 | 是 |
| capture fps | ~25.2 |
| display fps | ~8.3 |
| avg_convert | ~55 ms |
| avg_draw | ~64 ms |

## 硬件要求

- ESP32-S3 核心板（A0）+ C0 扩展板 + E0 摄像头板
- C0 板 20MHz 晶振正常工作
- DVP 数据线 D0~D7、PCLK、VSYNC、HSYNC 经 C0 连接
- I2C 总线：SCL=GPIO47, SDA=GPIO48
- IOEX：TPT29555A 地址 0x22

## 依赖组件

- `laiwfs300`（BSP）、`camera_hal`、`display_hal`、`esp_driver_cam`
