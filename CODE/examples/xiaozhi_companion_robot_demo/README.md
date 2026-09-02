<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# xiaozhi_companion_robot_demo

小智陪伴机器人独立 Example。屏幕/触摸、电机、SW3、4G 状态观测、基础声源定位和 AI 对话已有分阶段实机证据；网络状态、Controller 门禁、UI 和离线 ROAM 均以公共 `network_manager` 的事件/只读快照为准。蜂窝仅被动观测：不直接查询 `lte_hal`/`lsd_net_mgmt`，不自动 reconnect、LTE power cycle、retry、退避、自检或 manager 重建；SIM 插拔后的自恢复不属于本 Demo 需求。公共组件现通过闭源库既有 4G connected/disconnected 事件及时提交状态，本工程设置 `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=5` 作为 ESP-NETIF 被动保底。2026-08-14 含直接 4G 事件入口的新固件已完成 clean build `1826/1826`、COM7 全片擦除和五段校验烧录，应用 `0x2c5fd0`（4MB app 分区剩余约 31%）；本轮按人工测试安排未自动抓取串口日志，行为验收待执行。2026-08-13 T6 的 180 秒日志继续作为旧镜像 READY/持续心跳基线。TALK 播放期唤醒漏检仍是独立未闭合问题，不纳入本轮网络结论。

状态更新时间：2026-08-14

## 当前范围

- 与其他 Demo 平级，独立工程路径为 `CODE/examples/xiaozhi_companion_robot_demo/`。
- 当前提供 `companion_core`、`companion_logic`、`companion_turn_control`、`companion_motion`、`companion_input`、`companion_expression`、`companion_touch_gesture`、`companion_ui`、`companion_audio`、`companion_doa`、`companion_controller`、`companion_agent_adapter`，并使用公共 `network_manager`；同时保留默认关闭的目标板 self-test、主机测试 runner、分区和 SPIFFS。表情审核图、生产 manifest 和 96 张 80×60 生产预览位于 `assets/expressions/`，生成的 4-bit C catalog 位于 `components/companion_expression/generated/`。
- 公共 `CODE/components/xiaozhi_agent` 仅包含已批准且保持旧调用方兼容的 session/request 扩展；其他公共组件保持只读依赖。本 Example 不直接依赖其他 Example 的 `main` 组件。
- 不包含热插拔、摄像头、舵机、电池管理、避障、防跌落或自主导航。
- 历史实板基线已覆盖扩展 self-test `0 failures`、SW3 正式单击、有效唤醒与 1024ms DOA/转向、4G `net=1`，并在 Round 5 打通 WebSocket、上传、STT、TTS 和回 IDLE。当前播放音量固定为 90。目标板当前默认应用为 T6 烧录的 `0x2c5f30`；网络组件回归已完成，其他完整 AI/Controller/Adapter 交错体验仍按各自验收项独立跟踪。
- 当前源码在原 `companion_input` ADC 任务中增加 SW3 1500ms 长按返回 TF Launcher 事件，保留原有短按切换逻辑且不创建第二个 GPIO8 读取任务。该增量 clean build 通过，尚未烧录或实机回归。

## 当前增量参数与状态

| 增量 | 当前参数 | 已完成证据 | 当前验证状态/下一步 |
| --- | --- | --- | --- |
| DOA 方向共识 | 最近 12 个合法样本，至少 4 个；方向票数至少 55% 且领先 1 票；获胜方向中值、MAD `<=20°`；`ACTUAL_REL=clamp(RAW_REL×2.25,-90°,90°)`，最终实际角转向门槛 10° | 源码和静态审核完成；self-test 配置 clean build 通过 | 左/中/右方向、10°边界和实际角度 |
| BMI260 闭环转向 | 三轴陀螺零偏、三轴加速度重力轴投影、单调微秒真实 `dt` 梯形积分，2°/s 死区，5°/s 最低有效速度，EMA `alpha=0.25`，400ms 失速，停车提前量 0°，硬超时 `[1500,8000]ms`，Controller 期限 14s | 源码和静态审核完成；self-test 配置 clean build 通过 | 目标角、累计角、外部实际角和失败降级 |
| 表情颜色与布局 | 饱和度增益160%、上限85%；等比例110%，X=0、Y=-8px；正式96帧去除主题色角标；默认启动无6秒色卡 | 96帧角标/安全区和RGB565检查通过；史莱姆50.9% -> 81.3%；用户已确认当前实屏效果通过 | 当前修正版只复核状态关联无退化 |
| 随机移动降频 | 动作间停止 `[3000,8000]ms`；首次/恢复 3000ms；权重、单次动作时长和 0%/100% 输出不变 | 源码和静态审核完成；默认 clean build 通过 | 联网/离线 `IDLE` 连续 10min 频次观感 |
| 唤醒与打断回归 | WakeNet 阈值 0.65；有效唤醒先退休旧 binding，再并行启动 session 与 DOA/可选 WAKE_TURN；feed/fetch/playback/encode 为 `5/6/4/3`；playback 固定 CPU1；AFE fetch/WakeNet 与 Opus encode 拆分；固定 8 帧 PCM 队列；模型、MM 双麦无 AEC 链路和音量 90 不变 | 上一版 427 项 LLVM 纯 C、clean build `1812/1812`、应用 `0x2becb0` 和 COM7 烧录完成；16:02 的 14 次 WakeNet 全部进入 READY，10 次活动态复用健康 transport。当前 LLVM 全量纯 C 门禁为 `0 failures`，已覆盖独立状态/deadline、单平面失败隔离、真实 wake 入口及 DOA/Motion completion 不请求 Agent；clean build `1813/1813`、应用 `0x2bf200` 和 COM7 烧录已完成 | 补充完整 Controller/Adapter 交错和目标板故障注入；由用户验证活动态打断和旧问题回归 |
| 4G 被动状态观测 | 首次 LTE 上电+manager init 屏障 6000ms；运行期只消费 `network_manager` 事件/只读快照；组件直接消费闭源库既有 4G connected/disconnected，ESP-NETIF 5 秒 lost-IP 只作被动保底；4G 链路、IPv4、外网和活动接口分层为 `WAIT_4G`/`WAIT_INTERNET`/`READY_4G`；不执行主动蜂窝查询、reconnect、LTE power cycle、retry、退避、自检或 manager 重建 | 组件 21/21 纯 C 门禁通过；2026-08-14 EX-024 clean build `1826/1826`、应用 `0x2c5fd0`、COM7 全片擦除和五段烧录通过，人工日志待补 | 历史已就绪链路通过；SIM 插拔自恢复不属于需求；5 秒保底只消费被动事件，恢复只接受后续真实组件上报；未插卡和 IDLE/SPEAKING 断网待本轮人工覆盖 |
| WebSocket RAM P0 | managed `esp_websocket_client` 1.2.3；显式 4KB task stack；同 client 最多 3 次 start、相邻 100ms；重试前检查 worker/request cancel；连续失败销毁 client、释放 binding 并沿原失败出口回 `IDLE`；六阶段 RAM 探针 | LLVM 主机回归已纳入本轮全量 `0 failures`；`0x2becb0` 已完成历史 clean build 和 COM7 烧录，16:02 活动态健康 transport 复用未见退化 | 首次失败后成功、连续失败、取消和压力故障注入仍待目标板专项执行 |
| SW3 长按返回 | `pressed<=3000`、`released>=3500`、10ms 采样、30ms 消抖；先稳定释放，再持续按住 1500ms；`<=800ms` 短按保持原逻辑 | 复用原 ADC 任务，调用 `launcher_return_request()`，重启前停止 Motion/Audio；clean build `0x2bb5e0` 通过 | 待从 TF Launcher 真实 `ota_0` 启动后验证短按、长按、上电按住、返回页和无重复重启 |

`0x2b9740`、`0x2b9eb0`、`0x2baa20`、`0x2bc720`、`0x2bc650`、`0x2c30e0`、`0x2be760`、`0x2becb0`、`0x2bf200` 和 `0x2c5f30` 均作为历史回归基线保留；当前目标板为 2026-08-14 烧录的直接 4G 事件版 `0x2c5fd0`。该镜像已完成构建、全片擦除和五段校验烧录，尚未形成新一轮人工行为日志；完整 AI/Controller/Adapter 可靠性验收仍按其他验收项跟踪。

## 当前人工确认与优化顺序

1. **4G 被动状态人工验收**：当前 `0x2c5fd0` 已烧录，依次覆盖插卡冷启动、IDLE 拔卡、SPEAKING 拔卡、无卡冷启动 60 秒和 WAIT 状态重新插卡观测；只评价组件上报、Controller/UI 收口和无主动恢复，不把 SIM 重新插入后的底层自恢复作为通过条件。
2. **AI/motion 双状态并行**：修改前影响矩阵、逐行为 RED→GREEN、旧 Motion→Agent decision 清除、LLVM 全量 `0 failures`、clean build `1813/1813` 和 `0x2bf200` COM7 烧录均已完成；fetch/encode 解耦、playback CPU1/锁外解码、严格 TTS barrier、四元 upload token、request 原子激活/重绑和 deadline supersede 保持。下一步补充完整 Controller/Adapter 交错证据，由用户实测 LISTENING/PROCESSING/SPEAKING 打断。
3. **WebSocket RAM P0 补充验证**：13:38 日志中的多次新建/复用会话未再出现 WebSocket task 创建失败，但首次 start 人工注入失败后的 100ms 重试、连续三次失败清理和 request cancel 门禁仍需目标板专项覆盖。
4. **表情效果（P1）**：八套角色和每套 12 帧动态候选已通过人工审核。空闲主体固定/眨眼、触摸核心效果和 30px 滑动已通过；WebSocket 资源问题收口后继续检查八套角色的定位/转向、聆听/思考、说话和状态恢复。
5. **声源定位转向与 AI 对话体验（P1）**：历史带电样本证明 DOA 可触发且电机动作正常，实际转向角仍需带电标定；当前电机板电池无电，本轮超时不评价算法。AI 对话完整活动态打断、静默门禁和“1234”非唤醒回归在双状态 P0 实现后执行。

表情包按独立 pack 组织；左滑下一套、右滑上一套并跨首尾循环，默认 `icebox` 且初版不持久化选择。当前实屏标定值为 30px 最小水平位移、32px 最大垂直偏移和 700ms 最大时长；滑动优先总判定时间为 150ms，30px 灵敏度已由用户确认可接受。

## 安全边界

运动功能只允许在受控、平整、远离台阶和桌边且有人监护的场地运行。本 Demo 不具备避障或防跌落能力。当前默认配置会初始化电机并先执行安全停车；只有进入允许随机移动或有效唤醒转向的状态后才允许输出运动命令。未完成人工监护实机验证前，不得在桌面边缘、台阶附近或无人场地运行。

## 构建

按 `design.md` 4.1.2 的 Windows 成熟流程先全量镜像，再执行：

```powershell
& ".\CODE\tools\build_example.ps1" -Example xiaozhi_companion_robot_demo -Clean
```

目标板 self-test 默认关闭。使用 Example 内 runner 构建 self-test：

```powershell
& ".\CODE\examples\xiaozhi_companion_robot_demo\tests\run_target_self_test.ps1"
```

传入实际串口后，runner 会按成熟流程先全片擦除再烧录：

```powershell
& ".\CODE\examples\xiaozhi_companion_robot_demo\tests\run_target_self_test.ps1" -Port COM7
```

若系统 PATH 中存在 `gcc`、`clang` 或 `cl`，可以运行同一组公开接口用例：

```powershell
& ".\CODE\examples\xiaozhi_companion_robot_demo\tests\run_host_tests.ps1"
```

当前 Windows 环境已具备 LLVM/Clang。组件直接 4G 事件版最新 LLVM 纯 C 门禁为 21/21；EX-024 网络集成最近一次 Demo 主机回归结果仍为 `companion_core_test: PASS (0 failures)`。2026-08-14 按 `design.md` 4.1.2 全量镜像并采用已记录的 `CMAKE_BUILD_PARALLEL_LEVEL=2`、`NINJAFLAGS=-j2` 完成 clean build `1826/1826`；应用 BIN 为 `0x2c5fd0`，4MB app 分区剩余约 31%。COM7 已先全片擦除，再完成 bootloader、应用、分区表、`srmodels`、`spiffs_data` 五段 Hash 校验和硬复位。本轮按人工测试安排未自动抓取 EX-024 串口日志；2026-08-13 的 180 秒回归日志 `robotlog/2026-08-13_18-01-30_ex024-network-manager-passive-regression-com7.txt` 仅作为旧镜像基线。完整 Controller/Adapter 交错和 TALK 播放期唤醒仍按独立条目跟踪。

## Self-test 配置与验证状态

- `CONFIG_XIAOZHI_COMPANION_SELF_TEST=n`：默认产品配置，启动当前完整模块链并持续输出限频 heartbeat。
- `CONFIG_XIAOZHI_COMPANION_SELF_TEST=y`：启动时先执行公开接口用例并输出逐项 `PASS/FAIL` 与汇总；全部通过后继续启动当前完整模块链。
- self-test 配置只由 `tests/sdkconfig.self_test` 和 runner 开启，不进入默认固件配置。
- 当前正式门禁优先使用 LLVM 主机纯 C 入口；目标板 self-test、clean build、烧录和人工日志只能作为附加证据，不能替代新并行时序用例的实际执行。
- `DEV-0` 骨架版本的默认配置和 self-test 配置均已完成 clean build；其 self-test 固件已在 `COM7` 完成全片擦除和烧录，实机确认 10 秒门禁为 10006ms、5 个公开接口用例全部 PASS、汇总 `0 failures`，随后约 110 秒稳定心跳且无 panic、assert、watchdog 或重启，并已通过人工审核。
- 已烧录的扩展 self-test 基线固件为 `0x2720a0`，目标板汇总 `companion_core_test: PASS (0 failures)`，随后完整模块链稳定运行。Round 1 修复版 self-test clean build `1798/1798`，固件 `0x2728a0`、分区剩余39%，尚未烧录执行。
- Round 1 修复版默认配置确认 `CONFIG_XIAOZHI_COMPANION_SELF_TEST` 未设置且 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`，clean build `1797/1797`，固件 `0x270fe0`、分区剩余39%；已完成 COM7 全片擦除、五分区烧录和约150秒回归。13条 heartbeat 的 `read_err/errors/queue_drop/play_drop` 均为0，未见 panic、watchdog 或重启。新固件的屏幕状态映射、触摸、动作中SW3停车、左中右角度、完整对话和静默门禁仍待回归。
- Round 2 确认 ICCID 可读、4G 获取 IP `10.55.189.133`、controller 持续为 `net=1`。3次有效唤醒的 version check 均返回 HTTP 200，但均在创建 WebSocket 任务时失败并立即回 IDLE；上传、TTS 与完整对话尚未建立。网络质量监控仍高频输出 4G `F Bad` 和 `Target WiFi has no IP`。
- Round 5 已更新上述 WebSocket 结论：本文件段内 4 次有效唤醒均进入 DOA/转向和 Agent 链路，共记录 3 次新建 WebSocket 会话、1 次当前会话重启、4 次 STT 和 4 次 TTS start/stop，最终回 IDLE。Round 6/7 无唤醒，只证明空闲稳定，不能代替当前状态修正的功能回归。
- 空闲/滑动、状态完整性、六轴、网络、WebSocket RAM 和 Audio 调度固件均作为历史基线保留。14:13 暴露的 binding 交错以及 14:46 后的 replacement/deadline 竞态均已建立禁止回归保护。上一版 transport 复用的 LLVM 纯 C 427 项、默认 clean build `1812/1812`、应用 `0x2becb0` 和 COM7 烧录已完成；16:02 行为日志见 `project_memory.md`。新双状态并行实现已清除 Motion→Agent 旧 decision，通过当前 LLVM 全量 `0 failures` 门禁并完成 clean build `1813/1813` 与 COM7 烧录（应用 `0x2bf200`）；目标板 self-test 和实机交错仍未完成。

当前调试阶段每次上电或复位进入应用后，先输出启动延迟开始日志并等待 10000ms，再输出完成日志和执行 self-test/产品启动。该临时门禁仍是正式生效要求；后续只有在人工明确确认后才删除。

## 来源追踪

复制/参考基线提交：`3ea51b1`。

| 来源 | 当前使用内容 | 本地差异与验证状态 |
| --- | --- | --- |
| `CODE/examples/xiaozhi_ai_demo/` | 根工程结构、8MB/PSRAM 配置、4MB factory + 3MB model + 64KB SPIFFS 分区、提示音资源、WN9S/NSNET2、设备 MAC、纯 4G 网络、Agent 和音频参数基线 | 本 Example 在私有 `companion_audio` 和 `companion_agent_adapter` 中派生所需行为，网络使用公共 `network_manager`；公共 `xiaozhi_agent` 仅增加已批准的向后兼容事务接口。播放音量使用 90，WakeNet 阈值为 0.65。公共 Agent 已包含同 client WebSocket 有界重试、严格 TTS barrier、request/deadline 门禁和 `[DEBUG-WSRAM]/[DEBUG-AI-P0]` 探针；`0x2becb0` 已验证健康 transport 复用，双状态并行修改不得改变公共 Agent 既有保护 |
| `CODE/examples/lvgl_demo/` | 320x240 横屏、LVGL 任务、显示和 CST836U 触摸接入模式 | 当前本地实现由 `companion_ui` 保留 LVGL/触摸端口、PSRAM Canvas 和状态标签，表情策略/资源解析位于私有 `companion_expression`，触摸消抖/滑动仲裁位于私有 `companion_touch_gesture`；用户已确认八套角色基础显示及触摸/滑动核心效果，完整状态关联仍待实机验收 |
| `CODE/examples/motor_demo/` | 只读使用公共 `robot_motion`、`pt2466_motor` 和板级组件 | 本地 `companion_motion` 负责单所有者、异步动作和安全停车；联网进入 IDLE 后已执行全部六类动作及固定 0%/100% 命令，用户确认当前电机动作正常，受控场地逐动作量化仍待最终验收 |
| `CODE/examples/audio_dual_mic_doa_demo/` | 41mm 双麦 DOA、56dB 逐块门限、中值/EMA 滤波和 REL 映射基线 | 本地 `companion_doa` 只消费按 `wake_seq` 冻结的 16384 帧/约1024ms 唤醒快照，并仅将达到能量门限的 512 帧块送入滤波；Round 2 三次有效唤醒均产出有效结果并触发单次转向，左/中/右准确率与实际角度仍待标定 |
| `CODE/examples/audio_spatial_spectrum_demo/` | 音频与 UI 任务解耦、限频状态更新和轻量渲染组织方式 | 不复制 FFT 或频谱功能；本 Example 只保留像素表情和状态 UI |

## 当前硬件基线

- 主控：ESP32-S3，8MB Flash/PSRAM 配置沿用 `xiaozhi_ai_demo` 已验证基线。
- 电机：D0 PT2466，正式 GPIO 链路和方向以 `design.md` 5.3.3、11.24 为准；当前固件已接入 `companion_motion`，目标板初始化及启动/网络不可用安全停车已验证，完整动作待受控场地实测。
- 显示/触摸：320x240 ST7789V3 + CST836U，当前源码已接入 `companion_ui`、`companion_expression` 和 `companion_touch_gesture`；用户已确认八套角色基础显示、触摸/滑动互斥、30px 灵敏度、离线 `IDLE`、空闲眨眼以及160%/85%颜色补偿、110%/-8px布局、正式帧去角标和默认无色卡的当前实屏效果。状态修正版只需复核状态关联无退化；Round 2 的 FT6206 无 ACK 只作为历史现象保留。
- 双麦：MIC1/MIC2 声学中心距 41mm，TDM `slot0=MIC1`、`slot2=MIC2`；唯一 TDM reader、WakeNet/VAD、Opus、16384 帧/约1024ms 快照和 512 帧逐块 56dB 门限已接入并在 Round 2 连续触发3次有效 DOA/转向。左/中/右准确率与实际角度仍待标定。
- SW3：`SW_ADC -> GPIO8/ADC1_CH7`，正式路径使用三态采样、10ms采样、30ms消抖和最长800ms单击；当前窗口为 `pressed<=3000`、`released>=3500`，中间为UNKNOWN。短按模型切换已有实机证据；源码已增加 Motion ROAM role/token 执行层停车；同一 ADC 任务还增加“稳定释放后持续按住1500ms”的 TF Launcher 返回事件，返回前停止 Motion 和 Audio。长按增量已 clean build，未烧录；动作中短按立即停车、长按返回和屏幕联动待实测。
- 网络：历史插卡 4G USB ECM、ICCID、IP 和 `net=1` 已验证。当前源码以 6000ms 启动屏障保证首次 manager init 尝试先于 Audio tasks，manager 在进程内常驻；`WAIT_4G`、`WAIT_INTERNET` 和 `READY_4G` 只反映 `network_manager` 已提交的链路/IPv4/外网摘要。组件通过 linker wrapper 消费闭源库既有 4G connected/disconnected，本工程设置 5 秒 ESP-NETIF lost-IP 被动保底；两条路径都只更新状态，不触发恢复。EX-024 不主动查询蜂窝底层，不自动调用 reconnect、LTE power cycle、retry、退避、自检或 manager 重建；恢复只能由后续真实组件上报驱动。2026-08-14 直接事件版已完成组件 21/21 纯 C 门禁、EX-024 `1826/1826` clean build、COM7 全片擦除和五段烧录；最新 tracker 修复仍未重建/烧录，人工行为日志待补。历史 T6 日志证明已就绪链路的 4G 获 IP、READY 快照和持续心跳稳定。闭源库弱信号时打印的 WiFi 评估没有改变 4G-only 应用状态。TALK 播放期唤醒漏检仍是独立问题。

## 设计与验收依据

- 正式需求：`requirements.md` 9.5。
- 当前设计、阶段门禁和验证方法：`design.md` 11.24。
