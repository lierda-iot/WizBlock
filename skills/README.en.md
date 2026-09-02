# OPEN Development Skills

[中文](README.md) | [English](README.en.md)

This directory provides one full-flow orchestrator and 11 independently invocable child Skills. They target Windows and macOS, produce Chinese or English output, and make no Linux support promise.

## Entry points

| Skill | Purpose |
| --- | --- |
| [`open-dev-all`](open-dev-all/SKILL.md) | Dependency-aware orchestration from preflight through diagnosis |
| [`open-preflight`](open-preflight/SKILL.md) | Collect platform, architecture, shell, disk, serial, Git, and IDF facts |
| [`open-driver`](open-driver/SKILL.md) | Determine whether CH340E enumerates and driver action is needed |
| [`open-git`](open-git/SKILL.md) | Check Git and confirm before installation or configuration |
| [`open-idf`](open-idf/SKILL.md) | Discover, install, or activate ESP-IDF v5.5.4 |
| [`open-clone`](open-clone/SKILL.md) | Obtain the public repository and identify its local root |
| [`open-components`](open-components/SKILL.md) | Prepare Component Manager dependencies and lock state |
| [`open-first-example`](open-first-example/SKILL.md) | Select and explain the first Example, defaulting to `display_demo` |
| [`open-build`](open-build/SKILL.md) | Run or assess clean build, artifacts, and capacity |
| [`open-flash`](open-flash/SKILL.md) | Flash only after explicit port, device, and write confirmation |
| [`open-monitor`](open-monitor/SKILL.md) | Monitor serial output, redact logs, and check health markers |
| [`open-diagnose`](open-diagnose/SKILL.md) | Isolate the first environment, dependency, build, port, flash, or runtime issue |

## Shared contracts

- [Status contract](references/status-contract.md): meanings and JSON shape for `PASS`, `SKIPPED`, `FAILED`, `BLOCKED`, and `NEEDS_USER`.
- [Workflow contract](references/workflow-contract.md): dependencies, failure propagation, confirmation gates, and the final-S5 boundary.
- [Platform contract](references/platform-contract.md): Windows/macOS discovery, permissions, and portability requirements.
- [Output templates](references/output-templates.md): human-readable Chinese and English result templates.

`scripts/open_skill_runtime.py` is a stateless public state engine. It handles result contracts, blocking propagation, flash readiness, and redaction only; it does not install, build, or write devices. Until final S5 is complete, `open-build`, `open-flash`, and dependent automation must retain the `S5_PORTABLE_ENTRY_PENDING` blocker instead of inventing a new build method.

## Minimal invocation

When the client has discovered these Skills, invoke them directly by name:

```text
$open-dev-all Inspect the current Windows or macOS development environment for the default display_demo. Start read-only: do not install, use the network, build, or flash.
$open-first-example Explain display_demo hardware requirements, evidence, and limitations.
$open-build Check display_demo build prerequisites and return the final-S5 blocker when no portable entry exists.
$open-diagnose Diagnose the first actionable problem from the previous failed result without applying a fix.
```

If repository-local Skills are not auto-discovered, state the entry explicitly, for example: “Read `skills/open-dev-all/SKILL.md` and process this repository with `action=setup`.” The orchestrator defaults to `display_demo`; select a child Skill only for an isolated stage. Ports, installation, network access, builds, saved logs, and device writes still require their immediate confirmation gates.

Normal use does not require handwritten JSON or direct state-engine commands. One `$open-dev-all ...` sentence starts the orchestrated flow, while one `$open-build ...`-style sentence runs an isolated stage. Each Skill's `SKILL.md` is its input, workflow, and result usage document; this README is the index and shortest invocation guide.

## Validation

Run from the repository root:

```text
python -m unittest discover -s ./skills/tests -p "test_*.py"
python ./skills/scripts/validate_skill_tree.py
python ./skills/scripts/run_skill_validation.py --workspace ../skill-validation-output --live-repository .
```

The third command generates four deterministic orchestrator fixtures for Windows/macOS × empty/existing repository, one read-only marker inspection of the current public snapshot, an orchestrator package gate, a package/result simulation matrix for all 11 independently callable child Skills on Windows and macOS, eight failure/safety flow scenarios, and three Quick Start reading-role simulations. The three roles validate maintainer build reproduction, the external reading preview, and the functional acceptance checklist. The public Quick Start must contain a complete workflow that can be compared with `open-flash`, while exposing no executable flash command. The eight failure scenarios cover dependency propagation after IDF, component, build, and flash failures, plus multiple ports, no device, missing device-write confirmation, and the final-S5 portable-entry boundary. The child coverage gate also rejects a newly added Skill that has not been added to the matrix.

The unit suite additionally covers paths containing spaces and Unicode, a missing public snapshot, refusal to overwrite an existing validation directory, a missing orchestrator package, and an unvalidated child Skill. macOS is simulated coverage, not a claim of native macOS execution. The validator does not run Git, access the network, build, or write devices. Any package, contract, scenario, or coverage-gate failure makes the overall validation `FAILED` and the CLI exits non-zero, preventing that run from being treated as passed.

Each result preserves both the raw workflow `aggregate_status` and an independent `validation_status`. Expected `FAILED/BLOCKED` outcomes in an empty environment and the final-S5 `BLOCKED` boundary may still produce a platform validation `PASS` only when the observed workflow exactly matches the scenario expectation; blocked steps are never rewritten as successful execution.

Actual installation, network access, filesystem writes, repository mutation, and device writes remain subject to the relevant Skill's confirmation gate immediately before the action.
