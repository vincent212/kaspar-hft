# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Correlations between Hawkes params and markout tails at multiple time scales.

Runs three analyses on one session's message_tape + fill_tape:

    (a) Multi-window scan — 5 / 15 / 30 / 60 min windows.
        For each window size w:
            fit Hawkes on each w-min window
            per window: n, log(mean λ), p95 |markout_1s| over fills in the window
            report Spearman ρ(n, tail), ρ(λ̄, tail), ρ(n, λ̄)

    (b) Rolling-Hawkes event-level — n_rolling(t) at each fill.
        Fit Hawkes on the trailing 30-min window ending at every 5-min grid
        point; look up the containing grid's n_rolling and λ̂ at each fill's
        exec_ts; compute event-level ρ(n_rolling, |markout|), ρ(λ̂, |markout|).

    (c) Whole-session baseline — one Hawkes fit for the entire session,
        interpolate λ̂ at each fill. Reports ρ(λ̂, |markout|) at event level,
        matches the earlier smoke result for comparison.

Output: prints a summary table plus JSON of every correlation.

CLI:
    python3 -m arrival_paper.hawkes_correlations \\
        --msg-tape /vast/…/318/message/20250310.NQH5.csv \\
        --fill-tape /vast/…/318/fill/20250310.NQH5.parquet \\
        --windows 5,15,30,60 \\
        --rolling-window-min 30 --rolling-stride-min 5
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import List, Optional

import numpy as np
import pandas as pd
from scipy import stats

from arrival_paper.hawkes_smoke import load_arrivals, fit_hawkes, intensity_at


# ---------------------------------------------------------------------------
# (a) Multi-window scan
# ---------------------------------------------------------------------------


def scan_windows(ts_ns: np.ndarray, fill: pd.DataFrame,
                 window_minutes: int, min_events: int) -> pd.DataFrame:
    """Return one row per window with (n, log_mean_lambda, log_p95_absmark)."""
    win_ns = window_minutes * 60 * 1_000_000_000
    first = ts_ns[0]
    win_idx = (ts_ns - first) // win_ns
    df = pd.DataFrame({"ts": ts_ns, "w": win_idx.astype("int64")})
    groups = df.groupby("w")
    rows = []
    for w, g in groups:
        window_ts = g["ts"].to_numpy()
        n_events = len(window_ts)
        if n_events < min_events:
            continue
        # int64 origin-shift before float conversion (see fit_hawkes docstring
        # in hawkes_smoke on 2026-epoch float64 quantization).
        window_ts_i = np.asarray(window_ts, dtype=np.int64)
        try:
            fit = fit_hawkes(window_ts_i)
        except (ValueError, RuntimeError):
            continue
        start_ns = int(window_ts[0])
        end_ns = int(window_ts[-1])
        dur_sec = (end_ns - start_ns) / 1e9
        if dur_sec <= 0:
            continue
        # Fills whose exec_ts is in this window
        m = (fill["exec_ts"] >= start_ns) & (fill["exec_ts"] <= end_ns)
        a = fill.loc[m, "markout_1s"].abs().dropna()
        if len(a) < 20:
            continue
        rows.append({
            "window_id": int(w),
            "start_ns": start_ns,
            "end_ns": end_ns,
            "n_events": n_events,
            "n_branch": fit["n_branch"],
            "log_mean_lambda": float(np.log(n_events / dur_sec)),
            "p95_absmarkout": float(a.quantile(0.95)),
            # Illiquid / overnight windows can produce quantile == 0.
            # np.log(0) is -inf which poisons downstream mean / json.dump;
            # clip to a tiny floor as in hourly_panel.
            "log_p95_absmarkout": float(np.log(max(a.quantile(0.95), 1e-9))),
            "n_fills": int(m.sum()),
        })
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# (b) Rolling-Hawkes at event level
# ---------------------------------------------------------------------------


def rolling_hawkes_grid(ts_ns: np.ndarray, fill: pd.DataFrame,
                        rolling_window_min: int, rolling_stride_min: int,
                        min_events: int) -> pd.DataFrame:
    """For each grid point, fit Hawkes on the trailing rolling_window_min.
    Return one row per fill enriched with (n_rolling, lambda_rolling).
    """
    win_ns = rolling_window_min * 60 * 1_000_000_000
    stride_ns = rolling_stride_min * 60 * 1_000_000_000
    first = ts_ns[0]
    last = ts_ns[-1]
    grid = np.arange(first + win_ns, last, stride_ns, dtype=np.int64)

    grid_fits: list[dict] = []
    for g_end in grid:
        g_start = g_end - win_ns
        mask = (ts_ns >= g_start) & (ts_ns <= g_end)
        n_events = int(mask.sum())
        if n_events < min_events:
            continue
        try:
            fit = fit_hawkes(np.asarray(ts_ns[mask], dtype=np.int64))
        except (ValueError, RuntimeError):
            continue
        dur_sec = (g_end - g_start) / 1e9
        grid_fits.append({
            "grid_end_ns": int(g_end),
            "n_rolling": fit["n_branch"],
            "log_mean_lambda_rolling": float(np.log(n_events / dur_sec)),
            "n_events": n_events,
        })
    if not grid_fits:
        return pd.DataFrame()
    grid_df = pd.DataFrame(grid_fits).sort_values("grid_end_ns")

    # For each fill, look up nearest-before grid entry (as-of)
    fill_sorted = fill.dropna(subset=["exec_ts", "markout_1s"]).sort_values("exec_ts").copy()
    idx = np.searchsorted(grid_df["grid_end_ns"].to_numpy(),
                          fill_sorted["exec_ts"].to_numpy(),
                          side="right") - 1
    ok = idx >= 0
    out = fill_sorted[ok].copy()
    out["n_rolling"] = grid_df["n_rolling"].to_numpy()[idx[ok]]
    out["log_mean_lambda_rolling"] = grid_df["log_mean_lambda_rolling"].to_numpy()[idx[ok]]
    return out


# ---------------------------------------------------------------------------
# (c) Whole-session λ̂ at each fill
# ---------------------------------------------------------------------------


def whole_session_intensity_at_fills(ts_ns: np.ndarray,
                                     fill: pd.DataFrame,
                                     max_events_for_fit: int = 200_000,
                                     max_fills: int = 20_000) -> tuple[float, dict]:
    """Fit Hawkes once over the whole session; return ρ(λ̂, |markout|) at
    fills and the fit summary."""
    if len(ts_ns) > max_events_for_fit:
        step = len(ts_ns) // max_events_for_fit
        ts_ns = ts_ns[::step][:max_events_for_fit]
    # int64 origin-shift before float conversion (see fit_hawkes).
    ts_i = np.asarray(ts_ns, dtype=np.int64)
    origin_ns = int(ts_i[0])
    t_sec = (ts_i - origin_ns).astype(np.float64) / 1e9
    fit = fit_hawkes(ts_i)
    sample = fill.dropna(subset=["exec_ts", "markout_1s"])
    if len(sample) > max_fills:
        sample = sample.sample(max_fills, random_state=42).sort_values("exec_ts")
    # Same anchor for fill times so intensity_at gets a consistent time axis.
    fill_ts_i = sample["exec_ts"].to_numpy(dtype=np.int64)
    fill_t_sec = (fill_ts_i - origin_ns).astype(np.float64) / 1e9
    lam_hat = intensity_at(t_sec, fill_t_sec, fit["mu"], fit["alpha"],
                           fit["beta"], t_sec[0])
    rho, p = stats.spearmanr(lam_hat, sample["markout_1s"].abs())
    return float(rho), {"fit": fit, "n_fills": int(len(sample)), "p": float(p)}


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True)
    ap.add_argument("--fill-tape", required=True)
    ap.add_argument("--windows", default="5,15,30,60",
                    help="comma-separated window sizes in minutes")
    ap.add_argument("--min-events-per-window", type=int, default=2000)
    ap.add_argument("--rolling-window-min", type=int, default=30)
    ap.add_argument("--rolling-stride-min", type=int, default=5)
    ap.add_argument("--out-json", default=None)
    args = ap.parse_args()

    print(f"[hc] loading arrivals from {args.msg_tape}", file=sys.stderr)
    ts_ns = load_arrivals(args.msg_tape, max_events=0)
    print(f"[hc] {len(ts_ns):,} arrivals over "
          f"{(ts_ns[-1]-ts_ns[0])/1e9/3600:.2f} h", file=sys.stderr)

    fill = pd.read_parquet(args.fill_tape)
    print(f"[hc] {len(fill):,} fills in fill_tape", file=sys.stderr)

    windows: List[int] = [int(x) for x in args.windows.split(",")]

    summary: dict = {"window_scan": [], "rolling": None, "whole_session": None}

    # ------------------- (a) window scan --------------------------------
    print(f"\n{'=' * 72}", file=sys.stderr)
    print(f"(a) Multi-window scan: windows = {windows} min",
          file=sys.stderr)
    print(f"{'=' * 72}", file=sys.stderr)
    print(f"{'win_min':>8} {'n_win':>7} {'ρ(n, tail)':>14} {'ρ(λ̄, tail)':>14} "
          f"{'ρ(n, λ̄)':>12}", file=sys.stderr)
    for w_min in windows:
        panel = scan_windows(ts_ns, fill, window_minutes=w_min,
                             min_events=args.min_events_per_window)
        if len(panel) < 5:
            print(f"{w_min:>8} {len(panel):>7} — too few windows", file=sys.stderr)
            summary["window_scan"].append({"window_min": w_min, "n_windows": len(panel)})
            continue
        rho_n, p_n = stats.spearmanr(panel["n_branch"], panel["log_p95_absmarkout"])
        rho_l, p_l = stats.spearmanr(panel["log_mean_lambda"], panel["log_p95_absmarkout"])
        rho_nl, p_nl = stats.spearmanr(panel["n_branch"], panel["log_mean_lambda"])
        print(f"{w_min:>8} {len(panel):>7} "
              f"{rho_n:>+8.3f} (p={p_n:.2g}) "
              f"{rho_l:>+8.3f} (p={p_l:.2g}) "
              f"{rho_nl:>+8.3f}", file=sys.stderr)
        summary["window_scan"].append({
            "window_min": w_min, "n_windows": int(len(panel)),
            "rho_n_tail": float(rho_n), "p_n_tail": float(p_n),
            "rho_lambda_tail": float(rho_l), "p_lambda_tail": float(p_l),
            "rho_n_lambda": float(rho_nl), "p_n_lambda": float(p_nl),
        })

    # ------------------- (b) rolling-Hawkes event-level -----------------
    print(f"\n{'=' * 72}", file=sys.stderr)
    print(f"(b) Rolling-Hawkes event-level "
          f"(win={args.rolling_window_min} min, stride={args.rolling_stride_min} min)",
          file=sys.stderr)
    print(f"{'=' * 72}", file=sys.stderr)
    rolling = rolling_hawkes_grid(ts_ns, fill,
                                  rolling_window_min=args.rolling_window_min,
                                  rolling_stride_min=args.rolling_stride_min,
                                  min_events=args.min_events_per_window)
    if len(rolling) < 100:
        print(f"too few rolling matches ({len(rolling)})", file=sys.stderr)
        summary["rolling"] = {"n_fills": len(rolling)}
    else:
        abs_mkt = rolling["markout_1s"].abs()
        rho_n, p_n = stats.spearmanr(rolling["n_rolling"], abs_mkt)
        rho_l, p_l = stats.spearmanr(rolling["log_mean_lambda_rolling"], abs_mkt)
        print(f"{len(rolling):,} fills matched to a rolling fit",
              file=sys.stderr)
        print(f"ρ(n_rolling, |markout_1s|)   = {rho_n:>+7.4f}  (p = {p_n:.3g})",
              file=sys.stderr)
        print(f"ρ(λ̄_rolling, |markout_1s|)  = {rho_l:>+7.4f}  (p = {p_l:.3g})",
              file=sys.stderr)
        summary["rolling"] = {
            "n_fills": int(len(rolling)),
            "rho_n_markout": float(rho_n), "p_n_markout": float(p_n),
            "rho_lambda_markout": float(rho_l), "p_lambda_markout": float(p_l),
        }

    # ------------------- (c) whole-session baseline ---------------------
    print(f"\n{'=' * 72}", file=sys.stderr)
    print("(c) Whole-session Hawkes fit (baseline)", file=sys.stderr)
    print(f"{'=' * 72}", file=sys.stderr)
    rho, aux = whole_session_intensity_at_fills(ts_ns, fill)
    print(f"whole-session n = {aux['fit']['n_branch']:.4f}", file=sys.stderr)
    print(f"whole-session λ̄ = {aux['fit']['lambda_bar']:.2f} events/s",
          file=sys.stderr)
    print(f"ρ(λ̂(t_fill), |markout_1s|)  = {rho:+7.4f}  "
          f"(p = {aux['p']:.3g}, N = {aux['n_fills']:,})", file=sys.stderr)
    summary["whole_session"] = {
        "n_branch": aux["fit"]["n_branch"],
        "lambda_bar": aux["fit"]["lambda_bar"],
        "rho_lambda_markout_event_level": float(rho),
        "p": aux["p"],
        "n_fills": aux["n_fills"],
    }

    if args.out_json:
        Path(args.out_json).parent.mkdir(parents=True, exist_ok=True)
        with open(args.out_json, "w") as f:
            json.dump(summary, f, indent=2)
        print(f"\nwrote {args.out_json}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
