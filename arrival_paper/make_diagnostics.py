# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Recompute the Poisson-rejection diagnostics quoted in Section 3.2.

Reads the qsim_prep.py cache and reports, for the Hawkes (real) arm and for the
paper's own Poisson-null arm:

  * the fraction of interarrival gaps shorter than one tenth of the mean gap,
    against the Poisson benchmark 1 - exp(-0.1) = 9.52%;
  * the Fano factor F(tau) = Var[C_tau]/E[C_tau] over a range of bin widths,
    and the Hurst exponent H = (slope + 1)/2 from the log-log fit;
  * the same Fano factor after a Fisher-Yates shuffle of each window's gap
    sequence, which preserves the gap marginal and destroys only the ordering.

Running the estimator over the Poisson-null arm is the control: it must return
F = 1 at every bin width and H = 0.5. It is reported alongside the Hawkes
figures rather than assumed, because an earlier version of this estimator
folded each window's ragged remainder into the last bin, which is invisible at
0.1 s (18,000 bins) and dominant at 60 s (30 bins).

CLI:
    python3 -m arrival_paper.make_diagnostics \\
        --cache arrival_paper/figs/qsim_cache
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

BINS = [0.1, 0.5, 1.0, 5.0, 10.0, 60.0]
MIN_BINS = 10
SHUFFLE_SEED = 12345


def fano(t: np.ndarray, bin_s: float) -> float | None:
    """Var/mean of counts in whole bins of width bin_s. Returns None if the
    window is too short to give MIN_BINS whole bins."""
    bn = int(bin_s * 1e9)
    nb = int(t[-1]) // bn
    if nb < MIN_BINS:
        return None
    # Whole bins only: folding the ragged remainder into the last bin gives it
    # a double-length exposure and inflates the variance.
    tt = t[t < nb * bn]
    c = np.bincount((tt // bn).astype(np.int64), minlength=nb)[:nb]
    m = c.mean()
    return float(c.var() / m) if m > 0 else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cache", required=True,
                    help="qsim_prep.py output dir (metadata.parquet + arrivals/)")
    args = ap.parse_args()

    cache = Path(args.cache)
    meta_path = cache / "metadata.parquet"
    if not meta_path.exists():
        print(f"[diag] {meta_path} not found; run qsim_prep.py first",
              file=sys.stderr)
        return 1
    meta = pd.read_parquet(meta_path)
    rng = np.random.default_rng(SHUFFLE_SEED)

    gap_frac: list[float] = []
    F: dict[tuple[str, float], list[float]] = {
        (a, b): [] for a in ("H", "P", "S") for b in BINS}

    for sess, sub in meta.groupby("session"):
        f = cache / "arrivals" / f"{sess}.npz"
        if not f.exists():
            continue
        with np.load(f) as z:
            for w in sub["window_id"]:
                for arm in ("H", "P"):
                    k = f"w{int(w)}_{arm}"
                    if k not in z.files:
                        continue
                    t = z[k].astype(np.int64)
                    t = t - t[0]
                    for b in BINS:
                        v = fano(t, b)
                        if v is not None:
                            F[(arm, b)].append(v)
                    if arm != "H":
                        continue
                    g = np.diff(t)
                    if len(g) >= 100:
                        gap_frac.append(float((g < 0.1 * g.mean()).mean()))
                    # Gap-shuffled surrogate: same marginal, no ordering.
                    gs = g.copy()
                    rng.shuffle(gs)
                    ts = np.concatenate([[0], np.cumsum(gs)])
                    for b in BINS:
                        v = fano(ts, b)
                        if v is not None:
                            F[("S", b)].append(v)

    gf = np.array(gap_frac)
    print(f"windows: {len(gf)}\n")
    print("gaps shorter than 0.1 x mean gap (Poisson benchmark 9.52%)")
    print(f"  median {100*np.median(gf):.1f}%   5-95 {100*np.quantile(gf,.05):.1f}"
          f"-{100*np.quantile(gf,.95):.1f}%   range {100*gf.min():.1f}"
          f"-{100*gf.max():.1f}%\n")

    print(f"{'bin (s)':>8} {'Hawkes':>10} {'gap-shuffled':>14} {'Poisson null':>14}")
    for b in BINS:
        print(f"{b:8.1f} {np.median(F[('H',b)]):10.1f} "
              f"{np.median(F[('S',b)]):14.2f} {np.median(F[('P',b)]):14.2f}")

    x = np.log(BINS)
    print()
    for arm, lab in (("H", "Hawkes"), ("S", "gap-shuffled"), ("P", "Poisson null")):
        y = np.log([np.median(F[(arm, b)]) for b in BINS])
        slope = float(np.polyfit(x, y, 1)[0])
        print(f"  {lab:14s} log-log slope {slope:+.3f}  ->  H = {(slope+1)/2:.3f}")

    for b in (1.0, 5.0):
        raw, sh = np.median(F[("H", b)]), np.median(F[("S", b)])
        print(f"  shuffling removes {100*(raw-sh)/(raw-1):.1f}% of the excess "
              f"over Poisson at tau = {b} s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
