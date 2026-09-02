<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio AEC Test

AEC 回声消除效果自动化评估工具。用于先验证硬件回采和 AEC tone 抑制，再比较 AEC active 与 `disable_aec + reset_buffer` 后的语音保留。该工具不直接等价于小智 TTS+WakeNet 双讲验收。

## 测试流程

1. Phase 1 (5s)：播放 1kHz 正弦波 + AFE(AEC+NS) 处理，输出存入 PSRAM
2. Phase 2 (5s)：停止播放，提示用户说话，AFE 继续处理并将输出存入 PSRAM
3. Phase 3：分析 RMS 和 tone 抑制量(dB)
4. Phase 4：按峰值计算自适应增益，依次播放 Pass A/Pass B 的 Phase2 输出供人耳验证

## 设计要点

- A/B 对比测试：Pass A（AEC 保持 active + 3s recovery）+ Pass B（tone 停后 disable_aec + reset_buffer + 0.5s）
- 两个 pass 的 Phase 1 播放 tone 时都启用 AEC；差别只在 tone 结束后的 Phase 2，因此本工具不是“播放期 AEC ON/OFF”直接对照
- AFE 配置：format="MMR"，AFE_TYPE_VC，AEC_MODE_VOIP_LOW_COST(3)，ns=NSNet2，MIC_PGA=48dB
- 最终参数（Round 17 确认）：REF_GAIN=4, OUTPUT_VOL=100%, GAIN=adaptive(16000/peak, max 60)
- 详细 17 轮调优记录：`AEC_TUNING_LOG.md`

## 日志

- Phase1 AFE 输出 RMS/peak 应明显低于同窗 `m1_rms/ref_rms`，且不应存在明显持续 tone 残留；具体抑制量结合 feed 和 fetch 日志计算
- Pass A Phase2 用于观察 AEC/NLP 保持 active 后的语音压制，Pass B Phase2 用于观察 `disable_aec + reset_buffer` 后的语音恢复
- Phase1/Phase2 ratio 受说话音量和环境噪声影响，不能单独解释为 AEC 消除量，也不存在固定“0dB 即完美”的验收口径
关键日志使用固定字段，便于连续多轮检查：

```text
[AEC_ON][tone][MIC] n=... rms=... peak=... clip=... tone1k_rms=...
[AEC_ON][tone][REF] n=... rms=... peak=... clip=... tone1k_rms=...
[AEC_ON][tone][AFE] n=... rms=... peak=... clip=... tone1k_rms=...
[AEC_OFF][recovery][AFE] n=... rms=... peak=... clip=... tone1k_rms=...
[AEC_ON][WakeNet] tone=... recovery=...
[A/B] AFE 1kHz suppression OFF/ON = ... dB
```

失败时输出具体 pass、任务或资源阶段；正常运行不打印原始 PCM。

## 固定参数

| 参数 | 当前值 | 说明 |
|------|--------|------|
| OUTPUT_VOL | 100 | ES8311 DAC 音量 (%) |
| TONE_AMPLITUDE | 12000 | 正弦波振幅 (±32767) |
| REF_GAIN | 4 | slot1 回采信号软件放大倍数 |
| PLAYBACK_GAIN_MAX | 60 | Phase4 自适应回放增益上限，实际增益为 `16000/peak` |
| STARTUP_DELAY_MS | 15000 | 启动延迟（便于串口连接） |

## 实机安全提示

测试会在 Pass A 和 Pass B 中分别以 100% 输出音量播放 5 秒、振幅 12000 的 1kHz tone，并在结束后回放两段录音。烧录和上电前必须告知现场人员并确认喇叭、供电和听音距离可接受。

## 硬件

- I2S：MCLK=GPIO42、BCLK=GPIO41、WS=GPIO39、DOUT=GPIO38、DIN=GPIO40。
- ES7210 TDM：slot0=MIC1、slot1=REF 硬件回采、slot2=MIC2。
- AMP_CTRL：IOEX P1_0。

## 构建与烧录

macOS 使用项目已验证入口：

```bash
cd CODE
bash ./tools/build_example_macos.sh audio_aec_test
bash ./tools/build_example_macos.sh audio_aec_test flash -p <serial-port>
```

Windows 中文路径环境的正式构建/烧录入口以项目根 `design.md` 4.1.2 为准。2026-08-07 新喇叭调试轮仅完成 clean build，未烧录、未产生新的 Pass A/B 结果。
`flash` 动作会先执行 clean build 和全片擦除，再烧录并校验。
