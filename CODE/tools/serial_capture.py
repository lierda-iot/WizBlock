#!/usr/bin/env python3
"""串口日志抓取: python serial_capture.py <port> <duration_s> <outfile> [reset]

第4参数为 "reset" 时, 打开端口后主动产生一次复位脉冲(RTS->EN, DTR->IO0),
使芯片从 t=0 重新启动并抓取完整启动日志; 否则被动抓取运行中设备(不复位)。
"""
import sys
import time
import serial

def do_reset_pulse(ser):
    # 模仿 esptool 的 hard_reset: 仅脉冲 RTS(EN), DTR(IO0) 保持 False
    # 这是 esptool 验证可靠的复位到运行模式的序列
    ser.setDTR(False)   # IO0 保持默认（由板上电阻决定，通常上拉=启动模式）
    ser.setRTS(True)    # EN = LOW  (进入复位)
    time.sleep(0.1)
    ser.setRTS(False)   # EN = HIGH (释放复位, 启动 app)

def main():
    port = sys.argv[1]
    duration = float(sys.argv[2])
    outfile = sys.argv[3]
    reset = len(sys.argv) > 4 and sys.argv[4].lower() == "reset"

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    # 被动模式：显式把 DTR/RTS 都置 False (EN 与 IO0 均不拉低=正常运行),
    #           避免 Windows 打开端口默认拉高控制线而复位/进下载模式。
    # reset 模式：端口 open 后再手动脉冲 RTS。
    ser.dtr = False
    ser.rts = False
    ser.open()

    if reset:
        do_reset_pulse(ser)

    start = time.time()
    n = 0
    with open(outfile, "wb") as f:
        while time.time() - start < duration:
            data = ser.read(4096)
            if data:
                f.write(data)
                f.flush()
                n += len(data)
    ser.close()
    print(f"captured {n} bytes -> {outfile} (reset={reset})")

if __name__ == "__main__":
    main()
