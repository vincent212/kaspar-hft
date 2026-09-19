# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Synthetic exp-Hawkes → G/D/1 diagnostic.

Feed the paper's qsim a KNOWN exponential-Hawkes arrival stream at each
grid point (lambda_bar, n, beta) and record the resulting sys_time
percentiles under varying service times s. No real-data messiness — no
packet stacking, no session-state snapshots, no roll windows. If the
qsim produces a monotone p99 gradient in (lambda_bar, n) here, the qsim
is correct and any inversion in the real-data grid is a data-side
artefact.

Ogata thinning simulation of lambda(t) = mu + alpha * sum_{t_i<t} exp(-beta(t-t_i))
with alpha = n * beta, mu = lambda_bar * (1 - n).

CLI:
    python3 -m arrival_paper.synthetic_qsim_grid \\
        --out-dir arrival_paper/figs/synth_qsim \\
        [--seed 42]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


LAMBDA_BARS = [1.0, 10.0, 100.0, 1000.0, 10000.0]        # evt / sec
N_BRANCHES  = [0.30, 0.50, 0.70, 0.85, 0.95]              # branching ratio
BETAS       = [100.0, 1000.0, 10000.0]                    # 1 / sec
S_US        = [1, 3, 7, 30, 100]                          # service time in us

MIN_EVENTS_FLOOR = 1000
MIN_SECONDS      = 60.0


def ogata_thin_exp_hawkes(mu: float, alpha: float, beta: float,
                          T: float, rng: np.random.Generator,
                          max_events: int = 10_000_000) -> np.ndarray:
    """Simulate an exp-Hawkes on [0, T] via Ogata thinning.

    lambda(t) = mu + alpha * sum_{t_i < t} exp(-beta(t - t_i))

    Returns arrival times in seconds (float64, strictly increasing).
    Subcritical n = alpha/beta < 1 required.
    """
    if alpha >= beta:
        raise ValueError(f"supercritical: alpha={alpha} >= beta={beta}")

    events: list[float] = []
    t = 0.0
    # Running intensity above baseline: R(t) = sum exp(-beta(t - t_i)).
    R = 0.0
    while t < T and len(events) < max_events:
        lam_bar = mu + alpha * R           # upper bound on lambda over [t, ...] since exp decays
        if lam_bar <= 0:
            # Shouldn't happen when mu > 0; guard anyway.
            t += 1.0
            R = 0.0
            continue
        w = rng.exponential(1.0 / lam_bar)
        t_new = t + w
        if t_new >= T:
            break
        # Decay R across the gap:
        R_new = R * np.exp(-beta * w)
        lam_new = mu + alpha * R_new
        u = rng.uniform(0.0, lam_bar)
        if u <= lam_new:
            events.append(t_new)
            R = R_new + 1.0                # accept: add this event's contribution
        else:
            R = R_new                       # reject: intensity continues decaying
        t = t_new
    return np.asarray(events, dtype=np.float64)


def qsim_p50p99(arrivals_ns: np.ndarray, service_ns: int) -> tuple[float, float, float]:
    """Same Lindley recursion the panel builder uses. Returns (p50, p99, max)
    of per-arrival system time in ns."""
    a = np.asarray(arrivals_ns, dtype=np.int64)
    n = len(a)
    if n < 2:
        return (float("nan"), float("nan"), float("nan"))
    idx = np.arange(1, n + 1, dtype=np.int64)
    running = np.maximum.accumulate(a - (idx - 1) * service_ns)
    depart = running + idx * service_ns
    sys_time = depart - a
    return (float(np.quantile(sys_time, 0.50)),
            float(np.quantile(sys_time, 0.99)),
            float(sys_time.max()))


def run() -> pd.DataFrame:
    rng = np.random.default_rng(42)
    rows = []
    total = len(LAMBDA_BARS) * len(N_BRANCHES) * len(BETAS)
    done = 0
    for lam in LAMBDA_BARS:
        for n in N_BRANCHES:
            for beta in BETAS:
                alpha = n * beta
                mu    = lam * (1.0 - n)
                T = max(MIN_EVENTS_FLOOR / lam, MIN_SECONDS)
                arrivals_sec = ogata_thin_exp_hawkes(mu, alpha, beta, T, rng)
                arrivals_ns  = (arrivals_sec * 1e9).astype(np.int64)
                # Guarantee strict monotone after rounding; rare bumps at extreme rates.
                arrivals_ns = np.unique(arrivals_ns) if len(arrivals_ns) != len(np.unique(arrivals_ns)) else arrivals_ns
                for s_us in S_US:
                    service_ns = s_us * 1000
                    p50, p99, mx = qsim_p50p99(arrivals_ns, service_ns)
                    rows.append({
                        "lambda_bar": lam, "n_branch": n, "beta": beta,
                        "s_us": s_us, "T_sec": T, "n_events": len(arrivals_ns),
                        "p50_sys_ns": p50, "p99_sys_ns": p99, "max_sys_ns": mx,
                        "rho": lam * (s_us * 1e-6),
                    })
                done += 1
                print(f"[synth] {done}/{total}  lam={lam:g}  n={n:.2f}  "
                      f"beta={beta:g}  N={len(arrivals_ns):,}",
                      file=sys.stderr)
    return pd.DataFrame(rows)


def plot_grid(df: pd.DataFrame, beta: float, s_us: int, out_path: Path) -> None:
    sub = df[(df["beta"] == beta) & (df["s_us"] == s_us)].copy()
    piv = sub.pivot(index="lambda_bar", columns="n_branch", values="p99_sys_ns")
    piv = piv / 1000.0     # ns -> us

    fig, ax = plt.subplots(figsize=(7.5, 6.0))
    from matplotlib.colors import LogNorm
    v = piv.values
    finite = v[np.isfinite(v) & (v > 0)]
    norm = LogNorm(vmin=max(1e-9, finite.min()),
                   vmax=finite.max()) if finite.size else None
    im = ax.imshow(v, origin="lower", aspect="auto", cmap="OrRd", norm=norm)
    for iy in range(v.shape[0]):
        for ix in range(v.shape[1]):
            val = v[iy, ix]
            s = "n/a" if not np.isfinite(val) else (f"{val:.0f}" if val >= 100 else f"{val:.1f}")
            ax.text(ix, iy, s, ha="center", va="center", fontsize=9)
    ax.set_xticks(range(len(piv.columns)))
    ax.set_yticks(range(len(piv.index)))
    ax.set_xticklabels([f"{c:.2f}" for c in piv.columns])
    ax.set_yticklabels([f"{r:g}" for r in piv.index])
    ax.set_xlabel("n (branching ratio)")
    ax.set_ylabel("lambda_bar (evt/sec)")
    ax.set_title(f"p99 system time (us) — synthetic Hawkes, beta={beta:g}/s, s={s_us}us")
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label("median p99 (us)")
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out-dir", required=True)
    args = ap.parse_args()

    df = run()
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    df.to_parquet(out / "synth_qsim_grid.parquet", compression="snappy", index=False)
    print(f"[synth] wrote {out}/synth_qsim_grid.parquet", file=sys.stderr)

    for beta in BETAS:
        for s_us in S_US:
            fname = out / f"grid_beta{int(beta)}_s{s_us}us.png"
            plot_grid(df, beta, s_us, fname)
            print(f"[synth] wrote {fname.name}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
