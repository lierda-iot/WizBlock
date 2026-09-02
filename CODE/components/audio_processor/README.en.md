# audio_processor

[中文](README.md) | [English](README.en.md)

Purpose: ESP-SR/Opus audio processing wrapper.

## Metadata

- Classification: `reusable-component`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/audio_processor.h;include/audio_processor_task_policy.h;include/opus_codec.h
- Source files: audio_processor.c;audio_processor_policy.c;audio_processor_task_policy.c;opus_codec.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
