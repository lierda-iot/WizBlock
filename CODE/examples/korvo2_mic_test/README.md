<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Korvo2 MIC Test（ESP32-S3-Korvo-2 V3 麦克风对照测试）

ESP32-S3-Korvo-2 V3 开发板上的 ES7210 双麦克风 TDM slot 验证工具，用于对照确认 TDM 通道映射规律。

## 功能

- I2C 初始化 ES7210（ADC，地址 0x40）和 ES8311（DAC，地址 0x18）
- I2S TDM 4-slot 16-bit 采集
- 逐 slot 计算 RMS，确认哪些 slot 有信号、哪些为静默
- 日志输出 Slot0~Slot3 的 RMS 值

## 设计背景

本工程用于在 Korvo-2 V3 官方开发板上验证 ES7210 TDM slot 映射，对照确认本项目板的 slot 分配。

验证结论：Korvo-2 上 Slot0/Slot2 有 MIC 信号，Slot1/Slot3 为 0，与本项目板实测一致。由此确认 TDM 映射为：
- Slot0 → MIC1（ES7210 ADC1）
- Slot1 → Reference（ES8311 DAC 回采）
- Slot2 → MIC2（ES7210 ADC2）
- Slot3 → 未接（MIC3/4）

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh korvo2_mic_test flash
```

### 硬件连接

- ESP32-S3-Korvo-2 V3 开发板（非本项目主控板）
- MCLK=GPIO16, BCLK=GPIO9, WS=GPIO45, DIN=GPIO10, DOUT=GPIO8
- I2C：SDA=GPIO17, SCL=GPIO18

### 测试步骤与预期

1. 烧录到 Korvo-2 V3 开发板（注意：不是本项目主控板）
2. 串口日志打印 Slot0~Slot3 的 RMS 值
3. 对麦克风说话时，Slot0 和 Slot2 的 RMS 值明显增大；Slot1 和 Slot3 接近 0
4. 判断通过：4 个 slot 的 RMS 值正常输出，有信号 slot 与静默 slot 区分明确

## 硬件要求（Korvo-2 V3 专用）

- MCLK=GPIO16, BCLK=GPIO9, WS=GPIO45, DIN=GPIO10, DOUT=GPIO8
- I2C：SDA=GPIO17, SCL=GPIO18
- PA：GPIO48

## 注意

此工程仅用于 Korvo-2 V3 开发板，不适用于本项目正式硬件。引脚分配与本项目不同。
