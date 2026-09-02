# 组件开发边界

[中文](components.md) | [English](components.en.md)

| 分类 | 示例 | 约束 |
| --- | --- | --- |
| 通用组件 | `bus_i2c`、`display_hal`、`storage_hal`、`robot_motion` | 不写死L-AIWFS300 GPIO、阈值或产品文案；公共头文件定义生命周期和错误 |
| 板级组件 | `laiwfs300`、`board_adc`、`board_power`、`bringup_test` | 集中承载当前板卡资源和初始化顺序，不向通用驱动泄漏板级宏 |
| Example 私有模块 | `examples/<example>/main`及私有`components` | 只服务本Example，不被其他Example直接引入其`main` |
| 二进制适配 | `net_mgmt` | 自研但许可证、ABI、版本、源码/二进制形态待S8；当前候选默认hold |

组件 README 至少说明用途、公共头文件、初始化/反初始化、任务/ISR上下文、配置、错误、依赖、板级耦合、测试和许可证状态。暂未写入头文件或README的行为不作稳定接口承诺。

组件索引见 [`CODE/components/README.md`](../../CODE/components/README.md)。

