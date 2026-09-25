# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Regenerate the paper's results tables from the qsim_run.py grid output.

Reads `qsim_grid.parquet`, aggregates corpus medians per (T, N) cell for both
arrival regimes, and emits:

  * Table 1 (tab:main-corpus) -- p50 / p99 / p99.9 under Hawkes and the Poisson
    null, as LaTeX.
  * Table 2 (tab:delta) -- clustering tail excess Delta(N), its ratio to the
    single-stage value, and the least-squares exponent gamma, as LaTeX.
  * A side-by-side delta against the previous (transactTime-keyed) run, so a
    change in the headline numbers is visible rather than asserted.
  * The derived prose quantities the paper quotes: p99/T ratios, the per-window
    win rate for N=2 vs N=1, and the kappa breakpoints of the design equation.

CLI:
    python3 -m arrival_paper.make_tables \\
        --grid arrival_paper/figs/qsim_grid/qsim_grid.parquet
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd

SCEN = [("T2_h1p7us", 2), ("T4_h1p7us", 4), ("T8_h1p7us", 8),
        ("T16_h1p7us", 16), ("T32_h1p7us", 32),
        ("T64_h1p7us", 64), ("T128_h1p7us", 128)]
NS = [1, 2, 4, 8]
H = 1.7

# Previous run, keyed on transactTime (the timestamp bug: one matching-engine
# transaction split across several UDP packets collapsed onto one instant).
# Retained only so the rerun's effect is visible.
OLD_H = {
    (2, 1): (2.00, 4.00, 7.38),     (2, 2): (3.70, 4.70, 5.52),
    (2, 4): (7.10, 7.60, 7.75),     (2, 8): (13.90, 14.15, 14.15),
    (4, 1): (4.00, 10.87, 20.00),   (4, 2): (5.70, 7.70, 11.08),
    (4, 4): (9.10, 10.10, 10.92),   (4, 8): (15.90, 16.40, 16.55),
    (8, 1): (8.00, 28.20, 55.05),   (8, 2): (9.70, 16.57, 25.70),
    (8, 4): (13.10, 15.10, 18.48),  (8, 8): (19.90, 20.90, 21.72),
    (16, 1): (16.00, 79.22, 157.07), (16, 2): (17.70, 37.90, 64.75),
    (16, 4): (21.10, 27.97, 37.10),  (16, 8): (27.90, 29.90, 33.28),
    (32, 1): (32.00, 254.73, 497.15), (32, 2): (33.70, 96.92, 174.77),
    (32, 4): (37.10, 57.30, 84.15),   (32, 8): (43.90, 50.77, 59.90),
}


def col(label: str, regime: str, n: int, q: str) -> str:
    return f"{label}_{regime}_N{n}_{q}_us"


def gather(df: pd.DataFrame) -> dict:
    """(T, N, regime) -> (p50, p99, p999), corpus median over windows."""
    out = {}
    for label, T in SCEN:
        for n in NS:
            for regime in ("H", "P", "G", "B"):
                vals = []
                for q in ("p50", "p99", "p999"):
                    c = col(label, regime, n, q)
                    vals.append(float(df[c].median()) if c in df.columns
                                else float("nan"))
                out[(T, n, regime)] = tuple(vals)
    return out


def gamma_fit(deltas: list[float]) -> float:
    x = [math.log(n) for n in NS]
    y = [math.log(max(d, 1e-12)) for d in deltas]
    mx, my = sum(x) / len(x), sum(y) / len(y)
    num = sum((a - mx) * (b - my) for a, b in zip(x, y))
    den = sum((a - mx) ** 2 for a in x)
    return -num / den


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--grid", required=True)
    ap.add_argument("--compare", action="store_true", default=True,
                    help="show delta vs the previous transactTime-keyed run")
    args = ap.parse_args()

    p = Path(args.grid)
    if not p.exists():
        print(f"[tables] {p} not found; run qsim_run.py first", file=sys.stderr)
        return 1
    df = pd.read_parquet(p)
    nwin = len(df)
    nsess = df["session"].nunique() if "session" in df.columns else -1
    print(f"[tables] {nwin} windows across {nsess} sessions\n", file=sys.stderr)

    g = gather(df)

    # ---- Table 1 -----------------------------------------------------------
    print("% ===== Table 1 (tab:main-corpus) =====")
    print(r"$T$ & $N$ & $p_{50}$ & $p_{99}$ & $p_{99.9}$ "
          r"& $p_{50}$ & $p_{99}$ & $p_{99.9}$ \\")
    for label, T in SCEN:
        print(r"\midrule")
        for n in NS:
            h3 = g[(T, n, "H")]
            p3 = g[(T, n, "P")]
            print(f"{T:2d} & {n} & " +
                  " & ".join(f"{v:6.2f}" for v in h3) + " & " +
                  " & ".join(f"{v:6.2f}" for v in p3) + r" \\")

    # ---- Table 2 -----------------------------------------------------------
    print("\n% ===== Table 2 (tab:delta) =====")
    print(r"$T$ & $N{=}1$ & $N{=}2$ & $N{=}4$ & $N{=}8$ & "
          r"& $N{=}2$ & $N{=}4$ & $N{=}8$ & $\gamma$ \\")
    for label, T in SCEN:
        d = [g[(T, n, "H")][1] - g[(T, n, "H")][0] for n in NS]
        if d[0] <= 0:
            # No single-stage tail excess at this service floor: there is
            # nothing for the tandem to compress, and the ratio is undefined.
            print(f"{T:2d} & " + " & ".join(f"{v:7.2f}" for v in d) +
                  r" & & --- & --- & --- & --- \\")
            continue
        r = [d[i] / d[0] for i in (1, 2, 3)]
        # gamma is a log-log slope over Delta(N); it is only meaningful when
        # every Delta(N) is strictly positive. Where the tandem drives the tail
        # excess to exactly zero the fit is reading the 1e-12 floor, not data.
        gam = f"{gamma_fit(d):.2f}" if min(d) > 0 else "---"
        print(f"{T:2d} & " + " & ".join(f"{v:7.2f}" for v in d) + " & & " +
              " & ".join(f"{v:.3f}" for v in r) +
              f" & {gam}" + r" \\")


    # ---- Table (tab:nulls): p99 under the four arrival regimes -------------
    print("\n% ===== Table (tab:nulls): single-stage and tandem p99 under H / P / G / B =====")
    print(r"$T$ & $N$ & real (H) & uniform (P) & gap shuffle (G) & 1\,s-binned (B) \\")
    for label, T in SCEN:
        print(r"\midrule")
        for n in NS:
            vals = [g[(T, n, r)][1] for r in ("H", "P", "G", "B")]
            print(f"{T:2d} & {n} & " + " & ".join(f"{v:7.2f}" for v in vals) + r" \\")

    print("\n[prose] share of the single-stage Hawkes p99 EXCESS that survives each null "
          "(excess = p99 - T; corpus medians):", file=sys.stderr)
    for label, T in SCEN:
        eH = g[(T, 1, "H")][1] - T
        if eH <= 0:
            continue
        eG = g[(T, 1, "G")][1] - T
        eB = g[(T, 1, "B")][1] - T
        eP = g[(T, 1, "P")][1] - T
        print(f"   T={T:3d}: H {eH:8.2f}  G {eG:8.2f} ({100*eG/eH:5.1f}%)  "
              f"B {eB:8.2f} ({100*eB/eH:5.1f}%)  P {eP:8.2f} ({100*eP/eH:5.1f}%)",
              file=sys.stderr)

    # ---- Table (tab:equal-core): tandem vs M/D/N at the same core count ----
    def mdn(label, n, q):
        c = f"{label}_H_MDN{n}_{q}_us"
        return float(df[c].median()) if c in df.columns else float("nan")
    print("\n% ===== Table (tab:equal-core): Hawkes arrivals, N cores as tandem vs as M/D/N dispatch =====")
    print(r"$T$ & $N$ & tandem $p_{50}$ & tandem $p_{99}$ & M/D/N $p_{50}$ & M/D/N $p_{99}$ \\")
    for label, T in SCEN:
        print(r"\midrule")
        h1 = g[(T, 1, "H")]
        print(f"{T:2d} & 1 & {h1[0]:7.2f} & {h1[1]:7.2f} & {h1[0]:7.2f} & {h1[1]:7.2f} \\\\")
        for n in (2, 4, 8):
            ht = g[(T, n, "H")]
            print(f"{T:2d} & {n} & {ht[0]:7.2f} & {ht[1]:7.2f} & "
                  f"{mdn(label, n, 'p50'):7.2f} & {mdn(label, n, 'p99'):7.2f} \\\\")

    # ---- bound check -------------------------------------------------------
    print("\n[check] Delta(N)/Delta(1) <= 1/N :", file=sys.stderr)
    bad = []
    for label, T in SCEN:
        d = [g[(T, n, "H")][1] - g[(T, n, "H")][0] for n in NS]
        if d[0] <= 0:
            continue
        for i, n in enumerate(NS[1:], start=1):
            if d[i] / d[0] > 1.0 / n + 1e-9:
                bad.append((T, n, d[i] / d[0], 1.0 / n))
    print("   " + ("OK, no violations" if not bad else f"VIOLATIONS: {bad}"),
          file=sys.stderr)

    # ---- p50 identity ------------------------------------------------------
    off = [(T, n, g[(T, n, 'H')][0], T + (n - 1) * H)
           for label, T in SCEN for n in NS
           if abs(g[(T, n, "H")][0] - (T + (n - 1) * H)) > 0.02]
    print(f"[check] p50 = T+(N-1)h : "
          f"{'OK all 20 cells' if not off else f'OFF: {off}'}", file=sys.stderr)

    # ---- per-window win rate ----------------------------------------------
    print("\n[prose] per-window win rate, p99(2) < p99(1):", file=sys.stderr)
    for label, T in SCEN:
        a, b = col(label, "H", 1, "p99"), col(label, "H", 2, "p99")
        if a in df.columns and b in df.columns:
            print(f"   T={T:2d}: {100*(df[b] < df[a]).mean():5.1f}%",
                  file=sys.stderr)

    # ---- p99/T ratios ------------------------------------------------------
    print("\n[prose] single-stage p99/T  median / 95th-pct across windows:",
          file=sys.stderr)
    for label, T in SCEN:
        c = col(label, "H", 1, "p99")
        if c in df.columns:
            s = df[c] / T
            print(f"   T={T:2d}: {s.median():5.2f} / {s.quantile(.95):5.2f}",
                  file=sys.stderr)

    # ---- kappa breakpoints -------------------------------------------------
    print("\n[prose] kappa breakpoints (N: 1->2, 2->4, 4->8):", file=sys.stderr)
    for label, T in SCEN:
        d = [g[(T, n, "H")][1] - g[(T, n, "H")][0] for n in NS]
        bp = [(((NS[i + 1] - NS[i]) * H) / (d[i] - d[i + 1]))
              if d[i] - d[i + 1] > 1e-12 else float('inf') for i in range(3)]
        star = min(NS, key=lambda n: (n - 1) * H + 1.0 * d[NS.index(n)])
        print(f"   T={T:2d}: " + "  ".join(f"{v:8.3f}" for v in bp) +
              f"   N*(kappa=1)={star}", file=sys.stderr)

    # ---- delta vs previous run --------------------------------------------
    if args.compare:
        print("\n[compare] Hawkes, new (sendingTime) vs old (transactTime):",
              file=sys.stderr)
        print(f"   {'T':>3} {'N':>2} | {'p50 new':>8} {'old':>8} {'d%':>7}"
              f" | {'p99 new':>8} {'old':>8} {'d%':>7}"
              f" | {'p999 new':>9} {'old':>9} {'d%':>7}", file=sys.stderr)
        for label, T in SCEN:
            for n in NS:
                new = g[(T, n, "H")]
                old = OLD_H.get((T, n))
                if old is None:
                    continue
                parts = []
                for nv, ov in zip(new, old):
                    d = 100 * (nv - ov) / ov if ov else float("nan")
                    parts.append(f"{nv:8.2f} {ov:8.2f} {d:+6.1f}%")
                print(f"   {T:>3} {n:>2} | " + " | ".join(parts), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
