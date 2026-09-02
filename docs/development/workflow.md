# 开发流程

[中文](workflow.md) | [English](workflow.en.md)

1. 从 `CODE/examples/examples.yml` 选择实际存在且公开状态允许的 Example。
2. 阅读该 Example README、`main/CMakeLists.txt`、manifest、Kconfig、defaults、分区和测试入口。
3. 只修改目标责任边界；公共组件变化必须列出所有调用方与兼容影响。
4. 新行为、公共接口或高风险逻辑先建立可执行测试；Bug修复遵守项目纯C防回归门禁。
5. 源码修改后重新完整镜像并 clean build，不能用旧镜像或增量结果作为证据。
6. 设备验证按“明确端口→必要全片擦除→烧录校验→启动日志→可观察行为”执行。
7. 分别记录主机测试、clean build、flash和功能实机结果；未执行项明确写未验证。
8. 同步 README/manifest/公共文档；内部过程历史不复制到公开候选。

当前公开仓库地址、默认分支和PR流程尚未在D-14/S9确定；在正式信息发布前，本文不虚构分支名、组织或自动化门禁。

