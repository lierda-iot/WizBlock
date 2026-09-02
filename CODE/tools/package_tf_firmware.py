#!/usr/bin/env python3
"""Create one offline TF firmware package for tf_firmware_launcher_demo."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path


BOARD = "laiwfs300"
PARTITION_SCHEME = "demo-hub-v2"
OTA_PARTITION_SIZE = 0x440000
RESOURCE_PARTITION_LIMITS = {
    "model": 0x100000,
    "spiffs_data": 0x40000,
}
SAFE_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
DEMO_ID_MAX_BYTES = 47
VERSION_MAX_BYTES = 23
DISPLAY_NAME_MAX_BYTES = 63


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-bin", required=True, type=Path, help="ESP-IDF app.bin path")
    parser.add_argument("--id", required=True, help="Stable demo identifier")
    parser.add_argument("--name", required=True, help="Display name")
    parser.add_argument("--version", required=True, help="Package version")
    parser.add_argument("--output", required=True, type=Path, help="TF card root directory")
    parser.add_argument("--assets-dir", type=Path, help="Optional assets directory")
    parser.add_argument("--model-dir", type=Path, help="Optional model directory")
    parser.add_argument(
        "--partition-image",
        action="append",
        default=[],
        metavar="LABEL=PATH",
        help="Optional data partition image (model or spiffs_data)",
    )
    return parser.parse_args()


def validate_name(value: str, field_name: str, max_bytes: int) -> None:
    if not SAFE_NAME_PATTERN.fullmatch(value):
        raise ValueError(f"{field_name} must match {SAFE_NAME_PATTERN.pattern}: {value!r}")
    if len(value.encode("utf-8")) > max_bytes:
        raise ValueError(f"{field_name} exceeds {max_bytes} UTF-8 bytes")


def validate_display_name(value: str) -> None:
    encoded = value.encode("utf-8")
    if not encoded or len(encoded) > DISPLAY_NAME_MAX_BYTES:
        raise ValueError(
            f"name must contain 1..{DISPLAY_NAME_MAX_BYTES} UTF-8 bytes"
        )
    if any(ord(character) < 32 or ord(character) == 127 for character in value):
        raise ValueError("name must not contain control characters")


def calculate_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_optional_directory(source: Path | None, destination: Path) -> None:
    if source is None:
        return
    if not source.is_dir():
        raise FileNotFoundError(f"directory not found: {source}")
    shutil.copytree(source, destination)


def parse_partition_images(values: list[str]) -> list[tuple[str, Path]]:
    images: list[tuple[str, Path]] = []
    labels: set[str] = set()
    for value in values:
        label, separator, path_text = value.partition("=")
        if not separator or label not in RESOURCE_PARTITION_LIMITS or not path_text:
            allowed = ", ".join(sorted(RESOURCE_PARTITION_LIMITS))
            raise ValueError(
                f"partition image must be LABEL=PATH with LABEL in {{{allowed}}}: {value!r}"
            )
        if label in labels:
            raise ValueError(f"duplicate partition image label: {label}")

        source = Path(path_text).resolve()
        if not source.is_file():
            raise FileNotFoundError(f"partition image not found: {source}")
        size = source.stat().st_size
        limit = RESOURCE_PARTITION_LIMITS[label]
        if size <= 0 or size > limit:
            raise ValueError(
                f"{label} image size {size} is outside the partition limit 1..{limit}"
            )
        labels.add(label)
        images.append((label, source))
    return images


def main() -> int:
    args = parse_args()
    validate_name(args.id, "id", DEMO_ID_MAX_BYTES)
    validate_name(args.version, "version", VERSION_MAX_BYTES)
    validate_display_name(args.name)
    partition_images = parse_partition_images(args.partition_image)

    app_bin = args.app_bin.resolve()
    if not app_bin.is_file():
        raise FileNotFoundError(f"app binary not found: {app_bin}")

    app_size = app_bin.stat().st_size
    if app_size <= 0 or app_size > OTA_PARTITION_SIZE:
        raise ValueError(
            f"app size {app_size} is outside the ota_0 limit 1..{OTA_PARTITION_SIZE}"
        )

    package_dir = (
        args.output.resolve()
        / "demo_hub"
        / "packages"
        / args.id
        / args.version
    )
    if package_dir.exists():
        raise FileExistsError(f"package already exists: {package_dir}")

    package_dir.mkdir(parents=True)
    try:
        packaged_app = package_dir / "app.bin"
        shutil.copy2(app_bin, packaged_app)
        copy_optional_directory(args.assets_dir, package_dir / "assets")
        copy_optional_directory(args.model_dir, package_dir / "model")

        packaged_partitions = []
        for label, source in partition_images:
            file_name = f"{label}.bin"
            packaged_image = package_dir / file_name
            shutil.copy2(source, packaged_image)
            packaged_partitions.append(
                {
                    "label": label,
                    "file": file_name,
                    "size": packaged_image.stat().st_size,
                    "sha256": calculate_sha256(packaged_image),
                }
            )

        manifest = {
            "id": args.id,
            "name": args.name,
            "version": args.version,
            "board": BOARD,
            "partition_scheme": PARTITION_SCHEME,
            "app": "app.bin",
            "app_size": app_size,
            "app_sha256": calculate_sha256(packaged_app),
            "partitions": packaged_partitions,
        }
        manifest_path = package_dir / "manifest.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, ensure_ascii=True) + "\n",
            encoding="utf-8",
        )
        (package_dir / "READY").write_bytes(b"")
    except Exception:
        shutil.rmtree(package_dir, ignore_errors=True)
        raise

    print(f"Package created: {package_dir}")
    print(f"Size: {app_size} bytes")
    print(f"SHA-256: {manifest['app_sha256']}")
    for partition in manifest["partitions"]:
        print(
            f"Partition {partition['label']}: {partition['size']} bytes, "
            f"SHA-256 {partition['sha256']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
