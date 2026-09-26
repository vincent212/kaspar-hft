"""Kernel timescale 1/beta of the exponential Hawkes fit (objections.md TODO 2), on three event streams,
per 30-minute window, on a sample of sessions:
  pkt  -- packet arrivals (the qsim cache, sendingTime of the first message of each packet);
  blk  -- transaction-block starts as in qsim_tx (sendingTime);
  eng  -- distinct transactTime values: the matching engine's clock.
Reports corpus medians and 5-95% ranges of n = alpha/beta, 1/beta, and the fraction of the kernel mass
alpha/beta * (1 - exp(-beta * tau)) acting within tau = 7.5, 16, 32 us, 1 ms.

    python3 -m arrival_paper.kernel_timescale --cache arrival_paper/figs/qsim_cache \
        --spans arrival_paper/figs/qsim_spans_tr --tapes .../message_eoe --n 20
"""
import argparse, os, numpy as np, pandas as pd
from multiprocessing import Pool
from arrival_paper.hawkes_smoke import fit_hawkes
from arrival_paper.qsim_tx import block_ids
WIN = 30 * 60 * 10**9

def fit(t):
    try:
        f = fit_hawkes(np.asarray(t, dtype=np.int64)); return f['n_branch'], 1.0 / f['beta'], f['converged']
    except Exception:
        return np.nan, np.nan, False

def one(args):
    cache, spans, tapes, session = args; out = []
    za = np.load(f'{cache}/arrivals/{session}.npz'); zs = np.load(f'{spans}/{session}.npz')
    tp = os.path.join(tapes, f'{session}.csv')
    eng = None
    if os.path.exists(tp):
        d = pd.read_csv(tp, usecols=['transactTime']); u = np.unique(d.transactTime.values[d.transactTime.values > 0]); eng = u
    for k in zs.files:
        if not k.endswith('_S'): continue
        w = k[:-2]; t = za[w + '_H']
        if len(t) != len(zs[k]): continue
        b = block_ids(zs[w + '_X0'], zs[w + '_X1']); first = np.r_[0, np.flatnonzero(np.diff(b)) + 1]
        row = dict(session=session, window=w)
        for lab, ev in (('pkt', t), ('blk', t[first])):
            row[f'{lab}_n'], row[f'{lab}_inv_beta_s'], row[f'{lab}_conv'] = fit(ev)
        if eng is not None:
            x0 = zs[w + '_X0']; lo, hi = x0.min(), zs[w + '_X1'].max()
            ev = eng[(eng >= lo) & (eng <= hi)]
            row['eng_n'], row['eng_inv_beta_s'], row['eng_conv'] = fit(ev)
        out.append(row)
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cache', required=True); ap.add_argument('--spans', required=True)
    ap.add_argument('--tapes', required=True); ap.add_argument('--n', type=int, default=20); ap.add_argument('--jobs', type=int, default=20)
    a = ap.parse_args(); meta = pd.read_parquet(f'{a.cache}/metadata.parquet')
    sess = sorted(meta.session.unique()); sess = [s for s in sess if os.path.exists(os.path.join(a.tapes, s + '.csv'))]
    sess = sess[::max(1, len(sess) // a.n)][:a.n]
    with Pool(a.jobs) as p: d = pd.DataFrame([r for rs in p.map(one, [(a.cache, a.spans, a.tapes, s) for s in sess]) for r in rs])
    d.to_parquet(os.path.join(os.path.dirname(a.spans), 'kernel_timescale.parquet'))
    print(f"{len(sess)} sessions, {len(d)} windows")
    for lab, name in (('pkt', 'packets (sendingTime)'), ('blk', 'transaction blocks (sendingTime)'), ('eng', 'transactTime values (engine clock)')):
        if f'{lab}_n' not in d: continue
        n = d[f'{lab}_n']; ib = d[f'{lab}_inv_beta_s'] * 1e6; c = d[f'{lab}_conv'].mean() * 100
        print(f"\n{name}: converged {c:.0f}%")
        print(f"  n: median {n.median():.3f}  5-95% {n.quantile(.05):.3f}-{n.quantile(.95):.3f}")
        print(f"  1/beta: median {ib.median():,.0f} us  5-95% {ib.quantile(.05):,.0f}-{ib.quantile(.95):,.0f} us")
        for tau in (7.5, 16, 32, 1000):
            frac = (1 - np.exp(-tau / ib)).median(); print(f"  share of kernel mass within {tau:>6} us: {frac*100:.3f}%   (n x share = {np.median(n * (1 - np.exp(-tau / ib))):.4f})")
main()
