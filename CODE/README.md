# CODE 源码与 Example 构建

[中文](README.md) | [English](README.en.md)

`CODE/` 包含 ESP-IDF 主工程、公共组件和相互独立的 Example。项目目标芯片为 ESP32-S3，当前可靠版本口径为 ESP-IDF v5.5.4。

## 目录

| 路径 | 职责 |
| --- | --- |
| `main/` | 私有事实源中的主工程入口；首次公开候选不把它作为 Quick Start |
| `components/` | 公共、板级和二进制适配组件；见[组件索引](components/README.md) |
| `examples/` | 独立 ESP-IDF 工程；见[Example 索引](examples/README.md) |
| `tools/` | 当前私有环境工具；本轮保持只读并排除于公开候选，不作为跨机器公开入口承诺 |
| `boards/laiwfs300/` | 历史草稿；当前正式板级事实源是 `components/laiwfs300/`，公开候选排除前者 |

## 前置条件

- ESP-IDF v5.5.4 已安装并在当前终端激活，`idf.py --version` 可用。
- 仓库位于较短的 ASCII 路径，或按下方流程镜像到临时 ASCII 路径。
- 多个 Example 串行构建；不要让两个构建共享同一个镜像目录。
- 首次依赖解析需要联网；离线缓存和依赖锁策略留到最终 S5 完成。

## Windows：当前维护环境已验证入口

先在当前私有事实源的仓库根目录、Git Bash中完成设计4.1.2规定的完整镜像：

```bash
rm -rf "$TEMP/laiwfs300_build/CODE"
mkdir -p "$TEMP/laiwfs300_build"
cp -r "./CODE" "$TEMP/laiwfs300_build/CODE"
```

然后在仓库根目录的PowerShell中以相对路径调用既有脚本；将`<example>`替换为目录名：

```powershell
& "./CODE/tools/build_example.ps1" -Example <example> -Clean
```

2026-08-31的`display_demo`复验使用该入口在170.4秒内退出0并完成`1421/1421`。外层`build_example.sh`本轮虽生成了完整固件，但调用端超过200秒未正常返回，不作为本轮通过入口。

说明：

- 每次源码修改后都必须重新执行完整镜像和 clean build。
- 不使用`Copy-Item`替代Git Bash `cp -r`镜像。
- 当前 `build_example.ps1` 含维护者机器配置，按人工裁决保持不变且不进入公开候选。
- 若 Xtensa GCC 在高并发下连续出现已知 ICE，可在同一入口把 `CMAKE_BUILD_PARALLEL_LEVEL` 与 `NINJAFLAGS` 限制为2；不要切换工具链或改源码规避。

## macOS：当前已验证入口

在当前私有事实源的仓库根目录执行：

```bash
cd ./CODE
bash ./tools/build_example_macos.sh <example> clean
```

该入口与 `design.md` 4.1.3 一致，但脚本当前含维护者用户目录、Python版本和证书路径假设，按人工裁决保持不变且不进入公开候选。

## 公开候选边界

上述脚本未进入 `OPEN_REPOSITORY/`，公开候选当前没有经过验证的跨机器 build/flash/monitor 命令。直接执行 `idf.py fullclean/build` 没有本轮公开候选证据，不再作为替代入口。最终 S5 将在不改写本阶段历史证据的前提下完成参数化入口、端口选择和 Windows/macOS 新机器复核。

## 配置与依赖

- 提交并发布审核后的 `sdkconfig.defaults`；运行生成的 `sdkconfig`/`sdkconfig.old` 不进入候选。
- 真实 SSID、密码、Token、证书和设备标识不得放在 defaults 或源码中。
- `idf_component.yml` 描述 managed components；ESP-IDF v5.5.4 是当前唯一已验证口径。
- `dependencies.lock` 当前被私有规则忽略；公开可复现锁策略延后到最终 S5。

## 验证状态

Example 的主机测试、clean build、烧录和功能实机状态必须分别记录，统一以 `examples/examples.yml` 和各 README 为入口；禁止用历史固件证据覆盖当前源码。
