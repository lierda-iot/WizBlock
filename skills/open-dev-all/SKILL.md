---
name: open-dev-all
description: Orchestrate the OPEN repository onboarding flow on Windows or macOS from environment discovery through build, optional flash, monitor, and diagnosis. Use for an end-to-end guided setup; use a child Skill for one isolated stage.
---

# OPEN Dev All

Coordinate child Skills without duplicating their commands or carrying hidden success state between them.

## Inputs

- `language`: `zh-CN` or `en`; ask when it cannot be inferred from an explicit user choice.
- `platform`: `windows` or `macos`; detect and display it when omitted.
- `example`: default `display_demo`.
- `action`: `setup`, `build`, `flash`, `monitor`, `all`, or `diagnose`.
- `port`: required for flash/monitor; discovery may produce candidates but never a guessed selection.
- `idf_version`: first release accepts only `v5.5.4` as verified.
- `continue_policy`: dependency-aware only; a critical failure cannot be forced to success.

## Workflow

1. Find the repository root from public markers and state all resolved inputs.
2. Read [the workflow contract](../references/workflow-contract.md) and choose only the steps needed by `action`.
3. Invoke each selected child Skill with explicit inputs and the prior result object when relevant.
4. Pass observed results and explicit context through `../scripts/open_skill_runtime.py resolve`; preserve any explicit child result over a derived result.
5. Stop dependent work on `FAILED`, `BLOCKED`, or `NEEDS_USER`; continue only independent steps allowed by the workflow contract.
6. Run `open-diagnose` after a real failure or when the user requests diagnosis.
7. Return one aggregate summary plus every child result that was attempted, skipped, blocked, or needs input.

## Action routing

- `setup`: preflight, driver, Git, IDF, clone, and components as actually needed.
- `build`: preflight, repository/IDF/components checks, first Example, then build.
- `flash`: reuse explicit successful build evidence, then run flash readiness and flash only after confirmation.
- `monitor`: identify one explicit port, monitor, redact, and summarize.
- `all`: full dependency sequence; device write remains a separate confirmation gate.
- `diagnose`: call only `open-diagnose` with the failed stage and first error evidence.

## Safety and current boundary

- Do not install, download, configure, create a repository, save logs, or write a device without the relevant immediate confirmation.
- Final S5 has not yet supplied validated public portable build/flash/monitor entry points. Return `S5_PORTABLE_ENTRY_PENDING` where the flow reaches that boundary; do not synthesize a replacement command.
- Never convert a build result into hardware functional success.
- Redact credentials, full device identifiers, and user-home paths before showing or saving evidence.

## Result

Use the complete [status contract](../references/status-contract.md) and the selected-language [output template](../references/output-templates.md). Aggregate status is the most restrictive unresolved child status, with `FAILED` taking precedence over `BLOCKED`, `NEEDS_USER`, `SKIPPED`, and `PASS`.
