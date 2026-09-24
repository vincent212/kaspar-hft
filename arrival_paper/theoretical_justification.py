# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
"""Paper §5: theoretical justification for Hawkes-driven latency modelling.

Compare four latency models on the same arrival grid (lambda_bar, n):
    (a) Hawkes  -> G/D/1 Lindley       [our proposal]
    (b) Poisson -> G/D/1 (M/D/1)       [classical, same rate no clustering]
    (c) i.i.d. Exp(1/s) latency draws  [decoupled from arrival state]
    (d) Constant s latency             [no queue]

Outputs
    p99_p50_ratio_<model>.png : dispersion heatmap over (lambda_bar, n)
    ccdf_near_critical.png    : survival curves at one representative cell
    ccdf_empirical_overlay.png: same cell with real NQ p99 CCDF from a matched
                                empirical session
    grid.parquet              : all cells x models raw quantiles

CLI:
    python3 -m arrival_paper.theoretical_justification --out-dir arrival_paper/figs/theory
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


LAMBDA_BARS = [1.0, 10.0, 100.0, 1000.0, 10000.0]
N_BRANCHES  = [0.30, 0.50, 0.70, 0.85, 0.95]
BETA_FIXED  = 1000.0                    # 1 ms clustering timescale
S_US        = 7                         # paper's calibrated service time
CCDF_CELL   = (1000.0, 0.85)            # representative near-critical cell
MIN_EVENTS_FLOOR = 1000
MIN_SECONDS      = 60.0
SEED             = 42


# ---------------------------------------------------------------------------
# Arrival process simulators
# ---------------------------------------------------------------------------

def ogata_thin_exp_hawkes(mu: float, alpha: float, beta: float, T: float,
                          rng: np.random.Generator,
                          max_events: int = 20_000_000) -> np.ndarray:
    if alpha >= beta:
        raise ValueError(f"supercritical: alpha={alpha} >= beta={beta}")
    events: list[float] = []
    t = 0.0
    R = 0.0
    while t < T and len(events) < max_events:
        lam_bar = mu + alpha * R
        if lam_bar <= 0:
            t += 1.0
            R = 0.0
            continue
        w = rng.exponential(1.0 / lam_bar)
        t_new = t + w
        if t_new >= T:
            break
        R_new = R * np.exp(-beta * w)
        lam_new = mu + alpha * R_new
        if rng.uniform(0.0, lam_bar) <= lam_new:
            events.append(t_new)
            R = R_new + 1.0
        else:
            R = R_new
        t = t_new
    return np.asarray(events, dtype=np.float64)


def poisson_arrivals(rate: float, T: float, rng: np.random.Generator) -> np.ndarray:
    """Homogeneous Poisson process on [0, T] with intensity `rate`.
    Uses inter-arrival ~ Exp(1/rate); returns sorted arrival times (sec)."""
    n_expected = int(rate * T * 1.2)
    gaps = rng.exponential(1.0 / rate, size=max(n_expected, 1024))
    t = np.cumsum(gaps)
    t = t[t < T]
    # Extend if truncated too early.
    while t.size == 0 or t[-1] < T - 3.0 / rate:
        more = rng.exponential(1.0 / rate, size=n_expected)
        t = np.concatenate([t, t[-1] + np.cumsum(more) if t.size else np.cumsum(more)])
        t = t[t < T]
    return t


# ---------------------------------------------------------------------------
# Queue models
# ---------------------------------------------------------------------------

def lindley_gd1(arrivals_ns: np.ndarray, service_ns: int) -> np.ndarray:
    a = np.asarray(arrivals_ns, dtype=np.int64)
    n = len(a)
    if n < 2:
        return np.empty(0, dtype=np.int64)
    idx = np.arange(1, n + 1, dtype=np.int64)
    running = np.maximum.accumulate(a - (idx - 1) * service_ns)
    depart = running + idx * service_ns
    return depart - a


def iid_exp_latency(n: int, service_ns: int, rng: np.random.Generator) -> np.ndarray:
    """Each event gets an independent Exp(mean=service_ns) latency draw.
    No queue; latency does not depend on arrival state."""
    return rng.exponential(service_ns, size=n).astype(np.int64)


def constant_latency(n: int, service_ns: int) -> np.ndarray:
    return np.full(n, service_ns, dtype=np.int64)


# ---------------------------------------------------------------------------
# Grid evaluation
# ---------------------------------------------------------------------------

def run_grid() -> pd.DataFrame:
    rng = np.random.default_rng(SEED)
    rows = []
    service_ns = S_US * 1000
    for lam in LAMBDA_BARS:
        for n in N_BRANCHES:
            T = max(MIN_EVENTS_FLOOR / lam, MIN_SECONDS)
            alpha = n * BETA_FIXED
            mu    = lam * (1.0 - n)

            hawkes_sec = ogata_thin_exp_hawkes(mu, alpha, BETA_FIXED, T, rng)
            hawkes_ns  = np.unique((hawkes_sec * 1e9).astype(np.int64))
            poisson_sec = poisson_arrivals(lam, T, rng)
            poisson_ns  = np.unique((poisson_sec * 1e9).astype(np.int64))

            # (a) Hawkes -> G/D/1
            st_a = lindley_gd1(hawkes_ns, service_ns)
            # (b) Poisson -> G/D/1 (M/D/1)
            st_b = lindley_gd1(poisson_ns, service_ns)
            # (c) i.i.d. Exp(1/s) draws on Hawkes arrival timing
            st_c = iid_exp_latency(len(hawkes_ns), service_ns, rng)
            # (d) constant s
            st_d = constant_latency(len(hawkes_ns), service_ns)

            for lbl, st in [("hawkes_gd1", st_a),
                            ("poisson_mds1", st_b),
                            ("iid_exp", st_c),
                            ("constant", st_d)]:
                if len(st) < 100:
                    p50 = p99 = p999 = mx = float("nan")
                else:
                    p50 = float(np.quantile(st, 0.50))
                    p99 = float(np.quantile(st, 0.99))
                    p999 = float(np.quantile(st, 0.999))
                    mx  = float(st.max())
                rows.append({
                    "lambda_bar": lam, "n_branch": n, "beta": BETA_FIXED,
                    "s_us": S_US, "T_sec": T,
                    "n_events": len(st), "model": lbl,
                    "p50_ns": p50, "p99_ns": p99, "p999_ns": p999, "max_ns": mx,
                })
            print(f"[theory] lam={lam:g}  n={n:.2f}  "
                  f"N_hawkes={len(hawkes_ns):,} N_poisson={len(poisson_ns):,}",
                  file=sys.stderr)
    return pd.DataFrame(rows)


def run_ccdf_cell(lam: float, n: float) -> dict[str, np.ndarray]:
    rng = np.random.default_rng(SEED + 1)
    service_ns = S_US * 1000
    T = max(MIN_EVENTS_FLOOR / lam, MIN_SECONDS)
    T = max(T, 300.0)                    # need enough tail samples
    alpha = n * BETA_FIXED
    mu    = lam * (1.0 - n)

    hawkes_ns  = np.unique((ogata_thin_exp_hawkes(mu, alpha, BETA_FIXED, T, rng) * 1e9).astype(np.int64))
    poisson_ns = np.unique((poisson_arrivals(lam, T, rng) * 1e9).astype(np.int64))
    return {
        "hawkes_gd1":   lindley_gd1(hawkes_ns, service_ns),
        "poisson_mds1": lindley_gd1(poisson_ns, service_ns),
        "iid_exp":      iid_exp_latency(len(hawkes_ns), service_ns, rng),
        "constant":     constant_latency(len(hawkes_ns), service_ns),
    }


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def plot_ratio_heatmap(df: pd.DataFrame, model: str, out_path: Path) -> None:
    sub = df[df["model"] == model].copy()
    sub["ratio"] = sub["p99_ns"] / sub["p50_ns"].replace(0, np.nan)
    piv = sub.pivot(index="lambda_bar", columns="n_branch", values="ratio")

    fig, ax = plt.subplots(figsize=(7.0, 5.5))
    v = piv.values.astype(float)
    finite = v[np.isfinite(v) & (v > 0)]
    from matplotlib.colors import LogNorm
    norm = LogNorm(vmin=max(1.0, finite.min()),
                   vmax=max(finite.max(), 1.01)) if finite.size else None
    im = ax.imshow(v, origin="lower", aspect="auto", cmap="OrRd", norm=norm)
    for iy in range(v.shape[0]):
        for ix in range(v.shape[1]):
            val = v[iy, ix]
            s = "n/a" if not np.isfinite(val) else (f"{val:.0f}" if val >= 100
                                                    else (f"{val:.1f}" if val >= 1 else f"{val:.2g}"))
            ax.text(ix, iy, s, ha="center", va="center", fontsize=9)
    ax.set_xticks(range(len(piv.columns)))
    ax.set_yticks(range(len(piv.index)))
    ax.set_xticklabels([f"{c:.2f}" for c in piv.columns])
    ax.set_yticklabels([f"{r:g}" for r in piv.index])
    ax.set_xlabel("n (branching ratio)")
    ax.set_ylabel("lambda_bar (evt / sec)")
    ax.set_title(f"p99 / p50 sys_time ratio — {model}, s={S_US} us, beta={BETA_FIXED:g}/s")
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label("p99/p50")
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)


def plot_ccdf(streams: dict[str, np.ndarray], out_path: Path, title: str,
              extra: dict[str, np.ndarray] | None = None) -> None:
    fig, ax = plt.subplots(figsize=(7.0, 5.0))
    colors = {"hawkes_gd1": "#c1272d", "poisson_mds1": "#0000a7",
              "iid_exp": "#eecc16", "constant": "#008176"}
    labels = {"hawkes_gd1": "Hawkes → G/D/1 (ours)",
              "poisson_mds1": "Poisson → G/D/1 (M/D/1)",
              "iid_exp": "i.i.d. Exp(1/s) latency",
              "constant": "Constant s"}
    for name, st in streams.items():
        if len(st) < 10:
            continue
        st_us = np.sort(np.asarray(st, dtype=np.int64)) / 1000.0
        ccdf = 1.0 - (np.arange(len(st_us)) + 1) / len(st_us)
        # Drop the last point (ccdf==0) so log scale doesn't blow up.
        mask = ccdf > 0
        ax.plot(st_us[mask], ccdf[mask], label=labels.get(name, name),
                color=colors.get(name), lw=2)
    if extra:
        for name, st in extra.items():
            if len(st) < 10:
                continue
            st_us = np.sort(np.asarray(st, dtype=np.int64)) / 1000.0
            ccdf = 1.0 - (np.arange(len(st_us)) + 1) / len(st_us)
            mask = ccdf > 0
            ax.plot(st_us[mask], ccdf[mask], label=name, color="black",
                    lw=1.5, linestyle="--", alpha=0.8)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("system time x (us)")
    ax.set_ylabel("P(sys_time > x)")
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="lower left", fontsize=9)
    fig.tight_layout()
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)


def load_empirical_ccdf(msg_tape_csv: str, service_us: int = S_US) -> np.ndarray:
    """Read a real NQ msg tape and return sys_time array (ns) from the qsim
    on the whole session."""
    import csv
    ts: list[int] = []
    with open(msg_tape_csv, "rt", newline="") as f:
        r = csv.reader(f)
        h = next(r)
        i_tt = h.index("transactTime")
        for row in r:
            try:
                ts.append(int(row[i_tt]))
            except (IndexError, ValueError):
                continue
    a = np.asarray(ts, dtype=np.int64)
    a.sort()
    return lindley_gd1(a, service_us * 1000)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--empirical-tape",
                    default="/vast/home/vmayeski/out/arrival_paper/tapes/318/message/20250408.NQM5.csv",
                    help="Real NQ msg tape to overlay on the CCDF plot.")
    args = ap.parse_args()
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    df = run_grid()
    df.to_parquet(out / "grid.parquet", compression="snappy", index=False)
    print(f"[theory] wrote {out}/grid.parquet ({len(df)} rows)", file=sys.stderr)

    for model in ["hawkes_gd1", "poisson_mds1", "iid_exp", "constant"]:
        plot_ratio_heatmap(df, model, out / f"p99_p50_ratio_{model}.png")
        print(f"[theory] wrote p99_p50_ratio_{model}.png", file=sys.stderr)

    lam, n = CCDF_CELL
    streams = run_ccdf_cell(lam, n)
    plot_ccdf(streams, out / "ccdf_near_critical.png",
              f"CCDF at (lambda_bar={lam:g}, n={n:.2f}), s={S_US}us, beta={BETA_FIXED:g}/s")
    print(f"[theory] wrote ccdf_near_critical.png", file=sys.stderr)

    if args.empirical_tape and Path(args.empirical_tape).exists():
        print(f"[theory] reading empirical tape {args.empirical_tape} ...",
              file=sys.stderr)
        emp_sys_ns = load_empirical_ccdf(args.empirical_tape, service_us=S_US)
        plot_ccdf(streams, out / "ccdf_empirical_overlay.png",
                  f"CCDF: theory vs empirical NQ (s={S_US}us)",
                  extra={"empirical NQ (2025-04-08)": emp_sys_ns})
        print(f"[theory] wrote ccdf_empirical_overlay.png", file=sys.stderr)
    else:
        print(f"[theory] SKIP empirical overlay (tape not found)",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
