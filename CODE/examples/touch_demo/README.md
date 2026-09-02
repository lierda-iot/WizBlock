<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Touch Demo

CST836U 电容触摸屏轮询演示。

## 功能

- CST836U 初始化 + 固件版本/芯片类型读取
- 50ms 轮询触摸事件
- 输出触摸坐标 (x, y)、压力和事件类型

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh touch_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0）+ LCD 板（含 CST836U 触摸 IC）
- 通过 CN3 连接器插入

### 测试步骤与预期

1. 烧录后系统初始化触摸 IC 并开始轮询
2. 触摸屏幕时，串口日志输出触摸坐标 (x, y)
3. 坐标范围在 0~239(x) × 0~319(y) 内
4. 在屏幕不同位置触摸，坐标响应正确
5. 判断通过：触摸坐标输出正确，响应灵敏

## 设计要点

- 触摸 IC 为 CST836U
- 单点触摸，坐标范围 0~239(x) × 0~319(y)
- 无触摸时寄存器 touch_count=0，无需处理
- count>2 的读数视为无效数据（I2C 噪声），直接丢弃

## 硬件连接

- I2C 地址：0x15
- I2C 总线：SCL=GPIO47, SDA=GPIO48
- TP_INT：IOEX P1_4（中断指示，当前为轮询模式）
- TP_RST：IOEX P1_3（复位控制）
- 需要插入液晶/触摸板（CN3）

## 依赖组件

- `laiwfs300`（BSP）、`touch_hal`、`bus_i2c`、`io_expander`
