# 平台契约 / Platform Contract

首版只支持 `windows` 和 `macos`。发现其他平台时返回 `BLOCKED` 并说明“不在首版支持范围”，不得将相似 Unix 环境自动当成 macOS。

The first release supports `windows` and `macos` only. Any other platform returns `BLOCKED`; a Unix-like environment must not be silently treated as macOS.

## 必须显示的事实 / Facts to show

- 平台与版本、CPU 架构、当前 shell。
- 可用磁盘空间和仓库路径是否可写。
- Git 是否存在及版本；ESP-IDF 是否已激活及版本。
- 串口候选数量和稳定、脱敏的候选标签；不得猜测最终设备。
- 当前仓库根和目标 Example 是否存在。

Report the platform/version, CPU architecture, shell, free disk, repository writability, Git/IDF availability, redacted serial candidates, repository root, and selected Example.

## Windows

- 使用 PowerShell 原生命令做发现；路径必须由当前环境解析。
- 不固化维护者盘符、用户名、IDF 安装位置或串口号。
- 需要 Git Bash 镜像约束时只引用当前公开文档；最终 S5 前不复制私有脚本中的环境路径。

## macOS

- 识别 Apple Silicon 与 Intel；不要假设 Homebrew 前缀。
- 串口权限、驱动和安全提示按当前机器事实报告。
- 不固化用户名、Python minor 版本、证书路径或设备节点。

## 权限与恢复 / Permissions and recovery

只读发现不需要安装确认。任何安装、配置、下载、目录创建、日志保存或设备写入都先输出拟执行动作并请求确认。失败后保留首个可行动错误，提供最小恢复步骤；不得静默切换工具链、镜像、端口或目标目录。

Read-only discovery needs no installation approval. Installation, configuration, downloads, directory creation, saved logs, and device writes require confirmation. On failure, keep the first actionable error and do not silently switch toolchains, mirrors, ports, or targets.
