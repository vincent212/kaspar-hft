"""Are packets bigger while the publisher drains a backlog?

Per packet on the NQ-front --eoe tape: messages (NQ-front only), distinct transactions, the gap to the
previous packet on the publisher clock (sendingTime), and the publisher delay of its first transaction
(sendingTime - transactTime), a proxy for how far behind the publisher is. Reports mean span, share with
span >= 2 and share with >= 2 transactions, by gap bin and by delay bin, and jointly for packets sent at
the floor (gap 7.5-10 us) with low vs high delay.

    python3 -m arrival_paper.drain_span --tapes .../message_eoe --n 20
"""
import argparse, glob, os, numpy as np, pandas as pd
from multiprocessing import Pool

def one(path):
    d = pd.read_csv(path, usecols=['transactTime', 'sendingTime', 'packet_seq'])
    d = d[(d.transactTime > 0) & (d.sendingTime > 0)]
    if len(d) < 100_000: return None
    g = d.groupby('packet_seq')
    p = pd.DataFrame({'st': g.sendingTime.min(), 'msgs': g.size(), 'ntx': g.transactTime.nunique(), 'tt0': g.transactTime.min()}).sort_values('st')
    p['gap_us'] = p.st.diff() / 1e3; p['delay_us'] = (p.st - p.tt0) / 1e3
    return p.iloc[1:][['gap_us', 'delay_us', 'msgs', 'ntx']]

def tab(p, col, bins, lab):
    print(f"\n=== by {lab} ===")
    print(f"{'bin':>16} | {'share':>7} | {'mean span':>9} | {'span>=2':>8} | {'>=2 transactions':>16}")
    for lo, hi, name in bins:
        q = p[(p[col] > lo) & (p[col] <= hi)]
        if len(q) == 0: continue
        print(f"{name:>16} | {len(q)/len(p)*100:6.2f}% | {q.msgs.mean():9.3f} | {(q.msgs>=2).mean()*100:7.2f}% | {(q.ntx>=2).mean()*100:15.2f}%")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--tapes', required=True); ap.add_argument('--n', type=int, default=20); ap.add_argument('--jobs', type=int, default=20)
    a = ap.parse_args(); files = [f for f in sorted(glob.glob(os.path.join(a.tapes, '*.csv'))) if os.path.getsize(f) > 50_000_000]
    files = files[::max(1, len(files) // a.n)][:a.n]
    with Pool(a.jobs) as pool: p = pd.concat([x for x in pool.map(one, files) if x is not None], ignore_index=True)
    print(f"{len(files)} sessions, {len(p):,} packets (NQ front-month messages only)")
    tab(p, 'gap_us', [(-1, 7.5, '< 7.5 us'), (7.5, 10, '7.5-10 us (floor)'), (10, 16, '10-16 us'), (16, 32, '16-32 us'), (32, 100, '32-100 us'),
                      (100, 1000, '0.1-1 ms'), (1000, 1e12, '> 1 ms')], 'gap to previous packet (publisher clock)')
    tab(p, 'delay_us', [(-1e12, 100, '< 100 us'), (100, 200, '100-200 us'), (200, 500, '200-500 us'), (500, 1000, '0.5-1 ms'), (1000, 5000, '1-5 ms'),
                        (5000, 1e12, '> 5 ms')], 'publisher delay of the first transaction (backlog proxy)')
    f = p[(p.gap_us > 7.5) & (p.gap_us <= 10)]
    tab(f, 'delay_us', [(-1e12, 100, '< 100 us'), (100, 500, '100-500 us'), (500, 5000, '0.5-5 ms'), (5000, 1e12, '> 5 ms')],
        'publisher delay, packets sent at the floor only (gap 7.5-10 us)')
main()
