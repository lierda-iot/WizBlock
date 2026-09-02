from __future__ import annotations

import json
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

from codex_task_bridge.model import TaskStore
from codex_task_bridge.server import MAX_UINT64, make_handler, parse_after_event_seq


TOKEN = "0123456789abcdef0123456789abcdef"


class QueryParserTest(unittest.TestCase):
    def test_missing_zero_and_maximum_are_valid(self) -> None:
        self.assertEqual(parse_after_event_seq(""), 0)
        self.assertEqual(parse_after_event_seq("after_event_seq=0"), 0)
        self.assertEqual(parse_after_event_seq(f"after_event_seq={MAX_UINT64}"), MAX_UINT64)

    def test_invalid_values_are_rejected(self) -> None:
        invalid_queries = [
            "after_event_seq=",
            "after_event_seq=-1",
            "after_event_seq=+1",
            "after_event_seq=1.0",
            "after_event_seq=1e2",
            "after_event_seq=x",
            f"after_event_seq={MAX_UINT64 + 1}",
            "after_event_seq=1&after_event_seq=2",
            "after_event_seq=1&unknown=2",
            "unknown=1",
        ]
        for query in invalid_queries:
            with self.subTest(query=query), self.assertRaises(ValueError):
                parse_after_event_seq(query)


class HttpServerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.store = TaskStore()
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.store, TOKEN))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(self, path: str, token: str | None = TOKEN) -> tuple[int, dict[str, object], str]:
        headers = {"X-Codex-Notifier-Token": token} if token is not None else {}
        request = urllib.request.Request(self.base_url + path, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=2) as response:
                return response.status, json.load(response), response.headers.get("Content-Type", "")
        except urllib.error.HTTPError as error:
            try:
                return error.code, json.load(error), error.headers.get("Content-Type", "")
            finally:
                error.close()

    def test_authorized_state_request_returns_contract(self) -> None:
        status, payload, content_type = self.request("/api/v1/state?after_event_seq=0")

        self.assertEqual(status, 200)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["tasks"], [])
        self.assertEqual(payload["events"], [])
        self.assertEqual(content_type, "application/json; charset=utf-8")

    def test_missing_and_wrong_token_return_401(self) -> None:
        missing_status, missing_payload, _ = self.request("/api/v1/state", token=None)
        wrong_status, wrong_payload, _ = self.request("/api/v1/state", token="x" * 32)

        self.assertEqual((missing_status, missing_payload), (401, {"error": "unauthorized"}))
        self.assertEqual((wrong_status, wrong_payload), (401, {"error": "unauthorized"}))

    def test_invalid_query_returns_400(self) -> None:
        status, payload, _ = self.request("/api/v1/state?after_event_seq=-1")

        self.assertEqual(status, 400)
        self.assertEqual(payload, {"error": "invalid_after_event_seq"})

    def test_unknown_path_returns_404(self) -> None:
        status, payload, _ = self.request("/health")

        self.assertEqual(status, 404)
        self.assertEqual(payload, {"error": "not_found"})


if __name__ == "__main__":
    unittest.main()
