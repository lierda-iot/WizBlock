<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio Dual MIC DOA Demo

双麦声源定位 + 屏幕方向显示 demo。持续读取 `MIC1` 和 `MIC2`，在日志和 LCD 上同步输出声源方向结果。

## 功能

- 读取 ES7210 4-slot TDM 原始数据
- 提取 `MIC1(Slot0)` 与 `MIC2(Slot2)` 两路麦克风
- 调用 `esp_doa` 进行双麦声源方向估计
- 通过 5 帧中值滤波、指数平滑和方向滞回抑制语音期间的角度跳动
- 屏幕 `REL` 和方向轨道暂按滤波相对角度的 2 倍显示，并限制在 `-90°..+90°`，用于实机评估量程效果
- 日志持续输出方向、原始角度、滤波角度和能量信息
- LCD 显示大方向字、方向轨道、角度/能量文本和 `MIC1/MIC2` RMS 条形条

## 屏幕显示说明

- 顶部显示当前粗粒度方向：`IDLE`、`LEFT`、`CENTER`、`RIGHT`
- 中部方向轨道根据 `RELATIVE_DEG` 左右移动，中心线对应正前方
- 底部同时显示滤波后的 `DOA`、`REL`、`ENERGY_DB` 数值，以及 `MIC1/MIC2` 实时 RMS 条形条
- 低能量时界面会短暂保持最近一次有效方向，再回到 `IDLE`
- 当前版本已按实物安装方向对左右显示语义做镜像修正，屏幕方向字、轨道位置和日志方向保持一致

## 测试方法

### 构建

```bash
# Windows / Git Bash（在 CODE 目录执行）
./tools/build_example.sh audio_dual_mic_doa_demo

# macOS（在 CODE 目录执行）
bash ./tools/build_example_macos.sh audio_dual_mic_doa_demo
```

### 直接烧录

```bash
# Windows / Git Bash
./tools/build_example.sh audio_dual_mic_doa_demo flash

# macOS（示意端口，需按客户实际端口修改）
bash ./tools/build_example_macos.sh audio_dual_mic_doa_demo flash -p /dev/cu.usbserial-1410
```

其中 `/dev/cu.usbserial-1410` 只是示意串口名，实际烧录时请替换为客户机器当前识别到的端口。

### 硬件连接

- 主控板 + MIC 板（B0 板，含双麦 `MIC1/MIC2`）
- LCD 屏幕正常接入主控板
- 串口用于日志观察（Windows 例如 `COM7`，macOS 例如 `/dev/cu.usbserial-1410`，均以实际端口为准）

### 测试步骤与预期

1. 烧录后等待初始化完成，LCD 进入 DOA 状态页，空闲时显示 `IDLE`
2. 在主控板左侧击掌，预期日志输出 `LEFT`，屏幕大方向字切到 `LEFT`，轨道指示点偏左
3. 在主控板右侧击掌，预期日志输出 `RIGHT`，屏幕大方向字切到 `RIGHT`，轨道指示点偏右
4. 在正前方击掌，预期方向为 `CENTER`
5. 声音足够明显时，`MIC1/MIC2` 条形条会随输入能量变化，`ENERGY_DB` 与滤波角度文本同步刷新
6. 保持声源位置不变并连续说话，预期屏幕 `DOA`/轨道指示不再随单帧异常角度大幅跳动；字间短暂出现的 0° 不改变当前方向，串口 `RAW`/`FILT`/`USED` 可用于对比效果
7. 停止说话或敲击后，界面短暂保持最近一次有效方向，然后回到 `IDLE`

## 设计要点

- TDM slot 映射：`Slot0=MIC1`，`Slot1=ES8311 回采`，`Slot2=MIC2`，`Slot3=未接`
- `esp_doa` 基于相位差（TDOA）估算声源方向角
- 双麦间距以麦克风声学中心的实测距离 `4.1cm` 为准，对应的最大无混叠频率约 `4.2kHz`，16kHz 采样率下语音频段有效
- 有效角度先经 5 帧中值滤波去除孤立跳点，再以 `alpha=0.30` 指数平滑；连续低能量超过 `400ms` 后重置历史，避免新声源受上一次方向拖累
- 0°（实现阈值为 `<=0.5°`）按低置信度样本处理：连续少于 3 帧时不加入角度滤波并保持上一有效方向，连续达到 3 帧后才按真实侧向声源接纳
- 能量门限为 `56dB`：当前实测环境底噪约 `45dB..55dB`，低于门限时不让底噪方向参与 DOA 显示
- 2 倍显示下，从 `CENTER` 进入 `LEFT/RIGHT` 的边界约为 `REL=±30°`，已处于侧向时回到约 `REL=±20°` 才切回 `CENTER`
- UI 采用轻量 LVGL 状态页，不依赖触摸输入
- UI 刷新周期 `100ms`，低能量场景保留最近一次有效方向 `400ms`，避免方向指示闪烁

## TDM Slot 映射

| Slot | 来源 |
| --- | --- |
| Slot0 | MIC1（ES7210 ADC1） |
| Slot1 | Reference（ES8311 DAC 回采） |
| Slot2 | MIC2（ES7210 ADC2） |
| Slot3 | 未接 |

## 日志格式

- `DOA=<LEFT|CENTER|RIGHT> RAW=<0..180> FILT=<0..180> REL=<-90..90> USED=<0|1> E=<frame energy> MIC1_RMS=<value> MIC2_RMS=<value>`
- `RAW` 是 `esp_doa` 单帧输出，`FILT` 是时间滤波后供屏幕与方向分类使用的角度
- `REL` 是当前试看映射 `clamp(2 × (90 - FILT), -90, 90)`，仅用于屏幕数值、轨道与日志展示，不改写 `RAW/FILT`
- `USED=0` 表示当前低置信度 0° 样本被忽略，`USED=1` 表示当前原始角度已进入滤波计算

## 参数

- 双麦间距：`4.1cm`（`0.041m`，麦克风声学中心距）
- 采样率：`16kHz`
- 角度分辨率：`10deg`
- 角度中值窗口：`5` 帧
- 指数平滑系数：`0.30`
- 0° 样本确认门限：`3` 帧
- 显示角度倍率：`2.0`
- 方向滞回：从 CENTER 进入侧向约 `±30deg`，侧向退出约 `±20deg`
- 有效声源能量门限：`56dB`
- UI 刷新周期：`100ms`
- 低能量保持时间：`400ms`

## 硬件要求

- I2S：`MCLK=GPIO42`，`BCLK=GPIO41`，`WS=GPIO39`，`DOUT=GPIO38`，`DIN=GPIO40`
- ES7210 TDM 4 通道（I2C 地址 `0x40`）
- `MIC1 + MIC2`（B0 板两颗 MSM381ACB026 MEMS 麦克风）
- LCD：ST7789V3 320x240
- `AMP_CTRL`：IOEX `P1_0`

## 验证状态

- 2026-07-06：DOA 日志方向实机验证通过（左侧击掌 `LEFT RAW≈30°`，右侧击掌 `RIGHT RAW≈170°~180°`，`MIC1/MIC2` 幅度关系正确）
- 2026-07-17：新增 LCD 方向 UI 后，macOS 构建通过；LCD 已点亮，左右显示修正代码已加入，待重新实机确认方向一致性
- 2026-07-21：增加 5 帧中值、`alpha=0.30` 指数平滑、短暂 0° 样本忽略和方向滞回；麦距修正为 41 mm，`REL` 暂用 2 倍映射，能量门限提高至 56 dB，CENTER 范围同步收窄。主机逻辑测试、macOS clean build 1779/1779、全片擦除和 `/dev/tty.usbserial-1130` 烧录均通过，固件为 `0x93bb0`；实机确认当前 `45dB..55dB` 底噪会进入 `DOA_IDLE`。两侧量程、CENTER 观感和软声识别仍待人工复测
