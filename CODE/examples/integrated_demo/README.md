<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Integrated Demo（全外设集成测试）

集成全部外设 bring-up 测试的示例工程，对应主框架 `bringup_test` 组件的功能。

## 功能

通过 Kconfig 开关控制各外设测试的启停：

| 测试项 | Kconfig 开关 | 默认 |
|--------|-------------|------|
| 电机 100% 前进 | `CONFIG_LAIWFS300_ENABLE_MOTOR_SMOKE_TEST` | y |
| 显示白/红刷屏 | `CONFIG_LAIWFS300_ENABLE_DISPLAY_COLOR_TEST` | y |
| 触摸坐标轮询 | `CONFIG_LAIWFS300_ENABLE_TOUCH_TEST` | y |
| RTC 设置/读回 | `CONFIG_LAIWFS300_ENABLE_RTC_TEST` | y |
| BMI260 IMU 连续读 | `CONFIG_LAIWFS300_ENABLE_IMU_TEST` | y |
| IOEX 输入脚读取 | `CONFIG_LAIWFS300_ENABLE_IOEX_INPUT_TEST` | y |
| 音频录放（10s录+10s放） | `CONFIG_LAIWFS300_ENABLE_AUDIO_LOOPBACK_TEST` | y |
| LTE/4G 网络 | `CONFIG_LAIWFS300_ENABLE_LTE_NET_TEST` | n |
| 摄像头预览 | `CONFIG_LAIWFS300_ENABLE_CAMERA_PREVIEW_TEST` | n |

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh integrated_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0）+ C0 扩展板 + D0 电机板 + LCD/触摸板 + MIC 板 + 扬声器
- 各外设板按照对应连接器插入

### 测试步骤与预期

1. 烧录后系统按顺序初始化并执行各外设验证
2. 串口日志输出各项测试状态（电机运转、LCD 刷屏、触摸坐标、RTC 时间、IMU 数据、音频录放等）
3. 各外设可通过 Kconfig 开关独立启停
4. 判断通过：日志中各已启用模块正常输出状态信息，无 crash 或阻塞

## 设计要点

- 所有外设测试由 BSP `board_laiwfs300_init()` 统一初始化后，各测试模块独立运行
- Kconfig 互斥：`AUDIO_NS_TEST` 与 `AUDIO_LOOPBACK_TEST` 二选一
- 各外设测试通过/失败不影响其他模块（独立任务）
- 适用于全板 bring-up 快速验证，不需要逐个烧录单独 demo

## 硬件

需要连接：主控板 A0 + 液晶板 + 扩展板 C0 + 电机板 D0 + MIC 板 + 扬声器。

## 构建

```bash
cd CODE
./tools/build_example.sh integrated_demo
```

## 串口

UART0 / 115200 / 8N1 / COM7
