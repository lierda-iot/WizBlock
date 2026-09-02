#!/usr/bin/env python3
"""Generate a no-side-effect validation workspace for the public OPEN Skills."""

from __future__ import annotations

import argparse
import json
import platform
from pathlib import Path
from typing import Any

from open_skill_runtime import evaluate_flash_readiness, make_result, resolve_workflow


FIXTURE_SCENARIOS = (
    ("windows-empty", "windows", "zh-CN", False),
    ("windows-existing", "windows", "zh-CN", True),
    ("macos-empty", "macos", "en", False),
    ("macos-existing", "macos", "en", True),
)
STATUS_PRIORITY = {
    "PASS": 1,
    "SKIPPED": 2,
    "NEEDS_USER": 3,
    "BLOCKED": 4,
    "FAILED": 5,
}
CHILD_SKILL_CASES = {
    "open-preflight": ("PASS", None, "Read-only host facts were collected."),
    "open-driver": (
        "NEEDS_USER",
        "DEVICE_CONNECTION_UNCONFIRMED",
        "Physical device connection requires user confirmation.",
    ),
    "open-git": ("PASS", None, "Git availability was observed read-only."),
    "open-idf": (
        "FAILED",
        "IDF_VERSION_MISMATCH",
        "The simulated ESP-IDF version does not match v5.5.4.",
    ),
    "open-clone": (
        "BLOCKED",
        "NETWORK_UNAVAILABLE",
        "No local repository or network is available in this simulated case.",
    ),
    "open-components": (
        "BLOCKED",
        "UPSTREAM_IDF_FAILED",
        "Component preparation is blocked by the simulated IDF failure.",
    ),
    "open-first-example": (
        "BLOCKED",
        "EXAMPLE_UNAVAILABLE",
        "The explicitly requested simulated Example is unavailable.",
    ),
    "open-build": (
        "BLOCKED",
        "S5_PORTABLE_ENTRY_PENDING",
        "The final-S5 portable build entry is pending.",
    ),
    "open-flash": (
        "BLOCKED",
        "S5_PORTABLE_ENTRY_PENDING",
        "The final-S5 portable flash entry is pending.",
    ),
    "open-monitor": (
        "NEEDS_USER",
        "PORT_NOT_UNIQUE",
        "Multiple simulated ports require an explicit user selection.",
    ),
    "open-diagnose": (
        "PASS",
        None,
        "The first simulated actionable failure was classified.",
    ),
}


def _write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _aggregate_status(steps: dict[str, dict[str, Any]]) -> str:
    return max(
        (result["status"] for result in steps.values()),
        key=STATUS_PRIORITY.__getitem__,
    )


def _fixture_expectation(repository_available: bool) -> dict[str, Any]:
    if repository_available:
        return {
            "aggregate_status": "BLOCKED",
            "steps": {
                "build": {
                    "status": "BLOCKED",
                    "blocker": "S5_PORTABLE_ENTRY_PENDING",
                },
                "flash": {
                    "status": "BLOCKED",
                    "blocker": "S5_PORTABLE_ENTRY_PENDING",
                },
            },
        }
    return {
        "aggregate_status": "FAILED",
        "steps": {
            "driver": {"status": "FAILED"},
            "git": {"status": "FAILED"},
            "idf": {"status": "FAILED"},
            "clone": {"status": "BLOCKED"},
            "build": {"status": "BLOCKED"},
            "flash": {"status": "BLOCKED"},
            "monitor": {"status": "BLOCKED"},
        },
    }


def _live_expectation() -> dict[str, Any]:
    return {
        "aggregate_status": "BLOCKED",
        "steps": {
            "preflight": {"status": "PASS"},
            "components": {"status": "PASS"},
            "first_example": {"status": "PASS"},
            "build": {
                "status": "BLOCKED",
                "blocker": "S5_PORTABLE_ENTRY_PENDING",
            },
            "flash": {
                "status": "BLOCKED",
                "blocker": "NETWORK_UNAVAILABLE",
            },
        },
    }


def _validate_expected_workflow(
    steps: dict[str, dict[str, Any]], expectation: dict[str, Any]
) -> tuple[str, list[dict[str, Any]]]:
    checks: list[dict[str, Any]] = []

    def add_check(name: str, expected: Any, actual: Any) -> None:
        checks.append(
            {
                "name": name,
                "expected": expected,
                "actual": actual,
                "status": "PASS" if actual == expected else "FAILED",
            }
        )

    add_check(
        "aggregate_status",
        expectation["aggregate_status"],
        _aggregate_status(steps),
    )
    for step_name, expected_step in expectation["steps"].items():
        actual_step = steps.get(step_name, {})
        add_check(
            f"steps.{step_name}.status",
            expected_step["status"],
            actual_step.get("status"),
        )
        if "blocker" in expected_step:
            blockers = actual_step.get("blockers") or []
            actual_blocker = blockers[0].get("code") if blockers else None
            add_check(
                f"steps.{step_name}.blockers[0].code",
                expected_step["blocker"],
                actual_blocker,
            )
    validation_status = (
        "PASS" if all(check["status"] == "PASS" for check in checks) else "FAILED"
    )
    return validation_status, checks


def _simulated_child_result(name: str, platform_name: str) -> dict[str, Any]:
    expected_status, blocker_code, summary = CHILD_SKILL_CASES[name]
    language = "zh-CN" if platform_name == "windows" else "en"
    if name == "open-flash":
        result = evaluate_flash_readiness(
            language=language,
            build_succeeded=True,
            port_candidates=["simulated-port"],
            device_write_confirmed=True,
            portable_entry_available=False,
        )
        result["facts"]["platform"] = platform_name
        return result
    blockers = (
        [{"code": blocker_code, "message": summary}] if blocker_code else None
    )
    return _result(
        name,
        expected_status,
        language,
        summary,
        facts={"platform": platform_name, "simulated": True},
        blockers=blockers,
    )


def _skill_package_checks(repository: Path, name: str) -> tuple[dict[str, bool], str]:
    skill_file = repository / "skills" / name / "SKILL.md"
    skill_text = skill_file.read_text(encoding="utf-8") if skill_file.is_file() else ""
    return (
        {
            "skill_file": skill_file.is_file(),
            "frontmatter_name": f"name: {name}" in skill_text,
            "inputs": "## Inputs" in skill_text,
            "workflow": "## Workflow" in skill_text,
            "result": "## Result" in skill_text,
            "status_contract": "../references/status-contract.md" in skill_text,
        },
        skill_text,
    )


def _validate_orchestrator_package(repository: Path) -> dict[str, Any]:
    package_checks, skill_text = _skill_package_checks(repository, "open-dev-all")
    package_checks["action_routes"] = "## Action routing" in skill_text and all(
        f"`{action}`" in skill_text
        for action in ("setup", "build", "flash", "monitor", "all", "diagnose")
    )
    package_checks["runtime_resolver"] = "open_skill_runtime.py resolve" in skill_text
    return {
        "validation_status": "PASS" if all(package_checks.values()) else "FAILED",
        "package_checks": package_checks,
    }


def _validate_child_skills(repository: Path) -> dict[str, Any]:
    skill_results: dict[str, Any] = {}
    discovered_packages = {
        path.parent.name
        for path in (repository / "skills").glob("*/SKILL.md")
        if path.is_file()
    }
    expected_children = set(CHILD_SKILL_CASES)
    discovered_children = discovered_packages - {"open-dev-all"}
    coverage_checks = {
        "all_expected_children_present": expected_children <= discovered_children,
        "no_unvalidated_children": discovered_children == expected_children,
    }
    for name, (expected_status, _blocker_code, _summary) in CHILD_SKILL_CASES.items():
        package_checks, _skill_text = _skill_package_checks(repository, name)
        platform_results: dict[str, Any] = {}
        for platform_name in ("windows", "macos"):
            simulated_result = _simulated_child_result(name, platform_name)
            checks = {
                "skill": simulated_result["skill"] == name,
                "status": simulated_result["status"] == expected_status,
                "platform": simulated_result["facts"].get("platform") == platform_name,
                "no_side_effects": not any(simulated_result["side_effects"].values()),
            }
            platform_results[platform_name] = {
                "validation_status": (
                    "PASS" if all(checks.values()) else "FAILED"
                ),
                "checks": checks,
                "simulated_result": simulated_result,
            }
        validation_status = (
            "PASS"
            if all(package_checks.values())
            and all(
                result["validation_status"] == "PASS"
                for result in platform_results.values()
            )
            else "FAILED"
        )
        skill_results[name] = {
            "validation_status": validation_status,
            "expected_status": expected_status,
            "package_checks": package_checks,
            "platforms": platform_results,
        }
    passed = sum(
        result["validation_status"] == "PASS" for result in skill_results.values()
    )
    return {
        "validation_status": (
            "PASS"
            if passed == len(skill_results) and all(coverage_checks.values())
            else "FAILED"
        ),
        "passed": passed,
        "total": len(skill_results),
        "coverage_checks": coverage_checks,
        "discovered_children": sorted(discovered_children),
        "skills": skill_results,
    }


def _blocker_code(result: dict[str, Any]) -> str | None:
    blockers = result.get("blockers") or []
    return blockers[0].get("code") if blockers else None


def _validate_failure_flows() -> dict[str, Any]:
    cases: dict[str, dict[str, Any]] = {}

    def record_result_case(
        name: str,
        result: dict[str, Any],
        *,
        expected_status: str,
        expected_blocker: str,
    ) -> None:
        checks = {
            "status": result.get("status") == expected_status,
            "blocker": _blocker_code(result) == expected_blocker,
            "no_side_effects": not any(result.get("side_effects", {}).values()),
        }
        cases[name] = {
            "validation_status": "PASS" if all(checks.values()) else "FAILED",
            "checks": checks,
            "expected_status": expected_status,
            "expected_blocker": expected_blocker,
            "actual_result": result,
        }

    def record_propagation_case(
        name: str,
        resolved: dict[str, dict[str, Any]],
        *,
        blocked_steps: tuple[str, ...],
        expected_blocker: str,
    ) -> None:
        checks = {
            step: resolved.get(step, {}).get("status") == "BLOCKED"
            and _blocker_code(resolved[step]) == expected_blocker
            and not any(resolved[step].get("side_effects", {}).values())
            for step in blocked_steps
        }
        cases[name] = {
            "validation_status": "PASS" if all(checks.values()) else "FAILED",
            "checks": checks,
            "expected_status": "BLOCKED",
            "expected_blocker": expected_blocker,
            "blocked_steps": list(blocked_steps),
            "actual_steps": {step: resolved.get(step) for step in blocked_steps},
        }

    idf_failed = _result(
        "open-idf", "FAILED", "en", "Simulated ESP-IDF version mismatch."
    )
    record_propagation_case(
        "idf-failure-propagation",
        resolve_workflow(
            {"idf": idf_failed},
            language="en",
            context={
                "repository_available": True,
                "dependency_cache_available": True,
                "network_available": False,
            },
        ),
        blocked_steps=("components", "build", "flash"),
        expected_blocker="UPSTREAM_IDF_FAILED",
    )

    components_failed = _result(
        "open-components", "FAILED", "en", "Simulated component failure."
    )
    record_propagation_case(
        "component-failure-propagation",
        resolve_workflow(
            {"components": components_failed},
            language="en",
            context={
                "repository_available": True,
                "dependency_cache_available": True,
                "network_available": False,
            },
        ),
        blocked_steps=("build", "flash"),
        expected_blocker="UPSTREAM_COMPONENTS_FAILED",
    )

    build_failed = _result("open-build", "FAILED", "en", "Simulated build failure.")
    record_propagation_case(
        "build-failure-propagation",
        resolve_workflow({"build": build_failed}, language="en"),
        blocked_steps=("flash",),
        expected_blocker="UPSTREAM_BUILD_FAILED",
    )

    flash_failed = _result("open-flash", "FAILED", "en", "Simulated flash failure.")
    record_propagation_case(
        "flash-failure-propagation",
        resolve_workflow({"flash": flash_failed}, language="en"),
        blocked_steps=("monitor",),
        expected_blocker="UPSTREAM_FLASH_FAILED",
    )

    flash_cases = (
        (
            "multiple-ports-needs-user",
            evaluate_flash_readiness(
                language="en",
                build_succeeded=True,
                port_candidates=["simulated-port-1", "simulated-port-2"],
                device_write_confirmed=True,
                portable_entry_available=True,
            ),
            "NEEDS_USER",
            "PORT_NOT_UNIQUE",
        ),
        (
            "no-device-blocks",
            evaluate_flash_readiness(
                language="en",
                build_succeeded=True,
                port_candidates=[],
                device_write_confirmed=True,
                portable_entry_available=True,
            ),
            "BLOCKED",
            "NO_DEVICE",
        ),
        (
            "device-write-confirmation-required",
            evaluate_flash_readiness(
                language="en",
                build_succeeded=True,
                port_candidates=["simulated-port"],
                device_write_confirmed=False,
                portable_entry_available=True,
            ),
            "NEEDS_USER",
            "DEVICE_WRITE_NOT_CONFIRMED",
        ),
        (
            "s5-portable-entry-blocks",
            evaluate_flash_readiness(
                language="en",
                build_succeeded=True,
                port_candidates=["simulated-port"],
                device_write_confirmed=True,
                portable_entry_available=False,
            ),
            "BLOCKED",
            "S5_PORTABLE_ENTRY_PENDING",
        ),
    )
    for name, result, expected_status, expected_blocker in flash_cases:
        record_result_case(
            name,
            result,
            expected_status=expected_status,
            expected_blocker=expected_blocker,
        )

    passed = sum(
        case["validation_status"] == "PASS" for case in cases.values()
    )
    return {
        "validation_status": "PASS" if passed == len(cases) else "FAILED",
        "passed": passed,
        "total": len(cases),
        "cases": cases,
    }


def _markdown_code_lines(markdown: str) -> list[str]:
    code_lines: list[str] = []
    in_fence = False
    for line in markdown.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            code_lines.append(line.strip())
    return code_lines


def _validate_quick_start(repository: Path) -> dict[str, Any]:
    quick_start_path = repository / "docs" / "quick-start" / "README.md"
    quick_start_en_path = repository / "docs" / "quick-start" / "README.en.md"
    quick_start = (
        quick_start_path.read_text(encoding="utf-8")
        if quick_start_path.is_file()
        else ""
    )
    quick_start_en = (
        quick_start_en_path.read_text(encoding="utf-8")
        if quick_start_en_path.is_file()
        else ""
    )

    code_lines = _markdown_code_lines(quick_start) + _markdown_code_lines(
        quick_start_en
    )
    executable_flash_lines = [
        line
        for line in code_lines
        if ("idf.py" in line and "flash" in line)
        or ("build_example" in line and "flash" in line)
        or ("esptool" in line and ("erase" in line or "write" in line))
    ]
    public_scripts_absent = not (
        repository / "CODE" / "tools" / "build_example.ps1"
    ).exists() and not (
        repository / "CODE" / "tools" / "build_example_macos.sh"
    ).exists()
    private_script_boundary_documented = (
        "公开候选不包含上述维护者脚本" in quick_start
        and "public candidate does not contain the maintainer scripts" in quick_start_en
    )

    role_checks = {
        "maintainer": {
            "positioning": "维护者构建复现指南 + 外部阅读型预览 + 功能验收清单"
            in quick_start,
            "windows_build_entry": "build_example.ps1" in quick_start
            and "display_demo -Clean" in quick_start,
            "macos_build_entry": "build_example_macos.sh display_demo clean"
            in quick_start,
            "recorded_windows_evidence": "1421/1421" in quick_start,
            "english_positioning": "Maintainer build reproduction guide + external reading preview + functional acceptance checklist"
            in quick_start_en,
        },
        "external-reader": {
            "complete_flash_workflow": "完整烧录流程（阅读与实现对照）"
            in quick_start,
            "workflow_steps": all(
                token in quick_start
                for token in (
                    "成功构建证据",
                    "明确端口",
                    "全片擦除",
                    "设备写入确认",
                    "Hash校验",
                    "启动日志",
                )
            ),
            "implementation_links": all(
                link in quick_start
                for link in (
                    "../../skills/open-flash/SKILL.md",
                    "../../skills/references/workflow-contract.md",
                    "../../skills/references/status-contract.md",
                )
            ),
            "no_executable_flash_command": not executable_flash_lines,
            "public_script_boundary": public_scripts_absent
            or private_script_boundary_documented,
        },
        "functional-reviewer": {
            "acceptance_checklist": "功能验收清单" in quick_start,
            "evidence_layers": all(
                token in quick_start
                for token in ("构建证据", "烧录证据", "功能实机证据")
            ),
            "observable_colors": all(
                token in quick_start
                for token in (
                    "fill: white",
                    "fill: red",
                    "fill: green",
                    "fill: blue",
                )
            ),
            "negative_observations": all(
                token in quick_start
                for token in ("panic", "assert", "Task WDT", "异常复位")
            ),
            "evidence_boundary": "当前四色完整实机清单仍待复核" in quick_start,
        },
    }

    roles: dict[str, Any] = {}
    for name, checks in role_checks.items():
        roles[name] = {
            "validation_status": "PASS" if all(checks.values()) else "FAILED",
            "checks": checks,
        }
    passed = sum(role["validation_status"] == "PASS" for role in roles.values())
    return {
        "validation_status": "PASS" if passed == len(roles) else "FAILED",
        "passed": passed,
        "total": len(roles),
        "executable_flash_lines": executable_flash_lines,
        "roles": roles,
    }


def _result(
    skill: str,
    status: str,
    language: str,
    summary: str,
    *,
    facts: dict[str, Any] | None = None,
    evidence: list[str] | None = None,
    blockers: list[dict[str, str]] | None = None,
) -> dict[str, Any]:
    return make_result(
        skill=skill,
        status=status,
        language=language,
        summary=summary,
        facts=facts,
        evidence=evidence,
        blockers=blockers,
    )


def _fixture_steps(language: str, repository_available: bool) -> dict[str, dict[str, Any]]:
    if repository_available:
        pending = {
            "zh-CN": "S5 可移植构建入口仍处于挂起状态；本验证不执行构建。",
            "en": "The S5 portable build entry remains pending; this validation does not build.",
        }[language]
        observed = {
            "preflight": _result(
                "open-preflight",
                "PASS",
                language,
                "模拟环境事实已载入。" if language == "zh-CN" else "Simulated environment facts loaded.",
                facts={"simulated": True},
            ),
            "driver": _result("open-driver", "PASS", language, "driver fixture=ready"),
            "git": _result("open-git", "PASS", language, "git fixture=ready"),
            "idf": _result("open-idf", "PASS", language, "idf fixture=ready"),
            "clone": _result(
                "open-clone",
                "SKIPPED",
                language,
                "仓库夹具已存在。" if language == "zh-CN" else "Repository fixture already exists.",
            ),
            "components": _result("open-components", "PASS", language, "components fixture=ready"),
            "first_example": _result("open-first-example", "PASS", language, "example fixture=selected"),
            "build": _result(
                "open-build",
                "BLOCKED",
                language,
                pending,
                blockers=[{"code": "S5_PORTABLE_ENTRY_PENDING", "message": pending}],
            ),
        }
        context = {
            "network_available": False,
            "repository_available": True,
            "dependency_cache_available": True,
            "serial_available": True,
        }
    else:
        unavailable = "fixture=unavailable"
        observed = {
            "preflight": _result(
                "open-preflight",
                "PASS",
                language,
                "模拟空环境事实已载入。" if language == "zh-CN" else "Simulated empty-environment facts loaded.",
                facts={"simulated": True},
            ),
            "driver": _result("open-driver", "FAILED", language, unavailable),
            "git": _result("open-git", "FAILED", language, unavailable),
            "idf": _result("open-idf", "FAILED", language, unavailable),
        }
        context = {
            "network_available": False,
            "repository_available": False,
            "dependency_cache_available": False,
            "serial_available": False,
        }
    return resolve_workflow(observed, language=language, context=context)


def _live_readonly_steps(repository: Path) -> dict[str, dict[str, Any]]:
    markers = {
        "readme": (repository / "README.md").is_file(),
        "code": (repository / "CODE").is_dir(),
        "docs": (repository / "docs").is_dir(),
        "skills": (repository / "skills").is_dir(),
        "examples_manifest": (repository / "CODE" / "examples" / "examples.yml").is_file(),
        "display_demo_readme": (
            repository / "CODE" / "examples" / "display_demo" / "README.md"
        ).is_file(),
    }
    repository_ready = all(markers.values())
    pending = "S5 portable build entry is pending; build and device-write steps were not executed."
    observed = {
        "preflight": _result(
            "open-preflight",
            "PASS" if repository_ready else "FAILED",
            "en",
            "Public snapshot markers inspected read-only.",
            facts={
                "host_system": platform.system(),
                "host_machine": platform.machine(),
                "markers": markers,
            },
            evidence=["filesystem marker inspection only"],
        ),
        "git": _result(
            "open-git",
            "SKIPPED",
            "en",
            "Git execution is prohibited for this validation.",
            evidence=["public snapshot supplied as local input"],
        ),
        "clone": _result(
            "open-clone",
            "SKIPPED",
            "en",
            "Public snapshot already exists; no clone or pull was executed.",
        ),
        "components": _result(
            "open-components",
            "PASS" if markers["examples_manifest"] else "FAILED",
            "en",
            "Public component manifest marker inspected.",
        ),
        "first_example": _result(
            "open-first-example",
            "PASS" if markers["display_demo_readme"] else "FAILED",
            "en",
            "display_demo public README marker inspected.",
        ),
        "build": _result(
            "open-build",
            "BLOCKED",
            "en",
            pending,
            blockers=[{"code": "S5_PORTABLE_ENTRY_PENDING", "message": pending}],
        ),
    }
    return resolve_workflow(
        observed,
        language="en",
        context={
            "network_available": False,
            "repository_available": repository_ready,
            "dependency_cache_available": False,
            "serial_available": False,
        },
    )


def _prompt(language: str, mode: str) -> str:
    if language == "zh-CN":
        return (
            "# OPEN Skill 沙箱验证提示\n\n"
            "请从当前公开快照读取 `skills/open-dev-all/SKILL.md` 并按其契约处理本目录的场景事实。\n"
            f"场景模式：`{mode}`。只允许使用 `scenario.json` 和当前公开快照中的内容。\n"
            "禁止访问私有源仓库、Git、网络、构建工具或真实设备；不得在本场景目录之外写入。\n"
        )
    return (
        "# OPEN Skill sandbox validation prompt\n\n"
        "Read `skills/open-dev-all/SKILL.md` from the current public snapshot and apply its contract "
        "to the facts in this scenario directory.\n"
        f"Scenario mode: `{mode}`. Use only `scenario.json` and content from the public snapshot.\n"
        "Do not access a private source repository, Git, the network, build tools, or real devices. "
        "Do not write outside this scenario directory.\n"
    )


def _write_scenario(
    workspace: Path,
    *,
    name: str,
    language: str,
    scenario: dict[str, Any],
    steps: dict[str, dict[str, Any]],
    expectation: dict[str, Any],
) -> dict[str, Any]:
    scenario_root = workspace / name
    scenario_root.mkdir()
    _write_json(
        scenario_root / "scenario.json",
        {**scenario, "expected_workflow": expectation},
    )
    (scenario_root / "PROMPT.md").write_text(
        _prompt(language, scenario["mode"]), encoding="utf-8"
    )
    validation_status, validation_checks = _validate_expected_workflow(
        steps, expectation
    )
    result = {
        "scenario": name,
        "mode": scenario["mode"],
        "validation_status": validation_status,
        "aggregate_status": _aggregate_status(steps),
        "expected_aggregate_status": expectation["aggregate_status"],
        "validation_checks": validation_checks,
        "steps": steps,
        "side_effects": {
            "filesystem_write_scope": workspace.as_posix(),
            "network": False,
            "git": False,
            "build": False,
            "device_write": False,
        },
    }
    _write_json(scenario_root / "result.json", result)
    return result


def run_validation_workspace(workspace: Path, *, live_repository: Path) -> dict[str, Any]:
    """Create one isolated evidence workspace and return its summary."""

    workspace = workspace.resolve()
    live_repository = live_repository.resolve()
    if workspace.exists():
        raise FileExistsError(f"validation workspace already exists: {workspace}")
    if not live_repository.is_dir():
        raise FileNotFoundError(f"public repository snapshot not found: {live_repository}")
    workspace.mkdir(parents=True)

    fixture_results: dict[str, dict[str, Any]] = {}
    for name, operating_system, language, repository_available in FIXTURE_SCENARIOS:
        scenario = {
            "name": name,
            "mode": "simulated-fixture",
            "simulated": True,
            "operating_system": operating_system,
            "platform": operating_system,
            "language": language,
            "example": "display_demo",
            "action": "all",
            "idf_version": "v5.5.4",
            "continue_policy": "dependency-aware",
            "port": None,
            "repository_available": repository_available,
            "network_available": False,
            "allow_git": False,
            "allow_build": False,
            "allow_device_write": False,
        }
        fixture_results[name] = _write_scenario(
            workspace,
            name=name,
            language=language,
            scenario=scenario,
            steps=_fixture_steps(language, repository_available),
            expectation=_fixture_expectation(repository_available),
        )

    live_scenario = {
        "name": "live-readonly",
        "mode": "live-readonly",
        "simulated": False,
        "platform": (
            "windows"
            if platform.system() == "Windows"
            else "macos"
            if platform.system() == "Darwin"
            else "unsupported"
        ),
        "language": "en",
        "example": "display_demo",
        "action": "all",
        "idf_version": "v5.5.4",
        "continue_policy": "dependency-aware",
        "port": None,
        "repository": live_repository.as_posix(),
        "network_available": False,
        "allow_git": False,
        "allow_build": False,
        "allow_device_write": False,
    }
    live_result = _write_scenario(
        workspace,
        name="live-readonly",
        language="en",
        scenario=live_scenario,
        steps=_live_readonly_steps(live_repository),
        expectation=_live_expectation(),
    )

    platform_validation: dict[str, dict[str, Any]] = {}
    for operating_system in ("windows", "macos"):
        names = [f"{operating_system}-empty", f"{operating_system}-existing"]
        passed = sum(
            fixture_results[name]["validation_status"] == "PASS" for name in names
        )
        platform_validation[operating_system] = {
            "status": "PASS" if passed == len(names) else "FAILED",
            "passed": passed,
            "total": len(names),
            "scenarios": names,
        }

    orchestrator_package_validation = _validate_orchestrator_package(live_repository)
    failure_flow_validation = _validate_failure_flows()
    _write_json(
        workspace / "failure-flow-validation.json",
        failure_flow_validation,
    )
    quick_start_validation = _validate_quick_start(live_repository)
    _write_json(
        workspace / "quick-start-validation.json",
        quick_start_validation,
    )
    orchestrator_validation_status = (
        "PASS"
        if all(
            result["status"] == "PASS" for result in platform_validation.values()
        )
        and live_result["validation_status"] == "PASS"
        and orchestrator_package_validation["validation_status"] == "PASS"
        and failure_flow_validation["validation_status"] == "PASS"
        and quick_start_validation["validation_status"] == "PASS"
        else "FAILED"
    )
    child_skill_validation = _validate_child_skills(live_repository)
    _write_json(
        workspace / "child-skill-validation.json",
        child_skill_validation,
    )
    overall_validation_status = (
        "PASS"
        if orchestrator_validation_status == "PASS"
        and child_skill_validation["validation_status"] == "PASS"
        else "FAILED"
    )

    summary = {
        "overall_validation_status": overall_validation_status,
        "orchestrator_validation_status": orchestrator_validation_status,
        "orchestrator_package_validation": {
            "status": orchestrator_package_validation["validation_status"],
            "checks": orchestrator_package_validation["package_checks"],
        },
        "child_skill_validation": {
            "status": child_skill_validation["validation_status"],
            "passed": child_skill_validation["passed"],
            "total": child_skill_validation["total"],
        },
        "failure_flow_validation": {
            "status": failure_flow_validation["validation_status"],
            "passed": failure_flow_validation["passed"],
            "total": failure_flow_validation["total"],
        },
        "quick_start_validation": {
            "status": quick_start_validation["validation_status"],
            "passed": quick_start_validation["passed"],
            "total": quick_start_validation["total"],
        },
        "platform_validation": platform_validation,
        "live_readonly_validation_status": live_result["validation_status"],
        "fixture_scenarios": len(FIXTURE_SCENARIOS),
        "live_readonly_scenarios": 1,
        "filesystem_write_scope": workspace.as_posix(),
        "network": False,
        "git": False,
        "build": False,
        "device_write": False,
    }
    _write_json(workspace / "summary.json", summary)
    (workspace / "REPORT.md").write_text(
        "# OPEN Skill isolated validation\n\n"
        f"- Overall simulated validation: {overall_validation_status}.\n"
        f"- Windows simulated validation: {platform_validation['windows']['status']} "
        f"({platform_validation['windows']['passed']}/{platform_validation['windows']['total']}).\n"
        f"- macOS simulated validation: {platform_validation['macos']['status']} "
        f"({platform_validation['macos']['passed']}/{platform_validation['macos']['total']}).\n"
        f"- Live public-snapshot read-only validation: {live_result['validation_status']}.\n"
        f"- Orchestrator package validation: {orchestrator_package_validation['validation_status']}.\n"
        f"- Independently callable child Skills: {child_skill_validation['validation_status']} "
        f"({child_skill_validation['passed']}/{child_skill_validation['total']}) on both simulated platforms.\n"
        f"- Failure and safety flows: {failure_flow_validation['validation_status']} "
        f"({failure_flow_validation['passed']}/{failure_flow_validation['total']}).\n"
        f"- Quick Start reading roles: {quick_start_validation['validation_status']} "
        f"({quick_start_validation['passed']}/{quick_start_validation['total']}).\n"
        "- Four deterministic fixtures generated: Windows/macOS × empty/existing repository.\n"
        "- Workflow FAILED/BLOCKED values are expected fixture outcomes, not validation failures.\n"
        "- One live read-only public snapshot marker inspection generated.\n"
        "- Git, network access, builds, and device writes were not executed.\n"
        "- macOS coverage is simulated and is not a claim of native macOS execution.\n",
        encoding="utf-8",
    )
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--live-repository", required=True, type=Path)
    args = parser.parse_args(argv)
    summary = run_validation_workspace(
        args.workspace,
        live_repository=args.live_repository,
    )
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if summary["overall_validation_status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
