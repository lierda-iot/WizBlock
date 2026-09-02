<!-- OPEN-LANGUAGE-LINKS:START -->
[中文](README.md) | [English](README.en.md)
<!-- OPEN-LANGUAGE-LINKS:END -->

# Codex Task Notifier Demo

把 ESP32-S3 + 320x240 LCD 作为只读 Codex 多任务状态终端。电脑端 Bridge 支持
macOS 和原生 Windows，可同时跟踪当前用户的 Codex Desktop 与 VS Code 任务。
Codex 不需要位于前台，Bridge 不控制 Codex，也不使用键盘、鼠标或剪贴板。

## 功能边界

- Bridge 只读 `~/.codex/sessions/**/*.jsonl`；每轮只读第一条用户输入生成任务短标题，不读取回复正文、工具参数和剪贴板。
- 任务键为 `(session_id, turn_id)`；屏幕上的 `Desktop`、`VSCode` 和未知 `Codex` 来源可并存。
- 状态为 `RUN`、`DONE`、`STOP`、`UNKNOWN`；`task_complete` 表示本轮结束，不代表业务执行成功。
- 屏幕每页显示 2x2 共 4 张任务卡片，最多显示 12 条；5～12 条每 4 秒换页，超过 12 条显示 `+N`。
- 运行至少 30 秒的完成事件才提示；2 秒内多个完成事件合并为一次声音和动画。
- Bridge 只提供 `GET /api/v1/state`，请求必须携带共享 Token。
- 设备一次只连接一台 Bridge 主机，不会自动发现或合并多台电脑。

## 共通准备

1. 电脑安装 Python 3.11 或更高版本；Bridge 只使用 Python 标准库，安装脚本不联网、不下载依赖。
2. ESP32 和运行 Bridge 的电脑必须在同一个局域网，且客户端隔离不能阻断两者互访。
3. 先安装 Bridge，再把该电脑的局域网 IPv4、端口和 Token 写入固件本地配置。
4. `bridge/.env.local` 和 `main/notifier_secrets.h` 已被 Demo 内 `.gitignore` 排除，不得提交或对外分享。

## macOS 操作

macOS 默认监控当前用户的 `$HOME/.codex/sessions`，用户级运行数据位于
`$HOME/Library/Application Support/CodexTaskNotifierDemo`。

### 1. 开发测试

```bash
cd CODE/examples/codex_task_notifier_demo
python3 --version
PYTHONPATH=bridge/src python3 -m unittest discover -s bridge/tests -v
```

### 2. 安装并启动 Bridge

```bash
cd CODE/examples/codex_task_notifier_demo
sh bridge/scripts/install.sh
```

首次安装会在 `bridge/.env.local` 生成随机 64 位十六进制 Token，创建标准库
Python venv，并安装用户级 `com.codex-task-notifier.bridge` LaunchAgent。用户登录后
Bridge 自动启动，异常退出后由 `launchd` 重启。

### 3. 查看状态和日志

```bash
launchctl print "gui/$(id -u)/com.codex-task-notifier.bridge"
tail -n 80 "$HOME/Library/Application Support/CodexTaskNotifierDemo/bridge.err.log"
```

查看当前 Bridge Token：

```bash
sed -n 's/^CODEX_NOTIFIER_TOKEN=//p' bridge/.env.local
```

### 4. 查看 macOS 局域网 IPv4

Wi-Fi 常见接口为 `en0`：

```bash
ipconfig getifaddr en0
```

如果没有输出，先用 `networksetup -listallhardwareports` 查看 Wi-Fi 对应的接口，
再将 `en0` 替换为实际接口。这个 IPv4 将写入 `NOTIFIER_BRIDGE_HOST`。

### 5. 卸载 Bridge

```bash
cd CODE/examples/codex_task_notifier_demo
sh bridge/scripts/uninstall.sh
```

卸载只移除 LaunchAgent，保留运行时、状态、Token 和日志供排障。

## Windows 操作

首版支持 Windows 10/11 原生环境，不使用 WSL 运行 Bridge。默认监控
`%USERPROFILE%\.codex\sessions`，运行数据位于
`%LOCALAPPDATA%\CodexTaskNotifierDemo`。原生 Windows 和 WSL 的会话目录不会自动合并；
`-SessionsDir` 只能指定一个 Windows 可访问的单一目录。

### 1. 开发测试

打开普通 PowerShell（不要以管理员身份运行）：

```powershell
Set-Location -LiteralPath "C:\path\to\CODE\examples\codex_task_notifier_demo"
py -3 --version
$env:PYTHONPATH = (Resolve-Path -LiteralPath ".\bridge\src").Path
py -3 -m unittest discover -s ".\bridge\tests" -v
Remove-Item Env:PYTHONPATH
```

`py -3 --version` 必须是 3.11 或更高版本。如果没有 `py` 启动器，可将上述
`py -3` 替换为已加入 `PATH` 的 `python`。

### 2. 以当前用户安装并启动 Bridge

在普通 PowerShell 中执行：

```powershell
Set-Location -LiteralPath "C:\path\to\CODE\examples\codex_task_notifier_demo"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\bridge\scripts\install.ps1"
```

安装脚本会：

- 生成或复用 `bridge/.env.local` 中的 Token；
- 只复制本 Demo Bridge 运行文件到 `%LOCALAPPDATA%\CodexTaskNotifierDemo`；
- 创建 Python 3.11+ venv，不联网安装依赖；
- 注册当前用户、`Interactive`、`Limited` 权限的 `CodexTaskNotifierBridge` 计划任务；
- 用户登录时隐藏启动，异常退出后按 1 分钟间隔重启；
- 启动后执行一次本机鉴权 API 检查，但不修改 Windows 防火墙。

指定另一个单一会话目录时：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\bridge\scripts\install.ps1" `
    -SessionsDir "D:\CodexData\sessions"
```

### 3. 查看状态和日志

在普通 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\bridge\scripts\status.ps1"
```

正常时至少应看到计划任务为 `Running` 或 `Ready`，且
`Local authenticated API: OK`。该脚本不输出 Token 和任务标题，只显示任务状态、
本机 API 可达性和错误日志尾部。

查看当前 Bridge Token：

```powershell
Get-Content -LiteralPath ".\bridge\.env.local"
```

### 4. 查看 Windows 局域网 IPv4 和网络类型

```powershell
Get-NetIPConfiguration |
    Where-Object { $null -ne $_.IPv4DefaultGateway } |
    Select-Object InterfaceAlias, @{Name="IPv4"; Expression={$_.IPv4Address.IPAddress}}
Get-NetConnectionProfile | Select-Object Name, InterfaceAlias, NetworkCategory
```

选择与 ESP32 处于同一局域网的 IPv4 写入 `NOTIFIER_BRIDGE_HOST`。当前可信局域网
必须是 `Private`；如果显示 `Public`，先在 Windows 系统设置中确认并改为专用网络。
本 Demo 不会为 `Public` 网络开放端口。

### 5. 以管理员身份添加防火墙规则

右键打开“以管理员身份运行”的 PowerShell，再执行：

```powershell
Set-Location -LiteralPath "C:\path\to\CODE\examples\codex_task_notifier_demo"
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File ".\bridge\scripts\configure-firewall.ps1" -Action Add
```

规则只允许 `Private` 网络的 `LocalSubnet` 访问 TCP `8765`，并限定到 Bridge venv 中的
`python.exe`。防火墙只是第一层限制，HTTP 仍必须通过 Token 鉴权。

如果提权时使用的是另一个管理员账号，必须把安装脚本输出的 `Runtime data`
绝对路径传给 `-InstallDir`，否则脚本会查找错误用户的 `%LOCALAPPDATA%`。

### 6. 卸载 Bridge

先在普通 PowerShell 中移除计划任务：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\bridge\scripts\uninstall.ps1"
```

再在管理员 PowerShell 中删除防火墙规则：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File ".\bridge\scripts\configure-firewall.ps1" -Action Remove
```

卸载默认保留 `%LOCALAPPDATA%\CodexTaskNotifierDemo` 中的运行时、状态、Token 和日志。
如果安装时修改过 `-Port`、`-InstallDir` 或 `-TaskName`，状态、防火墙和卸载操作也要传入
相同参数。

## 固件本地配置

安装当前要使用的 Bridge 后，从 `main/notifier_secrets.example.h` 创建
`main/notifier_secrets.h`。

macOS：

```bash
cp main/notifier_secrets.example.h main/notifier_secrets.h
```

Windows PowerShell：

```powershell
Copy-Item -LiteralPath ".\main\notifier_secrets.example.h" `
    -Destination ".\main\notifier_secrets.h"
```

然后填写：

- `NOTIFIER_BRIDGE_HOST`：当前 Bridge 电脑在设备局域网中的 IPv4；
- `NOTIFIER_BRIDGE_PORT`：默认为 `8765`，必须与安装参数一致；
- `NOTIFIER_BRIDGE_TOKEN`：必须与当前电脑 `bridge/.env.local` 中的值完全一致。

从 macOS 切换到 Windows，或切换到另一台电脑时，主机 IPv4 和 Token 通常都会变化。
必须更新 `notifier_secrets.h` 并重新构建、烧录固件；只启动新 Bridge 不会让设备
自动切换主机。

## 固件构建与烧录

更改 `notifier_secrets.h` 后必须重新构建和烧录。实板回归前必须先全片擦除；
下方只使用 `design.md` 4.1.2/4.1.3 已记录的成熟入口。

### macOS

```bash
cd CODE
bash ./tools/build_example_macos.sh codex_task_notifier_demo
bash ./tools/build_example_macos.sh codex_task_notifier_demo flash -p /dev/cu.usbserial-1130
```

串口监听：

```bash
python -m serial.tools.miniterm /dev/cu.usbserial-1130 115200 --raw --dtr 0 --rts 0
```

### Windows

Windows 必须在 Git Bash 中先完整重建中文路径的临时镜像，再调用 PowerShell 构建。
当前成熟记录的源码路径为 `e:/10__AIProject/7_AI陪伴机器人/CODE`，串口为 `COM7`；
如实际环境已变更，只替换这两个环境值，不得省略三步流程。

```bash
# 步骤 1：全量镜像（必须先删除旧镜像再完整复制，不可省略）
rm -rf "$TEMP/laiwfs300_build/CODE" && cp -r "e:/10__AIProject/7_AI陪伴机器人/CODE" "$TEMP/laiwfs300_build/CODE"

# 步骤 2：Clean 构建
powershell.exe -ExecutionPolicy Bypass -File "e:/10__AIProject/7_AI陪伴机器人/CODE/tools/build_example.ps1" -Example codex_task_notifier_demo -Clean 2>&1 | tail -30

# 步骤 3：全片擦除 + 烧录
powershell.exe -ExecutionPolicy Bypass -Command "Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue; Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue; & 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' -m esptool --chip esp32s3 -p COM7 erase_flash" 2>&1 | tail -5
powershell.exe -ExecutionPolicy Bypass -File "e:/10__AIProject/7_AI陪伴机器人/CODE/tools/build_example.ps1" -Example codex_task_notifier_demo -Port COM7 2>&1 | tail -30
```

`build_example.ps1 -Clean` 不会自动重新镜像源码，所以每次修改源文件后都必须从步骤 1 开始。

## 设备 Wi-Fi 配置

- NVS 中没有合法配置时，上电直接进入 `Wi-Fi 设置` 页面并自动扫描当前可用网络，不发起空配置连接。
- SSID 从下拉菜单选择；点击刷新按钮可重新扫描，页面会显示扫描中、未发现网络或扫描失败状态。
- 密码为空表示开放网络，非空时必须为 8～63 个可见 ASCII 字符。
- 从已保存网络切换到其他网络时会清空旧密码，避免误用原网络凭据。
- 点击 `保存并连接` 后先持久化配置，再切回任务页并立即重连。
- 任务页右上角设置按钮可随时重新进入配置页；普通复位会优先使用上次保存的配置。
- 全片擦除会同时清除保存的 Wi-Fi 配置，下一次启动会重新进入设置页。

## 常见排查

- Windows `status.ps1` 显示 API 正常，但设备显示 `OFFLINE`：检查 Windows 网络是否为 `Private`、防火墙规则是否已添加、主机 IPv4 是否变化，以及 Token 是否一致。
- macOS Bridge 本机正常但设备无法连接：检查 macOS 防火墙是否已允许当前 Python 接收局域网入站连接。
- Bridge 运行但不出现任务：确认 Bridge 与 Codex 使用同一个登录用户，且监控的会话目录中实际存在 `*.jsonl`。
- 设备当前不会同时连接 macOS 和 Windows；切换电脑时按“固件本地配置”重新烧录。

## 中文字体

固件自带由官方 Noto Sans SC 转换的 16px、2bpp 静态字库，覆盖 ASCII、CJK 标点、
CJK 基本区和常用全角符号。固件直接嵌入 `main/notifier_noto_sans_sc_16.bin`，正常
构建不需要安装字体工具；完整 SIL Open Font License 1.1 位于 `fonts/OFL.txt`。

重新生成字库需要 Python 3 和 Pillow，并在 `fonts/.source/` 放置官方
`NotoSansSC-wght-cdn.ttf`：

```bash
cd CODE
python3 examples/codex_task_notifier_demo/fonts/generate_notifier_font.py
bash examples/codex_task_notifier_demo/tests/run_host_tests.sh
```

## 日志判据

- Bridge：`[observer]`、`[task]`、`[api]`、`[health]`。
- 固件：`[bridge]`、`[wifi]`、`[state]`、`[event]`、`[alert]`、`[health]`。
- 正常轮询不逐秒打印；状态变化即时打印，健康统计每 30 秒打印一次。
- 连续 3 次请求失败进入 `OFFLINE`；恢复后的第一份完整合法响应立即回到在线。

完整协议、数据边界、UI 布局、保留期和验收口径见项目根目录 `design.md` 11.26 与
`requirements.md` 9.7。
