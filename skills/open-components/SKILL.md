---
name: open-components
description: Prepare ESP-IDF Component Manager dependencies for an OPEN Example from its public manifests and lock state. Use after repository and IDF readiness, including cached/offline assessment and dependency-failure evidence.
---

# OPEN Components

Run independently with an explicit repository root, Example, platform, and IDF result.

## Inputs

- `language`, repository root, Example, platform, and verified IDF version.
- Whether network access is available and whether a compatible dependency cache/lock already exists.

## Workflow

1. Locate only public `idf_component.yml`, project manifests, defaults, and dependency locks relevant to the selected Example.
2. Verify that IDF `v5.5.4` is ready; otherwise return `BLOCKED`.
3. Explain resolved local/cache state before downloading anything.
4. If downloads are required, show manifest source, network use, and write locations, then request confirmation.
5. Do not delete a cache or lock file to recover from a failure. Preserve the first dependency error and classify network, registry, version, checksum, or local-path causes.
6. When offline, reuse only evidence-confirmed compatible inputs. Missing downloads return `BLOCKED`, not a fabricated offline success.
7. Return the lock/dependency evidence needed by `open-build` without exposing credentials or user paths.

## Dependency behavior

`FAILED` or `BLOCKED` components results block build and flash. A successful manifest parse alone is not equivalent to all dependency content being available.

## Result

Return [the status contract](../references/status-contract.md) and selected-language [output template](../references/output-templates.md), including actual network/filesystem side effects.
