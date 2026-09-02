<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Salary Calculator Demo

工资实时计算演示，复用 `lvgl_demo` 的 LVGL v8.4.0 横屏显示和触摸方案。

## 功能

- 设置月薪，支持数字键盘直接输入以及每次 1000 元的减/加按钮
- 设置金币音效触发间隔，支持数字键盘输入以及每次 1 元的减/加按钮，默认 1 元
- 设置上班时间和下班时间，每月固定按 22 个工作日计算
- 显示设备 AIP8563 RTC，并可校准年、月、日、时、分、秒
- 进入赚钱页时立即显示当天已经赚到的金额，之后每秒重新计算并刷新
- 每跨过用户设置的新金额档位播放一次 `coin_burst.wav`，不按每秒播放音效
- 使用用户提供的静态背景图，金币持续掉落；达到新音效档位时触发额外金币动画，不显示固定金额提示
- 赚钱页可返回设置页修改参数
- 月薪、音效间隔和上下班时间保存到 NVS

## 构建与烧录

在 `CODE` 目录执行：

```bash
# Windows / Git Bash
./tools/build_example.sh salary_calculator_demo

# macOS
bash ./tools/build_example_macos.sh salary_calculator_demo
```

烧录：

```bash
# Windows / Git Bash
./tools/build_example.sh salary_calculator_demo flash

# macOS，串口必须替换为客户机器实际识别到的端口
bash ./tools/build_example_macos.sh salary_calculator_demo flash -p /dev/cu.usbserial-1410
```

## 操作与预期

1. 设置页显示当前 RTC。月薪可使用数字键盘输入或按 1000 元减/加；音效间隔可使用数字键盘输入或按 1 元减/加。
2. 音效间隔默认为 1 元且必须为正整数。滚动设置上下班时间；需要校准 RTC 时，滚动设置完整日期和时间。
3. 点击 `Confirm` 进入赚钱页，金额应按当前时间立即计算。
4. 金额应每秒刷新；只有跨过用户设置的新金额档位时才播放一次金币音效并出现额外金币动画。
5. 点击 `< Settings` 返回设置页。

## 音频资源

音效文件位于 `spiffs/coin_burst.wav`，格式必须为 PCM、16kHz、16bit、mono。构建时会自动生成并烧录 `spiffs_data` 分区；文件缺失或解析失败时回退为短提示音。

## 背景资源

- `money_background.png`：用户提供的原始图片，保留不修改
- `money_background_320x240.png`：本地适配后的 320x240 预览图
- `main/money_background.rgb565`：固件实际嵌入的 RGB565 资源，运行时不需要图片解码器

## 验证状态

- 主机逻辑测试通过：完整日期合法性、闰年、逐秒金额计算和音效间隔校验
- macOS clean build 通过（2026-07-17）
- 实机验证通过（2026-07-17）：设置页输入、RTC 设置、触摸与显示、每秒金额刷新、可配置档位音效、背景与金币动画、返回设置页均正常
