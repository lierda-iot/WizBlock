# Quick Start: display_demo

[中文](README.md) | [English](README.en.md)

Goal: build and run `display_demo` on an ESP32-S3 L-AIWFS300 core board with the ST7789V3 display module, then observe white, red, green, and blue full-screen output in sequence, holding each color for about two seconds.

The 2026-06-12 historical device evidence covers only white and red fills. The current four-color source completed a clean build in the current Windows maintainer environment on 2026-08-31, but complete four-color device replay is pending. The validated scripts contain machine-specific configuration and are excluded from the public candidate.

Current positioning: **Maintainer build reproduction guide + external reading preview + functional acceptance checklist**. This page does not promise an externally executable build or flash entry and provides no public flash command. It places the maintainer build evidence, complete flash workflow, and functional acceptance criteria on one readable and reviewable path.

## Prerequisites

- L-AIWFS300 A0 core board and matching display module.
- USB-C data cable and stable power.
- CH340E driver and a visible serial device.
- Git and activated ESP-IDF v5.5.4 (`idf.py --version`).
- A short ASCII checkout path.

## Current Windows maintainer entry

First perform the complete mirror from the private source workspace root in Git Bash:

```bash
rm -rf "$TEMP/laiwfs300_build/CODE"
mkdir -p "$TEMP/laiwfs300_build"
cp -r "./CODE" "$TEMP/laiwfs300_build/CODE"
```

Then invoke the existing script with a repository-relative path from PowerShell at the workspace root:

```powershell
& "./CODE/tools/build_example.ps1" -Example display_demo -Clean
```

This is the design-4.1.2 entry. The 2026-08-31 replay exited zero in 170.4 seconds and completed `1421/1421`. The unchanged script contains maintainer-specific ESP-IDF paths and is excluded from the public candidate. The outer `build_example.sh` wrapper did not return inside the 200-second caller window in this round and is not recorded as passing.

## Current macOS entry

Run from the private source workspace root:

```bash
cd ./CODE
bash ./tools/build_example_macos.sh display_demo clean
```

This entry has historical macOS clean-build evidence, but the unchanged script contains maintainer-environment assumptions and is excluded from the public candidate. A new-machine macOS replay remains pending.

## Current public scope and boundary

The public candidate does not contain the maintainer scripts above and has no validated cross-machine build command. An external reader can currently:

1. Read the `display_demo` source, hardware notes, and current verification status.
2. Understand the maintainer entries and evidence boundaries used to reproduce clean builds on Windows and macOS.
3. Read the complete flash workflow below and compare it with `open-flash` and the shared workflow contract.
4. Use the functional checklist to review whether later build, flash, and device evidence is complete.

This phase does not require an executable public flash method. This page does not compose direct `idf.py`, esptool, fixed-port, or maintainer-path commands. If a Skill is asked to flash while no public portable entry exists, it returns `S5_PORTABLE_ENTRY_PENDING`. A future final-S5 cross-machine entry would still require separate Windows and macOS validation and would not retroactively turn this reading preview into executed evidence.

## Complete flash workflow (reading and implementation comparison)

This is a workflow contract, not a copyable flash command:

1. **Lock the inputs**: identify the repository revision, `display_demo`, platform, and artifact set without inheriting hidden chat state.
2. **Check successful build evidence**: require a successful clean build for the same revision, Example, and platform, and record the artifact result; stop on build failure.
3. **Discover and explicitly select the port**: enumerate current candidates without using a historical port. No candidate blocks; multiple candidates require the user; one candidate still grants no write permission.
4. **Confirm the target device**: the user checks the port, board model, and board role.
5. **Disclose erase impact**: state whether the flow performs a full-chip erase and that it removes the existing program, configuration, and persistent data.
6. **Obtain device-write confirmation**: require confirmation for this device and this port immediately before writing.
7. **Run one complete write flow**: use the documented maintainer entry for the Example to perform full-chip erase, programming, and hash verification; this page intentionally exposes no command.
8. **Record results in layers**: report erase, each image/partition write, hash verification, and reset separately. On failure, stop at the first error without retrying, erasing again, switching ports, or changing tools.
9. **Reset and inspect boot logs**: confirm that the device exits download mode and reaches application startup; use reset/serial recovery for port or download-mode problems instead of blind reflashing.
10. **Perform functional device acceptance**: claim functional success only when the display, serial log, and stability checklist below all pass.

Implementation comparison:

- [`open-flash` Skill](../../skills/open-flash/SKILL.md): inputs, port selection, device confirmation, write gate, failure stop, and result fields.
- [Skill workflow contract](../../skills/references/workflow-contract.md): dependency propagation, confirmation gates, and the final-S5 boundary.
- [Skill status contract](../../skills/references/status-contract.md): meanings of `PASS`, `FAILED`, `BLOCKED`, and `NEEDS_USER`.
- [Development workflow](../development/workflow.en.md): explicit port, required full erase, flash verification, boot logs, and observable behavior.

## Functional acceptance checklist

Keep the three evidence layers separate:

| Evidence layer | Minimum record | Proves | Does not prove |
| --- | --- | --- | --- |
| Build evidence | Example, platform, clean-build result, artifact | Firmware was generated | Device write or functional success |
| Flash evidence | Target and explicit port, full erase, image writes, hash verification, reset | This artifact was written to this target | Display, logs, or long-run behavior |
| Functional device evidence | Boot logs, color order, hold duration, negative scan, human observation | Whether this observable behavior passed | Other Examples or untested behavior |

`display_demo` passes functional device acceptance only when all of the following hold:

1. LCD initialization succeeds.
2. The panel cycles through white, red, green, and blue, holding each color for about two seconds.
3. Serial logs emit `fill: white`, `fill: red`, `fill: green`, and `fill: blue` in the same order.
4. No panic, assert, watchdog, or unexpected reset occurs.

A build proves only that firmware was generated. Functional device success requires actual flashing plus the display and log observations above. Historical 2026-06-12 evidence covers only white and red; the current four-color clean build passed on 2026-08-31, while complete current-source device replay remains pending.

## Failure handling

- If the maintainer entry cannot find ESP-IDF, repair the same v5.5.4 environment recorded in `design.md`; do not switch to an unknown runner.
- If source changes are missing, repeat the complete mirror and clean-build entry.
- For a Windows non-ASCII path problem, use the established Git Bash mirror instead of substituting `Copy-Item`.
- If no port is found, inspect the cable, CH340E driver, USB permissions, and power.
- For a black screen, inspect board connection, power, and the first serial initialization error; do not use repeated flashing as hardware diagnosis.

See [Troubleshooting](../troubleshooting/README.en.md).

## Three reading paths

- Maintainer build reproduction: follow prerequisites and the Windows/macOS maintainer sections, then record only build evidence in the checklist.
- External reading preview: read the public boundary and complete flash workflow without treating the description as an available command.
- Functional acceptance review: inspect flash evidence, boot logs, and four-color behavior; a missing layer prevents a complete-pass claim.

See [Troubleshooting](../troubleshooting/README.en.md) when a step fails.
