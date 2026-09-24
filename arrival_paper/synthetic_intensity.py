# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Hypothetical feed on which the tail DOES depend on intensity and criticality.

Section 4.6 finds the normalised p99 tail on CME MDP3 invariant to packet
rate and branching ratio at HFT service times. That is a property of that
gateway: it emits packets no closer than ~7.5 us, so at T = 8-32 us only one
or two cluster members fit inside one service time. This script constructs a
feed on which the same simulator shows both dependences at the same T, by
making the intra-cluster spacing much shorter than the service time.

Three conditions, one generator (Hawkes via the cluster representation):

    fast      1/beta = 1 us, no emission floor
    fast+floor same, then a 7.5 us minimum emission spacing is imposed
    poisson   matched-rate Poisson

The dimensionless knob is T*beta. CME at the operating point has T*beta ~ 1;
'fast' has T*beta = 8.

CLI:
    python3 -m arrival_paper.synthetic_intensity
"""
from __future__ import annotations

import sys
import numpy as np
from numba import njit

T_US       = 8.0
H_US       = 1.7
CONDS = [                        # (label, 1/beta in us, emission floor in us or None)
    ("fast kernel, no floor      (1/beta = 1 us)",   1.0,  None),
    ("fast kernel, 7.5 us floor  (1/beta = 1 us)",   1.0,  7.5),
    ("slow kernel, 7.5 us floor  (1/beta = 80 us)", 80.0,  7.5),
]
FLOOR_US   = 7.5
NS         = [0.5, 0.7, 0.8, 0.9, 0.95]
LAMS       = [500, 2000, 8000, 32000, 64000]  # packets per second
N_EVENTS   = 200_000
SEEDS      = 3


def hawkes_cluster(mu_per_us: float, n: float, beta_us: float,
                   n_target: int, rng) -> np.ndarray:
    """Immigrants Poisson(mu); each spawns a GW tree, Poisson(n) offspring at
    Exp(1/beta_us) delays. Returns sorted event times in microseconds."""
    mean_cluster = 1.0 / (1.0 - n)
    W = n_target / (mu_per_us * mean_cluster)          # window, us
    n_imm = rng.poisson(mu_per_us * W)
    frontier = np.sort(rng.uniform(0.0, W, n_imm))
    out = [frontier]
    while frontier.size:
        kids = rng.poisson(n, frontier.size)
        parents = np.repeat(frontier, kids)
        if parents.size == 0:
            break
        frontier = parents + rng.exponential(beta_us, parents.size)
        out.append(frontier)
    t = np.concatenate(out)
    t = t[t < W]
    return np.sort(t)


@njit(cache=True)
def apply_floor(t: np.ndarray, floor: float) -> np.ndarray:
    """Impose a minimum emission spacing: each arrival is delayed until at
    least `floor` after its predecessor. Models a gateway that cannot emit
    packets closer than `floor` apart."""
    out = t.copy()
    for i in range(1, out.shape[0]):
        if out[i] < out[i-1] + floor:
            out[i] = out[i-1] + floor
    return out


@njit(cache=True)
def tandem(t: np.ndarray, N: int, T: float, h: float) -> np.ndarray:
    """N-stage tandem, per-stage service T/N, hop h. Lindley in the form
    wait_i = max(0, dep_{i-1} - arr_i), which keeps arrivals and departures
    in separate variables."""
    per = T / N
    arr = t.copy()
    for stage in range(N):
        if stage > 0:
            arr = arr + h
        dep = np.empty_like(arr)
        prev = -1e300
        for i in range(arr.shape[0]):
            w = prev - arr[i]
            if w < 0.0:
                w = 0.0
            dep[i] = arr[i] + w + per
            prev = dep[i]
        arr = dep
    return arr - t


def p99_over_T(t, N):
    lat = tandem(t, N, T_US, H_US)
    return float(np.quantile(lat, 0.99)) / T_US


def frac_at_floor(t: np.ndarray, floor: float) -> float:
    g = np.diff(t)
    return float((g <= floor * 1.02).mean())


def gen(label_beta_floor, n, lam, seed):
    _, beta, floor = label_beta_floor
    lam_us = lam * 1e-6
    rng = np.random.default_rng(1000 * seed + int(lam) + int(n * 100))
    t = hawkes_cluster(lam_us * (1.0 - n), n, beta, N_EVENTS, rng)
    return apply_floor(t, floor) if floor is not None else t


def main() -> int:
    print(f"T = {T_US} us, h = {H_US} us, {N_EVENTS} events/cell, {SEEDS} seeds, median p99/T\n")
    for cond in CONDS:
        label = cond[0]
        print(f"=== {label} ===")
        print(f"{'n':>5} | " + " ".join(f"lam={l:>5}" for l in LAMS) + "  | gaps<=7.5us")
        for n in NS:
            row, atf = [], []
            for lam in LAMS:
                vals = []
                for seed in range(SEEDS):
                    t = gen(cond, n, lam, seed)
                    vals.append(p99_over_T(t, 1))
                    if lam == 8000:
                        atf.append(frac_at_floor(t, FLOOR_US))
                row.append(np.median(vals))
            print(f"{n:5.2f} | " + " ".join(f"{v:9.2f}" for v in row) +
                  f"  | {100*np.median(atf):5.1f}%")
        print(f"      rho(T=8): " + ", ".join(f"{l*T_US*1e-6:.3f}" for l in LAMS) +
              "   (gap fraction measured at lam=8000; CME corpus: ~1%)\n")

    print("=== Poisson, matched rate ===")
    row = []
    for lam in LAMS:
        vals = []
        for seed in range(SEEDS):
            rng = np.random.default_rng(seed + int(lam))
            t = np.sort(rng.uniform(0.0, N_EVENTS / (lam * 1e-6), N_EVENTS))
            vals.append(p99_over_T(t, 1))
        row.append(np.median(vals))
    print("  --- | " + " ".join(f"{v:9.2f}" for v in row) + "\n")

    print("=== design rule at T=8, n=0.8, lam=8000: p99/T, N=1 vs N=2 ===")
    for cond in CONDS:
        v1, v2 = [], []
        for seed in range(SEEDS):
            t = gen(cond, 0.8, 8000, seed)
            v1.append(p99_over_T(t, 1)); v2.append(p99_over_T(t, 2))
        print(f"  {cond[0]:44s} N=1 {np.median(v1):7.2f}   N=2 {np.median(v2):7.2f}   "
              f"{'split wins' if np.median(v2) < np.median(v1) else 'do not split'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
