from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from codex_task_bridge.model import TaskStore
from codex_task_bridge.observer import CodexSessionObserver
from codex_task_bridge.persistence import BridgeStatePersistence


SESSION_META = {
    "timestamp": "2026-07-29T01:00:00Z",
    "type": "session_meta",
    "payload": {
        "session_id": "session-1",
        "originator": "Codex Desktop",
        "cwd": "/tmp/project",
    },
}
TASK_STARTED = {
    "timestamp": "2026-07-29T01:00:01Z",
    "type": "event_msg",
    "payload": {"type": "task_started", "turn_id": "turn-1"},
}
TASK_COMPLETE = {
    "timestamp": "2026-07-29T01:00:32Z",
    "type": "event_msg",
    "payload": {"type": "task_complete", "turn_id": "turn-1", "duration_ms": 31_000},
}


class BridgeStatePersistenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.sessions_dir = self.root / "sessions"
        self.sessions_dir.mkdir()
        self.state_path = self.root / "state.json"
        self.session_path = self.sessions_dir / "session.jsonl"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def _append(self, *items: dict[str, object]) -> None:
        with self.session_path.open("a", encoding="utf-8") as handle:
            for item in items:
                handle.write(json.dumps(item) + "\n")

    def test_restart_resumes_offset_and_event_sequence_without_replay(self) -> None:
        self._append(SESSION_META, TASK_STARTED)
        first_store = TaskStore()
        first_observer = CodexSessionObserver(self.sessions_dir, first_store)
        first_observer.poll_once()
        persistence = BridgeStatePersistence(self.state_path)
        persistence.save(first_store, first_observer)

        restored_store = TaskStore()
        restored_observer = CodexSessionObserver(self.sessions_dir, restored_store)
        self.assertTrue(persistence.restore(restored_store, restored_observer))
        self.assertFalse(restored_observer.poll_once())

        self._append(TASK_COMPLETE)
        self.assertTrue(restored_observer.poll_once())
        snapshot = restored_store.snapshot(0, 1_785_286_900_000)
        self.assertEqual(snapshot["aggregate"]["done_count"], 1)
        self.assertEqual([event["seq"] for event in snapshot["events"]], [1])
        self.assertTrue(snapshot["events"][0]["notify"])

        persistence.save(restored_store, restored_observer)
        final_store = TaskStore()
        final_observer = CodexSessionObserver(self.sessions_dir, final_store)
        self.assertTrue(persistence.restore(final_store, final_observer))
        self.assertFalse(final_observer.poll_once())
        final_snapshot = final_store.snapshot(0, 1_785_286_900_000)
        self.assertEqual([event["seq"] for event in final_snapshot["events"]], [1])

    def test_corrupt_state_is_quarantined(self) -> None:
        self.state_path.write_text("{not-json", encoding="utf-8")
        persistence = BridgeStatePersistence(self.state_path)

        restored = persistence.restore(
            TaskStore(), CodexSessionObserver(self.sessions_dir, TaskStore())
        )

        self.assertFalse(restored)
        self.assertFalse(self.state_path.exists())
        self.assertEqual(len(list(self.root.glob("state.json.corrupt-*"))), 1)


if __name__ == "__main__":
    unittest.main()
