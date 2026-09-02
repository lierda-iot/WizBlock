# net_mgmt

[中文](README.md) | [English](README.en.md)

Purpose: Self-developed binary network adapter.

## Metadata

- Classification: `binary-adapter`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/lsd_net_mgmt.h
- Source files: None

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
