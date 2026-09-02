#!/usr/bin/env python3

import argparse
import struct
from pathlib import Path


MAGIC = b"NTFN16B2"
EXPECTED_RANGES = (
    (0x0020, 0x007E),
    (0x3000, 0x303F),
    (0x4E00, 0x9FFF),
    (0xFF01, 0xFF60),
)
FIXED_HEADER = struct.Struct("<8sHHIIIIBBBBB3x")
RANGE_RECORD = struct.Struct("<III")
METRIC_RECORD = struct.Struct("<IHBBbb2x")


def parse_arguments() -> argparse.Namespace:
    demo_dir = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Verify the embedded font binary.")
    parser.add_argument(
        "font",
        nargs="?",
        type=Path,
        default=demo_dir / "main" / "notifier_noto_sans_sc_16.bin",
    )
    return parser.parse_args()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def verify_font(path: Path) -> None:
    data = path.read_bytes()
    require(len(data) >= FIXED_HEADER.size, "font header is truncated")
    (
        magic,
        version,
        header_size,
        glyph_count,
        metrics_offset,
        bitmap_offset,
        file_size,
        line_height,
        base_line,
        bpp,
        range_count,
        metric_size,
    ) = FIXED_HEADER.unpack_from(data)

    require(MAGIC == magic, "font magic mismatch")
    require(1 == version, "font version mismatch")
    require(20 == line_height and 5 == base_line, "font vertical metrics mismatch")
    require(2 == bpp, "font must use 2 bpp bitmaps")
    require(METRIC_RECORD.size == metric_size, "font metric size mismatch")
    require(len(EXPECTED_RANGES) == range_count, "font range count mismatch")
    require(len(data) == file_size, "font file size mismatch")
    require(file_size < 2 * 1024 * 1024, "font exceeds the 2 MiB budget")
    require(
        FIXED_HEADER.size + range_count * RANGE_RECORD.size == header_size,
        "font header size mismatch",
    )
    require(header_size == metrics_offset, "font metrics offset mismatch")
    require(
        metrics_offset + glyph_count * metric_size <= bitmap_offset,
        "font metric table overlaps bitmap data",
    )

    expected_glyph_count = 0
    for range_index, expected in enumerate(EXPECTED_RANGES):
        start, end, first_index = RANGE_RECORD.unpack_from(
            data, FIXED_HEADER.size + range_index * RANGE_RECORD.size
        )
        require(expected == (start, end), "font Unicode range mismatch")
        require(first_index == expected_glyph_count, "font range index mismatch")
        expected_glyph_count += end - start + 1
    require(expected_glyph_count == glyph_count, "font glyph count mismatch")

    previous_end = 0
    nonempty_count = 0
    for index in range(glyph_count):
        record_offset = metrics_offset + index * metric_size
        bitmap_relative, advance, width, height, ofs_x, ofs_y = (
            METRIC_RECORD.unpack_from(data, record_offset)
        )
        bitmap_size = (width * height * bpp + 7) // 8
        require(1 <= advance <= 18, "font glyph advance is outside its budget")
        require(width <= 18 and height <= 20, "font glyph box exceeds its cell")
        require(-18 <= ofs_x <= 18, "font glyph x offset is invalid")
        require(-5 <= ofs_y <= 15, "font glyph y offset is invalid")
        require(bitmap_relative >= previous_end, "font bitmap offsets overlap")
        require(
            bitmap_offset + bitmap_relative + bitmap_size <= file_size,
            "font glyph bitmap exceeds the file",
        )
        previous_end = bitmap_relative + bitmap_size
        if 0 < bitmap_size:
            nonempty_count += 1

    require(nonempty_count > 21000, "font contains too many empty glyphs")
    require(previous_end == file_size - bitmap_offset, "font has trailing bitmap data")
    print(
        f"notifier_font_test: PASS glyphs={glyph_count}"
        f" bytes={file_size} nonempty={nonempty_count}"
    )


def main() -> None:
    arguments = parse_arguments()
    verify_font(arguments.font)


if __name__ == "__main__":
    main()
