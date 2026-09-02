# xiaozhi_agent

[中文](README.md) | [English](README.en.md)

用途：Xiaozhi protocol/session adapter。

## 元数据

- 分类：`reusable-component`
- 目标：ESP32-S3；已验证 ESP-IDF v5.5.4
- 公共头文件：include/xiaozhi_agent.h;include/xiaozhi_agent_listen_mode_policy.h;include/xiaozhi_agent_tts_barrier_policy.h;include/xiaozhi_agent_vad_stop_policy.h;include/xiaozhi_agent_ws_start_policy.h
- 实现文件：xiaozhi_agent.c;xiaozhi_agent_listen_mode_policy.c;xiaozhi_agent_tts_barrier_policy.c;xiaozhi_agent_vad_stop_policy.c;xiaozhi_agent_ws_start_policy.c

## 使用边界

初始化、反初始化、线程/中断上下文和错误返回以公共头文件契约及实际调用方为准；尚未写入头文件的行为不作稳定接口承诺。板级参数必须通过配置或 `laiwfs300` 适配层进入，通用组件不得写死业务文案。

## 验证与限制

- 主机测试文件数：0。
- 当前根许可证仍待权利人决定；本文件只说明本地公开候选边界，不构成正式许可证授予。
