# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Hourly / 30-min panel builder for the paper's Stage-1 analysis.

For one session's message_tape + fill_tape, split into fixed-length windows
(default 30 min), fit a standard exponential-kernel Hawkes independently on
each window, and compute the Stage-1 observables per window:

Arrival-side drivers (Hawkes fit)
    lambda_bar         μ/(1−n): long-run mean intensity from the window's Hawkes
    n_branch           α/β branching ratio from the window's Hawkes fit
    log_mean_lambda    log of empirical arrivals-per-second in the window

Latency tails (raw ns, no log transform):
    p50_lat_me_ns / p99_lat_me_ns
        matching-engine sub-stage = sendingTime − transactTime
    p50_lat_handler_ns / p99_lat_handler_ns
        send-to-handler sub-stage = handlerendtim − sendingTime
    p50_qsim_ns / p99_qsim_ns
        G/D/1 Lindley on transactTime (T_m) with deterministic service
        (default 7 μs). This is the modelled tail conditioned on the raw
        upstream point process — NOT run on handlerendtim (which is
        already downstream of the physical receive queue and would double-
        count that queue's smoothing).

Fill-side tails (from fill_tape):
    p50_absmark / p95_absmark
        |markout_1s| at 50th and 95th percentiles of fills in the window.
    p50_absret / p95_absret
        |mid-return over 1s snapshots| at 50th and 95th percentiles.

Note on what the tape can't carry: recv_time (pcap arrival timestamp)
is zero-initialized in the databento .bin pipeline, so a direct
"send-to-receive" split of the second sub-stage isn't computable
without rebuilding the tape from raw PCAP.

Windows failing a minimum-event filter (default 10000 events / 30 min) are
excluded from the panel — see methodology.md §5.6 "Low-intensity filter".

CLI:
    python3 -m arrival_paper.hourly_panel \\
        --msg-tape /vast/…/318/message/20250310.NQH5.csv \\
        --fill-tape /vast/…/318/fill/20250310.NQH5.parquet \\
        --window-minutes 30 \\
        --min-events 10000 \\
        --out /vast/…/hourly_panel/20250310.NQH5.parquet

The batch driver stitches all session outputs together into the ~5000-row
panel that Stage 1's confirmatory factor analysis then runs on.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd

from arrival_paper.hawkes_smoke import load_arrivals, fit_hawkes


DEFAULT_WINDOW_MIN = 30
DEFAULT_MIN_EVENTS = 10_000
MIN_LATENCY_SAMPLES = 20   # min per-window observations to compute a p99
DEFAULT_QSIM_SERVICE_US = 7   # G/D/1 service time for the modelled qlen tail


# ---------------------------------------------------------------------------
# Load transactTime + three latency streams
# ---------------------------------------------------------------------------


def load_msgtape_latencies(msg_tape_csv: str) -> dict:
    """Read msgtape CSV once; return a dict of numpy arrays for the three
    latency streams (end-to-end + two sub-stages) plus the arrival
    timestamps.

    Returns keys:
        ts_ns          int64 — transactTime of every message (sorted
                       ascending, exact-tie deduplicated to match
                       load_arrivals())
        lat_e2e_ns     int64 — handlerendtim − transactTime for messages
                       with handlerendtim > 0 (end-to-end: CME matching
                       engine → handler-end on our side)
        lat_e2e_ts     int64 — transactTime paired with each lat_e2e_ns
                       entry
        lat_me_ns      int64 — sendingTime − transactTime for messages
                       with sendingTime > 0 (matching-engine sub-stage)
        lat_me_ts      int64 — transactTime paired with each lat_me_ns
                       entry
        lat_hd_ns      int64 — handlerendtim − sendingTime for messages
                       with both > 0 (send-to-handler sub-stage: CME
                       gateway → databento handler-end)
        lat_hd_ts      int64 — transactTime paired with each lat_hd_ns
                       entry
    """
    opener = gzip.open if msg_tape_csv.endswith(".gz") else open
    tt: list[int] = []
    st: list[int] = []
    he: list[int] = []
    with opener(msg_tape_csv, "rt", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        required = ["transactTime", "sendingTime", "handlerendtim"]
        missing = [c for c in required if c not in header]
        if missing:
            raise ValueError(
                f"msg tape {msg_tape_csv}: missing required columns "
                f"{missing}; header is {header}")
        i_tt = header.index("transactTime")
        i_st = header.index("sendingTime")
        i_he = header.index("handlerendtim")
        for row in reader:
            try:
                v_tt = int(row[i_tt])
                v_st = int(row[i_st])
                v_he = int(row[i_he])
            except (IndexError, ValueError):
                continue
            tt.append(v_tt)
            st.append(v_st)
            he.append(v_he)

    tt_arr = np.asarray(tt, dtype=np.int64)
    st_arr = np.asarray(st, dtype=np.int64)
    he_arr = np.asarray(he, dtype=np.int64)

    order = np.argsort(tt_arr, kind="stable")
    tt_arr = tt_arr[order]
    st_arr = st_arr[order]
    he_arr = he_arr[order]

    e2e_mask = he_arr > 0
    lat_e2e_ns = (he_arr[e2e_mask] - tt_arr[e2e_mask]).astype(np.int64)
    lat_e2e_ts = tt_arr[e2e_mask]

    me_mask = st_arr > 0
    lat_me_ns = (st_arr[me_mask] - tt_arr[me_mask]).astype(np.int64)
    lat_me_ts = tt_arr[me_mask]

    hd_mask = (st_arr > 0) & (he_arr > 0)
    lat_hd_ns = (he_arr[hd_mask] - st_arr[hd_mask]).astype(np.int64)
    lat_hd_ts = tt_arr[hd_mask]

    # ts_ns is the raw sorted transactTime stream (int64, not deduplicated).
    # Same-ns SBE records (a single CME transaction that emits multiple
    # records at one txtim -- e.g. a sweep of asks + the trade) are kept as
    # separate points so both the qsim and n_events count them individually.
    # Consumers that need strict-monotone input (Hawkes MLE) dedup locally.

    return {
        "ts_ns": tt_arr,
        "lat_e2e_ns": lat_e2e_ns,
        "lat_e2e_ts": lat_e2e_ts,
        "lat_me_ns": lat_me_ns,
        "lat_me_ts": lat_me_ts,
        "lat_hd_ns": lat_hd_ns,
        "lat_hd_ts": lat_hd_ts,
    }


def latency_p99_in_window(lat_ts: np.ndarray, lat_ns: np.ndarray,
                          start_ns: int, end_ns: int,
                          min_samples: int = MIN_LATENCY_SAMPLES) -> float:
    """p99 of the latency stream within [start_ns, end_ns] (window on lat_ts).
    Rejects non-positive latencies (clock-skew artefacts). Returns NaN if
    fewer than min_samples valid observations."""
    if len(lat_ts) == 0:
        return np.nan
    m = (lat_ts >= start_ns) & (lat_ts <= end_ns) & (lat_ns > 0)
    if int(m.sum()) < min_samples:
        return np.nan
    return float(np.quantile(lat_ns[m], 0.99))


def latency_p50p99_in_window(lat_ts: np.ndarray, lat_ns: np.ndarray,
                             start_ns: int, end_ns: int,
                             min_samples: int = MIN_LATENCY_SAMPLES
                             ) -> tuple[float, float]:
    """Return (p50, p99) of the latency stream in the window. NaN, NaN if
    below min_samples."""
    if len(lat_ts) == 0:
        return (np.nan, np.nan)
    m = (lat_ts >= start_ns) & (lat_ts <= end_ns) & (lat_ns > 0)
    if int(m.sum()) < min_samples:
        return (np.nan, np.nan)
    a = lat_ns[m]
    return (float(np.quantile(a, 0.50)), float(np.quantile(a, 0.99)))


def qsim_p50p99_in_window(arrivals_ns: np.ndarray, service_ns: int,
                          min_samples: int = MIN_LATENCY_SAMPLES
                          ) -> tuple[float, float]:
    """G/D/1 Lindley recursion on the provided arrival stream (already
    restricted to the window and sorted). Returns (p50, p99) of per-message
    system time (departure − arrival), in ns. NaN, NaN if below min_samples.

    Arrivals must be the raw upstream point process (transactTime T_m from
    MDP3) — NOT post-decode timestamps like handlerendtim, which are already
    smoothed by the physical receive queue and mask the Hawkes burstiness
    the model is supposed to expose.

    Vectorised: d_i = max(a_i, d_{i-1}) + s. Rewrite as
    d_i − i·s = max_{j ≤ i}(a_j − (j−1)·s),
    then add i·s back and subtract a_i to recover the system time.
    """
    if len(arrivals_ns) < min_samples:
        return (np.nan, np.nan)
    a = arrivals_ns.astype(np.int64, copy=False)
    n = len(a)
    idx = np.arange(1, n + 1, dtype=np.int64)
    # d_i = max_{j≤i}(a_j + (i−j+1)·s)  =>  d_i − i·s = max_{j≤i}(a_j − (j−1)·s) + s
    running = np.maximum.accumulate(a - (idx - 1) * service_ns)
    depart = running + idx * service_ns
    sys_time = depart - a
    return (float(np.quantile(sys_time, 0.50)),
            float(np.quantile(sys_time, 0.99)))


# ---------------------------------------------------------------------------
# Window logic
# ---------------------------------------------------------------------------


def make_windows(ts_ns: np.ndarray, window_minutes: int) -> pd.DataFrame:
    """Split an array of arrival timestamps (int64 ns) into consecutive
    windows of `window_minutes`, aligned to the wall-clock hour."""
    if len(ts_ns) == 0:
        return pd.DataFrame(columns=["window_id", "start_ns", "end_ns", "n_events"])
    win_ns = window_minutes * 60 * 1_000_000_000
    # Align to the first window boundary at or before the earliest event
    first = ts_ns[0]
    win_index = (ts_ns - first) // win_ns
    df = pd.DataFrame({"ts_ns": ts_ns, "window_id": win_index.astype("int64")})
    grouped = df.groupby("window_id", sort=True).agg(
        start_ns=("ts_ns", "min"),
        end_ns=("ts_ns", "max"),
        n_events=("ts_ns", "size"),
    ).reset_index()
    return grouped


def hawkes_fit_one_window(ts_ns_in_window: np.ndarray) -> dict:
    """Fit exp-Hawkes on one window's arrivals; return a small dict of the
    five Stage-1 arrival-side observables. If the fit is unreliable (too few
    events, non-convergence) return NaN fields.
    """
    # Two subtleties:
    #  (a) Dedup same-ns ties. The Ogata recursion inside fit_hawkes needs
    #      strictly-monotone arrivals; upstream `ts_ns` is now the raw
    #      (undeduplicated) stream so the qsim can see burst depth.
    #  (b) Subtract the window's first arrival in INT64 before converting to
    #      float64/1e9. At the 2026 epoch (ts_ns ~ 1.7e18) the float64
    #      mantissa gives ~380 ns resolution, so a naive `.astype(float64)/1e9`
    #      first loses sub-microsecond precision that the exp-Hawkes MLE
    #      needs. `t - t[0]` afterward cannot recover what was already
    #      quantised.
    ts_int = np.unique(ts_ns_in_window)
    if len(ts_int) < 2:
        return {"mu": np.nan, "alpha": np.nan, "beta": np.nan,
                "n_branch": np.nan, "lambda_bar": np.nan,
                "converged": False, "final_nll": np.nan}
    t_sec = (ts_int - ts_int[0]).astype(np.float64) / 1e9
    try:
        fit = fit_hawkes(t_sec)
    except (ValueError, RuntimeError):
        return {"mu": np.nan, "alpha": np.nan, "beta": np.nan,
                "n_branch": np.nan, "lambda_bar": np.nan,
                "converged": False, "final_nll": np.nan}
    return {
        "mu": fit["mu"], "alpha": fit["alpha"], "beta": fit["beta"],
        "n_branch": fit["n_branch"], "lambda_bar": fit["lambda_bar"],
        "converged": fit["converged"], "final_nll": fit["final_nll"],
    }


# ---------------------------------------------------------------------------
# Tail statistics per window
# ---------------------------------------------------------------------------


def fill_p95_absmarkout(fill: pd.DataFrame, start_ns: int, end_ns: int) -> float:
    """95th percentile of |markout_1s| for fills whose exec_ts is in
    [start_ns, end_ns]. NaN if fewer than 20 fills."""
    m = (fill["exec_ts"] >= start_ns) & (fill["exec_ts"] <= end_ns)
    a = fill.loc[m, "markout_1s"].abs().dropna()
    if len(a) < 20:
        return np.nan
    return float(a.quantile(0.95))


def fill_p50p95_absmarkout(fill: pd.DataFrame, start_ns: int, end_ns: int
                           ) -> tuple[float, float]:
    """Return (p50, p95) of |markout_1s| in the window."""
    m = (fill["exec_ts"] >= start_ns) & (fill["exec_ts"] <= end_ns)
    a = fill.loc[m, "markout_1s"].abs().dropna()
    if len(a) < 20:
        return (np.nan, np.nan)
    return (float(a.quantile(0.50)), float(a.quantile(0.95)))


def return_p95_from_fill(fill: pd.DataFrame, start_ns: int, end_ns: int,
                         stride_ns: int = 1_000_000_000) -> float:
    """Placeholder return-tail proxy: 95th-percentile of |Δ exec_price_native|
    over stride-sized bins inside the window. Weak proxy — the real return
    series should come from the bbbochg mid, joined at build time. For
    Stage-1 we accept the proxy so the panel has all five columns."""
    m = (fill["exec_ts"] >= start_ns) & (fill["exec_ts"] <= end_ns)
    sub = fill.loc[m, ["exec_ts", "exec_price_native"]].dropna()
    if len(sub) < 20:
        return np.nan
    sub = sub.sort_values("exec_ts")
    bins = (sub["exec_ts"] - start_ns) // stride_ns
    binned = sub.groupby(bins)["exec_price_native"].agg(["first", "last"])
    returns = (binned["last"] - binned["first"]).abs()
    if len(returns) < 5:
        return np.nan
    return float(returns.quantile(0.95))


def return_p50p95_from_fill(fill: pd.DataFrame, start_ns: int, end_ns: int,
                            stride_ns: int = 1_000_000_000
                            ) -> tuple[float, float]:
    """Return (p50, p95) of the per-stride absolute return proxy."""
    m = (fill["exec_ts"] >= start_ns) & (fill["exec_ts"] <= end_ns)
    sub = fill.loc[m, ["exec_ts", "exec_price_native"]].dropna()
    if len(sub) < 20:
        return (np.nan, np.nan)
    sub = sub.sort_values("exec_ts")
    bins = (sub["exec_ts"] - start_ns) // stride_ns
    binned = sub.groupby(bins)["exec_price_native"].agg(["first", "last"])
    returns = (binned["last"] - binned["first"]).abs()
    if len(returns) < 5:
        return (np.nan, np.nan)
    return (float(returns.quantile(0.50)), float(returns.quantile(0.95)))


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def build_hourly_panel(msg_tape_csv: str,
                       fill_tape_parquet: str,
                       out_parquet: str,
                       window_minutes: int = DEFAULT_WINDOW_MIN,
                       min_events: int = DEFAULT_MIN_EVENTS,
                       session_date: Optional[str] = None,
                       symbol: Optional[str] = None,
                       qsim_service_us: int = DEFAULT_QSIM_SERVICE_US
                       ) -> pd.DataFrame:
    """Fit Hawkes + tail stats per window; write one-row-per-window parquet."""
    print(f"[hourly_panel] load msg_tape {msg_tape_csv}", file=sys.stderr)
    msg = load_msgtape_latencies(msg_tape_csv)
    ts_ns = msg["ts_ns"]
    print(f"[hourly_panel] {len(ts_ns):,} arrivals "
          f"(e2e={len(msg['lat_e2e_ns']):,} "
          f"me={len(msg['lat_me_ns']):,} hd={len(msg['lat_hd_ns']):,})",
          file=sys.stderr)

    windows = make_windows(ts_ns, window_minutes)
    if len(windows) == 0:
        empty = pd.DataFrame(columns=[
            "session_date", "symbol", "window_id", "start_ns", "end_ns",
            "n_events", "kept", "qsim_service_us",
            "mu", "alpha", "beta", "n_branch", "lambda_bar",
            "log_mean_lambda",
            "p50_lat_me_ns", "p99_lat_me_ns",
            "p50_lat_handler_ns", "p99_lat_handler_ns",
            "p50_qsim_ns", "p99_qsim_ns",
            "p50_absmark", "p95_absmark",
            "p50_absret", "p95_absret",
            "converged",
        ])
        Path(out_parquet).parent.mkdir(parents=True, exist_ok=True)
        empty.to_parquet(out_parquet, compression="snappy", index=False)
        return empty

    print(f"[hourly_panel] {len(windows)} windows, min_events={min_events}",
          file=sys.stderr)

    fill = pd.read_parquet(fill_tape_parquet)

    rows = []
    for _, w in windows.iterrows():
        start_ns = int(w["start_ns"])
        end_ns = int(w["end_ns"])
        window_duration_sec = (end_ns - start_ns) / 1e9
        # Zero-duration windows (identical-timestamp dedup collapse) are
        # kept in the panel as kept=False so their existence is auditable
        # rather than silently dropped.
        keep = (w["n_events"] >= min_events) and (window_duration_sec > 0)
        row: dict = {
            "session_date": session_date or "",
            "symbol": symbol or "",
            "window_id": int(w["window_id"]),
            "start_ns": start_ns,
            "end_ns": end_ns,
            "n_events": int(w["n_events"]),
            "kept": bool(keep),
            "qsim_service_us": int(qsim_service_us),
        }
        if keep:
            mask = (ts_ns >= start_ns) & (ts_ns <= end_ns)
            window_ts = ts_ns[mask]
            hf = hawkes_fit_one_window(window_ts)
            row.update(hf)
            mean_lambda = w["n_events"] / window_duration_sec
            row["log_mean_lambda"] = float(np.log(max(mean_lambda, 1e-9)))

            p50_me, p99_me = latency_p50p99_in_window(
                msg["lat_me_ts"], msg["lat_me_ns"], start_ns, end_ns)
            p50_hd, p99_hd = latency_p50p99_in_window(
                msg["lat_hd_ts"], msg["lat_hd_ns"], start_ns, end_ns)
            row["p50_lat_me_ns"] = p50_me
            row["p99_lat_me_ns"] = p99_me
            row["p50_lat_handler_ns"] = p50_hd
            row["p99_lat_handler_ns"] = p99_hd

            # Queue sim runs on the RAW matching-engine arrival stream T_m
            # (transactTime, sorted, same-ns duplicates KEPT). Same-ns
            // records are distinct SBE messages that each require one
            // service slot — dropping them would erase the burst structure
            // the paper is measuring.
            p50_qs, p99_qs = qsim_p50p99_in_window(
                window_ts, qsim_service_us * 1_000)
            row["p50_qsim_ns"] = p50_qs
            row["p99_qsim_ns"] = p99_qs

            p50_am, p95_am = fill_p50p95_absmarkout(fill, start_ns, end_ns)
            p50_ar, p95_ar = return_p50p95_from_fill(fill, start_ns, end_ns)
            row["p50_absmark"] = p50_am
            row["p95_absmark"] = p95_am
            row["p50_absret"] = p50_ar
            row["p95_absret"] = p95_ar
        else:
            for k in ("mu", "alpha", "beta", "n_branch", "lambda_bar",
                      "log_mean_lambda",
                      "p50_lat_me_ns", "p99_lat_me_ns",
                      "p50_lat_handler_ns", "p99_lat_handler_ns",
                      "p50_qsim_ns", "p99_qsim_ns",
                      "p50_absmark", "p95_absmark",
                      "p50_absret", "p95_absret"):
                row[k] = np.nan
            row["converged"] = False
        rows.append(row)

    if len(rows) == 0:
        empty = pd.DataFrame(columns=[
            "session_date", "symbol", "window_id", "start_ns", "end_ns",
            "n_events", "kept", "qsim_service_us",
            "mu", "alpha", "beta", "n_branch", "lambda_bar",
            "log_mean_lambda",
            "p50_lat_me_ns", "p99_lat_me_ns",
            "p50_lat_handler_ns", "p99_lat_handler_ns",
            "p50_qsim_ns", "p99_qsim_ns",
            "p50_absmark", "p95_absmark",
            "p50_absret", "p95_absret",
            "converged",
        ])
        Path(out_parquet).parent.mkdir(parents=True, exist_ok=True)
        empty.to_parquet(out_parquet, compression="snappy", index=False)
        print(f"[hourly_panel] no usable windows — wrote empty parquet to {out_parquet}",
              file=sys.stderr)
        return empty

    df = pd.DataFrame(rows)
    Path(out_parquet).parent.mkdir(parents=True, exist_ok=True)
    df.to_parquet(out_parquet, compression="snappy", index=False)
    print(f"[hourly_panel] wrote {len(df):,} window rows to {out_parquet} "
          f"({df['kept'].sum():,} kept, {len(df) - df['kept'].sum():,} filtered)",
          file=sys.stderr)
    return df


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True)
    ap.add_argument("--fill-tape", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--window-minutes", type=int, default=DEFAULT_WINDOW_MIN)
    ap.add_argument("--min-events", type=int, default=DEFAULT_MIN_EVENTS)
    ap.add_argument("--session-date", default=None)
    ap.add_argument("--symbol", default=None)
    ap.add_argument("--qsim-service-us", type=int,
                    default=DEFAULT_QSIM_SERVICE_US,
                    help="G/D/1 service time (μs) for the modelled qlen tail")
    args = ap.parse_args()
    build_hourly_panel(
        msg_tape_csv=args.msg_tape,
        fill_tape_parquet=args.fill_tape,
        out_parquet=args.out,
        window_minutes=args.window_minutes,
        min_events=args.min_events,
        session_date=args.session_date,
        symbol=args.symbol,
        qsim_service_us=args.qsim_service_us,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
