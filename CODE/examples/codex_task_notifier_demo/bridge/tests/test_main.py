from __future__ import annotations

import unittest

from codex_task_bridge.__main__ import validate_token


class TokenValidationTest(unittest.TestCase):
    def test_valid_visible_ascii_token_is_accepted(self) -> None:
        token = "0123456789abcdef0123456789abcdef"

        self.assertEqual(validate_token(token), token)

    def test_invalid_tokens_are_rejected(self) -> None:
        invalid_tokens = [
            "x" * 31,
            "x" * 129,
            "x" * 31 + " ",
            "x" * 31 + "\n",
            "x" * 31 + "中",
            "CHANGE_ME_" + "x" * 23,
        ]
        for token in invalid_tokens:
            with self.subTest(token=repr(token)), self.assertRaises(ValueError):
                validate_token(token)


if __name__ == "__main__":
    unittest.main()
