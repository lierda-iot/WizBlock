<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# LED Demo

板载 RGB LED 循环切换颜色和亮灭状态，每个状态保持 `500ms`。

## 功能

- 使用 `GPIO46 / RGB_PWM` 驱动板载可寻址 RGB LED
- 循环切换 `off / red / green / blue / yellow / cyan / magenta / white`
- 每次状态切换都会在日志中输出当前颜色

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh led_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0），板载 RGB LED 已集成
- RGB_PWM：GPIO46

### 测试步骤与预期

1. 烧录后板载 RGB LED 按 500ms 周期循环变色（红→绿→蓝→白→灭）
2. 串口日志输出当前颜色名称
3. 判断通过：肉眼观察颜色循环正确，各颜色可区分

## 设计要点

- RGB LED 为 WS2812 类型单线协议驱动（DIN/DOUT 级联）
- A0 板 `RGB_PWM`(GPIO46) → LED `DIN`，LED `DOUT` → `LED_OUT` 引出到 CN8
- 当前单颗 LED，后续可扩展 `LED_COUNT` 支持更多

## 硬件连接

- RGB_PWM：GPIO46
- LED 供电：VDD33

## 实机验证结果（2026-07-06）

RGB LED 循环变色正常，8 种状态全部可见。

## 构建

```bash
cd CODE
./tools/build_example.sh led_demo
```
