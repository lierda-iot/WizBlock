# OPEN Skill 流程契约 / Workflow Contract

## 编排顺序 / Orchestration order

```text
preflight -> driver
          -> git -> clone -> first-example
          -> idf -> components -> build -> flash -> monitor
failure or uncertainty -> diagnose
```

子 Skill 可以独立调用，因此调用者必须显式提供它所需的仓库根、平台、Example、端口或已有证据。总 Skill 只能编排结果，不得通过聊天上下文制造隐藏成功状态。

Each child Skill is independently invocable. Its caller supplies the repository root, platform, Example, port, and reusable evidence explicitly. The orchestrator composes results but never invents success from hidden context.

## 依赖传播 / Dependency propagation

| 条件 | 允许继续 | 必须阻断或询问 |
| --- | --- | --- |
| 驱动缺失且串口不可见 | build | flash、monitor=`BLOCKED` |
| Git 不可用且本地无仓库 | preflight、driver、diagnose | clone 及源码下游=`BLOCKED` |
| Git 不可用但仓库和依赖已存在 | 独立事实检查 | 不得假设更新成功 |
| IDF 缺失、激活失败或版本错误 | driver、git、clone、diagnose | components、build、flash=`BLOCKED` |
| 网络不可用 | 可复用的本地缓存与只读检查 | 需要下载的 install/clone/components=`BLOCKED`；不得伪造离线成功 |
| components 失败 | diagnose | build、flash=`BLOCKED` |
| build 失败 | monitor 仅可独立观察已有设备 | flash=`BLOCKED`，进入 diagnose |
| 多个端口 | build | flash/monitor=`NEEDS_USER`，不得猜测 |
| 无设备 | build | flash/monitor=`BLOCKED` |
| 未确认设备写入 | 只读检查 | flash=`NEEDS_USER` 或由用户明确跳过 |
| flash 失败 | diagnose | 不宣称完成，不自动重复写入 |

## 副作用确认 / Side-effect gates

- 安装驱动、Git 或 ESP-IDF：涉及联网和文件写入，执行前显示来源、目标和预计影响并取得确认。
- clone、更新或配置仓库：显示远程、目标目录和具体动作；禁止用 reset、clean、强制更新处理冲突。
- 依赖下载：显示 manifest、网络访问和写入目录。
- build：显示 Example、平台、clean 与输出目录；最终 S5 前不得新增替代构建入口。
- flash：必须有成功构建证据、唯一明确端口、目标设备确认和当次设备写入确认。
- monitor：只读串口可执行；保存日志属于文件写入，需显示目标并脱敏。

Installation, repository mutation, downloads, build writes, device writes, and saved logs each require the immediately relevant confirmation and must be reported in `side_effects` only after they occur.

## 最终 S5 边界 / Final-S5 boundary

当前已有本地构建/烧录脚本含维护者环境配置，不进入公开候选并保持只读。S7 只实现 Skill 编排、状态契约、公共状态脚本和失败策略。最终 S5 完成可移植脚本并在 Windows/macOS 新机器验证前：

- `open-build` 可以读取文档、检查参数和解释阻塞，但不得把未验证命令写成公开入口。
- `open-flash` 和 `open-monitor` 不得绕过缺失入口拼装命令。
- 总 Skill 的 `all` 动作在 build/flash 链路返回 `S5_PORTABLE_ENTRY_PENDING`。

Existing maintainer scripts remain read-only and excluded from the public candidate. Until final S5 provides validated portable entry points, the Skills may explain the blocker but may not synthesize replacement build, flash, or monitor commands.
