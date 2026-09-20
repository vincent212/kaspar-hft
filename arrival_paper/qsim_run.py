# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Stage 2 of the two-stage sim pipeline: read cache, run tandem Lindley grid.

Consumes the cache produced by `qsim_prep.py` (metadata.parquet + arrivals/*.npz)
and runs the (T, N, regime) tandem Lindley grid using numba-njit'd recursions.
Full-corpus scenario sweeps take seconds instead of hours, so re-runs across
new (T, h, N) grids or sensitivity variants are essentially free.

CLI:
    python3 -m arrival_paper.qsim_run \\
        --cache-dir arrival_paper/figs/qsim_cache \\
        --out-dir   arrival_paper/figs/qsim_grid
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import numpy as np
import pandas as pd
from numba import njit


# ---------------------------------------------------------------------------
# Scenario grid  (label, total_service_ns, hop_ns, N_list)
# h = 1_700 ns is the one-way async cross-thread hop cost derived by halving
# the ~3.4 us round-trip in Table 4 of the fast_send paper. T spans sub-hop
# through slow-servicing regime; T=2 sits below the 2h floor as the empirical
# demonstration of the "cannot be split" boundary.
# ---------------------------------------------------------------------------
SCENARIOS: list[tuple[str, int, int, list[int]]] = [
    ("T2_h1p7us",  2_000,  1_700, [1, 2, 4, 8]),
    ("T4_h1p7us",  4_000,  1_700, [1, 2, 4, 8]),
    ("T8_h1p7us",  8_000,  1_700, [1, 2, 4, 8]),
    ("T16_h1p7us", 16_000, 1_700, [1, 2, 4, 8]),
    ("T32_h1p7us", 32_000, 1_700, [1, 2, 4, 8]),
]


# ---------------------------------------------------------------------------
# Numba-jit'd Lindley recursions. All arithmetic is int64 nanoseconds.
# ---------------------------------------------------------------------------

@njit(cache=True)
def lindley(arr_ns: np.ndarray, service_ns: int) -> np.ndarray:
    """Single-server FIFO Lindley with deterministic service.
    Returns per-event latency = wait + service, in ns."""
    n = arr_ns.shape[0]
    out = np.empty(n, dtype=np.int64)
    w_prev = np.int64(0)
    out[0] = service_ns
    for i in range(1, n):
        w = w_prev + service_ns - (arr_ns[i] - arr_ns[i - 1])
        if w < 0:
            w = np.int64(0)
        out[i] = w + service_ns
        w_prev = w
    return out


@njit(cache=True)
def tandem_lindley(arr_ns: np.ndarray, N: int, total_service_ns: int,
                   hop_ns: int) -> np.ndarray:
    """N-stage series tandem. Per-stage service = total/N (rounded to nearest
    integer nanosecond), per-hop delay = hop_ns. Returns per-event end-to-end
    latency in ns."""
    per_stage_service = np.int64(round(total_service_ns / N))
    dep = arr_ns.copy()
    for stage in range(N):
        if stage > 0:
            dep = dep + hop_ns
        lat = lindley(dep, per_stage_service)
        dep = dep + lat                            # departure = arrival + (wait + service)
    return dep - arr_ns


def quantiles_us(lat_ns: np.ndarray) -> tuple[float, float, float, float, float]:
    """Return (p50, p95, p99, p999, max) in microseconds."""
    return (
        float(np.quantile(lat_ns, 0.50))  / 1e3,
        float(np.quantile(lat_ns, 0.95))  / 1e3,
        float(np.quantile(lat_ns, 0.99))  / 1e3,
        float(np.quantile(lat_ns, 0.999)) / 1e3,
        float(lat_ns.max())               / 1e3,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cache-dir", required=True,
                    help="qsim_prep.py output: contains metadata.parquet and "
                         "arrivals/{session}.npz")
    ap.add_argument("--out-dir", required=True,
                    help="where to write qsim_grid.parquet with the full grid")
    args = ap.parse_args()

    cache = Path(args.cache_dir)
    out   = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    meta_path = cache / "metadata.parquet"
    if not meta_path.exists():
        print(f"[run] {meta_path} not found; run qsim_prep.py first",
              file=sys.stderr)
        return 1
    meta = pd.read_parquet(meta_path)
    print(f"[run] loaded metadata: {len(meta)} windows across "
          f"{meta['session'].nunique()} sessions", file=sys.stderr)

    # Warm up numba JIT on a tiny array so timing reflects steady-state.
    _ = tandem_lindley(np.array([0, 100, 200], dtype=np.int64),
                       2, 100, 10)

    rows: list[dict] = []
    sessions = list(meta["session"].unique())
    t0 = time.time()
    for si, session in enumerate(sessions):
        npz_path = cache / "arrivals" / f"{session}.npz"
        if not npz_path.exists():
            print(f"[run] missing {npz_path}, skipping", file=sys.stderr)
            continue
        with np.load(npz_path) as npz:
            window_rows = meta[meta["session"] == session]
            for _, wrow in window_rows.iterrows():
                wid = int(wrow["window_id"])
                key_H = f"w{wid}_H"
                key_P = f"w{wid}_P"
                if key_H not in npz.files or key_P not in npz.files:
                    continue
                arr_H = npz[key_H]
                arr_P = npz[key_P]

                row = wrow.to_dict()
                for label, T_ns, h_ns, Ns in SCENARIOS:
                    for regime, arrivals in (("H", arr_H), ("P", arr_P)):
                        wait1 = None
                        for N in sorted(set(Ns) | {1}):
                            lat = tandem_lindley(
                                arrivals, N,
                                total_service_ns=T_ns, hop_ns=h_ns)
                            # Pathwise bound (paper Thm 2): W(N) <= W(1)/N + (N-1)h,
                            # W = latency - T. Tolerance N ns covers round(T/N).
                            wait = lat - T_ns
                            if N == 1:
                                wait1 = wait
                            excess = wait - (wait1 / N + (N - 1) * h_ns) - N
                            row[f"{label}_{regime}_N{N}_bound_viol"] = int((excess > 0).sum())
                            row[f"{label}_{regime}_N{N}_bound_viol_max_us"] = max(float(excess.max()), 0.0) / 1e3
                            if N not in Ns:
                                continue
                            p50, p95, p99, p999, mx = quantiles_us(lat)
                            row[f"{label}_{regime}_N{N}_p50_us"]  = p50
                            row[f"{label}_{regime}_N{N}_p95_us"]  = p95
                            row[f"{label}_{regime}_N{N}_p99_us"]  = p99
                            row[f"{label}_{regime}_N{N}_p999_us"] = p999
                            row[f"{label}_{regime}_N{N}_max_us"]  = mx
                rows.append(row)
        if (si + 1) % 25 == 0 or (si + 1) == len(sessions):
            elapsed = time.time() - t0
            print(f"[run] {si+1}/{len(sessions)} sessions, "
                  f"{len(rows)} window rows, {elapsed:.1f}s elapsed",
                  file=sys.stderr)

    df = pd.DataFrame(rows)
    out_path = out / "qsim_grid.parquet"
    df.to_parquet(out_path, compression="snappy", index=False)
    print(f"[run] wrote {out_path}: {len(df)} rows, {len(df.columns)} cols "
          f"in {time.time() - t0:.1f}s", file=sys.stderr)

    viol_cols = [c for c in df.columns if c.endswith("_bound_viol")]
    n_viol_msgs = int(df[viol_cols].sum().sum())
    n_viol_windows = int((df[viol_cols] > 0).any(axis=1).sum())
    print(f"[run] Thm 2 bound check: {n_viol_msgs} violating messages in "
          f"{n_viol_windows}/{len(df)} windows (expected 0)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
