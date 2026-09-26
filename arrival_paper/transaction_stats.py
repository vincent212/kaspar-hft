"""Packets and messages per CME transaction (messages sharing one transactTime = one
matching-engine event), from the per-message tapes. Also: intra-transaction packet gaps vs
gaps between transactions, and the share of packets/messages that belong to multi-packet
transactions.

    python3 -m arrival_paper.transaction_stats --tapes-dir .../tapes/318/message [--sessions 20]
"""
import argparse, glob, os, sys
import numpy as np, pandas as pd

def one(path):
    d = pd.read_csv(path, usecols=["transactTime", "sendingTime", "packet_seq"])
    d = d[(d.transactTime > 0) & (d.sendingTime > 0)]
    pk = d.groupby("packet_seq").agg(t=("sendingTime", "min"), n=("sendingTime", "size"),
                                     tx0=("transactTime", "min"), tx1=("transactTime", "max"),
                                     ntx=("transactTime", "nunique")).sort_values("t")
    tx = d.groupby("transactTime").agg(n_msg=("packet_seq", "size"), n_pkt=("packet_seq", "nunique"),
                                       t0=("sendingTime", "min"), t1=("sendingTime", "max"))
    # gap between consecutive packets, labelled same-transaction if they share a transactTime
    t = pk.t.values; g = np.diff(t)
    same = (pk.tx1.values[:-1] >= pk.tx0.values[1:]) & (pk.tx0.values[:-1] <= pk.tx1.values[1:])
    return tx, pk, g, same

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--tapes-dir", required=True); ap.add_argument("--sessions", type=int, default=20)
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.tapes_dir, "*.csv")))
    step = max(1, len(files) // a.sessions); files = files[::step][:a.sessions]
    TX, PK, G, SAME = [], [], [], []
    for f in files:
        tx, pk, g, same = one(f); TX.append(tx); PK.append(pk); G.append(g); SAME.append(same)
        print(f"[tx] {os.path.basename(f)}: {len(tx):,} transactions, {len(pk):,} packets", file=sys.stderr)
    tx = pd.concat(TX); pk = pd.concat(PK); g = np.concatenate(G); same = np.concatenate(SAME)
    print(f"sessions {len(files)}: {len(tx):,} transactions, {len(pk):,} packets, {int(tx.n_msg.sum()):,} messages")
    print("\npackets per transaction:")
    vc = tx.n_pkt.value_counts().sort_index()
    for k in [1, 2, 3, 4, 5]:
        print(f"  {k:>3}: {vc.get(k,0)/len(tx)*100:7.3f}% of transactions")
    for lo, hi in [(6, 10), (11, 20), (21, 50), (51, 10**9)]:
        m = (tx.n_pkt >= lo) & (tx.n_pkt <= hi); print(f"  {lo:>3}-{hi if hi<10**9 else 'max'}: {m.mean()*100:7.3f}% of transactions")
    print(f"  quantiles p50/p90/p99/p99.9/max: {tx.n_pkt.quantile([.5,.9,.99,.999]).tolist()} / {tx.n_pkt.max()}")
    print("\nmessages per transaction: p50/p90/p99/p99.9/max:", tx.n_msg.quantile([.5,.9,.99,.999]).tolist(), "/", tx.n_msg.max())
    multi = tx.n_pkt > 1
    print(f"\nmulti-packet transactions: {multi.mean()*100:.2f}% of transactions, carrying "
          f"{tx.n_pkt[multi].sum()/tx.n_pkt.sum()*100:.1f}% of transaction-packets and {tx.n_msg[multi].sum()/tx.n_msg.sum()*100:.1f}% of messages")
    dur = (tx.t1 - tx.t0)[multi] / 1e3
    print(f"duration of multi-packet transactions (first to last packet, us): p50 {dur.median():.1f}  p90 {dur.quantile(.9):.1f}  p99 {dur.quantile(.99):.1f}")
    print(f"\npacket-to-packet gaps (us): within a transaction p1/p10/p50 {np.quantile(g[same],[.01,.1,.5])/1e3}  (n={same.sum():,})")
    print(f"                          between transactions p1/p10/p50 {np.quantile(g[~same],[.01,.1,.5])/1e3}  (n={(~same).sum():,})")
    for thr in [7.5, 16, 32]:
        tg = g < thr * 1e3
        print(f"  gaps < {thr:>4} us: {tg.mean()*100:.2f}% of all gaps; of those {same[tg].mean()*100:.1f}% are inside one transaction")
    print(f"\npackets spanning more than one transaction: {(pk.ntx>1).mean()*100:.2f}%")
main()
