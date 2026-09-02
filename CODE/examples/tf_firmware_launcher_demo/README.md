<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# TF Firmware Launcher Demo

本示例把 TF 卡作为离线 Demo 固件仓库。设备正常上电或复位后运行内部 Flash 的固定 Launcher，扫描 TF 卡中的兼容固件包；用户选择并确认运行后，Launcher 校验 SHA-256，将可选的模型/SPIFFS 镜像写入固定数据分区，再将 `app.bin` 写入 `ota_0` 并执行一次性启动。目标 Demo 首次启动后保持 `PENDING_VERIFY`，再次断电或复位时由 Bootloader 自动回滚到 Launcher。

本示例不连接服务器，也不提供屏幕内返回按钮。由 Launcher 启动的已接入 Demo 可长按板载 GPIO8 模拟按键 1.5 秒，软件重启后直接返回固件选择页。

被 Launcher 启动的 Demo 不得调用 `esp_ota_mark_app_valid_cancel_rollback()` 或其他确认当前 OTA 镜像永久有效的接口，否则下一次启动不会自动回到 Launcher。当前仓库 Demo 未调用该接口。

Launcher 启动后先从 TF 卡读取全屏动画和开机音效，将资源全部预加载到 PSRAM 后开始播放；播放期间不再访问 TF，完成后进入固件选择页。运行资源与 Demo 固件包一起保存在 `tf_card_root_ready` 中，不会被目标 Demo 的 `model` 或 `spiffs_data` 镜像覆盖。

## 硬件连接

| 功能 | 信号 |
| --- | --- |
| TF CS | `GPIO9 / SPI2_CS1` |
| TF MISO | `GPIO11 / SPI2_MISO` |
| TF SCK | `GPIO12 / SPI2_SCK` |
| TF MOSI | `GPIO13 / SPI2_MOSI` |
| TF 卡检测 | `TPT29555A P1_6 / TF_CD` |
| LCD CS | `GPIO10 / SPI2_CS0` |
| 返回按键 | `SW_ADC -> GPIO8 / ADC1_CH7`，模拟分压输入 |

LCD 与 TF 共用 SPI2 的 SCK/MOSI，通过不同 CS 区分设备。当前 `TF_CD` 有效电平尚未实机确认，因此日志只显示原始电平，是否可用以 FATFS 挂载结果为准。

GPIO8 不是普通数字按键：硬件由 22kΩ 上拉、33kΩ 下拉和 100nF 滤波电容构成，按下时接地。固件只通过 `ADC1_CH7` 读取：`raw<=3000` 为按下、`raw>=3500` 为释放、中间区间为未知；10ms 采样、30ms 消抖，且上电后必须先稳定释放才允许长按返回。

Launcher 为改善横向卡片滑动刷新速度，LCD 设备使用 40 MHz SPI 时钟和两块 320x80 行 DMA 刷新缓冲；TF 设备仍使用 10 MHz。两者通过独立设备配置和应用级 SPI2 互斥串行访问，不会把 TF 卡同步提升到 40 MHz。

当前硬件不支持软件直接启用 TE 同步：2.4 寸模组规格书定义 LCM pin 4 为 `TE`，液晶板原理图也将该信号接入 `LCD_TE` 并引至核心接口，但核心板 A0 的 `CN3` 未提供 `LCD_TE` 到 ESP32-S3 GPIO 的连接。若后续需要硬件 TE 同步，需飞线接入空闲 GPIO 或修改下一版核心板。

## 开机动画与音效

- 原始资源保存在 `assets/boot_animation.gif` 和 `assets/boot_sound.wav`。
- 执行 `tools/prepare_launcher_boot_assets.py` 将 GIF 转换为逐帧 zlib 压缩 RGB565，并从 WAV 提取原始 PCM。运行资源位于 `tf_card_root_ready/demo_hub/launcher/boot_animation.r565z` 和 `boot_sound.pcm`。
- 动画为 320x240、50 帧、100ms/帧，压缩文件为 1026394 字节；音频为 16kHz、16bit、mono PCM，为 161982 字节。
- 动画压缩数据、RGB565 帧缓冲、miniz 解压状态和 PCM 均由 PSRAM 承载；LCD 的两块 320x80 行 DMA 缓冲继续使用内部 RAM。
- 音效使用板级 ES8311 输出，音量为 70%；播放前后写入短静音，结束后关闭功放以降低爆音和底噪。
- 动画和音效都结束后释放 PSRAM 资源并进入固件选择页；TF 挂载、资源读取、格式或内存分配失败时，Launcher 记录日志并降级进入固件选择页。
- TF 读取和 LCD 刷屏通过 SPI2 互斥串行化；播放前先读入完整资源，播放期间不访问 TF，避免两个设备竞争共享 SPI2。

## 构建与烧录

在 `CODE` 目录执行：

```bash
# Windows / Git Bash
./tools/build_example.sh tf_firmware_launcher_demo

# macOS
bash ./tools/build_example_macos.sh tf_firmware_launcher_demo
```

macOS 烧录示例：

```bash
bash ./tools/build_example_macos.sh tf_firmware_launcher_demo flash -p /dev/tty.usbserial-110
```

串口名是示意值，需替换为当前机器实际识别到的端口。烧录脚本会按项目成熟入口执行全片擦除，因此会更新 Bootloader、分区表、`otadata` 和 Launcher。

## 准备 TF 卡

1. 将 TF 卡格式化为 FAT32。
2. 先使用现有 Example 构建入口生成目标 Demo 的应用二进制。
3. 使用统一打包工具生成 TF 目录。以下以 `imu_6axis_demo` 为例。

仓库根目录的 `tf_card_root_ready/` 已包含开机资源和 6 个 Demo 包：`audio_ns_demo`、`imu_6axis_demo`、`audio_dual_mic_doa_demo`、`salary_calculator_demo`、`lte_net_demo`、`audio_spatial_spectrum_demo`。可将其内容整体复制到 TF 卡根目录，以下命令用于重新生成或追加单个包。

macOS：

```bash
python3 ./tools/package_tf_firmware.py \
  --app-bin /tmp/laiwfs300_build/CODE/examples/imu_6axis_demo/build/imu_6axis_demo.bin \
  --id imu_6axis_demo \
  --name "IMU 6-Axis Demo" \
  --version 1.0.0 \
  --output /Volumes/TF_CARD
```

Windows PowerShell：

```powershell
python .\tools\package_tf_firmware.py `
  --app-bin "$env:TEMP\laiwfs300_build\CODE\examples\imu_6axis_demo\build\imu_6axis_demo.bin" `
  --id imu_6axis_demo `
  --name "IMU 6-Axis Demo" `
  --version 1.0.0 `
  --output "E:\"
```

如 Demo 依赖 TF 资源，可附加：

```text
--assets-dir <assets_directory>
--model-dir <model_directory>
```

`assets/` 和 `model/` 目录由 Demo 运行时直接从 TF 卡读取。如果 Demo 原构建命令还会烧录 `srmodels.bin` 或 SPIFFS 镜像，则必须使用数据分区镜像参数：

```bash
# audio_ns_demo / audio_dual_mic_doa_demo
--partition-image model=<build_dir>/srmodels/srmodels.bin

# salary_calculator_demo
--partition-image spiffs_data=<build_dir>/spiffs_data.bin
```

打包工具只接受 `model` 和 `spiffs_data` 两个数据分区标签，分别限制为 1MB 和 256KB。工具会计算应用及各分区镜像的大小和 SHA-256，并最后生成 `READY` 文件；已存在的同 ID、同版本目录不会被覆盖。

## TF 目录

```text
/demo_hub/
  launcher/
    boot_animation.r565z
    boot_sound.pcm
  packages/
    imu_6axis_demo/
      1.0.0/
        app.bin
        model.bin          # 可选：ESP-SR 模型分区镜像
        spiffs_data.bin    # 可选：音效等 SPIFFS 分区镜像
        manifest.json
        READY
```

`manifest.json` 示例：

```json
{
  "id": "imu_6axis_demo",
  "name": "IMU 6-Axis Demo",
  "version": "1.0.0",
  "board": "laiwfs300",
  "partition_scheme": "demo-hub-v2",
  "app": "app.bin",
  "app_size": 535712,
  "app_sha256": "64-character-lowercase-sha256",
  "partitions": []
}
```

`partitions` 中的资源项包含 `label`、`file`、`size` 和 `sha256`。Launcher 最多显示 16 个兼容包；缺少 `READY`、manifest 非法、文件大小不匹配、板卡不匹配或分区方案不匹配的包会被拒绝，并计入界面的 `rejected` 数量。

## 使用流程

1. 插入准备好的 TF 卡并启动设备，等待开机动画和音效播放完成。
2. 等待 Launcher 扫描 TF 卡并显示兼容固件卡片。卡片以横向轮播方式排列，左右滑动后会平滑吸附到屏幕中央；底部圆点表示当前卡片位置。
3. 将目标固件滑动到中央，或直接点击目标卡片；中央卡片会高亮，底部显示名称和版本。
4. 点击右下角 `Run`，等待 `Verifying` 和 `Installing` 进度完成，设备自动重启进入目标 Demo。
5. 左下角刷新按钮可重新扫描 TF 卡；扫描或安装期间轮播和操作按钮会锁定，避免并发访问 SPI2。
6. Demo 运行期间长按 GPIO8 达 1.5 秒，固件自动重启并跳过开机动画/音效，直接回到固件选择页。短按、上电时一直按住都不触发。
7. Demo 运行期间断电或物理复位，下一次启动也会自动回到 Launcher，但保留正常开机动画和音效。

安装过程中不得拔出 TF 卡或断电。写入或校验失败时 Launcher 不切换启动分区，设备仍保留固定恢复入口。依赖 TF 模型、音频或图片的 Demo 运行期间也必须保持 TF 卡插入。

界面使用固定尺寸卡片、LVGL 原生惯性滚动和居中吸附，不在拖动过程中创建对象或执行 TF 文件操作；状态文字和进度控件仅在数据变化时刷新，以减少滑动期间的重绘开销。

## GPIO8 返回日志

返回链路使用 `LAUNCHER_RETURN` 前缀的低频日志。正常路径依次出现组件启用、`stable=PRESSED`、`trigger`、`OTA rollback mark=OK`、`restarting to factory Launcher`、`request consumed` 和 `skipping boot animation/audio`。稳定释放会记录 `stable=RELEASED`，正常 10ms 采样不逐次打印。

直接作为 `factory` 固件烧录的 Demo 会记录 `running app is not ota_0`并禁用返回采样任务。NVS、OTA 或 ADC 操作失败时日志包含 ESP-IDF 错误名，不强制重启。

## 当前验证状态

- 40 MHz LCD + 320x80 行双 DMA 缓冲版本已完成 macOS clean build、全片擦除、烧录和启动验证，固件大小为 `0x9ba10`。
- `factory` 为 2MB，`ota_0` 为 4.25MB，`model` 为 1MB，`spiffs_data` 为 256KB，已启用 Bootloader app rollback。
- `demo-hub-v2` Launcher 和当前 6 个目标 Demo 均已完成 macOS clean build；`audio_ns_demo`、`audio_dual_mic_doa_demo` 已携带模型镜像，`salary_calculator_demo` 已携带 SPIFFS 金币音效镜像，`audio_spatial_spectrum_demo` 仅需应用镜像。
- 启动日志确认 LCD `pclk=40000000`、TF `speed=10000 kHz`，5 个包 `accepted=5 rejected=0`，未出现缓冲分配失败、SPI 断言、panic 或重启。
- TF 挂载、LCD/TF 共用 SPI2 和触摸初始化已实机验证；用户实屏确认 40 MHz + 80 行双缓冲后滑动流畅度明显改善，仅剩轻微撕裂。当前硬件没有可用的 TE GPIO 同步链路，因此保留该轻微撕裂作为已知限制；真实 `ota_0` 启动和复位回滚仍需实机验证。
- TF 开机动画/音效版本已按 macOS 成熟入口完成 clean build、全片擦除和 `/dev/tty.usbserial-1130` 烧录，固件大小为 `0xab1d0`。串口确认资源预加载、50 帧动画、音效、Launcher UI 和 5 个固件包扫描全部完成，无栈溢出、panic 或二次复位；用户实屏确认开机动画与音效可用。
- GPIO8 返回状态机纯 C 测试已通过。当前代码已完成 Launcher、6 个首批 Demo、`touch_2048_demo` 和 `xiaozhi_companion_robot_demo` 的 macOS clean build；Launcher 固件为 `0xaf570`。本轮未烧录，真实 `ota_0` 长按回滚、主动返回跳过启动音画和普通复位保留启动音画仍待实机验证。
