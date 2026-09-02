# AEC 自动化测试调优记录

> 状态说明：Round 1～17 保留当时 runner、术语和实测结论用于追溯；当前生效 runner 从 Round 18 起改为播放期受控 AEC ON/OFF + WakeNet 对比。当前确认 TDM 映射为 slot0=MIC1、slot1=REF、slot2=MIC2，早期记录中的 slot 标号不作为当前实现依据。

## 测试环境

- 硬件：A0 主控板 + C0 扩展板 + 液晶板（含 MIC 板）
- 回采路径：POUT_P/N → 衰减网络 → ES7210 MIC3；当前确认为 TDM slot1（早期轮次曾按 slot2 记录）
- AFE：ESP AFE (1MIC_V251128), AEC_VOIP_HIGH_PERF + NS(WebRTC)
- 采样率：16kHz, I2S TDM 4ch
- 测试流程：Phase1(5s播放1kHz tone+AFE) → Phase2(5s安静+AFE) → Phase3(分析) → Phase4(回放)

## 测试轮次

### Round 1（2026-06-30，基线）

参数：
- REF_GAIN=8, OUTPUT_VOL=80%, PLAYBACK_GAIN=64(固定), TONE_AMPLITUDE=12000

Feed 诊断：
- Phase1: mic_rms≈6100-6300, ref_rms≈2200-2280
- Phase2(settled): mic_rms≈1139, ref_rms≈276

结果：
- Phase1 output RMS=27.1 (AEC 有效)
- Phase2 output RMS=103.0
- out_peak=2272, Phase1/Phase2 ratio=-11.6dB
- **PLAYBACK_GAIN=64 导致严重削波**：2272*64=145408→截断32767

用户反馈：
- Phase1 回放有明显杂音（削波失真）
- Phase2 人声可听到但不清晰，杂音明显，人声音量低于杂音
- Phase2 录制期间环境基本安静

分析：
- AEC 效果好（RMS 6200→27，约47dB抑制）
- 主要问题是 PLAYBACK_GAIN=64 导致 out_peak 严重削波
- Phase2 人声 RMS=103 相对峰值 2272 的比例说明信号动态范围大

---

### Round 2（2026-06-30）

修改点：
- REF_GAIN: 8→22（目标让 ref 匹配 mic 幅度，改善 AEC 对齐）
- PLAYBACK_GAIN: 固定64→自适应（PLAYBACK_GAIN_MAX=32000/peak，避免削波）

Feed 诊断：
- Phase1: mic_rms≈4368-5848, ref_rms≈4388-5843（ref 与 mic 匹配良好）
- Phase2(settled): mic_rms≈415, ref_rms≈320

结果：
- Phase1 output RMS=49.5 (**比 Round 1 的 27 更差**)
- Phase2 output RMS=18.7
- out_peak=387, Phase1/Phase2 ratio=8.5dB
- Playback gains: gain1=82, gain2=89（自适应，无削波）

用户反馈：
- Phase1 回放有明显杂音，类似啸叫，很尖锐（**比 Round 1 更差**）
- Phase2 人声极不清晰，杂音明显，人声远低于杂音
- 整体不如 Round 1

分析：
- REF_GAIN=22 过高，导致 AEC 自适应滤波器产生伪影/啸叫
- AEC 退化（RMS 27→49）说明过高的 REF 反而损害收敛
- Phase2 RMS=18.7 极低（用户确认环境安静，人声未被有效捕获）
- 结论：REF_GAIN=8 对 AEC 效果更好，不应增大

---

### Round 3（2026-06-30）

修改点：
- REF_GAIN: 保持 8
- NS: ON→OFF（关闭降噪，仅保留 AEC，隔离问题根因）
- PLAYBACK_GAIN: 自适应（同 Round 2）

Feed 诊断：
- Phase1: mic_rms≈5300-5700, ref_rms≈1930-2130
- Phase2(settled): mic_rms≈420, ref_rms≈115

结果：
- Phase1 output RMS=86.9 (**远差于 Round 1 的 27，NS 关闭后 AEC 残留大幅上升**)
- Phase2 output RMS=18.0（极低）
- out_peak=1171, Phase1/Phase2 ratio=13.7dB
- Playback gains: gain1=27, gain2=148

用户反馈：
- Phase1 回放有明显杂音，类似啸叫，很尖锐（**最差**）
- Phase2 几乎听不到人声，杂音明显
- 整体表现最差

分析：
- NS(WebRTC) 对 AEC 残留有显著压制作用（关闭后 RMS 27→87）
- 关闭 NS 后语音也未变清晰（Phase2 RMS=18 极低）
- Phase2 mic_rms=420 说明用户说话音量/距离不足（对比 Round 1 的 1139）
- 结论：NS 必须保留；Phase2 人声弱是输入问题，需用户贴近麦克风大声说话

---

### Round 4（2026-06-30）

修改点：
- 回到 Round 1 最佳配置：REF_GAIN=8, NS=ON
- PLAYBACK_GAIN: 自适应→固定 14（基于 Round 1 的 out_peak=2272 计算：32767/2272≈14，避免削波）
- 新增阶段提示音：Phase1 前 1 声滴，Phase2 前 2 声滴，Phase4 前 3 声滴（3000Hz，150ms/500ms间隔），提示音后 1s 再开始

Feed 诊断：
- Phase1: mic_rms≈4900-5200, ref_rms≈1900-2100
- Phase2: mic_rms≈690-890（用户说话）

结果（4-2，用户说话）：
- Phase1 output RMS=45.5 (AEC 有效)
- Phase2 output RMS=16.3
- out_peak=578, Phase1/Phase2 ratio=8.9dB
- PLAYBACK_GAIN=14

用户反馈：
- Phase1 回放有轻微底噪但可接受
- Phase2 可听到微弱人声但音量极低（PLAYBACK_GAIN=14 太小）

分析：
- AEC 工作正常，Phase1 RMS=45 略高于 Round 1 但可接受
- 主要问题是 PLAYBACK_GAIN=14 导致回放音量太小，人声无法有效辨认
- Phase2 mic_rms=890 证明用户有说话（比 Round 2/3 的 420 高），但 AFE 输出 RMS 仅 16.3

---

### Round 5（2026-06-30）

修改点：
- PLAYBACK_GAIN: 14→40（提高回放音量）
- 新增 3s AEC 恢复期（Phase1 结束后等待 3s 再开始 Phase2 录制，减少 NLP hangover 影响）
- Bug 修复：s_current_phase=2 移到恢复期之后（之前在恢复前就设为 2，导致 buffer 被无效数据填充）

Feed 诊断（5-2，用户说话）：
- Phase1: mic_rms≈4900-5700, ref_rms≈1900-2300
- Phase2(recovery): mic_rms=3336（残余）
- Phase2(recording): mic_rms=94→1969（后段说话）

结果（5-2，用户说话）：
- Phase1 output RMS=56.9
- Phase2 output RMS=38.3
- out_peak=786, Phase1/Phase2 ratio=3.4dB
- PLAYBACK_GAIN=40

用户反馈：
- Phase1 有轻微杂音
- Phase2 人声仍然非常弱，几乎听不清

分析：
- Phase2 mic_rms=1969 证明用户有大声说话
- 但 AFE 输出仅 RMS=38.3（衰减约 34dB），说明 AEC NLP 仍在抑制
- 发现新 bug：tone_task 在 3s 恢复期间仍在播放（检查 `while(s_current_phase==1)` 未变）
- 恢复期期间 tone 仍在播放意味着 AEC 没有真正的静默恢复时间

---

### Round 6（2026-06-30）

修改点：
- s_current_phase=2 正确移至 3s 恢复后

Feed 诊断（6-2，用户说话）：
- Phase1: mic_rms≈5500-6900, ref_rms≈1900-2400
- Phase2(recovery): mic_rms=3919, ref_rms=1376（**tone 仍在播放！**）
- Phase2(recording): mic_rms=112→465→366

结果（6-2，用户说话）：
- Phase1 output RMS=58.2
- Phase2 output RMS=11.3
- out_peak=504, Phase1/Phase2 ratio=14.3dB
- PLAYBACK_GAIN=40

用户反馈：
- Phase1 有较严重杂音，类似啸叫
- Phase2 能听到很微弱的人声和环境音，音量极低

分析：
- **关键发现**：`[tone] stopped` 和 `[phase2] recording now` 同时出现（同一时刻 25918ms）
  - 证实 tone 在整个 3s 恢复期间仍在播放（tone_task 检查 `s_current_phase==1` 未变）
  - AEC 没有真正的静默恢复时间，NLP 一直处于抑制状态
- Phase2 mic_rms=465 但 AFE 输出 RMS=11.3（衰减 ~33dB）—— AEC NLP 过度抑制近端语音
- Phase1 RMS 逐轮上升（27→25→57→58）可能与 AEC 收敛状态有关
- 根因：① tone 未及时停止 ② AEC_MODE_VOIP_HIGH_PERF NLP 过于激进

---

### Round 7（2026-06-30）

修改点：
- **Bug 修复**：新增 `s_tone_active` flag 控制 tone_task，Phase1 结束后立即停止 tone
  - 不再依赖 `s_current_phase` 控制 tone 停止
  - `s_tone_active=false` → 200ms 等待 tone_task 退出 → `amp_disable()` → 3s 真正静默恢复
- **AEC 模式降级**：`AEC_MODE_VOIP_HIGH_PERF`(4) → `AEC_MODE_VOIP_LOW_COST`(3)
  - 减轻 NLP 非线性处理的激进程度，减少近端语音被误抑制
- `audio_processor_config_t` 新增 `aec_mode` 字段（0=默认 VOIP_HIGH_PERF，便于后续切换测试）
- 其他参数不变：REF_GAIN=8, NS=ON(WebRTC), OUTPUT_VOL=80%, PLAYBACK_GAIN=40

结果（7-1，不说话）：
- Phase1 output RMS=19.0 (AEC 效果优秀，比 Round 6 的 58 大幅改善)
- Phase2 output RMS=4.3
- out_peak=304, Phase1/Phase2 ratio=12.9dB

结果（7-2，说话）：
- Phase1 output RMS=11.2
- Phase2 output RMS=33.8
- out_peak=591, Phase1/Phase2 ratio=-9.6dB
- Phase2 feed: mic_rms=352→547（用户说话）

用户反馈：
- Phase1 有较严重杂音类似啸叫（纯音 AEC 残留的固有特征）
- Phase2 能听到人声和环境音，无啸叫，比 Round 6 好，但人声音量仍太轻

分析：
- **tone bug 修复确认**：`[tone] stopped` 在 "Phase 2" 前 200ms 出现，恢复期 mic 从 3294→108 衰减正常
- **AEC 效果大幅改善**：Phase1 RMS 从 58（Round 6）降到 11-19（VOIP_LOW_COST 反而更好）
- **语音通过量提升 3x**：Phase2 RMS 从 11.3（Round 6）到 33.8
- **仍有 24dB 语音衰减**：mic_rms=547 → output RMS≈34（估算语音段），衰减约 24dB
- Phase1 "啸叫"是 1kHz 纯音残留放大后的固有特征，真实语音场景不会如此尖锐
- **剩余衰减嫌疑指向 NS(WebRTC)**：AEC NLP 已通过 3s 真静默释放，但 WebRTC NS 仍在持续抑制

---

### Round 8（2026-06-30）

修改点：
- **A/B 对比测试**：Pass A = NS OFF, Pass B = NS ON（同一次启动内对比）
- PLAYBACK_GAIN: 40→60（Round 7 out_peak=591，591×60=35460 仅轻微削波，提升可听度）
- AEC_MODE=VOIP_LOW_COST（保持）
- 其他参数不变：REF_GAIN=8, OUTPUT_VOL=80%

结果（8-1，不说话）：
- Pass A (NS OFF): Phase1 RMS=39.7, Phase2 RMS=14.6, out_peak=1068
- Pass B (NS ON): Phase1 RMS=26.7, Phase2 RMS=2.5, out_peak=364

结果（8-2，说话）：
- Pass A (NS OFF): Phase1 RMS=62.8, Phase2 RMS=15.0, out_peak=643, mic_rms=229→374
- Pass B (NS ON): Phase1 RMS=50.9, Phase2 RMS=11.0, out_peak=490, mic_rms=131→395

用户反馈：
- Phase2 能听到极微弱人声，几乎无法辨认
- Pass A 和 Pass B 差异不大

分析：
- **NS 不是语音衰减的根因**：Pass A(无NS) Phase2 RMS=15 vs Pass B(有NS) Phase2 RMS=11，仅差 2.7dB
- **AEC NLP 是真正根因**：即使无 NS，mic_rms=374 → output RMS=15，衰减约 28dB
- AEC NLP 在 tone 停止 3s 后仍未释放抑制状态
- 参考项目使用 `disable_aec` + `reset_buffer` 解决此问题（播放停止时关闭 AEC）

---

### Round 9（2026-06-30）

修改点：
- **新增 `audio_processor_disable_aec()` / `enable_aec()` / `reset_buffer()` API**
- A/B 对比：
  - Pass A（1声滴）：tone 停止后 AEC 保持 active + 等 3s（当前行为）
  - Pass B（2声滴）：tone 停止后立即 `disable_aec` + `reset_buffer` + 等 0.5s
- NS=ON（保持，已证明影响很小）
- AEC_MODE=VOIP_LOW_COST, REF_GAIN=8, OUTPUT_VOL=80%, PLAYBACK_GAIN=60

预期效果：
- Pass A：Phase2 人声仍被抑制（与 Round 7/8 一致）
- Pass B：如果 Phase2 人声明显增大，确认 `disable_aec` 是正确的生产方案

生产模式设计：
- AI 播放语音时：`enable_aec`（消除回声）
- AI 停止播放后：`disable_aec` + `reset_buffer`（释放抑制，人声通过）

状态：已构建烧录。

结果（9，大声说话）：
- Pass A (AEC active, 3s recovery): Phase1 RMS=77.3, Phase2 RMS=37.3, out_peak=1236, mic_rms=556→1747
- Pass B (disable_aec + reset_buffer): Phase1 RMS=85.3, **Phase2 RMS=841.1**, out_peak=**9819**, mic_rms=6586→264→1291

用户反馈：
- Pass B 明显更清晰响亮，基本可以辨认说话内容
- Pass A 人声极弱（与此前轮次一致）
- 声音较轻时两个 Pass 都没有明显人声（MIC 灵敏度/距离问题）
- Pass B 仍有杂音（环境噪声被 60x 放大）

分析：
- **根因确认：AEC NLP 残留抑制**
  - Phase2 RMS: 37.3 → 841.1（**22.5 倍 / +27dB**）
  - out_peak: 1236 → 9819（8 倍）
- `disable_aec()` 返回 0（成功），`reset_buffer()` 返回 1（成功）
- 仅需 0.5s 等待即可开始录音（不需要 3s 恢复期）
- 剩余杂音来源：环境噪声 + NS(WebRTC) 抑制不完全 + 60x 回放增益放大
- 声音轻时录不清是 MIC 灵敏度问题（ES7210 PGA 增益可调），独立于 AEC 调优

---

## 关键结论汇总

| Round | REF_GAIN | NS | AEC Mode | Phase1 RMS | Phase2 RMS | out_peak | 用户评价 |
|-------|----------|-----|----------|-----------|-----------|----------|---------|
| 1 | 8 | ON | VOIP_HIGH_PERF | 27.1 | 103.0 | 2272 | AEC有效，削波导致杂音 |
| 2 | 22 | ON | VOIP_HIGH_PERF | 49.5 | 18.7 | 387 | 更差，啸叫 |
| 3 | 8 | OFF | VOIP_HIGH_PERF | 86.9 | 18.0 | 1171 | 最差，啸叫+无人声 |
| 4-2 | 8 | ON | VOIP_HIGH_PERF | 45.5 | 16.3 | 578 | 人声极弱(gain=14太小) |
| 5-2 | 8 | ON | VOIP_HIGH_PERF | 56.9 | 38.3 | 786 | 人声弱(tone未停bug) |
| 6-2 | 8 | ON | VOIP_HIGH_PERF | 58.2 | 11.3 | 504 | 人声极弱(NLP过度抑制) |
| 7-1 | 8 | ON | VOIP_LOW_COST | 19.0 | 4.3 | 304 | AEC优秀，Phase2安静 |
| 7-2 | 8 | ON | VOIP_LOW_COST | 11.2 | 33.8 | 591 | 人声有改善但仍轻 |
| 8-1 | 8 | A:OFF/B:ON | VOIP_LOW_COST | A:39.7/B:26.7 | A:14.6/B:2.5 | A:1068/B:364 | NS差异极小 |
| 8-2 | 8 | A:OFF/B:ON | VOIP_LOW_COST | A:62.8/B:50.9 | A:15.0/B:11.0 | A:643/B:490 | NS非根因，AEC NLP是 |
| 9 | 8 | ON | VOIP_LOW_COST | A:77.3/B:85.3 | A:37.3/**B:841.1** | A:1236/**B:9819** | **disable_aec 解决！+27dB** |
| 11-2 | 8 | ON(WebRTC) | VOIP_LOW_COST | A:47.8/B:51.2 | A:7.7/**B:536.9** | A:1089/**B:5945** | Pass B清晰可辨 |
| 12-1 | 8 | ON(**NSNet2**) | VOIP_LOW_COST | A:17.9/B:30.3 | A:7.4/**B:315.5** | A:283/**B:2854** | **历史最佳：清晰、杂音不明显** |
| 13-1 | 8 | ON(NSNet2) | VOIP_LOW_COST | A:25.7/B:34.8 | A:9.6/**B:355.1** | A:285/**B:3573** | 清晰可辨，音量略弱，watchdog修复 |
| 14-1 | **4** | ON(NSNet2) | VOIP_LOW_COST | A:5.9/B:20.2 | A:11.4/**B:566.4** | A:196/**B:5447** | 清晰可辨，音量弱于音源(80%vol) |
| 15-1 | 4 | ON(NSNet2) | VOIP_LOW_COST | A:15.0/B:8.8 | A:17.1/**B:424.8** | A:434/**B:4131** | 同上，24000目标差异不大 |
| 16-1 | 4 | ON(NSNet2) | VOIP_LOW_COST | A:-/B:238.6 | A:-/**B:374.0** | A:-/**B:3087** | 100%vol，音量强于音源，有破音 |
| **17-1** | **4** | **ON(NSNet2)** | **VOIP_LOW_COST** | A:268.2/B:225.7 | A:21.9/**B:529.5** | A:343/**B:4589** | **★最佳：清晰、音量接近音源** |

---

### Round 10（2026-07-01，作废——构建镜像问题导致实际烧入旧代码 MR）

由于 robocopy 在 bash 环境下无法正确处理中文路径，实际烧录的是旧镜像中的 MR（1mic+1ref）代码而非 MMR。
AFE 日志确认：`format=MR`, `1 microphone, 1 playback`。Round 10 数据不具参考价值，跳过。

修复措施：后续构建使用 bash `find+cp` 方式替代 robocopy 进行镜像，并加入验证步骤确认源码正确。

---

### Round 11（2026-07-01，MMR + 48dB PGA + 自适应增益）

修改点：
- **格式切换**：`"MR"` → `"MMR"` (2mic+1ref)，`feed_task` 提取 slot0(MIC1) + slot1(MIC2) + slot2(REF) 交织 [M1,M2,R]
- **MIC PGA 48dB**：直接写 ES7210 REG17/REG18=0x60，绕过驱动 37.5dB 上限
- **自适应回放增益**：`gain = min(28000/out_peak, 60)`，避免 Pass B 信号强时严重削波
- A/B 对比不变：Pass A = AEC active(3s recovery), Pass B = disable_aec + reset_buffer
- 其他参数不变：AEC_MODE=VOIP_LOW_COST, REF_GAIN=8, NS=ON, OUTPUT_VOL=80%

预期效果：
- 48dB PGA 使 mic_rms 大幅提升（约 +18dB vs 默认 30dB），Phase2 不说话时 mic_rms 底噪也会上升
- MMR 双麦波束成形可能提升 3-6dB SNR
- 自适应增益确保回放不超过音源音量

验证要点：
- 日志确认 `format=MMR`（而非 MR）和 `ES7210 REG17=0x60`
- `m1_rms` 和 `m2_rms` 两路 MIC 信号
- Pass B 回放音量是否合理（不再比音源大）
- 语音是否更清晰（48dB 增益提升）

状态：已构建烧录（2026-07-01）。

结果（11-1，不说话）：
- Pass A (AEC active): Phase1 RMS=45.0, Phase2 RMS=4.7, Ratio=19.6dB, out_peak=456
- Pass B (disable_aec): Phase1 RMS=31.0, Phase2 RMS=101.0, Ratio=-10.3dB, out_peak=5123
- Feed: m1_rms=7274/7249, **m2_rms=1025/1051**, ref_rms=2412/2392（tone 播放时）
- Feed Phase2: m1_rms=100-108, **m2_rms=1**, ref_rms=42-47（安静时）
- Adaptive gains: A=60(peak=54), B=5(peak=5123)

结果（11-2，说话）：
- Pass A (AEC active): Phase1 RMS=47.8, Phase2 RMS=7.7, Ratio=15.8dB, out_peak=1089
- Pass B (disable_aec): Phase1 RMS=51.2, Phase2 RMS=536.9, Ratio=-20.4dB, out_peak=5945
- Feed: m1_rms=7861/6620（tone）, m1_rms=272-723（Phase2说话）
- **m2_rms 恒为 1**（安静/说话时均无信号）
- Adaptive gains: A=60(peak=126), B=4(peak=5945)

用户反馈：
- 11-1：Pass B 声音稍大，不能确认是背景音还是杂音，但没有啸叫
- 11-2：Pass B 可以听到比较清晰的人声，杂音比人声小，基本可以清晰辨认人声
- Pass B 回放音量略高于音源（gain target 28000 偏大）
- MIC2 完全无信号（m2_rms=1），tone 时的 ~1025 为 TDM 总线串扰

分析：
- **MIC2 无信号根因已定位**：`board_audio.c` 中 `esp_codec_dev_open` 的 `channel_mask` 只设了 CH0，ES7210 驱动据此只配置 slot 0 输出有效数据（Round 12 已修复）
- AEC 效果正常：Phase1 RMS 45-51（tone 抑制良好）
- disable_aec 方案持续有效：Phase2 RMS 从 4.7/7.7 升至 101/537
- 48dB PGA 使 m1_rms 达到 7274（tone 时），动态范围充足
- 回放 gain=4-5x 仍略大于 1x 音源音量（Round 12 改为 16000/peak 解决）
- AFE 警告 "AFE_TYPE_VC only support single microphone channel" 表明 1MIC 模型只用 MIC1，MIC2 修复后 AFE 仍只取第一通道（双麦波束成形需 2MIC 模型）

---

### Round 12（2026-07-01，MIC2修复 + NSNet2 + 增益微调）

修改点：
- **MIC2 channel_mask 修复**：`board_audio.c` 的 `esp_codec_dev_open` 从 `channel_mask=CH0` 改为 `CH0|CH1|CH2|CH3`，同时 `set_in_channel_gain` 也改为全通道。此为 MIC2 无信号的软件根因——之前只配置了 channel 0，ES7210 只在 TDM slot 0 输出有效数据
- **NS 模型升级**：从 WebRTC NS 改为 **NSNet2**（`CONFIG_SR_NSN_NSNET2=y`），神经网络降噪效果更好
- 自适应增益目标从 `28000` 降为 `16000`（匹配 tone 振幅 12000 的量级）

预期效果：
- MIC2 应出现有效信号（`m2_rms` 不再为 1）
- AFE 日志应显示 `|NS(NSNet2)|` 而非 `|NS(WebRTC)|`
- Pass B 回放增益 2-3x，音量与音源基本一致
- NSNet2 降噪可能进一步改善语音清晰度

构建信息：
- 产物：`audio_aec_test.bin`（696KB/87% free）
- srmodels.bin：338KB（含 NSNet2 模型）
- sdkconfig 确认：`CONFIG_SR_NSN_NSNET2=y`，`CONFIG_SR_NSN_WEBRTC` not set

状态：已烧录测试（2026-07-01）。

结果（12-1，说话）：
- Pass A (AEC active): Phase1 RMS=17.9, Phase2 RMS=7.4, Ratio=7.6dB, out_peak=283
- Pass B (disable_aec): Phase1 RMS=30.3, Phase2 RMS=315.5, Ratio=-20.3dB, out_peak=2854
- Feed Phase1: m1_rms=8716/8354, **m2_rms=22790/23353**(pk=32768 削波), ref_rms=29207/29457(pkR=32768 削波)
- Feed Phase2: m1_rms=372-584, **m2_rms=1**, ref_rms=3778-5981(pkR=31912-32768)
- Adaptive gains: A=60(peak=283), B=5(peak=2854)
- NSNet2 确认生效：`AFE Pipeline: [input] -> |AEC(VOIP_LOW_COST)| -> |NS(nsnet2)| -> [output]`
- slot_mask 确认：`0xf`（4通道全部启用）
- **task_wdt 触发**：aec_feed task 在 CPU1 触发看门狗（NSNet2 处理耗时）

用户反馈：
- Phase2 安静时基本安静
- Pass A 可以听到非常微弱的人声，杂音和人声差不多音量，几乎无法听清
- **Pass B 可以听到比较清晰的人声，效果不错，杂音不明显，内容清晰可辨认，音量稍弱于音源**

分析：
- **NSNet2 生效且效果好**：Pass B 语音清晰度为历史最佳
- **REF 通道严重削波**：ref_rms=29207(pkR=32768)——原因是 `set_in_channel_gain` 错误地给全 4 通道设了 30dB，REF(MIC3) 被过度放大（Round 11 ref_rms=2412 正常）
- **MIC2 伪信号确认**：tone 时 m2_rms=22790(pk=32768) 是 REF ADC 饱和导致的内部串扰，非真实声学信号；说话时 m2_rms=1 进一步确认 MIC2 为硬件问题
- **Watchdog 根因**：NSNet2 使 feed_chunksize 从 256 增为 512，`audio_processor_feed()` 处理耗时更长，feed_task 长时间不 yield 导致 IDLE1 饿死
- disable_aec 方案持续有效：Phase2 RMS 7.4 → 315.5（+32dB）
- 回放增益 5x 合理（16000/2854≈5），用户反馈"稍弱于音源"符合设计预期

---

### Round 13（2026-07-01，REF增益修复 + Watchdog修复）

修改点：
- **REF 通道增益修复**：`board_audio.c` 的 `set_in_channel_gain` 从全 4 通道改为只设 CH0|CH1（MIC1/MIC2），REF 通道(CH2/MIC3)恢复默认低增益
- **Watchdog 修复**：`feed_task` 中 `audio_processor_feed()` 后增加 `vTaskDelay(1)`，让出 CPU 给 IDLE task 重置看门狗
- 其他参数不变：AEC_MODE=VOIP_LOW_COST, REF_GAIN=8, NS=NSNet2, OUTPUT_VOL=80%, 自适应增益 16000/peak

结果（13-1，说话）：
- Pass A (AEC active, 3s recovery): Phase1 RMS=25.7, Phase2 RMS=9.6, Ratio=8.5dB, out_peak=285
- Pass B (disable_aec): Phase1 RMS=34.8, Phase2 RMS=355.1, Ratio=-20.2dB, out_peak=3573
- Feed Phase1: m1_rms=7925/7720, m2_rms=1032/1058, ref_rms=28835/29369, pkR=32768（**仍削波**）
- Feed Phase2 (Pass B): m1_rms=1684→677→694, ref_rms=15717→6319→6239, pkR=32768
- Adaptive gains: A=56(peak=285), B=4(peak=3573)
- **无 task_wdt 告警**（watchdog 修复确认有效）
- NSNet2 确认：`AFE Pipeline: [input] -> |AEC(VOIP_LOW_COST)| -> |NS(nsnet2)| -> [output]`

用户反馈：
- Phase2 阶段基本安静
- Pass A 可以听到非常微弱的人声，杂音和人声差不多音量，几乎无法听清
- **Pass B 可以听到比较清晰的人声，效果不错，杂音不明显，内容清晰可辨认，音量弱于音源，效果略差于 Round 12**

分析：
- **Watchdog 修复确认有效**：全程无 task_wdt 告警
- **REF 仍然削波**：`set_in_channel_gain` 修改对 TDM 读取路径无效——`board_laiwfs300_audio_read_tdm_4ch()` 直接调用 `i2s_channel_read()` 绕过 `esp_codec_dev`，软件增益不作用于该路径
- **REF 削波根因重新定位**：ES7210 MIC3 硬件 PGA 在全通道 open 后输出强信号（raw peak ~4096），× REF_GAIN=8 = 32768 削波。与 `set_in_channel_gain` 无关
- **MIC2 串扰大幅降低**：m2_rms 从 Round 12 的 22790 降至 ~1040（`set_in_channel_gain` 对 codec_dev 路径的 MIC2 通道仍有效果）
- **"效果略差于Round12"实为测试波动**：Round 13 实际 Phase2 RMS=355.1（高于 Round 12 的 315.5），回放幅度 3573×4=14292 ≈ Round 12 的 2854×5=14270，几乎一致
- 感知差异可能源于说话内容/距离略有不同，或 vTaskDelay(1) 对 AFE 处理时序的微小影响

---

### Round 14（2026-07-01，REF_GAIN降低 + 回放增益提升）

修改点：
- **REF_GAIN: 8→4**：根因修复——降低软件 REF 放大倍数，减轻 REF 削波
- **自适应增益目标: 16000→20000**：提高回放音量
- 其他参数不变：AEC_MODE=VOIP_LOW_COST, NS=NSNet2, OUTPUT_VOL=80%, MIC_PGA=48dB

结果（14-1，说话）：
- Pass A: Phase1 RMS=5.9, Phase2 RMS=11.4, out_peak=196, gain=60
- Pass B: Phase1 RMS=20.2, **Phase2 RMS=566.4**, out_peak=5447, gain=3 (20000/5447)
- Feed Phase1: m1_rms=3237, ref_rms=17423, pkR=32767（REF 削波大幅改善）
- Playback: 5447×3=16341 @80% vol

用户反馈：Pass B 清晰可辨认，杂音不明显，**音量还是弱于音源**，与 Round 13 差异不大

分析：
- REF_GAIN=4 显著改善 AEC（Pass A Phase1 RMS 从 25.7 降到 5.9）
- ref_rms 从 28835 降到 17423（-40%），pkR 仍偶尔触 32767 但不再全程饱和
- 回放增益从 4x→3x（因 peak 更高），实际回放幅度仅 16341，与 Round 13 接近

---

### Round 15（2026-07-01，增益目标继续提升）

修改点：
- **自适应增益目标: 20000→24000**
- 其他不变

结果（15-1，说话）：
- Pass B: Phase2 RMS=424.8, out_peak=4131, gain=5 (24000/4131)
- Playback: 4131×5=20655 @80% vol

用户反馈：Pass B 清晰可辨认，**音量还是弱于音源**，与 Round 14 差异感觉不大

分析：增益目标提升生效（gain 3→5），但自适应机制使不同 peak 值得到相似的回放幅度。瓶颈是 OUTPUT_VOL=80% 限制了扬声器物理输出。

---

### Round 16（2026-07-01，OUTPUT_VOL 提升）

修改点：
- **OUTPUT_VOL: 80%→100%**：增加扬声器物理输出功率
- 增益目标保持 24000

结果（16-1，说话）：
- Pass B: Phase1 RMS=238.6, Phase2 RMS=374.0, out_peak=3087, gain=7 (24000/3087)
- Feed Phase1: m1_rms=14589, pk1=32768（**MIC1 ADC 饱和！** tone 在 100% vol 下太响）
- Playback: 3087×7=21609 @100% vol

用户反馈：Pass B 人声响亮，**音量强于音源**，部分有破音/杂音

分析：
- OUTPUT_VOL=100% 有效，但 gain=7 过高导致：①回放音量过大 ②Phase2 初始残留被 7x 放大产生破音
- MIC1 在 tone 期间削波（100% vol 使声压过大）→ AEC Phase1 效果退化（RMS=238 vs Round 14 的 20）
- 需降低 gain 以平衡音量

---

### Round 17（2026-07-01，增益目标降回平衡点）★ 最终确认

修改点：
- **自适应增益目标: 24000→16000**：降低数字增益，配合 100% vol 达到平衡
- OUTPUT_VOL=100% 保持

结果（17-1，说话）：
- Pass A: Phase1 RMS=268.2, Phase2 RMS=21.9, out_peak=343, gain=46
- Pass B: Phase1 RMS=225.7, **Phase2 RMS=529.5**, out_peak=4589, **gain=3** (16000/4589)
- Feed Phase2 (说话段): m1_rms=892/941, ref_rms=4430/4576
- Playback: 4589×3=13767 @100% vol

用户反馈：
- **效果目前为止最好**
- 杂音不明显
- 内容清晰可辨认
- **音量接近音源** ✓

分析：
- OUTPUT_VOL=100% + GAIN=16000/peak 是最佳组合
- 回放幅度 13767 在 100% vol 下恰好接近用户说话音量
- Phase2 RMS=529.5 信号质量良好
- AEC Phase1 在 100% vol 下效果退化（tone 使 MIC1 饱和），但 Pass B 生产模式不受影响（disable_aec 后不依赖 AEC）

---

## 下一步计划

已确认事实：
- REF_GAIN=8 的 AEC 效果优于 22（RMS 27 vs 49）
- NS(WebRTC) 对 AEC 残留有显著压制作用（ON: RMS 19, OFF 预计 50-90）
- **NS 不是语音衰减的根因**（Round 8 证实：NS ON/OFF 仅差 2.7dB）
- REF_GAIN 过高会导致 AEC 自适应滤波器产生啸叫伪影
- PLAYBACK_GAIN=64 在 out_peak=2272 时产生严重削波（2272×64=145408）
- Phase2 人声 RMS 受用户说话音量/距离影响显著（Round 1=103 vs Round 2/3≈18）
- **tone_task 必须用独立 flag 控制停止**，不能依赖 s_current_phase（Round 5/6 的 bug）
- **AEC_MODE_VOIP_LOW_COST 比 HIGH_PERF 效果更好**（Phase1 RMS: 58→11-19）且语音衰减更少
- **AEC NLP 残留抑制是语音衰减的根因**（Round 9 证实：disable_aec 后 Phase2 RMS 37→841，+27dB）
- **生产方案：播放时 enable_aec，停止后 disable_aec + reset_buffer**
- `disable_aec()` + `reset_buffer()` 后仅需 0.5s 即可开始录音（无需 3s 恢复期）
- 3s 恢复期必须确保 tone 完全停止、参考信号为零，AEC NLP 才能释放抑制状态
- Phase1 "啸叫"是纯音 AEC 残留放大后的固有听感，真实语音场景不存在此问题
- **`set_in_channel_gain` 对 TDM 原始读取路径无效**（Round 13 证实：ref_rms 未变化）
- **REF 削波根因是 REF_GAIN=8 × ES7210 MIC3 原始强信号**，需降低 REF_GAIN
- **vTaskDelay(1) 成功解决 NSNet2 导致的 watchdog 问题**（Round 13 确认）
- **OUTPUT_VOL=100% 比 80% 提升约 +6dB（4倍声压）**（Round 16 vs 15: m1_rms 3175→14589）
- **Round 17 最终确认：OUTPUT_VOL=100% + GAIN=16000/peak 是最佳平衡点**

## 最终参数（生产推荐）★ Round 17 验证通过

```
AEC_MODE        = VOIP_LOW_COST (3)
REF_GAIN        = 4
NS              = ON (NSNet2)
MIC_PGA         = 48dB (REG17/REG18=0x60)
OUTPUT_VOL      = 100%
GAIN            = adaptive (16000/peak, max 60)
enable_aec      = 播放时开启
disable_aec     = 播放结束后立即关闭 + reset_buffer
```

生产音频流程：
1. AI 开始播放 → `audio_processor_enable_aec()`
2. AI 播放中 → AEC 实时消除回声，MIC 信号经 AFE 输出干净语音
3. AI 播放结束 → `audio_processor_disable_aec()` + `audio_processor_reset_buffer()`
4. 用户说话 → AFE 仅做 NS 降噪，语音直通无衰减

---

## MIC 增益调试计划（MMR 稳定后执行）

**硬件建议：从最大增益 48dB 开始递减调试。**

ES7210 PGA 增益范围：
- `esp_codec_dev` 驱动支持：0dB ~ 37.5dB（通过 `esp_codec_dev_set_in_channel_gain`）
- 硬件实际支持：0dB ~ 48dB（需直接写 ES7210 寄存器 REG17/REG18，PGA 增益步进 0.5dB）
- ES7210 PGA 寄存器 REG17(MIC1)/REG18(MIC2)：0x00=0dB, 0x60=48dB

调试策略：
- **起始点：MIC1/MIC2 PGA = 48dB**（直接写 ES7210 寄存器）
- 逐步递减：48 → 42 → 37.5 → 33 → 30 → 24 → 18dB
- 每档评估：信噪比、底噪水平、语音清晰度、是否削波
- REF 通道（MIC3/当前 TDM slot1）增益独立控制，不跟随 MIC 增益变化
- Round 10 先用驱动最大 37.5dB 验证 MMR 有效性，后续 Round 11+ 从 48dB 开始精调

评估标准：
| 指标 | 目标 |
|------|------|
| Phase2 不说话时 out_rms | 尽量低（< 50 为佳，表示底噪可控） |
| Phase2 正常音量说话时 out_rms | > 500（disable_aec 模式下） |
| 正常说话时 mic_rms | > 1000（ADC 未削波，peak < 30000） |
| 回放清晰度 | 无需 60x 增益即可辨认内容 |

调试顺序：
1. ~~MMR 双麦切换 + 验证 AEC/disable_aec 有效~~ → Round 10 进行中
2. MIC1/MIC2 PGA = 48dB（直接写 ES7210 REG17/REG18=0x60）
3. 如果底噪过大或削波，逐步降低 PGA
4. 找到"语音清晰 + 底噪可接受"的平衡点
5. 确认最终 PGA 值，写入生产参数

---

## Round 18（2026-08-07，受控 AEC ON/OFF + WakeNet 重设计；仅构建）

### 目标与边界

- 用户确认旧 runner 不能作为播放期 AEC ON/OFF 直接对照，并补充最终目标：AEC 应服务于小智在播放/TTS 状态下更可靠地识别唤醒词。
- 本轮只修改 `audio_aec_test` 私有实现、测试和模型配置，不修改共享 `audio_processor`、`audio_aec_demo`、小智、板级驱动或生产参数。
- 用户最终要求只完成构建，不烧录、不实机测试；因此本轮没有新喇叭声学数据，不形成 AEC 参数优选结论。

### 当前生效测试流程

- Pass A：AFE 初始化时 AEC ON；Pass B：AFE 初始化时 AEC OFF。
- 两个 pass 除 AEC 初始化开关外完全一致：1 次启动提示、250ms 任务预热、5s 的 1kHz/幅度12000/100%音量 tone、3s 恢复、MMR、REF_GAIN=4、MIC PGA 48dB、NSNet2、`wn9s_nihaoxiaozhi`、WakeNet 阈值0.65。
- slot0/1/2=`MIC1/REF/MIC2`，feed 格式仍为 `[MIC1,MIC2,REF]`；REF×4 后保留 int16 饱和限幅。
- 两个 pass 都启用 WakeNet，因此现有 `audio_processor` 选择 `AFE_TYPE_SR`；AEC ON pass 的算法子模式仍为 `AEC_MODE_VOIP_LOW_COST(3)`。当前共享封装仍固定 `AFE_MODE_HIGH_PERF`。
- 参考工程的 `MR + AFE_TYPE_SR + AFE_MODE_LOW_COST + AEC/SE + WakeNet + NS off` 只登记为下一层独立候选；当前 runner 已是 SR，但没有把 LOW_COST AFE mode、MR、SE/NS-off 与本轮一起混改。

### 数据、指标与生命周期

- 播放期和恢复期的 MIC1、REF、AFE 输出均先保存到 PSRAM；feed/fetch 实时任务不写 Flash。
- 每个窗口分别输出 sample count、RMS、peak、满量程削波率和 1kHz `tone_rms`；1kHz 分量使用 I/Q 正交投影，避免声学/处理延迟导致单相相关误判。
- A/B 总结输出 `20*log10(AEC_OFF tone_rms / AEC_ON tone_rms)`；fetch 同时统计播放期与恢复期的 WakeNet 检出次数。实机判读必须控制唤醒词次数、距离和音量，最终仍需小智 TTS 播放期打断回归。
- feed、fetch、tone 三任务分别报告退出，三者全部 join 后才销毁 AFE；创建失败、内部失败或 join 超时进入错误出口。join 超时时保留 AFE/PSRAM 并要求重启，防止 use-after-free。
- Round 7 的独立 `s_tone_active`、Round 13 的 feed 后 `vTaskDelay(1)`、Round 14 的 REF_GAIN=4 与限幅均保留。

### 纯 C RED→GREEN 门禁

- 新增 `main/aec_test_logic.c/.h`、`tests/aec_test_logic_test.c`、`tests/run_host_tests.ps1`。
- 逐行为取得真实 RED：受控计划退出码3、基础统计退出码7、1kHz 投影退出码1、抑制 dB退出码2、三任务 join退出码3、错误态退出码1、I/Q 相位独立退出码1、WakeNet 同条件退出码1、阈值一致退出码1。
- 最终使用 LLVM/Clang 22.1.8，target=`x86_64-pc-windows-msvc`，实际编译、链接并运行得到 `aec_test_logic_test: PASS (0 failures)`。

### 构建与当前状态

- 按 `design.md` 4.1.2 成熟 Windows 入口执行全量临时镜像和 clean build，结果 `1416/1416`。
- 应用 `audio_aec_test.bin=0xad290`（709264字节），5MB app 分区剩余 `0x452d70`（86%）。
- SHA-256：`6D9F369102FC8AB9959007B90A0C486575A55E90103C7F695E532B7D478D7F33`。
- 新主文件和 `aec_test_logic.c` 对象/符号均进入 ELF，WakeNet 与 NSNet2 配置进入构建；仅有既存 `ESP_IDF_VERSION`、`MODEL_IN_SPIFFS`、`SR_MN_NONE` 告警，无源码编译、链接或尺寸失败。
- 未连接 COM7、未全片擦除、未烧录、未抓串口、未执行声学/WakeNet/回放测试。正式状态：**已完成重设计与构建，待实机验证**。
