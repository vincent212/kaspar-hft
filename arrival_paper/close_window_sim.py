# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Two-model Lindley on 15:45-16:00 ET close window.

PACKET-only : arrivals = min(transactTime) per packet_seq; service = 7.23 us
SPAN-scaled : same arrivals; service = 7.23 us + 0.312 us * (span - 1)

CLI:
    python3 -m arrival_paper.close_window_sim --days 20250303 20250304 ...
"""
from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd
import pytz


FLOOR_NS = 7_230
SLOPE_NS = 312
TAPE_DIR = Path("/vast/home/vmayeski/out/arrival_paper/tapes/318/message")


def close_window_ns(date_str: str) -> tuple[int, int]:
    et = pytz.timezone("America/New_York")
    d = pd.Timestamp(f"{date_str[:4]}-{date_str[4:6]}-{date_str[6:]}")
    t0 = et.localize(d.replace(hour=15, minute=45).to_pydatetime()).astimezone(pytz.UTC)
    t1 = et.localize(d.replace(hour=16, minute=0 ).to_pydatetime()).astimezone(pytz.UTC)
    return int(pd.Timestamp(t0).value), int(pd.Timestamp(t1).value)


def lindley(arr_ns: np.ndarray, service_ns: np.ndarray) -> np.ndarray:
    """Sequential Lindley recursion. Returns latency (wait+service) per event."""
    n = len(arr_ns)
    wait = np.zeros(n, dtype=np.int64)
    for i in range(1, n):
        w = int(wait[i - 1]) + int(service_ns[i - 1]) - int(arr_ns[i] - arr_ns[i - 1])
        wait[i] = w if w > 0 else 0
    return wait + service_ns


def process_day(date_str: str) -> dict:
    tapes = list(TAPE_DIR.glob(f"{date_str}.*.csv"))
    if len(tapes) != 1:
        raise RuntimeError(f"expected exactly one tape for {date_str}, got {tapes}")
    t0, t1 = close_window_ns(date_str)
    seq_to_msgs: dict[int, list[int]] = defaultdict(list)
    n_read = 0
    with open(tapes[0]) as f:
        r = csv.reader(f); h = next(r)
        i_tt  = h.index("transactTime")
        i_seq = h.index("packet_seq")
        for row in r:
            n_read += 1
            try:
                tt  = int(row[i_tt])
                seq = int(row[i_seq])
            except (IndexError, ValueError):
                continue
            if not (t0 <= tt < t1):
                continue
            seq_to_msgs[seq].append(tt)
    print(f"  [{date_str}] read {n_read:,} rows, kept {sum(len(v) for v in seq_to_msgs.values()):,} msgs in {len(seq_to_msgs):,} packets", file=sys.stderr)

    packets = sorted((min(tts), len(tts)) for tts in seq_to_msgs.values())
    arr = np.array([p[0] for p in packets], dtype=np.int64)
    spans = np.array([p[1] for p in packets], dtype=np.int64)

    svA = np.full(len(arr), FLOOR_NS, dtype=np.int64)
    print(f"  [{date_str}] running Lindley A (PACKET-only)…", file=sys.stderr)
    latA = lindley(arr, svA)

    svB = (FLOOR_NS + SLOPE_NS * (spans - 1)).astype(np.int64)
    print(f"  [{date_str}] running Lindley B (SPAN-scaled)…", file=sys.stderr)
    latB = lindley(arr, svB)

    def q(x: np.ndarray) -> dict[float, float]:
        return {p: float(np.quantile(x, p)) for p in [0.5, 0.95, 0.99, 0.999, 0.9999]}

    qA, qB = q(latA), q(latB)
    return {
        "date": date_str,
        "n_msg": int(spans.sum()),
        "n_pkt": len(arr),
        "span_mean": float(spans.mean()),
        "span_max": int(spans.max()),
        "A_p50_us":   qA[0.5]/1e3,
        "A_p95_us":   qA[0.95]/1e3,
        "A_p99_us":   qA[0.99]/1e3,
        "A_p999_us":  qA[0.999]/1e3,
        "A_p9999_us": qA[0.9999]/1e3,
        "A_max_us":   float(latA.max())/1e3,
        "B_p50_us":   qB[0.5]/1e3,
        "B_p95_us":   qB[0.95]/1e3,
        "B_p99_us":   qB[0.99]/1e3,
        "B_p999_us":  qB[0.999]/1e3,
        "B_p9999_us": qB[0.9999]/1e3,
        "B_max_us":   float(latB.max())/1e3,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", nargs="+", required=True)
    args = ap.parse_args()

    rows = []
    for d in args.days:
        try:
            rows.append(process_day(d))
        except Exception as e:
            print(f"  [{d}] FAILED: {e}", file=sys.stderr)
    df = pd.DataFrame(rows)

    print()
    print(f"Two-model Lindley on 15:45-16:00 ET  (all latencies microseconds)")
    print(f"floor={FLOOR_NS/1e3} us, slope={SLOPE_NS/1e3} us")
    print(f"A = PACKET-only (span ignored)   B = SPAN-scaled")
    print()
    cols = ["date","n_msg","n_pkt","span_mean","span_max",
            "A_p99_us","B_p99_us","A_p999_us","B_p999_us","A_max_us","B_max_us"]
    print(df[cols].to_string(index=False, float_format=lambda x: f"{x:.2f}"))
    print()
    df["ratio_B_over_A_p99"]  = df["B_p99_us"]  / df["A_p99_us"]
    df["ratio_B_over_A_p999"] = df["B_p999_us"] / df["A_p999_us"]
    df["ratio_B_over_A_max"]  = df["B_max_us"]  / df["A_max_us"]
    print("SPAN-scaled / PACKET-only ratios (>>1 means span drives tails):")
    print(df[["date","ratio_B_over_A_p99","ratio_B_over_A_p999","ratio_B_over_A_max"]]
          .to_string(index=False, float_format=lambda x: f"{x:.3f}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
