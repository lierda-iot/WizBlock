"""Atomic persistence for the bridge task model and JSONL cursors."""

from __future__ import annotations

import json
import os
import time
from pathlib import Path
from typing import Any

from .model import TaskStore
from .observer import CodexSessionObserver


STATE_VERSION = 1


class BridgeStatePersistence:
    def __init__(self, path: Path) -> None:
        self.path = path

    def save(self, store: TaskStore, observer: CodexSessionObserver) -> None:
        payload = {
            "version": STATE_VERSION,
            "store": store.export_state(),
            "observer": observer.export_state(),
        }
        encoded = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = self.path.with_name(f".{self.path.name}.tmp")
        try:
            with temporary_path.open("wb") as handle:
                os.chmod(temporary_path, 0o600)
                handle.write(encoded)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary_path, self.path)
        finally:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass

    def restore(self, store: TaskStore, observer: CodexSessionObserver) -> bool:
        if not self.path.exists():
            return False
        try:
            raw_payload: Any = json.loads(self.path.read_text(encoding="utf-8"))
            if not isinstance(raw_payload, dict):
                raise ValueError("state root must be an object")
            if raw_payload.get("version") != STATE_VERSION:
                raise ValueError("unsupported state version")
            temporary_store = TaskStore()
            temporary_observer = CodexSessionObserver(observer.sessions_dir, temporary_store)
            temporary_store.restore_state(raw_payload.get("store"))
            temporary_observer.restore_state(raw_payload.get("observer"))
            store.restore_state(temporary_store.export_state())
            observer.restore_state(temporary_observer.export_state())
            return True
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError, TypeError):
            self._quarantine()
            return False

    def _quarantine(self) -> None:
        if not self.path.exists():
            return
        suffix = int(time.time() * 1000)
        corrupt_path = self.path.with_name(f"{self.path.name}.corrupt-{suffix}")
        try:
            os.replace(self.path, corrupt_path)
        except OSError:
            return
