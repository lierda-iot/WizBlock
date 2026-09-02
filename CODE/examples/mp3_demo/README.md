<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# MP3 Demo

本 Demo 从 TF 卡曲库播放 MP3，并在 320x240 触摸屏上显示歌曲名、可选封面和同步歌词。界面支持播放/暂停、上一首、下一首、歌曲列表和拖动进度条跳转。

## TF 卡目录

TF 卡根目录按以下结构放置曲库：

```text
/mp3/
  001_歌曲名/
    audio.mp3
    lyrics.lrc       # 可选
    cover.jpg        # 可选
  002_另一首歌/
    audio.mp3
```

- 每个直接子目录代表一首歌，不递归扫描更深层目录。
- `audio.mp3` 是必需文件；目录名是屏幕显示的歌曲名，不解析 ID3 标题。
- 目录名和 `lyrics.lrc` 使用 UTF-8。最多接纳 128 首，按目录名 UTF-8 字节顺序排序。

## 封面要求

`cover.jpg` 是 Demo 固定识别的封面文件名。`.jpg` 和 `.jpeg` 都表示 JPEG 图像格式，但当前目录协议只扫描精确文件名 `cover.jpg`。

封面内容必须满足：

- RGB Baseline JPEG（非 Progressive/渐进式 JPEG）。
- 宽高均为 1～512px。
- 文件不超过 512KiB。

仅修改扩展名不会改变图片编码。可使用 ImageMagick 重新导出：

```bash
magick input.png -resize '512x512>' -colorspace sRGB \
  -interlace none -strip -quality 88 cover.jpg
```

如果仍超过 512KiB，继续降低 `-quality` 后重新导出。封面缺失或不受支持时只显示占位图，不影响 MP3 播放。

## LRC 歌词

`lyrics.lrc` 支持 UTF-8 BOM、一行多个时间标签及以下时间格式：

```text
[00:12]第一行
[00:18.50]第二行
[00:25.125][01:40.000]重复歌词
```

文件最大 256KiB，最多接纳 2048 条带时间的歌词。无合法歌词时保持播放并显示缺省状态。

## 操作

- 上一首、播放/暂停、下一首和列表按钮位于播放页底部。
- 拖动进度条时只预览位置，松手后提交一次 seek。
- 列表页点击歌曲名后切换歌曲并返回播放页。

## 主机测试

从 `CODE/examples/mp3_demo` 执行：

```bash
bash tests/run_host_tests.sh
```

测试覆盖曲库排序与容量、UTF-8 边界、LRC 解析/去重/定位，以及进度映射、单次 seek 和 generation 取消。

## 构建与烧录

从 `CODE` 目录使用项目已验证的 macOS 入口：

```bash
bash ./tools/build_example_macos.sh mp3_demo
bash ./tools/build_example_macos.sh mp3_demo flash -p /dev/cu.usbserial-1130
```

`flash` 会执行 clean build、全片擦除、烧录和 Hash 校验。串口参数为 115200 8N1。

## 运行日志

启动时关注以下摘要：

```text
INIT version=1.0.1
SCAN result=ESP_OK found=... accepted=... rejected=... truncated=...
SCAN accept name=... lrc=... cover=...
SONG ... title=...
RESOURCE ... cover=ok source=... target=...
```

中文歌名必须在 `SCAN accept name=` 和 `SONG title=` 中以 UTF-8 原文输出，不应出现 `___~N` 形式的 FAT 8.3 短文件名。不合规封面会输出 `cover=degraded` 及对应路径，但不应导致播放器退出。

## 第三方依赖

版本、来源、许可和 Demo 内覆盖边界见 `THIRD_PARTY_NOTICES.md`。`gmf_io` 本地改动详见 `components/gmf_io/LOCAL_CHANGES.md`。
