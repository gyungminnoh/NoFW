#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from typing import TextIO

import matplotlib

if (
    sys.platform.startswith("linux")
    and os.environ.get("WAYLAND_DISPLAY")
    and not os.environ.get("QT_QPA_PLATFORM")
):
    os.environ["QT_QPA_PLATFORM"] = "wayland"

if sys.platform.startswith("linux"):
    try:
        import PyQt5  # noqa: F401

        matplotlib.use("QtAgg")
    except ModuleNotFoundError:
        pass

try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError as exc:
    raise SystemExit(
        "matplotlib is not installed for this Python. "
        "Use `.venv/bin/python tools/live_plot_tmag_xyz.py ...`."
    ) from exc

if matplotlib.get_backend().lower() == "agg" and sys.platform.startswith("linux"):
    raise SystemExit(
        "A GUI matplotlib backend is not available. "
        "Run with `.venv/bin/python ...` after installing PyQt5, or use a desktop session."
    )


def le_i16(lo: int, hi: int) -> int:
    value = (hi << 8) | lo
    if value & 0x8000:
        value -= 0x10000
    return value


def parse_frame(line: str) -> tuple[int, list[int]] | None:
    line = line.strip()
    if not line:
        return None

    if "#" in line:
        try:
            payload = line.split()[-1]
            can_id_hex, data_hex = payload.split("#", 1)
            if len(data_hex) != 16:
                return None
            return int(can_id_hex, 16), [int(data_hex[i:i + 2], 16) for i in range(0, 16, 2)]
        except ValueError:
            return None

    parts = line.split()
    if len(parts) >= 4 and parts[-2] == "[8]":
        try:
            return int(parts[-3], 16), [int(part, 16) for part in parts[-8:]]
        except ValueError:
            return None

    return None


@dataclass
class LiveSample:
    t: float
    x_mt: float | None = None
    y_mt: float | None = None
    z_mt: float | None = None
    x_raw: int | None = None
    y_raw: int | None = None
    z_raw: int | None = None


class LiveStore:
    def __init__(self, window_sec: float) -> None:
        self.window_sec = window_sec
        self.samples: deque[LiveSample] = deque()
        self.lock = threading.Lock()
        self.sample_count = 0
        self.last_status: tuple[int, int, int] | None = None
        self.last_variant = "?"

    def add_mt(self, now: float, data: list[int]) -> None:
        sample = LiveSample(
            t=now,
            x_mt=le_i16(data[1], data[2]) / 100.0,
            y_mt=le_i16(data[3], data[4]) / 100.0,
            z_mt=le_i16(data[5], data[6]) / 100.0,
        )
        variant = data[7]
        if variant == 2:
            self.last_variant = "A2"
        elif variant == 1:
            self.last_variant = "A1"
        else:
            self.last_variant = str(variant)

        with self.lock:
            self.samples.append(sample)
            self.sample_count += 1
            self._trim(now)

    def add_raw(self, now: float, data: list[int]) -> None:
        raw_sample = LiveSample(
            t=now,
            x_raw=le_i16(data[1], data[2]),
            y_raw=le_i16(data[3], data[4]),
            z_raw=le_i16(data[5], data[6]),
        )

        with self.lock:
            if self.samples and abs(self.samples[-1].t - now) < 0.1 and self.samples[-1].x_raw is None:
                self.samples[-1].x_raw = raw_sample.x_raw
                self.samples[-1].y_raw = raw_sample.y_raw
                self.samples[-1].z_raw = raw_sample.z_raw
            else:
                self.samples.append(raw_sample)
                self.sample_count += 1
            self._trim(now)

    def set_status(self, data: list[int]) -> None:
        conv = (data[2] << 8) | data[1]
        afe = (data[4] << 8) | data[3]
        sys_status = (data[6] << 8) | data[5]
        with self.lock:
            self.last_status = (conv, afe, sys_status)

    def snapshot(self) -> tuple[list[LiveSample], int, tuple[int, int, int] | None, str]:
        with self.lock:
            return list(self.samples), self.sample_count, self.last_status, self.last_variant

    def _trim(self, now: float) -> None:
        cutoff = now - self.window_sec
        while self.samples and self.samples[0].t < cutoff:
            self.samples.popleft()


def consume_stream(stream: TextIO, store: LiveStore, stop_event: threading.Event) -> None:
    while not stop_event.is_set():
        line = stream.readline()
        if line == "":
            break

        parsed = parse_frame(line)
        if parsed is None:
            continue

        can_id, data = parsed
        now = time.monotonic()

        if can_id == 0x607 and data[0] == 0xE6:
            store.set_status(data)
        elif can_id == 0x617 and data[0] == 0xE7:
            store.add_mt(now, data)
        elif can_id == 0x627 and data[0] == 0xE8:
            store.add_raw(now, data)


def build_series(samples: list[LiveSample], now: float) -> dict[str, list[float]]:
    series = {
        "mt_t": [],
        "raw_t": [],
        "x_mt": [],
        "y_mt": [],
        "z_mt": [],
        "x_raw": [],
        "y_raw": [],
        "z_raw": [],
    }

    for sample in samples:
        rel_t = sample.t - now
        if sample.x_mt is not None:
            series["mt_t"].append(rel_t)
            series["x_mt"].append(sample.x_mt)
            series["y_mt"].append(sample.y_mt if sample.y_mt is not None else float("nan"))
            series["z_mt"].append(sample.z_mt if sample.z_mt is not None else float("nan"))
        if sample.x_raw is not None:
            series["raw_t"].append(rel_t)
            series["x_raw"].append(sample.x_raw)
            series["y_raw"].append(sample.y_raw if sample.y_raw is not None else float("nan"))
            series["z_raw"].append(sample.z_raw if sample.z_raw is not None else float("nan"))

    return series


def main() -> None:
    parser = argparse.ArgumentParser(description="Live plot TMAG XYZ candump stream.")
    parser.add_argument("--interface", default="can0", help="SocketCAN interface for candump")
    parser.add_argument("--window", type=float, default=15.0, help="Visible time window in seconds")
    parser.add_argument(
        "--stdin",
        action="store_true",
        help="Read candump lines from stdin instead of starting candump",
    )
    args = parser.parse_args()

    store = LiveStore(window_sec=args.window)
    stop_event = threading.Event()
    proc: subprocess.Popen[str] | None = None

    if args.stdin:
        stream = sys.stdin
    else:
        proc = subprocess.Popen(
            ["candump", "-L", args.interface],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        if proc.stdout is None:
            raise SystemExit("Failed to start candump.")
        stream = proc.stdout

    reader = threading.Thread(target=consume_stream, args=(stream, store, stop_event), daemon=True)
    reader.start()

    plt.style.use("seaborn-v0_8-darkgrid")
    fig, (ax_mt, ax_raw) = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    if fig.canvas.manager is not None:
        fig.canvas.manager.set_window_title("TMAG XYZ Live Viewer")

    line_x_mt, = ax_mt.plot([], [], color="#0b84a5", linewidth=1.8, label="X [mT]")
    line_y_mt, = ax_mt.plot([], [], color="#f6c85f", linewidth=1.8, label="Y [mT]")
    line_z_mt, = ax_mt.plot([], [], color="#6f4e7c", linewidth=1.8, label="Z [mT]")

    line_x_raw, = ax_raw.plot([], [], color="#0b84a5", linewidth=1.4, label="X raw")
    line_y_raw, = ax_raw.plot([], [], color="#f6c85f", linewidth=1.4, label="Y raw")
    line_z_raw, = ax_raw.plot([], [], color="#6f4e7c", linewidth=1.4, label="Z raw")

    ax_mt.set_ylabel("Magnetic field [mT]")
    ax_raw.set_ylabel("Raw code")
    ax_raw.set_xlabel("Time [s]")
    ax_mt.legend(loc="upper left")
    ax_raw.legend(loc="upper left")

    status_text = fig.text(0.02, 0.97, "", ha="left", va="top", fontsize=10)
    plt.tight_layout(rect=(0, 0, 1, 0.95))

    try:
        while plt.fignum_exists(fig.number):
            samples, sample_count, status, variant = store.snapshot()
            now = time.monotonic()
            series = build_series(samples, now)

            line_x_mt.set_data(series["mt_t"], series["x_mt"])
            line_y_mt.set_data(series["mt_t"], series["y_mt"])
            line_z_mt.set_data(series["mt_t"], series["z_mt"])

            line_x_raw.set_data(series["raw_t"], series["x_raw"])
            line_y_raw.set_data(series["raw_t"], series["y_raw"])
            line_z_raw.set_data(series["raw_t"], series["z_raw"])

            ax_mt.set_xlim(-args.window, 0.0)
            ax_raw.set_xlim(-args.window, 0.0)

            if series["x_mt"]:
                mt_min = min(
                    min(series["x_mt"]),
                    min(series["y_mt"]),
                    min(series["z_mt"]),
                )
                mt_max = max(
                    max(series["x_mt"]),
                    max(series["y_mt"]),
                    max(series["z_mt"]),
                )
                if mt_min == mt_max:
                    pad = 1.0
                else:
                    pad = max(1.0, 0.1 * (mt_max - mt_min))
                ax_mt.set_ylim(mt_min - pad, mt_max + pad)

            if series["x_raw"]:
                raw_min = min(
                    min(series["x_raw"]),
                    min(series["y_raw"]),
                    min(series["z_raw"]),
                )
                raw_max = max(
                    max(series["x_raw"]),
                    max(series["y_raw"]),
                    max(series["z_raw"]),
                )
                if raw_min == raw_max:
                    pad = 32.0
                else:
                    pad = max(32.0, 0.1 * (raw_max - raw_min))
                ax_raw.set_ylim(raw_min - pad, raw_max + pad)

            if status is None:
                status_str = "status: waiting"
            else:
                status_str = f"status: CONV=0x{status[0]:04X} AFE=0x{status[1]:04X} SYS=0x{status[2]:04X}"
            status_text.set_text(
                f"TMAG5170 live viewer | variant={variant} | samples={sample_count} | "
                f"window={args.window:.1f}s | {status_str}"
            )

            fig.canvas.draw_idle()
            plt.pause(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    main()
