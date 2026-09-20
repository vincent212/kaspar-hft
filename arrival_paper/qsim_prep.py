# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Stage 1 of the two-stage sim pipeline: parse tapes + fit Hawkes + cache.

Reads each session's CSV tape once, extracts per-window packet-arrival streams,
fits an exponential-kernel Hawkes MLE per window, generates a matched-count
uniform-shuffle Poisson-null arrival stream (deterministic per (session, window)),
and writes the results to a compact on-disk cache:

    <out-dir>/metadata.parquet       one row per (session, window) with
                                     n_messages, n_packets, span_mean, span_max,
                                     lambda_bar_pkt, n_branch_pkt, converged, rho

    <out-dir>/arrivals/{session}.npz keyed w{window_id}_H (real arrivals, int64 ns)
                                     and w{window_id}_P (Poisson-null arrivals,
                                     int64 ns).

Stage 2 (`qsim_run.py`) reads this cache and runs the tandem Lindley grid
with numba-njit'd recursions, so scenario sweeps take seconds.

CLI:
    python3 -m arrival_paper.qsim_prep \\
        --tapes-dir /vast/home/vmayeski/out/arrival_paper/tapes/318/message \\
        --out-dir   arrival_paper/figs/qsim_cache \\
        --jobs      40
"""
from __future__ import annotations

import argparse
import csv
import glob
import multiprocessing as mp
import os
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd

from arrival_paper.hawkes_smoke import fit_hawkes


DEFAULT_WINDOW_MIN      = 30
MIN_PACKETS_PER_WINDOW  = 500
MIN_MESSAGES_PER_WINDOW = 1000
FLOOR_NS                = 7_230  # for rho calculation only; sim uses grid T
CHECKPOINT_EVERY        = 25


def process_session(msg_tape_csv: str, out_arrivals_dir: str) -> pd.DataFrame:
    """Parse one session tape and write its arrivals to <out>/{session}.npz.
    Returns a DataFrame with one row per accepted 30-min RTH window."""
    tt: list[int] = []
    seq: list[int] = []
    with open(msg_tape_csv, "rt", newline="") as f:
        r = csv.reader(f)
        h = next(r)
        i_tt  = h.index("transactTime")
        i_seq = h.index("packet_seq")
        for row in r:
            try:
                v_tt  = int(row[i_tt])
                v_seq = int(row[i_seq])
            except (IndexError, ValueError):
                continue
            tt.append(v_tt)
            seq.append(v_seq)

    tt_arr  = np.asarray(tt,  dtype=np.int64)
    seq_arr = np.asarray(seq, dtype=np.int64)
    order   = np.argsort(tt_arr, kind="stable")
    tt_arr  = tt_arr[order]
    seq_arr = seq_arr[order]
    if len(tt_arr) == 0:
        return pd.DataFrame()

    win_ns  = DEFAULT_WINDOW_MIN * 60 * 1_000_000_000
    first   = tt_arr[0]
    win_idx = (tt_arr - first) // win_ns

    session   = Path(msg_tape_csv).stem
    rows      = []
    npz_dict: dict[str, np.ndarray] = {}

    for wid in np.unique(win_idx):
        wm = win_idx == wid
        tt_w  = tt_arr[wm]
        seq_w = seq_arr[wm]
        n_messages = int(len(tt_w))
        if n_messages < MIN_MESSAGES_PER_WINDOW:
            continue

        # Packet grouping via packet_seq: arrival = min(transactTime) per packet.
        seq_to_min = defaultdict(lambda: np.iinfo(np.int64).max)
        seq_to_cnt = defaultdict(int)
        for t, s in zip(tt_w, seq_w):
            if t < seq_to_min[s]:
                seq_to_min[s] = t
            seq_to_cnt[s] += 1
        n_packets = len(seq_to_min)
        if n_packets < MIN_PACKETS_PER_WINDOW:
            continue

        pairs = sorted((seq_to_min[s], seq_to_cnt[s]) for s in seq_to_min)
        pkt_arr = np.array([p[0] for p in pairs], dtype=np.int64)
        spans   = np.array([p[1] for p in pairs], dtype=np.int64)

        window_span_ns = int(pkt_arr[-1] - pkt_arr[0])
        rho = float(n_packets * FLOOR_NS) / max(1, window_span_ns)

        # Packet-Hawkes fit on packet arrivals.
        try:
            fit = fit_hawkes(pkt_arr)
            lam_pkt   = float(fit["lambda_bar"])
            n_pkt     = float(fit["n_branch"])
            converged = bool(fit["converged"])
        except (ValueError, RuntimeError):
            lam_pkt = float("nan"); n_pkt = float("nan"); converged = False

        # Poisson null: uniform-shuffle same arrival count over the window.
        # Deterministic per (session, window_id) via a stable seed. Matches the
        # legacy qsim_grid.py so cached results are directly comparable.
        seed = abs(hash((session, int(wid)))) % (2**32)
        rng = np.random.default_rng(seed)
        poi_arr = np.sort(
            rng.integers(low=int(pkt_arr[0]), high=int(pkt_arr[-1]) + 1,
                         size=n_packets)
        ).astype(np.int64)

        rows.append({
            "session":        session,
            "window_id":      int(wid),
            "n_messages":     n_messages,
            "n_packets":      n_packets,
            "span_mean":      float(spans.mean()),
            "span_max":       int(spans.max()),
            "lambda_bar_pkt": lam_pkt,
            "n_branch_pkt":   n_pkt,
            "converged":      converged,
            "rho":            rho,
        })
        npz_dict[f"w{int(wid)}_H"] = pkt_arr
        npz_dict[f"w{int(wid)}_P"] = poi_arr

    if rows:
        np.savez_compressed(Path(out_arrivals_dir) / f"{session}.npz",
                            **npz_dict)
    return pd.DataFrame(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tapes-dir", required=True)
    ap.add_argument("--out-dir",   required=True,
                    help="cache root; writes metadata.parquet and "
                         "arrivals/{session}.npz here")
    ap.add_argument("--jobs", type=int, default=max(1, mp.cpu_count() // 2))
    args = ap.parse_args()

    out = Path(args.out_dir)
    (out / "arrivals").mkdir(parents=True, exist_ok=True)

    tapes = sorted(glob.glob(os.path.join(args.tapes_dir, "*.csv")))
    print(f"[prep] found {len(tapes)} tapes; jobs={args.jobs}", file=sys.stderr)

    parts: list[pd.DataFrame] = []
    with mp.Pool(args.jobs) as pool:
        it = pool.starmap(
            process_session,
            [(t, str(out / "arrivals")) for t in tapes],
            chunksize=1,
        )
        for i, df in enumerate(it):
            parts.append(df)
            nrows = 0 if df is None or df.empty else len(df)
            print(f"[prep] {i+1}/{len(tapes)}  rows={nrows}", file=sys.stderr)
            if (i + 1) % CHECKPOINT_EVERY == 0:
                corpus = pd.concat([p for p in parts if p is not None and not p.empty],
                                   ignore_index=True)
                corpus.to_parquet(out / "metadata.parquet",
                                  compression="snappy", index=False)
                print(f"[prep] checkpoint: {len(corpus)} rows saved",
                      file=sys.stderr)

    corpus = pd.concat([p for p in parts if p is not None and not p.empty],
                       ignore_index=True)
    corpus.to_parquet(out / "metadata.parquet",
                      compression="snappy", index=False)
    print(f"[prep] done: {len(corpus)} rows total", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
