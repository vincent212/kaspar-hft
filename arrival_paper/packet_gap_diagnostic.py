# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Paper §6.5 — packet inter-arrival diagnostic.

For each 30-min RTH window in each session tape, group SBE messages by
unique sendingTime T_g to get packet arrival times, take diffs to get
packet inter-arrival gaps, and evaluate three fractional-cover
probabilities against physically-motivated thresholds under the
fast_send calibration (floor = 7.23 us, slope = 0.312 us):

    p_floor       = P( gap < floor )
    p_mean_span   = P( gap < floor + slope * (mean_span - 1) )
    p_p95_span    = P( gap < floor + slope * (p95_span  - 1) )

Also emit span statistics and Hawkes fit on packet arrivals so downstream
aggregation into the (lambda_bar_pkt, n_pkt) grid is a single merge.

CLI:
    python3 -m arrival_paper.packet_gap_diagnostic \\
        --tapes-dir /vast/home/vmayeski/out/arrival_paper/tapes/318/message \\
        --out-dir arrival_paper/figs/packet_gap \\
        --jobs 20
"""

from __future__ import annotations

import argparse
import csv
import glob
import multiprocessing as mp
import os
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from arrival_paper.hawkes_smoke import fit_hawkes


DEFAULT_WINDOW_MIN         = 30
MIN_PACKETS_PER_WINDOW     = 500
MIN_MESSAGES_PER_WINDOW    = 1000
CHECKPOINT_EVERY           = 25

# fast_send Table 6 NQ book calibration
FLOOR_NS  = 7_230
SLOPE_NS  = 312


def process_session(msg_tape_csv: str) -> pd.DataFrame:
    """One session tape -> one row per 30-min window."""
    tt: list[int] = []
    st: list[int] = []
    with open(msg_tape_csv, "rt", newline="") as f:
        r = csv.reader(f)
        h = next(r)
        i_tt = h.index("transactTime")
        i_st = h.index("sendingTime")
        for row in r:
            try:
                v_tt = int(row[i_tt])
                v_st = int(row[i_st])
            except (IndexError, ValueError):
                continue
            tt.append(v_tt)
            st.append(v_st)

    tt_arr = np.asarray(tt, dtype=np.int64)
    st_arr = np.asarray(st, dtype=np.int64)
    order  = np.argsort(tt_arr, kind="stable")
    tt_arr = tt_arr[order]
    st_arr = st_arr[order]

    mask = st_arr > 0
    tt_arr = tt_arr[mask]
    st_arr = st_arr[mask]
    if len(tt_arr) == 0:
        return pd.DataFrame()

    win_ns = DEFAULT_WINDOW_MIN * 60 * 1_000_000_000
    first = tt_arr[0]
    win_idx = (tt_arr - first) // win_ns

    rows = []
    session = Path(msg_tape_csv).stem
    for wid in np.unique(win_idx):
        wm = win_idx == wid
        st_w = st_arr[wm]
        n_messages = int(len(st_w))
        if n_messages < MIN_MESSAGES_PER_WINDOW:
            continue

        # Packets = unique sendingTime.
        pkt_start, spans = np.unique(st_w, return_counts=True)
        n_packets = len(pkt_start)
        if n_packets < MIN_PACKETS_PER_WINDOW:
            continue

        pkt_start = np.sort(pkt_start)
        span_mean = float(spans.mean())
        span_p95  = float(np.quantile(spans, 0.95))
        span_max  = int(spans.max())

        # Packet inter-arrival gaps.
        gaps = np.diff(pkt_start)
        thr_floor    = FLOOR_NS
        thr_mean     = FLOOR_NS + SLOPE_NS * (span_mean - 1.0)
        thr_p95      = FLOOR_NS + SLOPE_NS * (span_p95  - 1.0)
        p_floor      = float((gaps < thr_floor).mean())
        p_mean_span  = float((gaps < thr_mean).mean())
        p_p95_span   = float((gaps < thr_p95).mean())

        # Hawkes fit on packet arrivals.
        try:
            fit = fit_hawkes(pkt_start.astype(np.int64))
            lam_pkt = float(fit["lambda_bar"])
            n_pkt   = float(fit["n_branch"])
            converged = bool(fit["converged"])
        except (ValueError, RuntimeError):
            lam_pkt = float("nan")
            n_pkt   = float("nan")
            converged = False

        rows.append({
            "session": session,
            "window_id": int(wid),
            "n_messages": n_messages,
            "n_packets": n_packets,
            "span_mean": span_mean,
            "span_p95": span_p95,
            "span_max": span_max,
            "gap_p50_ns": float(np.quantile(gaps, 0.50)),
            "gap_p05_ns": float(np.quantile(gaps, 0.05)),
            "p_floor": p_floor,
            "p_mean_span": p_mean_span,
            "p_p95_span": p_p95_span,
            "thr_mean_ns": float(thr_mean),
            "thr_p95_ns":  float(thr_p95),
            "lambda_bar_pkt": lam_pkt,
            "n_branch_pkt":   n_pkt,
            "converged": converged,
        })
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


def write_checkpoint(parts, out: Path, grid: int, min_cells: int,
                     tag: str = "[gap]", final: bool = False) -> None:
    """Write partial parquet + heatmaps from parts accumulated so far."""
    if not parts:
        return
    corpus = pd.concat(parts, ignore_index=True)
    out.mkdir(parents=True, exist_ok=True)
    corpus.to_parquet(out / "packet_gap.parquet", compression="snappy", index=False)
    prefix = "FINAL" if final else "CKPT"
    print(f"{tag} {prefix} wrote packet_gap.parquet ({len(corpus):,} rows)",
          file=sys.stderr)

    df = corpus[corpus["converged"]].dropna(subset=["lambda_bar_pkt", "n_branch_pkt"]).copy()
    if len(df) < 4 * min_cells:
        print(f"{tag} {prefix} skip heatmaps: only {len(df)} converged rows",
              file=sys.stderr)
        return
    lam_edges = equal_quantile_bins(df["lambda_bar_pkt"].to_numpy(), grid)
    n_edges   = equal_quantile_bins(df["n_branch_pkt"].to_numpy(),   grid)
    df["yb"] = assign_bin(df["lambda_bar_pkt"].to_numpy(), lam_edges)
    df["xb"] = assign_bin(df["n_branch_pkt"].to_numpy(),   n_edges)
    ky, kx = len(lam_edges) - 1, len(n_edges) - 1

    for stat, label, title in [
        ("p_floor",     "median P(gap < floor)",                  "P(gap < 7.23 us)"),
        ("p_mean_span", "median P(gap < floor + slope*(mean-1))", "P(gap < floor + slope*(mean_span-1))"),
        ("p_p95_span",  "median P(gap < floor + slope*(p95-1))",  "P(gap < floor + slope*(p95_span-1))"),
    ]:
        cells = np.full((ky, kx), np.nan, dtype=np.float64)
        for yb in range(ky):
            for xb in range(kx):
                sub = df[(df["yb"] == yb) & (df["xb"] == xb)]
                if len(sub) >= min_cells:
                    cells[yb, xb] = float(sub[stat].median())
        plot_heatmap(cells, n_edges, lam_edges, title,
                     out / f"grid_{stat}.png", cbar_label=label)

    for stat in ("p_floor", "p_mean_span", "p_p95_span"):
        rho_lam = df[["lambda_bar_pkt", stat]].corr(method="spearman").iloc[0, 1]
        rho_n   = df[["n_branch_pkt",   stat]].corr(method="spearman").iloc[0, 1]
        med     = df[stat].median()
        print(f"{tag} {prefix} median({stat})={med:.4f}  "
              f"spearman(lambda)={rho_lam:+.3f}  spearman(n)={rho_n:+.3f}",
              file=sys.stderr)


def plot_heatmap(cells: np.ndarray, x_edges: np.ndarray, y_edges: np.ndarray,
                 title: str, out_path: Path, cbar_label: str) -> None:
    fig, ax = plt.subplots(figsize=(7.5, 6.0))
    v = cells.astype(float)
    from matplotlib.colors import LogNorm
    finite = v[np.isfinite(v) & (v > 0)]
    norm = LogNorm(vmin=max(1e-6, finite.min()),
                   vmax=finite.max()) if finite.size else None
    im = ax.imshow(v, origin="lower", aspect="auto", cmap="OrRd", norm=norm)
    for iy in range(v.shape[0]):
        for ix in range(v.shape[1]):
            val = v[iy, ix]
            s = "n/a" if not np.isfinite(val) else (f"{val*100:.2f}%" if val < 1 else f"{val:.2f}")
            ax.text(ix, iy, s, ha="center", va="center", fontsize=9)
    ax.set_xticks(range(len(x_edges) - 1))
    ax.set_yticks(range(len(y_edges) - 1))
    ax.set_xticklabels([f"{e:.2g}" for e in x_edges[:-1]], fontsize=8)
    ax.set_yticklabels([f"{e:.2g}" for e in y_edges[:-1]], fontsize=8)
    ax.set_xlabel("n_branch_pkt (quantile bins)")
    ax.set_ylabel("lambda_bar_pkt (pkt/sec, quantile bins)")
    ax.set_title(title)
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label(cbar_label)
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tapes-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--jobs", type=int, default=int(os.cpu_count() or 8))
    ap.add_argument("--grid", type=int, default=5)
    ap.add_argument("--min-cells", type=int, default=10)
    args = ap.parse_args()

    tapes = sorted(glob.glob(str(Path(args.tapes_dir) / "*.csv")))
    print(f"[gap] found {len(tapes)} session tapes; jobs={args.jobs}",
          file=sys.stderr)
    if not tapes:
        return 1

    out = Path(args.out_dir)
    with mp.Pool(args.jobs) as pool:
        parts = []
        for i, df in enumerate(pool.imap_unordered(process_session, tapes)):
            if len(df):
                parts.append(df)
            print(f"[gap] {i+1}/{len(tapes)}  rows={sum(len(p) for p in parts):,}",
                  file=sys.stderr)
            if (i + 1) % CHECKPOINT_EVERY == 0:
                write_checkpoint(parts, out, args.grid, args.min_cells,
                                 tag="[gap]", final=False)
    if not parts:
        return 1
    write_checkpoint(parts, out, args.grid, args.min_cells,
                     tag="[gap]", final=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
