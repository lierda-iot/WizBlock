# S1 Example 独立文档归并追踪

执行日期：2026-08-31  
当前状态：内容已归并并建立中央哈希归档；用户于2026-08-31对下列六个精确路径授权删除，删除前复验源/归档SHA-256全部一致，原路径现已清理。

## 归并原则

- 当前正式需求只以根 `requirements.md` 为准。
- 当前有效设计只以根 `design.md` 为准。
- 当前阶段状态、验证边界和待办只以根 `project_memory.md` 为准。
- 完整迁移源保存到 `docs/archive/examples/<example>/source-snapshot/`，只供追溯，不作为活跃基线，也不进入公开候选。
- 原 Example 独立文档已按精确路径授权删除；中央源快照保持原始字节和哈希，只供追溯。

## 文件与哈希

| Example | 原路径 | SHA-256 | 中央归档路径 | 当前内容目标 | 处理结果 |
| --- | --- | --- | --- | --- | --- |
| EX-035 | `CODE/examples/rc_tank_demo/requirements.md` | `10FCB3168BFD63EDCC291AC709C0D01E1ABF69C590D816A835229D48FDE312EB` | `docs/archive/examples/rc_tank_demo/source-snapshot/requirements.md` | `requirements.md` 9.15 | 原始摘录、当前需求和验收边界已归并；全文归档；原路径已删除 |
| EX-035 | `CODE/examples/rc_tank_demo/design.md` | `328364F81B32F4343BB05124C54966930F8F1BC2A965454318362AD681A4CE8E` | `docs/archive/examples/rc_tank_demo/source-snapshot/design.md` | `design.md` 11.33 | 当前角色、模块、协议、时序、DVP收口和验证边界已归并；原路径已删除 |
| EX-035 | `CODE/examples/rc_tank_demo/project_memory.md` | `4586542442B2B2DABA8299A37A36EB1A44C9E5DC5B0B6B2A590087CDCC964B44` | `docs/archive/examples/rc_tank_demo/source-snapshot/project_memory.md` | `project_memory.md` 的 EX-035 入口与 2026-08-31 记录 | 当前状态、剩余风险和恢复入口已归并；原路径已删除 |
| EX-035 | `CODE/examples/rc_tank_demo/todo.md` | `0EDB2EE86E586655FED6C679DB31BEE3AD2B01267990DD7528CACA2D5034559D` | `docs/archive/examples/rc_tank_demo/source-snapshot/todo.md` | `project_memory.md` 的 EX-035 待办 | 未完成验收项已归并；原路径已删除 |
| EX-032 | `CODE/examples/merit_plus_one_demo/docs/requirements.md` | `9E968604EA9802DBE0C64AB0F0E21783AE39ADFDEF9ECBE1B8BFD657849058CD` | `docs/archive/examples/merit_plus_one_demo/source-snapshot/requirements.md` | `requirements.md` 9.12 | 根归档需求保持当前有效；原路径已删除 |
| EX-032 | `CODE/examples/merit_plus_one_demo/docs/design.md` | `B38189246486DB8E0A1EBB8E033CB09BEE24FFC70EAF6E99322A1DA0348508D8` | `docs/archive/examples/merit_plus_one_demo/source-snapshot/design.md` | `design.md` 11.30 | 根归档设计保持当前有效；原路径已删除 |

## 完整性检查

- 六个中央归档文件的 SHA-256 与各自迁移源完全一致。
- EX-035 根需求现包含两段正式原始需求摘录、角色/网络/协议/控制/视频/音频/电量/非功能要求和中央验收入口。
- EX-035 根设计现包含角色、资源所有权、协议、控制时序、视频/DVP、音频、电量、错误出口和验证边界。
- EX-032 继续保持“归档、不可独立构建/烧录”的当前口径，不并入活跃产品设计。
- 公开候选生成规则排除 `docs/archive/` 和所有 Example 独立 `requirements/design/project_memory/todo`。

## 已授权并完成的删除清单

用户于2026-08-31逐项同意S1清理；执行前确认下列路径均位于当前工作区且中央快照哈希一致，执行后复核六个原路径均不存在：

- `CODE/examples/rc_tank_demo/requirements.md`
- `CODE/examples/rc_tank_demo/design.md`
- `CODE/examples/rc_tank_demo/project_memory.md`
- `CODE/examples/rc_tank_demo/todo.md`
- `CODE/examples/merit_plus_one_demo/docs/requirements.md`
- `CODE/examples/merit_plus_one_demo/docs/design.md`
