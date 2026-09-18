"""NQ pilot statistics — one day (2025-03-10, secID 42288528 = NQH5, RTH).

Computes:
  §5.1 marginal-gap stats: CV, CV^2, P(gap < mean/10), quantile ratios
  §5.2 Fano scaling at multiple windows → Hurst exponent
  §5.3 gap ACF at lags {1,2,3,5,10}
  §5.3 Fisher-Yates shuffle collapse (Fano and H)

Reads /vast/home/vmayeski/out/arrival_paper/tapes/318/NQH5.20250310.rth.csv
"""
import sys, time
import numpy as np
import pandas as pd

TAPE = '/vast/home/vmayeski/out/arrival_paper/tapes/318/NQH5.20250310.rth.csv'

def stats(ts_ns, label):
    """ts_ns: sorted int64 array of arrival timestamps in ns."""
    print(f"\n=== {label}  n={len(ts_ns):,}  span={((ts_ns[-1]-ts_ns[0])/1e9):.1f}s ===")
    gaps_ns = np.diff(ts_ns)
    # drop zero gaps (simultaneous transacts, common on CME)
    n_zero = int((gaps_ns == 0).sum())
    print(f"  simultaneous gaps (=0): {n_zero:,} ({n_zero/len(gaps_ns)*100:.2f}%)")
    gaps_us = gaps_ns.astype(np.float64) / 1e3  # microseconds

    m = gaps_us.mean()
    sd = gaps_us.std()
    cv = sd / m
    print(f"  mean gap = {m:.3f} µs   → rate = {1e6/m:,.1f} msg/s")
    print(f"  std gap  = {sd:.3f} µs")
    print(f"  CV       = {cv:.3f}   (Poisson: 1.00)")
    print(f"  CV^2     = {cv*cv:.2f}")

    # quantile ratios vs Exp(mean=m)
    quantiles = [0.1, 0.5, 0.9, 0.99, 0.999]
    print(f"  {'p':>6s}  {'measured µs':>14s}  {'Exp same-µ µs':>14s}  {'ratio':>8s}")
    for p in quantiles:
        meas = np.quantile(gaps_us, p)
        expo = -m * np.log(1 - p)
        print(f"  {p:>6.3f}  {meas:>14.3f}  {expo:>14.3f}  {meas/expo:>8.3f}x")

    # P(gap < mean/10)
    thresh = m / 10.0
    p_short = (gaps_us < thresh).mean() * 100
    print(f"  P(gap < mean/10) = {p_short:.2f}%   (Exp: 9.52%,  ratio={p_short/9.52:.2f}x)")

    # instantaneous rate from median gap
    med_us = np.quantile(gaps_us, 0.5)
    inst_rate = 1e6 / med_us
    mean_rate = 1e6 / m
    print(f"  mean rate = {mean_rate:,.1f} msg/s")
    print(f"  rate from median gap = {inst_rate:,.1f} msg/s   (ratio = {inst_rate/mean_rate:.1f}x, Exp: 1.44x)")

    # gap ACF at select lags
    print(f"  gap ACF @ lag  1,2,3,5,10 :")
    n = len(gaps_us)
    g = gaps_us - gaps_us.mean()
    var = (g*g).sum() / n
    for lag in [1,2,3,5,10]:
        cov = (g[:-lag] * g[lag:]).sum() / n
        acf = cov / var if var > 0 else 0.0
        print(f"    lag {lag:>2}: {acf:+.4f}")

    # Fano scaling
    tspan_s = (ts_ns[-1] - ts_ns[0]) / 1e9
    print(f"  Fano F(T) at multiple T (sec):")
    fano_pts = []
    for T in [0.001, 0.01, 0.1, 1.0, 5.0, 30.0, 300.0]:
        if tspan_s < 2*T: continue
        edges = np.arange(ts_ns[0], ts_ns[-1] + int(T*1e9), int(T*1e9))
        counts, _ = np.histogram(ts_ns, bins=edges)
        # drop last (partial) bin
        counts = counts[:-1]
        if len(counts) < 3: continue
        F = counts.var() / counts.mean() if counts.mean() > 0 else float('nan')
        print(f"    T = {T:>8.3f}s   n_bins={len(counts):>8d}   F(T) = {F:>12.3f}  (Poisson: 1.0)")
        fano_pts.append((T, F))

    # Hurst from Fano scaling: log F ~ (2H-1) log T
    if len(fano_pts) >= 3:
        Ts = np.array([p[0] for p in fano_pts])
        Fs = np.array([p[1] for p in fano_pts])
        # fit on the middle range (avoid edge effects at T<0.01 and T>span/10)
        mask = (Ts >= 0.1) & (Ts <= max(5.0, tspan_s/20))
        if mask.sum() >= 3:
            slope, intercept = np.polyfit(np.log(Ts[mask]), np.log(Fs[mask]), 1)
            H = 0.5 * (slope + 1)
            print(f"  Hurst from Fano scaling: H = {H:.3f}  (Poisson: 0.5)")
            print(f"    fit slope (2H-1) = {slope:+.3f}   intercept = {intercept:+.3f}   from {int(mask.sum())} points in {Ts[mask][0]:.2f}..{Ts[mask][-1]:.1f}s")

    return gaps_ns

def shuffle_test(gaps_ns, seed=42):
    """Apply Fisher-Yates shuffle to gaps, reconstruct arrivals, recompute Fano and Hurst."""
    rng = np.random.default_rng(seed)
    perm = rng.permutation(gaps_ns.copy())
    ts_shuf = np.cumsum(np.concatenate([[0], perm]))
    stats(ts_shuf, "shuffled")

if __name__ == '__main__':
    t0 = time.time()
    print(f"loading {TAPE}...")
    df = pd.read_csv(TAPE, low_memory=False)
    print(f"  loaded {len(df):,} rows in {time.time()-t0:.1f}s")

    # sort by transactTime (usually already sorted but just in case)
    df = df.sort_values('transactTime').reset_index(drop=True)

    # =========== ALL MESSAGES ===========
    ts_all = df['transactTime'].to_numpy()
    gaps_all = stats(ts_all, "ALL messages (MBO + trades), NQH5, RTH")
    shuffle_test(gaps_all)

    # =========== TRADES ONLY ===========
    df_t = df[df['typ'] == 'T']
    ts_t = df_t['transactTime'].to_numpy()
    stats(ts_t, "TRADES only")

    # =========== MBO ADDS ONLY ===========
    df_a = df[(df['typ'] == 'M') & (df['action'] == '0')]
    if len(df_a) > 100:
        ts_a = df_a['transactTime'].to_numpy()
        stats(ts_a, "MBO Adds only")

    print(f"\ntotal elapsed: {time.time()-t0:.1f}s")
