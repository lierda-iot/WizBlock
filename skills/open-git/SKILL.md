---
name: open-git
description: Check Git availability for the OPEN development flow and, when explicitly confirmed, guide installation or minimal configuration on Windows or macOS. Use for Git setup, not for arbitrary repository history manipulation.
---

# OPEN Git

This Skill is independently callable. It does not imply authorization for clone, pull, reset, clean, rebase, force operations, commits, or pushes.

## Inputs

- `language` and `platform`.
- Whether a usable local repository already exists.
- The exact requested Git task, if any.

## Workflow

1. Check Git availability and version with a read-only version query.
2. If Git is present, return its fact and do not modify global configuration by default.
3. If Git is absent, show the official installation source, network use, destination scope, and expected system changes; obtain confirmation before acting.
4. Configure only a specifically requested minimal setting after showing its scope and value.
5. Do not use broad repair operations to resolve repository conflicts. Route repository acquisition to `open-clone`.
6. If Git is unavailable but a local repository already exists, report which independent local steps remain possible; never claim update capability.

## Permissions

Version discovery is read-only. Downloads, installers, package-manager actions, and configuration writes require explicit confirmation immediately before execution.

## Result

Return [the status contract](../references/status-contract.md). A `PASS` result proves Git availability only. Use [the bilingual output template](../references/output-templates.md), and describe actual filesystem/network side effects.
