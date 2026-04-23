#!/usr/bin/env python3
import argparse
import json
import statistics
import time
import urllib.request
from dataclasses import dataclass
from typing import Any


@dataclass
class Sample:
    t_s: float
    angle_deg: float
    velocity_deg_s: float
    armed: bool


class CanUiClient:
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip("/")

    def get_state(self) -> dict[str, Any]:
        with urllib.request.urlopen(self.base_url + "/api/state", timeout=2.0) as response:
            return json.loads(response.read().decode("utf-8"))

    def post(self, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
        data = json.dumps(payload or {}).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + path,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(request, timeout=2.0) as response:
            body = json.loads(response.read().decode("utf-8"))
            if not body.get("ok"):
                raise RuntimeError(body.get("error", "request failed"))
            return body

    def wait_live(self, timeout_s: float = 5.0) -> dict[str, Any]:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            state = self.get_state()
            if state.get("link_alive") and state.get("diag", {}).get("active_profile"):
                return state
            time.sleep(0.1)
        raise RuntimeError("UI did not report live firmware status in time")

    def disarm_stop(self) -> None:
        last_state: dict[str, Any] | None = None
        for _ in range(5):
            try:
                self.post("/api/power", {"armed": False})
            finally:
                self.post("/api/stop_stream", {})
            time.sleep(0.25)
            last_state = self.get_state()
            if last_state.get("diag", {}).get("armed") is False:
                return
        raise RuntimeError(f"failed to confirm disarm: {last_state}")


def state_to_sample(t0: float, state: dict[str, Any]) -> Sample:
    return Sample(
        t_s=time.time() - t0,
        angle_deg=float(state["angle_deg"]),
        velocity_deg_s=float(state["velocity_deg_s"]),
        armed=bool(state.get("diag", {}).get("armed")),
    )


def collect_samples(client: CanUiClient, duration_s: float, period_s: float) -> list[Sample]:
    samples: list[Sample] = []
    t0 = time.time()
    deadline = t0 + duration_s
    while time.time() < deadline:
        samples.append(state_to_sample(t0, client.get_state()))
        time.sleep(period_s)
    samples.append(state_to_sample(t0, client.get_state()))
    return samples


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def summarize_angle_step(samples: list[Sample], initial: float, target: float) -> dict[str, float]:
    direction = 1.0 if target >= initial else -1.0
    angles = [s.angle_deg for s in samples]
    velocities = [s.velocity_deg_s for s in samples]
    final_tail = angles[-max(1, min(5, len(angles))):]
    directed_positions = [(a - target) * direction for a in angles]
    overshoot = max(0.0, max(directed_positions))
    step_mag = abs(target - initial)
    t_90_s = 0.0
    first_within_1deg_s = 0.0
    settle_within_1deg_s = 0.0

    if step_mag > 0.0:
        threshold_90 = initial + direction * step_mag * 0.9
        for sample in samples:
            if (sample.angle_deg - threshold_90) * direction >= 0.0:
                t_90_s = sample.t_s
                break

    for sample in samples:
        if abs(sample.angle_deg - target) <= 1.0:
            first_within_1deg_s = sample.t_s
            break

    for idx, sample in enumerate(samples):
        if all(abs(s.angle_deg - target) <= 1.0 for s in samples[idx:]):
            settle_within_1deg_s = sample.t_s
            break

    return {
        "initial_deg": initial,
        "target_deg": target,
        "final_deg": angles[-1],
        "tail_avg_deg": mean(final_tail),
        "tail_error_deg": mean(final_tail) - target,
        "min_deg": min(angles),
        "max_deg": max(angles),
        "overshoot_deg": overshoot,
        "max_abs_velocity_deg_s": max(abs(v) for v in velocities),
        "t_90_s": t_90_s,
        "first_within_1deg_s": first_within_1deg_s,
        "settle_within_1deg_s": settle_within_1deg_s,
    }


def summarize_velocity_step(samples: list[Sample], command: float) -> dict[str, float]:
    velocities = [s.velocity_deg_s for s in samples]
    angles = [s.angle_deg for s in samples]
    half = max(1, len(velocities) // 2)
    tail = velocities[half:]
    direction = 1.0 if command >= 0.0 else -1.0
    command_mag = abs(command)
    t_90_s = 0.0
    first_within_5pct_s = 0.0
    settle_within_5pct_s = 0.0
    overshoot = 0.0

    if command_mag > 0.0:
        threshold_90 = command * 0.9
        tolerance = max(1.0, command_mag * 0.05)
        for sample in samples:
            if sample.velocity_deg_s * direction >= threshold_90 * direction:
                t_90_s = sample.t_s
                break
        for sample in samples:
            if abs(sample.velocity_deg_s - command) <= tolerance:
                first_within_5pct_s = sample.t_s
                break
        for idx, sample in enumerate(samples):
            if all(abs(s.velocity_deg_s - command) <= tolerance for s in samples[idx:]):
                settle_within_5pct_s = sample.t_s
                break
        overshoot = max(0.0, max((v - command) * direction for v in velocities))

    return {
        "command_deg_s": command,
        "tail_avg_deg_s": mean(tail),
        "tail_error_deg_s": mean(tail) - command,
        "tail_std_deg_s": statistics.pstdev(tail) if len(tail) > 1 else 0.0,
        "min_velocity_deg_s": min(velocities),
        "max_velocity_deg_s": max(velocities),
        "overshoot_deg_s": overshoot,
        "max_abs_velocity_deg_s": max(abs(v) for v in velocities),
        "t_90_s": t_90_s,
        "first_within_5pct_s": first_within_5pct_s,
        "settle_within_5pct_s": settle_within_5pct_s,
        "angle_delta_deg": angles[-1] - angles[0],
    }


def print_summary(title: str, summary: dict[str, float]) -> None:
    print(title)
    for key, value in summary.items():
        print(f"  {key}: {value:.3f}")


def run_angle_step(client: CanUiClient, step_deg: float, settle_s: float, sample_period_s: float) -> dict[str, float]:
    before = client.wait_live()
    initial = float(before["angle_deg"])
    target = initial + step_deg

    client.post("/api/hold", {})
    time.sleep(0.25)
    client.post("/api/power", {"armed": True})
    time.sleep(0.5)
    client.post("/api/angle", {"deg": target})
    samples = collect_samples(client, settle_s, sample_period_s)
    return summarize_angle_step(samples, initial, target)


def run_velocity_step(client: CanUiClient, velocity_deg_s: float, duration_s: float,
                      sample_period_s: float) -> dict[str, float]:
    client.post("/api/power", {"armed": True})
    time.sleep(0.25)
    client.post("/api/velocity", {"deg_s": velocity_deg_s})
    samples = collect_samples(client, duration_s, sample_period_s)
    client.post("/api/zero_velocity", {})
    time.sleep(0.25)
    return summarize_velocity_step(samples, velocity_deg_s)


def main() -> int:
    parser = argparse.ArgumentParser(description="Measure small CAN step responses through the web UI API.")
    parser.add_argument("--base-url", default="http://127.0.0.1:8765")
    parser.add_argument("--angle-step-deg", type=float, default=0.5)
    parser.add_argument("--angle-settle-s", type=float, default=1.8)
    parser.add_argument("--velocity-deg-s", type=float, default=5.0)
    parser.add_argument("--velocity-duration-s", type=float, default=1.2)
    parser.add_argument("--sample-period-s", type=float, default=0.05)
    parser.add_argument("--skip-angle", action="store_true")
    parser.add_argument("--skip-velocity", action="store_true")
    args = parser.parse_args()

    client = CanUiClient(args.base_url)
    state = client.wait_live()
    print(
        "initial_state:",
        json.dumps(
            {
                "angle_deg": state.get("angle_deg"),
                "velocity_deg_s": state.get("velocity_deg_s"),
                "profile": state.get("diag", {}).get("active_profile"),
                "armed": state.get("diag", {}).get("armed"),
                "stream": state.get("stream"),
            },
            ensure_ascii=False,
        ),
    )

    try:
        client.disarm_stop()
        if not args.skip_angle:
            angle_summary = run_angle_step(
                client,
                args.angle_step_deg,
                args.angle_settle_s,
                args.sample_period_s,
            )
            print_summary("angle_step", angle_summary)
        client.disarm_stop()

        if not args.skip_velocity:
            velocity_summary = run_velocity_step(
                client,
                args.velocity_deg_s,
                args.velocity_duration_s,
                args.sample_period_s,
            )
            print_summary("velocity_step", velocity_summary)
    finally:
        client.disarm_stop()
        time.sleep(0.3)
        final = client.wait_live()
        print(
            "final_state:",
            json.dumps(
                {
                    "angle_deg": final.get("angle_deg"),
                    "velocity_deg_s": final.get("velocity_deg_s"),
                    "profile": final.get("diag", {}).get("active_profile"),
                    "armed": final.get("diag", {}).get("armed"),
                    "stream": final.get("stream"),
                },
                ensure_ascii=False,
            ),
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
