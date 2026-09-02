# io_expander

[中文](README.md) | [English](README.en.md)

Purpose: TPT29555A IO-expander adapter.

## Metadata

- Classification: `reusable-component`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/io_expander.h
- Source files: io_expander.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
