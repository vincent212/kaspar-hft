"""Session-clustered bootstrap for the survival shares of tab:nulls and tab:tx-arms.

share(arm, T) = (median_w p99_arm - T) / (median_w p99_H - T), medians over windows, as in the
paper. Resampling unit: session (276), with replacement, B draws; reports the point estimate and
the 2.5 / 97.5 percentiles.

    python3 -m arrival_paper.bootstrap_shares --grid arrival_paper/figs/qsim_grid_v3/qsim_grid.parquet \
        --tx arrival_paper/figs/qsim_grid_tx/qsim_grid_tx.parquet [--B 2000]
"""
import argparse, numpy as np, pandas as pd

def boot(df, num_col, den_col, T, B, rng):
    g = df.groupby('session'); sess = list(g.groups); idx = [g.indices[s] for s in sess]
    a = df[num_col].values; b = df[den_col].values
    def est(ix):
        return (np.median(a[ix]) - T) / (np.median(b[ix]) - T)
    pt = est(np.arange(len(df))); out = np.empty(B)
    for i in range(B):
        pick = rng.integers(0, len(sess), len(sess)); ix = np.concatenate([idx[k] for k in pick]); out[i] = est(ix)
    return pt, np.percentile(out, 2.5), np.percentile(out, 97.5)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--grid', required=True); ap.add_argument('--tx', required=True)
    ap.add_argument('--B', type=int, default=2000); a = ap.parse_args(); rng = np.random.default_rng(0)
    g = pd.read_parquet(a.grid)
    print(f"== tab:nulls survival share of the single-stage p99 excess, session bootstrap B={a.B}, {g.session.nunique()} sessions ==")
    print(f"{'T':>4} | {'gap shuffle G':>24} | {'1 s-binned B':>24} | {'uniform P':>24}")
    for T in (8, 16, 32, 64, 128):
        h = f'T{T}_h1p7us_H_N1_p99_us'; cells = []
        for arm in ('G', 'B', 'P'):
            pt, lo, hi = boot(g, f'T{T}_h1p7us_{arm}_N1_p99_us', h, T, a.B, rng)
            cells.append(f"{pt*100:5.1f}% [{lo*100:5.1f}, {hi*100:5.1f}]")
        print(f"{T:>4} | " + " | ".join(f"{c:>24}" for c in cells))
    t = pd.read_parquet(a.tx)
    print(f"\n== tab:tx-arms survival share (NQ slope, packet-weighted), session bootstrap B={a.B} ==")
    print(f"{'T':>4} | " + " | ".join(f"{x:>24}" for x in ('TG', 'TP', 'TS', 'TM', 'TW')))
    for T in (16, 32, 64, 128):
        h = f'nq_T{T}_H_N1_p99_us'; cells = []
        for arm in ('TG', 'TP', 'TS', 'TM', 'TW'):
            pt, lo, hi = boot(t, f'nq_T{T}_{arm}_N1_p99_us', h, T, a.B, rng)
            cells.append(f"{pt*100:5.1f}% [{lo*100:5.1f}, {hi*100:5.1f}]")
        print(f"{T:>4} | " + " | ".join(f"{c:>24}" for c in cells))
    print("\n== tail ratio p99/T, single stage, real stream ==")
    for T in (8, 16, 32, 64, 128):
        c = f'T{T}_h1p7us_H_N1_p99_us'; gg = g.groupby('session'); sess = list(gg.groups); idx = [gg.indices[s] for s in sess]; v = g[c].values
        bs = [np.median(v[np.concatenate([idx[k] for k in rng.integers(0, len(sess), len(sess))])]) / T for _ in range(a.B)]
        print(f"  T={T:>3}: {np.median(v)/T:.2f} [{np.percentile(bs,2.5):.2f}, {np.percentile(bs,97.5):.2f}]")
main()
