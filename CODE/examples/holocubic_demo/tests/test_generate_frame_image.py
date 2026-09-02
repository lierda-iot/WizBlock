#!/usr/bin/env python3

import importlib.util
import pathlib
import struct
import tempfile
import unittest
import zlib


SCRIPT_PATH = pathlib.Path(__file__).resolve().parents[1] / "tools" / \
    "generate_frame_image.py"
SPEC = importlib.util.spec_from_file_location("generate_frame_image", SCRIPT_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class GenerateFrameImageTest(unittest.TestCase):
    def make_frames(self, directory: pathlib.Path, count: int = 49) -> None:
        frame_data = bytes(MODULE.FRAME_BYTES)
        for index in range(count):
            (directory / f"frame{index:03d}.rgb565").write_bytes(frame_data)

    def test_builds_expected_header_and_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            self.make_frames(root)
            output = root / "holo_frames.bin"
            stats = MODULE.build_image(root, output)
            image = output.read_bytes()
            header = struct.unpack(MODULE.HEADER_FORMAT,
                                   image[:MODULE.HEADER_BYTES])
            self.assertEqual(MODULE.MAGIC, header[0])
            self.assertEqual(MODULE.VERSION, header[1])
            self.assertEqual(MODULE.HEADER_BYTES, header[2])
            self.assertEqual(MODULE.FRAME_WIDTH, header[3])
            self.assertEqual(MODULE.FRAME_HEIGHT, header[4])
            self.assertEqual(MODULE.PIXEL_FORMAT_RGB565LE, header[5])
            self.assertEqual(MODULE.FRAME_COUNT, header[6])
            self.assertEqual(MODULE.FRAME_PERIOD_MS, header[7])
            self.assertEqual(MODULE.FRAME_COUNT * MODULE.FRAME_BYTES, header[8])
            self.assertEqual(zlib.crc32(image[MODULE.HEADER_BYTES:]), header[9])
            self.assertEqual(0, header[10])
            self.assertEqual(MODULE.HEADER_BYTES + header[8], len(image))
            self.assertEqual(len(image), stats["image_bytes"])

    def test_rejects_missing_or_extra_frame(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            self.make_frames(root, 48)
            with self.assertRaises(ValueError):
                MODULE.build_image(root, root / "missing.bin")
            (root / "frame048.rgb565").write_bytes(bytes(MODULE.FRAME_BYTES))
            (root / "frame049.rgb565").write_bytes(bytes(MODULE.FRAME_BYTES))
            with self.assertRaises(ValueError):
                MODULE.build_image(root, root / "extra.bin")

    def test_rejects_invalid_frame_size(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = pathlib.Path(temp_dir)
            self.make_frames(root)
            (root / "frame024.rgb565").write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                MODULE.build_image(root, root / "invalid.bin")


if __name__ == "__main__":
    unittest.main()
