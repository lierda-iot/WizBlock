---
name: open-idf
description: Discover, validate, install, or activate the OPEN repository's verified ESP-IDF v5.5.4 environment on Windows or macOS. Use for IDF setup or version mismatch; do not invent another supported version.
---

# OPEN ESP-IDF

Run independently with explicit platform and repository facts.

## Inputs

- `language`, `platform`, and requested IDF version.
- Optional existing activation evidence and repository root.

## Workflow

1. Detect whether ESP-IDF is currently activated and capture its reported version and target capability.
2. Accept `v5.5.4` as the first-release verified version. Treat another version as a visible mismatch, not as compatible by assumption.
3. Reuse an existing verified installation when possible; do not scan or reveal user-home paths in output.
4. If installation or repair is required, use only the current public platform documentation and official source. Show network, disk, environment, and shell effects before requesting confirmation.
5. Activate the selected installation only after its path and version are confirmed; do not hardcode a maintainer location.
6. Re-run version and target checks after a user-approved change.
7. If activation or version validation fails, return `FAILED`; components/build/flash become `BLOCKED`.

## S5 boundary

IDF readiness does not authorize a direct `idf.py` build command. Build routing remains owned by `open-build` and the final-S5 portable entry.

## Result

Return [the status contract](../references/status-contract.md), [platform facts](../references/platform-contract.md), and a bilingual summary from [the output template](../references/output-templates.md).
