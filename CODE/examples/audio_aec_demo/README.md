<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio AEC Demo（硬件回采与连续自透传调试）

全双工边播放边录音调试入口。当前源码保留 MMR 硬件回采输入，但 AFE 初始化后立即禁用 AEC；实际输出链路为 NSNet2 + 100ms 延迟 + 噪声门/冷却期 + 自适应增益。

## 功能

- 按 MMR/AEC 能力初始化 ESP AFE，随后执行 `disable_aec + reset_buffer`
- 10s 启动延迟 → 音频初始化；当前源码不播放历史版本的 1kHz 启动诊断音
- 切换 RX 到 4ch TDM 原始模式 → 初始化 AFE → 使能功放
- feed 任务：读取 4ch TDM → 提取 slot0(MIC1) + slot2(MIC2) + slot1(REF) → 交织为 [M1,M2,R] 格式 → feed AFE
- fetch 任务：AFE fetch → 100ms 延迟 → 噪声门/冷却期 → 自适应增益 → 播放
- 每 3 秒输出 MIC/clean RMS，gate 状态变化时输出 OPEN/CLOSED

## 设计要点

- 架构：硬件回采方案（v2），参考 esp-box-3 AFE 架构设计
- 硬件回采路径：POUT_P/N → 衰减滤波网络 → ES7210 MIC3_P/MIC3_N → TDM slot1
- AFE 初始化配置：format="MMR"（2MIC, 1REF），AFE_TYPE_VC，AEC_MODE_VOIP_LOW_COST(3)，ns=NSNet2，aec=true，vad=false，memory=PSRAM
- 当前运行态：初始化后立即 `disable_aec + reset_buffer`，日志明确打印 `AEC=OFF`；硬件 REF 仍进入 feed，但不参与主动 AEC
- 必须用 `board_laiwfs300_audio_open_input_all_channels()` + `board_laiwfs300_audio_read_tdm_4ch()` 直接读 I2S TDM（`esp_codec_dev_read` 不支持多通道原始数据提取）

## 当前自透传基线（历史 Round 17）

- AEC=OFF, NS=NSNet2, OUTPUT_VOL=60%, MIC_PGA=48dB
- 100ms 延迟，自适应增益=28000/peak（最大 3）
- 噪声门：open>500、close<250、hold=3、cooldown=10
- 数字单 tap 相减系数为 0，当前不参与处理

## 硬件要求

- I2S：MCLK=GPIO42, BCLK=GPIO41, WS=GPIO39, DOUT=GPIO38, DIN=GPIO40
- ES7210 TDM 4通道：slot0=MIC1, slot1=REF(硬件回采), slot2=MIC2
- AMP_CTRL：IOEX P1_0
- 硬件 AEC 回采网络（POUT_P/N → 衰减 → ES7210 MIC3）

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh audio_aec_demo flash
```

### 硬件连接

- 主控板 + 喇叭（HT6872 功放已板载）
- 硬件 AEC 回采网络（POUT_P/N → 衰减 → ES7210 MIC3）
- 串口 COM7 用于日志观察

### 测试步骤与预期

1. 烧录后等待 10 秒启动延迟
2. 启动延迟结束后进入连续透传模式（边录边播）
3. 对着麦克风说话，应从喇叭听到经 NSNet2、延迟、噪声门和增益处理的透传声音
4. 观察日志：每 3 秒输出 `mic_rms/clean_rms/echo_sub`，当前 `echo_sub` 应为 0
5. 日志中 gate OPEN/CLOSED 交替出现表示噪声门正常工作
6. 当前基线通过标准：能听到经处理的透传语音，无持续啸叫，日志周期性输出正常

当前固件不能用于判断主动 AEC 的消除效果。需要 AEC ON/OFF 对比时，使用 `audio_aec_test` 的 A/B 流程；该结果也不能直接替代小智 TTS+WakeNet 双讲回归。

## 依赖组件

- `laiwfs300`（BSP）、`audio_processor`（ESP AFE 封装）、`espressif/esp-sr`

## 构建

```bash
cd CODE
./tools/build_example.sh audio_aec_demo
```
