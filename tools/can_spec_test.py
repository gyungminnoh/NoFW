#!/usr/bin/env python3
"""NoFW CAN protocol/spec test runner.

Default mode is intentionally non-motion:
- protocol encode/decode checks always run locally
- live CAN checks only observe status, disarm, and round-trip stored config
- power-stage arm and motor motion tests require explicit flags
"""

from __future__ import annotations

import argparse
import json
import math
import os
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable


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
PROFILE_SELECT_RESULT_NAMES = {
    0: "None",
    1: "Ok",
    2: "RejectedArmed",
    3: "As5600ReadFailed",
    4: "NotSelectable",
    5: "SaveFailed",
}
RUNTIME_FAULT_NAMES = {
    0: "None",
    1: "FollowingError",
    2: "FocInitFailed",
    3: "As5600ReadFailed",
    4: "CalibrationSaveFailed",
}
CALIBRATION_LOAD_STATUS_NAMES = {
    0: "None",
    1: "Trusted",
}

FRAME_RE = re.compile(
    r"^(?:\((?P<timestamp>[0-9.]+)\)\s+)?"
    r"(?P<iface>[A-Za-z0-9_.:-]+)\s+"
    r"(?P<can_id>[0-9A-Fa-f]+)#(?P<data>[0-9A-Fa-f]*)$"
)
IFACE_RE = re.compile(r"^[A-Za-z0-9_.:-]+$")

MAX_STD_CAN_ID = 0x7FF
MAX_CLASSIC_DLC = 8
MAX_ABS_COMMAND_VALUE = 1_000_000.0
MIN_GEAR_RATIO = 0.001
MAX_GEAR_RATIO = 1000.0
MIN_VOLTAGE_LIMIT = 0.001
MAX_VOLTAGE_LIMIT = 40.0


def frame_id(base: int, node_id: int) -> int:
    return base + node_id


def encode_milli(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("value must be finite")
    if abs(value) > MAX_ABS_COMMAND_VALUE:
        raise ValueError("value magnitude too large")
    raw = int(round(value * 1000.0))
    if raw < -(2**31) or raw > (2**31 - 1):
        raise ValueError("value is out of int32 range")
    return raw.to_bytes(4, byteorder="little", signed=True).hex().upper()


def decode_milli(payload_hex: str, offset: int = 0) -> float:
    data = bytes.fromhex(payload_hex)
    if len(data) < offset + 4:
        raise ValueError("payload is shorter than int32 milli-unit field")
    raw = int.from_bytes(data[offset : offset + 4], byteorder="little", signed=True)
    return raw * 0.001


def validate_node_id(node_id: int) -> int:
    if node_id < 0 or node_id > 127:
        raise ValueError("node_id must be between 0 and 127")
    return node_id


def validate_iface(iface: str) -> str:
    if not IFACE_RE.match(iface):
        raise ValueError("CAN interface contains unsupported characters")
    return iface


def validate_payload(payload_hex: str) -> str:
    payload_hex = payload_hex.strip().upper()
    if payload_hex and not re.fullmatch(r"[0-9A-F]+", payload_hex):
        raise ValueError("payload must be hex")
    if len(payload_hex) % 2 != 0:
        raise ValueError("payload must contain whole bytes")
    if len(payload_hex) // 2 > MAX_CLASSIC_DLC:
        raise ValueError("classic CAN payload must be 8 bytes or fewer")
    return payload_hex


@dataclass(frozen=True)
class Frame:
    can_id: int
    payload_hex: str
    iface: str = ""
    timestamp: float | None = None

    @property
    def data(self) -> bytes:
        return bytes.fromhex(self.payload_hex)


def parse_candump_line(line: str) -> Frame | None:
    match = FRAME_RE.match(line.strip())
    if not match:
        return None
    timestamp = match.group("timestamp")
    return Frame(
        can_id=int(match.group("can_id"), 16),
        payload_hex=match.group("data").upper(),
        iface=match.group("iface"),
        timestamp=float(timestamp) if timestamp is not None else None,
    )


def decode_diag(payload_hex: str) -> dict[str, Any]:
    data = bytes.fromhex(payload_hex)
    if len(data) != 8:
        raise ValueError("runtime diag must be DLC 8")
    if data[0] != 0xFB:
        raise ValueError(f"runtime diag magic mismatch: 0x{data[0]:02X}")
    select_bits = data[6]
    flags = data[7]
    status_flags = data[4]
    fault_code = data[5]
    calibration_status_code = (status_flags >> 4) & 0x03
    return {
        "magic": data[0],
        "stored_profile_code": data[1],
        "stored_profile": PROFILE_NAMES.get(data[1], f"Unknown({data[1]})"),
        "active_profile_code": data[2],
        "active_profile": PROFILE_NAMES.get(data[2], f"Unknown({data[2]})"),
        "default_control_mode_code": data[3],
        "default_control_mode": CONTROL_MODE_NAMES.get(data[3], f"Unknown({data[3]})"),
        "enable_velocity_mode": bool(status_flags & 0x01),
        "enable_output_angle_mode": bool(status_flags & 0x02),
        "foc_valid": bool(status_flags & 0x04),
        "output_cal_valid": bool(status_flags & 0x08),
        "calibration_load_status_code": calibration_status_code,
        "calibration_load_status": CALIBRATION_LOAD_STATUS_NAMES.get(
            calibration_status_code, f"Unknown({calibration_status_code})"
        ),
        "fault_code": fault_code,
        "fault": RUNTIME_FAULT_NAMES.get(fault_code, f"Unknown({fault_code})"),
        "need_calibration": bool(select_bits & 0x01),
        "profile_select_result_code": (select_bits >> 4) & 0x0F,
        "profile_select_result": PROFILE_SELECT_RESULT_NAMES.get(
            (select_bits >> 4) & 0x0F,
            f"Unknown({(select_bits >> 4) & 0x0F})",
        ),
        "output_feedback_required": bool(flags & 0x01),
        "armed": bool(flags & 0x02),
    }


def decode_limits(payload_hex: str) -> dict[str, float]:
    if len(bytes.fromhex(payload_hex)) != 8:
        raise ValueError("limits status must be DLC 8")
    return {
        "output_min_deg": decode_milli(payload_hex, 0),
        "output_max_deg": decode_milli(payload_hex, 4),
    }


def decode_config(payload_hex: str) -> dict[str, Any]:
    data = bytes.fromhex(payload_hex)
    if len(data) != 8:
        raise ValueError("actuator config status must be DLC 8")
    return {
        "gear_ratio": decode_milli(payload_hex, 0),
        "stored_profile_code": data[4],
        "stored_profile": PROFILE_NAMES.get(data[4], f"Unknown({data[4]})"),
        "default_control_mode_code": data[5],
        "default_control_mode": CONTROL_MODE_NAMES.get(data[5], f"Unknown({data[5]})"),
        "enable_velocity_mode": bool(data[6] & 0x01),
        "enable_output_angle_mode": bool(data[6] & 0x02),
    }


def decode_voltage_limit(payload_hex: str) -> dict[str, float]:
    if len(bytes.fromhex(payload_hex)) != 4:
        raise ValueError("voltage limit status must be DLC 4")
    return {
        "voltage_limit": decode_milli(payload_hex, 0),
    }


@dataclass
class Snapshot:
    angle_deg: float | None = None
    velocity_deg_s: float | None = None
    limits: dict[str, float] | None = None
    config: dict[str, Any] | None = None
    voltage_limit: dict[str, float] | None = None
    diag: dict[str, Any] | None = None
    frames_seen: dict[int, int] = field(default_factory=dict)


class CanSession:
    def __init__(self, iface: str, node_id: int):
        self.iface = validate_iface(iface)
        self.node_id = validate_node_id(node_id)
        self._queue: queue.Queue[Frame] = queue.Queue()
        self._stop = threading.Event()
        self._proc: subprocess.Popen[str] | None = None
        self._thread: threading.Thread | None = None

    @property
    def ids(self) -> dict[str, int]:
        node = self.node_id
        return {
            "angle_cmd": frame_id(0x200, node),
            "angle_status": frame_id(0x400, node),
            "velocity_cmd": frame_id(0x210, node),
            "velocity_status": frame_id(0x410, node),
            "limits_status": frame_id(0x420, node),
            "config_status": frame_id(0x430, node),
            "profile_cmd": frame_id(0x220, node),
            "power_cmd": frame_id(0x230, node),
            "limits_cmd": frame_id(0x240, node),
            "gear_cmd": frame_id(0x250, node),
            "encoder_config_cmd": frame_id(0x260, node),
            "encoder_auto_cal_cmd": frame_id(0x270, node),
            "encoder_zero_cmd": frame_id(0x280, node),
            "foc_cal_cmd": frame_id(0x290, node),
            "voltage_limit_cmd": frame_id(0x2A0, node),
            "voltage_limit_status": frame_id(0x440, node),
            "diag": frame_id(0x5F0, node),
        }

    def start(self) -> None:
        filters = [
            f"{self.iface},{self.ids['angle_status']:03X}:7FF",
            f"{self.iface},{self.ids['velocity_status']:03X}:7FF",
            f"{self.iface},{self.ids['limits_status']:03X}:7FF",
            f"{self.iface},{self.ids['config_status']:03X}:7FF",
            f"{self.iface},{self.ids['voltage_limit_status']:03X}:7FF",
            f"{self.iface},{self.ids['diag']:03X}:7FF",
        ]
        self._proc = subprocess.Popen(
            ["candump", "-L", *filters],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            preexec_fn=os.setsid,
        )
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        proc = self._proc
        if proc and proc.poll() is None:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
                proc.wait(timeout=1.0)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                proc.kill()
                proc.wait(timeout=1.0)

    def _reader(self) -> None:
        assert self._proc is not None
        assert self._proc.stdout is not None
        while not self._stop.is_set():
            line = self._proc.stdout.readline()
            if not line:
                if self._proc.poll() is not None:
                    return
                time.sleep(0.02)
                continue
            frame = parse_candump_line(line)
            if frame is not None:
                self._queue.put(frame)

    def send(self, can_id: int, payload_hex: str) -> None:
        if can_id < 0 or can_id > MAX_STD_CAN_ID:
            raise ValueError("CAN ID must be standard 11-bit")
        payload_hex = validate_payload(payload_hex)
        subprocess.run(
            ["cansend", self.iface, f"{can_id:03X}#{payload_hex}"],
            check=True,
            capture_output=True,
            text=True,
        )

    def send_power(self, armed: bool) -> None:
        self.send(self.ids["power_cmd"], "01" if armed else "00")

    def send_profile(self, profile_code: int) -> None:
        self.send(self.ids["profile_cmd"], f"{profile_code:02X}")

    def send_limits(self, output_min_deg: float, output_max_deg: float) -> None:
        self.send(self.ids["limits_cmd"], encode_milli(output_min_deg) + encode_milli(output_max_deg))

    def send_gear_ratio(self, gear_ratio: float) -> None:
        self.send(self.ids["gear_cmd"], encode_milli(gear_ratio))

    def send_angle(self, angle_deg: float) -> None:
        self.send(self.ids["angle_cmd"], encode_milli(angle_deg))

    def send_velocity(self, velocity_deg_s: float) -> None:
        self.send(self.ids["velocity_cmd"], encode_milli(velocity_deg_s))

    def send_voltage_limit(self, voltage_limit: float) -> None:
        self.send(self.ids["voltage_limit_cmd"], encode_milli(voltage_limit))

    def drain(self) -> None:
        while True:
            try:
                self._queue.get_nowait()
            except queue.Empty:
                return

    def collect_snapshot(self, timeout_s: float = 3.0) -> Snapshot:
        deadline = time.time() + timeout_s
        snapshot = Snapshot()
        required = {
            self.ids["angle_status"],
            self.ids["velocity_status"],
            self.ids["limits_status"],
            self.ids["config_status"],
            self.ids["voltage_limit_status"],
            self.ids["diag"],
        }
        while time.time() < deadline:
            try:
                frame = self._queue.get(timeout=0.1)
            except queue.Empty:
                continue
            snapshot.frames_seen[frame.can_id] = snapshot.frames_seen.get(frame.can_id, 0) + 1
            if frame.can_id == self.ids["angle_status"]:
                snapshot.angle_deg = decode_milli(frame.payload_hex)
            elif frame.can_id == self.ids["velocity_status"]:
                snapshot.velocity_deg_s = decode_milli(frame.payload_hex)
            elif frame.can_id == self.ids["limits_status"]:
                snapshot.limits = decode_limits(frame.payload_hex)
            elif frame.can_id == self.ids["config_status"]:
                snapshot.config = decode_config(frame.payload_hex)
            elif frame.can_id == self.ids["voltage_limit_status"]:
                snapshot.voltage_limit = decode_voltage_limit(frame.payload_hex)
            elif frame.can_id == self.ids["diag"]:
                snapshot.diag = decode_diag(frame.payload_hex)
            if required.issubset(snapshot.frames_seen.keys()):
                return snapshot
        return snapshot

    def wait_for(self, predicate: Callable[[Snapshot], bool], timeout_s: float, label: str) -> Snapshot:
        deadline = time.time() + timeout_s
        last = Snapshot()
        while time.time() < deadline:
            snap = self.collect_snapshot(timeout_s=0.6)
            if snap.diag is not None:
                last.diag = snap.diag
            if snap.config is not None:
                last.config = snap.config
            if snap.voltage_limit is not None:
                last.voltage_limit = snap.voltage_limit
            if snap.limits is not None:
                last.limits = snap.limits
            if snap.angle_deg is not None:
                last.angle_deg = snap.angle_deg
            if snap.velocity_deg_s is not None:
                last.velocity_deg_s = snap.velocity_deg_s
            last.frames_seen.update(snap.frames_seen)
            if predicate(last):
                return last
        raise TimeoutError(f"timeout waiting for {label}; last={last}")


class SpecRunner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.results: list[dict[str, Any]] = []
        self.session: CanSession | None = None
        self.original_limits: dict[str, float] | None = None
        self.original_config: dict[str, Any] | None = None
        self.original_voltage_limit: dict[str, float] | None = None
        self.original_profile_code: int | None = None

    def check(self, name: str, fn: Callable[[], Any]) -> None:
        started = time.time()
        try:
            detail = fn()
            self.results.append(
                {"name": name, "ok": True, "detail": detail, "elapsed_s": time.time() - started}
            )
            print(f"PASS {name}" + (f": {detail}" if detail else ""))
        except Exception as exc:  # noqa: BLE001 - test runner must report every failure uniformly.
            self.results.append(
                {"name": name, "ok": False, "detail": str(exc), "elapsed_s": time.time() - started}
            )
            print(f"FAIL {name}: {exc}")
            if self.args.fail_fast:
                raise

    def run_protocol_tests(self) -> None:
        self.check("protocol frame IDs for node", self._test_frame_ids)
        self.check("milli-unit payload encoding", self._test_milli_encoding)
        self.check("candump parser", self._test_candump_parser)
        self.check("runtime diag decoder", self._test_diag_decoder)
        self.check("limits/config decoder", self._test_limits_config_decoder)
        self.check("payload and interface validators", self._test_validators)

    def _test_frame_ids(self) -> str:
        node = self.args.node_id
        expected = {
            "angle_cmd": 0x200 + node,
            "angle_status": 0x400 + node,
            "velocity_cmd": 0x210 + node,
            "velocity_status": 0x410 + node,
            "limits_status": 0x420 + node,
            "config_status": 0x430 + node,
            "profile_cmd": 0x220 + node,
            "power_cmd": 0x230 + node,
            "limits_cmd": 0x240 + node,
            "gear_cmd": 0x250 + node,
            "encoder_config_cmd": 0x260 + node,
            "encoder_auto_cal_cmd": 0x270 + node,
            "encoder_zero_cmd": 0x280 + node,
            "foc_cal_cmd": 0x290 + node,
            "voltage_limit_cmd": 0x2A0 + node,
            "voltage_limit_status": 0x440 + node,
            "diag": 0x5F0 + node,
        }
        for name, can_id in expected.items():
            if can_id > MAX_STD_CAN_ID:
                raise AssertionError(f"{name} overflows standard CAN ID: 0x{can_id:X}")
        return ", ".join(f"{name}=0x{can_id:03X}" for name, can_id in expected.items())

    def _test_milli_encoding(self) -> str:
        vectors = [
            (50.0, "50C30000"),
            (-12.5, "2CCFFFFF"),
            (100.0, "A0860100"),
            (-25.0, "589EFFFF"),
            (8.0, "401F0000"),
            (2160.0, "80F52000"),
        ]
        for value, payload in vectors:
            actual = encode_milli(value)
            if actual != payload:
                raise AssertionError(f"{value} encoded as {actual}, expected {payload}")
            decoded = decode_milli(payload)
            if abs(decoded - value) > 0.0005:
                raise AssertionError(f"{payload} decoded as {decoded}, expected {value}")
        return f"{len(vectors)} vectors"

    def _test_candump_parser(self) -> str:
        parsed = parse_candump_line("(1713960000.123456) can0 407#50C30000")
        if parsed is None:
            raise AssertionError("failed to parse valid candump line")
        if parsed.iface != "can0" or parsed.can_id != 0x407 or parsed.payload_hex != "50C30000":
            raise AssertionError(f"unexpected parsed frame: {parsed}")
        if parse_candump_line("not a frame") is not None:
            raise AssertionError("invalid line parsed as frame")
        return "candump -L format accepted"

    def _test_diag_decoder(self) -> str:
        diag = decode_diag("FB0101011F000001")
        if diag["stored_profile"] != "As5600" or diag["active_profile"] != "As5600":
            raise AssertionError(diag)
        if diag["armed"]:
            raise AssertionError("expected disarmed")
        if not diag["foc_valid"] or not diag["output_cal_valid"]:
            raise AssertionError(diag)
        diag = decode_diag("FB01010100010303")
        if not diag["need_calibration"] or not diag["armed"]:
            raise AssertionError(diag)
        if diag["fault"] != "FollowingError":
            raise AssertionError(diag)
        return "0x5F0 payload decoded"

    def _test_limits_config_decoder(self) -> str:
        limits = decode_limits("0000000080F52000")
        if limits != {"output_min_deg": 0.0, "output_max_deg": 2160.0}:
            raise AssertionError(limits)
        config = decode_config("401F000001010300")
        if config["gear_ratio"] != 8.0 or config["stored_profile"] != "As5600":
            raise AssertionError(config)
        if not config["enable_velocity_mode"] or not config["enable_output_angle_mode"]:
            raise AssertionError(config)
        voltage_limit = decode_voltage_limit("30750000")
        if voltage_limit != {"voltage_limit": 30.0}:
            raise AssertionError(voltage_limit)
        return "0x420/0x430/0x440 payloads decoded"

    def _test_validators(self) -> str:
        validate_iface("can0")
        validate_node_id(127)
        validate_payload("0011223344556677")
        invalid_cases: list[Callable[[], Any]] = [
            lambda: validate_iface("can0;rm"),
            lambda: validate_node_id(128),
            lambda: validate_payload("0"),
            lambda: validate_payload("001122334455667788"),
            lambda: encode_milli(float("nan")),
            lambda: encode_milli(1_000_001.0),
        ]
        for case in invalid_cases:
            try:
                case()
            except ValueError:
                continue
            raise AssertionError("invalid validator case was accepted")
        return f"{len(invalid_cases)} invalid cases rejected"

    def run_live_tests(self) -> None:
        if self.args.protocol_only:
            return
        self.check("host CAN tools available", self._test_host_tools)
        self.session = CanSession(self.args.iface, self.args.node_id)
        self.session.start()
        try:
            self.check("live status frames visible", self._test_live_status)
            self.check("runtime status consistency", self._test_runtime_status_consistency)
            self.check("disarm command is idempotent", self._test_disarm_idempotent)
            self.check("actuator limits config roundtrip", self._test_limits_roundtrip)
            self.check("gear ratio config roundtrip", self._test_gear_roundtrip)
            self.check("voltage limit config roundtrip", self._test_voltage_limit_roundtrip)
            self.check("current profile command succeeds while disarmed", self._test_current_profile_roundtrip)
            self.check("invalid profile command is ignored", self._test_invalid_profile_ignored)
            if self.args.allow_arm:
                self.check("arm/disarm status transition", self._test_arm_disarm_transition)
                self.check("profile/config commands rejected while armed", self._test_reject_while_armed)
            if self.args.allow_motion:
                self.check("angle command small-step response", self._test_angle_small_step)
                self.check("velocity command small-step response", self._test_velocity_small_step)
        finally:
            self._restore_safe_state()
            self.session.close()

    def _require_session(self) -> CanSession:
        if self.session is None:
            raise RuntimeError("CAN session is not started")
        return self.session

    def _test_host_tools(self) -> str:
        missing = [tool for tool in ("candump", "cansend") if shutil.which(tool) is None]
        if missing:
            raise AssertionError(f"missing tools: {', '.join(missing)}")
        return "candump/cansend found"

    def _test_live_status(self) -> str:
        session = self._require_session()
        snapshot = session.wait_for(
            lambda snap: snap.diag is not None and snap.limits is not None and snap.config is not None and snap.voltage_limit is not None,
            self.args.timeout,
            "diag + limits + config + voltage_limit",
        )
        self.original_limits = dict(snapshot.limits or {})
        self.original_config = dict(snapshot.config or {})
        self.original_voltage_limit = dict(snapshot.voltage_limit or {})
        self.original_profile_code = int(snapshot.diag["stored_profile_code"])
        return (
            f"profile={snapshot.diag['active_profile']}, armed={snapshot.diag['armed']}, "
            f"limits={snapshot.limits['output_min_deg']:.3f}..{snapshot.limits['output_max_deg']:.3f}, "
            f"gear={snapshot.config['gear_ratio']:.3f}, "
            f"voltage={snapshot.voltage_limit['voltage_limit']:.3f} V"
        )

    def _test_runtime_status_consistency(self) -> str:
        session = self._require_session()
        snap = session.collect_snapshot(timeout_s=2.0)
        if snap.angle_deg is None or snap.velocity_deg_s is None:
            raise AssertionError("angle/velocity status frames were not both observed")
        if snap.diag is not None:
            stored = snap.diag["stored_profile_code"]
            active = snap.diag["active_profile_code"]
            if stored not in PROFILE_NAMES or active not in PROFILE_NAMES:
                raise AssertionError(f"unknown profile in diag: {snap.diag}")
        if snap.config is not None:
            gear = snap.config["gear_ratio"]
            if not (MIN_GEAR_RATIO <= gear <= MAX_GEAR_RATIO):
                raise AssertionError(f"gear ratio out of range: {gear}")
        if snap.voltage_limit is not None:
            voltage_limit = snap.voltage_limit["voltage_limit"]
            if not (MIN_VOLTAGE_LIMIT <= voltage_limit <= MAX_VOLTAGE_LIMIT):
                raise AssertionError(f"voltage limit out of range: {voltage_limit}")
        return f"angle={snap.angle_deg:.3f} deg, velocity={snap.velocity_deg_s:.3f} deg/s"

    def _test_disarm_idempotent(self) -> str:
        session = self._require_session()
        for _ in range(3):
            session.send_power(False)
            time.sleep(0.03)
        snap = session.wait_for(
            lambda s: s.diag is not None and s.diag["armed"] is False,
            self.args.timeout,
            "disarmed diag",
        )
        return f"armed={snap.diag['armed']}"

    def _test_limits_roundtrip(self) -> str:
        session = self._require_session()
        assert self.original_limits is not None
        min_deg = self.original_limits["output_min_deg"]
        max_deg = self.original_limits["output_max_deg"]
        span = max_deg - min_deg
        if span <= 1.0:
            raise AssertionError(f"original limits too narrow for roundtrip test: {self.original_limits}")
        test_min = min_deg + min(1.0, span * 0.1)
        test_max = max_deg - min(1.0, span * 0.1)
        session.send_power(False)
        session.send_limits(test_min, test_max)
        snap = session.wait_for(
            lambda s: s.limits is not None
            and abs(s.limits["output_min_deg"] - test_min) <= 0.002
            and abs(s.limits["output_max_deg"] - test_max) <= 0.002,
            self.args.timeout,
            "temporary actuator limits",
        )
        session.send_limits(min_deg, max_deg)
        session.wait_for(
            lambda s: s.limits is not None
            and abs(s.limits["output_min_deg"] - min_deg) <= 0.002
            and abs(s.limits["output_max_deg"] - max_deg) <= 0.002,
            self.args.timeout,
            "restored actuator limits",
        )
        return f"{snap.limits['output_min_deg']:.3f}..{snap.limits['output_max_deg']:.3f}, restored"

    def _test_gear_roundtrip(self) -> str:
        session = self._require_session()
        assert self.original_config is not None
        original_gear = float(self.original_config["gear_ratio"])
        original_profile = int(self.original_config["stored_profile_code"])
        if original_profile == PROFILE_CODES["DirectInput"]:
            return "skipped: DirectInput only accepts gear_ratio=1.000"
        test_gear = original_gear + 0.125 if original_gear < 999.0 else original_gear - 0.125
        session.send_power(False)
        session.send_gear_ratio(test_gear)
        snap = session.wait_for(
            lambda s: s.config is not None and abs(s.config["gear_ratio"] - test_gear) <= 0.002,
            self.args.timeout,
            "temporary gear ratio",
        )
        session.send_gear_ratio(original_gear)
        session.wait_for(
            lambda s: s.config is not None and abs(s.config["gear_ratio"] - original_gear) <= 0.002,
            self.args.timeout,
            "restored gear ratio",
        )
        return f"{snap.config['gear_ratio']:.3f}, restored {original_gear:.3f}"

    def _test_voltage_limit_roundtrip(self) -> str:
        session = self._require_session()
        assert self.original_voltage_limit is not None
        original_voltage = float(self.original_voltage_limit["voltage_limit"])
        test_voltage = 30.0 if abs(original_voltage - 30.0) > 0.01 else 29.5
        session.send_power(False)
        session.send_voltage_limit(test_voltage)
        snap = session.wait_for(
            lambda s: s.voltage_limit is not None
            and abs(s.voltage_limit["voltage_limit"] - test_voltage) <= 0.002,
            self.args.timeout,
            "temporary voltage limit",
        )
        session.send_voltage_limit(original_voltage)
        session.wait_for(
            lambda s: s.voltage_limit is not None
            and abs(s.voltage_limit["voltage_limit"] - original_voltage) <= 0.002,
            self.args.timeout,
            "restored voltage limit",
        )
        return f"{snap.voltage_limit['voltage_limit']:.3f} V, restored {original_voltage:.3f} V"

    def _test_current_profile_roundtrip(self) -> str:
        session = self._require_session()
        assert self.original_profile_code is not None
        session.send_power(False)
        session.send_profile(self.original_profile_code)
        snap = session.wait_for(
            lambda s: s.diag is not None
            and s.diag["stored_profile_code"] == self.original_profile_code
            and s.diag["active_profile_code"] == self.original_profile_code
            and s.diag["profile_select_result"] == "Ok",
            self.args.timeout,
            "profile Ok result",
        )
        return f"profile={snap.diag['active_profile']}, result={snap.diag['profile_select_result']}"

    def _test_invalid_profile_ignored(self) -> str:
        session = self._require_session()
        assert self.original_profile_code is not None
        session.send_power(False)
        session.send(session.ids["profile_cmd"], "04")
        time.sleep(0.2)
        snap = session.wait_for(
            lambda s: s.diag is not None,
            self.args.timeout,
            "diag after invalid profile",
        )
        if snap.diag["stored_profile_code"] != self.original_profile_code:
            raise AssertionError(f"profile changed after invalid command: {snap.diag}")
        return f"profile stayed {snap.diag['stored_profile']}"

    def _test_arm_disarm_transition(self) -> str:
        session = self._require_session()
        session.send_power(True)
        armed = session.wait_for(
            lambda s: s.diag is not None and s.diag["armed"] is True,
            self.args.timeout,
            "armed diag",
        )
        session.send_power(False)
        disarmed = session.wait_for(
            lambda s: s.diag is not None and s.diag["armed"] is False,
            self.args.timeout,
            "disarmed diag",
        )
        return f"{armed.diag['armed']} -> {disarmed.diag['armed']}"

    def _test_reject_while_armed(self) -> str:
        session = self._require_session()
        assert self.original_profile_code is not None
        session.send_power(True)
        session.wait_for(
            lambda s: s.diag is not None and s.diag["armed"] is True,
            self.args.timeout,
            "armed before reject test",
        )
        requested_profile = PROFILE_CODES["VelocityOnly"]
        if requested_profile == self.original_profile_code:
            requested_profile = PROFILE_CODES["As5600"]
        session.send_profile(requested_profile)
        snap = session.wait_for(
            lambda s: s.diag is not None and s.diag["profile_select_result"] == "RejectedArmed",
            self.args.timeout,
            "RejectedArmed profile result",
        )
        session.send_power(False)
        return f"profile_result={snap.diag['profile_select_result']}"

    def _test_angle_small_step(self) -> str:
        session = self._require_session()
        snap = session.wait_for(
            lambda s: s.diag is not None and s.angle_deg is not None,
            self.args.timeout,
            "angle status before step",
        )
        if not snap.diag["enable_output_angle_mode"]:
            return "skipped: active profile does not allow angle mode"
        start = float(snap.angle_deg)
        target = start + self.args.angle_step_deg
        if self.original_limits is not None:
            target = min(max(target, self.original_limits["output_min_deg"]), self.original_limits["output_max_deg"])
        session.send_power(True)
        session.wait_for(
            lambda s: s.diag is not None and s.diag["armed"] is True,
            self.args.timeout,
            "armed before angle step",
        )
        for _ in range(max(1, int(self.args.motion_hold_s / 0.05))):
            session.send_angle(target)
            time.sleep(0.05)
        moved = session.collect_snapshot(timeout_s=1.0)
        if moved.angle_deg is None:
            raise AssertionError("no angle status after step")
        session.send_angle(start)
        time.sleep(0.2)
        session.send_power(False)
        if abs(moved.angle_deg - start) < min(0.2, abs(target - start) * 0.25):
            raise AssertionError(f"angle did not move enough: start={start}, observed={moved.angle_deg}")
        return f"{start:.3f} -> {moved.angle_deg:.3f} deg toward target {target:.3f}"

    def _test_velocity_small_step(self) -> str:
        session = self._require_session()
        snap = session.wait_for(
            lambda s: s.diag is not None,
            self.args.timeout,
            "diag before velocity step",
        )
        if not snap.diag["enable_velocity_mode"]:
            return "skipped: active profile does not allow velocity mode"
        session.send_power(True)
        session.wait_for(
            lambda s: s.diag is not None and s.diag["armed"] is True,
            self.args.timeout,
            "armed before velocity step",
        )
        for _ in range(max(1, int(self.args.motion_hold_s / 0.05))):
            session.send_velocity(self.args.velocity_step_deg_s)
            time.sleep(0.05)
        observed = session.collect_snapshot(timeout_s=1.0)
        session.send_velocity(0.0)
        time.sleep(0.2)
        session.send_power(False)
        if observed.velocity_deg_s is None:
            raise AssertionError("no velocity status after velocity command")
        if abs(observed.velocity_deg_s) < 0.2:
            raise AssertionError(f"velocity did not respond enough: {observed.velocity_deg_s}")
        return f"observed_velocity={observed.velocity_deg_s:.3f} deg/s"

    def _restore_safe_state(self) -> None:
        if self.session is None:
            return
        try:
            self.session.send_velocity(0.0)
        except Exception:
            pass
        try:
            self.session.send_power(False)
        except Exception:
            pass
        if self.original_limits is not None:
            try:
                self.session.send_limits(
                    self.original_limits["output_min_deg"],
                    self.original_limits["output_max_deg"],
                )
            except Exception:
                pass
        if self.original_config is not None:
            try:
                self.session.send_gear_ratio(float(self.original_config["gear_ratio"]))
            except Exception:
                pass
        if self.original_profile_code is not None:
            try:
                self.session.send_profile(self.original_profile_code)
            except Exception:
                pass

    def write_report(self) -> None:
        if not self.args.report:
            return
        data = {
            "ok": all(item["ok"] for item in self.results),
            "iface": self.args.iface,
            "node_id": self.args.node_id,
            "protocol_only": self.args.protocol_only,
            "allow_arm": self.args.allow_arm,
            "allow_motion": self.args.allow_motion,
            "results": self.results,
        }
        with open(self.args.report, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)

    def exit_code(self) -> int:
        return 0 if all(item["ok"] for item in self.results) else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run NoFW CAN protocol and live hardware spec tests."
    )
    parser.add_argument("--iface", default="can0", help="SocketCAN interface, default: can0")
    parser.add_argument("--node-id", type=int, default=7, help="CAN node ID, default: 7")
    parser.add_argument(
        "--protocol-only",
        action="store_true",
        help="Run local protocol/decoder tests only; do not touch CAN hardware.",
    )
    parser.add_argument(
        "--allow-arm",
        action="store_true",
        help="Allow tests that arm the power stage. Default tests stay disarmed.",
    )
    parser.add_argument(
        "--allow-motion",
        action="store_true",
        help="Allow small angle/velocity motion tests. Implies --allow-arm.",
    )
    parser.add_argument("--timeout", type=float, default=5.0, help="Live wait timeout in seconds.")
    parser.add_argument("--angle-step-deg", type=float, default=2.0, help="Motion test angle step.")
    parser.add_argument(
        "--velocity-step-deg-s",
        type=float,
        default=10.0,
        help="Motion test velocity command.",
    )
    parser.add_argument(
        "--motion-hold-s",
        type=float,
        default=0.6,
        help="How long to stream each motion command.",
    )
    parser.add_argument("--fail-fast", action="store_true", help="Stop at the first failed test.")
    parser.add_argument("--report", help="Optional JSON report path.")
    args = parser.parse_args()
    args.node_id = validate_node_id(args.node_id)
    args.iface = validate_iface(args.iface)
    if args.allow_motion:
        args.allow_arm = True
    return args


def main() -> int:
    args = parse_args()
    runner = SpecRunner(args)
    try:
        runner.run_protocol_tests()
        runner.run_live_tests()
    finally:
        runner.write_report()
    passed = sum(1 for item in runner.results if item["ok"])
    total = len(runner.results)
    print()
    print(f"SUMMARY {'PASS' if runner.exit_code() == 0 else 'FAIL'} {passed}/{total} passed")
    if args.report:
        print(f"REPORT {args.report}")
    return runner.exit_code()


if __name__ == "__main__":
    raise SystemExit(main())
