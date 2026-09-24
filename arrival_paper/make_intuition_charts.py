"""Intuition charts for the methodology doc.

Produces:
  1. exponential_gaps.png    — exponential PDF vs shorter-than-exp burst PDF
  2. rasters_by_fano.png     — event rasters at Fano ≈ 0.2, 1.0, 5.0, 20
  3. count_hists_by_fano.png — histogram of per-bin counts at same Fano levels
  4. fano_visual.png         — Fano value → what the count histogram looks like

All charts land in /home/vmayeski/kaspar-hft/arrival_paper/figs/.
"""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import os

OUT = '/home/vmayeski/kaspar-hft/arrival_paper/figs'
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)

# =============================================================
# 1) Exponential distribution of gaps (Poisson benchmark)
# =============================================================
fig, ax = plt.subplots(1, 1, figsize=(8, 4))
mu = 300.0                     # arrivals per second
mean_gap = 1.0 / mu            # 3.33 ms
xs = np.linspace(0, 6*mean_gap*1000, 500)  # in ms
# exponential PDF (in ms domain)
pdf_exp = mu * np.exp(-mu * xs/1000) / 1000  # per ms
ax.plot(xs, pdf_exp, lw=2.5, color='#1f77b4', label=f'Exponential (mean = 3.33 ms, μ = 300/s)')
# quantiles
q = -mean_gap * np.log(1 - np.array([0.5, 0.9, 0.99])) * 1000
for qi, lab in zip(q, ['p50', 'p90', 'p99']):
    ax.axvline(qi, color='#666', ls='--', lw=0.8, alpha=0.6)
    ax.text(qi + 0.2, ax.get_ylim()[1]*0.85 if 'p50' in lab else ax.get_ylim()[1]*0.5,
            lab + f': {qi:.2f} ms', rotation=90, fontsize=8, color='#333')
ax.set_xlabel('gap (ms)')
ax.set_ylabel('probability density (per ms)')
ax.set_title('Exponential distribution of interarrival gaps — Poisson at μ = 300 arrivals/s')
ax.grid(True, alpha=0.3)
ax.legend(loc='upper right')
plt.tight_layout()
plt.savefig(f'{OUT}/exponential_gaps.png', dpi=120)
plt.close()
print(f'saved: exponential_gaps.png')

# =============================================================
# 2 + 3) Rasters and count histograms for different Fano values.
#
# Strategy: for each Fano level, simulate an arrival sequence
# that yields that dispersion in count-per-bin.
#
# - Fano ≈ 0 (highly regular): equally-spaced ticks
# - Fano ≈ 1 (Poisson): homogeneous Poisson
# - Fano ≈ 5 (moderately bursty): Hawkes-like with n = 0.55
# - Fano ≈ 20 (strongly bursty): Hawkes-like with n = 0.85
# =============================================================
T_max   = 60.0        # 60 seconds
bin_T   = 1.0         # 1-second bins
n_bins  = int(T_max / bin_T)
mu_bg   = 20.0        # nominal background rate (arrivals/s)
target_mean_count = mu_bg * bin_T  # 20

def regular(rate, T):
    """Equally-spaced arrivals — Fano → 0."""
    n = int(rate * T)
    ts = np.linspace(0, T, n, endpoint=False)
    # small jitter so bins aren't all identical
    ts = ts + rng.normal(0, 1e-4, size=n)
    return np.clip(np.sort(ts), 0, T)

def homogeneous_poisson(rate, T):
    """Poisson — Fano = 1."""
    n_exp = int(rate * T * 1.2)
    gaps = rng.exponential(1/rate, size=n_exp * 3)
    ts = np.cumsum(gaps)
    ts = ts[ts < T]
    return ts

def hawkes_thinning(mu, alpha, beta, T):
    """Ogata-thinning simulation of exp-Hawkes."""
    ts = []
    t = 0.0
    # upper bound M for thinning: intensity right after each event
    while t < T:
        # current intensity (all past events)
        past = np.array(ts)
        s = np.sum(alpha * np.exp(-beta * (t - past))) if len(past) else 0.0
        lam_upper = mu + s + alpha  # bound after next possible event
        u = rng.exponential(1 / lam_upper)
        t = t + u
        if t >= T: break
        past = np.array(ts)
        lam_actual = mu + np.sum(alpha * np.exp(-beta * (t - past))) if len(past) else mu
        if rng.random() <= lam_actual / lam_upper:
            ts.append(t)
    return np.array(ts)

# Generate the four series, calibrated so mean count per bin is close to target_mean_count
series = {}
series['Fano ≈ 0.0 (highly regular)'] = regular(mu_bg, T_max)
series['Fano ≈ 1.0 (Poisson)']       = homogeneous_poisson(mu_bg, T_max)
# For higher Fano, use Hawkes and adjust background so stationary rate matches target
# stationary rate = mu / (1 - alpha/beta). Aim for rate = mu_bg.
alpha, beta = 3.0, 6.0  # n = 0.5, moderate clustering
mu_h_mod = mu_bg * (1 - alpha/beta)
series['Fano ≈ 5 (moderate bursty)'] = hawkes_thinning(mu_h_mod, alpha, beta, T_max)
alpha, beta = 5.1, 6.0  # n = 0.85, strong clustering, near-critical
mu_h_strong = mu_bg * (1 - alpha/beta)
series['Fano ≈ 20 (near-critical Hawkes)'] = hawkes_thinning(mu_h_strong, alpha, beta, T_max)

# Compute actual Fano and mean-count for each
labels_and_fano = []
for label, ts in series.items():
    if len(ts) < 2: continue
    edges = np.arange(0, T_max + bin_T, bin_T)
    counts, _ = np.histogram(ts, bins=edges)
    F = counts.var() / counts.mean() if counts.mean() > 0 else float('nan')
    labels_and_fano.append((label, ts, counts, F, counts.mean()))
    print(f'{label}: n={len(ts):>6d}  mean_count/bin={counts.mean():>6.2f}  Fano={F:>6.2f}')

# Raster plot — 4 rows, one per series
fig, axes = plt.subplots(4, 1, figsize=(12, 8), sharex=True)
colors = ['#2ca02c', '#1f77b4', '#ff7f0e', '#d62728']
for ax, (label, ts, counts, F, mean_c), col in zip(axes, labels_and_fano, colors):
    ax.vlines(ts, 0, 1, color=col, linewidth=0.6, alpha=0.9)
    # bin count as background
    for k, c in enumerate(counts):
        ax.axvspan(k, k+1, color=col, alpha=0.06 * (c / max(counts.max(), 1)))
    ax.set_ylim(0, 1)
    ax.set_yticks([])
    ax.set_title(f'{label}   [measured Fano = {F:.2f}, mean count/1-s bin = {mean_c:.1f}]',
                 loc='left', fontsize=10)
    ax.set_ylabel('arrivals', fontsize=9)
    for x in np.arange(0, T_max+1, 5):
        ax.axvline(x, color='#ccc', lw=0.3, alpha=0.4)
axes[-1].set_xlabel('time (seconds)')
fig.suptitle('Four arrival processes with the same mean rate (~20 arrivals/s), different Fano', y=1.005, fontsize=12)
plt.tight_layout()
plt.savefig(f'{OUT}/rasters_by_fano.png', dpi=120, bbox_inches='tight')
plt.close()
print(f'saved: rasters_by_fano.png')

# Count histograms — side-by-side
fig, axes = plt.subplots(1, 4, figsize=(15, 3.5), sharey=True)
for ax, (label, ts, counts, F, mean_c), col in zip(axes, labels_and_fano, colors):
    max_c = int(max(counts.max(), 40))
    ax.hist(counts, bins=np.arange(-0.5, max_c+1.5, 1), color=col, alpha=0.75, edgecolor='#222', linewidth=0.5)
    # overlay Poisson(mean_c) PMF for comparison
    from scipy.stats import poisson
    ks = np.arange(0, max_c)
    ax.plot(ks, poisson.pmf(ks, mean_c) * len(counts), 'k-', lw=1.2, alpha=0.7, label=f'Poisson({mean_c:.1f}) pmf')
    ax.set_title(f'{label}\nFano = {F:.2f}', fontsize=9)
    ax.set_xlabel('count in one 1-second bin')
    ax.set_xlim(-1, min(max_c, 100))
    ax.legend(fontsize=7)
axes[0].set_ylabel('bins with that count')
fig.suptitle('Distribution of arrivals-per-1-s-bin at four Fano levels — same mean, different spread', y=1.02, fontsize=12)
plt.tight_layout()
plt.savefig(f'{OUT}/count_hists_by_fano.png', dpi=120, bbox_inches='tight')
plt.close()
print(f'saved: count_hists_by_fano.png')

# =============================================================
# 4) Fano vs T scaling — one curve per process
# =============================================================
fig, ax = plt.subplots(1, 1, figsize=(8, 5))
Ts = np.array([0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0])
for (label, ts, counts, F, mean_c), col in zip(labels_and_fano, colors):
    fanos = []
    for T in Ts:
        if T > T_max / 3: continue
        edges = np.arange(0, T_max + T, T)
        c, _ = np.histogram(ts, bins=edges)
        c = c[:-1]  # drop possibly-partial last bin
        if len(c) < 3 or c.mean() < 0.5: fanos.append(np.nan); continue
        fanos.append(c.var() / c.mean())
    T_use = Ts[:len(fanos)]
    ax.plot(T_use, fanos, 'o-', color=col, lw=1.8, ms=6, label=label + f'  (Fano@1s={F:.2f})')
ax.axhline(1, color='#666', ls='--', lw=0.8, label='Poisson baseline (F = 1)')
ax.set_xscale('log')
ax.set_yscale('log')
ax.set_xlabel('window size T (s)')
ax.set_ylabel('Fano factor F(T)')
ax.set_title('Fano factor vs window size — how Fano behaves as T grows')
ax.grid(True, which='both', alpha=0.3)
ax.legend(loc='upper left', fontsize=9)
plt.tight_layout()
plt.savefig(f'{OUT}/fano_scaling.png', dpi=120)
plt.close()
print(f'saved: fano_scaling.png')

print('\nall charts saved to', OUT)
