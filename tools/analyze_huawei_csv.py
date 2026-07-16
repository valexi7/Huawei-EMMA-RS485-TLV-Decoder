#!/usr/bin/env python3
"""Replay Huawei RS485 logger CSV files and rank register interpretations.

The logger CSV stores a human-readable summary plus the complete frame after
the final ``raw=`` marker. This tool extracts that frame, validates its Modbus
CRC, replays FC41/0x35 register blocks, reports capture-backed inverter fields,
and ranks three-value candidates by how closely their sum follows inverter
active power.

Only Python's standard library is required.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import math
import re
import statistics
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


CURRENT_DATA_SUBFUNCTION = 0x35
AMBIGUOUS_THREE_VALUE_CANDIDATES = {
    "0x90FB i16[3]": (0x90FB, 3, 2),
}

# Register, words, signed, scale. These mappings are backed by the July 2026
# SUN2000-10KTL-M1 capture and portal screenshots.
KNOWN_FIELDS = {
    "pv1_voltage": (0x7D10, 1, False, 0.1, "V"),
    "pv1_current": (0x7D11, 1, False, 0.01, "A"),
    "pv2_voltage": (0x7D12, 1, False, 0.1, "V"),
    "pv2_current": (0x7D13, 1, False, 0.01, "A"),
    "input_power": (0x7D40, 2, True, 1.0, "W"),
    "line_ab_voltage": (0x7D42, 1, False, 0.1, "V"),
    "line_bc_voltage": (0x7D43, 1, False, 0.1, "V"),
    "line_ca_voltage": (0x7D44, 1, False, 0.1, "V"),
    "phase_a_current": (0x7D48, 2, True, 0.001, "A"),
    "phase_b_current": (0x7D4A, 2, True, 0.001, "A"),
    "phase_c_current": (0x7D4C, 2, True, 0.001, "A"),
    "active_power_reference": (0x7D50, 2, True, 1.0, "W"),
    "active_power": (0x7D5F, 2, True, 1.0, "W"),
    "reactive_power": (0x7D66, 2, True, 1.0, "var"),
    "power_factor": (0x7D54, 1, True, 0.001, ""),
    "grid_frequency": (0x7D55, 1, False, 0.01, "Hz"),
    "internal_temperature": (0x7D57, 1, True, 0.1, "C"),
    "insulation_resistance": (0x7D58, 1, False, 0.001, "MOhm"),
    "inverter_status": (0x7D59, 1, False, 1.0, ""),
    "inverter_fault_code": (0x7D5A, 1, False, 1.0, ""),
    "inverter_alarm_1": (0x7D08, 1, False, 1.0, ""),
    "inverter_alarm_2": (0x7D09, 1, False, 1.0, ""),
    "inverter_alarm_3": (0x7D0A, 1, False, 1.0, ""),
    "total_yield": (0x7D6A, 2, False, 0.01, "kWh"),
    "daily_yield": (0x7D72, 2, False, 0.01, "kWh"),
    "daily_dc_energy_yield": (0x7D80, 2, False, 0.01, "kWh"),
    "mppt1_total_dc_energy_yield": (0x7DD4, 2, False, 0.01, "kWh"),
    "battery_running_status": (0x9088, 1, False, 1.0, ""),
    "battery_working_mode": (0x908E, 1, False, 1.0, ""),
    "battery_charged_today": (0x9097, 2, False, 0.01, "kWh"),
    "battery_discharged_today": (0x9099, 2, False, 0.01, "kWh"),
    "battery_charged_today_legacy": (0x9398, 2, False, 0.01, "kWh"),
    "battery_discharged_today_legacy": (0x939A, 2, False, 0.01, "kWh"),
    "battery_maximum_charge_power": (0xB7E3, 2, False, 1.0, "W"),
    "battery_maximum_discharge_power": (0xB7E5, 2, False, 1.0, "W"),
    "battery_end_of_charge_soc": (0xB7E9, 1, False, 0.1, "%"),
    "battery_end_of_discharge_soc": (0xB7EA, 1, False, 0.1, "%"),
    "battery_forced_period": (0xB7EB, 1, False, 1.0, "min"),
    "battery_forced_power": (0xB7EC, 2, True, 1.0, "W"),
    "battery_working_mode_setting": (0xB7EE, 1, False, 1.0, ""),
    "battery_charge_from_grid": (0xB7EF, 1, False, 1.0, ""),
    "battery_grid_charge_cutoff_soc": (0xB7F0, 1, False, 0.1, "%"),
    "phase_a_voltage": (0x90ED, 2, True, 0.1, "V"),
    "phase_b_voltage": (0x90EF, 2, True, 0.1, "V"),
    "phase_c_voltage": (0x90F1, 2, True, 0.1, "V"),
    "meter_phase_a_current": (0x90F3, 2, True, 0.01, "A"),
    "meter_phase_b_current": (0x90F5, 2, True, 0.01, "A"),
    "meter_phase_c_current": (0x90F7, 2, True, 0.01, "A"),
    "meter_active_power": (0x90F9, 2, True, 1.0, "W"),
    "meter_reactive_power": (0x90FB, 2, True, 1.0, "var"),
    "meter_power_factor": (0x90FD, 1, True, 0.001, ""),
    "line_ab_voltage_fast": (0x9106, 2, True, 0.1, "V"),
    "line_bc_voltage_fast": (0x9108, 2, True, 0.1, "V"),
    "line_ca_voltage_fast": (0x910A, 2, True, 0.1, "V"),
    "meter_phase_a_active_power": (0x910C, 2, True, 1.0, "W"),
    "meter_phase_b_active_power": (0x910E, 2, True, 1.0, "W"),
    "meter_phase_c_active_power": (0x9110, 2, True, 1.0, "W"),
}


@dataclass(frozen=True)
class Block:
    start: int
    words: int
    data: bytes


@dataclass(frozen=True)
class Observation:
    second: int
    timestamp: str
    values: tuple[int, int, int]

    @property
    def total(self) -> int:
        return sum(self.values)


def modbus_crc(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def extract_raw_frame(value: str) -> bytes:
    try:
        parsed = json.loads(value)
    except (json.JSONDecodeError, TypeError):
        parsed = None
    if isinstance(parsed, dict) and isinstance(parsed.get("raw"), str):
        value = parsed["raw"]
    raw = value[value.rfind("raw=") + 4 :] if "raw=" in value else value
    return bytes(int(pair, 16) for pair in re.findall(r"(?<![0-9A-Fa-f])[0-9A-Fa-f]{2}(?![0-9A-Fa-f])", raw))


def timestamp_second(value: str) -> int:
    match = re.search(r"(\d{1,2})[.:](\d{2})[.:](\d{2})", value)
    if not match:
        raise ValueError(f"Timestamp has no time-of-day: {value!r}")
    hour, minute, second = map(int, match.groups())
    return hour * 3600 + minute * 60 + second


def frame_bus_clock(blocks: Iterable[Block]) -> int | None:
    for block in blocks:
        unix_seconds = read_register(block, 0x7D6E, 2)
        if unix_seconds is not None and 1_577_836_800 <= unix_seconds < 2_208_988_800:
            return unix_seconds
    return None


def read_register(block: Block, register: int, words: int, signed: bool = False) -> int | None:
    offset_words = register - block.start
    if offset_words < 0 or offset_words + words > block.words:
        return None
    start = offset_words * 2
    end = start + words * 2
    if end > len(block.data):
        return None
    return int.from_bytes(block.data[start:end], "big", signed=signed)


def parse_current_data_frame(frame: bytes) -> tuple[int, bool, list[Block]]:
    if len(frame) < 8 or frame[1] != 0x41 or frame[2] != CURRENT_DATA_SUBFUNCTION:
        raise ValueError("Not an FC41/0x35 frame")
    length = frame[3]
    if 4 + length + 2 != len(frame):
        raise ValueError("Frame length does not match payload length")
    payload = frame[4 : 4 + length]
    if len(payload) < 2:
        raise ValueError("Payload has no sequence/count header")
    raw_count = payload[1]
    expected_count = raw_count & 0x7F
    blocks: list[Block] = []
    position = 2
    for _ in range(expected_count):
        if position + 3 > len(payload):
            raise ValueError("Truncated register-block header")
        start = int.from_bytes(payload[position : position + 2], "big")
        words = payload[position + 2]
        end = position + 3 + words * 2
        if end > len(payload):
            raise ValueError("Register block crosses the payload boundary")
        blocks.append(Block(start, words, payload[position + 3 : end]))
        position = end
    if position != len(payload):
        raise ValueError("Register-block count did not consume the payload")
    return raw_count, bool(raw_count & 0x80), blocks


def correlation(xs: list[float], ys: list[float]) -> float | None:
    if len(xs) < 2:
        return None
    mean_x = statistics.fmean(xs)
    mean_y = statistics.fmean(ys)
    numerator = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    denominator_x = sum((x - mean_x) ** 2 for x in xs)
    denominator_y = sum((y - mean_y) ** 2 for y in ys)
    if not denominator_x or not denominator_y:
        return None
    return numerator / math.sqrt(denominator_x * denominator_y)


def nearest_value(times: list[int], values: list[int], target: int, maximum_distance: int = 180) -> int | None:
    position = bisect.bisect_left(times, target)
    choices = [index for index in (position - 1, position) if 0 <= index < len(times)]
    if not choices:
        return None
    index = min(choices, key=lambda item: abs(times[item] - target))
    return values[index] if abs(times[index] - target) <= maximum_distance else None


def candidate_score(observations: Iterable[Observation], active_series: list[dict]) -> dict | None:
    items = list(observations)
    active = sorted(active_series, key=lambda item: item["second"])
    times = [item["second"] for item in active]
    values = [item["raw"] for item in active]
    pairs = []
    for item in items:
        target = nearest_value(times, values, item.second)
        if target is not None:
            pairs.append((item.total, target))
    if len(pairs) < 10:
        return None
    sums = [pair[0] for pair in pairs]
    totals = [pair[1] for pair in pairs]
    errors = [abs(total - target) for total, target in pairs]
    return {
        "observations": len(items),
        "paired": len(pairs),
        "correlation": correlation(sums, totals),
        "median_abs_error_w": statistics.median(errors),
        "mean_abs_error_w": statistics.fmean(errors),
        "last": {
            "timestamp": items[-1].timestamp,
            "values": list(items[-1].values),
            "sum": items[-1].total,
        },
    }


def summarize(items: list[dict]) -> dict | None:
    if not items:
        return None
    return {
        "count": len(items),
        "first": items[0],
        "last": items[-1],
        "minimum": min(item["value"] for item in items),
        "maximum": max(item["value"] for item in items),
    }


def analyze(path: Path, top: int = 20) -> dict:
    fields: dict[str, list[dict]] = defaultdict(list)
    candidates_16: dict[int, list[Observation]] = defaultdict(list)
    candidates_32: dict[int, list[Observation]] = defaultdict(list)
    starts: dict[int, dict] = {}
    stats = {
        "rows": 0,
        "parsed": 0,
        "crc_valid": 0,
        "aligned": 0,
        "flagged": 0,
        "malformed": 0,
        "inferred_timestamps": 0,
    }
    first_timestamp = None
    last_timestamp = None
    last_clock_row = None
    last_clock_second = None

    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        columns = {name.casefold(): name for name in (reader.fieldnames or [])}
        timestamp_column = columns.get("timestamp")
        frame_column = columns.get("rs485frame")
        if timestamp_column is None or frame_column is None:
            raise ValueError(f"Expected timestamp and rs485Frame columns, got {reader.fieldnames}")
        for row in reader:
            stats["rows"] += 1
            timestamp = row[timestamp_column]
            first_timestamp = first_timestamp or timestamp
            last_timestamp = timestamp
            try:
                frame = extract_raw_frame(row[frame_column])
                raw_count, flagged, blocks = parse_current_data_frame(frame)
            except (TypeError, ValueError):
                stats["malformed"] += 1
                continue
            try:
                second = timestamp_second(timestamp)
            except (TypeError, ValueError):
                clock_second = frame_bus_clock(blocks)
                if clock_second is not None:
                    second = clock_second
                    last_clock_row = stats["rows"]
                    last_clock_second = clock_second
                elif last_clock_row is not None and last_clock_second is not None:
                    # Captures normally contain a bus-clock window every few
                    # frames. Three seconds per intervening row keeps the
                    # timeline monotonic until the next exact clock anchor.
                    second = last_clock_second + ((stats["rows"] - last_clock_row) * 3)
                else:
                    second = stats["rows"] * 3
                stats["inferred_timestamps"] += 1
            stats["parsed"] += 1
            stats["crc_valid"] += modbus_crc(frame[:-2]) == int.from_bytes(frame[-2:], "little")
            stats["aligned"] += len(blocks) == (raw_count & 0x7F)
            stats["flagged"] += flagged

            for block in blocks:
                entry = starts.setdefault(block.start, {"count": 0, "word_counts": set(), "last": timestamp})
                entry["count"] += 1
                entry["word_counts"].add(block.words)
                entry["last"] = timestamp
                for name, (register, words, signed, scale, unit) in KNOWN_FIELDS.items():
                    raw = read_register(block, register, words, signed)
                    if raw is not None:
                        fields[name].append(
                            {
                                "second": second,
                                "timestamp": timestamp,
                                "raw": raw,
                                "value": raw * scale,
                                "unit": unit,
                                "block": f"0x{block.start:04X}",
                                "words": block.words,
                            }
                        )
                for offset in range(max(0, block.words - 2)):
                    values = tuple(
                        int.from_bytes(block.data[(offset + index) * 2 : (offset + index + 1) * 2], "big", signed=True)
                        for index in range(3)
                    )
                    if all(abs(value) <= 20000 for value in values):
                        candidates_16[block.start + offset].append(Observation(second, timestamp, values))
                for offset in range(max(0, block.words - 5)):
                    values = tuple(
                        int.from_bytes(block.data[(offset + index * 2) * 2 : (offset + index * 2 + 2) * 2], "big", signed=True)
                        for index in range(3)
                    )
                    if all(abs(value) <= 20000 for value in values):
                        candidates_32[block.start + offset].append(Observation(second, timestamp, values))

    active_series = fields["active_power"] or fields["active_power_reference"]
    scores = []
    for width, candidates in ((16, candidates_16), (32, candidates_32)):
        for register, observations in candidates.items():
            score = candidate_score(observations, active_series)
            if score is not None:
                score.update({"register": f"0x{register:04X}", "width": width})
                scores.append(score)
    scores.sort(key=lambda item: (-(abs(item["correlation"] or 0)), item["median_abs_error_w"]))

    code_candidates = {}
    for label, (register, _, width) in AMBIGUOUS_THREE_VALUE_CANDIDATES.items():
        source = candidates_32 if width == 4 else candidates_16
        code_candidates[label] = candidate_score(source.get(register, []), active_series)

    return {
        "source": str(path.resolve()),
        "first_timestamp": first_timestamp,
        "last_timestamp": last_timestamp,
        "frame_stats": stats,
        "fields": {name: summarize(fields[name]) for name in KNOWN_FIELDS},
        "ambiguous_three_value_candidates": code_candidates,
        "ranked_candidates": scores[:top],
        "block_starts": {
            f"0x{register:04X}": {
                "count": value["count"],
                "word_counts": sorted(value["word_counts"]),
                "last": value["last"],
            }
            for register, value in sorted(starts.items())
        },
    }


def print_report(result: dict) -> None:
    stats = result["frame_stats"]
    print(f"Capture: {result['first_timestamp']} -> {result['last_timestamp']}")
    print(
        f"Frames: rows={stats['rows']} parsed={stats['parsed']} crc_valid={stats['crc_valid']} "
        f"aligned={stats['aligned']} flagged={stats['flagged']} malformed={stats['malformed']}"
    )
    print("\nLatest known inverter values:")
    for name, summary in result["fields"].items():
        if summary is None:
            continue
        last = summary["last"]
        print(f"  {name:28s} {last['value']:10.3f} {last['unit']:<5s}  {last['timestamp']} ({last['block']})")
    print("\nAmbiguous three-value tags versus active power:")
    for label, score in result["ambiguous_three_value_candidates"].items():
        if score is None:
            print(f"  {label:18s} insufficient paired observations")
            continue
        correlation_value = score["correlation"]
        correlation_text = "n/a" if correlation_value is None else f"{correlation_value:.3f}"
        print(
            f"  {label:18s} corr={correlation_text:>6s} median_error={score['median_abs_error_w']:8.1f} W "
            f"last={score['last']['values']} sum={score['last']['sum']} W"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="Huawei logger CSV to analyze")
    parser.add_argument("--json", type=Path, help="Write the complete analysis as JSON")
    parser.add_argument("--top", type=int, default=20, help="Number of heuristic candidates to retain")
    args = parser.parse_args()
    result = analyze(args.csv, max(0, args.top))
    print_report(result)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
