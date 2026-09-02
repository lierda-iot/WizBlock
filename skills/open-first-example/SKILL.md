---
name: open-first-example
description: Select and explain an OPEN repository Example from the machine-readable manifest, defaulting to display_demo. Use to establish hardware prerequisites, verification status, and the correct README before build.
---

# OPEN First Example

This Skill is read-only and independently callable.

## Inputs

- `language`, repository root, and optional Example name.
- Optional available-hardware facts.

## Workflow

1. Read `CODE/examples/examples.yml` as the machine-readable inventory and cross-link the matching Example README.
2. Default to `display_demo` only when the user did not explicitly request another Example.
3. Report public status, target board/chip, verified IDF, dependencies, required hardware, current evidence, and known limitations.
4. Distinguish source existence, host tests, clean build, flash, and functional hardware verification.
5. Return `NEEDS_USER` when required hardware or an explicitly requested Example is ambiguous.
6. Return `BLOCKED` for held, archived, unavailable, or missing Example source; do not silently substitute another Example.

`display_demo` currently means white, red, green, and blue full-screen output in that order, two seconds per color. Its current source has clean-build evidence, while complete four-color hardware verification remains pending.

## Result

Return [the status contract](../references/status-contract.md) with no side effects and the selected-language [output template](../references/output-templates.md). `PASS` selects and explains an Example; it does not build it.
