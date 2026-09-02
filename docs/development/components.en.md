# Component Boundaries

[中文](components.md) | [English](components.en.md)

Reusable components must not hard-code L-AIWFS300 GPIOs, thresholds, or product wording. Board-specific facts belong in `laiwfs300` and board adapters. Example-private modules must not be imported through another Example's `main`. The self-developed binary `net_mgmt` adapter remains held until S8 resolves license, ABI, version, and source/binary delivery form.

Each public component README should cover purpose, headers, lifecycle, task/ISR context, configuration, errors, dependencies, board coupling, tests, and license status. See the [component index](../../CODE/components/README.en.md).

