#!/usr/bin/env python3
"""Prepare TF-backed boot animation and audio assets for Demo Hub."""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import wave
import zlib
from pathlib import Path


MAGIC = b"R5Z1"
FORMAT_VERSION = 1
HEADER = struct.Struct("<4s6H2I")
FRAME_HEADER = struct.Struct("<II")
EXPECTED_WIDTH = 320
EXPECTED_HEIGHT = 240
MAX_FRAME_COUNT = 200


def read_gif_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:10]
    if len(data) != 10 or data[:4] != b"GIF8":
        raise ValueError(f"not a GIF file: {path}")
    return struct.unpack_from("<HH", data, 6)


def decode_gif_rgb565(path: Path, fps: int) -> tuple[int, int, list[bytes]]:
    if shutil.which("ffmpeg") is None:
        raise RuntimeError("ffmpeg is required to prepare the boot animation")

    width, height = read_gif_dimensions(path)
    if (EXPECTED_WIDTH, EXPECTED_HEIGHT) != (width, height):
        raise ValueError(
            f"boot GIF must be {EXPECTED_WIDTH}x{EXPECTED_HEIGHT}, got {width}x{height}"
        )

    command = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(path),
        "-vf",
        f"fps={fps}",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb565le",
        "pipe:1",
    ]
    decoded = subprocess.run(command, check=True, stdout=subprocess.PIPE).stdout
    frame_size = width * height * 2
    if 0 == len(decoded) or 0 != (len(decoded) % frame_size):
        raise ValueError("ffmpeg returned an incomplete RGB565 frame stream")

    frame_count = len(decoded) // frame_size
    if MAX_FRAME_COUNT < frame_count:
        raise ValueError(f"too many animation frames: {frame_count}")
    frames = [decoded[offset : offset + frame_size]
              for offset in range(0, len(decoded), frame_size)]
    return width, height, frames


def encode_animation(width: int, height: int, frames: list[bytes],
                     fps: int) -> tuple[bytes, dict[str, int]]:
    payload = bytearray()
    compressed_sizes = []
    expected_frame_size = width * height * 2
    for frame in frames:
        if expected_frame_size != len(frame):
            raise ValueError("decoded RGB565 frame has an invalid size")
        compressed = zlib.compress(frame, level=9)
        payload.extend(FRAME_HEADER.pack(len(compressed), len(frame)))
        payload.extend(compressed)
        compressed_sizes.append(len(compressed))

    frame_delay_ms = round(1000 / fps)
    payload_bytes = bytes(payload)
    header = HEADER.pack(
        MAGIC,
        FORMAT_VERSION,
        HEADER.size,
        width,
        height,
        len(frames),
        frame_delay_ms,
        len(payload_bytes),
        zlib.crc32(payload_bytes) & 0xFFFFFFFF,
    )
    stats = {
        "frame_count": len(frames),
        "frame_delay_ms": frame_delay_ms,
        "smallest_frame": min(compressed_sizes),
        "largest_frame": max(compressed_sizes),
    }
    return header + payload_bytes, stats


def validate_animation(data: bytes) -> dict[str, int]:
    if HEADER.size > len(data):
        raise ValueError("animation file is smaller than its header")
    (magic, version, header_size, width, height, frame_count, frame_delay_ms,
     payload_size, expected_crc) = HEADER.unpack_from(data)
    if MAGIC != magic or FORMAT_VERSION != version or HEADER.size != header_size:
        raise ValueError("animation header is not compatible")
    if (EXPECTED_WIDTH, EXPECTED_HEIGHT) != (width, height):
        raise ValueError("animation dimensions are not compatible")
    if 0 == frame_count or MAX_FRAME_COUNT < frame_count or 0 == frame_delay_ms:
        raise ValueError("animation timing is invalid")

    payload = data[header_size:]
    if payload_size != len(payload):
        raise ValueError("animation payload size does not match the header")
    if expected_crc != (zlib.crc32(payload) & 0xFFFFFFFF):
        raise ValueError("animation payload CRC32 mismatch")

    expected_frame_size = width * height * 2
    cursor = 0
    for _ in range(frame_count):
        if FRAME_HEADER.size > len(payload) - cursor:
            raise ValueError("truncated frame header")
        compressed_size, frame_size = FRAME_HEADER.unpack_from(payload, cursor)
        cursor += FRAME_HEADER.size
        if (expected_frame_size != frame_size or 0 == compressed_size or
                compressed_size > len(payload) - cursor):
            raise ValueError("invalid compressed frame header")
        compressed = payload[cursor : cursor + compressed_size]
        cursor += compressed_size
        frame = zlib.decompress(compressed)
        if expected_frame_size != len(frame):
            raise ValueError("decompressed RGB565 frame has an invalid size")
    if cursor != len(payload):
        raise ValueError("animation payload contains trailing bytes")
    return {
        "width": width,
        "height": height,
        "frame_count": frame_count,
        "frame_delay_ms": frame_delay_ms,
        "payload_size": payload_size,
    }


def prepare_audio(source: Path, destination: Path) -> int:
    with wave.open(str(source), "rb") as wav_file:
        if (1 != wav_file.getnchannels() or 2 != wav_file.getsampwidth() or
                16000 != wav_file.getframerate() or "NONE" != wav_file.getcomptype()):
            raise ValueError("boot WAV must be PCM, 16kHz, 16-bit, mono")
        pcm = wav_file.readframes(wav_file.getnframes())
    destination.write_bytes(pcm)
    return len(pcm)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gif", required=True, type=Path)
    parser.add_argument("--wav", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--fps", type=int, default=10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 1 <= args.fps <= 30:
        raise ValueError("fps must be between 1 and 30")

    width, height, frames = decode_gif_rgb565(args.gif, args.fps)
    animation, encoder_stats = encode_animation(width, height, frames, args.fps)
    validation_stats = validate_animation(animation)

    args.output.mkdir(parents=True, exist_ok=True)
    animation_path = args.output / "boot_animation.r565z"
    audio_path = args.output / "boot_sound.pcm"
    animation_path.write_bytes(animation)
    audio_size = prepare_audio(args.wav, audio_path)

    print(f"animation: {animation_path} ({len(animation)} bytes)")
    print(f"audio: {audio_path} ({audio_size} bytes)")
    print(
        "frames={frame_count} delay={frame_delay_ms}ms compressed_frame="
        "{smallest_frame}..{largest_frame} bytes".format(**encoder_stats)
    )
    print(f"validated payload={validation_stats['payload_size']} bytes")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError,
            zlib.error) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
