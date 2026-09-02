from __future__ import annotations

import unittest
from pathlib import Path


SCRIPTS_DIR = Path(__file__).resolve().parents[1] / "scripts"
README_PATH = Path(__file__).resolve().parents[2] / "README.md"


class WindowsScriptsContractTest(unittest.TestCase):
    def _read_script(self, name: str) -> str:
        return (SCRIPTS_DIR / name).read_text(encoding="utf-8")

    def test_install_uses_current_user_limited_scheduled_task(self) -> None:
        script = self._read_script("install.ps1")

        self.assertIn("New-ScheduledTaskPrincipal", script)
        self.assertIn("-LogonType Interactive", script)
        self.assertIn("-RunLevel Limited", script)
        self.assertIn("-RestartInterval", script)
        self.assertNotIn("ServiceAccount", script)

    def test_install_runner_preserves_non_ascii_windows_paths(self) -> None:
        script = self._read_script("install.ps1")

        self.assertIn("New-Object System.Text.UTF8Encoding($true)", script)
        self.assertIn("Write-Utf8Bom -Path $runnerPath", script)

    def test_install_token_parser_does_not_depend_on_matches_state(self) -> None:
        script = self._read_script("install.ps1")

        self.assertNotIn("$Matches.token", script)
        self.assertIn(".Substring($prefix.Length)", script)

    def test_firewall_is_private_and_local_subnet_only(self) -> None:
        script = self._read_script("configure-firewall.ps1")

        self.assertIn("-Profile Private", script)
        self.assertIn("-RemoteAddress LocalSubnet", script)
        self.assertNotIn("-Profile Public", script)

    def test_uninstall_retains_runtime_state_token_and_logs(self) -> None:
        script = self._read_script("uninstall.ps1")

        self.assertIn("State, token, runtime, and logs were retained", script)
        self.assertNotIn("Remove-Item -LiteralPath $InstallDir", script)

    def test_readme_documents_macos_and_windows_operations(self) -> None:
        readme = README_PATH.read_text(encoding="utf-8")

        self.assertIn("## macOS 操作", readme)
        self.assertIn("## Windows 操作", readme)
        for script_name in (
            "install.sh",
            "uninstall.sh",
            "install.ps1",
            "status.ps1",
            "configure-firewall.ps1",
            "uninstall.ps1",
        ):
            self.assertIn(script_name, readme)
        self.assertIn("%USERPROFILE%\\.codex\\sessions", readme)
        self.assertIn("%LOCALAPPDATA%\\CodexTaskNotifierDemo", readme)


if __name__ == "__main__":
    unittest.main()
