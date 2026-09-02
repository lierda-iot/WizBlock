---
name: open-flash
description: Flash one explicitly selected OPEN Example to one explicitly confirmed device after successful build evidence and a validated public entry point. Use only for device programming, with mandatory port and write confirmation.
---

# OPEN Flash

Run independently; never inherit an implicit port, device, build result, or permission from the orchestrator.

## Inputs

- `language`, platform, repository root, Example, artifact/build evidence, and explicit port.
- Evidence that the final-S5 public portable flash entry exists.
- Current user confirmation for the target device and device write.

## Workflow

1. Validate successful build evidence for the same repository revision, Example, platform, and artifact set.
2. Discover current port candidates without selecting one. No candidate returns `BLOCKED`; multiple candidates return `NEEDS_USER`.
3. Confirm the chosen port and target device with the user. A single candidate is not implicit permission.
4. If the public portable entry is absent, return `S5_PORTABLE_ENTRY_PENDING`; do not compose a private or direct flash command.
5. Show whether the documented Example flow includes erase, the exact target, affected stored configuration, verification, and recovery behavior.
6. Obtain device-write confirmation immediately before execution.
7. Execute the validated entry once. On failure, stop, retain the first error, and route to `open-diagnose`; do not automatically retry, erase again, switch ports, or change tools.
8. Report erase/program/verify evidence separately. Do not claim functional hardware success.

Use `evaluate_flash_readiness()` in `../scripts/open_skill_runtime.py` for the pre-write result. That function never writes a device.

## Result

Return [the status contract](../references/status-contract.md) and [output template](../references/output-templates.md). Set `device_write=true` only after a real write attempt occurred.
