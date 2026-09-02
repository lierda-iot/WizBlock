# RC Tank Demo P0/P1/P2 验证清单

## 构建状态

### ✅ 固件构建完成
- [x] Tank 固件构建成功
- [x] Remote 固件构建成功
- [x] 固件已备份到 firmware_backup/

### 🔄 固件烧录进行中
- [x] Tank 固件已烧录到 COM7
- [ ] Remote 固件烧录中...

---

## P0: 屏幕撕裂修复验证

### 代码修复点
- [x] rc_video.c video_rx_task 添加初始 LCD 空闲等待
- [x] 每个 chunk 绘制前严格检查 DMA 完成
- [x] 超时检测机制（200ms）
- [x] 建立帧间屏障

### 实机验证步骤
1. **启动 Remote 设备**
   - [ ] 连接 COM7，按复位键
   - [ ] 确认启动日志显示 "Role: REMOTE"
   - [ ] 等待 WiFi 连接建立

2. **启动 Tank 设备**
   - [ ] 连接另一个串口，按复位键
   - [ ] 确认启动日志显示 "Role: TANK"
   - [ ] 确认 "WiFi SoftAP started"

3. **观察视频显示**
   - [ ] Remote 屏幕是否显示摄像头画面
   - [ ] **关键：快速移动摄像头或改变场景**
   - [ ] 是否出现色块、条纹、撕裂现象
   - [ ] 画面是否连贯平滑

### 预期结果
✅ **成功标志**：画面连贯，无色块撕裂，快速运动场景下无异常  
❌ **失败标志**：出现色块、横向撕裂、画面错位

---

## P1: 摇杆修复验证

### 代码修复点
- [x] rc_joystick.c 交换 LEFT/RIGHT 方向映射
- [x] rc_joystick.h 修改摇杆位置和尺寸
  - BASE_CX: 72 → 60 (左移)
  - BASE_R: 40 → 50 (直径 100px)
  - KNOB_R: 20 → 25
  - MAX_TRAVEL: 40 → 50
- [x] 添加触摸边界验证

### 实机验证步骤
1. **视觉检查**
   - [ ] 摇杆圆圈是否显示在屏幕左侧
   - [ ] 摇杆尺寸是否增大（直径约 100px）
   - [ ] 触摸摇杆外围是否无响应

2. **方向验证**
   - [ ] 向左推摇杆 → Tank **左转** ✅
   - [ ] 向右推摇杆 → Tank **右转** ✅
   - [ ] 向上推摇杆 → Tank 前进
   - [ ] 向下推摇杆 → Tank 后退

3. **边界验证**
   - [ ] 触摸摇杆圆圈内部 → 响应正常
   - [ ] 触摸摇杆圆圈外部 → 无响应

### 预期结果
✅ **成功标志**：方向正确，位置左侧，尺寸合适，边界清晰  
❌ **失败标志**：方向仍然相反，位置错误，越界触发

---

## P1: Tank 屏幕图标修复验证

### 代码修复点
- [x] rc_tank_screen.c 添加 RGB565_SWAP 宏
- [x] 字体位序从 LSB 改为 MSB
- [x] 所有颜色定义应用字节序交换

### 实机验证步骤
1. **启动 Tank 设备后观察屏幕**
   - [ ] 左上角 WiFi 图标是否清晰（不乱码）
   - [ ] 电量百分比数字是否可读
   - [ ] 其他文本/图标是否正常

### 预期结果
✅ **成功标志**：WiFi 图标清晰，数字可读，无乱码  
❌ **失败标志**：图标乱码，数字镜像或错位

---

## P2: 帧率优化验证（当前方案：UDP + JPEG）

### 代码修复点
- [x] rc_net.c 改为 UDP 传输
- [x] Tank 端：UDP bind 8002 + sendto
- [x] Remote 端：UDP bind 8002 + recvfrom
- [x] 帧头：magic + seq + length
- [x] RLE 差分编码器已实现但**未集成**

### 实机验证步骤
1. **启动后观察串口日志**
   - [ ] Tank 端显示 "Video TX frame: XXXms"
   - [ ] Remote 端显示 "Video RX decode: XXXms, display: XXXms"
   - [ ] 记录实测帧率和延迟

2. **性能记录**
   ```
   Tank 编码时间: _____ ms
   Remote 解码时间: _____ ms
   Remote 显示时间: _____ ms
   端到端延迟: _____ ms
   实测帧率: _____ fps
   ```

3. **UDP 稳定性**
   - [ ] 视频流是否连续
   - [ ] 是否有明显丢帧卡顿
   - [ ] 长时间运行是否稳定

### 预期结果（UDP + JPEG）
✅ **成功标志**：帧率 6-8fps，延迟 <150ms，UDP 无阻塞  
⚠️ **10fps 目标**：需要集成 RLE 替换 JPEG（可选步骤）

---

## 日志抓取命令

### Tank 设备日志
```bash
cd e:/10__AIProject/7_AI陪伴机器人/CODE/examples/rc_tank_demo
python monitor_log.py 60 > logs/tank_$(date +%Y%m%d_%H%M%S).log
```

### Remote 设备日志
```bash
cd e:/10__AIProject/7_AI陪伴机器人/CODE/examples/rc_tank_demo
python monitor_log.py 60 > logs/remote_$(date +%Y%m%d_%H%M%S).log
```

### IDF Monitor（可解码 backtrace）
```bash
D:/Espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe \
  D:/Espressif/frameworks/esp-idf-v5.5.4/tools/idf_monitor.py \
  --port COM7 --baud 115200 \
  --elf build/rc_tank_demo.elf
```

---

## 问题记录表

| 问题 | 现象 | 是否复现 | 优先级 |
|------|------|----------|--------|
| P0 屏幕撕裂 | | [ ] | P0 |
| P1 摇杆方向反 | | [ ] | P1 |
| P1 摇杆位置/尺寸 | | [ ] | P1 |
| P1 Tank 图标乱码 | | [ ] | P1 |
| P2 帧率 <10fps | | [ ] | P2 |

---

## 迭代决策点

### 如果 P0/P1 全部通过
- [x] 标记本轮成功
- [ ] 评估是否需要集成 RLE 达到 10fps
- [ ] 用户确认是否满意

### 如果发现新问题
- [ ] 记录详细现象和日志
- [ ] 分析根因
- [ ] 修改代码
- [ ] 重新构建和烧录（最多 10 轮）

---

生成时间：2026-08-18
当前轮次：第 1 轮
