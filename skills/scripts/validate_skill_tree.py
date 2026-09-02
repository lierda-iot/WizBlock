#!/usr/bin/env python3
"""Validate that the public OPEN Skill tree is complete and portable."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


EXPECTED_SKILLS = (
    "open-dev-all",
    "open-preflight",
    "open-driver",
    "open-git",
    "open-idf",
    "open-clone",
    "open-components",
    "open-first-example",
    "open-build",
    "open-flash",
    "open-monitor",
    "open-diagnose",
)
REQUIRED_SHARED_FILES = (
    "README.md",
    "README.en.md",
    "references/status-contract.md",
    "references/workflow-contract.md",
    "references/platform-contract.md",
    "references/output-templates.md",
    "scripts/open_skill_runtime.py",
    "scripts/run_skill_validation.py",
)
_WINDOWS_HOME_PATTERN = r"[A-Za-z]:" + r"\\"
_MACOS_HOME_PATTERN = "/" + "Users" + r"/[^/\s]+"
ABSOLUTE_PATH = re.compile(f"(?:{_WINDOWS_HOME_PATTERN}|{_MACOS_HOME_PATTERN})")
FIXED_PORT = re.compile(r"(?:COM\d+|usbserial-\d+)", re.IGNORECASE)


def _frontmatter(text: str) -> dict[str, str]:
    lines = text.splitlines()
    if len(lines) < 4 or lines[0].strip() != "---":
        return {}
    try:
        end = lines.index("---", 1)
    except ValueError:
        return {}
    values: dict[str, str] = {}
    for line in lines[1:end]:
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        values[key.strip()] = value.strip()
    return values


def validate_skill_tree(root: Path) -> list[str]:
    """Return portable-package errors; an empty list means PASS."""

    errors: list[str] = []
    for relative in REQUIRED_SHARED_FILES:
        if not (root / relative).is_file():
            errors.append(f"missing shared file: {relative}")

    actual = sorted(
        path.name
        for path in root.iterdir()
        if path.is_dir() and path.name.startswith("open-")
    )
    expected = sorted(EXPECTED_SKILLS)
    if actual != expected:
        errors.append(f"skill directory mismatch: expected={expected} actual={actual}")

    for name in EXPECTED_SKILLS:
        skill_file = root / name / "SKILL.md"
        if not skill_file.is_file():
            errors.append(f"missing SKILL.md: {name}")
            continue
        text = skill_file.read_text(encoding="utf-8")
        metadata = _frontmatter(text)
        if metadata.get("name") != name:
            errors.append(f"frontmatter name mismatch: {name}")
        if not metadata.get("description"):
            errors.append(f"missing description: {name}")
        if "../references/status-contract.md" not in text:
            errors.append(f"status contract is not discoverable: {name}")
        if ABSOLUTE_PATH.search(text):
            errors.append(f"absolute maintainer path: {name}")
        if FIXED_PORT.search(text):
            errors.append(f"fixed serial port: {name}")
        if "TODO" in text or "Replace with" in text:
            errors.append(f"unfinished scaffold marker: {name}")
    return sorted(errors)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors = validate_skill_tree(args.root.resolve())
    if errors:
        for error in errors:
            print(error)
        return 1
    print(f"skills={len(EXPECTED_SKILLS)} errors=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
