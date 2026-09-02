# RC Tank Demo 固件构建指南

## ⚠️ 重要说明

由于 ESP-IDF 在 Git Bash 环境下不兼容，需要在 **Windows CMD** 或 **PowerShell** 中手动执行构建。

---

## 🔨 手动构建步骤

### 第1步：打开 CMD 窗口

按 `Win + R`，输入 `cmd`，回车打开命令提示符。

### 第2步：进入项目目录

```cmd
cd /d E:\10__AIProject\7_AI陪伴机器人\CODE\examples\rc_tank_demo
```

### 第3步：构建 Tank 固件

直接双击运行 `build_tank.bat`，或在 CMD 中执行：

```cmd
build_tank.bat
```

**构建时间**：约 3-5 分钟

**成功标志**：
- 看到 "Tank firmware built successfully!"
- `firmware_backup\` 目录出现 3 个文件：
  - `rc_tank_demo_TANK.bin`
  - `bootloader_TANK.bin`
  - `partition-table_TANK.bin`

### 第4步：构建 Remote 固件

等待 Tank 构建完成后，双击运行 `build_remote.bat`，或在 CMD 中执行：

```cmd
build_remote.bat
```

**构建时间**：约 3-5 分钟

**成功标志**：
- 看到 "Remote firmware built successfully!"
- `firmware_backup\` 目录新增 3 个文件：
  - `rc_tank_demo_REMOTE.bin`
  - `bootloader_REMOTE.bin`
  - `partition-table_REMOTE.bin`

---

## ✅ 构建完成检查

执行以下命令检查固件是否全部生成：

```cmd
dir firmware_backup
```

**应该看到 6 个文件**：
```
rc_tank_demo_TANK.bin
bootloader_TANK.bin
partition-table_TANK.bin
rc_tank_demo_REMOTE.bin
bootloader_REMOTE.bin
partition-table_REMOTE.bin
```

---

## 🔌 烧录（硬件准备好后）

### 烧录 Tank 固件

1. 连接 Tank 设备到 COM7
2. 双击运行 `flash_tank_COM7.bat`

### 烧录 Remote 固件

1. 连接 Remote 设备到 COM7
2. 双击运行 `flash_remote_COM7.bat`

---

## ❌ 常见问题

### 问题1：构建脚本报错 "python.exe 不是内部或外部命令"

**原因**：Python 环境路径不正确

**解决**：检查以下文件是否存在
```
D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe
```

### 问题2：构建报错 "IDF_PATH 未设置"

**原因**：环境变量未正确设置

**解决**：构建脚本会自动设置，如果仍报错，手动执行：
```cmd
set IDF_PATH=D:\Espressif\frameworks\esp-idf-v5.5.4
```

### 问题3：构建卡在某个步骤

**解决**：
1. 按 `Ctrl+C` 终止
2. 删除 `build` 和 `sdkconfig` 文件
3. 重新运行构建脚本

### 问题4：想要全新构建

**解决**：
```cmd
rd /s /q build
del sdkconfig
build_tank.bat
```

---

## 📝 构建输出说明

### 正常构建日志关键信息

```
========================================
Building RC Tank Demo - TANK Role
========================================

Executing action: fullclean
...
Executing action: set-target
...
Executing action: build
...
[100%] Built target app

========================================
Tank firmware built successfully!
========================================
Firmware backed up to firmware_backup\rc_tank_demo_TANK.bin
```

### 构建失败示例

```
FAILED: ...
ninja: build stopped: subcommand failed
```

如果看到 `FAILED` 或 `ninja: build stopped`，说明构建失败，请检查错误信息。

---

## 🎯 快速构建流程总结

1. **打开 CMD** → 进入项目目录
2. **执行 `build_tank.bat`** → 等待 3-5 分钟
3. **执行 `build_remote.bat`** → 等待 3-5 分钟
4. **检查 `firmware_backup\`** → 确认 6 个文件存在
5. **等待硬件准备好** → 使用烧录脚本

---

构建脚本位置：
```
E:\10__AIProject\7_AI陪伴机器人\CODE\examples\rc_tank_demo\
├── build_tank.bat          ← Tank 构建脚本
├── build_remote.bat        ← Remote 构建脚本
├── flash_tank_COM7.bat     ← Tank 烧录脚本
└── flash_remote_COM7.bat   ← Remote 烧录脚本
```

---

生成时间：2026-08-18
