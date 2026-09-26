# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Regenerate the paper's tandem-curves figure from the qsim_run.py grid output.

Produces `figs/tandem_curves.pdf`, the three-panel figure of Section 4.1:

  * left   -- corpus-median p50 vs N, showing the exact (N-1)h hop tax;
  * centre -- corpus-median p99 vs N, Hawkes solid and Poisson null dashed;
  * right  -- the Hawkes tail-excess ratio Delta(N)/Delta(1) against the
              Theorem 2 bound 1/N.

One shade per service floor T, on a single sequential ramp (dark = slow stage),
with each curve direct-labelled at its right end so the figure carries no
colour-only encoding. Previously this PDF existed in the repo with no script
behind it, so a rerun of the sweep could not regenerate it; that is why the
figure silently went stale against the sendingTime rerun.

CLI:
    python3 -m arrival_paper.make_figure \\
        --grid arrival_paper/figs/qsim_grid/qsim_grid.parquet \\
        --out  arrival_paper/figs/tandem_curves.pdf
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

TS = [2, 4, 8, 16, 32, 64, 128]
NS = [1, 2, 4, 8]
H = 1.7
FLOOR = 8e-4          # y-position used to draw exact zeros on the log panel


def med(df: pd.DataFrame, T: int, regime: str, n: int, q: str) -> float:
    c = f"T{T}_h1p7us_{regime}_N{n}_{q}_us"
    return float(df[c].median()) if c in df.columns else float("nan")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--grid", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    p = Path(args.grid)
    if not p.exists():
        print(f"[fig] {p} not found; run qsim_run.py first", file=sys.stderr)
        return 1
    df = pd.read_parquet(p)
    print(f"[fig] {len(df)} windows across "
          f"{df['session'].nunique()} sessions", file=sys.stderr)

    # Sequential single-hue ramp: light = short stage, dark = long stage.
    # Magnitude data gets one hue, never a categorical rainbow.
    shades = plt.cm.viridis(np.linspace(0.85, 0.08, len(TS)))

    # The figure prints at \textwidth (~6.3 in) from a 13.5 in canvas, a 0.47x
    # reduction, so source sizes are ~2x the intended printed size (7-8 pt).
    plt.rcParams.update({"font.size": 15, "axes.labelsize": 15, "xtick.labelsize": 14,
                         "ytick.labelsize": 14})
    fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.3))
    x = np.array(NS, dtype=float)

    for T, col in zip(TS, shades):
        p50 = [med(df, T, "H", n, "p50") for n in NS]
        h99 = [med(df, T, "H", n, "p99") for n in NS]
        p99 = [med(df, T, "P", n, "p99") for n in NS]
        d = [h99[i] - p50[i] for i in range(len(NS))]

        axes[0].plot(x, p50, "o-", color=col, lw=2, ms=5)
        axes[1].plot(x, h99, "o-", color=col, lw=2, ms=5)
        axes[1].plot(x, p99, "s--", color=col, lw=1.4, ms=4, alpha=.85)
        if d[0] > 0:
            # Delta(N) hits exactly 0 once the per-stage service drops under
            # the feed's tight-gap floor. Zero has no place on a log axis, so
            # those points are drawn as open markers pinned to the floor of the
            # panel and called out in the caption as exact zeros.
            r = np.array([v / d[0] for v in d], dtype=float)
            hit = r <= 0
            axes[2].plot(x[~hit], r[~hit], "o-", color=col, lw=2, ms=5)
            if hit.any():
                axes[2].plot(x[hit], np.full(hit.sum(), FLOOR), "o",
                             mfc="white", mec=col, mew=1.6, ms=6)
        # Direct label on each curve: identity is never carried by colour
        # alone. Labels sit at the N=1 end, where the curves are separated by
        # a factor of two or more; at the N=8 end the short-T curves converge
        # and the labels would collide.
        for ax, ys in ((axes[0], p50), (axes[1], h99)):
            ax.annotate(f"{T}", (x[0], ys[0]), textcoords="offset points",
                        xytext=(-7, 0), ha="right", va="center",
                        fontsize=13, color=col)

    axes[2].plot(x, 1.0 / x, ":", color="0.35", lw=1.8, label=r"bound $1/N$")

    for ax in axes:
        ax.set_xscale("log", base=2)
        ax.set_xticks(NS)
        ax.set_xticklabels([str(n) for n in NS])
        ax.set_xlabel("stages $N$")
        ax.set_xlim(0.78, 9.2)
        ax.grid(alpha=.25, lw=.6)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)

    axes[0].set_yscale("log")
    axes[0].set_ylabel(r"median latency ($\mu$s)")
    axes[0].set_title(r"$p_{50}$: hop tax $(N-1)h$", fontsize=15)

    axes[1].set_yscale("log")
    axes[1].set_ylabel(r"$p_{99}$ latency ($\mu$s)")
    axes[1].set_title(r"$p_{99}$: Hawkes (solid) vs Poisson null (dashed)",
                      fontsize=15)

    axes[2].set_yscale("log")
    axes[2].set_ylim(FLOOR / 1.6, 1.5)
    axes[2].set_ylabel(r"$\Delta(N)\,/\,\Delta(1)$")
    axes[2].set_title(r"Hawkes tail excess vs the $1/N$ bound", fontsize=15)
    axes[2].legend(frameon=False, fontsize=13, loc="lower left")

    # One shared note for the T labelling, rather than a colour-only legend.
    fig.text(0.5, 0.005,
             r"curves labelled by service floor $T$ in $\mu$s; "
             r"$h = 1.7\,\mu$s; corpus medians over 3512 windows",
             ha="center", fontsize=14, color="0.3")

    fig.tight_layout(rect=(0, 0.06, 1, 1))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, bbox_inches="tight")
    print(f"[fig] wrote {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
