# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Hawkes smoke test on one session.

Purpose: give the paper's core hypothesis a first, honest look.

    "Days with higher Hawkes intensity / higher branching ratio
     see fatter adverse-selection markout tails."

For a single session's message_tape + fill_tape:

    1. Extract the arrival timestamps of all MBO events on the target
       securityID (transactTime, RTH only). This is the raw point process.
    2. Fit an exponential-kernel Hawkes to those arrivals via MLE:
           λ(t) = μ + α · Σ_{t_i < t} exp(-β · (t - t_i))
       Free parameters: (μ, α, β). Report:
           n = α / β    branching ratio (criticality)
           λ̄ = μ / (1 − n)   long-run mean intensity  (n < 1)
    3. Evaluate λ(t) at each fill's exec_ts using the same fitted (α, β, μ).
       Bucket fills by λ(exec_ts) decile, report:
           mean markout_1s per decile   (adverse selection)
           |markout|_p95 per decile     (tail)
       plus Spearman correlation ρ(λ̂, markout) for a single-number summary.

The point of the smoke test is not to run production statistics — it is to
see whether the sign of the relationship goes the right way on ONE
representative day before we commit to the full 830-session pipeline.

CLI:
    python3 -m arrival_paper.hawkes_smoke \\
        --msg-tape /vast/…/318/message/20250310.NQH5.csv \\
        --fill-tape /vast/…/318/fill/20250310.NQH5.parquet \\
        [--sample-fraction 0.1] [--max-events 500000]
"""

from __future__ import annotations

import argparse
import csv
import gzip
import sys
from pathlib import Path
from typing import Tuple

import numpy as np
import pandas as pd
from scipy import optimize, stats


# ---------------------------------------------------------------------------
# Load arrival timestamps
# ---------------------------------------------------------------------------


def load_arrivals(msg_tape_csv: str, max_events: int = 0) -> np.ndarray:
    """Read msgtape CSV; return the arrival transactTime array (int64 ns),
    sorted ascending, deduplicated by exact timestamp.

    If max_events > 0, downsample uniformly to that many events (keeps every
    Nth) to keep MLE runtime bounded on 10 M-event sessions.
    """
    opener = gzip.open if msg_tape_csv.endswith(".gz") else open
    ts_list: list[int] = []
    with opener(msg_tape_csv, "rt", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        i = header.index("transactTime")
        for row in reader:
            try:
                ts_list.append(int(row[i]))
            except (IndexError, ValueError):
                continue
    ts = np.asarray(ts_list, dtype=np.int64)
    ts.sort()
    # Deduplicate exact-tie timestamps: exp-Hawkes MLE prefers strictly
    # increasing arrivals; ties can be handled but complicate the recursion.
    ts = np.unique(ts)
    if max_events and len(ts) > max_events:
        step = len(ts) // max_events
        ts = ts[::step][:max_events]
    return ts


# ---------------------------------------------------------------------------
# Exponential-kernel Hawkes MLE
# ---------------------------------------------------------------------------


def hawkes_loglik(params: np.ndarray, t: np.ndarray, T: float) -> float:
    """Negative log-likelihood of an exp-Hawkes with parameters (μ, α, β).

    λ(t) = μ + α · Σ_{t_i < t} exp(-β · (t - t_i))

    Uses the standard Ogata recursion (O(N)) for the sum inside the log
    and a closed-form for the compensator integral.

    t : arrival times (float, monotone increasing, in the same unit as T)
    T : observation-window length (from t[0] to t[-1] + a small epsilon)
    """
    mu, alpha, beta = params
    if mu <= 0 or alpha < 0 or beta <= 0 or alpha >= beta:
        return 1e12   # infeasible

    # Recursion for R_i = Σ_{j<i} exp(-β (t_i - t_j))
    n = len(t)
    if n < 2:
        return 1e12
    dt = np.diff(t)
    R = np.zeros(n)
    for i in range(1, n):
        R[i] = np.exp(-beta * dt[i - 1]) * (1.0 + R[i - 1])
    # Log-intensity at each event.
    lam = mu + alpha * R
    if np.any(lam <= 0):
        return 1e12
    log_lam_sum = np.sum(np.log(lam))
    # Compensator: μ * T + α/β * Σ (1 - exp(-β (T - t_i)))
    tail = 1.0 - np.exp(-beta * (T - t))
    compensator = mu * T + (alpha / beta) * np.sum(tail)
    return compensator - log_lam_sum


def fit_hawkes(t: np.ndarray,
               x0: Tuple[float, float, float] = (1.0, 0.5, 1.0)) -> dict:
    """Fit (μ, α, β) by minimising the negative log-likelihood.

    Input `t` is arrival times in NATIVE UNITS (seconds). We convert internally
    to that scale so β has units of 1/sec and the fitted numbers are
    interpretable.
    """
    if len(t) < 100:
        raise ValueError(f"too few events for a stable fit: {len(t)}")
    t = np.asarray(t, dtype=np.float64)
    t = t - t[0]        # start at 0
    T = t[-1] + 1e-6
    # Start from reasonable priors: μ = N/(2T), α/β = 0.5, β = 1
    x0 = (len(t) / (2.0 * T), 0.5, 1.0)
    res = optimize.minimize(
        hawkes_loglik, x0=x0, args=(t, T),
        method="Nelder-Mead",
        options={"xatol": 1e-6, "fatol": 1e-4, "maxiter": 5000},
    )
    mu, alpha, beta = res.x
    n_branch = alpha / beta
    lambda_bar = mu / (1.0 - n_branch) if n_branch < 1 else np.nan
    return {
        "mu": float(mu),
        "alpha": float(alpha),
        "beta": float(beta),
        "n_branch": float(n_branch),
        "lambda_bar": float(lambda_bar),
        "n_events": int(len(t)),
        "T_seconds": float(T),
        "converged": bool(res.success),
        "final_nll": float(res.fun),
    }


# ---------------------------------------------------------------------------
# Intensity at arbitrary times (post-fit)
# ---------------------------------------------------------------------------


def intensity_at(t_events: np.ndarray, t_query: np.ndarray,
                 mu: float, alpha: float, beta: float,
                 t0: float) -> np.ndarray:
    """Evaluate λ(t_query) under the fitted Hawkes, using the FULL event
    history up to (and strictly before) each query time.

    Inputs are in the same time-scale as the fit; caller must pass t0 (the
    offset that was used to zero-align t during fitting) so t_query is put on
    the same scale.
    """
    t_events = np.asarray(t_events, dtype=np.float64) - t0
    t_query = np.asarray(t_query, dtype=np.float64) - t0
    lam = np.full(t_query.shape, mu, dtype=np.float64)
    # For each query, sum over events strictly before it.
    # Vectorised via searchsorted + cumulative decay is possible but for a
    # first-pass smoke test the O(N * M) loop is fine on ~500k events.
    idx_end = np.searchsorted(t_events, t_query, side="left")
    for i, (qt, ie) in enumerate(zip(t_query, idx_end)):
        if ie == 0:
            continue
        dt = qt - t_events[:ie]
        lam[i] += alpha * np.sum(np.exp(-beta * dt))
    return lam


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------


def report(msg_tape: str, fill_tape: str,
           max_events_for_fit: int = 200_000,
           max_fills_for_intensity: int = 20_000) -> dict:
    print(f"[hawkes_smoke] loading {msg_tape}", file=sys.stderr)
    ts_ns = load_arrivals(msg_tape, max_events=max_events_for_fit)
    if len(ts_ns) < 100:
        raise RuntimeError(f"only {len(ts_ns)} arrivals; too few")

    # Convert to seconds for fit stability.
    t_sec = ts_ns.astype(np.float64) / 1e9
    print(f"[hawkes_smoke] {len(t_sec):,} arrivals over "
          f"{(t_sec[-1] - t_sec[0]) / 3600:.2f} h", file=sys.stderr)

    fit = fit_hawkes(t_sec)
    print(f"[hawkes_smoke] fit: μ={fit['mu']:.4f}/s  "
          f"α={fit['alpha']:.4f}  β={fit['beta']:.4f}", file=sys.stderr)
    print(f"[hawkes_smoke] branching ratio n = {fit['n_branch']:.4f}  "
          f"(criticality = 1 - n)", file=sys.stderr)
    print(f"[hawkes_smoke] long-run λ̄ = {fit['lambda_bar']:.4f} events/s",
          file=sys.stderr)
    print(f"[hawkes_smoke] converged: {fit['converged']}", file=sys.stderr)

    # ------------------------------------------------------------------
    # Intensity at fill times → markout per intensity decile
    # ------------------------------------------------------------------
    fill = pd.read_parquet(fill_tape)
    fill = fill.dropna(subset=["exec_ts", "markout_1s"]).copy()
    if len(fill) > max_fills_for_intensity:
        fill = fill.sample(max_fills_for_intensity, random_state=42).sort_values("exec_ts")
    print(f"[hawkes_smoke] computing λ̂ at {len(fill):,} fill times",
          file=sys.stderr)

    t0 = t_sec[0]
    fill_t_sec = fill["exec_ts"].to_numpy(dtype=np.float64) / 1e9
    lam_hat = intensity_at(t_sec, fill_t_sec, fit["mu"], fit["alpha"],
                           fit["beta"], t0)
    fill["lambda_hat"] = lam_hat

    # Decile buckets on intensity.
    fill["lambda_decile"] = pd.qcut(fill["lambda_hat"], q=10,
                                    duplicates="drop", labels=False)
    agg = fill.groupby("lambda_decile").agg(
        n_fills=("markout_1s", "size"),
        mean_lambda=("lambda_hat", "mean"),
        median_markout_100ms=("markout_100ms", "median"),
        median_markout_1s=("markout_1s", "median"),
        median_markout_10s=("markout_10s", "median"),
        p95_absmarkout_1s=("markout_1s", lambda s: s.abs().quantile(0.95)),
        p95_absmarkout_10s=("markout_10s", lambda s: s.abs().quantile(0.95)),
    ).reset_index()

    print(file=sys.stderr)
    print("[hawkes_smoke] markout by λ̂ decile:", file=sys.stderr)
    print(agg.to_string(index=False), file=sys.stderr)

    # Spearman rho.
    rho, pval = stats.spearmanr(fill["lambda_hat"], fill["markout_1s"].abs())
    print(file=sys.stderr)
    print(f"[hawkes_smoke] Spearman ρ(λ̂, |markout_1s|) = {rho:+.4f}  "
          f"(p = {pval:.3g})", file=sys.stderr)

    return {"fit": fit, "decile_table": agg.to_dict("records"),
            "spearman_rho": float(rho), "spearman_p": float(pval)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True)
    ap.add_argument("--fill-tape", required=True)
    ap.add_argument("--max-events-for-fit", type=int, default=200_000)
    ap.add_argument("--max-fills-for-intensity", type=int, default=20_000)
    args = ap.parse_args()
    report(msg_tape=args.msg_tape, fill_tape=args.fill_tape,
           max_events_for_fit=args.max_events_for_fit,
           max_fills_for_intensity=args.max_fills_for_intensity)
    return 0


if __name__ == "__main__":
    sys.exit(main())
