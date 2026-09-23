# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Span-aware tandem sweep: service time that depends on the packet's message count.

`qsim_run.py` charges every packet the same service time $T$ regardless of how
many SBE messages the datagram carried. The live production measurement
(arXiv:2609.21173) shows per-packet decode cost is affine in the message count:
a packet of span $s$ occupies the decoder for floor + (s-1)*slope, with a
measured NQ-book ratio slope/floor = 0.312/7.23 = 0.0432. This script re-runs the
sweep with that dependence and reports it beside the constant-service result.

Service models (both hold the per-window MEAN service at T, so the utilisation
rho is identical and only the DISTRIBUTION of work across packets differs -- any
tail difference is the span effect, not a slower system):

    const : S_i = T
    span  : S_i = A * (1 + r*(s_i - 1)),  A = T / (1 + r*(sbar - 1))

Arms:

    H   real arrival times, real spans in their real order
    HS  real arrival times, spans randomly permuted across packets
        -- preserves both marginals, destroys only the span/timing coupling.
        H vs HS isolates whether large packets arrive preferentially inside
        clusters, which is the second tail channel the constant-service model
        cannot represent.
    P   Poisson-null arrival times, real spans

CLI:
    python3 -m arrival_paper.qsim_span \\
        --cache arrival_paper/figs/qsim_cache \\
        --spans arrival_paper/figs/qsim_spans \\
        --out-dir arrival_paper/figs/qsim_grid_span
"""
from __future__ import annotations

import argparse
import hashlib
import multiprocessing as mp
import os
import sys
import time
from pathlib import Path

import numpy as np
import pandas as pd
from numba import njit

TS   = [2, 4, 8, 16, 32, 64, 128]
NS   = [1, 2, 4, 8]
HOP  = 1_700
# slope/floor measured on NQ book in the live study; see module docstring.
R_SPAN = 0.312 / 7.23


@njit(cache=True)
def lindley_var(arr_ns: np.ndarray, svc_ns: np.ndarray) -> np.ndarray:
    """Single-server FIFO Lindley with per-arrival service. Returns wait (ns).
    `arr_ns` must already be rebased (see tandem_var)."""
    n = arr_ns.shape[0]
    w = np.empty(n, dtype=np.float64)
    w[0] = 0.0
    for i in range(1, n):
        v = w[i - 1] + svc_ns[i - 1] - (arr_ns[i] - arr_ns[i - 1])
        w[i] = v if v > 0.0 else 0.0
    return w


@njit(cache=True)
def tandem_var(arr_ns: np.ndarray, svc_ns: np.ndarray, N: int,
               hop_ns: int) -> np.ndarray:
    """N-stage tandem, per-stage service svc/N. Returns end-to-end latency (ns).

    Arrivals are rebased to the window start before converting to float64.
    CME timestamps are epoch nanoseconds, order 1.7e18, where float64 spacing is
    256 ns -- large enough to corrupt 99.6% of the microsecond-scale gaps this
    model is built on. Rebasing puts them below 2e12, where float64 is exact.
    """
    base = arr_ns[0]
    rel = (arr_ns - base).astype(np.float64)
    per = svc_ns / N
    dep = rel.copy()
    for stage in range(N):
        if stage > 0:
            dep = dep + hop_ns
        w = lindley_var(dep, per)
        dep = dep + w + per
    return dep - rel


@njit(cache=True)
def expand_messages(wait_ns: np.ndarray, spans: np.ndarray,
                    A: float, B: float) -> np.ndarray:
    """Per-message latency at N=1: message j of packet i finishes A + B*j after
    its packet starts service, so its latency is wait_i + A + B*j."""
    total = 0
    for i in range(spans.shape[0]):
        total += spans[i]
    out = np.empty(total, dtype=np.float64)
    k = 0
    for i in range(spans.shape[0]):
        for j in range(spans[i]):
            out[k] = wait_ns[i] + A + B * j
            k += 1
    return out


@njit(cache=True)
def ladder_medians(wait_ns: np.ndarray, spans: np.ndarray, A: float, B: float,
                   max_idx: int) -> np.ndarray:
    """Median per-message latency at each in-packet position 0..max_idx-1.
    NaN where no packet in the window reaches that position."""
    out = np.full(max_idx, np.nan)
    for j in range(max_idx):
        cnt = 0
        for i in range(spans.shape[0]):
            if spans[i] > j:
                cnt += 1
        if cnt < 30:
            continue
        buf = np.empty(cnt, dtype=np.float64)
        k = 0
        for i in range(spans.shape[0]):
            if spans[i] > j:
                buf[k] = wait_ns[i] + A + B * j
                k += 1
        out[j] = np.median(buf)
    return out


def q_us(x: np.ndarray) -> tuple:
    return (float(np.quantile(x, .50)) / 1e3, float(np.quantile(x, .99)) / 1e3,
            float(np.quantile(x, .999)) / 1e3)


MAX_IDX = 40


def run_session(task):
    cache_dir, spans_dir, session, rows = task
    ap = Path(cache_dir) / "arrivals" / f"{session}.npz"
    sp = Path(spans_dir) / f"{session}.npz"
    if not ap.exists() or not sp.exists():
        return []
    out = []
    with np.load(ap) as za, np.load(sp) as zs:
        for wr in rows:
            wid = int(wr["window_id"])
            kH, kP, kS = f"w{wid}_H", f"w{wid}_P", f"w{wid}_S"
            if kH not in za.files or kP not in za.files or kS not in zs.files:
                continue
            aH, aP, spans = za[kH], za[kP], zs[kS]
            if len(spans) != len(aH):
                continue
            seed = int.from_bytes(
                hashlib.md5(f"{session}|{wid}|span".encode()).digest()[:4], "big")
            sh = np.random.default_rng(seed).permutation(spans)
            sbar = float(spans.mean())

            row = {"session": session, "window_id": wid,
                   "n_packets": len(spans), "span_mean": sbar,
                   "span_max": int(spans.max()),
                   "lambda_bar_obs": float(wr.get("lambda_bar_obs", np.nan)),
                   "n_branch_pkt": float(wr.get("n_branch_pkt", np.nan)),
                   "converged": bool(wr.get("converged", False))}

            for T in TS:
                T_ns = float(T * 1000)
                A = T_ns / (1.0 + R_SPAN * (sbar - 1.0))
                B = A * R_SPAN
                for arm, arr, sv in (("H", aH, spans), ("HS", aH, sh),
                                     ("P", aP, spans)):
                    for model in ("const", "span", "prop"):
                        if model == "const":
                            svc = np.full(len(arr), T_ns)
                        elif model == "span":
                            svc = A * (1.0 + R_SPAN * (sv - 1.0))
                        else:
                            # Upper bound: service fully proportional to span,
                            # i.e. no per-packet fixed cost at all. Physically
                            # too strong (the live floor dominates the slope),
                            # but it brackets the span effect from above.
                            svc = T_ns * sv / sbar
                        for N in NS:
                            lat = tandem_var(arr, svc, N, HOP)
                            p50, p99, p999 = q_us(lat)
                            tag = f"T{T}_{arm}_{model}_N{N}"
                            row[f"{tag}_p50_us"] = p50
                            row[f"{tag}_p99_us"] = p99
                            row[f"{tag}_p999_us"] = p999
                            # Message-weighted latency at N=1: messages, not
                            # packets, are the unit an operator trades on, and
                            # large packets hold disproportionately many.
                            if N == 1 and arm == "H":
                                w = lat - svc
                                if model == "span":
                                    mA, mB = A, B
                                elif model == "prop":
                                    mA, mB = T_ns / sbar, T_ns / sbar
                                else:
                                    mA, mB = T_ns, 0.0
                                mv = expand_messages(w, sv.astype(np.int64),
                                                     mA, mB)
                                m50, m99, m999 = q_us(mv)
                                row[f"{tag}_msg_p50_us"] = m50
                                row[f"{tag}_msg_p99_us"] = m99
                                row[f"{tag}_msg_p999_us"] = m999
                                if T == 8 and arm == "H" and model == "span":
                                    lad = ladder_medians(
                                        w, sv.astype(np.int64), A, B, MAX_IDX)
                                    for j in range(MAX_IDX):
                                        row[f"ladder_idx{j}_us"] = lad[j] / 1e3
            out.append(row)
    return out


def main() -> int:
    ap_ = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap_.add_argument("--cache", required=True)
    ap_.add_argument("--spans", required=True)
    ap_.add_argument("--out-dir", required=True)
    ap_.add_argument("--jobs", type=int, default=int(os.cpu_count() or 8))
    a = ap_.parse_args()

    out = Path(a.out_dir); out.mkdir(parents=True, exist_ok=True)
    meta = pd.read_parquet(Path(a.cache) / "metadata.parquet")
    print(f"[span] {len(meta)} windows, {meta['session'].nunique()} sessions",
          file=sys.stderr)
    _ = tandem_var(np.array([0, 100, 200], dtype=np.int64),
                   np.full(3, 100.0), 2, 10)

    sessions = list(meta["session"].unique())
    tasks = [(str(Path(a.cache)), str(Path(a.spans)), s,
              meta[meta["session"] == s].to_dict("records")) for s in sessions]
    rows, t0 = [], time.time()
    with mp.Pool(max(1, min(a.jobs, len(tasks)))) as pool:
        for i, rs in enumerate(pool.imap_unordered(run_session, tasks)):
            rows.extend(rs)
            if (i + 1) % 25 == 0 or (i + 1) == len(sessions):
                print(f"[span] {i+1}/{len(sessions)} sessions, {len(rows)} rows,"
                      f" {time.time()-t0:.0f}s", file=sys.stderr)
    df = pd.DataFrame(rows).sort_values(["session", "window_id"])
    p = out / "qsim_grid_span.parquet"
    df.to_parquet(p, compression="snappy", index=False)
    print(f"[span] wrote {p}: {len(df)} rows, {len(df.columns)} cols, "
          f"{time.time()-t0:.0f}s", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
