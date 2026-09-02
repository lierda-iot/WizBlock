<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# IMU 6-Axis Demo

基于 BMI260 的独立 6 轴 demo。持续采集加速度计和陀螺仪数据，并通过 LCD 姿态魔方和六轴数据页进行可视化展示。

## 功能

- BMI260 初始化（内含 8192 字节配置微码上传 + internal_status 校验）
- 持续读取 3 轴加速度 + 3 轴陀螺仪
- LCD 横屏显示姿态魔方、重力方向、三轴旋转状态和六轴数据
- 触摸切换姿态页/数据页，并支持陀螺仪零偏重新校准
- 每 `200ms` 刷新一次日志；显示采样和 UI 刷新独立运行

## 测试方法

### 构建与烧录

macOS：

```bash
cd CODE
bash ./tools/build_example_macos.sh imu_6axis_demo flash -p /dev/cu.usbserial-1410
```

Windows/Git Bash：

```bash
bash ./tools/build_example.sh imu_6axis_demo flash
```

其中 `/dev/cu.usbserial-1410` 只是 macOS 示意串口名，实际烧录时按客户机器当前识别到的端口替换。

### 硬件连接

- ESP32-S3 核心板（A0），BMI260 已集成在主控板上
- I2C 总线：SCL=GPIO47, SDA=GPIO48

### 测试步骤与预期

1. 烧录后保持设备静止约 1 秒，等待陀螺仪零偏校准完成
2. 姿态页显示姿态魔方、重力球和旋转光环；倾斜或旋转设备时画面应平滑变化
3. 点击 `DATA`/`VIEW` 切换六轴数据页，点击 `CAL` 可重新校准
4. 静置水平放置时，z 轴原始值约 8000~8400（对应 1g），x/y 轴接近 0
5. 判断通过：日志、姿态页和数据页持续更新，静置和运动时数值变化符合物理预期

## 设计要点

- BMI260 上电后处于 suspend 模式，必须先上传官方 config 微码才能输出数据
- 微码通过 burst write 方式写入（一次 I2C 传输 8192 字节）
- 校验方式：写入后读 `internal_status` 寄存器，bit0=1 表示初始化成功
- 当前代码配置：`ACC_RANGE=0x01`、`GYR_RANGE=0x00`
- 当前显示换算口径：加速度约 `8192 LSB/g`，陀螺仪约 `16.4 LSB/(°/s)`；如驱动寄存器配置变化，需同步调整

## 硬件连接

- I2C 地址：0x68（SDO=GND）
- I2C 总线：SCL=GPIO47, SDA=GPIO48
- IMU_INT1：IOEX P1_2（中断输出，当前未使用）

## 屏幕交互

- 姿态页：姿态魔方随横滚/俯仰变化，重力球表示当前倾斜方向，旋转光环表示陀螺仪三轴动态；屏幕坐标映射为 `ball_x=-AY`、`ball_y=-AX`，用于保持用户视角下的左右/上下直觉
- 数据页：显示 `ACC X/Y/Z` 和 `GYRO X/Y/Z` 数值及彩色柱状条
- `VIEW/DATA`：切换两个页面
- `CAL`：重新采集陀螺仪零偏，校准期间保持设备静止
- 本 demo 不包含挑战模式；没有磁力计，偏航只表示相对旋转，不作为绝对指南针方向

## 日志格式

```text
accel[x=.... y=.... z=....] gyro[x=.... y=.... z=....]
```

## 实机验证结果（2026-07-06）

- 静置水平：z ≈ 8300（≈1g），x/y ≈ 0
- 多姿态测试：加速度方向正确对应 1g 重力方向
- 运动时陀螺仪响应正常
- 姿态魔方和触摸页面扩展已完成 macOS 构建，待实机验证画面方向、触摸坐标和校准效果

## 构建

```bash
cd CODE
bash ./tools/build_example_macos.sh imu_6axis_demo
```
