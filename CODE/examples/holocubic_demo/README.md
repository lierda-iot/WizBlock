<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# HoloCubic Demo

在本项目 ESP32-S3 硬件上独立实现 HoloCubic 风格透明显示桌面站。首版提供黑底动画、杭州天气、北京时间和空间频谱四页。主页使用物理 `(40,0,240,240)` 显示域适配 `40x40 mm` 分光棱镜，保留左右预镜像并将整体输出旋转 `180°`。

设备上电后直接进入主页。动画来自内部 Flash，网络固定使用 4G/CAT1；本 Demo 不依赖 TF 卡，不启动 Wi-Fi，不读取或保存 Wi-Fi 凭据，也不提供 Wi-Fi 配置、扫描或密码键盘页面。

## 参考与许可边界

- 产品形态和交互行为参考：<https://gitcode.com/gh_mirrors/ho/HoloCubic>
- 本 Demo 使用本项目 ESP-IDF 驱动、GPIO 映射和公共组件独立实现，不复制上游 Arduino、MPU6050、板级 GPIO 配置、GPL-3.0 源码或未授权素材。
- Demo 私有 JSON 解析器 `main/third_party/jsmn.*` 使用 MIT 许可，许可标识保留在源码头部。
- Flash 正式动画由用户提供的 49 帧 RGB565LE 素材生成；资源副本位于 `assets/animation_frames/`。动画校验失败时使用的降级动画由程序绘制，不包含上游图片素材。

## 页面与交互

- 页面顺序：`ANIMATION -> WEATHER -> CLOCK -> SPECTRUM`，循环切换。
- 天气页采用黑底悬浮的体积天气主体，底部显示 `HANGZHOU` 与大号实时温度；晴、云、阴、雾、雨、雪、雷暴和离线状态使用独立视觉资源，最高/最低温、湿度、更新时间与新鲜度保留为弱化辅助信息。
- 时间页采用黑底发光天体、大号 `HH:MM`、独立秒数、日期和同步状态，与天气页保持同一视觉语言。
- 空间频谱页使用 ES7210 双麦 `16kHz/16bit` TDM、512 点 FFT 和 24 个 `80Hz~8kHz` 对数频段，主频谱结合双麦平均和稳定后的相对 DOA；点击/前倾依次循环 `RADAR`、`MIRROR`、`WATERFALL`、`METABALLS`、`LEVEL`、`DUAL` 六种黑底模式。
- CST836U 左滑显示下一页，右滑显示上一页，点击执行确认；频谱页点击只切换频谱子模式。
- BMI260 主页切换使用左右倾斜轴：左倾切换上一页、右倾切换下一页；前倾只执行确认，不再用前后倾斜切页。正式阈值和物理方向仍需实机标定。
- B站粉丝数、相册、RGB 氛围灯和环境光自动亮度不属于首版。

## Flash 动画资源

构建输入固定为：

```text
assets/animation_frames/frame000.rgb565
...
assets/animation_frames/frame048.rgb565
```

每帧必须为 `240x240`、RGB565 little-endian、行优先、无文件头，大小固定 `115200` bytes。构建工具要求恰好存在 49 个连续编号文件；缺号、多余文件或尺寸错误都会使构建失败。

构建时 `tools/generate_frame_image.py` 将全部帧打包为 `build/holo_frames.bin`：

- 32-byte little-endian 镜像头，魔数 `HFRM`，格式版本 `1`；
- 49 帧、100 ms/帧，载荷 `5644800` bytes；
- 载荷使用 CRC32 校验；当前正式资源 CRC32 为 `296ed2b9`；
- 当前完整镜像为 `5644832` bytes，SHA-256 为 `43f3619872dc39aa8c49618202a1710dd27965bb918de257242281656946c6ac`。

镜像烧录到 8 MB Flash 的 `holo_frames` 只读数据分区，偏移 `0x240000`，大小 `0x570000`。启动时固件检查镜像头、分区边界和 CRC32，再将全部载荷一次性读入 PSRAM；校验成功后以 10 fps 播放，播放期间不读取 Flash 或文件系统。分区缺失、镜像损坏或 PSRAM 分配/读取失败时自动回退到内置程序动画。

## 内置视觉资源

天气和时间页使用九份 `160x112` RGB565 little-endian 位图，通过 `EMBED_FILES` 固化在应用镜像中，并合成到 `240x240` 逻辑画布的 `(40,24)`。素材外围保持纯黑，不在棱镜中形成可见矩形底图。资源由 Demo 内确定性脚本独立生成，不复制上游素材：

```bash
cd CODE/examples/holocubic_demo
python3 tools/generate_visual_assets.py --output-dir main/assets
```

## 4G、天气与时钟

网络统一由公共 `network_manager` 管理，Demo 在启动前固定选择 `NETWORK_MANAGER_MODE_4G_ONLY`。天气和 SNTP 只在 CAT1 已稳定就绪且稳定活动接口为 4G 时运行；4G 不可用不会阻塞动画、时钟、触摸或姿态交互。

天气固定使用杭州坐标 `30.2741,120.1551`，通过 Open-Meteo HTTPS API 获取当前温度、湿度、天气码和当日最高/最低温。成功后每 30 分钟刷新，失败后 60 秒重试；接口不需要访问 token。

时区固定为北京时间。启动优先从 AIP8563 RTC 恢复系统时间，联网后通过 SNTP 校时并回写 RTC。RTC 和网络时间均不可用时显示未校时状态。

## 构建与测试

```bash
cd CODE/examples/holocubic_demo
bash tests/run_host_tests.sh
python3 tests/test_generate_frame_image.py

cd ../..
bash ./tools/build_example_macos.sh holocubic_demo
```

当前分区表已由原版本变更。设备第一次使用本版本时必须先全片擦除，再完整烧录 bootloader、分区表、应用和 `holo_frames.bin`；只覆盖应用会导致动画分区缺失或使用旧布局。macOS 成熟入口的 `flash` 动作已固化 `erase-flash flash`：

```bash
cd CODE
bash ./tools/build_example_macos.sh holocubic_demo flash -p <当前实际串口>
```

本轮只完成构建与离线验证，未连接或烧录设备。

## 硬件与当前状态

- 主控：ESP32-S3
- 显示：ST7789V3，320x240 横屏；主页物理窗口 `(40,0,240,240)`，左右黑边各 40 px，左右预镜像后整体旋转 `180°`
- 触摸：CST836U，通过公共 `touch_hal`
- 姿态：BMI260
- RTC：AIP8563
- 动画存储：内部 Flash `holo_frames` 数据分区；运行时整组预载到 PSRAM
- 网络：公共 `network_manager` 4G-only + HTTPS + SNTP

离线验证已通过纯 C 主机测试、镜像生成器测试和 macOS clean build。当前应用大小为 `0x194ab0`，小于 `0x220000` factory 分区；构建烧录清单已包含 `0x240000 holo_frames.bin`。

仍待实机验收：首次全片擦除和完整烧录、Flash 镜像启动预载、真实 LCD 10 fps、4G 稳定联网、Open-Meteo/SNTP/RTC 回写、四页与频谱六模式交互、棱镜观感以及 30 分钟并发稳定性。
