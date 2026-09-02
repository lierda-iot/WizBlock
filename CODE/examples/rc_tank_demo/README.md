<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# RC Tank Demo (EX-035)

双机遥控坦克示例。两套 ESP32-S3 设备共用同一工程，通过 Kconfig 分别构建 Tank 与 Remote 角色；Tank 创建 SoftAP，Remote 自动连接，并通过 UDP/TCP 通道承载控制、视频和语音。

## 当前能力与状态

- Tank：接收连续角度/力度控制并经平滑履带混控、PWM映射、启动助推和快速斜坡驱动双路电机，采集 SP0A39 画面，显示 WiFi、电量和坦克状态。
- Remote：显示 Tank 的 MJPEG 视频，叠加触摸摇杆及状态，并向 Tank 发送V1连续控制指令。
- 控制安全：Remote对保持不变的DRIVE以不超过100ms的间隔保活；进入STOP发送一次，失败保持同序号重试，成功后静默，重连空闲重新发送一次。Tank 300ms未收到有效新控制包时停车。
- 当前正式视频链路：640×480 VYUY 采集、Q60 JPEG、1200B UDP 应用层分片、Remote 完整帧重组与 240×180 显示。
- 2026-08-26 最终人工确认：Remote 画面正常，亮度与方向符合要求，移动操作正常。底部约 1/3～1/4 高度偶发 3～4 像素横向叠影线作为已知问题保留，本阶段不继续处理。
- 2026-08-27 连续控制版最终闭环：DVP健康门连续拒绝4次`73/30`概率性坏启动，第5次以`81/0`接受，用户确认画面正常；保持Tank运行并单独重启Remote后自动重连且画面仍正常。WiFi暂断是Tank主动自愈重启的结果，不是独立网络故障。
- 2026-08-27 遥控手感收口：默认`offset=0/start=60%/boost=60ms`下，前进、后退、掉头启动及最终转向曲线均人工通过。最终曲线测试契约已同步，但按用户持续授权未运行纯C。

当前正式需求、设计、阶段状态和剩余事项统一见根目录[需求基线](../../../requirements.md)、[设计基线](../../../design.md)和[项目记忆](../../../project_memory.md)；迁移前全文只在[中央源快照](../../../docs/archive/examples/rc_tank_demo/source-snapshot/)中保留用于追溯，不再作为活跃入口。

## 通信通道

| 通道 | 协议/端口 | 方向 | 用途 |
| --- | --- | --- | --- |
| 控制 | UDP/8001 | Remote → Tank | 固定14字节V1角度/力度控制包与心跳 |
| 视频 | UDP/8002 | Tank → Remote | 带 8 字节帧头的 MJPEG 应用层分片 |
| 语音 | TCP/8003 | Remote → Tank | 16kHz 单声道 Opus 语音段 |

控制帧格式与编解码集中在 `main/rc_ctrl_protocol.h/.c`，通用端口等常量位于 `main/rc_tank_common.h`；角色配置分别位于 `sdkconfig.defaults.tank` 和 `sdkconfig.defaults.remote`。

## 推荐构建与烧录

当前 Windows 成熟入口会把工程镜像到 ASCII 临时路径，完成角色 clean build，并把三段固件复制到 `firmware_backup/`：

```powershell
.\build_rc_tank.ps1 -Role tank -Clean
.\build_rc_tank.ps1 -Role remote -Clean
```

烧录脚本强制先全片擦除，再写入 bootloader、partition table 和 application：

```powershell
.\flash_rc_tank.ps1 -Port COM7 -Role TANK
.\flash_rc_tank.ps1 -Port COM24 -Role REMOTE
```

设备端口的当前约定为 Tank=`COM7`、Remote=`COM24`。构建、烧录和特殊 RAM bootloader 启动边界见根[设计基线](../../../design.md)中的EX-035章节。

## 脚本用途

### 当前推荐入口

| 脚本 | 用途 |
| --- | --- |
| `build_rc_tank.ps1` | 推荐的双角色构建入口；按 `-Role tank/remote` 选择配置，`-Clean` 清理镜像构建目录，完成 ASCII 路径镜像构建并发布三段固件。 |
| `flash_rc_tank.ps1` | 推荐烧录入口；按 `-Port`、`-Role` 选择设备与固件，先执行 `erase_flash`，再写入并校验三段固件。 |
| `capture_serial.py` | 通用单串口二进制安全日志抓取；支持端口、输出文件、时长及可选复位参数。 |
| `tests/run_host_tests.ps1` | LLVM/Clang主机纯C全量回归入口；默认运行全部21项，也可用`-TestName`聚焦单项。 |

### 辅助与历史兼容入口

| 脚本 | 用途与边界 |
| --- | --- |
| `monitor_log.py` | 固定监控 `COM7` 的简易串口查看器，默认 60 秒，只适合人工临时查看；多端口/归档优先使用 `capture_serial.py`。 |
| `build_tank.ps1` | 早期在源码路径直接 clean build Tank 并备份固件的脚本；工具版本与路径固定，已由 `build_rc_tank.ps1` 取代。 |
| `rebuild_remote.ps1` | 早期在当前目录构建 Remote 并备份固件的脚本；未提供当前统一的 ASCII 镜像与 `-Clean` 角色入口。 |
| `mirror_and_build_remote.ps1` | 早期仅为 Remote 镜像完整 `CODE` 后构建的脚本；功能已由 `build_rc_tank.ps1 -Role remote -Clean` 统一覆盖。 |
| `build_tank.bat` / `build_remote.bat` | 早期直接路径全清理构建包装器；保留作历史兼容，不作为当前成熟入口。 |
| `build_tank_direct.bat` | 名称虽为 direct，实际仍删除旧 build/sdkconfig 后重建 Tank；保留作历史兼容。 |
| `rebuild_remote_clean.bat` | 早期 Remote 构建与制件备份包装器；不使用当前统一镜像构建流程。 |
| `flash_tank_COM7.bat` / `flash_remote_COM7.bat` | 早期分三次写入的 COM7 包装器，默认未执行全片擦除，且 Remote 端口固定为 COM7；不符合当前每轮全片擦除约束，不应作为正式烧录入口。 |

## 验证边界

- 本次正式清理版的 Tank/Remote clean build、COM7/COM24 全片擦除、三段写入及 Hash 校验均已完成。
- 用户已完成正式清理版的人工画面与旧五态移动操作确认。
- 2026-08-26连续控制实现完成后，使用LLVM-MinGW Clang 22.1.8、目标`x86_64-w64-windows-gnu`运行主机纯C全量回归，21项全部`exit=0`。
- 新的14字节V1连续控制版本已构建并烧录，空闲STOP静默有日志证据，DVP健康门最终接受态及Remote重启重连后的画面均已人工通过；前进、后退、掉头启动和最终转向手感也已人工通过。最终曲线断言已同步，但当前空闲策略、DVP诊断及转向调优按用户授权跳过纯C，不能用2026-08-26的21项结果替代本轮纯C验证。
- 仍待验证的网络长断连/重扫、双端 WiFi 图标一致性、失联停车、音频和30分钟长稳统一记录在根[项目记忆](../../../project_memory.md)的EX-035入口，不混入当前已完成状态。
