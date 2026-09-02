"""Command-line service for the Codex task bridge."""

from __future__ import annotations

import argparse
import logging
import os
import signal
import threading
import time
from http.server import ThreadingHTTPServer
from pathlib import Path

from .model import TaskStore
from .observer import CodexSessionObserver
from .persistence import BridgeStatePersistence
from .platform_paths import default_sessions_dir, default_state_file
from .server import ApiStats, make_handler


POLL_INTERVAL_SECONDS = 0.5
SAVE_INTERVAL_SECONDS = 5.0
HEALTH_INTERVAL_SECONDS = 30.0
DEFAULT_PORT = 8765
TOKEN_ENV = "CODEX_NOTIFIER_TOKEN"


def validate_token(token: str) -> str:
    if not 32 <= len(token) <= 128:
        raise ValueError("token length must be 32..128 characters")
    try:
        encoded = token.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("token must be visible ASCII") from error
    if any(byte < 0x21 or byte > 0x7E for byte in encoded):
        raise ValueError("token must not contain whitespace or control characters")
    lowered = token.lower()
    if "change_me" in lowered or "replace_me" in lowered or "placeholder" in lowered:
        raise ValueError("placeholder token is not allowed")
    return token


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Read-only Codex task status bridge")
    parser.add_argument(
        "--sessions-dir",
        type=Path,
        default=default_sessions_dir(),
    )
    parser.add_argument(
        "--state-file",
        type=Path,
        default=default_state_file(),
    )
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if not 1 <= args.port <= 65_535:
        raise SystemExit("invalid --port: expected 1..65535")
    try:
        token = validate_token(os.environ.get(TOKEN_ENV, ""))
    except ValueError as error:
        raise SystemExit(f"invalid {TOKEN_ENV}: {error}") from error

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    logger = logging.getLogger("codex_task_bridge")
    store = TaskStore()
    observer = CodexSessionObserver(args.sessions_dir.expanduser(), store)
    persistence = BridgeStatePersistence(args.state_file.expanduser())
    restored = persistence.restore(store, observer)
    if not restored:
        observer.establish_baseline()
        try:
            persistence.save(store, observer)
        except OSError as error:
            logger.error("[observer] initial state save failed error=%s", type(error).__name__)
    logger.info(
        "[observer] ready mode=%s files=%d",
        "resume" if restored else "baseline",
        len(observer.export_state()["files"]),
    )

    api_stats = ApiStats()
    server = ThreadingHTTPServer((args.host, args.port), make_handler(store, token, api_stats))
    server.daemon_threads = True
    server_thread = threading.Thread(
        target=server.serve_forever,
        name="bridge-http",
        daemon=True,
    )
    stop_event = threading.Event()

    def request_stop(signum: int, _frame: object) -> None:
        logger.info("[observer] stop requested signal=%d", signum)
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    server_thread.start()
    logger.info("[api] listening host=%s port=%d", args.host, args.port)

    next_save = time.monotonic() + SAVE_INTERVAL_SECONDS
    next_health = time.monotonic() + HEALTH_INTERVAL_SECONDS
    try:
        while not stop_event.wait(POLL_INTERVAL_SECONDS):
            changed = observer.poll_once()
            now_monotonic = time.monotonic()
            if changed:
                snapshot = store.snapshot(0, int(time.time() * 1000))
                aggregate = snapshot["aggregate"]
                logger.info(
                    "[task] changed total=%d run=%d done=%d stop=%d event_seq=%d",
                    aggregate["total_count"],
                    aggregate["running_count"],
                    aggregate["done_count"],
                    aggregate["stop_count"],
                    snapshot["events"][-1]["seq"] if snapshot["events"] else 0,
                )
            if changed or now_monotonic >= next_save:
                try:
                    persistence.save(store, observer)
                except OSError as error:
                    logger.error("[observer] state save failed error=%s", type(error).__name__)
                next_save = now_monotonic + SAVE_INTERVAL_SECONDS
            if now_monotonic >= next_health:
                snapshot = store.snapshot(0, int(time.time() * 1000))
                stats = api_stats.snapshot()
                logger.info(
                    "[health] files=%d tasks=%d stale_run=%d invalid_event=%d invalid_json=%d "
                    "io_error=%d requests=%d bad_request=%d unauthorized=%d "
                    "not_found=%d server_error=%d",
                    len(observer.export_state()["files"]),
                    snapshot["aggregate"]["total_count"],
                    store.stale_run_pruned_count(),
                    observer.invalid_event_count,
                    observer.invalid_json_count,
                    observer.io_error_count,
                    stats.requests,
                    stats.bad_requests,
                    stats.unauthorized,
                    stats.not_found,
                    stats.server_errors,
                )
                next_health = now_monotonic + HEALTH_INTERVAL_SECONDS
    finally:
        try:
            persistence.save(store, observer)
        except OSError as error:
            logger.error("[observer] final state save failed error=%s", type(error).__name__)
        server.shutdown()
        server.server_close()
        server_thread.join(timeout=2)
        logger.info("[observer] stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
