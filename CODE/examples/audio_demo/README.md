<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio Demo

ES8311 DAC 播放 + ES7210 双麦克风采集演示（录放测试）。

## 功能

- I2S 初始化：TX=STD(STEREO)，RX=TDM(4-slot)，16kHz，MCLK_MULTIPLE_256
- 录放测试：嘀声(1kHz) → 录音 10s → 嘀声(1.5kHz) → 播放 10s
- 使用 `esp_codec_dev` 管理组件（`es8311_codec_new` / `es7210_codec_new`）

## 设计要点

- 音频驱动使用 `espressif/esp_codec_dev` ~1.5.6，通过 `es8311_codec_new()` / `es7210_codec_new()` 创建 codec 接口，不手动写寄存器
- I2S TX 使用 STD 模式（STEREO），RX 使用 TDM 模式（4-slot），MCLK=256×16kHz=4.096MHz
- ES8311 负责 DAC 播放，ES7210 负责双 MIC 采集，共享同一 I2S 总线
- `audio_codec_i2c_cfg_t.addr` 使用 8-bit 地址格式：ES8311=0x30，ES7210=0x80（内部执行 `addr >> 1` 得到 7-bit 地址）
- AMP 使能通过 IOEX P1_0，播放前开启、播放后关闭，避免 pop 噪声
- DMA 配置：6 descriptors × 240 frames

## TDM Slot 映射

| Slot | 来源 | 说明 |
| --- | --- | --- |
| Slot0 | MIC1（ES7210 ADC1） | 物理麦克风 1 |
| Slot1 | MIC3 / ES8311 DAC Reference | AEC 硬件回采 |
| Slot2 | MIC2（ES7210 ADC2） | 物理麦克风 2 |
| Slot3 | MIC4（未接） | 无信号 |

## 硬件连接

- I2S：MCLK=GPIO42, BCLK=GPIO41, WS=GPIO39, DOUT=GPIO38, DIN=GPIO40
- ES8311 I2C 地址：0x18（7-bit）/ 0x30（8-bit）
- ES7210 I2C 地址：0x40（7-bit）/ 0x80（8-bit）
- AMP_CTRL：IOEX P1_0（高电平使能 HT6872 功放）

## 实机验证（2026-06-26）

init OK，ES8311/ES7210 Slave TDM 正常初始化；loopback peak=32767（MIC→ADC→I2S→DAC→AMP→扬声器全链路贯通）。

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh audio_demo flash
```

### 硬件连接

- 主控板 + 喇叭（HT6872 功放已板载）
- 串口 COM7 用于日志观察

### 测试步骤与预期

1. 烧录后等待初始化完成
2. 听到 1kHz 嘀声表示开始录音，录制 10 秒
3. 听到 1.5kHz 嘀声表示开始播放录音
4. 日志显示录制/播放进度（采样点偏移量）
5. 通过标准：能从喇叭听到刚才录制的声音回放，日志显示正常的录制和播放流程

## 依赖组件

- `laiwfs300`（BSP：I2S、I2C、IO 扩展器、音频子系统）
- `espressif/esp_codec_dev`、`io_expander`、`bus_i2c`

## 构建

```bash
cd CODE
./tools/build_example.sh audio_demo
```
