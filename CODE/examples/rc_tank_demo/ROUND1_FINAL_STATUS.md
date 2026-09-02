# RC Tank Demo 第 1 轮调试最终状态

## 时间
2026-08-18 13:30

## 问题根因确认

### Remote 触摸屏初始化失败
- **现象**：Remote 固件启动后触摸屏初始化失败，摇杆无法工作，设备 5 秒重启循环
- **错误原因**：我错误地修改了 `components/touch_hal/touch_hal.c` 的初始化逻辑
  - **原始正确逻辑**：先 probe I2C 设备 → 再执行 TP_RST 复位
  - **我的错误修改**：先执行 TP_RST 复位 → 再 probe I2C 设备
- **根因**：CST836U 触摸屏上电后已处于工作状态可响应 I2C，原始代码先 probe 确认设备存在，再软复位初始化。我的修改破坏了这个逻辑。
- **解决方案**：已使用 `git checkout` 恢复 touch_hal.c 到原始版本

### 关键事实
1. ✅ 历史版本（round10）触摸屏在 0x15 成功初始化
2. ✅ 硬件未修改
3. ✅ 原始代码逻辑正确
4. ❌ 我的 3 轮修改（调整时序）都是错误方向

## 已完成的正确修复

### P0: 屏幕撕裂
- **文件**：`CODE/examples/rc_tank_demo/main/rc_video.c`
- **修改**：video_rx_task 添加严格 DMA 完成检查和帧间屏障
- **状态**：✅ 代码修改完成，待实机验证

### P1: 摇杆方向反转
- **文件**：`CODE/examples/rc_tank_demo/main/rc_joystick.c`
- **修改**：交换 LEFT/RIGHT 映射，dx<0→RIGHT, dx>0→LEFT
- **状态**：✅ 代码修改完成，待实机验证

### P1: 摇杆位置和尺寸
- **文件**：`CODE/examples/rc_tank_demo/main/rc_joystick.h`
- **修改**：BASE_CX 72→60（左移），BASE_R 40→50（直径 100px），KNOB_R 20→25，MAX_TRAVEL 40→50
- **状态**：✅ 代码修改完成，待实机验证

### P1: 摇杆边界限制
- **文件**：`CODE/examples/rc_tank_demo/main/rc_joystick.c`
- **修改**：添加距离验证，拒绝超出 BASE_R+10 的触摸
- **状态**：✅ 代码修改完成，待实机验证

### P1: Tank 屏幕图标乱码
- **文件**：`CODE/examples/rc_tank_demo/main/rc_tank_screen.c`
- **修改**：修正字体位序（LSB→MSB）和 RGB565 字节序（添加 RGB565_SWAP 宏）
- **状态**：✅ 代码修改完成，待实机验证

### P2: UDP 视频传输
- **文件**：`CODE/examples/rc_tank_demo/main/rc_net.c`
- **修改**：替换 TCP 为 UDP，8 字节帧头（magic + seq + length）
- **状态**：✅ 代码修改完成，待实机验证

### P2: RLE 差分编码（可选优化）
- **文件**：`CODE/examples/rc_tank_demo/main/rc_rle.c` / `rc_rle.h`
- **状态**：✅ 已实现并通过纯 C 测试，**但未集成到视频管道**
- **说明**：当前固件使用 UDP+JPEG，理论帧率 6-8fps。若要达到 10fps 目标，需集成 RLE 替换 JPEG

## 待办事项

### 立即任务
1. **重新构建 Remote 固件**（使用恢复后的 touch_hal.c）
   - 使用成熟构建方法：`mirror_and_build_remote.ps1`
   - 清除 MSYSTEM 环境变量避免 ESP-IDF 检测拦截
   
2. **烧录并验证 Remote 触摸屏**
   - 烧录 Remote 固件到 COM7
   - 确认触摸屏初始化成功
   - 确认摇杆可以正常工作

3. **烧录 Tank 固件**
   - 使用已构建的 Tank 固件（12:03 版本）
   - 烧录到 COM7

4. **验证 P0/P1/P2 修复**
   - 按照 `VERIFICATION_CHECKLIST.md` 执行验证
   - 记录验证结果

### 后续优化（如果帧率不达标）
- 集成 RLE 差分编码替换 JPEG
- 目标：10fps @ 320×240

## Tank 固件状态
- ✅ 已构建（时间戳 12:03）
- ✅ 已烧录并验证启动成功
- ✅ 所有模块正常：电机、显示、摄像头、音频、WiFi SoftAP

## Remote 固件状态
- ❌ 当前固件（13:24）包含错误的 touch_hal.c 修改，无法使用
- ⏳ 需要使用原始 touch_hal.c 重新构建

## 构建方法记录

### 成熟构建方法（from design.md 4.1.1）
```bash
cd "e:/10__AIProject/7_AI陪伴机器人/CODE/examples/rc_tank_demo"
powershell.exe -ExecutionPolicy Bypass -Command "
  Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue;
  Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue;
  Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue;
  & '.\mirror_and_build_remote.ps1'
"
```

### 烧录方法
```bash
cd "e:/10__AIProject/7_AI陪伴机器人/CODE/examples/rc_tank_demo"
powershell.exe -ExecutionPolicy Bypass -File flash_rc_tank.ps1 -Port COM7 -Role REMOTE
```

## 经验教训

1. **不要凭直觉修改已验证成功的代码**
   - 历史日志显示原始代码工作正常
   - 应该先对比差异，理解原始逻辑

2. **时序问题不一定是延迟不够**
   - 原始代码的顺序才是关键
   - "先 probe 再复位" vs "先复位再 probe" 是逻辑问题，不是时序问题

3. **使用 git diff 快速定位问题**
   - `git diff HEAD -- <file>` 可以立即看到所有修改
   - 对比历史成功版本是定位问题的最快方法

4. **构建系统环境变量很重要**
   - Git Bash 的 MSYSTEM 变量会泄漏到子进程
   - ESP-IDF 检测到 MSys 环境会直接退出
   - 必须清除相关环境变量

## 当前轮次
第 1 轮（授权最多 10 轮）

## 下一步
等待用户指令继续构建和验证
