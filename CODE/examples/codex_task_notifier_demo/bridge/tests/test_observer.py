from __future__ import annotations

import json
import shutil
import tempfile
import unittest
from pathlib import Path

from codex_task_bridge.model import TaskStore
from codex_task_bridge.observer import CodexSessionObserver


FIXTURES = Path(__file__).parent / "fixtures"
FIXTURE_NOW_MS = 1_785_286_900_000


class CodexSessionObserverTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.sessions_dir = Path(self.temp_dir.name)
        self.store = TaskStore()
        self.observer = CodexSessionObserver(self.sessions_dir, self.store)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def test_existing_files_form_baseline_without_replaying_notifications(self) -> None:
        shutil.copy(FIXTURES / "vscode_complete.jsonl", self.sessions_dir / "existing.jsonl")

        self.observer.establish_baseline()
        snapshot = self.store.snapshot(after_event_seq=0, now_ms=FIXTURE_NOW_MS)

        self.assertEqual(snapshot["aggregate"]["done_count"], 1)
        self.assertEqual(snapshot["tasks"][0]["surface"], "VS")
        self.assertEqual(snapshot["tasks"][0]["title"], "Fix VS Code task")
        self.assertEqual(snapshot["events"], [])

    def test_new_desktop_and_vscode_files_are_followed_independently(self) -> None:
        self.observer.establish_baseline()
        shutil.copy(FIXTURES / "desktop_running.jsonl", self.sessions_dir / "desktop.jsonl")
        shutil.copy(FIXTURES / "vscode_complete.jsonl", self.sessions_dir / "vscode.jsonl")

        changed = self.observer.poll_once()
        second_changed = self.observer.poll_once()
        snapshot = self.store.snapshot(after_event_seq=0, now_ms=FIXTURE_NOW_MS)

        self.assertTrue(changed)
        self.assertFalse(second_changed)
        self.assertEqual(snapshot["aggregate"]["running_count"], 1)
        self.assertEqual(snapshot["aggregate"]["done_count"], 1)
        self.assertEqual({task["surface"] for task in snapshot["tasks"]}, {"APP", "VS"})
        self.assertEqual(
            {task["title"] for task in snapshot["tasks"]},
            {"Build desktop notifier", "Fix VS Code task"},
        )
        self.assertEqual(len(snapshot["events"]), 1)

    def test_partial_json_line_waits_for_completion(self) -> None:
        target = self.sessions_dir / "partial.jsonl"
        target.write_text(
            '{"timestamp":"2026-07-29T01:00:00Z","type":"session_meta",'
            '"payload":{"session_id":"s1","originator":"codex_vscode","cwd":"/tmp/p"}}\n',
            encoding="utf-8",
        )
        self.observer.establish_baseline()
        with target.open("ab") as handle:
            handle.write(b'{"timestamp":"2026-07-29T01:00:01Z","type":"event_msg","payload":')

        self.assertFalse(self.observer.poll_once())

        with target.open("ab") as handle:
            handle.write(b'{"type":"task_started","turn_id":"t1"}}\n')

        self.assertTrue(self.observer.poll_once())
        snapshot = self.store.snapshot(after_event_seq=0, now_ms=FIXTURE_NOW_MS)
        self.assertEqual(snapshot["aggregate"]["running_count"], 1)

    def test_invalid_event_is_counted_without_creating_task(self) -> None:
        target = self.sessions_dir / "invalid.jsonl"
        lines = [
            {
                "timestamp": "2026-07-29T01:00:00Z",
                "type": "session_meta",
                "payload": {"session_id": "s1", "originator": "codex_vscode", "cwd": "/tmp/p"},
            },
            {
                "timestamp": "2026-07-29T01:00:01Z",
                "type": "event_msg",
                "payload": {"type": "task_started", "turn_id": "   "},
            },
        ]
        target.write_text("\n".join(json.dumps(item) for item in lines) + "\n", encoding="utf-8")

        self.observer.poll_once()
        snapshot = self.store.snapshot(after_event_seq=0, now_ms=FIXTURE_NOW_MS)

        self.assertEqual(snapshot["aggregate"]["total_count"], 0)
        self.assertEqual(self.observer.invalid_event_count, 1)

    def test_only_first_user_message_sets_title_and_other_records_touch_task(self) -> None:
        target = self.sessions_dir / "title.jsonl"
        lines = [
            {
                "timestamp": "2026-07-29T01:00:00Z",
                "type": "session_meta",
                "payload": {
                    "session_id": "s1",
                    "originator": "Codex Desktop",
                    "cwd": "/tmp/project",
                },
            },
            {
                "timestamp": "2026-07-29T01:00:01Z",
                "type": "event_msg",
                "payload": {"type": "task_started", "turn_id": "t1"},
            },
            {
                "timestamp": "2026-07-29T01:00:02Z",
                "type": "event_msg",
                "payload": {"type": "user_message", "message": "  First\n title "},
            },
            {
                "timestamp": "2026-07-29T01:00:03Z",
                "type": "event_msg",
                "payload": {"type": "user_message", "message": "Second title"},
            },
            {
                "timestamp": "2026-07-29T01:05:00Z",
                "type": "event_msg",
                "payload": {"type": "token_count", "info": {}},
            },
        ]
        target.write_text(
            "\n".join(json.dumps(item) for item in lines) + "\n", encoding="utf-8"
        )

        self.assertTrue(self.observer.poll_once())
        task = self.store.snapshot(0, FIXTURE_NOW_MS)["tasks"][0]

        self.assertEqual(task["title"], "First title")
        self.assertEqual(task["updated_at_ms"], 1_785_287_100_000)


if __name__ == "__main__":
    unittest.main()
