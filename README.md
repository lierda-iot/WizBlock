# AI开源硬件

[中文](README.md) | [English](README.en.md)

面向 ESP32-S3 与 L-AIWFS300 板卡的组件化硬件示例工程。当前工作区正在生成首次公开候选：源码、Example、组件和开发文档会经过事实、敏感信息、二进制资产与许可证门禁后再发布。

> 当前不是正式开源发布。主许可证、第三方素材许可、`net_mgmt` 静态库交付形态和正式仓库信息尚未完成裁决；根目录 `OPEN_REPOSITORY/` 只是从当前唯一工作仓库按 allowlist 生成的临时审核产物，不作为第二套源码维护，也不构成许可证授予。

> Windows 当前维护环境已按 `design.md` 4.1.2 既有脚本入口和200秒调用端窗口完成 `display_demo` clean build `1421/1421`。现有脚本含本机配置且未修改、未进入公开候选，因此该证据不等于公开仓库新机器入口通过；跨机器入口与双平台复核留到最终 S5。

## 快速入口

- [Quick Start 草案](docs/quick-start/README.md)
- [源码与通用构建方法](CODE/README.md)
- [Example 索引](CODE/examples/README.md)
- [组件索引](CODE/components/README.md)
- [开发文档](docs/README.md)
- [OPEN 开发 Skills](skills/README.md)
- [当前公开候选与门禁](docs/release/README.md)

首个入门 Example 固定为 `display_demo`：当前源码每2秒依次全屏显示白、红、绿、蓝，用于验证 ESP-IDF、ESP32-S3、板级 I2C/IO 扩展和 LCD SPI 链路。当前四色源码已有 clean build 证据，完整四色实屏复核仍待执行。

## 支持口径

| 项目 | 当前口径 |
| --- | --- |
| 目标芯片 | ESP32-S3 |
| 目标板 | L-AIWFS300；`korvo2_mic_test` 另行面向 ESP32-S3-Korvo-2 V3 |
| ESP-IDF | v5.5.4 有项目验证记录；其他版本不作支持承诺 |
| Windows | 首版范围；公开候选手工构建入口待新机器复核 |
| macOS | 首版范围；公开候选手工构建入口待新机器复核 |
| Linux | 首版不承诺 |
| 构建并行 | 当前临时镜像目录为单一共享位置，多个 Example 必须串行构建 |

## 仓库结构

```text
CODE/                 ESP-IDF 主工程、公共组件和独立 Example
docs/                 Quick Start、平台、硬件、开发、测试、排障和发布说明
skills/               总 Skill、11个独立子 Skill、共享状态契约、脚本和测试
OPEN仓库项目记忆.md    私有事实源中的 OPEN 任务进度；不进入正式公开候选
```

正式公开候选不会复制私有 `requirements.md`、`design.md`、项目记忆、调试日志、构建目录、生成 `sdkconfig`、固件备份、真实凭据或未批准资产。

## 构建原则

当前不统一或修改本地构建脚本。项目已经验证的入口是私有事实源中的 `CODE/tools/build_example.sh`、`build_example.ps1` 和 `build_example_macos.sh`；文档只以仓库相对路径记录这些入口，不再把直接调用 `idf.py` 写成已验证的新方法。

这些脚本含维护者环境路径或历史固定端口，按当前门禁不复制到公开候选。公开仓库可供非 AI 用户使用的跨机器 build/flash/monitor 入口仍由最终 S5 参数化并在 Windows/macOS 新机器复核；完成前 Quick Start 只作为草案，不能宣称10分钟闭环已通过。具体边界见 [CODE/README.md](CODE/README.md)。

`skills/` 已提供依赖感知总流程、11个独立子 Skill、统一状态/脱敏脚本和行为测试。Skill 不绕过上述边界：最终 S5 完成前，自动 build/flash/monitor 返回 `S5_PORTABLE_ENTRY_PENDING`，不会自行拼装替代命令。

## Example 状态

`CODE/examples/examples.yml` 是当前35条正式状态与33个实际目录的机器可读清单。状态含义：

- `publish`：可进入本地公开候选，仍受根许可证门禁约束。
- `publish-with-limitations`：可进入候选，但必须保留未验证项与限制。
- `hold`：凭据、依赖、资产、许可或功能阻塞未闭合，暂不复制。
- `archive`：只保留中央归档说明，不作为可构建工程。
- `unavailable`：正式状态存在，但当前目录缺失。

## 安全与许可证

- 不要提交真实 Wi-Fi、Token、证书、设备标识、内部地址或本机绝对路径。
- 非源码资产必须有来源、用途、SHA-256、许可证和再生成说明后才能进入候选。
- `net_mgmt` 已确认为自研内容，但许可证、ABI、源码/二进制交付形态仍待权利人决定。
- 在根许可证正式确定前，不得把候选树描述为“已经开源”。
