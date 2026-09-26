"""4.9 conditioning table with span columns (objections.md, TODO 11).

Per window: mean span, share of packets with span >= 2, share of packets carrying more than one
transaction (min != max transactTime), p1 packet gap. Reported as corpus medians by quintile of
packet rate and of fitted branching ratio, with Q5/Q1 ratios, and the per-window Spearman
correlation of each span column with n and with the rate.

    python3 -m arrival_paper.span_conditioning --cache arrival_paper/figs/qsim_cache --spans arrival_paper/figs/qsim_spans_tr
"""
import argparse, os, numpy as np, pandas as pd
from multiprocessing import Pool
from scipy.stats import spearmanr

def one(args):
    cache, spans, session = args; out = []
    za = np.load(f'{cache}/arrivals/{session}.npz'); zs = np.load(f'{spans}/{session}.npz')
    for k in zs.files:
        if not k.endswith('_S'): continue
        w = k[:-2]; S = zs[k]; x0 = zs[w + '_X0']; x1 = zs[w + '_X1']; t = za[w + '_H']
        if len(t) != len(S): continue
        out.append(dict(session=session, window_id=int(w[1:]), mean_span=S.mean(), share_span2=(S >= 2).mean(),
                        share_multi_tx=(x0 != x1).mean(), p1_gap_us=np.quantile(np.diff(t), 0.01) / 1e3))
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cache', required=True); ap.add_argument('--spans', required=True); ap.add_argument('--jobs', type=int, default=16)
    a = ap.parse_args(); meta = pd.read_parquet(f'{a.cache}/metadata.parquet')
    with Pool(a.jobs) as p:
        rows = [r for rs in p.map(one, [(a.cache, a.spans, s) for s in meta.session.unique()]) for r in rs]
    d = pd.DataFrame(rows).merge(meta[['session', 'window_id', 'lambda_bar_obs', 'n_branch_pkt']], on=['session', 'window_id'])
    cols = ['mean_span', 'share_span2', 'share_multi_tx', 'p1_gap_us']
    print(f"{len(d)} windows")
    for var, lab in (('lambda_bar_obs', 'packet rate (pkt/s)'), ('n_branch_pkt', 'branching ratio n')):
        d['q'] = pd.qcut(d[var], 5, labels=False) + 1; t = d.groupby('q').agg(**{'median ' + lab: (var, 'median')}, **{c: (c, 'median') for c in cols})
        print(f"\n== by {lab} quintile (corpus medians) =="); print(t.round(4).to_string())
        r = t.loc[5, cols] / t.loc[1, cols]; print('Q5/Q1: ' + '  '.join(f"{c} {r[c]:.3f}" for c in cols))
    print("\n== per-window Spearman correlation ==")
    for c in cols:
        print(f"  {c:>15}: with n {spearmanr(d[c], d.n_branch_pkt).correlation:+.3f}   with rate {spearmanr(d[c], d.lambda_bar_obs).correlation:+.3f}")
main()
