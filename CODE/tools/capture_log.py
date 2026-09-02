#!/usr/bin/env python3
"""Read serial port for a fixed duration and dump to file. Robust, non-interactive."""
import sys
import time
import argparse
import serial  # pyserial, bundled with ESP-IDF python env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM7')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--duration', type=int, default=90)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    print(f"=== reading {args.port}@{args.baud} for {args.duration}s -> {args.out} ===",
          flush=True)

    # Toggle DTR/RTS to reset the ESP32 so we catch a clean boot.
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    # Hardware reset: RTS -> EN. Pulse it.
    ser.setRTS(True)
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setRTS(False)
    time.sleep(0.05)
    ser.reset_input_buffer()

    deadline = time.time() + args.duration
    total = 0
    with open(args.out, 'wb') as f:
        while time.time() < deadline:
            data = ser.read(4096)
            if data:
                f.write(data)
                f.flush()
                total += len(data)
    ser.close()
    print(f"BYTES={total}", flush=True)


if __name__ == '__main__':
    main()
