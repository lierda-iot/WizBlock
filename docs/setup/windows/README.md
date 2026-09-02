# Windows 环境准备

[中文](README.md) | [English](README.en.md)

## 检查顺序

1. 使用数据线连接设备，在设备管理器确认 CH340E 串口出现。
2. 安装 Git，并确认 Git Bash 可用。
3. 安装 ESP-IDF v5.5.4；使用其官方环境入口激活终端。
4. 检查 `idf.py --version`、`python --version`、`cmake --version` 和 `ninja --version`。
5. 仓库使用短 ASCII 路径；当前私有事实源含非 ASCII 路径时，按 `CODE/README.md` 使用 Git Bash 完整镜像。

不要把 ESP-IDF 固定到某个盘符，也不要复制维护者机器的 Python、工具链或证书绝对路径。当前已验证脚本含机器配置但保持只读并排除于公开候选；可移植公开入口留到最终S5。

## 串口

- 从设备管理器确认当前 COM 端口，不使用文档历史端口作为默认。
- 烧录前拔插一次设备，核对端口确实属于目标板。
- 多个设备同时连接时，逐一确认角色与端口；不要按数字大小猜测。

## PowerShell 与 Git Bash 分工

- 当前维护环境先由Git Bash把完整`./CODE`镜像到规定临时目录，再在仓库根PowerShell中相对调用`& "./CODE/tools/build_example.ps1" -Example <example> -Clean`。
- 不把直接`idf.py`写成已经验证的公开替代方法。
- 源码修改后重新执行完整镜像和`-Clean`入口，不能依赖时间戳增量同步。
