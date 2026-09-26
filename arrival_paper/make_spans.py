# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Extract per-packet span (messages per UDP datagram) aligned to an existing cache.

`qsim_prep.py` computes each packet's span while grouping messages by
`packet_seq`, then discards it -- only the arrival timestamps reach the cache.
This script re-parses the tapes and emits the span arrays, in the SAME order as
the cached arrival arrays, so the two can be zipped element-wise.

The packet grouping, window chopping and admission filters are replicated from
`qsim_prep.py` verbatim; any divergence would silently misalign spans against
arrivals. As a guard, every window's span-array length is checked against the
`n_packets` recorded in the cache metadata, and a mismatch is reported rather
than written.

Output: <out-dir>/{session}.npz keyed w{window_id}_S (int64 message counts) and,
with --trades, w{window_id}_TR (int64 count of trade messages, typ == "T", per packet),
and w{window_id}_X0 / _X1 (int64 min / max transactTime in the packet). A CME
transaction is one matching-engine event: all messages sharing one transactTime.

CLI:
    python3 -m arrival_paper.make_spans \\
        --tapes-dir /vast/home/vmayeski/out/arrival_paper/tapes/318/message \\
        --cache     arrival_paper/figs/qsim_cache \\
        --out-dir   arrival_paper/figs/qsim_spans \\
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

from arrival_paper.qsim_prep import (DEFAULT_WINDOW_MIN, MIN_PACKETS_PER_WINDOW,
                                     MIN_MESSAGES_PER_WINDOW)


def spans_for_session(msg_tape_csv: str, out_dir: str,
                      window_min: int = DEFAULT_WINDOW_MIN,
                      trades: bool = False) -> tuple[str, dict]:
    """Return (session, {window_id: n_packets}) and write the span npz."""
    tt: list[int] = []
    seq: list[int] = []
    trd: list[int] = []
    txt: list[int] = []
    with open(msg_tape_csv, "rt", newline="") as f:
        r = csv.reader(f)
        h = next(r)
        i_tt  = h.index("sendingTime")
        i_seq = h.index("packet_seq")
        i_typ = h.index("typ") if trades else -1
        i_tx  = h.index("transactTime") if trades else -1
        for row in r:
            try:
                v_tt  = int(row[i_tt])
                v_seq = int(row[i_seq])
            except (IndexError, ValueError):
                continue
            if v_tt <= 0:
                continue
            tt.append(v_tt)
            seq.append(v_seq)
            if trades:
                trd.append(1 if row[i_typ] == "T" else 0)
                try:
                    txt.append(int(row[i_tx]))
                except (IndexError, ValueError):
                    txt.append(v_tt)

    tt_arr  = np.asarray(tt,  dtype=np.int64)
    seq_arr = np.asarray(seq, dtype=np.int64)
    order   = np.argsort(tt_arr, kind="stable")
    tt_arr  = tt_arr[order]
    seq_arr = seq_arr[order]
    trd_arr = np.asarray(trd, dtype=np.int64)[order] if trades else None
    txt_arr = np.asarray(txt, dtype=np.int64)[order] if trades else None
    session = Path(msg_tape_csv).stem
    if len(tt_arr) == 0:
        return session, {}

    win_ns  = window_min * 60 * 1_000_000_000
    win_idx = (tt_arr - tt_arr[0]) // win_ns

    npz: dict[str, np.ndarray] = {}
    counts: dict[int, int] = {}
    for wid in np.unique(win_idx):
        wm = win_idx == wid
        tt_w, seq_w = tt_arr[wm], seq_arr[wm]
        trd_w = trd_arr[wm] if trades else np.zeros(len(tt_w), dtype=np.int64)
        txt_w = txt_arr[wm] if trades else tt_w
        if int(len(tt_w)) < MIN_MESSAGES_PER_WINDOW:
            continue
        seq_to_min = defaultdict(lambda: np.iinfo(np.int64).max)
        seq_to_cnt = defaultdict(int)
        seq_to_trd = defaultdict(int)
        seq_to_x0 = defaultdict(lambda: np.iinfo(np.int64).max)
        seq_to_x1 = defaultdict(lambda: np.iinfo(np.int64).min)
        for t, s, k, x in zip(tt_w, seq_w, trd_w, txt_w):
            if t < seq_to_min[s]:
                seq_to_min[s] = t
            seq_to_cnt[s] += 1
            seq_to_trd[s] += int(k)
            if x < seq_to_x0[s]:
                seq_to_x0[s] = x
            if x > seq_to_x1[s]:
                seq_to_x1[s] = x
        if len(seq_to_min) < MIN_PACKETS_PER_WINDOW:
            continue
        # Same sort key as qsim_prep.py, so element i here is element i there.
        pairs = sorted((seq_to_min[s], seq_to_cnt[s], seq_to_trd[s], seq_to_x0[s], seq_to_x1[s])
                       for s in seq_to_min)
        npz[f"w{int(wid)}_S"] = np.array([p[1] for p in pairs], dtype=np.int64)
        if trades:
            npz[f"w{int(wid)}_TR"] = np.array([p[2] for p in pairs], dtype=np.int64)
            npz[f"w{int(wid)}_X0"] = np.array([p[3] for p in pairs], dtype=np.int64)
            npz[f"w{int(wid)}_X1"] = np.array([p[4] for p in pairs], dtype=np.int64)
        counts[int(wid)] = len(pairs)

    if npz:
        np.savez_compressed(Path(out_dir) / f"{session}.npz", **npz)
    return session, counts


def _worker(a: tuple[str, str, int, bool]) -> tuple[str, dict]:
    return spans_for_session(*a)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tapes-dir", required=True)
    ap.add_argument("--cache", required=True,
                    help="existing qsim_prep cache, for the alignment check")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--jobs", type=int, default=max(1, mp.cpu_count() // 2))
    ap.add_argument("--window-min", type=int, default=DEFAULT_WINDOW_MIN)
    ap.add_argument("--trades", action="store_true",
                    help="also write w{wid}_TR, trade messages (typ == 'T') per packet")
    args = ap.parse_args()

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    meta = pd.read_parquet(Path(args.cache) / "metadata.parquet")
    want = {(r.session, int(r.window_id)): int(r.n_packets)
            for r in meta.itertuples()}

    tapes = sorted(glob.glob(os.path.join(args.tapes_dir, "*.csv")))
    print(f"[spans] {len(tapes)} tapes; jobs={args.jobs}", file=sys.stderr)

    ok = bad = missing = 0
    with mp.Pool(args.jobs) as pool:
        for i, (session, counts) in enumerate(pool.imap_unordered(
                _worker, [(t, str(out), args.window_min, args.trades) for t in tapes],
                chunksize=1)):
            for wid, n in counts.items():
                key = (session, wid)
                if key not in want:
                    continue
                if want[key] == n:
                    ok += 1
                else:
                    bad += 1
                    print(f"[spans] MISALIGNED {session} w{wid}: "
                          f"spans={n} cache n_packets={want[key]}",
                          file=sys.stderr)
            if (i + 1) % 25 == 0 or (i + 1) == len(tapes):
                print(f"[spans] {i+1}/{len(tapes)} tapes, {ok} windows aligned",
                      file=sys.stderr)
    missing = len(want) - ok - bad
    print(f"[spans] done: {ok} aligned, {bad} misaligned, {missing} "
          f"cache windows without a span array", file=sys.stderr)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
