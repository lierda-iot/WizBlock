# macOS 环境准备

[中文](README.md) | [English](README.en.md)

## 检查顺序

1. 使用数据线连接设备，检查 `/dev/cu.*` 中是否新增 CH340E 串口。
2. 安装 Xcode Command Line Tools 与 Git。
3. 安装并激活 ESP-IDF v5.5.4，确认 `idf.py --version`。
4. 确认`rsync`可用；当前维护环境从仓库根目录进入`./CODE`并调用`bash ./tools/build_example_macos.sh <example> clean`。
5. 该脚本完成激活、完整镜像和clean build；不把直接`idf.py`写成已验证替代方法。

现有脚本仍含维护者用户目录、Python minor版本、CA bundle和ESP-IDF安装路径假设，按人工裁决保持不变并排除于公开候选。若环境激活失败，按ESP-IDF v5.5.4安装记录修复同一环境，不切换未知runner；可移植入口留到最终S5。

## 串口与权限

- 烧录端口以当前枚举结果为准，`/dev/cu.usbserial-*` 只是设备类型示例。
- 若端口被占用，先关闭 monitor/串口工具，不用重复擦除解决占用。
- 若设备停在下载态，先按当前 USB-UART 控制线流程复位并观察 ROM 启动模式；没有必要时不要重复写 Flash。
