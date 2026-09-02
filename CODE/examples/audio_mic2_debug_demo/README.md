<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio MIC2 Debug Demo

ES7210 双麦克风 TDM slot 诊断工具。用于验证 MIC1/MIC2 各 slot 信号状态。

## 用途

- 读取 ES7210 关键寄存器（PGA、PDN、mute、clock 等）
- 直接抓取 4-slot TDM 原始数据
- 逐 slot 计算 RMS 统计值
- 对比 Slot0(MIC1)、Slot1(REF)、Slot2(MIC2)、Slot3(未接)

## TDM Slot 映射

| Slot | 来源 | 预期 RMS |
| --- | --- | --- |
| Slot0 | MIC1（ES7210 ADC1） | ~500（有信号） |
| Slot1 | Reference（ES8311 DAC 回采） | ~0（无播放时） |
| Slot2 | MIC2（ES7210 ADC2） | ~500（有信号） |
| Slot3 | 未接（MIC3/4） | ~0 |

## 诊断结论（2026-07-06）

MIC2 硬件正常。此前误判为硬件故障，根因是代码从 Slot1 取 MIC2 数据（实际 Slot1 是 DAC 回采）。修正为从 Slot2 取后 RMS=500，功能正常。

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh audio_mic2_debug_demo flash
```

### 硬件连接

- 主控板 + MIC 板（ES7210 双麦克风）
- 串口 COM7 用于日志观察

### 测试步骤与预期

1. 烧录后每 2 秒自动打印 4 个 slot 的 RMS/peak 统计信息
2. 对着麦克风说话或制造声响
3. 预期结果：
   - Slot0（MIC1）：RMS > 100，有明显信号
   - Slot1（REF）：无播放时 RMS ≈ 0，为噪底
   - Slot2（MIC2）：RMS > 100，有明显信号
   - Slot3（未接）：RMS ≈ 0，为噪底
4. 通过标准：Slot0 和 Slot2 有正常麦克风信号（RMS > 100），Slot1 和 Slot3 为噪底

## 硬件要求

- I2S：MCLK=GPIO42, BCLK=GPIO41, WS=GPIO39, DOUT=GPIO38, DIN=GPIO40
- ES7210 I2C 地址：0x40（7-bit）/ 0x80（8-bit）
- AMP_CTRL：IOEX P1_0

## 构建

```bash
cd CODE
./tools/build_example.sh audio_mic2_debug_demo
```
