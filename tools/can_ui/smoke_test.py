#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any


def request_json(base_url: str, path: str, payload: dict[str, Any] | None = None,
                 timeout_s: float = 2.0) -> tuple[int, Any]:
    if payload is None:
        with urllib.request.urlopen(base_url + path, timeout=timeout_s) as response:
            body = response.read().decode("utf-8")
            if response.headers.get_content_type() == "application/json":
                return response.status, json.loads(body)
            return response.status, body

    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        base_url + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_s) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read().decode("utf-8"))


class SmokeTest:
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip("/")
        self.results: list[tuple[str, bool, str]] = []

    def check(self, name: str, condition: bool, detail: str = "") -> None:
        self.results.append((name, condition, detail))
        prefix = "PASS" if condition else "FAIL"
        suffix = f": {detail}" if detail else ""
        print(f"{prefix} {name}{suffix}")

    def wait_live_diag(self, timeout_s: float = 5.0) -> dict[str, Any]:
        deadline = time.time() + timeout_s
        last_error: Exception | None = None
        while time.time() < deadline:
            try:
                status, state = request_json(self.base_url, "/api/state", timeout_s=0.5)
                if (
                    status == 200
                    and isinstance(state, dict)
                    and state.get("link_alive")
                    and state.get("diag", {}).get("active_profile")
                    and state.get("limits", {}).get("output_max_deg") is not None
                    and state.get("config", {}).get("gear_ratio") is not None
                ):
                    return state
            except Exception as exc:  # noqa: BLE001 - smoke test should report any readiness issue.
                last_error = exc
            time.sleep(0.1)
        raise RuntimeError(f"server did not report live diag in time: {last_error}")

    def run(self) -> None:
        state = self.wait_live_diag()
        self.check("server has live diag", True)
        self.check("link alive", state.get("link_alive") is True)
        self.check("starts disarmed", state.get("diag", {}).get("armed") is False)

        status, body = request_json(self.base_url, "/api/power", {"armed": False})
        self.check(
            "disarm endpoint accepts boolean false",
            status == 200 and body.get("ok") is True,
        )
        status, body = request_json(self.base_url, "/api/stop_stream", {})
        self.check("stop stream endpoint", status == 200 and body.get("ok") is True)

        live_limits = state.get("limits", {})
        live_config = state.get("config", {})
        status, body = request_json(
            self.base_url,
            "/api/actuator_limits",
            {
                "output_min_deg": live_limits.get("output_min_deg", 0.0),
                "output_max_deg": live_limits.get("output_max_deg", 1.0),
            },
        )
        self.check(
            "actuator limits endpoint accepts current limits while disarmed",
            status == 200 and body.get("ok") is True,
        )
        status, body = request_json(
            self.base_url,
            "/api/gear_ratio",
            {"gear_ratio": live_config.get("gear_ratio", 1.0)},
        )
        self.check(
            "gear ratio endpoint accepts current ratio while disarmed",
            status == 200 and body.get("ok") is True,
        )

        status, html = request_json(self.base_url, "/")
        self.check("index served", status == 200)
        self.check("index references app v7", "/static/app.js?v=7" in html)
        self.check("index references styles v7", "/static/styles.css?v=7" in html)
        self.check("index exposes output min", "outputMinStatus" in html)
        self.check("index exposes gear ratio", "gearRatioStatus" in html)
        self.check("index exposes current range warning", "currentRangeFeedback" in html)
        self.check("index exposes config controls", "saveLimitsBtn" in html)

        invalid_cases = [
            (
                "reject non-boolean power",
                "/api/power",
                {"armed": "false"},
                "armed must be a boolean",
            ),
            (
                "reject raw std id > 7FF",
                "/api/raw_send",
                {"can_id": "800", "payload": ""},
                "can_id must be <= 7FF",
            ),
            (
                "reject raw odd payload",
                "/api/raw_send",
                {"can_id": "237", "payload": "0"},
                "payload must be even-length",
            ),
            (
                "reject raw >8 bytes",
                "/api/raw_send",
                {"can_id": "237", "payload": "001122334455667788"},
                "8 bytes or fewer",
            ),
            (
                "reject huge angle",
                "/api/angle",
                {"deg": 1000001},
                "angle command magnitude",
            ),
            (
                "reject non-finite velocity",
                "/api/velocity",
                {"deg_s": "NaN"},
                "velocity command must be finite",
            ),
            (
                "reject invalid session iface",
                "/api/session",
                {"can_iface": "can0;bad", "node_id": 7},
                "unsupported characters",
            ),
            (
                "reject invalid session node",
                "/api/session",
                {"can_iface": "can0", "node_id": 128},
                "between 0 and 127",
            ),
            (
                "reject inverted actuator limits",
                "/api/actuator_limits",
                {"output_min_deg": 10, "output_max_deg": 0},
                "output_max_deg must be greater",
            ),
            (
                "reject invalid gear ratio",
                "/api/gear_ratio",
                {"gear_ratio": 0},
                "gear_ratio must be between",
            ),
        ]
        for name, path, payload, expected_error in invalid_cases:
            status, body = request_json(self.base_url, path, payload)
            error = body.get("error", "") if isinstance(body, dict) else ""
            self.check(
                name,
                status == 400 and isinstance(body, dict) and body.get("ok") is False
                and expected_error in error,
                error,
            )

        status, body = request_json(
            self.base_url,
            "/api/session",
            {"can_iface": "can0", "node_id": 7},
        )
        self.check("valid same-session update succeeds", status == 200 and body.get("ok") is True)

        state = self.wait_live_diag()
        self.check("link alive after session reset", state.get("link_alive") is True)
        self.check(
            "travel limits visible",
            state.get("limits", {}).get("output_min_deg") is not None
            and state.get("limits", {}).get("output_max_deg") is not None,
        )
        self.check(
            "gear ratio visible",
            state.get("config", {}).get("gear_ratio") is not None,
        )
        self.check("still disarmed after tests", state.get("diag", {}).get("armed") is False)
        self.check("stream remains off after tests", state.get("stream", {}).get("enabled") is False)

        failed = [name for name, ok, _ in self.results if not ok]
        print()
        print(
            "SUMMARY",
            "PASS" if not failed else "FAIL",
            f"{len(self.results) - len(failed)}/{len(self.results)} passed",
        )
        if failed:
            raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke-test the NoFW CAN web UI.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--can-iface", default="can0")
    parser.add_argument("--node-id", type=int, default=7)
    parser.add_argument(
        "--use-running-server",
        action="store_true",
        help="Test an already-running server instead of starting a temporary one.",
    )
    args = parser.parse_args()

    base_url = f"http://{args.host}:{args.port}"
    server: subprocess.Popen[str] | None = None

    try:
        if not args.use_running_server:
            server = subprocess.Popen(
                [
                    sys.executable,
                    "tools/can_ui/server.py",
                    "--host",
                    args.host,
                    "--port",
                    str(args.port),
                    "--can-iface",
                    args.can_iface,
                    "--node-id",
                    str(args.node_id),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        SmokeTest(base_url).run()
    finally:
        if server is not None and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=3.0)
        if server is not None:
            stdout, stderr = server.communicate(timeout=1.0)
            if stdout.strip():
                print("SERVER STDOUT:", stdout.strip())
            if stderr.strip():
                print("SERVER STDERR:", stderr.strip(), file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
