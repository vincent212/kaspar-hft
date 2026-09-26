# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Marked-arrival sweep: is it the clustering of LARGE packets that makes the tail?

The constant-service sweep (`qsim_run.py`) is blind to packet size, and the span sweep
(`qsim_span.py`) holds each window's mean work at T, which redistributes work instead of
adding it. This script charges each packet its absolute measured cost,

    S_i = T * (1 + r * (sigma_i - 1)),        sigma_i = messages in packet i,

so T is the single-message (floor) cost and a large packet ADDS work, with r the measured
slope/floor ratio of a stream (NQ book 0.312/7.23, ZN book 0.97/6.83). It then runs the
real marked stream against counterfactuals that remove one ingredient at a time.

A packet is LARGE if sigma >= L (default 5), or, in the transaction variant, if it carries
at least one trade message (typ == 'T', from make_spans --trades).

Arms (arrival times, spans):
    H    real times, real spans in real order                     the feed
    LX   real times, large spans cut to 1                         large packets removed
    LS   real times, large packets moved to random packet slots   large packets de-clustered
         (small spans keep their order; large ones keep theirs)
    LST  as LS for trade-bearing packets                          transactions de-clustered
    HS   real times, all spans permuted                           no size/time coupling at all
    GS   gaps shuffled, spans in real order                       no temporal runs, size sequence kept
    GP   (gap-before, span) pairs shuffled jointly                no ordering, gap/size pairing kept
    GI   gaps and spans shuffled independently                    marked renewal null
    B    1 s-binned Poisson times, spans permuted within each bin second-scale load only

Decomposition of the single-stage p99 excess (p99 - T), read at each T:
    H - LX   what large packets contribute at all
    H - LS   what their CLUSTERING (placement among packets) contributes      <- the question
    LS - LX  what their mere presence contributes, unclustered
    H - GS   what temporal runs contribute, size sequence held
    H - GP   what the ordering of the joint (gap, size) marks contributes

Direct attribution (H only): for every packet the busy period it waits in is tracked; the
share of the backlog work in front of it that belongs to large packets is recorded. For
tail packets (latency > window p99) that share is compared with large packets' share of
all work, and with the same statistic under LS. A clustering index for large packets is
also recorded: the fraction of large packets with another large packet within 100 us
before them, real vs LS.

Equal-core dispatch with its costs (HANDOFF 8.1): ingress stage (service s_in) -> hop h ->
N parallel whole-packet servers (least-loaded) -> hop h -> resequencer releasing in sequence
order. Reported with the resequencing wait separately, beside the tandem.

CLI:
    python3 -m arrival_paper.qsim_marked \\
        --cache  arrival_paper/figs/qsim_cache \\
        --spans  arrival_paper/figs/qsim_spans_tr \\
        --out-dir arrival_paper/figs/qsim_grid_marked [--jobs 59] [--sessions N]
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

from arrival_paper.qsim_span import tandem_var, expand_messages

TS = [4, 7, 8, 16, 32]
NS = [1, 2]
HOP = 1_700
R = {"nq": 0.312 / 7.23, "zn": 0.97 / 6.83}
L_BIG = 5
NEAR_NS = 100_000
DISP_NS = [2, 4]
S_IN_NS = [0, 1_000]


def _seed(session: str, wid: int, salt: str) -> int:
    return int.from_bytes(hashlib.md5(f"{session}|{int(wid)}|{salt}".encode()).digest()[:4], "big")


# ------------------------------------------------------------------ arrival/mark nulls
def gap_shuffle(a: np.ndarray, rng) -> tuple[np.ndarray, np.ndarray]:
    """Returns shuffled times and the permutation applied to gaps (gap k -> position)."""
    g = np.diff(a).astype(np.int64)
    perm = rng.permutation(len(g))
    out = np.empty_like(a); out[0] = a[0]; out[1:] = a[0] + np.cumsum(g[perm])
    return out, perm


def binned(a: np.ndarray, spans: np.ndarray, session: str, wid: int,
           bin_ns: int = 1_000_000_000) -> tuple[np.ndarray, np.ndarray]:
    """Same times as qsim_run.binned_poisson_null (same seed); spans permuted within bin."""
    rng = np.random.default_rng(_seed(session, wid, "bin1s"))
    rs = np.random.default_rng(_seed(session, wid, "bin1s_span"))
    t0, t1 = int(a[0]), int(a[-1]) + 1
    edges = np.arange(t0, t1 + bin_ns, bin_ns, dtype=np.int64); edges[-1] = max(edges[-1], t1)
    counts, _ = np.histogram(a, bins=edges)
    bin_of = np.searchsorted(edges, a, side="right") - 1
    parts, sp = [], []
    for i in range(len(counts)):
        c = int(counts[i])
        if c == 0:
            continue
        lo, hi = int(edges[i]), int(min(edges[i + 1], t1))
        parts.append(np.sort(rng.integers(low=lo, high=max(hi, lo + 1), size=c)))
        sp.append(rs.permutation(spans[bin_of == i]))
    return np.concatenate(parts).astype(np.int64), np.concatenate(sp)


def relocate(spans: np.ndarray, big: np.ndarray, rng) -> np.ndarray:
    """Move the big packets' spans to uniformly random packet slots; small spans fill the
    rest in their original order, big spans keep their original relative order."""
    n = len(spans); nb = int(big.sum())
    if nb == 0:
        return spans.copy()
    slots = np.sort(rng.choice(n, nb, replace=False))
    out = np.empty_like(spans)
    mask = np.zeros(n, dtype=bool); mask[slots] = True
    out[mask] = spans[big]; out[~mask] = spans[~big]
    return out


# ------------------------------------------------------------------ diagnostics
@njit(cache=True)
def busy_share(arr_ns: np.ndarray, svc: np.ndarray, big: np.ndarray) -> tuple:
    """Single server. For each packet: wait, and the share of backlog work in front of it
    (predecessors in the same busy period) that belongs to big packets (-1 if no backlog)."""
    n = arr_ns.shape[0]
    base = arr_ns[0]
    w = np.empty(n); share = np.full(n, -1.0)
    w[0] = 0.0
    tot = svc[0]; bw = svc[0] if big[0] else 0.0
    for i in range(1, n):
        v = w[i - 1] + svc[i - 1] - float(arr_ns[i] - arr_ns[i - 1])
        if v > 0.0:
            w[i] = v
            share[i] = bw / tot if tot > 0 else 0.0
            tot += svc[i]
            if big[i]:
                bw += svc[i]
        else:
            w[i] = 0.0
            tot = svc[i]; bw = svc[i] if big[i] else 0.0
    return w, share


@njit(cache=True)
def dispatch_full(arr_ns: np.ndarray, svc: np.ndarray, N: int, s_in: float,
                  h_d: float, h_r: float) -> tuple:
    """Ingress (service s_in) -> hop h_d -> N least-loaded whole-packet servers ->
    hop h_r -> in-order resequencer. Returns (latency, resequencing wait) in ns."""
    n = arr_ns.shape[0]
    base = arr_ns[0]
    lat = np.empty(n); rq = np.empty(n)
    free = np.zeros(N)
    e_prev = -1e30; r_prev = -1e30
    for i in range(n):
        a = float(arr_ns[i] - base)
        e = (a if a > e_prev else e_prev) + s_in
        e_prev = e
        d = e + h_d
        j = 0; fmin = free[0]
        for k in range(1, N):
            if free[k] < fmin:
                fmin = free[k]; j = k
        st = d if d > fmin else fmin
        c = st + svc[i]
        free[j] = c
        ready = c + h_r
        r = ready if ready > r_prev else r_prev
        rq[i] = r - ready
        r_prev = r
        lat[i] = r - a
    return lat, rq


def near_frac(t: np.ndarray, big: np.ndarray) -> float:
    tb = t[big]
    if len(tb) < 2:
        return float("nan")
    return float(np.mean(np.diff(tb) <= NEAR_NS))


def q(x: np.ndarray) -> tuple:
    return (float(np.quantile(x, .50)) / 1e3, float(np.quantile(x, .99)) / 1e3,
            float(np.quantile(x, .999)) / 1e3)


# ------------------------------------------------------------------ one session
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
            kH, kS, kT = f"w{wid}_H", f"w{wid}_S", f"w{wid}_TR"
            if kH not in za.files or kS not in zs.files:
                continue
            aH = za[kH]; S = zs[kS].astype(np.int64)
            TR = zs[kT].astype(np.int64) if kT in zs.files else np.zeros_like(S)
            if len(S) != len(aH):
                continue
            big = S >= L_BIG; trd = TR > 0
            rngLS = np.random.default_rng(_seed(session, wid, "LS"))
            rngLT = np.random.default_rng(_seed(session, wid, "LST"))
            rngHS = np.random.default_rng(_seed(session, wid, "HSm"))
            rngG = np.random.default_rng(_seed(session, wid, "gap"))       # same as qsim_run G
            rngGI = np.random.default_rng(_seed(session, wid, "GIspan"))
            rngGP = np.random.default_rng(_seed(session, wid, "GP"))
            aG, _ = gap_shuffle(aH, rngG)
            # GP: permute (gap-before, span) pairs jointly for packets 1..n-1
            gaps = np.diff(aH).astype(np.int64); pp = rngGP.permutation(len(gaps))
            aGP = np.empty_like(aH); aGP[0] = aH[0]; aGP[1:] = aH[0] + np.cumsum(gaps[pp])
            SGP = np.concatenate([S[:1], S[1:][pp]])
            aB, SB = binned(aH, S, session, wid)
            S_LS = relocate(S, big, rngLS); S_LT = relocate(S, trd, rngLT)
            arms = {"H": (aH, S), "LX": (aH, np.where(big, 1, S)), "LS": (aH, S_LS),
                    "LST": (aH, S_LT), "HS": (aH, rngHS.permutation(S)),
                    "GS": (aG, S), "GP": (aGP, SGP), "GI": (aG, rngGI.permutation(S)),
                    "B": (aB, SB)}
            row = {"session": session, "window_id": wid, "n_packets": len(S),
                   "n_messages": int(S.sum()), "big_frac": float(big.mean()),
                   "big_msg_frac": float(S[big].sum() / S.sum()),
                   "trade_pkt_frac": float(trd.mean()),
                   "big_near_H": near_frac(aH, big),
                   "big_near_LS": near_frac(aH, S_LS >= L_BIG),
                   "trd_near_H": near_frac(aH, trd)}
            for rk, r in R.items():
                for T in TS:
                    T_ns = float(T * 1000)
                    for arm, (arr, s) in arms.items():
                        svc = T_ns * (1.0 + r * (s - 1.0))
                        for N in NS:
                            lat = tandem_var(arr, svc, N, HOP)
                            p50, p99, p999 = q(lat)
                            tag = f"{rk}_T{T}_{arm}_N{N}"
                            row[f"{tag}_p50_us"] = p50; row[f"{tag}_p99_us"] = p99
                            row[f"{tag}_p999_us"] = p999
                            if N == 1 and arm != "LX":
                                mv = expand_messages(lat - svc, s, T_ns, T_ns * r)
                                m50, m99, m999 = q(mv)
                                row[f"{tag}_msg_p50_us"] = m50; row[f"{tag}_msg_p99_us"] = m99
                                row[f"{tag}_msg_p999_us"] = m999
                    # direct busy-period attribution on the real stream and on LS
                    if T in (8, 16):
                        for arm in ("H", "LS"):
                            arr, s = arms[arm]; b = s >= L_BIG
                            svc = T_ns * (1.0 + r * (s - 1.0))
                            w, sh = busy_share(arr, svc, b)
                            lat = w + svc; thr = np.quantile(lat, .99)
                            tail = (lat > thr) & (sh >= 0); queued = sh >= 0
                            tag = f"{rk}_T{T}_{arm}"
                            row[f"{tag}_bigwork_share_all"] = float(svc[b].sum() / svc.sum())
                            row[f"{tag}_bigwork_share_queued"] = float(sh[queued].mean()) if queued.any() else np.nan
                            row[f"{tag}_bigwork_share_tail"] = float(sh[tail].mean()) if tail.any() else np.nan
                            row[f"{tag}_tail_with_big_frac"] = float((sh[tail] > 0).mean()) if tail.any() else np.nan
                            row[f"{tag}_queued_with_big_frac"] = float((sh[queued] > 0).mean()) if queued.any() else np.nan
                    # dispatch with split + resequencing costs, real stream
                    if rk == "zn" or T in (8, 16, 32):
                        arr, s = arms["H"]
                        for model, svc in (("const", np.full(len(s), T_ns)),
                                           ("abs", T_ns * (1.0 + r * (s - 1.0)))):
                            for N in DISP_NS:
                                for s_in in S_IN_NS:
                                    lat, rq = dispatch_full(arr, svc, N, float(s_in), float(HOP), float(HOP))
                                    p50, p99, p999 = q(lat)
                                    tag = f"{rk}_T{T}_disp_{model}_N{N}_sin{s_in}"
                                    row[f"{tag}_p50_us"] = p50; row[f"{tag}_p99_us"] = p99
                                    row[f"{tag}_p999_us"] = p999
                                    row[f"{tag}_reseq_p99_us"] = float(np.quantile(rq, .99)) / 1e3
                                    row[f"{tag}_reseq_mean_us"] = float(rq.mean()) / 1e3
                                if model == "abs":
                                    for N in DISP_NS:
                                        lt = tandem_var(arr, svc, N, HOP)
                                        row[f"{rk}_T{T}_tandem_abs_N{N}_p50_us"], row[f"{rk}_T{T}_tandem_abs_N{N}_p99_us"], _ = q(lt)
            out.append(row)
    return out


def summarise(df: pd.DataFrame) -> None:
    """Corpus-median readout: share of H's single-stage p99 excess surviving each arm."""
    arms = ["LX", "LS", "LST", "HS", "GS", "GP", "GI", "B"]
    for rk in R:
        print(f"\n===== r = {rk} ({R[rk]:.4f}): corpus-median p99 (us) at N=1, and share of the H excess (p99 - T) that survives =====")
        print(f"{'T':>3} {'H':>9} | " + " ".join(f"{a:>13}" for a in arms))
        for T in TS:
            h = df[f"{rk}_T{T}_H_N1_p99_us"].median(); ex = h - T
            cells = []
            for a in arms:
                v = df[f"{rk}_T{T}_{a}_N1_p99_us"].median()
                cells.append(f"{v:>7.2f} ({(v - T) / ex * 100 if ex > 1e-9 else float('nan'):>3.0f}%)")
            print(f"{T:>3} {h:>9.2f} | " + " ".join(f"{c:>13}" for c in cells))
        print("  message-weighted p99 (N=1):")
        for T in TS:
            h = df[f"{rk}_T{T}_H_N1_msg_p99_us"].median(); ex = h - T
            cells = []
            for a in [x for x in arms if x != "LX"]:
                v = df[f"{rk}_T{T}_{a}_N1_msg_p99_us"].median()
                cells.append(f"{a} {v:.2f} ({(v - T) / ex * 100 if ex > 1e-9 else float('nan'):.0f}%)")
            print(f"   T={T:>3}  H {h:.2f} | " + "  ".join(cells))
        print("  busy-period attribution (corpus medians): big-packet share of backlog work")
        for T in (8, 16):
            for arm in ("H", "LS"):
                t = f"{rk}_T{T}_{arm}"
                print(f"   T={T:>2} {arm:>2}: all work {df[t+'_bigwork_share_all'].median():.3f}  queued {df[t+'_bigwork_share_queued'].median():.3f}  "
                      f"tail {df[t+'_bigwork_share_tail'].median():.3f} | tail with a big pkt in backlog {df[t+'_tail_with_big_frac'].median():.3f}  "
                      f"queued with one {df[t+'_queued_with_big_frac'].median():.3f}")
    print(f"\nlarge-packet clustering index (frac of large pkts with another within {NEAR_NS/1000:.0f} us before): "
          f"real {df.big_near_H.median():.3f}  vs de-clustered LS {df.big_near_LS.median():.3f};  trade pkts real {df.trd_near_H.median():.3f}")
    print(f"large packets (>= {L_BIG} msgs): {df.big_frac.median()*100:.2f}% of packets, {df.big_msg_frac.median()*100:.1f}% of messages; "
          f"trade-bearing packets {df.trade_pkt_frac.median()*100:.2f}%")
    print("\ndispatch with split + resequencing (hop 1.7 us each), vs tandem, abs service, corpus medians p50/p99 (us):")
    for rk in R:
        for T in TS:
            for N in DISP_NS:
                c = f"{rk}_T{T}_tandem_abs_N{N}_p99_us"
                if c not in df:
                    continue
                parts = [f"tandem {df[f'{rk}_T{T}_tandem_abs_N{N}_p50_us'].median():.2f}/{df[c].median():.2f}"]
                for s_in in S_IN_NS:
                    t = f"{rk}_T{T}_disp_abs_N{N}_sin{s_in}"
                    parts.append(f"disp s_in={s_in/1000:.0f}us {df[t+'_p50_us'].median():.2f}/{df[t+'_p99_us'].median():.2f} (reseq p99 {df[t+'_reseq_p99_us'].median():.2f})")
                print(f"  {rk} T={T:>3} N={N}: " + " | ".join(parts))


def main() -> int:
    ap_ = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap_.add_argument("--cache", required=True); ap_.add_argument("--spans", required=True)
    ap_.add_argument("--out-dir", required=True)
    ap_.add_argument("--jobs", type=int, default=int(os.cpu_count() or 8))
    ap_.add_argument("--sessions", type=int, default=0, help="limit to the first N sessions (testing)")
    ap_.add_argument("--summarise-only", action="store_true")
    a = ap_.parse_args()
    out = Path(a.out_dir); out.mkdir(parents=True, exist_ok=True); p = out / "qsim_grid_marked.parquet"
    if a.summarise_only:
        summarise(pd.read_parquet(p)); return 0
    meta = pd.read_parquet(Path(a.cache) / "metadata.parquet")
    sessions = list(meta["session"].unique())
    if a.sessions:
        sessions = sessions[:a.sessions]
    print(f"[marked] {len(sessions)} sessions", file=sys.stderr)
    _ = tandem_var(np.array([0, 100, 200], dtype=np.int64), np.full(3, 100.0), 2, 10)
    _ = busy_share(np.array([0, 100, 200], dtype=np.int64), np.full(3, 100.0), np.zeros(3, dtype=np.bool_))
    _ = dispatch_full(np.array([0, 100, 200], dtype=np.int64), np.full(3, 100.0), 2, 0.0, 10.0, 10.0)
    tasks = [(str(Path(a.cache)), str(Path(a.spans)), s, meta[meta["session"] == s].to_dict("records")) for s in sessions]
    rows, t0 = [], time.time()
    with mp.Pool(max(1, min(a.jobs, len(tasks)))) as pool:
        for i, rs in enumerate(pool.imap_unordered(run_session, tasks)):
            rows.extend(rs)
            if (i + 1) % 10 == 0 or (i + 1) == len(tasks):
                print(f"[marked] {i+1}/{len(tasks)} sessions, {len(rows)} rows, {time.time()-t0:.0f}s", file=sys.stderr)
    df = pd.DataFrame(rows).sort_values(["session", "window_id"])
    df.to_parquet(p, compression="snappy", index=False)
    print(f"[marked] wrote {p}: {len(df)} rows, {len(df.columns)} cols, {time.time()-t0:.0f}s", file=sys.stderr)
    summarise(df)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
