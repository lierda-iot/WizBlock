# OPEN Skill 状态契约 / Status Contract

所有总 Skill 和子 Skill 都返回一个完整结果，不依赖聊天中不可见的隐式状态。`schema_version` 当前固定为 `1.0`。

Every orchestrator and child Skill returns a complete result without hidden chat state. The current `schema_version` is `1.0`.

## 状态 / Status

| 状态 | 中文含义 | English meaning |
| --- | --- | --- |
| `PASS` | 本 Skill 的目标已完成并有证据；不代表未执行的下游步骤通过 | This Skill completed its own goal with evidence; downstream work is not implied |
| `SKIPPED` | 步骤明确不适用、被用户跳过或已有可复用结果 | Explicitly not applicable, user-skipped, or satisfied by reusable evidence |
| `FAILED` | 本 Skill 实际执行失败；记录首个可行动错误 | Execution failed; retain the first actionable error |
| `BLOCKED` | 因关键前置条件、依赖或当前阶段边界不能执行 | A prerequisite, dependency, or phase boundary prevents execution |
| `NEEDS_USER` | 必须由用户选择目标、提供参数或确认副作用 | The user must choose a target, provide input, or confirm a side effect |

## JSON 结果 / JSON result

```json
{
  "schema_version": "1.0",
  "skill": "open-build",
  "status": "BLOCKED",
  "language": "zh-CN",
  "summary": "最终 S5 尚未提供公开可移植入口。",
  "facts": {"example": "display_demo", "platform": "windows"},
  "evidence": [],
  "blockers": [
    {"code": "S5_PORTABLE_ENTRY_PENDING", "message": "禁止自行创造新的构建方法。"}
  ],
  "next_actions": ["完成最终 S5 后重试"],
  "side_effects": {
    "filesystem_write": false,
    "network": false,
    "device_write": false
  }
}
```

规则 / Rules:

- `language` 只接受 `zh-CN` 或 `en`。
- `summary` 使用所选语言；机器可读 `status` 和 `blockers[].code` 不翻译。
- `facts` 只写已观察或由用户提供的事实；推断进入 `blockers` 或 `next_actions`。
- `evidence` 不得包含密码、Token、完整设备标识或用户目录。
- `side_effects` 描述本次已经发生的副作用，不描述计划中的动作。
- 重复运行不得把先前失败改写成通过；只有新证据才能改变状态。

The selected language controls human-readable text only. Facts require observed or user-provided evidence. Side effects report what actually happened, and reruns may change status only when new evidence exists.

## 公共状态引擎 / Public runtime

状态传播使用 stdin/stdout JSON，不读写隐藏状态：

```text
python ./skills/scripts/open_skill_runtime.py resolve --language zh-CN
```

stdin 结构为 `{"observed": {...}, "context": {...}}`。stdout 是补齐依赖阻断后的结果映射。无效 JSON 或不支持的语言返回非零退出码。
