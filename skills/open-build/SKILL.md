---
name: open-build
description: Validate prerequisites and perform a clean build for one OPEN Example only through a public, platform-validated entry point. Use for build requests and artifact/capacity summaries; currently report the final-S5 portability blocker when that entry is absent.
---

# OPEN Build

Run independently with explicit repository, platform, Example, IDF, and dependency results.

## Inputs

- `language`, `platform`, repository root, Example, clean-build choice, and output location.
- Evidence that ESP-IDF `v5.5.4` and required components are ready.
- Whether a public portable entry for this platform has completed final S5 validation.

## Workflow

1. Confirm the Example against `CODE/examples/examples.yml` and its README.
2. Validate repository, IDF, components, disk, and writable output prerequisites.
3. Show the exact public entry, clean behavior, output directory, and expected writes before execution.
4. If no final-S5 validated public entry exists, return `BLOCKED` with `S5_PORTABLE_ENTRY_PENDING`. Do not call private maintainer scripts or assemble a direct alternative command.
5. When the entry exists, run exactly that documented entry with a 200-second automation wait window for clean build unless a later approved design changes it.
6. Preserve the first actionable build error; do not retry with a different runner, toolchain, mirror strategy, or Example.
7. On success, report command identity, platform, Example, IDF version, completion marker, artifact paths, sizes, partition capacity, and limitations.

## Boundaries

Build writes files but never writes a device. A successful build is not flash or functional hardware verification. Existing local build/flash scripts remain read-only until final S5.

## Result

Return [the status contract](../references/status-contract.md) and [output template](../references/output-templates.md). Record filesystem writes only when the build actually ran.
