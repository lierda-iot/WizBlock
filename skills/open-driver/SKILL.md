---
name: open-driver
description: Check whether the L-AIWFS300 CH340E USB-UART path enumerates on Windows or macOS and guide driver recovery only when evidence shows it is needed. Use for missing or ambiguous serial-device visibility.
---

# OPEN Driver

Operate independently from the orchestrator; require only platform facts and current serial/USB evidence.

## Inputs

- `language` and `platform`.
- Optional redacted USB/serial discovery evidence from `open-preflight`.

## Workflow

1. Confirm the platform and repeat the minimum read-only USB/serial discovery needed to avoid stale evidence.
2. Distinguish device absent, cable/power uncertainty, CH340E visible without a serial port, serial port visible, and multiple candidates.
3. If the port is visible, return `PASS`; do not reinstall a working driver.
4. If the device is physically absent or connection is uncertain, return `NEEDS_USER` with cable, power, and reconnect checks.
5. Only when evidence indicates a missing driver, identify the official vendor/source and show the intended download/install/restart impact.
6. Request confirmation immediately before any download, installer launch, privilege elevation, or restart guidance that changes the system.
7. Re-run discovery after user-completed installation and report new evidence.

## Dependency behavior

An unavailable serial path blocks flash and monitor but does not block build. Never claim that driver installation proves the board is the intended target.

## Result

Return [the complete status contract](../references/status-contract.md) with no device write. Apply [the platform contract](../references/platform-contract.md) and [output template](../references/output-templates.md).
