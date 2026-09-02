# bringup_test

[中文](README.md) | [English](README.en.md)

Purpose: Board-wide bring-up test coordinator.

## Metadata

- Classification: `board-specific`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/bringup_test.h
- Source files: bringup_test.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 1.
- The root license is pending owner approval; this candidate documentation is not a license grant.
