<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Audio OFDM Text Demo

两块本项目 ESP32-S3 板通过板载扬声器和麦克风以半双工 OFDM 声学链路传输 UTF-8 文字。本 Demo 独立复刻参考视频可观察到的功能和处理链，不与视频作者的私有波形或 ProjectUltra 协议互通。

## 功能

- 内置 UTF-8 文字按 112 bytes 分帧，最大 1024 bytes
- 每帧使用 7 个 `RS(32,16)` 码字、列交织、15-bit LFSR 加扰和 CRC 门禁
- BPSK 帧头、QPSK 载荷、SC/LTS 同步、导频公共相位跟踪和单抽头均衡
- 严格半双工：发送期间不解调本机回声，监听期间关闭功放
- 320x240 触摸 UI，显示收发状态、分帧进度、完整接收正文和清除按钮
- UART 单字节命令可触发普通发送及双板增益/频段校准
- 固定低噪声日志前缀，支持长时监听和错误定位

## 当前正常 Profile

当前参数来自 2026-08-03 在约 30cm、正对、安静桌面环境下的两轮配对校准，并已由主机测试锁定：

| 参数 | 当前值 |
| --- | --- |
| PCM | 48kHz / 16-bit / mono |
| FFT / CP | 256 / 256 samples |
| 有效正频率 bins | `32..59`（6.0kHz～11.0625kHz） |
| Pilots | `34/42/50/58` |
| Data carriers | 24 |
| Header / payload symbols | 22 / 38 |
| 单物理帧 | 34208 samples，约 712.7ms |
| ES8311 volume | 90% |
| ES7210 MIC1 gain | 30.0dB |
| PCM scale | 24000 |

该 profile 已通过主机 DSP 回归和 macOS clean build；尚未在两块实板上重新烧录和测量首包成功率，不将校准结果表述为空气声道验收通过。

## 界面语义

- 上电和空闲时只显示等待接收，不预先显示内置测试文字。
- `TX_DONE` 只表示本机 PCM 已全部送出并排空，不表示对端已收到。
- 只有完整消息的分帧、CRC 和 UTF-8 全部通过后才显示正文。
- “清除”只隐藏当前正文和成功样式，不停止监听；下一条合法完整消息会自动重新显示。

## 主机测试

在 `CODE` 目录执行：

```bash
bash examples/audio_ofdm_text_demo/tests/run_host_tests.sh
```

当前七组测试覆盖 CRC、UTF-8、分帧/重组、FEC、PHY、同步和校准协议。其中 PHY 回归显式锁定当前有效 bin、导频、数据载波数和校准选中档位。

## 构建与烧录

macOS 使用项目已验证入口：

```bash
# clean build
bash ./tools/build_example_macos.sh audio_ofdm_text_demo

# fullclean build + 全片擦除 + 烧录
bash ./tools/build_example_macos.sh audio_ofdm_text_demo flash -p /dev/cu.usbserial-1130
```

Windows 构建使用项目统一 Example 脚本：

```powershell
.\tools\build_example.ps1 -Example audio_ofdm_text_demo -Clean
```

Windows 烧录必须按项目根目录 `design.md` 4.1.2 的成熟入口先单独执行全片擦除，再调用带端口的 Example 脚本；macOS 以 4.1.3 为准。

## 两板操作

当前样机角色为：

- `1130`：带扬声器，可发送和接收。
- `1140`：无扬声器，只用作接收端。

UART0 命令为单字节，不要附加换行：

| 命令 | 作用 |
| --- | --- |
| `s` / `S` | 发送内置普通文字 |
| `r` / `R` | 接收端进入校准待命 |
| `t` / `T` | 发送端启动完整校准序列 |
| `x` / `X` | 取消校准并恢复普通监听 |

配对校准顺序：先在 RX 输入 `r`，等待 `OFDM_CAL role=RX stage=READY`，再在 TX 输入 `t`。校准结果只在 RAM 中统计并输出建议，不自动写 NVS 或改动普通 profile。

## 日志

主要前缀：

- `OFDM_BOOT`：启动参数与模块初始化
- `OFDM_TX`：发送命令、分帧进度与 PCM 结果
- `OFDM_SYNC`：前导、LTS 和同步候选
- `OFDM_RX`：分帧 CRC/FEC 与完整消息结果
- `OFDM_DROP`：定位失败阶段和原因
- `OFDM_CAL`：校准命令、样本和唯一建议
- `OFDM_HEALTH`：每 10 秒输出 RX 丢块、TX 下溢、DSP 最长时间和剩余内存

macOS 直连 CH340E 的当前成熟监听入口记录在项目 `design.md` 4.1.3，应使用 ESP-IDF venv Python 的绝对路径。

## 目录

- `main/ofdm_frame.*`：帧头、分片和重组
- `main/ofdm_fec.*`：RS、交织和加扰
- `main/ofdm_phy.*`：OFDM 映射、训练、导频和均衡
- `main/ofdm_sync.*`：流式前导检测和帧边界
- `main/ofdm_audio.*`：Demo 私有 48kHz codec/I2S 链路
- `main/ofdm_link.*`：半双工状态机和校准运行时
- `main/ofdm_ui.*`：LVGL 界面与触摸命令
- `tests/`：主机单元和 DSP 回归
- `components/`：Demo 私有 libcorrect 和 KissFFT
- `fonts/`：Noto Sans SC 的 OFL 许可与可复现生成器

## 已知边界

- 首版无 ACK、自动重传、信道侦听和碰撞退避。
- CFO 仍输出 `not_measured`，尚未完成正负频偏实板验证。
- 不承诺超声、不可听、固定距离、固定吞吐率或固定首包成功率。
- 当前新 profile 还需要两板普通文字、337-byte 四帧和至少 30 分钟重复收发回归。

## 第三方资产

- `quiet/libcorrect`：BSD-3-Clause
- `mborgerding/kissfft`：BSD-3-Clause
- Noto Sans SC：SIL Open Font License 1.1

完整版本、固定提交、复制边界和许可文本见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
