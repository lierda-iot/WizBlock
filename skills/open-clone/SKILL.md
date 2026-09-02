---
name: open-clone
description: Acquire or locate the OPEN public repository on Windows or macOS with an explicit remote and destination. Use for initial clone or a specifically requested safe update; do not perform destructive repository repair.
---

# OPEN Clone

This Skill is independently callable and treats remote access and local writes as explicit side effects.

## Inputs

- `language`, platform, exact repository URL, and destination directory.
- Whether the task is initial clone, locate-only, or a specifically requested update.

## Workflow

1. Validate that the remote and destination are explicit; return `NEEDS_USER` on ambiguity.
2. For locate-only, confirm repository markers without contacting a remote.
3. Before clone, show remote, destination, network access, and created files; obtain confirmation.
4. Refuse to overwrite a non-empty destination or merge unrelated content automatically.
5. For update, state the exact allowed operation and inspect conflicts without reset, clean, rebase, force, or branch deletion.
6. After success, report the resolved repository root and public markers. Do not expose credentials embedded in a remote URL.
7. If network is unavailable and no local repository exists, return `BLOCKED`; if a valid repository exists, preserve it as a local fact without claiming freshness.

## Result

Return [the status contract](../references/status-contract.md). `PASS` means the requested locate/clone/update action completed, with actual network/filesystem effects recorded. Use [the output template](../references/output-templates.md).
