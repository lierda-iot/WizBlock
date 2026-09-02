# 输出模板 / Output Templates

模板用于人类可读摘要；同一结果还必须满足 [状态契约](status-contract.md)。

## 中文

```text
Skill：<open-skill-name>
状态：<PASS|SKIPPED|FAILED|BLOCKED|NEEDS_USER>
摘要：<一句话结论>
已确认事实：
- <fact>
证据：
- <evidence or “无”>
阻塞/待确认：
- <blocker or “无”>
下一步：
- <smallest next action>
本次副作用：文件写入=<true|false>，联网=<true|false>，设备写入=<true|false>
```

## English

```text
Skill: <open-skill-name>
Status: <PASS|SKIPPED|FAILED|BLOCKED|NEEDS_USER>
Summary: <one-sentence outcome>
Confirmed facts:
- <fact>
Evidence:
- <evidence or “none”>
Blockers / user input:
- <blocker or “none”>
Next action:
- <smallest next action>
Side effects this run: filesystem_write=<true|false>, network=<true|false>, device_write=<true|false>
```

失败输出只保留首个可行动错误和必要上下文；日志片段必须先经 `open_skill_runtime.redact_text()` 等价规则脱敏。不要把 build 通过写成功能实机通过，也不要把 `TX_DONE`、串口打开或设备枚举写成完整功能通过。

Failure output keeps only the first actionable error and necessary context after redaction. Build success, an open serial port, or device enumeration must not be presented as functional hardware success.
