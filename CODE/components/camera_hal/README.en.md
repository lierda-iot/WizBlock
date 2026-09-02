# camera_hal

[中文](README.md) | [English](README.en.md)

Purpose: Camera capability abstraction.

## Metadata

- Classification: `reusable-component`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/camera_hal.h;include/camera_hal_policy.h
- Source files: camera_hal.c;camera_hal_policy.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
