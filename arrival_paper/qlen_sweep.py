# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Modelled hypothetical-decoder queue latency: service-time sweep.

Physical setup — single-server FIFO queue downstream of the databento
handler:

    arrivals   : each message's handlerendtim (moment the upstream
                 databento handler finished emitting it — that's when
                 the message becomes visible to any downstream consumer,
                 e.g. Kaspar's own decoder).
    service    : deterministic per-message service time in [1, 20] μs
                 (swept parameter). Represents a hypothetical downstream
                 decoder's per-message cost — the reader plugs in their
                 own budget.
    server     : one server, FIFO, work-conserving.

We model a HYPOTHETICAL decoder, not our actual system: we don't know
CME → handler transit exactly, so we can't reconstruct arrivals from
sendingTime; instead we take the moment the databento handler finished
each message and ask "if a downstream decoder took X μs to service each
message, what latency would every arrival see?".

For each service time in the sweep we run a G/D/1 simulation and report:

    p50 and p99 of per-message system time
        = departure − arrival = wait_in_queue + service_time

Output: a two-line PNG chart, x-axis service_us in [1, 20], y-axis μs.

CLI:
    python3 -m arrival_paper.qlen_sweep \\
        --msg-tape /vast/…/318/message/20250129.NQH5.csv \\
        --service-us-min 1 --service-us-max 20 \\
        --after-hh-et 14 --before-hh-et 16 \\
        --out arrival_paper/figs/qlen_sweep_20250129_fomc_pm.png
"""

from __future__ import annotations

import argparse
import csv
import gzip
import sys
from datetime import datetime, time as dtime
from pathlib import Path
from zoneinfo import ZoneInfo

import numpy as np
import matplotlib.pyplot as plt


NY = ZoneInfo("America/New_York")


def load_handlerend_time_ns(msg_tape_csv: str,
                            after_hh_et: float | None = None,
                            before_hh_et: float | None = None) -> np.ndarray:
    """Read handlerendtim column from the msgtape CSV, return sorted
    int64 ns array of arrivals (drop rows with handlerendtim <= 0).

    handlerendtim is the moment the upstream databento handler finished
    emitting each message; that's the "arrival" into our hypothetical
    downstream decoder queue.

    If after_hh_et / before_hh_et are given, filter to handlerendtim
    whose New-York wall-clock time is in [after_hh, before_hh). Fractional
    hours accepted (e.g. 14.5 for 14:30).
    """
    opener = gzip.open if msg_tape_csv.endswith(".gz") else open
    ts: list[int] = []
    with opener(msg_tape_csv, "rt", newline="") as f:
        r = csv.reader(f)
        h = next(r)
        if "handlerendtim" not in h:
            raise ValueError(
                f"tape {msg_tape_csv}: missing 'handlerendtim' column; "
                f"header is {h}")
        i_he = h.index("handlerendtim")
        for row in r:
            try:
                v = int(row[i_he])
            except (IndexError, ValueError):
                continue
            if v > 0:
                ts.append(v)
    a = np.asarray(ts, dtype=np.int64)
    a.sort()
    # Guard: even with no filter, the caller may have received an empty tape
    # (all handlerendtim=0). Return empty array; downstream users check size.
    if len(a) == 0:
        return a
    if after_hh_et is None and before_hh_et is None:
        return a
    # ET-anchored hours-of-session filter. Overnight CME sessions (Sun 18:00
    # ET open through Mon 17:00 ET close) cross midnight, so an event's
    # hours-since-ET-midnight can exceed 24. To let a caller express "keep
    # RTH on the calendar day the session CLOSES on", we anchor to the ET
    # date whose midnight-to-midnight window contains the MEDIAN event,
    # not the first event. Values above 24 are wrapped: a Mon 09:30 event
    # in a Sun-open session reads as hour 9.5, not 33.5.
    anchor_ns = int(np.median(a))
    dt = datetime.fromtimestamp(anchor_ns / 1e9, tz=NY)
    et_midnight = datetime.combine(dt.date(), dtime(0, 0), tzinfo=NY)
    et_mid_ns = int(et_midnight.timestamp() * 1e9)
    hours = ((a - et_mid_ns) / 1e9 / 3600.0) % 24.0
    m = np.ones(len(a), dtype=bool)
    if after_hh_et is not None:
        m &= hours >= after_hh_et
    if before_hh_et is not None:
        m &= hours < before_hh_et
    return a[m]


def simulate_gd1(arrivals_ns: np.ndarray, service_ns: int) -> np.ndarray:
    """G/D/1 queue simulation.

    arrivals_ns : sorted arrival timestamps (int64 ns), with any receive
                  overhead already added.
    service_ns  : deterministic per-message service time in ns.

    Returns the per-message SYSTEM TIME (int64 ns) = depart - arrival.
    """
    n = len(arrivals_ns)
    if n == 0:
        return np.empty(0, dtype=np.int64)
    depart = np.empty(n, dtype=np.int64)
    a = arrivals_ns
    depart[0] = a[0] + service_ns
    for i in range(1, n):
        d_prev = depart[i - 1]
        depart[i] = (a[i] if a[i] > d_prev else d_prev) + service_ns
    return depart - a


def sweep_service_time(arrivals_ns: np.ndarray,
                       service_us_min: int, service_us_max: int) -> dict:
    """Run the G/D/1 sim across service_us in [min, max] (inclusive step 1).
    Returns dict of arrays: service_us, p50_us, p99_us."""
    service_us = np.arange(service_us_min, service_us_max + 1, dtype=np.int64)
    p50 = np.empty(len(service_us), dtype=np.float64)
    p99 = np.empty(len(service_us), dtype=np.float64)
    for k, s_us in enumerate(service_us):
        sys_ns = simulate_gd1(arrivals_ns, int(s_us) * 1_000)
        p50[k] = float(np.quantile(sys_ns, 0.50)) / 1_000.0
        p99[k] = float(np.quantile(sys_ns, 0.99)) / 1_000.0
        print(f"  service={s_us:>3} μs  p50={p50[k]:>7.2f} μs  "
              f"p99={p99[k]:>12.2f} μs", file=sys.stderr)
    return {"service_us": service_us, "p50_us": p50, "p99_us": p99}


def plot_sweep(sweep: dict, session_label: str, out_path: str) -> None:
    fig, ax = plt.subplots(figsize=(8.0, 5.0))
    ax.plot(sweep["service_us"], sweep["p50_us"], marker="o", linewidth=2,
            label="p50 system time")
    ax.plot(sweep["service_us"], sweep["p99_us"], marker="s", linewidth=2,
            label="p99 system time")
    ax.set_xlabel("Service time per message (μs)")
    ax.set_ylabel("Modelled system time (μs)")
    ax.set_yscale("log")
    ax.grid(True, which="both", linewidth=0.3, alpha=0.4)
    ax.set_title(
        f"Hypothetical downstream-decoder queue — G/D/1 on handlerendtim\n"
        f"session {session_label}"
    )
    ax.legend(loc="upper left")
    fig.tight_layout()
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    print(f"wrote {out_path}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True)
    ap.add_argument("--service-us-min", type=int, default=1)
    ap.add_argument("--service-us-max", type=int, default=20)
    ap.add_argument("--after-hh-et", type=float, default=None,
                    help="filter arrivals to on/after this ET hour")
    ap.add_argument("--before-hh-et", type=float, default=None,
                    help="filter arrivals to strictly before this ET hour")
    ap.add_argument("--session-label", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    print(f"[qlen_sweep] load {args.msg_tape}", file=sys.stderr)
    arrivals = load_handlerend_time_ns(args.msg_tape,
                                       after_hh_et=args.after_hh_et,
                                       before_hh_et=args.before_hh_et)
    print(f"[qlen_sweep] {len(arrivals):,} arrivals (handlerendtim)", file=sys.stderr)
    if len(arrivals) < 100:
        print("[qlen_sweep] too few arrivals — aborting", file=sys.stderr)
        return 1

    print(f"[qlen_sweep] sweep service {args.service_us_min}..{args.service_us_max} μs",
          file=sys.stderr)
    sw = sweep_service_time(arrivals, args.service_us_min, args.service_us_max)
    label = args.session_label or Path(args.msg_tape).stem
    plot_sweep(sw, label, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
