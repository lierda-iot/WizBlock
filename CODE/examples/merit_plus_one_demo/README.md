<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# merit_plus_one_demo

`merit_plus_one_demo` 是”功德+1”敲击反馈功能的独立归档 Demo（EX-032）。

该功能曾集成到 `xiaozhi_companion_robot_demo`，经过多轮目标板测试后确认：壳体敲击与随机电机运动产生的 BMI260 瞬态振动无法稳定区分，不适合作为小智陪伴机器人的正式功能。因此，相关需求、设计、审核图、生成脚本、音效、纯 C 判定源码和最后一版集成源码快照迁移到本目录，供后续在静止设备或其他硬件形态中继续开发。

## 当前状态

- 归档完成，暂停开发。
- 7 张 `320x240` `icebox` 背景审核图已通过人工视觉评审，尺寸已逐项核对。
- `companion_merit_tap` 保存了最后一轮纯 C 判定实现。
- `spiffs/merit_tap.wav` 是未完成人工音效审核的阶段资源。
- 本目录当前不是可独立构建或烧录的 ESP-IDF 工程。
- 不再与 `xiaozhi_companion_robot_demo` 共享源码或构建依赖。

## 目录

- `assets/effects/merit_tap/`：7 张审核图及图像/WAV/运行资源生成脚本。
- `assets/expressions/concepts/icebox/`：效果图生成所需的原始背景。
- `components/companion_merit_tap/`：无硬件依赖的纯 C 敲击判定组件。
- `components/companion_expression/generated/`：由审核图生成的阶段运行资源。
- `spiffs/merit_tap.wav`：阶段木鱼提示音。
- `archive/xiaozhi_companion_robot_integration_snapshot/`：从 EX-024 移除前的相关集成源码完整快照，仅用于参考，不参与构建。
- 快照内的 `tests/` 保存迁移前阶段纯 C/集成测试入口；快照和测试均不参与 EX-024 或 EX-032 的任何构建、烧录或默认运行。
- `docs/requirements.md`：从 EX-024 迁出的原需求与验收口径。
- `docs/design.md`：从 EX-024 迁出的设计、边界、风险和恢复入口。

## 恢复开发门禁

重新开发前应先确定目标设备是否存在持续机械运动。若存在电机、风扇或其他结构振动，必须先证明传感器数据具有可分离特征，再决定是否继续使用 BMI260；不得直接把本归档阈值作为产品参数。
