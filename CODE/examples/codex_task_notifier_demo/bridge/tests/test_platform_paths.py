from __future__ import annotations

import unittest
from pathlib import Path

from codex_task_bridge.platform_paths import (
    default_sessions_dir,
    resolve_default_state_file,
)


class PlatformPathsTest(unittest.TestCase):
    def test_sessions_directory_is_under_current_user_home(self) -> None:
        home = Path("/users/tester")

        self.assertEqual(
            default_sessions_dir(home=home),
            home / ".codex" / "sessions",
        )

    def test_windows_state_uses_local_app_data(self) -> None:
        state_file = resolve_default_state_file(
            platform_name="win32",
            home=Path("/users/tester"),
            environment={"LOCALAPPDATA": "/windows/local"},
        )

        self.assertEqual(
            state_file,
            Path("/windows/local")
            / "CodexTaskNotifierDemo"
            / "bridge-state.json",
        )

    def test_windows_state_falls_back_to_user_profile(self) -> None:
        home = Path("/users/tester")

        state_file = resolve_default_state_file(
            platform_name="win32",
            home=home,
            environment={},
        )

        self.assertEqual(
            state_file,
            home
            / "AppData"
            / "Local"
            / "CodexTaskNotifierDemo"
            / "bridge-state.json",
        )

    def test_macos_and_xdg_state_locations_remain_platform_native(self) -> None:
        home = Path("/users/tester")

        mac_state = resolve_default_state_file(
            platform_name="darwin",
            home=home,
            environment={},
        )
        linux_state = resolve_default_state_file(
            platform_name="linux",
            home=home,
            environment={"XDG_STATE_HOME": "/state"},
        )

        self.assertEqual(
            mac_state,
            home
            / "Library"
            / "Application Support"
            / "codex_task_notifier_demo"
            / "bridge-state.json",
        )
        self.assertEqual(
            linux_state,
            Path("/state")
            / "codex_task_notifier_demo"
            / "bridge-state.json",
        )


if __name__ == "__main__":
    unittest.main()
