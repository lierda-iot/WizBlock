# 小智陪伴机器人调试记录

更新时间：2026-08-07

## 1. 文档用途

本文档仅记录 `xiaozhi_companion_robot_demo` 的分轮实机调试现象、日志证据、诊断假设、验证动作和结论。

- 每轮调试保留完整问题上下文和可复核证据。
- 未验证的推断必须标记为“待验证”，不得写成确认结论。
- `requirements.md`、`design.md` 和 `project_memory.md` 只同步已确认需求、有效设计结论、已完成修复和阶段性验证结果，不完整复制各 Round 的过程记录。
- 功能源码、构建和烧录仍按项目正式规则执行；本文档不构成实现授权或正式需求变更。

## 当前状态摘要（2026-08-06）

当前状态：Motion completion、DOA 无效和 DOA 死区反向请求 Agent 的旧 decision 已按逐行为纯 C RED→GREEN 清除；最新 LLVM 主机纯 C 全量门禁实际结果为 `companion_core_test: PASS (0 failures)`。默认 clean build `1813/1813` 和 COM7 全片擦除/五段烧录已完成，目标板应用为 `0x2bf200`，最小 app 分区剩余约 31%。完整 Controller/Adapter/实机交错回归仍未完成。下文 `1 failure`/`2 failures` 是历史 RED 过程，不是当前门禁结果。

- 当前源码已完成健康 transport 复用及既有竞态保护：CONNECTING replacement request 原子激活/重绑、阻塞网络阶段刷新 live request、迟到网络提交门禁、跨 session epoch cancel 隔离、Controller deadline supersede、binding 先退休和严格 TTS barrier 均保持。
- 上一版 LLVM 主机纯 C 门禁实际执行 427 项、`0 failures`，默认 clean build `1812/1812`，应用 `0x2becb0`，并完成 COM7 全片擦除和五段烧录。16:02 日志中 14/14 次 WakeNet 均进入新 `LISTENING_READY`，活动态 10 次均复用健康 transport；未见 `TTS outside SPEAKING`、runtime invariant、Task WDT、panic/assert、跨 epoch cancel 或旧播放回灌。
- 用户已正式变更需求：AI session 与 DOA/WAKE_TURN 是同一 `generation/wake_seq` 下两个有状态但不互阻塞的任务。目标板当前运行双状态并行版 `0x2bf200`；工作区已完成双状态 model/policy、真实 wake-effect 入口、单 effect 失败隔离、共享 network fatal 和 Motion completion 隔离，LLVM 全量门禁为 `PASS (0 failures)`，clean build 为 `1813/1813`，COM7 烧录已完成。完整 Controller/Adapter 回归和活动态体验仍待完成。
- 电机板电池无电，16:02 本轮所有转向没有真实运动；`ESP_ERR_TIMEOUT` 只能作为 Motion 环境/出口证据，不可评价 DOA、BMI260 闭环、方向或角度精度。第 18～20 节保留为历史实施与故障证据，当前恢复入口以本摘要、第 21 节、`requirements.md` 9.5、`design.md` 11.24 和 `project_memory.md` 为准。

## 2. Round 索引

| Round | 日期 | 日志 | 当前状态 |
| --- | --- | --- | --- |
| Round 1 | 2026-07-27 | `robot log/1.txt` | 诊断与修复构建完成，修复版已进入 Round 2 实机验证 |
| Round 2 | 2026-07-27 | `robot log/2.txt` | SW3、TLS 分配和 1024ms DOA 部分闭合；当轮 Agent 阻塞定位为 WebSocket 任务创建失败，已由 Round 5 更新 |
| Round 3 | 2026-07-27 | `robot log/3.txt` | 运行中段历史窗口；已出现 TTS 收口与后续唤醒，状态异常仍存在，不作为当前最终结论 |
| Round 4 | 2026-07-27 | `robot log/4.txt` | 长时间空闲后建立 WebSocket 并进入 STT/TTS；日志窗口不完整，主结论由 Round 5 更新 |
| Round 5 | 2026-07-27 | `robot log/5.txt` | WebSocket、上传、STT、TTS 全链打通；暴露直接 `LISTENING -> SPEAKING` 兼容缺口，当前源码已修正并待功能复测 |
| Round 6 | 2026-07-27 | `robot log/6.txt` | 18 条空闲 heartbeat 稳定，无唤醒，不构成 AI 功能回归 |
| Round 7 | 2026-07-27 | `robot log/7.txt` | 该轮记录固件复位后约 148 秒空闲稳定，状态修正尚未唤醒复测 |

## 3. Round 1

### 3.1 测试背景

- 固件：`xiaozhi_companion_robot_demo` 默认正式配置，companion self-test 关闭。
- 硬件：新组装样机；重新检查连接后，静默待机随机移动时左右电机均可运行。
- 网络：4G 取得 IP 后进入 `net=1`，网络质量日志在 `Good` 与 `Bad` 间波动。
- 原始日志：`E:\10__AIProject\7_AI陪伴机器人\robot log\1.txt`。
- 本轮只做日志和源码分析，未修改代码、未重新构建、未重新烧录。

### 3.2 用户报告的问题

1. EX-024 正式排除热插拔需求，但日志中出现 ADC 采集。
2. 至少单击 SW3 两到三次，随机运动没有停止。
3. 约 45 度方向说出唤醒词后只观察到约 5 到 10 度转动。
4. 第一次唤醒后不确定是否回到空闲状态，随机动作长时间没有恢复。
5. 后续再次说唤醒词，没有转向动作响应。

### 3.3 诊断摘要

| 问题 | 当前结论 | 置信度 | 下一步 |
| --- | --- | --- | --- |
| ADC 与热插拔边界 | 运行期周期 ADC 是 SW3 输入所需；启动时 D0/BAT 采样和 module-detect 注册来自公共 BSP 的全量初始化，不是 EX-024 的热插拔轮询任务，但初始化边界过宽，与 XCR-020 的隔离目标存在疑点 | 高 | 设计选择后再决定拆分 BSP 初始化或增加按能力选择的初始化入口 |
| SW3 单击无效 | 三次按键窗口最低 raw 约为 2380 到 2383，仍高于当前 `released>=1800` 门限，始终被分类为 RELEASED，没有产生 click 事件 | 已确认 | 分别采集按下/释放各至少 100 个连续样本，确认正式滞回窗口后再修改配置 |
| 45 度只转 5 到 10 度 | 本次 DOA 结果为无效，没有下发声源转向命令；观察到的小角度来自唤醒前正在执行的随机左转被安全停止 | 已确认本次未执行 DOA 转向；真实角度标定待验证 | 先取得有效 DOA 样本，再对比 `REL`、计划时长和实测转角 |
| 唤醒后不回空闲 | 产品状态停在 `CONNECTING`。TLS 建链返回 mbedTLS `-0x7F00` 内存分配失败，Agent 的 version-check 循环无失败次数或总超时，持续重试且不回 IDLE | 已确认 | 先解决 TLS 内部内存预算，并为 version-check 失败增加有界恢复路径 |
| 后续唤醒无响应 | WakeNet 实际又检出 3 次，但 CONNECTING 状态不允许 reserve wake，均返回 `ESP_ERR_INVALID_STATE`，所以没有新 generation、DOA 或转向 | 已确认；问题 4 的下游结果 | 连接失败恢复 IDLE 后复测第二次唤醒；另评审 CONNECTING 状态下的重复唤醒反馈策略 |

### 3.4 问题 1：ADC 与热插拔边界

#### 已确认事实

1. 正式需求 XCR-020 明确排除 EX-024 的运行时热插拔检测、动态能力注册/注销和插拔联动。
2. EX-024 的 `start_product()` 调用公共 `board_laiwfs300_init()`。
3. 公共 BSP 初始化会无条件执行以下动作：
   - 初始化 ADC service。
   - 一次性读取 `D0_DETECT/GPIO1`、`BAT_ADC/GPIO7`、`SW_ADC/GPIO8`。
   - 初始化 `module_detect` 并注册默认 detection rules 和 capabilities。
4. EX-024 自身没有启动 `module_hotplug_demo` 的 `hotplug_manager`、slot 轮询或插拔事件处理。
5. 运行期间每 10 秒出现的 `companion_input: SW3 ADC ...` 是 SW3 的 ADC 输入诊断，属于 XCR-023 的按键需求。

#### 日志证据

- 启动时：`board_laiwfs300: init ADC service`。
- 启动时一次性读取：
  - `ADC D0_DETECT (GPIO1): raw=2000`
  - `ADC BAT_ADC (GPIO7): raw=0`
  - `ADC SW_ADC (GPIO8): raw=4095`
- 运行期：`companion_input: SW3 ADC raw=...`，没有 hotplug slot 插入/拔出事件。

#### 源码证据

- `main/xiaozhi_companion_robot_demo_main.c:59`：调用 `board_laiwfs300_init()`。
- `CODE/components/laiwfs300/board_laiwfs300.c:184`：公共 BSP 初始化 ADC，并读取三个 ADC 通道。
- `CODE/components/laiwfs300/board_laiwfs300.c:217`：公共 BSP 初始化 `module_detect` 并注册规则。
- `components/companion_input/companion_input.c:11`：EX-024 运行期只使用 `ADC_CHANNEL_7` 读取 SW3。

#### 当前判断

日志中的 ADC 有两类来源，不能统一视为热插拔：

- `SW_ADC` 周期采集是 EX-024 有效输入链路，必须保留或用等价输入实现替代。
- `D0_DETECT/BAT_ADC` 一次性采样以及 module-detect 注册来自公共 BSP 的全量初始化。它们当前没有形成运行时热插拔行为，但造成了 EX-024 与无关能力的初始化耦合，应作为后续设计评审项，而不是把它误认为新的产品需求。

### 3.5 问题 2：SW3 单击没有停止随机运动

#### 日志证据

当前配置日志为：

```text
pressed<=300 released>=1800 sample=10ms debounce=30ms max_click=800ms
```

用户单击期间，三个连续 10 秒统计窗口分别记录：

```text
SW3 ADC raw=4095 range=[2383,4095] stable=1
SW3 ADC raw=4095 range=[2380,4095] stable=1
SW3 ADC raw=4095 range=[2383,4095] stable=1
```

日志中没有对应的 `SW3 pressed`、有效 `SW3 released ... armed=1` 或 `SW3 roam_enabled=0`。

#### 源码解释

- `stable=1` 对应 `SW3_SAMPLE_RELEASED`。
- `raw<=300` 才分类为 PRESSED；`raw>=1800` 分类为 RELEASED。
- 实测最低值约 2380，整个按键过程仍处于 RELEASED 区间，因此不会建立 `click_armed`，也不会向 controller 投递单击事件。
- 如果 click 事件成功到达，controller 已实现关闭许可、立即 `companion_motion_stop("SW3 roam off")` 和取消后续随机调度。

#### 结论

本轮不是 controller 收到单击后未停车，而是 ADC 门限与当前实板电平不匹配，单击根本没有被识别。当前门限日志也明确标记为 provisional，不能直接作为正式标定值。

#### Round 2 验证

1. 固定同一块样机和供电条件。
2. 分别采集释放、按下各至少 100 个连续 raw 样本，并记录最小值、最大值和稳定区间。
3. 短按、长按、启动时按住分别采样，确认是否存在第三电平或过渡区。
4. 根据实测间隔确定 pressed/released 滞回窗口；在样本完成前不预设正式阈值。
5. 验证每次短按只产生一次 click，关闭时当前动作立即中断且后续无 role=roam 动作，再次短按只在 IDLE 恢复随机移动。

### 3.6 问题 3：声源转向角度明显不足

#### 本轮时间线

```text
164818ms  随机动作 turn_left 开始，role=0，计划 470ms
164871ms  WakeNet 检出唤醒词
164874ms  wake accepted，执行 safety stop
164937ms  DOA: RAW=90.0 FILT=87.0 REL=0.0 energy=50.9 valid=0
164944ms  原随机 turn_left 完成，interrupted=1
```

DOA 门限为 56dB，本次快照能量只有 50.9dB，因此返回 `ESP_ERR_INVALID_RESPONSE`。controller 按 XCR-007 跳过转向并继续通知 Agent。整份日志没有 `role=1` 的声源转向命令。

随机左转从开始到 WakeNet 检出约 53ms，随后被安全停止。该短暂运动与用户观察到的约 5 到 10 度相符，但它不是按 45 度目标计算的声源转向。

#### 公式核对

当前正式公式与 XCR-006 一致。若后续取得有效 `REL=45deg`，计划时长应为：

```text
250 + (45 - 15) * 1250 / 75 = 750ms
```

本轮没有生成 750ms 或任何 role=DOA 的电机命令，因此不能用本轮现象判断 750ms 的实机转角是否准确。

#### 待验证假设

1. DOA 能量门限 56dB 不适合当前 wake snapshot 的幅度尺度。预测：在相同角度提高说话声压或降低验证门限后，`valid` 将变为 1 并产生 role=DOA 命令。
2. wake snapshot 的窗口位置或双麦数据使 45 度声源被估计为接近中心。预测：左/中/右固定位置各重复 5 次时，RAW/FILT 会持续偏向 90 度而不是随位置变化。
3. 只有取得有效 REL 和明确计划时长后，才能判断“时间到转角”的机械标定是否也需要调整。

#### Round 2 验证

1. 固定声源距离、角度、声压和样机朝向，左/中/右各执行 5 次有效唤醒。
2. 每次记录 RAW、FILT、REL、energy、valid、转向 role、方向、计划时长和实测角度。
3. 先解决 `valid=0` 与角度估计问题，再单独标定 250ms、750ms、1500ms 的实际转角。
4. 转角允许误差尚未形成正式口径，Round 2 只记录测量数据，不自行定义合格阈值。

### 3.7 问题 4：唤醒后不回 IDLE，随机动作不恢复

#### 状态与日志证据

- `state=5` 对应 `COMPANION_PRODUCT_CONNECTING`。
- 首次唤醒后状态从 LOCATING 进入 CONNECTING，此后直到日志结束都保持 `state=5`。
- Agent 周期执行 OTA/version-check，每次均失败：

```text
esp-tls-mbedtls: mbedtls_ssl_setup returned -0x7F00
xiaozhi_agent: version_check HTTP failed: ESP_ERR_HTTP_CONNECT
xiaozhi_agent: version_check failed, retry in 30000 ms
```

- ESP-IDF v5.5.4 的 `mbedtls/ssl.h` 明确定义 `-0x7F00` 为 `MBEDTLS_ERR_SSL_ALLOC_FAILED`。
- 唤醒前 heartbeat 显示 internal heap 约 7511B、PSRAM 约 7.45MB；失败状态下 internal heap 约 6.3 到 6.6KB。
- `sdkconfig.defaults` 未覆盖 mbedTLS allocator；ESP-IDF Kconfig 默认使用 internal memory，而当前配置为 PSRAM 预留 64KB internal pool。

#### 源码证据

`CODE/components/xiaozhi_agent/xiaozhi_agent.c:502` 的 `open_session()` 在 version-check 失败时只指数退避并继续 `while (s_running)`。该阶段没有最大重试次数、总超时或失败后 `set_state(IDLE)`；只有后续 WebSocket connect/hello 分支具备回 IDLE/超时逻辑。

#### 结论

本轮直接失败点是 TLS 内存分配，不是单凭 `network quality F Bad` 能解释的链路故障。低 internal heap 与 mbedTLS 默认内部内存分配策略构成强证据；同时，Agent 的无界 version-check 重试把一次可恢复失败放大为永久 CONNECTING。

随机移动长时间不恢复是安全状态机的预期下游行为：XCR-018 只允许 IDLE 随机移动。真正缺陷是连接失败没有有界地结束会话并回到可再次唤醒的安全状态。

#### Round 2 验证

1. 在第一次 TLS 请求前后记录 internal free、largest block、PSRAM free 和实际 mbedTLS allocator 模式。
2. 单变量验证 TLS 内存方案：释放足够 internal heap，或经设计确认后使用适合本平台的 mbedTLS PSRAM/default allocator。
3. 为 version-check 定义有界失败策略；最大次数/总超时和用户反馈口径仍待确认，不能在本轮自行设定。
4. 验证失败后状态回到 IDLE、upload gate 关闭、电机保持停止，并允许下一次有效唤醒；网络恢复后再验证正常 LISTENING/PROCESSING/SPEAKING/IDLE 闭环。

### 3.8 问题 5：后续唤醒词没有转向响应

#### 日志证据

后续语音并非未被 WakeNet 检出。日志在 fetch 7305、7362、7479 三次记录：

```text
WAKENET_DETECTED
companion_audio: wake snapshot generation=0 wake_seq=0 version=1 result=ESP_ERR_INVALID_STATE
```

heartbeat 的 `wake_reject` 从 0 增至 3。期间产品持续为 `state=5/CONNECTING`，没有新 generation、DOA 请求或转向命令。

#### 源码解释

controller model 当前只允许 IDLE、PROCESSING 或 SPEAKING 状态 reserve wake；CONNECTING 状态返回 `ESP_ERR_INVALID_STATE`。因此这三次属于“声学唤醒已检出，但产品状态门禁拒绝”，不是 WakeNet 模型失效。

#### 结论

问题 5 是问题 4 的直接下游结果。优先修复 CONNECTING 的失败恢复，再验证第二次唤醒。是否允许 CONNECTING 中的新唤醒取消并重开会话属于额外产品行为，目前正式需求未定义，需单独确认后才能修改。

### 3.9 根因优先级与可证伪预测

1. **TLS 内存不足和无界重试**：如果是主因，增加可用 internal heap或改变经确认的 mbedTLS 分配策略后，`-0x7F00` 应消失；即使网络仍失败，有界策略也应使状态回到 IDLE。
2. **SW3 门限与实板电平不匹配**：如果是主因，按实测窗口更新配置后，同一按键应产生一次 pressed、一次 armed release 和一次 roam toggle。
3. **DOA 快照有效性/方向估计问题**：如果是主因，受控左/中/右样本会显示 energy 或 FILT/REL 与物理位置系统性不符；在此之前调整转向时长不会解决本轮现象。
4. **公共 BSP 初始化范围过宽**：如果拆分为 EX-024 所需能力初始化，D0/BAT/module-detect 启动日志应消失，而 SW3 ADC、音频、显示、电机和网络能力仍正常。

### 3.10 Round 1 结论和下一步顺序

Round 1 已形成四项有效诊断结论：SW3 门限不匹配；本次没有执行 DOA 转向；TLS 内存分配失败使 Agent 永久停在 CONNECTING；后续 WakeNet 检出被该状态门禁拒绝。ADC 日志已区分为有效 SW3 输入和公共 BSP 的无关全量初始化两类。

建议 Round 2 按以下顺序执行，避免多个变量互相遮蔽：

1. 解决或定向验证 TLS internal heap，并补齐 CONNECTING 有界失败恢复。
2. 完成 SW3 按下/释放实板标定，再验证一次单击只切换一次。
3. 受控采集有效 DOA 的左/中/右样本，再标定 REL 到实际转角。
4. 评审并收窄公共 BSP 初始化边界，确保 EX-024 不带入无关 module-detect 行为。
5. 复测首次唤醒完整会话、回 IDLE、随机移动恢复和第二次唤醒。

上述步骤随后获得人工确认并进入代码修改；落实结果见 3.11。

### 3.11 Round 1 修复落实与构建

用户确认现有 SW3 数据已经足够，并明确要求不新增或保留单独的 SW3 测试/分类接口，直接修改正式按键触发路径。本轮完成以下修改：

1. SW3 正式窗口改为 `pressed<=3000`、`released>=3500`，`3001..3499` 保持 UNKNOWN 滞回区；继续使用 10ms 采样、30ms 消抖和 800ms 最长单击。Round 1 的按下最低值 `2380..2383` 将进入 PRESSED，释放值 `4095` 将进入 RELEASED。controller 原有单击切换、关闭时立即停止 roam、再次开启仅在 IDLE 恢复的正式链路保持不变。
2. `sdkconfig.defaults` 启用 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`。ESP-IDF v5.5.4 的 `mbedtls/port/esp_mem.c` 确认该配置使用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`，用于避开 Round 1 中 TLS 建链时仅剩约 6KB internal heap 导致的 `MBEDTLS_ERR_SSL_ALLOC_FAILED`。
3. Demo 私有 Agent adapter 增加 30s CONNECTING watchdog。超时后停止公共 Agent worker，等待 12s 让旧 version-check 退出，再执行一次本地重启；恢复启动失败时关闭 Agent capability。该路径不修改公共 `xiaozhi_agent`，不会继续在同一会话内无界 version-check 重试。
4. DOA 唤醒快照从 2560 帧修正为 16384 帧，即 32 个 512 帧块、约 1024ms。每块先执行 56dB 能量门限，只有达标块的 DOA 角度才进入中值/EMA 滤波；日志新增 `QUALIFIED/USED`。`REL x2`、左右符号、56dB 门限和转向时长没有凭本轮现象调整，继续等待实机标定。
5. DOA 工作缓冲改为启动阶段同步预分配；UI 任务内部显示/缓冲/tick 初始化失败通过错误回调关闭 UI capability；audio feed/fetch/playback 任务若因工作缓冲不足退出，则发送 fatal 事件并关闭 Audio capability；Agent worker 恢复启动失败关闭 Agent capability。失败只降级对应依赖能力，controller、其他无依赖任务和 heartbeat 继续运行。

构建验证：

- self-test 配置 clean build `1798/1798`，固件 `0x2728a0`，4MB app 分区剩余 `0x18d760`（39%）；生成配置包含 `CONFIG_XIAOZHI_COMPANION_SELF_TEST=y` 和 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`。
- 默认配置 clean build `1797/1797`，固件 `0x270fe0`，4MB app 分区剩余 `0x18f020`（39%）；生成配置明确 `# CONFIG_XIAOZHI_COMPANION_SELF_TEST is not set`，同时包含 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`。
- 首次 self-test clean build 在 ESP-IDF `esp_lcd_panel_rgb.c:700` 遇到项目已记录的 GCC ICE；按成熟入口保持并发 2 重建后完整通过。该工具链 ICE 与本次功能源码无关。
- 本轮未连接、枚举、擦除或烧录 COM7。新固件的 SW3 单击、TLS 建链、CONNECTING 超时恢复、1024ms DOA 和故障降级仍需 Round 2 实机验证。

### 3.12 Round 2 最小验证顺序

1. 烧录默认固件后短按 SW3，确认日志依次出现 PRESSED、有效 RELEASED/click 和 `SW3 roam_enabled=0`，当前 roam 动作立即停止；再次短按只切换一次并仅在 IDLE 恢复。
2. 有效唤醒后确认 TLS 不再出现 `-0x7F00`；若网络请求仍失败，确认 CONNECTING 最长 30s 后执行一次本地恢复，heartbeat、UI、输入和安全停车不停止。
3. 左/中/右各说唤醒词 5 次，记录 `QUALIFIED/USED/RAW/FILT/REL`、转向 role、计划时长和实测角度；先验证 DOA 有效性，再决定是否调整 56dB、REL 增益或时长。
4. 完成首次会话回 IDLE、roam 恢复、第二次唤醒、播放期打断和十轮静默门禁回归。

## 4. Round 2

### 4.1 烧录与测试条件

- 固件：Round 1 默认修复版，`CONFIG_XIAOZHI_COMPANION_SELF_TEST` 未设置，`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`。
- 构建：clean build `1797/1797`，应用 `0x270fe0`，4MB app 分区剩余 `0x18f020`（39%）。
- 烧录：按 `design.md` 4.1.2 对 `COM7` 先执行全片擦除，再写入 bootloader、partition table、应用、`srmodels` 和 `spiffs_data`；五项均通过 Hash 校验并完成 RTS 硬复位。
- 日志：`E:\10__AIProject\7_AI陪伴机器人\robot log\2.txt`，按 `COM7 / 115200 / 8N1` 抓取约 150 秒。

### 4.2 启动与任务存活

- 默认配置明确输出 `self-test disabled in default configuration`。
- 应用启动门禁从 `1630ms` 到 `11636ms`，实测 10006ms；门禁后才开始板级和产品初始化。
- 4G 在 `16476ms` 取得 IP `10.55.189.133`，controller 在 `18087ms` 进入 `ready=1`。
- 共取得 13 条 heartbeat；末条位于 `149150ms`，始终 `net=1`、`errors=0`、`queue_drop=0`、`read_err=0`、`play_drop=0`。
- 全日志只出现一次启动 ROM 记录，未见 panic、assert、task watchdog、堆破坏、栈溢出或意外重启。
- FT6206 无 ACK、LCD 初始化前一次 `SPI bus already initialized`、ECM 初始 MAC 不可用仍为既有非阻断问题。
- LVGL 输出 `LVGL pixel dog ready touch=unavailable 320x240 landscape` 后没有 UI capability 关闭、fatal、缓冲或 tick 错误。源码确认 controller 每消费一个事件后都会调用 `sync_ui()`，UI task 持续读取 pending state 并渲染；但 `companion_ui_set_state()` 没有语义状态变化日志，controller 也忽略其返回值，因此本轮日志不能证明 `LOCATING/TURNING/CONNECTING/IDLE` 已逐次显示正确。这是 XCR-030 诊断可观察性的实现缺口，不等同于已确认的 LVGL 刷新故障。

### 4.3 SW3 正式功能链路

- 启动日志确认正式窗口为 `pressed<=3000`、`released>=3500`，10ms 采样、30ms 消抖、800ms 最长单击。
- 本轮捕获 4 次有效短按，按下 raw 为 `2392..2395`，释放 raw 均为 `4095`，按住时间分别为 247ms、311ms、251ms、201ms。
- 每次短按均只产生一次切换，`roam_enabled` 严格按 `0 -> 1 -> 0 -> 1` 变化；关闭期间没有新的随机动作，重新开启后只在 IDLE 恢复调度。
- 两次关闭均发生在随机动作间隔内，因此本轮日志不能证明“动作执行中短按会立即中断”；角落 `ROAM ON/OFF` 的实屏同步也未由日志验证。

### 4.4 唤醒、DOA 与转向

- 共触发 3 次 `WAKENET_DETECTED`，`wake_seq=1/2/3` 各只生成一次快照、一次 DOA 结果、一次转向和一次 Agent notify。
- DOA 启动参数为 32 个 512-frame 块、1024ms 快照、56dB 逐块门限；音频快照为 16384 帧。
- 三次结果分别为：
  - `RAW=70.0 FILT=70.1 REL=39.8 QUALIFIED=17 USED=17`，左转 660ms；
  - `RAW=70.0 FILT=70.0 REL=39.9 QUALIFIED=24 USED=24`，左转 670ms；
  - `RAW=30.0 FILT=70.0 REL=39.9 QUALIFIED=22 USED=21`，左转 670ms。
- 第二次唤醒发生在随机左转期间，原随机动作以 `interrupted=1` 完成，随后执行该 wake 的转向，证明唤醒可抢占 roam。
- 本轮只覆盖一个实际声源侧位，未记录人工实测角度；左/中/右准确率、EMA 跨唤醒残留是否合理、REL 增益和转向时长仍待专项标定。

### 4.5 TLS、Agent 与有界失败

- 三次有效唤醒的 version check 均在约 1.1 秒内返回 HTTP 200；未再出现 TLS `-0x7F00` 或 `MBEDTLS_ERR_SSL_ALLOC_FAILED`，说明 mbedTLS 外部内存分配已消除 Round 1 的直接失败点。
- 三次 version check 后均在创建 WebSocket 任务时立即报 `Error create websocket task`，随后 `WS connect failed` 和 `open_session failed: ESP_FAIL`。因此仍未进入 LISTENING、上传、TTS 或完整对话。
- 失败均立即使 Agent 从 CONNECTING 回到 IDLE，随后随机动作恢复；13 条 heartbeat、音频和其他任务持续运行。没有出现同一请求内的 version-check 无限重试，也未触发 30 秒 watchdog，因为失败发生在约 1.2 秒内。
- Agent 初始化前 internal heap 为 85519 字节；完整模块启动后 heartbeat 只剩 7083 字节，首次失败后的周期日志约为 5527～5871 字节。结合固定复现的任务创建失败，当前首要根因是 WebSocket 动态任务所需内部堆/连续块预算不足；具体栈大小、分配能力和修复位置仍需定向核对，不能仅凭总 free heap 直接定稿。

### 4.6 Round 2 结论

1. SW3 阈值和正式单击切换链路已实板通过；动作中立即停车与实屏状态仍待补测。
2. 16384 帧/1024ms 快照和逐块 56dB 门限已实板运行，三次唤醒均产生有效 DOA 和单次转向；方向/角度仍未完成左中右标定。
3. TLS 内存分配失败已消失，version check 不再无限重试；失败路径不会阻塞其他任务。
4. Agent/WebSocket 在 Round 2 当轮仍不可用，阻塞收敛为 WebSocket 任务创建失败；该历史结论已被 Round 5 的连接、上传、STT、TTS 和关闭会话证据更新，不再是当前阻塞。
5. LVGL 任务初始化和持续存活未见异常，但缺少逐次状态/表情变更日志且 UI 更新返回值未被检查；下一轮需补齐限于语义变化的 UI 可观察性，再用实屏对照日志验证状态映射。

## 5. Round 3 与 Round 4 历史窗口

- 两份日志都不是从完整启动到完整会话的单一封闭窗口，不单独用作最终验收证据。
- Round 3 开始时已在运行中段，可见 TTS stop 和回 IDLE，之后又有新唤醒；后段长时间保持 CONNECTING 并拒绝新唤醒，说明该轮仍有会话状态收口问题。
- Round 4 前段约 315 秒保持 IDLE 且 heartbeat 的 `errors/queue_drop/read_err/play_drop=0`；后续有效唤醒建立 WebSocket，进入 LISTENING、STT 和 TTS start。该窗口未提供足够完整的后续收口证据，因此由 Round 5 的完整链路更新当前结论。

## 6. Round 5

### 6.1 完整 AI 链路

- 本文件段内有 4 次有效唤醒，每次都产生有效 DOA 并执行单次左转。
- Agent 记录 3 次新建 WebSocket 会话和 1 次 PROCESSING 期 `abort + listen start` 当前会话重启。
- 记录 4 次 STT、4 次 TTS start/stop，最终回 IDLE。末条关键 heartbeat 为 `wake_ok=6`、`upload=969`、`errors=0`、`queue_drop=0`、`read_err=0`、`play_drop=0`。
- 全程未见 panic、assert、task watchdog 或意外重启。Round 2 的 `Error create websocket task` 已被该轮成功连接证据更新，不再是当前阻塞。

### 6.2 状态机缺口

- 日志在 `556550ms` 记录 Agent `state: 3 -> 5`，即服务端从 LISTENING 直接通过 `tts.start` 进入 SPEAKING。
- 旧 controller model 只允许 PROCESSING -> SPEAKING，所以当时没有记录对应的产品状态转移，最后以 `LISTENING->IDLE` 收口。TTS 本身已播放，但 controller 与 Agent 语义不一致。
- 当前 `companion_controller_model.c` 已允许 `LISTENING -> SPEAKING`，并允许 PROCESSING/SPEAKING 新唤醒；打断时使用 `ignore_next_agent_idle` 拒绝旧会话迟到的下一个 `IDLE`。

## 7. Round 6 与 Round 7

- Round 6 共 18 条 heartbeat，没有唤醒、WebSocket、STT 或 TTS。
- Round 7 是 `Jul 27 2026 18:10:21` 编译的默认配置固件；复位后约 148 秒内有 12 条 heartbeat，同样无唤醒、会话或致命错误，触摸初始化为 ready。
- 两轮的 `errors/queue_drop/read_err/play_drop` 均为 0，未见 panic/watchdog。这些证据只说明空闲稳定，不能说明直接 SPEAKING、播放期打断或会话结束门禁已验证。
- Round 7 `stale=16` 与非 LISTENING 状态收到的 16 次 VAD end 一一对应，是当前 `handle_vad_end()` 计数口径污染，不是 16 次真实会话故障。

## 8. 2026-07-28 修正、构建与烧录

- 本节对应 2026-07-28 历史固件，当时播放音量由 50 调为 70；该值随后已被用户确认的当前正式值 90 取代，当前配置以 `requirements.md` XCR-032 和源码为准。
- 新增回归用例覆盖直接 LISTENING -> SPEAKING、PROCESSING/SPEAKING 新唤醒、旧 `IDLE` 不得结束新 wake generation，并修正主机 runner 使其包含 logic 和 controller model 源码。
- 当前 Windows PATH 无可执行主机测试的 `gcc/clang/cl`；因此新用例只随目标板 self-test 配置完成 clean build `1798/1798`，固件 `0x273190`。本轮没有烧录 self-test 或读取其 PASS/FAIL，只记为编译覆盖。
- 默认配置重新全量镜像并 clean build `1797/1797`，生成配置明确关闭 self-test，固件 `0x2711f0`，4MB app 分区剩余 `0x18ee10`（39%）。
- COM7 识别为 ESP32-S3 rev v0.2、ROM MAC `44:1b:f6:f3:ae:24`；按 `design.md` 4.1.2 全片擦除成功，随后默认固件烧录正常退出，最后 `Hard resetting via RTS pin... Done`。
- 该轮按用户要求在烧录后未自动打开串口或抓取日志，当时停止在“固件已烧录，等待用户后续测试指令”。
- 随后的 2026-07-28 文档统一轮次没有新增源码修改、构建、烧录、日志或实机测试；该阶段状态已由下方 8.1 的表情增量记录更新。

### 8.1 八套表情增量构建与烧录

- 八套静态形象和每套 12 帧动态候选通过人工视觉审核后，角色/场景双维解耦设计获用户确认并进入代码实现；本轮授权范围为编辑、构建和烧录，测试与 Git 操作未授权，烧录后不自动抓日志。
- `expression_manifest.psd1` 注册 8 个角色和 12 个场景，离线生成 96 张 80×60 生产预览及 4-bit C catalog。新增 `companion_expression` 负责 catalog 校验、场景 fallback、角色循环、动画调度和 RGB565 展开；新增 `companion_touch_gesture` 负责 30/120ms 消抖、48px/32px/700ms 滑动识别和普通触摸取消仲裁；`companion_ui` 改为 320×240 PSRAM RGB565 Canvas。
- 已加入人工回归所需日志：catalog/默认角色、PSRAM Canvas 分配、表情状态信号、触摸坐标、消抖转移、滑动方向/位移/时长、角色索引/ID、渲染失败与恢复。日志已经进入源码，但本轮没有串口运行证据。
- 默认配置 clean build `1802/1802`，应用 `0x2aba40`，最小 app 分区 `0x400000`，剩余 `0x1545c0`（33%）；除既有 `ESP_IDF_VERSION` 环境警告外无构建失败。
- COM7 全片擦除成功；目标为 ESP32-S3 QFN56 rev v0.2，本轮 ROM MAC `44:1b:f6:f3:ae:70`。bootloader、应用、partition table、`srmodels`、`spiffs_data` 均显示 `Hash of data verified`，最后 `Hard resetting via RTS pin... Done`。
- 本轮未执行目标板 self-test、主机测试或人工功能操作，也未抓启动/运行日志。构建与烧录成功不能替代八套表情、动画时序、触摸效果和左右滑动的实机验证。

## 9. 2026-07-28 当前人工功能判断与后续优化

本节是烧录后的用户人工体验反馈，不是新增串口日志，也不改变 Round 5～7 的证据边界：

- 屏幕显示正常，触摸正常。
- 电机动作正常；声源定位能够触发，触发后的实际转向角仍需标定优化。
- AI 对话能够触发，偶发对话无响应，待后续详细测试定位。
- SW3 有效，网络可用。
- 当前表情模式、状态切换和触发效果满足功能要求；触摸触发的别扭表情已有脸颊挤压鼓动语义，但整体视觉仍过于简易。

优化顺序确定为：表情效果、声源定位转向角、AI 对话体验。该判断之后，八套原创像素角色及每套 12 帧动态候选已通过人工视觉审核，解耦式表情资源、渲染、动画和左右滑动切换已接入，并完成 clean build 与 COM7 烧录。当前仍缺少新版实屏功能操作和运行日志，下一步由用户人工验收表情与滑动效果；DOA 与 AI 对话专项顺序不变。

## 10. 2026-07-29 手势、离线 IDLE 与空闲表情调优

- 用户在状态解耦修正版上实机确认普通触摸与左右滑动互斥通过，未插 4G 时 controller/左上角保持 `IDLE`，离线空闲显示正常。该结论来自人工操作，不是串口日志。
- 空闲主体的左上/右上倾摆观感未通过，当前运行时已停止推进 sway 相位，`IDLE` 与兼容 `WAIT_NETWORK` 只选择 `idle/blink`；两个 sway 场景资源保留但不启用。
- 有效滑动距离按单变量从初版 48px 依次标定为 40px、36px、30px；30px 最终由用户确认效果可接受。其他手势参数保持 30ms 按下消抖、150ms 总判定、12px 意图锁定、32px 最大垂直偏移、700ms 最大时长、220ms 快速点击反馈和 120ms 释放消抖。
- 各次修改均按 `design.md` 4.1.2 执行全量镜像、clean build、COM7 全片擦除和五分区烧录。最终版本 clean build `1802/1802`，应用 `0x2abc70`，4MB app 分区剩余 `0x154390`（33%）；五段镜像均通过 Hash 校验并完成 RTS 硬复位。
- 本轮未执行自动测试、self-test、运行日志抓取或 Git。仍需补充双向次数、慢滑/快速点击/长按/轻抖/斜移、0/1/2/8 catalog、网络恢复/活动断网和八套角色转向/说话状态回归。

## 11. 2026-07-31 完整状态修正版待测基线

- `robotlog/2026-07-31_09-11-38_manual-regression-com7.txt` 对应旧固件，覆盖 SW3、拔卡/插卡、重复唤醒、`CONNECT` 卡死和转向偏大过程。该日志用于问题定位，不代表当前修正版结果。
- 当前正式源码已完成 Controller 全状态与 deadline、Agent `session_epoch/request_id`、Audio owner/phase/token、DOA request/cancel、Motion 失败分类、BMI260 真实微秒 `dt` 梯形积分、Roam/SW3 role-token 停车、Network lifecycle/ready、capability revision/reconcile 和 UI 派生边界的静态闭环审核。
- 4G 恢复链使用标准 ETH/IP 断线锁存：5 秒宽限后由唯一 worker 执行 manager deinit、LTE 断电 1 秒、重新上电、manager init并等待新 `GOT_IP` 最多 20 秒；失败按 5/10/20/30 秒封顶退避。重新插卡恢复只发布网络 `READY_4G`，不复活旧 AI 会话，也不改变离线 `IDLE`、表情或 ROAM。
- 默认配置 clean build `1805/1805` 通过，应用 `0x2b7fc0`、4MB app 分区剩余 `0x148040`（约 32%），self-test 关闭。为解决构建依赖，`companion_motion/CMakeLists.txt` 已显式声明 `esp_timer`。
- 当前没有可用硬件，因此修正版没有执行全片擦除、烧录、串口日志、目标板 self-test 或实机功能验证。硬件恢复后先按 `design.md` 4.1.2 全片擦除并五分区烧录，再由人工依次验证重新插卡恢复、活动断网、SW3 动作中停车、转向角度、活动态唤醒打断和“1234”误触发。

## 12. 2026-08-03 首次 4G 初始化回归与修正版烧录

- 原始人工日志已归档为 `robotlog/2026-08-03_10-02-22_network-wait-audio-wdt-com7.txt`，SHA-256 为 `B5597DB8FB5F33CCCB7E26CD7AE99972D4EB325782B85E8EEA911C6AEE9FB634`。失败顺序为 Audio 任务先启动，随后 USB Host/CDC 初始化失败，Network 只发布 `WAIT_4G error=ESP_OK`，且之后无 GOT_IP、恢复或退避。
- 根因为上一轮网络改造后 `companion_network_start()` 在 worker 完成 LTE 上电/manager init 前返回，导致 Audio feed/fetch 与 USB Host 启动竞态；同时初始 `WAIT_4G` 无 20s GOT_IP 超时出口，预编译 manager 内部失败但外层返回 `ESP_OK` 时会永久等待。
- `companion_network` 现以静态二值信号量让主启动路径最多等待 6000ms，只等首次 LTE 上电和 manager init 尝试，不等 SIM/ECM/GOT_IP。初始 `WAIT_4G` 20000ms 无有效 GOT_IP 时以 `ESP_ERR_TIMEOUT` 进入现有 `RECOVERING/BACKOFF`。增加的低频日志覆盖 manager init scope/attempt/耗时/结果、屏障、WAIT 截止、宽限到期、manager deinit、LTE off/on、GOT_IP 等待和退避。
- Audio 实时性后续方案只记录，本轮未改 `companion_audio`：待后续将 AFE fetch/WakeNet 与 Opus 编码拆任务，以有界 PCM 队列交接，上传门禁关闭时不编码，再按 AFE full、速率/间隔、队列深度和 WDT 成组回归。
- 默认 clean build `1805/1805` 通过，应用 `0x2b98d0`，4MB app 分区剩余 `0x146730`（32%）。`COM7` 为状态 `OK` 的 CH340；ESP32-S3 rev v0.2 / ROM MAC `44:1b:f6:f3:ae:24` 全片擦除成功，bootloader、应用、分区表、`srmodels`、`spiffs_data` 五项均 `Hash of data verified`，并完成 RTS 硬复位。
- 本轮没有自动打开串口、没有抓取新日志或执行目标板功能测试，也没有执行 Git。下一窗口先核对带卡启动顺序与 `READY_4G`，再验证无卡 20s 超时、插卡恢复、拔卡/重插及离线 `IDLE/ROAM`。

## 13. 2026-08-03 带卡复测失败与根因定位

- 用户补充确认测试全程 SIM 卡始终插着、没有插拔动作。反馈日志已原样归档为 `robotlog/2026-08-03_11-15-52_ex024-network-recovery-reboot-com7.txt`，大小 974786 字节，SHA-256 `1CBD5DC37FF4B2568E6E8ACE9293249AAF1FD22ACF7046F0FE8B968E87831B91`，与原附件完全一致。
- 17 个完整周期均在 `WAIT_4G` 约 20 秒超时后进入 recovery，并在 manager deinit 的 USB CDC 接口释放处断言重启；16 个周期此前已有非零 4G IP，0 个周期发布 `READY_4G`。
- 已确认网络错误机制：`accept_valid_got_ip_after()` 将 GOT_IP/netif up 与 `lsd_network_is_ready()` 外网摘要绑定；ECM 发送超时时外网摘要为 false，已有 IP 仍被判为初始化失败。随后 `lsd_network_mgmt_deinit()` 经 `lsd_4g_module_deinit -> usbh_cdc_port_close -> usb_host_interface_release()` 返回 `ESP_ERR_INVALID_STATE`，组件内部 `ESP_ERROR_CHECK` 执行 `abort()`。
- 历史窗口同窗记录 65 次 CPU0 Task WDT、302 次 ECM transmit timeout 和 3429 次 AFE feed ringbuffer full；当时 Audio feed 优先级为 7、固定 CPU0 且成功路径不让出。该值只作为历史诱因记录，当前正式口径已调整为 feed/fetch/playback `5/6/4`，并在 feed 成功路径保留 1 tick 让步。
- 当前恢复路径已判定无效。下一步须先人工确认修复设计：恢复一次性网络初始化语义，分离 USB/ECM 链路、IPv4 与外网可达状态，禁止仅因外网探测失败执行 manager deinit，并同步处理 Audio CPU0 调度。完整阶段结论与恢复入口见项目根目录 `project_memory.md`。

## 14. 2026-08-03 LISTENING 打断与 LTE-only 恢复修复

### 14.1 13:48 反馈

- 日志：`robotlog/2026-08-03_13-48-07_ex024-manual-listen-hotplug-com7.txt`，SHA-256 `828AA0F93D42627B58EA1CEDB2765596D58465E3755E42275DF0A4D09B9AD7BF`。
- LISTENING 态新唤醒能够打断并触发 DOA/转向；一次长时间 LISTENING 最终由 30s deadline 正常回 `IDLE`。拔卡转 `4G WAIT`，插卡恢复在该窗口仍未完成。

### 14.2 14:18 反馈

- 日志：`robotlog/2026-08-03_14-18-51_ex024-manual-listen-hotplug-recovery-com7.txt`，SHA-256 `B237BEBABCEFBABC35DE3A31C1927397BA4B171906950B2E5F778B1EB06761E9`。
- LISTENING 打断、转向、拔卡/插卡状态切换、LTE-only power cycle、恢复后两轮 AI 对话和转向均通过；67.5° 目标闭环实际 67.7°。网络状态和对话不改变随机移动。该窗口未见 panic、assert、Task WDT、重启、AFE full 或队列/栈溢出；一次 30s LISTENING deadline 属于正常兜底。
- 当前网络设计口径：manager 常驻；`WAIT_4G` 表示链路/IPv4 未就绪，`WAIT_INTERNET` 表示已有 IPv4 但外网摘要未就绪；初始无 IPv4 只被动等待，真实断线恢复只执行 LTE off 1s/on 和 5/10/20/30s 退避，不执行 manager deinit/re-init。

## 15. 2026-08-03 偶发 WebSocket task 创建失败

### 15.1 现象与状态出口

- 日志：`robotlog/2026-08-03_14-49-05_ex024-websocket-task-create-failure-com7.txt`，大小 33868 字节，SHA-256 `A24B3485C8E86A164D334280B8B9CB877B888F77128272A4F972B3D1D385A77A`。原始日志包含认证响应，仅保留本地归档，不把认证内容复制到文档或 Git。
- `generation=17/wake_seq=11` 已完成 DOA 与转向，目标 22.5°、实际 22.8°、Motion `ESP_OK`；Controller 正常进入 `CONNECTING`。version check 返回 HTTP 200 后，`websocket_client: Error create websocket task`，Agent 以 `ESP_FAIL` 关闭 session，Controller 合法执行 `CONNECTING -> IDLE`。用户确认该现象偶发，有时能正常进入 AI 对话，因此不能归因于“转向后马上 IDLE”。

### 15.2 高置信假设与后续验证

- 构建镜像实际锁定的 managed `esp_websocket_client` 为 1.2.3；未设置 `task_stack` 时采用组件内 `WEBSOCKET_TASK_STACK=4KB`，并调用普通 `xTaskCreate()`，任务栈从 internal RAM 分配。当前 P0 改为显式传入 4096 字节，因此 Example 的 `CONFIG_WEBSOCKET_CLIENT_TASK_STACK_SIZE=6144` 不改变该路径。
- 失败窗口 internal heap 最大连续块约 7680B，会话中降至约 2048B；前后空闲值约 26451/26423B、最大块均 7680B，无持续泄漏证据。创建失败与 `dingding.wav` GMF 收尾重叠，当前高置信判断为瞬时 internal RAM 连续块不足/碎片化。
- 该诊断轮只记录问题，未修改源码、未构建、未烧录、未自动测试；后续实施与构建状态见 15.3。

### 15.3 2026-08-04 P0 实施与构建

- 保持 WebSocket task 栈 4096 字节，不降低栈、不改公共 session/request Interface、不新增 Agent worker，也不重排提示音时序。单次会话使用同一个 WebSocket client 最多执行 3 次 `esp_websocket_client_start()`；第 1/2 次失败后等待 100ms，第 3 次失败放弃。重试前检查 `s_running` 和当前 request cancel；连续失败沿原路径销毁 client、清空 binding 并返回失败，使 Controller 有界回 `IDLE`。
- `esp_websocket_client` 1.2.3 源码复核确认：task 创建失败时 client 尚未进入 `WEBSOCKET_STATE_INIT`；下一次 `start()` 会销毁并重建 transport，因此同 client 重试路径成立。
- 新增 `[DEBUG-WSRAM]` 探针：Audio 记录 `prompt_start/prompt_terminal`，Agent 记录 `before_init/after_init/before_start/after_start`；字段包含 attempt/result、4096 字节 task stack、internal free/largest/minimum 和 PSRAM free。
- 纯 C 策略用例已加入主机/self-test 集合，覆盖首次成功不重试、第 1/2 次失败重试、第 3 次失败放弃和 attempt 0 放弃。当前 Windows PATH 无 `gcc/clang/cl`，主机用例未执行，不得记为测试通过。
- self-test 配置 clean build `1808/1808`，应用 `0x2bfdb0`，最小 app 分区剩余 `0x140250`（31%）；默认配置 clean build `1807/1807`，应用 `0x2b9eb0`，剩余 `0x146150`（32%）。首次 self-test clean build 在 ESP-IDF `esp_lcd_panel_rgb.c` 遇到已知工具链 ICE，按 `design.md` 4.1.2 原入口重试后完成；默认构建的外层 PowerShell stderr 警告不影响 `idf.py` 最终返回 0、链接、镜像生成或分区检查。
- 本轮未执行目标板 self-test、串口验证或 Git。2026-08-04 已按成熟入口在 COM7 完成全片擦除并烧录默认应用 `0x2b9eb0`，五分区 Hash 校验通过并完成 RTS 硬复位；下一步由用户执行首次失败后成功、连续三次失败、request cancel 和连续唤醒资源压力验证。

## 16. 2026-08-04 活动态打断/响应停滞与 Audio 调度统一

- 用户反馈 SPEAKING 偶发不能打断、LISTENING 无响应后再次唤醒恢复、一次 PROCESSING/思考停滞后再次唤醒恢复。13:38 日志共记录 14 次 WakeNet，覆盖 LISTENING、PROCESSING 和 SPEAKING；该窗口未见 Task WDT、AFE full、WebSocket task 创建失败或 Audio TX 失败，但长 SPEAKING 段 feed/fetch 最低约为 5.3/10.3 次每秒，最大成功间隔约 262/240ms。
- 结论：结合该日志时点源码/README 基线，`7/6/5` 没有消除播放期实时性竞争，且曾与历史 CPU0 WDT/ECM 超时同窗；`4/6/5` 会使 playback 高于 feed，不符合播放期唤醒目标。EX-024 统一为 feed/fetch/playback `5/6/4`，并以 `_Static_assert` 固化 `fetch > feed > playback`。
- 该修正版已完成 Windows clean build，应用 BIN `0x2baa20`，最小 app 分区剩余 `0x1455e0`（32%）；主机测试因无 `gcc/clang/cl` 未执行。随后已在 COM7 完成全片擦除和五分区烧录，五项 Hash 校验通过并完成 RTS 硬复位；目标板行为结果见第 17 节。

## 17. 2026-08-04 16:17 TALK/LISTEN 打断回归

### 17.1 日志归档

- 原始串口日志已原样保存为 `robotlog/2026-08-04_16-17-50_ex024-talk-listen-interrupt-com7.txt`，共 4207 行、216975 字节，SHA-256 `C6D64D6FA21AA340D0D54B76D8EC653738C51541FDB6A18400FEDBFE5279C63A`。
- 用户人工判断为 TALK 无法有效打断、LISTEN 打断基本成功。本轮只完成日志与源码分析，没有修改功能源码、构建、烧录、测试或执行 Git。

### 17.2 已确认事实

- LISTENING 重唤醒链路成功：`84866ms` 检出 WakeNet，`87757ms` 开始同会话重聆听，`87775ms` 发送 abort，`87815ms` 发送 listen start，`87839ms` Controller 进入 LISTENING，`87993ms` 首帧上传成功；`113136ms -> 116995ms` 和 `184664ms -> 186517ms` 也走通同类路径。
- SPEAKING 期间至少两次 WakeNet 真正检出。`150015ms` 检出后本地 TTS 在 `150032ms` 停止，但 Agent 必须等待 DOA 和 67.5 度闭环转向完成，直到 `153275ms` 才收到新 wake，端到端延迟约 3.26s；旧服务端 TTS 已在 `151606ms` 自然结束并关闭旧会话。该串行顺序会直接放大 TALK 打断等待。
- `159898ms` 的另一轮 SPEAKING 检出因 DOA 无效而跳过转向：`160136ms` abort、`160183ms` listen start、`160214ms` 进入 LISTENING，说明 WakeNet、abort 和 listen 事务本身可以工作。`160108ms` 的旧 request audio stop 返回 `ESP_ERR_INVALID_STATE`，原因是 Controller 在接受新 wake 时已经提前停止/失效旧输出，随后 Agent 的 retired stop 成为重复停止；该返回值在本窗口不是 TTS 继续播放的直接证据。
- 同一 WebSocket 会话中存在旧文本事件跨 request 的缺口：binding 在 `request=9 -> 10` 后立即改写，服务器旧 `tts.stop` 和 `tts.sentence_start` 仍带同一 `session_id` 到达，因此被回调标成当前 request。旧 stop 被 LISTENING 状态门禁忽略，sentence_start 只打印日志。`163460ms` 的 TTS start 前已有当前请求 STT（`163449ms`），所以本轮没有证据证明旧 TTS start 抢回 SPEAKING；但当前 request/session 拒绝契约仍未完整满足。

### 17.3 高置信原因与证据边界

- 最高优先级怀疑是播放期 CPU0 Audio 实时性不足导致 WakeNet 召回下降。当前 feed/fetch/playback 虽为 `5/6/4`，三个任务仍固定 CPU0，且 fetch 与 Opus 编码仍在同一任务中；上传门禁关闭时仍持续编码，Controller 只在编码后丢弃数据。
- 空闲/轻载窗口 fetch 约 `19～22/s`、feed 约 `10～11/s`；长 TTS 窗口 fetch 降到 `14.1～15.9/s`、feed 降到 `7.1～7.6/s`，最大间隔约 `175～219ms`。日志同时出现 3 次 CPU0 Task WDT（`103701ms`、`169577ms`、`196520ms`）、一次 AFE empty、播放队列峰值 32，以及 `play_drop/errors/upload_drop` 持续增长。
- 这些数据证明 `5/6/4` 尚未解决播放、采集、AFE 和编码在 CPU0 的竞争，也能解释 TALK 比 LISTEN 更难唤醒；但日志无法记录用户说出却未检出的唤醒词，因此“漏检率”仍需带人工测试标记的成组回归确认，不能仅凭缺少 WakeNet 日志计算成功率。
- 网络质量 `F Bad` / WiFi 无 IP 告警同窗频繁出现，但设备的 link/IPv4/internet 快照保持 ready，关键 TALK 窗口无 WebSocket 断开、abort 发送失败或 Audio TX 失败，当前不列为 TALK 主因。

### 17.4 当时的下一步门禁（已由第 18 节完成）

- 当时要求先形成并人工确认 TALK P0 方案，不直接继续调任务优先级。方案需同时处理：活动态 wake 的 Agent 通知不再被 DOA/转向串行阻塞；AFE fetch/WakeNet 与 Opus 编码解耦，上传门禁关闭时跳过编码；同会话 abort 后旧 TTS 文本事件具备明确隔离或收敛屏障。方案及实现已由第 18 节收口。
- 修改后回归仍必须给每次人工 TALK/LISTEN 尝试加时间标记，分别统计“说出唤醒词 -> WakeNet 检出”“检出 -> 本地停播”“检出 -> abort/listen ready”，并记录 feed/fetch、最大间隔、PCM/播放队列、编码耗时、丢帧和 Task WDT；第 18 节完成的构建与烧录不替代该行为验收。

## 18. 2026-08-04 TALK/Audio P0 实施交付

### 18.1 逻辑枚举与源码评审

- 功能源码修改前按 5 个唤醒来源、2 个 Agent 结果、5 个 DOA 结果、4 个 Motion 结果、2 种 READY/旁路顺序、2 种连续 wake、3 种网络/core 结果和 4 种旧 TTS/audio 组合执行 9600 组枚举；修正设计缺口后异常为 0，终态为 `IDLE=4800`、`LISTENING=1600`、`ERROR=3200`。
- 实现包括活动态 Agent-first `abort + listen`、DOA/WAKE_TURN 旁路、AFE fetch/WakeNet 与 Opus encode 拆分、固定 8 帧 PCM 队列、严格 TTS barrier，以及覆盖 wake、Agent、旁路、barrier、feed/fetch/encode、PCM queue、旧 audio 和 upload drop 的 `[DEBUG-AI-P0]` 日志。
- 源码评审修正两处竞态：TTS barrier 不再允许 `request_id=0` 通配；Opus 回调携带编码开始时冻结的 `generation/wake_seq/session_epoch/request_id`，Controller 与当前 upload gate 严格相等后才发送，旧结果记录 `phase=upload_drop` 并丢弃。

### 18.2 构建与 COM7 烧录

- self-test 配置 clean build `1812/1812`，应用 `0x2c2cc0`，4MB app 分区剩余 `0x13d340`（31%）；默认配置 clean build `1811/1811`，应用 `0x2bc720`，剩余 `0x1438e0`（32%）。默认生成配置确认 `CONFIG_XIAOZHI_COMPANION_SELF_TEST` 未设置。
- 默认构建首次在 ESP-IDF `esp_lcd_panel_rgb.c:700` 遇到已知 GCC ICE；保持同一成熟 runner 和并发 2 重试后完整通过。Windows 无 `gcc/clang/cl`，主机测试未执行；目标 self-test 代码已编译链接但未在板上运行。
- COM7 已先完成全片擦除，再写入 bootloader、默认应用、partition table、`srmodels` 和 SPIFFS；五项均通过 Hash 校验，最后完成 RTS 硬复位。本轮未打开串口或执行人工功能测试。

### 18.3 当前验证入口

- 当轮目标板运行默认应用 `0x2bc720`；该应用和第 17 节均为历史证据，不得作为后续修正版行为结论。后续状态以本文件“当前状态摘要”和第 20 节为准。
- 当轮下一步是由用户按 `LISTENING/PROCESSING/SPEAKING` 分组执行 TALK/LISTEN，并记录人工时间标记与 `[DEBUG-AI-P0]` 指标；该动作已被第 19 节和第 20 节的后续修正状态取代。

## 19. 2026-08-05 14:13 SPEAKING 打断进入 ERROR

- 日志：`robotlog/2026-08-05_14-13-26_ex024-talk-interrupt-error-com7.txt`，802 行、63708 字节，SHA-256 `10DC8BB1DAFD248359BE7579E1D4BA86810FEB7ADA649E3568595CFF5DA30BA6`。`LISTENING` 重唤醒成功；`SPEAKING` 重唤醒完成 WakeNet、本地停播、`serial=1` DOA submit 和 `SPEAKING -> LOCATING`，随后触发 `TTS outside SPEAKING`，音频快照为 `TTS/ACTIVE`。
- 根因是 `stop_runtime_effects()` 原顺序先同步停播、最后才清 Controller binding。停播返回后、binding 清除前，旧 request 的迟到音频回调仍可通过 Adapter/Controller 校验，并在产品状态仍为 `SPEAKING` 时重新把 Audio owner 设为 `TTS/ACTIVE`；随后状态转 `LOCATING`，严格不变式正确进入 ERROR。
- 第一轮 9600 组枚举覆盖了旧 TTS/audio 的输入类别和终态，但没有覆盖同步 effect 与异步回调的交错顺序，因此不能视为全部并发时序已穷尽。修复明确把 binding 退休作为线性化点：Controller 先清 binding，Agent adapter cancel 先清自身 binding，随后才执行底层异步 cancel 和按保存 token 的 Audio stop。严格不变式不放宽。
- 新增 binding 已退休后迟到音频必须 DROP 的纯 C 用例，以及 `controller_binding_retired/agent_binding_retired/agent_cancel/audio_stop` 顺序日志。该轮 self-test clean build `1812/1812`、应用 `0x2c30e0`；默认 clean build `1811/1811`、应用 `0x2bc650`，默认 self-test 未设置。Windows 无 `gcc/clang/cl`，主机测试未执行；目标板 self-test 未执行。上述构建号均为该轮历史证据。
- 该轮 COM7 已先全片擦除并完成五段写入，均通过 Hash 校验并硬复位；该烧录结果对应历史默认应用 `0x2bc650`。其后的 14:46 request/deadline 修正版另行生成 `0x2be6f0`，尚未烧录。

## 20. 2026-08-05 14:46 后静态修正版状态

### 20.1 日志与结论

- 原始日志已保存为 `robotlog/2026-08-05_14-46-43_ex025-talk-interrupt-think-error-com7.txt`，3801 行、201329 字节，SHA-256 `1CD73FFA1120E65D91235FE8490154D5C87F5FAA5BE497772FA8B9DC6A98CC22`；文件名沿用原始归档名，内容为本 Demo 的 EX-024 COM7 日志。
- 日志确认 `PROCESSING` 重唤醒曾进入 `LOCATING -> CONNECTING -> LISTENING`，`SPEAKING` 也出现 WakeNet 检出、binding 退休、Agent cancel、audio stop 和 `serial=1` DOA；但部分用户操作没有形成 WakeNet 事件，不能据此宣称 TALK 已通过。

### 20.2 静态审查与源码状态

- 审查发现并修正四类竞态：旧 `open_session()` 误关 replacement request；阻塞网络阶段继续使用旧 request；停止态/跨 epoch 的迟到 cancel 或 completion 改变当前 worker；旧 deadline 在清理副作用前关闭 replacement request。
- 当前实现采用 opening request 原子激活/重绑、live request 动态刷新、停止态网络提交门禁、跨 epoch cancel 隔离和 deadline token 二次核对；旧 binding 仍先退休，迟到旧音频只记录 drop，不放宽 `LOCATING` 禁止 `TTS/ACTIVE` 的不变式。

### 20.3 构建、烧录与验证边界

- 最新默认 clean build `1811/1811` 通过，应用 `0x2be760`，剩余 `0x1418a0`（31%）。主机用例因 Windows 无 `gcc/clang/cl` 未执行；目标板 self-test 已执行并通过，人工 TALK/LISTEN 和 WebSocket 故障注入/压力回归待执行。
- 2026-08-06 按 `design.md` 4.1.2 对 COM7 完成默认 Demo 全片擦除和五段烧录；bootloader、应用、分区表、`srmodels`、`spiffs_data` 均 `Hash of data verified`，最后完成 RTS 硬复位。当前目标板运行 `0x2be760`。

### 20.4 重启后的唯一恢复入口

1. 保持当前 `0x2be760` 固件，执行带时间标记的 `LISTENING/PROCESSING/SPEAKING` 重唤醒回归和 WebSocket RAM P0 故障注入；重点核对 `controller_binding_retired -> agent_binding_retired/agent_cancel -> audio_stop -> serial=1 doa_submit` 顺序、`TTS outside SPEAKING`/`ERROR` 是否消失，以及 `[DEBUG-AI-P0]` 实时性指标。Demo 串口日志由用户手动抓取。

## 21. 2026-08-06 16:02 transport 复用验证与双状态并行变更

### 21.1 日志与逐唤醒结论

- 日志：`robotlog/2026-08-06_16-02-17_ex024-talk-listen-interrupt-com7.txt`，7175 行、399289 字节，SHA-256 `ADE699D2149682ECA7FDFECE2E967E66D9DA94F80ED43BAEDC61E014442F7E39`。
- 14/14 次 WakeNet 均被 Controller 接受并产生新 `LISTENING_READY`；2/2 次 LISTENING 打断、8/8 次 SPEAKING 打断在日志层面成功。活动态 10/10 次保持同一健康 transport，以新 `request_id` 执行 `abort + listen`，没有固定重跑 version check。
- TALK 第 4/10/13 次从 WakeNet 到新 READY 分别约 2403/2821/1795ms，与转向等待或 `ESP_ERR_TIMEOUT` 同窗；其余 TALK 约 517～618ms。根因不是 transport 重连，而是该实测固件的 Controller 仍在 motion 收口后才提交 Agent begin。
- 电机板电池无电，本轮全部转向没有真实运动；这些超时只能证明 Motion 失败出口被执行，不能评价方向、角度或闭环算法。

### 21.2 当前正式设计与回归边界

- 同一有效 wake 只分配一个 `generation/wake_seq`，随后 session 与 motion 两个子事务立即独立推进。motion 未完成、失败、超时或无电不得阻塞、延迟、取消或关闭 session；session READY/FAILED/CLOSED/deadline/transport reconnect 也不得反向取消 motion。
- 只有新 wake 换代、网络/core fatal 和安全停机可以共同取消两平面。显示采用 session 基础状态加 motion gaze/角度叠加；触摸和说话嘴部按既有优先级覆盖 gaze，只是视觉覆盖，不是运行门禁。
- 保留禁止回归项：Controller/Adapter binding 先退休、严格 TTS barrier、四元 token、opening request 原子换绑、迟到网络提交门禁、跨 epoch cancel、deadline supersede、单 Agent worker和三次 WebSocket start 重试。
- 开发顺序固定为：已知问题影响矩阵 -> 单行为纯 C RED -> 最小源码修改 -> 该行为 GREEN -> 下一个行为 -> 受影响 LLVM 全回归 -> clean build -> 后续当次授权的全片擦除/五段烧录。当前双状态 model/policy、真实 wake-effect 入口、单平面失败隔离、共享 network fatal 和 DOA/Motion completion 不请求 Agent 均已完成，LLVM 为 `PASS (0 failures)`，clean build 为 `1813/1813`，应用 `0x2bf200` 已按成熟入口完成 COM7 全片擦除和五段校验烧录；下一步是补充完整 Controller/Adapter 级交错并由用户执行目标板活动态体验，不能用构建结果替代实机体验。

### 21.3 实施、纯 C 与构建结果

- 单行为 RED 证据：Motion completion 不请求 Agent 初始为 `expected=0 actual=2`；DOA 无效和死区不请求 Agent 初始共 2 个失败。最小修改后上述行为全部 GREEN。
- `model_on_doa()` 的非转向完成出口和 `model_on_motion_done()` 当前固定返回 `DECISION_NONE`；真实 `handle_audio_wake()` 固定先尝试 Agent effect、再尝试 Motion effect，任一同步失败仍尝试另一平面。兼容枚举和 `apply_decision()` 分支为避免扩大接口变更而保留，已明确标记为 current model 无运行入口。
- 全部受影响 LLVM 主机纯 C 回归实际结果为 `companion_core_test: PASS (0 failures)`；既有 binding、TTS barrier、四元 token、deadline、transport 复用和 WebSocket 重试保护未回退。
- 按 `design.md` 4.1.2 成熟入口完成默认 clean build `1813/1813`，应用 `0x2bf200`，最小 app 分区剩余约 31%；本阶段未连接 COM7、未擦除/烧录、未抓取串口日志、未执行 Git。

### 21.4 2026-08-07 COM7 烧录结果

- 用户提供设备管理器证据确认 `USB-SERIAL CH340 (COM7)` 正常；此前 `Win32_SerialPort` 两次未枚举到 COM7 属于假阴性，不再把该 WMI 查询作为 CH340 烧录门禁。
- 按 `design.md` 4.1.2 先全片擦除，再烧录 bootloader `0x0`、应用 `0x10000`、partition table `0x8000`、`srmodels` `0x410000` 和 `spiffs_data` `0x710000`；五段均 `Hash of data verified`，最终 `Hard resetting via RTS pin... Done`。
- 目标板应用为 `0x2bf200`，最小 app 分区剩余约 31%。本轮按用户既定要求未打开串口或抓取 Demo 日志，活动态体验由用户手动验证。

## 22. 2026-08-07 09:27 TALK/LISTEN 打断回归

### 22.1 日志归档与测试边界

- 原始日志已原样保存为 `robotlog/2026-08-07_09-27-54_ex024-talk-listen-interrupt-com7.txt`，共 4774 行、269353 字节，SHA-256 `06C7DC0AEB6598C90FFE3CA18C38ED2782CB6B3962554DFFAB09F4751898EC65`。
- 用户人工现象：TALK 首次打断成功；LISTEN 首轮出现一次疑似卡住；后续 TALK 仍有无法打断，最后两次最明显。
- 当前测试硬件不带实体喇叭，人工只通过 UI 状态判断流程。该事实排除 TTS 声音回灌、实际响度和声学遮蔽，但不会停止软件提示音/TTS 播放任务，也不会取消其资源交接、调度和 AFE reset 副作用。
- 本节只分析并更新文档，没有修改功能源码、运行测试、构建、烧录或执行 Git。

### 22.2 逐次 WakeNet 流程

日志共出现 9 次 `WAKENET_DETECTED`、9 次 Controller `wake_detect` 和 9 次新 `listen_ready`；`wake_reject` 始终为 0。下表的耗时按 ESP 日志时间计算。

| 序号 | generation/wake_seq | 检出来源 | WakeNet 到新 READY | 后续结果 |
| --- | --- | --- | ---: | --- |
| 1 | `3/2` | `IDLE` | 2840ms | 完整建连后进入 LISTENING，VAD end 后进入 PROCESSING；随后被第 2 次唤醒替换 |
| 2 | `4/3` | `PROCESSING` | 482ms | 健康 transport 重聆听成功；后续进入 PROCESSING、SPEAKING 并正常回 IDLE |
| 3 | `6/4` | `IDLE` | 2694ms | 完整建连后进入 LISTENING；随后被第 4 次 LISTENING 重唤醒替换 |
| 4 | `7/5` | `LISTENING` | 525ms | 重聆听成功，VAD end 后进入 PROCESSING、SPEAKING |
| 5 | `8/6` | `SPEAKING` | 267ms | 本地停播约 63ms，重聆听成功；随后再次进入 PROCESSING、SPEAKING |
| 6 | `9/7` | `SPEAKING` | 581ms | 本地停播约 74ms，进入 LISTENING；约 30s 内没有有效 VAD end，deadline 正常回 IDLE |
| 7 | `11/8` | `IDLE` | 2904ms | 完整建连后进入 LISTENING；随后被第 8 次 LISTENING 重唤醒替换 |
| 8 | `12/9` | `LISTENING` | 464ms | 重聆听成功，后续进入 PROCESSING、SPEAKING 并正常回 IDLE |
| 9 | `14/10` | `IDLE` | 2707ms | 完整建连后进入 LISTENING、PROCESSING、SPEAKING；最后两次人工 TALK 尝试期间没有新的 WakeNet 事件，最终自然回 IDLE |

结论：只要 WakeNet 实际检出，本轮 9/9 都被 Controller 接受并产生新 READY。活动态 `binding` 退休、健康 transport 复用、`abort + listen` 和 Session/Motion 非门禁均未见回退；UI 上的疑似失败不能归因于 Controller 拒绝唤醒。

### 22.3 LISTENING 疑似卡住

- 第 6 次唤醒在 `SPEAKING` 检出后 581ms 已产生 `generation=9/wake_seq=7` 的新 READY，Controller 并未卡在 CONNECTING。之后约 30s 保持 LISTENING，最终由既有 deadline 有界回 IDLE。
- 该 LISTENING 窗口共记录 23 个完整 VAD end，`active_fetches` 为 `7,4,16,3,6,7,18,3,7,9,4,15,13,4,6,14,3,6,5,10,17,13,19`，最高 19，全部 `emit=0`。当前 EX-024 本地门槛为 `COMPANION_AUDIO_VAD_MIN_ACTIVE_FETCHES=20`，因此没有任何一次 VAD end 上送 Controller。
- READY 后约 715ms，无声提示音才结束并调用 `audio_processor_reset_buffer()`。源码契约允许 READY 先于提示音 terminal，提示音结束回调又无条件 reset AFE；这会在 UI 已显示 LISTEN 后改变识别器上下文，是当前最高优先级时序风险。但本日志中 READY 后首个 466ms/7-fetch VAD 段在 reset 前约 176ms 已结束，不能仅凭本日志断言 reset 直接切断了该段语音。
- 因此本轮能确认的是“已 READY，但后续语音被切成多个未达本地门槛的 VAD 段”，不能确认是 Controller 卡死；提示音 terminal/reset 是否导致或放大切分，需要在纯 C 时序 seam 中复现后才能修改。

### 22.4 TALK 最后两次无法打断

- 最后一个 SPEAKING 窗口从 `2176614ms` 持续到 `2188730ms`。按用户操作记录，最后两次 TALK 尝试位于此窗口；日志没有新的 `WAKENET_DETECTED`、Controller `wake_detect` 或 `wake_reject`，所以失败点在 Controller 之前。
- 该窗口存在多段 VAD，最长约 1189.6ms/20 个 active fetch；另外可见 956.3ms/16、1013.4ms/17 等语音段，证明采集和 AFE VAD 仍在运行，但 WakeNet 没有命中。VAD 命中不能代替有效唤醒词，也不能绕过 XCR-051 的 WakeNet 门禁。
- 失败窗口 fetch/feed 约为 `16.8~20.7/8.3~9.9` 次每秒；此前一次成功的 SPEAKING 唤醒前同类速率约为 `18.0~19.9/8.7~10.4` 次每秒。调度降速可能放大漏检，但相近负载下也能成功，当前证据不足以把它定为唯一根因。
- 本轮无 Task WDT、panic/assert、runtime invariant、`TTS outside SPEAKING`、TDM read error、Controller queue/upload/play drop 或非零 wake reject。PCM encode `queue_drop` 累计到 6，但最终失败窗口没有继续增长，不能解释最后两次 WakeNet 漏检。

### 22.5 文档冲突与下一门禁

- `requirements.md` XCR-032 和既有设计写“最短有效 VAD 30 次 fetch”，当前源码实际分为两层：EX-024 本地 VAD end 门槛为 20 个 active AFE fetch；公共 Agent 在至少发送 30 个 Opus TX frame 后才允许执行 pending VAD stop。两者不是同一个计数器，也不能合并描述为“30 次 fetch”。正式需求数值暂不改，冲突标记为待确认。
- 下一步若进入修复，必须先建立能驱动真实时序的纯 C seam：覆盖 `LISTENING_READY -> prompt terminal/reset -> VAD/WakeNet` 交错，并单独回放 SPEAKING 下“有 VAD、无 WakeNet”的采样/调度条件。RED 可重复前不得修改提示音、VAD 门槛、WakeNet 阈值或任务优先级。
- 禁止回归边界不变：binding 先退休、严格 TTS barrier、四元 token、健康 transport 复用、CONNECTING replacement、迟到网络提交、跨 epoch cancel、deadline supersede、Session/Motion 双平面和 WebSocket 三次有界重试均不得改动或放宽。

### 22.6 后续需求确认（2026-08-07）

- 22.5 的 Q-027 待确认状态已结束：用户确认统一为当前 request 成功发送的 20 个 60ms Opus TX 帧，由 Agent 单一计数；Audio 对匹配 token 的 AFE `VAD_END` 只上报边沿，不再按单个碎片 active fetch 数设置第二道门禁。正式口径以 `requirements.md` XCR-032/XCR-056/XCR-AC-042 和 `design.md` 当前章节为准。
- 提示音自然终态不 reset AFE 已评估为更优候选，但仍是 Q-028 待确认项：正常 `FINISHED/STOPPED` 不得在新 `LISTENING_READY` 后无 token 地 reset 当前 AFE；初始化、模块停止、fatal recovery 和 pre-arm 显式旧输出停止的受控 reset 不在删除范围。
- 本次只同步需求、设计和恢复入口，功能源码尚未修改。进入实现前仍必须先建立 VAD 19/20/21、多个碎片累计、旧 prompt terminal 晚于新 READY、旧 generation terminal 及全部禁止回归项的纯 C RED。

### 22.7 挂起交接（2026-08-07）

- 用户要求小智机器人继续作为后续主要任务，但当前保持代码、日志与问题现场，完成独立 AEC 调试记录后等待下一条命令；挂起期间不继续修改、构建或烧录 EX-024。
- 未实施项保持不变：VAD 从“Audio 20 active fetch + Agent 30 TX frame”收敛为 Agent 单一 20 个成功 TX 帧；Q-028 正常提示音终态不 reset AFE 仍待确认；对应纯 C RED 尚未建立。
- 09:27 日志采集时硬件无实体喇叭；当前已更换实体喇叭，但只测试了独立 EX-010 AEC-OFF 自透传，不能把该啸叫日志外推为 EX-024 的 TTS/WakeNet 结果。
- 恢复顺序固定为：重新读取 `coding_rule.md` 和本节 → 确认 Q-028 → 建立 prompt terminal/reset、VAD 19/20/21、碎片累计、旧 token/换代/deadline 及禁止回归项的纯 C RED → 最小功能修改 → 受影响纯 C 全回归 → clean build → 获得当轮授权后再全片擦除/烧录和实机验收。
- 历史 EX-024 应用 `0x2bf200` 的构建、烧录和日志证据继续保留；COM7 当前运行 EX-010 `audio_aec_demo` `0xaba70`，不再运行本应用。
