<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# xiaozhi_ai_demo

小智 AI 平台接入示例：通过 LTE/CAT1 联网，接入小智 AI 对话平台，实现唤醒词触发语音对话。

## 功能

- LTE/CAT1 4G 联网（复用 lte_net_demo 方案）
- 小智平台 WebSocket 协议对接（版本检查 → WS 连接 → 会话管理）
- 唤醒词 "你好小智" 触发对话
- `MIC1 + MIC2` 双通道 AFE 处理（NS + VAD + WakeNet，无 AEC）
- Opus 16kHz 单声道上行编码、24kHz 单声道下行 TTS 解码播放
- TTS 下行播放 + 打断支持
- 无屏幕运行，状态通过串口日志和平台交互确认

## 当前源码音频口径

- TDM 原始输入为 `Slot0=MIC1`、`Slot2=MIC2`、`Slot1=REF`；当前生效 feed 只提取 Slot0 和 Slot2，按 `MM` 顺序送入 AFE，Slot1 不参与 AFE
- AFE 配置为 `mic_channels=2`、`ref_channels=0`、`enable_aec=false`
- ES7210 MIC1/MIC2 PGA 为 37.5dB，输出音量为 50
- `main/CMakeLists.txt` 只编译 `xiaozhi_ai_demo_main.c` 并依赖公共 `xiaozhi_audio` 组件；`main/` 目录下残留的同名音频源码不参与当前构建，不得作为生效实现解读
- 2026-07-13 已验证 MM 双麦无 AEC 方案的空闲唤醒、播放期打断和完整对话；2026-07-21 当前源码完成 clean build、全片擦除、烧录和启动监控，但该轮未重新覆盖完整对话

## 已知问题

- 2026-07-23 用户报告一次疑似现象：唤醒并完成一轮正常对话后，设备可能未稳定回到静默待机，后续曾在没有再次说唤醒词的情况下突然回应外部人声。
- 当前没有对应串口日志、发生时间、稳定复现步骤或复现概率，尚不能确认根因，也不能确认是否由本地状态机、VAD 事件、服务器会话状态或其他链路引起。
- 本问题暂只记录，不修改 `xiaozhi_ai_demo` 功能源码；后续复现与采集要求见 `DEBUG_ANALYSIS.md` 的 2026-07-23 条目。

## 硬件

- 核心板 L-AIWFS300-A0（ESP32-S3 + ES7210 + ES8311 + HT6872）
- MIC 板 L-AIWFS300-B0（双麦克风）
- LTE 模块 NT26-KCN B（USB ECM）

## 配置

通过 `menuconfig` 配置以下参数：

| 参数 | Kconfig | 默认值 |
| --- | --- | --- |
| 设备 MAC | `XIAOZHI_DEVICE_MAC` | `2e:ee:8c:6b:58:28` |
| OTA URL | `XIAOZHI_OTA_URL` | `https://api.tenclass.net/xiaozhi/ota/` |
| 激活 URL | `XIAOZHI_ACTIVATION_URL` | `https://api.tenclass.net/xiaozhi/ota/activate` |
| 语言 | `XIAOZHI_LANG` | `zh-CN` |

## 构建与烧录

```bash
bash CODE/tools/build_example.sh xiaozhi_ai_demo
bash CODE/tools/build_example.sh xiaozhi_ai_demo flash
```

## 设计文档

详见 `design.md` 11.20 节；AEC 自检与三通道方案只作为历史诊断记录，当前生效方案以本节和实际公共组件源码为准。
