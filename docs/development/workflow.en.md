# Development Workflow

[中文](workflow.md) | [English](workflow.en.md)

1. Select an existing, candidate-eligible Example from `CODE/examples/examples.yml`.
2. Read its README, `main/CMakeLists.txt`, manifest, Kconfig, defaults, partition table, and tests.
3. Keep changes inside the smallest responsibility boundary and assess every caller before changing a shared component.
4. Add executable tests first for new behavior, public interfaces, high-risk logic, and regressions.
5. Repeat the complete mirror and clean build after source changes.
6. Validate hardware through explicit port selection, required full erase, verified flash, boot logs, and observable behavior.
7. Record host test, clean build, flash, and functional hardware evidence separately.
8. Synchronize README/manifest/public docs without exporting internal history.

The public repository URL, default branch, and PR/CI policy are pending D-14/S9 and are not invented here.

