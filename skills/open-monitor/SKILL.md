---
name: open-monitor
description: Observe one explicitly selected OPEN device serial stream, redact sensitive content, and summarize startup or health evidence. Use after flash or independently for an already running device.
---

# OPEN Monitor

Run independently with one explicit port and expected Example evidence.

## Inputs

- `language`, platform, explicit port, baud/settings from the public Example documentation, and observation goal.
- Optional output-log path when the user wants a saved log.

## Workflow

1. Discover current port candidates and require explicit selection on ambiguity.
2. Confirm that the requested serial settings come from public documentation; do not copy a maintainer-specific port or path.
3. Opening a port for read-only monitoring must not be described as a device write.
4. Observe only long enough to meet the stated goal; retain the first boot/error/health evidence rather than dumping an entire stream.
5. Redact Wi-Fi passwords, tokens, Bearer credentials, complete device identifiers, and user-home paths using `../scripts/open_skill_runtime.py` equivalent rules.
6. Saving a log is a filesystem write: show the target, request confirmation, and save only the redacted form.
7. Compare observed markers with the selected Example README. Report missing or conflicting markers without upgrading evidence.
8. Close monitoring cleanly and summarize whether evidence is build/boot/behavior level.

## S5 boundary

If monitoring depends on a public wrapper that final S5 has not supplied, return `BLOCKED`. A standard read-only serial facility may be used only when its explicit settings are already documented and the user selected the port.

## Result

Return [the status contract](../references/status-contract.md) and [output template](../references/output-templates.md). `PASS` means the requested observation completed, not that every Example behavior passed.
