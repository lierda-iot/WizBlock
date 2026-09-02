<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio NS Demo（噪声抑制对比演示）

录音 10 秒，分别播放原始录音和 ESP AFE NSNet2 降噪后的录音；320x240 横屏同步展示各阶段状态，并支持触摸重新开始完整流程。

## 功能

- 初始化 ESP AFE 框架（NSNet2 模式，无 AEC）
- 嘟声(1kHz) → 录音 10s（同时保存原始和 AFE 降噪后 PCM）
- 嘟声(1.5kHz) → 播放原始录音
- 嘟声(2kHz) → 播放降噪后录音
- 横屏展示 `PREPARING`、`RECORDING`、`PLAYING RAW`、`PLAYING NS`、`COMPLETE` 和 `ERROR`
- 阶段轨迹为 `REC → RAW → NS → DONE`，并展示录音/播放进度、`RAW RMS` 和 `NS RMS`
- 上电自动执行首轮；完成后点击 `Run again` 重新开始，失败后点击 `Retry` 重试
- 录音和播放期间按钮显示 `Running...` 并禁用，避免重复触发
- ES7210 MIC1 输入 PGA 在本 demo 内固定为 30 dB，ES8311 输出音量固定为 90%
- 不修改共享板级默认增益、PCM、提示音幅度和 NS 参数

## 设计要点

- 使用 `audio_processor` 组件封装 ESP AFE，配置为 format="M"（1MIC, 0REF）、ns=true、aec=false、vad=true
- MIC1 数据取自 TDM Slot0，原始与降噪 PCM 保存在 PSRAM
- 音频工作任务常驻等待启动通知，每轮独立初始化和释放 AFE 与 PSRAM 缓冲
- 音频状态写入受锁快照，独立 UI 刷新任务每 100ms 更新 LVGL，避免绘制阻塞录音
- 成功和失败路径都会关闭功放、释放每轮缓冲并反初始化 AFE

## 硬件连接

- I2S：MCLK=GPIO42, BCLK=GPIO41, WS=GPIO39, DOUT=GPIO38, DIN=GPIO40
- ES8311 I2C 地址：0x18（7-bit）/ 0x30（8-bit）
- ES7210 I2C 地址：0x40（7-bit）/ 0x80（8-bit）
- AMP_CTRL：IOEX P1_0
- LCD：ST7789V3，320x240 横屏
- 触摸：CST836U

## 验证状态

- 2026-06-29：原 NSNet2 音频链路构建和实机验证通过，降噪前后 RMS 对比日志正常
- 2026-07-17：状态逻辑主机测试通过，覆盖重跑许可和进度钳位
- 2026-07-17：60% 音量版本 macOS clean build 通过，固件大小 `0xf4f60`，5MB app 分区剩余 81%
- 2026-07-17：输出音量调整为 80%；当次 clean build 因工具链未找到 `p256-m_driver_entrypoints.c.obj` 而中止，待重新构建验证
- 2026-07-17：针对破音先降低输入端，MIC1 PGA 由板级默认 37.5 dB 在本 demo 内覆盖为 30 dB
- 2026-07-17：30 dB 输入、80% 输出版本 macOS clean build 通过，固件大小 `0xf5040`，5MB app 分区剩余 81%
- 2026-07-17：用户实机确认 30 dB 输入、80% 输出版本的破音“好了不少”
- 2026-07-17：下一轮输入目标为 34 dB；ES7210 无 34 dB 档，采用最接近的 34.5 dB 硬件档位，输出调整为 100%；macOS clean build 通过，固件大小 `0xf5040`，5MB app 分区剩余 81%
- 2026-07-17：确认 30 dB 与 34.5 dB 之间存在 33 dB 硬件档；33 dB/100% 版本 clean build 通过，固件大小 `0xf5110`，但未实机测试
- 2026-07-19：按最终选择恢复为 30 dB 输入，输出保持 100%；macOS clean build 通过，固件大小 `0xf5040`，5MB app 分区剩余 81%
- 2026-07-19：输入保持 30 dB，输出音量调整为 90%；macOS clean build 通过，固件大小 `0xf5040`，5MB app 分区剩余 81%，待实机验证
- 新增屏幕、触摸重跑与当前参数组合待实机验证

## 测试方法

### 构建

在 `CODE` 目录执行：

```bash
# Windows / Git Bash
./tools/build_example.sh audio_ns_demo

# macOS
bash ./tools/build_example_macos.sh audio_ns_demo
```

烧录：

```bash
# Windows / Git Bash
./tools/build_example.sh audio_ns_demo flash

# macOS，串口必须替换为客户机器实际识别到的端口
bash ./tools/build_example_macos.sh audio_ns_demo flash -p /dev/cu.usbserial-1410
```

### 测试步骤与预期

1. 烧录后屏幕应先显示 `PREPARING`，按钮显示 `Running...` 且不可点击。
2. 听到 1kHz 嘟声后进入 `RECORDING`，录音时间和进度在 10 秒内持续增加。
3. 录音结束后显示 `RAW RMS` 和 `NS RMS`，并依次进入 `PLAYING RAW` 与 `PLAYING NS`，播放进度持续更新。
4. 确认先听到原始录音，再听到 NSNet2 降噪录音；MIC1 输入 PGA 为 30 dB，输出音量为 90% 档位，记录破音和响度。
5. 结束时应显示 `COMPLETE`，按钮变为 `Run again`；点击后应重新执行完整流程。
6. 通过标准：连续两轮状态顺序、进度、RMS、触摸和音频回放均正常，无 crash、卡死或重复触发。

## 依赖组件

- `laiwfs300`、`audio_processor`、`display_hal`、`touch_hal`
- `lvgl/lvgl` 8.4.0
- `espressif/esp-sr`（NSNet2 模型）
