#!/usr/bin/env python3

import argparse
import hashlib
import struct
from pathlib import Path

from PIL import Image, ImageFont


MAGIC = b"NTFN16B2"
VERSION = 1
BPP = 2
LINE_HEIGHT = 20
BASE_LINE = 5
MAX_BOX_WIDTH = 18
METRIC_SIZE = 12
FIXED_HEADER = struct.Struct("<8sHHIIIIBBBBB3x")
RANGE_RECORD = struct.Struct("<III")
METRIC_RECORD = struct.Struct("<IHBBbb2x")
UNICODE_RANGES = (
    (0x0020, 0x007E),
    (0x3000, 0x303F),
    (0x4E00, 0x9FFF),
    (0xFF01, 0xFF60),
)


def parse_arguments() -> argparse.Namespace:
    demo_dir = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Generate the embedded Noto Sans SC 16 px LVGL font."
    )
    parser.add_argument(
        "--source",
        type=Path,
        default=demo_dir / "fonts" / ".source" / "NotoSansSC-wght-cdn.ttf",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=demo_dir / "main" / "notifier_noto_sans_sc_16.bin",
    )
    return parser.parse_args()


def load_font(source: Path) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(source), 16)
    try:
        axes = font.get_variation_axes()
        if axes:
            font.set_variation_by_axes([400])
    except (AttributeError, OSError, ValueError):
        pass
    return font


def quantize_and_pack(image: Image.Image) -> bytes:
    packed = bytearray()
    accumulator = 0
    pixel_index = 0

    for pixel in image.tobytes():
        value = (int(pixel) * 3 + 127) // 255
        accumulator |= value << (6 - ((pixel_index % 4) * 2))
        pixel_index += 1
        if 0 == (pixel_index % 4):
            packed.append(accumulator)
            accumulator = 0
    if 0 != (pixel_index % 4):
        packed.append(accumulator)
    return bytes(packed)


def render_glyph(font: ImageFont.FreeTypeFont, codepoint: int):
    character = chr(codepoint)
    box = font.getbbox(character, anchor="ls")
    advance = max(1, int(font.getlength(character) + 0.5))
    if box is None:
        return advance, 0, 0, 0, 0, b""

    x0, y0, x1, y1 = box
    width = x1 - x0
    height = y1 - y0
    if 0 == width or 0 == height:
        return advance, 0, 0, 0, 0, b""

    mask = font.getmask(character, mode="L")
    image = Image.frombytes("L", (width, height), bytes(mask))
    scale = min(1.0, MAX_BOX_WIDTH / width, LINE_HEIGHT / height)
    if scale < 1.0:
        width = max(1, int(width * scale + 0.5))
        height = max(1, int(height * scale + 0.5))
        image = image.resize((width, height), Image.Resampling.LANCZOS)
        x0 = int(x0 * scale + (0.5 if 0 <= x0 else -0.5))
        y0 = int(y0 * scale + (0.5 if 0 <= y0 else -0.5))
        advance = max(1, min(MAX_BOX_WIDTH, int(advance * scale + 0.5)))

    y1 = y0 + height
    if y0 < -(LINE_HEIGHT - BASE_LINE):
        y0 = -(LINE_HEIGHT - BASE_LINE)
    if y1 > BASE_LINE:
        y0 -= y1 - BASE_LINE

    return advance, width, height, x0, -y0 - height, quantize_and_pack(image)


def generate_font(source: Path, output: Path) -> None:
    if not source.is_file():
        raise SystemExit(f"Source font not found: {source}")
    license_path = source.parents[1] / "OFL.txt"
    if not license_path.is_file():
        raise SystemExit(f"Font license not found: {license_path}")

    font = load_font(source)
    metrics = bytearray()
    bitmaps = bytearray()
    ranges = bytearray()
    glyph_index = 0

    for range_start, range_end in UNICODE_RANGES:
        ranges.extend(RANGE_RECORD.pack(range_start, range_end, glyph_index))
        for codepoint in range(range_start, range_end + 1):
            advance, width, height, ofs_x, ofs_y, bitmap = render_glyph(
                font, codepoint
            )
            metrics.extend(
                METRIC_RECORD.pack(
                    len(bitmaps), advance, width, height, ofs_x, ofs_y
                )
            )
            bitmaps.extend(bitmap)
            glyph_index += 1

    header_size = FIXED_HEADER.size + len(ranges)
    metrics_offset = header_size
    bitmap_offset = metrics_offset + len(metrics)
    alignment_padding = (-bitmap_offset) % 4
    bitmap_offset += alignment_padding
    file_size = bitmap_offset + len(bitmaps)
    header = FIXED_HEADER.pack(
        MAGIC,
        VERSION,
        header_size,
        glyph_index,
        metrics_offset,
        bitmap_offset,
        file_size,
        LINE_HEIGHT,
        BASE_LINE,
        BPP,
        len(UNICODE_RANGES),
        METRIC_SIZE,
    )
    payload = header + ranges + metrics + (b"\0" * alignment_padding) + bitmaps
    if len(payload) != file_size:
        raise RuntimeError("Generated font size does not match its header")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(payload)
    temporary.replace(output)
    print(
        "generated"
        f" glyphs={glyph_index} bytes={file_size} bpp={BPP}"
        f" source_sha256={hashlib.sha256(source.read_bytes()).hexdigest()[:16]}"
        f" output_sha256={hashlib.sha256(payload).hexdigest()[:16]}"
    )


def main() -> None:
    arguments = parse_arguments()
    generate_font(arguments.source, arguments.output)


if __name__ == "__main__":
    main()
