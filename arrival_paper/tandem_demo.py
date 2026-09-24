#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech. Licensed under the MIT License.
"""Self-contained demonstration of the paper's core result.

Simulates an exponential-Hawkes packet-arrival process and a rate-matched
homogeneous-Poisson null, runs an N-stage tandem Lindley recursion on each
under identical service parameters, and reports how end-to-end p50 and p99
latency scale with N.

Reproduces the paper's headline: under Hawkes-clustered arrivals the tail
compresses roughly as 1/N with a small (N-1)*h additive median cost; under
Poisson the tail is essentially flat in N and only the median grows.

Usage:
    python3 tandem_demo.py                       # default parameters
    python3 tandem_demo.py --N 1 2 4 8 12        # custom N sweep
    python3 tandem_demo.py --lambda-bar 500 --n 0.90 --T-us 7.23 --h-us 0.09

Dependencies: numpy, matplotlib (matplotlib optional; --no-plot skips it).
"""
from __future__ import annotations

import argparse
import numpy as np


# ---------------------------------------------------------------------------
# Hawkes simulation (Ogata thinning) and Poisson null
# ---------------------------------------------------------------------------

def simulate_hawkes(mu: float, alpha: float, beta: float, T: float,
                    rng: np.random.Generator) -> np.ndarray:
    """Exponential-Hawkes arrivals on [0, T] via Ogata's thinning algorithm.

    lambda(t) = mu + sum_{t_i < t} alpha * exp(-beta * (t - t_i)).
    Requires alpha < beta (branching ratio n = alpha/beta < 1) for stationarity.
    Returns an ns-precision (int64) array of arrival timestamps in seconds*1e9.
    """
    assert alpha < beta, "n = alpha/beta must be < 1 for stationarity"
    arrivals: list[float] = []
    t = 0.0
    lam = mu
    while t < T:
        lam_bar = lam                             # upper bound on lambda(s) for s >= t
        u = rng.random()
        w = -np.log(u) / lam_bar                  # candidate gap
        t = t + w
        if t >= T:
            break
        # Evaluate true lambda(t) after decay.
        if arrivals:
            dt = t - arrivals[-1]
            lam = mu + (lam - mu) * np.exp(-beta * dt)
        # Accept/reject.
        d = rng.random()
        if d * lam_bar <= lam:
            arrivals.append(t)
            lam = lam + alpha                     # jump on accepted event
    return (np.asarray(arrivals) * 1e9).astype(np.int64)


def poisson_null(n_arrivals: int, T: float,
                 rng: np.random.Generator) -> np.ndarray:
    """Uniform-shuffle of n arrivals on [0, T]; equivalent to a rate-n/T Poisson
    process conditional on the count. Returns ns-precision int64 timestamps."""
    a = np.sort(rng.uniform(0.0, T, size=n_arrivals))
    return (a * 1e9).astype(np.int64)


# ---------------------------------------------------------------------------
# Deterministic-service tandem Lindley
# ---------------------------------------------------------------------------

def lindley(arr_ns: np.ndarray, service_ns: int) -> np.ndarray:
    """Single-server FIFO Lindley recursion with deterministic service.
    Returns per-event time-in-system (wait + service) in ns."""
    n = len(arr_ns)
    wait = np.zeros(n, dtype=np.int64)
    for i in range(1, n):
        w = int(wait[i - 1]) + service_ns - int(arr_ns[i] - arr_ns[i - 1])
        wait[i] = w if w > 0 else 0
    return wait + service_ns


def tandem_lindley(arr_ns: np.ndarray, N: int,
                   total_service_ns: int, hop_ns: int) -> np.ndarray:
    """N-stage series tandem. Each stage has per-stage service T/N and its own
    independent single-server FIFO queue; a hop of h is added between stages.

    Returns per-event end-to-end latency (departure from stage N minus original
    arrival at stage 1), in ns.
    """
    per_stage = total_service_ns // N
    dep = arr_ns.copy()
    for stage in range(N):
        if stage > 0:
            dep = dep + hop_ns              # per-stage hop cost
        # This stage is an independent server: its own Lindley on its arrivals.
        wait = lindley(dep, per_stage)      # returns wait + service
        dep = dep + wait                    # departure from this stage
    return dep - arr_ns


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def run_sweep(mu: float, alpha: float, beta: float, T_sec: float,
              T_service_us: float, h_us: float, N_list: list[int],
              seed: int) -> dict:
    """Simulate one 30-min-equivalent window, run the tandem sweep under both
    Hawkes arrivals and a matched Poisson null. Returns a nested dict of
    (regime, N) -> {p50, p95, p99, p999, max} in microseconds."""
    rng = np.random.default_rng(seed)

    hawkes_arr = simulate_hawkes(mu, alpha, beta, T_sec, rng)
    poi_arr    = poisson_null(len(hawkes_arr), T_sec, rng)

    T_ns = int(round(T_service_us * 1_000))
    h_ns = int(round(h_us          * 1_000))

    result = {"hawkes": {}, "poisson": {}}
    for regime, arr in (("hawkes", hawkes_arr), ("poisson", poi_arr)):
        for N in N_list:
            lat_ns = tandem_lindley(arr, N, T_ns, h_ns)
            lat_us = lat_ns / 1e3
            result[regime][N] = {
                "p50":  float(np.quantile(lat_us, 0.50)),
                "p95":  float(np.quantile(lat_us, 0.95)),
                "p99":  float(np.quantile(lat_us, 0.99)),
                "p999": float(np.quantile(lat_us, 0.999)),
                "max":  float(lat_us.max()),
            }
    result["n_arrivals"] = len(hawkes_arr)
    return result


def print_report(result: dict, N_list: list[int]) -> None:
    n_arr = result["n_arrivals"]
    print(f"\nn arrivals in window: {n_arr:,}")
    print(f"\nEnd-to-end latency (us) — median, p99, p99.9, max — by N:\n")
    header = f"{'N':>4} | " + " | ".join(f"{lbl:>10}" for lbl in
        ["H p50", "H p99", "H p999", "H max", "P p50", "P p99", "P p999", "P max"])
    print(header)
    print("-" * len(header))
    for N in N_list:
        h = result["hawkes"][N]; p = result["poisson"][N]
        print(f"{N:>4} | "
              f"{h['p50']:>10.2f} | {h['p99']:>10.2f} | {h['p999']:>10.2f} | {h['max']:>10.2f} | "
              f"{p['p50']:>10.2f} | {p['p99']:>10.2f} | {p['p999']:>10.2f} | {p['max']:>10.2f}")
    print()
    print("Ratios (relative to N=1):")
    base_h_p99 = result["hawkes"][1]["p99"];  base_p_p99 = result["poisson"][1]["p99"]
    base_h_p50 = result["hawkes"][1]["p50"];  base_p_p50 = result["poisson"][1]["p50"]
    print(f"{'N':>4} | {'H p99/N=1':>10} | {'P p99/N=1':>10} | {'H p50-N=1':>10} | {'P p50-N=1':>10}")
    for N in N_list:
        print(f"{N:>4} | "
              f"{result['hawkes'][N]['p99']/base_h_p99:>10.3f} | "
              f"{result['poisson'][N]['p99']/base_p_p99:>10.3f} | "
              f"{result['hawkes'][N]['p50']-base_h_p50:>+10.3f} | "
              f"{result['poisson'][N]['p50']-base_p_p50:>+10.3f}")


def plot_curves(result: dict, N_list: list[int], out_path: str) -> None:
    import matplotlib.pyplot as plt
    Hp50 = [result["hawkes"][N]["p50"]  for N in N_list]
    Hp99 = [result["hawkes"][N]["p99"]  for N in N_list]
    Pp50 = [result["poisson"][N]["p50"] for N in N_list]
    Pp99 = [result["poisson"][N]["p99"] for N in N_list]

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
    axes[0].plot(N_list, Hp50, "o-", label="Hawkes p50")
    axes[0].plot(N_list, Pp50, "s-", label="Poisson p50")
    axes[0].set_xlabel("N stages"); axes[0].set_ylabel("median latency (us)")
    axes[0].set_title("Median (p50) vs N"); axes[0].legend(); axes[0].grid(alpha=.3)

    axes[1].plot(N_list, Hp99, "o-", label="Hawkes p99")
    axes[1].plot(N_list, Pp99, "s-", label="Poisson p99")
    axes[1].set_xlabel("N stages"); axes[1].set_ylabel("p99 latency (us)")
    axes[1].set_title("Tail (p99) vs N"); axes[1].legend(); axes[1].grid(alpha=.3)

    fig.tight_layout()
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    print(f"wrote {out_path}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lambda-bar", type=float, default=500.0,
                    help="mean packet intensity (packets/sec)")
    ap.add_argument("--n", type=float, default=0.90,
                    help="Hawkes branching ratio (in [0,1))")
    ap.add_argument("--beta", type=float, default=100.0,
                    help="Hawkes decay rate (1/sec)")
    ap.add_argument("--duration", type=float, default=1800.0,
                    help="window duration in seconds (default 30 min)")
    ap.add_argument("--T-us", type=float, default=7.23,
                    help="total decode-service floor T in microseconds")
    ap.add_argument("--h-us", type=float, default=0.09,
                    help="per-stage thread-hop cost h in microseconds")
    ap.add_argument("--N", type=int, nargs="+", default=[1, 2, 4, 8, 12],
                    help="stage counts to sweep")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", default="tandem_demo.png",
                    help="output plot path (PNG)")
    ap.add_argument("--no-plot", action="store_true")
    args = ap.parse_args()

    # Solve mu, alpha from (lambda_bar, n, beta):
    # lambda_bar = mu / (1 - n); alpha = n * beta.
    n = args.n
    alpha = n * args.beta
    mu    = args.lambda_bar * (1.0 - n)
    print(f"Hawkes:  mu={mu:.3f}/s  alpha={alpha:.3f}/s  beta={args.beta:.3f}/s  "
          f"n={n:.3f}  lambda_bar={args.lambda_bar:.1f}/s")
    print(f"Service: T={args.T_us:.3f} us  h={args.h_us:.3f} us  "
          f"N sweep={args.N}  duration={args.duration:.0f} s  seed={args.seed}")

    result = run_sweep(mu, alpha, args.beta, args.duration,
                       args.T_us, args.h_us, args.N, args.seed)
    print_report(result, args.N)
    if not args.no_plot:
        plot_curves(result, args.N, args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
