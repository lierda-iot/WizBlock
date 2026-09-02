"""Platform-native default paths for the Codex task bridge."""

from __future__ import annotations

import os
import sys
from collections.abc import Mapping
from pathlib import Path


def default_sessions_dir(*, home: Path | None = None) -> Path:
    effective_home = Path.home() if home is None else home
    return effective_home / ".codex" / "sessions"


def resolve_default_state_file(
    *,
    platform_name: str,
    home: Path,
    environment: Mapping[str, str],
) -> Path:
    if platform_name == "win32":
        local_app_data = environment.get("LOCALAPPDATA", "").strip()
        state_root = (
            Path(local_app_data)
            if local_app_data
            else home / "AppData" / "Local"
        )
        return state_root / "CodexTaskNotifierDemo" / "bridge-state.json"

    if platform_name == "darwin":
        return (
            home
            / "Library"
            / "Application Support"
            / "codex_task_notifier_demo"
            / "bridge-state.json"
        )

    xdg_state_home = environment.get("XDG_STATE_HOME", "").strip()
    state_root = Path(xdg_state_home) if xdg_state_home else home / ".local" / "state"
    return state_root / "codex_task_notifier_demo" / "bridge-state.json"


def default_state_file() -> Path:
    return resolve_default_state_file(
        platform_name=sys.platform,
        home=Path.home(),
        environment=os.environ,
    )
