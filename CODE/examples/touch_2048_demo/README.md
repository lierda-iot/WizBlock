<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Touch 2048 Demo

本 Demo 在 LAIWFS300 的 320x240 横屏上运行经典 4x4 2048。棋盘支持四向滑动、一步撤销、立即重开、胜利后继续游戏、无路可走提示，以及 NVS 最高分保存。

## 操作

- 在左侧棋盘内滑动至少 30px；主轴必须严格长于副轴。
- 每次接触最多执行一步，短滑、点击和等长对角滑动不会移动棋盘。
- 右下角左箭头用于一步撤销，刷新图标用于重新开始。
- 重新开始和复位不会清除已经成功写入 NVS 的最高分；复位后当前棋局从新局开始。

## 模块

- `game_2048_core`：纯 C 棋盘、合并、计分、随机落子、胜负和快照。
- `game_2048_gesture`：纯 C 的 30px 单接触单事件手势状态机。
- `game_2048_storage`：可注入的最高分存储接口。
- `game_2048_ui`：固定 320x240 LVGL 布局和触摸事件接线。
- `touch_2048_demo_main`：板级显示、触摸、NVS、ESP 随机源和健康日志。

所有功能文件都位于本 Demo；公共组件和其他 Demo 仅作为只读依赖。

## 主机测试

从 `CODE/examples/touch_2048_demo` 执行：

```bash
bash tests/run_host_tests.sh
```

测试覆盖四向移动、单次合并、饱和计分、指数 31、均匀随机拒绝采样、RNG 失败原子回滚、一步快照、胜负、30px 手势边界和存储降级。

## 构建与烧录

从 `CODE` 目录使用项目已验证的 macOS 入口：

```bash
bash ./tools/build_example_macos.sh touch_2048_demo
bash ./tools/build_example_macos.sh touch_2048_demo flash -p /dev/cu.usbserial-1130
```

`flash` 会执行 clean build、全片擦除和烧录。默认 LCD SPI 时钟为 40MHz，两块 DMA 缓冲各为 320x80 像素；每次 LVGL flush 都等待 LCD 传输实际结束后再 ready。

如果 40MHz 未通过显示稳定性门禁，只修改本 Demo 的 `sdkconfig.defaults`：将 `CONFIG_TOUCH_2048_LCD_PIXEL_CLOCK_40MHZ=y` 替换为 `CONFIG_TOUCH_2048_LCD_PIXEL_CLOCK_20MHZ=y`，然后重新执行同一 clean build、全片擦除、烧录和稳定性验证。

串口日志以 `[2048]` 开头，只输出启动、方向结果、撤销、重开、胜负、错误和每 30 秒健康统计。健康统计包含有效/无效移动、触摸拒绝、当前分数、最高方块、空格、LCD/NVS 错误和剩余堆。

## TF Launcher 包

构建完成后，从 `CODE` 目录执行：

```bash
python3 ./tools/package_tf_firmware.py \
  --app-bin /tmp/laiwfs300_build/CODE/examples/touch_2048_demo/build/touch_2048_demo.bin \
  --id touch_2048_demo \
  --name "Touch 2048" \
  --version 1.0.0 \
  --output /Volumes/TF_CARD
```

该包只包含 `app.bin`，不需要模型、SPIFFS 或外部素材。`tf_package/manifest.json.in` 是 manifest 字段参考；正式包由统一工具计算实际大小和 SHA-256。固件不调用任何 OTA 永久确认接口；从 Launcher 启动后，长按 GPIO8 模拟按键 1.5 秒可自动重启并直接返回固件选择页，外部复位仍按既有 rollback 机制返回 Launcher 并保留正常开机音画。

## 第三方来源

固定来源、采用范围和本地改动见 `THIRD_PARTY_NOTICES.md`，完整许可证位于 `licenses/`。
