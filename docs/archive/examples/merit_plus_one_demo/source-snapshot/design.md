# 功德+1 Demo 归档设计

状态：设计与阶段实现已迁出 EX-024；EX-032 暂停开发。

## 设计结论

功能采用“传感器采样 -> 纯 C 判定 -> 业务事件 -> 气泡 overlay / Prompt 音效”的分层结构。传感器所有权、业务状态、显示和音频必须保持解耦；纯 C 判定组件不得直接访问 I2C、FreeRTOS、LVGL、电机或产品状态。

原方案在 EX-024 上的核心假设是随机运动期间仍可检测敲击。实机日志证明该假设不成立：电机振动能形成稳定局部基线、短时大偏离和快速回落，完整通过候选链。继续调单一阈值会在误触和漏检之间移动，不能解决可分性问题，因此终止 EX-024 集成。

## 模块边界

| 模块 | 职责 | 禁止事项 |
| --- | --- | --- |
| 传感器 adapter | 串行读取 BMI260 六轴样本并附加单调时间 | 不判断业务状态，不播放音效，不绘制 UI |
| `companion_merit_tap` | 维护局部基线、冲击候选、回落确认、冷却和序号 | 不访问硬件、任务、UI、音频或电机 |
| Controller adapter | 校验业务状态、事件序号和生命周期，生成局部 effect | 不把敲击转成 AI、DOA、运动或网络状态 |
| Expression/UI adapter | 在完整画布上按 800ms 时间线绘制单实例气泡 | 不读取传感器，不修改产品状态 |
| Audio adapter | 以独立 token 尝试播放短 WAV，失败时仅降级音效 | 不因提示音失败进入产品 ERROR |

## 纯 C 判定接口

归档接口位于 `components/companion_merit_tap/include/companion_merit_tap.h`：

```c
void companion_merit_tap_config_default(
    companion_merit_tap_config_t *config);
esp_err_t companion_merit_tap_init(
    companion_merit_tap_t *detector,
    const companion_merit_tap_config_t *config);
void companion_merit_tap_reset(companion_merit_tap_t *detector);
esp_err_t companion_merit_tap_push(
    companion_merit_tap_t *detector,
    const companion_merit_sample_t *sample,
    companion_merit_result_t *result);
```

最后一轮实现使用 5 样本局部中值和局部范围估计动态阈值，候选需要在确认窗口内回到局部基线。参数仍是实验值，不代表可用于任何具体结构。

## 视觉资源

| 文件 | 阶段 | 视觉约束 |
| --- | --- | --- |
| `merit_bubble_01_seed.png` | 光点 | 黄色小光点、低 alpha、两层光晕 |
| `merit_bubble_01a_sprout.png` | 萌芽 | 粗颗粒像素台阶开始成形 |
| `merit_bubble_01b_growing.png` | 成长 | 轮廓放大，不做细密抗锯齿 |
| `merit_bubble_02_readable.png` | 可读 | “功德+1”大字靠近内沿，轮廓轻微倾斜 |
| `merit_bubble_02a_expand.png` | 扩展 | 继续放大，锚点保持稳定 |
| `merit_bubble_03_fade_max.png` | 最大淡出 | 最大尺寸开始降低亮度/alpha |
| `merit_bubble_03a_dissolve.png` | 同尺寸溶解 | 保持最大尺寸，仅淡化消失 |

所有审核图均为完整 `320x240` `icebox` 背景。`generate_merit_tap_assets.ps1` 从右上区域生成 RGB565 与 alpha 运行资源；`render_merit_tap_previews.ps1` 保留完整预览重生成路径。

## 音频资源

`spiffs/merit_tap.wav` 是 16kHz、单声道、16-bit PCM 的阶段资源。正式产品中应使用独立、精确匹配的本地 effect token；产品运行时不变量只能放行当前活动的本地 token，不能泛化放宽所有 Prompt。

## 集成快照

`archive/xiaozhi_companion_robot_integration_snapshot/` 保存从 EX-024 删除前的 Motion、Controller、Expression、UI、入口、CMake 和测试文件。该快照用于恢复集成思路，不参与本 Demo 或 EX-024 构建；后续恢复开发时应按目标产品重新设计接口，不能直接覆盖当前源码。

## 后续恢复顺序

1. 先采集目标结构的静止、缓慢移动、持续机械振动和真实敲击原始六轴数据。
2. 用离线 trace replay 证明类别可分，并形成确定性回归测试。
3. 再调整纯 C 判定或选择额外传感器/结构隔振方案。
4. 判定测试稳定后，才实现独立 Demo 的 Controller、UI 和 Audio adapter。
5. 最后执行 clean build、全片擦除、烧录和实机阈值标定。
