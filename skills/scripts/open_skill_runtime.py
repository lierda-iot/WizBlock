#!/usr/bin/env python3
"""Shared state contract and dependency propagation for OPEN Skills."""

from __future__ import annotations

import argparse
import json
import re
import sys
from copy import deepcopy
from typing import Any, Mapping


SUPPORTED_LANGUAGES = {"zh-CN", "en"}
SUPPORTED_STATUSES = {"PASS", "SKIPPED", "FAILED", "BLOCKED", "NEEDS_USER"}
SIDE_EFFECT_KEYS = {"filesystem_write", "network", "device_write"}

_MESSAGES = {
    "UPSTREAM_GIT_FAILED": {
        "zh-CN": "Git 不可用且本地没有仓库，源码相关步骤被阻断。",
        "en": "Git is unavailable and no local repository exists, so source-dependent steps are blocked.",
    },
    "UPSTREAM_IDF_FAILED": {
        "zh-CN": "ESP-IDF 未就绪，依赖、构建和烧录步骤被阻断。",
        "en": "ESP-IDF is not ready, so component, build, and flash steps are blocked.",
    },
    "UPSTREAM_COMPONENTS_FAILED": {
        "zh-CN": "组件依赖准备失败，构建和烧录步骤被阻断。",
        "en": "Component preparation failed, so build and flash steps are blocked.",
    },
    "SERIAL_UNAVAILABLE": {
        "zh-CN": "串口设备不可见，烧录和监控步骤被阻断；构建仍可独立继续。",
        "en": "No serial device is visible, so flash and monitor are blocked; build may continue independently.",
    },
    "NETWORK_UNAVAILABLE": {
        "zh-CN": "网络不可用且缺少已确认的本地仓库或依赖缓存，需要下载的步骤被阻断。",
        "en": "The network is unavailable and confirmed local repository or dependency inputs are missing, so download-dependent steps are blocked.",
    },
    "UPSTREAM_BUILD_FAILED": {
        "zh-CN": "构建失败，禁止继续烧录。",
        "en": "The build failed, so flashing must not continue.",
    },
    "UPSTREAM_FLASH_FAILED": {
        "zh-CN": "烧录失败，依赖本次烧录成功的监控被阻断；需要先诊断且不得自动重试。",
        "en": "Flashing failed, so monitoring that depends on this flash is blocked; diagnose first and do not retry automatically.",
    },
    "PORT_NOT_UNIQUE": {
        "zh-CN": "发现多个候选串口，需要用户明确选择目标设备。",
        "en": "Multiple serial ports were found; the user must select the target device.",
    },
    "NO_DEVICE": {
        "zh-CN": "未发现可确认的目标设备，烧录被阻断。",
        "en": "No target device is available for confirmation, so flashing is blocked.",
    },
    "DEVICE_WRITE_NOT_CONFIRMED": {
        "zh-CN": "目标设备写入尚未获得用户明确确认。",
        "en": "Writing to the target device has not been explicitly confirmed by the user.",
    },
    "S5_PORTABLE_ENTRY_PENDING": {
        "zh-CN": "最终 S5 尚未提供并验证公开可移植入口，禁止执行自动烧录。",
        "en": "Final S5 has not yet provided and validated a public portable entry, so automated flashing is blocked.",
    },
    "FLASH_READY": {
        "zh-CN": "烧录前置条件已满足；本检查未写入设备。",
        "en": "Flash prerequisites are satisfied; this check did not write to the device.",
    },
}

_STEP_SKILLS = {
    "clone": "open-clone",
    "components": "open-components",
    "first_example": "open-first-example",
    "build": "open-build",
    "flash": "open-flash",
    "monitor": "open-monitor",
}


def redact_text(text: str) -> str:
    """Redact credentials, complete device identifiers, and user-home paths."""

    redacted = re.sub(
        r"(?i)\b(wifi_?password|password|token|api_?key|secret)\s*=\s*(?:\"[^\"]*\"|'[^']*'|[^\s]+)",
        r"\1=<REDACTED>",
        text,
    )
    redacted = re.sub(
        r"(?i)(Authorization\s*:\s*Bearer\s+)[^\s]+",
        r"\1<REDACTED>",
        redacted,
    )
    windows_home_pattern = r"(?i)\b[A-Za-z]:" + r"\\+Users\\+[^\\\s]+"
    macos_home_pattern = "/" + "Users" + r"/[^/\s]+"
    redacted = re.sub(
        windows_home_pattern,
        "<USER_HOME>",
        redacted,
    )
    redacted = re.sub(macos_home_pattern, "<USER_HOME>", redacted)
    redacted = re.sub(
        r"(?i)\b(?:[0-9A-F]{2}:){5}[0-9A-F]{2}\b",
        "<DEVICE_ID>",
        redacted,
    )
    return redacted


def make_result(
    *,
    skill: str,
    status: str,
    language: str,
    summary: str,
    facts: Mapping[str, Any] | None = None,
    evidence: list[str] | None = None,
    blockers: list[dict[str, str]] | None = None,
    next_actions: list[str] | None = None,
    side_effects: Mapping[str, bool] | None = None,
) -> dict[str, Any]:
    """Create one complete, validated Skill result."""

    if not skill.startswith("open-"):
        raise ValueError(f"invalid skill name: {skill}")
    if status not in SUPPORTED_STATUSES:
        raise ValueError(f"unsupported status: {status}")
    if language not in SUPPORTED_LANGUAGES:
        raise ValueError(f"unsupported language: {language}")
    if not summary.strip():
        raise ValueError("summary must not be empty")

    effects = dict(side_effects or {key: False for key in SIDE_EFFECT_KEYS})
    if set(effects) != SIDE_EFFECT_KEYS or not all(
        isinstance(value, bool) for value in effects.values()
    ):
        raise ValueError(f"side_effects must contain boolean keys: {sorted(SIDE_EFFECT_KEYS)}")

    return {
        "schema_version": "1.0",
        "skill": skill,
        "status": status,
        "language": language,
        "summary": summary,
        "facts": deepcopy(dict(facts or {})),
        "evidence": list(evidence or []),
        "blockers": deepcopy(list(blockers or [])),
        "next_actions": list(next_actions or []),
        "side_effects": effects,
    }


def evaluate_flash_readiness(
    *,
    language: str,
    build_succeeded: bool,
    port_candidates: list[str],
    device_write_confirmed: bool,
    portable_entry_available: bool,
) -> dict[str, Any]:
    """Evaluate flash prerequisites without writing to a device."""

    if language not in SUPPORTED_LANGUAGES:
        raise ValueError(f"unsupported language: {language}")
    if not build_succeeded:
        message = _MESSAGES["UPSTREAM_BUILD_FAILED"][language]
        return make_result(
            skill="open-flash",
            status="BLOCKED",
            language=language,
            summary=message,
            blockers=[{"code": "UPSTREAM_BUILD_FAILED", "message": message}],
        )
    if len(port_candidates) > 1:
        message = _MESSAGES["PORT_NOT_UNIQUE"][language]
        return make_result(
            skill="open-flash",
            status="NEEDS_USER",
            language=language,
            summary=message,
            facts={"port_candidates": list(port_candidates)},
            blockers=[{"code": "PORT_NOT_UNIQUE", "message": message}],
        )
    if not port_candidates:
        message = _MESSAGES["NO_DEVICE"][language]
        return make_result(
            skill="open-flash",
            status="BLOCKED",
            language=language,
            summary=message,
            blockers=[{"code": "NO_DEVICE", "message": message}],
        )
    if not device_write_confirmed:
        message = _MESSAGES["DEVICE_WRITE_NOT_CONFIRMED"][language]
        return make_result(
            skill="open-flash",
            status="NEEDS_USER",
            language=language,
            summary=message,
            facts={"port_candidates": list(port_candidates)},
            blockers=[{"code": "DEVICE_WRITE_NOT_CONFIRMED", "message": message}],
        )
    if not portable_entry_available:
        message = _MESSAGES["S5_PORTABLE_ENTRY_PENDING"][language]
        return make_result(
            skill="open-flash",
            status="BLOCKED",
            language=language,
            summary=message,
            facts={"port_candidates": list(port_candidates)},
            blockers=[{"code": "S5_PORTABLE_ENTRY_PENDING", "message": message}],
        )
    return make_result(
        skill="open-flash",
        status="PASS",
        language=language,
        summary=_MESSAGES["FLASH_READY"][language],
        facts={"selected_port": port_candidates[0]},
        evidence=["build=PASS", "device_write_confirmation=present"],
    )


def resolve_workflow(
    observed: Mapping[str, Mapping[str, Any]],
    *,
    language: str,
    context: Mapping[str, Any] | None = None,
) -> dict[str, dict[str, Any]]:
    """Return observed results plus dependency-derived blocked steps."""

    if language not in SUPPORTED_LANGUAGES:
        raise ValueError(f"unsupported language: {language}")

    resolved = deepcopy(dict(observed))
    workflow_context = dict(context or {})

    def block_steps(steps: tuple[str, ...], code: str) -> None:
        message = _MESSAGES[code][language]
        for step in steps:
            if step in resolved:
                continue
            resolved[step] = make_result(
                skill=_STEP_SKILLS[step],
                status="BLOCKED",
                language=language,
                summary=message,
                blockers=[{"code": code, "message": message}],
            )

    git = resolved.get("git")
    if (
        git
        and git.get("status") in {"FAILED", "BLOCKED"}
        and not workflow_context.get("repository_available", False)
    ):
        block_steps(
            ("clone", "components", "first_example", "build", "flash"),
            "UPSTREAM_GIT_FAILED",
        )

    driver = resolved.get("driver")
    if (
        driver
        and driver.get("status") in {"FAILED", "BLOCKED"}
        and workflow_context.get("serial_available") is False
    ):
        block_steps(("flash", "monitor"), "SERIAL_UNAVAILABLE")

    if workflow_context.get("network_available") is False:
        repository_available = workflow_context.get("repository_available", False)
        cache_available = workflow_context.get("dependency_cache_available", False)
        if not repository_available:
            block_steps(("clone", "first_example"), "NETWORK_UNAVAILABLE")
        if not repository_available or not cache_available:
            block_steps(("components", "build", "flash"), "NETWORK_UNAVAILABLE")

    idf = resolved.get("idf")
    if idf and idf.get("status") in {"FAILED", "BLOCKED"}:
        block_steps(("components", "build", "flash"), "UPSTREAM_IDF_FAILED")

    components = resolved.get("components")
    if components and components.get("status") in {"FAILED", "BLOCKED"}:
        block_steps(("build", "flash"), "UPSTREAM_COMPONENTS_FAILED")

    build = resolved.get("build")
    if build and build.get("status") == "FAILED":
        block_steps(("flash",), "UPSTREAM_BUILD_FAILED")
    elif build and build.get("status") == "BLOCKED" and "flash" not in resolved:
        build_blockers = deepcopy(build.get("blockers") or [])
        resolved["flash"] = make_result(
            skill=_STEP_SKILLS["flash"],
            status="BLOCKED",
            language=language,
            summary=build.get("summary", ""),
            blockers=build_blockers,
        )

    flash = resolved.get("flash")
    if flash and flash.get("status") == "FAILED":
        block_steps(("monitor",), "UPSTREAM_FLASH_FAILED")
    return resolved


def main(argv: list[str] | None = None) -> int:
    """Run the stateless JSON command-line interface."""

    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    resolve_parser = subparsers.add_parser("resolve", help="resolve workflow dependencies from stdin JSON")
    resolve_parser.add_argument("--language", choices=sorted(SUPPORTED_LANGUAGES), required=True)
    args = parser.parse_args(argv)

    try:
        request = json.load(sys.stdin)
        response = resolve_workflow(
            request.get("observed", {}),
            language=args.language,
            context=request.get("context", {}),
        )
    except (json.JSONDecodeError, TypeError, ValueError) as error:
        print(f"invalid request: {error}", file=sys.stderr)
        return 2

    json.dump(response, sys.stdout, ensure_ascii=False, sort_keys=True)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
