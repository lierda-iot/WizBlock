<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio Opus Demo（Opus 编解码演示）

录音 10 秒 + NS 降噪 + Opus 编码存储到 PSRAM，再解码播放；320x240 横屏同步展示处理状态，并支持触摸重新开始完整流程。

## 功能

- 初始化 ESP AFE 框架（NS 模式）+ Opus 编解码器
- 嘀声(1kHz) → 录音 10s + 实时 NS 降噪
- 将降噪后 PCM 逐帧 Opus 编码，保存编码包到 PSRAM
- 日志输出压缩比（预计 PCM 320KB → Opus ~20-30KB）
- 嘀声(2kHz) → 逐帧 Opus 解码并播放
- 横屏展示 `PREPARING`、`RECORDING`、`ENCODING`、`PLAYING`、`COMPLETE` 和 `ERROR` 状态
- 展示当前阶段进度、Opus 帧数和压缩率
- 上电自动执行首轮；完成后点击 `Record again` 重新开始，失败后点击 `Retry` 重试
- 录音、编码和播放期间按钮禁用，避免重复触发或中途打断资源清理

## 设计要点

- 使用 `opus_codec` 组件封装 `esp_opus_enc` / `esp_opus_dec`
- Opus 配置：16kHz mono，60ms frame（960 samples），VBR 开启、DTX 关闭，complexity=5，bitrate=32000
- 音频工作任务常驻等待启动通知，每轮独立初始化和释放 AFE、Opus 与 PSRAM 缓冲
- 音频状态写入受锁快照，独立 UI 刷新任务每 100ms 更新 LVGL，避免绘制阻塞录音
- 音频任务 priority 3、stack 32768，Opus CELT 编码需要较大栈空间
- 编码循环每 10 帧 yield 一次防 watchdog
- 压缩率日志使用整数除法（避免 dtoa float 格式化 crash）
- AFE 配置与 ns_demo 一致：format="M"（1MIC），ns=NSNet2

## 参数

- 采样率：16kHz，单声道，16bit
- Opus 帧时长：60ms（960 samples/帧）
- VBR 启用，DTX 禁用
- 最大 256 帧（约 15.36s 容量）

## 已知问题

- 录音后半段可能出现 "FEED is full" 警告（AFE ringbuffer 溢出，部分数据丢失）
- 播放音量比 ns_demo 偏大，92% 压缩率下有损编码属预期行为

## 硬件连接

- I2S：MCLK=GPIO42, BCLK=GPIO41, WS=GPIO39, DOUT=GPIO38, DIN=GPIO40
- ES8311 I2C 地址：0x18（7-bit）/ 0x30（8-bit）
- ES7210 I2C 地址：0x40（7-bit）/ 0x80（8-bit）
- AMP_CTRL：IOEX P1_0
- LCD：ST7789V3，320x240 横屏
- 触摸：CST836U

## 验证状态

- 2026-06-29：原音频链路构建和实机验证通过；crash 已通过任务栈从 8KB 增至 32KB 修复，Opus 编解码正常
- 2026-07-17：状态逻辑主机测试通过，覆盖重启许可、进度钳位和压缩率边界
- 2026-07-17：macOS clean build 通过，固件大小 `0x1205f0`，5MB app 分区剩余 77%
- 2026-07-17：新增屏幕状态、触摸按钮和连续两轮录音播放经用户实机验证通过

## 测试方法

### 构建

在 `CODE` 目录执行：

```bash
# Windows / Git Bash
./tools/build_example.sh audio_opus_demo

# macOS
bash ./tools/build_example_macos.sh audio_opus_demo
```

烧录：

```bash
# Windows / Git Bash
./tools/build_example.sh audio_opus_demo flash

# macOS，串口必须替换为客户机器实际识别到的端口
bash ./tools/build_example_macos.sh audio_opus_demo flash -p /dev/cu.usbserial-1410
```

### 硬件连接

- 主控板 + 屏幕 + 触摸屏 + 喇叭（HT6872 功放已板载）
- 串口 COM7 用于日志观察

### 测试步骤与预期

1. 烧录后屏幕应先显示 `PREPARING`，按钮显示 `Running...` 且不可点击。
2. 听到 1kHz 嘀声后进入 `RECORDING`，录音时间和进度在 10 秒内持续增加。
3. 随后依次显示 `ENCODING` 和 `PLAYING`，帧进度持续变化；编码结束后显示帧数和压缩率。
4. 听到 2kHz 嘀声后应从喇叭听到解码回放，结束时显示 `COMPLETE`，按钮变为 `Record again`。
5. 点击 `Record again`，应重新执行完整录音、编码和播放流程；连续两轮不得 crash、卡死或出现按钮重复触发。
6. 通过标准：状态顺序、进度、音频回放和重新开始均正常，日志显示合理的编码帧数与压缩率。

## 依赖组件

- `laiwfs300`（BSP）、`audio_processor`、`opus_codec`
- `display_hal`、`touch_hal`、`lvgl/lvgl` 8.4.0
- `espressif/esp-sr`（NSNet 模型）
