"""Gateway delay (sendingTime - transactTime) against engine-side burst intensity.

Distinguishes a capacity limit at the market-data gateway (delay grows when the engine produces events faster
than the gateway can publish them) from a fixed publication policy (delay flat). Per transaction (distinct
transactTime on the NQ-front --eoe tape): delay = first sendingTime - transactTime; engine burst intensity =
number of other transactions within the preceding 50 us on the engine clock. Also: share of transactions
published in a packet shared with another transaction, by the same intensity bins.

    python3 -m arrival_paper.gateway_delay --tapes .../message_eoe --n 20
"""
import argparse, glob, os, numpy as np, pandas as pd
from multiprocessing import Pool

def one(path):
    d = pd.read_csv(path, usecols=['transactTime', 'sendingTime', 'packet_seq'])
    d = d[(d.transactTime > 0) & (d.sendingTime > 0)]
    if len(d) < 100_000: return None
    g = d.groupby('transactTime'); tx = pd.DataFrame({'st': g.sendingTime.min(), 'pk': g.packet_seq.min()}).reset_index()
    tt = tx.transactTime.values; delay = (tx.st.values - tt) / 1e3
    burst = np.arange(len(tt)) - np.searchsorted(tt, tt - 50_000)          # others within the previous 50 us
    pk_cnt = d.groupby('packet_seq').transactTime.nunique(); shared = tx.pk.map(pk_cnt).values > 1
    return pd.DataFrame({'delay_us': delay, 'burst': burst, 'shared': shared})

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--tapes', required=True); ap.add_argument('--n', type=int, default=20); ap.add_argument('--jobs', type=int, default=10)
    a = ap.parse_args(); files = [f for f in sorted(glob.glob(os.path.join(a.tapes, '*.csv'))) if os.path.getsize(f) > 50_000_000]
    files = files[::max(1, len(files) // a.n)][:a.n]
    with Pool(a.jobs) as p: d = pd.concat([x for x in p.map(one, files) if x is not None], ignore_index=True)
    print(f"{len(files)} sessions, {len(d):,} transactions")
    print(f"gateway delay sendingTime - transactTime (us): p1 {d.delay_us.quantile(.01):.2f}  p50 {d.delay_us.median():.2f}  p90 {d.delay_us.quantile(.9):.2f}  p99 {d.delay_us.quantile(.99):.2f}  p99.9 {d.delay_us.quantile(.999):.2f}")
    bins = [(-1, 0, '0'), (0, 1, '1'), (1, 3, '2-3'), (3, 7, '4-7'), (7, 15, '8-15'), (15, 31, '16-31'), (31, 10**9, '32+')]
    print(f"\n{'other tx in prior 50 us (engine clock)':>40} | {'share':>7} | {'delay p50':>9} {'p90':>7} {'p99':>7} | {'published in a shared packet':>28}")
    for lo, hi, lab in bins:
        q = d[(d.burst > lo) & (d.burst <= hi)]
        if len(q) == 0: continue
        print(f"{lab:>40} | {len(q)/len(d)*100:6.2f}% | {q.delay_us.median():9.2f} {q.delay_us.quantile(.9):7.2f} {q.delay_us.quantile(.99):7.2f} | {q.shared.mean()*100:27.2f}%")
main()
