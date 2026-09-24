# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.). Licensed under the MIT License.
"""Build per-cell empirical delay samplers for the tail-aware Kaspar model.

Reads all *.parquet panels in --panel-dir (schema: see hourly_panel.py),
keeps rows with ``kept & converged``, drops NaN ``lambda_bar`` / ``n_branch``,
and builds a 5x5 equal-quantile grid on those two Hawkes drivers.

For every corpus SESSION touched by at least one kept+converged window we
re-open the underlying msgtape CSV in --tape-dir to get raw per-message
latency samples (per-window p50/p99 alone cannot rebuild an empirical CDF).
Samples are reservoir-sampled uniformly across the corpus, capped at
--max-samples per (side, cell), so the output CSV stays small.

Two sides:
    inbound  = handlerendtim - sendingTime   (CME gateway -> databento handler)
    outbound = sendingTime   - transactTime  (matching engine egress)

For each cell, an empirical CDF is emitted at fixed quantiles
    [0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99, 0.995, 0.999].

The cancel side is the order side plus a constant offset ``k`` in
microseconds. ``k`` is pulled from kaspr/config/kaspr.ini (if there is a
``set_delay(order, cancel)``-style entry there); otherwise the default
``k=40`` is used and a warning is logged.

Output:
    * --out-csv  columns: side, action, lambda_bin, n_bin, quantile, latency_us
                 rows sorted by (side, action, lambda_bin, n_bin, quantile)
    * --out-json sidecar: {"lambda_edges": [...], "n_edges": [...],
                            "quantiles": [...], "k_us": <int>}

CLI:
    python3 -m arrival_paper.build_delay_samplers \\
        --panel-dir /vast/.../tapes/318/panel_s7 \\
        --tape-dir  /vast/.../tapes/318/message \\
        --out-csv   arrival_paper/delay_samplers.csv \\
        --out-json  arrival_paper/delay_samplers.json
"""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import re
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd

from arrival_paper.grid_scan import equal_quantile_bins, assign_bin


GRID = 5
QUANTILES = [0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99, 0.995, 0.999]
DEFAULT_MAX_SAMPLES = 500_000
DEFAULT_MIN_SAMPLES = 1_000
DEFAULT_K_US = 40
KASPR_INI = "kaspr/config/kaspr.ini"


# ---------------------------------------------------------------------------
# k (cancel offset) resolver
# ---------------------------------------------------------------------------


def resolve_k_us(ini_path: Optional[str], default_k: int = DEFAULT_K_US) -> int:
    """Return cancel_us - order_us from kaspr.ini's set_delay entry.

    Looks for either a literal ``set_delay(order, cancel)`` call site or an
    ini key pair. Returns ``default_k`` and warns to stderr if nothing
    matches (which is the case in the current tree)."""
    if not ini_path or not Path(ini_path).exists():
        print(f"[build_delay_samplers] WARN: {ini_path} not found; "
              f"using default k={default_k} us", file=sys.stderr)
        return default_k
    text = Path(ini_path).read_text()
    m = re.search(
        r"set_delay\s*\(\s*(\d+)\s*(?:us)?\s*,\s*(\d+)\s*(?:us)?\s*\)",
        text)
    if m:
        order, cancel = int(m.group(1)), int(m.group(2))
        return cancel - order
    order_m = re.search(r"^\s*ob_delay_us\s+(\d+)", text, re.MULTILINE)
    cancel_m = re.search(r"^\s*ob_cancel_delay_us\s+(\d+)", text, re.MULTILINE)
    if order_m and cancel_m:
        return int(cancel_m.group(1)) - int(order_m.group(1))
    print(f"[build_delay_samplers] WARN: no set_delay(...) in {ini_path}; "
          f"using default k={default_k} us", file=sys.stderr)
    return default_k


# ---------------------------------------------------------------------------
# Panel -> (session -> list of (start_ns, end_ns, lambda_bin, n_bin)) map
# ---------------------------------------------------------------------------


def load_kept_windows(panel_dir: str) -> pd.DataFrame:
    """Concatenate every *.parquet in panel_dir, keep converged windows,
    drop NaN Hawkes drivers. Empty parquets (no windows) are skipped."""
    files = sorted(Path(panel_dir).glob("*.parquet"))
    if not files:
        raise FileNotFoundError(f"no *.parquet in {panel_dir}")
    dfs: list[pd.DataFrame] = []
    for f in files:
        try:
            d = pd.read_parquet(f)
        except Exception as e:
            print(f"[build_delay_samplers] SKIP {f.name}: {e}", file=sys.stderr)
            continue
        if len(d) == 0:
            continue
        dfs.append(d)
    if not dfs:
        raise RuntimeError(f"no non-empty panels in {panel_dir}")
    df = pd.concat(dfs, ignore_index=True)
    df = df[df["kept"] & df["converged"]].copy()
    df = df.dropna(subset=["lambda_bar", "n_branch"])
    print(f"[build_delay_samplers] loaded {len(files)} panels, "
          f"{len(df):,} kept+converged windows", file=sys.stderr)
    return df


def build_grid(df: pd.DataFrame, grid: int = GRID
               ) -> tuple[np.ndarray, np.ndarray, pd.DataFrame]:
    """Attach lambda_bin, n_bin columns to df; return (lam_edges, n_edges, df)."""
    lam = df["lambda_bar"].to_numpy()
    n_br = df["n_branch"].to_numpy()
    lam_edges = equal_quantile_bins(lam, grid)
    n_edges = equal_quantile_bins(n_br, grid)
    df = df.copy()
    df["lambda_bin"] = assign_bin(lam, lam_edges)
    df["n_bin"] = assign_bin(n_br, n_edges)
    return lam_edges, n_edges, df


# ---------------------------------------------------------------------------
# Msgtape reader (streaming, low memory)
# ---------------------------------------------------------------------------


def _tape_path_for_session(tape_dir: str, session_date: str, symbol: str) -> Optional[Path]:
    """Locate one msgtape CSV for a (session_date, symbol). The panel batch
    writes plain CSV; tape_batch may write .gz. Return the first match, or
    None if neither exists."""
    plain = Path(tape_dir) / f"{session_date}.{symbol}.csv"
    if plain.exists():
        return plain
    gz = Path(tape_dir) / f"{session_date}.{symbol}.csv.gz"
    if gz.exists():
        return gz
    return None


def iter_msgtape(tape_path: Path):
    """Yield (transactTime, sendingTime, handlerendtim) per row.

    Rows with any non-integer timestamp are skipped silently, matching
    hourly_panel.py's tolerance for malformed lines."""
    opener = gzip.open if str(tape_path).endswith(".gz") else open
    with opener(tape_path, "rt", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        i_tt = header.index("transactTime")
        i_st = header.index("sendingTime")
        i_he = header.index("handlerendtim")
        for row in reader:
            try:
                yield int(row[i_tt]), int(row[i_st]), int(row[i_he])
            except (IndexError, ValueError):
                continue


# ---------------------------------------------------------------------------
# Reservoir sampler per (side, cell)
# ---------------------------------------------------------------------------


class Reservoir:
    """Uniform reservoir sampler, single stream. Capacity = max_samples."""

    __slots__ = ("cap", "buf", "n", "rng")

    def __init__(self, cap: int, rng: np.random.Generator):
        self.cap = cap
        self.buf: list[int] = []
        self.n = 0
        self.rng = rng

    def offer(self, x: int) -> None:
        self.n += 1
        if len(self.buf) < self.cap:
            self.buf.append(x)
            return
        # Vitter Algorithm R
        j = int(self.rng.integers(0, self.n))
        if j < self.cap:
            self.buf[j] = x

    def offer_many(self, xs: np.ndarray) -> None:
        # Vectorised: fill remaining capacity from head, then replace at
        # rate cap/n_so_far for each subsequent draw.
        remain = self.cap - len(self.buf)
        if remain > 0:
            take = xs[:remain]
            self.buf.extend(int(v) for v in take)
            self.n += len(take)
            xs = xs[remain:]
        if len(xs) == 0:
            return
        n0 = self.n
        ns = n0 + 1 + np.arange(len(xs), dtype=np.int64)  # 1-indexed positions
        js = self.rng.integers(0, ns)
        keep = js < self.cap
        for xi, ji, kk in zip(xs.tolist(), js.tolist(), keep.tolist()):
            if kk:
                self.buf[int(ji)] = int(xi)
        self.n += len(xs)

    def values(self) -> np.ndarray:
        return np.asarray(self.buf, dtype=np.int64)


# ---------------------------------------------------------------------------
# Cell -> sample collection
# ---------------------------------------------------------------------------


def collect_samples(df_windows: pd.DataFrame, tape_dir: str,
                    max_samples: int, seed: int, grid: int = GRID
                    ) -> dict[tuple[str, int, int], np.ndarray]:
    """Scan every msgtape CSV once. For each row, find the window it lands
    in (if any), read the window's (lambda_bin, n_bin), and offer the
    latency to that (side, cell) reservoir.

    Returns dict keyed by (side, lambda_bin, n_bin) with an ndarray of
    latency samples in ns.
    """
    rng = np.random.default_rng(seed)
    reservoirs: dict[tuple[str, int, int], Reservoir] = {}

    def get(side: str, lb: int, nb: int) -> Reservoir:
        k = (side, lb, nb)
        r = reservoirs.get(k)
        if r is None:
            r = Reservoir(max_samples, rng)
            reservoirs[k] = r
        return r

    for (session, symbol), sub in df_windows.groupby(
            ["session_date", "symbol"], sort=True):
        tape = _tape_path_for_session(tape_dir, str(session), str(symbol))
        if tape is None:
            print(f"[build_delay_samplers] no msgtape for "
                  f"{session}.{symbol}", file=sys.stderr)
            continue
        # For fast row-to-window lookup, sort windows by start_ns.
        wins = sub[["start_ns", "end_ns", "lambda_bin", "n_bin"]].to_numpy()
        wins = wins[np.argsort(wins[:, 0])]
        starts = wins[:, 0].astype(np.int64)
        ends = wins[:, 1].astype(np.int64)
        lbs = wins[:, 2].astype(np.int64)
        nbs = wins[:, 3].astype(np.int64)

        # Bucket rows into windows via searchsorted on transactTime. Doing
        # this in chunks keeps memory small; a raw NQ session is ~10M rows.
        chunk_tt: list[int] = []
        chunk_st: list[int] = []
        chunk_he: list[int] = []
        CHUNK = 200_000

        def flush():
            if not chunk_tt:
                return
            tt = np.asarray(chunk_tt, dtype=np.int64)
            st = np.asarray(chunk_st, dtype=np.int64)
            he = np.asarray(chunk_he, dtype=np.int64)
            # Which window (if any) does each row's transactTime fall into?
            idx = np.searchsorted(starts, tt, side="right") - 1
            in_win = (idx >= 0) & (idx < len(starts))
            in_win &= tt <= ends[np.clip(idx, 0, len(ends) - 1)]
            if not in_win.any():
                chunk_tt.clear(); chunk_st.clear(); chunk_he.clear()
                return
            sel = np.nonzero(in_win)[0]
            for i in sel:
                bi = idx[i]
                lb = int(lbs[bi]); nb = int(nbs[bi])
                v_tt = int(tt[i]); v_st = int(st[i]); v_he = int(he[i])
                if v_st > 0:
                    out = v_st - v_tt
                    if out > 0:
                        get("outbound", lb, nb).offer(out)
                if v_st > 0 and v_he > 0:
                    inb = v_he - v_st
                    if inb > 0:
                        get("inbound", lb, nb).offer(inb)
            chunk_tt.clear(); chunk_st.clear(); chunk_he.clear()

        n_rows = 0
        for v_tt, v_st, v_he in iter_msgtape(tape):
            chunk_tt.append(v_tt)
            chunk_st.append(v_st)
            chunk_he.append(v_he)
            n_rows += 1
            if len(chunk_tt) >= CHUNK:
                flush()
        flush()
        print(f"[build_delay_samplers] scanned {session}.{symbol}: "
              f"{n_rows:,} rows, {len(sub)} windows", file=sys.stderr)

    return {k: r.values() for k, r in reservoirs.items()}


# ---------------------------------------------------------------------------
# Emit CSV
# ---------------------------------------------------------------------------


def _quantiles_for(samples: np.ndarray, qs: list[float],
                   min_samples: int) -> list[float]:
    """Return NaN-filled list if the cell has fewer than min_samples."""
    if samples is None or len(samples) < min_samples:
        return [float("nan")] * len(qs)
    return [float(np.quantile(samples, q)) for q in qs]


def build_rows(samples: dict[tuple[str, int, int], np.ndarray],
               k_us: int, grid: int, min_samples: int,
               qs: list[float] = QUANTILES) -> list[dict]:
    """Emit one row per (side, action, cell, quantile).

    Rows for side=outbound also emit action=cancel, which is action=send
    shifted by k_us microseconds. Rows for side=inbound only ever have
    action=any (feed latency is not directional)."""
    rows: list[dict] = []
    for lb in range(grid):
        for nb in range(grid):
            for side, action in [("inbound", "any"),
                                 ("outbound", "send"),
                                 ("outbound", "cancel")]:
                # Cancel latencies are the "send" reservoir plus k_us*1000 ns.
                src = ("outbound", lb, nb) if side == "outbound" else ("inbound", lb, nb)
                ns = samples.get(src)
                if action == "cancel":
                    if ns is not None and len(ns) >= min_samples:
                        ns = ns + k_us * 1_000  # ns
                    # otherwise fall through to NaN emission
                qvals = _quantiles_for(ns, qs, min_samples)
                if all(pd.isna(v) for v in qvals):
                    print(f"[build_delay_samplers] WARN: low-sample cell "
                          f"side={side} action={action} "
                          f"lambda_bin={lb} n_bin={nb} "
                          f"(n={0 if ns is None else len(ns)} < {min_samples})",
                          file=sys.stderr)
                for q, v in zip(qs, qvals):
                    rows.append({
                        "side": side,
                        "action": action,
                        "lambda_bin": lb,
                        "n_bin": nb,
                        "quantile": q,
                        "latency_us": float("nan") if pd.isna(v) else v / 1000.0,
                    })
    return rows


def write_csv(rows: list[dict], out_csv: str) -> None:
    df = pd.DataFrame(rows)
    df = df.sort_values(["side", "action", "lambda_bin", "n_bin", "quantile"])
    Path(out_csv).parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(out_csv, index=False, float_format="%.6f")


def write_json(out_json: str, lam_edges: np.ndarray, n_edges: np.ndarray,
               k_us: int, qs: list[float]) -> None:
    payload = {
        "lambda_edges": [float(x) for x in lam_edges],
        "n_edges": [float(x) for x in n_edges],
        "quantiles": list(qs),
        "k_us": int(k_us),
    }
    Path(out_json).parent.mkdir(parents=True, exist_ok=True)
    Path(out_json).write_text(json.dumps(payload, indent=2))


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def build(panel_dir: str, tape_dir: str, out_csv: str, out_json: str,
          k_us: Optional[int] = None,
          kaspr_ini: str = KASPR_INI,
          max_samples: int = DEFAULT_MAX_SAMPLES,
          min_samples: int = DEFAULT_MIN_SAMPLES,
          seed: int = 42,
          grid: int = GRID) -> pd.DataFrame:
    """End-to-end: panel dir + tape dir -> CSV + JSON. Returns the emitted
    DataFrame (useful for tests)."""
    if k_us is None:
        k_us = resolve_k_us(kaspr_ini)
    df = load_kept_windows(panel_dir)
    lam_edges, n_edges, df = build_grid(df, grid)
    samples = collect_samples(df, tape_dir, max_samples, seed, grid)
    rows = build_rows(samples, k_us, grid, min_samples)
    write_csv(rows, out_csv)
    write_json(out_json, lam_edges, n_edges, k_us, QUANTILES)
    print(f"[build_delay_samplers] wrote {out_csv} ({len(rows)} rows) "
          f"and {out_json}; k_us={k_us}", file=sys.stderr)
    return pd.DataFrame(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--panel-dir", required=True,
                    help="directory of *.parquet panels from hourly_panel.py")
    ap.add_argument("--tape-dir", required=True,
                    help="directory of msgtape CSVs matching the panels")
    ap.add_argument("--out-csv", required=True)
    ap.add_argument("--out-json", required=True,
                    help="sidecar with grid edges + k_us + quantiles")
    ap.add_argument("--kaspr-ini", default=KASPR_INI,
                    help="config to read set_delay(order,cancel) from")
    ap.add_argument("--k-us", type=int, default=None,
                    help="override cancel-vs-order offset (us); "
                         "if unset, read from --kaspr-ini")
    ap.add_argument("--max-samples", type=int, default=DEFAULT_MAX_SAMPLES,
                    help="reservoir cap per (side, cell)")
    ap.add_argument("--min-samples", type=int, default=DEFAULT_MIN_SAMPLES,
                    help="cells with fewer samples emit NaN quantiles")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--grid", type=int, default=GRID)
    args = ap.parse_args()
    build(panel_dir=args.panel_dir,
          tape_dir=args.tape_dir,
          out_csv=args.out_csv,
          out_json=args.out_json,
          k_us=args.k_us,
          kaspr_ini=args.kaspr_ini,
          max_samples=args.max_samples,
          min_samples=args.min_samples,
          seed=args.seed,
          grid=args.grid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
