<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio Spatial Spectrum Demo

双麦空间音频频谱 Demo。同时使用 `MIC1` 和 `MIC2` 生成实时频谱，并在屏幕上叠加左右声源方向。

## 功能

- 读取 ES7210 4-slot TDM 数据，使用 `Slot0=MIC1` 和 `Slot2=MIC2`
- 16kHz、16bit、512 点 FFT，单帧约 32ms，频率分辨率 31.25Hz
- 去直流、Hamming 窗、dB 映射、噪声门、快速上升/慢速下降和峰值缓降
- 将 80Hz～8kHz 映射为 24 个对数频段
- 两路分别做 FFT，主频谱使用两路幅值平均，避免时域直接混音导致相位抵消
- 复用 `esp_doa` 和现有时间滤波，以 41mm 双麦声学中心距估算左右方向
- 提供 `RADAR`、`MIRROR`、`WATERFALL`、`METABALLS`、`LEVEL` 和 `DUAL` 6 种可视化模式
- 所有模式使用纯黑背景，保持频谱和能量球的颜色对比
- 仅通过整屏左滑、右滑循环切换模式，不显示按钮、页点、模式名或固定字母标签
- LCD SPI 使用 40MHz，LVGL 使用两块 320x80 行 DMA 缓冲，动态区目标刷新率为 25fps

## 屏幕页面

### RADAR

- 中心 24 段彩色环形频谱表示双麦合并幅值
- 外侧半圆轨道表示 `-90°..+90°` 左右方向，光标带有短拖尾
- 中心显示当前能量和相对角度，屏幕两侧用颜色区分双麦电平

### MIRROR

- 24 段合并频谱以屏幕中心轴向上下两侧镜像展开
- DOA 方向标记随左右声源方向横向移动

### WATERFALL

- 将相邻频段合并为 12 行热力图
- 使用固定容量循环缓存显示频谱随时间的变化

### METABALLS

- 将低频、中频和高频能量分别映射为三组动态能量球
- 三个球不绘制连线，各自沿独立且连续的轨迹运动，避免相对位置长期固定
- 能量平滑控制球体大小，DOA 经过平滑和限速后控制整体横向偏移

### LEVEL

- 显示双麦合成信号的快速、慢速 dBFS 电平和必要数字
- 未完成声学校准，不显示或宣称 dB SPL

### DUAL

- 上下区域分别用不同颜色显示两路麦克风的 24 段频谱与峰值
- 同时显示两路 RMS 数字，不使用固定麦克风字母标签

## 构建与烧录

```bash
# Windows / Git Bash（在 CODE 目录执行）
./tools/build_example.sh audio_spatial_spectrum_demo

# macOS（在 CODE 目录执行）
bash ./tools/build_example_macos.sh audio_spatial_spectrum_demo
```

直接烧录：

```bash
# Windows / Git Bash
./tools/build_example.sh audio_spatial_spectrum_demo flash

# macOS（示意端口，需按实际端口修改）
bash ./tools/build_example_macos.sh audio_spatial_spectrum_demo flash -p /dev/cu.usbserial-1410
```

`/dev/cu.usbserial-1410` 只是示意串口名，实际烧录时请替换为当前机器识别到的端口。

## 测试方法

1. 烧录并启动后，确认屏幕进入环形频谱画面，且两侧麦克风电平会随声音变化
2. 播放或哼唱不同音高，确认环形频谱的主要活动频段随音高变化，停止后峰值缓慢回落
3. 分别在设备左侧、正前和右侧发出明显声音，确认 DOA 光标与实际方向一致
4. 在屏幕任意区域连续左滑，依次确认 6 种画面并从最后一页循环回第一页
5. 连续右滑，确认切换顺序相反且同样支持首尾循环；屏幕不应出现按钮、页点、模式名或固定字母标签
6. 连续运行至少 5 分钟，确认无 panic、看门狗、DMA 缓冲分配失败或显示卡死
7. 观察快速变化音频时的跟手性和画面撕裂；当前硬件未连接 LCD TE，该项需实机评估

## 处理参数

| 参数 | 当前值 |
| --- | --- |
| 采样率 | 16kHz |
| 采样格式 | 16bit TDM |
| FFT 长度 | 512 点 |
| 频段数 | 24 |
| 频谱范围 | 80Hz～8kHz |
| 幅值平滑 | attack=0.65，release=0.12 |
| 峰值下降 | 每帧 0.025 |
| DOA 麦距 | 41mm |
| DOA 能量门限 | 56dB |
| DOA 显示映射 | `clamp(2 × (90 - FILT), -90, 90)` |
| UI 刷新周期 | 40ms |
| LCD SPI | 40MHz |
| LVGL DMA 缓冲 | 2 x 320 x 80 行 |

## 边界与限制

- 双麦线性阵列主要表达左右角度，不能可靠区分前后，也不是 360° 定位
- 41mm 麦距下，约 4.2kHz 以上的 DOA 存在空间相位混叠；频谱仍显示至 8kHz，但不宣称高频方向精度
- `56dB` 门限只决定 DOA 光标是否有效，频谱显示仍会响应较弱的声音
- 当前硬件没有将 LCD TE 引脚连接到 ESP32-S3，40MHz 与 80 行双缓冲可减少撕裂，但无法实现真正的 TE 同步
- 本 Demo 只参考网页项目的功能思路，处理与 UI 使用本项目的 ESP-IDF、`esp-dsp` 和 LVGL 实现，未复制其 Arduino 源码

## 验证状态

- 纯 C 数学测试通过：覆盖对数频段边界、双麦幅值融合、dB 映射、攻击/释放、峰值缓降和 DOA 限幅
- 2026-07-21：纯黑背景和连续三球版本 macOS clean build 1781/1781 通过，镜像大小 `0xa2000`（663552 字节），5MB app 分区剩余 87%
- 2026-07-21：在 `/dev/tty.usbserial-1130` 完成全片擦除和烧录，Bootloader、应用、分区表和 `srmodels` 均通过 Hash 校验
- 2026-07-21：实机确认 LCD 40MHz、CST836U、双麦 TDM、FFT 和 DOA 初始化正常，持续监控约 1 分钟无 panic、assert、看门狗或重启
- TF 固件包已同步更新，`app.bin` SHA-256 为 `bce07b892dd42c0f7c7d55c0071c396f463b7108004c29a9387d93b0013421b5`
- 2026-07-22：用户实机确认六模式滑动切换、纯黑背景、三球连续运动、无连线和动态相对位置效果符合要求
