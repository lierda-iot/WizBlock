"""Authenticated HTTP API for ESP32 task-state polling."""

from __future__ import annotations

import hmac
import json
import threading
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler
from typing import Any, Type
from urllib.parse import urlsplit

from .model import TaskStore


MAX_UINT64 = 18_446_744_073_709_551_615
MAX_RESPONSE_BYTES = 12_288


@dataclass(frozen=True, slots=True)
class ApiStatsSnapshot:
    requests: int
    bad_requests: int
    unauthorized: int
    not_found: int
    server_errors: int


class ApiStats:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._counts = {200: 0, 400: 0, 401: 0, 404: 0, 500: 0}

    def record(self, status: int) -> None:
        with self._lock:
            self._counts[status] = self._counts.get(status, 0) + 1

    def snapshot(self) -> ApiStatsSnapshot:
        with self._lock:
            return ApiStatsSnapshot(
                requests=sum(self._counts.values()),
                bad_requests=self._counts.get(400, 0),
                unauthorized=self._counts.get(401, 0),
                not_found=self._counts.get(404, 0),
                server_errors=self._counts.get(500, 0),
            )


def parse_after_event_seq(query: str) -> int:
    if query == "":
        return 0
    parts = query.split("&")
    if len(parts) != 1:
        raise ValueError("query must contain one value")
    key, separator, value = parts[0].partition("=")
    if separator != "=" or key != "after_event_seq" or not value:
        raise ValueError("invalid query field")
    if not value.isascii() or not value.isdecimal():
        raise ValueError("cursor must be unsigned decimal ASCII")
    parsed = int(value, 10)
    if parsed > MAX_UINT64:
        raise ValueError("cursor overflow")
    return parsed


def make_handler(
    store: TaskStore, token: str, stats: ApiStats | None = None
) -> Type[BaseHTTPRequestHandler]:
    class StateHandler(BaseHTTPRequestHandler):
        server_version = "CodexTaskBridge/0.1"

        def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            parsed_url = urlsplit(self.path)
            if parsed_url.path != "/api/v1/state":
                self._send_json(404, {"error": "not_found"})
                return

            supplied_token = self.headers.get("X-Codex-Notifier-Token", "")
            if not hmac.compare_digest(supplied_token, token):
                self._send_json(401, {"error": "unauthorized"})
                return

            try:
                after_event_seq = parse_after_event_seq(parsed_url.query)
            except ValueError:
                self._send_json(400, {"error": "invalid_after_event_seq"})
                return

            payload = store.snapshot(after_event_seq, int(time.time() * 1000))
            encoded = self._encode_with_budget(payload)
            if encoded is None:
                self._send_json(500, {"error": "response_too_large"})
                return
            self._send_encoded(200, encoded)

        def _encode_with_budget(self, payload: dict[str, Any]) -> bytes | None:
            while True:
                encoded = json.dumps(
                    payload, ensure_ascii=False, separators=(",", ":")
                ).encode("utf-8")
                if len(encoded) <= MAX_RESPONSE_BYTES:
                    return encoded
                tasks = payload.get("tasks")
                if not isinstance(tasks, list) or not tasks:
                    return None
                remove_index = next(
                    (
                        index
                        for index in range(len(tasks) - 1, -1, -1)
                        if tasks[index].get("status") != "RUN"
                    ),
                    len(tasks) - 1,
                )
                tasks.pop(remove_index)
                aggregate = payload["aggregate"]
                aggregate["overflow_count"] = aggregate["total_count"] - len(tasks)

        def _send_json(self, status: int, payload: dict[str, Any]) -> None:
            encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            self._send_encoded(status, encoded)

        def _send_encoded(self, status: int, encoded: bytes) -> None:
            if stats is not None:
                stats.record(status)
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(encoded)

        def log_message(self, format: str, *args: object) -> None:
            return

    return StateHandler
