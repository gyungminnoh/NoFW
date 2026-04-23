#!/usr/bin/env python3
import argparse
import json
import math
import os
import re
import signal
import subprocess
import threading
import time
from collections import deque
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional
from urllib.parse import urlparse


PROFILE_NAMES = {
    0: "VelocityOnly",
    1: "As5600",
    2: "TmagLut",
    3: "DirectInput",
}
PROFILE_CODES = {name: code for code, name in PROFILE_NAMES.items()}
CONTROL_MODE_NAMES = {
    1: "OutputAngle",
    2: "OutputVelocity",
}

FRAME_RE = re.compile(
    r"^(?:\((?P<timestamp>[0-9.]+)\)\s+)?"
    r"(?P<iface>[A-Za-z0-9_]+)\s+"
    r"(?P<can_id>[0-9A-Fa-f]+)#(?P<data>[0-9A-Fa-f]*)$"
)
SAFE_IFACE_RE = re.compile(r"^[A-Za-z0-9_.:-]+$")

CAN_STATUS_PERIOD_S = 0.05
LINK_ALIVE_WINDOW_S = 1.5
MAX_STD_CAN_ID = 0x7FF
MAX_CLASSIC_CAN_DLC = 8
MAX_COMMAND_ABS_VALUE = 1_000_000.0
DISARM_REPEAT_COUNT = 3
DISARM_REPEAT_DELAY_S = 0.02


def clamp_node_id(node_id: int) -> int:
    if node_id < 0 or node_id > 127:
        raise ValueError("node_id must be between 0 and 127")
    return node_id


def ensure_iface_name(can_iface: str) -> str:
    if not SAFE_IFACE_RE.match(can_iface):
        raise ValueError("can_iface contains unsupported characters")
    return can_iface


def frame_id(base: int, node_id: int) -> int:
    return base + node_id


def encode_milli_units(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("value must be finite")
    if abs(value) > MAX_COMMAND_ABS_VALUE:
        raise ValueError(f"value magnitude must be <= {MAX_COMMAND_ABS_VALUE:g}")
    raw = int(round(value * 1000.0))
    if raw < -(2**31) or raw > (2**31 - 1):
        raise ValueError("value is out of int32 range after milli scaling")
    return raw.to_bytes(4, byteorder="little", signed=True).hex().upper()


def decode_milli_units(payload_hex: str) -> Optional[float]:
    if len(payload_hex) < 8:
        return None
    payload = bytes.fromhex(payload_hex[:8])
    raw = int.from_bytes(payload, byteorder="little", signed=True)
    return raw * 0.001


def ensure_finite_command_value(value: Any, unit: str) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{unit} command must be a finite number") from exc
    if not math.isfinite(parsed):
        raise ValueError(f"{unit} command must be finite")
    if abs(parsed) > MAX_COMMAND_ABS_VALUE:
        raise ValueError(f"{unit} command magnitude must be <= {MAX_COMMAND_ABS_VALUE:g}")
    return parsed


@dataclass
class SessionConfig:
    can_iface: str
    node_id: int


class CanUiBridge:
    def __init__(self, can_iface: str, node_id: int):
        self._lock = threading.RLock()
        self._running = True
        self._static_dir = Path(__file__).resolve().parent / "static"
        self._config = SessionConfig(
            can_iface=ensure_iface_name(can_iface),
            node_id=clamp_node_id(node_id),
        )
        self._logs: deque[dict[str, Any]] = deque(maxlen=300)
        self._latest_angle_deg: Optional[float] = None
        self._latest_velocity_deg_s: Optional[float] = None
        self._latest_limits: dict[str, Any] = {}
        self._latest_config: dict[str, Any] = {}
        self._latest_diag: dict[str, Any] = {}
        self._last_frame_time: Optional[float] = None
        self._last_error: Optional[str] = None
        self._command_mode: Optional[str] = None
        self._command_value: float = 0.0
        self._stream_enabled = False
        self._candump_process: Optional[subprocess.Popen[str]] = None

        self._monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self._tx_thread = threading.Thread(target=self._tx_loop, daemon=True)
        self._monitor_thread.start()
        self._tx_thread.start()

    @property
    def static_dir(self) -> Path:
        return self._static_dir

    def shutdown(self) -> None:
        with self._lock:
            self._running = False
            self._terminate_candump_locked()

    def log(self, level: str, message: str) -> None:
        with self._lock:
            self._logs.appendleft(
                {
                    "ts": time.strftime("%H:%M:%S"),
                    "level": level,
                    "message": message,
                }
            )

    def _set_error(self, message: Optional[str]) -> None:
        with self._lock:
            self._last_error = message

    def _terminate_candump_locked(self) -> None:
        proc = self._candump_process
        self._candump_process = None
        if proc is None:
            return
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=1.0)

    def update_session(self, can_iface: str, node_id: int) -> None:
        can_iface = ensure_iface_name(can_iface)
        node_id = clamp_node_id(node_id)
        with self._lock:
            self._config = SessionConfig(can_iface=can_iface, node_id=node_id)
            self._latest_angle_deg = None
            self._latest_velocity_deg_s = None
            self._latest_limits = {}
            self._latest_config = {}
            self._latest_diag = {}
            self._last_frame_time = None
            self._last_error = None
            self._command_mode = None
            self._command_value = 0.0
            self._stream_enabled = False
            self._terminate_candump_locked()
        self.log("info", f"Session updated: iface={can_iface}, node_id={node_id}")

    def _candump_args(self) -> list[str]:
        cfg = self._config
        angle_id = frame_id(0x400, cfg.node_id)
        velocity_id = frame_id(0x410, cfg.node_id)
        limits_id = frame_id(0x420, cfg.node_id)
        config_id = frame_id(0x430, cfg.node_id)
        diag_id = frame_id(0x5F0, cfg.node_id)
        return [
            "candump",
            "-L",
            f"{cfg.can_iface},{angle_id:03X}:7FF",
            f"{cfg.can_iface},{velocity_id:03X}:7FF",
            f"{cfg.can_iface},{limits_id:03X}:7FF",
            f"{cfg.can_iface},{config_id:03X}:7FF",
            f"{cfg.can_iface},{diag_id:03X}:7FF",
        ]

    def _monitor_loop(self) -> None:
        while True:
            with self._lock:
                if not self._running:
                    return
                if self._candump_process is None:
                    args = self._candump_args()
                    try:
                        self._candump_process = subprocess.Popen(
                            args,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True,
                            bufsize=1,
                        )
                        self._set_error(None)
                        self.log("info", "Started candump monitor")
                    except OSError as exc:
                        self._set_error(str(exc))
                        self.log("error", f"Failed to start candump: {exc}")
                        time.sleep(1.0)
                        continue
                proc = self._candump_process

            assert proc is not None
            line = proc.stdout.readline() if proc.stdout is not None else ""
            if not line:
                if proc.poll() is not None:
                    self.log("warn", f"candump exited with code {proc.returncode}")
                    with self._lock:
                        if self._candump_process is proc:
                            self._candump_process = None
                    time.sleep(0.5)
                else:
                    time.sleep(0.05)
                continue

            self._handle_candump_line(line.strip())

    def _handle_candump_line(self, line: str) -> None:
        match = FRAME_RE.match(line)
        if not match:
            self.log("warn", f"Unparsed candump output: {line}")
            return

        can_id = int(match.group("can_id"), 16)
        payload_hex = match.group("data").upper()
        now = time.time()
        cfg = self._config
        angle_id = frame_id(0x400, cfg.node_id)
        velocity_id = frame_id(0x410, cfg.node_id)
        limits_id = frame_id(0x420, cfg.node_id)
        config_id = frame_id(0x430, cfg.node_id)
        diag_id = frame_id(0x5F0, cfg.node_id)

        with self._lock:
            self._last_frame_time = now

            if can_id == angle_id:
                self._latest_angle_deg = decode_milli_units(payload_hex)
            elif can_id == velocity_id:
                self._latest_velocity_deg_s = decode_milli_units(payload_hex)
            elif can_id == limits_id:
                self._latest_limits = self._decode_limits(payload_hex)
                self._latest_limits["raw_hex"] = payload_hex
            elif can_id == config_id:
                self._latest_config = self._decode_config(payload_hex)
                self._latest_config["raw_hex"] = payload_hex
            elif can_id == diag_id:
                self._latest_diag = self._decode_diag(payload_hex)
                self._latest_diag["raw_hex"] = payload_hex

    def _decode_limits(self, payload_hex: str) -> dict[str, Any]:
        if len(payload_hex) < 16:
            return {"raw_hex": payload_hex}
        min_deg = decode_milli_units(payload_hex[:8])
        max_deg = decode_milli_units(payload_hex[8:16])
        return {
            "output_min_deg": min_deg,
            "output_max_deg": max_deg,
        }

    def _decode_config(self, payload_hex: str) -> dict[str, Any]:
        data = bytes.fromhex(payload_hex)
        if len(data) < 8:
            return {"raw_hex": payload_hex}
        gear_ratio = decode_milli_units(payload_hex[:8])
        flags = data[6]
        return {
            "gear_ratio": gear_ratio,
            "stored_profile_code": data[4],
            "stored_profile": PROFILE_NAMES.get(data[4], f"Unknown({data[4]})"),
            "default_control_mode_code": data[5],
            "default_control_mode": CONTROL_MODE_NAMES.get(
                data[5], f"Unknown({data[5]})"
            ),
            "enable_velocity_mode": bool(flags & 0x01),
            "enable_output_angle_mode": bool(flags & 0x02),
        }

    def _decode_diag(self, payload_hex: str) -> dict[str, Any]:
        data = bytes.fromhex(payload_hex)
        if len(data) < 8:
            return {"raw_hex": payload_hex}
        flags = data[7]
        return {
            "magic": data[0],
            "stored_profile_code": data[1],
            "stored_profile": PROFILE_NAMES.get(data[1], f"Unknown({data[1]})"),
            "active_profile_code": data[2],
            "active_profile": PROFILE_NAMES.get(data[2], f"Unknown({data[2]})"),
            "default_control_mode_code": data[3],
            "default_control_mode": CONTROL_MODE_NAMES.get(
                data[3], f"Unknown({data[3]})"
            ),
            "enable_velocity_mode": bool(data[4]),
            "enable_output_angle_mode": bool(data[5]),
            "need_calibration": bool(data[6]),
            "output_feedback_required": bool(flags & 0x01),
            "armed": bool(flags & 0x02),
        }

    def _frame_spec(self, frame_type: str, value: Any) -> tuple[str, str]:
        cfg = self._config
        if frame_type == "angle":
            return (
                f"{frame_id(0x200, cfg.node_id):03X}",
                encode_milli_units(float(value)),
            )
        if frame_type == "velocity":
            return (
                f"{frame_id(0x210, cfg.node_id):03X}",
                encode_milli_units(float(value)),
            )
        if frame_type == "profile":
            if isinstance(value, str):
                profile_code = PROFILE_CODES[value]
            else:
                profile_code = int(value)
            return (f"{frame_id(0x220, cfg.node_id):03X}", f"{profile_code:02X}")
        if frame_type == "power":
            return (f"{frame_id(0x230, cfg.node_id):03X}", "01" if value else "00")
        raise ValueError(f"unsupported frame type: {frame_type}")

    def _send_frame(self, can_id_hex: str, payload_hex: str) -> None:
        cfg = self._config
        frame = f"{can_id_hex}#{payload_hex}"
        try:
            subprocess.run(
                ["cansend", cfg.can_iface, frame],
                check=True,
                capture_output=True,
                text=True,
            )
            self.log("tx", f"{cfg.can_iface} {frame}")
            self._set_error(None)
        except subprocess.CalledProcessError as exc:
            stderr = (exc.stderr or "").strip()
            stdout = (exc.stdout or "").strip()
            message = stderr or stdout or str(exc)
            self._set_error(message)
            self.log("error", f"cansend failed for {frame}: {message}")
            raise RuntimeError(message) from exc

    def send_one_shot(self, frame_type: str, value: Any) -> None:
        can_id_hex, payload_hex = self._frame_spec(frame_type, value)
        self._send_frame(can_id_hex, payload_hex)

    def send_raw(self, can_id_hex: str, payload_hex: str) -> None:
        can_id_hex = can_id_hex.strip().upper()
        payload_hex = payload_hex.strip().upper()
        if not re.fullmatch(r"[0-9A-F]{1,3}", can_id_hex):
            raise ValueError("can_id must be 1-3 hex digits")
        if int(can_id_hex, 16) > MAX_STD_CAN_ID:
            raise ValueError("can_id must be <= 7FF for standard CAN frames")
        if payload_hex and (len(payload_hex) % 2 != 0 or not re.fullmatch(r"[0-9A-F]+", payload_hex)):
            raise ValueError("payload must be even-length hex bytes")
        if len(payload_hex) // 2 > MAX_CLASSIC_CAN_DLC:
            raise ValueError("payload must be 8 bytes or fewer")
        self._send_frame(can_id_hex, payload_hex)

    def set_profile(self, profile_name: str) -> None:
        if profile_name not in PROFILE_CODES:
            raise ValueError("unsupported profile")
        self.send_one_shot("profile", profile_name)

    def set_power(self, armed: bool) -> None:
        if not isinstance(armed, bool):
            raise ValueError("armed must be a boolean")
        if not armed:
            with self._lock:
                self._command_mode = None
                self._command_value = 0.0
                self._stream_enabled = False
            for idx in range(DISARM_REPEAT_COUNT):
                self.send_one_shot("power", False)
                if idx + 1 < DISARM_REPEAT_COUNT:
                    time.sleep(DISARM_REPEAT_DELAY_S)
            return

        self.send_one_shot("power", True)

    def set_angle_target(self, angle_deg: float) -> None:
        angle_deg = ensure_finite_command_value(angle_deg, "angle")
        with self._lock:
            self._command_mode = "angle"
            self._command_value = angle_deg
            self._stream_enabled = True
        self.log("info", f"Latched angle target = {angle_deg:.3f} deg")

    def set_velocity_target(self, velocity_deg_s: float) -> None:
        velocity_deg_s = ensure_finite_command_value(velocity_deg_s, "velocity")
        with self._lock:
            self._command_mode = "velocity"
            self._command_value = velocity_deg_s
            self._stream_enabled = True
        self.log("info", f"Latched velocity target = {velocity_deg_s:.3f} deg/s")

    def hold_current_angle(self) -> None:
        with self._lock:
            if self._latest_angle_deg is None:
                raise RuntimeError("current angle is not available yet")
            self._command_mode = "angle"
            self._command_value = self._latest_angle_deg
            self._stream_enabled = True
            angle = self._latest_angle_deg
        self.log("info", f"Holding current angle at {angle:.3f} deg")

    def zero_velocity(self) -> None:
        self.set_velocity_target(0.0)

    def stop_stream(self) -> None:
        with self._lock:
            self._stream_enabled = False
            self._command_mode = None
            self._command_value = 0.0
        self.log("info", "Stopped latched command stream")

    def _tx_loop(self) -> None:
        while True:
            time.sleep(CAN_STATUS_PERIOD_S)
            with self._lock:
                if not self._running:
                    return
                if not self._stream_enabled or self._command_mode is None:
                    continue
                mode = self._command_mode
                value = self._command_value
            try:
                self.send_one_shot(mode, value)
            except (RuntimeError, ValueError) as exc:
                with self._lock:
                    self._stream_enabled = False
                    self._command_mode = None
                    self._command_value = 0.0
                self._set_error(str(exc))
                self.log("error", f"Stopped latched stream: {exc}")
                pass

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            last_frame_age_s: Optional[float] = None
            link_alive = False
            if self._last_frame_time is not None:
                last_frame_age_s = time.time() - self._last_frame_time
                link_alive = last_frame_age_s <= LINK_ALIVE_WINDOW_S

            return {
                "session": {
                    "can_iface": self._config.can_iface,
                    "node_id": self._config.node_id,
                },
                "link_alive": link_alive,
                "last_frame_age_s": last_frame_age_s,
                "last_error": self._last_error,
                "angle_deg": self._latest_angle_deg,
                "velocity_deg_s": self._latest_velocity_deg_s,
                "limits": self._latest_limits,
                "config": self._latest_config,
                "diag": self._latest_diag,
                "stream": {
                    "enabled": self._stream_enabled,
                    "mode": self._command_mode,
                    "value": self._command_value,
                },
                "logs": list(self._logs),
            }


class UiRequestHandler(BaseHTTPRequestHandler):
    bridge: CanUiBridge
    static_dir: Path

    def log_message(self, format: str, *args: Any) -> None:
        return

    def _send_json(self, payload: dict[str, Any], status: int = HTTPStatus.OK) -> None:
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _send_file(self, path: Path, content_type: str) -> None:
        data = path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _read_json(self) -> dict[str, Any]:
        content_length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(content_length) if content_length > 0 else b"{}"
        return json.loads(raw.decode("utf-8"))

    def _dispatch_post(self, path: str, body: dict[str, Any]) -> dict[str, Any]:
        if path == "/api/session":
            self.bridge.update_session(body["can_iface"], int(body["node_id"]))
            return {"ok": True}
        if path == "/api/profile":
            self.bridge.set_profile(body["profile"])
            return {"ok": True}
        if path == "/api/power":
            self.bridge.set_power(body["armed"])
            return {"ok": True}
        if path == "/api/angle":
            self.bridge.set_angle_target(float(body["deg"]))
            return {"ok": True}
        if path == "/api/velocity":
            self.bridge.set_velocity_target(float(body["deg_s"]))
            return {"ok": True}
        if path == "/api/hold":
            self.bridge.hold_current_angle()
            return {"ok": True}
        if path == "/api/zero_velocity":
            self.bridge.zero_velocity()
            return {"ok": True}
        if path == "/api/stop_stream":
            self.bridge.stop_stream()
            return {"ok": True}
        if path == "/api/raw_send":
            self.bridge.send_raw(body["can_id"], body.get("payload", ""))
            return {"ok": True}
        raise KeyError(path)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/api/state":
            self._send_json(self.bridge.snapshot())
            return

        if parsed.path == "/" or parsed.path == "/index.html":
            self._send_file(self.static_dir / "index.html", "text/html; charset=utf-8")
            return
        if parsed.path == "/static/app.js":
            self._send_file(
                self.static_dir / "app.js", "application/javascript; charset=utf-8"
            )
            return
        if parsed.path == "/static/styles.css":
            self._send_file(self.static_dir / "styles.css", "text/css; charset=utf-8")
            return

        self.send_error(HTTPStatus.NOT_FOUND, "Not found")

    def do_POST(self) -> None:
        try:
            body = self._read_json()
            response = self._dispatch_post(urlparse(self.path).path, body)
            response["state"] = self.bridge.snapshot()
            self._send_json(response)
        except (ValueError, RuntimeError, KeyError) as exc:
            self._send_json({"ok": False, "error": str(exc)}, status=HTTPStatus.BAD_REQUEST)


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def build_handler(bridge: CanUiBridge):
    class Handler(UiRequestHandler):
        pass

    Handler.bridge = bridge
    Handler.static_dir = bridge.static_dir
    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description="Local CAN test web UI for NoFW")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--can-iface", default="can0")
    parser.add_argument("--node-id", type=int, default=7)
    args = parser.parse_args()

    bridge = CanUiBridge(args.can_iface, args.node_id)
    handler_cls = build_handler(bridge)
    server = ReusableThreadingHTTPServer((args.host, args.port), handler_cls)

    def shutdown_handler(signum: int, frame: Any) -> None:
        del signum, frame
        bridge.shutdown()
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGINT, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)

    print(f"Serving CAN UI on http://{args.host}:{args.port}")
    try:
        server.serve_forever()
    finally:
        bridge.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
