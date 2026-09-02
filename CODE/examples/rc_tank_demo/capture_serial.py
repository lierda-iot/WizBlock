#!/usr/bin/env python3
"""Capture one serial port to a binary-safe log file."""

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--duration", type=float, default=120.0)
    parser.add_argument("--reset", action="store_true")
    args = parser.parse_args()

    byte_count = 0
    with serial.Serial(args.port, 115200, timeout=0.1) as uart:
        if args.reset:
            uart.dtr = False
            uart.rts = True
            time.sleep(0.15)
            uart.rts = False

        deadline = time.monotonic() + args.duration
        with open(args.output, "wb") as output:
            while time.monotonic() < deadline:
                data = uart.read(4096)
                if data:
                    output.write(data)
                    output.flush()
                    byte_count += len(data)

    print(f"port={args.port} bytes={byte_count} duration={args.duration:.1f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
