# xiaozhi_ai_demo 调试分析日志

本文件记录小智 demo 在完整实机验证前发现的现象、候选根因、对照依据、修复动作和验证结果。目标是避免只依赖会话上下文导致问题反复。

## 2026-07-10 Round 17

### 用户反馈

- 唤醒词可以反复触发。
- 唤醒词提示音音量极小。
- 唤醒后，AI 对话内容没有得到响应。
- 串口日志：`xiaozhilog/17.txt`。
- 可靠参考代码：`参考资料/09_参考项目/小智平台接入参考代码`。
- 需求口径：AI 对话链路尽可能与可靠参考代码保持一致。

### 已确认事实

- WakeNet 可以触发，日志多次出现 `WAKENET_DETECTED`。
- AFE 初始化后 `AEC disabled` 和 `AFE buffer reset` 生效，唤醒词问题已不再是当前主因。
- `version_check` 返回 HTTP 200，响应中包含 `websocket.url` 和 `token`。
- WebSocket 可以连接并收到 server hello，日志出现 `Session opened successfully`。
- 第一轮会话在 `listen start` 后只记录 `Audio TX: 1 frames sent`，随后出现 `transport_poll_write(0)`、`WS error`、`WS disconnected`。
- 当前代码会发送 `listen start`，并且在第二轮会话中发送了 40 帧上行音频后发送 `listen stop`；但当时 `s_tx_frame_count` 在真正写 WebSocket 前递增，`sent 40 frames` 不能证明 40 帧都成功写入网络。
- 第二轮进入 `PROCESSING` 后 15 秒未收到 `tts`/`stt`/`goodbye`，最终由本地超时关闭。
- 日志中仍出现 `lsd_net_mgmt: network quality F Bad` 和 `Network quality CRITICAL! Triggering switch...`，说明虽然业务层禁用了 WiFi 启动，闭源网络管理仍在会话期间触发质量评估/切换动作。
- `dingding.wav` 为 16kHz、mono、16-bit，时长约 371ms，峰值约 60.24%，RMS 约 17.97%，素材本身不属于低电平音频；提示音小更可能来自播放器链路、输出音量或播放期间 AEC/codec 状态。
- 当前生效源码来自 `CODE/components/xiaozhi_agent` 与 `CODE/components/xiaozhi_audio`；`xiaozhi_ai_demo/main` 下残留同名 `xiaozhi_agent.c` 和 `xiaozhi_audio.c` 未被 `main/CMakeLists.txt` 编译，排查时不得误读为当前生效实现。

### 初始候选根因

1. WebSocket 二进制音频帧封装、listen 消息或会话状态机与参考代码仍存在协议级差异，导致服务端没有识别有效语音。
2. 唤醒提示音播放时启用 AEC 或占用 codec，影响后续采集/发送；提示音音量极小也可能来自播放器链路或输出音量设置。
3. `lsd_net_mgmt` 在会话期间触发网络切换或重配，导致 WebSocket 写失败或服务端处理被中断。
4. VAD 结束策略过早或未对齐参考代码，导致上行语音截断。

### 对照依据

- 参考代码 `agent_xiaozhi_ws_protocol.c` 中 `Protocol-Version=3` 使用 4 字节二进制头：`type=0`、`reserved=0`、`payload_size=htons(len)`。
- 参考代码 `liot_ai_audio_promt.c` 的 prompt 播放逻辑同样使用 `esp_audio_simple_player`、pre-roll silence 和 fade-in。
- 参考代码 `xiaozhi_app.c` 通过 agent 的输入守卫处理唤醒、VAD end、TTS 打断和 prompt 播放；当前 demo 是手写简化状态机，需要继续逐项对齐。

### 待验证问题

- 当前 `lsd_network_mgmt_init(true)` 是否有 4G-only 配置或是否必须替换为更直接的 4G 网络初始化链路。
- 当前提示音文件本身峰值/响度是否低，还是 `esp_codec_dev_set_out_vol()` 或播放器输出链路问题。
- 服务端是否期望 server hello 返回的 `audio_params.sample_rate/frame_duration` 被客户端记录并用于下行解码或日志校验。
- 当前 `PROCESSING_TIMEOUT_MS=15000` 是否过短，或只是掩盖了服务端根本没有响应的问题。

## 2026-07-10 Round 17 方案 A 实施记录

### 已落实到代码

- `CODE/components/xiaozhi_agent/xiaozhi_agent.c`：补充上行音频发送成功/失败统计，将 `s_tx_frame_count` 改为成功发送帧数；`ws_send_audio_frame()` 校验参数、帧长和完整发送结果；连续发送失败达到阈值后关闭 session；`PROCESSING`/`SPEAKING` 收到 wake 时对齐参考代码，发送 abort、停止音频、重新进入 listening；补充 server hello 的 `audio_params`、未知文本消息、非预期下行音频等日志。
- `CODE/components/xiaozhi_audio/xiaozhi_audio.c`：`OUTPUT_VOL` 提升到 80；prompt 播放前显式设置输出音量；prompt 播放不再启用 AEC，prompt stopped/finished/error/stop 后统一 disable AEC、reset AFE buffer、清理 `s_was_speaking`；TTS 首帧仍保留启用 AEC，TTS stop 后仍 disable AEC + reset buffer，用于播放回声抑制和语音打断场景。
- `CODE/examples/xiaozhi_ai_demo/main/xiaozhi_ai_demo_main.c`：增加网络状态日志，在 state change、wake、VAD end、network switch、network ready 等节点打印默认网卡和 ready 状态，辅助判断 `lsd_net_mgmt` 是否导致 WebSocket 写失败或默认网卡变化。

### 当前未完成验证

- 未完成 clean 构建。
- 未执行全片擦除。
- 未烧录到设备。
- 未进行实机唤醒词、提示音、上行音频、服务端响应、TTS 下行全链路验证。

### 中断与进程状态

- 曾按记录入口尝试启动 `CODE/tools/build_example.sh xiaozhi_ai_demo clean`；PowerShell PATH 中无 `bash`，随后使用 `C:\Program Files\Git\bin\bash.exe` 启动构建。
- 构建启动后用户要求暂停并重启窗口，本轮不继续构建。
- 中断后曾有残留构建相关进程，已终止；恢复时再次精确检查 `build_example|idf.py|ninja|esptool|xiaozhi_ai_demo|laiwfs300_build|xtensa-esp|collect2`，未发现构建链条残留。

### 重启后下一步

- 重新读取 `coding_rule.md` 和本项目规则后再继续。
- 复查 `git diff` 中本轮三个目标文件的改动，确认没有混入无关改动。
- 在用户重新授权后，按 `design.md` 记录入口执行 clean 构建；烧录前必须先全片擦除。
- 串口日志继续由用户手动提供，AI 不主动抓串口。

## 2026-07-10 Round 19 AEC/播放期打断分析

### 用户反馈

- 唤醒词可以反复起效。
- 智能体回复时无法被唤醒词打断。
- 语音音量足够，后续希望改成 50。
- 唤醒词提示音音量明显低于语音，不容易听见。
- 本轮没有异常重启。
- TTS 声音已恢复为女声，说明 24kHz TTS 下行到 16kHz 播放的重采样修复有效。
- 用户补充判断：代码中已有 AEC，理论上应对播放期打断起效；提高唤醒词提示音音量也无效，说明仍存在更深层问题。
- 串口日志：`xiaozhilog/19.txt`。

### 已确认事实

- 空闲状态 WakeNet 正常，日志多次出现 `WAKENET_DETECTED`，例如 fetch #432、#969、#1236、#1320、#5421。
- 播放 TTS 期间用户说话时，日志出现多次 `VAD end detected` / `notify_vad_end called, current state=5`，但没有出现 `WAKENET_DETECTED`。
- 播放 TTS 期间 AEC 开关确实被打开，日志有 `audio_processor: AEC enabled (ret=1)`；TTS stop 后也有 `AEC disabled` + `AFE buffer reset`。
- 播放期 AFE 能检测到语音活动，例如 `fetch #5600: vad=1 wakeup=0 data_size=1024`，说明问题不是麦克风完全听不到用户，而是 WakeNet 特征未命中。
- 未见异常重启证据，启动日志为 `Reset reason: POWERON (1)`。
- 服务端下行 `audio_params.sample_rate=24000`，本地日志显示 `TTS playback resample: decoder=24000 Hz -> codec=16000 Hz`，女声音色恢复与该链路一致。
- 当前小智 feed 日志中，TTS/非 TTS 期间 `ref_e` 基本只有几十量级，例如 `feed #2800: mic1_e=17787 mic2_e=12126 ref_e=49 chunk=1024`。同期 `mic1_e/mic2_e` 经常为几千到几万。

### 历史 AEC 记录对照

- `project_memory.md` 曾记录当前硬件回采路径为 `POUT_P/N -> 衰减网络 -> ES7210 MIC3_P/N -> TDM slot1`，读取方式为 4ch TDM raw read。
- `audio_aec_test` / `audio_aec_demo` 历史实测不是纯文档推断：`aectestlog/14-1.txt` 中 `REF_GAIN=4` 时播放 tone 阶段出现 `ref_rms=17423/17656/12561`，说话段也有 `ref_rms=4540/4311`。
- `aectestlog/17-1.txt` 中 `REF_GAIN=4` 且 100% 输出音量时，Pass B 说话段记录 `ref_rms=4430/4576`，峰值接近满量程。
- 历史 `audio_aec_test` feed 顺序为 `[slot0 MIC1, slot2 MIC2, slot1 REF]`，并对 slot1 REF 应用 `REF_GAIN=4` 后送入 AFE。
- 当前小智 `xiaozhi_audio.c` feed 顺序同样是 `[slot0 MIC1, slot2 MIC2, slot1 REF]`，映射形式与历史验证一致，但没有对 REF 应用 `REF_GAIN=4`。

### 当前判断

- 不能只用“已调用 enable_aec”判断 AEC 有效；AEC 必须同时满足参考通道有足够、同步、未削波且映射正确的扬声器回采。
- 当前最可疑点是小智 demo 的 AEC reference 输入链路没有达到历史 AEC demo 的有效量级。即使考虑小智日志只统计前 64 个样本绝对值、不是全窗口 RMS/peak，播放期 `ref_e≈49` 与历史 `ref_rms=4k~17k` 的差距仍然过大。
- 单纯提高唤醒词提示音音量不能解决智能体回复期间打断失败，因为失败发生在 TTS 播放期 WakeNet 特征识别，核心影响因素是 TTS 声学回声是否被 AEC 正确消除或至少不过度污染 WakeNet。
- 上层 agent 已具备 `SPEAKING` 状态收到 wake 后 abort + stop audio + listen 的逻辑；当前播放期没有 wake 事件进入上层，因此首要问题在音频前端输入质量或 WakeNet/AEC 运行策略。
- 参考小智项目存在 `liot_ai_audio_set_keep_awake()`，底层调用 `esp_gmf_afe_keep_awake()`；当前自研 `audio_processor` 没有等价接口。该差异可能影响会话期间 WakeNet 是否持续保持唤醒检测状态，需要作为独立假设验证，不能只归因于 REF。

### 候选根因排序

1. **P0：AEC REF 幅度/统计/增益未对齐历史验证参数。** 当前小智没有 `REF_GAIN=4`，且没有全窗口 RMS/peak 观测；若 REF 只是偏弱，应用历史参数和确认 RMS 后应改善播放期 WakeNet。
2. **P0：slot1 REF 当前实际没有有效回采。** 历史 demo 证明硬件曾可产生强 REF，但当前小智日志显示 ref 近似底噪；若插桩确认 slot1 raw RMS 在 TTS 播放时仍接近 0，则应回到 ES7210 MIC3/slot1 初始化、I2S reconfig、播放链路是否同一路 POUT 回采排查。
3. **P1：WakeNet 在播放/会话期间缺少 keep_awake 等价控制。** 参考代码在会话开始/停止调用 `liot_ai_audio_set_keep_awake()`，当前没有该接口；若 REF 确认可用但播放期仍不报 wake，应重点验证该假设。
4. **P1：AEC 启用时序晚于 TTS 声音进入扬声器，导致 AEC 未及时收敛。** 当前在收到首个 TTS opus 帧时 enable AEC，然后排队解码播放；如果参考通道有效但前几秒不稳定，需观察 TTS start、AEC enable、实际写 codec、fetch wake 的时序。
5. **P2：提示音播放链路与 TTS 播放链路音量/格式不同。** 这解释提示音小，但不能解释 TTS 播放期间 WakeNet 无法打断，因此不是播放期打断的首要根因。

### 下一轮建议

- 暂不直接改状态机或做 VAD 兜底打断；先做音频链路插桩，避免把 WakeNet 失败掩盖成上层策略问题。
- 在 `xiaozhi_audio.c` feed_task 增加按播放状态统计 slot0/slot1/slot2/slot3 的全窗口 RMS/peak，单独打印 slot1 raw REF 与应用 `REF_GAIN=4` 后的 RMS/peak。
- TTS 播放开始后每约 1 秒打印一次上述统计，避免当前每 200 个 feed、只看前 64 样本导致错过强播放段。
- 增加一次启动或命令触发的 1kHz tone 回采自检，复用 `audio_aec_test` 的判断口径：播放 tone 时 slot1 REF 应明显非零，且与历史 `ref_rms` 同量级。
- 若 slot1 raw REF 有效，只是未应用 `REF_GAIN=4`，再按历史 `audio_aec_test` 参数对小智 feed 中 REF 做限幅增益补偿。
- 若 slot1 raw REF 无效，优先查 `board_audio.c` / ES7210 MIC3 初始化、I2S TDM reconfig 后 slot 是否变化、TTS/prompt 是否实际走 `ES8311 -> HT6872 -> POUT_P/N` 回采路径。
- 若 REF 有效且 AEC 后仍无法播放期唤醒，再补充 `audio_processor` keep_awake 等价接口或寻找 ESP AFE 原生 API，对照参考项目验证 WakeNet 会话期保持策略。

## 2026-07-10 Round 20 AEC slot 插桩验证

### 用户反馈

- 测试固件为 slot0/1/2/3/4 诊断插桩版。
- 测试现象：AI 播放回复时反复说唤醒词，没有打断效果。
- 串口日志：`xiaozhilog/20.txt`。

### 已确认事实

- 空闲期 WakeNet 正常触发：日志在 `fetch #939` 出现 `WAKENET_DETECTED`，随后进入 `listening`。
- 播放期没有 WakeNet 事件：`state=5 speaking` 的 TTS 播放阶段只有多次 `VAD end detected` / `notify_vad_end called, current state=5`，没有 `WAKENET_DETECTED`。
- 上层不是首要根因：`xiaozhi_agent` 已实现 `SPEAKING`/`PROCESSING` 收到 wake 后 `abort + audio_stop + listen.start`；本轮失败点在 `audio_processor_fetch()` 没有返回 `wakeup=true`。
- AEC 启用时序有效：TTS 播放首帧前后日志出现 `audio_processor: AEC enabled (ret=1)`，TTS stop 后出现 `AEC disabled` + `AFE buffer reset`。
- slot 插桩确认 TDM 当前为 4-slot：`slot0/slot1/slot2/slot3` 均有统计，`slot4=N/A` 是预期结果，不存在第五个 TDM slot。
- 播放期 `slot1` 不是完全无回采，但回采幅度弱且不稳定：
  - 空闲 `playing=0,prompt=0`：65 个窗口，`slot1_rms` 平均约 6.6，最大 236。
  - prompt `playing=0,prompt=1`：1 个窗口，`slot1_rms=151`，`slot1_x4_rms=604`。
  - TTS `playing=1,prompt=0`：14 个窗口，`slot0_rms` 平均约 2129，`slot2_rms` 平均约 1956，`slot1_rms` 平均约 156，`slot1_x4_rms` 平均约 624，`slot1_rms` 范围 1~304。
- 与历史 AEC demo 对比，Round 20 播放期 `slot1_x4_rms≈624` 明显低于历史 `audio_aec_test`/`audio_aec_demo` 有效回采量级（几千到一万多）。

### 参考代码对照

- 参考项目在输入被接受后调用 `liot_ai_audio_set_keep_awake(s_audio, true)`，会话停止时调用 `liot_ai_audio_set_keep_awake(s_audio, false)`。
- 参考项目 ESP32-S3 音频 port 中 `liot_ai_audio_set_keep_awake()` 最终调用 `esp_gmf_afe_keep_awake(afe_el, enable)`。
- 当前项目直接使用 `esp_afe_sr` 封装的 `audio_processor`，没有 GMF pipeline，也没有 keep_awake 等价接口。
- 因此 keep_awake 差异仍是独立候选根因，但 Round 20 的首要证据仍指向 REF 幅度/有效性不足：播放期存在强 MIC 信号和弱 REF 信号，WakeNet 无命中。

### 当前判断

1. **P0：当前小智 AFE 实际喂入的是 `slot1 raw`，没有应用历史验证过的 `REF_GAIN=4`。** 插桩只打印了 `slot1_x4` 统计，没有把增益后的 REF 喂给 AFE；播放期 `slot1 raw` 相对 MIC 明显偏弱，AEC 可能无法充分消除 TTS 回声，导致 WakeNet 特征被污染。
2. **P0：slot1 回采链路有效但不稳定。** 播放期 `slot1_rms` 有时回到 1，说明需要进一步确认 TTS 播放、ES8311 输出、HT6872/POUT 回采、ES7210 MIC3/TDM slot1 之间的时序和幅度稳定性。
3. **P1：缺少 keep_awake 等价策略。** 如果应用 `REF_GAIN=4` 后 REF 质量足够但播放期仍无 WakeNet，再优先验证 keep_awake 差异，而不是改上层状态机。
4. **P2：VAD 兜底打断不应作为当前修复。** 播放期 VAD 能触发但无法区分用户唤醒词和普通声音/回声，直接用 VAD 打断会掩盖 WakeNet/AEC 问题并增加误打断。

### 下一轮建议

- 不改上层 `xiaozhi_agent` 状态机；当前没有证据表明 wake 事件到达后被忽略。
- 在 `xiaozhi_audio.c` 的 `feed_task` 中将 AFE 输入第三路从 `slot1` 改为 `clamp_i16(slot1 * 4)`，与历史 `audio_aec_demo` / `audio_aec_test` 的有效参数对齐。
- 保留 `[DEBUG-AEC-SLOTS]` 一轮，用于验证应用 REF_GAIN 后播放期 WakeNet 是否恢复。
- 若仍失败，补充 1kHz tone 回采自检，复用历史 AEC demo 判断口径确认 slot1 回采在本 demo 中是否能达到同量级。
- 若 REF 有效且仍失败，再查 ESP AFE/GMF 的 keep_awake 等价 API 或考虑引入最小封装接口；不要先引入完整 GMF pipeline。

## 2026-07-10 Round 21 AEC 自动自检固件

### 目标

- 不继续修改智能体对话链路；Round 19/20 已证明空闲唤醒、平台回复、TTS 女声音色和上层打断分支基本成立。
- 单独验证 AEC reference 回采链路，避免网络、agent、VAD/WakeNet 会话日志干扰。
- 通过本地 1kHz tone + TDM 4-slot 统计，判断 `slot1` 是否能在小智 demo 工程上下文中达到可用 REF 量级。

### 已落实到代码

- `CODE/components/xiaozhi_audio/include/xiaozhi_audio.h`：新增 `xiaozhi_audio_run_aec_self_test()`。
- `CODE/components/xiaozhi_audio/xiaozhi_audio.c`：新增 `[AEC-SELFTEST]` 自检流程：
  - 初始化板级音频和 4-slot TDM raw read。
  - baseline 采样 1000ms。
  - 播放 1kHz tone 2000ms，同时采集 `slot0/slot1/slot2/slot3` RMS/peak。
  - recovery 采样 500ms。
  - 输出 `slot1_x4` 统计和 `verdict=PASS/FAIL`。
- `CODE/examples/xiaozhi_ai_demo/main/Kconfig.projbuild`：新增 `CONFIG_XIAOZHI_AEC_SELF_TEST`，Kconfig 默认关闭。
- `CODE/examples/xiaozhi_ai_demo/main/xiaozhi_ai_demo_main.c`：自检开关开启时，`board_laiwfs300_init()` 后直接运行 AEC 自检，跳过 LTE、网络、小智 agent、VAD/WakeNet 正常运行路径。
- `CODE/examples/xiaozhi_ai_demo/sdkconfig.defaults`：本轮临时启用 `CONFIG_XIAOZHI_AEC_SELF_TEST=y`，用于构建并烧录自检固件；恢复正常小智对话固件前必须关闭或删除该项。

### 判定口径

- 自检日志只看 `[AEC-SELFTEST]` 行。
- PASS 条件当前为保守阈值：
  - tone 阶段 `slot1_rms >= 100`。
  - tone 阶段 `slot1_x4_rms >= 2000`。
  - tone 阶段 `slot1_rms >= baseline_slot1_rms * 5`。
  - baseline/tone/recovery 三段无 TDM read error。
- 若 `slot1_x4_rms` 仍明显低于 2000，优先继续查回采链路有效性或小智 demo 与 `audio_aec_test` 的音频初始化差异。
- 若自检 PASS，但正常 TTS 播放期仍不能 WakeNet 打断，再进入 `REF_GAIN=4` 实际喂 AFE 或 keep_awake 等价策略验证。

### 构建与烧录

- 已按 `design.md` 4.1.2 三步流程执行：
  - Git Bash 全量镜像到 `%TEMP%\laiwfs300_build\CODE`。
  - 限制并发 `CMAKE_BUILD_PARALLEL_LEVEL=2`、`NINJAFLAGS=-j2` 后 clean 构建 `xiaozhi_ai_demo`。
  - `COM7` 全片擦除后烧录。
- Clean 构建首个命令因 120s 工具超时未返回，但 `ninja` 后台继续完成；随后用同一 `build_example.ps1 -Example xiaozhi_ai_demo` 入口确认构建完成。
- 自检固件 `xiaozhi_ai_demo.bin` 大小 `0x4ae80`，最小 app 分区剩余 93%。
- 烧录结果：bootloader、app、partition_table、srmodels、spiffs_data 均 `Hash of data verified`，最后 `Hard resetting via RTS pin... Done`。

### 待用户实机日志

- 当前设备最后烧录版本为 Round 21 AEC 自检固件，不是正常联网对话固件。
- 下一步需要用户抓取串口启动日志，重点提供 `[AEC-SELFTEST] phase=...` 和 `[AEC-SELFTEST] verdict=...` 行。

## 2026-07-13 Round 22 AEC 自检实测 + audio_aec_test 对比 + 根因定位

### Round 21 自检实测结果（VOL=60）

实机日志（MAC 44:1b:f6:f3:ae:7c，自检固件）：

| 阶段 | slot0(MIC1) rms | slot1(REF) rms | slot1_x4_rms | slot2(MIC2) rms | slot3 rms |
|---|---|---|---|---|---|
| baseline(静音) | 1076 | 157 | 629 | 1133 | 158 |
| tone(1kHz播放) | 5567 | 398 | 1595 | 6558 | 0 |
| recovery | 2602 | 186 | 744 | 3072 | 0 |

- `verdict=FAIL`：`abs_ok=1 ratio_ok=0 gain_ok=0 read_ok=1`
- 失败原因：`slot1_ref_not_following_tone`（ratio=398/157=2.5×，要求≥5×）、`slot1_x4_ref_too_low`（1595<2000）
- MIC(slot0/slot2)播放时暴涨 5×+，REF(slot1)只 2.5×；slot3(未接)播放时为 0，说明 REF 电气链路连通但信号弱。

### VOL 单变量对照自检（VOL=100）

同一自检固件仅把播放音量 60→100 重测：

| 指标 | VOL=60 | VOL=100 |
|---|---|---|
| baseline REF rms | 157 | 158（几乎不变） |
| tone REF rms | 398 | 1683 |
| ratio | 2.5× (FAIL) | 10.6× (PASS) |
| slot1_x4_rms | 1595 (FAIL) | 6735 (PASS) |
| verdict | FAIL | PASS |

结论：
1. REF 硬件正常。此前"REF 弱"是 VOL=60 播放太轻导致，不是硬件故障。
2. baseline 底噪固定 ~158（各通道 ADC 本底/固定串扰），与音量无关。REF 中真正跟随播放的成分 = tone−baseline：VOL=60 只有 240，VOL=100 有 1525。
3. REF 弱本质是 SNR 问题：VOL=60 时 SNR≈1.5，软件放大治不了（同等放大底噪，ratio 不变）。

### 硬件回采链路（A0 原理图 page7 确认）

- 播放链路：ES8311 DACOUT → HT6872(功放, Av=200k/RIN, RIN=12k→~18dB, 硬件固定不可运行时调) → POUT → 扬声器。AMP_CTRL(IOEX P1_0) 仅使能/关断。
- 回采抽头：**POUT_P/N → R45/R46(2.2kΩ) → 衰减网络 → ES7210 MIC3(slot1)**，抽头在**功放输出之后**。
- 板上预留 **DACOUT_P/N → R19/R20(0Ω, 标注 NC 未贴)** 备用抽头，在功放输入之前。
- 衰减网络：POUT→2.2k→[C82 10nF]→C77 470nF→10k→[2.2nF]→20k→[R88 4.3k]→0Ω→[100pF]→220nF→MIC3。
- **硬件绑定结论**：当前 POUT 抽头下 REF ∝ POUT ∝ 喇叭响度，三者被 POUT 节点锁死。功放增益改 RIN 会同时改喇叭响度和 REF，无法解耦。"VOL=60 安静 TTS + 有效 AEC"在当前硬件上无软件解，除非改板（减小衰减网络 / 改用 DACOUT 抽头+降功放增益+提数字 VOL）。

### audio_aec_test vs xiaozhi_audio 完整对比

`audio_aec_test` 是本项目实机验证 AEC "有效" 的例程；两者共用 `audio_processor`(AFE) 封装。

| 维度 | audio_aec_test | xiaozhi_audio(原) | 影响 |
|---|---|---|---|
| OUTPUT_VOL | 100 | 60 | 决定性，见上表 |
| feed REF 处理 | slot1×REF_GAIN(4) 后 feed | slot1 原值 feed（ref_gain 仅用于统计） | aec_test 进 AFE 的 REF 强 4 倍 |
| MIC PGA | 写 ES7210 REG17/18=0x60 | 板默认 30dB | REG0x17/0x18 非官方 PGA 寄存器（官方 MIC1-4 增益=0x43-0x46），aec_test 那段大概率无效；其强 REF 来自 VOL=100 |
| AFE mic_channels | 2 | 2 | 相同 |
| AFE ref_channels | 1 | 1 | 相同 |
| AFE enable_aec | true | true | 相同 |
| AFE aec_mode | 3 | 3 | 相同 |
| AFE enable_ns | true | true | 相同 |
| AFE enable_vad | false | true | 小智需 VAD |
| AFE enable_wakenet | 无此选项 | true | WakeNet 仅小智 |
| TDM slot 映射 | slot0=MIC1,slot1=REF,slot2=MIC2 | 相同 | 相同 |

### 根因（三层叠加）

1. VOL=60 播放轻 → REF 弱(398)，SNR≈1.5，AEC 无法跟踪播放。
2. feed 未乘 REF_GAIN → AFE 内部 REF 再弱 4 倍（aec_test 有乘）。
3. WakeNet 在播放期 AFE 输出上检测 → AEC 减不掉回声(因 1+2) → WakeNet 输入被 TTS 回声淹没 → 播放期检测不到唤醒词 → 打断失败。

audio_aec_test 能工作是因为：VOL=100(REF 强) + feed 前 REF×4 + 无 WakeNet（只测 AEC 抑制量，不做播放期唤醒）。

### 所有可能方案（穷尽，供选型）

软件侧（当前硬件）：
1. 提 VOL 到 AEC 可用最低值（VOL=100 已验证 PASS，需扫 70/80/90 找最低）。代价：TTS 变响。
2. feed 前给 REF 乘 REF_GAIN=4（对齐 aec_test）。单独在 VOL=60 下仍边缘（slot1×4=1595），需配合提 VOL。
3. 转参考代码 MM 方案（ref_channels=0, enable_aec=false, feed 只送 [mic1,mic2]，WakeNet 硬扛回声）。代价：依赖本板声学隔离，需先验证。

硬件侧（需改板）：
4. 减小回采衰减网络（改 R45/R46 或分压电阻），VOL=60 下让 REF 强 4~6 倍。风险：高 VOL 削波。
5. 回采抽头 POUT→DACOUT（贴 R19/R20、去 R45/R46）+ 降功放增益 + 提数字 VOL，真正解耦。复杂度高。

### 本轮采纳方案（用户 2026-07-13 确认）

组合方案 1+2：**VOL 60→70 + feed 前 REF×REF_GAIN(4)**。

已落实到代码：
- `CODE/components/xiaozhi_audio/xiaozhi_audio.c`：
  - `#define OUTPUT_VOL 70`（原 60）。
  - feed 循环 `feed_buf[i*3+2]` 从 `slot1` 改为 `ref_gain`（=slot1×SLOT_DIAG_REF_GAIN(4)，clamp 到 int16），对齐 audio_aec_test。
- 待办：恢复正常对话固件需关闭 `sdkconfig.defaults` 的 `CONFIG_XIAOZHI_AEC_SELF_TEST=y`（自检模式会跳过网络/agent，无法测真实播放期打断）。
- 预期：VOL=70 时 REF 跟随成分比 VOL=60 增强，再叠加 feed×4，AFE 内部 REF 达到可用量级；需实机验证播放期 WakeNet 能否打断。风险：VOL=70 仍可能不足（介于 60 FAIL 与 100 PASS 之间），需实测确认。

### VOL=70 自检实测（三档 VOL 曲线）

| VOL | baseline REF | tone REF rms | ratio | slot1_x4_rms | gain_ok | ratio_ok | verdict |
|---|---|---|---|---|---|---|---|
| 60 | 157 | 398 | 2.5× | 1595 | ✗ | ✗ | FAIL |
| 70 | 158 | 709 | 4.49× | 2839 | ✓ | ✗ | FAIL |
| 100 | 158 | 1683 | 10.6× | 6735 | ✓ | ✓ | PASS |

- VOL=70：`slot1_x4_rms=2839` 已过绝对阈值 2000（gain_ok=1），但 ratio=4.49× 差一点没到 5×（ratio_ok=0），verdict 仍 FAIL。ratio≥5 是自检的保守判据，非 AFE 硬性要求，故转入真实对话链路实测。

### VOL=70 + feed×4 真实对话实测（决定性，2026-07-13）

固件：自检关闭 + OUTPUT_VOL=70 + feed×4，MAC 2e:ee:8c:6b:58:28，4G 联网。

- **结果：播放期仍不能打断。**
- 直接证据：整段 TTS 播放期（`playing=1`，feed=953~1040，约 15s）**无任何 `WAKENET_DETECTED`**，fetch 全程 `wakeup=0`。唯一唤醒发生在播放开始前的 idle 阶段（fetch #1785）。上层 SPEAKING 打断分支因此从未触发。
- feed×4 确已生效：`feed #1000 ref_e=18948`（VOL=60 时 feed #800 仅 212）。
- 播放期 slot 数据（feed=1028）：`slot0(MIC1)=4623, slot1(REF)=507, slot1_x4=2029, slot2(MIC2)=3552`。REF 即使 ×4 仍只有 MIC 回声的 ~44%。
- 对话链路本身正常：唤醒→STT（"杭州的天气怎么样"）→TTS 女声回复→正常结束。

### 新增决定性发现：AEC 在弱 REF 下损害 WakeNet

同一固件两个状态对比：
- 空闲期 `AEC disabled`（日志 83393ms）→ 能唤醒（fetch #1785 唤醒即发生在 AEC disable 后）。
- 播放期 `AEC enabled`（日志 87357ms）→ 全程无唤醒。

结论：问题不只是"回声淹没唤醒词"，而是**在弱 REF 下启用 AEC 反而损害 WakeNet 输入频谱**（呼应历史记录"AEC 的 NLP 在 REF≈0 时错误抑制语音频段"）。这解释了为什么 REF 越强（VOL=100）自检越好、但真实对话中只要 AEC 开着 WakeNet 就失效。

### Round 22 最终结论

- AEC 调参空间（VOL 60/70/100 + feed×4）已穷尽。在当前硬件（回采抽头在 POUT、REF 与喇叭响度硬绑定、REF 相对声学回声过弱、且 AEC 在弱 REF 下损害 WakeNet）上，**靠 AEC 做播放期打断这条路走不通**。
- 这与参考代码放弃 AEC/REF、改用 MM 双麦（WakeNet 直接在含回声的双麦信号中检测）的设计选择一致。

### 下一步（用户 2026-07-13 确认方向）

1. **转 MM 方案**：AFE 配置改 `ref_channels=0`、`enable_aec=false`、format="MM"，feed 只送 `[mic1, mic2]`（对齐参考代码 `channel_allocation="MM"`, `BOARD_AUDIO_INPUT_REFERENCE=0`）。TTS 保持 VOL=70（或按需调回 60）。风险：能否成功依赖本板喇叭-MIC 声学隔离，参考板能成功不代表本板等同，需实机验证。
2. **WiFi/4G 切换加消抖**：本轮日志 `lsd_net_mgmt` 全程刷 `network quality F Bad! Triggering switch` + `Target WiFi has no IP`，WiFi/4G 反复评估切换。参照 lte_net_demo 已验证的上层消抖做法处理。
3. 构建、全片擦除、烧录、实测。

## 2026-07-23 会话结束后疑似未回到静默待机（待复现）

### 用户报告

- 唤醒词唤醒后可以正常完成 AI 对话。
- 一轮对话完成后，设备疑似没有稳定回到静默待机；曾出现没有再次说唤醒词、但设备突然回应外部人声的情况。
- 当前要求只记录，不修改 `xiaozhi_ai_demo` 功能源码；新的 `xiaozhi_companion_robot_demo` 必须关注并解决该问题。

### 当前证据状态

- 现象为偶发用户观察，当前缺少串口日志、服务端消息、发生时间点、复现次数和复现概率。
- 尚未建立可重复反馈 loop，不能确认现象发生在本地唤醒/VAD、Agent 状态机、WebSocket 会话或服务端对话状态中的哪一层。
- 对 `xiaozhi_ai_demo` 本身仍不形成根因结论，也不实施功能源码修复。
- 2026-07-28 关联状态：`xiaozhi_ai_demo` 功能源码仍未因本条风险修改；EX-024 已实现并烧录 IDLE 门禁、generation/wake sequence 防陈旧事件、直接 `LISTENING -> SPEAKING`、PROCESSING/SPEAKING 新唤醒和旧 `IDLE` 拒绝逻辑，但新增状态用例目前只有编译证据，尚未完成目标板对话与静默回归，因此该风险仍不能记为实机闭环。

### 后续复现口径

1. 说唤醒词并完成一轮正常对话，记录唤醒、聆听、VAD 结束、TTS 开始/结束、服务器 `goodbye` 和本地状态切换日志。
2. 确认本地已进入 `IDLE` 后，在观察窗口内持续播放或说出不含唤醒词的普通外部人声。
3. 若设备离开 `IDLE`、发送新的 `listen start/stop`、创建或延续对话会话、上传对话音频或产生语音回复，则判定复现。
4. 随后再次说有效唤醒词，确认正常唤醒能力仍可恢复，区分“错误静默”与“未静默”问题。
5. 观察窗口、重复轮数、外部语音样本、播放声压和距离等待需求确认后固定。

### 后续定向采集点

- AFE 输出：WakeNet 命中、VAD 起止及其时间戳。
- Agent：所有状态转换的前后状态和触发事件，重点关注 `EVT_WAKE_WORD`、`EVT_VAD_END`、TTS `stop` 和服务器 `goodbye`。
- 会话：`session_id` 创建、保留和清空时间，以及 `listen start/stop` 的发送条件。
- 当前源码可见 `EVT_VAD_END` 会被投递到 Agent，后续复现时需核对该事件在 `IDLE` 等非聆听状态是否出现；此项仅为定向采集点，不作为已确认根因。

### 新 Example 回归要求

- `xiaozhi_companion_robot_demo` 在一轮对话明确结束后必须进入静默待机。
- 静默待机期间，普通外部人声不得启动或延续 AI 会话、不得产生语音回复；只有新的有效唤醒词可以重新进入聆听和对话。
- 该场景已形成状态机回归用例和 XCR-AC-010 固定实机步骤；当前新增用例尚未执行，后续仍须保留目标板 PASS/FAIL、对话日志和十轮静默回归证据。
