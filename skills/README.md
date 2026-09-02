# OPEN 开发 Skills

[中文](README.md) | [English](README.en.md)

本目录提供一个完整流程编排 Skill 和 11 个可独立调用的子 Skill。它们面向 Windows 与 macOS，默认使用中文或英文输出，不承诺 Linux 支持。

## 入口

| Skill | 用途 |
| --- | --- |
| [`open-dev-all`](open-dev-all/SKILL.md) | 从环境检查到诊断的依赖感知编排器 |
| [`open-preflight`](open-preflight/SKILL.md) | 收集平台、架构、Shell、磁盘、串口、Git 与 IDF 事实 |
| [`open-driver`](open-driver/SKILL.md) | 判断 CH340E 串口是否枚举以及是否需要驱动处理 |
| [`open-git`](open-git/SKILL.md) | 检查 Git；安装或配置前取得确认 |
| [`open-idf`](open-idf/SKILL.md) | 发现、安装或激活 ESP-IDF v5.5.4 |
| [`open-clone`](open-clone/SKILL.md) | 获取公开仓库并确认本地仓库根 |
| [`open-components`](open-components/SKILL.md) | 准备 Component Manager 依赖和锁文件 |
| [`open-first-example`](open-first-example/SKILL.md) | 选择并解释首个 Example，默认 `display_demo` |
| [`open-build`](open-build/SKILL.md) | 执行或评估 clean build 与产物容量 |
| [`open-flash`](open-flash/SKILL.md) | 在明确端口、设备和写入确认后烧录 |
| [`open-monitor`](open-monitor/SKILL.md) | 监控串口、脱敏日志并核对健康标记 |
| [`open-diagnose`](open-diagnose/SKILL.md) | 定位首个环境、依赖、构建、端口、烧录或运行问题 |

## 统一契约

- [状态契约](references/status-contract.md)：`PASS`、`SKIPPED`、`FAILED`、`BLOCKED`、`NEEDS_USER` 的含义和 JSON 结果。
- [流程契约](references/workflow-contract.md)：依赖关系、失败传播、确认门和最终 S5 边界。
- [平台契约](references/platform-contract.md)：Windows/macOS 事实收集、权限和可移植性要求。
- [输出模板](references/output-templates.md)：中文和英文的人类可读结果模板。

`scripts/open_skill_runtime.py` 是无隐藏状态的公共状态引擎。它只处理结果契约、阻断传播、烧录前置检查和脱敏，不安装依赖、不构建、不烧录设备。最终 S5 完成前，`open-build`、`open-flash` 和依赖它们的自动流程必须保留 `S5_PORTABLE_ENTRY_PENDING` 阻塞，不得自行创造新的构建方法。

## 最简调用

Skill 已被客户端发现时，直接使用名称即可：

```text
$open-dev-all 检查当前 Windows 或 macOS 开发环境，使用默认 display_demo；先只读检查，不安装、不联网、不构建、不烧录。
$open-first-example 解释 display_demo 的硬件要求、验证状态和限制。
$open-build 检查 display_demo 的构建前置；没有最终 S5 入口时返回明确阻塞。
$open-diagnose 根据上一项失败结果定位第一个可行动问题，不执行修复。
```

如果客户端尚未自动发现仓库内 Skill，只需把入口写清楚，例如：“读取`skills/open-dev-all/SKILL.md`，按`action=setup`处理当前仓库”。总 Skill 默认 Example 为`display_demo`；只有调用独立阶段时才选择对应子 Skill。端口、安装、联网、构建、日志保存和设备写入仍在动作发生前单独确认。

日常使用无需手写 JSON 或直接调用状态引擎；一句 `$open-dev-all ...` 即可进入总流程，一句 `$open-build ...` 等即可只运行独立阶段。每个 Skill 自身的 `SKILL.md` 是该入口的输入、工作流和结果使用文档，本 README 提供总索引与最短示例。

## 验证

从仓库根目录执行：

```text
python -m unittest discover -s ./skills/tests -p "test_*.py"
python ./skills/scripts/validate_skill_tree.py
python ./skills/scripts/run_skill_validation.py --workspace ../skill-validation-output --live-repository .
```

第三条命令会生成 Windows/macOS × 空环境/已有仓库的四组总 Skill 确定性夹具、一组仅检查当前公开快照文件标记的只读场景、总 Skill 包门禁、11个独立子 Skill 在Windows/macOS上的包与模拟结果矩阵、8个失败/安全流程场景，以及Quick Start的3种阅读角色模拟。3种角色分别检查维护者构建复现、外部阅读型预览和功能验收清单；公开Quick Start必须提供可对照`open-flash`的完整烧录流程，但不得包含可执行烧录命令。8个失败场景覆盖 IDF、组件、构建、烧录失败的依赖传播，以及多串口、无设备、未确认设备写入和最终S5入口缺失。子 Skill 覆盖门禁还会拒绝“新增了 Skill 但未纳入验证矩阵”的情况。

单元测试另覆盖包含空格与中文的路径、缺失公开快照、拒绝覆盖已有验证目录、缺失总 Skill 包和未纳入矩阵的子 Skill。macOS 结果属于模拟覆盖，不代表已在真实 macOS 主机执行。验证器不执行 Git、联网、构建或设备写入；任何包、契约、场景或覆盖门禁失败都会使总验证为`FAILED`，命令行返回非零状态，从而阻塞把该轮验证当作通过。

验证结果会同时保留工作流的原始`aggregate_status`和独立的`validation_status`。空环境按契约出现`FAILED/BLOCKED`、最终S5边界出现`BLOCKED`都可以是预期工作流结果；只有实际结果与场景预期一致时，Windows/macOS平台模拟验证才汇总为`PASS`，不会把阻塞状态伪装成步骤成功。

Skill 的实际安装、联网、文件写入、仓库变更和设备写入仍需在动作发生前按对应 Skill 的确认门处理。
