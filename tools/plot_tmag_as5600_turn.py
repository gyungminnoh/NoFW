#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
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


@dataclass
class Frame:
    timestamp: float
    can_id: int
    data: list[int]


@dataclass
class AngleEvent:
    timestamp: float
    phase: int | None
    ref_deg_wrapped: float


@dataclass
class VectorEvent:
    timestamp: float
    phase: int | None
    x_raw: int
    y_raw: int
    z_raw: int


@dataclass
class Sample:
    timestamp: float
    phase: int | None
    angle_deg_unwrapped: float
    x_raw: int
    y_raw: int
    z_raw: int

    def x_mt(self) -> float:
        return raw_to_mt(self.x_raw)

    def y_mt(self) -> float:
        return raw_to_mt(self.y_raw)

    def z_mt(self) -> float:
        return raw_to_mt(self.z_raw)


def raw_to_mt(raw_code: int, range_mt: float = 150.0) -> float:
    return (float(raw_code) / 32768.0) * range_mt


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


def build_events(frames: Iterable[Frame], node_id: int) -> tuple[list[AngleEvent], list[VectorEvent]]:
    summary_id = 0x780 + node_id
    angle_id = 0x790 + node_id
    vector_id = 0x7B0 + node_id

    current_phase: int | None = None
    angles: list[AngleEvent] = []
    vectors: list[VectorEvent] = []

    for frame in frames:
        data = frame.data
        if frame.can_id == summary_id and data[0] == 0xF1:
            current_phase = data[1]
            continue

        if frame.can_id == angle_id and data[0] == 0xF2:
            angles.append(
                AngleEvent(
                    timestamp=frame.timestamp,
                    phase=current_phase,
                    ref_deg_wrapped=le_i16(data[1], data[2]) / 100.0,
                )
            )
            continue

        if frame.can_id == vector_id and data[0] == 0xF4:
            vectors.append(
                VectorEvent(
                    timestamp=frame.timestamp,
                    phase=current_phase,
                    x_raw=le_i16(data[1], data[2]),
                    y_raw=le_i16(data[3], data[4]),
                    z_raw=le_i16(data[5], data[6]),
                )
            )

    return angles, vectors


def unwrap_angles_deg(angles: list[AngleEvent]) -> list[float]:
    if not angles:
        return []

    unwrapped: list[float] = []
    prev_wrapped = math.radians(angles[0].ref_deg_wrapped)
    acc = prev_wrapped
    unwrapped.append(math.degrees(acc))

    for event in angles[1:]:
        wrapped = math.radians(event.ref_deg_wrapped)
        acc += wrap_pm_pi(wrapped - prev_wrapped)
        prev_wrapped = wrapped
        unwrapped.append(math.degrees(acc))

    return unwrapped


def merge_samples(
    angles: list[AngleEvent],
    vectors: list[VectorEvent],
    phase_filter: int | None,
    max_gap_sec: float,
) -> list[Sample]:
    angle_unwrapped = unwrap_angles_deg(angles)
    merged: list[Sample] = []

    j = 0
    for idx, angle in enumerate(angles):
        if phase_filter is not None and angle.phase != phase_filter:
            continue

        while j + 1 < len(vectors) and vectors[j + 1].timestamp <= angle.timestamp:
            j += 1

        candidates: list[VectorEvent] = []
        if j < len(vectors):
            candidates.append(vectors[j])
        if j + 1 < len(vectors):
            candidates.append(vectors[j + 1])
        if j > 0:
            candidates.append(vectors[j - 1])

        best: VectorEvent | None = None
        best_dt = float("inf")
        for vector in candidates:
            if phase_filter is not None and vector.phase != phase_filter:
                continue
            dt = abs(vector.timestamp - angle.timestamp)
            if dt < best_dt:
                best_dt = dt
                best = vector

        if best is None or best_dt > max_gap_sec:
            continue

        merged.append(
            Sample(
                timestamp=angle.timestamp,
                phase=angle.phase,
                angle_deg_unwrapped=angle_unwrapped[idx],
                x_raw=best.x_raw,
                y_raw=best.y_raw,
                z_raw=best.z_raw,
            )
        )

    return merged


def select_turn(samples: list[Sample], turn_index: int) -> list[Sample]:
    if len(samples) < 2:
        raise ValueError("샘플이 너무 적어서 1회전 구간을 추출할 수 없습니다.")

    direction = 1.0 if (samples[-1].angle_deg_unwrapped - samples[0].angle_deg_unwrapped) >= 0.0 else -1.0

    segments: list[list[Sample]] = []
    start = 0
    while start < len(samples):
        start_angle = samples[start].angle_deg_unwrapped
        end = start
        while end + 1 < len(samples):
            next_delta = direction * (samples[end + 1].angle_deg_unwrapped - start_angle)
            if next_delta >= 360.0:
                end += 1
                break
            if next_delta < -5.0:
                break
            end += 1

        span = direction * (samples[end].angle_deg_unwrapped - start_angle)
        if span >= 360.0:
            segment = samples[start : end + 1]
            base = segment[0].angle_deg_unwrapped
            rel_samples = [
                Sample(
                    timestamp=s.timestamp,
                    phase=s.phase,
                    angle_deg_unwrapped=direction * (s.angle_deg_unwrapped - base),
                    x_raw=s.x_raw,
                    y_raw=s.y_raw,
                    z_raw=s.z_raw,
                )
                for s in segment
            ]
            segments.append(rel_samples)
            start = end
        else:
            start += 1

    if not segments:
        raise ValueError("로그 안에서 360도 이상 연속된 AS5600 구간을 찾지 못했습니다.")

    if turn_index < 0 or turn_index >= len(segments):
        raise ValueError(f"추출 가능한 회전 수는 {len(segments)}개인데 turn-index={turn_index}가 들어왔습니다.")

    return segments[turn_index]


def write_csv(samples: list[Sample], csv_path: Path, unit: str) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        if unit == "mt":
            writer.writerow(["angle_deg", "x_mt", "y_mt", "z_mt", "x_raw", "y_raw", "z_raw"])
            for s in samples:
                writer.writerow(
                    [
                        f"{s.angle_deg_unwrapped:.6f}",
                        f"{s.x_mt():.6f}",
                        f"{s.y_mt():.6f}",
                        f"{s.z_mt():.6f}",
                        s.x_raw,
                        s.y_raw,
                        s.z_raw,
                    ]
                )
        else:
            writer.writerow(["angle_deg", "x_raw", "y_raw", "z_raw"])
            for s in samples:
                writer.writerow([f"{s.angle_deg_unwrapped:.6f}", s.x_raw, s.y_raw, s.z_raw])


def plot_samples(samples: list[Sample], output_path: Path | None, unit: str, title: str, show: bool) -> None:
    try:
        import matplotlib
        if not show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "matplotlib is not installed for this Python. "
            "Use `.venv/bin/python tools/plot_tmag_as5600_turn.py ...`."
        ) from exc

    angles = [s.angle_deg_unwrapped for s in samples]

    if unit == "mt":
        x_vals = [s.x_mt() for s in samples]
        y_vals = [s.y_mt() for s in samples]
        z_vals = [s.z_mt() for s in samples]
        y_label = "Magnetic field [mT]"
    else:
        x_vals = [s.x_raw for s in samples]
        y_vals = [s.y_raw for s in samples]
        z_vals = [s.z_raw for s in samples]
        y_label = "TMAG raw code"

    plt.style.use("seaborn-v0_8-darkgrid")
    fig = plt.figure(figsize=(16, 9))
    gs = fig.add_gridspec(2, 3, height_ratios=[1.0, 1.2])

    ax_x = fig.add_subplot(gs[0, 0])
    ax_y = fig.add_subplot(gs[0, 1], sharex=ax_x)
    ax_z = fig.add_subplot(gs[0, 2], sharex=ax_x)
    ax_3d = fig.add_subplot(gs[1, :], projection="3d")

    x_limit = max(360.0, angles[-1])

    ax_x.plot(angles, x_vals, color="#0b84a5", linewidth=1.8)
    ax_x.set_xlim(0.0, x_limit)
    ax_x.set_ylabel(y_label)
    ax_x.set_title("TMAG X vs AS5600 angle")

    ax_y.plot(angles, y_vals, color="#f6c85f", linewidth=1.8)
    ax_y.set_xlim(0.0, x_limit)
    ax_y.set_ylabel(y_label)
    ax_y.set_title("TMAG Y vs AS5600 angle")

    ax_z.plot(angles, z_vals, color="#6f4e7c", linewidth=1.8)
    ax_z.set_xlim(0.0, x_limit)
    ax_z.set_xlabel("AS5600 angle [deg]")
    ax_z.set_ylabel(y_label)
    ax_z.set_title("TMAG Z vs AS5600 angle")

    scatter = ax_3d.scatter(x_vals, y_vals, z_vals, c=angles, cmap="viridis", s=18)
    ax_3d.set_xlabel("X")
    ax_3d.set_ylabel("Y")
    ax_3d.set_zlabel("Z")
    ax_3d.set_title("3D magnetic vector trajectory")
    fig.colorbar(scatter, ax=ax_3d, shrink=0.75, pad=0.1, label="AS5600 angle [deg]")

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
        description="Plot one AS5600 turn against TMAG XYZ from tmag_lut_angle_test candump logs."
    )
    parser.add_argument("input", nargs="?", help="candump -L 로그 파일 경로. 생략하면 stdin에서 읽습니다.")
    parser.add_argument("--node-id", type=int, default=7, help="CAN_NODE_ID 값")
    parser.add_argument(
        "--phase",
        type=parse_phase,
        default=CALIBRATING_PHASE,
        help="calibrating | validating | any (default: calibrating)",
    )
    parser.add_argument("--turn-index", type=int, default=0, help="추출할 360도 구간의 0-based 인덱스")
    parser.add_argument(
        "--max-gap-ms",
        type=float,
        default=30.0,
        help="F2 angle 프레임과 F4 vector 프레임을 짝지을 최대 시간 차이 [ms]",
    )
    parser.add_argument(
        "--unit",
        choices=("mt", "raw"),
        default="mt",
        help="그래프 단위. mt는 A2 기본 150mT full-scale 기준 환산",
    )
    parser.add_argument(
        "--save",
        default="capture/tmag_as5600_turn.png",
        help="저장할 그래프 파일 경로. 빈 문자열이면 저장하지 않습니다.",
    )
    parser.add_argument("--csv-out", default="capture/tmag_as5600_turn.csv", help="저장할 CSV 경로")
    parser.add_argument("--show", action="store_true", help="저장 후 GUI 창도 띄웁니다.")
    args = parser.parse_args()

    with open_input(args.input) as fp:
        frames = read_frames(fp)

    angles, vectors = build_events(frames, args.node_id)
    if not angles:
        raise SystemExit("F2 angle 프레임을 찾지 못했습니다. tmag_lut_angle_test 로그가 맞는지 확인해 주세요.")
    if not vectors:
        raise SystemExit("F4 vector 프레임을 찾지 못했습니다. tmag_lut_angle_test 로그가 맞는지 확인해 주세요.")

    samples = merge_samples(
        angles=angles,
        vectors=vectors,
        phase_filter=args.phase,
        max_gap_sec=args.max_gap_ms / 1000.0,
    )
    if not samples:
        raise SystemExit("angle/vector 프레임을 짝지은 샘플을 만들지 못했습니다.")

    turn_samples = select_turn(samples, args.turn_index)

    csv_path = Path(args.csv_out) if args.csv_out else None
    if csv_path is not None:
        write_csv(turn_samples, csv_path, args.unit)

    output_path = Path(args.save) if args.save else None
    phase_name = {None: "any", CALIBRATING_PHASE: "calibrating", VALIDATING_PHASE: "validating"}[args.phase]
    title = (
        f"TMAG vs AS5600 one-turn plot | phase={phase_name} | "
        f"turn={args.turn_index} | samples={len(turn_samples)} | node={args.node_id}"
    )
    plot_samples(turn_samples, output_path, args.unit, title, args.show)

    angle_span = turn_samples[-1].angle_deg_unwrapped - turn_samples[0].angle_deg_unwrapped
    print(f"samples={len(turn_samples)}")
    print(f"angle_span_deg={angle_span:.3f}")
    if output_path is not None:
        print(f"plot_saved={output_path}")
    if csv_path is not None:
        print(f"csv_saved={csv_path}")


if __name__ == "__main__":
    main()
