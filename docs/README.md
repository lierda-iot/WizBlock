# 开发文档

[中文](README.md) | [English](README.en.md)

本文档树面向不依赖 AI Skill 的开发者。所有命令使用仓库相对路径或显式占位符；不知道或尚未验证的内容明确标为待确认。

需要 AI 编排时使用 [OPEN 开发 Skills](../skills/README.md)；手工文档与 Skill 共享同一事实和最终 S5 边界。

| 入口 | 内容 |
| --- | --- |
| [Quick Start](quick-start/README.md) | 从环境检查到 `display_demo` 构建、烧录和观察 |
| [Windows 环境](setup/windows/README.md) | CH340E、Git、ESP-IDF v5.5.4、镜像和端口 |
| [macOS 环境](setup/macos/README.md) | CH340E、ESP-IDF、权限、镜像和端口 |
| [硬件](hardware/README.md) | 公开板卡事实、连接和安全边界 |
| [开发流程](development/workflow.md) | 选 Example、最小变更、验证和文档同步 |
| [组件开发](development/components.md) | 公共、板级、Example 私有和二进制边界 |
| [测试](development/testing.md) | 主机、clean build、烧录和实机证据分层 |
| [Example 公开说明](examples/README.md) | 公开状态与详细入口 |
| [安全](security/README.md) | 凭据、日志、设备标识和诊断包 |
| [故障排查](troubleshooting/README.md) | 环境、依赖、构建、串口、启动和硬件 |
| [候选发布](release/README.md) | allowlist、许可、安全、验证和人工门禁 |

当前不提供统一可移植构建 wrapper。构建说明复用 `design.md` 已验证的“完整镜像到短 ASCII 路径 + 串行 clean build”约束；公开候选双平台新机器复核仍待完成。
