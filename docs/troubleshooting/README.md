# 故障排查

[中文](README.md) | [English](README.en.md)

按最先失败的层级处理，不在一个失败后同时切换脚本、工具链、端口和源码。

| 现象 | 最小检查 | 下一步 |
| --- | --- | --- |
| `idf.py` 不存在 | `idf.py --version` | 激活同一 ESP-IDF v5.5.4 环境 |
| 找不到 Example | 核对 `CODE/examples/examples.yml` 和实际目录 | `unavailable/archive/hold` 不作为可构建入口 |
| 源码改动未编译 | 核对镜像时间与路径 | 重新完整镜像，执行 `fullclean` 后构建 |
| 依赖下载失败 | 查看首个 Component Manager 错误与网络 | 恢复网络/缓存后重试同一入口，不换版本 |
| Windows 路径异常 | 核对镜像是否由 Git Bash `cp -r` 生成 | 使用短 ASCII 临时镜像，不用增量同步 |
| GCC ICE | 确认是 `internal compiler error` 而非源码错误 | 同一入口限制并发为2后重新 clean build |
| 找不到串口 | 拔插设备、检查CH340E与数据线 | 人工选择新增端口，不用历史端口 |
| 串口占用 | 关闭现有 monitor/串口程序 | 不用全片擦除解决端口占用 |
| 烧录成功但未启动 | 查看ROM启动模式和首个应用错误 | 先复位/退出下载态；不重复盲目擦写 |
| 屏幕黑屏 | 检查供电、板卡连接和LCD初始化日志 | 先定位硬件/初始化层，不修改无关组件 |
| 设备重启 | 查首个 panic/assert/WDT/brownout/reset证据 | 没有证据时不猜单一根因 |

若 `design.md` 已记录成熟构建/烧录/监控入口，必须继续使用该入口；失败时停止在首个相关错误，不自行尝试替代 runner。

