<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Motor Demo

PT2466 双履带电机控制演示，循环执行前进、后退、原地左转和原地右转。

## 功能

- LEDC PWM 初始化（20kHz，10-bit 分辨率）
- 双路 PT2466 电机驱动
- 100% 速度循环执行前进、后退、原地左转和原地右转
- 每个动作持续 3 秒，动作之间停止 1 秒

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh motor_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0）+ C0 扩展板 + D0 电机板
- 连接电机到 D0 板输出端子

### 测试步骤与预期

1. 烧录后电机以 100% 速度依次前进、后退、原地左转和原地右转
2. 每个动作持续 3 秒，动作之间停止 1 秒，然后循环执行
3. 串口日志依次显示 `forward`、`backward`、`turn left` 和 `turn right`
4. 判断通过：四个方向均与日志一致，动作间隔期间两个电机均停止

## 设计要点

- PT2466 为 H 桥电机驱动，每路需 IN1/IN2 两个 PWM 信号控制方向和速度
- 正转：IN1=PWM, IN2=LOW；反转：IN1=LOW, IN2=PWM；制动：IN1=LOW, IN2=LOW
- 原地左转：左履带反转、右履带正转；原地右转：左履带正转、右履带反转
- 方向已校准：`invert_direction=true`（硬件接线导致默认逻辑方向相反）
- PWM 频率 20kHz（超出人耳听觉范围，避免电机啸叫）

## 硬件连接

- IN1=GPIO5 (LEDC0), IN2=GPIO4 (LEDC1) — 左履带（U3 PT2466）
- IN3=GPIO37 (LEDC2), IN4=GPIO45 (LEDC3) — 右履带（U4 PT2466）
- 信号链路：ESP32-S3 → A0 CN8 → C0 扩展板 → C0 控制接口 → D0 U5 → PT2466

## 注意事项

- 需接电机控制板（D0）和扩展板（C0）
- GPIO0 与 GPIO4 已完成改板互换并通过实机验证：GPIO4 用于电机 IN2，GPIO0 用于摄像头 DVP D1
- GPIO45 为 strapping pin（VDD_SPI 电压选择），使用时需注意上电默认态

## 依赖组件

- `laiwfs300`（BSP）、`pt2466_motor`、`robot_motion`
