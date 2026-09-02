#!/usr/bin/env python3
"""Behavior tests for the public OPEN Skill state contract."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))

from open_skill_runtime import (  # noqa: E402
    evaluate_flash_readiness,
    make_result,
    redact_text,
    resolve_workflow,
)


class ResultContractTest(unittest.TestCase):
    def test_result_contract_is_complete_and_rejects_unknown_status(self) -> None:
        result = make_result(
            skill="open-preflight",
            status="PASS",
            language="zh-CN",
            summary="环境事实已收集。",
            evidence=["platform=windows"],
            side_effects={"filesystem_write": False, "network": False, "device_write": False},
        )

        self.assertEqual(
            set(result),
            {
                "schema_version",
                "skill",
                "status",
                "language",
                "summary",
                "facts",
                "evidence",
                "blockers",
                "next_actions",
                "side_effects",
            },
        )
        with self.assertRaises(ValueError):
            make_result(
                skill="open-preflight",
                status="SUCCESS",
                language="zh-CN",
                summary="invalid",
            )

    def test_log_redaction_removes_credentials_device_ids_and_user_homes(self) -> None:
        windows_home = "C:" + chr(92) + "Users" + chr(92) + "alice" + chr(92) + "project"
        macos_home = "/" + "Users" + "/bob/work"
        raw = (
            "wifi_password=secret-value token=abc123 "
            "Authorization: Bearer bearer-secret "
            f"path={windows_home} other={macos_home} "
            "mac=AA:BB:CC:DD:EE:FF message=keep-me"
        )

        redacted = redact_text(raw)

        for sensitive in (
            "secret-value",
            "abc123",
            "bearer-secret",
            "alice",
            "bob",
            "AA:BB:CC:DD:EE:FF",
        ):
            self.assertNotIn(sensitive, redacted)
        self.assertIn("message=keep-me", redacted)
        self.assertIn("<REDACTED>", redacted)


class WorkflowDependencyTest(unittest.TestCase):
    def test_failed_build_blocks_flash_with_bilingual_contract(self) -> None:
        observed = {
            "build": {
                "status": "FAILED",
                "summary": "compiler returned a non-zero exit code",
            }
        }

        zh_result = resolve_workflow(observed, language="zh-CN")
        en_result = resolve_workflow(observed, language="en")

        self.assertEqual(zh_result["flash"]["status"], "BLOCKED")
        self.assertEqual(en_result["flash"]["status"], "BLOCKED")
        self.assertEqual(
            zh_result["flash"]["blockers"][0]["code"],
            "UPSTREAM_BUILD_FAILED",
        )
        self.assertNotEqual(
            zh_result["flash"]["blockers"][0]["message"],
            en_result["flash"]["blockers"][0]["message"],
        )

    def test_blocked_build_propagates_its_blocker_to_flash(self) -> None:
        observed = {
            "build": make_result(
                skill="open-build",
                status="BLOCKED",
                language="en",
                summary="Portable entry pending.",
                blockers=[
                    {
                        "code": "S5_PORTABLE_ENTRY_PENDING",
                        "message": "Portable entry pending.",
                    }
                ],
            )
        }

        result = resolve_workflow(observed, language="en")

        self.assertEqual(result["flash"]["status"], "BLOCKED")
        self.assertEqual(
            result["flash"]["blockers"][0]["code"],
            "S5_PORTABLE_ENTRY_PENDING",
        )

    def test_git_and_idf_failures_block_only_their_real_dependents(self) -> None:
        git_failed = {
            "git": make_result(
                skill="open-git",
                status="FAILED",
                language="en",
                summary="Git is unavailable.",
            )
        }
        without_source = resolve_workflow(
            git_failed,
            language="en",
            context={"repository_available": False},
        )

        self.assertEqual(without_source["clone"]["status"], "BLOCKED")
        self.assertEqual(without_source["components"]["status"], "BLOCKED")
        self.assertEqual(without_source["build"]["status"], "BLOCKED")
        self.assertEqual(without_source["flash"]["status"], "BLOCKED")

        idf_failed = {
            "idf": make_result(
                skill="open-idf",
                status="FAILED",
                language="en",
                summary="ESP-IDF is unavailable.",
            )
        }
        with_source = resolve_workflow(
            idf_failed,
            language="en",
            context={"repository_available": True},
        )

        self.assertNotIn("clone", with_source)
        self.assertEqual(with_source["components"]["status"], "BLOCKED")
        self.assertEqual(with_source["build"]["status"], "BLOCKED")
        self.assertEqual(with_source["flash"]["status"], "BLOCKED")

    def test_cli_resolves_stdin_json_without_hidden_state(self) -> None:
        request = {
            "observed": {
                "build": make_result(
                    skill="open-build",
                    status="FAILED",
                    language="en",
                    summary="build failed",
                )
            },
            "context": {},
        }

        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPTS / "open_skill_runtime.py"),
                "resolve",
                "--language",
                "en",
            ],
            input=json.dumps(request),
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        response = json.loads(completed.stdout)
        self.assertEqual(response["flash"]["status"], "BLOCKED")

    def test_component_failure_blocks_build_and_flash(self) -> None:
        observed = {
            "components": make_result(
                skill="open-components",
                status="FAILED",
                language="zh-CN",
                summary="依赖解析失败。",
            )
        }

        result = resolve_workflow(observed, language="zh-CN")

        self.assertEqual(result["build"]["status"], "BLOCKED")
        self.assertEqual(result["flash"]["status"], "BLOCKED")
        self.assertEqual(
            result["build"]["blockers"][0]["code"],
            "UPSTREAM_COMPONENTS_FAILED",
        )

    def test_missing_driver_blocks_device_steps_but_not_build(self) -> None:
        observed = {
            "driver": make_result(
                skill="open-driver",
                status="FAILED",
                language="en",
                summary="Serial device is not visible.",
            )
        }

        result = resolve_workflow(
            observed,
            language="en",
            context={"serial_available": False},
        )

        self.assertNotIn("build", result)
        self.assertEqual(result["flash"]["status"], "BLOCKED")
        self.assertEqual(result["monitor"]["status"], "BLOCKED")
        self.assertEqual(
            result["monitor"]["blockers"][0]["code"],
            "SERIAL_UNAVAILABLE",
        )

    def test_offline_flow_blocks_downloads_unless_local_inputs_are_available(self) -> None:
        missing_inputs = resolve_workflow(
            {},
            language="en",
            context={
                "network_available": False,
                "repository_available": False,
                "dependency_cache_available": False,
            },
        )

        self.assertEqual(missing_inputs["clone"]["status"], "BLOCKED")
        self.assertEqual(missing_inputs["components"]["status"], "BLOCKED")
        self.assertEqual(missing_inputs["build"]["status"], "BLOCKED")
        self.assertEqual(
            missing_inputs["clone"]["blockers"][0]["code"],
            "NETWORK_UNAVAILABLE",
        )

        cached_inputs = resolve_workflow(
            {},
            language="en",
            context={
                "network_available": False,
                "repository_available": True,
                "dependency_cache_available": True,
            },
        )
        self.assertNotIn("clone", cached_inputs)
        self.assertNotIn("components", cached_inputs)

    def test_failed_flash_blocks_dependent_monitoring_without_retry(self) -> None:
        observed = {
            "flash": make_result(
                skill="open-flash",
                status="FAILED",
                language="zh-CN",
                summary="烧录工具返回非零结果。",
                side_effects={
                    "filesystem_write": False,
                    "network": False,
                    "device_write": True,
                },
            )
        }

        result = resolve_workflow(observed, language="zh-CN")

        self.assertEqual(result["flash"]["status"], "FAILED")
        self.assertEqual(result["monitor"]["status"], "BLOCKED")
        self.assertEqual(
            result["monitor"]["blockers"][0]["code"],
            "UPSTREAM_FLASH_FAILED",
        )


class FlashSafetyTest(unittest.TestCase):
    def test_multiple_ports_need_user_selection_without_guessing(self) -> None:
        result = evaluate_flash_readiness(
            language="zh-CN",
            build_succeeded=True,
            port_candidates=["candidate-a", "candidate-b"],
            device_write_confirmed=False,
            portable_entry_available=True,
        )

        self.assertEqual(result["status"], "NEEDS_USER")
        self.assertEqual(result["blockers"][0]["code"], "PORT_NOT_UNIQUE")
        self.assertNotIn("selected_port", result["facts"])
        self.assertFalse(result["side_effects"]["device_write"])

    def test_no_port_blocks_flash_without_device_write(self) -> None:
        result = evaluate_flash_readiness(
            language="en",
            build_succeeded=True,
            port_candidates=[],
            device_write_confirmed=True,
            portable_entry_available=True,
        )

        self.assertEqual(result["status"], "BLOCKED")
        self.assertEqual(result["blockers"][0]["code"], "NO_DEVICE")
        self.assertFalse(result["side_effects"]["device_write"])

    def test_unconfirmed_device_write_needs_user_confirmation(self) -> None:
        result = evaluate_flash_readiness(
            language="zh-CN",
            build_succeeded=True,
            port_candidates=["candidate-a"],
            device_write_confirmed=False,
            portable_entry_available=True,
        )

        self.assertEqual(result["status"], "NEEDS_USER")
        self.assertEqual(
            result["blockers"][0]["code"],
            "DEVICE_WRITE_NOT_CONFIRMED",
        )
        self.assertFalse(result["side_effects"]["device_write"])

    def test_missing_portable_entry_blocks_flash_until_final_s5(self) -> None:
        result = evaluate_flash_readiness(
            language="en",
            build_succeeded=True,
            port_candidates=["candidate-a"],
            device_write_confirmed=True,
            portable_entry_available=False,
        )

        self.assertEqual(result["status"], "BLOCKED")
        self.assertEqual(
            result["blockers"][0]["code"],
            "S5_PORTABLE_ENTRY_PENDING",
        )
        self.assertFalse(result["side_effects"]["device_write"])

    def test_unsuccessful_build_blocks_flash_readiness(self) -> None:
        result = evaluate_flash_readiness(
            language="zh-CN",
            build_succeeded=False,
            port_candidates=["candidate-a"],
            device_write_confirmed=True,
            portable_entry_available=True,
        )

        self.assertEqual(result["status"], "BLOCKED")
        self.assertEqual(
            result["blockers"][0]["code"],
            "UPSTREAM_BUILD_FAILED",
        )

    def test_complete_prerequisites_report_ready_without_flashing(self) -> None:
        result = evaluate_flash_readiness(
            language="en",
            build_succeeded=True,
            port_candidates=["candidate-a"],
            device_write_confirmed=True,
            portable_entry_available=True,
        )

        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["facts"]["selected_port"], "candidate-a")
        self.assertFalse(result["side_effects"]["device_write"])


if __name__ == "__main__":
    unittest.main()
