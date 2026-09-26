"""Same transaction pairs on two clocks: engine (transactTime) vs publisher (sendingTime).

Per NQ front-month transaction (distinct transactTime): its engine time and the sendingTime of the
packet that carries its first message. For every consecutive pair: engine gap and publisher gap.
Prints the joint distribution, the publisher gap given the engine gap, and the engine gap given the
publisher gap.

    python3 -m arrival_paper.clock_pairs --tapes .../message_eoe --n 20
"""
import argparse, glob, os, numpy as np, pandas as pd
from multiprocessing import Pool

EB = [-1, 1e3, 7.5e3, 16e3, 100e3, np.inf];  EL = ['< 1 us', '1-7.5 us', '7.5-16 us', '16-100 us', '> 100 us']
PB = [-0.5, 0.5, 7.5e3, 10e3, 16e3, 100e3, np.inf]; PL = ['same packet (0)', '0-7.5 us', '7.5-10 us', '10-16 us', '16-100 us', '> 100 us']

def one(path):
    d = pd.read_csv(path, usecols=['transactTime', 'sendingTime'])
    d = d[(d.transactTime > 0) & (d.sendingTime > 0)]
    if len(d) < 100_000: return None
    tx = d.groupby('transactTime', sort=True).sendingTime.min()
    te = tx.index.values.astype(np.int64); ts = tx.values.astype(np.int64)
    ge = np.diff(te); gp = np.diff(ts)
    ok = gp >= 0                                   # publisher order agrees with engine order
    h = np.histogram2d(ge[ok], gp[ok], bins=[EB, PB])[0]
    return h, (~ok).sum(), len(ge)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--tapes', required=True); ap.add_argument('--n', type=int, default=20); ap.add_argument('--jobs', type=int, default=20)
    a = ap.parse_args(); files = [f for f in sorted(glob.glob(os.path.join(a.tapes, '*.csv'))) if os.path.getsize(f) > 50_000_000]
    files = files[::max(1, len(files) // a.n)][:a.n]
    with Pool(a.jobs) as pool: r = [x for x in pool.map(one, files) if x is not None]
    H = sum(x[0] for x in r); neg = sum(x[1] for x in r); tot = sum(x[2] for x in r)
    print(f"{len(r)} sessions, {tot:,} consecutive transaction pairs; publisher order reversed in {neg/tot*100:.4f}% (excluded)")
    J = pd.DataFrame(H / H.sum() * 100, index=EL, columns=PL)
    print("\n=== joint, % of all pairs (rows engine gap, columns publisher gap) ==="); print(J.round(2).to_string())
    print("\n=== publisher gap given engine gap, row % ==="); R = pd.DataFrame(H / H.sum(1, keepdims=True) * 100, index=EL, columns=PL); R['share of pairs'] = H.sum(1) / H.sum() * 100; print(R.round(2).to_string())
    print("\n=== engine gap given publisher gap, column % ==="); C = pd.DataFrame(H / H.sum(0, keepdims=True) * 100, index=EL, columns=PL); C.loc['share of pairs'] = H.sum(0) / H.sum() * 100; print(C.round(2).to_string())
main()
