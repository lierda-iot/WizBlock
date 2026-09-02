# S4/S6 公开候选构建验证

验证日期：2026-08-31  
验证对象：`OPEN_REPOSITORY/CODE/examples/display_demo`  
结果：`PASS_WINDOWS_CURRENT_ENV_BUILD_ONLY`；公开跨机器入口与当前四色实机行为未通过

## 执行边界

- 从仓库根目录使用相对路径 `./OPEN_REPOSITORY/CODE`，按 `design.md` 4.1.2 的成熟约束全量镜像到临时 ASCII 目录。
- 只读调用现有 `CODE/tools/build_example.ps1`，执行 `display_demo` clean build；脚本未修改。
- 未执行烧录、串口监控、Git 或远程仓库操作。

## 已通过前置检查

- 候选导出：514个实际文件，22个Example目录；导出报告计数与实际递归文件数一致。
- 候选只额外允许一个人工确认可进入本地候选的自研库：`CODE/components/net_mgmt/lib/libnet_mgmt_esp32s3_idf5_5_4.a`；另外6个版本及其他未批准二进制仍被排除。
- 目标库源文件与候选副本 SHA-256 均为`0DAB2ACFFD0BC40F6D71AD5EBF47B22EFDEE5439EC40DF21B72E59A88FCF4BEA`。
- 候选负向检查：内部基线、禁入目录、非allowlist二进制/媒体、维护者绝对路径、固定串口、非空敏感 defaults 均未发现。
- Markdown 本地链接：检查 442 项，0 个错误。
- 导出策略Python回归3项通过；新增集成回归覆盖“报告文件数必须等于实际候选文件数”。纯C可执行harness使用LLVM-MinGW Clang 22.1.8、目标`x86_64-w64-windows-gnu`完成编译、链接和实际执行，并启动相同回归输出`PASS`。

## 首轮失败与修正

CMake 在组件需求扫描阶段读取 `CODE/components/net_mgmt/CMakeLists.txt`，但公开候选按当前二进制排除规则未复制下列预编译库：

```text
CODE/components/net_mgmt/lib/libnet_mgmt_esp32s3_idf5_5_4.a
```

因此首轮 CMake 配置失败，尚未进入编译阶段。用户随后明确同意推荐恢复路径，并补充要求不考虑新构建方法，只按设计文档既有方式验证。

导出器已改为只允许上述精确路径的v5.5.4目标库，候选副本哈希一致；没有加入其他版本库，没有修改组件CMake、Example或本地构建/烧录脚本。S8对主许可证、ABI、源码/二进制长期交付说明的门禁仍保留。

## 既有入口复验

1. 按`design.md` 4.1.2使用Git Bash把完整`./OPEN_REPOSITORY/CODE`重新镜像到规定临时ASCII目录。
2. 只读调用现有`CODE/tools/build_example.ps1 -Example display_demo -Clean`。
3. 旧的短调用窗口先返回124，但后台Ninja在约136～137秒完成最终`app_check_size`，证明超时来自调用端而不是构建失败。
4. 用户将所有当前有效设计中的clean build调用端超时统一修订为`200000ms`（200秒），并要求继续只用设计文档既有方法。
5. 通过4.1.2原样Git Bash→`powershell.exe -File ... -Clean 2>&1 | tail -30`入口复验，179.9秒内退出码0，输出`[1421/1421]`和`Project build complete`。

## 当前结论

- Windows当前维护环境的公开候选`display_demo` clean build通过：`1421/1421`。
- `display_demo.bin`大小为`0x45540`（283,968字节），1MiB最小app分区剩余`0xBAAC0`（73%），SHA-256为`2DDF1ACE89D21C32312855D2050C498925742F97FA0B38CEF71C3B9618DB8893`。
- 未更换runner、命令或工具链，未修改构建脚本，未执行烧录；公开文档继续只记录`design.md`既有流程的相对路径等价说明，不新增构建方法。
- macOS和全新机器构建仍待后续复核；构建通过不替代烧录或功能实机验证。

## 严格门禁复核后的增量验证

- EX-001当前源码事实已纠正为白、红、绿、蓝顺序循环、每色2秒；2026-06-12历史实机只覆盖白/红，当前四色功能仍待实机复核。
- 公开文档已删除未经验证的直接`idf.py fullclean/build`入口；现阶段只记录设计4.1.2/4.1.3既有脚本入口的仓库相对位置。现有脚本含本机配置并排除于候选，最终S5前不宣称公开跨机器入口通过。
- Windows外层`build_example.sh display_demo clean`在200秒调用端窗口返回124；只读诊断确认底层Ninja已完成最终`app_check_size`并生成283,968字节固件，但因外层未正常返回，该包装入口不记为通过。
- 按设计4.1.2同一镜像、同一工具链直接调用既有`./CODE/tools/build_example.ps1 -Example display_demo -Clean`复验，170.4秒退出0，完成`1421/1421`和`Project build complete`。应用仍为283,968字节，分区剩余73%，本次SHA-256为`297BF4BF3BBD799795C92D054D81D01951B679D5726C0DD3F55A2FCB05CFB49F`。
- 本轮未烧录、未监控串口；该结果只证明Windows当前维护环境clean build，不证明公开候选新机器入口、macOS复核或当前四色实屏行为。
