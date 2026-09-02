#!/usr/bin/env python3
"""Build the deterministic HoloCubic RGB565LE animation partition image."""

import argparse
import pathlib
import struct
import zlib


FRAME_WIDTH = 240
FRAME_HEIGHT = 240
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * 2
FRAME_COUNT = 49
FRAME_PERIOD_MS = 100
MAGIC = b"HFRM"
VERSION = 1
HEADER_BYTES = 32
PIXEL_FORMAT_RGB565LE = 1
HEADER_FORMAT = "<4sHHHHHHIIII"


def collect_frames(input_dir: pathlib.Path) -> list[pathlib.Path]:
    expected = [input_dir / f"frame{index:03d}.rgb565"
                for index in range(FRAME_COUNT)]
    actual = sorted(input_dir.glob("*.rgb565"))
    if actual != expected:
        expected_names = {path.name for path in expected}
        actual_names = {path.name for path in actual}
        missing = sorted(expected_names - actual_names)
        extra = sorted(actual_names - expected_names)
        raise ValueError(f"animation frames mismatch missing={missing} extra={extra}")
    for frame_path in expected:
        frame_size = frame_path.stat().st_size
        if FRAME_BYTES != frame_size:
            raise ValueError(
                f"invalid frame size path={frame_path} actual={frame_size} "
                f"expected={FRAME_BYTES}")
    return expected


def build_image(input_dir: pathlib.Path, output_path: pathlib.Path) -> dict[str, int]:
    frame_paths = collect_frames(input_dir)
    payload_crc32 = 0
    payload_bytes = FRAME_COUNT * FRAME_BYTES
    for frame_path in frame_paths:
        payload_crc32 = zlib.crc32(frame_path.read_bytes(), payload_crc32)
    payload_crc32 &= 0xFFFFFFFF
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        HEADER_BYTES,
        FRAME_WIDTH,
        FRAME_HEIGHT,
        PIXEL_FORMAT_RGB565LE,
        FRAME_COUNT,
        FRAME_PERIOD_MS,
        payload_bytes,
        payload_crc32,
        0,
    )
    if HEADER_BYTES != len(header):
        raise RuntimeError(f"unexpected header size {len(header)}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as image_file:
        image_file.write(header)
        for frame_path in frame_paths:
            image_file.write(frame_path.read_bytes())
    image_bytes = output_path.stat().st_size
    expected_image_bytes = HEADER_BYTES + payload_bytes
    if expected_image_bytes != image_bytes:
        raise RuntimeError(
            f"unexpected image size actual={image_bytes} expected={expected_image_bytes}")
    return {
        "frame_count": FRAME_COUNT,
        "payload_bytes": payload_bytes,
        "payload_crc32": payload_crc32,
        "image_bytes": image_bytes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    stats = build_image(args.input_dir.resolve(), args.output.resolve())
    print(
        "holo_frames image ready "
        f"frames={stats['frame_count']} payload={stats['payload_bytes']} "
        f"crc32={stats['payload_crc32']:08x} bytes={stats['image_bytes']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
