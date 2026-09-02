# Example Index

[中文](README.md) | [English](README.en.md)

> Generated from the S0 audit in `examples.yml`. Candidate status does not mean that licensing or release gates are complete.

## Included in the local public candidate

| ID | Directory | Public status | Verification | Main limitation |
| --- | --- | --- | --- | --- |
| EX-001 | [display_demo](display_demo/README.md) | publish-with-limitations | 构建通过，当前四色待实机复核 | Current four-color source awaits device replay |
| EX-002 | [motor_demo](motor_demo/README.md) | publish | 已验证 | Requires a supervised test area |
| EX-003 | [touch_demo](touch_demo/README.md) | publish | 已验证 | Single-touch path |
| EX-004 | [rtc_demo](rtc_demo/README.md) | publish | 已验证 | RTC hardware required |
| EX-005 | [imu_6axis_demo](imu_6axis_demo/README.md) | publish-with-limitations | 构建通过 | Extended UI awaits device validation |
| EX-006 | [led_demo](led_demo/README.md) | publish | 已验证 | 500 ms color cycle |
| EX-007 | [audio_demo](audio_demo/README.md) | publish-with-limitations | 待复核 | Current audio quality and TDM handling require review |
| EX-008 | [audio_ns_demo](audio_ns_demo/README.md) | publish-with-limitations | 部分实机验证 | Audio quality and repeat-run behavior remain unverified |
| EX-009 | [audio_opus_demo](audio_opus_demo/README.md) | publish-with-limitations | 已验证 | Managed ESP-Opus dependency |
| EX-010 | [audio_aec_demo](audio_aec_demo/README.md) | publish-with-limitations | 当前回归未通过 | Current source disables active AEC and must not be presented as an AEC pass |
| EX-011 | [audio_aec_test](audio_aec_test/README.md) | publish-with-limitations | 主机测试与构建通过 | New-speaker device validation is pending |
| EX-012 | [audio_ns_mic2_demo](audio_ns_mic2_demo/README.md) | publish-with-limitations | 构建通过 | Requires MIC2 on TDM slot 2 |
| EX-013 | [audio_mic2_debug_demo](audio_mic2_debug_demo/README.md) | publish-with-limitations | 已验证 | Diagnostic example |
| EX-014 | [audio_dual_mic_doa_demo](audio_dual_mic_doa_demo/README.md) | publish-with-limitations | 部分实机验证 | Angle range and soft-speech behavior remain unverified |
| EX-015 | [korvo2_mic_test](korvo2_mic_test/README.md) | publish-with-limitations | 已验证 | Targets Korvo-2 rather than L-AIWFS300 |
| EX-017 | [lvgl_demo](lvgl_demo/README.md) | publish | 已验证 | LVGL v8.4.0 |
| EX-018 | [camera_display_demo](camera_display_demo/README.md) | publish | 已验证 | Camera hardware required |
| EX-019 | [module_hotplug_demo](module_hotplug_demo/README.md) | publish-with-limitations | 已验证 | Only documented supported module paths are covered |
| EX-023 | [audio_spatial_spectrum_demo](audio_spatial_spectrum_demo/README.md) | publish-with-limitations | 已验证 | Internal values are dBFS, not calibrated dB SPL |
| EX-025 | [integrated_demo](integrated_demo/README.md) | publish-with-limitations | 源码存在待回归 | Full integration regression is pending |
| EX-026 | [camera_face_detect_demo](camera_face_detect_demo/README.md) | publish-with-limitations | 已验证 | ESP-DL dependency and notices require final review |
| EX-029 | [touch_2048_demo](touch_2048_demo/README.md) | publish-with-limitations | 开发中 | MIT upstream notices must remain traceable |

## Held, archived, or unavailable

| ID | Directory | Status | Reason |
| --- | --- | --- | --- |
| EX-016 | `lte_net_demo` | hold | Held for credential sanitization and net_mgmt licensing |
| EX-020 | `xiaozhi_ai_demo` | hold | Held for credentials, service configuration, and binary dependency review |
| EX-021 | `salary_calculator_demo` | hold | Embedded image/audio licensing is pending |
| EX-022 | `tf_firmware_launcher_demo` | hold | launcher_return is missing; firmware/media assets are not approved |
| EX-024 | `xiaozhi_companion_robot_demo` | hold | Held for credentials, assets, service boundary, and licensing review |
| EX-027 | `camera_face_servo_expression_demo` | unavailable | Not present in the current workspace |
| EX-028 | `codex_task_notifier_demo` | hold | Held for configuration, host integration, and audio asset review |
| EX-030 | `mp3_demo` | hold | Media inputs and full hardware behavior remain unverified |
| EX-031 | `audio_ofdm_text_demo` | hold | Audio asset/source review and new profile validation are pending |
| EX-032 | `merit_plus_one_demo` | archive | Not independently buildable or flashable |
| EX-033 | `audio_aec_ns_compare_demo` | unavailable | Formal entry exists but the directory is absent |
| EX-034 | `holocubic_demo` | hold | Large image/font asset set requires licensing review |
| EX-035 | `rc_tank_demo` | hold | Held for credential sanitization, history removal, and remaining validation |

## Build entry

See [CODE/README.en.md](../README.en.md). Existing local build scripts are not modified or unified in this stage.
