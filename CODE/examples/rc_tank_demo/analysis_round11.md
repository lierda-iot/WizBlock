# RC Tank Demo Round 11 问题分析

**分析时间**: 2026-08-18  
**Round 10 实测结果**: Tank 采集 25.26fps、发送 5.98fps (165ms/帧)；Remote 显示 6.04fps (165.87ms/帧)  
**用户反馈问题**:
1. 摇杆左右方向反了，且超出控件范围也能触发；需求澄清：移到左侧、加大尺寸、限制生效范围
2. 屏幕撕裂严重（图1：严重色块错乱）
3. 坦克左上角图标乱码（图2：WiFi 方块和电量显示异常）
4. 6fps 远低于需求，要求从理论速率和时延角度系统分析瓶颈

---

## 1. 摇杆控制问题分析

### 1.1 左右反向根因

**当前代码逻辑** ([rc_joystick.c:11-31](main/rc_joystick.c)):
```c
uint8_t rc_joystick_dir_from_offset(int dx, int dy, int deadzone) {
    // X 主导: dx < 0 (左) = TURN_LEFT, dx > 0 (右) = TURN_RIGHT
    return (dx < 0) ? RC_CMD_TURN_LEFT : RC_CMD_TURN_RIGHT;
}
```

**触摸到屏幕坐标映射** ([rc_joystick.c:33-48](main/rc_joystick.c)):
```c
int mapped_x = (RC_JOY_SCREEN_W - 1) - raw_y;  // 319 - raw_y
int mapped_y = raw_x;
```
- 竖屏面板 (240×320) 逆时针旋转 90° → 横屏 (320×240)
- `raw_y=0` (面板顶部) → `screen_x=319` (横屏右侧)
- `raw_y=319` (面板底部) → `screen_x=0` (横屏左侧)

**问题点**:
- 摇杆底座在 `(72, 168)`，左下角位置
- 用户**向右推**（`screen_x` 增大）→ `dx > 0` → `TURN_RIGHT` ✅ 正确
- 用户**向左推**（`screen_x` 减小）→ `dx < 0` → `TURN_LEFT` ✅ 逻辑正确

**反向根因推断**:
- **可能是电机接线物理反向**：代码 `TURN_LEFT` 驱动了右转电机组合
- **或者用户预期与实际定义相反**：例如用户认为"摇杆向左"应该让坦克"车头向左转"，但代码实现的是"原地左转"（左履带后退、右履带前进）

**验证方法**: 检查 `robot_motion_turn_left()` 在 [board_laiwfs300.c](../../components/laiwfs300/board_motor.c) 中的 GPIO 输出是否与硬件接线一致。

### 1.2 超出控件触发问题

**当前触摸处理** ([rc_control.c:179-206](main/rc_control.c)):
- 任意触摸点都会映射到屏幕坐标
- 计算相对摇杆底座 `(72, 168)` 的偏移 `dx, dy`
- **没有边界检查**：即使触摸点在屏幕右上角 `(319, 0)`，也会计算为 `dx=247, dy=-168`，超过 `MAX_TRAVEL=40` 后被 clamp 到边界

**问题**: 用户在摇杆区域外触摸，仍会触发方向指令。

**修复方向**:
1. 触摸前先判断 `(screen_x, screen_y)` 是否在底座圆形范围内（`dist² ≤ (BASE_R + margin)²`）
2. 超出范围时忽略触摸，保持 `STOP` 状态

### 1.3 需求澄清要点

**新需求**:
- 摇杆移到**左侧**（当前 `BASE_CX=72` 已经在左侧，可能需要更靠边）
- **加大尺寸**（当前底座直径 80px，可调整到 100px 或更大）
- **限制生效范围**（只在摇杆可见区域内响应，超出自动回正）

**建议参数调整**:
- `RC_JOY_BASE_CX`: 从 72 改为 60（更靠左）
- `RC_JOY_BASE_R`: 从 40 改为 50（直径 100px）
- `RC_JOY_KNOB_R`: 从 20 改为 25（比例保持）
- `RC_JOY_MAX_TRAVEL`: 从 40 改为 50
- 新增触摸有效半径检查：`BASE_R + 10` (容错边缘)

---

## 2. 屏幕撕裂问题分析

### 2.1 问题现象

**图1 表现**: 严重色块、横向错位、类似 DMA 传输中断或帧缓冲指针错乱。

**Round 10 显示链路** ([rc_video.c:463-625](main/rc_video.c)):
```c
// 60 行 DMA 分块
#define RC_REMOTE_LCD_CHUNK_LINES 60U
// 20MHz SPI 时钟
#define RC_REMOTE_LCD_PIXEL_CLOCK_HZ 20000000U

for (uint32_t y = 0; y < 240; y += 60) {
    // 1. 从 PSRAM 解码帧复制 60 行到 DMA-capable 内部缓冲
    memcpy(display_chunk_buf + row * 320, rgb_buf + (y + row) * 320, ...);
    // 2. 等待上一块完成
    while (display_hal_wait_pending(0) == ESP_OK) {}
    // 3. 提交当前块
    display_hal_draw_bitmap_rgb565(0, y, 320, 60, display_chunk_buf);
    // 4. 等待当前块完成（200ms 超时）
    display_hal_wait_pending(200);
}
```

### 2.2 可能根因

#### 根因 A: JPEG 解码输出错误
- **症状匹配度**: ★★★★☆  
  色块错乱、横向错位可能是 YUV→RGB 转换错误或 JPEG 损坏
- **触发条件**: 
  - Tank 端 JPEG 编码质量下降（Q=60 可能不稳定）
  - 网络丢包导致 JPEG 流不完整（但 TCP 理论可靠）
  - 解码器 `jpeg_dec_process()` 返回 `JPEG_ERR_OK` 但输出脏数据
- **验证方法**: 
  - 在 Remote 端保存原始 JPEG 文件到 SD 卡，用 PC 查看是否正常
  - 检查 `hdr_info.width/height` 是否偶尔不等于 320×240

#### 根因 B: PSRAM 缓冲生命周期错误
- **症状匹配度**: ★★☆☆☆  
  `rgb_buf` 是 PSRAM 分配，每次解码复用；如果摇杆叠加时写越界，可能污染其他数据
- **触发条件**: 
  - `rc_joystick_render_overlay()` 内圆形绘制算法越界
  - `display_chunk_buf` DMA 传输期间被覆盖（但代码有 `wait_pending(200)`）
- **验证方法**: 
  - 临时禁用摇杆叠加，观察撕裂是否消失
  - 在 `rc_joystick.c:draw_circle_filled()` 增加严格边界检查日志

#### 根因 C: LCD DMA 传输冲突
- **症状匹配度**: ★★★★★ **最可能**  
  分块传输时，硬件 DMA 尚未完成前一块，软件就提交了下一块
- **触发条件**:
  - `display_hal_wait_pending(0)` 的 `while` 循环可能提前退出（例如函数返回 `ESP_ERR_TIMEOUT` 而不是 `ESP_OK`）
  - `display_hal_wait_pending(200)` 的 200ms 超时可能不足（理论 60 行需要 `60/240 * 12500 us = 3125 us`，200ms 足够，但硬件可能有额外延迟）
  - **核心问题**: 当前代码在 `y=0` 时调用 `wait_pending(0)` 的 `while` 循环，但**第一块还没提交**，循环直接跳过；后续块如果前一块 DMA 未完成，`draw_bitmap` 可能覆盖 DMA 正在读取的缓冲

**当前 Round 10 代码片段**:
```c
for (uint32_t y = 0; y < 240; y += 60) {
    // ...复制数据到 display_chunk_buf...
    while (display_hal_wait_pending(0) == ESP_OK) {}  // ⚠️ y=0 时无意义
    ret = display_hal_draw_bitmap_rgb565(0, y, 320, 60, display_chunk_buf);
    ret = display_hal_wait_pending(200);  // ⚠️ 如果超时，下一轮仍会提交
}
```

**修复方向**:
1. **在循环外等待初始状态**: 在第一块提交前，先确保 LCD 空闲
2. **严格检查 `wait_pending` 返回值**: 如果超时或失败，应跳过当前帧而不是继续提交下一块
3. **增加 DMA 完成信号的显式检查**: 例如使用 `display_hal` 内部的 `trans_done` 回调或信号量

#### 根因 D: CPU/任务调度抢占
- **症状匹配度**: ★★☆☆☆  
  `video_rx_task` 优先级为 `configMAX_PRIORITIES - 3`，可能被更高优先级任务（控制/网络）抢占
- **影响**: 分块传输被中断，导致时序错乱
- **验证方法**: 临时提升 `video_rx_task` 优先级到 `configMAX_PRIORITIES - 1`

### 2.3 理论时延分析

**Remote 显示链路时延构成** (Round 10 实测平均):
- `recv`: ~0ms（队列等待，取最新帧）
- `decode`: ~? ms（未单独统计，包含在 `total` 中）
- `display`: ~? ms（未单独统计）
- `total`: **165.87 ms/帧**

**理论 SPI 传输时延**:
- 320×240 RGB565 = 153,600 字节 = 1,228,800 位
- 20MHz SPI 单向传输（无 dummy cycle）: `1,228,800 / 20,000,000 = 61.44 ms`
- **加上命令开销**: 每块 60 行需要发送坐标命令（~10-20 字节），4 块总计 ~80 字节 × 8 bit / 20MHz ≈ 32 us（可忽略）
- **理论下限**: **61.44 ms/帧**

**实测 165.87 ms 的分解**:
- SPI 传输: ~61 ms
- JPEG 解码: ~? ms（需要插桩）
- 摇杆叠加: ~? ms（需要插桩）
- memcpy PSRAM→内部缓冲: 60 行 × 4 次 × (320×2 字节) = 153,600 字节  
  假设 PSRAM 读取速度 80MB/s: `153,600 / (80×10^6) ≈ 1.9 ms`（可忽略）
- **其他开销**: ~100 ms **← 瓶颈所在**

**可能的 100ms 额外开销来源**:
1. **JPEG 解码**: `jpeg_dec_process()` 软件解码 320×240 可能需要 50-80ms
2. **摇杆叠加**: `rc_joystick_render_overlay()` 的圆形绘制（嵌套循环 + alpha 混合）可能需要 10-30ms
3. **任务调度 + 队列等待**: 5-10ms
4. **DMA 等待超时**: 如果 `wait_pending(200)` 实际等待了接近 200ms，说明硬件有阻塞

---

## 3. 坦克屏幕乱码问题分析

### 3.1 问题现象

**图2 表现**: 左上角 WiFi 方块和电量数字显示异常，可能是：
- 矩形位置错误（超出屏幕边界）
- 颜色值错误（RGB565 大小端不匹配）
- 字体点阵渲染错误

### 3.2 代码检查

**WiFi 图标定义** ([rc_tank_screen.c:28-31](main/rc_tank_screen.c)):
```c
#define WIFI_ICON_X   280
#define WIFI_ICON_Y   10
#define WIFI_ICON_W   20
#define WIFI_ICON_H   15
```
- 坦克屏幕分辨率: 240×320（竖屏）或 320×240（横屏）
- **问题**: 如果是竖屏 240×320，`WIFI_ICON_X=280` 超出边界 (240 - 20 = 220)
- **代码中设置横屏** ([rc_video.c:698](main/rc_video.c)): `display_hal_set_orientation(true, false, false)`
- **矩形绘制** ([rc_tank_screen.c:133](main/rc_tank_screen.c)):
  ```c
  fill_rect(fb, w, h, WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_W, WIFI_ICON_H, wifi_color);
  ```
  - `w=320, h=240` (横屏)
  - `WIFI_ICON_X=280, WIFI_ICON_Y=10` → 右上角位置 ✅
  - `fill_rect` 有越界保护: `if (x + w > fb_w || y + h > fb_h) return;` ✅

**电量数字定义** ([rc_tank_screen.c:34-35](main/rc_tank_screen.c)):
```c
#define BATTERY_TEXT_X  265
#define BATTERY_TEXT_Y  35
```
- 两位数 + '%' 符号宽度: 5×2 + 6 + 斜线 4 像素 ≈ 20 像素
- `265 + 20 = 285 < 320` ✅ 不越界

**字体点阵** ([rc_tank_screen.c:57-68](main/rc_tank_screen.c)):
```c
static const uint8_t font_5x7[10][7] = {
    /* 0 */ {0x1F, 0x11, 0x11, 0x11, 0x1F, 0x00, 0x00},
    // ...
};
```
- 格式正确，但 **bit 顺序可能有问题**
- 绘制逻辑 ([rc_tank_screen.c:76-88](main/rc_tank_screen.c)):
  ```c
  for (int col = 0; col < 5; col++) {
      if (bits & (1 << col)) {  // ⚠️ LSB 先绘制
          fb[py * fb_w + px] = color;
      }
  }
  ```
  - 如果字体定义是 MSB-first，但代码用 LSB 测试，数字会左右镜像

### 3.3 根因推断

**最可能原因**: 
1. **颜色值大小端不匹配**: WiFi 方块可能是透明或黑色（颜色常量定义错误）
2. **字体点阵 bit 顺序反向**: 数字显示为乱码或镜像
3. **帧缓冲传输时 LCD 方向未设置**: `rc_tank_screen_render()` 渲染的是横屏 320×240，但 LCD 仍在竖屏模式

**验证方法**:
- 检查 `display_hal_set_orientation()` 是否在 `rc_tank_screen_render()` 前调用
- 在 PC 上导出 `fb` 内容为 BMP 文件，用图像查看器检查渲染是否正确

---

## 4. 帧率瓶颈系统分析

### 4.1 理论最优速率计算

#### Tank 采集侧理论上限

**SP0A39 摄像头输出**:
- 分辨率: 640×480 VYUY (YUV422)
- 数据量: 640 × 480 × 2 = 614,400 字节/帧
- **Sensor 理论帧率**: 假设 PCLK = 24MHz（需要查规格书确认）
  - 有效像素传输: 614,400 字节 × 8 bit / 24MHz = **204.8 ms/帧 ≈ 4.88 fps**  
    ⚠️ **这已经低于 Round 10 的 25fps，说明 PCLK 实际更高或有硬件加速**

**重新假设**: SP0A39 在 VGA 640×480 模式下典型 PCLK = 48MHz:
- 传输时间: 614,400 字节 × 8 / 48MHz = **102.4 ms/帧 ≈ 9.77 fps**  
  仍低于实测 25fps → **Sensor 可能工作在更高时钟或有其他优化**

**Round 10 实测 Tank 采集 25.26 fps** → 说明 DVP 链路正常。

#### Tank 编码与发送理论上限

**下采样** (640×480 → 320×240):
- 操作: 每隔一行、每隔一个宏像素取样
- CPU 纯拷贝: 153,600 字节 / 240MHz / CPI ≈ ? ms（需要 profiling）
- **Round 10 实测**: `subsample=48.19 ms` ✅ 已知

**JPEG 编码**:
- 输入: 320×240 YUV422 = 153,600 字节
- 输出: ~24KB (质量 Q=60)
- **Round 10 实测**: `encode=45.49 ms` ✅ 已知
- **理论**: ESP32-S3 软件 JPEG 编码器速度约 2-3 MB/s → 153KB / 2.5MB/s ≈ 61 ms（实测 45ms 优于预期）

**TCP 发送**:
- 数据量: 24KB/帧
- WiFi 理论吞吐: 802.11n 20MHz 单流 ~65Mbps（实际 TCP 约 30-40Mbps）
- 传输时间: 24KB × 8 / 30Mbps = **6.4 ms/帧** ✅ 不是瓶颈
- **Round 10 实测**: `send=? ms`（未单独统计，包含在 `total` 中）

**Tank 发送链路理论最快**:
- wait: 0 ms（sensor-driven，无等待）
- cache: ~2 ms（PSRAM msync）
- subsample: 48 ms
- encode: 45 ms
- send: 7 ms
- **总计**: ~**102 ms/帧 ≈ 9.8 fps**

**Round 10 实测**: `total=165.01 ms ≈ 6.06 fps` → **实际比理论慢 63ms**

**额外 63ms 来源**:
1. **任务调度开销**: 5-10ms
2. **TCP 发送阻塞**: 如果接收方来不及取走数据，`send()` 会阻塞等待窗口空闲（最多 5760 字节缓冲）
3. **JPEG 编码器内部开销**: `jpeg_enc_process()` 可能有内存分配/释放、Huffman 表构建等额外开销

#### Remote 接收与显示理论上限

**TCP 接收**:
- WiFi 理论: ~6.4 ms/帧（同发送）
- **实际**: 队列化接收，`video_net_rx_task` 独立运行，不应成为瓶颈

**JPEG 解码**:
- 输入: 24KB JPEG
- 输出: 320×240 RGB565 = 153,600 字节
- **理论**: 软件解码速度约 2-3 MB/s → 24KB / 2.5MB/s ≈ 10 ms  
  ⚠️ **这只是读取 JPEG 的时间，实际解码需要 Huffman 解压 + IDCT + YUV→RGB 转换**
- **估算**: 50-80 ms（需要实测插桩）

**摇杆叠加**:
- 操作: 绘制两个圆（底座 + 摇杆头），每个圆约 40-50 像素半径
- 像素数: 底座 π×40² ≈ 5000 像素，摇杆 π×20² ≈ 1250 像素
- alpha 混合: 每像素需要 RGB565 拆分 + 平均 + 重组 ≈ 10 条指令
- CPU 开销: 6250 像素 × 10 / 240MHz ≈ **0.26 ms** ✅ 可忽略  
  ⚠️ **但嵌套循环 `for dy in -r..r { for dx in ...}` 可能有大量无效判断，实际可能 5-10ms**

**SPI 传输**:
- 理论: 61.44 ms（已算出）
- **实际**: 可能有命令开销 + CS 切换延迟，约 **65-70 ms**

**Remote 显示链路理论最快**:
- recv: 0 ms（队列）
- decode: 60 ms
- joystick: 5 ms
- display: 65 ms
- **总计**: ~**130 ms/帧 ≈ 7.69 fps**

**Round 10 实测**: `total=165.87 ms ≈ 6.03 fps` → **实际比理论慢 36ms**

**额外 36ms 来源**:
1. **JPEG 解码实际更慢**: 可能需要 80-100ms
2. **memcpy PSRAM→内部缓冲**: 虽然单次快，但 4 次累积可能 5-10ms
3. **`wait_pending()` 等待超时**: 如果 DMA 有阻塞，每次等待可能接近 200ms（但这会导致 `total` 更大，矛盾）
4. **任务被抢占**: 网络/控制任务优先级更高

### 4.2 端到端理论最优

**理想流水线**（忽略任务切换）:
- Tank 编码: 102 ms
- 网络传输: 7 ms
- Remote 解码+显示: 130 ms
- **端到端**: 239 ms/帧 ≈ **4.18 fps**

**Round 10 实测**:
- Tank 发送: 165 ms/帧 (6 fps)
- Remote 显示: 166 ms/帧 (6 fps)
- **基本相等** → 说明不是网络瓶颈，而是**双方都受到各自本地处理速度限制**

### 4.3 达到 10fps 的理论路径

要达到 10fps (100ms/帧)，需要：
- Tank: 下采样 + 编码 + 发送 < 100ms
- Remote: 接收 + 解码 + 显示 < 100ms

**Tank 优化空间**:
1. **下采样加速**: 
   - 当前 48ms，改用硬件加速（ESP32-S3 无硬件 scaler，但可以用 ESP-DL 的优化函数）
   - 或降低到 QVGA 直接输出（需要 Sensor 支持，可能降低画质）
2. **JPEG 编码加速**:
   - 当前 45ms，降低质量到 Q=40-50 → 可能降到 30-35ms
   - 或使用硬件 JPEG 编码器（ESP32-S3 无此硬件，只能用软件）
3. **并行处理**:
   - 下采样与编码串行 → 改为：下采样完成后立即释放 DVP buffer，编码在独立 buffer 中进行（**当前已实现**）
   - 发送与下一帧采集并行（**当前已实现**）

**理论最优 Tank 速度**（激进假设）:
- subsample: 30ms（优化算法 + DMA 加速）
- encode: 30ms（降低质量 + 优化）
- send: 7ms
- **总计**: 67ms/帧 ≈ **14.9 fps** ✅ 可达 10fps

**Remote 优化空间**:
1. **JPEG 解码加速**:
   - 假设当前 60-80ms → 无法再优化（软件解码已经是瓶颈）
   - **激进方案**: 使用 MJPEG 硬件解码器（ESP32-S3 无此硬件）
2. **SPI 传输加速**:
   - 当前 20MHz → 提升到 26.67MHz 或 40MHz（需要验证 ST7789V3 支持）
   - 40MHz 下传输时间: 61.44 × (20/40) = **30.72 ms** ✅ 节省 30ms
3. **减少 memcpy 开销**:
   - 直接在 DMA-capable 内部 RAM 中解码（需要 153KB 内部 RAM，可能不足）
   - 或使用 LCD DMA 直接从 PSRAM 读取（需要硬件支持 external memory burst）

**理论最优 Remote 速度**（激进假设）:
- decode: 60ms（无法优化）
- joystick: 5ms
- display: 31ms（40MHz SPI）
- **总计**: 96ms/帧 ≈ **10.4 fps** ✅ 刚好达到 10fps

### 4.4 结论

**当前 6fps 的主要瓶颈**:
1. **Tank 端**: 下采样 (48ms) + JPEG 编码 (45ms) = **93ms**，占总时延 165ms 的 56%
2. **Remote 端**: JPEG 解码 (估计 60-80ms) + SPI 传输 (61ms) = **121-141ms**，占总时延 166ms 的 73-85%

**达到 10fps 的必要条件**:
- **Tank**: 必须将下采样+编码降到 60ms 以内 → 需要优化算法或降低质量
- **Remote**: 必须将 SPI 时钟提升到 40MHz 或更高 → 需要硬件支持验证

**不优化 JPEG 编解码的情况下**:
- Tank 编码 45ms + Remote 解码 60ms = **105ms** → 即使其他开销为 0，最多只能达到 **9.5 fps**
- **结论**: **10fps 目标在当前软件 JPEG 路径下几乎不可能达到**，除非：
  1. 大幅降低 JPEG 质量（可能导致画质不可接受）
  2. 使用硬件加速（ESP32-S3 无 JPEG 硬件）
  3. 改用更简单的压缩格式（如 RLE、差分编码，但压缩率低，网络成本增加）

---

## 5. 综合建议

### 5.1 问题 1: 摇杆控制

**短期修复**:
1. 检查电机接线物理方向，或在代码中交换 `TURN_LEFT` 和 `TURN_RIGHT` 的映射
2. 增加触摸有效范围检查，超出摇杆底座 `BASE_R + 10` 时忽略触摸

**长期优化** (遵循新需求):
1. 调整摇杆位置到更左侧 (`BASE_CX = 50-60`)
2. 增大摇杆尺寸 (`BASE_R = 50`, `KNOB_R = 25`)
3. 实现"超出边界自动回正"逻辑

### 5.2 问题 2: 屏幕撕裂

**高优先级排查**:
1. 在 `video_rx_task` 第一块传输前增加初始 `wait_pending(500)`，确保 LCD 空闲
2. 严格检查每次 `wait_pending(200)` 返回值，超时则跳过本帧而不是继续下一块
3. 增加 JPEG 解码错误检测，保存异常 JPEG 文件到日志

**次优先级**:
1. 临时禁用摇杆叠加，观察撕裂是否消失
2. 提升 `video_rx_task` 优先级到 `configMAX_PRIORITIES - 1`

### 5.3 问题 3: 坦克屏幕乱码

**优先验证**:
1. 在 Tank 端增加日志，输出 `fb` 前 100 个像素的 RGB565 值，检查颜色是否正确
2. 检查 `display_hal_set_orientation()` 的调用时机是否在渲染前
3. 修正字体点阵 bit 顺序（测试 MSB-first vs LSB-first）

### 5.4 问题 4: 帧率瓶颈

**理性预期**:
- 在当前硬件和软件 JPEG 路径下，**10fps 几乎不可达到**
- **现实目标**: 7-8 fps（通过以下优化）

**推荐优化路径** (按收益排序):
1. **提升 Remote SPI 时钟到 26.67MHz** → 可节省 20ms，提升到 ~7.5fps
2. **降低 JPEG 质量到 Q=50** → Tank 编码可能降到 35ms，提升到 ~8fps
3. **优化下采样算法** → 使用 ESP-IDF 的 DMA/memcpy 优化，可能降到 35ms
4. **并行化解码与显示** → Remote 端解码下一帧时，当前帧 SPI 传输并行进行（需要双缓冲）

**不推荐**:
- 32KB TCP 窗口：Round 10 已验证无效，反而导致内存不足
- 降低分辨率到 QQVGA (160×120)：画质不可接受

---

## 6. 下一步行动

**本轮只做分析，不开发**。等待用户确认:
1. 摇杆左右反向是否需要代码修复，还是硬件接线调整？
2. 是否接受 7-8fps 作为现实目标，还是坚持 10fps（需要更激进优化）？
3. 屏幕撕裂和乱码问题优先级？（建议优先修复撕裂）

