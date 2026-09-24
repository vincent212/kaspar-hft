# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Modelled hypothetical-decoder queue latency vs service time, sliced by
arrival intensity.

The paper's centrepiece chart. Same G/D/1 simulation as `qlen_sweep`,
but partitioned into contrasting-intensity windows so the reader can
see the two regimes:

    low intensity   (quiet market)  →  queue drains between arrivals,
                                       p99 ≈ service_time, flat line
    high intensity  (FOMC / news)   →  service_time approaches burst
                                       inter-arrival, p99 diverges
                                       super-linearly, hockey stick

Each --window entry has the form "HH.hh:HH.hh:label", e.g.
    --window 3.0:5.0:overnight-quiet
    --window 10.0:11.0:RTH-normal
    --window 14.0:14.25:FOMC-statement
    --window 14.5:15.5:FOMC-press-conf

Chart: x = service_us in [service-us-min, service-us-max], y = p99 μs,
one line per window (log-y). This is the tail-vs-service diagram that
motivates the paper: same downstream decoder budget produces different
p99s depending on where in the CME NQ arrival regime you're sitting.

CLI:
    python3 -m arrival_paper.qlen_sweep_by_intensity \\
        --msg-tape /vast/…/318/message/20250129.NQH5.csv \\
        --service-us-min 1 --service-us-max 30 \\
        --window 3:5:overnight-quiet \\
        --window 10:11:RTH-normal \\
        --window 14:14.25:FOMC-statement \\
        --window 14.5:15.5:FOMC-press-conf \\
        --session-label "NQH5 20250129" \\
        --out arrival_paper/figs/qlen_by_intensity_20250129.png
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt

from arrival_paper.qlen_sweep import (
    load_handlerend_time_ns,
    simulate_gd1,
)


def sweep_one_window(arrivals_ns: np.ndarray,
                     service_us_min: int,
                     service_us_max: int) -> dict:
    """G/D/1 sweep on one window. Returns arrays service_us, p50_us, p99_us."""
    service_us = np.arange(service_us_min, service_us_max + 1, dtype=np.int64)
    p50 = np.empty(len(service_us), dtype=np.float64)
    p99 = np.empty(len(service_us), dtype=np.float64)
    for k, s_us in enumerate(service_us):
        sys_ns = simulate_gd1(arrivals_ns, int(s_us) * 1_000)
        p50[k] = float(np.quantile(sys_ns, 0.50)) / 1_000.0
        p99[k] = float(np.quantile(sys_ns, 0.99)) / 1_000.0
    return {"service_us": service_us, "p50_us": p50, "p99_us": p99}


def parse_window(spec: str) -> tuple[float, float, str]:
    """Parse 'HH.hh:HH.hh:label' → (lo, hi, label)."""
    parts = spec.split(":")
    if len(parts) != 3:
        raise ValueError(f"expected HH:HH:label, got {spec!r}")
    return float(parts[0]), float(parts[1]), parts[2]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True)
    ap.add_argument("--service-us-min", type=int, default=1)
    ap.add_argument("--service-us-max", type=int, default=30)
    ap.add_argument("--window", action="append", required=True,
                    help="'HH.hh:HH.hh:label' ET wall-clock window; "
                         "specify --window multiple times")
    ap.add_argument("--session-label", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    windows = [parse_window(s) for s in args.window]
    print(f"[qlen_by_intensity] load {args.msg_tape}", file=sys.stderr)

    results = []
    for lo, hi, label in windows:
        arr = load_handlerend_time_ns(args.msg_tape,
                                      after_hh_et=lo, before_hh_et=hi)
        if len(arr) < 100:
            print(f"[qlen_by_intensity] SKIP {label} ({lo}-{hi} ET): "
                  f"only {len(arr)} arrivals", file=sys.stderr)
            continue
        dur_s = (hi - lo) * 3600.0
        rate = len(arr) / dur_s
        print(f"[qlen_by_intensity] window {label} ({lo:.2f}-{hi:.2f} ET): "
              f"{len(arr):,} arrivals, rate={rate:,.0f}/s", file=sys.stderr)
        r = sweep_one_window(arr, args.service_us_min, args.service_us_max)
        r["label"] = f"{label} ({rate:,.0f}/s)"
        r["rate"] = rate
        results.append(r)
        for k, s_us in enumerate(r["service_us"]):
            print(f"    service={int(s_us):>3} μs  p50={r['p50_us'][k]:>7.2f}  "
                  f"p99={r['p99_us'][k]:>10.2f}", file=sys.stderr)

    # Sort so lowest-rate is drawn first (visually cleaner in the legend)
    results.sort(key=lambda r: r["rate"])

    fig, ax = plt.subplots(figsize=(9.0, 6.0))
    for r in results:
        ax.plot(r["service_us"], r["p99_us"], marker="o", linewidth=2,
                label=r["label"])
    ax.set_xlabel("Service time per message (μs)")
    ax.set_ylabel("Modelled p99 system time (μs)")
    ax.set_yscale("log")
    ax.grid(True, which="both", linewidth=0.3, alpha=0.4)
    ax.set_title(
        f"Hypothetical downstream-decoder queue p99 vs service time,\n"
        f"conditioned on arrival intensity — session {args.session_label}"
    )
    ax.legend(loc="upper left", fontsize=9)
    fig.tight_layout()
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out, dpi=140, bbox_inches="tight")
    print(f"wrote {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
