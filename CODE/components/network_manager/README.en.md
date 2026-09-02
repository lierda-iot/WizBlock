# network_manager

[中文](README.md) | [English](README.en.md)

Purpose: Unified Wi-Fi/CAT1 policy and state manager.

## Metadata

- Classification: `reusable-component`
- Target: ESP32-S3; validated with ESP-IDF v5.5.4
- Public headers: include/network_manager.h;tests/idf_project/components/lte_hal/include/lte_hal.h;tests/idf_project/components/net_mgmt/include/lsd_net_mgmt.h
- Source files: network_manager.c;network_manager_cellular_runtime_model.c;network_manager_dual_runtime_model.c;network_manager_event_journal.c;network_manager_policy.c;network_manager_storage_model.c;network_manager_storage_nvs.c;network_manager_wifi_config.c;network_manager_wifi_runtime_model.c;network_manager_wifi_scan.c

## Contract boundary

Initialization, teardown, task/ISR context, and errors follow the public headers and current callers. Undocumented behavior is not a stable API. Board-specific parameters must enter through configuration or the `laiwfs300` adapter.

## Verification and limitations

- Host-test file count: 57.
- The root license is pending owner approval; this candidate documentation is not a license grant.
