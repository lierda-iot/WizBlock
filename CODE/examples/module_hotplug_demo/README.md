<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Module Hotplug Demo（模块热插拔检测演示）

运行时热插拔检测演示，支持 4 种板卡的自动识别、事件回调和功能联动。

## 功能

- 可扩展的 `hotplug_manager` 框架（注册式 slot 机制）
- 上电后先等待 10 秒，留出串口日志抓取窗口，再初始化并启动热插拔任务
- C0 扩展板检测（ADC raw 中值 + ESP-IDF curve fitting calibration + 饱和高档）
- D0 电机板检测（1.45V 标定窗口）+ 插入后持续 100% 前进、拔出后停止并反初始化
- LCD 板检测（I2C probe CST836U 0x15）+ 红白交替刷屏 / 摄像头联动
- E0 摄像头板检测（SCCB probe SP0A39 0x21）+ DVP 采集
- 双重消抖：ADC 中值滤波 + 轮询状态连续 3 次确认（1.5s 窗口）
- 板卡间依赖管理（D0/E0 依赖 C0 在位，C0 拔出级联通知）

## 测试方法

### 构建与烧录

Windows按`design.md` 4.1.2的成熟流程执行：修改源码后先全量镜像，再clean构建；实板回归前必须先全片擦除。

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc 'rm -rf "$TEMP/laiwfs300_build/CODE" && cp -r "e:/10__AIProject/7_AI陪伴机器人/CODE" "$TEMP/laiwfs300_build/CODE"'
powershell.exe -ExecutionPolicy Bypass -File "e:/10__AIProject/7_AI陪伴机器人/CODE/tools/build_example.ps1" -Example module_hotplug_demo -Clean
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue
& "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" -m esptool --chip esp32s3 -p COM7 erase_flash
powershell.exe -ExecutionPolicy Bypass -File "e:/10__AIProject/7_AI陪伴机器人/CODE/tools/build_example.ps1" -Example module_hotplug_demo -Port COM7
```

### 硬件连接

- ESP32-S3 核心板（A0）+ C0 扩展板
- 可选：LCD 板 和/或 E0 摄像头板、D0 电机板
- ADC 检测：GPIO1

### 测试步骤与预期

1. 烧录后确认日志先显示 10 秒启动延迟，延迟结束后系统进入热插拔检测循环
2. 插入各板卡时，日志正确报告 `INSERTED` 事件
3. 拔出各板卡时，日志正确报告 `REMOVED` 事件
4. LCD 在位时屏幕红白交替刷屏；cam+lcd 同时在位时显示摄像头预览
5. D0 插入确认后电机持续 100% 前进；D0 拔出确认后电机停止且 LEDC/GPIO 资源释放，重插后可再次运行
6. 运行中持续输出 ADC raw/校准值/饱和状态/分类日志和心跳日志
7. 拔出后各外设正确释放（日志无错误）
8. 判断通过：插拔事件正确检测，联动功能正常，拔出后无 crash 或资源泄漏

### 实机验证结果（2026-07-24）

- clean build `1420/1420`通过，固件大小`0x4f820`；COM7全片擦除和烧录通过，bootloader、应用及分区表均完成哈希校验。
- 测试顺序为核心板+C0启动、D0插入、D0拔出、再次插入、再次拔出；用户最终确认Demo无问题。
- 核心板+C0稳定为`raw=4095`、`cal_mv=3160`、`saturated=1`、`class=C0_ONLY`；D0在位和电机100%前进期间稳定为`raw=1685~1690`、`cal_mv=1435~1437`、`class=C0_D0`。
- 两轮插入均完成init和100% forward，两轮拔出均完成stop和deinit，第二轮可重新初始化；heartbeat持续运行，无panic、assert、watchdog、重复事件或资源释放错误。
- 首次拔出时一次采样窗口跨越`1687~4095`并记录`ADC unstable`，中值滤波和3轮状态确认未产生误判，属于正常过渡诊断日志。

## 检测方案

| 板卡 | 检测手段 | 信号来源 |
| --- | --- | --- |
| C0 扩展板 | ADC GPIO1 状态分类 | C0 的 20kΩ 上拉使核心板+C0稳定为3.3V；该点按 `raw>=4000` 高端饱和档识别，不作为精确电压测量 |
| D0 电机板 | ADC GPIO1 分压 | D0 移除 R12、保留 R13=15kΩ 下拉后，核心板+C0+D0实测1.45V；初始校准窗口为1200~1700mV |
| LCD/TP 板 | I2C probe 0x15 | CST836U 触摸 IC ACK |
| E0 摄像头板 | I2C/SCCB probe 0x21 | SP0A39 传感器 ACK（需 C0 在位） |

### ADC 当前状态（2026-07-24）

- 原电压波动已确认由检测脚无固定电平、处于浮空状态导致；硬件上拉改动后已稳定，不再继续调查旧“ADC 干扰”根因。
- 旧源码曾使用 `raw * 2500 / 4095` 把 12bit 原始码线性标成 mV。该调试公式已移除；当前先对5次raw取中位值，再使用ESP-IDF curve fitting calibration输出ADC引脚校准电压。
- ESP32-S3 在当前 `ADC_ATTEN_DB_12`（约 11dB）配置下，理论 full-scale 为 3.9V，但输入上限受 3.3V `VDD_A` 限制，官方建议的精确测量范围仅约 150~2450mV。3.3V 可作为 ADC 引脚的上边界输入，但本板已读到 raw=4095，不能作为精确 3.3V 测量值；直接把公式上限从 2500 改成 3300 只会改显示，不会恢复饱和后丢失的信息。
- 当前分类阈值：校准电压 `<=800mV` 为低档，`1200~1700mV` 为 C0+D0，`raw>=4000` 为 C0 高端饱和档；校准不可用时使用受限 raw 后备窗口。无法归类时返回 UNKNOWN，不触发状态改变。
- 100%前进期间的1.45V档稳定性及两轮重复插拔已实机验证；单核心板低档样本只作为后续扩展数据，不阻塞当前正式场景。
- Round 7 的“LEDC运行10秒后停止并释放GPIO、再锁存ready”仅保留在历史分析记录中，正式源码已经删除该流程。

## 联动逻辑

- LCD + Camera 同时在位：摄像头画面实时显示到 LCD
- LCD 在位但无 Camera：红白交替刷屏（1s 周期）
- Camera 在位但无 LCD：日志输出帧计数
- D0 插入：初始化 Demo 私有电机 runtime，100% 持续前进直到拔出
- D0 拔出：先停止电机，再停止 LEDC、暂停定时器、复位 GPIO5/4/37/45并清除初始化状态

## 扩展方式

新增板卡只需实现一个 `hotplug_slot_t`（`name` + `detect_fn`），调用 `hotplug_manager_register_slot()` 注册；应用行为统一由事件回调处理。

## 硬件要求

- ESP32-S3 核心板（A0）
- 可选：C0 扩展板、D0 电机板、LCD/TP 板、E0 摄像头板
- ADC：GPIO1（D0_DETECT，11dB 衰减）
- I2C：SCL=GPIO47, SDA=GPIO48

## 安全设计

- 电机板拔出时先停止 PWM 再释放资源
- C0 拔出时级联将 D0/E0 标记为 ABSENT
- 初始化失败不阻塞系统
- ADC 5 次 raw 中值滤波 + 状态消抖 3 轮（500ms/轮）；UNKNOWN 不累计插拔消抖

## 依赖组件

- `laiwfs300`（BSP）、`robot_motion`、`pt2466_motor`、`camera_hal`、`display_hal`、`bus_i2c`
