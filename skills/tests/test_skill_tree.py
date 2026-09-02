#!/usr/bin/env python3
"""Public package-level tests for the OPEN Skill tree."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


SKILLS_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SKILLS_ROOT / "scripts"))

from validate_skill_tree import validate_skill_tree  # noqa: E402


class SkillTreeTest(unittest.TestCase):
    def test_all_skills_are_discoverable_and_independently_packaged(self) -> None:
        self.assertEqual(validate_skill_tree(SKILLS_ROOT), [])


if __name__ == "__main__":
    unittest.main()
