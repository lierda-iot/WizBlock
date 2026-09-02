# AEC Demo 调优记录

## 需求定义

实时连续透传（边录边播）：MIC → 音频处理 → 扬声器，持续运行，目标是最佳人声音质。

## 硬件环境

- A0 主控板 + 液晶板（含 MIC 板）+ C0 扩展板
- MIC: ES7210 4ch TDM, MIC1 = slot0
- DAC: ES8311, MONO 输出
- 回采路径: POUT_P/N → 衰减网络 → ES7210 MIC3_P/MIC3_N (TDM slot2)
- MIC 与 Speaker 物理距离近（同机壳组装），声学耦合强

## 核心矛盾

passthrough 模式同时存在两个对立需求：
1. **防啸叫**：需要降低/消除 speaker → MIC 的声学反馈环路增益
2. **人声透传**：需要让 MIC 采集的人声以足够音量从 speaker 播出

AEC 解决了需求 1（Round 2 确认无啸叫），但其 NLP 模块同时破坏了需求 2（语音被压制 25-33dB）。

## AEC Test 关键结论回顾

| 轮次 | 关键发现 | 对 passthrough 的意义 |
|------|---------|---------------------|
| Round 7 | VOIP_LOW_COST 模式 AEC RMS 58→11 | 该模式 AEC 抑制效果强 |
| Round 8 | NS OFF vs ON 仅差 2.7dB；NLP 是语音衰减根因 | **NS 不是问题，NLP 才是** |
| Round 9 | disable_aec+reset 后语音 +27dB（RMS 37→841） | **NLP 单独造成 ~27dB 语音压制** |
| Round 14 | REF_GAIN 8→4 修复削波，AEC RMS 25.7→5.9 | REF_GAIN=4 时 AEC 抑制约 25-30dB |

### 关键数据点

AEC Test Round 9:
- Pass A (AEC active): mic_rms=1747, output_rms=37.3 → **语音衰减 33dB**
- Pass B (disable_aec): mic_rms=1291, output_rms=841 → **语音几乎无衰减**

结论：AEC 的 NLP 不区分"回声"和"直接人声"，只要 AEC 处于 active 状态且检测到 REF 信号活动，就会应用 25-33dB 的全频带抑制。

## 调优轮次记录

### Round 1（AEC 禁用，原始透传）

参数：AEC disabled + reset_buffer, PGA=48dB, VOL=100%, GAIN_MAX=8

结果：
- 无人声时 mic_rms=8885-13687（反馈环路振荡）
- out_rms=896-4975, peak=7253-31385
- **啸叫严重**

分析：
- 开环增益估算：speaker 数字输出 4131 → 经声学耦合 + 48dB PGA 后 mic_rms=11825
- 反馈环路增益 ≈ 1.3-2×（> 1，不稳定）
- 48dB PGA（250×电压增益）+ 近距离声学耦合 = 极高环路增益
- NSNet2 在环路中提供约 -9dB 衰减，但不足以抵消

### Round 2（AEC 启用，REF_GAIN=4）

参数：AEC enabled（不 disable）, REF_GAIN=4, PGA=48dB, VOL=100%, GAIN_MAX=6

结果：
- 安静时：mic_rms=112-163, out_rms=0-1, gain=6 → **无啸叫** ✓
- 人声较大时：mic_rms=6498, out_rms=506, peak=4786, gain=3
- 人声较弱时：mic_rms=425, out_rms=7-10, gain=6 → **几乎无输出**

分析：
- AEC 成功消除反馈环路 ✓
- 但 NLP 对人声抑制：506/(6498×AFE通过率) ≈ -22 到 -32dB
- 与 AEC Test Round 9 Pass A 的 33dB 抑制一致
- 原因：passthrough 中 speaker 持续播放（即使是极小信号），NLP 检测到"远端活动"后持续压制近端信号
- 用户反馈：啸叫消除，但人声极弱（大声勉强可辨认，小声无输出）

## 问题根因

```
passthrough 闭环：
MIC → [AFE: AEC+NS] → gain → Speaker → (声学耦合) → MIC
                         ↑                    ↓
                    REF通道(硬件回采) ←────────┘
```

**AEC 的设计前提**：MIC 信号 = 近端人声 + 远端回声。AEC 利用 REF 信号估计回声并消除。

**passthrough 的特殊性**：Speaker 播放的不是独立的"远端"音频，而是 MIC 自身的处理输出。这导致：
1. REF 信号与 MIC 信号高度相关（因果关系延迟仅几十ms）
2. NLP 始终认为"远端在说话"（因为 speaker 一直有输出）
3. NLP 持续抑制"近端"（即真实人声），造成 25-33dB 衰减

这是 AEC 算法的固有限制——它为**双向通话**设计（两个独立声源），不适用于**单源环回**场景。

## 解决方案分析

### 方案 A：降低 REF_GAIN（削弱 AEC 参考）

思路：REF_GAIN 从 4 降到 1，使 AEC 参考信号更弱。

问题：
- NLP 的双端检测器依据 REF 通道是否有能量来判断"远端是否活动"
- 即使 REF 很弱，只要非零，NLP 仍可能触发抑制
- Round 9 证明 NLP 抑制是阈值行为（有/无），不是线性比例于 REF 幅度
- 预期效果不确定，可能仍有 20+dB 抑制

### 方案 B：禁用 AEC + 降低环路增益（推荐）

思路：完全避免 NLP 问题，通过降低 MIC PGA 和输出音量使开环增益 < 1。

从 Round 1 数据推算开环增益组成：
```
开环增益 = 声学耦合 × MIC_PGA × AFE通过率 × 软件增益 × 输出音量
```

Round 1 实测：在 PGA=48dB, VOL=100%, GAIN=1 时，环路增益 ≈ 1.3-2×
其中 AFE(NSNet2 only) 通过率 ≈ 0.35（-9dB）

要使环路增益 < 1，需额外衰减 > 6dB（保守取 10dB 安全余量）。

**降 PGA 是最有效的杠杆**：
- 48dB → 30dB = -18dB 环路增益降低
- 允许软件增益恢复到 3×（+10dB）仍保持 -8dB 安全余量
- 人声在 30dB PGA 下仍可被采集（只是数字幅度降低 8×）

参数计算：
- PGA=30dB：环路增益 -18dB（8× 衰减）
- VOL=80%：环路增益 -2dB
- GAIN_MAX=3：环路增益 +10dB
- 净变化：-18 -2 +10 = -10dB（相对 Round 1 降低 10dB）
- Round 1 环路增益约 +3dB → 新增益 = 3-10 = -7dB → **稳定，余量充足**

人声预估：
- PGA=30dB 正常说话距离：mic_rms ≈ 800（基于 Round 2 数据 6498/8）
- NSNet2 通过人声约 -3dB：out ≈ 600 RMS
- Gain=3：1800 RMS → peak ≈ 5000-6000
- VOL=80%：speaker 输出峰值 4000-5000 → **清晰可闻**

### 方案 C：AEC + 极低 REF_GAIN + 高增益

思路：AEC 启用但 REF_GAIN=1，NLP 抑制可能减弱，同时保留部分防啸叫能力。

问题：
- NLP 行为不可控/不可预测
- 可能仍有显著压制
- 如果 NLP 确实减弱，防啸叫效果也同步减弱
- 两个目标相互矛盾，没有稳定平衡点

## Round 3 参数与结果

**方案 B：禁用 AEC + 降 PGA 防啸叫**

| 参数 | Round 2 值 | Round 3 值 | 变更原因 |
|------|-----------|-----------|---------|
| AEC | enabled | **disabled + reset** | 消除 NLP 对人声的 25-33dB 压制 |
| MIC_PGA | 48dB | **30dB** | 降低环路增益 18dB，防止无 AEC 时啸叫 |
| OUTPUT_VOL | 100% | **80%** | 额外 2dB 环路余量 |
| GAIN_TARGET | 16000 | 16000 | 保持 |
| GAIN_MAX | 6 | **3** | 限制环路增益上限（+10dB），确保稳定 |
| REF_GAIN | 4 | 4 | 保持（AEC disabled 时 REF 仍 feed 但不使用） |
| NS | NSNet2 | NSNet2 | 保持降噪，对人声衰减仅 2-3dB |

### Round 3 测试结果

实测现象：
1. 无人声时：大部分安静，但**时不时有啸叫爆发**
2. 人声较小：基本不能辨认，有明显啸叫
3. 人声较大：可辨认人声，但**杂音严重，有啸叫**

日志数据特征——典型的"爆发-衰减"间歇振荡：
```
17:13:21 mic=7189  out=3210  peak=19550 gain=1  ← 爆发
17:13:24 mic=128   out=17    peak=9388  gain=1  ← 衰减
17:13:27 mic=8319  out=3014  peak=19282 gain=1  ← 再次爆发
17:13:30 mic=121   out=18    peak=9259  gain=1  ← 再次衰减
```

peak 频繁达到 19000-27000（接近 clipping），"安静"时 peak 仍为 3000-9000。

### 根因修正：NSNet2 对语音/噪声通过率不同

原始估算错误点：使用了 NSNet2 对噪声的通过率（35%）来计算环路增益。
实际上 NSNet2 对不同信号通过率差异巨大：
- 噪声/啸叫信号：~35%（Round 1 推算值）
- **人声/语音信号：~60%**（NSNet2 设计目的就是保留语音、抑制噪声）

修正后环路增益（PGA=30dB, VOL=80%）：
- 声学耦合系数 C ≈ 2.1（从 Round 3 数据反推：gain=1 时边界振荡 → C×0.6×1×0.8 ≈ 1）
- gain=3 时：2.1 × 0.6 × 3 × 0.8 = **3.0** → 严重不稳定
- gain=1 时：2.1 × 0.6 × 1 × 0.8 = **1.0** → 临界（实测确认间歇振荡）

### 方案 B 的根本限制

| PGA | VOL | GAIN | 环路增益(语音) | 结果 |
|-----|-----|------|--------------|------|
| 30dB | 80% | 3 | 3.0 | 严重啸叫 |
| 30dB | 80% | 1 | 1.0 | 临界振荡（实测确认）|
| 30dB | 50% | 1 | 0.63 | 稳定但输出极弱 |
| 48dB | 40% | 1 | **4.0** | 严重啸叫 |
| 48dB | 10% | 1 | 1.0 | 临界（实用价值为零）|

结论：**方案 B 不可行**。在 MIC-Speaker 同机壳强耦合条件下，无回声消除的纯增益管理无法同时满足"不啸叫"和"人声可闻"两个需求。

ES7210 PGA 寄存器：
- 48dB = 0x60
- 30dB = 0x3C
- 计算：30dB / 0.5dB-per-step = 60 = 0x3C

## 备选方案 D：手动回声相减（利用硬件回采通道）

**思路**：不使用 ESP AFE 的 AEC（避免 NLP），而是在 fetch 阶段用 REF 通道信号做简单的延迟对齐 + 比例相减，手动消除回声分量。

```
output = afe_out - alpha * ref_delayed
```

**原理**：
- REF 通道（ES7210 slot2）采集的是 speaker 的电气回采信号（经衰减网络）
- MIC 拾取的回声 = REF 信号经声学路径后的延迟+衰减版本
- 如果能对齐延迟并估计衰减系数 alpha，直接相减即可消除回声

**实现要点**：
1. 延迟估计：声学路径延迟 = 物理距离/声速 + ADC/DAC pipeline 延迟，预计 2-5ms（32-80 samples @16kHz）
2. alpha 系数：REF → MIC 的衰减比例，需实测标定（播放已知信号，测量 MIC 响应）
3. 环形缓冲区：保存最近 N 帧 REF 数据用于延迟对齐
4. 自适应：alpha 可能随频率变化，简单实现用固定值，复杂实现用 LMS 自适应滤波

**优点**：
- 利用了硬件回采通道（不浪费 ES7210 MIC3）
- 无 NLP 副作用，不会压制人声
- 可精确控制回声消除强度

**缺点**：
- 需要手动标定延迟和系数
- 简单比例相减对非线性失真无效（speaker 在高音量下可能失真）
- 频响不平坦时单一 alpha 不够精确
- 实现复杂度高于 Round 3 方案

**适用场景**：如果 Round 3（纯降增益）的残余回声不可接受，但 AEC NLP 又压制人声，则本方案是中间路线。

**实现优先级**：待 Round 3 测试结果评估后决定是否启用。

## Round 4（方案 D 首版：回声相减 post-AFE）

参数：AEC disabled, PGA=48dB, VOL=80%, GAIN_MAX=4, ECHO_DELAY=512, ECHO_ALPHA=3/10
回声相减位置：**fetch_task（AFE 输出之后）**

结果：
- 无人声时，持续啸叫，播放声音极大
- 日志：`pre=179 post=9608`（相减后反而放大了信号）

根因：
- NSNet2 输出已被大幅衰减（pre=179），但 REF 原始值仍然很大
- 两者量级不匹配：`output = afe_out(小) - alpha × ref(大)` → 结果为大负数 → 绝对值暴增
- 回声相减必须在**量级匹配**的信号之间进行

结论：回声相减位置错误，应移到 AFE 之前对原始 MIC 操作。

## Round 5（方案 D 修正：回声相减 pre-AFE，delay=560）

参数：AEC disabled, PGA=48dB, VOL=80%, GAIN_MAX=4, ECHO_DELAY=560, ECHO_ALPHA=5/10
回声相减位置：**feed_task（AFE 之前，对原始 MIC 数据）**

结果：
- 无人声时有很大啸叫（比 Round 4 好一些）
- 人声小时无法辨认，啸叫严重
- 人声大时可勉强辨认一点人声，啸叫严重

日志关键数据：
```
[feed] raw=7713  clean=10713 ref=13523   ← clean > raw！
[feed] raw=10589 clean=15811 ref=20285   ← 越减越大
[feed] raw=27061 clean=30993 ref=31893   ← 饱和
```

根因：**延迟估算完全错误**
- ECHO_DELAY=560（35ms）读取的是 35ms 前的 REF，与当前 MIC 中的回声完全不相关
- 实际上 REF（ES7210 MIC3）和 MIC1 在同一 TDM 帧同步采样
- REF 捕获 speaker 当前电气输出；MIC 的回声 = speaker 当前输出经空气传播（~5cm，延迟仅 ~5 采样点）
- 真实延迟 ≈ 0-5 采样点，不是 560

数学验证：减去不相关信号 → `RMS(A-B_uncorrelated) = sqrt(A²+B²) > A`
- raw²+ref²×alpha² = 7713²+13523²×0.25 = 59.5M+45.7M = 105.2M → sqrt ≈ 10260 ≈ 实测 10713 ✓

结论：延迟必须修正为 ≈0。

## Round 6（方案 D：delay=0 同帧相减）— 待测试

参数：

| 参数 | Round 5 值 | Round 6 值 | 变更原因 |
|------|-----------|-----------|---------|
| ECHO_DELAY | 560 | **0** | REF 与 MIC 同帧采样，无需延迟 |
| ECHO_ALPHA | 5/10 | **6/10** | 实测耦合比 raw/ref≈0.57，取 0.6 |
| PGA | 48dB | 48dB | 保持 |
| VOL | 80% | 80% | 保持 |
| GAIN_MAX | 4 | 4 | 保持 |
| AEC | disabled | disabled | 保持 |

核心修正：
```c
mic_clean = mic1 - ref * 6/10    // delay=0，直接用同帧 REF
```

预期：
- 同帧 REF 与 MIC 回声高度相关 → 相减有效，clean < raw
- alpha=0.6 ≈ 实际耦合比 → 环路增益大幅降低
- 人声只在 MIC（不在 REF）→ 人声不受影响

状态：跳过，用户决定直接进入 Round 7（新方案）。

## Round 7-12（2026-07-02：延迟缓冲 + 噪声门方案）

用户补充需求：可以接受播放的音频晚于录音几十到几百ms。
基于此约束，采用全新方案：100ms 延迟缓冲打破瞬时反馈 + 噪声门控制。

### Round 7（纯 NS + 延迟缓冲 — 基线）

参数：AEC disabled, NS=NSNet2, PGA=48dB, VOL=60%, GAIN_TARGET=8000, GAIN_MAX=2, Delay=1600 samples(100ms)

结果：
- 无明显啸叫（偶发一下）✓
- 人声可录制播放，较清晰辨认，无明显杂音 ✓
- **人声播放音量较低**（比音源低不少）
- 安静时 mic_rms≈100-126, out_rms≈25-28, peak≈127
- 说话时 mic_rms≈962-5463, out_rms≈713-3788, peak≈9562-28031

分析：gain 被 peak 压到 1（peak>8000），加上 VOL=60%，输出音量不足。

### Round 8（提升增益）

变更：OUTPUT_VOL=80%, GAIN_MAX=4
结果：**啸叫严重**。启动即自激（安静时 mic_rms=7726, out_rms=2986）。
原因：VOL=80% + GAIN=4 环路增益远超 1。

### Round 9（最大音量零增益）

变更：OUTPUT_VOL=100%, GAIN_MAX=1
结果：**比 Round 8 更严重**。安静时 mic_rms=8261-16265。

关键发现：**`esp_codec_dev_set_out_vol` 是 dB 级控制**，100% vs 60% 差距远超线性。
**结论：VOL 必须保持 ≤60%，否则裸声学耦合本身就超过自激阈值。**

### Round 10（噪声门）

变更：VOL=60%, GAIN_MAX=3, 新增噪声门 (open>400, close<200, hold=5 chunks)

结果：
- 安静时无啸叫（gate=CLOSED，完全静音）✓
- 说话时人声音量有提升 ✓
- **尾音仍触发啸叫**（门 hold 期间回声超过 close 阈值，门不关闭→反馈持续）

### Round 11（噪声门 + 回声相减 + 自校准）

变更：新增 speaker_history 环形缓冲 + echo_loop_delay 自动校准 + alpha=0.6 相减

结果：
- 校准脉冲（±12000 方波）在 MIC 中 max_val=365，低于阈值 500 → **校准失败**
- 使用 default 延迟，计算 bug 导致 echo_loop=3760（235ms，过大）
- 延迟对齐错误导致**多次回放效应**（回声相减制造新振荡）
- **结论：单 tap 固定延迟回声相减太脆弱，延迟精度要求极高，放弃此方案**

### Round 12（噪声门 + 冷却期）

变更：去掉回声相减，门关闭后加入 cooldown=10 chunks（~320ms），期间不允许重新开门

参数：
- VOL=60%, GAIN_MAX=2, GAIN_TARGET=16000
- Delay=100ms, NS=NSNet2, AEC=disabled
- Gate: open>500, close<250, hold=3, cooldown=10
- Peak 衰减加速（/64 代替 /128）

信号链：MIC → AFE(NS) → delay(100ms) → gate(+cooldown) → gain → speaker

设计思路：
- 噪声门安静时彻底断开环路（R10 已验证有效）
- 冷却期防止门关闭后回声重新触发（解决 R10 尾音问题）
- GAIN_MAX=2（与 R7 相同，已验证环路稳定）
- open 阈值提高到 500（排除低能量回声误触发）

实测结果（2026-07-02）：
- 安静时无啸叫 ✓
- 说话时人声清晰 ✓
- 尾音不再触发持续啸叫 ✓（cooldown 有效）
- 音量偏低（GAIN_MAX=2 限制）
- 用户评价 6 点观察，确认方向正确

### Round 13（AEC 启用验证）

目的：验证 ESP AFE AEC 在连续透传下的表现（对比 Round 12 的 AEC-disabled 方案）

变更：`enable_aec=true`（启用 AFE 的 AEC + NLP），其余参数不变

实测结果：
- 无啸叫 ✓
- **人声被严重压制**（主观听感接近静音，仅偶尔漏过极短音节）
- 与 AEC Test Round 9 结论一致：NLP 在 passthrough 场景持续压制人声 25-40dB

**最终结论：AEC（含 NLP）不适用于自透传场景。** 原因分析：
1. NLP 设计目标是消除"近端说话人之外的所有声音"
2. 在 passthrough 中，speaker 播出的恰好是 MIC 采集的人声，对 NLP 来说这是"回声"
3. NLP 无法区分"speaker 回声"和"直接入射人声"（两者频谱相同）
4. 结合 AEC Test Round 8/9 数据：NS 仅贡献 2.7dB 衰减，NLP 贡献 ~27dB 衰减

### Round 14（噪声门 + 冷却期 + 播出缓存回声对消）

变更：在 Round 12 基础上新增"played_ring"数字回声对消 + 提升增益

核心思路（用户提出）：
- 缓存实际播出的数字信号到环形缓冲区
- 在 MIC 输入端减去延迟对齐的播出信号（精确数字值，无通道误差）
- 优势：参考信号精确（直接是播出的 PCM 值）、不误伤人声、无 NLP 副作用
- 与 Round 11 的单 tap 方案区别：alpha 极小（0.03），只削弱回声而非完全消除，容错性高

参数：
- VOL=60%, GAIN_MAX=3, GAIN_TARGET=20000
- Delay=100ms (1600 samples), NS=NSNet2, AEC=disabled (init true + runtime disable)
- Gate: open>500, close<250, hold=3, cooldown=10
- **Echo cancel: played_ring=4096 samples, delay=1280 samples(80ms), alpha=3/100(0.03)**

信号链：MIC - echo_sub(played_ring×0.03, 80ms delay) → AFE(NS) → delay(100ms) → gate(+cooldown) → gain(max3) → speaker → played_ring

实测结果（2026-07-02）：
- 安静时无啸叫 ✓
- 人声可辨认，有延迟播放 ✓
- 播放尾音略有回声感
- 连续/大音量人声时偶发轻微啸叫（不影响辨认）
- **echo_sub=1-4（几乎无效）**，原因：ECHO_DELAY=80ms 与实际环路延迟不匹配
- 人声播放音量远低于音源

### Round 15（修正回声延迟 80ms→32ms）

变更：ECHO_DELAY 1280→512 samples（80ms→32ms），其余不变

实测结果（2026-07-02）：
- 尾部啸叫有改善（小音源基本消除，大音源仍有一些）
- 音量仍低
- **echo_sub 仍≈0，且出现负值（-4/-6）**：32ms 延迟仍不匹配
- 负值说明在减去不相关信号，随机波动

### Round 16（缩短延迟 + 提升 alpha + 提升音量）

变更：ECHO_DELAY 512→160(10ms)，ECHO_ALPHA 3/100→10/100，GAIN_TARGET 20000→26000

实测结果（2026-07-02）：
- 音量有增加
- 啸叫无恶化，触发概率更低
- 回音比 R15 略严重（echo_sub 加噪效果）
- **echo_sub 全为负值（-10/-7/-25/-24）**：alpha=0.10 放大了不相关信号的加噪效果
- 确认单 tap 方案在所有延迟值下均无效

**played_ring 单 tap 方案最终结论：**
- 80ms/32ms/10ms 三个延迟值全部无法对齐
- 根因：单 tap（一个标量 alpha × 一个延迟点）无法匹配真实声学路径的频率响应
- 真实回声经过 DAC→功放→扬声器→空气→MIC→ADC 多级频率变换，不是简单的"延迟+缩放"
- 正规 AEC 用上百 tap 的自适应滤波器才能学到传递函数
- **结论：在当前硬件条件下，单 tap played_ring 方案不可行，放弃**

### Round 17★（关闭 echo_sub + 提升音量 — 当前代码/当前最优）

变更：ECHO_ALPHA 10/100→0（关闭 echo_sub），GAIN_TARGET 26000→28000

参数：
- VOL=60%, GAIN_MAX=3, GAIN_TARGET=28000
- Delay=100ms, NS=NSNet2, AEC=disabled
- Gate: open>500, close<250, hold=3, cooldown=10
- Echo cancel: **OFF**（alpha=0）

信号链：MIC → AFE(NS) → delay(100ms) → gate(+cooldown) → gain(max3, target=28000) → speaker

实测结果（2026-07-02）：
- 安静时无啸叫 ✓
- 音量略有提升（仍低于音源）
- 啸叫基本不出现，仅尖锐音源尾部偶发极轻微啸叫 ✓
- 尾音拖长感有（100ms 延迟环路固有特性，非 bug）
- 用户评价：效果相对不错，达到当前硬件条件下的合理水平

**音量受限根因：** peak 频繁达到 20000-30000，GAIN_TARGET=28000 时实际增益仅 1-1.5×，远低于 GAIN_MAX=3 上限。VOL=60% 为硬性天花板，无法进一步提升。

**已知问题（2026-07-02 补充）：** 无人声时若环境中出现较大声音（拍桌、音乐、车辆等），可触发啸叫。原因：噪声门只看能量（peak>500 即开门），无法区分人声和环境大声；环境音打开门后 speaker 回声叠加可能维持门不关闭，形成正反馈。这是纯能量门控方案的结构性局限。

## 关键结论汇总

1. **VOL ≤ 60% 是硬性边界**：超过此值裸耦合即自激（`esp_codec_dev_set_out_vol` 为 dB 级）
2. **声学耦合系数约 3%**（VOL=60%，PGA=48dB 条件下）
3. **噪声门是最有效的防啸叫手段**（安静期彻底断开环路）
4. **100ms 延迟缓冲用户可接受**
5. **单 tap 回声相减不可靠**：无论延迟对齐还是 played_ring 方式，单标量乘法无法匹配多级声学传递函数
6. **NLP 在 passthrough 场景不可用**：持续压制人声 25-33dB
7. **NSNet2 对语音通过率约 60%**（非对噪声的 35%），影响环路增益计算
8. 生产方案（enable_aec/disable_aec 切换）适用于非连续播放场景
9. **played_ring 单 tap 方案已确认失败**：80ms/32ms/10ms 全部不相关或反向加噪

## 可能的优化方向（后续）

1. **VAD 辅助开门**：门打开前先过 ESP AFE VAD，只有检测到语音特征才真正开门，避免环境大声误触发啸叫（当前 VAD=disabled）
2. **提高开门阈值 / 自适应阈值**：静态方案可提高 GATE_OPEN_THRESH（500→800+），代价是小声人声需更大声；动态方案根据近期噪声底线自适应调整 open/close 阈值
3. **双麦克风波束成形**（MIC2 修复后）：空间滤波可抑制 speaker 方向回声，从根本上降低环路增益，允许更高输出音量
4. **缩短延迟缓冲**（100ms→60ms）：减少尾音拖长感，但需验证啸叫风险
5. **GAIN_MAX=4 试探**：在门控保护下继续推高增益上限
6. **频域自适应回声消除**：多 tap FIR + LMS 自适应，替代单 tap 标量方案（复杂度高，可能超出 ESP32-S3 实时能力）
7. **硬件优化**：增加 speaker 与 MIC 物理隔离（结构件/吸音材料），降低声学耦合系数
