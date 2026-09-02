# board_adc

[中文](README.md) | [English](README.en.md)

Purpose: Board ADC access and calibration helpers.

## Metadata

- Classification: `board-specific`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/board_adc.h
- Source files: board_adc.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
