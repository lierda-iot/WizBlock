<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# RTC Demo

AIP8563 实时时钟演示，设置时间并持续读取。

## 功能

- AIP8563 初始化
- VL 标志检测（电源丢失判断）
- 如电源丢失则设置默认时间
- 每秒读取并输出当前时间

## 测试方法

### 构建与烧录

```bash
bash CODE/tools/build_example.sh rtc_demo flash
```

### 硬件连接

- ESP32-S3 核心板（A0），AIP8563 RTC 已集成在主控板上
- I2C 总线：SCL=GPIO47, SDA=GPIO48

### 测试步骤与预期

1. 烧录后日志显示设置 RTC 时间为 12:00:00
2. 随后每秒读回并打印当前 RTC 时间
3. 时间正确递增（12:00:01, 12:00:02, ...）
4. 判断通过：时间持续递增且无跳变或停滞

## 设计要点

- AIP8563 为低功耗 RTC，I2C 接口（写地址 0xA2/读 0xA3，7bit=0x51）
- VL（Voltage Low）位用于判断 RTC 是否经历过掉电，掉电后时间不可信需要重设
- 寄存器 BCD 编码，读写时需 BCD↔BIN 转换
- RTC_INT 可用于闹钟/倒计时中断（当前未使用）

## 硬件连接

- I2C 地址：0x51
- I2C 总线：SCL=GPIO47, SDA=GPIO48
- RTC_INT：IOEX P1_7（低电平有效中断输出，当前未使用）
- 需外部电池维持掉电计时

## 依赖组件

- `laiwfs300`（BSP）、`aip8563_rtc`、`bus_i2c`
