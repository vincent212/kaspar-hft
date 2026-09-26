# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Transactions: are they clustered, what shape are they, and which part makes the tail.

A CME transaction is one matching-engine event: every message sharing one transactTime.
CME may split it over several UDP packets. Packets are grouped into transaction BLOCKS:
consecutive packets whose transactTime ranges overlap (make_spans --trades writes each
packet's min/max transactTime as w{wid}_X0 / _X1). A block is one transaction, or a few
transactions that share a packet.

E1  descriptive, pooled over the corpus: packets per transaction, messages per packet,
    messages per transaction, block duration; packet-to-packet gaps INSIDE a transaction vs
    BETWEEN transactions, and the share of tight gaps (< 7.5, 16, 32 us) that are inside one.
E2  clustering of transactions: Fano factor of block-start counts at 1 ms .. 1 s against
    the same with the idle gaps between blocks shuffled; exponential-Hawkes fit on block
    starts (branching ratio) beside the packet fit from qsim_prep.
E3  simulation, service charged per packet at the measured absolute cost
    S_i = T (1 + r (sigma_i - 1)), r = slope/floor (NQ book 0.0432, ZN book 0.142):
      H   real stream
      TG  blocks kept intact, idle gaps between blocks shuffled   no clustering of transactions
      TP  blocks kept intact, start times uniform over the window no transaction timing at all
      TS  real block start times, block shapes shuffled           size decoupled from timing
      TM  each block merged into one packet at its start          no split into packets
      TW  real starts, a block's packets spaced by the median     no tight intra-transaction spacing
          idle gap
    Reported: share of the real stream's single-stage p99 excess (p99 - T) surviving each arm,
    packet- and message-weighted, and the N = 2 tandem p99.

    python3 -m arrival_paper.qsim_tx --cache arrival_paper/figs/qsim_cache \\
        --spans arrival_paper/figs/qsim_spans_tr --out-dir arrival_paper/figs/qsim_grid_tx
"""
from __future__ import annotations

import argparse, hashlib, multiprocessing as mp, os, sys, time
from pathlib import Path

import numpy as np
import pandas as pd
from numba import njit

from arrival_paper.qsim_span import tandem_var, expand_messages
from arrival_paper.hawkes_smoke import fit_hawkes

TS = [4, 7, 8, 16, 32, 64, 128]
NS = [1, 2]
HOP = 1_700
R = {"nq": 0.312 / 7.23, "zn": 0.97 / 6.83}
PPT_BINS = [1, 2, 3, 4, 5, 6, 11, 21, 51, 10**9]          # packets per transaction bin edges
MPP_BINS = [1, 2, 3, 4, 5, 6, 11, 21, 51, 10**9]          # messages per packet
MPT_BINS = [1, 2, 3, 4, 5, 6, 11, 21, 51, 101, 10**9]     # messages per transaction
GAP_EDGES_US = [0, 1, 2, 5, 7.5, 10, 16, 32, 64, 128, 256, 1000, 1e12]
FANO_SCALES_NS = [1_000_000, 10_000_000, 100_000_000, 1_000_000_000]
# fine histograms for the paper figures: integer counts 1..200 (then overflow), log gap bins
CNT_EDGES = np.r_[np.arange(1, 201), 10**9]
GAP_LOG_EDGES_NS = np.r_[0, np.logspace(2, 10, 161)]         # 100 ns .. 10 s, 20 bins/decade


def _seed(session, wid, salt):
    return int.from_bytes(hashlib.md5(f"{session}|{int(wid)}|{salt}".encode()).digest()[:4], "big")


@njit(cache=True)
def block_ids(x0, x1):
    n = x0.shape[0]; b = np.empty(n, dtype=np.int64); b[0] = 0; cur = x1[0]; k = 0
    for i in range(1, n):
        if x0[i] <= cur:
            if x1[i] > cur:
                cur = x1[i]
        else:
            k += 1; cur = x1[i]
        b[i] = k
    return b


def fano(t, scale):
    if len(t) < 10:
        return np.nan
    c = np.bincount(((t - t[0]) // scale).astype(np.int64))
    return float(c.var() / c.mean()) if c.mean() > 0 else np.nan


def build_arms(t, S, b, rng_tg, rng_tp, rng_ts):
    nb = int(b[-1]) + 1
    first = np.r_[0, np.flatnonzero(np.diff(b)) + 1]; last = np.r_[first[1:] - 1, len(b) - 1]
    L = last - first + 1
    s0 = t[first]; e0 = t[last]; dur = e0 - s0; off = t - s0[b]
    idle = s0[1:] - e0[:-1]
    arms = {"H": (t, S)}
    # TG: shuffle idle gaps, keep blocks and their order
    g = idle[rng_tg.permutation(len(idle))]
    sN = np.empty(nb, dtype=np.int64); sN[0] = s0[0]
    sN[1:] = s0[0] + np.cumsum(dur[:-1] + g)
    arms["TG"] = (sN[b] + off, S)
    # TP: uniform starts, blocks in original order onto sorted starts
    sP = np.sort(rng_tp.integers(int(t[0]), int(t[-1]) + 1, size=nb))
    tt = sP[b] + off; o = np.argsort(tt, kind="stable"); arms["TP"] = (tt[o], S[o])
    # TS: real starts, shapes permuted across blocks
    perm = rng_ts.permutation(nb)
    Lp = L[perm]; idx = np.concatenate([np.arange(first[k], last[k] + 1) for k in perm]) if nb < 200_000 else \
        np.repeat(first[perm], Lp) + (np.arange(Lp.sum()) - np.repeat(np.cumsum(Lp) - Lp, Lp))
    tt = np.repeat(s0, Lp) + off[idx]; ss = S[idx]; o = np.argsort(tt, kind="stable"); arms["TS"] = (tt[o], ss[o])
    # TM: one packet per block
    arms["TM"] = (s0.copy(), np.bincount(b, weights=S).astype(np.int64))
    # TW: intra-block spacing replaced by the median idle gap
    G = int(np.median(idle)) if len(idle) else 1000
    pos = np.arange(len(b)) - first[b]
    tt = s0[b] + pos * G; o = np.argsort(tt, kind="stable"); arms["TW"] = (tt[o], S[o])
    return arms, dict(nb=nb, L=L, first=first, last=last, s0=s0, dur=dur, idle=idle, G=G)


def run_session(task):
    cache_dir, spans_dir, session, rows = task
    ap = Path(cache_dir) / "arrivals" / f"{session}.npz"; sp = Path(spans_dir) / f"{session}.npz"
    if not ap.exists() or not sp.exists():
        return []
    out = []
    with np.load(ap) as za, np.load(sp) as zs:
        for wr in rows:
            wid = int(wr["window_id"])
            if f"w{wid}_H" not in za.files or f"w{wid}_X0" not in zs.files:
                continue
            t = za[f"w{wid}_H"]; S = zs[f"w{wid}_S"].astype(np.int64)
            x0 = zs[f"w{wid}_X0"]; x1 = zs[f"w{wid}_X1"]
            if len(S) != len(t):
                continue
            b = block_ids(x0, x1)
            arms, info = build_arms(t, S, b, *(np.random.default_rng(_seed(session, wid, s)) for s in ("TG", "TP", "TS")))
            L = info["L"]; mpt = np.bincount(b, weights=S).astype(np.int64)
            gaps = np.diff(t); intra = b[1:] == b[:-1]
            row = {"session": session, "window_id": wid, "n_packets": len(S), "n_messages": int(S.sum()),
                   "n_blocks": info["nb"], "multi_tx_packets": float(np.mean(x0 != x1)),
                   "n_branch_pkt": float(wr.get("n_branch_pkt", np.nan))}
            # E1 histograms (pooled at summary time)
            for name, v, bins in (("ppt", L, PPT_BINS), ("mpp", S, MPP_BINS), ("mpt", mpt, MPT_BINS)):
                h = np.histogram(v, bins=bins)[0]
                for i, c in enumerate(h):
                    row[f"h_{name}_{bins[i]}"] = int(c)
            for lab, gsel in (("intra", gaps[intra]), ("inter", gaps[~intra])):
                h = np.histogram(gsel / 1e3, bins=GAP_EDGES_US)[0]
                for i, c in enumerate(h):
                    row[f"h_gap_{lab}_{GAP_EDGES_US[i]}"] = int(c)
            # fine histograms (pooled at figure time)
            fine = {"mpp": np.histogram(S, CNT_EDGES)[0], "ppt": np.histogram(L, CNT_EDGES)[0],
                    "mpt": np.histogram(mpt, CNT_EDGES)[0],
                    "gpi": np.histogram(gaps[intra], GAP_LOG_EDGES_NS)[0],
                    "gpe": np.histogram(gaps[~intra], GAP_LOG_EDGES_NS)[0],
                    "gpall": np.histogram(gaps, GAP_LOG_EDGES_NS)[0],
                    "gpP": np.histogram(np.diff(np.sort(np.random.default_rng(_seed(session, wid, "gP")).integers(int(t[0]), int(t[-1]) + 1, size=len(t)))), GAP_LOG_EDGES_NS)[0],
                    "gtx": np.histogram(np.diff(info["s0"]), GAP_LOG_EDGES_NS)[0],
                    "gtxP": np.histogram(np.diff(np.sort(np.random.default_rng(_seed(session, wid, "gtxP")).integers(int(t[0]), int(t[-1]) + 1, size=info["nb"]))), GAP_LOG_EDGES_NS)[0],
                    "gtxTG": np.histogram(np.diff(np.sort(arms["TG"][0][info["first"]])), GAP_LOG_EDGES_NS)[0],
                    "gdur": np.histogram(info["dur"][L > 1], GAP_LOG_EDGES_NS)[0]}
            row["_fine"] = {k: v.astype(np.int64) for k, v in fine.items()}
            row["dur_multi_p50_us"] = float(np.median(info["dur"][L > 1])) / 1e3 if (L > 1).any() else np.nan
            row["dur_multi_p99_us"] = float(np.quantile(info["dur"][L > 1], .99)) / 1e3 if (L > 1).any() else np.nan
            # E2 clustering of transactions
            s0 = info["s0"]; sTG = np.sort(arms["TG"][0][info["first"]]) if True else None
            for sc in FANO_SCALES_NS:
                row[f"fano_tx_{sc//1_000_000}ms"] = fano(s0, sc)
                row[f"fano_txTG_{sc//1_000_000}ms"] = fano(sTG, sc)
                row[f"fano_pkt_{sc//1_000_000}ms"] = fano(t, sc)
            try:
                f = fit_hawkes(s0); row["n_branch_tx"] = f["n_branch"]; row["tx_fit_conv"] = f["converged"]
            except Exception:
                row["n_branch_tx"] = np.nan; row["tx_fit_conv"] = False
            # E3 simulation
            for rk, r in R.items():
                for T in TS:
                    T_ns = float(T * 1000)
                    for arm, (arr, s) in arms.items():
                        svc = T_ns * (1.0 + r * (s - 1.0))
                        for N in NS:
                            lat = tandem_var(arr, svc, N, HOP)
                            tag = f"{rk}_T{T}_{arm}_N{N}"
                            row[f"{tag}_p50_us"] = float(np.quantile(lat, .5)) / 1e3
                            row[f"{tag}_p99_us"] = float(np.quantile(lat, .99)) / 1e3
                            row[f"{tag}_p999_us"] = float(np.quantile(lat, .999)) / 1e3
                            if N == 1:
                                mv = expand_messages(lat - svc, s, T_ns, T_ns * r)
                                row[f"{tag}_msg_p99_us"] = float(np.quantile(mv, .99)) / 1e3
                                row[f"{tag}_msg_p999_us"] = float(np.quantile(mv, .999)) / 1e3
            out.append(row)
    return out


def _pooled(df, name, bins):
    cols = [f"h_{name}_{bins[i]}" for i in range(len(bins) - 1)]
    tot = df[cols].sum(); tot = tot / tot.sum() * 100
    labs = [f"{bins[i]}" if bins[i + 1] - bins[i] == 1 else (f"{bins[i]}-{bins[i+1]-1}" if bins[i + 1] < 10**9 else f"{bins[i]}+")
            for i in range(len(bins) - 1)]
    return "  ".join(f"{l}: {v:.3f}%" for l, v in zip(labs, tot.values))


def summarise(df):
    print(f"windows {len(df)}, packets {df.n_packets.sum():,}, messages {df.n_messages.sum():,}, "
          f"transaction blocks {df.n_blocks.sum():,}; packets carrying >1 transaction: {df.multi_tx_packets.median()*100:.2f}% (median window)")
    print("\n== E1 ==")
    print("packets per transaction:  " + _pooled(df, "ppt", PPT_BINS))
    print("messages per packet:      " + _pooled(df, "mpp", MPP_BINS))
    print("messages per transaction: " + _pooled(df, "mpt", MPT_BINS))
    print(f"multi-packet transaction duration (first->last packet): median-window p50 {df.dur_multi_p50_us.median():.2f} us, p99 {df.dur_multi_p99_us.median():.1f} us")
    edges = GAP_EDGES_US
    for lab in ("intra", "inter"):
        c = df[[f"h_gap_{lab}_{edges[i]}" for i in range(len(edges) - 1)]].sum()
        cum = np.cumsum(c.values) / c.sum() * 100
        print(f"gaps {lab}-transaction (n={int(c.sum()):,}), cumulative %: " +
              "  ".join(f"<{edges[i+1]:g}us {cum[i]:.1f}" for i in range(len(edges) - 2)))
    ci = df[[f"h_gap_intra_{edges[i]}" for i in range(len(edges) - 1)]].sum().values
    ce = df[[f"h_gap_inter_{edges[i]}" for i in range(len(edges) - 1)]].sum().values
    for thr in (7.5, 16, 32):
        k = edges.index(thr); a, e = ci[:k].sum(), ce[:k].sum()
        print(f"  gaps < {thr:>4} us: {(a+e)/(ci.sum()+ce.sum())*100:.2f}% of all gaps; {a/(a+e)*100:.1f}% of them inside one transaction")
    print("\n== E2: clustering of transactions (Fano of counts, corpus median) ==")
    for sc in FANO_SCALES_NS:
        k = f"{sc//1_000_000}ms"
        print(f"  {k:>6}: transactions {df['fano_tx_'+k].median():8.2f}   transactions, idle gaps shuffled {df['fano_txTG_'+k].median():8.2f}   packets {df['fano_pkt_'+k].median():8.2f}")
    print(f"  Hawkes branching ratio: transactions {df.n_branch_tx.median():.3f} (converged {df.tx_fit_conv.mean()*100:.0f}%)   packets {df.n_branch_pkt.median():.3f}")
    print("\n== E3: corpus-median p99 (us) at N=1 and share of the real stream's excess (p99 - T) surviving ==")
    arms = ["TG", "TP", "TS", "TM", "TW"]
    for rk in R:
        for wt, suf in (("packet", "_p99_us"), ("message", "_msg_p99_us")):
            print(f" r={rk} ({R[rk]:.4f}), {wt}-weighted")
            print(f"  {'T':>3} {'H':>8} | " + " ".join(f"{a:>15}" for a in arms))
            for T in TS:
                h = df[f"{rk}_T{T}_H_N1{suf}"].median(); ex = h - T
                cells = [f"{df[f'{rk}_T{T}_{a}_N1{suf}'].median():>7.2f} ({(df[f'{rk}_T{T}_{a}_N1{suf}'].median()-T)/ex*100 if ex>1e-9 else float('nan'):>4.0f}%)" for a in arms]
                print(f"  {T:>3} {h:>8.2f} | " + " ".join(f"{c:>15}" for c in cells))
        print(f" r={rk}: N=2 tandem p99 (packet): " + "  ".join(
            f"T={T}: H {df[f'{rk}_T{T}_H_N2_p99_us'].median():.2f} TG {df[f'{rk}_T{T}_TG_N2_p99_us'].median():.2f} TM {df[f'{rk}_T{T}_TM_N2_p99_us'].median():.2f}" for T in TS))


def main():
    ap_ = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap_.add_argument("--cache", required=True); ap_.add_argument("--spans", required=True); ap_.add_argument("--out-dir", required=True)
    ap_.add_argument("--jobs", type=int, default=int(os.cpu_count() or 8)); ap_.add_argument("--sessions", type=int, default=0)
    ap_.add_argument("--summarise-only", action="store_true")
    a = ap_.parse_args()
    out = Path(a.out_dir); out.mkdir(parents=True, exist_ok=True); p = out / "qsim_grid_tx.parquet"
    if a.summarise_only:
        summarise(pd.read_parquet(p)); return 0
    meta = pd.read_parquet(Path(a.cache) / "metadata.parquet")
    have = {Path(f).stem for f in os.listdir(a.spans) if f.endswith(".npz")}
    sessions = [s for s in meta["session"].unique() if s in have]
    if a.sessions:
        sessions = sessions[:a.sessions]
    print(f"[tx] {len(sessions)} sessions", file=sys.stderr)
    _ = tandem_var(np.array([0, 100, 200], dtype=np.int64), np.full(3, 100.0), 2, 10)
    _ = block_ids(np.array([0, 1, 2], dtype=np.int64), np.array([0, 1, 2], dtype=np.int64))
    tasks = [(str(Path(a.cache)), str(Path(a.spans)), s, meta[meta["session"] == s].to_dict("records")) for s in sessions]
    rows, t0 = [], time.time()
    with mp.Pool(max(1, min(a.jobs, len(tasks)))) as pool:
        for i, rs in enumerate(pool.imap_unordered(run_session, tasks)):
            rows.extend(rs)
            if (i + 1) % 10 == 0 or (i + 1) == len(tasks):
                print(f"[tx] {i+1}/{len(tasks)} sessions, {len(rows)} rows, {time.time()-t0:.0f}s", file=sys.stderr)
    fine = {}
    for r_ in rows:
        for k, v in r_.pop("_fine").items():
            fine[k] = fine.get(k, 0) + v
    np.savez_compressed(out / "tx_hists.npz", cnt_edges=CNT_EDGES, gap_edges_ns=GAP_LOG_EDGES_NS, **fine)
    df = pd.DataFrame(rows).sort_values(["session", "window_id"]); df.to_parquet(p, compression="snappy", index=False)
    print(f"[tx] wrote {p}: {len(df)} rows, {time.time()-t0:.0f}s", file=sys.stderr)
    summarise(df); return 0


if __name__ == "__main__":
    raise SystemExit(main())
