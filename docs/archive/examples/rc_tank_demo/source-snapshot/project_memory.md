# RC Tank Demo (EX-035) 项目记忆

最后更新: 2026-08-27

---

## 1. 当前状态

**阶段**: 连续控制遥控手感调优已完成。双端固件保持C16w、Q60、实载健康门及空闲STOP一次后静默；DVP最终闭环和Remote单独重启重连仍有效。当前Tank使用最终转向曲线和`offset=0/start=60%/boost=60ms`启动参数，前进、后退、掉头启动及转向手感均人工通过。下一步转入Remote无热点长等待、长断连重扫、双端WiFi图标一致性、失联停车、音频和30分钟长稳验收。

### 2026-08-27 — 连续控制与转向手感收口

- **最终参数**：满力度右转节点定稿为`(0,100,100)`、`(15,100,65)`、`(30,100,30)`、`(45,100,-10)`、`(60,100,-80)`、`(75,100,-95)`、`(90,100,-100)`；负角交换左右，后退半区继续按取反并交换生成。快速迭代中间三轮不展开，只保留最终有效曲线。
- **人工结论**：前进启动、后退启动、掉头启动及当前转向手感通过；`30°`与`45°`过渡已按反馈收平。默认四方向`offset=0/start=60%/boost=60ms`继续使用。
- **交付证据**：Tank最终构建`1447/1447`，应用`915,904B`；COM7全片擦除成功，bootloader、partition table、application三段Hash校验通过，RAM boot成功。Remote功能和固件无变化，本轮未重烧Remote。
- **验证边界**：`tests/test_rc_drive_control.c`已同步最终曲线、对称/插值、力度缩放和换向断言；纯C按用户对快速调优阶段的持续授权未编译、未运行，不得记录为通过。剩余风险仅保留为最终曲线缺少自动执行回归证据，人工验收结论不受此表述替代。
- **下一步**：保持当前控制参数不变，继续Remote无热点长等待、长断连重扫、双端WiFi图标一致性、失联停车、音频和30分钟长稳；Remote偶发窄叠影线继续挂起。

### 2026-08-27 — 连续控制启动基本通过，进入转向手感调优

- **人工通过项**：当前固件的前进启动、后退启动和掉头启动均正常；本轮不再把启动失败作为主要问题，也不先改四方向默认启动占空比和助推时长。
- **人工未通过项**：`90°`原地转向速度太慢；`<45°`的转弯差速几乎不足以产生有效转向；`45..90°`的实际转角小于摇杆意图。连续控制整体仍处于机械调优中，不得标记为实机验收完成。
- **当前实现量化**：满力度`30°/45°/90°`逻辑输出分别为`100/85`、`100/70`、`75/-75`，经当前非零PWM曲线后约为`100%/92%`、`100%/85%`、`±87%`；力度下降时，`50%`非零PWM下限会进一步压缩同向履带的实际占空比差。
- **诊断排序**：首要原因是现有混控节点在`0..45°`差速过缓且PWM下限继续压缩差值；其次是`90°`把双履带限制在逻辑`±75`而非满输出；四方向偏移为0可能影响左右对称，但不能单独解释全角域转向普遍不足。
- **回归边界**：本轮聚焦执行`tests/run_host_tests.ps1 -TestName test_rc_drive_control`，LLVM-MinGW Clang 22.1.8、目标`x86_64-w64-windows-gnu`编译、链接和执行成功，`test_rc_drive_control exit=0`。该结果只确认当前曲线基线，不证明当前实机手感合格。
- **待确认方案边界**：下一版候选只修改`main/rc_drive_control.c`的满力度混控节点和对应纯C断言，保持启动助推、基础PWM映射、斜坡、换向死区、STOP/超时/断连安全、V1协议以及视频链路不变；具体新节点需人工确认后实施。

### 2026-08-27 — DVP最终闭环、空闲静默与Remote重启验证

- **最终重试次数**：全片擦写后的健康门序列从`retry=0`开始；`retry=0/1/2/3`四次启动均为`complete=73,incomplete=30,total=103`并拒绝，第5次启动记录`retry=4,complete=81,incomplete=0,total=81,verdict=accept`。因此用户未直接观察到的实际结果是“失败4次，第5次恢复”，未达到第5次失败上限。
- **画面与重连人工结论**：用户确认最终接受态画面正常；随后保持Tank工作、单独重启Remote，Remote自动重连且画面仍正常。本轮画面与Remote重启重连闭环有效。
- **问题原因**：拒绝态几乎全部残帧停在10块，接受态81帧全部完成11块，复现的是既有概率性DVP坏启动。每个窗口`ctrl_rx=1`，所有残帧均距控制包超过10ms；Tank空闲接收等待从20ms恢复到50ms后仍为`73/30`。因此新摇杆流量或控制任务调度不是DVP异常原因。WiFi断连发生在健康门拒绝并执行`esp_restart()`之后，是Tank主动重启的直接结果。
- **有效解决方案**：不改C16w的102,400B/51,200B staged-DVP、软件索引、逐帧暂停、C2M和固定行修复，不改Q60/1200B分片；在真实Q60编码和UDP负载下保留8秒健康门，残帧率超过5%时最多执行5次有限完整重启，正常后清零重试并持续运行。不得用缺失第10块伪补、放宽完整性判据或历史已否决的EOF/VSYNC/延时路线替代。
- **空闲控制结果**：Remote首次命令或变化立即发送，未变化DRIVE保持100ms保活，未变化STOP不保活；STOP发送失败保持策略和序号、下一20ms重试，重连世代重新发送一次STOP。最终Remote日志两次间隔8秒均为`ok=43,stop=12,drive=31,fail=0,current=STOP`，证明空闲未持续发包。
- **启动环境边界**：COM7每次软件重启在当前串口/BOOT硬件状态进入`DOWNLOAD`，本轮复用成熟RAM bootloader入口人工续跑健康门；这是既有GPIO0/BOOT启动边界，不是应用DVP或WiFi故障。当前Tank停留在已接受的正常运行态。
- **证据**：`verification/dvp_limit_round4_tank_com7.log`、`dvp_limit_round5_boot2_tank_com7.log`、`boot3`、`boot4`、`boot5`及`dvp_limit_round5_accept_remote_com24.log`。本轮纯C按用户授权跳过，不是“通过”；文档更新由用户在最终人工确认后明确授权。

### 2026-08-27 — 连续控制双端首轮实测、DVP自愈重启与空闲发包变更

- **用户现场状态**：连续角度/力度摇杆方案已完成双端烧录。首轮实测报告WiFi断连和Remote画面再次异常，并怀疑与新摇杆方案有关；两端附件日志分别来自Tank与Remote。
- **Tank证据**：首次连接后健康窗为`complete=78,incomplete=29,total=107`，残帧率`27.1%`；第二次为`85/18/103`，残帧率`17.5%`。两次均超过`RC_TANK_DVP_HEALTH_BAD_PERCENT=5%`，日志先明确`restarting in 1s`，随后出现`RTC_SW_CPU_RST`。每次重启前Tank记录STA离开、控制立即停车和视频socket失效。
- **Remote证据**：Remote先成功关联、获取`192.168.4.2`并建立UDP/TCP通道；Tank重启后才出现`bcn_timeout`、`ap_probe_send over`和断连回调，随后能重新关联。第二次亦在Tank下一次健康门拒绝后进入beacon timeout。日志中没有Panic、WDT、JPEG解码、LCD DMA或越界错误。
- **历史对照**：已验证健康门闭环曾连续拒绝`64/45`、`68/33`、`67/40`三次坏态，第4次以`80/1`接受并由用户确认画面正常。当前两次比值与该已知概率性DVP坏启动同签名；日志未继续到后续接受态或5次有限重试上限，因此本轮状态为“自愈进行中/结果未闭合”，不是已证实的独立WiFi回归。
- **Git基线对比**：用户明确允许把远端已验证画面正常版本作为基线。已最小刷新`origin/develop`，当前`develop`与`origin/develop`均指向`d039627`；工作区相对该基线修改集中在连续控制协议、摇杆、Tank电机控制及其测试/文档，`main/rc_video.c`、`main/rc_net.c`、`main/app_main.c`无差异。未提交、推送、切换、回滚或覆盖工作树。
- **耦合风险**：新版Tank控制接收等待由50ms缩短至20ms并保持高优先级；Remote当前空闲每100ms发送STOP，活动时命令变化可约20ms发送一次。该负载可能提高时序敏感DVP进入坏态的概率，但控制改版前已存在同类坏启动，且本轮无单变量对照，当前只能列为待验证风险。
- **正式需求变更**：用户要求“遥控器在摇杆完全无操作的时候，不应该持续发送数据到坦克”。原话已登记到根`requirements.md`；该变更与现有100ms STOP心跳冲突。待确认完全无操作定义、进入空闲是否发送一次STOP、STOP失败是否重试及重连空闲行为；确认前不修改功能代码。
- **下一步**：先确认空闲发送契约；随后以已验证远端基线为对照，在相同冷启动和同一画面观察窗口下分别测试旧控制与新控制/空闲静默，记录每次8秒`complete/incomplete`、是否接受、Remote画面和重启次数。没有该反馈环前不修改视频链路或健康门阈值。

### 2026-08-26 — 连续角度/力度控制实现与纯C全量回归

- **实现范围**：Remote摇杆改为连续角度`-180..180°`与逻辑力度`1..100`，控制通道改为固定14字节V1大端协议；Tank新增连续履带混控、力度缩放、`0→0%/1→50%/100→100%` PWM映射、四方向校准槽、自适应启动助推、20ms快速斜坡和40ms换向死区。纯水平`±90°`保留原地转向，满幅逻辑履带速度限制为`±75`。旧7字节五态协议不兼容。
- **安全与时序**：发送端保持变化立即发送、100ms保活、失败同序号重试；接收端只接受字段与新序号均有效的包，300ms无有效新包停车。断连/超时会清接收基线并停车，Remote重连后等待摇杆释放；STOP立即生效，左右履带独立执行助推、斜坡和换向。
- **验证证据**：在`tests/run_host_tests.ps1`执行最终全量回归，LLVM-MinGW Clang 22.1.8、目标`x86_64-w64-windows-gnu`完成21个测试程序的编译、链接和真实执行，21项全部`exit=0`。新增覆盖V1编解码/非法包/序号回绕、连续摇杆边界、发送策略、混控曲线/对称/插值、PWM配置、助推、斜坡、换向和立即STOP；既有网络、视频、显示、采集和RLE回归同时通过。
- **剩余边界**：本轮未执行Tank/Remote目标工程构建、全片擦除、烧录或实机机械标定。默认四方向`offset=0`、`start=60%`、`boost=60ms`仍是待实测初值；主机纯C结果不能证明真实电机起步、转向观感、机械方向或带载性能。

### 2026-08-26 — 正式诊断清理版构建、擦写与最终人工验收

- **清理范围**：TANK/REMOTE正式Kconfig和启动路径已移除A0/A1/A2/C15/C16窗口矩阵、`RC_TANK_DEBUG_VERBOSE`、本地预览接口、全帧CRC/自解码/逐帧探针及历史`rc_tank_startup_policy`。正式路径保留Q60、102,400B staged-DVP、51,200B半缓冲、软件索引、逐帧暂停握手、C2M invalidate、固定异常行修复、1200B UDP分片、Remote最新完整帧显示、WiFi重连与一次性实载健康门。
- **构建证据**：Tank clean build `1445/1445`，应用911,392B、SHA-256 `90FD0C4D636CFF98CD66F0B70826F8CDCF3FFF46020005DB66C3B936A09243F8`；Remote clean build `1445/1445`，应用920,480B、SHA-256 `9F95030742CF5C1BADB2D11C10E813F4821E1CC423BD4F9156DE6413D5CF48C7`。仅保留已知非阻断Kconfig环境警告。
- **擦写证据**：COM7 Tank与COM24 Remote均按成熟脚本先全片擦除，再写入bootloader、partition table和application；两端三段均完成`Hash of data verified`。COM7随后按设计文档记录的成熟`load_ram`入口启动成功。
- **人工裁决**：用户确认画面验证正常、移动操作没有问题；此前已确认方向正确、100%亮度合适。该结果关闭本轮“清理后是否回归”的人工门禁。
- **保留边界**：Remote底部约1/3～1/4高度偶发3～4像素横向叠影线只保留记录，本阶段不继续处理。网络长断连/重扫、双端图标一致性、失联停车、音频及30分钟长稳未由本轮覆盖。
- **验证豁免**：纯C未运行（按用户持续授权跳过），不是“通过”。`tests/run_host_tests.ps1`仍引用已删除的历史启动策略测试，已在README标为非当前交付入口。

### 2026-08-26 — Remote画质人工边界与现场备份（已完成）

- **人工结论**：Remote画面显示基本正常；底部约1/3～1/4高度偶发3～4像素横向叠影线。按用户指令，该问题只保留记录，本阶段不再继续修复。
- **冻结边界**：方向正确、100%亮度合适；Q60、C16w稳定采集、固定异常行修复及启动健康自愈门保持不变，不因该偶发现象新增参数或诊断流程。
- **交付结果**：诊断现场已以提交`648b83d`推送到远程`develop`；随后完成双端冗余诊断清理、构建、擦写和人工画面/移动确认。清理版的详细结果以上一节为准。
- **验证边界**：纯C未运行（按授权跳过），不是“通过”。

### 2026-08-26 — Remote概率画面实载健康自愈首次闭环

- **用户边界**：保持当前Remote方向，不新增左右镜像；收敛概率画面异常；正式亮度由110%降为100%。用户最终确认“亮度合适，方向正确”，并在14:49:00确认第4次Remote画面正常。
- **历史与改动边界**：继续冻结C16w/C16q的102,400B staged-DVP、51,200B半缓冲、软件索引、逐帧暂停握手、C2M invalidate和固定异常行修复；未修改GDMA ISR、Q60、UDP分片、Remote解码/合成或方向。已否决的EOF/VSYNC defer和仅重建DVP不再保留。`main/app_main.c`把临时多窗口矩阵收敛为一次性实载健康门，`main/rc_video.c`只把正式亮度增益改为100%。
- **健康判据与退出**：通道和`video_tx`启动后在真实Q60/UDP负载下采样8秒；少于40个事件或残帧率大于5%即拒绝，NVS记录重试并在1秒后完整软件重启，最多5次。接受后清零重试、结束监控且不再周期重启；Tank重连时幂等复查。NVS保存失败或达到上限时停止自动重启，避免无限循环。
- **构建与擦写**：成熟Tank clean入口完成`1446/1446`，应用926,000B，SHA-256`FE7732F397FECB57E70926696F97C4C9A4F8CD10FAE48A67198733908F72442C`；bootloader和partition SHA-256分别为`C71DD885AB0FF944F9B18607AC7314C7D807BAB06958F35EE114849828EB11F3`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`。COM7整片擦除、三段写入和三段`Hash of data verified`均成功。
- **自动与人工对齐**：前三次人眼均为异常画面，对应8秒窗口`complete/incomplete=64/45、68/33、67/40`，门禁全部拒绝并自动重启；第4次`80/1`通过，用户确认画面正常。接受后保持段最终`complete=370, incomplete=1`，所有稳定采集生命周期、暂停与组装错误为0；Remote显示约`8.89～10.15fps`，`missing=24494`未增长，无JPEG解码、DMA显示、Panic或WDT错误。
- **重连证据**：三次Tank软件重启期间，COM24均记录WiFi断开、红色状态更新、自动重新关联、绿色状态更新和视频恢复；Remote自身未重启。该结果只覆盖Tank短时重启恢复，不外推为Tank长时间缺席或重试耗尽后的持续重扫通过。
- **证据文件**：`verification/dvp_live_health_gate_r1_build_tank.log`、`verification/dvp_live_health_gate_r1_flash_tank_com7.log`、`verification/dvp_live_health_gate_r1_runtime_tank_com7.log`、`verification/dvp_live_health_gate_r1_runtime_remote_com24.log`。
- **剩余验收**：重复冷启动确认每次坏态均在5次上限内收敛；执行30分钟双端长稳，确认放行后不会再转为高残帧状态；补完无热点长等待、长断连重扫和双端图标肉眼一致性。纯C未运行（按授权跳过），不是“通过”。

### 2026-08-25 — Remote概率画面异常十轮迭代到达上限

- **授权与停止边界**：用户授权以Tank侧为主、最多10轮自动编辑、编译、全片擦除、烧录和日志抓取，并授权跳过纯C。十轮已全部用完且未达画质停止条件，因此不再继续功能代码修改、构建、擦写或实机迭代。此前单次Git合并/推送任务已完成，稳定C16w内容以`8d6b8be`推送到`origin/develop`；该Git授权已失效，本阶段收口未执行Git操作。
- **R1～R6生命周期收敛**：R1去除request/wait/prepare后为`complete=1226, incomplete=745`；R2仅恢复prepare曾出现好启动，但后续仍有坏启动；R3用“生产者请求hold + 消费者等待ack + prepare resume”达到本组最好的`1120/2`；R4的start-prepare-armed为779残帧，R5去除逐帧stop/start为741残帧，R6恢复R3后三次启动依次为`490/0`、`250/1`、`98/47`。这证明R3语义是当前最佳边界，但不足以消除概率坏启动。
- **R7/R8直接插桩结论**：好启动为`complete=190, incomplete=0`，坏启动为`complete=248, incomplete=89, incomplete_blocks=890`；坏启动的残帧候选全部固定在第10块。好/坏启动的EOF ISR核、DVP任务核分布与start延时一致（均约26～28µs），坏状态也可在无WiFi关联抖动时出现。因此任务核/ISR核、高层start延时及网络关联抖动不再作为主根因。R8构建`1446/1446`，应用SHA-256`F488728DD41BD287C6A0F09FB322FC5B47BB20A8B8F725C9528D98EF196F4327`，全片擦写和校验证据为`verification/remote_image_r8_flash_tank_com7.log`。
- **R9边界对齐否决**：在启用采集前等待VSYNC高电平并清pending的硬件边界对齐明显恶化，最终约`complete=246, incomplete=140`，残帧块从固定块10扩展到块0与块10。该路线已在R10前撤销，不得回归有效设计。R9构建`1446/1446`，应用SHA-256`E5B94BCA7F4F00D4B1AC53BF51D8AC0E106F17E2FADB855564A4FE12D737EDBD`；擦写与运行证据分别为`verification/remote_image_r9_flash_tank_com7.log`、`verification/remote_image_r9_boot1_tank_com7_45s.log`。
- **R10启动健康门控否决**：R10恢复R8/R3采集语义、移除临时`[DEBUG-r*]`插桩，在交付视频前编码20帧并要求残帧不超过1。实机先记录`passed complete=20 incomplete=0; Q60 delivery enabled`，但正式UDP发送后转入坏状态，最终约`complete=676, incomplete=423, incomplete_blocks=3239`、`pause_req=pause_ack=680`、`lifecycle_errors=0`，且无Panic/WDT/重启。这直接证明启动前短窗口门控不能防止正式负载下的后续状态转换。
- **R10制件与板上状态**：Tank clean build完成`1446/1446`，应用924,144B；bootloader/partition/application SHA-256依次为`69DC339B380E9B85FF2C829612EF7CD0117BFDD0683F5A9D874667B54477921C`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`、`765CB41DB8695FC50F45D991D65DF1FA97400F92EEFD880949753EC65AEA1960`。COM7已完成全片擦除、三段写入和Hash校验，证据为`verification/remote_image_r10_flash_tank_com7.log`；失败运行证据为`verification/remote_image_r10_boot1_tank_com7_90s.log`。当前源码与COM7板上都是这一实验候选，未通过Remote画质门禁，不得标记为交付版或反向写入当前有效设计。
- **COM24与下一入口**：Remote硬件虽已在COM24上电，但Tank源端已在自动可观测门禁中失败，本次没有必要引入Remote端变量，因此未抓取、擦写或重新测试COM24。后续若继续，需要新的人工方向与新迭代授权，且健康判定必须在实际Q60/UDP负载下持续执行。
- **验证豁免**：纯C未运行（按授权跳过），不是“通过”。

### 2026-08-25 — C16w五次启动重复验证

- **人工结论**：用户手动测试5轮启动，W7均为正常画面。该结果把C16w从单次W7验收提升为重复启动稳定基线，但只覆盖Tank本地诊断画面，不外推Remote、网络重连或30分钟长稳。
- **日志证据**：本轮附件`pasted-text.txt`为200,784B、2,521行，SHA-256`51F863CDC7981830B5054DCA44E2B1ED37B38E70BDF98AEECD9DFDA2F19B58D9`，可解析出4次完整`WINDOW_07 start`和4次`final fixed-line sequence complete`；各次末尾分别达到`display_frame=465/405/375/555`，且`pause_req=pause_ack`。此前本地自动日志`verification/c16w_reboot_r1_tank_com7_130s.log`提供另1次完整启动证据，与用户5轮人工结论一致。
- **当前源重建边界**：当前源重新clean构建的应用为942,544B，SHA-256`148C42E227EAA72132D983DAC39AAB0849EF1746323F14EE959F21532DAC9AC4`；bootloader和partition SHA-256分别为`9353356DD4C86F4FD5EB56DCCE74C3A678E4404B255666DF01C3F04972640714`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`。该固件已完成COM7全片擦除、三段Hash校验和成熟RAM bootloader启动。
- **非阻断错误边界**：附件4次启动均出现`SPI bus already initialized`和`post_table p0_31=0x00`，但均继续运行到W7；未见Panic/Guru/WDT，也未见pause、生命周期或staged错误。不得把本轮表述为零错误启动，也不得因这两条日志回退已验证的C16w画面链路。
- **验证豁免**：纯C按用户持续授权完全跳过，状态为“未运行”，不是“通过”。

### 2026-08-25 — Remote主动复位复现与正式重连需求

- **人工结论**：用户确认“基本功能都有了，遥控器端的画面也还可以”，因此正式视频链路具备基本人眼可用性；尚未据此扩张为全部画质、四向机械控制、失联恢复或长稳验收通过。
- **附件证据**：附件`pasted-text.txt`为26,598B、650行，SHA-256`CFCE7901B1DCCDE448CA8F07A9AC60E45327CDAE61C841CB2C26E32D33246BED`。日志三次重复：约12.3秒`esp_wifi_connect failed: ESP_ERR_WIFI_SSID`，随后扫描无`RC_TANK_*`，约14.9秒`Network init failed: ESP_FAIL`，等待5秒后以`rst:0xc (RTC_SW_CPU_RST)`重新启动；周期约19.9秒。没有Panic/Guru/WDT/brownout或堆损坏证据。
- **直接根因**：Remote `rc_net_init()`只做一次扫描，找不到Tank热点即返回`ESP_FAIL`；`rc_remote_role_run()`把该结果返回给`app_main()`，顶层既有“关键初始化失败”策略等待5秒调用`esp_restart()`。这是确定性的主动复位链，不是Tank画面链路或随机崩溃。
- **关联缺口**：已连接后的Remote断连处理最多重试5次，耗尽后只置失败位且不重新扫描；Tank断连事件会清连接状态并发布回调，但当前依赖300ms控制超时停车而非事件级立即停车；Tank右上红/绿WiFi图标已实现，Remote左上对应图标尚未实现。
- **正式需求变化**：用户明确要求两端上电自动连接、断连后两端自动处理重连，并要求Remote左上WiFi连接状态与Tank右上状态保持一致。原话及规范化验收已原位进入requirements.md v0.33。候选实现尚未开始，不在本阶段记为设计结论。
- **验证边界**：附件证明无热点启动失败路径，但未包含“已连接后切断Tank再恢复”的完整时序；正式修复必须分别覆盖Remote先上电、Tank先上电、运行中Tank消失/恢复、socket与任务单例、Tank安全停车和双端图标。纯C仍按持续授权完全跳过，状态为“未运行”，不是“通过”。

### 2026-08-25 — 正式双端集成与自检收敛

- **当时正式配置（已由2026-08-26清理版取代）**：TANK和REMOTE当时仅默认关闭A0/A1/A2/C16窗口诊断，并保留显式诊断开关；2026-08-26清理版已把相关Kconfig、接口和构建依赖彻底移除。仍有效的产品边界是C16w staged-DVP、不启动Tank本地预览、Tank状态任务独占LCD及Q60正式JPEG。
- **首轮集成问题与根因**：`verification/formal_r1_tank_com7.log`（849B，SHA-256`6D5DA1B46F22C8F8D053BD3EDF7FC65148476B9057E96C3F5A1E89688BE059F7`）与`verification/formal_r1_remote_com24.log`（10,613B，SHA-256`C05C3DDD07B549D32CC5E36FD3F3922AA4EC63A27552944EF3987874FA79EA58`）确认双端网络和通道已经建立，但TANK停在`Local preview quiesced before video_tx`。根因是正式路径未启动本地预览，网络回调仍无条件执行预览停止并操作LCD，与Tank状态任务形成所有权冲突。最小修复为：仅在预览/采集确实活动时执行停止握手和对应日志；无预览时直接进入video_tx，不修改控制协议、摄像头池或LCD显示契约。
- **最终构建**：TANK clean构建完成`1446/1446`，应用921,744B，SHA-256`680A1C537AB81C1DA23A45F8258C591C5D4FC18E79A7BB81937E30A9BA84FE7E`；REMOTE在同一clean构建图完成，应用921,712B，SHA-256`9477CE08DA21AC236C7BE5B94466A27C463A3C5AEBF2BF1CAE46E553B42370D5`。REMOTE保留两条未使用函数/变量非致命编译警告，不能表述为零警告构建。
- **擦写与启动**：COM7 TANK与COM24 REMOTE均完成全片擦除、bootloader/partition/application三段写入及`Hash of data verified`，随后用设计文档记录的RAM bootloader成熟入口启动；当前板上保持这组正式固件。
- **最终自动证据**：`verification/formal_r2_tank_com7.log`为34,739B，SHA-256`CCC08CB9D97C9E623B2481AC4AA4F4D4E65A0AE722A76854EE7AF45009D8804E`；`verification/formal_r2_remote_com24.log`为34,373B，SHA-256`C387CC7F0AD2D26B3B7B273FE0856197F2E9E459C13E0F04FE765CC39FA5092B`。180秒内TANK最后40个稳态窗口发送平均`9.95fps`（`9.8～10.1`），JPEG平均8,765B（`8,314～9,018B`）；REMOTE显示平均`9.85fps`（`9.36～10.17`）。TANK最终`pause_req=pause_ack=1655`、`timeout=0`、`quiesce=resume=1629`、`lifecycle_errors=0`、`repairs=1629`，无采集长度/身份/staged错误；REMOTE完整帧达到1620、`queue_drop=0`，启动形成的`missing=2`未继续增长。双端无Panic/Guru/abort/assert/WDT或重启，低频同序号CRC抽样一致。
- **启动瞬态与边界**：Remote尚未完成启动/关联时，TANK首个短窗口出现9次`errno=12`发送失败，随后稳定且未复发；REMOTE有23次`stale_drop`，符合只消费最新完整帧的既定策略。自动日志不能证明Remote画面颜色、固定横线/撕裂及真实端到端时延，也未完成四向机械控制、松手/失联停车、30分钟长稳和音频验收。
- **验证豁免**：纯C按用户持续授权完全跳过，状态为“未运行”，不是“通过”。本轮未执行任何Git操作。

### 2026-08-25 — C16v根因定位、W31人工通过与C16w最终验收

- **C16v直接证据**：`verification/c16v_r1_source_row_matrix_tank_com7.log`为142,671B，SHA-256`DF3E032BEF7C87D7D8533C2855E89D19E0F5E8FA48239FE5E4271B35E442BEEF`。真实W31从设备约350秒到477秒持续命中`target=392 source=472 rows=8`；最终`display_frame=2141`、`scans=2141`、`rgb_repairs=661`，无guard/lifecycle/pause/cache/raw-post/overlap/finish-collision/staged错误。
- **根因边界**：`y=392..399`位于第10个40行块末尾，`y=472..479`位于第12个40行块末尾；两者使用同一个51,200B物理半缓冲。结合C16q的前向ISR复制和C16t中几乎每帧固定在第11块的EOF身份错配，最符合全部证据的机制是CPU向PSRAM复制尚未完全读完时GDMA已开始复用同一半缓冲，导致复制尾部读到末块内容。LCD、VYUY转RGB和各类延时已经由前轮反证排除。
- **C16v人工裁决**：W31在RGB画布中把受损原始8行对应的输出`y=147..149`以相邻正常行插值重建；用户确认“31窗口画面已经正常，没有叠影线条”。该修复方式已通过人工画面验证，但逐帧480行扫描约增加23～31ms，不作为最终运行开销。
- **C16w最小收敛**：保留C16q/C16o的102,400B staged-DVP、GDMA EOF ISR复制、软件索引、逐帧暂停握手、614,400B承载区、终端保护、C2M invalidate、20MHz/60行LCD和110%亮度；仅把W31已确认的`raw_y=392 rows=8`固定为RGB画布3行重建，跳过逐帧行扫描。窗口恢复W1黑屏、W2真人、W3色块、W4色块、W5真人、W6色块、W7真人并保持。
- **C16w构建与擦写**：clean构建命中设计第16节已记录的GCC 14.2.0 esp-dsp IRA ICE，严格复用同一构建图`ninja -j1 all`完成`303/303`。应用1,004,464B；应用/bootloader/partition SHA-256分别为`E0282B768927E8F33DFDCAE8832E9886757E9A5A29502F51C26F5DDE9CE9A27D`、`5FE162C8B94B0BD1BAC3A968625407FB0C6FCD9640AEF9D41EFE29A296512D0F`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，构建源与备份一致。COM7全片擦除和三段`Hash of data verified`成功；RAM bootloader用ESP-IDF Python 3.11成熟入口启动成功。
- **C16w自动证据**：`verification/c16w_r1_final_fixed_rgb_tank_com7.log`为75,500B，SHA-256`2EEE80D4DF4C20680CB4D74C8F751D7DBA059818186BF6706E87B398E8C58E60`。被动捕获开始时设备已在W7，覆盖设备约128～354秒；W7从累计495帧增长到1,875帧，新增1,380帧。最终`pause_req=pause_ack=1875`、`rgb_repairs=1875`、`scans=0`，无重启/Panic/WDT、帧超时、guard/lifecycle/pause/cache/raw-post/overlap/finish-collision/staged错误。该日志不补称W1～W6起始标记已被本次捕获。
- **C16w人工裁决与停止点**：用户确认“W7画面符合要求，没有异常”。C16w的自动稳定证据和最终肉眼门禁均已完成，Tank本地C16彩色画面专项到此结束并停止，不再继续参数迭代，也不自动进入Remote或其他后续阶段。
- **验证边界**：纯C按持续授权完全跳过，状态为“未运行”，不是“通过”；Remote、产品链路帧率、控制、音频和30分钟长稳均未由本轮覆盖。

### 2026-08-25 — C16u异常块VSYNC后组装矩阵

- **机制与矩阵**：W4/W5保留软件索引即时复制基线；W6/W7起在描述符/软件索引不一致时不在GDMA ISR复制第11个51,200B块，改为VSYNC到达DVP任务后先按软件索引补该块，再让上游复制最终块。W6/W7、W8/W9、W10/W11、W12/W13、W14/W15、W16/W17、W18/W19、W20/W21、W22/W23依次为0/10/20/50/100/200/500/800/1000µs，固定色块/真人成对；其他稳定边界不变。
- **构建与擦写**：Tank clean build一次完成`1446/1446`，应用1,002,912B（`0xf4da0`）；应用/bootloader/partition SHA-256分别为`A6E91D04C61AF549736AD904BC3D3B6A105826281AE48473B4BD535BC30C6FC7`、`846F054C309B3FD0B3EA3CB0D81A3C59F0782D789E55758C9B9AED4687E020E5`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，构建源与备份一致。COM7全片擦除、三段Hash校验和成熟RAM bootloader启动均成功。
- **自动证据**：`verification/c16u_r1_vsync_defer_matrix_tank_com7.log`为137,811B，SHA-256`B36BF506FBC86409F5A888E259FB133622BEBDC80DC3F7913254AE816C3F67DD`。日志覆盖W2～W23并保持至设备约389秒/2,105显示帧；最终`defer_events=drained=1853`、`drain_errors=0`，有97次pending期间额外数据事件被显式计数。无重启、Panic、WDT、`invalid state`、暂停超时、生命周期、缓存、槽重叠、完成冲突或组装错误。
- **人工裁决**：W5基线及W7/W9/W11/W13/W15/W17/W19/W21/W23所有真人窗口均表现相同：一条稳定叠影窄带，其余位置正常；0～1000µs和VSYNC后组装均无改善。最新图片进一步确认不是窄带以下整体错位，而是源帧最底部约3～4显示像素出现在预览中下部固定位置。C16u整组否决，停止全部延时路线。
- **下一候选（未验证，仅记忆）**：C16v给480条源行做轻量指纹，直接输出“中间目标行=底部来源行”的映射；同次烧录对40行半块边界y=320/360/400/440分别做窄带RAW修复，并比较固定RAW、自适应重复行RAW和仅RGB显示修复。目标是在一轮内定位准确源行/字节段并区分采集组装与转换呈现，不再用时延推测。候选通过人工验证前不得进入`design.md`。
- **验证边界**：纯C按持续授权完全跳过，状态为“未运行”，不是“通过”。

### 2026-08-25 — C16t异常块位置与等待矩阵

- **机制与矩阵**：恢复软件索引作为唯一拷贝源，只把EOF描述符作为观测；仅当描述符半块与软件索引不一致时记录当前帧内块序号并施加等待。W4/W5为0µs，W6/W7、W8/W9、W10/W11、W12/W13、W14/W15、W16/W17、W18/W19、W20/W21、W22/W23依次为2/5/10/20/50/100/200/500/1000µs，固定色块/真人成对；110%亮度、102,400B staged-DVP、C16o暂停握手和direct60x1不变。
- **构建与擦写**：clean构建命中设计第16节已记录的GCC 14.2.0 IRA偶发ICE，严格复用同一构建图`ninja -j1 all`恢复并完成`305/305`；应用1,002,272B，应用/bootloader/partition SHA-256分别为`43D543F1AC8A5FE628D64ADB09A8DF10345EDED728A1A16CD21DE223435CF9AE`、`CF8CD770BB2D0D59F9D8AF043645C52507169EECED588C2A71B37E526D097F27`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，构建源与备份一致。COM7全片擦除、三段`Hash of data verified`和成熟RAM bootloader启动均成功。
- **自动证据**：`verification/c16t_r1_mismatch_settle_matrix_tank_com7.log`为134,391B，SHA-256`1F5A6E8DF26D839BA072712F0DB3EA65FEA8F46E0651567AB4584533CE174A16`。日志覆盖W2～W23并保持W23到设备约394秒/2,136显示帧；无重启、Panic、WDT、暂停超时、生命周期、缓存、槽重叠、完成冲突或组装错误。最终`eof_valid=23551`、`eof_mismatch=2161`，错配块直方图为`0/0/0/0/1/1/1/0/9/11/2138`，即98.9%的错配固定在索引10（每帧第11块、帧尾前一块）。日志有2条可恢复`invalid state 1`，随后继续运行且生命周期计数为0。
- **人工裁决**：W1黑屏；W2及所有真人窗口W5/W7/W9/W11/W13/W15/W17/W19/W21/W23均保留相同稳定叠影线，所有固定色块窗口也没有随0～1000µs等待出现可见改善。等待阶梯整组否决，不能继续用“DMA完成后多等一会”解释或修复窄带。
- **下一候选（未验证，仅记忆）**：C16u不再在GDMA ISR的异常EOF上立即复制第11块，而只标记待处理；收到VSYNC后在任务态先组装该软件索引半块，再让上游按既有契约复制最后半块。单次烧录比较基线与0～数百微秒的“VSYNC后异常块组装”成对窗口，并新增pending/drain/collision计数。候选依据是异常内容与帧底部一致、错配固定在帧尾前一块且ISR等待无效；通过人工验证前不得进入`design.md`。
- **验证边界**：纯C按持续授权完全跳过，状态为“未运行”，不是“通过”。

### 2026-08-25 — C16r人工裁决与C16s EOF身份矩阵

- **C16r否决**：用户确认W2～W15所有真人窗口均有同一条约3～4像素横向重叠带，0/50/100/200/400/800µs帧尾等待对位置和宽度无影响；所有画面无闪烁、无随机错位。进一步肉眼核对确认重叠带内容与帧最底部几行一致，故“最后半块VSYNC排空不足”否决，问题转向中间帧组装边界。
- **C16s机制与矩阵**：包装GDMA EOF回调并读取`event_data->rx_eof_desc_addr`。W4/W5保持软件索引；W6/W7改为描述符选源0µs；W8/W9、W10/W11、W12/W13、W14/W15、W16/W17分别为2/5/10/20/50µs；W18/W19回到描述符0µs。固定色块/真人成对，110%亮度、102,400B DMA、51,200B半块、C16o暂停握手、direct60x1不变，全帧重探针关闭。
- **C16s自动证据**：Tank clean build`1446/1446`，应用1,001,776B（`0xf4930`）；应用/bootloader/partition SHA-256为`235440B054A6B79A986BCDCE992935C361D2D58CC1522BB95B1971613B90B1D8`、`40430DB58886A05C8AF90749939374B1BBEA53E91D5860E6383365C80D53FB54`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，构建与备份一致；COM7全片擦除和三段Hash校验成功。`verification/c16s_r1_eof_identity_matrix_tank_com7.log`为123,254B，SHA-256`81243DEFAD3BF0336D6285D0DA6DA0AC6FA643BA0CCA17936032C8C222335079`，覆盖W2～W19并保持至设备约372秒/2,014帧。最终`eof_valid=22155`、`eof_invalid=0`、`eof_mismatch=2017`，错配约每帧一次；pause请求/确认均2,014，所有自动保护错误为0，无重启/WDT。约356秒有2条可恢复`invalid state 1`，之后继续运行且`lifecycle_errors=0`。
- **C16s人工裁决（否决描述符选源）**：W1黑屏；W2软件索引真人仍有窄叠影，W3/W4为色块，W5同W2；W6描述符模式色块开始异常，W7起真人及后续色块/真人均从原窄带向下整体错位，2～50µs等待无改善。直接把EOF描述符当完成半块身份会明显退化，不得作为修复。用户反馈包含W20，但本固件只定义至W19，W20不计入参数证据。
- **当前候选（未验证，仅记忆）**：恢复软件索引作为唯一选源；增加错配所在的51,200B块序号直方图，并只在描述符/软件索引不一致的那次EOF上比较0～约1000µs等待。若等待改善窄带，说明异常EOF提前于目标半块完全可见；若恶化或无差异，再比较忽略/重采该边界。候选未通过前不得写入`design.md`。
- **验证边界**：纯C按持续授权完全跳过，状态为“未运行”，不是“通过”。

### 2026-08-25 — C16q人工反馈与C16r尾部排空矩阵

- **C16q人工裁决（部分通过）**：用户确认W7上下区域均已正常，C16p/C16o固定约1/3高度以下的大范围重叠已消失；只剩一条位置较稳定、约3～4像素高的横向重叠带。C16q因此形成显著且可复核的改善，但尚未达到“无重叠、无撕裂、无闪烁”的最终画面验收，不写入`design.md`作为已验证设计。
- **C16r待验证判断**：锁定C16q的102,400B staged-DVP、51,200B半块、GDMA EOF ISR直接复制、C16o暂停握手、614,400B承载区、cache同步、direct/60行/单次呈现和110%亮度，只在DVP任务收到帧结束VSYNC后、停止GDMA和复制最后半块之前加入任务态尾部排空等待。若窄带来自最后半块尾数据未完全落稳，等待增加应使窄带缩小或消失；该判断仍是候选，不得当作设计结论。
- **C16r单烧录矩阵**：W4/W5为0µs，W6/W7为50µs，W8/W9为100µs，W10/W11为200µs，W12/W13为400µs，W14/W15为800µs；每组偶数窗口为固定色块、奇数窗口为真实人像，W15保持。延时只在任务态执行，ISR和前11个半块复制不变；Remote、控制、音频、传感器帧率与LCD参数未改。
- **C16r构建与擦写**：Tank clean build完成`1446/1446`，应用1,000,096B（`0xf42a0`，分区余量68%）；应用/bootloader/partition SHA-256分别为`04C0B79588A3E5F12106050286014D6ADFE2BE6305FBF363FA9DAD0A8613BD02`、`063B8507F4430E4A72F2C16A5D6FF1388E59CC769904DBCF81911C0C56507FBE`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，构建目录与`firmware_backup`三文件一致。COM7全片擦除、三段`Hash of data verified`和成熟RAM bootloader启动均成功。
- **C16r自动证据**：`verification/c16r_r1_tail_drain_matrix_tank_com7.log`为93,290B，SHA-256`A460FAAF5B27A750C40362A0DECEC377F71FC08001738C0D1404CB82D0B66FED`。日志从W1结束标记开始，覆盖W2～W15；约`180117ms`完成矩阵并保持W15到`330342ms`。最终`display_frame=1710`、`pause_req=pause_ack=1710`、`pause_timeout=0`、`staged_fallback=0`、`staged_errors=0`、`tail_drain=800`、`tail_waits=1467`；每帧11次ISR复制关系保持成立。无Panic/WDT/重启、槽重叠、完成冲突、guard/cache/raw-post或生命周期错误。
- **警告边界**：日志在W3全帧探针后和W12/W13附近共出现4条`dvp_ext: invalid state 1`；这些警告紧邻约593～620ms全帧诊断探针，之后采集自动恢复，`lifecycle_errors=0`且没有超时、HOLD或重启。当前按“可恢复的诊断探针噪声”保留，不隐瞒为零；正式候选收口时应移除全帧探针并复验。
- **纯C豁免**：按用户持续授权完全跳过，状态为“未运行”，不是“通过”。

**当时下一步**：人工比较C16r六组窗口，重点确认那条3～4像素重叠带是否随0/50/100/200/400/800µs单调缩小或消失。该步骤后来已经执行并否决全部延时路线，当前入口以上方C16w最终验收记录为准。

### 2026-08-24 — C16a～C16p 彩色恢复、否定路线与当前C16q入口

- **C16a基准**：保持官方640×480 Gray8，在每个完成帧后执行`stop -> disable -> CPU/LCD读取 -> enable -> start`。自动日志完整进入W1～W7，最终`quiesce=resume=1290`，生命周期、guard、槽重叠、cache和重启错误均为0；人工确认W7黑白画面稳定、无撕裂、无闪烁。
- **C16b彩色恢复**：只切回既有原厂640×480 VYUY表和既有240×180彩色分块转换；自动门禁通过，人工确认W7已有彩色完整画面，但约2秒一次低概率闪烁/错位且整体偏暗。
- **C16c/C16d否定证据**：C16c每次复采丢弃首帧后，W4固定图仍有4种RAW指纹，否决“只坏在首帧”；C16d恢复C16b流程并仅启用WiFi EXTRA IRAM，W4仍有5种、W6有2种RAW指纹，未改善且已撤销该配置。两轮均完整进入W1～W7且无重启或保护错误。
- **C16d人工分窗**：W1黑屏；W2彩色基本正常但有低频闪烁、错位和撕裂；W3彩色色块稳定；W4色块闪烁；W5人像闪烁且频率高于W2；W6色块稳定但仍有低频闪烁、错位和撕裂；W7彩色人像稳定，但亮度低于W2。W4/W5证明WiFi开启仍显著放大异常；W7已关闭WiFi，不能把全部低频现象归因于WiFi热路径。
- **C16e假设与实现**：C16b～C16d每30帧的RAW探针约耗时594ms，随后两条长串口日志原本在DVP已经复采后输出，可能覆盖下一帧采集的大部分时间。C16e撤销EXTRA IRAM，不改传感器、VYUY、帧池、LCD或亮度，只让当前帧诊断和日志在DVP停止态完成后再恢复下一帧。
- **C16e构建与擦写**：Tank clean build`1445/1445`，应用988,720B，SHA-256 `6E21B6A72945ECBDD2BE106E217BC3DAD1D23F058AD37F9D0CB2FEE9C41A714A`；构建日志SHA-256 `331A00A4168D9C30A17F6463B19D3F0E7CB82CA36E4164B2FA3ED75BAFABF6DC`。COM7整片擦除成功，三段均`Hash of data verified`；擦写日志SHA-256 `10222DD161FAC701223EB601A315160E090F9E2C7A54D8CCEBD559F3AA4FDB77`，RAM启动日志SHA-256 `8C05EF2B2A20C94003A06B705CCD84EFFDD02C53A38299930882600F193DE7B9`。
- **C16e自动门禁**：`verification/c16e_r1_full_runtime_tank_com7.log`为47,361B，SHA-256 `53AA655570E7021E7B003B89079A75C038D1AFB4E4AF3A6DF3D5CC06257AFA2C`。日志完整进入W1～W7，约`197865ms`打印sequence-complete并在W7保持到`frame=1200`/`225222ms`；45个探针全部满足`quiesce=resume_before+1`和`resume_pending=1`，后续恢复失败日志为0，故实际暂停/恢复一一对应。Panic/WDT/重启、guard、cache、槽重叠、LCD stale和生命周期错误均为0。W3/W6固定图各7个样本均1种RAW，W4 7个样本为4种RAW，WiFi放大因素仍未关闭。
- **C16e人工裁决（以修正反馈为准）**：W1黑屏；W2彩色人像正常、无低频异常且亮度足够；W3和W6固定图稳定；W4固定图与W5人像严重跳动、撕裂、错位和闪烁；W7彩色人像总体正常且亮度足够，但约在5s、10s、16s、20s随机出现极低频闪烁。C16e只算显著改善，人工门禁未通过。纯C按持续授权完全跳过，状态为“未运行”，不是“通过”。
- **C16f自动证据**：仅启用DVP ISR cache-safe并把应用帧边界回调最小调用图放入IRAM。Tank clean build`1445/1445`，应用988,992B，固件SHA-256`D8128AF45291810E7C18BACDF65E17475553943CA0F0C9FFD42282CE9E47F543`；COM7全片擦除、三段Hash校验和RAM bootloader启动成功。`verification/c16f_r1_full_runtime_tank_com7.log`为46,071B，SHA-256`87ED3C149A57FF3B75338D84DA275263D1C18D6514A49BB3429F9D9D6A4AF0F6`；被动抓取从W2开始，后续进入W3～W7并在W7保持，无Panic/WDT/重启或生命周期错误。纯C未运行（授权跳过），不是通过。
- **C16f否决**：W3固定图7个低频样本为1种RAW；W4的7个样本仍有3种RAW，前两次偏离后才回到W3基准，30帧抽样不足以覆盖全部人工异常。用户确认W4固定色块与W5真实画面仍持续错位、撕裂和闪烁，cache-safe/IRAM未改善主故障，C16g撤销该变量。
- **C16g方案**：恢复C16e调用图，窗口扩展为W1～W16、每窗15秒。增加每帧等距读取16×64B的轻量RAW签名和窗口内unique/change统计；以W4/W5比较重探针，W4/W6/W7比较camera/hold/fixture LCD模式，W4/W8/W9/W10比较0/20/50/100ms停止态帧间空闲，W11验证fixture+100ms组合，W12～W14映射真实画面，W15/W16验证关WiFi恢复。完整矩阵见`design.md`第18.7节。
- **C16g构建与擦写**：Tank clean build`1445/1445`，应用989,840B，固件SHA-256`DF727F8CA22E6E7E102DA5C66AB623C0EF4B1A6AA250A597B3C223812172250F`；COM7全片擦除、三段Hash校验和RAM bootloader启动成功。构建日志SHA-256`74616B7E853F872B242047B15EB8CBAEBBEFCD29A4F10F3D40BBD87BA80153E7`。
- **C16g自动证据**：`verification/c16g_r1_full_runtime_tank_com7.log`为43,113B，SHA-256`1699E8A97BCCC0F0A8AB0751D69E2DDFE2402131D444C833B50BD13080825854`。捕获从设备约62.4秒、W4前开始，获得W3完成统计和W4～W16；sequence-complete约243秒，W16保持到约360秒。所有HEALTH均为guard/lifecycle/cache/overlap/finish-collision错误0，无Panic/WDT/重启。纯C按授权未运行，不是通过。
- **C16g裁决**：固定图W4重探针关、W5重探针开、W6保持LCD、W7内部夹具、W8/W9/W10空闲20/50/100ms均持续多签名，延时无单调收敛；W15关闭WiFi后125帧仅11次变化，显著恢复。重探针、LCD读取/提交和0～100ms空闲候选否决，不再枚举。W11因旧C13夹具只接受1～7号窗口，产生96次`ESP_ERR_INVALID_ARG`且0帧有效样本；自动门禁未完整通过，但该诊断缺陷不推翻其他成对证据。
- **C16h交付证据**：Tank clean build`1445/1445`，应用990,112B，构建日志SHA-256`252B8FBEB40C725851B6F6ADFF63181D258EA4106BA4EC0D6BECE13F57AF3CFB`，固件SHA-256`24423498C9051EE6777F309B5B63B3F2DD3372962310C4D2304E9546705F301B`；COM7全片擦除、三段Hash校验和RAM启动成功。运行日志`verification/c16h_r1_full_runtime_tank_com7.log`为41,021B，SHA-256`6D0AEC784CADDC9299002D1D072680736202E32068735143A9C023643A242B1D`。纯C按授权未运行，不是通过。
- **C16h裁决**：无WiFi固定图W3周期样本稳定；WiFi固定图四模式两轮变化率均约67%～81%，无模式收敛，所有帧`sync_changes=0`。sequence-complete后W16保持至设备约362秒；重启/Panic/WDT/guard/cache/槽/生命周期负向扫描为0。COM7重枚举仍漏抓W1/W2起始标记，因此只把C16h视为缓存假设有效否决，不宣称完整自动门禁通过。
- **C16i停止与路线关闭**：C16i试图比较相机采集任务优先级，但当前公开驱动为直接DVP控制器/GDMA链路，运行态不存在名为`dvp_task`的任务。设备在W2输出`TASK_PRIO_ERROR ... reason=not_found`后按设计HOLD并抑制网络启动；没有把无效变量带入后续窗口。日志`verification/c16i_r1_full_runtime_tank_com7.log`为3,360B，SHA-256`CA0E9EC003A937E708BEC5C9629E163A37BEEB2E28BD051D382994BB429FB969`。后续不得再按任务名调整不存在的DVP任务优先级。
- **C16j矩阵与否决**：改为真实控制器GDMA优先级×SoftAP Beacon间隔的W1～W20矩阵。GDMA优先级没有可重复改善；延长Beacon间隔只在部分窗口降低固定图变化率，未稳定收敛，用户确认最终W20明显变暗，候选否决。运行完整至W20且无重启/保护错误；日志`verification/c16j_r1_full_runtime_tank_com7.log`为51,264B，SHA-256`7338B1FFD2347887C9088CB2BFBC99CF81FF2A978AFFDDE2F2E261F9960168CD`，固件SHA-256`718DAFCD2C23B4C321AC15F737857B2F176C481AFAA7712F98CFF267FADD2E46`。不得退回GDMA优先级枚举或把单独延长Beacon当作已成立修复。
- **C16k机制与自动证据**：加入SoftAP协议/Beacon配置和`rc_video_set_diag_wifi_tsf_guard()`，在DVP停止/禁用后、下一次启用/启动前读取AP TSF，使采集启动避开Beacon前后保护区；只使用有效的`B/BG/BGN`协议组合，TSF异常计数并fail-open。W1～W20完整执行，W20保持到约388秒，自动错误均为0；同轮BGN/200TU/Beacon后20ms/60ms预算固定图变化为`15/59`，说明避让具有改善信号但未达到零变化。运行日志57,371B，SHA-256`D57363A38970E2410549109ACE5DC34FEBB667C12C7A5F565BAD66A9F9847731`；权威擦写日志为`verification/c16k_r1_erase_flash_tank_com7_corrected.log`，SHA-256`527A5CF5E5F7B147F5BE17E359D9DC225A43C47888F562BE220853370A4AA63A`；固件995,696B，SHA-256`08CFDB9739264D646916539A4EF16D27D09A0ECDF007DA5095D472C336751CDA`。
- **C16k构建发布恢复事实**：clean构建命中已记录的GCC 14.2.0 managed esp-dsp `dspi_conv_f32_ansi.c:184`、RTL IRA ICE后，以同一构建图`ninja -j1`续编成功；但续编不会执行`build_rc_tank.ps1`末尾的固件备份发布。第一次擦写因此取到旧C16j应用，已由应用大小/Hash当场识别；随后严格复用脚本既有发布步骤复制bootloader、partition和application并逐项核对源/目标Hash，再重新全片擦除和烧录。上条`corrected`日志才是C16k有效擦写证据。以后凡Ninja续编，烧录前必须执行同一发布步骤和三文件Hash核对，不得直接使用旧`firmware_backup`。
- **C16l重复矩阵**：保持C16k机制，仅集中复验BGN/BG、200TU和相位/采集预算。BGN无避让W4为`35/76`，Beacon后20ms/60ms预算的W5～W7为`10/59`、`12/59`、`21/59`；BG无避让W8为`39/76`，同类避让W9/W10为`13/59`、`16/60`。重复结果确认TSF避让通常降低WiFi固定图变化率，但窗口间仍有波动，不能记录为零异常修复。
- **C16l当前交付**：最终W20为BGN、200TU、Beacon后20ms、60ms采集预算。Tank clean build经已知IRA ICE后的成熟`ninja -j1`续编完成`1445/1445`；严格补做脚本既有三文件发布与Hash核对。应用995,696B，SHA-256`D40C42A20A3F45AC1CF0E03AD0B820FECB801BA66B325738EBC5199B0888A882`。COM7全片擦除、三段Hash校验和RAM启动成功；擦写日志SHA-256`B8FA9BC736B3EE06D18B440544484DB00E874F4BCB54528891DD630B5A98B24E`，RAM启动日志SHA-256`8C05EF2B2A20C94003A06B705CCD84EFFDD02C53A38299930882600F193DE7B9`。
- **C16l运行门禁**：`verification/c16l_r1_full_runtime_tank_com7.log`为56,149B，SHA-256`9FA4A900CA54F0F91473E8B1CDE4C1652C2DD1261E2A54C202AD12F2FE0A1CDF`。日志完整覆盖W1～W20，约`264656ms`完成矩阵，W20保持到约`387618ms`；未出现ESP-ROM二次启动、Panic、WDT、重启或HOLD，guard/lifecycle/cache/overlap/finish-collision/TSF错误均为0。纯C按持续授权未运行，不是通过。
- **C16m人工裁决**：精确TSF相位矩阵没有关闭异常。用户确认W20颜色偏暗且仍有秒级画面异常，W23/W24过亮且仍有秒级异常；W18颜色和亮度正常但撕裂/错位比C16m/W24更频繁的反馈对应后续C16n。C16m精确TSF搜索停止，不形成正式参数。
- **C16n官方staged-DVP裁决**：采用乐鑫`esp_cam_sensor v2.4.0`扩展DVP控制器及32KB配置，实测半缓冲约15,360B、每帧约40次搬运；运行出现多次`invalid state 1`、帧超时和2次槽重叠，人工确认W18彩色画面异常频率高于C16m/W24。日志`verification/c16n_r1_full_runtime_tank_com7.log`为62,141B，SHA-256`7E042F9674EEBD9E49E722DF7D36B4712047897B0EA4D36A83F74BDE8F89B7BE`；应用SHA-256`B0DBD0EBE8C3611775975D036BC06EEC37510749EBE78D95F80EB298B1C658F2`。候选否决。
- **C16o受控staged-DVP实现与自动证据**：薄封装复用锁定的扩展DVP驱动，使用102,400B内部DMA、51,200B半缓冲和每帧12次复制；完成帧置暂停请求，驱动下一缓冲回调返回空并确认暂停，显示完成后再恢复。clean build完成`1446/1446`，应用996,624B；应用/bootloader/partition SHA-256分别为`EABB552FF82AA139F31ECB2C30F39FD05E96E1A1A175C1756F12576BD5848505`、`7D50E29563DF5F682BF4131E61B6F17A73B82F59115F67D9B00FFA9F36143C2A`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，源/备份三文件Hash一致；COM7全片擦除、三段校验和RAM启动成功。
- **C16o运行门禁**：`verification/c16o_r1_full_runtime_tank_com7.log`为89,250B，SHA-256`D5F861EAA3439C6A0C2A0C9C8469EEB6CB57D228945C423011D56F190E27D8C1`。捕获从设备约45秒开始，覆盖W2～W18并保持到约377秒；累计约2,010显示帧，`pause_req=pause_ack=2010`、`pause_timeout=0`，无Panic/WDT/重启、`invalid state`、槽重叠、完成冲突、guard/cache/TSF错误。固定图窗口除切换边界外接近稳定。
- **C16o人工裁决**：用户确认动态摄像头窗口存在固定的约1/3高度分界：上部已经相当稳定、基本具备交付水平；下部持续显示跨帧重叠内容，并以较低频率抖动。照片W14/W15可见分界与240×180预览的60行LCD提交边界一致，但当前尚不能区分采集帧内部混合与动态多分块LCD呈现，C16o仍判定未通过。C16o实测100%亮度稍弱；用户指定下一轮候选统一使用110%，该值尚不是最终产品参数。
- **C16p实现与交付证据**：保持C16o采集生命周期，加入RAW直出/PSRAM整帧RGB画布、30/60/80行LCD分块及同帧1/2次呈现矩阵，并增加显示后RAW轻签名。clean build完成`1446/1446`，应用998,752B；应用/bootloader/partition SHA-256分别为`052E1D16D88A20B3F177D719E2014A11DF0A9EA592BF084864D1AF846345181F`、`F24CA0FD83F3F5743E1C7DEE04C053C25CCF895AAC3BB97B1AAD88C578CBD52C`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，源/备份一致；COM7全片擦除、三段Hash校验和RAM启动成功。
- **C16p自动门禁**：`verification/c16p_r1_full_runtime_tank_com7.log`为90,744B，SHA-256`2581686E1146BB5F7FBEABE09F5E2590E7A63BB18862D0DA62D2BE328A5C39FD`。日志覆盖W2～W19，约`219425ms`完成矩阵并保持W19到约`359553ms`；最终`display_frame=1832`、`quiesce=1832`、`pause_req=pause_ack=1832`、`pause_timeout=0`、`raw_post_err=0`，无Panic/WDT/重启、`invalid state`、槽重叠、完成冲突、guard/cache/lifecycle错误。纯C按持续授权未运行，不是通过。
- **C16p人工裁决**：W09彩色色块偶有下部撕裂；W10～W19均在约1/3高度处存在稳定叠影，以下持续重叠并低频抖动，以上基本正常，亮度基本可接受。异常边界不跟随LCD直出/画布、30/60/80行或同帧重复次数变化，故这些显示变量否决，不再重复。
- **C16q候选实现**：只在诊断专用staged-DVP封装内改变分段消费位置：GDMA EOF ISR立即把已完成的51,200B半缓冲复制到当前PSRAM目标帧，数据事件不再进入只有类型的3项队列；最后一段仍由VSYNC路径完成，帧完成回调、C16o暂停确认、停止/禁用后显示及恢复采集保持不变。窗口恢复为W1～W7，W4/W6固定色块、W5/W7真实画面，W7为WiFi开启的最终保持窗；全部110%亮度、direct/60行/单次呈现。该候选尚待人工画面确认，不作为已验证设计。
- **C16q构建与擦写**：Tank clean build`1446/1446`，应用999,168B（`0xf3f00`，分区余量68%）；应用/bootloader/partition SHA-256分别为`13B5A7A323D93AFB34DF27344A1834F04A20BC252FDAC328FA050F4291162E5A`、`2A5CA49EE592A26D671CB0F9D4A40FF8F8C74DA771C2DAE3B0BCA92EB52545FD`、`376637B0E14B9D9F65C7EA63745C39FAABA15EE750D8AF6AD96B3AFF9A020CFD`，源/备份一致。COM7全片擦除完成，三段均`Hash of data verified`，成熟RAM bootloader入口启动成功。
- **C16q自动门禁**：`verification/c16q_r1_full_runtime_tank_com7.log`为64,529B，SHA-256`404702D6BA2022CB3B6E87684F243B1FB94FDE7A1B7635BCE8EAA02D653746E9`。日志覆盖W2～W7，`98453ms`完成序列并保持W7到`277454ms`；最终`display_frame=1410`、`pause_req=pause_ack=1410`、`pause_timeout=0`。`staged_events=staged_copies=15510`恰为每帧11次，`staged_forward=2820`恰为每帧2次VSYNC，`staged_fallback=0`、`staged_errors=0`；无Panic/WDT/重启、`invalid state`、队列OVF、槽重叠、完成冲突、guard/cache/lifecycle/raw-post错误。纯C按持续授权未运行，不是通过。
- **防止重复与异常回退**：C16c丢首帧、C16d EXTRA IRAM、C16f cache-safe/IRAM、C16g重探针/LCD模式/0～100ms空闲、C16h四缓存模式、C16i任务优先级、C16j GDMA优先级、C16m精确TSF和C16n原样32KB staged-DVP均已实板否决；C7分核、C9 TX功率、C12 burst和C8/C8.1原始staged搬运继续冻结。后续不得回退C15解决的614,400B物理承载区、终端保护、逐帧停采读帧，也不得无对照移除C16o的102,400B staged-DVP、暂停握手和已稳定自动计数。

**当时下一步**：保持板上C16q W7，交用户观察W4/W5/W7；该步骤已由上方2026-08-25人工反馈完成并进入C16r。

**当时待验证判断（不得当作设计结论）**：C16q自动计数证明新的ISR分段路径按11段/帧稳定执行；后续人工已确认大范围重叠显著缩小，但仍有3～4像素窄带，因此完整根因尚未关闭。

### 2026-08-24 — C15 人工七窗口复验与最终停止

- **人工画面**：W1黑屏；W2灰度有画面、重影且内容较小；W3灰度无画面；W4同W3；W5同W2；W6同W3；W7同W2。七个窗口均无撕裂或错位突变。
- **运行证据**：单次启动日志依次出现W1～W7，`sequence complete` 为`194305ms`，后续在W7运行到约`240145ms`/`frame=4080`。`window_2_end` 和 `window_3_end_before_network` 堆完整性均为1；141个终端保护采样均为`guard=1/guard_errors=0`；Panic/Guru/Assert/abort/WDT/堆破坏/重启/download扫描为0。
- **证据备份**：用户附件已原样备份为`verification/c15_stability_manual_runtime_tank_com7_20260824_093819.log`，124,243B，SHA-256 `E28B036D94EB386F1EA87A0E2E2B7D9AAEC2B3FEF178C2B5A09E109FE33145F9`，源文件与副本校验一致。
- **停止裁决**：用户设定的“顺利运行到W7、无重启即停止”在本轮输入日志中已经满足。因此本轮不做无必要的功能修改、重新构建、全片擦除或烧录，避免破坏已验证稳定基线。人工画面只作为未解决事实保留，未扩展到画面根因排查。
- **验证豁免**：纯C验证按用户持续授权跳过，状态为“未运行”，不是“通过”。本轮没有功能代码变更，回归影响矩阵中的状态机、事件时序、帧池/事务、并发任务、资源生命周期、错误出口、公共接口及历史问题均不受影响。

**当时下一步**：保持 C15 稳定化固件并等待画面排查指令；该入口已在同日执行并进入C15.2，随后继续推进到上方C16a～C16l最新记录。

### 2026-08-24 — C15.2 自动门禁与人工画面验收完成

- **执行边界**：用户要求在 C15 无撕裂、无闪烁、无重启的成果上稳步前进，不回到 C6～C15 已排除路线；继续授权最多10轮自动编辑、编译、全片擦除、烧录、启动和抓取日志，并要求候选稳定运行 W1～W7 后停下交给人工。本次 C15.2 使用第1轮即通过，未继续无必要迭代。
- **最小前进**：只把诊断传感器表、DVP 和消费者统一到乐鑫官方 `640×480 Gray8`、`307,200B` 逻辑载荷；官方表本地361组寄存器与归档源逐项一致。继续保留 `614,400B + 256B guard`、直接分块转换、240×180 `(80,0)`、20MHz、60行 chunk、帧池/backup、固定图 RAW 指纹和七窗口时序；未恢复 VYUY、全画布、staged DMA，也未修改 Remote、控制或音频。
- **构建与擦写**：Tank clean build `1445/1445`，应用 `987,072B (0xF0FC0)`，SHA-256 `312172C075A1152EF4F375138918F6B1F9730FDFBA56D84B1AC11FA307A2680E`。COM7 全片擦除成功，bootloader、partition table、application 三段均 `Hash of data verified`，RAM bootloader 启动成功。
- **自动运行证据**：`verification/c15_2_r1_full_runtime_tank_com7.log` 为57,208B，SHA-256 `1A654E3C3E4F9E16BC8846A9E49D76F4EDDB51E0DE8E26F0EEF5A96B58828EE7`。日志完整进入W1～W7，约`197569ms`打印 sequence-complete 并继续保持W7；67个保护采样全部`guard=1/guard_errors=0`，Panic/WDT/重启、堆、cache、重叠和所有权错误扫描均为0。W3/W4/W6各9个固定RAW样本均为唯一签名，三窗口共同签名保持一致，C14.1 的WiFi RAW损坏未回归。
- **人工画面**：W1黑屏；W2稳定黑白摄像头画面、无撕裂、无闪烁（用户图1）；W3稳定黑白测试色块、无撕裂、无闪烁（用户图2）；W4同W3；W5同W2；W6同W3；W7同W2。人工结果与窗口设计及自动稳定性证据一致。
- **验收裁决**：C15 既定画面异常排查固件已实现从W1到W7稳定运行，并恢复稳定可辨识摄像头画面和测试色块；本任务到此结束。该结论不表示正式彩色240×180、Remote或30分钟产品长稳已经通过。
- **帧率边界**：用户表示后续 Remote 肉眼稳定12fps即可，并可考虑把摄像头降至约15fps；该信息作为后续性能阶段的过程边界保留，不是本轮正式需求变更。当前诊断日志包含全帧RAW CRC开销，不能据此证明产品摄像头或Remote实际帧率。
- **验证豁免**：纯C验证按用户持续授权完全跳过，状态为“未运行”，不是“通过”。

**当时下一步**：保持C15.2固件并等待后续彩色画面排查指令；该停止点已由同日用户指令解除，并进入下方C16、最终推进到上方C16a～C16l最新记录。

### 2026-08-24 — C16 正式彩色目标恢复执行

- **恢复指令**：用户明确要求不要在C15.2阶段停止，继续朝既有正式需求目标前进；该指令解除上方C15.2的阶段停止入口，不构成新的正式原始需求。
- **当时方案**：先以C16a保持Gray8并验证完成帧后的DVP `stop/disable -> CPU/LCD读取 -> enable/start`生命周期；通过后C16b只恢复既有原厂VYUY表和已验证彩色分块转换。该方案已执行，完整过程与回归影响矩阵见`design.md`第18.7节。
- **验证边界**：两步均复用W1～W7、固定图RAW、guard、堆/所有权/cache和重启扫描；每轮烧录前必须全片擦除。纯C按持续授权完全跳过，状态为“未运行”，不是“通过”。

**当时下一步**：实施并自动回归C16a，通过后进入C16b；该计划已经执行，并继续迭代到上方C16a～C16l最新记录。

### 2026-08-21 — C15 稳定化完成（W1～W7）

- **授权与停止条件**：用户授权本任务完整编辑、编译、全片擦除、烧录、启动和自动日志抓取，最多10轮，并明确授权完全跳过纯C验证；目标仅为既有C15画面异常排查固件稳定运行W1～W7，成功后停止，不继续画面问题。本次使用2轮实板迭代后提前停止。
- **C15.1根因复核**：以当前源码重新构建ELF并解码自动/人工日志回溯，失败链为`window_2_end -> heap_caps_check_integrity_all -> multi_heap_check -> tlsf_check/block_next`，确认堆元数据在W2边界检查前已损坏。公共窗口时序、帧池所有权、LCD生命周期和错误出口不变。
- **稳定化Round 1**：每个40,000B帧后增加256B保护，逻辑载荷和DVP参数不变。保护从`frame=1`起即被图像样值覆盖，到`frame=600`累计25次错误，随后W2堆检查再次LoadProhibited；这排除“仅堆检查与活动DMA不兼容”，确认采集写入实际越过40,000B并继续触及堆元数据。运行日志`verification/c15_stability_r1_runtime_tank_com7.log`为24,224B，SHA-256 `2BBC9797EDC29DA3E7132750EBF3D1C5630B8EA6BE0C38DF57B7CA74ECC6E35E`。
- **稳定化Round 2最小修正**：仅把C15每个DMA槽和backup的物理承载区扩大为既有稳定VGA最大帧容量`614,400B`，末尾附加`256B`终端保护；DVP申明、Gray8转换、RAW探针和上层消费者仍使用前`40,000B`，W1～W7时序、WiFi开关、test-pattern、帧池/backup和LCD presenter未改。该修正隔离越界写入，但不等于确认实际Gray8帧长或低带宽对照成立。
- **构建**：两次clean构建分别在ESP-IDF `esp_lcd_panel_rgb.c`和managed `esp-dsp`的IRA pass遭遇已知GCC 14.2.0瞬态ICE；保留同一构建图并按已记录策略以Ninja `-j1`增量续编后通过。应用`986,256B (0xF0C90)`，SHA-256 `95DBDC59F81AA9E65266371DA3A305ABFA8ACCDF1463BB2D718864C16BE0FADC`；成功日志`verification/c15_stability_r2_build_tank_j1.log`。
- **擦写与启动**：COM7识别ESP32-S3 rev0.2/MAC`44:1b:f6:f3:ae:78`；全片擦除成功，bootloader、partition table和application三段均`Hash of data verified`，随后成熟`load_ram bootloader.bin`入口启动成功。证据为`verification/c15_stability_r2_flash_tank_com7.log`和`c15_stability_r2_load_ram_tank_com7.log`。
- **自动运行证据**：`verification/c15_stability_r2_runtime_tank_com7.log`为108,026B，SHA-256 `748956E0CF9DA9076F1AF960127F0C03FA96F9EFFAE5DA29C233FA38872B716A`。235秒内W1～W7七个标记各出现一次，`low-bandwidth raw-source isolation sequence complete`后继续保持W7至`frame=3960`；W2/W3边界堆完整性均为1，137个保护采样全部`guard=1/guard_errors=0`，帧池重叠、finish collision、stale和cache同步错误扫描均为0，Panic/Assert/Guru/WDT/重启/download扫描为0。
- **验证边界**：按用户授权完全未创建、未编译、未运行纯C验证，不得表述为纯C通过。本次通过只覆盖C15诊断固件W1～W7短时稳定运行；未验证画面改善、实际DMA写入范围、40,000B低带宽假设、正式彩色240×180、Remote链路或30分钟长稳。

**当时下一步**：保持 C15 稳定化固件、不进入 C15.2；该停止点后来被用户的新画面排查指令解除，并已由上方 C15.2 记录完成。

### 2026-08-21 — C15.1 人工回归未通过与恢复点（历史）

- **人工反馈**：W1 黑屏；W2 显示全灰色摄像头画面，并伴随闪烁、撕裂和异常，随后设备卡死并重启；W3～W7 未进入。
- **执行状态**：C15.1 Tank clean build `1445/1445`，应用 `985,616B (0xF0A10)`；COM7 已完成全片擦除、三段烧录及 Hash 校验，并通过既有 RAM bootloader 入口启动。板上保持 C15.1 固件。
- **日志证据**：自动运行日志为 `verification/c15_1_r1_runtime_tank_com7.log`（SHA-256 `AEEF908828852F4AF127FD044BA9584C58EB0E1067FDFF3CA440F6C93218CFD8`）；人工日志已从附件原样备份为 `verification/c15_1_manual_runtime_tank_com7_20260821_170556.log`（40,060B，SHA-256 `BD1B0E04233DD539BFD83FE3A7FC6F3316C05CB2F254AA7EBE736BEC21D9FB85`，源文件与副本校验一致）。构建、烧录和启动证据分别为 `c15_1_r1_build_tank.log`、`c15_1_r1_flash_tank_com7.log`、`c15_1_r1_load_ram_tank_com7.log`。
- **日志记录边界**：自动日志与人工日志均在 W2 约 `frame=600` 后停止正常推进；已记录的失败检查点为 `window_2_end`，随后发生 Panic/Assert 并重启。本轮只归档事实，不开展原因分析。
- **验证边界**：纯 C 验证按用户持续授权跳过，状态为“未运行”，不是“通过”；C15.1 七窗口回归未完成，也未形成或批准 C15.2。

**当时下一步**：重启窗口后从C15.1日志和W2失败点继续；该入口已由上方“C15稳定化完成”记录执行并关闭，当前停止入口以上方最新状态为准。

### 2026-08-21 — C15 官方 200×200 Gray8 实施与烧录

- **验证目的**：把传感器至 PSRAM 的完成帧从 C14.1 的 `640×480×2=614,400B` 降为 `200×200×1=40,000B`（约原来的`1/15.36`），重复固定图 WiFi 关/开/关窗口。若窗口4 RAW 离群显著下降则支持带宽/PSRAM写入压力假设；若仍随WiFi以相近幅度恶化，则继续检查信号同步、DVP DMA和PSRAM/cache完成边界。
- **最小实现**：`camera_hal` 增加零值兼容的传感器模式枚举和乐鑫 `esp_cam_sensor v2.4.0` 官方 Gray8 表；本地368组寄存器已与归档源逐项比较，`368/368`一致。`laiwfs300`新增灰度初始化入口，原VYUY入口不变；诊断DVP改为`CAM_CTLR_COLOR_GRAY8`和40,000B帧。
- **显示与探针**：新增 Gray8→RGB565_BE 最近邻缩放，仍输出240×180 `(80,0)`并沿用逐行镜像、20MHz、60行chunk、公共submit/wait。七个30秒窗口、100ms settle、test-pattern bit7、`rc_capture_pool + backup`、burst64、`raw_full/raw_q/raw_edge`、cache/generation和chunk CRC/提交完成探针均未改变。
- **回归影响边界**：状态机、窗口时序、事务/序号、帧池所有权、错误出口和LCD资源生命周期不变；公共接口只做加法，其他调用方的零值模式仍为640×480 VYUY。JPEG/UDP/video_tx、Remote、摇杆/电机、控制、帧率和音频未修改。剩余风险是灰度寄存器表、传感器实际输出和DVP 40,000B帧长需实机日志确认。
- **验证豁免**：按用户持续授权完全跳过纯C验证，未创建、未编译、未运行，不表述为通过。实机七窗口回归也按本轮要求暂不执行。
- **构建**：成熟`build_rc_tank.ps1 -Role tank -Clean`首次调用端60秒超时，但底层Ninja继续并生成镜像；按`design.md`允许的同入口重试后完整`1445/1445`通过。配置为`CONFIG_RC_TANK_ROLE_TANK=y`、`CONFIG_RC_TANK_CAMERA_DIAG_C=y`，应用`983,376B (0xF0150)`，SHA-256 `85FCCB7189BF7771CB2246D99427A370F482E1B0FF5E6586994D40F5F5B73C93`。
- **擦写与启动**：COM7识别ESP32-S3 rev0.2/MAC`44:1b:f6:f3:ae:78`；全片擦除成功，bootloader、partition table、application三段均`Hash of data verified`并硬复位。成熟`load_ram bootloader.bin`入口执行成功，COM7已释放。
- **证据**：`verification/c15_build_tank.log`保留首次调用端超时前输出，成功证据为`verification/c15_build_tank_retry.log`、`c15_flash_tank_com7.log`和`c15_load_ram_tank_com7.log`。

**当时下一步**：用户手动观察并抓取 C15 七窗口日志；该步骤已进入 C15.1 并在 W2 卡死重启，当前入口以上方 C15.1 记录为准。

### 2026-08-21 — C13 裁决、C14 主动重启定位与 C14.1 烧录

- **C13 人工结果**：窗口1黑屏；窗口2真实图无 WiFi 正常；窗口3真实图+WiFi出现与C6/C7同级异常；窗口4 WiFi开启且DVP持续采集时，内部DMA色条/斜线/块号图稳定、无抖动和撕裂；窗口5切回真实图再次异常；窗口6 WiFi关闭的内部图稳定；窗口7真实图恢复正常。
- **C13 自动证据**：真实窗口 prepare 约`46.4～46.8ms`；内部图窗口约`9.9ms`。内部图首/中/尾chunk CRC在窗口4/6各自保持固定；所有采样均`stable=1`、`cache_sync=ESP_OK`、`cache_sync_err=0`、`submit=complete=3`、提交/完成sequence一致、`stale=0`。
- **C13 结论**：公共LCD presenter、SPI DMA、chunk复用与完成等待在同一WiFi+DVP负载下可稳定工作，故不再作为主分支；故障范围收敛到真实传感器输出、DVP raw、PSRAM/cache可见性或raw读取/转换。该结论不能单独区分raw→RGB转换与共同上游。
- **C14 方案**：七个30秒窗口依次为黑屏、真实/WiFi关、固定传感器图/WiFi关、固定图/WiFi开、真实/WiFi开、固定图/WiFi关、真实/WiFi关保持。DVP全程不重启；低频记录`raw_full`、四段`raw_q`、逐行首尾`raw_edge`、三段`chunk_crc`和LCD提交/完成序号。
- **C14 首轮结果**：窗口2人工反馈基本正常，仅很低概率错位/撕裂。自然场景采样均generation前后相同、`stable=1`、cache同步成功、LCD提交完成一致；bitwise全帧指纹单次约`623ms`，只视为诊断数据，不用于帧率评价。
- **C14 重启根因**：test-pattern切换时日志为`P1:32 before=0x15, write=0x95, read=0x80`，屏幕已出现彩色块，证明官方bit7已生效。初版错误要求整个读回字节等于`0x95`，返回`ESP_ERR_INVALID_RESPONSE`；上层按既有关键初始化失败策略等待5秒后主动`esp_restart()`，复位原因为`RTC_SW_CPU_RST`，无Panic/Guru/WDT/brownout。
- **C14.1 单变量修正**：保留官方read-modify-write，只把成功条件改为检查`P1:0x32[7]`；I2C错误、Page 0恢复错误或bit7真正不匹配仍失败。DVP、帧池、backup、转换、CRC、LCD presenter、窗口、WiFi、Remote、控制和音频均未改。
- **C14.1 自动交付**：按持续授权完全跳过纯C，未表述为通过。Tank clean build`1445/1445`，应用`982,464B`，SHA-256 `2B6CDFA24238C781621C46DFAFD910DF155CCDD505AB13E4689B8E9F00AC9F01`；COM7全片擦除成功，bootloader/partition/application三段均`Hash of data verified`，成熟`load_ram`入口启动成功。证据为`verification/c14_1_build_tank.log`、`c14_1_erase_tank_com7.log`、`c14_1_flash_tank_com7.log`和`c14_1_load_ram_tank_com7.log`。
- **C14.1 人工结果**：窗口1黑屏；窗口2基本正常但偶发闪烁/错位；窗口3固定色块正常对齐，约每1～2秒偶发撕裂；窗口4固定色块明显撕裂、错位和闪烁；窗口5真实画面明显撕裂和闪烁；窗口6固定色块仅偶发闪烁，接近窗口3；窗口7真实画面偶发闪烁和撕裂。
- **C14.1 RAW证据**：窗口3固定图9/9样本为同一`raw_full/raw_q/raw_edge/chunk_crc`；窗口4的9个样本中4个保持该基准、5个形成不同RAW指纹，变化分区与转换chunk CRC同步改变；窗口6的9个样本中8个保持本次启用基准、1个RAW/chunk同时偏离。全部样本均`stable=1`、`cache_sync=ESP_OK`、`cache_sync_err=0`、提交/完成与sequence一致、`stale=0`，无Panic/Guru/WDT/brownout或重启。
- **C14.1 结论**：异常内容在消费者取得完成帧时已经存在，RAW→RGB转换是在反映已损坏RAW，不是当前主根因；WiFi将固定图抽样偏离从关闭后的`1/9`放大到开启时的`5/9`，但关闭时仍有低频异常，因此WiFi是显著放大因素而非唯一触发源。当前主分支为传感器输出、DVP DMA写入或PSRAM/cache完成帧形成。
- **诊断限制**：raw探针约每3秒采样一次，抽样偏离率不等同于肉眼闪烁频率。窗口3与窗口6是两次独立启用固定图，众数指纹不同；只按各窗口内部众数识别离群，不将跨启用差异直接认定为损坏，`P1:0x32`非bit7回读语义留待后续审计。
- **授权和边界**：用户持续授权编辑、编译、全片擦除、烧录和复位，并持续授权完全跳过纯C，直到明确撤销；不包含Git。用户手动观察画面并抓日志。本阶段不改Remote，不处理帧率，不实现音频。

**当时下一步**：等待用户确认 C15；该方案已确认、实施并烧录，当前入口以上方 C15 记录为准。

### 2026-08-20 — A0～C12 阶段总结、路线审计与暂停点

- **完整记录**：逐轮变更点、验证目的、自动数据、人工结果、有效性审计和下一步计划已集中保存到 [`tanklog/2026-08-20_rc-tank-camera-debug-a0-c12-summary-and-next-plan.md`](../../../tanklog/2026-08-20_rc-tank-camera-debug-a0-c12-summary-and-next-plan.md)。用户口述“到C11”，但实际最后一次日志和反馈是C12，文档按实际证据覆盖到C12。
- **A/B阶段**：A2证明`rc_capture_pool + backup`不是主要根因；B/B2与B2.1的交叉证明整帧PSRAM画布/复制会显著恶化，直接分块转换更稳定；B3.1在240×180右上角几何下约`11.88fps`显示、`25.25fps`采集，人工仅约25s一次低频异常，是今天无WiFi最佳基线。
- **C阶段关键结论**：C2排除电机；C3证明SoftAP单独运行即可触发Tank本地异常；C6证明WiFi关闭后无需重启DVP即恢复。C9在20/11/2dBm下均异常，排除TX功率；C7分核无改善；C12 burst=32与C6/C7约`25.25fps`且现象相同，关闭burst粒度假设。
- **Remote边界**：C中Tank在Remote连接前已经异常；Round2抽样同序号Tank发送前与Remote重组后JPEG CRC一致。Remote会进一步恶化，但当前不是源头，不与Tank同时修改。
- **路线审计**：总体“WiFi运行态与raw YUV/PSRAM/DMA/cache链竞争”方向仍正确，但C9以后参数枚举信息增益下降。C4初版和C8没有形成有效采集；C10未经可靠寄存器依据且实际改变图像组织；C11 burst=128违反外部内存最大64限制；C12合法但基本重复C6/C7。后续不再重复这些试验。
- **当前未决边界**：现有`raw_stable/size/identity`和帧率只证明槽持有与帧完成，不能证明像素正确。必须先区分DVP raw/PSRAM/cache/转换与LCD presenter/SPI DMA两条责任链。
- **待确认设计**：C13在同一“摄像头持续采集+WiFi”负载下，只把LCD输入在真实摄像头与内部DMA确定性合成图之间切换。合成图稳定则转raw分支；合成图也异常则转LCD分支。方案详见`design.md`第18节和`todo.md`。
- **执行授权**：用户持续授权本专项编辑、编译、全片擦除、烧录和复位，并持续授权完全跳过纯C验证，直到用户明确撤销；不包含Git。用户手动测试和抓取日志。当前用户要求先总结、暂不改下一步，因此本轮未修改代码、未构建、未擦写。
- **优先级**：先完成Tank图像源头稳定，再完成Tank 240×180、Remote传输/显示和控制；帧率优化暂缓；音频优先级最低且暂不实现。

**当时下一步**：等待用户确认`design.md`第18节的C13边界二分方案；该方案已于2026-08-21执行并进入C14.1，当前入口以上方最新状态为准。

### 2026-08-20 — 历史：摄像头画面专项 A0 方案确认与执行入口

- **问题边界**：问题 A 先限定在 `DVP raw -> Tank 自检 LCD`；问题 B 在 Tank 自检稳定后再验证 JPEG、UDP、Remote 解码和 LCD。
- **已批准方案**：A0 先保留当前 `rc_capture_pool + backup` 和 240×180 直显，只关闭 motor/audio/network/control/video_tx；若 A0 失败，A1 才切换 `camera_display_demo` 的 `active_fb/locked_fb` 与全屏 portrait；若 A1 通过，再用相同全屏几何的 A2 单变量交叉区分所有权与几何。阶段 B 统一 presenter，阶段 C 在 preview quiescence 后按音频、WiFi/控制、JPEG TX 单变量恢复。
- **正式界面边界**：Remote 的 240×180 右上角 `(80,0)` 产品布局不变；Tank 全屏仅是诊断模式，不作为正式 Remote 布局需求变更。
- **已知问题矩阵**：Round4 的采集池/backup 所有权修复由 A0 原样保留；现有 VYUY、LCD 直显、JPEG/UDP 与 Remote 路径均不改。Round4 直显日志在联网后的 `video_tx` 上实际出现两次 Task WDT，旧摘要遗漏；A0 将任何 WDT 作为失败。preview→TX 无 quiescence 的竞争风险在 A0 中通过不启 TX 隔离，留待阶段 C 修复。
- **当时验证门禁**：A0按当时规则执行最小纯C回归。此项后来已被`coding_rule.md`“人工明确授权时可完全豁免”和用户持续授权取代；当前专项允许完全跳过纯C，不能再沿用本条作为当前门禁。
- **授权状态**：用户授权本专项持续编辑、编译、全片擦除、烧录和复位，直到明确命令退出；不包含 Git 操作。授权当前继续有效。
- **当时Todo**：执行清单为[`todo.md`](todo.md)；该文件现已更新为C13待确认入口，本条只保留A0阶段的历史安排。
- **分析依据**：完整三场景链路和差异见根目录 `tanklog/2026-08-20_rc-tank-camera-image-corruption-analysis.md` 第 11～14 节。

### 2026-08-20 — 历史：A0 实施与自动证据

- **实现边界**：新增 `rc_tank_startup_policy` 并由 `app_main.c` 的真实 Tank 启动点使用；A0 只启动板级、显示、真实摄像头和本地预览，禁用 motor/audio/network/control/video_tx，摄像头失败不回退合成源。`rc_video.c`、采集池、backup、VYUY、240×180 `(80,0)` 直显和 LCD 生命周期未改。
- **RED/GREEN**：新增策略测试后先因 `rc_tank_startup_policy.c` 不存在而失败；最小实现接入后 `tests/run_host_tests.ps1` 20/20 通过，证据为 `verification/stage_a0_host_tests.log`。
- **构建**：成熟 `build_rc_tank.ps1 -Role tank -Clean` 通过 `1433/1433`；配置确认 `CONFIG_RC_TANK_ROLE_TANK=y`、`CONFIG_RC_TANK_CAMERA_DIAG_A0=y`。应用 1,122,000B，SHA-256 `4C5EDC032EAD969A5540D49EA47D635808DD18AE961AB3CC0E4B12933141BAA2`；日志为 `verification/stage_a0_build_tank.log`。
- **擦写与启动**：COM7 全片擦除成功，bootloader/partition/application 三段均 `Hash of data verified`；使用既有 `load_ram bootloader.bin` 入口启动并被动抓取，证据为 `verification/stage_a0_flash_tank_com7.log`、`stage_a0_load_ram_tank_com7.log`、`stage_a0_tank_com7.log`。
- **自动结果**：A0 marker 确认五类并发均关闭；日志连续到 360 个显示帧，稳态预览约 `9.87fps`、采集约 `25fps`，抽样 `raw_stable=1`，`size_err=0`、`identity_err=0`；未见 WDT、Panic、Guru、abort、DMA timeout/out-of-bounds。串口抓取进程已退出。
- **待人工**：自动证据不能替代画面观察。用户反馈前不进入 A1/A2 或 Remote；若 A0 仍异常，执行 A1；若 A0 正常，则优先把根因范围收敛到被移除的并发/切换窗口。

**当时下一步**：用户观察 Tank A0 的 240×180 右上角画面；该步骤已完成并进入A1～C12，不再是当前入口。

### 2026-08-19 — Round 1/2 摄像头调优与双端实机验证

- **任务授权**: 最多 10 轮自动编辑、纯 C 验证、双角色构建、全片擦除、烧录和日志；达到自动目标后提前停止。未执行 Git 操作。
- **Round 1 修改**: 对齐 `camera_display_demo` 的 VYUY→RGB565_BE 颜色契约，移除 Remote 二次红蓝交换；Tank/Remote 显示初始化使用 80 行 DMA 缓冲，Tank 20MHz；测试契约更新到 Round8 已确认摇杆几何。18 项纯 C 全部通过后才构建/烧录。
- **Round 1 证据**: Tank clean build `1431/1431`，Remote clean build `1431/1431`；COM7/COM24 均全片擦除、三段 Hash 校验成功。COM7 用成熟 `load_ram bootloader.bin` 入口启动；日志显示 Tank 采集约 25.3fps、发送约 12.5fps，Remote 20MHz 显示约 8.68fps，完整帧 `missing=0`，无 DMA/JPEG/越界/Panic。Remote 20MHz 未达 10fps 目标，进入 Round 2。
- **Round 2 单变量**: 保持像素格式、UDP 分片、DMA 缓冲、摇杆和 PSRAM 布局不变，仅恢复 Remote 40MHz；Tank 保持 20MHz。RED 先由显示计划纯 C 暴露，补充角色级时钟常量后 18 项全部通过。
- **Round 2 构建/烧录**: Tank `1431/1431`，应用 `0x101860`（1,054,816B）；Remote `1431/1431`，应用 `0x1123e0`（1,123,296B）。COM7/COM24 再次全片擦除并完成三段 `Hash of data verified`。
- **Round 2 稳定日志**: Remote 复位后重新连接 `RC_TANK_F3AE25`，日志确认 `pclk=40000000`、理论上限 32.55fps；16 个显示窗口为 11.58–11.90fps、平均 11.79fps，平均解码约 21ms、DMA 33.5ms、总显示段约 52ms、总处理约 84–86ms；`Video RX complete` 持续 `missing=0`，`queue_drop=0`。Tank 采集约 25.2fps，17 个发送窗口为 12.6–13.0fps、平均 12.81fps，`capture_size_errors=0`、`capture_identity_errors=0`。
- **瞬态边界**: Round 2 首次同时采样落在重烧录后 Remote 尚未关联时，控制发送出现 `ESP_ERR_INVALID_STATE` 高频日志；复位 Remote 后链路稳定，未据此修改控制协议或引入无关日志优化。
- **Round 2 结论**: 真实摄像头数据链路已达到 10fps 自动目标，应用层分片和 LCD 稳定基线有效。JPEG 18 个窗口平均 `16,684.83B`（范围 `15,003–19,062B`），平均 8–12KB 目标未达，样本峰值 <20KB；端到端时延因缺少跨设备时间戳仍不可审计。颜色/撕裂需要用户目视，30 分钟长稳和电量标定仍未完成。

### 2026-08-19 — 摄像头调优 Round 3/4 与提前结束

- **轮次命名边界**: 本节 Round 3/4 属于 2026-08-19 摄像头调优任务；文档后部 2026-08-17 性能实验中的同名轮次是另一段历史序列。
- **Round 3 单变量与结果**: JPEG 质量从 Q60 降至 Q45；18 项纯 C、双角色 clean build、COM7/COM24 全片擦除与三段烧录均通过。Tank 稳态发送平均 `13.55fps`，JPEG 平均 `14,126.74B`（`12,568–15,587B`）；Remote 稳态显示平均 `11.99fps`，`missing=0`。平均 8–12KB 目标未达，进入 Round 4。
- **Round 4 RED/GREEN**: 测试先改为期望 Q30，源码仍为 Q45 时 `test_rc_video_format exit=13`；仅将 `RC_VIDEO_JPEG_QUALITY` 改为 `30U` 后，`tests/run_host_tests.ps1` 的 18 个程序全部 `exit=0`。LLVM-MinGW clang 22.1.8，目标 `x86_64-w64-windows-gnu`。
- **Round 4 回归边界**: 只改变 Tank 软件 JPEG 编码质量参数；分辨率、VYUY/RGB565_BE 颜色契约、UDP 1200B 分片、PSRAM 槽、Remote 最新完整帧队列、LCD 40MHz/60 行 DMA、摇杆、控制保活、电机、音频和公共协议均未修改。现有纯 C 覆盖相关历史问题。
- **Round 4 构建/烧录**: Tank/Remote clean build 均 `1431/1431`；应用分别为 `0x101860`（1,054,816B）和 `0x1123e0`（1,123,296B），角色配置正确。COM7、COM24 均先全片擦除，bootloader/partition/application 三段均 `Hash of data verified`；COM7 使用成熟 `load_ram` 入口启动。
- **Round 4 Tank 稳态**: 采集平均 `25.25fps`（`24.9–25.7fps`），发送约 `14.4fps`，编码平均 `27.3ms`，总处理平均 `68.2ms`；JPEG 平均 `11,495B`，范围 `10,037–13,677B`，`capture_size_errors=0`、`capture_identity_errors=0`。
- **Round 4 Remote 稳态**: `pclk=40000000`，显示平均约 `12.30fps`（`12.04–12.43fps`），总处理平均约 `81.0ms`；末值 `complete=810, missing=2`，`missing=2` 仅在启动关联窗口形成，后续不再增加；`queue_drop=0`。未见 JPEG 解码失败、DMA 越界/超时、Panic/Guru/abort 或内存分配失败。
- **启动瞬态**: 双端擦写后 Remote 尚未关联 Tank AP 时，Tank 有 258 条发送失败（主要为 `errno=12`，短暂伴随 `ESP_ERR_INVALID_STATE`）；关联建立后恢复约 `14.4fps` 稳态发送。该现象与 Round 2 的重烧录关联窗口一致，本轮不修改稳定运行链路。
- **产物证据**: Tank/Remote 应用 SHA-256 分别为 `04AC9D4DDF63900063C7A171808A638BA3BE2E8DFD54C106F76F3BA76A909EF1`、`5B8F0C8AFA5D3C54A4573C77E9B2AC345017A554B36BCAA23144BDAE6F3BDBE9`；运行日志 `verification/round4_tank_com7.log`、`verification/round4_remote_com24.log` 的 SHA-256 分别为 `D9D134E290975572A0B8A6FEAA3E01BEA7E042C2845D210023689BDEEAAF9623`、`156B1D84AF2A950A6F9B22069A7EE588B972243ADBAEEE5CD8E531F77C3DD278`。
- **停止结论**: Q30 已同时满足平均 8–12KB、峰值 <20KB、Tank/Remote ≥10fps 和短时稳定日志目标，因此在第 4 轮提前结束，不进入 Round 5–10。真实画质、颜色、撕裂、机械四向控制、端到端时延和 30 分钟长稳不能由日志替代，仍待人工验收。
- **文档冲突**: `requirements.md` 的正式参数仍为 Q60，源码和当前设计为 Q30；未把实现反向改写为已确认需求，保留为待用户确认项。本轮未执行 Git 操作。

**历史基线** (2026-08-18 — 摄像头/摇杆/持续控制/Tank 字形修复):
- **根因与修复**:
  - 摄像头扫描曾误选触摸地址 `0x15`；现固定板级地址 `0x21` 并强制 ID=`0x0A39`，失败时释放 I2C device，空 DVP 控制器不启动。
  - Remote 摇杆曾依赖视频帧解码后绘制；现由唯一 LCD 合成任务独立刷新，零视频和停流时仍可见/可响应。
  - 非 STOP 控制曾只发一次；现 20ms 检查、变化立即发送、任意当前命令最长 100ms 保活、松手立即 STOP。
  - Tank 字库曾把列数据按行解释且颜色契约不统一；现修正 `5x7` 数字/百分号并统一 LCD BGR565 转换。
- **自动验证**: 11/11 纯 C 回归通过；Remote/Tank clean build 均为 `1425/1425`，角色配置核对正确。Remote 应用 `0x111b40`，Tank 应用 `0xfff40`。
- **Tank 烧录**: COM7 全片擦除成功，bootloader/partition/application 三段均 `Hash of data verified`。Tank 应用 SHA-256：`1FAA39E26DC6B2AD7B14726F840821485D0A30D1E3CCF4F9715F6B3AAF705BB2`。
- **Tank 自动日志**: `verification/ex035_20260818_tank_com7_ram_boot.log`（SHA-256 `B6CE40D051BAE7ED6A17DCD7E61106B7E52634721582474E81664595B224B9CF`）确认 TANK 角色、SP0A39 `0x21/0x0A39`、显示/相机/音频/SoftAP/控制正常启动，无应用重启或 Panic。
- **COM7 已知硬件问题**: 常规 hard reset 后无日志；`load_ram bootloader.bin` + `serial_capture.py` 被动模式可正常启动。继续人工检查 GPIO0/BOOT 复位采样。
- **任务上限结果**: COM24 在第 4～15 轮均未枚举，PnP 历史项为 `Present=False`。Remote 固件已保存但未擦除/烧录；本轮双端连接、帧率、白屏/撕裂和持续控制无新实板证据。

### 2026-08-19 — Round8：原尺寸右上角视频、分块摇杆合成与阶段日志

- **用户授权范围**: 快速迭代，跳过纯 C 验证；允许多轮编辑、双角色构建、全片擦除、烧录和复位；测试由用户手动完成。本轮未抓取串口日志，烧录后已释放串口资源。
- **Remote 视频**: 保持网络 JPEG `240x180` 原尺寸，写入 `320x240` 黑色画布右上角 `(80,0)`；删除每帧 `composite_buf` 和整帧复制，减少约 `153600 B/帧` 的 PSRAM 复制。LCD 仍按 60 行 DMA chunk，摇杆在 chunk 内裁剪绘制并在 DMA 完成后再提交下一块。
- **Remote 摇杆**: 保留初次触摸捕获、拖出后限幅、释放回中和 100ms 控制保活；新增 `rc_joystick_render_overlay_region()`，不改变方向映射或 UDP 控制协议。
- **合成视频**: `rc_video_synthetic_fill_ycbycr()` 每 8 帧改变色块相位并保留移动标记，供用户观察跨帧边界和撕裂。
- **阶段日志**: Remote 每 30 个显示视频帧记录 `recv/decode/color/place/copy/overlay/dma/display/total`，并记录 `ready/queue_drop/stale_drop`；初始化记录 JPEG 槽、解码/画布/DMA 缓冲大小、PSRAM/内部 RAM 余量和内存属性。Tank 记录 subsample/JPEG/send 缓冲和 PSRAM/内部 RAM 余量。已验证路径没有新增高频逐帧日志。
- **构建**: `build_rc_tank.ps1 -Role tank -Clean` 与 `-Role remote -Clean` 均完成 `1431/1431`；Tank 应用 `1051072` bytes，Remote 应用 `1123392` bytes。镜像保存于 `firmware_backup/*_TANK.bin` 与 `*_REMOTE.bin`。
- **烧录**: COM7 与 COM24 均先 `erase_flash` 成功，再写入 bootloader、partition table、application；每端三段均出现 `Hash of data verified`，最后由 esptool 硬复位。首次旧脚本失败原因是不存在的 `Scripts\\esptool.py` 路径，后改用同一环境的 `python -m esptool`（历史验证入口）成功完成。
- **验证边界**: 未运行纯 C（按用户本轮明确授权跳过），未抓取运行日志；不能据此宣称实际帧率、撕裂已消除、画面颜色正确、机械方向正确或 PSRAM 时延已达标。

#### Round8 用户实测与日志统计（2026-08-19 16:29～16:36）

- **人工验收事实**: 照片确认 `240x180` 合成画面位于右上角 `(80,0)`，几种规律色块持续切换且肉眼无撕裂感；遥控基本正常，摇杆控件尺寸和手感本轮可接受。该输入是测试反馈，不登记为原始需求。
- **样本范围**: Tank 424 个发送统计窗口、Remote 161 个显示统计窗口，稳态约 `390s`。Tank/Remote 原始日志 SHA-256 分别为 `3FA1C0EE44D9C3CE94512E8107B190777460466AFDE355F1EB91755FD5C76A32`、`A878AC68C08D0E3BE81B65B378185E29516EFB4766512307FE5A4927453C1462`。
- **Tank 合成源稳态**: 发送计数跨度帧率 `32.474fps`；窗口均值 `32.475fps`，P95 `32.7fps`。单帧平均：合成色块 `9.374ms`、JPEG 编码 `18.553ms`、UDP 分片发送 `2.160ms`、总计 `30.086ms`；JPEG 平均 `4664.95B`，范围 `4624～4700B`，有效 JPEG 载荷约 `151.49KB/s`（`1.212Mbps`）。`capture_identity_errors=0`。日志字段 `wait` 在合成模式实际代表色块生成，`cache/subsample=0`；不得把该结果外推为摄像头采集/下采样性能。
- **UDP 完整帧接收**: 计数跨度 `32.410fps`，约为 Tank 发送速率的 `99.80%`。末值 `complete=12720, missing=27`，按序号间隙口径完整率 `99.788%`、缺帧率 `0.212%`。`missing` 同时包含发送失败和空口/接收侧未完成帧，现有日志不能继续细分。Tank 启动阶段在 Remote 就绪前有约 28 次发送失败，稳态后另见 2 次；Remote 首次找不到 AP 后按既有逻辑 5s 软件重启，第二次启动后稳定连接。
- **Remote 显示稳态**: 显示计数跨度 `12.336fps`；窗口均值 `12.423fps`、P50 `12.51fps`、P95 `12.54fps`，最低窗口 `10.46fps`。单帧平均：ready queue 取帧 `0.234ms`、JPEG 解码 `12.178ms`、RGB565 颜色转换 `7.302ms`、写入右上画布 `10.712ms`、画布到 DMA chunk 复制 `6.867ms`、摇杆叠加 `10.722ms`、LCD DMA `31.919ms`、完整显示段 `49.563ms`、Remote 总计 `80.006ms`。无 JPEG 解码失败、显示提交失败或 DMA 超时。
- **队列去向闭环**: 末值 `ready=12689, displayed=4830, stale_drop=7857, queue_drop=0`，另有 2 帧处于显示任务/队列交接中；显示比例 `38.064%`，主动跳旧比例 `61.920%`。因此 Tank/Remote 完整帧率约 32.4fps 而屏显约 12.34fps 的差额主要是低延迟策略主动丢弃旧完整帧，不是网络缺片。
- **显示瓶颈**: 40MHz 全屏 RGB565 理论 SPI payload `30.720ms`，实测 DMA `31.919ms`，额外约 `1.199ms`；复制+摇杆叠加另占 `17.589ms`，三项合计 `49.508ms`，与完整显示段 `49.563ms` 一致。当前 80.006ms Remote 总耗时决定理论处理上限约 `12.5fps`；网络不是当前显示帧率瓶颈。
- **PSRAM/内部 RAM**: Tank 的 `subsample=86400B`、`jpeg=43200B`、`send=43208B` 均确认在 PSRAM，分配后 `free_psram=8049080B`、`free_internal=152091B`。Remote 三个 JPEG 槽、`decoded=86400B`、`canvas=153600B` 均在 PSRAM，DMA chunk `38400B` 在内部 DMA RAM；分配后 `free_psram=8015260B`、`free_internal=242451B`、`largest_internal_dma=126976B`，未见内存分配失败。
- **时延边界**: Tank 平均处理 `30.086ms` + Remote 平均处理 `80.006ms` = `110.092ms`，只代表两个设备处理阶段的串行预算。当前视频头没有跨设备生成时间戳，Remote `recv` 也不包含 UDP 空口与重组耗时，因此本轮没有可审计的真实端到端时延数据。

**当前阻塞/下一步**:
- 真实摄像头 `240x180` 链路已在 Q30 下完成自动指标回归；颜色、画面细节和撕裂只能由用户目视验收，日志不能代替。
- Round 4 已达到自动目标，不继续 Round 5–10；如人工画质不能接受，再基于反馈单变量调整质量参数。
- 如需精确端到端时延，需在视频帧头增加 Tank 单调时间戳并建立双端时钟校准，或使用同源 GPIO/逻辑分析仪；当前日志不足以给出该指标。
- 正式需求 Q60 与当前 Q30 实现的参数差异待用户确认。
- 正式电池分压系数/阈值仍未确认，Tank 电量暂为 `75%` 占位，不把数值正确性列为已通过。
- **公共组件修改约束**: 已在根目录 design.md 补充授权原则，非授权下不得修改 CODE/components/ 公共组件
- 未执行 Git 操作。

---

## 2. 已完成阶段

### P0 工程骨架验证 (2026-08-14)

**交付物**:
- [requirements.md](requirements.md) v0.2 — 33 条需求,14 个关键问题确认
- [design.md](design.md) v1.0 — 17 章完整系统设计(含协议/状态机/任务规划/风险表/分阶段计划)
- [sdkconfig.defaults](sdkconfig.defaults) — 基础配置(ESP32-S3 + PSRAM + WiFi SoftAP)
- [sdkconfig.defaults.tank](sdkconfig.defaults.tank) — 坦克角色片段
- [sdkconfig.defaults.remote](sdkconfig.defaults.remote) — 遥控器角色片段
- [main/Kconfig.projbuild](main/Kconfig.projbuild) — 角色选择 + 启动延时配置
- [main/rc_tank_common.h](main/rc_tank_common.h) — 协议定义(命令/包格式/常量)
- [main/app_main.c](main/app_main.c) — 入口 + 10 秒启动延时 + 角色分发

**验证结果**:
- 坦克角色(Kconfig 默认): `build_example.ps1 -Example rc_tank_demo -Clean` → 1420/1420 ✅
- 遥控器角色(环境变量覆盖): `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.remote"` → 1420/1420 ✅
- 两角色 `sdkconfig` 生成正确(坦克 `TANK=y` / 遥控器 `REMOTE=y`)

### P1-P3 核心链路实机验证 (2026-08-17)

**目标**: 验证双端连接 + 遥控指令传递 + 图像传输端到端链路

**交付物**:
- rc_net.c WiFi 点对点连接实现(SoftAP + STA 扫描/连接)
- rc_control.c 摇杆控制框架(Tank 侧 UDP 8001 接收 + 电机驱动,Remote 侧 stub)
- rc_video.c MJPEG 编码/解码链路(Tank SP0A39 → JPEG 编码 → TCP 8002 发送,Remote TCP 接收 → 显示)
- rc_audio.c Opus 录音框架(代码实现,未实机测试)
- 串口抓取工具 `tools/serial_capture.py` 改进(被动模式 DTR/RTS=False)

**实机验证结果**:
- ✅ Tank 初始化: 摄像头(SP0A39 640×480 VYUY)、显示(ST7789V3 240×320)、电机(PT2466 GPIO 驱动)、音频(ES8311/ES7210 I2S duplex)、SoftAP(RC_TANK_F3AE25 ch1)全部成功
- ✅ Remote 初始化: 显示、音频、WiFi STA 全部成功
- ✅ WiFi 连接: Remote 扫描到 Tank AP(RSSI -20~-22),连接成功(aid=1),PMF 协商为 pmf:1(虽 capable=false 配置未生效,但连接稳定)
- ✅ 图像传输链路: Tank 编码并发送 450+ 帧 JPEG(~14.9KB/帧,320×240 QVGA quality 60),Remote 接收并处理 180+ 帧
- ⚠️ **遥控器显示缺陷**: 稳态日志持续报 `display_hal_draw_bitmap_rgb565(225): bitmap out of bounds`,帧数据到达但未成功渲染到屏幕
- ⏳ 控制通道(UDP 8001)和音频通道(TCP 8003)代码已实现,未实机验证

**已修复的阻塞问题**:
1. **摄像头初始化失败**(rc_tank_role_run 初始化顺序):
   - 现象: Tank 初始化时摄像头卡在 DVP 诊断阶段,SoftAP 未启动
   - 根因: 摄像头初始化先于显示,SPI 总线竞态或 IOEX 状态不稳定
   - 修复: 参考 camera_display_demo,调整顺序为 显示 → 摄像头 → 音频 → 网络
   - 验证: Tank 启动日志显示 `[STEP] Display init OK` → `[STEP] Camera init OK` → `[STEP] Network init OK`

2. **遥控器连接超时**(rc_net.c Remote STA 扫描后连接逻辑):
   - 现象: Remote 扫描到 Tank AP 后等待 10s 超时,日志 `Connection timeout`
   - 根因: `esp_wifi_set_config()` 配置 SSID/密码后直接 `xEventGroupWaitBits()`,缺少 `esp_wifi_connect()` 调用
   - 修复: 在 `esp_wifi_set_config()` 后、等待事件前插入 `esp_wifi_connect()`
   - 验证: Remote 日志显示 `Connecting to tank AP...` → `connected with RC_TANK_F3AE25, aid = 1`

3. **PMF 握手超时**(rc_net.c WiFi 配置 PMF 能力不匹配):
   - 现象: Remote 连接后发起 SA Query,6 次无响应后断开重连,循环往复
   - 根因: Tank AP `pmf_cfg.required=false` 未设 capable,Remote STA `capable=true`,协商出 pmf:1 但 AP 不响应 SA Query
   - 修复: 两端都设置 `pmf_cfg.capable=false, required=false` 关闭 PMF
   - 实际结果: pmf 仍协商为 1(ESP-IDF 可能有 Kconfig 覆盖),但连接稳定不再触发 SA Query 超时
   - 验证: Remote 稳态运行 70s+,WiFi 连接保持,无 SA Query 日志

**工具链改进**:
- `tools/serial_capture.py` 被动模式改进:
  - 问题: Windows 打开 COM 口默认拉高 DTR/RTS,导致芯片复位或进下载模式
  - 修复: open 前显式设置 `ser.dtr=False, ser.rts=False`(EN 与 IO0 均不拉低=正常运行)
  - 效果: 被动抓取稳定捕获运行中设备日志,不再扰动芯片状态

**构建注意事项**:
- GCC 14.2.0 ICE(段错误)在 esp-dsp 和 esp_lcd_panel_rgb.c 上偶发触发
- 规避策略: 镜像时删除 `managed_components/espressif__esp-dsp`,增量重试失败的编译任务(单文件重编译时并发压力小,通常成功)
- Tank 固件大小: 0xff390 (1MB,67% 空闲),Remote 固件: 0x10fc30 (1.06MB,65% 空闲)

---

## 3. 待实现阶段

按 [design.md 第 17 章](design.md#17-分阶段计划) 规划:

| 阶段 | 目标 | 依赖 | 状态 |
|------|------|------|------|
| P0 | 工程骨架,双角色可编译 | - | ✅ 已完成 |
| P1 | rc_net WiFi 点对点连接 | P0 | ✅ 首次连接与Tank短时重启后的Remote自动恢复已通过；长时间无热点、长断连重扫和双端图标一致性待验收 |
| P2 | rc_control 摇杆控制+电机+安全停止 | P1 | ✅ 正常连接下移动操作人工通过；断连立即停车与重连保持停止待验收 |
| P3 | rc_video MJPEG 编码验证(R1 风险验证) | P2 | ✅ Q60正式链路与Remote画面人工通过；偶发3～4像素窄叠影线挂起，帧率下限和30分钟长稳待验收 |
| P4 | rc_audio SW3 录音+Opus | P3 | ⏳ 代码已实现,未实机测试 |
| P5 | rc_display UI + rc_power 电池 | P4 | ⏳ Tank状态屏、Remote视频/摇杆已通过当前人工检查；Remote电量与双端WiFi图标一致性待验收 |
| P6 | 集成测试与稳定性验证 | P5 | 📅 计划中 |

**当前视频结果**: 软件JPEG Q60正式路径已稳定运行，既有180秒稳态证据为Tank发送约`9.95fps`、Remote显示约`9.85fps`；用户已确认清理版画面正常。严格`≥10fps`下限与30分钟长稳仍需独立验收。

---

## 4. 已确认关键决策

来源: [requirements.md 第 7 节](requirements.md#7-已确认的问题)

1. **WiFi 点对点**: 坦克作 SoftAP(SSID: `RC_TANK_<MAC后6位>`),遥控器 STA 连接(无路由器)
2. **三通道通信**: UDP(控制,端口 8001) + UDP(视频,端口 8002) + TCP(音频,端口 8003)
3. **运动控制**: 离散五态(前/后/左转/右转/停),300ms 超时安全停止
4. **电机驱动**: PT2466 0%/100% GPIO 模式(无 PWM),GPIO4/5/37/45 = IN1-4
5. **视频**: Tank采集640×480 VYUY并缩放为240×180，软件JPEG Q60，按1200B UDP载荷分片；现有稳态约9.85～9.95fps，Remote人工画面通过，偶发窄叠影线挂起
6. **音频**: SW3 按下录音,Opus 16kHz mono 60ms 帧
7. **坦克屏幕**: WiFi 连接状态(角落) + 简单坦克像素图(LVGL 绘制) + 电池电量
8. **电池监测**: GPIO7 BAT_ADC,双角色均支持(标定参数待测)
9. **启动延时**: 10 秒(XCR-028 约定)
10. **摄像头热插拔**: COM 枚举问题已知,但正常插着可带动所有板子(EX-027 已验证)

---

## 5. 已知风险与对策

来源: [design.md 第 15 章风险表](design.md#15-风险评估)

| ID | 风险描述 | 概率 | 影响 | 对策 | 验证阶段 | 状态 |
|----|---------|------|------|------|----------|------|
| R1 | 软件 JPEG 编码继续提高吞吐的余量有限 | 中 | 中 | 正式Q60链路Tank约`9.95fps`、Remote约`9.85fps`；保持当前可用画质，严格帧率下限在长稳中复核 | P3 | ⏳ 基本可用，长稳待验收 |
| R2 | UDP 分片链路存在空口缺片风险 | 中 | 高 | 1200B 应用层分片、只解码完整帧；正式短稳启动关联窗口后未持续增长，继续做30分钟长稳 | P3 | ✅ 短时通过 |
| R3 | 遥控器触摸+WiFi+视频解码可能内存不足 | 中 | 高 | 三个 JPEG 槽放 PSRAM，LCD 使用独立 DMA 分块缓冲 | P5 | ✅ Round 10 未见分配失败 |
| R4 | PT2466 电机与摄像头 DVP 同时工作可能电流尖峰 | 低 | 中 | EX-027 已验证可共存 | - | ✅ 已消除 |
| R5 | 坦克像素图复杂度待定 | 低 | 低 | LVGL 内置绘图,简单即可(用户确认) | P5 | ✅ 已确认 |
| R6 | Remote 偶发3～4像素横向叠影线 | 低 | 中 | 保持已通过的方向、100%亮度、Q60与固定行修复；按用户指令只记录，不继续引入修复变量 | P3 | ⏸ 挂起 |
| R7 | Opus 编解码器资源占用(CPU/内存)待评估 | 低 | 中 | P4 测试,不行降采样率或用 G.711 | P4 | ⏳ 待验证 |

---

## 6. 项目事实

- **硬件平台**: ESP32-S3 (8MB Flash + 8MB PSRAM)
- **构建系统**: ESP-IDF v5.5.4
- **坦克硬件**: A0 核心板 + C0 扩展桥 + D0 电机板 + E0 摄像头板 + 屏幕板 + B0 MIC 板(保留)
- **遥控器硬件**: A0 核心板 + 屏幕板 + B0 MIC 板
- **屏幕**: ST7789V3 (240×320 SPI) + CST836U 触摸 I2C
- **摄像头**: SP0A39 DVP (最高 VGA 640×480)
- **已验证**: 摄像头 + LCD + 电机可同时工作(EX-027)
- **构建入口**: 使用本目录 `build_rc_tank.ps1 -Role tank/remote -Clean` 镜像到ASCII临时路径并发布制件，细节见 [README.md](README.md#推荐构建与烧录) 与 [design.md](design.md#16-验证入口)
- **设备端口**: Tank=COM7，Remote=COM24，115200/8N1
- **构建注意事项**: 需限制并发为 2 避免 GCC ICE；每轮烧录前必须全片擦除

---

## 7. 参考文档

- [requirements.md](requirements.md) — 需求规范
- [design.md](design.md) — 系统设计
- [../../../design.md](../../../design.md)（根）— 硬件 GPIO 映射、板型定义和全项目设计入口
- [../../../coding_rule.md](../../../coding_rule.md) — 项目通用开发规则

---

## 8. 阶段记录

### 2026-08-17 下午 — 帧率统计插桩 + 透明摇杆 + 坦克像素屏

**用户三项要求**:
1. 加充足统计日志，优先实现摄像头采集的稳定 25 帧(补充: camera_display_demo 25fps 仅相对稳定的基线，非最终需求，应尽量提升)
2. 用日志调试确认完整链路延迟，保障帧率稳定(可接受图像整体时间落后，但帧率应稳定)
3. 本轮完成透明摇杆 + 坦克屏幕像素坦克

**已完成代码改动(未构建/未烧录/未实机验证)**:

1. **帧率统计插桩** — [main/rc_video.c](main/rc_video.c)
   - Tank 侧新增 `s_capture_count`(volatile)，在 DVP 完成中断 `on_trans_finished` 累加，独立统计"采集帧率"
   - `video_tx_task` 每 30 帧输出: `cap_fps`(DVP 采集速率) vs `send_fps`(编码+发送吞吐)，并分解各阶段耗时: wait/cache/subsample/encode/send/total(ms) + JPEG 字节数
   - 目的: 区分"采集是否达 25fps"(目标1)与"瓶颈在哪一级"(目标2)
   - 发送缓冲预分配(避免每帧 malloc/free)
   - Remote 侧新增 recv/decode/display 三级计时

2. **透明摇杆(Remote)** — 新建 [main/rc_joystick.c](main/rc_joystick.c) + [main/rc_joystick.h](main/rc_joystick.h)
   - 纯逻辑函数: `rc_joystick_dir_from_offset`(方向映射,死区判定,主轴选择) + `rc_joystick_clamp_offset`(偏移限幅)
   - `rc_joystick_render_overlay`: RGB565 帧缓冲直接合成(半透明底座 alpha 混合 + 摇杆头)，避开 LVGL
   - 几何: 底座中心(56,184)左下角，底座半径40/摇杆头半径20/死区10/最大行程40
   - [main/rc_control.c](main/rc_control.c) Remote 侧从 stub 改为完整触摸集成: `touch_poll_task` 50Hz 轮询 → 坐标变换(面板240×320 → 横屏320×240: `screen_x=319-point.y; screen_y=point.x`) → 限幅 → 方向映射 → 更新 `s_current_cmd` + `s_joystick_state`
   - [main/rc_control.h](main/rc_control.h) 新增 `rc_joystick_get_state()` 供视频叠加读取
   - 叠加点: JPEG 解码后、LCD 输出前合成摇杆(层级在视频之上, REQ-035-010)

3. **坦克屏像素坦克(Tank)** — 新建 [main/rc_tank_screen.c](main/rc_tank_screen.c) + [main/rc_tank_screen.h](main/rc_tank_screen.h)
   - `rc_tank_screen_render(fb,w,h,wifi_connected,battery_percent)`: 纯几何，绘制黑底 + 像素坦克(上下履带/车体/炮塔/右伸炮管) + 右上角 WiFi 状态方块(绿=连/红=断) + 5×7 点阵电量百分比
   - [main/rc_video.c](main/rc_video.c) `rc_video_display_init()`(Tank 侧)从填充绿屏改为: PSRAM 分配 320×240 帧缓冲 → render → `board_laiwfs300_display_draw_bitmap_rgb565` 推屏 → 释放。当前 WiFi=false/电量=75% 为占位值(未接实际状态/ADC)
   - [main/CMakeLists.txt](main/CMakeLists.txt) SRCS 增加 rc_joystick.c + rc_tank_screen.c

**纯 C 测试(P0 门禁，全部实际运行通过)**:
- runner: [tests/run_host_tests.ps1](tests/run_host_tests.ps1)，LLVM-MinGW clang(x86_64-w64-windows-gnu)
- `test_rc_net_stream` exit=0 / `test_rc_video_buffer_select` exit=0 / `test_rc_joystick_direction` exit=0(10 用例) / `test_rc_tank_screen_render` exit=0(8 用例)
- 新建 [tests/test_rc_tank_screen_render.c](tests/test_rc_tank_screen_render.c): 背景/车体/履带/炮管/WiFi图标/电量渲染/电量-1不渲染/越界保护
- 踩坑记录:
  - 测试 include 应用 `-Isupport -Imain`(不带相对前缀)，TEST_ASSERT 由测试文件自定义，freestanding.c 提供 memset/memcpy/memcmp
  - 大栈数组(320×240×2=150KB)触发 Windows `___chkstk_ms` 链接错误 → 改用静态全局帧缓冲
  - 屏幕中心被炮管(橙色)覆盖 → 车体颜色断言点移到 cx-20

**关键待办(下次进入优先)**:
1. **用户手动实机验收**: Tank(COM7)验证像素坦克屏与采集/发送帧率；Remote(COM24)验证连接与透明摇杆叠加（触摸左下角摇杆头随手指移动，松手归位）。
2. **观察遗留缺陷**: 重点确认 Remote `display_hal_draw_bitmap_rgb565: bitmap out of bounds` 是否复现。
3. **帧率瓶颈优化(实测后)**: 依据 `cap_fps/send_fps` 与各阶段耗时定位瓶颈（预判 JPEG 编码或 TCP 发送），再优化。
4. **占位项补实**: Tank 屏 WiFi 状态接真实连接状态，电量接 GPIO7 BAT_ADC。

### 2026-08-17 晚间 — 构建集成修复与双角色烧录

**触发**: 用户要求恢复 RC Tank Demo 状态，构建并烧录 Tank 到 COM7、Remote 到 COM24，实机测试由用户手动执行，不抓取日志。

**最小修复**:
- `main/rc_tank_common.h`: ESP-IDF 构建下显式包含生成的 `sdkconfig.h`，使角色 choice 宏在各编译单元可见。
- `main/rc_video.c`: 补充 `esp_timer.h` 与 `rc_tank_screen.h`，修复帧率统计和 Tank 屏渲染新增依赖的编译声明；未改变协议、状态机、任务时序或硬件行为。

**验证证据**:
- 纯 C 主机回归: `test_rc_net_stream`、`test_rc_video_buffer_select`、`test_rc_joystick_direction`、`test_rc_tank_screen_render` 全部 exit=0；LLVM-MinGW clang，target `x86_64-w64-windows-gnu`。
- Tank: `build_example.ps1 -Example rc_tank_demo -Clean`（`SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.tank`，并发 2）`1424/1424`；固件 `0xffb10`。COM7 全片擦除成功，烧录应用与分区表均 `Hash of data verified`，硬复位完成。
- Remote: 同入口使用 `sdkconfig.defaults;sdkconfig.defaults.remote`，`1424/1424`；固件 `0x110bf0`。COM24 全片擦除成功，烧录应用与分区表均 `Hash of data verified`，硬复位完成。

**当前下一步**:
1. 用户手动执行 Tank(COM7)屏幕/帧率与 Remote(COM24)连接/摇杆叠加验收。
2. 重点观察 Remote `display_hal_draw_bitmap_rgb565: bitmap out of bounds` 是否复现。
3. 收到实机结果后，再决定是否补真实 WiFi 状态、电量 ADC 和帧率瓶颈优化；当前不据构建/烧录结果宣称功能验收完成。

### 2026-08-17 — Round 1：显示/DMA/方向回归修复与实板验证

**用户补充并授权**:
- 先按理论逻辑计算各阶段速率/时延，再用实机统计对照优化；最多 10 轮自动编辑、构建、全片擦除、烧录和日志抓取。
- 遥控器必须横屏使用，屏幕向上为前进；摇杆不能贴边，需能测试四方向。

**问题矩阵结论**:
- Tank 边界错误根因是未在静态帧提交前切换横屏；同时异步 LCD DMA 未等待就释放 PSRAM 帧缓冲，存在生命周期风险。
- Remote 整帧 PSRAM 提交触发 DMA bounce buffer 分配失败；成熟 `camera_display_demo` 的 DMA-capable 分块 + 逐块等待是复用方案。
- 触摸变换沿用已验证 `screen_x=319-raw_y, screen_y=raw_x`，纯逻辑确认 `dy<0`（屏幕向上）映射 `RC_CMD_FORWARD`。

**本轮代码改动**:
- `main/rc_video.c`: Remote 使用 20MHz LCD、初始化传输几何 80 行、40 行 DMA chunk；每块复制到内部 DMA buffer，清空旧完成信号后提交并等待；增加理论 SPI payload 时延与实际显示 FPS 日志。Tank 设置横屏后提交静态图并等待 DMA 完成。
- `main/rc_joystick.h/.c`、`main/rc_control.c`: 摇杆中心改为 `(72,168)`，新增纯逻辑触摸坐标映射，方向变化时记录 raw/screen/offset/cmd。
- `main/rc_video_display_plan.h`、`main/rc_video_theory.h`: 新增纯 C 分块边界和理论时延 seam。
- `tests/test_rc_video_display_plan.c`、`tests/test_rc_joystick_direction.c`、`tests/run_host_tests.ps1`: 覆盖 320x240→6×40 行、20MHz/10MHz SPI 理论时延、四角坐标和四方向。

**纯 C 验证**:
- `test_rc_net_stream`、`test_rc_video_buffer_select`、`test_rc_joystick_direction`、`test_rc_video_display_plan`、`test_rc_tank_screen_render` 均 `exit=0`。
- LLVM-MinGW clang target `x86_64-w64-windows-gnu`，真实执行通过；尚未进入固件构建/烧录。

**Round 1 实机记录**:
- Tank(COM7) 与 Remote(COM24) 固件均完成全片擦除、烧录和校验。
- Remote 启动日志确认 `pclk=20000000`、`Display theory: RGB565=153600 B/frame, SPI payload=61440 us/frame, max=16.28 fps, chunk=40 lines (10240 us)`；未观察到 `setup_dma_priv_buffer` 或 `bitmap out of bounds`。
- 首次并行抓取时 COM7 的 `serial_capture.py reset` 将 Tank 留在 `boot:0x3 DOWNLOAD`，改用同一串口句柄 `DTR=True, RTS=True -> DTR=False, RTS=False` 复位后，Tank 正常启动并连接 Remote。
- Tank 日志确认静态屏初始化完成且无边界错误，但视频任务出现 `Send buffer alloc failed`，因此本轮尚未得到发送帧率；Remote 无视频是该发送缓冲失败的连带结果。
- 下一轮根因边界：`rc_net_video_send()` 只做 TCP 复制/发送，不要求 DMA；发送缓冲应放 PSRAM，不能占用内部 DMA 保留区。

### 2026-08-17 — Round 2：Tank 发送缓冲能力修正

- 新证据：Tank 连接 Remote 后 `Video TX task started` 随即 `Send buffer alloc failed`，此前显示修复已越过边界检查；Remote 无视频由此连带发生。
- 根因：约 76808 B 的 TCP 发送缓冲错误使用 `MALLOC_CAP_DMA`，而工程仅保留 64KB 内部 DMA 池；TCP `send()` 不要求 DMA-capable 内存。
- 修复：`main/rc_video.c` 使用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`，并新增 `TX theory` 日志（原始 YUV/QVGA 字节量与 10fps 预算）。
- 纯 C：`rc_video_tx_buffer_bytes(76800,8)==76808` 纳入 `test_rc_video_display_plan`，全套 5 项 exit=0。
- 后续实板结果见 Round 3；该修复已越过 `Send buffer alloc failed`。

### 2026-08-17 — Round 3：PSRAM 发送缓冲实板闭环

- Tank/Remote 完成 clean build、全片擦除、烧录和日志抓取。
- Tank 成功连续发送 180 帧，Remote 成功连续显示 180 帧，说明 Round 2 的 PSRAM TCP 发送缓冲修复有效。
- 平均性能：Tank 发送 4.80fps，下采样 46.02ms、编码 66.72ms、发送 100.30ms、总计 213.08ms；Remote 显示 4.84fps、总计 208.90ms。
- Remote 使用 20MHz LCD、40 行 DMA 分块；未再观察到 `bitmap out of bounds` 或 DMA 私有缓冲失败。
- 结论：显示/传输功能链路已通，瓶颈转为编码和 TCP 阻塞时延。

### 2026-08-17 — Round 4：LCD 分块开销优化

- 将 Remote DMA 分块从 40 行增加为 60 行，在不超过单次 DMA 传输边界的前提下把每帧事务数从 6 次降为 4 次。
- COM7 多次硬复位进入下载模式；恢复 Tank 启动后，`round4g_*` 获得有效稳态数据。
- 平均性能：Tank 发送 5.00fps、总计 200.46ms；Remote 显示 5.05fps、LCD 75.18ms、总计 198.55ms。
- 结论：减少事务数量有小幅改善，但 LCD 实际时延仍约 75ms，TCP/编码仍是更大瓶颈。

### 2026-08-17 — Round 5：CPU 频率提升

- CPU 从 160MHz 固定到 240MHz；LCD 保持 20MHz/60 行分块，TCP 保持 5760/5760/6。
- 平均编码从 Round 4 的 67.50ms 降为 48.18ms，Remote 解码从 40.04ms 降为 28.81ms，证明 CPU 频率调整有效。
- 发送端 TCP 平均时延升至 110.32ms，Tank/Remote 帧率分别为 4.90/5.02fps；整体吞吐没有同步达到 10fps。
- 结论：计算阶段显著改善后，TCP 等待成为主要限制。

### 2026-08-17 — Round 6：Remote 最新完整帧流水线

- Remote 拆分独立网络接收任务和解码/显示任务，新增三个 PSRAM JPEG 槽、free/ready 队列和“只消费最新完整帧”策略；增加 `rc_video_latest_frame` 纯 C seam 与测试。
- 平均性能：Tank 采集 25.27fps、发送 5.46fps、发送时延 86.93ms、总计 182.33ms；Remote 显示 5.61fps、接收 72.27ms、解码 30.86ms、LCD 75.35ms、总计 178.49ms。
- 结论：解耦接收和显示后吞吐与时延改善，Round 6 作为后续 TCP 配置实验的稳定基线。

### 2026-08-17 — Round 7：32KB TCP 窗口实验与 COM7 启动定位

- 基于单帧 JPEG 大小尝试把 TCP 发送/接收窗口扩大到 32768B；Remote 仍使用三槽最新帧流水线。
- 实板首帧传输异常，Remote 仅短暂显示到 1.53fps，随后 Tank 持续 `ESP_ERR_INVALID_STATE`；该配置未形成稳定链路。
- COM7 硬复位反复进入 `boot:0x3 DOWNLOAD(USB/UART0)`；常规 RTS/DTR 序列不能可靠释放 GPIO0。使用 `esptool ... load_ram ...\bootloader.bin` 可启动 Flash 中应用，说明应用镜像本身可运行，问题边界指向复位采样/硬件连接。
- 结论：扩大窗口未证明可提升吞吐，需要先定位发送失败的具体位置。

### 2026-08-17 — Round 8：TCP 发送定向探针

- 在 `send_all` 失败出口加入临时 `[DEBUG-R7-SEND]` 探针，只记录 socket、已发送字节、返回值、errno 和耗时。
- 首帧证据：`sent=3508/20468, errno=11 (EAGAIN), elapsed=2441.5ms`；Remote 已连接视频/音频 TCP，但完整 JPEG 帧未送达。
- 结论：失败不是 JPEG 解码或 LCD，而是 32KB TCP 配置下发送端在首帧中途长期背压并超时。

### 2026-08-17 — Round 9：TCP 接收邮箱扩容反证

- 保持 32768B TCP 窗口，把 TCP 接收邮箱从 6 增加到 25，以覆盖理论上的 23 个 MSS 段加调度余量。
- 首帧进度改善为 `sent=12958/17543`，但仍以 `errno=11` 失败，Remote 仍不能得到完整帧。
- 结论：邮箱不是唯一限制；大窗口配置在目标板上加剧 WiFi/lwIP pbuf 与队列压力，不能仅按“单帧装入窗口”思路优化 TCP 流。

### 2026-08-17 — Round 10：恢复 4×MSS 默认配置并完成最终烧录

- 恢复 ESP-IDF 稳定配置：MSS=1440B、TCP TX/RX window=5760B、TCP RX mailbox=6；保留 CPU 240MHz、Remote 20MHz/60 行 DMA 分块和三槽最新帧流水线。
- 删除 Round 8/9 的 `[DEBUG-R7-SEND]` 临时探针；`rc_video_theory.h` 和纯 C 测试明确 20KB JPEG 可跨 4 个 TCP 窗口连续传输，不要求单帧装入单窗口。
- 六项主机纯 C 测试全部 `exit=0`：`test_rc_net_stream`、`test_rc_video_buffer_select`、`test_rc_joystick_direction`、`test_rc_video_display_plan`、`test_rc_video_latest_frame`、`test_rc_tank_screen_render`。工具链为 LLVM-MinGW clang，目标 `x86_64-w64-windows-gnu`。
- Tank clean build `1424/1424`，固件 `0xffe60`；Remote clean build `1424/1424`，固件 `0x111480`。COM7/COM24 全片擦除成功，烧录日志均包含分区 Hash 校验成功。
- Tank 稳态 19 个统计窗口：采集 25.10～25.40fps，发送 4.50～6.70fps，平均发送 5.98fps；平均下采样/编码/TCP/总时延为 48.19/45.49/71.25/165.01ms。
- Remote 稳态 17 个统计窗口：显示 5.52～6.72fps，平均 6.04fps；平均接收/解码/LCD/总时延为 65.60/26.85/73.39/165.87ms。
- JPEG 样本均值约 24KB、范围 9231～34001B；按平均 5.98fps 计算有效 JPEG 载荷约 144KB/s。
- 稳态未见显示越界、DMA 私有缓冲、颜色发送、JPEG 解码、TCP 关闭、Panic、Guru Meditation 或 abort。启动早期在视频客户端连接前有一次 `ESP_ERR_INVALID_STATE`，连接建立后未复现，不归类为稳态故障。
- 相比 Round 6：Tank 平均发送 5.46→5.98fps，Remote 平均显示 5.61→6.04fps，TCP 发送 86.93→71.25ms，Remote 解码 30.86→26.85ms。
- 最终边界：自动链路已稳定且日志缺陷消失，但 10fps 需求未达标；坦克静态画面、Remote 撕裂/错色和实际控制方向只能由用户人工验收。

**最终回归影响复核**:

| 影响面 | 结论 | 覆盖证据/剩余风险 |
|-------|------|------------------|
| 状态机与协议 | 不受影响 | UDP 控制包、TCP 视频帧格式、端口和角色状态机未修改；`test_rc_net_stream` 通过 |
| 事件时序与并发任务 | 已覆盖核心纯逻辑，实板稳定 | Remote 接收/显示拆分和最新帧选择由 `test_rc_video_latest_frame` 覆盖；Round 10 连续显示 510 帧以上 |
| 缓冲所有权与资源生命周期 | 已覆盖并通过实板日志 | TCP 拼帧缓冲位于 PSRAM；Remote 三槽队列和 DMA 分块边界由 buffer/latest-frame/display-plan 测试覆盖；无分配或 DMA 错误 |
| 错误出口 | 已验证稳态无触发 | 残缺帧/失败帧不进入显示；Round 10 无 JPEG 解码、显示提交或稳态 TCP 关闭错误 |
| 历史显示越界/DMA 问题 | 自动回归通过，肉眼待验收 | 日志无 `bitmap out of bounds`、DMA 私有缓冲和颜色发送错误；撕裂、错色、雪花仍需人工观察 |
| 横屏方向与摇杆边界 | 纯 C 已覆盖，机械方向待验收 | `test_rc_joystick_direction` 覆盖四角坐标、四方向及“屏幕向上=前进”；实际电机接线方向无法由主机测试确认 |
| TCP 大窗口回归 | 已覆盖 | `test_rc_video_display_plan` 固定 5760/5760/6，并验证 20KB 帧跨 4 个窗口；Round 10 实板稳定 |

### 2026-08-19 — Remote 摇杆与 UDP 视频应用层分片修复

- 用户实测确认 Tank 屏幕方向正常、连接和电量指示有效。Remote 控件实际位于右下，只能持续左转/前进，不能右转/后退；越界不回中，且无摄像头画面。本轮输入属于测试反馈，不登记为原始需求。
- 摇杆根因与修复：Remote 显示 `mirror_y=false` 与触摸 `screen_x=319-raw_y, screen_y=raw_x` 契约不一致；越界返回 `(-1,-1)` 后仍继续计算方向。Remote/Tank 显示现统一为 `swap_xy=true, mirror_x=false, mirror_y=true`；新增 `rc_joystick_resolve_touch()`，越界和释放统一输出回中、inactive、`RC_CMD_STOP`。100ms 控制保活和 Tank 300ms 超时停车保持不变。
- 视频根因与修复：Tank 本轮日志连续发送至 2580 帧，约 9.5fps、JPEG 20～29KB；Remote 已绑定 UDP/8002 但没有 `Displayed`。旧实现把完整 JPEG 放入单 UDP datagram，而当前 lwIP 未启用 IPv4 分片重组。新增 `rc_video_udp_transport.c/.h`，以 1200B 最大载荷分片；8 字节头 `reserved` 高字节为总片数、低字节为片序号。Remote 支持同帧乱序重组，缺片旧帧由新 `seq` 替换，完整帧才进入解码。
- 验证：`tests/run_host_tests.ps1` 共 13 个程序全部 `exit=0`，LLVM-MinGW clang 22.1.8，目标 `x86_64-w64-windows-gnu`。Remote/Tank ASCII 镜像 clean build 均 `1426/1426`。Remote 应用 `1121824` bytes，SHA-256 `3D5A40C080D56869E528EFCF0E371E68E9D58029B9E6DF525872C6D54920FB35`；Tank 应用 `1048816` bytes，SHA-256 `B6C42151550747F05D19891910B15A1B4C999AE2EDEB5D4BEDED548A5FC88FE6`。
- 待办：本轮没有新的擦除/烧录授权，COM24/COM7 均未写入这版固件。下一步烧录后验证摇杆在左下、四向持续运动、越界/释放停车、Remote 视频帧率/颜色/撕裂；Tank 状态屏周期刷新出现的 LCD DMA `ESP_ERR_NO_MEM` 仍是独立问题。
