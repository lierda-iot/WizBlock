#!/usr/bin/env python3
"""Summarize EX-024 audio diagnostics without changing firmware behavior."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional


KEY_VALUE_RE = re.compile(r"(?P<key>[A-Za-z_][A-Za-z0-9_]*)=(?P<value>[^\s]+)")


def parse_number(value: str) -> Optional[float]:
    match = re.match(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)", value.rstrip(","))
    if match is None:
        return None
    return float(match.group(0))


def parse_fields(line: str) -> Dict[str, str]:
    return {
        match.group("key"): match.group("value")
        for match in KEY_VALUE_RE.finditer(line)
    }


def number(fields: Dict[str, str], key: str) -> Optional[float]:
    value = fields.get(key)
    return None if value is None else parse_number(value)


def add(values: List[float], value: Optional[float]) -> None:
    if value is not None:
        values.append(value)


def maximum(values: Iterable[float]) -> Optional[float]:
    values = list(values)
    return max(values) if values else None


def minimum(values: Iterable[float]) -> Optional[float]:
    values = list(values)
    return min(values) if values else None


def average(values: Iterable[float]) -> Optional[float]:
    values = list(values)
    return sum(values) / len(values) if values else None


def fmt(value: Optional[float], digits: int = 2) -> str:
    return "n/a" if value is None else f"{value:.{digits}f}"


def analyze(lines: Iterable[str]) -> Dict[str, object]:
    feed_processor: List[float] = []
    fetch_processor: List[float] = []
    feed_gap: List[float] = []
    fetch_gap: List[float] = []
    feed_rate: List[float] = []
    fetch_rate: List[float] = []
    capture_ratios: List[float] = []
    late_blocks: List[float] = []
    mic1_clips: List[float] = []
    mic2_clips: List[float] = []
    ref_clips: List[float] = []
    mic1_corr: List[float] = []
    mic2_corr: List[float] = []
    aec_feed_processor: List[float] = []
    no_aec_feed_processor: List[float] = []
    aec_fetch_processor: List[float] = []
    no_aec_fetch_processor: List[float] = []
    aec_mic1_clips: List[float] = []
    aec_mic2_clips: List[float] = []
    no_aec_mic1_clips: List[float] = []
    no_aec_mic2_clips: List[float] = []
    aec_windows = 0
    playback_windows = 0
    playback_wakenet_contexts = 0
    wakenet_events = 0
    wakenet_aec_contexts = 0
    vad_starts = 0
    vad_ends = 0
    signal_windows = 0
    signal_lines = 0

    for raw_line in lines:
        fields = parse_fields(raw_line.strip())
        phase = fields.get("phase")
        event = fields.get("event")
        aec_on = fields.get("aec") == "1"
        playback_context = (
            aec_on
            and fields.get("owner") == "2"
            and fields.get("output_phase") == "1"
        )

        if phase == "feed":
            processor_max = number(fields, "processor_max")
            mic1_clip = number(fields, "mic1_clip")
            mic2_clip = number(fields, "mic2_clip")
            add(feed_processor, processor_max)
            add(feed_gap, number(fields, "max_gap"))
            add(feed_rate, number(fields, "rate"))
            add(mic1_clips, mic1_clip)
            add(mic2_clips, mic2_clip)
            add(ref_clips, number(fields, "ref_raw_clip"))
            if aec_on:
                aec_windows += 1
                add(aec_feed_processor, processor_max)
                add(aec_mic1_clips, mic1_clip)
                add(aec_mic2_clips, mic2_clip)
            else:
                add(no_aec_feed_processor, processor_max)
                add(no_aec_mic1_clips, mic1_clip)
                add(no_aec_mic2_clips, mic2_clip)
            if playback_context:
                playback_windows += 1
        elif phase == "fetch":
            processor_max = number(fields, "processor_max")
            add(fetch_processor, processor_max)
            add(fetch_gap, number(fields, "max_gap"))
            add(fetch_rate, number(fields, "rate"))
            if aec_on:
                aec_windows += 1
                add(aec_fetch_processor, processor_max)
            else:
                add(no_aec_fetch_processor, processor_max)
            if playback_context:
                playback_windows += 1
        elif phase == "playback" and playback_context:
            playback_windows += 1

        if "capture_ratio" in fields:
            add(capture_ratios, number(fields, "capture_ratio"))
        if "late_blocks" in fields:
            add(late_blocks, number(fields, "late_blocks"))

        if "mic1_ref" in fields or "mic2_ref" in fields:
            signal_lines += 1
            for key, target in (("mic1_ref", mic1_corr), ("mic2_ref", mic2_corr)):
                for raw_value in fields.get(key, "").split(","):
                    value = parse_number(raw_value)
                    if value is not None:
                        target.append(value)
            if "capture_ratio" in fields:
                signal_windows += 1

        if event == "wakenet":
            wakenet_events += 1
            if aec_on:
                wakenet_aec_contexts += 1
            if playback_context:
                playback_wakenet_contexts += 1
        elif event == "vad_start":
            vad_starts += 1
        elif event == "vad_end":
            vad_ends += 1

    return {
        "feed_windows": len(feed_processor),
        "fetch_windows": len(fetch_processor),
        "aec_windows": aec_windows,
        "playback_windows": playback_windows,
        "signal_windows": signal_windows,
        "signal_lines": signal_lines,
        "wakenet_events": wakenet_events,
        "wakenet_aec_contexts": wakenet_aec_contexts,
        "playback_wakenet_contexts": playback_wakenet_contexts,
        "vad_starts": vad_starts,
        "vad_ends": vad_ends,
        "feed_processor_max_ms": maximum(feed_processor),
        "fetch_processor_max_ms": maximum(fetch_processor),
        "feed_gap_max_ms": maximum(feed_gap),
        "fetch_gap_max_ms": maximum(fetch_gap),
        "feed_rate_min": minimum(feed_rate),
        "fetch_rate_min": minimum(fetch_rate),
        "capture_ratio_min": minimum(capture_ratios),
        "late_blocks_max": maximum(late_blocks),
        "mic1_clip_max": maximum(mic1_clips),
        "mic2_clip_max": maximum(mic2_clips),
        "ref_clip_max": maximum(ref_clips),
        "mic1_corr_abs_max": maximum(abs(value) for value in mic1_corr),
        "mic2_corr_abs_max": maximum(abs(value) for value in mic2_corr),
        "mic1_corr_abs_avg": average(abs(value) for value in mic1_corr),
        "mic2_corr_abs_avg": average(abs(value) for value in mic2_corr),
        "aec_feed_processor_max_ms": maximum(aec_feed_processor),
        "no_aec_feed_processor_max_ms": maximum(no_aec_feed_processor),
        "aec_fetch_processor_max_ms": maximum(aec_fetch_processor),
        "no_aec_fetch_processor_max_ms": maximum(no_aec_fetch_processor),
        "aec_feed_processor_avg_ms": average(aec_feed_processor),
        "no_aec_feed_processor_avg_ms": average(no_aec_feed_processor),
        "aec_fetch_processor_avg_ms": average(aec_fetch_processor),
        "no_aec_fetch_processor_avg_ms": average(no_aec_fetch_processor),
        "aec_mic1_clip_max": maximum(aec_mic1_clips),
        "aec_mic2_clip_max": maximum(aec_mic2_clips),
        "no_aec_mic1_clip_max": maximum(no_aec_mic1_clips),
        "no_aec_mic2_clip_max": maximum(no_aec_mic2_clips),
    }


def render_report(
    path: Path, result: Dict[str, object], expected_playback_wakes: int
) -> str:
    lines = [
        f"# EX-024 Audio Diagnostics: {path.name}",
        "",
        "这是日志回放统计，不替代 WakeNet 模型或实机听感验收。",
        "",
        "## 事件",
        "",
        f"- feed windows: {result['feed_windows']}; fetch windows: {result['fetch_windows']}",
        f"- AEC-on windows: {result['aec_windows']}; playback-context windows: {result['playback_windows']}",
        f"- signal windows: {result['signal_windows']}; signal lines: {result['signal_lines']}",
        f"- WakeNet events: {result['wakenet_events']}; AEC-context hits: {result['wakenet_aec_contexts']}",
        f"- expected playback WakeNet hits: {expected_playback_wakes}; observed: {result['playback_wakenet_contexts']}",
        f"- VAD start/end: {result['vad_starts']}/{result['vad_ends']}",
        "",
        "## 实时性和信号质量",
        "",
        "| 指标 | 结果 | 解释 |",
        "|---|---:|---|",
        f"| feed processor max (ms) | {fmt(result['feed_processor_max_ms'])} | 64 ms 输入块周期的对照 |",
        f"| fetch processor max (ms) | {fmt(result['fetch_processor_max_ms'])} | 64 ms 输入块周期的对照 |",
        f"| AEC ON feed processor max (ms) | {fmt(result['aec_feed_processor_max_ms'])} | 播放期处理压力 |",
        f"| AEC OFF feed processor max (ms) | {fmt(result['no_aec_feed_processor_max_ms'])} | 无 AEC 对照 |",
        f"| AEC ON fetch processor max (ms) | {fmt(result['aec_fetch_processor_max_ms'])} | 播放期处理压力 |",
        f"| AEC OFF fetch processor max (ms) | {fmt(result['no_aec_fetch_processor_max_ms'])} | 无 AEC 对照 |",
        f"| AEC ON/OFF feed processor avg (ms) | {fmt(result['aec_feed_processor_avg_ms'])}/{fmt(result['no_aec_feed_processor_avg_ms'])} | 同一日志内对照 |",
        f"| AEC ON/OFF fetch processor avg (ms) | {fmt(result['aec_fetch_processor_avg_ms'])}/{fmt(result['no_aec_fetch_processor_avg_ms'])} | 同一日志内对照 |",
        f"| feed max gap (ms) | {fmt(result['feed_gap_max_ms'])} | 大于块周期表示存在积压风险 |",
        f"| fetch max gap (ms) | {fmt(result['fetch_gap_max_ms'])} | 大于块周期表示存在积压风险 |",
        f"| capture ratio min | {fmt(result['capture_ratio_min'], 3)} | 低于 0.90 标记采样不足 |",
        f"| late blocks max | {fmt(result['late_blocks_max'], 0)} | 非零表示存在迟到块 |",
        f"| MIC1 clip max | {fmt(result['mic1_clip_max'], 0)} | 非零表示 AEC 前已削波 |",
        f"| MIC2 clip max | {fmt(result['mic2_clip_max'], 0)} | 非零表示 AEC 前已削波 |",
        f"| AEC ON MIC1/MIC2 clip max | {fmt(result['aec_mic1_clip_max'], 0)}/{fmt(result['aec_mic2_clip_max'], 0)} | 播放期削波 |",
        f"| AEC OFF MIC1/MIC2 clip max | {fmt(result['no_aec_mic1_clip_max'], 0)}/{fmt(result['no_aec_mic2_clip_max'], 0)} | 无播放对照 |",
        f"| REF clip max | {fmt(result['ref_clip_max'], 0)} | REF 自身削波 |",
        f"| MIC1/REF mean abs corr | {fmt(result['mic1_corr_abs_avg'], 3)} | 多窗口多延迟的平均绝对相关 |",
        f"| MIC2/REF mean abs corr | {fmt(result['mic2_corr_abs_avg'], 3)} | 多窗口多延迟的平均绝对相关 |",
        f"| MIC1/REF max abs corr | {fmt(result['mic1_corr_abs_max'], 3)} | 日志窗口 probe，不是全波形最优相关 |",
        f"| MIC2/REF max abs corr | {fmt(result['mic2_corr_abs_max'], 3)} | 日志窗口 probe，不是全波形最优相关 |",
        "",
        "## 诊断判定",
        "",
    ]
    if result["playback_windows"] and not result["playback_wakenet_contexts"]:
        lines.append(
            "- 播放上下文存在，但日志没有在相同状态字段下记录 WakeNet 命中；需要同步 PCM 复测。"
        )
    if expected_playback_wakes > result["playback_wakenet_contexts"]:
        lines.append(
            "- TALK 唤醒门禁：FAIL，播放上下文 WakeNet 命中少于预期次数。"
        )
    if (
        result["capture_ratio_min"] is not None
        and result["capture_ratio_min"] < 0.90
    ) or (
        result["feed_processor_max_ms"] is not None
        and result["feed_processor_max_ms"] > 64.0
    ):
        lines.append("- 实时性门禁：FAIL，存在采样不足或处理超出 64 ms 块周期。")
    else:
        lines.append("- 实时性门禁：PASS，日志窗口未发现采样不足或处理超时。")
    if (result["mic1_clip_max"] or 0) > 0 or (result["mic2_clip_max"] or 0) > 0:
        lines.append("- 动态范围门禁：FAIL，MIC 播放期存在削波。")
    else:
        lines.append("- 动态范围门禁：PASS，日志窗口未发现 MIC 削波。")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--expected-playback-wakes", type=int, default=0)
    args = parser.parse_args()
    if not args.log.is_file():
        print(f"log file not found: {args.log}", file=sys.stderr)
        return 2

    text = args.log.read_text(encoding="utf-8", errors="replace")
    result = analyze(text.splitlines())
    if args.expected_playback_wakes < 0:
        print("expected playback wakes must be non-negative", file=sys.stderr)
        return 2
    report = render_report(args.log, result, args.expected_playback_wakes)
    output = args.output or args.log.with_suffix(".audio-diagnostics.md")
    output.write_text(report, encoding="utf-8")
    print(report, end="")

    if not args.strict:
        return 0
    realtime_fail = (
        result["capture_ratio_min"] is not None
        and result["capture_ratio_min"] < 0.90
    ) or (
        result["feed_processor_max_ms"] is not None
        and result["feed_processor_max_ms"] > 64.0
    )
    clipping_fail = (
        (result["mic1_clip_max"] or 0) > 0
        or (result["mic2_clip_max"] or 0) > 0
    )
    missing_data = (
        result["feed_windows"] == 0
        or result["fetch_windows"] == 0
        or result["signal_lines"] == 0
        or result["playback_windows"] == 0
    )
    wake_fail = (
        result["playback_wakenet_contexts"] < args.expected_playback_wakes
    )
    return 1 if realtime_fail or clipping_fail or missing_data or wake_fail else 0


if __name__ == "__main__":
    raise SystemExit(main())
