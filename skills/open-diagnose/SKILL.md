---
name: open-diagnose
description: Diagnose the first actionable OPEN development failure across environment, dependency, build, port, flash, startup, or behavior categories. Use after a failed Skill or when observed behavior conflicts with public documentation.
---

# OPEN Diagnose

Run independently from explicit failure evidence; do not require orchestrator history.

## Inputs

- `language`, platform, failed Skill/stage, selected Example, expected result, and first redacted error or observation.
- The exact actions and side effects already attempted.

## Workflow

1. Reconstruct only the minimum known facts from explicit inputs and current public docs.
2. Classify the first failure as platform, permissions, driver/port, Git/network, IDF/version, components, build, flash, startup, or functional behavior.
3. Separate the observed fact, expected contract, mismatch, leading hypothesis, and evidence still needed.
4. Choose the smallest read-only diagnostic that can distinguish the leading hypotheses.
5. Do not mutate configuration, reinstall, update repositories, switch toolchains, erase, reflash, or retry a failed device write without a new explicit request and confirmation.
6. Preserve known limitations such as the final-S5 portability blocker and current `display_demo` hardware-verification gap.
7. Redact all evidence before output or optional saved diagnostics.
8. Return one root-cause candidate only when evidence supports it; otherwise return `NEEDS_USER` with the smallest requested evidence.

## Result

Return [the status contract](../references/status-contract.md) and [output template](../references/output-templates.md). `PASS` means the diagnostic question was resolved, not that the underlying failed operation was repaired.
