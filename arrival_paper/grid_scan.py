# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Grid-scan analysis: bin per-window rows on (λ̄, n) equal-quantile grid,
report each of the five tails per cell.

Consumes every *.parquet in --panel-dir (one file per session, produced by
hourly_panel.py) and produces the paper's centrepiece figures:

Per tail (5 tails):
    1. absolute cell metric — median across the cell's windows of the tail's
       raw p99 (or p95 for markout / return, matching hourly_panel's
       convention). Reported in native units (ns for latencies, price
       units for markout / return proxy).
    2. relative cell metric — median across the cell's windows of
       (p99 / p50) for that tail (or p95/p50 for markout / return),
       i.e. how much heavier the tail is than the median observation.

Plus one "cell mass" heatmap = # windows per cell (should be ~40 for a
5×5 grid on ~1000 windows, with equal-quantile binning giving equal-ish
cell counts).

Output: 11 PNGs in --out-dir, plus grid_summary.parquet with cell-level
values for downstream code review.

CLI:
    python3 -m arrival_paper.grid_scan \\
        --panel-dir /vast/…/tapes/318/panel_s7 \\
        --out-dir arrival_paper/figs/grid_s7 \\
        --grid 5 \\
        [--min-cells 10]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


# Five tails: (col name, p50 col, label, display unit, cell divisor for abs).
# Raw columns are int64 ns; charts display μs (divisor 1000). Markout / return
# columns are already in the paper's chosen price units and pass through.
TAILS = [
    ("p99_lat_me_ns",       "p50_lat_me_ns",       "matching-engine latency",        "μs",    1000.0),
    ("p99_lat_handler_ns",  "p50_lat_handler_ns",  "send-to-handler latency",        "μs",    1000.0),
    ("p99_qsim_ns",         "p50_qsim_ns",         "modelled queue latency (s=7μs)", "μs",    1000.0),
    ("p95_absmark",         "p50_absmark",         "adverse-selection |markout_1s|", "price", 1.0),
    ("p95_absret",          "p50_absret",          "return fat-tail proxy",          "price", 1.0),
]


def load_all_panels(panel_dir: str) -> pd.DataFrame:
    files = sorted(Path(panel_dir).glob("*.parquet"))
    if not files:
        print(f"[grid_scan] no panels in {panel_dir}", file=sys.stderr)
        sys.exit(1)
    dfs = []
    for f in files:
        try:
            d = pd.read_parquet(f)
        except Exception as e:
            print(f"[grid_scan] SKIP {f.name}: {e}", file=sys.stderr)
            continue
        dfs.append(d)
    df = pd.concat(dfs, ignore_index=True)
    print(f"[grid_scan] loaded {len(files)} sessions, "
          f"{len(df):,} rows total ({df['kept'].sum():,} kept)", file=sys.stderr)
    return df


def equal_quantile_bins(x: np.ndarray, k: int) -> np.ndarray:
    """Return length-(k+1) bin edges from equal-quantile split of x
    (ignoring NaNs). Duplicates dropped."""
    x = np.asarray(x, dtype=np.float64)
    valid = x[~np.isnan(x)]
    if len(valid) == 0:
        return np.linspace(0, 1, k + 1)
    qs = np.linspace(0, 1, k + 1)
    edges = np.quantile(valid, qs)
    return np.unique(edges)


def assign_bin(x: np.ndarray, edges: np.ndarray) -> np.ndarray:
    """Return int bin index in [0, len(edges)-2]; NaN → -1."""
    x = np.asarray(x, dtype=np.float64)
    idx = np.searchsorted(edges, x, side="right") - 1
    idx = np.clip(idx, 0, len(edges) - 2)
    idx = np.where(np.isnan(x), -1, idx)
    return idx.astype(np.int64)


def plot_heatmap(cells: np.ndarray, x_edges: np.ndarray, y_edges: np.ndarray,
                 x_label: str, y_label: str, title: str, out_path: str,
                 cbar_label: str, log_norm: bool = False) -> None:
    fig, ax = plt.subplots(figsize=(7.5, 6.0))
    # Cells indexed [y, x] where y = lambda_bar bin (rows), x = n bin (cols).
    # Show y=0 at top so lambda increases downward for reader intuition. Actually
    # matplotlib origin='lower' puts y=0 at bottom which is the usual chart look.
    from matplotlib.colors import LogNorm
    norm = LogNorm(vmin=max(1e-9, np.nanmin(cells[cells > 0])),
                   vmax=np.nanmax(cells)) if log_norm and np.any(cells > 0) else None
    im = ax.imshow(cells, origin="lower", aspect="auto",
                   extent=[0, cells.shape[1], 0, cells.shape[0]],
                   cmap="OrRd", norm=norm)
    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.set_title(title)
    # Annotate each cell with its value
    for iy in range(cells.shape[0]):
        for ix in range(cells.shape[1]):
            v = cells[iy, ix]
            if np.isnan(v):
                s = "n/a"
            elif v >= 100:
                s = f"{v:.0f}"
            elif v >= 1:
                s = f"{v:.1f}"
            else:
                s = f"{v:.2g}"
            ax.text(ix + 0.5, iy + 0.5, s, ha="center", va="center",
                    fontsize=9, color="white" if (log_norm or v < np.nanquantile(cells, 0.5)) else "black")
    # Tick labels showing edge values
    xt = np.arange(cells.shape[1] + 1)
    yt = np.arange(cells.shape[0] + 1)
    ax.set_xticks(xt)
    ax.set_yticks(yt)
    ax.set_xticklabels([f"{e:.2g}" for e in x_edges], fontsize=8, rotation=30)
    ax.set_yticklabels([f"{e:.2g}" for e in y_edges], fontsize=8)
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label(cbar_label)
    fig.tight_layout()
    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--panel-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--grid", type=int, default=5)
    ap.add_argument("--min-cells", type=int, default=10)
    args = ap.parse_args()

    df = load_all_panels(args.panel_dir)
    df = df[df["kept"] & df["converged"]].copy()
    df = df.dropna(subset=["lambda_bar", "n_branch"])
    print(f"[grid_scan] kept+converged rows for grid: {len(df):,}", file=sys.stderr)
    if len(df) == 0:
        print("[grid_scan] no usable rows", file=sys.stderr)
        return 1

    lam = df["lambda_bar"].to_numpy()
    n_br = df["n_branch"].to_numpy()

    lam_edges = equal_quantile_bins(lam, args.grid)
    n_edges = equal_quantile_bins(n_br, args.grid)
    df["y_bin"] = assign_bin(lam, lam_edges)
    df["x_bin"] = assign_bin(n_br, n_edges)

    ky = len(lam_edges) - 1
    kx = len(n_edges) - 1
    print(f"[grid_scan] grid shape: lambda={ky} × n={kx}", file=sys.stderr)

    # Cell mass
    mass = np.zeros((ky, kx), dtype=np.int64)
    for _, r in df.iterrows():
        yb, xb = int(r["y_bin"]), int(r["x_bin"])
        if yb < 0 or xb < 0: continue
        mass[yb, xb] += 1

    Path(args.out_dir).mkdir(parents=True, exist_ok=True)
    plot_heatmap(mass.astype(float), n_edges, lam_edges,
                 x_label="Hawkes branching ratio n (quantile bins)",
                 y_label="Mean intensity λ̄ (evt/s, quantile bins)",
                 title=f"Grid mass — # windows per cell (total {mass.sum():,})",
                 out_path=str(Path(args.out_dir) / "00_mass.png"),
                 cbar_label="# windows", log_norm=False)

    summary_rows = []
    for p99col, p50col, label, unit, divisor in TAILS:
        for kind in ("abs", "rel"):
            cells = np.full((ky, kx), np.nan, dtype=np.float64)
            for yb in range(ky):
                for xb in range(kx):
                    sub = df[(df["y_bin"] == yb) & (df["x_bin"] == xb)]
                    sub = sub.dropna(subset=[p99col, p50col])
                    if len(sub) < args.min_cells:
                        continue
                    if kind == "abs":
                        # Convert to display unit (μs for latency tails).
                        cells[yb, xb] = float(sub[p99col].median()) / divisor
                    else:
                        # Relative = median of (p99/p50) across cell.
                        # Avoid divide-by-zero. Divisor cancels.
                        r = sub[p99col] / sub[p50col].replace(0, np.nan)
                        cells[yb, xb] = float(r.median())
            title = (f"{label} — median cell p99 ({unit})" if kind == "abs"
                     else f"{label} — median cell p99/p50 ratio")
            slug = p99col.replace("p99_", "").replace("_ns", "").replace("p95_", "")
            out = Path(args.out_dir) / f"tail_{slug}_{kind}.png"
            plot_heatmap(cells, n_edges, lam_edges,
                         x_label="Hawkes branching ratio n (quantile bins)",
                         y_label="Mean intensity λ̄ (evt/s, quantile bins)",
                         title=title,
                         out_path=str(out),
                         cbar_label=f"median {unit}" if kind == "abs" else "median p99/p50",
                         log_norm=(kind == "abs" and unit != "price"))
            for yb in range(ky):
                for xb in range(kx):
                    summary_rows.append({
                        "tail": label, "kind": kind,
                        "y_bin_lambda": yb, "x_bin_n": xb,
                        "n_windows": int(mass[yb, xb]),
                        "cell_value": cells[yb, xb],
                    })
            print(f"[grid_scan] wrote {out.name}", file=sys.stderr)

    summary = pd.DataFrame(summary_rows)
    summary.to_parquet(str(Path(args.out_dir) / "grid_summary.parquet"),
                        compression="snappy", index=False)
    print(f"[grid_scan] wrote grid_summary.parquet ({len(summary):,} rows)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
