# 本地公开候选与发布门禁

[中文](README.md) | [English](README.en.md)

当前只从本地唯一工作仓库按 allowlist 生成根目录 `OPEN_REPOSITORY/` 临时审查候选；它是可重新生成的发布产物，不是第二套长期源码。当前不创建远程仓库、不提交、不推送，也不清理私有事实源。

## 候选输入

- 根中英文 README 和公开开发文档。
- `skills/`中的总 Skill、11个独立子 Skill、共享references、公共状态脚本和测试。
- 审核后的 `CODE` 顶层工程文件、公共组件和 `examples.yml`。
- `public_status` 为 `publish` 或 `publish-with-limitations` 的 Example 源码。
- 审核后的 defaults/分区/manifest/测试；敏感值必须为空或占位符。

## 默认排除

- `hold`、`archive`、`unavailable` Example 源码目录。
- `CODE/boards/laiwfs300` 历史草稿。
- build、managed cache、生成sdkconfig、固件备份、verification/log/内部调试文档。
- 私有需求、设计、记忆、TODO、OPEN执行记忆和中央迁移快照。
- 所有未完成S8许可判断的非源码二进制/媒体资产。
- 当前机器专属的构建/烧录脚本和路径配置。

## 负向检查

候选必须同时通过：

1. 无内部基线/记忆/TODO/迁移快照。
2. 无build/sdkconfig/cache/log/firmware backup。
3. 无真实凭据、私钥、Token、设备标识、维护者绝对路径或固定串口。
4. 无未列入 allowlist 的二进制和媒体。
5. Example目录与`examples.yml`的公开条目一致。
6. Markdown相对链接有效，中文/英文关键命令和限制一致。
7. `display_demo`按公开说明完成当前声明平台的clean build；未执行的平台标未验证。
8. 12个 Skill 包结构有效，状态契约、依赖传播、脱敏和关键失败分支测试通过；最终S5前自动build/flash/monitor保持`S5_PORTABLE_ENTRY_PENDING`。

## 尚未批准

- 主许可证和文档/素材许可证。
- `net_mgmt`许可证、ABI、版本和源码/二进制交付形态。
- S9 PR/nightly/release CI。
- 新仓库名称、托管平台、组织、默认分支和公开时间。
- 任何Git操作。

这些门禁未闭合前，本地候选不能称为正式开源发布。

当前 Skill 入口见 [OPEN 开发 Skills](../../skills/README.md)。Skill 包和逻辑回归通过不等于Windows/macOS空环境/已有环境实机演练完成；后者仍受最终S5可移植入口和双平台环境约束。
