#!/usr/bin/env python3
"""End-to-end test for the isolated OPEN Skill validation path."""

from __future__ import annotations

import json
import io
import shutil
import sys
import tempfile
import time
import unittest
from contextlib import contextmanager, redirect_stdout
from pathlib import Path
from typing import Iterator


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "skills" / "scripts"
sys.path.insert(0, str(SCRIPTS))

from run_skill_validation import main as run_validation_main  # noqa: E402
from run_skill_validation import run_validation_workspace  # noqa: E402


@contextmanager
def temporary_test_directory(parent: Path) -> Iterator[Path]:
    path = Path(tempfile.mkdtemp(prefix="skill-validation-test-", dir=parent))
    try:
        yield path
    finally:
        for attempt in range(20):
            try:
                shutil.rmtree(path)
                break
            except FileNotFoundError:
                break
            except PermissionError:
                if attempt == 19:
                    raise
                time.sleep(0.1)


class SkillValidationWorkspaceTest(unittest.TestCase):
    def test_runs_four_fixtures_and_one_live_readonly_path_without_side_effects(self) -> None:
        temporary_root = ROOT / ".codex_tmp"
        temporary_root.mkdir(exist_ok=True)
        with temporary_test_directory(temporary_root) as temporary:
            workspace = temporary / "run"

            summary = run_validation_workspace(
                workspace,
                live_repository=ROOT,
            )

            self.assertEqual(summary["fixture_scenarios"], 4)
            self.assertEqual(summary["live_readonly_scenarios"], 1)
            self.assertEqual(summary["overall_validation_status"], "PASS")
            self.assertEqual(
                summary["platform_validation"]["windows"]["status"], "PASS"
            )
            self.assertEqual(
                summary["platform_validation"]["macos"]["status"], "PASS"
            )
            self.assertEqual(summary["orchestrator_validation_status"], "PASS")
            self.assertEqual(summary["orchestrator_package_validation"]["status"], "PASS")
            self.assertEqual(summary["child_skill_validation"]["status"], "PASS")
            self.assertEqual(summary["child_skill_validation"]["passed"], 11)
            self.assertEqual(summary["child_skill_validation"]["total"], 11)
            self.assertEqual(summary["failure_flow_validation"]["status"], "PASS")
            self.assertEqual(summary["failure_flow_validation"]["passed"], 8)
            self.assertEqual(summary["failure_flow_validation"]["total"], 8)
            self.assertEqual(summary["quick_start_validation"]["status"], "PASS")
            self.assertEqual(summary["quick_start_validation"]["passed"], 3)
            self.assertEqual(summary["quick_start_validation"]["total"], 3)
            self.assertEqual(summary["filesystem_write_scope"], workspace.as_posix())
            self.assertFalse(summary["network"])
            self.assertFalse(summary["device_write"])
            self.assertTrue((workspace / "REPORT.md").is_file())
            child_matrix = json.loads(
                (workspace / "child-skill-validation.json").read_text(encoding="utf-8")
            )
            expected_children = {
                "open-preflight",
                "open-driver",
                "open-git",
                "open-idf",
                "open-clone",
                "open-components",
                "open-first-example",
                "open-build",
                "open-flash",
                "open-monitor",
                "open-diagnose",
            }
            self.assertEqual(set(child_matrix["skills"]), expected_children)
            self.assertEqual(child_matrix["validation_status"], "PASS")
            self.assertTrue(all(child_matrix["coverage_checks"].values()))
            self.assertTrue(
                all(
                    result["validation_status"] == "PASS"
                    for result in child_matrix["skills"].values()
                )
            )
            simulated_statuses = {
                platform_result["simulated_result"]["status"]
                for result in child_matrix["skills"].values()
                for platform_result in result["platforms"].values()
            }
            self.assertTrue({"FAILED", "BLOCKED", "NEEDS_USER"} <= simulated_statuses)
            for result in child_matrix["skills"].values():
                self.assertEqual(set(result["platforms"]), {"windows", "macos"})
                self.assertTrue(
                    all(
                        platform_result["validation_status"] == "PASS"
                        for platform_result in result["platforms"].values()
                    )
                )

            expected = {
                "windows-empty",
                "windows-existing",
                "macos-empty",
                "macos-existing",
                "live-readonly",
            }
            self.assertEqual(
                {path.name for path in workspace.iterdir() if path.is_dir()},
                expected,
            )

            for name in expected:
                scenario_root = workspace / name
                self.assertTrue((scenario_root / "scenario.json").is_file())
                self.assertTrue((scenario_root / "PROMPT.md").is_file())
                scenario = json.loads(
                    (scenario_root / "scenario.json").read_text(encoding="utf-8")
                )
                self.assertIn(scenario["platform"], {"windows", "macos", "unsupported"})
                self.assertEqual(scenario["example"], "display_demo")
                self.assertEqual(scenario["action"], "all")
                self.assertEqual(scenario["idf_version"], "v5.5.4")
                self.assertEqual(scenario["continue_policy"], "dependency-aware")
                result = json.loads((scenario_root / "result.json").read_text(encoding="utf-8"))
                self.assertEqual(result["validation_status"], "PASS")
                self.assertTrue(
                    all(check["status"] == "PASS" for check in result["validation_checks"])
                )
                self.assertFalse(result["side_effects"]["network"])
                self.assertFalse(result["side_effects"]["device_write"])
                self.assertEqual(
                    result["side_effects"]["filesystem_write_scope"],
                    workspace.as_posix(),
                )

            windows_empty = json.loads(
                (workspace / "windows-empty" / "result.json").read_text(encoding="utf-8")
            )
            windows_existing = json.loads(
                (workspace / "windows-existing" / "result.json").read_text(encoding="utf-8")
            )
            self.assertEqual(windows_empty["steps"]["clone"]["status"], "BLOCKED")
            self.assertEqual(windows_empty["aggregate_status"], "FAILED")
            self.assertEqual(windows_empty["expected_aggregate_status"], "FAILED")
            self.assertEqual(windows_existing["steps"]["build"]["status"], "BLOCKED")
            self.assertEqual(windows_existing["aggregate_status"], "BLOCKED")
            self.assertEqual(windows_existing["expected_aggregate_status"], "BLOCKED")
            self.assertEqual(
                windows_existing["steps"]["build"]["blockers"][0]["code"],
                "S5_PORTABLE_ENTRY_PENDING",
            )
            self.assertEqual(windows_existing["steps"]["flash"]["status"], "BLOCKED")

            failure_flows = json.loads(
                (workspace / "failure-flow-validation.json").read_text(encoding="utf-8")
            )
            self.assertEqual(failure_flows["validation_status"], "PASS")
            self.assertEqual(failure_flows["passed"], 8)
            self.assertEqual(failure_flows["total"], 8)
            self.assertEqual(
                set(failure_flows["cases"]),
                {
                    "idf-failure-propagation",
                    "component-failure-propagation",
                    "build-failure-propagation",
                    "flash-failure-propagation",
                    "multiple-ports-needs-user",
                    "no-device-blocks",
                    "device-write-confirmation-required",
                    "s5-portable-entry-blocks",
                },
            )
            self.assertTrue(
                all(
                    case["validation_status"] == "PASS"
                    for case in failure_flows["cases"].values()
                )
            )

            quick_start = json.loads(
                (workspace / "quick-start-validation.json").read_text(encoding="utf-8")
            )
            self.assertEqual(quick_start["validation_status"], "PASS")
            self.assertEqual(
                set(quick_start["roles"]),
                {"maintainer", "external-reader", "functional-reviewer"},
            )
            self.assertTrue(
                all(
                    role["validation_status"] == "PASS"
                    for role in quick_start["roles"].values()
                )
            )

    def test_accepts_space_and_unicode_paths(self) -> None:
        temporary_root = ROOT / ".codex_tmp"
        temporary_root.mkdir(exist_ok=True)
        with temporary_test_directory(temporary_root) as temporary:
            workspace = temporary / "validation results 验证结果"
            summary = run_validation_workspace(workspace, live_repository=ROOT)

            self.assertEqual(summary["overall_validation_status"], "PASS")
            self.assertEqual(summary["filesystem_write_scope"], workspace.as_posix())

    def test_refuses_missing_snapshot_and_existing_workspace(self) -> None:
        temporary_root = ROOT / ".codex_tmp"
        temporary_root.mkdir(exist_ok=True)
        with temporary_test_directory(temporary_root) as temporary:
            with self.assertRaises(FileNotFoundError):
                run_validation_workspace(
                    temporary / "missing-output",
                    live_repository=temporary / "missing-repository",
                )

            existing_workspace = temporary / "existing-output"
            existing_workspace.mkdir()
            with self.assertRaises(FileExistsError):
                run_validation_workspace(existing_workspace, live_repository=ROOT)

    def test_missing_orchestrator_package_fails_the_overall_gate(self) -> None:
        temporary_root = ROOT / ".codex_tmp"
        temporary_root.mkdir(exist_ok=True)
        with temporary_test_directory(temporary_root) as temporary:
            public_repository = temporary / "public repository"
            shutil.copytree(ROOT / "skills", public_repository / "skills")
            (public_repository / "README.md").write_text("fixture\n", encoding="utf-8")
            (public_repository / "docs").mkdir()
            example_root = public_repository / "CODE" / "examples"
            (example_root / "display_demo").mkdir(parents=True)
            (example_root / "examples.yml").write_text("examples: []\n", encoding="utf-8")
            (example_root / "display_demo" / "README.md").write_text(
                "fixture\n", encoding="utf-8"
            )
            (public_repository / "skills" / "open-dev-all" / "SKILL.md").unlink()

            summary = run_validation_workspace(
                temporary / "missing-orchestrator-result",
                live_repository=public_repository,
            )

            self.assertEqual(summary["orchestrator_package_validation"]["status"], "FAILED")
            self.assertEqual(summary["orchestrator_validation_status"], "FAILED")
            self.assertEqual(summary["overall_validation_status"], "FAILED")

            cli_output = temporary / "missing-orchestrator-cli-result"
            with redirect_stdout(io.StringIO()):
                exit_code = run_validation_main(
                    [
                        "--workspace",
                        str(cli_output),
                        "--live-repository",
                        str(public_repository),
                    ]
                )
            self.assertEqual(exit_code, 1)

    def test_unvalidated_child_skill_fails_coverage_gate(self) -> None:
        temporary_root = ROOT / ".codex_tmp"
        temporary_root.mkdir(exist_ok=True)
        with temporary_test_directory(temporary_root) as temporary:
            public_repository = temporary / "public repository"
            shutil.copytree(ROOT / "skills", public_repository / "skills")
            (public_repository / "README.md").write_text("fixture\n", encoding="utf-8")
            (public_repository / "docs").mkdir()
            example_root = public_repository / "CODE" / "examples"
            (example_root / "display_demo").mkdir(parents=True)
            (example_root / "examples.yml").write_text("examples: []\n", encoding="utf-8")
            (example_root / "display_demo" / "README.md").write_text(
                "fixture\n", encoding="utf-8"
            )
            extra_skill = public_repository / "skills" / "open-unvalidated"
            extra_skill.mkdir()
            (extra_skill / "SKILL.md").write_text(
                "---\nname: open-unvalidated\ndescription: fixture\n---\n",
                encoding="utf-8",
            )

            workspace = temporary / "unvalidated-child-result"
            summary = run_validation_workspace(
                workspace,
                live_repository=public_repository,
            )
            child_matrix = json.loads(
                (workspace / "child-skill-validation.json").read_text(encoding="utf-8")
            )

            self.assertFalse(child_matrix["coverage_checks"]["no_unvalidated_children"])
            self.assertEqual(child_matrix["validation_status"], "FAILED")
            self.assertEqual(summary["child_skill_validation"]["status"], "FAILED")
            self.assertEqual(summary["overall_validation_status"], "FAILED")


if __name__ == "__main__":
    unittest.main()
