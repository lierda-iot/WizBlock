<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Camera Face Detect Demo（摄像头人脸检测）

SP0A39 摄像头 + ST7789V3 LCD + ESP-DL 人脸检测演示。该 Demo 保持 `camera_display_demo` 的稳定采集与显示链路，并在预览画面上叠加人脸检测结果。

## 功能

- 复用 SP0A39 DVP 采集链路（640×480 VYUY）
- 复用 ST7789V3 LCD 分块实时预览（240×320 RGB565）
- 使用 `espressif/human_face_detect` 本地检测人脸，demo 阈值为 `0.20` 以提高边缘和低置信度场景命中率
- 在 LCD 预览画面上叠加人脸框，当前选中目标为红框，其他目标为绿框
- 当前结果只有一张人脸时直接选中；首次出现多张人脸或无可延续目标时随机选择；后续多人结果选择与上次目标中心最近的人脸
- 串口日志输出 capture fps、display fps、fresh fps、detect fps、检测数量和各阶段耗时

## 测试方法

### 构建

```bash
bash CODE/tools/build_example_macos.sh camera_face_detect_demo
```

### 烧录

```bash
bash CODE/tools/build_example_macos.sh camera_face_detect_demo flash -p /dev/cu.usbserial-1130
```

串口号按当前机器实际枚举结果替换。

## 硬件连接

- ESP32-S3 核心板（A0）+ C0 扩展板 + E0 摄像头板 + LCD 板
- C0 板 20MHz 晶振正常工作
- DVP、I2C、SPI 和 IOEX 链路同 `camera_display_demo`

## 预期结果

1. LCD 显示摄像头实时画面
2. 检测到人脸时画面上显示红色矩形框
3. 多人场景中只有一个目标框标记为当前选中目标
4. 无人脸时继续实时预览，不应 crash 或重启

## 最终实机基线（2026-07-27）

- 显示保持 `VYUY -> RGB565`、LCD `20MHz`、`80` 行分块、单 DMA 缓冲同步等待；摄像头到 LCD 的像素转换路径未交给人脸检测修改。
- DVP 使用 3 个 PSRAM framebuffer。显示任务根据 DVP 完成回调的 `trans->buffer` 选择实际完成帧，并在读取期间锁定该 framebuffer。
- 人脸检测每 8 个显示帧触发一次。检测期间保持 DVP `stop -> disable -> enable -> start` 完整屏障，避免 DVP DMA 与检测输入准备并发访问 PSRAM 导致异常帧。
- clean build `1547/1547` 通过，固件大小 `0x185760`，5MB app 分区剩余 70%；已在 `/dev/cu.usbserial-1130` 完成全片擦除和烧录，四个区域均通过 Hash 校验。
- 约 115 秒串口日志中 `display/fresh=9.5~10.1fps`、`stale=0`，采集、检测和停采/恢复错误计数均为 0，无 panic、WDT 或异常重启。
- 用户实屏确认当前版本可作为最终版。

## 设计边界

- 检测限频执行，避免阻塞已验证的预览链路
- 已验证“检测复制与 DVP 连续采集并发”会重新引入异常帧，当前禁止移除检测期间的 DVP 停采屏障
- 当前硬件没有 LCD TE 同步，画面轻微撕裂按既有显示链路限制处理
