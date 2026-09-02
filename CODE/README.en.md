# CODE Source Tree and Example Builds

[中文](README.md) | [English](README.en.md)

`CODE/` contains the ESP-IDF main project, shared components, and independent Examples. The target is ESP32-S3, and ESP-IDF v5.5.4 is the only currently validated version statement.

## Layout

| Path | Responsibility |
| --- | --- |
| `main/` | Main private-source project; not the first public Quick Start |
| `components/` | Reusable, board-specific, and binary adapters; see the [component index](components/README.en.md) |
| `examples/` | Independent ESP-IDF projects; see the [Example index](examples/README.en.md) |
| `tools/` | Existing private-environment tools; read-only and excluded from the public candidate in this stage |
| `boards/laiwfs300/` | Historical draft; `components/laiwfs300/` is the current board source and the draft is excluded |

## Prerequisites

- Activate ESP-IDF v5.5.4 and verify that `idf.py --version` works.
- Use a short ASCII checkout path, or mirror the tree into a temporary ASCII path.
- Build Examples serially because the current mirror location is shared.
- First dependency resolution requires network access. Offline and lock-file policy is deferred to final S5.

## Windows: currently validated maintainer entry

First perform the complete design-4.1.2 mirror from the private source workspace root in Git Bash:

```bash
rm -rf "$TEMP/laiwfs300_build/CODE"
mkdir -p "$TEMP/laiwfs300_build"
cp -r "./CODE" "$TEMP/laiwfs300_build/CODE"
```

Then invoke the existing script with a repository-relative path from PowerShell at the workspace root, replacing `<example>` with the directory name:

```powershell
& "./CODE/tools/build_example.ps1" -Example <example> -Clean
```

The 2026-08-31 `display_demo` replay used this entry, exited zero in 170.4 seconds, and completed `1421/1421`. The outer `build_example.sh` wrapper produced the complete firmware but did not return inside the 200-second caller window, so it is not a passing entry for this round. The PowerShell script still contains maintainer-specific configuration, remains unchanged, and is excluded from the public candidate.

## macOS: currently validated entry

Run from the private source workspace root:

```bash
cd ./CODE
bash ./tools/build_example_macos.sh <example> clean
```

This is the `design.md` 4.1.3 entry. The unchanged script still assumes maintainer-specific user, Python, certificate, and IDF paths and is excluded from the public candidate.

## Public-candidate boundary

The scripts above are not present in `OPEN_REPOSITORY/`, so the candidate currently has no validated cross-machine build/flash/monitor command. Direct `idf.py fullclean/build` calls have no public-candidate evidence in this stage and are not presented as an alternative. Final S5 will add a parameterized entry, explicit port selection, and clean-machine Windows/macOS replay. A successful build remains separate from functional hardware validation.

## Configuration

- Publish reviewed `sdkconfig.defaults`; exclude generated `sdkconfig` and `sdkconfig.old`.
- Never embed real SSIDs, passwords, tokens, certificates, or device identifiers.
- Managed dependencies come from `idf_component.yml`.
- The reproducible `dependencies.lock` policy is deferred to final S5.
