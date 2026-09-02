from __future__ import annotations

import unittest

from codex_task_bridge.model import RUN_STALE_TIMEOUT_MS, TaskStatus, TaskStore


class TaskStoreTest(unittest.TestCase):
    def setUp(self) -> None:
        self.store = TaskStore()

    def test_desktop_and_vscode_tasks_are_independent(self) -> None:
        self.store.register_session("desktop-session", "Codex Desktop", "/tmp/project-a")
        self.store.register_session("vscode-session", "codex_vscode", "/tmp/project-b")

        self.store.start_task("desktop-session", "turn-a", 1_000)
        self.store.start_task("vscode-session", "turn-b", 2_000)
        self.store.complete_task("desktop-session", "turn-a", 32_000, duration_ms=31_000)

        snapshot = self.store.snapshot(after_event_seq=0, now_ms=33_000)

        self.assertEqual(snapshot["aggregate"]["state"], "RUNNING")
        self.assertEqual(snapshot["aggregate"]["running_count"], 1)
        tasks = {task["project"]: task for task in snapshot["tasks"]}
        self.assertEqual(tasks["project-a"]["surface"], "APP")
        self.assertEqual(tasks["project-a"]["status"], "DONE")
        self.assertEqual(tasks["project-a"]["title"], "project-a")
        self.assertEqual(tasks["project-b"]["surface"], "VS")
        self.assertEqual(tasks["project-b"]["status"], "RUN")
        self.assertEqual(len(snapshot["events"]), 1)
        self.assertTrue(snapshot["events"][0]["notify"])

    def test_completion_notification_has_30_second_inclusive_boundary(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/short")
        self.store.start_task("s1", "t-short", 1_000)
        self.store.complete_task("s1", "t-short", 30_999, duration_ms=29_999)
        self.store.start_task("s1", "t-boundary", 40_000)
        self.store.complete_task("s1", "t-boundary", 70_000, duration_ms=30_000)

        events = self.store.snapshot(after_event_seq=0, now_ms=70_000)["events"]

        self.assertEqual([event["notify"] for event in events], [False, True])

    def test_duplicate_completion_is_idempotent(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/project")
        self.store.start_task("s1", "t1", 1_000)
        self.store.complete_task("s1", "t1", 40_000, duration_ms=39_000)
        self.store.complete_task("s1", "t1", 41_000, duration_ms=40_000)

        snapshot = self.store.snapshot(after_event_seq=0, now_ms=41_000)

        self.assertEqual(len(snapshot["events"]), 1)
        self.assertEqual(snapshot["aggregate"]["done_count"], 1)

    def test_completion_without_start_is_visible_but_never_notifies(self) -> None:
        self.store.register_session("s1", "Codex Desktop", "/tmp/project")
        self.store.complete_task("s1", "t1", 40_000, duration_ms=60_000)

        snapshot = self.store.snapshot(after_event_seq=0, now_ms=40_000)

        self.assertEqual(snapshot["tasks"][0]["status"], TaskStatus.DONE.value)
        self.assertFalse(snapshot["events"][0]["notify"])

    def test_stop_updates_same_task_without_completion_notification(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/project")
        self.store.start_task("s1", "t1", 1_000)
        self.store.stop_task("s1", "t1", 10_000)

        snapshot = self.store.snapshot(after_event_seq=0, now_ms=10_000)

        self.assertEqual(snapshot["tasks"][0]["status"], TaskStatus.STOP.value)
        self.assertEqual(snapshot["events"][0]["type"], "TURN_STOPPED")
        self.assertFalse(snapshot["events"][0]["notify"])

    def test_visible_task_limit_covers_11_12_and_13(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/project")
        for index in range(13):
            self.store.start_task("s1", f"turn-{index}", index * 1_000)

        snapshot = self.store.snapshot(after_event_seq=0, now_ms=20_000)

        self.assertEqual(snapshot["aggregate"]["total_count"], 13)
        self.assertEqual(snapshot["aggregate"]["overflow_count"], 1)
        self.assertEqual(len(snapshot["tasks"]), 12)

    def test_done_and_stopped_retention_are_inclusive(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/project")
        self.store.start_task("s1", "done", 0)
        self.store.complete_task("s1", "done", 30_000, duration_ms=30_000)
        self.store.start_task("s1", "stop", 0)
        self.store.stop_task("s1", "stop", 30_000)

        at_stop_boundary = self.store.snapshot(after_event_seq=0, now_ms=210_000)
        after_stop_boundary = self.store.snapshot(after_event_seq=0, now_ms=210_001)
        at_done_boundary = self.store.snapshot(after_event_seq=0, now_ms=630_000)
        after_done_boundary = self.store.snapshot(after_event_seq=0, now_ms=630_001)

        self.assertEqual(at_stop_boundary["aggregate"]["total_count"], 2)
        self.assertEqual(after_stop_boundary["aggregate"]["total_count"], 1)
        self.assertEqual(at_done_boundary["aggregate"]["total_count"], 1)
        self.assertEqual(after_done_boundary["aggregate"]["total_count"], 0)

    def test_event_ring_reports_truncation_and_honors_cursor(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/project")
        for index in range(33):
            turn_id = f"turn-{index}"
            self.store.start_task("s1", turn_id, index * 1_000)
            self.store.stop_task("s1", turn_id, index * 1_000 + 500)

        full_snapshot = self.store.snapshot(after_event_seq=0, now_ms=40_000)
        tail_snapshot = self.store.snapshot(after_event_seq=32, now_ms=40_000)

        self.assertEqual(len(full_snapshot["events"]), 32)
        self.assertEqual(full_snapshot["events"][0]["seq"], 2)
        self.assertTrue(full_snapshot["events_truncated"])
        self.assertEqual([event["seq"] for event in tail_snapshot["events"]], [33])
        self.assertFalse(tail_snapshot["events_truncated"])

    def test_turn_context_overrides_session_project_and_utf8_stays_valid(self) -> None:
        self.store.register_session("s1", "Codex Desktop", "/tmp/session-project")
        long_project = "项目" * 20
        self.store.register_turn_context("s1", "t1", f"/tmp/{long_project}")
        self.store.start_task("s1", "t1", 1_000)

        project = self.store.snapshot(0, 2_000)["tasks"][0]["project"]

        self.assertLessEqual(len(project.encode("utf-8")), 48)
        self.assertTrue(long_project.startswith(project))

    def test_windows_cwd_uses_final_directory_as_project(self) -> None:
        self.store.register_session(
            "s1",
            "Codex Desktop",
            r"C:\Users\tester\Project Documents\魔块",
        )
        self.store.start_task("s1", "t1", 1_000)

        project = self.store.snapshot(0, 2_000)["tasks"][0]["project"]

        self.assertEqual(project, "魔块")

    def test_first_user_title_wins_and_utf8_truncation_stays_valid(self) -> None:
        self.store.register_session("s1", "Codex Desktop", "/tmp/project")
        self.store.start_task("s1", "t1", 1_000)

        self.assertTrue(
            self.store.register_task_title(
                "s1", "t1", "  修复\n Bridge\t任务  "
            )
        )
        self.assertFalse(
            self.store.register_task_title("s1", "t1", "must not replace")
        )
        self.store.start_task("s1", "t2", 2_000)
        self.store.register_task_title("s1", "t2", "任务" * 40)

        tasks = {
            task["id"]: task for task in self.store.snapshot(0, 3_000)["tasks"]
        }
        titles = {task["title"] for task in tasks.values()}

        self.assertIn("修复 Bridge 任务", titles)
        long_title = next(title for title in titles if title != "修复 Bridge 任务")
        self.assertLessEqual(len(long_title.encode("utf-8")), 72)
        self.assertTrue(("任务" * 40).startswith(long_title))

    def test_running_task_is_pruned_at_inactivity_boundary_without_event(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/project")
        self.store.start_task("s1", "t1", 1_000)
        self.assertTrue(self.store.touch_task("s1", "t1", 2_000))

        before = self.store.snapshot(0, 2_000 + RUN_STALE_TIMEOUT_MS - 1)
        at_boundary = self.store.snapshot(0, 2_000 + RUN_STALE_TIMEOUT_MS)

        self.assertEqual(before["aggregate"]["running_count"], 1)
        self.assertEqual(at_boundary["aggregate"]["total_count"], 0)
        self.assertEqual(at_boundary["events"], [])
        self.assertEqual(self.store.stale_run_pruned_count(), 1)

    def test_legacy_persisted_task_without_title_uses_project_fallback(self) -> None:
        self.store.register_session("s1", "codex_vscode", "/tmp/legacy-project")
        self.store.start_task("s1", "t1", 1_000)
        state = self.store.export_state()
        state["tasks"][0].pop("title")
        state["tasks"][0].pop("title_set")
        restored = TaskStore()

        restored.restore_state(state)
        task = restored.snapshot(0, 2_000)["tasks"][0]

        self.assertEqual(task["title"], "legacy-project")


if __name__ == "__main__":
    unittest.main()
