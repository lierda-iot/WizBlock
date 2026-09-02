"""Thread-safe task and completion-event model for the local bridge."""

from __future__ import annotations

import hashlib
import threading
import unicodedata
from collections import deque
from dataclasses import dataclass
from enum import Enum
from typing import Any


MAX_VISIBLE_TASKS = 12
MAX_EVENTS = 32
MAX_TASK_TITLE_BYTES = 72
DONE_RETENTION_MS = 600_000
STOP_RETENTION_MS = 180_000
RUN_STALE_TIMEOUT_MS = 86_400_000
NOTIFY_MIN_DURATION_MS = 30_000
MAX_ELAPSED_MS = 4_294_967_295
MAX_EVENT_SEQ = 18_446_744_073_709_551_615


class TaskStatus(str, Enum):
    RUN = "RUN"
    DONE = "DONE"
    STOP = "STOP"
    UNKNOWN = "UNKNOWN"


@dataclass(slots=True)
class SessionRecord:
    originator: str
    cwd: str


@dataclass(slots=True)
class TaskRecord:
    session_id: str
    turn_id: str
    task_id: str
    surface: str
    project: str
    title: str
    title_set: bool
    status: TaskStatus
    started_at_ms: int | None
    updated_at_ms: int
    elapsed_ms: int


@dataclass(slots=True)
class TaskEvent:
    seq: int
    task_id: str
    event_type: str
    notify: bool
    occurred_at_ms: int


def _truncate_utf8(value: str, max_bytes: int) -> str:
    encoded = value.encode("utf-8")
    if len(encoded) <= max_bytes:
        return value
    return encoded[:max_bytes].decode("utf-8", errors="ignore")


def _project_from_cwd(cwd: str) -> str:
    normalized = cwd.strip().rstrip("/\\")
    if not normalized:
        return "unknown"
    # Codex logs can originate on either host platform.
    leaf = normalized.replace("\\", "/").rsplit("/", 1)[-1].strip()
    sanitized = "".join(
        character if ord(character) >= 0x20 and ord(character) != 0x7F else "_"
        for character in leaf
    )
    return _truncate_utf8(sanitized or "unknown", 48)


def _surface_from_originator(originator: str) -> str:
    if originator == "Codex Desktop":
        return "APP"
    if originator == "codex_vscode":
        return "VS"
    return "CODEX"


def _title_from_user_message(message: str) -> str:
    sanitized = "".join(
        " " if character.isspace() or unicodedata.category(character).startswith("C")
        else character
        for character in message
    )
    collapsed = " ".join(sanitized.split())
    return _truncate_utf8(collapsed or "Untitled task", MAX_TASK_TITLE_BYTES)


def _public_task_id(session_id: str, turn_id: str) -> str:
    material = f"{session_id}\0{turn_id}".encode("utf-8")
    return hashlib.sha256(material).hexdigest()[:24]


class TaskStore:
    """Own all observed tasks and provide immutable protocol snapshots."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._sessions: dict[str, SessionRecord] = {}
        self._turn_cwds: dict[tuple[str, str], str] = {}
        self._tasks: dict[tuple[str, str], TaskRecord] = {}
        self._events: deque[TaskEvent] = deque(maxlen=MAX_EVENTS)
        self._event_seq = 0
        self._stale_run_pruned_count = 0

    def register_session(self, session_id: str, originator: str, cwd: str) -> None:
        with self._lock:
            self._sessions[session_id] = SessionRecord(originator=originator, cwd=cwd)

    def register_turn_context(self, session_id: str, turn_id: str, cwd: str) -> None:
        with self._lock:
            self._turn_cwds[(session_id, turn_id)] = cwd
            task = self._tasks.get((session_id, turn_id))
            if task is not None:
                task.project = _project_from_cwd(cwd)
                if not task.title_set:
                    task.title = task.project

    def start_task(self, session_id: str, turn_id: str, occurred_at_ms: int) -> bool:
        key = (session_id, turn_id)
        with self._lock:
            existing = self._tasks.get(key)
            if existing is not None:
                if existing.status is TaskStatus.RUN:
                    previous = existing.updated_at_ms
                    existing.updated_at_ms = max(existing.updated_at_ms, occurred_at_ms)
                    return existing.updated_at_ms != previous
                return False

            session = self._sessions.get(session_id, SessionRecord("", ""))
            cwd = self._turn_cwds.get(key, session.cwd)
            project = _project_from_cwd(cwd)
            self._tasks[key] = TaskRecord(
                session_id=session_id,
                turn_id=turn_id,
                task_id=_public_task_id(session_id, turn_id),
                surface=_surface_from_originator(session.originator),
                project=project,
                title=project,
                title_set=False,
                status=TaskStatus.RUN,
                started_at_ms=occurred_at_ms,
                updated_at_ms=occurred_at_ms,
                elapsed_ms=0,
            )
            return True

    def register_task_title(self, session_id: str, turn_id: str, message: str) -> bool:
        key = (session_id, turn_id)
        with self._lock:
            task = self._tasks.get(key)
            if task is None or task.title_set:
                return False
            task.title = _title_from_user_message(message)
            task.title_set = True
            return True

    def touch_task(self, session_id: str, turn_id: str, occurred_at_ms: int) -> bool:
        key = (session_id, turn_id)
        with self._lock:
            task = self._tasks.get(key)
            if task is None or task.status is not TaskStatus.RUN:
                return False
            previous = task.updated_at_ms
            task.updated_at_ms = max(task.updated_at_ms, occurred_at_ms)
            return task.updated_at_ms != previous

    def stale_run_pruned_count(self) -> int:
        with self._lock:
            return self._stale_run_pruned_count

    def complete_task(
        self,
        session_id: str,
        turn_id: str,
        occurred_at_ms: int,
        *,
        duration_ms: int | None = None,
        emit_event: bool = True,
    ) -> bool:
        key = (session_id, turn_id)
        with self._lock:
            task = self._tasks.get(key)
            if task is not None and task.status in (TaskStatus.DONE, TaskStatus.STOP):
                return False

            had_start = task is not None and task.started_at_ms is not None
            if task is None:
                session = self._sessions.get(session_id, SessionRecord("", ""))
                cwd = self._turn_cwds.get(key, session.cwd)
                project = _project_from_cwd(cwd)
                task = TaskRecord(
                    session_id=session_id,
                    turn_id=turn_id,
                    task_id=_public_task_id(session_id, turn_id),
                    surface=_surface_from_originator(session.originator),
                    project=project,
                    title=project,
                    title_set=False,
                    status=TaskStatus.UNKNOWN,
                    started_at_ms=None,
                    updated_at_ms=occurred_at_ms,
                    elapsed_ms=0,
                )
                self._tasks[key] = task

            elapsed_ms = self._completion_duration(task, occurred_at_ms, duration_ms)
            task.status = TaskStatus.DONE
            task.updated_at_ms = occurred_at_ms
            task.elapsed_ms = elapsed_ms
            if emit_event:
                self._append_event(
                    task,
                    "TURN_COMPLETED",
                    had_start and elapsed_ms >= NOTIFY_MIN_DURATION_MS,
                    occurred_at_ms,
                )
            return True

    def stop_task(
        self,
        session_id: str,
        turn_id: str,
        occurred_at_ms: int,
        *,
        emit_event: bool = True,
    ) -> bool:
        key = (session_id, turn_id)
        with self._lock:
            task = self._tasks.get(key)
            if task is not None and task.status in (TaskStatus.DONE, TaskStatus.STOP):
                return False

            if task is None:
                session = self._sessions.get(session_id, SessionRecord("", ""))
                cwd = self._turn_cwds.get(key, session.cwd)
                project = _project_from_cwd(cwd)
                task = TaskRecord(
                    session_id=session_id,
                    turn_id=turn_id,
                    task_id=_public_task_id(session_id, turn_id),
                    surface=_surface_from_originator(session.originator),
                    project=project,
                    title=project,
                    title_set=False,
                    status=TaskStatus.UNKNOWN,
                    started_at_ms=None,
                    updated_at_ms=occurred_at_ms,
                    elapsed_ms=0,
                )
                self._tasks[key] = task

            task.elapsed_ms = self._completion_duration(task, occurred_at_ms, None)
            task.status = TaskStatus.STOP
            task.updated_at_ms = occurred_at_ms
            if emit_event:
                self._append_event(task, "TURN_STOPPED", False, occurred_at_ms)
            return True

    def snapshot(self, after_event_seq: int, now_ms: int) -> dict[str, Any]:
        with self._lock:
            self._prune(now_ms)
            ordered = sorted(
                self._tasks.values(),
                key=lambda task: (
                    self._status_priority(task.status),
                    -task.updated_at_ms,
                    task.task_id,
                ),
            )
            visible = ordered[:MAX_VISIBLE_TASKS]
            counts = {status: 0 for status in TaskStatus}
            for task in ordered:
                counts[task.status] += 1

            oldest_seq = self._events[0].seq if self._events else self._event_seq + 1
            events_truncated = bool(self._events) and after_event_seq < (oldest_seq - 1)
            events = [event for event in self._events if event.seq > after_event_seq]
            running_count = counts[TaskStatus.RUN]
            return {
                "schema_version": 1,
                "generated_at_ms": max(0, now_ms),
                "bridge_status": "ONLINE",
                "aggregate": {
                    "state": "RUNNING" if running_count else "IDLE",
                    "total_count": len(ordered),
                    "running_count": running_count,
                    "done_count": counts[TaskStatus.DONE],
                    "stop_count": counts[TaskStatus.STOP],
                    "overflow_count": max(0, len(ordered) - len(visible)),
                },
                "tasks": [self._task_payload(task, now_ms) for task in visible],
                "events": [self._event_payload(event) for event in events],
                "events_truncated": events_truncated,
            }

    def export_state(self) -> dict[str, Any]:
        with self._lock:
            return {
                "event_seq": self._event_seq,
                "sessions": [
                    {
                        "session_id": session_id,
                        "originator": session.originator,
                        "cwd": session.cwd,
                    }
                    for session_id, session in self._sessions.items()
                ],
                "turn_cwds": [
                    {"session_id": key[0], "turn_id": key[1], "cwd": cwd}
                    for key, cwd in self._turn_cwds.items()
                ],
                "tasks": [
                    {
                        "session_id": task.session_id,
                        "turn_id": task.turn_id,
                        "surface": task.surface,
                        "project": task.project,
                        "title": task.title,
                        "title_set": task.title_set,
                        "status": task.status.value,
                        "started_at_ms": task.started_at_ms,
                        "updated_at_ms": task.updated_at_ms,
                        "elapsed_ms": task.elapsed_ms,
                    }
                    for task in self._tasks.values()
                ],
                "events": [self._event_payload(event) for event in self._events],
            }

    def restore_state(self, state: Any) -> None:
        if not isinstance(state, dict):
            raise ValueError("store state must be an object")
        event_seq = self._state_uint(state.get("event_seq"), "event_seq")
        if event_seq > MAX_EVENT_SEQ:
            raise ValueError("event_seq exceeds uint64")
        sessions_payload = self._state_list(state.get("sessions"), "sessions")
        turn_cwds_payload = self._state_list(state.get("turn_cwds"), "turn_cwds")
        tasks_payload = self._state_list(state.get("tasks"), "tasks")
        events_payload = self._state_list(state.get("events"), "events")
        if len(events_payload) > MAX_EVENTS:
            raise ValueError("too many persisted events")

        sessions: dict[str, SessionRecord] = {}
        for item in sessions_payload:
            item = self._state_object(item, "session")
            session_id = self._state_id(item.get("session_id"), "session_id")
            originator = self._state_string(item.get("originator"), "originator")
            cwd = self._state_string(item.get("cwd"), "cwd")
            if session_id in sessions:
                raise ValueError("duplicate session")
            sessions[session_id] = SessionRecord(originator=originator, cwd=cwd)

        turn_cwds: dict[tuple[str, str], str] = {}
        for item in turn_cwds_payload:
            item = self._state_object(item, "turn context")
            key = (
                self._state_id(item.get("session_id"), "session_id"),
                self._state_id(item.get("turn_id"), "turn_id"),
            )
            if key in turn_cwds:
                raise ValueError("duplicate turn context")
            turn_cwds[key] = self._state_string(item.get("cwd"), "cwd")

        tasks: dict[tuple[str, str], TaskRecord] = {}
        for item in tasks_payload:
            item = self._state_object(item, "task")
            session_id = self._state_id(item.get("session_id"), "session_id")
            turn_id = self._state_id(item.get("turn_id"), "turn_id")
            key = (session_id, turn_id)
            if key in tasks:
                raise ValueError("duplicate task")
            started_at_ms = item.get("started_at_ms")
            if started_at_ms is not None:
                started_at_ms = self._state_uint(started_at_ms, "started_at_ms")
            try:
                status = TaskStatus(item.get("status"))
            except (TypeError, ValueError) as error:
                raise ValueError("invalid task status") from error
            surface = self._state_string(item.get("surface"), "surface")
            if surface not in ("APP", "VS", "CODEX"):
                raise ValueError("invalid task surface")
            project = self._state_string(item.get("project"), "project")
            if not project or len(project.encode("utf-8")) > 48:
                raise ValueError("invalid project")
            title = self._state_string(item.get("title", project), "title")
            if not title or len(title.encode("utf-8")) > MAX_TASK_TITLE_BYTES:
                raise ValueError("invalid title")
            title_set = item.get("title_set", False)
            if not isinstance(title_set, bool):
                raise ValueError("invalid title_set")
            tasks[key] = TaskRecord(
                session_id=session_id,
                turn_id=turn_id,
                task_id=_public_task_id(session_id, turn_id),
                surface=surface,
                project=project,
                title=title,
                title_set=title_set,
                status=status,
                started_at_ms=started_at_ms,
                updated_at_ms=self._state_uint(item.get("updated_at_ms"), "updated_at_ms"),
                elapsed_ms=min(
                    self._state_uint(item.get("elapsed_ms"), "elapsed_ms"), MAX_ELAPSED_MS
                ),
            )

        events: deque[TaskEvent] = deque(maxlen=MAX_EVENTS)
        previous_seq = 0
        for item in events_payload:
            item = self._state_object(item, "event")
            seq = self._state_uint(item.get("seq"), "seq")
            if seq <= previous_seq or seq == 0 or seq > event_seq:
                raise ValueError("invalid event sequence")
            event_type = self._state_string(item.get("type"), "event type")
            if event_type not in ("TURN_COMPLETED", "TURN_STOPPED"):
                raise ValueError("invalid event type")
            notify = item.get("notify")
            if not isinstance(notify, bool):
                raise ValueError("invalid event notify flag")
            task_id = self._state_string(item.get("task_id"), "task_id")
            if not task_id or len(task_id.encode("ascii", errors="ignore")) != len(task_id):
                raise ValueError("invalid public task id")
            events.append(
                TaskEvent(
                    seq=seq,
                    task_id=task_id,
                    event_type=event_type,
                    notify=notify,
                    occurred_at_ms=self._state_uint(
                        item.get("occurred_at_ms"), "occurred_at_ms"
                    ),
                )
            )
            previous_seq = seq

        with self._lock:
            self._sessions = sessions
            self._turn_cwds = turn_cwds
            self._tasks = tasks
            self._events = events
            self._event_seq = event_seq
            self._stale_run_pruned_count = 0

    @staticmethod
    def _completion_duration(
        task: TaskRecord, occurred_at_ms: int, duration_ms: int | None
    ) -> int:
        if duration_ms is not None and duration_ms >= 0:
            elapsed_ms = duration_ms
        elif task.started_at_ms is not None:
            elapsed_ms = max(0, occurred_at_ms - task.started_at_ms)
        else:
            elapsed_ms = 0
        return min(elapsed_ms, MAX_ELAPSED_MS)

    def _append_event(
        self,
        task: TaskRecord,
        event_type: str,
        notify: bool,
        occurred_at_ms: int,
    ) -> None:
        if self._event_seq >= MAX_EVENT_SEQ:
            return
        self._event_seq += 1
        self._events.append(
            TaskEvent(
                seq=self._event_seq,
                task_id=task.task_id,
                event_type=event_type,
                notify=notify,
                occurred_at_ms=occurred_at_ms,
            )
        )

    def _prune(self, now_ms: int) -> None:
        expired: list[tuple[str, str]] = []
        for key, task in self._tasks.items():
            age_ms = max(0, now_ms - task.updated_at_ms)
            if task.status is TaskStatus.DONE and age_ms > DONE_RETENTION_MS:
                expired.append(key)
            elif task.status is TaskStatus.STOP and age_ms > STOP_RETENTION_MS:
                expired.append(key)
            elif task.status is TaskStatus.RUN and age_ms >= RUN_STALE_TIMEOUT_MS:
                expired.append(key)
                self._stale_run_pruned_count += 1
        for key in expired:
            self._tasks.pop(key, None)
            self._turn_cwds.pop(key, None)

    @staticmethod
    def _status_priority(status: TaskStatus) -> int:
        return {
            TaskStatus.RUN: 0,
            TaskStatus.UNKNOWN: 1,
            TaskStatus.DONE: 2,
            TaskStatus.STOP: 3,
        }[status]

    @staticmethod
    def _task_payload(task: TaskRecord, now_ms: int) -> dict[str, Any]:
        elapsed_ms = task.elapsed_ms
        if task.status is TaskStatus.RUN and task.started_at_ms is not None:
            elapsed_ms = min(max(0, now_ms - task.started_at_ms), MAX_ELAPSED_MS)
        return {
            "id": task.task_id,
            "surface": task.surface,
            "project": task.project,
            "title": task.title,
            "status": task.status.value,
            "elapsed_ms": elapsed_ms,
            "updated_at_ms": max(0, task.updated_at_ms),
        }

    @staticmethod
    def _event_payload(event: TaskEvent) -> dict[str, Any]:
        return {
            "seq": event.seq,
            "task_id": event.task_id,
            "type": event.event_type,
            "notify": event.notify,
            "occurred_at_ms": max(0, event.occurred_at_ms),
        }

    @staticmethod
    def _state_object(value: Any, name: str) -> dict[str, Any]:
        if not isinstance(value, dict):
            raise ValueError(f"{name} must be an object")
        return value

    @staticmethod
    def _state_list(value: Any, name: str) -> list[Any]:
        if not isinstance(value, list):
            raise ValueError(f"{name} must be an array")
        return value

    @staticmethod
    def _state_string(value: Any, name: str) -> str:
        if not isinstance(value, str):
            raise ValueError(f"{name} must be a string")
        return value

    @staticmethod
    def _state_id(value: Any, name: str) -> str:
        value = TaskStore._state_string(value, name)
        if not value.strip() or len(value.encode("utf-8")) > 128:
            raise ValueError(f"invalid {name}")
        return value

    @staticmethod
    def _state_uint(value: Any, name: str) -> int:
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{name} must be an unsigned integer")
        return value
