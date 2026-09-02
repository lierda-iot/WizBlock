#!/usr/bin/env python3
"""
RC Tank Demo 串口监控工具
实时显示设备启动日志和运行状态
"""
import serial
import time
import sys

PORT = 'COM7'
BAUDRATE = 115200

def monitor_serial(duration=60):
    """监控串口输出"""
    try:
        print(f"正在连接 {PORT} @ {BAUDRATE} baud...")
        ser = serial.Serial(PORT, BAUDRATE, timeout=1)
        print("连接成功！监控中...\n")
        print("=" * 60)

        start_time = time.time()
        last_data_time = time.time()

        while time.time() - start_time < duration:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                try:
                    text = data.decode('utf-8', errors='replace')
                    print(text, end='', flush=True)
                    last_data_time = time.time()
                except Exception as e:
                    print(f"\n[解码错误: {e}]")

            # 如果5秒没数据，提示用户
            if time.time() - last_data_time > 5:
                print("\n[提示: 5秒无数据，请按设备复位键]", flush=True)
                last_data_time = time.time()

            time.sleep(0.05)

        print("\n" + "=" * 60)
        print("监控结束")
        ser.close()

    except serial.SerialException as e:
        print(f"串口错误: {e}")
        print("请检查:")
        print("1. 设备是否连接到 COM7")
        print("2. 是否有其他程序占用串口")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n用户中断")
        ser.close()
        sys.exit(0)

if __name__ == '__main__':
    if len(sys.argv) > 1:
        duration = int(sys.argv[1])
    else:
        duration = 60

    monitor_serial(duration)
