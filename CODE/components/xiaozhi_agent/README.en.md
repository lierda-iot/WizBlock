# xiaozhi_agent

[中文](README.md) | [English](README.en.md)

Purpose: Xiaozhi protocol/session adapter.

## Metadata

- Classification: `reusable-component`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/xiaozhi_agent.h;include/xiaozhi_agent_listen_mode_policy.h;include/xiaozhi_agent_tts_barrier_policy.h;include/xiaozhi_agent_vad_stop_policy.h;include/xiaozhi_agent_ws_start_policy.h
- Source files: xiaozhi_agent.c;xiaozhi_agent_listen_mode_policy.c;xiaozhi_agent_tts_barrier_policy.c;xiaozhi_agent_vad_stop_policy.c;xiaozhi_agent_ws_start_policy.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 0.
- The root license is pending owner approval; this candidate documentation is not a license grant.
