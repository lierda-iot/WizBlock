# 遥控坦克 Demo 设计文档

**文档版本**: v1.38
**创建日期**: 2026-08-14  
**最后更新**: 2026-08-27
**状态**: C16w、Q60、1200B UDP分片、实载健康门和空闲STOP静默保持当前有效设计。连续控制的启动和最终转向曲线已完成人工验收；长断连重扫、双端图标、失联停车、音频和30分钟长稳仍待验收。
**关联需求**: [requirements.md](requirements.md) v0.44
**Demo 编号**: EX-035 (rc_tank_demo)

---

## 1. 文档目的

本文记录遥控坦克 Demo 当前有效的实现方案、技术取舍、模块边界、接口契约、数据结构、状态机、资源映射、错误处理和验证入口。基于 requirements.md v0.44 当前需求和本文已记录的硬件事实展开，不把经验推测写成确认事实。

---

## 2. 设计目标与依据

### 2.1 设计目标
单一代码库通过 sdkconfig choice 编译为坦克或遥控器两种角色，经 WiFi 点对点实现三路数据流：控制指令、视频流、语音流。

### 2.2 事实依据（来自根目录 design.md）
- 主控 ESP32-S3，8MB Flash + 8MB PSRAM
- 构建入口：`CODE/tools/idf_build.ps1`（镜像到 ASCII 临时路径构建）
- 烧录/日志：Tank 使用 COM7，Remote 使用 COM24，均为 115200 / 8N1
- 摄像头 SP0A39 DVP、LCD ST7789V3、电机 PT2466、音频 ES8311/ES7210 均有可靠参考或已验证记录
- EX-027 已验证摄像头 + LCD + 电机同时工作，GPIO 无冲突
- 电机板使用左右两台PT2466履带电机和四个方向输入；板级实现已经使用20kHz、10bit LEDC PWM，最终占空比接口为 `0..100%`

### 2.3 复用边界
| 能力 | 复用来源 | 复用方式 |
|------|---------|---------|
| WiFi 管理 | network_manager 组件 / EX-024 | SoftAP+STA 模式适配 |
| 摄像头采集 | camera_display_demo (EX-018) | SP0A39 DVP 初始化 + 帧获取 |
| 电机控制 | motor_demo (EX-002) + 当前板级PWM实现 | 复用PT2466方向控制与 `robot_motion` 左右履带接口，Tank控制层负责逻辑速度到PWM映射 |
| LCD 显示 | display_demo / lvgl_demo | ST7789V3 + LVGL |
| 触摸 | touch_demo | CST836U I2C 读取 |
| Opus 编解码 | audio_opus_demo (EX-009) | 16kHz/mono/60ms |
| 音频输出 | xiaozhi_ai_demo | ES8311 播放链路 |
| 音频采集 | audio_demo | ES7210 双 MIC |
| 电池 ADC | board_power | BAT_ADC/GPIO7 采样 |

---

## 3. 系统架构

### 3.1 角色划分与宏定义

```
sdkconfig choice: RC_TANK_ROLE
  ├── CONFIG_RC_TANK_ROLE_TANK    坦克角色
  └── CONFIG_RC_TANK_ROLE_REMOTE  遥控器角色
```

编译时通过 Kconfig choice 二选一，源码中以 `#if CONFIG_RC_TANK_ROLE_TANK` / `#if CONFIG_RC_TANK_ROLE_REMOTE` 区分角色专属逻辑。公共逻辑（网络协议、数据结构、Opus 编解码）不加宏，两角色共享。

### 3.2 数据流拓扑

```
┌─────────────── 遥控器 (SoftAP STA) ───────────────┐        ┌──────────────── 坦克 (SoftAP) ────────────────┐
│  屏幕板(视频显示+摇杆+电量)   B0(SW3录音)          │        │  E0摄像头   D0电机   屏幕(状态)   喇叭         │
│                                                    │        │                                                │
│  摇杆 ──控制包──> UDP:8001 ────────────────────────┼───────>│ UDP:8001 ──> 电机执行                          │
│  视频显示 <── JPEG分片 <── UDP:8002 <──────────────┼────────┤ UDP:8002 <── 摄像头JPEG编码                    │
│  SW3录音 ──Opus──> TCP:8003 ───────────────────────┼───────>│ TCP:8003 ──> Opus解码 ──> 喇叭播放             │
└────────────────────────────────────────────────────┘        └────────────────────────────────────────────────┘
                          WiFi 2.4GHz 点对点（坦克 SoftAP，遥控器 STA 直连）
```

### 3.3 组件分层

```
examples/rc_tank_demo/
├── CMakeLists.txt
├── sdkconfig.defaults              # 公共默认配置
├── sdkconfig.defaults.tank         # 坦克角色默认（可选）
├── sdkconfig.defaults.remote       # 遥控器角色默认（可选）
├── partitions.csv
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild            # RC_TANK_ROLE choice 定义
│   ├── app_main.c                   # 入口 + 10秒启动延迟 + 角色分发
│   ├── rc_tank_common.h             # 公共协议/数据结构/端口常量
│   ├── rc_tank_tank.c               # 坦克角色主逻辑（#if TANK）
│   └── rc_tank_remote.c             # 遥控器角色主逻辑（#if REMOTE）
└── components/
    ├── rc_net/                      # 网络层（SoftAP/STA + 三通道 socket）
    ├── rc_proto/                    # 协议编解码（包格式序列化）
    ├── rc_control/                  # 控制层（摇杆解析 / 电机执行）
    ├── rc_video/                    # 视频层（采集编码 / 接收解码显示）
    ├── rc_audio/                    # 音频层（录音编码 / 解码播放）
    ├── rc_display/                  # 显示层（LCD UI：视频铺底/状态/电量）
    └── rc_power/                    # 电量检测（BAT_ADC 采样换算）
```

---

## 4. 网络层设计 (rc_net)

### 4.1 组网方案
- **坦克**: SoftAP 模式，SSID=`RC_TANK_<MAC后3字节>`，密码=`12345678`，channel=1，最大连接 1 个
- **遥控器**: STA 模式，扫描 SSID 前缀 `RC_TANK_`，匹配后连接
- 坦克固定 IP：`192.168.4.1`（SoftAP 默认网关），遥控器 DHCP 获取

### 4.2 三通道 socket

| 通道 | 协议 | 端口 | 方向 | 服务端 | 客户端 |
|------|------|------|------|--------|--------|
| 控制流 | UDP | 8001 | 遥控器→坦克 | 坦克 bind | 遥控器 sendto |
| 视频流 | UDP | 8002 | 坦克→遥控器 | 坦克 sendto | 遥控器 bind |
| 语音流 | TCP | 8003 | 遥控器→坦克 | 坦克 listen | 遥控器 connect |

设计理由：控制流和视频流用 UDP 保持低延迟；控制由后续包覆盖，视频由应用层按帧分片，缺片时丢弃该帧并等待新帧。语音用 TCP 保证完整性。

### 4.3 接口契约

```c
// rc_net.h
typedef enum { RC_NET_DISCONNECTED, RC_NET_CONNECTING, RC_NET_CONNECTED } rc_net_state_t;

esp_err_t rc_net_init(void);                          // 按角色初始化 SoftAP 或 STA
esp_err_t rc_net_start(void);                         // 启动连接流程
rc_net_state_t rc_net_get_state(void);
void rc_net_register_state_cb(void (*cb)(rc_net_state_t));

// 控制通道（UDP）
esp_err_t rc_net_ctrl_send(const uint8_t *data, size_t len);          // 遥控器用
esp_err_t rc_net_ctrl_recv(uint8_t *buf, size_t buflen, size_t *out,
                           uint32_t timeout_ms);                       // 坦克用

// 视频通道（UDP 应用层分片）
esp_err_t rc_net_video_send(const uint8_t *frame, size_t len);        // 坦克用
esp_err_t rc_net_video_recv(uint8_t *buf, size_t buflen,
                            size_t *out);                              // 遥控器用

// 语音通道（TCP）
esp_err_t rc_net_audio_send(const uint8_t *data, size_t len);         // 遥控器用
esp_err_t rc_net_audio_recv(uint8_t *buf, size_t buflen, size_t *out); // 坦克用
```

### 4.4 重连与失联
- **当前已验证实现**：Remote启动时只执行一次阻塞扫描；未找到`RC_TANK_*`即从`rc_net_init()`返回`ESP_FAIL`，随后顶层等待5秒并调用`esp_restart()`。附件日志连续复现`RTC_SW_CPU_RST`，因此当前实现不满足持续等待/重扫。
- Remote已连接后的`WIFI_EVENT_STA_DISCONNECTED`最多调用5次`esp_wifi_connect()`；耗尽后只置`WIFI_FAIL_BIT`并重置计数，不会回到扫描，也没有完整的持续重连闭环。
- Tank的`WIFI_EVENT_AP_STADISCONNECTED`会清除连接/对端IP并发布断连回调；控制接收任务已有300ms超时停车，但网络断连回调当前没有直接调用`rc_motor_stop()`，所以“断连立即停车”尚未落实。
- Tank状态屏已经由网络回调发布连接状态并在右上角使用红/绿图标；Remote画布尚无左上角WiFi图标。以上是当前实现事实，后续设计必须在不重复创建socket、视频和控制任务的前提下补齐正式需求。

---

## 5. 协议层设计 (rc_proto)

### 5.1 控制包格式（V1，14字节，UDP）

控制协议以显式编解码函数读写网络字节序，不直接把UDP缓冲强转为C结构体。旧7字节五态协议不兼容、不解析。

| 偏移 | 字段 | 类型 | V1语义 |
|---:|---|---|---|
| 0 | `magic` | `uint16` | 固定 `0x5243`，网络字节序 |
| 2 | `version` | `uint8` | 固定 `1` |
| 3 | `mode` | `uint8` | `0=STOP`，`1=DRIVE`，其他值非法 |
| 4 | `angle_deg` | `int16` | `-180..180`；`0`前进、正值右转、负值左转、`±180`后退 |
| 6 | `magnitude_pct` | `uint8` | STOP时发送端写0；DRIVE为 `1..100` |
| 7 | `reserved` | `uint8` | 固定0 |
| 8 | `seq` | `uint16` | 发送成功后递增，允许 `65535→0` |
| 10 | `sender_time_ms` | `uint32` | Remote单调运行时间，只用于诊断，不参与合法性和跨设备绝对延迟判断 |

接收端只接受长度恰好14字节、固定字段正确且参数合法的包。STOP忽略线上的角度与力度并在语义对象中归零；DRIVE要求角度和力度均在合法域。非法包无协议响应，不改变目标、不提交序号、不刷新安全超时，仅做限频本地日志。

首个有效包建立序号基线。已有基线时，令 `delta=(uint16_t)(new_seq-last_seq)`：`1..32767`为新包，`0`为重复包，`32768..65535`为旧包。重复和旧包不改变任何控制状态。WiFi断连或300ms有效包超时立即停车并清除基线，重连后的首个有效包重新建立基线。

### 5.2 视频分片格式（UDP）

```c
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;      // 0xAA55 帧起始标识
    uint16_t length;     // 当前 UDP 分片的 JPEG 载荷长度
    uint16_t seq;        // 帧序号
    uint16_t reserved;   // 高8位=总片数，低8位=片序号
    // 后接 length 字节分片载荷，当前最大 1200B
} rc_video_header_t;     // 8 字节头
#pragma pack(pop)
```

Tank 对每个 JPEG 使用同一 `seq`，按 1200B 最大载荷切分；每个 UDP datagram 均小于常见 1500B MTU，不依赖 lwIP IPv4 分片重组。Remote 按 `seq/index/count` 接受乱序分片，全部到齐后才把完整 JPEG 交给解码器；重复片覆盖原位置，旧帧缺片时由更新 `seq` 直接替换，避免阻塞控制链路。

### 5.3 语音包格式（TCP，整段）

```c
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;        // 0xBB66
    uint16_t sample_rate;  // 16000
    uint32_t length;       // Opus 编码后总字节数
    // 后接 length 字节 Opus 数据（多个 60ms frame，每 frame 前置 2 字节长度）
} rc_audio_header_t;       // 8 字节头
#pragma pack(pop)
```

Opus 为变长帧，段内每个 frame 前置 `uint16_t frame_len` 便于解码端切分。

---

## 6. 控制层设计 (rc_control)

### 6.1 遥控器侧：摇杆解析
- 保持现有悬浮摇杆几何：中心 `(108,136)`、底座半径72px、摇杆头34px、初次捕获半径108px、最大行程62px；捕获后的偏移改为真实圆形限幅，避免旧主轴近似在对角线超出最大半径
- 未触摸、未捕获或半径 `<10px` 时输出 STOP。半径 `10..62px` 时输出DRIVE，力度为 `1 + round((radius-10)*99/52)`，结果 `1..100`
- 角度为 `round(atan2(dx,-dy)*180/pi)`：上0°、右+90°、左-90°、下统一+180°。角度和力度的半点均远离0取整
- 当前控制发送任务每20ms检查语义命令；首次命令或模式、角度、力度变化时立即发送。未变化的DRIVE每100ms保活，未变化的STOP不保活
- 只有发送成功才提交命令、时间并递增序号；失败时保留序号，下一周期按最新命令和最新 `sender_time_ms` 重试
- 断连期间暂停发送；连接世代改变时重置发送策略，重连空闲立即发送一次STOP，成功后再次静默，并保持重连后释放摇杆才能重新解锁DRIVE
- Tank侧300ms安全超时保持不变；DRIVE保持操作仍需周期性有效新包避免运动中误停车。Tank控制接收任务在DRIVE时使用20ms等待，在STOP、空闲或断连时使用50ms等待；该调度调整未改善DVP坏启动，不作为视频修复
- 最终Remote日志中两次相隔8秒的`ctrl_tx`累计值均为`ok=43,stop=12,drive=31,fail=0,current=STOP`，证明同一连接世代内STOP成功后保持静默

### 6.2 坦克侧：电机执行
Tank控制层按“协议命令→左右逻辑速度→时序状态→PWM映射”执行，`robot_motion`与PT2466公共接口保持不变。

满力度右转曲线如下；节点间线性插值。负角交换左右履带；`90..180°`先用 `180-|angle|` 查询前半区，再取反并交换生成后退半区。最后乘力度并按半点远离0取整。

| 角度 | 左逻辑速度 | 右逻辑速度 |
|---:|---:|---:|
| 0° | +100 | +100 |
| +15° | +100 | +65 |
| +30° | +100 | +30 |
| +45° | +100 | -10 |
| +60° | +100 | -80 |
| +75° | +100 | -95 |
| +90° | +100 | -100 |

共享PWM基础曲线为 `(逻辑0,占空比0%)`、`(逻辑1,50%)`、`(逻辑100,100%)`，中间线性插值。左前、左后、右前、右后四个方向槽分别保存 `duty_offset_pct`、`start_duty_pct`、`start_boost_ms`；首版均为 `0/60/60ms`。偏移范围 `-20..20`，启动占空比 `1..100`，助推 `20..200ms`且为20ms整数倍，非法配置使初始化失败并保持停车。

左右履带各自维护目标逻辑速度、当前逻辑速度、助推和换向状态：

- 从完全停止进入非零时，先计算最终目标占空比；低于启动占空比时输出 `max(target,start_duty)` 60ms后回落最新目标，不低于阈值时直接输出目标
- 已运行且同方向时，每20ms最多增加20逻辑点、减少25逻辑点；目标为0时按减速斜坡回零
- 当前非零且目标异号时立即物理归零，保持40ms换向死区，再从0按每20ms最多20逻辑点进入反向；典型 `+100→-100` 约120～140ms到达目标
- 最新目标覆盖旧目标。助推或换向中收到整车STOP时立即取消状态并物理归零；单履带跨过70°附近时只影响该履带
- 只有最终左右PWM输出变化时才调用 `robot_motion_set_track_speed`，其中传入值表示最终带符号PWM百分比

GPIO45仍为strapping pin，电机初始化前和任一失败出口保持全部输入为0。

#### 2026-08-27 最终人工验收定稿

人工确认前进启动、后退启动和掉头启动正常，四方向继续使用`duty_offset_pct=0`、`start_duty_pct=60%`、`start_boost_ms=60ms`，不修改启动状态机。最终转向曲线采用本节表格节点；人工分角度复测并收敛`30°/45°`过渡后，确认当前版本转向手感通过。快速迭代的中间三轮参数不展开，避免失效过程值与当前设计并列。

本轮只调整`rc_drive_control.c`满力度混控节点及对应测试断言；摇杆角度/力度解析、V1协议、基础PWM曲线、启动助推、20ms斜坡、40ms换向死区、STOP/超时/断连安全、网络、视频和音频均保持不变。Tank最终构建`1447/1447`，应用`915,904B`；COM7全片擦除成功，bootloader、partition table和application三段Hash校验通过，RAM boot成功。Remote源码无变化，未重新构建或烧录。

纯C验证按用户对本阶段的持续授权跳过。`test_rc_drive_control.c`已同步最终节点、插值/对称、力度缩放及换向断言，但没有编译或运行，状态只能记录为“测试契约已同步、纯C未运行”，不得表述为通过；剩余风险是自动化回归未对最终曲线提供执行证据，当前验收依据为成功构建、擦写启动和人工手感确认。

### 6.3 失联安全
- 控制任务以不超过20ms周期推进状态，并仅在接受有效新包时更新 `last_ctrl_ms`
- `now-last_ctrl_ms>300ms` → 绕过斜坡立即停止、清除助推/换向和序号基线
- WiFi AP STA 断开事件 → 立即停止 + LED 红闪
- 非法、重复和旧包不能延长运动；连续接收这些包仍按最后一个有效新包计时停车

### 6.4 接口契约

```c
// rc_ctrl_protocol.h（双端纯逻辑）
bool rc_ctrl_packet_encode(const rc_ctrl_packet_t *packet, uint8_t *wire, size_t wire_size);
bool rc_ctrl_packet_decode(const uint8_t *wire, size_t wire_size, rc_ctrl_packet_t *packet);
bool rc_ctrl_seq_is_newer(uint16_t new_seq, uint16_t last_seq);

// rc_drive_control.h（Tank纯逻辑）
bool rc_drive_controller_init(rc_drive_controller_t *controller, const rc_drive_config_t *config);
void rc_drive_controller_set_target(rc_drive_controller_t *controller, const rc_ctrl_command_t *command);
bool rc_drive_controller_step(rc_drive_controller_t *controller, rc_drive_output_t *output);
void rc_drive_controller_stop(rc_drive_controller_t *controller, rc_drive_output_t *output);

// rc_control.h（Tank集成）
esp_err_t rc_motor_init(void);
void rc_motor_apply(const rc_ctrl_command_t *command);
void rc_motor_stop(void);              // 立即停车

// rc_control.h（Remote集成）
esp_err_t rc_joystick_init(void *parent);
void rc_joystick_get_command(rc_ctrl_command_t *command);
```

---

## 7. 视频层设计 (rc_video)

### 7.1 坦克侧：采集与编码
- SP0A39 采集 640×480 VYUY（参考 EX-018）
- `camera_hal` 在上电后完整扫描总线仅输出诊断日志；实际设备选择只接受板级配置地址 `0x21`
- 读取 page0 `0x00/0x01` 后必须得到 ID `0x0A39`；ID 不匹配时移除本次 I2C device、保持未初始化并返回失败
- `rc_video_start_tank()` 在调用 DVP start 前检查控制器非空，摄像头初始化失败不得把空句柄传给 `esp_cam_ctlr_start`
- 缩放至 240×180；Remote 解码后按原尺寸写入右上角 `(80,0)`，不放大
- JPEG 编码：优先 ESP32-S3 硬件 JPEG 编码器；若不可用则用 `esp_jpeg`(libjpeg) 软编；正式实现与 `requirements.md` 统一为 Q=60
- 帧率控制：目标 10-15fps，编码任务独立于电机控制任务
- 编码后经 `rc_net_video_send` 发送

**当前实现与验证**: 已采用软件 JPEG Q60 编码路径。TANK正式路径固定使用102,400B staged-DVP、51,200B半缓冲、软件索引和逐帧暂停/恢复，不启动Tank本地视频预览，状态任务继续独占Tank LCD。UDP使用1200B应用层分片，Remote只显示完整重组帧。正式双端最后40个稳态窗口中，Tank发送平均`9.95fps`，JPEG平均`8,765B`；Remote使用`40MHz` SPI和60行DMA分块，显示平均`9.85fps`，`queue_drop=0`。清理版烧录后用户确认Remote画面正常、方向与100%亮度符合要求；偶发3～4像素窄叠影线作为挂起问题，不改变当前视频设计。

### 7.2 遥控器侧：接收与显示
- UDP 接收任务先按帧序和分片元数据在 PSRAM 槽内重组完整 JPEG，再软解码（`esp_jpeg` 解码为 RGB565）
- 网络接收和解码/显示使用独立任务；三个 PSRAM JPEG 槽通过 free/ready 队列交接，只有完整帧进入 ready 队列，显示任务只消费最新完整帧，过期完整帧可丢弃
- 单一合成/显示任务独占 LCD：显示初始化后立即以黑色底图合成左下摇杆，不等待网络或首个视频帧；视频解码只更新最新底图，触摸状态变化也能独立触发合成与刷屏
- RGB565 视频底图保存在 PSRAM，固定为 320×240 黑色画布；每个 240×180 解码帧按原尺寸写入右上角 `(80,0)`，不再放大到全屏，也不再分配/复制每帧 `composite_buf`
- 摇杆在每个 60 行 LCD DMA chunk 内按区域裁剪叠加，避免先生成整帧合成缓冲；LCD 仍由单一任务独占，逐块提交并等待完成
- 继续保留三个 JPEG 槽、只消费最新完整帧、Remote `40MHz` SPI、Tank `20MHz` 状态屏、60 行 DMA 分块和逐块完成等待；Round8 新增 `recv/decode/color/place/copy/overlay/dma/display/total` 分阶段日志及 `ready/queue_drop/stale_drop` 计数
- Round8 启动日志记录 JPEG 槽、解码/画布/DMA 缓冲大小、PSRAM/内部 RAM 余量和缓冲能力属性；已验证功能的高频逐帧日志未新增，错误日志与每 30 帧统计保留
- LCD 逻辑方向固定为横屏 320×240；Remote 视频链路使用 40MHz，Tank 状态屏使用 20MHz；丢帧/残缺帧跳过并保持上一帧。初始化使用 80 行 DMA 缓冲（240×80×2=38400B），视频提交使用 60 行块（320×60×2=38400B）

### 7.3 接口契约

```c
// rc_video.h（坦克）
esp_err_t rc_video_capture_init(void);
esp_err_t rc_video_capture_start(void);   // 启动采集+编码+发送任务

// rc_video.h（遥控器）
esp_err_t rc_video_display_init(lv_obj_t *canvas);
esp_err_t rc_video_display_start(void);    // 启动接收+解码+渲染任务
```

### 7.4 内存策略
- JPEG 帧缓冲、解码 RGB565 缓冲均分配在 PSRAM（8MB 充足）
- Tank 每次发送使用 `8+1200B` UDP datagram 临时缓冲；原完整 JPEG 发送缓冲继续位于 PSRAM，不申请内部 DMA 内存
- Remote 使用三个 JPEG 接收槽隔离网络接收与解码/显示；LCD 仅使用单独的内部 DMA 分块缓冲

### 7.5 Round8 合成源预算与实测

| 阶段 | Round8 实测 | 结论 |
|------|--------------|---------------|------|
| Tank 合成色块 | `9.374ms`，日志字段名为 `wait` | 合成源无 DVP/cache/subsample，不能外推摄像头性能 |
| Tank JPEG 编码 | `18.553ms`；平均 `4664.95B` | Tank 最大计算项，占 Tank `30.086ms` 的约 61.7% |
| Tank UDP 分片发送 | `2.160ms`；发送 `32.474fps` | 当前合成小帧下不是瓶颈 |
| Remote 完整帧重组 | `32.410fps`；`complete=12720, missing=27` | 序号完整率 `99.788%`；`missing` 不能区分发送失败与空口/接收侧丢失 |
| Remote 解码/颜色/画布 | `12.178/7.302/10.712ms` | 合计 `30.192ms`，均为 PSRAM 相关软件处理 |
| LCD chunk 复制/摇杆/DMA | `6.867/10.722/31.919ms` | 合计 `49.508ms`；40MHz 理论 payload `30.720ms`，DMA 仅多约 `1.199ms` |
| Remote 总处理/显示 | `80.006ms`，显示计数 `12.336fps` | 本地处理上限约 `12.5fps`，是当前屏显帧率瓶颈 |
| 最新帧队列 | `ready=12689, displayed=4830, stale_drop=7857, queue_drop=0` | `61.920%` 完整帧为保持低延迟被主动跳过，只有 `0.212%` 属于序号缺口 |

当前优化结论：正式Q60真实摄像头链路的Tank/Remote稳态吞吐约为`9.95/9.85fps`，符合需求的`10±2fps`验收带；JPEG平均`8,765B`、范围`8,314–9,018B`，平均8–12KB与峰值<20KB目标通过。启动关联窗口形成`missing=2`，后续不再增加；`stale_drop`是最新完整帧优先策略，`queue_drop=0`。视频协议没有跨设备可校准生成时间戳，不能把Tank与Remote阶段耗时相加当作实测端到端时延；该指标仍需同源外部测量或协议时间戳。

---

## 8. 音频层设计 (rc_audio)

### 8.1 遥控器侧：录音与编码
- SW3 (GPIO8 ADC) 按下(<3000)开始，松开(>3500)结束
- ES7210 采集 16kHz/mono/16bit PCM（参考 audio_demo）
- 录音结束后 Opus 编码（16kHz/mono/60ms，参考 EX-009）
- 整段编码完成后经 `rc_net_audio_send` 发送
- 时长限制：0.5s ~ 10s

### 8.2 坦克侧：解码与播放
- TCP 收齐整段语音包 → Opus 解码 → ES8311 播放（参考 xiaozhi_ai_demo）
- 音量固定 70%
- 播放期间 LED 蓝闪；多段排队播放不丢弃

### 8.3 接口契约

```c
// rc_audio.h（遥控器）
esp_err_t rc_audio_record_init(void);
bool rc_audio_sw3_pressed(void);           // 查询 SW3 状态
esp_err_t rc_audio_record_and_send(void);  // 录音→编码→发送（阻塞至松开）

// rc_audio.h（坦克）
esp_err_t rc_audio_play_init(void);
esp_err_t rc_audio_play_start(void);       // 启动接收→解码→播放任务
```

---

## 9. 显示层设计 (rc_display)

### 9.1 遥控器 UI（240×180 右上视频 + 叠加）
```
┌────────────────────────────────┐
│ [电量]  ┌──── 视频 240×180 ────┐│  ← 视频贴右上角 (80,0)
│         │                     ││
│  ╭───╮  │                     ││
│  │摇杆│  │                     ││  ← 摇杆叠加于左下区域
│  ╰───╯  └─────────────────────┘│
│                      [录音状态] │
└────────────────────────────────┘
```
- 直接 framebuffer 层级：320×240 黑色画布 < 右上视频 < 状态栏 < 摇杆 < 录音指示；合成/显示任务是 LCD 唯一所有者
- 摇杆呈现与视频帧到达解耦：零视频、摄像头失败或停流时仍按触摸状态刷新
- 电量左上角，帧率/信号右上角，录音红点右下角

### 9.2 坦克 UI（状态展示，不含视频）
```
┌────────────────────────────────┐
│ [电量]                  [WiFi]  │  ← 角落状态
│                                 │
│        坦克像素图（主体）        │
│                                 │
└────────────────────────────────┘
```
- 主体：坦克静态像素图（assets 内 PNG 或 LVGL image）
- 左上电量，右上 WiFi 状态图标（绿=连接/红=未连）；字形和颜色统一使用 LCD BGR565 大端传输契约
- WiFi 事件只发布状态并通知 Tank 显示任务，LCD 刷屏不在网络事件回调内执行

### 9.3 接口契约

```c
// rc_display.h
esp_err_t rc_display_init(void);
void rc_display_set_wifi_state(rc_net_state_t st);   // 更新 WiFi 图标
void rc_display_set_battery(uint8_t percent);        // 更新电量
// 遥控器专用
lv_obj_t *rc_display_get_video_canvas(void);
void rc_display_set_fps(uint8_t fps);
void rc_display_set_recording(bool on);
```

---

## 10. 电量检测设计 (rc_power)

- ADC 读取 `GPIO7 / BAT_ADC`（VBAT 分压），多次采样平均去抖
- 电压→百分比：分压系数和满/低电阈值待电池资料实测标定（design.md 记录充电芯片 ETA6002E8A）
- **待标定项**: 分压比、满电电压、低电阈值 —— 实现阶段先用占位系数，实测后修正
- BAT_ADC 仅作模拟输入（design.md 5.3 约束：不当数字 IO）

```c
// rc_power.h
esp_err_t rc_power_init(void);
uint16_t rc_power_get_voltage_mv(void);   // 换算后电压(mV)
uint8_t rc_power_get_percent(void);       // 电量百分比(0-100)
```

---

## 11. 状态机设计

### 11.1 坦克当前状态机
```
BOOT ──(10s延迟)──> INIT_HW ──> START_AP ──> WAIT_STA
                                                │
                        ┌──(STA连接)────────────┘
                        ▼
                     RUNNING ──(视频发送/控制执行/语音播放并行)
                        │
        ┌──(STA断开/超时)┘
        ▼
     DISCONNECTED ──(300ms控制超时停车)──> WAIT_STA
             └──(STA重新接入；通道任务保持单例)──> RUNNING
```

当前`AP_STADISCONNECTED`已发布断连状态，但“事件到达即直接停车”尚未实现；图中的300ms停车来自现有控制超时保护。

### 11.2 遥控器当前状态机（存在正式需求缺口）
```
BOOT ──(10s延迟)──> INIT_HW ──> SCAN_AP ──> CONNECTING
           │                                   │
           └──(无匹配AP)──> INIT_FAIL ──5s──> 软件复位
                     ┌──(连接成功)──────────────┘
                     ▼
                  RUNNING ──(视频显示/摇杆发送/录音发送并行)
                     │
     ┌──(WiFi断开)───┘
     ▼
  RECONNECT ──(最多5次内成功)──> RUNNING
           └──(耗尽)──> FAIL_BIT（当前不再扫描）
```

该图只描述当前代码和附件日志能够证明的行为，不是最终目标设计；requirements.md v0.33要求的循环扫描/重连和Remote左上状态图标仍待实现验证。

---

## 12. 初始化流程

### 12.1 坦克初始化顺序
1. app_main：10 秒启动延迟（XCR-028）
2. IOEX (TPT29555A @0x22) → RGB LED 红色
3. `rc_power_init`（BAT_ADC）
4. `rc_display_init`（LCD，显示坦克图 + 电量）
5. D0 检测（GPIO1 ADC ≈1.45V）→ `rc_motor_init`（GPIO 安全态）
6. `rc_video_capture_init`（SP0A39 DVP）
7. `rc_audio_play_init`（ES8311）
8. `rc_net_init` + `rc_net_start`（SoftAP）
9. 启动任务：视频发送、控制接收、语音播放、显示刷新

### 12.2 遥控器初始化顺序
1. app_main：10 秒启动延迟
2. IOEX → RGB LED 红色
3. `rc_power_init`
4. `rc_display_init`（LCD + 触摸 + LVGL）
5. `rc_joystick_init`（悬浮摇杆）
6. `rc_audio_record_init`（ES7210 + SW3 ADC）
7. `rc_video_display_init`（canvas）
8. `rc_net_init` + `rc_net_start`（STA 扫描连接）
9. 启动任务：视频接收显示、摇杆发送、录音发送、显示刷新

---

## 13. 任务与资源规划

| 任务 | 角色 | 优先级 | 栈 | 说明 |
|------|------|--------|-----|------|
| video_tx | 坦克 | 中 | 4KB | 采集+编码+发送 |
| ctrl_rx | 坦克 | 高 | 3KB | UDP 接收+电机执行（实时性优先） |
| audio_play | 坦克 | 中 | 4KB | 接收+解码+播放 |
| safety_mon | 坦克 | 高 | 2KB | 50ms 超时停车检查 |
| video_rx | 遥控器 | 中 | 4KB | 接收+解码+渲染 |
| ctrl_tx | 遥控器 | 高 | 2KB | 摇杆采样+发送 |
| audio_rec | 遥控器 | 中 | 4KB | SW3 录音+编码+发送 |
| lvgl_tick | 遥控器 | 中 | 6KB | LVGL 刷新 |
| ui_refresh | 双 | 低 | 2KB | 电量/状态刷新(5s) |

大缓冲（JPEG、PCM、RGB565）统一分配 PSRAM。

---

## 14. 错误处理

| 场景 | 处理 |
|------|------|
| WiFi 连接失败 | 遥控器重试 3 次，屏显"未找到坦克"；坦克保持 SoftAP 等待 |
| 控制包超时(>300ms) | 坦克 SAFE_STOP |
| WiFi 断连 | 坦克立即停车 + 红闪；遥控器重连 |
| 控制包长度/固定字段/参数非法 | 丢弃，不响应、不更新目标/序号/超时，仅限频本地日志 |
| 控制包重复或旧序号 | 丢弃，不更新目标或超时；按最后有效新包继续300ms安全计时 |
| 电机PWM配置非法 | 电机控制初始化失败，保持全部输出为0，不启动运动状态机 |
| 视频帧残缺 | 跳过该帧，显示上一帧，不花屏 |
| 摄像头配置地址无响应或 ID 非 `0x0A39` | 初始化失败并释放本次 I2C device；不启动 DVP，控制/音频/网络继续运行 |
| 视频尚未到达或中途停流 | 保持黑色/上一帧底图，摇杆继续独立显示和刷新 |
| JPEG 编码失败 | 跳过该帧，记录日志，不崩溃 |
| 语音包不完整 | 丢弃该段，不播放半段 |
| ADC 读取异常 | 电量显示保持上一有效值 |

---

## 15. 风险与待确认项

| 编号 | 风险/待确认 | 说明 | 处理策略 |
|------|------------|------|---------|
| R1 | ESP32-S3 JPEG 编码路径 | 正式软件Q60编码平均约`43ms`，Tank稳态发送约`9.95fps`；硬件编码路径仍未采用 | 已覆盖`10±2fps`、编码<80ms和平均8–12KB自动目标；保留硬编码为后续性能候选，不在人工画面验收前改动 |
| R2 | WiFi 点对点视频带宽实测 | 正式Q60链路启动关联窗口`missing=2`后至`complete=1620`不再增加，Tank/Remote稳态约`9.95/9.85fps` | 当前不提高发射功率或扩大buffer；先完成人工颜色/撕裂和长稳验收 |
| R3 | 电池分压系数/阈值 | design.md 未记录 BAT_ADC 分压比和电压阈值 | 实现用占位系数，实测标定后修正 |
| R4 | 三板供电（屏+摄+电机） | 用户确认正常插着能带动；仅热插拔摄像头瞬间出现 COM 枚举异常 | 不作为供电风险；避免运行中热插拔摄像头 |
| R5 | 坦克像素图资源 | 遥控器/坦克屏幕需一张简易坦克图形 | 用 LVGL 内置绘图（矩形+线条拼简易坦克，车体+炮塔+履带），不依赖外部 PNG，保持简单 |
| R6 | LCD 全屏刷新 | Remote 40MHz、60行分块与逐块等待已完成人工画面确认；Tank 20MHz状态屏无DMA容量错误 | 保持当前参数；偶发3～4像素窄叠影线按用户指令挂起，不继续枚举LCD变量 |
| R7 | 音视频并发资源 | 坦克同时跑摄像头 DVP + I2S 播放 + 电机 | 任务优先级隔离，实测 CPU/内存占用 |
| R8 | 电机PWM曲线与启动阈值 | 当前只有“0%停止、100%全速、50%以下很弱且难启动”的人工事实，首版 `1→50%`、`start=60%/60ms` 尚未实测标定 | 纯C只验证算法契约；后续满载、最低允许电量和高阻力路面分别标定四方向槽，不把默认值表述为实机通过 |

---

## 16. 验证入口

- 当前唯一推荐构建入口：在本目录执行 `.\build_rc_tank.ps1 -Role tank -Clean` 或 `.\build_rc_tank.ps1 -Role remote -Clean`。脚本默认把本工程镜像到 `%TEMP%\laiwfs300_build\rc_tank_demo`，把公共 `CODE/components` 镜像到其 `components/` 子目录，按角色设置`SDKCONFIG_DEFAULTS`，使用ESP-IDF v5.5.4、CMake 3.30.2、Ninja 1.12.1完成clean build，并把三段制件发布到源码目录`firmware_backup/*_TANK.bin`或`*_REMOTE.bin`
- 不直接拼写`idf.py`环境，不使用`build_tank*.bat`、`build_remote.bat`、`rebuild_remote*.{ps1,bat}`或`mirror_and_build_remote.ps1`作为正式构建入口；这些脚本只作历史兼容，具体用途与限制见 [README.md](README.md#脚本用途)
- GCC 14.2.0 若在 ESP-IDF/managed component 的 IRA pass 偶发 `internal compiler error: Segmentation fault`，保留 clean 已生成的镜像和构建图，以同一 Ninja 构建图执行 `ninja -C <镜像>\build -j1 all` 增量续编；不得据此修改业务代码、工具链或配置
- 当前唯一推荐烧录入口：`.\flash_rc_tank.ps1 -Port COM7 -Role TANK`与`.\flash_rc_tank.ps1 -Port COM24 -Role REMOTE`。脚本必须先`erase_flash`，再写入bootloader `0x0`、partition table `0x8000`和application `0x10000`，并确认三段均完成Hash校验；不使用默认跳过全片擦除的旧`.bat`烧录脚本
- 日志：COM7/COM24，115200 / 8N1；阶段证据保存在 `verification/round*_*.log`
- COM7 硬复位可能进入 `boot:0x3 DOWNLOAD(USB/UART0)`。已验证的临时启动入口是 `D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe -m esptool --chip esp32s3 --port COM7 --before default_reset --after no_reset load_ram <Tank构建目录>\build\bootloader\bootloader.bin`，用于从 RAM 启动二级 bootloader 并运行 Flash 中应用；不得使用当前会解析到Windows Store Python 3.13的裸`python -m esptool`。该现象仍需人工检查 GPIO0/BOOT 电路
- 主机纯 C 回归入口：`tests/run_host_tests.ps1`，支持可选`-TestName`聚焦单项。2026-08-26已使用LLVM-MinGW Clang 22.1.8、目标`x86_64-w64-windows-gnu`完成编译、链接和执行；脚本登记的21项测试全部`exit=0`，其中包含V1协议、连续摇杆、发送策略和Tank驱动状态测试，已删除的历史启动策略测试不再注册
- 需两套硬件（坦克 + 遥控器）分别烧录对应角色固件

### 16.1 2026-08-18 本轮交付证据

- 纯 C：`tests/run_host_tests.ps1` 共 11 项全部通过，覆盖摄像头地址/ID、空 DVP 保护、无视频摇杆合成、控制保活、Tank `75%` 字形/颜色及既有网络/视频/RLE 回归。
- Remote clean build：`1425/1425`，`CONFIG_RC_TANK_ROLE_REMOTE=y`，应用 `0x111b40`，产物保存到 `firmware_backup/*_REMOTE.bin`。
- Tank clean build：`1425/1425`，`CONFIG_RC_TANK_ROLE_TANK=y`，应用 `0xfff40`，产物保存到 `firmware_backup/*_TANK.bin`。
- Tank(COM7)：全片擦除成功，bootloader、partition table、application 三段均 `Hash of data verified`；应用 SHA-256 为 `1FAA39E26DC6B2AD7B14726F840821485D0A30D1E3CCF4F9715F6B3AAF705BB2`。
- Tank 启动日志：`verification/ex035_20260818_tank_com7_ram_boot.log`。日志确认 TANK 角色、SP0A39 地址 `0x21`/ID `0x0A39`、显示/相机/音频/SoftAP/控制任务启动，无 Panic/Guru/abort；日志 SHA-256 为 `B6CE40D051BAE7ED6A17DCD7E61106B7E52634721582474E81664595B224B9CF`。
- COM7 硬复位后仍会无日志，使用本节既有 `load_ram` 入口配合 `tools/serial_capture.py` 被动模式可启动并抓取；这是 GPIO0/BOOT 复位采样硬件问题，不归因于本次应用重启。
- Remote(COM24)：15 轮上限内始终未枚举；PnP 历史项 `USB-SERIAL CH340 (COM24)` 为 `Present=False`。因此不得宣称本轮 Remote 烧录、双端连接、帧率、白屏/撕裂或持续控制已通过实板验证。
- 分阶段验证：
  1. 单角色编译通过（TANK / REMOTE 各一次 clean build）
  2. WiFi 连接建立（坦克 AP + 遥控器 STA 握手日志）
  3. 控制链路（摇杆→电机响应）
  4. 视频链路（采集→显示，实测帧率）
  5. 语音链路（录音→播放）
  6. 失联安全（断连停车）
  7. 长时稳定性（30 分钟）

---

## 17. 阶段计划

| 阶段 | 内容 | 前置 |
|------|------|------|
| P0 | 工程骨架 + Kconfig 角色切换 + 双角色空跑编译 | 需求/设计确认 |
| P1 | rc_net WiFi 点对点连接建立 | P0 |
| P2 | rc_control 控制链路（摇杆+电机+失联停车） | P1 |
| P3 | rc_video 视频链路（含 JPEG 编码验证 R1） | P1 |
| P4 | rc_audio 语音链路 | P1 |
| P5 | rc_display UI + rc_power 电量 | P2/P3 |
| P6 | 集成联调 + 稳定性验证 | P2-P5 |

---

### 16.2 2026-08-19 Round 1/2 摄像头调优交付证据

- 变更边界：`rc_video_yuv_scale.c` 对齐 `camera_display_demo` 的 VYUY/RGB565_BE 颜色契约；`rc_video_display_plan.h` 统一 Tank `20MHz`、Remote `40MHz`、80 行初始化 DMA 和 60 行视频块；`rc_video.c` 移除 Remote 二次红蓝交换并保持 PSRAM 解码/画布、内部 DMA chunk；过时摇杆纯 C 断言更新为当前已确认几何，未改摇杆协议或电机逻辑。
- 纯 C：`tests/run_host_tests.ps1` 共 18 个程序全部 `exit=0`；LLVM-MinGW clang 22.1.8，目标 `x86_64-w64-windows-gnu`。
- 构建：Tank/Remote clean build 均 `1431/1431`；Tank 应用 `0x101860`（1,054,816B），Remote 应用 `0x1123e0`（1,123,296B）；角色配置分别为 `CONFIG_RC_TANK_ROLE_TANK=y`、`CONFIG_RC_TANK_ROLE_REMOTE=y`。
- 烧录：COM7、COM24 均先 `erase_flash` 成功，再写入 bootloader、partition table、application；每端三段均 `Hash of data verified`。COM7 常规硬复位后无日志，使用已验证 `load_ram bootloader.bin` 入口启动 Flash 应用。
- Tank 日志：SP0A39 ID `0x0A39`，采集约 `25.2fps`；17 个稳定发送窗口为 `12.6–13.0fps`、平均 `12.81fps`；`capture_size_errors=0`、`capture_identity_errors=0`。发送稳定窗口总处理约 `76–78ms`，未见持续发送失败。JPEG 18 个窗口平均 `16,684.83B`、范围 `15,003–19,062B`，平均大小目标未达，样本峰值目标通过。
- Remote 日志：`pclk=40000000`，理论 SPI 上限 `32.55fps`；16 个稳定窗口显示 `11.58–11.90fps`、平均 `11.79fps`，`missing=0`、`queue_drop=0`，`stale_drop` 为低延迟策略主动跳过旧完整帧；平均解码约 `21ms`、DMA 约 `33.5ms`、显示总段约 `52ms`、总处理约 `84–86ms`。未见 DMA 容量/越界、JPEG 解码失败、Panic/Guru/abort 或内存分配失败。
- 采样边界：第一次 Round 2 采样正值处于重烧录后的断线窗口，Remote 控制发送出现 `ESP_ERR_INVALID_STATE` 高频日志；复位 Remote 后重新关联 Tank AP，第二次采样形成上述稳定证据。该瞬态未改变视频协议和控制逻辑，本轮不引入无关修改。

### 16.3 2026-08-19 摄像头调优 Round 4 交付证据

- **单变量改动**: 仅将 `main/rc_video_format.h` 的 `RC_VIDEO_JPEG_QUALITY` 从 `45U` 调整为 `30U`；分辨率、颜色契约、UDP 1200B 分片、PSRAM 槽、LCD DMA、摇杆和控制协议未改。
- **纯 C 门禁**: 先执行 Round 4 RED，`test_rc_video_format` 因源码仍为 Q45 以 `exit=13` 失败；修改后重新执行 `tests/run_host_tests.ps1`，18 个程序全部 `exit=0`。工具链为 LLVM-MinGW clang 22.1.8，目标 `x86_64-w64-windows-gnu`。
- **构建与配置**: Tank/Remote clean build 均 `1431/1431`；Tank 应用 `0x101860`（1,054,816B，角色 `CONFIG_RC_TANK_ROLE_TANK=y`），Remote 应用 `0x1123e0`（1,123,296B，角色 `CONFIG_RC_TANK_ROLE_REMOTE=y`）。
- **烧录**: COM7、COM24 均先 `erase_flash` 成功，再写入 bootloader、partition table、application；每端三段均 `Hash of data verified`。COM7 使用已验证 `load_ram bootloader_TANK.bin` 入口后被动抓取，Remote 使用 `serial_capture.py reset` 抓取。
- **Tank 稳态日志**: 采集 `24.9–25.7fps`（平均 `25.25fps`），发送窗口（排除关联瞬态）约 `14.4fps`，编码约 `27.3ms`，总处理约 `68.2ms`；JPEG 平均 `11,495B`，范围 `10,037–13,677B`，`capture_size_errors=0`、`capture_identity_errors=0`。关联前出现 `errno=12`/`ESP_ERR_INVALID_STATE` 发送失败，关联后窗口稳定。
- **Remote 稳态日志**: `pclk=40000000`，显示窗口约 `12.30fps`（`12.04–12.43fps`），总处理约 `81.0ms`；`queue_drop=0`，`stale_drop` 为低延迟策略主动丢弃旧完整帧。启动关联窗口形成 `missing=2`，后续保持不增加；未见 JPEG 解码失败、DMA 越界/超时、Panic/Guru/abort 或分配失败。
- **交付产物**: Tank/Remote 应用 SHA-256 分别为 `04AC9D4DDF63900063C7A171808A638BA3BE2E8DFD54C106F76F3BA76A909EF1`、`5B8F0C8AFA5D3C54A4573C77E9B2AC345017A554B36BCAA23144BDAE6F3BDBE9`；原始运行日志保存在 `verification/round4_tank_com7.log` 和 `verification/round4_remote_com24.log`。
- **结论与边界**: Q30 使平均 JPEG 大小和自动帧率目标均通过，因此在第 4 轮提前结束，不进入 Round 5–10。真实颜色、撕裂、机械四向控制、端到端时延和 30 分钟长稳仍需人工验证；正式需求 Q60 是否改为 Q30 待确认。历史性能实验中的“Round 4”与本节“摄像头调优 Round 4”属于不同轮次序列。

**后续总体验收**: 摄像头调优 Round 4 已达到 10fps 与平均 8–12KB 自动目标；当前先按第18节完成 Tank 源头稳定。源头稳定后再人工确认真实摄像头颜色和撕裂、摇杆四向持续控制及松手停车，随后补做30分钟长稳、电量标定，并确认正式Q60是否由当前Q30取代。

---

## 18. 摄像头画面专项当前设计（2026-08-21，当前生效）

### 18.1 当前问题边界

- A0～C14.1 的完整历史、逐轮变更和证据见 `../../../tanklog/2026-08-20_rc-tank-camera-debug-a0-c12-summary-and-next-plan.md`；文件名为稳定历史入口，本节只保留当前仍有效的设计。
- Tank 无 WiFi 的 B3.1 路径采用 `rc_capture_pool + backup`、raw直接分块缩放转换、240×180 `(80,0)`、20MHz和60行DMA chunk，是当前回归基线；不恢复已证明会恶化的整帧PSRAM画布路径。
- SoftAP单独运行可触发Tank本地真实画面异常，关闭WiFi无需重启DVP即可恢复；现有约25.25fps、帧完成、长度和槽所有权统计不能证明像素正确。
- C13 已证明相同 DVP/WiFi 负载下内部 DMA 确定性合成图稳定，而真实摄像头图异常；公共 LCD presenter、SPI DMA、chunk 复用和完成等待不是当前主嫌疑。
- C14.1 已证明固定传感器图在 WiFi 开启时完成帧 RAW 指纹直接偏离，且对应转换 chunk 同步变化；RAW→RGB 不是当前主根因。当前继续切分传感器输出、DVP DMA 写入与 PSRAM/cache 完成帧形成，不再枚举任务核、WiFi TX 功率或 DMA burst。
- Remote正式界面保持320×240横屏画布、视频240×180右上角`(80,0)`；Tank源头未稳定前不修改Remote。
- 音频优先级降到最低，图像传输、显示和控制完成前不实现、不恢复音频链路。

### 18.2 C13 已裁决的显示责任边界

- 摄像头启动后始终持续 DVP 采集；C13 只在真实 raw 转换输出与内部 DMA 确定性合成图之间切换 LCD 输入，共用同一 `submit/wait_pending` 生命周期。
- WiFi 开启时，真实源窗口 3/5 人工观察到与 C6/C7 同级的错位、撕裂和错色；内部合成图窗口 4 稳定，无抖动或撕裂。WiFi 关闭后的合成图窗口 6 和真实源窗口 7同样稳定。
- 合成图窗口的首/中/尾 chunk CRC 各自保持固定，`submit=complete=3`、提交/完成序号一致、`stale=0`；真实和合成窗口均 `cache_sync=ESP_OK`、槽 generation 前后相同。
- 因此公共 presenter/LCD 主链已形成同负载稳定证据。后续除非固定 raw 与转换后 chunk 都稳定而肉眼仍异常，否则不重新打开 LCD 分支。

### 18.3 C14.1：固定传感器图与 raw 指纹裁决（已完成）

C14.1 保持 DVP、帧池、LCD presenter 和转换算法不变，只切换乐鑫官方 SP0A39 test-pattern bit，并低频读取同一 READING 槽。每个编号窗口约 30 秒：

| 窗口 | WiFi | 传感器输出 | 判定用途 |
|---|---|---|---|
| 1 | off | 摄像头未启动，黑屏 | 启动标记 |
| 2 | off | 真实场景 | 回归 B3.1/C13 无 WiFi 基线 |
| 3 | off | 固定测试图 | 建立固定 raw 指纹基线 |
| 4 | on | 固定测试图 | 直接观察 WiFi 是否改变共同上游 raw 数据 |
| 5 | on | 真实场景 | 回归主故障并保留自然场景对照 |
| 6 | off | 固定测试图 | 验证关闭 WiFi 后 fixed raw 是否恢复 |
| 7 | off | 真实场景 | 验证真实画面可逆恢复并持续保持 |

实现约束：

- test pattern 仅按官方 `P1:0x32[7]` 做 read-modify-write，切换后恢复 Page 0；寄存器其他位不作为稳定回读契约。实机已证明写入 `0x95` 时可读回 `0x80` 且彩色块已生效，因此成功条件只检查 bit7。
- 切换传感器图时不停止或重建 DVP，等待 100ms 冲掉旧帧后再切窗口号。
- probe 在 cache 同步后、转换前记录 `raw_full`、四个 `raw_q` 和逐行首尾滚动 `raw_edge`；转换后继续记录首/中/尾 `chunk_crc`、submit/complete 与 sequence。
- probe 只读当前 READING 槽，不改变帧池状态、generation 或 backup 策略；当前 bitwise CRC 单次约 623ms，属于诊断扰动，不能作为产品帧率数据。
- DVP 保持 640×480 VYUY、direct 模式、外部 PSRAM、合法 burst=64；真实源保持 240×180 `(80,0)`、20MHz、60 行 DMA chunk。

判定规则：

- 窗口 3 固定指纹稳定、窗口 4 的 raw 指纹或分区关系异常、窗口 6 恢复：优先定位传感器输出、DVP DMA、PSRAM/cache 共同上游。
- 窗口 3/4 的 raw 指纹稳定，而窗口 4 的 `chunk_crc` 或画面异常：优先定位 PSRAM 读取或 raw→RGB 转换在 WiFi 并发下的行为。
- raw 指纹与 `chunk_crc` 均稳定，而肉眼仍异常：与 C13 结论冲突，重新审计 LCD 提交证据，不直接修改 raw 链。
- 窗口 3 在 WiFi 关闭时也不能形成稳定 fixed raw：test-pattern/指纹反馈环无效，停止根因宣称并先修正诊断方法。

实测裁决：

| 窗口 | 人工画面 | RAW/转换日志 | 结论 |
|---|---|---|---|
| 1 | 黑屏 | 摄像头未启动 | 符合启动标记 |
| 2 | 基本正常，偶发闪烁或错位 | 自然场景 13 个样本均 `stable=1`，流程字段正常 | 保留无 WiFi 真实图基线，仍有低频异常 |
| 3 | 色块正常、无错位，约每 1～2 秒偶发撕裂 | 9/9 样本为同一 `raw_full/raw_q/raw_edge/chunk_crc` | 固定图基线成立；低频探针可能漏过瞬态事件 |
| 4 | 色块明显撕裂、错位和闪烁 | 9 个样本中 4 个保持窗口3基准，5 个形成不同 RAW 指纹；对应分区与 chunk CRC 同时改变 | WiFi 开启时异常已发生在 RAW→RGB 前 |
| 5 | 真实画面明显撕裂和闪烁 | 自然场景样本流程字段正常 | 回归主故障成立 |
| 6 | 色块偶发闪烁，接近窗口3 | 9 个样本中 8 个保持本次启用基准、1 个 RAW/chunk 同时偏离 | 关闭 WiFi 后显著改善但未归零，WiFi 不是唯一触发源 |
| 7 | 真实画面偶发闪烁和撕裂 | 自然场景样本流程字段正常 | 与窗口6的低频残留一致 |

- 全部固定图异常样本均为 `stable=1`、`cache_sync=ESP_OK`、`cache_sync_err=0`、`submit=complete=3`、sequence 一致且 `stale=0`。这表示帧在消费者持有和转换期间没有继续变化，错误内容在取得“完成帧”时已经存在。
- 窗口4变化的 RAW 分区与转换后 chunk CRC 同时变化，故转换输出是在反映已损坏的 RAW；RAW→RGB 作为主根因显著降权，LCD 分支继续保持关闭。
- 指纹探针约每 3 秒采样一次，以上 `5/9`、`1/9` 是日志抽样偏离率，不等同于人工观察到的闪烁频率。
- 窗口3和窗口6是两次独立启用固定图，二者的众数指纹不同；`P1:0x32`除bit7外的读回值也发生变化。当前只比较每次启用窗口内部的众数与离群值，不把跨启用指纹不一致直接归因于图像损坏；该差异保留为后续诊断注意项。

### 18.4 C15/C15.2：稳定 Gray 链路上的分辨率前进（当前生效）

C15 的有效成果是 200×200 Gray8 在 WiFi 关/开/关七窗口中均无撕裂、闪烁、错位突变和重启；C15.2 在不回退原 640×480 VYUY 链路的前提下，只恢复官方 VGA 空间分辨率。当前自动门禁和人工观察均已通过：W2/W5/W7 是稳定可辨识的黑白摄像头画面，W3/W4/W6 是稳定黑白测试色块，所有窗口均无撕裂、闪烁或重启。因此 C15.2 作为后续恢复彩色产品链路时的当前诊断基线。

- C15 稳定基线继续保留乐鑫 `esp_cam_sensor v2.4.0` 官方 `DVP_8bit_24Minput_Gray_200x200_30fps` 表、既有模式枚举和 `board_laiwfs300_camera_init_gray_200x200()`；不得删除或反向改写该入口。
- C15.2 新增同一官方包的 `DVP_8bit_24Minput_Gray_640x480_30fps` 精确副本，保留 Apache-2.0 标识。`camera_hal` 以加法式枚举和板级 `board_laiwfs300_camera_init_gray_640x480()` 选择该表；零值仍为既有 VYUY，其他调用方行为不变。
- 仅 `CONFIG_RC_TANK_CAMERA_DIAG_C=y` 的当前诊断构建使用 `640×480`、`CAM_CTLR_COLOR_GRAY8`、每像素1字节和 `307,200B` 逻辑载荷。它是旧 VGA VYUY `614,400B` 的一半，不恢复彩色双字节采集；DVP burst 保持64。
- 每个 DMA 槽和 backup 仍分配 `614,400B` 承载区及 `256B` 终端保护。转换、cache 同步和 RAW 指纹改为读取声明的 `307,200B`，终端保护位置不移动，以继续隔离并检测实际写入超出承载区的回归。
- Gray8 最近邻缩放、逐行镜像、240×180 `(80,0)`、20MHz、60行 chunk、公共 submit/wait、帧池/backup、generation 和完成帧所有权均不变；不恢复全画布或 staged DMA。
- 七个30秒窗口、100ms settle、test-pattern `P1:0x32[7]`、`raw_full/raw_q/raw_edge`、三段 chunk CRC和提交完成探针全部不变。JPEG/UDP/video_tx、Remote、控制和音频继续冻结。

C15.2 自动否决门禁：

1. 必须完整进入 W1～W7、打印 sequence-complete 并在 W7 继续保持；不得出现 Panic/Assert/WDT、主动异常重启、堆损坏、guard错误、槽重叠、cache同步失败或LCD提交未完成。
2. 窗口3、4、6的固定 RAW 必须在各自窗口内建立稳定众数；若 WiFi 开启的窗口4重新出现 C14.1 级别离群，判定带宽回归，候选不得交给人工观察。
3. 自动门禁通过后由人工观察 W2/W5/W7 的取景范围、重影和灰度层次；本轮人工已确认稳定可辨识黑白画面，画面稳定性目标通过，但不得扩张为正式彩色产品画面已通过。
4. 候选失败时保留 C15 的 200×200 Gray8、614,400B承载区和终端保护作为回退边界，再选择不触碰LCD/Remote/控制的单变量方案。

### 18.5 Tank稳定后的集成顺序

1. 先在Tank侧以当前320宽/raw输入链路稳定。
2. 再在Tank侧验证240×180右上角`(80,0)`稳定，避免到Remote才重新处理缩放/几何问题。
3. 恢复WiFi与控制但保持video_tx关闭，回归Tank本地画面。
4. 启动JPEG TX；Tank记录`seq/raw或test-pattern指纹/jpeg_crc`。
5. Remote按同一`seq`比较重组JPEG CRC，再检查decoded RGB、canvas和LCD chunk；残缺帧继续丢弃。
6. 完成Remote画面后再验收四向控制、失联停车和30分钟长稳；音频继续延后。

### 18.6 不触碰项与验证边界

- C15/C15.2 不改 JPEG Q、UDP 分片协议、socket buffer、摇杆、电机方向、失联停车、Remote 显示或音频协议。
- 不重复C8/C8.1 staged实现，不再测试WiFi TX功率、DVP/WiFi核绑定、burst32/64/128或未经确认的SP0A39寄存器。
- 用户已持续授权本专项编辑、编译、全片擦除、烧录和复位，并持续授权跳过纯C验证，直到明确撤销；该授权不包含Git。C14/C14.1/C15/C15.2 已按授权完全跳过纯 C，不得把未运行写成通过。
- 构建、擦除、烧录、RAM bootloader启动和串口抓取仍必须复用第16节成熟入口。
- C15.2 已逐项守住 C15 稳定门禁：Tank clean build、COM7全片擦除、三段烧录、RAM bootloader启动和完整W1～W7自动运行均完成；无Panic/Assert/WDT/重启、终端保护破坏、帧池所有权或cache同步错误，人工画面亦稳定。后续候选必须继续守住该边界；纯C按持续授权完全跳过，状态为“未运行”，不是“通过”。

### 18.7 C16a～C16g：DVP读写串行化与彩色恢复裁决（历史过程，结论保留）

事实与假设边界：

- C13 已证明摄像头持续写 PSRAM、WiFi 开启且 LCD 显示内部确定性图时稳定；C14.1 在同一条件改为读取摄像头完成帧后，RAW 在转换前损坏；C15.2 把每帧读写量降为 Gray8 后稳定。因此C16a启动时的最高优先级假设是“CPU/LCD读取摄像头帧时，下一帧DVP仍并发写PSRAM”放大了WiFi下的争用或可见性问题；C16a～C16e已据此完成串行化，后续证据表明该措施必要但不足以关闭全部异常。
- C16 不猜 SP0A39 帧率寄存器。现有资料只有 SPI 4-bit YUV422 15fps 表，没有可直接用于本板 DVP 8-bit 的官方15fps彩色表；不得把 SPI 表或未经确认的寄存器移植到 DVP。
- 用户允许 Remote 肉眼稳定12fps、摄像头约15fps。C16通过完成帧后暂停/重启 DVP 自然降低有效采集与显示频率，不修改外部20MHz MCLK和传感器表。

C16a 单变量：

1. 传感器、DVP格式、消费者和画面仍保持 C15.2 的官方 `640×480 Gray8`、`307,200B`逻辑载荷。
2. 显示任务取得一个完成帧后执行 `stop -> disable`；DVP关闭期间完成cache同步、RAW探针、Gray8缩放和LCD分块提交。
3. 完成帧释放后重置帧池、清空旧信号，再执行 `enable -> start` 捕获下一帧；不得在正在读取的帧上重启DVP。
4. 每次暂停、恢复和失败均累计并打印诊断计数；任一生命周期错误、帧池错误、guard错误或重启均否决候选。

C16b 单变量：

1. 仅在C16a完整通过后，把诊断传感器入口切回既有原厂 `640×480 VYUY` 表，DVP改为`CAM_CTLR_COLOR_YUV422`、`614,400B`逻辑载荷。
2. 消费者改回已验证的 `rc_video_scale_vyuy_to_lcd_bgr565_region()` 直接分块转换；240×180 `(80,0)`、20MHz、60行chunk和窗口标记不变。
3. 继续保留每槽/backup `614,400B`承载区、256B终端保护、完成帧所有权、RAW分区指纹和固定图窗口；不得新增全帧副本。

C16a/C16b 已形成的实板事实：

- C16a 自动运行完整进入W1～W7，最终探针帧1290，`quiesce=resume=1290`，生命周期、guard、槽重叠、cache和重启错误均为0；窗口3/4/6固定图在各自窗口内各只有1种RAW指纹。人工确认W7黑白画面稳定、无撕裂、无闪烁。
- C16b 自动运行完整进入W1～W7，最终探针帧1230，`quiesce=resume=1230`，同类错误均为0；但固定图窗口4在WiFi开启后7个探针出现4种RAW指纹，窗口3和6各为1种。人工确认W7已恢复彩色和完整画面，同时约2秒出现一次低概率闪烁/错位，整体亮度明显偏暗。
- 因 C16b 的完成帧在CPU读取期间保持稳定且生命周期无错，当时已降低“LCD提交覆盖当前帧”的优先级；C16b之后曾优先假设控制器每次 `enable` 重置CAM/FIFO后取得的首个完成帧偶发处于不稳定入帧边界，后续C16c实板已否决“只坏在首帧”。

C16c 单变量：

1. 保持C16b的传感器表、VYUY格式、帧大小、停采/复采顺序、LCD参数和七窗口时序不变。
2. 每次 `enable -> start` 后设置“丢弃首个完成帧”标志；显示任务取得该帧后仅释放读所有权，不暂停DVP、不转换、不提交LCD，继续等待同一连续采集阶段的下一个完成帧。
3. 只有第二个完成帧进入既有 `stop -> disable -> cache/RAW/转换/LCD -> release/reset -> enable -> start` 流程。记录累计丢弃计数，并要求其与恢复周期一致；不得通过放宽错误检查掩盖失败。
4. C16c只验证几何/闪烁稳定性，不同时调整亮度、色彩矩阵或传感器曝光，避免无法区分稳定性与画质变量。

C16c 裁决与C16d单变量：

- C16c 自动运行完整进入W1～W7，最终探针帧960，`quiesce=resume=discard_first=960`，生命周期、guard、槽重叠、cache和重启错误均为0；但窗口4固定图6个探针仍有4种RAW指纹，与C16b的4种相同，故“只坏在DVP重启后首个完成帧”假设被否决，丢首帧逻辑不得保留。
- C16d恢复C16b的每次恢复后直接消费首个完成帧，只在Tank构建启用ESP-IDF官方`CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=y`。该配置把额外的高频WiFi库函数放入IRAM，官方说明约增加5KB IRAM占用并提高WiFi吞吐；它不修改WiFi协议、TX功率、任务核、DVP burst、传感器表或画面链路。
- C16d假设是WiFi高频代码/中断路径带来的外部存储和时序压力放大了VYUY全帧DVP写入离群。构建后必须核对内部RAM余量；若分配失败、重启或窗口4固定图未收敛，则该配置候选否决，不继续叠加缓存安全、ISR或传感器变量。

C16d裁决与C16e单变量：

- C16d 自动运行完整进入W1～W7，最终探针帧1230，`quiesce=resume=1230`，生命周期、guard、槽重叠、cache和重启错误均为0；窗口4固定图7个探针仍有5种RAW指纹，窗口6也出现2种，未见优于C16b的自动证据。W7已执行`esp_wifi_stop()`，所以用户在W7观察到的周期性低概率闪烁/错位不能由WiFi热代码路径直接解释；`CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=y`候选否决并撤销。
- C16b～C16d 的显示任务在一帧完成并恢复DVP后，才执行每30帧一次的全帧RAW指纹日志。W7日志显示每次RAW探针约`594ms`，随后两条长串口日志在下一帧DVP采集已经开始时输出；115200波特串口输出可覆盖下一帧采集的大部分时间。该时序相关性随后由C16e实板验证为“可显著改善非WiFi窗口但不能关闭W4/W5及W7全部异常”，不得再当作完整根因。
- C16e保持C16b的传感器表、VYUY、三槽加backup、逐帧`stop -> disable`、直接240×180转换、LCD 20MHz/60行chunk和七窗口时序不变；只把`enable -> start`移到当前帧的RAW探针、LCD提交、稳定性核对及诊断日志全部完成之后。诊断失败出口也必须先在DVP停止态记录，再恢复采集，避免日志与下一帧DMA重叠。
- C16e不采用C8/C8.1的内部DMA staging，也不改为参考工程的单次拍照生命周期。参考工程的“4个控制器DMA buffer、跳过首帧、停采后复制稳定PSRAM”仅作为后续候选依据；C8.1曾同时改变数据搬运和恢复行为且失败，当前先验证更小、更可解释的日志时序单变量。

C16e人工裁决、C16f裁决与C16g多窗口矩阵：

- C16e人工确认W1黑屏；W2彩色人像正常、无低频异常且亮度足够；W3和W6固定图稳定；W4固定图与W5人像在WiFi开启时仍严重跳动、撕裂、错位和闪烁；W7彩色人像总体正常且亮度足够，但在约5s、10s、16s、20s随机出现极低频闪烁。C16e关闭了原先约2秒一次的普遍诊断日志干扰，但没有关闭WiFi并发和WiFi退出后的残余异常，人工候选否决。
- C16f不引入C8/C8.1 staged搬运，仅启用`CONFIG_CAM_CTLR_DVP_CAM_ISR_CACHE_SAFE=y`并把帧边界回调最小调用图放入IRAM。clean build、全片擦除、三段Hash校验和RAM bootloader启动均成功，运行日志进入W2～W7且无重启或生命周期错误；串口捕获开始于W2，未把漏抓W1表述为完整七窗口证据。
- C16f的W3固定图7个样本为1种指纹；W4的7个样本仍有3种指纹，人工确认W4色块与W5真实画面仍持续错位、撕裂和闪烁。cache-safe/IRAM没有改善主故障，C16g必须撤销该配置和应用IRAM标记，恢复C16e调用图。
- 旧全帧bitwise CRC每30帧才执行一次且单次约594ms，会漏掉人工看到的帧级异常。C16g新增逐帧轻量RAW签名：在614,400B完成帧上等距抽取16段、每段64B，共读取1,024B；每个窗口统计样本数、相邻变化次数、最多8种签名及溢出数。重探针仍保留为可切换变量，但不再作为唯一自动判据。
- C16g把窗口长度改为15秒，并将窗口号扩展为两位十进制显示。传感器保持现有640×480 VYUY表，DVP保持burst64、三槽加backup、逐帧`stop -> disable -> CPU/LCD -> enable -> start`，Remote、控制、JPEG和音频继续冻结。矩阵如下：

| 窗口 | WiFi | 传感器 | LCD模式 | 停止态帧间空闲 | 全帧重探针 | 判定用途 |
|---|---|---|---|---:|---|---|
| 1 | off | 相机未启动 | 黑屏 | - | off | 启动标记 |
| 2 | off | real | camera | 0ms | off | 无WiFi真实图基线 |
| 3 | off | pattern | camera | 0ms | off | 固定图逐帧签名基线 |
| 4 | on | pattern | camera | 0ms | off | WiFi故障基线，排除重探针参与 |
| 5 | on | pattern | camera | 0ms | on | 与W4成对比较重探针扰动 |
| 6 | on | pattern | hold | 0ms | off | 保持LCD不更新，只跑DVP和轻签名 |
| 7 | on | pattern | fixture | 0ms | off | LCD持续更新但不读RAW转RGB |
| 8 | on | pattern | camera | 20ms | off | 帧间空闲单变量1 |
| 9 | on | pattern | camera | 50ms | off | 帧间空闲单变量2 |
| 10 | on | pattern | camera | 100ms | off | 帧间空闲单变量3 |
| 11 | on | pattern | fixture | 100ms | off | LCD源隔离与100ms空闲组合 |
| 12 | on | real | camera | 0ms | off | WiFi真实图故障基线 |
| 13 | on | real | camera | 50ms | off | 真实图50ms空闲 |
| 14 | on | real | camera | 100ms | off | 真实图100ms空闲 |
| 15 | off | pattern | camera | 0ms | off | 关闭WiFi后的固定图恢复 |
| 16 | off | real | camera | 0ms | off | 最终真实图保持/人工观察入口 |

- `hold`窗口仍取得并释放完成帧、执行cache同步和轻签名，但除窗口号切换外不提交LCD；`fixture`窗口仍持续采集，却只提交内部DMA确定性图。帧间空闲发生在当前帧所有日志完成、下一次`enable -> start`之前，不与DVP写入重叠。
- C16g成对判据：W4/W5裁决重探针；W4/W6/W7裁决LCD提交、RAW读取转换和DVP上游；W4/W8/W9/W10检验空闲时长是否单调降低固定图签名变化；W11检验组合效应；W12/W13/W14映射真实画面；W15/W16验证WiFi关闭后的恢复。实板已确认所有WiFi固定图窗口仍多签名，因此0～100ms空闲与上述显示变量停止枚举，后续已经转入C16h～C16l的缓存、真实GDMA和SoftAP时序裁决。

回归影响矩阵：

| 影响面 | C16a/C16b评估 | 约束或证据 |
|---|---|---|
| 状态机 | 需要新增覆盖 | 只在诊断显示任务取得完成帧后进入`STARTED -> ENABLED -> INIT -> ENABLED -> STARTED`；W1未启相机，窗口时序不改 |
| 事件时序 | 存在实板风险 | stop/disable后才读帧；release/reset/drain后才enable/start，日志核对暂停/恢复一一对应 |
| 事务/帧身份 | 需要新增覆盖 | 读取期间DVP关闭，generation和首/中/尾CRC必须稳定；重启前清理被中止的WRITING槽 |
| 并发/任务 | 目标修改点 | 只消除DVP写与当前帧CPU/LCD读的重叠；WiFi、LCD任务核绑定和优先级不改 |
| 资源生命周期 | 需要新增覆盖 | 不重新分配帧；复用现有池、backup、guard和控制器，频繁enable/disable失败即否决 |
| 错误出口 | 需要新增覆盖 | 暂停或恢复失败必须记录并停止消费该候选，不得静默继续并宣称稳定 |
| 公共接口 | 不受影响 | 不新增板级或camera_hal公共API；C16b复用现有VYUY零值模式和板级初始化入口 |
| C6～C15.2历史问题 | 必须实板回归 | 七窗口、WiFi关/开/关、固定图RAW、guard、堆、所有权、cache和重启扫描全部保留 |
| Remote/控制/音频 | 不受影响 | 本轮继续冻结，不修改协议、socket、摇杆、电机或音频 |

C16c 对上述矩阵的增量影响：状态机仍使用C16a/C16b既有控制器状态迁移，只在帧池消费者侧增加一次“完成帧释放后继续等待”；事件时序和帧身份需要新增实板覆盖，丢弃帧不得进入cache读取、转换或LCD；资源不新增分配，公共接口、WiFi配置、传感器表和LCD契约均不受影响；丢弃标志/计数不一致、读所有权释放失败、生命周期错误或任一C6～C15.2回归均否决候选。

C16d 对上述矩阵的增量影响：状态机、事件时序、帧身份、帧池和公共接口恢复C16b；并发行为不改，只改变WiFi库代码驻留位置；内部IRAM资源需新增构建/启动余量覆盖，WiFi连接和窗口4/5行为需实板回归；错误出口、Remote/控制/音频协议不受影响。C16d不得保留C16c丢帧逻辑，否则无法单变量裁决。

C16e 对上述矩阵的增量影响：状态迁移集合不变，只延后每轮`enable -> start`；当前帧的RAW、转换、LCD和日志均在DVP停止态完成，下一帧采集期间不再有显示任务主动输出探针日志。帧池仍在恢复前完成release/reset/drain，资源分配、公共接口、传感器表、WiFi协议和LCD契约均不受影响。新增风险是停止态延长约`594ms + 串口输出时间`会降低探针帧附近的瞬时帧率，但用户已接受约12fps显示/约15fps摄像头边界，且该延迟只发生于诊断采样帧；任何恢复遗漏、计数不相等或W1～W7回归均否决候选。

C16f 对上述矩阵的增量影响已经实板否决；C16g撤销cache-safe和应用IRAM标记，不把失败变量带入新矩阵。C16g新增的诊断profile仅在Tank诊断构建内生效，包含窗口号、传感器固定图标志、`camera/hold/fixture`显示模式、0～100ms停止态空闲和重探针开关；窗口切换由单次API原子表达，产品构建不改变。轻签名只读完成帧，不改变generation、帧池状态或LCD数据；`hold`仍必须释放当前读槽并恢复下一帧。公共camera_hal、网络协议、Remote和产品接口不变。

C16g当时自动门禁：完整进入W1～W16、打印sequence-complete并保持W16；每个窗口打印profile和轻签名统计，所有暂停均需成功恢复，Panic/Assert/WDT/重启、堆、guard、帧池、cache和LCD错误为0。自动日志只比较签名与生命周期，不能替代camera/fixture窗口的人工画面判定。实际捕获漏掉W1/W2且W11存在夹具范围缺陷，因此未宣称完整门禁通过；其余成对证据仍有效并已用于否决重探针、LCD模式和0～100ms空闲。

### 18.8 C16h：缓存同步语义实板裁决（已完成，未通过）

- Tank clean build`1445/1445`，应用990,112B；COM7全片擦除、三段Hash校验和RAM启动成功。运行日志`verification/c16h_r1_full_runtime_tank_com7.log`为41,021B，SHA-256`6D0AEC784CADDC9299002D1D072680736202E32068735143A9C023643A242B1D`。
- 无WiFi固定图W3周期样本稳定；WiFi固定图的`M2C/NONE/C2M/C2M+INVALIDATE`四种模式两轮变化率均约67%～81%，没有模式收敛，所有帧`sync_changes=0`。
- sequence-complete后W16保持至设备约362秒；Panic/WDT/重启、guard、cache、槽重叠、完成冲突和生命周期错误扫描为0。COM7重枚举漏抓W1/W2起始标记，因此不宣称完整自动门禁通过。
- 实板结果只支持“应用层四种缓存同步模式不能修复当前故障”；不保留据此形成的候选设计，也不把C16h表述为正式彩色画面通过。

### 18.9 C16i～C16l：控制器并发与SoftAP时序的已验证诊断结论

- **C16i停止事实**：启动日志确认当前公开相机驱动创建的是直接DVP控制器/GDMA链路，运行态不存在可按名称取得的`dvp_task`。C16i在W2输出`TASK_PRIO_ERROR ... task=dvp_task ... reason=not_found`后按证据保护进入HOLD，未启动网络，也没有把不存在的任务优先级当作有效变量；该任务优先级路线不得重复。
- **C16j实板裁决**：单次W1～W20矩阵改为真实控制器GDMA优先级与SoftAP Beacon间隔组合。GDMA优先级变化没有产生可重复改善；延长Beacon间隔在部分固定图窗口降低轻签名变化率，但未稳定收敛，且用户确认最终W20画面明显变暗。C16j候选否决，不把GDMA优先级或单独延长Beacon作为后续修复方向。
- **C16k自动结果**：单次W1～W20矩阵比较了有效SoftAP协议组合、Beacon间隔与TSF采集避让。完整执行并在W20保持，无Panic/WDT/重启/HOLD；guard、生命周期、cache、槽重叠、完成冲突和TSF错误均为0。固定图中，一组TSF避让窗口在该轮取得`15/59`变化，优于同轮若干无避让或宽预算窗口，但单轮最低值不构成正式参数或产品设计验收。
- **C16l重复性矩阵**：保持C16k链路不变，集中重复无避让对照与多组TSF避让窗口。两组无避让固定图变化率为46.1%和51.3%，对应的多次避让对照降至16.9%～35.6%和22.0%～26.7%。实板重复证据支持“TSF避让能降低WiFi固定图变化率”，同时也证明它尚未达到零变化或完全确定性；具体候选参数不在本设计中固化。
- **C16l自动结果及后续裁决**：日志完整覆盖W1～W20，约`264656ms`打印矩阵完成，随后保持W20至设备约`387618ms`；无Panic/WDT/重启/HOLD，guard、生命周期、cache同步、槽重叠、完成冲突和TSF错误均为0。后续C16m精确相位实板仍出现秒级异常，证明TSF避让只能降低部分固定图变化率，不能形成正式彩色稳定方案。
- **防回退边界**：C16c丢首帧、C16d WiFi EXTRA IRAM、C16f cache-safe/IRAM、C16g重探针/LCD模式/0～100ms停止态空闲、C16h四种cache同步语义、C16i任务优先级和C16j GDMA优先级均已有否定证据，不再重复。C7/C9/C12已否定的分核、TX功率和burst粒度同样继续冻结。
- **验证边界**：C16i～C16l均按用户持续授权完全跳过纯C，状态为“未运行”，不是“通过”。C16l只完成Tank诊断固件短时自动稳定验证；人工画面、Remote链路、产品帧率和30分钟长稳仍未通过。

### 18.10 C16m～C16o：TSF收口与受控staged-DVP裁决

- **C16m精确TSF相位未通过**：单次矩阵继续收窄SoftAP TSF启动相位。用户确认W20颜色偏暗且仍有秒级异常，W23/W24过亮且仍有秒级异常；精确相位没有关闭撕裂/错位，因此不再把TSF相位搜索作为主修复路线。
- **C16n官方32KB staged-DVP未通过**：切到乐鑫`esp_cam_sensor v2.4.0`扩展DVP控制器及其公开生命周期，实测内部DMA半缓冲约15,360B、每帧约40次搬运。运行出现多次`dvp_ext: invalid state 1`、帧超时和2次槽重叠；用户确认W18虽有颜色且亮度正常，但撕裂/错位比C16m/W24更频繁。该原样参数与生命周期不得重复。
- **C16o受控staged-DVP自动链路成立**：在同一扩展DVP实现上使用102,400B内部DMA、51,200B半缓冲，每个614,400B VYUY帧严格12次半缓冲复制；完成帧后置暂停请求，驱动下一缓冲回调返回空并确认暂停，显示任务完成读取后再恢复采集。运行日志覆盖W2～W18并保持到设备约377秒，累计显示约2,010帧；`pause_req=pause_ack=2010`，`pause_timeout=0`，未见Panic/WDT/重启、`invalid state`、槽重叠、完成冲突、guard/cache/TSF错误。固定图窗口除切换边界外接近稳定，说明该DMA尺寸与暂停握手是后续诊断应保留的有效进展。
- **C16o人工画面未通过**：用户确认动态摄像头窗口在约1/3高度处形成固定分界；上部已相当稳定、接近交付水平，下部持续显示跨帧重叠内容并伴随较低频抖动。照片示例W14/W15与当前240×180预览每60行一次LCD提交的第一分块边界一致，但这只是新的责任边界线索，尚未证明根因属于采集混帧或LCD动态多分块呈现；不得写成根因结论。
- **防回退与验证边界**：后续候选不得无对照地移除C16o的102,400B staged-DVP、逐帧暂停握手、614,400B承载区、终端保护和现有自动计数。C16m精确TSF与C16n原样32KB staged-DVP已经否决，不再重复。C16m～C16o按用户持续授权完全跳过纯C，状态为“未运行”，不是“通过”；当前仍只覆盖Tank诊断链路，人工彩色画面、Remote、产品帧率和30分钟长稳未通过。

### 18.11 C16p：LCD呈现边界矩阵裁决（已完成，未通过）

- **实板矩阵**：保持C16o的102,400B staged-DVP、51,200B半缓冲、逐帧暂停握手、VYUY采集和110%候选亮度不变；同次烧录比较RAW直接转换与86,400B PSRAM整帧RGB画布、30/60/80行LCD提交，以及同一帧重复呈现1/2次。
- **自动稳定证据**：`verification/c16p_r1_full_runtime_tank_com7.log`覆盖W2～W19并保持W19到设备约360秒，最终`display_frame=1832`、`pause_req=pause_ack=1832`、`pause_timeout=0`、`raw_post_err=0`；未出现Panic/WDT/重启、`invalid state`、槽重叠、完成冲突、guard/cache/lifecycle错误。显示前后的RAW轻签名一致，说明显示过程没有改写已完成帧。
- **人工裁决**：W09彩色色块偶有下部撕裂；W10～W19均在约1/3高度处形成稳定叠影边界，边界以下持续重叠并低频抖动，边界以上基本正常，110%亮度基本可接受。异常位置不随直出/画布、30/60/80行分块或重复次数移动，因此这些LCD呈现变量不能关闭故障，不再重复枚举。
- **已验证责任边界**：240×180预览约1/3高度对应显示`y=60`和640×480源图`y=160`，VYUY字节偏移为204,800B；该位置恰为C16o内部102,400B DMA容量的两倍。这里只记录可复核的边界关系，不把尚未通过实板修复的分段组装猜测写成根因或当前设计。
- **验证边界**：C16p按用户持续授权完全跳过纯C，状态为“未运行”，不是“通过”；Tank短时运行稳定不等于人工彩色画面、Remote链路、产品帧率或30分钟长稳通过。

### 18.12 C16q～C16w：staged-DVP固定行覆盖定位与最终显示修复

- **稳定采集边界**：C16q把GDMA半缓冲的51,200B分段复制移入EOF ISR，继续保留102,400B内部DMA、614,400B目标承载区、256B终端保护、逐帧暂停握手和软件索引。人工确认原先约1/3高度以下的大范围重叠消失，只剩一条稳定的3～4显示像素横带；因此上述采集生命周期不得回退。
- **否定路线**：C16r的0～800µs帧尾排空、C16s的描述符直接选源、C16t的0～1000µs异常EOF等待及C16u的异常块VSYNC后组装均未改变窄带。延时、描述符整块选源和VSYNC后补块不再作为该故障的修复变量。
- **原始帧直接证据**：C16v对480条源行取轻量指纹，真实画面窗口持续命中`target y=392`、`source y=472`、`rows=8`，即源帧`y=392..399`与底部`y=472..479`完全一致。两段分别位于40行块9和块11，映射到同一个51,200B物理半缓冲的两次复用；异常在VYUY转RGB及LCD提交前已经存在，责任边界确定为staged-DVP半缓冲复用/组装。
- **已验证显示修复**：240×180最近邻缩放把受损原始行映射到输出`y=147..149`。C16v W31先完成整帧RGB565画布转换，再以相邻正常行插值重建这3条输出行；用户确认W31画面正常且不再有叠影线。该结论只覆盖Tank本地预览和此固定窄带，不推导Remote、产品帧率或长稳通过。
- **最终固化设计**：C16w验证配置保留C16q/C16o已稳定的102,400B staged-DVP、51,200B半缓冲、EOF ISR直接复制、软件索引、逐帧暂停握手、614,400B承载区、256B终端保护、C2M invalidate、20MHz/60行LCD和当时的110%候选亮度；固定修复原始`y=392..399`所对应的RGB输出`y=147..149`，不再逐帧扫描480行。当时的W1～W7窗口只属于历史验证夹具；正式产品已在18.13收敛为100%亮度且不编译、不运行这些窗口。
- **最终验证**：C16w被动日志覆盖W7约226秒并新增1,380帧，最终累计1,875帧；`pause_req=pause_ack=1875`、`rgb_repairs=1875`、`scans=0`，未见重启/Panic/WDT、帧超时或guard/lifecycle/pause/cache/raw-post/overlap/finish-collision/staged错误。用户随后确认“W7画面符合要求，没有异常”。最新重复启动验证中，用户共观察5次启动的W7均正常；附件可解析4次完整启动序列，另1次为本地自动启动日志。4次附件序列均保持`pause_req=pause_ack`且无Panic/WDT，但每次仍有`SPI bus already initialized`和`post_table p0_31=0x00`两条既有启动错误，因此重复验证证明的是W7画面及运行稳定，不扩张为零错误启动。上述固定修复继续作为当前Tank本地彩色显示有效设计。
- **性能与验证边界**：C16v逐帧扫描480行约增加23～31ms，仅作为根因探针，C16w已移除该运行开销。C16q～C16w按用户持续授权完全跳过纯C，状态为“未运行”，不是“通过”。本结论只覆盖Tank本地画面，不推导Remote、产品帧率、控制、音频或30分钟长稳通过。

### 18.13 正式双端产品链路

- **产品配置边界**：TANK与REMOTE正式工程不再提供A0/A1/A2/C15/C16窗口矩阵、高开销全帧CRC、自解码、启动测试图或诊断启动策略的Kconfig/构建入口；相关调用、接口和构建依赖均已移除。TANK直接进入已验证的staged-DVP采集生命周期，摄像头数据只进入正式JPEG/UDP链路。
- **显示所有权**：正式TANK不启动本地摄像头预览，Tank状态任务独占LCD并显示坦克图形、WiFi与电量。网络连通回调只有在本地预览或采集实际运行时才执行预览停止握手；无本地预览时直接启动video_tx，避免状态屏与网络回调并发操作LCD形成启动死锁。
- **视频链路**：TANK使用102,400B staged-DVP、51,200B半缓冲、逐帧暂停/恢复和C2M invalidate取得640×480 VYUY帧，在独立YCbYCr编码缓冲中执行已验证的固定异常行处理，以Q60生成JPEG并按1200B UDP载荷分片；REMOTE只重组和显示完整帧，继续使用三槽最新帧策略、40MHz LCD SPI和60行DMA分块。
- **自动验证证据**：COM7/COM24正式固件全片擦除、三段写入和Hash校验完成。180秒双端运行中，最后40个稳态窗口TANK发送平均`9.95fps`（`9.8～10.1`），REMOTE显示平均`9.85fps`（`9.36～10.17`）；TANK最终`pause_req=pause_ack=1655`、`timeout=0`、`lifecycle_errors=0`，REMOTE `queue_drop=0`，启动形成的`missing=2`未继续增长。双端未见Panic/Guru/abort/assert/WDT或重启。
- **验证边界**：自动日志证明启动、采集、编码、分片、重组和显示任务持续运行，但不能替代Remote真实画面的颜色、固定横线、撕裂和端到端时延目视验收，也未覆盖四向机械控制、松手/失联停车、30分钟长稳或音频。纯C按用户持续授权完全跳过，状态为“未运行”，不是“通过”。
- **最新人工与故障边界**：用户确认正式固件的基本功能已具备、Remote画面“还可以”；这只作为基本可用性初验，不替代完整画质指标。无Tank热点场景则稳定复现主动软件复位，说明18.13的180秒结论只覆盖两端热点均可用的持续连接场景，不能外推为断连/重连通过。
- **实载启动健康自愈门**：仅在Tank正式通道和`video_tx`已经启动后，对真实Q60编码、UDP分片和发送负载连续采样8秒。样本少于40个，或`sync_end_incomplete_events / (complete + incomplete)`大于5%，均判为坏态；坏态把重试次数写入独立NVS命名空间并在1秒后执行完整软件重启，最多5次。正常态把重试次数清零、结束监控并继续长期运行，不设置周期重启。Tank首次连接及运行中重新连接都会幂等启动一次监控；NVS保存失败或达到上限时停止自动重启，避免无界循环。
- **方向与亮度边界**：Remote现有显示方向和触摸契约保持不变，本轮未新增左右镜像；Tank正式视频在固定异常行修复后使用100%亮度增益，不改变Q60、行修复、缩放或颜色格式。
- **首次闭环证据**：同一轮实机中，前三次人眼异常分别对应`45/109`、`33/101`、`40/107`残帧并被自动拒绝；第4次`1/81`被接受，用户确认最终画面正常、方向正确且亮度合适。接受后30秒内Tank统计保持`complete=370, incomplete=1`，Remote显示约`8.89～10.15fps`且`missing`未增长；无Panic/WDT、生命周期、JPEG解码或显示错误。该证据验证了自愈判据与本轮人眼结果对齐，但不是DVP概率坏态根因已消除，也不替代重复冷启动和30分钟长稳。
- **2026-08-27最终闭环与原因**：本轮最终序列从NVS `retry=0`开始，前4次启动均为`complete=73,incomplete=30`并被拒绝，第5次启动在`retry=4`达到`81/0`并接受；用户确认最终画面正常。每轮28～29/30个残帧停在10块，接受态81帧全部完成11块，属于历史同签名的概率性DVP坏启动。各拒绝窗口仅收到1个控制包，30个残帧全部距离控制包超过10ms；把Tank空闲接收等待从20ms恢复到50ms也保持`73/30`，因此摇杆流量和控制任务调度不是原因。底层GDMA/DVP概率时序未被新参数消除，正式解决方案是冻结C16w/Q60并继续使用真实编码/UDP负载下的8秒健康门和最多5次有限完整重启，不用伪造第10块或放宽判据。
- **重连验证**：健康门接受后Remote日志持续收到完整视频帧；用户保持Tank运行并单独重启Remote，Remote自动重连后画面仍正常。这证明本次最终正常画面来自Tank接受态，Remote重启不会重新触发Tank坏态；仍不外推为Tank长期缺席或重连耗尽后的持续重扫通过。
- **诊断固件边界**：当前源码与板上接受态固件保留`[DEBUG-DVP-LIMIT]`健康窗口、EOF统计和控制时间相关计数，用于形成上述否决证据；它不是18.13正式清理版的长期日志设计。移除诊断及其后续重建、擦写与人工复验尚未执行，不得把本轮“功能有效”扩张为“诊断已清理”。
- **正式清理版与最终人工确认**：Tank clean build完成`1445/1445`，应用911,392B、SHA-256 `90FD0C4D636CFF98CD66F0B70826F8CDCF3FFF46020005DB66C3B936A09243F8`；Remote clean build完成`1445/1445`，应用920,480B、SHA-256 `9F95030742CF5C1BADB2D11C10E813F4821E1CC423BD4F9156DE6413D5CF48C7`。COM7/COM24均完成全片擦除、三段写入及Hash校验，COM7按成熟RAM bootloader入口启动。用户随后确认画面正常且移动操作没有问题；方向正确、100%亮度合适。底部约1/3～1/4高度偶发一条3～4像素横向叠影线只作为挂起问题，不改变Q60、C16w采集、固定行修复与健康自愈设计。
- **防回退边界**：C16w的102,400B staged-DVP、51,200B半缓冲、软件索引、逐帧暂停/恢复、C2M invalidate和固定异常行修复全部保持不变。R9启用前VSYNC对齐、R10发送前20帧门控、EOF/VSYNC defer及仅重建DVP均已有否定证据，不得替代当前实载判据。

---

## 修订历史

| 版本 | 日期 | 修订人 | 说明 |
|------|------|--------|------|
| v0.1-draft | 2026-08-14 | AI (Kiro) | 初稿，基于 requirements.md v0.2 展开 17 章设计 |
| v1.0 | 2026-08-14 | AI (Kiro) | 人工评审通过，R5 坦克图形改为 LVGL 内置绘图方案，状态标记为已批准 |
| v1.1 | 2026-08-17 | AI | 同步 Round 10 最终视频流水线、理论/实测预算、默认 TCP 配置和 COM7 临时启动入口 |
| v1.2 | 2026-08-19 | AI | 按当前正式 UDP/8002 基线修正早期 TCP 设计冲突；记录 1200B 应用层分片、完整帧重组、摇杆坐标/越界回中修复及待实板验证边界 |
| v1.2 | 2026-08-18 | AI | 固化摄像头选择、无视频显示、控制保活和 LCD 字形/颜色设计；同步双角色构建、Tank 烧录日志与 COM24 阻塞边界 |
| v1.3 | 2026-08-19 | AI | 同步 Round 1/2 真实摄像头链路、颜色契约、Tank/Remote LCD 时钟、纯 C 18 项、双端擦写和运行日志证据 |
| v1.4 | 2026-08-19 | AI | 同步摄像头调优 Round 4 的 Q30 实现、纯 C RED/GREEN、双端构建擦写和稳态运行证据，标注 Q60/Q30 待确认冲突 |
| v1.5 | 2026-08-20 | AI | 记录摄像头画面专项执行方案；确认 Tank 自检全屏诊断、跳过右上角自检恢复步骤、统一 LCD presenter 的实施边界 |
| v1.6 | 2026-08-20 | AI | 将阶段 A 优化为 A0 并发隔离、A1 Demo 等价、A2 单变量交叉；补入 WDT、preview/TX 所有权、确定性 CRC 和 UDP 边界 |
| v1.7 | 2026-08-20 | AI | 以 A0～C12 证据替换已完成的旧执行方案；停止参数枚举，形成待确认的 C13 同负载显示源二分及 raw/LCD 分支计划 |
| v1.8 | 2026-08-21 | AI | 以 C13 稳定内部图证据关闭 LCD 主分支；固化 C14.1 官方 test-pattern bit7、raw 指纹七窗口裁决与后续带宽/转换分支 |
| v1.9 | 2026-08-21 | AI | 同步 C14.1 实测：固定图 RAW 在 WiFi 开启时直接损坏并在关闭后显著改善，RAW→RGB 主因降权；形成待确认的 C15 官方 200×200 灰度带宽对照 |
| v1.10 | 2026-08-21 | AI | 固化 C15 官方 200×200 Gray8 低带宽诊断实现、七窗口判定规则和回归边界；同步构建/擦写完成、运行回归暂缓 |
| v1.11 | 2026-08-21 | AI | 记录 C15 实际 DMA 写入越过40,000B的稳定性根因，以614,400B承载区和256B终端保护完成W1～W7；低带宽画面裁决暂停 |
| v1.12 | 2026-08-24 | AI | 在 C15 稳定基线上前进到官方 640×480 Gray8；固化 C15.2 的单字节链路、承载边界、自动门禁及人工稳定黑白画面验收边界 |
| v1.13 | 2026-08-24 | AI | 解除阶段停止点；以C16a Gray8生命周期回归、C16b单变量恢复VYUY的两步方案推进正式彩色目标，并固化回归影响矩阵 |
| v1.14 | 2026-08-24 | AI | 记录C16a稳定与C16b彩色恢复证据；针对约2秒一次低概率闪烁/错位，新增重启后丢弃首帧的C16c单变量及增量回归矩阵，亮度保持独立后续变量 |
| v1.15 | 2026-08-24 | AI | 记录C16c丢首帧未改善窗口4固定图离群并否决；恢复C16b帧流程，新增仅启用WiFi EXTRA IRAM优化的C16d单变量与资源回归边界 |
| v1.16 | 2026-08-24 | AI | 记录C16d无改善并撤销；基于W7已关闭WiFi和每30帧日志在复采后输出的时序证据，新增C16e停止态完成诊断输出后再恢复DVP的单变量与回归边界 |
| v1.17 | 2026-08-24 | AI | 记录C16e显著改善非WiFi窗口但W4/W5严重异常、W7仍有极低频闪烁；新增仅使DVP ISR/帧池回调调用链cache-safe的C16f单变量和IRAM资源门禁 |
| v1.18 | 2026-08-24 | AI | 记录C16f cache-safe/IRAM对W4/W5无改善并撤销；新增C16g单次烧录16窗口正交矩阵、逐帧1,024B轻签名、LCD模式和0～100ms帧间空闲组合判据 |
| v1.19 | 2026-08-24 | AI | 记录C16g否定重探针/LCD/0～100ms空闲及W11夹具缺陷；依据DVP任务CPU memcpy和IDF缓存语义新增C16h四模式、固定图重复与同步前后签名矩阵 |
| v1.20 | 2026-08-24 | AI | 记录C16h四缓存模式均无改善且同步前后签名不变；C16系列正式彩色画面验收仍未通过 |
| v1.21 | 2026-08-24 | AI | 补齐C16i任务优先级路线停止、C16j GDMA/Beacon否定证据及C16k/C16l TSF避让自动结果；只记录实板支持的结论，当前W20及候选参数未形成正式产品设计 |
| v1.22 | 2026-08-24 | AI | 记录C16m精确TSF与C16n官方32KB staged-DVP否定证据，以及C16o受控staged-DVP自动稳定进展和动态画面1/3分界人工失败；未把下一轮候选固化为产品设计 |
| v1.23 | 2026-08-24 | AI | 记录C16p显示边界矩阵的自动稳定与人工失败证据，排除直出/画布、30/60/80行分块及同帧重复；仅记录1/3边界换算事实，不固化未验证的分段组装候选 |
| v1.24 | 2026-08-25 | AI | 记录C16q稳定采集边界、C16r～C16u否定路线，以及C16v的`392..399→472..479`原始行证据和W31人工无叠影；最终固定化版本待W7人工确认后再固化 |
| v1.25 | 2026-08-25 | AI | 用户确认C16w W7画面符合要求且无异常；固化固定3条RGB输出行重建、零逐帧扫描及其自动稳定证据，作为当前Tank本地彩色显示有效设计 |
| v1.26 | 2026-08-25 | AI | 收敛正式双端产品配置、自检开关、Tank LCD所有权与Q60视频链路；记录COM7/COM24 180秒自动短稳证据，Remote画面与控制保留人工验收 |
| v1.27 | 2026-08-25 | AI | 依据人工附件纠正重连设计与实现不一致：记录Remote无热点主动复位、重试耗尽不重扫、Tank未事件级直接停车及Remote无WiFi图标的已验证缺口；未固化候选修复 |
| v1.28 | 2026-08-25 | AI | 记录C16w五次启动到W7均正常的重复验证及两条既有非阻断启动错误边界；不把Remote概率异常候选提前固化为设计 |
| v1.29 | 2026-08-25 | AI | 固化十轮实机证据确认的负载内健康验收约束；明确R9对齐与R10启动门控均非有效设计，不固化未验证候选 |
| v1.30 | 2026-08-26 | AI | 固化一次性实载健康自愈门、5次有限重试、重连复查及100%亮度；记录3次坏态拒绝与第4次正常放行的人机对齐证据和重复启动/长稳边界 |
| v1.31 | 2026-08-26 | AI | 记录Remote最新人工画质边界：画面基本正常，底部约1/3～1/4高度偶发3～4像素横向叠影线；按用户指令挂起，不改变当前有效视频设计 |
| v1.32 | 2026-08-26 | AI | 收敛正式产品设计：移除诊断矩阵、历史启动策略、冗余探针和高频日志入口；记录双端清理版构建/擦写及画面、方向、亮度、移动操作人工通过 |
| v1.33 | 2026-08-26 | AI | 固化连续角度/力度V1协议、摇杆径向映射、平滑履带曲线、Tank端PWM配置、启动助推、快速斜坡、换向死区、安全处理、模块边界和纯C验证入口 |
| v1.34 | 2026-08-26 | AI | 同步连续控制代码实现完成和21项LLVM主机纯C全量回归通过；保留新版双角色构建、烧录及四方向机械标定边界 |
| v1.35 | 2026-08-27 | AI | 同步连续控制双角色已构建烧录及首轮DVP健康门主动重启状态；标注空闲不持续发包正式变更与现有STOP心跳设计冲突，具体空闲/一次STOP/失败与重连重试方案待人工确认 |
| v1.36 | 2026-08-27 | AI | 闭合最终实机结果：4次`73/30`坏启动被拒绝，第5次`81/0`接受并由用户确认画面正常；固化空闲STOP一次后静默，否决控制负载原因，记录Remote单独重启重连后画面仍正常 |
| v1.37 | 2026-08-27 | AI | 记录连续控制机械反馈与曲线量化诊断：启动基本通过，转向未通过；限定下一轮仅调整混控节点，具体参数等待人工确认 |
