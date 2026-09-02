---
name: open-preflight
description: Collect read-only Windows or macOS host facts needed by the OPEN development flow, including platform, architecture, shell, disk, repository, serial candidates, Git, and ESP-IDF. Use before setup or when environment facts are uncertain.
---

# OPEN Preflight

This Skill is independently callable and performs discovery only.

## Inputs

- `language`: `zh-CN` or `en`.
- Optional repository path and Example name; default Example is `display_demo`.

## Workflow

1. Detect the actual platform, version, architecture, and current shell. Reject unsupported platforms instead of treating them as macOS.
2. Resolve a repository root only from public markers such as root README, `CODE/`, `docs/`, and `skills/`; report ambiguity as `NEEDS_USER`.
3. Report free disk space and whether the intended workspace is readable/writable without creating probe files.
4. Discover Git and ESP-IDF availability/version without installing or activating anything.
5. Discover serial candidates using stable, redacted labels; do not select a device.
6. Confirm whether the requested Example exists and link its README.
7. Return missing facts separately from blocking prerequisites.

Follow the [platform contract](../references/platform-contract.md). Do not print usernames, user-home paths, credentials, or full device identifiers.

## Permissions

All actions are read-only. If a discovery method would require elevated permissions, installation, network access, or a filesystem write, return `NEEDS_USER` and describe that proposed action rather than performing it.

## Result

Return the [status contract](../references/status-contract.md). `PASS` means the environment facts were collected, not that setup/build/flash succeeded. Use [the bilingual output template](../references/output-templates.md).
