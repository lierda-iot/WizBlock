"""Incremental reader for Codex session JSONL files."""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

from .model import TaskStore


MAX_ID_BYTES = 128


@dataclass(slots=True)
class FileCursor:
    device: int
    inode: int
    offset: int = 0
    pending: bytes = b""
    session_id: str | None = None
    current_turn_id: str | None = None


class CodexSessionObserver:
    def __init__(self, sessions_dir: Path, store: TaskStore) -> None:
        self.sessions_dir = sessions_dir
        self.store = store
        self._files: dict[Path, FileCursor] = {}
        self.invalid_event_count = 0
        self.invalid_json_count = 0
        self.io_error_count = 0

    def establish_baseline(self) -> None:
        for path in self._discover_files():
            cursor = self._new_cursor(path)
            if cursor is None:
                continue
            self._files[path] = cursor
            self._read_file(path, cursor, emit_notifications=False)

    def poll_once(self) -> bool:
        changed = False
        discovered = set(self._discover_files())
        for missing in set(self._files) - discovered:
            self._files.pop(missing, None)

        for path in sorted(discovered):
            try:
                stat = path.stat()
            except OSError:
                self.io_error_count += 1
                continue

            cursor = self._files.get(path)
            replaced = (
                cursor is not None
                and (cursor.device != stat.st_dev or cursor.inode != stat.st_ino)
            )
            truncated = cursor is not None and stat.st_size < cursor.offset
            if cursor is None:
                cursor = FileCursor(device=stat.st_dev, inode=stat.st_ino)
                self._files[path] = cursor
                changed = self._read_file(path, cursor, emit_notifications=True) or changed
            elif replaced or truncated:
                cursor = FileCursor(device=stat.st_dev, inode=stat.st_ino)
                self._files[path] = cursor
                changed = self._read_file(path, cursor, emit_notifications=False) or changed
            elif stat.st_size > cursor.offset:
                changed = self._read_file(path, cursor, emit_notifications=True) or changed
        return changed

    def export_state(self) -> dict[str, Any]:
        files = []
        for path, cursor in self._files.items():
            try:
                relative_path = path.relative_to(self.sessions_dir)
            except ValueError:
                continue
            files.append(
                {
                    "path": relative_path.as_posix(),
                    "device": cursor.device,
                    "inode": cursor.inode,
                    "offset": max(0, cursor.offset - len(cursor.pending)),
                    "session_id": cursor.session_id,
                    "current_turn_id": cursor.current_turn_id,
                }
            )
        return {"files": files}

    def restore_state(self, state: Any) -> None:
        if not isinstance(state, dict) or not isinstance(state.get("files"), list):
            raise ValueError("observer state must contain files")
        saved: dict[Path, dict[str, Any]] = {}
        for item in state["files"]:
            if not isinstance(item, dict):
                raise ValueError("file cursor must be an object")
            relative_value = item.get("path")
            if not isinstance(relative_value, str) or not relative_value:
                raise ValueError("invalid cursor path")
            relative_path = Path(relative_value)
            if relative_path.is_absolute() or ".." in relative_path.parts:
                raise ValueError("cursor path escapes sessions directory")
            device = self._state_uint(item.get("device"), "device")
            inode = self._state_uint(item.get("inode"), "inode")
            offset = self._state_uint(item.get("offset"), "offset")
            session_id = item.get("session_id")
            if session_id is not None and self._valid_id(session_id) is None:
                raise ValueError("invalid cursor session id")
            current_turn_id = item.get("current_turn_id")
            if current_turn_id is not None and self._valid_id(current_turn_id) is None:
                raise ValueError("invalid cursor current turn id")
            saved[self.sessions_dir / relative_path] = {
                "device": device,
                "inode": inode,
                "offset": offset,
                "session_id": session_id,
                "current_turn_id": current_turn_id,
            }

        restored: dict[Path, FileCursor] = {}
        for path in self._discover_files():
            try:
                stat = path.stat()
            except OSError:
                self.io_error_count += 1
                continue
            item = saved.get(path)
            if (
                item is not None
                and item["device"] == stat.st_dev
                and item["inode"] == stat.st_ino
                and item["offset"] <= stat.st_size
            ):
                restored[path] = FileCursor(
                    device=stat.st_dev,
                    inode=stat.st_ino,
                    offset=item["offset"],
                    session_id=item["session_id"],
                    current_turn_id=item["current_turn_id"],
                )
                continue

            cursor = FileCursor(device=stat.st_dev, inode=stat.st_ino)
            restored[path] = cursor
            self._read_file(path, cursor, emit_notifications=False)
        self._files = restored

    def _discover_files(self) -> list[Path]:
        if not self.sessions_dir.is_dir():
            return []
        try:
            return sorted(path for path in self.sessions_dir.rglob("*.jsonl") if path.is_file())
        except OSError:
            self.io_error_count += 1
            return []

    def _new_cursor(self, path: Path) -> FileCursor | None:
        try:
            stat = path.stat()
        except OSError:
            self.io_error_count += 1
            return None
        return FileCursor(device=stat.st_dev, inode=stat.st_ino)

    def _read_file(self, path: Path, cursor: FileCursor, *, emit_notifications: bool) -> bool:
        try:
            with path.open("rb") as handle:
                handle.seek(cursor.offset)
                chunk = handle.read()
                cursor.offset = handle.tell()
        except OSError:
            self.io_error_count += 1
            return False

        data = cursor.pending + chunk
        lines = data.split(b"\n")
        cursor.pending = lines.pop()
        changed = False
        for raw_line in lines:
            if not raw_line.strip():
                continue
            try:
                item = json.loads(raw_line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                self.invalid_json_count += 1
                continue
            changed = self._consume_item(cursor, item, emit_notifications) or changed
        return changed

    def _consume_item(
        self, cursor: FileCursor, item: Any, emit_notifications: bool
    ) -> bool:
        if not isinstance(item, dict):
            self.invalid_json_count += 1
            return False
        item_type = item.get("type")
        payload = item.get("payload")
        if not isinstance(payload, dict):
            return False

        changed = False
        occurred_at_ms = self._timestamp_ms(item.get("timestamp"))
        if (
            cursor.session_id is not None
            and cursor.current_turn_id is not None
            and occurred_at_ms is not None
        ):
            changed = self.store.touch_task(
                cursor.session_id, cursor.current_turn_id, occurred_at_ms
            )

        if item_type == "session_meta":
            session_id = self._valid_id(payload.get("session_id") or payload.get("id"))
            if session_id is None:
                self.invalid_event_count += 1
                return False
            originator = payload.get("originator")
            cwd = payload.get("cwd")
            cursor.session_id = session_id
            self.store.register_session(
                session_id,
                originator if isinstance(originator, str) else "",
                cwd if isinstance(cwd, str) else "",
            )
            return changed

        session_id = cursor.session_id
        if session_id is None:
            if item_type in ("turn_context", "event_msg"):
                self.invalid_event_count += 1
            return changed

        if item_type == "turn_context":
            turn_id = self._valid_id(payload.get("turn_id"))
            cwd = payload.get("cwd")
            if turn_id is None or not isinstance(cwd, str):
                self.invalid_event_count += 1
                return False
            self.store.register_turn_context(session_id, turn_id, cwd)
            return False

        if item_type != "event_msg":
            return changed

        event_type = payload.get("type")
        if event_type == "user_message":
            message = payload.get("message")
            if cursor.current_turn_id is None or not isinstance(message, str):
                self.invalid_event_count += 1
                return changed
            return self.store.register_task_title(
                session_id, cursor.current_turn_id, message
            ) or changed
        if event_type not in ("task_started", "task_complete", "turn_aborted"):
            return changed
        turn_id = self._valid_id(payload.get("turn_id"))
        if turn_id is None or occurred_at_ms is None:
            self.invalid_event_count += 1
            return changed

        if event_type == "task_started":
            cursor.current_turn_id = turn_id
            return self.store.start_task(session_id, turn_id, occurred_at_ms) or changed
        if event_type == "task_complete":
            duration = payload.get("duration_ms")
            duration_ms = duration if isinstance(duration, int) and not isinstance(duration, bool) else None
            completed = self.store.complete_task(
                session_id,
                turn_id,
                occurred_at_ms,
                duration_ms=duration_ms,
                emit_event=emit_notifications,
            )
            if cursor.current_turn_id == turn_id:
                cursor.current_turn_id = None
            return completed or changed
        stopped = self.store.stop_task(
            session_id,
            turn_id,
            occurred_at_ms,
            emit_event=emit_notifications,
        )
        if cursor.current_turn_id == turn_id:
            cursor.current_turn_id = None
        return stopped or changed

    @staticmethod
    def _valid_id(value: Any) -> str | None:
        if not isinstance(value, str) or not value.strip():
            return None
        if len(value.encode("utf-8")) > MAX_ID_BYTES:
            return None
        return value

    @staticmethod
    def _timestamp_ms(value: Any) -> int | None:
        if not isinstance(value, str):
            return None
        try:
            parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError:
            return None
        if parsed.tzinfo is None:
            return None
        return max(0, int(parsed.timestamp() * 1000))

    @staticmethod
    def _state_uint(value: Any, name: str) -> int:
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{name} must be an unsigned integer")
        return value
