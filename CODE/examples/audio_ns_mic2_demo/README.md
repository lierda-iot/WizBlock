<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio NS MIC2 Demo（MIC2 通道独立降噪测试）

MIC2 通道独立降噪测试例程，验证 MIC2（ES7210 ADC2）通过 NSNet2 降噪的效果。

## 功能

- I2S TDM 4-slot 原始读取
- 从 Slot2 提取 MIC2 数据
- ESP AFE NSNet2 降噪处理
- 录音 10s → 播放原始 → 播放降噪后对比

## 设计要点

- MIC2 位于 TDM Slot2（非 Slot1），需使用 `read_tdm_4ch()` + 提取 channel 2
- NS 模型：NSNet2（`CONFIG_SR_NSN_NSNET2=y`），比 WebRTC NS 效果更好
- 此 demo 依赖 MIC2 硬件正常工作

## TDM Slot 映射

| Slot | 来源 |
| --- | --- |
| Slot0 | MIC1（ES7210 ADC1） |
| Slot1 | Reference（ES8311 DAC 回采） |
| Slot2 | MIC2（ES7210 ADC2） |
| Slot3 | 未接 |

## 硬件要求

- I2S：MCLK=GPIO42, BCLK=GPIO41, WS=GPIO39, DOUT=GPIO38, DIN=GPIO40
- ES7210 TDM 4通道
- MIC2（B0 板第二颗麦克风）正常连接
- AMP_CTRL：IOEX P1_0

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh audio_ns_mic2_demo flash
```

### 硬件连接

- 主控板 + 喇叭（HT6872 功放已板载）+ MIC 板（B0 板，含双麦）
- 串口 COM7 用于日志观察

### 测试步骤与预期

1. 烧录后等待初始化完成
2. 听到提示音后开始录音 10 秒（使用 MIC2 通道，即 TDM Slot2）
3. 期间制造背景噪声和说话声
4. 先播放原始录音，再播放 NSNet2 降噪后录音
5. 通过标准：降噪后背景噪声明显减小，语音清晰，确认 MIC2 通道独立降噪功能正常

## 依赖组件

- `laiwfs300`（BSP）、`audio_processor`（ESP AFE + NSNet2）、`bus_i2c`、`io_expander`
