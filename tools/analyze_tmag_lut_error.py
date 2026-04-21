#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, TextIO

CALIBRATING_PHASE = 1
VALIDATING_PHASE = 2


def le_i16(lo: int, hi: int) -> int:
    value = (hi << 8) | lo
    if value & 0x8000:
        value -= 0x10000
    return value


def wrap_pm_pi(rad: float) -> float:
    while rad > math.pi:
        rad -= 2.0 * math.pi
    while rad < -math.pi:
        rad += 2.0 * math.pi
    return rad


def wrap_pm_180(deg: float) -> float:
    while deg > 180.0:
        deg -= 360.0
    while deg <= -180.0:
        deg += 360.0
    return deg


def percentile(sorted_values: list[float], q: float) -> float:
    if not sorted_values:
        raise ValueError("percentile input is empty")
    if len(sorted_values) == 1:
        return sorted_values[0]
    pos = (len(sorted_values) - 1) * q
    low = int(math.floor(pos))
    high = int(math.ceil(pos))
    if low == high:
        return sorted_values[low]
    t = pos - low
    return sorted_values[low] * (1.0 - t) + sorted_values[high] * t


@dataclass
class Frame:
    timestamp: float
    can_id: int
    data: list[int]


@dataclass
class AngleSample:
    timestamp: float
    phase: int | None
    ref_deg_wrapped: float
    est_deg_wrapped: float
    err_deg_wrapped: float
    best_bin: int


@dataclass
class UnwrappedSample:
    timestamp: float
    phase: int | None
    ref_deg: float
    est_deg: float
    err_deg: float
    best_bin: int
    direction: int


def parse_frame(line: str, fallback_ts: float) -> Frame | None:
    line = line.strip()
    if not line:
        return None

    timestamp = fallback_ts

    if line.startswith("("):
        close = line.find(")")
        if close > 1:
            try:
                timestamp = float(line[1:close])
                line = line[close + 1 :].strip()
            except ValueError:
                pass

    if "#" in line:
        try:
            payload = line.split()[-1]
            can_id_hex, data_hex = payload.split("#", 1)
            if len(data_hex) != 16:
                return None
            data = [int(data_hex[i : i + 2], 16) for i in range(0, 16, 2)]
            return Frame(timestamp=timestamp, can_id=int(can_id_hex, 16), data=data)
        except ValueError:
            return None

    parts = line.split()
    if len(parts) >= 4 and parts[-2] == "[8]":
        try:
            can_id = int(parts[-3], 16)
            data = [int(part, 16) for part in parts[-8:]]
            return Frame(timestamp=timestamp, can_id=can_id, data=data)
        except ValueError:
            return None

    return None


def read_frames(stream: TextIO) -> list[Frame]:
    frames: list[Frame] = []
    fallback_ts = 0.0
    for line in stream:
        frame = parse_frame(line, fallback_ts)
        if frame is None:
            continue
        frames.append(frame)
        fallback_ts = frame.timestamp + 1e-3
    return frames


def parse_angle_samples(frames: Iterable[Frame], node_id: int, phase_filter: int | None) -> list[AngleSample]:
    summary_id = 0x780 + node_id
    angle_id = 0x790 + node_id

    current_phase: int | None = None
    samples: list[AngleSample] = []

    for frame in frames:
        data = frame.data
        if frame.can_id == summary_id and data[0] == 0xF1:
            current_phase = data[1]
            continue

        if frame.can_id != angle_id or data[0] != 0xF2:
            continue
        if phase_filter is not None and current_phase != phase_filter:
            continue

        samples.append(
            AngleSample(
                timestamp=frame.timestamp,
                phase=current_phase,
                ref_deg_wrapped=le_i16(data[1], data[2]) / 100.0,
                est_deg_wrapped=le_i16(data[3], data[4]) / 100.0,
                err_deg_wrapped=le_i16(data[5], data[6]) / 100.0,
                best_bin=data[7],
            )
        )

    return samples


def unwrap_series_deg(values_wrapped_deg: list[float]) -> list[float]:
    if not values_wrapped_deg:
        return []

    prev_wrapped = math.radians(values_wrapped_deg[0])
    acc = prev_wrapped
    out = [math.degrees(acc)]

    for value_deg in values_wrapped_deg[1:]:
        wrapped = math.radians(value_deg)
        acc += wrap_pm_pi(wrapped - prev_wrapped)
        prev_wrapped = wrapped
        out.append(math.degrees(acc))

    return out


def build_unwrapped_samples(samples: list[AngleSample]) -> list[UnwrappedSample]:
    if not samples:
        return []

    ref_unwrapped = unwrap_series_deg([s.ref_deg_wrapped for s in samples])
    est_unwrapped = unwrap_series_deg([s.est_deg_wrapped for s in samples])

    out: list[UnwrappedSample] = []
    for idx, sample in enumerate(samples):
        err_deg = wrap_pm_180(est_unwrapped[idx] - ref_unwrapped[idx])
        out.append(
            UnwrappedSample(
                timestamp=sample.timestamp,
                phase=sample.phase,
                ref_deg=ref_unwrapped[idx],
                est_deg=est_unwrapped[idx],
                err_deg=err_deg,
                best_bin=sample.best_bin,
                direction=0,
            )
        )

    for idx in range(len(out)):
        if idx == 0:
            delta = out[1].ref_deg - out[0].ref_deg if len(out) > 1 else 0.0
        elif idx == len(out) - 1:
            delta = out[idx].ref_deg - out[idx - 1].ref_deg
        else:
            delta = out[idx + 1].ref_deg - out[idx - 1].ref_deg

        direction = 1 if delta >= 0.0 else -1
        out[idx] = UnwrappedSample(
            timestamp=out[idx].timestamp,
            phase=out[idx].phase,
            ref_deg=out[idx].ref_deg,
            est_deg=out[idx].est_deg,
            err_deg=out[idx].err_deg,
            best_bin=out[idx].best_bin,
            direction=direction,
        )

    return out


def harmonic_amplitude(samples: list[UnwrappedSample], harmonic: int) -> float:
    if not samples:
        return 0.0

    sin_sum = 0.0
    cos_sum = 0.0
    for sample in samples:
        theta = math.radians(sample.ref_deg % 360.0)
        sin_sum += sample.err_deg * math.sin(harmonic * theta)
        cos_sum += sample.err_deg * math.cos(harmonic * theta)

    n = float(len(samples))
    a = 2.0 * sin_sum / n
    b = 2.0 * cos_sum / n
    return math.sqrt(a * a + b * b)


def save_csv(samples: list[UnwrappedSample], csv_path: Path) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow(
            [
                "time_s",
                "ref_deg_unwrapped",
                "ref_deg_mod360",
                "est_deg_unwrapped",
                "est_deg_mod360",
                "err_deg_wrapped",
                "direction",
                "best_bin",
            ]
        )
        t0 = samples[0].timestamp if samples else 0.0
        for sample in samples:
            writer.writerow(
                [
                    f"{sample.timestamp - t0:.6f}",
                    f"{sample.ref_deg:.6f}",
                    f"{sample.ref_deg % 360.0:.6f}",
                    f"{sample.est_deg:.6f}",
                    f"{sample.est_deg % 360.0:.6f}",
                    f"{sample.err_deg:.6f}",
                    sample.direction,
                    sample.best_bin,
                ]
            )


def plot_samples(samples: list[UnwrappedSample], output_path: Path | None, title: str, show: bool) -> None:
    try:
        import matplotlib

        if not show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "matplotlib is not installed for this Python. "
            "Use `.venv/bin/python tools/analyze_tmag_lut_error.py ...`."
        ) from exc

    t0 = samples[0].timestamp
    times = [sample.timestamp - t0 for sample in samples]
    ref_deg = [sample.ref_deg for sample in samples]
    est_deg = [sample.est_deg for sample in samples]
    err_deg = [sample.err_deg for sample in samples]
    ref_mod = [sample.ref_deg % 360.0 for sample in samples]

    fwd = [sample for sample in samples if sample.direction > 0]
    rev = [sample for sample in samples if sample.direction < 0]

    plt.style.use("seaborn-v0_8-darkgrid")
    fig = plt.figure(figsize=(14, 10))
    gs = fig.add_gridspec(2, 2, height_ratios=[1.0, 1.0])

    ax_err_angle = fig.add_subplot(gs[0, 0])
    ax_err_time = fig.add_subplot(gs[0, 1])
    ax_angles = fig.add_subplot(gs[1, 0])
    ax_dir = fig.add_subplot(gs[1, 1])

    ax_err_angle.plot(ref_mod, err_deg, color="#e45756", linewidth=1.6)
    ax_err_angle.axhline(0.0, color="black", linewidth=0.8, alpha=0.5)
    ax_err_angle.set_xlim(0.0, 360.0)
    ax_err_angle.set_xlabel("AS5600 angle [deg]")
    ax_err_angle.set_ylabel("Error [deg]")
    ax_err_angle.set_title("LUT error vs AS5600 angle")

    ax_err_time.plot(times, err_deg, color="#6f4e7c", linewidth=1.6)
    ax_err_time.axhline(0.0, color="black", linewidth=0.8, alpha=0.5)
    ax_err_time.set_xlabel("Time [s]")
    ax_err_time.set_ylabel("Error [deg]")
    ax_err_time.set_title("LUT error vs time")

    ax_angles.plot(times, ref_deg, label="AS5600 ref", color="#0b84a5", linewidth=1.8)
    ax_angles.plot(times, est_deg, label="LUT estimate", color="#f6c85f", linewidth=1.6)
    ax_angles.set_xlabel("Time [s]")
    ax_angles.set_ylabel("Angle [deg]")
    ax_angles.set_title("AS5600 vs LUT angle")
    ax_angles.legend(loc="best")

    if fwd:
        ax_dir.plot(
            [sample.ref_deg % 360.0 for sample in fwd],
            [sample.err_deg for sample in fwd],
            label="Forward",
            color="#0b84a5",
            linewidth=1.4,
        )
    if rev:
        ax_dir.plot(
            [sample.ref_deg % 360.0 for sample in rev],
            [sample.err_deg for sample in rev],
            label="Reverse",
            color="#f28e2b",
            linewidth=1.4,
        )
    ax_dir.axhline(0.0, color="black", linewidth=0.8, alpha=0.5)
    ax_dir.set_xlim(0.0, 360.0)
    ax_dir.set_xlabel("AS5600 angle [deg]")
    ax_dir.set_ylabel("Error [deg]")
    ax_dir.set_title("Forward / Reverse error comparison")
    if fwd or rev:
        ax_dir.legend(loc="best")

    fig.suptitle(title, fontsize=14)
    fig.tight_layout()

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output_path, dpi=180, bbox_inches="tight")

    if show:
        plt.show()
    else:
        plt.close(fig)


def open_input(path: str | None) -> TextIO:
    if path is None or path == "-":
        return sys.stdin
    return open(path, "r", encoding="utf-8")


def parse_phase(value: str) -> int | None:
    lowered = value.lower()
    if lowered in {"any", "all"}:
        return None
    if lowered in {"cal", "calibrating"}:
        return CALIBRATING_PHASE
    if lowered in {"val", "validating"}:
        return VALIDATING_PHASE
    raise argparse.ArgumentTypeError("phase는 calibrating, validating, any 중 하나여야 합니다.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Analyze LUT-estimated angle accuracy against AS5600 from tmag_lut_angle_test candump logs."
    )
    parser.add_argument("input", nargs="?", help="candump -L 로그 파일 경로. 생략하면 stdin에서 읽습니다.")
    parser.add_argument("--node-id", type=int, default=7, help="CAN_NODE_ID 값")
    parser.add_argument(
        "--phase",
        type=parse_phase,
        default=VALIDATING_PHASE,
        help="calibrating | validating | any (default: validating)",
    )
    parser.add_argument(
        "--save",
        default="capture/tmag_lut_error_analysis.png",
        help="저장할 그래프 파일 경로. 빈 문자열이면 저장하지 않습니다.",
    )
    parser.add_argument(
        "--csv-out",
        default="capture/tmag_lut_error_analysis.csv",
        help="저장할 CSV 경로. 빈 문자열이면 저장하지 않습니다.",
    )
    parser.add_argument(
        "--skip-first",
        type=int,
        default=0,
        help="분석에서 제외할 초기 샘플 수 (default: 0)",
    )
    parser.add_argument("--show", action="store_true", help="저장 후 GUI 창도 띄웁니다.")
    args = parser.parse_args()

    with open_input(args.input) as fp:
        frames = read_frames(fp)

    samples = parse_angle_samples(frames, args.node_id, args.phase)
    if not samples:
        raise SystemExit("F2 angle 프레임을 찾지 못했습니다. 로그와 phase를 확인해 주세요.")

    unwrapped = build_unwrapped_samples(samples)
    if not unwrapped:
        raise SystemExit("분석 가능한 샘플이 없습니다.")
    if args.skip_first < 0:
        raise SystemExit("--skip-first는 0 이상이어야 합니다.")
    if args.skip_first >= len(unwrapped):
        raise SystemExit("skip-first 값이 전체 샘플 수보다 크거나 같습니다.")
    if args.skip_first > 0:
        unwrapped = unwrapped[args.skip_first:]

    err_abs = [abs(sample.err_deg) for sample in unwrapped]
    err_sq = [sample.err_deg * sample.err_deg for sample in unwrapped]
    err_sorted = sorted(err_abs)

    rms = math.sqrt(sum(err_sq) / len(err_sq))
    mae = sum(err_abs) / len(err_abs)
    max_abs = max(err_abs)
    p95 = percentile(err_sorted, 0.95)
    bias = statistics.fmean(sample.err_deg for sample in unwrapped)
    stddev = statistics.pstdev(sample.err_deg for sample in unwrapped)
    harm_1x = harmonic_amplitude(unwrapped, 1)
    harm_8x = harmonic_amplitude(unwrapped, 8)

    forward = [sample for sample in unwrapped if sample.direction > 0]
    reverse = [sample for sample in unwrapped if sample.direction < 0]

    csv_path = Path(args.csv_out) if args.csv_out else None
    if csv_path is not None:
        save_csv(unwrapped, csv_path)

    output_path = Path(args.save) if args.save else None
    phase_name = {None: "any", CALIBRATING_PHASE: "calibrating", VALIDATING_PHASE: "validating"}[args.phase]
    title = f"TMAG LUT angle error analysis | phase={phase_name} | samples={len(unwrapped)} | node={args.node_id}"
    plot_samples(unwrapped, output_path, title, args.show)

    print(f"samples={len(unwrapped)}")
    print(f"rms_deg={rms:.6f}")
    print(f"mae_deg={mae:.6f}")
    print(f"max_abs_deg={max_abs:.6f}")
    print(f"p95_abs_deg={p95:.6f}")
    print(f"bias_deg={bias:.6f}")
    print(f"stddev_deg={stddev:.6f}")
    print(f"harmonic_1x_deg={harm_1x:.6f}")
    print(f"harmonic_8x_deg={harm_8x:.6f}")
    print(f"forward_samples={len(forward)}")
    print(f"reverse_samples={len(reverse)}")
    print(f"skip_first={args.skip_first}")
    if output_path is not None:
        print(f"plot_saved={output_path}")
    if csv_path is not None:
        print(f"csv_saved={csv_path}")


if __name__ == "__main__":
    main()
