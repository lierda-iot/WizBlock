# S0 冲突与门禁清单

生成日期：2026-08-31

## 已确认事实

- 正式状态表有 35 条 Example，当前文件系统有 33 个目录。
- EX-027 `camera_face_servo_expression_demo` 与 EX-033 `audio_aec_ns_compare_demo` 目录缺失。
- EX-022 `tf_firmware_launcher_demo` 当前缺 `launcher_return`，因此构建阻塞。
- EX-032 `merit_plus_one_demo` 是非构建归档。
- 组件共 26 个；S3 前只有 26 个有组件 README。
- 排除 build/log/verification/firmware_backup 后，发现 296 个需逐项许可判断的二进制或媒体资产。
- `CODE/boards/laiwfs300` 是历史草稿，当前正式构建事实源为 `CODE/components/laiwfs300`；公开候选排除前者。

## 当前阻断

- 主许可证、素材许可证、`net_mgmt` 的许可证/ABI/源码交付形态尚未在 S8 决定；候选树不能视为正式开源发布。
- 网络/AI/RC Tank 等目录存在配置或设备/本机信息审查面，按矩阵标为 `hold`，不得直接复制。
- 本地构建脚本含机器专属环境配置；按人工裁决保持只读。README 使用 `design.md` 成熟镜像流程的相对路径等价描述，S5 移到整个计划最后。

## 状态解释

- `publish`：可进入本地公开候选；仍受根许可证与正式发布门禁约束。
- `publish-with-limitations`：源码可进入候选，但 README 必须保留未验证项和限制。
- `hold`：因凭据、资产、依赖、许可或功能阻塞暂不进入候选。
- `archive`：只保留中央归档说明，不作为可构建源码发布。
- `unavailable`：正式条目存在但当前目录缺失。
