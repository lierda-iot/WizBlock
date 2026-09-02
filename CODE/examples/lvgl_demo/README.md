<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# LVGL Demo（GUI 图形界面演示）

LVGL v8.4.0 图形界面演示，验证 LCD 显示 + 触摸交互的完整 GUI 链路。

## 功能

- LVGL v8.4.0 手动集成（非 esp_lvgl_port）
- ST7789V3 横屏显示（320×240，硬件旋转）
- CST836U 触摸输入（坐标映射到横屏逻辑坐标）
- NXP GUI Guider 生成的 UI 界面（Button + Slider + 文本）
- 双缓冲 DMA 刷新

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh lvgl_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0）+ LCD/TP 板（含触摸 IC）
- ST7789V3：SPI 接口
- CST836U 触摸：I2C 地址 0x15

### 测试步骤与预期

1. 烧录后 LCD 显示 LVGL 界面（深灰背景、白色文字、蓝色按钮）
2. 触摸屏幕上的按钮，按钮有视觉响应（按压效果）
3. 串口日志输出触摸事件和 LVGL 刷新信息
4. 判断通过：界面正确显示且触摸交互有响应

## 设计要点

- 横屏旋转方案：通过 `display_hal_set_orientation()` 设置 ST7789V3 MADCTL 寄存器实现硬件旋转，无性能损耗
- flush_cb 执行 RGB→BGR 字节交换（匹配 SPI LE + BGR 模式）
- 触摸坐标映射：`lv_x = 319 - phy_y`, `lv_y = phy_x`
- LVGL tick 由 `esp_timer` 2ms 周期驱动
- LVGL task 运行在 Core1，8KB 栈

## 资源占用

| 资源 | 实测 |
| --- | --- |
| Flash（含 UI + 字体） | 515KB |
| RAM (draw buffer) | ~25.6KB × 2 双缓冲 |
| RAM (LVGL 内部) | ~32KB |
| Task 栈 | 8KB |

## 硬件要求

- ESP32-S3 核心板（A0）+ LCD/TP 板
- ST7789V3：SPI（MOSI=GPIO13, SCK=GPIO12, CS=GPIO10, DC=GPIO16）
- 背光：GPIO17
- LCD_RST：IOEX P1_5
- CST836U 触摸：I2C 地址 0x15（SCL=GPIO47, SDA=GPIO48）

## 依赖组件

- `laiwfs300`（BSP）、`display_hal`、`touch_hal`、`bus_i2c`、`io_expander`
- LVGL v8.4.0（手动集成，源码在 `components/lvgl/`）
