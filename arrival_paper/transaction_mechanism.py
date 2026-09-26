# Copyright (c) 2026 Vincent Mayeski / M2 Tech.  Licensed under the MIT License.
"""Transaction origin of clusters and large packets (paper Section 3.3).

Reads the per-message tapes (same --tapes-dir as qsim_prep.py; columns t_match,
t_send, msg_type, packet_seq, idx_in_packet) and measures, per packet, what
FOLLOWS it, conditioned on (a) the packet's span and (b) whether it carries a
trade. If transactions cause both clustering and large packets, large packets
and trade packets are followed by tight gaps, by further packets, and by packets
sharing their transactTime; single-message packets are not.

Outputs (stdout): LaTeX rows for tab:transactions; (stderr): [prose] numbers.
Statistics are computed per session and reported as medians across sessions.

CLI:
    python3 -m arrival_paper.transaction_mechanism --tapes-dir <dir> [--limit N]
"""
from __future__ import annotations
import argparse, glob, os, sys
import numpy as np
import pandas as pd

TRADE = 100
TIGHT_NS = 20_000        # "tight" follow-on gap: 20 us (the floor is ~7.5 us)
NEAR_NS = 100_000        # 100 us
FLURRY_NS = 1_000_000    # 1 ms
CLASSES = [("span 1", lambda s, t: s == 1),
           ("span 2--9", lambda s, t: (s >= 2) & (s <= 9)),
           ("span $\\ge 10$", lambda s, t: s >= 10),
           ("no trade", lambda s, t: ~t),
           ("trade", lambda s, t: t)]


def per_session(df: pd.DataFrame) -> dict:
    df = df.sort_values(["packet_seq", "idx_in_packet"])
    g = df.groupby("packet_seq", sort=True)
    pk = pd.DataFrame({
        "t_send": g["t_send"].first(),
        "t_match": g["t_match"].first(),
        "span": g.size(),
        "n_trade": g["msg_type"].apply(lambda s: int((s == TRADE).sum())),
        "n_del": g["msg_type"].apply(lambda s: int((s == 2).sum())),
    })
    pk = pk.sort_values("t_send").reset_index(drop=True)
    t = pk["t_send"].to_numpy(np.int64)
    n = len(pk)
    gap_next = np.full(n, np.iinfo(np.int64).max, dtype=np.int64)
    gap_next[:-1] = t[1:] - t[:-1]
    # packets in the following 100 us / 1 ms (exclusive of self)
    idx_near = np.searchsorted(t, t + NEAR_NS, side="right") - np.arange(n) - 1
    idx_flurry = np.searchsorted(t, t + FLURRY_NS, side="right") - np.arange(n) - 1
    same_tx_next = np.zeros(n, dtype=bool)
    tm = pk["t_match"].to_numpy(np.int64)
    same_tx_next[:-1] = tm[1:] == tm[:-1]
    span = pk["span"].to_numpy()
    has_trade = pk["n_trade"].to_numpy() > 0
    out = {"n_packets": n, "frac_span1": float((span == 1).mean()),
           "frac_span1_no_trade": float(((span == 1) & ~has_trade).sum() / max(1, (span == 1).sum())),
           "frac_large_with_trade": float((has_trade & (span >= 10)).sum() / max(1, (span >= 10).sum())),
           "mean_trades_per_large": float(pk.loc[span >= 10, "n_trade"].mean()) if (span >= 10).any() else float("nan"),
           "mean_deletes_per_large": float(pk.loc[span >= 10, "n_del"].mean()) if (span >= 10).any() else float("nan")}
    # transaction groups: packets sharing a transactTime
    grp = pk.groupby("t_match").size()
    out["frac_packets_in_multi_tx"] = float((grp[grp > 1].sum()) / n)
    multi = pk["t_match"].map(grp) > 1
    out["mean_span_multi_tx"] = float(pk.loc[multi, "span"].mean()) if multi.any() else float("nan")
    out["mean_span_single_tx"] = float(pk.loc[~multi, "span"].mean()) if (~multi).any() else float("nan")

    # ---- transaction shape: packets and messages per transactTime group, duration ----
    q = lambda a, p: float(np.quantile(a, p)) if len(a) else float("nan")
    ppt = grp.to_numpy()                                   # packets per transaction
    out["tx_count"] = int(len(ppt))
    out["tx_frac_ge2"] = float((ppt >= 2).mean())
    out["tx_frac_ge3"] = float((ppt >= 3).mean())
    for pq, lab in ((0.5, "p50"), (0.9, "p90"), (0.99, "p99"), (0.999, "p999")):
        out[f"tx_pkts_{lab}"] = q(ppt, pq)
    out["tx_pkts_max"] = int(ppt.max())
    mpt = pk.groupby("t_match")["span"].sum().to_numpy()   # messages per transaction
    for pq, lab in ((0.5, "p50"), (0.99, "p99")):
        out[f"tx_msgs_{lab}"] = q(mpt, pq)
    out["tx_msgs_max"] = int(mpt.max())
    tg = pk.groupby("t_match")["t_send"]
    dur = (tg.max() - tg.min()).to_numpy(np.int64)
    dur = dur[ppt >= 2] / 1e3                               # us, multi-packet transactions only
    for pq, lab in ((0.5, "p50"), (0.9, "p90"), (0.99, "p99")):
        out[f"tx_dur_us_{lab}"] = q(dur, pq)
    out["tx_dur_us_max"] = float(dur.max()) if len(dur) else float("nan")
    # consecutive-packet gaps: within a transaction vs between unrelated packets
    gaps = (t[1:] - t[:-1]) / 1e3                           # us
    within = tm[1:] == tm[:-1]
    for name, sel in (("within", within), ("between", ~within)):
        a = gaps[sel]
        out[f"gap_{name}_n"] = int(len(a))
        for pq, lab in ((0.01, "p1"), (0.1, "p10"), (0.5, "p50"), (0.9, "p90"), (0.99, "p99")):
            out[f"gap_{name}_{lab}"] = q(a, pq)
        out[f"gap_{name}_frac_lt_7p5"] = float((a < 7.5).mean()) if len(a) else float("nan")
        out[f"gap_{name}_frac_lt_20"] = float((a < 20.0).mean()) if len(a) else float("nan")
    for name, sel in CLASSES:
        m = sel(span, has_trade)
        m[-1] = False  # last packet has no follower
        k = int(m.sum())
        out[f"{name}|n"] = k
        if k == 0:
            for q in ("p_tight", "p_near", "mean_flurry", "p_same_tx"):
                out[f"{name}|{q}"] = float("nan")
            continue
        out[f"{name}|p_tight"] = float((gap_next[m] < TIGHT_NS).mean())
        out[f"{name}|p_near"] = float((gap_next[m] < NEAR_NS).mean())
        out[f"{name}|mean_flurry"] = float(idx_flurry[m].mean())
        out[f"{name}|p_same_tx"] = float(same_tx_next[m].mean())
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tapes-dir", required=True)
    ap.add_argument("--limit", type=int, default=0, help="first N tapes only (smoke test)")
    a = ap.parse_args()
    tapes = sorted(glob.glob(os.path.join(a.tapes_dir, "*.csv")))
    if a.limit:
        tapes = tapes[: a.limit]
    if not tapes:
        print("no tapes", file=sys.stderr); return 1
    rows = []
    for p in tapes:
        df = pd.read_csv(p, usecols=["t_match", "t_send", "msg_type", "packet_seq", "idx_in_packet"])
        r = per_session(df); r["session"] = os.path.basename(p); rows.append(r)
        print(f"[tx] {r['session']}: {r['n_packets']} packets", file=sys.stderr)
    R = pd.DataFrame(rows)
    med = R.median(numeric_only=True)
    print("% ===== Table (tab:transactions): what follows a packet, by class; medians across sessions =====")
    print(r"class & packets & $P(\text{next} < 20\,\mu\mathrm{s})$ & $P(\text{next} < 100\,\mu\mathrm{s})$ & packets in next 1\,ms & $P(\text{same transactTime})$ \\")
    for name, _ in CLASSES:
        print(f"{name} & {int(med[f'{name}|n']):,} & {med[f'{name}|p_tight']:.3f} & {med[f'{name}|p_near']:.3f} & "
              f"{med[f'{name}|mean_flurry']:.2f} & {med[f'{name}|p_same_tx']:.3f} \\\\")
    print("\n% ===== Table (tab:transaction-shape): packets per transaction, duration, and within- vs between-transaction gaps =====")
    print(r"\multicolumn{6}{l}{\emph{packets per transaction}} \\")
    print(f"share with $\\ge 2$ packets & {med['tx_frac_ge2']:.4f} & share with $\\ge 3$ & {med['tx_frac_ge3']:.4f} & max & {int(med['tx_pkts_max'])} \\\\")
    print(f"$p_{{50}}$ / $p_{{90}}$ / $p_{{99}}$ / $p_{{99.9}}$ & {med['tx_pkts_p50']:.0f} & {med['tx_pkts_p90']:.0f} & {med['tx_pkts_p99']:.0f} & {med['tx_pkts_p999']:.0f} & \\\\")
    print(r"\multicolumn{6}{l}{\emph{messages per transaction}} \\")
    print(f"$p_{{50}}$ / $p_{{99}}$ / max & {med['tx_msgs_p50']:.0f} & {med['tx_msgs_p99']:.0f} & {int(med['tx_msgs_max'])} & & \\\\")
    print(r"\multicolumn{6}{l}{\emph{duration of a multi-packet transaction, first to last packet (\si{\micro\second})}} \\")
    print(f"$p_{{50}}$ / $p_{{90}}$ / $p_{{99}}$ / max & {med['tx_dur_us_p50']:.1f} & {med['tx_dur_us_p90']:.1f} & {med['tx_dur_us_p99']:.1f} & {med['tx_dur_us_max']:.0f} & \\\\")
    print(r"\multicolumn{6}{l}{\emph{gap to the next packet (\si{\micro\second}): within one transaction vs between unrelated packets}} \\")
    for nm in ("within", "between"):
        print(f"{nm} & $p_1$ {med[f'gap_{nm}_p1']:.1f} & $p_{{10}}$ {med[f'gap_{nm}_p10']:.1f} & $p_{{50}}$ {med[f'gap_{nm}_p50']:.1f} & $p_{{90}}$ {med[f'gap_{nm}_p90']:.1f} & share $< 20\\,\\mu\\mathrm{{s}}$: {med[f'gap_{nm}_frac_lt_20']:.3f} \\\\")
    print("\n[prose] transaction shape (medians across sessions):", file=sys.stderr)
    for k in ("tx_count","tx_frac_ge2","tx_frac_ge3","tx_pkts_p50","tx_pkts_p90","tx_pkts_p99","tx_pkts_p999","tx_pkts_max",
              "tx_msgs_p50","tx_msgs_p99","tx_msgs_max","tx_dur_us_p50","tx_dur_us_p90","tx_dur_us_p99","tx_dur_us_max",
              "gap_within_n","gap_within_p1","gap_within_p50","gap_within_p90","gap_within_frac_lt_7p5","gap_within_frac_lt_20",
              "gap_between_n","gap_between_p1","gap_between_p50","gap_between_p90","gap_between_frac_lt_7p5","gap_between_frac_lt_20"):
        print(f"   {k:26s} {med[k]:.4f}", file=sys.stderr)
    print("\n[prose] composition (medians across sessions):", file=sys.stderr)
    for k in ("frac_span1", "frac_span1_no_trade", "frac_large_with_trade", "mean_trades_per_large",
              "mean_deletes_per_large", "frac_packets_in_multi_tx", "mean_span_multi_tx", "mean_span_single_tx"):
        print(f"   {k:28s} {med[k]:.4f}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
