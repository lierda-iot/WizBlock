<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# LTE + WiFi 双网管理 Demo

WiFi + 4G CAT1 双网络管理演示，通过 `network_manager` 使用闭源
`lsd_net_mgmt` 能力。

## 功能

- LTE 模组上电（IOEX P1_1 → ETA6027S2F → VLTE）
- WiFi STA 连接 + 4G USB ECM 双通道
- 网络自动选择（WiFi 优先，4G 作为补充）
- WiFi 连接稳定 10 秒后由组件桥接上报给 `lsd_net_mgmt`
- 蜂窝状态仅观测：组件直接消费闭源库既有 4G connected/disconnected 事件，并保留 ESP ETH/IP 事件作为被动补充；Demo 只显示 `network_manager` 已提交的状态和快照
- `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=5` 只作为直接 4G 事件之外的 lost-IP 被动保底
- Demo 不主动查询蜂窝底层，不调用显式 4G reconnect，不执行 SIM 插拔自动恢复
- LVGL 第一页输入 WiFi SSID/密码，点击输入框进入独立键盘编辑页，密码使用掩码显示；当前实现先启动组件并观测 4G，点击 `Confirm` 后再提交 WiFi 配置
- WiFi 配置持久化到 NVS，下次启动时自动预填上次配置；没有保存配置时预填默认值
- LCD 横屏 320x240 第二页显示启动、WiFi/4G 连接、WiFi 检查、网卡切换、已连接、无网络重试和初始化错误状态
- 连续 60 秒无 stable ready 时显示 `No network`，WiFi 可继续使用组件既有有限重试，并提供 `WiFi Settings` 返回配置页；4G 不执行自动恢复
- 每 5 秒输出当前网络接口和就绪状态

## 测试方法

### 构建与烧录

Windows 成熟烧录流程以根目录 `design.md` 4.1.2 为准：先 Git Bash 全量镜像 `CODE`，再用 `build_example.ps1` 构建；烧录前先全片擦除。macOS 构建流程以 `design.md` 4.1.3 为准。

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc 'rm -rf "$TEMP/laiwfs300_build/CODE" && cp -r "e:/10__AIProject/7_AI陪伴机器人/CODE" "$TEMP/laiwfs300_build/CODE"'
& "E:\10__AIProject\7_AI陪伴机器人\CODE\tools\build_example.ps1" -Example lte_net_demo -Clean
& "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" -m esptool --chip esp32s3 -p COM7 erase_flash
& "E:\10__AIProject\7_AI陪伴机器人\CODE\tools\build_example.ps1" -Example lte_net_demo -Port COM7
```

macOS 只构建：

```bash
bash ./tools/build_example_macos.sh lte_net_demo
```

macOS 全片擦除后烧录（串口名按实际设备修改）：

```bash
bash ./tools/build_example_macos.sh lte_net_demo flash -p /dev/tty.usbserial-110
```

### 显示 flush 主机测试

```bash
cd CODE/examples/lte_net_demo
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain \
  tests/lte_net_display_flush_test.c \
  -o /tmp/lte_net_display_flush_test
/tmp/lte_net_display_flush_test
```

### 硬件连接

- ESP32-S3 核心板（A0）+ C0 扩展板
- 插入 nano-SIM 卡
- 连接 4G 天线
- NT26-KCN B 模组通过 USB (GPIO19/GPIO20) 连接

### 测试步骤与预期

1. 烧录后 LCD 显示 `WiFi Settings`，确认 SSID 和密码已经预填或手动输入
2. 点击 SSID 或密码输入框，进入键盘编辑页；使用 `Done` 完成输入，使用 `Back` 放弃本次编辑
3. 点击 `Confirm`，LCD 切换到网络状态页并显示 `Starting network...`
4. 观察状态依次进入 `Connecting WiFi...`、`Checking WiFi...` 和 `WiFi connected`；WiFi 断开后按组件快照显示当前可用的 4G 状态
5. WiFi 恢复且通过 10 秒稳定检查后，显示 `Switching to WiFi...`，默认网卡可用且稳定 3 秒后显示 `WiFi connected`
6. WiFi/4G 均无 stable ready 时，连续 60 秒后应显示 `No network` 和 `WiFi Settings`；点击按钮可返回配置页重新提交 WiFi。不得把该 UI 超时解释为蜂窝自恢复触发器
7. 系统开始初始化 LTE 模组和 WiFi 后，串口日志应显示网络状态；有 SIM 卡时还应显示 USB 枚举、IMEI/ICCID 和 4G 获取 IP 信息
8. 判断通过：组件启动后可先观测 4G；确认按钮提交 WiFi 配置后再观察 WiFi 建链。过程/错误状态不保留过时的已连接文案，且在线状态与组件 ready、当前默认网卡一致
9. 在 4G READY 后拔出 SIM，确认 `lsd_4g` / `lsd_net_mgmt` 先报告真实断开，随后组件快照进入 `if=NONE`、`ready=N`、`WAIT_LINK`、`link=0`、`ip=0`；按当前 5 秒日志周期，最迟应在下一条 Demo 状态快照中可见
10. 拔卡后继续观察，确认 Demo 不调用 reconnect、不执行 LTE power cycle、重试、退避、自检或 manager 重建；重新插卡是否恢复只取决于底层后续是否产生真实上报，不作为本 Demo 验收要求

## 设计要点

- LTE 模组为 NT26-KCN B（CAT1），通过 USB (GPIO19/GPIO20) 以 ECM 模式接入
- LTE 供电由 IOEX P1_1 控制 ETA6027S2F LDO 使能（高电平开启 VLTE 3.3V→4.0V）
- 双网策略：WiFi 优先，默认接口由 `network_manager` 与闭源库依据已上报事实选择
- `lsd_net_mgmt` 为闭源网络管理库，封装了 WiFi + USB ECM 双链路管理
- `network_manager` 负责 WiFi 稳定窗口和事件桥；组件通过 linker wrapper 旁路消费闭源库本来就会提交的 4G connected/disconnected，先调用真实 `lsd_net_send_event()` 再缓存事实，Demo 不直接调用该接口
- 5 秒 ESP-NETIF lost-IP 定时只作被动保底，不替代闭源 4G 事件，也不产生自动恢复动作
- 连接过程、断网和初始化错误立即刷新；`WiFi connected` / `4G connected` 需默认网卡可用且稳定 3 秒
- 无外网连续 60 秒后显示 `No network`；只有 WiFi 使用组件有限重试，4G 仅保留真实状态
- SIM 拔出或蜂窝丢 IP 时只显示组件已上报的状态；重新插入后是否恢复完全取决于底层后续上报，Demo 不查询、不重拨、不 power cycle
- UI 确认回调只提交配置并通知网络任务，LTE/WiFi 初始化不在 LVGL 事件回调中阻塞执行
- LVGL draw buffer 由异步 SPI DMA 读取；bitmap 提交成功后必须等待
  `display_hal_wait_pending(1000)` 后才调用 `lv_disp_flush_ready()`。提交失败
  时不等待，但所有路径都只释放一次 LVGL flush；显示错误只记录第 1 次及
  每 60 次。

## 硬件要求

- 插入 SIM 卡（nano-SIM）
- 连接 4G 天线
- NT26-KCN B 模组通过 USB (GPIO19/GPIO20) 连接

## 实机验证

- 2026-07-03：WiFi 和 4G 双网连接正常，历史双网选择基线已验证。
- 2026-07-13：默认 WiFi、UART `WiFi SET:<ssid>/<password>` 配置更新、错误 WiFi 回退 4G、正确 WiFi 恢复后切回 WiFi、WiFi 10s 稳定上报和 LCD 3s 状态显示均已实机验证通过；该记录保留作为原网络链路基线。
- 2026-07-19：独立键盘编辑页已用于实机配置 WiFi；同日新增完整网络过程/错误状态，macOS clean build 1774/1774 通过，固件大小 `0x11aab0`，并已在 `/dev/tty.usbserial-110` 全片擦除烧录。
- 2026-07-20：用户实机复测确认 WiFi 配置、网络连接过程、断网重试和错误状态可用，本轮功能验收通过。
- 2026-08-12：`network_manager` 迁移版完成 clean build、COM7 全片擦除/烧录和串口验收；DUAL 中 4G 先获 IP、WiFi 建链后按优先级切换，WiFi 配置页正常，日志无 panic/assert/WDT/任务创建失败。
- 2026-08-14：组件全量 LLVM 纯 C 回归 21/21 通过。因本 Demo 增加 `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=5`，重新执行 clean build；前两次命中已知 `esp-dsp/dspi_conv_f32_ansi.c:184` GCC ICE，按成熟记录限制为 `CMAKE_BUILD_PARALLEL_LEVEL=2`、`NINJAFLAGS=-j2` 后通过 `1794/1794`。应用镜像 `0x131b40`，5MB app 分区剩余 76%，ELF 同时包含 wrapper 与原 `lsd_net_send_event`；COM7 全片擦除、三段烧录及 Hash 校验通过。
- 2026-08-14 启动日志：`robotlog/2026-08-14_10-47-54_lte-net-demo-cellular-event-regression-com7.txt`，SHA-256 `5209C3B988F81F9EF960CEFF7CB0238B7A24F3AE4B202E0700F253C146D00597`；`6741ms` 获 4G IP、`7871ms` 选择 4G、`11791ms` READY，后续稳定。
- 2026-08-14 真实拔卡日志：`robotlog/2026-08-14_10-59-49_lte-net-demo-manual-sim-remove-com7.txt`，SHA-256 `5FB715623A6D41C084745CCF3B6C2DE1C541EB409F7FFA3AA7956CDF8F270340`；`64681ms` 闭源两层同步报告断开，`65301ms` 首次 ping 失败，`66791ms` 下一条 Demo 快照已为 `NONE/not ready/WAIT_LINK/link=0/ip=0`，日志可见上界 2110ms。未出现 reconnect、LTE power cycle、recovery、panic、assert 或 WDT；5 秒 lost-IP 仅为保底，本轮直接事件为主路径。
- 2026-08-19：显示 flush 时序测试经历旧行为 RED 后转为 PASS；macOS clean build `1798/1798`，应用 `0x133680`。已更新 TF `1.0.0` 包，未烧录，Launcher 切换后首帧待实机复验。

## 配置

默认 WiFi：

- SSID：`lierda-guest`
- 密码：`lsd920249`

WiFi 配置通过 LCD 第一页完成：

- `SSID` 输入框：长度 1~32 字符
- `Password` 输入框：长度 1~63 字符，输入时以掩码显示
- 两项均有效后点击 `Confirm`，配置写入组件 NVS 并提交 WiFi 配置；LTE 状态始终由组件被动观测
- 下次启动先从 NVS 预填上次配置；NVS 缺失或配置非法时预填 `lierda-guest` / `lsd920249`
- 网络启动后不再从串口读取 WiFi 配置

蜂窝消费工程配置：

- `CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=5`
- 该配置只缩短 ESP-NETIF 被动 lost-IP 保底，不承诺物理拔卡在 5 秒内被闭源底层发现，也不允许据此触发任何主动恢复

## 依赖组件

- `laiwfs300`（BSP）、`network_manager`（统一网络入口）
- `network_manager` 内部承接 `lte_hal`（LTE 电源控制）和 `net_mgmt`（`lsd_net_mgmt` 闭源库 + `iot_usbh_ecm`）
- `display_hal`、`touch_hal`、`lvgl`（横屏配置页和网络状态显示）
- `nvs_flash`、`esp_timer`

## 构建

```powershell
& "C:\Program Files\Git\bin\bash.exe" -lc 'rm -rf "$TEMP/laiwfs300_build/CODE" && cp -r "e:/10__AIProject/7_AI陪伴机器人/CODE" "$TEMP/laiwfs300_build/CODE"'
& "E:\10__AIProject\7_AI陪伴机器人\CODE\tools\build_example.ps1" -Example lte_net_demo -Clean
```

macOS：

```bash
bash ./tools/build_example_macos.sh lte_net_demo
```
