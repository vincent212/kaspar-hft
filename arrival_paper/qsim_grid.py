# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Per-window packet-arrival Lindley + packet-Hawkes fit, gridded on (lambda_bar_pkt, n_pkt).

For each 30-min RTH window in each session:
    1. Group messages by packet_seq; arrival = min(transactTime) per packet.
    2. Fit exp-Hawkes on the packet-arrival stream -> (lambda_bar_pkt, n_pkt).
    3. Run G/D/1 Lindley on packet arrivals with constant service T = 7.23 us.
    4. Record p50, p95, p99, p99.9, max of end-to-end latency (wait + service),
       utilisation rho, and p99/p50 ratio.

Corpus output: one row per (session, window), plus 5x5 equal-quantile heatmaps
of median cell qsim_p50, qsim_p99, and qsim_p99/p50 ratio, checkpointed every
25 sessions.

CLI:
    python3 -m arrival_paper.qsim_grid \\
        --tapes-dir /vast/home/vmayeski/out/arrival_paper/tapes/318/message \\
        --out-dir arrival_paper/figs/qsim_grid \\
        --jobs 20
"""
from __future__ import annotations

import argparse
import csv
import glob
import multiprocessing as mp
import os
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from arrival_paper.hawkes_smoke import fit_hawkes


DEFAULT_WINDOW_MIN      = 30
MIN_PACKETS_PER_WINDOW  = 500
MIN_MESSAGES_PER_WINDOW = 1000
CHECKPOINT_EVERY        = 25

FLOOR_NS = 7_230       # legacy fast_send-calibration floor (no longer used as main scenario)
HOP_NS   = 90          # legacy per-stage hop (no longer used as main scenario)
N_STAGES = [1, 2, 4, 8]

# (label, total_service_ns, hop_ns, N_list) scenarios for the arrival paper's
# canonical sweep. h = 1_700 ns is the one-way async cross-thread hop cost
# derived by halving the ~3.4 us round-trip figure in Table 4 of the fast_send
# paper. T spans the regime from sub-hop (2 us; below the 2h floor, included
# to demonstrate that the design equation prescribes N*=1 there) through the
# edge case at 4 us and up to well above the floor at 32 us.
SCENARIOS = [
    ("T2_h1p7us",  2_000,  1_700, [1, 2, 4, 8]),
    ("T4_h1p7us",  4_000,  1_700, [1, 2, 4, 8]),
    ("T8_h1p7us",  8_000,  1_700, [1, 2, 4, 8]),
    ("T16_h1p7us", 16_000, 1_700, [1, 2, 4, 8]),
    ("T32_h1p7us", 32_000, 1_700, [1, 2, 4, 8]),
]


def lindley(arr_ns: np.ndarray, service_ns: int) -> np.ndarray:
    """Constant-service Lindley recursion. Returns latency = wait + service."""
    n = len(arr_ns)
    wait = np.zeros(n, dtype=np.int64)
    for i in range(1, n):
        w = int(wait[i - 1]) + service_ns - int(arr_ns[i] - arr_ns[i - 1])
        wait[i] = w if w > 0 else 0
    return wait + service_ns


def tandem_lindley(arr_ns: np.ndarray, N: int, total_service_ns: int,
                   hop_ns: int) -> np.ndarray:
    """N-stage tandem Lindley. Per-stage service = total/N; per-hop delay = hop_ns.

    Returns per-event end-to-end latency (in ns) = last-stage-departure - arrival.
    """
    # Round to nearest ns rather than truncate. `//` would lose up to N-1 ns
    # per event when T is not divisible by N, which shows up as a 0.01-us
    # deviation from the p50 identity T + (N-1)h. The current SCENARIOS grid
    # picks T divisible by every N in use, so this is a defensive invariant
    # against future scenario additions rather than a correction for anything
    # in the current sweep.
    per_stage_service = int(round(total_service_ns / N))
    dep = arr_ns.copy()
    for stage in range(N):
        if stage > 0:
            dep = dep + hop_ns
        wait = lindley(dep, per_stage_service)   # wait includes service
        dep = dep + wait                          # departure from this stage
    return dep - arr_ns


def process_session(msg_tape_csv: str) -> pd.DataFrame:
    """One session tape -> one row per 30-min window."""
    tt: list[int] = []
    seq: list[int] = []
    with open(msg_tape_csv, "rt", newline="") as f:
        r = csv.reader(f)
        h = next(r)
        i_tt  = h.index("transactTime")
        i_seq = h.index("packet_seq")
        for row in r:
            try:
                v_tt  = int(row[i_tt])
                v_seq = int(row[i_seq])
            except (IndexError, ValueError):
                continue
            tt.append(v_tt)
            seq.append(v_seq)

    tt_arr  = np.asarray(tt,  dtype=np.int64)
    seq_arr = np.asarray(seq, dtype=np.int64)
    order   = np.argsort(tt_arr, kind="stable")
    tt_arr  = tt_arr[order]
    seq_arr = seq_arr[order]
    if len(tt_arr) == 0:
        return pd.DataFrame()

    win_ns  = DEFAULT_WINDOW_MIN * 60 * 1_000_000_000
    first   = tt_arr[0]
    win_idx = (tt_arr - first) // win_ns

    session = Path(msg_tape_csv).stem
    rows = []
    for wid in np.unique(win_idx):
        wm = win_idx == wid
        tt_w  = tt_arr[wm]
        seq_w = seq_arr[wm]
        n_messages = int(len(tt_w))
        if n_messages < MIN_MESSAGES_PER_WINDOW:
            continue

        # Packet grouping via packet_seq: arrival = min(transactTime) per packet.
        seq_to_min = defaultdict(lambda: np.iinfo(np.int64).max)
        seq_to_cnt = defaultdict(int)
        for t, s in zip(tt_w, seq_w):
            if t < seq_to_min[s]:
                seq_to_min[s] = t
            seq_to_cnt[s] += 1
        n_packets = len(seq_to_min)
        if n_packets < MIN_PACKETS_PER_WINDOW:
            continue

        packets = np.array(sorted(seq_to_min.items()), dtype=object)
        pkt_arr = np.array([v for _, v in sorted(seq_to_min.items())], dtype=np.int64)
        pkt_arr.sort()  # by arrival
        spans   = np.array([seq_to_cnt[s] for s in sorted(seq_to_min.keys())],
                           dtype=np.int64)
        # NB: spans ordering must match pkt_arr - reconstruct via arrival-sort
        # simpler: build list of (arrival, span), sort, split
        pairs = sorted((seq_to_min[s], seq_to_cnt[s]) for s in seq_to_min)
        pkt_arr = np.array([p[0] for p in pairs], dtype=np.int64)
        spans   = np.array([p[1] for p in pairs], dtype=np.int64)

        window_span_ns = int(pkt_arr[-1] - pkt_arr[0])
        rho = float(n_packets * FLOOR_NS) / max(1, window_span_ns)

        # Packet-Hawkes fit on packet arrivals.
        try:
            fit = fit_hawkes(pkt_arr)
            lam_pkt   = float(fit["lambda_bar"])
            n_pkt     = float(fit["n_branch"])
            converged = bool(fit["converged"])
        except (ValueError, RuntimeError):
            lam_pkt = float("nan"); n_pkt = float("nan"); converged = False

        # Poisson null: uniform-shuffle same arrival count over the window.
        # Deterministic per (session, window_id) via a stable seed.
        seed = abs(hash((session, int(wid)))) % (2**32)
        rng = np.random.default_rng(seed)
        poi_arr = np.sort(
            rng.integers(low=int(pkt_arr[0]), high=int(pkt_arr[-1]) + 1,
                         size=n_packets)
        ).astype(np.int64)

        row = {
            "session": session,
            "window_id": int(wid),
            "n_messages": n_messages,
            "n_packets":  n_packets,
            "span_mean":  float(spans.mean()),
            "span_max":   int(spans.max()),
            "lambda_bar_pkt": lam_pkt,
            "n_branch_pkt":   n_pkt,
            "converged":  converged,
            "rho":        rho,
        }

        # N-stage tandem across each scenario, Hawkes and Poisson-null.
        for label, T_ns, h_ns, Ns in SCENARIOS:
            for N in Ns:
                for regime, arrivals in (("H", pkt_arr), ("P", poi_arr)):
                    lat = tandem_lindley(arrivals, N,
                                         total_service_ns=T_ns,
                                         hop_ns=h_ns)
                    p50  = float(np.quantile(lat, 0.5))
                    p95  = float(np.quantile(lat, 0.95))
                    p99  = float(np.quantile(lat, 0.99))
                    p999 = float(np.quantile(lat, 0.999))
                    mx   = float(lat.max())
                    row[f"{label}_{regime}_N{N}_p50_us"]  = p50 / 1e3
                    row[f"{label}_{regime}_N{N}_p95_us"]  = p95 / 1e3
                    row[f"{label}_{regime}_N{N}_p99_us"]  = p99 / 1e3
                    row[f"{label}_{regime}_N{N}_p999_us"] = p999 / 1e3
                    row[f"{label}_{regime}_N{N}_max_us"]  = mx / 1e3
        rows.append(row)
    return pd.DataFrame(rows)


def equal_quantile_bins(x: np.ndarray, k: int) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    valid = x[~np.isnan(x)]
    if len(valid) == 0:
        return np.linspace(0, 1, k + 1)
    qs = np.linspace(0, 1, k + 1)
    return np.unique(np.quantile(valid, qs))


def assign_bin(x: np.ndarray, edges: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    idx = np.searchsorted(edges, x, side="right") - 1
    idx = np.clip(idx, 0, len(edges) - 2)
    idx = np.where(np.isnan(x), -1, idx)
    return idx.astype(np.int64)


def plot_heatmap(cells: np.ndarray, x_edges: np.ndarray, y_edges: np.ndarray,
                 title: str, out_path: Path, cbar_label: str,
                 fmt: str = ".2f") -> None:
    from matplotlib.colors import LogNorm
    fig, ax = plt.subplots(figsize=(7.5, 6.0))
    v = cells.astype(float)
    finite = v[np.isfinite(v) & (v > 0)]
    norm = LogNorm(vmin=max(1e-6, finite.min()),
                   vmax=finite.max()) if finite.size else None
    im = ax.imshow(v, origin="lower", aspect="auto", cmap="OrRd", norm=norm)
    for iy in range(v.shape[0]):
        for ix in range(v.shape[1]):
            val = v[iy, ix]
            s = "n/a" if not np.isfinite(val) else format(val, fmt)
            ax.text(ix, iy, s, ha="center", va="center", fontsize=9)
    ax.set_xticks(range(len(x_edges) - 1))
    ax.set_yticks(range(len(y_edges) - 1))
    ax.set_xticklabels([f"{e:.2g}" for e in x_edges[:-1]], fontsize=8)
    ax.set_yticklabels([f"{e:.2g}" for e in y_edges[:-1]], fontsize=8)
    ax.set_xlabel("n_branch_pkt (packet Hawkes branching)")
    ax.set_ylabel("lambda_bar_pkt (packets/sec)")
    ax.set_title(title)
    cbar = fig.colorbar(im, ax=ax); cbar.set_label(cbar_label)
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)


def write_checkpoint(parts, out: Path, grid: int, min_cells: int,
                     tag: str = "[qsim]", final: bool = False) -> None:
    if not parts:
        return
    corpus = pd.concat(parts, ignore_index=True)
    out.mkdir(parents=True, exist_ok=True)
    corpus.to_parquet(out / "qsim_grid.parquet", compression="snappy", index=False)
    prefix = "FINAL" if final else "CKPT"
    print(f"{tag} {prefix} wrote qsim_grid.parquet ({len(corpus):,} rows)",
          file=sys.stderr)

    df = corpus[corpus["converged"]].dropna(
        subset=["lambda_bar_pkt", "n_branch_pkt"]).copy()
    if len(df) < 4 * min_cells:
        print(f"{tag} {prefix} skip heatmaps: only {len(df)} rows",
              file=sys.stderr)
        return
    lam_edges = equal_quantile_bins(df["lambda_bar_pkt"].to_numpy(), grid)
    n_edges   = equal_quantile_bins(df["n_branch_pkt"].to_numpy(),   grid)
    df["yb"] = assign_bin(df["lambda_bar_pkt"].to_numpy(), lam_edges)
    df["xb"] = assign_bin(df["n_branch_pkt"].to_numpy(),   n_edges)
    ky, kx = len(lam_edges) - 1, len(n_edges) - 1

    # T = 8 us is the paper's canonical "representative" scenario: safely above
    # the 2h floor and squarely in the regime where the design equation
    # reliably applies. T = 2 us (SCENARIOS[0]) is the below-floor demo case
    # and would produce uninformative heatmaps if used here.
    primary_label = "T8_h1p7us"
    heatmap_stats = [
        (f"{primary_label}_H_N1_p99_us", "median cell p99 (us)",
                                  f"{primary_label} Hawkes p99, N=1", ".1f"),
        (f"{primary_label}_H_N{N_STAGES[-1]}_p99_us", "median cell p99 (us)",
                                  f"{primary_label} Hawkes p99, N={N_STAGES[-1]}", ".1f"),
        ("rho", "median cell utilisation rho",
                                  "utilisation (rho = pkt_rate * service)", ".3f"),
    ]
    for stat, label, title, fmt in heatmap_stats:
        cells = np.full((ky, kx), np.nan, dtype=np.float64)
        for yb in range(ky):
            for xb in range(kx):
                sub = df[(df["yb"] == yb) & (df["xb"] == xb)]
                if len(sub) >= min_cells:
                    cells[yb, xb] = float(sub[stat].median())
        plot_heatmap(cells, n_edges, lam_edges, title,
                     out / f"grid_{stat}.png", cbar_label=label, fmt=fmt)

    # Cell mass.
    mass = np.zeros((ky, kx), dtype=np.int64)
    for yb in range(ky):
        for xb in range(kx):
            mass[yb, xb] = len(df[(df["yb"] == yb) & (df["xb"] == xb)])
    plot_heatmap(mass.astype(float), n_edges, lam_edges,
                 f"Cell mass ({mass.sum():,} windows)",
                 out / "grid_mass.png", cbar_label="# windows", fmt=".0f")

    # Per-scenario curves.
    all_curves = []
    for label, T_ns, h_ns, Ns in SCENARIOS:
        for N in Ns:
            row = {"scenario": label, "T_us": T_ns/1e3, "h_us": h_ns/1e3, "N": N}
            for stat in ("p50", "p99"):
                for regime in ("H", "P"):
                    col = f"{label}_{regime}_N{N}_{stat}_us"
                    if col in df.columns:
                        row[f"{regime}_{stat}_us"] = df[col].median()
            all_curves.append(row)
    curves = pd.DataFrame(all_curves)
    curves.to_csv(out / "tandem_curves.csv", index=False)

    for label, _, _, _ in SCENARIOS:
        sub = curves[curves["scenario"] == label]
        fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))
        axes[0].plot(sub["N"], sub["H_p50_us"], "o-", label="Hawkes p50")
        axes[0].plot(sub["N"], sub["P_p50_us"], "s-", label="Poisson p50")
        axes[0].set_xlabel("N stages"); axes[0].set_ylabel("median latency (us)")
        axes[0].set_title(f"{label} — median vs N"); axes[0].legend(); axes[0].grid(alpha=.3)
        axes[1].plot(sub["N"], sub["H_p99_us"], "o-", label="Hawkes p99")
        axes[1].plot(sub["N"], sub["P_p99_us"], "s-", label="Poisson p99")
        axes[1].set_xlabel("N stages"); axes[1].set_ylabel("p99 latency (us)")
        axes[1].set_title(f"{label} — p99 vs N"); axes[1].legend(); axes[1].grid(alpha=.3)
        fig.tight_layout()
        fig.savefig(out / f"tandem_curves_{label}.png", dpi=140, bbox_inches="tight")
        plt.close(fig)

    print(f"{tag} {prefix} tandem curves (all scenarios):", file=sys.stderr)
    print(curves.to_string(index=False, float_format=lambda x: f"{x:.2f}"),
          file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tapes-dir", required=True)
    ap.add_argument("--out-dir",   required=True)
    ap.add_argument("--jobs", type=int, default=int(os.cpu_count() or 8))
    ap.add_argument("--grid", type=int, default=5)
    ap.add_argument("--min-cells", type=int, default=10)
    args = ap.parse_args()

    tapes = sorted(glob.glob(str(Path(args.tapes_dir) / "*.csv")))
    print(f"[qsim] found {len(tapes)} tapes; jobs={args.jobs}", file=sys.stderr)
    if not tapes:
        return 1

    out = Path(args.out_dir)
    with mp.Pool(args.jobs) as pool:
        parts = []
        for i, df in enumerate(pool.imap_unordered(process_session, tapes)):
            if len(df):
                parts.append(df)
            print(f"[qsim] {i+1}/{len(tapes)}  rows={sum(len(p) for p in parts):,}",
                  file=sys.stderr)
            if (i + 1) % CHECKPOINT_EVERY == 0:
                write_checkpoint(parts, out, args.grid, args.min_cells,
                                 tag="[qsim]", final=False)
    if not parts:
        return 1
    write_checkpoint(parts, out, args.grid, args.min_cells,
                     tag="[qsim]", final=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
