# Developer Documentation

[中文](README.md) | [English](README.en.md)

This tree is for developers who do not depend on an AI Skill. Commands use repository-relative paths or explicit placeholders, and unknown/unverified facts are labelled accordingly.

For AI orchestration, use the [OPEN development Skills](../skills/README.en.md). Manual documentation and Skills share the same facts and final-S5 boundary.

| Entry | Purpose |
| --- | --- |
| [Quick Start](quick-start/README.en.md) | Environment checks through `display_demo` build, flash, and observation |
| [Windows setup](setup/windows/README.en.md) | CH340E, Git, ESP-IDF v5.5.4, mirroring, and ports |
| [macOS setup](setup/macos/README.en.md) | CH340E, ESP-IDF, permissions, mirroring, and ports |
| [Hardware](hardware/README.en.md) | Public board facts, connections, and safety |
| [Development workflow](development/workflow.en.md) | Example selection, focused changes, validation, and docs |
| [Components](development/components.en.md) | Reusable, board, Example-private, and binary boundaries |
| [Testing](development/testing.en.md) | Host, clean-build, flash, and hardware evidence layers |
| [Examples](examples/README.en.md) | Public status and detailed entries |
| [Security](security/README.en.md) | Credentials, logs, device IDs, and diagnostic bundles |
| [Troubleshooting](troubleshooting/README.en.md) | Environment, dependencies, builds, ports, boot, and hardware |
| [Release candidate](release/README.en.md) | Allowlist, licensing, security, verification, and human gates |

There is no portable unified build wrapper yet. The documented procedure preserves the validated complete-mirror, short-ASCII-path, serial clean-build constraints. Clean-machine replay on both public platforms is still pending.
