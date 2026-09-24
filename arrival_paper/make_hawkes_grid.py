# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Generate the 3x3 Hawkes visualisation grid described in methodology.md §1.6a.

Simulates an exponential Hawkes on a 60-second window at each of nine
(mean intensity, branching ratio) combinations and plots three rows:

    row 1 — event raster (every arrival as a vertical tick)
    row 2 — cumulative count N(t)
    row 3 — analytic intensity trace lambda(t)

Rows are indexed by mean intensity in {2, 10, 50} events/s and columns by
branching ratio in {0.30, 0.60, 0.90}. Each cell is a stack of three
subplots so the raster, count curve, and intensity trace line up in time.

Output: arrival_paper/figs/hawkes_grid_3x3.png
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt


def simulate_hawkes_ogata(mu: float, alpha: float, beta: float,
                           T: float, seed: int) -> np.ndarray:
    """Ogata thinning simulation of exp-Hawkes on [0, T]."""
    rng = np.random.default_rng(seed)
    events: list[float] = []
    t = 0.0
    lam_bar = mu
    while True:
        w = -np.log(rng.uniform()) / lam_bar
        t = t + w
        if t >= T:
            break
        if events:
            e = np.asarray(events)
            lam_t = mu + alpha * np.sum(np.exp(-beta * (t - e)))
        else:
            lam_t = mu
        if rng.uniform() * lam_bar <= lam_t:
            events.append(t)
            lam_bar = lam_t + alpha
        else:
            lam_bar = lam_t
    return np.asarray(events)


def lambda_trace(events: np.ndarray, mu: float, alpha: float, beta: float,
                 grid: np.ndarray) -> np.ndarray:
    """Analytic lambda(t) on grid points, given the event history."""
    lam = np.full(grid.shape, mu, dtype=np.float64)
    if len(events) == 0:
        return lam
    for j, t in enumerate(grid):
        past = events[events < t]
        if len(past):
            lam[j] += alpha * np.sum(np.exp(-beta * (t - past)))
    return lam


def params_for_cell(lambda_bar: float, n: float, beta: float = 1.0) -> tuple[float, float, float]:
    """Convert (lambda_bar, n) → (mu, alpha, beta). Fix beta=1 so decay
    is 1 s across all cells and the ONLY things that change are mu and n."""
    alpha = n * beta
    mu = lambda_bar * (1.0 - n)
    return mu, alpha, beta


def main() -> None:
    T = 15.0                                  # 15 seconds — long enough to show clusters
                                              #             short enough that even λ̄=50/s
                                              #             renders individually resolvable
                                              #             ticks in the raster
    grid = np.linspace(0.0, T, 3000)          # 5 ms grid for lambda(t)
    lambda_bars = [2.0, 10.0, 50.0]
    ns = [0.30, 0.60, 0.90]

    n_rows = len(lambda_bars)
    n_cols = len(ns)
    fig, axes = plt.subplots(
        n_rows * 3, n_cols,
        figsize=(4 * n_cols, 2 * n_rows * 3),
        sharex=True,
        gridspec_kw={"hspace": 0.15, "wspace": 0.25},
    )

    palette = {"raster": "#1f77b4", "count": "#ff7f0e", "lambda": "#2ca02c"}

    for i, lam_bar in enumerate(lambda_bars):
        for j, n in enumerate(ns):
            mu, alpha, beta = params_for_cell(lam_bar, n)
            events = simulate_hawkes_ogata(mu, alpha, beta, T, seed=42 + 10*i + j)
            lam_t = lambda_trace(events, mu, alpha, beta, grid)

            ax_r = axes[3*i,     j]
            ax_c = axes[3*i + 1, j]
            ax_l = axes[3*i + 2, j]

            # Thin the raster ticks in proportion to density so high-intensity
            # cells stay visually resolvable rather than blurring into a bar.
            n_events = len(events)
            lw = max(0.15, min(0.8, 40.0 / max(n_events, 1)))
            ax_r.vlines(events, 0, 1, colors=palette["raster"],
                        linewidths=lw, alpha=0.7)
            ax_r.set_yticks([])
            ax_r.set_ylim(0, 1)

            n_cum = np.searchsorted(events, grid, side="right")
            ax_c.step(grid, n_cum, where="post", color=palette["count"], linewidth=1.0)
            ax_c.set_ylabel("N(t)", fontsize=8)

            ax_l.plot(grid, lam_t, color=palette["lambda"], linewidth=0.9)
            ax_l.set_ylabel(r"$\lambda(t)$" + "\n(evt/s)", fontsize=8)

            if i == 0:
                ax_r.set_title(rf"$n = {n:.2f}$", fontsize=10, pad=12)
            if j == 0:
                ax_r.text(-0.18, 0.5, rf"$\bar\lambda = {lam_bar:.0f}$/s",
                          transform=ax_r.transAxes, rotation=90,
                          va="center", ha="center", fontsize=10, weight="bold")

            for ax in (ax_r, ax_c, ax_l):
                ax.tick_params(labelsize=7)
                ax.grid(True, linewidth=0.3, alpha=0.4)

            ax_r.set_xlim(0, T)
            ax_l.set_xlabel("time (s)" if i == n_rows - 1 else "", fontsize=8)

    fig.suptitle(
        "Hawkes process across mean intensity $\\bar\\lambda$ and branching ratio $n$\n"
        "each cell: event raster (top), cumulative count N(t) (middle), analytic $\\lambda(t)$ (bottom); "
        f"T = {int(T)} s, $\\beta = 1$/s (kernel decay time = 1 s)",
        fontsize=11, y=0.995,
    )
    plt.tight_layout(rect=[0, 0, 1, 0.985])

    out = Path(__file__).parent / "figs" / "hawkes_grid_3x3.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
