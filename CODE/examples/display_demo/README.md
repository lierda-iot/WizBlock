<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Display Demo

ST7789V3 2.4寸 TFT LCD 显示演示，按白、红、绿、蓝顺序循环全屏填色。

## 功能

- SPI 初始化 + ST7789V3 驱动
- IOEX P1_5 LCD_RST 复位控制
- 背光 GPIO17 使能
- 每 2 秒切换到下一种全屏颜色，顺序为白、红、绿、蓝

## 测试方法

### 构建与烧录

```bash
# 在 CODE 目录执行

# Windows：先按根 `design.md` 4.1.2 用 Git Bash 完整镜像，再从仓库根执行
powershell.exe -ExecutionPolicy Bypass -File "./CODE/tools/build_example.ps1" -Example display_demo -Clean

# macOS：构建
bash ./tools/build_example_macos.sh display_demo

# Windows / Git Bash：烧录
./tools/build_example.sh display_demo flash

# macOS：烧录（`/dev/cu.usbserial-1410` 只是示意端口，需按客户实际端口修改）
bash ./tools/build_example_macos.sh display_demo flash -p /dev/cu.usbserial-1410
```

### 硬件连接

- ESP32-S3 核心板（A0）+ LCD 板（通过 CN3 连接）
- 确认背光 GPIO17、SPI 接口、IOEX LCD_RST 连接正常

### 测试步骤与预期

1. 烧录后 LCD 屏幕自动按白、红、绿、蓝顺序循环，每种颜色保持约2秒
2. 串口按相同顺序输出`fill: white`、`fill: red`、`fill: green`、`fill: blue`
3. 判断通过：四色顺序、保持时间和日志一致，且无panic、assert、看门狗或异常复位

当前验证边界：2026-06-12历史实机证据只覆盖白色和红色；2026-08-31当前四色源码clean build通过，完整四色实屏行为待复核。

## 设计要点

- 面板分辨率 240×320，SPI 单数据线
- 颜色格式 RGB565（驱动内部做 byte swap 适配 SPI LE 传输）
- 颜色反转使能（`INVON` 命令），匹配面板 IPS 工艺
- LCD_RST 由 IOEX 控制（非 ESP32 直连 GPIO）

## 硬件连接

- SPI：MOSI=GPIO13, SCK=GPIO12, CS=GPIO10
- DC（数据/命令选择）：GPIO16
- 背光：GPIO17（高电平亮）
- LCD_RST：IOEX P1_5（低电平复位）
- VDD_LCD：CN3 供电，来自 A0 板 VDD33

## 依赖组件

- `laiwfs300`（BSP）、`display_hal`、`bus_i2c`、`io_expander`
