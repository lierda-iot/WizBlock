# Korvo-2 V3 MIC Test 对照实验分析

日期：2026-07-06  
测试板：ESP32-S3-Korvo-2 V3（COM11）  
目的：通过已知双 MIC 正常的开发板验证 TDM slot 映射，判断本项目板 MIC2 是硬件故障还是 slot 映射错误

## 1. 测试结果

### mic_test TDM 4-slot 16-bit 模式（直接 i2s_channel_read）

| TDM Slot | 标注 | RMS 范围 | Peak | 结论 |
|----------|------|----------|------|------|
| Slot0 | MIC1 | 112~358 | 420 | 有信号 ✓ |
| Slot1 | MIC2 | 0 | 2 | 无信号 ✗ |
| Slot2 | MIC3 | 122~类似Slot0 | 406 | 有信号 ✓ |
| Slot3 | MIC4 | 0 | 2 | 无信号 ✗ |

### ES7210 寄存器状态（初始化后）

- REG 0x21 (SDP_TIMING) = 0x2A
- MIC1_GAIN (0x46) = 0x10, MIC2_GAIN (0x47) = 0x08
- MIC12_PDN (0x42) = 0x70
- ADC_MUTE (0x1B) = 0xBF

## 2. 参考项目对照：esp-skainet DOA (Korvo-2 V3)

路径：`参考资料/09_参考项目/双麦克风-声源定位参考代码/esp-skainet/`

### I2S 配置差异

| 项目 | I2S 模式 | 数据位宽 | Slot 模式 | 读取方式 |
|------|----------|----------|-----------|----------|
| mic_test | TDM 4-slot | 16-bit per slot | 4x16-bit | i2s_channel_read() |
| esp-skainet | STD Philips | 32-bit per channel | 2x32-bit stereo | esp_codec_dev_read() |

### esp-skainet 的通道映射

`bsp_get_input_format()` 返回 `"RMNM"`，含义：
- 位置 [0] = **R** (Reference) — ES8311 DAC 回采
- 位置 [1] = **M** (MIC1) — 第一个麦克风
- 位置 [2] = **N** (Noise/MIC3) — 噪声参考通道
- 位置 [3] = **M** (MIC2) — 第二个麦克风

DOA 算法通过 `get_doa_mic_positions()` 扫描格式字符串中 'M' 的位置：
- left_pos = 1 (MIC1)
- right_pos = 3 (MIC2)

### bsp_get_feed_data 通道重映射

```c
// 原始 4ch buffer (esp_codec_dev_read 返回):
//   [4*i+0]=Ref, [4*i+1]=MIC1, [4*i+2]=Noise, [4*i+3]=MIC2
// 重映射为 3ch (AFE 格式):
//   [3*i+0]=MIC1, [3*i+1]=MIC2, [3*i+2]=Ref
```

## 3. 关键发现：I2S 模式导致 slot 映射不同

ESP32-S3 I2S 外设在 STD 32-bit stereo 模式和 TDM 16-bit 4-slot 模式下，
对同一条 I2S 数据线的解析方式不同：

- **STD 32-bit stereo**：一个 I2S 帧 = 64 bit = Left(32bit) + Right(32bit)
  - 每个 32-bit word 在小端内存中被解释为 2 个 int16_t
  - 所以 4 个 int16_t 的排列顺序取决于字节序和 bit 对齐

- **TDM 16-bit 4-slot**：一个 I2S 帧 = 64 bit = Slot0(16bit) + Slot1(16bit) + Slot2(16bit) + Slot3(16bit)
  - 4 个 int16_t 直接对应 4 个时间槽

两种模式读取相同物理信号时，数组索引的含义不同。

## 4. Korvo-2 测试结果解读

Korvo-2 V3 只有 **2 个物理 MIC**（无 MIC3/MIC4）。测试中：
- 无播放音频 → ES8311 Reference 通道为 0
- MIC3/MIC4 未接 → 对应通道为 0

TDM 模式下 Slot0 和 Slot2 有信号，且 RMS 相近（两个 MIC 同环境收音），说明：
- **TDM Slot0 = MIC1**
- **TDM Slot2 = MIC2**
- **TDM Slot1 = Reference（未播放所以为 0）**
- **TDM Slot3 = MIC3/MIC4（未接所以为 0）**

## 5. 本项目板验证结果（2026-07-06 15:51）

在本项目板（COM7, MAC ae:7c）上重新运行 `audio_mic2_debug_demo`：

### Phase 3 TDM Slot 统计

| Slot | RMS | Peak | Min | Max |
|------|-----|------|-----|-----|
| Slot0 | **489** | 1415 | -1384 | 1415 |
| Slot1 | 1 | 4 | -4 | 4 |
| Slot2 | **500** | 1549 | -1432 | 1549 |
| Slot3 | 0 | 1 | -1 | 1 |

### 结论：MIC2 硬件完全正常

- **Slot0 = MIC1**（RMS=489）
- **Slot1 = ES8311 DAC 回采**（无播放 → RMS≈0）
- **Slot2 = MIC2**（RMS=500，略高于 MIC1）
- **Slot3 = MIC3/4**（未接 → RMS=0）

之前所有诊断代码（`audio_mic2_debug_demo`、`audio_dual_mic_doa_demo`）均从 `tdm_buf[i*4+1]`
取 MIC2 数据，实际该位置是 ES8311 回采通道（无播放时恒为 0），导致误判为 MIC2 硬件故障。

### 确认的 TDM Slot 映射（I2S TDM 16-bit 4-slot 模式）

```
Slot0 → ES7210 ADC1 → MIC1
Slot1 → ES8311 DAC Reference（回采）
Slot2 → ES7210 ADC2 → MIC2
Slot3 → ES7210 ADC3/4（未接）
```

与 esp-skainet 参考项目的 `esp_codec_dev_read()` 格式 "RMNM" 的对应关系：
- esp_codec_dev 返回: [0]=Ref, [1]=MIC1, [2]=Noise, [3]=MIC2
- TDM raw read 返回: [0]=MIC1, [1]=Ref, [2]=MIC2, [3]=MIC3/4

两种读取方式的通道顺序不同，是因为 esp_codec_dev 内部做了重排。

## 6. 下一步

1. 修正所有 demo 中 MIC2 的 slot 索引：从 `tdm_buf[i*4+1]` → `tdm_buf[i*4+2]`
2. 修正 DOA demo 中的双 MIC 提取逻辑
3. 重新验证双 MIC DOA 功能
