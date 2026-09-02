# laiwfs300

[中文](README.md) | [English](README.en.md)

Purpose: L-AIWFS300 board-support component.

## Metadata

- Classification: `board-specific`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/board_laiwfs300.h;include/board_module_map.h;include/board_pins.h;include/board_power_map.h
- Source files: board_audio.c;board_camera.c;board_display.c;board_laiwfs300.c;board_motor.c;board_touch.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
