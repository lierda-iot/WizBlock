# S7 OPEN Skill 验证记录

验证日期：2026-09-01  
结论：`PASS_IMPLEMENTATION_AND_LOGIC`；`PASS_ISOLATED_SANDBOX_SIMULATION`；`PENDING_CROSS_PLATFORM_REPLAY`  
验证范围：Skill 包结构、统一状态契约、依赖传播、设备确认门、脱敏、公开候选导出、外部隔离沙箱和文档链接

## 执行边界

- 用户明确要求开始 S7；本轮实现与测试位于当前唯一事实源的`skills/`，并通过allowlist进入临时`OPEN_REPOSITORY/`。
- S5继续保持整个计划最后。现有本地构建/烧录脚本未修改，S7没有新增或拼装替代build/flash/monitor命令。
- 未执行驱动/Git/IDF安装、外部下载、真实仓库变更、设备写入、烧录或串口监控。
- 本轮没有Git授权，未执行任何Git命令。
- 用户授权在项目根路径之外使用`E:\Skiltest`。本轮以allowlist导出模拟“仅取得拟公开内容”的拉取结果，没有执行真实clone/pull；公开快照与验证结果分别位于`public-repository/`和`validation-results/`。

## 交付结构

- 总 Skill：`open-dev-all`。
- 独立子 Skill：`open-preflight`、`open-driver`、`open-git`、`open-idf`、`open-clone`、`open-components`、`open-first-example`、`open-build`、`open-flash`、`open-monitor`、`open-diagnose`。
- 共享references：状态契约、流程契约、平台契约和中英文输出模板。
- 公共脚本：无隐藏状态的`open_skill_runtime.py`、可移植结构校验器`validate_skill_tree.py`和无副作用沙箱验证器`run_skill_validation.py`。
- 测试：公共状态/安全行为测试、Skill树独立打包测试、沙箱验证器测试和公开候选导出回归。

## TDD与行为证据

`python -m unittest discover -s ./skills/tests -p "test_*.py"`：22项通过；源树与外部公开快照均执行通过。

覆盖的可观察行为：

1. 完整状态契约只接受`PASS/SKIPPED/FAILED/BLOCKED/NEEDS_USER`和`zh-CN/en`。
2. build失败阻断flash；components失败阻断build/flash；IDF失败阻断components/build/flash。
3. Git失败只在本地仓库不存在时阻断clone和源码下游。
4. 驱动/串口不可见不阻断build，但阻断flash/monitor。
5. 网络不可用时只允许复用已确认的本地仓库与依赖缓存；缺失下载输入时阻断下游。
6. 多端口返回`NEEDS_USER`且不猜端口；无设备返回`BLOCKED`；未确认设备写入返回`NEEDS_USER`。
7. 最终S5入口缺失返回`S5_PORTABLE_ENTRY_PENDING`；成功的烧录前置检查本身不写设备。
8. flash失败阻断依赖该次烧录成功的monitor，不自动重试。
9. 日志脱敏移除密码、Token/Bearer、完整MAC/设备标识和Windows/macOS用户目录，并保留非敏感上下文。
10. stdin/stdout JSON CLI不依赖隐藏文件或聊天状态。
11. 1个总Skill与11个子Skill均具有独立`SKILL.md`、可发现状态契约且无维护者绝对路径或固定串口。
12. build因最终S5边界返回`BLOCKED`时，flash继承`S5_PORTABLE_ENTRY_PENDING`，不丢失实际阻塞原因。
13. 沙箱验证器输出四组Windows/macOS×空/已有仓库夹具和一组公开快照只读场景；每组显式记录总Skill输入、聚合状态和副作用，并拒绝覆盖已有验证目录。
14. 总Skill包门禁检查`open-dev-all`的包结构、6种动作路由和统一状态解析器；缺失总Skill包时总验证为`FAILED`。
15. 11个独立子Skill均在Windows/macOS生成契约结果并逐包检查；若新增子Skill但未加入验证矩阵，覆盖门禁为`FAILED`。
16. 独立的8场景失败/安全矩阵覆盖IDF、组件、build、flash失败传播，以及多端口、无设备、未确认设备写入和最终S5入口缺失；所有依赖步骤保持`BLOCKED/NEEDS_USER`，不伪装为成功。
17. 缺失公开快照会拒绝启动；已有输出目录会拒绝覆盖；包含空格和中文的路径可正常完成验证。
18. 故意移除总Skill包时，验证命令返回非零状态，证明失败结果会阻塞该轮验证被当作通过。
19. Quick Start三角色模拟分别检查维护者构建复现、外部阅读预览和功能验收清单；完整烧录流程必须可与`open-flash`/共享契约对照，公开文档中的可执行烧录代码行必须为0。

`python -m unittest discover -s ./tools/open_release -p "test_*.py"`：6项通过；自定义外部目标、导出报告计数、S7包、脚本和测试进入候选，以及Quick Start三层定位和无公开烧录命令均有回归覆盖。

Skill Creator官方`quick_validate.py`：12个Skill逐包通过。

## 候选与文档证据

- 外部隔离公开快照：538个文件、22个Example、12个Skill包；根内上一轮临时候选未覆盖，仍是可重新生成的临时产物。
- 外部快照负向检查：私有基线0、禁入目录0、导出策略错误0。
- Skill树结构校验：`skills=12 errors=0`，源树和外部快照均通过；12包逐个通过官方`quick_validate.py`；外部快照完整测试22项通过。
- Markdown相对链接：522项，0错误。
- 沙箱总验证：`overall=PASS`、总Skill包与总流程`PASS`、11个独立子Skill在两种模拟平台均`PASS 11/11`、8个失败/安全流程`PASS 8/8`、Quick Start三角色`PASS 3/3`、Windows模拟`PASS 2/2`、macOS模拟`PASS 2/2`、Windows公开快照`live-readonly=PASS`，五个总流程场景失败检查均为0。
- 原始工作流状态仍按契约保留：Windows/macOS空环境均为预期`FAILED`，已有仓库均为预期`BLOCKED`；`live-readonly`为预期`BLOCKED`。已有仓库的build/flash保留`S5_PORTABLE_ENTRY_PENDING`；只读场景build保留S5阻塞，flash因网络不可用且未确认依赖缓存保留`NETWORK_UNAVAILABLE`。
- 总Skill包、子Skill包与共享契约均直接从外部快照复核，`live-readonly`同时检查公开目录和`display_demo`标记；该证据不是独立Codex会话、真实macOS主机、真实安装、构建、烧录或监控证据。
- 中间沙箱与Python缓存只按精确路径移入Windows回收站，可恢复；最终`E:\Skiltest`仅保留`public-repository/`和`validation-results/`，事实源文件未删除。

## S7门禁结论

| 门禁 | 当前结论 | 证据/剩余项 |
| --- | --- | --- |
| 总Skill、11个独立子Skill、references、公共脚本和测试 | 满足 | 12包官方校验通过，结构校验0错误 |
| 中英文状态与输出契约 | 满足逻辑门禁 | 双语言行为测试与模板通过 |
| 关键失败、跳过、阻断、确认和重复运行边界 | 满足逻辑门禁 | 22项测试通过；失败/安全矩阵`PASS 8/8`；故意缺包时CLI非零退出 |
| 公开候选可交付性 | 满足当前外部快照门禁 | 538文件、负向检查0、522链接0错误 |
| Quick Start当前定位 | 满足 | 维护者构建复现、外部阅读预览、功能验收三角色`PASS 3/3`；完整烧录流程可读可对照，可执行烧录代码行0 |
| Windows/macOS空环境/已有仓库模拟验证 | 满足 | Windows `PASS 2/2`、macOS `PASS 2/2`，五个总流程场景失败检查0；11个子Skill双平台`PASS 11/11`；原始工作流状态与预期逐项一致 |
| Windows原生空环境/已有环境实际演练 | 待完成 | 当前Windows仅完成公开快照只读复核与确定性模拟；真实安装/执行仍未进行 |
| macOS原生空环境/已有环境实际演练 | 待完成 | 模拟验证通过，但当前无原生macOS执行环境 |
| build/flash/monitor端到端 | 被最终S5阻塞 | 公开可移植入口尚未实现；Skill正确返回`S5_PORTABLE_ENTRY_PENDING` |

因此，S7的源码实现、逻辑回归、独立打包、候选接入和外部隔离沙箱模拟已经完成；严格的“两平台原生空环境/已有环境、独立Skill会话与真实build/flash/monitor”验收仍未满足，不能将S7整体标为最终门禁通过。

## 使用入口

- 最简总流程：一句`$open-dev-all ...`；默认Example为`display_demo`。
- 最简独立阶段：一句`$open-build ...`、`$open-first-example ...`或其他子Skill名称。
- 客户端尚未自动发现仓库Skill时，直接说明“读取`skills/open-dev-all/SKILL.md`并按`action=setup`处理当前仓库”。
- 中文和英文最短示例、验证命令与边界见`skills/README.md`、`skills/README.en.md`；每个Skill自己的`SKILL.md`记录该入口的Inputs、Workflow和Result，不要求使用者手写JSON或直接调用状态引擎。
