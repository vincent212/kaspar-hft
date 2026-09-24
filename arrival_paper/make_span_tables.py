# Copyright (c) 2026 Vincent Maciejewski / M2 Tech.
# Licensed under the MIT License.
"""Tables comparing constant-service against span-aware service.

Reads `qsim_grid_span.parquet` from qsim_span.py and emits:

  * a validation check that the const/H arm reproduces the published
    constant-service grid, which it must, since it is the same model;
  * Table: corpus-median latency under const vs span service, per (T, N);
  * the span-shuffle control (H vs HS), which isolates how much of the span
    effect comes from large packets landing inside clusters rather than from
    span variability alone;
  * packet-weighted against message-weighted quantiles;
  * the in-packet position ladder, for comparison with the live measurement;
  * whether splitting still pays once service depends on span.

CLI:
    python3 -m arrival_paper.make_span_tables \\
        --span arrival_paper/figs/qsim_grid_span/qsim_grid_span.parquet \\
        --base arrival_paper/figs/qsim_grid/qsim_grid.parquet
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

TS = [2, 4, 8, 16, 32, 64, 128]
NS = [1, 2, 4, 8]


def med(df, col):
    return float(df[col].median()) if col in df.columns else float("nan")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--span", required=True)
    ap.add_argument("--base", required=True)
    a = ap.parse_args()
    df = pd.read_parquet(a.span)
    print(f"[span-tables] {len(df)} windows\n", file=sys.stderr)

    # ---- validation: const/H must reproduce the published grid -------------
    if Path(a.base).exists():
        b = pd.read_parquet(a.base)
        print("[check] const/H vs published constant-service grid (p99, us):",
              file=sys.stderr)
        worst = 0.0
        for T in TS:
            for N in NS:
                new = med(df, f"T{T}_H_const_N{N}_p99_us")
                old = med(b, f"T{T}_h1p7us_H_N{N}_p99_us")
                if np.isfinite(new) and np.isfinite(old) and old > 0:
                    worst = max(worst, abs(new - old) / old)
        print(f"   worst relative difference across 28 cells: {100*worst:.4f}%",
              file=sys.stderr)
        print("   (expected ~0: identical model, different implementation)\n",
              file=sys.stderr)

    # ---- Table: const vs span ---------------------------------------------
    print("% ===== const vs span service, Hawkes arm, corpus median (us) =====")
    print(r"$T$ & $N$ & const $p_{50}$ & span $p_{50}$ & const $p_{99}$ "
          r"& span $p_{99}$ & ratio & const $p_{99.9}$ & span $p_{99.9}$ \\")
    for T in TS:
        print(r"\midrule")
        for N in NS:
            c50 = med(df, f"T{T}_H_const_N{N}_p50_us")
            s50 = med(df, f"T{T}_H_span_N{N}_p50_us")
            c99 = med(df, f"T{T}_H_const_N{N}_p99_us")
            s99 = med(df, f"T{T}_H_span_N{N}_p99_us")
            c999 = med(df, f"T{T}_H_const_N{N}_p999_us")
            s999 = med(df, f"T{T}_H_span_N{N}_p999_us")
            r = s99 / c99 if c99 > 0 else float("nan")
            print(f"{T:3d} & {N} & {c50:7.2f} & {s50:7.2f} & {c99:8.2f} & "
                  f"{s99:8.2f} & {r:5.2f} & {c999:9.2f} & {s999:9.2f} " + r"\\")

    # ---- span-shuffle control ---------------------------------------------
    print("\n[control] span shuffle: does span/timing coupling add tail?",
          file=sys.stderr)
    print(f"   {'T':>4} {'N':>2} | {'H span':>9} {'HS span':>9} {'H/HS':>6}"
          f" | {'H const':>9}", file=sys.stderr)
    for T in TS:
        for N in (1, 2):
            h = med(df, f"T{T}_H_span_N{N}_p99_us")
            hs = med(df, f"T{T}_HS_span_N{N}_p99_us")
            c = med(df, f"T{T}_H_const_N{N}_p99_us")
            print(f"   {T:>4} {N:>2} | {h:9.2f} {hs:9.2f} "
                  f"{(h/hs if hs else np.nan):6.3f} | {c:9.2f}",
                  file=sys.stderr)

    # ---- packet- vs message-weighted --------------------------------------
    print("\n[weighting] N=1, span service: packet- vs message-weighted (us)",
          file=sys.stderr)
    print(f"   {'T':>4} | {'pkt p50':>8} {'msg p50':>8} | {'pkt p99':>9} "
          f"{'msg p99':>9} {'ratio':>6}", file=sys.stderr)
    for T in TS:
        p50 = med(df, f"T{T}_H_span_N1_p50_us")
        m50 = med(df, f"T{T}_H_span_N1_msg_p50_us")
        p99 = med(df, f"T{T}_H_span_N1_p99_us")
        m99 = med(df, f"T{T}_H_span_N1_msg_p99_us")
        print(f"   {T:>4} | {p50:8.2f} {m50:8.2f} | {p99:9.2f} {m99:9.2f} "
              f"{(m99/p99 if p99 else np.nan):6.2f}", file=sys.stderr)

    # ---- in-packet position ladder ----------------------------------------
    print("\n[ladder] median latency by in-packet position, T=8us, span service",
          file=sys.stderr)
    cols = [f"ladder_idx{j}_us" for j in range(40)]
    have = [c for c in cols if c in df.columns]
    if have:
        vals = [(int(c.replace("ladder_idx", "").replace("_us", "")),
                 float(df[c].median())) for c in have]
        vals = [(j, v) for j, v in vals if np.isfinite(v)]
        for j, v in vals[:13]:
            print(f"   idx {j:2d}: {v:7.3f} us", file=sys.stderr)
        if len(vals) > 2:
            js = np.array([j for j, _ in vals], dtype=float)
            vs = np.array([v for _, v in vals], dtype=float)
            sl, ic = np.polyfit(js, vs, 1)
            print(f"   fit over idx 0-{int(js[-1])}: "
                  f"{ic:.3f} + {1000*sl:.0f} ns/msg", file=sys.stderr)

    # ---- does splitting still pay? ----------------------------------------
    print("\n[design] N* minimising p99, const vs span service", file=sys.stderr)
    for T in TS:
        c = [med(df, f"T{T}_H_const_N{N}_p99_us") for N in NS]
        s = [med(df, f"T{T}_H_span_N{N}_p99_us") for N in NS]
        print(f"   T={T:>3}: const N*={NS[int(np.nanargmin(c))]} "
              f"({np.nanmin(c):8.2f})   span N*={NS[int(np.nanargmin(s))]} "
              f"({np.nanmin(s):8.2f})", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
